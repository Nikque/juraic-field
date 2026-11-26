// The reading (Duel) of the ways over soonest with Rules::cancel, held against Round: included by ai_tests.cpp after
// check, aboutToSpawn and describe.

// An opponent the reading gives a pair not on the screen (exchange.h, answer): with xWildEvery 2 the pair that fires comes
// one pair later than with 1, so it locks wildFrames(1) later - read as the other seat with the cancel (FASTEST_BOUND)
// as well as without it.
static void testDuelWildWait() {
    Rules rules;
    rules.cancel = true;
    Round r;
    r.start(rules, 5);
    Observation o = ai::observe(r, 0);
    // (three pieces of one colour on the opponent's column 0, and garbage waiting above him)
    for (int y = 0; y < 3; ++y) o.player[1].field.f.set(0, y, Cell(1));
    o.player[1].incoming.confirmed = 2;
    ai::PlanSet mine, theirs;
    mine.who = 0, theirs.who = 1;
    const int cells = o.player[1].field.f.count();
    for (int told = 0; told < 2; ++told) {
        int64_t lock[2] = {-1, -1};
        for (int every = 1; every <= 2; ++every) {
            ai::Params p;
            p.xWildEvery = every;
            Observation view = o;
            if (told) view.fastestRead[1] = Observation::FASTEST_BOUND;
            const ai::Duel d = ai::playOut(view, mine, nullptr, theirs, nullptr, p, 0);
            check(d.player[1].moves == o.player[1].moves + 1, "未知の組で返す相手が1組だけ置かない (xWildEvery " + std::to_string(every) + ")");
            lock[every - 1] = d.player[1].lockFrame;
        }
        const int64_t wait = ai::Params{}.wildFrames(1, cells);
        check(lock[1] - lock[0] == wait, std::string(told ? "最速で読む相手" : "最速で読まない相手") + "の未知の組の待ち: 固定 " +
                                             std::to_string(lock[0]) + " と " + std::to_string(lock[1]) + "、待ち " + std::to_string(wait));
    }
    std::cout << "未知の組の待ち: 相手を最速で読んでも、役に立たない組の時間だけ遅く固定する\n";
}

// The own pair in mid-air goes on with the buttons noted for it (Observation::ownWayTarget and those after it): on an
// empty field, the axis on column 2 a row up in the lower half, 110/256 of a step fallen, two frames down to the floor,
// a slide to column 3 on it and the lock - five frames, the landing over in seventeen, two points of the down key - where
// the straight way would lock in four and be over in eighteen. The Duel locks the pair, scores it and has the next pair
// as the Round does.
static void testDuelOwnWay() {
    Rules rules;
    rules.cancel = true;
    Round r;
    r.start(rules, 11);
    const Timing& t = r.timing;
    Player& p = r.player[0];
    PlayerEvents e[2];
    const uint8_t none[2] = {0, 0};
    while (!aboutToSpawn(p)) r.step(none, e);
    r.step(none, e); // (the pair appears)
    check(p.phase == Phase::Control, "空中の組の試験: 組が出ない");
    PairControl q = spawnPair(t);
    q.x = 2, q.y = 1, q.dir = UP, q.upper = false, q.gain = 110, q.grounded = false, q.held = 0;
    p.pair = q;
    const Placement target{3, UP};
    const std::vector<uint8_t> way = {BTN_DOWN, BTN_DOWN, uint8_t(BTN_RIGHT | BTN_DOWN), BTN_DOWN, BTN_DOWN};
    const int end = ai::FastestSearch::ends(p.field, q, way, t, rules.speedPercent);
    check(end == 17, "空中の組の試験: 床を滑る経路が17フレームで終わらない (" + std::to_string(end) + ")");
    Observation o = ai::observe(r, 0);
    o.fastestRead[0] = Observation::FASTEST_OWN;
    o.ownWayTarget = placementId(target), o.ownWayCheck = o.frame + end - 1, o.ownWayLock = o.frame + int64_t(way.size()) - 1;
    {
        PairControl c = q;
        int soft = 0;
        for (const uint8_t b : way) soft += stepPair(c, p.field, b, t, rules.speedPercent).dropBonus;
        o.ownWaySoft = soft;
    }
    // (the rest of the straight way from where the pair is: the lock the Duel is given, as Gaze has it)
    ai::InputPlanner straight;
    ai::InputPath rest;
    check(straight.plan(p.field, q, target, t, rules.speedPercent, rest), "空中の組の試験: 直進の経路がない");
    PairControl c = q;
    for (const uint8_t b : rest.buttons) stepPair(c, p.field, b, t, rules.speedPercent);
    ai::Duel d(o);
    for (int k = 0; k < 2; ++k) d.garbageRng[k] = r.garbageRng[k];
    d.place(0, target, p.colors, o.frame + rest.frames() - 1, rest.softDropRows, restedFrames(c, t));
    int64_t lockedAt = -1, roundNext = -1, duelNext = -1;
    for (int frames = 0; roundNext < 0 && frames < 5000; ++frames) {
        const uint8_t buttons[2] = {frames < int(way.size()) ? way[size_t(frames)] : uint8_t(0), 0};
        r.step(buttons, e);
        const int appeared = duelNext < 0 ? d.run(r.frame) : 0;
        if (e[0].locked) lockedAt = r.frame - 1;
        if (e[0].spawned) roundNext = r.frame - 1;
        if (appeared & 1) duelNext = r.frame - 1;
    }
    check(roundNext >= 0 && lockedAt == o.frame + 4, "空中の組の試験: 5フレーム目で固定しない");
    if (duelNext < 0) d.run(roundNext + 400), duelNext = d.player[0].spawnFrame;
    check(d.player[0].lockFrame == lockedAt, "空中の組の固定: 本体 " + std::to_string(lockedAt) + "、読み " + std::to_string(d.player[0].lockFrame));
    check(duelNext == roundNext, "空中の組の次の組: 本体 " + std::to_string(roundNext) + "、読み " + std::to_string(duelNext));
    check(d.player[0].scoring.score == p.scoring.score,
          "空中の組の下入力の点: 本体 " + std::to_string(p.scoring.score) + "、読み " + std::to_string(d.player[0].scoring.score));
    std::cout << "空中の組: 記録した入力の固定・点・次の組が本体と同じ\n";
}

