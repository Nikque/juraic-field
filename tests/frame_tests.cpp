// Tests of the frame-by-frame game (game/control.h, player.h, match.h).
//
//  1. the pair falls at the measured frames and locks after the grace period;
//  2. with the fastest input every placement takes exactly the time of the
//     measured tables (Timing::placeFrames / splitFrames);
//  3. a whole game played frame by frame gives the same fields, scores, garbage
//     and times as the move-level playTurn (which the Python simulator checks);
//  4. the placements the AI is offered (legalPlacements) against what the
//     controller can really reach, by a search over all inputs.
#include "../game/match.h"
#include <cstring>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

using namespace juraic;

static void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

static const Timing& TM = DEFAULT_TIMING;

// ---- 1. falling and locking ---------------------------------------------------------
static void testFall() {
    GameField empty;
    PairControl p = spawnPair(TM);
    std::vector<int> steps;
    int lockFrame = -1, landFrame = -1;
    for (int frame = 0; frame < 2000 && lockFrame < 0; ++frame) {
        const PairControl before = p;
        const ControlEvents e = stepPair(p, empty, 0, TM);
        if (before.y != p.y || before.upper != p.upper) steps.push_back(frame);
        if (e.landed) landFrame = frame;
        if (e.locked) lockFrame = frame;
    }
    const int want[] = {12, 25, 38, 51, 63, 76};
    for (int i = 0; i < 6; ++i) check(steps[size_t(i)] == want[i], "free fall: step " + std::to_string(i) + " at frame " + std::to_string(steps[size_t(i)]));
    check(steps.size() == 23 && p.y == 0 && !p.upper, "free fall does not end on the floor");
    // 23 half steps of 12.8 frames, then the grace period on the ground. Measured on the real game:
    // a pair left alone locks 44 frames after it came to rest, 43 when 8/256 of a cell were left
    // over from its last step (Timing::lockGain). On the floor 6/256 are left, on two pieces 8/256.
    check(landFrame == steps.back() && lockFrame == landFrame + 44, "natural lock on the floor is not 44 frames after landing: " + std::to_string(lockFrame - landFrame));
    {
        GameField two;
        addGarbage(two, SPAWN_X, 2);
        PairControl q = spawnPair(TM);
        int landed = -1, locked = -1;
        for (int frame = 0; frame < 2000 && locked < 0; ++frame) {
            const ControlEvents e = stepPair(q, two, 0, TM);
            if (e.landed) landed = frame;
            if (e.locked) locked = frame;
        }
        check(landed == 243 && locked == landed + 43, "natural lock on two pieces: landed in frame " + std::to_string(landed) + ", locked " + std::to_string(locked - landed) + " later");
    }

    // Soft drop: half a cell per frame, lock in the frame the pair lands, one point per row and two for landing with the key (13 from the top to the floor, as on the real game).
    p = spawnPair(TM);
    int bonus = 0, frames = 0;
    for (; !p.locked; ++frames) bonus += stepPair(p, empty, BTN_DOWN, TM).dropBonus;
    check(frames == 23 && bonus == 11 + TM.dropLockPoints + TM.dropLandPoints && bonus == 13, "soft drop: " + std::to_string(frames) + " frames, bonus " + std::to_string(bonus));

    // Floor kicks: the 8th landing locks the pair at once.
    Timing patient = TM;
    patient.lockGain = 30000; // so that the count of landings decides, not the time
    p = spawnPair(patient);
    int kicks = 0;
    uint8_t buttons = 0;
    for (int frame = 0; frame < 5000 && !p.locked; ++frame) {
        // on the ground: turn the child under the axis (floor kick), then back to the side
        buttons = p.grounded && !(buttons & (BTN_CW | BTN_CCW)) ? (p.dir == DOWN ? BTN_CCW : BTN_CW) : 0;
        const ControlEvents e = stepPair(p, empty, buttons, patient);
        kicks += e.kicked;
    }
    check(p.locked && p.landings == TM.maxLandings, "the 8th landing does not lock: landings " + std::to_string(p.landings));
    check(kicks >= 3, "no floor kick happened");

    // Quick turn in a one-wide well, and no rotation for quickTurnLock frames after it.
    GameField well;
    for (int y = 0; y < 12; ++y) well.f.set(1, y, GARBAGE), well.f.set(3, y, GARBAGE);
    p = spawnPair(TM);
    stepPair(p, well, BTN_CW, TM);
    check(p.dir == UP, "rotation in a well must wait for the second press");
    stepPair(p, well, 0, TM);
    stepPair(p, well, BTN_CW, TM);
    check(p.dir == DOWN && p.y == SPAWN_Y + 1 && p.upper, "quick turn did not turn the pair upside down");
    stepPair(p, well, 0, TM);
    stepPair(p, well, BTN_CW, TM);
    check(p.dir == DOWN, "rotation accepted right after a quick turn");

    // Repeated quick turns as recorded on the real game: child from above to
    // below puts the pair half a cell higher, from below to above half a cell
    // lower, and twelve frames after each turn the pair steps down half a cell.
    p = spawnPair(TM);
    while (p.y > 8 || p.upper || p.gain > 20) stepPair(p, well, 0, TM); // well below the top of the walls, just settled in a cell
    const auto quickTurn = [&] {
        stepPair(p, well, BTN_CW, TM);
        stepPair(p, well, 0, TM);
        stepPair(p, well, BTN_CW, TM);
    };
    const auto bottom2 = [&] { return 2 * std::min(int(p.y), p.childY()) + p.upper; }; // lower piece, in half cells
    int base = bottom2();
    for (int turn = 0; turn < 6; ++turn) {
        const uint8_t before = p.dir;
        quickTurn();
        check(p.dir == (before == UP ? DOWN : UP), "the quick turn did not swap the pieces");
        check(bottom2() == base + (before == UP ? 1 : -1), "quick turn " + std::to_string(turn) + ": the pair is not half a cell " + (before == UP ? "higher" : "lower"));
        base = bottom2();
        int waited = 1; // the frame of the turn itself
        while (bottom2() == base) stepPair(p, well, 0, TM), ++waited;
        check(waited == 13 && bottom2() == base - 1, "after a quick turn the next half step is not 12.8 frames later: " + std::to_string(waited));
        base = bottom2();
    }
}

