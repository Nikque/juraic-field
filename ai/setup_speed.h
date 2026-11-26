#pragma once
#include "gaze.h"

namespace juraic::ai {

// Elapsed frames from the first input to the next pair for a quiet placement.
// The wait before the current pair appears is common to all placements.
inline int quietSetupFrames(const Observation& o, const PlanSet& mine, int id, InputPlanner& planner) {
    const auto& self = o.self();
    const PairControl* air = ownPairInAir(o); // (the pair in mid-air, also shown as the one about to appear: from there)
    PairControl control = air ? *air : spawnPair(o.timing);
    InputPath path;
    const Placement target = placement(id);
    Duel turn(o, 0);
    if (turn.hasRecordedWay(o.me, target)) {
        const auto& hand = turn.inHand[o.me];
        turn.place(o.me, target, mine.pair[0], hand.wayLock, hand.waySoft);
    } else {
        if (!planner.plan(self.field, control, target, o.timing, o.rules.speedPercent, path)) return -1;
        for (const uint8_t buttons : path.buttons) stepPair(control, self.field, buttons, o.timing, o.rules.speedPercent);
        turn.place(o.me, target, mine.pair[0], o.frame + path.frames() - 1, path.softDropRows, restedFrames(control, o.timing));
    }
    const auto& placed = turn.player[o.me];
    if (placed.field.dead() || anyClear(placed.field.f, o.rules)) return -1;
    int64_t at = placed.at;
    // (Observation::fastestRead: the own pairs go the way over soonest. The pair in the air is counted so by the Duel; the
    //  pair still to come here, from the frame its first button is for; with a piece left to fall, from the lock of that
    //  way, the fall and all of the landing, as Duel::place counts it)
    if (!air && o.fastestRead[o.me] == Observation::FASTEST_OWN) {
        GameField g = self.field;
        const PlaceResult r = juraic::place(g, target, mine.pair[0]);
        const int split = o.timing.splitFrames(r.split, target.x, r.lockY);
        const FastestWay way = fastestEnd(self.field, spawnPair(o.timing), o.frame, target, o.timing, o.rules.speedPercent,
                                          o.fastestReadStates[o.me]);
        if (way.end > 0) at = std::min(at, split ? o.frame + way.lock - 1 + split + o.timing.landFrames : o.frame + way.end - 1);
    }
    return int(at - o.frame) + o.timing.nextFrames;
}

template<class Stop>
int pickFasterSetup(const Observation& o, const PlanSet& mine, const BuildResult& b, int chosen,
                    unsigned legal, const Params& p, Stop stop, bool namedLv38Ex = false) {
    if (chosen < 0 || !mine.pairCount || !p.readMidgame || p.setupSpeedNear <= 0 || p.handWait > 0 ||
        b.cut || b.sum[chosen] <= 0 || stop()) return chosen;
    unsigned options = legal & b.alive & ~(1u << chosen);
    if (mine.firstInAir()) options &= mine.firstReach;
    // Only time alternatives near enough in the completed, unchanged beam.
    for (unsigned ids = options; ids; ids &= ids - 1) {
        const int id = std::countr_zero(ids);
        if ((b.sum[chosen] - b.sum[id]) * 1000 > b.sum[chosen] * std::clamp(p.setupSpeedNear, 0, 1000))
            options &= ~(1u << id);
    }
    if (!options) return chosen;
    static thread_local InputPlanner planner;
    const int base = quietSetupFrames(o, mine, chosen, planner);
    if (base < 0) return chosen;
    const int minimumSaved = namedLv38Ex && p.lv38ExSetupSaved > 0 ? p.lv38ExSetupSaved : 4;
    int best = chosen, fastest = base;
    for (; options; options &= options - 1) {
        if (stop()) return chosen;
        const int id = std::countr_zero(options);
        const int frames = quietSetupFrames(o, mine, id, planner);
        const int saved = base - frames;
        if (frames < 0 || saved < minimumSaved || (b.sum[chosen] - b.sum[id]) * 1000 > b.sum[chosen] * (2 * saved)) continue;
        if (frames < fastest || (frames == fastest && b.sum[id] > b.sum[best])) best = id, fastest = frames;
    }
    return stop() ? chosen : best;
}

} // namespace juraic::ai
