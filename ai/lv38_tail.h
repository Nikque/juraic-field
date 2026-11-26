#pragma once
#include "detect.h"
#include <algorithm>
#include <array>

namespace juraic::ai::lv38_tail {

constexpr int MIN_POINTS = 920;

inline bool inWindow(int colored, int from = 40) { return colored >= from && colored <= 66; }

inline bool eligible(int waves, int points, int minimum = MIN_POINTS) {
    return waves >= 2 && waves <= 3 && points >= minimum;
}

struct Shape {
    int points = 0;
    int waves = 0;
    int added = 0;

    // A trigger with one piece is worth more than the same fire with two.
    // One extra wave is a smaller cost than one extra trigger piece.
    int value() const {
        if (!points) return 0;
        return (added == 1 ? 30 : 16) - (waves == 3 ? 6 : 0);
    }
};

inline FieldBits settledMask(FieldBits tracked, FieldBits occupied) {
    tracked &= occupied;
    if (tracked.empty() || occupied == occupied.settled()) return tracked;
    FieldBits result;
    for (int x = 0; x < W; ++x) {
        const unsigned cells = tracked.column(x);
        if (cells) result |= FieldBits::columnCells(x, detail::compactColumn(cells, occupied.column(x)));
    }
    return result;
}

struct WaveEnd {
    int waves = 0, points = 0;
};

// Follow the same cells through gravity. A replacement group of the same
// colour at the old trigger column is insufficient to prove the main survives.
inline WaveEnd trace(BitField& field, const Rules& rules, FieldBits& tracked) {
    WaveEnd end;
    Wave w;
    while (findWaveFor(field, w, rules.clearCount)) {
        end.points += gameWaveScore(w, ++end.waves);
        if (rules.clearTopGarbage) addTopGarbage(field, w);
        field.erase(w.cleared | w.garbage);
        tracked = settledMask(tracked, field.occupied());
        if (!field.drop()) break;
    }
    return end;
}

inline bool addTrigger(BitField& field, int height[W], const Trial& trigger) {
    const int x = trigger.x;
    if (height[x] + trigger.added > 12) return false;
    for (int j = 0; j < trigger.added; ++j) field.set(x, height[x]++, trigger.color);
    return true;
}

// Simulate the original trigger column and colour, with its original key when
// useful. The first group has to include pieces of the original ignition.
inline int fireAt(const BitField& source, const KeyPlan& plan, const Rules& rules, const Timing& timing,
                  FieldBits required, FieldBits* support = nullptr) {
    BitField f = source;
    int h[W];
    f.heights(h);
    const int x = plan.t.x, kx = plan.keyX;
    if (plan.key && kx != x) {
        if (h[kx] >= 12) return 0;
        f.set(kx, h[kx]++, plan.keyColor);
    }
    if (plan.key && anyClear(f, rules)) return 0;
    const int y = h[x];
    if (!addTrigger(f, h, plan.t)) return 0;
    if (plan.key && kx == x) {
        if (h[x] >= 12) return 0;
        f.set(x, h[x]++, plan.keyColor);
    }
    const FieldBits start = FieldBits::cell(x, y);
    Wave wave;
    if (!findWaveFor(f, wave, rules.clearCount, false) || (wave.cleared & start).empty()) return 0;
    const FieldBits group = BitField::group(start, f.links());
    if (group.disjoint(required)) return 0;
    if (support) {
        *support = group & source.normal();
        return plan.t.points;
    }
    Trial scored;
    runTrialChain(f, rules, timing, scored, false);
    return scored.chains >= rules.chainRequirement ? scored.points : 0;
}

inline FieldBits supportOf(const GameField& field, const KeyPlan& main, const Rules& rules, const Timing& timing) {
    FieldBits support;
    if (main.t.points > 0) fireAt(field.f, main, rules, timing, field.f.normal(), &support);
    return support;
}

inline int retainedPoints(const BitField& field, const KeyPlan& main, FieldBits support,
                          const Rules& rules, const Timing& timing) {
    if (support.empty()) return 0;
    int best = 0;
    KeyPlan fire = main;
    for (int added = 1; added <= main.t.added; ++added) {
        fire.t.added = int8_t(added);
        fire.key = false;
        best = std::max(best, fireAt(field, fire, rules, timing, support));
        if (main.key) {
            fire.key = true;
            best = std::max(best, fireAt(field, fire, rules, timing, support));
        }
    }
    return best;
}

inline bool keepsMain(int remaining, const KeyPlan& main) {
    return main.t.points > 0 && int64_t(remaining) * 2 >= main.t.points;
}

// Map cleared pieces to their original locations through each fall.
inline FieldBits originalCleared(const GameField& original, const KeyPlan& plan, const Rules& rules) {
    BitField field = original.f;
    int h[W];
    field.heights(h);
    const int x = plan.t.x, keyX = plan.keyX;
    if (plan.key && keyX != x) {
        if (h[keyX] >= 12) return {};
        field.set(keyX, h[keyX]++, plan.keyColor);
        if (anyClear(field, rules)) return {};
    }
    if (!addTrigger(field, h, plan.t)) return {};
    if (plan.key && keyX == x) {
        if (h[x] >= 12 || plan.keyColor == plan.t.color) return {};
        field.set(x, h[x]++, plan.keyColor);
    }
    std::array<std::array<int, H>, W> originalRow;
    const FieldBits colours = original.f.normal();
    for (int column = 0; column < W; ++column)
        for (int y = 0; y < H; ++y)
            originalRow[column][y] = colours.get(column, y) ? y : -1;

    FieldBits cleared;
    Wave wave;
    while (findWaveFor(field, wave, rules.clearCount)) {
        if (rules.clearTopGarbage) addTopGarbage(field, wave);
        for (int column = 0; column < W; ++column)
            for (int y = 0; y < H; ++y)
                if (wave.cleared.get(column, y) && originalRow[column][y] >= 0)
                    cleared |= FieldBits::cell(column, originalRow[column][y]);
        field.erase(wave.cleared | wave.garbage);
        const FieldBits occupied = field.occupied();
        if (!field.drop()) break;
        for (int column = 0; column < W; ++column) {
            int settled = 0;
            for (int y = 0; y < H; ++y)
                if (occupied.get(column, y)) originalRow[column][settled++] = originalRow[column][y];
            for (; settled < H; ++settled) originalRow[column][settled] = -1;
        }
    }
    return cleared;
}

inline Shape bestShape(const GameField& field, const Trials& trials, const KeyPlan& main, const Rules& rules,
                       const Timing& timing, int* reserve = nullptr, int minimum = MIN_POINTS, bool ofMain = false) {
    Shape best;
    if (reserve) *reserve = 0;
    FieldBits original;
    FieldBits mainClears;
    bool readSupport = false;
    for (int i = 0; i < trials.count; ++i) {
        const Trial& t = trials.item[i];
        if (t.added > 2 || t.chains < rules.chainRequirement || !eligible(t.chains, t.points, minimum) ||
            (t.x == main.t.x && t.color == main.t.color)) continue;
        const Shape next{t.points, t.chains, t.added};
        if (next.value() <= best.value()) continue;
        if (!readSupport) {
            original = supportOf(field, main, rules, timing);
            readSupport = true;
            if (original.empty()) return {};
            if (ofMain) mainClears = originalCleared(field, main, rules);
        }
        BitField after = field.f;
        int h[W];
        after.heights(h);
        if (!addTrigger(after, h, t)) continue;
        FieldBits tracked = original;
        trace(after, rules, tracked);
        if (after.occupied().get(DEATH_X, DEATH_Y)) continue;
        const int remains = retainedPoints(after, main, tracked, rules, timing);
        if (!keepsMain(remains, main)) continue;
        if (ofMain) {
            const FieldBits tailClears = originalCleared(field, KeyPlan{t}, rules);
            if (tailClears.empty() || (mainClears & tailClears) != tailClears) continue;
        }
        best = next;
        if (reserve) *reserve = remains;
    }
    return best;
}

} // namespace juraic::ai::lv38_tail
