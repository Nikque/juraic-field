#pragma once
// The mode in which Juraic plays 2P of the real game (play/realgame_win.h): what a picture of the real game's window
// shows, read without help from the system. The picture is taken by play/realgame_capture_win.h (the Windows build);
// everything here is plain C++ and is tested on every system (tests/realgame_tests.cpp).
//
// Where things are on the screen and what colours they have is a table (Layout), in the pixels of a window whose
// inside is BASE_WIDTH x BASE_HEIGHT; a picture of another size is read with the table scaled to it. The table can be
// written to a text file and read from one (formatLayout, parseLayout), so that it can be made from pictures of the
// real game without building the program again (tools/realgame_read.cpp reads a saved picture with a table). The
// table built in (sampleLayout) is not taken from the real game: it stands in until one is.
//
// A field is read at two points of every cell: a quarter of a cell above its lower edge and a quarter of a cell
// below its upper edge. A piece at rest covers both points of its cell; the pair in play, which moves in half cells
// (game/control.h), covers the upper point of one cell and the lower point of the next when it is half a cell up.
//
// Tracker follows the readings frame by frame: the pieces at rest and whether they stand still, the pair in play,
// the pairs each side has been given and the pairs the windows of the next pairs have shown. observeSpawn makes of
// it what the AI is shown (ai/observe.h) in the frame in which a new pair of one side is seen on its field, one frame
// before it can be moved (Timing::nextShown).
#include "../ai/observe.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace play::realgame {

constexpr int BASE_WIDTH = 2240, BASE_HEIGHT = 1260; // the inside of the window the table is made for
constexpr int ROWS = 14;                             // rows read, the 14th included (the screen does not show it)
constexpr int POINTS = 2 * ROWS;                     // points read in a column
constexpr juraic::Cell HIDDEN = 7;                   // a point above what the screen shows of the field

// One picture: four bytes a pixel, blue, green, red and one more (not read), rows from the top.
struct Picture {
    const uint8_t* pixels = nullptr;
    int width = 0, height = 0;
    int stride = 0; // bytes from one row to the next
};

struct Rgb {
    int r = 0, g = 0, b = 0;
    bool operator==(const Rgb&) const = default;
};
struct Spot {
    float x = 0, y = 0;
    bool operator==(const Spot&) const = default;
};

// A colour of the table and the piece it stands for (EMPTY for a colour of the background).
struct ColorRef {
    juraic::Cell cell = juraic::EMPTY;
    Rgb rgb;
    bool operator==(const ColorRef&) const = default;
};
// One mark of the garbage notice: the colours at points around the middle of its place, and the garbage pieces it
// stands for.
struct Probe {
    float dx = 0, dy = 0;
    Rgb rgb;
    bool operator==(const Probe&) const = default;
};
struct Mark {
    int value = 0;
    std::vector<Probe> probes;
    bool operator==(const Mark&) const = default;
};

struct SideLayout {
    float left = 0, bottom = 0;           // the lower left corner of the field
    float cellWidth = 0, cellHeight = 0;
    float shownRows = 12;                 // how much of the field the screen shows, in cells from its floor
    Spot next[2][2];                      // the window of the next pairs, [0 next, 1 next-next][0 axis, 1 child]: the middle of the piece
    std::vector<Spot> notice;             // the places of the marks of the garbage notice
    bool operator==(const SideLayout&) const = default;
};

struct Layout {
    int baseWidth = BASE_WIDTH, baseHeight = BASE_HEIGHT;
    float patch = 4;              // a point is the mean of the square this many base pixels from it each way
    int colorDistance = 60;       // a point within this distance (red, green, blue) of a colour of the table is that colour
    std::vector<ColorRef> colors; // (the nearest counts; a point near none of them is empty)
    int markDistance = 60;        // the same for the points of a mark
    std::vector<Mark> marks;
    int stillFrames = 2;          // pieces at rest read the same for this many frames stand still
    SideLayout side[2];           // by seat: 0 the 1P side, 1 the 2P side
    bool operator==(const Layout&) const = default;
};

// ---- the table ------------------------------------------------------------------------------------------------

