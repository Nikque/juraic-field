#pragma once
// The mode in which Juraic plays the real game (play/realgame_win.h), with the parts for finding faults: a film of the
// two windows of the next pairs, for finding why a window was read wrong - the pictures of the record (play/realgame_log.h)
// are seconds apart and a window read wrong for a few frames is not on them. Of each picture the two windows are kept,
// for some seconds; where a pair was learnt and learnt otherwise, the two windows differed, or a window read one pair in
// both places for a few readings (note), the frames around it go into a folder of the record:
//   <frame>-<kind>.bmp  a sheet: for each frame a cell, in the order of the frames, the window of 1P and that of 2P under
//                       a bar, red over the frames the sheet was asked for (the two windows as the picture had them)
//   <frame>-<kind>.txt  what each frame read, where the boxes lie on the picture and the points read in them
// The sheets are made and written on a thread of their own. Plain C++.
#include "realgame_read.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace play::realgame {

class WindowFilm {
public:
    // What a sheet is asked for: a pair learnt otherwise (Tracker::relearnt), a pair the two windows show otherwise
    // (Tracker::unlike), any other reading that did not fit (Tracker::doubts), a window that read one pair in both places
    // for a few readings, a pair counted put unseen (SideTrack::unseen).
    enum Kind { RELEARNT, UNLIKE, DOUBT, SLIDING, UNSEEN, KINDS };
    static constexpr const char* KIND_NAMES[KINDS] = {"relearnt", "unlike", "doubt", "sliding", "unseen"};
    static constexpr int MOST_SHEETS[KINDS] = {100, 100, 50, 100, 100};
    struct Box {
        int x = 0, y = 0, w = 0, h = 0;
    };
    static constexpr int BEFORE = 20, AFTER = 10; // frames on a sheet before and after the frame it is asked for
    static constexpr int COLUMNS = 10;            // cells in a row of a sheet
    static constexpr int BAR = 6, SIDE_GAP = 2, ROW_GAP = 4;
    static constexpr int MARGIN = 56;             // base pixels of a box around the points read in the window
    static constexpr int MOST_FRAMES = 900;       // frames held: 15 seconds (a pair stands learnt wrong for 4 on average)
    static constexpr size_t MOST_BYTES = size_t(600) << 20; // ... at most so much of memory
    static constexpr int SLIDE_LEAST = 3, SLIDE_MOST = 7;   // readings in a row of one pair in both places: a window sliding

    WindowFilm() = default;
    WindowFilm(const WindowFilm&) = delete;
    WindowFilm& operator=(const WindowFilm&) = delete;
    ~WindowFilm() { close(); }

    // The box of the window of a side on a picture of this size.
    static Box boxOf(const SideLayout& d, const Layout& l, int width, int height) {
        const float sx = float(width) / float(l.baseWidth), sy = float(height) / float(l.baseHeight);
        float x0 = d.next[0][0].x, x1 = x0, y0 = d.next[0][0].y, y1 = y0;
        for (const auto& pair : d.next)
            for (const Spot& s : pair) x0 = std::min(x0, s.x), x1 = std::max(x1, s.x), y0 = std::min(y0, s.y), y1 = std::max(y1, s.y);
        const int left = std::max(0, int((x0 - MARGIN) * sx)), top = std::max(0, int((y0 - MARGIN) * sy));
        const int right = std::min(width, int((x1 + MARGIN) * sx) + 1), bottom = std::min(height, int((y1 + MARGIN) * sy) + 1);
        return {left, top, std::max(0, right - left), std::max(0, bottom - top)};
    }
    // The frames held of pictures of this size.
    static int framesFor(int width, int height, const Layout& l) {
        const Box a = boxOf(l.side[0], l, width, height), b = boxOf(l.side[1], l, width, height);
        const size_t bytes = size_t(a.w) * size_t(a.h) * 4 + size_t(b.w) * size_t(b.h) * 4;
        return bytes ? int(std::min(size_t(MOST_FRAMES), MOST_BYTES / bytes)) : 0;
    }

