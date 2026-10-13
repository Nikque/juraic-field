#pragma once
// What a player can do with the pairs on the screen: every sequence of
// placements is played in the Duel with the other player standing still, and
// the ones worth reading further are kept as plans.
//
// A plan ends with the move that fires a chain, or (a plan that builds) after
// the last pair on the screen. Kept: for every [first placement,] firing move
// (1st, 2nd, 3rd) and size of the attack, one plan; and one plan that builds.
// Small attacks (up to four rows) keep the one that leaves the best field,
// large ones the one that sends most. All clears are kept apart.
//
// Time is exact: the Timing tables for pairs that have not appeared yet, and
// for a pair that is already in the air the path of InputPlanner from where it
// is now.
#include "build.h"
#include "duel.h"
#include "input.h"

namespace juraic::ai {

struct Plan {
    int8_t count = 0;     // placements
    int8_t id[3]{};       // placement ids, in order
    int8_t fireDepth = 0; // the placement that fires (1..3); 0 = the plan builds
    int8_t chains = 0;    // waves of the chain it fires
    bool allClear = false;
    int sent = 0;         // garbage left above the other field (the other standing still)
    int value = 0;        // buildValue of the own field afterwards
    int mainAfter = 0;    // points of the largest chain a trial drop starts on the own field afterwards
    int points = 0;       // score gained on the way (the chain, and the drop bonus)
    int used = 0;         // coloured pieces that were cleared
    int fallen = 0;       // garbage that fell on the own field on the way, and what still waits above it when the next pair appears
    int64_t lockFrame = 0; // the frame the last placement locks
    int64_t endFrame = 0;  // the frame after the one in which the next pair appears
};

struct PlanSet {
    int who = 0;
    Pair pair[3];
    int pairCount = 0;
    bool midAir = false;             // the first pair is already in the player's hands
    unsigned firstReach = 0;         // midAir: the placements it can still reach
    int64_t firstLock[PLACEMENTS]{}; // midAir: the frame it locks there at the earliest
    int firstSoft[PLACEMENTS]{};     // midAir: rows of soft drop on the way
    int firstRest[PLACEMENTS]{};     // midAir: frames it will have lain on the ground at that lock (Params::x3AirRest; 0 = not counted)
    std::vector<Plan> plans;
};

constexpr int ATTACK_SIZES = 8; // rows of garbage: 0, 1, 2, 3, 4, 5-9, 10-19, 20 or more
constexpr int attackSize(int garbage) {
    const int rows = garbage / W;
    return rows < 5 ? (rows < 0 ? 0 : rows) : rows < 10 ? 5 : rows < 20 ? 6 : 7;
}
// Params::x3KeepFine, the finer sizes: no garbage, one to five pieces, one to nine rows each, 10-19 rows, 20 or more.
constexpr int FINE_SIZES = 13;
constexpr int fineSize(int garbage) {
    const int rows = garbage / W;
    return garbage <= 0 ? 0 : rows < 1 ? 1 : rows < 10 ? rows + 1 : rows < 20 ? 11 : 12;
}

// Puts placement number k of a plan for its player. The pair must be in his hands.
inline void applyMove(Duel& d, const PlanSet& set, const Plan& plan, int k) {
    const int id = plan.id[k];
    if (k == 0 && set.midAir) d.place(set.who, placement(id), set.pair[0], set.firstLock[id], set.firstSoft[id], set.firstRest[id]);
    else d.placeFast(set.who, placement(id), set.pair[k]);
}

// Plays the frames until a pair of `who` appears; false when that does not happen before `limit`.
inline bool toPairOf(Duel& d, int who, int64_t limit) {
    while (d.state == RoundState::Playing && d.frame < limit)
        if (d.run(limit) >> who & 1) return true;
    return false;
}

class Gaze {
public:
    // Fills `set` with the plans of player `who`. byFirst: keep the plans apart
    // by their first placement (for the player who is deciding now).
    void plans(const Observation& o, int who, bool byFirst, int depthLimit, const Params& p, uint64_t seed, PlanSet& set) {
        const Player& player = o.player[who];
        set.who = who;
        set.plans.clear();
        set.pairCount = 0;
        set.midAir = player.phase == Phase::Control;
        set.firstReach = 0;
        if (set.midAir) set.pair[set.pairCount++] = player.colors;
        const SeenTsumo& shown = o.pairsOf(who); // (his own tsumo in a measuring match with Round::ownTsumo)
        for (int i = player.nextPair; set.pairCount < 3 && set.pairCount < depthLimit && shown.known(i); ++i) set.pair[set.pairCount++] = shown.pair(i);
        if (!set.pairCount || player.phase == Phase::Dead) return;
        if (set.midAir) {
            const unsigned legal = legalPlacements(player.field);
            for (int id = 0; id < PLACEMENTS; ++id) {
                if (!(legal >> id & 1) || !planner.plan(player.field, player.pair, placement(id), o.timing, o.rules.speedPercent, path)) continue;
                set.firstReach |= 1u << id;
                set.firstLock[id] = o.frame + path.frames() - 1;
                set.firstSoft[id] = path.softDropRows;
                set.firstRest[id] = 0;
                if (p.x3AirRest) {
                    // (the pair as those buttons leave it: how long it has lain on the ground when it locks)
                    PairControl q = player.pair;
                    for (const uint8_t b : path.buttons) stepPair(q, player.field, b, o.timing, o.rules.speedPercent);
                    set.firstRest[id] = int(q.groundFrames);
                }
            }
        }
        limit = o.frame + 6000;
        Duel d(o, seed);
        d.tallPaths = p.x3TallPaths;
        d.wait[o.me] = p.handWait > 0 ? p.handWait : 0; // (the hands of the player who reads, whoever's plans these are)
        base = d.player[1 - who].incoming.total();
        baseScore = d.player[who].scoring.score;
        baseColoured = d.player[who].field.f.count() - d.player[who].field.f.garbage().count();
        if (!set.midAir && !toPairOf(d, who, limit)) return;
        for (int& k : kept) k = -1;
        for (int& k : keptToo) k = -1;
        spare.clear();
        firsts = byFirst ? PLACEMENTS : 1;
        Plan plan;
        walk(o, p, set, d, 0, plan);
        // x3KeepFall 2: the plans that let the least fall, after those kept as before, where they are others.
        if (p.x3KeepFall == 2)
            for (size_t slot = 0; slot < std::size(keptToo); ++slot) {
                if (keptToo[slot] < 0) continue;
                const Plan& least = spare[size_t(keptToo[slot])];
                const Plan& usual = set.plans[size_t(kept[slot])];
                bool same = least.count == usual.count;
                for (int k = 0; same && k < least.count; ++k) same = least.id[k] == usual.id[k];
                if (!same) set.plans.push_back(least);
            }
    }

private:
    InputPlanner planner;
    InputPath path;
    int64_t limit = 0;
    int base = 0, firsts = 1, baseScore = 0, baseColoured = 0;
    // [first][firing move 0..3][attack size, or the number of sizes for an all clear] -> index in set.plans
    // (the sizes: ATTACK_SIZES of them, or FINE_SIZES with Params::x3KeepFine)
    int kept[PLACEMENTS * 4 * (FINE_SIZES + 1)];
    // x3KeepFall 2: the same slots -> index in `spare`, the plan of the slot that lets the least fall
    int keptToo[PLACEMENTS * 4 * (FINE_SIZES + 1)];
    std::vector<Plan> spare;

