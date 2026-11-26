#pragma once

// Included by ai_tests.cpp after r4_board_tests.h ("ai_tests r6 [name]" runs these alone).
//
//  RecordedSameColour   a same-colour pair whose recorded buttons go to the upper of two placements that make the same
//                       field: the reading keeps that placement (knownPlacements), with the recorded lock.
//  RecordedWithoutFastest  the buttons of a choice made in mid-air are read as they go on, without fastestWays too: the
//                       Duel, quietSetupFrames, Gaze, playOut with the buttons of every placement and quietLanding; and
//                       the agent tells the choice the buttons it goes on with.
//  AirRestSetting       Params::x3AirRest 0: the pair in mid-air and the same pair shown as the one about to appear are
//                       read with the same rest on the ground.
//  MissedRivalAgain     a search begun ahead and missed is counted among againCut, and with thinkFit 2 its width is
//                       noted where a choice played again searches it.
//  MissedRivalChoose    a recorded miss keeps the first choice when the choice is played again (choose).
//  OutlastBoundary      the earlier garbage settling the frame before the own chain is confirmed: one pair of his.
//  OutlastNoDrop        the frame of the earlier drop as prepareRival takes it, and none without one.
//  ConfirmGaze, ConfirmMidgame, ConfirmSplit  Plan::confirmFrame where the plans are made: Gaze, MidgameBeam,
//                       splitCandidates.
//  FoundationShared     two last placements on the same three pieces are not separate triggers.
//  FoundationAfterFall  the first wave after a prefix that clears is counted where the pieces have fallen to.
//  AirLanding           quietLanding of the pair in mid-air and of the same pair shown as the one about to appear.

constexpr bool r6Protected = false; // src-ex: the readings that are kept as they were without the cancel

static Observation r6ShownNext(const Observation& o) {
    Observation b = o;
    b.ownAir = true, b.ownAirPair = b.player[o.me].pair;
    b.player[o.me].phase = Phase::Next, b.player[o.me].wait = 1, --b.player[o.me].nextPair;
    b.busyLeft[o.me] = 0;
    return b;
}

// The buttons of a way to `target` on which the pair waits one frame first: a way slower than the straight one.
struct R6Way {
    int target = -1;
    int64_t lock = -1, check = -1;
    int soft = 0;
};
static bool r6SlowWay(const Player& p, int64_t frame, int target, const Timing& t, int speed, R6Way& way) {
    PairControl delayed = p.pair;
    stepPair(delayed, p.field, 0, t, speed);
    ai::InputPlanner planner;
    ai::InputPath tail, straight;
    if (delayed.locked || !planner.plan(p.field, delayed, placement(target), t, speed, tail)) return false;
    if (!planner.plan(p.field, p.pair, placement(target), t, speed, straight)) return false;
    std::vector<uint8_t> recorded{0};
    recorded.insert(recorded.end(), tail.buttons.begin(), tail.buttons.end());
    const int end = ai::FastestSearch::ends(p.field, p.pair, recorded, t, speed);
    const int straightEnd = ai::FastestSearch::ends(p.field, p.pair, straight.buttons, t, speed);
    if (end <= 0 || straightEnd <= 0 || end <= straightEnd) return false;
    way = {target, frame + int64_t(recorded.size()) - 1, frame + end - 1, tail.softDropRows};
    return true;
}
static void r6Mark(Observation& o, const R6Way& way) {
    o.ownWayTarget = way.target, o.ownWayLock = way.lock, o.ownWayCheck = way.check, o.ownWaySoft = way.soft;
}