// ---- helpers: play one move with the fastest input ----------------------------------
// The fastest input: rotate at once (twice, two frames apart, to put the child
// below), tap the direction every second frame, hold down.
struct FastInput {
    Placement target;
    int frame = 0;
    uint8_t operator()(const PairControl& p) {
        uint8_t b = BTN_DOWN;
        const int f = frame++;
        if (f % 2 == 0) {
            if (target.dir == RIGHT && p.dir == UP) b |= BTN_CW;
            if (target.dir == LEFT && p.dir == UP) b |= BTN_CCW;
            if (target.dir == DOWN && p.dir != DOWN) b |= BTN_CW;
            if (p.x < target.x) b |= BTN_RIGHT;
            if (p.x > target.x) b |= BTN_LEFT;
        }
        return b;
    }
};

// The frame before a pair appears: the input of the next frame already moves it.
static bool aboutToSpawn(const Player& p) { return p.phase == Phase::Next && p.wait == 1; }

// Plays one move, from the frame the pair appears to the frame before the next
// pair appears. Returns the number of frames.
static int playMove(Round& r, Placement target, int& softRows) {
    FastInput input{target};
    PlayerEvents e[2];
    softRows = 0;
    Player& p = r.player[0];
    check(aboutToSpawn(p), "playMove: no pair is about to appear");
    for (int frames = 1;; ++frames) {
        const uint8_t buttons[2] = {p.phase == Phase::Control ? input(p.pair) : aboutToSpawn(p) ? input(spawnPair(TM)) : uint8_t(0), 0};
        r.step(buttons, e);
        softRows += e[0].control.dropBonus;
        check(!e[0].died, "playMove: the pair could not appear");
        if (e[0].locked) check(p.pair.x == target.x && p.pair.dir == target.dir, "fast input reached another placement");
        if (aboutToSpawn(p)) return frames;
        check(frames < 5000, "playMove: the move does not end");
    }
}

static void toFirstPair(Round& r) {
    PlayerEvents e[2];
    const uint8_t none[2] = {0, 0};
    while (!aboutToSpawn(r.player[0])) r.step(none, e);
    check(r.frame == TM.firstSpawn, "the first pair does not appear on frame firstSpawn");
}

