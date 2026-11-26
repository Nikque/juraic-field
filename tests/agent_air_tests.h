#pragma once

// Included by ai_tests.cpp before main(), after its helpers (check, aboutToSpawn) and ThinkProbe.
//
//  (a) testDuelOwnSeatBeforeAppears: the own seat with the pair in mid-air taken for the pair about to appear, as
//      PlacementAgent::beforeItAppears has it for a choice made in mid-air (the seat shown as Next, Observation::ownAir
//      and ownAirPair where the pair is, ownWayTarget and ownWayCheck the buttons it goes on with). The Duel reads the
//      next pair never before the Round's, and in the frame the Duel of the observation in mid-air reads it whenever
//      that one reads it as the Round plays it.
//  (b) testAskInAirWay: a choice made in mid-air (askInAir) with fastestWays, through PlacementAgent::frame, is told that
//      the pair is in mid-air and the buttons it goes on with.
//  (c) testNearestWayOverSoonest: a pair whose choice came when it had fallen behind a column goes to the nearest
//      placement it still reaches by the way a chosen placement gets (InputPlanner::planSure): with fastestWays, the way
//      over soonest (PlacementAgent::nearest).

static void testDuelOwnSeatBeforeAppears() {
    Rng rng(20261124, 11);
    Rules rules;
    rules.cancel = true;
    ai::InputPlanner soonest;
    soonest.fastest = true;
    ai::InputPath path;
    int cases = 0, matched = 0, others = 0;
    bool checkedRecorded = false;
    for (int i = 0; i < 2000; ++i) {
        Round r;
        r.start(rules, uint64_t(i) + 1);
        const Timing& t = r.timing;
        Player& p = r.player[0];
        for (int x = 0; x < W; ++x)
            addGarbage(p.field, x, x == DEATH_X ? int(rng.below(9)) : int(rng.below(i % 2 ? 13 : 9)));
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
        if (!soonest.planSure(p.field, spawnPair(t), target, t, rules.speedPercent, path) || path.frames() < 3) continue;
        GameField closed = p.field;
        const PlaceResult drop = place(closed, target, colors);
        if (closed.dead() || t.splitFrames(drop.split, target.x, drop.lockY)) continue;
        const std::string at = " (case " + std::to_string(i) + ", target " + std::to_string(placementId(target)) + ")";
        // Some frames of the way played first: the pair is in mid-air.
        const int ahead = 1 + int(rng.below(uint64_t(std::min(6, path.frames() - 2))));
        int frames = 0;
        for (; frames < ahead; ++frames) {
            const uint8_t buttons[2] = {path.buttons[size_t(frames)], 0};
            r.step(buttons, e);
            if (e[0].locked) break;
        }
        if (p.phase != Phase::Control || p.pair.locked) continue;
        Observation o = ai::observe(r, 0);
        o.fastestRead[0] = Observation::FASTEST_OWN;
        const std::vector<uint8_t> rest(path.buttons.begin() + frames, path.buttons.end());
        const int end = ai::FastestSearch::ends(p.field, p.pair, rest, t, rules.speedPercent);
        if (end <= 0) continue;
        o.ownWayTarget = placementId(target), o.ownWayCheck = o.frame + end - 1;
        // (the observation in mid-air as beforeItAppears makes it)
        Observation b = o;
        b.ownAir = true, b.ownAirPair = b.player[0].pair;
        b.player[0].phase = Phase::Next, b.player[0].wait = 1, --b.player[0].nextPair;
        b.busyLeft[0] = 0;
        ai::Duel d(o), before(b);
        d.tallPaths = before.tallPaths = 1;
        for (int k = 0; k < 2; ++k) d.garbageRng[k] = before.garbageRng[k] = r.garbageRng[k];
        // Another placement than the one the buttons are on their way to: read from where the pair is, as the Duel of the
        // observation in mid-air reads it with the straight way from there (its lock, its points, its next pair), whether
        // the way over soonest is shorter or not; never before the Round that plays the way over soonest there.
        int other = -1;
        for (int k = 1; k < n && other < 0; ++k) {
            const int id = ids[(k + i) % n];
            GameField g = p.field;
            const PlaceResult put = place(g, placement(id), colors);
            ai::InputPath there;
            if (id != placementId(target) && !g.dead() && !t.splitFrames(put.split, placement(id).x, put.lockY) &&
                ai::InputPlanner{}.plan(p.field, p.pair, placement(id), t, rules.speedPercent, there))
                other = id;
        }
        if (other >= 0) {
            ai::Duel d2(o), before2(b);
            d2.tallPaths = before2.tallPaths = 1;
            for (int k = 0; k < 2; ++k) d2.garbageRng[k] = before2.garbageRng[k] = r.garbageRng[k];
            ai::InputPlanner straight2;
            ai::InputPath way2;
            check(straight2.plan(p.field, p.pair, placement(other), t, rules.speedPercent, way2), "no way to the other placement" + at);
            PairControl q2 = p.pair;
            for (const uint8_t button : way2.buttons) stepPair(q2, p.field, button, t, rules.speedPercent);
            d2.place(0, placement(other), colors, o.frame + way2.frames() - 1, way2.softDropRows, restedFrames(q2, t));
            check(before2.run(o.frame + 1) & 1, "the pair taken for the one about to appear did not appear at once" + at);
            before2.placeFast(0, placement(other), colors);
            ai::toPairOf(d2, 0, o.frame + 6000);
            ai::toPairOf(before2, 0, o.frame + 6000);
            const std::string there = at + " (other placement " + std::to_string(other) + ")";
            check(before2.player[0].spawnFrame == d2.player[0].spawnFrame && before2.player[0].scoring.score == d2.player[0].scoring.score,
                  "another placement of the pair taken for the one about to appear is not read from where it is in mid-air: next pair " +
                      std::to_string(before2.player[0].spawnFrame) + " for " + std::to_string(d2.player[0].spawnFrame) + ", score " +
                      std::to_string(before2.player[0].scoring.score) + " for " + std::to_string(d2.player[0].scoring.score) + there);
            // (the Round, the pair going there by the way over soonest from where it is)
            Round air = r;
            ai::InputPath soonestThere;
            if (soonest.planSure(p.field, p.pair, placement(other), t, rules.speedPercent, soonestThere)) {
                PlayerEvents ae[2];
                int64_t airNext = -1;
                for (int step = 0; airNext < 0 && step < 5000; ++step) {
                    const uint8_t buttons[2] = {step < soonestThere.frames() ? soonestThere.buttons[size_t(step)] : uint8_t(0), 0};
                    air.step(buttons, ae);
                    if (ae[0].spawned) airNext = air.frame - 1;
                }
                check(airNext >= 0 && before2.player[0].spawnFrame >= airNext,
                      "another placement of the pair taken for the one about to appear is read sooner than it plays: frame " +
                          std::to_string(airNext) + ", duel " + std::to_string(before2.player[0].spawnFrame) + there);
            }
            // The readings that take the buttons of every placement (playOut with knownPaths) and the speed of a quiet
            // placement (quietSetupFrames): the same from the pair shown as the one about to appear as from the pair in
            // mid-air - its lock, its points, the frames to the next pair.
            {
                ai::PlanSet inAir, shownNext, rival;
                inAir.who = shownNext.who = 0, rival.who = 1;
                inAir.pair[0] = shownNext.pair[0] = colors;
                inAir.pairCount = shownNext.pairCount = inAir.knownCount = shownNext.knownCount = 1;
                inAir.midAir = true;
                ai::Plan one;
                one.count = 1;
                one.id[0] = int8_t(other);
                const ai::Params params{};
                const ai::Duel byAir = ai::playOut(o, inAir, &one, rival, nullptr, params, 0, true);
                const ai::Duel byNext = ai::playOut(b, shownNext, &one, rival, nullptr, params, 0, true);
                check(byNext.player[0].moves == byAir.player[0].moves && byNext.player[0].lockFrame == byAir.player[0].lockFrame &&
                          byNext.player[0].scoring.score == byAir.player[0].scoring.score,
                      "the reading with the buttons of every placement takes the pair shown as the one about to appear from where a "
                      "pair appears: lock " + std::to_string(byNext.player[0].lockFrame) + " for " + std::to_string(byAir.player[0].lockFrame) +
                          ", score " + std::to_string(byNext.player[0].scoring.score) + " for " + std::to_string(byAir.player[0].scoring.score) + there);
                Observation plainAir = o, plainNext = b;
                plainAir.fastestRead[0] = plainNext.fastestRead[0] = 0;
                ai::Duel ordinary(plainNext);
                check(ordinary.run(o.frame + 1) & 1, "the ordinary reading did not start the pair" + there);
                ordinary.placeFast(0, placement(other), colors);
                check(ordinary.player[0].lockFrame == o.frame + way2.frames() - 1 &&
                          ordinary.player[0].scoring.score == p.scoring.score + (rules.softDropBonus ? way2.softDropRows : 0),
                      "ordinary input used the spawn position for a pair already in the air" + there);
                const ai::Duel plain = ai::playOut(plainAir, inAir, &one, rival, nullptr, params, 0, true);
                const ai::Duel plausible = ai::playOut(plainNext, shownNext, &one, rival, nullptr, params, 0, true);
                Observation reverse = plainNext;
                reverse.ownAirSeat = 0;
                reverse.me = 1;
                const ai::Duel reversed = ai::playOut(reverse, rival, nullptr, shownNext, &one, params, 0, true);
                for (const ai::Duel* exact : {&plausible, &reversed}) {
                    check(exact->player[0].moves == plain.player[0].moves &&
                              exact->player[0].lockFrame == plain.player[0].lockFrame &&
                              exact->player[0].spawnFrame == plain.player[0].spawnFrame &&
                              exact->player[0].field == plain.player[0].field &&
                              exact->player[1].moves == plain.player[1].moves,
                          "the exact reading did not keep the ordinary pair at its original seat and frame" + there);
                }
                ai::InputPlanner planner;
                const int quietAir = ai::quietSetupFrames(o, inAir, other, planner), quietNext = ai::quietSetupFrames(b, shownNext, other, planner);
                check(quietNext == quietAir, "the speed of a quiet placement takes the pair shown as the one about to appear from where a pair "
                                             "appears: " + std::to_string(quietNext) + " frames for " + std::to_string(quietAir) + there);
                if (!checkedRecorded && quietNext >= 0) {
                    PairControl delayed = p.pair;
                    stepPair(delayed, p.field, 0, t, rules.speedPercent);
                    ai::InputPath tail;
                    if (!delayed.locked && planner.plan(p.field, delayed, placement(other), t, rules.speedPercent, tail)) {
                        std::vector<uint8_t> recorded{0};
                        recorded.insert(recorded.end(), tail.buttons.begin(), tail.buttons.end());
                        const int checkFrame = ai::FastestSearch::ends(p.field, p.pair, recorded, t, rules.speedPercent);
                        const int64_t atCheck = o.frame + checkFrame - 1;
                        if (checkFrame > 0 && atCheck > o.frame + quietNext - t.nextFrames) {
                            Observation slow = b;
                            slow.ownWayTarget = other;
                            slow.ownWayLock = o.frame + int64_t(recorded.size()) - 1;
                            slow.ownWayCheck = atCheck;
                            slow.ownWaySoft = tail.softDropRows;
                            ai::Duel recordedDuel(slow);
                            check(recordedDuel.run(o.frame + 1) & 1, "the recorded pair did not start" + there);
                            recordedDuel.placeFast(0, placement(other), colors);
                            check(recordedDuel.player[0].lockFrame == slow.ownWayLock &&
                                      recordedDuel.player[0].at == atCheck &&
                                      ai::quietSetupFrames(slow, shownNext, other, planner) ==
                                          atCheck - o.frame + t.nextFrames,
                                  "a valid slower recorded path was replaced by a faster new path" + there);
                            ai::Gaze gaze;
                            ai::PlanSet reading;
                            gaze.plans(slow, 0, true, 1, params, 0, reading);
                            check((reading.firstReach >> other & 1) && reading.firstLock[other] == slow.ownWayLock,
                                  "Gaze lost the recorded first lock" + there);
                            checkedRecorded = true;
                        }
                    }
                }
            }
            ++others;
        }
        // (the Duel of the observation in mid-air: the rest of the straight way from where the pair is, as Gaze has it)
        ai::InputPlanner straight;
        ai::InputPath way;
        if (!straight.plan(p.field, p.pair, target, t, rules.speedPercent, way)) continue;
        PairControl q = p.pair;
        for (const uint8_t button : way.buttons) stepPair(q, p.field, button, t, rules.speedPercent);
        d.place(0, target, colors, o.frame + way.frames() - 1, way.softDropRows, restedFrames(q, t));
        int64_t roundNext = -1, duelNext = -1, beforeNext = -1;
        bool placed = false;
        for (int step = 0; roundNext < 0 && step < 5000; ++step, ++frames) {
            const uint8_t buttons[2] = {frames < path.frames() ? path.buttons[size_t(frames)] : uint8_t(0), 0};
            r.step(buttons, e);
            const int appeared = duelNext < 0 ? d.run(r.frame) : 0;
            const int appearedBefore = beforeNext < 0 ? before.run(r.frame) : 0;
            if (e[0].spawned) roundNext = r.frame - 1;
            if (appeared & 1) duelNext = r.frame - 1;
            if (!placed) {
                check(step == 0 && (appearedBefore & 1), "the pair taken for the one about to appear did not appear at once" + at);
                before.placeFast(0, target, colors);
                placed = true;
            } else if (appearedBefore & 1) {
                beforeNext = r.frame - 1;
            }
        }
        check(roundNext >= 0, "the move did not end" + at);
        if (duelNext < 0) d.run(roundNext + 400), duelNext = d.player[0].spawnFrame;
        if (beforeNext < 0) before.run(roundNext + 400), beforeNext = before.player[0].spawnFrame;
        check(beforeNext >= roundNext, "the pair taken for the one about to appear is read sooner than it plays: frame " +
                                           std::to_string(roundNext) + ", duel " + std::to_string(beforeNext) + at);
        if (duelNext == roundNext) {
            ++matched;
            check(beforeNext == duelNext, "the pair taken for the one about to appear is not read from where it is in mid-air: frame " +
                                              std::to_string(roundNext) + ", duel " + std::to_string(beforeNext) + at);
        }
        ++cases;
    }
    check(cases > 500 && matched > 200 && others > 500, "the test of the pair taken for the one about to appear has too few cases");
    check(checkedRecorded, "no valid slower recorded path was checked");

    Round wall;
    wall.start(rules, 9);
    Observation sealed = ai::observe(wall, 0);
    Player& self = sealed.player[0];
    addGarbage(self.field, 1, 13);
    self.phase = Phase::Next;
    self.wait = 1;
    sealed.ownAir = true;
    sealed.ownAirPair.x = 0;
    sealed.ownAirPair.y = 1;
    sealed.ownAirPair.upper = false;
    sealed.ownAirPair.grounded = true;
    const Placement across = placement(W - 1);
    const int id = placementId(across);
    check((legalPlacements(self.field) >> id & 1) &&
              ai::tallSpawnInput(self.field, across, sealed.timing, rules.speedPercent).frames > 0,
          "the wall setup must allow the destination from spawn");
    ai::Duel blocked(sealed);
    check(blocked.run(sealed.frame + 1) & 1, "the ordinary air pair did not start");
    blocked.placeFast(0, across, {1, 2});
    check(blocked.player[0].moves == self.moves && blocked.player[0].field == self.field,
          "an unreachable air placement was read from the spawn position");
    std::cout << "duel of the own seat, the pair in mid-air taken for the one about to appear: " << cases << " moves, " << matched
              << " read in the frame of the round, " << others << " to another placement\n";
}

