#pragma once
#include "preparation.h"

namespace juraic::ai {

// A quiet first placement can retain a main chain on the next visible pair.
// Compare that choice with an incidental single selected by the building beam.
template<class Stop>
int preserveReadyBuild(const Observation& o, const PlanSet& mine, const PlanSet& theirs, int chosen,
                       unsigned legal, const Params& p, uint64_t seed, Stop stop) {
    if (chosen < 0 || !p.readMidgame || !p.checkKnownExchange || p.handWait || mine.pairCount < 2 ||
        o.self().incoming.total() || o.opponent().incoming.total() || chainRunning(o, 1 - o.me) ||
        o.self().scoring.allClear || o.opponent().scoring.allClear ||
        (o.self().phase != Phase::Control && (o.self().phase != Phase::Next || o.busyLeft[o.me])) || stop()) return chosen;
    const Plan* single = nullptr;
    for (const Plan& s : mine.plans)
        if (s.id[0] == chosen && s.count == 1 && s.chains == 1 && s.fireDepth == 1 && s.sent < W && !s.allClear)
            single = &s;
    if (!single) return chosen;
    const Plan* threat = nullptr;
    for (const Plan& t : theirs.plans)
        if (t.count == 1 && t.fireDepth == 1 && !t.allClear && t.sent >= W && (!threat || t.sent > threat->sent)) threat = &t;
    if (!threat) return chosen;
    const unsigned quiet = quietSetupChoices(o, mine, legal);
    const Plan* main = nullptr;
    int retained = single->mainAfter;
    for (const auto* list : {&mine.plans, &mine.tactical, &mine.keeping}) for (const Plan& s : *list) {
        if (s.id[0] == chosen) {
            if (s.allClear) return chosen;
            retained = std::max(retained, s.points);
        }
        if (s.count == 2 && s.fireDepth == 2 && ((quiet >> s.id[0]) & 1u) &&
            !s.allClear && !s.fallen && s.sent >= p.x3ChokeMin && (!main || s.sent > main->sent)) main = &s;
    }
    if (!main || main->sent <= threat->sent || int64_t(retained) * 100 >= int64_t(main->points) * p.x3ChokeKeep || stop()) return chosen;
    const int me = o.me, him = 1 - me;
    for (const auto* list : {&mine.plans, &mine.tactical, &mine.keeping}) for (const Plan& s : *list) {
        if (s.id[0] != chosen || !s.fireDepth) continue;
        if (stop()) return chosen;
        const Duel d = playOut(o, mine, &s, theirs, threat, p, seed, true);
        const auto& us = d.player[me];
        const auto& opp = d.player[him];
        if (opp.moves != o.opponent().moves + 1 || opp.chain.chains != threat->chains || opp.scoring.allClear) return chosen;
        if (!us.fallen && !us.incoming.total() && !us.field.dead()) return chosen;
    }
    // Read the idle opponent and his retained attacks with actual input paths.
    const auto holds = [&](const Plan* enemy) {
        if (stop()) return false;
        const Duel d = playOut(o, mine, main, theirs, enemy, p, seed, true);
        const auto& us = d.player[me];
        const auto& opp = d.player[him];
        return us.moves == o.self().moves + main->count && us.chain.chains == main->chains &&
               !us.field.dead() && !us.fallen && !us.incoming.total() && !us.scoring.allClear &&
               !opp.scoring.allClear && (d.state == (me ? RoundState::Won1 : RoundState::Won0) ||
                                          (d.state == RoundState::Playing && opp.incoming.confirmed >= W));
    };
    if (!holds(nullptr)) return chosen;
    for (const auto* list : {&theirs.plans, &theirs.tactical, &theirs.keeping}) for (const Plan& t : *list)
        if (t.fireDepth && !holds(&t)) return chosen;
    return !stop() ? main->id[0] : chosen;
}

} // namespace juraic::ai
