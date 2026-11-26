#pragma once

// Included by ai_tests.cpp before main(), after its helpers (check, situation).
//
//  (a) testOutlastPairInHand, testOutlastOwnGarbage: the chain the opponent can answer with (X3Agent::outlasts) is
//      searched as deep as he has placements until the own chain is confirmed - the moves that fit before it and the
//      pair he then holds, which fires (X3Agent::placementsUpTo and heldUntil, in prepareRival and in outlasts). What
//      he has is taken from the game: a Round played with the fastest input, and the pairs he had locked when the
//      garbage of the chain fell on him.
//  (b) testOutlastCarriedPoints: the points he carries count in his answer only when he fires a chain, and the answer
//      is turned into garbage at the rate of the frame the own chain ends in. The garbage his waves make in the game
//      is what the reading must say.
//  (e) testOutlastLaterAttack: an attack that ends later than those his field was searched for is read with a search
//      of its own depth, whichever attack is read first.
//  (f) testSentAfterHisSameTime: Params::x3SentAfterHis 1 with a placement that is over before his chain offsets
//      what waited above him: nothing is sent.
//  (c) testWinQuietUnchecked: a round that is won without firing (X3Agent::win, x3WinBuild) is taken as won whatever
//      chains the pairs on the screen could fire below the same first placement.
//  (d) testSentAfterHis: Params::x3SentAfterHis. What a plan sends and what an answer leaves above the opponent are
//      counted from what is above his field in the observation (0), or from what is above it when his chain that runs
//      is over (1): garbage that waited there and that his chain offsets.

struct JudgeProbe : ai::X3Agent {
    using X3Agent::X3Agent;
    void read(const Observation& o) {
        gaze.plans(o, o.me, true, p.x2Depth, p, 7, mine);
        gaze.plans(o, 1 - o.me, false, p.x2Depth, p, 7, theirs);
    }
    // The plan that fires with the first pair on placement `id` (null: none).
    const ai::Plan* fireOf(int id) const {
        for (const ai::Plan& s : mine.plans)
            if (s.id[0] == id && s.fireDepth == 1) return &s;
        return nullptr;
    }
    // Of the plans below first placement `id` that fire: how many, and the most garbage one of them sends.
    int firesBelow(int id, int& most) const {
        int n = 0;
        most = 0;
        for (const ai::Plan& s : mine.plans) {
            if (s.id[0] != id || !s.fireDepth) continue;
            ++n;
            if (s.sent > most) most = s.sent;
        }
        return n;
    }
    // The last frame a plan that fires ends in (0: none fires).
    int64_t lastFireEnd() const {
        int64_t last = 0;
        for (const ai::Plan& s : mine.plans)
            if (s.fireDepth && s.endFrame > last) last = s.endFrame;
        return last;
    }
    // prepareRival: the frame he places from, the cells of his field then and the depth his field is searched to.
    void rivalOf(const Observation& o, int64_t& from, int& cells, int& depth) const {
        Rival rival;
        prepareRival(o, 7, rival, 0);
        from = rival.from, cells = rival.cells, depth = rival.depth;
    }
    int fits(int64_t frames, int cells) const { return p.movesIn(frames, cells, 0); }
    // outlasts: the garbage his answer to the largest chain below first placement `id` is counted as (-1: not counted).
    int answerOf(const Observation& o, int id) {
        Rival rival;
        int garbage = -1;
        outlasts(o, id, o.rules.rateAt(o.frame), 7, rival, nullptr, &garbage);
        return garbage;
    }
    int winOf(const Observation& o, unsigned legal) { return win(o, legal, o.rules.rateAt(o.frame), 7); }
    bool won() const { return kind == Win; }
    // What is above his field at the end of plan `s` played against him standing still.
    int leftAbove(const Observation& o, const ai::Plan& s) const {
        const ai::Duel d = ai::playOut(o, mine, &s, theirs, nullptr, p, 7);
        return d.player[1 - o.me].fallen + d.player[1 - o.me].incoming.total();
    }
    int answerFrom(const Observation& o) const { return answerBase(o).aboveHim; }
    // Of the plans, only the one that fires with the first pair on placement `id` stays.
    void only(int id) {
        std::erase_if(mine.plans, [&](const ai::Plan& s) { return s.id[0] != id || s.fireDepth != 1; });
    }
    // The placements exchangePoke and midgamePressure give him against an own chain confirmed in frame `confirm`
    // (repliesUntil, from the Duel in which he next holds a pair, as they make it); `spawn`: the frame that pair appears in.
    int turnsOf(const Observation& o, int64_t confirm, int64_t& spawn) const {
        ai::Duel before(o, 7);
        const int him = 1 - o.me;
        if (before.player[him].phase != ai::DuelPhase::Control) ai::toPairOf(before, him, o.frame + 6000);
        spawn = std::max(o.frame, before.player[him].spawnFrame);
        return repliesUntil(o, before, confirm, before.player[him].field.f.count());
    }
    // The plan of one pair that puts it on placement `id` and clears nothing (null: none).
    const ai::Plan* quietOf(int id) const {
        for (const ai::Plan& s : mine.plans)
            if (s.id[0] == id && s.count == 1 && !s.fireDepth) return &s;
        return nullptr;
    }
    // outlasts of two attacks with one reading of his field (Rival), `first` before `second`: the garbage his answer
    // to each is counted as (-1: not counted).
    void answersOf(const Observation& o, int id, const ai::Plan& first, const ai::Plan& second, int& a, int& b) {
        Rival rival;
        a = b = -1;
        outlasts(o, id, o.rules.rateAt(o.frame), 7, rival, &first, &a);
        outlasts(o, id, o.rules.rateAt(o.frame), 7, rival, &second, &b);
    }
};