// (b) The second choice of the pair, made in mid-air, as the choice sees it.
struct AirWayProbe : ThinkProbe {
    bool secondSeen = false, secondAir = false;
    int secondWay = -2;
    int64_t secondCheck = -2, secondFrame = -1;
    unsigned secondLegal = 0;   // the placements the second choice is offered
    PairControl secondPair{};   // ... and where the pair is (Observation::ownAirPair)
    GameField secondField;
    int choose(const Observation& o, Pair pair, unsigned legal) override {
        if (choosingAgain && !secondSeen) {
            secondSeen = true;
            secondAir = o.ownAir && o.self().phase == Phase::Next;
            secondWay = o.ownWayTarget, secondCheck = o.ownWayCheck, secondFrame = o.frame;
            secondLegal = legal, secondPair = o.ownAirPair, secondField = o.self().field;
        }
        return ThinkProbe::choose(o, pair, legal);
    }
};
static void testAskInAirWay() {
    Rules rules;
    rules.cancel = true;
    const int far = placementId({0, UP});
    AirWayProbe probe;
    probe.ahead = 12, probe.fixed = far, probe.secondFixed = far;
    probe.sayAgain = true, probe.askInAir = true, probe.fastestWays = true, probe.fastestStates = 4000;
    Round r;
    r.start(rules, 3);
    probe.startRound(0, 5);
    PlayerEvents e[2];
    for (int f = 0; f < 4000 && r.player[0].moves < 1; ++f) {
        Observation o = ai::observe(r, 0);
        // (before the pair appears only that pair is shown, as a reader of another program may show it; once it is in
        //  the air, the pairs after it too: a pair the first choice did not know - the second choice is asked in the air)
        if (o.self().phase == Phase::Next && o.tsumo.count() > o.self().nextPair + 1) o.tsumo.pairs.resize(size_t(o.self().nextPair + 1));
        const uint8_t buttons[2] = {probe.frame(o), 0};
        r.step(buttons, e);
    }
    check(r.player[0].moves == 1, "the pair of the test of a choice in mid-air was not put");
    check(probe.secondSeen, "no second choice was asked for in mid-air");
    check(probe.secondAir, "the second choice made in mid-air is not told that the pair is in mid-air");
    check(probe.secondWay == far && probe.secondCheck >= probe.secondFrame,
          "the second choice made in mid-air is not told the buttons the pair goes on with: target " + std::to_string(probe.secondWay) +
              ", check " + std::to_string(probe.secondCheck) + ", frame " + std::to_string(probe.secondFrame));
    std::cout << "a choice made in mid-air is told where the pair is and the buttons it goes on with\n";
}

