#pragma once

struct R4BoardProbe : ai::X3Agent {
    using X3Agent::X3Agent;

    void read(const Observation& o) {
        gaze.plans(o, o.me, true, p.x2Depth, p, 7, mine);
        gaze.plans(o, 1 - o.me, false, p.x2Depth, p, 7, theirs);
    }

    std::pair<bool, int> checkShot(const Observation& o, ai::Plan shot) {
        ai::Duel untilPair(o, 7);
        const int him = 1 - o.me;
        check(ai::toPairOf(untilPair, him, o.frame + 6000), "r4 rival's first pair does not appear");
        int heights[W];
        untilPair.player[him].field.heights(heights);
        shot.sent = W * (12 - heights[DEATH_X]) + 1;
        Rival rival;
        int garbage = -1;
        const bool holds = outlasts(o, shot.id[0], o.rules.rateAt(o.frame), 7, rival, &shot, &garbage);
        return {holds, garbage};
    }
};

static void testR4OutlastAfterEarlierDrop() {
    for (bool cancel : {false, true}) for (int me : {0, 1}) {
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
        Round round = me ? situation(rules, rivalField, ownField, {4, 4})
                         : situation(rules, ownField, rivalField, {4, 4});
        // Keep the confirmation comfortably inside the earlier garbage animation in both timing models.
        round.timing.waveQuick = round.timing.waveBase = 24;
        round.player[him].phase = Phase::Land;
        round.player[him].wait = 1;
        round.player[him].incoming.confirmed = 30;
        const Observation landing = ai::observe(round, me);
        Observation dropping;
        const Placement ownTarget{5, UP}, rivalTarget{2, UP};
        ai::InputPlanner planner;
        ai::InputPath paths[2];
        check(planner.plan(ownField, appearingPair(round.player[me], rules, round.timing), ownTarget,
                           round.timing, rules.speedPercent, paths[me]), "r4 own attack has no button path");
        int cursor[2]{};
        int64_t confirmed = -1, spawned = -1;
        int reply = -1;
        for (int frames = 0; frames < 450 && reply < 0 && round.state == RoundState::Playing; ++frames) {
            uint8_t buttons[2]{};
            const auto& opponent = round.player[him];
            if (aboutToSpawn(opponent))
                check(planner.plan(opponent.field, appearingPair(opponent, rules, round.timing), rivalTarget,
                                   round.timing, rules.speedPercent, paths[him]), "r4 first pair after drop has no button path");
            for (int side : {0, 1}) if (cursor[side] < paths[side].frames()) buttons[side] = paths[side].buttons[cursor[side]++];
            PlayerEvents events[2];
            round.step(buttons, events);
            if (!frames) {
                check(events[him].garbageFell && round.player[him].phase == Phase::Garbage,
                      "r4 previous 30 garbage pieces did not settle before the attack");
                dropping = ai::observe(round, me);
            }
            if (events[me].chainEnded && confirmed < 0) confirmed = round.frame - 1;
            if (events[him].spawned && spawned < 0) spawned = round.frame - 1;
            if (events[him].waveStarted) {
                const auto& wave = round.player[him].wave;
                reply = wave.sent + wave.offsetted;
            }
        }
        check(confirmed > landing.frame && spawned > confirmed + round.timing.nextFrames,
              "r4 first counter pair did not appear beyond the old cutoff");
        check(reply == 2, "r4 first pair did not form the intended two-piece counter");

        ai::Params p;
        p.x2Depth = 2;
        p.beamWidth = 30;
        p.beamQueues = 3;
        p.x3OutlastSettled = 1;
        p.x3OutlastMean = 0;
        p.checkKnownExchange = 0;
        const auto evaluate = [&](const Observation& o, int64_t confirm) {
            R4BoardProbe probe(p);
            probe.startRound(me, 7);
            probe.read(o);
            ai::Plan shot;
            shot.count = shot.fireDepth = 1;
            shot.id[0] = int8_t(placementId(ownTarget));
            shot.confirmFrame = confirm;
            shot.endFrame = confirm + o.timing.nextFrames + 1;
            return probe.checkShot(o, shot);
        };
        constexpr bool preserveNonCancel = false;
        const auto expected = preserveNonCancel && !cancel ? std::pair{true, -1} : std::pair{false, reply};
        check(evaluate(landing, confirmed) == expected, "r4 win test skipped the first reply after the previous long drop (Land)");
        check(evaluate(dropping, confirmed) == expected, "r4 win test skipped the first reply after the previous long drop (Garbage)");
        check(evaluate(landing, landing.frame) == std::pair{true, -1},
              "r4 win test counted a pair despite the previous garbage settling on the same frame as the attack");
    }
}