// What the game does: a Round played on with the fastest straight input (FastInput, as testDuel plays it).
struct JudgePlay {
    int64_t confirm = -1; // the frame the first chain of seat `me` ended in (what it sent is confirmed then)
    int64_t fell = -1;    // the frame garbage first fell on the other seat ...
    int locked = -1;      // ... and the pairs he had locked then
    int waves = 0;        // his waves
    int64_t waveAt[4] = {-1, -1, -1, -1}; // the frames they began in
    int made = 0;         // garbage pieces his waves made: those that offset what waited above him, and those sent
    int offset = 0;       // ... those that offset
    int64_t ended = -1;   // the frame his first chain ended in
    int spawns = 0;       // his pairs that appeared
    int64_t spawnAt[4] = {-1, -1, -1, -1}; // the frames they appeared in
    int above = 0;        // what waits above his field at the end
};
// Seat `me` puts its first pair on placement `ownFirst` and the three after it upright on the first, third and fifth
// column (ownFirst -1: it touches no button); the other seat puts his first three pairs upright on the first, fourth
// and sixth column (hisInput false: he touches no button). Pairs after those fall by themselves. Played until
// stop(what happened so far), `limit` frames at most.
template <class Stop>
static JudgePlay judgePlay(Round r, int me, int ownFirst, bool hisInput, Stop stop, int limit = 900) {
    const int him = 1 - me;
    const int ownLater[3] = {placementId({0, UP}), placementId({2, UP}), placementId({4, UP})};
    const int hisPlaces[3] = {placementId({0, UP}), placementId({3, UP}), placementId({5, UP})};
    JudgePlay out;
    FastInput input[2];
    bool driven[2] = {false, false};
    int pairs[2] = {0, 0};
    PlayerEvents e[2];
    for (int n = 0; n < limit && r.state == RoundState::Playing && !stop(out); ++n) {
        uint8_t buttons[2] = {0, 0};
        for (int i = 0; i < 2; ++i) {
            Player& q = r.player[i];
            if (q.phase == Phase::Control) {
                if (driven[i]) buttons[i] = input[i](q.pair);
            } else if (aboutToSpawn(q) && !q.field.dead()) {
                const int k = pairs[i]++;
                int id = -1;
                if (i == me) id = ownFirst < 0 ? -1 : k == 0 ? ownFirst : k <= 3 ? ownLater[k - 1] : -1;
                else id = hisInput && k < 3 ? hisPlaces[k] : -1;
                driven[i] = id >= 0;
                if (driven[i]) {
                    input[i] = FastInput{placement(id)};
                    buttons[i] = input[i](spawnPair(TM));
                }
            }
        }
        r.step(buttons, e);
        const int64_t played = r.frame - 1;
        if (e[me].chainEnded && out.confirm < 0) out.confirm = played;
        if (e[him].spawned) {
            if (out.spawns < 4) out.spawnAt[out.spawns] = played;
            ++out.spawns;
        }
        if (e[him].waveStarted) {
            if (out.waves < 4) out.waveAt[out.waves] = played;
            ++out.waves;
            out.made += r.player[him].wave.sent + r.player[him].wave.offsetted;
            out.offset += r.player[him].wave.offsetted;
        }
        if (e[him].chainEnded && out.ended < 0) out.ended = played;
        if (e[him].garbageFell && out.fell < 0) out.fell = played, out.locked = r.player[him].moves;
    }
    out.above = r.player[him].incoming.total();
    return out;
}