// ---- 2. the measured tables -----------------------------------------------------------
static void testPlacementTimes() {
    Rng rng(20261006, 1);
    Rules rules;
    int cases = 0, splits = 0;
    for (int i = 0; i < 4000; ++i) {
        Round r;
        r.start(rules, uint64_t(i) + 1, 1);
        // Garbage columns up to 9 high: nothing clears, and every placement is
        // reachable in a straight line from the spawn cell.
        int h[W];
        const int flat = int(rng.below(10));
        for (int x = 0; x < W; ++x) {
            h[x] = i % 2 ? int(rng.below(10)) : flat;
            addGarbage(r.player[0].field, x, h[x]);
        }
        toFirstPair(r);
        const Placement target = placement(int(rng.below(PLACEMENTS)));
        const GameField before = r.player[0].field;
        int soft;
        const int frames = playMove(r, target, soft);
        GameField expected = before;
        const PlaceResult placed = place(expected, target, r.tsumo.pair(0));
        check(r.player[0].field == expected, "frame game put the pair elsewhere");
        const int want = TM.placeFrames(target.x, placed.lockY) + TM.splitFrames(placed.split, target.x, placed.lockY);
        check(frames == want, "placement " + std::to_string(placementId(target)) + " at row " + std::to_string(placed.lockY) +
                                  " split " + std::to_string(placed.split) + ": " + std::to_string(frames) + " frames, table " +
                                  std::to_string(want));
        check(soft == SPAWN_Y - placed.lockY + TM.dropLockPoints + TM.dropLandPoints, "soft drop bonus is not one point per row and those of landing with the down key");
        ++cases, splits += placed.split > 0;
    }
    std::cout << "placement times: " << cases << " moves match the tables (" << splits << " with a split)\n";
}

// ---- 3. frame game against the move-level game ------------------------------------------
static void testAgainstTurns() {
    Rng rng(20261006, 2);
    long moves = 0, waves = 0, drops = 0, deaths = 0;
    for (int game = 0; game < 300; ++game) {
        Rules rules;
        if (game % 3 == 1) rules.marginSeconds = int(rng.below(40)), rules.marginStepSeconds = 1 + int(rng.below(10));
        if (game % 4 == 2) rules.clearCount = 2 + int(rng.below(4)), rules.colors = 3 + int(rng.below(3));
        if (game % 5 == 3) rules.chainRequirement = 2;
        const uint64_t seed = rng.next();
        Round r;
        r.start(rules, seed, 1);
        toFirstPair(r);
        // The move-level copy.
        PlayerCore core;
        Incoming sink;
        Rng coreRng(seed, GARBAGE_STREAM);
        for (int move = 0; move < 200; ++move) {
            Player& p = r.player[0];
            int h[W];
            p.field.heights(h);
            // Only what the straight fast input can do: low columns on the way.
            std::vector<int> ids;
            const unsigned legal = legalPlacements(p.field);
            for (int id = 0; id < PLACEMENTS; ++id) {
                if (!(legal >> id & 1)) continue;
                const Placement t = placement(id);
                bool low = true;
                // (one column more on each side: a wall kick at the spawn would move the pair)
                const int from = std::min(int(t.x), std::min(t.childX(), SPAWN_X)) - 1, to = std::max(int(t.x), std::max(t.childX(), SPAWN_X)) + 1;
                for (int x = std::max(from, 0); x <= std::min(to, W - 1); ++x) low = low && h[x] <= 9;
                if (low) ids.push_back(id);
            }
            if (ids.empty()) break;
            const Placement target = placement(ids[rng.below(ids.size())]);
            // Garbage from nowhere, the same for both.
            if (rng.below(6) == 0) {
                const int n = 1 + int(rng.below(40));
                p.incoming.confirmed += n, core.incoming.confirmed += n;
            }
            const int64_t spawnFrame = r.frame; // the frame in which the pair appears
            int soft;
            const int frames = playMove(r, target, soft);
            const bool died = p.field.dead();
            const TurnResult t = playTurn(core, sink, target, r.tsumo.pair(move), rules, TM, spawnFrame, coreRng, soft);
            const std::string at = " (game " + std::to_string(game) + ", move " + std::to_string(move) + ")";
            check(p.field == core.field, "fields differ" + at);
            check(p.scoring.score == core.scoring.score && p.scoring.pool == core.scoring.pool &&
                      p.scoring.allClear == core.scoring.allClear,
                  "scores differ" + at);
            check(p.incoming.confirmed == core.incoming.confirmed && r.nobody.confirmed == sink.confirmed && !r.nobody.pending,
                  "garbage differs" + at);
            check(died == t.dead, "death differs" + at);
            if (frames != t.frames) {
                std::cerr << "target " << placementId(target) << " lockY " << t.placed.lockY << " split " << t.placed.split << " chains " << t.chains
                          << " dropped " << t.dropped << " clearCount " << rules.clearCount << " heights";
                for (int x = 0; x < W; ++x) std::cerr << ' ' << h[x];
                std::cerr << std::endl;
            }
            check(frames == t.frames, "time differs: " + std::to_string(frames) + " frames, move level " + std::to_string(t.frames) + at);
            ++moves, waves += t.chains, drops += t.dropped > 0;
            if (died) {
                ++deaths;
                break;
            }
        }
    }
    std::cout << "frame game = move-level game: " << moves << " moves, " << waves << " waves, " << drops << " garbage drops, "
              << deaths << " deaths\n";
}

