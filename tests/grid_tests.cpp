// Tests of the field of any size (generator/grid_field.h) and the template
// search (generator/grid_generator.h).
//
// On the 6 x 13 geometry both are compared with the 6 x 13 engine (core/ and
// generator.cpp): the same waves, the same reachable pairs, the same
// predecessors and, attempt by attempt, the same generated boards. On the
// 10 x 19 geometry they are compared with a cell-by-cell simulator written here.
#include "../generator/generator.h"
#include "../generator/grid_generator.h"
#include <chrono>
#include <iostream>
#include <random>
#include <set>

using namespace juraic;

static void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

// ---- cell-by-cell reference of any geometry -----------------------------------
template <class G> struct Cells {
    Cell c[G::W][G::H]{};
};
template <class G> struct CellWave {
    std::set<std::pair<int, int>> cleared, garbage;
    int colors = 0, groups = 0, bonus = 0;
};
template <class G> static bool cellWave(Cells<G>& g, CellWave<G>& w) {
    bool seen[G::W][G::H]{};
    bool colorUsed[8]{};
    const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
    for (int x = 0; x < G::W; ++x)
        for (int y = 0; y < G::CLEAR_H; ++y) {
            if (seen[x][y] || g.c[x][y] < 1 || g.c[x][y] > 5) continue;
            std::vector<std::pair<int, int>> group{{x, y}};
            seen[x][y] = true;
            for (size_t i = 0; i < group.size(); ++i)
                for (int d = 0; d < 4; ++d) {
                    const int nx = group[i].first + dx[d], ny = group[i].second + dy[d];
                    if (nx < 0 || nx >= G::W || ny < 0 || ny >= G::CLEAR_H || seen[nx][ny] || g.c[nx][ny] != g.c[x][y]) continue;
                    seen[nx][ny] = true;
                    group.push_back({nx, ny});
                }
            if (group.size() < 4) continue;
            ++w.groups;
            w.bonus += groupBonus(int(group.size()));
            colorUsed[g.c[x][y]] = true;
            w.cleared.insert(group.begin(), group.end());
        }
    if (w.cleared.empty()) return false;
    for (bool used : colorUsed) w.colors += used;
    for (auto [x, y] : w.cleared)
        for (int d = 0; d < 4; ++d) {
            const int nx = x + dx[d], ny = y + dy[d];
            if (nx >= 0 && nx < G::W && ny >= 0 && ny < G::CLEAR_H && g.c[nx][ny] == GARBAGE) w.garbage.insert({nx, ny});
        }
    for (auto [x, y] : w.cleared) g.c[x][y] = EMPTY;
    for (auto [x, y] : w.garbage) g.c[x][y] = EMPTY;
    for (int x = 0; x < G::W; ++x) {
        int to = 0;
        for (int y = 0; y < G::H; ++y)
            if (g.c[x][y]) {
                const Cell v = g.c[x][y];
                g.c[x][y] = EMPTY;
                g.c[x][to++] = v;
            }
    }
    return true;
}
template <class G> static std::set<std::pair<int, int>> cellsOf(const GridBits<G>& b) {
    std::set<std::pair<int, int>> s;
    forEachCell(b, [&](int x, int y) { s.insert({x, y}); });
    return s;
}
template <class G> static Cells<G> randomCells(std::mt19937_64& rng, int colors) {
    Cells<G> g;
    const int fill = int(rng() % 101);
    for (int x = 0; x < G::W; ++x)
        for (int y = 0; y < G::H; ++y)
            if (int(rng() % 100) < fill) g.c[x][y] = Cell(rng() % 8 == 0 ? GARBAGE : 1 + rng() % colors);
    return g;
}
template <class G> static GridField<G> toGrid(const Cells<G>& g) {
    GridField<G> f;
    for (int x = 0; x < G::W; ++x)
        for (int y = 0; y < G::H; ++y)
            if (g.c[x][y]) f.set(x, y, g.c[x][y]);
    return f;
}
template <class G> static bool same(const GridField<G>& f, const Cells<G>& g) {
    for (int x = 0; x < G::W; ++x)
        for (int y = 0; y < G::H; ++y)
            if (f.get(x, y) != g.c[x][y]) return false;
    return true;
}

