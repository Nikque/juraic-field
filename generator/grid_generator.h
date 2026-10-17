#pragma once
// The backward beam search of generator.cpp, on a field of any geometry
// (grid_field.h). The steps, their order and the use of the random numbers are
// those of generator.cpp, so on the 6 x 13 geometry this search finds the very
// boards generator.cpp finds (tests/core_tests.cpp compares them); see
// generator.cpp for why a board made this way needs no forward simulation.
#include "grid_field.h"
#include "../core/reach.h"
#include "../core/rng.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdlib>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace juraic {

template <class G> struct GridSolution {
    GridField<G> field;      // the board right after the last pair has been placed
    GridBits<G> trigger;     // the four pieces of the first wave
    uint32_t firePairs = 0;  // pair positions (gridPairPosition ids) that can be the last pair
    int targetChain = 0;
};

// ---- where the falling pair can be put ------------------------------------
// As core/reach.h, with the rows of the geometry: a column of CLEAR_H - 1 or
// less can be passed, one of H never, one of CLEAR_H only while the axis piece
// is lifted into the hidden row. Ids 0..W-1 are vertical in column id, ids
// W..2W-2 horizontal in columns (id-W, id-W+1).
template <class G> constexpr int gridPairPositions() { return 2 * G::W - 1; }
template <class G> constexpr PairPosition gridPairPosition(int id) {
    return id < G::W ? PairPosition{id, id} : PairPosition{id - G::W, id - G::W + 1};
}

template <class G> inline bool gridColumnReachable(const int h[G::W], int target) {
    constexpr int S = G::SPAWN_X, TOP = G::CLEAR_H, KICK = G::CLEAR_H - 1;
    for (int x = 0; x < G::W; ++x)
        if ((G::DEATH_COLUMNS >> x & 1) && h[x] >= TOP) return false;
    const int step = target < S ? -1 : 1;
    int fly = -1;
    if ((S > 0 && S + 1 < G::W && h[S - 1] >= TOP && h[S + 1] >= TOP) || h[S] == KICK) {
        fly = MAX_HOP;
    } else {
        for (int hopped = 1, x = S - step; hopped <= MAX_HOP && x >= 0 && x < G::W; ++hopped, x -= step) {
            if (h[x] == KICK) fly = MAX_HOP - hopped;
            if (h[x] >= KICK) break;
        }
    }
    for (int x = S; x != target;) {
        x += step;
        if (h[x] >= G::H) return false;
        if (h[x] == TOP) {
            if (fly < 0) return false;
            fly = MAX_HOP;
        } else if (h[x] == KICK) {
            fly = MAX_HOP;
        } else if (fly >= 0) {
            --fly;
        }
    }
    return true;
}
template <class G> inline uint32_t gridReachablePairs(const int h[G::W]) {
    unsigned column = 0;
    for (int x = 0; x < G::W; ++x)
        if (gridColumnReachable<G>(h, x)) column |= 1u << x;
    uint32_t result = 0;
    for (int x = 0; x < G::W; ++x)
        if ((column >> x & 1) && h[x] <= G::H - 2) result |= 1u << x;
    for (int x = 0; x + 1 < G::W; ++x) {
        const int outer = x >= G::SPAWN_X ? x + 1 : x;
        if ((column >> outer & 1) && h[x] <= G::H - 1 && h[x + 1] <= G::H - 1) result |= 1u << (G::W + x);
    }
    return result;
}
// True when every cell of row CLEAR_H in a column of DEATH_COLUMNS is free.
template <class G> inline bool deathCellsFree(const GridField<G>& f) {
    for (int x = 0; x < G::W; ++x)
        if ((G::DEATH_COLUMNS >> x & 1) && f.get(x, G::CLEAR_H - 1)) return false;
    return true;
}

template <class G> GridBits<G> gridPairCells(const GridField<G>& field, int id) {
    int h[G::W];
    field.heights(h);
    const PairPosition p = gridPairPosition<G>(id);
    using Bits = GridBits<G>;
    if (p.x1 == p.x2)
        return h[p.x1] >= 2 ? Bits::cell(p.x1, h[p.x1] - 1) | Bits::cell(p.x1, h[p.x1] - 2) : Bits();
    return h[p.x1] && h[p.x2] ? Bits::cell(p.x1, h[p.x1] - 1) | Bits::cell(p.x2, h[p.x2] - 1) : Bits();
}

