#pragma once
#include "decision_metrics.h"
#include "exchange.h"
#include "lv38_tail.h"
#include "lv38_head.h"

namespace juraic::ai::lv38_tail_answer {

struct Note {
    int selected = -1, points = 0, waves = 0, preserved = 0, main = 0, returned = 0, offset = 0, depth = 0;
    bool head = false;
    int keyColumn = -1, keyColor = 0, reservePoints = 0, keyRow = -1;
};

template<class Stop>
int prefer(const Observation& o, const PlanSet& mine, const PlanSet& theirs, unsigned legal, int arriving,
           const Params& p, uint64_t seed, Stop stop, Note& note) {
    metrics::Scope scope(metrics::Part::TailAnswer);
    const int me = o.me, him = 1 - me;
    if (arriving <= 0 || arriving >= 30 || !mine.pairCount || p.handWait || o.busyLeft[me] ||
        o.self().scoring.allClear || o.opponent().scoring.allClear ||
        !lv38_tail::inWindow(o.self().field.f.normal().count()) || stop()) return -1;
    KeyPlan main;
    readMainChain(o.self().field, o.rules, o.timing, p, nullptr, &main);
    const FieldBits support = p.lv38Plan5 > 0 ? lv38_tail::supportOf(o.self().field, main, o.rules, o.timing) : FieldBits{};
    const auto head = p.lv38Plan12 > 0 || p.lv38HeadCut > 0
        ? lv38_head::shape(o.self().field, main, o.rules, o.timing) : lv38_head::Shape{};
    const FieldBits ignition = head.valid() ? lv38_tail::supportOf(o.self().field, KeyPlan{head.fire}, o.rules, o.timing) : FieldBits{};
    if (support.empty() && ignition.empty()) return -1;
    const auto shortFire = [&](int waves, int points) {
        return (!support.empty() && lv38_tail::eligible(waves, points)) ||
            (!ignition.empty() && lv38_head::eligible(waves, points));
    };

    const auto eligible = [&](const Plan& plan) {
        return plan.fireDepth == plan.count && plan.count >= 1 && plan.count <= 3 &&
            plan.count <= mine.knownCount && (legal >> plan.id[0] & 1) && !plan.allClear &&
            shortFire(plan.chains, plan.points) && lv38_tail::keepsMain(plan.mainAfter, main);
    };
    std::vector<const Plan*> candidates;
    for (const auto* list : {&mine.plans, &mine.keeping, &mine.tactical})
        for (const Plan& plan : *list)
            if (eligible(plan)) candidates.push_back(&plan);
    if (candidates.empty() || stop()) return -1;
    const auto byPlacement = [](const Plan* a, const Plan* b) {
        return std::lexicographical_compare(a->id, a->id + a->count, b->id, b->id + b->count);
    };
    std::sort(candidates.begin(), candidates.end(), byPlacement);
    candidates.erase(std::unique(candidates.begin(), candidates.end(), [](const Plan* a, const Plan* b) {
        return samePlacements(*a, *b);
    }), candidates.end());
    std::stable_sort(candidates.begin(), candidates.end(), [](const Plan* a, const Plan* b) {
        if (a->endFrame != b->endFrame) return a->endFrame < b->endFrame;
        return a->mainAfter > b->mainAfter;
    });

    KnownReplay replay{stop};
    DuelExchangeTrace trace;
    replay.trace = &trace;
    int64_t bestReady = -1;
    Note best;
    for (const Plan* plan : candidates) {
        if (stop()) return -1;
        trace = {};
        trace.initialMoves[me] = o.self().moves;
        trace.initialMoves[him] = o.opponent().moves;
        GameField firing;
        bool placed = false;
        replay.acceptPlacement = [&](const Duel& d, int who, Placement target, Pair pair, int64_t, int64_t lock) {
            if (lock < 0) return false;
            if (who != me) return true;
            const auto& before = d.player[me];
            if (before.fallen || (before.moves > o.self().moves && before.chain.chains)) return false;
            if (before.moves - o.self().moves + 1 == plan->count) {
                firing = before.field;
                juraic::place(firing, target, pair);
                placed = true;
            }
            return true;
        };
        const Duel after = playOut(o, mine, plan, theirs, nullptr, p, seed, true, nullptr, &replay);
        if (stop()) return -1;
        const auto& own = after.player[me];
        const auto& rival = after.player[him];
        const auto& fire = trace.turns[me][plan->count];
        const int returned = rival.fallen + rival.incoming.total() - aboveOther(o, me, p);
        if (!placed || own.moves != o.self().moves + plan->count || own.phase != DuelPhase::Control ||
            own.fallen || own.incoming.total() || own.scoring.allClear || returned < W || fire.confirmed < 0 ||
            !shortFire(fire.waves, fire.points)) continue;
        if (best.selected >= 0 && fire.confirmed > bestReady) continue;

        BitField remaining = firing.f;
        FieldBits tracked = support;
        const auto fired = lv38_tail::trace(remaining, o.rules, tracked);
        if (fired.waves != fire.waves || fired.points != fire.points || remaining != own.field.f) continue;
        int preserved = 0;
        if (!support.empty() && lv38_tail::eligible(fire.waves, fire.points))
            preserved = lv38_tail::retainedPoints(remaining, main, tracked, o.rules, o.timing);
        Trial reserve;
        if (!ignition.empty() && lv38_head::eligible(fire.waves, fire.points)) {
            Wave first;
            int h[W];
            o.self().field.heights(h);
            const int y = h[main.keyX] + (main.keyX == main.t.x ? head.fire.added : 0);
            const bool keyIsPlaced = y < ROW_14 && firing.f.get(main.keyX, y) == main.keyColor;
            if (!keyIsPlaced && findWaveFor(firing.f, first, o.rules.clearCount, false) && (first.cleared & ignition) == ignition)
                reserve = lv38_head::retained(own.field, main, o.rules, o.timing);
        }
        const bool usesHead = lv38_tail::keepsMain(reserve.points, main);
        if (usesHead) {
            int keyedReserve = 0;
            readMainChain(own.field, o.rules, o.timing, p, &keyedReserve);
            preserved = std::max({preserved, keyedReserve, reserve.points});
        }
        if (!lv38_tail::keepsMain(preserved, main) ||
            (best.selected >= 0 && fire.confirmed == bestReady && preserved <= best.preserved)) continue;
        best = {plan->id[0], fire.points, fire.waves, preserved, main.t.points, returned, fire.offset, plan->count};
        if (usesHead) {
            best.head = true;
            best.keyColumn = reserve.x;
            best.keyColor = reserve.color;
            best.reservePoints = reserve.points;
            best.keyRow = reserve.y;
        }
        bestReady = fire.confirmed;
    }
    if (stop()) return -1;
    note = best;
    return best.selected;
}

} // namespace juraic::ai::lv38_tail_answer