// Waves of the field type against the cell-by-cell reference, on boards with
// holes, garbage and pieces in the hidden rows.
template <class G> static void testWavesAgainstCells(std::mt19937_64& rng, int boards) {
    for (int n = 0; n < boards; ++n) {
        Cells<G> g = randomCells<G>(rng, 2 + int(rng() % 4));
        GridField<G> f = toGrid(g);
        check(same(f, g), "set/get");
        for (int wave = 0; wave < 40; ++wave) {
            CellWave<G> expected;
            const Cells<G> before = g;
            const bool more = cellWave(g, expected);
            GridFieldWave<G> w;
            check(f.hasClear() == more, "hasClear against the cell reference");
            const bool fired = stepWave(f, w);
            check(fired == more, "stepWave against the cell reference");
            if (!more) {
                // stepWave does not drop a board on which nothing clears; the reference did.
                g = before;
                break;
            }
            check(cellsOf(w.cleared) == expected.cleared, "cleared cells");
            check(cellsOf(w.garbage) == expected.garbage, "garbage cells");
            check(w.pieces == int(expected.cleared.size()) && w.colors == expected.colors && w.groups == expected.groups &&
                      w.groupBonus == expected.bonus,
                  "wave details");
            check(same(f, g), "board after gravity");
        }
    }
}

// ---- 6 x 13: the field type against the engine --------------------------------
static BitField toBits(const GridField<Grid6x13>& g) {
    BitField f;
    for (int x = 0; x < 6; ++x)
        for (int y = 0; y < 13; ++y)
            if (const Cell v = g.get(x, y)) f.set(x, y, v);
    return f;
}
static GridField<Grid6x13> fromBits(const BitField& f) {
    GridField<Grid6x13> g;
    for (int x = 0; x < 6; ++x)
        for (int y = 0; y < 13; ++y)
            if (const Cell v = f.get(x, y)) g.set(x, y, v);
    return g;
}
static FieldBits toFieldBits(const GridBits<Grid6x13>& b) {
    uint16_t c[6];
    for (int x = 0; x < 6; ++x) c[x] = uint16_t(b.c[x]);
    return FieldBits::fromColumns(c);
}
static GridBits<Grid6x13> fromFieldBits(FieldBits b) {
    GridBits<Grid6x13> g;
    for (int x = 0; x < 6; ++x) g.c[x] = b.column(x);
    return g;
}

static void testWavesAgainstEngine(std::mt19937_64& rng) {
    for (int n = 0; n < 3000; ++n) {
        GridField<Grid6x13> g = toGrid(randomCells<Grid6x13>(rng, 2 + int(rng() % 4)));
        BitField f = toBits(g);
        for (int wave = 0; wave < 30; ++wave) {
            check(g.hasClear() == f.hasClear(), "hasClear differs from the engine");
            Wave a;
            GridFieldWave<Grid6x13> b;
            const bool fa = stepWave(f, a), fb = stepWave(g, b);
            check(fa == fb, "stepWave differs from the engine");
            if (!fa) break;
            check(toFieldBits(b.cleared) == a.cleared && toFieldBits(b.garbage) == a.garbage && a.pieces == b.pieces &&
                      a.colors == b.colors && a.groups == b.groups && a.groupBonus == b.groupBonus,
                  "wave differs from the engine");
            check(toBits(g) == f, "board after the wave differs from the engine");
            check(gridWaveScore(b, wave + 1) == waveScore(a, wave + 1), "score differs from the engine");
        }
    }
}

static void testReachAgainstEngine() {
    // Every combination of heights 0..13 in six columns.
    int h[6];
    long cases = 0;
    for (int i = 0; i < 14 * 14 * 14 * 14 * 14 * 14; ++i) {
        int v = i;
        for (int x = 0; x < 6; ++x, v /= 14) h[x] = v % 14;
        check(gridReachablePairs<Grid6x13>(h) == reachablePairs(h), "reachable pairs differ from the engine");
        ++cases;
    }
    check(cases == 7529536, "reach cases");
}

