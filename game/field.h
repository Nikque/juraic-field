#pragma once
// The field of one player, and what one move does to it.
//
// GameField = the bitboard of core/ (rows 1..13) plus the 14th row.
// A piece put in the 14th row never falls and never clears; it stays as an
// obstacle until the round ends, and nothing else can be put in that cell.
// BitField::drop() compacts whole columns, so the 14th row is kept outside it.
//
// A placement is where the pair comes to rest: the column of the axis piece and
// the direction of the child piece seen from the axis. There are 22 of them.
#include "../core/bit_field.h"
#include "../core/reach.h"
#include "rules.h"
#include <array>

namespace juraic {

constexpr int ROW_14 = 13; // row index of the 14th row
constexpr int DEATH_X = 2, DEATH_Y = 11; // third column, twelfth row

struct GameField {
    BitField f;
    std::array<Cell, W> row14{}; // colour of the piece in the 14th row, 0 = none

    bool operator==(const GameField& o) const { return f == o.f && row14 == o.row14; }
    // Column heights. The field must be settled (no holes).
    void heights(int h[W]) const { f.heights(h); }
    bool dead() const { return f.occupied().get(DEATH_X, DEATH_Y); }
};

// ---- placements ---------------------------------------------------------------

enum Direction : uint8_t { UP, RIGHT, DOWN, LEFT }; // where the child piece is

constexpr int PLACEMENTS = 22;
struct Placement {
    uint8_t x;   // column of the axis piece
    uint8_t dir; // Direction
    constexpr int childX() const { return dir == RIGHT ? x + 1 : dir == LEFT ? x - 1 : x; }
};
// id 0..5 UP, 6..10 RIGHT (axis in column id-6), 11..16 DOWN, 17..21 LEFT (axis in column id-16).
constexpr Placement placement(int id) {
    return id < 6    ? Placement{uint8_t(id), UP}
           : id < 11 ? Placement{uint8_t(id - 6), RIGHT}
           : id < 17 ? Placement{uint8_t(id - 11), DOWN}
                     : Placement{uint8_t(id - 16), LEFT};
}
constexpr int placementId(Placement p) {
    return p.dir == UP ? p.x : p.dir == RIGHT ? 6 + p.x : p.dir == DOWN ? 11 + p.x : 16 + p.x;
}

// Bit id is set when placement id can be made on this field.
//
// Built on core/reach.h (which column the pair can travel to). On top of it:
//  - the axis piece never rests in the 14th row, the child piece may, once per
//    column: a vertical pair with the child on top may stand on a 12-high column;
//  - with the child below, the column must be 11 high or lower.
// This is the move list for the AI; the frame-by-frame controller is what
// decides for a human player, and the tests compare the two.
inline unsigned legalPlacements(const GameField& g) {
    int h[W];
    g.heights(h);
    const unsigned pairs = reachablePairs(h);
    unsigned result = 0;
    for (int x = 0; x < W; ++x) {
        if (pairs >> x & 1) result |= 1u << x | 1u << (11 + x);
        else if (h[x] == 12 && !g.row14[x] && columnReachable(h, x)) result |= 1u << x;
    }
    for (int x = 0; x + 1 < W; ++x)
        if (pairs >> (W + x) & 1) result |= 1u << (6 + x) | 1u << (17 + x);
    return result;
}

struct PlaceResult {
    int axisY = 0, childY = 0; // rows where the pieces came to rest (ROW_14 = 14th row)
    int lockY = 0;             // row of the axis piece when the pair locked (before a split)
    int split = 0;             // cells one piece fell after the other had landed
};

namespace detail {
inline void putCell(GameField& g, int x, int y, Cell code) {
    if (y >= ROW_14) {
        g.row14[size_t(x)] = code;
        return;
    }
    const FieldBits bit = FieldBits::cell(x, y);
    for (int i = 0; i < 3; ++i)
        if (code >> i & 1) g.f.plane[size_t(i)] |= bit;
}
} // namespace detail

// Puts the pair on a settled field. The placement must be legal.
inline PlaceResult place(GameField& g, Placement p, Pair pair) {
    const FieldBits occ = g.f.occupied();
    const int ax = p.x, cx = p.childX();
    const int ha = detail::countBits(occ.column(ax)), hc = detail::countBits(occ.column(cx));
    PlaceResult r;
    if (p.dir == UP) {
        r.axisY = r.lockY = ha, r.childY = ha + 1;
    } else if (p.dir == DOWN) {
        r.childY = ha, r.axisY = r.lockY = ha + 1;
    } else {
        r.axisY = ha, r.childY = hc;
        r.lockY = ha > hc ? ha : hc;
        r.split = ha > hc ? ha - hc : hc - ha;
    }
    detail::putCell(g, ax, r.axisY, pair.axis);
    detail::putCell(g, cx, r.childY, pair.child);
    return r;
}

// ---- waves with any clear count -----------------------------------------------

// Group bonus when the clear count is not four: below four the table starts at
// the clear count, above four it is unchanged (the wiki of [A] in game/timing.h, Scoring).
constexpr int groupBonusFor(int size, int clearCount) {
    return groupBonus(clearCount < 4 ? size + 4 - clearCount : size);
}

// findWave of core/ for any clear count. Four uses the fast path of core/.
inline bool findWaveFor(const BitField& f, Wave& w, int clearCount, bool details = true) {
    if (clearCount == 4) return findWave(f, w, details);
    const Links k = f.links();
    w = {};
    bool colorSeen[8]{};
    FieldBits rest = f.normal() & MASK_12;
    while (rest.any()) {
        uint16_t c[W];
        rest.columns(c);
        int x = 0;
        while (!c[x]) ++x;
        const int y = std::countr_zero(unsigned(c[x]));
        const FieldBits g = BitField::group(FieldBits::cell(x, y), k);
        rest = rest.without(g);
        const int size = g.count();
        if (size < clearCount) continue;
        w.cleared |= g;
        ++w.groups;
        w.groupBonus += groupBonusFor(size, clearCount);
        const Cell color = f.get(x, y);
        if (!colorSeen[color]) colorSeen[color] = true, ++w.colors;
    }
    if (w.cleared.empty()) return false;
    w.garbage = f.garbageNextTo(w.cleared);
    w.pieces = w.cleared.count();
    return true;
}

// Score of one wave. Unlike waveScore of core/ the multiplier is capped at 999
// and the chain bonus goes on beyond the 19th chain (32 more per chain).
constexpr int chainBonus(int chain) {
    return chain < 1 ? 0 : chain <= 19 ? CHAIN_BONUS[chain] : CHAIN_BONUS[19] + 32 * (chain - 19);
}
inline int gameWaveScore(const Wave& w, int chain) {
    const int bonus = chainBonus(chain) + COLOR_BONUS[w.colors] + w.groupBonus;
    return 10 * w.pieces * (bonus < 1 ? 1 : bonus > 999 ? 999 : bonus);
}

// The longest distance a piece falls once `erased` is removed from the settled
// field `before` (0 = nothing falls: a "quick" wave).
inline int longestFall(const BitField& before, FieldBits erased) {
    uint16_t occ[W], gone[W];
    before.occupied().columns(occ);
    erased.columns(gone);
    int longest = 0;
    for (int x = 0; x < W; ++x) {
        const unsigned left = occ[x] & ~gone[x];
        if (!gone[x] || !left) continue;
        // erased cells below the highest remaining piece of the column
        const unsigned below = (1u << std::bit_width(left)) - 1;
        const int fall = std::popcount(gone[x] & below);
        if (fall > longest) longest = fall;
    }
    return longest;
}

// Adds `count` garbage pieces on top of column x; those above the 13th row are lost.
// Returns the number actually added.
inline int addGarbage(GameField& g, int x, int count) {
    const int h = std::popcount(g.f.occupied().column(x));
    const int room = H - h;
    const int n = count < room ? count : room;
    if (n <= 0) return 0;
    uint16_t c[W]{};
    c[x] = uint16_t(((1u << n) - 1) << h);
    const FieldBits bits = FieldBits::fromColumns(c);
    g.f.plane[1] |= bits;
    g.f.plane[2] |= bits;
    return n;
}

} // namespace juraic
