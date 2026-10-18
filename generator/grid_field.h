#pragma once
// A field of any size up to 32 rows, for the generator's larger boards.
//
// The 6 x 13 field of the game, the AI and the 6 x 13 generator is
// core/bit_field.h (one SSE register a plane). This type keeps one 32-bit word
// per column and plane instead, so that the same rules work on wider and
// taller fields: bit y of column x is row y counted from the bottom; rows
// 0..CLEAR_H-1 connect and clear, the rows above them up to H-1 hold pieces
// that never connect but fall when the pieces below them disappear.
//
// A geometry G gives
//   W, H, CLEAR_H   columns, rows a piece can rest in, rows that clear;
//   SPAWN_X         the column the falling pair appears in;
//   DEATH_COLUMNS   bit x: the game is over when column x reaches row CLEAR_H
//                   (the cell of row CLEAR_H in that column must stay free);
//   GUIDE_MIN_PIECES, GUIDED_STEPS  the guidance of generator.cpp on nearly
//                   full boards (grid_generator.h);
//   PLAN_*          the plan of grid_generator.h for boards that leave at most
//                   PLAN_MAX_SLACK cells of maxPieces() unused (-1: never used):
//                   PLAN_STEPS last steps near the death columns, within
//                   PLAN_DISTANCE columns of them; PLAN_RAMP percent: how far
//                   ahead the other columns are filled; PLAN_GREEDY: which of
//                   the new boards are kept (0 at random, 1 the least room away
//                   from the death columns before the last steps, 2 the least
//                   room weighted by (distance + 1)^2 at every step);
//                   PLAN_FAR_FIRST_EXTRA: from this many extra pieces on, they
//                   are put into the columns farthest from the death columns first;
//                   PLAN_LIVE: boards on which the next step can insert nothing
//                   are dropped; PLAN_BUMP: added to the room for each step
//                   in height between neighbouring columns when boards are kept;
//                   PLAN_END_BEAM times as many boards in the last PLAN_END_STEPS steps;
//   ThreeColors     the geometry whose plan is used for three colours.
#include "../core/bit_field.h"
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>

