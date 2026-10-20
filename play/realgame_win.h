#pragma once
// The mode in which Juraic plays the second player of the real game running on the same PC (the Windows build
// only): the real game's offline versus of two players, a person on 1P. Never for a match over a network.
// This part holds what the screen of the mode sets: which of the two AIs plays and at which level. Each time the
// mode is opened both start from the AI of the 2P side in the settings (Settings::aiStyle, aiLevel); what is
// changed here is not written back to the settings.
// And the reading of the real game's screen: the pictures of the window the person chooses (play/realgame_capture_win.h)
// read with the table (play/realgame_read.h) and followed frame after frame with the rules of the game
// (play/realgame_follow.h). And the play: the AI plays 2P (play/realgame_drive.h) through a pad of the driver ViGEmBus
// (play/realgame_pad_win.h), plugged in when the AI first plays or the person first presses it by hand to pick it for 2P
// in the real game's menus, and kept plugged in until the mode is left.
// The parts for finding faults are built only with JURAIC_REALGAME_DEBUG=1 (CMake: JURAIC_REALGAME_DEBUG=ON): the
// record of each reading (play/realgame_log.h), a table read from LAYOUT_FILE, a picture of the real game kept when
// asked for (the key S), and the counters of the reading and the play on the screen of the mode.
#ifndef JURAIC_REALGAME_DEBUG
#define JURAIC_REALGAME_DEBUG 0
#endif
#include "../ai/level.h"
#include "realgame_capture_win.h"
#include "realgame_drive.h"
#include "realgame_follow.h"
#include "realgame_log.h"
#include "realgame_menu.h"
#include "realgame_pad_win.h"
#include <atomic>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <mutex>
#include <thread>
#include <string>
#include <utility>
#include <vector>

namespace play {

// The program (its image file) of the process of a window; empty when it cannot be had.
inline std::wstring windowProgram(HWND w) {
    DWORD id = 0;
    GetWindowThreadProcessId(w, &id);
    const HANDLE process = id ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id) : nullptr;
    if (!process) return {};
    wchar_t path[MAX_PATH * 2];
    DWORD size = DWORD(std::size(path));
    const bool got = QueryFullProcessImageNameW(process, 0, path, &size) != 0;
    CloseHandle(process);
    return got ? std::wstring(path, size) : std::wstring();
}
// A window that may be the real game's: shown, of the top, no bar of tools, at least 320 x 180 inside.
inline bool gameLikeWindow(HWND w, RECT& inside) {
    return w && w == GetAncestor(w, GA_ROOT) && w != GetShellWindow() && IsWindowVisible(w) && !IsIconic(w) && !(GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) &&
           GetClientRect(w, &inside) && inside.right >= 320 && inside.bottom >= 180;
}
// The window of the program `program` that may be the real game's, the largest inside (nullptr: none).
inline HWND findProgramWindow(const std::wstring& program) {
    struct Search {
        const std::wstring* program;
        HWND found = nullptr;
        long area = 0;
    } s{&program};
    EnumWindows(
        [](HWND w, LPARAM data) -> BOOL {
            auto& s = *reinterpret_cast<Search*>(data);
            RECT inside{};
            if (!gameLikeWindow(w, inside) || _wcsicmp(windowProgram(w).c_str(), s.program->c_str()) != 0) return TRUE;
            const long area = long(inside.right) * long(inside.bottom);
            if (area > s.area) s.found = w, s.area = area;
            return TRUE;
        },
        LPARAM(&s));
    return s.found;
}

struct RealGameMode {
    enum Row { ROW_STYLE, ROW_LEVEL, ROW_HIGH, ROW_READ, ROW_PLAY, ROW_PRESS, ROW_BACK, ROWS };
    int row = ROW_STYLE;
    int style = 0;                 // 0: Juraic 催促（小）, 1: Juraic 催促（極小） (as Settings::aiStyle)
    int level = juraic::ai::LEVELS; // 1 .. LEVELS (as Settings::aiLevel)
    bool keepLow = true;           // the pairs kept below the 13th row (Driver::keepLow); so each time the mode is opened

    RealGameMode() = default;
    RealGameMode(const RealGameMode&) = delete;
    RealGameMode& operator=(const RealGameMode&) = delete;
    // (before the members the threads of the capture and of the play use are gone)
    ~RealGameMode() {
        capture.stop();
        stopPlayer();
        pad.stop();
    }

