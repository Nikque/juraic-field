#pragma once
#include "gaze.h"

namespace juraic::ai {
namespace setup_room {

struct Landing {
    GameField field;
    int frames, check, soft;
};

inline std::optional<Landing> quietLanding(const Observation& o, Pair pair, int id, InputPlanner& planner, bool picture = false) {
    const auto& self = o.self();
    const PairControl* air = ownPairInAir(o);
    auto control = air ? *air : appearingPair(self, o.rules, o.timing);
    const auto to = placement(id);
    const Duel current(o);
    const bool recorded = !picture && current.hasRecordedWay(o.me, to);
    InputPath path;
    if (!recorded) {
        if (picture) {
            if (!planner.planSure(self.field, control, to, o.timing, o.rules.speedPercent, path) ||
                planner.lostBySkip(self.field, control, to, o.timing, o.rules.speedPercent, path)) return {};
        } else if (!planner.plan(self.field, control, to, o.timing, o.rules.speedPercent, path)) return {};
        for (const auto button : path.buttons) stepPair(control, self.field, button, o.timing, o.rules.speedPercent);
        if (!control.locked || placementId(control.placement()) != id) return {};
    }
    GameField field = self.field;
    const auto placed = place(field, to, pair);
    if (field.dead() || anyClear(field.f, o.rules)) return {};
    const auto split = o.timing.splitFrames(placed.split, to.x, placed.lockY);
    if (recorded) {
        const auto& hand = current.inHand[o.me];
        const auto check = split ? hand.wayLock + split + o.timing.landFrames : hand.wayCheck;
        return Landing{field, int(hand.wayLock - o.frame + 1), int(check - o.frame + 1),
                       o.rules.softDropBonus ? hand.waySoft : 0};
    }
    const auto land = split ? o.timing.landFrames : std::max(o.timing.lockWait, o.timing.landFrames - restedFrames(control, o.timing));
    return Landing{field, path.frames(), path.frames() + split + land, o.rules.softDropBonus ? path.softDropRows : 0};
}

template<class Stop>
class Reader {
public:
    Reader(const Observation& observation, const Stop& stop, int target, InputPlanner* later = nullptr) : o(observation), expired(stop), goal(target), inputs(later) {
        inputs.configure(o.timing, o.rules.speedPercent);
        for (int a = 0; a < 4; ++a)
            for (int b = 0; b < 4; ++b) types[4 * a + b] = {o.rules.palette[a], o.rules.palette[b]};
    }

    // Both orientations must keep a main the pair already shown can fire.
    int knownLock(const GameField& field, Pair pair) {
        int best = INT32_MAX;
        each(field, pair, [&](const GameField&, const Trial& chain, int frames) {
            if (chain.points >= goal) best = std::min(best, frames);
            return false;
        });
        return best;
    }

    // On the second turn the fourth pair is known, but the fifth is not.
    // Keep one second placement for each fourth pair across all fifth pairs.
    unsigned coverage(const GameField& field, Pair second, Pair third, Pair fourth) {
        unsigned best = 0;
        int peak = -1;
        each(field, second, [&](const GameField& afterSecond, const Trial& build, int) {
            if (build.chains) return false;
            int known = 0;
            std::vector<GameField> leaves;
            each(afterSecond, third, [&](const GameField& afterThird, const Trial& chain, int) {
                known = std::max(known, chain.points);
                if (chain.chains) return known >= goal;
                each(afterThird, fourth, [&](const GameField& afterFourth, const Trial& fired, int) {
                    known = std::max(known, fired.points);
                    if (!fired.chains) leaves.push_back(afterFourth);
                    return known >= goal;
                });
                return known >= goal;
            });
            if (known >= goal) { best = all; return true; }
            unsigned reached = 0;
            for (int i = 0; i < int(types.size()) && !expired(); ++i) {
                for (const auto& leaf : leaves) {
                    if (expired()) return true;
                    if (!each(leaf, types[i], [&](const GameField&, const Trial& fired, int) { return fired.points >= goal; })) continue;
                    reached |= 1u << i;
                    break;
                }
            }
            const int count = std::popcount(reached), previous = std::popcount(best);
            if (count > previous || (count == previous && known > peak)) best = reached, peak = known;
            return best == all;
        });
        return best;
    }

