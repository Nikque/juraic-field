// Tests of game/ (move level): tsumo, placements, waves with any clear count,
// scoring, garbage exchange.
//
// Checked here against a cell-by-cell grid, and once more by an independent
// Python simulator (tests/test_game.py) that replays the log written by
//   game_tests <log file>
#include "../game/tsumo.h"
#include "../game/turn.h"
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace juraic;

static void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

// ---- tsumo ----------------------------------------------------------------------
static void testTsumo() {
    for (int colors = 3; colors <= 5; ++colors)
        for (int mode = 0; mode < 3; ++mode)
            for (uint64_t seed = 1; seed <= 300; ++seed) {
                Rules r;
                r.colors = colors;
                r.tsumoCorrection = TsumoCorrection(mode);
                const Tsumo t = makeTsumo(r, seed);
                check(t.piece == makeTsumo(r, seed).piece, "tsumo is not reproducible");
                int count[8]{};
                for (Cell c : t.piece) ++count[c];
                int used = 0, low = 999, high = 0;
                for (int c = 1; c <= 5; ++c)
                    if (count[c]) {
                        ++used;
                        low = std::min(low, count[c]);
                        high = std::max(high, count[c]);
                    }
                check(used == colors && high - low <= 1 && !count[0] && !count[6] && !count[7], "tsumo colours are not balanced");
                const size_t head = mode == 1 ? 4 : mode == 2 ? 6 : 0;
                std::set<Cell> first(t.piece.begin(), t.piece.begin() + long(head));
                check(first.size() <= 3, "tsumo correction failed");
                check(t.pair(TSUMO_PAIRS + 5).axis == t.piece[10] && t.pair(5).child == t.piece[11], "tsumo does not repeat");
            }
    // Without correction four colours do appear in the first two pairs sometimes.
    Rules plain;
    plain.tsumoCorrection = TsumoCorrection::None;
    int four = 0;
    for (uint64_t seed = 1; seed <= 300; ++seed) {
        const Tsumo t = makeTsumo(plain, seed);
        four += std::set<Cell>(t.piece.begin(), t.piece.begin() + 4).size() == 4;
    }
    check(four > 5, "uncorrected tsumo never has four colours");
}

// ---- cell-by-cell reference -------------------------------------------------------
struct Grid {
    Cell c[W][H]{};
};
static Grid toGrid(const BitField& f) {
    Grid g;
    for (int x = 0; x < W; ++x)
        for (int y = 0; y < H; ++y) g.c[x][y] = f.get(x, y);
    return g;
}
struct GridWave {
    std::set<std::pair<int, int>> cleared, garbage;
    int colors = 0, groups = 0, bonus = 0;
};
static bool gridWave(const Grid& g, GridWave& w, int clearCount) {
    bool seen[W][H]{};
    bool colorUsed[8]{};
    const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
    for (int x = 0; x < W; ++x)
        for (int y = 0; y < CLEAR_H; ++y) {
            if (seen[x][y] || g.c[x][y] < 1 || g.c[x][y] > 5) continue;
            std::vector<std::pair<int, int>> group{{x, y}};
            seen[x][y] = true;
            for (size_t i = 0; i < group.size(); ++i)
                for (int d = 0; d < 4; ++d) {
                    const int nx = group[i].first + dx[d], ny = group[i].second + dy[d];
                    if (nx < 0 || nx >= W || ny < 0 || ny >= CLEAR_H || seen[nx][ny] || g.c[nx][ny] != g.c[x][y]) continue;
                    seen[nx][ny] = true;
                    group.push_back({nx, ny});
                }
            if (int(group.size()) < clearCount) continue;
            ++w.groups;
            const int size = int(group.size()) + (clearCount < 4 ? 4 - clearCount : 0);
            w.bonus += size >= 11 ? 10 : size >= 5 ? size - 3 : 0;
            colorUsed[g.c[x][y]] = true;
            w.cleared.insert(group.begin(), group.end());
        }
    for (auto [x, y] : w.cleared)
        for (int d = 0; d < 4; ++d) {
            const int nx = x + dx[d], ny = y + dy[d];
            if (nx >= 0 && nx < W && ny >= 0 && ny < CLEAR_H && g.c[nx][ny] == GARBAGE) w.garbage.insert({nx, ny});
        }
    for (bool u : colorUsed) w.colors += u;
    return !w.cleared.empty();
}
static std::set<std::pair<int, int>> cellsOf(FieldBits bits) {
    std::set<std::pair<int, int>> s;
    forEachCell(bits, [&](int x, int y) { s.insert({x, y}); });
    return s;
}

