// Tests of the versus AI (ai/).
//
//  1. the observation: the players are copied as they are, the tsumo is known
//     exactly as far as it has been on the screen, and nothing hidden (the
//     rest of the tsumo, the seed, the garbage columns) changes it.
//  2. the two-player simulator (ai/duel.h) against the frame-by-frame Round.
//  3. from a placement to buttons (ai/input.h): the straight way takes the time
//     of the tables, and on high fields every placement the AI is offered gets
//     a path that stepPair really locks there.
//
// "ai_tests bench" measures instead of testing; a number is the number of
// high fields of test 3.
#include "../ai/agents.h"
#include "../play/realgame_read.h"
#include "../ai/answer.h"
#include "../ai/duel.h"
#include "../ai/build.h"
#include "../ai/detect.h"
#include "../ai/exchange.h"
#include "../ai/tactics.h"
#include "../ai/input.h"
#include "../ai/params.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>

using namespace juraic;
using ai::Observation;

static void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

#include "lv38_tail_tests.h"

static void testLv38AnswerWindowBoundary() {
    GameField field;
    for (int y = 0; y < 3; ++y) field.f.set(0, y, 2);
    const Rules rules;
    const Timing timing = DEFAULT_TIMING;
    const Pair pairs[] = {{4, 4}, {2, 4}};
    ai::Params params;
    params.beamWidth = 40;
    params.beamDepth = 2;
    params.beamQueues = 1;
    constexpr int first = 5;
    ai::BuildAnswerWindow window;
    window.confirm = 100;
    window.offsetPoints = 40;
    window.returnPoints = 41;
    window.nextSpawn[first] = window.confirm + timing.nextFrames - 1;
    ai::Builder builder;
    const auto before = builder.search(field, pairs, 2, rules, timing, params, 1, 1, &window);
    ++window.nextSpawn[first];
    const auto same = builder.search(field, pairs, 2, rules, timing, params, 1, 1, &window);
    check(before.shortOffset[first] == 1 && !before.shortReturn[first] && !same.shortOffset[first],
          "Lv38 counts the next fire after confirmation only when the quiet placement settled earlier");
}

// ---- 1. the observation ---------------------------------------------------------------
// The type itself holds nothing hidden.
template <class T> concept HasSeed = requires(T t) { t.seed; };
template <class T> concept HasGarbageRng = requires(T t) { t.garbageRng; };
static_assert(HasSeed<Round> && HasGarbageRng<Round>);
static_assert(!HasSeed<Observation> && !HasGarbageRng<Observation>);
static_assert(!std::is_same_v<decltype(Observation::tsumo), Tsumo>);

static bool sameRules(const Rules& a, const Rules& b) {
    return a.colors == b.colors && a.palette == b.palette && a.clearCount == b.clearCount &&
           a.tsumoCorrection == b.tsumoCorrection && a.targetPoints == b.targetPoints &&
           a.marginSeconds == b.marginSeconds && a.marginStepSeconds == b.marginStepSeconds &&
           a.marginSteps == b.marginSteps && a.marginRates == b.marginRates && a.chainRequirement == b.chainRequirement &&
           a.allClearBonus == b.allClearBonus && a.softDropBonus == b.softDropBonus &&
           a.maxGarbagePerDrop == b.maxGarbagePerDrop && a.speedPercent == b.speedPercent && a.winsNeeded == b.winsNeeded;
}

static bool samePlayer(const Player& a, const Player& b) {
    bool same = a.field == b.field && a.scoring.score == b.scoring.score && a.scoring.pool == b.scoring.pool &&
                a.scoring.allClear == b.scoring.allClear && a.incoming.pending == b.incoming.pending &&
                a.incoming.confirmed == b.incoming.confirmed && a.phase == b.phase && a.wait == b.wait &&
                a.nextPair == b.nextPair && a.colors.axis == b.colors.axis && a.colors.child == b.colors.child &&
                a.pair == b.pair && a.buttons == b.buttons && a.chain.chains == b.chain.chains && a.chain.held == b.chain.held &&
                a.placed.axisY == b.placed.axisY && a.placed.childY == b.placed.childY && a.placed.lockY == b.placed.lockY &&
                a.placed.split == b.placed.split && a.wave.wave.cleared == b.wave.wave.cleared &&
                a.wave.wave.garbage == b.wave.wave.garbage && a.wave.wave.pieces == b.wave.wave.pieces &&
                a.wave.wave.colors == b.wave.wave.colors && a.wave.wave.groups == b.wave.wave.groups &&
                a.wave.wave.groupBonus == b.wave.wave.groupBonus && a.wave.longestFall == b.wave.longestFall &&
                a.wave.points == b.wave.points && a.wave.sent == b.wave.sent && a.wave.offsetted == b.wave.offsetted &&
                a.moves == b.moves;
    for (int x = 0; x < W; ++x) same = same && a.dropped[x] == b.dropped[x];
    return same;
}

static bool sameObservation(const Observation& a, const Observation& b) {
    if (!sameRules(a.rules, b.rules) || !(a.timing == b.timing) || a.players != b.players ||
        a.me != b.me || a.frame != b.frame || a.state != b.state || a.tsumo.count() != b.tsumo.count())
        return false;
    for (int i = 0; i < 2; ++i)
        if (!samePlayer(a.player[i], b.player[i])) return false;
    for (int i = 0; i < a.tsumo.count(); ++i)
        if (a.tsumo.pair(i).axis != b.tsumo.pair(i).axis || a.tsumo.pair(i).child != b.tsumo.pair(i).child) return false;
    return true;
}

// `shown` = number of pairs that have been on the screen, counted by the caller
// from the events of the round (not from Player::nextPair).
static void checkObservation(const Round& r, int shown, Observation& o, const std::string& at) {
    for (int me = 0; me < r.players; ++me) {
        ai::observe(r, me, o);
        check(o.me == me && o.frame == r.frame && o.players == r.players && o.state == r.state, "header differs" + at);
        check(sameRules(o.rules, r.rules) && o.timing == r.timing, "rules differ" + at);
        check(samePlayer(o.player[0], r.player[0]) && samePlayer(o.player[1], r.player[1]), "players differ" + at);
        check(&o.self() == &o.player[me] && &o.opponent() == &o.player[1 - me], "self / opponent" + at);
        check(o.tsumo.count() == shown, "pairs seen: " + std::to_string(o.tsumo.count()) + ", shown " + std::to_string(shown) + at);
        check(!o.tsumo.known(-1) && !o.tsumo.known(shown) && !o.tsumo.known(shown + TSUMO_PAIRS), "a pair not shown is known" + at);
        for (int i = 0; i < shown; ++i) {
            const Pair seen = o.tsumo.pair(i), real = r.tsumo.pair(i);
            check(o.tsumo.known(i) && seen.axis == real.axis && seen.child == real.child, "pair " + std::to_string(i) + " differs" + at);
        }
    }
}

// The same round with everything hidden replaced must look the same.
static void checkHidden(const Round& r, int shown, Rng& rng, const std::string& at) {
    Round other = r;
    other.seed = rng.next();
    for (int i = 0; i < 2; ++i) other.garbageRng[i] = Rng(rng.next(), rng.next());
    // (once 128 pairs have been shown, the whole tsumo has)
    for (size_t i = size_t(shown) * 2; i < other.tsumo.piece.size(); ++i) other.tsumo.piece[i] = Cell(1 + rng.below(5));
    for (int me = 0; me < r.players; ++me)
        check(sameObservation(ai::observe(r, me), ai::observe(other, me)), "the observation depends on something hidden" + at);
}

// How many pairs the screen has shown, from the events of a round alone (not from Player::nextPair
// or the phases): the pair in play is number spawned - 1 and the window shows two more; and in the
// last `early` frames before a pair appears (Timing::nextShown) the window has moved on and shows
// the pair after those. So what a frame should have shown is known only when the frames after it
// have been played: every frame is kept and looked at when the round is over. (Not its last frames:
// a pair that would have appeared after the end never does.)
struct ShownPairs {
    std::vector<int> count, spawned[2]; // by the frames played
    void add(int seen, const int now[2]) { count.push_back(seen), spawned[0].push_back(now[0]), spawned[1].push_back(now[1]); }
    void verify(int early, const std::string& at) const {
        // (early + 1 frames at the end are left out: a player who dies as his pair should appear has
        // been shown the next one for `early` frames, and the round ends in the frame after them)
        for (size_t f = 0; f + size_t(early > 0 ? early + 1 : 0) < count.size(); ++f) {
            int want = 0;
            for (int i = 0; i < 2; ++i) want = std::max(want, spawned[i][f] + 2 + (early > 0 && spawned[i][f + size_t(early)] > spawned[i][f] ? 1 : 0));
            if (count[f] != want) {
                check(false, "pairs seen: " + std::to_string(count[f]) + ", shown " + std::to_string(want) + at + ", after " + std::to_string(f) + " frames");
                return;
            }
        }
    }
};

// Random buttons, the down key held most of the time.
static void randomButtons(Rng& rng, uint8_t buttons[2]) {
    for (int i = 0; i < 2; ++i)
        if (rng.below(4) == 0) buttons[i] = uint8_t(rng.below(32) | (rng.below(3) ? BTN_DOWN : 0));
}

static void testObserve() {
    Rng rng(20261006, 10);
    Observation o;
    long frames = 0, hidden = 0;
    int ahead = 0, most = 0;
    // Two players, and every fifth round one player.
    for (int game = 0; game < 60; ++game) {
        Rules rules;
        if (game % 2) rules.colors = 3;
        if (game % 4 == 2) rules.clearCount = 2 + int(rng.below(4)), rules.colors = 3 + int(rng.below(3));
        if (game % 3 == 1) rules.marginSeconds = int(rng.below(40));
        Round r;
        r.start(rules, rng.next(), game % 5 == 4 ? 1 : 2);
        int spawned[2] = {0, 0};
        uint8_t buttons[2] = {0, 0};
        PlayerEvents e[2];
        ShownPairs all;
        checkObservation(r, 2, o, " (game " + std::to_string(game) + ", before the first frame)");
        all.add(o.tsumo.count(), spawned);
        // (a single player pressing at random can last long: one such round took more than 60000 frames)
        while (r.state == RoundState::Playing && r.frame < 600000) {
            randomButtons(rng, buttons);
            r.step(buttons, e);
            for (int i = 0; i < r.players; ++i) spawned[i] += e[i].spawned;
            // (the number of pairs itself is looked at when the round is over: ShownPairs)
            const int shown = ai::seenPairs(r);
            const std::string at = " (game " + std::to_string(game) + ", frame " + std::to_string(r.frame) + ")";
            checkObservation(r, shown, o, at);
            all.add(o.tsumo.count(), spawned);
            if (rng.below(64) == 0) checkHidden(r, shown, rng, at), ++hidden;
            if (r.players == 2) ahead = std::max(ahead, std::abs(spawned[0] - spawned[1]));
            most = std::max(most, shown);
        }
        check(r.state != RoundState::Playing, "a round of random play did not end");
        all.verify(DEFAULT_TIMING.nextShown, " (game " + std::to_string(game) + ")");
        frames += long(r.frame);
    }
    check(ahead >= 3, "no player got ahead of the other");
    check(DEFAULT_TIMING.nextShown == 4, "the pair that comes into the window is not shown four frames before a pair appears");

    // A long round: more than 128 pairs. Pair 128 + k is the same as pair k,
    // and still unknown until it has been shown.
    int longest = 0;
    for (int game = 0; game < 20 && longest < TSUMO_PAIRS + 12; ++game) {
        Rules rules;
        rules.colors = 3, rules.clearCount = 2; // almost everything clears: the players live long
        Round r;
        r.start(rules, rng.next());
        int spawned[2] = {0, 0};
        uint8_t buttons[2] = {0, 0};
        PlayerEvents e[2];
        ShownPairs all;
        all.add(ai::seenPairs(r), spawned);
        while (r.state == RoundState::Playing && std::max(spawned[0], spawned[1]) < TSUMO_PAIRS + 20) {
            // Each player drops the pair on the lowest column.
            for (int i = 0; i < 2; ++i) {
                int h[W], low = 0;
                r.player[i].field.heights(h);
                for (int x = 1; x < W; ++x)
                    if (h[x] < h[low]) low = x;
                const int x = r.player[i].phase == Phase::Control ? r.player[i].pair.x : SPAWN_X;
                buttons[i] = uint8_t(rng.below(8) ? BTN_DOWN : 0);
                if (r.frame % 2 == 0 && x != low) buttons[i] |= x < low ? BTN_RIGHT : BTN_LEFT;
            }
            r.step(buttons, e);
            for (int i = 0; i < 2; ++i) spawned[i] += e[i].spawned;
            const int shown = ai::seenPairs(r);
            const std::string at = " (long game " + std::to_string(game) + ", frame " + std::to_string(r.frame) + ")";
            checkObservation(r, shown, o, at);
            all.add(o.tsumo.count(), spawned);
            if (rng.below(64) == 0) checkHidden(r, shown, rng, at), ++hidden;
            longest = std::max(longest, shown);
        }
        all.verify(DEFAULT_TIMING.nextShown, " (long game " + std::to_string(game) + ")");
        frames += long(r.frame);
    }
    check(longest >= TSUMO_PAIRS + 12, "no round went past 128 pairs: " + std::to_string(longest));
    std::cout << "observation: " << frames << " frames, at most " << most << " pairs seen in random play, " << longest
              << " in the long round, one player up to " << ahead << " pairs ahead, " << hidden << " checks with the hidden parts replaced\n";
}

// ---- 2. the two-player simulator --------------------------------------------------------
static const Timing& TM = DEFAULT_TIMING;

// The fastest input, as in frame_tests.cpp: rotate at once, tap the direction
// every second frame, hold down.
struct FastInput {
    Placement target{};
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

static bool aboutToSpawn(const Player& p) { return p.phase == Phase::Next && p.wait == 1; }

// A random placement among those the straight fast input can make (low columns
// on the way, as in frame_tests.cpp testAgainstTurns). False when there is none.
static bool straightPlacement(const GameField& g, Rng& rng, Placement& target) {
    int h[W], ids[PLACEMENTS], n = 0;
    g.heights(h);
    const unsigned legal = legalPlacements(g);
    for (int id = 0; id < PLACEMENTS; ++id) {
        if (!(legal >> id & 1)) continue;
        const Placement t = placement(id);
        bool low = true;
        const int from = std::min(int(t.x), std::min(t.childX(), SPAWN_X)) - 1, to = std::max(int(t.x), std::max(t.childX(), SPAWN_X)) + 1;
        for (int x = std::max(from, 0); x <= std::min(to, W - 1); ++x) low = low && h[x] <= 9;
        if (low) ids[n++] = id;
    }
    if (!n) return false;
    target = placement(ids[rng.below(uint64_t(n))]);
    return true;
}

static bool sameCore(const PlayerCore& a, const PlayerCore& b) {
    return a.field == b.field && a.scoring.score == b.scoring.score && a.scoring.pool == b.scoring.pool &&
           a.scoring.allClear == b.scoring.allClear && a.incoming.pending == b.incoming.pending &&
           a.incoming.confirmed == b.incoming.confirmed;
}

// (a) Two players put random pairs with the fastest input, in Round and in the
//     Duel. Whenever a pair appears: the same frame, field, score, carried
//     points, all clear and garbage. The waiting garbage is compared in every frame.
// (b) Now and then the Duel is thrown away and made again from an observation
//     of that frame. A pair that was in mid-air then is put with the frame of
//     its lock and the rows of soft drop that followed, as the Round shows them.
static void testDuel() {
    Rng rng(20261006, 11);
    long moves = 0, waves = 0, sent = 0, offsets = 0, drops = 0, bothChains = 0, rebuilt = 0, rebuiltMidAir = 0, sameFrameWaves = 0;
    int results[4]{}, unfinished = 0;
    const int games = 400;
    for (int game = 0; game < games; ++game) {
        Rules rules;
        if (game % 2) rules.colors = 3; // more chains
        if (game % 3 == 1) rules.marginSeconds = int(rng.below(40)), rules.marginStepSeconds = 1 + int(rng.below(10));
        if (game % 4 == 2) rules.clearCount = 2 + int(rng.below(4)), rules.colors = 3 + int(rng.below(3));
        if (game % 5 == 3) rules.chainRequirement = 2;
        const bool fromNowhere = game % 3 == 0; // garbage that nobody sent, as well
        Round r;
        r.start(rules, rng.next());
        Observation o = ai::observe(r, 0);
        ai::Duel d(o);
        for (int i = 0; i < 2; ++i) d.garbageRng[i] = r.garbageRng[i];
        FastInput input[2];
        bool midAir[2] = {false, false}; // the pair was in the player's hands when the Duel was made
        int soft[2] = {0, 0};
        PlayerEvents e[2];
        bool stop = false;
        while (r.state == RoundState::Playing && !stop) {
            const std::string at = " (game " + std::to_string(game) + ", frame " + std::to_string(r.frame) + ")";
            uint8_t buttons[2] = {0, 0};
            for (int i = 0; i < 2; ++i) {
                Player& p = r.player[i];
                if (p.phase == Phase::Control) {
                    buttons[i] = input[i](p.pair);
                } else if (aboutToSpawn(p) && !p.field.dead()) {
                    Placement target;
                    if (!straightPlacement(p.field, rng, target)) {
                        stop = true;
                        break;
                    }
                    input[i] = FastInput{target};
                    buttons[i] = input[i](spawnPair(TM));
                    if (fromNowhere && rng.below(8) == 0) {
                        const int n = 1 + int(rng.below(40));
                        p.incoming.confirmed += n, d.player[i].incoming.confirmed += n;
                    }
                }
            }
            if (stop) break;
            r.step(buttons, e);
            const int64_t played = r.frame - 1;
            const int appeared = d.run(r.frame);
            check(d.frame == r.frame, "the duel is at frame " + std::to_string(d.frame) + at);
            check(d.state == r.state, "the state of the round differs" + at);
            for (int i = 0; i < 2; ++i) {
                const Player& p = r.player[i];
                ai::DuelPlayer& q = d.player[i];
                soft[i] += e[i].control.dropBonus;
                check(p.incoming.pending == q.incoming.pending && p.incoming.confirmed == q.incoming.confirmed,
                      "waiting garbage differs: " + std::to_string(p.incoming.pending) + "+" + std::to_string(p.incoming.confirmed) +
                          ", duel " + std::to_string(q.incoming.pending) + "+" + std::to_string(q.incoming.confirmed) + at);
                check(e[i].spawned == bool(appeared >> i & 1), "a pair appeared for one of the two only" + at);
                check(e[i].died == (q.phase == ai::DuelPhase::Dead) || (!e[i].died && p.phase == Phase::Dead), "death differs" + at);
                if (e[i].spawned) {
                    check(q.spawnFrame == played && q.nextPair == p.nextPair, "spawn frame or pair number differs" + at);
                    check(p.field == q.field, "fields differ" + at);
                    check(sameCore(p, q), "score " + std::to_string(p.scoring.score) + " carried " + std::to_string(p.scoring.pool) +
                                              ", duel " + std::to_string(q.scoring.score) + " carried " + std::to_string(q.scoring.pool) + at);
                    d.placeFast(i, input[i].target, p.colors);
                }
                if (e[i].locked) {
                    check(p.pair.x == input[i].target.x && p.pair.dir == input[i].target.dir, "fast input reached another placement" + at);
                    if (midAir[i]) {
                        d.place(i, input[i].target, p.colors, played, soft[i]);
                        midAir[i] = false;
                    }
                    check(q.lockFrame == played && q.moves == p.moves, "lock frame differs: duel " + std::to_string(q.lockFrame) + at);
                    ++moves;
                }
                if (e[i].waveStarted) ++waves, sent += p.wave.sent, offsets += p.wave.offsetted;
                drops += e[i].garbageFell;
            }
            sameFrameWaves += e[0].waveStarted && e[1].waveStarted;
            bothChains += r.player[0].phase == Phase::Wave && r.player[1].phase == Phase::Wave;
            if (r.state == RoundState::Playing && rng.below(150) == 0) {
                ai::observe(r, int(rng.below(2)), o);
                d = ai::Duel(o);
                for (int i = 0; i < 2; ++i) {
                    d.garbageRng[i] = r.garbageRng[i];
                    midAir[i] = r.player[i].phase == Phase::Control;
                    soft[i] = 0;
                    rebuiltMidAir += midAir[i];
                }
                ++rebuilt;
            }
        }
        if (stop) ++unfinished;
        else ++results[int(r.state)];
    }
    check(waves > 1000 && offsets > 0 && drops > 300 && bothChains > 0 && rebuiltMidAir > 100, "the duel test is not exercising the garbage");
    std::cout << "duel = frame game: " << games << " games of two, " << moves << " moves, " << waves << " waves, " << sent
              << " garbage sent, " << offsets << " offset, " << drops << " drops, " << bothChains << " frames with both chains running, "
              << sameFrameWaves << " frames with a wave of both, made again from an observation " << rebuilt << " times ("
              << rebuiltMidAir << " pairs in mid-air), wins " << results[1] << '/' << results[2] << ", draws " << results[3]
              << ", stopped on a high field " << unfinished << '\n';
}

// (c) The frame of the end of a chain (frame_tests.cpp testSameFrame): a move
//     that ends one frame before the opponent's chain is safe from it; in the
//     same frame, or later, the garbage falls. Whoever is player 0.
static void testDuelSameFrame() {
    for (int attacker = 0; attacker < 2; ++attacker)
        for (int lead = -1; lead <= 1; ++lead) {
            Round r;
            r.start(Rules{}, 1);
            Player& a = r.player[attacker];
            Player& p = r.player[1 - attacker];
            a.phase = Phase::Wave, a.wait = 10, a.chain.chains = 1;
            p.incoming.pending = 10;
            p.phase = Phase::Land, p.wait = 10 - lead;
            const Observation o = ai::observe(r, attacker);
            ai::Duel d(o);
            for (int i = 0; i < 2; ++i) d.garbageRng[i] = r.garbageRng[i];
            PlayerEvents e[2];
            const uint8_t none[2] = {0, 0};
            const std::string at = " (attacker " + std::to_string(attacker) + ", lead " + std::to_string(lead) + ")";
            int64_t spawn[2] = {-1, -1}, duelSpawn[2] = {-1, -1};
            for (int step = 0; step < 120; ++step) {
                r.step(none, e);
                const int appeared = d.run(r.frame);
                for (int i = 0; i < 2; ++i) {
                    if (e[i].spawned) spawn[i] = r.frame - 1;
                    if (appeared >> i & 1) duelSpawn[i] = d.frame - 1;
                }
                if (step != 10) continue;
                const ai::DuelPlayer& q = d.player[1 - attacker];
                const int fallen = q.field.f.garbage().count();
                check(p.field == q.field && p.incoming.confirmed == q.incoming.confirmed && !q.incoming.pending, "the duel differs from the round" + at);
                if (lead > 0) check(fallen == 0 && q.incoming.confirmed == 10, "garbage fell although the move ended first" + at);
                else check(fallen == 10 && q.incoming.confirmed == 0, "garbage did not fall" + at);
            }
            for (int i = 0; i < 2; ++i)
                check(spawn[i] >= 0 && spawn[i] == duelSpawn[i], "the next pair appears in another frame" + at);
        }
}

// (d) Two waves in the same frame: player 0 comes first. Both players clear
//     with an all clear bonus waiting; one of them has confirmed garbage above.
//     The garbage of a running chain is offset before confirmed garbage, so who
//     comes first decides what is left as confirmed (and falls next).
static void testDuelOrder() {
    int left[2][2];
    for (int loaded = 0; loaded < 2; ++loaded) {
        Round r;
        r.start(Rules{}, 1);
        for (int i = 0; i < 2; ++i) {
            Player& p = r.player[i];
            for (int y = 0; y < 4; ++y) {
                p.field.f.set(0, y, 1);
                if (i == loaded) p.field.f.set(1, y, 1); // a bigger group: more garbage
            }
            p.scoring.allClear = true;
            p.phase = Phase::Land, p.wait = 5;
        }
        r.player[loaded].incoming.confirmed = 20;
        const Observation o = ai::observe(r, 0);
        ai::Duel d(o);
        for (int i = 0; i < 2; ++i) d.garbageRng[i] = r.garbageRng[i];
        PlayerEvents e[2];
        const uint8_t none[2] = {0, 0};
        const std::string at = " (two waves in one frame, garbage above player " + std::to_string(loaded) + ")";
        int spawns = 0;
        for (int step = 0; step < 400 && spawns < 2; ++step) {
            r.step(none, e);
            const int appeared = d.run(r.frame);
            if (step == 4) {
                check(e[0].waveStarted && e[1].waveStarted, "the two waves are not in the same frame" + at);
                left[loaded][0] = d.player[loaded].incoming.pending, left[loaded][1] = d.player[loaded].incoming.confirmed;
            }
            for (int i = 0; i < 2; ++i) {
                const Player& p = r.player[i];
                const ai::DuelPlayer& q = d.player[i];
                check(p.incoming.pending == q.incoming.pending && p.incoming.confirmed == q.incoming.confirmed,
                      "waiting garbage differs: " + std::to_string(p.incoming.pending) + "+" + std::to_string(p.incoming.confirmed) +
                          ", duel " + std::to_string(q.incoming.pending) + "+" + std::to_string(q.incoming.confirmed) + at);
                check(e[i].spawned == bool(appeared >> i & 1), "a pair appeared for one of the two only" + at);
                if (e[i].spawned) check(sameCore(p, q), "the players differ when the pair appears" + at), ++spawns;
            }
        }
        check(spawns == 2, "the pairs did not appear" + at);
    }
    // The same position with the seats exchanged ends differently.
    check(left[0][0] != left[1][0] || left[0][1] != left[1][1], "the order inside a frame does not show in this test");
    std::cout << "two waves in one frame: garbage above player 0 leaves " << left[0][0] << " unconfirmed + " << left[0][1]
              << " confirmed, above player 1 " << left[1][0] << " + " << left[1][1] << '\n';
}

static std::string describe(const GameField& g, int id); // (below, with the tests of the buttons)

static void testTallSpawnInputCache() {
    GameField field;
    for (int y = 0; y < 12; ++y) field.f.set(1, y, GARBAGE);
    ai::SpawnInputCache cache;
    int reachable = 0, blocked = 0;
    auto compare = [&](const Timing& timing, int speed) {
        cache.configure(timing, speed);
        for (int id = 0; id < PLACEMENTS; ++id) {
            const auto direct = ai::tallSpawnInput(field, placement(id), timing, speed);
            const auto saved = cache.get(field, placement(id));
            check(saved.frames == direct.frames && saved.soft == direct.soft && saved.rested == direct.rested,
                  "cached tall-field input differs from the button path");
            if (direct.frames) ++reachable;
            else ++blocked;
        }
    };
    compare(TM, 100);
    for (int y = 0; y < 12; ++y) field.f.set(1, y, Cell(1 + y % 4));
    compare(TM, 100);
    field.f.set(1, 10, EMPTY);
    field.f.set(1, 12, GARBAGE);
    field.row14[1] = GARBAGE;
    compare(TM, 100);
    Timing slower = TM;
    slower.softDropPerFrame /= 2;
    compare(slower, 100);
    compare(slower, 150);
    field.row14[1] = EMPTY;
    compare(slower, 150);
    check(reachable > 0 && blocked > 0, "cache fixture needs reachable and unreachable placements");
}

// (e) Duel::tallPaths (Params::x3TallPaths). Fields with columns up to thirteen
//     high: player 0 plays the buttons InputPlanner finds for a random
//     placement, and the Duel puts the pair with placeFast. With tallPaths the
//     pair locks in the same frame, and frame, field and score are the same
//     when the next pair appears; without it the lock comes too early in the
//     cases counted (the Timing tables describe the straight way only).
static void testDuelTall() {
    Rng rng(20261010, 5);
    Rules rules;
    ai::InputPlanner planner;
    ai::InputPath path;
    int cases = 0, searched = 0, tableEarly = 0, tableLate = 0, most = 0;
    for (int i = 0; i < 4000; ++i) {
        Round r;
        r.start(rules, uint64_t(i) + 1);
        Player& p = r.player[0];
        for (int x = 0; x < W; ++x) addGarbage(p.field, x, x == DEATH_X ? int(rng.below(12)) : 6 + int(rng.below(8)));
        PlayerEvents e[2];
        const uint8_t none[2] = {0, 0};
        while (!aboutToSpawn(p)) r.step(none, e);
        const unsigned legal = legalPlacements(p.field);
        int ids[PLACEMENTS], n = 0;
        for (int id = 0; id < PLACEMENTS; ++id)
            if (legal >> id & 1) ids[n++] = id;
        if (!n) continue;
        const Placement target = placement(ids[rng.below(uint64_t(n))]);
        if (!planner.plan(p.field, spawnPair(TM), target, TM, rules.speedPercent, path)) continue; // (legalPlacements allows a few placements no buttons lead to)
        GameField closed = p.field;
        place(closed, target, r.tsumo.pair(0));
        if (closed.dead()) continue; // (the placement closes the field: no next pair to compare at)
        const std::string at = " (case " + std::to_string(i) + ", " + describe(p.field, placementId(target)) + ")";
        const Observation o = ai::observe(r, 0);
        ai::Duel d(o), plain(o);
        d.tallPaths = 1;
        for (int k = 0; k < 2; ++k) d.garbageRng[k] = plain.garbageRng[k] = r.garbageRng[k];
        int frames = 0;
        int64_t lockedAt = -1;
        bool again = false;
        while (!again && frames < 5000) {
            const uint8_t buttons[2] = {frames < path.frames() ? path.buttons[size_t(frames)] : uint8_t(0), 0};
            r.step(buttons, e);
            const int appeared = d.run(r.frame);
            const int appearedPlain = plain.run(r.frame);
            if (frames == 0) {
                check(e[0].spawned && (appeared & 1) && (appearedPlain & 1), "the pair did not appear" + at);
                d.placeFast(0, target, p.colors);
                plain.placeFast(0, target, p.colors);
            } else if (e[0].spawned) {
                again = true;
                check((appeared & 1) != 0, "the next pair appears in another frame" + at);
            } else {
                check(!(appeared & 1), "the next pair appears too early in the duel" + at);
            }
            if (e[0].locked) lockedAt = r.frame - 1;
            ++frames;
        }
        const ai::DuelPlayer& q = d.player[0];
        check(again && lockedAt >= 0, "the move did not end" + at);
        check(q.lockFrame == lockedAt, "lock in frame " + std::to_string(lockedAt) + ", duel " + std::to_string(q.lockFrame) + at);
        check(p.field == q.field && sameCore(p, q), "field or score differ when the next pair appears" + at);
        const int diff = int(lockedAt - plain.player[0].lockFrame);
        tableEarly += diff > 0, tableLate += diff < 0, most = std::max(most, diff);
        searched += path.states > 0;
        ++cases;
    }
    check(cases > 2000 && tableEarly > 200, "the test of tall fields is not exercising what the tables miss");
    std::cout << "duel on tall fields: " << cases << " moves lock in the frame of their buttons (" << searched << " ways searched); the tables alone are too early for "
              << tableEarly << " of them (by " << most << " frames at most), too late for " << tableLate << '\n';
}

// (f) With Rules::cancel (the rules of the records: game/timing.h recordedRules). Fields of garbage up to thirteen high,
//     a random placement, player 0 alone moving.
//     (A) The straight way of InputPlanner, the Duel with tallPaths: the lock and the next pair in the frames of the
//         Round - the landing waited for after the lock shorter by restedFrames, as game/player.h has it.
//     (B) The way over soonest (InputPlanner::fastest), the Duel told so as of the other seat (Observation::fastestRead
//         FASTEST_BOUND) and putting the pair by the tables: its next pair never later than the Round's; without
//         fastestRead, never sooner.
//     (C) The same way, the Duel told so as of the own seat (FASTEST_OWN) and putting the pair by the buttons
//         (tallPaths): its next pair never sooner than the Round's - the own seat is never read faster than it plays.
static void testDuelCancel() {
    Rng rng(20261124, 7);
    Rules rules;
    rules.cancel = true;
    ai::InputPlanner straight, soonest;
    soonest.fastest = true;
    ai::InputPath path;
    int cases = 0, faster = 0, plainLate = 0, earlyMost = 0, early = 0, tablesOff = 0, ownSame = 0, ownLate = 0, ownCases = 0;
    for (int i = 0; i < 3000; ++i) {
        for (int kind = 0; kind < 3; ++kind) {
            Round r;
            r.start(rules, uint64_t(i) * 3 + uint64_t(kind) + 1);
            const Timing& t = r.timing;
            Player& p = r.player[0];
            for (int x = 0; x < W; ++x) addGarbage(p.field, x, x == DEATH_X ? int(rng.below(10)) : 1 + int(rng.below(11)));
            PlayerEvents e[2];
            const uint8_t none[2] = {0, 0};
            while (!aboutToSpawn(p)) r.step(none, e);
            const unsigned legal = legalPlacements(p.field);
            int ids[PLACEMENTS], n = 0;
            for (int id = 0; id < PLACEMENTS; ++id)
                if (legal >> id & 1) ids[n++] = id;
            if (!n) continue;
            const Placement target = placement(ids[rng.below(uint64_t(n))]);
            ai::InputPlanner& planner = kind ? soonest : straight;
            if (!(kind ? planner.planSure(p.field, spawnPair(t), target, t, rules.speedPercent, path)
                       : planner.plan(p.field, spawnPair(t), target, t, rules.speedPercent, path))) continue;
            GameField closed = p.field;
            place(closed, target, r.tsumo.pair(0));
            if (closed.dead()) continue; // (no next pair; a piece left to fall is counted as the Round counts it)
            const std::string at = " (case " + std::to_string(i) + (kind == 2 ? ", soonest way, own seat" : kind ? ", soonest way" : ", straight way") + ", " + describe(p.field, placementId(target)) + ")";
            Observation o = ai::observe(r, 0);
            const GameField start = p.field;
            check(o.rules.cancel && o.timing == t, "the observation does not carry the rules of the records" + at);
            Observation told = o;
            told.fastestRead[0] = kind == 1 ? Observation::FASTEST_BOUND : kind == 2 ? Observation::FASTEST_OWN : 0;
            ai::Duel d(told), plain(o);
            d.tallPaths = kind == 1 ? 0 : 1;
            for (int k = 0; k < 2; ++k) d.garbageRng[k] = plain.garbageRng[k] = r.garbageRng[k];
            int frames = 0;
            int64_t lockedAt = -1, roundNext = -1, duelNext = -1, plainNext = -1;
            while (roundNext < 0 && frames < 5000) {
                const uint8_t buttons[2] = {frames < path.frames() ? path.buttons[size_t(frames)] : uint8_t(0), 0};
                r.step(buttons, e);
                const int appeared = duelNext < 0 ? d.run(r.frame) : 0;
                const int appearedPlain = plainNext < 0 ? plain.run(r.frame) : 0;
                if (frames == 0) {
                    check(e[0].spawned && (appeared & 1) && (appearedPlain & 1), "the pair did not appear" + at);
                    d.placeFast(0, target, p.colors);
                    plain.placeFast(0, target, p.colors);
                } else {
                    if (e[0].spawned) roundNext = r.frame - 1;
                    if (appeared & 1) duelNext = r.frame - 1;
                    if (appearedPlain & 1) plainNext = r.frame - 1;
                }
                if (e[0].locked) lockedAt = r.frame - 1;
                ++frames;
            }
            check(roundNext >= 0 && lockedAt >= 0, "the move did not end" + at);
            if (duelNext < 0) d.run(roundNext + 400), duelNext = d.player[0].spawnFrame;
            if (plainNext < 0) plain.run(roundNext + 400), plainNext = plain.player[0].spawnFrame;
            if (kind == 0) {
                check(d.player[0].lockFrame == lockedAt, "lock in frame " + std::to_string(lockedAt) + ", duel " + std::to_string(d.player[0].lockFrame) + at);
                check(duelNext == roundNext, "next pair in frame " + std::to_string(roundNext) + ", duel " + std::to_string(duelNext) + at);
                tablesOff += plainNext != roundNext;
            } else if (kind == 2) {
                check(duelNext >= roundNext, "the duel told of the own seat has the next pair sooner than it is played: frame " +
                                                 std::to_string(roundNext) + ", duel " + std::to_string(duelNext) + at);
                ownSame += duelNext == roundNext, ownLate += duelNext > roundNext, ++ownCases;
            } else {
                if (duelNext > roundNext) {
                    ai::FastestSearch bound;
                    const int estimate = bound.estimate(start, spawnPair(t), target, t, rules.speedPercent);
                    const int ends = ai::FastestSearch::ends(start, spawnPair(t), path.buttons, t, rules.speedPercent);
                    std::cout << "  (lock " << lockedAt << ", buttons " << path.frames() << ", ends " << ends << ", estimate " << estimate
                              << ", duel lock " << d.player[0].lockFrame << ", plain next " << plainNext << ")" << std::endl;
                    PairControl q = spawnPair(t);
                    for (int f = 0; f < path.frames(); ++f) {
                        const int left = ai::FastestSearch::ends(start, q, std::vector<uint8_t>(path.buttons.begin() + f, path.buttons.end()), t, rules.speedPercent);
                        std::cout << "   f" << f << " btn " << int(path.buttons[size_t(f)]) << " x" << int(q.x) << " y" << int(q.y) << " dir" << int(q.dir)
                                  << " up" << int(q.upper) << " gnd" << int(q.grounded) << " gf" << int(q.groundFrames) << " own" << int(q.owned)
                                  << " of" << int(q.ownFrames) << " qf" << int(q.quickFast) << " tw" << int(q.turnWait) << " gain" << int(q.gain)
                                  << " | left " << left << " est " << bound.estimate(start, q, target, t, rules.speedPercent) << std::endl;
                        stepPair(q, start, path.buttons[size_t(f)], t, rules.speedPercent);
                    }
                    std::cout << "   h: " << describe(start, placementId(target)) << std::endl;
                }
                check(duelNext <= roundNext, "the duel told of the soonest way has the next pair later: frame " + std::to_string(roundNext) +
                                                 ", duel " + std::to_string(duelNext) + at);
                check(plainNext >= duelNext, "the duel not told is sooner than the one told" + at);
                faster += path.states > 0 && duelNext < plainNext;
                plainLate += plainNext > roundNext;
                const int gap = int(roundNext - duelNext);
                early += gap > 0, earlyMost = std::max(earlyMost, gap);
            }
            ++cases;
        }
    }
    check(cases > 4500 && ownCases > 1500, "the test with the cancel has too few cases");
    std::cout << "duel with the cancel: " << cases << " moves; the straight way in the frames of the round (the tables alone off for "
              << tablesOff << "); the soonest way: the duel not told is late for " << plainLate << ", told sooner than it for "
              << faster << ", told sooner than the round for " << early << " (by " << earlyMost << " frames at most); the own seat: in the frame of the round "
              << ownSame << " of " << ownCases << ", later " << ownLate << '\n';
}

// (f2) The rival read with FASTEST_BOUND (Rules::cancel). Player 0, the rival, on garbage up to eleven high, puts his
//      first pair upright on three pieces of its axis's colour (it fires) or on garbage (it does not). The truth is the
//      Round, his pair moved with the buttons of the straight way: the frame of its first wave up to his next pair, or none.
//      - ai::rivalSeenCheck gives that frame when the straight way fires, never sooner than the bound, and -1 when it fires
//        nothing;
//      - lv38_threat::reactsInTime and X3Agent::pressureReplySeen count on seeing the chain only from that frame on, with
//        the lead of the choice (ai::seenBeforeChoice: PlacementAgent::ahead, Params::seeLate).
static void testRivalSeenCheck() {
    using ai::lv38_threat::reactsInTime;
    check(!reactsInTime(102, 100, 105) && reactsInTime(106, 100, 105) && reactsInTime(102, 100, 100) &&
          reactsInTime(102, -1, -1) && !reactsInTime(102, 100, -1) && !reactsInTime(100, 100, 100) &&
          !reactsInTime(102, 100, 100, 3, 0) && !reactsInTime(102, 100, 100, 1, 2) && reactsInTime(103, 100, 100, 3, 0),
          "a second pair reacts to the rival's chain only after the straight way has shown it to its choice");
    check(ai::seenBeforeChoice(100, 101, 1, 0) && !ai::seenBeforeChoice(100, 100, 1, 0) && !ai::seenBeforeChoice(100, 101, 2, 0) &&
              !ai::seenBeforeChoice(100, 101, 1, 1) && ai::seenBeforeChoice(100, 104, 3, 1),
          "a choice begun before its pair appears, or seeing late, sees only what showed before it");
    struct Probe : ai::X3Agent {
        using X3Agent::X3Agent;
        using X3Agent::pressureReplyRevealed;
        using X3Agent::pressureReplySeen;
        void hold(const ai::PlanSet& own, const ai::PlanSet& rival) { mine = own, theirs = rival; }
        int64_t lead() const { return std::max(0, ahead - 1) + std::max(0, p.seeLate); }
    };
    Rng rng(20261124, 11);
    Rules rules;
    rules.cancel = true;
    const ai::Params p;
    int firing = 0, quiet = 0, later = 0;
    for (int i = 0; i < 3000; ++i) {
        const bool fires = i % 2 == 0;
        Round r;
        r.start(rules, uint64_t(i) + 1);
        Player& rival = r.player[0];
        const Pair pair = r.tsumo.pair(0);
        const int column = int(rng.below(W));
        for (int x = 0; x < W; ++x)
            addGarbage(rival.field, x, x == column ? int(rng.below(7)) : x == DEATH_X ? int(rng.below(10)) : 1 + int(rng.below(11)));
        int h[W];
        rival.field.heights(h);
        if (fires)
            for (int k = 0; k < 3; ++k) rival.field.f.set(column, h[column] + k, Cell(pair.axis));
        PlayerEvents e[2];
        const uint8_t none[2] = {0, 0};
        while (!aboutToSpawn(rival)) r.step(none, e);
        const int target = placementId(Placement{uint8_t(column), UP});
        if (!(legalPlacements(rival.field) >> target & 1)) continue;
        const ai::Observation o = ai::observe(r, 1);
        ai::Observation bound = o;
        bound.fastestRead[0] = ai::Observation::FASTEST_BOUND;
        ai::PlanSet own, theirs;
        own.who = 1;
        theirs.who = 0;
        theirs.pair[0] = pair;
        theirs.pairCount = theirs.knownCount = 1;
        ai::Plan move;
        move.id[move.count++] = int8_t(target);
        int64_t soonest = -1, plain = -1;
        const ai::Duel exchange = ai::playOut(bound, own, nullptr, theirs, &move, p, 7, true, &soonest);
        ai::playOut(o, own, nullptr, theirs, &move, p, 7, true, &plain);
        if (soonest < 0 || plain < 0) continue;
        // The truth: the Round, the rival's pair moved with the straight way's buttons, up to his next pair.
        ai::InputPlanner planner;
        ai::InputPath path;
        if (!planner.plan(rival.field, spawnPair(r.timing), placement(target), r.timing, rules.speedPercent, path)) continue;
        const std::string at = " (case " + std::to_string(i) + ", " + describe(rival.field, target) + ")";
        int64_t wave = -1;
        for (int frame = 0; frame < path.frames() + 200 && wave < 0; ++frame) {
            const uint8_t buttons[2] = {frame < path.frames() ? path.buttons[size_t(frame)] : uint8_t(0), 0};
            r.step(buttons, e);
            if (e[0].waveStarted) wave = r.frame - 1;
            if (frame > 0 && e[0].spawned) break;
        }
        check((wave >= 0) == fires, "the round fires as the field was built" + at);
        const int64_t seen = ai::rivalSeenCheck(bound, own, nullptr, theirs, &move, p, 7, soonest);
        check(ai::rivalSeenCheck(o, own, nullptr, theirs, &move, p, 7, plain) == plain,
              "without FASTEST_BOUND the frame read is the frame seen" + at);
        if (!fires) {
            ++quiet;
            check(seen == -1, "a placement the straight way fires nothing with is never seen: " + std::to_string(seen) + at);
            continue;
        }
        ++firing;
        check(plain == wave, "the straight reading checks in the frame of the round's first wave: " + std::to_string(plain) +
                                 " / " + std::to_string(wave) + at);
        check(soonest <= wave && seen == wave, "the bound is no later and the frame seen is the first wave: " + std::to_string(soonest) +
                                                   " " + std::to_string(seen) + " / " + std::to_string(wave) + at);
        if (wave <= soonest) continue;
        ++later;
        // The same placement as a reply that fires: the own next choice right after the bound's check, then after the wave.
        Probe probe(p);
        probe.hold(own, theirs);
        ai::Plan reply = move, stay;
        reply.fireDepth = 1;
        check(exchange.player[0].chain.chains > 0, "the reading with the bound fires as well" + at);
        ai::Duel next = exchange;
        next.player[1].spawnFrame = soonest + 1 + probe.lead();
        check(probe.pressureReplyRevealed(bound, next, reply, exchange, soonest) &&
                  !probe.pressureReplySeen(bound, stay, next, reply, exchange, soonest, 7),
              "a reply seen at the bound only is not taken as seen before the next choice" + at);
        next.player[1].spawnFrame = wave + 1 + probe.lead();
        check(probe.pressureReplySeen(bound, stay, next, reply, exchange, soonest, 7),
              "a reply whose first wave comes before the next choice is seen" + at);
    }
    check(firing > 0 && quiet > 0 && later > 0, "too few cases: " + std::to_string(firing) + " firing, " + std::to_string(quiet) +
                                                    " not, " + std::to_string(later) + " with the bound sooner");
}

// (g) The own seat read as it plays the way over soonest (Observation::FASTEST_OWN), with Rules::cancel, player 0 alone
//     moving the buttons InputPlanner::planSure gives with fastest:
//     - the frames FastestSearch::find says a way ends in are those FastestSearch::ends counts for its buttons;
//     - the Duel's next pair never before the Round's, for placements that fire a chain (three pieces of the axis's
//       colour under it), for the pair seen in mid-air (the Duel made from an observation some frames after it appeared,
//       its rest of the way searched from there), and on fields with columns up to thirteen high;
//     - with the states of the seat's own search within a frame given (Observation::fastestReadStates), never more found.
static void testDuelOwnSeat() {
    Rng rng(20261124, 9);
    Rules rules;
    rules.cancel = true;
    ai::InputPlanner soonest;
    soonest.fastest = true;
    ai::InputPath path;
    int cases = 0, fired = 0, inAir = 0, tall = 0, same = 0, later = 0, found = 0, few = 0, fewer = 0;
    for (int i = 0; i < 4000; ++i) {
        const int kind = i % 4; // 0: at the spawn, 1: a chain fired, 2: seen in mid-air, 3: the seat's own states few
        Round r;
        r.start(rules, uint64_t(i) + 1);
        const Timing& t = r.timing;
        Player& p = r.player[0];
        const bool high = i % 8 >= 4;
        for (int x = 0; x < W; ++x)
            addGarbage(p.field, x, x == DEATH_X ? int(rng.below(9)) : int(rng.below(high ? 13 : 9)));
        PlayerEvents e[2];
        const uint8_t none[2] = {0, 0};
        while (!aboutToSpawn(p)) r.step(none, e);
        const unsigned legal = legalPlacements(p.field);
        int ids[PLACEMENTS], n = 0;
        for (int id = 0; id < PLACEMENTS; ++id)
            if (legal >> id & 1) ids[n++] = id;
        if (!n) continue;
        const Placement target = placement(ids[rng.below(uint64_t(n))]);
        const Pair colors = r.tsumo.pair(0);
        if (kind == 1) {
            // (three pieces of the axis's colour on the axis's column: the pair clears them with its axis)
            int h[W];
            p.field.heights(h);
            if (h[target.x] + 3 > 10 || colors.axis == colors.child) continue;
            for (int k = 0; k < 3; ++k) p.field.f.set(target.x, h[target.x] + k, colors.axis);
            if (!(legalPlacements(p.field) >> placementId(target) & 1)) continue;
        }
        if (!soonest.planSure(p.field, spawnPair(t), target, t, rules.speedPercent, path)) continue;
        GameField closed = p.field;
        place(closed, target, colors);
        if (closed.dead()) continue; // (a piece left to fall included)
        const std::string at = " (case " + std::to_string(i) + ", kind " + std::to_string(kind) + ", " + describe(p.field, placementId(target)) + ")";
        {
            // (the frames a way found ends in, as FastestSearch counts them for its buttons)
            ai::FastestSearch search;
            std::vector<uint8_t> buttons;
            int soft = 0, end = 0;
            if (search.find(p.field, spawnPair(t), target, t, rules.speedPercent, 1000, buttons, soft, end)) {
                check(end == ai::FastestSearch::ends(p.field, spawnPair(t), buttons, t, rules.speedPercent),
                      "a way found ends in other frames than its buttons" + at);
                ++found;
            }
        }
        // Seen in mid-air: some frames of the way played first.
        const int ahead = kind == 2 ? 1 + int(rng.below(uint64_t(std::max(1, std::min(6, path.frames() - 1))))) : 0;
        int frames = 0;
        if (ahead) {
            for (; frames < ahead; ++frames) {
                const uint8_t buttons[2] = {path.buttons[size_t(frames)], 0};
                r.step(buttons, e);
                if (e[0].locked) break;
            }
            if (p.phase != Phase::Control || p.pair.locked) continue;
        }
        Observation o = ai::observe(r, 0);
        o.fastestRead[0] = Observation::FASTEST_OWN;
        if (kind == 3) o.fastestReadStates[0] = 20;
        if (ahead) {
            // (the pair goes on with its buttons, as PlacementAgent::noteWay tells the reading)
            const std::vector<uint8_t> rest(path.buttons.begin() + frames, path.buttons.end());
            const int end = ai::FastestSearch::ends(p.field, p.pair, rest, t, rules.speedPercent);
            if (end > 0) {
                o.ownWayTarget = placementId(target), o.ownWayCheck = o.frame + end - 1, o.ownWayLock = o.frame + int64_t(rest.size()) - 1;
                PairControl c = p.pair;
                for (const uint8_t b : rest) o.ownWaySoft += stepPair(c, p.field, b, t, rules.speedPercent).dropBonus;
            }
        }
        ai::Duel d(o);
        d.tallPaths = 1;
        for (int k = 0; k < 2; ++k) d.garbageRng[k] = r.garbageRng[k];
        if (ahead) {
            // (the rest of the straight way from where the pair is: the lock the Duel is given, as Gaze has it)
            ai::InputPlanner straight;
            ai::InputPath rest;
            if (!straight.plan(p.field, p.pair, target, t, rules.speedPercent, rest)) continue;
            PairControl q = p.pair;
            for (const uint8_t b : rest.buttons) stepPair(q, p.field, b, t, rules.speedPercent);
            d.place(0, target, colors, o.frame + rest.frames() - 1, rest.softDropRows, restedFrames(q, t));
        }
        int64_t roundNext = -1, duelNext = -1;
        for (int step = 0; roundNext < 0 && step < 5000; ++step, ++frames) {
            const uint8_t buttons[2] = {frames < path.frames() ? path.buttons[size_t(frames)] : uint8_t(0), 0};
            r.step(buttons, e);
            const int appeared = duelNext < 0 ? d.run(r.frame) : 0;
            if (!ahead && step == 0) {
                check(e[0].spawned && (appeared & 1), "the pair did not appear" + at);
                d.placeFast(0, target, colors);
            } else {
                if (e[0].spawned) roundNext = r.frame - 1;
                if (appeared & 1) duelNext = r.frame - 1;
            }
        }
        check(roundNext >= 0, "the move did not end" + at);
        if (duelNext < 0) d.run(roundNext + 400), duelNext = d.player[0].spawnFrame;
        check(duelNext >= roundNext, "the own seat is read sooner than it plays: frame " + std::to_string(roundNext) + ", duel " +
                                         std::to_string(duelNext) + at);
        same += duelNext == roundNext, later += duelNext > roundNext;
        if (kind == 3) ++few, fewer += duelNext > roundNext; // (with the seat's few states: later more often)
        fired += kind == 1 && r.player[0].chain.chains > 0, inAir += kind == 2, tall += high;
        ++cases;
    }
    check(cases > 2000 && inAir > 300 && found > 1000, "the test of the own seat has too few cases");
    std::cout << "duel of the own seat with the cancel: " << cases << " moves (" << inAir << " seen in mid-air, " << tall
              << " on tall fields, chains begun " << fired << "); in the frame of the round " << same << ", later " << later
              << "; with few states later " << fewer << " of " << few << "; " << found << " ways found end as their buttons do\n";
}

// ---- 3. from a placement to buttons -----------------------------------------------------
// Plays the buttons of a path on a copy of the pair: true when the pair locks
// in the last frame, at the target, with the soft drop the path says.
static bool replays(const GameField& g, PairControl p, Placement target, const ai::InputPath& path) {
    int soft = 0;
    for (int k = 0; k < path.frames(); ++k) {
        if (p.locked) return false;
        soft += stepPair(p, g, path.buttons[size_t(k)], TM).dropBonus;
    }
    return p.locked && p.x == target.x && p.dir == target.dir && soft == path.softDropRows;
}

static std::string describe(const GameField& g, int id) {
    int h[W];
    g.heights(h);
    std::string s = "heights";
    for (int x = 0; x < W; ++x) s += ' ' + std::to_string(h[x]) + (g.row14[size_t(x)] ? "*" : "");
    const Placement p = placement(id);
    return s + ", axis column " + std::to_string(p.x + 1) + " child " + "URDL"[p.dir];
}

// (a) The fields and targets of frame_tests.cpp testPlacementTimes (columns up
//     to 9 high): the path is the straight one, and played in a Round the move
//     takes placeFrames + splitFrames.
static void testInputStraight() {
    Rng rng(20261006, 1);
    Rules rules;
    ai::InputPlanner planner;
    ai::InputPath path;
    int cases = 0;
    for (int i = 0; i < 4000; ++i) {
        Round r;
        r.start(rules, uint64_t(i) + 1, 1);
        int h[W];
        const int flat = int(rng.below(10));
        for (int x = 0; x < W; ++x) {
            h[x] = i % 2 ? int(rng.below(10)) : flat;
            addGarbage(r.player[0].field, x, h[x]);
        }
        PlayerEvents e[2];
        const uint8_t none[2] = {0, 0};
        Player& p = r.player[0];
        while (!aboutToSpawn(p)) r.step(none, e);
        const Placement target = placement(int(rng.below(PLACEMENTS)));
        const std::string at = " (case " + std::to_string(i) + ", " + describe(p.field, placementId(target)) + ")";
        check(planner.plan(p.field, spawnPair(TM), target, TM, rules.speedPercent, path), "no path" + at);
        check(path.states == 0, "the straight way was not taken" + at);
        GameField expected = p.field;
        const PlaceResult placed = place(expected, target, r.tsumo.pair(0));
        int frames = 0, soft = 0, lockedAt = -1;
        do {
            const uint8_t buttons[2] = {frames < path.frames() ? path.buttons[size_t(frames)] : uint8_t(0), 0};
            r.step(buttons, e);
            soft += e[0].control.dropBonus;
            if (e[0].locked) lockedAt = frames;
            ++frames;
        } while (!aboutToSpawn(p) && frames < 5000);
        check(lockedAt == path.frames() - 1, "the pair did not lock in the last frame of the path" + at);
        check(p.field == expected, "the path put the pair elsewhere" + at);
        const int want = TM.placeFrames(target.x, placed.lockY) + TM.splitFrames(placed.split, target.x, placed.lockY);
        check(frames == want, std::to_string(frames) + " frames, table " + std::to_string(want) + at);
        // (a point for every row, and those of landing with the down key held)
        check(soft == path.softDropRows && soft == SPAWN_Y - placed.lockY + TM.dropLockPoints + TM.dropLandPoints, "soft drop points differ" + at);
        ++cases;
    }
    std::cout << "input, straight: " << cases << " moves take the time of the tables\n";
}

// (b) High fields (made as in frame_tests.cpp testReach): every placement of
//     legalPlacements gets a path, and stepPair locks the pair there.
// (c) From a random frame of such a path: again to the same placement, and to
//     another one (which may be out of reach by then).
static void testInputHigh(int fields) {
    using Clock = std::chrono::steady_clock;
    Rng rng(20261006, 3);
    ai::InputPlanner planner;
    ai::InputPath path, again;
    int64_t plans = 0, straight = 0, missing = 0, notLegal = 0, ns = 0, searchNs = 0, searches = 0, statesSum = 0, limit = 0;
    long midSame = 0, midSameLonger = 0, midSameShorter = 0, midOther = 0, midOtherFound = 0;
    int64_t worstNs = 0, noPath = 0, noPathNs = 0, noPathWorst = 0;
    int worstStates = 0, longest = 0;
    std::string firstMissing, worstCase;
    for (int i = 0; i < fields; ++i) {
        GameField g;
        for (int x = 0; x < W; ++x) {
            const int h = rng.below(4) ? 9 + int(rng.below(5)) : int(rng.below(14));
            addGarbage(g, x, h);
            if (h == H && rng.below(2)) g.row14[size_t(x)] = GARBAGE;
        }
        if (g.dead()) continue;
        const unsigned legal = legalPlacements(g);
        const PairControl start = spawnPair(TM);
        for (int id = 0; id < PLACEMENTS; ++id) {
            const Placement target = placement(id);
            const auto t0 = Clock::now();
            const bool found = planner.plan(g, start, target, TM, 100, path);
            const int64_t t = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
            if (!(legal >> id & 1)) {
                notLegal += found;
                if (!found) ++noPath, noPathNs += t, noPathWorst = std::max(noPathWorst, t);
                continue;
            }
            ++plans, ns += t;
            if (path.states) ++searches, searchNs += t, statesSum += path.states;
            else ++straight;
            if (t > worstNs) worstNs = t, worstCase = describe(g, id);
            worstStates = std::max(worstStates, path.states);
            limit += path.states >= planner.maxStates;
            if (!found) {
                if (!missing++) firstMissing = describe(g, id);
                continue;
            }
            longest = std::max(longest, path.frames());
            check(replays(g, start, target, path), "the path does not lock the pair at the target: " + describe(g, id));
            // (c)
            if (path.frames() < 2 || rng.below(4)) continue;
            PairControl p = start;
            const int k = 1 + int(rng.below(uint64_t(path.frames() - 1)));
            for (int f = 0; f < k; ++f) stepPair(p, g, path.buttons[size_t(f)], TM);
            ++midSame;
            check(planner.plan(g, p, target, TM, 100, again), "no path from the middle of a path: " + describe(g, id));
            check(replays(g, p, target, again), "the path from the middle does not lock at the target: " + describe(g, id));
            midSameLonger += k + again.frames() > path.frames(), midSameShorter += k + again.frames() < path.frames();
            const Placement other = placement(int(rng.below(PLACEMENTS)));
            ++midOther;
            if (planner.plan(g, p, other, TM, 100, again)) {
                ++midOtherFound;
                check(replays(g, p, other, again), "the path to another placement does not lock there: " + describe(g, placementId(other)));
            }
        }
    }
    std::cout << "input, high fields: " << fields << " fields, " << plans << " legal placements, " << missing << " without a path, "
              << straight << " straight, " << searches << " searched (" << (searches ? statesSum / searches : 0) << " states on average, at most "
              << worstStates << ", " << limit << " at the limit), longest path " << longest << " frames, " << notLegal
              << " paths to placements that are not legal\n";
    std::cout << "  time: " << (plans ? ns / plans : 0) << " ns on average, searches " << (searches ? searchNs / searches : 0)
              << " ns on average, at most " << worstNs << " ns (" << worstCase << "); " << noPath
              << " placements that are not legal: no path after " << (noPath ? noPathNs / noPath : 0) << " ns on average, at most "
              << noPathWorst << " ns\n";
    std::cout << "  from the middle: " << midSame << " to the same placement (" << midSameShorter << " earlier, " << midSameLonger
              << " later than the first path), " << midOther << " to another placement, " << midOtherFound << " found\n";
    if (missing) std::cout << "  without a path, e.g.: " << firstMissing << '\n';
    check(!missing, "a placement offered to the AI has no path");
}

// ---- 4. trial drops and parameters ------------------------------------------------------
static const ai::Trial* findTrial(const ai::Trials& t, int x, Cell color) {
    for (int i = 0; i < t.count; ++i)
        if (t.item[i].x == x && t.item[i].color == color) return &t.item[i];
    return nullptr;
}

// The same by the game's own chain (chainWave) on a field with the pieces added.
static bool trialByGame(const GameField& g, const Rules& rules, int x, Cell color, int n, int& chains, int& points, BitField& after) {
    int h[W];
    g.heights(h);
    if (h[x] + n > 12) return false;
    PlayerCore me;
    me.field = g;
    for (int i = 0; i < n; ++i) me.field.f.set(x, h[x] + i, color);
    Incoming sink;
    ChainState chain;
    WaveReport w;
    points = 0;
    while (chainWave(me, sink, rules, rules.targetPoints, chain, w)) points += w.points;
    chains = chain.chains;
    after = me.field.f;
    return chains > 0;
}

static void testDetect() {
    const Cell R = 1, G = 2;
    Rules rules;
    ai::Trials t;
    // Three reds in column 1; three greens with a red on top in column 2.
    //   a green in column 3: the greens clear, the red falls next to the reds: 2 chains, 40 + 320.
    //   a red on column 1: it also touches the red on the greens, five clear (10 x 5 x 2).
    //   three reds on column 2: 1 chain. Nothing else.
    GameField g;
    for (int y = 0; y < 3; ++y) g.f.set(0, y, R), g.f.set(1, y, G);
    g.f.set(1, 3, R);
    check(ai::detect(g, rules, TM, 3, t) == 3, "trial drops found: " + std::to_string(t.count) + ", expected 3");
    const ai::Trial* a = findTrial(t, 2, G);
    check(a && a->added == 1 && a->y == 0 && a->chains == 2 && a->points == 360 && a->cleared == 8, "the 2-chain is not found as it is");
    check(a->frames == TM.waveFrames(3) + TM.waveFrames(0), "frames of the 2-chain: " + std::to_string(a->frames));
    const ai::Trial* b = findTrial(t, 0, R);
    check(b && b->added == 1 && b->y == 3 && b->chains == 1 && b->points == 100 && b->cleared == 5, "a red on column 1");
    const ai::Trial* c = findTrial(t, 1, R);
    check(c && c->added == 3 && c->y == 4 && c->chains == 1 && c->points == 40, "three reds on column 2");
    check(t.best() == a, "the best trial is not the 2-chain");
    check(ai::detect(g, rules, TM, 2, t) == 2 && !findTrial(t, 1, R), "with two pieces at most, three are still added");

    // Other clear counts.
    rules.clearCount = 5; // one red on column 1 still makes five; the greens need two; on column 2 four reds would be needed
    ai::detect(g, rules, TM, 3, t);
    b = findTrial(t, 0, R);
    check(b && b->added == 1 && b->chains == 1 && b->cleared == 5 && !findTrial(t, 1, R), "clear count 5");
    a = findTrial(t, 2, G);
    check(t.count == 2 && a && a->added == 2 && a->chains == 1 && a->cleared == 5, "clear count 5, the greens");
    rules.clearCount = 2; // the field itself would clear: use a single piece
    GameField one;
    one.f.set(0, 0, R);
    // A red on or next to it clears with one; anywhere else two pieces of any colour clear by themselves.
    check(ai::detect(one, rules, TM, 3, t) == W * rules.colors, "clear count 2: " + std::to_string(t.count) + " trials");
    check(findTrial(t, 0, R)->added == 1 && findTrial(t, 1, R)->added == 1 && findTrial(t, 0, R)->cleared == 2 && findTrial(t, 2, R)->added == 2 &&
              findTrial(t, 0, G)->added == 2 && findTrial(t, 0, G)->cleared == 2,
          "clear count 2");
    rules = Rules{};

    // The top: nothing is added above the twelfth row.
    GameField high;
    addGarbage(high, 0, 12);
    for (int y = 0; y < 11; ++y) high.f.set(1, y, y < 8 ? GARBAGE : R);
    ai::detect(high, rules, TM, 3, t);
    check(t.count == 1 && t.item[0].x == 1 && t.item[0].color == R && t.item[0].added == 1 && t.item[0].y == 11, "the twelfth row");

    // Random fields: every column, colour and number against the game's own chain.
    Rng rng(20261006, 30);
    long fields = 0, found = 0, chains = 0;
    BitField after[W * MAX_COLORS];
    for (int i = 0; i < 3000; ++i) {
        Rules r;
        if (i % 3 == 1) r.colors = 3 + int(rng.below(3));
        if (i % 4 == 2) r.clearCount = 2 + int(rng.below(5));
        GameField f;
        const int moves = 5 + int(rng.below(30));
        for (int m = 0; m < moves && !f.dead(); ++m) {
            const unsigned legal = legalPlacements(f);
            if (!legal) break;
            int id;
            do id = int(rng.below(PLACEMENTS));
            while (!(legal >> id & 1));
            place(f, placement(id), Pair{r.palette[rng.below(uint64_t(r.colors))], r.palette[rng.below(uint64_t(r.colors))]});
            Wave w;
            while (findWaveFor(f.f, w, r.clearCount)) f.f.erase(w.cleared | w.garbage), f.f.drop();
            if (rng.below(8) == 0) addGarbage(f, int(rng.below(W)), 1 + int(rng.below(3)));
        }
        const int maxAdd = 1 + int(rng.below(3));
        ai::detect(f, r, TM, maxAdd, t, after);
        ++fields, found += t.count;
        for (int x = 0; x < W; ++x)
            for (int k = 0; k < r.colors; ++k) {
                const Cell color = r.palette[size_t(k)];
                const ai::Trial* got = findTrial(t, x, color);
                int n = 1, wantChains = 0, wantPoints = 0;
                BitField wantAfter;
                while (n <= maxAdd && !trialByGame(f, r, x, color, n, wantChains, wantPoints, wantAfter)) ++n;
                const std::string at = " (field " + std::to_string(i) + ", column " + std::to_string(x + 1) + ", colour " + std::to_string(color) + ")";
                if (n > maxAdd) {
                    check(!got, "a trial drop that the game does not confirm" + at);
                    continue;
                }
                check(got && got->added == n && got->chains == wantChains && got->points == wantPoints,
                      "trial drop differs from the game: " + std::to_string(wantChains) + " chains, " + std::to_string(wantPoints) + " points" + at);
                check(after[got - t.item] == wantAfter, "the field after the trial differs" + at);
                chains += wantChains;
            }
    }
    std::cout << "trial drops: " << fields << " random fields, " << found << " trials equal to the game's chain (" << chains << " waves)\n";

    // Parameters: written and read back; one value replaced; a wrong line is refused.
    check(sizeof(ai::Params) == sizeof(int) * std::size(ai::PARAM_ITEMS), "a member of Params has no line in PARAM_ITEMS");
    ai::Params p, q;
    p.trialMaxAdd = 2;
    std::stringstream text;
    ai::saveParams(text, p);
    std::string error;
    check(ai::loadParams(text, q, error) && q.trialMaxAdd == 2, "parameters are not read back");
    std::istringstream some("# comment\n\ntrialMaxAdd=1\n"), unknown("trialMaxAd=1\n"), bad("trialMaxAdd=1x\n");
    check(ai::loadParams(some, q, error) && q.trialMaxAdd == 1, "a parameter file with one line");
    check(!ai::loadParams(unknown, q, error) && error.find("line 1") == 0 && !ai::loadParams(bad, q, error) && q.trialMaxAdd == 1,
          "a wrong line in a parameter file is accepted");
}

// ---- 5. the beam search ---------------------------------------------------------------------
// A field of random play: `moves` pairs, chains run, nobody dead.
static GameField randomField(Rng& rng, const Rules& r, int moves) {
    for (;;) {
        GameField f;
        for (int m = 0; m < moves && !f.dead(); ++m) {
            const unsigned legal = legalPlacements(f);
            if (!legal) break;
            int id;
            do id = int(rng.below(PLACEMENTS));
            while (!(legal >> id & 1));
            place(f, placement(id), Pair{r.palette[rng.below(uint64_t(r.colors))], r.palette[rng.below(uint64_t(r.colors))]});
            Wave w;
            while (findWaveFor(f.f, w, r.clearCount)) f.f.erase(w.cleared | w.garbage), f.f.drop();
        }
        if (!f.dead() && legalPlacements(f)) return f;
    }
}

static bool sameBuild(const ai::BuildResult& a, const ai::BuildResult& b) {
    bool same = a.best == b.best && a.alive == b.alive && a.nodes == b.nodes;
    for (int id = 0; id < PLACEMENTS; ++id) same = same && a.sum[id] == b.sum[id] && a.points[id] == b.points[id];
    return same;
}

// The same search twice, and on 1, 3 and 6 threads: the same answer. The
// checksum is the same on every compiler (compare the printed value).
static void testBuild() {
    Rng rng(20261006, 40);
    ai::Builder one, many;
    uint64_t sum = 0;
    int64_t nodes = 0;
    int searches = 0;
    for (int i = 0; i < 24; ++i) {
        Rules rules;
        if (i % 4 == 1) rules.colors = 5;
        if (i % 4 == 2) rules.clearCount = 3 + int(rng.below(3));
        if (i % 4 == 3) rules.colors = 3;
        ai::Params p;
        p.beamWidth = 40 + int(rng.below(60)), p.beamDepth = 6 + int(rng.below(6)); // small: this is a test of sameness
        p.beamFixedQueues = i % 2;
        if (i % 6 == 5) p.beamQueues = 9;
        const GameField f = randomField(rng, rules, int(rng.below(26)));
        Pair known[3];
        for (Pair& k : known) k = {rules.palette[rng.below(uint64_t(rules.colors))], rules.palette[rng.below(uint64_t(rules.colors))]};
        const int count = 1 + int(rng.below(3));
        const uint64_t seed = rng.next();
        const ai::BuildResult a = one.search(f, known, count, rules, TM, p, seed, 1);
        const std::string at = " (search " + std::to_string(i) + ")";
        check(sameBuild(a, one.search(f, known, count, rules, TM, p, seed, 1)), "the same search twice differs" + at);
        check(sameBuild(a, many.search(f, known, count, rules, TM, p, seed, 3)), "three threads give another answer" + at);
        check(sameBuild(a, many.search(f, known, count, rules, TM, p, seed, 6)), "six threads give another answer" + at);
        {
            // More threads than queues: the spare ones help to value the fields of a layer.
            const ai::BuildResult b = many.search(f, known, count, rules, TM, p, seed, p.beamQueues + 5);
            bool same = sameBuild(a, b) && a.valued == b.valued;
            for (int id = 0; id < PLACEMENTS; ++id) same = same && a.value[id] == b.value[id];
            check(same, "threads that help the queues give another answer" + at);
        }
        {
            // The layers of the known pairs searched once for all queues (the default) or by every queue: the same answer.
            check(p.beamShare == 1, "the layers of the known pairs are not shared by default");
            ai::Params apart = p;
            apart.beamShare = 0;
            const ai::BuildResult b = one.search(f, known, count, rules, TM, apart, seed, 1);
            bool same = sameBuild(a, b) && a.valued == b.valued;
            for (int id = 0; id < PLACEMENTS; ++id) {
                same = same && a.value[id] == b.value[id];
                for (int n = 0; n < ai::HORIZONS; ++n) same = same && a.sumBy[id][n] == b.sumBy[id][n] && a.pointsBy[id][n] == b.pointsBy[id][n];
            }
            check(same, "sharing the layers of the known pairs gives another answer" + at);
        }
        check(a.best >= 0 && (legalPlacements(f) >> a.best & 1) && (a.alive >> a.best & 1) && a.nodes > 0, "the move is not a legal one" + at);
        for (int id = 0; id < PLACEMENTS; ++id) check((a.alive >> id & 1) || !a.sum[id], "points below a placement that loses" + at);
        ++searches, nodes += a.nodes;
        sum = sum * 0x9E3779B97F4A7C15ull + uint64_t(a.best) + uint64_t(a.nodes) * 31 + uint64_t(a.sum[a.best]) * 131 + a.alive;
    }
    // The made-up pairs drawn from bags (beamFixedQueues = 2): every bag holds every pair once,
    // the queues and the bags of a queue differ in their order, and the seed plays no part.
    for (int colors = 3; colors <= 5; ++colors) {
        Rules rules;
        rules.colors = colors;
        ai::Params p;
        p.beamFixedQueues = 2;
        const int size = colors * colors;
        int differ = 0;
        for (int q = 0; q < 9; ++q)
            for (int bag = 0; bag < 3; ++bag) {
                std::set<int> seen;
                for (int n = 0; n < size; ++n) {
                    Rng oneSeed(1), otherSeed(99);
                    const Pair a = ai::Builder::queuePair(nullptr, 0, rules, p, q, bag * size + n, oneSeed), b = ai::Builder::queuePair(nullptr, 0, rules, p, q, bag * size + n, otherSeed);
                    check(a.axis == b.axis && a.child == b.child && a.axis >= 1 && a.axis <= colors && a.child >= 1 && a.child <= colors, "a pair from the bag");
                    seen.insert(a.axis * 8 + a.child);
                    const Pair first = ai::Builder::bagPair(rules, colors, 0, n);
                    differ += a.axis != first.axis || a.child != first.child;
                }
                check(int(seen.size()) == size, "a bag does not hold every pair once");
            }
        check(differ > size * 9, "the bags come in the same order");
    }
    // The made-up pairs with pairs of one colour among them (beamFixedQueues = 4, four colours): in every eight
    // pairs of a queue the six pairs of two colours once each and two pairs of one colour; in sixteen every colour
    // eight times and each of the four pairs of one colour once; eight queues begin with eight different pairs,
    // six of two colours and two of one; the seed plays no part. They are the default (eight queues);
    // the six fixed sequences (beamFixedQueues 1) are untouched, and stay the first six of more.
    {
        const Rules rules;
        ai::Params p, usual;
        check(usual.beamFixedQueues == 4 && usual.beamQueues == 8, "the default is not eight queues of the table with pairs of one colour");
        usual.beamFixedQueues = 1, usual.beamQueues = 6;
        p.beamFixedQueues = 4, p.beamQueues = 8;
        check(!ai::Builder::seeded(rules, p), "the made-up pairs of beamFixedQueues 4 are said to depend on the seed");
        std::set<int> firsts;
        int firstOnes = 0;
        for (int q = 0; q < 11; ++q) {
            int colour[6] = {0, 0, 0, 0, 0, 0};
            std::set<int> ones;
            for (int round = 0; round < 2; ++round) {
                std::set<int> two;
                int alike = 0;
                for (int n = 0; n < 8; ++n) {
                    Rng oneSeed(1), otherSeed(99);
                    const Pair a = ai::Builder::queuePair(nullptr, 0, rules, p, q, round * 8 + n, oneSeed), b = ai::Builder::queuePair(nullptr, 0, rules, p, q, round * 8 + n, otherSeed);
                    check(a.axis == b.axis && a.child == b.child && a.axis >= 1 && a.axis <= 4 && a.child >= 1 && a.child <= 4, "a pair of beamFixedQueues 4");
                    ++colour[a.axis], ++colour[a.child];
                    if (a.axis == a.child) ++alike, ones.insert(a.axis);
                    else two.insert(std::min(a.axis, a.child) * 8 + std::max(a.axis, a.child));
                    if (round == 0 && n == 0 && q < 8) firsts.insert(a.axis * 8 + a.child), firstOnes += a.axis == a.child;
                }
                check(alike == 2 && two.size() == 6, "eight pairs of beamFixedQueues 4 are not the six pairs of two colours and two of one colour (queue " + std::to_string(q) + ")");
            }
            check(colour[1] == 8 && colour[2] == 8 && colour[3] == 8 && colour[4] == 8 && ones.size() == 4, "sixteen pairs of beamFixedQueues 4 do not hold every colour eight times (queue " + std::to_string(q) + ")");
        }
        check(firsts.size() == 8 && firstOnes == 2, "eight queues of beamFixedQueues 4 do not begin with eight different pairs, two of them of one colour");
        for (int q = 0; q < 6; ++q)
            for (int n = 0; n < 6; ++n) {
                Rng oneSeed(1), otherSeed(1);
                ai::Params old = usual;
                old.beamQueues = 8; // (more queues than six: the first six are still the fixed ones)
                const Pair a = ai::Builder::queuePair(nullptr, 0, rules, usual, q, n, oneSeed), b = ai::Builder::queuePair(nullptr, 0, rules, old, q, n, otherSeed);
                check(a.axis == b.axis && a.child == b.child && a.axis != a.child, "the six fixed queues (beamFixedQueues 1) changed");
            }
    }
    // A placement that loses is not offered: the third column is eleven high.
    GameField tall;
    addGarbage(tall, DEATH_X, 11);
    const Pair pair[2] = {{1, 2}, {3, 4}};
    const ai::BuildResult r = one.search(tall, pair, 2, Rules{}, TM, ai::Params{}, 1, 1);
    check(!(r.alive >> 2 & 1) && !(r.alive >> 13 & 1) && r.best >= 0 && r.best != 2 && r.best != 13, "a placement on the third column that loses is offered");
    std::cout << "beam search: " << searches << " searches the same twice and on 1, 3 and 6 threads, " << nodes << " nodes, checksum " << std::hex
              << uint32_t(sum ^ sum >> 32) << std::dec << '\n';
}

// Toko with the beam as Ama's own loop does it (two pairs known, stop at a
// chain of 78,000 points or after 100 moves): nodes and time of a move.
static void benchBuild(int threads, const ai::Params& p) {
    using Clock = std::chrono::steady_clock;
    const Rules rules;
    ai::Builder builder;
    int64_t nodes = 0, moves = 0, ns = 0, worst = 0;
    for (uint64_t seed = 1; seed <= 2; ++seed) {
        const Tsumo tsumo = makeTsumo(rules, seed);
        PlayerCore me;
        Incoming nobody;
        Rng garbage(1);
        int64_t frame = 0;
        int score = 0, move = 0;
        for (; move < 100; ++move) {
            const Pair known[2] = {tsumo.pair(move), tsumo.pair(move + 1)};
            const auto t0 = Clock::now();
            const ai::BuildResult r = builder.search(me.field, known, 2, rules, TM, p, seed * 1000 + uint64_t(move), threads);
            const int64_t t = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
            ns += t, worst = std::max(worst, t), nodes += r.nodes, ++moves;
            if (r.best < 0) break;
            const TurnResult turn = playTurn(me, nobody, placement(r.best), known[0], rules, TM, frame, garbage);
            frame += turn.frames;
            score = std::max(score, turn.points);
            if (turn.dead || turn.points >= 78000) break;
        }
        std::cout << "  seed " << seed << ": " << move + 1 << " moves, largest chain " << score << " points, " << frame << " frames\n";
    }
    std::cout << "beam 250 x 16, 6 queues, " << threads << " thread(s): " << double(nodes) / double(moves) << " nodes and "
              << double(ns) / 1e6 / double(moves) << " ms per move (longest " << double(worst) / 1e6 << " ms), " << double(nodes) * 1e3 / double(ns)
              << " million nodes per second\n";
}

// ---- 6. the value of a position, and the plans of a player ------------------------------------
static void testValue() {
    const Rules rules;
    const ai::Params p;
    const int rate = rules.targetPoints;
    Rng rng(20261006, 50);
    PlayerCore low, same, dead, buried, threatened;
    low.field = same.field = randomField(rng, rules, 12);
    addGarbage(dead.field, DEATH_X, 12);
    // The plain cases: lost, about to be buried, garbage waiting, garbage on the field.
    check(ai::sideValue(dead, rules, TM, p, rate, 1) == ai::VALUE_LOST, "a closed third column is not lost");
    check(ai::positionValue(dead, low, rules, TM, p, rate, 1) == ai::VALUE_LOST && ai::positionValue(low, dead, rules, TM, p, rate, 1) == -ai::VALUE_LOST &&
              ai::positionValue(dead, dead, rules, TM, p, rate, 1) == 0,
          "lost against not lost");
    check(ai::positionValue(low, same, rules, TM, p, rate, 1) == 0, "the same field on both sides is not even");
    addGarbage(threatened.field, DEATH_X, 8);
    threatened.incoming.pending = 24; // four rows: the third column closes
    check(ai::sideValue(threatened, rules, TM, p, rate, 1) == ai::VALUE_LOST, "garbage that closes the third column is not lost");
    threatened.incoming.pending = 12;
    check(ai::sideValue(threatened, rules, TM, p, rate, 1) > ai::VALUE_LOST, "two rows on a third column of eight is lost");
    same.incoming.confirmed = 6;
    check(ai::positionValue(low, same, rules, TM, p, rate, 1) > 0, "garbage waiting above the opponent is not good");
    same.incoming.confirmed = 0;
    buried = low;
    for (int x = 0; x < W; ++x) addGarbage(buried.field, x, 1);
    check(ai::positionValue(low, buried, rules, TM, p, rate, 1) > 0, "garbage on the opponent's field is not good");
    // A chain ready to fire is worth more than the same pieces apart.
    PlayerCore ready, apart;
    for (int y = 0; y < 3; ++y) ready.field.f.set(0, y, 1), ready.field.f.set(1, y, 2);
    ready.field.f.set(1, 3, 1);
    for (int x = 0; x < W; ++x) apart.field.f.set(x, 0, Cell(1 + x % 2));
    apart.field.f.set(0, 1, 2);
    check(ready.field.f.count() == apart.field.f.count() && ai::positionValue(ready, apart, rules, TM, p, rate, 1) > 0, "a chain ready to fire is not worth more");
}

// The plans of gaze.h against the frame-by-frame game: a plan that fires is
// played in a Round with the fastest input (the opponent presses nothing), and
// the frame of the lock, the garbage sent, the frame of the next pair and the
// field left behind must be the plan's.
static void testGaze() {
    Rng rng(20261006, 51);
    const Rules rules;
    const ai::Params p;
    ai::Gaze gaze;
    ai::PlanSet set;
    int positions = 0, fired = 0, played = 0, skipped = 0, builds = 0;
    for (int i = 0; i < 300; ++i) {
        Round r;
        r.start(rules, rng.next());
        for (int k = 0; k < 2; ++k) {
            GameField f;
            int h[W];
            do {
                f = randomField(rng, rules, 6 + int(rng.below(12)));
                f.heights(h);
            } while (*std::max_element(h, h + W) > 6);
            r.player[k].field = f;
        }
        PlayerEvents e[2];
        const uint8_t none[2] = {0, 0};
        while (!aboutToSpawn(r.player[0])) r.step(none, e);
        const int me = int(rng.below(2));
        const Observation o = ai::observe(r, me);
        gaze.plans(o, me, true, 2, p, 1, set);
        ++positions;
        check(set.pairCount == 2 && !set.midAir, "the pairs of a player whose pair is about to appear");
        int tried = 0;
        for (const ai::Plan& plan : set.plans) {
            if (!plan.fireDepth) {
                ++builds;
                continue;
            }
            ++fired;
            if (tried++ >= 4) continue;
            Round q = r;
            FastInput input;
            int move = 0, sent = 0;
            int64_t lock = -1, end = -1;
            bool disturbed = false;
            for (int frame = 0; frame < 3000 && end < 0 && !disturbed; ++frame) {
                Player& pl = q.player[me];
                uint8_t buttons[2] = {0, 0};
                if (pl.phase == Phase::Control) buttons[me] = input(pl.pair);
                else if (aboutToSpawn(pl)) {
                    if (move == plan.count) {
                        end = q.frame + 1; // the pair after the plan appears in this frame
                        break;
                    }
                    input = FastInput{placement(plan.id[move++])};
                    buttons[me] = input(spawnPair(TM));
                }
                q.step(buttons, e);
                if (e[me].locked) lock = q.frame - 1;
                if (e[me].waveStarted) sent += pl.wave.sent;
                disturbed = e[1 - me].waveStarted || q.state != RoundState::Playing; // the opponent's own pair fell and cleared, or someone lost
            }
            if (disturbed || end < 0) {
                ++skipped;
                continue;
            }
            const std::string at = " (position " + std::to_string(i) + ", plan";
            std::string ids;
            for (int k = 0; k < plan.count; ++k) ids += ' ' + std::to_string(plan.id[k]);
            check(plan.lockFrame == lock, "lock frame " + std::to_string(plan.lockFrame) + ", in the game " + std::to_string(lock) + at + ids + ")");
            check(plan.sent == sent, "garbage sent " + std::to_string(plan.sent) + ", in the game " + std::to_string(sent) + at + ids + ")");
            check(plan.endFrame == end, "next pair at " + std::to_string(plan.endFrame) + ", in the game " + std::to_string(end) + at + ids + ")");
            check(plan.value == ai::buildValue(q.player[me].field, rules, TM, p), "the field after the plan differs" + at + ids + ")");
            ++played;
        }
    }
    check(played > 200 && builds > 1000, "the plans are not exercised");
    std::cout << "plans: " << positions << " positions, " << builds << " plans that build and " << fired << " that fire kept, " << played
              << " of the firing ones played in the frame game with the same lock frame, garbage, next pair and field (" << skipped << " skipped)\n";
}

// A pair in the air that has lain on the ground (Params::x3AirRest): the game takes those frames off the landing
// it waits for after the lock, and the plans must count as the game does. A pair is left alone until it has lain
// some frames on the ground; the plan that puts it with its buttons where it stands is then played in the Round,
// and the frame in which the next pair appears must be the plan's. With x3AirRest 0 the plan names a later frame.
static void testAirRest() {
    Rng rng(20261011, 71);
    const Rules rules;
    ai::Params p, whole;
    whole.x3AirRest = 0;
    ai::Gaze gaze;
    ai::PlanSet set, was;
    ai::InputPlanner planner;
    ai::InputPath path;
    int checked = 0, later = 0, most = 0;
    for (int i = 0; i < 60; ++i) {
        Round r;
        r.start(rules, rng.next());
        for (int k = 0; k < 2; ++k) {
            GameField f;
            int h[W];
            do {
                f = randomField(rng, rules, 6 + int(rng.below(12)));
                f.heights(h);
            } while (*std::max_element(h, h + W) > 6);
            r.player[k].field = f;
        }
        PlayerEvents e[2];
        const uint8_t none[2] = {0, 0};
        const int me = int(rng.below(2)), rest = 1 + int(rng.below(30));
        for (int frame = 0; frame < 3000 && !(r.player[me].phase == Phase::Control && r.player[me].pair.groundFrames >= rest); ++frame) r.step(none, e);
        const Player& pl = r.player[me];
        check(pl.phase == Phase::Control && pl.pair.groundFrames >= rest, "the pair of the rest test did not come to lie on the ground");
        const int id = placementId(pl.pair.placement());
        const Observation o = ai::observe(r, me);
        gaze.plans(o, me, true, 1, p, 1, set);
        gaze.plans(o, me, true, 1, whole, 1, was);
        check(set.midAir && set.pairCount == 1 && (set.firstReach >> id & 1), "the pair in the air cannot be put where it stands");
        const ai::Plan* plan = nullptr;
        const ai::Plan* before = nullptr;
        for (const ai::Plan& s : set.plans)
            if (s.count == 1 && s.id[0] == id) plan = &s;
        for (const ai::Plan& s : was.plans)
            if (s.count == 1 && s.id[0] == id) before = &s;
        if (!plan || !before || plan->fireDepth) continue; // (the placement closes the field, or clears: not this test)
        check(planner.plan(pl.field, pl.pair, placement(id), TM, rules.speedPercent, path), "no buttons for the placement the pair stands on");
        Round q = r;
        int64_t lock = -1, end = -1;
        size_t k = 0;
        bool disturbed = false;
        for (int frame = 0; frame < 3000 && end < 0 && !disturbed; ++frame) {
            uint8_t buttons[2] = {0, 0};
            if (k < path.buttons.size()) buttons[me] = path.buttons[k++];
            else if (aboutToSpawn(q.player[me])) {
                end = q.frame + 1; // the next pair appears in this frame
                break;
            }
            q.step(buttons, e);
            if (e[me].locked) lock = q.frame - 1;
            disturbed = e[me].waveStarted || e[1 - me].waveStarted || q.state != RoundState::Playing;
        }
        if (disturbed || end < 0) continue;
        const std::string at = " (position " + std::to_string(i) + ", " + std::to_string(int(pl.pair.groundFrames)) + " frames on the ground)";
        check(plan->lockFrame == lock, "the pair in the air: lock frame " + std::to_string(plan->lockFrame) + ", in the game " + std::to_string(lock) + at);
        check(plan->endFrame == end, "the pair in the air: next pair at " + std::to_string(plan->endFrame) + ", in the game " + std::to_string(end) + at);
        check(before->endFrame >= plan->endFrame && before->endFrame - plan->endFrame <= TM.landFrames - TM.lockWait, "x3AirRest 0 names a frame that is not later by a landing at most" + at);
        later += before->endFrame > plan->endFrame;
        most = std::max(most, int(before->endFrame - plan->endFrame));
        ++checked;
    }
    check(checked >= 40 && later >= 30 && most == TM.landFrames - TM.lockWait, "the rest test is not exercised: " + std::to_string(checked) + " checked, " + std::to_string(later) + " later, at most " + std::to_string(most));
    std::cout << "a pair in the air that lay on the ground: " << checked << " plans name the frame of the next pair as the game has it; without x3AirRest "
              << later << " of them name a later one, by " << most << " frames at most\n";
}

// The AI that does not poke (ai/agents.h, x3q): X3 with the eight settings of quietPoke, whatever the default is.
// quietPoke sets them, and over a round against the random agent the buttons of x3q are those of X3 given them.
static void testQuietPoke() {
    ai::Params small;
    small.beamWidth = 30, small.beamDepth = 8, small.beamQueues = 3;
    ai::Params quiet = small;
    quiet.evFireHeight = 300, quiet.x3PokeGain = 75, quiet.xWildChance = 0, quiet.xWildEvery = 2, quiet.x3PokeNet = 0, quiet.x3PokeFrom = 0, quiet.x3KeepMain = 85, quiet.x3Crowd = 1000;
    const ai::Params made = ai::quietPoke(small), standard = ai::agentParams("x3", {});
    check(made.evFireHeight == 300 && made.x3PokeGain == 75 && made.xWildChance == 0 && made.xWildEvery == 2 && made.x3PokeNet == 0 && made.x3PokeFrom == 0 &&
              made.x3KeepMain == 85 && made.x3Crowd == 1000 && made.beamWidth == small.beamWidth,
          "quietPoke does not set its eight settings and leave the rest");
    check(standard.x3PokeNet == 0 && made.x3PokeNet == 0 && standard.x3PokeFrom != made.x3PokeFrom && standard.x3KeepMain != made.x3KeepMain,
          "the regular and quiet levels share the zero net setting and retain their distinct poke thresholds");
    const Rules rules;
    std::vector<uint8_t> buttons[2];
    for (int k = 0; k < 2; ++k) {
        const std::unique_ptr<ai::Agent> a = k == 0 ? ai::makeAgent("x3q", small) : ai::makeAgent("x3", quiet);
        const std::unique_ptr<ai::Agent> b = ai::makeAgent("random");
        check(a && b, "the agents x3q, x3 and random are not all there");
        Round r;
        r.start(rules, 21);
        a->startRound(0, 5), b->startRound(1, 6);
        ai::Observation o;
        PlayerEvents e[2];
        for (int f = 0; f < 4000 && r.state == RoundState::Playing; ++f) {
            ai::observe(r, 0, o);
            uint8_t now[2];
            now[0] = a->frame(o);
            o.me = 1;
            now[1] = b->frame(o);
            buttons[k].push_back(now[0]);
            r.step(now, e);
        }
    }
    check(buttons[0] == buttons[1] && buttons[0].size() > 500, "x3q does not play as X3 with the settings of quietPoke");
    check(std::string(ai::agentNames()).find("x3q") != std::string::npos, "x3q is not among the names of the agents");
    std::cout << "the AI that hardly pokes (x3q): quietPoke sets its eight settings, and x3q presses the buttons of X3 given them over a round of " << buttons[0].size() << " frames\n";
}

// The levels (ai/level.h; the agents lv1 to lv36): their names, the groups the screen shows them in, the table as
// it was measured (rows written out here once more), that a level changes its twelve settings and nothing else,
// that the top level is X3 as it is, and that over a round against the random agent a level presses the buttons
// of X3 given its settings.
static void testKnownSearchPairs() {
    ai::Observation o;
    o.me = 1;
    o.ownTsumo = true;
    o.player[1].phase = Phase::Control;
    o.player[1].nextPair = 1;
    o.player[1].colors = {Cell(1), Cell(2)};
    o.tsumo.pairs.assign(20, {Cell(1), Cell(1)});
    o.tsumo1.pairs.assign(20, {Cell(3), Cell(4)});
    o.tsumo1.pairs[0] = o.player[1].colors;
    ai::Params p = ai::levelParams(ai::Params{}, 37);
    ai::Gaze gaze;
    ai::PlanSet plans;
    // Limit the exhaustive search to one placement. The beam still receives 16.
    gaze.plans(o, 1, true, 1, p, 1, plans);
    check(plans.pairCount == 1 && plans.knownCount == 16 && ai::samePair(plans.pair[0], o.player[1].colors) &&
          ai::samePair(plans.pair[1], o.tsumo1.pair(1)) && ai::samePair(plans.pair[15], o.tsumo1.pair(15)),
          "a pair in the hand appears once and the longer known prefix belongs to that seat");
    gaze.plans(o, 1, true, 1, ai::Params{}, 1, plans);
    check(plans.pairCount == 1 && plans.knownCount == 1, "the legacy beam uses exactly the pairs used by its exhaustive plans");
    o.player[1].phase = Phase::Next;
    o.player[1].nextPair = 0;
    o.player[1].wait = 1;
    o.tsumo1.pairs.resize(4);
    gaze.plans(o, 1, true, 4, p, 1, plans);
    check(plans.pairCount == 3 && plans.knownCount == 4, "four known pairs feed the beam, but exhaustive search stops at three");
    for (const auto& plan : plans.plans) check(plan.count <= 3, "exhaustive plan over three placements");
    o.tsumo1.pairs.resize(2);
    gaze.plans(o, 1, true, 3, p, 1, plans);
    check(plans.knownCount == 2, "a shortened observation exposes no previous fourth pair");

    struct RivalProbe : ai::X3Agent {
        using X3Agent::X3Agent;
        using X3Pace::beginRival;
        using X3Pace::takeRival;
    };
    p.beamWidth = 2;
    p.beamDepth = 4;
    p.beamQueues = 1;
    p.searchThreads = 1;
    RivalProbe rival(p);
    std::array<Pair, ai::MAX_KNOWN> known;
    known.fill({Cell(1), Cell(2)});
    const GameField empty;
    check(!rival.beginRival(empty, known.data(), 17, o.rules, o.timing, p, 1, true),
          "the rival search rejects a count beyond the captured array");
    for (int count : {4, 11, 16}) {
        auto caller = known;
        check(rival.beginRival(empty, caller.data(), count, o.rules, o.timing, p, 1, true),
              "begin a rival search with the entire known prefix");
        caller[count - 1] = {Cell(3), Cell(4)};
        ai::BuildResult actual;
        const auto end = ai::Builder::Clock::now() + std::chrono::seconds(3);
        check(!rival.takeRival(empty, caller.data(), count, p, 1, end, actual),
              "a correction at the end of a known prefix invalidates the cached answer");
        check(rival.takeRival(empty, known.data(), count, p, 1, end, actual),
              "the worker owns a copy of the entire original prefix");
        ai::Builder direct;
        const auto expected = direct.search(empty, known.data(), count, o.rules, o.timing, p, 1, 1);
        check(actual.best == expected.best && actual.alive == expected.alive &&
              std::equal(actual.sum, actual.sum + PLACEMENTS, expected.sum),
              "the asynchronous and direct searches use the same known pairs");
    }
}

static void testLevelTable() {
    const auto standard = ai::namedDefaults("x3");
    check(ai::levelOf("lv37") == 37 && !ai::makeAgent("lv37q") &&
          ai::agentParams("lv37", standard).knownPairs == ai::MAX_KNOWN &&
          ai::agentParams("lv36", standard).knownPairs == 3 && ai::agentParams("x3", standard).knownPairs == 3,
          "only the selected level 37 uses the longer known prefix");
    check(ai::LEVELS == 36 && ai::levelOf("lv1") == 1 && ai::levelOf("lv9") == 9 && ai::levelOf("lv36") == 36 && ai::levelOf("lv1q") == 1 && ai::levelOf("lv12q") == 12 && ai::levelOf("lv36q") == 36,
          "the names of the levels are not read");
    check(ai::levelOf("lv38") == 38 && ai::makeAgent("lv38") && ai::agentParams("lv38", standard).lv38Midgame == 1 &&
          !ai::agentParams("lv37", standard).lv38Midgame && !ai::agentParams("lv36", standard).lv38Midgame &&
          !standard.lv38Midgame && ai::LAST_LEVEL == 38, "Lv 38 の明示指定と画面の上限");
    {
        ai::Params off = standard;
        off.lv38AnswerLegal = off.lv38WinRecheck = off.lv38FullOffsetNext = 0;
        const auto lv38 = ai::agentParams("lv38", standard), lv38ex = ai::agentParams("lv38ex", standard), given = ai::agentParams("lv38", off);
        check(lv38.lv38AnswerLegal == 1 && lv38.lv38WinRecheck == 1 && lv38ex.lv38AnswerLegal == 1 && lv38ex.lv38WinRecheck == 1 &&
              lv38.lv38FullOffsetNext == 1 && lv38ex.lv38FullOffsetNext == 1 && given.lv38FullOffsetNext == 0 &&
              given.lv38AnswerLegal == 0 && given.lv38WinRecheck == 0 && !ai::agentParams("lv37", standard).lv38Midgame,
              "Lv 38 (lv38, lv38ex) checks every win it picks again and keeps the tactical answers to the placements the input reaches; a given 0 turns them off");
    }
    const auto development = ai::agentParams("lv38", standard);
    check(standard.lv38Plan5 == -1 && development.lv38Plan5 == 1 && development.lv38FastInput &&
          development.lv38TwoWave && development.lv38TwoWaveMainCover,
          "Lv38 keeps its input and two-wave defence and resolves the tail setting");
    ai::Params givenDefense = standard;
    givenDefense.lv38TwoWaveNoMain = 0;
    givenDefense.lv38ReadyCover = 1;
    for (const char* name : {"lv38", "lv38ex"}) {
        const auto defaults = ai::agentParams(name, standard);
        const auto given = ai::agentParams(name, givenDefense);
        check(defaults.lv38TwoWaveNoMain == 1 && defaults.lv38ReadyCover == 0 &&
              given.lv38TwoWaveNoMain == 0 && given.lv38ReadyCover == 1,
              "Named Lv38 defaults suppress main-chain substitutions and leave explicit defence settings intact");
    }
    auto explicitTail = standard;
    explicitTail.lv38Plan5 = 1;
    check(ai::agentParams("lv38", explicitTail).lv38Plan5 == 1 && !ai::agentParams("lv37", explicitTail).lv38Midgame,
          "only explicit Lv38 uses the enabled tail setting");
    explicitTail.lv38Plan5 = 0;
    check(ai::agentParams("lv38", explicitTail).lv38Plan5 == 0,
          "an explicit tail setting overrides the Lv38 default");
    auto inherited = ai::agentParams("lv38", standard);
    for (const char* name : {"lv36", "lv37", "x3", "x3q", "lv36q"})
        check(!ai::agentParams(name, inherited).lv38Midgame, "Lv 38 から切り替えた後に中盤判断を引き継いでいます");
    for (const char* name : {"lv", "lv0", "lv01", "lv39", "lv100", "lv1a", "x3", "x3q", "random", "", "LV1", "q", "lvq", "lv0q", "lv37q", "lv38q", "lv100q", "lv1qq"})
        check(!ai::levelOf(name), std::string("a level is read from the name ") + name);
    check(!ai::quietOf("x3") && ai::quietOf("x3q") && !ai::quietOf("lv12") && ai::quietOf("lv12q") && ai::quietOf("lv36q") && !ai::quietOf("random") && !ai::quietOf("q"), "which names are the AI that hardly pokes");
    const auto text = [](const ai::Params& p) {
        std::ostringstream out;
        ai::saveParams(out, p);
        return out.str();
    };
    check(text(ai::levelParams(standard, ai::LEVELS)) == text(standard) && text(ai::agentParams("lv36", standard)) == text(standard) && text(ai::agentParams("x3", standard)) == text(standard),
          "the top level is not X3 as it is");
    // (the two switches of the top level, which the levels below it play without: level.h)
    check(standard.x3Enough == 110 && standard.x3Guard == 10 && standard.x3GuardNear == 20 && standard.x3GuardMost == 50, "the top level does not play with x3Enough 110 and the guard 10, 20, 50");
    check(text(ai::levelParams(standard, 0)) == text(standard) && text(ai::levelParams(standard, 39)) == text(standard), "a number that is no level changes the settings");
    check(text(ai::agentParams("x3q", standard)) == text(ai::quietPoke(standard)) && text(ai::agentParams("lv36q", standard)) == text(ai::quietPoke(standard)),
          "agentParams does not give x3q the settings of quietPoke");
    // (either AI at every level: the level of the AI that hardly pokes is its eight settings and the fourteen of the level)
    for (int level = 1; level < ai::LEVELS; ++level) {
        const std::string name = "lv" + std::to_string(level);
        check(text(ai::agentParams(name, standard)) == text(ai::levelParams(standard, level)), name + " is not X3 with the settings of its level");
        check(text(ai::agentParams(name + "q", standard)) == text(ai::levelParams(ai::quietPoke(standard), level)) &&
                  text(ai::agentParams(name + "q", standard)) == text(ai::quietPoke(ai::levelParams(standard, level))) && text(ai::agentParams(name + "q", standard)) != text(ai::agentParams(name, standard)),
              name + "q is not the AI that hardly pokes with the settings of its level");
        check(ai::makeAgent(name) && ai::makeAgent(name + "q"), "no agent of the name " + name);
    }
    static const int GROUP_OF[ai::LEVELS] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 5};
    for (int level = 1; level <= ai::LEVELS; ++level) check(ai::levelGroup(level) == GROUP_OF[level - 1], "the group of level " + std::to_string(level));
    const auto is = [&](int level, int hand, int see, int width, int depth, int queues, int add, int runs, int again, int pairs, int noise) {
        const ai::Params p = ai::levelParams(standard, level);
        return p.handWait == hand && p.seeLate == see && p.beamWidth == width && p.beamDepth == depth && p.beamQueues == queues && p.trialMaxAdd == add && p.keyRuns == runs &&
               p.thinkAgain == again && p.x2Depth == pairs && p.b0Noise == noise && p.beamFixedQueues == 1 && p.thinkWidthMax == 0 && p.x3Enough == 0 && p.x3Guard == 0;
    };
    check(is(1, 116, 12, 22, 1, 1, 1, 0, 0, 1, 0) && is(9, 0, 12, 22, 1, 1, 1, 0, 0, 1, 0) && is(10, 64, 12, 22, 2, 2, 1, 0, 0, 1, 0) && is(16, 21, 12, 20, 2, 2, 1, 0, 0, 1, 0) &&
              is(21, 11, 12, 30, 3, 3, 1, 0, 0, 1, 150) && is(24, 10, 12, 40, 4, 4, 1, 0, 0, 1, 100) && is(26, 9, 12, 49, 5, 5, 1, 1, 0, 1, 0) && is(28, 8, 10, 90, 8, 6, 1, 2, 0, 1, 0) &&
              is(30, 6, 9, 100, 6, 6, 2, 4, 0, 2, 0) && is(33, 3, 3, 220, 16, 6, 3, 8, 0, 2, 0) && is(34, 4, 6, 150, 10, 6, 3, 8, 1, 3, 0) && is(35, 3, 3, 220, 16, 6, 3, 16, 1, 3, 0),
          "a level is not as it was measured");
    for (int level = 1; level < ai::LEVELS; ++level) {
        // (the fourteen settings of a level put back: nothing else may differ from the default)
        ai::Params p = ai::levelParams(standard, level);
        p.handWait = standard.handWait, p.seeLate = standard.seeLate, p.beamWidth = standard.beamWidth, p.beamDepth = standard.beamDepth, p.beamQueues = standard.beamQueues;
        p.beamFixedQueues = standard.beamFixedQueues, p.trialMaxAdd = standard.trialMaxAdd, p.keyRuns = standard.keyRuns, p.thinkAgain = standard.thinkAgain, p.x2Depth = standard.x2Depth;
        p.b0Noise = standard.b0Noise, p.thinkWidthMax = standard.thinkWidthMax, p.x3Enough = standard.x3Enough, p.x3Guard = standard.x3Guard;
        check(text(p) == text(standard), "level " + std::to_string(level) + " changes a setting that is not one of its fourteen");
        check(ai::LEVEL_PARENT_PERMILLE[level - 1] >= 590 && ai::LEVEL_PARENT_PERMILLE[level - 1] <= 627, "the rate of level " + std::to_string(level) + " against the level above it");
        // (a level is no stronger in what it searches than the top)
        const ai::LevelSettings& l = ai::LEVEL_SETTINGS[level - 1];
        check(l.beamWidth <= standard.beamWidth && l.beamDepth <= standard.beamDepth && l.beamQueues <= 6 && l.trialMaxAdd <= standard.trialMaxAdd && l.keyRuns <= standard.keyRuns &&
                  l.x2Depth <= standard.x2Depth && l.handWait >= 0 && l.seeLate >= 3,
              "level " + std::to_string(level) + " searches more than the top, or sees the opponent at once");
    }
    const Rules rules;
    size_t frames = 0;
    for (int level : {12, 30}) {
        std::vector<uint8_t> buttons[2];
        for (int k = 0; k < 2; ++k) {
            const std::unique_ptr<ai::Agent> a = k == 0 ? ai::makeAgent("lv" + std::to_string(level)) : ai::makeAgent("x3", ai::levelParams(standard, level));
            const std::unique_ptr<ai::Agent> b = ai::makeAgent("random");
            check(a && b, "the agent of a level is not there");
            Round r;
            r.start(rules, 23);
            a->startRound(0, 5), b->startRound(1, 6);
            ai::Observation o;
            PlayerEvents e[2];
            for (int f = 0; f < 2500 && r.state == RoundState::Playing; ++f) {
                ai::observe(r, 0, o);
                uint8_t now[2];
                now[0] = a->frame(o);
                o.me = 1;
                now[1] = b->frame(o);
                buttons[k].push_back(now[0]);
                r.step(now, e);
            }
        }
        check(buttons[0] == buttons[1] && buttons[0].size() > 300, "lv" + std::to_string(level) + " does not play as X3 with the settings of its level");
        frames += buttons[0].size();
    }
    check(std::string(ai::agentNames()).find("lv1") != std::string::npos, "the levels are not among the names of the agents");
    std::cout << "the levels: 36 names, six groups, the table as measured, twelve settings a level and no other, and lv12 and lv30 press the buttons of X3 given their settings over "
              << frames << " frames\n";
}

// ---- 7. moves by kind --------------------------------------------------------------------------
// A chain of n waves on columns x0 .. x0 + n - 1 (n <= 4): every column has
// three of its colour, and on top one of the colour of the column before.
// It starts with one piece of the last column's colour on the floor of column x0 + n.
static void stairs(GameField& g, int x0, int n) {
    for (int k = 0; k < n; ++k) {
        for (int y = 0; y < 3; ++y) g.f.set(x0 + k, y, Cell(1 + k % 4));
        if (k) g.f.set(x0 + k, 3, Cell(1 + (k - 1) % 4));
    }
}

// A round in which player 0's pair is about to appear, with the given fields and pair.
static Round situation(const Rules& rules, const GameField& mine, const GameField& theirs, Pair pair) {
    Round r;
    r.start(rules, 1);
    r.player[0].field = mine, r.player[1].field = theirs;
    PlayerEvents e[2];
    const uint8_t none[2] = {0, 0};
    while (!aboutToSpawn(r.player[0])) r.step(none, e);
    for (int i = 0; i < TSUMO_PAIRS; ++i) r.tsumo.piece[size_t(i) * 2] = pair.axis, r.tsumo.piece[size_t(i) * 2 + 1] = pair.child; // every pair is this one
    return r;
}

// ---- the levels below the strongest: slow hands (Params::handWait), a late view of the opponent (Params::seeLate) ----
// A round of one player played by an agent: the frames in which its pairs appeared and locked.
struct SoloRun {
    std::vector<int64_t> spawn, lock;
    std::vector<uint8_t> buttons;
    GameField field;
};
static SoloRun soloRun(ai::PlacementAgent& agent, uint64_t seed, int pairs, const GameField* start = nullptr) {
    const Rules rules;
    Round r;
    r.start(rules, seed, 1);
    if (start) r.player[0].field = *start;
    agent.startRound(0, seed + 1);
    SoloRun run;
    ai::Observation o;
    PlayerEvents e[2];
    for (int f = 0; f < 30000 && r.state == RoundState::Playing && int(run.lock.size()) < pairs; ++f) {
        ai::observe(r, 0, o);
        const uint8_t now[2] = {agent.frame(o), 0};
        run.buttons.push_back(now[0]);
        r.step(now, e);
        if (e[0].spawned) run.spawn.push_back(r.frame - 1);
        if (e[0].locked) run.lock.push_back(r.frame - 1);
    }
    run.field = r.player[0].field;
    return run;
}

// X3 with the protected parts of the late view open.
struct PeekX3 : ai::X3Agent {
    using ai::X3Agent::X3Agent;
    using ai::X3Agent::sight;
    using ai::X3Agent::viewFrame;
};

static void testLevels() {
    const Rules rules;
    // 1. Slow hands. The random agent puts the same pairs on the same placements with and without the wait (its
    // choices do not depend on time), so the two rounds differ only in the frames: no pair locks earlier, none
    // later than by the wait, and most of them later by nearly all of it (the pair falls a little while it waits).
    const int wait = 20, pairs = 24;
    int rounds = 0, moves = 0, full = 0, least = wait, most = 0;
    int64_t waited = 0, extra = 0;
    for (uint64_t seed = 1; seed <= 12; ++seed) {
        ai::RandomAgent fast, slow;
        slow.handWait = wait;
        const SoloRun a = soloRun(fast, seed, pairs), b = soloRun(slow, seed, pairs);
        check(fast.handWaited == 0, "an agent with the fastest input left a pair alone");
        const std::string at = " (round " + std::to_string(seed) + ")";
        check(a.lock.size() == b.lock.size() && a.spawn.size() == b.spawn.size() && a.field == b.field,
              "slow hands put the pairs elsewhere" + at);
        check(a.spawn.size() >= a.lock.size() && !a.lock.empty(), "a pair locked that had not appeared" + at);
        for (size_t k = 0; k < a.lock.size(); ++k) {
            const int more = int((b.lock[k] - b.spawn[k]) - (a.lock[k] - a.spawn[k]));
            check(more >= 0 && more <= wait, "slow hands: pair " + std::to_string(k) + " took " + std::to_string(more) + " frames longer" + at);
            least = std::min(least, more), most = std::max(most, more), extra += more, ++moves;
            full += more >= wait - wait * TM.fallPerFrame / TM.softDropPerFrame - 1;
        }
        waited += slow.handWaited;
        check(slow.handWaited >= 0 && slow.handWaited <= int64_t(wait) * int64_t(a.spawn.size()), "more frames waited than the wait allows" + at);
        ++rounds;
    }
    check(moves >= 150 && full * 10 >= moves * 7 && waited >= extra, "the test of the slow hands is not exercised: " + std::to_string(moves) + " pairs, " +
                                                                        std::to_string(full) + " of them slower by the whole wait, " + std::to_string(waited) +
                                                                        " frames waited, " + std::to_string(extra) + " frames longer");
    // ... on a high field a long wait ends before the pair comes to rest by itself, and the pair still goes where
    // the fastest input puts it.
    int64_t cut = 0;
    {
        GameField high;
        for (int x = 0; x < W; ++x)
            for (int y = 0; y < 9; ++y) high.f.set(x, y, Cell(1 + (x + 2 * y) % 4)); // (no two neighbours alike: nothing clears)
        const int longWait = 200;
        ai::RandomAgent fast, slow;
        slow.handWait = longWait;
        const SoloRun a = soloRun(fast, 5, 3, &high), b = soloRun(slow, 5, 3, &high);
        check(!a.lock.empty() && a.lock.size() == b.lock.size() && a.field == b.field, "slow hands on a high field put the pairs elsewhere");
        check(slow.handWaited > 0 && slow.handWaited < int64_t(longWait) * int64_t(a.lock.size()), "slow hands on a high field: the wait did not end early (" +
                                                                                                       std::to_string(slow.handWaited) + " frames over " + std::to_string(a.lock.size()) + " pairs)");
        for (size_t k = 0; k < a.lock.size(); ++k)
            check(b.lock[k] - b.spawn[k] >= a.lock[k] - a.spawn[k] && b.lock[k] - b.spawn[k] < a.lock[k] - a.spawn[k] + longWait, "slow hands on a high field: a pair locked earlier, or waited the whole wait");
        cut = slow.handWaited / int64_t(a.lock.size());
    }
    // ... in the reading: the own pair locks later by the wait, the opponent's does not; and a move counts that much longer.
    {
        Round r = situation(rules, GameField{}, GameField{}, Pair{Cell(1), Cell(2)});
        const Observation o = ai::observe(r, 0);
        ai::Duel d(o), w(o);
        w.wait[0] = wait;
        check(ai::toPairOf(d, 0, o.frame + 100) && ai::toPairOf(w, 0, o.frame + 100), "the pair of the Duel does not appear");
        d.placeFast(0, placement(0), Pair{Cell(1), Cell(2)}), w.placeFast(0, placement(0), Pair{Cell(1), Cell(2)});
        check(w.player[0].lockFrame == d.player[0].lockFrame + wait, "Duel::wait does not lock the own pair later by the wait");
        ai::Duel d1(o), w1(o);
        w1.wait[0] = wait;
        if (d1.player[1].phase != ai::DuelPhase::Control) check(ai::toPairOf(d1, 1, o.frame + 200) && ai::toPairOf(w1, 1, o.frame + 200), "the opponent's pair of the Duel does not appear");
        d1.placeFast(1, placement(0), Pair{Cell(1), Cell(2)}), w1.placeFast(1, placement(0), Pair{Cell(1), Cell(2)});
        check(w1.player[1].lockFrame == d1.player[1].lockFrame, "Duel::wait of one seat moved the lock of the other");
        ai::Params p, q;
        q.x3MoveFrames += 7, q.x3MoveFramesMid += 7, q.x3MoveFramesLate += 7;
        check(p.x3MoveFramesMid > 0 && p.x3MoveFramesLate > 0, "the test of movesIn needs three lengths of a move");
        for (const int64_t frames : {int64_t(0), int64_t(49), int64_t(50), int64_t(333), int64_t(1000), int64_t(4000)})
            for (const int cells : {0, 30, 44, 60, 70})
                check(p.movesIn(frames, cells, 7) == q.movesIn(frames, cells) && p.movesIn(frames, cells, 0) == p.movesIn(frames, cells),
                      "movesIn with longer moves is not movesIn of longer lengths");
    }
    // 2. X3 with either setting: the same seeds give the same round, and another round than without it.
    ai::Params small;
    small.beamWidth = 30, small.beamDepth = 8, small.beamQueues = 3;
    const auto versusRandom = [&](const ai::Params& p, int64_t* waitedFrames) {
        ai::X3Agent a(p);
        const std::unique_ptr<ai::Agent> b = ai::makeAgent("random");
        Round r;
        r.start(rules, 21);
        a.startRound(0, 5), b->startRound(1, 6);
        std::vector<uint8_t> buttons;
        ai::Observation o;
        PlayerEvents e[2];
        for (int f = 0; f < 4000 && r.state == RoundState::Playing; ++f) {
            ai::observe(r, 0, o);
            uint8_t now[2];
            now[0] = a.frame(o);
            o.me = 1;
            now[1] = b->frame(o);
            buttons.push_back(now[0]);
            r.step(now, e);
        }
        if (waitedFrames) *waitedFrames = a.handWaited;
        return buttons;
    };
    ai::Params slowHands = small, lateEyes = small;
    slowHands.handWait = 12, lateEyes.seeLate = 90; // (90: the second wave of a chain of his is sent before he is seen to fire)
    int64_t none = 0, some = 0;
    const std::vector<uint8_t> plain = versusRandom(small, &none), slow1 = versusRandom(slowHands, &some), slow2 = versusRandom(slowHands, nullptr);
    check(none == 0 && some > 0 && slow1 == slow2 && slow1 != plain && plain.size() > 500, "X3 with slow hands: not the same round twice, or the round of the fastest input");
    const std::vector<uint8_t> late1 = versusRandom(lateEyes, nullptr), late2 = versusRandom(lateEyes, nullptr);
    check(late1 == late2 && late1.size() > 500, "X3 that sees late: not the same round twice");
    // 3. The late view, frame by frame, against an X3 that fires chains: the opponent's side is that of the frame
    // he is seen at, never more than seeLate frames old and never going back; no late view shows a chain of his
    // running; what waits above the two fields fits that frame (counted here from the events of the round); and
    // a chain of his is noticed seeLate frames after the pair that fires it has locked, or sooner.
    const int lag = lateEyes.seeLate;
    int64_t lagged = 0, present = 0, hidden = 0, running = 0, noticed = 0, exact = 0;
    {
        ai::Params off = small;
        PeekX3 none0(off);
        Round r;
        r.start(rules, 7);
        none0.startRound(0, 1);
        const Observation o = ai::observe(r, 0);
        none0.frame(o);
        check(&none0.sight(o) == &o, "without seeLate the screen is not shown as it is");
    }
    for (uint64_t seed = 31; seed <= 32; ++seed) {
        PeekX3 a(lateEyes);
        const std::unique_ptr<ai::Agent> b = ai::makeAgent("x3", small);
        Round r;
        r.start(rules, seed);
        a.startRound(0, seed * 2 + 1), b->startRound(1, seed * 2 + 2);
        std::vector<Player> his;      // his side before every frame
        std::vector<int64_t> sent[2]; // what the waves of each player had sent before every frame
        int64_t total[2] = {0, 0}, lastView = -1, firedAt = -1;
        ai::Observation o;
        PlayerEvents e[2];
        for (int f = 0; f < 6000 && r.state == RoundState::Playing; ++f) {
            ai::observe(r, 0, o);
            his.push_back(o.player[1]), sent[0].push_back(total[0]), sent[1].push_back(total[1]);
            uint8_t now[2];
            now[0] = a.frame(o);
            const std::string at = " (round " + std::to_string(seed) + ", frame " + std::to_string(f) + ")";
            const Observation& v = a.sight(o);
            const int64_t seenAt = a.viewFrame;
            check(seenAt >= lastView && seenAt <= o.frame && o.frame - seenAt <= lag, "the frame the opponent is seen at: " + std::to_string(seenAt) + at);
            lastView = seenAt;
            if (seenAt == o.frame) {
                check(&v == &o, "the present is not shown as it is" + at);
                ++present, running += o.player[1].phase == Phase::Wave;
                if (firedAt >= 0) {
                    const int64_t after = o.frame - (firedAt + 1);
                    check(after <= lag, "a chain of his was noticed " + std::to_string(after) + " frames after its pair locked" + at);
                    ++noticed, exact += after == lag;
                    firedAt = -1;
                }
            } else {
                ++lagged;
                check(&v != &o && v.frame == o.frame && v.me == o.me && v.tsumo.count() == o.tsumo.count(), "the late view is not of this frame" + at);
                Player want = his[size_t(seenAt)];
                want.incoming = v.player[1].incoming;
                check(samePlayer(v.player[1], want), "his side is not that of the frame he is seen at" + at);
                check(v.player[1].phase != Phase::Wave, "a late view shows a chain of his running" + at);
                Player mine = o.player[0];
                mine.incoming = v.player[0].incoming;
                check(samePlayer(v.player[0], mine), "the own side of a late view is not the present one" + at);
                const int64_t unseen = sent[1][size_t(f)] - sent[1][size_t(seenAt)], own = sent[0][size_t(f)] - sent[0][size_t(seenAt)];
                const int64_t left = std::max<int64_t>(0, o.player[0].incoming.total() - unseen);
                check(v.player[0].incoming.pending == 0 && v.player[0].incoming.confirmed == left,
                      "above the own field: " + std::to_string(v.player[0].incoming.total()) + " in the late view, " + std::to_string(left) + " by the events" + at);
                check(v.player[1].incoming.total() == his[size_t(seenAt)].incoming.total() + own, "above his field: not what waited then and what was sent since" + at);
                hidden += unseen > 0;
            }
            o.me = 1;
            now[1] = b->frame(o);
            r.step(now, e);
            for (int i = 0; i < 2; ++i)
                if (e[i].waveStarted) total[i] += r.player[i].wave.sent;
            // (a pair of his that fires has locked in this frame: the screen shows it from the next frame on)
            if (e[1].locked && firedAt < 0 && r.player[1].field.f.hasClear()) firedAt = r.frame - 1;
        }
    }
    check(lagged >= 1000 && present >= 30 && running >= 10 && hidden >= 1 && noticed >= 2 && exact >= 1,
          "the test of the late view is not exercised: " + std::to_string(lagged) + " late frames, " + std::to_string(present) + " present, " + std::to_string(running) +
              " with his chain running, " + std::to_string(hidden) + " with garbage hidden, " + std::to_string(noticed) + " chains noticed, " + std::to_string(exact) + " of them after the whole delay");
    // 4. A match for measuring in which each player draws from a tsumo of his own (Round::ownTsumo, tools/ai_match
    // --own-tsumo): each gets the pairs of his tsumo, and the observation holds of each tsumo what that player's
    // window has shown, no more. With the one tsumo of the game nothing changes.
    int own[2] = {0, 0};
    {
        Rng rng(20261012, 3);
        Round r;
        r.start(rules, 11);
        r.splitTsumo(12);
        bool differ = false;
        for (int i = 0; i < 8; ++i) differ = differ || r.tsumo.pair(i).axis != r.tsumo1.pair(i).axis || r.tsumo.pair(i).child != r.tsumo1.pair(i).child;
        check(r.ownTsumo && differ, "the tsumo of player 1 is that of player 0");
        ai::Observation o;
        PlayerEvents e[2];
        uint8_t buttons[2] = {0, 0};
        for (int f = 0; f < 3000 && r.state == RoundState::Playing; ++f) {
            ai::observe(r, 0, o);
            const std::string at = " (frame " + std::to_string(f) + ")";
            check(o.ownTsumo && o.tsumo.count() == ai::shownPairs(r.player[0], r.timing) && o.tsumo1.count() == ai::shownPairs(r.player[1], r.timing),
                  "own tsumo: the observation does not hold what each window has shown" + at);
            for (int i = 0; i < o.tsumo.count(); ++i)
                check(o.tsumo.pair(i).axis == r.tsumo.pair(i).axis && o.tsumo.pair(i).child == r.tsumo.pair(i).child, "own tsumo: a pair of player 0 differs" + at);
            for (int i = 0; i < o.tsumo1.count(); ++i)
                check(o.tsumo1.pair(i).axis == r.tsumo1.pair(i).axis && o.tsumo1.pair(i).child == r.tsumo1.pair(i).child, "own tsumo: a pair of player 1 differs" + at);
            check(&o.pairsOf(0) == &o.tsumo && &o.pairsOf(1) == &o.tsumo1 && &o.mine() == &o.tsumo, "own tsumo: pairsOf does not name the tsumo of the seat" + at);
            o.me = 1;
            check(&o.mine() == &o.tsumo1, "own tsumo: mine() of seat 1 is not its tsumo" + at);
            randomButtons(rng, buttons);
            r.step(buttons, e);
            for (int i = 0; i < 2; ++i) {
                if (!e[i].spawned) continue;
                const Pair want = r.tsumoOf(i).pair(r.player[i].nextPair - 1);
                check(r.player[i].colors.axis == want.axis && r.player[i].colors.child == want.child, "own tsumo: a player did not get the pair of his tsumo" + at);
                ++own[i];
            }
        }
        check(own[0] >= 5 && own[1] >= 5, "the test of the tsumo of each player is not exercised: " + std::to_string(own[0]) + " and " + std::to_string(own[1]) + " pairs");
        Round one;
        one.start(rules, 11);
        const Observation shared = ai::observe(one, 1);
        check(!one.ownTsumo && !shared.ownTsumo && shared.tsumo1.count() == 0 && &shared.pairsOf(0) == &shared.tsumo && &shared.pairsOf(1) == &shared.tsumo &&
                  &shared.mine() == &shared.tsumo && &one.tsumoOf(1) == &one.tsumo,
              "one tsumo: the observation or the round names a second one");
    }
    std::cout << "levels: a tsumo for each player (measuring only): " << own[0] << " and " << own[1] << " pairs came from the tsumo of their player\n";
    std::cout << "levels: slow hands (handWait " << wait << ") put " << moves << " pairs of " << rounds << " rounds where the fastest input puts them, " << least << " to " << most
              << " frames later (" << full << " by nearly the whole wait; " << waited << " frames waited, " << extra << " frames longer; on a high field a wait of 200 ends after "
              << cut << " frames a pair); the late view (seeLate " << lag << ") over "
              << lagged << " late and " << present << " present frames, " << hidden << " with garbage of his hidden, " << noticed << " chains noticed (" << exact
              << " after the whole delay); X3 with either plays the same round twice\n";
}

// The terms of the evaluation for the base (ai/build.h baseCells, evCentre).
static void testBase() {
    const Rules rules;
    // The first form in the left corner: seven pieces of two colours. Mirrored it counts the same.
    static const int8_t A[4][2] = {{0, 0}, {1, 0}, {2, 1}, {1, 2}}, B[3][2] = {{0, 1}, {1, 1}, {0, 2}};
    for (int side = 0; side < 2; ++side) {
        GameField g;
        for (const auto& c : A) g.f.set(side ? W - 1 - c[0] : c[0], c[1], 1);
        for (const auto& c : B) g.f.set(side ? W - 1 - c[0] : c[0], c[1], 2);
        check(ai::baseCells(g.f) == 7, "the fold of seven is counted as " + std::to_string(ai::baseCells(g.f)));
        // one piece of the wrong colour on a cell of the fold: that form no longer fits (another may, with fewer pieces)
        g.f.set(side ? W - 1 : 0, 1, 3);
        check(ai::baseCells(g.f) < 7, "a fold with a wrong colour still counts in full");
    }
    check(ai::baseCells(GameField{}.f) == 0, "an empty field has pieces of a base");
    // A field high in the middle is worth less than the same columns with the high ones at the walls.
    ai::Params p;
    p.evCentre = 300;
    GameField middle, walls;
    const int hm[W] = {1, 1, 4, 4, 1, 1}, hw[W] = {4, 1, 1, 1, 1, 4};
    for (int x = 0; x < W; ++x) {
        for (int y = 0; y < hm[x]; ++y) middle.f.set(x, y, Cell(1 + (x + y) % 2 * 2 + x % 2));
        for (int y = 0; y < hw[x]; ++y) walls.f.set(x, y, Cell(1 + (x + y) % 2 * 2 + x % 2));
    }
    ai::Params off = p;
    off.evCentre = 0;
    const int lossMiddle = ai::buildValue(middle, rules, TM, off) - ai::buildValue(middle, rules, TM, p);
    const int lossWalls = ai::buildValue(walls, rules, TM, off) - ai::buildValue(walls, rules, TM, p);
    check(lossMiddle == 300 * 6 && lossWalls == 0, "evCentre takes " + std::to_string(lossMiddle) + " from a field high in the middle and " +
                                                       std::to_string(lossWalls) + " from one high at the walls");
    std::cout << "base: the fold in both corners, a wrong colour, high in the middle against high at the walls\n";
}

// The time a torn pair takes is charged to the way, and only to a pair that is torn (evTearFrame).
static void testTear() {
    const Rules rules;
    GameField g;
    for (int y = 0; y < 3; ++y) g.f.set(0, y, Cell(1 + y % 2)); // column 1 is three high
    const Pair pair{3, 4};
    ai::Params p;
    p.beamWidth = 100, p.beamDepth = 1, p.beamQueues = 1;
    p.evTearFrame = 0;
    ai::Builder builder;
    const ai::BuildResult off = builder.search(g, &pair, 1, rules, TM, p, 1);
    p.evTearFrame = 7;
    const ai::BuildResult on = builder.search(g, &pair, 1, rules, TM, p, 1);
    // lying on columns 1 and 2: the axis piece rests on row 4, the other falls three cells
    const int torn = placementId({0, RIGHT}), flat = placementId({2, RIGHT}), upright = placementId({0, UP});
    const int frames = TM.splitFrames(3, 0, 3);
    check(frames == 28, "a fall of three cells after the lock takes " + std::to_string(frames) + " frames");
    check(off.value[torn] - on.value[torn] == 7 * frames,
          "a torn pair costs " + std::to_string(off.value[torn] - on.value[torn]) + ", not " + std::to_string(7 * frames));
    check(off.value[flat] == on.value[flat] && off.value[upright] == on.value[upright], "a pair that is not torn costs time");
    std::cout << "time: a torn pair costs its extra frames, others nothing\n";
}

// Key pieces (ai/detect.h addKey): a chain that stops for want of one piece is carried on with it.
static void testKeys() {
    const Rules rules;
    ai::Params p;
    // Stairs of three waves without the piece of colour 1 on column 2 that makes the third wave.
    GameField g;
    stairs(g, 0, 3);
    check(g.f.get(1, 3) == 1, "the stairs are not what the test expects");
    g.f.set(1, 3, EMPTY);
    ai::Trials trials;
    BitField after[W * MAX_COLORS];
    ai::detect(g, rules, TM, 3, trials, after);
    int at = -1;
    for (int i = 0; i < trials.count; ++i)
        if (trials.item[i].x == 3 && trials.item[i].color == 3) at = i;
    check(at >= 0 && trials.item[at].chains == 2 && trials.best()->chains == 2, "the cut stairs are not a chain of two");
    int h[W];
    g.heights(h);
    ai::KeyPlan plan, cur;
    cur.t = trials.item[at];
    int runs = 8;
    ai::addKey(g.f, h, rules, TM, cur, after[at], runs, plan);
    // One piece of colour 1 on column 2 is the key. (On column 1 it would clear the three at once: not a key.)
    check(plan.key && plan.t.chains == 3 && plan.keyX == 1 && plan.keyColor == 1,
          "the key piece of the stairs: " + std::string(plan.key ? "found" : "none") + ", " + std::to_string(plan.t.chains) + " waves, column " +
              std::to_string(plan.keyX + 1) + ", colour " + std::to_string(plan.keyColor));
    check(runs < 8, "the trials run for the key piece were not counted");
    // The same by the other way: with that piece put on the field, the trial drop itself runs three waves.
    GameField whole = g;
    whole.f.set(1, 3, 1);
    ai::detect(whole, rules, TM, 3, trials);
    check(trials.best()->chains == 3 && trials.best()->points == plan.t.points, "the chain with the key piece is not the chain of the whole stairs");
    // The value: the third wave counts, less the price of the key piece. (Without the height of the
    // trigger, so that the gain is told by the waves and the key piece alone.)
    p.evFireHeight = 0, p.evKeyed = 0;
    ai::Params on = p;
    on.evKeyed = 400;
    const int gain = ai::buildValue(g, rules, TM, on) - ai::buildValue(g, rules, TM, p);
    check(gain == p.evChain - 400, "a chain one key piece short is worth " + std::to_string(gain) + " more, not " + std::to_string(p.evChain - 400));
    check(ai::buildValue(whole, rules, TM, on) == ai::buildValue(whole, rules, TM, p), "a chain that stands is worth something else with key pieces on");

    // The fold of a GTR with six pieces: the group below (colour 2) has three after the fold clears;
    // with a fourth it is a chain of two. Checked by running the chain with the key piece found.
    GameField fold;
    fold.f.set(0, 0, 2), fold.f.set(1, 0, 2), fold.f.set(0, 1, 1), fold.f.set(1, 1, 1), fold.f.set(0, 2, 1), fold.f.set(1, 2, 2);
    ai::detect(fold, rules, TM, 3, trials, after);
    fold.heights(h);
    int most = 0;
    for (int i = 0; i < trials.count; ++i) {
        check(trials.item[i].chains == 1, "the fold of six is more than chains of one");
        ai::KeyPlan found, start;
        start.t = trials.item[i];
        runs = 8;
        ai::addKey(fold.f, h, rules, TM, start, after[i], runs, found);
        if (!found.key) continue;
        ai::Trial t;
        BitField left;
        check(ai::runWithKey(fold.f, h, rules, TM, found, t, left) && t.chains == found.t.chains && t.points == found.t.points,
              "the key piece of the fold does not give its chain");
        // (the trigger is of one colour, the key piece of the other)
        check(found.keyColor != start.t.color, "the key piece of the fold has the colour of the trigger");
        most = std::max(most, t.chains);
    }
    check(most == 2, "the fold of six with a key piece is a chain of " + std::to_string(most));

    std::cout << "key pieces: the cut stairs, the fold of six, the value of a chain one piece short\n";
}

// The usual field (four clear, no group of four standing, only colours of the rules) goes a
// shorter way through detect and addKey (ai/detect.h). Whatever it gives must be what the plain
// way gives, which puts every piece on a copy of the field and looks: the trials and the fields
// after them, the key pieces of a field where a chain has stopped, the plans with key pieces, the
// value of the field, and a whole beam search. Fields the shorter way is not for (a group of four
// standing, another clear count, a colour the rules do not have) must take the plain way.
static bool sameTrial(const ai::Trial& a, const ai::Trial& b, bool frames = true) {
    return a.x == b.x && a.color == b.color && a.added == b.added && a.y == b.y && a.chains == b.chains && a.points == b.points && a.cleared == b.cleared &&
           (!frames || a.frames == b.frames);
}
static bool samePlan(const ai::KeyPlan& a, const ai::KeyPlan& b, bool frames = true) {
    if (a.key != b.key) return false;
    if (!a.key) return true; // (a plan that is not there)
    if (a.keyX != b.keyX || a.keyColor != b.keyColor) return false;
    return sameTrial(a.t, b.t, frames);
}
// A chain as the game runs it wave by wave: every wave is looked for, and gravity after every wave.
static ai::Trial chainByWaves(BitField& f, const Rules& rules) {
    ai::Trial t;
    Wave w;
    while (findWaveFor(f, w, rules.clearCount)) {
        ++t.chains;
        const FieldBits erased = w.cleared | w.garbage;
        t.frames += TM.waveFrames(longestFall(f, erased));
        t.points += gameWaveScore(w, t.chains);
        t.cleared += w.pieces;
        f.erase(erased);
        f.drop();
    }
    return t;
}
static void testQuickTrials() {
    struct Off { // the plain way for as long as this lives
        Off() { ai::quickTrials = false; }
        ~Off() { ai::quickTrials = true; }
    };
    long fields = 0, quiet = 0, trialsSeen = 0, stalls = 0, keysSeen = 0, plansSeen = 0, values = 0, chainsRun = 0;
    const auto compare = [&](const GameField& g, const Rules& rules, const std::string& at) {
        ++fields;
        int h[W];
        g.heights(h);
        for (int maxAdd = 0; maxAdd <= 4; ++maxAdd) {
            ai::Trials quick, plain, untimed;
            BitField afterQuick[W * MAX_COLORS], afterPlain[W * MAX_COLORS];
            ai::FirstWaves first;
            ai::detect(g, rules, TM, maxAdd, quick, afterQuick);
            ai::detectPlain(g, rules, TM, maxAdd, plain, afterPlain);
            ai::detect(g, rules, TM, maxAdd, untimed, nullptr, false, &first);
            check(quick.count == plain.count && untimed.count == plain.count,
                  "trial drops: " + std::to_string(quick.count) + " the shorter way, " + std::to_string(plain.count) + " the plain way" + at);
            for (int i = 0; i < plain.count; ++i) {
                check(sameTrial(quick.item[i], plain.item[i]) && afterQuick[i] == afterPlain[i], "a trial drop differs from the plain way" + at);
                check(sameTrial(untimed.item[i], plain.item[i], false) && untimed.item[i].frames == 0, "a trial drop without its frames differs" + at);
            }
            trialsSeen += plain.count;
            if (maxAdd != 3) continue;
            quiet += quick.quiet;
            const bool usual = rules.clearCount == 4 && !g.f.hasClear();
            check(!quick.quiet || usual, "a field that is not the usual one went the shorter way" + at);
            if (quick.quiet)
                for (int x = 0; x < W; ++x)
                    for (Cell c = 1; c <= MAX_COLORS; ++c) {
                        BitField f = g.f;
                        if (h[x] < 12) f.set(x, h[x], c);
                        check((quick.alone[x] >> c & 1) == (h[x] < 12 && f.hasClear()), "what a single piece clears is told wrongly" + at);
                    }
            for (int i = 0; i < plain.count; ++i) {
                if (rules.clearCount == 4) {
                    // where the chain has stopped: the key pieces, found both ways (and with less room for answers)
                    for (int most : {3 * W, 2, 1}) {
                        int8_t xa[3 * W], xb[3 * W];
                        Cell ca[3 * W], cb[3 * W];
                        const int a = ai::stallKeysQuiet(afterPlain[i], xa, ca, most), b = ai::stallKeys(afterPlain[i], rules, xb, cb, most);
                        bool same = a == b;
                        for (int k = 0; same && k < a; ++k) same = xa[k] == xb[k] && ca[k] == cb[k];
                        check(same, "the key pieces of a stopped chain differ from the plain way" + at);
                        if (most == 3 * W) ++stalls, keysSeen += a;
                    }
                }
                for (int allowed : {16, 5, 1}) {
                    ai::KeyPlan a, b, c, cur;
                    cur.t = plain.item[i];
                    int ra = allowed, rb = allowed, rc = allowed;
                    ai::addKey(g.f, h, rules, TM, cur, afterPlain[i], ra, a, &quick);
                    ai::addKey(g.f, h, rules, TM, cur, afterPlain[i], rb, b);
                    ai::addKey(g.f, h, rules, TM, cur, afterPlain[i], rc, c, &quick, false, untimed.quiet ? &first : nullptr, i);
                    check(ra == rb && rc == rb, "the trials run for key pieces differ in number" + at);
                    check(samePlan(a, b) && samePlan(c, b, false), "a plan with a key piece differs from the plain way" + at);
                    plansSeen += b.key;
                }
            }
        }
        // The value of the field with what the search sets, and the main chain it reports.
        ai::Params set[4];
        set[1].keyRuns = 40;
        set[2].evKeyed = 0, set[2].trialMaxAdd = 1;
        set[3].keyRuns = 3, set[3].evFire = 1000, set[3].evStand = 1000, set[3].trialMaxAdd = 2;
        for (const ai::Params& p : set) {
            int mainQuick = 0, mainPlain = 0;
            const int a = ai::buildValue(g, rules, TM, p, &mainQuick);
            Off off;
            check(a == ai::buildValue(g, rules, TM, p, &mainPlain) && mainQuick == mainPlain, "the value of a field differs from the plain way" + at);
            ++values;
        }
    };
    Rng rng(20261008, 77);
    // 1. Fields of random play, with garbage, of three to five colours; and the same with a pair
    // put that is not cleared yet, with columns up to the top, with another clear count, and
    // with fewer colours in the rules than on the field.
    for (int i = 0; i < 1500; ++i) {
        Rules rules;
        if (i % 3 == 1) rules.colors = 3 + int(rng.below(3));
        GameField g = randomField(rng, rules, int(rng.below(34)));
        for (int n = int(rng.below(4)); n > 0; --n) addGarbage(g, int(rng.below(W)), 1 + int(rng.below(3)));
        if (g.dead()) continue;
        const std::string at = " (random field " + std::to_string(i) + ")";
        compare(g, rules, at);
        if (const unsigned legal = legalPlacements(g); legal && i % 5 == 0) {
            // a pair put and left as it lies: sometimes a group of four stands
            GameField put = g;
            int id;
            do id = int(rng.below(PLACEMENTS));
            while (!(legal >> id & 1));
            const Cell colour = rules.palette[rng.below(uint64_t(rules.colors))];
            place(put, placement(id), Pair{colour, rng.below(2) ? colour : rules.palette[rng.below(uint64_t(rules.colors))]});
            compare(put, rules, at + ", a pair put");
            // ... and its chain, run with and without the frames, against the game's wave by wave
            BitField byWaves = put.f, one = put.f, two = put.f;
            const ai::Trial want = chainByWaves(byWaves, rules);
            ai::Trial timed, untimed;
            ai::runTrialChain(one, rules, TM, timed);
            ai::runTrialChain(two, rules, TM, untimed, false);
            check(sameTrial(timed, want) && sameTrial(untimed, want, false) && untimed.frames == 0 && one == byWaves && two == byWaves, "a chain differs from the game's" + at);
            chainsRun += want.chains > 0;
        }
        if (i % 7 == 0) {
            GameField tall = g;
            for (int n = 0; n < 2; ++n) {
                const int x = int(rng.below(W));
                int h[W];
                tall.heights(h);
                if (x != DEATH_X && h[x] < 13) addGarbage(tall, x, 12 + int(rng.below(2)) - h[x]);
            }
            compare(tall, rules, at + ", columns to the top");
        }
        if (i % 11 == 0) {
            Rules other = rules;
            other.clearCount = 2 + int(rng.below(5));
            compare(g, other, at + ", clear count " + std::to_string(other.clearCount));
        }
        if (i % 13 == 0 && rules.colors > 3) {
            Rules fewer = rules;
            fewer.colors = 3;
            compare(g, fewer, at + ", three colours in the rules");
        }
    }
    // 2. Fields the beam search builds: chains of many waves, cut by key pieces. Every field one
    // pair away from the fields of games of toko, and a search from some of them both ways.
    ai::Builder builder, other;
    ai::Params small;
    small.beamWidth = 24, small.beamDepth = 6, small.beamQueues = 2;
    int searches = 0;
    for (int game = 0; game < 6; ++game) {
        const Rules rules;
        GameField g;
        for (int move = 0; move < 34 && !g.dead(); ++move) {
            const Pair known[2] = {{rules.palette[rng.below(4)], rules.palette[rng.below(4)]}, {rules.palette[rng.below(4)], rules.palette[rng.below(4)]}};
            const uint64_t seed = rng.next();
            const ai::BuildResult r = builder.search(g, known, 2, rules, TM, small, seed, 1);
            if (r.best < 0) break;
            const std::string at = " (game " + std::to_string(game) + ", move " + std::to_string(move) + ")";
            if (move % 6 == 5) {
                Off off;
                const ai::BuildResult plain = other.search(g, known, 2, rules, TM, small, seed, 1);
                bool same = sameBuild(r, plain) && r.valued == plain.valued;
                for (int id = 0; id < PLACEMENTS; ++id) same = same && r.value[id] == plain.value[id];
                check(same, "a beam search differs from the plain way" + at);
                ++searches;
            }
            const unsigned legal = legalPlacements(g);
            for (int id = 0; id < PLACEMENTS; ++id) {
                if (!(legal >> id & 1) || (move + id) % 3) continue;
                GameField c = g;
                place(c, placement(id), known[0]);
                ai::Trial chain;
                if (c.f.hasClear()) ai::runTrialChain(c.f, rules, TM, chain);
                if (!c.dead()) compare(c, rules, at + ", placement " + std::to_string(id));
            }
            place(g, placement(r.best), known[0]);
            ai::Trial chain;
            if (g.f.hasClear()) ai::runTrialChain(g.f, rules, TM, chain);
        }
    }
    check(quiet > fields / 2 && quiet < fields && plansSeen > 1000 && keysSeen > 1000 && chainsRun > 20, "the fields of the test are not what it is for");
    std::cout << "the shorter way of trial drops and key pieces: " << fields << " fields (" << quiet << " the usual kind), " << trialsSeen << " trials, " << stalls
              << " stopped chains with " << keysSeen << " key pieces, " << plansSeen << " plans, " << values << " values and " << searches
              << " searches equal to the plain way; " << chainsRun << " chains equal to the game's\n";
}

// X3 asks whether the opponent can still answer a chain (outlasts). With x3OutlastSettled his field
// is the one he has when he next holds a pair, not one that shows a pair he has just put.
struct OutlastProbe : ai::X3Agent {
    using ai::X3Agent::X3Agent;
    bool ask(const ai::Observation& o, int id) {
        const uint64_t seed = 7;
        gaze.plans(o, o.me, true, p.x2Depth, p, seed, mine);
        gaze.plans(o, 1 - o.me, false, p.x2Depth, p, seed, theirs);
        return outlasts(o, id, o.rules.rateAt(o.frame), seed);
    }
};
static void testKnownExchange() {
    struct Probe : ai::X3Agent {
        using X3Agent::X3Agent;
        void read(const ai::Observation& o, int depth) {
            gaze.plans(o, o.me, true, depth, p, 7, mine);
            gaze.plans(o, 1 - o.me, false, depth, p, 7, theirs);
        }
        ai::Plan immediate(int minimum) const {
            for (const auto& s : mine.plans)
                if (s.fireDepth == 1 && !s.allClear && s.sent >= minimum) return s;
            throw std::runtime_error("test field has no eligible immediate chain");
        }
        using X3Agent::knownCounter;
        bool judged(const ai::Observation& o, int id) { return outlasts(o, id, o.rules.rateAt(o.frame), 7); }
        int protect(const ai::Observation& o, int hold, const ai::Plan& fire) {
            return protectAnswer(o, hold, (1u << hold) | (1u << fire.id[0]), 7, fire);
        }
        int answerFor(const ai::Observation& o, int hold, int fire) {
            const unsigned legal = (1u << hold) | (1u << fire);
            std::erase_if(mine.plans, [&](const ai::Plan& s) { return !(legal >> s.id[0] & 1); });
            const ai::Duel d(o, 7);
            return answer(o, legal, o.rules.rateAt(o.frame), 7, d, false, o.self().incoming.total());
        }
    };
    ai::Params baseline;
    check(!baseline.checkKnownExchange && !ai::levelParams(baseline, 35).checkKnownExchange &&
          !ai::levelParams(baseline, 36).checkKnownExchange && ai::levelParams(baseline, 37).checkKnownExchange,
          "verified exchanges are enabled only by explicit Lv37");
    Rules rules;
    rules.targetPoints = 10;
    for (int i = 0; i < rules.marginSteps; ++i) rules.marginRates[size_t(i)] = 10;
    baseline.x3OutlastScale = 0; // Isolate the case of a beam estimate that missed his reply.
    baseline.x3OutlastSettled = 0;
    baseline.beamDepth = 1, baseline.beamWidth = 2, baseline.beamQueues = 1;
    GameField equal;
    stairs(equal, 0, 3);
    equal.f.set(5, 0, 4); // A chain remains an ordinary fire even when the stairs clear.
    const Pair key{4, 3};
    Round match = situation(rules, equal, equal, key);
    const auto o = ai::observe(match, 0);
    Probe legacy(baseline), checked(ai::levelParams(baseline, 37));
    legacy.read(o, 1), checked.read(o, 1);
    const ai::Plan attack = checked.immediate(72);
    check(checked.knownCounter(o, attack, 7) && legacy.judged(o, attack.id[0]) && !checked.judged(o, attack.id[0]),
          "a known counter cancels this exact main chain despite an optimistic beam estimate");
    match.player[1].field = {};
    const auto undefended = ai::observe(match, 0);
    checked.read(undefended, 1);
    check(!checked.knownCounter(undefended, attack, 7) && checked.judged(undefended, attack.id[0]),
          "without the concrete counter the estimate retains its result");

    // A holding move takes one whole row over the three reds on the floor.
    // Neither colour of the following pair can then start a chain. A red in
    // the fourth column now starts one and offsets all six pieces.
    rules.targetPoints = 1;
    for (int i = 0; i < rules.marginSteps; ++i) rules.marginRates[size_t(i)] = 1;
    GameField reds;
    for (int x = 0; x < 3; ++x) reds.f.set(x, 0, 1);
    Round defence = situation(rules, reds, GameField{}, Pair{1, 2});
    const int hold = placementId({5, UP});
    baseline.x3AnswerShape = 0;
    baseline.x3Fall = baseline.x3Main = baseline.x3Surplus = baseline.x3Danger = baseline.x3Close = 0;
    baseline.x3Crowd = baseline.x3Crowd2 = baseline.x3Crowd3 = baseline.x3Crowd4 = 0;
    baseline.x3Used = 1; // Isolate a score that would otherwise prefer the non-firing move.
    Probe waiting(baseline), saving(ai::levelParams(baseline, 37));
    auto obs = [&](int confirmed) {
        defence.player[0].incoming.confirmed = confirmed;
        return ai::observe(defence, 0);
    };
    const auto row = obs(W);
    waiting.read(row, 2), saving.read(row, 2);
    const ai::Plan counter = saving.immediate(0);
    check(saving.protect(row, hold, counter) == counter.id[0] &&
          waiting.answerFor(row, hold, counter.id[0]) == hold && saving.answerFor(row, hold, counter.id[0]) == counter.id[0],
          "fire the known offset before a complete row buries the available answer");
    for (int incoming : {W - 1, W + 1}) {
        auto partial = obs(incoming);
        partial.nextGarbageOrder[0].reset();
        saving.read(partial, 2);
        check(saving.protect(partial, hold, counter) == hold, "a partial or undetermined fall must not force a fire");
    }
    saving.read(row, 1);
    check(saving.protect(row, hold, counter) == hold, "a missing next pair does not prove a lost firing opportunity");
    for (int y = 1; y <= 3; ++y) defence.player[0].field.f.set(0, y, 2);
    const auto sideways = obs(W);
    saving.read(sideways, 2);
    check(saving.protect(sideways, hold, saving.immediate(0)) == hold,
          "the next blue can still join a tall column from the side after a complete row");
    // Other clear counts: whether the next pair can still fire after the row is judged by the clear count of the rules.
    const auto protectWith = [&](int clearCount, const GameField& field) {
        Rules other = rules;
        other.clearCount = clearCount;
        Round round = situation(other, field, GameField{}, Pair{1, 2});
        round.player[0].incoming.confirmed = W;
        const auto at = ai::observe(round, 0);
        Probe probe(ai::levelParams(baseline, 37));
        probe.read(at, 2);
        const ai::Plan fire = probe.immediate(0);
        return std::pair{probe.protect(at, hold, fire), int(fire.id[0])};
    };
    // Three: two reds on the floor fire with a third; over them two blues in the first column. After the row the
    // next blue makes three with them from the side (a clear of three), never four.
    GameField threes;
    for (int x = 0; x < 2; ++x) threes.f.set(x, 0, 1);
    for (int y = 1; y <= 2; ++y) threes.f.set(0, y, 2);
    const auto [threeHeld, threeFire] = protectWith(3, threes);
    check(threeFire != hold && threeHeld == hold, "a clear of three: the next blue can still fire three after a complete row");
    // Five: four reds on the floor fire with a fifth; over them three blues in the first column. After the row the
    // next blue makes four with them, which do not clear.
    GameField fives;
    for (int x = 0; x < 4; ++x) fives.f.set(x, 0, 1);
    for (int y = 1; y <= 3; ++y) fives.f.set(0, y, 2);
    const auto [fiveHeld, fiveFire] = protectWith(5, fives);
    check(fiveFire != hold && fiveHeld == fiveFire, "a clear of five: four blues are no answer, the known offset is fired before the row");
}

// ai::duelValue: a round the Duel has ended counts as it ended, whatever the fields hold - a winner whose pair still
// fills the third column of the twelfth row, waiting for its clear, has won.
static void testDuelValue() {
    const ai::Observation o;
    const ai::Params p;
    const int rate = o.rules.rateAt(0);
    ai::Duel d(o, 0);
    d.player[0].field.f.set(DEATH_X, DEATH_Y, Cell(1));
    d.player[1].field.f.set(DEATH_X, DEATH_Y, Cell(2));
    d.state = RoundState::Won0;
    check(ai::positionValue(d.player[0], d.player[1], o.rules, o.timing, p, rate, 7) == 0 &&
              ai::duelValue(d, 0, o, p, rate, 7) == -ai::VALUE_LOST && ai::duelValue(d, 1, o, p, rate, 7) == ai::VALUE_LOST,
          "the seat whose opponent died first has won, although its own pair still fills the death cell");
    d.state = RoundState::Won1;
    check(ai::duelValue(d, 0, o, p, rate, 7) == ai::VALUE_LOST && ai::duelValue(d, 1, o, p, rate, 7) == -ai::VALUE_LOST,
          "the seat that died first has lost");
    d.state = RoundState::Draw;
    check(ai::duelValue(d, 0, o, p, rate, 7) == 0 && ai::duelValue(d, 1, o, p, rate, 7) == 0, "both dead in one frame: a draw");
    d.state = RoundState::Playing;
    d.player[1].field = GameField{};
    check(ai::duelValue(d, 0, o, p, rate, 7) == ai::positionValue(d.player[0], d.player[1], o.rules, o.timing, p, rate, 7),
          "a round still played is valued as before");
}

// X3Agent::offsetsIn: the reply disproves an immediate fire when it leaves none of it above him - both playing on,
// garbage of his on the own field or not - or when he wins; not when he is left with some of it, nor in a draw, nor
// when his reply was not played.
static void testOffsetsIn() {
    struct Probe : ai::X3Agent {
        using X3Agent::offsetsIn;
    };
    for (int me = 0; me < 2; ++me) {
        ai::Observation o;
        o.me = me;
        o.player[me].moves = 10;
        o.player[1 - me].moves = 20;
        ai::Plan reply;
        reply.id[reply.count++] = 0;
        reply.fireDepth = 1;
        reply.chains = 6;
        ai::Duel d(o, 0);
        ai::DuelPlayer& a = d.player[me];
        ai::DuelPlayer& b = d.player[1 - me];
        a.moves = 11, a.phase = ai::DuelPhase::Control, a.chain.chains = 5;
        b.moves = 21, b.phase = ai::DuelPhase::Control, b.chain.chains = 6;
        const std::string at = " (seat " + std::to_string(me) + ")";
        check(Probe::offsetsIn(o, d, reply), "a full offset with both playing on" + at);
        a.fallen = 6;
        check(Probe::offsetsIn(o, d, reply), "a full offset that sends garbage onto the own field" + at);
        ai::Duel won = d;
        won.state = me ? RoundState::Won0 : RoundState::Won1;
        won.player[me].phase = ai::DuelPhase::Dead;
        check(Probe::offsetsIn(o, won, reply), "his reply wins" + at);
        ai::Duel drawn = won;
        drawn.state = RoundState::Draw;
        drawn.player[1 - me].phase = ai::DuelPhase::Dead;
        check(!Probe::offsetsIn(o, drawn, reply), "a draw is no offset" + at);
        ai::Duel left = d;
        left.player[1 - me].incoming.confirmed = 3;
        check(!Probe::offsetsIn(o, left, reply), "some of the fire left above him" + at);
        ai::Duel unplayed = won;
        unplayed.player[1 - me].moves = 20;
        check(!Probe::offsetsIn(o, unplayed, reply), "his reply was not played" + at);
    }
}

// X3Agent::refutation and keepsWin, the recheck of a win against the replies the midgame beam adds: the clock stopping
// before the last reply leaves the reading Unfinished, not None; a candidate taken again after a refuted one stands only
// on a complete reading, the first one as before unless a reply offsets it.
static void testWinRecheckUnfinished() {
    struct Probe : ai::X3Agent {
        using X3Agent::X3Agent;
        using X3Agent::Refutation;
        using X3Agent::refutation;
        using X3Agent::keepsWin;
        using X3Agent::withBeam;
        void replies(int n) { theirs.tactical.assign(size_t(n), ai::Plan{}); }
    };
    using Refutation = Probe::Refutation;
    Probe probe{ai::Params{}};
    probe.replies(3); // (none of them a chain of five or more: none offsets the fire)
    const ai::Observation o;
    ai::Plan fire;
    fire.id[fire.count++] = 0;
    fire.fireDepth = 1, fire.chains = 6, fire.sent = 30;
    int asked = 0;
    const auto stopAtThird = [&] { return ++asked >= 3; };
    check(probe.refutation(o, fire, 0, 7, [] { return false; }) == Refutation::None, "every reply read, none offsets the fire");
    check(probe.refutation(o, fire, 0, 7, stopAtThird) == Refutation::Unfinished && asked == 3,
          "the clock stopping before the last reply leaves the reading unfinished");
    check(probe.refutation(o, fire, 3, 7, [] { return true; }) == Refutation::None, "no reply left to read");
    check(Probe::keepsWin(Refutation::None, false) && Probe::keepsWin(Refutation::None, true) &&
              !Probe::keepsWin(Refutation::Found, false) && !Probe::keepsWin(Refutation::Found, true) &&
              Probe::keepsWin(Refutation::Unfinished, false) && !Probe::keepsWin(Refutation::Unfinished, true),
          "a candidate taken again stands only when every reply was read");
    check(Probe::withBeam(Refutation::None, false) == Refutation::Unfinished && Probe::withBeam(Refutation::None, true) == Refutation::None &&
              Probe::withBeam(Refutation::Found, false) == Refutation::Found &&
              !Probe::keepsWin(Probe::withBeam(Refutation::None, false), true),
          "no reply refuting it among replies the midgame beam made in part is no complete reading");
}

// MidgameBeam::extend tells whether it made every layer: false when the clock stops it, true when there is nothing to
// make or it went to its end.
static void testBeamCut() {
    const ai::Observation o;
    const ai::Params p;
    ai::PlanSet set;
    set.knownCount = 3;
    check(ai::MidgameBeam::extend(o, p, 7, set, [] { return true; }), "no exhaustive plans: nothing to make, complete");
    set.pairCount = 1;
    check(!ai::MidgameBeam::extend(o, p, 7, set, [] { return true; }), "the clock stopping the beam: not complete");
}

// ai::chainRunning on the observation of a Round: a single clear by player 0 runs while its wave is played; from the end
// of the chain to the next lock Player::chain keeps its wave, and the chain counts as over.
static void testChainRunning() {
    Rules rules;
    Round r;
    r.start(rules, 5);
    Player& p = r.player[0];
    const Pair pair = r.tsumo.pair(0);
    for (int x = 0; x < 3; ++x) p.field.f.set(x, 0, Cell(pair.axis));
    PlayerEvents e[2];
    const uint8_t none[2] = {0, 0};
    while (!aboutToSpawn(p)) r.step(none, e);
    const Placement target{3, UP};
    ai::InputPlanner planner;
    ai::InputPath path;
    check(planner.plan(p.field, spawnPair(r.timing), target, r.timing, rules.speedPercent, path), "no way to the fourth column");
    bool wave = false, overInHand = false;
    for (int frame = 0; frame < 2000 && !overInHand && r.state == RoundState::Playing; ++frame) {
        const uint8_t buttons[2] = {frame < path.frames() ? path.buttons[size_t(frame)] : uint8_t(0), 0};
        r.step(buttons, e);
        const auto o = ai::observe(r, 1);
        check(ai::chainRunning(o, 0) == (p.phase == Phase::Wave),
              "the chain runs exactly while its wave is played (frame " + std::to_string(frame) + ")");
        wave |= p.phase == Phase::Wave;
        overInHand = wave && p.phase == Phase::Control && p.chain.chains > 0;
    }
    check(wave && overInHand, "the next pair is in hand with the wave of the chain that is over");
}

static void testExchangePoke() {
    struct Probe : ai::X3Agent {
        using X3Agent::X3Agent;

        int exchange(const ai::Observation& o, unsigned legal, uint64_t seed, bool expired = false, bool whole = true) {
            rivalBeamWhole = whole;
            gaze.plans(o, o.me, true, p.x2Depth, p, seed, mine);
            gaze.plans(o, 1 - o.me, false, p.x2Depth, p, seed, theirs);
            ai::MidgameBeam::extend(o, p, seed, mine, [] { return false; });
            ai::MidgameBeam::extend(o, p, seed, theirs, [] { return false; });
            if (expired) {
                budgetNow = p.thinkBudget;
                began = ai::Builder::Clock::now() - std::chrono::milliseconds(budgetNow);
            }
            return exchangePoke(o, legal, o.rules.rateAt(o.frame), seed);
        }
    };
    auto p = ai::agentParams("lv37", {});
    check(p.pokeExchange == 2 && !ai::agentParams("lv36", {}).pokeExchange && !ai::agentParams("x3", {}).pokeExchange,
          "the exchange poke is enabled by explicit Lv37");
    p.beamWidth = 250;
    p.beamDepth = 16;
    p.beamQueues = 8;
    p.beamFixedQueues = 4;
    p.searchThreads = 1;
    p.thinkBudget = 0;

    Round round;
    Rules rules;
    rules.cancel = false;
    rules.colors = 4;
    round.start(rules, 1);
    auto o = ai::observe(round, 1);
    o.frame = 1173;
    o.ownTsumo = true;
    const auto board = [](std::string_view rows, GameField& field) {
        int y = ROW_14, x = 0;
        for (const char c : rows) {
            if (c == '/') { --y; x = 0; continue; }
            const Cell cell = Cell(c - '0');
            if (y == ROW_14) field.row14[size_t(x)] = cell;
            else field.f.set(x, y, cell);
            ++x;
        }
    };
    board("000000/200000/400000/100001/100004/400023/110043/130043/420022/413032/434414/141142/433414/432244", o.player[1].field);
    board("000000/000000/000000/000004/000004/000004/330012/140443/142311/311331/224122/113442/131411/243331", o.player[0].field);
    o.player[1].phase = Phase::Next;
    o.player[1].wait = 1;
    o.player[1].moves = 25;
    o.player[1].scoring.pool = 224;
    o.player[0].phase = Phase::Control;
    o.player[0].wait = 0;
    o.player[0].moves = 24;
    o.player[0].scoring.pool = 228;
    o.player[0].colors = {2, 2};
    o.player[0].pair.x = 1;
    o.player[0].pair.y = 10;
    o.tsumo.pairs = {{4, 3}, {1, 2}};
    o.tsumo1.pairs = {{1, 2}, {3, 3}, {3, 4}};

    constexpr uint64_t seed = 8551104070121336641ull + 25;
    constexpr unsigned route = 1u << 3;
    Probe unfinished(p);
    check(unfinished.exchange(o, route, seed, false, false) < 0 && !unfinished.exchangeNote.chosen,
          "do not take an exchange poke from an unfinished rival search");
    Probe safe(p);
    check(safe.exchange(o, route, seed) == 3 && safe.exchangeNote.chosen && safe.exchangeNote.depth == 1,
          "the side chain is fired when the exchange leaves enough for the main chain");
    // The rival in hand with the waves of a chain that is over (Player::chain until his next lock): the same choice.
    auto stale = o;
    stale.player[0].chain.chains = 1;
    Probe after(p);
    check(!ai::chainRunning(stale, 0) && after.exchange(stale, route, seed) == 3 && after.exchangeNote.chosen &&
              after.exchangeNote.depth == 1,
          "the waves of a chain that is over neither stop nor change the exchange poke");
    auto running = o;
    running.player[0].phase = Phase::Wave;
    running.player[0].wait = 10;
    running.player[0].chain.chains = 1;
    Probe waits(p);
    check(ai::chainRunning(running, 0) && waits.exchange(running, route, seed) < 0, "a chain still running stops the exchange poke");

    auto mirror = o;
    mirror.player[0] = mirror.player[1];
    mirror.tsumo = mirror.tsumo1;
    auto readsPartial = p;
    readsPartial.x3PokeKeep = 1;
    Probe defended(readsPartial);
    check(defended.exchange(mirror, route, seed) < 0 && !defended.exchangeNote.chosen,
          "the same side chain is declined when the opponent can offset it");
    check(defended.exchangeCounts.smallReply > 0 || defended.exchangeCounts.chance > 0,
          "the side chain was refused by the small reply reading");

    p.thinkBudget = 180;
    Probe expired(p);
    check(expired.exchange(o, route, seed, true) < 0 && !expired.exchangeNote.chosen,
          "an expired exchange does not commit the side chain");
}

static void testMidgamePressure() {
    struct Probe : ai::X3Agent {
        using X3Agent::X3Agent;
        unsigned fires(const ai::Observation& o, int waves = 2) {
            gaze.plans(o, o.me, true, 1, p, 7, mine);
            gaze.plans(o, 1 - o.me, false, 1, p, 7, theirs);
            unsigned fire = 0;
            for (const ai::Plan& s : mine.plans)
                if (s.chains == waves && !s.allClear && s.sent >= 2 * W) fire |= 1u << s.id[0];
            return fire;
        }
        bool extraReply() const { return !theirs.tactical.empty(); }
        int pressure(const ai::Observation& o, unsigned legal) { return midgamePressure(o, legal, 7); }
    };
    const ai::Params legacy;
    ai::Params midgame = ai::levelParams(legacy, 37);
    check(!legacy.readMidgame && !ai::levelParams(legacy, 36).readMidgame && midgame.readMidgame,
          "only explicit Lv37 enables the midgame reading");
    // Keep the optimistic turn estimate fixed while checking actual input and garbage timing.
    midgame.x3MoveFrames = midgame.x3MoveFramesMid = midgame.x3MoveFramesLate = 200;

    Rules rules;
    rules.targetPoints = 20;
    for (int i = 0; i < rules.marginSteps; ++i) rules.marginRates[size_t(i)] = 20;
    GameField mine, crowded;
    stairs(mine, 0, 2);
    mine.f.set(5, 0, Cell(3));
    for (int x = 0; x < W; ++x)
        for (int y = 0; y < 9; ++y) crowded.f.set(x, y, GARBAGE);
    Round match = situation(rules, mine, crowded, Pair{4, 2});
    auto open = ai::observe(match, 0);
    Probe newAgent(midgame), oldAgent(legacy);
    const unsigned early = newAgent.fires(open);
    check(early && newAgent.pressure(open, early) < 0, "a fourth rival pair can offset the attack before it lands");
    // With the later rival spawn the same attack can land within the visible window.
    open.player[1].wait = 94;
    const unsigned fire = newAgent.fires(open);
    const int hit = newAgent.pressure(open, fire);
    check(hit >= 0 && (fire >> hit & 1), "Lv37 fires a confirmed two-row attack into an undefended crowded field");

    GameField counter;
    for (int x = 0; x < W; ++x)
        for (int y = 0; y < (x >= 3 ? 9 : 6); ++y) counter.f.set(x, y, Cell(3 + (x + y) % 2));
    for (int x = 0; x < 2; ++x)
        for (int y = 0; y < 4; ++y)
            if (const Cell color = mine.f.get(x, y)) counter.f.set(x, y + 6, color);
    match.player[1].field = counter;
    const auto defended = ai::observe(match, 0);
    oldAgent.fires(defended);
    newAgent.fires(defended);
    check(!oldAgent.extraReply() && newAgent.extraReply(), "the small-reply slot keeps alternative first placements for Lv37");
    check(newAgent.pressure(defended, 1u << hit) < 0, "the same attack is rejected when the opponent can offset it");

    GameField doubleGroup;
    for (int x : {1, 3}) for (int y = 0; y < 3; ++y) doubleGroup.f.set(x, y, Cell(2));
    doubleGroup.f.set(5, 0, Cell(1));
    auto single = ai::observe(situation(rules, doubleGroup, crowded, Pair{2, 3}), 0);
    single.player[1].wait = open.player[1].wait;
    const unsigned shortFire = newAgent.fires(single, 1);
    const int singleHit = newAgent.pressure(single, shortFire);
    check(singleHit >= 0 && (shortFire >> singleHit & 1), "a fast single can crush when it sends a full two rows");
}

static void testMidgameBeam() {
    Rules rules;
    rules.targetPoints = 20;
    for (int i = 0; i < rules.marginSteps; ++i) rules.marginRates[size_t(i)] = 20;
    GameField mine;
    stairs(mine, 0, 2);
    mine.f.set(5, 0, Cell(3));
    const auto match = situation(rules, mine, GameField{}, Pair{4, 3});
    auto o = ai::observe(match, 0);
    o.tsumo.pairs = {{4, 3}, {4, 3}, {4, 3}, {4, 2}};
    const ai::Params p = ai::levelParams(ai::Params{}, 37);
    ai::Gaze gaze;
    ai::PlanSet own, rival;
    gaze.plans(o, 0, true, 3, p, 7, own);
    gaze.plans(o, 1, false, 3, p, 7, rival);
    const auto later = [](const ai::Plan& s) { return s.fireDepth == 4 && s.chains == 2 && s.sent >= 2 * W; };
    ai::MidgameBeam::extend(o, p, 7, own, [] { return false; });
    const auto target = std::find_if(own.tactical.begin(), own.tactical.end(), later);
    check(target != own.tactical.end(), "the shorter beam retains an attack with the fourth observed pair");
    const ai::Duel exchange = ai::playOut(o, own, &*target, rival, nullptr, p, 7, true);
    check(exchange.player[0].moves == o.self().moves + 4 && exchange.player[0].chain.chains == 2 &&
          exchange.player[1].incoming.total() >= 2 * W, "the attack found by the beam works with concrete input paths");

    o.tsumo.pairs.resize(3);
    gaze.plans(o, 0, true, 3, p, 7, own);
    ai::MidgameBeam::extend(o, p, 7, own, [] { return false; });
    check(std::none_of(own.tactical.begin(), own.tactical.end(), later), "an unseen fourth pair cannot create an attack");

    // This stack keeps the second pair above the trigger. A weak single with the
    // first pair makes the second pair fire, although the exhaustive tree stops
    // at the single. Both pairs are already visible.
    for (int y = 0; y < 3; ++y) o.player[0].field.f.set(2, y, Cell(3));
    o.tsumo.pairs = {{4, 3}, {4, 2}};
    gaze.plans(o, 0, true, 3, p, 7, own);
    gaze.plans(o, 1, false, 3, p, 7, rival);
    const auto reshape = [](const ai::Plan& s) { return s.fireDepth == 2 && s.chains == 2 && s.sent >= 2 * W; };
    check(std::none_of(own.plans.begin(), own.plans.end(), reshape), "the normal gaze stops at the earlier single");
    ai::MidgameBeam::extend(o, p, 7, own, [] { return false; });
    const auto cleared = std::find_if(own.tactical.begin(), own.tactical.end(), reshape);
    check(cleared != own.tactical.end(), "the extra beam can clear a blocking single and fire the follow-up");
    const ai::Duel shaped = ai::playOut(o, own, &*cleared, rival, nullptr, p, 7, true);
    check(shaped.player[0].moves == o.self().moves + 2 && shaped.player[0].chain.chains == 2,
          "reshaping and the following fire both have concrete input paths");
}

static void testMidgamePoke() {
    struct Probe : ai::X3Agent {
        using X3Agent::X3Agent;
        bool readsMainReply(const ai::Observation& o) {
            gaze.plans(o, 0, true, 1, p, 7, mine);
            gaze.plans(o, 1, false, 1, p, 7, theirs);
            for (const ai::Plan& fire : mine.plans) {
                if (fire.chains != 2 || fire.sent != 2 * W) continue;
                const ai::Duel quiet = ai::playOut(o, mine, &fire, theirs, nullptr, p, 7, true);
                for (const auto* list : {&theirs.plans, &theirs.tactical}) for (const ai::Plan& reply : *list)
                    if (reply.chains == 3 && reply.points == 1180 && !pressureFollowUp(o, fire, quiet, reply, false, 7) &&
                        pressureFollowUp(o, fire, quiet, reply, true, 7)) return true;
            }
            return false;
        }
    };
    ai::Observation o;
    o.rules.colors = 5;
    o.rules.targetPoints = 30;
    o.rules.softDropBonus = false;
    o.ownTsumo = true;
    o.tsumo.pairs = {{1, 2}, {5, 1}, {3, 4}};
    o.tsumo1.pairs = {{3, 5}, {4, 2}, {3, 5}};
    o.player[0].phase = o.player[1].phase = Phase::Next;
    o.player[0].wait = 1;
    o.player[1].wait = 100;
    stairs(o.player[0].field, 0, 2);
    for (int k = 0; k < 3; ++k) {
        for (int y = 0; y < 3; ++y) o.player[0].field.f.set(5 - k, y, Cell(3 + k));
        if (k) o.player[0].field.f.set(5 - k, 3, Cell(2 + k));
    }
    stairs(o.player[1].field, 0, 3);
    o.player[1].field.f.set(2, 4, Cell(2));
    o.player[1].field.f.set(5, 0, Cell(5));
    Probe agent(ai::levelParams(ai::Params{}, 37));
    check(agent.readsMainReply(o), "forcing the larger reply can leave one row after a verified second fire");
}

static void testMidgameShelter() {
    ai::Observation o;
    o.rules.colors = 5;
    o.rules.targetPoints = 20;
    o.ownTsumo = true;
    o.tsumo.pairs = {{3, 4}, {4, 2}, {4, 5}};
    o.tsumo1.pairs = {{2, 3}, {3, 5}, {4, 5}};
    o.player[0].phase = o.player[1].phase = Phase::Next;
    o.player[0].wait = 130;
    o.player[1].wait = 1;
    GameField& own = o.player[0].field;
    GameField& rival = o.player[1].field;
    for (int x = 0; x < W; ++x)
        for (int y = 0; y < 4; ++y) own.f.set(x, y, Cell((x < 3 ? 3 : 1) + (x + y) % 2));
    GameField left;
    stairs(left, 0, 2);
    stairs(rival, 0, 2);
    rival.f.set(5, 0, Cell(1));
    for (int x = 0; x < 2; ++x) for (int y = 0; y < 4; ++y)
        if (const Cell c = left.f.get(x, y)) own.f.set(x, y + 4, c);
    for (int k = 0; k < 3; ++k) {
        for (int y = 4; y < 7; ++y) own.f.set(5 - k, y, Cell(3 + k));
        if (k) own.f.set(5 - k, 7, Cell(2 + k));
    }
    const ai::Params p = ai::levelParams(ai::Params{}, 37);
    ai::Gaze gaze;
    ai::PlanSet a, b;
    gaze.plans(o, 1, false, 3, p, 7, b);
    gaze.plans(o, 0, true, 3, p, 7, a);
    const unsigned legal = legalPlacements(own);
    const auto shelter = ai::findMidgameShelter(o, a, b, legal, p, 7, [] { return false; });
    check((shelter.safe & (1u << 2)) && (shelter.buried & (1u << 7)),
          "a quiet first move can leave an offset where another move takes two rows");
    o.tsumo.pairs.resize(1);
    gaze.plans(o, 0, true, 3, p, 7, a);
    const auto unseen = ai::findMidgameShelter(o, a, b, legal, p, 7, [] { return false; });
    check(!unseen.safe && !unseen.buried, "an unknown key pair cannot justify a defensive replacement");
}

static void testOutlast() {
    Rules rules;
    rules.targetPoints = 40;
    for (int i = 0; i < rules.marginSteps; ++i) rules.marginRates[size_t(i)] = 40;
    ai::Params p;
    p.beamWidth = 30, p.beamDepth = 8, p.beamQueues = 3;
    // (the positions below are made for these values of the judging of a poke: the default waits for 60 cells on the
    //  own field, and these fields are small. The default values are tested where a test sets them.)
    p.x3PokeFrom = 0, p.x3PokeNet = 0, p.x3KeepMain = 85, p.x3PokeGain = 75, p.xWildChance = 0, p.xWildEvery = 2;
    // Mine: three of colour 4 in column 6; the pair clears them (a clear of five, two garbage pieces).
    GameField mine;
    for (int y = 0; y < 3; ++y) mine.f.set(5, y, 4);
    const Pair pair{4, 4};
    const int fire = placementId({5, UP});
    // His: the third column eight high, and on it four of one colour that he has just completed.
    // They are still on the field (his pair is landing), so the column shows twelve.
    GameField his;
    for (int y = 0; y < 8; ++y) his.f.set(2, y, Cell(1 + y % 2));
    for (int y = 8; y < 12; ++y) his.f.set(2, y, 3);
    check(his.dead() && his.f.hasClear(), "the test field is not what the test expects");
    Round r = situation(rules, mine, his, pair);
    r.player[1].phase = Phase::Land, r.player[1].wait = 3;
    const ai::Observation o = ai::observe(r, 0);
    p.x3OutlastSettled = 0;
    ai::Params settled = p;
    settled.x3OutlastSettled = 1;
    OutlastProbe before(p), after(settled);
    before.startRound(0, 7), after.startRound(0, 7);
    check(before.ask(o, fire), "without the switch the unsettled field is no longer taken for a closed one (the test is out of date)");
    check(!after.ask(o, fire), "two garbage pieces are taken to bury a field whose third column is eight high once the four have cleared");
    // A field that is settled and really closed needs nothing more: both say so.
    GameField closed;
    for (int y = 0; y < 11; ++y) closed.f.set(2, y, Cell(1 + y % 2));
    Round s = situation(rules, mine, closed, pair);
    const ai::Observation q = ai::observe(s, 0);
    OutlastProbe c(p), d(settled);
    c.startRound(0, 7), d.startRound(0, 7);
    check(c.ask(q, fire) == d.ask(q, fire), "a settled field is judged differently with the switch");
    // x3OutlastScale: what the search of his field finds counts for more or less. A chain that outlasts his answer
    // taken larger also outlasts it taken smaller (on both fields of this test).
    for (const ai::Observation* at : {&o, &q}) {
        bool last = true;
        for (int scale : {0, 100, 300, 10000}) {
            ai::Params w = settled;
            w.x3OutlastScale = scale;
            OutlastProbe e(w);
            e.startRound(0, 7);
            const bool holds = e.ask(*at, fire);
            check(last || !holds, "x3OutlastScale: a chain that does not outlast a smaller answer outlasts a larger one (scale " + std::to_string(scale) + ")");
            last = holds;
        }
    }
    // The points he carries when he next holds a pair: the four of colour 3 that are clearing make 40 points, no
    // garbage at a rate of 70, and those 40 points count in his answer with what he carried before. x3OutlastScale 0
    // leaves the carried points alone in the answer, which is divided here by a rate of the test's choosing. (He has
    // the one pair he holds when the own chain is confirmed; two more of colour 4 stand on his first column, so that
    // pair clears four: carried points count in an answer only when a chain is fired.)
    {
        struct CarryProbe : OutlastProbe {
            using OutlastProbe::OutlastProbe;
            int reply(const ai::Observation& o, int id, int rate) {
                gaze.plans(o, o.me, true, p.x2Depth, p, 7, mine);
                gaze.plans(o, 1 - o.me, false, p.x2Depth, p, 7, theirs);
                Rival rival;
                int garbage = -1;
                outlasts(o, id, rate, 7, rival, nullptr, &garbage);
                return garbage;
            }
        };
        Rules slow = rules;
        slow.targetPoints = 70;
        for (int i = 0; i < slow.marginSteps; ++i) slow.marginRates[size_t(i)] = 70;
        ai::Params carried = settled;
        carried.x3OutlastScale = 0;
        GameField answering = his;
        answering.f.set(0, 0, 4), answering.f.set(0, 1, 4);
        const auto replyAt = [&](int before, int rate) {
            Round t = situation(slow, mine, answering, pair);
            t.player[1].phase = Phase::Land, t.player[1].wait = 3;
            t.player[1].scoring.pool = before;
            const ai::Observation at = ai::observe(t, 0);
            CarryProbe e(carried);
            e.startRound(0, 7);
            return e.reply(at, fire, rate);
        };
        check(replyAt(0, 40) == 1, "40 points of his running chain, carried: one garbage piece of answer at a rate of 40");
        check(replyAt(0, 41) == 0, "40 points of his running chain, carried: none at a rate of 41");
        check(replyAt(29, 69) == 1, "29 points carried before and 40 of his running chain: one garbage piece at a rate of 69");
        check(replyAt(29, 70) == 0, "29 points carried before and 40 of his running chain: none at a rate of 70");
    }
    std::cout << "the opponent's answer: his field is taken as it stands when he next holds a pair\n";
}

// A search also tells what a search of fewer placements finds (BuildResult::sumBy, pointsBy):
// outlasts reads the opponent's chain for every length of time off one search.
static void testSearchBy() {
    const Rules rules;
    GameField g;
    stairs(g, 0, 3);
    for (int y = 0; y < 2; ++y) g.f.set(4, y, 2), g.f.set(5, y, 3);
    const Pair known[2] = {{3, 4}, {1, 2}};
    ai::Params p;
    p.beamWidth = 30, p.beamDepth = 9, p.beamQueues = 3;
    ai::Builder builder;
    const ai::BuildResult deep = builder.search(g, known, 2, rules, TM, p, 5);
    bool grows = false;
    for (int depth = 1; depth <= p.beamDepth; ++depth) {
        ai::Params q = p;
        q.beamDepth = depth;
        const ai::BuildResult r = builder.search(g, known, 2, rules, TM, q, 5);
        check(r.alive == deep.alive, "the first placements that lose depend on the depth of the search");
        for (int id = 0; id < PLACEMENTS; ++id) {
            check(deep.sumBy[id][depth - 1] == r.sum[id], "sumBy is not the sum of the search of that depth (depth " + std::to_string(depth) + ")");
            check(deep.pointsBy[id][depth - 1] == r.points[id], "pointsBy is not the largest chain of the search of that depth (depth " + std::to_string(depth) + ")");
            grows = grows || (depth > 1 && deep.sumBy[id][depth - 1] > deep.sumBy[id][depth - 2]);
        }
    }
    check(grows, "the test field has no chain that a deeper search finds and a shallower one does not");
    for (int id = 0; id < PLACEMENTS; ++id)
        for (int n = p.beamDepth; n < ai::HORIZONS; ++n)
            check(deep.sumBy[id][n] == deep.sum[id] && deep.pointsBy[id][n] == deep.points[id], "beyond its depth a search does not report what it found");
    std::cout << "a search tells what every shallower search finds\n";
}

// An agent may choose before its pair appears, and on another thread (ai/agent.h): the buttons
// are the same as when it chooses at once, unless nothing is to wait for the choice (Free): then a
// pair is left alone until its choice is made, and still goes where the choice says.
struct ThinkProbe : ai::PlacementAgent {
    std::atomic<bool> go{true}; // false: a choice is held back
    std::atomic<int> calls{0};
    int fixed = -1; // the placement chosen; -1 = one that depends on what is seen
    // The second choice of a pair (PlacementAgent::again): whether one is made, held back or not, and its placement.
    bool sayAgain = false;
    std::atomic<bool> goSecond{true};
    std::atomic<int> secondCalls{0};
    int secondFixed = -1;
    ~ThinkProbe() override { wait(); }
    void wait() { // for a choice that is being made
        go = goSecond = true;
        finish();
    }
    bool again(const Observation&) override { return sayAgain; }
    int choose(const Observation& o, Pair pair, unsigned legal) override {
        if (choosingAgain) {
            ++secondCalls;
            while (!goSecond) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (secondFixed >= 0) return secondFixed;
        } else {
            ++calls;
            while (!go) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (fixed >= 0) return fixed;
        }
        int id = int((uint64_t(o.frame) * 7 + uint64_t(pair.axis) * 3 + uint64_t(o.self().field.f.count())) % PLACEMENTS);
        while (!(legal >> id & 1)) id = (id + 1) % PLACEMENTS;
        return id;
    }
};
// A choice on a thread of its own reads the ways of the own seat from what was taken of them on the thread of frame()
// as it began (PlacementAgent::wayRead), never from the members: here the members say the straight ways and what is
// taken says the ways over soonest, and the choice reads the latter whatever the order of the two threads.
struct WayProbe : ThinkProbe {
    std::atomic<int> read{-1};                    // Observation::fastestRead of the own seat in the first choice
    mutable std::atomic<int> taken{0};            // the times the ways were taken
    mutable std::atomic<bool> takenElsewhere{false}; // ... on another thread than the one the probe was made on
    const std::thread::id home = std::this_thread::get_id();
    WayRead wayRead() const override {
        WayRead way = ai::PlacementAgent::wayRead();
        way.fastest = true, way.states = 600;
        ++taken;
        if (std::this_thread::get_id() != home) takenElsewhere = true;
        return way;
    }
    int choose(const Observation& o, Pair pair, unsigned legal) override {
        int none = -1;
        read.compare_exchange_strong(none, int(o.fastestRead[o.me]));
        return ThinkProbe::choose(o, pair, legal);
    }
};
static void testThinkingWayRead() {
    const Rules rules;
    WayProbe a;
    a.ahead = 12, a.thinking = ai::Thinking::Free, a.fastestWays = false;
    Round r;
    r.start(rules, 3);
    a.startRound(0, 5);
    PlayerEvents e[2];
    for (int f = 0; f < 200 && a.taken == 0; ++f) {
        const uint8_t b[2] = {a.frame(ai::observe(r, 0)), 0};
        r.step(b, e);
    }
    a.wait();
    check(a.taken > 0 && !a.takenElsewhere && a.read == int(Observation::FASTEST_OWN),
          "a choice on its own thread read the ways from the members, not from what was taken of them as it began (read " +
              std::to_string(a.read) + ", taken " + std::to_string(a.taken) + (a.takenElsewhere ? ", on another thread" : "") + ")");
}
static void testThinking() {
    const Rules rules;
    // The second player does nothing. Twelve frames ahead is the whole pause before a pair.
    const auto play = [&](ThinkProbe& a, int frames, int* pairs) {
        Round r;
        r.start(rules, 3);
        a.startRound(0, 5);
        std::vector<uint8_t> out;
        PlayerEvents e[2];
        for (int f = 0; f < frames && r.state == RoundState::Playing; ++f) {
            const uint8_t b[2] = {a.frame(ai::observe(r, 0)), 0};
            out.push_back(b[0]);
            r.step(b, e);
        }
        a.wait();
        *pairs = r.player[0].moves;
        return out;
    };
    ThinkProbe once, thread;
    once.ahead = thread.ahead = 12;
    thread.thinking = ai::Thinking::Thread;
    int pairsOnce = 0, pairsThread = 0;
    const std::vector<uint8_t> a = play(once, 1500, &pairsOnce), b = play(thread, 1500, &pairsThread);
    check(pairsOnce >= 10, "the test round is over after " + std::to_string(pairsOnce) + " pairs");
    check(a == b && pairsOnce == pairsThread, "a choice made on a thread gives other buttons than one made at once");
    check(once.calls == thread.calls && once.calls >= pairsOnce, "the choices are not made once a pair");

    // Free, with the choice held back: the pair is left alone; then it goes where the choice says.
    ThinkProbe slow;
    slow.ahead = 12, slow.thinking = ai::Thinking::Free, slow.fixed = placementId({0, UP}), slow.go = false;
    Round r;
    r.start(rules, 3);
    slow.startRound(0, 5);
    PlayerEvents e[2];
    const int held = 40;
    for (int f = 0; f < held; ++f) {
        const uint8_t buttons[2] = {slow.frame(ai::observe(r, 0)), 0};
        check(buttons[0] == 0, "a pair whose choice is not made is moved");
        r.step(buttons, e);
    }
    check(r.player[0].phase == Phase::Control && r.player[0].moves == 0, "the pair is not in the air after forty frames (the test expects it there)");
    check(slow.lateFrames == held - r.timing.firstSpawn, "the frames a pair waited for its choice: " + std::to_string(slow.lateFrames));
    slow.go = true;
    int heights[W];
    for (int pairs = 1; pairs <= 2; ++pairs) {
        for (int f = 0; f < 2000 && r.player[0].moves < pairs; ++f) {
            const int64_t late = slow.lateFrames;
            const uint8_t buttons[2] = {slow.frame(ai::observe(r, 0)), 0};
            if (slow.lateFrames > late) std::this_thread::sleep_for(std::chrono::milliseconds(1)); // (the thread needs a moment)
            r.step(buttons, e);
        }
        r.player[0].field.heights(heights);
        check(r.player[0].moves == pairs && heights[0] == 2 * pairs, "a pair that waited for its choice did not go where the choice says");
    }
    check(slow.calls >= 2, "the second pair got no choice of its own");

    // A choice that comes when the pair can no longer get there: the pair goes where it can still
    // get to, as near to the columns chosen as possible. (The second column is ten high; the choice,
    // axis on it and the other piece on the first column, comes when the pair is below its top.)
    {
        ThinkProbe behind;
        behind.ahead = 12, behind.thinking = ai::Thinking::Free, behind.fixed = placementId({1, LEFT}), behind.go = false;
        Round s;
        s.start(rules, 3);
        addGarbage(s.player[0].field, 1, 10);
        behind.startRound(0, 5);
        PlayerEvents ev[2];
        int f = 0;
        for (; f < 4000 && !(s.player[0].phase == Phase::Control && s.player[0].pair.y <= 7); ++f) {
            const uint8_t buttons[2] = {behind.frame(ai::observe(s, 0)), 0};
            s.step(buttons, ev);
        }
        check(s.player[0].phase == Phase::Control && s.player[0].moves == 0 && s.player[0].pair.x == SPAWN_X, "the pair of the test is not behind the column");
        behind.go = true;
        for (; f < 8000 && s.player[0].moves < 1; ++f) {
            const int64_t late = behind.lateFrames;
            const uint8_t buttons[2] = {behind.frame(ai::observe(s, 0)), 0};
            if (behind.lateFrames > late) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            s.step(buttons, ev);
        }
        s.player[0].field.heights(heights);
        check(s.player[0].moves == 1 && heights[SPAWN_X] == 2 && heights[0] == 0 && heights[1] == 10,
              "a pair that cannot get to its placement any more did not go to the nearest one: heights " + std::to_string(heights[0]) + " " +
                  std::to_string(heights[1]) + " " + std::to_string(heights[2]) + " " + std::to_string(heights[3]));
    }

    // An agent that is dropped while a thread is at work waits for it.
    {
        ThinkProbe gone;
        gone.ahead = 12, gone.thinking = ai::Thinking::Free, gone.go = false;
        Round s;
        s.start(rules, 3);
        gone.startRound(0, 5);
        gone.frame(ai::observe(s, 0));
        check(gone.calls <= 1, "more than one choice for one pair");
    }
    std::cout << "thinking: ahead of the pair and on a thread the same buttons; a pair without a choice waits and then goes where the choice says, or as near as it still can\n";
}

// The second choice (ai/agent.h, Params::thinkAgain). The third pair is on the screen four frames
// before a pair appears (Timing::nextShown); a choice begun before that is made once more, and the
// pair goes to the second choice if a path leads there from where it is.
static void testSecondChoice() {
    const Rules rules;
    const int left = placementId({0, UP}), right = placementId({5, UP});
    // The second player does nothing. Until `pairs` pairs are placed; wall: the second column is so high.
    const auto play = [&](ThinkProbe& a, int pairs, std::vector<uint8_t>* buttons = nullptr, int wall = 0, int64_t* frames = nullptr) {
        Round r;
        r.start(rules, 3);
        if (wall) addGarbage(r.player[0].field, 1, wall);
        a.startRound(0, 5);
        PlayerEvents e[2];
        for (int f = 0; f < 4000 && r.state == RoundState::Playing && r.player[0].moves < pairs; ++f) {
            const uint8_t b[2] = {a.frame(ai::observe(r, 0)), 0};
            if (buttons) buttons->push_back(b[0]);
            r.step(b, e);
        }
        if (frames) *frames = r.frame;
        a.wait();
        std::array<int, W> h{};
        int heights[W];
        r.player[0].field.heights(heights);
        for (int x = 0; x < W; ++x) h[size_t(x)] = heights[x];
        return h;
    };
    const auto probe = [&](int ahead, int late, bool again) {
        auto a = std::make_unique<ThinkProbe>();
        a->ahead = ahead, a->againLate = late, a->sayAgain = again, a->fixed = left, a->secondFixed = right;
        return a;
    };
    const auto logOf = [](const ThinkProbe& a) {
        std::string s;
        for (int v : a.againLog) s += std::to_string(v) + " ";
        return s;
    };
    {
        // It comes before the pair appears: the pair goes there from the start.
        auto a = probe(12, 0, true);
        const auto h = play(*a, 2);
        check(h[5] == 4 && h[0] == 0 && a->calls == 2 && a->secondCalls == 2 && a->againSwitched == 0 && a->againLog == std::vector<int>({0, 0, 1, 0}),
              "a second choice before the pair appears: heights " + std::to_string(h[0]) + " and " + std::to_string(h[5]) + ", log " + logOf(*a));
    }
    {
        // It comes three frames after. The pair has gone straight down meanwhile (holdSecond) and goes
        // there: no frame is lost against a second choice that came before the pair. Without that it had
        // set out for the first choice, turns round, and comes later.
        int64_t before = 0, held = 0, turned = 0;
        auto first = probe(12, 0, true);
        play(*first, 2, nullptr, 0, &before);
        auto a = probe(12, 3, true);
        a->holdSecond = true; // (Params::thinkHold 1; the default is 0)
        const auto h = play(*a, 2, nullptr, 0, &held);
        check(h[5] == 4 && h[0] == 0 && a->againSwitched == 2 && a->againNoPath == 0 && a->againLog == std::vector<int>({0, 3, 1, 3}),
              "a second choice three frames after the pair appeared: heights " + std::to_string(h[0]) + " and " + std::to_string(h[5]) + ", log " + logOf(*a));
        check(a->againHeld == 6 && held == before, "a pair that waits for its second choice: went straight down in " + std::to_string(a->againHeld) + " frames, two pairs placed after " +
                                                       std::to_string(held) + " frames (" + std::to_string(before) + " with the second choice before the pair)");
        auto b = probe(12, 3, true);
        b->holdSecond = false;
        const auto g = play(*b, 2, nullptr, 0, &turned);
        check(g[5] == 4 && b->againSwitched == 2 && b->againHeld == 0 && turned > before, "a pair that sets out for its first choice at once does not lose frames by turning round: " + std::to_string(turned));
    }
    {
        // It does not come while the pair is in the air: the first choice stands.
        auto a = probe(12, 500, true);
        const auto h = play(*a, 2);
        check(h[0] == 4 && h[5] == 0 && a->againSwitched == 0 && a->againLog == std::vector<int>({0, -1, 1, -1}),
              "a second choice that comes after the pair has come to rest: heights " + std::to_string(h[0]) + " and " + std::to_string(h[5]) + ", log " + logOf(*a));
    }
    {
        // No path leads there any more (the second column is ten high, the first choice goes straight
        // down, the second is the first column): early it is taken, behind the wall it is not.
        for (int late : {1, 14}) {
            auto a = probe(12, late, true);
            a->fixed = placementId({SPAWN_X, UP}), a->secondFixed = left;
            const auto h = play(*a, 1, nullptr, 10);
            const bool taken = late == 1;
            check(h[0] == (taken ? 2 : 0) && h[SPAWN_X] == (taken ? 0 : 2) && a->againSwitched == (taken ? 1 : 0) && a->againNoPath == (taken ? 0 : 1),
                  "a second choice behind a wall, " + std::to_string(late) + " frames after the pair appeared: heights " + std::to_string(h[0]) + " and " + std::to_string(h[SPAWN_X]) +
                      ", switched " + std::to_string(a->againSwitched) + ", no path " + std::to_string(a->againNoPath));
        }
    }
    {
        // holdLevel: the pair that waits for its second choice does not go down behind the wall, and is taken there
        // fourteen frames after it appeared too.
        auto a = probe(12, 14, true);
        a->fixed = placementId({SPAWN_X, UP}), a->secondFixed = left, a->holdSecond = true, a->holdLevel = true;
        const auto h = play(*a, 1, nullptr, 10);
        check(h[0] == 2 && h[SPAWN_X] == 0 && a->againSwitched == 1 && a->againNoPath == 0 && a->againLevel > 0,
              "a pair waiting for a second choice behind a wall stays above it: heights " + std::to_string(h[0]) + " and " + std::to_string(h[SPAWN_X]) + ", switched " +
                  std::to_string(a->againSwitched) + ", no path " + std::to_string(a->againNoPath) + ", left alone in " + std::to_string(a->againLevel) + " frames");
    }
    {
        // No second choice: when again() says no; when the choice is made in the frame of the pair;
        // when it is begun with the third pair on the screen already.
        auto no = probe(12, 0, false), late = probe(1, 0, true), four = probe(4, 0, true);
        const auto h0 = play(*no, 2), h1 = play(*late, 2), h2 = play(*four, 2);
        check(h0[0] == 4 && no->secondCalls == 0 && no->againAsked == 2 && no->againLog.empty(), "a second choice was made though again() said no");
        check(h1[0] == 4 && late->secondCalls == 0 && late->againAsked == 0, "a second choice for a choice made in the frame of its pair");
        check(h2[0] == 4 && four->secondCalls == 0 && four->againAsked == 0, "a second choice for a choice begun four frames before its pair");
    }
    {
        // On a thread as at once (placements that depend on what is seen).
        for (int late : {0, 3}) {
            ThinkProbe once, thread;
            once.ahead = thread.ahead = 12, once.againLate = thread.againLate = late, once.sayAgain = thread.sayAgain = true;
            thread.thinking = ai::Thinking::Thread;
            std::vector<uint8_t> a, b;
            play(once, 12, &a), play(thread, 12, &b);
            check(a == b && once.secondCalls == thread.secondCalls && once.secondCalls >= 8 && once.againLog == thread.againLog,
                  "second choices on a thread give other buttons than at once (" + std::to_string(late) + " frames after the pair)");
        }
    }
    {
        // Free: the second choice is held back while the pair appears and falls (straight down); let
        // go, the pair goes there. Held back until the pair has come to rest, the first choice stands,
        // and the next pair gets its choices as usual.
        ThinkProbe slow;
        slow.ahead = 12, slow.thinking = ai::Thinking::Free, slow.sayAgain = true, slow.fixed = left, slow.secondFixed = right, slow.goSecond = false;
        slow.holdSecond = true; // (Params::thinkHold 1; the default is 0)
        Round r;
        r.start(rules, 3);
        slow.startRound(0, 5);
        PlayerEvents e[2];
        const auto step = [&] {
            const uint8_t b[2] = {slow.frame(ai::observe(r, 0)), 0};
            // (the threads need a moment: more of it before a pair appears, where the first choice is being made -
            // on a machine busy with other work its thread was not through in two milliseconds a frame)
            std::this_thread::sleep_for(std::chrono::milliseconds(r.player[0].phase == Phase::Next ? 20 : 2));
            r.step(b, e);
        };
        for (int f = 0; f < 4000 && !(r.player[0].phase == Phase::Control && r.player[0].pair.y <= 9); ++f) step();
        check(slow.secondCalls == 1 && slow.lateFrames == 0 && r.player[0].pair.x == SPAWN_X && slow.againHeld > 0, "Free: the pair did not go straight down while its second choice was being made");
        slow.goSecond = true;
        for (int f = 0; f < 4000 && r.player[0].moves < 1; ++f) step();
        int h[W];
        r.player[0].field.heights(h);
        check(h[5] == 2 && h[0] == 0 && slow.againSwitched == 1 && slow.againLog.size() == 2 && slow.againLog[1] > 0, "Free: the pair did not turn round when its second choice came");
        slow.goSecond = false;
        for (int f = 0; f < 4000 && r.player[0].moves < 2; ++f) step();
        step(); // (the agent sees that its pair has come to rest)
        r.player[0].field.heights(h);
        check(h[0] == 2 && slow.againLog.size() == 4 && slow.againLog[3] == -1, "Free: a second choice that did not come while the pair was in the air");
        slow.goSecond = true;
        for (int f = 0; f < 4000 && r.player[0].moves < 3; ++f) step();
        check(r.player[0].moves == 3 && slow.calls == 3 && slow.secondCalls == 3 && slow.lateFrames == 0, "Free: the pair after a second choice that came too late");
        slow.wait();
    }

    // X3: a round with second choices that come in the air, played again from what the game keeps
    // of it (the notes of the choices and the frames the second ones came at): the same buttons.
    {
        ai::Params q;
        q.beamWidth = 20, q.beamDepth = 6, q.beamQueues = 2, q.thinkAhead = 12, q.thinkLate = 3;
        ai::Params plain = q;
        plain.thinkAgain = 0;
        struct Played {
            std::vector<uint8_t> buttons;
            std::vector<ai::X3Agent::Note> notes;
            std::vector<int> again;
            int64_t begun = 0, switched = 0;
            int pairs = 0;
        };
        // (The second player does nothing, so that the first is always ahead: the one behind is shown
        // his third pair by the other's window, and his first choice knows it.)
        const auto round = [&](const ai::Params& first, const Played* script) {
            Played out;
            ai::X3Agent a(first);
            a.noting = true;
            if (script) a.script = &script->notes, a.againScript = &script->again;
            a.startRound(0, 5);
            Round r;
            r.start(rules, 4711);
            Observation o;
            PlayerEvents e[2];
            for (int f = 0; f < 2400 && r.state == RoundState::Playing; ++f) {
                ai::observe(r, 0, o);
                const uint8_t now[2] = {a.frame(o), 0};
                out.buttons.push_back(now[0]);
                r.step(now, e);
            }
            out.pairs = r.player[0].moves;
            out.notes = a.takeNotes(), out.again = a.againLog, out.begun = a.againBegun, out.switched = a.againSwitched;
            return out;
        };
        const Played game = round(q, nullptr), without = round(plain, nullptr);
        int twice = 0;
        for (size_t i = 1; i < game.notes.size(); ++i) twice += game.notes[i].move == game.notes[i - 1].move;
        check(game.pairs >= 15 && game.begun >= 10 && twice == game.begun && int(game.again.size()) == 2 * twice,
              "X3 with second choices: " + std::to_string(game.pairs) + " pairs, " + std::to_string(game.begun) + " second choices, " + std::to_string(twice) + " moves with two notes");
        check(without.begun == 0 && without.again.empty(), "X3 made second choices with thinkAgain 0");
        ai::Params tool = q;
        tool.thinkLate = 0; // (the frames come from the round that is played again)
        const Played again = round(tool, &game);
        check(again.buttons == game.buttons && again.begun == game.begun && again.switched == game.switched, "a round of X3 with second choices is not played again from its notes");
        // (and with the third pair on the screen when the choice is begun there is none)
        ai::Params four = q;
        four.thinkAhead = 4;
        check(round(four, nullptr).begun == 0, "X3 made a second choice though its first knew three pairs");
        // A twin (Params::x3Salt): the same AI with other random numbers plays another round on the same
        // seed. (Here with chance far larger than the sums of the search, so that every choice is drawn:
        // the chance X3 has by default is small against the gaps between placements, and this small
        // search then chooses the same placements whatever the numbers. Counted with this test: 0 of
        // 1194 frames differed with b0Noise 300.)
        ai::Params noisy = plain, twin = plain;
        noisy.b0Noise = twin.b0Noise = 100000000, twin.x3Salt = 1;
        const Played once = round(noisy, nullptr);
        check(round(plain, nullptr).buttons == without.buttons && round(noisy, nullptr).buttons == once.buttons, "X3 does not play its round again");
        check(round(twin, nullptr).buttons != once.buttons, "a twin of X3 plays the round of X3 itself");
        std::cout << "second choice: before the pair, in the air, too late, behind a wall, on a thread, held back; X3 made " << game.begun << " in " << game.pairs << " pairs ("
                  << game.switched << " changed the placement), and the round was played again from its notes\n";
    }
}

// The queues of a beam search may run on threads of their own (Params::searchThreads): X3 puts
// every pair where it puts it on one thread.
static void testSearchThreads() {
    const Rules rules;
    ai::Params p;
    p.beamWidth = 30, p.beamDepth = 8, p.beamQueues = 3;
    // (the positions below are made for these values of the judging of a poke: the default waits for 60 cells on the
    //  own field, and these fields are small. The default values are tested where a test sets them.)
    p.x3PokeFrom = 0, p.x3PokeNet = 0, p.x3KeepMain = 85, p.x3PokeGain = 75, p.xWildChance = 0, p.xWildEvery = 2;
    ai::Params threaded = p;
    threaded.searchThreads = 3;
    std::vector<uint8_t> buttons[2];
    int pairs[2]{};
    for (int k = 0; k < 2; ++k) {
        ai::X3Agent a(k ? threaded : p), b(p);
        Round r;
        r.start(rules, 11);
        a.startRound(0, 5), b.startRound(1, 6);
        ai::Observation o;
        PlayerEvents e[2];
        for (int f = 0; f < 1500 && r.state == RoundState::Playing; ++f) {
            ai::observe(r, 0, o);
            uint8_t now[2];
            now[0] = a.frame(o);
            o.me = 1;
            now[1] = b.frame(o);
            buttons[k].push_back(now[0]), buttons[k].push_back(now[1]);
            r.step(now, e);
        }
        pairs[k] = r.player[0].moves;
    }
    check(pairs[0] >= 20, "the round of the thread test is over after " + std::to_string(pairs[0]) + " pairs");
    check(buttons[0] == buttons[1], "X3 plays otherwise when its beam search runs on three threads");
    std::cout << "threads: X3 plays the same with its beam search on three threads\n";
}

// A search may be given a time to stop at (Builder::until; Params::thinkBudget for X3). Out of
// time it still answers with a placement that is legal and does not lose; with time to spare the
// answer is that of a search without a clock.
static void testBudget() {
    using Clock = ai::Builder::Clock;
    Rng rng(20261008, 41);
    ai::Builder plain, timed;
    int cut = 0;
    for (int i = 0; i < 12; ++i) {
        Rules rules;
        if (i % 4 == 1) rules.colors = 5;
        ai::Params p;
        p.beamWidth = 40 + int(rng.below(60)), p.beamDepth = 6 + int(rng.below(6));
        if (i % 3 == 2) p.beamShare = 0;
        const GameField f = randomField(rng, rules, int(rng.below(26)));
        Pair known[3];
        for (Pair& k : known) k = {rules.palette[rng.below(uint64_t(rules.colors))], rules.palette[rng.below(uint64_t(rules.colors))]};
        const int count = 1 + int(rng.below(3)), threads = i % 2 ? 3 : 1;
        const uint64_t seed = rng.next();
        const std::string at = " (search " + std::to_string(i) + ")";
        const ai::BuildResult a = plain.search(f, known, count, rules, TM, p, seed, 1);
        check(!a.cut, "a search without a clock says it was stopped" + at);
        timed.until(Clock::now() + std::chrono::hours(1));
        const ai::BuildResult far = timed.search(f, known, count, rules, TM, p, seed, threads);
        bool same = sameBuild(a, far) && !far.cut && a.valued == far.valued;
        for (int id = 0; id < PLACEMENTS; ++id) {
            same = same && a.value[id] == far.value[id];
            for (int n = 0; n < ai::HORIZONS; ++n) same = same && a.sumBy[id][n] == far.sumBy[id][n] && a.pointsBy[id][n] == far.pointsBy[id][n];
        }
        check(same, "a time to stop at that is far away gives another answer" + at);
        timed.until(Clock::now() - std::chrono::seconds(1));
        const ai::BuildResult late = timed.search(f, known, count, rules, TM, p, seed, threads);
        check(late.cut && late.layers == 1 && late.nodes > 0 && late.nodes < a.nodes, "a search that is out of time is not stopped" + at);
        check(late.alive == a.alive, "out of time the first placements that lose are not those of the whole search" + at);
        check(late.best >= 0 && (legalPlacements(f) >> late.best & 1) && (late.alive >> late.best & 1), "out of time the move is not a legal one" + at);
        for (int id = 0; id < PLACEMENTS; ++id) check(late.sum[id] <= a.sum[id], "out of time a search finds more than the whole search" + at);
        // (the first layer only: what the first placement itself fires)
        ai::Params first = p;
        first.beamDepth = 1;
        check(sameBuild(late, plain.search(f, known, count, rules, TM, first, seed, 1)), "out of time the answer is not that of the first layer" + at);
        timed.untimed();
        check(sameBuild(a, timed.search(f, known, count, rules, TM, p, seed, threads)), "the clock is not taken off again" + at);
        cut += late.cut;
    }
    // X3: with time to spare the buttons of a round are those without a clock, also when searches
    // begun while a pair fell are taken (thinkPonder); with next to no time (1 ms, less than its
    // plans take) and no such searches every search is stopped and every pair is still put on a
    // placement, without a pair kept waiting; with them (the last run) the pairs are placed as well.
    const Rules rules;
    ai::Params p;
    p.beamWidth = 30, p.beamDepth = 8, p.beamQueues = 3;
    // (the positions below are made for these values of the judging of a poke: the default waits for 60 cells on the
    //  own field, and these fields are small. The default values are tested where a test sets them.)
    p.x3PokeFrom = 0, p.x3PokeNet = 0, p.x3KeepMain = 85, p.x3PokeGain = 75, p.xWildChance = 0, p.xWildEvery = 2;
    p.thinkAhead = 12; // (as in the game: the choice is made when the pair before has come to rest, and sees the pairs that were on the screen while it fell)
    std::vector<uint8_t> buttons[4];
    int pairs[4]{};
    int64_t stopped[4]{}, taken[4]{};
    for (int k = 0; k < 4; ++k) {
        ai::Params q = p;
        q.thinkBudget = k == 1 ? 3600 * 1000 : k >= 2 ? 1 : 0;
        if (k == 2) q.thinkPonder = 0;
        ai::X3Agent a(q), b(p);
        Round r;
        r.start(rules, 11);
        a.startRound(0, 5), b.startRound(1, 6);
        ai::Observation o;
        PlayerEvents e[2];
        for (int f = 0; f < 1500 && r.state == RoundState::Playing; ++f) {
            ai::observe(r, 0, o);
            uint8_t now[2];
            now[0] = a.frame(o);
            o.me = 1;
            now[1] = b.frame(o);
            buttons[k].push_back(now[0]), buttons[k].push_back(now[1]);
            r.step(now, e);
        }
        pairs[k] = r.player[0].moves, stopped[k] = a.cutSearches, taken[k] = a.earlyTaken;
    }
    check(pairs[0] >= 20, "the round of the clock test is over after " + std::to_string(pairs[0]) + " pairs");
    check(buttons[0] == buttons[1] && !stopped[0] && !stopped[1], "X3 plays otherwise with a time to stop at that is far away");
    check(taken[1] >= 10 && !taken[0], "X3 with time to spare took " + std::to_string(taken[1]) + " searches begun while its pair fell (" + std::to_string(taken[0]) + " without a clock)");
    check(stopped[2] >= 10 && pairs[2] >= 10, "X3 with 1 ms for a choice: " + std::to_string(stopped[2]) + " searches stopped, " + std::to_string(pairs[2]) + " pairs placed");
    check(pairs[3] >= 10, "X3 with 1 ms for a choice and searches begun early: " + std::to_string(pairs[3]) + " pairs placed");
    std::cout << "clock: " << cut << " searches out of time answer with the first layer, with time to spare as without a clock (" << taken[1]
              << " searches begun while the pair fell were taken); X3 with 1 ms places " << pairs[2] << " pairs (" << stopped[2] << " searches stopped), with searches begun early "
              << pairs[3] << " pairs (" << stopped[3] << " stopped, " << taken[3] << " taken)\n";
}

// A field from rows of text, top row first (R G B Y = colours 1 to 4, o = garbage).
static GameField fieldOf(const char* const rows[], int count) {
    const auto cell = [](char c) { return c == 'o' ? GARBAGE : c == 'R' ? Cell(1) : c == 'G' ? Cell(2) : c == 'B' ? Cell(3) : c == 'Y' ? Cell(4) : EMPTY; };
    GameField g;
    for (int row = 0; row < count; ++row)
        for (int x = 0; x < W; ++x)
            if (cell(rows[row][x]) != EMPTY) g.f.set(x, count - 1 - row, cell(rows[row][x]));
    return g;
}
// The largest chain a pair can fire on a field (points; 0 = none).
static int bestFire(const GameField& g, Pair pair, const Rules& rules) {
    int most = 0;
    const unsigned legal = legalPlacements(g);
    for (int id = 0; id < PLACEMENTS; ++id) {
        if (!(legal >> id & 1)) continue;
        GameField f = g;
        place(f, placement(id), pair);
        ai::Trial chain;
        ai::runTrialChain(f.f, rules, TM, chain);
        most = std::max(most, chain.points);
    }
    return most;
}
struct SwitchProbe : ai::X3Agent {
    using ai::X3Agent::X3Agent;
    using ai::X3Agent::shapeChoices;
    using ai::X3Agent::enoughChoices;
    using ai::X3Agent::guardChoices;
    using ai::X3Agent::pickBuild;
    using ai::X3Agent::soonSum;
    using ai::X3Agent::roomPairs;
    const ai::PlanSet& plans() const { return mine; }
};
// Switches of X3, each on what it was made for.
// Params::thinkRivalFull: the search of the opponent's field made on a thread of its own is the search itself, taken
// once, and only for the field, the pairs, the depth and the width it was begun with.
static void testRivalAhead() {
    struct Open : ai::X3Agent {
        using ai::X3Agent::X3Agent;
        using ai::X3Pace::beginRival;
        using ai::X3Pace::takeRival;
    };
    using Clock = ai::Builder::Clock;
    Rng rng(20261118, 7);
    Rules rules;
    ai::Params p;
    p.beamWidth = 30, p.beamDepth = 6, p.beamQueues = 3;
    Open agent(p);
    const GameField f = randomField(rng, rules, 20), other = randomField(rng, rules, 22);
    const Pair known[2] = {{rules.palette[0], rules.palette[1]}, {rules.palette[2], rules.palette[2]}};
    ai::Params q = p;
    q.beamLastValues = 0; // (as a search of a choice that does not read the values of the last layer)
    ai::Builder plain;
    const ai::BuildResult direct = plain.search(f, known, 2, rules, TM, q, 5, 1);
    ai::BuildResult got;
    check(!agent.takeRival(f, known, 2, p, 5, Clock::now(), got), "thinkRivalFull: nothing to take before a search is begun");
    check(agent.beginRival(f, known, 2, rules, TM, p, 5, true), "thinkRivalFull: a search begun");
    check(!agent.takeRival(other, known, 2, p, 5, Clock::now() + std::chrono::seconds(60), got), "thinkRivalFull: not taken for another field");
    ai::Params deeper = p;
    deeper.beamDepth = 7;
    check(!agent.takeRival(f, known, 2, deeper, 5, Clock::now() + std::chrono::seconds(60), got), "thinkRivalFull: not taken for another depth");
    check(agent.beginRival(f, known, 2, rules, TM, p, 5, false), "thinkRivalFull: asked for again while at work - the same search");
    check(agent.takeRival(f, known, 2, p, 5, Clock::now() + std::chrono::seconds(60), got), "thinkRivalFull: taken when done");
    bool same = got.best == direct.best && !got.cut;
    for (int i = 0; i < PLACEMENTS; ++i) same = same && got.sum[i] == direct.sum[i] && got.sumBy[i][2] == direct.sumBy[i][2];
    check(same, "thinkRivalFull: the search made on its own thread is the search made at once");
    check(!agent.takeRival(f, known, 2, p, 5, Clock::now(), got), "thinkRivalFull: taken once");
    // (another field: the search at work is stopped and the new one begun)
    check(agent.beginRival(f, known, 2, rules, TM, p, 5, true) && agent.beginRival(other, known, 2, rules, TM, p, 5, true), "thinkRivalFull: a search for another field begun");
    check(!agent.takeRival(f, known, 2, p, 5, Clock::now() + std::chrono::seconds(60), got) && agent.takeRival(other, known, 2, p, 5, Clock::now() + std::chrono::seconds(60), got),
          "thinkRivalFull: the search of the field asked for last is the one at work");
}

static void testSwitches() {
    Rules rules;
    rules.targetPoints = 40; // a clear of four is one garbage piece
    for (int i = 0; i < rules.marginSteps; ++i) rules.marginRates[size_t(i)] = 40;
    const Rules usual;
    ai::Params p;
    p.beamWidth = 30, p.beamDepth = 8, p.beamQueues = 3;
    // (the positions below are made for these values of the judging of a poke: the default waits for 60 cells on the
    //  own field, and these fields are small. The default values are tested where a test sets them.)
    p.x3PokeFrom = 0, p.x3PokeNet = 0, p.x3KeepMain = 85, p.x3PokeGain = 75, p.xWildChance = 0, p.xWildEvery = 2;
    const auto choose = [&](const Round& r, ai::X3Agent& agent, uint64_t seed = 7) {
        agent.startRound(0, seed);
        agent.frame(ai::observe(r, 0));
        return agent.lastChoice;
    };
    const auto pairAt = [](Round& r, int index, Pair pair) { r.tsumo.piece[size_t(index) * 2] = pair.axis, r.tsumo.piece[size_t(index) * 2 + 1] = pair.child; };

    // x3ShapeSlack: the placements the beam search may choose among when the best answer builds.
    {
        int builds[PLACEMENTS];
        int64_t score[PLACEMENTS]{};
        for (int& c : builds) c = -1;
        builds[0] = builds[1] = builds[2] = builds[3] = 8, builds[4] = 2;
        score[0] = 7000, score[1] = 6992, score[2] = -3500, score[3] = -3508, score[4] = 6900;
        check(SwitchProbe::shapeChoices(builds, score, 8, 7000, -1) == 0b01111, "without a slack: not the placements that let as much fall as the best");
        check(SwitchProbe::shapeChoices(builds, score, 8, 7000, 500) == 0b10011, "with a slack: not the placements that score within it");
        check(SwitchProbe::shapeChoices(builds, score, 8, 7000, 0) == 0b00001, "with no slack at all: not the best alone");
        // (a first placement without a building plan has no score: it is never one of them)
        check(SwitchProbe::shapeChoices(builds, score, 8, -100, 500) == 0b10011, "a placement without a building plan is among the choices");
    }

    // x3ShapeSlack and x3RowStrict on the position they come from (a round lost to Ama): 58 cells, a
    // chain of thirteen that the pair in hand fires, eight garbage pieces that fall after this move.
    // Taking them buries the chain; it is fired.
    {
        static const char* const MINE[11] = {"YY....", "BGRG..", "GRRB..", "GBYRGB", "GRYGRB", "BBBGYG", "RRGYRR", "YRGGYB", "BBRRYB", "BGRBBY", "YYYRRY"};
        static const char* const HIS[9] = {"B.....", "B.....", "RG.Y.R", "RGBG.B", "YGBGBB", "RRRYRR", "YYGYRB", "YBBGYB", "GGBGYB"};
        const GameField mine = fieldOf(MINE, 11), his = fieldOf(HIS, 9);
        const Pair hand{1, 2}, next{4, 1};
        check(mine.f.count() == 58 && his.f.count() == 41 && bestFire(mine, hand, usual) >= 70000, "the field of the buried chain is not what the test expects");
        ai::Params q;
        q.beamWidth = 40, q.beamDepth = 10, q.beamQueues = 3, q.x3ShapeSlack = 500, q.x3RowStrict = 1;
        for (uint64_t seed = 1; seed <= 4; ++seed) {
            Round r = situation(usual, mine, his, hand);
            pairAt(r, r.player[0].nextPair + 1, next);
            r.player[0].incoming.confirmed = 8;
            ai::X3Agent agent(q);
            const int id = choose(r, agent, seed);
            GameField after = mine;
            place(after, placement(id), hand);
            ai::Trial chain;
            ai::runTrialChain(after.f, usual, TM, chain);
            check(chain.points >= 70000, "a row of garbage on its way, a chain of thirteen in hand: not fired (seed " + std::to_string(seed) + ", kind " +
                                             std::to_string(agent.kind) + ", placement " + std::to_string(id) + ", " + std::to_string(chain.points) + " points)");
        }
    }

    // x3ChokeFrom on the position it comes from: 64 cells, the pair in hand fires a chain of
    // twelve; the search prefers a larger chain that needs two more pieces of a colour no pair on
    // the screen brings. With the switch the chain of twelve is fired. (The next pair fires it too,
    // sixty points larger: not worth a move with the third column eleven high.)
    {
        static const char* const MINE[13] = {"RB....", "YR....", "RG.Y..", "RGRB..", "YYGG.B", "YGYYBG", "RBGGBY", "RRYYGG", "BBGBGY", "RBYRBB", "GGYRBG", "GBBYRG", "RRRYYR"};
        static const char* const HIS[13] = {"G.....", "GG....", "RR..BR", "YYGYBR", "YGYYGG", "RYGRBR", "BYGBGY", "RRYBBG", "RYYGGB", "BBBGBG", "RGGYBB", "RRGRRR", "GGYYYB"};
        const GameField mine = fieldOf(MINE, 13), his = fieldOf(HIS, 13);
        const Pair hand{2, 2}, next{2, 4}, third{4, 1};
        // The largest chain the two pairs can fire: now, or with the next pair after a placement that fires nothing.
        int most = bestFire(mine, hand, usual);
        const unsigned legal = legalPlacements(mine);
        int later[PLACEMENTS]{};
        for (int id = 0; id < PLACEMENTS; ++id) {
            if (!(legal >> id & 1)) continue;
            GameField f = mine;
            place(f, placement(id), hand);
            ai::Trial chain;
            ai::runTrialChain(f.f, usual, TM, chain);
            if (chain.chains || f.dead()) continue;
            later[id] = bestFire(f, next, usual);
            most = std::max(most, later[id]);
        }
        check(mine.f.count() == 64 && most >= 50000, "the field of the choke is not what the test expects (" + std::to_string(most) + " points)");
        ai::Params q;
        q.beamWidth = 40, q.beamDepth = 10, q.beamQueues = 3, q.x3ChokeFrom = 64;
        ai::Params usualParams = q;
        usualParams.x3ChokeFrom = 0;
        Round r = situation(usual, mine, his, hand);
        pairAt(r, r.player[0].nextPair + 1, next), pairAt(r, r.player[0].nextPair + 2, third);
        ai::X3Agent agent(q);
        const int id = choose(r, agent);
        GameField after = mine;
        place(after, placement(id), hand);
        ai::Trial chain;
        ai::runTrialChain(after.f, usual, TM, chain);
        check(agent.kind == ai::X3Agent::Fire && int64_t(chain.points) * 100 >= int64_t(most) * q.x3ChokeKeep,
              "near a full field the chain the pair in hand can fire is not fired (kind " + std::to_string(agent.kind) + ", placement " + std::to_string(id) + ", " +
                  std::to_string(chain.points) + " of " + std::to_string(most) + " points)");
        // A larger chain that only the next pair fires is waited for: the same field, the pair in
        // hand of a colour that fires nothing, the next one the pair that fires the twelve.
        const Pair idle{4, 1};
        check(bestFire(mine, idle, usual) < most / 2, "the pair that should fire nothing fires the chain");
        Round wait = situation(usual, mine, his, idle);
        pairAt(wait, wait.player[0].nextPair + 1, hand), pairAt(wait, wait.player[0].nextPair + 2, third);
        ai::X3Agent patient(q);
        const int first = choose(wait, patient);
        GameField then = mine;
        place(then, placement(first), idle);
        ai::Trial none;
        ai::runTrialChain(then.f, usual, TM, none);
        check(patient.kind == ai::X3Agent::Build && !then.dead() && int64_t(bestFire(then, hand, usual)) * 100 >= int64_t(most) * q.x3ChokeKeep,
              "near a full field the chain the next pair can fire is not kept (kind " + std::to_string(patient.kind) + ", placement " + std::to_string(first) + ")");
        // x3AnswerFire on the same field: four garbage pieces wait above it, and the pair in hand fires the
        // chain of twelve. With the switch that chain is the answer (kind Fire, which no other rule gives
        // while garbage is on its way); below its number of cells the rule is not asked.
        for (int from = 60; from <= 70; from += 10) {
            ai::Params a;
            a.beamWidth = 40, a.beamDepth = 10, a.beamQueues = 3, a.x3AnswerFire = from;
            Round attacked = situation(usual, mine, his, hand);
            pairAt(attacked, attacked.player[0].nextPair + 1, next), pairAt(attacked, attacked.player[0].nextPair + 2, third);
            attacked.player[0].incoming.confirmed = 4;
            ai::X3Agent fires(a);
            const int answer = choose(attacked, fires);
            GameField fired = mine;
            place(fired, placement(answer), hand);
            ai::Trial sent;
            ai::runTrialChain(fired.f, usual, TM, sent);
            if (from <= 64)
                check(fires.kind == ai::X3Agent::Fire && int64_t(sent.points) * 100 >= int64_t(most) * a.x3ChokeKeep,
                      "attacked near a full field, the chain the pair in hand can fire is not fired (kind " + std::to_string(fires.kind) + ", placement " +
                          std::to_string(answer) + ", " + std::to_string(sent.points) + " of " + std::to_string(most) + " points)");
            else check(fires.kind != ai::X3Agent::Fire, "x3AnswerFire below its number of cells");
        }
        // x3ChokeLast on the same field. The next pair fires the chain too: it is not fired first, and the
        // placement keeps it. With next pairs that fire nothing it is the last chance, and it is fired.
        for (int last = 0; last < 2; ++last) {
            ai::Params c = q;
            c.x3ChokeLast = 1;
            Round r2 = situation(usual, mine, his, hand);
            pairAt(r2, r2.player[0].nextPair + 1, last ? idle : next), pairAt(r2, r2.player[0].nextPair + 2, last ? idle : third);
            ai::X3Agent waits(c);
            const int at = choose(r2, waits);
            GameField kept = mine;
            place(kept, placement(at), hand);
            ai::Trial fired;
            ai::runTrialChain(kept.f, usual, TM, fired);
            if (last) check(waits.kind == ai::X3Agent::Fire && int64_t(fired.points) * 100 >= int64_t(most) * c.x3ChokeKeep, "the last chance to fire near a full field is not taken (kind " + std::to_string(waits.kind) + ")");
            else check(waits.kind != ai::X3Agent::Fire && !kept.dead() && int64_t(bestFire(kept, next, usual)) * 100 >= int64_t(most) * c.x3ChokeKeep,
                       "x3ChokeLast: a chain the next pair fires too is fired first, or not kept (kind " + std::to_string(waits.kind) + ", placement " + std::to_string(at) + ")");
        }
        // x3ChokeHis on the same field: against an opponent who holds a chain of three the chain is fired
        // first (as a win, when the reading says he cannot answer it at all; else by this rule); against
        // one who holds the same chain (my own field as his) it is not.
        for (int holds = 0; holds < 2; ++holds) {
            ai::Params c = q;
            c.x3ChokeHis = 50;
            GameField little;
            stairs(little, 0, 3);
            Round r3 = situation(usual, mine, holds ? mine : little, hand);
            pairAt(r3, r3.player[0].nextPair + 1, next), pairAt(r3, r3.player[0].nextPair + 2, third);
            ai::X3Agent asks(c), plain(usualParams);
            const int at = choose(r3, asks);
            // (what the placement does counts, not only the kind of the choice: a "build" that fires the chain is the chain fired)
            GameField afterPlacement = mine;
            place(afterPlacement, placement(at), hand);
            ai::Trial sent;
            ai::runTrialChain(afterPlacement.f, usual, TM, sent);
            const bool fired = int64_t(sent.points) * 100 >= int64_t(most) * c.x3ChokeKeep;
            if (!holds) check(fired && (asks.kind == ai::X3Agent::Fire || asks.kind == ai::X3Agent::Win), "x3ChokeHis: the chain is not fired against an opponent who holds little (kind " + std::to_string(asks.kind) + ", " + std::to_string(sent.points) + " points)");
            else {
                // Against one who holds as much: the choice of the rules without x3ChokeFrom.
                Round r4 = situation(usual, mine, mine, hand);
                pairAt(r4, r4.player[0].nextPair + 1, next), pairAt(r4, r4.player[0].nextPair + 2, third);
                const int usualAt = choose(r4, plain);
                check(!fired && at == usualAt && asks.kind == plain.kind, "x3ChokeHis: against an opponent who holds as much the choice is not that of the rules without x3ChokeFrom (kind " +
                                                                              std::to_string(asks.kind) + " for " + std::to_string(plain.kind) + ", placement " + std::to_string(at) + " for " +
                                                                              std::to_string(usualAt) + ", " + std::to_string(sent.points) + " points fired)");
            }
        }
    }

    // x3Guard on the position it comes from (a game of the professional played again, X3's field of
    // 22 cells): the opponent holds a chain of four that his pair fires, 32 garbage pieces. Of the
    // placements of the pair in hand some keep a chain of three for the next pair, others give it
    // up. With the switch the placement keeps (nearly) the largest chain the next pair can fire; a
    // threat below x3Guard, or above x3GuardMost, narrows nothing.
    {
        static const char* const MINE[7] = {"B.....", "Y.....", "RR....", "RY..R.", "YY..R.", "BBBRGG", "RGGGBG"};
        static const char* const HIS[4] = {".YRG..", "YRGB..", "YRGB..", "YRGB.."};
        const GameField mine = fieldOf(MINE, 7), his = fieldOf(HIS, 4);
        const Pair hand{2, 3}, next{2, 3}, third{4, 4};
        check(mine.f.count() == 22 && bestFire(his, hand, usual) >= 2280, "the threat of the guard is not what the test expects (" + std::to_string(bestFire(his, hand, usual)) + " points)");
        // What the next pair can still fire after every placement of the pair in hand that fires nothing.
        const unsigned legal = legalPlacements(mine);
        int later[PLACEMENTS], most = 0, least = 1 << 30;
        for (int id = 0; id < PLACEMENTS; ++id) {
            later[id] = -1;
            if (!(legal >> id & 1)) continue;
            GameField f = mine;
            place(f, placement(id), hand);
            ai::Trial chain;
            ai::runTrialChain(f.f, usual, TM, chain);
            if (chain.chains || f.dead()) continue;
            later[id] = bestFire(f, next, usual);
            most = std::max(most, later[id]), least = std::min(least, later[id]);
        }
        check(most >= 900 && least * 2 < most, "the field of the guard is not what the test expects (the next pair fires " + std::to_string(least) + " to " + std::to_string(most) + " points)");
        ai::Params q;
        q.beamWidth = 40, q.beamDepth = 10, q.beamQueues = 3, q.x2Depth = 2;
        q.x3Guard = 0, q.x3GuardNear = 0, q.x3GuardMost = 0; // (the guard off: each part of it is set where it is tested)
        bool guarded = false;
        int kind = 0;
        const auto ask = [&](const ai::Params& c) {
            Round r = situation(usual, mine, his, hand);
            pairAt(r, r.player[0].nextPair + 1, next), pairAt(r, r.player[0].nextPair + 2, third);
            ai::X3Agent agent(c);
            const int id = choose(r, agent);
            guarded = agent.guardedNow, kind = agent.kind;
            return id;
        };
        const int plain = ask(q);
        check(!guarded, "without x3Guard a choice is narrowed by the guard");
        ai::Params g = q;
        g.x3Guard = 12;
        const int kept = ask(g);
        check(guarded && kind == ai::X3Agent::Build && later[kept] >= 0 && int64_t(later[kept]) * 100 >= int64_t(most) * 90,
              "x3Guard: with a chain of four held against it the placement does not keep the answer (placement " + std::to_string(kept) + " for " + std::to_string(plain) +
                  " without the switch, kind " + std::to_string(kind) + ", the next pair then fires " + std::to_string(later[kept]) + " of " + std::to_string(most) + " points)");
        ai::Params high = q;
        high.x3Guard = 40;
        const int unmoved = ask(high);
        check(unmoved == plain && !guarded, "x3Guard: a threat below its number of pieces narrows the choice");
        ai::Params capped = g;
        capped.x3GuardMost = 20;
        const int unasked = ask(capped);
        check(unasked == plain && !guarded, "x3Guard: a threat above x3GuardMost narrows the choice");
        // x3GuardNear: the guard only among the guarded placements whose sum is nearly the best. A thousand
        // thousandths is any sum: the choice of x3Guard alone.
        ai::Params any = g;
        any.x3GuardNear = 1000;
        const int anyway = ask(any);
        check(anyway == kept && guarded, "x3GuardNear 1000: not the choice of x3Guard alone (placement " + std::to_string(anyway) + " for " + std::to_string(kept) + ")");
    }
    // x3GuardNear on a result written by hand. Placement 0 has the largest sum and is not guarded; placements 1, 2 and 3
    // are, with sums one percent, ten percent and half a percent below it, and placement 3 loses in the search.
    {
        ai::BuildResult b;
        b.alive = 0b0111;
        b.sum[0] = 1000000, b.sum[1] = 990000, b.sum[2] = 900000, b.sum[3] = 995000;
        const unsigned guardedOnes = 0b1110;
        check(SwitchProbe::guardChoices(b, guardedOnes, 1000000, 0) == guardedOnes, "x3GuardNear 0: not every guarded placement, as x3Guard chose before");
        check(SwitchProbe::guardChoices(b, guardedOnes, 1000000, 20) == 0b0010, "x3GuardNear 20: not the guarded placement within two percent of the best");
        check(SwitchProbe::guardChoices(b, guardedOnes, 1000000, 100) == 0b0110, "x3GuardNear 100: not the guarded placements within ten percent of the best");
        check(SwitchProbe::guardChoices(b, guardedOnes, 1000000, 5) == 0, "x3GuardNear 5: a placement that is not within half a percent, or one that loses");
        check(SwitchProbe::guardChoices(b, guardedOnes, 1000000, 1000) == 0b0110, "x3GuardNear 1000: not every guarded placement that does not lose");
    }

    // x3AllClear: two pieces and a pair of their colour: the all clear is taken, by a chain of one wave.
    {
        GameField two;
        two.f.set(0, 0, 1), two.f.set(1, 0, 1);
        const Pair same{1, 1};
        for (int limit = 0; limit <= 2; limit += 2) {
            ai::Params q = p;
            q.x3AllClear = limit, q.x3Poke = 0, q.x3ClearNow = 0;
            SwitchProbe agent(q);
            Round r = situation(rules, two, GameField{}, same);
            const int id = choose(r, agent);
            GameField after = two;
            place(after, placement(id), same);
            ai::Trial chain;
            ai::runTrialChain(after.f, rules, TM, chain);
            if (limit) {
                check(agent.kind == ai::X3Agent::Clear && after.f.count() == 0, "an all clear by a chain of one is not taken (kind " + std::to_string(agent.kind) + ")");
                bool found = false;
                for (const ai::Plan& s : agent.plans().plans)
                    if (s.allClear && s.id[0] == id) found = true, check(s.chains == 1, "the plan of the all clear has " + std::to_string(int(s.chains)) + " waves");
                check(found, "no plan of the all clear");
            } else {
                check(agent.kind != ai::X3Agent::Clear, "an all clear is taken with the switch off");
            }
        }
    }

    // x3ClearNow (the default): the same all clear by a chain of one is taken with garbage on its way
    // too, and with nothing on its way without x3AllClear. Switched off, the garbage is answered.
    {
        GameField two;
        two.f.set(0, 0, 1), two.f.set(1, 0, 1);
        const Pair same{1, 1};
        for (int garbage = 0; garbage <= 15; garbage += 15) {
            for (int now = 0; now <= 1; ++now) {
                ai::Params q = p;
                q.x3ClearNow = now, q.x3AllClear = 0;
                // (the rule, to be kept: by a chain of one or two always, by a chain of three on fourteen pieces or fewer)
                check(p.x3ClearNow == 2 && p.x3AllClear == 3 && p.x3ClearThree == 14, "the defaults of the all clear are not the rule (two always, three on fourteen pieces or fewer)");
                SwitchProbe agent(q);
                Round r = situation(rules, two, GameField{}, same);
                r.player[0].incoming.confirmed = garbage;
                const int id = choose(r, agent);
                GameField after = two;
                place(after, placement(id), same);
                ai::Trial chain;
                ai::runTrialChain(after.f, rules, TM, chain);
                const std::string at = " (" + std::to_string(garbage) + " garbage pieces waiting, kind " + std::to_string(agent.kind) + ", placement " + std::to_string(id) + ")";
                if (now) check(agent.kind == ai::X3Agent::Clear && after.f.count() == 0, "an all clear by a chain of one is not taken" + at);
                else check(agent.kind != ai::X3Agent::Clear, "an all clear is taken by the rule that is switched off" + at);
            }
        }
    }

    // keyNoneFrom: on a field of that many cells the evaluation is that without a key piece.
    {
        GameField g;
        stairs(g, 0, 3);
        g.f.set(1, 3, EMPTY); // (the cut stairs of testKeys: ten cells)
        const int cells = g.f.count();
        const Rules plain;
        ai::Params one, none;
        none.evKeyed = 0;
        check(ai::buildValue(g, plain, TM, one) != ai::buildValue(g, plain, TM, none),
              "the cut stairs are worth the same with and without a key piece (the test needs a difference)");
        ai::Params q = one;
        q.keyNoneFrom = cells + 1;
        check(ai::buildValue(g, plain, TM, q) == ai::buildValue(g, plain, TM, one), "keyNoneFrom above the cells of the field changes the value");
        q.keyNoneFrom = cells;
        check(ai::buildValue(g, plain, TM, q) == ai::buildValue(g, plain, TM, none), "keyNoneFrom: not the value without key pieces");
    }

    // x3Crush: the clear of four that leaves the chain of three standing (one garbage piece here, so x3Crush 1)
    // crushes an opponent who has nothing to answer it with; not with a threshold above what it leaves; not one who
    // answers it with a clear of his own and keeps his chain; not one whose main chain, fired at it, is larger than
    // the own main chain built on while his runs; never with the switch off. The chain of three is no crush: it
    // spends the own main chain. (Without the pairs nobody can see yet - xWildEvery 0 - each case turns on one rule.)
    {
        GameField mine;
        stairs(mine, 0, 3);
        for (int y = 0; y < 3; ++y) mine.f.set(5, y, 4);
        GameField small; // (the own main chain of two waves, which the pair cannot fire)
        stairs(small, 0, 2);
        for (int y = 0; y < 3; ++y) small.f.set(5, y, 4);
        GameField answers;
        stairs(answers, 0, 2);
        for (int y = 0; y < 3; ++y) answers.f.set(5, y, 4);
        GameField larger; // (a chain of four his pair fires, and nothing else: far more than the own chain of two can become in the moves it runs)
        stairs(larger, 0, 4);
        const Pair pair{4, 3};
        struct Case {
            const GameField* own;
            const GameField* his;
            int crush;
            bool fires;
            const char* what;
        };
        const Case cases[] = {
            {&mine, nullptr, 1, true, "no crush at an opponent who has nothing"},
            {&mine, nullptr, 2, false, "a crush that leaves less than x3Crush"},
            {&mine, &answers, 1, false, "a crush at an opponent who answers it and keeps his chain"},
            {&small, &larger, 1, false, "a crush whose answer by his main chain the own main chain does not offset"},
            {&small, nullptr, 1, true, "no crush with the smaller own main chain at an opponent who has nothing"},
            {&mine, nullptr, 0, false, "a crush with the switch off"},
        };
        for (const Case& c : cases) {
            ai::Params q = p;
            q.x3Crush = c.crush, q.xWildEvery = 0;
            ai::X3Agent agent(q);
            Round r = situation(rules, *c.own, c.his ? *c.his : GameField{}, pair);
            const int id = choose(r, agent);
            GameField after = *c.own;
            place(after, placement(id), pair);
            ai::Trial chain;
            ai::runTrialChain(after.f, rules, TM, chain);
            const bool crushed = agent.kind == ai::X3Agent::Crush;
            check(crushed == c.fires && (!crushed || chain.chains == 1),
                  std::string(c.what) + " (kind " + std::to_string(agent.kind) + ", " + std::to_string(chain.chains) + " waves)");
        }
    }

    // x3CrushPairs: two of the crush's colour stand on the sixth column, and every pair brings one more. The pair in
    // hand fires only the main chain, which is no crush; the clear of four is fired by the second pair. With one pair
    // nothing is done about it; with two the pair in hand builds it (kind CrushBuild) and fires nothing.
    {
        GameField mine;
        stairs(mine, 0, 3);
        mine.f.set(5, 0, 4), mine.f.set(5, 1, 4);
        const Pair pair{4, 3};
        for (int pairs = 1; pairs <= 2; ++pairs) {
            ai::Params q = p;
            q.x3Crush = 1, q.x3CrushPairs = pairs, q.xWildEvery = 0;
            ai::X3Agent agent(q);
            Round r = situation(rules, mine, GameField{}, pair);
            const int id = choose(r, agent);
            GameField after = mine;
            place(after, placement(id), pair);
            ai::Trial chain;
            ai::runTrialChain(after.f, rules, TM, chain);
            const std::string at = " (kind " + std::to_string(agent.kind) + ", placement " + std::to_string(id) + ", " + std::to_string(chain.chains) + " waves)";
            if (pairs == 1) check(agent.kind != ai::X3Agent::Crush && agent.kind != ai::X3Agent::CrushBuild, "a crush of the pair in hand where only the next pair fires one" + at);
            else check(agent.kind == ai::X3Agent::CrushBuild && chain.chains == 0 && agent.crushCounts.built > 0, "x3CrushPairs 2: the crush the next pair fires is not built" + at);
        }
    }

    // x3Crush and what stands on his field. One clear of four that a single piece fires, and nothing else: firing it
    // spends all he has and offsets no more than the crush sends, so the crush stands. Two of them: he fires one at the
    // crush and keeps the other - an answer that leaves his main chain, though no pair on the screen holds its colour.
    {
        GameField mine;
        stairs(mine, 0, 3);
        for (int y = 0; y < 3; ++y) mine.f.set(5, y, 4);
        GameField one, two;
        for (int y = 0; y < 3; ++y) one.f.set(0, y, 1), two.f.set(0, y, 1), two.f.set(5, y, 2);
        const Pair pair{4, 3};
        for (const GameField* his : {&one, &two}) {
            ai::Params q = p;
            q.x3Crush = 1, q.xWildEvery = 0;
            ai::X3Agent agent(q);
            Round r = situation(rules, mine, *his, pair);
            choose(r, agent);
            const bool crushed = agent.kind == ai::X3Agent::Crush;
            check(crushed == (his == &one), std::string(his == &one ? "no crush at an opponent whose one clear answers nothing more than it" : "a crush at an opponent who holds an answer one piece fires") +
                                                " (kind " + std::to_string(agent.kind) + ")");
        }
    }

    // x3ChokeRoom. The pairs a field still takes; and on a field with room for four (every column ten high, the second
    // and the fourth eleven, no two neighbours of one colour but for three of colour 4 on top of the sixth column and
    // three of colour 3 on top of the first): a poke, when a chain the pair fires keeps the main chain (the other
    // three) and sends enough; without one the chain in hand is fired at an opponent with room to wait, when no later
    // pair on the screen fires it; it is waited for when one does; and it is not fired at an opponent whose field is as
    // full. (The pairs {1, 2} that follow in two of the cases clear nothing: no three pieces of one of their colours
    // come together with them.)
    {
        GameField tall;
        for (int y = 0; y < 12; ++y) tall.f.set(0, y, Cell(1 + y % 2));
        check(SwitchProbe::roomPairs(GameField{}) == 35 && SwitchProbe::roomPairs(tall) == 29, "the room of a field is not counted as its open cells below the top (" +
                                                                                                    std::to_string(SwitchProbe::roomPairs(GameField{})) + ", " +
                                                                                                    std::to_string(SwitchProbe::roomPairs(tall)) + ")");
        GameField full;
        for (int x = 0; x < W; ++x)
            for (int y = 0; y < 10; ++y) full.f.set(x, y, Cell(1 + (x + 2 * y) % 4));
        full.f.set(5, 8, 4), full.f.set(0, 8, 3);
        full.f.set(1, 10, 2), full.f.set(3, 10, 4);
        check(SwitchProbe::roomPairs(full) == 4 && !full.f.hasClear(), "the full field of the test is not what it expects");
        const Pair hand{4, 3}, other{1, 2};
        struct Case {
            int room, minPoke;
            bool othersNext; // the two pairs after the one in hand hold neither colour of it
            const GameField* his;
            int kind;        // the kind expected; -1: neither a poke nor a chain fired first
            const char* what;
        };
        const GameField none{};
        const Case cases[] = {
            {2, 1, false, &none, ai::X3Agent::Poke, "about to choke, a chain that keeps the main chain is not fired as a poke"},
            {2, 4, true, &none, ai::X3Agent::Fire, "about to choke, no poke, an opponent with room: the chain in hand is not fired"},
            {2, 4, false, &none, -1, "about to choke: the chain is fired though a later pair on the screen fires it too"},
            {2, 4, true, &full, -1, "about to choke: the chain is fired first at an opponent whose field is as full"},
            {0, 1, true, &none, -1, "x3ChokeRoom 0: a poke or a chain fired first by the rule that is switched off"},
        };
        for (const Case& c : cases) {
            ai::Params q = p;
            q.x3Poke = 0, q.x3ChokeRoom = c.room, q.x3MinPoke = c.minPoke;
            ai::X3Agent agent(q);
            Round r = situation(rules, full, *c.his, hand);
            if (c.othersNext) pairAt(r, r.player[0].nextPair + 1, other), pairAt(r, r.player[0].nextPair + 2, other);
            const int id = choose(r, agent);
            GameField after = full;
            place(after, placement(id), hand);
            ai::Trial chain;
            ai::runTrialChain(after.f, rules, TM, chain);
            const bool fired = agent.kind == ai::X3Agent::Poke || agent.kind == ai::X3Agent::Fire;
            check(c.kind < 0 ? !fired : agent.kind == c.kind && chain.chains == 1,
                  std::string(c.what) + " (kind " + std::to_string(agent.kind) + ", placement " + std::to_string(id) + ", " + std::to_string(chain.chains) + " waves)");
        }
    }

    // x3Crush: the plans of a player are the same with the switch on, and for every plan that fires, the list the crush
    // reads (PlanSet::keeping) holds one of its slot that keeps as much of the main chain or more (a slot keeps one plan
    // by another rule, and the answer that keeps the main chain can be the one it drops).
    {
        GameField g;
        stairs(g, 0, 3);
        for (int y = 0; y < 3; ++y) g.f.set(5, y, 4);
        const Round r = situation(rules, GameField{}, g, Pair{4, 3});
        const ai::Observation o = ai::observe(r, 0);
        ai::Params on = p;
        on.x3Crush = 12;
        ai::Gaze gaze;
        ai::PlanSet off, with;
        gaze.plans(o, 1, false, 3, p, 7, off);
        gaze.plans(o, 1, false, 3, on, 7, with);
        bool same = off.plans.size() == with.plans.size() && off.keeping.empty();
        for (size_t i = 0; same && i < off.plans.size(); ++i)
            same = off.plans[i].id[0] == with.plans[i].id[0] && off.plans[i].fireDepth == with.plans[i].fireDepth && off.plans[i].sent == with.plans[i].sent &&
                   off.plans[i].mainAfter == with.plans[i].mainAfter;
        check(same, "x3Crush changes the plans of a player");
        int fires = 0;
        for (const ai::Plan& x : with.plans) {
            if (!x.fireDepth) continue;
            ++fires;
            bool covered = false;
            for (const ai::Plan& k : with.keeping)
                covered = covered || (k.fireDepth == x.fireDepth && k.allClear == x.allClear && (x.allClear || ai::attackSize(k.sent) == ai::attackSize(x.sent)) &&
                                      k.mainAfter >= x.mainAfter);
            check(covered, "a plan that fires has no plan of its slot that keeps as much of the main chain in the list of the crush");
        }
        check(fires > 0, "no plan fires in the test of the list of the crush");
    }

    // x3PokeUnanswered: the clear of four that leaves the chain of three standing is a poke at an
    // opponent who has nothing; not at one who answers it with a clear of his own and keeps his
    // chain; and at him too once the own field counts as full (x3PokeFull).
    {
        GameField mine;
        stairs(mine, 0, 3);
        for (int y = 0; y < 3; ++y) mine.f.set(5, y, 4);
        GameField answers;
        stairs(answers, 0, 2);
        for (int y = 0; y < 3; ++y) answers.f.set(5, y, 4);
        const Pair pair{4, 3};
        for (int k = 0; k < 3; ++k) {
            ai::Params q = p;
            q.x3PokeUnanswered = 1, q.x3MinPoke = 1;
            // (these values of the judging of a poke: the default waits for 60 cells on the own field, and the fields are small)
            q.x3PokeFrom = 0, q.x3PokeNet = 0, q.x3KeepMain = 85, q.x3PokeGain = 75, q.xWildChance = 0, q.xWildEvery = 2;
            if (k == 2) q.x3PokeFull = 1;
            ai::X3Agent agent(q);
            Round r = situation(rules, mine, k ? answers : GameField{}, pair);
            const int id = choose(r, agent);
            GameField after = mine;
            place(after, placement(id), pair);
            ai::Trial chain;
            ai::runTrialChain(after.f, rules, TM, chain);
            if (k == 1) check(agent.kind != ai::X3Agent::Poke, "a poke at an opponent who answers it and keeps his chain");
            else check(agent.kind == ai::X3Agent::Poke && chain.chains == 1, std::string(k ? "no poke from a field that counts as full" : "no poke at an opponent who has nothing") +
                                                                                 " (kind " + std::to_string(agent.kind) + ")");
        }
        // x3PokeAt: the opponent who answers it is poked all the same once his field holds that many cells; not below.
        for (int k = 0; k < 2; ++k) {
            ai::Params q = p;
            q.x3PokeUnanswered = 1, q.x3MinPoke = 1, q.x3PokeAt = k ? answers.f.count() + 1 : answers.f.count();
            ai::X3Agent agent(q);
            Round r = situation(rules, mine, answers, pair);
            choose(r, agent);
            check((agent.kind == ai::X3Agent::Poke) == (k == 0), k ? "x3PokeAt below its number of cells" : "no poke at an opponent whose field holds x3PokeAt cells");
        }
        // x3PokeNet: how the gain of a poke is counted (x3PokeGain is the threshold). With a threshold below every
        // gain the same poke is fired however it is counted; counted against the best plan (2) a gain is never
        // above 0; and against an opponent without a chain no reply is a main chain fired at the poke, so 3 chooses as 0.
        {
            int kinds[4];
            for (int net = 0; net < 4; ++net) {
                ai::Params q = p;
                q.x3PokeNet = net, q.x3MinPoke = 1, q.x3PokeGain = -300000000;
                ai::X3Agent agent(q);
                Round r = situation(rules, mine, GameField{}, pair);
                choose(r, agent), kinds[net] = agent.kind;
            }
            check(kinds[0] == ai::X3Agent::Poke, "no poke with a threshold below every gain");
            for (int net = 1; net < 4; ++net)
                check(kinds[net] == kinds[0], "x3PokeNet " + std::to_string(net) + ": no poke with a threshold below every gain (kind " + std::to_string(kinds[net]) + ")");
            ai::Params q = p;
            q.x3PokeNet = 2, q.x3MinPoke = 1, q.x3PokeGain = 1;
            ai::X3Agent never(q);
            Round r = situation(rules, mine, GameField{}, pair);
            choose(r, never);
            check(never.kind != ai::X3Agent::Poke, "x3PokeNet 2: a poke with a gain above 0");
            for (int gainAt : {0, 75, 300}) {
                int kind[2], id[2];
                for (int k = 0; k < 2; ++k) {
                    ai::Params w = p;
                    w.x3PokeNet = k ? 3 : 0, w.x3MinPoke = 1, w.x3PokeGain = gainAt;
                    ai::X3Agent agent(w);
                    Round round = situation(rules, mine, GameField{}, pair);
                    id[k] = choose(round, agent), kind[k] = agent.kind;
                }
                check(kind[0] == kind[1] && id[0] == id[1], "x3PokeNet 3 against an opponent without a chain: another choice than 0 (threshold " + std::to_string(gainAt) + ")");
            }
            // x3PokeFrom, x3PokeMainFrom: the same poke is fired from that many cells and that main chain on, not below.
            int mainNow = 0;
            ai::buildValue(mine, rules, TM, p, &mainNow);
            for (int k = 0; k < 4; ++k) {
                ai::Params w = p;
                w.x3MinPoke = 1, w.x3PokeGain = -300000000;
                if (k < 2) w.x3PokeFrom = mine.f.count() + k;
                else w.x3PokeMainFrom = mainNow + (k - 2);
                ai::X3Agent agent(w);
                Round round = situation(rules, mine, GameField{}, pair);
                choose(round, agent);
                check((agent.kind == ai::X3Agent::Poke) == (k % 2 == 0), std::string(k < 2 ? "x3PokeFrom" : "x3PokeMainFrom") + (k % 2 ? ": a poke below it" : ": no poke at it"));
            }
            // x3PokeMost: a chain that sends more is no poke (with a least number above the most, nothing is one).
            for (int k = 0; k < 2; ++k) {
                ai::Params w = p;
                w.x3PokeGain = -300000000, w.x3MinPoke = k ? 2 : 1, w.x3PokeMost = k ? 1 : 100000;
                ai::X3Agent agent(w);
                Round round = situation(rules, mine, GameField{}, pair);
                choose(round, agent);
                check((agent.kind == ai::X3Agent::Poke) == (k == 0), k ? "x3PokeMost: a poke that sends more" : "x3PokeMost above what the poke sends: no poke");
            }
            // xWildChance: with xWildEvery 1 his next pair always holds the answer, which is the reading without it;
            // and with a threshold below every gain the poke is fired as before.
            for (int everyth : {1, 4}) {
                int kind[2], id[2];
                for (int k = 0; k < 2; ++k) {
                    ai::Params w = p;
                    w.x3MinPoke = 1, w.x3PokeGain = everyth == 1 ? 0 : -300000000, w.xWildEvery = everyth, w.xWildChance = k;
                    ai::X3Agent agent(w);
                    Round round = situation(rules, mine, answers, pair);
                    id[k] = choose(round, agent), kind[k] = agent.kind;
                }
                check(kind[0] == kind[1] && id[0] == id[1], "xWildChance: another choice (xWildEvery " + std::to_string(everyth) + ")");
            }
        }
    }
    std::cout << "switches: the choices of the beam search under garbage, a chain of thirteen fired before a row buries it, the chain kept near a full field, the all clear, fewer key pieces on a fuller field, the poke nobody answers\n";
}

// x3Soon (the placement that builds, by when its chains can be fired), and x3KeepFall and x3KeepFine (the plans kept).
static void testSoonKeep() {
    // x3Soon on a result written by hand. Placement 0 has the largest sum, found with the last pairs; placement 1 a
    // little less, found early; placement 2 fires a chain that ends a line of the search with the pair in hand.
    {
        const Rules rules;
        Round r;
        r.start(rules, 1);
        r.player[0].moves = 10; // (not one of the first pairs of a round, which go almost anywhere)
        const Observation o = ai::observe(r, 0);
        ai::BuildResult b;
        b.depth = 16, b.alive = 0b111, b.best = 0;
        const auto fill = [&](int id, int from, int64_t early, int64_t late) { // `early` from placement `from` on, `late` with the last
            for (int n = 0; n < ai::HORIZONS; ++n) b.sumBy[id][n] = n + 1 >= 16 ? late : n + 1 >= from ? early : 0;
            b.sum[id] = late;
        };
        fill(0, 12, 60000, 600000), fill(1, 3, 500000, 570000), fill(2, 1, 400000, 400000);
        b.pointsBy[2][0] = 80000;
        const auto pick = [&](const ai::Params& p, bool soon = true) {
            SwitchProbe agent(p);
            agent.startRound(0, 7);
            return agent.pickBuild(o, b, 0b111, soon);
        };
        ai::Params off, on;
        on.x3Soon = 500, on.x3SoonFrom = 2;
        check(pick(off) == 0, "without x3Soon: not the placement with the largest sum");
        {
            // (placements 2 to 16: ten of nothing, four of 60000 and the last for placement 0; one of nothing, thirteen of 500000 and the last for placement 1)
            const SwitchProbe agent(on);
            check(agent.soonSum(b, 0) == (600000 * 500 + int64_t(4 * 60000 + 600000) / 15 * 500) / 1000, "x3Soon: the sum of the placement whose chain comes late");
            check(agent.soonSum(b, 1) == (570000 * 500 + int64_t(13 * 500000 + 570000) / 15 * 500) / 1000, "x3Soon: the sum of the placement whose chain comes early");
        }
        check(pick(on) == 1, "x3Soon: not the placement whose chain can be fired early");
        check(pick(on, false) == 0, "x3Soon in a search that is not the one to build with");
        ai::Params narrow = on;
        narrow.x3Soon = 1000, narrow.x3SoonFrom = 13, narrow.x3SoonTo = 15;
        check(SwitchProbe(narrow).soonSum(b, 0) == 60000 && SwitchProbe(narrow).soonSum(b, 1) == 500000, "x3SoonFrom and x3SoonTo: not the mean of those placements");
        // The placement that fires now is never taken for x3Soon, and stays the choice when the sums choose it.
        ai::Params all = on;
        all.x3Soon = 1000, all.x3SoonFrom = 1;
        check(pick(all) == 1, "x3Soon: a placement that fires the main chain now was taken, or the early one was not");
        b.sum[2] = 700000;
        check(pick(off) == 2 && pick(on) == 2 && pick(all) == 2, "x3Soon: the sums choose the placement that fires now, and it is not the choice");
        b.sum[2] = 400000;
        // A search the clock stopped after its first layer has nothing to count: the sums.
        b.cut = true, b.layers = 1;
        check(SwitchProbe(on).soonSum(b, 1) == b.sum[1] && pick(on) == 0, "x3Soon in a search that the clock stopped");
    }

    // x3KeepFall and x3KeepFine on random positions, with and without garbage on its way to the player who plans. The
    // plans are kept as those of the opponent are, not apart by their first placement: garbage that is confirmed falls
    // after the first pair that clears nothing, so only the plans that fire with that pair differ in what they let fall,
    // and apart by first placement there is one of them to a slot. (Plans that fire with a later pair differ in it
    // while the chain that sends the garbage still runs.)
    Rules rules;
    rules.targetPoints = 40; // a clear of four is one garbage piece
    for (int i = 0; i < rules.marginSteps; ++i) rules.marginRates[size_t(i)] = 40;
    Rng rng(20261012, 53);
    ai::Params off, fall1, fall2, fine;
    fall1.x3KeepFall = 1, fall2.x3KeepFall = 2, fine.x3KeepFine = 1;
    ai::Gaze gaze;
    ai::PlanSet usual, one, two, finer;
    const auto same = [](const ai::Plan& a, const ai::Plan& b) {
        if (a.count != b.count) return false;
        for (int k = 0; k < a.count; ++k)
            if (a.id[k] != b.id[k]) return false;
        return true;
    };
    int positions = 0, under = 0, less = 0, extra = 0, split = 0;
    for (int i = 0; i < 720; ++i) {
        Round r;
        r.start(rules, rng.next());
        for (int k = 0; k < 2; ++k) {
            GameField f;
            int h[W];
            do {
                f = randomField(rng, rules, 8 + int(rng.below(12)));
                f.heights(h);
            } while (*std::max_element(h, h + W) > 7);
            r.player[k].field = f;
        }
        PlayerEvents e[2];
        const uint8_t none[2] = {0, 0};
        while (!aboutToSpawn(r.player[0])) r.step(none, e);
        const int coming = i % 3 ? 1 + int(rng.below(12)) : 0;
        r.player[0].incoming.confirmed = coming;
        const Observation o = ai::observe(r, 0);
        gaze.plans(o, 0, false, 2, off, 1, usual);
        gaze.plans(o, 0, false, 2, fall1, 1, one);
        gaze.plans(o, 0, false, 2, fall2, 1, two);
        gaze.plans(o, 0, false, 2, fine, 1, finer);
        ++positions, under += coming > 0;
        const std::string at = " (position " + std::to_string(i) + ")";
        // x3KeepFall 1: the same slots; a plan that fires less than five rows lets no more fall than the one kept before.
        check(one.plans.size() == usual.plans.size(), "x3KeepFall 1: another number of plans" + at);
        int changed = 0;
        for (size_t k = 0; k < usual.plans.size(); ++k) {
            const ai::Plan& a = usual.plans[k];
            const ai::Plan& b = one.plans[k];
            check(a.fireDepth == b.fireDepth && a.allClear == b.allClear && ai::attackSize(a.sent) == ai::attackSize(b.sent),
                  "x3KeepFall 1: the plans are not those of the same slots" + at);
            if (same(a, b)) continue;
            ++changed;
            check(a.fireDepth && !a.allClear && a.sent / W < 5 && b.fallen < a.fallen, "x3KeepFall 1: another plan that does not let less fall" + at);
        }
        less += changed;
        // x3KeepFall 2: the plans kept before, and after them those of x3KeepFall 1 that are others.
        check(int(two.plans.size()) == int(usual.plans.size()) + changed, "x3KeepFall 2: not as many more plans as x3KeepFall 1 changes" + at);
        for (size_t k = 0; k < usual.plans.size(); ++k) check(same(usual.plans[k], two.plans[k]), "x3KeepFall 2: the plans kept before are not all there, in their order" + at);
        for (size_t k = usual.plans.size(); k < two.plans.size(); ++k) {
            bool found = false;
            for (const ai::Plan& b : one.plans) found = found || (same(b, two.plans[k]) && b.fallen == two.plans[k].fallen);
            check(found, "x3KeepFall 2: a plan more that x3KeepFall 1 does not keep" + at);
        }
        extra += int(two.plans.size() - usual.plans.size());
        // With nothing on its way nothing falls, and nothing changes.
        if (!coming) check(changed == 0, "x3KeepFall without garbage on its way: other plans" + at);
        // x3KeepFine: one plan for a firing pair and a finer size; no fewer plans than before.
        check(finer.plans.size() >= usual.plans.size(), "x3KeepFine: fewer plans" + at);
        split += finer.plans.size() > usual.plans.size();
        for (size_t k = 0; k < finer.plans.size(); ++k)
            for (size_t j = 0; j < k; ++j) {
                const ai::Plan& a = finer.plans[j];
                const ai::Plan& b = finer.plans[k];
                check(a.fireDepth != b.fireDepth || a.allClear != b.allClear || (!a.allClear && a.fireDepth && ai::fineSize(a.sent) != ai::fineSize(b.sent)),
                      "x3KeepFine: two plans of one slot" + at);
            }
    }
    // (random fields hold few chains: a plan is changed in one position of thirty or so, 6 of 240 when this was written)
    check(less >= 8 && extra == less && split >= 25, "x3KeepFall and x3KeepFine are not exercised (" + std::to_string(less) + " plans changed, " + std::to_string(extra) +
                                                        " more, finer in " + std::to_string(split) + " positions)");
    std::cout << "the choice by time and the plans kept: x3Soon on a result written by hand; " << positions << " positions (" << under << " with garbage on its way), x3KeepFall changes "
              << less << " plans for ones that let less fall and keeps as many more beside them, x3KeepFine keeps more plans in " << split << " positions\n";
}

// x3Enough (building on against a running chain: the placements below which the most queues fire a chain that is large enough).
static void testEnough() {
    // What a search tells queue by queue (BuildResult::each), on the field of testSearchBy: as many queues as it has,
    // their sum and their largest are sum and points, and no queue tells of a first placement that loses.
    int apart = 0;
    {
        const Rules rules;
        GameField g;
        stairs(g, 0, 3);
        for (int y = 0; y < 2; ++y) g.f.set(4, y, 2), g.f.set(5, y, 3);
        const Pair known[2] = {{3, 4}, {1, 2}};
        ai::Params p;
        p.beamWidth = 30, p.beamDepth = 9, p.beamQueues = 3;
        ai::Builder builder;
        const ai::BuildResult r = builder.search(g, known, 2, rules, TM, p, 5);
        check(r.told == 3, "a search of three queues does not tell of three");
        for (int id = 0; id < PLACEMENTS; ++id) {
            const bool alive = (r.alive >> id & 1) != 0;
            int64_t sum = 0;
            int most = 0, least = -1;
            for (int q = 0; q < r.told; ++q) {
                const int points = r.each[q][id];
                check((points >= 0) == alive, "a queue tells of a first placement that loses, or not of one that does not");
                if (points < 0) continue;
                sum += points;
                most = std::max(most, points), least = least < 0 ? points : std::min(least, points);
            }
            if (!alive) continue;
            check(sum == r.sum[id] && most == r.points[id], "what the queues tell one by one is not what the search adds up");
            check(r.enough(id, int64_t(most) + 1) == 0 && r.enough(id, least) == r.told && (most == 0 || r.enough(id, most) >= 1),
                  "BuildResult::enough does not count the queues that fire a chain of that size");
            apart += most > least;
        }
        // (more queues than are told: the first QUEUES_TOLD of them)
        ai::Params many = p;
        many.beamWidth = 8, many.beamDepth = 4, many.beamQueues = ai::QUEUES_TOLD + 2;
        check(builder.search(g, known, 2, rules, TM, many, 5).told == ai::QUEUES_TOLD, "a search of more queues than are told does not tell of QUEUES_TOLD");
    }
    // The choice on a result written by hand: eight queues and three placements. Below placement 0 three queues fire a
    // chain far larger than is needed and five fire too little - the largest sum. Below placement 1 every queue fires
    // just enough, below placement 2 six do.
    {
        const Rules rules;
        Round r;
        r.start(rules, 1);
        r.player[0].moves = 10; // (not one of the first pairs of a round, which go almost anywhere)
        const Observation o = ai::observe(r, 0);
        ai::BuildResult b;
        b.depth = 8, b.alive = 0b111, b.best = 0, b.told = 8;
        const auto fill = [&](int id, int much, int queues, int little) { // `much` in the first `queues` queues, `little` in the others
            b.sum[id] = 0, b.points[id] = 0;
            for (int q = 0; q < b.told; ++q) {
                const int points = q < queues ? much : little;
                b.each[q][id] = points, b.sum[id] += points;
                if (points > b.points[id]) b.points[id] = points;
            }
        };
        fill(0, 90000, 3, 10000), fill(1, 32000, 8, 0), fill(2, 34000, 6, 20000); // (the sums: 320000, 256000, 244000)
        const ai::Params usual;
        const auto pick = [&](unsigned legal) {
            SwitchProbe agent(usual);
            agent.startRound(0, 7);
            return agent.pickBuild(o, b, legal);
        };
        const int64_t goal = 30000;
        check(b.enough(0, goal) == 3 && b.enough(1, goal) == 8 && b.enough(2, goal) == 6, "BuildResult::enough on the result written by hand");
        check(pick(0b111) == 0, "the sums alone: not the placement with the largest sum");
        check(SwitchProbe::enoughChoices(b, 0b111, goal) == 0b010 && pick(SwitchProbe::enoughChoices(b, 0b111, goal)) == 1,
              "x3Enough: not the placement below which every queue fires enough");
        check(SwitchProbe::enoughChoices(b, 0b101, goal) == 0b100, "x3Enough: among the placements allowed, not the one with the most queues");
        // As many queues below two placements: the sums tell them apart.
        check(SwitchProbe::enoughChoices(b, 0b111, 20000) == 0b110 && pick(SwitchProbe::enoughChoices(b, 0b111, 20000)) == 1,
              "x3Enough: placements with as many queues are not left to the sums");
        // No queue fires enough, or nothing is asked for: every placement allowed, and the sums choose as without the switch.
        check(SwitchProbe::enoughChoices(b, 0b111, 100000) == 0b111 && SwitchProbe::enoughChoices(b, 0b011, 0) == 0b011,
              "x3Enough: the placements are narrowed although no queue fires enough");
        // A first placement that loses is not chosen from, whatever its row of the table holds.
        b.alive = 0b101;
        check(SwitchProbe::enoughChoices(b, 0b111, goal) == 0b100, "x3Enough: a placement that loses is chosen from");
    }
    std::cout << "the choice by the queues that fire enough: a search tells what each queue found (" << apart
              << " first placements below which the queues differ), x3Enough on a result written by hand\n";
}

static void testTactics() {
    Rules rules;
    rules.targetPoints = 40; // a clear of four is one garbage piece
    for (int i = 0; i < rules.marginSteps; ++i) rules.marginRates[size_t(i)] = 40;
    ai::Params p;
    p.beamWidth = 30, p.beamDepth = 8, p.beamQueues = 3;
    // (the positions below are made for these values of the judging of a poke: the default waits for 60 cells on the
    //  own field, and these fields are small. The default values are tested where a test sets them.)
    p.x3PokeFrom = 0, p.x3PokeNet = 0, p.x3KeepMain = 85, p.x3PokeGain = 75, p.xWildChance = 0, p.xWildEvery = 2;
    const auto choose = [&](const Round& r, ai::X3Agent& agent) {
        agent.startRound(0, 7);
        agent.frame(ai::observe(r, 0));
        return agent.lastChoice;
    };
    // The own field: a chain of three on the left (it starts with colour 3 on the
    // floor of column 4), and three of colour 4 in column 6 (one more clears them).
    GameField mine;
    stairs(mine, 0, 3);
    for (int y = 0; y < 3; ++y) mine.f.set(5, y, 4);
    {
        ai::Trials t;
        ai::detect(mine, rules, TM, 3, t);
        check(t.best() && t.best()->chains == 3 && t.best()->x == 3 && t.best()->color == 3 && t.best()->points == 1000, "the test field is not a chain of three");
    }
    const Pair pair{4, 3}; // either the small clear or the chain of three can be fired with it

    // 1. Nothing is happening: the pair is put to build, nothing is fired.
    {
        ai::X3Agent agent(p);
        Round r = situation(rules, mine, GameField{}, pair);
        const int id = choose(r, agent);
        GameField after = mine;
        place(after, placement(id), pair);
        check(agent.kind == ai::X3Agent::Build && !after.f.hasClear(), "with nothing happening something is fired (kind " + std::to_string(agent.kind) + ")");
    }
    // 2. One garbage piece is on its way from a chain that is ending. The small clear would
    //    answer it exactly, but a single piece is better taken: nothing
    //    is fired, and the chain of three stays. With the own third column eleven high that
    //    one piece may close the field: then it is answered.
    for (int high = 0; high < 2; ++high) {
        ai::X3Agent agent(p);
        GameField own = mine;
        if (high)
            for (int y = 4; y < 11; ++y) own.f.set(2, y, Cell(4 - y % 2)); // (colours 4 and 3 in turn: nothing joins, the chain of three is as it was)
        Round r = situation(rules, own, GameField{}, pair);
        r.player[1].phase = Phase::Wave, r.player[1].wait = 30, r.player[1].chain.chains = 1;
        r.player[0].incoming.pending = 1;
        const int id = choose(r, agent);
        GameField after = own;
        place(after, placement(id), pair);
        ai::Trial chain;
        ai::runTrialChain(after.f, rules, TM, chain);
        int main = 0;
        ai::buildValue(after, rules, TM, p, &main);
        const std::string what = " (kind " + std::to_string(agent.kind) + ", placement " + std::to_string(id) + ", " + std::to_string(chain.chains) + " waves)";
        if (high) // (which chain answers is not prescribed: the chain of three also brings the third column down)
            check(agent.kind == ai::X3Agent::Answer && chain.chains >= 1, "with the third column eleven high one garbage piece is taken" + what);
        else
            check(agent.kind == ai::X3Agent::Answer && chain.chains == 0 && main >= 1000, "a single garbage piece is not simply taken" + what);
    }
    // 3. Two rows are on their way: only the chain of three stops them, and it is fired.
    {
        ai::X3Agent agent(p);
        Round r = situation(rules, mine, GameField{}, pair);
        // (his chain ends in five frames: no pair can be put before that. With thirty frames
        //  X3 first puts a pair on the high column 3, which is over in time, and fires a larger chain after it.)
        r.player[1].phase = Phase::Wave, r.player[1].wait = 5, r.player[1].chain.chains = 1;
        r.player[0].incoming.pending = 12;
        const int id = choose(r, agent);
        GameField after = mine;
        place(after, placement(id), pair);
        ai::Trial chain;
        ai::runTrialChain(after.f, rules, TM, chain);
        check(agent.kind == ai::X3Agent::Answer && chain.chains == 3, "two rows of garbage are not answered with the chain of three (kind " +
                                                                           std::to_string(agent.kind) + ", " + std::to_string(chain.chains) + " waves)");
    }
    // 4. The opponent's third column is ten high and he has nothing: the chain of three buries him.
    {
        ai::X3Agent agent(p);
        GameField tall;
        for (int x = 0; x < W; ++x) addGarbage(tall, x, 10);
        Round r = situation(rules, mine, tall, pair);
        // (the placement itself need not fire: a plan that fires with the next pair wins as well)
        choose(r, agent);
        check(agent.kind == ai::X3Agent::Win, "the win is not seen (kind " + std::to_string(agent.kind) + ")");
    }
    // 5. Garbage that nothing can answer is on its way: where the pair goes is left to the
    //    beam search (x3AnswerShape), which does not put it on a column ten high beside the third.
    {
        ai::Params q = p;
        q.x3AnswerShape = 1;
        ai::X3Agent agent(q);
        GameField tower;
        for (int y = 0; y < 10; ++y) tower.f.set(1, y, Cell(1 + y % 2));
        tower.f.set(0, 0, 3), tower.f.set(0, 1, 4), tower.f.set(3, 0, 4), tower.f.set(3, 1, 3);
        const Pair other{3, 4};
        Round r = situation(rules, tower, GameField{}, other);
        r.player[1].phase = Phase::Wave, r.player[1].wait = 5, r.player[1].chain.chains = 1;
        r.player[0].incoming.pending = 6;
        const Placement t = placement(choose(r, agent));
        check(agent.kind == ai::X3Agent::Answer && t.x != 1 && t.childX() != 1,
              "with garbage that cannot be answered the pair goes on the tower (kind " + std::to_string(agent.kind) + ", column " + std::to_string(t.x) + ")");
    }
    // 6. The opponent starts a chain of five and nothing on the own field can be ready in
    //    time: with x3ExtendCheck the short beam search is not asked (its choice means nothing).
    {
        GameField theirs;
        stairs(theirs, 0, 5);
        theirs.f.set(5, 0, 1); // the chain starts
        GameField little;
        little.f.set(0, 0, 1), little.f.set(5, 0, 2);
        for (int on = 0; on < 2; ++on) {
            ai::Params q = p;
            q.x3ExtendCheck = on;
            ai::X3Agent agent(q);
            Round r = situation(rules, little, theirs, Pair{3, 4});
            r.player[1].phase = Phase::Land, r.player[1].wait = 1;
            choose(r, agent);
            check(agent.kind == (on ? ai::X3Agent::Answer : ai::X3Agent::AnswerBuild),
                  "a chain of five that cannot be answered in time: x3ExtendCheck " + std::to_string(on) + " gives kind " + std::to_string(agent.kind));
        }
    }
    // 7. The own field is full, a large chain can be fired now, and the opponent could
    //    build a larger one while it runs (so it is not a "win"): it is fired all the same
    //    (x3FullPercent). The fields are those of a round X3 lost to G1 by building on.
    {
        static const char* const MINE[13] = {".o....", ".Y.B..", ".YBY..", ".RGGB.", "oYoRR.", "ooYGR.", "RBoGG.",
                                             "YBYoYY", "YGRBBG", "YGRGRG", "RRGBoG", "GRGGoo", "GBBBGo"};
        static const char* const THEIRS[5] = {"....YB", ".Y..RR", "YYB.GG", "RBBYGY", "RGYYRY"};
        const auto cell = [](char c) { return c == 'o' ? GARBAGE : c == 'R' ? Cell(1) : c == 'G' ? Cell(2) : c == 'B' ? Cell(3) : c == 'Y' ? Cell(4) : EMPTY; };
        GameField full, low;
        for (int row = 0; row < 13; ++row)
            for (int x = 0; x < W; ++x)
                if (cell(MINE[row][x]) != EMPTY) full.f.set(x, 12 - row, cell(MINE[row][x]));
        for (int row = 0; row < 5; ++row)
            for (int x = 0; x < W; ++x)
                if (cell(THEIRS[row][x]) != EMPTY) low.f.set(x, 4 - row, cell(THEIRS[row][x]));
        const Pair key{4, 1};
        const Rules usual;
        for (int percent = 0; percent <= 50; percent += 50) {
            ai::Params q;
            q.beamWidth = 40, q.beamDepth = 10, q.beamQueues = 3, q.x3FullPercent = percent;
            ai::X3Agent agent(q);
            Round r = situation(usual, full, low, key);
            const int id = choose(r, agent);
            GameField after = full;
            place(after, placement(id), key);
            ai::Trial chain;
            ai::runTrialChain(after.f, usual, TM, chain);
            if (percent) check(agent.kind == ai::X3Agent::Fire && chain.points >= 20000, "the full field is not fired (kind " + std::to_string(agent.kind) + ", " +
                                                                                        std::to_string(chain.points) + " points)");
            else check(agent.kind != ai::X3Agent::Win, "the test of the full field is a plain win");
        }
    }
    // The moves that fit into a stretch of frames (Params::movesIn): with one length of a move the frames
    // divided by it, as before; with three, by the cells on the field, every move adding a pair.
    {
        ai::Params q = p;
        q.x3MoveFrames = 50, q.x3MoveFramesMid = 0, q.x3MoveFramesLate = 0;
        check(q.movesIn(249, 60) == 4 && q.movesIn(250, 0) == 5 && q.movesIn(-10, 0) == 0, "one length of a move: the frames divided by it");
        q.x3MoveFramesMid = 46, q.x3MoveFramesLate = 40, q.x3MoveMidFrom = 24, q.x3MoveLateFrom = 48;
        check(q.movesIn(99, 0) == 1 && q.movesIn(100, 0) == 2, "an emptier field: 50 frames a move");
        check(q.movesIn(191, 20) == 3 && q.movesIn(192, 20) == 4, "20 cells: two moves of 50 frames, then of 46");
        check(q.movesIn(171, 44) == 3 && q.movesIn(172, 44) == 4, "44 cells: two moves of 46 frames, then of 40");
        check(q.movesIn(399, 60) == 9 && q.movesIn(400, 60) == 10 && q.movesIn(-10, 60) == 0, "a full field: 40 frames a move");
        // (the pairs that do not help in the reading of an exchange, Params::wildFrames)
        q.xWildFrames = 50, q.xWildFramesMid = 0, q.xWildFramesLate = 0;
        check(q.wildFrames(1, 60) == 50 && q.wildFrames(3, 0) == 150 && q.wildFrames(0, 60) == 0, "one length of a pair that does not help: as before");
        q.xWildFramesMid = 46, q.xWildFramesLate = 40;
        check(q.wildFrames(1, 10) == 50 && q.wildFrames(1, 30) == 46 && q.wildFrames(1, 60) == 40 && q.wildFrames(3, 44) == 132, "three lengths by the cells on the field");
    }
    // 8. A poke read on (x3PokeRead): the small clear leaves the chain of three standing.
    //    Against an opponent with nothing it is fired. Against one who can fire a chain of
    //    five at it, with no time to build on while that runs, it is not.
    {
        GameField five;
        stairs(five, 0, 5);
        for (int strong = 0; strong < 2; ++strong) {
            ai::Params q = p;
            q.x3PokeRead = 1, q.x3MinPoke = 1;
            // (this rule is tested with these values of the judging of a poke: the fields of this test are small,
            //  and the default waits for 60 cells on the own field)
            q.x3PokeFrom = 0, q.x3PokeNet = 0, q.x3KeepMain = 85, q.x3PokeGain = 75, q.xWildChance = 0, q.xWildEvery = 2;
            if (strong) q.x3MoveFrames = 1000000, q.x3MoveFramesMid = q.x3MoveFramesLate = 0; // (no move fits into his chain)
            ai::X3Agent agent(q);
            Round r = situation(rules, mine, strong ? five : GameField{}, pair);
            const int id = choose(r, agent);
            GameField after = mine;
            place(after, placement(id), pair);
            ai::Trial chain;
            ai::runTrialChain(after.f, rules, TM, chain);
            if (strong) check(agent.kind != ai::X3Agent::Poke, "a poke into a chain of five that cannot be returned");
            else check(agent.kind == ai::X3Agent::Poke && chain.chains == 1, "no poke at an opponent who has nothing (kind " + std::to_string(agent.kind) + ")");
        }
    }
    // 9. A row of garbage is about to fall on a third column eleven high, and nothing can be
    //    fired: every plan ends dead. The pair is not put on the third column all the same (the
    //    two left columns are full, so that is the first legal placement).
    {
        GameField walled;
        for (int x = 0; x < W; ++x)
            for (int y = 0; y < (x < 2 ? 13 : x == 2 ? 11 : 3); ++y) walled.f.set(x, y, Cell(3 + (x + y) % 2));
        const Pair lost{1, 2};
        Round r = situation(rules, walled, GameField{}, lost);
        r.player[0].incoming.confirmed = 6;
        check(std::countr_zero(legalPlacements(walled)) == placementId({2, UP}), "the first legal placement of the test field is not the third column");
        ai::X3Agent agent(p);
        const int id = choose(r, agent);
        check(agent.kind == ai::X3Agent::None, "a field that garbage closes whatever is placed: kind " + std::to_string(agent.kind));
        check(placement(id).x != DEATH_X && placement(id).childX() != DEATH_X, "with every plan dead the pair is put on the third column (placement " + std::to_string(id) + ")");
    }
    std::cout << "moves by kind: build, one piece taken and answered when it could close the field, the answer with the main chain, the win, the answer left to the beam search, no short search, the full field, no plan left\n";
}

// Time of the trial drops on fields of random play.
static void benchDetect() {
    using Clock = std::chrono::steady_clock;
    const Rules rules;
    Rng rng(1, 31);
    std::vector<GameField> fields;
    while (fields.size() < 2000) {
        GameField f;
        const int moves = 10 + int(rng.below(25));
        for (int m = 0; m < moves; ++m) {
            const unsigned legal = legalPlacements(f);
            if (!legal) break;
            int id;
            do id = int(rng.below(PLACEMENTS));
            while (!(legal >> id & 1));
            place(f, placement(id), Pair{rules.palette[rng.below(4)], rules.palette[rng.below(4)]});
            Wave w;
            while (findWave(f.f, w, false)) f.f.erase(w.cleared | w.garbage), f.f.drop();
        }
        if (!f.dead()) fields.push_back(f);
    }
    for (int maxAdd = 1; maxAdd <= 3; ++maxAdd)
        for (int round = 0; round < 3; ++round) {
            ai::Trials t;
            int64_t trials = 0, waves = 0, sum = 0;
            const auto t0 = Clock::now();
            for (int rep = 0; rep < 200; ++rep)
                for (const GameField& f : fields) {
                    trials += ai::detect(f, rules, TM, maxAdd, t);
                    for (int i = 0; i < t.count; ++i) waves += t.item[i].chains, sum += t.item[i].points;
                }
            const double ns = double(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count());
            std::cout << "trial drops, at most " << maxAdd << " added: " << ns / (200.0 * double(fields.size())) << " ns per field, "
                      << double(trials) / (200.0 * double(fields.size())) << " chains started and " << double(waves) / (200.0 * double(fields.size()))
                      << " waves run per field (checksum " << sum << ")\n";
        }
}

// Time of one move in the Duel and with playTurn, one after the other.
static void benchDuel() {
    using Clock = std::chrono::steady_clock;
    const Rules rules;
    for (int round = 0; round < 3; ++round) {
        long duelMoves = 0, turnMoves = 0;
        Rng rng(1, 20);
        const int games = 20000;
        Round r;
        r.start(rules, 1);
        const Observation o = ai::observe(r, 0);
        const auto randomLegal = [&](const GameField& g) {
            const unsigned legal = legalPlacements(g);
            int id = int(rng.below(PLACEMENTS));
            while (!(legal >> id & 1)) id = (id + 1) % PLACEMENTS;
            return placement(id);
        };
        const auto t0 = Clock::now();
        for (int game = 0; game < games; ++game) {
            const Tsumo tsumo = makeTsumo(rules, uint64_t(game));
            ai::Duel d(o, uint64_t(game));
            while (d.state == RoundState::Playing) {
                const int appeared = d.run(INT64_MAX);
                for (int i = 0; i < 2; ++i)
                    if (appeared >> i & 1) d.placeFast(i, randomLegal(d.player[i].field), tsumo.pair(d.player[i].nextPair - 1)), ++duelMoves;
            }
        }
        const auto t1 = Clock::now();
        for (int game = 0; game < games; ++game) {
            const Tsumo tsumo = makeTsumo(rules, uint64_t(game));
            PlayerCore core[2];
            Rng garbage(uint64_t(game), 1);
            int64_t frame = 0;
            for (int move = 0;; ++move) {
                PlayerCore& me = core[move & 1];
                if (me.field.dead()) break;
                frame += playTurn(me, core[1 - (move & 1)].incoming, randomLegal(me.field), tsumo.pair(move / 2), rules, TM, frame, garbage).frames;
                ++turnMoves;
            }
        }
        const auto t2 = Clock::now();
        const double duelNs = double(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        const double turnNs = double(std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count());
        std::cout << "duel: " << duelMoves << " moves, " << duelNs / double(duelMoves) << " ns per move (tsumo and duel made " << games
                  << " times included); playTurn: " << turnMoves << " moves, " << turnNs / double(turnMoves) << " ns per move\n";
    }
}

// The answers a field holds, and the chance that its player has the pieces for one (ai/answer.h).
static void testAnswer() {
    // Unseen pieces, four colours: none of two has one colour (9/16), or one of two colours (1/4); one of two has it (7/16), both (1/16).
    check(ai::chanceNone(1, 2, 4) == 562500 && ai::chanceNone(2, 2, 4) == 250000 && ai::chanceNone(4, 1, 4) == 0 && ai::chanceNone(1, 0, 4) == ai::CHANCE_SURE,
          "the chance that no unseen piece has one of the colours");
    check(ai::chanceAtLeast(1, 2, 4) == 437500 && ai::chanceAtLeast(2, 2, 4) == 62500 && ai::chanceAtLeast(0, 0, 4) == ai::CHANCE_SURE && ai::chanceAtLeast(3, 2, 4) == 0,
          "the chance of so many unseen pieces of one colour");
    // Three red pieces on the floor: one red piece more clears them, 40 points, and nothing is left.
    static const char* const ROWS[1] = {"RRR..."};
    const GameField field = fieldOf(ROWS, 1);
    const Rules rules;
    const ai::Params p;
    ai::Replies r;
    ai::repliesOf(field, rules, DEFAULT_TIMING, p, r);
    bool red = r.count > 0;
    for (int i = 0; i < r.count; ++i) red = red && r.item[i].color == 1 && r.item[i].added == 1 && r.item[i].points == 40 && r.item[i].mainAfter == 0;
    check(red, "the answers of three red pieces on the floor: one red piece each, 40 points, " + std::to_string(r.count) + " found");
    const Pair withRed{1, 2}, without{2, 3};
    check(ai::answerChance(r, 40, 0, 1, &withRed, 1, rules) == ai::CHANCE_SURE, "a red piece in the pair on the screen: the answer is sure");
    check(ai::answerChance(r, 40, 0, 1, &without, 1, rules) == 0, "no red piece in the only pair he places: no answer");
    check(ai::answerChance(r, 40, 0, 1, nullptr, 0, rules) == 437500, "one unseen pair: a red piece in it 7 times of 16");
    check(ai::answerChance(r, 40, 0, 2, &without, 1, rules) == 437500, "a pair without and one unseen pair: 7 times of 16");
    check(ai::answerChance(r, 40, 0, 3, &without, 1, rules) == ai::CHANCE_SURE - ai::chanceNone(1, 4, 4), "a pair without and two unseen pairs: one of four pieces");
    check(ai::answerChance(r, 41, 0, 3, &withRed, 1, rules) == 0, "an attack larger than any chain of the field: no answer");
    check(ai::answerChance(r, 40, 1, 3, &withRed, 1, rules) == 0, "a main chain to be kept that the answer does not leave: no answer");
    check(ai::answerChance(r, 40, 0, 0, &withRed, 1, rules) == 0, "no pair placed in time: no answer");
    // Two colours each of which makes an answer: the chance that either has enough, not the larger of the two.
    ai::Replies two;
    two.count = 2;
    two.item[0] = {40, 0, 3, 0, 1, 0};
    two.item[1] = {40, 0, 3, 1, 2, 0};
    check(ai::answerChance(two, 40, 0, 3, nullptr, 0, rules) == 333984, "three red or three blue among six unseen pieces: 1,368 times of 4,096, "
          + std::to_string(ai::answerChance(two, 40, 0, 3, nullptr, 0, rules)));
    two.item[0].added = 1;
    two.item[1].added = 2;
    check(ai::answerChance(two, 40, 0, 2, nullptr, 0, rules) == 812500, "one red or two blue among four unseen pieces: 208 times of 256, "
          + std::to_string(ai::answerChance(two, 40, 0, 2, nullptr, 0, rules)));
    two.count = 3;
    two.item[0].added = 2;
    two.item[2] = {40, 0, 2, 2, 3, 0};
    check(ai::answerChance(two, 40, 0, 2, nullptr, 0, rules) == 714844, "two red, two blue or two green among four unseen pieces: 183 times of 256, "
          + std::to_string(ai::answerChance(two, 40, 0, 2, nullptr, 0, rules)));
    // The colours in play are those of the palette, whatever their codes: four of them with codes 1, 2, 3 and 5
    // (as the real game's rounds have them) give the chance of four with codes 1 to 4.
    Rules gapped;
    gapped.palette = {1, 2, 3, 5, 4};
    ai::Replies apart;
    apart.count = 2;
    apart.item[0] = {40, 0, 3, 0, 1, 0};
    apart.item[1] = {40, 0, 3, 1, 2, 0};
    const int64_t inOrder = ai::answerChance(apart, 40, 0, 3, nullptr, 0, rules);
    apart.item[0].color = 2, apart.item[1].color = 5;
    const int64_t withGap = ai::answerChance(apart, 40, 0, 3, nullptr, 0, gapped);
    check(inOrder == 333984 && withGap == 333984, "three of colour 2 or three of colour 5 among six unseen pieces of colours 1, 2, 3 and 5: "
          "1,368 times of 4,096, " + std::to_string(inOrder) + " and " + std::to_string(withGap));
    check(ai::largestAnswer(r, 0) == 40 && ai::largestAnswer(r, 1) == 0, "the largest answer, and the largest that leaves a main chain");
}

// The coloured pieces a plan clears (Plan::used): a piece left in the 14th row is not cleared.
static void testUsedRow14() {
    // Column 1 twelve high, column 2 eleven high (no piece of the one touches one of the same colour in the other),
    // three red pieces on the floor of column 5.
    GameField mine;
    for (int y = 0; y < 12; ++y) mine.f.set(0, y, Cell(1 + y % 2));
    for (int y = 0; y < 11; ++y) mine.f.set(1, y, Cell(3 + y % 2));
    for (int y = 0; y < 3; ++y) mine.f.set(4, y, 1);
    const Pair reds{1, 1};
    const int high = placementId({0, UP}), low = placementId({4, UP});
    GameField put = mine;
    juraic::place(put, placement(high), reds);
    check(!mine.f.hasClear() && (legalPlacements(mine) >> high & 1) && put.row14[0] == 1 && !put.f.hasClear(),
          "the test field is not what the test expects: a pair stood on the twelve-high column leaves its child in the 14th row");
    check(ai::colouredPieces(mine) == 12 + 11 + 3 && ai::colouredPieces(put) == ai::colouredPieces(mine) + 2 &&
          put.f.count() - put.f.garbage().count() == ai::colouredPieces(mine) + 1,
          "the coloured pieces of a field count the one in the 14th row");
    Rules rules;
    rules.targetPoints = 10;
    for (int i = 0; i < rules.marginSteps; ++i) rules.marginRates[size_t(i)] = 10;
    const Round r = situation(rules, mine, GameField{}, reds);
    const ai::Observation o = ai::observe(r, 0);
    const ai::Params p;
    ai::Gaze gaze;
    ai::PlanSet one, two;
    gaze.plans(o, 0, true, 1, p, 7, one);
    bool stood = false, fired = false;
    for (const ai::Plan& s : one.plans) {
        if (s.count == 1 && s.id[0] == high && !s.fireDepth) {
            stood = true;
            check(s.used == 0, "a pair that clears nothing and leaves its child in the 14th row has used " + std::to_string(s.used) + " pieces, not 0");
        }
        if (s.count == 1 && s.id[0] == low && s.fireDepth == 1) {
            fired = true;
            check(s.used == 5, "five red pieces cleared on the floor: " + std::to_string(s.used) + " used, not 5");
        }
    }
    check(stood && fired, "the plans of one pair hold the pair stood on the high column and the five reds cleared");
    gaze.plans(o, 0, true, 2, p, 7, two);
    int later = 0;
    for (const ai::Plan& s : two.plans) {
        if (s.count != 2 || s.id[0] != high || s.fireDepth != 2 || s.sent < W) continue;
        ++later;
        check(s.used == 5, "the pair stood on the high column, then five reds cleared: " + std::to_string(s.used) + " used, not 5");
    }
    check(later > 0, "the plans of two pairs hold the pair stood on the high column, then five reds cleared");
    std::cout << "the pieces a plan clears: a child left in the 14th row is not one of them\n";
}

static void testSetupSpeed() {
    ai::Observation o;
    o.player[0].phase = o.player[1].phase = Phase::Next;
    o.player[0].wait = o.player[1].wait = 1;
    for (int y = 0; y < 4; ++y) o.player[0].field.f.set(2, y, GARBAGE);
    ai::PlanSet mine;
    mine.pairCount = mine.knownCount = 1;
    mine.pair[0] = {1, 2};
    ai::Params p = ai::levelParams(ai::Params{}, 37);
    ai::BuildResult b;
    constexpr int torn = 8, upright = 2;
    constexpr unsigned choices = (1u << torn) | (1u << upright);
    b.alive = choices;
    b.sum[torn] = 10000;
    b.sum[upright] = 9900;
    const auto pick = [&](unsigned mask) { return ai::pickFasterSetup(o, mine, b, torn, mask, p, [] { return false; }); };
    check(pick(choices) == upright, "a nearby upright build frees the next pair earlier");
    b.sum[upright] = 9000;
    check(pick(choices) == torn, "a faster path cannot override the main chain margin");
    b.sum[upright] = 9900;
    check(pick(1u << torn) == torn, "a faster build cannot bypass the tactical mask");
    check(ai::pickFasterSetup(o, mine, b, torn, choices, p, [] { return true; }) == torn, "keep the original build at the deadline");
    b.cut = true;
    check(pick(choices) == torn, "a partial beam cannot certify comparable main chains");
    b.cut = false;
    p = ai::levelParams(ai::Params{}, 36);
    check(pick(choices) == torn && !p.equivalentSetupRoute, "legacy level retains the original route and build");
    p.setupSpeedNear = 0;
    p.setupSideNear = 5;
    p = ai::levelParams(p, 37);
    check(p.setupSpeedNear == 0 && p.setupSideNear == 5, "explicit setup settings survive level selection");
}

static void testMidgameAttackRetention() {
    ai::Plan quick;
    quick.count = quick.fireDepth = 3;
    quick.endFrame = 120;
    quick.sent = 25;
    quick.mainAfter = 3000;
    ai::Plan reserve = quick, strong = quick;
    reserve.id[2] = 1;
    reserve.endFrame = 150;
    reserve.mainAfter = 4500;
    strong.id[2] = 2;
    strong.endFrame = 165;
    strong.sent = 41;
    ai::MidgameAttacks bucket;
    for (const auto& plan : {quick, reserve, strong, strong}) bucket.consider(plan);
    std::vector<ai::Plan> plans;
    bucket.appendTo(plans);
    check(plans.size() == 3 && plans[2].sent == 41, "a stronger offset survives a faster and a better-preserved attack");
    bucket = {};
    bucket.consider(quick);
    plans.clear();
    bucket.appendTo(plans);
    check(plans.size() == 1, "the same plan is appended only once");
}
static void testMidgamePreparation() {
    ai::Observation o;
    o.rules.colors = 5;
    o.rules.targetPoints = 20;
    o.rules.softDropBonus = false;
    for (int i = 0; i < o.rules.marginSteps; ++i) o.rules.marginRates[size_t(i)] = 20;
    o.ownTsumo = true;
    o.tsumo.pairs = {{4, 5}, {1, 2}};
    o.tsumo1.pairs = {{1, 2}, {3, 4}};
    o.player[0].phase = o.player[1].phase = Phase::Next;
    o.player[0].wait = o.player[1].wait = 1;
    GameField& own = o.player[0].field;
    stairs(own, 0, 2);
    for (int k = 0; k < 3; ++k) {
        const int x = 5 - k;
        for (int y = 0; y < 3; ++y) own.f.set(x, y, Cell(3 + k));
        if (k) own.f.set(x, 3, Cell(2 + k));
    }
    for (int y = 0; y < 6; ++y) own.f.set(2, y, GARBAGE);

    ai::Params p = ai::levelParams(ai::Params{}, 37);
    p.setupSideNear = 20;
    ai::Gaze gaze;
    ai::PlanSet mine, theirs;
    gaze.plans(o, 1, false, 2, p, 7, theirs);
    gaze.plans(o, 0, true, 2, p, 7, mine);
    constexpr unsigned blocked = 1u << 0, prepared = 1u << 2, both = blocked | prepared;
    ai::BuildResult built;
    built.alive = both;
    built.sum[0] = 1000;
    built.sum[2] = 990;
    const auto keep = [&] {
        return ai::preparationChoices(o, mine, theirs, built, 0, both, p, 7, [] { return false; });
    };
    check(ai::quietSetupChoices(o, mine, both) == both, "both setup placements leave the first pair quiet");
    check(keep() == prepared, "a nearby build keeps a concrete side fire and the larger main chain");
    built.sum[2] = 970;
    check(keep() == both, "side fire cannot buy a build outside the main-chain margin");
    built.sum[2] = 990;
    check(ai::preparationChoices(o, mine, theirs, built, 0, both, p, 7, [] { return true; }) == 0,
          "an interrupted preparation read does not narrow the build");
    o.tsumo.pairs.resize(1);
    gaze.plans(o, 0, true, 2, p, 7, mine);
    check(keep() == both, "the unseen second pair cannot justify preparing a fire");
    // Open the approach to the right-hand staircase. Its leftmost column
    // starts with colour 5, which the second pair adds beside that column.
    for (int y = 0; y < 6; ++y) own.f.set(2, y, EMPTY);
    o.tsumo.pairs = {{3, 4}, {5, 5}};
    gaze.plans(o, 0, true, 2, p, 7, mine);
    check(std::any_of(mine.plans.begin(), mine.plans.end(), [](const ai::Plan& plan) {
        return plan.id[0] == 2 && plan.fireDepth == 2 && plan.chains == 3 && plan.sent >= W;
    }), "the known pair can fire the main chain");
    ai::Plan main;
    main.count = 2;
    main.id[0] = main.id[1] = 2;
    const ai::Duel fired = ai::playOut(o, mine, &main, theirs, nullptr, p, 7, true);
    check(fired.player[0].moves == o.self().moves + 2 && fired.player[0].chain.chains == 3 && fired.player[1].incoming.total() >= W,
          "the main chain has concrete input paths");
    check(keep() == both, "the fire that consumes the main chain is not a preparation advantage");
}

static void testEquivalentSetupRoute() {
    constexpr int wanted = 7, reversed = 18;
    const Pair mono{1, 1};
    const Rules rules;
    const GameField field;
    const PairControl from = spawnPair(TM);
    ai::InputPlanner planner;
    ai::InputPath original, faster;
    const auto choose = [&](const GameField& g, const Rules& r, Pair pair, int target, unsigned mask) {
        check(planner.planEquivalent(g, from, target, mask, pair, TM, r, faster), "the placement has an input route");
        return target;
    };
    const unsigned legal = legalPlacements(field);
    check(planner.plan(field, from, placement(wanted), TM, rules.speedPercent, original), "the original orientation is reachable");
    check(choose(field, rules, mono, wanted, legal) == reversed && replays(field, from, placement(reversed), faster),
          "the alternate orientation is replayable");

    struct Playback {
        GameField field;
        int score, lock, next;
    };
    const auto play = [&](const ai::InputPath& path) {
        Round round;
        round.start(rules, 1, 1);
        round.tsumo.piece[0] = mono.axis, round.tsumo.piece[1] = mono.child;
        PlayerEvents events[2];
        const uint8_t idle[] = {0, 0};
        while (!aboutToSpawn(round.player[0])) round.step(idle, events);
        int frames = 0, lock = -1;
        do {
            const uint8_t buttons[] = {frames < path.frames() ? path.buttons[size_t(frames)] : uint8_t(0), 0};
            round.step(buttons, events);
            if (events[0].locked) lock = frames;
            ++frames;
        } while (!aboutToSpawn(round.player[0]) && frames < 100);
        const Player& player = round.player[0];
        return Playback{player.field, player.scoring.score, lock, frames};
    };
    const Playback base = play(original), fast = play(faster);
    check(base.field == fast.field && base.score == fast.score && fast.lock == faster.frames() - 1 &&
              fast.lock < base.lock && fast.next < base.next,
          "the alternate locks and exposes the next pair earlier with the same field and score");
    check(choose(field, rules, {1, 2}, wanted, legal) == wanted && faster.buttons == original.buttons,
          "different colours retain the selected orientation");
    check(choose(field, rules, mono, wanted, 1u << wanted) == wanted, "an unavailable alternate cannot replace the original");
    check(choose(field, rules, mono, wanted, 1u << reversed) == reversed, "the valid alternate preserves an otherwise unavailable placement");

    int vertical = 2;
    check(choose(field, rules, mono, vertical, legal) == vertical, "a faster orientation cannot give up scored soft-drop points");
    Rules noSoftBonus = rules;
    noSoftBonus.softDropBonus = false;
    check(choose(field, noSoftBonus, mono, vertical, legal) == ai::reversePlacement(vertical),
          "unscored soft-drop points do not constrain the faster orientation");

    GameField splitField;
    addGarbage(splitField, 1, 10);
    addGarbage(splitField, 2, 9);
    check(choose(splitField, rules, mono, wanted, legalPlacements(splitField)) == wanted,
          "the faster lock must also account for the alternate split animation");

    GameField firingField;
    firingField.f.set(1, 0, 1), firingField.f.set(1, 1, 1);
    check(choose(firingField, rules, mono, wanted, legalPlacements(firingField)) == wanted,
          "an immediate chain retains the original timing");

    class FixedSetupAgent final : public ai::PlacementAgent {
    public:
        bool build = true;
        int choose(const Observation&, Pair, unsigned) override { return 7; }
        bool setupChoice() const override { return build; }
    };
    const auto target = [&](const Observation& o, bool enabled = true, bool build = true) {
        FixedSetupAgent agent;
        agent.equivalentSetupRoute = enabled;
        agent.build = build;
        agent.startRound(0, 1);
        agent.frame(o);
        return agent.targetPlacement();
    };
    Observation o;
    o.player[0].wait = 1;
    o.tsumo.pairs.push_back(mono);
    check(target(o) == reversed && target(o, false) == wanted && target(o, true, false) == wanted,
          "only enabled setup decisions route to the faster orientation");
    o.player[0].incoming.pending = 1;
    check(target(o) == wanted, "pending garbage preserves the original route");
    o.player[0].incoming.pending = 0;
    o.player[1].phase = Phase::Land;
    for (int y = 0; y < 4; ++y) o.player[1].field.f.set(0, y, 2);
    check(target(o) == wanted, "an opponent about to start a chain preserves the original route");

    const auto lv36 = ai::makeAgent("lv36"), lv37 = ai::makeAgent("lv37");
    const ai::X3Agent direct(ai::levelParams(ai::Params{}, 37));
    check(!dynamic_cast<const ai::PlacementAgent&>(*lv36).equivalentSetupRoute &&
              dynamic_cast<const ai::PlacementAgent&>(*lv37).equivalentSetupRoute && direct.equivalentSetupRoute &&
              dynamic_cast<const ai::PlacementAgent&>(*lv36).holdDown == 1 && direct.holdDown == 1,
          "both Lv37 construction paths enable reversal and preserve Lv36 and holdDown");
}

static void testGazeDeadline() {
    GameField field, rival;
    stairs(field, 0, 2);
    const auto o = ai::observe(situation(Rules{}, field, rival, Pair{1, 2}), 0);
    ai::Params p = ai::levelParams({}, 37);
    ai::Gaze gaze;
    ai::PlanSet mine, theirs;
    gaze.plans(o, 0, true, 2, p, 7, mine);
    gaze.plans(o, 1, false, 2, p, 7, theirs);
    const auto same = [](const ai::PlanSet& a, const ai::PlanSet& b) {
        const auto equal = [](const auto& x, const auto& y) {
            return x.size() == y.size() && std::equal(x.begin(), x.end(), y.begin(), [](const ai::Plan& l, const ai::Plan& r) {
                return ai::samePlacements(l,r) && l.points == r.points && l.sent == r.sent && l.endFrame == r.endFrame;
            });
        };
        return a.pairCount == b.pairCount && a.knownCount == b.knownCount && equal(a.plans,b.plans) && equal(a.tactical,b.tactical);
    };
    const auto shallowMine = mine, shallowTheirs = theirs;
    check(!gaze.tryThirdPair(o,p,7,mine,theirs,ai::Builder::Clock::now()) && same(mine,shallowMine) && same(theirs,shallowTheirs),
          "a deadline cannot publish a partial set of opponent replies");
    check(gaze.tryThirdPair(o,p,7,mine,theirs,ai::Builder::Clock::time_point::max()), "the extra ply can finish");
    ai::PlanSet fullMine, fullTheirs;
    gaze.plans(o, 0, true, 3, p, 7, fullMine);
    gaze.plans(o, 1, false, 3, p, 7, fullTheirs);
    check(same(mine,fullMine) && same(theirs,fullTheirs), "a finished reading retains the ordinary plans of both seats");
}
static void testMidgameDig() {
    static const char* const rowsP1[] = {"......", "o.....", "Y..ooR", "RoBGRR", "RYBGBB", "RYoGBY", "BYGBRY", "YGGBBR", "YYRYRR", "GBRRBB", "GBYYYB", "GRRGGG", "BBBYYY"};
    static const char* const rowsP2[] = {"....YR", "....RY", "....BR", "....YR", "....BR", ".B..BY", "BBGBRY", "RRGBBR", "RGRYRR", "GBRRBB", "GBYYYB", "GRRGGG", "BBBYYY"};
    ai::Observation o;
    o.frame = 1839;
    o.ownTsumo = true;
    o.tsumo.pairs = {{1,4}, {1,3}, {3,4}};
    o.tsumo1.pairs = {{1,1}, {1,4}};
    o.player[0].field = fieldOf(rowsP1, 13);
    o.player[1].field = fieldOf(rowsP2, 13);
    for (auto& player : o.player) { player.phase = Phase::Next; player.moves = 43; }
    o.player[0].wait = 1;
    o.player[1].wait = 13;
    o.player[0].scoring.score = 1302;
    o.player[1].scoring.score = 1675;
    ai::Params p = ai::levelParams({}, 37);
    p.beamWidth = 40;
    p.beamDepth = 16;
    p.beamQueues = 8;
    p.searchThreads = 1;
    p.thinkBudget = 0;
    struct Probe : ai::X3Agent { using X3Agent::X3Agent; using X3Agent::choose; };
    Probe probe(p);
    probe.startRound(0, 1);
    const int first = probe.choose(o, o.tsumo.pair(0), legalPlacements(o.self().field));
    check(first == 21, "the narrow beam finds the chain beyond a small digging clear");
}

static void testSmallReceive() {
    static const char* const rows[] = {"YY....", "BGRG..", "GRRB..", "GBYRGB", "GRYGRB", "BBBGYG",
                                      "RRGYRR", "YRGGYB", "BBRRYB", "BGRBBY", "YYYRRY"};
    ai::Observation o;
    o.ownTsumo = true;
    o.tsumo.pairs = {{1, 4}, {1, 4}, {1, 4}};
    o.tsumo1.pairs = {{1, 2}, {3, 4}};
    o.player[0].phase = o.player[1].phase = Phase::Next;
    o.player[0].wait = 1;
    o.player[1].wait = 150;
    o.player[0].field = fieldOf(rows, int(std::size(rows)));
    o.player[0].incoming.confirmed = 3;
    ai::Params p = ai::levelParams(ai::Params{}, 37);
    ai::Gaze gaze;
    ai::PlanSet mine, theirs;
    const auto read = [&] {
        gaze.plans(o, 0, true, 1, p, 7, mine);
        gaze.plans(o, 1, false, 1, p, 7, theirs);
    };
    const unsigned legal = legalPlacements(o.self().field);
    const auto receive = [&](unsigned choices = 0, int preferred = -1) {
        const auto fire = std::find_if(mine.plans.begin(), mine.plans.end(), [](const ai::Plan& s) {
            return s.fireDepth == 1 && s.id[0] == 8;
        });
        check(fire != mine.plans.end() && fire->chains == 13 && !fire->allClear, "small receive fixture has an immediate 13-chain");
        return ai::smallReceive(o, mine, theirs, *fire, choices ? choices : legal, o.self().incoming.confirmed, p, 7,
                                [] { return false; }, preferred);
    };
    read();
    check(receive() == 6, "a quiet placement leaves a larger chain under all 20 three-garbage drops");
    check(receive(0, 1) == 6, "an unsafe quiet ordinary answer gives way to the robust receive");
    check(receive(0, 12) == 12, "a quiet ordinary answer that already passes the proof is kept");
    check(receive((1u << 8) | (1u << 1)) < 0, "a placement which depends on the drop columns is rejected");
    o.nextGarbageOrder[0] = std::array<uint8_t, W>{0, 1, 3, 2, 4, 5};
    check(receive((1u << 8) | (1u << 1)) == 1, "the same placement works for an actually observed favorable order");
    o.nextGarbageOrder[0].reset();

    o.player[1].field = o.player[0].field;
    o.tsumo1.pairs = {{1, 2}, {1, 2}};
    read();
    check(receive() < 0, "a stronger visible opponent response rules out taking the small attack");
    o.player[1].field = {};
    o.tsumo1.pairs = {{1, 2}, {3, 4}};
    o.player[0].incoming.confirmed = 1;
    o.tsumo.pairs = {{1, 4}, {4, 4}, {1, 1}};
    read();
    check(receive() == 3, "a side chain can return the garbage and leave the main on the third visible pair");
    o.tsumo.pairs.pop_back();
    read();
    check(receive() < 0, "the side chain is not selected when the main would depend on an unseen pair");

    o.tsumo.pairs = {{1, 4}, {1, 4}};
    o.tsumo1.pairs.pop_back();
    read();
    check(receive() < 0, "taking garbage requires two observed opponent pairs");
    o.tsumo1.pairs.push_back({3, 4});
    p.readMidgame = false;
    read();
    check(receive() < 0, "small receive is disabled without the Lv37 midgame setting");
    p.readMidgame = true;
    read();
    const auto fire = std::find_if(mine.plans.begin(), mine.plans.end(), [](const ai::Plan& s) { return s.fireDepth == 1 && s.id[0] == 8; });
    check(fire != mine.plans.end() && ai::smallReceive(o, mine, theirs, *fire, legal, 1, p, 7, [] { return true; }) < 0,
          "small receive falls back when the deadline is reached");
    struct Probe : ai::X3Agent { using X3Agent::X3Agent; using X3Agent::decide; };
    o.player[1].field = o.player[0].field;
    o.player[1].wait = 22;
    o.tsumo.pairs = {{1,4},{1,4},{1,4}};
    o.tsumo1.pairs = {{2,2},{2,2},{1,2}};
    for (int garbage : {3, 6}) {
        Probe probe(p);
        probe.startRound(0, 7);
        o.player[0].incoming.confirmed = garbage;
        check(probe.decide(o, legal) == 6, "regular Lv37 chooses the quiet answer safe for each garbage drop");
    }
}

// The small take of the provisional Lv38 (Params::lv38TakeSmall): the pieces of a drop beyond whole rows may fall on
// any column, the third too, unless the order of the drop is seen.
static void testLv38TakeSmallNoDeath() {
    struct Probe : ai::X3Agent {
        using X3Agent::X3Agent;
        int take(const ai::Observation& o, int ordinary, unsigned legal) {
            gaze.plans(o, o.me, true, 1, p, 7, mine);
            return smallTake(o, ordinary, legal, true).best.id;
        }
    };
    // A chain of five that a pair of colour 1 on the sixth column fires; the third column nine high.
    static const char* const rows[] = {"..Y...", "..R...", "..Y...", "..R...", "..Y...", ".RGBY.", "RGBYR.", "RGBYR.", "RGBYR."};
    ai::Observation o;
    o.ownTsumo = true;
    o.tsumo.pairs = {{1, 1}, {2, 3}, {4, 2}};
    o.tsumo1.pairs = {{2, 3}, {4, 2}};
    o.player[0].phase = o.player[1].phase = Phase::Next;
    o.player[0].wait = 1;
    o.player[1].wait = 150;
    o.player[0].field = fieldOf(rows, int(std::size(rows)));
    o.player[0].incoming.confirmed = 1;
    const GameField field = o.self().field;
    int heights[W];
    field.heights(heights);
    check(heights[DEATH_X] == 9, "small take fixture: the third column is nine high");
    const unsigned legal = legalPlacements(field);
    int ordinary = -1, tall = -1;
    for (int id = 0; id < PLACEMENTS; ++id) {
        if (!(legal >> id & 1)) continue;
        GameField f = field;
        place(f, placement(id), Pair{1, 1});
        int h[W];
        f.heights(h);
        ai::Trial chain;
        ai::runTrialChain(f.f, o.rules, o.timing, chain, false);
        if (chain.chains >= 5 && ordinary < 0) ordinary = id;
        if (h[DEATH_X] == 11 && !chain.chains) tall = id;
    }
    check(ordinary >= 0 && tall >= 0, "small take fixture: a chain of five and a vertical pair on the third column");
    if (ordinary < 0 || tall < 0) return;
    const unsigned choices = (1u << ordinary) | (1u << tall);
    const ai::Params params = ai::agentParams("lv38ex", {});
    check(params.lv38TakeSmall == 1, "the explicit lv38ex takes small attacks");
    for (uint64_t seed = 1; seed <= 12; ++seed) {
        Probe probe(params);
        probe.startRound(0, seed);
        check(probe.take(o, ordinary, choices) != tall,
              "a small take that leaves the third column eleven high is taken while the drop columns are unknown (seed " + std::to_string(seed) + ")");
    }
    o.nextGarbageOrder[0] = std::array<uint8_t, W>{0, 1, 2, 3, 4, 5};
    Probe first(params);
    first.startRound(0, 1);
    check(first.take(o, ordinary, choices) == tall, "the same small take is not kept when the piece is seen to fall on the first column");
    o.nextGarbageOrder[0] = std::array<uint8_t, W>{2, 0, 1, 3, 4, 5};
    Probe third(params);
    third.startRound(0, 1);
    check(third.take(o, ordinary, choices) != tall, "the same small take is kept when the piece is seen to fall on the third column");
}

// secondOffsets (x3Crush): a chain standing on his field is counted from the lock of the crush, as Plan::endFrame.
static void testStandingSpare() {
    struct Probe : ai::X3Agent {
        using X3Agent::standingSpare;
    };
    ai::Plan crush;
    crush.lockFrame = 1000, crush.endFrame = 1200;
    // (180 frames of waves end 20 frames before the own next pair, counted without the landing and the next pair)
    const int64_t spare = Probe::standingSpare(crush, 180, TM);
    check(spare == 180 + TM.landFrames + TM.nextFrames + 1 - 200,
          "a chain standing on his field is not counted as a plan's endFrame - lockFrame (" + std::to_string(spare) + ")");
    check(spare > 0, "his standing chain that ends after the own next pair appears is read as ending before it");
}

// exchangePoke (2): the frames to build on while his main chain runs, and what falls before the own main chain fires.
static void testExchangeSpare() {
    struct Probe : ai::X3Agent {
        using X3Agent::exchangeSpare;
        using X3Agent::fallsFirst;
    };
    ai::Plan poke;
    poke.lockFrame = 1000, poke.endFrame = 1200, poke.sent = 6;
    // His plan on the screen: 250 frames from its lock at 900 to 1150, 50 frames before the own next pair.
    check(Probe::exchangeSpare(poke, 250, 1150) == -50, "a plan of his on the screen is counted from the lock of the poke, not from its own");
    // A trial drop of 250 frames, taken to lock with the poke.
    check(Probe::exchangeSpare(poke, 250, -1) == 50, "a trial drop is not taken to lock with the poke");
    const int rate = 70;
    check(Probe::fallsFirst(poke, -50, (6 + W) * rate, rate), "a row beyond the poke that falls before the own next pair is not refused");
    check(Probe::fallsFirst(poke, 0, (6 + W) * rate, rate), "a chain of his that ends as the own next pair appears is read as ending after it");
    check(!Probe::fallsFirst(poke, -50, (6 + W - 1) * rate, rate), "less than a row beyond the poke is refused");
    check(!Probe::fallsFirst(poke, 1, (6 + W) * rate, rate), "a chain of his that ends after the own next pair appears is refused");
}

static void testPressureFollowUp() {
    struct Probe : ai::X3Agent {
        using X3Agent::X3Agent;

        void read(const ai::Observation& o) {
            gaze.plans(o, 0, true, 1, p, 7, mine);
            gaze.plans(o, 1, false, 1, p, 7, theirs);
        }

        void inspectOffset(const ai::Observation& o, bool spentMain = false) {
            read(o);
            const auto attack = std::find_if(mine.plans.begin(), mine.plans.end(), [](const ai::Plan& s) {
                return s.count == 1 && s.id[0] == 0 && s.points == 420;
            });
            const auto findReply = [&](const ai::Plan& s) {
                return s.count == 1 && s.id[0] == (spentMain ? 9 : 0) && s.points == (spentMain ? 1000 : 420);
            };
            const ai::Plan* reply = nullptr;
            for (const auto* list : {&theirs.plans, &theirs.tactical}) {
                const auto found = std::find_if(list->begin(), list->end(), findReply);
                if (found != list->end()) reply = &*found;
            }
            check(attack != mine.plans.end() && reply, "both initial fires are visible");

            const ai::Duel quiet = ai::playOut(o, mine, &*attack, theirs, nullptr, p, 7, true);
            if (spentMain) {
                ai::Replies standing;
                ai::repliesOf(o.opponent().field, o.rules, o.timing, p, standing);
                int64_t firstCheck;
                const ai::Duel initial = ai::playOut(o, mine, &*attack, theirs, reply, p, 7, true, &firstCheck);
                const auto& rival = initial.player[1];
                check(pressureReplyRevealed(o, quiet, *reply, initial, firstCheck) && rival.chain.chains == 3 &&
                      !reply->allClear && !rival.scoring.allClear && !rival.fallen && reply->sent >= attack->sent + W &&
                      atLeastPercent(reply->points, p.x3PokeKeep, standing.main) &&
                      !atLeastPercent(reply->mainAfter, p.x3PokeKeep, reply->points),
                      "the larger reply has actually spent its main chain");
            }
            ai::Plan second = *attack;
            second.id[second.count++] = 8;
            const ai::Duel still = ai::playOut(o, mine, &second, theirs, &*reply, p, 7, true);
            check(pressureLands(o, second, still) && still.player[1].fallen == 0 &&
                  still.player[1].incoming.total() == (spentMain ? 26 : 40),
                  "standing still leaves enough unlanded garbage for the old pressure check");

            ai::Plan counter = *reply;
            counter.id[counter.count++] = int8_t(spentMain ? 3 : 2);
            const ai::Duel exchange = ai::playOut(o, mine, &second, theirs, &counter, p, 7, true);
            check(exchange.player[1].moves == o.opponent().moves + 2 && !exchange.player[1].incoming.total() &&
                  !exchange.player[1].fallen && exchange.player[0].fallen == (spentMain ? 0 : 3) &&
                  exchange.player[0].incoming.total() == (spentMain ? 8 : 0) &&
                  !pressureFollowUp(o, *attack, quiet, *reply, spentMain, 7),
                  "a reachable known response can remove the whole pending attack and send garbage back");

            if (spentMain) return;
            ai::Plan hold;
            hold.id[hold.count++] = 5;
            const ai::Duel direct = ai::playOut(o, mine, &*attack, theirs, &hold, p, 7, true);
            check(pressureLands(o, *attack, direct) && direct.player[1].fallen == 0 &&
                  !pressureAfterReply(o, *attack, hold, direct, 2 * W, 2, 7),
                  "pending garbage after a quiet opposing move is subject to the same continuation check");
        }

        bool revealed(const ai::Observation& o, int64_t& firstCheck, int64_t& nextPair) {
            read(o);
            ai::Plan first;
            first.id[first.count++] = 0;
            first.fireDepth = 1;
            const ai::Duel quiet = ai::playOut(o, mine, &first, theirs, nullptr, p, 7, true);
            const ai::Duel exchange = ai::playOut(o, mine, &first, theirs, &first, p, 7, true, &firstCheck);
            nextPair = quiet.player[0].spawnFrame;
            return pressureReplyRevealed(o, quiet, first, exchange, firstCheck);
        }

        bool canExtend(const ai::Observation& o) {
            read(o);
            ai::Plan attack, reply;
            attack.id[attack.count++] = 19;
            attack.fireDepth = 1;
            reply.id[reply.count++] = 19;
            reply.fireDepth = 1;
            reply.points = 1180;
            const ai::Duel quiet = ai::playOut(o, mine, &attack, theirs, nullptr, p, 7, true);
            int64_t firstCheck;
            const ai::Duel exchange = ai::playOut(o, mine, &attack, theirs, &reply, p, 7, true, &firstCheck);
            check(exchange.player[1].chain.chains == 3 && exchange.player[1].scoring.score == reply.points &&
                  pressureReplyRevealed(o, quiet, reply, exchange, firstCheck),
                  "the committed three-chain is visible in time to choose the extension");
            return pressureFollowUp(o, attack, quiet, reply, true, 7);
        }
    };

    ai::Observation o;
    o.rules.colors = 5;
    o.rules.targetPoints = 25;
    o.rules.softDropBonus = false;
    o.ownTsumo = true;
    o.tsumo.pairs = o.tsumo1.pairs = {{1, 2}, {5, 5}, {1, 5}};
    o.player[0].phase = o.player[1].phase = Phase::Next;
    o.player[0].wait = o.player[1].wait = 1;
    stairs(o.player[0].field, 0, 2);
    for (int k = 0; k < 3; ++k) {
        for (int y = 0; y < 3; ++y) o.player[0].field.f.set(5 - k, y, Cell(3 + k));
        if (k) o.player[0].field.f.set(5 - k, 3, Cell(2 + k));
    }
    o.player[1].field = o.player[0].field;

    ai::Params p = ai::levelParams(ai::Params{}, 37);
    Probe reacting(p);
    reacting.inspectOffset(o);

    o.player[1].wait = 150;
    int64_t firstCheck = -1, nextPair = -1;
    check(reacting.revealed(o, firstCheck, nextPair) && firstCheck == 185 && nextPair == 189,
          "the exact first wave is visible before a decision made at spawn");
    p.thinkAhead = 12;
    Probe ahead(p);
    check(!ahead.revealed(o, firstCheck, nextPair), "the same fire is too late for the first decision with an eleven-frame lead");

    o.rules.targetPoints = 16;
    o.player[1].wait = 1;
    o.tsumo1.pairs = {{3, 5}, {4, 5}, {1, 5}};
    o.player[1].field = {};
    stairs(o.player[1].field, 0, 3);
    for (int y = 0; y < 3; ++y) {
        o.player[1].field.f.set(5, y, Cell(4));
        o.player[1].field.f.set(4, y, Cell(5));
    }
    o.player[1].field.f.set(4, 3, Cell(4));
    // The 1,000-point reply spends the chain at a 55% retention threshold.
    // The test keeps the default threshold for all other fixtures.
    ai::Params mainReply = ai::levelParams(ai::Params{}, 37);
    mainReply.x3PokeKeep = 55;
    Probe mainReading(mainReply);
    mainReading.inspectOffset(o, true);

    o.rules.targetPoints = 30;
    o.tsumo.pairs = {{1, 2}, {1, 3}, {1, 1}, {3, 4}};
    o.tsumo1.pairs = {{3, 5}, {4, 2}, {3, 5}};
    o.player[1].wait = 142;
    o.player[1].field = {};
    stairs(o.player[1].field, 0, 3);
    o.player[1].field.f.set(2, 4, Cell(2));
    o.player[1].field.f.set(5, 0, Cell(5));
    check(reacting.canExtend(o), "two known setup moves can produce a verified follow-up behind a running chain");
    o.tsumo.pairs.pop_back();
    check(!reacting.canExtend(o), "the same follow-up is unavailable without the observed fourth pair");
}

static void testKnownMainReturn() {
    static const char* const rows1[] = {"G.....", "R....G", "G..RYG", "BY.BYB", "BG.RYR", "YG.RRB", "YR.GYB", "YGBBYY", "BGGBRB", "RYYRRB", "RBGBBR", "YYYGBR", "RRRGGB"};
    static const char* const rows0[] = {"......", "......", ".....B", "...GGY", "G..RGY", "RBYGRB", "RYGGBY", "BYYRBY", "RBBYBR", "RGRBYR", "GBRBYR", "GGBRBY", "BBRYBR"};
    ai::Observation o;
    o.me = 1;
    o.ownTsumo = true;
    o.frame = 1493;
    o.player[0].field = fieldOf(rows0, 13);
    o.player[1].field = fieldOf(rows1, 13);
    o.player[1].field.row14[0] = Cell(4);
    for (auto& side : o.player) side.phase = Phase::Next;
    o.player[0].wait = 13;
    o.player[1].wait = 10;
    o.tsumo.pairs = {{1,4},{2,2},{1,4},{2,4},{4,1},{1,3},{2,2}};
    o.tsumo1.pairs = {{4,1},{1,3},{2,2}};
    ai::Params p = ai::levelParams({}, 37);
    ai::Gaze gaze;
    ai::PlanSet mine, theirs;
    gaze.plans(o, 1, true, 3, p, 7, mine);
    gaze.plans(o, 0, false, 3, p, 7, theirs);
    const auto fire = std::find_if(mine.plans.begin(), mine.plans.end(), [](const ai::Plan& s) {
        return s.count == 1 && s.id[0] == 2 && s.chains == 11;
    });
    check(fire != mine.plans.end(), "the saved field can fire its 11-chain immediately");
    const size_t before = theirs.tactical.size();
    ai::MidgameBeam::extend(o, p, 7, theirs, [] { return false; });
    bool counter = false;
    for (size_t i = before; i < theirs.tactical.size(); ++i) {
        const ai::Plan& reply = theirs.tactical[i];
        if (reply.count < 4 || reply.chains < 5 || reply.sent < fire->sent) continue;
        const auto d = ai::playOut(o, mine, &*fire, theirs, &reply, p, 7, true);
        counter |= d.player[0].moves == reply.count && d.player[0].chain.chains == reply.chains &&
                   !d.player[0].fallen && !d.player[0].incoming.total() && d.player[1].incoming.total() >= W;
    }
    check(counter, "a visible later main chain offsets the immediate fire with executable inputs");
    o.tsumo.pairs.resize(3);
    gaze.plans(o, 0, false, 3, p, 7, theirs);
    ai::MidgameBeam::extend(o, p, 7, theirs, [] { return false; });
    check(std::none_of(theirs.tactical.begin(), theirs.tactical.end(), [](const ai::Plan& s) { return s.count > 3; }),
          "a fourth or fifth pair must be observed before it can supply a counter");
}

static void testMidairSameColorOffset() {
    Rules rules;
    rules.softDropBonus = false;
    Round round;
    round.start(rules, 7, 2);
    Player& player = round.player[0];
    round.player[1].wait = 1000;
    for (int x : {1, 3}) for (int y = 0; y < 11; ++y) player.field.f.set(x, y, GARBAGE);
    for (int y = 0; y < 9; ++y) player.field.f.set(2, y, y < 6 ? GARBAGE : Cell(1));
    round.tsumo.piece[0] = round.tsumo.piece[1] = Cell(1);
    player.incoming.confirmed = 1;
    PlayerEvents events[2];
    const auto step = [&](uint8_t key) {
        const uint8_t buttons[2]{key, 0};
        round.step(buttons, events);
    };
    while (player.phase != Phase::Control) step(0);
    for (auto key : {int(BTN_CW), 0, int(BTN_CW | BTN_DOWN), int(BTN_DOWN)}) step(uint8_t(key));
    for (int i = 0; i < 53; ++i) step(0);
    const int only = placementId({2, DOWN});
    check(player.phase == Phase::Control && player.pair.dir == DOWN, "the real inputs reach the child-down pose");
    for (int keys = 0; keys < 64; ++keys) {
        auto next = player.pair;
        stepPair(next, player.field, uint8_t(keys), round.timing, rules.speedPercent);
        check(next.locked && placementId(next.placement()) == only, "every input locks the same placement next frame");
    }

    const auto o = ai::observe(round, 1);
    const auto p = ai::levelParams({}, 37);
    ai::Gaze gaze;
    ai::PlanSet own, rival;
    gaze.plans(o, 1, true, 1, p, 7, own);
    gaze.plans(o, 0, false, 1, p, 7, rival);
    check(rival.firstReach == (1u << only), "the only input path reaches the reversed orientation");
    const auto fire = std::find_if(rival.plans.begin(), rival.plans.end(), [&](const ai::Plan& plan) {
        return plan.count == 1 && plan.id[0] == only && plan.chains == 1;
    });
    check(fire != rival.plans.end(), "the rival still has a real offset with a same-colour pair in the air");
    const auto d = ai::playOut(o, own, nullptr, rival, &*fire, p, 7, true);
    check(d.player[0].moves == 1 && d.player[0].scoring.score == 100 && !d.player[0].fallen && !d.player[0].incoming.total(),
          "the known input path cancels the confirmed garbage");
    step(0);
    while (player.moves == 1 && player.phase != Phase::Next) step(0);
    check(player.moves == 1 && player.scoring.score == 100 && !player.incoming.total() &&
          std::none_of(std::begin(player.dropped), std::end(player.dropped), [](int n) { return n > 0; }),
          "the frame game confirms the same offset");
}

static GameField splitAttackField(const char* cells) {
    GameField field;
    for (int y = 13; y >= 0; --y) for (int x = 0; x < W; ++x) {
        const Cell color = Cell(*cells++ - '0');
        if (y == 13) field.row14[x] = color;
        else if (color) field.f.set(x, y, color);
    }
    return field;
}

static ai::Observation splitPosition() {
    ai::Observation o;
    o.me = 1;
    o.frame = 1472;
    o.rules.palette = {5, 2, 3, 4, 1};
    o.timing = recordedRules(Timing{});
    o.timing.quickDownDrag = 32;
    o.ownTsumo = true;
    o.tsumo1.pairs = {{2, 5}, {3, 4}};
    o.tsumo.pairs = {{2, 2}, {3, 4}};
    o.player[1].field = splitAttackField("000000000003000004240022555342234234224555533322324523544255222445534534554334333445");
    o.player[0].field = splitAttackField("000000000000000000000000442030352020225224254533325534544424222545534553553444334333");
    o.player[1].moves = 33;
    o.player[0].moves = 28;
    o.player[1].wait = 10;
    o.player[0].wait = 13;
    return o;
}

struct SplitProbe : ai::X3Agent {
    using X3Agent::X3Agent;

    int chooseNow(const ai::Observation& o) {
        startRound(o.me, 1);
        return choose(o, o.mine().pair(o.self().nextPair), legalPlacements(o.self().field));
    }

    void prepare(const ai::Observation& o) {
        const uint64_t seed = 1 + o.self().moves;
        gaze.plans(o, o.me, true, 2, p, seed, mine);
        gaze.plans(o, 1 - o.me, false, 2, p, seed, theirs);
        ai::MidgameBeam::extend(o, p, seed, mine, [] { return false; });
    }

    int split(const ai::Observation& o) {
        prepare(o);
        return splitMainAttack(o, legalPlacements(o.self().field), 1 + o.self().moves);
    }

    ai::Duel replay(const ai::Observation& o, const ai::Plan* reply) {
        prepare(o);
        ai::Plan attack;
        attack.count = 2;
        attack.id[0] = 3;
        attack.id[1] = 15;
        return ai::playOut(o, mine, &attack, theirs, reply, p, 1 + o.self().moves, true);
    }
};

static void testSplitMainAttack() {
    auto p = ai::levelParams(ai::agentParams("x3", ai::Params{}), 37);
    p.searchThreads = 1;
    p.beamWidth = 40;
    p.beamDepth = 10;
    p.beamQueues = 3;
    SplitProbe ai(p);
    auto o = splitPosition();
    check(ai.chooseNow(o) == 3 && ai.kind == ai::X3Agent::Win, "the normal Lv37 choice exposes the known fifteen-chain");

    ai::Plan reply;
    reply.count = 2;
    reply.id[0] = 0;
    reply.id[1] = 2;
    const auto earlyEnd = ai.replay(o, &reply);
    check(earlyEnd.state == RoundState::Won1 && earlyEnd.player[0].moves == o.opponent().moves + 2 &&
          earlyEnd.player[1].chain.chains < 15, "an opponent self-loss cannot refute the unfinished own plan");

    o.player[0].field = o.player[1].field;
    o.player[0].wait = o.player[1].wait;
    o.tsumo = o.tsumo1;
    reply.id[0] = 3;
    reply.id[1] = 15;
    const auto matched = ai.replay(o, &reply);
    check(matched.player[0].chain.chains == 15 && matched.player[1].chain.chains == 15 &&
          !matched.player[0].incoming.total() && !matched.player[1].incoming.total(),
          "the opponent has a physically reachable full offset with the same known horizon");
    check(ai.split(o) < 0, "the matching opponent main prevents selecting the opening as a winning attack");
}

static void testWinImmediateFire() {
    Observation o;
    o.me = 1;
    o.frame = 1543;
    o.rules.palette = {5, 2, 3, 4, 1};
    o.timing = recordedRules(Timing{});
    o.timing.quickDownDrag = 32;
    o.ownTsumo = true;
    o.tsumo.pairs = {{2,2}, {3,4}, {5,4}, {3,5}};
    o.tsumo1.pairs = {{2,5}, {3,4}, {4,4}, {2,2}, {4,5}};
    o.player[0].field = splitAttackField("000000000000200000200000442034352023225224254533325534544424222545534553553444334333");
    o.player[1].field = splitAttackField("000000000003250404240322555342234234224555533322324523544255222445534534554334333445");
    o.player[0].nextPair = o.player[1].nextPair = 2;
    o.player[0].phase = Phase::Land;
    o.player[0].wait = 2;
    o.player[0].moves = 30;
    o.player[0].scoring = {6, 6, false};
    o.player[1].wait = 1;
    o.player[1].moves = 35;
    o.player[1].scoring = {3, 3, false};

    auto p = ai::levelParams(ai::agentParams("x3", {}), 37);
    p.searchThreads = 1;
    p.beamWidth = 250;
    p.beamDepth = 16;
    p.beamQueues = 8;
    p.beamFixedQueues = 4;
    struct Probe : ai::X3Agent {
        using X3Agent::X3Agent;
        int select(const Observation& view) {
            startRound(view.me, 1);
            return choose(view, view.mine().pair(view.self().nextPair), legalPlacements(view.self().field));
        }
        bool won() const { return kind == Win; }
    };
    Probe active(p);
    const int chosen = active.select(o);
    check(chosen == 2 && active.won(), "a stronger immediate Win is chosen ahead of the delayed Win");

    const auto play = [&](const Observation& view, bool mirror) {
        Round round;
        round.start(view.rules, 34, 2, view.timing);
        round.splitTsumo(35);
        round.frame = view.frame;
        for (int seat = 0; seat < 2; ++seat) {
            round.player[seat] = view.player[seat];
            auto& tsumo = seat ? round.tsumo1 : round.tsumo;
            for (int i = 0; i < view.pairsOf(seat).count(); ++i) {
                const Pair pair = view.pairsOf(seat).pair(i);
                tsumo.piece[2 * i] = pair.axis;
                tsumo.piece[2 * i + 1] = pair.child;
            }
        }
        ai::InputPlanner planner;
        ai::InputPath input;
        check(planner.plan(view.self().field, appearingPair(view.self(), view.rules, view.timing), placement(chosen),
                           view.timing, view.rules.speedPercent, input), "the immediate Win has an input path");
        bool finished[2]{};
        for (int t = 0; t < 1400 && !finished[1]; ++t) {
            if (!mirror && round.player[0].phase == Phase::Next && round.player[0].wait == 1)
                round.player[0].wait = 1500;
            const uint8_t button = t < input.frames() ? input.buttons[size_t(t)] : 0;
            const uint8_t buttons[2] = {mirror ? button : uint8_t(0), button};
            PlayerEvents events[2];
            round.step(buttons, events);
            for (int seat = 0; seat < 2; ++seat) finished[seat] |= events[seat].chainEnded;
        }
        check(finished[1] && round.player[1].chain.chains == 14 && round.frame == 2703,
              "the selected immediate Win really fires and completes its 14-chain");
        if (mirror) {
            check(finished[0] && !round.player[0].incoming.total() && !round.player[1].incoming.total(),
                  "the visible mirrored reply offsets the immediate chain");
        } else {
            check(round.player[0].incoming.confirmed == 1517, "the immediate chain sends its confirmed garbage");
        }
    };
    play(o, false);
    auto counter = o;
    counter.player[0] = counter.player[1];
    counter.tsumo = counter.tsumo1;
    Probe opposed(p);
    check(opposed.select(counter) != 2, "the immediate Win does not override a visible full offset");
    play(counter, true);
    ai::Gaze gaze;
    ai::PlanSet mine, rival;
    const uint64_t seed = 1 + counter.self().moves;
    gaze.plans(counter, 1, true, 3, p, seed, mine);
    gaze.plans(counter, 0, false, 3, p, seed, rival);
    ai::Plan fire;
    fire.count = fire.fireDepth = 1;
    fire.id[0] = int8_t(chosen);
    fire.chains = 14;
    const auto quiet = ai::playOut(counter, mine, &fire, rival, nullptr, p, seed, true);
    check(!ai::knownWinFire(counter, mine, rival, fire, p, seed, quiet, [] { return false; }),
          "the known-response check rejects the mirrored full offset");
}

static void testConfirmedOffsetCoverage() {
    const char* const own[] = {
        "BB....", "RG.B.G", "YR.R.G", "YB.RBB", "RRGBRB", "BGGBGG", "GRYRBR",
        "BRRGRB", "BGBRYB", "BGYBYY", "GYYBRR", "GRGGRB", "GRGBYB"
    };
    const char* const rival[] = {
        "......", "G...YY", "RB..YB", "GR..GY", "YY..GG", "YGRYBY", "YGBRGB",
        "GRBYBB", "GRRBYR", "YYYBBR", "GRBGRB", "GGRBRG", "RRBBGG"
    };
    Observation o;
    o.frame = 1734;
    o.tsumo.pairs = {{2, 3}, {1, 2}, {1, 4}, {4, 1}};
    o.player[0].field = fieldOf(own, 13);
    o.player[0].field.row14[0] = Cell(4);
    o.player[0].moves = 37;
    o.player[0].nextPair = 1;
    o.player[0].wait = 1;
    o.player[0].scoring = {312, 32};
    o.player[0].incoming.pending = 7;
    o.player[1].field = fieldOf(rival, 13);
    o.player[1].moves = 36;
    o.player[1].phase = Phase::Wave;
    o.player[1].wait = 79;
    o.player[1].chain.chains = 2;
    o.player[1].scoring = {806, 36};

    struct Probe : ai::X3Agent {
        using X3Agent::X3Agent;
        void test(const Observation& o) {
            constexpr uint64_t seed = 7;
            const unsigned hold = 1u << 5, offset = 1u << 15, fire = 1u << 13;
            gaze.plans(o, 0, true, 3, p, seed, mine);
            gaze.plans(o, 1, false, 3, p, seed, theirs);
            check(ai::quietSetupChoices(o, mine, hold | offset | fire) == (hold | offset),
                  "the two quiet placements and the immediate chain must be available");
            ai::Duel projected(o, seed);
            check(ai::toPairOf(projected, 1, o.frame + 6000), "the running chain must resolve");
            const int arriving = projected.player[0].incoming.confirmed;
            check(projected.player[1].chain.chains == 15 && arriving == 1697 &&
                  projected.player[1].spawnFrame == 2894, "the running fifteen-chain must confirm 1697 pieces");
            ai::Plan first;
            first.count = 1;
            first.id[0] = 15;
            const auto played = ai::playOut(o, mine, &first, theirs, nullptr, p, seed, true);
            check(played.player[0].lockFrame == 1744 && played.player[0].spawnFrame == 1772 &&
                  !played.player[0].fallen, "the alternate placement must reach the next decision before any fall");

            const int rate = o.rules.rateAt(o.frame);
            const int need = arriving * rate - o.self().scoring.pool;
            const int surplus = arriving * rate * p.x3Enough / 100;
            ai::BuildResult b;
            b.told = p.queues();
            b.depth = p.beamDepth;
            b.alive = hold | offset;
            b.each[0][5] = surplus;
            b.each[1][5] = need;
            for (int q = 0; q < 5; ++q) b.each[q][15] = need;
            const auto rank = [&](const ai::Duel& d, const ai::BuildResult& scores) {
                const unsigned ordinary = enoughChoices(scores, scores.alive, surplus);
                return fullOffsetChoices(o, d, scores, scores.alive, ordinary, arriving, rate, surplus, seed);
            };
            check(enoughChoices(b, b.alive, surplus) == hold && rank(projected, b) == offset,
                  "five full offsets should outrank one surplus when both placements build");
            for (int q = 0; q < 5; ++q) --b.each[q][15];
            check(rank(projected, b) == hold, "one missing point cannot offset the last of 1697 pieces");
            for (int q = 0; q < 5; ++q) ++b.each[q][15];
            auto forecast = projected;
            forecast.player[0].incoming = {arriving, 0};
            check(rank(forecast, b) == hold, "an unconfirmed forecast should leave the ordinary ordering in place");
            b.alive = offset | fire;
            b.each[0][13] = surplus;
            check(rank(projected, b) == fire, "a speculative build should preserve an immediate fire chosen by the ordinary ordering");
        }
    };
    auto p = ai::levelParams({}, 37);
    p.beamDepth = 16;
    p.beamQueues = 8;
    p.thinkBudget = 0;
    Probe probe(p);
    probe.test(o);
}

static void testBuildKeepsVisibleMain() {
    Observation o;
    o.me = 1;
    o.frame = 1690;
    o.tsumo.pairs = {{4,3}, {4,2}, {1,3}, {3,3}};
    o.player[0].field = splitAttackField("400000300003100002100043230023110424120114220313140423120322223343131133113342331442");
    o.player[1].field = splitAttackField("000000200400400300344330114620123430332331323441122112241221441421223344213114311333");
    o.player[0].phase = Phase::Split;
    o.player[0].wait = 16;
    o.player[1].wait = 1;
    o.player[0].scoring = {396, 46};
    o.player[1].scoring = {308, 28};
    for (auto& player : o.player) {
        player.moves = 35;
        player.nextPair = 1;
        player.colors = {4, 3};
    }
    ai::Gaze gaze;
    ai::PlanSet mine, theirs;
    auto p = ai::levelParams({}, 37);
    constexpr uint64_t seed = 7;
    gaze.plans(o, 1, true, 3, p, seed, mine);
    gaze.plans(o, 0, false, 3, p, seed, theirs);
    const unsigned choices = (1u << 1) | (1u << 12);
    const auto choose = [&](const Observation& view, const ai::PlanSet& opponent, bool stopped = false) {
        return ai::preserveReadyBuild(view, mine, opponent, 1, choices, p, seed, [&] { return stopped; });
    };
    check(choose(o, theirs) == 12, "a build can hold the visible next-pair main against the known immediate attack");
    check(choose(o, theirs, true) == 1, "an unfinished comparison must keep the original placement");
    check(choose(o, ai::PlanSet{}) == 1, "a board without a known immediate attack keeps the ordinary build");
    auto allClear = o;
    allClear.player[1].scoring.allClear = true;
    check(choose(allClear, theirs) == 1, "an all-clear bonus remains under the ordinary decision");
    p = ai::levelParams({}, 36);
    check(choose(o, theirs) == 1, "Lv36 keeps its own building decision");
}

static void testKnownPokeReserve() {
    constexpr const char* rows[]{
        "......", "RG....", "RR.RYG", "GG.GRB", "RR.GGY", "BY.RRR", "GRYGGG",
        "GRRBBY", "BGBRGG", "BYBYRB", "BYYRYY", "RRRGYB", "GBYBRB"
    };
    Observation o;
    o.me = 1;
    o.frame = 1590;
    o.player[0].wait = o.player[1].wait = 1;
    o.player[1].scoring.pool = 22;
    o.player[1].field = fieldOf(rows, 13);

    ai::PlanSet own, enemy;
    own.who = 1;
    enemy.who = 0;
    own.knownCount = own.pairCount = 3;
    enemy.knownCount = enemy.pairCount = 1;
    own.pair[0] = {2, 4};
    own.pair[1] = {1, 4};
    own.pair[2] = {1, 2};
    enemy.pair[0] = {2, 3};
    ai::Plan poke, hold, reply;
    poke.count = poke.fireDepth = 1;
    poke.id[0] = 13;
    poke.chains = 2;
    poke.points = 545;
    poke.sent = 8;
    poke.mainAfter = 52220;
    hold.count = hold.fireDepth = 3;
    hold.id[0] = 7;
    hold.id[1] = 8;
    hold.id[2] = 2;
    hold.chains = 14;
    hold.points = 96588;
    reply.count = 1;
    reply.id[0] = 2;
    own.plans = {poke, hold};
    enemy.plans = {reply};

    const auto params = ai::levelParams({}, 37);
    const auto legal = legalPlacements(o.self().field);
    const auto select = [&](int room) {
        return ai::known_poke::prefer(o, legal, 70, 1, params, own, enemy, poke, room, false, 0, [] { return false; });
    };
    check(select(3) == 7, "Lv37 holds a visible 14-chain when every known exchange is at least as good");
    for (const char* name : {"lv36", "lv36q", "lv37", "lv38", "lv38ex"}) {
        const bool upper = ai::levelOf(name) >= 37;
        for (bool cancel : {false, true}) {
            if (ai::explicitLv38Ex(name) && !cancel) continue;
            o.rules.cancel = cancel;
            const auto named = ai::agentParams(name, {});
            const auto choose = [&](const ai::Params& p) {
                return ai::known_poke::prefer(o, legal, 70, 1, p, own, enemy, poke, 3, false, 0, [] { return false; });
            };
            check(named.x3PokeNet == (upper ? 3 : 0) && choose(named) == (upper ? 7 : -1),
                  "the named level has its measured reserve choice");
            if (upper) {
                auto given = ai::namedDefaults(name);
                for (int mode : {0, 1, 2, 3}) {
                    given.x3PokeNet = mode;
                    check(ai::agentParams(name, given).x3PokeNet == mode &&
                          ai::configuredParams(name, given).x3PokeNet == mode &&
                          choose(ai::configuredParams(name, given)) == (mode == 3 ? 7 : -1),
                          "an explicitly supplied poke mode takes precedence");
                }
            }
        }
    }
    o.rules.cancel = false;
    check(select(2) < 0, "the reserve rule leaves a cramped poke unchanged");
    own.pair[3] = {1, 1};
    own.knownCount = 4;
    check(select(3) < 0, "the reserve rule leaves a four-pair window unchanged");
}

static void testKnownPokeHandoff() {
    constexpr const char* ownRows[]{
        "......", "Y..Y.R", "BR.GGY", "GBYGRB", "YBYYGB", "YGGGBY", "BBRBBG",
        "BGGRRR", "YYGYBB", "RRRYBG", "BYGRRG", "BBGBRG", "YYYGGR"
    };
    constexpr const char* enemyRows[]{
        "B.....", "RR....", "RGBGYG", "BYRRYG", "BBYYGB", "RGYGYG", "BRRYBB",
        "GGRBGY", "GRYBGY", "BBBGYB", "RGRYYB", "RRGGBY", "GGYGYY"
    };
    Observation o;
    o.frame = 1532;
    auto& own = o.player[0];
    auto& enemy = o.player[1];
    own.field = fieldOf(ownRows, 13);
    enemy.field = fieldOf(enemyRows, 13);
    enemy.field.row14[0] = 3;
    own.phase = Phase::Next;
    own.wait = 1;
    own.moves = 34;
    own.scoring = {261, 261, false};
    enemy.phase = Phase::Control;
    enemy.moves = 35;
    enemy.nextPair = 2;
    enemy.scoring = {267, 267, false};
    enemy.colors = {1, 2};
    enemy.pair.x = 2;
    enemy.pair.y = 12;
    enemy.pair.dir = DOWN;
    enemy.pair.gain = 84;
    o.tsumo.pairs = {{2, 4}, {1, 2}, {3, 1}, {2, 2}};

    ai::Plan poke, hold, main;
    poke.id[0] = 2;
    poke.count = poke.fireDepth = 1;
    poke.points = 543;
    poke.chains = 2;
    poke.sent = 11;
    poke.mainAfter = 94760;
    hold.id[0] = 9;
    hold.id[1] = 7;
    hold.count = hold.fireDepth = 2;
    hold.points = 131124;
    hold.chains = 16;
    main.id[0] = 1;
    main.id[1] = 19;
    main.count = main.fireDepth = 2;
    main.points = 106124;
    main.chains = 14;
    main.mainAfter = 100;
    ai::PlanSet a, b;
    a.who = 0;
    b.who = 1;
    a.knownCount = ai::copySearchPairs(o, 0, true, a.pair, 4);
    b.knownCount = ai::copySearchPairs(o, 1, true, b.pair, 4);
    b.midAir = true;
    a.plans = {poke, hold};
    b.plans = {main};
    const auto params = ai::levelParams({}, 37);
    const int rate = o.rules.rateAt(o.frame);
    const unsigned legal = legalPlacements(own.field);
    const auto select = [&](bool discounted, auto stop) {
        return ai::known_poke::prefer(o, legal, rate, 37, params, a, b, poke, 1, discounted, main.points, stop);
    };
    const auto live = [] { return false; };
    check(select(true, live) == 9, "Lv37 retains a visible full offset against a previously discounted main");
    check(select(false, live) < 0, "a poke without a discounted main is unchanged on this field");
    int polls = 0;
    check(select(true, [&] { return ++polls == 128; }) < 0 && polls == 128,
          "an interrupted known-pair replay retains the original poke");
    enemy.pair.locked = true;
    check(select(true, live) < 0, "a reported in-hand reply with no playable pair cannot replace the poke");
    enemy.pair.locked = false;
    own.scoring.allClear = true;
    check(select(true, live) < 0, "an all-clear bonus retains the existing poke judgement");
    own.scoring.allClear = false;
    o.rules.cancel = true;
    check(select(true, live) == 9, "cancel rules keep the known-pair poke judgement (the Ex levels read it as well)");
    o.rules.cancel = false;
    a.knownCount = 1;
    check(select(true, live) < 0, "an unseen offset pair cannot replace the poke");
}

static void testKnownPrefixTiming() {
    // The observations give the player only the pairs visible at the decision.
    Observation o;
    o.me = 1;
    o.frame = 2976;
    o.ownTsumo = true;
    o.tsumo.pairs = {{1, 2}, {2, 4}};
    o.tsumo1.pairs = {{3, 3}, {3, 1}, {3, 2}};
    auto& foe = o.player[0];
    auto& self = o.player[1];
    foe.field = splitAttackField("100000000000000000000000000000000000000244000242010334010322040232332323223433222244");
    self.field = splitAttackField("100002020011010334030141230412431141422441332124324424432114433221411133222444311233");
    foe.phase = Phase::Wave;
    foe.wait = 5;
    foe.chain.chains = 8;
    foe.moves = 46;
    self.phase = Phase::Next;
    self.wait = 1;
    self.moves = 54;
    self.incoming.pending = 285;
    self.scoring.pool = 61;

    auto p = ai::levelParams({}, 37);
    p.beamWidth = 250;
    p.beamDepth = 16;
    p.beamQueues = 8;
    p.beamFixedQueues = 4;
    p.searchThreads = 1;
    p.thinkBudget = 0;
    struct Probe : ai::X3Agent { using X3Agent::X3Agent; using X3Agent::choose; };
    const auto choose = [](const Observation& view, const ai::Params& p) {
        Probe agent(p);
        agent.startRound(view.me, 7674555874918794106ull);
        const auto hand = view.mine().pair(view.self().nextPair);
        const auto legal = ai::differentFields(legalPlacements(view.self().field), hand);
        const int id = agent.choose(view, hand, legal);
        return std::pair{id, agent.kind};
    };

    auto ordinary = p;
    ordinary.checkKnownExchange = false;
    check(choose(o, p) == std::pair{2, ai::X3Agent::AnswerBuild},
          "an unavoidable visible four-chain leaves too few layers for the old build");
    check(choose(o, ordinary) == std::pair{3, ai::X3Agent::AnswerBuild},
          "known timing is disabled with known exchanges");

    Observation clear;
    clear.player[0].field.f.set(0, 0, 1);
    clear.player[0].field.f.set(1, 0, 1);
    clear.player[0].phase = Phase::Next;
    clear.player[0].wait = 1;
    clear.player[1].phase = Phase::Next;
    clear.player[1].wait = 200;
    clear.tsumo.pairs = {{1, 1}, {2, 3}, {3, 4}};
    const auto selected = choose(clear, p);
    GameField field = clear.self().field;
    place(field, placement(selected.first), clear.mine().pair(0));
    ai::Trial fired;
    ai::runTrialChain(field.f, clear.rules, clear.timing, fired);
    check(selected.second == ai::X3Agent::Clear && field.f.empty() && selected == choose(clear, ordinary),
          "a visible all-clear still takes the ordinary clear route");
}

static void testRunningChainOffsetRetention() {
    Observation o;
    o.frame = 2239;
    o.ownTsumo = true;
    o.rules.cancel = false;
    o.tsumo.pairs = {{3, 4}, {1, 2}, {1, 2}};
    o.tsumo1.pairs = {{1, 3}, {1, 2}};
    constexpr char ownCells[] =
        "040000" "440000" "410300" "320200" "210100" "220124" "311424"
        "313132" "422434" "424434" "432124" "232441" "232411" "211144";
    constexpr char foeCells[] =
        "000000" "000000" "040000" "220104" "130403" "320301" "212314"
        "221241" "423231" "113132" "132142" "442212" "143424" "113144";
    static_assert(sizeof(ownCells) == W * 14 + 1 && sizeof(foeCells) == W * 14 + 1);
    auto& self = o.player[0];
    self.field = splitAttackField(ownCells);
    self.wait = 1;
    self.moves = 44;
    self.scoring = {1080, 30};
    self.incoming.pending = 5;
    auto& foe = o.player[1];
    foe.field = splitAttackField(foeCells);
    foe.phase = Phase::Wave;
    foe.wait = 68;
    foe.moves = 41;
    foe.scoring = {1224, 34};
    foe.chain.chains = 2;

    ai::Gaze gaze;
    const auto p = ai::levelParams({}, 37);
    constexpr uint64_t seed = 3122309291697762058ull + 44;
    const auto collect = [&](const Observation& view, const ai::Params& q) {
        ai::PlanSet s;
        gaze.plans(view, 0, true, 3, q, seed, s);
        return s;
    };
    const auto hasReturn = [](const ai::Plan& s) {
        return s.count == 3 && s.id[0] == 4 && s.id[1] == 5 && s.id[2] == 19;
    };
    const auto mine = collect(o, p);
    const auto actual = std::find_if(mine.tactical.begin(), mine.tactical.end(), hasReturn);
    check(actual != mine.tactical.end() && actual->chains == 13 && actual->sent == 0,
          "a 13-chain that spends its attack on offsets remains available");
    ai::PlanSet theirs;
    gaze.plans(o, 1, false, 3, p, seed, theirs);
    const ai::Duel d = ai::playOut(o, mine, &*actual, theirs, nullptr, p, seed, true);
    check(d.player[1].chain.chains == 13 && d.player[1].phase == ai::DuelPhase::Control &&
          d.player[0].chain.chains == 13 && d.player[0].moves == self.moves + 3 &&
          d.player[0].fallen + d.player[0].incoming.total() == 147,
          "the retained 13-chain leaves 147 after the running attack completes");
    auto withoutOffset = p;
    withoutOffset.checkKnownExchange = false;
    const auto legacy = collect(o, withoutOffset);
    check(std::none_of(legacy.tactical.begin(), legacy.tactical.end(), hasReturn),
          "the offset-specific slot is needed for this return");
    const auto previousLevel = collect(o, ai::levelParams({}, 36));
    check(previousLevel.tactical.empty(), "Lv36 does not get running-chain offset retention");

    auto quiet = o;
    quiet.player[1].phase = Phase::Next;
    quiet.player[1].wait = 1;
    quiet.player[1].chain = {};
    quiet.player[0].incoming = {};
    const auto idle = collect(quiet, p), idleLegacy = collect(quiet, withoutOffset);
    check(std::equal(idle.tactical.begin(), idle.tactical.end(), idleLegacy.tactical.begin(), idleLegacy.tactical.end(),
        [](const ai::Plan& a, const ai::Plan& b) {
            return ai::samePlacements(a, b) && a.sent == b.sent && a.points == b.points &&
                a.mainAfter == b.mainAfter && a.endFrame == b.endFrame;
        }), "a rival without a running chain leaves tactical retention unchanged");
}

// Add below the small-attack fixtures, and call from the ordinary AI tests.
static juraic::ai::Observation smallReplyField(bool early) {
    using namespace juraic;
    ai::Observation o;
    o.me = 1;
    o.ownTsumo = true;
    o.frame = early ? 2416 : 2606;
    const auto field = [](std::string_view rows) {
        GameField f;
        for (int x = 0; x < W; ++x) f.row14[x] = Cell(rows[x] - '0');
        for (int i = W; i < int(rows.size()); ++i) {
            const Cell c = Cell(rows[size_t(i)] - '0');
            if (c != EMPTY) f.f.set(i % W, 13 - i / W, c);
        }
        return f;
    };
    auto& rival = o.player[0];
    auto& own = o.player[1];
    rival.field = field(early ?
        "000000200000100003143003162123226124311424313132422434424434432124232441232411211144" :
        "000000200002100003140003163003222004311404313132422434424434432124232441232411211144");
    own.field = field(early ?
        "000004000001000012440334430244230312430112320244341424131221232444421131322441311141" :
        "000004400001400012400334430244240312440112321244341424131221232444421131322441311141");
    rival.phase = early ? Phase::Next : Phase::Wave;
    rival.wait = early ? 5 : 49;
    rival.chain.chains = early ? 1 : 2;
    rival.moves = early ? 45 : 47;
    rival.scoring = early ? Scoring{880, 40, false} : Scoring{1244, 54, false};
    own.phase = Phase::Next;
    own.wait = 1;
    own.moves = early ? 46 : 49;
    own.scoring = early ? Scoring{1228, 38, false} : Scoring{1282, 22, false};
    own.incoming.confirmed = early ? 1 : 0;
    own.incoming.pending = early ? 0 : 5;
    o.tsumo.pairs = early ? std::vector<Pair>{{1, 2}, {1, 2}} : std::vector<Pair>{{4, 1}, {2, 1}};
    o.tsumo1.pairs = early ? std::vector<Pair>{{1, 3}, {4, 3}, {4, 4}} : std::vector<Pair>{{3, 1}, {2, 4}, {4, 1}};
    return o;
}

static void testSmallReplyExchange() {
    using namespace juraic;
    auto p = ai::levelParams({}, 37);
    p.searchThreads = 1;
    p.beamWidth = 250;
    p.beamDepth = 16;
    p.beamQueues = 8;
    p.beamFixedQueues = 4;
    struct TestAgent : ai::X3Agent {
        using ai::X3Agent::X3Agent;
        int pick(const ai::Observation& o) {
            startRound(o.me, 10192824035178659411ull);
            return choose(o, o.mine().pair(o.self().nextPair), legalPlacements(o.self().field));
        }
    };
    TestAgent agent(p);
    auto late = smallReplyField(false);
    check(agent.pick(late) == 7, "Lv37 fires the earlier, stronger reply with an equally ready three-pair field");
    check(agent.kind == TestAgent::Answer, "the earlier attack answers incoming garbage");
    check(agent.pick(smallReplyField(true)) == 2, "a small reply keeps its main when no visible large threat can start");
    ai::Gaze gaze;
    ai::PlanSet ours, theirs;
    static constexpr uint64_t seed = 10192824035178659411ull;
    gaze.plans(late, 1, true, 3, p, seed, ours);
    gaze.plans(late, 0, false, 3, p, seed, theirs);
    int polls = 0;
    const auto ask = [&](auto expired, int arriving) {
        return ai::receive_compare::prefer(late, ours, theirs, 3, legalPlacements(late.self().field), arriving, p, seed, expired);
    };
    check(ask([&] { return ++polls == 200; }, 5) == -1 && polls == 200,
          "a stopped comparison leaves the ordinary answer");
    int whole = 0, latePolls = 0;
    check(ask([&] { ++whole; return false; }, 5) == 7 && whole > 200,
          "the positive fixture completes its exchange comparison");
    check(ask([&] { return ++latePolls == whole - 1; }, 5) == -1 && latePolls == whole - 1,
          "interrupting the final checks cannot take an already promising attack");
    check(ask([] { return false; }, 797) == -1, "an incoming main uses regular offset selection");
}

static void testVisiblePressureBeforeFire() {
    Observation o;
    o.ownTsumo = true;
    o.rules.cancel = false;
    o.tsumo.pairs = {{1, 2}, {4, 4}};
    o.tsumo1.pairs = {{3, 3}, {3, 3}};
    auto& self = o.player[0];
    self.phase = Phase::Next;
    self.wait = 1;
    auto& enemy = o.player[1];
    enemy.phase = Phase::Next;
    enemy.wait = 1;
    stairs(enemy.field, 0, 3);
    enemy.field.f.set(5, 0, 4); // Prevent an all-clear from hiding the ordinary pressure.

    constexpr uint64_t seed = 8551104070121336641ull + 25;
    const auto p = ai::levelParams({}, 38);
    ai::Gaze gaze;
    ai::PlanSet ours, theirs;
    gaze.plans(o, 0, true, 2, p, seed, ours);
    gaze.plans(o, 1, false, 2, p, seed, theirs);
    const auto pressure = ai::lv38_threat::threats(theirs);
    check(pressure[0] && pressure[0]->count == 1, "相手が発火する前に手の組から2段以上の潰しを見つける");
    const auto replay = ai::playOut(o, ours, nullptr, theirs, pressure[0], p, seed, true);
    check(replay.player[1].moves == enemy.moves + 1 && replay.player[0].incoming.total() >= 2 * W,
          "可視の潰しが実際の配置で2段以上届く");
    auto onePair = theirs;
    onePair.knownCount = 1;
    const auto withoutNext = ai::lv38_threat::threats(onePair);
    check(!withoutNext[2] && !withoutNext[3], "未表示の2手目から潰しを作らない");

    using ai::lv38_threat::Outcome;
    const int main = 34000, rate = 70;
    const Outcome buried{20000, 28000, 14, 0, true};
    const Outcome secondary{31000, 28000, 0, 2, true};
    const Outcome forcedMain{1000, 28000, 0, 35, true};
    check(buried.crushed(main, rate) && forcedMain.crushed(main, rate) && secondary.holds(main, rate) &&
          secondary.margin(rate) > buried.margin(rate), "副砲で本線を残せる交換を潰される手と区別する");
}

static void testSetupKeepsVisibleAnswer() {
    using namespace juraic;
    ai::Observation o;
    o.frame = 1589;
    o.rules.cancel = false;
    o.tsumo.pairs = {{3,4}, {1,2}, {4,3}, {2,1}};
    auto& self = o.player[0];
    self.field = splitAttackField(
        "000000110000240004140342211342444221113142"
        "123242221142134123133443144224222344111423");
    self.nextPair = 1;
    self.moves = 35;
    self.phase = Phase::Next;
    self.wait = 1;
    self.scoring = {267,267,false};
    auto& enemy = o.player[1];
    enemy.field = splitAttackField(
        "000000420000110042421041443021122041213022"
        "233244114414122322444342121434112334221113");
    enemy.nextPair = 0;
    enemy.moves = 34;
    enemy.phase = Phase::Next;
    enemy.wait = 6;
    enemy.scoring = {257,257,false};

    const auto p = ai::levelParams({}, 37);
    const uint64_t seed = 11;
    ai::Gaze gaze;
    ai::PlanSet ours, theirs;
    gaze.plans(o, 0, true, 3, p, seed, ours);
    gaze.plans(o, 1, false, 3, p, seed, theirs);
    const auto open = [] { return false; };
    const auto result = ai::keepSetupAnswer(o, ours, theirs, 8, 5, p, seed, open);
    check(result == 8, "a faster setup must keep the visible answer to the rival main chain");
    ai::PlanSet quietRival = theirs;
    quietRival.plans.clear();
    quietRival.tactical.clear();
    quietRival.keeping.clear();
    check(ai::keepSetupAnswer(o, ours, quietRival, 8, 5, p, seed, open) == 5,
          "a faster placement remains available with no shown attack");
    check(ai::keepSetupAnswer(o, ours, theirs, 8, 5, ai::levelParams({},36), seed, open) == 5,
          "the setup check belongs to level 37");
    check(ai::keepSetupAnswer(o, ours, theirs, 8, 5, p, seed, [] { return true; }) == 8,
          "the existing beam choice stands if the comparison runs out of time");
}

static void testPokeAgainstStoredAllClear() {
    constexpr const char* left[]{
        "Y.....", "R...Y.", "RG..G.", "RY..BG", "BY..YG", "BYRBGY", "GGBYBB",
        "GYBYRB", "BRRGBR", "BBYGBR", "YRGRBR", "YRRBRG", "GGGBRG"
    };
    constexpr const char* right[]{
        "......", "......", "GY....", "GY....", "GY....", "BGG..B", "BYRR.R",
        "YGGR.R", "BGYBGR", "BYYRRG", "BRBBRG", "YBRYBG", "YYRRBB"
    };
    Observation o;
    o.frame = 1373;
    o.ownTsumo = true;
    auto& own = o.player[0];
    auto& foe = o.player[1];
    own.field = fieldOf(left, 13);
    own.wait = 1;
    own.nextPair = own.moves = 31;
    own.scoring.pool = 260;
    foe.field = fieldOf(right, 13);
    foe.phase = Phase::Control;
    foe.nextPair = 28;
    foe.moves = 27;
    foe.scoring.pool = 293;
    foe.scoring.allClear = true;
    foe.colors = {3, 2};
    foe.pair = spawnPair(o.timing);
    for (const uint8_t button : {6, 4, 6, 4, 4, 4, 4})
        stepPair(foe.pair, foe.field, button, o.timing, o.rules.speedPercent);
    o.tsumo.pairs.resize(31);
    o.tsumo1.pairs.resize(28);
    for (Pair p : {Pair{3, 2}, Pair{1, 4}, Pair{2, 1}}) o.tsumo.pairs.push_back(p);
    for (Pair p : {Pair{4, 1}, Pair{3, 2}}) o.tsumo1.pairs.push_back(p);

    const auto p = ai::levelParams({}, 37);
    const auto legal = legalPlacements(own.field);
    constexpr uint64_t seed = 4687432659440854654ull;
    ai::Gaze gaze;
    ai::PlanSet plans, counters;
    gaze.plans(o, 0, true, 3, p, seed, plans);
    gaze.plans(o, 1, false, 3, p, seed, counters);
    const auto poke = std::find_if(plans.plans.begin(), plans.plans.end(), [](const ai::Plan& plan) {
        return plan.count == 1 && plan.fireDepth == 1 && plan.id[0] == 10;
    });
    check(poke != plans.plans.end() && poke->sent == 4, "a four-piece poke is available under the stored all-clear");
    const auto choose = [&](const ai::Params& params, auto stop) {
        return ai::known_poke::prefer(o, legal, 70, seed, params, plans, counters, *poke, 0, true, 0, stop);
    };
    const auto noTimeLimit = [] { return false; };
    check(choose(p, noTimeLimit) == 14, "Lv37 keeps a physically executable offset to the known all-clear counter");
    int checks = 0;
    check(choose(p, [&] { return ++checks >= 16; }) < 0, "an interrupted all-clear counter scan leaves the poke selection unchanged");
    check(choose(ai::levelParams({}, 36), noTimeLimit) < 0, "Lv36 never runs the stored all-clear counter scan");
    counters.knownCount = 1;
    check(choose(p, noTimeLimit) < 0, "an unseen next pair cannot prove a lethal all-clear counter");
    counters.knownCount = 3;
    foe.scoring.allClear = false;
    own.scoring.allClear = true;
    check(choose(p, noTimeLimit) < 0, "the player's own stored all-clear bonus keeps the normal poke rules");
}

// The three visible pairs and complete own field in the audited high reach case.
// Opponent motion and unknown pairs are irrelevant to the standalone comparator.
static void testOpenSetup() {
    ai::Observation o;
    o.me = 1;
    o.rules.colors = 4;
    o.rules.cancel = false;
    o.player[1].wait = 1;
    o.player[0].phase = Phase::Control;
    o.player[0].incoming.confirmed = 1;
    constexpr const char* rows[] = {"000000","400000","310000","244130","114341","123413","214433",
                                    "231114","432321","131221","141432","144232","222421","114422"};
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) o.player[1].field.f.set(x, y, Cell(rows[ROW_14-y][x] - '0'));
    o.tsumo.pairs = {{1,4}, {4,4}, {3,2}};
    ai::PlanSet mine;
    mine.who = 1;
    mine.knownCount = mine.pairCount = 3;
    std::copy(o.tsumo.pairs.begin(), o.tsumo.pairs.end(), mine.pair);
    ai::Plan visible;
    visible.count = visible.fireDepth = 2;
    visible.id[0] = 20;
    visible.id[1] = 7;
    visible.points = 101424;
    visible.sent = 1449;
    mine.plans.push_back(visible);
    ai::BuildResult b;
    const unsigned both = (1u << 20) | (1u << 9);
    b.alive = both;
    b.sum[20] = 1054040;
    b.sum[9] = 1023340;
    const auto lv37 = ai::agentParams("lv37", ai::Params{});
    const auto run = [&](const ai::Observation& state, int room = 2) {
        return ai::preferOpenSetup(state, mine, b, 20, both, room, lv37, [] { return false; });
    };
    check(run(o) == 9, "the quiet reverse must strictly extend visible conditional coverage");

    for (bool ex : {false, true}) {
        auto picture = o;
        picture.rules.cancel = ex;
        const int held[2]{};
        ai::PictureRoutes routes(2, held, true, true, false);
        const auto usual = ai::setup_room::quietLanding(picture, mine.pair[0], 20, routes.first, true);
        const auto other = ai::setup_room::quietLanding(picture, mine.pair[0], 9, routes.first, true);
        const int id = ai::preferOpenSetup(picture, mine, b, 20, both, 2, lv37, [] { return false; }, &routes);
        std::cout << "held open setup: cancel " << ex << ", selected " << id << ", locks "
                  << (usual ? usual->frames : -1) << "/" << (other ? other->frames : -1) << "\n";
        check(id == 20 || (id == 9 && usual && other && usual->frames == other->frames &&
                           usual->check == other->check && usual->soft == other->soft), "held setup preserves lock, check and drop points");
        check(ai::preferOpenSetup(picture,mine,b,20,both,2,lv37,[] { return true; },&routes) == 20,
              "a held setup past its deadline retains the original move");
    }
    for (bool ex : {false, true}) {
        auto picture = o;
        picture.timing = play::realgame::realGameTiming();
        picture.rules.cancel = ex;
        picture.rules.preInput = true;
        auto& own = picture.player[picture.me];
        own.buttons = own.sideQueued = BTN_RIGHT;
        own.sideAge = own.turnAge = 0;
        const int held[2]{};
        ai::PictureRoutes routes(2, held, true, true, false);
        const int selected=ai::preferOpenSetup(picture,mine,b,20,both,2,lv37,[] {return false;},&routes);
        check(selected == 9, "the held picture setup must select the better reachable orientation with a queued right tap");
        std::cout << "held picture setup changed 20 -> " << selected << ", cancel " << ex << "\n";
    }
    auto changed = o;
    changed.tsumo.pairs.pop_back();
    check(run(changed) == 20, "two pairs cannot prove the four-pair branches");
    changed = o;
    changed.tsumo.pairs.push_back({2,3});
    check(run(changed) == 20, "a visible fourth pair cannot be replaced by forecasts");
    changed = o;
    changed.player[0].incoming = {1,0};
    check(run(changed) == 20, "unconfirmed opponent garbage stays with the usual choice");
    changed = o;
    changed.player[0].incoming.confirmed = W;
    check(run(changed) == 20, "a row on the rival must be left to ordinary tactics");
    changed = o;
    changed.player[1].incoming.pending = 1;
    check(run(changed) == 20, "own garbage blocks quiet reversal");
    changed = o;
    changed.player[0].phase = Phase::Wave;
    check(run(changed) == 20, "an opponent chain blocks quiet reversal");
    changed = o;
    changed.player[1].phase = Phase::Wave;
    check(run(changed) == 20, "an own chain blocks quiet reversal");
    changed = o;
    changed.busyChain[0] = true;
    changed.busyLeft[0] = 1;
    check(run(changed) == 20, "an unfinished landing reported earlier than the phase also blocks reversal");
    for (int seat : {0, 1}) {
        changed = o;
        changed.player[seat].scoring.allClear = true;
        check(run(changed) == 20, "both all-clear bonuses take precedence");
    }
    check(run(o, 3) == 20, "the quiet reverse is restricted to a field with less room than the preview");
    const auto lv36 = ai::agentParams("lv36", ai::Params{});
    check(ai::preferOpenSetup(o, mine, b, 20, both, 2, lv36, [] { return false; }) == 20,
            "the old level cannot change its choice");
    check(ai::preferOpenSetup(o, mine, b, 20, both, 2, lv37, [] { return true; }) == 20,
            "an expired start keeps the ordinary orientation");
    int checks = 0;
    check(ai::preferOpenSetup(o, mine, b, 20, both, 2, lv37, [&] { return ++checks >= 100; }) == 20 && checks >= 100,
            "an interrupted comparison keeps the ordinary orientation");
    b.sum[9] = 1000000;
    check(run(o) == 20, "the original tolerance applies before trying the orientation");
    b.sum[9] = 1023340;
    changed = o;
    changed.tsumo.pairs[0] = {4,4};
    mine.pair[0] = {4,4};
    check(run(changed) == 20, "the same-colour orientations already have the same field");
}

static void testKnownPartialBuild() {
    Observation pressured;
    pressured.ownTsumo = true;
    pressured.frame = 1614;
    pressured.tsumo.pairs = {{4,1}, {2,2}, {4,4}};
    pressured.tsumo1.pairs = {{4,3}, {3,1}};
    auto& own = pressured.player[0];
    own.field = splitAttackField(
        "020000" "140000" "210044" "444014" "113141" "122321" "342122"
        "323234" "133224" "222114" "113142" "134211" "434422" "111311");
    own.scoring = {268, 268, false};
    own.moves = 36;
    own.wait = 1;
    auto& foe = pressured.player[1];
    foe.field = splitAttackField(
        "000004" "000004" "310003" "130343" "211142" "233432" "113112"
        "144241" "224324" "123414" "113134" "442431" "433411" "223244");
    foe.scoring = {261, 261, false};
    foe.moves = 35;
    foe.wait = 6;

    Observation hold;
    hold.me = 1;
    hold.ownTsumo = true;
    hold.frame = 1945;
    hold.tsumo.pairs = {{4,2}, {1,3}};
    hold.tsumo1.pairs = {{4,4}, {3,2}, {2,3}};
    auto& rival = hold.player[0];
    rival.field = splitAttackField(
        "000000" "200042" "100033" "400623" "410322" "140113" "110244"
        "431131" "431431" "432244" "111422" "422434" "332314" "444112");
    rival.scoring = {350, 70, false};
    rival.moves = 37;
    rival.phase = Phase::Garbage;
    rival.wait = 24;
    auto& player = hold.player[1];
    player.field = splitAttackField(
        "000000" "400000" "310410" "244130" "114341" "123413" "214433"
        "231114" "432321" "131221" "141432" "144232" "222421" "114422");
    player.scoring = {361, 11, false};
    player.moves = 39;
    player.wait = 1;

    struct Probe : ai::X3Agent {
        using X3Agent::X3Agent;

        static unsigned offeredFromSpawn(const Observation& o) {
            ai::InputPlanner planner;
            ai::InputPath path;
            const auto spawn = spawnPair(o.timing);
            unsigned reached = 0;
            for (unsigned legal = legalPlacements(o.self().field); legal; legal &= legal - 1) {
                const int id = std::countr_zero(legal);
                if (planner.plan(o.self().field, spawn, placement(id), o.timing, o.rules.speedPercent, path)) reached |= 1u << id;
            }
            return reached;
        }

        std::pair<int, Kind> pick(const Observation& o, uint64_t seed) {
            startRound(o.me, seed);
            const int id = choose(o, o.mine().pair(o.self().nextPair), offeredFromSpawn(o));
            return {id, kind};
        }

        void checkExact(const Observation& o, uint64_t seed) {
            const auto legal = offeredFromSpawn(o);
            const auto beam = searchBuild(o, 0);
            const int rate = o.rules.rateAt(o.frame);
            const auto ask = [&](const ai::PlanSet& opponent, auto expired) {
                return ai::preferKnownPartialBuild(o, mine, opponent, beam, 14, legal, p, seed + o.self().moves, rate, expired);
            };
            int polls = 0;
            check(ask(theirs, [&] { ++polls; return false; }) == 3 && polls > 4,
                  "the partial offset is supported by complete concrete exchanges");
            const int deadline = polls - 1;
            polls = 0;
            check(ask(theirs, [&] { return ++polls >= deadline; }) == 14 && polls >= deadline,
                  "a deadline during the comparison keeps the ordinary build");
            auto onePair = theirs;
            onePair.pairCount = 1;
            check(ask(onePair, [] { return false; }) == 14,
                  "the second opposing pair must be available to the concrete reading");
        }
    };

    auto p = ai::levelParams({}, 37);
    p.searchThreads = 1;
    p.beamWidth = 250;
    p.beamDepth = 16;
    p.beamQueues = 8;
    p.beamFixedQueues = 4;
    p.thinkBudget = 0;
    auto ordinary = p;
    ordinary.checkKnownExchange = false;
    constexpr uint64_t pressuredSeed = 13761247434487866029ull;
    constexpr uint64_t holdSeed = 12786619812055743890ull;
    Probe agent(p), legacy(ordinary);
    check(legacy.pick(pressured, pressuredSeed) == std::pair{14, Probe::Build}, "the ordinary beam spends the visible main on a single");
    check(agent.pick(pressured, pressuredSeed) == std::pair{3, Probe::Build}, "the normal Lv37 choice limits the known partial offset loss");
    agent.checkExact(pressured, pressuredSeed);
    check(agent.pick(hold, holdSeed) == std::pair{5, Probe::Build} && legacy.pick(hold, holdSeed) == std::pair{5, Probe::Build},
          "a quiet build retains its stronger idle possibilities against a weak visible response");

    Observation quiet;
    quiet.tsumo.pairs = {{1,1}};
    quiet.player[0].wait = 1;
    quiet.player[1].wait = 20;
    constexpr int wanted = 7;
    const Pair mono = quiet.mine().pair(0);
    ai::InputPlanner planner;
    ai::InputPath canonical, selected;
    const auto spawn = spawnPair(quiet.timing);
    const unsigned legal = legalPlacements(quiet.self().field);
    check(planner.plan(quiet.self().field, spawn, placement(wanted), quiet.timing, quiet.rules.speedPercent, canonical),
          "the canonical quiet path is physically available");
    int alternate = wanted;
    check(planner.planEquivalent(quiet.self().field, spawn, alternate, legal, mono, quiet.timing, quiet.rules, selected) &&
              alternate == ai::reversePlacement(wanted) && selected.frames() < canonical.frames(),
          "ordinary input can change the time of the quiet same-colour placement");

    ai::PlanSet a, b;
    ai::Gaze gaze;
    gaze.plans(quiet, 0, true, 1, p, 1, a);
    gaze.plans(quiet, 1, false, 1, p, 1, b);
    ai::Plan one;
    one.count = 1;
    one.id[0] = wanted;
    const auto replay = [&](bool equivalent) {
        ai::KnownReplay guarded{[] { return false; }};
        guarded.acceptPlacement = [&](const ai::Duel& d, int who, Placement target, Pair pair, int64_t from, int64_t lock) {
            return ai::stablePartialBuildRoute(d, who, target, pair, from, lock, equivalent);
        };
        return ai::playOut(quiet, a, &one, b, nullptr, p, 1, true, nullptr, &guarded);
    };
    check(replay(false).player[0].moves == 1 && replay(true).player[0].moves == 0,
          "a quiet alternate interrupts the exact replay only when equivalent input is enabled");

    auto tall = quiet;
    for (int y = 0; y < 12; ++y) tall.player[0].field.f.set(1, y, GARBAGE);
    for (int y = 0; y < 11; ++y) tall.player[0].field.f.set(DEATH_X, y, GARBAGE);
    const unsigned tallLegal = legalPlacements(tall.self().field);
    one.id[0] = 12;
    check(!(tallLegal >> 12 & 1u) && (tallLegal >> 1 & 1u), "only the alternate is legal on the high column");
    bool rejected = false;
    ai::KnownReplay alternateOnly{[] { return false; }};
    alternateOnly.acceptPlacement = [&](const ai::Duel& d, int who, Placement target, Pair pair, int64_t from, int64_t lock) {
        rejected = lock == -1 && !ai::stablePartialBuildRoute(d, who, target, pair, from, lock, true);
        return !rejected;
    };
    ai::playOut(tall, a, &one, b, nullptr, p, 1, true, nullptr, &alternateOnly);
    check(rejected, "the exchange also checks an orientation with no canonical path");

    ai::Duel blocked(quiet);
    const auto stable = [&](Pair pair, int64_t lock) {
        return ai::stablePartialBuildRoute(blocked, 0, placement(wanted), pair, 0, lock, true);
    };
    check(stable({1,2}, 5), "a mixed pair never changes orientation");
    blocked.player[0].field.f.set(1, 0, 1), blocked.player[0].field.f.set(1, 1, 1);
    check(stable(mono, 5), "an immediate clear keeps its canonical input");
    blocked.player[0].field = {};
    blocked.player[1].phase = ai::DuelPhase::Check;
    blocked.player[1].chain.chains = 1;
    blocked.player[1].at = 10;
    check(stable(mono, 5) && !stable(mono, 10), "a running chain blocks equivalent input only within its known interval");
}

static void testKnownAnswerWindow(int pictureWait = 0, bool cancel = false) {
    const auto parse = [](const char* cells) {
        GameField field;
        for (int y = ROW_14; y >= 0; --y) for (int x = 0; x < W; ++x) {
            const Cell c = Cell(*cells++ - '0');
            if (y == ROW_14) field.row14[x] = c;
            else field.f.set(x, y, c);
        }
        return field;
    };
    ai::Observation o;
    o.rules.cancel = false;
    o.rules.colors = 4;
    o.rules.preInput = true;
    o.tsumo.pairs = {{3,3}, {1,3}, {1,4}, {3,1}, {4,4}};
    auto& self = o.player[0];
    auto& rival = o.player[1];
    self.wait = 1;
    self.nextPair = 2;
    self.scoring = {255, 255, false};
    self.incoming.pending = 8;
    self.field = parse("000000110000220000413030212330133110134431222321444343232433221122333432211233221244");
    rival.phase = Phase::Wave;
    rival.wait = 77;
    rival.chain.chains = 2;
    rival.scoring = {605, 45, false};
    rival.field = parse("000000000000000000000001200002242203343303342302114211122441333222242433224312441122");

    if (pictureWait) {
        o.timing = play::realgame::realGameTiming();
        o.rules.cancel = cancel;
        rival.wait = pictureWait;
    }
    const auto params = ai::levelParams(ai::Params{}, 37);
    ai::Gaze gaze;
    ai::PlanSet ours, theirs;
    gaze.plans(o,0,true,3,params,1,ours);
    gaze.plans(o,1,false,3,params,1,theirs);
    const auto known = [&](int first, int second = -1) -> const ai::Plan& {
        for (const auto* list : {&ours.plans,&ours.tactical,&ours.keeping}) for (const auto& plan : *list) {
            const int count = second < 0 ? 1 : 2;
            if (plan.count == count && plan.fireDepth == count && plan.id[0] == first && (second < 0 || plan.id[1] == second)) return plan;
        }
        throw std::runtime_error("the answer fixture lost its visible route");
    };
    const auto& ordinary = known(10,3);
    const auto& immediate = known(21);
    const auto& prepared = known(20,16);
    ai::Duel confirmed(o,1);
    ai::toPairOf(confirmed,1,o.frame+6000);
    const auto ask = [&](const ai::Observation& view, const ai::Plan& answer, const ai::Params& profile, const auto& stop) {
        return ai::answer_window::prefer(view,ours,theirs,answer,&immediate,confirmed,legalPlacements(view.self().field),8,profile,1,stop);
    };
    const auto deadlineOpen = [] { return false; };
    if (pictureWait) {
        const int held[2]{};
        ai::PictureRoutes routes(2,held,true,true,false);
        const int picked = ai::answer_window::prefer(o,ours,theirs,ordinary,&immediate,confirmed,
            legalPlacements(o.self().field),8,params,1,deadlineOpen,&routes);
        std::cout << "held picture answer " << int(ordinary.id[0]) << " -> " << picked << ", cancel " << cancel << "\n";
        check(picked == 20, "the held picture answer must choose the proven alternative before the chain lands");
        return;
    }
    for (bool ex : {false, true}) {
        auto picture = o;
        picture.rules.cancel = ex;
        ai::Duel visible(picture,1);
        ai::toPairOf(visible,1,picture.frame+6000);
        const int held[2]{};
        ai::PictureRoutes routes(2, held, true, true, false);
        const auto legal = legalPlacements(picture.self().field);
        const int selected = ai::answer_window::prefer(picture,ours,theirs,ordinary,&immediate,visible,legal,8,params,1,deadlineOpen,&routes);
        std::cout << "held answer window: cancel " << ex << ", selected " << selected << "\n";
        check(selected < 0 || (selected == 20 && ai::picturePlacementInput(routes.first,picture.self().field,
              spawnPair(picture.timing),placement(selected),picture.timing,picture.rules.speedPercent).frames > 0),
              "held answer must qualify on the actual input path");
        check(ai::answer_window::prefer(picture,ours,theirs,ordinary,&immediate,visible,legal,8,params,1,[] { return true; },&routes)<0,
              "a held answer past its deadline retains the ordinary answer");
    }
    check(ask(o,ordinary,params,deadlineOpen) == 20, "the first answer keeps both an early full offset and a visible main");
    check(ask(o,prepared,params,deadlineOpen) < 0, "the existing first placement with both answers is kept");
    auto onlyOne = o;
    onlyOne.tsumo.pairs.resize(3);
    check(ask(onlyOne,ordinary,params,deadlineOpen) < 0, "an unseen pair cannot be used to prove the main");
    int calls = 0;
    check(ask(o,ordinary,params,[&] { return ++calls >= 25; }) < 0 && calls >= 25,
          "a comparison interrupted in progress keeps the ordinary answer");
    check(ask(o,ordinary,ai::levelParams(ai::Params{},36),deadlineOpen) < 0, "Lv36 keeps its ordinary answer");
    for (int who : {0,1}) {
        auto allClear = o;
        allClear.player[who].scoring.allClear = true;
        check(ask(allClear,ordinary,params,deadlineOpen) < 0, "a stored all clear keeps its established tactics");
    }
    auto preInput = o;
    preInput.player[0].buttons = BTN_CW;
    check(ask(preInput,ordinary,params,deadlineOpen) < 0, "an altered spawn orientation invalidates the canonical input clock");
}

static void testCommittedAnswerExchange(bool withClock = false) {
    Observation o;
    o.frame = 1509;
    o.me = 1;
    o.ownTsumo = true;
    o.tsumo.pairs = {{2,2}, {1,4}};
    o.tsumo1.pairs = {{1,4}, {3,2}, {3,2}};
    auto& foe = o.player[0];
    foe.field = splitAttackField(
        "000000" "200000" "440000" "411000" "212000" "442020" "423440"
        "224120" "414421" "413312" "223112" "211432" "331314" "444114");
    foe.moves = 32;
    foe.wait = 5;
    foe.scoring = {355, 5, false};
    foe.chain.chains = 1;
    auto& own = o.player[1];
    own.field = splitAttackField(
        "300000" "300000" "200000" "420000" "421030" "211023" "233422"
        "413113" "414422" "414311" "222141" "313132" "343412" "111442");
    own.moves = 31;
    own.wait = 1;
    own.scoring = {249, 249, false};
    own.incoming.confirmed = 5;

    struct Probe : ai::X3Agent {
        using X3Agent::X3Agent;
        int64_t decisionUs = 0;

        static unsigned offered(const Observation& view) {
            ai::InputPlanner planner;
            ai::InputPath path;
            const auto spawn = spawnPair(view.timing);
            unsigned mask = 0;
            for (unsigned ids = legalPlacements(view.self().field); ids; ids &= ids - 1) {
                const int id = std::countr_zero(ids);
                if (planner.plan(view.self().field, spawn, placement(id), view.timing, view.rules.speedPercent, path)) mask |= 1u << id;
            }
            return mask;
        }

        std::pair<int, Kind> pick(const Observation& view, uint64_t seed) {
            startRound(view.me, seed);
            const unsigned legal = offered(view);
            const auto start = std::chrono::steady_clock::now();
            const int id = choose(view, view.mine().pair(view.self().nextPair), legal);
            decisionUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
            return {id, kind};
        }

        void checkExact(const Observation& view, uint64_t seed) {
            const unsigned legal = offered(view);
            const auto compare = [&](const Observation& current, const ai::PlanSet& opposing, auto stop) {
                return ai::preferCommittedAnswer(current, mine, opposing, 9, legal, 5, p, seed + view.self().moves, stop);
            };
            int polls = 0;
            check(compare(view, theirs, [&] { ++polls; return false; }) == 14 && polls > 50,
                  "the stronger committed answer reduces the worst concrete partial offset");
            const int deadline = polls - 7;
            polls = 0;
            check(compare(view, theirs, [&] { return ++polls >= deadline; }) == 9 && polls >= deadline,
                  "an incomplete committed-answer comparison keeps the ordinary placement");
            auto limited = theirs;
            limited.knownCount = limited.pairCount = 1;
            check(compare(view, limited, [] { return false; }) == 9, "an unobserved second pair cannot establish a counter");
            auto bonus = view;
            bonus.player[1 - view.me].scoring.allClear = true;
            check(compare(bonus, theirs, [] { return false; }) == 9, "a stored all-clear keeps the ordinary answer");
            auto clears = mine;
            for (auto& plan : clears.plans) if (plan.id[0] == 9 && plan.count == 1) plan.allClear = true;
            check(ai::preferCommittedAnswer(view, clears, theirs, 9, legal, 5, p, seed, [] { return false; }) == 9,
                  "an immediate all-clear keeps the ordinary answer");

            const unsigned foeLegal = legalPlacements(view.opponent().field);
            bool differentTime = false;
            for (unsigned ids = foeLegal; ids && !differentTime; ids &= ids - 1) {
                const int id = std::countr_zero(ids), reverse = ai::reversePlacement(id);
                if (id >= reverse || !(foeLegal >> reverse & 1u)) continue;
                ai::Plan route;
                route.count = 1;
                route.id[0] = int8_t(id);
                const auto a = ai::playOut(view, mine, nullptr, theirs, &route, p, seed, true);
                route.id[0] = int8_t(reverse);
                const auto b = ai::playOut(view, mine, nullptr, theirs, &route, p, seed, true);
                const auto& first = a.player[1 - view.me];
                const auto& second = b.player[1 - view.me];
                differentTime = first.moves == view.opponent().moves + 1 && second.moves == first.moves &&
                    first.field == second.field && first.lockFrame != second.lockFrame;
            }
            check(differentTime, "visible same-colour orientations can leave the same field at different times");
        }
    };
    auto p = ai::levelParams({}, 37);
    p.beamWidth = 250;
    p.beamDepth = 16;
    p.beamQueues = 8;
    p.beamFixedQueues = 4;
    p.searchThreads = 1;
    p.thinkBudget = 0;
    auto ordinary = p;
    ordinary.checkKnownExchange = false;
    constexpr uint64_t seed = 8191215416231582517ull;
    Probe baseline(ordinary), tested(p);
    check(baseline.pick(o, seed) == std::pair{9, Probe::Answer}, "the ordinary answer commits the smaller main");
    check(tested.pick(o, seed) == std::pair{14, Probe::Answer}, "the normal Answer entry chooses the stronger immediate offset");
    tested.checkExact(o, seed);

    if (withClock) {
        p.thinkBudget = 180;
        Probe timed(p);
        check(timed.pick(o, seed) == std::pair{14, Probe::Answer}, "the committed answer fits the standard clock on the fixed position");
        std::cout << "committed answer with clock: " << timed.decisionUs << " us\n";
    }
}

// Define this after the AI test assertions and call testSideResponse() from main.
static void testSideResponse() {
    using namespace juraic;
    using namespace juraic::ai;
    const auto check = [](bool ok, const char* text) {
        if (!ok) throw std::runtime_error(text);
    };
    Observation o;
    o.frame = 1487;
    o.ownTsumo = true;
    o.rules.cancel = false;
    o.tsumo.pairs = {{4,2}, {2,1}, {1,1}};
    o.tsumo1.pairs = {{3,1}, {3,4}};
    const auto board = [](std::string_view rows, GameField& field) {
        int y = H - 1, x = 0;
        for (const char c : rows) {
            if (c == '/') { --y; x = 0; } else field.f.set(x++, y, Cell(c - '0'));
        }
    };
    board("000000/000123/020214/011241/243442/441223/212323/221413/413244/113314/432411/442443/333224", o.player[0].field);
    board("200000/100000/200001/100033/220434/114231/133231/241324/241144/214223/311342/342132/322132", o.player[1].field);
    o.player[0].phase = Phase::Next;
    o.player[0].wait = 1;
    o.player[0].scoring.pool = 263;
    o.player[0].incoming.pending = 4;
    o.player[1].phase = Phase::Wave;
    o.player[1].wait = 35;
    o.player[1].scoring.pool = 19;
    o.player[1].chain.chains = 1;
    const Params p = agentParams("lv37", {});
    constexpr uint64_t seed = 3856410095300094479ull;
    Gaze gaze;
    const auto choose = [&](const Observation& view, const Params& params, int ordinary, side_response::Ready& ready, int stopAfter = 0) {
        PlanSet mine, theirs;
        gaze.plans(view, 0, true, 3, params, seed, mine);
        gaze.plans(view, 1, false, 3, params, seed, theirs);
        Duel after(view, seed);
        toPairOf(after, 1, view.frame + 1000);
        int visited = 0;
        return side_response::prefer(view, after, mine, theirs, ordinary, legalPlacements(view.self().field),
            after.player[0].incoming.total(), params, seed, ready, [&] { return stopAfter > 0 && ++visited >= stopAfter; });
    };
    side_response::Ready ready;
    check(choose(o, p, 1, ready) == 11 && ready.next == 8, "A short, visible full offset keeps the main chain");
    {
        // The other seat's chain as the memory reader shows it: landing, with the waves of the chain still to come.
        Observation landing = o;
        landing.player[1].phase = Phase::Land;
        side_response::Ready shown;
        check(chainShown(landing.player[1]) && choose(landing, p, 1, shown) == 11 && shown.next == 8,
              "A chain shown landing is answered as one shown in its wave");
    }
    Duel after(o, seed);
    toPairOf(after, 1, o.frame + 1000);
    PlanSet ours, rival;
    gaze.plans(o, 0, true, 3, p, seed, ours);
    gaze.plans(o, 1, false, 3, p, seed, rival);
    const auto window = side_response::quietWindow(o, after, rival, [] { return false; });
    check(window.firstWave == 1682 && window.firstConfirm == 1737, "Bound the first unseen blast without using its colors");
    // A green double in the second enemy preview opens an executable main.
    auto targeted = o;
    targeted.tsumo1.pairs[1] = {2, 2};
    targeted.me = 1;
    rival.pair[1] = {2, 2};
    Plan reply;
    reply.count = 2;
    reply.id[0] = 15;
    reply.id[1] = 2;
    KnownReplay replay{[] { return false; }};
    ReplayCheckpoint settled;
    const Duel direct = playOut(targeted, rival, &reply, ours, nullptr, p, seed, true, nullptr, &replay, nullptr, &settled);
    check(settled.duel.rules && direct.player[1].moves == 2 && direct.player[1].chain.chains == 12,
          "The visible enemy main must have real buttons");
    side_response::Ready beforeMain;
    targeted.me = 0;
    check(choose(targeted, p, 1, beforeMain) == 1 && beforeMain.next < 0, "Do not ignore a visible enemy main");
    Observation next = o;
    next.frame = 1519;
    next.player[1].wait = 3;
    next.player[0].moves = 1;
    next.player[0].nextPair = 1;
    next.player[0].field = ready.field;
    check(choose(next, p, 7, ready) == 8, "Recheck the selected response at the next playable pair");

    side_response::Ready stopped;
    check(choose(o, p, 1, stopped, 1) == 1 && stopped.next < 0, "Preserve the ordinary choice when expired");
    check(choose(o, p, 1, stopped, 250) == 1 && stopped.next < 0, "Do not use an unfinished reply enumeration");
    check(choose(next, p, 7, ready, 1) == 7, "A carried response must be rechecked before use");
    auto alternate = next;
    alternate.tsumo1.pairs.push_back({3,3});
    check(choose(alternate, p, 7, ready) == 7, "Newly visible enemy information invalidates the carried scope");
    alternate = o;
    alternate.tsumo.pairs.resize(1);
    check(choose(alternate, p, 1, stopped) == 1, "Need both pairs for a setup and short response");
    alternate = o;
    alternate.player[0].scoring.allClear = true;
    check(choose(alternate, p, 1, stopped) == 1, "Keep the ordinary use of an own all-clear bonus");
    alternate = o;
    alternate.player[1].scoring.allClear = true;
    check(choose(alternate, p, 1, stopped) == 1, "Keep the ordinary answer to an enemy all-clear bonus");
    check(choose(o, agentParams("lv36", {}), 1, stopped) == 1, "Keep the level 36 choice");
    check(choose(o, agentParams("x3", {}), 1, stopped) == 1, "Keep the Ex choice");
}

#include "agent_air_tests.h"

#include "fastest_read_tests.h"

#include "judgement_tests.h"
#include "r4_board_tests.h"
#include "r6_tests.h"

// Mark the rival as prefetched without a pending job: takeRival must fail independently of machine speed.
static void testMissedRivalNote() {
    struct Probe : ai::X3Agent {
        using X3Agent::X3Agent;
        void miss(const Observation& o) {
            noting = true;
            beginChoice(o, true);
            gaze.plans(o, 1 - o.me, false, 1, p, 7, theirs);
            Rival rival;
            prepareRival(o, 7, rival, o.frame + 60);
            rival.prefetched = true;
            ai::Plan attack;
            attack.count = attack.fireDepth = 1;
            attack.sent = 12;
            attack.endFrame = o.frame + 60;
            outlasts(o, 0, o.rules.rateAt(o.frame), 7, rival, &attack);
            check(rivalMissed == 1 && poor && notes.size() == 1 && notes.back().cut == 1,
                  "a missed rival search must be recorded on this choice");
        }
        bool restore(const Observation& o) { beginChoice(o, true); return poor; }
    };
    ai::Params p;
    p.thinkBudget = 30;
    p.checkKnownExchange = 0;
    const auto o = ai::observe(situation(Rules{}, {}, {}, {1, 2}), 0);
    Probe timed(p);
    timed.startRound(0, 7);
    timed.miss(o);
    p.thinkBudget = 0;
    Probe replay(p);
    replay.startRound(0, 7);
    replay.script = &timed.notes;
    check(replay.restore(o), "a recorded miss must preserve the first choice on replay");
}

static void testR4Changes() {
    testR4BoardCases();
    testMissedRivalNote();
    testDuelOwnSeatBeforeAppears();
    testChosenInAirReach();
    testExchangePoke();
    testSetupSpeed();
    testSmallReceive();
    testLv38TakeSmallNoDeath();
    std::cout << "round 4 regression tests passed\n";
}


int main(int argc, char** argv) {
    std::cout << std::unitbuf; // (every line out at once: a run that is stopped still tells how far it came)
    try {
        if (argc == 2 && std::string(argv[1]) == "reserve") {
            testLevelTable();
            testKnownPokeReserve();
            testKnownPokeHandoff();
            std::cout << "level defaults, reserve choice and handoff passed\n";
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "picture37") {
            testLv38TakeSmallNoDeath();
            const auto raw = ai::agentParams("lv38ex", {});
            auto explicitOff = ai::namedDefaults("lv38ex");
            explicitOff.lv38TakeSmall = 0;
            check(raw.lv38TakeSmall == 1 && raw.lv38CutAhead == 2 &&
                  ai::configuredParams("lv38ex", explicitOff).lv38TakeSmall == 0, "named defaults and explicit options");
            testKnownAnswerWindow();
            testKnownAnswerWindow(86, false);
            testKnownAnswerWindow(86, true);
            testOpenSetup();
            std::cout << "picture37 checks passed\n";
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "r4") {
            testR4Changes();
            return 0;
        }
        if (argc >= 2 && argc <= 3 && std::string(argv[1]) == "r6") {
            testR6Changes(argc == 3 ? std::string(argv[2]) : std::string());
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "eval-focus") {
            testQuickTrials();
            testBuild();
            benchDetect();
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "bench") {
            if (argc > 2 && std::string(argv[2]) == "build") {
                // further arguments: name=value lines for ai/params.h
                ai::Params p;
                std::string lines, error;
                for (int i = 3; i < argc; ++i) lines += std::string(argv[i]) + '\n';
                std::istringstream in(lines);
                if (!ai::loadParams(in, p, error)) throw std::runtime_error("parameter " + error);
                benchBuild(1, p), benchBuild(1, p), benchBuild(6, p);
                return 0;
            }
            benchDetect();
            benchDuel();
            return 0;
        }
        testR4BoardCases();
        testMissedRivalNote();
        testR6Changes();
        testObserve();
        testDuel();
        testDuelSameFrame();
        testDuelOrder();
        testDuelTall();
        testDuelCancel();
        testRivalSeenCheck();
        testDuelOwnSeat();
        testDuelWildWait();
        testDuelOwnWay();
        testDuelTablesCancel();
        testDuelSplitSoonest();
        testFastestEndKept();
        testSpawnInputCacheSame();
        testDuelOwnSeatBeforeAppears();
        testTallSpawnInputCache();
        testInputStraight();
        testInputHigh(argc > 1 ? std::atoi(argv[1]) : 2000);
        testDetect();
        testBuild();
        testValue();
        testGaze();
        testAirRest();
        testMidairSameColorOffset();
        testQuietPoke();
        testLevelTable();
        testKnownSearchPairs();
        testKnownExchange();
        testDuelValue();
        testOffsetsIn();
        testChainRunning();
        testWinRecheckUnfinished();
        testBeamCut();
        testExchangePoke();
        testMidgamePressure();
        testMidgameBeam();
        testMidgamePoke();
        testPressureFollowUp();
        testKnownMainReturn();
        testMidgameShelter();
        testSetupSpeed();
        testLv38Tail();
        testLv38AnswerWindowBoundary();
        testGazeDeadline();
        testEquivalentSetupRoute();
        testMidgameAttackRetention();
        testMidgamePreparation();
        testSmallReceive();
        testLv38TakeSmallNoDeath();
        testStandingSpare();
        testExchangeSpare();
        testMidgameDig();
        testLevels();
        testBase();
        testTear();
        testKeys();
        testQuickTrials();
        testOutlast();
        testJudgement();
        testSearchBy();
        testThinking();
        testAskInAirWay();
        testAskInAirReach();
        testChosenInAirReach();
        testNearestWayOverSoonest();
        testThinkingWayRead();
        testSecondChoice();
        testSearchThreads();
        testBudget();
        testRivalAhead();
        testSwitches();
        testSoonKeep();
        testEnough();
        testAnswer();
        testUsedRow14();
        testTactics();
        testSplitMainAttack();
        testWinImmediateFire();
        testConfirmedOffsetCoverage();
        testBuildKeepsVisibleMain();
        testKnownPokeReserve();
        testKnownPokeHandoff();
        testKnownPrefixTiming();
        testSideResponse();
        testCommittedAnswerExchange();
        testKnownAnswerWindow();
        testKnownAnswerWindow(86, false);
        testKnownAnswerWindow(86, true);
        testKnownPartialBuild();
        testOpenSetup();
        testPokeAgainstStoredAllClear();
        testSetupKeepsVisibleAnswer();
        testSmallReplyExchange();
        testRunningChainOffsetRetention();
        testVisiblePressureBeforeFire();
        std::cout << "ai tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