static void testR6RecordedSameColour() {
    Rules rules;
    rules.cancel = true;
    const ai::Params params{};
    constexpr int HALF = PLACEMENTS / 2;
    int checked = 0;
    for (uint64_t seed = 1; seed < 2000 && checked < 6; ++seed) {
        Round r;
        r.start(rules, seed);
        Player& p = r.player[0];
        const Timing& t = r.timing;
        PlayerEvents e[2];
        const uint8_t none[2] = {0, 0};
        while (!aboutToSpawn(p)) r.step(none, e);
        r.step(none, e);
        r.step(none, e);
        if (p.phase != Phase::Control || p.pair.locked) continue;
        const Pair colors = r.tsumo.pair(p.nextPair - 1);
        if (colors.axis != colors.child) continue;
        const unsigned legal = legalPlacements(p.field);
        for (int upper = HALF; upper < PLACEMENTS && checked < 6; ++upper) {
            const int lower = upper - HALF;
            ai::InputPlanner planner;
            ai::InputPath there;
            if (!(legal >> upper & 1) || !(legal >> lower & 1) ||
                !planner.plan(p.field, p.pair, placement(lower), t, rules.speedPercent, there)) continue;
            R6Way way;
            if (!r6SlowWay(p, r.frame, upper, t, rules.speedPercent, way)) continue;
            const std::string at = " (seed " + std::to_string(seed) + ", placement " + std::to_string(upper) + ")";
            Observation inAir = ai::observe(r, 0);
            inAir.fastestRead[0] = Observation::FASTEST_OWN;
            r6Mark(inAir, way);
            for (const Observation& o : {inAir, r6ShownNext(inAir)}) {
                ai::Gaze gaze;
                ai::PlanSet set;
                gaze.plans(o, 0, true, 1, params, 0, set);
                const unsigned first = ai::knownPlacements(o.self().field, set, 0, params);
                check((set.firstReach >> upper & 1) && set.firstLock[upper] == way.lock,
                      "the recorded placement of a same-colour pair is not read with its recorded lock" + at);
                check((first >> upper & 1) && !(first >> lower & 1),
                      "the recorded placement of a same-colour pair gave way to the one that makes the same field" + at);
                check(std::any_of(set.plans.begin(), set.plans.end(), [&](const ai::Plan& s) { return s.id[0] == upper; }) &&
                          std::none_of(set.plans.begin(), set.plans.end(), [&](const ai::Plan& s) { return s.id[0] == lower; }),
                      "the plans of a same-colour pair do not start with the recorded placement" + at);
            }
            ++checked;
        }
    }
    check(checked >= 3, "the test of the recorded same-colour placement has too few cases");
    std::cout << "a same-colour pair keeps the placement its recorded buttons go to: " << checked << " cases\n";
}

static void testR6RecordedWithoutFastest() {
    Rules rules;
    rules.cancel = true;
    const ai::Params params{};
    int checked[2] = {0, 0};
    for (int i = 0; i < 2000 && (checked[0] < 20 || checked[1] < 20); ++i) {
        Round r;
        r.start(rules, uint64_t(i) + 1);
        Player& p = r.player[0];
        const Timing& t = r.timing;
        PlayerEvents e[2];
        const uint8_t none[2] = {0, 0};
        while (!aboutToSpawn(p)) r.step(none, e);
        for (int k = 0; k < 1 + i % 3; ++k) r.step(none, e);
        if (p.phase != Phase::Control || p.pair.locked) continue;
        const Pair colors = r.tsumo.pair(p.nextPair - 1);
        const unsigned legal = legalPlacements(p.field);
        const int target = int((uint64_t(i) * 7) % PLACEMENTS);
        if (!(legal >> target & 1)) continue;
        GameField g = p.field;
        const PlaceResult put = place(g, placement(target), colors);
        if (g.dead() || t.splitFrames(put.split, placement(target).x, put.lockY)) continue;
        R6Way way;
        if (!r6SlowWay(p, r.frame, target, t, rules.speedPercent, way)) continue;
        const Observation o = ai::observe(r, 0);
        for (int fastest : {1, 0}) {
            const std::string at = " (case " + std::to_string(i) + ", target " + std::to_string(target) +
                                    (fastest ? ", fastestWays)" : ", no fastestWays)");
            Observation slow = r6ShownNext(o);
            slow.fastestRead[0] = fastest ? Observation::FASTEST_OWN : 0;
            r6Mark(slow, way);
            ai::Duel d(slow);
            check(d.run(o.frame + 1) & 1, "the pair shown as the one about to appear did not start" + at);
            d.placeFast(0, placement(target), colors);
            check(d.player[0].lockFrame == way.lock && d.player[0].at == way.check,
                  "the Duel reads the recorded buttons by a faster new way: lock " + std::to_string(d.player[0].lockFrame) + " for " +
                      std::to_string(way.lock) + ", check " + std::to_string(d.player[0].at) + " for " + std::to_string(way.check) + at);
            ai::PlanSet shownNext, rival;
            shownNext.who = 0, rival.who = 1;
            shownNext.pair[0] = colors;
            shownNext.pairCount = shownNext.knownCount = 1;
            ai::InputPlanner planner;
            check(ai::quietSetupFrames(slow, shownNext, target, planner) == way.check - o.frame + t.nextFrames,
                  "the speed of a quiet placement reads the recorded buttons by a faster new way" + at);
            ai::Gaze gaze;
            ai::PlanSet reading;
            gaze.plans(slow, 0, true, 1, params, 0, reading);
            check((reading.firstReach >> target & 1) && reading.firstLock[target] == way.lock,
                  "Gaze reads the recorded buttons by a faster new way" + at);
            ai::Plan one;
            one.count = 1;
            one.id[0] = int8_t(target);
            int64_t offered = -2;
            ai::KnownReplay bounded{[] { return false; }};
            bounded.acceptPlacement = [&](const ai::Duel&, int who, Placement, Pair, int64_t, int64_t lock) {
                if (who == 0 && offered == -2) offered = lock;
                return true;
            };
            const ai::Duel exact = ai::playOut(slow, shownNext, &one, rival, nullptr, params, 0, true, nullptr, &bounded);
            check(offered == way.lock, "the exact reading offers the placement with the lock of a faster new way: " + std::to_string(offered) +
                                           " for " + std::to_string(way.lock) + at);
            check(exact.player[0].moves == o.self().moves + 1 && exact.player[0].lockFrame == way.lock,
                  "the exact reading (the buttons of every placement) does not take the recorded lock: " +
                      std::to_string(exact.player[0].lockFrame) + " for " + std::to_string(way.lock) + at);
            const auto landing = ai::setup_room::quietLanding(slow, colors, target, planner);
            check(landing && landing->frames == int(way.lock - o.frame + 1) && landing->check == int(way.check - o.frame + 1),
                  "quietLanding does not take the recorded buttons" + at);
            ++checked[fastest];
        }
    }
    check(checked[0] >= 20 && checked[1] >= 20, "the test of the recorded buttons without fastestWays has too few cases");

    // The agent: a choice made in mid-air without fastestWays is told the buttons the pair goes on with.
    const int far = placementId({0, UP});
    AirWayProbe probe;
    probe.ahead = 12, probe.fixed = far, probe.secondFixed = far;
    probe.sayAgain = true, probe.askInAir = true, probe.fastestWays = false;
    Round r;
    r.start(rules, 3);
    probe.startRound(0, 5);
    PlayerEvents e[2];
    for (int f = 0; f < 4000 && r.player[0].moves < 1; ++f) {
        Observation o = ai::observe(r, 0);
        if (o.self().phase == Phase::Next && o.tsumo.count() > o.self().nextPair + 1) o.tsumo.pairs.resize(size_t(o.self().nextPair + 1));
        const uint8_t buttons[2] = {probe.frame(o), 0};
        r.step(buttons, e);
    }
    check(r.player[0].moves == 1 && probe.secondSeen && probe.secondAir, "no second choice was asked for in mid-air without fastestWays");
    check(probe.secondWay == far && probe.secondCheck >= probe.secondFrame,
          "the second choice made in mid-air without fastestWays is not told the buttons the pair goes on with: target " +
              std::to_string(probe.secondWay));
    std::cout << "the recorded buttons of a pair in mid-air, without fastestWays too: " << checked[0] << " and " << checked[1] << " cases\n";
}

