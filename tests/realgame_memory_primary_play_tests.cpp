// play/memory/primary_play14.h on rounds of this program's engine shown as the real game's memory is read
// (play/memory/primary_madeup14.h): an AI played through PrimaryPlayer14 puts its pairs where it chose, with buttons
// that reach the game at once and some frames late, with frames no sample comes for, with the frame counts of the
// engine as they are and with the rules of the other model and the ways over soonest.
#include "../ai/agents.h"
#include "../play/memory/primary_madeup14.h"
#include "../play/memory/primary_play14.h"
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
using namespace juraic;
using namespace play::realgame;

constexpr int DIFFS = 16; // (Played::diff)
struct Played {
    int frames = 0, locks = 0, misses = 0, firstMisses = 0, untargeted = 0, idle = 0, air = 0;
    long skipped = 0, filled = 0, recovered = 0, corrections = 0, repaired = 0;
    long long steered = 0, rerouted = 0, faster = 0, saved = 0;
    long rounds = 0; // times the AI was told a round begins
    int before = 0;  // pairs put that had appeared before the player joined (joinAt)
    long long revised = 0; // choices the AI dropped and made again (PlacementAgent::revisedChoices)
    int shownBefore = 0;   // frames the AI was shown waiting for the first pair of the round, before the frame it appears in
    long long aheadUsed = 0, aheadMissed = 0; // ways taken from a search made before / searched in the frame itself (InputPlanner)
    long long switched = 0, noPath = 0, held = 0, againWays = 0, begun = 0; // the second choices (PlacementAgent::againSwitched and those beside it)
    int measured = -1, measuredRun = 0;       // the frames a button took as the player measured them, and taps in a row that gave them
    RoundState state = RoundState::Playing;
    std::vector<int> placements; // where the own pairs were put, one after the other
    std::vector<int> appeared;   // ... and the frame the pair after each of them could first be moved in (0: the round ended first)
    GameField field[2];
    // Frames in which what the AI was shown was not as the round had it: [0] frames compared, [1] the own all-clear
    // bonus, [2] the other's, [3] the own garbage waiting in all, [4] that of it that is sure, [5] the own field (a pair
    // in the hands or about to be), [6] the other's field (likewise), [7] fewer pairs known, [8] the other's chain
    // running and not shown as one, [9] the other's garbage waiting in all, [10] a chain shown that does not run.
    // ... [11] the frame, [12] the frames to the own pair to come (from the last busy frame on), [13] a pair known that is
    // not the round's, [14] the other's pair in the hands, [15] the own points carried over (not read from the game).
    long diff[DIFFS]{};
};
// The two rounds are one: as long, the own pairs put alike, the same end.
static bool sameRound(const Played& a, const Played& b) { return a.frames == b.frames && a.placements == b.placements && a.state == b.state; }

struct Setup {
    uint64_t seed = 1;
    Timing timing = DEFAULT_TIMING;
    int delay = 0;         // frames a button takes to reach the game
    int playerDelay = -1;  // ... and the frames the player reckons it takes, when not those (-1: those)
    bool direct = false;   // the AI is shown the round itself and its buttons are played at once (what to compare with)
    const char* own = "random";
    bool fastest = false;  // PlacementAgent::fastestWays
    int dropOne = 0;       // one sample in so many does not come (0: none)
    int dropRun = 1;       // ... and so many after it neither
    int limit = 4000;      // frames
    int me = 1;
    bool trace = false;    // a line for every frame
    const Timing* reckoned = nullptr; // the frame counts the AI reckons with, when not those of the round
    int otherMissing = 0;  // the other seat is not in one sample in so many, nor in up to seven after it (0: always there)
    int unplaced = 0;      // in one sample in so many the place of the own pair is not read (0: always)
    int joinAt = 0;        // the first sample the player gets is that of this frame (a round joined on its way)
    bool opening = false;  // the window shows the first pairs before the first pair of the round is in the hands
    int againLate = 0;     // Params::thinkLate: the second choice comes so many frames after the pair has appeared
    bool hold = false;     // PlacementAgent::holdSecond and askInAir, as on the real game
    bool keepWay = false;  // PlacementAgent::holdKeepsWay
    int again = 0;         // PlacementAgent::fastestAgain
    const char* rival = "random"; // the AI of the other seat
    bool level = false;    // PlacementAgent::holdLevel
    bool real = false;     // the other settings of the real game's driver: waitQuickLock, the choice begun 12 frames before the pair
};

// The counter in the frame the first pair of an engine round is in the hands, as madeUpSample14 shows it.
static int64_t engineFirstTick(const Timing& timing) {
    Round round;
    round.start(Rules{}, 1, 2, timing);
    for (int f = 0; f < 100; ++f) {
        const uint8_t none[2]{};
        PlayerEvents events[2];
        round.step(none, events);
        const PrimarySample14 s = madeUpSample14(round);
        if (s.seat[0]->pose.operating) return int64_t(s.seat[0]->pose.count108);
    }
    return -1;
}
// A seat made up by hand.
static PrimarySeat14 seatOf(int seat, uint32_t tick, uint32_t put, bool operating, const std::array<Pair, 3>& pairs, bool window = false) {
    PrimarySeat14 s;
    OwnedPublicPose14& p = s.pose;
    p.seat = seat, p.count108 = tick, p.fieldState = 1, p.playerState = 1, p.lockedPairs = put, p.operating = operating;
    if (operating) {
        const PairControl born;
        p.x = born.x, p.y = born.y, p.upper = born.upper, p.dir = born.dir;
        p.currentColors = {int(pairs[0].axis), int(pairs[0].child)};
        s.placed = true, s.pairsKnown = true, s.pairs = pairs;
    } else if (window) {
        s.windowKnown = true, s.window = pairs;
    }
    return s;
}

static Played playRound(const Setup& u) {
    Played out;
    const int me = u.me, them = 1 - me;
    ai::Params params;
    params.beamWidth = 30, params.beamDepth = 8, params.beamQueues = 3;
    params.thinkLate = u.againLate;
    // (the other seat's AI chooses as within this program, its second choice never late)
    ai::Params rivalParams = params;
    rivalParams.thinkLate = 0;
    auto own = ai::makeAgent(u.own, params), other = ai::makeAgent(u.rival, rivalParams);
    auto* placing = dynamic_cast<ai::PlacementAgent*>(own.get());
    if (!placing) return out;
    placing->fastestWays = u.fastest;
    if (u.hold) placing->holdSecond = placing->askInAir = true;
    placing->holdKeepsWay = u.keepWay, placing->fastestAgain = u.again;
    placing->holdLevel = u.level;
    if (std::getenv("HOLD_DOWN")) placing->holdDown = std::atoi(std::getenv("HOLD_DOWN")); // (a trial: PlacementAgent::holdDown)
    if (u.real) placing->waitQuickLock = true, placing->ahead = 12;
    // (as on the real game: the search made ahead - waited for, so that a round is the same whatever the threads do -
    //  and a short one in the frame itself)
    //  (with the rules of the other model alone a search of that many states takes seconds: fewer there)
    placing->fastestAhead = u.timing.restBoth ? PRIMARY_FASTEST_AHEAD14 : 20000, placing->fastestStates = PRIMARY_FASTEST_NOW14, placing->fastestWaits = true;
    // (the placement chosen some frames before the pair appears, as the agents of the game do: the search ahead begins then)
    if (u.fastest && placing->ahead < 8) placing->ahead = 8;
    own->startRound(me, u.seed + uint64_t(me)), other->startRound(them, u.seed + uint64_t(them));
    Round round;
    round.start(Rules{}, u.seed, 2, u.timing);
    PrimaryPlayer14 player(*own, me, u.playerDelay >= 0 ? u.playerDelay : u.delay);
    player.observer.rules = round.rules, player.observer.timing = u.reckoned ? *u.reckoned : round.timing;
    player.seed = u.seed + uint64_t(me);
    if (u.opening) player.observer.firstTick = engineFirstTick(u.timing);
    std::deque<uint8_t> pad(size_t(u.delay), 0);
    Rng drops(u.seed, 77);
    std::map<int, int> targets, firstTargets;
    uint8_t command = 0;
    bool pressed = false;
    int dropping = 0, otherGone = 0, spawnedAt = 0;
    bool chaining[2]{}, boardSet[2]{}, bonusMark[2]{};
    GameField board[2]; // (u.real: the board the real game shows - the end of the last pair's chain, no garbage fallen since)
    int chainAll[2]{};
    int flyLeft[2]{}, flyAmount[2]{}, pendingWas[2]{};
    const auto aim = [&](const Player& p) {
        if (placing->targetPlacement() < 0 || (p.phase != Phase::Control && p.phase != Phase::Next)) return;
        const int pair = p.nextPair - (p.phase == Phase::Control ? 1 : 0);
        targets[pair] = placing->targetPlacement();
        if (p.phase == Phase::Control || p.wait == 1) firstTargets.emplace(pair, placing->targetPlacement());
    };
    for (; out.frames < u.limit && round.state == RoundState::Playing; ++out.frames) {
        uint8_t mine = 0;
        if (u.direct) {
            const auto shown = ai::observe(round, me);
            mine = own->frame(shown);
            aim(shown.self());
        } else {
            pad.push_back(command);
            mine = pad.front();
            pad.pop_front();
        }
        uint8_t buttons[2]{};
        buttons[them] = other->frame(ai::observe(round, them)), buttons[me] = mine;
        if (round.player[me].phase == Phase::Control) ++out.air, pressed = pressed || mine;
        PlayerEvents events[2];
        round.step(buttons, events);
        // (u.real: the chain a pair begins, as the real game has it from the frame the pair is put - its end and its garbage)
        for (int i = 0; i < 2 && u.real; ++i) {
            if (events[i].locked) {
                BitField f = round.player[i].field.f;
                int points = 0, chains = 0;
                for (Wave w; findWaveFor(f, w, round.rules.clearCount);) {
                    if (round.rules.clearTopGarbage) addTopGarbage(f, w);
                    points += gameWaveScore(w, ++chains);
                    f.erase(w.cleared | w.garbage), f.drop();
                }
                chaining[i] = chains > 0;
                chainAll[i] = chains >= round.rules.chainRequirement ? points / round.rules.rateAt(round.frame) : 0;
                board[i] = round.player[i].field, board[i].f = f, boardSet[i] = true;
                if (chaining[i]) bonusMark[i] = false; // (the mark goes in the frame the pair of the chain that takes the bonus is put)
            }
            if (events[i].chainEnded) chaining[i] = false;
            if (events[i].allClear) bonusMark[i] = true; // (and comes in the frame the chain that empties the field is over)
        }
        if (events[me].spawned) {
            pressed = mine != 0, spawnedAt = out.frames;
            if (out.appeared.size() < out.placements.size()) out.appeared.push_back(out.frames);
        }
        // (a round joined on its way: the pairs that appeared before the player had a sample are not its own)
        if (events[me].locked && u.joinAt && spawnedAt <= u.joinAt) {
            ++out.before;
        } else if (events[me].locked) {
            ++out.locks;
            const int pair = round.player[me].nextPair - 1, id = placementId(round.player[me].pair.placement());
            out.placements.push_back(id);
            const auto t = targets.find(pair);
            if (t == targets.end()) ++out.untargeted;
            else out.misses += id != t->second;
            const auto f = firstTargets.find(pair);
            if (f != firstTargets.end()) out.firstMisses += id != f->second;
            out.idle += !pressed;
            if (u.trace) std::printf("LOCK pair %d at %d aimed %d first %d\n", pair, id, t == targets.end() ? -1 : t->second, f == firstTargets.end() ? -1 : f->second);
        }
        if (u.direct) continue;
        const auto line = [&](const char* what) {
            if (!u.trace) return;
            const Player& r = round.player[me];
            const Player& s = player.shown().self();
            const Player& p = player.foreseen().self();
            std::printf("f %d got %2d %s| real ph %d w %d np %d at %d,%d,%d,%d lock %d | cmd %2d | shown f %lld ph %d w %d np %d at %d,%d,%d,%d | fore f %lld ph %d w %d np %d at %d,%d,%d,%d | aim %d corr %ld rep %ld rec %ld\n",
                        out.frames, mine, what, int(r.phase), r.wait, r.nextPair, r.pair.x, r.pair.y, int(r.pair.upper), r.pair.dir, int(events[me].locked), command,
                        (long long)player.shown().frame, int(s.phase), s.wait, s.nextPair, s.pair.x, s.pair.y, int(s.pair.upper), s.pair.dir, (long long)player.foreseen().frame,
                        int(p.phase), p.wait, p.nextPair, p.pair.x, p.pair.y, int(p.pair.upper), p.pair.dir, placing->targetPlacement(), player.corrections(), player.repaired(),
                        player.recovered);
        };
        // (the sample of the frame just played; one in dropOne does not come, nor the dropRun - 1 after it)
        if (dropping > 0) {
            --dropping;
            line("none ");
            continue;
        }
        if (u.dropOne && drops.below(uint32_t(u.dropOne)) == 0) {
            dropping = u.dropRun - 1;
            line("none ");
            continue;
        }
        const std::array<int, 2> notice{u.real ? round.player[0].incoming.total() : round.player[0].incoming.confirmed,
                                        u.real ? round.player[1].incoming.total() : round.player[1].incoming.confirmed};
        if (out.frames < u.joinAt) continue;
        PrimarySample14 sample = madeUpSample14(round, 1, 10, u.opening);
        for (int i = 0; i < 2 && u.real; ++i) {
            if (!sample.seat[size_t(i)]) continue;
            PrimarySeat14& t = *sample.seat[size_t(i)];
            const Player& q = round.player[i];
            int pieces = 0, garbage = 0;
            for (int x = 0; x < W; ++x) {
                for (int y = 0; y < 13; ++y) pieces += q.field.f.get(x, y) != EMPTY, garbage += q.field.f.get(x, y) == GARBAGE;
                pieces += q.field.row14[size_t(x)] != EMPTY, garbage += q.field.row14[size_t(x)] == GARBAGE;
            }
            t.counted = true, t.pieces = pieces, t.garbagePieces = garbage;
            // (the board the real game shows: garbage fallen since the last pair is in the counts alone, by column too)
            if (boardSet[i]) t.field = board[i];
            if (chaining[i]) t.chainGarbage = uint32_t(chainAll[i]);
            t.columnsKnown = true, t.sixthHeight = 0;
            for (int x = 0; x < W; ++x) {
                int n = q.field.row14[size_t(x)] != EMPTY;
                for (int y = 0; y < 13; ++y) n += q.field.f.get(x, y) != EMPTY;
                t.columnPieces[size_t(x)] = uint8_t(n);
            }
            for (int y = 0; y < 13; ++y)
                if (q.field.f.get(5, y) != EMPTY) t.sixthHeight = y + 1;
            t.bonusKnown = true, t.bonusWaits = bonusMark[i];
            // (LISTED: the pairs beyond the window as read from the game's own list - PrimarySeat14::listed)
            if (std::getenv("LISTED") && t.pairsKnown && int(t.pose.lockedPairs) + 3 + int(LISTED14) <= TSUMO_PAIRS) {
                t.listedKnown = true;
                for (size_t k = 0; k < LISTED14; ++k) t.listed[k] = round.tsumo.pair(int(t.pose.lockedPairs) + 3 + int(k));
            }
            t.noticeKnown = true, t.notice = q.incoming.total();
            // (what a wave sends is on its way for some twenty frames, as on the real game, before it waits above the field)
            if (q.incoming.pending > pendingWas[i]) flyAmount[i] = q.incoming.pending - pendingWas[i], flyLeft[i] = 20;
            pendingWas[i] = q.incoming.pending;
            const int flying = flyLeft[i] > 0 ? std::min(flyAmount[i], q.incoming.pending) : 0;
            if (flyLeft[i] > 0) --flyLeft[i];
            t.trayKnown = true, t.trayChain = chaining[1 - i], t.trayFlying = flying, t.trayPending = q.incoming.pending - flying, t.trayConfirmed = q.incoming.confirmed;
        }
        if (u.otherMissing && (otherGone > 0 || drops.below(uint32_t(u.otherMissing)) == 0)) {
            otherGone = otherGone > 0 ? otherGone - 1 : int(drops.below(8));
            sample.seat[size_t(them)].reset();
        }
        if (u.unplaced && sample.seat[size_t(me)]->pose.operating && drops.below(uint32_t(u.unplaced)) == 0) {
            PrimarySeat14& mine = *sample.seat[size_t(me)];
            mine.placed = false, mine.pairsKnown = false, mine.pose.x = mine.pose.y = mine.pose.dir = -1, mine.pose.currentColors = {0, 0};
        }
        command = player.frame(sample, notice);
        // 回戦終了で測定器を初期化する前の、対戦中の結果を検証する。
        if (round.state == RoundState::Playing) out.measured = player.measured(), out.measuredRun = player.measuredRun();
        if (player.rounds && player.shown().self().phase == Phase::Next && player.shown().self().nextPair == 0 && player.shown().self().wait > 1 && !round.player[me].moves) ++out.shownBefore;
        if (player.foreseenWhole()) aim(player.foreseen().self());
        if (u.real && round.state == RoundState::Playing && player.rounds) {
            const ai::Observation& a = player.shown();
            const ai::Observation b = ai::observe(round, me);
            const auto quiet = [](const Player& q) { return q.phase == Phase::Control || q.phase == Phase::Next; };
            ++out.diff[0];
            out.diff[1] += a.self().scoring.allClear != b.self().scoring.allClear;
            out.diff[2] += a.opponent().scoring.allClear != b.opponent().scoring.allClear;
            out.diff[3] += quiet(b.opponent()) && a.self().incoming.total() != b.self().incoming.total();
            out.diff[4] += a.self().incoming.confirmed != b.self().incoming.confirmed;
            out.diff[5] += quiet(b.self()) && !(a.self().field == b.self().field);
            out.diff[6] += quiet(b.opponent()) && !(a.opponent().field == b.opponent().field);
            out.diff[7] += a.tsumo.count() < b.tsumo.count();
            out.diff[8] += b.opponent().phase == Phase::Wave && a.opponent().phase != Phase::Land;
            out.diff[9] += quiet(b.self()) && a.opponent().incoming.total() != b.opponent().incoming.total();
            out.diff[10] += a.opponent().phase == Phase::Land && quiet(b.opponent());
            out.diff[11] += a.frame != b.frame;
            out.diff[12] += b.self().phase == Phase::Next && b.self().wait <= 11 && a.self().phase == Phase::Next && a.self().wait != b.self().wait;
            bool pairs = true;
            for (int k = 0; k < std::min(a.tsumo.count(), b.tsumo.count()); ++k)
                pairs = pairs && (a.tsumo.pair(k).axis == b.tsumo.pair(k).axis && a.tsumo.pair(k).child == b.tsumo.pair(k).child);
            out.diff[13] += !pairs;
            const Player &ao = a.opponent(), &bo = b.opponent();
            out.diff[14] += bo.phase == Phase::Control && (ao.phase != Phase::Control || ao.pair.x != bo.pair.x || ao.pair.y != bo.pair.y || ao.pair.dir != bo.pair.dir ||
                                                         ao.colors.axis != bo.colors.axis || ao.colors.child != bo.colors.child);
            out.diff[15] += a.self().scoring.pool != b.self().scoring.pool;
        }
        line("     ");
    }
    out.state = round.state;
    out.skipped = player.skipped, out.filled = player.filled, out.recovered = player.recovered, out.corrections = player.corrections(), out.repaired = player.repaired();
    out.steered = placing->steered, out.rerouted = placing->rerouted, out.rounds = player.rounds, out.revised = placing->revisedChoices;
    out.faster = placing->ways().fasterWays, out.saved = placing->ways().framesSaved;
    out.aheadUsed = placing->ways().aheadUsed, out.aheadMissed = placing->ways().aheadMissed;
    out.switched = placing->againSwitched, out.noPath = placing->againNoPath, out.held = placing->againHeld, out.againWays = placing->againWays, out.begun = placing->againBegun;
    out.field[0] = round.player[0].field, out.field[1] = round.player[1].field;
    return out;
}

