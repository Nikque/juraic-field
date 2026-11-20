#pragma once
// The reading of one exchange: a plan of each player (gaze.h) is played against the other in the
// Duel, with the real times, offsets and falls, and the position it ends in is valued with V
// (value.h). X3 (tactics.h) reads every kind of move with it.
#include "agent.h"
#include "gaze.h"
#include "value.h"
#include <functional>

namespace juraic::ai {

// Bounded exact replays from one observation share its starting pair paths.
struct KnownReplay {
    std::function<bool()> stop;
    SpawnInput held[2][PLACEMENTS]{};
    unsigned tried[2]{};
    // Optional check of the canonical path, before putting the pair. A failed
    // check stops this reading. The lock frame is -1 without a path.
    std::function<bool(const Duel&, int, Placement, Pair, int64_t, int64_t)> acceptPlacement;
};
// A paused exact replay keeps the rival's scheduled move and both plan cursors.
struct ReplayCheckpoint {
    Duel duel;
    int next[2]{};
};

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
// `rivalFinalCheck`, when requested, records when the last placement of the
// rival is checked for its first clear. It remains -1 if that pair never locks.
// `ownNext` pauses at our next spawn after its planned placements. With `start`,
// the replay continues from there and may use a longer plan for our side.
inline Duel playOut(const Observation& o, const PlanSet& a, const Plan* mine, const PlanSet& b, const Plan* theirs,
                    const Params& p, uint64_t seed, bool knownPaths = false, int64_t* rivalFinalCheck = nullptr,
                    KnownReplay* bounded = nullptr, const ReplayCheckpoint* start = nullptr, ReplayCheckpoint* ownNext = nullptr) {
    if (rivalFinalCheck) *rivalFinalCheck = -1;
    Duel d = start ? start->duel : Duel(o, seed);
    d.tallPaths = p.x3TallPaths;
    d.wait[o.me] = p.handWait > 0 ? p.handWait : 0;
    const PlanSet* set[2];
    const Plan* plan[2];
    set[a.who] = &a, plan[a.who] = mine;
    set[b.who] = &b, plan[b.who] = theirs;
    int next[2] = {start ? start->next[0] : 0, start ? start->next[1] : 0};
    static thread_local SpawnInputCache spawned;
    if (knownPaths) spawned.configure(o.timing, o.rules.speedPercent);
    bool rejected = false;
    const auto stop = [&] { return rejected || (bounded && bounded->stop()); };
    const auto answer = [&](int who) {
        DuelPlayer& q = d.player[who];
        if (knownPaths || p.xWildEvery <= 0 || q.incoming.total() <= 0 || (who == a.who && !p.xWildSelf)) return;
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
        if (stop()) return;
        if (!plan[who] || next[who] >= plan[who]->count) {
            if (d.player[who].spawnFrame >= 0 && d.player[who].phase == DuelPhase::Control) answer(who);
            return;
        }
        const int k = next[who]++;
        const bool inHand = k == 0 && set[who]->midAir;
        const DuelPlayer& player = d.player[who];
        const Placement target = placement(plan[who]->id[k]);
        const int64_t from = inHand ? o.frame : player.spawnFrame;
        const auto accepts = [&](int64_t lock) {
            if (knownPaths && bounded && bounded->acceptPlacement &&
                !bounded->acceptPlacement(d, who, target, set[who]->pair[k], from, lock)) rejected = true;
            return !rejected;
        };
        // What happened in between may have closed the way: then the player stands still.
        const bool open = inHand || (legalPlacements(player.field) >> plan[who]->id[k] & 1);
        if (!open) {
            if (!accepts(-1)) return;
            next[who] = plan[who]->count;
        } else if (knownPaths) {
            // For a concrete counterexample every placement must have a button path.
            // A failed route stops the sequence; the caller checks the number of locks.
            SpawnInput input;
            if (inHand) {
                const int id = plan[who]->id[k];
                const unsigned bit = 1u << id;
                if (bounded && (bounded->tried[who] & bit)) input = bounded->held[who][id];
                else {
                    PairControl control = o.player[who].pair;
                    static thread_local InputPlanner planner;
                    static thread_local InputPath path;
                    if (planner.plan(player.field, control, target, o.timing, o.rules.speedPercent, path)) {
                        for (const uint8_t buttons : path.buttons) stepPair(control, player.field, buttons, o.timing, o.rules.speedPercent);
                        input = {path.frames(), path.softDropRows, int(control.groundFrames)};
                    }
                    if (bounded) bounded->held[who][id] = input, bounded->tried[who] |= bit;
                }
            } else {
                input = spawned.get(player.field, target);
            }
            if (stop()) return;
            const int64_t lock = input.frames ? from + input.frames - 1 : -1;
            if (!accepts(lock)) return;
            if (!input.frames) {
                next[who] = plan[who]->count;
                return;
            }
            d.place(who, target, set[who]->pair[k], lock, input.soft, input.rested);
        } else {
            applyMove(d, *set[who], *plan[who], k);
        }
        if (rivalFinalCheck && who == b.who && k + 1 == plan[who]->count && d.player[who].phase == DuelPhase::Check)
            *rivalFinalCheck = d.player[who].at;
    };
    if (start) move(o.me); // The saved branch is waiting for this player's next placement.
    else for (int who = 0; who < 2; ++who)
        if (set[who]->midAir) move(who);
    const int64_t limit = o.frame + 6000;
    while (d.state == RoundState::Playing && d.frame < limit && !stop()) {
        const int appeared = d.run(limit);
        if (!appeared) break;
        for (int who = 0; who < 2; ++who)
            if (appeared >> who & 1) move(who);
        if (ownNext && (appeared >> o.me & 1) && d.player[o.me].phase == DuelPhase::Control) {
            *ownNext = {d, {next[0], next[1]}};
            break;
        }
    }
    return d;
}

} // namespace juraic::ai