    void walk(const Observation& o, const Params& p, PlanSet& set, const Duel& d, int k, Plan& plan) {
        const int who = set.who;
        unsigned legal = k == 0 && set.midAir ? set.firstReach : legalPlacements(d.player[who].field);
        if (set.pair[k].axis == set.pair[k].child) legal &= (1u << 11) - 1; // the other two directions give the same field
        for (int id = 0; id < PLACEMENTS; ++id) {
            if (!(legal >> id & 1)) continue;
            Duel c = d;
            plan.id[k] = int8_t(id);
            plan.count = int8_t(k + 1);
            applyMove(c, set, plan, k);
            const bool again = toPairOf(c, who, limit);
            const DuelPlayer& mine = c.player[who];
            if (mine.phase == DuelPhase::Dead) continue;
            const bool fired = mine.chain.chains > 0;
            if (!fired && again && k + 1 < set.pairCount) {
                walk(o, p, set, c, k + 1, plan);
                plan.count = int8_t(k + 1);
                continue;
            }
            plan.fireDepth = int8_t(fired ? k + 1 : 0);
            plan.chains = int8_t(fired ? mine.chain.chains : 0);
            plan.allClear = fired && mine.scoring.allClear;
            plan.sent = c.player[1 - who].incoming.total() - base;
            plan.value = buildValue(mine.field, o.rules, o.timing, p, &plan.mainAfter);
            plan.points = mine.scoring.score - baseScore;
            plan.used = baseColoured + 2 * (k + 1) - (mine.field.f.count() - mine.field.f.garbage().count());
            plan.fallen = mine.fallen + mine.incoming.total();
            plan.lockFrame = mine.lockFrame;
            plan.endFrame = c.frame;
            keep(p, set, plan);
        }
    }

    void keep(const Params& p, PlanSet& set, const Plan& plan) {
        const int sizes = p.x3KeepFine ? FINE_SIZES : ATTACK_SIZES;
        const int size = plan.allClear ? sizes : plan.fireDepth ? (p.x3KeepFine ? fineSize(plan.sent) : attackSize(plan.sent)) : 0;
        const int first = firsts == 1 ? 0 : plan.id[0];
        const int at = (first * 4 + plan.fireDepth) * (sizes + 1) + size;
        // (large: an all clear, or five rows or more)
        const bool large = plan.fireDepth && (plan.allClear || plan.sent / W >= 5);
        // x3KeepFall: a plan that fires less than that is also told by what it lets fall on the own field.
        const bool byFall = p.x3KeepFall > 0 && plan.fireDepth && !large;
        if (byFall && p.x3KeepFall == 2) {
            int& too = keptToo[at];
            if (too < 0) {
                too = int(spare.size());
                spare.push_back(plan);
            } else {
                Plan& least = spare[size_t(too)];
                if (plan.fallen < least.fallen || (plan.fallen == least.fallen && plan.value > least.value)) least = plan;
            }
        }
        int& slot = kept[at];
        if (slot < 0) {
            slot = int(set.plans.size());
            set.plans.push_back(plan);
            return;
        }
        Plan& old = set.plans[size_t(slot)];
        const bool better = large                           ? plan.sent > old.sent || (plan.sent == old.sent && plan.value > old.value)
                            : byFall && p.x3KeepFall == 1 ? plan.fallen < old.fallen || (plan.fallen == old.fallen && plan.value > old.value)
                                                            : plan.value > old.value;
        if (better) old = plan; // equal: the one found first stays
    }
};

} // namespace juraic::ai
