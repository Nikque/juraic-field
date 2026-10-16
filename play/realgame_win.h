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
#include "../ai/level.h"
#include "realgame_capture_win.h"
#include "realgame_drive.h"
#include "realgame_follow.h"
#include "realgame_pad_win.h"
#include <deque>
#include <mutex>

namespace play {

struct RealGameMode {
    enum Row { ROW_STYLE, ROW_LEVEL, ROW_READ, ROW_PLAY, ROW_LAG, ROW_TURN, ROW_PRESS, ROW_BACK, ROWS };
    int row = ROW_STYLE;
    int style = 0;                 // 0: Juraic 催促（小）, 1: Juraic 催促（極小） (as Settings::aiStyle)
    int level = juraic::ai::LEVELS; // 1 .. LEVELS (as Settings::aiLevel)

    RealGameMode() = default;
    RealGameMode(const RealGameMode&) = delete;
    RealGameMode& operator=(const RealGameMode&) = delete;
    ~RealGameMode() { capture.stop(), pad.stop(); } // (before the members the capture's thread hands its pictures to are gone)

    void open(int settingsStyle, int settingsLevel) {
        row = ROW_STYLE;
        stopPlaying(), stopPress();
        style = settingsStyle ? 1 : 0;
        level = settingsLevel < 1 ? 1 : settingsLevel > juraic::ai::LEVELS ? juraic::ai::LEVELS : settingsLevel;
        stopReading();
    }
    // The value of the row under the cursor changed by `by` steps; past either end it comes in again at the other
    // (as on the settings screen: Lv 36 → Lv 1). False: the row has no value.
    bool change(int by) {
        if (row == ROW_STYLE) style = ((style + by) % 2 + 2) % 2;
        else if (row == ROW_LEVEL) level = ((level - 1 + by) % juraic::ai::LEVELS + juraic::ai::LEVELS) % juraic::ai::LEVELS + 1;
        else if (row == ROW_LAG) lag = ((lag + by) % (MAX_LAG + 1) + MAX_LAG + 1) % (MAX_LAG + 1), driver.lag = lag;
        else if (row == ROW_TURN) pad.aTurnsRight = !pad.aTurnsRight;
        else if (row == ROW_PRESS) pressButton = ((pressButton + by) % PRESS_BUTTONS + PRESS_BUTTONS) % PRESS_BUTTONS;
        else return false;
        return true;
    }

    // ---- the play ----
    // The frames from a button of the pad to the frame of the real game that takes it (Driver::lag), and which button
    // turns which way: as the person sets them here (measured: the screen of the real game shows a key 2 to 4 frames
    // after it is pressed; this is the part before the frame of the game).
    static constexpr int MAX_LAG = 6;
    int lag = 2;
    VirtualPad pad;
    realgame::Driver driver;
    bool playing = false; // the AI plays 2P

    // A press of a button of the pad, asked for to pick it for 2P in the real game's menus (there a pad becomes 2P by a
    // press of its own): PRESS_WAIT frames after it is asked, the button is held PRESS_HOLD frames. In the wait the
    // person brings the real game's window to the front: Steam, which takes in the pad as a controller of its own, gives
    // the real game its buttons only while that window is in front. No key of this program presses the pad.
    static constexpr int PRESS_WAIT = 5 * 60, PRESS_HOLD = 6, PRESS_BUTTONS = 3;
    static constexpr USHORT PRESS_BUTTON_BITS[PRESS_BUTTONS] = {XUSB_GAMEPAD_A, XUSB_GAMEPAD_B, XUSB_GAMEPAD_START};
    static constexpr const wchar_t* PRESS_BUTTON_NAMES[PRESS_BUTTONS] = {L"A", L"B", L"Start"};
    int pressButton = 0; // (of PRESS_BUTTON_BITS)
    int pressLeft = -1;  // frames until the button is let go; above PRESS_HOLD: still waiting. -1: no press asked