static void testPredecessorsAgainstEngine(std::mt19937_64& rng) {
    int withTrigger = 0;
    for (int n = 0; n < 400; ++n) {
        // Boards made by a few backward steps, so that there is a trigger.
        BitField post;
        FieldBits trigger;
        const int colors = 3 + int(rng() % 3);
        const int steps = int(rng() % 6);
        for (int s = 0; s < steps; ++s) {
            const auto list = predecessors(post, trigger, colors);
            if (list.empty()) break;
            const auto& pick = list[rng() % list.size()];
            post = pick.field;
            trigger = pick.trigger;
        }
        withTrigger += trigger.any();
        const auto expected = predecessors(post, trigger, colors);
        const auto actual = gridPredecessors<Grid6x13>(fromBits(post), fromFieldBits(trigger), colors);
        check(expected.size() == actual.size(), "number of predecessors differs from the engine");
        for (size_t i = 0; i < expected.size(); ++i)
            check(fromBits(expected[i].field) == actual[i].first && fromFieldBits(expected[i].trigger) == actual[i].second,
                  "predecessor differs from the engine");
    }
    check(withTrigger > 300, "predecessor test must cover boards with a trigger");
}

// The template search on 6 x 13 finds, attempt by attempt, the boards of generator.cpp.
static void testGeneratorAgainstEngine() {
    int compared = 0, found = 0;
    for (int chain = 1; chain <= 19; ++chain)
        for (int colors = 3; colors <= 5; ++colors)
            for (int extraCase = 0; extraCase < 3; ++extraCase) {
                const int capacity = 78 - 4 * chain;
                GeneratorConfig c;
                c.colorCount = colors;
                c.beamWidth = 48;
                c.candidatesPerParent = 2;
                c.targetChain = chain;
                c.minExtraPieces = extraCase == 2 ? capacity : 0;
                c.maxExtraPieces = extraCase == 0 ? 0 : capacity;
                for (uint64_t attempt = 1; attempt <= 3; ++attempt) {
                    Rng a(20261004 + uint64_t(chain), attempt), b(20261004 + uint64_t(chain), attempt);
                    const auto expected = generateOne(c, a);
                    const auto actual = gridGenerateOne<Grid6x13>(c.colorCount, c.targetChain, c.beamWidth, c.candidatesPerParent,
                                                                  c.minExtraPieces, c.maxExtraPieces, b);
                    check(bool(expected) == bool(actual), "success differs from generator.cpp");
                    ++compared;
                    if (!expected) continue;
                    ++found;
                    check(fromBits(expected->field) == actual->field && fromFieldBits(expected->trigger) == actual->trigger &&
                              expected->firePairs == actual->firePairs,
                          "generated board differs from generator.cpp");
                    check(verifySolution(*expected).empty() && gridVerifySolution(*actual).empty(), "generated board is invalid");
                    check(a.next() == b.next(), "random numbers used differ from generator.cpp");
                }
            }
    check(compared == 513 && found > 300, "generator comparison coverage");
    std::cout << "6x13 template search: " << found << " of " << compared << " attempts found the same board\n";
}

// gridVerifySolution rejects what verifySolution rejects.
static void testVerifyAgainstEngine(std::mt19937_64& rng) {
    GeneratorConfig c;
    c.colorCount = 4;
    c.beamWidth = 16;
    c.candidatesPerParent = 2;
    int checked = 0;
    for (uint64_t attempt = 1; checked < 200; ++attempt) {
        c.targetChain = 1 + int(rng() % 19);
        Rng r(7, attempt);
        const auto s = generateOne(c, r);
        if (!s) continue;
        ++checked;
        GridSolution<Grid6x13> g{fromBits(s->field), fromFieldBits(s->trigger), s->firePairs, s->targetChain};
        for (int change = 0; change < 6; ++change) {
            Solution bad = *s;
            GridSolution<Grid6x13> gbad = g;
            if (change == 1) bad.targetChain = gbad.targetChain = s->targetChain + 1;
            if (change == 2) bad.firePairs = gbad.firePairs = s->firePairs | 1u << (rng() % PAIR_POSITIONS);
            if (change == 3) {
                const int x = int(rng() % 6), y = int(rng() % 13);
                const Cell v = Cell(1 + rng() % 4);
                bad.field.set(x, y, v);
                gbad.field.set(x, y, v);
            }
            if (change == 4) {
                bad.trigger = bad.trigger ^ FieldBits::cell(0, 0);
                gbad.trigger = fromFieldBits(bad.trigger);
            }
            if (change == 5) {
                const int x = int(rng() % 6);
                int h[6];
                bad.field.heights(h);
                if (h[x]) {
                    bad.field.set(x, h[x] - 1, EMPTY);
                    gbad.field.set(x, h[x] - 1, EMPTY);
                }
            }
            check(verifySolution(bad).empty() == gridVerifySolution(gbad).empty(), "verification differs from the engine");
        }
    }
}