static void testR6AirRestSetting() {
    Rules rules;
    rules.cancel = true;
    ai::Params still, counted;
    still.x3AirRest = 0;
    int compared = 0, rested = 0;
    for (int i = 0; i < 400 && compared < 20; ++i) {
        Round r;
        r.start(rules, uint64_t(i) + 1);
        Player& p = r.player[0];
        for (int x = 0; x < W; ++x) addGarbage(p.field, x, x == DEATH_X ? 0 : int((uint64_t(i) * 5 + uint64_t(x) * 3) % 7));
        PlayerEvents e[2];
        const uint8_t none[2] = {0, 0};
        while (!aboutToSpawn(p)) r.step(none, e);
        r.step(none, e); // (the pair appears)
        for (int guard = 0; guard < 3000 && p.phase == Phase::Control && !p.pair.grounded; ++guard) r.step(none, e);
        for (int k = 0; k < 1 + i % 4 && p.phase == Phase::Control && !p.pair.locked; ++k) r.step(none, e);
        if (p.phase != Phase::Control || p.pair.locked || !p.pair.grounded) continue;
        const Observation o = ai::observe(r, 0), b = r6ShownNext(o);
        const std::string at = " (case " + std::to_string(i) + ")";
        ai::Gaze gaze;
        ai::PlanSet real, shown, withRest;
        gaze.plans(o, 0, true, 1, still, 0, real);
        gaze.plans(b, 0, true, 1, still, 0, shown);
        gaze.plans(b, 0, true, 1, counted, 0, withRest);
        check(real.firstReach == shown.firstReach, "the pair shown as the one about to appear reaches other placements" + at);
        for (int id = 0; id < PLACEMENTS; ++id) {
            if (!(real.firstReach >> id & 1)) continue;
            check(real.firstLock[id] == shown.firstLock[id] && real.firstRest[id] == shown.firstRest[id],
                  "x3AirRest 0: the pair shown as the one about to appear is read with another rest on the ground: " +
                      std::to_string(shown.firstRest[id]) + " for " + std::to_string(real.firstRest[id]) + " (placement " +
                      std::to_string(id) + ")" + at);
            rested += withRest.firstRest[id] > 0;
        }
        for (const ai::Plan& plan : real.plans)
            for (const ai::Plan& other : shown.plans)
                if (ai::samePlacements(plan, other))
                    check(plan.endFrame == other.endFrame, "x3AirRest 0: the plans of the two readings end in other frames" + at);
        ++compared;
    }
    check(compared >= 10 && rested > 0, "the test of x3AirRest 0 in mid-air has too few cases");
    std::cout << "x3AirRest 0: the pair in mid-air read alike as the pair shown as the one about to appear (" << compared << " cases)\n";
}

