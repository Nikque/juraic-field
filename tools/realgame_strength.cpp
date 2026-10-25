// realgame_strength <mode> <seed>: rounds drawn of X3 against X3 (beam 30, depth 8, queues 3), 2P as the mode plays it,
// 12000 frames at most. Built from the root of the tree:
//   g++ -std=c++20 -O2 -msse4.1 -mpopcnt -pthread -I. tools/realgame_strength.cpp -o realgame_strength
// mode 0: 2P plays the round itself; 6: the same with Driver::prepare; 7: as 6, 2P shown parts of what Follower reads of
// the screen in every frame (VIEW bits: 1 the other side, 2 own score and notice, 4 the pairs, 8 own score, 16 own
// notice; 7 the default; with VIEWDIFF how the other side read differs, VFIX and VFIXPH parts of it as they are);
// 2: 2P through the screen and a pad (driveRound
// of tests/realgame_tests.cpp, Driver::prepare); 3: as 2, the driver not called in one frame of 7; 4: as 3, a shaky pad.
// The last line: "mode M seed S state T ..." - state 2: 2P won, 1: 1P won. Before it (modes 2-4) "misses N of L left A
// down D miscounted C untargeted U" (pairs elsewhere than the AI chose, pairs put, pairs left alone, pairs that took the
// pair before's down button as they appeared, new pairs counted otherwise than the round gave them, pairs put with no
// placement chosen), and "timing pairs P wait W rest R hist ...": frames
// from the pair appearing to the first frame that takes a button, and from there to the lock, summed over 2P's pairs.
// Environment: LAG (the pad's lag, 5), HID, QTL (the quick turn's lock of the round), ORACLE (bits of what 2P is shown
// replaced by the round itself: 1 the other side, 2 own notice, 4 own score, 8 the frame, 16 the pairs, 32/64 own field,
// 256 own side between pairs, 1024 own side, 32768 own 13th and 14th rows, ...), PLACES (each pair put), TP (each 2P
// pair's frames), TPB=<frame> (2P's buttons up to that frame), SHOWDIFF, HOLDSTART=N (modes 0, 5, 6: 2P's first pair
// appears N frames late), AISEED (modes 2-4: the AI of 2P draws its random numbers from seed + 1, as in modes 0, 5 and
// 6, not from 1 in every round), SUREOFF (modes 2-4: Driver::unsureWays false), P2PARAMS=name=value,... (settings of
// 2P's AI; P1PARAMS of 1P's), P1AI=<name> (1P's AI, x3 by default: X3 against X3 on one tsumo often plays the same moves
// on both sides, the later side losing - a real opponent does not; lv35 never did), FINALFIELD (the fields as the round
// ended), AGENTSTATS (2P's choices made again and the like). In modes 0, 5 and 6, and in modes 2-4 with ORACLE (0 for
// nothing replaced): CHOICES (each choice of 2P with what it saw, and 2P's field as each pair appears; TOPROWS with the
// 12th to 14th rows, and in modes 2-4 those of Follower and of the driver's engine), RESEED (2P's random numbers drawn
// again for each choice from the round and the pair, so that a pair chosen twice does not change the choices after it),
// KNOWN2 (2P's own window never shows the third pair before its pair appears), SHOWSTART. In mode 7: NOTICEDIFF (2P's
// own notice read against the round's; NOTICESHOW, NOTICELONG). NOEARLY (modes 2-4): Driver::early false.
#include <bit>
#include <sstream>
#define main realgame_tests_main
#include "tests/realgame_tests.cpp"
using namespace juraic;
long T_pairs = 0, T_wait = 0, T_rest = 0, T_hist[40] = {};
int64_t T_spawn = -1, T_first = -1;
static void timePair(const Player& p, uint8_t b, const PlayerEvents& e, int64_t f) {
    if (e.spawned) T_spawn = f, T_first = -1;
    if (std::getenv("TPB") && p.phase == Phase::Control && f < std::atoi(std::getenv("TPB"))) std::printf("B %lld %02x x%d y%d up%d dir%d\n", (long long)f, b, int(p.pair.x), int(p.pair.y), int(p.pair.upper), int(p.pair.dir));
    if (p.phase == Phase::Control && T_spawn >= 0 && T_first < 0 && b) T_first = f;
    if (e.locked && T_spawn >= 0) {
        const int64_t first = T_first < 0 ? f : T_first;
        ++T_pairs, T_wait += first - T_spawn, T_rest += f - first, ++T_hist[std::min<int64_t>(39, first - T_spawn)];
        if (std::getenv("TP")) std::printf("TP spawn %lld first %lld lock %lld ground %d\n", (long long)T_spawn, (long long)first, (long long)f, int(p.pair.groundFrames));
        T_spawn = -1;
    }
}
#undef main
static uint64_t fieldHash(const GameField& g) {
    uint64_t h = 1469598103934665603ull;
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < W; ++x) h = (h ^ uint64_t(g.f.get(x, y))) * 1099511628211ull;
    return h % 1000000;
}
// The 12th, 13th and 14th rows of `g` ("row12/row13/row14").
static std::string topRows(const GameField& g) {
    std::string s;
    for (const int y : {11, 12, 13}) {
        for (int x = 0; x < W; ++x) s += ".123456G"[std::min<int>(7, y == 13 ? g.row14[size_t(x)] : g.f.get(x, y))];
        if (y != 13) s += '/';
    }
    return s;
}
// (CHOICES: 2P's field as each of its pairs appears; TOPROWS: and its top rows)
static void notePair(const Player& p, const PlayerEvents& e) {
    if (e.spawned && std::getenv("CHOICES"))
        std::printf("SP %d own %06llu%s%s\n", p.nextPair - 1, (unsigned long long)fieldHash(p.field), std::getenv("TOPROWS") ? " top " : "", std::getenv("TOPROWS") ? topRows(p.field).c_str() : "");
}
// P2PARAMS=name=value,...: settings of 2P's AI (P1PARAMS: of 1P's)
static ai::Params forTwo(ai::Params p, const char* name = "P2PARAMS") {
    if (const char* s = std::getenv(name)) {
        std::string t(s);
        std::replace(t.begin(), t.end(), ',', '\n');
        std::istringstream in(t);
        std::string error;
        if (!ai::loadParams(in, p, error)) std::fprintf(stderr, "%s: %s\n", name, error.c_str()), std::exit(2);
    }
    return p;
}
// TIMED=name=value,...: the pictures and the pad in time (TimedRun of tests/realgame_tests.cpp; "on" alone with its
// defaults): phase shown shownSpread comes comesSpread read readSpread drive driveSpread input inputSpread dropOneIn
// twiceOneIn backOneIn stallEvery stallFor slowOneIn slow fill filter takeSpread sendAfter.
static TimedRun timedRun() {
    TimedRun t;
    const char* s = std::getenv("TIMED");
    if (!s) return t;
    t.on = true;
    std::string all(s);
    std::replace(all.begin(), all.end(), ',', ' ');
    std::istringstream in(all);
    for (std::string item; in >> item;) {
        const size_t eq = item.find('=');
        const std::string k = item.substr(0, eq);
        const double v = eq == std::string::npos ? 1.0 : std::atof(item.c_str() + eq + 1);
        double* d = k == "phase" ? &t.phase : k == "shown" ? &t.shown : k == "shownSpread" ? &t.shownSpread : k == "comes" ? &t.comes : k == "comesSpread" ? &t.comesSpread
                  : k == "read" ? &t.read : k == "readSpread" ? &t.readSpread : k == "drive" ? &t.drive : k == "driveSpread" ? &t.driveSpread : k == "input" ? &t.input
                  : k == "inputSpread" ? &t.inputSpread : k == "slow" ? &t.slow : k == "takeSpread" ? &t.takeSpread : k == "sendAfter" ? &t.sendAfter : nullptr;
        int* i = k == "dropOneIn" ? &t.dropOneIn : k == "twiceOneIn" ? &t.twiceOneIn : k == "backOneIn" ? &t.backOneIn : k == "stallEvery" ? &t.stallEvery
               : k == "stallFor" ? &t.stallFor : k == "slowOneIn" ? &t.slowOneIn : nullptr;
        if (d) *d = v;
        else if (i) *i = int(v);
        else if (k == "fill") t.fill = v != 0;
        else if (k == "filter") t.filter = v != 0;
        else if (k != "on") std::fprintf(stderr, "TIMED: %s?\n", k.c_str()), std::exit(2);
    }
    return t;
}
// (Driver::replans where the driver has it)
template <class D> static int replansOf(const D& d) {
    if constexpr (requires { d.replans; }) return d.replans;
    else return -1;
}
// The pairs of 2P (modes 2-4): the frames from a pair appearing to the first frame that took a button sent for it and to
// its lock, the turns and moves it took (from buttons sent for it) and the fewest that bring it from where it appeared
// to where it locked, the placement first chosen for it and the last, and why the last is another (the second choice,
// no way left to the one before, or else), and the own pairs to come not known as a pair appeared (the window not read).
struct PairLog {
    long pairs = 0, firstSum = 0, lockSum = 0, presses = 0, fewest = 0, retargeted = 0, bySecond = 0, byNoWay = 0, byOther = 0, holes = 0;
    long otherRunTicks = 0, otherRunAge = 0, ticks = 0; // ticks with the other side's chain followed, and the frames by which it was behind the own side
};
static PairLog G_pairs;
static const Round* G_round = nullptr;     // the round being played (null once it is over)
static GameField G_final, G_final2;       // the fields of 1P and 2P as the last round ended (FINALFIELD)
static int G_locks[2] = {0, 0}, G_control[2] = {0, 0};
static int G_state = 0, G_maxChain[2] = {0, 0}, G_frames = 0;
static int64_t G_score[2] = {0, 0};
// (PlacementAgent::unknownFrames where the agent has it)
template <class A> static int64_t unknownOf(const A& a) {
    if constexpr (requires { a.unknownFrames; }) return a.unknownFrames;
    else return -1;
}
// A round so (tests/realgame_tests.cpp driveRound), and the driver's ways planned again and the frames 2P's pairs waited
// for their colours.
static void driveMatch(uint64_t seed, DriveCounts& n, DriveScenario how, int* replans = nullptr, int64_t* unknownWaited = nullptr) {
    const Layout l = sampleLayout();
    const Rules rules;
    Round r;
    Timing roundTiming;
    if (how.quickTurnLock >= 0) roundTiming.quickTurnLock = how.quickTurnLock;
    if (const char* q = std::getenv("QTL")) roundTiming.quickTurnLock = std::atoi(q);
    r.start(rules, seed, 2, roundTiming);
    G_round = &r;
    Canvas canvas(1280, 720, l);
    Rng noise(seed, how.noiseStream), shake(seed, 9), skipper(seed, 11), glitcher(seed, 13), twicer(seed, 15);
    ai::Params narrow;
    narrow.beamWidth = 30, narrow.beamDepth = 8, narrow.beamQueues = 3;
    const std::unique_ptr<ai::Agent> other = ai::makeAgent(std::getenv("P1AI") ? std::getenv("P1AI") : "x3", forTwo(narrow, "P1PARAMS"));
    other->startRound(0, seed);
    Follower follow;
    Driver d;
    d.lag = how.startLag >= 0 ? how.startLag : how.lag, d.rules = rules, d.agent = how.given ? std::move(how.given) : ai::makeAgent("x3", narrow);
    d.notes = how.notes;
    // (the AI's own random numbers as in modes 0, 5 and 6: the driver begins a round of its agent before the round and
    //  again as it begins, and the second one draws from seed + 1)
    if (std::getenv("AISEED")) d.seed = seed;
    d.takeLag = !how.keepLag;
    d.unsureWays = how.unsureWays && !std::getenv("SUREOFF"); // (SUREOFF: Driver::unsureWays false)
    d.early = how.early && !std::getenv("NOEARLY");            // (NOEARLY: Driver::early false)
    int64_t clock = 1000;
    TimedPipe pipe(how.timed, seed, follow, d, 1045);
    for (int f = 0; f < 45; ++f, ++clock) {
        draw(canvas, l, r, true, noise);
        if (how.timed.on) {
            pipe.picture(read(canvas.picture(), l), f - 45);
            pipe.run(f - 44, [&](int64_t, int, uint8_t out) { check(out == 0, "buttons before the round"); });
            continue;
        }
        follow.step(read(canvas.picture(), l), clock);
        check(d.tick(follow, clock) == 0, "buttons before the round");
    }
    std::vector<uint8_t> sent(size_t(how.frames) + 100, 0); // by the frame of the clock they were sent in
    const int64_t first = clock;
    ai::Observation o;
    int target = -1, played = 0; // (the engine of the Driver plays `lag` frames ahead of the round: the target of the pair it played last)
    int oldFor = 0, hideFor = 0, spawns = 0; // (spawns: pairs of 2P that have appeared in the round)
    int lastTurn = -100;                     // the frame of the round the pair of 2P last turned in
    PairStart ps; // the buttons the pair of 2P that appeared last takes as it appears
    uint8_t oldDir = 0;
    int alone = 0; // frames in a row the pair of 2P in the air took no button
    int litFor = 0; // litAsAppears: frames the light over 2P stays
    int strayPair = -1;        // strayAfterLight: the pair lit as it appeared, whose colour is drawn once after the light
    int64_t litEndedAt = -1;   // ... the frame of the clock the last light went in
    bool words = false;
    int correctedBefore = 0; // the driver's pictures put right before the words came
    int kickFor = 0;         // kickShown: pictures left that show the pair of 2P as before its turn that changed its column
    int8_t kickFrom = 0, kickTo = 0;
    uint8_t kickDir = 0;
    int64_t kickAt = -100;   // the frame of the clock of that turn
    bool kickPair = false;   // the pair in play has turned so
    int liftFor = 0;         // liftShown: pictures left that show the pair of 2P as before its turn that changed its row
    PairControl liftFrom{};
    int64_t liftAt = -100;
    bool liftPair = false;
    bool highPair = false; // the pair in play has been taken up into the 13th row
    bool quickPair = false; // ... turned quickly
    bool quickMissed = false; // the last pair of 2P locked was turned quickly and locked elsewhere than the AI chose
    int sinceQuick = -1;      // frames since the pair of 2P in play last turned quickly (-1: it has not)
    bool earlyPair = false;   // ... and it took a turn sooner after it than the driver's engine takes one
    int spawnIndex = -1;   // ... its number
    int64_t garbageAt = -1, clearAt = -1, lockedAt = -1; // the frames of the clock garbage last fell on 2P, its last clear picture, its last lock
    bool beginning = false; // the pair in play has taken no button sent for it yet ...
    long unbegun = 0;       // ... in so many frames
    bool loseNext = false;  // loseAtGround: the next frame takes the buttons sent a frame later
    bool glitchNext = false, glitchDrawn = false; // glitchAfterTurn: the next picture shows the pair at glitchAt; this one does
    PairControl glitchAt{};
    std::deque<PairControl> recent; // the pair of 2P as each of the last frames began
    int turnedAt = -100;            // the frame of the round the pair of 2P last turned in
    int pauseLeft = 0, inPlay = 0;  // pauseFor: frames the round stays stopped; frames the pair of 2P has been in play
    bool pausedPair = false;        // ... the pair in play was in play as the round stopped,
    int pausedTarget = -1;          // ... and the placement the AI had chosen for it then
    // (the pairs of 2P: PairLog)
    const auto* placing = dynamic_cast<const ai::PlacementAgent*>(d.agent.get());
    int64_t pairFrom = -1, pairFirst = -1; // the frame of the round the pair in play appeared in, its first button sent for it
    int pairPresses = 0, firstAim = -1;
    int64_t againAt = 0, steeredAt = 0;    // the agent's counts as it appeared
    GameField pairField;                   // the field it appeared over
    uint8_t pressedBefore = 0;
    // (a pair begun by the driver: its first placement chosen from now on)
    const auto begun = [&] { firstAim = -1, againAt = placing ? placing->againSwitched : 0, steeredAt = placing ? placing->steered : 0; };
    // (timed: after each drive of the pad, in the frame of the clock `now` it was driven in)
    const auto driven = [&](int64_t now, int correctedNow, uint8_t) {
        if (now - kickAt <= 4) n.kickCorrected += d.corrected - correctedNow;
        if (now - liftAt <= 8) n.liftCorrected += d.corrected - correctedNow;
        if (d.played != played) played = d.played, target = -1, ps.begin(now, d.begunPair()), begun();
        if (d.target() >= 0) target = d.target();
        if (d.target() >= 0 && firstAim < 0) firstAim = d.target();
        ++G_pairs.ticks;
        if (follow.model[0].running) ++G_pairs.otherRunTicks, G_pairs.otherRunAge += std::max<int64_t>(0, now + d.lag - follow.model[0].runAt);
    };
    uint8_t tapAs[2] = {0, 0};
    int tapFor[2] = {0, 0};
    int nextLitFor = 0; // NEXTLIT: frames the window of 2P stays lit
    for (int f = 0; f < how.frames && r.state == RoundState::Playing; ++f, ++clock) {
        if (pauseLeft > 0) {
            // (the round stopped: drawn as it stands, the light over it; the driver's clock goes on)
            draw(canvas, l, r, false, noise, how.quietLanding, words);
            const SideLayout& s = l.side[1];
            canvas.fill(s.left + s.cellWidth, s.bottom - 12 * s.cellHeight, s.left + 4 * s.cellWidth, s.bottom - 9 * s.cellHeight, LIGHT);
            if (how.timed.on) {
                pipe.picture(read(canvas.picture(), l), f);
                pipe.run(f + 1, driven);
                int64_t ignored = 0;
                pipe.taken(f + 1, ignored);
                --pauseLeft;
                continue;
            }
            follow.step(read(canvas.picture(), l), clock);
            sent[size_t(clock - first)] = d.tick(follow, clock);
            if (d.played != played) played = d.played, target = -1, ps.begin(clock, d.begunPair());
            if (d.target() >= 0) target = d.target();
            --pauseLeft;
            continue;
        }
        if (!words && how.wordsFrom >= 0 && f >= how.wordsFrom) {
            int h[W];
            r.player[1].field.heights(h);
            words = std::max({h[1], h[2], h[3], h[4]}) <= 8;
            correctedBefore = d.corrected;
        }
        if (oldFor > 0 || hideFor > 0 || kickFor > 0 || liftFor > 0 || glitchNext) {
            Round shown = r;
            if (oldFor > 0) shown.player[1].pair.dir = oldDir, --oldFor;
            // (a pair turned so in the 12th row is read clear, its child in the hidden 13th row as it turns)
            const bool kickTop = (kickFor > 0 || liftFor > 0) && shown.player[1].phase == Phase::Control && shown.player[1].pair.y >= 11;
            if (liftFor > 0) {
                if (shown.player[1].phase == Phase::Control) {
                    PairControl& q = shown.player[1].pair;
                    q.x = liftFrom.x, q.y = liftFrom.y, q.upper = liftFrom.upper, q.dir = liftFrom.dir;
                }
                --liftFor;
            }
            if (kickFor > 0) {
                if (shown.player[1].phase == Phase::Control)
                    shown.player[1].pair.x = int8_t(kickFor == how.kickShown ? kickFrom : kickFrom + (kickTo > kickFrom ? 1 : -1)), shown.player[1].pair.dir = kickDir;
                --kickFor;
            }
            if (hideFor > 0 && shown.player[1].phase == Phase::Control) {
                if (!kickTop) shown.player[1].phase = Phase::Split;
                --hideFor;
            }
            if (glitchNext) {
                if (r.player[1].phase == Phase::Control) {
                    shown.player[1].phase = Phase::Control;
                    PairControl& q = shown.player[1].pair;
                    q.x = glitchAt.x, q.y = glitchAt.y, q.upper = glitchAt.upper, q.dir = glitchAt.dir;
                    ++n.glitches, hideFor = std::max(hideFor, how.glitchAfterTurn), glitchDrawn = true;
                }
                glitchNext = false;
            }
            draw(canvas, l, shown, false, noise, how.quietLanding, words);
        } else {
            draw(canvas, l, r, false, noise, how.quietLanding, words);
        }
        // (litAsAppears: one pair of 2P in three lit from 8 frames before it appears, for that many frames - on the real game
        //  the screen of 2P read nothing for 30 frames as a pair appeared)
        if (how.litAsAppears && r.player[1].phase == Phase::Next && r.player[1].wait == 8 && spawns % 3 == 2)
            litFor = how.strayAfterLight ? 1000 : how.litAsAppears, strayPair = r.player[1].nextPair;
        if (how.litFirst && spawns == 0 && r.player[1].phase == Phase::Next && r.player[1].wait == 8) litFor = how.litFirst;
        // (strayAfterLight: the light stays until that pair has locked)
        if (how.strayAfterLight && strayPair >= 0 && litFor > 1 && r.player[1].nextPair == strayPair + 1 && r.player[1].phase != Phase::Control) litFor = 1;
        if (f % 12 < how.lit || litFor > 0) {
            // (a light over the field of 2P: nothing of it is read in this picture)
            const SideLayout& s = l.side[1];
            canvas.fill(s.left, s.bottom - 2 * s.cellHeight, s.left + s.cellWidth, s.bottom - s.cellHeight, LIGHT);
            if (litFor > 0 && --litFor == 0) litEndedAt = clock;
        }
        // (NEXTLIT=n: one pair of 2P in three, the window of its next pairs lit from 10 frames before the pair appears for n
        //  frames - the window not read as it moves on, the pair after the next two not learnt from it)
        static const int nextLit = std::getenv("NEXTLIT") ? std::atoi(std::getenv("NEXTLIT")) : 0;
        if (nextLit && r.player[1].phase == Phase::Next && r.player[1].wait == 10 && spawns % 3 == 1) nextLitFor = nextLit;
        if (nextLitFor > 0) {
            const SideLayout& s = l.side[1];
            for (const auto& spots : s.next)
                for (const Spot& at : spots) canvas.fill(at.x - s.cellWidth * 0.5f, at.y - s.cellWidth * 0.5f, at.x + s.cellWidth * 0.5f, at.y + s.cellWidth * 0.5f, LIGHT);
            --nextLitFor;
        }
        // (... and once it has gone, the pair unseen: a piece of its colour drawn once where pairs appear - 1 -, half a
        //  cell lower - 2 -, a cell lower - 3 - or in the column beside - 4 -, as a piece of a chain falling past, three
        //  frames or more after the light, the field read
        //  still for three frames, as the pair lands or before the window moves)
        const bool quiet = r.player[1].phase == Phase::Land || (r.player[1].phase == Phase::Next && r.player[1].wait > r.timing.nextSlide);
        if (how.strayAfterLight && strayPair >= 0 && litFor == 0 && litEndedAt >= 0 && clock - litEndedAt >= 3 && r.player[1].nextPair == strayPair + 1 && quiet &&
            clock - follow.tracker.side[1].changedAt >= 3 && follow.tracker.side[1].pairs + follow.tracker.side[1].unseen <= strayPair) {
            int h[W];
            r.player[1].field.heights(h);
            const int sx = how.strayAfterLight == 4 ? SPAWN_X + 1 : SPAWN_X;
            const float sy = how.strayAfterLight == 1 || how.strayAfterLight == 4 ? 12.f : how.strayAfterLight == 2 ? 11.5f : 11.f;
            if (h[sx] <= 10) piece(canvas, l, l.side[1], sx, sy, r.tsumo.pair(strayPair).axis), ++n.strays;
            strayPair = -1;
        }
        if (strayPair >= 0 && r.player[1].nextPair > strayPair + 1) strayPair = -1;
        if (how.coveredFrom >= 0 && f >= how.coveredFrom) {
            const SideLayout& s = l.side[1];
            canvas.fill(s.left + s.cellWidth, s.bottom - 10.75f * s.cellHeight, s.left + 5 * s.cellWidth, s.bottom - 8.25f * s.cellHeight, Rgb{240, 210, 60});
        }
        if (how.timed.on) pipe.picture(read(canvas.picture(), l), f), pipe.run(f + 1, driven);
        else follow.step(read(canvas.picture(), l), clock);
        if (follow.tracker.side[1].clear) clearAt = clock;
        // (the pair seen as it is about to appear, or in play already when seen late)
        if (follow.tracker.spawned[1])
            ++n.spawnsSeen, n.miscounted += follow.tracker.side[1].pairs != r.player[1].nextPair + (r.player[1].phase == Phase::Control ? 0 : 1);
        if (glitchDrawn) {
            const SideTrack& s1 = follow.tracker.side[1];
            n.glitchesRead += s1.pair.seen && s1.clear && s1.pair.at.x == glitchAt.x && s1.pair.at.y == glitchAt.y && s1.pair.at.dir == glitchAt.dir;
            glitchDrawn = false;
        }
        if (words) {
            n.wordsSeen += follow.tracker.spawned[1];
            if (r.player[1].phase == Phase::Control) ++n.wordsControl, n.wordsPairSeen += follow.tracker.side[1].pair.seen;
        }
        if (how.later && f == how.laterAt) d.replaceAgent(std::move(how.later)); // (another AI chosen on the screen of the mode)
        // (a frame the driver is not called in: the pad holds what it held)
        if (how.timed.on) {
        } else if (how.skipOneIn && f > 0 && skipper.below(uint64_t(how.skipOneIn)) == 0) {
            sent[size_t(clock - first)] = sent[size_t(clock - first - 1)];
        } else {
            const int correctedNow = d.corrected;
            sent[size_t(clock - first)] = d.tick(follow, clock);
            if (how.twiceOneIn && twicer.below(uint64_t(how.twiceOneIn)) == 0) sent[size_t(clock - first)] = d.tick(follow, clock);
            if (clock - kickAt <= 4) n.kickCorrected += d.corrected - correctedNow;
            if (clock - liftAt <= 8) n.liftCorrected += d.corrected - correctedNow;
            ++G_pairs.ticks;
            if (follow.model[0].running) ++G_pairs.otherRunTicks, G_pairs.otherRunAge += std::max<int64_t>(0, clock + d.lag - follow.model[0].runAt);
        }
        if (!how.timed.on && d.played != played) begun();
        if (d.target() >= 0 && firstAim < 0 && !how.timed.on) firstAim = d.target();
        if (!how.timed.on && d.played != played) {
            played = d.played, target = -1, ps.begin(clock, d.begunPair());
            const GameField &begun = d.begunField(), &game = r.player[1].field;
            for (int x = 0; x < W; ++x) {
                if (Follower::shown(begun).f.occupied().column(x) != Follower::shown(game).f.occupied().column(x)) continue;
                for (const bool row14 : {false, true}) {
                    const bool there = row14 ? game.row14[size_t(x)] != EMPTY : game.f.get(x, 12) != EMPTY;
                    const bool had = row14 ? begun.row14[size_t(x)] != EMPTY : begun.f.get(x, 12) != EMPTY;
                    if (!there && !had) continue;
                    n.hiddenCells += 1, n.hiddenWrong += there != had, n.hiddenWrongUnsure += there != had && (d.unsureColumns() >> x & 1);
                }
            }
        }
        if (d.target() >= 0) target = d.target();
        // The next frame of the round takes what was sent `lag` frames before it (a shaky pad: now and then a frame
        // sooner or later).
        int64_t from = clock + 1 - how.lag;
        if (how.shaky && shake.below(8) == 0) from += shake.below(2) ? 1 : -1;
        if (loseNext) from += 1, loseNext = false;
        from = std::min(from, clock);
        uint8_t buttons[2];
        ai::observe(r, 0, o);
        buttons[0] = other->frame(o);
        buttons[1] = how.timed.on ? pipe.taken(f + 1, from) : from >= first ? sent[size_t(from - first)] : 0;
        if (how.timed.on && r.player[1].phase == Phase::Control) {
            static constexpr uint8_t KINDS[2] = {BTN_CW | BTN_CCW, BTN_LEFT | BTN_RIGHT};
            for (int k = 0; k < 2; ++k) {
                const uint8_t now = buttons[1] & KINDS[k];
                if (now != tapAs[k] && tapAs[k]) ++n.taps[k][std::min(3, tapFor[k] - 1)];
                tapFor[k] = now && now == tapAs[k] ? tapFor[k] + 1 : 1;
                tapAs[k] = now;
            }
        }
        if (how.swapped) buttons[1] = uint8_t((buttons[1] & ~(BTN_CW | BTN_CCW)) | (buttons[1] & BTN_CW ? BTN_CCW : 0) | (buttons[1] & BTN_CCW ? BTN_CW : 0));
        PlayerEvents e[2];
        // (the pair of 2P before this frame - as it appears, when it appears in this frame: a turn taken in its first
        //  frame and shown late is drawn the way it appeared, not the way of the pair put before it)
        const PairControl pairBefore = r.player[1].phase == Phase::Control ? r.player[1].pair : PairControl{};
        const uint8_t dirBefore = pairBefore.dir;
        const int8_t columnBefore = pairBefore.x;
        const bool groundedBefore = r.player[1].phase == Phase::Control && r.player[1].pair.grounded;
        alone = r.player[1].phase == Phase::Control && buttons[1] == 0 ? alone + 1 : 0;
        n.firstIdle += spawns == 1 && r.player[1].phase == Phase::Control && buttons[1] == 0;
        if (alone == 10) ++n.leftAlone;
        n.control += r.player[1].phase == Phase::Control;
        const uint8_t pressed1 = buttons[1];
        r.step(buttons, e);
        timePair(r.player[1], pressed1, e[1], f);
        notePair(r.player[1], e[1]);
        if (e[1].spawned && std::getenv("TOPROWS"))
            std::printf("FM %d followed %d known %d model %s engine %s unsure %02x\n", r.player[1].nextPair - 1, follow.model[1].followed, int(follow.model[1].known),
                        topRows(follow.model[1].core.field).c_str(), topRows(d.begunField()).c_str(), unsigned(d.unsureColumns()));
        for (int s = 0; s < 2; ++s) G_maxChain[s] = std::max(G_maxChain[s], int(r.player[s].chain.chains)), G_locks[s] += e[s].locked, G_control[s] += r.player[s].phase == Phase::Control;
        if (std::getenv("PLACES")) for (int s = 0; s < 2; ++s) if (e[s].locked) std::printf("P%d %d %d f%d\n", s + 1, r.player[s].nextPair, placementId(r.player[s].pair.placement()), int(r.frame));
        G_frames = f;
        if (how.turnShownLate && e[1].control.rotated && r.player[1].phase == Phase::Control) oldDir = dirBefore, oldFor = how.turnShownLate;
        if (how.crowdedLate && e[1].control.rotated && r.player[1].phase == Phase::Control) {
            if (r.player[1].pair.x != columnBefore || f - lastTurn <= 3) {
                if (oldFor == 0) oldDir = dirBefore;
                oldFor = how.crowdedLate;
            }
            lastTurn = f;
        }
        if (how.movedHidden && r.player[1].phase == Phase::Control && r.player[1].pair.x != columnBefore && !e[1].spawned) hideFor = std::max(hideFor, how.movedHidden);
        // (from the frame a pair appears in, until the frames take buttons the driver sent once it had begun that pair)
        if (e[1].spawned) ++spawns, n.early += ps.appear(clock, r.player[1].nextPair), n.appeared.push_back(clock), n.wordsSpawns += words;
        ps.take(from);
        if (e[1].spawned) beginning = true, unbegun = 0, spawnIndex = r.player[1].nextPair - 1;
        if (beginning && ps.firstBegunAt >= 0 && from >= ps.firstBegunAt) beginning = false;
        if (beginning && !e[1].spawned && r.player[1].phase == Phase::Control) n.mostUnbegun = std::max(n.mostUnbegun, ++unbegun), ++n.unbegunFrames;
        if (e[1].garbageFell) garbageAt = clock;
        if (how.loseAtGround && r.player[1].phase == Phase::Control && r.player[1].pair.grounded && !groundedBefore) loseNext = true;
        // (a turn taken from buttons the driver sent for the pair in play, not from those sent before it was begun)
        if (how.kickShown && e[1].control.rotated && dirBefore == UP && !ps.leftOver && !e[1].spawned && r.player[1].phase == Phase::Control && r.player[1].pair.x != columnBefore)
            ++n.kicks, n.kickMoves += std::abs(r.player[1].pair.x - columnBefore) > 1, kickFrom = columnBefore, kickTo = r.player[1].pair.x, kickDir = dirBefore, kickFor = how.kickShown, kickAt = clock, kickPair = true;
        if (ps.downCarried(buttons[1])) ++n.downAtSpawn;
        // (PairLog)
        if (e[1].spawned) {
            pairFrom = f, pairFirst = -1, pairPresses = 0, pairField = r.player[1].field, pressedBefore = 0;
            // (a pair to come no window showed, before one known: shown to the AI as known before observeWith kept the
            //  known pairs together)
            for (int i = r.player[1].nextPair - 1; i < int(follow.tracker.pairsLearnt()); ++i)
                if (!follow.tracker.knows(1, i)) {
                    ++G_pairs.holes;
                    break;
                }
        }
        if (pairFrom >= 0 && (r.player[1].phase == Phase::Control || e[1].locked)) {
            const uint8_t mine = ps.leftOver ? 0 : uint8_t(buttons[1] & (BTN_LEFT | BTN_RIGHT | BTN_CW | BTN_CCW));
            if (pairFirst < 0 && !ps.leftOver && buttons[1]) pairFirst = f;
            pairPresses += std::popcount(unsigned(mine & ~pressedBefore));
            pressedBefore = mine;
        }
        if (e[1].locked && pairFrom >= 0) {
            ++G_pairs.pairs, G_pairs.firstSum += (pairFirst < 0 ? f : pairFirst) - pairFrom, G_pairs.lockSum += f - pairFrom, G_pairs.presses += pairPresses;
            ai::InputPlanner fewest;
            ai::InputPath way;
            if (fewest.plan(pairField, spawnPair(r.timing), r.player[1].pair.placement(), r.timing, rules.speedPercent, way)) {
                uint8_t was = 0;
                for (const uint8_t b : way.buttons) {
                    const uint8_t k = b & (BTN_LEFT | BTN_RIGHT | BTN_CW | BTN_CCW);
                    G_pairs.fewest += std::popcount(unsigned(k & ~was)), was = k;
                }
            }
            if (firstAim >= 0 && target >= 0 && firstAim != target) {
                ++G_pairs.retargeted;
                if (placing && placing->againSwitched != againAt) ++G_pairs.bySecond;
                else if (placing && placing->steered != steeredAt) ++G_pairs.byNoWay;
                else ++G_pairs.byOther;
            }
            pairFrom = -1;
        }
        if (how.hidden && e[1].control.rotated && r.player[1].phase == Phase::Control) hideFor = how.hidden;
        // (a pair the AI found no placement for - the field full - is not counted)
        if (e[1].locked && target >= 0) {
            const bool hit = placementId(r.player[1].pair.placement()) == target;
            n.locks += 1, n.hits += hit;
            n.garbageMisses += !hit && garbageAt > lockedAt, n.unseenGarbageMisses += !hit && garbageAt > lockedAt && clearAt <= garbageAt;
        }
        if (e[1].locked && target < 0) ++n.untargeted;
        if (e[1].locked && target >= 0 && kickPair) n.kickLocks += 1, n.kickHits += placementId(r.player[1].pair.placement()) == target;
        if (how.liftShown && e[1].control.rotated && !ps.leftOver && !e[1].spawned && r.player[1].phase == Phase::Control &&
            (r.player[1].pair.y != pairBefore.y || r.player[1].pair.upper != pairBefore.upper))
            ++n.lifts, liftFrom = pairBefore, liftFor = how.liftShown, liftAt = clock, liftPair = true;
        if (e[1].locked && target >= 0 && liftPair) n.liftLocks += 1, n.liftHits += placementId(r.player[1].pair.placement()) == target;
        if (!ps.leftOver && !e[1].spawned && (e[1].control.quickTurned || (e[1].control.rotated && r.player[1].pair.y >= 12))) highPair = true;
        recent.push_back(pairBefore);
        if (recent.size() > 3) recent.pop_front();
        if (e[1].spawned) recent.clear();
        turnedAt = e[1].control.rotated ? f : turnedAt;
        if (how.glitchAfterTurn && e[1].control.moved && f - turnedAt <= 2 && !ps.leftOver && !e[1].spawned && r.player[1].phase == Phase::Control && recent.size() == 3 &&
            glitcher.below(2) == 0)
            glitchAt = recent.front(), glitchNext = true;
        if (e[1].locked && highPair) {
            const auto* placing = dynamic_cast<const ai::PlacementAgent*>(d.agent.get());
            n.highUnallowed += !placing || std::find(placing->climbing.begin(), placing->climbing.end(), spawnIndex) == placing->climbing.end();
        }
        if (!ps.leftOver && !e[1].spawned && e[1].control.quickTurned) quickPair = true;
        if (e[1].spawned) sinceQuick = -1;
        else if (sinceQuick >= 0 && ++sinceQuick <= realGameTiming().quickTurnLock && e[1].control.rotated) ++n.quickEarly, earlyPair = true;
        if (!ps.leftOver && !e[1].spawned && e[1].control.quickTurned) sinceQuick = 0;
        if (e[1].locked) n.quickEarlyPairs += earlyPair, earlyPair = false, sinceQuick = -1;
        if (e[1].locked && quickPair && target >= 0) n.quickLocks += 1, n.quickHits += placementId(r.player[1].pair.placement()) == target;
        if (e[1].locked) quickMissed = quickPair && target >= 0 && placementId(r.player[1].pair.placement()) != target;
        if (e[1].locked) quickPair = false;
        if (e[1].locked) n.highPairs += highPair, highPair = false;
        if (e[1].locked) kickPair = false, kickFor = 0, liftPair = false, liftFor = 0, beginning = false, lockedAt = clock;
        if (how.garbageAsLit && e[1].locked && spawns % 3 == 2) r.player[1].incoming.confirmed += how.garbageAsLit;
        if (how.hiddenGarbage && e[1].locked && follow.model[1].known) {
            int h[W];
            r.player[1].field.heights(h);
            for (int x = 0; x < W; ++x)
                if (h[x] == 12 && x != SPAWN_X && (Follower::twelveHigh(follow.model[1].core.field) >> x & 1) && follow.model[1].core.field.f.get(x, 12) == EMPTY)
                    r.player[1].field.f.set(x, 12, GARBAGE), follow.model[1].unsure |= uint8_t(1u << x);
        }
        if (e[1].locked && pausedPair)
            n.pausedLocks += pausedTarget >= 0, n.pausedHits += pausedTarget >= 0 && placementId(r.player[1].pair.placement()) == pausedTarget, pausedPair = false;
        if (e[1].spawned) inPlay = 0;
        if (how.pauseFor && r.player[1].phase == Phase::Control && ++inPlay == how.pauseAfter && spawns % how.pauseEvery == 1)
            pauseLeft = how.pauseFor, ++n.pauses, pausedPair = true, pausedTarget = target;
    }
    n.corrected += d.corrected;
    G_state = int(r.state), G_final = r.player[0].field, G_final2 = r.player[1].field, G_round = nullptr;
    for (int s = 0; s < 2; ++s) G_score[s] = r.player[s].scoring.score;
    n.quickLost += quickMissed && r.state != RoundState::Playing && r.state != RoundState::Won1;
    if (const auto* placing = dynamic_cast<const ai::PlacementAgent*>(d.agent.get())) {
        n.rerouted += placing->rerouted, n.steered += placing->steered, n.steeredLow += placing->steeredLow, n.climbs += placing->climbs;
        const ai::InputPlanner& w = placing->ways();
        n.ways[0] += w.sureWays, n.ways[1] += w.steadyWays, n.ways[2] += w.probingWays, n.ways[3] += w.seenWays, n.ways[4] += w.blindWays;
    }
    n.explained += d.explained;
    if (std::getenv("AGENTSTATS"))
        if (const auto* placing = dynamic_cast<const ai::PlacementAgent*>(d.agent.get()))
            std::printf("AGENT revised %lld late %lld rerouted %lld steered %lld again %lld/%lld/%lld/%lld corrected %d\n", (long long)placing->revisedChoices,
                        (long long)placing->lateFrames, (long long)placing->rerouted, (long long)placing->steered, (long long)placing->againAsked, (long long)placing->againBegun,
                        (long long)placing->againSwitched, (long long)placing->againNoPath, d.corrected);
    if (words) n.wordsCorrected += d.corrected - correctedBefore;
    n.lagRounds += 1, n.lagRight += d.measuredLag() == how.lag;
    n.lagTaken += d.lag == how.lag, n.turnsFound += d.turnsSwapped;
    if (how.sentOut) *how.sentOut = sent;
    if (how.timed.on) {
        n.pictures += pipe.pictures, n.followed += pipe.followed, n.drives += pipe.drives, n.fills += pipe.fills, n.lost += pipe.lost;
        n.twice += pipe.pace.twice, n.back += pipe.pace.back, n.nextFrame += pipe.pace.next, n.missing += pipe.pace.missing, n.stalls += pipe.pace.stalls;
    }
    if (replans) *replans = replansOf(d);
    if (unknownWaited)
        if (const auto* p = dynamic_cast<const ai::PlacementAgent*>(d.agent.get())) *unknownWaited = unknownOf(*p);
}