    // The AI `agent` begins to play 2P. False, with the reason in `error`, when it cannot.
    bool startPlaying(std::unique_ptr<juraic::ai::Agent> agent, std::wstring& error) {
        if (reading != Reading::On) return error = L"先に「読み始める」で実機の窓を選んでください", false;
        if (!pad.running() && !pad.start(error)) return false;
        driver = realgame::Driver{};
        driver.lag = lag, driver.agent = std::move(agent);
        playing = true;
        return true;
    }
    void stopPlaying() {
        playing = false;
        driver.stop();
        driver.agent.reset();
        pad.press(0);
    }
    // A press asked for (the pad plugged in if it is not). False, with the reason in `error`, when the pad cannot be.
    bool pressLater(std::wstring& error) {
        if (!pad.running() && !pad.start(error)) return false;
        pressLeft = PRESS_WAIT + PRESS_HOLD;
        return true;
    }
    bool pressing() const { return pressLeft >= 0; }
    // The seconds of the wait before the press (0 while the button is held).
    int pressSeconds() const { return pressLeft > PRESS_HOLD ? (pressLeft - PRESS_HOLD + 59) / 60 : 0; }
    void stopPress() {
        if (pressLeft >= 0) pad.hold(0);
        pressLeft = -1;
    }
    // The mode is left: everything stops and the pad is pulled out. Until then the pad stays plugged in once it is
    // (stopPlaying and stopPress only let its buttons go): a pad pulled out and plugged in again is a new pad to the
    // real game, which no longer has it as the pad of 2P.
    void leave() {
        stopReading(), stopPlaying(), stopPress();
        pad.stop();
    }
    // Every frame of the game: the press asked for, or while the AI plays the buttons for the frame `now` of the clock
    // (frames of 1/60 s of QueryPerformanceCounter, as the times of the pictures).
    void drive(int64_t now) {
        if (pressLeft >= 0) {
            --pressLeft;
            pad.hold(pressLeft >= 0 && pressLeft < PRESS_HOLD ? PRESS_BUTTON_BITS[pressButton] : USHORT(0));
            return;
        }
        if (playing) pad.press(driver.tick(follow, now));
    }

    // ---- the reading of the real game's screen ----
    // Off; Choosing: the person is to bring the real game's window to the front (the window that comes to the front
    // is the one read); On: its pictures are read.
    enum class Reading { Off, Choosing, On };
    Reading reading = Reading::Off;
    static constexpr int CHOOSE_FRAMES = 10 * 60;
    int chooseLeft = 0;
    // The table: the file LAYOUT_FILE in the folder of one's own files when there is one (read when reading begins),
    // otherwise the one built in (realgame::measuredLayout).
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
    int framesWithout = 0;      // frames of the game since a picture last came

    // Begins to read the window `w` with the table of `folder` (or the one built in).
    bool startReading(HWND w, const std::filesystem::path& folder, std::wstring& error) {
        stopReading();
        layout = realgame::measuredLayout(), layoutOwn = false, layoutError.clear();
        std::error_code ignored;
        const std::filesystem::path file = folder / LAYOUT_FILE;
        if (std::filesystem::exists(file, ignored)) {
            std::string why;
            realgame::Layout own;
            if (realgame::loadLayout(file, own, &why)) layout = own, layoutOwn = true;
            else layoutError = std::wstring(why.begin(), why.end());
        }
        follow.reset();
        follow.tracker.stillFrames = layout.stillFrames;
        const realgame::Layout table = layout; // (the capture's own copy: it reads on its thread)
        const bool started = capture.start(w, [this, table](const realgame::Picture& p, int64_t time) { took(p, time, table); }, error);
        reading = started ? Reading::On : Reading::Off;
        return started;
    }
    void stopReading() {
        if (playing) stopPlaying();
        capture.stop();
        reading = Reading::Off;
        std::lock_guard<std::mutex> hold(lock);
        taken.clear();
        saveWanted = savedReady = false;
        width = height = 0, pictures = 0, firstFrame = -1, framesWithout = 0;
    }

    // Every frame of the game: what was read since is followed, in its order.
    void pump() {
        std::deque<Taken> now;
        {
            std::lock_guard<std::mutex> hold(lock);
            now.swap(taken);
        }
        framesWithout = now.empty() ? framesWithout + 1 : 0;
        for (const Taken& t : now) {
            if (firstFrame < 0) firstFrame = t.frame;
            follow.step(t.reading, t.frame);
            width = t.width, height = t.height;
            ++pictures;
        }
    }

    // The next picture kept as it is, to be saved (savedPicture).
    void keepNext() {
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
    };
    std::mutex lock; // what the capture's thread hands over
    std::deque<Taken> taken;
    bool saveWanted = false, savedReady = false;
    std::vector<uint8_t> saved;
    int savedWidth = 0, savedHeight = 0;

    // On the capture's thread: a picture read; the frame from the time it was shown (frames of 1/60 s).
    void took(const realgame::Picture& p, int64_t time, const realgame::Layout& table) {
        Taken t{realgame::read(p, table), (time * 60 + 5000000) / 10000000, p.width, p.height};
        std::lock_guard<std::mutex> hold(lock);
        if (taken.size() >= 600) taken.pop_front(); // (no one takes them: the oldest go)
        taken.push_back(std::move(t));
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