    std::array<Pair, 16> types;

private:
    template<class Visit>
    bool each(const GameField& field, Pair pair, const Visit& visit) {
        for (unsigned ids = differentFields(legalPlacements(field), pair); ids && !expired(); ids &= ids - 1) {
            const auto to = placement(std::countr_zero(ids));
            const auto input = inputs.get(field, to);
            if (!input.frames) continue;
            GameField next = field;
            place(next, to, pair);
            Trial chain;
            if (o.rules.clearCount != 4 || next.f.hasClear()) runTrialChain(next.f, o.rules, o.timing, chain, false);
            if (!next.dead() && visit(next, chain, input.frames)) return true;
        }
        return false;
    }

    static constexpr unsigned all = (1u << 16) - 1;
    const Observation& o;
    const Stop& expired;
    const int goal;
    SpawnInputCache inputs;
};

} // namespace setup_room

// The quiet reversed orientation takes the same space and time. Compare
// which later pairs can fire the visible main, keeping the second action
// fixed until the fifth pair is actually shown.
template<class Stop>
int preferOpenSetup(const Observation& o, const PlanSet& mine, const BuildResult& b, int chosen,
                    unsigned choices, int room, const Params& p, const Stop& stop, PictureRoutes* picture = nullptr) {
    const auto& self = o.self();
    const auto& rival = o.opponent();
    if (chosen < 0 || !p.readMidgame || !p.checkKnownExchange || p.setupSpeedNear <= 0 || p.x3ChokeKeep <= 0 ||
        p.x3ChokeMin <= 0 || p.handWait || b.cut || o.players != 2 || o.rules.colorsInPlay() != 4 || room >= 3 ||
        mine.knownCount != 3 || mine.pairCount != 3 || mine.midAir || self.phase != Phase::Next || self.wait != 1 ||
        o.busyLeft[o.me] || o.busyLeft[1 - o.me] || self.incoming.total() || rival.incoming.pending ||
        rival.incoming.confirmed >= W || (rival.phase != Phase::Next && rival.phase != Phase::Control) ||
        self.scoring.allClear || rival.scoring.allClear || stop()) return chosen;
    const Pair pair = mine.pair[0];
    if (pair.axis == pair.child) return chosen;
    Pair shown[4];
    if (copySearchPairs(o, o.me, true, shown, 4) != 3) return chosen;
    for (int i = 0; i < 3; ++i)
        if (!samePair(shown[i], mine.pair[i])) return chosen;
    const int reversed = (chosen + PLACEMENTS / 2) % PLACEMENTS;
    const unsigned both = (1u << chosen) | (1u << reversed);
    if ((choices & b.alive & both) != both || b.sum[chosen] <= 0 ||
        (b.sum[chosen] - b.sum[reversed]) * 1000 > b.sum[chosen] * std::clamp(p.setupSpeedNear, 0, 1000)) return chosen;

    int main = 0;
    for (const auto* list : {&mine.plans, &mine.tactical, &mine.keeping}) for (const auto& plan : *list) {
        if (!plan.fireDepth || plan.fireDepth != plan.count || plan.count > 3 || plan.fallen) continue;
        if (plan.allClear && (both >> plan.id[0] & 1u)) return chosen;
        if (!plan.allClear && plan.sent >= p.x3ChokeMin) main = std::max(main, plan.points);
    }
    if (!main || stop()) return chosen;
    const int goal = int((int64_t(main) * std::min(100, p.x3ChokeKeep) + 99) / 100);
    InputPlanner ordinary;
    auto& planner = picture ? picture->first : ordinary;
    const auto usual = setup_room::quietLanding(o, pair, chosen, planner, picture != nullptr);
    if (!usual || stop()) return chosen;
    const auto other = setup_room::quietLanding(o, pair, reversed, planner, picture != nullptr);
    if (!other || usual->frames != other->frames || usual->check != other->check || usual->soft != other->soft ||
        usual->field.f.occupied() != other->field.f.occupied() || stop()) return chosen;
    for (int x = 0; x < W; ++x)
        if (bool(usual->field.row14[x]) != bool(other->field.row14[x])) return chosen;

    setup_room::Reader read(o, stop, goal, picture ? &picture->later : nullptr);
    const int lock = read.knownLock(usual->field, mine.pair[1]);
    if (lock == INT32_MAX || read.knownLock(other->field, mine.pair[1]) > lock || stop()) return chosen;
    bool gains = false;
    for (const Pair fourth : read.types) {
        const unsigned before = read.coverage(usual->field, mine.pair[1], mine.pair[2], fourth);
        if (stop()) return chosen;
        const unsigned after = read.coverage(other->field, mine.pair[1], mine.pair[2], fourth);
        if ((before & ~after) || stop()) return chosen;
        gains = gains || (after & ~before);
    }
    return gains ? reversed : chosen;
}

} // namespace juraic::ai