static void testR4FoundationAfterReshape() {
    for (bool cancel : {false, true}) {
        Rules rules;
        rules.cancel = cancel;
        GameField field;
        for (int y = 0; y < 3; ++y) {
            field.f.set(0, y, 1);
            field.f.set(2, y, 2);
            field.f.set(5, y, 1);
        }
        Round round = situation(rules, field, {}, {1, 1});
        ai::PlanSet mine;
        mine.who = 0;
        mine.knownCount = mine.pairCount = 3;
        mine.pair[0] = {4, 4};
        mine.pair[1] = {2, 3};
        mine.pair[2] = {1, 1};
        for (size_t i = 0; i < 3; ++i) {
            round.tsumo.piece[i * 2] = mine.pair[i].axis;
            round.tsumo.piece[i * 2 + 1] = mine.pair[i].child;
        }
        const Observation o = ai::observe(round, 0);
        const auto playToNext = [&](Round& r, Placement target) {
            ai::InputPlanner planner;
            ai::InputPath path;
            check(planner.plan(r.player[0].field, appearingPair(r.player[0], rules, r.timing), target,
                               r.timing, rules.speedPercent, path), "r4 foundation fixture has no button path");
            const int moves = r.player[0].moves;
            for (int frame = 0; frame < 450; ++frame) {
                const uint8_t buttons[] = {frame < path.frames() ? path.buttons[frame] : uint8_t(0), 0};
                PlayerEvents events[2];
                r.step(buttons, events);
                if (r.player[0].moves == moves + 1 && aboutToSpawn(r.player[0])) return;
            }
            check(false, "r4 foundation fixture never finished the placement");
        };
        playToNext(round, {3, UP});
        playToNext(round, {2, UP});
        check(round.player[0].field.f.get(2, 0) == 3 && round.player[0].field.f.get(2, 1) == 0,
              "r4 foundation fixture did not remove the prior four-piece group and settle its child");

        ai::Plan plan;
        plan.count = plan.fireDepth = 3;
        plan.id[0] = int8_t(placementId({3, UP}));
        plan.id[1] = int8_t(placementId({2, UP}));
        FieldBits corners[2];
        for (int y = 0; y < 3; ++y) {
            corners[0] |= FieldBits::cell(0, y);
            corners[1] |= FieldBits::cell(5, y);
        }
        FieldBits actual[2];
        for (int side : {0, 1}) {
            const Placement last{uint8_t(side ? 5 : 0), UP};
            GameField placed = round.player[0].field;
            place(placed, last, mine.pair[2]);
            Wave wave;
            check(findWaveFor(placed.f, wave, rules.clearCount), "r4 fixture's last placement does not fire");
            check((wave.cleared & round.player[0].field.f.normal()) == corners[side],
                  "r4 fixture did not have the expected initial support in the real settled board");
            plan.id[2] = int8_t(placementId(last));
            actual[side] = ai::lv38_foundation::firstSupport(o, mine, plan);
            constexpr bool preserveNonCancel = false;
            if (!(preserveNonCancel && !cancel))
                check(actual[side] == corners[side], "r4 foundation counted the pieces already cleared by the previous placement");
        }
        constexpr bool preserveNonCancel = false;
        check(actual[0].disjoint(actual[1]) == !(preserveNonCancel && !cancel),
              "r4 foundation confused separate triggers after a prior clear");
    }
}

static void testR4BoardCases() {
    testR4OutlastAfterEarlierDrop();
    testR4FoundationAfterReshape();
}