// ---- 10 x 19 -----------------------------------------------------------------
using G10 = Grid10x19;

static void testWideRules() {
    check(maxPieces<Grid6x13>() == 78 && maxChain<Grid6x13>() == 19, "6 x 13 capacity");
    check(maxPieces<G10>() == 188 && maxChain<G10>() == 47, "10 x 19 capacity: 47 chains");
    check(gridPairPositions<G10>() == 19, "10 x 19 pair positions");

    // Four in a column reaching into the hidden 19th row: only three are in the rows that clear.
    GridField<G10> f;
    for (int y = 15; y < 19; ++y) f.set(3, y, 1);
    for (int y = 0; y < 15; ++y) f.set(3, y, Cell(2 + y % 2));
    check(!f.hasClear(), "the 19th row does not connect");
    GridField<G10> g;
    for (int x = 0; x < 4; ++x) g.set(x, 17, 2);
    for (int x = 0; x < 4; ++x)
        for (int y = 0; y < 17; ++y) g.set(x, y, Cell(3 + (x + y) % 2));
    check(g.hasClear(), "four in the 18th row clear");
    GridField<G10> h;
    for (int x = 0; x < 4; ++x) h.set(x, 18, 2);
    for (int x = 0; x < 4; ++x)
        for (int y = 0; y < 18; ++y) h.set(x, y, Cell(3 + (x + y) % 2));
    check(!h.hasClear(), "four in the 19th row do not clear");

    // A piece of the hidden row falls and then connects.
    GridField<G10> fall2;
    for (int y = 0; y < 4; ++y) fall2.set(0, y, 2);
    for (int y = 4; y < 18; ++y) fall2.set(0, y, Cell(3 + y % 2));
    fall2.set(0, 18, 1);
    for (int x = 1; x < 4; ++x) fall2.set(x, 14, 1);
    for (int x = 1; x < 4; ++x)
        for (int y = 0; y < 14; ++y) fall2.set(x, y, Cell(3 + (x + y) % 2));
    GridFieldWave<G10> w1, w2;
    check(stepWave(fall2, w1) && w1.pieces >= 4, "first wave");
    check(fall2.get(0, 14) == 1, "the hidden-row piece falls four rows");
    check(stepWave(fall2, w2) && w2.cleared.get(0, 14), "the fallen piece connects");

    // Reach on the 10-column field: pairs appear in the fifth column.
    const auto pairs = [](std::array<int, 10> h) { return gridReachablePairs<G10>(h.data()); };
    check(pairs({}) == (1u << 19) - 1, "empty 10 x 19 field: everything reachable");
    check(pairs({0, 0, 0, 0, 18, 0, 0, 0, 0, 0}) == 0, "fifth column at the 18th row: the game is over");
    check(pairs({0, 0, 0, 0, 0, 18, 0, 0, 0, 0}) == 0, "sixth column at the 18th row: the game is over");
    check(pairs({0, 0, 0, 0, 17, 17, 0, 0, 0, 0}) == (1u << 19) - 1, "17-high fifth and sixth columns: every pair still fits");
    check(!(pairs({0, 0, 0, 19, 0, 0, 0, 0, 0, 0}) >> 0 & 1), "a 19-high column cannot be crossed");
    check(!(pairs({0, 0, 0, 18, 0, 0, 0, 0, 0, 0}) >> 0 & 1), "an 18-high column needs a lift");
    check(pairs({0, 0, 0, 18, 17, 0, 0, 0, 0, 0}) >> 0 & 1, "floor kick on the 17-high spawn column");
    check(!(pairs({0, 0, 0, 0, 0, 0, 19, 0, 0, 0}) >> 9 & 1), "a 19-high column on the right cannot be crossed");
    check(pairs({0, 0, 0, 0, 0, 0, 0, 0, 0, 0}) >> (10 + 4) & 1, "horizontal pair on the fifth and sixth columns");
    // Without a 17-high column on the way the axis piece is never lifted, so an 18-high column cannot be climbed.
    const uint32_t far = pairs({0, 0, 0, 0, 0, 0, 0, 0, 0, 18});
    check(!(far >> 9 & 1) && !(far >> (10 + 8) & 1) && (far >> 8 & 1), "an 18-high column far away needs a lift");
    check(pairs({0, 0, 0, 0, 0, 0, 0, 0, 17, 18}) >> (10 + 8) & 1, "floor kick on a 17-high column, then onto the 18-high one");
}