// With the cancel (Timing::landThenLock) a pair put by placeFast on a low field, the Duel without tallPaths: the lock, the
// points of the down key and the next pair as the Round has them with the straight way (the tables are those of
// DEFAULT_TIMING: a pair dropped there locks a frame later, without the point of a landing with the down key).
static void testDuelTablesCancel() {
    Rng rng(20261124, 21);
    Rules rules;
    rules.cancel = true;
    ai::InputPlanner straight;
    ai::InputPath path;
    int cases = 0;
    for (int i = 0; i < 600; ++i) {
        Round r;
        r.start(rules, uint64_t(i) + 1);
        const Timing& t = r.timing;
        Player& p = r.player[0];
        for (int x = 0; x < W; ++x) addGarbage(p.field, x, int(rng.below(x == DEATH_X ? 6 : 9)));
        PlayerEvents e[2];
        const uint8_t none[2] = {0, 0};
        while (!aboutToSpawn(p)) r.step(none, e);
        const unsigned legal = legalPlacements(p.field);
        int ids[PLACEMENTS], n = 0;
        for (int id = 0; id < PLACEMENTS; ++id)
            if (legal >> id & 1) ids[n++] = id;
        if (!n) continue;
        const Placement target = placement(ids[rng.below(uint64_t(n))]);
        if (!straight.plan(p.field, spawnPair(t), target, t, rules.speedPercent, path)) continue;
        GameField closed = p.field;
        place(closed, target, r.tsumo.pair(0));
        if (closed.dead()) continue;
        const std::string at = " (case " + std::to_string(i) + ", " + describe(p.field, placementId(target)) + ")";
        const Observation o = ai::observe(r, 0);
        ai::Duel d(o);
        for (int k = 0; k < 2; ++k) d.garbageRng[k] = r.garbageRng[k];
        int64_t lockedAt = -1, roundNext = -1, duelNext = -1;
        for (int frames = 0; roundNext < 0 && frames < 5000; ++frames) {
            const uint8_t buttons[2] = {frames < path.frames() ? path.buttons[size_t(frames)] : uint8_t(0), 0};
            r.step(buttons, e);
            const int appeared = duelNext < 0 ? d.run(r.frame) : 0;
            if (frames == 0) {
                check(e[0].spawned && (appeared & 1), "表の試験: 組が出ない" + at);
                d.placeFast(0, target, p.colors);
            } else {
                if (e[0].spawned) roundNext = r.frame - 1;
                if (appeared & 1) duelNext = r.frame - 1;
            }
            if (e[0].locked) lockedAt = r.frame - 1;
        }
        check(roundNext >= 0 && lockedAt >= 0, "表の試験: 手が終わらない" + at);
        if (duelNext < 0) d.run(roundNext + 400), duelNext = d.player[0].spawnFrame;
        check(d.player[0].lockFrame == lockedAt, "表の試験の固定: 本体 " + std::to_string(lockedAt) + "、読み " + std::to_string(d.player[0].lockFrame) + at);
        check(duelNext == roundNext, "表の試験の次の組: 本体 " + std::to_string(roundNext) + "、読み " + std::to_string(duelNext) + at);
        check(d.player[0].scoring.score == p.scoring.score,
              "表の試験の下入力の点: 本体 " + std::to_string(p.scoring.score) + "、読み " + std::to_string(d.player[0].scoring.score) + at);
        ++cases;
    }
    check(cases > 300, "表の試験の件数が少ない");
    std::cout << "キャンセルありの低い盤: " << cases << " 手の固定・点・次の組が本体と同じ\n";
}

