// JuraicField: the playable front end (Direct2D): mode select, toko (one
// player), versus (two players on this PC), replays, settings and key bindings.
//
//   JuraicField.exe                         normal start
//   JuraicField.exe --board <file>          start toko from a board (.jfchain of the generator or of its editor)
//   JuraicField.exe --replay <file>         play a replay
//   JuraicField.exe --screenshot <png> [n]  self test: play n frames by script, save a picture, exit
//       n > 0: toko (n ending in 1: stop in the middle of a wave, in 11: while the pieces of a wave fall;
//              in 2: versus; in 3: versus, saved and played as a replay; in 4: the same with two AIs; in 5: two AIs, the picture of the match)
//       n = 0: menu, n = -1: settings, n = -2: key bindings, n = -3: bindings of the other controllers (and a check of the binding rules)
//       n = -4: the screen before a match over a network, n = -5: the settings and the menu, a direction held down (the steps are counted),
//       n = -6: the end of a match against an AI, the mouse button held down on its level (the steps are counted, and
//              a window is asked whether it keeps the mouse while the button is down),
//       n = -11 to -14: the four parts of the settings
//              in 8: a match over a line inside the program that delays and loses, saved and played as a replay)
//   JuraicField.exe --check-replay <file>   self test: play a replay without a window, write <file>.result, exit
//   JuraicField.exe --net-test host <port> <result file>            self test: a match over UDP on this machine, without
//                              (host4: the host listens at 127.0.0.1 instead of ::1; hostall: at every address of the machine)
//   JuraicField.exe --net-test join <address> <port> <result file>  a window: two programs, their result files must be equal
//
// Windows: Win32, Direct2D, XAudio2 and Media Foundation. Elsewhere: SDL (play/platform_sdl.h), the drawing engine
// of play/raster.h; the program takes the same arguments.
#ifdef _WIN32
#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <wincodec.h>
#else
#include "platform_sdl.h"
#endif
#include "../ai/agents.h"
#include "../gui/gui_editor.h"
#include "audio.h"
#include "input.h"
#include "media.h"
#include "netplay.h"
#include "replay.h"
#include "session.h"
#include "settings.h"
#include "udp.h"
#include "view.h"
#include <chrono>
#include <map>
#include <memory>
#include <set>
#ifdef __ANDROID__
#include <SDL3/SDL_main.h> // (main is called by the activity of SDL; last, as it renames the word main)
#endif

using namespace juraic;
using namespace play;

namespace {

enum class Screen { Menu, Toko, Versus, Replay, Settings, Bindings, NetLobby, Net };

// ---- the application --------------------------------------------------------------------
#ifdef _WIN32
LRESULT CALLBACK windowProc(HWND w, UINT m, WPARAM wp, LPARAM lp); // (below; a self test sends it the messages of the mouse)
#endif

struct App {
#ifdef _WIN32
    HWND window = nullptr;
    ID2D1Factory* factory = nullptr;
    IDWriteFactory* dwrite = nullptr;
    ID2D1HwndRenderTarget* target = nullptr;
#else
    Window window;
    bool quit = false; // the window is to close
#endif
    Gfx gfx;
    Audio audio;
    Settings settings;
    InputConfig input;
    Pads pads;
    std::filesystem::path exeDirectory;
    // The files that come with the game (the pictures, the music, the fonts) are read from dataDirectory; the settings,
    // the replays and one's own pictures and music are in userDirectory. Both are the folder of the program, except in
    // an application bundle of macOS (play/platform_sdl.h, programFolders).
    std::filesystem::path dataDirectory, userDirectory;
    Screen screen = Screen::Menu;
    int menuIndex = 0, settingIndex = 0, bindIndex = 0, bindLayout = 0;
    int settingSection = 0;     // the part of the settings screen that is shown (SettingSection)
    // The picture behind the game and the music (media.h). The music of a file is read on a thread
    // of its own and started when that is done; musicWanted is the file the settings ask for in the
    // scene of the moment (musicScene). The samples of the files the settings name are kept
    // (musicKept): going from the menu into a match and back reads no file again.
    Backdrop backdrop[PICTURE_PLACES];        // (one for each place a picture can stand in: Settings::background)
    bool backdropShown[PICTURE_PLACES]{};     // the last frame drawn had the picture of that place
    std::wstring backdropFailed[PICTURE_PLACES]; // the picture that could not be read (said once)
    std::wstring musicWanted, musicPlaying;
    std::future<std::pair<std::wstring, MusicSamples>> musicJob;
    struct KeptMusic {
        std::shared_ptr<const std::vector<uint8_t>> pcm;
        PcmFormat format{};
    };
    std::map<std::wstring, KeptMusic> musicKept;
    std::set<std::wstring> musicFailed; // files that could not be read: said once, not tried again until the folder is looked at again
    int musicSceneForced = -1;          // (the self test: this scene, whatever the screen)
    bool settingInside = false; // the cursor is among its settings (settingIndex), not on the list of the parts
    // A direction of the menus that is held down (heldStep): the frames each of NAV_UP, NAV_DOWN, NAV_LEFT and NAV_RIGHT
    // has been down since a press that counted in the list the cursor is in (0: no such press is being held), that
    // list, and the frame each was last looked at.
    int dirHeld[4]{}, dirPlace = -1;
    int64_t dirSeen[4]{}, uiFrame = 0; // (uiFrame: counted by update)
    unsigned selfNav = 0, selfNavWas = 0; // (a self test: the directions of the menus it holds down, in this frame and in the one before)
    // The mouse, as the window is told: the left button is down, and where the pointer was last (in the canvas). At the
    // end of a match the button held down on a level goes on changing it: pickNow is the pick it went down on,
    // pickHeld the frames since (0: nothing is held).
    bool mouseDown = false;
    float mouseX = 0, mouseY = 0;
    int pickNow = -1, pickHeld = 0;
    bool binding = false; // waiting for the key or button of bindIndex
    unsigned tokoHeld = 0; // actions held in the previous frame of toko
    Lever lever[2];        // the directions of each person, when the diagonal is off
    Toko toko;
    Versus versus;
    Viewer viewer;
    // A match over a network (play/netplay.h) and the screen before it: the own name, the address of the host that is
    // called, the port a host listens on. The three are kept in the settings file. An address is shown only while F1
    // has asked for it (the screen may be on a stream).
    NetPlay net;
    std::wstring netName = L"Player", netAddress;
    int netPort = NET_PORT, netDevice[2]{};
    int lobbyIndex = 0;
    bool lobbyEditing = false, showAddress = false;
    int netQuit = 0;                       // frames left in which a second Esc leaves the match
    std::vector<std::string> ownAddressList; // (looked up when a room is opened)
    std::wstring typed;                    // the characters typed since the last frame (WM_CHAR)
    std::wstring message; // one line shown at the bottom
    int messageTimer = 0;
    bool keyWas[256]{};
    bool focused = true;
    bool selfTest = false; // a self test (--screenshot): no key and no controller of the machine is read, and no settings file is written
    bool clicked = false; // the left mouse button went down since the last frame, at (clickX, clickY) of the canvas
    float clickX = 0, clickY = 0;
    unsigned navHeld = 0, navWas = 0; // menu actions from all controllers
    std::vector<uint64_t> padWas;

    void say(const std::wstring& text) { message = text, messageTimer = 240; }
    std::filesystem::path iniPath() const { return userDirectory / L"JuraicField.ini"; }
    // A picture or a piece of music by its place below "background" or "bgm": one's own if there is one, else the game's.
    std::filesystem::path mediaFile(const wchar_t* kind, const std::filesystem::path& name) const {
        const std::filesystem::path own = userDirectory / kind / name;
        std::error_code ignored;
        return userDirectory == dataDirectory || std::filesystem::exists(own, ignored) ? own : dataDirectory / kind / name;
    }
    // The files of a folder below "background" or "bgm", of both places, each name once, in the order of the names.
    template <size_t N> std::vector<std::wstring> mediaFiles(const wchar_t* kind, const wchar_t* folder, const wchar_t* const (&endings)[N]) const {
        std::vector<std::wstring> names = filesOf(userDirectory / kind / folder, endings);
        if (dataDirectory == userDirectory) return names;
        for (const std::wstring& name : filesOf(dataDirectory / kind / folder, endings))
            if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
        std::sort(names.begin(), names.end());
        return names;
    }

    // The buttons of a person for the game, from the actions held: with the quick drop on, up
    // drops the pair; with the diagonal off, the directions go through a lever of four ways.
    // quickDrop: the setting of the game that is played - this machine's, or in a match over a network the
    // host's (play/netplay.h, travels), so that both have the button or neither.
    uint8_t personButtons(int seat, unsigned held, bool up, bool quickDrop) {
        uint8_t b = gameButtons(held);
        if (quickDrop && up) b |= BTN_DROP;
        return settings.diagonal ? b : lever[seat](b);
    }

    bool down(int vk) const { return focused && keyDown(vk); }
    // True on the frame the key goes down.
    bool pressed(int vk) {
        const bool now = down(vk), was = keyWas[vk];
        keyWas[vk] = now;
        return now && !was;
    }
    void forgetKeys() { // keys pressed in a dialog or while binding are not ours
        for (int vk = 0; vk < 256; ++vk) keyWas[vk] = down(vk);
        navWas = navHeld = ~0u;
    }
    // Menu input from the keyboard or any controller, true on the frame it goes down.
    enum Nav { NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT, NAV_OK, NAV_BACK, NAV_START };
    bool nav(Nav n) {
        const unsigned bit = 1u << n;
        const bool pad = ((navHeld & bit) && !(navWas & bit)) || ((selfNav & bit) && !(selfNavWas & bit));
        switch (n) {
        case NAV_UP: return pressed(VK_UP) | pad;
        case NAV_DOWN: return pressed(VK_DOWN) | pad;
        case NAV_LEFT: return pressed(VK_LEFT) | pad;
        case NAV_RIGHT: return pressed(VK_RIGHT) | pad;
        case NAV_OK: return pressed(VK_RETURN) | pad;
        case NAV_BACK: return pressed(VK_ESCAPE) | pad;
        default: return pad;
        }
    }
    // Is a direction of the menus down now (the keyboard, any controller, a self test)?
    bool navDown(Nav n) const {
        const int vk = n == NAV_UP ? VK_UP : n == NAV_DOWN ? VK_DOWN : n == NAV_LEFT ? VK_LEFT : n == NAV_RIGHT ? VK_RIGHT : 0;
        return (vk && down(vk)) || (navHeld >> n & 1) || (selfNav >> n & 1);
    }
    // A step by a direction (NAV_UP to NAV_RIGHT) that may be held down: true in the frame the direction goes down
    // and, while it stays down, again after HELD_WAIT frames - every `every` frames for a second, then every `later`
    // frames. The cursor of a list: 6 and 6. A value of the settings: 4 and 2, so that a value far away (99 rounds,
    // level 36) is reached without a press a step.
    // Only a press made in the list the cursor is in is carried on: a direction that was down already (the one that
    // opened the list, or one held while the screen changed) does nothing until it is pressed again.
    // heard: set when this step is one to be heard (of the fast ones every other).
    static constexpr int HELD_WAIT = 24, HELD_SLOW = 60;
    bool heldStep(Nav n, int every, int later, bool* heard = nullptr) {
        const int place = int(screen) * 4 + (screen == Screen::Settings ? 1 + int(settingInside) : 0);
        if (place != dirPlace) dirPlace = place, dirHeld[0] = dirHeld[1] = dirHeld[2] = dirHeld[3] = 0;
        int& held = dirHeld[n];
        // (not looked at in the frame before: whatever was held then is not this press)
        const bool first = nav(n), followed = uiFrame - dirSeen[n] <= 1;
        dirSeen[n] = uiFrame;
        held = first ? 1 : held > 0 && followed && navDown(n) ? held + 1 : 0;
        const int over = held - HELD_WAIT;
        const bool again = over > 0 && over % (over <= HELD_SLOW ? every : later) == 0;
        if (heard && (first || (again && over % 4 == 0))) *heard = true;
        return first || again;
    }
    void readPads() {
        pads.update();
        navWas = navHeld;
        navHeld = 0;
        for (int i = 0; focused && i < pads.count(); ++i) {
            const uint64_t raw = pads.raw(i);
            const int* bind = input.bind[layoutOf(2 + i, pads)];
            if (raw >> PAD_UP & 1) navHeld |= 1u << NAV_UP;
            if (raw >> PAD_DOWN & 1) navHeld |= 1u << NAV_DOWN;
            if (raw >> PAD_LEFT & 1) navHeld |= 1u << NAV_LEFT;
            if (raw >> PAD_RIGHT & 1) navHeld |= 1u << NAV_RIGHT;
            if (raw >> bind[ACT_MENU_OK] & 1) navHeld |= 1u << NAV_OK;
            if (raw >> bind[ACT_MENU_BACK] & 1) navHeld |= 1u << NAV_BACK;
            if (raw >> bind[ACT_START] & 1) navHeld |= 1u << NAV_START;
        }
    }