static void print(const char* what, const Setup& u, const Played& p) {
    std::printf("%-34s seed %llu delay %d frames %d locks %d misses %d first %d untargeted %d idle %d air/lock %.2f skipped %ld filled %ld recovered %ld corrections %ld repaired %ld steered %lld rerouted %lld faster %lld saved %lld ahead %lld now %lld\n",
                what, (unsigned long long)u.seed, u.delay, p.frames, p.locks, p.misses, p.firstMisses, p.untargeted, p.idle, p.locks ? double(p.air) / p.locks : 0.0,
                p.skipped, p.filled, p.recovered, p.corrections, p.repaired, p.steered, p.rerouted, p.faster, p.saved, p.aheadUsed, p.aheadMissed);
}

// 記録にあったクイックターンを3回続けても、入力遅延2とは判定しない。
static bool testQuickTurnDelay() {
    struct Buttons : ai::Agent {
        uint8_t held = 0;
        void startRound(int, uint64_t) override {}
        uint8_t frame(const ai::Observation&) override { return held; }
    } agent;
    PrimaryPlayer14 player(agent, 1, 0);
    player.observer.timing = primaryTiming14(true);
    const std::array<Pair, 3> pairs{{{Cell(1), Cell(2)}, {Cell(3), Cell(4)}, {Cell(2), Cell(3)}}};
    const auto feed = [&](int tick, int pair, int dir, uint8_t buttons) {
        PrimarySample14 sample;
        sample.generation = 1;
        sample.seat[0] = seatOf(0, uint32_t(tick), uint32_t(pair), true, pairs);
        sample.seat[1] = seatOf(1, uint32_t(tick), uint32_t(pair), true, pairs);
        sample.seat[1]->pose.dir = dir;
        if (pair < 3) for (int y = 0; y < 12; ++y) {
            sample.seat[1]->field.f.set(1, y, GARBAGE);
            sample.seat[1]->field.f.set(3, y, GARBAGE);
        }
        agent.held = buttons;
        player.frame(sample, {0, 0});
    };
    int tick = 100;
    for (int pair = 0; pair < 3; ++pair) {
        for (int i = 0; i < 16; ++i) feed(tick++, pair, UP, 0);
        feed(tick++, pair, UP, BTN_CW);
        feed(tick++, pair, UP, 0);
        feed(tick++, pair, UP, BTN_CW);
        feed(tick++, pair, DOWN, 0);
    }
    if (player.measured() != -1 || player.measuredRun() != 0) return false;
    // 同じ測定器は、単独の回転が次のフレームで効く時には遅延0を得る。
    for (int pair = 3; pair < 6; ++pair) {
        for (int i = 0; i < 16; ++i) feed(tick++, pair, UP, 0);
        feed(tick++, pair, UP, BTN_CW);
        feed(tick++, pair, RIGHT, 0);
    }
    if (player.measured() != 0 || player.measuredRun() != 3) return false;
    // 回戦をまたぐ候補・連続回数は捨てる。
    feed(0, 0, UP, 0);
    return player.measured() == -1 && player.measuredRun() == 0;
}