// A placement that leaves a piece to fall, with the cancel: columns 0, 12, 0, 0, 1, 0, the pair on its side at column 3.
// The Round plays the way over soonest (a turn pushed off column 1 locks it two frames before the straight way); the
// Duel told of the other seat (FASTEST_BOUND) has the next pair no later, the one told of the own seat (FASTEST_OWN) in
// the same frame.
static void testDuelSplitSoonest() {
    Rules rules;
    rules.cancel = true;
    const int heights[W] = {0, 12, 0, 0, 1, 0};
    const Placement target{3, RIGHT};
    ai::InputPlanner soonest;
    soonest.fastest = true;
    ai::InputPath path;
    for (int kind = 1; kind <= 2; ++kind) {
        Round r;
        r.start(rules, 3);
        const Timing& t = r.timing;
        Player& p = r.player[0];
        for (int x = 0; x < W; ++x)
            for (int y = 0; y < heights[x]; ++y) p.field.f.set(x, y, GARBAGE);
        PlayerEvents e[2];
        const uint8_t none[2] = {0, 0};
        while (!aboutToSpawn(p)) r.step(none, e);
        check(soonest.planSure(p.field, spawnPair(t), target, t, rules.speedPercent, path), "ちぎりの試験: 経路がない");
        Observation o = ai::observe(r, 0);
        o.fastestRead[0] = kind == 1 ? Observation::FASTEST_BOUND : Observation::FASTEST_OWN;
        ai::Duel d(o);
        d.tallPaths = 1;
        for (int k = 0; k < 2; ++k) d.garbageRng[k] = r.garbageRng[k];
        int64_t roundNext = -1, duelNext = -1;
        for (int frames = 0; roundNext < 0 && frames < 5000; ++frames) {
            const uint8_t buttons[2] = {frames < path.frames() ? path.buttons[size_t(frames)] : uint8_t(0), 0};
            r.step(buttons, e);
            const int appeared = duelNext < 0 ? d.run(r.frame) : 0;
            if (frames == 0) {
                check(e[0].spawned && (appeared & 1), "ちぎりの試験: 組が出ない");
                d.placeFast(0, target, p.colors);
            } else {
                if (e[0].spawned) roundNext = r.frame - 1;
                if (appeared & 1) duelNext = r.frame - 1;
            }
        }
        check(roundNext >= 0, "ちぎりの試験: 手が終わらない");
        if (duelNext < 0) d.run(roundNext + 400), duelNext = d.player[0].spawnFrame;
        if (kind == 1)
            check(duelNext <= roundNext, "ちぎりの配置を最速で読む相手が本体より遅い: 本体 " + std::to_string(roundNext) + "、読み " + std::to_string(duelNext));
        else
            check(duelNext == roundNext, "ちぎりの配置を最速で読む自席が本体とずれる: 本体 " + std::to_string(roundNext) + "、読み " + std::to_string(duelNext));
    }
    std::cout << "ちぎりの配置: 最速で読む相手は本体より遅くなく、自席は本体と同じ\n";
}

