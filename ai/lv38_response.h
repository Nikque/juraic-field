#pragma once

#include "exchange.h"

namespace juraic::ai::lv38_response {

inline bool keepsReserve(int points, int reserve, int main, int keep = 85) {
    return int64_t(reserve) * 100 >= int64_t(main) * keep && reserve > points;
}

// Try a short, complete offset before committing the main chain. Checking the
// locks and the resulting board also catches fast paths lost in Gaze's buckets.
template<class Stop>
int offset(const Observation& o, const Duel& afterChain, const PlanSet& mine, const PlanSet& theirs,
           unsigned legal, int arriving, const Params& p, uint64_t seed, Stop stop, int* newlyEligible = nullptr) {
    const auto& rival = afterChain.player[1 - o.me];
    const int me = o.me;
    if (o.players != 2 || p.handWait || arriving < W || arriving > 5 * W ||
        o.self().incoming.confirmed || o.self().scoring.allClear || o.opponent().scoring.allClear ||
        o.opponent().incoming.total() || o.busyLeft[me] || !mine.knownCount ||
        rival.chain.chains < 1 || rival.chain.chains > 4 || rival.scoring.allClear || rival.fallen ||
        rival.phase != DuelPhase::Control || rival.spawnFrame <= o.frame || stop()) return -1;

    int main = 0;
    buildValue(o.self().field, o.rules, o.timing, p, &main);
    if (main < 6000) return -1;
    const int keep = p.lv38Midgame && p.lv38Plan11 > 0 ? p.lv38ResponseKeep : 85;
    KnownReplay bounded{stop};
    int best = -1, bestCount = 0, bestReserve = 0;
    int64_t bestEnd = 0;
    const auto verify = [&](const Plan& plan, const ReplayCheckpoint* start = nullptr) {
        ReplayCheckpoint finish;
        const auto duel = playOut(o, mine, &plan, theirs, nullptr, p, seed, true, nullptr, &bounded, start, &finish);
        const auto& own = duel.player[me];
        if (!finish.duel.rules || own.moves != o.self().moves + plan.count ||
            own.chain.chains < 1 || own.chain.chains > 4 ||
            own.fallen || own.incoming.total() || own.scoring.allClear ||
            own.lockFrame > rival.spawnFrame || stop()) return;
        // If the reply confirms before the original attack has ended, require
        // that the rival finish too. Later waves can add more incoming.
        const auto end = playOut(o, mine, &plan, theirs, nullptr, p, seed, true, nullptr, &bounded, &finish);
        const auto& settled = end.player[me];
        if (end.player[1 - me].phase != DuelPhase::Control || settled.phase != DuelPhase::Control ||
            settled.fallen || settled.incoming.total() || stop()) return;
        int reserve = 0;
        buildValue(settled.field, o.rules, o.timing, p, &reserve);
        const int points = settled.scoring.score - o.self().scoring.score;
        if (newlyEligible && keepsReserve(points, reserve, main, p.lv38ResponseKeep) &&
            !keepsReserve(points, reserve, main, 85)) ++*newlyEligible;
        if (!keepsReserve(points, reserve, main, keep)) return;
        if (best < 0 || plan.count < bestCount ||
            (plan.count == bestCount && (own.spawnFrame < bestEnd ||
                (own.spawnFrame == bestEnd && reserve > bestReserve)))) {
            best = plan.id[0];
            bestCount = plan.count;
            bestReserve = reserve;
            bestEnd = own.spawnFrame;
        }
    };

    // Geometry only screens non-clearing placements. Timing and offsets are
    // decided by a replay with the observed pairs and actual button paths.
    const unsigned roots = legal & (mine.firstInAir() ? mine.firstReach : legalPlacements(o.self().field));
    unsigned quiet = 0;
    for (unsigned moves = roots; moves; moves &= moves - 1) {
        if (stop()) return -1;
        Plan plan;
        plan.count = 1;
        plan.id[0] = int8_t(std::countr_zero(moves));
        GameField first = o.self().field;
        place(first, placement(plan.id[0]), mine.pair[0]);
        if (anyClear(first.f, o.rules)) verify(plan);
        else if (!first.dead()) quiet |= 1u << plan.id[0];
    }
    if (best >= 0 || mine.knownCount < 2) return stop() ? -1 : best;
    for (; quiet; quiet &= quiet - 1) {
        if (stop()) return -1;
        Plan plan;
        plan.count = 1;
        plan.id[0] = int8_t(std::countr_zero(quiet));
        ReplayCheckpoint prefix;
        const auto duel = playOut(o, mine, &plan, theirs, nullptr, p, seed, true, nullptr, &bounded, nullptr, &prefix);
        const auto& own = duel.player[me];
        // The next decision must still see the attack that justified this setup.
        if (!prefix.duel.rules || own.moves != o.self().moves + 1 || own.fallen ||
            duel.player[1 - me].phase != DuelPhase::Check) continue;
        plan.count = 2;
        for (unsigned moves = knownPlacements(own.field, mine, 1, p); moves; moves &= moves - 1) {
            if (stop()) return -1;
            plan.id[1] = int8_t(std::countr_zero(moves));
            GameField next = own.field;
            place(next, placement(plan.id[1]), mine.pair[1]);
            if (anyClear(next.f, o.rules)) verify(plan, &prefix);
        }
    }
    return stop() ? -1 : best;
}

// A clean quiet placement or an ordinary short answer remains the first
// choice. Replace only a proved first drop or a main spent on this small attack.
template<class Stop>
int prefer(const Observation& o, const Duel& afterChain, const PlanSet& mine, const PlanSet& theirs,
           int ordinary, unsigned legal, int arriving, const Params& p, uint64_t seed, Stop stop,
           int* newlyEligible = nullptr) {
    const int me = o.me, him = 1 - me;
    if (o.players != 2 || p.handWait || ordinary < 0 || !(legal >> ordinary & 1u) || !mine.knownCount ||
        arriving < W || arriving > 5 * W || o.self().incoming.confirmed || o.opponent().incoming.total() ||
        o.self().scoring.allClear || o.opponent().scoring.allClear || o.busyLeft[me] ||
        afterChain.player[him].chain.chains < 1 || afterChain.player[him].chain.chains > 4 || stop()) return ordinary;

    Plan path;
    path.count = 1;
    path.id[0] = int8_t(ordinary);
    ReplayCheckpoint finish;
    KnownReplay replay{stop};
    const auto baseline = playOut(o, mine, &path, theirs, nullptr, p, seed, true, nullptr, &replay, nullptr, &finish);
    const auto& own = baseline.player[me];
    const auto& rival = baseline.player[him];
    if (!finish.duel.rules || own.moves != o.self().moves + 1 || own.scoring.allClear || rival.phase == DuelPhase::Dead ||
        baseline.state != RoundState::Playing || stop()) return ordinary;
    if (own.chain.chains >= 5) {
        int before = 0, reserve = 0, opponentMain = 0;
        buildValue(o.self().field, o.rules, o.timing, p, &before);
        buildValue(own.field, o.rules, o.timing, p, &reserve);
        if (int64_t(reserve) * 100 >= int64_t(before) * 85) return ordinary;
        buildValue(afterChain.player[him].field, o.rules, o.timing, p, &opponentMain);
        // Preserve a main that can overwhelm what the opponent demonstrably holds.
        const int counter = rival.fallen + rival.incoming.total();
        if (int64_t(counter) * o.rules.rateAt(o.frame) > opponentMain) return ordinary;
    } else if (own.chain.chains || own.fallen < W) {
        return ordinary;
    }

    const int alternative = offset(o, afterChain, mine, theirs, legal, arriving, p, seed, stop, newlyEligible);
    return alternative >= 0 ? alternative : ordinary;
}

} // namespace juraic::ai::lv38_response
