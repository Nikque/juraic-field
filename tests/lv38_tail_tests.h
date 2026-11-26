#pragma once

// Included by ai_tests.cpp after its common check helper.
static void testLv38Tail() {
    using namespace juraic::ai;

    const auto board = [](const std::array<const char*, W>& columns) {
        GameField field;
        for (int x = 0; x < W; ++x)
            for (int y = 0; columns[x][y]; ++y)
                field.f.set(x, y, Cell(columns[x][y] - '0'));
        return field;
    };
    const Rules rules;
    const Timing timing = DEFAULT_TIMING;
    Params p;
    p.lv38Midgame = 1;
    p.lv38Plan5 = 1;
    p.evKeyed = 0;

    const auto tailAt = [&](const GameField& field, int x, Cell color) {
        Trials choices, tail;
        detect(field, rules, timing, 2, choices, nullptr, false);
        for (int i = 0; i < choices.count; ++i) {
            const Trial& trial = choices.item[i];
            if (trial.x == x && trial.color == color) {
                tail.item[tail.count++] = trial;
                break;
            }
        }
        check(tail.count == 1 && tail.item[0].added == 1 && tail.item[0].chains == 2 && tail.item[0].points == 920,
              "Lv38 tail fixture has an exactly 920-point two-wave fire");
        return tail;
    };

    const GameField positive = board({"33222", "3114122114", "1443323122", "11313", "442211334", "4242123431"});
    const Trials tail = tailAt(positive, 3, 2);
    KeyPlan main;
    buildValue(positive, rules, timing, p, nullptr, &main);
    int reserve = 0;
    const auto shape = lv38_tail::bestShape(positive, tail, main, rules, timing, &reserve);
    check(main.t.points == 4920 && shape.points == 920 && reserve == 3160,
          "Lv38 prefers a legal tail at 920 points and counts the same trigger after gravity");
    check(!lv38_tail::eligible(2, 919) && lv38_tail::eligible(2, 920) &&
          lv38_tail::keepsMain(2460, main) && !lv38_tail::keepsMain(2459, main),
          "Lv38 tail and reserve use inclusive point thresholds");

    const int withTail = buildValue(positive, rules, timing, p, nullptr, nullptr, true);
    p.lv38Plan5 = 0;
    check(withTail > buildValue(positive, rules, timing, p, nullptr, nullptr, true),
          "Lv38 tail quality changes the actual build evaluation");
    p.lv38Plan5 = 1;

    const GameField negative = board({"22111223", "1322323343", "414414423", "242134", "1343221331", "14433211"});
    const Trials wrong = tailAt(negative, 3, 4);
    KeyPlan original;
    buildValue(negative, rules, timing, p, nullptr, &original);
    GameField after = negative;
    after.f.set(3, wrong.item[0].y, 4);
    FieldBits tracked = lv38_tail::supportOf(negative, original, rules, timing);
    lv38_tail::trace(after.f, rules, tracked);
    int anyTrigger = 0;
    buildValue(after, rules, timing, p, &anyTrigger);
    check(original.t.points == 1000 && anyTrigger == 500 &&
          !lv38_tail::bestShape(negative, wrong, original, rules, timing).points,
          "Lv38 rejects a replacement trigger even when it reaches 50 percent");

    Observation attack;
    attack.player[0].field = positive;
    attack.player[0].phase = attack.player[1].phase = Phase::Control;
    attack.player[0].pair = attack.player[1].pair = spawnPair(timing);
    attack.player[0].colors = {2, 4};
    attack.player[0].incoming.confirmed = 7;
    PlanSet mine, theirs;
    mine.midAir = theirs.midAir = true;
    mine.who = 0, theirs.who = 1;
    mine.pairCount = mine.knownCount = 1;
    mine.pair[0] = attack.self().colors;
    Plan returnTail;
    returnTail.id[0] = 3;
    returnTail.count = returnTail.fireDepth = 1;
    returnTail.chains = 2;
    returnTail.points = 920;
    returnTail.mainAfter = 4800;
    mine.plans.push_back(returnTail);
    lv38_tail_answer::Note answer;
    const auto neverStop = [] { return false; };
    check(lv38_tail_answer::prefer(attack, mine, theirs, 1u << 3, 7, p, 1, neverStop, answer) == 3 &&
          answer.offset == 7 && answer.returned == 6 && answer.points == 920 && answer.preserved == 4800,
          "Lv38 chooses the concrete cheap answer when it offsets all seven and sends six back");
    attack.player[0].incoming.confirmed = 8;
    check(lv38_tail_answer::prefer(attack, mine, theirs, 1u << 3, 8, p, 1, neverStop, answer) == -1,
          "Lv38 rejects the same answer with only five garbage to return");

    Observation quiet;
    quiet.player[0].phase = Phase::Next;
    quiet.player[0].wait = 1;
    quiet.player[0].field = positive;
    quiet.player[1].phase = Phase::Control;
    PlanSet shown;
    shown.pairCount = shown.knownCount = 1;
    shown.pair[0] = {4, 1};

    constexpr int ordinary = 3, prepared = 0;
    BuildResult beam;
    beam.best = ordinary;
    beam.depth = 2;
    beam.alive = 1u << ordinary | 1u << prepared;
    beam.sum[ordinary] = 10000;
    beam.sum[prepared] = 9000;
    lv38_tail_build::Note build;
    check(lv38_tail_build::prefer(quiet, shown, beam, ordinary, beam.alive, p, false, neverStop, build) == prepared &&
          build.ordinary == ordinary && build.selected == prepared && build.points == 1210 && build.retained == 3160,
          "Lv38 chooses a quiet placement that preserves a tail at the inclusive ten percent limit");
    --beam.sum[prepared];
    build = {};
    check(lv38_tail_build::prefer(quiet, shown, beam, ordinary, beam.alive, p, false, neverStop, build) == ordinary,
          "Lv38 keeps the ordinary placement when the tail would cost too much");
}