    // What the play keeps (the reading followed, the driver, the pad, the press, the record) is used by the thread of the
    // play - each picture as it comes - and by this program's own (its screen and keys): each holds this while it does.
    mutable std::recursive_mutex state;
    using Hold = std::lock_guard<std::recursive_mutex>;
    // Frames of 1/60 s of QueryPerformanceCounter (the clock of the pictures).
    static int64_t clockFrame() {
        LARGE_INTEGER counter, frequency;
        QueryPerformanceCounter(&counter), QueryPerformanceFrequency(&frequency);
        return (counter.QuadPart / frequency.QuadPart) * 60 + (counter.QuadPart % frequency.QuadPart) * 60 / frequency.QuadPart;
    }

    void open(int settingsStyle, int settingsLevel) {
        row = ROW_STYLE;
        autoRead = true, notTheGame = nullptr;
        stopPlaying(), stopPress();
        style = settingsStyle ? 1 : 0;
        level = settingsLevel < 1 ? 1 : settingsLevel > juraic::ai::LEVELS ? juraic::ai::LEVELS : settingsLevel;
        keepLow = true;
        stopReading();
    }
    // The value of the row under the cursor changed by `by` steps; past either end it comes in again at the other
    // (as on the settings screen: Lv 36 → Lv 1). False: the row has no value.
    bool change(int by) {
        const Hold hold(state);
        if (row == ROW_STYLE) style = ((style + by) % 2 + 2) % 2;
        else if (row == ROW_LEVEL) level = ((level - 1 + by) % juraic::ai::LEVELS + juraic::ai::LEVELS) % juraic::ai::LEVELS + 1;
        else if (row == ROW_HIGH) keepLow = by % 2 ? !keepLow : keepLow;
        else if (row == ROW_PRESS) pressButton = ((pressButton + by) % PRESS_BUTTONS + PRESS_BUTTONS) % PRESS_BUTTONS;
        else return false;
        return true;
    }

    // ---- the play ----
    // The frames from a button of the pad to the picture of the real game that shows it taken, which the AI begins with
    // (Driver::lag): measured on the real game, 6. The way of the two turns it begins with
    // (Driver::turnsSwapped): the other way round, B sent for a turn to the right - so the real game turned its pair in
    // all six of its records through the virtual pad. The driver then takes the lag and the way of the turns
    // that fit the pictures (Driver::fit).
    int lag = 6;
    bool turnsSwapped = true;
    VirtualPad pad;
    realgame::Driver driver;
    bool playing = false; // the AI plays 2P

    // A press of a button of the pad, asked for to pick it for 2P in the real game's menus (there a pad becomes 2P by a
    // press of its own): PRESS_WAIT frames after it is asked, the button is held PRESS_HOLD frames. In the wait the
    // person brings the real game's window to the front: the program that runs the real game takes in the pad as its own and gives
    // the real game its buttons only while that window is in front. No key of this program presses the pad.
    static constexpr int PRESS_WAIT = 5 * 60, PRESS_HOLD = 6, PRESS_BUTTONS = 3;
    static constexpr USHORT PRESS_BUTTON_BITS[PRESS_BUTTONS] = {XUSB_GAMEPAD_A, XUSB_GAMEPAD_B, XUSB_GAMEPAD_START};
    static constexpr const wchar_t* PRESS_BUTTON_NAMES[PRESS_BUTTONS] = {L"A", L"B", L"Start"};
    static constexpr const char* PRESS_RECORD_NAMES[PRESS_BUTTONS] = {"A", "B", "Start"};
    int pressButton = 0; // (of PRESS_BUTTON_BITS)
    int pressLeft = -1;  // frames until the button is let go; above PRESS_HOLD: still waiting. -1: no press asked
    USHORT pressBits = 0; // the button of the press asked for

    // The real game's menus read from its screen (play/realgame_menu.h): the pad of 2P presses A where 2P has to pick
    // its row, its character and its way of play, so that the person need not.
    bool autoMenus = true;
    bool padOnReading = true; // the pad is plugged in as reading begins (a test of the reading alone plugs in none)
    const realgame::MenuLayout menus = realgame::measuredMenus();
    realgame::Menu menu = realgame::Menu::None; // as last read
    realgame::MenuPresser presser;
    std::wstring padError; // (the pad could not be plugged in as reading began)