// A table to stand in until one is made from pictures of the real game: the sizes and the places are guesses and the
// colours plain ones. The tests draw pictures with it.
inline Layout sampleLayout() {
    Layout l;
    l.colors = {{1, {230, 60, 60}}, {2, {60, 200, 80}}, {3, {60, 110, 230}}, {4, {240, 210, 60}}, {5, {170, 80, 210}}, {juraic::GARBAGE, {205, 205, 215}}};
    // 1, 6, 30, 180, 360 and 720 pieces: a mark of one colour each (the mark of 6 with a second point, as a larger
    // one of the colour of the mark of 1 would be).
    l.marks = {{1, {{0, 0, {150, 150, 160}}}},
               {6, {{0, 0, {150, 150, 160}}, {0, 14, {90, 90, 100}}}},
               {30, {{0, 0, {240, 140, 40}}}},
               {180, {{0, 0, {250, 240, 90}}}},
               {360, {{0, 0, {120, 220, 250}}}},
               {720, {{0, 0, {250, 100, 200}}}}};
    const float cell = 84, left[2] = {336, 1400}, nextX[2] = {912, 1328};
    for (int s = 0; s < 2; ++s) {
        SideLayout& d = l.side[s];
        d.left = left[s], d.bottom = 1176, d.cellWidth = d.cellHeight = cell, d.shownRows = 12.4f;
        d.next[0][1] = {nextX[s], 214}, d.next[0][0] = {nextX[s], 298};
        d.next[1][1] = {nextX[s] + (s ? -20.f : 20.f), 390}, d.next[1][0] = {nextX[s] + (s ? -20.f : 20.f), 460};
        for (int k = 0; k < 6; ++k) d.notice.push_back({left[s] + 42 + 84 * float(k), 90});
    }
    return l;
}

// The table as text: one item a line, a word and its numbers; # begins a remark.
//   base <width> <height>              patch <half side>        still <frames>
//   distance <colours> <marks>
//   color <piece> <r> <g> <b>          (piece: 0 background, 1 to 5 the colours, 6 garbage; one line a colour)
//   mark <pieces> <dx> <dy> <r> <g> <b> [<dx> <dy> <r> <g> <b> ...]
//   field <side> <left> <bottom> <cell width> <cell height> <rows shown>      (side: 1 or 2)
//   next <side> <1 next, 2 next-next> <axis x> <axis y> <child x> <child y>
//   notice <side> <x> <y>              (one line a place, from the first)
inline std::string formatLayout(const Layout& l) {
    std::ostringstream o;
    o << "base " << l.baseWidth << ' ' << l.baseHeight << "\npatch " << l.patch << "\nstill " << l.stillFrames << "\ndistance " << l.colorDistance << ' ' << l.markDistance << '\n';
    for (const ColorRef& c : l.colors) o << "color " << int(c.cell) << ' ' << c.rgb.r << ' ' << c.rgb.g << ' ' << c.rgb.b << '\n';
    for (const Mark& m : l.marks) {
        o << "mark " << m.value;
        for (const Probe& p : m.probes) o << ' ' << p.dx << ' ' << p.dy << ' ' << p.rgb.r << ' ' << p.rgb.g << ' ' << p.rgb.b;
        o << '\n';
    }
    for (int s = 0; s < 2; ++s) {
        const SideLayout& d = l.side[s];
        o << "field " << s + 1 << ' ' << d.left << ' ' << d.bottom << ' ' << d.cellWidth << ' ' << d.cellHeight << ' ' << d.shownRows << '\n';
        for (int k = 0; k < 2; ++k) o << "next " << s + 1 << ' ' << k + 1 << ' ' << d.next[k][0].x << ' ' << d.next[k][0].y << ' ' << d.next[k][1].x << ' ' << d.next[k][1].y << '\n';
        for (const Spot& p : d.notice) o << "notice " << s + 1 << ' ' << p.x << ' ' << p.y << '\n';
    }
    return o.str();
}

