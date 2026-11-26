#pragma once
#include "lv38_tail.h"

namespace juraic::ai::lv38_head {

constexpr int MIN_POINTS = 560;

inline bool eligible(int waves, int points) {
    return waves >= 2 && waves <= 3 && points >= MIN_POINTS;
}

struct Shape {
    Trial fire;
    Trial reserve;

    bool valid() const { return reserve.points > 0; }
};

// The key's color has to start this chain directly from the settled
// board. A hypothetical additional key does not count as a trigger.
inline Trial retained(const GameField& after, const KeyPlan& main, const Rules& rules, const Timing& timing) {
    Trial best;
    if (after.dead()) return best;
    Trials trials;
    detect(after, rules, timing, 2, trials, nullptr, false);
    for (int i = 0; i < trials.count; ++i) {
        const Trial& t = trials.item[i];
        if (t.color == main.keyColor && t.chains >= rules.chainRequirement && t.points > best.points)
            best = t;
    }
    return best;
}

inline Shape shape(const GameField& original, const Trial& fire, const BitField& after,
                   const KeyPlan& main, const Rules& rules, const Timing& timing) {
    if (!main.key || fire.added > 2 || fire.chains < rules.chainRequirement || !eligible(fire.chains, fire.points)) return {};
    GameField remainder = original;
    remainder.f = after;
    const Trial reserve = retained(remainder, main, rules, timing);
    if (!lv38_tail::keepsMain(reserve.points, main)) return {};
    return {fire, reserve};
}

inline Shape shape(const GameField& field, const KeyPlan& main, const Rules& rules, const Timing& timing) {
    if (!main.key || main.t.added > 2) return {};
    int heights[W];
    field.heights(heights);
    KeyPlan fire = main;
    fire.key = false;
    Trial front;
    BitField after;
    if (!runWithKey(field.f, heights, rules, timing, fire, front, after, false)) return {};
    return shape(field, front, after, main, rules, timing);
}

} // namespace juraic::ai::lv38_head