// fastestEnd keeps the results of the last pair it was asked for: a pair that differs from it only where InputPlanner's key
// of a state does not look (PairControl::restless, owned and ownFrames: the clock of the landing under the recorded rules)
// gets its own. The pairs lie on the floor; the first pair and placement whose two ends differ are taken.
static void testFastestEndKept() {
    const Timing t = recordedRules(Timing{});
    const GameField g;
    int64_t frame = 1000;
    int found = 0, keyed = 0;
    for (int x = 0; x < W && !found; ++x)
        for (int kind = 0; kind < 32 && !found; ++kind)
            for (int id = 0; id < PLACEMENTS && !found; ++id) {
                PairControl a = spawnPair(t);
                a.x = int8_t(x), a.y = 0, a.dir = UP, a.upper = false, a.gain = 0, a.grounded = true, a.held = 0;
                a.landings = 1, a.groundFrames = 1, a.owned = true, a.ownFrames = 1;
                PairControl b = a;
                if (kind == 0) b.restless = true;
                else b.ownFrames = uint8_t(1 + 2 * kind);
                if (!(ai::InputPlanner::stateKey(a) == ai::InputPlanner::stateKey(b))) continue;
                ++keyed;
                const Placement target = placement(id);
                const ai::FastestWay alone = ai::fastestEnd(g, b, frame++, target, t, 100);
                const ai::FastestWay first = ai::fastestEnd(g, a, frame++, target, t, 100);
                if (first.end == alone.end) continue;
                found = 1;
                const int64_t at = frame++;
                ai::fastestEnd(g, a, at, target, t, 100);
                const ai::FastestWay kept = ai::fastestEnd(g, b, at, target, t, 100);
                check(kept.end == alone.end && kept.lock == alone.lock && kept.soft == alone.soft,
                      "一時記憶が別の組の結果を返す: " + std::to_string(kept.end) + "、単独では " + std::to_string(alone.end));
            }
    check(keyed > 0, "一時記憶の試験: 鍵が同じで着地の時計だけ違う組がない");
    check(found, "一時記憶の試験: 着地の時計だけ違う組で最速の終わりが違う例がない");
    std::cout << "最速経路の一時記憶: 着地の時計だけ違う組を別に扱う\n";
}

// The buttons of placeFast kept by field (SpawnInputCache, lent to the Duels of a reading with Timing::landThenLock): on
// low and tall fields, with pieces in the 14th row, asked twice, the same as tallSpawnInput gives; and a Duel that borrows
// the cache puts a pair with the same lock, check and points as one that does not.
static void testSpawnInputCacheSame() {
    Rng rng(20261124, 33);
    Rules rules;
    rules.cancel = true;
    const Timing t = recordedRules(Timing{});
    ai::SpawnInputCache cache;
    cache.configure(t, rules.speedPercent);
    int compared = 0, duels = 0;
    for (int i = 0; i < 300; ++i) {
        Round r;
        r.start(rules, uint64_t(i) + 1);
        Player& p = r.player[0];
        for (int x = 0; x < W; ++x) addGarbage(p.field, x, int(rng.below(i % 3 == 0 ? 13 : 9)));
        if (i % 7 == 0) p.field.row14[size_t(rng.below(W))] = GARBAGE;
        for (int pass = 0; pass < 2; ++pass)
            for (int id = 0; id < PLACEMENTS; ++id) {
                const ai::SpawnInput a = cache.get(p.field, placement(id));
                const ai::SpawnInput b = ai::tallSpawnInput(p.field, placement(id), t, rules.speedPercent);
                check(a.frames == b.frames && a.soft == b.soft && a.rested == b.rested,
                      "入力の一時記憶が経路の計画と違う (case " + std::to_string(i) + ", " + describe(p.field, id) + ")");
                ++compared;
            }
        PlayerEvents e[2];
        const uint8_t none[2] = {0, 0};
        while (!aboutToSpawn(p)) r.step(none, e);
        const unsigned legal = legalPlacements(p.field);
        if (!legal) continue;
        const Observation o = ai::observe(r, 0);
        check(o.timing == t, "入力の一時記憶の試験: 観測の時間表が記録規則でない");
        for (int id = 0; id < PLACEMENTS; ++id) {
            if (!(legal >> id & 1)) continue;
            ai::Duel with(o), without(o);
            with.spawnInputs = &cache;
            const int64_t until = o.frame + 400;
            if (!(with.run(until) & 1) || !(without.run(until) & 1)) continue;
            const Pair colors{Cell(1), Cell(2)};
            with.placeFast(0, placement(id), colors);
            without.placeFast(0, placement(id), colors);
            const ai::DuelPlayer &x = with.player[0], &y = without.player[0];
            check(x.lockFrame == y.lockFrame && x.at == y.at && x.scoring.score == y.scoring.score && x.field == y.field,
                  "入力の一時記憶を借りた読みが違う (case " + std::to_string(i) + ", " + describe(p.field, id) + ")");
            ++duels;
        }
    }
    check(duels > 1000, "入力の一時記憶の試験の件数が少ない");
    std::cout << "入力の一時記憶: " << compared << " 件が経路の計画と同じ、" << duels << " 手の読みが同じ\n";
}