static BitField randomSettled(Rng& rng, int colors, int garbagePercent) {
    BitField f;
    for (int x = 0; x < W; ++x) {
        const int h = int(rng.below(H + 1));
        for (int y = 0; y < h; ++y)
            f.set(x, y, int(rng.below(100)) < garbagePercent ? GARBAGE : Cell(1 + rng.below(uint64_t(colors))));
    }
    return f;
}

static void testWaves() {
    Rng rng(20261005, 1);
    for (int i = 0; i < 40000; ++i) {
        const int clearCount = 2 + int(rng.below(6));
        const BitField f = randomSettled(rng, 2 + int(rng.below(4)), int(rng.below(30)));
        Wave w;
        GridWave gw;
        const bool a = findWaveFor(f, w, clearCount), b = gridWave(toGrid(f), gw, clearCount);
        check(a == b, "findWaveFor disagrees on whether something clears");
        if (!a) continue;
        check(cellsOf(w.cleared) == gw.cleared && cellsOf(w.garbage) == gw.garbage, "findWaveFor clears other cells");
        check(w.pieces == int(gw.cleared.size()) && w.colors == gw.colors && w.groups == gw.groups && w.groupBonus == gw.bonus,
              "findWaveFor counts differ");
        // longest fall against the grid
        Grid g = toGrid(f);
        for (auto [x, y] : gw.cleared) g.c[x][y] = 0;
        for (auto [x, y] : gw.garbage) g.c[x][y] = 0;
        int longest = 0;
        for (int x = 0; x < W; ++x)
            for (int y = 0, erasedBelow = 0; y < H; ++y) {
                if (f.get(x, y) && !g.c[x][y]) ++erasedBelow;
                else if (g.c[x][y]) longest = std::max(longest, erasedBelow);
            }
        check(longestFall(f, w.cleared | w.garbage) == longest, "longestFall differs");
    }
    check(chainBonus(1) == 0 && chainBonus(19) == 512 && chainBonus(21) == 576, "chain bonus table");
    Wave big;
    big.pieces = 72, big.colors = 5, big.groupBonus = 600;
    check(gameWaveScore(big, 19) == 10 * 72 * 999, "score multiplier is not capped");
    Wave one;
    one.pieces = 4, one.colors = 1;
    check(gameWaveScore(one, 1) == 40, "single clear is not 40 points");
}

// ---- placements ---------------------------------------------------------------------
static void testPlacements() {
    for (int id = 0; id < PLACEMENTS; ++id) {
        const Placement p = placement(id);
        check(placementId(p) == id && p.childX() >= 0 && p.childX() < W, "placement numbering");
    }
    Rng rng(20261005, 2);
    for (int i = 0; i < 30000; ++i) {
        GameField g;
        g.f = randomSettled(rng, 4, 10);
        for (int x = 0; x < W; ++x)
            if (std::popcount(g.f.occupied().column(x)) == H && rng.below(2)) g.row14[size_t(x)] = 1;
        int h[W];
        g.heights(h);
        const unsigned legal = legalPlacements(g);
        if (h[SPAWN_X] >= 12) check(!legal, "placement although the spawn column is full");
        for (int id = 0; id < PLACEMENTS; ++id) {
            if (!(legal >> id & 1)) continue;
            const Placement p = placement(id);
            const Pair pair{Cell(1 + rng.below(4)), Cell(1 + rng.below(4))};
            GameField after = g;
            const PlaceResult r = place(after, p, pair);
            // reference: let the two pieces fall cell by cell
            const int ax = p.x, cx = p.childX();
            int ay, cy;
            if (p.dir == UP) ay = h[ax], cy = ay + 1;
            else if (p.dir == DOWN) cy = h[ax], ay = cy + 1;
            else ay = h[ax], cy = h[cx];
            check(r.axisY == ay && r.childY == cy, "place: wrong rows");
            check(ay < ROW_14, "axis piece in the 14th row");
            check(cy < ROW_14 || !g.row14[size_t(cx)], "14th row used twice");
            check(r.split == (p.dir == UP || p.dir == DOWN ? 0 : std::abs(ay - cy)), "place: wrong split");
            for (int x = 0; x < W; ++x)
                for (int y = 0; y <= ROW_14; ++y) {
                    const Cell before = y == ROW_14 ? g.row14[size_t(x)] : g.f.get(x, y);
                    const Cell now = y == ROW_14 ? after.row14[size_t(x)] : after.f.get(x, y);
                    const Cell want = x == ax && y == ay ? pair.axis : x == cx && y == cy ? pair.child : before;
                    check(now == want, "place: wrong cell");
                }
        }
    }
    // An empty field allows all 22.
    check(legalPlacements(GameField{}) == (1u << PLACEMENTS) - 1, "empty field does not allow every placement");
}

