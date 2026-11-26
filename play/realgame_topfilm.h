#pragma once
// The mode in which Juraic plays the real game (play/realgame_win.h), with the parts for finding faults: a film of the
// top of the field of 2P, for finding what the game does with a pair up there - the readings of the record tell two
// points a cell, a pair in the 13th row shows none, and the pictures of the record are seconds apart. Of each picture
// the top rows of the field of 2P are kept, every second pixel of every second row, for some seconds; for a pair the engine has had in the 13th row, and around
// a picture that put the engine's pair right, the frames go into the folder of the film (play/realgame_film.h):
//   <frame>-<kind>.bmp  a sheet: for each frame a cell, in the order of the frames, under a bar, red over the frames the
//                       sheet was asked for (the top of the field as the picture had it)
//   <frame>-<kind>.txt  the frames of the cells, the box on the picture, and what each frame read in rows 10 to 14
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

class TopFilm {
public:
    // What a sheet is asked for: a pair the engine has had in the 13th row (from the frame it was begun in to the frame
    // the next was), a picture that put the engine's pair right.
    enum Kind { ALOFT, PUT_RIGHT, KINDS };
    static constexpr const char* KIND_NAMES[KINDS] = {"aloft", "put-right"};
    static constexpr int MOST_SHEETS[KINDS] = {120, 80};
    struct Box {
        int x = 0, y = 0, w = 0, h = 0;
    };
    static constexpr int ROWS_BELOW = 3, ROWS_ABOVE = 1; // cells kept below and above the top of what the screen shows
    static constexpr int BEFORE = 6, AFTER = 6;          // frames on a sheet before the pair was begun and after the next was
    static constexpr int MOST_SPAN = 140;                // frames on a sheet at most (the first of them)
    static constexpr int AROUND = 24;                    // frames before and after a picture that put the pair right
    static constexpr int COLUMNS = 10;                   // cells in a row of a sheet
    static constexpr int BAR = 6, GAP = 4;
    static constexpr int STEP = 2;                       // of the picture every so many pixels each way are kept
    static constexpr int MOST_FRAMES = 400;              // frames held
    static constexpr size_t MOST_BYTES = size_t(300) << 20; // ... at most so much of memory

    TopFilm() = default;
    TopFilm(const TopFilm&) = delete;
    TopFilm& operator=(const TopFilm&) = delete;
    ~TopFilm() { close(); }

    // The box of the top of the field of a side on a picture of this size.
    static Box boxOf(const SideLayout& d, const Layout& l, int width, int height) {
        const float sx = float(width) / float(l.baseWidth), sy = float(height) / float(l.baseHeight);
        const float top = d.bottom - d.shownRows * d.cellHeight;
        const int left = std::max(0, int(d.left * sx)), right = std::min(width, int((d.left + float(juraic::W) * d.cellWidth) * sx) + 1);
        const int up = std::max(0, int((top - float(ROWS_ABOVE) * d.cellHeight) * sy)), down = std::min(height, int((top + float(ROWS_BELOW) * d.cellHeight) * sy) + 1);
        return {left, up, std::max(0, right - left), std::max(0, down - up)};
    }

    // Sheets into `folder` (made here when it is not there). False when it cannot be.
    bool open(const std::filesystem::path& folder) {
        close();
        std::error_code e;
        std::filesystem::create_directories(folder, e);
        if (e) return false;
        dir = folder;
        {
            std::lock_guard<std::mutex> hold(lock);
            stopping = false, newest = -1;
            for (int k = 0; k < KINDS; ++k) sheets[k] = 0;
        }
        width = height = 0;
        pair = -1, pairAt = 0, aloft = false, sent = false;
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
        asked.clear();
    }