// Reads a table written as formatLayout writes it. False, with the line in `error`, when a line is not understood;
// the colours, the marks and the places of the notice are those of the text alone.
inline bool parseLayout(const std::string& text, Layout& out, std::string* error = nullptr) {
    Layout l;
    std::istringstream in(text);
    std::string line;
    int number = 0;
    const auto fail = [&](const std::string& why) {
        if (error) *error = "line " + std::to_string(number) + ": " + why;
        return false;
    };
    const auto side = [](int s) { return s == 1 || s == 2; };
    const auto rgbOk = [](const Rgb& c) { return c.r >= 0 && c.r < 256 && c.g >= 0 && c.g < 256 && c.b >= 0 && c.b < 256; };
    while (std::getline(in, line)) {
        ++number;
        if (const size_t hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        std::istringstream words(line);
        std::string word;
        if (!(words >> word)) continue;
        bool ok = true;
        if (word == "base") ok = bool(words >> l.baseWidth >> l.baseHeight) && l.baseWidth > 0 && l.baseHeight > 0;
        else if (word == "patch") ok = bool(words >> l.patch) && l.patch >= 0;
        else if (word == "still") ok = bool(words >> l.stillFrames) && l.stillFrames >= 1;
        else if (word == "distance") ok = bool(words >> l.colorDistance >> l.markDistance) && l.colorDistance >= 0 && l.markDistance >= 0;
        else if (word == "color") {
            int cell = 0;
            ColorRef c;
            ok = bool(words >> cell >> c.rgb.r >> c.rgb.g >> c.rgb.b) && cell >= 0 && cell <= juraic::GARBAGE && rgbOk(c.rgb);
            c.cell = juraic::Cell(cell);
            if (ok) l.colors.push_back(c);
        } else if (word == "mark") {
            Mark m;
            ok = bool(words >> m.value) && m.value > 0;
            Probe p;
            while (ok && words >> p.dx) {
                ok = bool(words >> p.dy >> p.rgb.r >> p.rgb.g >> p.rgb.b) && rgbOk(p.rgb);
                m.probes.push_back(p);
            }
            ok = ok && !m.probes.empty() && words.eof();
            if (ok) l.marks.push_back(m);
        } else if (word == "field") {
            int s = 0;
            SideLayout d;
            ok = bool(words >> s >> d.left >> d.bottom >> d.cellWidth >> d.cellHeight >> d.shownRows) && side(s) && d.cellWidth > 0 && d.cellHeight > 0;
            if (ok) {
                SideLayout& to = l.side[s - 1];
                to.left = d.left, to.bottom = d.bottom, to.cellWidth = d.cellWidth, to.cellHeight = d.cellHeight, to.shownRows = d.shownRows;
            }
        } else if (word == "next") {
            int s = 0, k = 0;
            Spot a, c;
            ok = bool(words >> s >> k >> a.x >> a.y >> c.x >> c.y) && side(s) && (k == 1 || k == 2);
            if (ok) l.side[s - 1].next[k - 1][0] = a, l.side[s - 1].next[k - 1][1] = c;
        } else if (word == "notice") {
            int s = 0;
            Spot p;
            ok = bool(words >> s >> p.x >> p.y) && side(s);
            if (ok) l.side[s - 1].notice.push_back(p);
        } else {
            return fail("unknown word \"" + word + "\"");
        }
        std::string rest;
        if (!ok || words >> rest) return fail("\"" + word + "\" with numbers that do not fit");
    }
    out = l;
    return true;
}

inline bool loadLayout(const std::filesystem::path& file, Layout& out, std::string* error = nullptr) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open the file";
        return false;
    }
    std::ostringstream text;
    text << in.rdbuf();
    return parseLayout(text.str(), out, error);
}

// ---- one picture --------------------------------------------------------------------------------------------------

// The table scaled to a picture.
struct Scale {
    float x = 1, y = 1;
    Scale() = default;
    Scale(const Picture& p, const Layout& l) : x(float(p.width) / float(l.baseWidth)), y(float(p.height) / float(l.baseHeight)) {}
};

// The mean colour of the square around (x, y), in base pixels.
inline Rgb sample(const Picture& p, const Layout& l, const Scale& s, float x, float y) {
    const auto clamp = [](int v, int high) { return v < 0 ? 0 : v > high ? high : v; };
    const float px = x * s.x, py = y * s.y, hx = l.patch * s.x, hy = l.patch * s.y;
    const int x0 = clamp(int(std::floor(px - hx)), p.width - 1), x1 = clamp(int(std::floor(px + hx)), p.width - 1);
    const int y0 = clamp(int(std::floor(py - hy)), p.height - 1), y1 = clamp(int(std::floor(py + hy)), p.height - 1);
    long r = 0, g = 0, b = 0, n = 0;
    for (int j = y0; j <= y1; ++j) {
        const uint8_t* row = p.pixels + size_t(j) * size_t(p.stride);
        for (int i = x0; i <= x1; ++i) b += row[4 * i], g += row[4 * i + 1], r += row[4 * i + 2], ++n;
    }
    return n ? Rgb{int(r / n), int(g / n), int(b / n)} : Rgb{};
}

inline int distance2(const Rgb& a, const Rgb& b) {
    return (a.r - b.r) * (a.r - b.r) + (a.g - b.g) * (a.g - b.g) + (a.b - b.b) * (a.b - b.b);
}

// The piece of the table nearest to `c`; EMPTY when none is near enough.
inline juraic::Cell classify(const Layout& l, const Rgb& c) {
    int best = l.colorDistance * l.colorDistance + 1;
    juraic::Cell cell = juraic::EMPTY;
    for (const ColorRef& ref : l.colors)
        if (const int d = distance2(c, ref.rgb); d < best) best = d, cell = ref.cell;
    return cell;
}