// The rules of these tests: `rate` points a garbage piece at any time.
static Rules judgementRules(int rate) {
    Rules rules;
    rules.targetPoints = rate;
    for (int i = 0; i < rules.marginSteps; ++i) rules.marginRates[size_t(i)] = rate;
    return rules;
}
// The own field of these tests: three of colour 4 in the sixth column. A pair of colour 4 put upright on them clears five.
static GameField judgementOwnField() {
    GameField mine;
    for (int y = 0; y < 3; ++y) mine.f.set(5, y, 4);
    return mine;
}

static void testOutlastPairInHand() {
    const Rules rules = judgementRules(40);
    ai::Params p;
    p.beamWidth = 30, p.beamDepth = 8, p.beamQueues = 3;
    p.x2Depth = 2; // (two pairs of his are known: both of colour 4)
    const GameField mine = judgementOwnField();
    const Pair pair{4, 4};
    const int fire = placementId({5, UP});
    const GameField his{}; // (empty: one pair of colour 4 clears nothing, two put together clear four - 40 points)
    for (int me : {0, 1}) {
        const int him = 1 - me;
        const std::string seat = " (seat " + std::to_string(me) + ")";
        const Round base = me == 0 ? situation(rules, mine, his, pair) : situation(rules, his, mine, pair);
        // The frames his first move takes in the game: from the frame his pair appears in to the frame the next does.
        const JudgePlay alone = judgePlay(base, me, -1, true, [](const JudgePlay& s) { return s.spawns >= 2; });
        check(alone.spawns >= 2 && alone.fell < 0 && alone.spawnAt[1] > alone.spawnAt[0], "his first move is not played to its end" + seat);
        ai::Params q = p;
        q.x3MoveFrames = int(alone.spawnAt[1] - alone.spawnAt[0]), q.x3MoveFramesMid = 0, q.x3MoveFramesLate = 0;
        int seen[3] = {0, 0, 0}, short11 = 0;
        // His landing takes `late` frames more. The game says what he has when the garbage of the own chain falls on
        // him: no pair locked (it fell at the end of that landing), one (the pair he held when the chain was
        // confirmed), or more (a pair before it was put in time).
        for (int late = 1; late <= 150; ++late) {
            const std::string at = seat + " (his landing " + std::to_string(late) + " frames)";
            Round r = base;
            r.player[him].phase = Phase::Land, r.player[him].wait = late;
            const JudgePlay game = judgePlay(r, me, fire, true, [](const JudgePlay& s) { return s.fell >= 0; });
            check(game.confirm >= 0 && game.fell >= 0, "the garbage of the own chain does not fall on him in the game" + at);
            const int has = std::min(game.locked, 2);
            const ai::Observation o = ai::observe(r, me);
            JudgeProbe probe(q);
            probe.startRound(me, 7);
            probe.read(o);
            probe.only(fire);
            const ai::Plan* shot = probe.fireOf(fire);
            check(shot != nullptr, "the test field has no chain fired with the pair in hand" + at);
            check(shot->confirmFrame == game.confirm,
                  "the plan's chain is confirmed in frame " + std::to_string(shot->confirmFrame) + ", the game's in " + std::to_string(game.confirm) + at);
            int64_t from = 0;
            int cells = 0, depth = 0;
            probe.rivalOf(o, from, cells, depth);
            const int garbage = probe.answerOf(o, fire);
            // The reading: no pair of his in time (his answer is not counted), or his field searched one placement
            // deep, or deeper.
            const int reads = garbage < 0 ? 0 : std::min(depth, 2);
            check(reads == has, "the reading gives him " + std::to_string(reads) + " placements (2: or more), the game " + std::to_string(has) + at);
            // One placement: a pair of colour 4 clears nothing. Two: they clear four, a garbage piece at a rate of 40.
            check(has == 0 ? garbage < 0 : has == 1 ? garbage == 0 : garbage >= 1, "his answer is read as " + std::to_string(garbage) + " garbage" + at);
            // exchangePoke and midgamePressure count his placements with the same border (repliesUntil): one more
            // than one exactly when the game lets him put a pair before the garbage falls. (With no pair in time
            // they still count the one, on the safe side.)
            int64_t spawn = 0;
            const int turns = probe.turnsOf(o, game.confirm, spawn);
            check(std::min(turns, 2) == std::max(has, 1), "repliesUntil gives him " + std::to_string(turns) + " placements, the game " + std::to_string(has) + at);
            // (the count from his pair's frame to the confirmation alone, without the nextFrames a landing ends
            //  before the next pair, is one short on some of these landings)
            short11 += has == 2 && 1 + probe.fits(game.confirm - spawn, cells) < 2;
            ++seen[has];
        }
        check(short11 > 0, "the count to the confirmation alone is never short here (the test is out of date)" + seat);
        check(seen[0] > 0 && seen[1] > 0 && seen[2] > 0, "the landings of the test do not reach all three cases (the test is out of date)" + seat);
    }
    std::cout << "the opponent's answer: the moves that fit and the pair he then holds, as the game has it\n";
}