    // The AI `agent` begins to play 2P. False, with the reason in `error`, when it cannot.
    bool startPlaying(std::unique_ptr<juraic::ai::Agent> agent, std::wstring& error) {
        const Hold hold(state);
        if (reading != Reading::On) return error = L"先に「読み始める」で実機の窓を選んでください", false;
        if (!pad.running() && !pad.start(error)) return false;
        driver = realgame::Driver{};
        driver.keepLow = keepLow;
        realgame::Driver::prepare(agent.get(), keepLow);
        driver.lag = lag, driver.turnsSwapped = turnsSwapped, driver.agent = std::move(agent);
        driver.notes = &notes;
        playing = true;
        record.event(tracker.frame, "the AI plays 2P: lag " + std::to_string(lag) + " to begin with, the two turns sent " + (turnsSwapped ? "swapped" : "as they are") +
                                        (keepLow ? ", the pairs kept below the 13th row" : ", the 13th row used"));
        return true;
    }
    // The AI changed on the screen while it plays (its kind, its level, the 13th row): `agent` plays 2P from the next
    // pair on.
    void replaceAgent(std::unique_ptr<juraic::ai::Agent> agent) {
        const Hold hold(state);
        if (!playing) return;
        driver.keepLow = keepLow;
        driver.replaceAgent(std::move(agent));
        record.event(tracker.frame, std::string("the AI from the next pair on: ") + (style ? "x3q" : "x3") + ", level " + std::to_string(level) +
                                        (keepLow ? ", the pairs kept below the 13th row" : ", the 13th row used"));
    }
    void stopPlaying() {
        const Hold hold(state);
        if (playing) record.event(tracker.frame, "the AI stops");
        playing = false;
        driver.stop();
        driver.agent.reset();
        pad.press(0);
    }
    // A press asked for (the pad plugged in if it is not). False, with the reason in `error`, when the pad cannot be.
    bool pressLater(std::wstring& error) {
        const Hold hold(state);
        if (!pad.running() && !pad.start(error)) return false;
        pressLeft = PRESS_WAIT + PRESS_HOLD, pressBits = PRESS_BUTTON_BITS[pressButton];
        record.event(tracker.frame, std::string("a press of ") + PRESS_RECORD_NAMES[pressButton] + " asked for");
        return true;
    }
    bool pressing() const {
        const Hold hold(state);
        return pressLeft >= 0;
    }
    // The seconds of the wait before the press (0 while the button is held).
    int pressSeconds() const {
        const Hold hold(state);
        return pressLeft > PRESS_HOLD ? (pressLeft - PRESS_HOLD + 59) / 60 : 0;
    }
    void stopPress() {
        const Hold hold(state);
        if (pressLeft >= 0) pad.hold(0);
        pressLeft = -1;
    }
    // The mode is left: everything stops and the pad is pulled out. Until then the pad stays plugged in once it is
    // (stopPlaying and stopPress only let its buttons go): a pad pulled out and plugged in again is a new pad to the
    // real game, which no longer has it as the pad of 2P.
    void leave() {
        stopReading();
        const Hold hold(state);
        stopPlaying(), stopPress();
        pad.stop();
    }
    // The press asked for, or while the AI plays the buttons for the frame `now` of the clock (clockFrame): on the thread
    // of the play after each picture while reading, otherwise each frame of this program.
    void drive(int64_t now) {
        const Hold hold(state);
        realgame::PadRecord held{now, 0, 0, 0};
        if (pressLeft >= 0) {
            --pressLeft;
            pad.hold(pressLeft >= 0 && pressLeft < PRESS_HOLD ? pressBits : USHORT(0));
            held.by = 2;
        } else if (playing) {
            const int corrected = driver.corrected;
            held.buttons = driver.tick(follow, now);
            pad.press(held.buttons);
            held.by = 1;
            for (const auto& [at, line] : notes) record.event(at, line);
            notes.clear();
            if (driver.corrected != corrected) record.ask("put-right");
        }
        if (record.isOpen() && pad.running()) {
            held.pad = pad.heldButtons();
            record.pad(held);
        }
        record.flush();
    }