// The point `k` of column x (2y: the lower point of row y, 2y + 1: its upper point), in base pixels; how high it is in
// cells from the floor of the field.
inline float pointHeight(int k) { return float(k / 2) + (k % 2 ? 0.75f : 0.25f); }
inline Spot pointSpot(const SideLayout& d, int x, int k) {
    return {d.left + (float(x) + 0.5f) * d.cellWidth, d.bottom - pointHeight(k) * d.cellHeight};
}

struct SideRead {
    juraic::Cell point[juraic::W][POINTS]{}; // what each point of the field shows (HIDDEN: above what the screen shows)
    juraic::Pair next[2]{};                  // the window of the next pairs: next, next-next
    bool nextRead[2]{};                      // false: a piece of it is no colour of the table (the window is moving)
    int notice = 0;                          // garbage pieces the notice shows
};
struct Reading {
    SideRead side[2]; // by seat
};

inline int readNotice(const Picture& p, const Layout& l, const Scale& s, const SideLayout& d) {
    int total = 0;
    const int limit = l.markDistance * l.markDistance;
    for (const Spot& at : d.notice) {
        // (of the marks whose every point is near, the one of most points: one of them may be part of another)
        const Mark* found = nullptr;
        for (const Mark& m : l.marks) {
            bool near = true;
            for (const Probe& q : m.probes) near = near && distance2(sample(p, l, s, at.x + q.dx, at.y + q.dy), q.rgb) <= limit;
            if (near && (!found || m.probes.size() > found->probes.size())) found = &m;
        }
        if (found) total += found->value;
    }
    return total;
}

inline Reading read(const Picture& p, const Layout& l) {
    Reading r;
    const Scale s(p, l);
    for (int side = 0; side < 2; ++side) {
        const SideLayout& d = l.side[side];
        SideRead& out = r.side[side];
        for (int x = 0; x < juraic::W; ++x)
            for (int k = 0; k < POINTS; ++k) {
                const Spot at = pointSpot(d, x, k);
                out.point[x][k] = pointHeight(k) > d.shownRows ? HIDDEN : classify(l, sample(p, l, s, at.x, at.y));
            }
        for (int n = 0; n < 2; ++n) {
            const juraic::Cell axis = classify(l, sample(p, l, s, d.next[n][0].x, d.next[n][0].y));
            const juraic::Cell child = classify(l, sample(p, l, s, d.next[n][1].x, d.next[n][1].y));
            out.next[n] = {axis, child};
            out.nextRead[n] = axis >= 1 && axis <= juraic::MAX_COLORS && child >= 1 && child <= juraic::MAX_COLORS;
        }
        out.notice = readNotice(p, l, s, d);
    }
    return r;
}

// ---- what a reading shows ---------------------------------------------------------------------------------------

inline bool isPiece(juraic::Cell c) { return c != juraic::EMPTY && c != HIDDEN; }

// The pieces at rest: from the floor of each column up, the cells whose two points show the same piece (or whose upper
// point is above the screen). What is above the first cell that is not such a cell is not in it.
inline juraic::GameField restingField(const SideRead& s) {
    juraic::GameField g;
    for (int x = 0; x < juraic::W; ++x)
        for (int y = 0; y < juraic::H; ++y) {
            const juraic::Cell low = s.point[x][2 * y], high = s.point[x][2 * y + 1];
            if (!isPiece(low) || (high != low && high != HIDDEN)) break;
            g.f.set(x, y, low);
        }
    return g;
}

// Every piece the field shows, each once (by the lower point of each cell: a piece covers the lower point of one cell
// wherever it is), let fall to the floor: the field a chain that is running comes to, as far as one picture tells.
inline juraic::GameField fallenField(const SideRead& s) {
    juraic::GameField g;
    for (int x = 0; x < juraic::W; ++x)
        for (int y = 0; y < juraic::H; ++y)
            if (isPiece(s.point[x][2 * y])) g.f.set(x, y, s.point[x][2 * y]);
    g.f.drop();
    return g;
}

// The pair in play as one picture shows it.
struct PairSeen {
    bool seen = false;
    juraic::PairControl at; // the cell of the axis piece, upper, dir (the rest as in a pair that has just appeared)
    juraic::Pair colors{};  // the child EMPTY while it is above the screen
    bool childHidden = false;
};

// The cells the two pieces of a pair cover, the axis first (y * 2 + upper: in half cells).
inline void pairHalves(const juraic::PairControl& p, int& axisX, int& axisHalf, int& childX, int& childHalf) {
    axisX = p.x, childX = p.childX(), axisHalf = 2 * p.y + (p.upper ? 1 : 0), childHalf = 2 * p.childY() + (p.upper ? 1 : 0);
}