// The same count when garbage falls on the own field after the chain: the own next pair appears later by the frames
// of that fall, and his placements are still counted to the frame the chain is confirmed in, which the game gives.
static void testOutlastOwnGarbage() {
    const Rules rules = judgementRules(40);
    ai::Params p;
    p.beamWidth = 30, p.beamDepth = 8, p.beamQueues = 3;
    p.x2Depth = 2;
    const GameField mine = judgementOwnField();
    const Pair pair{4, 4};
    const int fire = placementId({5, UP});
    const GameField his{};
    Round base = situation(rules, mine, his, pair);
    const JudgePlay alone = judgePlay(base, 0, -1, true, [](const JudgePlay& s) { return s.spawns >= 2; });
    check(alone.spawns >= 2 && alone.spawnAt[1] > alone.spawnAt[0], "his first move is not played to its end");
    p.x3MoveFrames = int(alone.spawnAt[1] - alone.spawnAt[0]), p.x3MoveFramesMid = 0, p.x3MoveFramesLate = 0;
    // Five garbage pieces wait above the own field: the chain (two pieces at a rate of 40) offsets two, three fall.
    base.player[0].incoming.confirmed = 5;
    const JudgePlay game = judgePlay(base, 0, fire, true, [](const JudgePlay& s) { return s.confirm >= 0; });
    check(game.confirm >= 0, "the own chain does not end in the game");
    int differs = 0;
    for (int late = 1; late <= 150; ++late) {
        const std::string at = " (his landing " + std::to_string(late) + " frames)";
        Round r = base;
        r.player[1].phase = Phase::Land, r.player[1].wait = late;
        const ai::Observation o = ai::observe(r, 0);
        JudgeProbe probe(p);
        probe.startRound(0, 7);
        probe.read(o);
        probe.only(fire);
        const ai::Plan* shot = probe.fireOf(fire);
        check(shot != nullptr, "the test field has no chain fired with the pair in hand" + at);
        check(shot->confirmFrame == game.confirm, "the plan's chain is not confirmed in the frame the game's is" + at);
        check(shot->endFrame - shot->confirmFrame > TM.nextFrames + 1, "no garbage falls on the own field after the chain (the test is out of date)" + at);
        int64_t from = 0;
        int cells = 0, depth = 0;
        probe.rivalOf(o, from, cells, depth);
        // (a pair of his appears nextFrames after the landing before it: the last one he holds in time appears
        //  nextFrames after the confirmation at the latest)
        const int64_t until = game.confirm + TM.nextFrames;
        const int expected = 1 + (until > from ? probe.fits(until - from, cells) : 0);
        check(depth == std::min(ai::HORIZONS, expected), "his field is searched " + std::to_string(depth) + " deep for " + std::to_string(expected) + " placements" + at);
        differs += expected != 1 + (shot->endFrame > from ? probe.fits(shot->endFrame - from, cells) : 0);
    }
    check(differs > 0, "the frames of the fall on the own field change no count (the test is out of date)");
    std::cout << "the opponent's answer: counted to the confirmation, whatever falls on the own field after it\n";
}