struct R6MissProbe : ai::X3Agent {
    using X3Agent::X3Agent;
    void miss(const Observation& o, bool again) {
        noting = true;
        beginChoice(o, again);
        gaze.plans(o, 1 - o.me, false, 1, p, 7, theirs);
        Rival rival;
        prepareRival(o, 7, rival, o.frame + 60);
        rival.prefetched = true;
        ai::Plan attack;
        attack.count = attack.fireDepth = 1;
        attack.sent = 12;
        attack.endFrame = o.frame + 60;
        choosingAgain = again;
        outlasts(o, 0, o.rules.rateAt(o.frame), 7, rival, &attack);
        choosingAgain = false;
    }
    void again(bool on) { choosingAgain = on; }
    using X3Agent::choose;
};

static void testR6MissedRivalAgain() {
    ai::Params p;
    p.thinkBudget = 30;
    p.checkKnownExchange = 0;
    p.thinkFit = 2;
    const auto o = ai::observe(situation(Rules{}, {}, {}, {1, 2}), 0);
    R6MissProbe timed(p);
    timed.startRound(0, 7);
    timed.miss(o, true);
    check(timed.rivalMissed == 1 && timed.againCut == 1, "a missed rival search of a second choice is not counted among againCut");
    check(timed.notes.size() == 1 && timed.notes.back().widths.size() == 1 && timed.notes.back().widths[0] > 0,
          "a rival search taken from the search begun ahead does not note its width for thinkFit 2");
    std::cout << "a missed rival search: againCut and the width noted\n";
}

static void testR6MissedRivalChoose() {
    ai::Params p;
    p.thinkBudget = 30;
    p.checkKnownExchange = 0;
    const auto o = ai::observe(situation(Rules{}, {}, {}, {1, 2}), 0);
    R6MissProbe timed(p);
    timed.startRound(0, 7);
    timed.miss(o, false);
    check(timed.notes.size() == 1, "the miss was not noted");
    const unsigned legal = legalPlacements(o.self().field);
    const int a = placementId({0, UP}), b = placementId({4, UP});
    check((legal >> a & 1) && (legal >> b & 1), "the placements of the test are not legal");
    const Pair pair = o.mine().pair(o.self().nextPair);
    p.thinkBudget = 0;
    std::vector<ai::X3Pace::Note> clean = timed.notes;
    clean.back().cut = 0;
    for (const bool missed : {false, true}) {
        R6MissProbe replay(p);
        replay.startRound(0, 7);
        replay.script = missed ? &timed.notes : &clean;
        check(replay.choose(o, pair, 1u << a) == a, "the first choice of the test was not the only placement offered");
        replay.again(true);
        const int second = replay.choose(o, pair, 1u << b);
        check(second == (missed ? a : b), missed ? "a recorded miss did not keep the first choice when the choice was made again"
                                                 : "without a miss the second choice does not stand (the test is out of date)");
    }
    std::cout << "a recorded miss keeps the first choice of a pair played again\n";
}