// The pair over `under`, the field it moves over: the pieces above it, two of them side by side or one on the other,
// or one with the place above it beyond the screen (the other piece there). `expected`: its colours when they are known
// (axis EMPTY: not known). `previous`: where it was seen last, for a pair whose two pieces have one colour (its axis is
// taken to be where the axis was). Not seen: other pieces than the pair's, or pieces between two places (a pair
// turning).
inline PairSeen readPair(const SideRead& s, const juraic::GameField& under, juraic::Pair expected = {}, const PairSeen* previous = nullptr) {
    struct Piece {
        int x, bottom; // bottom: the lowest point it covers
        juraic::Cell cell;
    };
    Piece piece[2];
    int count = 0;
    int h[juraic::W];
    under.heights(h);
    for (int x = 0; x < juraic::W; ++x) {
        for (int k = 2 * h[x]; k < POINTS;) {
            const juraic::Cell c = s.point[x][k];
            if (c == HIDDEN) break;
            if (c == juraic::EMPTY) {
                ++k;
                continue;
            }
            int end = k;
            while (end < POINTS && s.point[x][end] == c) ++end;
            const bool cut = end == POINTS || s.point[x][end] == HIDDEN; // (the last piece may go on above the screen)
            if ((end - k) % 2 && !cut) return {};
            for (int b = k; b < end; b += 2) {
                if (count == 2) return {};
                piece[count++] = {x, b, c};
            }
            k = end;
        }
    }
    const auto hiddenAt = [&](int x, int k) { return k >= POINTS || s.point[x][k] == HIDDEN; };
    // The two ways of reading the pieces: the axis first (UP or RIGHT) or the axis second (DOWN or LEFT).
    Piece first, second;
    bool vertical;
    bool hiddenChild = false;
    if (count == 2) {
        const Piece& a = piece[0];
        const Piece& b = piece[1];
        if (a.x == b.x && b.bottom == a.bottom + 2) vertical = true, first = a, second = b;
        else if (a.x + 1 == b.x && a.bottom == b.bottom) vertical = false, first = a, second = b;
        else return {};
    } else if (count == 1 && hiddenAt(piece[0].x, piece[0].bottom + 2)) {
        // (the other piece is above it, beyond the screen)
        vertical = true, first = piece[0], second = {piece[0].x, piece[0].bottom + 2, juraic::EMPTY}, hiddenChild = true;
    } else {
        return {};
    }
    const auto fits = [&](const Piece& axis, const Piece& child) {
        if (!expected.axis) return true;
        return axis.cell == expected.axis && (child.cell == juraic::EMPTY || !expected.child || child.cell == expected.child);
    };
    bool axisFirst = fits(first, second), axisSecond = !hiddenChild && fits(second, first);
    if (!axisFirst && !axisSecond) return {};
    if (axisFirst && axisSecond && previous && previous->seen) {
        // (one colour: the axis where it was, or nearest to it)
        int ax, ah, cx, ch;
        pairHalves(previous->at, ax, ah, cx, ch);
        const auto away = [&](const Piece& p) { return std::abs(p.x - ax) * 2 + std::abs(p.bottom - ah); };
        if (away(second) < away(first)) axisFirst = false;
    }
    const Piece& axis = axisFirst ? first : second;
    const Piece& child = axisFirst ? second : first;
    PairSeen r;
    r.seen = true;
    r.at.x = int8_t(axis.x), r.at.y = int8_t(axis.bottom / 2), r.at.upper = axis.bottom % 2 == 1;
    r.at.dir = vertical ? (axisFirst ? juraic::UP : juraic::DOWN) : (axisFirst ? juraic::RIGHT : juraic::LEFT);
    r.colors = {axis.cell, child.cell};
    r.childHidden = hiddenChild;
    return r;
}

// Is the pair lying on the pieces of `under` (one of its pieces cannot go down half a cell)? A pair lying there may
// already be locked: one picture does not tell.
inline bool pairLying(const PairSeen& p, const juraic::GameField& under) {
    if (!p.seen || p.at.upper) return false;
    int h[juraic::W];
    under.heights(h);
    int ax, ah, cx, ch;
    pairHalves(p.at, ax, ah, cx, ch);
    return ah == 2 * h[ax] || ch == 2 * h[cx];
}

// ---- frame after frame ------------------------------------------------------------------------------------------

struct SideTrack {
    juraic::GameField resting; // the pieces at rest as last read
    int64_t changedAt = 0;     // the frame in which they were last read other than before
    int loose = 0;             // points that show pieces above them, other than those of the pair seen
    // The pair in play: from the frame it was seen where pairs appear until the field it appeared over changes or the
    // next pair appears. In the air it may be between two places (turning) and not seen in a frame.
    bool inHand = false;
    juraic::GameField under;   // the field it appeared over
    juraic::Pair colors{};     // its colours (the child EMPTY while not known)
    PairSeen pair;             // as seen in the last reading (seen false: not seen there)
    PairSeen lastSeen;         // as seen last
    int pairs = 0;             // pairs this side has been given since the round began
    int64_t spawnedAt = -1;    // the frame in which the last of them was seen first
    int notice = 0;
    juraic::Pair next[2]{};    // the window of the next pairs as last read
    bool nextRead[2]{};
};