    // ---- the reading of the real game's screen ----
    // Off; Choosing: the person is to bring the real game's window to the front (the window that comes to the front
    // is the one read); On: its pictures are read.
    enum class Reading { Off, Choosing, On };
    Reading reading = Reading::Off;
    // The real game's window is found by this program as the screen of the mode opens (autoRead): the window of the
    // program read last time (WINDOW_FILE in the folder of one's own files), otherwise the window the person brings to
    // the front, of the shape of the real game's (16:9). A window read is the real game's once a menu of it or a round
    // has been read (confirmed: its program is kept in WINDOW_FILE); one in which neither is read for NOT_THE_GAME frames
    // is let go, and another is looked for.
    static constexpr const wchar_t* WINDOW_FILE = L"realgame_window.txt";
    static constexpr int NOT_THE_GAME = 20 * 60;
    bool autoRead = true;
    bool confirmed = false;
    int64_t readSince = 0; // (the frame of the clock reading began in)
    HWND notTheGame = nullptr;
    // The parts for finding faults (JURAIC_REALGAME_DEBUG) are built in.
    static constexpr bool DEBUG_PARTS = JURAIC_REALGAME_DEBUG != 0;
    // The table: with DEBUG_PARTS the file LAYOUT_FILE in the folder of one's own files when there is one (read when
    // reading begins), otherwise the one built in (realgame::measuredLayout).
    static constexpr const wchar_t* LAYOUT_FILE = L"realgame_layout.txt";
    realgame::Layout layout = realgame::measuredLayout();
    bool layoutOwn = false;
    std::wstring layoutError; // (the file is there and a line of it is not understood: the table built in is used)
    realgame::Follower follow;
    const realgame::Tracker& tracker = follow.tracker;
    WindowCapture capture;
    int width = 0, height = 0;  // of the last picture
    int64_t pictures = 0;       // pictures read since reading began
    int64_t firstFrame = -1;    // the frame of the clock of the first of them
    int framesWithout = 0;      // frames of the clock since a picture last came

    // The record of the reading (play/realgame_log.h), with DEBUG_PARTS: in the folder of one's own files,
    // realgame\<date and time>. Without them it is never opened, and what is offered to it is dropped.
    realgame::Record record;
    std::wstring recordError; // (it could not be made: the reading goes on without it)

    // Begins to read the window `w` with the table of `folder` (or the one built in); `about` begins the record.
    bool startReading(HWND w, const std::filesystem::path& folder, const std::string& about, std::wstring& error) {
        stopReading();
        const Hold hold(state);
        layout = realgame::measuredLayout(), layoutOwn = false, layoutError.clear();
        std::error_code ignored;
        const std::filesystem::path file = folder / LAYOUT_FILE;
        if (DEBUG_PARTS && std::filesystem::exists(file, ignored)) {
            std::string why;
            realgame::Layout own;
            if (realgame::loadLayout(file, own, &why)) layout = own, layoutOwn = true;
            else layoutError = std::wstring(why.begin(), why.end());
        }
        follow.reset();
        follow.tracker.stillFrames = layout.stillFrames;
        // (the pad of 2P is there from the menus on: the real game gives it the row of 2P)
        padError.clear();
        if (padOnReading && !pad.running() && !pad.start(padError)) padError = L"仮想のパッドを差せません: " + padError;
        menu = realgame::Menu::None, presser = realgame::MenuPresser{};
        const realgame::Layout table = layout; // (the capture's own copy: it reads on its thread)
        recordError.clear();
        const std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &now);
        char stamp[32];
        std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &local);
        std::string header = about;
        if (!header.empty() && header.back() != '\n') header += '\n';
        header += std::string("table: ") + (layoutOwn ? "the file realgame_layout.txt" : "built in") + ", " + std::to_string(layout.baseWidth) + " x " +
                  std::to_string(layout.baseHeight) + "\n";
        header += "lag " + std::to_string(lag) + " to begin with, the press " + PRESS_RECORD_NAMES[pressButton] + "\n";
        header += "each line: the frame of the clock (1/60 s), then what happened\n";
        if (DEBUG_PARTS && !record.open(folder / L"realgame" / stamp, header)) recordError = L"記録を作れませんでした（" + (folder / L"realgame").wstring() + L"）";
        notes.clear();
        const bool started = capture.start(w, [this, table](const realgame::Picture& p, int64_t time) { took(p, time, table, menus); }, error);
        reading = started ? Reading::On : Reading::Off;
        if (started) startPlayer();
        else record.close();
        return started;
    }
    // (the threads of the capture and of the play end first, this holding nothing they wait for)
    void stopReading() {
        capture.stop();
        stopPlayer();
        const Hold hold(state);
        if (playing) stopPlaying();
        record.close(); // (after the capture: its thread offers the record pictures)
        reading = Reading::Off;
        std::lock_guard<std::mutex> handed(lock);
        taken.clear();
        saveWanted = savedReady = false;
        width = height = 0, pictures = 0, firstFrame = -1, framesWithout = 0;
    }

    // Every frame of this program: how long since a picture came.
    void pump() {
        const int64_t last = lastPicture.load();
        framesWithout = last < 0 ? framesWithout + 1 : int(clockFrame() - last);
    }

    // The next picture kept as it is, to be saved (savedPicture). Nothing without DEBUG_PARTS.
    void keepNext() {
        if (!DEBUG_PARTS) return;
        std::lock_guard<std::mutex> hold(lock);
        saveWanted = true, savedReady = false;
    }
    // The picture kept, once it has come.
    bool savedPicture(std::vector<uint8_t>& pixels, int& w, int& h) {
        std::lock_guard<std::mutex> hold(lock);
        if (!savedReady) return false;
        pixels.swap(saved), w = savedWidth, h = savedHeight;
        savedReady = false;
        return true;
    }
    bool savePending() {
        std::lock_guard<std::mutex> hold(lock);
        return saveWanted;
    }