// (b2) A choice made in mid-air is offered only the placements the pair still reaches from where it is, on a field
//      lower than eleven too: the second column ten high, the pair on its way down the sixth when the pair after it is
//      shown - the first column is behind the second.
static void testAskInAirReach() {
    Rules rules;
    rules.cancel = true;
    const int far = placementId({5, UP});
    AirWayProbe probe;
    probe.ahead = 12, probe.fixed = far, probe.secondFixed = far;
    probe.sayAgain = true, probe.askInAir = true, probe.fastestWays = true, probe.fastestStates = 4000;
    probe.pathCheck = true; // (as X3 with Params::x3PathCheck)
    Round r;
    r.start(rules, 3);
    addGarbage(r.player[0].field, 1, 10);
    probe.startRound(0, 5);
    PlayerEvents e[2];
    for (int f = 0; f < 4000 && r.player[0].moves < 1; ++f) {
        Observation o = ai::observe(r, 0);
        // (the pair after it shown only once the pair is below the top of the second column)
        const Player& self = o.self();
        const bool low = self.phase == Phase::Control && self.pair.y < 9;
        const int shown = self.phase == Phase::Control ? self.nextPair : self.nextPair + 1;
        if (!low && o.tsumo.count() > shown) o.tsumo.pairs.resize(size_t(shown));
        const uint8_t buttons[2] = {probe.frame(o), 0};
        r.step(buttons, e);
    }
    check(r.player[0].moves == 1, "the pair of the test of a choice in mid-air was not put");
    check(probe.secondSeen && probe.secondAir, "no second choice was asked for in mid-air, below the top of the second column");
    ai::InputPlanner plain;
    ai::InputPath way;
    const unsigned legal = legalPlacements(probe.secondField);
    unsigned reached = 0;
    for (int id = 0; id < PLACEMENTS; ++id)
        if ((legal >> id & 1) && plain.plan(probe.secondField, probe.secondPair, placement(id), r.timing, rules.speedPercent, way)) reached |= 1u << id;
    check(reached != legal, "the test of a choice in mid-air expects placements the pair no longer reaches");
    check(probe.secondLegal == reached, "a choice made in mid-air is offered placements the pair no longer reaches: offered " +
                                            std::to_string(probe.secondLegal) + ", reached " + std::to_string(reached));
    std::cout << "a choice made in mid-air is offered the placements the pair still reaches\n";
}

