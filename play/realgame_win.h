#pragma once
// The mode in which Juraic plays the second player of the real game running on the same PC (the Windows build
// only): the real game's offline versus of two players, a person on 1P. Never for a match over a network.
// This part holds what the screen of the mode sets: which of the two AIs plays and at which level. Each time the
// mode is opened both start from the AI of the 2P side in the settings (Settings::aiStyle, aiLevel); what is
// changed here is not written back to the settings.
// And the reading of the real game's screen: the pictures of the window the person chooses (play/realgame_capture_win.h)
// read with the table (play/realgame_read.h) and followed frame after frame with the rules of the game
// (play/realgame_follow.h). Nothing is pressed in the real game yet.
#include "../ai/level.h"
#include "realgame_capture_win.h"
#include "realgame_follow.h"
#include <deque>
#include <mutex>

namespace play {

struct RealGameMode {
    enum Row { ROW_STYLE, ROW_LEVEL, ROW_READ, ROW_BACK, ROWS };
    int row = ROW_STYLE;
    int style = 0;                 // 0: Juraic 催促（小）, 1: Juraic 催促（極小） (as Settings::aiStyle)
    int level = juraic::ai::LEVELS; // 1 .. LEVELS (as Settings::aiLevel)

    RealGameMode() = default;
    RealGameMode(const RealGameMode&) = delete;
    RealGameMode& operator=(const RealGameMode&) = delete;
    ~RealGameMode() { capture.stop(); } // (before the members the capture's thread hands its pictures to are gone)

    void open(int settingsStyle, int settingsLevel) {
        row = ROW_STYLE;
        style = settingsStyle ? 1 : 0;
        level = settingsLevel < 1 ? 1 : settingsLevel > juraic::ai::LEVELS ? juraic::ai::LEVELS : settingsLevel;
        stopReading();
    }
    // The value of the row under the cursor changed by `by` steps; past either end it comes in again at the other
    // (as on the settings screen: Lv 36 → Lv 1). False: the row has no value.
    bool change(int by) {
        if (row == ROW_STYLE) style = ((style + by) % 2 + 2) % 2;
        else if (row == ROW_LEVEL) level = ((level - 1 + by) % juraic::ai::LEVELS + juraic::ai::LEVELS) % juraic::ai::LEVELS + 1;
        else return false;
        return true;
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