// As firePairs() in generator.cpp.
template <class G> uint32_t gridFirePairs(const GridField<G>& field, const GridBits<G>& trigger) {
    int h[G::W];
    field.heights(h);
    const auto& t = trigger.c;
    uint32_t result = 0;
    const auto reachable = [&](int id, int x1, int x2, int n1, int n2) {
        h[x1] -= n1;
        h[x2] -= n2;
        if (gridReachablePairs<G>(h) >> id & 1) result |= 1u << id;
        h[x1] += n1;
        h[x2] += n2;
    };
    for (int x = 0; x < G::W; ++x)
        if (h[x] >= 2 && (t[x] >> (h[x] - 2) & 3)) reachable(x, x, x, 2, 0);
    for (int x = 0; x + 1 < G::W; ++x)
        if (h[x] && h[x + 1] && ((t[x] >> (h[x] - 1) & 1) || (t[x + 1] >> (h[x + 1] - 1) & 1)))
            reachable(G::W + x, x, x + 1, 1, 1);
    return result;
}

// Checks every condition of a valid result directly, by simulation, without
// relying on how the board was made. Returns an empty string when valid.
template <class G> std::string gridVerifySolution(const GridSolution<G>& s) {
    const GridBits<G> occupied = s.field.occupied();
    for (int x = 0; x < G::W; ++x)
        if (occupied.c[x] & (occupied.c[x] + 1) || occupied.c[x] > GridBits<G>::COLUMN_ALL)
            return "column with a hole or a piece above the top row";
    if (!(s.field.normal() == occupied)) return "not a normal colour";
    if (s.trigger.count() != 4) return "trigger is not four cells";
    constexpr int P = gridPairPositions<G>();
    if (!s.firePairs || s.firePairs >> P) return "no last pair";
    for (int id = 0; id < P; ++id) {
        if (!(s.firePairs >> id & 1)) continue;
        const GridBits<G> pair = gridPairCells(s.field, id);
        if (pair.count() != 2) return "last pair is not two pieces";
        GridField<G> before = s.field;
        before.erase(pair);
        if (before.hasClear()) return "the board clears before the last pair";
        if (!deathCellsFree(before)) return "the last pair cannot appear";
        int h[G::W];
        before.heights(h);
        if (!(gridReachablePairs<G>(h) >> id & 1)) return "the last pair cannot reach its place";
    }
    GridField<G> f = s.field;
    int waves = 0;
    for (GridFieldWave<G> w; stepWave(f, w); ++waves) {
        if (w.groups != 1 || w.pieces != 4 || w.garbage.any()) return "a wave is not a single group of four";
        if (waves == 0 && !(w.cleared == s.trigger)) return "the first wave is not the trigger";
    }
    if (waves != s.targetChain) return "wrong number of waves";
    if (!deathCellsFree(f)) return "choked after the chain";
    return {};
}