class Tracker {
public:
    juraic::Timing timing = juraic::DEFAULT_TIMING;
    int stillFrames = 2;
    // Both fields empty, with no pair, for this many frames: a new round (the screen before a round shows them so).
    int roundGap = 30;
    SideTrack side[2];
    // The pairs the screen has shown, numbered as the players draw them (both draw from one tsumo).
    std::vector<juraic::Pair> pairs;
    std::vector<uint8_t> known;
    Reading last;             // the last reading
    int64_t frame = -1;       // ... and its frame
    int64_t roundStart = -1;  // the frame that is frame 0 of the round (-1: not known yet)
    bool fromStart = false;   // the round has been followed from its beginning (both fields seen empty before it)
    bool spawned[2] = {};     // in the last reading a new pair was seen on that side
    int doubts = 0;           // readings that did not fit what had been read before

    void reset() {
        const juraic::Timing t = timing;
        const int still = stillFrames, gap = roundGap;
        *this = Tracker{};
        timing = t, stillFrames = still, roundGap = gap;
    }

    bool still(int s) const { return frame - side[s].changedAt >= stillFrames && side[s].loose == 0; }
    bool knows(int index) const { return index >= 0 && index < int(known.size()) && known[size_t(index)]; }

    // One picture, of the frame `at` (frames counted by the clock: a frame that was not taken is a number left out).
    void step(const Reading& r, int64_t at) {
        last = r;
        frame = at;
        spawned[0] = spawned[1] = false;
        bool empty = true;
        for (int s = 0; s < 2; ++s) {
            const SideRead& read = r.side[s];
            SideTrack& t = side[s];
            const juraic::GameField resting = restingField(read);
            if (!(resting == t.resting)) t.resting = resting, t.changedAt = at;
            t.notice = read.notice;
            for (int n = 0; n < 2; ++n) t.next[n] = read.next[n], t.nextRead[n] = read.nextRead[n];
            t.pair = {};
            if (t.inHand) {
                // (gone: the field under it changed - a chain, or a field read wrong -, or something came to rest and
                //  the pair is not seen - it split, or it turns on the ground)
                t.pair = contains(resting, t.under) ? readPair(read, t.under, t.colors, &t.lastSeen) : PairSeen{};
                if (t.pair.seen) {
                    t.lastSeen = t.pair;
                    if (t.pair.childHidden) t.pair.colors.child = t.colors.child;
                    else t.colors.child = t.pair.colors.child;
                } else if (!(resting == t.under)) {
                    t.inHand = false;
                }
            }
            if (!t.pair.seen) {
                // A new pair where pairs appear, on a field that stood still before it (pieces of a chain falling past
                // the place are no pair).
                const juraic::Pair coming = knows(t.pairs) ? pairs[size_t(t.pairs)] : juraic::Pair{};
                const PairSeen fresh = readPair(read, resting, coming);
                const juraic::PairControl spawn = juraic::spawnPair(timing);
                const bool there = fresh.seen && fresh.at.x == spawn.x && fresh.at.y == spawn.y && fresh.at.upper == spawn.upper && fresh.at.dir == spawn.dir;
                if (there && at - t.changedAt >= stillFrames) newPair(s, fresh, resting, read, at);
            }
            t.loose = looseOf(read, t);
            empty = empty && resting.f.empty() && t.loose == 0 && !t.inHand;
        }
        if (!empty) emptySince = -1;
        else if (emptySince < 0) emptySince = at;
        if (empty && at - emptySince >= roundGap && (side[0].pairs || side[1].pairs || !fromStart)) newRound();
        // Before the first pair of a round: the window shows the first two pairs.
        if (fromStart && !side[0].pairs && !side[1].pairs && !knows(0) && side[0].nextRead[0] && side[0].nextRead[1]) {
            if (side[1].nextRead[0] && side[1].nextRead[1] && !(same(side[0].next[0], side[1].next[0]) && same(side[0].next[1], side[1].next[1]))) ++doubts;
            else learn(0, side[0].next[0]), learn(1, side[0].next[1]);
        }
    }

private:
    int64_t emptySince = -1;