// The board of testR4OutlastAfterEarlierDrop: his pair after 30 pieces of garbage that settle as the own chain is fired.
struct R6DropBoard {
    Observation landing, dropping, calm;
    int64_t confirmed = -1;
    int reply = -1;
};
static R6DropBoard r6DropBoard(bool cancel, int me) {
    const int him = 1 - me;
    Rules rules;
    rules.targetPoints = 40;
    for (int i = 0; i < rules.marginSteps; ++i) rules.marginRates[size_t(i)] = 40;
    rules.cancel = cancel;
    rules.softDropBonus = false;
    GameField rivalField;
    for (int y = 0; y < 5; ++y) rivalField.f.set(1, y, GARBAGE);
    for (int y = 5; y < 8; ++y) rivalField.f.set(1, y, 4);
    GameField ownField;
    for (int y = 0; y < 3; ++y) ownField.f.set(5, y, 4);
    Round round = me ? situation(rules, rivalField, ownField, {4, 4}) : situation(rules, ownField, rivalField, {4, 4});
    round.timing.waveQuick = round.timing.waveBase = 24;
    round.player[him].phase = Phase::Land;
    round.player[him].wait = 1;
    R6DropBoard board;
    board.calm = ai::observe(round, me);
    round.player[him].incoming.confirmed = 30;
    board.landing = ai::observe(round, me);
    const Placement ownTarget{5, UP}, rivalTarget{2, UP};
    ai::InputPlanner planner;
    ai::InputPath paths[2];
    check(planner.plan(ownField, appearingPair(round.player[me], rules, round.timing), ownTarget, round.timing, rules.speedPercent, paths[me]),
          "r6 own attack has no button path");
    int cursor[2]{};
    int64_t spawned = -1;
    for (int frames = 0; frames < 450 && board.reply < 0 && round.state == RoundState::Playing; ++frames) {
        uint8_t buttons[2]{};
        const auto& opponent = round.player[him];
        if (aboutToSpawn(opponent))
            check(planner.plan(opponent.field, appearingPair(opponent, rules, round.timing), rivalTarget, round.timing, rules.speedPercent, paths[him]),
                  "r6 first pair after drop has no button path");
        for (int side : {0, 1}) if (cursor[side] < paths[side].frames()) buttons[side] = paths[side].buttons[cursor[side]++];
        PlayerEvents events[2];
        round.step(buttons, events);
        if (!frames) {
            check(events[him].garbageFell && round.player[him].phase == Phase::Garbage, "r6 previous garbage did not settle before the attack");
            board.dropping = ai::observe(round, me);
        }
        if (events[me].chainEnded && board.confirmed < 0) board.confirmed = round.frame - 1;
        if (events[him].spawned && spawned < 0) spawned = round.frame - 1;
        if (events[him].waveStarted) board.reply = round.player[him].wave.sent + round.player[him].wave.offsetted;
    }
    check(board.confirmed > board.landing.frame && board.reply == 2, "r6 drop board is out of date");
    return board;
}
static ai::Params r6DropParams() {
    ai::Params p;
    p.x2Depth = 2;
    p.beamWidth = 30;
    p.beamQueues = 3;
    p.x3OutlastSettled = 1;
    p.x3OutlastMean = 0;
    p.checkKnownExchange = 0;
    return p;
}
struct R6DropProbe : R4BoardProbe {
    using R4BoardProbe::R4BoardProbe;
    int64_t previousDropOf(const Observation& o) {
        read(o);
        Rival rival;
        prepareRival(o, 7, rival, o.frame + 600);
        return rival.previousDrop;
    }
};
static std::pair<bool, int> r6Evaluate(const ai::Params& p, const Observation& o, int64_t confirm) {
    R4BoardProbe probe(p);
    probe.startRound(o.me, 7);
    probe.read(o);
    ai::Plan shot;
    shot.count = shot.fireDepth = 1;
    shot.id[0] = int8_t(placementId({5, UP}));
    shot.confirmFrame = confirm;
    shot.endFrame = confirm + o.timing.nextFrames + 1;
    return probe.checkShot(o, shot);
}

static void testR6OutlastBoundary() {
    const ai::Params p = r6DropParams();
    for (bool cancel : {false, true}) for (int me : {0, 1}) {
        const R6DropBoard board = r6DropBoard(cancel, me);
        const auto expected = r6Protected && !cancel ? std::pair{true, -1} : std::pair{false, board.reply};
        // (the earlier drop: F = landing.frame, the frame before `dropping`; the own chain confirmed in F + 1)
        check(r6Evaluate(p, board.landing, board.landing.frame + 1) == expected,
              "r6 win test skipped the reply to a chain confirmed the frame after the previous drop (Land)");
        check(r6Evaluate(p, board.dropping, board.dropping.frame) == expected,
              "r6 win test skipped the reply to a chain confirmed the frame after the previous drop (Garbage)");
    }
    std::cout << "the reply after an earlier drop: a chain confirmed the frame after it\n";
}

static void testR6OutlastNoDrop() {
    const ai::Params p = r6DropParams();
    for (bool cancel : {false, true}) for (int me : {0, 1}) {
        const R6DropBoard board = r6DropBoard(cancel, me);
        R6DropProbe probe(p);
        probe.startRound(me, 7);
        check(probe.previousDropOf(board.landing) == board.landing.frame, "prepareRival does not take the frame the earlier garbage settles in (Land)");
        check(probe.previousDropOf(board.dropping) == board.dropping.frame - 1,
              "prepareRival does not take the frame the earlier garbage settled in (Garbage)");
        check(probe.previousDropOf(board.calm) == INT64_MAX, "prepareRival takes an earlier drop where none falls");
    }
    std::cout << "the frame of an earlier drop, and none without one\n";
}

