#pragma once
// The mode in which Juraic plays 2P of the real game (play/realgame_win.h), the Windows build: the buttons of the real
// game pressed as keys of the keyboard, with no driver. The keys are those of the real game's own settings file (its
// InputConfig: numbers of a key as its scan code, 128 more for a key of the extended set), and they are sent through the
// system's own ways for input: SendInput (by scan code, or by virtual key code) to the window in front - only while
// that window is the real game's -, or as the messages of a key to the real game's window. Nothing of the real game's
// program is written, opened or changed.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace play {

// The keys the real game has for its buttons (the first of each row of its settings file), and those of its menus.
struct GameKeys {
    int left = 203, right = 205, down = 208, turnRight = 45, turnLeft = 44, pause = 1;
    static constexpr int MENU_OK = 28, MENU_BACK = 1; // (Enter, Esc)

    // The settings file of a program: config.ini in the folder of the program's name under the person's local files.
    static std::filesystem::path fileOf(const std::wstring& program) {
        wchar_t local[MAX_PATH];
        const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
        if (!n || n >= MAX_PATH || program.empty()) return {};
        return std::filesystem::path(local) / std::filesystem::path(program).stem() / L"config.ini";
    }
    // The number of the first key of the row `name` in the text of the file (0: not there).
    static int keyOf(const std::string& text, const std::string& name) {
        const size_t row = text.find("<" + name + " ");
        if (row == std::string::npos) return 0;
        const size_t end = text.find('>', row), at = text.find("B0=\"", row);
        if (at == std::string::npos || end == std::string::npos || at > end) return 0;
        int v = 0;
        for (size_t i = at + 4; i < end && text[i] >= '0' && text[i] <= '9'; ++i) v = v * 10 + (text[i] - '0');
        return v > 0 && v < 256 ? v : 0;
    }
    // Takes the keys of the text of a settings file; a row not found keeps the key it has. How many rows were found.
    int take(const std::string& text) {
        int found = 0;
        const auto row = [&](const char* name, int& key) {
            if (const int v = keyOf(text, name)) key = v, ++found;
        };
        row("Left", left), row("Right", right), row("SoftDrop", down), row("RotateRight", turnRight), row("RotateLeft", turnLeft), row("Pause", pause);
        return found;
    }
    int read(const std::filesystem::path& file) {
        std::ifstream in(file, std::ios::binary);
        if (!in) return 0;
        return take(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()));
    }
};

class KeySender {
public:
    enum How { SCAN, VIRTUAL, WINDOW };
    GameKeys keys;
    int keysRead = 0; // rows of the settings file the keys were taken from (0: the file was not read)
    std::atomic<long> sent{0};    // key events sent
    std::atomic<long> away{0};    // times keys were not sent because the real game's window was not in front
    std::atomic<long> refused{0}; // key events the system did not take

    explicit KeySender(decltype(&GetForegroundWindow) foreground = GetForegroundWindow,
                       decltype(&SendInput) sendInput = SendInput)
        : foreground_(foreground), sendInput_(sendInput) {}
    KeySender(const KeySender&) = delete;
    KeySender& operator=(const KeySender&) = delete;
    ~KeySender() { stop(); }

    bool start(HWND window, const std::wstring& program, How way, std::wstring& error) {
        stop();
        if (!window || !IsWindow(window)) return error = L"実機のウィンドウを特定できません（先に「読み取り開始」で実機のウィンドウを選択してください）", false;
        const std::lock_guard<std::mutex> one(setting);
        target = window, how = way, keys = GameKeys{};
        keysRead = keys.read(GameKeys::fileOf(program));
        down = {}, lost = false, sent = 0, away = 0, refused = 0, on = true;
        return true;
    }
    void stop() {
        const std::lock_guard<std::mutex> one(setting);
        if (on) apply({});
        on = false, target = nullptr;
    }
    bool running() const { return on; }
    // Holds these keys and no others (numbers as GameKeys has them). False when they are not all held so: the keys are
    // not begun, the real game's window is not in front, or the system did not take every key sent - the keys it did not
    // take stay as they were, and are sent again the next time.
    bool hold(const std::vector<int>& wanted) {
        const std::lock_guard<std::mutex> one(setting);
        return on && apply(wanted);
    }
    // The keys held down now, as far as the system took them.
    std::array<bool, 256> held() const {
        const std::lock_guard<std::mutex> one(setting);
        return down;
    }