    // A new folder `folder` (made here). False when it cannot be.
    bool open(const std::filesystem::path& folder) {
        close();
        std::error_code e;
        std::filesystem::create_directories(folder, e);
        if (e) return false;
        dir = folder;
        {
            std::lock_guard<std::mutex> hold(lock);
            stopping = false, newest = -1;
            for (int k = 0; k < KINDS; ++k) sheets[k] = 0, spanFrom[k] = 1, spanTo[k] = 0;
        }
        width = height = 0;
        seen = Seen{};
        alikeFor[0] = alikeFor[1] = 0;
        open_ = true;
        writer = std::thread([this] { writeLoop(); });
        return true;
    }
    bool isOpen() const { return open_; }
    // (the sheets still waiting are written first, with the frames there are)
    void close() {
        open_ = false;
        {
            std::lock_guard<std::mutex> hold(lock);
            stopping = true;
        }
        wake.notify_all();
        if (writer.joinable()) writer.join();
        std::lock_guard<std::mutex> hold(lock);
        shots.clear();
    }
    int framesHeld() const {
        std::lock_guard<std::mutex> hold(lock);
        return int(shots.size());
    }

    // On the thread of the pictures, each picture as it was read. A picture of another size lets go of the frames held.
    void take(const Picture& p, int64_t frame, const Reading& r, const Layout& l) {
        if (!open_) return;
        if (p.width != width || p.height != height) {
            width = p.width, height = p.height;
            most = framesFor(width, height, l);
            const float sx = float(width) / float(l.baseWidth), sy = float(height) / float(l.baseHeight);
            patch = l.patch * sx;
            for (int s = 0; s < 2; ++s) {
                box[s] = boxOf(l.side[s], l, width, height);
                for (int n = 0; n < 2; ++n)
                    for (int k = 0; k < 2; ++k) spot[s][n][k] = {l.side[s].next[n][k].x * sx - float(box[s].x), l.side[s].next[n][k].y * sy - float(box[s].y)};
            }
            std::lock_guard<std::mutex> hold(lock);
            shots.clear();
        }
        if (most <= 0) return;
        std::shared_ptr<Shot> s;
        {
            std::lock_guard<std::mutex> hold(lock);
            if (int(shots.size()) >= most) {
                s = std::move(shots.front());
                shots.pop_front();
                if (s.use_count() > 1) s.reset(); // (the writer is making a sheet of it)
            }
        }
        if (!s) s = std::make_shared<Shot>();
        s->frame = frame, s->width = p.width, s->height = p.height, s->patch = patch;
        std::memcpy(s->spot, spot, sizeof spot);
        s->pixels.resize(size_t(box[0].w) * size_t(box[0].h) * 4 + size_t(box[1].w) * size_t(box[1].h) * 4);
        uint8_t* to = s->pixels.data();
        for (int k = 0; k < 2; ++k) {
            s->box[k] = box[k];
            for (int n = 0; n < 2; ++n) s->next[k][n] = r.side[k].next[n], s->nextRead[k][n] = r.side[k].nextRead[n];
            for (int y = 0; y < box[k].h; ++y, to += size_t(box[k].w) * 4)
                std::memcpy(to, p.pixels + size_t(box[k].y + y) * size_t(p.stride) + size_t(box[k].x) * 4, size_t(box[k].w) * 4);
        }
        bool due;
        {
            std::lock_guard<std::mutex> hold(lock);
            shots.push_back(std::move(s));
            newest = frame;
            due = !asked.empty() && newest >= asked.front().to;
        }
        if (due) wake.notify_all();
    }

