#pragma once
// V: how a position stands for one player against the other, as one integer
// (larger = better for the player). It is what the reading of exchanges
// compares at the end of every line it plays out.
//
// The same items are taken for both players and subtracted. They are made only
// from what can be measured the same way in any position: the garbage still
// waiting is let fall first, and then the field is looked at with trial drops.
// The unit is about a tenth of a garbage piece. Integers only; the weights are
// in params.h (set by hand for now, to be fitted to results of games).
#include "detect.h"
#include "params.h"
#include "../game/turn.h"

namespace juraic::ai {

constexpr int VALUE_LOST = -100000000; // the player has lost (the third column is closed)
constexpr uint64_t VALUE_GARBAGE_STREAM = 0x76616C; // columns of the garbage let fall for the evaluation

// The items of one player. `core` is a copy: its waiting garbage is dropped on it.
inline int sideValue(PlayerCore core, const Rules& rules, const Timing& timing, const Params& p, int rate, uint64_t seed) {
    if (core.field.dead()) return VALUE_LOST;
    // What waits will fall, thirty at a time as in the game. The first fall
    // comes before the player can do anything about it: when it closes the
    // third column the position is lost. Before a later fall he has a move, so
    // garbage that would close the column then is only counted as waiting.
    core.incoming.confirm();
    Rng rng(seed, VALUE_GARBAGE_STREAM);
    int placed[W];
    for (bool first = true; core.incoming.confirmed > 0; first = false) {
        const PlayerCore before = core;
        dropGarbage(core.field, core.incoming, rules, rng, placed);
        if (!core.field.dead()) continue;
        if (first || !p.vFirstDropOnly) return VALUE_LOST;
        core = before;
        break;
    }
    const int waiting = core.incoming.confirmed;
    int h[W];
    core.field.heights(h);
    // The chain that can be fired: the best, and the best that one piece starts.
    Trials trials;
    detect(core.field, rules, timing, p.trialMaxAdd, trials, nullptr, false); // (the frames of the chains are not read here)
    int best = 0, quick = 0;
    for (int i = 0; i < trials.count; ++i) {
        const Trial& t = trials.item[i];
        if (t.chains < rules.chainRequirement) continue;
        if (t.points > best) best = t.points;
        if (t.added == 1 && t.points > quick) quick = t.points;
    }
    const auto capped = [&](int points) {
        const int garbage = points / rate;
        return garbage < p.vPotentialCap ? garbage : p.vPotentialCap;
    };
    const int garbage = core.field.f.garbage().count();
    const int coloured = core.field.f.count() - garbage;
    const int danger = h[DEATH_X] > p.vDangerFrom ? h[DEATH_X] - p.vDangerFrom : 0;
    int v = p.vPotential * capped(best) + p.vQuick * capped(quick) + p.vPiece * coloured - p.vGarbage * garbage - p.vDanger * danger * danger -
            p.vWaiting * waiting;
    if (core.scoring.allClear) v += p.vAllClear * capped(rules.allClearBonus);
    return v;
}

// V for player `me` of two players. `rate` = points per garbage piece at that moment.
inline int positionValue(const PlayerCore& mine, const PlayerCore& theirs, const Rules& rules, const Timing& timing, const Params& p, int rate,
                         uint64_t seed) {
    const int a = sideValue(mine, rules, timing, p, rate, seed), b = sideValue(theirs, rules, timing, p, rate, seed + 1);
    if (a == VALUE_LOST) return b == VALUE_LOST ? 0 : VALUE_LOST;
    if (b == VALUE_LOST) return -VALUE_LOST;
    return a - b;
}

} // namespace juraic::ai
