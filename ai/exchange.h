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
    DuelExchangeTrace* trace = nullptr;
    PictureRoutes* picture = nullptr;
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
    d.exchangeTrace = bounded ? bounded->trace : nullptr;
    d.tallPaths = p.x3TallPaths;
    d.wait[o.me] = p.handWait > 0 ? p.handWait : 0;
    const PlanSet* set[2];
    const Plan* plan[2];
    set[a.who] = &a, plan[a.who] = mine;
    set[b.who] = &b, plan[b.who] = theirs;
    int next[2] = {start ? start->next[0] : 0, start ? start->next[1] : 0};
    static thread_local SpawnInputCache spawned;
    if (knownPaths || o.timing.landThenLock) spawned.configure(o.timing, o.rules.speedPercent);
    // (Timing::landThenLock: placeFast takes the buttons on every field - Duel::place -, the same as tallSpawnInput gives.
    //  Lent for this reading only: the Duels it hands out carry none of this thread's cache.)
    const bool lent = o.timing.landThenLock && !d.spawnInputs;
    if (lent) d.spawnInputs = &spawned;
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
        // The pairs that did not help are time: the pair that does can be moved that much later, and locks so.
        const int64_t ready = q.spawnFrame + p.wildFrames(p.xWildEvery - 1, q.field.f.count());
        const int64_t lock = ready + o.timing.placeFrames(best->x, best->y) - o.timing.landFrames - o.timing.nextFrames;
        d.place(who, placement(best->x), Pair{best->color, best->color}, lock, 0, 0, ready);
    };
    const auto move = [&](int who) {
        if (stop()) return;
        if (!plan[who] || next[who] >= plan[who]->count) {
            if (d.player[who].spawnFrame >= 0 && d.player[who].phase == DuelPhase::Control) answer(who);
            return;
        }
        const int k = next[who]++;
        // (in mid-air: in the player's hands as observed, or the own pair shown as the one about to appear -
        //  Duel::appearingInAir; either goes from where it is, not from where a pair appears)
        const bool inHand = (k == 0 && set[who]->midAir) || d.appearingInAir(who);
        const DuelPlayer& player = d.player[who];
        const Placement target = placement(plan[who]->id[k]);
        const int64_t from = inHand ? (set[who]->midAir ? o.frame : d.inHand[who].frame) : player.spawnFrame;
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
            if (bounded && bounded->picture && who == o.me) {
                auto& routes = *bounded->picture;
                if (k == 0) {
                    const auto control = inHand ? (set[who]->midAir ? o.player[who].pair : d.inHand[who].pair)
                                                : spawnPair(o.timing);
                    input = picturePlacementInput(routes.first, player.field, control, target, o.timing, o.rules.speedPercent);
                } else {
                    routes.next.configure(o.timing, o.rules.speedPercent);
                    input = routes.next.get(player.field, target);
                }
                // Our route above supplies the lock; preserve the rival's timing model.
                d.fastestSeat[who] = 0;
                d.inHand[who].wayTarget = -1;
            } else if (inHand) {
                const int id = plan[who]->id[k];
                const unsigned bit = 1u << id;
                if (d.hasRecordedWay(who, target)) {
                    const auto& hand = d.inHand[who];
                    input = {int(hand.wayLock - from + 1), hand.waySoft, 0};
                } else if (bounded && (bounded->tried[who] & bit)) input = bounded->held[who][id];
                else {
                    const PairControl& control = set[who]->midAir ? o.player[who].pair : d.inHand[who].pair;
                    input = heldPlacementInput(player.field, control, target, o.timing, o.rules.speedPercent);
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
            if (lent) ownNext->duel.spawnInputs = nullptr;
            break;
        }
    }
    if (lent) d.spawnInputs = nullptr;
    return d;
}

// The frame in which the last placement of plan `theirs` (set `b`) is checked when that seat goes the straight way
// (Observation::fastestRead cleared for it) and, in that reading, every placement of the plan locks and the last one
// fires a chain. With FASTEST_BOUND `soonest` is no later than this. Without it, or when `soonest` is -1: `soonest`.
// -1: in the straight reading the plan is not played out, or its last placement fires no chain.
inline int64_t rivalSeenCheck(const Observation& o, const PlanSet& a, const Plan* mine, const PlanSet& b, const Plan* theirs,
                              const Params& p, uint64_t seed, int64_t soonest, const std::function<bool()>& stop = {}) {
    if (soonest < 0 || o.fastestRead[b.who] != Observation::FASTEST_BOUND) return soonest;
    Observation straight = o;
    straight.fastestRead[b.who] = 0;
    KnownReplay replay{stop};
    int64_t check = -1;
    const Duel d = playOut(straight, a, mine, b, theirs, p, seed, true, &check, stop ? &replay : nullptr);
    const DuelPlayer& rival = d.player[b.who];
    if (check < 0 || !theirs || rival.moves != o.player[b.who].moves + theirs->count || !rival.chain.chains) return -1;
    return std::max(check, soonest);
}

// The choice of the own pair that appears in frame `spawn` sees what shows in frame `shown`: that choice begins
// `ahead - 1` frames before the pair appears (Params::thinkAhead, PlacementAgent::ahead) and sees the field `seeLate`
// frames late (Params::seeLate).
inline bool seenBeforeChoice(int64_t shown, int64_t spawn, int ahead, int seeLate) {
    return shown < spawn - std::max(0, ahead - 1) - std::max(0, seeLate);
}

// V for player `me` at the end of `d`: a round the Duel has ended counts as it ended (won, lost, drawn), whatever the
// fields hold then; a round still played is valued with positionValue.
inline int duelValue(const Duel& d, int me, const Observation& o, const Params& p, int rate, uint64_t seed) {
    if (d.state == RoundState::Draw) return 0;
    if (d.state == (me ? RoundState::Won1 : RoundState::Won0)) return -VALUE_LOST;
    if (d.state == (me ? RoundState::Won0 : RoundState::Won1)) return VALUE_LOST;
    return positionValue(d.player[me], d.player[1 - me], o.rules, o.timing, p, rate, seed);
}

} // namespace juraic::ai