    // A sheet of the frames `from` to `to`, its bars red over `marks`, named after `at`; at most MOST_SHEETS of a kind, none
    // for a frame the last sheet of its kind has. False when none is made.
    bool ask(Kind kind, int64_t at, int64_t from, int64_t to, std::vector<int64_t> marks) {
        if (!open_) return false;
        {
            std::lock_guard<std::mutex> hold(lock);
            if (sheets[kind] >= MOST_SHEETS[kind] || (at >= spanFrom[kind] && at <= spanTo[kind])) return false;
            ++sheets[kind], spanFrom[kind] = from, spanTo[kind] = to;
            asked.push_back({kind, at, from, to, std::move(marks)});
        }
        wake.notify_all();
        return true;
    }
    bool ask(Kind kind, int64_t at) { return ask(kind, at, at - BEFORE, at + AFTER, {at}); }
    // Something of the frame `was` found out in the frame `now`: one sheet of both, or one of each when they are far apart.
    void askBetween(Kind kind, int64_t was, int64_t now) {
        if (now - was <= BEFORE + AFTER) {
            ask(kind, now, was - BEFORE, now + AFTER, {was, now});
        } else {
            ask(kind, was);
            ask(kind, now);
        }
    }

    // After each reading followed: sheets of what the tracker has noted since the last one.
    void note(const Tracker& t, int64_t at) {
        if (!open_) return;
        int learntAgain = 0;
        for (int s = 0; s < 2; ++s) {
            if (t.relearnt[s] > seen.relearnt[s]) learntAgain += t.relearnt[s] - seen.relearnt[s], askBetween(RELEARNT, t.relearntFrom[s], at);
            if (t.side[s].unseen > seen.unseen[s]) ask(UNSEEN, at);
            seen.relearnt[s] = t.relearnt[s], seen.unseen[s] = t.side[s].unseen;
            const SideTrack& w = t.side[s];
            const bool alike = w.nextRead[0] && w.nextRead[1] && w.next[0].axis == w.next[1].axis && w.next[0].child == w.next[1].child;
            if (alike) ++alikeFor[s];
            else {
                if (alikeFor[s] >= SLIDE_LEAST && alikeFor[s] <= SLIDE_MOST) ask(SLIDING, at, at - alikeFor[s] - BEFORE / 2, at + AFTER, {at - alikeFor[s], at});
                alikeFor[s] = 0;
            }
        }
        if (t.unlike > seen.unlike) askBetween(UNLIKE, t.unlikeFrom, at);
        if (t.doubts - seen.doubts > learntAgain) ask(DOUBT, at);
        seen.unlike = t.unlike, seen.doubts = t.doubts;
    }

private:
    struct Shot {
        int64_t frame = 0;
        int width = 0, height = 0;
        Box box[2];
        Spot spot[2][2][2]{}; // (the points read, on the boxes: Layout::next of each side)
        float patch = 0;
        juraic::Pair next[2][2]{};
        bool nextRead[2][2]{};
        std::vector<uint8_t> pixels; // (the window of 1P, then that of 2P, rows from the top)
    };
    struct Sheet {
        Kind kind = RELEARNT;
        int64_t at = 0, from = 0, to = 0;
        std::vector<int64_t> marks;
    };
    // What the tracker had noted (note).
    struct Seen {
        int relearnt[2]{}, unseen[2]{}, unlike = 0, doubts = 0;
    };
    std::filesystem::path dir;
    std::atomic<bool> open_{false};
    std::thread writer;
    // (of the thread of the pictures)
    int width = 0, height = 0, most = 0;
    Box box[2];
    Spot spot[2][2][2]{};
    float patch = 0;
    // (of the thread of the play)
    Seen seen;
    int alikeFor[2]{};
    mutable std::mutex lock; // (of what follows)
    std::condition_variable wake;
    std::deque<std::shared_ptr<Shot>> shots;
    std::deque<Sheet> asked;
    bool stopping = false;
    int64_t newest = -1;
    int sheets[KINDS]{};
    int64_t spanFrom[KINDS]{}, spanTo[KINDS]{};