// ---- garbage arithmetic ---------------------------------------------------------------
static void testGarbage() {
    Rules r;
    check(r.rateAt(0) == 70 && r.rateAt(96 * 60 - 1) == 70 && r.rateAt(96 * 60) == 52 && r.rateAt(112 * 60) == 34 &&
              r.rateAt(255 * 60) == 2 && r.rateAt(256 * 60) == 1 && r.rateAt(100000 * 60) == 1,
          "margin time table");
    Scoring s;
    check(convert(s, 40, 70) == 0 && s.pool == 40 && convert(s, 320, 70) == 5 && s.pool == 10 && s.score == 360, "convert");
    Incoming in{3, 4};
    check(offset(in, 2) == 0 && in.pending == 1 && in.confirmed == 4, "offset takes pending first");
    check(offset(in, 9) == 4 && in.total() == 0, "offset remainder");
    // All clear: 2100 points on the next chain, 30 garbage at rate 70.
    PlayerCore me;
    Incoming opp;
    Rng rng(1);
    for (int x = 0; x < 2; ++x) me.field.f.set(x, 0, 1);
    TurnResult t = playTurn(me, opp, placement(6 + 2), Pair{1, 1}, r, DEFAULT_TIMING, 0, rng);
    check(t.chains == 1 && t.allClear && me.scoring.allClear && t.sent == 0 && me.scoring.pool == 40, "all clear not detected");
    for (int x = 0; x < 2; ++x) me.field.f.set(x, 0, 2);
    t = playTurn(me, opp, placement(6 + 2), Pair{2, 2}, r, DEFAULT_TIMING, 0, rng);
    check(t.points == 2140 && t.sent == (40 + 2140) / 70 && opp.confirmed == t.sent && !opp.pending, "all clear bonus");
    // Chain requirement: a chain that reaches it attacks with its full score,
    // a shorter one sends nothing and leaves nothing in the pool.
    for (int requirement = 1; requirement <= 3; ++requirement) {
        Rules shibari;
        shibari.chainRequirement = requirement;
        PlayerCore p;
        for (int y = 0; y < 3; ++y) p.field.f.set(0, y, 1);
        p.field.f.set(1, 0, 2), p.field.f.set(1, 1, 2), p.field.f.set(2, 0, 2);
        Incoming target;
        t = playTurn(p, target, placement(1), Pair{1, 2}, shibari, DEFAULT_TIMING, 0, rng);
        check(t.chains == 2 && t.points == 360 && p.scoring.score == 360, "two-chain for the requirement test");
        check(t.sent == (requirement <= 2 ? 5 : 0) && p.scoring.pool == (requirement <= 2 ? 10 : 0), "chain requirement");
    }
    // 36 confirmed garbage: 30 fall (five rows), 6 stay.
    PlayerCore victim;
    victim.incoming.confirmed = 36;
    Incoming none;
    t = playTurn(victim, none, placement(0), Pair{1, 2}, r, DEFAULT_TIMING, 0, rng);
    check(t.dropped == 30 && victim.incoming.confirmed == 6 && victim.field.f.garbage().count() == 30, "garbage drop limit");
    int h[W];
    victim.field.heights(h);
    check(h[0] == 7 && h[1] == 5 && h[5] == 5, "garbage rows");
}

// ---- log for the independent Python simulator ---------------------------------------------
static std::string cells(const GameField& g) {
    std::string s;
    for (int x = 0; x < W; ++x)
        for (int y = 0; y < H; ++y) s += char('0' + g.f.get(x, y));
    for (int x = 0; x < W; ++x) s += char('0' + g.row14[size_t(x)]);
    return s;
}

