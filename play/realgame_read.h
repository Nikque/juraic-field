#pragma once
// The mode in which Juraic plays 2P of the real game (play/realgame_win.h): what a picture of the real game's window
// shows, read without help from the system. The picture is taken by play/realgame_capture_win.h (the Windows build);
// everything here is plain C++ and is tested on every system (tests/realgame_tests.cpp).
//
// Where things are on the screen and what colours they have is a table (Layout), in the pixels of a window whose
// inside is BASE_WIDTH x BASE_HEIGHT; a picture of another size is read with the table scaled to it. The table can be
// written to a text file and read from one (formatLayout, parseLayout), so that it can be made from pictures of the
// real game without building the program again (tools/realgame_read.cpp reads a saved picture with a table). The
// table built in (measuredLayout) was measured on five pictures of the real game's offline versus.
//
// A field is read at two points of every cell: a quarter of a cell above its lower edge and a quarter of a cell
// below its upper edge. A piece at rest covers both points of its cell; the pair in play, which moves in half cells
// (game/control.h), covers the upper point of one cell and the lower point of the next when it is half a cell up.
// A point is the square around it: its pixels of strong colour (pieces have faces and shines of white, outlines of
// dark) tell the colour of a piece, by the colour of the table nearest to their mean, when with the pixels of pale
// grey or white they cover enough of it (a small dot of colour is no piece); with few of them, many pixels of pale
// grey are garbage; the background of the field (dark) is neither. A point that shows something bright that is none of
// these (the lights, the smoke and the words the screen draws over a field) is UNSURE, and a reading with such points
// is not taken by Tracker for the pieces of that side.
//
// Tracker follows the readings frame by frame: the pieces at rest and whether they stand still, the pair in play,
// the pairs each side has been given and the pairs the windows of the next pairs have shown. observeSpawn makes of
// it what the AI is shown (ai/observe.h) in the frame in which a new pair of one side is seen on its field, one frame
// before it can be moved (Timing::nextShown).
#include "../ai/observe.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <deque>
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
constexpr juraic::Cell UNSURE = 8;                   // a point that shows something bright that is neither a piece nor the field

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

// A colour of the table and the piece it stands for (EMPTY for a colour that is no piece).
struct ColorRef {
    juraic::Cell cell = juraic::EMPTY;
    Rgb rgb;
    bool operator==(const ColorRef&) const = default;
};
// A cell of the field where the screen draws a mark of its own, of the colours `colors` (it turns and its light
// changes). A point there whose colour is nearer to one of them than to every piece is the mark, not a piece, unless
// `pale` in a hundred of its pixels are pale grey or white (the shine and the face of a piece; the mark has none).
struct MarkedCell {
    int x = 0, y = 0;
    int pale = 0;
    std::vector<Rgb> colors;
    bool operator==(const MarkedCell&) const = default;
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
    std::vector<MarkedCell> marked;
    bool operator==(const SideLayout&) const = default;
};

struct Layout {
    int baseWidth = BASE_WIDTH, baseHeight = BASE_HEIGHT;
    float patch = 4;              // a point is the square this many base pixels from it each way
    float markPatch = 4;          // ... a point of a mark of the notice
    int colorDistance = 60;       // a point within this distance (red, green, blue) of a colour of the table is that colour
    int faceHue = 0;              // ... or, near none and with the warm white of a piece's face, the colour nearest in hue
                                  // within this many degrees when the other point of its cell is a piece of it (0: not used)
    std::vector<ColorRef> colors; // (the nearest counts; a point near none of them is empty)
    // A point is a piece of a colour when `colored` in a hundred of its pixels have strong colour and `cover` in a hundred
    // have strong colour or are pale grey; with fewer of strong colour, garbage when `garbage` in a hundred are pale grey;
    // otherwise UNSURE when `unsure` in a hundred are bright (at least 45 in a hundred as bright as white), else nothing.
    int colored = 8, cover = 45, garbage = 30, unsure = 30;
    // A point of a window that reads a piece is that piece when this many in a hundred of its pixels of strong colour are
    // nearer that piece's colour than every other of the table and of `ground`, the colours of the windows behind their
    // pieces (either window may have either); else UNSURE (0: not used).
    int windowShare = 0;
    std::vector<Rgb> ground;
    int markDistance = 60;        // the same for the points of a mark
    std::vector<Mark> marks;
    int stillFrames = 2;          // pieces at rest read the same for this many frames stand still
    SideLayout side[2];           // by seat: 0 the 1P side, 1 the 2P side
    bool operator==(const Layout&) const = default;
};

// ---- the table ------------------------------------------------------------------------------------------------

// A table of plain colours and places, for pictures drawn by the tests.
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

// The table of the real game, measured on eleven pictures of its offline versus taken by its store's own key for
// pictures (saved at 2272 x 1278, measured at 2000 x 1125; the inside of the window was 2240 x 1260, the same shape:
// the pictures are taken as the window shows them, scaled). Here in the pixels of a picture of 2240 x 1260, 1.12 times
// those of the pictures measured:
//  - the fields: the dark inside 412 x 749 pixels of the pictures; the pieces stand in columns 67 apart (the first
//    5 pixels from its left edge) and rows 62.5 apart from its lower edge, twelve rows; the 13th row is behind the
//    frame above it;
//  - the colours: the mean of the pixels of strong colour of pieces at rest (red 158 23 24, green 54 185 37, blue 36
//    122 216, yellow 233 143 21, purple 130 50 188; at the upper point, where the eyes are, darker: blue 22 57 182);
//    on another stage yellow was lighter (242 194 34 to 246 216 51), a second colour of it;
//  - at a point of a piece 12 to 97 in a hundred of the pixels had strong colour (fewest at the upper point, where
//    the eyes are) and 51 or more strong colour or pale grey; at a point of the background none of either; at a point
//    of the small dots that show where the pair would land (red) 17 to 28 strong colour and 35 at most of both; at a
//    point of garbage none of strong colour and 37 to 93 pale grey (37 and 45 where it was dim), its mean grey (red
//    and blue alike); the upper point of a yellow piece half a cell up, its white face, 2 strong colour and 76 pale,
//    the pale warm (its red 14 above its blue): a point of few pixels of strong colour whose pale ones are warm is a
//    piece of the colour of those few;
//  - the mark of the third column of the twelfth row (where the pair appears) is red, its mean 162 45 45 to 227 64 64
//    as it turns and the light changes, and once orange (194 152 63 and 204 139 68), without pale grey; the pair as it
//    appears there shines (36 and 37 in a hundred pale grey), the lower edge of a piece of the pair half a cell up
//    there had none;
//  - the windows of the next pairs: the middles of the pieces, a little below them (their eyes are above);
//  - the notice: its marks in a row above the field, over the columns, their middles 135 pixels from the top. Four
//    kinds were in the pictures, each told by the mean colour of small squares (3.5 pixels each way) at points around
//    the middle of its place: a small ball (1 piece; white 8 pixels above the middle, where every other mark is
//    darker), a large one (6; white 20 pixels above, a dark grey eye 16 to the right), a red one (30; red at the eye,
//    pink 20 above), a star (180; yellow 10 below, red at the eye), a moon (360; orange 10 below, pale orange and pink
//    26 to the sides) and a crown (720; red at the eye, orange below and to the right). (Under a light of the screen
//    over the field the marks change their colours and are not read.)
inline Layout measuredLayout() {
    Layout l;
    l.patch = 15.7f, l.markPatch = 3.9f; // (14 and 3.5 pixels of the pictures)
    l.markDistance = 45;
    // (the points of the marks, in pixels of the pictures from the middle of the place, as they were measured)
    const auto mark = [](int value, std::vector<Probe> probes) {
        for (Probe& q : probes) q.dx *= 1.12f, q.dy *= 1.12f;
        return Mark{value, probes};
    };
    l.marks = {mark(1, {{0, -8, {249, 249, 249}}}),
               mark(6, {{0, -20, {253, 253, 253}}, {16, 0, {110, 110, 119}}}),
               mark(30, {{16, 0, {203, 15, 15}}, {0, -20, {246, 187, 175}}}),
               mark(180, {{0, 10, {224, 153, 27}}, {16, 0, {168, 47, 29}}}),
               mark(360, {{0, 10, {228, 136, 14}}, {26, -6, {224, 176, 141}}, {-26, -6, {214, 89, 92}}}),
               mark(720, {{16, 0, {220, 34, 34}}, {0, 10, {212, 127, 53}}, {26, -6, {219, 125, 45}}})};
    l.colorDistance = 80;
    // (the eyes of a face of joined red pieces under the shine: mean 219 67 64 and 237 77 74, 85 and 108 from the red
    //  of the table, 88 and 85 from its yellow; in hue 1 degree, the red 0, the yellows 35 and 47)
    l.faceHue = 12;
    l.colored = 8, l.cover = 45, l.garbage = 30;
    l.colors = {{1, {158, 23, 24}}, {2, {54, 185, 37}}, {3, {36, 122, 216}}, {4, {233, 143, 21}}, {4, {245, 200, 40}}, {5, {130, 50, 188}}};
    // (in the windows of the real game a piece at rest under no light has 66 in a hundred or more of its pixels of strong
    //  colour nearest its own colour, one under a light 32; the blue ground, 48 from the blue of the table, has none)
    l.windowShare = 30;
    l.ground = {{61, 164, 215}, {235, 108, 108}};
    const float left[2] = {324.2f, 1461.6f}, nextX[2][2] = {{875.8f, 927.4f}, {1361.9f, 1308.2f}};
    for (int s = 0; s < 2; ++s) {
        SideLayout& d = l.side[s];
        d.left = left[s], d.bottom = 1024.8f, d.cellWidth = 75, d.cellHeight = 70, d.shownRows = 12;
        d.next[0][1] = {nextX[s][0], 236.3f}, d.next[0][0] = {nextX[s][0], 305.8f};
        d.next[1][1] = {nextX[s][1], 380.8f}, d.next[1][0] = {nextX[s][1], 436.8f};
        for (int x = 0; x < juraic::W; ++x) d.notice.push_back({left[s] + (float(x) + 0.5f) * d.cellWidth, 151.2f});
        d.marked = {{juraic::DEATH_X, juraic::DEATH_Y, 8, {{165, 45, 45}, {190, 55, 48}, {215, 60, 56}, {227, 64, 64}, {200, 145, 66}}}};
    }
    return l;
}

