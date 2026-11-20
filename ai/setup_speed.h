#pragma once
#include "gaze.h"

namespace juraic::ai {

// Elapsed frames from the first input to the next pair for a quiet placement.
// The wait before the current pair appears is common to all placements.
inline int quietSetupFrames(const Observation& o, const PlanSet& mine, int id, InputPlanner& planner) {
    const auto& self = o.self();
    PairControl control = mine.midAir ? self.pair : spawnPair(o.timing);
    InputPath path;
    const Placement target = placement(id);
    if (!planner.plan(self.field, control, target, o.timing, o.rules.speedPercent, path)) return -1;
    for (const uint8_t buttons : path.buttons) stepPair(control, self.field, buttons, o.timing, o.rules.speedPercent);
    Duel turn(o, 0);
    turn.place(o.me, target, mine.pair[0], o.frame + path.frames() - 1, path.softDropRows, restedFrames(control, o.timing));
    const auto& placed = turn.player[o.me];
    if (placed.field.dead() || anyClear(placed.field.f, o.rules)) return -1;
    return int(placed.at - o.frame) + o.timing.nextFrames;
}

template<class Stop>
int pickFasterSetup(const Observation& o, const PlanSet& mine, const BuildResult& b, int chosen,
                    unsigned legal, const Params& p, Stop stop) {
    if (chosen < 0 || !mine.pairCount || !p.readMidgame || p.setupSpeedNear <= 0 || p.handWait > 0 ||
        b.cut || b.sum[chosen] <= 0 || stop()) return chosen;
    unsigned options = legal & b.alive & ~(1u << chosen);
    if (mine.midAir) options &= mine.firstReach;
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
    int best = chosen, fastest = base;
    for (; options; options &= options - 1) {
        if (stop()) return chosen;
        const int id = std::countr_zero(options);
        const int frames = quietSetupFrames(o, mine, id, planner);
        const int saved = base - frames;
        if (frames < 0 || saved < 4 || (b.sum[chosen] - b.sum[id]) * 1000 > b.sum[chosen] * (2 * saved)) continue;
        if (frames < fastest || (frames == fastest && b.sum[id] > b.sum[best])) best = id, fastest = frames;
    }
    return stop() ? chosen : best;
}

} // namespace juraic::ai