// (b3) The pair chosen anew in mid-air (thinkInAir: its colours known only once it is in the air) is offered only the
//      placements it still reaches from where it is: the second column ten high, the pair falling in the third with no
//      choice until it is below the top of the second.
struct FirstChoiceProbe : ThinkProbe {
    bool seen = false, air = false;
    unsigned offeredLegal = 0;
    PairControl pair{};
    GameField field;
    int choose(const Observation& o, Pair colors, unsigned legal) override {
        if (!choosingAgain && !seen)
            seen = true, air = o.ownAir && o.self().phase == Phase::Next, offeredLegal = legal, pair = o.ownAirPair, field = o.self().field;
        return ThinkProbe::choose(o, colors, legal);
    }
};
static void testChosenInAirReach() {
    Rules rules;
    rules.cancel = true;
    FirstChoiceProbe probe;
    probe.ahead = 12, probe.fixed = placementId({5, UP});
    probe.fastestWays = true, probe.fastestStates = 4000;
    probe.pathCheck = true; // (as X3 with Params::x3PathCheck)
    Round r;
    r.start(rules, 3);
    addGarbage(r.player[0].field, 1, 10);
    probe.startRound(0, 5);
    PlayerEvents e[2];
    for (int f = 0; f < 4000 && r.player[0].moves < 1; ++f) {
        Observation o = ai::observe(r, 0);
        // (the colours of the pair not shown until it is below the top of the second column, as a reader of another
        //  program may miss them)
        const Player& self = o.self();
        const bool low = self.phase == Phase::Control && self.pair.y < 9;
        const int shown = self.phase == Phase::Control ? self.nextPair - 1 : self.nextPair;
        if (!low && o.tsumo.count() > shown) o.tsumo.pairs.resize(size_t(shown));
        const uint8_t buttons[2] = {probe.frame(o), 0};
        r.step(buttons, e);
    }
    check(r.player[0].moves == 1, "the pair of the test of a pair chosen in mid-air was not put");
    check(probe.seen && probe.air, "the pair was not chosen in mid-air, below the top of the second column");
    ai::InputPlanner plain;
    ai::InputPath way;
    const unsigned legal = legalPlacements(probe.field);
    unsigned reached = 0;
    for (int id = 0; id < PLACEMENTS; ++id)
        if ((legal >> id & 1) && plain.plan(probe.field, probe.pair, placement(id), r.timing, rules.speedPercent, way)) reached |= 1u << id;
    check(reached != legal, "the test of a pair chosen in mid-air expects placements the pair no longer reaches");
    check(probe.offeredLegal == reached, "a pair chosen anew in mid-air is offered placements it no longer reaches: offered " +
                                             std::to_string(probe.offeredLegal) + ", reached " + std::to_string(reached));
    std::cout << "a pair chosen anew in mid-air is offered the placements it still reaches\n";
}

