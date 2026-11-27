#pragma once
#include "decision_metrics.h"
#include "preparation.h"

namespace juraic::ai::lv38_ready_cover {

struct Fire {
    const Plan* plan = nullptr;
    int64_t confirm = -1;
    int sent = 0;
};

inline bool visibleFire(const Plan& plan, const PlanSet& shown) {
    return plan.count > 0 && plan.count == plan.fireDepth && plan.count <= shown.knownCount &&
           !plan.allClear && !plan.fallen;
}

// A quiet placement opens a known main that can cover an imminent short attack.
// Keep the ordinary build when it already has an answer, a path cannot be read,
// or the opponent can also fire a main with the shown pairs.
template<class Stop>
int prefer(const Observation& o, const PlanSet& mine, const PlanSet& theirs, int ordinary,
           unsigned choices, const Params& p, uint64_t seed, Stop stop) {
    metrics::Scope scope(metrics::Part::ReadyCover);
    if (!p.lv38Midgame || !p.lv38ReadyCover || o.players != 2 || p.handWait || ordinary < 0 || mine.knownCount < 2 ||
        !theirs.knownCount || o.busyLeft[o.me] || chainRunning(o, o.me) || chainRunning(o, 1 - o.me) ||
        o.self().incoming.total() || o.opponent().incoming.total() ||
        o.self().scoring.allClear || o.opponent().scoring.allClear || stop()) return ordinary;
    const unsigned quiet = quietSetupChoices(o, mine, choices);
    if (!(quiet >> ordinary & 1) || !(quiet & ~(1u << ordinary))) return ordinary;
    bool hasMain = false;
    for (const auto* list : {&mine.plans, &mine.keeping, &mine.tactical}) for (const Plan& fire : *list) {
        if (fire.chains < 5 || !visibleFire(fire, mine)) continue;
        if (fire.id[0] == ordinary) return ordinary;
        if (fire.count >= 2 && (quiet >> fire.id[0] & 1)) hasMain = true;
    }
    if (!hasMain || stop()) return ordinary;
    const int me = o.me, him = 1 - me;
    KnownReplay replay{stop};
    replay.acceptPlacement = [](const Duel&, int, Placement, Pair, int64_t, int64_t lock) { return lock >= 0; };
    const auto run = [&](const Plan* own, const Plan* enemy, int64_t& ownIgnition) {
        // The final-check output belongs to the second plan set.
        return playOut(o, theirs, enemy, mine, own, p, seed, true, &ownIgnition, &replay);
    };
    const auto fired = [&](const Duel& d, int side, const Plan& plan) {
        const auto& player = d.player[side];
        return player.moves == o.player[side].moves + plan.count && player.chain.chains == plan.chains &&
               !player.scoring.allClear;
    };

    constexpr size_t maxReplies = 12;
    std::array<Fire, maxReplies> threats{};
    size_t count = 0;
    int mostSent = 0;
    int64_t deadline = INT64_MAX;
    for (const auto* list : {&theirs.plans, &theirs.keeping, &theirs.tactical}) for (const Plan& fire : *list) {
        if (fire.allClear && fire.count > 0 && fire.count == fire.fireDepth &&
            fire.count <= theirs.knownCount) return ordinary;
        if (!visibleFire(fire, theirs) || !fire.sent) continue;
        if (fire.chains >= 5) return ordinary;
        const auto end = threats.begin() + count;
        if (std::any_of(threats.begin(), end, [&](const Fire& t) { return samePlacements(fire, *t.plan); })) continue;
        if (count == maxReplies || stop()) return ordinary;
        int64_t unused;
        const Duel alone = run(nullptr, &fire, unused);
        if (stop() || !fired(alone, him, fire) || alone.player[him].phase != DuelPhase::Control ||
            alone.player[him].fallen || alone.player[me].fallen) return ordinary;
        const int sent = alone.player[me].incoming.total();
        const int64_t confirm = alone.player[him].spawnFrame - o.timing.nextFrames;
        if (confirm <= o.frame) return ordinary;
        if (!sent) continue;
        threats[count++] = {&fire, confirm, sent};
        mostSent = std::max(mostSent, sent);
        if (sent >= W) deadline = std::min(deadline, confirm);
    }
    if (deadline == INT64_MAX || stop()) return ordinary;

    // At most two replays of our own main are considered. On a crowded
    // observation, incomplete information leaves the original choice intact.
    constexpr size_t maxMains = 2;
    std::array<const Plan*, maxMains> candidates{};
    for (const auto* list : {&mine.plans, &mine.keeping, &mine.tactical}) for (const Plan& fire : *list) {
        if (!visibleFire(fire, mine)) continue;
        const int id = fire.id[0];
        if (id == ordinary && (fire.chains >= 5 || fire.sent >= mostSent)) return ordinary;
        if (!(quiet >> id & 1) || id == ordinary || fire.count < 2 || fire.chains < 5 ||
            fire.sent < mostSent || fire.lockFrame > deadline) continue;
        if (std::any_of(candidates.begin(), candidates.end(), [&](const Plan* plan) {
                return plan && samePlacements(fire, *plan);
            })) continue;
        const auto better = [&](const Plan* other) {
            return !other || fire.lockFrame < other->lockFrame ||
                   (fire.lockFrame == other->lockFrame && fire.sent > other->sent);
        };
        for (size_t k = 0; k < candidates.size(); ++k) if (better(candidates[k])) {
            if (k + 1 < candidates.size()) candidates[k + 1] = candidates[k];
            candidates[k] = &fire;
            break;
        }
    }
    for (const Plan* main : candidates) {
        if (!main || stop()) break;
        int64_t ignition;
        const Duel alone = run(main, nullptr, ignition);
        const int64_t ready = alone.player[me].spawnFrame;
        if (stop()) return ordinary;
        if (!fired(alone, me, *main) || ignition < o.frame || ignition > deadline ||
            ready <= ignition || alone.player[him].incoming.total() < mostSent) continue;
        bool safe = true;
        for (size_t k = 0; k < count && safe; ++k) {
            const Fire& pressure = threats[k];
            if (stop()) return ordinary;
            const Duel together = run(main, pressure.plan, ignition);
            const auto& us = together.player[me];
            safe = fired(together, me, *main) && fired(together, him, *pressure.plan) &&
                   ignition >= o.frame && (pressure.sent < W || ignition <= pressure.confirm) &&
                   !us.field.dead() && !us.fallen && !us.incoming.total() &&
                   us.spawnFrame == ready && us.phase == DuelPhase::Control;
        }
        if (safe && !stop()) return main->id[0];
    }
    return ordinary;
}

} // namespace juraic::ai::lv38_ready_cover