static bool testMeasuredDelays() {
        bool right = true;
        for (int delay = 0; delay <= 4; ++delay) {
            Setup u;
            u.seed = 61, u.delay = delay, u.limit = 1500;
            const Played a = playRound(u);
            right = right && a.measured == delay; // (few taps tell with the delay reckoned right: most are sent before the pair appears)
            Setup v = u;
            v.playerDelay = 0; // (the player reckons with none: its pairs go astray, and the taps still tell)
            const Played b = playRound(v);
            right = right && b.measured == delay && b.measuredRun >= 3;
        }
    return right;
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "delay") {
        const bool ok = testQuickTurnDelay() && testMeasuredDelays();
        std::printf("回転の遅延測定: %s\n", ok ? "成功" : "失敗");
        return ok ? 0 : 1;
    }
    if (argc >= 3 && std::string(argv[1]) == "strength") {
        // strength <rounds> [rival x3] [frames the second choice comes late 5] [holdLevel 1] [cancels 1] [first seed 1]:
        // the same rounds played by the AI shown the round itself (as within this program) and through PrimaryPlayer14
        // with the settings of the real game's driver; rounds won by the AI's seat, and the frames a pair is in the hands.
        const int n = std::atoi(argv[2]);
        const char* rival = argc > 3 ? argv[3] : "x3";
        const int late = argc > 4 ? std::atoi(argv[4]) : 5;
        const bool level = argc > 5 ? std::atoi(argv[5]) != 0 : true, cancel = argc > 6 ? std::atoi(argv[6]) != 0 : true;
        const uint64_t first = argc > 7 ? std::strtoull(argv[7], nullptr, 10) : 1;
        int won[2]{}, lost[2]{}, locks[2]{}, misses = 0, untargeted = 0;
        long air[2]{}, frames[2]{};
        long long switched = 0, noPath = 0;
        long diff[DIFFS]{};
        for (int k = 0; k < n; ++k) {
            for (int way = std::getenv("MEMORY_ONLY") ? 1 : 0; way < (std::getenv("DIRECT_ONLY") ? 1 : 2); ++way) {
                Setup u;
                u.seed = first + uint64_t(k), u.own = "x3", u.rival = rival, u.limit = 12000;
                u.timing = primaryTiming14(cancel), u.fastest = cancel;
                // (DIRECT_REAL: bit 1 the settings of the real game's driver for the AI shown the round itself as well,
                //  bit 2 the late second choice as well)
                const int both = std::getenv("DIRECT_REAL") ? std::atoi(std::getenv("DIRECT_REAL")) : 0;
                if (way == 0) u.direct = true;
                else u.opening = true; // (the real game's window shows the first pairs before the first pair is in the hands)
                if (way == 1 || (both & 1)) u.hold = true, u.real = true, u.level = level, u.again = 1;
                if (way == 1 || (both & 2)) u.againLate = late;
                if (std::getenv("NO_SECOND")) u.hold = false, u.level = false, u.again = 0, u.againLate = 0;
                // (DROP_ONE: through the memory one sample in so many does not come, as the reader misses a frame now and then)
                if (way == 1 && std::getenv("DROP_ONE")) u.dropOne = std::atoi(std::getenv("DROP_ONE"));
                const Played p = playRound(u);
                won[way] += p.state == RoundState::Won1, lost[way] += p.state == RoundState::Won0;
                locks[way] += p.locks, air[way] += p.air, frames[way] += p.frames;
                if (way == 1) misses += p.misses, untargeted += p.untargeted, switched += p.switched, noPath += p.noPath;
                for (int d = 0; d < DIFFS && way == 1; ++d) diff[d] += p.diff[d];
            }
            if ((k + 1) % 20 == 0 || k + 1 == n) {
                std::printf("rounds %d: shown the round itself won %d lost %d, in the hands %.2f frames a pair, %.2f frames a pair in all | through the memory won %d lost %d, %.2f, %.2f; misses %d, untargeted %d, switched %lld, no path %lld\n",
                            k + 1, won[0], lost[0], locks[0] ? double(air[0]) / locks[0] : 0.0, locks[0] ? double(frames[0]) / locks[0] : 0.0, won[1], lost[1],
                            locks[1] ? double(air[1]) / locks[1] : 0.0, locks[1] ? double(frames[1]) / locks[1] : 0.0, misses, untargeted, switched, noPath);
                if (k + 1 == n) {
                    std::printf("shown against the round, frames not the same of %ld:", diff[0]);
                    for (int d = 1; d < DIFFS; ++d) std::printf(" [%d] %ld", d, diff[d]);
                    std::printf("\n");
                }
                std::fflush(stdout);
            }
        }
        return 0;
    }
    if (argc == 6 || argc == 7) {
        // (for looking at one round frame by frame: seed, delay, dropOne, dropRun, other rules 0/1[, frames])
        Setup u;
        u.seed = std::strtoull(argv[1], nullptr, 10), u.delay = std::atoi(argv[2]), u.dropOne = std::atoi(argv[3]), u.dropRun = std::atoi(argv[4]);
        if (std::atoi(argv[5])) u.timing = OTHER_MODEL_TIMING, u.fastest = true;
        if (argc == 7) u.limit = std::atoi(argv[6]);
        if (std::getenv("JOIN_AT")) u.joinAt = std::atoi(std::getenv("JOIN_AT"));
        if (std::getenv("OWN_X3")) u.own = "x3";
        // (the ways of a pair whose second choice comes in the air, with the frame counts of the real game)
        if (std::getenv("REAL")) u.timing = primaryTiming14(true), u.fastest = true;
        if (std::getenv("AGAIN_LATE")) u.againLate = std::atoi(std::getenv("AGAIN_LATE"));
        if (std::getenv("HOLD")) u.hold = true;
        if (std::getenv("KEEP")) u.keepWay = true;
        if (std::getenv("AGAIN")) u.again = std::atoi(std::getenv("AGAIN"));
        u.trace = !std::getenv("QUIET");
        const Played one = playRound(u);
        print("one round", u, one);
        std::printf("revised choices %lld, rounds begun %ld\n", one.revised, one.rounds);
        std::printf("second choices begun %lld, taken in the air %lld, no way %lld, frames held %lld, ways searched once more and taken %lld\n", one.begun, one.switched, one.noPath,
                    one.held, one.againWays);
        return 0;
    }
    int checks = 0, failed = 0;
    const auto check = [&](bool b, const char* s) {
        ++checks;
        if (!b) ++failed, std::printf("FAIL %s\n", s);
    };
    check(testQuickTurnDelay(), "クイックターンを入力遅延と誤認しない。回戦間の測定を引き継がない");
    // Buttons that reach the game at once: the round is the one the AI plays when shown the round itself.
    for (int rules = 0; rules < 2; ++rules) {
        bool same = true, clean = true;
        int locks = 0;
        for (uint64_t seed = 1; seed <= 6; ++seed) {
            Setup u;
            u.seed = seed, u.timing = rules ? OTHER_MODEL_TIMING : DEFAULT_TIMING, u.fastest = rules != 0;
            Setup d = u;
            d.direct = true;
            const Played a = playRound(u), b = playRound(d);
            if (seed <= 2) print(rules ? "at once, other rules, soonest ways" : "at once", u, a), print("  shown the round itself", d, b);
            same = same && sameRound(a, b) && a.field[0] == b.field[0] && a.field[1] == b.field[1];
            clean = clean && !a.misses && !a.firstMisses && !a.untargeted && !a.idle && !a.recovered && !a.skipped && !a.repaired;
            locks += a.locks;
        }
        check(locks > 100, rules ? "other rules: rounds were played" : "rounds were played");
        check(same, rules ? "other rules, the ways over soonest, buttons at once: the round the AI plays when shown the round itself"
                          : "buttons at once: the round the AI plays when shown the round itself");
        check(clean, rules ? "other rules: every pair where the AI chose, none left alone, none taken up, no pair put right" : "every pair where the AI chose, none left alone, none taken up, no pair put right");
    }
    // The other seat not read now and then (for up to eight frames in a row), the place of the own pair not read now
    // and then: the round is still the one the AI plays when shown the round itself.
    for (int kind = 0; kind < 2; ++kind) {
        bool same = true;
        int locks = 0;
        for (uint64_t seed = 61; seed <= 64; ++seed) {
            Setup u;
            u.seed = seed, u.otherMissing = kind == 0 ? 5 : 0, u.unplaced = kind == 1 ? 4 : 0;
            Setup d = u;
            d.direct = true;
            const Played a = playRound(u), b = playRound(d);
            if (seed == 61) print(kind ? "the own pair's place not read" : "the other seat not read", u, a);
            same = same && sameRound(a, b) && !a.recovered && !a.misses && !a.idle && a.rounds == 1;
            locks += a.locks;
        }
        check(locks > 60 && same, kind ? "the place of the own pair not read in a sample in four: the round the AI plays when shown the round itself, no pair taken up"
                                       : "the other seat not read in a sample in five and up to seven after: the round the AI plays when shown the round itself, begun once");
    }
    // A round joined on its way (the seats bound late): the pair in the air then is taken up where it is, and every
    // pair from the next on goes where the AI chose, none of its choices made twice.
    {
        int locks = 0, bad = 0, taken = 0;
        long long revised = 0;
        for (uint64_t seed = 71; seed <= 74; ++seed) {
            Setup u;
            u.seed = seed, u.joinAt = 200 + int(seed) * 7, u.own = seed % 2 ? "random" : "x3", u.limit = 2000;
            const Played a = playRound(u);
            if (seed == 71) print("joined on its way", u, a);
            locks += a.locks, bad += a.misses + a.idle + a.untargeted, taken += int(a.recovered), revised += a.revised;
        }
        check(locks > 60 && bad == 0 && revised == 0, "a round joined on its way: every pair where the AI aimed from then on, no choice made twice");
    }
    // Buttons that reach the game late.
    for (int rules = 0; rules < 2; ++rules)
        for (int delay = 1; delay <= 4; ++delay) {
            int locks = 0, bad = 0, taken = 0, air = 0, airDirect = 0, locksDirect = 0;
            for (uint64_t seed = 11; seed <= 14; ++seed) {
                Setup u;
                u.seed = seed, u.delay = delay, u.timing = rules ? OTHER_MODEL_TIMING : DEFAULT_TIMING, u.fastest = rules != 0;
                Setup d = u;
                d.direct = true, d.delay = 0;
                const Played a = playRound(u), b = playRound(d);
                if (seed == 11) print(rules ? "late, other rules, soonest ways" : "late", u, a);
                locks += a.locks, bad += a.misses + a.firstMisses + a.untargeted + a.idle, taken += int(a.recovered), air += a.air;
                airDirect += b.air, locksDirect += b.locks;
            }
            char text[160];
            std::snprintf(text, sizeof text, "buttons %d frames late%s: every pair where the AI chose first, none left alone", delay, rules ? ", other rules" : "");
            check(locks > 60 && bad == 0, text);
            // (the first pair of a round is seen only as it appears: its buttons are late, and it is taken up in the air)
            std::snprintf(text, sizeof text, "buttons %d frames late%s: only the first pair of a round is taken up in the air", delay, rules ? ", other rules" : "");
            check(taken <= 4, text);
            // (other rounds than with buttons at once - the first pair is put elsewhere -, so only roughly)
            std::snprintf(text, sizeof text, "buttons %d frames late%s: the pairs are in the air about as long as with buttons at once (%.2f and %.2f frames a pair)", delay,
                          rules ? ", other rules" : "", locks ? double(air) / locks : 0.0, locksDirect ? double(airDirect) / locksDirect : 0.0);
            check(locks && locksDirect && double(air) / locks <= double(airDirect) / locksDirect + 2.0, text);
            std::printf("  %s\n", text);
        }
    // Frames no sample comes for (one in six, and two in a row one time in twelve).
    for (int run = 1; run <= 2; ++run)
        for (int delay = 0; delay <= 3; ++delay) {
            int locks = 0, bad = 0, idle = 0;
            long skipped = 0, filled = 0;
            for (uint64_t seed = 21; seed <= 24; ++seed) {
                Setup u;
                u.seed = seed, u.delay = delay, u.dropOne = run == 1 ? 6 : 12, u.dropRun = run;
                const Played a = playRound(u);
                print(run == 1 ? "one sample in 6 missing" : "two in a row missing", u, a);
                locks += a.locks, bad += a.misses + a.untargeted, idle += a.idle, skipped += a.skipped, filled += a.filled;
            }
            char text[160];
            std::snprintf(text, sizeof text, "samples missing (%d in a row), buttons %d frames late: no pair left alone, the frames played on as the engine has them (but runs of more than six)", run, delay);
            check(locks > 60 && skipped > 50 && filled * 10 >= skipped * 9 && idle == 0, text);
            // (in a frame no sample came for the buttons of the frame before stay: a pair that appears on a field nearly
            //  full, the down button held, is then locked before it is turned - one pair in a hundred or so here)
            std::snprintf(text, sizeof text, "samples missing (%d in a row), buttons %d frames late: every pair where the AI last aimed, but two at most", run, delay);
            check(bad <= 2, text);
        }
    // X3 (a small search), buttons two frames late: its second choices and its view of the other side included.
    {
        Setup u;
        u.seed = 31, u.delay = 2, u.own = "x3", u.limit = 2500;
        const Played a = playRound(u);
        print("x3, late", u, a);
        check(a.locks > 25 && !a.misses && !a.untargeted && !a.idle, "X3 with buttons 2 frames late: every pair where it last aimed, none left alone");
        Setup v = u;
        v.timing = OTHER_MODEL_TIMING, v.fastest = true, v.seed = 32;
        const Played b = playRound(v);
        print("x3, late, other rules, soonest", v, b);
        check(b.locks > 25 && !b.misses && !b.untargeted && !b.idle, "X3, other rules and the ways over soonest, buttons 2 frames late: every pair where it last aimed");
    }
    // The game puts its pairs later than the AI reckons (the rules of the other model in the game, not in the AI's frame
    // counts), and sooner (the other way round): the pairs still go where the AI aimed, and are not left lying.
    for (int way = 0; way < 2; ++way)
        for (int delay = 0; delay <= 3; ++delay) {
            int locks = 0, bad = 0, air = 0, airSame = 0, locksSame = 0;
            const Timing plain = DEFAULT_TIMING, other = OTHER_MODEL_TIMING;
            for (uint64_t seed = 51; seed <= 54; ++seed) {
                Setup u;
                u.seed = seed, u.delay = delay, u.timing = way ? plain : other, u.reckoned = way ? &other : &plain;
                Setup same = u;
                same.reckoned = nullptr;
                const Played a = playRound(u), b = playRound(same);
                if (seed == 51) print(way ? "the game sooner than reckoned" : "the game later than reckoned", u, a);
                locks += a.locks, bad += a.misses + a.untargeted + a.idle, air += a.air, airSame += b.air, locksSame += b.locks;
            }
            char text[200];
            std::snprintf(text, sizeof text, "the game puts its pairs %s than the AI reckons, buttons %d frames late: every pair where the AI last aimed (%.2f frames a pair in the air, %.2f reckoned right)",
                          way ? "sooner" : "later", delay, locks ? double(air) / locks : 0.0, locksSame ? double(airSame) / locksSame : 0.0);
            check(locks > 60 && bad == 0 && locksSame && double(air) / locks <= double(airSame) / locksSame + 2.0, text);
            std::printf("  %s\n", text);
        }
    // The first pair of a round: the window shows it before it is in the hands, and the AI is shown the frames before
    // it - with buttons at once the round is still the one the AI plays when shown the round itself, with late buttons
    // the first pair is not taken up in the air any more.
    {
        bool same = true, clean = true;
        int locks = 0, before = 0, beforeWithout = 0;
        for (uint64_t seed = 81; seed <= 84; ++seed) {
            Setup u;
            u.seed = seed, u.opening = true, u.own = seed % 2 ? "random" : "x3", u.limit = 1500;
            Setup d = u;
            d.direct = true;
            Setup w = u;
            w.opening = false;
            const Played a = playRound(u), b = playRound(d), c = playRound(w);
            if (seed == 81) print("the first pair known before it appears", u, a);
            const bool equal = sameRound(a, b);
            std::printf("  seed %llu %s: %s the round shown itself; without the window: %s\n", (unsigned long long)seed, u.own, equal ? "the same as" : "another than",
                        c.frames == b.frames && c.placements == b.placements ? "the same" : "another");
            same = same && (equal || seed % 2 == 0); // (X3 goes by more of what it is shown than the round has: not the same round either way)
            clean = clean && !a.misses && !a.firstMisses && !a.untargeted && !a.idle && !a.recovered && !a.revised && a.rounds == 1;
            locks += a.locks, before += a.shownBefore, beforeWithout += c.shownBefore;
        }
        check(locks > 40 && before >= 16 && beforeWithout == 0, "the first pair of a round known from the window: the AI is shown the frames before it appears");
        check(same, "... and the round is the one the AI plays when shown the round itself (the AI that goes by its pairs alone)");
        check(clean, "... every pair where the AI chose, none left alone, no choice made twice, the round begun once");
        int taken = 0, bad = 0;
        for (uint64_t seed = 85; seed <= 88; ++seed) {
            Setup u;
            u.seed = seed, u.opening = true, u.delay = 2, u.limit = 1500;
            const Played a = playRound(u);
            taken += int(a.recovered), bad += a.misses + a.firstMisses + a.untargeted + a.idle;
        }
        check(taken == 0 && bad == 0, "... with buttons 2 frames late the first pair is not taken up in the air, every pair where the AI chose first");
    }
    // The first pair of a round as the real game's memory shows it: nothing of the round until the window stands, then the
    // frames to the pair; the frame numbers those of a round whose first pair appears in the frame Timing::firstSpawn.
    {
        const std::array<Pair, 3> none{}, first{Pair{Cell(2), Cell(1)}, Pair{Cell(1), Cell(1)}, Pair{Cell(1), Cell(4)}};
        const std::array<int, 2> notice{0, 0};
        const Timing timing = primaryTiming14(false);
        for (int from = 9; from >= 2; from -= 7) {
            PrimaryObserver14 seen(Rules{}, timing);
            ai::Observation o;
            bool early = false, good = true;
            for (uint32_t tick = 1; tick <= 11; ++tick) {
                PrimarySample14 s;
                s.generation = 1, s.serial = tick;
                const bool hands = tick >= 10;
                for (int i = 0; i < 2; ++i) s.seat[size_t(i)] = seatOf(i, tick, 0, hands, hands || int(tick) >= from ? first : none, int(tick) >= from);
                const bool ok = seen.observe(s, 1, notice, o);
                const int64_t frame = int64_t(tick); // (the frames are counted from the frame the round is made ready in)
                if (int(tick) < from || frame < 0) {
                    early = early || ok;
                    continue;
                }
                good = good && ok && o.frame == frame && o.mine().count() == 3 && o.mine().pair(2).child == Cell(4) && o.self().nextPair == (tick == 11 ? 1 : 0) && o.player[0].moves == 0;
                if (tick < 10) good = good && o.self().phase == Phase::Next && o.self().wait == int(11 - tick) && o.player[0].phase == Phase::Next && o.player[0].wait == int(11 - tick);
                if (tick == 10) good = good && o.self().phase == Phase::Next && o.self().wait == 1 && seen.shownOnly(1);
                if (tick == 11) good = good && o.self().phase == Phase::Control && o.self().colors.axis == Cell(2);
            }
            check(!early, from == 9 ? "the observer: before the first pair of a round, nothing while the window does not stand" : "the observer: ... nor while the first pair is not known");
            check(good, from == 9 ? "the observer: the window standing a frame before the first pair: the AI is shown that frame, the pair two frames off"
                                  : "the observer: the window standing earlier: the AI is shown the frames to the first pair from the round's frame 0 on");
        }
    }
    // A round of the real game from the frame it is made ready in: the window holds the first two pairs then (its first
    // record empty), slides, and stands a frame before the first pair with the third; between two pairs it stands with
    // the pair just put, slides, and stands with the next three a frame before the pair is in the hands.
    {
        const auto P = [](int a, int b) { return Pair{Cell(a), Cell(b)}; };
        const std::array<Pair, 3> none{}, first{P(4, 3), P(4, 2), P(4, 4)}, second{P(4, 2), P(4, 4), P(2, 5)};
        const std::array<int, 2> notice{0, 0};
        PrimaryObserver14 seen(Rules{}, primaryTiming14(false));
        ai::Observation o;
        const auto window = [&](PrimarySeat14 seat, uint8_t stage, bool begun, const std::array<Pair, 3>& held) {
            // (the window not standing: how far its read came and its records - before the first pair the first is empty)
            seat.windowKnown = false, seat.windowRead.stage = stage;
            seat.windowRead.slide = {stage == PrimaryWindow14::Sliding ? 1u : 0u, 0x40000000u, 0x3e124925u};
            for (size_t k = 0; k < 3; ++k) {
                const bool empty = !begun && k == 0;
                const Pair pair = begun ? held[k] : k ? held[k - 1] : Pair{};
                seat.windowRead.records[k] = {empty ? 0u : 0x201u, uint32_t(pair.axis), uint32_t(pair.child)};
            }
            return seat;
        };
        const auto both = [&](uint32_t tick, const PrimarySeat14& mine, const PrimarySeat14& theirs) {
            PrimarySample14 s;
            s.generation = 1, s.serial = tick + 1;
            s.seat[1] = mine, s.seat[0] = theirs;
            s.seat[1]->pose.count108 = s.seat[0]->pose.count108 = tick;
            return seen.observe(s, 1, notice, o);
        };
        // (made ready: the counter at 0, the seats not yet playing)
        PrimarySeat14 made0 = window(seatOf(0, 0, 0, false, none), PrimaryWindow14::Kind, false, first), made1 = window(seatOf(1, 0, 0, false, none), PrimaryWindow14::Kind, false, first);
        made0.pose.fieldState = made0.pose.playerState = made1.pose.fieldState = made1.pose.playerState = 0;
        bool ok = both(0, made1, made0);
        check(ok && o.frame == 0 && o.self().phase == Phase::Next && o.self().nextPair == 0 && o.self().wait == 11 && o.mine().count() == 2 && o.mine().pair(0).child == Cell(3) &&
                  o.mine().pair(1).child == Cell(2) && o.player[0].wait == 11,
              "the observer: a round being made ready is shown with its first two pairs, the first pair eleven frames off");
        bool good = true;
        for (uint32_t tick = 1; tick <= 8; ++tick) {
            const uint8_t stage = tick == 1 ? PrimaryWindow14::Kind : PrimaryWindow14::Sliding;
            ok = both(tick, window(seatOf(1, tick, 0, false, none), stage, false, first), window(seatOf(0, tick, 0, false, none), stage, false, first));
            good = good && ok && o.frame == tick && o.self().wait == int(11 - tick) && o.mine().count() == 2;
        }
        check(good, "the observer: ... and frame by frame as the counter goes on and the window slides");
        ok = both(9, seatOf(1, 9, 0, false, first, true), seatOf(0, 9, 0, false, first, true));
        check(ok && o.self().wait == 2 && o.mine().count() == 3 && o.mine().pair(2).axis == Cell(4), "the observer: the window standing a frame before the first pair: its third pair is known");
        ok = both(10, seatOf(1, 10, 0, true, first), seatOf(0, 10, 0, true, first));
        check(ok && o.frame == 10 && o.self().wait == 1 && o.self().phase == Phase::Next && seen.shownOnly(1), "the observer: the first pair in the hands, shown as about to appear");
        ok = both(11, seatOf(1, 11, 0, true, first), seatOf(0, 11, 0, true, first));
        // (the own pair put; the other seat still has its first)
        ok = ok && both(40, seatOf(1, 40, 1, false, first, true), seatOf(0, 40, 0, true, first));
        check(ok && o.self().phase == Phase::Next && o.self().nextPair == 1 && o.mine().count() == 3, "the observer: the window standing with the pair just put: nothing new is taken from it");
        ok = both(58, window(seatOf(1, 58, 1, false, none), PrimaryWindow14::Sliding, true, first), seatOf(0, 58, 0, true, first));
        check(ok && o.mine().count() == 3, "the observer: the window sliding: nothing new yet");
        ok = both(65, seatOf(1, 65, 1, false, second, true), seatOf(0, 65, 0, true, first));
        check(ok && o.mine().count() == 4 && o.mine().pair(3).child == Cell(5) && o.mine().pair(1).child == Cell(2),
              "the observer: the window standing again a frame before the next pair: its third pair is known a frame sooner");
        ok = both(66, seatOf(1, 66, 1, true, second), seatOf(0, 66, 0, true, first));
        check(ok && o.self().wait == 1 && o.mine().count() == 4, "the observer: ... and the pair appears as it was shown");
    }
    check(testMeasuredDelays(), "0〜4フレームの入力遅延を対戦中に正しく測定する");
    // Garbage that has fallen and is not in the board yet: what the game counts beyond the board's pieces is put on the
    // field shown, a sixth of it on every column; pieces left over are not, but for the sixth column, and the field is
    // then not sure. Nothing while the field is busy with no pair in the hands.
    {
        const std::array<Pair, 3> none{};
        PrimarySeat14 seat = seatOf(1, 300, 4, false, none);
        for (int x = 0; x < W; ++x)
            for (int y = 0; y < x; ++y) seat.field.f.set(x, y, Cell(1 + (x + y) % 4)); // (heights 0 1 2 3 4 5: 15 pieces)
        GameField shown;
        const auto heightOf = [&](int x) {
            int h = 0;
            for (int y = 0; y < 13; ++y)
                if (shown.f.get(x, y) != EMPTY) h = y + 1;
            return h;
        };
        auto h = PrimaryObserver14::withFallen(seat, shown);
        check(h.pieces == 0 && h.sure && shown == seat.field, "garbage not in the board: counts not read, the board as it is");
        seat.counted = true, seat.pieces = 15, seat.sixthHeight = 5;
        h = PrimaryObserver14::withFallen(seat, shown);
        check(h.pieces == 0 && h.sure && shown == seat.field, "garbage not in the board: the count of the board, nothing added");
        seat.pieces = 15 + 12, seat.garbagePieces = 12, seat.sixthHeight = 7;
        h = PrimaryObserver14::withFallen(seat, shown);
        bool rows = h.pieces == 12 && h.rows == 2 && h.sure;
        for (int x = 0; x < W; ++x) rows = rows && heightOf(x) == x + 2 && shown.f.get(x, x) == GARBAGE && shown.f.get(x, x + 1) == GARBAGE;
        check(rows, "garbage not in the board: twelve pieces more are two rows on every column, and sure");
        seat.pieces = 15 + 7, seat.garbagePieces = 7, seat.sixthHeight = 7;
        h = PrimaryObserver14::withFallen(seat, shown);
        check(h.pieces == 7 && h.rows == 1 && h.sure && heightOf(5) == 7 && heightOf(0) == 1 && heightOf(4) == 5,
              "garbage not in the board: seven pieces, the one left over on the sixth column by its height - sure");
        seat.pieces = 15 + 8, seat.garbagePieces = 8, seat.sixthHeight = 7;
        h = PrimaryObserver14::withFallen(seat, shown);
        check(h.pieces == 8 && h.rows == 1 && !h.sure && heightOf(5) == 7 && heightOf(0) == 1, "garbage not in the board: a piece left over on a column not known - not sure");
        seat.pieces = 15 + 2, seat.garbagePieces = 2, seat.sixthHeight = 5;
        h = PrimaryObserver14::withFallen(seat, shown);
        check(h.pieces == 2 && h.rows == 0 && !h.sure && shown == seat.field, "garbage not in the board: two pieces on columns not known - the board, not sure");
        seat.pieces = 15 + 12, seat.garbagePieces = 12, seat.sixthHeight = 7, seat.pose.fieldActive = 1, seat.pose.phase = 1;
        h = PrimaryObserver14::withFallen(seat, shown);
        check(h.pieces == 0 && shown == seat.field, "garbage not in the board: nothing is added while the field clears with no pair in the hands");
        seat.pose.phase = 0;
        // (the pieces of a chain the board has cleared already, still in the count: more pieces than garbage beyond the board)
        seat.pose.fieldActive = 0;
        seat.pieces = 15 + 12, seat.garbagePieces = 2, seat.sixthHeight = 7;
        h = PrimaryObserver14::withFallen(seat, shown);
        check(h.pieces == 0 && h.sure && shown == seat.field, "garbage not in the board: pieces counted beyond the board that are not all garbage are those of a chain to come - nothing added");
        // (a column near the top: no piece above the 13th row)
        for (int y = 0; y < 12; ++y) seat.field.f.set(0, y, Cell(1));
        seat.pieces = 15 + 12 + 12, seat.garbagePieces = 12, seat.sixthHeight = 7;
        h = PrimaryObserver14::withFallen(seat, shown);
        check(h.rows == 2 && heightOf(0) == 13 && heightOf(1) == 3, "garbage not in the board: no piece is put above the 13th row");
        // The columns the garbage fell on, by the pieces the game counts in each column: the field shown has the garbage
        // where it lies, and is sure. Counts that do not fit the board are not gone by.
        {
            PrimarySeat14 by = seatOf(1, 300, 4, false, none);
            for (int x = 0; x < W; ++x)
                for (int y = 0; y < x; ++y) by.field.f.set(x, y, Cell(1 + (x + y) % 4)); // (heights 0 1 2 3 4 5: 15 pieces)
            by.counted = true, by.pieces = 15 + 8, by.garbagePieces = 8, by.sixthHeight = 6;
            by.columnsKnown = true, by.columnPieces = {2, 2, 4, 4, 5, 6}; // (2, 1, 2, 1, 1 and 1 more than the board has)
            h = PrimaryObserver14::withFallen(by, shown);
            check(h.pieces == 8 && h.sure && h.byColumns && heightOf(0) == 2 && heightOf(1) == 2 && heightOf(2) == 4 && heightOf(3) == 4 && heightOf(4) == 5 && heightOf(5) == 6 &&
                      shown.f.get(0, 0) == GARBAGE && shown.f.get(0, 1) == GARBAGE && shown.f.get(1, 1) == GARBAGE && shown.f.get(1, 0) == by.field.f.get(1, 0) &&
                      shown.f.get(5, 5) == GARBAGE,
                  "garbage not in the board: with the pieces of each column counted, it lies on the columns it fell on - sure");
            by.columnPieces = {0, 0, 2, 3, 4, 14}; // (the second column with fewer than the board has)
            h = PrimaryObserver14::withFallen(by, shown);
            check(h.pieces == 8 && !h.byColumns && h.rows == 1 && !h.sure, "garbage not in the board: a column counted with fewer pieces than the board has - by rows, as without the counts");
            by.columnPieces = {1, 2, 3, 4, 5, 6}; // (six more than the board, where eight are counted)
            h = PrimaryObserver14::withFallen(by, shown);
            check(h.pieces == 8 && !h.byColumns && h.rows == 1, "garbage not in the board: the columns counted have not as many more as the field - by rows");
            // (a piece in the 14th row is counted with its column)
            by.field.row14[2] = Cell(3);
            by.pieces = 16 + 2, by.garbagePieces = 2, by.columnPieces = {1, 1, 3, 3, 4, 6};
            h = PrimaryObserver14::withFallen(by, shown);
            check(h.pieces == 2 && h.byColumns && h.sure && heightOf(0) == 1 && heightOf(5) == 6 && heightOf(2) == 2 && shown.row14[2] == Cell(3),
                  "garbage not in the board: a piece in the 14th row is one of its column's");
            // (a column thirteen high: what lies above it is left out, and the field is not sure)
            for (int y = 0; y < 13; ++y) by.field.f.set(0, y, Cell(1));
            by.pieces = 29 + 1, by.garbagePieces = 1, by.columnPieces = {14, 1, 3, 3, 4, 5};
            h = PrimaryObserver14::withFallen(by, shown);
            check(h.pieces == 1 && h.byColumns && !h.sure && heightOf(0) == 13, "garbage not in the board: a piece above the 13th row is left out - not sure");
        }
        // In a recorded fall two full columns lose their top garbage one frame apart. The garbage counter
        // follows the column counter one frame later. The piece on the shorter column stays on the field.
        {
            PrimarySeat14 drop = seatOf(1, 300, 4, false, none);
            drop.pose.fieldActive = 1, drop.pose.phase = 10;
            for (int x = 0; x < 2; ++x)
                for (int y = 0; y < 13; ++y) drop.field.f.set(x, y, Cell(1));
            drop.counted = drop.columnsKnown = true;
            drop.garbagePieces = 3, drop.pieces = 29, drop.columnPieces = {14, 14, 1, 0, 0, 0};
            PrimaryObserver14::FallColumns fall;
            h = PrimaryObserver14::withFallen(drop, shown, &fall);
            check(h.pieces == 3 && !h.sure && shown.f.get(2, 0) == GARBAGE, "落下中、上限を超えた列と残るおじゃまを区別する");
            ++drop.pose.count108, --drop.pieces, --drop.columnPieces[0];
            h = PrimaryObserver14::withFallen(drop, shown, &fall);
            check(h.pieces == 2 && !h.sure && h.byColumns && shown.f.get(2, 0) == GARBAGE, "最初の列から最上段が消えた後もほかの列を保つ");
            ++drop.pose.count108, --drop.pieces, --drop.garbagePieces, --drop.columnPieces[1];
            h = PrimaryObserver14::withFallen(drop, shown, &fall);
            check(h.pieces == 1 && h.sure && h.byColumns && shown.f.get(2, 0) == GARBAGE, "次の列から最上段が消えた後も残ったおじゃまを保つ");
            PrimarySeat14 changed = drop;
            --changed.columnPieces[2], ++changed.columnPieces[3];
            h = PrimaryObserver14::withFallen(changed, shown, &fall);
            check(!h.byColumns && h.pieces == 0 && shown == changed.field, "短い列まで変わった場合には直前の落下から推定しない");
            ++drop.pose.count108, --drop.garbagePieces;
            h = PrimaryObserver14::withFallen(drop, shown, &fall);
            check(h.pieces == 1 && h.sure && shown.f.get(2, 0) == GARBAGE, "おじゃま数の更新後も同じ列が残る");
        }
        // The garbage waiting while the other seat's chain runs: what stood before can fall, the rest cannot yet.
        PrimaryObserver14 seen(Rules{}, primaryTiming14(false));
        PrimarySeat14 other = seatOf(0, 300, 4, false, none);
        juraic::Incoming in = seen.waiting(1, 3, other);
        check(in.confirmed == 3 && in.pending == 0, "the garbage waiting: no chain of the other seat, all of the notice can fall");
        other.counted = true, other.chainGarbage = 20;
        in = seen.waiting(1, 3, other);
        check(in.confirmed == 3 && in.pending == 20, "the garbage waiting: the other seat's chain begins - what it makes in all cannot fall yet");
        in = seen.waiting(1, 9, other);
        check(in.confirmed == 3 && in.pending == 20, "the garbage waiting: the notice grows with the chain - still what stood before, and the chain's");
        in = seen.waiting(1, 30, other);
        check(in.confirmed == 3 && in.pending == 27, "the garbage waiting: the notice has more of the chain than was counted - that much");
        other.chainGarbage = 0;
        in = seen.waiting(1, 30, other);
        check(in.confirmed == 30 && in.pending == 0, "the garbage waiting: the chain over, all of the notice can fall");
        // With the game's own account of the garbage: what the game has as able to fall, what the chain has sent, and
        // of what is still to come only what the garbage above the chain's own field leaves.
        {
            PrimaryObserver14 exact(Rules{}, primaryTiming14(false));
            PrimarySeat14 own = seatOf(1, 300, 4, false, none), his = seatOf(0, 300, 4, false, none);
            own.trayKnown = his.trayKnown = true, own.trayConfirmed = 4;
            in = exact.waiting(1, 9, own, his);
            check(in.confirmed == 4 && in.pending == 0, "the game's account: what the game has as able to fall, not the number shown");
            his.counted = true, his.chainGarbage = 50, his.trayConfirmed = 30;
            in = exact.waiting(1, 9, own, his);
            check(in.confirmed == 4 && in.pending == 20, "the game's account: a chain of 50 with 30 above its own field - 20 are to come");
            his.trayConfirmed = 18;
            in = exact.waiting(1, 9, own, his);
            check(in.confirmed == 4 && in.pending == 20, "the game's account: the chain has taken 12 from above its field - still 20 to come");
            his.trayConfirmed = 0, own.trayFlying = 5;
            in = exact.waiting(1, 9, own, his);
            check(in.confirmed == 4 && in.pending == 20, "the game's account: 5 on their way, 15 more to come");
            own.trayFlying = 0, own.trayPending = 5;
            in = exact.waiting(1, 9, own, his);
            check(in.confirmed == 4 && in.pending == 20, "the game's account: arrived, the same");
            own.trayPending = 2;
            in = exact.waiting(1, 9, own, his);
            check(in.confirmed == 4 && in.pending == 17, "the game's account: the seat's own chain took 3 of them - 2 there, 15 to come");
            own.trayFlying = 15;
            in = exact.waiting(1, 9, own, his);
            check(in.confirmed == 4 && in.pending == 17, "the game's account: all of the chain's garbage made - 2 there, 15 on their way");
            own.trayFlying = 3, own.trayPending = 17;
            in = exact.waiting(1, 9, own, his);
            check(in.confirmed == 4 && in.pending == 20, "the game's account: more than the chain's garbage (points held over) - as the game has it");
            his.chainGarbage = 0, own.trayFlying = 0, own.trayPending = 0, own.trayConfirmed = 24;
            in = exact.waiting(1, 9, own, his);
            check(in.confirmed == 24 && in.pending == 0, "the game's account: the chain over, all of it can fall");
            // (a chain that is all taken by the garbage above its own field: nothing to come)
            his.chainGarbage = 439, his.trayConfirmed = 1413;
            in = exact.waiting(1, 9, own, his);
            check(in.confirmed == 24 && in.pending == 0, "the game's account: a chain smaller than the garbage above its own field sends nothing");
            // (the seat sends 10 while the chain runs: the chain takes from them first)
            PrimaryObserver14 both(Rules{}, primaryTiming14(false));
            PrimarySeat14 mine = seatOf(1, 300, 4, false, none), chaining = seatOf(0, 300, 4, false, none);
            mine.trayKnown = chaining.trayKnown = true, chaining.counted = true, chaining.chainGarbage = 50;
            in = both.waiting(1, 0, mine, chaining);
            check(in.confirmed == 0 && in.pending == 50, "the game's account: a chain of 50 with nothing above its field - 50 to come");
            chaining.trayFlying = 10;
            in = both.waiting(1, 0, mine, chaining);
            check(in.confirmed == 0 && in.pending == 40, "the game's account: 10 on their way to the chain's field - 40 to come");
            chaining.trayFlying = 0, chaining.trayPending = 10;
            in = both.waiting(1, 0, mine, chaining);
            check(in.pending == 40, "the game's account: arrived above the chain's field, the same");
            // (a step of the chain takes the 10 and sends 6, and 4 more go to the chain's field in the same frame)
            chaining.trayPending = 0, chaining.trayFlying = 4, mine.trayFlying = 6;
            in = both.waiting(1, 0, mine, chaining);
            check(in.pending == 6 + 30, "the game's account: taken, sent and come in one frame - 6 on their way, 34 left of which 4 will be taken");
            // A running chain's flying garbage can be cancelled before it reaches either tray.
            // Check both directions, including a second launch observed in the sample where the first has arrived.
            {
                PrimaryObserver14 flight(Rules{}, primaryTiming14(false));
                PrimarySeat14 receive = seatOf(1, 300, 4, false, none), send = seatOf(0, 300, 4, false, none);
                receive.trayKnown = send.trayKnown = true, send.counted = true, send.chainGarbage = 50;
                const auto pending = [&]() { return flight.waiting(1, 0, receive, send).pending; };
                pending();
                receive.trayFlying = 3;
                const int launched = pending();
                receive.trayFlying = 2;
                const int cancelled = pending();
                receive.trayFlying = 0, receive.trayPending = 2;
                check(launched == 50 && cancelled == 49 && pending() == 49, "自分宛ての飛行中の3個が2個に減っても残りを二重に数えない");
                receive.trayFlying = 5;
                const int next = pending();
                receive.trayFlying = 7;
                check(next == 49 && pending() == 49, "飛行中が増えたときは増加分だけを追加する");
                receive.trayFlying = 2, receive.trayPending += 7;
                check(pending() == 49, "前の7個が到着して別の2個が飛行中なら新たな送信として数える");
                send.trayFlying = 3;
                const int sentBack = pending();
                send.trayFlying = 2;
                const int offset = pending();
                send.trayFlying = 0, send.trayPending = 2;
                check(sentBack == 46 && offset == 46 && pending() == 46, "相手宛ての飛行中の3個が2個に減っても相殺した分を二重に数えない");
            }
            // (the account not read: the number shown and the chain's garbage in all, as before)
            own.trayKnown = false, his.chainGarbage = 0;
            in = exact.waiting(1, 9, own, his);
            check(in.confirmed == 9 && in.pending == 0, "without the game's account: the number shown");
            // (the account not read for a sample while the chain runs: what the chain has sent is not counted again)
            PrimaryObserver14 gap(Rules{}, primaryTiming14(false));
            PrimarySeat14 me2 = seatOf(1, 300, 4, false, none), his2 = seatOf(0, 300, 4, false, none);
            me2.trayKnown = his2.trayKnown = true, his2.counted = true, his2.chainGarbage = 50;
            gap.waiting(1, 0, me2, his2);
            me2.trayFlying = 6;
            const int sent = gap.waiting(1, 0, me2, his2).pending;
            me2.trayKnown = false;
            gap.waiting(1, 6, me2, his2);
            me2.trayKnown = true;
            in = gap.waiting(1, 0, me2, his2);
            check(sent == 50 && in.pending == 50, "the game's account not read for a sample while the other seat's chain runs: what it has sent is not counted again");
        }
    }
    // The other seat's chain: shown with the frames it still runs. (The board has the end of the chain in the frame the
    // pair is put; the wave that runs is told by the field's phase and by the pieces the game still counts.)
    {
        const std::array<Pair, 3> none{}, pairs{Pair{Cell(1), Cell(2)}, Pair{Cell(3), Cell(3)}, Pair{Cell(4), Cell(4)}};
        const std::array<int, 2> notice{0, 0};
        const Timing timing = primaryTiming14(false);
        GameField before;
        for (int y = 0; y < 3; ++y) before.f.set(0, y, Cell(2)), before.f.set(0, y + 3, Cell(1));
        // (the pair upright on the first column: four of colour 1 clear, the piece of colour 2 falls four rows on three
        //  of its colour, and those clear with nothing left to fall)
        const int first = timing.waveFrames(4), second = timing.waveFrames(0), land = timing.landFrames;
        for (int fits = 1; fits >= 0; --fits) {
            PrimaryObserver14 seen(Rules{}, timing);
            ai::Observation o;
            const auto at = [&](uint32_t tick, bool hands, uint32_t phase, int pieces, bool active) {
                PrimarySample14 s;
                s.generation = 1, s.serial = tick;
                s.seat[0] = seatOf(0, tick, hands ? 5 : 6, hands, pairs);
                s.seat[1] = seatOf(1, tick, 5, false, none);
                PrimarySeat14& his = *s.seat[0];
                his.field = hands || !fits ? before : GameField{};
                his.pose.phase = phase, his.pose.fieldActive = active;
                his.counted = true, his.pieces = pieces, his.chainGarbage = hands ? 0 : 10;
                return seen.observe(s, 1, notice, o);
            };
            bool ok = at(100, true, 0, 6, false);
            ok = at(101, false, 0, 8, true) && ok;
            if (!fits) {
                check(ok && o.player[0].phase == Phase::Land && o.player[0].wait >= 1 && o.player[0].chain.chains == 1,
                      "the other seat's chain: a board the model's chain does not end in - a chain all the same, over no sooner than the field is quiet");
                {
                    ai::Duel d(o);
                    const int64_t until = o.frame + 400;
                    while (d.state == RoundState::Playing && d.frame < until) d.run(until);
                    check(o.player[1].incoming.pending == 10 && d.player[1].incoming.pending == 0 && d.player[1].incoming.confirmed == 10,
                          "... and in the AI's reading its garbage is sure once it is over");
                }
                continue;
            }
            check(ok && o.player[0].phase == Phase::Land && o.player[0].wait == land + first + second && o.player[0].moves == 6,
                  "the other seat's chain: in the frame its pair is put, the frames of the landing and of both waves");
            ok = at(uint32_t(101 + land), false, 1, 8, true);
            check(ok && o.player[0].phase == Phase::Land && o.player[0].wait == first + second, "the other seat's chain: the first wave begins - the frames of both waves");
            ok = at(190, false, 2, 4, true);
            check(ok && o.player[0].phase == Phase::Land && o.player[0].wait == 101 + land + first + second - 190, "the other seat's chain: its pieces falling - the same end");
            ok = at(uint32_t(101 + land + first + 3), false, 1, 4, true);
            check(ok && o.player[0].phase == Phase::Land && o.player[0].wait == second, "the other seat's chain: the second wave seen to begin three frames late - its frames from there");
            ok = at(uint32_t(101 + land + first + 3 + second), false, 0, 0, false);
            check(ok && o.player[0].phase == Phase::Next, "the other seat's chain: over - the seat waits for its pair");
        }
        // (garbage in the 13th row over a piece that clears: gone with it in the game's board - the chain is still told)
        {
            // (the first column: nine pieces of two colours by turns, three of colour 1, garbage in the 13th row; the
            //  second: nine pieces the other way round. The pair upright on the second: its piece of colour 1 clears
            //  with the three, its other piece falls one row - and the garbage is gone from the game's board)
            GameField tall, end;
            for (int y = 0; y < 9; ++y) {
                tall.f.set(0, y, Cell(y % 2 ? 3 : 4)), tall.f.set(1, y, Cell(y % 2 ? 4 : 3));
                end.f.set(0, y, Cell(y % 2 ? 3 : 4)), end.f.set(1, y, Cell(y % 2 ? 4 : 3));
            }
            for (int y = 9; y < 12; ++y) tall.f.set(0, y, Cell(1));
            tall.f.set(0, 12, GARBAGE);
            end.f.set(1, 9, Cell(2));
            const std::array<Pair, 3> lying{Pair{Cell(1), Cell(2)}, Pair{Cell(3), Cell(3)}, Pair{Cell(4), Cell(4)}};
            PrimaryObserver14 seen(Rules{}, timing);
            ai::Observation o;
            bool ok = true;
            for (int step = 0; step < 2; ++step) {
                PrimarySample14 s;
                s.generation = 1, s.serial = uint32_t(100 + step);
                s.seat[0] = seatOf(0, uint32_t(100 + step), step ? 6 : 5, step == 0, lying);
                s.seat[1] = seatOf(1, uint32_t(100 + step), 5, false, none);
                PrimarySeat14& his = *s.seat[0];
                his.field = step ? end : tall;
                his.pose.fieldActive = step > 0, his.counted = true, his.pieces = 30, his.chainGarbage = step ? 4 : 0;
                ok = seen.observe(s, 1, notice, o) && ok;
            }
            check(ok && o.player[0].phase == Phase::Land && o.player[0].wait > timing.landFrames, "the other seat's chain: garbage of the 13th row gone with the piece under it - the chain is told all the same");
        }
        // (a wave not seen to begin: told by the pieces the game still counts)
        PrimaryObserver14 seen(Rules{}, timing);
        ai::Observation o;
        bool ok = true;
        for (int step = 0; step < 3; ++step) {
            PrimarySample14 s;
            const uint32_t tick = step == 0 ? 100 : step == 1 ? 101 : 240;
            s.generation = 1, s.serial = tick;
            s.seat[0] = seatOf(0, tick, step ? 6 : 5, step == 0, pairs);
            s.seat[1] = seatOf(1, tick, 5, false, none);
            PrimarySeat14& his = *s.seat[0];
            his.field = step ? GameField{} : before;
            his.pose.phase = step == 2 ? 2 : 0, his.pose.fieldActive = step > 0;
            his.counted = true, his.pieces = step == 0 ? 6 : step == 1 ? 8 : 4, his.chainGarbage = step ? 10 : 0;
            ok = seen.observe(s, 1, notice, o) && ok;
        }
        check(ok && o.player[0].phase == Phase::Land && o.player[0].wait > 0 && o.player[0].wait <= first + second,
              "the other seat's chain: a wave not seen to begin, its pieces gone - no later wave than the count allows, and still running");
    }
    // The frame counts the AI reckons with on the real game with the cancels (what is left of the other model's rules):
    // a round played with them is the one the AI plays when shown the round itself.
    {
        bool same = true, clean = true;
        int locks = 0;
        long long faster = 0;
        for (uint64_t seed = 91; seed <= 94; ++seed) {
            Setup u;
            u.seed = seed, u.timing = primaryTiming14(true), u.fastest = true, u.limit = 2500;
            Setup d = u;
            d.direct = true;
            const Played a = playRound(u), b = playRound(d);
            if (seed == 91) print("the real game's cancels", u, a);
            same = same && sameRound(a, b);
            clean = clean && !a.misses && !a.firstMisses && !a.untargeted && !a.idle && !a.recovered && !a.repaired;
            locks += a.locks, faster += a.faster;
        }
        check(locks > 60 && same && clean, "the cancels kept for the real game, buttons at once: the round the AI plays when shown the round itself, every pair where it chose");
        check(faster > 0, "... and ways shorter than the straight one are found with them");
    }
    // The cancels make the turns shorter: the same placements played with the ways over soonest and with the straight
    // ways (the random agent puts its pairs alike while the fields are alike), the frame the pair after the last of them
    // appears in: sooner with the cancels over the rounds. (Pair by pair the two are not to be compared: garbage of the other
    // side falls after another pair in a round that is further on.)
    {
        long long with = 0, without = 0;
        int pairs = 0;
        for (uint64_t seed = 91; seed <= 98; ++seed) {
            Setup u;
            u.seed = seed, u.timing = primaryTiming14(true), u.fastest = true, u.limit = 2500;
            Setup v = u;
            v.fastest = false;
            const Played a = playRound(u), b = playRound(v);
            size_t n = 0;
            while (n < a.appeared.size() && n < b.appeared.size() && a.placements[n] == b.placements[n]) ++n;
            if (!n) continue;
            with += a.appeared[n - 1], without += b.appeared[n - 1], pairs += int(n);
        }
        std::printf("  the cancels: %d pairs put alike with and without, %.2f and %.2f frames a pair\n", pairs, pairs ? double(with) / pairs : 0.0,
                    pairs ? double(without) / pairs : 0.0);
        check(pairs > 60, "the cancels beside the straight ways: too few pairs put alike to compare");
        check(with < without, "the cancels beside the straight ways: the pairs are not put sooner with them");
    }
    // A round joined on its way with the other seat pairs ahead: the pairs between the two windows have not been seen,
    // and the AI is not shown pairs in their place.
    {
        const auto P = [](int a, int b) { return Pair{Cell(a), Cell(b)}; };
        const std::array<Pair, 3> mine1{P(2, 2), P(2, 3), P(3, 1)}, mine2{P(2, 3), P(3, 1), P(1, 4)}, mine3{P(3, 1), P(1, 4), P(2, 1)}, theirs{P(4, 4), P(5, 5), P(1, 2)};
        const std::array<int, 2> notice{0, 0};
        PrimaryObserver14 seen(Rules{}, primaryTiming14(false));
        ai::Observation o;
        const auto at = [&](uint32_t tick, uint32_t put, bool hands, const std::array<Pair, 3>& pairs) {
            PrimarySample14 s;
            s.generation = 1, s.serial = tick;
            s.seat[1] = seatOf(1, tick, put, hands, pairs), s.seat[0] = seatOf(0, tick, 6, true, theirs);
            return seen.observe(s, 1, notice, o);
        };
        bool ok = at(400, 1, true, mine1);
        check(ok && o.mine().count() == 4 && o.mine().pair(3).axis == Cell(3) && o.player[0].nextPair == 6, "joined with the other seat five pairs ahead: the pairs of the own window, none beyond");
        ok = at(430, 2, false, mine1);
        check(ok && o.mine().count() == 4 && o.self().phase == Phase::Next && o.self().nextPair == 2, "... the same with the own pair put");
        ok = at(460, 2, true, mine2);
        check(ok && o.mine().count() == 5 && o.mine().pair(4).child == Cell(4), "... one more with each pair the own window shows");
        ok = at(490, 3, false, mine2) && at(520, 3, true, mine3);
        check(ok && o.mine().count() == 9 && o.mine().pair(5).axis == Cell(2) && o.mine().pair(6).axis == Cell(4) && o.mine().pair(8).child == Cell(2),
              "... and all of them once the own window has come to those the other seat's showed");
    }
    // The colours in play are those the round shows: four of the game's five, another four in the next round. The AI
    // is told them (Rules::palette, its first four) - its searches make up pairs of those and try them on a field.
    {
        const auto P = [](int a, int b) { return Pair{Cell(a), Cell(b)}; };
        const std::array<int, 2> notice{0, 0};
        PrimaryObserver14 seen(Rules{}, primaryTiming14(false));
        ai::Observation o;
        const auto first4 = [&](int a, int b, int c, int d) {
            return o.rules.palette[0] == Cell(a) && o.rules.palette[1] == Cell(b) && o.rules.palette[2] == Cell(c) && o.rules.palette[3] == Cell(d);
        };
        const auto at = [&](uint32_t tick, const std::array<Pair, 3>& mine, const std::array<Pair, 3>& theirs, int otherPiece) {
            PrimarySample14 s;
            s.generation = 1, s.serial = tick;
            s.seat[1] = seatOf(1, tick, 1, true, mine), s.seat[0] = seatOf(0, tick, 1, true, theirs);
            if (otherPiece) s.seat[0]->field.f.set(0, 0, Cell(otherPiece)), s.seat[0]->field.f.set(1, 0, GARBAGE);
            return seen.observe(s, 1, notice, o);
        };
        // (the windows show the colours 1, 2, 3 and 5: 5 takes the place of 4, which is not in play)
        const std::array<Pair, 3> rgbp{P(1, 2), P(2, 3), P(3, 5)}, rg{P(1, 1), P(1, 2), P(2, 2)}, bp{P(3, 5), P(5, 3), P(3, 3)}, plain{P(1, 2), P(3, 4), P(4, 1)};
        bool ok = at(400, rgbp, rgbp, 0);
        check(ok && first4(1, 2, 3, 5) && o.rules.palette[4] == Cell(4), "colours 1, 2, 3 and 5 shown by the windows: those four are the colours in play");
        // (a new round, the counter back: what the round before showed is forgotten; a colour only a field holds counts)
        ok = at(300, rg, rg, 5);
        check(ok && first4(1, 2, 5, 4) && o.rules.palette[4] == Cell(3), "a new round with 1 and 2 in the windows and a piece of 5 on the other field: 5 for the first colour not shown, garbage is no colour");
        // (the same whichever colours were shown first: 3 and 5, then 1 - colour 2 is the one not in play)
        ok = at(200, bp, bp, 0);
        check(ok && first4(5, 2, 3, 4), "3 and 5 shown: 5 for the first colour not shown yet");
        ok = at(230, bp, rg, 0) && at(260, bp, plain, 0);
        check(ok && o.rules.palette[0] == Cell(1) && o.rules.palette[1] == Cell(2) && o.rules.palette[2] == Cell(3) && o.rules.palette[3] == Cell(4) && o.rules.palette[4] == Cell(5),
              "all five codes shown in one round (never so in the game): the rules as they were given");
        // (the colours of this program's rounds, 1 to 4: nothing changes)
        ok = at(100, plain, plain, 0);
        check(ok && first4(1, 2, 3, 4) && o.rules.palette[4] == Cell(5), "colours 1 to 4 shown: the rules as they were given");
    }
    // What the observer makes of a seat, frame by frame.
    {
        bool waited = true, appeared = true, counted = true, inHand = true;
        int shownFrames = 0, controls = 0, known = 0;
        for (uint64_t seed = 41; seed <= 43; ++seed) {
        Round round;
        round.start(Rules{}, seed, 2);
        PrimaryObserver14 seen(round.rules, round.timing);
        ai::Observation o;
        auto first = ai::makeAgent("random"), second = ai::makeAgent("random");
        first->startRound(0, seed), second->startRound(1, seed + 1);
        for (int f = 0; f < 3000 && round.state == RoundState::Playing; ++f) {
            const uint8_t buttons[2]{first->frame(ai::observe(round, 0)), second->frame(ai::observe(round, 1))};
            PlayerEvents events[2];
            round.step(buttons, events);
            const std::array<int, 2> notice{0, 0};
            const bool ok = seen.observe(madeUpSample14(round), 1, notice, o);
            const Player& real = round.player[1];
            if (!ok) continue;
            counted = counted && o.frame == round.frame;
            if (real.phase == Phase::Control) {
                ++controls;
                inHand = inHand && o.self().phase == Phase::Control && o.self().nextPair == real.nextPair && o.self().pair.x == real.pair.x && o.self().pair.y == real.pair.y &&
                         o.self().pair.upper == real.pair.upper && o.self().pair.dir == real.pair.dir && o.self().colors.axis == real.colors.axis && o.self().field == real.field;
            } else {
                waited = waited && o.self().phase == Phase::Next && o.self().nextPair == real.nextPair && o.self().wait >= 1;
                const bool fits = real.phase == Phase::Next && real.wait <= 11 ? o.self().wait == real.wait || real.field.dead() : o.self().wait >= 11;
                known += real.phase == Phase::Next && real.wait <= 11;
                if (!fits && appeared) std::printf("the observer: frame %lld, the round has phase %d wait %d, shown wait %d\n", (long long)round.frame, int(real.phase), real.wait, o.self().wait);
                appeared = appeared && fits;
                shownFrames += real.phase == Phase::Next && real.wait == 1 && seen.shownOnly(1);
            }
        }
        }
        std::printf("the observer: %d frames with a pair in the hands, %d known frames before a pair, %d first frames\n", controls, known, shownFrames);
        check(controls > 100 && inHand, "the observer: a pair in the hands is shown where it is, with its colours and its field");
        check(waited, "the observer: a seat without a pair is shown waiting for the pair that comes");
        check(known > 100 && appeared, "the observer: from the last busy frame on, the frames to the pair are those of the round; before, more");
        check(shownFrames > 8, "the observer: the first frame of a pair in the hands is shown as the frame before it appears");
        check(counted, "the observer: the frame is that of the round");
    }
    {
        // The all-clear bonus that waits for a seat's next chain, told from the boards.
        PrimaryObserver14 seen(Rules{}, primaryTiming14(true));
        ai::Observation o;
        const std::array<Pair, 3> pairs{{{Cell(1), Cell(1)}, {Cell(1), Cell(1)}, {Cell(1), Cell(1)}}};
        const std::array<int, 2> notice{0, 0};
        uint32_t tick = 500, put = 3;
        bool active = false;
        const auto feed = [&](bool operating, const std::vector<std::array<int, 3>>& cells) {
            PrimarySample14 sample;
            sample.generation = 1;
            sample.seat[0] = seatOf(0, tick, put, operating, pairs);
            sample.seat[1] = seatOf(1, tick, 3, true, pairs);
            for (const auto& c : cells) sample.seat[0]->field.f.set(c[0], c[1], Cell(c[2]));
            sample.seat[0]->counted = true, sample.seat[0]->pieces = int(cells.size()), sample.seat[0]->pose.fieldActive = active;
            ++tick;
            return seen.observe(sample, 1, notice, o);
        };
        const std::vector<std::array<int, 3>> two{{0, 0, 1}, {0, 1, 1}}, four{{0, 0, 1}, {0, 1, 1}, {5, 0, 2}, {5, 1, 2}}, other{{5, 0, 2}, {5, 1, 2}};
        feed(true, two);
        bool ok = feed(true, two);
        check(ok && !o.player[0].scoring.allClear && !o.player[1].scoring.allClear, "the all-clear bonus: none at first");
        put = 4, ok = feed(false, {});
        check(ok && o.player[0].scoring.allClear && !o.player[1].scoring.allClear, "the all-clear bonus: the pair's chain leaves the field empty - it waits for that seat");
        ok = feed(true, {});
        check(ok && o.player[0].scoring.allClear, "the all-clear bonus: it waits while the next pair is in the hands");
        put = 5, ok = feed(false, two);
        check(ok && o.player[0].scoring.allClear, "the all-clear bonus: a pair put without a chain leaves it");
        feed(true, four);
        put = 6, ok = feed(false, other);
        check(ok && !o.player[0].scoring.allClear, "the all-clear bonus: a chain that does not empty the field takes it");
        check(ok && o.player[1].incoming.pending == 30 && o.player[1].incoming.confirmed == 0, "the all-clear bonus: its garbage waits above the other seat while that chain runs, though the game counts none for the chain");
        active = true, ok = feed(false, other);
        check(ok && o.player[1].incoming.pending == 30, "the all-clear bonus: the same while the field is busy");
        active = false, ok = feed(false, other);
        check(ok && o.player[1].incoming.total() == 0, "the all-clear bonus: the chain over - what waits is what the game has");
        feed(true, other);
        put = 7, ok = feed(false, {});
        check(ok && o.player[0].scoring.allClear && o.player[1].incoming.total() == 0, "the all-clear bonus: and the next chain that empties the field gives it again, with no garbage of a bonus");
        seen.reset();
        ok = feed(true, two) && feed(true, two);
        check(ok && !o.player[0].scoring.allClear, "the all-clear bonus: none in a new round");
    }
    {
        // The all-clear bonus by the game's own mark (PrimarySeat14::bonusWaits), whatever the boards look like; and the
        // pairs beyond the window (PrimarySeat14::listed) shown to the AI when they are read.
        PrimaryObserver14 seen(Rules{}, primaryTiming14(true));
        ai::Observation o;
        const std::array<Pair, 3> pairs{{{Cell(1), Cell(1)}, {Cell(1), Cell(1)}, {Cell(1), Cell(1)}}};
        const std::array<int, 2> notice{0, 0};
        uint32_t tick = 500, put = 3;
        bool waits = false, listed = false;
        const std::vector<std::array<int, 3>> two{{0, 0, 1}, {0, 1, 1}};
        const auto feed = [&](bool operating) {
            PrimarySample14 sample;
            sample.generation = 1;
            sample.seat[0] = seatOf(0, tick, put, operating, pairs);
            sample.seat[1] = seatOf(1, tick, 3, true, pairs);
            for (const auto& c : two) sample.seat[0]->field.f.set(c[0], c[1], Cell(c[2]));
            sample.seat[0]->counted = true, sample.seat[0]->pieces = 2, sample.seat[0]->bonusKnown = true, sample.seat[0]->bonusWaits = waits;
            sample.seat[1]->listedKnown = listed;
            for (size_t k = 0; k < LISTED14; ++k) sample.seat[1]->listed[k] = {Cell(2 + int(k % 3)), Cell(3)};
            ++tick;
            return seen.observe(sample, 1, notice, o);
        };
        feed(true);
        bool ok = feed(true);
        check(ok && !o.player[0].scoring.allClear, "the mark of the all-clear bonus: not set, none");
        const int shown = o.tsumo.count();
        check(ok && shown == 6, "the pairs beyond the window: not read, the AI is shown the window's pairs only");
        waits = true, put = 4, ok = feed(false);
        check(ok && o.player[0].scoring.allClear, "the mark of the all-clear bonus: set, the bonus waits - though the board is not empty");
        ok = feed(true);
        check(ok && o.player[0].scoring.allClear, "... and waits with the next pair in the hands");
        waits = false, put = 5, ok = feed(false);
        check(ok && !o.player[0].scoring.allClear && o.player[1].incoming.total() == 30, "the mark gone as a pair is put: the bonus is taken, its garbage waits above the other seat");
        listed = true, ok = feed(false);
        check(ok && o.tsumo.count() >= 3 + 3 + int(LISTED14) && o.tsumo.known(6) && o.tsumo.pair(6).axis == Cell(2) && o.tsumo.pair(7).axis == Cell(3) && o.tsumo.pair(13).child == Cell(3),
              "the pairs beyond the window: read, the AI is shown them after the window's three");
        listed = false, ok = feed(false);
        check(ok && o.tsumo.count() >= 3 + 3 + int(LISTED14) && o.tsumo.pair(13).child == Cell(3), "the pairs beyond the window no longer read: those read before are still shown");
    }
    {
        // Garbage on its way down: counted by the game in the field and above it no more, not in the board yet - shown
        // fallen from then on; not while a chain runs (the counts are then not of the board's moment).
        PrimaryObserver14 seen(Rules{}, primaryTiming14(true));
        ai::Observation o;
        const std::array<Pair, 3> pairs{{{Cell(1), Cell(1)}, {Cell(1), Cell(1)}, {Cell(1), Cell(1)}}};
        const std::array<int, 2> notice{0, 0};
        uint32_t tick = 500;
        const auto feed = [&](bool operating, bool active, uint32_t phase, int garbage, int chainGarbage) {
            PrimarySample14 sample;
            sample.generation = 1;
            sample.seat[0] = seatOf(0, tick, 4, operating, pairs);
            sample.seat[1] = seatOf(1, tick, 3, true, pairs);
            sample.seat[0]->field.f.set(0, 0, Cell(1)), sample.seat[0]->field.f.set(0, 1, Cell(1));
            sample.seat[0]->counted = true, sample.seat[0]->pieces = 2 + garbage, sample.seat[0]->garbagePieces = garbage, sample.seat[0]->chainGarbage = chainGarbage;
            sample.seat[0]->pose.fieldActive = active, sample.seat[0]->pose.phase = phase;
            ++tick;
            return seen.observe(sample, 1, notice, o);
        };
        feed(true, false, 0, 0, 0);
        bool ok = feed(true, false, 0, 0, 0);
        check(ok && o.player[0].field.f.get(5, 0) == EMPTY, "garbage on its way down: none yet, the field is the board");
        ok = feed(false, true, 10, 6, 0);
        check(ok && o.player[0].field.f.get(5, 0) == GARBAGE && o.player[0].field.f.get(0, 2) == GARBAGE && o.player[0].field.f.get(0, 0) == Cell(1),
              "garbage on its way down: shown fallen from the frame the game counts it in the field");
        ok = feed(false, false, 0, 6, 0);
        check(ok && o.player[0].field.f.get(5, 0) == GARBAGE && o.player[0].field.f.get(0, 2) == GARBAGE, "... and the same field when it has come down");
        ok = feed(false, true, 1, 6, 0);
        check(ok && o.player[0].field.f.get(5, 0) == EMPTY, "the field clearing, no pair in the hands: nothing is added to the board");
        ok = feed(false, true, 0, 6, 12);
        check(ok && o.player[0].field.f.get(5, 0) == EMPTY, "... nor in the first frames of a chain, told by the garbage it makes");
    }
    {
        // The AI on the first seat: the round it plays through the memory is the round it plays when shown the round itself.
        bool same = true;
        int locks = 0;
        for (uint64_t seed = 1; seed <= 4; ++seed) {
            Setup u;
            u.seed = seed, u.me = 0;
            Setup d = u;
            d.direct = true;
            const Played x = playRound(u), y = playRound(d);
            same = same && sameRound(x, y) && !x.misses && !x.untargeted && !x.idle;
            locks += x.locks;
        }
        check(locks > 60 && same, "the AI on the first seat, buttons at once: the round the AI plays when shown the round itself");
    }
    {
        PrimaryObserver14 seen(Rules{}, primaryTiming14(false));
        ai::Observation o;
        const std::array<Pair, 3> pairs{{{Cell(1), Cell(2)}, {Cell(3), Cell(4)}, {Cell(2), Cell(3)}}};
        PrimarySample14 sample;
        sample.generation = 1;
        sample.seat[0] = seatOf(0, 500, 4, true, pairs);
        sample.seat[1] = seatOf(1, 500, 4, true, pairs);
        for (auto& seat : sample.seat) seat->garbageRandom = uint32_t(0xc5d95c6b), seat->garbageCursor = uint8_t(0);
        const std::array<uint8_t, W> expected{4, 5, 2, 1, 0, 3};
        const std::array<int, 2> notice{};
        check(seen.observe(sample, 1, notice, o) && o.nextGarbageOrder[0] == expected && o.nextGarbageOrder[1] == expected,
              "the memory observation passes the next drop for each current seat");
        for (int i = 0; i < 2; ++i) o.player[i].phase = Phase::Decide, o.player[i].field = {}, o.player[i].incoming = {};
        o.player[1].incoming.confirmed = 2;
        ai::Duel d(o);
        d.run(o.frame + 1);
        check(d.player[1].field.f.get(4, 0) == GARBAGE && d.player[1].field.f.get(5, 0) == GARBAGE &&
                  !d.nextGarbageOrder[1] && d.nextGarbageOrder[0] == expected,
              "the first positive drop uses its own order once and an empty drop keeps the other seat's order");
        auto plain = o;
        plain.nextGarbageOrder = {};
        ai::Duel fallback(plain);
        fallback.run(o.frame + 1);
        d.player[1].field = fallback.player[1].field = {};
        for (auto* simulation : {&d, &fallback}) {
            auto& seat = simulation->player[1];
            seat.phase = ai::DuelPhase::Settle, seat.at = simulation->frame, seat.incoming.confirmed = 5;
            simulation->run(simulation->frame + 1);
        }
        check(d.player[1].field == fallback.player[1].field, "later drops use the normal stream of the simulated duel");
        ++sample.seat[1]->pose.count108;
        sample.seat[1]->pose.phase = 10;
        check(seen.observe(sample, 1, notice, o) && !o.nextGarbageOrder[0] && !o.nextGarbageOrder[1],
              "an older seat and a fall in progress give no order to the forecast");
        sample.seat[0].reset(), sample.seat[1]->pose.phase = 0;
        ++sample.seat[1]->pose.count108;
        check(seen.observe(sample, 1, notice, o) && !o.nextGarbageOrder[0] && o.nextGarbageOrder[1],
              "a missing seat cannot reuse its old order");
        ai::observe(Round{}, 0, o);
        check(!o.nextGarbageOrder[0] && !o.nextGarbageOrder[1], "an internal round clears any order from memory");
    }
    {
        // The own seat with its field busy and no pair in the hands: shown as waiting for its pair, with the frames its
        // landing still takes - and the AI's reading lets the garbage sure above it fall before its next pair.
        PrimaryObserver14 seen(Rules{}, primaryTiming14(true));
        ai::Observation o;
        const std::array<Pair, 3> pairs{{{Cell(1), Cell(2)}, {Cell(3), Cell(4)}, {Cell(2), Cell(3)}}};
        const std::array<int, 2> notice{0, 0};
        uint32_t tick = 500;
        const auto feed = [&](bool operating, bool active, int chainGarbage) {
            PrimarySample14 sample;
            sample.generation = 1;
            sample.seat[0] = seatOf(0, tick, 4, true, pairs);
            sample.seat[1] = seatOf(1, tick, 4, operating, pairs);
            sample.seat[1]->pose.fieldActive = active, sample.seat[1]->counted = true, sample.seat[1]->chainGarbage = chainGarbage;
            ++tick;
            return seen.observe(sample, 1, notice, o);
        };
        feed(true, false, 0);
        bool ok = feed(true, false, 0);
        check(ok && o.busyLeft[0] == 0 && o.busyLeft[1] == 0, "the own seat with a pair in its hands: not busy");
        ok = feed(false, true, 0);
        check(ok && o.player[1].phase == Phase::Next && o.busyLeft[1] >= 1 && !o.busyChain[1] && o.busyLeft[0] == 0, "the own seat's pair landing: its field is busy for the frames told, with no chain");
        ai::Observation with = o;
        with.player[1].incoming.confirmed = 6;
        ai::Duel fallen(with);
        fallen.run(with.frame + 600);
        with.busyLeft[1] = 0;
        ai::Duel late(with);
        late.run(with.frame + 600);
        check(fallen.player[1].field.f.get(5, 0) == GARBAGE && late.player[1].field.f.get(5, 0) != GARBAGE,
              "... and the garbage sure above it has fallen when its next pair appears (without the frames told, it has not)");
        ok = feed(false, true, 12);
        check(ok && o.busyLeft[1] >= 1 && o.busyChain[1], "the own seat's chain: busy with a chain");
    }
    {
        // The other seat not read: shown as it was last read for up to 120 frames, then nothing is shown.
        const std::array<Pair, 3> pairs{{{Cell(1), Cell(2)}, {Cell(3), Cell(4)}, {Cell(2), Cell(3)}}};
        PrimaryObserver14 seen(Rules{}, primaryTiming14(false));
        ai::Observation o;
        bool ok = true;
        for (uint32_t tick = 100; tick <= 221; ++tick) {
            PrimarySample14 s;
            s.generation = 1, s.serial = tick;
            s.seat[1] = seatOf(1, tick, 2, true, pairs);
            if (tick == 100) s.seat[0] = seatOf(0, tick, 2, true, pairs);
            ok = ok && seen.observe(s, 1, {0, 0}, o) == (tick <= 220);
        }
        check(ok, "the other seat not read: shown as last read for 120 frames, not after");
    }
    {
        // The mark of the all-clear bonus not read in the frame the pair of the next chain is put: told from the boards.
        PrimaryObserver14 seen(Rules{}, primaryTiming14(true));
        ai::Observation o;
        const std::array<Pair, 3> pairs{{{Cell(1), Cell(1)}, {Cell(1), Cell(1)}, {Cell(1), Cell(1)}}};
        const std::array<int, 2> notice{0, 0};
        uint32_t tick = 500, put = 3;
        bool waits = false, readable = true;
        const auto feed = [&](bool operating) {
            PrimarySample14 sample;
            sample.generation = 1;
            sample.seat[0] = seatOf(0, tick, put, operating, pairs);
            sample.seat[1] = seatOf(1, tick, 3, true, pairs);
            sample.seat[0]->field.f.set(0, 0, Cell(1)), sample.seat[0]->field.f.set(0, 1, Cell(1));
            sample.seat[0]->counted = true, sample.seat[0]->pieces = 2, sample.seat[0]->bonusKnown = readable, sample.seat[0]->bonusWaits = readable && waits;
            ++tick;
            return seen.observe(sample, 1, notice, o);
        };
        feed(true), feed(true);
        waits = true, put = 4;
        bool ok = feed(false) && feed(true) && o.player[0].scoring.allClear;
        waits = false, readable = false, put = 5;
        ok = feed(false) && ok;
        check(ok && !o.player[0].scoring.allClear && o.player[1].incoming.total() == 30, "the mark of the all-clear bonus not read as the pair of the next chain is put: the bonus taken all the same");
        readable = true, ok = feed(false);
        check(ok && !o.player[0].scoring.allClear && o.player[1].incoming.total() == 30, "... and the mark read again: the bonus still taken once");
    }
    {
        // A sample with the numbers of a seat out of place: nothing is shown for it, and the round goes on as it was; a
        // counter or a count of pairs that goes back begins a new round.
        PrimaryObserver14 seen(Rules{}, primaryTiming14(true));
        ai::Observation o;
        const std::array<Pair, 3> pairs{{{Cell(1), Cell(2)}, {Cell(3), Cell(4)}, {Cell(2), Cell(3)}}};
        const std::array<int, 2> notice{0, 0};
        const auto feed = [&](uint32_t tick, uint32_t put, bool out) {
            PrimarySample14 sample;
            sample.generation = 1;
            sample.seat[0] = seatOf(0, tick, put, true, pairs);
            sample.seat[1] = seatOf(1, tick, put, true, pairs);
            if (out) sample.seat[0]->pose.playerState = 5;
            return seen.observe(sample, 1, notice, o);
        };
        bool ok = feed(500, 3, false) && feed(501, 3, false);
        const uint64_t was = seen.round();
        const int known = o.tsumo.count();
        const int64_t frame = o.frame;
        ok = ok && !feed(502, 3, true) && feed(503, 3, false);
        check(ok && was >= 1 && seen.round() == was && o.tsumo.count() == known && o.frame == frame + 2,
              "a sample out of place: nothing shown for it, the round and its pairs kept");
        ok = feed(0, 0, false);
        check(ok && seen.round() == was + 1, "the counter gone back: a new round");
        ok = feed(20, 5, false) && feed(21, 2, false);
        check(ok && seen.round() == was + 2, "the count of pairs put gone back: a new round");
    }
    {
        struct Press : ai::Agent {
            int calls = 0;
            void startRound(int, uint64_t) override {}
            uint8_t frame(const ai::Observation&) override { ++calls; return BTN_DOWN; }
        } agent;
        const std::array<Pair, 3> pairs{{{Cell(1), Cell(2)}, {Cell(3), Cell(4)}, {Cell(2), Cell(3)}}};
        for (int me : {0, 1}) {
            PrimaryPlayer14 player(agent, me, 0);
            PrimarySample14 sample;
            sample.generation = 1;
            const auto feed = [&](uint32_t tick, int cpu) {
                for (int seat = 0; seat < 2; ++seat) {
                    sample.seat[seat] = seatOf(seat, tick, 3, true, pairs);
                    sample.seat[seat]->pose.cpuControlled = seat == cpu;
                }
                return player.frame(sample, {0, 0});
            };
            check(feed(500, 1 - me) == BTN_DOWN, "the configured human seat can play against a CPU");
            const int before = agent.calls;
            check(feed(501, me) == 0 && feed(502, me) == 0 && agent.calls == before, "a CPU in the configured seat releases held input and receives no decisions");
            check(feed(503, 1 - me) == BTN_DOWN && agent.calls > before, "input resumes only when the configured seat is human again");
        }
    }
    {
        struct Down : ai::Agent {
            int choices = 0;
            void startRound(int, uint64_t) override {}
            uint8_t frame(const ai::Observation&) override { ++choices; return BTN_DOWN; }
        } agent;
        const std::array<Pair, 3> pairs{{{Cell(1), Cell(2)}, {Cell(3), Cell(4)}, {Cell(2), Cell(3)}}};
        for (int loser : {0, 1}) {
            PrimaryPlayer14 player(agent, 1, 0);
            PrimarySample14 s;
            s.generation = 1;
            const auto sample = [&](uint32_t tick, uint32_t pairsPut) {
                for (int seat = 0; seat < 2; ++seat) s.seat[seat] = seatOf(seat, tick, pairsPut, true, pairs);
            };
            sample(2607, 35);
            check(player.frame(s, {0, 0}) == BTN_DOWN, "対戦中の下入力を送る");
            sample(2608, 35);
            s.seat[loser]->pose.fieldState = 0;
            const int before = agent.choices;
            check(player.frame(s, {0, 0}) == BTN_DOWN && agent.choices == before, "終了が確認できない一時的な読み抜けでは押したままにする");
            sample(2609, 35);
            s.seat[loser]->pose.fieldState = 9;
            s.seat[loser]->pose.operating = false;
            check(player.frame(s, {0, 0}) == 0 && agent.choices == before, "どちらの席が敗北しても、終了したフレームで下を離す");
            sample(2610, 35);
            s.seat[loser].reset();
            check(player.frame(s, {0, 0}) == 0 && agent.choices == before, "結果表示で席を読み抜けても新たな配置を選ばない");
            sample(2611, 35);
            check(player.frame(s, {0, 0}) == 0 && agent.choices == before, "同じ回戦の後続サンプルで操作を再開しない");
            sample(100, 1);
            check(player.frame(s, {0, 0}) == BTN_DOWN && agent.choices > before, "新しい回戦で操作を再開する");
        }
    }
    {
        // The player: one sample out of place, or many, does not begin the agent's round anew; a counter that goes back does.
        struct Counting : ai::Agent {
            int begun = 0;
            void startRound(int, uint64_t) override { ++begun; }
            uint8_t frame(const ai::Observation&) override { return 0; }
        } agent;
        PrimaryPlayer14 player(agent, 1, 0);
        player.observer.timing = primaryTiming14(true);
        const std::array<Pair, 3> pairs{{{Cell(1), Cell(2)}, {Cell(3), Cell(4)}, {Cell(2), Cell(3)}}};
        const auto feed = [&](uint32_t tick, uint32_t put, bool out) {
            PrimarySample14 sample;
            sample.generation = 1;
            sample.seat[0] = seatOf(0, tick, put, true, pairs);
            sample.seat[1] = seatOf(1, tick, put, true, pairs);
            if (out) sample.seat[1]->pose.fieldState = 0;
            return player.frame(sample, {0, 0});
        };
        feed(500, 3, false);
        const uint8_t before = feed(501, 3, false), during = feed(502, 3, true);
        feed(503, 3, false);
        check(agent.begun == 1 && player.rounds == 1 && during == before, "the player: a sample out of place - the buttons stay, the round is not begun anew");
        for (uint32_t t = 504; t < 520; ++t) feed(t, 3, true);
        feed(520, 3, false);
        check(agent.begun == 1 && player.rounds == 1, "the player: many samples out of place - still the same round");
        feed(0, 0, false);
        check(agent.begun == 2 && player.rounds == 2, "the player: the counter gone back - the next round is begun");
    }
    {
        // The other seat's chain the model does not play: over no sooner than a wave after the field began to clear.
        const Timing timing = primaryTiming14(true);
        const int wave = timing.waveFrames(1);
        PrimaryObserver14 seen(Rules{}, timing);
        ai::Observation o;
        const std::array<Pair, 3> pairs{{{Cell(1), Cell(2)}, {Cell(3), Cell(4)}, {Cell(2), Cell(3)}}};
        const std::array<int, 2> notice{0, 0};
        uint32_t tick = 500;
        const auto feed = [&](bool operating, bool active, uint32_t phase) {
            PrimarySample14 sample;
            sample.generation = 1;
            sample.seat[0] = seatOf(0, tick, operating ? 4 : 5, operating, pairs);
            sample.seat[1] = seatOf(1, tick, 4, true, pairs);
            sample.seat[0]->pose.fieldActive = active, sample.seat[0]->pose.phase = phase;
            ++tick;
            return seen.observe(sample, 1, notice, o);
        };
        bool ok = feed(true, false, 0) && feed(true, false, 0) && feed(false, true, 0);
        const uint32_t clearedAt = tick;
        bool good = true;
        for (int k = 0; k < wave; ++k) {
            const uint32_t at = tick;
            ok = feed(false, true, k < wave / 2 ? 1 : 2) && ok;
            good = good && o.player[0].phase == Phase::Land && o.player[0].wait >= int(clearedAt + uint32_t(wave) - at);
        }
        check(ok && good, "the other seat's chain not played by the model: a wave's frames at least from the frame the field began to clear");
    }
    {
        // Without the game's account: of what the chain makes, the garbage above its own field is taken first, and what
        // the seat's own chain cancels of what has arrived is gone.
        const std::array<Pair, 3> none{};
        PrimaryObserver14 seen(Rules{}, primaryTiming14(false));
        PrimarySeat14 his = seatOf(0, 300, 4, false, none);
        his.counted = true, his.chainGarbage = 20, his.noticeKnown = true, his.notice = 8;
        juraic::Incoming in = seen.waiting(1, 3, his);
        check(in.confirmed == 3 && in.pending == 12, "without the game's account: a chain of 20 with 8 above its own field - 12 to come");
        his.notice = 0;
        in = seen.waiting(1, 3, his);
        check(in.confirmed == 3 && in.pending == 12, "... the 8 taken - still 12 to come");
        in = seen.waiting(1, 9, his);
        check(in.confirmed == 3 && in.pending == 12, "... 6 arrived - 6 there, 6 to come");
        in = seen.waiting(1, 5, his);
        check(in.confirmed == 3 && in.pending == 2 + 6, "... the seat's own chain cancels 4 of them - 2 there, still 6 to come");
    }
    {
        // A pair that lay long before it locked: the next pair no sooner than Timing::lockWait and nextFrames after the lock.
        const Timing timing = primaryTiming14(true);
        PrimaryObserver14 seen(Rules{}, timing);
        ai::Observation o;
        const std::array<Pair, 3> pairs{{{Cell(1), Cell(2)}, {Cell(3), Cell(4)}, {Cell(2), Cell(3)}}};
        const std::array<int, 2> notice{0, 0};
        uint32_t tick = 500;
        const auto feed = [&](bool operating) {
            PrimarySample14 sample;
            sample.generation = 1;
            sample.seat[0] = seatOf(0, tick, 4, true, pairs);
            sample.seat[1] = seatOf(1, tick, operating ? 4 : 5, operating, pairs);
            if (operating) sample.seat[1]->pose.y = 0, sample.seat[1]->pose.upper = false;
            ++tick;
            return seen.observe(sample, 1, notice, o);
        };
        bool ok = true;
        for (int k = 0; k < 30; ++k) ok = feed(true) && ok;
        ok = feed(false) && ok;
        check(ok && o.self().phase == Phase::Next && o.self().wait == timing.lockWait + timing.nextFrames,
              "a pair that lay long before it locked: its next pair Timing::lockWait and nextFrames after the lock");
    }
    {
        // The other seat's pair lying: shown with the frames it has lain, as the engine counts them.
        PrimaryObserver14 seen(Rules{}, primaryTiming14(true));
        ai::Observation o;
        const std::array<Pair, 3> pairs{{{Cell(1), Cell(2)}, {Cell(3), Cell(4)}, {Cell(2), Cell(3)}}};
        const std::array<int, 2> notice{0, 0};
        uint32_t tick = 500;
        const auto feed = [&](int x, int y, int dir) {
            PrimarySample14 sample;
            sample.generation = 1;
            sample.seat[0] = seatOf(0, tick, 4, true, pairs);
            sample.seat[1] = seatOf(1, tick, 4, true, pairs);
            sample.seat[0]->pose.x = x, sample.seat[0]->pose.y = y, sample.seat[0]->pose.upper = false, sample.seat[0]->pose.dir = dir;
            ++tick;
            return seen.observe(sample, 1, notice, o);
        };
        bool ok = true;
        for (int k = 0; k < 5; ++k) ok = feed(2, 0, LEFT) && ok;
        const Player& q = o.player[0];
        check(ok && q.phase == Phase::Control && q.pair.grounded && q.pair.groundFrames == 5 && q.pair.owned && q.pair.ownFrames == 5,
              "the other seat's pair lying five frames: grounded, five frames, the clock of the landing as long");
        ok = feed(2, 1, DOWN);
        check(ok && q.pair.grounded && q.pair.groundFrames == 1 && !q.pair.owned && q.pair.restless, "... kicked up from the floor: landed anew, no clock of the landing");
        ok = feed(3, 1, DOWN);
        check(ok && q.pair.groundFrames == 2 && q.pair.owned && q.pair.ownFrames == 1 && !q.pair.restless, "... moved along the ground: the clock begins");
        ok = feed(3, 5, DOWN);
        check(ok && !q.pair.grounded && q.pair.groundFrames == 0 && !q.pair.owned, "... in the air: not lying");
    }
    std::printf("memory first, the player: %d checks, %d failures\n", checks, failed);
    return failed ? 1 : 0;
}