private:
    struct Taken {
        realgame::Reading reading;
        int64_t frame;
        int width, height;
        realgame::Menu menu;
    };
    // The thread of the play: woken by each picture taken (`arrived`, with `lock`), it follows what was read and then
    // drives the pad - in the rhythm of the pictures, not of this program's frames (which come late or twice at times).
    std::thread player;
    std::condition_variable arrived;
    bool playerStop = false;
    std::atomic<int64_t> lastPicture{-1}; // the frame of the clock in which the play last took pictures

    void startPlayer() {
        stopPlayer();
        {
            std::lock_guard<std::mutex> hold(lock);
            playerStop = false;
        }
        lastPicture = -1;
        player = std::thread([this] { playLoop(); });
    }
    void stopPlayer() {
        {
            std::lock_guard<std::mutex> hold(lock);
            playerStop = true;
        }
        arrived.notify_all();
        if (player.joinable()) player.join();
    }
    // On the thread of the play: what was read, in its order.
    void followTaken(const std::deque<Taken>& now) {
        for (const Taken& t : now) {
            if (firstFrame < 0) firstFrame = t.frame;
            if (record.isOpen()) {
                record.reading(t.reading, t.frame, t.width, t.height);
                if (t.width != width || t.height != height) record.event(t.frame, "picture size " + std::to_string(t.width) + " x " + std::to_string(t.height));
            }
            const bool noting = record.isOpen();
            const Counts before = noting ? counts() : Counts{};
            follow.step(t.reading, t.frame);
            width = t.width, height = t.height;
            ++pictures;
            if (noting) noteStep(before, t.frame);
            if (t.menu != menu) record.event(t.frame, std::string("menu: ") + realgame::menuName(t.menu));
            menu = t.menu;
        }
        // (in the menus the pad of 2P presses A where 2P has to; a press asked for by hand comes first)
        if (!now.empty() && autoMenus && pad.running() && pressLeft < 0 && presser.step(menu)) {
            pressLeft = PRESS_HOLD, pressBits = XUSB_GAMEPAD_A;
            record.event(now.back().frame, std::string("the pad of 2P presses A: ") + realgame::menuName(menu));
        }
    }
    void playLoop() {
        // (above the threads of this program's drawing and of the AI's search: on a machine under load the pad was set a
        //  frame late in 7% of the frames of a record of the real game, twice in one frame in as many - the pictures
        //  themselves came in each frame)
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        for (;;) {
            std::deque<Taken> now;
            {
                std::unique_lock<std::mutex> hold(lock);
                arrived.wait(hold, [this] { return playerStop || !taken.empty(); });
                if (playerStop) return;
                now.swap(taken);
            }
            const Hold hold(state);
            followTaken(now);
            const int64_t at = clockFrame();
            lastPicture = at;
            drive(at);
        }
    }
    std::vector<std::pair<int64_t, std::string>> notes; // of the driver, until they go into the record

    // What the record is told after each reading: what it changed of these.
    struct Counts {
        int rounds = 0, doubts = 0, misses = 0, held = 0, followed[2]{}, notice[2]{};
        juraic::GameField reckoned[2]; // the pieces at rest as the game made them, before the reading
        bool known[2]{};
    };
    Counts counts() const {
        Counts c;
        c.rounds = tracker.rounds, c.doubts = tracker.doubts, c.misses = follow.misses, c.held = follow.held;
        for (int s = 0; s < 2; ++s)
            c.followed[s] = follow.model[s].followed, c.notice[s] = follow.notice[s], c.reckoned[s] = follow.model[s].core.field, c.known[s] = follow.model[s].known;
        return c;
    }
    static std::string pairText(juraic::Pair p) { return std::to_string(p.axis) + std::to_string(p.child); }
    void noteStep(const Counts& b, int64_t at) {
        static const char* turns[] = {"up", "right", "down", "left"};
        if (tracker.rounds != b.rounds) record.event(at, "a round begins (both fields empty)");
        if (tracker.doubts != b.doubts) record.event(at, "a reading did not fit what was read before (" + std::to_string(tracker.doubts) + " so far)"), record.ask("doubt");
        for (int s = 0; s < 2; ++s) {
            const char* who = s ? "2P" : "1P";
            const realgame::SideTrack& t = tracker.side[s];
            if (tracker.spawned[s]) {
                std::string line = std::string(who) + " pair " + std::to_string(t.pairs) + " appears" + (tracker.late[s] ? " (seen late)" : "") + ", colours " + pairText(t.colors) + ", next";
                for (int n = 0; n < 2; ++n) line += t.nextRead[n] ? " " + pairText(t.next[n]) : std::string(" ?");
                record.event(at, line);
            }
            if (follow.model[s].followed != b.followed[s]) {
                const realgame::SideModel& m = follow.model[s];
                record.event(at, std::string(who) + " pair " + std::to_string(m.followed + 1) + " placed: column " + std::to_string(m.placed.x + 1) + ", child " + turns[m.placed.dir] +
                                     (m.chains ? ", a chain of " + std::to_string(m.chains) : ""));
            }
            if (follow.notice[s] != b.notice[s]) record.event(at, std::string(who) + " notice " + std::to_string(follow.notice[s]));
            // (a check fails as a new pair appears: the pieces it appeared over against the game's)
            if (follow.misses != b.misses && tracker.spawned[s] && b.known[s])
                record.event(at, std::string(who) + " pieces at rest not as the game made them; the game:\n" + realgame::fieldText(b.reckoned[s]) + "    the screen:\n" + realgame::fieldText(t.under)),
                    record.ask("miss");
        }
        if (follow.held != b.held) record.event(at, "a notice held back (more than the chains can have sent)");
    }
    std::mutex lock; // what the capture's thread hands over
    std::deque<Taken> taken;
    bool saveWanted = false, savedReady = false;
    std::vector<uint8_t> saved;
    int savedWidth = 0, savedHeight = 0;

    // On the capture's thread: a picture read; the frame from the time it was shown (frames of 1/60 s).
    void took(const realgame::Picture& p, int64_t time, const realgame::Layout& table, const realgame::MenuLayout& menuTable) {
        Taken t{realgame::read(p, table), (time * 60 + 5000000) / 10000000, p.width, p.height, realgame::readMenu(p, menuTable)};
        record.offer(p, t.frame);
        std::lock_guard<std::mutex> hold(lock);
        if (taken.size() >= 600) taken.pop_front(); // (no one takes them: the oldest go)
        taken.push_back(std::move(t));
        arrived.notify_one();
        if (saveWanted) {
            saved.resize(size_t(p.width) * size_t(p.height) * 4);
            for (int y = 0; y < p.height; ++y)
                std::copy_n(p.pixels + size_t(y) * size_t(p.stride), size_t(p.width) * 4, saved.data() + size_t(y) * size_t(p.width) * 4);
            savedWidth = p.width, savedHeight = p.height;
            saveWanted = false, savedReady = true;
        }
    }
};

} // namespace play