    void writeLoop() {
        for (;;) {
            Sheet q;
            std::vector<std::shared_ptr<Shot>> got;
            {
                std::unique_lock<std::mutex> hold(lock);
                wake.wait(hold, [this] { return stopping || (!asked.empty() && newest >= asked.front().to); });
                if (asked.empty()) return;
                q = std::move(asked.front());
                asked.pop_front();
                for (const auto& s : shots)
                    if (s->frame >= q.from && s->frame <= q.to) got.push_back(s);
            }
            if (!got.empty()) write(q, got);
        }
    }
    void write(const Sheet& q, const std::vector<std::shared_ptr<Shot>>& got) {
        const Box a = got[0]->box[0], b = got[0]->box[1];
        const int cellW = a.w + b.w + SIDE_GAP, cellH = std::max(a.h, b.h) + BAR + ROW_GAP;
        const int cells = int(got.size()), rows = (cells + COLUMNS - 1) / COLUMNS;
        const int w = COLUMNS * (cellW + ROW_GAP), h = rows * cellH;
        std::vector<uint8_t> pixels(size_t(w) * size_t(h) * 4, 0);
        for (int i = 0; i < cells; ++i) {
            const Shot& s = *got[size_t(i)];
            const int x0 = i % COLUMNS * (cellW + ROW_GAP), y0 = i / COLUMNS * cellH;
            const bool marked = std::find(q.marks.begin(), q.marks.end(), s.frame) != q.marks.end();
            for (int y = 0; y < BAR; ++y)
                for (int x = 0; x < cellW; ++x) {
                    uint8_t* to = pixels.data() + (size_t(y0 + y) * size_t(w) + size_t(x0 + x)) * 4;
                    to[0] = marked ? 0 : 80, to[1] = marked ? 0 : 80, to[2] = marked ? 255 : 80;
                }
            const uint8_t* from = s.pixels.data();
            int x = x0;
            for (const Box& k : {a, b}) {
                for (int y = 0; y < k.h; ++y, from += size_t(k.w) * 4)
                    std::memcpy(pixels.data() + (size_t(y0 + BAR + y) * size_t(w) + size_t(x)) * 4, from, size_t(k.w) * 4);
                x += k.w + SIDE_GAP;
            }
        }
        const std::string name = std::to_string(q.at) + "-" + KIND_NAMES[q.kind];
        saveBmp(dir / (name + ".bmp"), {pixels.data(), w, h, w * 4});
        std::ofstream text(dir / (name + ".txt"), std::ios::binary);
        text << "kind " << KIND_NAMES[q.kind] << "\nmarks";
        for (const int64_t m : q.marks) text << ' ' << m;
        text << "\npicture " << got[0]->width << ' ' << got[0]->height << "\ncells: " << COLUMNS << " in a row, the window of 1P, then that of 2P, under a bar of " << BAR << " pixels\n";
        text << "points read: the square " << got[0]->patch << " pixels each way around each of these, axis then child, next then next-next, on its box\n";
        for (int s = 0; s < 2; ++s) {
            const Box& k = s ? b : a;
            text << "box " << (s ? "2P " : "1P ") << k.x << ' ' << k.y << ' ' << k.w << ' ' << k.h << "\npoints " << (s ? "2P" : "1P");
            for (const auto& pair : got[0]->spot[s])
                for (const Spot& p : pair) text << ' ' << p.x << ' ' << p.y;
            text << '\n';
        }
        for (const auto& s : got) {
            text << s->frame;
            for (int k = 0; k < 2; ++k) {
                text << (k ? " 2P" : " 1P");
                for (int n = 0; n < 2; ++n) {
                    if (s->nextRead[k][n]) text << ' ' << int(s->next[k][n].axis) << int(s->next[k][n].child);
                    else text << " ?";
                }
            }
            text << '\n';
        }
    }
};

} // namespace play::realgame