// Plan::confirmFrame of the plans as they are made, against the game (Gaze) or the exact reading.
static void testR6ConfirmGaze() {
    Rules rules;
    rules.targetPoints = 40;
    for (int i = 0; i < rules.marginSteps; ++i) rules.marginRates[size_t(i)] = 40;
    for (bool cancel : {false, true}) {
        rules.cancel = cancel;
        GameField mine;
        for (int y = 0; y < 3; ++y) mine.f.set(5, y, 4);
        Round r = situation(rules, mine, GameField{}, {4, 4});
        r.player[0].incoming.confirmed = 5; // (the chain offsets two of them, three fall after it)
        const Observation o = ai::observe(r, 0);
        const Placement fire{5, UP};
        ai::InputPlanner planner;
        ai::InputPath path;
        check(planner.plan(mine, appearingPair(r.player[0], rules, r.timing), fire, r.timing, rules.speedPercent, path), "r6 confirm board has no path");
        int64_t confirm = -1;
        for (int frame = 0; frame < 600 && confirm < 0; ++frame) {
            const uint8_t buttons[2] = {frame < path.frames() ? path.buttons[size_t(frame)] : uint8_t(0), 0};
            PlayerEvents e[2];
            r.step(buttons, e);
            if (e[0].chainEnded) confirm = r.frame - 1;
        }
        check(confirm > 0, "the own chain of the confirm board does not end");
        ai::Params p;
        p.beamWidth = 30, p.beamDepth = 8, p.beamQueues = 3, p.x2Depth = 2;
        ai::Gaze gaze;
        ai::PlanSet set;
        gaze.plans(o, 0, true, 2, p, 7, set);
        const auto shot = std::find_if(set.plans.begin(), set.plans.end(), [&](const ai::Plan& s) {
            return s.fireDepth == 1 && s.id[0] == placementId(fire);
        });
        check(shot != set.plans.end(), "the confirm board has no chain fired with the pair in hand");
        check(shot->confirmFrame == confirm, "Gaze: the plan's chain is confirmed in frame " + std::to_string(shot->confirmFrame) +
                                                  ", the game's in " + std::to_string(confirm));
        check(shot->endFrame - 1 - o.timing.nextFrames > confirm, "no garbage falls on the own field after the chain (the test is out of date)");
    }
    std::cout << "Gaze: the frame a plan's chain is confirmed in, with garbage falling after it\n";
}

static void testR6ConfirmMidgame() {
    Rules rules;
    rules.targetPoints = 20;
    for (int i = 0; i < rules.marginSteps; ++i) rules.marginRates[size_t(i)] = 20;
    for (bool cancel : {false, true}) {
        rules.cancel = cancel;
        GameField mine;
        stairs(mine, 0, 2);
        mine.f.set(5, 0, Cell(3));
        for (int y = 0; y < 3; ++y) mine.f.set(2, y, Cell(3));
        const auto match = situation(rules, mine, GameField{}, Pair{4, 3});
        auto o = ai::observe(match, 0);
        o.tsumo.pairs = {{4, 3}, {4, 2}};
        const ai::Params p = ai::levelParams(ai::Params{}, 37);
        ai::Gaze gaze;
        ai::PlanSet own, rival;
        gaze.plans(o, 0, true, 3, p, 7, own);
        gaze.plans(o, 1, false, 3, p, 7, rival);
        ai::MidgameBeam::extend(o, p, 7, own, [] { return false; });
        int seen = 0;
        for (const ai::Plan& plan : own.tactical) {
            if (!plan.fireDepth || plan.fireDepth != plan.count) continue;
            // (played again as the beam plays it: placeFast, the other standing still)
            const ai::Duel again = ai::playOut(o, own, &plan, rival, nullptr, p, 7, false);
            if (again.player[0].moves != o.self().moves + plan.count || again.player[0].chain.chains != plan.chains) continue;
            check(plan.confirmFrame > 0 && plan.confirmFrame == again.player[0].settleFrame,
                  "MidgameBeam: the plan's chain is confirmed in frame " + std::to_string(plan.confirmFrame) + ", played again in " +
                      std::to_string(again.player[0].settleFrame));
            ++seen;
        }
        check(seen > 0, "the midgame board has no attack of the extra beam (the test is out of date)");
    }
    std::cout << "MidgameBeam: the frame a plan's chain is confirmed in\n";
}

struct R6SplitProbe : SplitProbe {
    using SplitProbe::SplitProbe;
    int checkConfirm(const ai::Observation& o) {
        prepare(o);
        const uint64_t seed = 1 + o.self().moves;
        int seen = 0;
        for (const ai::Plan& plan : splitCandidates(o, legalPlacements(o.self().field), seed)) {
            const ai::Duel quiet = ai::playOut(o, mine, &plan, theirs, nullptr, p, seed, true);
            check(plan.confirmFrame > 0 && plan.confirmFrame == quiet.player[o.me].settleFrame,
                  "splitCandidates: the plan's chain is confirmed in frame " + std::to_string(plan.confirmFrame) + ", the exact reading's in " +
                      std::to_string(quiet.player[o.me].settleFrame));
            ++seen;
        }
        return seen;
    }
};