// The AI of 2P shown parts of the round as they are (ORACLE bits): 1 the opponent, 2 its own notice, 4 its own
// score, 8 the frame of the round, 16 the pairs of the tsumo.
struct Oracle : ai::X3Agent {
    int bits = 0;
    long opp = 0, oppPhase = 0, oppField = 0, oppIncoming = 0, oppChain = 0, oppScore = 0, oppNext = 0, oppTab[8][8] = {};
    long between = 0, phaseOff = 0, waitOff = 0, fieldOff = 0, chainOff = 0, phaseTab[8][8] = {};
    long seen = 0, indexOff = 0, colourOff = 0, pairsSeen = 0, pairsOff = 0;
    ~Oracle() override {
        std::printf("choices %ld known on average %.2f (1:%ld 2:%ld 3:%ld 4+:%ld)\n", choices, choices ? double(knownSum) / double(choices) : 0.0, knownTab[1], knownTab[2], knownTab[3], knownTab[4] + knownTab[5]);
        std::printf("opponent %ld: phase other %ld field %ld incoming %ld chain %ld score %ld nextPair %ld\n", opp, oppPhase, oppField, oppIncoming, oppChain, oppScore, oppNext);
        for (int a = 0; a < 8; ++a) for (int b = 0; b < 8; ++b) if (oppTab[a][b] > 30) std::printf("  opp shown %d true %d: %ld\n", a, b, oppTab[a][b]);
        std::printf("between %ld phase other %ld wait other %ld field other %ld chain other %ld\n", between, phaseOff, waitOff, fieldOff, chainOff);
        for (int a = 0; a < 8; ++a) for (int b = 0; b < 8; ++b) if (phaseTab[a][b] > 20) std::printf("  shown %d true %d: %ld\n", a, b, phaseTab[a][b]);
    }
    using ai::X3Agent::X3Agent;
    uint64_t roundSeed = 0;
    void startRound(int seat, uint64_t seed) override {
        if (std::getenv("SHOWSTART")) std::printf("startRound %d %llu\n", seat, (unsigned long long)seed);
        roundSeed = seed;
        ai::X3Agent::startRound(seat, seed);
    }
    long choices = 0, knownSum = 0, knownTab[6] = {};
    int choose(const ai::Observation& o, Pair pair, unsigned legal) override {
        const int index = o.self().nextPair - (o.self().phase == Phase::Control ? 1 : 0);
        int known = 0;
        for (int i = index; i < index + 5 && o.mine().known(i); ++i) ++known;
        ++choices, knownSum += known, ++knownTab[std::min(known, 5)];
        // (RESEED: the random numbers of each choice drawn from the round and the pair, however often a pair is chosen)
        if (std::getenv("RESEED")) rng = Rng(roundSeed * 1000003 + uint64_t(index), ai::AGENT_STREAM);
        Rng probe = rng;
        const int pick = ai::X3Agent::choose(o, pair, legal);
        if (std::getenv("CHOICES")) {
            // what the choice saw: own field, the other side's field, phase and chain, the notices, the pairs known, the
            // next draw of the random numbers
            const auto hash = fieldHash;
            std::printf("CH %d moves %d own %06llu opp %06llu oppPhase %d oppChain %d inMine %d inTheirs %d known %d tie %llu pick %d\n", index, int(o.self().moves),
                        (unsigned long long)hash(o.self().field), (unsigned long long)hash(o.player[1 - o.me].field), int(o.player[1 - o.me].phase), int(o.player[1 - o.me].chain.chains),
                        int(o.self().incoming.total()), int(o.player[1 - o.me].incoming.total()), known, (unsigned long long)(probe.next() % 1000), pick);
            if (std::getenv("TOPROWS")) std::printf("CT %d top %s\n", index, topRows(o.self().field).c_str());
            const Player &m = o.self(), &t = o.opponent();
            std::printf("CX %d frame %lld phase %d wait %d score %lld left %lld pend %d conf %d oScore %lld oLeft %lld oPend %d oConf %d oWait %d oNext %d oWave %d\n", index,
                        (long long)o.frame, int(m.phase), int(m.wait), (long long)m.scoring.score, (long long)m.scoring.pool, m.incoming.pending, m.incoming.confirmed,
                        (long long)t.scoring.score, (long long)t.scoring.pool, t.incoming.pending, t.incoming.confirmed, int(t.wait), int(t.nextPair), int(t.wave.longestFall));
        }
        return pick;
    }
    // KNOWN2: the pair after the next two is not shown by 2P's own window before its pair appears (only the window of
    // the other side tells it), as for a choice made `lag` frames before the pair appears
    ai::Observation cut;
    const ai::Observation& knownTwo(const ai::Observation& o) {
        if (!std::getenv("KNOWN2")) return o;
        cut = o;
        const int keep = std::max(o.self().nextPair + 2, ai::shownPairs(o.opponent(), o.timing));
        if (cut.tsumo.count() > keep) cut.tsumo.pairs.resize(size_t(keep));
        return cut;
    }
    uint8_t frame(const ai::Observation& given) override {
        const ai::Observation& o = knownTwo(given);
        if (G_round) {
            const ai::Observation tr = ai::observe(*G_round, 1);
            const Player& mine = o.player[1];
            ++seen;
            if (mine.nextPair != tr.player[1].nextPair) ++indexOff;
            if (mine.phase == Phase::Control && tr.player[1].phase == Phase::Control && mine.nextPair == tr.player[1].nextPair &&
                (mine.colors.axis != tr.player[1].colors.axis || mine.colors.child != tr.player[1].colors.child)) ++colourOff;
            for (int k = 0; k < 3; ++k) {
                const int i = mine.nextPair + k;
                if (o.mine().known(i) && i < int(tr.tsumo.pairs.size()) && tr.mine().known(i)) {
                    ++pairsSeen;
                    const Pair x = o.mine().pair(i), y = tr.mine().pair(i);
                    if (x.axis != y.axis || x.child != y.child) ++pairsOff;
                }
            }
        }
        if (G_round) {
            const ai::Observation tr = ai::observe(*G_round, 1);
            const Player& m = o.player[1];
            const Player& q = tr.player[1];
            {
                const Player& a = o.player[0];
                const Player& b = tr.player[0];
                ++opp;
                if (a.phase != b.phase) ++oppPhase, oppTab[int(a.phase)][int(b.phase)]++;
                if (!(a.field == b.field)) ++oppField;
                if (a.incoming.total() != b.incoming.total()) ++oppIncoming;
                if (a.chain.chains != b.chain.chains) ++oppChain;
                if (a.scoring.score != b.scoring.score) ++oppScore;
                if (a.nextPair != b.nextPair) ++oppNext;
            }
            if (m.phase != Phase::Control || q.phase != Phase::Control) {
                ++between;
                if (m.phase != q.phase) ++phaseOff, phaseTab[int(m.phase)][int(q.phase)]++;
                else if (m.wait != q.wait) ++waitOff;
                if (!(m.field == q.field)) {
                    ++fieldOff;
                    if (std::getenv("SHOWDIFF") && fieldOff % 97 == 1) {
                        std::printf("frame %lld shown phase %d wait %d next %d | true phase %d wait %d next %d\n", (long long)tr.frame, int(m.phase), int(m.wait), int(m.nextPair), int(q.phase), int(q.wait), int(q.nextPair));
                        for (int y = 12; y >= 0; --y) {
                            std::printf("   ");
                            for (int x = 0; x < W; ++x) std::printf("%c", ".123456"[m.field.f.get(x, y)]);
                            std::printf("  ");
                            for (int x = 0; x < W; ++x) std::printf("%c", ".123456"[q.field.f.get(x, y)]);
                            std::printf("\n");
                        }
                    }
                }
                if (m.chain.chains != q.chain.chains) ++chainOff;
            }
        }
        if (!bits || !G_round) return ai::X3Agent::frame(o);
        ai::Observation t = o;
        const ai::Observation truth = ai::observe(*G_round, 1);
        if (bits & 1) t.player[0] = truth.player[0];
        if (bits & 2) t.player[1].incoming = truth.player[1].incoming;
        if (bits & 4) t.player[1].scoring = truth.player[1].scoring;
        if (bits & 8) t.frame = truth.frame;
        if (bits & 16) t.tsumo = truth.tsumo;
        if ((bits & 32) && t.player[1].phase == truth.player[1].phase) t.player[1].field = truth.player[1].field;
        if ((bits & 256) && truth.player[1].phase != Phase::Control && t.player[1].phase != Phase::Control) t.player[1] = truth.player[1];
        if (bits & 2048) {
            // the opponent as it will be `lag` frames on (no button of its: its chain and garbage go on)
            Round ahead = *G_round;
            const int lag = std::getenv("LAG") ? std::atoi(std::getenv("LAG")) : 5;
            PlayerEvents ev[2];
            for (int k = 0; k < lag && ahead.state == RoundState::Playing; ++k) {
                const uint8_t none[2] = {0, 0};
                ahead.step(none, ev);
            }
            t.player[0] = ai::observe(ahead, 1).player[0];
        }
        if (bits & 4096) {
            Player& m = t.player[1];
            const Player& q = truth.player[1];
            m.phase = q.phase, m.wait = q.wait, m.pair = q.pair, m.colors = q.colors, m.nextPair = q.nextPair, m.moves = q.moves;
        }
        if (bits & 8192) t.player[1].nextPair = truth.player[1].nextPair, t.player[1].moves = truth.player[1].moves;
        if (bits & 16384) t.player[1].wave = truth.player[1].wave, t.player[1].placed = truth.player[1].placed, t.player[1].buttons = truth.player[1].buttons;
        if (bits & 32768) {
            // own 13th and 14th rows as they are (while the round's field stands still - its pair in play or about to
            // appear - over the same twelve rows)
            GameField& g = t.player[1].field;
            const GameField& q = truth.player[1].field;
            const Phase ph = truth.player[1].phase;
            if ((ph == Phase::Control || ph == Phase::Next) && Follower::shown(g).f == Follower::shown(q).f)
                for (int x = 0; x < W; ++x) g.f.set(x, 12, q.f.get(x, 12)), g.row14[size_t(x)] = q.row14[size_t(x)];
        }
        if (bits & 1024) t.player[1] = truth.player[1], t.frame = truth.frame;
        if (bits & 64) t.player[1].field = truth.player[1].field, t.player[1].scoring = truth.player[1].scoring, t.player[1].incoming = truth.player[1].incoming, t.player[1].chain = truth.player[1].chain;
        return ai::X3Agent::frame(t);
    }
};
// strength <mode> <seed>: 2P (x3 narrow) against 1P (x3 narrow). mode 0: 2P plays the round itself; 1: through the
// screen and a pad of lag 5 (taps of a frame); 2: as 1 with Driver::prepare; 3: as 2, the driver not called in one
// frame of 7; 4: as 3, a pad a frame early or late in one frame of 8.
int main(int argc, char** argv) {
    const int mode = std::atoi(argv[1]);
    const uint64_t seed = std::strtoull(argv[2], nullptr, 10);
    const int frames = 12000;
    ai::Params narrow;
    narrow.beamWidth = 30, narrow.beamDepth = 8, narrow.beamQueues = 3;
    if (mode == 0 || mode >= 5) {
        const Rules rules;
        Timing timing;
        if (const char* q = std::getenv("QTL")) timing.quickTurnLock = std::atoi(q);
        Round r;
        r.start(rules, seed, 2, timing);
        // (HOLDSTART=N: the first pair of 2P appears N frames after that of 1P, and all of 2P's round with it)
        if (const char* h = std::getenv("HOLDSTART")) r.player[1].wait += std::atoi(h);
        auto a = ai::makeAgent(std::getenv("P1AI") ? std::getenv("P1AI") : "x3", forTwo(narrow, "P1PARAMS"));
        ai::Params seeing = forTwo(narrow);
        if (const char* e = std::getenv("SEELATE")) seeing.seeLate = std::atoi(e);
        std::unique_ptr<ai::Agent> b = std::make_unique<Oracle>(seeing);
        if (mode == 6 || mode == 7) Driver::prepare(b.get());
        if (std::getenv("FINALFIELD")) G_round = &r;
        a->startRound(0, seed), b->startRound(1, seed + 1);
        PlayerEvents e[2];
        int f = 0;
        const int delay = mode == 5 ? (std::getenv("LAG") ? std::atoi(std::getenv("LAG")) : 5) : 0;
        std::deque<uint8_t> late(size_t(delay), 0);
        // mode 7: the screen drawn and read in every frame; the AI is shown what Follower makes of it (VIEW bits, 7: all):
        // 1 the other side, 2 its own score and notice, 4 the pairs
        const Layout lay = sampleLayout();
        std::unique_ptr<Canvas> canvas;
        Follower follow;
        Rng noise(seed, 7);
        int64_t clock = 1000;
        const int view = std::getenv("VIEW") ? std::atoi(std::getenv("VIEW")) : 7;
        if (mode == 7) {
            canvas = std::make_unique<Canvas>(1280, 720, lay);
            for (int k = 0; k < 45; ++k, ++clock) draw(*canvas, lay, r, true, noise), follow.step(read(canvas->picture(), lay), clock);
        }
        for (; f < frames && r.state == RoundState::Playing; ++f) {
            ai::Observation seen = ai::observe(r, 1);
            if (mode == 7) {
                draw(*canvas, lay, r, false, noise, true);
                follow.step(read(canvas->picture(), lay), clock);
                ai::Observation screen;
                follow.observeAt(1, rules, r.player[1], clock++, screen);
                if (std::getenv("NOTICEDIFF")) {
                    // (NOTICEDIFF: 2P's own notice read from the screen against the round's)
                    static long n = 0, total = 0, split = 0, under = 0, over = 0, shown = 0;
                    const Incoming &a = screen.player[1].incoming, &b = seen.player[1].incoming;
                    ++n;
                    static long run = 0, longOff = 0, longExamples = 0;
                    if (a.total() != b.total()) ++total, under += a.total() < b.total(), over += a.total() > b.total();
                    else if (a.pending != b.pending) ++split;
                    run = a.total() != b.total() ? run + 1 : 0;
                    longOff += run >= 4;
                    if (run == 4 && std::getenv("NOTICELONG") && longExamples++ < 30)
                        std::printf("NL f %d shown %d+%d true %d+%d read %d phase1P %d phase2P %d\n", f, a.pending, a.confirmed, b.pending, b.confirmed,
                                    follow.tracker.side[1].notice, int(r.player[0].phase),
                                    int(r.player[1].phase));
                    static bool longRegistered = false;
                    if (!longRegistered) longRegistered = true, std::atexit([] { std::printf("NOTICE long %ld\n", longOff); });
                    if (a.total() != b.total() && shown < 40 && std::getenv("NOTICESHOW"))
                        ++shown, std::printf("ND f %d shown %d+%d true %d+%d phase1P %d chain1P %d phase2P %d\n", f, a.pending, a.confirmed, b.pending, b.confirmed, int(r.player[0].phase),
                                             int(r.player[0].chain.chains), int(r.player[1].phase));
                    static bool registered = false;
                    if (!registered) registered = true, std::atexit([] { std::printf("NOTICE frames %ld totalOff %ld under %ld over %ld splitOff %ld\n", n, total, under, over, split); });
                }
                if (std::getenv("VIEWDIFF")) {
                    // (VIEWDIFF: how the other side read from the screen differs from it, frame by frame)
                    static long n = 0, phase = 0, wait = 0, field = 0, chain = 0, score = 0, pool = 0, incoming = 0, next = 0, tab[8][8] = {};
                    const Player &a = screen.player[0], &b = seen.player[0];
                    ++n;
                    if (a.phase != b.phase) ++phase, ++tab[int(b.phase)][int(a.phase)];
                    else if (a.phase != Phase::Control && a.wait != b.wait) ++wait;
                    static long fieldBy[8] = {};
                    const bool fieldOff = !(Follower::shown(a.field).f == Follower::shown(b.field).f);
                    field += fieldOff, fieldBy[int(b.phase)] += fieldOff;
                    static bool byRegistered = false;
                    if (!byRegistered) byRegistered = true, std::atexit([] { for (int x = 0; x < 8; ++x) if (fieldBy[x]) std::printf("VDF true %d %ld\n", x, fieldBy[x]); });
                    if (fieldOff && std::getenv("VIEWDIFFSHOW") && fieldBy[int(b.phase)] % 400 == 1) {
                        std::printf("FIELD frame %d true phase %d wait %d | shown phase %d wait %d\n", f, int(b.phase), int(b.wait), int(a.phase), int(a.wait));
                        for (int y = 11; y >= 0; --y) {
                            std::printf("   ");
                            for (int x = 0; x < W; ++x) std::printf("%c", ".123456G"[std::min<int>(7, b.field.f.get(x, y))]);
                            std::printf("  ");
                            for (int x = 0; x < W; ++x) std::printf("%c", ".123456G"[std::min<int>(7, a.field.f.get(x, y))]);
                            std::printf("\n");
                        }
                    }
                    chain += a.chain.chains != b.chain.chains, score += a.scoring.score != b.scoring.score, pool += a.scoring.pool != b.scoring.pool;
                    incoming += a.incoming.pending != b.incoming.pending || a.incoming.confirmed != b.incoming.confirmed, next += a.nextPair != b.nextPair;
                    static bool registered = false;
                    if (!registered) registered = true, std::atexit([] {
                        std::printf("VD frames %ld phase %ld wait %ld field %ld chain %ld score %ld pool %ld incoming %ld next %ld\n", n, phase, wait, field, chain, score, pool, incoming, next);
                        for (int x = 0; x < 8; ++x) for (int y = 0; y < 8; ++y) if (tab[x][y]) std::printf("VDT true %d shown %d %ld\n", x, y, tab[x][y]);
                    });
                }
                if (view & 1) {
                    // (VFIX bits: parts of the other side as they are after all - 1 score, 2 notice, 4 phase and wait, 8 field,
                    //  16 chain and wave)
                    const Player truth = seen.player[0];
                    seen.player[0] = screen.player[0];
                    const int fix = std::getenv("VFIX") ? std::atoi(std::getenv("VFIX")) : 0;
                    Player& p = seen.player[0];
                    if (fix & 1) p.scoring = truth.scoring;
                    if (fix & 2) p.incoming = truth.incoming;
                    if (fix & 4) p.phase = truth.phase, p.wait = truth.wait, p.pair = truth.pair, p.colors = truth.colors, p.nextPair = truth.nextPair, p.moves = truth.moves;
                    if (fix & 8) p.field = truth.field;
                    if (fix & 16) p.chain = truth.chain, p.wave = truth.wave, p.placed = truth.placed;
                    // (VFIXPH bits: the other side all as it is while it is in these phases, bit 1 << Phase)
                    if (const char* ph = std::getenv("VFIXPH"))
                        if (std::atoi(ph) >> int(truth.phase) & 1) p = truth;
                }
                if (view & (2 | 8)) seen.player[1].scoring = screen.player[1].scoring;
                if (view & (2 | 16)) seen.player[1].incoming = screen.player[1].incoming;
                if (view & 4) seen.ownTsumo = screen.ownTsumo, seen.tsumo = screen.tsumo, seen.tsumo1 = screen.tsumo1;
            }
            uint8_t mine = b->frame(seen);
            if (delay) late.push_back(mine), mine = late.front(), late.pop_front();
            const uint8_t bs[2] = {a->frame(ai::observe(r, 0)), mine};
            r.step(bs, e);
            timePair(r.player[1], bs[1], e[1], f);
            notePair(r.player[1], e[1]);
            for (int s = 0; s < 2; ++s) G_maxChain[s] = std::max(G_maxChain[s], int(r.player[s].chain.chains)), G_locks[s] += e[s].locked, G_control[s] += r.player[s].phase == Phase::Control;
        if (std::getenv("PLACES")) for (int s = 0; s < 2; ++s) if (e[s].locked) std::printf("P%d %d %d f%d\n", s + 1, r.player[s].nextPair, placementId(r.player[s].pair.placement()), int(r.frame));
        }
        G_state = int(r.state), G_frames = f, G_final = r.player[0].field, G_final2 = r.player[1].field, G_round = nullptr;
        for (int s = 0; s < 2; ++s) G_score[s] = r.player[s].scoring.score;
        if (std::getenv("AGENTSTATS"))
            if (const auto* placing = dynamic_cast<const ai::PlacementAgent*>(b.get()))
                std::printf("AGENT revised %lld late %lld rerouted %lld steered %lld again %lld/%lld/%lld/%lld corrected 0\n", (long long)placing->revisedChoices,
                            (long long)placing->lateFrames, (long long)placing->rerouted, (long long)placing->steered, (long long)placing->againAsked,
                            (long long)placing->againBegun, (long long)placing->againSwitched, (long long)placing->againNoPath);
    } else {
        std::unique_ptr<ai::Agent> a;
        if (const char* e = std::getenv("ORACLE")) { auto o = std::make_unique<Oracle>(forTwo(narrow)); o->bits = std::atoi(e); a = std::move(o); } else a = ai::makeAgent("x3", forTwo(narrow));
        if (mode >= 2) Driver::prepare(a.get());
        // THINK=n: the choices of 2P take n frames as on a machine that thinks in real time - the second choice comes n
        // frames after its pair appeared (PlacementAgent::againLate), and the first one, begun `ahead` frames before the pair
        // appears, n - ahead frames after (lateScript) - the round going on meanwhile
        static std::vector<int> lateFor;
        if (const char* th = std::getenv("THINK"))
            if (auto* p = dynamic_cast<ai::PlacementAgent*>(a.get())) {
                const int think = std::atoi(th);
                p->againLate = think;
                for (int moves = 0; moves < 2000; ++moves) lateFor.push_back(moves), lateFor.push_back(std::max(0, think - p->ahead));
                p->lateScript = &lateFor;
            }
        int64_t unknownWaited = -1;
        DriveCounts n;
        const int lag = std::getenv("LAG") ? std::atoi(std::getenv("LAG")) : 5;
        const int hid = std::getenv("HID") ? std::atoi(std::getenv("HID")) : 2;
        int replans = -1;
        // (LIT=n: the screen of 2P lit in n frames of every 12; LITAS=n: one pair of 2P in three lit from 8 frames before it
        //  appears, for n frames - DriveScenario::lit, litAsAppears)
        const int lit = std::getenv("LIT") ? std::atoi(std::getenv("LIT")) : 0, litAs = std::getenv("LITAS") ? std::atoi(std::getenv("LITAS")) : 0;
        driveMatch(seed, n, {.lag = lag, .shaky = mode >= 4, .frames = frames, .startLag = lag + 1, .given = std::move(a), .lit = lit, .hidden = hid, .movedHidden = hid ? 1 : 0,
                             .quietLanding = true, .skipOneIn = mode >= 3 ? 7 : 0, .litAsAppears = litAs, .timed = timedRun()},
                   &replans, &unknownWaited);
        std::printf("misses %ld of %ld left %ld down %ld miscounted %ld untargeted %ld ", n.locks - n.hits, n.locks, n.leftAlone, n.downAtSpawn, n.miscounted, n.untargeted);
        const PairLog& q = G_pairs;
        std::printf("\npairs %ld first %ld lock %ld presses %ld fewest %ld retargeted %ld second %ld noway %ld other %ld holes %ld unknownWaited %lld otherRun %ld age %ld ticks %ld replans %d corrected %ld\n",
                    q.pairs, q.firstSum, q.lockSum, q.presses, q.fewest, q.retargeted, q.bySecond, q.byNoWay, q.byOther, q.holes, (long long)unknownWaited, q.otherRunTicks,
                    q.otherRunAge, q.ticks, replans, n.corrected);
        std::printf("timed pictures %ld followed %ld twice %ld back %ld next %ld missing %ld drives %ld fills %ld stalls %ld lost %ld taps turn %ld %ld %ld %ld move %ld %ld %ld %ld\n", n.pictures,
                    n.followed, n.twice, n.back, n.nextFrame, n.missing, n.drives, n.fills, n.stalls, n.lost, n.taps[0][0], n.taps[0][1], n.taps[0][2], n.taps[0][3], n.taps[1][0], n.taps[1][1],
                    n.taps[1][2], n.taps[1][3]);
    }
    if (std::getenv("FINALFIELD")) {
        // (FINALFIELD: the fields as the round ended, 1P then 2P, the 14th row first - kept as it ended: the round itself is
        //  gone by now)
        for (int y = 13; y >= 0; --y) {
            for (int s = 0; s < 2; ++s) {
                std::printf("   ");
                const GameField& g = s == 0 ? G_final : G_final2;
                for (int x = 0; x < W; ++x) {
                    const Cell c = y == 13 ? g.row14[size_t(x)] : g.f.get(x, y);
                    std::printf("%c", ".123456G"[std::min<int>(7, c)]);
                }
            }
            std::printf("\n");
        }
    }
    std::printf("timing pairs %ld wait %ld rest %ld hist", T_pairs, T_wait, T_rest); for (int i = 0; i < 12; ++i) std::printf(" %ld", T_hist[i]); std::printf("\n");
    std::printf("locks1P %d locks2P %d ctl1P %d ctl2P %d ", G_locks[0], G_locks[1], G_control[0], G_control[1]);
    std::printf("mode %d seed %llu state %d frames %d chain1P %d chain2P %d score1P %lld score2P %lld\n", mode, (unsigned long long)seed, G_state, G_frames, G_maxChain[0], G_maxChain[1],
                (long long)G_score[0], (long long)G_score[1]);
}