    static bool same(juraic::Pair a, juraic::Pair b) { return a.axis == b.axis && a.child == b.child; }
    // Every piece of `part` is in `whole` (the pieces of a pair lying on it, or garbage, may be above them).
    static bool contains(const juraic::GameField& whole, const juraic::GameField& part) {
        int h[juraic::W];
        part.heights(h);
        for (int x = 0; x < juraic::W; ++x)
            for (int y = 0; y < h[x]; ++y)
                if (whole.f.get(x, y) != part.f.get(x, y)) return false;
        return true;
    }
    // Points that show a piece above the field the pair moves over (or above the pieces at rest), the pair's own left out.
    static int looseOf(const SideRead& read, const SideTrack& t) {
        int ax = -1, ah = -1, cx = -1, ch = -1;
        if (t.pair.seen) pairHalves(t.pair.at, ax, ah, cx, ch);
        const auto own = [&](int x, int k) { return (x == ax && (k == ah || k == ah + 1)) || (x == cx && (k == ch || k == ch + 1)); };
        int h[juraic::W], n = 0;
        (t.inHand ? t.under : t.resting).heights(h);
        for (int x = 0; x < juraic::W; ++x)
            for (int k = 2 * h[x]; k < POINTS; ++k) n += isPiece(read.point[x][k]) && !own(x, k);
        return n;
    }
    void learn(int index, juraic::Pair p) {
        if (index < 0) return;
        if (int(known.size()) <= index) pairs.resize(size_t(index) + 1), known.resize(size_t(index) + 1, 0);
        if (known[size_t(index)] && !same(pairs[size_t(index)], p)) ++doubts;
        pairs[size_t(index)] = p, known[size_t(index)] = 1;
    }
    void newRound() {
        for (SideTrack& t : side) t.pairs = 0, t.spawnedAt = -1, t.inHand = false;
        pairs.clear(), known.clear();
        roundStart = -1;
        fromStart = true;
    }
    void newPair(int s, const PairSeen& fresh, const juraic::GameField& resting, const SideRead& read, int64_t at) {
        SideTrack& t = side[s];
        const int index = t.pairs++;
        t.spawnedAt = at;
        t.inHand = true;
        t.under = resting;
        t.pair = t.lastSeen = fresh;
        t.colors = fresh.colors;
        if (knows(index)) {
            if (fresh.colors.axis != pairs[size_t(index)].axis) ++doubts;
            t.colors = t.pair.colors = t.lastSeen.colors = pairs[size_t(index)];
        }
        // (the window has moved on: the two pairs after this one)
        for (int n = 0; n < 2; ++n)
            if (read.nextRead[n]) learn(index + 1 + n, read.next[n]);
        spawned[s] = true;
        // The first pair of a round is seen in its frame firstSpawn (game/player.h startPlayer).
        if (index == 0 && roundStart < 0) roundStart = at - timing.firstSpawn;
    }
};