static void testR6ConfirmSplit() {
    auto p = ai::levelParams(ai::agentParams("x3", ai::Params{}), 37);
    p.searchThreads = 1;
    p.beamWidth = 40;
    p.beamDepth = 10;
    p.beamQueues = 3;
    // (the board of testSplitMainAttack, and the same with his field and pairs on both sides)
    auto o = splitPosition();
    int seen = 0;
    for (int board = 0; board < 2; ++board) {
        if (board) {
            o.player[0].field = o.player[1].field;
            o.player[0].wait = o.player[1].wait;
            o.tsumo = o.tsumo1;
        }
        R6SplitProbe probe(p);
        probe.startRound(o.me, 1);
        seen += probe.checkConfirm(o);
    }
    check(seen > 0, "the split boards have no exact candidate (the test is out of date)");
    std::cout << "splitCandidates: the frame a plan's chain is confirmed in\n";
}

// The board of testR4FoundationAfterReshape, played to the last pair: the prefix clears four of colour 2.
struct R6Foundation {
    Observation o;
    ai::PlanSet mine;
    ai::Plan plan;
    Round round; // after the prefix, the last pair about to appear
};
static R6Foundation r6Foundation(bool cancel, Pair last, bool besideChild) {
    Rules rules;
    rules.cancel = cancel;
    GameField field;
    for (int y = 0; y < 3; ++y) {
        field.f.set(0, y, 1);
        field.f.set(2, y, 2);
        field.f.set(5, y, 1);
    }
    if (besideChild) field.f.set(1, 0, 3); // (beside the cell the child of the second pair falls to)
    R6Foundation b;
    b.round = situation(rules, field, {}, {1, 1});
    b.mine.who = 0;
    b.mine.knownCount = b.mine.pairCount = 3;
    b.mine.pair[0] = {4, 4};
    b.mine.pair[1] = {2, 3};
    b.mine.pair[2] = last;
    for (size_t i = 0; i < 3; ++i) {
        b.round.tsumo.piece[i * 2] = b.mine.pair[i].axis;
        b.round.tsumo.piece[i * 2 + 1] = b.mine.pair[i].child;
    }
    b.o = ai::observe(b.round, 0);
    const auto playToNext = [&](Round& r, Placement target) {
        ai::InputPlanner planner;
        ai::InputPath path;
        check(planner.plan(r.player[0].field, appearingPair(r.player[0], rules, r.timing), target, r.timing, rules.speedPercent, path),
              "r6 foundation fixture has no button path");
        const int moves = r.player[0].moves;
        for (int frame = 0; frame < 450; ++frame) {
            const uint8_t buttons[] = {frame < path.frames() ? path.buttons[size_t(frame)] : uint8_t(0), 0};
            PlayerEvents events[2];
            r.step(buttons, events);
            if (r.player[0].moves == moves + 1 && aboutToSpawn(r.player[0])) return;
        }
        check(false, "r6 foundation fixture never finished the placement");
    };
    playToNext(b.round, {3, UP});
    playToNext(b.round, {2, UP});
    check(b.round.player[0].field.f.get(2, 0) == 3 && b.round.player[0].field.f.get(2, 1) == 0,
          "r6 foundation fixture did not clear the four of colour 2 and let the child fall");
    b.plan.count = b.plan.fireDepth = 3;
    b.plan.id[0] = int8_t(placementId({3, UP}));
    b.plan.id[1] = int8_t(placementId({2, UP}));
    return b;
}
// The pieces already on the field that the first wave of the last placement clears, in the game.
static FieldBits r6GameSupport(const R6Foundation& b, Placement last) {
    GameField placed = b.round.player[0].field;
    place(placed, last, b.mine.pair[2]);
    Wave wave;
    check(findWaveFor(placed.f, wave, b.o.rules.clearCount), "r6 foundation: the last placement does not fire");
    return wave.cleared & b.round.player[0].field.f.normal();
}

static void testR6FoundationShared() {
    for (bool cancel : {false, true}) {
        R6Foundation b = r6Foundation(cancel, {1, 1}, false);
        const Placement corner{0, UP}, beside{1, UP};
        check(r6GameSupport(b, corner) == r6GameSupport(b, beside), "r6 foundation: the two last placements do not share their pieces in the game");
        b.plan.id[2] = int8_t(placementId(corner));
        const FieldBits one = ai::lv38_foundation::firstSupport(b.o, b.mine, b.plan);
        b.plan.id[2] = int8_t(placementId(beside));
        const FieldBits two = ai::lv38_foundation::firstSupport(b.o, b.mine, b.plan);
        check(!one.disjoint(two), "r6 foundation took two last placements on the same three pieces for separate triggers");
        if (!(r6Protected && !cancel))
            check(one == r6GameSupport(b, corner) && two == r6GameSupport(b, beside), "r6 foundation: the shared pieces are not those of the game");
    }
    std::cout << "the foundation: two triggers on the same pieces are not separate\n";
}