    // On the thread of the pictures, each picture as it was read. A picture of another size lets go of the frames held.
    void take(const Picture& p, int64_t frame, const Reading& r, const Layout& l) {
        if (!open_) return;
        if (p.width != width || p.height != height) {
            width = p.width, height = p.height;
            box = boxOf(l.side[1], l, width, height);
            keptW = box.w / STEP, keptH = box.h / STEP;
            const size_t bytes = size_t(keptW) * size_t(keptH) * 4;
            most = bytes ? int(std::min(size_t(MOST_FRAMES), MOST_BYTES / bytes)) : 0;
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
        s->frame = frame, s->width = p.width, s->height = p.height, s->box = box, s->w = keptW, s->h = keptH;
        for (int x = 0; x < juraic::W; ++x)
            for (int k = 0; k < TOLD; ++k) s->point[x][k] = r.side[1].point[x][FIRST + k];
        s->pixels.resize(size_t(keptW) * size_t(keptH) * 4);
        uint8_t* to = s->pixels.data();
        for (int y = 0; y < keptH; ++y) {
            const uint8_t* from = p.pixels + size_t(box.y + y * STEP) * size_t(p.stride) + size_t(box.x) * 4;
            for (int x = 0; x < keptW; ++x, to += 4, from += size_t(STEP) * 4) std::memcpy(to, from, 4);
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

    // On the thread of the play, after each frame driven: the pair the driver has begun (its number), whether the engine
    // has had it in the 13th row, whether a picture put it right in this frame, the frame of the clock.
    void follow(int begun, bool high, bool putRight, int64_t at) {
        if (!open_) return;
        // (the first MOST_SPAN frames of the pair: a pair whose chain runs long is asked for as they are all in - asked
        //  for only as the next pair was begun, the sheet of such a pair had its last frames, the pair no longer in them)
        if (begun != pair) {
            if (aloft && !sent) ask(ALOFT, pairAt, pairAt - BEFORE, at + AFTER, {pairAt, at});
            pair = begun, pairAt = at, aloft = false, sent = false;
        }
        aloft = aloft || high;
        if (aloft && !sent && at >= pairAt - BEFORE + MOST_SPAN) ask(ALOFT, pairAt, pairAt - BEFORE, pairAt - BEFORE + MOST_SPAN, {pairAt}), sent = true;
        if (putRight) ask(PUT_RIGHT, at, at - AROUND, at + AROUND, {at});
    }

private:
    static constexpr int FIRST = 18, TOLD = POINTS - FIRST; // the points of a column told in the text: rows 10 to 14
    struct Shot {
        int64_t frame = 0;
        int width = 0, height = 0;
        Box box;          // (on the picture)
        int w = 0, h = 0; // (of the pixels kept)
        juraic::Cell point[juraic::W][TOLD]{};
        std::vector<uint8_t> pixels; // (rows from the top)
    };
    struct Sheet {
        Kind kind = ALOFT;
        int64_t at = 0, from = 0, to = 0;
        std::vector<int64_t> marks;
    };
    std::filesystem::path dir;
    std::atomic<bool> open_{false};
    std::thread writer;
    // (of the thread of the pictures)
    int width = 0, height = 0, most = 0, keptW = 0, keptH = 0;
    Box box;
    // (of the thread of the play)
    int pair = -1;
    int64_t pairAt = 0;
    bool aloft = false;
    bool sent = false; // (the sheet of the pair has been asked for)
    mutable std::mutex lock; // (of what follows)
    std::condition_variable wake;
    std::deque<std::shared_ptr<Shot>> shots;
    std::deque<Sheet> asked;
    bool stopping = false;
    int64_t newest = -1;
    int sheets[KINDS]{};

    void ask(Kind kind, int64_t at, int64_t from, int64_t to, std::vector<int64_t> marks) {
        {
            std::lock_guard<std::mutex> hold(lock);
            if (sheets[kind] >= MOST_SHEETS[kind]) return;
            ++sheets[kind];
            asked.push_back({kind, at, from, to, std::move(marks)});
        }
        wake.notify_all();
    }
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
        const Box b = got[0]->box;
        const int bw = got[0]->w, bh = got[0]->h;
        const int cellW = bw + GAP, cellH = bh + BAR + GAP;
        const int cells = int(got.size()), rows = (cells + COLUMNS - 1) / COLUMNS;
        const int w = COLUMNS * cellW, h = rows * cellH;
        std::vector<uint8_t> pixels(size_t(w) * size_t(h) * 4, 0);
        for (int i = 0; i < cells; ++i) {
            const Shot& s = *got[size_t(i)];
            if (s.w != bw || s.h != bh) continue; // (a picture of another size)
            const int x0 = i % COLUMNS * cellW, y0 = i / COLUMNS * cellH;
            const bool marked = std::find(q.marks.begin(), q.marks.end(), s.frame) != q.marks.end();
            for (int y = 0; y < BAR; ++y)
                for (int x = 0; x < bw; ++x) {
                    uint8_t* to = pixels.data() + (size_t(y0 + y) * size_t(w) + size_t(x0 + x)) * 4;
                    to[0] = marked ? 0 : 80, to[1] = marked ? 0 : 80, to[2] = marked ? 255 : 80;
                }
            const uint8_t* from = s.pixels.data();
            for (int y = 0; y < bh; ++y, from += size_t(bw) * 4)
                std::memcpy(pixels.data() + (size_t(y0 + BAR + y) * size_t(w) + size_t(x0)) * 4, from, size_t(bw) * 4);
        }
        const std::string name = std::to_string(q.at) + "-" + KIND_NAMES[q.kind];
        saveBmp(dir / (name + ".bmp"), {pixels.data(), w, h, w * 4});
        std::ofstream text(dir / (name + ".txt"), std::ios::binary);
        text << "kind " << KIND_NAMES[q.kind] << "\nmarks";
        for (const int64_t m : q.marks) text << ' ' << m;
        text << "\npicture " << got[0]->width << ' ' << got[0]->height << "\ncells: " << COLUMNS << " in a row, the top of the field of 2P under a bar of " << BAR
             << " pixels: " << ROWS_ABOVE << " cell above what the screen shows of the field, " << ROWS_BELOW << " below\n";
        text << "box " << b.x << ' ' << b.y << ' ' << b.w << ' ' << b.h << ", every " << STEP << " pixels each way kept: " << bw << " x " << bh << " a cell\n";
        text << "each frame: the points read of rows 10 to 14, a column two letters a row (the lower, then the upper), the lowest row first (. empty, 7 above the screen)\n";
        for (const auto& s : got) {
            text << s->frame;
            for (int k = 0; k < TOLD; k += 2) {
                text << ' ';
                for (int x = 0; x < juraic::W; ++x)
                    for (int half = 0; half < 2; ++half) {
                        const juraic::Cell c = s->point[x][k + half];
                        text << (c == juraic::EMPTY ? '.' : char('0' + int(c)));
                    }
            }
            text << '\n';
        }
    }
};

} // namespace play::realgame