    // The key's scan code, whether it is of the extended set, and its virtual key code.
    static WORD scanOf(int key) { return WORD(key & 0x7F); }
    static bool extendedOf(int key) { return (key & 0x80) != 0; }
    static WORD virtualOf(int key) { return WORD(MapVirtualKeyW(UINT(extendedOf(key) ? 0xE000 | scanOf(key) : scanOf(key)), MAPVK_VSC_TO_VK_EX)); }
    // What a key going down or up is sent as: to the window in front, or as a message of the real game's window.
    INPUT inputOf(int key, bool goesDown) const {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wScan = scanOf(key);
        if (how == VIRTUAL) in.ki.wVk = virtualOf(key);
        in.ki.dwFlags = (how == SCAN ? KEYEVENTF_SCANCODE : 0) | (extendedOf(key) ? KEYEVENTF_EXTENDEDKEY : 0) | (goesDown ? 0 : KEYEVENTF_KEYUP);
        return in;
    }
    static LPARAM messageOf(int key, bool goesDown) {
        return LPARAM(1) | LPARAM(scanOf(key)) << 16 | (extendedOf(key) ? LPARAM(1) << 24 : 0) | (goesDown ? 0 : LPARAM(0xC0000000));
    }

private:
    const decltype(&GetForegroundWindow) foreground_;
    const decltype(&SendInput) sendInput_;
    HWND target = nullptr;
    How how = SCAN;
    std::atomic<bool> on{false};
    bool lost = false; // keys were held as the real game's window left the front: all are let go once it is back
    std::array<bool, 256> down{}; // (the keys the system took as down)
    mutable std::mutex setting;

    // (the caller holds `setting`) True when the keys held are those wanted.
    bool apply(const std::vector<int>& wanted) {
        std::array<bool, 256> next{};
        for (const int k : wanted)
            if (k > 0 && k < 256) next[size_t(k)] = true;
        if (how != WINDOW && foreground_() != target) {
            // (keys sent now would go to another program: none is pressed. What was held is let go all the same - a key
            //  left down stays down for the whole system until it is let go - and counts as lost for the window)
            if (!wanted.empty()) ++away;
            std::vector<INPUT> ups;
            std::vector<int> upKeys;
            for (int k = 1; k < 256; ++k)
                if (down[size_t(k)]) ups.push_back(inputOf(k, false)), upKeys.push_back(k), lost = true;
            const UINT taken = ups.empty() ? 0 : sendInput_(UINT(ups.size()), ups.data(), sizeof(INPUT));
            for (UINT i = 0; i < taken; ++i) down[size_t(upKeys[i])] = false;
            return wanted.empty() && taken == ups.size();
        }
        std::vector<std::pair<int, bool>> events; // (the keys let go first)
        size_t lostEvents = 0;                    // (the first events: all the real game's keys let go after `lost`)
        auto afterRelease = down;
        if (lost) {
            for (const int k : {keys.left, keys.right, keys.down, keys.turnRight, keys.turnLeft, keys.pause, GameKeys::MENU_OK, GameKeys::MENU_BACK}) {
                events.push_back({k, false});
                afterRelease[size_t(k)] = false;
            }
            lostEvents = events.size();
        }
        for (int k = 1; k < 256; ++k)
            if (afterRelease[size_t(k)] && !next[size_t(k)]) events.push_back({k, false});
        for (int k = 1; k < 256; ++k)
            if (next[size_t(k)] && !afterRelease[size_t(k)]) events.push_back({k, true});
        if (events.empty()) return true;
        // (each key is held as the system took it: one not taken stays as it was, and is sent again the next time)
        size_t taken = 0;
        bool all = true;
        if (how == WINDOW) {
            for (const auto& [k, goesDown] : events) {
                if (PostMessageW(target, goesDown ? WM_KEYDOWN : WM_KEYUP, virtualOf(k), messageOf(k, goesDown))) {
                    ++sent, down[size_t(k)] = goesDown;
                    if (all) ++taken;
                } else {
                    ++refused, all = false;
                }
            }
        } else {
            std::vector<INPUT> inputs;
            for (const auto& [k, goesDown] : events) inputs.push_back(inputOf(k, goesDown));
            taken = sendInput_(UINT(inputs.size()), inputs.data(), sizeof(INPUT));
            sent += long(taken), refused += long(inputs.size()) - long(taken);
            for (size_t i = 0; i < taken && i < events.size(); ++i) down[size_t(events[i].first)] = events[i].second;
            all = taken == inputs.size();
        }
        if (taken >= lostEvents) lost = false;
        return all && down == next;
    }
};

} // namespace play