static void testOutlastCarriedPoints() {
    ai::Params p;
    p.beamWidth = 30, p.beamDepth = 8, p.beamQueues = 3;
    p.x2Depth = 2;
    const GameField mine = judgementOwnField();
    const Pair pair{4, 4};
    const int fire = placementId({5, UP});
    // The frame the own chain ends in, in the game, at one rate throughout. (No points for the down button: the search
    // of his field counts none either.)
    Rules calm = judgementRules(40);
    calm.softDropBonus = false;
    const Round first = situation(calm, mine, GameField{}, pair);
    const JudgePlay timed = judgePlay(first, 0, fire, false, [](const JudgePlay& s) { return s.confirm >= 0; });
    check(timed.confirm >= 0, "the own chain does not end in the game");
    const int64_t now = first.frame, confirm = timed.confirm;
    // The rate falls from 40 to 20 after the wave of the own chain and before the own next pair: his answer runs at 20.
    Rules falling = calm;
    falling.marginSeconds = int((confirm + TM.nextFrames + 1) / FPS);
    for (int i = 0; i < falling.marginSteps; ++i) falling.marginRates[size_t(i)] = 20;
    check(falling.rateAt(now) == 40 && falling.rateAt(confirm - TM.waveQuick) == 40 && falling.rateAt(confirm + TM.nextFrames + 1) == 20,
          "the rate does not fall between the own wave and the own next pair (the test is out of date)");
    // His landing ends six frames before the own chain is confirmed: he holds one pair then, and has no move before.
    // He carries 39 points. Without a chain they stay points; with one (his pair clears four, 40 points) they count:
    // 79 points, three garbage pieces at 20.
    const int late = int(confirm - now) - 5;
    check(late >= 1, "the own chain is too short for the test");
    for (bool fires : {false, true}) {
        const std::string at = fires ? " (his pair clears four)" : " (his pair clears nothing)";
        GameField his;
        if (fires) his.f.set(0, 0, 4), his.f.set(0, 1, 4);
        Round r = situation(falling, mine, his, pair);
        r.player[1].phase = Phase::Land, r.player[1].wait = late;
        r.player[1].scoring.pool = 39;
        const JudgePlay game = judgePlay(r, 0, fire, true, [](const JudgePlay& s) { return s.spawns >= 2; });
        check(game.confirm == confirm && game.spawns >= 2 && game.waves == (fires ? 1 : 0) && (game.made > 0) == fires,
              "the game does not play the answer the test expects (" + std::to_string(game.waves) + " waves, " + std::to_string(game.made) + " garbage)" + at);
        const ai::Observation o = ai::observe(r, 0);
        JudgeProbe probe(p);
        probe.startRound(0, 7);
        probe.read(o);
        probe.only(fire);
        const int garbage = probe.answerOf(o, fire);
        check(garbage == game.made, "his answer is read as " + std::to_string(garbage) + " garbage, his waves make " + std::to_string(game.made) + " in the game" + at);
    }
    std::cout << "the opponent's answer: the points he carries count with a chain only, at the rate of its time\n";
}