static void testR6FoundationAfterFall() {
    for (bool cancel : {false, true}) {
        R6Foundation b = r6Foundation(cancel, {3, 3}, true);
        const Placement last{1, UP};
        const FieldBits game = r6GameSupport(b, last);
        check(game == (FieldBits::cell(1, 0) | FieldBits::cell(2, 0)), "r6 foundation: the fallen child is not part of the first wave in the game");
        b.plan.id[2] = int8_t(placementId(last));
        if (!(r6Protected && !cancel))
            check(ai::lv38_foundation::firstSupport(b.o, b.mine, b.plan) == game,
                  "r6 foundation does not count the first wave where the pieces have fallen to after the prefix");
    }
    std::cout << "the foundation: the first wave after a prefix that clears, where the pieces have fallen to\n";
}

// quietLanding of the pair in mid-air (Control) and of the same pair shown as the one about to appear.
static void testR6AirLanding() {
    Rules rules;
    rules.cancel = true;
    int compared = 0;
    for (int i = 0; i < 600 && compared < 40; ++i) {
        Round r;
        r.start(rules, uint64_t(i) + 1);
        Player& p = r.player[0];
        for (int x = 0; x < W; ++x) addGarbage(p.field, x, x == DEATH_X ? 0 : int((uint64_t(i) * 3 + uint64_t(x) * 5) % 9));
        PlayerEvents e[2];
        const uint8_t none[2] = {0, 0};
        while (!aboutToSpawn(p)) r.step(none, e);
        const uint8_t aside = i % 2 ? uint8_t(BTN_LEFT) : uint8_t(BTN_RIGHT);
        for (int k = 0; k < 4 + i % 9 && !p.pair.locked; ++k) {
            const uint8_t buttons[2] = {uint8_t(k % 2 ? 0 : aside), 0};
            r.step(buttons, e);
        }
        if (p.phase != Phase::Control || p.pair.locked) continue;
        const Pair colors = r.tsumo.pair(p.nextPair - 1);
        const Observation o = ai::observe(r, 0), b = r6ShownNext(o);
        ai::InputPlanner planner;
        bool differsFromSpawn = false;
        for (int id = 0; id < PLACEMENTS; ++id) {
            if (!(legalPlacements(p.field) >> id & 1)) continue;
            const auto real = ai::setup_room::quietLanding(o, colors, id, planner);
            const auto shown = ai::setup_room::quietLanding(b, colors, id, planner);
            check(real.has_value() == shown.has_value() &&
                      (!real || (real->frames == shown->frames && real->check == shown->check && real->soft == shown->soft && real->field == shown->field)),
                  "quietLanding reads the pair shown as the one about to appear otherwise than the pair in mid-air (case " + std::to_string(i) +
                      ", placement " + std::to_string(id) + ")");
            Observation spawnNext = o;
            spawnNext.player[0].phase = Phase::Next, spawnNext.player[0].wait = 1, --spawnNext.player[0].nextPair;
            const auto fromSpawn = ai::setup_room::quietLanding(spawnNext, colors, id, planner);
            differsFromSpawn |= real.has_value() != fromSpawn.has_value() || (real && real->frames != fromSpawn->frames);
        }
        compared += differsFromSpawn;
    }
    check(compared >= 10, "the test of quietLanding in mid-air has too few cases that differ from a pair that appears");
    std::cout << "quietLanding: the pair shown as the one about to appear as the pair in mid-air (" << compared << " cases)\n";
}

static void testR6Changes(const std::string& only = {}) {
    const std::pair<const char*, void (*)()> tests[] = {
        {"RecordedSameColour", testR6RecordedSameColour},   {"RecordedWithoutFastest", testR6RecordedWithoutFastest},
        {"AirRestSetting", testR6AirRestSetting},           {"MissedRivalAgain", testR6MissedRivalAgain},
        {"MissedRivalChoose", testR6MissedRivalChoose},     {"OutlastBoundary", testR6OutlastBoundary},
        {"OutlastNoDrop", testR6OutlastNoDrop},             {"ConfirmGaze", testR6ConfirmGaze},
        {"ConfirmMidgame", testR6ConfirmMidgame},           {"ConfirmSplit", testR6ConfirmSplit},
        {"FoundationShared", testR6FoundationShared},       {"FoundationAfterFall", testR6FoundationAfterFall},
        {"AirLanding", testR6AirLanding},
    };
    bool ran = false;
    for (const auto& [name, test] : tests)
        if (only.empty() || only == name) test(), ran = true;
    check(ran, "no round 6 test is named " + only);
    if (only.empty()) std::cout << "round 6 regression tests passed\n";
}
