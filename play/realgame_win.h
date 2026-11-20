#pragma once
// The mode in which Juraic plays the second player of the real game running on the same PC (the Windows build
// only): the real game's offline versus of two players, a person on 1P. Never for a match over a network.
// This part holds what the screen of the mode sets: which of the two AIs plays and at which level. Each time the
// mode is opened both start from the AI of the 2P side in the settings (Settings::aiStyle, aiLevel); what is
// changed here is not written back to the settings.
// And the reading of the real game's screen: the pictures of the window the person chooses (play/realgame_capture_win.h)
// read with the table (play/realgame_read.h) and followed frame after frame with the rules of the game
// (play/realgame_follow.h). And the play: the AI plays 2P by the real game's memory (play/memory/primary_drive14_win.h)
// or by its pictures (play/realgame_drive.h), the buttons sent the way chosen (play/realgame_input_win.h: keys of the
// keyboard, or a pad of the driver ViGEmBus), made ready as reading begins and stopped when the mode is left.
// The parts for finding faults are built only with JURAIC_REALGAME_DEBUG=1 (CMake: JURAIC_REALGAME_DEBUG=ON): the
// record of each reading (play/realgame_log.h), a table read from LAYOUT_FILE, a picture of the real game kept when
// asked for (the key S), and the counters of the reading and the play on the screen of the mode.
#ifndef JURAIC_REALGAME_DEBUG
#define JURAIC_REALGAME_DEBUG 0
#endif
// (the driver's notes of why: in the record only, realgame_drive.h)
#ifndef JURAIC_REALGAME_REASONS
#define JURAIC_REALGAME_REASONS JURAIC_REALGAME_DEBUG
#endif
#include "../ai/level.h"
#include "level_ex.h"
#include "realgame_capture_win.h"
#include "realgame_drive.h"
#include "realgame_film.h"
#include "realgame_topfilm.h"
#include "realgame_follow.h"
#include "realgame_log.h"
#include "realgame_menu.h"
#include "realgame_pace.h"
#include "realgame_input_win.h"
#include "realgame_pad_win.h"
#if JURAIC_REALGAME_MEMORY
#include "memory/primary_drive14_win.h"
#include "memory/runtime14_win.h"
#include "memory/window_watch14.h"
#endif
#include <mmsystem.h>
#include <atomic>
#include <chrono>
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
    enum Row { ROW_STYLE, ROW_LEVEL,
#if JURAIC_REALGAME_MEMORY
        ROW_SOURCE,
#endif
        ROW_READ, ROW_PLAY, ROW_PRESS,
#if JURAIC_REALGAME_MEMORY
        ROW_WAY, ROW_LATE, ROW_LISTED, ROW_RANDOM, ROW_OPPONENT_INPUT,
#endif
        ROW_BACK, ROWS };
    int row = ROW_STYLE;
    int style = 0;                 // 0: Juraic 催促（小）, 1: Juraic 催促（極小） (as Settings::aiStyle)
    int level = juraic::ai::LAST_LEVEL; // stored normal level
    // The seat is marked "Ex" (as Settings::aiEx, play/level_ex.h): from Lv 36 up, with the real game read by its memory
    // first and the row of the screen on the cancels, the AI takes the ways over soonest; the same level without the
    // mark takes the straight ways there. The row of the level steps through both.
    bool ex = false;
    int shownLevel() const { return styleLevel(level, style); }
    // Settings laid over those of the AI that plays 2P (realgame::overlayParams: name=value,...); "": the AI's as a match
    // against it has them.
    std::string aiSettings;
    static constexpr int seat = 1; // the seat the AI plays
    const char* seatName() const { return "2P"; } // (as the record names it)

    RealGameMode() = default;
    RealGameMode(const RealGameMode&) = delete;
    RealGameMode& operator=(const RealGameMode&) = delete;
    // (before the members the threads of the capture and of the play use are gone)
    ~RealGameMode() {
        capture.stop();
        stopPlayer();
#if JURAIC_REALGAME_MEMORY
        primary.stop();
        memory.stop();
#endif
        pad.stop();
    }

    // What the play keeps (the reading followed, the driver, the pad, the press, the record) is used by the thread of the
    // play - each picture as it comes - and by this program's own (its screen and keys): each holds this while it does.
    mutable std::recursive_mutex state;
    using Hold = std::lock_guard<std::recursive_mutex>;
    // The time of QueryPerformanceCounter (the clock of the pictures) in 100 ns, and its frames of 1/60 s.
    static int64_t clockTime() {
        LARGE_INTEGER counter, frequency;
        QueryPerformanceCounter(&counter), QueryPerformanceFrequency(&frequency);
        return counter.QuadPart / frequency.QuadPart * realgame::TIME_SECOND + counter.QuadPart % frequency.QuadPart * realgame::TIME_SECOND / frequency.QuadPart;
    }
    static int64_t clockFrame() { return realgame::frameAt(clockTime()); }

    void open(int settingsStyle, int settingsLevel) {
        row = ROW_STYLE;
        autoRead = true, notTheGame = nullptr;
#if JURAIC_REALGAME_MEMORY
        listedPairs = false, realgame::primaryListedPairs14 = false;
        garbageRandom = false, realgame::primaryGarbageRandom14 = false;
        opponentInput = false, realgame::primaryInputSeat14 = -1;
        lateTrial = false, primary.lateTrial = false;
#endif
        stopPlaying(), stopPress();
        style = settingsStyle ? 1 : 0;
        level = settingsLevel < 1 ? 1 : settingsLevel > juraic::ai::LAST_LEVEL ? juraic::ai::LAST_LEVEL : settingsLevel;
        stopReading();
    }
    bool showRow(int i) const {
        (void)i;
        return true;
    }
    // The value of the row under the cursor changed by `by` steps; past either end it comes in again at the other
    // (as on the settings screen: Lv 36 → Lv 1). False: the row has no value.
    bool change(int by) {
        const Hold hold(state);
        if (row == ROW_STYLE) style = ((style + by) % 2 + 2) % 2;
        else if (row == ROW_LEVEL) {
            if (style) return false;
            // (with the cancels: Lv 36, then Lv 36Ex)
            const LevelPick to = levelStep(level, ex, by, cancelOffered());
            level = to.level, ex = to.ex;
        }
#if JURAIC_REALGAME_MEMORY
        else if(row==ROW_SOURCE){
            // (the screen, the memory under the pictures, the memory first without and with the cancels; changed while
            //  the AI plays, the AI stops and is begun again by whoever shows the screen: restartWanted)
            source=((source+by)%SOURCES+SOURCES)%SOURCES;
            if(playing){stopPlaying();restartWanted=true;}
            memoryEnabled=source==SOURCE_UNDER;memory.enabled(memoryEnabled);follow.clearMemoryFields();memoryUsed=false;
        }
#endif
        else if (row == ROW_PRESS) pressButton = ((pressButton + by) % PRESS_BUTTONS + PRESS_BUTTONS) % PRESS_BUTTONS;
#if JURAIC_REALGAME_MEMORY
        else if (row == ROW_WAY) changeWay(((pad.way() + by) % RealPad::WAYS + RealPad::WAYS) % RealPad::WAYS);
        else if (row == ROW_LATE) {
            lateTrial = !lateTrial, primary.lateTrial = lateTrial;
            record.event(tracker.frame, lateTrial ? "the trial of buttons set late begins" : "the trial of buttons set late ends");
        }
        else if (row == ROW_LISTED) {
            // (read from the next sample on: PrimarySeat14::listed)
            listedPairs = !listedPairs;
            realgame::primaryListedPairs14 = listedPairs;
            record.event(tracker.frame, listedPairs ? "the pairs beyond the window are read from now on" : "the pairs beyond the window are not read any more");
        }
        else if (row == ROW_RANDOM) {
            garbageRandom = !garbageRandom, realgame::primaryGarbageRandom14 = garbageRandom;
            record.event(tracker.frame, garbageRandom ? "the garbage random states are read from now on" : "the garbage random states are not read any more");
        }
        else if (row == ROW_OPPONENT_INPUT) {
            opponentInput = !opponentInput;
            realgame::primaryInputSeat14 = opponentInput ? 1 - seat : -1;
            record.event(tracker.frame, opponentInput ? "the other seat input is read from now on" : "the other seat input is not read any more");
        }
#endif
        else return false;
        return true;
    }

    // ---- the play ----
    // The frames from the picture followed as a button of the pad is set to the picture of the real game that shows it
    // taken, which the AI begins with (Driver::lag): 4 in the records of the real game. The way of the two turns it begins with
    // (Driver::turnsSwapped): the other way round, B sent for a turn to the right - so the real game turned its pair in
    // all six of its records through the virtual pad. The driver then takes the lag and the way of the turns
    // that fit the pictures (Driver::fit).
    int lag = 4;
    bool turnsSwapped = true;
    // The buttons go to the real game by the virtual pad or by keys with no driver (play/realgame_input_win.h: the row
    // ROW_WAY); the mode presses one pad whichever way it is.
    RealPad pad;
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
    // Keyboard menu confirmation is shared and cannot be sent to just the second seat.
    bool automaticMenuInput() const { return !pad.byKeys(); }
    bool padOnReading = true; // the pad is plugged in as reading begins (a test of the reading alone plugs in none)
    const realgame::MenuLayout menus = realgame::measuredMenus();
    realgame::Menu menu = realgame::Menu::None; // as last read
    realgame::MenuPresser presser;
    std::wstring padError; // (the pad could not be plugged in as reading began)

    // The driver of the pictures takes `agent` and is made ready to play (the caller holds `state`).
    void beginDriver(std::unique_ptr<juraic::ai::Agent> agent) {
        driver = realgame::Driver{};
        told = {};
        driver.me = seat;
        realgame::Driver::prepare(agent.get());
        driver.lag = lag, driver.turnsSwapped = turnsSwapped, driver.agent = std::move(agent);
        driver.notes = &notes;
        juraic::inputdiag::trace.seat=driver.me;
#if JURAIC_REALGAME_MEMORY
        const int64_t memoryOn=memoryEnabled;
#else
        const int64_t memoryOn=0;
#endif
        juraic::inputdiag::trace.emit(juraic::inputdiag::CONFIG,{2,driver.me,memoryOn,1,automaticMenuInput()});
        // (no picture for long, or a menu read: the pad holds nothing, and the driver's engine takes so - holdNothing)
        driver.gate = [this](int64_t now, uint8_t b) {
            ++diagnosticGateCalls;int reasons=0;
#if JURAIC_REALGAME_MEMORY
            reasons=driver.inputGateReasons(now,memoryEnabled,memoryMotionBound,memoryMotionAdvancing,memoryMotionPair,memoryMotionFresh,holdNothing);
#endif
            if(holdNothing)reasons|=4;
            const auto output=reasons?uint8_t(0):b;
#if JURAIC_REALGAME_MEMORY
            juraic::inputdiag::trace.emit(juraic::inputdiag::GATE,{tracker.rounds,now,driver.begunPair(),int64_t(memoryMotionPair),b,output,reasons,
                memoryEnabled,memoryMotionBound,memoryMotionAdvancing,memoryMotionRunning,driver.lag,driver.turnsSwapped,
                diagnosticNativeTick,diagnosticPoseFrame,memoryMotionFresh,driver.imageGateReady(now)});
#else
            juraic::inputdiag::trace.emit(juraic::inputdiag::GATE,{tracker.rounds,now,driver.begunPair(),0,b,output,reasons,
                0,0,0,0,driver.lag,driver.turnsSwapped,-1,-1,0,0});
#endif
            return output;
        };
    }
    // The AI `agent` begins to play 2P. False, with the reason in `error`, when it cannot. `spare`: with the play by the
    // memory first, an agent like it for the driver of the pictures, which takes over while the memory cannot be read
    // (watchPrimary); none: nothing takes over.
    bool startPlaying(std::unique_ptr<juraic::ai::Agent> agent, std::wstring& error, std::unique_ptr<juraic::ai::Agent> spare = nullptr) {
        const Hold hold(state);
        if (reading != Reading::On) return error = L"先に「読み取り開始」で実機のウィンドウを選択してください", false;
        if (!pad.running() && !pad.start(error)) return false;
#if JURAIC_REALGAME_MEMORY
        if (sourceFirst()) {
            // Memory first (play/memory/primary_drive14_win.h): the AI plays by the real game's memory on a thread of
            // its own, which sets the pad; the pictures go on being read (the notice above the fields is theirs, and
            // the screen of the mode shows them), and their driver presses nothing.
            realgame::PrimaryDrive14::Setup setup;
            setup.program = windowProgram(capture.window()), setup.me = seat, setup.delay = primaryDelay;
            setup.cancel = cancels(), setup.turnsSwapped = turnsSwapped, setup.listedPairs = listedPairs;
            setup.garbageRandom = garbageRandom;
            setup.opponentInput = opponentInput;
            // (with the record: also the bytes of the field and player objects for the first minutes, for finding where the
            //  score and the waiting garbage are kept - PrimaryDrive14::Setup::rawBytes)
            // (a folder of its own for each time the AI begins - the row of the screen that chooses how the field is read begins it
            //  anew -, so that one does not write over the other: primary, primary-2, ...)
            if (record.isOpen()) {
                if (primaryRecord != record.folder()) primaryRecord = record.folder(), primaryStarts = 0;
                ++primaryStarts;
                setup.record = record.folder() / (primaryStarts > 1 ? L"primary-" + std::to_wstring(primaryStarts) : std::wstring(L"primary"));
                setup.rawBytes = uint64_t(48) << 20;
            }
            if (setup.program.empty()) return error = L"実機の実行ファイルの場所を取得できません", false;
            primary.press = [this](uint8_t b) {
                // (a press asked for by hand comes first: the menus; and while the driver of the pictures has taken
                //  over, the pad is its own)
                if (!handPress.load() && !primaryFallback.load()) pad.press(b);
            };
            spareAgent = std::move(spare), primaryFallback = false, primaryLost = 0;
            primary.notice = [this] { return std::array<int, 2>{noticeShown[0].load(), noticeShown[1].load()}; };
            driver = realgame::Driver{};
            driver.me = seat; // (the seat the pictures are watched for: watchPrimary)
            primary.start(setup, std::move(agent));
            playing = primaryPlaying = true;
            record.event(tracker.frame, std::string("the AI plays ") + seatName() + " by the memory first: " + (setup.cancel ? "the ways over soonest (cancels)" : "the straight ways") + ", delay " +
                                            std::to_string(primaryDelay) + ", the two turns sent " + (turnsSwapped ? "swapped" : "as they are") + ", the buttons by " +
                                            RealPad::WAY_RECORD_NAMES[pad.way()] + (lateTrial ? ", set late for a trial" : "") +
                                            (listedPairs ? ", the pairs beyond the window read" : ""));
            return true;
        }
#endif
        if(!juraic::inputdiag::trace.active())return error=L"診断ログが開始されていないため、画面認識の方式では AI を開始できません",false;
        beginDriver(std::move(agent));
        playing = true;
        record.event(tracker.frame, std::string("the AI plays ") + seatName() + ": lag " + std::to_string(lag) + " to begin with, the two turns sent " + (turnsSwapped ? "swapped" : "as they are"));
        return true;
    }
    // The AI plays by the real game's memory first (not by its pictures).
    bool memoryFirst() const {
#if JURAIC_REALGAME_MEMORY
        return primaryPlaying;
#else
        return false;
#endif
    }
    // The AI changed on the screen while it plays (its kind, its level): `agent` plays 2P from the next pair on.
    void replaceAgent(std::unique_ptr<juraic::ai::Agent> agent, std::unique_ptr<juraic::ai::Agent> spare = nullptr) {
        const Hold hold(state);
        if (!playing) return;
#if JURAIC_REALGAME_MEMORY
        if (primaryPlaying) {
            primary.replaceAgent(std::move(agent), cancels());
            // (the spare one: the driver of the pictures has it while it has taken over)
            if (spare && primaryFallback.load()) realgame::Driver::prepare(spare.get()), driver.replaceAgent(std::move(spare));
            else if (spare) spareAgent = std::move(spare);
        } else
#endif
        driver.replaceAgent(std::move(agent));
        record.event(tracker.frame, std::string("the AI from the next pair on: ") + (style ? "x3q" : "x3") + ", level " + std::to_string(shownLevel()) + (cancels() ? "Ex (the ways over soonest)" : ""));
    }
    void stopPlaying() {
        const Hold hold(state);
        if (playing) {
            juraic::inputdiag::trace.emit(juraic::inputdiag::MARKER,{1,tracker.frame,driver.begunPair()});
            record.event(tracker.frame, "the AI stops");
        }
        playing = false;
        // (the agents are let go on threads of their own, as Driver::replaceAgent does: one may wait for a search it has
        //  begun, and the thread that draws the screen is not to wait for that)
        const auto letGo = [](std::unique_ptr<juraic::ai::Agent>& agent) {
            if (agent) std::thread([a = std::move(agent)]() mutable { a.reset(); }).detach();
        };
#if JURAIC_REALGAME_MEMORY
        // (its thread sets the pad without holding `state`: it can be waited for here)
        if (primaryPlaying) primary.stop(), primaryPlaying = false;
        primaryFallback = false, primaryLost = 0;
        letGo(spareAgent);
#endif
        driver.stop();
        letGo(driver.agent);
        pad.press(0);
    }
    // A press asked for (the pad plugged in if it is not). False, with the reason in `error`, when the pad cannot be.
    bool pressLater(std::wstring& error) {
        const Hold hold(state);
        if (!pad.running() && !pad.start(error)) return false;
        pressLeft = PRESS_WAIT + PRESS_HOLD, pressBits = PRESS_BUTTON_BITS[pressButton];
#if JURAIC_REALGAME_MEMORY
        handPress = true;
#endif
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
#if JURAIC_REALGAME_MEMORY
        handPress = false;
#endif
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
    // of the play after the pictures that came, or in a frame none came in (playLoop: `why` as RECORD_SENT has it), while
    // reading; otherwise each frame of this program.
    void drive(int64_t now, uint8_t why = 0) {
        const Hold hold(state);
        const auto diagnosticBegin=juraic::inputdiag::clock();const auto costBefore=juraic::inputdiag::trace.cost100ns();
        diagnosticGateCalls=0;int64_t diagnosticDriverTime=0;
#if JURAIC_REALGAME_MEMORY
        refreshMemory();
#endif
        const auto diagnosticRefreshEnd=juraic::inputdiag::clock();
        realgame::PadRecord held{now, 0, 0, 0};
        realgame::SentRecord sent{0, 0, why};
        if (pressLeft >= 0) {
            --pressLeft;
            pad.hold(pressLeft >= 0 && pressLeft < PRESS_HOLD ? pressBits : USHORT(0));
            held.by = 2;
#if JURAIC_REALGAME_MEMORY
            handPress = pressLeft >= 0;
        } else if (playing && primaryPlaying && !primaryFallback.load()) {
            // (memory first: the pad is set by the thread of PrimaryDrive14, as each frame of the game is read)
            handPress = false;
            held.buttons = primary.status().held, held.by = 1;
            watchPrimary();
#endif
        } else if (playing) {
#if JURAIC_REALGAME_MEMORY
            handPress = false;
            if (primaryPlaying && primaryBack()) return drive(now, why); // (the memory is read again: its play goes on)
#endif
            const int corrected = driver.corrected;
            holdNothing = why == 3 || why == 4;
            const auto diagnosticDriverBegin=juraic::inputdiag::clock();
            held.buttons = driver.tick(follow, now);
            diagnosticDriverTime=juraic::inputdiag::clock()-diagnosticDriverBegin;
            sent.driven = clockTime();
            pad.press(held.buttons);
            sent.set = clockTime();
            held.by = 1;
            for (const auto& [at, line] : notes) record.event(at, line);
            notes.clear();
            if (driver.corrected != corrected) record.ask("put-right");
            if (DEBUG_PARTS) topFilm.follow(driver.begunPair(), driver.beenAloft(), driver.corrected != corrected, now);
            if (driver.toldPair() >= 0 && (driver.toldPair() != told.pair || juraic::placementId(driver.toldPlacement()) != told.placement))
                told = {driver.toldPair(), uint8_t(juraic::placementId(driver.toldPlacement()))}, record.tell(told);
        }
        if (record.isOpen() && pad.running()) {
            held.pad = pad.heldButtons();
            record.pad(held);
            if (held.by == 1) record.sent(sent);
        }
        record.flush();
#if JURAIC_REALGAME_MEMORY
        const int64_t memoryOn=memoryEnabled,memoryTaken=memoryUsed;
#else
        const int64_t memoryOn=0,memoryTaken=0;
#endif
        juraic::inputdiag::trace.emit(juraic::inputdiag::DRIVE,{tracker.rounds,now,tracker.frame,driver.begunPair(),diagnosticGateCalls,
            memoryOn,memoryTaken,diagnosticDriverTime/10,(diagnosticRefreshEnd-diagnosticBegin)/10,held.buttons,held.pad,why,
            diagnosticBegin,sent.driven,sent.set,juraic::inputdiag::clock(),int64_t((juraic::inputdiag::trace.cost100ns()-costBefore)/10),
            int64_t(juraic::inputdiag::trace.dropped())});
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
    // The way the buttons go to the real game as it was last chosen (RealPad::WAY_RECORD_NAMES), in the same folder.
    static constexpr const wchar_t* INPUT_FILE = L"realgame_input.txt";
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
    int64_t diagnosticGateCalls=0;
#if JURAIC_REALGAME_MEMORY
    realgame::MemoryRuntime14 memory;
    bool memoryEnabled=false,memoryUsed=false;
    // How the real game is read for the play (the row ROW_SOURCE): by its pictures; by its pictures with the memory
    // under them (memoryEnabled); by its memory first, the AI's ways straight or over soonest with the cancels.
    // (the play begins by the memory first, without the cancels)
    enum { SOURCE_SCREEN, SOURCE_FIRST, SOURCE_UNDER, SOURCE_FIRST_CANCEL, SOURCES };
    bool sourceFirst() const { return source == SOURCE_FIRST || source == SOURCE_FIRST_CANCEL; }
    // The row of the screen is on the cancels: the levels from Lv 36 up are offered twice (play/level_ex.h).
    bool cancelOffered() const { return source == SOURCE_FIRST_CANCEL; }
    // The AI takes the ways over soonest: the cancels are offered and the level chosen is an Ex.
    bool cancels() const { return !style && levelEx(level, ex, cancelOffered()); }
    int source = SOURCE_FIRST;
    bool restartWanted = false;   // the source was changed while the AI played: it is to be begun again
    // The trial of buttons set late is on (the row ROW_LATE; PrimaryDrive14::lateTrial): with the play by the memory
    // first, newly pressed buttons are set some milliseconds after their frame on purpose, to measure the way they go.
    bool lateTrial = false;
    // The pairs beyond the window are read from the game's own list and shown to the AI (the row ROW_LISTED;
    // PrimaryDrive14::Setup::listedPairs). Off unless the row is set.
    bool listedPairs = false;
    bool garbageRandom = false;
    bool opponentInput = false;
    // The buttons go another way from now on (the row ROW_WAY). The AI stops if it plays, and whoever shows the screen
    // begins it again (restartWanted); what was held is let go and a pad plugged in is pulled out. The real game is to
    // be told anew which of its devices plays the seat (in its menus).
    void changeWay(int to) {
        const Hold hold(state);
        const bool played = playing;
        if (playing) stopPlaying();
        stopPress();
        pad.setWay(to);
        padError.clear();
        if (reading == Reading::On && padOnReading && !pad.start(padError)) padError = L"入力方式を開始できません: " + padError;
        presser = realgame::MenuPresser{};
        if (played) restartWanted = true;
        record.event(tracker.frame, std::string("the buttons go by ") + RealPad::WAY_RECORD_NAMES[pad.way()]);
    }
    realgame::PrimaryDrive14 primary;
    bool primaryPlaying = false;  // the AI plays by the memory first
    // The play by the memory first has no seats to read (a round whose objects are not found, another version of the
    // program) while the pictures show 2P with a pair in its hands: after PRIMARY_LOST such frames the driver of the
    // pictures takes over with the spare agent (primaryFallback), and gives the pad back once both seats are read in a
    // round again. (In four records of the real game two rounds were not bound at all, and 2P pressed nothing in them.)
    // The driver of the pictures plays only with the diagnostic log open (startPlaying); without it nothing takes over,
    // and the screen of the mode tells that the memory is not read (memoryText).
    static constexpr int PRIMARY_LOST = 45;
    std::atomic<bool> primaryFallback{false};
    int primaryLost = 0;
    long primaryFallbacks = 0; // times the driver of the pictures took over
    std::unique_ptr<juraic::ai::Agent> spareAgent;
    void watchPrimary() {
        const auto s = primary.status();
        if (s.cpuSeat || s.reader.state == "bound") primaryLost = 0;
        else if (tracker.side[driver.me].inHand) ++primaryLost;
        if (primaryLost < PRIMARY_LOST || !spareAgent || !juraic::inputdiag::trace.active()) return;
        beginDriver(std::move(spareAgent));
        primaryFallback = true, primaryLost = 0, ++primaryFallbacks;
        pad.press(0);
        record.event(tracker.frame, "the memory is not read (" + s.reader.state + ") and " + seatName() + " has a pair in its hands: the driver of the pictures takes over");
    }
    // The driver of the pictures has taken over: true once the memory is read again in a round - the driver stops, its
    // agent is the spare one again, and the pad is the memory's play's.
    bool primaryBack() {
        if (!primaryFallback.load()) return false;
        const auto s = primary.status();
        if (!s.cpuSeat && (s.reader.state != "bound" || !s.round)) return false;
        driver.stop();
        if (s.cpuSeat && !handPress.load()) pad.press(0);
        spareAgent = driver.takeAgent();
        driver = realgame::Driver{};
        driver.me = seat;
        primaryFallback = false, primaryLost = 0;
        primary.resend();
        record.event(tracker.frame, "the memory is read again: the play by the memory first goes on");
        return true;
    }
    int primaryDelay = 0;         // frames a button takes to reach the game beyond its next frame (PrimaryDrive14::Setup)
    int primaryStarts = 0;        // times the AI began to play by the memory first with the record of `primaryRecord`
    std::filesystem::path primaryRecord;
    std::atomic<bool> handPress{false};          // a press asked for by hand is on its way: the play sets nothing
    std::array<std::atomic<int>, 2> noticeShown{}; // the notice above each field as the pictures have it
    int64_t diagnosticNativeTick=-1,diagnosticPoseFrame=-1;
    bool memoryMotionBound=false,memoryMotionRunning=false;
    bool memoryMotionAdvancing=false;
    uint32_t memoryMotionPair=0;
    bool memoryMotionFresh=false;
    uint64_t memoryMotionNoteAt=0;
    int memoryMotionRound=-1;
    uint64_t memorySequence=0;
    bool userSelectedWindow=false,rememberedWindowProgram=false,windowWatchNoted=false;
    std::array<std::tuple<uint64_t,int64_t,int,int,uint64_t,uint32_t>,2> nextConsumerDiagnostic{};
    // (for the screen of the mode, with the parts for finding faults: how the play by the memory first goes)
    std::wstring primaryLine() const {
        const auto s=primary.status();
        return L"メモリ読み取り: 取得 "+std::to_wstring(s.reader.samples)+L" フレーム（欠落 "+std::to_wstring(s.reader.stepsMore)+L"、3 ms 超の待ち "+std::to_wstring(s.reader.lateWaits)+
            L"）　操作 "+std::to_wstring(s.frames)+L" フレーム（補完 "+std::to_wstring(s.filled)+L"、予測との不一致 "+std::to_wstring(s.corrections)+L"、再捕捉 "+std::to_wstring(s.recovered)+
            L"）　入力遅延 "+std::to_wstring(s.delay)+(s.measured>=0?L"（実測 "+std::to_wstring(s.measured)+L"）":L"")+L"　最大処理時間 "+std::to_wstring(s.slowestUs/1000)+L" ms"+
            L"　思考 "+std::to_wstring(s.thinking[0])+L" 回（時間切れで打ち切り "+std::to_wstring(s.thinking[1])+L"、探索幅を縮小 "+std::to_wstring(s.thinking[2])+L"、2 組先までの計画 "+std::to_wstring(s.thinking[3])+L"）";
    }
    std::wstring memoryText() const {
        if(sourceFirst()){
            if (!primaryPlaying) return L"メモリ読み取り: 「2P の操作を開始」で開始します（読み取り専用）";
            if(primaryFallback.load())return L"メモリ読み取り: メモリを読み取れないため、画面認識で操作中（読み取れ次第、戻ります）";
            const auto s=primary.status();
            const std::wstring base=s.reader.state=="bound"?(s.round?L"メモリ読み取り: 両プレイヤーを読み取って操作中":L"メモリ読み取り: 両プレイヤーを読み取り中（対戦開始待ち）"):
                s.reader.state=="no_process"?L"メモリ読み取り: 実機のプロセスが見つかりません":
                s.reader.state=="no_access"?L"メモリ読み取り: 実機のプロセスを開けません（実機が管理者権限で動いている場合は、本ソフトも管理者権限で起動してください）":
                primaryLost>=PRIMARY_LOST?L"メモリ読み取り: 対戦中ですが、メモリから両プレイヤーのデータを読み取れません（AI は操作していません）":L"メモリ読み取り: 両プレイヤーのデータを検索中";
            return base+(DEBUG_PARTS?L"　組数 "+std::to_wstring(s.pairs)+L"　欠落 "+std::to_wstring(s.skipped)+L"　補正 "+std::to_wstring(s.repaired)+L"　画面認識への切り替え "+std::to_wstring(primaryFallbacks)+L" 回":L"");
        }
        if(!memoryEnabled)return L"盤面: 画面認識";
        if(memoryMotionBound)return memoryMotionRunning?(follow.memoryNextAvailable(driver.me)?L"メモリ: 操作中の組と NEXT を照合して操作中":L"メモリ: 操作中の組を追跡して操作中（NEXT は画面認識）"):L"メモリ: 操作待機中";
        if(memoryUsed)return L"盤面: メモリから両プレイヤーの 14 段を取得。操作対象を照合中（NEXT は画面認識）";
        const auto s=memory.status();
        if(s.state==realgame::Follow14State::NeedScan)return L"盤面: メモリアドレスを検索中。現在は画面認識";
        if(s.state==realgame::Follow14State::NoProcess || s.state==realgame::Follow14State::Unsupported)return L"盤面: メモリ未接続または未対応。現在は画面認識";
        if(s.state==realgame::Follow14State::RoundUnknown)return L"メモリ待機: 対戦開始時からの確認が必要です。現在は画面認識";
        return L"盤面: メモリを照合中。現在は画面認識";
    }
#else
    bool cancelOffered() const { return false; }
    bool cancels() const { return false; }
#endif
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
    realgame::WindowFilm film; // (of the two windows, play/realgame_film.h: with DEBUG_PARTS, in the record's folder)
    realgame::TopFilm topFilm; // (of the top of the field of 2P, play/realgame_topfilm.h: with DEBUG_PARTS, there too)

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
        told = {};
        // (with DEBUG_PARTS, PlayPace::sendAfter from JURAIC_SEND_AFTER_US - microseconds after a picture was shown -, for
        //  trying it on the real game)
        pace.sendAfter = realgame::PlayPace::SEND_AFTER;
        wchar_t after[32];
        if (DEBUG_PARTS && GetEnvironmentVariableW(L"JURAIC_SEND_AFTER_US", after, 32) > 0) pace.sendAfter = _wtoll(after) * 10;
        // (the pad of 2P is there from the menus on: the real game gives it the row of 2P)
        padError.clear();
        pad.aim(w, windowProgram(w)); // (keys go to this window, and are those of this program's settings)
        if (padOnReading && !pad.running() && !pad.start(padError)) padError = L"入力方式を開始できません: " + padError;
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
        if (DEBUG_PARTS && !record.open(folder / L"realgame" / stamp, header)) recordError = L"記録を作成できませんでした（" + (folder / L"realgame").wstring() + L"）";
        if(record.isOpen() && !juraic::inputdiag::trace.open(folder / L"realgame" / stamp / L"input-diagnostics.tsv",&RealGameMode::clockTime))
            recordError=L"診断ログを作成できません（画面認識の方式では AI を開始できません）";
        if (DEBUG_PARTS && record.isOpen()) film.open(record.folder() / L"film"), topFilm.open(record.folder() / L"film");
        notes.clear();
#if JURAIC_REALGAME_MEMORY
        DWORD memoryPid=0;GetWindowThreadProcessId(w,&memoryPid);
        memory.enabled(memoryEnabled);memory.start(windowProgram(w),memoryPid);memorySequence=0;memoryUsed=false;
#endif
        const bool started = capture.start(w, [this, table](const realgame::Picture& p, int64_t time) { took(p, time, table, menus); }, error);
        reading = started ? Reading::On : Reading::Off;
        if (started) startPlayer();
        else {
#if JURAIC_REALGAME_MEMORY
            memory.stop();
#endif
            juraic::inputdiag::trace.close();
            if (DEBUG_PARTS) film.close(), topFilm.close();
            record.close();
        }
        return started;
    }
    // (the threads of the capture and of the play end first, this holding nothing they wait for)
    void stopReading() {
        capture.stop();
        stopPlayer();
        const Hold hold(state);
        if (playing) stopPlaying();
#if JURAIC_REALGAME_MEMORY
        memory.stop();follow.clearMemoryFields();memoryUsed=false;
#endif
        juraic::inputdiag::trace.close();
        if (DEBUG_PARTS) film.close(), topFilm.close();
        record.close(); // (after the capture: its thread offers the record pictures)
        reading = Reading::Off;
        std::lock_guard<std::mutex> handed(lock);
        taken.clear();
        saveWanted = savedReady = false;
        width = height = 0, pictures = 0, firstFrame = -1, framesWithout = 0;
        pace.reset(), nextDue = -1, menuFor = 0, holdNothing = false;
    }

    // Every frame of this program: how long since a picture came.
    void pump() {
        const int64_t last = lastPicture.load();
        framesWithout = last < 0 ? framesWithout + 1 : int(clockFrame() - last);
#if JURAIC_REALGAME_MEMORY
        const Hold hold(state);
        if(framesWithout>15){memoryUsed=false;follow.clearMemoryFields();}
#endif
    }

    // The next picture kept as it is, to be saved (savedPicture). Nothing without DEBUG_PARTS.
    void keepNext() {
        const Hold held(state);
        juraic::inputdiag::trace.emit(juraic::inputdiag::MARKER,{0,tracker.frame,driver.begunPair()});
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
#if JURAIC_REALGAME_MEMORY
    void refreshMemory() {
        // One transition record per seat/status/pair/generation. The frame and
        // packet identity in the event are evidence, not a source of control.
        std::array<int,2> nextStatus{2,2};
        std::array<std::optional<realgame::PublishedNextVerified14>,2> nextEvidence;
        std::array<uint64_t,2> nextNow{};
        uint32_t nextPid=0;
        juraic::inputdiag::ScopeExit nextLog{[&]{
            auto& trace=juraic::inputdiag::trace;if(!trace.active())return;
            for(int seat=0;seat<2;++seat){const auto& n=nextEvidence[size_t(seat)];
                if(nextStatus[size_t(seat)]!=7)follow.noteNextUnavailable(seat);
                const auto gen=n?n->generation:0;auto key=std::tuple{trace.session(),tracker.roundStart,tracker.side[seat].pairs,nextStatus[size_t(seat)],gen,nextPid};
                if(key==nextConsumerDiagnostic[size_t(seat)])continue;nextConsumerDiagnostic[size_t(seat)]=key;
                trace.emit(juraic::inputdiag::NEXT_CONSUMER,{seat,tracker.roundStart,tracker.side[seat].pairs,tracker.frame,nextStatus[size_t(seat)],
                    int64_t(gen),n?int64_t(n->pair):-1,n?n->frame:-1,n?int64_t(n->observedMs):0,int64_t(nextNow[size_t(seat)]),n?int64_t(n->tick):0,nextPid});
            }
        }};
        memoryMotionFresh=false;
        int64_t present=0,usable=0,accepted=0,generation=0,observed=0,sampleFrame=-1,readBegin=0,readEnd=0;
        juraic::inputdiag::ScopeExit sourceLog{[&]{juraic::inputdiag::trace.emit(juraic::inputdiag::SOURCE,{
            tracker.rounds,tracker.frame,memoryEnabled,memoryUsed,memoryMotionBound,memoryMotionAdvancing,memoryMotionRunning,
            int64_t(memoryMotionPair),present,usable,accepted,generation,observed,sampleFrame,diagnosticPoseFrame,
            diagnosticNativeTick,readBegin,readEnd});}};
        diagnosticNativeTick=-1;diagnosticPoseFrame=-1;
        follow.clearMemoryFields();memoryUsed=false;
        if(!memoryEnabled || reading!=Reading::On){nextStatus={1,1};memoryMotionBound=memoryMotionRunning=memoryMotionAdvancing=false;driver.useMemoryPose({},false);return;}
        if(memoryMotionRound!=tracker.rounds){memoryMotionRound=tracker.rounds;memoryMotionBound=memoryMotionRunning=false;driver.useMemoryPose({},false);}
        DWORD pid=0;GetWindowThreadProcessId(capture.window(),&pid);nextPid=pid;
        const auto now=realgame::MemoryRuntime14::clockMs();
        const auto motion=memory.motion();
        if(motion){present=1;generation=int64_t(motion->generation);observed=int64_t(motion->observedMs);sampleFrame=motion->frame;
            const auto seat=size_t(driver.me);diagnosticNativeTick=motion->nativeTick[seat];diagnosticPoseFrame=motion->poseFrame[seat];
            readBegin=motion->readBegin100ns[seat];readEnd=motion->readEnd100ns[seat];}
        if(!motion){memoryMotionBound=memoryMotionRunning=memoryMotionAdvancing=false;driver.useMemoryPose({},false);}
        else if(realgame::usableMemoryMotion14(*motion,tracker,now,pid)){usable=1;memoryMotionFresh=true;
            memoryMotionBound=motion->bound[size_t(driver.me)];memoryMotionRunning=motion->running[size_t(driver.me)];
            memoryMotionAdvancing=motion->advancing[size_t(driver.me)];
            memoryMotionPair=motion->pairs[size_t(driver.me)];
            const auto pose=realgame::memoryControlPose14(*motion,tracker,driver.me,now,pid);accepted=bool(pose);
            driver.useMemoryPose(pose,memoryMotionBound,memoryMotionRunning,memoryMotionPair);
        }else{
            // Once a bound stream expires, release until revalidated. Never
            // silently reuse a last good pose as the current control state.
            memoryMotionRunning=memoryMotionAdvancing=false;driver.useMemoryPose({},memoryMotionBound);
        }
        if(now>=memoryMotionNoteAt){memoryMotionNoteAt=now+1000;
            record.event(clockFrame(),"native input feedback: seat="+std::to_string(driver.me+1)+", bound="+std::to_string(memoryMotionBound)+
                ", advancing="+std::to_string(memoryMotionAdvancing)+", operating="+std::to_string(memoryMotionRunning)+
                ", pair="+std::to_string(memoryMotionPair)+", used="+std::to_string(driver.memoryPoseSamples)+
                ", corrected="+std::to_string(driver.memoryPoseCorrections)+", lag="+std::to_string(driver.lag));
        }
        const auto sample=memory.sample();if(!sample)return;
        if(juraic::inputdiag::trace.active())nextEvidence=sample->next;
        if(!realgame::usableMemorySample14(*sample,tracker,realgame::MemoryRuntime14::clockMs(),pid)){nextStatus={3,3};return;}
        if(!follow.useMemoryFields(sample->value.fields)){nextStatus={8,8};return;}
        memoryUsed=true;
        nextStatus={motion?5:4,motion?5:4};
        if(motion)for(int seat=0;seat<2;++seat)if(sample->next[size_t(seat)]){
            const auto checkedAt=realgame::MemoryRuntime14::clockMs();nextNow[size_t(seat)]=checkedAt;
            nextStatus[size_t(seat)]=follow.useMemoryNext(*sample->next[size_t(seat)],*motion,seat,checkedAt,pid)?7:6;
        }
        if(memorySequence!=sample->value.sequence){
            memorySequence=sample->value.sequence;
            record.event(tracker.frame,"read-only memory accepted: pid "+std::to_string(pid)+", sequence "+std::to_string(memorySequence)+
                ", source frame "+std::to_string(sample->frame)+", both fields (14 rows):\n"+
                realgame::memoryFieldText14(sample->value.fields[0])+realgame::memoryFieldText14(sample->value.fields[1]));
        }
    }
#endif
    struct Taken {
        realgame::Reading reading;
        int64_t frame;
        int width, height;
        realgame::Menu menu;
        int64_t shown, came, read; // the time it was shown (as the capture gives it), came in, was read (clockTime)
    };
    // Which pictures the play follows and when it drives the pad (play/realgame_pace.h), and the time the pad is to be
    // driven by when no picture comes (for the wait of the thread of the play, which takes `lock` and not `state`).
    realgame::PlayPace pace;
    std::atomic<int64_t> nextDue{-1};
    // The pad holds nothing (Driver::gate): no picture for long, or a menu read MENU_HOLD readings in a row (the AI's buttons
    // are not for the menus: a pair the engine was playing as the round was left, pressed through them).
    static constexpr int MENU_HOLD = 6;
    bool holdNothing = false;
    int menuFor = 0;
    realgame::TellRecord told; // (the placement last told Follower, as the record has it)
    // The thread of the play: woken by each picture taken (`arrived`, with `lock`), it follows what was read and then
    // drives the pad - in the rhythm of the pictures, not of this program's frames (which come late or twice at times) -,
    // and in a frame no picture came in all the same (PlayPace: the taps planned for it are let go in it).
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
            // (a picture shown when the last one followed was, or before, is not followed; one shown later of the same
            //  frame to the nearest is of the frame after: PlayPace)
            const int64_t frame = pace.take(t.frame, t.shown, t.read);
            if (record.isOpen()) record.time({t.shown, t.came, t.read, uint8_t(frame >= 0)});
            if (frame < 0) {
                record.event(t.frame, "a picture of frame " + std::to_string(t.frame) + " not followed: the last one followed was of frame " + std::to_string(pace.last()));
                continue;
            }
            if (firstFrame < 0) firstFrame = frame;
            if (record.isOpen()) {
                record.reading(t.reading, frame, t.width, t.height);
                if (t.width != width || t.height != height) record.event(frame, "picture size " + std::to_string(t.width) + " x " + std::to_string(t.height));
            }
            const bool noting = record.isOpen();
            const Counts before = noting ? counts() : Counts{};
            follow.step(t.reading, frame);
#if JURAIC_REALGAME_MEMORY
            memory.submit(t.reading,frame);
            for (size_t s = 0; s < 2; ++s) noticeShown[s] = follow.notice[s];
#endif
            width = t.width, height = t.height;
            ++pictures;
            if (noting) noteStep(before, frame);
            if (DEBUG_PARTS) film.note(tracker, frame);
            if (t.menu != menu) record.event(frame, std::string("menu: ") + realgame::menuName(t.menu));
            menu = t.menu;
            menuFor = menu == realgame::Menu::None ? 0 : menuFor + 1;
        }
        // (in the menus the pad of 2P presses A where 2P has to; a press asked for by hand comes first)
        if (!now.empty() && automaticMenuInput() && pad.running() && pressLeft < 0 && presser.step(menu)) {
            pressLeft = PRESS_HOLD, pressBits = XUSB_GAMEPAD_A;
            record.event(now.back().frame, std::string("the pad of 2P presses A: ") + realgame::menuName(menu));
        }
    }
    // Woken by the pictures, or by the time the pad is to be driven when none comes (PlayPace::due): the waits of the
    // system's clock are made a millisecond long while it runs (timeBeginPeriod; otherwise up to 16 ms, a frame).
    void playLoop() {
        // (above the threads of this program's drawing and of the AI's search: on a machine under load the pad was set a
        //  frame late in 7% of the frames of a record of the real game, twice in one frame in as many - the pictures
        //  themselves came in each frame)
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        timeBeginPeriod(1);
        int64_t drivenLast = -1; // the frame the pad was last driven as
        for (;;) {
            std::deque<Taken> now;
            {
                std::unique_lock<std::mutex> hold(lock);
                const auto ready = [this] { return playerStop || !taken.empty(); };
                for (;;) {
                    if (ready()) break;
                    const int64_t due = nextDue.load();
                    if (due < 0) {
                        arrived.wait(hold, ready);
                        break;
                    }
                    const int64_t left = due - clockTime();
                    if (left <= 0) break;
                    arrived.wait_for(hold, std::chrono::microseconds(left / 10 + 1), ready);
                }
                if (playerStop) break;
                now.swap(taken);
            }
            uint8_t why = 1;
            int64_t setAt = 0;
            {
                const Hold hold(state);
                followTaken(now);
                const int64_t t = clockTime();
                if (now.empty()) {
                    if (pace.due() < 0 || t < pace.due()) continue;
                    pace.filledAt(t);
                    why = pace.stalled(t) ? 3 : 2;
                } else {
                    lastPicture = realgame::frameAt(t);
                }
                if (menuFor >= MENU_HOLD) why = 4;
                // (the picture before this one left out, the frame the game reads next not driven yet: driven at once,
                //  PlayPace::behind)
                if (why == 1 && pace.behind(t, drivenLast)) drivenLast = pace.frameSet(t), drive(drivenLast, 2);
                setAt = why == 1 ? pace.sendAt(t) : t;
            }
            // (PlayPace::sendAfter: the buttons are set no sooner than setAt - a millisecond's sleeps, then the rest waited
            //  out, the state not held meanwhile: the thread that draws reads it each frame)
            while (clockTime() < setAt - realgame::TIME_SECOND / 1000) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            while (clockTime() < setAt) std::this_thread::yield();
            const Hold hold(state);
            // (driven as the frame of the game that reads the buttons set now, PlayPace::frameSet; never a frame before the
            //  last one driven: a picture that came in after the frames with none were driven)
            const int64_t at = clockTime(), as = pace.frameSet(at);
            drivenLast = std::max(drivenLast, as >= 0 ? as : realgame::frameAt(at));
            drive(drivenLast, why);
            nextDue = pace.due();
        }
        timeEndPeriod(1);
    }
    std::vector<std::pair<int64_t, std::string>> notes; // of the driver, until they go into the record

    // What the record is told after each reading: what it changed of these.
    struct Counts {
        int rounds = 0, doubts = 0, misses = 0, held = 0, followed[2]{}, notice[2]{}, holds[2]{};
        juraic::GameField reckoned[2]; // the pieces at rest as the game made them, before the reading
        bool known[2]{};
    };
    Counts counts() const {
        Counts c;
        c.rounds = tracker.rounds, c.doubts = tracker.doubts, c.misses = follow.misses, c.held = follow.held;
        for (int s = 0; s < 2; ++s)
            c.followed[s] = follow.model[s].followed, c.notice[s] = follow.notice[s], c.reckoned[s] = follow.model[s].core.field, c.known[s] = follow.model[s].known,
            c.holds[s] = follow.holding[s].begun;
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
            if constexpr (DEBUG_PARTS) {
                // (why a notice is held back: what the most it can show was made of)
                const realgame::Follower::Hold& h = follow.holding[s];
                if (h.begun != b.holds[s])
                    record.event(at, std::string(who) + " notice " + std::to_string(h.read) + " held back for now: the most it can show " + std::to_string(h.taken + h.budget + h.sends + h.slack) +
                                         " (taken " + std::to_string(h.taken) + ", still to come " + std::to_string(h.budget) + ", the pair lying sends " + std::to_string(h.sends) +
                                         ", slack " + std::to_string(h.slack) + ")");
            }
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
        const int64_t came = clockTime();
        Taken t{realgame::read(p, table), realgame::frameShown(time), p.width, p.height, realgame::readMenu(p, menuTable), time, came, 0};
        t.read = clockTime();
        record.offer(p, t.frame);
        if (DEBUG_PARTS) film.take(p, t.frame, t.reading, table), topFilm.take(p, t.frame, t.reading, table);
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
