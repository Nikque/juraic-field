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

constexpr int MIDGAME_PLAN_PAIRS = 5;

struct Plan {
    int8_t count = 0;     // placements
    int8_t id[MIDGAME_PLAN_PAIRS]{}; // placement ids, in order
    int8_t fireDepth = 0; // the placement that fires; 0 = the plan builds
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

// Do two plans put the pairs on the same placements?
inline bool samePlacements(const Plan& a, const Plan& b) { return a.count == b.count && std::equal(a.id, a.id + a.count, b.id); }

struct PlanSet {
    int who = 0;
    Pair pair[MAX_KNOWN]{};
    int pairCount = 0; // exhaustive plans
    int knownCount = 0; // beam search
    bool midAir = false;             // the first pair is already in the player's hands
    unsigned firstReach = 0;         // midAir: the placements it can still reach
    int64_t firstLock[PLACEMENTS]{}; // midAir: the frame it locks there at the earliest
    int firstSoft[PLACEMENTS]{};     // midAir: rows of soft drop on the way
    int firstRest[PLACEMENTS]{};     // midAir: frames it will have lain on the ground at that lock (Params::x3AirRest; 0 = not counted)
    std::vector<Plan> plans;
    // Params::x3Crush: of the plans that fire, for every slot of `plans` (Gaze::keep), the one that keeps the most of
    // the main chain - kept apart: the slot keeps one plan by another rule, and an answer that keeps the main chain can
    // be one it drops. Only the crush reads these.
    std::vector<Plan> keeping;
    // An attack-size slot can hide a faster fire, a stronger offset or a better reserve.
    // These alternatives are read alongside plans without changing their original order.
    std::vector<Plan> tactical;
};

// Once a same-colour pair is in the air, a wall or the lock timer can leave
// just one orientation reachable. Keep one actual path for each resulting field.
inline unsigned knownPlacements(const GameField& field, const PlanSet& set, int index, const Params& p) {
    if (index == 0 && set.midAir) {
        const unsigned reach = set.firstReach;
        if (p.readMidgame && set.pair[0].axis == set.pair[0].child) {
            const unsigned preferred = differentFields(reach, set.pair[0]);
            return reach & ~(preferred << (PLACEMENTS / 2));
        }
        return differentFields(reach, set.pair[0]);
    }
    return differentFields(legalPlacements(field), set.pair[index]);
}

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
        collect(o, who, byFirst, depthLimit, p, seed, set, nullptr);
    }

    // Keep both complete readings on a timeout. An incomplete set of enemy replies
    // must never be used as evidence that our attack cannot be answered.
    bool tryThirdPair(const Observation& o, const Params& p, uint64_t seed, PlanSet& mine, PlanSet& theirs,
                      Builder::Clock::time_point deadline) {
        if (Builder::Clock::now() >= deadline) return false;
        PlanSet own, other;
        const bool extendOwn = mine.knownCount >= 3, extendOther = theirs.knownCount >= 3;
        if (extendOwn && !collect(o, mine.who, true, 3, p, seed, own, &deadline)) return false;
        if (extendOther && !collect(o, theirs.who, false, 3, p, seed, other, &deadline)) return false;
        if (Builder::Clock::now() >= deadline) return false;
        if (extendOwn) std::swap(mine, own);
        if (extendOther) std::swap(theirs, other);
        return true;
    }