namespace grid_detail {

template <class G> struct Placement {
    GridBits<G> ins;          // the four inserted cells, at their final position
    GridBits<G> neighbors;    // visible cells next to the inserted ones
    std::array<uint32_t, G::W> low;  // shape columns: the cells below the insertion; other columns: all
    std::array<uint8_t, G::W> shift; // shape columns: cells inserted in this column; other columns: 0
    std::array<int8_t, G::W> slot;   // row where the insertion starts, -1 if the column is not used
    uint8_t centerCells = 0, deathCells = 0; // inserted cells in the columns of centerColumns() / DEATH_COLUMNS
};

// The columns at most PLAN_DISTANCE away from a column of DEATH_COLUMNS (bit x = column x).
template <class G> constexpr unsigned centerColumns() {
    unsigned mask = 0;
    for (int x = 0; x < G::W; ++x)
        for (int d = -G::PLAN_DISTANCE; d <= G::PLAN_DISTANCE; ++d)
            if (x + d >= 0 && x + d < G::W && (G::DEATH_COLUMNS >> (x + d) & 1)) mask |= 1u << x;
    return mask;
}
// The most pieces the columns of DEATH_COLUMNS hold together on a valid board:
// each stops one row below CLEAR_H until the last pair adds two pieces.
template <class G> constexpr int deathColumnsTotal() {
    return std::popcount(G::DEATH_COLUMNS) * (G::CLEAR_H - 1) + 2;
}

using Shape = std::vector<std::pair<int, int>>;

// The five free tetrominoes, rotated and reflected: 19 fixed shapes (as generator.cpp).
inline std::vector<Shape> tetrominoShapes() {
    const std::vector<Shape> bases{{{0, 0}, {1, 0}, {2, 0}, {3, 0}},
                                   {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
                                   {{0, 0}, {1, 0}, {2, 0}, {1, 1}},
                                   {{0, 0}, {0, 1}, {0, 2}, {1, 0}},
                                   {{1, 0}, {2, 0}, {0, 1}, {1, 1}}};
    std::set<Shape> unique;
    for (const Shape& base : bases)
        for (int reflected = 0; reflected < 2; ++reflected)
            for (int rotation = 0; rotation < 4; ++rotation) {
                Shape s;
                int minX = 99, minY = 99;
                for (auto [px, py] : base) {
                    int x = reflected ? -px : px, y = py;
                    for (int r = 0; r < rotation; ++r) {
                        const int old = x;
                        x = -y;
                        y = old;
                    }
                    s.emplace_back(x, y);
                    minX = std::min(minX, x);
                    minY = std::min(minY, y);
                }
                for (auto& [x, y] : s) x -= minX, y -= minY;
                std::sort(s.begin(), s.end());
                unique.insert(std::move(s));
            }
    return {unique.begin(), unique.end()};
}

template <class G> std::vector<Placement<G>> makePlacements() {
    constexpr int W = G::W;
    std::vector<Placement<G>> table;
    for (const Shape& shape : tetrominoShapes()) {
        int width = 0, height = 0;
        int bottom[4], count[4]{};
        std::fill(bottom, bottom + 4, 99);
        for (auto [x, y] : shape) {
            width = std::max(width, x + 1);
            height = std::max(height, y + 1);
            bottom[x] = std::min(bottom[x], y);
            ++count[x];
        }
        bool contiguous = true;
        for (auto [x, y] : shape) contiguous &= y < bottom[x] + count[x];
        if (!contiguous) continue;
        for (int x0 = 0; x0 + width <= W; ++x0)
            for (int y0 = 0; y0 + height <= G::CLEAR_H; ++y0) {
                Placement<G> p;
                p.low.fill(~0u);
                p.shift.fill(0);
                p.slot.fill(-1);
                for (int dx = 0; dx < width; ++dx) {
                    const int x = x0 + dx, slot = y0 + bottom[dx], n = count[dx];
                    p.ins.c[x] = ((1u << n) - 1) << slot;
                    p.low[x] = (1u << slot) - 1;
                    p.shift[x] = uint8_t(n);
                    p.slot[x] = int8_t(slot);
                }
                p.neighbors = (p.ins.up() | p.ins.down() | p.ins.left() | p.ins.right()).without(p.ins) & GRID_CLEAR_MASK<G>;
                for (int x = 0; x < W; ++x) {
                    const int n = std::popcount(p.ins.c[x]);
                    if (centerColumns<G>() >> x & 1) p.centerCells = uint8_t(p.centerCells + n);
                    if (G::DEATH_COLUMNS >> x & 1) p.deathCells = uint8_t(p.deathCells + n);
                }
                table.push_back(p);
            }
    }
    return table;
}
template <class G> const std::vector<Placement<G>>& placements() {
    static const std::vector<Placement<G>> table = makePlacements<G>();
    return table;
}

// As PlacementIndex in generator.cpp: belowCell[x][y] = placements that move
// the piece at (x, y); fits[x][h] = placements possible in column x when it
// holds h pieces (or that do not use column x).
template <class G> struct PlacementIndex {
    static constexpr int WORDS = (19 * G::W * G::CLEAR_H + 63) / 64;
    using Set = std::array<uint64_t, WORDS>;
    Set belowCell[G::W][G::H]{};
    Set fits[G::W][G::H + 1]{};
    int words = 0;
};
template <class G> const PlacementIndex<G>& placementIndex() {
    static const PlacementIndex<G>* index = [] {
        auto* ix = new PlacementIndex<G>();
        const auto& table = placements<G>();
        if (table.size() > 64 * PlacementIndex<G>::WORDS) std::abort();
        ix->words = int((table.size() + 63) / 64);
        for (size_t i = 0; i < table.size(); ++i) {
            const uint64_t bit = 1ull << (i % 64);
            for (int x = 0; x < G::W; ++x) {
                const uint32_t ins = table[i].ins.c[x];
                if (!ins) {
                    for (int h = 0; h <= G::H; ++h) ix->fits[x][h][i / 64] |= bit;
                    continue;
                }
                const int slot = std::countr_zero(ins), n = std::popcount(ins);
                for (int y = slot; y < G::H; ++y) ix->belowCell[x][y][i / 64] |= bit;
                for (int h = slot; h + n <= G::H; ++h) ix->fits[x][h][i / 64] |= bit;
            }
        }
        return ix;
    }();
    return *index;
}

template <class G> struct Node {
    GridField<G> field;
    GridBits<G> trigger;
};
struct Choice {
    uint16_t placement;
    Cell color;
};

template <class G> inline GridField<G> openGap(const GridField<G>& post, const Placement<G>& p) {
    GridField<G> q;
    for (int i = 0; i < 3; ++i)
        for (int x = 0; x < G::W; ++x) {
            const uint32_t v = post.plane[i].c[x];
            q.plane[i].c[x] = (v & p.low[x]) | ((v & ~p.low[x]) << p.shift[x]);
        }
    return q;
}
template <class G> inline GridField<G> fillGap(GridField<G> q, const Placement<G>& p, Cell color) {
    for (int i = 0; i < 3; ++i)
        if (color >> i & 1) q.plane[i] |= p.ins;
    return q;
}

template <class G> constexpr size_t maxPlacements() { return 64 * size_t(PlacementIndex<G>::WORDS); }

// Which of the placements the rules allow a step may use (no filter: all of them).
template <class G> struct Filter {
    // Heights of `post`: at most one old piece may end up above the new group in
    // one of its columns (the guidance of generator.cpp) ...
    const int* nearTop = nullptr;
    // ... in one of its columns of centerColumns().
    const int* nearTopCenter = nullptr;
    // >= 0: the pieces in the columns of DEATH_COLUMNS; placements that make them
    // more than deathColumnsTotal() are skipped (no valid board can follow).
    int deathSum = -1;
    // >= 0: the room left in the columns of centerColumns() (deathColumnsTotal()
    // for the columns of DEATH_COLUMNS together, H for the others) must stay at
    // least minCenterRoom after the insertion.
    int centerRoom = 0, minCenterRoom = -1;
    // Heights of `post`: placements are skipped that leave a column whose room
    // no group of four can fill (fewer than four, or not a multiple of four,
    // with no room on either side) when that room is more than `slack`.
    const int* roomHeights = nullptr;
    int slack = 0;
};
template <class G> inline bool leavesIsolatedRoom(const Placement<G>& p, const int* heights, int deathSum, int slack) {
    int room[G::W];
    const int deathRoom = deathColumnsTotal<G>() - deathSum - p.deathCells;
    for (int x = 0; x < G::W; ++x) {
        room[x] = G::H - heights[x] - std::popcount(p.ins.c[x]);
        if ((G::DEATH_COLUMNS >> x & 1) && deathRoom < room[x]) room[x] = deathRoom;
    }
    for (int x = 0; x < G::W; ++x)
        if (room[x] > 0 && room[x] % 4 > slack && (x == 0 || room[x - 1] <= 0) && (x + 1 == G::W || room[x + 1] <= 0))
            return true;
    return false;
}
template <class G> inline bool nearTopIn(const Placement<G>& p, const int* heights, unsigned columns) {
    for (int x = 0; x < G::W; ++x)
        if ((columns >> x & 1) && p.slot[x] >= 0 && heights[x] - p.slot[x] <= 1) return true;
    return false;
}

template <class G> inline size_t candidatePlacements(const Node<G>& post, uint16_t* out, const Filter<G>* filter) {
    const PlacementIndex<G>& index = placementIndex<G>();
    const GridBits<G> occupied = post.field.occupied();
    typename PlacementIndex<G>::Set candidates{};
    bool hasTrigger = false;
    for (int x = 0; x < G::W; ++x)
        if (post.trigger.c[x]) {
            hasTrigger = true;
            const auto& below = index.belowCell[x][std::bit_width(post.trigger.c[x]) - 1];
            for (int w = 0; w < index.words; ++w) candidates[w] |= below[w];
        }
    if (!hasTrigger) candidates.fill(~0ull);
    for (int x = 0; x < G::W; ++x) {
        const auto& fits = index.fits[x][std::popcount(occupied.c[x])];
        for (int w = 0; w < index.words; ++w) candidates[w] &= fits[w];
    }
    const Placement<G>* table = placements<G>().data();
    size_t count = 0;
    for (int w = 0; w < index.words; ++w)
        for (uint64_t rest = candidates[w]; rest; rest &= rest - 1) {
            const size_t i = size_t(w) * 64 + size_t(std::countr_zero(rest));
            if (filter) {
                const Placement<G>& p = table[i];
                if (filter->nearTop && !nearTopIn(p, filter->nearTop, ~0u)) continue;
                if (filter->nearTopCenter && !nearTopIn(p, filter->nearTopCenter, centerColumns<G>())) continue;
                if (filter->deathSum >= 0 && filter->deathSum + p.deathCells > deathColumnsTotal<G>()) continue;
                if (filter->minCenterRoom >= 0 && filter->centerRoom - p.centerCells < filter->minCenterRoom) continue;
                if (filter->roomHeights && leavesIsolatedRoom(p, filter->roomHeights, filter->deathSum, filter->slack)) continue;
            }
            out[count++] = uint16_t(i);
        }
    return count;
}

template <class G> inline unsigned validColors(const GridField<G>& post, const Placement<G>& p, int colorCount) {
    const GridField<G> q = openGap(post, p);
    if (q.hasClear()) return 0;
    unsigned colors = 0;
    for (Cell color = 1; color <= colorCount; ++color)
        if (q.colorMask(color).disjoint(p.neighbors)) colors |= 1u << color;
    return colors;
}

template <class G, class F>
void forEachPredecessor(const Node<G>& post, int colorCount, F&& f, const Filter<G>* filter = nullptr) {
    thread_local std::vector<uint16_t> candidates;
    candidates.resize(maxPlacements<G>());
    const size_t count = candidatePlacements(post, candidates.data(), filter);
    const Placement<G>* table = placements<G>().data();
    for (size_t n = 0; n < count; ++n) {
        const unsigned colors = validColors(post.field, table[candidates[n]], colorCount);
        for (Cell color = 1; color <= colorCount; ++color)
            if (colors >> color & 1) f(candidates[n], color);
    }
}

// As samplePredecessors() in generator.cpp.
constexpr int SAMPLE_DRAWS = 96;
template <class G>
void samplePredecessors(const Node<G>& post, int colorCount, size_t take, Rng& rng, std::vector<Choice>& out,
                        const Filter<G>* filter, std::vector<uint16_t>& candidates, std::vector<uint8_t>& known) {
    candidates.resize(maxPlacements<G>());
    known.resize(maxPlacements<G>());
    const size_t count = candidatePlacements(post, candidates.data(), filter);
    out.clear();
    if (!count) return;
    std::fill(known.begin(), known.begin() + ptrdiff_t(count), uint8_t(0xFF));
    const Placement<G>* table = placements<G>().data();
    const auto colorsOf = [&](size_t n) {
        if (known[n] == 0xFF) known[n] = uint8_t(validColors(post.field, table[candidates[n]], colorCount));
        return known[n];
    };
    const auto chosen = [&](uint16_t placement, Cell color) {
        for (const Choice& c : out)
            if (c.placement == placement && c.color == color) return true;
        return false;
    };
    for (int draw = 0; draw < SAMPLE_DRAWS && out.size() < take; ++draw) {
        const size_t n = size_t(rng.below(count));
        const Cell color = Cell(1 + rng.below(uint64_t(colorCount)));
        if ((colorsOf(n) >> color & 1) && !chosen(candidates[n], color)) out.push_back({candidates[n], color});
    }
    if (out.size() >= take) return;
    const size_t found = out.size();
    for (size_t n = 0; n < count; ++n)
        for (Cell color = 1; color <= colorCount; ++color)
            if ((colorsOf(n) >> color & 1) && !chosen(candidates[n], color)) out.push_back({candidates[n], color});
    const size_t more = std::min(take - found, out.size() - found);
    rng.partialShuffle(out.data() + found, out.size() - found, more);
    out.resize(found + more);
}

template <class G> inline Node<G> makeChild(const Node<G>& post, Choice c) {
    const Placement<G>& p = placements<G>()[c.placement];
    return {fillGap(openGap(post.field, p), p, c.color), p.ins};
}

// The distance of column x from the nearest column of DEATH_COLUMNS.
template <class G> constexpr int deathDistance(int x) {
    int distance = G::W;
    for (int d = 0; d < G::W; ++d)
        if (G::DEATH_COLUMNS >> d & 1) distance = std::min(distance, x > d ? x - d : d - x);
    return distance;
}

// The board the chain ends with: `extra` pieces, nothing clears, and the cells
// of row CLEAR_H in the columns of DEATH_COLUMNS are free. `farFirst` (the
// plan): each piece goes into one of the columns farthest from the death
// columns that still have room, so that the room is left next to them.
template <class G> bool makeResidual(GridField<G>& field, int extra, int colorCount, Rng& rng, bool farFirst = false) {
    int h[G::W]{};
    const auto room = [&](int x) { return h[x] < ((G::DEATH_COLUMNS >> x & 1) ? G::CLEAR_H - 1 : G::H); };
    for (int i = 0; i < extra; ++i) {
        int x;
        if (farFirst) {
            int farthest = -1, columns[G::W], n = 0;
            for (int c = 0; c < G::W; ++c)
                if (room(c)) farthest = std::max(farthest, deathDistance<G>(c));
            for (int c = 0; c < G::W; ++c)
                if (room(c) && deathDistance<G>(c) == farthest) columns[n++] = c;
            if (!n) return false;
            x = columns[rng.below(uint64_t(n))];
        } else {
            do x = int(rng.below(G::W));
            while (!room(x));
        }
        const int start = int(rng.below(colorCount));
        bool placed = false;
        for (int n = 0; n < colorCount && !placed; ++n) {
            field.set(x, h[x], Cell(1 + (start + n) % colorCount));
            placed = !field.hasClear();
            if (!placed) field.set(x, h[x], EMPTY);
        }
        if (!placed) return false;
        ++h[x];
    }
    return true;
}

inline bool stopped(const std::atomic_bool* stop) { return stop && stop->load(std::memory_order_relaxed); }

} // namespace grid_detail

// Every board from which `post` results after one wave that clears exactly the
// inserted group of four, with that group. Exposed for the tests.
template <class G> std::vector<std::pair<GridField<G>, GridBits<G>>> gridPredecessors(const GridField<G>& post, const GridBits<G>& trigger, int colorCount) {
    using namespace grid_detail;
    std::vector<std::pair<GridField<G>, GridBits<G>>> result;
    const Node<G> node{post, trigger};
    forEachPredecessor<G>(node, colorCount, [&](uint16_t i, Cell color) {
        const Node<G> child = makeChild(node, {i, color});
        result.emplace_back(child.field, child.trigger);
    });
    return result;
}

// One independent search, as generateOne() in generator.cpp, with the values of
// GeneratorConfig of the same names.
template <class G>
std::optional<GridSolution<G>> gridGenerateOne(int colors, int target, int beamWidthValue, int perParentValue,
                                               int minExtra, int maxExtra, Rng& rng,
                                               const std::atomic_bool* stop = nullptr,
                                               std::atomic_int* depthOut = nullptr,
                                               const std::atomic_bool* cancel = nullptr) {
    using namespace grid_detail;
    const size_t perParent = size_t(perParentValue), beamWidth = size_t(beamWidthValue);
    std::vector<Node<G>> beam(1), next;
    const int extra = maxExtra ? minExtra + int(rng.below(uint64_t(maxExtra - minExtra + 1))) : 0;
    // The plan for boards with little room to spare (see grid_field.h): until
    // PLAN_STEPS steps before the end the columns away from the death columns
    // are filled and the room the last steps need is kept next to the death
    // columns; then the new groups go near the top there.
    const int slack = maxPieces<G>() - extra - 4 * target;
    const bool planned = G::PLAN_MAX_SLACK >= 0 && slack <= G::PLAN_MAX_SLACK;
    if (!makeResidual(beam[0].field, extra, colors, rng, planned && extra >= G::PLAN_FAR_FIRST_EXTRA)) return std::nullopt;

    const bool guided = extra + 4 * target >= G::GUIDE_MIN_PIECES;
    const auto filterFor = [&](const GridField<G>& field, int depth, int* heights) -> std::optional<Filter<G>> {
        field.heights(heights);
        Filter<G> f;
        if (guided && depth >= target - G::GUIDED_STEPS) f.nearTop = heights;
        if (planned) {
            f.deathSum = 0;
            for (int x = 0; x < G::W; ++x)
                if (G::DEATH_COLUMNS >> x & 1) f.deathSum += heights[x];
                else if (centerColumns<G>() >> x & 1) f.centerRoom += G::H - heights[x];
            f.centerRoom += deathColumnsTotal<G>() - f.deathSum;
            f.roomHeights = heights;
            f.slack = slack;
            // The other columns are filled ahead of time: PLAN_RAMP percent of the
            // steps left before the last PLAN_STEPS would be enough for them.
            if (depth <= target - G::PLAN_STEPS)
                f.minCenterRoom = 4 * G::PLAN_STEPS + slack + (100 - G::PLAN_RAMP) * 4 * (target - G::PLAN_STEPS - depth) / 100;
            else if (depth < target) f.nearTopCenter = heights;
        }
        if (!f.nearTop && !planned) return std::nullopt;
        return f;
    };
    std::vector<Choice> choices;
    std::vector<uint16_t> candidates;
    std::vector<uint8_t> known;
    std::unordered_set<typename GridField<G>::Key, GridKeyHash<G>> seen;
    for (int depth = 1; depth <= target; ++depth) {
        if (depthOut) depthOut->store(depth, std::memory_order_relaxed);
        next.clear();
        seen.clear();
        for (const Node<G>& post : beam) {
            if (stopped(stop) || stopped(cancel)) return std::nullopt;
            choices.clear();
            int heights[G::W];
            const std::optional<Filter<G>> filter = filterFor(post.field, depth, heights);
            const Filter<G>* guide = filter ? &*filter : nullptr;
            if (depth < target) {
                if (depth == target - 1) {
                    forEachPredecessor<G>(post, colors, [&](uint16_t i, Cell color) { choices.push_back({i, color}); }, guide);
                } else {
                    samplePredecessors<G>(post, colors, perParent, rng, choices, guide, candidates, known);
                }
                for (const Choice& choice : choices) {
                    const Node<G> child = makeChild(post, choice);
                    if (seen.insert(child.field.key()).second) next.push_back(child);
                }
                continue;
            }
            std::vector<uint32_t> pairs;
            forEachPredecessor<G>(post, colors, [&](uint16_t i, Cell color) {
                const Node<G> child = makeChild(post, {i, color});
                if (const uint32_t fire = gridFirePairs(child.field, child.trigger)) {
                    choices.push_back({i, color});
                    pairs.push_back(fire);
                }
            }, planned ? guide : nullptr);
            if (choices.empty()) continue;
            const size_t pick = size_t(rng.below(choices.size()));
            const Node<G> child = makeChild(post, choices[pick]);
            return GridSolution<G>{child.field, child.trigger, pairs[pick], target};
        }
        if (next.empty()) return std::nullopt;
        const size_t width = depth == target - 1 ? next.size() : beamWidth;
        if (planned && G::PLAN_GREEDY && (G::PLAN_GREEDY == 2 || depth <= target - G::PLAN_STEPS) && next.size() > width) {
            // In random order, the boards with the least room left away from the death columns first.
            rng.partialShuffle(next.data(), next.size(), next.size());
            const auto outerRoom = [](const Node<G>& n) {
                int h[G::W], room = 0;
                n.field.heights(h);
                for (int x = 0; x < G::W; ++x) {
                    const int distance = deathDistance<G>(x);
                    if (G::PLAN_GREEDY == 2) room += (G::H - h[x]) * (distance + 1) * (distance + 1);
                    else if (!(centerColumns<G>() >> x & 1)) room += G::H - h[x];
                }
                return room;
            };
            std::vector<std::pair<int, size_t>> order(next.size());
            for (size_t i = 0; i < next.size(); ++i) order[i] = {outerRoom(next[i]), i};
            std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            std::vector<Node<G>> kept;
            kept.reserve(width);
            for (size_t i = 0; i < width; ++i) kept.push_back(next[order[i].second]);
            next.swap(kept);
        }
        rng.partialShuffle(next.data(), next.size(), std::min(width, next.size()));
        if (next.size() > width) next.resize(width);
        beam.swap(next);
    }
    return std::nullopt;
}

} // namespace juraic
