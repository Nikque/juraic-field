#pragma once
#include "exchange.h"

namespace juraic::ai {

// When both immediate answers spend the main, compare what is left to offset
// after each of the rival's visible replies. A counterable send alone cannot
// tell these two answers apart.
template<class Stop>
int preferCommittedAnswer(const Observation& o, const PlanSet& mine, const PlanSet& theirs,
                          int ordinary, unsigned legal, int arriving, const Params& p, uint64_t seed, Stop stop) {
    if (!p.readMidgame || !p.checkKnownExchange || p.handWait || o.players != 2 || ordinary < 0 ||
        !(legal >> ordinary & 1u) || arriving <= 0 || arriving >= W || arriving != o.self().incoming.confirmed ||
        o.self().incoming.pending || o.opponent().incoming.total() || o.self().scoring.allClear || o.opponent().scoring.allClear ||
        o.busyLeft[o.me] || o.busyLeft[1 - o.me] || o.opponent().phase != Phase::Next ||
        (o.self().phase != Phase::Next && o.self().phase != Phase::Control) ||
        theirs.knownCount != 2 || theirs.pairCount != 2 || !mine.pairCount || stop()) return ordinary;

    const int rate = o.rules.rateAt(o.frame);
    const auto thin = [&](int reserve, int power) {
        return reserve < int64_t(p.x3ChokeMin) * rate &&
            int64_t(reserve) * 100 <= int64_t(power) * (100 - p.x3ChokeKeep);
    };
    const auto spendsMain = [&](const Plan& plan) {
        return plan.count == 1 && plan.fireDepth == 1 && plan.chains >= 5 && !plan.allClear &&
            thin(plan.mainAfter, plan.points) && (legal >> plan.id[0] & 1u);
    };
    const auto original = std::find_if(mine.plans.begin(), mine.plans.end(), [&](const Plan& plan) {
        return plan.id[0] == ordinary && spendsMain(plan);
    });
    if (original == mine.plans.end() || std::any_of(mine.plans.begin(), mine.plans.end(), [&](const Plan& plan) {
            return plan.count == 1 && plan.allClear && (legal >> plan.id[0] & 1u);
        })) return ordinary;

    std::vector<const Plan*> alternatives;
    for (const Plan& plan : mine.plans)
        if (spendsMain(plan) && plan.points > original->points && plan.sent >= original->sent) alternatives.push_back(&plan);
    std::stable_sort(alternatives.begin(), alternatives.end(), [](const Plan* a, const Plan* b) {
        return a->sent == b->sent ? a->endFrame < b->endFrame : a->sent > b->sent;
    });
    if (alternatives.empty()) return ordinary;

    const int me = o.me, him = 1 - me;
    KnownReplay bounded{stop};
    const auto replay = [&](const Plan& fire, const Plan* rival) {
        return playOut(o, mine, &fire, theirs, rival, p, seed, true, nullptr, &bounded);
    };
    const auto burden = [](const DuelPlayer& player) { return player.fallen + player.incoming.total(); };
    const auto noClear = [&](const Duel& d) { return !d.player[me].scoring.allClear && !d.player[him].scoring.allClear; };
    const auto complete = [&](const Duel& d, const Plan& fire) {
        const auto& own = d.player[me];
        return d.state == RoundState::Playing && own.phase == DuelPhase::Control && own.moves == o.self().moves + 1 &&
            own.chain.chains == fire.chains && noClear(d);
    };
    const Duel standing = replay(*original, nullptr);
    if (stop() || !complete(standing, *original) || burden(standing.player[me])) return ordinary;
    int ordinaryReserve = 0;
    buildValue(standing.player[me].field, o.rules, o.timing, p, &ordinaryReserve);
    if (!thin(ordinaryReserve, original->points)) return ordinary;

    struct Reply { Plan route; int own, fall, enemy; bool reached, closesRival; };
    std::vector<Reply> visible;
    unsigned counterRoots = 0;
    int ordinaryWorst = 0;
    const RoundState ownWin = me ? RoundState::Won1 : RoundState::Won0;
    const auto consider = [&](const Plan& plan) {
        if (stop()) return false;
        const Duel d = replay(*original, &plan);
        if (stop()) return false;
        const auto& foe = d.player[him];
        if (foe.moves != o.opponent().moves + plan.count) {
            visible.push_back({plan, 0, 0, 0, false, false});
            return true;
        }
        if (d.state == ownWin && noClear(d)) {
            visible.push_back({plan, 0, 0, 0, true, true});
            return true;
        }
        if (!complete(d, *original)) return false;
        const auto& own = d.player[me];
        const int left = burden(own);
        ordinaryWorst = std::max(ordinaryWorst, left);
        if (left >= W && foe.chain.chains >= 5) {
            int root = plan.id[0];
            if (theirs.pair[0].axis == theirs.pair[0].child) root = std::min(root, reversePlacement(root));
            counterRoots |= 1u << root;
        }
        visible.push_back({plan, left, own.fallen, burden(foe), true, false});
        return true;
    };
    // Keep both orientations in the exchange even for matching colours: the
    // resulting field can be identical while the lock or split time changes.
    for (unsigned starts = legalPlacements(o.opponent().field); starts; starts &= starts - 1) {
        Plan plan;
        plan.count = 1;
        plan.id[0] = int8_t(std::countr_zero(starts));
        if (!consider(plan)) return ordinary;
        plan.count = 2;
        for (int second = 0; second < PLACEMENTS; ++second) {
            plan.id[1] = int8_t(second);
            if (!consider(plan)) return ordinary;
        }
    }
    if (std::popcount(counterRoots) < 2) return ordinary;

    for (const Plan* fire : alternatives) {
        if (stop()) return ordinary;
        const Duel idle = replay(*fire, nullptr);
        if (stop()) return ordinary;
        if (!complete(idle, *fire) || burden(idle.player[me]) || burden(idle.player[him]) < burden(standing.player[him])) continue;
        int reserve = 0;
        buildValue(idle.player[me].field, o.rules, o.timing, p, &reserve);
        if (!thin(reserve, fire->points) || int64_t(ordinaryReserve - reserve) * 1000 > int64_t(original->points) * p.x3GuardNear) continue;

        int worst = 0;
        bool dominates = true;
        for (const Reply& reference : visible) {
            const Duel d = replay(*fire, &reference.route);
            if (stop()) return ordinary;
            const auto& own = d.player[me];
            const auto& foe = d.player[him];
            const bool reached = foe.moves == o.opponent().moves + reference.route.count;
            const auto noWorse = [&] {
                if (!reached) return true;
                if (reference.closesRival) return d.state == ownWin && noClear(d);
                return complete(d, *fire) && own.fallen <= reference.fall && burden(own) <= reference.own &&
                    (burden(own) != reference.own || burden(foe) >= reference.enemy);
            };
            if (reached != reference.reached || !noWorse()) { dominates = false; break; }
            if (reached && !reference.closesRival) worst = std::max(worst, burden(own));
        }
        if (dominates && worst + W <= ordinaryWorst && !stop()) return fire->id[0];
    }
    return ordinary;
}

} // namespace juraic::ai
