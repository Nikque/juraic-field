#pragma once
// The reading of one exchange: a plan of each player (gaze.h) is played against the other in the
// Duel, with the real times, offsets and falls, and the position it ends in is valued with V
// (value.h). X3 (tactics.h) reads every kind of move with it.
#include "agent.h"
#include "gaze.h"
#include "value.h"

namespace juraic::ai {

// Plays plan `mine` of set `a` against plan `theirs` of set `b` (either may be
// null: that player has no plan) from the observation to the end of all that
// follows. Returns the Duel as it is then.
//
// After his plan a player goes on getting pairs nobody can see yet. While
// garbage waits above his field he answers with them: every xWildEvery-th such
// pair is taken to be two pieces of the colour that the best trial drop on his
// field needs, put on its column (a chain that needs three takes two such
// pairs); the pairs in between cost him xWildFrames each. Without garbage
// waiting he stands still. This stands in for what a player can do beyond
// the pairs on the screen while a chain is running. By default only the
// opponent of the player who decides (set `a`) is given such pairs: an attack
// is judged against an opponent who may be lucky, a defence by what is sure.
inline Duel playOut(const Observation& o, const PlanSet& a, const Plan* mine, const PlanSet& b, const Plan* theirs, const Params& p, uint64_t seed) {
    Duel d(o, seed);
    d.tallPaths = p.x3TallPaths;
    d.wait[o.me] = p.handWait > 0 ? p.handWait : 0;
    const PlanSet* set[2];
    const Plan* plan[2];
    set[a.who] = &a, plan[a.who] = mine;
    set[b.who] = &b, plan[b.who] = theirs;
    int next[2] = {0, 0};
    const auto answer = [&](int who) {
        DuelPlayer& q = d.player[who];
        if (p.xWildEvery <= 0 || q.incoming.total() <= 0 || (who == a.who && !p.xWildSelf)) return;
        Trials trials;
        detect(q.field, o.rules, o.timing, p.trialMaxAdd, trials, nullptr, false); // (the frames of the chains are not read here)
        // The smallest chain that offsets everything, or else the largest.
        const int need = q.incoming.total() * o.rules.rateAt(d.frame) - q.scoring.pool;
        const Trial* best = nullptr;
        for (int i = 0; i < trials.count; ++i) {
            const Trial& t = trials.item[i];
            if (t.chains < o.rules.chainRequirement) continue;
            const bool enough = p.xWildExact && t.points >= need, bestEnough = best && p.xWildExact && best->points >= need;
            if (!best || (enough && !bestEnough) || (enough == bestEnough && (enough ? t.points < best->points : t.points > best->points))) best = &t;
        }
        if (!best || !(legalPlacements(q.field) >> best->x & 1)) return;
        // The pairs that did not help are time: the pair that does locks that much later.
        const int64_t lock = q.spawnFrame + p.wildFrames(p.xWildEvery - 1, q.field.f.count()) + o.timing.placeFrames(best->x, best->y) - o.timing.landFrames -
                             o.timing.nextFrames;
        d.place(who, placement(best->x), Pair{best->color, best->color}, lock, 0);
    };
    const auto move = [&](int who) {
        if (!plan[who] || next[who] >= plan[who]->count) {
            if (d.player[who].spawnFrame >= 0 && d.player[who].phase == DuelPhase::Control) answer(who);
            return;
        }
        const int k = next[who]++;
        // What happened in between may have closed the way: then the player stands still.
        const bool open = k == 0 && set[who]->midAir ? true : (legalPlacements(d.player[who].field) >> plan[who]->id[k] & 1) != 0;
        if (open) applyMove(d, *set[who], *plan[who], k);
        else next[who] = plan[who]->count;
    };
    for (int who = 0; who < 2; ++who)
        if (set[who]->midAir) move(who);
    const int64_t limit = o.frame + 6000;
    while (d.state == RoundState::Playing && d.frame < limit) {
        const int appeared = d.run(limit);
        if (!appeared) break;
        for (int who = 0; who < 2; ++who)
            if (appeared >> who & 1) move(who);
    }
    return d;
}

} // namespace juraic::ai