// (c) The nearest placement and its way, when the choice comes late (lateScript) and the pair is behind a column.
static void testNearestWayOverSoonest() {
    Rules rules;
    rules.cancel = true;
    int cases = 0, otherWay = 0;
    for (int fixedId = 0; fixedId < PLACEMENTS; ++fixedId)
        for (int column = 0; column < W; ++column)
            for (const int high : {8, 10, 11})
                for (const int waited : {1, 8, 20, 40}) {
            ThinkProbe behind;
            behind.ahead = 12, behind.fixed = fixedId;
            behind.fastestWays = true, behind.fastestStates = 4000;
            const std::vector<int> script{0, waited}; // (the first pair waits that many frames in the air for its choice)
            behind.lateScript = &script;
            Round s;
            s.start(rules, 3);
            addGarbage(s.player[0].field, column, high);
            behind.startRound(0, 5);
            PlayerEvents ev[2];
            int64_t routed = -1, locked = -1;
            Observation atRoute;
            std::vector<uint8_t> pressed; // (the buttons the agent gives from the frame the choice comes in)
            for (int f = 0; f < 4000 && locked < 0; ++f) {
                const Observation o = ai::observe(s, 0);
                const int64_t waitedBefore = behind.lateFrames;
                const uint8_t buttons[2] = {behind.frame(o), 0};
                // (the frame the choice comes in: the pair waits no more, and takes its first button of the way)
                if (routed < 0 && waitedBefore > 0 && behind.lateFrames == waitedBefore && o.self().phase == Phase::Control)
                    routed = o.frame, atRoute = o;
                if (routed >= 0) pressed.push_back(buttons[0]);
                const int64_t frame = s.frame;
                s.step(buttons, ev);
                if (ev[0].locked) locked = frame;
            }
            if (routed < 0 || locked < 0) continue;
            const Player& p = atRoute.self();
            const Placement want = placement(behind.fixed);
            ai::InputPlanner plain;
            ai::InputPath way;
            if (plain.plan(p.field, p.pair, want, atRoute.timing, rules.speedPercent, way)) continue; // (the choice is still reached)
            // (the nearest placement with a way, as PlacementAgent::nearest takes it)
            const unsigned legal = legalPlacements(p.field);
            int best = -1, bestApart = 0;
            for (int other = 0; other < PLACEMENTS; ++other) {
                if (!(legal >> other & 1)) continue;
                const Placement t = placement(other);
                const int apart = std::abs(int(t.x) - int(want.x)) + std::abs(t.childX() - want.childX());
                if (best >= 0 && apart >= bestApart) continue;
                if (plain.plan(p.field, p.pair, t, atRoute.timing, rules.speedPercent, way)) best = other, bestApart = apart;
            }
            if (best < 0) continue;
            ai::InputPath straight, sure;
            check(plain.plan(p.field, p.pair, placement(best), atRoute.timing, rules.speedPercent, straight), "no straight way to the nearest placement");
            ai::InputPlanner soonest;
            soonest.fastest = true, soonest.fastestStates = 4000;
            check(soonest.planSure(p.field, p.pair, placement(best), atRoute.timing, rules.speedPercent, sure), "no way to the nearest placement");
            // (the frame the way locks the pair in, played from where the pair was when the choice came)
            PairControl q = p.pair;
            int64_t lockAt = -1;
            for (size_t k = 0; k < sure.buttons.size() && lockAt < 0; ++k) {
                stepPair(q, p.field, sure.buttons[k], atRoute.timing, rules.speedPercent);
                if (q.locked) lockAt = routed + int64_t(k);
            }
            const std::string at = " (placement " + std::to_string(fixedId) + ", column " + std::to_string(column) + " " + std::to_string(high) +
                                   " high, the choice " + std::to_string(waited) + " frames late)";
            check(lockAt >= 0, "the way to the nearest placement does not lock the pair" + at);
            check(locked == lockAt, "a pair behind a column did not go to the nearest placement by the way over soonest: locked in frame " +
                                        std::to_string(locked) + ", that way " + std::to_string(lockAt) + at);
            // (the buttons themselves: a way over soonest may lock in the frame the straight one does and differ in them)
            check(pressed.size() >= sure.buttons.size() && std::equal(sure.buttons.begin(), sure.buttons.end(), pressed.begin()),
                  "a pair behind a column did not go to the nearest placement with the buttons of the way over soonest" + at);
            otherWay += sure.buttons != straight.buttons;
            ++cases;
        }
    check(cases > 20 && otherWay >= 8, "the test of the nearest placement has too few cases: " + std::to_string(cases) + ", " +
                                          std::to_string(otherWay) + " with a way over soonest other than the straight one");
    std::cout << "nearest placement: " << cases << " pairs behind a column, " << otherWay << " by a way over soonest other than the straight one\n";
}