static void testOutlastLaterAttack() {
    const Rules rules = judgementRules(40);
    ai::Params p;
    p.beamWidth = 30, p.beamDepth = 8, p.beamQueues = 3;
    p.x2Depth = 2;
    const GameField mine = judgementOwnField();
    const Pair pair{4, 4};
    const int fire = placementId({5, UP});
    const Round base = situation(rules, mine, GameField{}, pair);
    const JudgePlay timed = judgePlay(base, 0, fire, false, [](const JudgePlay& s) { return s.confirm >= 0; });
    check(timed.confirm >= 0, "the own chain does not end in the game");
    // His landing ends six frames before the own chain is confirmed: against that chain he has the pair he then holds,
    // which clears nothing. Against the same chain ending 60 frames later he has one move more, and two pairs of
    // colour 4 clear four: a garbage piece at a rate of 40.
    Round r = base;
    r.player[1].phase = Phase::Land, r.player[1].wait = int(timed.confirm - base.frame) - 5;
    check(r.player[1].wait >= 1, "the own chain is too short for the test");
    const ai::Observation o = ai::observe(r, 0);
    for (bool laterFirst : {false, true}) {
        JudgeProbe probe(p);
        probe.startRound(0, 7);
        probe.read(o);
        probe.only(fire);
        const ai::Plan* shot = probe.fireOf(fire);
        check(shot != nullptr && shot->confirmFrame == timed.confirm, "the plan's chain is not confirmed in the frame the game's is");
        const ai::Plan sooner = *shot;
        ai::Plan later = sooner;
        later.endFrame += 60, later.confirmFrame += 60;
        int soon = -1, late = -1;
        if (laterFirst) probe.answersOf(o, fire, later, sooner, late, soon);
        else probe.answersOf(o, fire, sooner, later, soon, late);
        const std::string at = laterFirst ? " (the later attack read first)" : " (the sooner attack read first)";
        check(soon == 0, "his answer to the sooner attack is read as " + std::to_string(soon) + " garbage" + at);
        check(late == 1, "his answer to the later attack is read as " + std::to_string(late) + " garbage" + at);
    }
    std::cout << "the opponent's answer: an attack that ends later is read with a search of its own depth\n";
}

// Rules::chainRequirement 2: a chain of one wave makes no garbage, and the points he carries stay points; a chain of two
// waves counts in full (the points of both waves, and those he carries). What his waves make in the game is what the
// reading must say.
static void testOutlastChainRequirement() {
    ai::Params p;
    p.beamWidth = 30, p.beamDepth = 8, p.beamQueues = 3;
    p.x2Depth = 2;
    const GameField mine = judgementOwnField();
    const Pair pair{4, 4};
    const int fire = placementId({5, UP});
    Rules rules = judgementRules(40);
    rules.chainRequirement = 2;
    rules.softDropBonus = false; // (no points for the down button: the search of his field counts none either)
    const Round first = situation(rules, mine, GameField{}, pair);
    const JudgePlay timed = judgePlay(first, 0, fire, false, [](const JudgePlay& s) { return s.confirm >= 0; });
    check(timed.confirm >= 0, "the own chain does not end in the game");
    // His landing ends six frames before the own chain is over: he holds one pair then, and has no move before. He
    // carries 39 points and puts that pair upright on his first column.
    const int late = int(timed.confirm - first.frame) - 5;
    check(late >= 1, "the own chain is too short for the test");
    for (bool two : {false, true}) {
        const std::string at = two ? " (his pair fires two waves)" : " (his pair fires one wave)";
        GameField his;
        if (two) {
            // The pair of colour 4 clears with the two beside it; the piece of colour 1 above those then falls to
            // the three of colour 1 below: a second wave, 320 points after the 40 of the first.
            his.f.set(0, 0, 1), his.f.set(0, 1, 1);
            his.f.set(1, 0, 2), his.f.set(1, 1, 1), his.f.set(1, 2, 4), his.f.set(1, 3, 4), his.f.set(1, 4, 1);
        } else {
            his.f.set(0, 0, 4), his.f.set(0, 1, 4); // (the pair clears four: one wave, 40 points)
        }
        check(!his.f.hasClear(), "the test field is not what the test expects" + at);
        Round r = situation(rules, mine, his, pair);
        r.player[1].phase = Phase::Land, r.player[1].wait = late;
        r.player[1].scoring.pool = 39;
        const JudgePlay game = judgePlay(r, 0, fire, true, [](const JudgePlay& s) { return s.spawns >= 2; });
        check(game.confirm == timed.confirm && game.spawns >= 2 && game.waves == (two ? 2 : 1) && (game.made > 0) == two,
              "the game does not play the answer the test expects (" + std::to_string(game.waves) + " waves, " + std::to_string(game.made) + " garbage)" + at);
        const ai::Observation o = ai::observe(r, 0);
        JudgeProbe probe(p);
        probe.startRound(0, 7);
        probe.read(o);
        probe.only(fire);
        const int garbage = probe.answerOf(o, fire);
        check(garbage == game.made, "his answer is read as " + std::to_string(garbage) + " garbage, his waves make " + std::to_string(game.made) + " in the game" + at);
    }
    std::cout << "the opponent's answer: a chain of fewer waves than the rules ask for makes no garbage\n";
}