private:
    bool collect(const Observation& o, int who, bool byFirst, int depthLimit, const Params& p, uint64_t seed, PlanSet& set,
                 const Builder::Clock::time_point* deadline) {
        const Player& player = o.player[who];
        set.who = who;
        set.plans.clear();
        set.keeping.clear();
        set.tactical.clear();
        set.pairCount = 0;
        set.midAir = player.phase == Phase::Control;
        set.firstReach = 0;
        const int planLimit = std::max(set.midAir ? 1 : 0, std::min(3, depthLimit));
        set.knownCount = copySearchPairs(o, who, true, set.pair, std::max(planLimit, p.searchKnownLimit(depthLimit)));
        set.pairCount = std::min(set.knownCount, planLimit);
        if (!set.pairCount || player.phase == Phase::Dead) return true;
        if (set.midAir) {
            const unsigned legal = legalPlacements(player.field);
            for (int id = 0; id < PLACEMENTS; ++id) {
                if (deadline && Builder::Clock::now() >= *deadline) return false;
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
        if (p.readMidgame && d.tallPaths > 0) {
            spawnInputs.configure(o.timing, o.rules.speedPercent);
            d.spawnInputs = &spawnInputs;
        }
        d.wait[o.me] = p.handWait > 0 ? p.handWait : 0; // (the hands of the player who reads, whoever's plans these are)
        base = d.player[1 - who].incoming.total();
        baseScore = d.player[who].scoring.score;
        baseColoured = d.player[who].field.f.count() - d.player[who].field.f.garbage().count();
        retainOffset = p.readMidgame && p.checkKnownExchange && who == o.me && o.opponent().phase == Phase::Wave;
        if (!set.midAir && !toPairOf(d, who, limit)) return true;
        for (int& k : kept) k = -1;
        for (int& k : keptToo) k = -1;
        for (int& k : keptMain) k = -1;
        if (p.readMidgame)
            for (auto& byRole : keptTactical) std::fill(std::begin(byRole), std::end(byRole), -1);
        spare.clear();
        firsts = byFirst ? PLACEMENTS : 1;
        Plan plan;
        if (!walk(o, p, set, d, 0, plan, deadline)) return false;
        // x3KeepFall 2: the plans that let the least fall, after those kept as before, where they are others.
        if (p.x3KeepFall == 2)
            for (size_t slot = 0; slot < std::size(keptToo); ++slot) {
                if (keptToo[slot] < 0) continue;
                const Plan& least = spare[size_t(keptToo[slot])];
                if (!samePlacements(least, set.plans[size_t(kept[slot])])) set.plans.push_back(least);
            }
        if (p.readMidgame) {
            // Keep the same sequence only once across the lists that read the exchange.
            const auto byPlacement = [](const Plan& a, const Plan& b) {
                return std::lexicographical_compare(a.id, a.id + a.count, b.id, b.id + b.count);
            };
            std::sort(set.tactical.begin(), set.tactical.end(), byPlacement);
            set.tactical.erase(std::unique(set.tactical.begin(), set.tactical.end(), samePlacements), set.tactical.end());
            std::vector<Plan> regular = set.plans;
            std::sort(regular.begin(), regular.end(), byPlacement);
            std::erase_if(set.tactical, [&](const Plan& x) {
                return std::binary_search(regular.begin(), regular.end(), x, byPlacement);
            });
        }
        return true;
    }

    InputPlanner planner;
    SpawnInputCache spawnInputs;
    InputPath path;
    int64_t limit = 0;
    int base = 0, firsts = 1, baseScore = 0, baseColoured = 0;
    bool retainOffset = false;
    // [first][firing move 0..3][attack size, or the number of sizes for an all clear] -> index in set.plans
    // (the sizes: ATTACK_SIZES of them, or FINE_SIZES with Params::x3KeepFine)
    int kept[PLACEMENTS * 4 * (FINE_SIZES + 1)];
    // x3KeepFall 2: the same slots -> index in `spare`, the plan of the slot that lets the least fall
    int keptToo[PLACEMENTS * 4 * (FINE_SIZES + 1)];
    std::vector<Plan> spare;
    // Params::x3Crush: the same slots -> index in PlanSet::keeping
    int keptMain[PLACEMENTS * 4 * (FINE_SIZES + 1)];
    int keptTactical[5][PLACEMENTS * 4 * (FINE_SIZES + 1)];
    int keptOffsetLoss[PLACEMENTS * 4 * (FINE_SIZES + 1)];

    bool walk(const Observation& o, const Params& p, PlanSet& set, const Duel& d, int k, Plan& plan,
              const Builder::Clock::time_point* deadline) {
        const int who = set.who;
        const unsigned legal = knownPlacements(d.player[who].field, set, k, p);
        for (int id = 0; id < PLACEMENTS; ++id) {
            if (!(legal >> id & 1)) continue;
            if (deadline && Builder::Clock::now() >= *deadline) return false;
            Duel c = d;
            plan.id[k] = int8_t(id);
            plan.count = int8_t(k + 1);
            applyMove(c, set, plan, k);
            const bool again = toPairOf(c, who, limit);
            const DuelPlayer& mine = c.player[who];
            if (mine.phase == DuelPhase::Dead) continue;
            const bool fired = mine.chain.chains > 0;
            if (!fired && again && k + 1 < set.pairCount) {
                if (!walk(o, p, set, c, k + 1, plan, deadline)) return false;
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
            int finalLoss = -1;
            if (retainOffset && fired && !plan.allClear) {
                // A short answer often finishes while most of the rival chain is still to come.
                // Compare every answer after that same attack ends, with no further placements.
                Duel exchange = c;
                const int rival = 1 - who;
                const auto phase = exchange.player[rival].phase;
                if ((phase != DuelPhase::Check && phase != DuelPhase::Settle) || toPairOf(exchange, rival, limit)) {
                    const auto& self = exchange.player[who];
                    finalLoss = self.fallen + self.incoming.total();
                }
            }
            keep(p, set, plan, finalLoss);
        }
        return true;
    }

    void keep(const Params& p, PlanSet& set, const Plan& plan, int finalLoss) {
        const int sizes = p.x3KeepFine ? FINE_SIZES : ATTACK_SIZES;
        const int size = plan.allClear ? sizes : plan.fireDepth ? (p.x3KeepFine ? fineSize(plan.sent) : attackSize(plan.sent)) : 0;
        const int first = firsts == 1 ? 0 : plan.id[0];
        const int at = (first * 4 + plan.fireDepth) * (sizes + 1) + size;
        // (large: an all clear, or five rows or more)
        const bool large = plan.fireDepth && (plan.allClear || plan.sent / W >= 5);
        // (a is better than b: it sends more; it lets less fall; it keeps more of the main chain. Equal: the better field,
        //  or the one that sends more)
        const auto sends = [](const Plan& a, const Plan& b) { return a.sent > b.sent || (a.sent == b.sent && a.value > b.value); };
        const auto spares = [](const Plan& a, const Plan& b) { return a.fallen < b.fallen || (a.fallen == b.fallen && a.value > b.value); };
        const auto keepsMore = [](const Plan& a, const Plan& b) { return a.mainAfter > b.mainAfter || (a.mainAfter == b.mainAfter && a.sent > b.sent); };
        if (p.readMidgame && plan.fireDepth && !plan.allClear) {
            // Keep opposing immediate fires apart, since they commit before the attack lands.
            const int tacticalAt = ((plan.fireDepth == 1 ? plan.id[0] : first) * 4 + plan.fireDepth) * (sizes + 1) + size;
            const auto firesEarlier = [](const Plan& a, const Plan& b) {
                return a.lockFrame < b.lockFrame || (a.lockFrame == b.lockFrame && a.endFrame < b.endFrame);
            };
            keepIn(set.tactical, keptTactical[0][tacticalAt], plan, firesEarlier);
            keepIn(set.tactical, keptTactical[1][tacticalAt], plan, sends);
            keepIn(set.tactical, keptTactical[2][tacticalAt], plan, keepsMore);
            keepIn(set.tactical, keptTactical[3][tacticalAt], plan, spares);
            // A counter can spend its whole attack on offsets and share the zero-send slot.
            int& offset = keptTactical[4][tacticalAt];
            if (finalLoss >= 0 && (offset < 0 || finalLoss < keptOffsetLoss[tacticalAt] ||
                (finalLoss == keptOffsetLoss[tacticalAt] && firesEarlier(plan, set.tactical[size_t(offset)])))) {
                keepIn(set.tactical, offset, plan, [](const Plan&, const Plan&) { return true; });
                keptOffsetLoss[tacticalAt] = finalLoss;
            }
        }
        // (x3Crush: the plan of the slot that keeps the most of the main chain)
        if (p.x3Crush > 0 && plan.fireDepth) keepIn(set.keeping, keptMain[at], plan, keepsMore);
        // x3KeepFall: a plan that fires less than that is also told by what it lets fall on the own field.
        const bool byFall = p.x3KeepFall > 0 && plan.fireDepth && !large;
        if (byFall && p.x3KeepFall == 2) keepIn(spare, keptToo[at], plan, spares);
        keepIn(set.plans, kept[at], plan, [&](const Plan& a, const Plan& b) {
            return large ? sends(a, b) : byFall && p.x3KeepFall == 1 ? spares(a, b) : a.value > b.value;
        });
    }
    // `plan` into `list` at the place `slot` holds (-1: none yet, and it is put at the end), there in place of the plan
    // that better(plan, that one) says it is better than. Equal: the one found first stays.
    template <class Better>
    static void keepIn(std::vector<Plan>& list, int& slot, const Plan& plan, Better better) {
        if (slot < 0) slot = int(list.size()), list.push_back(plan);
        else if (Plan& old = list[size_t(slot)]; better(plan, old)) old = plan;
    }
};

} // namespace juraic::ai