// ---- two players ----------------------------------------------------------------------------
// A pair that locks one frame before the opponent's chain ends is safe from
// that chain; in the same frame the garbage falls. Whoever is player 0.
static void testSameFrame() {
    for (int attacker = 0; attacker < 2; ++attacker)
        for (int lead = 0; lead <= 1; ++lead) {
            Round r;
            r.start(Rules{}, 1);
            Player& a = r.player[attacker];
            Player& d = r.player[1 - attacker];
            a.phase = Phase::Wave, a.wait = 10, a.chain.chains = 1;
            d.incoming.pending = 10;
            d.phase = Phase::Land, d.wait = 10 - lead;
            PlayerEvents e[2];
            const uint8_t none[2] = {0, 0};
            for (int i = 0; i < 10; ++i) r.step(none, e);
            const int fallen = d.field.f.garbage().count();
            check(a.phase == Phase::Next && d.incoming.pending == 0, "the chain did not end");
            if (lead) check(fallen == 0 && d.incoming.confirmed == 10 && d.phase == Phase::Next, "garbage fell although the pair locked first");
            else check(fallen == 10 && d.incoming.confirmed == 0 && d.phase == Phase::Garbage, "garbage did not fall in the same frame");
        }
}

// Two players pressing random buttons: garbage really travels, rounds end, and
// the checksum is the same on every compiler (compare the printed value).
static void testRandomMatches() {
    Rng rng(20261006, 4);
    uint64_t sum = 0;
    long frames = 0, sent = 0, fell = 0, offsets = 0;
    int results[4]{};
    for (int game = 0; game < 200; ++game) {
        Round r;
        Rules rules;
        if (game % 2) rules.colors = 3; // more chains
        r.start(rules, rng.next());
        uint8_t buttons[2] = {0, 0};
        PlayerEvents e[2];
        while (r.state == RoundState::Playing && r.frame < 60000) {
            for (int i = 0; i < 2; ++i)
                if (rng.below(4) == 0) buttons[i] = uint8_t(rng.below(32) | (rng.below(3) ? BTN_DOWN : 0));
            r.step(buttons, e);
            for (int i = 0; i < 2; ++i) {
                if (e[i].waveStarted) sent += r.player[i].wave.sent, offsets += r.player[i].wave.offsetted;
                fell += e[i].garbageFell;
                check(r.player[i].incoming.pending >= 0 && r.player[i].incoming.confirmed >= 0, "negative garbage");
            }
        }
        check(r.state != RoundState::Playing, "a round of random play did not end");
        ++results[int(r.state)];
        frames += long(r.frame);
        for (int i = 0; i < 2; ++i) {
            const BitField::Key k = r.player[i].field.f.key();
            sum = sum * 0x9E3779B97F4A7C15ull + KeyHash()(k) + uint64_t(r.player[i].scoring.score) + uint64_t(r.frame);
        }
    }
    check(sent > 100 && fell > 50 && offsets > 0 && results[1] > 20 && results[2] > 20, "random matches are not exercising the garbage");
    std::cout << "random matches: " << frames << " frames, " << sent << " garbage sent, " << offsets << " offset, " << fell
              << " drops, wins " << results[1] << '/' << results[2] << ", draws " << results[3] << ", checksum " << std::hex
              << uint32_t(sum ^ sum >> 32) << std::dec << '\n';
}