    // ---- settings file ----
    // The files of the two folders: looked at when the game starts and whenever the settings screen is
    // opened (a file put there while the game runs can be chosen without starting it again). The
    // choices stay with their files by name; a file that is gone means none.
    void scanMedia() {
        for (int k = 0; k < PICTURE_PLACES; ++k) settings.backgroundFiles[k] = mediaFiles(L"background", PICTURE_FOLDERS[k], PICTURE_ENDINGS);
        for (int k = 0; k < MUSIC_SCENES; ++k) settings.bgmFiles[k] = mediaFiles(L"bgm", MUSIC_FOLDERS[k], MUSIC_ENDINGS);
        for (int k = 0; k < PICTURE_PLACES; ++k) {
            // (the name follows the file found: it may have another ending than the name asked for, settings.h placeOf)
            settings.background[k] = placeOf(settings.backgroundFiles[k], settings.backgroundName[k]);
            settings.backgroundName[k] = settings.background[k] ? settings.backgroundFiles[k][size_t(settings.background[k] - 1)] : std::wstring();
        }
        for (int k = 0; k < MUSIC_SCENES; ++k) {
            settings.bgm[k] = placeOf(settings.bgmFiles[k], settings.bgmName[k]);
            settings.bgmName[k] = settings.bgm[k] ? settings.bgmFiles[k][size_t(settings.bgm[k] - 1)] : std::wstring();
        }
        musicFailed.clear(); // (a file that could not be read may have been put there anew)
    }
    void loadAll() {
        scanMedia();
        const std::filesystem::path file = iniPath();
        loadSettings(file, settings);
        std::ifstream in(file);
        std::string line;
        while (std::getline(in, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = line.substr(0, eq);
            const int v = atoi(line.c_str() + eq + 1);
            if (key == "device1") input.device[0] = v;
            if (key == "device2") input.device[1] = v;
            if (key == "netName" && eq + 1 < line.size()) netName = fromUtf8(line.substr(eq + 1)).substr(0, 12);
            if (key == "netAddress") netAddress = fromUtf8(line.substr(eq + 1)).substr(0, 80);
            if (key == "netPort" && v > 0 && v < 65536) netPort = v;
            for (int l = 0; l < LAYOUT_COUNT; ++l)
                for (int a = 0; a < ACT_COUNT; ++a)
                    if (key == std::string(layoutKeys[l]) + "." + std::to_string(a) && v >= 0 && v < 256) input.bind[l][a] = v;
        }
        // Two actions on one button (possible in files of older versions) could
        // make a controller useless: such a layout starts again from the defaults.
        for (int l = 0; l < LAYOUT_COUNT; ++l)
            if (input.clashes(l)) input.resetLayout(l);
    }
    void saveAll() {
        if (selfTest) return;
        saveSettings(iniPath(), settings);
        std::ofstream out(iniPath(), std::ios::app);
        out << "device1=" << input.device[0] << "\ndevice2=" << input.device[1] << '\n';
        out << "netName=" << toUtf8(netName) << "\nnetAddress=" << toUtf8(netAddress) << "\nnetPort=" << netPort << '\n';
        for (int l = 0; l < LAYOUT_COUNT; ++l)
            for (int a = 0; a < ACT_COUNT; ++a) out << layoutKeys[l] << '.' << a << '=' << input.bind[l][a] << '\n';
    }

    // The scene the music is chosen for: a match and its replay, toko and its replay, everything
    // else. On the settings screen the music of the setting under the cursor is heard, so that a
    // choice for a match or for toko can be listened to there.
    int musicScene() {
        if (musicSceneForced >= 0) return musicSceneForced;
        if (screen == Screen::Versus || screen == Screen::Net) return MUSIC_VERSUS;
        if (screen == Screen::Toko) return MUSIC_TOKO;
        if (screen == Screen::Replay) return viewer.replay.players == 2 ? MUSIC_VERSUS : MUSIC_TOKO;
        if (screen == Screen::Settings && settingInside) {
            SettingsTable table(settings);
            if (settingIndex >= 0 && settingIndex < int(table.items.size()))
                for (int k = 0; k < MUSIC_SCENES; ++k)
                    if (table.items[size_t(settingIndex)].key == MUSIC_KEYS[k]) return k;
        }
        return MUSIC_OTHER;
    }
    // The music follows the scene and the settings: called in every frame (and when the settings
    // have changed). A file is read once, on a thread of its own; while one is being read the
    // music that is wanted and already kept starts at once all the same.
    void mediaChanged() {
        const int scene = musicScene();
        musicWanted = settings.musicFile(scene); // (the file below "bgm", with the folder of its scene)
        audio.setMusicVolume(settings.bgmVolume);
    }
    void updateMedia() {
        mediaChanged();
        if (musicJob.valid() && musicJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            std::pair<std::wstring, MusicSamples> read = musicJob.get();
            if (read.second.pcm.empty()) {
                musicFailed.insert(read.first);
                say(L"BGM を読めませんでした: " + read.first);
            } else {
                musicKept[read.first] = KeptMusic{std::make_shared<const std::vector<uint8_t>>(std::move(read.second.pcm)), read.second.format};
            }
        }
        // (kept: what the three settings name, and what is playing)
        const auto named = [&](const std::wstring& name) {
            for (int k = 0; k < MUSIC_SCENES; ++k)
                if (settings.musicFile(k) == name) return true;
            return false;
        };
        for (auto it = musicKept.begin(); it != musicKept.end();) it = named(it->first) || it->first == musicPlaying ? std::next(it) : musicKept.erase(it);
        if (musicWanted != musicPlaying) {
            const auto kept = musicKept.find(musicWanted);
            if (musicWanted.empty() || musicFailed.count(musicWanted)) {
                audio.stopMusic();
                musicPlaying = musicWanted; // (a file that could not be read is not tried again)
            } else if (kept != musicKept.end()) {
                if (!audio.startMusic(kept->second.pcm, kept->second.format, settings.bgmVolume)) {
                    musicFailed.insert(musicWanted);
                    say(L"BGM を鳴らせませんでした: " + musicWanted);
                }
                musicPlaying = musicWanted;
            } else if (!musicPlaying.empty()) {
                audio.stopMusic(); // (silence until the file of this scene is read)
                musicPlaying.clear();
            }
        }
        if (musicJob.valid()) return;
        // The next file to read: the one wanted now, then the others the settings name (so that a
        // scene begins with its music).
        std::wstring next = !musicWanted.empty() && !musicKept.count(musicWanted) && !musicFailed.count(musicWanted) ? musicWanted : std::wstring();
        for (int k = 0; k < MUSIC_SCENES && next.empty(); ++k)
            if (const std::wstring file = settings.musicFile(k); !file.empty() && !musicKept.count(file) && !musicFailed.count(file)) next = file;
        if (next.empty()) return;
        const std::filesystem::path file = mediaFile(L"bgm", next);
        musicJob = std::async(std::launch::async, [file, name = next] {
#ifdef _WIN32
            const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
            std::pair<std::wstring, MusicSamples> out{name, readMusic(file)};
            if (com) CoUninitialize();
            return out;
#else
            return std::pair<std::wstring, MusicSamples>{name, readMusic(file)};
#endif
        });
    }

#ifdef _WIN32
    void createFonts() {
        const float sizes[] = {18, 24, 36, 60};
        for (int i = 0; i < 4; ++i) {
            dwrite->CreateTextFormat(L"Yu Gothic UI", nullptr, i >= 2 ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                     DWRITE_FONT_STRETCH_NORMAL, sizes[i], L"ja-jp", &gfx.font[i]);
            gfx.font[i]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }
    void bind(ID2D1RenderTarget* rt) {
        if (gfx.brush) gfx.brush->Release();
        for (Backdrop& b : backdrop) b.release();
        gfx.rt = rt;
        rt->CreateSolidColorBrush(colorF(0, 0, 0), &gfx.brush);
    }
    bool createTarget() {
        RECT rc;
        GetClientRect(window, &rc);
        if (FAILED(factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
                                                   D2D1::HwndRenderTargetProperties(window, D2D1::SizeU(UINT32(rc.right), UINT32(rc.bottom))), &target)))
            return false;
        bind(target);
        return true;
    }
#endif

    // ---- files ----
    bool loadBoardFile(const std::filesystem::path& path) {
        try {
            std::ifstream in(path, std::ios::binary);
            std::string magic;
            in >> magic;
            in.close();
            BitField f;
            if (magic == "JURAIC_FIELD_BOARD") {
                f = juraic_gui::bitsFromBoard(juraic_gui::loadBoard(path));
            } else {
                // A result of the generator: the board right after the last pair.
                // Take the last pair away so that finding it is the exercise.
                const Solution s = juraic_gui::loadRecord(path).solution;
                f = s.field;
                if (s.firePairs) f.erase(pairCells(s.field, std::countr_zero(s.firePairs)));
            }
            f.drop();
            if (f.hasClear()) say(L"この盤面は最初から消える形を含んでいます（最初の手で消えます）");
            toko.startField = GameField{};
            toko.startField.f = f;
            toko.hasBoard = true;
            toko.boardName = path.stem().wstring(); // (shown on the screen: the name without its extension)
            return true;
        } catch (const std::exception&) {
            say(L"盤面ファイルを読めませんでした: " + path.stem().wstring());
            return false;
        }
    }
    bool openDialog(const wchar_t* filter, const std::filesystem::path& folder, std::filesystem::path& chosen) {
#ifndef _WIN32
        const bool ok = chooseFile(window, filter, folder, chosen);
        forgetKeys();
        return ok;
#else
        wchar_t file[MAX_PATH] = L"";
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof ofn;
        ofn.hwndOwner = window;
        ofn.lpstrFilter = filter;
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        const std::wstring initial = folder.wstring();
        if (!initial.empty()) ofn.lpstrInitialDir = initial.c_str();
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        const bool ok = GetOpenFileNameW(&ofn) != 0;
        forgetKeys();
        if (ok) chosen = file;
        return ok;
#endif
    }
    std::filesystem::path replayPath(const wchar_t* kind) const {
        wchar_t name[64];
#ifdef _WIN32
        SYSTEMTIME t;
        GetLocalTime(&t);
        swprintf_s(name, L"%s-%04d%02d%02d-%02d%02d%02d.jfreplay", kind, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
#else
        const std::time_t now = std::time(nullptr);
        std::tm t{};
        localtime_r(&now, &t);
        std::swprintf(name, 64, L"%ls-%04d%02d%02d-%02d%02d%02d.jfreplay", kind, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
#endif
        return userDirectory / L"replays" / name;
    }
    void saveTokoReplay() {
        Replay r{settings, 1, TIMING_VERSION, {toko.game.log}};
        r.settings.rules = toko.game.round.rules;
        const std::filesystem::path path = replayPath(L"toko");
        say(saveReplay(path, r) ? L"リプレイを保存しました: replays\\" + path.stem().wstring() : L"リプレイを保存できませんでした");
    }
    bool openReplay(const std::filesystem::path& path) {
        Replay r;
        if (!loadReplay(path, r)) {
            say(L"リプレイを読めませんでした: " + path.stem().wstring());
            return false;
        }
        viewer = Viewer{};
        viewer.replay = std::move(r);
        viewer.name = path.stem().wstring(); // (shown on the screen: the name without its extension)
        viewer.startRound();
        screen = Screen::Replay;
        // (a later version may have other frame counts: the buttons are played with those of this program)
        if (viewer.replay.timingVersion > TIMING_VERSION) say(L"このソフトより新しい版で保存したリプレイです。記録した時と違う進み方をするかもしれません");
        return true;
    }
    // A file dropped on the window: a replay is played, a board opens a game alone. Not during a match over a
    // network: the other end would be left without a word, and the line would stay open behind the new screen.
    void openDropped(const std::filesystem::path& path) {
        if (screen == Screen::Net) return say(L"ネット対戦の間は、ファイルを開けません（先に対戦を抜けてください）");
        if (path.extension() == L".jfreplay") openReplay(path);
        else if (loadBoardFile(path)) toko.start(settings), screen = Screen::Toko;
    }
    void copySeed() {
        const std::wstring s = std::to_wstring(toko.seed);
#ifdef _WIN32
        if (!OpenClipboard(window)) return;
        EmptyClipboard();
        if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t))) {
            memcpy(GlobalLock(h), s.c_str(), (s.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
        }
        CloseClipboard();
#else
        if (!setClipboardText(s)) return;
#endif
        say(L"シード " + s + L" をコピーしました");
    }
    // The text on the clipboard (nothing when it holds none).
    std::wstring clipboardText() {
#ifndef _WIN32
        return play::clipboardText();
#else
        std::wstring s;
        if (OpenClipboard(window)) {
            if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
                if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h))) s = p;
                GlobalUnlock(h);
            }
            CloseClipboard();
        }
        return s;
#endif
    }
    void pasteSeed() {
        try {
            toko.seed = std::stoull(clipboardText());
            toko.start(settings);
            say(L"シード " + std::to_wstring(toko.seed) + L" で開始しました");
        } catch (...) {
            say(L"クリップボードに数字のシードがありません");
        }
    }
    void launchGenerator() {
#ifndef _WIN32
        say(L"盤面ジェネレーターの窓は Windows 版だけです（コマンドの random_19_chain は同じフォルダーにあります）");
        return;
#endif
        const std::filesystem::path exe = exeDirectory / L"JuraicFieldGenerator.exe";
        if (!std::filesystem::exists(exe)) {
            say(L"盤面ジェネレーターの exe が同じフォルダーにありません");
            return;
        }
#ifdef _WIN32
        ShellExecuteW(window, L"open", exe.c_str(), nullptr, exeDirectory.c_str(), SW_SHOWNORMAL);
#endif
    }
    // A setting of the self tests (--screenshot), given in the environment: its text, nothing when it is not set.
    //   JURAIC_SELFTEST_ALL_CLEAR  the picture is taken with "all clear" on every field
    //   JURAIC_SELFTEST_WEAK       ... with the words for an AI short of time
    //   JURAIC_SELFTEST_PERSON     n ending in 6 or 7: the 1P seat is left to the script, and the picture is that of the match
    //   JURAIC_SELFTEST_NET        n ending in 8: the line of the match and where it stops (see there)
    //   JURAIC_SELFTEST_WINS       "rounds sets won0 won1 sets0 sets1": the picture of a match is taken with these numbers of rounds
    //                              and sets to win and won (for looking at how many of them are written; nothing is played by them)
    static std::wstring selfTestSetting(const wchar_t* name) {
#ifdef _WIN32
        wchar_t text[128];
        const DWORD length = GetEnvironmentVariableW(name, text, 128);
        return length > 0 && length < 128 ? std::wstring(text, length) : std::wstring();
#else
        const char* text = std::getenv(toUtf8(name).c_str());
        return text && std::strlen(text) < 128 ? fromUtf8(text) : std::wstring();
#endif
    }
    static uint64_t newSeed() { return uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()) % 1000000000ull; }

    void sounds(const PlayerEvents& e, const Player& p) {
        if (e.control.moved) audio.play(SND_MOVE);
        if (e.control.rotated) audio.play(SND_ROTATE);
        if (e.locked) audio.play(SND_LAND);
        if (e.waveStarted) audio.chain(p.chain.chains);
        if (e.garbageFell) audio.play(SND_GARBAGE);
        if (e.allClear) audio.play(SND_ALL_CLEAR);
        if (e.died) audio.play(SND_OVER);
    }

    // ---- one game frame (1/60 s) ----
    void updateMenu() {
        const int count = 10;
        if (heldStep(NAV_UP, 6, 6)) menuIndex = (menuIndex + count - 1) % count, audio.play(SND_MOVE);
        if (heldStep(NAV_DOWN, 6, 6)) menuIndex = (menuIndex + 1) % count, audio.play(SND_MOVE);
        const bool back = nav(NAV_BACK);
        if (!(nav(NAV_OK) | pressed('Z') | pressed('X')) || back) return;
        audio.play(SND_SELECT);
        switch (menuIndex) {
        case 0:
            toko.seed = newSeed();
            toko.start(settings);
            screen = Screen::Toko;
            break;
        case 1:
        case 2: // against the AI: the person is 1P
        case 3: // two AIs to watch
            // (the AI of a seat: its level - the AI settings aiLevel and aiLevel1, ai/level.h - and, at the top level,
            //  Juraic as it is or the one that does not poke, aiStyle and aiStyle1)
            versus.seat(0, menuIndex == 3 ? aiOfLevel(settings.aiLevel1, settings.aiStyle1) : "", settings);
            versus.seat(1, menuIndex == 1 ? "" : aiOfLevel(settings.aiLevel, settings.aiStyle), settings);
            chooseDevices(input, pads, menuIndex == 1 ? 2 : 1, versus.device);
            versus.startMatch(settings, newSeed());
            screen = Screen::Versus;
            break;
        case 4: screen = Screen::NetLobby, lobbyEditing = false; break;
        case 5: {
            std::filesystem::path path;
            if (openDialog(L"リプレイ\0*.jfreplay\0すべてのファイル\0*.*\0", userDirectory / L"replays", path)) openReplay(path);
            break;
        }
        case 6:
            scanMedia();
            mediaChanged();
            screen = Screen::Settings;
            break;
        case 7: screen = Screen::Bindings, binding = false; break;
        case 8: launchGenerator(); break;
#ifdef _WIN32
        default: PostMessageW(window, WM_CLOSE, 0, 0);
#else
        default: quit = true;
#endif
        }
    }
    // The settings screen: the parts on the left, the settings of the part chosen on the right. A
    // setting that has no effect as the others stand (SettingItem::enabled) is passed over.
    void updateSettings() {
        SettingsTable table(settings);
        const int count = int(table.items.size());
        const auto usable = [&](int i) { return table.items[size_t(i)].section == settingSection && table.enabled(table.items[size_t(i)], settings); };
        const auto step = [&](int from, int by) { // the next usable setting of the part, -1 = none
            for (int n = 1; n <= count; ++n) {
                const int i = ((from + by * n) % count + count) % count;
                if (usable(i)) return i;
            }
            return -1;
        };
        if (!settingInside) {
            if (heldStep(NAV_UP, 6, 6)) settingSection = (settingSection + SETTING_SECTIONS - 1) % SETTING_SECTIONS, audio.play(SND_MOVE);
            if (heldStep(NAV_DOWN, 6, 6)) settingSection = (settingSection + 1) % SETTING_SECTIONS, audio.play(SND_MOVE);
            const bool back = nav(NAV_BACK);
            if ((nav(NAV_OK) | nav(NAV_RIGHT)) && !back) {
                const int first = step(-1, 1);
                if (first >= 0) settingInside = true, settingIndex = first, audio.play(SND_SELECT);
            } else if (back) {
                saveAll();
                audio.play(SND_SELECT);
                screen = Screen::Menu;
            }
            return;
        }
        if (settingIndex < 0 || settingIndex >= count || !usable(settingIndex)) settingIndex = step(-1, 1);
        if (settingIndex < 0) {
            settingInside = false;
            return;
        }
        // (the cursor moves on: a direction to the left or right that is still held does not go on with the next setting)
        if (heldStep(NAV_UP, 6, 6)) settingIndex = step(settingIndex, -1), audio.play(SND_MOVE), dirHeld[NAV_LEFT] = dirHeld[NAV_RIGHT] = 0;
        if (heldStep(NAV_DOWN, 6, 6)) settingIndex = step(settingIndex, 1), audio.play(SND_MOVE), dirHeld[NAV_LEFT] = dirHeld[NAV_RIGHT] = 0;
        // (a direction held down goes on changing the value, faster after a second: heldStep)
        bool heard = false;
        const bool right = heldStep(NAV_RIGHT, 4, 2, &heard), left = heldStep(NAV_LEFT, 4, 2, &heard);
        const int change = (right ? 1 : 0) - (left ? 1 : 0);
        if (change) {
            SettingItem& item = table.items[size_t(settingIndex)];
            int& v = item.value(settings);
            // (past either end the value comes in again at the other: Lv 36 → Lv 1)
            v += change * item.step;
            if (v > item.high) v = item.low;
            else if (v < item.low) v = item.high;
            table.apply(settings);
            audio.setVolume(settings.volume);
            mediaChanged();
            if (heard) audio.play(SND_ROTATE);
        }
        if (nav(NAV_BACK) | nav(NAV_OK)) {
            saveAll();
            audio.play(SND_SELECT);
            settingInside = false;
        }
    }
    // Rows of the binding screen: device of player 1, of player 2, the layout, then its actions.
    // (The menu actions are bound on controllers only.)
    int bindActions() const { return bindLayout < LAYOUT_XBOX ? GAME_ACTIONS : ACT_COUNT; }
    void updateBindings() {
        const int rows = 3 + bindActions();
        if (bindIndex >= rows) bindIndex = rows - 1;
        if (binding) {
            const int action = bindIndex - 3;
            int found = -1;
            if (bindLayout < LAYOUT_XBOX) {
                if (pressed(VK_ESCAPE)) {
                    binding = false;
                    return;
                }
                for (int vk = 8; vk < 255 && found < 0; ++vk)
                    if (vk != VK_ESCAPE && vk != VK_LSHIFT && vk != VK_RSHIFT && vk != VK_LCONTROL && vk != VK_RCONTROL && vk != VK_LMENU && vk != VK_RMENU &&
                        pressed(vk))
                        found = vk;
            } else {
                if (pressed(VK_ESCAPE)) {
                    binding = false;
                    return;
                }
                for (int i = 0; i < pads.count() && found < 0; ++i) {
                    if (pads.isXbox(i) != (bindLayout == LAYOUT_XBOX)) continue;
                    const uint64_t fresh = pads.raw(i) & ~(size_t(i) < padWas.size() ? padWas[size_t(i)] : ~0ull);
                    if (fresh) found = std::countr_zero(fresh);
                }
            }
            if (found >= 0) {
                input.set(bindLayout, action, found);
                binding = false;
                audio.play(SND_SELECT);
                saveAll();
                forgetKeys();
            }
            return;
        }
        if (heldStep(NAV_UP, 6, 6)) bindIndex = (bindIndex + rows - 1) % rows, audio.play(SND_MOVE);
        if (heldStep(NAV_DOWN, 6, 6)) bindIndex = (bindIndex + 1) % rows, audio.play(SND_MOVE);
        const int change = (nav(NAV_RIGHT) ? 1 : 0) - (nav(NAV_LEFT) ? 1 : 0);
        if (change && bindIndex < 2) {
            const int limit = 2 + pads.count(); // -1 automatic, then the devices
            int& d = input.device[bindIndex];
            d = (d + 1 + change + limit + 1) % (limit + 1) - 1;
            audio.play(SND_ROTATE);
        }
        if (change && bindIndex == 2) bindLayout = (bindLayout + change + LAYOUT_COUNT) % LAYOUT_COUNT, audio.play(SND_ROTATE);
        const bool back = nav(NAV_BACK);
        if (nav(NAV_OK) && !back && bindIndex >= 3) {
            binding = true;
            forgetKeys();
            audio.play(SND_SELECT);
            return;
        }
        if (back) {
            saveAll();
            audio.play(SND_SELECT);
            screen = Screen::Menu;
        }
    }
    void updateToko() {
        const bool padStart = nav(NAV_START);
        if (pressed(VK_ESCAPE) || (toko.paused && nav(NAV_BACK))) {
            screen = Screen::Menu;
            return;
        }
        const bool ctrl = down(VK_CONTROL);
        int device[2];
        chooseDevices(input, pads, 1, device);
        // One player: the keyboard always works, together with the chosen device.
        unsigned held = actionsOf(device[0], input, pads, focused) | actionsOf(0, input, pads, focused);
        const unsigned fresh = held & ~tokoHeld;
        tokoHeld = held;
        if (fresh >> ACT_UNDO & 1) toko.undo(), audio.play(SND_SELECT);
        if (fresh >> ACT_RETRY & 1) toko.start(settings), say(L"同じツモでやり直します");
        if (fresh >> ACT_NEW & 1) {
            toko.seed = newSeed();
            toko.start(settings);
            say(L"新しいツモで開始しました");
        }
        if (pressed('O')) {
            std::filesystem::path path;
            if (openDialog(L"盤面\0*.jfchain;*.jfboard;*.txt\0すべてのファイル\0*.*\0", {}, path) && loadBoardFile(path)) toko.start(settings);
        }
        if (pressed('E') && toko.hasBoard) toko.hasBoard = false, toko.start(settings), say(L"空の盤面に戻しました");
        if (pressed('C') && ctrl) copySeed();
        if (pressed('V') && ctrl) pasteSeed();
        if (pressed(VK_F5)) saveTokoReplay();
        if (pressed(VK_PAUSE) | pressed(input.bind[LAYOUT_KEYS1][ACT_START]) | padStart) {
            if (toko.game.round.state != RoundState::Playing) toko.start(settings);
            else toko.paused = !toko.paused;
        }
        if (!settings.quickDrop && down(VK_UP)) held |= 1u << ACT_CW; // (with the quick drop on, up is the drop)
        const PlayerEvents e = toko.step(personButtons(0, held, upHeld(device[0], pads, focused) || upHeld(0, pads, focused), settings.quickDrop != 0));
        sounds(e, toko.game.round.player[0]);
        if (settings.prediction) toko.predict();
    }
    void updateVersus() {
        Versus& v = versus;
        // (Esc is read once a frame: nav(NAV_BACK) reads it too, and a key that was read is not "pressed" a second
        // time - so Esc did not pause a match. The start keys of the keyboard pause as the controller's start does.)
        const bool esc = pressed(VK_ESCAPE);
        const bool start = nav(NAV_START) | pressed(VK_PAUSE) | pressed(input.bind[LAYOUT_KEYS1][ACT_START]) | pressed(input.bind[LAYOUT_KEYS2][ACT_START]);
        const bool back = esc | nav(NAV_BACK);
        if (v.paused) {
            if (start) v.paused = false;
            if (back) screen = Screen::Menu;
            return;
        }
        if (v.state == Versus::Play && (start || esc)) {
            v.paused = true;
            return;
        }
        if (v.state == Versus::Ready) {
            if (--v.timer <= 0) v.state = Versus::Play;
            return;
        }
        if (v.state == Versus::RoundEnd) {
            if (--v.timer <= 0) {
                // (the thinking of the round is judged here and not as the round ends: an AI may be in the middle of a
                // choice then, on its own thread, and the numbers were read while it wrote them. keepNotes waits for it.)
                v.keepNotes();
                judgeThinking();
                const int winner = v.setWon(settings.rules);
                if (winner < 0) {
                    v.startRound(settings);
                } else if (v.sets[winner] >= settings.rules.setsNeeded) {
                    v.state = Versus::MatchEnd;
                    v.keepNotes();
                    const std::filesystem::path path = replayPath(L"versus");
                    say(saveReplay(path, v.replay) ? L"リプレイを保存しました: replays\\" + path.stem().wstring() : L"リプレイを保存できませんでした");
                } else {
                    v.state = Versus::SetEnd, v.timer = 150;
                }
            }
            return;
        }
        if (v.state == Versus::SetEnd) { // (the set that was won is shown as long as a round, then the next set begins by itself)
            if (--v.timer <= 0) {
                v.wins[0] = v.wins[1] = 0;
                v.startRound(settings);
            }
            return;
        }
        if (v.state == Versus::MatchEnd) {
            // The AI of a seat is changed here and nowhere else in a match, with the mouse: no key of the game does it,
            // so none does it by mistake, and the keys that begin the next match at once do nothing else.
            // What is chosen goes into the settings and plays from the next match.
            // The button held down on a level goes on changing it, as a direction held on a value of the settings does
            // (heldStep; there are thirty-six levels) - while the pointer stays on what it went down on. The AI itself,
            // of which there are two, changes once a click.
            int pick = clicked ? pickAt(clickX, clickY) : -1;
            bool heard = pick >= 0;
            if (pick >= 0) pickNow = pick, pickHeld = 1;
            else if (pickHeld > 0 && mouseDown && pickNow % 4 >= 2 && pickAt(mouseX, mouseY) == pickNow) {
                const int over = ++pickHeld - HELD_WAIT;
                if (over > 0 && over % (over <= HELD_SLOW ? 4 : 2) == 0) pick = pickNow, heard = over % 4 == 0;
            } else pickHeld = 0;
            if (pick >= 0) {
                const int seat = pick / 4;
                int& style = seat ? settings.aiStyle : settings.aiStyle1;
                int& level = seat ? settings.aiLevel : settings.aiLevel1;
                if (pick % 4 < 2) style = !style;
                else level = pick % 4 == 2 ? (level <= 1 ? ai::LEVELS : level - 1) : (level >= ai::LEVELS ? 1 : level + 1);
                v.picked[seat] = true;
                if (heard) audio.play(SND_ROTATE);
            }
            const bool again = nav(NAV_OK) | start;
            if ((again || back) && (v.picked[0] || v.picked[1])) {
                saveAll();
                for (int i = 0; i < 2; ++i) {
                    if (v.picked[i] && again) v.seat(i, aiOfLevel(i ? settings.aiLevel : settings.aiLevel1, i ? settings.aiStyle : settings.aiStyle1), settings);
                    v.picked[i] = false;
                }
            }
            if (again) v.startMatch(settings, newSeed());
            if (back) screen = Screen::Menu;
            return;
        }
        uint8_t buttons[2];
        for (int i = 0; i < 2; ++i)
            buttons[i] = v.agent[i] ? v.agentButtons(i)
                                    : personButtons(i, actionsOf(v.device[i], input, pads, focused), upHeld(v.device[i], pads, focused), settings.quickDrop != 0);
        PlayerEvents e[2];
        v.game.step(buttons, e);
        for (int i = 0; i < 2; ++i) sounds(e[i], v.game.round.player[i]);
        if (v.game.round.state != RoundState::Playing) {
            v.roundOver();
            v.timer = 150;
        }
    }
    // After every round with an AI seat: was its AI short of time in it on this machine? Then the AI played weaker
    // than it should, and the screen says so until a round without it has ended (drawVersus). Short of time: a fifth
    // of its choices or more with a search made narrower or stopped by the clock, or half of them or more with plans
    // of two pairs, or a frame or more a choice in which a pair waited. (With the clock at a fifth of what the
    // searches need, where two thirds of the searches were narrowed, X3 won 36.5% against itself; at two fifths,
    // with 14% and a third of the plans short, 48.5%.)
    // (What the clock did in each choice is in the replay.)
    void judgeThinking() {
        Versus& v = versus;
        v.weak = false;
        for (int i = 0; i < 2; ++i) {
            const ai::X3Agent* x3 = dynamic_cast<const ai::X3Agent*>(v.agent[i].get());
            if (!x3 || !x3->thinkCount || x3->budgetMs() <= 0) continue; // (not in a self-test, which plays without the clock)
            // this round: what was added since the round before
            // (second choices are extra: one that the clock made narrower leaves the first standing, and does not make the AI weaker)
            const int64_t now[5] = {x3->thinkCount - x3->againMade - x3->againPoor, x3->narrowSearches - x3->againNarrow, x3->cutSearches - x3->againCut, x3->shortPlans, x3->lateFrames};
            int64_t round[5];
            for (int k = 0; k < 5; ++k) round[k] = now[k] - v.logged[i][k], v.logged[i][k] = now[k];
            if (round[0] >= 5 && ((round[1] + round[2]) * 5 >= round[0] || round[3] * 2 >= round[0] || round[4] >= round[0])) v.weak = true;
        }
    }
    // The match-end banner has a row for every AI seat, in which its AI and its level are changed with the mouse:
    // ◀ and ▶ on either side of each (and the value itself, which goes on as ▶ does). The boxes of the row of
    // `seat`, k = 0 to 5: ◀, the AI, ▶, ◀, the level, ▶.
    RectF pickBox(int seat, int k) const {
        const float x[8] = {396, 426, 600, 630, 640, 670, 898, 928}, y = 586 + (seat == 1 && versus.agent[0] ? 32.f : 0.f);
        const int at = k < 3 ? k : k + 1; // (a gap between the two)
        return rectF(x[at], y, x[at + 1], y + 28);
    }
    // What a click at (x, y) of the canvas changes: seat * 4 + (0, 1: the other AI; 2, 3: the level below and above), -1 = nothing.
    int pickAt(float x, float y) const {
        for (int seat = 0; seat < 2; ++seat)
            for (int k = 0; versus.agent[seat] && k < 6; ++k) {
                const RectF r = pickBox(seat, k);
                if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return seat * 4 + (k < 3 ? (k == 0 ? 0 : 1) : (k == 3 ? 2 : 3));
            }
        return -1;
    }
    // ---- a match over a network ----
    // The screen before it: the own name, a room opened (this machine waits for a call on its port) or entered (the
    // address of the machine that waits). A text is typed after Enter on its row, and ended with Enter or Esc.
    void updateLobby() {
        if (pressed(VK_F1)) showAddress = !showAddress;
        if (lobbyEditing) {
            std::wstring& text = lobbyIndex == 0 ? netName : netAddress;
            const size_t most = lobbyIndex == 0 ? 12 : 80;
            for (const wchar_t c : typed) {
                if (c == 8 && !text.empty()) text.pop_back();
                // (Ctrl+V: an address is usually pasted)
                if (c == 22)
                    for (const wchar_t p : clipboardText())
                        if (p > 32 && p < 127 && lobbyIndex == 2 && text.size() < most) text += p;
                if (c >= 32 && c != 127 && text.size() < most && (lobbyIndex == 0 || (c > 32 && c < 127))) text += c;
            }
            if (pressed(VK_RETURN) | pressed(VK_ESCAPE)) {
                lobbyEditing = false;
                if (netName.empty()) netName = L"Player";
                forgetKeys();
                saveAll();
                audio.play(SND_SELECT);
            }
            return;
        }
        const int count = 5;
        if (heldStep(NAV_UP, 6, 6)) lobbyIndex = (lobbyIndex + count - 1) % count, audio.play(SND_MOVE);
        if (heldStep(NAV_DOWN, 6, 6)) lobbyIndex = (lobbyIndex + 1) % count, audio.play(SND_MOVE);
        const bool back = nav(NAV_BACK), ok = nav(NAV_OK);
        if (back || (ok && lobbyIndex == 4)) {
            audio.play(SND_SELECT);
            screen = Screen::Menu;
            return;
        }
        if (!ok) return;
        audio.play(SND_SELECT);
        if (lobbyIndex == 0 || lobbyIndex == 2) {
            lobbyEditing = true;
            return;
        }
        auto line = std::make_unique<UdpLine>();
        if (lobbyIndex == 1) {
            if (!line->listen(netPort)) {
                say(L"ポート " + std::to_wstring(netPort) + L" で待ち受けられませんでした（ほかのソフトが使っているかもしれません）");
                return;
            }
            ownAddressList = ownAddresses();
#ifdef _WIN32
            const uint64_t ticks = uint64_t(GetTickCount64());
#else
            const uint64_t ticks = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
            net.host(std::move(line), settings, toUtf8(netName), newSeed() ^ ticks << 20, settings.netDelay);
        } else {
            std::string host;
            int port = netPort;
            splitAddress(toUtf8(netAddress), host, port);
            if (host.empty() || !line->call(host, port)) {
                say(host.empty() ? L"相手のアドレスを入れてください" : L"そのアドレスは見つかりませんでした");
                return;
            }
            net.join(std::move(line), settings, toUtf8(netName));
        }
        chooseDevices(input, pads, 1, netDevice);
        netQuit = 0;
        screen = Screen::Net;
    }
    // The match. There is no pause; the keyboard's Esc pressed twice leaves it (a button of a controller never does:
    // the buttons that go back in the menus are buttons of the game).
    void leaveNet() {
        net.leave();
        netQuit = 0;
        screen = Screen::NetLobby;
    }
    void updateNet() {
        const bool esc = pressed(VK_ESCAPE);
        const bool ok = nav(NAV_OK), start = nav(NAV_START), back = nav(NAV_BACK) | esc;
        const bool playing = net.stage == NetPlay::Ready || net.stage == NetPlay::Play || net.stage == NetPlay::RoundEnd || net.stage == NetPlay::SetEnd ||
                             (net.stage == NetPlay::Connecting && net.peer.round > 0);
        if (netQuit) --netQuit;
        if (playing) {
            if (esc && netQuit) return leaveNet();
            if (esc) netQuit = 180;
        } else {
            netQuit = 0;
            if (net.stage == NetPlay::Over ? (back || ok) : back) return leaveNet();
            if (net.stage == NetPlay::MatchEnd && (ok || start)) net.askAgain(), audio.play(SND_SELECT);
        }
        const int device = netDevice[0];
        // (the quick drop of the match, which is the host's: net.settings, not this machine's own)
        const uint8_t buttons = personButtons(net.seat(), actionsOf(device, input, pads, focused), upHeld(device, pads, focused), net.settings.quickDrop != 0);
        net.tick(buttons, [&](int seat, const PlayerEvents& e) { sounds(e, net.link.round.player[seat]); });
        if (net.matchEnded) {
            net.matchEnded = false;
            const std::filesystem::path path = replayPath(L"net");
            say(saveReplay(path, net.replay) ? L"リプレイを保存しました: replays\\" + path.stem().wstring() : L"リプレイを保存できませんでした");
        }
    }
    void updateViewer() {
        Viewer& w = viewer;
        if (nav(NAV_BACK)) {
            screen = Screen::Menu;
            return;
        }
        if (pressed(VK_SPACE) | nav(NAV_OK)) w.paused = !w.paused;
        if (pressed('R')) w.restart();
        if (w.paused || w.finished) return;
        const int speed = down(VK_RIGHT) || (navHeld >> NAV_RIGHT & 1) ? 4 : 1;
        for (int i = 0; i < speed && !w.finished; ++i) {
            const RoundLog& log = w.replay.rounds[w.round];
            if (w.frame >= log.frames.size() || w.game.round.state != RoundState::Playing) {
                if (w.round + 1 < w.replay.rounds.size()) w.nextRound();
                else w.finished = true;
                continue;
            }
            const uint8_t buttons[2] = {uint8_t(log.frames[w.frame] & 255), uint8_t(log.frames[w.frame] >> 8)};
            ++w.frame;
            PlayerEvents e[2];
            w.game.step(buttons, e);
            w.countRound();
            if (speed == 1)
                for (int p = 0; p < w.replay.players; ++p) sounds(e[p], w.game.round.player[p]);
        }
    }
    void update() {
        ++uiFrame;
        if (!(screen == Screen::Versus && versus.state == Versus::MatchEnd)) pickHeld = 0; // (the mouse holds a level only at the end of a match)
        if (messageTimer) --messageTimer;
        updateMedia();
        readPads();
        switch (screen) {
        case Screen::Menu: updateMenu(); break;
        case Screen::Settings: updateSettings(); break;
        case Screen::Bindings: updateBindings(); break;
        case Screen::Toko: updateToko(); break;
        case Screen::Versus: updateVersus(); break;
        case Screen::Replay: updateViewer(); break;
        case Screen::NetLobby: updateLobby(); break;
        case Screen::Net: updateNet(); break;
        }
        typed.clear();   // (what was typed is for the frame it came in)
        clicked = false; // (a click is for the frame it came in)
        padWas.assign(size_t(pads.count()), 0);
        for (int i = 0; i < pads.count(); ++i) padWas[size_t(i)] = pads.raw(i);
    }

    // ---- drawing ----
    void drawMenu() {
        gfx.text(L"Juraic Field", 0, 70, CANVAS_W, 80, 3, rgb(255, 255, 255), 0);
        const wchar_t* items[] = {L"とこ（1 人プレイ）", L"対戦（2 人）",   L"対戦（人 対 AI）",   L"観戦（AI 対 AI）", L"ネット対戦（人 対 人）",
                                  L"リプレイを見る",         L"設定",           L"操作の割り当て",     L"盤面ジェネレーター", L"終了"};
        for (int i = 0; i < 10; ++i) {
            const float y = 164 + i * 46.f;
            if (i == menuIndex) gfx.round(rectF(410, y - 1, 870, y + 43), 12, rgb(84, 104, 190));
            gfx.text(items[i], 410, y, 460, 44, 2, i == menuIndex ? rgb(255, 255, 255) : rgb(170, 178, 204), 0);
        }
        gfx.text(L"↑↓ で選ぶ　Enter で決定　（コントローラー: 方向と決定ボタン。「操作の割り当て」で変えられます）", 0, 640, CANVAS_W, 30, 0, rgb(150, 158, 186), 0);
        gfx.text(pads.count() ? L"コントローラー " + std::to_wstring(pads.count()) + L" 台を認識しています" : L"コントローラーは接続されていません", 0, 668,
                 CANVAS_W, 26, 0, pads.count() ? rgb(160, 220, 170) : rgb(120, 126, 150), 0);
    }
    void drawSettings() {
        SettingsTable table(settings);
        gfx.text(L"設定", 0, 40, CANVAS_W, 60, 2, rgb(255, 255, 255), 0);
        // The parts.
        for (int k = 0; k < SETTING_SECTIONS; ++k) {
            const float y = 132 + float(k) * 58;
            const bool on = k == settingSection;
            if (on) gfx.round(rectF(96, y - 6, 316, y + 40), 8, settingInside ? rgb(52, 62, 104) : rgb(84, 104, 190));
            gfx.text(SETTING_SECTION_NAMES[k], 96, y, 220, 34, 1, on ? rgb(255, 255, 255) : rgb(180, 188, 212), 0);
        }
        // The settings of the part chosen (fourteen rows fit: the list moves with the row chosen).
        std::vector<int> shown;
        for (size_t i = 0; i < table.items.size(); ++i)
            if (table.items[i].section == settingSection) shown.push_back(int(i));
        const int rows = 14, count = int(shown.size());
        int at = 0;
        for (int n = 0; n < count; ++n)
            if (shown[size_t(n)] == settingIndex) at = n;
        const int top = count <= rows || !settingInside ? 0 : std::clamp(at - rows / 2, 0, count - rows);
        gfx.round(rectF(344, 112, 1184, 112 + float(rows) * 36 + 16), 10, rgb(34, 39, 64));
        for (int n = top; n < count && n < top + rows; ++n) {
            SettingItem& item = table.items[size_t(shown[size_t(n)])];
            const float y = 122 + float(n - top) * 36;
            const bool on = settingInside && shown[size_t(n)] == settingIndex, usable = table.enabled(item, settings);
            if (on) gfx.round(rectF(356, y - 3, 1172, y + 31), 8, rgb(84, 104, 190));
            const ColorF name = !usable ? rgb(98, 104, 128) : on ? rgb(255, 255, 255) : rgb(180, 188, 212);
            gfx.text(item.name, 376, y, 400, 30, 1, name);
            // (the levels of the AI are written in the colour of their group, play/settings.h)
            const unsigned tint = usable && item.color ? item.color(item.value(settings)) : 0xFFFFFF;
            gfx.text((on ? L"◀  " : L"") + item.show(item.value(settings)) + (on ? L"  ▶" : L""), 700, y, 452, 30, 1,
                     usable ? rgb(int(tint >> 16 & 255), int(tint >> 8 & 255), int(tint & 255)) : rgb(98, 104, 128), 1);
        }
        gfx.text(settingInside ? L"↑↓ で選ぶ　←→ で変更（押したままで続けて変わります）　Esc / Enter で左の一覧へ　（灰色の項目は、今の設定では使われません）"
                               : L"↑↓ で選ぶ　Enter / → で開く　Esc で保存して戻る　（ルールの変更は次のゲームから）",
                 0, 660, CANVAS_W, 30, 0, rgb(150, 158, 186), 0);
    }
    void drawBindings() {
        gfx.text(L"操作の割り当て", 0, 40, CANVAS_W, 60, 2, rgb(255, 255, 255), 0);
        const auto row = [&](int index, const std::wstring& name, const std::wstring& value, bool arrows) {
            const float y = 112 + float(index) * 34 + (index >= 2 ? 14 : 0) + (index >= 3 ? 8 : 0) + (index >= 3 + GAME_ACTIONS ? 10 : 0);
            const bool on = index == bindIndex;
            if (on) gfx.round(rectF(300, y - 2, 980, y + 30), 8, binding ? rgb(190, 110, 60) : rgb(84, 104, 190));
            gfx.text(name, 320, y, 360, 30, 1, on ? rgb(255, 255, 255) : rgb(180, 188, 212));
            gfx.text((on && arrows ? L"◀  " : L"") + value + (on && arrows ? L"  ▶" : L""), 600, y, 360, 30, 1, rgb(255, 255, 255), 1);
        };
        for (int i = 0; i < 2; ++i)
            row(i, std::to_wstring(i + 1) + L"P の操作", input.device[i] < 0 ? L"自動（コントローラー優先）" : deviceName(input.device[i]), true);
        row(2, L"割り当てを変えるもの", layoutNames[bindLayout], true);
        for (int a = 0; a < bindActions(); ++a)
            row(3 + a, actionNames[a], binding && bindIndex == 3 + a ? L"キーかボタンを押してください" : codeName(bindLayout, input.bind[bindLayout][a]), false);
        gfx.text(L"↑↓ で選ぶ　←→ で変更　Enter で割り当てを変更（Esc で取り消し）　Esc で保存して戻る", 0, 640, CANVAS_W, 30, 0, rgb(150, 158, 186), 0);
        gfx.text(L"とこは、キーボード 1 と 1P の操作の両方で動かせます。メニューの決定と戻るは、対戦の操作とは別に割り当てます。", 0, 668, CANVAS_W, 26,
                 0, rgb(120, 126, 150), 0);
    }
    // Garbage waiting above a field: the count, and the usual symbols for it.
    void drawTray(const Incoming& in, float x, float y) {
        int n = in.total();
        if (n <= 0) return;
        struct Symbol {
            int value;
            float size;
            ColorF color;
        };
        const Symbol symbols[] = {{720, 22, rgb(250, 210, 60)}, {360, 21, rgb(240, 240, 170)}, {180, 20, rgb(250, 170, 60)},
                                  {30, 19, rgb(214, 80, 70)},   {6, 16, rgb(190, 196, 208)},   {1, 10, rgb(190, 196, 208)}};
        float cx = x + 20;
        int shown = 0;
        for (const Symbol& s : symbols)
            for (; n >= s.value && shown < 6; n -= s.value, ++shown, cx += 44) {
                gfx.circle(cx, y + 20, s.size, s.size * 0.92f, rgb(20, 24, 40));
                gfx.circle(cx, y + 20, s.size - 2, (s.size - 2) * 0.92f, in.confirmed ? s.color : colorF(s.color.r, s.color.g, s.color.b, 0.6f));
                if (s.value >= 180) gfx.text(s.value == 720 ? L"王" : s.value == 360 ? L"月" : L"星", cx - 14, y + 7, 28, 24, 0, rgb(60, 40, 20), 0);
            }
#ifdef _WIN32
        gfx.text(std::to_wstring(in.total()), x + W * CELL - 80, y + 4, 80, 30, 1, rgb(255, 255, 255), 1);
#else
        // (the digits of the font of this version are a third wider: the count stands 14 further right, over the edge
        //  of the field, clear of the sixth symbol)
        gfx.text(std::to_wstring(in.total()), x + W * CELL - 66, y + 4, 80, 30, 1, rgb(255, 255, 255), 1);
#endif
    }
    void drawScore(const Player& p, float x, float y) {
        wchar_t score[16];
        swprintf_s(score, L"%08d", p.scoring.score);
        gfx.text(score, x, y, W * CELL, 40, 2, rgb(255, 255, 255), 0);
    }
    // "All clear" on the upper part of the field at (x, y), while the bonus waits. Always with a dark edge: pieces
    // may be behind it.
    void drawAllClear(const Player& p, float x, float y) {
        if (!p.scoring.allClear) return;
        const bool edge = gfx.shadow;
        gfx.shadow = true;
        gfx.text(L"全消し！", x, y + CELL * 1.5f, W * CELL, 40, 2, rgb(255, 236, 120), 0);
        gfx.shadow = edge;
    }
    // Two fields side by side with everything between them. needed: the stars of a seat (the rounds a match is won with).
    // sets, setsNeeded: the sets won and the sets a match is won with (marks above the stars; none while a match is one set).
    // Up to TALLY_MARKS of them are drawn as marks, full for those that are won; more - a match may be set to 99 rounds
    // and to 99 sets - are written as numbers, "won / needed" behind one mark: the place of a seat between the fields
    // holds that many marks and no more.
    // frame: the time shown and the garbage rate (the frames of the round; of the own seat in a match of two machines).
    static constexpr int TALLY_MARKS = 5;
    void drawTwo(const Round& r, const View view[2], int64_t frame, const int wins[2], const std::wstring names[2], int needed, const int sets[2], int setsNeeded) {
        const float y0 = 96, x0[2] = {150, CANVAS_W - 150 - W * CELL};
        for (int i = 0; i < 2; ++i) {
            const Player& p = r.player[i];
            drawTray(p.incoming, x0[i], y0 - 54);
            const Pair coming = r.tsumo.pair(p.nextPair);
            drawField(gfx, p, view[i], x0[i], y0, settings.ghost, &coming, fieldPicture(i == 0 ? PICTURE_FIELD1 : PICTURE_FIELD2));
            drawNext(gfx, r, p, i == 0 ? x0[0] + W * CELL + 22 : x0[1] - 100, y0);
            drawScore(p, x0[i], y0 + CLEAR_H * CELL + 10);
            gfx.text(names[i], x0[i], 8, W * CELL, 30, 1, rgb(200, 206, 226), 0);
            drawAllClear(p, x0[i], y0);
            const auto tally = [](int won, int of, const wchar_t* full, const wchar_t* empty) {
                std::wstring marks;
                if (of > TALLY_MARKS) return std::wstring(full) + L" " + std::to_wstring(won) + L" / " + std::to_wstring(of);
                for (int w = 0; w < of; ++w) marks += w < won ? full : empty;
                return marks;
            };
            gfx.text(tally(wins[i], needed, L"★", L"☆"), i == 0 ? 440.f : 640.f, 420, 200, 40, 2, rgb(255, 220, 90), 0);
            if (setsNeeded > 1) gfx.text(tally(sets[i], setsNeeded, L"◆", L"◇"), i == 0 ? 440.f : 640.f, 392, 200, 30, 1, rgb(150, 210, 255), 0);
        }
        wchar_t time[32];
        swprintf_s(time, L"%d:%02d", int(frame / 3600), int(frame / 60 % 60));
        gfx.text(time, 540, 300, 200, 50, 2, rgb(255, 255, 255), 0);
        const int rate = r.rules.rateAt(frame);
        gfx.text(L"おじゃまレート " + std::to_wstring(rate), 490, 356, 300, 30, 1, rate < r.rules.targetPoints ? rgb(255, 150, 120) : rgb(170, 178, 204), 0);
    }
    // The dark band across the lower middle of a match: a title and a line below it. size: the font of the title;
    // bottom: where the band ends (further down when rows follow below the line).
    void banner(const std::wstring& title, const std::wstring& note, int size = 2, float bottom = 600) {
        gfx.round(rectF(340, 470, 940, bottom), 14, rgb(20, 24, 44, 0.9f));
        gfx.text(title, 340, size == 2 ? 480.f : 490.f, 600, 60, size, rgb(255, 236, 120), 0);
        gfx.text(note, 340, 550, 600, 30, 0, rgb(200, 206, 226), 0);
    }
    void drawVersus() {
        const Versus& v = versus;
        // (a person in seat i uses device[i], except against the AI: the only person uses device[0])
        const std::wstring names[2] = {L"1P  " + (v.agent[0] ? v.agentName[0] : deviceName(v.device[0])),
                                       L"2P  " + (v.agent[1] ? v.agentName[1] : deviceName(v.device[1]))};
        drawTwo(v.game.round, v.game.view, v.game.round.frame, v.wins, names, settings.rules.winsNeeded, v.sets, settings.rules.setsNeeded);
        // (an AI was short of time in the round before: between the fields at the top, until a round without it has
        //  ended: short, near the middle, off the fields, and not gone at once)
        if (v.weak) {
            gfx.round(rectF(446, 46, 834, 86), 10, rgb(20, 24, 44, 0.9f));
            gfx.text(L"処理落ち: AI が弱くなった可能性", 446, 50, 388, 30, 1, rgb(255, 170, 110), 0);
        }
        const int picks = v.state == Versus::MatchEnd ? (v.agent[0] ? 1 : 0) + (v.agent[1] ? 1 : 0) : 0;
        const RoundState s = v.game.round.state;
        if (v.paused) banner(L"一時停止", L"一時停止ボタンで再開　左回転 / Esc でメニューへ");
        else if (v.state == Versus::Ready) banner(v.timer > 30 ? L"レディ…" : L"ゴー！", L"");
        else if (v.state == Versus::RoundEnd) banner(s == RoundState::Draw ? L"引き分け" : s == RoundState::Won0 ? L"1P の勝ち" : L"2P の勝ち", L"");
        else if (v.state == Versus::SetEnd)
            banner(v.wins[0] > v.wins[1] ? L"1P がセットを取りました" : L"2P がセットを取りました", L"セット  " + std::to_wstring(v.sets[0]) + L" － " + std::to_wstring(v.sets[1]));
        else if (v.state == Versus::MatchEnd) {
            // (the band reaches down over the rows of the AI seats and the line below them)
            banner(v.sets[0] > v.sets[1] ? L"1P の優勝！" : L"2P の優勝！", L"決定 / 一時停止ボタンでもう一度　左回転 / Esc でメニューへ", 2,
                   picks ? 586 + float(picks) * 32 + 28 : 600);
            // (the AI of each AI seat and its level, changed with the mouse for the next match: updateVersus)
            for (int seat = 0; seat < 2; ++seat) {
                if (!v.agent[seat]) continue;
                const int style = seat ? settings.aiStyle : settings.aiStyle1, level = seat ? settings.aiLevel : settings.aiLevel1;
                const unsigned tint = levelColor(level);
                const std::wstring words[6] = {L"◀", style ? L"Juraic 催促（極小）" : L"Juraic 催促（小）", L"▶", L"◀", levelText(level), L"▶"};
                gfx.text(seat ? L"2P" : L"1P", 352, pickBox(seat, 0).top, 40, 28, 0, rgb(200, 206, 226));
                for (int k = 0; k < 6; ++k) {
                    const RectF r = pickBox(seat, k);
                    if (k % 3 != 1) gfx.round(rectF(r.left + 2, r.top + 1, r.right - 2, r.bottom - 1), 6, rgb(84, 104, 190));
                    gfx.text(words[k], r.left, r.top + 1, r.right - r.left, 26, 0,
                             k == 4 ? rgb(int(tint >> 16 & 255), int(tint >> 8 & 255), int(tint & 255)) : rgb(255, 255, 255), 0);
                }
            }
            if (picks) gfx.text(L"AI の変更: ◀ ▶ をクリック（レベルは押したままで連続。次の対戦から）", 340, 586 + float(picks) * 32, 600, 24, 0, rgb(150, 158, 186), 0);
        }
    }
    void drawLobby() {
        gfx.text(L"ネット対戦（人 対 人）", 0, 40, CANVAS_W, 60, 2, rgb(255, 255, 255), 0);
        const std::wstring hidden(netAddress.size(), L'●');
        const std::wstring rows[5] = {L"名前", L"部屋を立てる（相手を待つ）", L"相手のアドレス", L"部屋に入る（上のアドレスへ）", L"戻る"};
        const std::wstring values[5] = {netName, L"ポート " + std::to_wstring(netPort), netAddress.empty() ? L"（未入力）" : showAddress ? netAddress : hidden, L"", L""};
        for (int i = 0; i < 5; ++i) {
            const float y = 150 + float(i) * 56;
            const bool on = i == lobbyIndex;
            if (on) gfx.round(rectF(260, y - 6, 1020, y + 40), 8, lobbyEditing ? rgb(190, 110, 60) : rgb(84, 104, 190));
            gfx.text(rows[i], 284, y, 400, 34, 1, on ? rgb(255, 255, 255) : rgb(180, 188, 212));
            gfx.text(values[i] + (on && lobbyEditing ? L"_" : L""), 600, y, 400, 34, 1, rgb(255, 255, 255), 1);
        }
        const wchar_t* notes[] = {L"部屋を立てた側が 1P、入った側が 2P です。ルール（設定の「ルール」と、操作の「先行入力」「クイックドロップ」）は、立てた側のものを使います。",
                                  L"入る側は、立てた側のアドレス（同じネットワーク・VPN のアドレスなど。ポートが違う時は アドレス:ポート）を入れてください。",
                                  L"立てる側は、最初に Windows のファイアウォールの確認が出たら許可してください。ルーターの外からは、ポートの開放か VPN が要ります。",
                                  L"対戦中は一時停止できません。キーボードの Esc を 2 回押すと退出します。"};
        for (int i = 0; i < 4; ++i) gfx.text(notes[i], 0, 452 + float(i) * 30, CANVAS_W, 26, 0, rgb(150, 158, 186), 0);
        gfx.text(lobbyEditing ? L"文字を入力　Backspace で消す　Ctrl+V で貼り付け（アドレス）　Enter / Esc で終わり"
                              : L"↑↓ で選ぶ　Enter で決定・入力　F1 でアドレスを表示 / 隠す　Esc で戻る",
                 0, 640, CANVAS_W, 30, 0, rgb(150, 158, 186), 0);
    }
    void drawNet() {
        const NetPlay& n = net;
        if (n.peer.round == 0) {
            // Before the first round: waiting for a guest, or calling the host.
            gfx.text(L"ネット対戦（人 対 人）", 0, 40, CANVAS_W, 60, 2, rgb(255, 255, 255), 0);
            if (n.stage == NetPlay::Over) {
                banner(n.over, L"決定 / Esc で戻る", 1);
                return;
            }
            const bool found = n.peer.state == NetPeer::Measuring || n.peer.state == NetPeer::Joined;
            gfx.text(found ? L"つながりました。まもなく始まります…" : n.peer.host ? L"相手を待っています…" : L"相手を呼び出しています…", 0, 220, CANVAS_W, 50, 2, rgb(255, 236, 120), 0);
            if (n.peer.host) {
                gfx.text(L"ポート " + std::to_wstring(netPort) + L" で待ち受けています", 0, 300, CANVAS_W, 34, 1, rgb(200, 206, 226), 0);
                if (showAddress) {
                    gfx.text(L"この PC のアドレス（相手に伝えるもの。ルーターの外向きのアドレスはここには出ません）", 0, 350, CANVAS_W, 26, 0, rgb(150, 158, 186), 0);
                    for (size_t i = 0; i < ownAddressList.size() && i < 6; ++i)
                        gfx.text(std::wstring(ownAddressList[i].begin(), ownAddressList[i].end()), 0, 380 + float(i) * 28, CANVAS_W, 26, 0, rgb(255, 255, 255), 0);
                } else {
                    gfx.text(L"F1 でこの PC のアドレスを表示（配信中は映ることに注意）", 0, 350, CANVAS_W, 26, 0, rgb(150, 158, 186), 0);
                }
            }
            gfx.text(L"Esc でやめる", 0, 640, CANVAS_W, 30, 0, rgb(150, 158, 186), 0);
            return;
        }
        const auto nameOf = [&](int seat) {
            const std::wstring name = fromUtf8(n.peer.match.name[seat]).substr(0, 12);
            return std::wstring(seat ? L"2P  " : L"1P  ") + name + (seat == n.seat() ? L"（自分）" : L"");
        };
        const std::wstring names[2] = {nameOf(0), nameOf(1)};
        const Round& r = n.link.round;
        const Rules& rules = n.settings.rules;
        drawTwo(r, n.view, r.state == RoundState::Playing ? n.link.played[n.seat()] : r.frame, n.wins, names, rules.winsNeeded, n.sets, rules.setsNeeded);
        // (the line: the delay of the match, and the time there and back as it is now)
        gfx.text(L"遅延 " + std::to_wstring(n.peer.match.delay) + L" フレーム　往復 " + std::to_wstring(n.peer.roundTrip < 0 ? 0 : n.peer.roundTrip * 1000 / FPS) + L" ms", 446, 52, 388, 26, 0,
                 rgb(150, 158, 186), 0);
        const std::wstring score = L"セット  " + std::to_wstring(n.sets[0]) + L" － " + std::to_wstring(n.sets[1]);
        if (n.stage == NetPlay::Over) banner(n.over, L"決定 / Esc で戻る", 1);
        else if (netQuit) banner(L"退出しますか？", L"もう一度 Esc で退出（対戦はここで終わります）", 2);
        else if (n.stage == NetPlay::Ready) banner(n.timer > 30 ? L"レディ…" : L"ゴー！", L"");
        else if (n.stage == NetPlay::Play && n.waited > 30) banner(L"通信を待っています…", L"相手の操作が届くまで止まります", 1);
        else if (n.stage == NetPlay::RoundEnd) banner(r.state == RoundState::Draw ? L"引き分け" : r.state == RoundState::Won0 ? L"1P の勝ち" : L"2P の勝ち", L"");
        else if (n.stage == NetPlay::SetEnd) banner(n.wins[0] > n.wins[1] ? L"1P がセットを取りました" : L"2P がセットを取りました", score);
        else if (n.stage == NetPlay::MatchEnd)
            banner(n.sets[0] > n.sets[1] ? L"1P の優勝！" : L"2P の優勝！",
                   n.peer.again ? L"相手の返事を待っています…　Esc で退出" : n.peer.hisAgain ? L"相手がもう一度を希望しています　決定でもう一度　Esc で退出" : L"決定 / 一時停止ボタンでもう一度　Esc で退出");
        else if (n.stage == NetPlay::Connecting) banner(L"次の対戦を始めます…", L"");
    }
    void drawToko() {
        const Round& r = toko.game.round;
        const Player& p = r.player[0];
        const float x0 = 420, y0 = 96;
        const Pair coming = r.tsumo.pair(p.nextPair);
        drawField(gfx, p, toko.game.view[0], x0, y0, settings.ghost, &coming, fieldPicture(PICTURE_FIELD1));
        drawNext(gfx, r, p, x0 + W * CELL + 26, y0);

        // left: score and records
        const float lx = 60;
        const auto row = [&](float y, const std::wstring& label, const std::wstring& value) {
            gfx.text(label, lx, y, 300, 26, 0, rgb(150, 158, 186));
            gfx.text(value, lx, y + 22, 300, 44, 2, rgb(255, 255, 255));
        };
        wchar_t score[16];
        swprintf_s(score, L"%08d", p.scoring.score);
        row(96, L"スコア", score);
        row(180, L"手数", std::to_wstring(p.moves));
        row(264, L"最大連鎖", std::to_wstring(toko.maxChain) + L" 連鎖");
        row(348, L"直前の連鎖", toko.lastChain ? std::to_wstring(toko.lastChain) + L" 連鎖  " + std::to_wstring(toko.lastPoints) + L" 点" : L"―");
        wchar_t time[32];
        swprintf_s(time, L"%d:%02d", int(r.frame / 3600), int(r.frame / 60 % 60));
        row(432, L"時間", time);
        gfx.text(L"シード  " + std::to_wstring(toko.seed), lx, 530, 320, 26, 1, rgb(200, 206, 226));
        if (toko.hasBoard) gfx.text(L"練習盤面  " + toko.boardName, lx, 562, 340, 26, 0, rgb(160, 220, 170));
        drawAllClear(p, x0, y0);

        // right: prediction and keys
        const float rx = x0 + W * CELL + 130;
        if (settings.prediction && p.phase == Phase::Control) {
            gfx.round(rectF(rx, y0, rx + 360, y0 + 150), 10, rgb(40, 46, 70));
            gfx.text(L"連鎖の予測", rx + 16, y0 + 8, 300, 26, 0, rgb(150, 158, 186));
            const Prediction& q = toko.prediction;
            gfx.text(L"今の位置に置くと", rx + 16, y0 + 38, 330, 26, 0, rgb(200, 206, 226));
            gfx.text(q.chains ? std::to_wstring(q.chains) + L" 連鎖  " + std::to_wstring(q.points) + L" 点" : L"消えない", rx + 16, y0 + 58, 330, 34, 1,
                     q.chains ? rgb(255, 236, 120) : rgb(255, 255, 255));
            gfx.text(L"この組でできる最大", rx + 16, y0 + 92, 330, 26, 0, rgb(200, 206, 226));
            gfx.text(q.bestChains ? std::to_wstring(q.bestChains) + L" 連鎖  " + std::to_wstring(q.bestPoints) + L" 点" : L"消えない", rx + 16, y0 + 112, 330,
                     34, 1, q.bestChains ? rgb(255, 236, 120) : rgb(255, 255, 255));
        }
        const wchar_t* help[] = {L"移動・回転は「操作の割り当て」のとおり（↑ でも右回転）", L"Backspace / LB   1 手戻す（続けて押すとさらに戻る）", L"R / Y   同じツモでやり直す",
                                 L"N / Back   新しいツモ（割り当ては変更可）", L"O   盤面ファイルから練習", L"E   空の盤面に戻す", L"Ctrl+C / Ctrl+V   シードのコピー / 貼り付け",
                                 L"F5   ここまでをリプレイに保存", L"一時停止ボタン / Pause   一時停止", L"Esc   メニューへ"};
        for (int i = 0; i < 10; ++i) gfx.text(help[i], rx, y0 + 180 + i * 28.f, 440, 26, 0, rgb(150, 158, 186));
        if (toko.paused) gfx.text(L"一時停止中", x0 - 60, y0 + 250, W * CELL + 120, 50, 2, rgb(255, 255, 255), 0);
        if (r.state != RoundState::Playing)
            gfx.text(L"R でやり直し　Backspace で 1 手戻す", x0 - 80, y0 + CLEAR_H * CELL + 14, W * CELL + 160, 30, 1, rgb(255, 236, 120), 0);
    }
    void drawViewer() {
        const Viewer& w = viewer;
        if (w.replay.players == 2) {
            const std::wstring names[2] = {L"1P", L"2P"};
            // (the stars of the match that was played: its rule, and never fewer than the rounds a seat has won in the replay)
            const Rules& rules = w.replay.settings.rules;
            // (a set that has just been won is counted at once; the viewer itself counts it as the next round begins)
            const int sets[2] = {w.sets[0] + (rules.setsNeeded > 1 && w.wins[0] >= rules.winsNeeded), w.sets[1] + (rules.setsNeeded > 1 && w.wins[1] >= rules.winsNeeded)};
            drawTwo(w.game.round, w.game.view, w.game.round.frame, w.wins, names, std::max(rules.winsNeeded, std::max(w.wins[0], w.wins[1])), sets, std::max(rules.setsNeeded, std::max(sets[0], sets[1])));
        } else {
            const Player& p = w.game.round.player[0];
            const Pair coming = w.game.round.tsumo.pair(p.nextPair);
            drawField(gfx, p, w.game.view[0], 420, 96, false, &coming, fieldPicture(PICTURE_FIELD1));
            drawNext(gfx, w.game.round, p, 420 + W * CELL + 26, 96);
            drawScore(p, 420, 96 + CLEAR_H * CELL + 10);
            drawAllClear(p, 420, 96);
        }
        gfx.text(L"リプレイ  " + w.name + L"   ラウンド " + std::to_wstring(w.round + 1) + L" / " + std::to_wstring(w.replay.rounds.size()), 0, 36, CANVAS_W, 26,
                 0, rgb(160, 220, 170), 0);
        // (The keys: between the fields, or beside the one field. Along the bottom they ran into the scores.)
        const bool two = w.replay.players == 2;
        const float hx = two ? 440.f : 760.f, hy = two ? 500.f : 400.f;
        const int align = two ? 0 : -1;
        gfx.text(w.finished ? L"終わり" : w.paused ? L"一時停止中" : L"", hx, hy, 400, 30, 1, rgb(255, 236, 120), align);
        const wchar_t* help[] = {L"Space　一時停止", L"→　押している間 4 倍速", L"R　最初から", L"Esc　メニューへ"};
        for (int i = 0; i < 4; ++i) gfx.text(help[i], hx, hy + 36 + i * 26.f, 400, 26, 0, rgb(150, 158, 186), align);
    }
    // The picture the settings give a place, drawn over `area` (the whole canvas, or a field in the
    // place of its plain ground). False: there is none, or it cannot be read (said once).
    bool showPicture(int place, const RectF& area) {
        const std::wstring& name = settings.backgroundName[place];
        if (!settings.background[place] || name.empty()) return false;
        backdropShown[place] = backdrop[place].draw(gfx, mediaFile(L"background", std::filesystem::path(PICTURE_FOLDERS[place]) / name), area, settings.backgroundDim);
        if (!backdropShown[place] && backdropFailed[place] != name) {
            backdropFailed[place] = name;
            say(L"背景を読めませんでした: " + name);
        }
        return backdropShown[place];
    }
    std::function<bool(const RectF&)> fieldPicture(int place) {
        return [this, place](const RectF& area) { return showPicture(place, area); };
    }
    void draw(float width, float height) {
#ifdef _WIN32
        gfx.rt->BeginDraw();
        gfx.rt->SetTransform(D2D1::Matrix3x2F::Identity());
        gfx.rt->Clear(rgb(14, 16, 30));
        const float scale = std::min(width / CANVAS_W, height / CANVAS_H);
        gfx.rt->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) *
                             D2D1::Matrix3x2F::Translation((width - CANVAS_W * scale) / 2, (height - CANVAS_H * scale) / 2));