// The table as text: one item a line, a word and its numbers; # begins a remark.
//   base <width> <height>              patch <half side>        markpatch <half side>        still <frames>
//   distance <colours> <marks> [<face hue>]   share <strong colour> <cover> <garbage> <unsure>   (in a hundred pixels of a point)
//   windowshare <in a hundred pixels of strong colour of a point of a window>
//   ground <r> <g> <b>                 (one line a colour)
//   color <piece> <r> <g> <b>          (piece: 0 no piece, 1 to 5 the colours, 6 garbage; one line a colour)
//   mark <pieces> <dx> <dy> <r> <g> <b> [<dx> <dy> <r> <g> <b> ...]
//   field <side> <left> <bottom> <cell width> <cell height> <rows shown>      (side: 1 or 2)
//   next <side> <1 next, 2 next-next> <axis x> <axis y> <child x> <child y>
//   notice <side> <x> <y>              (one line a place, from the first)
//   marked <side> <column> <row> <pale grey> <r> <g> <b> [<r> <g> <b> ...]   (columns 1 to 6, rows 1 to 13 from the floor)
inline std::string formatLayout(const Layout& l) {
    std::ostringstream o;
    o << "base " << l.baseWidth << ' ' << l.baseHeight << "\npatch " << l.patch << "\nmarkpatch " << l.markPatch << "\nstill " << l.stillFrames << "\ndistance " << l.colorDistance << ' ' << l.markDistance << ' ' << l.faceHue
      << "\nshare " << l.colored << ' ' << l.cover << ' ' << l.garbage << ' ' << l.unsure << "\nwindowshare " << l.windowShare << '\n';
    for (const Rgb& c : l.ground) o << "ground " << c.r << ' ' << c.g << ' ' << c.b << '\n';
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
        for (const MarkedCell& m : d.marked) {
            o << "marked " << s + 1 << ' ' << m.x + 1 << ' ' << m.y + 1 << ' ' << m.pale;
            for (const Rgb& c : m.colors) o << ' ' << c.r << ' ' << c.g << ' ' << c.b;
            o << '\n';
        }
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
        else if (word == "markpatch") ok = bool(words >> l.markPatch) && l.markPatch >= 0;
        else if (word == "still") ok = bool(words >> l.stillFrames) && l.stillFrames >= 1;
        else if (word == "distance") {
            ok = bool(words >> l.colorDistance >> l.markDistance) && l.colorDistance >= 0 && l.markDistance >= 0;
            if (ok && !(words >> std::ws).eof()) ok = bool(words >> l.faceHue) && l.faceHue >= 0 && l.faceHue <= 180;
        }
        else if (word == "share") {
            ok = bool(words >> l.colored >> l.cover >> l.garbage >> l.unsure);
            for (int v : {l.colored, l.cover, l.garbage, l.unsure}) ok = ok && v >= 0 && v <= 100;
        }
        else if (word == "windowshare") ok = bool(words >> l.windowShare) && l.windowShare >= 0 && l.windowShare <= 100;
        else if (word == "ground") {
            Rgb c;
            ok = bool(words >> c.r >> c.g >> c.b) && rgbOk(c);
            if (ok) l.ground.push_back(c);
        }
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
        } else if (word == "marked") {
            int s = 0;
            MarkedCell m;
            ok = bool(words >> s >> m.x >> m.y >> m.pale) && side(s) && m.x >= 1 && m.x <= juraic::W && m.y >= 1 && m.y <= juraic::H && m.pale >= 0 && m.pale <= 100;
            Rgb c;
            while (ok && words >> c.r) {
                ok = bool(words >> c.g >> c.b) && rgbOk(c);
                m.colors.push_back(c);
            }
            ok = ok && !m.colors.empty() && words.eof();
            if (ok) --m.x, --m.y, l.side[s - 1].marked.push_back(m);
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

// The pixels of the square `half` base pixels each way around (x, y), in base pixels: columns x0 to x1 and rows y0 to
// y1 of the picture, cut at its edges.
struct Patch {
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    Patch(const Picture& p, const Scale& s, float x, float y, float half) {
        const auto clamp = [](int v, int high) { return v < 0 ? 0 : v > high ? high : v; };
        const float px = x * s.x, py = y * s.y, hx = half * s.x, hy = half * s.y;
        x0 = clamp(int(std::floor(px - hx)), p.width - 1), x1 = clamp(int(std::floor(px + hx)), p.width - 1);
        y0 = clamp(int(std::floor(py - hy)), p.height - 1), y1 = clamp(int(std::floor(py + hy)), p.height - 1);
    }
};

// The mean colour of the square `half` base pixels each way around (x, y), in base pixels.
inline Rgb sample(const Picture& p, const Scale& s, float x, float y, float half) {
    const Patch q(p, s, x, y, half);
    long r = 0, g = 0, b = 0, n = 0;
    for (int j = q.y0; j <= q.y1; ++j) {
        const uint8_t* row = p.pixels + size_t(j) * size_t(p.stride);
        for (int i = q.x0; i <= q.x1; ++i) b += row[4 * i], g += row[4 * i + 1], r += row[4 * i + 2], ++n;
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

// The hue of `c` in degrees, 0 to 360 (0 for a grey).
inline float hueOf(const Rgb& c) {
    const int high = std::max(c.r, std::max(c.g, c.b)), low = std::min(c.r, std::min(c.g, c.b));
    if (high == low) return 0;
    const float d = float(high - low);
    const float h = 60 * (high == c.r ? float(c.g - c.b) / d : high == c.g ? 2 + float(c.b - c.r) / d : 4 + float(c.r - c.g) / d);
    return h < 0 ? h + 360 : h;
}

// The piece of the table nearest to `c` in hue, within `degrees`; EMPTY when none is.
inline juraic::Cell classifyHue(const Layout& l, const Rgb& c, int degrees) {
    const float h = hueOf(c);
    float best = float(degrees);
    juraic::Cell cell = juraic::EMPTY;
    for (const ColorRef& ref : l.colors) {
        const float apart = std::fabs(h - hueOf(ref.rgb)), d = std::min(apart, 360 - apart);
        if (d <= best) best = d, cell = ref.cell;
    }
    return cell;
}

// Strong colour: at least 45 in a hundred as bright as white and its weakest part at most half its strongest.
inline bool strongColour(int high, int low) { return 2 * (high - low) > high && 100 * high > 45 * 255; }

// What the square around (x, y) shows (in base pixels): a piece of a colour, garbage, nothing or UNSURE. Pale grey: at
// least 40 in a hundred as bright as white and its weakest part at least three quarters of its strongest. `mark`: the mark of the
// screen in that cell, if any. `face`: for an UNSURE point with the warm white of a piece's face, the colour of the
// table nearest in hue within Layout::faceHue (EMPTY for none), for read to take when the other point of the cell is
// a piece of that colour.
inline juraic::Cell piece(const Picture& p, const Layout& l, const Scale& s, float x, float y, const MarkedCell* mark = nullptr, juraic::Cell* face = nullptr) {
    const Patch q(p, s, x, y, l.patch);
    long r = 0, g = 0, b = 0, strong = 0, light = 0, all = 0, pr = 0, pb = 0, bright = 0;
    for (int j = q.y0; j <= q.y1; ++j) {
        const uint8_t* row = p.pixels + size_t(j) * size_t(p.stride);
        for (int i = q.x0; i <= q.x1; ++i) {
            const int cb = row[4 * i], cg = row[4 * i + 1], cr = row[4 * i + 2];
            const int high = std::max(cr, std::max(cg, cb)), low = std::min(cr, std::min(cg, cb));
            ++all;
            if (100 * high > 45 * 255) ++bright;
            if (strongColour(high, low)) r += cr, g += cg, b += cb, ++strong;
            else if (4 * (high - low) < high && 10 * high > 4 * 255) ++light, pr += cr, pb += cb;
        }
    }
    if (!all) return juraic::EMPTY;
    const juraic::Cell other = 100 * bright >= long(l.unsure) * all ? UNSURE : juraic::EMPTY;
    // (the white face of a piece is warm, garbage is grey)
    const bool warm = light && (pr - pb) >= 8 * light && strong;
    if (strong && (100 * strong >= long(l.colored) * all || (warm && 100 * (strong + light) >= long(l.cover) * all))) {
        const Rgb mean{int(r / strong), int(g / strong), int(b / strong)};
        if (mark && 100 * light < long(mark->pale) * all) {
            // (the mark of the screen in this cell)
            int nearest = 1 << 30;
            for (const ColorRef& ref : l.colors) nearest = std::min(nearest, distance2(mean, ref.rgb));
            for (const Rgb& c : mark->colors)
                if (distance2(mean, c) < nearest) return juraic::EMPTY;
        }
        juraic::Cell cell = classify(l, mean);
        // (too little of a piece's colour: the dots where the pair would land; of another colour: something else)
        if (100 * (strong + light) < long(l.cover) * all) return cell ? juraic::EMPTY : other;
        // (the eyes and the shine of a face make the strong colour lighter than the table, toward white and so toward
        //  other colours too: with the warm white of the face, the colour of its hue - which the edge of another piece
        //  over a face, or words over the field, may have too: read takes it only from the rest of the cell)
        if (!cell && warm && l.faceHue > 0 && face) *face = classifyHue(l, mean, l.faceHue);
        return cell ? cell : UNSURE;
    }
    return 100 * light >= long(l.garbage) * all ? juraic::GARBAGE : other;
}

// What the square around (x, y) of a window shows: as piece, but a piece only when Layout::windowShare in a hundred of its
// pixels of strong colour (every other pixel each way: the windows are read in every picture) are nearest that piece's
// colour. (A pair on its way leaves a point to the ground of the window, near a colour of the table, and the mean of the
// edges of two pieces can be near a third colour; each pixel is near neither.)
inline juraic::Cell windowPiece(const Picture& p, const Layout& l, const Scale& s, float x, float y) {
    const juraic::Cell cell = piece(p, l, s, x, y);
    if (l.windowShare <= 0 || cell < 1 || cell > juraic::MAX_COLORS) return cell;
    const Patch q(p, s, x, y, l.patch);
    long strong = 0, own = 0;
    for (int j = q.y0; j <= q.y1; j += 2) {
        const uint8_t* row = p.pixels + size_t(j) * size_t(p.stride);
        for (int i = q.x0; i <= q.x1; i += 2) {
            const Rgb c{row[4 * i + 2], row[4 * i + 1], row[4 * i]};
            if (!strongColour(std::max(c.r, std::max(c.g, c.b)), std::min(c.r, std::min(c.g, c.b)))) continue;
            ++strong;
            int best = 1 << 30;
            juraic::Cell nearest = juraic::EMPTY;
            for (const Rgb& ground : l.ground)
                if (const int e = distance2(c, ground); e < best) best = e;
            for (const ColorRef& ref : l.colors)
                if (const int e = distance2(c, ref.rgb); e < best) best = e, nearest = ref.cell;
            own += nearest == cell;
        }
    }
    return 100 * own >= long(l.windowShare) * strong ? cell : UNSURE;
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
    int unsure = 0;                          // points of the field that are UNSURE
};
struct Reading {
    SideRead side[2]; // by seat
};

inline int readNotice(const Picture& p, const Layout& l, const Scale& s, const SideLayout& d) {
    int total = 0;
    const int limit = l.markDistance * l.markDistance;
    for (const Spot& at : d.notice) {
        // (of the marks whose every point is near, the one of most points - one of them may be part of another -, and
        //  of those the nearest)
        const Mark* found = nullptr;
        long foundAway = 0;
        for (const Mark& m : l.marks) {
            bool allNear = true;
            long away = 0;
            for (const Probe& q : m.probes) {
                const int apart = distance2(sample(p, s, at.x + q.dx, at.y + q.dy, l.markPatch), q.rgb);
                allNear = allNear && apart <= limit, away += apart;
            }
            if (allNear && (!found || m.probes.size() > found->probes.size() || (m.probes.size() == found->probes.size() && away < foundAway))) found = &m, foundAway = away;
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
        juraic::Cell face[juraic::W][POINTS]{};
        for (int x = 0; x < juraic::W; ++x)
            for (int k = 0; k < POINTS; ++k) {
                const Spot at = pointSpot(d, x, k);
                const MarkedCell* mark = nullptr;
                for (const MarkedCell& m : d.marked)
                    if (m.x == x && m.y == k / 2) mark = &m;
                out.point[x][k] = pointHeight(k) > d.shownRows ? HIDDEN : piece(p, l, s, at.x, at.y, mark, &face[x][k]);
            }
        // (an UNSURE point with a piece's face takes the colour of its hue when the other point of its cell reads it)
        for (int x = 0; x < juraic::W; ++x)
            for (int k = 0; k < POINTS; ++k) {
                if (out.point[x][k] == UNSURE && face[x][k] != juraic::EMPTY && out.point[x][k ^ 1] == face[x][k]) out.point[x][k] = face[x][k];
                out.unsure += out.point[x][k] == UNSURE;
            }
        for (int n = 0; n < 2; ++n) {
            const juraic::Cell axis = windowPiece(p, l, s, d.next[n][0].x, d.next[n][0].y);
            const juraic::Cell child = windowPiece(p, l, s, d.next[n][1].x, d.next[n][1].y);
            out.next[n] = {axis, child};
            out.nextRead[n] = axis >= 1 && axis <= juraic::MAX_COLORS && child >= 1 && child <= juraic::MAX_COLORS;
        }
        out.notice = readNotice(p, l, s, d);
    }
    return r;
}

// ---- what a reading shows ---------------------------------------------------------------------------------------

inline bool isPiece(juraic::Cell c) { return c != juraic::EMPTY && c != HIDDEN && c != UNSURE; }

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
    bool axisHidden = false; // the pair upside down, its axis above the screen (readPair, upsideDown)
};

// The cells the two pieces of a pair cover, the axis first (y * 2 + upper: in half cells).
inline void pairHalves(const juraic::PairControl& p, int& axisX, int& axisHalf, int& childX, int& childHalf) {
    axisX = p.x, childX = p.childX(), axisHalf = 2 * p.y + (p.upper ? 1 : 0), childHalf = 2 * p.childY() + (p.upper ? 1 : 0);
}
// The points of a field the pieces of a pair seen cover (none when it is not seen).
struct PairPoints {
    int ax = -1, ah = -1, cx = -1, ch = -1;
    explicit PairPoints(const PairSeen& pair) {
        if (pair.seen) pairHalves(pair.at, ax, ah, cx, ch);
    }
    bool covers(int x, int k) const { return (x == ax && (k == ah || k == ah + 1)) || (x == cx && (k == ch || k == ch + 1)); }
};

// The pair over `under`, the field it moves over: the pieces above it, two of them side by side or one on the other,
// or one with the place above it beyond the screen (the other piece there). `expected`: its colours when they are known
// (axis EMPTY: not known). `previous`: where it was seen last, for a pair whose two pieces have one colour (its axis is
// taken to be where the axis was). Not seen: other pieces than the pair's, or pieces between two places (a pair
// turning). upsideDown: one piece of the child's colour, not the axis's, in the twelve rows and with the place above it
// beyond the screen, a column at most from where `previous` saw the pair, is the pair upside down, its axis above (turned
// quickly in the 12th row, the axis lifted into the 13th: not read so, the pair in hand was lost on rounds drawn as the
// real game, and not seen again as it came down; taken anywhere, a piece at rest in the 13th row of the first column, its
// lower half on the screen, was read as the pair as it appeared in the third).
inline PairSeen readPair(const SideRead& s, const juraic::GameField& under, juraic::Pair expected = {}, const PairSeen* previous = nullptr, bool upsideDown = false) {
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
    if (!axisFirst && !axisSecond && hiddenChild && upsideDown && expected.axis && first.cell == expected.child && first.bottom / 2 < 12 && previous && previous->seen &&
        std::abs(first.x - previous->at.x) <= 1) {
        PairSeen r;
        r.seen = true;
        r.at.x = int8_t(first.x), r.at.y = int8_t(first.bottom / 2 + 1), r.at.upper = first.bottom % 2 == 1;
        r.at.dir = juraic::DOWN;
        r.colors = {expected.axis, first.cell};
        r.axisHidden = true;
        return r;
    }
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

// Points of `read` that show a piece of a colour (not garbage) above the pieces of `base`.
inline int colouredAbove(const SideRead& read, const juraic::GameField& base) {
    int h[juraic::W], n = 0;
    base.heights(h);
    for (int x = 0; x < juraic::W; ++x)
        for (int k = 2 * h[x]; k < POINTS; ++k) n += isPiece(read.point[x][k]) && read.point[x][k] != juraic::GARBAGE;
    return n;
}

// ---- frame after frame ------------------------------------------------------------------------------------------

// Every piece of `part` is in `whole` (the pieces of a pair lying on it, or garbage, may be above them).
inline bool contains(const juraic::GameField& whole, const juraic::GameField& part) {
    int h[juraic::W];
    part.heights(h);
    for (int x = 0; x < juraic::W; ++x)
        for (int y = 0; y < h[x]; ++y)
            if (whole.f.get(x, y) != part.f.get(x, y)) return false;
    return true;
}

// Points of a field the screen draws something over and leaves standing (the words of an all clear): what each point
// read when it was taken for that (EMPTY: not taken); since each point last read nothing, in the air with an empty point
// under it, the piece it has read in the most clear readings so far and in how many (0: none), and the piece it read
// last and in how many readings in a row. `was`: what each point read when it was last taken for the words in the round
// (kept after it reads nothing: the words' points read nothing for up to 444 readings in a row on the real game, and
// may then show again right on top of the pieces under them).
struct Words {
    juraic::Cell at[juraic::W][POINTS]{};
    juraic::Cell was[juraic::W][POINTS]{};
    juraic::Cell stillAs[juraic::W][POINTS]{};
    int stillFor[juraic::W][POINTS]{};
    juraic::Cell lastAs[juraic::W][POINTS]{};
    int lastFor[juraic::W][POINTS]{};
    bool any = false; // a point is taken
};

// The reading with the words taken away: a point that reads what the words read there is nothing, unless a piece is in
// front of it - a run of that piece in its column with points that are not the words': the run keeps the words' points
// between those, and one more next to them where it has one on one side only, for its points to come in twos (a piece
// covers two points; with one on each side the run is left short, as the words may be over either).
inline SideRead withoutWords(const SideRead& read, const Words& words) {
    if (!words.any) return read;
    SideRead out = read;
    for (int x = 0; x < juraic::W; ++x)
        for (int k = 0; k < POINTS;) {
            const juraic::Cell c = read.point[x][k];
            if (!isPiece(c)) {
                ++k;
                continue;
            }
            int end = k;
            while (end < POINTS && read.point[x][end] == c) ++end;
            int low = -1, high = -1; // the lowest and highest points of the run that are not the words'
            for (int j = k; j < end; ++j)
                if (words.at[x][j] != c) high = j, low = low < 0 ? j : low;
            if (low >= 0 && (high - low) % 2 == 0) {
                const bool below = low > k, above = high + 1 < end;
                if (below != above) (below ? low : high) += below ? -1 : 1;
            }
            // (a run that is the words' at every point, right between two pieces that are not: a piece of a stack - the
            //  words are behind the pieces, and a stack shows nothing of them between two of its pieces. So too the words'
            //  points at the bottom of a run, right on a piece that is not the words': on rounds drawn so, the two pieces
            //  above them were left over a gap and taken for a pair in the air)
            const auto piece = [&](int j) { return j >= 0 && j < POINTS && isPiece(read.point[x][j]) && words.at[x][j] != read.point[x][j]; };
            if (low < 0 && piece(k - 1) && piece(end)) low = k, high = end - 1;
            else if (low > k && piece(k - 1)) low = k;
            for (int j = k; j < end; ++j)
                if (low < 0 || j < low || j > high) out.point[x][j] = juraic::EMPTY;
            k = end;
        }
    return out;
}

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
    int64_t lastSeenAt = -1;
    int pairs = 0;             // pairs this side has been given since the round began
    int64_t spawnedAt = -1;    // the frame in which the last of them was seen first
    int64_t spawnedBefore = -1; // ... and the one before it
    juraic::Cell seenAxis = juraic::EMPTY; // the axis of the last of them as it was seen when taken
    bool late = false;         // ... seen late (Tracker::late)
    bool clear = true;         // the last reading of this side had no UNSURE point (otherwise nothing of its field was taken)
    int64_t unclearAt = -1;    // the frame of the last reading that was not clear
    int notice = 0;            // the garbage notice, as it has read the same for Tracker::noticeFrames frames
    int noticeRead = 0;        // ... as last read, and since which frame
    int64_t noticeSince = 0;
    juraic::Pair next[2]{};    // the window of the next pairs as last read
    bool nextRead[2]{};
    int nextStill = 0;         // readings in a row that showed the window as the one before it (both pairs read)
    int64_t nextMovedAt = -1;  // the last reading in which it was read otherwise (moving)
    int64_t runFrom = -1;      // the frame of the first of those readings in a row
    int64_t stillAt = -1;      // ... and of the one in which it had read so windowStill readings after the first
    // The window as it last read standing (windowStill readings in a row): its two pairs, that reading, and the first
    // reading that showed it so (Tracker::learnFirstWindow).
    juraic::Pair stood[2]{};
    int64_t stoodAt = -1;
    int64_t stoodFrom = -1;
    // The window as it last stood (Tracker::readWindow): its two pairs, and the number of the first of them among the pairs
    // of the round (-1: not known - a round not followed from its beginning, before its first pair). The number is the
    // state the pairs are counted by: the window moves on by one just before each pair appears.
    juraic::Pair stand[2]{};
    bool standKnown = false;
    int standAt = -1;
    int64_t awayFrom = -1;     // the first reading of the last time it was not read whole as it stood (moving on, or lit)
    bool asStood = false;      // the last reading showed it whole as it stood
    int64_t standFrom = -1;    // the first reading that showed it standing where it stands (moved on there)
    int anchorFor = -1;        // the pair the window is to be numbered after, its number not known (Tracker::anchor)
    // The frame the window last stood moved on (stillAt), when that told of a pair not shown before - or of the pair that
    // appeared, the window read standing pairGap frames after it at most: later than that, it was hidden as the pair
    // appeared, and its move tells nothing of a pair to come. So too, for the driver, as soon as it stands where it moves
    // on to, not taken as moved yet (provisional), and as before (shiftedBefore) when it goes away from there untaken.
    int64_t shiftedAt = -1;
    int64_t shiftedBefore = -1;
    bool provisional = false;
    // The pairs this side has been given in the round: as the window moved on, and as the field showed them (the number of
    // the pair that comes after them). More than `pairs`: the window has moved on for a pair not seen yet; by more than one,
    // pairs before that one were put with the screen not showing them (a light over the field): `unseen` of them.
    int dealt = 0;
    int unseen = 0;
    // A pair seen where pairs appear and not taken (pieces above the field in the reading before; before the first pair
    // of the round, other colours than those learnt): the frame of the reading it was seen in, and its axis (Tracker::step).
    int64_t strayAt = -1;
    juraic::Cell strayAxis = juraic::EMPTY;
    // Clear readings in a row in which the pair in hand was not seen, over the field it appeared over with nothing above
    // it (Tracker::outOfSight).
    int missedFor = 0;
    // Something the screen draws over the field and leaves standing (Tracker::wordsFrames).
    Words words;
};

// The real game as its records show it where they differ from the frame tables of game/timing.h: no turn is taken for 4
// to 7 frames after a quick turn (of the pairs that turned quickly and were turned again 4 frames after, none was turned
// - with Timing::quickTurnLock 3 the engine turned them, and 4 of 25 such pairs went where the engine locked them, 12 of 25
// with 4 to 7 -, and two turned 8 frames after were). The longest of them is taken. And the fall is counted on while a
// pair lies (Timing::fallOnGround): a pair that loses its ground steps down sooner than one that begins to fall then.
// And a quick turn whose first press reached the game before the pair appeared leaves the axis where it is
// (Timing::queuedTurnBelow).
inline juraic::Timing realGameTiming() {
    juraic::Timing t;
    t.quickTurnLock = 7;
    t.fallOnGround = 1;
    t.queuedTurnBelow = 1;
    return t;
}

class Tracker {
public:
    juraic::Timing timing = realGameTiming();
    int stillFrames = 2;
    // Both fields empty, with no pair, for this many frames: a new round (the screen before a round shows them so). A
    // reading otherwise for fewer than roundBreak frames in a row does not end the empty time (a light read as a piece).
    int roundGap = 30, roundBreak = 2;
    // The notice is taken when it has read the same for this many frames (a light of the screen over it changes it).
    int noticeFrames = 3;
    // A new pair hidden by a light as it appeared: taken when it is seen within this many frames after the light, in
    // the air, of the colours of the pair that comes next (Tracker::late).
    int lateFrames = 20;
    // A pair where pairs appear within this many frames after the last one appeared, over the field it appeared over,
    // is that pair (moved at once and read as another colour in a picture, it was taken for a new one).
    int pairGap = 12;
    // The pair in hand not seen in this many clear readings in a row, over the field it appeared over with nothing above
    // it, has been put out of sight (both pieces in the 13th and 14th rows), the window moved on or not. (The real game
    // does not show a pair as it turns: 9 frames for two turns in its records.)
    int outOfSight = 20;
    // The window of the next pairs (readWindow). On the real game it stands, slides for 7 frames as the pair before is put
    // (6 to 9 in nearly every move of the records of 36 sessions), stands moved on by one, and the pair appears a reading
    // later (1P 887 of 1003 moves, 2P 1019 of 1082, in the records of 11-03). On its way, and under a light, it reads
    // for some readings in a row as a window it never showed: in those records, as the window moved on by one (its first
    // pair the one it showed second) for 5 readings at most (1 with the window's ground read as no piece), two pairs alike
    // in it as their first and another for 5 at most, its second place otherwise for 8 at most (19 as it moved on, the
    // pair new in it lit, in records read before the ground was), and its first place as a pair of neither place for 9.
    //  - windowStill: readings in a row (after the first) in which it reads the same to stand (Tracker::readWindow
    //    looks at it then; Driver too).
    //  - windowMove: ... to be taken as moved on with no pair seen since it left where it stood (with a pair seen,
    //    windowStill).
    //  - windowLearn: ... for the pairs it shows to be learnt for sure (before that, as it may be moving: with 2, in the
    //    records, 152 rounds, pairs learnt and learnt again otherwise before they appeared were 78 on 1P and 19 on 2P; with
    //    3, 62 and 13; with 4, 54 and 8, but in a round drawn and read, lit 80 frames, a pair went elsewhere than the AI
    //    chose).
    //  - windowOver: ... for a pair learnt for sure to be learnt otherwise from it - and more than the readings that
    //    showed it as learnt.
    // A window whose number is not known is numbered after the first pair seen when it stands, having moved on at most
    // windowBefore frames before that pair appeared, or windowWait frames after it (anchor).
    // The window can be taken as moved on by windowAhead pairs at most at once (pairs put unseen under a light; it is read
    // so only as the pairs learnt tell). Away from where it stood windowSlide frames or more before standing again, it may
    // have slid (Tracker::newPair).
    int windowStill = 2, windowMove = 5, windowLearn = 3, windowOver = 8, windowBefore = 10, windowWait = 30, windowAhead = 4, windowSlide = 5;
    // Before the first pair of a round the window shows the first two pairs; the screens before a round (the title, the
    // menus) may read as a window standing still too. A window that stands still for windowLong readings and reads
    // other pairs than those learnt is taken instead (on the real game the window read three pictures as other pairs on
    // its way), unless it reads the second of them first - the window moved on as the first pair comes: it gives the
    // third - and has stood so for fewer than roundGap readings.
    int windowLong = 8;
    // Pieces do not stand still in the air: a pair falls half a cell in 13 frames at most (Timing::fallPerFrame), so a
    // piece of it covers a point for 26 frames at most and a pair of one colour standing up 52, and it lies on what is
    // under it. Points that read the same piece with an empty point under them, above the pieces at rest, in
    // wordsFrames clear readings since they last read nothing (another piece in front of them in between not counted),
    // wordsPoints of them or more at once, are something the screen draws over the field and leaves standing (on the
    // real game the words of an all clear, behind the pieces, for more than 900 frames: 18 points read as pieces; with
    // 20 frames the points of a pair falling by itself in front of them were taken too), read as nothing (withoutWords)
    // until a point of them reads nothing. While they stand the field is not taken for empty.
    int wordsFrames = 60, wordsPoints = 8;
    SideTrack side[2];
    // The pairs the screen has shown, numbered as the players draw them (both draw from one tsumo), as the window of each
    // side showed them by that side's own count: a side that counted its pairs wrong learns them at the wrong places,
    // for itself only (on the real game the person's side, three pairs short, made 2P wait for colours that did not
    // come). knownOf: 1 learnt from a window that stood still; 2 from one that may have been moving (put right after).
    // strengthOf: the readings of the side's window, standing, that showed a pair learnt for sure so (windowOver).
    std::vector<juraic::Pair> pairsOf[2];
    std::vector<uint8_t> knownOf[2];
    std::vector<int> strengthOf[2];
    // The pairs of 2P are numbered `shift` places on from the same pairs of 1P, as the two windows showed them (align);
    // aligns: the times it changed.
    int shift = 0;
    int aligns = 0;
    int maxShift = 3, alignRun = 6;
    // The place the other side counts pair `index` of side s at.
    int otherIndex(int s, int index) const { return s == 0 ? index + shift : index - shift; }
    Reading last;             // the last reading
    int64_t frame = -1;       // ... and its frame
    int64_t frameBefore = -1; // the frame of the reading before it
    int64_t roundBefore = -1; // ... in the reading in which the round was taken to begin (newRound)
    int64_t roundStart = -1;  // the frame that is frame 0 of the round (-1: not known yet)
    bool fromStart = false;   // the round has been followed from its beginning (both fields seen empty before it)
    bool spawned[2] = {};     // in the last reading a new pair was seen on that side
    bool late[2] = {};        // ... seen late: it was hidden as it appeared, and may have been moved already
    int doubts = 0;           // readings that did not fit what had been read before
    int rounds = 0;           // rounds seen begin (both fields empty for roundGap frames)
    // For the record of the reading: the frame each pair was last learnt in from a window that stood still; how often a
    // pair so learnt was learnt otherwise, and the frame of the colours it had; how often one was learnt otherwise than
    // the other window showed it at its place, and the frame of that.
    std::vector<int64_t> learntAt[2];
    int relearnt[2] = {};
    int64_t relearntFrom[2] = {-1, -1};
    int unlike = 0;
    int64_t unlikeFrom = -1;

    void reset() {
        const juraic::Timing t = timing;
        const int still = stillFrames, gap = roundGap, notice = noticeFrames, lateness = lateFrames, twice = pairGap, broken = roundBreak, hiddenFor = outOfSight;
        const int windowS = windowStill, windowB = windowBefore, windowW = windowWait, windowL = windowLong, wordsF = wordsFrames, wordsP = wordsPoints;
        const int shiftMost = maxShift, run = alignRun, windowM = windowMove, windowO = windowOver, windowLe = windowLearn, windowA = windowAhead, windowSl = windowSlide;
        *this = Tracker{};
        timing = t, stillFrames = still, roundGap = gap, noticeFrames = notice, lateFrames = lateness, pairGap = twice, roundBreak = broken, outOfSight = hiddenFor;
        windowStill = windowS, windowBefore = windowB, windowWait = windowW, windowLong = windowL, wordsFrames = wordsF, wordsPoints = wordsP;
        maxShift = shiftMost, alignRun = run, windowMove = windowM, windowOver = windowO, windowLearn = windowLe, windowAhead = windowA, windowSlide = windowSl;
    }

    // The pieces of side s stand still: read the same for stillFrames frames, nothing above them but the pair, and the
    // last reading clear. (A reading that is not clear is no reading: it neither changes the pieces nor counts.)
    bool still(int s) const { return side[s].clear && frame - side[s].changedAt >= stillFrames && side[s].loose == 0; }
    // The frame of the round that the frame `at` of the clock is (0 before it, or while its beginning is not known).
    int64_t roundFrame(int64_t at) const { return roundStart >= 0 && at >= roundStart ? at - roundStart : 0; }
    // Pair `index` for side s: as its own window showed it, or else as the other side's did, at the place the other side
    // counts it (shift).
    bool knows(int s, int index) const { return learnt(s, index) || learnt(1 - s, otherIndex(s, index)); }
    juraic::Pair pairOf(int s, int index) const { return learnt(s, index) ? pairsOf[s][size_t(index)] : pairsOf[1 - s][size_t(otherIndex(s, index))]; }
    // The pairs either side has learnt of (some of them, perhaps, not known).
    size_t pairsLearnt() const {
        const int n0 = int(pairsOf[0].size()), n1 = int(pairsOf[1].size());
        return size_t(std::max({n0, n1, n1 - shift, n0 + shift}));
    }
    // The pairs known, of both sides together (it grows as pairs are learnt).
    size_t knownCount() const {
        size_t n = 0;
        for (const auto& k : knownOf)
            for (const uint8_t x : k) n += x != 0;
        return n;
    }
    // The pairs as side s is shown them (knows, pairOf), one number each: 0 not known, else 256 + axis * 16 + child. A
    // pair read again otherwise at the same place changes it while pairsLearnt and knownCount stay as they were.
    std::vector<uint16_t> pairsShown(int s) const {
        std::vector<uint16_t> v(pairsLearnt(), 0);
        for (size_t i = 0; i < v.size(); ++i)
            if (knows(s, int(i))) {
                const juraic::Pair p = pairOf(s, int(i));
                v[i] = uint16_t(256 + (int(p.axis) & 15) * 16 + (int(p.child) & 15));
            }
        return v;
    }
    // Where the two windows' pairs agree: for each shift (maxShift at most either way), the pairs both windows showed
    // standing still (knownOf 1) at the places it puts side by side, from the last of them back - the shift kept while
    // its last two agree (or all, fewer), else another taken whose last alignRun agree, the most of them (one alone).
    // (In the records a side that counted a pair more or less learnt the pairs after it one place on to the end of the
    // round, and the other side was shown them so. A pair one window read wrong - under a light - moves nothing: a side
    // not shown the pairs the other's window read counts a pair hidden as it appeared no more, lookForPair, and the
    // pairs after it were one place off then. The window of a side lit long read 4 pairs in a row one place on for a
    // while in a round drawn and read, and with the shift moved so its count went wrong: alignRun 6.)
    void align() {
        const auto run = [&](int d, int& seen) {
            int n = 0;
            seen = 0;
            for (int i = int(pairsOf[0].size()) - 1; i >= 0; --i) {
                const int j = i + d;
                if (j < 0 || j >= int(pairsOf[1].size()) || knownOf[0][size_t(i)] != 1 || knownOf[1][size_t(j)] != 1) continue;
                ++seen;
                if (!same(pairsOf[0][size_t(i)], pairsOf[1][size_t(j)])) break;
                if (++n >= alignRun) break;
            }
            return n;
        };
        int seen = 0;
        if (run(shift, seen) >= std::min(2, seen)) return;
        int best = 0, ties = 0, to = shift;
        for (int d = -maxShift; d <= maxShift; ++d) {
            if (d == shift) continue;
            const int n = run(d, seen);
            if (n > best) best = n, to = d, ties = 1;
            else if (n == best) ++ties;
        }
        if (best >= alignRun && ties == 1) shift = to, ++aligns;
    }

    // What changed for each side, reading after reading, in the order it was read - what the driver takes, rather than
    // the frames SideTrack keeps (their meaning is this class's own, and changed with it): a round began; a pair appeared
    // (the frame it was seen first, the pairs counted then, seen late), or was taken again so; the window moved on - or
    // its move was taken back - (the frame it stood moved on, as SideTrack::shiftedAt tells it); the window went away from
    // where it stood (SideTrack::awayFrom). `told`: the events of the side since the tracker was made; the last
    // EVENTS_KEPT of them are kept, the first of `events` being number told - size (from 0).
    struct Event {
        enum Kind : uint8_t { ROUND, APPEARED, MOVED, AWAY } kind = ROUND;
        int64_t frame = -1;
        int pairs = 0;
        bool late = false;
    };
    static constexpr size_t EVENTS_KEPT = 256;
    std::deque<Event> events[2];
    uint64_t told[2] = {};
    // ... of this tracker as it was made or reset (made anew: the events counted from 0 again)
    // (counted atomically: trackers are made and reset on the image thread and on the memory worker at once)
    static inline std::atomic<uint64_t> made{0};
    uint64_t id = made.fetch_add(1, std::memory_order_relaxed) + 1;

    // One picture, of the frame `at` (frames counted by the clock: a frame that was not taken is a number left out).
    void step(const Reading& r, int64_t at) {
        struct Was {
            int64_t spawnedAt, shiftedAt, awayFrom;
            bool late;
        };
        const Was was[2] = {{side[0].spawnedAt, side[0].shiftedAt, side[0].awayFrom, side[0].late}, {side[1].spawnedAt, side[1].shiftedAt, side[1].awayFrom, side[1].late}};
        const int roundsWere = rounds;
        read(r, at);
        for (int s = 0; s < 2; ++s) {
            const SideTrack& t = side[s];
            if (rounds != roundsWere) tell(s, {Event::ROUND, at});
            if (t.spawnedAt != was[s].spawnedAt || t.late != was[s].late) tell(s, {Event::APPEARED, t.spawnedAt, t.pairs, t.late});
            if (t.shiftedAt != was[s].shiftedAt) tell(s, {Event::MOVED, t.shiftedAt});
            if (t.awayFrom != was[s].awayFrom) tell(s, {Event::AWAY, t.awayFrom});
        }
    }

private:
    void tell(int s, const Event& e) {
        events[s].push_back(e), ++told[s];
        if (events[s].size() > EVENTS_KEPT) events[s].pop_front();
    }
    void read(const Reading& r, int64_t at) {
        last = r;
        frameBefore = frame, frame = at;
        spawned[0] = spawned[1] = late[0] = late[1] = false;
        bool empty = true;
        for (int s = 0; s < 2; ++s) {
            const SideRead& read = r.side[s];
            SideTrack& t = side[s];
            readWindow(s, read, at);
            t.clear = read.unsure == 0;
            if (!t.clear) {
                // (something the screen draws over the field: nothing of it is taken, the pair not seen in this frame)
                t.pair = {};
                t.unclearAt = at;
                empty = false;
                continue;
            }
            // (the words the screen leaves standing taken away)
            const SideRead field = withoutWords(read, t.words);
            last.side[s] = field;
            const juraic::GameField resting = restingField(field);
            if (!(resting == t.resting)) t.resting = resting, t.changedAt = at;
            t.pair = {};
            followHand(s, field, resting, at);
            if (!t.pair.seen) lookForPair(s, field, resting, at);
            t.loose = looseOf(field, t);
            takeWords(t, read, resting);
            empty = empty && resting.f.empty() && t.loose == 0 && !t.inHand && !t.words.any;
        }
        fullFor = empty ? 0 : fullFor + 1;
        if (fullFor >= roundBreak) emptySince = -1;
        else if (empty && emptySince < 0) emptySince = at;
        if (empty && at - emptySince >= roundGap && (side[0].pairs || side[1].pairs || !fromStart)) newRound();
        learnFirstWindow();
        for (SideTrack& t : side) t.unseen = t.dealt > t.pairs ? t.dealt - t.pairs - 1 : 0;
    }

private:
    // The notice and the window of side s in the reading `read`. The window standing (windowStill readings in a row):
    // where it has moved on to from where it stood (moveOf), taken as a move once it has stood windowMove readings, or
    // windowStill with the pairs it moved on for seen where pairs appear since; the pairs it shows learnt at its number.
    void readWindow(int s, const SideRead& read, int64_t at) {
        SideTrack& t = side[s];
        if (read.notice != t.noticeRead) t.noticeRead = read.notice, t.noticeSince = at;
        if (at - t.noticeSince + 1 >= noticeFrames) t.notice = t.noticeRead;
        const bool sameWindow = read.nextRead[0] && read.nextRead[1] && t.nextRead[0] && t.nextRead[1] && same(read.next[0], t.next[0]) && same(read.next[1], t.next[1]);
        t.nextStill = sameWindow ? t.nextStill + 1 : 0;
        if (!sameWindow) t.nextMovedAt = t.runFrom = at;
        if (!sameWindow && t.provisional) t.shiftedAt = t.shiftedBefore, t.provisional = false;
        if (t.nextStill == windowStill) t.stillAt = at;
        for (int n = 0; n < 2; ++n) t.next[n] = read.next[n], t.nextRead[n] = read.nextRead[n];
        if (whole(t) && t.nextStill >= windowStill) t.stood[0] = t.next[0], t.stood[1] = t.next[1], t.stoodAt = at, t.stoodFrom = t.runFrom;
        const bool asStood = whole(t) && t.standKnown && same(t.next[0], t.stand[0]) && same(t.next[1], t.stand[1]);
        if (!asStood && t.asStood) t.awayFrom = at;
        t.asStood = asStood;
        // (not with no pieces at rest on either field, no pair in hand and nothing drawn over the fields after pairs have
        //  come: the window is that of the next round, which has not been taken to begin)
        const auto bare = [](const SideTrack& u) { return u.resting.f.empty() && !u.inHand && !u.words.any; };
        const bool roundOver = (side[0].pairs || side[1].pairs) && bare(side[0]) && bare(side[1]);
        if (!whole(t) || t.nextStill < windowStill || roundOver) return;
        if (t.standAt < 0) {
            anchor(s, at);
            return;
        }
        const int k = moveOf(s);
        if (k < 0) return;
        if (k > 0) {
            // (moved on: once it has stood windowMove readings, or windowStill with the pairs it moved on for, or one
            //  more, seen where pairs appear since it left where it stood)
            if (t.nextStill < windowMove && t.dealt < t.standAt + k && t.spawnedAt < t.awayFrom) {
                if (!t.provisional) t.shiftedBefore = t.shiftedAt, t.shiftedAt = t.stillAt, t.provisional = true;
                return;
            }
            moveOn(s, k, t.stillAt);
            // (moved on past the pair after the one taken last, having left where it stood before that one was taken - its
            //  own move, made before the pair appears; for the next pair it moves on only after that one is put -: the
            //  pairs before it were put unseen, and the pair taken is the one before the window's first. On the real game
            //  the pairs after a pair seen as the window moved on again under a light were learnt a place out.)
            //  Only as the colours seen of the pair taken tell so - those of the one before the window's first, not of the
            //  one it was numbered as: two pairs of one axis, a pair taken as the window moved on was numbered a place out
            //  so in a play test of the real game.)
            if (t.spawnedAt >= 0 && t.awayFrom <= t.spawnedAt && t.standAt > t.pairs && knows(s, t.standAt - 1) && t.seenAxis == pairOf(s, t.standAt - 1).axis &&
                !(knows(s, t.pairs - 1) && t.seenAxis == pairOf(s, t.pairs - 1).axis))
                renumber(s, t.standAt);
        } else if (t.standAt < t.pairs && same(t.stand[0], t.stand[1]) && asStood && t.runFrom > t.spawnedBefore) {
            // (two pairs alike in it, standing as it stood, away since the pair before the one taken last was taken: it has
            //  moved on for that one to a third pair alike - on the real game a pair seen late so was numbered as the one
            //  before it, and the pairs after it a place out)
            moveOn(s, t.pairs - t.standAt, t.stillAt);
        }
        // (Two pairs alike in it, away from where it stood for a few readings and standing there again, is not taken as
        //  moved on to a third pair alike: on the real game its window reads so at other times too - taken so, the
        //  pairs of two records were counted up to five too many in a round, and 30 pairs and more learnt otherwise.)
        learnStand(s);
    }
    static bool whole(const SideTrack& t) { return t.nextRead[0] && t.nextRead[1]; }
    // How far the window as read has moved on from where it stood: 0 as it stood or with its second place otherwise, 1 with
    // first the pair it showed second (two pairs alike in it: with its first pair and another - but not within pairGap
    // frames after it moved on there, as it does not move on again so soon: its second place read otherwise then; on the
    // real game a window read before its ground was, sliding, stood for 6 readings as two pairs alike, and its move on
    // to the first of them and another was counted again), more as the pairs learnt after those tell (either side's
    // window, windowAhead at most); -1 as no window it can have moved on to (a light, the screen after a round, a window
    // read wrong).
    int moveOf(int s) const {
        const SideTrack& t = side[s];
        if (same(t.next[0], t.stand[0]) && same(t.next[1], t.stand[1])) return 0;
        if (same(t.next[0], t.stand[1]) && !(same(t.stand[0], t.stand[1]) && frame - t.standFrom < pairGap)) {
            // (two pairs alike in it and a third alike after them, as the pairs learnt tell - the other side's window
            //  showed them -, read with a pair alike first and the pair after the third second: it has moved on by two,
            //  once to the third pair alike, which the pairs it shows did not tell. Taken as one move, a pair of 1P
            //  hidden as it appeared, the second of three alike, was not counted on a record of the real game, and the
            //  pairs of 1P were numbered a place behind for the rest of the round.)
            const int third = t.standAt + 2;
            if (same(t.stand[0], t.stand[1]) && knows(s, third) && same(t.next[0], pairOf(s, third)) && !same(t.next[1], pairOf(s, third)) && knows(s, third + 1) &&
                same(t.next[1], pairOf(s, third + 1)))
                return 2;
            return 1;
        }
        if (same(t.next[0], t.stand[0])) return 0;
        for (int k = 2; k <= windowAhead; ++k) {
            const int index = t.standAt + k;
            if (knows(s, index) && same(t.next[0], pairOf(s, index)) && (!knows(s, index + 1) || same(t.next[1], pairOf(s, index + 1)))) return k;
        }
        return -1;
    }
    // The window of side s has moved on by k from where it stood, standing so from the frame `when`: as many pairs given,
    // fewer the field has shown already.
    void moveOn(int s, int k, int64_t when) {
        SideTrack& t = side[s];
        t.standAt += k;
        const bool told = t.standAt > t.dealt; // (it tells of pairs the field has not shown)
        t.dealt = std::max(t.dealt, t.standAt);
        if (t.provisional) t.shiftedAt = t.shiftedBefore, t.provisional = false;
        if (told || when <= t.spawnedAt + pairGap) t.shiftedAt = when;
        t.stand[0] = t.next[0], t.stand[1] = t.next[1], t.asStood = true, t.standFrom = t.runFrom;
    }
    // The pair taken last is pair `pairs` - 1 (the window tells so).
    void renumber(int s, int pairs) {
        SideTrack& t = side[s];
        t.pairs = pairs;
        if (knows(s, pairs - 1)) t.colors = t.pair.colors = t.lastSeen.colors = pairOf(s, pairs - 1);
    }
    // The two pairs the window standing shows, learnt at its number: for sure once it has stood windowLearn readings, and
    // pairGap frames since it moved on there (on its way it reads as another pair in its second place for a few readings,
    // and so stands); over a pair learnt for sure otherwise only once it has stood windowOver readings, and more than the
    // readings that showed that pair so (strengthOf, with the other side's window when it showed it alike).
    void learnStand(int s) {
        SideTrack& t = side[s];
        const bool sure = t.nextStill >= windowLearn && frame - t.standFrom >= pairGap;
        for (int n = 0; n < 2; ++n) {
            const int index = t.standAt + n;
            const juraic::Pair p = t.next[n];
            if (learnt(s, index) && knownOf[s][size_t(index)] == 1) {
                int& shown = strengthAt(s, index);
                if (same(pairsOf[s][size_t(index)], p)) ++shown;
                else if (t.nextStill >= windowOver && t.nextStill + 1 > strength(s, index)) learn(s, index, p), shown = t.nextStill + 1;
            } else if (!learnt(s, index) || same(pairsOf[s][size_t(index)], p) || t.nextStill >= windowLearn) {
                learn(s, index, p, sure);
                if (sure) strengthAt(s, index) = t.nextStill + 1;
            }
            // (what it stands as is what is learnt: a pair learnt as it may be moving is learnt otherwise only once it has
            //  stood windowLearn readings so - a light over its second place for 3 readings made it stand as two pairs
            //  alike, and the light gone it was taken as moved on, on the real game)
            if (learnt(s, index)) t.stand[n] = pairsOf[s][size_t(index)];
        }
    }
    // The readings that showed pair `index` as side s learnt it for sure (a pair set from outside: windowOver).
    int& strengthAt(int s, int index) {
        if (int(strengthOf[s].size()) <= index) strengthOf[s].resize(size_t(index) + 1, windowOver);
        return strengthOf[s][size_t(index)];
    }
    int strength(int s, int index) const {
        const auto of = [&](int k, int i) { return i >= 0 && i < int(strengthOf[k].size()) ? strengthOf[k][size_t(i)] : windowOver; };
        return of(s, index) + (shownAlike(s, index) ? of(1 - s, otherIndex(s, index)) : 0);
    }
    // A window whose number is not known (a round not followed from its beginning): numbered after the first pair seen
    // (anchorFor) once it stands windowLearn readings, moved on at most windowBefore frames before that pair appeared, or
    // windowWait frames after it.
    void anchor(int s, int64_t at) {
        SideTrack& t = side[s];
        if (t.anchorFor < 0 || t.nextStill < windowLearn || (t.runFrom < t.spawnedAt - windowBefore && at - t.spawnedAt < windowWait)) return;
        t.standAt = t.anchorFor + 1, t.standKnown = true, t.anchorFor = -1, t.standFrom = t.runFrom, t.stand[0] = t.next[0], t.stand[1] = t.next[1];
        t.dealt = std::max(t.dealt, t.standAt);
        learnStand(s);
        t.asStood = true;
    }
    // The pair in hand, from a clear reading of side s (`field`, without the words; `resting`, its pieces at rest): where it
    // is seen, or let go.
    void followHand(int s, const SideRead& field, const juraic::GameField& resting, int64_t at) {
        SideTrack& t = side[s];
        // (the window has moved on for another pair since the pair in hand appeared: it has been put - out of sight, both
        //  pieces in the 13th and 14th rows, if the screen showed nothing of it: on the real game the next pair, of the same
        //  colour below, was taken for it still where pairs appear, and nothing played it)
        if (t.inHand && t.dealt > t.pairs) t.inHand = false;
        if (t.inHand) {
            // (gone: the field under it changed - a chain, or a field read wrong -, or something came to rest and
            //  the pair is not seen - it split, or it turns on the ground)
            t.pair = contains(resting, t.under) ? readPair(field, t.under, t.colors, &t.lastSeen, true) : PairSeen{};
            if (t.pair.seen) {
                t.lastSeen = t.pair;
                t.lastSeenAt = at;
                t.missedFor = 0;
                if (t.pair.childHidden) t.pair.colors.child = t.colors.child;
                else t.colors.child = t.pair.colors.child;
                // (no window showed it, and its child was above the screen as it appeared: its colours, both pieces shown
                //  now, as a window that may have been moving tells them - not known, the AI would wait for it on: on the
                //  real game such a first pair fell by itself where it appeared)
                if (!knows(s, t.pairs - 1) && t.colors.axis && t.colors.child && !t.pair.childHidden) learn(s, t.pairs - 1, t.colors, false);
            } else if (!(resting == t.under) && (contains(resting, t.under) || t.changedAt < at)) {
                // (pieces under it missing in one picture only are a picture read wrong - a piece moving drawn over
                //  them: the pieces under a pair in play do not go. Let go for such a picture, a pair turned up into
                //  the 13th row where pairs appear, its child alone showing there, was counted again in a round drawn
                //  and read.)
                t.inHand = false;
            } else if (resting == t.under && looseOver(field, t.under, PairSeen{}) == 0 && ++t.missedFor >= outOfSight) {
                t.inHand = false; // (outOfSight)
            }
        }
    }
    // No pair seen in hand on side s: a new one looked for in a clear reading of it.
    void lookForPair(int s, const SideRead& field, const juraic::GameField& resting, int64_t at) {
        SideTrack& t = side[s];
        const juraic::PairControl spawn = juraic::spawnPair(timing);
        const auto atSpawn = [&](const PairSeen& p) { return p.seen && p.at.x == spawn.x && p.at.y == spawn.y && p.at.upper == spawn.upper && p.at.dir == spawn.dir; };
        // (the number of the pair that comes - or, the window read standing where it moves on to and not taken as moved
        //  yet, the one it moves on for, when that is seen where pairs appear: the pair seen takes the move. Not for a
        //  pair seen elsewhere, late: words over the field read as two pieces of its colours under them, in rounds drawn
        //  so, the pair before it was numbered as it.)
        int comes = comesOf(t);
        const int moving = whole(t) && t.standKnown && t.standAt >= 0 && t.nextStill >= 1 ? moveOf(s) : 0;
        if (moving > 0 && t.standAt + moving - 1 > comes) {
            const int ahead = t.standAt + moving - 1;
            if (atSpawn(readPair(field, resting, knows(s, ahead) ? pairOf(s, ahead) : juraic::Pair{}))) comes = ahead;
        }
        // (... or the one after that, when the window, two pairs alike in it, moved on for that one 2 * pairGap frames
        //  or more ago, the field has changed since - it has been put, unseen -, and a pair of that one's colours is seen,
        //  of not the colours of the one it moved on for: it moved on again, showing two pairs alike as before - on the
        //  real game a window of 1P read before its ground was, three pairs alike after one put unseen, and the pairs of
        //  1P were numbered a place behind)
        bool alikeOn = false;
        if (t.dealt > t.pairs && comes == t.dealt - 1 && t.standKnown && t.standAt == t.dealt && same(t.stand[0], t.stand[1]) && at - t.shiftedAt >= 2 * pairGap &&
            t.changedAt > t.shiftedAt && knows(s, comes) && knows(s, comes + 1) && pairOf(s, comes).axis != pairOf(s, comes + 1).axis &&
            readPair(field, resting, pairOf(s, comes + 1)).seen)
            ++comes, alikeOn = true;
        // A new pair where pairs appear, on a field that stood still before it with nothing above it in the
        // last clear reading (pieces of a chain falling past the place are no pair).
        const juraic::Pair coming = knows(s, comes) ? pairOf(s, comes) : juraic::Pair{};
        const PairSeen fresh = readPair(field, resting, coming);
        const bool there = atSpawn(fresh);
        const bool settled = at - t.changedAt >= stillFrames && t.loose == 0;
        // (hidden by a light as it appeared: seen after it, in the air, both pieces, of the colours of the pair
        //  that comes - or its axis only, the child still above the screen, where pairs appear or a row below,
        //  2 * pairGap frames or more after the last pair appeared, over pieces at rest: on the real game a pair
        //  first seen so after a light, after a chain, was taken for no pair, and nothing played it; between two
        //  rounds, the fields empty, a point read there in one picture is no pair. With no pair in hand the
        //  reading before had that pair's own points above the field: for it, nothing besides it may be there now.
        //  Not above where pairs appear: a screen that shows a part of the 13th row showed a piece moving there as
        //  garbage fell, the point above it beyond the screen, in rounds drawn and read.)
        // (the window has moved on since the last pair appeared, or stands where it moves on to, no pair in hand: a pair is
        //  coming - just now, within pairGap frames)
        const bool movingOn = whole(t) && t.standKnown && t.standAt >= 0 && t.nextStill >= windowStill && moveOf(s) > 0;
        // (... or, two pairs alike in it, it has gone away from where it stood since the pair before came to rest and stands
        //  there again: it moved on to a third pair alike for the pair that comes - a pair that went down at once as it
        //  appeared, the reading of that frame missing, was never seen where pairs appear: on the real game a pair of 2P
        //  begun early so was not counted, and the pairs after it were numbered a place behind)
        const bool alikeAgainNow = whole(t) && t.standKnown && t.standAt >= 0 && t.nextStill >= windowStill && t.asStood && same(t.stand[0], t.stand[1]) &&
                                   t.dealt == t.pairs && t.changedAt > t.spawnedAt && t.awayFrom > t.spawnedAt + pairGap;
        const bool due = !t.inHand && (t.dealt > t.pairs || movingOn || alikeAgainNow);
        const bool justMoved = due && at - (t.dealt > t.pairs ? t.shiftedAt : t.stillAt) <= pairGap;
        // (... a pair moved a column aside as it appeared, its child still above the screen, is so too, just after the
        //  window moved on: a pair the driver began before it appeared does so - not taken, it was not followed on the real
        //  game, the engine had it where it was going, and it fell with nothing pressed for 46 frames)
        const bool aside = std::abs(fresh.at.x - spawn.x) <= (justMoved ? 1 : 0);
        const bool colours = knows(s, comes) && fresh.colors.axis == pairOf(s, comes).axis &&
                             (fresh.childHidden ? aside && fresh.at.y >= juraic::SPAWN_Y - 1 && fresh.at.y <= juraic::SPAWN_Y &&
                                                      (t.spawnedAt < 0 || at - t.spawnedAt >= 2 * pairGap || justMoved) && !resting.f.empty()
                                                : fresh.colors.child == pairOf(s, comes).child);
        const bool settledLate = at - t.changedAt >= stillFrames && (t.loose == 0 || (t.loose <= 4 && looseOver(field, resting, fresh) == 0));
        const bool samePair = t.inHand && resting == t.under && at - t.spawnedAt < pairGap;
        // (... and seen whole, in the air, a column at most beside where pairs appear and not far below, of the colours
        //  of the pair that comes, soon after the window moved on: turned or moved in the frame it appeared - a pair the
        //  driver began before it appeared does so -, it was never shown where pairs appear. Not taken, it was not counted,
        //  the window's move as it appeared was taken for the next pair coming, and the pair lay with no button for 10
        //  frames before it locked, in rounds drawn so. Its child above the screen: as `colours` says.)
        //  (Lying already when first seen so - on a column high near where pairs appear -, it is taken all the same.)
        const bool movedFirst = justMoved && std::abs(fresh.at.x - spawn.x) <= 1 && fresh.at.y >= juraic::SPAWN_Y - 3;
        const bool hidden = !there && !t.inHand && fresh.seen && colours && fresh.at.y >= juraic::SPAWN_Y - 3 &&
                            ((t.unclearAt >= 0 && at - t.unclearAt <= lateFrames && !pairLying(fresh, resting)) || movedFirst);
        // (the first pair of a round: its own pieces read in the reading before, when it was not taken, do not keep
        //  the field from standing still; seen there before as a pair of other colours than those learnt, it came then.
        //  Not once the window has moved on from the first two pairs: they were the round's, and a pair after the first
        //  may be seen - on rounds drawn so, the first put under a light, the pairs learnt were dropped for the second.)
        const bool firstOfRound = fromStart && comes == 0 && t.dealt == 0;
        // (seen there in the clear reading before and not taken - pieces above the field then, or other colours
        //  than learnt before the first pair of a round -, it came then: pieces of a chain do not stay there)
        const bool seenBefore = t.strayAt >= 0 && at - t.strayAt <= 2 && fresh.seen && t.strayAxis == fresh.colors.axis;
        // (... and the field as it was then: it stood for the two readings that showed it - the last piece of the pair
        //  before, split, came to rest in the first of them. Waited for a reading more, such a pair was not taken before a
        //  light on the real game, and nothing played it for 12 frames.)
        const bool restedSinceSeen = seenBefore && t.changedAt <= t.strayAt && (t.loose == 0 || (t.loose <= 4 && looseOver(field, resting, fresh) == 0));
        // (seen late, both pieces showing, 2 * pairGap frames or more after the last pair appeared: its own
        //  pieces read in the reading before do not keep the field from standing still either - on the real game
        //  such a pair, after garbage fell under a light, was taken for none to the end of the round)
        const bool longAfter = t.spawnedAt < 0 || at - t.spawnedAt >= 2 * pairGap;
        // (the window moves on before a pair appears: read standing still ever since the last pair appeared, no
        //  pair has come - a pair kept where pairs appear and turned over, its axis up in the 13th row, showed
        //  its child alone there, of the colour of the next pair, and was counted again in a round drawn. Read otherwise
        //  and then standing as it stood - a light passing over it - it has not moved, and its move that ended as the last
        //  pair appeared is not one since: on the real game pieces of a chain seen there were counted so, and the pairs
        //  after them numbered a place out. A window still moving, or one the last pair seen late left no time to tell,
        //  has moved as far as it reads; one standing where it moves on to, or taken as moved on, has moved; so has one of
        //  two pairs alike standing again, pairGap frames after the pair appeared, as it stood: it moves on so to a third.)
        const bool alikeAgain = whole(t) && t.standKnown && same(t.stand[0], t.stand[1]) && t.runFrom > t.spawnedAt + pairGap;
        const bool windowMoved = t.spawnedAt < 0 || (t.nextMovedAt >= t.spawnedAt && (t.late || t.nextStill < windowStill || t.dealt > t.pairs || movingOn || alikeAgain));
        // (first seen there pairGap frames or more after the window last moved, the screen not read clear since then and
        //  the pieces at rest changed since then: the pair that came has been put while the screen was lit - a piece drawn
        //  once there after the light, of its colour, was taken for it in a round drawn and read. Taken when seen there in
        //  two clear readings in a row. With the field as it was, a pair seen there once after a light, which then lay
        //  where it appeared, was the last of a round on the real game.)
        const bool overdue = !firstOfRound && at - t.nextMovedAt >= pairGap && t.unclearAt > t.nextMovedAt && t.changedAt > t.nextMovedAt;
        if (((((firstOfRound || seenBefore) ? settledLate || restedSinceSeen : settled) && there && (seenBefore || !overdue)) ||
             (((fresh.childHidden || longAfter) ? settledLate : settled) && hidden)) &&
            !samePair && windowMoved) {
            if (alikeOn) t.dealt = std::max(t.dealt, comes + 1), t.standAt = std::max(t.standAt, comes + 1);
            newPair(s, fresh, resting, field, seenBefore ? t.strayAt : at, hidden, alikeOn), late[s] = hidden;
        } else if (there && !samePair) {
            t.strayAt = at, t.strayAxis = fresh.colors.axis;
        } else if (((firstOfRound && knows(s, 0)) || due) && !fresh.seen) {
            // A pair of other colours than learnt where pairs appear - the first of a round (the window read on a
            // screen before the round), or one after the window moved on (a window read wrong, pairs learnt at the
            // wrong places): taken when seen there in two clear readings in a row with nothing else above the
            // field, as the pair that came in the first of them, of the colours read. For the first of a round
            // the pairs learnt are dropped; otherwise this one is learnt again. No pair is left alone for its
            // colours.
            const PairSeen any = readPair(field, resting, {});
            if (atSpawn(any) && at - t.changedAt >= stillFrames && looseOver(field, resting, any) == 0) {
                if (t.strayAt >= 0 && at - t.strayAt <= 2 && t.strayAxis == any.colors.axis) {
                    // (of the colours of the pair after the one that comes, the window showing the pair after that
                    //  first: the one that came was put unseen - under a light, the window moving on as this one
                    //  appeared read with its second place not read, so that it did not tell -, and this is the next)
                    const bool after = !firstOfRound && knows(s, comes + 1) && knows(s, comes + 2) && t.nextRead[0] && same(t.next[0], pairOf(s, comes + 2)) &&
                                       any.colors.axis == pairOf(s, comes + 1).axis && (any.childHidden || any.colors.child == pairOf(s, comes + 1).child);
                    if (after) {
                        t.dealt = std::max(t.dealt, comes + 2);
                    } else if (firstOfRound) {
                        for (int k = 0; k < 2; ++k) pairsOf[k].clear(), knownOf[k].clear(), strengthOf[k].clear();
                        shift = 0;
                    } else {
                        const juraic::Cell child = !any.childHidden ? any.colors.child : knows(s, comes) ? pairOf(s, comes).child : any.colors.axis;
                        // (the window standing since the last pair appeared numbers this one: the picture of it was read
                        //  wrong, or the pair turned as it appeared - its child read where its axis appears, the axis up
                        //  out of sight: on the real game 1P turned pairs so, and seven right pairs were learnt again wrong
                        //  in the records, six of them as the pair after them. Not so, the window was not read standing -
                        //  under a long light -: the colours of the pair after it seen, the count is behind, and the pair
                        //  that comes was put unseen; the colours of the pair learnt turned over, the other side's window
                        //  showing it alike or not at all, it turned.)
                        const int moved = whole(t) && t.nextStill >= windowStill && t.standKnown && t.standAt >= 0 ? moveOf(s) : -1;
                        const bool numbered = moved >= 0 && t.standAt + moved == comes + 1 && t.stillAt > t.spawnedAt;
                        const bool nextOne = knows(s, comes + 1) && any.colors.axis == pairOf(s, comes + 1).axis && (any.childHidden || any.colors.child == pairOf(s, comes + 1).child);
                        const int other = otherIndex(s, comes);
                        const bool otherLearnt = other >= 0 && other < int(knownOf[1 - s].size()) && knownOf[1 - s][size_t(other)];
                        const bool turned = knows(s, comes) && any.colors.axis == pairOf(s, comes).child &&
                                            (any.childHidden || any.colors.child == pairOf(s, comes).axis) && (shownAlike(s, comes) || !otherLearnt);
                        // (shown alike by both windows standing, the pair is as learnt: it is the picture of it that was
                        //  read wrong - on the real game right pairs were learnt again so)
                        // (nor a pair learnt for sure from the window standing, which shows it more readings than it
                        //  takes to learn it otherwise from the window, windowOver - every pair learnt so, as it is
                        //  learnt only after the window has stood pairGap frames -, the other side's window not having
                        //  shown it otherwise: a picture of the pair as it appears is one reading, and on the real game
                        //  the red mark drawn above the third column was read as its axis, a pair the window had shown
                        //  for 21 readings, the other window not yet, learnt again so; in 37 records this was the one
                        //  pair learnt again here. Two windows showing it otherwise, the picture tells which.)
                        const bool stoodLong = learnt(s, comes) && knownOf[s][size_t(comes)] == 1 && strength(s, comes) > windowOver && !otherLearnt;
                        if (!numbered && !turned && nextOne) t.dealt = std::max(t.dealt, comes + 2);
                        else if (!turned && !(nextOne && numbered) && !shownAlike(s, comes) && !stoodLong) learn(s, comes, {any.colors.axis, child});
                    }
                    if (!after) ++doubts;
                    newPair(s, any, resting, field, t.strayAt, false);
                } else {
                    t.strayAt = at, t.strayAxis = any.colors.axis;
                }
            }
        }
    }
    // Before the first pair of a round: the window shows the first two pairs (the one that stood last: windowLong). The
    // round taken to begin in the reading the windows began to slide in, as the first pair comes (the fields empty for
    // roundGap frames only then), they are those the window showed standing in the reading before - the other side's,
    // read standing now or then, alike. (Not learnt so on the real game, the first pair of 2P, its child above the screen
    // as it appeared, was known to neither side: the AI waited for its colours, and it fell by itself where it appeared;
    // the person's first pair, moved as it appeared, was not counted.)
    void learnFirstWindow() {
        if (!fromStart || side[0].pairs || side[1].pairs) return;
        const auto stands = [&](const SideTrack& t) { return whole(t) && t.nextStill >= windowStill; };
        const auto stoodBefore = [&](const SideTrack& t) { return !stands(t) && t.stoodAt >= 0 && t.stoodAt == roundBefore; };
        if (!knows(0, 0) && stoodBefore(side[0])) {
            const juraic::Pair a = side[0].stood[0], b = side[0].stood[1];
            const SideTrack& o = side[1];
            const juraic::Pair* other = stands(o) ? o.next : stoodBefore(o) ? o.stood : nullptr;
            if (other && !(same(a, other[0]) && same(b, other[1]))) {
                ++doubts;
            } else {
                for (int k = 0; k < 2; ++k) learn(k, 0, a), learn(k, 1, b);
                standFirst(a, b, true);
            }
            return;
        }
        if (side[0].nextRead[0] && side[0].nextRead[1] && side[0].nextStill >= windowStill && (!side[1].nextRead[0] || !side[1].nextRead[1] || side[1].nextStill >= windowStill)) {
            const juraic::Pair a = side[0].next[0], b = side[0].next[1];
            if (side[1].nextRead[0] && side[1].nextRead[1] && !(same(a, side[1].next[0]) && same(b, side[1].next[1]))) {
                if (!knows(0, 0)) ++doubts;
            } else if (!knows(0, 0)) {
                for (int k = 0; k < 2; ++k) learn(k, 0, a), learn(k, 1, b);
                standFirst(a, b);
            } else if (side[0].nextStill >= windowLong && !(same(pairOf(0, 0), a) && knows(0, 1) && same(pairOf(0, 1), b))) {
                if (knows(0, 1) && same(pairOf(0, 1), a) && side[0].nextStill < roundGap)
                    for (int k = 0; k < 2; ++k) learn(k, 2, b);
                else {
                    for (int k = 0; k < 2; ++k) pairsOf[k].assign({a, b}), knownOf[k].assign(2, 1), strengthOf[k].clear();
                    shift = 0;
                    standFirst(a, b);
                }
            }
        }
    }
    // Both windows stand before the first pair of the round, at its first two pairs (`before`: as they stood in the reading
    // before the round was taken - from the first reading that showed them so).
    void standFirst(juraic::Pair a, juraic::Pair b, bool before = false) {
        for (SideTrack& t : side)
            if (t.standAt <= 0) {
                t.stand[0] = a, t.stand[1] = b, t.standKnown = true, t.standAt = 0, t.dealt = 0;
                t.standFrom = before && t.stoodAt == roundBefore ? t.stoodFrom : t.runFrom;
                t.asStood = whole(t) && same(t.next[0], a) && same(t.next[1], b);
            }
    }
    int64_t emptySince = -1;
    int fullFor = 0; // readings in a row in which the fields were not empty

    static bool same(juraic::Pair a, juraic::Pair b) { return a.axis == b.axis && a.child == b.child; }
    // The number of the pair that comes on side t: the one the window moved on for last, not seen yet, or else the one
    // after the pair taken last.
    static int comesOf(const SideTrack& t) { return t.dealt > t.pairs ? t.dealt - 1 : t.pairs; }
    // Points that show a piece above the field the pair moves over (or above the pieces at rest), the pair's own left out.
    static int looseOf(const SideRead& read, const SideTrack& t) { return looseOver(read, t.inHand ? t.under : t.resting, t.pair); }
    // ... above `base`, those of `pair` (if seen) left out.
    static int looseOver(const SideRead& read, const juraic::GameField& base, const PairSeen& pair) {
        const PairPoints own(pair);
        int h[juraic::W], n = 0;
        base.heights(h);
        for (int x = 0; x < juraic::W; ++x)
            for (int k = 2 * h[x]; k < POINTS; ++k) n += isPiece(read.point[x][k]) && !own.covers(x, k);
        return n;
    }
    // The words (wordsFrames), from a clear reading as it was read and the pieces at rest it shows without them: a point
    // that reads nothing is no longer the words and counts from nothing; a point in the air that reads a piece with an
    // empty point under it, the pair seen left out, counts one more reading of that piece.
    void takeWords(SideTrack& t, const SideRead& read, const juraic::GameField& resting) const {
        Words& w = t.words;
        int h[juraic::W];
        resting.heights(h);
        const PairPoints own(t.pair);
        int standing = 0;
        for (int x = 0; x < juraic::W; ++x) {
            bool gap = false;
            for (int k = 0; k < POINTS; ++k) {
                const juraic::Cell c = read.point[x][k];
                // (a point that was the words in this round, reading what it read then: with no empty point under it
                //  too - the pieces under the words come right up to them -, and taken alone)
                const bool again = w.was[x][k] != juraic::EMPTY && c == w.was[x][k];
                const bool air = k >= 2 * h[x] && (gap || again) && isPiece(c) && !own.covers(x, k);
                gap = gap || (k >= 2 * h[x] && c == juraic::EMPTY);
                // (the pair seen, or pieces at rest: something in front of it, neither counted nor counted from again;
                //  another piece than it counts is taken instead when it has read so in more readings in a row than the
                //  piece counted in all - the words settle so as they come up - and is else something passing in front
                //  of it: a pair covers a point for 52 frames at most)
                if (c == juraic::EMPTY) {
                    w.at[x][k] = juraic::EMPTY, w.stillFor[x][k] = w.lastFor[x][k] = 0;
                } else if (air) {
                    w.lastFor[x][k] = c == w.lastAs[x][k] && w.lastFor[x][k] ? w.lastFor[x][k] + 1 : 1, w.lastAs[x][k] = c;
                    if (c == w.stillAs[x][k] && w.stillFor[x][k]) ++w.stillFor[x][k];
                    else if (w.lastFor[x][k] > w.stillFor[x][k]) w.stillAs[x][k] = c, w.stillFor[x][k] = w.lastFor[x][k];
                }
                standing += w.stillFor[x][k] >= wordsFrames;
            }
        }
        w.any = false;
        for (int x = 0; x < juraic::W; ++x)
            for (int k = 0; k < POINTS; ++k) {
                if (w.stillFor[x][k] >= wordsFrames && (standing >= wordsPoints || (w.was[x][k] != juraic::EMPTY && w.stillAs[x][k] == w.was[x][k])))
                    w.at[x][k] = w.was[x][k] = w.stillAs[x][k];
                w.any = w.any || w.at[x][k] != juraic::EMPTY;
            }
    }
    bool learnt(int s, int index) const { return index >= 0 && index < int(knownOf[s].size()) && knownOf[s][size_t(index)]; }
    // Pair `index` of side s was learnt from both windows standing, as one pair.
    bool shownAlike(int s, int index) const {
        const int other = otherIndex(s, index);
        return index >= 0 && index < int(knownOf[s].size()) && knownOf[s][size_t(index)] == 1 && other >= 0 && other < int(knownOf[1 - s].size()) && knownOf[1 - s][size_t(other)] == 1 &&
               same(pairsOf[s][size_t(index)], pairsOf[1 - s][size_t(other)]);
    }
    // The pair `index` is p, as side s's window shows it: read from a window that stood still (sure), or that may have
    // been moving - taken until the window stands still, never over a pair learnt so.
    void learn(int s, int index, juraic::Pair p, bool sure = true) {
        if (index < 0) return;
        std::vector<juraic::Pair>& pairs = pairsOf[s];
        std::vector<uint8_t>& known = knownOf[s];
        if (int(known.size()) <= index) pairs.resize(size_t(index) + 1), known.resize(size_t(index) + 1, 0), learntAt[s].resize(size_t(index) + 1, -1);
        uint8_t& k = known[size_t(index)];
        if (k == 1 && sure && !same(pairs[size_t(index)], p)) ++doubts, ++relearnt[s], relearntFrom[s] = learntAt[s][size_t(index)];
        if (k == 1 && !sure) return;
        const bool changed = k != 1 || !same(pairs[size_t(index)], p);
        pairs[size_t(index)] = p, k = sure ? 1 : 2;
        if (!sure || !changed) return;
        learntAt[s][size_t(index)] = frame;
        const int other = otherIndex(s, index);
        if (other >= 0 && other < int(knownOf[1 - s].size()) && knownOf[1 - s][size_t(other)] == 1 && !same(pairsOf[1 - s][size_t(other)], p))
            ++unlike, unlikeFrom = learntAt[1 - s][size_t(other)];
        strengthAt(s, index) = 1;
        align();
    }
    void newRound() {
        ++rounds;
        for (SideTrack& t : side)
            t.pairs = t.dealt = t.unseen = 0, t.spawnedAt = t.spawnedBefore = -1, t.inHand = false, t.strayAt = -1, t.shiftedAt = -1, t.provisional = t.asStood = false, t.awayFrom = -1, t.standKnown = false, t.standAt = t.anchorFor = -1,
            t.words = Words{};
        for (int k = 0; k < 2; ++k) pairsOf[k].clear(), knownOf[k].clear(), learntAt[k].clear(), strengthOf[k].clear();
        shift = 0;
        roundStart = -1;
        roundBefore = frameBefore;
        fromStart = true;
    }
    // A new pair seen where pairs appear on side s (seenLate: hidden as it appeared, and seen after). The window moving on
    // as it appears tells so too: read standing where it moves on to, the move is taken with the pair (windowMove not
    // waited for); two pairs alike in it, slid (away windowSlide frames or more) and standing as it stood just before the
    // pair appeared, long after it moved on for a pair not seen, the field changed since - that one put unseen -, it moved
    // on for this one (unless that move was taken already, `movedAlike`: lookForPair, the pair seen of the colours of the one
    // after). The pair is the one the window moved on for last, or, no move of it seen, the one after the pair taken last.
    // (Taken twice so, on the real game a pair of 1P after one put unseen under a light was numbered a place ahead.)
    void newPair(int s, const PairSeen& fresh, const juraic::GameField& resting, const SideRead& read, int64_t at, bool seenLate = false, bool movedAlike = false) {
        SideTrack& t = side[s];
        if (whole(t) && t.standKnown && t.standAt >= 0 && !seenLate) {
            const int k = moveOf(s);
            if (k > 0 && t.nextStill >= 1) moveOn(s, k, t.nextStill >= windowStill ? t.stillAt : at);
            else if (k == 0 && !movedAlike && t.asStood && same(t.stand[0], t.stand[1]) && t.dealt > t.pairs && t.runFrom >= at - windowBefore &&
                     t.runFrom - t.awayFrom >= windowSlide && t.awayFrom > t.shiftedAt + pairGap && t.changedAt > t.shiftedAt)
                moveOn(s, 1, at);
            // (... and, no pair to come, away since the pair before was taken: it moved on for this one to a third pair
            //  alike - not taken so, the pair after the next was not learnt as the pair appeared, in rounds drawn so)
            else if (k == 0 && t.asStood && same(t.stand[0], t.stand[1]) && t.dealt == t.pairs && t.runFrom > t.spawnedAt)
                moveOn(s, 1, at);
        }
        const int index = comesOf(t);
        t.pairs = index + 1, t.dealt = std::max(t.dealt, t.pairs), t.missedFor = 0;
        t.spawnedBefore = t.spawnedAt;
        t.spawnedAt = at;
        t.seenAxis = fresh.colors.axis;
        t.late = seenLate;
        t.inHand = true;
        t.under = resting;
        t.pair = t.lastSeen = fresh;
        t.lastSeenAt = at;
        t.colors = fresh.colors;
        if (knows(s, index)) {
            if (fresh.colors.axis != pairOf(s, index).axis) ++doubts;
            t.colors = t.pair.colors = t.lastSeen.colors = pairOf(s, index);
        } else if (fresh.colors.axis && fresh.colors.child && !fresh.childHidden) {
            // (no window showed it - lit as it stood: the pair itself, seen whole as it appears, tells its colours, as
            //  a window that may have been moving does; not known, the AI would wait for it: observeWith)
            learn(s, index, fresh.colors, false);
        }
        // (the two pairs after it, as the window standing at its number shows them - as it may be moving, until it has
        //  stood windowLearn readings: learnStand. A window whose number is not known is numbered after this pair: anchor.)
        if (t.standAt == index + 1 && whole(t) && moveOf(s) == 0) {
            learnStand(s);
        } else if (t.standAt < 0) {
            for (int n = 0; n < 2; ++n)
                if (read.nextRead[n]) learn(s, index + 1 + n, read.next[n], false);
            t.anchorFor = index;
        }
        spawned[s] = true;
        // The first pair of a round is seen in its frame firstSpawn (game/player.h startPlayer).
        if (index == 0 && roundStart < 0) roundStart = at - timing.firstSpawn;
    }
};

// What a side's events (Tracker::Event) have told, taken in their order (take): the frame the last pair appeared in and
// whether it was seen late, the frame the window last moved on (or its move was taken back to), the frame it last went
// away from where it stood. What the driver and Follower keep of them, rather than reading SideTrack's frames, whose
// meaning is the tracker's own.
struct SideSeen {
    int64_t appearedAt = -1, movedAt = -1, awayAt = -1;
    bool late = false;
    // The events of side s since the last take, in their order (a tracker made anew: from its first).
    void take(const Tracker& t, int s) {
        if (t.id != of) *this = SideSeen{}, of = t.id;
        const std::deque<Tracker::Event>& events = t.events[s];
        const uint64_t kept = t.told[s] - events.size();
        for (uint64_t i = std::max(taken, kept); i < t.told[s]; ++i) {
            const Tracker::Event& e = events[size_t(i - kept)];
            if (e.kind == Tracker::Event::APPEARED) appearedAt = e.frame, late = e.late;
            else if (e.kind == Tracker::Event::MOVED) movedAt = e.frame;
            else if (e.kind == Tracker::Event::AWAY) awayAt = e.frame;
        }
        taken = t.told[s];
    }
    // ... as SideTrack has them (always, the events taken to the last: the tests check it)
    bool asTracker(const SideTrack& s) const { return appearedAt == s.spawnedAt && late == s.late && movedAt == s.shiftedAt && awayAt == s.awayFrom; }
    // The window has moved on since the last pair appeared, more than `gap` frames after (Tracker::pairGap: then it moved
    // on for the next pair, not as the last one appeared).
    bool movedForNext(int gap) const { return movedAt > appearedAt + gap; }
    // ... and gone away from where it stood so.
    bool awayForNext(int gap) const { return awayAt > appearedAt + gap; }

private:
    uint64_t of = 0, taken = 0; // (the tracker taken from, by Tracker::id, and how many of the side's events)
};

// What the AI of seat `me` is shown in the frame in which its new pair was seen (Tracker::spawned[me]): the own side
// as it is then in the game (phase Next, one frame left; a pair seen late, Tracker::late, in play where it is), the other side as far as the picture tells, and the pairs
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
//
// observeWith: the same with the own side as the caller follows it (`own`, the engine's player) at the frame `at` of
// the clock; null: as observeSpawn shows it.
inline void observeWith(const Tracker& t, int me, const juraic::Rules& rules, const juraic::Player* own, int64_t at, juraic::ai::Observation& o) {
    using namespace juraic;
    o = ai::Observation{};
    o.rules = rules;
    o.timing = t.timing;
    o.players = 2;
    o.me = me;
    o.frame = t.roundFrame(at);
    o.state = RoundState::Playing;
    // The colours in play.
    // (the pairs either side has learnt: those both sides know of, as each side's own window or the other's showed them)
    bool seen[8]{};
    for (int s = 0; s < 2; ++s)
        for (size_t i = 0; i < t.knownOf[s].size(); ++i)
            if (t.knownOf[s][i]) seen[t.pairsOf[s][i].axis] = seen[t.pairsOf[s][i].child] = true;
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
        if (s == me && own) {
            const Incoming in = p.incoming;
            p = *own;
            p.incoming = in;
        } else if (s == me && t.late[me]) {
            // (seen late: in play already, where it was seen)
            p.field = side.under;
            p.phase = Phase::Control;
            p.pair = side.lastSeen.at;
            p.colors = side.colors;
            p.moves = side.pairs - 1;
        } else if (s == me) {
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
            // (counted on from the reading to the frame `at`, not past the end of the landing - a chain may begin there,
            //  which the pictures tell - nor past the frame before the next pair)
            const int since = int(t.frame - side.changedAt), ahead = int(std::max<int64_t>(0, at - t.frame));
            p.phase = since < t.timing.landFrames ? Phase::Land : Phase::Next;
            p.wait = since < t.timing.landFrames ? std::max(1, t.timing.landFrames - since - ahead) : std::max(1, t.timing.landFrames + t.timing.nextFrames - since - ahead);
        } else {
            p.field = fallenField(t.last.side[s]);
            p.phase = Phase::Land;
            p.wait = 1;
        }
    }
    // The pairs shown (one not known, of a round not followed from its beginning: of the first colour in play), and no
    // other: before the pair of `me` appears the window shows it and the one after it; the pair after those is learnt
    // from the window when it has appeared, and the AI is then asked for its second choice (PlacementAgent::again).
    // The sides can have counted their pairs differently. Keep their own window readings separate in the
    // observation too; pairOf borrows from the other side only where this side has no reading of its own.
    // A side's pairs are known (SeenTsumo: the first count() of them) from its pair in play, or the one it is to take
    // next, on as far as they come one after the other: up to the first one neither window showed (a light over it, the
    // window not read standing). Its pairs before that one stand for pairs already played, of the first colour in play
    // where not known. (Filled in with that colour, a pair not read was taken for a known pair of one colour, and the
    // pairs known after it were counted too.) The own pair not known, none is: the AI waits for it (ai::PlacementAgent).
    o.ownTsumo = true;
    for (int s = 0; s < 2; ++s) {
        ai::SeenTsumo& pairs = s == 0 ? o.tsumo : o.tsumo1;
        int count = std::max(0, o.player[s].nextPair - (o.player[s].phase == Phase::Control ? 1 : 0));
        while (t.knows(s, count)) ++count;
        pairs.pairs.assign(size_t(count), Pair{o.rules.palette[0], o.rules.palette[0]});
        for (int i = 0; i < count; ++i)
            if (t.knows(s, i)) pairs.pairs[size_t(i)] = t.pairOf(s, i);
    }
}

inline bool observeSpawn(const Tracker& t, int me, const juraic::Rules& rules, juraic::ai::Observation& o) {
    if (me < 0 || me > 1 || !t.spawned[me]) return false;
    observeWith(t, me, rules, nullptr, t.frame, o);
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
