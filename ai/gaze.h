#pragma once
#include <type_traits>
#include <cstdlib>
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

// The coloured pieces of a field, those in the 14th row included.
inline int colouredPieces(const GameField& g) {
    int n = g.f.count() - g.f.garbage().count();
    for (const Cell c : g.row14) n += c >= 1 && c <= MAX_COLORS;
    return n;
}

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
    int64_t confirmFrame = 0; // the frame the landing or chain of the last placement is over in: what the chain sent is confirmed then (0: not known)
};

// Do two plans put the pairs on the same placements?
inline bool samePlacements(const Plan& a, const Plan& b) { return a.count == b.count && std::equal(a.id, a.id + a.count, b.id); }

struct PlanSet {
    int who = 0;
    Pair pair[MAX_KNOWN]{};
    int pairCount = 0; // exhaustive plans
    int knownCount = 0; // beam search
    bool midAir = false;             // the first pair is already in the player's hands
    bool airAsNext = false;          // the held pair presented as the next pair
    bool firstInAir() const { return midAir || airAsNext; }
    unsigned firstReach = 0;         // firstInAir(): the placements it can still reach
    int64_t firstLock[PLACEMENTS]{}; // firstInAir(): the frame it locks there at the earliest
    int firstSoft[PLACEMENTS]{};     // firstInAir(): rows of soft drop on the way
    int firstRest[PLACEMENTS]{};     // firstInAir(): frames it will have lain on the ground at that lock (Params::x3AirRest; 0 = not counted)
    int firstRecorded = -1;          // firstInAir(): the placement its recorded buttons go on to (Duel::hasRecordedWay; -1: none)
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
// Of the two placements that make the same field, the one the recorded buttons go on to is the one kept.
inline unsigned keepRecordedFirst(unsigned kept, unsigned reach, int recorded) {
    if (recorded < PLACEMENTS / 2 || !(reach >> recorded & 1)) return kept;
    return (kept | 1u << recorded) & ~(1u << (recorded - PLACEMENTS / 2));
}
inline unsigned knownPlacements(const GameField& field, const PlanSet& set, int index, const Params& p) {
    if (index == 0 && set.firstInAir()) {
        const unsigned reach = set.firstReach;
        if (set.pair[0].axis == set.pair[0].child) {
            const unsigned preferred = differentFields(reach, set.pair[0]);
            return keepRecordedFirst(reach & ~(preferred << (PLACEMENTS / 2)), reach, set.firstRecorded);
        }
        return reach;
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
    if (k == 0 && set.airAsNext && !(set.firstReach >> id & 1)) return;
    if (k == 0 && set.firstInAir()) d.place(set.who, placement(id), set.pair[0], set.firstLock[id], set.firstSoft[id], set.firstRest[id]);
    else d.placeFast(set.who, placement(id), set.pair[k]);
}

// Plays the frames until a pair of `who` appears; false when that does not happen before `limit`.
inline bool toPairOf(Duel& d, int who, int64_t limit) {
    while (d.state == RoundState::Playing && d.frame < limit)
        if (d.run(limit) >> who & 1) return true;
    return false;
}

// What is above the field of the player other than `who` (waiting, and fallen since `d` began) when his landing or
// chain that runs in `d` is over, with `who` placing nothing and no wave of `who` played: of the garbage that waited
// there, what falls on him stays in the count and what his chain offsets leaves it.
inline int aboveAfterHis(const Duel& d, int who, int64_t limit) {
    Duel alone = d;
    alone.exchangeTrace = nullptr, alone.firstDrops = nullptr;
    const DuelPlayer& he = alone.player[1 - who];
    if (he.phase == DuelPhase::Check || he.phase == DuelPhase::Settle) {
        if (alone.player[who].phase != DuelPhase::Dead) alone.player[who].phase = DuelPhase::Control; // (run() passes a player in Control by)
        toPairOf(alone, 1 - who, limit);
    }
    return he.fallen + he.incoming.total();
}
// What waits above the field of the player other than `who` in the observation; with Params::x3SentAfterHis, what
// is above it when his landing or chain that runs is over (aboveAfterHis).
inline int aboveOther(const Observation& o, int who, const Params& p) {
    if (!p.x3SentAfterHis) return o.player[1 - who].incoming.total();
    return aboveAfterHis(Duel(o), who, o.frame + 6000);
}

class Gaze {
public:
    // Scoped to the two shallow readings of a single decision.
    class ThirdPair {
        friend class Gaze;
        struct Entry {
            Plan plan;
            int finalLoss;
            int next = -1;
        };
        struct Seat {
            std::vector<Entry> order;
            std::vector<Duel> next;
            int base = 0, baseScore = 0, baseColoured = 0;
        } seat[2];
    };

    // Fills `set` with the plans of player `who`. byFirst: keep the plans apart
    // by their first placement (for the player who is deciding now).
    void plans(const Observation& o, int who, bool byFirst, int depthLimit, const Params& p, uint64_t seed,
               PlanSet& set, ThirdPair* third = nullptr) {
        collect(o, who, byFirst, depthLimit, p, seed, set, nullptr, third ? &third->seat[who] : nullptr);
    }

    // Keep both complete readings on a timeout. An incomplete set of enemy replies
    // must never be used as evidence that our attack cannot be answered.
    bool tryThirdPair(const Observation& o, const Params& p, uint64_t seed, PlanSet& mine, PlanSet& theirs,
                      Builder::Clock::time_point deadline, const ThirdPair* third = nullptr) {
        if (Builder::Clock::now() >= deadline) return false;
        PlanSet own, other;
        const bool extendOwn = mine.knownCount >= 3, extendOther = theirs.knownCount >= 3;
        const auto extend = [&](const PlanSet& shallow, bool byFirst, PlanSet& full) {
            return third ? resume(o, p, shallow, byFirst, third->seat[shallow.who], full, deadline)
                         : collect(o, shallow.who, byFirst, 3, p, seed, full, &deadline);
        };
        if (extendOwn && !extend(mine, true, own)) return false;
        if (extendOther && !extend(theirs, false, other)) return false;
        if (Builder::Clock::now() >= deadline) return false;
        if (extendOwn) std::swap(mine, own);
        if (extendOther) std::swap(theirs, other);
        return true;
    }

private:
    bool resume(const Observation& o, const Params& p, const PlanSet& shallow, bool byFirst,
                const ThirdPair::Seat& third, PlanSet& full, Builder::Clock::time_point deadline) {
        full = shallow;
        full.plans.clear();
        full.keeping.clear();
        full.tactical.clear();
        full.pairCount = 3;
        limit = o.frame + 6000;
        base = third.base;
        baseScore = third.baseScore;
        baseColoured = third.baseColoured;
        retainOffset = p.readMidgame && p.checkKnownExchange && full.who == o.me && chainShown(o.opponent());
        beginPlans(p, byFirst);
        for (const auto& entry : third.order) {
            if (Builder::Clock::now() >= deadline) return false;
            if (entry.next < 0) {
                keep(p, full, entry.plan, entry.finalLoss);
            } else {
                Plan plan = entry.plan;
                if (!walk(o, p, full, third.next[size_t(entry.next)], 2, plan, &deadline)) return false;
            }
        }
        finishPlans(p, full);
        return true;
    }

    bool collect(const Observation& o, int who, bool byFirst, int depthLimit, const Params& p, uint64_t seed, PlanSet& set,
                 const Builder::Clock::time_point* deadline, ThirdPair::Seat* third = nullptr) {
        const Player& player = o.player[who];
        set.who = who;
        set.plans.clear();
        set.keeping.clear();
        set.tactical.clear();
        set.pairCount = 0;
        set.midAir = player.phase == Phase::Control;
        const PairControl* air = pairInAir(o, who);
        set.airAsNext = air && !set.midAir;
        set.firstReach = 0;
        set.firstRecorded = -1;
        const int planLimit = std::max(set.midAir ? 1 : 0, std::min(3, depthLimit));
        set.knownCount = copySearchPairs(o, who, true, set.pair, std::max(planLimit, p.searchKnownLimit(depthLimit)));
        set.pairCount = std::min(set.knownCount, planLimit);
        if (set.pairCount != 2 || set.knownCount < 3) third = nullptr;
        if (!set.pairCount || player.phase == Phase::Dead) return true;
        Duel d(o, seed);
        if (air) {
            const unsigned legal = legalPlacements(player.field);
            for (int id = 0; id < PLACEMENTS; ++id) {
                if (deadline && Builder::Clock::now() >= *deadline) return false;
                if (!(legal >> id & 1)) continue;
                const Placement target = placement(id);
                if (d.hasRecordedWay(who, target)) {
                    const auto& hand = d.inHand[who];
                    set.firstReach |= 1u << id;
                    set.firstRecorded = id;
                    set.firstLock[id] = hand.wayLock;
                    set.firstSoft[id] = hand.waySoft;
                    set.firstRest[id] = 0; // Duel::place retains the recorded first check.
                    continue;
                }
                if (!planner.plan(player.field, *air, target, o.timing, o.rules.speedPercent, path)) continue;
                set.firstReach |= 1u << id;
                set.firstLock[id] = o.frame + path.frames() - 1;
                set.firstSoft[id] = path.softDropRows;
                set.firstRest[id] = 0;
                if (p.x3AirRest) {
                    // (the pair as those buttons leave it: how long it has lain on the ground when it locks)
                    PairControl q = *air;
                    for (const uint8_t b : path.buttons) stepPair(q, player.field, b, o.timing, o.rules.speedPercent);
                    set.firstRest[id] = restedFrames(q, o.timing);
                }
            }
        }
        limit = o.frame + 6000;
        d.tallPaths = p.x3TallPaths;
        // (the buttons of placeFast kept by field: on tall fields, and on every field with Timing::landThenLock, where
        //  Duel::place takes them always; the same buttons as tallSpawnInput gives)
        if ((p.readMidgame && d.tallPaths > 0) || o.timing.landThenLock) {
            spawnInputs.configure(o.timing, o.rules.speedPercent);
            d.spawnInputs = &spawnInputs;
        }
        d.wait[o.me] = p.handWait > 0 ? p.handWait : 0; // (the hands of the player who reads, whoever's plans these are)
        // (what waits above the other field and what of it has fallen: garbage that was waiting falls there while he lands
        //  or his chain ends, and is no part of what a plan sends)
        // (Params::x3SentAfterHis: nor is what his chain that runs offsets of it)
        base = p.x3SentAfterHis ? aboveAfterHis(d, who, limit) : d.player[1 - who].fallen + d.player[1 - who].incoming.total();
        baseScore = d.player[who].scoring.score;
        baseColoured = colouredPieces(d.player[who].field);
        retainOffset = p.readMidgame && p.checkKnownExchange && who == o.me && chainShown(o.opponent());
        if (!set.midAir && !toPairOf(d, who, limit)) return true;
        if (third) {
            third->order.reserve(PLACEMENTS * PLACEMENTS);
            third->next.reserve(PLACEMENTS * PLACEMENTS);
            third->base = base;
            third->baseScore = baseScore;
            third->baseColoured = baseColoured;
        }
        beginPlans(p, byFirst);
        Plan plan;
        if (!walk(o, p, set, d, 0, plan, deadline, third)) return false;
        finishPlans(p, set);
        return true;
    }

    void beginPlans(const Params& p, bool byFirst) {
        for (int& k : kept) k = -1;
        for (int& k : keptToo) k = -1;
        for (int& k : keptMain) k = -1;
        if (p.readMidgame)
            for (auto& byRole : keptTactical) std::fill(std::begin(byRole), std::end(byRole), -1);
        spare.clear();
        firsts = byFirst ? PLACEMENTS : 1;
    }

    void finishPlans(const Params& p, PlanSet& set) {
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
            if (set.tactical.empty()) return;
            static_assert(PLACEMENTS <= 32 && std::extent_v<decltype(Plan::id)> <= 6);
            // Leading one encodes the length. Only the initialized IDs of a
            // legal candidate are read. Membership does not need lexical order.
            const auto key = [](const Plan& plan) {
                uint32_t value = 1;
                for (int i = 0; i < plan.count; ++i)
                    value = (value << 5) | unsigned(plan.id[i]);
                return value;
            };
            std::vector<uint32_t> regular;
            regular.reserve(set.plans.size());
            for (const Plan& plan : set.plans) regular.push_back(key(plan));
            std::sort(regular.begin(), regular.end());
            std::erase_if(set.tactical, [&](const Plan& plan) {
                const bool found = std::binary_search(regular.begin(), regular.end(), key(plan));
                return found;
            });
        }
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
              const Builder::Clock::time_point* deadline, ThirdPair::Seat* third = nullptr) {
        const int who = set.who;
        const unsigned legal = knownPlacements(d.player[who].field, set, k, p);
        for (int id = 0; id < PLACEMENTS; ++id) {
            if (!(legal >> id & 1)) continue;
            if (deadline && Builder::Clock::now() >= *deadline) return false;
            Duel c = d;
            plan.id[k] = int8_t(id);
            plan.count = int8_t(k + 1);
            applyMove(c, set, plan, k);
            if (c.player[who].moves == d.player[who].moves) continue;
            const bool again = toPairOf(c, who, limit);
            const DuelPlayer& mine = c.player[who];
            if (mine.phase == DuelPhase::Dead) continue;
            const bool fired = mine.chain.chains > 0;
            if (!fired && again && k + 1 < set.pairCount) {
                if (!walk(o, p, set, c, k + 1, plan, deadline, third)) return false;
                plan.count = int8_t(k + 1);
                continue;
            }
            plan.fireDepth = int8_t(fired ? k + 1 : 0);
            plan.chains = int8_t(fired ? mine.chain.chains : 0);
            plan.allClear = fired && mine.scoring.allClear;
            // (Params::x3SentAfterHis: taken when his chain that runs is over too, as `base` is)
            plan.sent = (p.x3SentAfterHis ? aboveAfterHis(c, who, limit) : c.player[1 - who].fallen + c.player[1 - who].incoming.total()) - base;
            plan.value = buildValue(mine.field, o.rules, o.timing, p, &plan.mainAfter);
            plan.points = mine.scoring.score - baseScore;
            plan.used = baseColoured + 2 * (k + 1) - colouredPieces(mine.field);
            plan.fallen = mine.fallen + mine.incoming.total();
            plan.lockFrame = mine.lockFrame;
            plan.endFrame = c.frame;
            plan.confirmFrame = mine.settleFrame > 0 ? mine.settleFrame : 0;
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
            if (third) {
                // Replace each extendable two-placement leaf by its children
                // at the same point in the original depth-first traversal.
                if (again && !fired && k == 1) {
                    third->order.push_back({plan, finalLoss, int(third->next.size())});
                    third->next.push_back(c);
                } else {
                    third->order.push_back({plan, finalLoss});
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
