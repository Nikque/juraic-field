#pragma once
// The mode in which Juraic plays 2P of the real game (play/realgame_win.h): the record of a reading, for finding later
// what went wrong in it - a folder of its own for each time reading begins:
//   readings.bin  every reading of the screen in its order (the frame, the size of the picture, what each side shows)
//                 and, after the readings of a frame of this program, the buttons the pad held from then on: enough to
//                 follow the reading again with Follower and Driver (RecordReader reads it back); with each reading
//                 the times its picture was shown, came in and was read, with each drive of the pad the times it was
//                 driven and set and why, and each placement the driver told Follower (tools/realgame_replay.cpp);
//   events.txt    what happened, as lines of text (new pairs, placements found, checks that failed, readings that did not
//                 fit, the placements the AI chose, the pair put right, the lags measured, presses of the pad);
//   *.bmp         pictures of the screen: one every few seconds and one where something did not fit (at most so many).
// The pictures are written on a thread of their own, so that the play does not wait for the disk. Plain C++.
#include "realgame_read.h"
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace play::realgame {

// The kinds of record of readings.bin, each a byte followed by its fields. Times are in 100 ns of the clock of the
// pictures (QueryPerformanceCounter).
enum RecordKind : uint8_t {
    RECORD_READING = 'R', // int64 frame, int32 width, int32 height, then the two SideRead as they are in memory
    RECORD_PAD = 'P',     // int64 frame of the clock, uint8 buttons of the game (game/control.h), uint16 buttons of the
                          // pad (XUSB), uint8 what drove it (0 nothing, 1 the AI, 2 a press asked for)
    // (from the second kind of record on: JFRGLOG2)
    RECORD_TIME = 'T',    // before each reading: int64 the time the picture was shown (as the capture gives it), the time
                          // it came in, the time it was read; uint8 followed (0: of a frame not after the last one followed)
    RECORD_SENT = 'S',    // after each pad record: int64 the time the pad was driven, the time the call that set the pad
                          // returned (not the time the game took the buttons: that the pictures tell, the lag); uint8 why it
                          // was driven (1 pictures came, 2 none came in the frame, 3 none came for long - it holds nothing
                          // -, 4 a menu read - it holds nothing)
    RECORD_TELL = 'L',    // int32 the number of the own pair (from 0 in the round) the driver's engine locked, uint8 where
                          // (juraic::placementId): what the driver told Follower (Follower::tell)
};
constexpr char RECORD_MAGIC[8] = {'J', 'F', 'R', 'G', 'L', 'O', 'G', '2'};
constexpr char RECORD_MAGIC_1[8] = {'J', 'F', 'R', 'G', 'L', 'O', 'G', '1'}; // (records of readings and the pad only)

struct PadRecord {
    int64_t frame = 0;
    uint8_t buttons = 0;
    uint16_t pad = 0;
    uint8_t by = 0;
};
struct TimeRecord {
    int64_t shown = 0, came = 0, read = 0;
    uint8_t followed = 1;
};
struct SentRecord {
    int64_t driven = 0, set = 0;
    uint8_t why = 0;
};
struct TellRecord {
    int32_t pair = -1;
    uint8_t placement = 0;
};

class Record {
public:
    static constexpr int MAX_PICTURES = 200;
    static constexpr int PICTURE_EVERY = 3 * 60; // frames between the pictures taken at any time
    static constexpr int PICTURE_GAP = 30;       // frames at least between two pictures of things that did not fit

    Record() = default;
    Record(const Record&) = delete;
    Record& operator=(const Record&) = delete;
    ~Record() { close(); }

    // A new folder `folder` (made here) for a reading; the lines of `header` begin events.txt. False when it cannot be.
    bool open(const std::filesystem::path& folder, const std::string& header) {
        close();
        std::error_code e;
        std::filesystem::create_directories(folder, e);
        bin.open(folder / "readings.bin", std::ios::binary | std::ios::trunc);
        text.open(folder / "events.txt", std::ios::binary | std::ios::trunc);
        if (!bin || !text) return close(), false;
        bin.write(RECORD_MAGIC, sizeof RECORD_MAGIC);
        const uint32_t size = uint32_t(sizeof(SideRead));
        bin.write(reinterpret_cast<const char*>(&size), sizeof size);
        text << header;
        if (!header.empty() && header.back() != '\n') text << '\n';
        dir = folder;
        {
            std::lock_guard<std::mutex> hold(lock);
            pictures = 0, lastPicture = lastEvent = -1000000, asked.clear();
            stopping = false, open_ = true;
        }
        writer = std::thread([this] { writeLoop(); });
        return true;
    }
    bool isOpen() const { return bin.is_open(); }
    const std::filesystem::path& folder() const { return dir; }
    // (the pictures still waiting are written first)
    void close() {
        {
            std::lock_guard<std::mutex> hold(lock);
            open_ = false, stopping = true;
        }
        wake.notify_all();
        if (writer.joinable()) writer.join();
        if (bin.is_open()) bin.close();
        std::lock_guard<std::mutex> hold(textLock);
        if (text.is_open()) text.close();
    }

    void reading(const Reading& r, int64_t frame, int width, int height) {
        if (!bin) return;
        const uint8_t kind = RECORD_READING;
        const int32_t w = width, h = height;
        bin.write(reinterpret_cast<const char*>(&kind), 1);
        bin.write(reinterpret_cast<const char*>(&frame), sizeof frame);
        bin.write(reinterpret_cast<const char*>(&w), sizeof w);
        bin.write(reinterpret_cast<const char*>(&h), sizeof h);
        bin.write(reinterpret_cast<const char*>(&r.side[0]), sizeof(SideRead));
        bin.write(reinterpret_cast<const char*>(&r.side[1]), sizeof(SideRead));
    }
    void pad(const PadRecord& p) {
        if (!bin) return;
        const uint8_t kind = RECORD_PAD;
        bin.write(reinterpret_cast<const char*>(&kind), 1);
        bin.write(reinterpret_cast<const char*>(&p.frame), sizeof p.frame);
        bin.write(reinterpret_cast<const char*>(&p.buttons), 1);
        bin.write(reinterpret_cast<const char*>(&p.pad), sizeof p.pad);
        bin.write(reinterpret_cast<const char*>(&p.by), 1);
    }
    void time(const TimeRecord& t) {
        if (!bin) return;
        const uint8_t kind = RECORD_TIME;
        bin.write(reinterpret_cast<const char*>(&kind), 1);
        for (const int64_t v : {t.shown, t.came, t.read}) bin.write(reinterpret_cast<const char*>(&v), sizeof v);
        bin.write(reinterpret_cast<const char*>(&t.followed), 1);
    }
    void sent(const SentRecord& s) {
        if (!bin) return;
        const uint8_t kind = RECORD_SENT;
        bin.write(reinterpret_cast<const char*>(&kind), 1);
        for (const int64_t v : {s.driven, s.set}) bin.write(reinterpret_cast<const char*>(&v), sizeof v);
        bin.write(reinterpret_cast<const char*>(&s.why), 1);
    }
    void tell(const TellRecord& t) {
        if (!bin) return;
        const uint8_t kind = RECORD_TELL;
        bin.write(reinterpret_cast<const char*>(&kind), 1);
        bin.write(reinterpret_cast<const char*>(&t.pair), sizeof t.pair);
        bin.write(reinterpret_cast<const char*>(&t.placement), 1);
    }
    // A line of events.txt, after the frame it is of (from either thread).
    void event(int64_t frame, const std::string& line) {
        std::lock_guard<std::mutex> hold(textLock);
        if (text) text << frame << ' ' << line << '\n';
    }
    // Lines are written out (as a frame of this program ends).
    void flush() {
        if (bin) bin.flush();
        std::lock_guard<std::mutex> hold(textLock);
        if (text) text.flush();
    }

    // The next picture offered is kept, named after `why` (something did not fit): at most one in PICTURE_GAP frames.
    void ask(const std::string& why) {
        std::lock_guard<std::mutex> hold(lock);
        if (asked.empty()) asked = why;
    }
    // On the thread of the pictures, each picture as it comes: kept when one was asked for (and PICTURE_GAP frames have
    // passed since the last kept so) or PICTURE_EVERY frames have passed since the last kept at any time; never more than
    // MAX_PICTURES. Written as <frame>[-<why>].bmp; dropped when the writer is four behind. True when it was kept.
    bool offer(const Picture& p, int64_t frame) {
        std::string why;
        {
            std::lock_guard<std::mutex> hold(lock);
            if (!open_ || pictures >= MAX_PICTURES || queue.size() >= 4) return false;
            // (asked for too soon after the last such picture: that one shows it, and the ask goes)
            if (!asked.empty() && frame - lastEvent < PICTURE_GAP) asked.clear();
            const bool odd = !asked.empty();
            if (!odd && frame - lastPicture < PICTURE_EVERY) return false;
            if (odd) why = asked, asked.clear(), lastEvent = frame;
            else lastPicture = frame;
            ++pictures;
        }
        Pending q;
        q.name = std::to_string(frame) + (why.empty() ? "" : "-" + why) + ".bmp";
        q.width = p.width, q.height = p.height;
        q.pixels.resize(size_t(p.width) * size_t(p.height) * 4);
        for (int y = 0; y < p.height; ++y) std::memcpy(q.pixels.data() + size_t(y) * size_t(p.width) * 4, p.pixels + size_t(y) * size_t(p.stride), size_t(p.width) * 4);
        event(frame, "picture " + q.name);
        {
            std::lock_guard<std::mutex> hold(lock);
            queue.push_back(std::move(q));
        }
        wake.notify_all();
        return true;
    }
    int picturesTaken() const {
        std::lock_guard<std::mutex> hold(lock);
        return pictures;
    }

private:
    struct Pending {
        std::string name;
        int width = 0, height = 0;
        std::vector<uint8_t> pixels;
    };
    std::filesystem::path dir;
    std::ofstream bin, text;
    std::mutex textLock; // (of text)
    std::thread writer;
    mutable std::mutex lock; // (of what follows: the thread of the pictures and the writer use it)
    std::condition_variable wake;
    std::deque<Pending> queue;
    bool stopping = false, open_ = false;
    int pictures = 0;
    int64_t lastPicture = -1000000, lastEvent = -1000000;
    std::string asked;

    void writeLoop() {
        for (;;) {
            Pending q;
            {
                std::unique_lock<std::mutex> hold(lock);
                wake.wait(hold, [this] { return stopping || !queue.empty(); });
                if (queue.empty()) return;
                q = std::move(queue.front());
                queue.pop_front();
            }
            saveBmp(dir / q.name, {q.pixels.data(), q.width, q.height, q.width * 4});
        }
    }
};

// readings.bin read back: its records in their order (of either kind of record: JFRGLOG1 has readings and the pad only).
class RecordReader {
public:
    TimeRecord time; // (the last of these kinds read)
    SentRecord sent;
    TellRecord tell;
    bool open(const std::filesystem::path& file) {
        in.open(file, std::ios::binary);
        char magic[8] = {};
        uint32_t size = 0;
        in.read(magic, sizeof magic);
        in.read(reinterpret_cast<char*>(&size), sizeof size);
        return in && (std::memcmp(magic, RECORD_MAGIC, sizeof magic) == 0 || std::memcmp(magic, RECORD_MAGIC_1, sizeof magic) == 0) && size == sizeof(SideRead);
    }
    void close() { in.close(); }
    // The next record: its kind (0 at the end), and its fields in `r`, `frame`, `width`, `height` or `p` - or in `time`,
    // `sent` or `tell`.
    uint8_t next(Reading& r, int64_t& frame, int& width, int& height, PadRecord& p) {
        uint8_t kind = 0;
        if (!in.read(reinterpret_cast<char*>(&kind), 1)) return 0;
        if (kind == RECORD_READING) {
            int32_t w = 0, h = 0;
            in.read(reinterpret_cast<char*>(&frame), sizeof frame);
            in.read(reinterpret_cast<char*>(&w), sizeof w);
            in.read(reinterpret_cast<char*>(&h), sizeof h);
            in.read(reinterpret_cast<char*>(&r.side[0]), sizeof(SideRead));
            in.read(reinterpret_cast<char*>(&r.side[1]), sizeof(SideRead));
            width = w, height = h;
        } else if (kind == RECORD_PAD) {
            in.read(reinterpret_cast<char*>(&p.frame), sizeof p.frame);
            in.read(reinterpret_cast<char*>(&p.buttons), 1);
            in.read(reinterpret_cast<char*>(&p.pad), sizeof p.pad);
            in.read(reinterpret_cast<char*>(&p.by), 1);
        } else if (kind == RECORD_TIME) {
            for (int64_t* v : {&time.shown, &time.came, &time.read}) in.read(reinterpret_cast<char*>(v), sizeof *v);
            in.read(reinterpret_cast<char*>(&time.followed), 1);
        } else if (kind == RECORD_SENT) {
            for (int64_t* v : {&sent.driven, &sent.set}) in.read(reinterpret_cast<char*>(v), sizeof *v);
            in.read(reinterpret_cast<char*>(&sent.why), 1);
        } else if (kind == RECORD_TELL) {
            in.read(reinterpret_cast<char*>(&tell.pair), sizeof tell.pair);
            in.read(reinterpret_cast<char*>(&tell.placement), 1);
        } else {
            return 0;
        }
        return in ? kind : 0;
    }

private:
    std::ifstream in;
};

// A field as lines of text, the top row first (. empty, 1 to 5 the colours, O garbage).
inline std::string fieldText(const juraic::GameField& g) {
    static const char symbols[] = ".12345O";
    std::string out;
    for (int y = juraic::H - 1; y >= 0; --y) {
        bool any = false;
        for (int x = 0; x < juraic::W; ++x) any = any || g.f.get(x, y) != juraic::EMPTY;
        if (!any) continue;
        out += "    ";
        for (int x = 0; x < juraic::W; ++x) {
            const juraic::Cell c = g.f.get(x, y);
            out += c < sizeof symbols - 1 ? symbols[c] : '?';
        }
        out += '\n';
    }
    return out.empty() ? "    (empty)\n" : out;
}

} // namespace play::realgame