static void writeLog(const std::string& path, int games) {
    std::ofstream out(path, std::ios::binary);
    const Timing& tm = DEFAULT_TIMING;
    out << "TIMING " << tm.nextFrames << ' ' << tm.placeBase << ' ' << tm.placePerColumn << ' ' << tm.placePerRow << ' ' << tm.waveQuick << ' '
        << tm.waveBase << ' ' << tm.wavePerCell;
    for (int v : tm.split) out << ' ' << v;
    for (auto& row : tm.garbageCell)
        for (int v : row) out << ' ' << v;
    for (int v : tm.garbageShake) out << ' ' << v;
    out << '\n';
    Rng rng(20261005, 3);
    long turns = 0, chains = 0, sent = 0, drops = 0, allClears = 0, deaths = 0;
    for (int game = 0; game < games; ++game) {
        Rules r;
        r.colors = game % 4 == 0 ? 3 + int(rng.below(3)) : 4;
        r.clearCount = game % 5 == 0 ? 2 + int(rng.below(5)) : 4;
        r.tsumoCorrection = TsumoCorrection(rng.below(3));
        if (game % 3 == 0) {
            r.targetPoints = 20 + int(rng.below(150));
            r.marginSeconds = int(rng.below(60));
            r.marginStepSeconds = 1 + int(rng.below(20));
            r.marginSteps = 1 + int(rng.below(8));
            int rate = r.targetPoints;
            for (int i = 0; i < r.marginSteps; ++i) r.marginRates[size_t(i)] = rate = std::max(1, rate * 3 / 4);
            r.chainRequirement = 1 + int(rng.below(3));
            r.allClearBonus = int(rng.below(3000));
            r.softDropBonus = rng.below(2) != 0;
            r.maxGarbagePerDrop = 1 + int(rng.below(40));
        }
        const uint64_t seed = rng.next();
        const Tsumo tsumo = makeTsumo(r, seed);
        out << "GAME " << r.colors << ' ' << r.clearCount << ' ' << r.targetPoints << ' ' << r.marginSeconds << ' '
            << r.marginStepSeconds << ' ' << r.marginSteps;
        for (int i = 0; i < r.marginSteps; ++i) out << ' ' << r.marginRates[size_t(i)];
        out << ' ' << r.chainRequirement << ' ' << r.allClearBonus << ' ' << int(r.softDropBonus) << ' ' << r.maxGarbagePerDrop << '\n';
        PlayerCore player[2];
        int next[2] = {0, 0};
        int64_t frame = 0;
        Rng garbageRng(seed, 99);
        for (int turn = 0; turn < 400; ++turn) {
            const int who = int(rng.below(2));
            PlayerCore& me = player[who];
            const unsigned legal = legalPlacements(me.field);
            if (!legal) break;
            int id;
            do id = int(rng.below(PLACEMENTS));
            while (!(legal >> id & 1));
            const Pair pair = tsumo.pair(next[who]++);
            const int soft = int(rng.below(13));
            frame += int64_t(rng.below(400));
            const TurnResult t = playTurn(me, player[1 - who].incoming, placement(id), pair, r, tm, frame, garbageRng, soft);
            out << "T " << who << ' ' << frame << ' ' << id << ' ' << int(pair.axis) << ' ' << int(pair.child) << ' ' << soft
                << ' ' << t.chains << ' ' << t.points << ' ' << t.sent << ' ' << t.offsetted << ' ' << t.dropped << ' '
                << int(t.allClear) << ' ' << int(t.dead) << ' ' << t.frames << ' ' << t.chainEndFrame << ' ' << cells(me.field)
                << ' ' << me.scoring.score << ' ' << me.scoring.pool << ' ' << me.incoming.pending << ' '
                << me.incoming.confirmed << ' ' << player[1 - who].incoming.pending << ' '
                << player[1 - who].incoming.confirmed << '\n';
            ++turns, chains += t.chains, sent += t.sent, drops += t.dropped > 0, allClears += t.allClear, deaths += t.dead;
            if (t.dead) break;
        }
    }
    std::cout << "log: " << games << " games, " << turns << " turns, " << chains << " waves, " << sent << " garbage sent, "
              << drops << " drops, " << allClears << " all clears, " << deaths << " deaths\n";
}

int main(int argc, char** argv) {
    try {
        testTsumo();
        testWaves();
        testPlacements();
        testGarbage();
        if (argc > 1) writeLog(argv[1], 600);
        std::cout << "game tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