static void testWinQuietUnchecked() {
    const Rules rules = judgementRules(40);
    const GameField mine = judgementOwnField();
    const Pair pair{4, 4};
    // His third column is eleven high and a row of garbage waits above it: it falls when his next pair lands, and no
    // pair of colour 4 clears anything on his field before.
    GameField his;
    for (int y = 0; y < 11; ++y) his.f.set(DEATH_X, y, Cell(1 + y % 2));
    Round r = situation(rules, mine, his, pair);
    r.player[1].incoming.confirmed = W;
    const ai::Observation o = ai::observe(r, 0);
    const int quiet = placementId({0, UP}); // (far from the three of colour 4: it clears nothing)
    for (int depth : {2, 1}) {
        ai::Params p;
        p.beamWidth = 30, p.beamDepth = 8, p.beamQueues = 3;
        p.x2Depth = depth;
        JudgeProbe probe(p);
        probe.startRound(0, 7);
        probe.read(o);
        int most = 0;
        const int fires = probe.firesBelow(quiet, most);
        // Two pairs: below the placement the second pair can fire a small chain, which buries nothing by itself.
        // One pair: no chain below it.
        if (depth == 2) check(fires > 0 && most < W, "the test field has no small chain below the quiet placement (" + std::to_string(fires) + " chains, " + std::to_string(most) + " garbage)");
        else check(fires == 0, "one pair fires a chain below the quiet placement");
        const int id = probe.winOf(o, 1u << quiet);
        check(id == quiet && probe.won(),
              "a round won without firing is not taken as won with plans of " + std::to_string(depth) + " pairs (placement " + std::to_string(id) + ")");
    }
    std::cout << "a round won without firing is won whatever chains stand below the same placement\n";
}