// ---- 4. what the controller can reach ------------------------------------------------------
static uint64_t packState(const PairControl& p) {
    uint64_t k = uint64_t(p.x);
    k = k << 4 | uint64_t(p.y);
    k = k << 2 | p.dir;
    k = k << 1 | uint64_t(p.upper);
    k = k << 1 | uint64_t(p.grounded);
    k = k << 8 | uint64_t(p.gain);
    k = k << 4 | p.landings;
    k = k << 6 | p.groundFrames;
    k = k << 5 | p.quickWindow;
    k = k << 2 | p.quickLock;
    return k;
}

// Every placement some input sequence leads to. The inputs are taps: a frame
// with buttons followed by a frame without (two frames is also what one
// horizontal move takes), or one frame of waiting. Soft drop is used only to
// lock a pair that is on the ground.
static unsigned reachableByInput(const GameField& g, size_t& states) {
    static const uint8_t taps[] = {BTN_LEFT, BTN_RIGHT, BTN_CW, BTN_CCW, BTN_LEFT | BTN_CW, BTN_LEFT | BTN_CCW,
                                   BTN_RIGHT | BTN_CW, BTN_RIGHT | BTN_CCW};
    unsigned reached = 0;
    std::unordered_set<uint64_t> seen;
    std::deque<PairControl> queue{spawnPair(TM)};
    seen.insert(packState(queue.front()));
    const auto visit = [&](const PairControl& q) {
        if (q.grounded || q.locked) reached |= 1u << placementId(q.placement());
        return !q.locked;
    };
    while (!queue.empty()) {
        const PairControl p = queue.front();
        queue.pop_front();
        for (int action = 0; action <= 8; ++action) {
            PairControl q = p;
            if (action < 8) {
                stepPair(q, g, taps[action], TM);
                if (!visit(q)) continue;
            }
            stepPair(q, g, 0, TM);
            if (!visit(q)) continue;
            if (seen.insert(packState(q)).second) queue.push_back(q);
        }
    }
    states = seen.size();
    return reached;
}

static std::string describe(const GameField& g, int id) {
    int h[W];
    g.heights(h);
    std::string s = "heights";
    for (int x = 0; x < W; ++x) s += ' ' + std::to_string(h[x]) + (g.row14[size_t(x)] ? "*" : "");
    const Placement p = placement(id);
    return s + ", axis column " + std::to_string(p.x + 1) + " child " + "URDL"[p.dir];
}

static void testReach(int fields) {
    Rng rng(20261006, 3);
    long legalTotal = 0, both = 0, onlyLegal = 0, onlyInput = 0;
    size_t statesMax = 0;
    std::string firstOnlyLegal, firstOnlyInput;
    for (int i = 0; i < fields; ++i) {
        GameField g;
        // Mostly high fields: that is where the rules matter.
        for (int x = 0; x < W; ++x) {
            const int h = rng.below(4) ? 9 + int(rng.below(5)) : int(rng.below(14));
            addGarbage(g, x, h);
            if (h == H && rng.below(2)) g.row14[size_t(x)] = GARBAGE;
        }
        if (g.dead()) continue;
        size_t states;
        const unsigned input = reachableByInput(g, states), legal = legalPlacements(g);
        statesMax = std::max(statesMax, states);
        for (int id = 0; id < PLACEMENTS; ++id) {
            const bool a = legal >> id & 1, b = input >> id & 1;
            legalTotal += a, both += a && b;
            if (a && !b && !onlyLegal++) firstOnlyLegal = describe(g, id);
            if (b && !a && !onlyInput++) firstOnlyInput = describe(g, id);
        }
        if (i % 10 == 9) std::cout << "  reach: " << i + 1 << " fields, largest search " << statesMax << " states" << std::endl;
    }
    std::cout << "reach: " << fields << " fields, " << legalTotal << " legal placements, " << both << " reached by input, "
              << onlyLegal << " legal but not reached, " << onlyInput << " reached but not legal (largest search "
              << statesMax << " states)\n";
    if (onlyLegal) std::cout << "  legal but not reached, e.g.: " << firstOnlyLegal << '\n';
    if (onlyInput) std::cout << "  reached but not legal, e.g.: " << firstOnlyInput << '\n';
    check(!onlyLegal, "a placement offered to the AI cannot be made with the controller");
}

