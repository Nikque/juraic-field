#pragma once
#include "preparation.h"

namespace juraic::ai::lv38_foundation {

struct Threat {
    const Plan* plan = nullptr;
    FieldBits support{};
};

struct Options {
    const Plan* keeps = nullptr;
    const Plan* quickest = nullptr;
    const Plan* pair[2]{};
    std::vector<Threat> distinct; // First-wave supports for the same quiet first move.

    int count() const { return int(keeps != nullptr) + int(pair[0] != nullptr); }
};

inline FieldBits firstSupport(const Observation& o, const PlanSet& mine, const Plan& plan) {
    GameField field = o.self().field;
    for (int step = 0; step + 1 < plan.fireDepth; ++step) {
        place(field, placement(plan.id[step]), mine.pair[step]);
        if (anyClear(field.f, o.rules)) {
            Trial chain{};
            runTrialChain(field.f, o.rules, o.timing, chain, false);
        }
    }
    const FieldBits stock = field.f.normal();
    const int step = plan.fireDepth - 1;
    place(field, placement(plan.id[step]), mine.pair[step]);
    Wave wave{};
    return findWaveFor(field.f, wave, o.rules.clearCount) ? wave.cleared & stock : FieldBits{};
}

inline bool sameSetup(const Plan& a, const Plan& b) {
    return a.fireDepth == b.fireDepth && std::equal(a.id, a.id + a.fireDepth - 1, b.id);
}

template<class Stop>
int prefer(const Observation& o, const PlanSet& mine, const PlanSet& rival, const BuildResult& beam,
           int ordinary, unsigned allowed, const Params& p, uint64_t seed, Stop stop) {
    if (ordinary < 0 || mine.pairCount < 2 || beam.cut || o.rules.clearCount != 4 ||
        (o.self().phase != Phase::Control && (o.self().phase != Phase::Next || o.busyLeft[o.me])) ||
        o.self().incoming.total() || o.opponent().incoming.total()) return ordinary;
    const int colors = o.self().field.f.normal().count();
    if (colors < 14 || colors > 44 || stop()) return ordinary;

    const unsigned quiet = quietSetupChoices(o, mine, allowed & beam.alive);
    if (!(quiet >> ordinary & 1)) return ordinary;
    const int rate = o.rules.rateAt(o.frame);
    int standing = 0;
    for (const Plan& plan : mine.plans)
        if (!plan.fireDepth && plan.id[0] == ordinary) standing = std::max(standing, plan.mainAfter);

    std::array<Options, PLACEMENTS> options;
    const auto collect = [&](const std::vector<Plan>& plans) {
        for (const Plan& plan : plans) {
            if (plan.fireDepth < 2 || plan.fireDepth > 3 || plan.fireDepth != plan.count ||
                !(quiet >> plan.id[0] & 1) || plan.chains < 1 || plan.chains > 3 ||
                plan.points < W * rate || plan.sent < W || plan.allClear) continue;
            const int id = plan.id[0];
            if (beam.sum[id] * 100 < beam.sum[ordinary] * 82 || stop()) continue;
            Options& root = options[id];
            if (!root.quickest || plan.fireDepth < root.quickest->fireDepth ||
                (plan.fireDepth == root.quickest->fireDepth && plan.endFrame < root.quickest->endFrame))
                root.quickest = &plan;
            if (plan.mainAfter >= 2280 && int64_t(plan.mainAfter) * 100 >= int64_t(standing) * 85 &&
                (!root.keeps || plan.endFrame < root.keeps->endFrame)) root.keeps = &plan;
            if (root.pair[0]) continue;
            const FieldBits support = firstSupport(o, mine, plan);
            if (!support.any()) continue;
            for (const Threat& other : root.distinct) {
                if (sameSetup(*other.plan, plan) && other.support.disjoint(support)) {
                    root.pair[0] = other.plan;
                    root.pair[1] = &plan;
                    break;
                }
            }
            if (!root.pair[0]) root.distinct.push_back({&plan, support});
        }
    };
    collect(mine.plans);
    collect(mine.keeping);
    collect(mine.tactical);
    if (stop()) return ordinary;

    KnownReplay bounded{stop, {}, {}, {}};
    const auto ready = [&](int id) {
        Plan move;
        move.id[0] = int8_t(id);
        move.count = 1;
        ReplayCheckpoint next;
        const Duel after = playOut(o, mine, &move, rival, nullptr, p, seed, true, nullptr, &bounded, nullptr, &next);
        return next.duel.rules && after.player[o.me].moves == o.self().moves + 1 ? next.duel.frame : int64_t{0};
    };
    const int need = options[ordinary].count();
    const Plan* normalQuickest = options[ordinary].quickest;
    unsigned choices = 0;
    for (unsigned ids = quiet; ids; ids &= ids - 1) {
        const int id = std::countr_zero(ids);
        const auto& root = options[id];
        // Additional shapes at the same visible depth are not an earlier
        // option to attack. Preserve the ordinary setup in that case.
        if (root.count() > need && root.quickest &&
            (!normalQuickest || root.quickest->fireDepth < normalQuickest->fireDepth)) choices |= 1u << id;
    }
    if (!choices) return ordinary;
    const int64_t normalReady = ready(ordinary);
    if (!normalReady) return ordinary;
    const auto playable = [&](const Plan& plan) {
        const Duel after = playOut(o, mine, &plan, rival, nullptr, p, seed, true, nullptr, &bounded);
        const auto& ours = after.player[o.me];
        const auto& other = after.player[1 - o.me];
        const int points = ours.scoring.score - o.self().scoring.score;
        const int sent = other.incoming.total() - o.opponent().incoming.total() + other.fallen;
        const int used = colouredPieces(o.self().field) + 2 * plan.count - colouredPieces(ours.field);
        // The button path can carry a few more soft-drop points than the
        // table. The chain, consumed pieces and useful garbage must agree.
        if (stop() || ours.moves != o.self().moves + plan.count || ours.phase == DuelPhase::Dead ||
            ours.chain.chains != plan.chains || used != plan.used || sent < W ||
            std::abs(sent - plan.sent) > 1 || std::abs(points - plan.points) > (H + 2) * plan.count) return false;
        if (plan.mainAfter <= 0) return true;
        int remains = 0;
        buildValue(ours.field, o.rules, o.timing, p, &remains);
        return remains >= plan.mainAfter;
    };
    for (int tries = 0; choices && tries < 3 && !stop(); ++tries) {
        int selected = std::countr_zero(choices);
        for (unsigned ids = choices; ids; ids &= ids - 1) {
            const int id = std::countr_zero(ids);
            if (options[id].count() > options[selected].count() ||
                (options[id].count() == options[selected].count() && beam.sum[id] > beam.sum[selected])) selected = id;
        }
        choices &= ~(1u << selected);
        const int64_t nextReady = ready(selected);
        if (!nextReady || nextReady > normalReady + o.timing.nextFrames + 6) continue;
        const Options& root = options[selected];
        if (!playable(*root.quickest)) continue;
        int usable = root.keeps && playable(*root.keeps);
        usable += root.pair[0] && playable(*root.pair[0]) && playable(*root.pair[1]);
        if (!stop() && usable > need) return selected;
    }
    return ordinary;
}

} // namespace juraic::ai::lv38_foundation