namespace juraic {

// The 6 x 13 field (the same rules as core/), used to compare this type and the
// template generator with the 6 x 13 engine.
struct Grid6x13 {
    using ThreeColors = Grid6x13; // the geometry used for three colours (grid_generator.h)
    static constexpr int W = 6, H = 13, CLEAR_H = 12, SPAWN_X = 2;
    static constexpr unsigned DEATH_COLUMNS = 1u << 2;
    static constexpr int GUIDE_MIN_PIECES = 72, GUIDED_STEPS = 6;
    static constexpr int PLAN_MAX_SLACK = -1, PLAN_STEPS = 0, PLAN_DISTANCE = 0, PLAN_RAMP = 100, PLAN_GREEDY = 0, PLAN_FAR_FIRST_EXTRA = 0, PLAN_LIVE = 0, PLAN_BUMP = 0, PLAN_END_STEPS = 0, PLAN_END_BEAM = 1;
};
// 10 columns, 18 rows that clear and one hidden row above them. Pairs appear
// in the fifth column; the fifth and sixth columns must not reach the 18th row.
//
// The values of the plan were measured with tools/grid_bench.cpp (4 colours,
// beam 48, 2 candidates, attempts that find a board):
//  - 47 chains: without a plan 0 of 300; the guidance of generator.cpp 0 of 300;
//    the plan (12 steps, distance 2, PLAN_RAMP 60) with PLAN_GREEDY 0 / 1 / 2:
//    7 / 3 / 53 of 200; PLAN_GREEDY 2 with PLAN_RAMP 100: 50 of 200.
//  - PLAN_STEPS 8 / 12 / 14 (PLAN_GREEDY 0, PLAN_RAMP 60): 3 / 7 / 5 of 200;
//    PLAN_GREEDY 2, PLAN_RAMP 100, PLAN_STEPS 8 / 12: 43 / 50 of 200;
//    PLAN_DISTANCE 1 (6 steps) / 2 / 3: 16 / 50 / 52 of 200.
//  - PLAN_MAX_SLACK 8 / 20 / 40: 42 chains 25 / 56 / 56, 44 chains 8 / 60 / 60,
//    40 chains (slack 28) 54 / 54 / 29 of 100.
//  - extra pieces put far first / at random: 20 chains + 108: 41 / 0,
//    27 + 80: 15 / 2, 28 + 76: 6 / 7, 36 + 44: 6 / 42, 40 + 28: 22 / 59 of 200.
struct Grid10x19ThreeColors;
struct Grid10x19 {
    using ThreeColors = Grid10x19ThreeColors;
    static constexpr int W = 10, H = 19, CLEAR_H = 18, SPAWN_X = 4;
    static constexpr unsigned DEATH_COLUMNS = (1u << 4) | (1u << 5);
    static constexpr int GUIDE_MIN_PIECES = 1 << 20, GUIDED_STEPS = 0;
    static constexpr int PLAN_MAX_SLACK = 20, PLAN_STEPS = 12, PLAN_DISTANCE = 2, PLAN_RAMP = 100, PLAN_GREEDY = 2,
                         PLAN_FAR_FIRST_EXTRA = 78, PLAN_LIVE = 0, PLAN_BUMP = 0, PLAN_END_STEPS = 0, PLAN_END_BEAM = 1;
};
// The plan for three colours on 10 x 19. With three colours few insertions find
// a colour that none of their neighbours has; the plan of Grid10x19 leaves deep
// narrow gaps that only upright groups of four could fill and boards on which
// nothing can be inserted. Measured as above (3 colours unless said otherwise):
//  - 46 chains: Grid10x19 0 of 300; + PLAN_LIVE 1 of 300; + PLAN_BUMP 10: 7, with
//    PLAN_DISTANCE 4: 11; PLAN_BUMP 20: 35; + PLAN_END_STEPS 6 / PLAN_END_BEAM 4: 42;
//    PLAN_BUMP 40 / 60 / 100 / 150 / 250 / 500: 73 / 99 / 119 / 117 / 119 / 113 of 300.
//  - 47 chains: Grid10x19 0 of 100; these values 23 of 300 (0.18 s an attempt).
//  - 44 chains: 4 of 200 (0.85 a second) / 180 of 200 (5.3 a second).
//  - in four colours these values find more boards an attempt but fewer a second
//    (47 chains: 8.3 / 2.9 a second), so they are used for three colours only.
struct Grid10x19ThreeColors : Grid10x19 {
    using ThreeColors = Grid10x19ThreeColors;
    static constexpr int PLAN_DISTANCE = 4, PLAN_LIVE = 1, PLAN_BUMP = 250, PLAN_END_STEPS = 6, PLAN_END_BEAM = 4;
};

// The most pieces a valid board can hold: every column full, except that the
// columns of DEATH_COLUMNS stop one row below CLEAR_H until the last pair is
// placed, which adds two pieces. 78 for 6 x 13 (all cells) and 188 for 10 x 19.
template <class G> constexpr int maxPieces() {
    const int death = std::popcount(G::DEATH_COLUMNS);
    const int beforePair = (G::W - death) * G::H + death * (G::CLEAR_H - 1);
    return beforePair + 2 < G::W * G::H ? beforePair + 2 : G::W * G::H;
}
template <class G> constexpr int maxChain() { return maxPieces<G>() / 4; }

template <class G> struct GridBits {
    static constexpr int W = G::W;
    std::array<uint32_t, W> c{};

    static constexpr uint32_t COLUMN_ALL = (1u << G::H) - 1;
    static constexpr uint32_t COLUMN_CLEAR = (1u << G::CLEAR_H) - 1;
    static GridBits fill(uint32_t column) {
        GridBits b;
        b.c.fill(column);
        return b;
    }
    static GridBits cell(int x, int y) {
        GridBits b;
        b.c[x] = 1u << y;
        return b;
    }
    uint32_t column(int x) const { return c[x]; }
    void columns(uint32_t out[W]) const { std::memcpy(out, c.data(), sizeof c); }
    bool get(int x, int y) const { return c[x] >> y & 1; }

    bool empty() const {
        uint32_t any = 0;
        for (uint32_t v : c) any |= v;
        return !any;
    }
    bool any() const { return !empty(); }
    bool disjoint(const GridBits& o) const {
        uint32_t both = 0;
        for (int x = 0; x < W; ++x) both |= c[x] & o.c[x];
        return !both;
    }
    bool subsetOf(const GridBits& o) const {
        uint32_t outside = 0;
        for (int x = 0; x < W; ++x) outside |= c[x] & ~o.c[x];
        return !outside;
    }
    int count() const {
        int n = 0;
        for (uint32_t v : c) n += std::popcount(v);
        return n;
    }
    bool operator==(const GridBits& o) const { return c == o.c; }

    GridBits operator&(const GridBits& o) const { GridBits r; for (int x = 0; x < W; ++x) r.c[x] = c[x] & o.c[x]; return r; }
    GridBits operator|(const GridBits& o) const { GridBits r; for (int x = 0; x < W; ++x) r.c[x] = c[x] | o.c[x]; return r; }
    GridBits operator^(const GridBits& o) const { GridBits r; for (int x = 0; x < W; ++x) r.c[x] = c[x] ^ o.c[x]; return r; }
    GridBits without(const GridBits& o) const { GridBits r; for (int x = 0; x < W; ++x) r.c[x] = c[x] & ~o.c[x]; return r; }
    GridBits& operator&=(const GridBits& o) { return *this = *this & o; }
    GridBits& operator|=(const GridBits& o) { return *this = *this | o; }

    // Each cell moves one step. A cell may leave the field upwards into bits
    // above row H-1; callers AND with a field mask where that matters.
    GridBits up() const { GridBits r; for (int x = 0; x < W; ++x) r.c[x] = c[x] << 1; return r; }
    GridBits down() const { GridBits r; for (int x = 0; x < W; ++x) r.c[x] = c[x] >> 1; return r; }
    // left(): each cell takes the value of its right neighbour; right(): of its left one.
    GridBits left() const { GridBits r; for (int x = 0; x + 1 < W; ++x) r.c[x] = c[x + 1]; return r; }
    GridBits right() const { GridBits r; for (int x = 1; x < W; ++x) r.c[x] = c[x - 1]; return r; }
};

template <class G> inline const GridBits<G> GRID_CLEAR_MASK = GridBits<G>::fill(GridBits<G>::COLUMN_CLEAR);

// Calls f(x, y) for every cell of the set, columns left to right, rows bottom up.
template <class G, class F> inline void forEachCell(const GridBits<G>& bits, F&& f) {
    for (int x = 0; x < G::W; ++x)
        for (uint32_t m = bits.c[x]; m; m &= m - 1) f(x, std::countr_zero(m));
}

template <class G> struct GridLinks {
    GridBits<G> u, d, l, r;
};

template <class G> struct GridField {
    using Bits = GridBits<G>;
    std::array<Bits, 3> plane{}; // cell codes as in core/bit_field.h: 0 empty, 1..5 colours, 6 garbage

    Bits occupied() const { return plane[0] | plane[1] | plane[2]; }
    Bits colorMask(Cell code) const {
        Bits m = code & 1 ? plane[0] : occupied().without(plane[0]);
        m = code & 2 ? m & plane[1] : m.without(plane[1]);
        return code & 4 ? m & plane[2] : m.without(plane[2]);
    }
    Bits normal() const { return occupied().without(plane[1] & plane[2]); }
    Bits garbage() const { return (plane[1] & plane[2]).without(plane[0]); }

    Cell get(int x, int y) const {
        return Cell(int(plane[0].get(x, y)) | int(plane[1].get(x, y)) << 1 | int(plane[2].get(x, y)) << 2);
    }
    void set(int x, int y, Cell code) {
        const uint32_t bit = 1u << y;
        for (int i = 0; i < 3; ++i)
            plane[i].c[x] = code >> i & 1 ? plane[i].c[x] | bit : plane[i].c[x] & ~bit;
    }
    bool empty() const { return occupied().empty(); }
    int count() const { return occupied().count(); }
    // Number of cells in each column; equals the height when there are no holes.
    void heights(int h[G::W]) const {
        const Bits occ = occupied();
        for (int x = 0; x < G::W; ++x) h[x] = std::popcount(occ.c[x]);
    }
    bool operator==(const GridField& o) const { return plane == o.plane; }

    GridLinks<G> links() const {
        const Bits n = normal() & GRID_CLEAR_MASK<G>;
        GridLinks<G> k;
        k.u = (n & n.down()).without((plane[0] ^ plane[0].down()) | (plane[1] ^ plane[1].down()) |
                                     (plane[2] ^ plane[2].down()));
        k.r = (n & n.left()).without((plane[0] ^ plane[0].left()) | (plane[1] ^ plane[1].left()) |
                                     (plane[2] ^ plane[2].left()));
        k.d = k.u.up();
        k.l = k.r.right();
        return k;
    }
    // As BitField::seeds: every cell of a group of four or more is a seed or next to one.
    struct Seeds {
        Bits three, pairU, pairR;
    };
    static Seeds seeds(const GridLinks<G>& k) {
        const Bits udAnd = k.u & k.d, lrAnd = k.l & k.r, udOr = k.u | k.d, lrOr = k.l | k.r;
        const Bits two = udAnd | lrAnd | (udOr & lrOr);
        return {(udAnd & lrOr) | (lrAnd & udOr), two & k.u & two.down(), two & k.r & two.left()};
    }
    bool hasClear() const {
        const Seeds s = seeds(links());
        return (s.three | s.pairU | s.pairR).any();
    }
    static Bits group(Bits start, const GridLinks<G>& k) {
        for (Bits g = start;;) {
            const Bits next = g | (g & k.u).up() | (g & k.d).down() | (g & k.r).right() | (g & k.l).left();
            if (next == g) return g;
            g = next;
        }
    }
    Bits garbageNextTo(const Bits& cleared) const {
        return garbage() & GRID_CLEAR_MASK<G> & (cleared.up() | cleared.down() | cleared.left() | cleared.right());
    }
    void erase(const Bits& cells) {
        for (auto& p : plane) p = p.without(cells);
    }
    // Gravity: every column is compacted towards the floor. False when no cell had to move.
    bool drop() {
        const Bits occ = occupied();
        bool moved = false;
        for (int x = 0; x < G::W; ++x) {
            const uint32_t keep = occ.c[x];
            if (!(keep & (keep + 1))) continue; // no hole in this column
            moved = true;
            for (auto& p : plane) {
                uint32_t out = 0, bit = 1;
                for (uint32_t m = keep; m; m &= m - 1, bit <<= 1)
                    if (p.c[x] & m & (0u - m)) out |= bit;
                p.c[x] = out;
            }
        }
        return moved;
    }

    struct Key {
        std::array<Bits, 3> plane;
        bool operator==(const Key& o) const { return plane == o.plane; }
    };
    Key key() const { return {plane}; }
};

template <class G> struct GridKeyHash {
    size_t operator()(const typename GridField<G>::Key& k) const {
        uint64_t h = 0x9E3779B97F4A7C15ull;
        for (const auto& p : k.plane)
            for (uint32_t v : p.c) {
                h ^= v;
                h *= 0xFF51AFD7ED558CCDull;
                h ^= h >> 32;
            }
        return size_t(h);
    }
};

template <class G> struct GridFieldWave {
    GridBits<G> cleared, garbage;
    int pieces = 0, colors = 0, groups = 0, groupBonus = 0;
};

// As core/bit_field.h findWave, on this field.
template <class G> inline bool findWave(const GridField<G>& f, GridFieldWave<G>& w) {
    using Bits = GridBits<G>;
    const GridLinks<G> k = f.links();
    const typename GridField<G>::Seeds s = GridField<G>::seeds(k);
    if ((s.three | s.pairU | s.pairR).empty()) return false;
    const Bits seed = s.three | s.pairU | s.pairU.up() | s.pairR | s.pairR.right();
    w = {};
    w.cleared = seed | (seed & k.u).up() | (seed & k.d).down() | (seed & k.r).right() | (seed & k.l).left();
    w.garbage = f.garbageNextTo(w.cleared);
    w.pieces = w.cleared.count();
    for (Cell color = 1; color <= MAX_COLORS; ++color) {
        Bits rest = w.cleared & f.colorMask(color);
        if (rest.empty()) continue;
        ++w.colors;
        while (rest.any()) {
            int x = 0;
            while (!rest.c[x]) ++x;
            const Bits g = GridField<G>::group(Bits::cell(x, std::countr_zero(rest.c[x])), k);
            ++w.groups;
            w.groupBonus += groupBonus(g.count());
            rest = rest.without(g);
        }
    }
    return true;
}
template <class G> inline bool stepWave(GridField<G>& f, GridFieldWave<G>& w) {
    if (!findWave(f, w)) return false;
    f.erase(w.cleared | w.garbage);
    f.drop();
    return true;
}
// The score uses the tables of core/bit_field.h; chains past 19 use the bonus of the 19th.
template <class G> inline int gridWaveScore(const GridFieldWave<G>& w, int chain) {
    Wave v;
    v.pieces = w.pieces;
    v.colors = w.colors;
    v.groupBonus = w.groupBonus;
    return waveScore(v, chain);
}

} // namespace juraic