// Generated 10 x 19 boards: valid by the definition, checked against the cell reference.
static void testWideGeneration() {
    int found = 0;
    for (int chain : {1, 2, 5, 10, 20, 30}) {
        for (uint64_t attempt = 1; attempt <= 4; ++attempt) {
            Rng r(99 + uint64_t(chain), attempt);
            const auto s = gridGenerateOne<G10>(4, chain, 48, 2, 0, 3, r);
            if (!s) continue;
            ++found;
            check(gridVerifySolution(*s).empty(), gridVerifySolution(*s).c_str());
            Cells<G10> g;
            for (int x = 0; x < 10; ++x)
                for (int y = 0; y < 19; ++y) g.c[x][y] = s->field.get(x, y);
            int waves = 0;
            for (;; ++waves) {
                CellWave<G10> w;
                if (!cellWave(g, w)) break;
                check(w.groups == 1 && w.cleared.size() == 4 && (waves || w.cleared == cellsOf(s->trigger)),
                      "a 10 x 19 wave is not one group of four");
            }
            check(waves == chain, "10 x 19 chain length by the cell reference");
            check(!g.c[4][17] && !g.c[5][17], "10 x 19: fifth and sixth columns free after the chain");
        }
    }
    check(found >= 12, "10 x 19 generation coverage");
    // The most: 47 chains of 188 pieces (the plan of grid_generator.h; for three colours that of
    // Grid10x19ThreeColors), checked cell by cell.
    for (int colors : {3, 4, 5}) {
        bool fortySeven = false;
        for (uint64_t attempt = 1; attempt <= 100 && !fortySeven; ++attempt) {
            Rng r(47, attempt);
            const auto s = gridGenerateOne<G10>(colors, 47, 48, 2, 0, 0, r);
            if (!s) continue;
            fortySeven = true;
            check(gridVerifySolution(*s).empty() && s->field.count() == 188, "47 chains: 188 pieces");
            Cells<G10> g;
            for (int x = 0; x < 10; ++x)
                for (int y = 0; y < 19; ++y) g.c[x][y] = s->field.get(x, y);
            int waves = 0;
            for (;; ++waves) {
                CellWave<G10> w;
                if (!cellWave(g, w)) break;
                check(w.groups == 1 && w.cleared.size() == 4, "47 chains: one group of four a wave");
            }
            int left = 0;
            for (auto& column : g.c)
                for (Cell v : column) left += v != EMPTY;
            check(waves == 47 && left == 0, "47 chains by the cell reference, nothing left");
        }
        check(fortySeven, "a board of 47 chains within 100 attempts");
    }
}

int main() {
    try {
        std::mt19937_64 rng(20261017);
        const auto started = std::chrono::steady_clock::now();
        const auto step = [&](const char* name, auto&& f) {
            f();
            std::cout << name << " passed ("
                      << std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() << " s)\n"
                      << std::flush;
        };
        step("waves against cells, 6 x 13", [&] { testWavesAgainstCells<Grid6x13>(rng, 2000); });
        step("waves against cells, 10 x 19", [&] { testWavesAgainstCells<G10>(rng, 2000); });
        step("waves against the engine", [&] { testWavesAgainstEngine(rng); });
        step("reach against the engine", [&] { testReachAgainstEngine(); });
        step("predecessors against the engine", [&] { testPredecessorsAgainstEngine(rng); });
        step("verification against the engine", [&] { testVerifyAgainstEngine(rng); });
        step("search against generator.cpp", [&] { testGeneratorAgainstEngine(); });
        step("10 x 19 rules", [&] { testWideRules(); });
        step("10 x 19 generation", [&] { testWideGeneration(); });
        std::cout << "grid tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