// What the AI of seat `me` is shown in the frame in which its new pair was seen (Tracker::spawned[me]): the own side
// as it is then in the game (phase Next, one frame left), the other side as far as the picture tells, and the pairs
// the screen has shown. False when no pair of `me` was seen in the last reading. The colours in play are those seen
// so far, then the others (Rules::palette); the frame is counted from the frame of the round in which the first pair
// was seen (Tracker::roundStart).
//
// The other side (a guess from one picture and the frames counted since its pieces last changed): a pair in the air is
// in play (Control) where it was seen last; a pair lying on the pieces may be locked already and is taken as locked;
// pieces that have stood still since they changed are landing (Land) for Timing::landFrames, then wait for the next
// pair (Next); pieces that move are a chain that runs (Land, one frame, over every piece the picture shows let fall).
// The garbage above a field waits for a chain when the other side moves, and falls after the next move when it stands
// still.
inline bool observeSpawn(const Tracker& t, int me, const juraic::Rules& rules, juraic::ai::Observation& o) {
    using namespace juraic;
    if (me < 0 || me > 1 || !t.spawned[me]) return false;
    o = ai::Observation{};
    o.rules = rules;
    o.timing = t.timing;
    o.players = 2;
    o.me = me;
    o.frame = t.roundStart >= 0 && t.frame >= t.roundStart ? t.frame - t.roundStart : 0;
    o.state = RoundState::Playing;
    // The colours in play.
    bool seen[8]{};
    for (size_t i = 0; i < t.pairs.size(); ++i)
        if (t.known[i]) seen[t.pairs[i].axis] = seen[t.pairs[i].child] = true;
    for (const SideTrack& side : t.side)
        for (int x = 0; x < W; ++x)
            for (int y = 0; y < H; ++y) seen[side.resting.f.get(x, y)] = true;
    int n = 0;
    for (Cell c = 1; c <= MAX_COLORS; ++c)
        if (seen[c]) o.rules.palette[size_t(n++)] = c;
    for (Cell c = 1; c <= MAX_COLORS; ++c)
        if (!seen[c]) o.rules.palette[size_t(n++)] = c;
    for (int s = 0; s < 2; ++s) {
        const SideTrack& side = t.side[s];
        Player& p = o.player[s];
        (t.still(1 - s) ? p.incoming.confirmed : p.incoming.pending) = side.notice;
        p.nextPair = p.moves = side.pairs;
        if (s == me) {
            p.field = side.under;
            p.phase = Phase::Next;
            p.wait = 1;
            p.nextPair = p.moves = side.pairs - 1;
        } else if (side.inHand && !pairLying(side.lastSeen, side.under)) {
            p.field = side.under;
            p.phase = Phase::Control;
            p.pair = side.lastSeen.at;
            p.colors = side.colors;
            p.moves = side.pairs - 1;
        } else if (side.inHand || t.still(s)) {
            p.field = side.resting;
            const int since = int(t.frame - side.changedAt);
            p.phase = since < t.timing.landFrames ? Phase::Land : Phase::Next;
            p.wait = since < t.timing.landFrames ? t.timing.landFrames - since : std::max(1, t.timing.landFrames + t.timing.nextFrames - since);
        } else {
            p.field = fallenField(t.last.side[s]);
            p.phase = Phase::Land;
            p.wait = 1;
        }
    }
    // The pairs shown (one not known, of a round not followed from its beginning: of the first colour in play). The
    // pair of `me` and the two after it are on the screen (Timing::nextShown).
    const size_t count = std::max(t.pairs.size(), size_t(o.player[me].nextPair + 3));
    o.tsumo.pairs.assign(count, Pair{o.rules.palette[0], o.rules.palette[0]});
    for (size_t i = 0; i < t.pairs.size(); ++i)
        if (t.known[i]) o.tsumo.pairs[i] = t.pairs[i];
    return true;
}

// ---- pictures to look at ------------------------------------------------------------------------------------------

// Writes a picture as a file of 32 bits a pixel (BMP, rows from the top).
inline bool saveBmp(const std::filesystem::path& file, const Picture& p) {
    std::ofstream out(file, std::ios::binary);
    if (!out || p.width <= 0 || p.height <= 0) return false;
    const uint32_t bytes = uint32_t(p.width) * uint32_t(p.height) * 4;
    const auto u16 = [&](uint32_t v) { out.put(char(v & 255)), out.put(char(v >> 8 & 255)); };
    const auto u32 = [&](uint32_t v) { u16(v & 0xFFFF), u16(v >> 16); };
    out.put('B'), out.put('M');
    u32(54 + bytes), u32(0), u32(54);
    u32(40), u32(uint32_t(p.width)), u32(uint32_t(-p.height)), u16(1), u16(32), u32(0), u32(bytes), u32(2835), u32(2835), u32(0), u32(0);
    for (int y = 0; y < p.height; ++y) out.write(reinterpret_cast<const char*>(p.pixels + size_t(y) * size_t(p.stride)), std::streamsize(p.width) * 4);
    return bool(out);
}

// Reads a BMP of 24 or 32 bits a pixel without compression into four bytes a pixel, rows from the top.
inline bool loadBmp(const std::filesystem::path& file, std::vector<uint8_t>& pixels, int& width, int& height) {
    std::ifstream in(file, std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto u16 = [&](size_t at) { return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8; };
    const auto u32 = [&](size_t at) { return u16(at) | u16(at + 2) << 16; };
    if (d.size() < 54 || d[0] != 'B' || d[1] != 'M') return false;
    const uint32_t offset = u32(10), bits = u16(28), compression = u32(30);
    const int32_t w = int32_t(u32(18)), h = int32_t(u32(22));
    if ((bits != 24 && bits != 32) || (compression != 0 && !(compression == 3 && bits == 32)) || w <= 0 || h == 0) return false;
    const bool down = h < 0; // rows from the top
    width = w, height = down ? -h : h;
    const size_t step = (size_t(w) * (bits / 8) + 3) / 4 * 4;
    if (d.size() < offset + step * size_t(height)) return false;
    pixels.assign(size_t(width) * size_t(height) * 4, 255);
    for (int y = 0; y < height; ++y) {
        const uint8_t* row = d.data() + offset + step * size_t(down ? y : height - 1 - y);
        for (int x = 0; x < width; ++x)
            for (int c = 0; c < 3; ++c) pixels[(size_t(y) * size_t(width) + size_t(x)) * 4 + size_t(c)] = row[size_t(x) * (bits / 8) + size_t(c)];
    }
    return true;
}

} // namespace play::realgame