#else
        gfx.begin(int(width), int(height), rgb(14, 16, 30)); // (the canvas as large as fits, in the middle)
#endif
        gfx.fill(rectF(0, 0, CANVAS_W, CANVAS_H), rgb(28, 32, 54));
        for (bool& shown : backdropShown) shown = false;
        gfx.shadow = showPicture(PICTURE_OTHER, rectF(0, 0, CANVAS_W, CANVAS_H)); // (text over a picture gets a dark edge)
        switch (screen) {
        case Screen::Menu: drawMenu(); break;
        case Screen::Settings: drawSettings(); break;
        case Screen::Bindings: drawBindings(); break;
        case Screen::Toko: drawToko(); break;
        case Screen::Versus: drawVersus(); break;
        case Screen::Replay: drawViewer(); break;
        case Screen::NetLobby: drawLobby(); break;
        case Screen::Net: drawNet(); break;
        }
        if (messageTimer) gfx.text(message, 0, 692, CANVAS_W, 26, 0, rgb(255, 236, 120, std::min(1.f, float(messageTimer) / 30.f)), 0);
    }
    void present() {
#ifdef _WIN32
        RECT rc;
        GetClientRect(window, &rc);
        draw(float(rc.right), float(rc.bottom));
        if (gfx.rt->EndDraw() == D2DERR_RECREATE_TARGET) {
            target->Release();
            target = nullptr;
            createTarget();
        }
#else
        int width = 0, height = 0;
        windowPixels(window, width, height);
        draw(float(width), float(height));
        showFrame(window, gfx.raster);
#endif
    }

    // ---- self test: play by script, save one picture ----
    int screenshot(const std::filesystem::path& out, int frames) {
        // (a self test steps the screens as the game does, and those read the keyboard wherever the window stands: the
        //  person at the machine may be typing, and the settings file next to the exe is theirs - nothing of the
        //  machine is read, and nothing is saved)
        focused = false, selfTest = true;
#ifdef _WIN32
        IWICImagingFactory* wic = nullptr;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return 2;
        IWICBitmap* bitmap = nullptr;
        wic->CreateBitmap(UINT(CANVAS_W), UINT(CANVAS_H), GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
        ID2D1RenderTarget* rt = nullptr;
        if (FAILED(factory->CreateWicBitmapRenderTarget(bitmap, D2D1::RenderTargetProperties(), &rt))) return 3;
        bind(rt);
#endif
        const int mode = frames > 0 ? frames % 10 : 0;
        screen = frames == -2 || frames == -3 ? Screen::Bindings : frames == -4 ? Screen::NetLobby : frames == -6 ? Screen::Versus : frames < 0 ? Screen::Settings : frames == 0 ? Screen::Menu : mode == 8 ? Screen::Net : mode >= 2 ? Screen::Versus : Screen::Toko;
        if (frames <= -11 && frames > -11 - SETTING_SECTIONS) {
            // (-11 to -14: the parts of the settings screen, the cursor on the first setting that can be chosen)
            settingSection = -11 - frames, settingInside = true, settingIndex = -1;
            SettingsTable table(settings);
            for (size_t i = 0; i < table.items.size() && settingIndex < 0; ++i)
                if (table.items[i].section == settingSection && table.enabled(table.items[i], settings)) settingIndex = int(i);
        }
        // (JURAIC_SELFTEST_ALL_CLEAR set: the picture is taken with "all clear" shown on every field, for looking at where it stands)
        const bool showAllClear = !selfTestSetting(L"JURAIC_SELFTEST_ALL_CLEAR").empty();
        const bool person = !selfTestSetting(L"JURAIC_SELFTEST_PERSON").empty();
        int result = 0;
        if (frames == -5) {
            // A direction held down (heldStep). First a value of the settings screen, counted on the rounds a match is won
            // with; then the cursor of a list. Result 14 when a count is not what the rule says:
            //   the list opened with the direction to the right, which then stays down: the first setting does not change;
            //   from 2, to the right for 124 frames: one step at once, fifteen in the second after HELD_WAIT frames and
            //   twenty in the forty frames after it - 38; let go and pressed once - 39;
            //   from 3, to the left as long: past the lower end the value comes in at 99 and goes on down - 66;
            //   to the right for 40 frames (five steps), then the cursor moves down with the direction still held: neither
            //   the setting it leaves nor the one it comes to changes any more.
            settingSection = SEC_RULES, settingInside = false, settingIndex = 0;
            const auto frame = [&](unsigned held) { selfNav = held, updateSettings(), selfNavWas = selfNav; };
            const unsigned right = 1u << NAV_RIGHT, left = 1u << NAV_LEFT, below = 1u << NAV_DOWN;
            int wins = -1;
            {
                SettingsTable table(settings);
                for (size_t i = 0; i < table.items.size(); ++i)
                    if (table.items[i].key == "winsNeeded") wins = int(i);
            }
            Rules& rules = settings.rules;
            const int colors = rules.colors;
            for (int f = 0; f < 100; ++f) frame(right);
            bool good = wins >= 0 && settingInside && rules.colors == colors;
            frame(0);
            settingIndex = wins, rules.winsNeeded = 2;
            for (int f = 0; f < 124; ++f) frame(right);
            good = good && rules.winsNeeded == 38;
            frame(0), frame(right), frame(0);
            good = good && rules.winsNeeded == 39;
            rules.winsNeeded = 3;
            for (int f = 0; f < 124; ++f) frame(left);
            good = good && rules.winsNeeded == 66;
            frame(0);
            for (int f = 0; f < 40; ++f) frame(right);
            good = good && rules.winsNeeded == 71;
            const int sets = rules.setsNeeded;
            frame(right | below);
            for (int f = 0; f < 100; ++f) frame(right);
            good = good && settingIndex != wins && rules.winsNeeded == 71 && rules.setsNeeded == sets;
            // The cursor: down for 60 frames - one step at once and one every sixth frame after HELD_WAIT frames, seven in
            // all; let go and pressed once - eight. In the list of the settings, and in the menu. And a direction that is
            // down when the list is left for the list of the parts moves nothing there.
            frame(0);
            int moves = 0;
            for (int f = 0, was = settingIndex; f < 60; ++f, was = settingIndex) frame(below), moves += settingIndex != was;
            good = good && moves == 7;
            {
                const int was = settingIndex;
                frame(0), frame(below), frame(0);
                good = good && settingIndex != was;
            }
            const int part = settingSection;
            frame(below), settingInside = false; // (the list is left with the direction still down: back at the parts)
            for (int f = 0; f < 100; ++f) frame(below);
            good = good && !settingInside && settingSection == part;
            frame(0);
            screen = Screen::Menu, menuIndex = 0;
            for (int f = 0; f < 60; ++f) selfNav = below, updateMenu(), selfNavWas = selfNav;
            good = good && menuIndex == 7 && screen == Screen::Menu;
            selfNav = 0, updateMenu(), selfNavWas = 0;
            screen = Screen::Settings, settingInside = true, settingIndex = wins;
            selfNav = selfNavWas = 0;
            if (!good) result = 14;
            say(good ? L"a direction held down: the steps are as the rule says" : L"a direction held down: a count is not what the rule says");
        }
        if (frames == -6) {
            // The end of a match against an AI, the mouse button held down on ▶ of its level (updateVersus): from level 10,
            // 40 frames - one step at the click and four after HELD_WAIT frames, 15; on to 84 frames - 26; the pointer
            // off the mark with the button still down: no more; a click on ◀ of the AI itself with the button held as
            // long: it changes once. Result 15 when a count is not what the rule says.
            Versus& v = versus;
            v.device[0] = 0, v.device[1] = 1;
            v.seat(1, "x3", settings, false);
            v.startMatch(settings, 4242);
            v.state = Versus::MatchEnd;
            const auto at = [&](int k, bool click) {
                const RectF r = pickBox(1, k);
                mouseX = (r.left + r.right) / 2, mouseY = (r.top + r.bottom) / 2, mouseDown = true;
                if (click) clicked = true, clickX = mouseX, clickY = mouseY;
                updateVersus(), clicked = false;
            };
            settings.aiLevel = 10;
            const int style = settings.aiStyle;
            at(5, true);
            for (int f = 1; f < 40; ++f) at(5, false);
            bool good = settings.aiLevel == 15;
            for (int f = 40; f < 84; ++f) at(5, false);
            good = good && settings.aiLevel == 26;
            mouseX = 10, mouseY = 10;
            for (int f = 0; f < 100; ++f) updateVersus();
            good = good && settings.aiLevel == 26;
            mouseDown = false, updateVersus();
            at(0, true);
            for (int f = 1; f < 100; ++f) at(0, false);
            good = good && settings.aiStyle != style && settings.aiLevel == 26 && v.state == Versus::MatchEnd;
            mouseDown = false, updateVersus();
#ifdef _WIN32
            // The window's part (windowProc), on a window that is never shown: the button that goes down takes the
            // mouse, so that its release and the pointer are heard outside the window too; the release gives the mouse
            // back, and another window that takes it ends the hold.
            WNDCLASSW testClass{};
            testClass.lpfnWndProc = windowProc, testClass.hInstance = GetModuleHandleW(nullptr), testClass.lpszClassName = L"JuraicFieldSelfTest";
            RegisterClassW(&testClass);
            const HWND testWindow = CreateWindowExW(0, testClass.lpszClassName, L"", WS_POPUP, 0, 0, 320, 180, nullptr, nullptr, testClass.hInstance, nullptr);
            SendMessageW(testWindow, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
            good = good && testWindow && mouseDown && GetCapture() == testWindow;
            SendMessageW(testWindow, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
            good = good && !mouseDown && GetCapture() != testWindow;
            SendMessageW(testWindow, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
            ReleaseCapture(); // (what the window is told when another one takes the mouse)
            good = good && !mouseDown;
#endif
            clicked = false;
            if (!good) result = 15;
            say(good ? L"the mouse button held down on a level: the steps are as the rule says" : L"the mouse button held down on a level: a count is not what the rule says");
        }
        if (frames == -3) {
            // The binding rules, on the defaults: a button given to another
            // action of the same group changes places, the menus keep theirs.
            bindLayout = LAYOUT_PAD;
            InputConfig c;
            const int ccw = c.bind[LAYOUT_PAD][ACT_CCW], cw = c.bind[LAYOUT_PAD][ACT_CW], ok = c.bind[LAYOUT_PAD][ACT_MENU_OK];
            bool good = !c.clashes(LAYOUT_PAD) && !c.clashes(LAYOUT_XBOX) && !c.clashes(LAYOUT_KEYS1) && !c.clashes(LAYOUT_KEYS2);
            c.set(LAYOUT_PAD, ACT_CCW, cw);
            good = good && c.bind[LAYOUT_PAD][ACT_CCW] == cw && c.bind[LAYOUT_PAD][ACT_CW] == ccw && c.bind[LAYOUT_PAD][ACT_MENU_OK] == ok && !c.clashes(LAYOUT_PAD);
            c.set(LAYOUT_PAD, ACT_MENU_BACK, ok);
            good = good && c.bind[LAYOUT_PAD][ACT_MENU_BACK] == ok && c.bind[LAYOUT_PAD][ACT_MENU_OK] != ok && c.bind[LAYOUT_PAD][ACT_CW] == ccw && !c.clashes(LAYOUT_PAD);
            c.bind[LAYOUT_PAD][ACT_CW] = c.bind[LAYOUT_PAD][ACT_CCW]; // as an old file could say
            good = good && c.clashes(LAYOUT_PAD);
            if (!good) result = 9;
        }
        if (screen == Screen::Toko) {
            {
                // Going back (Toko::undo): the round and the recorded buttons must then be those of the start of the move
                // gone back to - a fresh game played with the buttons that are left stands exactly where this one stands.
                Toko trial;
                trial.seed = 12345;
                trial.start(settings);
                ScriptBot script;
                for (int f = 0; f < 900; ++f) trial.step(script(trial.game.round.player[0], f));
                if (trial.history.size() < 4) return 12;
                for (int back = 0; back < 3; ++back) {
                    trial.undo();
                    Game again;
                    again.start(settings.rules, trial.seed, 1);
                    PlayerEvents e[2];
                    for (const uint16_t b : trial.game.log.frames) {
                        const uint8_t buttons[2] = {uint8_t(b & 255), uint8_t(b >> 8)};
                        again.step(buttons, e);
                    }
                    if (roundSummary(again.round) != roundSummary(trial.game.round) || again.log.frames != trial.game.log.frames) return 12;
                    for (int f = 0; f < 60; ++f) trial.step(0); // (on from there, so that the next undo has a move to go back to)
                }
            }
            toko.seed = 12345;
            toko.start(settings);
            ScriptBot bot;
            for (int f = 0; f < frames; ++f) toko.step(bot(toko.game.round.player[0], f));
            // Then go on without input until the wanted moment: a pair in the air, or the middle of a wave;
            // n ending in 10 to 80: so many frames before a pair appears (the window of the next pairs
            // on its way, Timing::nextSlide; in the last of them the pair is drawn where it appears).
            const int before = mode == 0 ? frames / 10 % 10 : 0;
            for (int f = 0; f < 3000; ++f) {
                const Player& p = toko.game.round.player[0];
                const bool falling = frames % 100 == 11;
                if (before ? p.phase == Phase::Next && p.wait == before
                    : mode == 1 ? p.phase == Phase::Wave && toko.game.view[0].total - p.wait == (falling ? 55 : 20) && (!falling || p.wave.longestFall > 0)
                              : p.phase == Phase::Control && p.pair.y <= 8)
                    break;
                toko.step(mode == 1 && p.phase == Phase::Control ? uint8_t(BTN_DOWN) : uint8_t(0));
            }
            if (settings.prediction) toko.predict();
        } else if (screen == Screen::Versus && frames == -6) {
            // (the picture is that of the end of the match, as the test above left it)
        } else if (screen == Screen::Versus) {
            Versus& v = versus;
            v.device[0] = 0, v.device[1] = 1;
            settings.rules.winsNeeded = 2;
            if (mode == 4 || mode == 5) v.seat(0, frames % 100 >= 10 ? "x3" : "random", settings, false), v.seat(1, frames % 100 >= 10 ? "x3" : "random", settings, false); // two AIs instead of the scripts (n ending in 14 or 15: X3; otherwise random placements)
            // (two AIs as in a match on the screen: with the clock, and their thinking judged. They are the AIs
            //  of the settings, as when two AIs are watched: X3 and X3 unless the settings file names other levels.)
            // (JURAIC_SELFTEST_PERSON set: the 1P seat is left to the script, as a person against the AI - for the picture of
            //  such a match on a machine where the AI is short of time)
            if (mode == 6 || mode == 7) v.seat(0, person ? "" : aiOfLevel(settings.aiLevel1, settings.aiStyle1), settings), v.seat(1, aiOfLevel(settings.aiLevel, settings.aiStyle), settings);
            v.startMatch(settings, 4242);
            ScriptBot bots[2];
            bots[1].rng = Rng(99);
            // (7: as 6, at the speed of the screen, sixty frames a second by the clock. In 6 the frames
            // come as fast as the machine makes them, hundreds while an AI makes one choice: its pairs
            // come to rest before they are placed, and nothing that uses the time a pair is in the
            // air is tried. n frames take n / 60 seconds.)
            const std::chrono::steady_clock::time_point paced = std::chrono::steady_clock::now();
            for (int f = 0; f < frames; ++f) {
                if (mode == 7)
                    while (std::chrono::steady_clock::now() < paced + std::chrono::microseconds(int64_t(f) * 1000000 / 60)) std::this_thread::yield();
                if (v.state == Versus::Ready || v.state == Versus::RoundEnd) v.timer = 1;
                if (v.state == Versus::MatchEnd) break;
                if (v.state != Versus::Play) {
                    // the same steps as updateVersus without input devices
                    if (v.state == Versus::Ready) v.state = Versus::Play;
                    else if (v.state == Versus::SetEnd) v.wins[0] = v.wins[1] = 0, v.startRound(settings);
                    else if (const int winner = v.setWon(settings.rules); winner < 0) v.startRound(settings);
                    else v.state = v.sets[winner] >= settings.rules.setsNeeded ? Versus::MatchEnd : Versus::SetEnd;
                    continue;
                }
                uint8_t buttons[2];
                for (int i = 0; i < 2; ++i) buttons[i] = v.agent[i] ? v.agentButtons(i) : bots[i](v.game.round.player[i], f);
                PlayerEvents e[2];
                v.game.step(buttons, e);
                if (v.game.round.state != RoundState::Playing) {
                    v.roundOver();
                    v.keepNotes(); // (waits for an AI in the middle of a choice: judgeThinking reads what its thread writes)
                    judgeThinking();
                }
            }
            // (not with JURAIC_SELFTEST_PERSON: the picture is then that of the match itself)
            if ((mode == 3 || mode == 4 || mode == 6 || mode == 7) && !person) {
                // Save what was played, load it and play it again: the result must be the same.
                // (6: the replay of two AIs with the clock, whose AI lines a measuring tool reads to make the choices again)
                if (v.state == Versus::Play) v.keepRound();
                v.keepNotes();
                const std::filesystem::path file = out.parent_path() / L"selftest.jfreplay";
                Replay loaded;
                if (!saveReplay(file, v.replay) || !loadReplay(file, loaded) || loaded.rounds.size() != v.replay.rounds.size()) return 7;
                viewer = Viewer{};
                viewer.replay = loaded;
                viewer.name = file.stem().wstring();
                viewer.startRound();
                screen = Screen::Replay;
                focused = false; // no keys, no controllers
                for (int f = 0; f < frames * 2 && !viewer.finished; ++f) updateViewer();
                const Game& a = v.game;
                const Game& b = viewer.game;
                for (int i = 0; i < 2; ++i)
                    if (!(a.round.player[i].field == b.round.player[i].field) || a.round.player[i].scoring.score != b.round.player[i].scoring.score) result = 8;
                if (a.round.frame != b.round.frame) result = 8;
                // (the stars: the rounds the replay counted are those of the match. A round cut off by the end of the script has no winner.)
                if (viewer.wins[0] != v.wins[0] || viewer.wins[1] != v.wins[1]) result = 8;
                say(result ? L"replay differs from the game" : L"replay equals the game");
            }
        } else if (screen == Screen::Net) {
            // A match over a line inside the program that delays, loses and reorders: this end is the host, a second end
            // the guest, scripts play both. Both ends must keep the same rounds, and the replay of the match, saved and
            // read again, must end every round as the match did (result 13 when something differs).
            // (JURAIC_SELFTEST_NET = "delay jitter loss fixed stop cut view", for looking at a match on a line of one's
            //  choosing: the line's frames one way, their swing and the datagrams lost of a hundred; the delay of the
            //  match set by hand (0: by the line); stop: the picture is taken after so many frames of play instead of at
            //  the end of the match, and nothing is compared; cut: before it the line is dead for so many frames;
            //  view 1: the picture is the guest's screen at that moment instead of the host's.)
            int lineDelay = 3, lineJitter = 2, lineLoss = 5, fixedDelay = 0, stop = 0, cut = 0, view = 0;
            swscanf_s(selfTestSetting(L"JURAIC_SELFTEST_NET").c_str(), L"%d %d %d %d %d %d %d", &lineDelay, &lineJitter, &lineLoss, &fixedDelay, &stop, &cut, &view);
            auto wire = std::make_shared<FakeWire>();
            wire->delay = lineDelay, wire->jitter = lineJitter, wire->loss = lineLoss, wire->rng = Rng(8, 8);
            settings.rules.winsNeeded = 2, settings.rules.setsNeeded = 1; // (whatever the settings file says: the match of the test is won with two rounds)
            net.host(std::make_unique<FakeLine>(wire, 0), settings, "host", 4242, fixedDelay);
            const auto guest = std::make_unique<NetPlay>();
            // (the guest comes with rules of its own, with the other setting of pre-input - a rule of the round that
            //  stands with the buttons on the settings screen - and with another volume: it must play by the host's
            //  rules, and keep what is not a rule)
            Settings guestSettings = settings;
            guestSettings.rules.colors = settings.rules.colors == 4 ? 5 : 4, guestSettings.rules.winsNeeded = 5, guestSettings.rules.setsNeeded = 3;
            guestSettings.rules.preInput = !settings.rules.preInput, guestSettings.quickDrop = !settings.quickDrop;
            guestSettings.volume = settings.volume == 35 ? 40 : 35;
            guest->join(std::make_unique<FakeLine>(wire, 1), guestSettings, "guest");
            ScriptBot bots[2];
            bots[1].rng = Rng(99);
            bots[0].early = bots[1].early = true;
            const auto nothing = [](int, const PlayerEvents&) {};
            std::vector<std::string> ends;
            std::string summaries; // (how every round ended, as --check-replay writes it)
            int played = 0, left = cut;
            for (int f = 0; f < frames * 4 && !(net.stage == NetPlay::MatchEnd && guest->stage == NetPlay::MatchEnd); ++f) {
                if (net.stage == NetPlay::Play) ++played;
                if (stop && played >= stop) {
                    if (left-- <= 0) break;
                    wire->loss = 100;
                }
                for (NetPlay* n : {&net, guest.get()}) {
                    if ((n->stage == NetPlay::Ready || n->stage == NetPlay::RoundEnd || n->stage == NetPlay::SetEnd) && n->timer > 2) n->timer = 2;
                    const NetPlay::Stage before = n->stage;
                    n->tick(bots[n->seat()](n->link.round.player[n->seat()], f), nothing);
                    if (n == &net && before == NetPlay::Play && n->stage == NetPlay::RoundEnd)
                        ends.push_back(std::to_string(int(n->link.round.state)) + " " + std::to_string(n->link.round.frame)), summaries += roundSummary(n->link.round) + '\n';
                    if (n->stage == NetPlay::Over && !stop) result = 13;
                }
            }
            if (!stop && (net.stage != NetPlay::MatchEnd || guest->stage != NetPlay::MatchEnd || net.replay.rounds.size() != guest->replay.rounds.size())) result = 13;
            if (!stop && !result) {
                const juraic::Rules& his = guest->settings.rules;
                if (his.colors != settings.rules.colors || his.winsNeeded != settings.rules.winsNeeded || his.setsNeeded != settings.rules.setsNeeded || his.preInput != settings.rules.preInput ||
                    guest->settings.quickDrop != settings.quickDrop || guest->settings.diagonal != guestSettings.diagonal || guest->settings.volume != guestSettings.volume)
                    result = 13;
                // (the button of the quick drop is there with the setting given, and not without it)
                if (!(personButtons(0, 0, true, true) & BTN_DROP) || (personButtons(0, 0, true, false) & BTN_DROP)) result = 13;
                // (and of the host's settings nothing was sent but what a match needs)
                const std::string sent = NetPlay::rulesText(settings);
                if (sent.find("colors=") == std::string::npos || sent.find("preInput=") == std::string::npos || sent.find("quickDrop=") == std::string::npos || sent.find("diagonal=") != std::string::npos ||
                    sent.find("volume=") != std::string::npos || sent.find("aiLevel=") != std::string::npos)
                    result = 13;
            }
            for (size_t i = 0; !stop && !result && i < net.replay.rounds.size(); ++i)
                if (net.replay.rounds[i].frames != guest->replay.rounds[i].frames || net.replay.rounds[i].seed != guest->replay.rounds[i].seed) result = 13;
            if (!result && !stop) {
                const std::filesystem::path file = out.parent_path() / L"selftest-net.jfreplay";
                Replay loaded;
                if (!saveReplay(file, net.replay) || !loadReplay(file, loaded) || loaded.rounds.size() != ends.size() || loaded.linkDelay != net.peer.match.delay) return 7;
                const auto game = std::make_unique<Game>();
                for (size_t i = 0; i < loaded.rounds.size(); ++i) {
                    game->startReplay(loaded, loaded.rounds[i]);
                    for (const uint16_t b : loaded.rounds[i].frames) {
                        const uint8_t buttons[2] = {uint8_t(b & 255), uint8_t(b >> 8)};
                        PlayerEvents e[2];
                        game->step(buttons, e);
                    }
                    if (std::to_string(int(game->round.state)) + " " + std::to_string(game->round.frame) != ends[i]) result = 13;
                }
                // (--check-replay plays the file the same way: every round must end as the match did, to the score and the fields)
                if (replayResult(loaded) != summaries) result = 13;
                // (a file dropped on the window while the match is on is not opened: the line would stay open behind another screen)
                openDropped(file);
                if (screen != Screen::Net || !net.peer.line) result = 13;
                screen = Screen::Net;
            }
            say(result ? L"net match: the two ends or the replay differ, or the guest does not play by the host's rules alone" + (net.over.empty() && guest->over.empty() ? std::wstring() : L" (" + net.over + guest->over + L")")
                       : L"net match: both ends and the replay agree (" + std::to_wstring(ends.size()) + L" rounds, delay " +
                                                                               std::to_wstring(net.peer.match.delay) + L", waited " + std::to_wstring(net.waitedInAll) + L" frames)");
            if (stop) {
                const NetPlay& n = view ? *guest : net;
                say(std::wstring(view ? L"guest" : L"host") + L": line " + std::to_wstring(lineDelay) + L"+-" + std::to_wstring(lineJitter) + L" frames, loss " + std::to_wstring(lineLoss) + L"%; own seat at frame " +
                    std::to_wstring(n.link.played[n.seat()]) + L", other seat at frame " + std::to_wstring(n.link.played[1 - n.seat()]) + L"; waited " + std::to_wstring(n.waitedInAll) + L" frames so far");
                if (view) std::swap(net, *guest);
            }
        } else if (screen == Screen::Menu) {
            // The menu picture also reports the controllers the system knows. And the music the
            // settings ask for must be playing in every scene (result 10 when one is not): each scene
            // is given until its music plays or its file has turned out unreadable, twenty seconds at
            // most (a large file on a busy machine takes seconds to read), and half a second at least.
            audio.init();
#ifdef _WIN32
            MFStartup(MF_VERSION, MFSTARTUP_LITE);
#endif
            std::wstring heard;
            const wchar_t* const sceneNames[MUSIC_SCENES] = {L"対戦", L"とこ", L"メニュー"};
            for (int scene : {int(MUSIC_OTHER), int(MUSIC_VERSUS), int(MUSIC_TOKO), int(MUSIC_OTHER)}) {
                musicSceneForced = scene;
                for (int i = 0; i < 1000; ++i) {
#ifdef _WIN32
                    MSG msg;
                    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
#else
                    pumpEvents();
#endif
                    pads.update();
                    updateMedia();
#ifdef _WIN32
                    Sleep(20);
#else
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
#endif
                    const bool settled = musicWanted.empty() ? !audio.musicOn() : (audio.musicOn() && musicPlaying == musicWanted) || musicFailed.count(musicWanted) != 0;
                    if (settled && i >= 25) break;
                }
                const bool on = audio.musicOn() && musicPlaying == musicWanted;
                if (!musicWanted.empty()) heard += std::wstring(L"　") + sceneNames[scene] + L": " + (on ? musicPlaying : L"鳴っていません");
                else heard += std::wstring(L"　") + sceneNames[scene] + L": なし";
                if (!musicWanted.empty() && !on) result = 10;
                if (musicWanted.empty() && audio.musicOn()) result = 10;
            }
            musicSceneForced = -1;
            const bool any = settings.bgm[MUSIC_VERSUS] || settings.bgm[MUSIC_TOKO] || settings.bgm[MUSIC_OTHER];
            say(pads.describe() + (any ? L"　BGM" + heard : L""));
        }
        if (showAllClear)
            for (Game* g : {&toko.game, &versus.game, &viewer.game})
                for (Player& p : g->round.player) p.scoring.allClear = true;
        // (JURAIC_SELFTEST_WEAK set: the picture of a match is taken with the words for an AI short of time, for looking at where they stand)
        if (!selfTestSetting(L"JURAIC_SELFTEST_WEAK").empty()) versus.weak = true;
        // (JURAIC_SELFTEST_WINS set: the picture of a match is taken with these numbers of rounds and sets to win and won)
        if (int n[6]{}; swscanf_s(selfTestSetting(L"JURAIC_SELFTEST_WINS").c_str(), L"%d %d %d %d %d %d", &n[0], &n[1], &n[2], &n[3], &n[4], &n[5]) == 6) {
            settings.rules.winsNeeded = n[0], settings.rules.setsNeeded = n[1];
            versus.wins[0] = n[2], versus.wins[1] = n[3], versus.sets[0] = n[4], versus.sets[1] = n[5];
        }
        draw(CANVAS_W, CANVAS_H);
#ifdef _WIN32
        if (FAILED(rt->EndDraw())) return 4;
#endif
        {
            // (a picture the settings name must be there: the one of the screen always, those of the fields where fields are)
            const bool fields = screen == Screen::Toko || screen == Screen::Versus || screen == Screen::Replay;
            const bool two = screen == Screen::Versus || (screen == Screen::Replay && viewer.replay.players == 2);
            const bool wanted[PICTURE_PLACES] = {fields, two, true};
            for (int k = 0; k < PICTURE_PLACES; ++k)
                if (settings.background[k] && wanted[k] && !backdropShown[k] && !result) result = 11;
        }
#ifndef _WIN32
        if (!gfx.raster.savePng(out.string())) return 5;
#else
        IWICStream* stream = nullptr;
        IWICBitmapEncoder* encoder = nullptr;
        IWICBitmapFrameEncode* frame = nullptr;
        wic->CreateStream(&stream);
        if (FAILED(stream->InitializeFromFilename(out.c_str(), GENERIC_WRITE))) return 5;
        wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
        encoder->Initialize(stream, WICBitmapEncoderNoCache);
        encoder->CreateNewFrame(&frame, nullptr);
        frame->Initialize(nullptr);
        if (FAILED(frame->WriteSource(bitmap, nullptr))) return 6;
        frame->Commit();
        encoder->Commit();
#endif
        return result;
    }
};

App* app = nullptr;

#ifdef _WIN32
LRESULT CALLBACK windowProc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_SIZE:
        if (app && app->target) app->target->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
        return 0;
    case WM_SETFOCUS:
        if (app) app->focused = true;
        return 0;
    case WM_KILLFOCUS:
        if (app) app->focused = false, app->mouseDown = false;
        if (GetCapture() == w) ReleaseCapture();
        return 0;
    case WM_CHAR:
        if (app && app->typed.size() < 256) app->typed += wchar_t(wp);
        return 0;
    case WM_CAPTURECHANGED: // (the mouse is another window's now: the release of the button will not be heard)
        if (app && HWND(lp) != w) app->mouseDown = false;
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSEMOVE:
        // (while the button is down the mouse is this window's, SetCapture: its release and the pointer are heard
        //  outside the window too - a level held at the end of a match would go on changing otherwise)
        if (m == WM_LBUTTONDOWN) SetCapture(w);
        if (m == WM_LBUTTONUP && GetCapture() == w) ReleaseCapture();
        if (app) {
            // (to the canvas, which App::draw scales into the middle of the window)
            RECT rc;
            GetClientRect(w, &rc);
            const float scale = std::min(float(rc.right) / CANVAS_W, float(rc.bottom) / CANVAS_H);
            if (scale > 0) {
                app->mouseX = (float(short(LOWORD(lp))) - (float(rc.right) - CANVAS_W * scale) / 2) / scale;
                app->mouseY = (float(short(HIWORD(lp))) - (float(rc.bottom) - CANVAS_H * scale) / 2) / scale;
                if (m == WM_LBUTTONDOWN) app->clickX = app->mouseX, app->clickY = app->mouseY, app->clicked = true;
            }
            if (m != WM_MOUSEMOVE) app->mouseDown = m == WM_LBUTTONDOWN;
        }
        return 0;
    case WM_DROPFILES: {
        wchar_t file[MAX_PATH];
        if (app && DragQueryFileW(HDROP(wp), 0, file, MAX_PATH)) app->openDropped(file);
        DragFinish(HDROP(wp));
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}
#else
// What the window was told since the last frame (SDL): keys, typed text, the mouse, focus, a dropped file, closing.
void handleEvents(App& a) {
    for (SDL_Event e; SDL_PollEvent(&e);) {
        switch (e.type) {
        case SDL_EVENT_QUIT: a.quit = true; break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED: a.focused = true; break;
        case SDL_EVENT_WINDOW_FOCUS_LOST: a.focused = false, a.mouseDown = false, forgetAllKeys(); break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: noteKey(e.key.scancode, e.type == SDL_EVENT_KEY_DOWN); break;
        case SDL_EVENT_TEXT_INPUT:
            for (const wchar_t c : fromUtf8(e.text.text))
                if (a.typed.size() < 256) a.typed += c;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_MOTION: {
            if (e.type != SDL_EVENT_MOUSE_MOTION && e.button.button != SDL_BUTTON_LEFT) break;
            const float x = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.x : e.button.x, y = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.y : e.button.y;
            // (to the canvas, which App::draw scales into the middle of the window)
            float cx = 0, cy = 0;
            if (canvasPoint(a.window, x, y, cx, cy)) {
                a.mouseX = cx, a.mouseY = cy;
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) a.clickX = cx, a.clickY = cy, a.clicked = true;
            }
            // (while the button is down the mouse is this window's: its release and the pointer are heard outside it too)
            if (e.type != SDL_EVENT_MOUSE_MOTION) a.mouseDown = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_CaptureMouse(a.mouseDown);
            break;
        }
        case SDL_EVENT_DROP_FILE:
            if (e.drop.data) a.openDropped(std::filesystem::path(fromUtf8(e.drop.data)));
            break;
        default: break;
        }
    }
}
#endif

} // namespace

#ifndef _WIN32
int main(int argc, char** argv) {
    static App a; // large (move history): not on the stack
    app = &a;
    a.exeDirectory = programFolder();
    programFolders(a.dataDirectory, a.userDirectory);
    a.loadAll();
    a.gfx.loadFonts(a.dataDirectory / "fonts");
    std::filesystem::path board, replay;
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--board") board = fromUtf8(argv[i + 1]);
        if (arg == "--replay") replay = fromUtf8(argv[i + 1]);
        if (arg == "--check-replay") return checkReplay(fromUtf8(argv[i + 1]));
        if (arg == "--net-test" && i + 3 < argc) {
            const std::string kind = argv[i + 1];
            const int host = kind == "host" ? 1 : kind == "host4" ? 2 : kind == "hostall" ? 3 : 0;
            if (!host && i + 4 >= argc) return 2;
            return netTest(a.net, host, host ? std::string() : std::string(argv[i + 2]), std::atoi(argv[i + (host ? 2 : 3)]), fromUtf8(argv[i + (host ? 3 : 4)]));
        }
        if (arg == "--screenshot") {
            if (!board.empty()) a.loadBoardFile(board);
            return a.screenshot(fromUtf8(argv[i + 1]), i + 2 < argc ? std::atoi(argv[i + 2]) : 600);
        }
    }
    if (!openWindow(a.window, "Juraic Field", 1280, 720)) return 1;
    a.audio.init();
    a.audio.setVolume(a.settings.volume);
    a.mediaChanged();
    if (!board.empty() && a.loadBoardFile(board)) {
        a.toko.start(a.settings);
        a.screen = Screen::Toko;
    }
    if (!replay.empty()) a.openReplay(replay);

    // Game frames are 1/60 s whatever the display does; drawing waits for the display.
    using Clock = std::chrono::steady_clock;
    const std::chrono::nanoseconds frameTime(1000000000 / FPS);
    Clock::time_point next = Clock::now();
    for (;;) {
        wantTyping(a.window, a.screen == Screen::NetLobby && a.lobbyEditing);
        handleEvents(a);
        if (a.quit) {
            a.net.leave(); // (the other end of a match is told)
            a.saveAll();
            closeWindow(a.window);
            return 0;
        }
        const Clock::time_point now = Clock::now();
        if (now - next > frameTime * 10) next = now - frameTime; // after a stall: do not catch up
        while (next <= now) {
            a.update();
            next += frameTime;
        }
        a.present();
    }
}
#else
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    static App a; // large (move history): not on the stack
    app = &a;
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    a.exeDirectory = std::filesystem::path(path).parent_path();
    a.dataDirectory = a.userDirectory = a.exeDirectory;
    a.loadAll();
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &a.factory);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&a.dwrite));
    a.createFonts();

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::filesystem::path board, replay;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::wstring(argv[i]) == L"--board") board = argv[i + 1];
        if (std::wstring(argv[i]) == L"--replay") replay = argv[i + 1];
        if (std::wstring(argv[i]) == L"--check-replay") return checkReplay(argv[i + 1]);
        if (std::wstring(argv[i]) == L"--net-test" && i + 3 < argc) {
            const int host = std::wstring(argv[i + 1]) == L"host" ? 1 : std::wstring(argv[i + 1]) == L"host4" ? 2 : std::wstring(argv[i + 1]) == L"hostall" ? 3 : 0;
            const std::wstring address = host ? L"" : argv[i + 2];
            if (!host && i + 4 >= argc) return 2;
            return netTest(a.net, host, toUtf8(address),_wtoi(argv[i + (host ? 2 : 3)]), argv[i + (host ? 3 : 4)]);
        }
        if (std::wstring(argv[i]) == L"--screenshot") {
            if (!board.empty()) a.loadBoardFile(board);
            return a.screenshot(argv[i + 1], i + 2 < argc ? _wtoi(argv[i + 2]) : 600);
        }
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"JuraicFieldWindow";
    RegisterClassW(&wc);
    RECT rc{0, 0, 1280, 720};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    a.window = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, L"Juraic Field", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                               rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, instance, nullptr);
    if (!a.window || !a.createTarget()) return 1;
    a.audio.init();
    a.audio.setVolume(a.settings.volume);
    MFStartup(MF_VERSION, MFSTARTUP_LITE);
    a.mediaChanged();
    if (!board.empty() && a.loadBoardFile(board)) {
        a.toko.start(a.settings);
        a.screen = Screen::Toko;
    }
    if (!replay.empty()) a.openReplay(replay);
    ShowWindow(a.window, show);

    // Game frames are 1/60 s whatever the display does; drawing waits for the display.
    using Clock = std::chrono::steady_clock;
    const std::chrono::nanoseconds frameTime(1000000000 / FPS);
    Clock::time_point next = Clock::now();
    for (MSG msg;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                a.net.leave(); // (the other end of a match is told)
                a.saveAll();
                return 0;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        const Clock::time_point now = Clock::now();
        if (now - next > frameTime * 10) next = now - frameTime; // after a stall: do not catch up
        while (next <= now) {
            a.update();
            next += frameTime;
        }
        a.present();
    }
}
#endif