static void testSentAfterHis() {
    const Rules rules = judgementRules(10);
    const GameField mine = judgementOwnField();
    const Pair pair{4, 4};
    const int fire = placementId({5, UP});
    const int waiting = 3;
    // His pair is landing with three garbage pieces waiting above his field. chain: it completes four of colour 3,
    // 40 points - four garbage pieces at a rate of 10, which offset the three. No chain: the three fall on him.
    for (bool chain : {true, false}) {
        GameField his;
        if (chain)
            for (int y = 0; y < 4; ++y) his.f.set(0, y, 3);
        check(his.f.hasClear() == chain, "the test field is not what the test expects");
        Round r = situation(rules, mine, his, pair);
        r.player[1].phase = Phase::Land, r.player[1].wait = 3;
        r.player[1].incoming.confirmed = waiting;
        const ai::Observation o = ai::observe(r, 0);
        const int after = ai::aboveAfterHis(ai::Duel(o), 0, o.frame + 6000);
        check(after == (chain ? 0 : waiting), "what is above his field when his landing is over is not what the test expects (" + std::to_string(after) + ")");
        ai::Params off;
        off.beamWidth = 30, off.beamDepth = 8, off.beamQueues = 3;
        off.xWildEvery = 0; // (he stands still after his landing: what is above him stays there)
        ai::Params on = off;
        on.x3SentAfterHis = 1;
        JudgeProbe before(off), counted(on);
        before.startRound(0, 7), counted.startRound(0, 7);
        before.read(o), counted.read(o);
        const ai::Plan* plain = before.fireOf(fire);
        const ai::Plan* fair = counted.fireOf(fire);
        check(plain != nullptr && fair != nullptr, "the test field has no chain fired with the pair in hand");
        const int left = before.leftAbove(o, *plain);
        const std::string at = chain ? " (his chain runs)" : " (no chain of his)";
        check(left == counted.leftAbove(o, *fair), "the chain leaves another amount above him with the switch" + at);
        check(plain->sent == left - waiting, "x3SentAfterHis 0: the plan does not send what it leaves above him less what waited there" + at);
        check(fair->sent == left - after, "x3SentAfterHis 1: the plan does not send what it leaves above him less what was left of the garbage that waited" + at);
        check(before.answerFrom(o) == waiting, "x3SentAfterHis 0: the surplus of an answer is not counted from what waits above him" + at);
        check(counted.answerFrom(o) == after, "x3SentAfterHis 1: the surplus of an answer is not counted from what his chain leaves above him" + at);
    }
    std::cout << "x3SentAfterHis: garbage his running chain offsets is no part of what a plan sends\n";
}

static void testSentAfterHisSameTime() {
    const Rules rules = judgementRules(100);
    const Pair pair{4, 4};
    const GameField mine{};
    // His pair has just started a chain of two waves, and three garbage pieces wait above his field. The first wave
    // (40 points) makes no garbage at a rate of 100; the second (320 points more) makes three, which offset the three.
    GameField his;
    stairs(his, 0, 2);
    his.f.set(2, 0, 2);
    check(his.f.hasClear(), "the test field is not what the test expects");
    const int waiting = 3;
    Round r = situation(rules, mine, his, pair);
    r.player[1].phase = Phase::Land, r.player[1].wait = 3;
    r.player[1].incoming.confirmed = waiting;
    // The game, with the own seat touching no button: nothing of the three is left and none of them falls.
    const JudgePlay game = judgePlay(r, 0, -1, false, [](const JudgePlay& s) { return s.ended >= 0; });
    check(game.ended >= 0 && game.waves == 2 && game.offset == waiting && game.fell < 0 && game.above == 0,
          "his chain does not offset the three with its second wave in the game (" + std::to_string(game.waves) + " waves, " +
              std::to_string(game.offset) + " offset; the test is out of date)");
    const ai::Observation o = ai::observe(r, 0);
    const int quiet = placementId({5, UP}); // (on the empty own field: it clears nothing)
    for (int counted : {0, 1}) {
        ai::Params p;
        p.beamWidth = 30, p.beamDepth = 8, p.beamQueues = 3;
        p.x2Depth = 1;
        p.xWildEvery = 0;
        p.x3SentAfterHis = counted;
        JudgeProbe probe(p);
        probe.startRound(0, 7);
        probe.read(o);
        const ai::Plan* plan = probe.quietOf(quiet);
        check(plan != nullptr, "the test has no plan of one pair that clears nothing");
        // The own next pair appears while his three still wait: his second wave has not begun.
        check(plan->endFrame <= game.waveAt[1], "the own placement is not over before his second wave (the test is out of date)");
        check(plan->sent == 0, "x3SentAfterHis " + std::to_string(counted) + ": a placement that clears nothing is read as sending " + std::to_string(plan->sent) + " garbage");
    }
    std::cout << "x3SentAfterHis: what a plan leaves above him is taken when his running chain is over, as what it is counted from\n";
}

static void testJudgement() {
    testOutlastPairInHand();
    testOutlastOwnGarbage();
    testOutlastCarriedPoints();
    testOutlastChainRequirement();
    testOutlastLaterAttack();
    testWinQuietUnchecked();
    testSentAfterHis();
    testSentAfterHisSameTime();
}
