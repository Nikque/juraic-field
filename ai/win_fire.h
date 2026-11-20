#pragma once
#include "exchange.h"

namespace juraic::ai {

inline bool knownWinFireHolds(const Observation& o, const Plan& fire, const Duel& d) {
    const auto& self = d.player[o.me];
    const auto& rival = d.player[1 - o.me];
    if (rival.fallen % W || self.fallen || self.incoming.total() || self.field.dead() || self.scoring.allClear) return false;
    if (d.state == (o.me ? RoundState::Won1 : RoundState::Won0)) return true;
    if (d.state != RoundState::Playing || self.moves != o.self().moves + 1 || self.chain.chains != fire.chains ||
        self.phase != DuelPhase::Control || rival.scoring.allClear) return false;
    int heights[W];
    rival.field.heights(heights);
    return rival.incoming.confirmed >= W * (12 - heights[DEATH_X]);
}

template<class Stop>
bool knownWinFire(const Observation& o, const PlanSet& mine, const PlanSet& theirs, const Plan& fire,
                  const Params& p, uint64_t seed, const Duel& quiet, Stop stop) {
    if (stop() || !knownWinFireHolds(o, fire, quiet) || theirs.pairCount != theirs.knownCount || theirs.pairCount == 0)
        return false;
    const int enemy = theirs.who;
    Duel start(o, seed);
    if (!theirs.midAir && !toPairOf(start, enemy, o.frame + 6000)) return false;

    Plan answer;
    const auto replies = [&](auto&& visit, unsigned ids, int depth) -> bool {
        for (; ids; ids &= ids - 1) {
            if (stop()) return false;
            answer.count = int8_t(depth + 1);
            answer.id[depth] = int8_t(std::countr_zero(ids));
            const Duel replay = playOut(o, mine, &fire, theirs, &answer, p, seed, true);
            if (!knownWinFireHolds(o, fire, replay)) return false;
            if (replay.state != RoundState::Playing) continue;
            const auto& rival = replay.player[enemy];
            if (rival.moves != o.opponent().moves + depth + 1 || rival.phase != DuelPhase::Control) return false;
            if (depth + 1 < theirs.pairCount &&
                !visit(visit, legalPlacements(rival.field), depth + 1)) return false;
        }
        return true;
    };
    const unsigned first = theirs.midAir ? theirs.firstReach : legalPlacements(start.player[enemy].field);
    return first && replies(replies, first, 0) && !stop();
}

} // namespace juraic::ai