// ---- 5. what was measured on the real game with the inputs written down (timing.h [M]) ------
// Plays one pair straight down with the down key, then the frames before the next pair appears
// with before(n) (n frames before the frame it appears; 1 = the frame before), then the frames
// from the one it appears in with after(k), k = 0 .. last. Returns the pair after frame `last`.
template <class Before, class After>
static PairControl secondPair(bool preInput, Before before, After after, int last) {
    Rules rules;
    rules.preInput = preInput;
    Round r;
    r.start(rules, 1, 1);
    PlayerEvents e[2];
    Player& p = r.player[0];
    for (int frame = 0;; ++frame) {
        const uint8_t b[2] = {BTN_DOWN, 0};
        r.step(b, e);
        if (e[0].locked) break;
        check(frame < 1000, "secondPair: the first pair does not lock");
    }
    const int wait = TM.landFrames + TM.nextFrames; // the next pair appears so many frames after the lock
    for (int n = wait - 1; n >= 1; --n) {
        const uint8_t b[2] = {uint8_t(before(n)), 0};
        r.step(b, e);
        check(!e[0].spawned, "secondPair: the next pair appears early");
    }
    for (int k = 0; k <= last; ++k) {
        const uint8_t b[2] = {uint8_t(after(k)), 0};
        r.step(b, e);
        check(k > 0 || e[0].spawned, "secondPair: the next pair does not appear when it should");
        check(p.phase == Phase::Control || k == last, "secondPair: the pair locked on the way");
    }
    return p.pair;
}

