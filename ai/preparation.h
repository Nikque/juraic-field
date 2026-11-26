#pragma once
#include "exchange.h"
#include <compare>

namespace juraic::ai {

inline unsigned quietSetupChoices(const Observation& o, const PlanSet& mine, unsigned choices) {
    const GameField& field = o.self().field;
    choices &= legalPlacements(field);
    if (mine.firstInAir()) choices &= mine.firstReach;
    unsigned quiet = 0;
    for (; choices; choices &= choices - 1) {
        const int id = std::countr_zero(choices);
        GameField next = field;
        juraic::place(next, placement(id), mine.pair[0]);
        if (!next.dead() && !anyClear(next.f, o.rules)) quiet |= 1u << id;
    }
    return quiet;
}

struct SetupThreat {
    const Plan* fire = nullptr;
    int sent = 0;
    int64_t reveal = 0;
};

struct SetupReadiness {
    bool answers = false;
    int readiness = 0;
    int rows = 0;
    auto operator<=>(const SetupReadiness&) const = default;
};

// Use the strongest visible immediate short fire as a concrete offset test.
inline SetupThreat setupThreat(const Observation& o, const PlanSet& mine, const PlanSet& theirs,
                               const Params& p, uint64_t seed) {
    const Plan* fire = nullptr;
    for (const auto* list : {&theirs.plans, &theirs.tactical}) for (const Plan& plan : *list) {
        if (plan.count != 1 || plan.fireDepth != 1 || plan.chains < 1 || plan.chains > 4 || plan.allClear || plan.sent < W) continue;
        if (!fire || plan.sent > fire->sent || (plan.sent == fire->sent && plan.endFrame < fire->endFrame)) fire = &plan;
    }
    if (!fire) return {};
    const Duel shown = playOut(o, mine, nullptr, theirs, fire, p, seed, true);
    const int sent = shown.player[mine.who].incoming.total();
    const auto& rival = shown.player[theirs.who];
    if (rival.moves != o.opponent().moves + 1 || rival.chain.chains != fire->chains || rival.scoring.allClear || sent < W) return {};
    GameField field = o.opponent().field;
    const Placement target = placement(fire->id[0]);
    const auto placed = juraic::place(field, target, theirs.pair[0]);
    // Full landing time is an upper bound on when the first wave is visible; with FASTEST_BOUND no sooner than the
    // straight way shows the chain, and never when the straight way fires none.
    const int64_t landed = rival.lockFrame + o.timing.landFrames + o.timing.splitFrames(placed.split, target.x, placed.lockY);
    const int64_t straight = rivalSeenCheck(o, mine, nullptr, theirs, fire, p, seed, landed);
    return {fire, sent, straight < 0 ? INT64_MAX : straight};
}

inline bool keepsSetupMain(const Observation& o, const Duel& d, const Plan& plan, const Params& p, int required) {
    const auto& self = d.player[o.me];
    if (self.moves != o.self().moves + plan.count || self.chain.chains != plan.chains || self.scoring.allClear ||
        self.phase != DuelPhase::Control || self.fallen || self.incoming.total()) return false;
    int main = 0;
    buildValue(self.field, o.rules, o.timing, p, &main);
    return main >= required && main > self.scoring.score - o.self().scoring.score;
}

// Narrow the quiet beam placements only when one has a demonstrated short fire
// with its main chain held in reserve. A zero mask means the reading ran out of time.
template<class Stop>
unsigned preparationChoices(const Observation& o, const PlanSet& mine, const PlanSet& theirs, const BuildResult& b,
                            int chosen, unsigned choices, const Params& p, uint64_t seed, Stop stop, int ahead = 1) {
    if (p.setupSideNear <= 0 || mine.knownCount < 2 || b.cut || o.self().scoring.allClear || std::popcount(choices) < 2) return choices;
    if (stop()) return 0;
    int64_t best = 0;
    for (unsigned pending = choices; pending; pending &= pending - 1) best = std::max(best, b.sum[std::countr_zero(pending)]);
    if (best <= 0) return choices;
    const int slack = std::clamp(p.setupSideNear, 0, 1000);
    unsigned eligible = 1u << chosen;
    for (unsigned pending = choices; pending; pending &= pending - 1) {
        const int id = std::countr_zero(pending);
        if (b.sum[id] * 1000 >= best * (1000 - slack)) eligible |= 1u << id;
    }
    int main = 0;
    buildValue(o.self().field, o.rules, o.timing, p, &main);
    const int required = int((int64_t(main) * std::clamp(p.x3PokeKeep, 0, 100) + 99) / 100);
    if (stop()) return 0;
    const SetupThreat threat = setupThreat(o, mine, theirs, p, seed);
    std::array<int64_t, PLACEMENTS> nextOwn;
    nextOwn.fill(-1);
    // Among concrete side fires, favour a clean offset, then readiness and amount.
    std::array<SetupReadiness, PLACEMENTS> rating{};
    for (const auto* list : {&mine.plans, &mine.tactical}) for (const Plan& plan : *list) {
        if (stop()) return 0;
        const int id = plan.id[0];
        if (!(eligible >> id & 1) || plan.fireDepth < 2 || plan.fireDepth != plan.count || plan.count > mine.knownCount ||
            plan.chains < 1 || plan.chains > 4 || plan.allClear || plan.sent < W || plan.fallen ||
            plan.mainAfter < required || plan.mainAfter <= plan.points) continue;
        const int readiness = MIDGAME_PLAN_PAIRS + 1 - plan.fireDepth;
        if ((!threat.fire || rating[id].answers) && readiness < rating[id].readiness) continue;
        const Duel quiet = playOut(o, mine, &plan, theirs, nullptr, p, seed, true);
        const auto& other = quiet.player[theirs.who];
        const int sent = other.fallen + other.incoming.total();
        if (sent < W || !keepsSetupMain(o, quiet, plan, p, required)) continue;
        bool answers = false;
        if (threat.fire && sent >= threat.sent && !stop()) {
            if (nextOwn[id] < 0) {
                Plan first = plan;
                first.count = 1;
                const auto hold = playOut(o, mine, &first, theirs, nullptr, p, seed, true);
                nextOwn[id] = std::max<int64_t>(0, hold.player[mine.who].spawnFrame);
            }
            if (seenBeforeChoice(threat.reveal, nextOwn[id], ahead, p.seeLate) && !stop()) {
                const Duel exchange = playOut(o, mine, &plan, theirs, threat.fire, p, seed, true);
                const auto& rival = exchange.player[theirs.who];
                answers = rival.moves == o.opponent().moves + 1 && rival.chain.chains == threat.fire->chains &&
                    keepsSetupMain(o, exchange, plan, p, required);
            }
        }
        rating[id] = std::max(rating[id], SetupReadiness{answers, readiness, sent / W});
    }
    if (stop()) return 0;
    const SetupReadiness strongest = *std::max_element(rating.begin(), rating.end());
    if (strongest == SetupReadiness{}) return choices;
    unsigned prepared = 0;
    for (unsigned pending = eligible; pending; pending &= pending - 1) {
        const int id = std::countr_zero(pending);
        if (rating[id] == strongest) prepared |= 1u << id;
    }
    return prepared;
}

} // namespace juraic::ai