static void testMeasuredInput() {
    const auto nothing = [](int) { return 0; };
    const int W9 = TM.preFrames;
    check(W9 == 9 && TM.repeatDelay == 10 && TM.repeatInterval == 2, "the measured values are 9 frames of pre-input and an auto-repeat of 10 and 2");

    // The auto-repeat of a direction held in the air: the first move, the next 10 frames later, then every 2.
    {
        GameField empty;
        PairControl p = spawnPair(TM);
        std::vector<int> moves;
        for (int frame = 0; frame < 16; ++frame)
            if (stepPair(p, empty, BTN_RIGHT, TM).moved) moves.push_back(frame);
        check(moves == std::vector<int>({0, 10, 12}), "a direction held does not move the pair in frames 0, 10 and 12");
    }

    // A rotation pressed for one frame, n frames before the pair appears: done in the pair's first
    // frame from 9 frames before on, not from 10.
    for (int n = 1; n <= 14; ++n) {
        const PairControl p = secondPair(true, [n](int m) { return m == n ? BTN_CW : 0; }, nothing, 0);
        check(p.dir == (n <= W9 ? RIGHT : UP), "a rotation pressed " + std::to_string(n) + " frames before the pair: wrong");
        const PairControl off = secondPair(false, [n](int m) { return m == n ? BTN_CW : 0; }, nothing, 0);
        check(off.dir == UP, "without pre-input a rotation pressed before the pair turned it");
    }
    // Only held (pressed before the window): nothing. The last of two presses counts. A press in
    // the first frame goes before the one remembered.
    check(secondPair(true, [](int m) { return m <= 16 ? BTN_CW : 0; }, [](int) { return BTN_CW; }, 3).dir == UP, "a rotation button held since before the window turned the pair");
    check(secondPair(true, [](int m) { return m == 8 ? BTN_CCW : m == 3 ? BTN_CW : 0; }, nothing, 2).dir == RIGHT, "of two rotations pressed before the pair the last one does not count alone");
    check(secondPair(true, [](int m) { return m == 4 ? BTN_CW : 0; }, [](int k) { return k == 0 ? BTN_CCW : 0; }, 0).dir == LEFT, "a rotation pressed in the first frame does not go before the one remembered");

    // A direction tapped for one frame, n frames before the pair: one move, from 9 frames before on.
    for (int n = 1; n <= 14; ++n) {
        const PairControl p = secondPair(true, [n](int m) { return m == n ? BTN_LEFT : 0; }, nothing, 0);
        check(p.x == (n <= W9 ? SPAWN_X - 1 : SPAWN_X), "a direction tapped " + std::to_string(n) + " frames before the pair: wrong");
    }
    // Held from long before and on: the pair moves in its first frame and every second frame from there.
    {
        const auto held = [](int) { return BTN_RIGHT; };
        check(secondPair(true, held, held, 0).x == SPAWN_X + 1 && secondPair(true, held, held, 1).x == SPAWN_X + 1 &&
                  secondPair(true, held, held, 2).x == SPAWN_X + 2 && secondPair(true, held, held, 3).x == SPAWN_X + 2 &&
                  secondPair(true, held, held, 4).x == SPAWN_X + 3,
              "a direction held from long before does not move the pair in frames 0, 2 and 4");
        // Without pre-input the count starts anew as the pair appears: the first move comes in frame 9.
        check(secondPair(false, held, held, 8).x == SPAWN_X && secondPair(false, held, held, 9).x == SPAWN_X + 1, "without pre-input a held direction does not wait for its auto-repeat");
    }
    // Held from 7 frames before: the move of the first frame, then the auto-repeat 10 frames after the press.
    {
        const auto before = [](int m) { return m <= 7 ? BTN_RIGHT : 0; };
        const auto after = [](int) { return BTN_RIGHT; };
        check(secondPair(true, before, after, 0).x == SPAWN_X + 1 && secondPair(true, before, after, 2).x == SPAWN_X + 1 &&
                  secondPair(true, before, after, 3).x == SPAWN_X + 2 && secondPair(true, before, after, 5).x == SPAWN_X + 3,
              "a direction held from 7 frames before does not move the pair in frames 0, 3 and 5");
    }
    // Held until its auto-repeat came round inside the window, then let go: one move. Let go before
    // the window, or before the auto-repeat came round: none.
    check(secondPair(true, [](int m) { return m <= 14 && m >= 4 ? BTN_LEFT : 0; }, nothing, 0).x == SPAWN_X - 1, "a direction held 11 frames and let go 3 frames before the pair did not move it");
    check(secondPair(true, [](int m) { return m <= 25 && m >= 12 ? BTN_LEFT : 0; }, nothing, 0).x == SPAWN_X, "a direction let go 11 frames before the pair moved it");
    check(secondPair(true, [](int m) { return m <= 12 && m >= 3 ? BTN_LEFT : 0; }, nothing, 0).x == SPAWN_X, "a direction pressed before the window and let go before its auto-repeat moved the pair");

    // Quick drop: down to the ground and locked in the same frame, after a move of that frame too.
    {
        GameField g;
        addGarbage(g, SPAWN_X + 1, 3);
        PairControl p = spawnPair(TM);
        for (int i = 0; i < 20; ++i) stepPair(p, g, 0, TM);
        ControlEvents e = stepPair(p, g, BTN_DROP, TM);
        check(e.locked && p.locked && p.x == SPAWN_X && p.y == 0 && !p.upper, "the quick drop does not lock the pair on the floor");
        p = spawnPair(TM);
        e = stepPair(p, g, BTN_RIGHT | BTN_DROP, TM);
        check(e.locked && e.moved && p.x == SPAWN_X + 1 && p.y == 3 && !p.upper, "a move and the quick drop in one frame: the pair is not on the next column");
        // Held on: every pair drops in its first frame, the next one appears 28 frames later.
        Round r;
        r.start(Rules{}, 1, 1);
        PlayerEvents ev[2];
        std::vector<int64_t> appeared;
        const uint8_t drop[2] = {BTN_DROP, 0};
        while (appeared.size() < 3) {
            r.step(drop, ev);
            if (ev[0].spawned) appeared.push_back(r.frame), check(ev[0].locked, "a quick drop held as the pair appears does not drop it in its first frame");
        }
        check(appeared[1] - appeared[0] == 28 && appeared[2] - appeared[1] == 28, "pairs dropped at once do not follow each other 28 frames apart");
    }

    // A pair left alone: from the frame it can be moved to that of the next pair 352 frames on the
    // floor (294 down, 44 on the ground, 14 after the lock), 300 on two pieces; a pair that lies on
    // the ground and gets the down key locks in that frame, and the next pair comes 14 frames
    // later; within the bounce of its landing, when that is over (28 frames after the landing).
    {
        const auto nextAfter = [](int height, int downAfter) { // frames from one pair to the next; the down key so many frames after the landing (-1 = never)
            Round r;
            r.start(Rules{}, 1, 1);
            addGarbage(r.player[0].field, SPAWN_X, height);
            PlayerEvents e[2];
            int64_t first = -1, landed = -1;
            for (int frame = 0; frame < 2000; ++frame) {
                const uint8_t b[2] = {uint8_t(landed >= 0 && downAfter >= 0 && r.frame >= landed + downAfter ? BTN_DOWN : 0), 0};
                r.step(b, e);
                if (e[0].control.landed && landed < 0) landed = r.frame - 1;
                if (e[0].spawned && first >= 0) return int(r.frame - first);
                if (e[0].spawned) first = r.frame;
            }
            return -1;
        };
        check(nextAfter(0, -1) == 352 && nextAfter(2, -1) == 300, "a pair left alone: the next pair does not come 352 (floor) and 300 (two pieces) frames later");
        check(nextAfter(0, 27) == 294 + 27 + 14 && nextAfter(0, 41) == 294 + 41 + 14, "the down key on a pair that lies on the ground: the next pair does not come 14 frames later");
        check(nextAfter(0, 7) == 294 + 28 && nextAfter(0, 0) == 294 + 28, "the down key within the bounce of the landing: the next pair does not come 28 frames after the landing");
    }
    // The points of the down key at the end of a drop, when they are counted: two for a landing
    // with it, two for a lock with it soon after the landing, one for a lock with it later.
    {
        Timing points = TM;
        points.dropLockPoints = points.dropLandPoints = 1;
        GameField empty;
        const auto drop = [&](int from, int free) { // the down key from frame `from` to the landing, and again from `free` frames after the landing
            PairControl p = spawnPair(points);
            int sum = 0, landed = -1;
            for (int frame = 0; frame < 2000 && !p.locked; ++frame) {
                const bool down = landed < 0 ? frame >= from : frame >= landed + free;
                const ControlEvents e = stepPair(p, empty, down ? BTN_DOWN : 0, points);
                if (e.landed && landed < 0) landed = frame;
                sum += e.dropBonus;
            }
            return sum;
        };
        check(drop(0, 0) == 13, "down held from the top to the floor: not 11 rows and 2 points: " + std::to_string(drop(0, 0)));
        check(drop(5000, 7) == 2 && drop(5000, 27) == 1 && drop(5000, 41) == 1 && drop(5000, 100) == 0, "the down key after a landing by itself: not 2 points soon after, 1 later, none after the lock");
        PairControl q = spawnPair(points);
        check(stepPair(q, empty, BTN_DROP, points).dropBonus == 0, "a quick drop gets points");
    }

    // The lever of four ways: one direction at a time, the one pushed last.
    {
        Lever lever;
        check(lever(BTN_LEFT) == BTN_LEFT && lever(BTN_LEFT | BTN_DOWN) == BTN_DOWN && lever(BTN_LEFT | BTN_DOWN | BTN_CW) == (BTN_DOWN | BTN_CW) &&
                  lever(BTN_LEFT) == BTN_LEFT && lever(0) == 0,
              "lever: down pushed after left, then let go");
        check(lever(BTN_DOWN | BTN_RIGHT) == BTN_RIGHT && lever(BTN_DOWN | BTN_RIGHT) == BTN_RIGHT && lever(BTN_DOWN) == BTN_DOWN &&
                  lever(BTN_DOWN | BTN_LEFT) == BTN_LEFT && lever(BTN_DOWN) == BTN_DOWN,
              "lever: of two pushed together the sideways one, and taps beside a held down");
    }
    std::cout << "measured input: auto-repeat, pre-input, quick drop and the lever of four ways behave as measured\n";
}

int main(int argc, char** argv) {
    try {
        testFall();
        testMeasuredInput();
        testPlacementTimes();
        testAgainstTurns();
        testSameFrame();
        testRandomMatches();
        testReach(argc > 1 ? std::atoi(argv[1]) : 30);
        std::cout << "frame tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
