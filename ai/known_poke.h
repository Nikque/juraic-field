#pragma once
#include "preparation.h"

namespace juraic::ai::known_poke {

inline bool known(const Plan& plan, const PlanSet& shown) {
    return plan.count > 0 && plan.count <= shown.knownCount && plan.count <= MIDGAME_PLAN_PAIRS &&
        std::all_of(plan.id, plan.id + plan.count, [](int id) { return id >= 0 && id < PLACEMENTS; });
}

inline bool followed(const DuelPlayer& state, int moves, const Plan* plan) {
    return state.phase == DuelPhase::Control && !state.field.dead() &&
        state.moves == moves + (plan ? plan->count : 0) && (!plan || !plan->fireDepth || state.chain.chains == plan->chains);
}

inline void add(std::vector<const Plan*>& plans, const Plan& next) {
    if (std::none_of(plans.begin(), plans.end(), [&](const Plan* p) { return samePlacements(*p, next); })) plans.push_back(&next);
}

struct Result {
    bool complete;
    int fallen, waiting, enemyFallen, enemyConfirmed;
    int64_t power;
};

inline bool noWorse(const Result& a, const Result& b) {
    return a.complete && b.complete && a.fallen <= b.fallen && a.waiting <= b.waiting &&
        a.enemyFallen >= b.enemyFallen && a.enemyConfirmed >= b.enemyConfirmed && a.power >= b.power;
}

// A stored all-clear bonus can make even a single lethal before the next pair
// after a poke. Check the visible input routes that can spend it by then.
template<class Stop>
int surviveBonus(const Observation& o, unsigned legal, uint64_t seed, const Params& p, const PlanSet& own,
                 const PlanSet& enemy, const Plan& poke, Stop stop) {
    if (!enemy.midAir || !enemy.knownCount || o.rules.allClearBonus <= 0 || stop()) return -1;
    const unsigned quiet = quietSetupChoices(o, own, legal);
    std::vector<const Plan*> holds;
    for (const auto* list : {&own.plans, &own.tactical}) for (const Plan& line : *list) {
        if (known(line, own) && (quiet >> line.id[0] & 1) && line.count == line.fireDepth &&
            line.chains >= 5 && !line.allClear) add(holds, line);
    }
    if (holds.empty() || stop()) return -1;
    const int me = o.me, him = 1 - me;
    KnownReplay bounded{stop};
    ReplayCheckpoint nextOwn;
    const auto standing = playOut(o, own, &poke, enemy, nullptr, p, seed, true, nullptr, &bounded, nullptr, &nextOwn);
    if (!nextOwn.duel.rules || !followed(standing.player[me], o.self().moves, &poke)) return -1;
    const int64_t settle = standing.player[me].spawnFrame - o.timing.nextFrames;
    const int64_t minimumFire = o.timing.landFrames + o.timing.waveFrames(0);
    const int64_t initialSpawn = o.frame + o.self().wait - 1;

    Observation fromRival = o;
    fromRival.me = him;
    std::vector<Plan> lethal;
    Plan reply;
    bool incomplete = false;
    const auto walk = [&](const auto& visit, unsigned places, int index, const ReplayCheckpoint* prefix) -> void {
        for (; places; places &= places - 1) {
            if (incomplete || stop()) return;
            reply.count = int8_t(index + 1);
            reply.id[index] = int8_t(std::countr_zero(places));
            ReplayCheckpoint next;
            const auto d = playOut(fromRival, enemy, &reply, own, &poke, p, seed, true, nullptr, &bounded, prefix, &next);
            const auto& rival = d.player[him];
            if (rival.moves != o.opponent().moves + reply.count || rival.fallen) continue;
            if (rival.chain.chains && rival.scoring.score - o.opponent().scoring.score >= o.rules.allClearBonus) {
                const auto complete = next.duel.rules
                    ? playOut(fromRival, enemy, &reply, own, &poke, p, seed, true, nullptr, &bounded, &next)
                    : d;
                const auto& self = complete.player[me];
                if (self.phase == DuelPhase::Dead && complete.player[him].phase != DuelPhase::Dead &&
                    self.moves == o.self().moves + 1 && self.spawnFrame == initialSpawn &&
                    self.fallen && (self.fallen % W == 0 || o.nextGarbageOrder[me])) {
                    reply.chains = int8_t(rival.chain.chains);
                    reply.points = rival.scoring.score - o.opponent().scoring.score;
                    lethal.push_back(reply);
                }
            } else if (next.duel.rules && rival.spawnFrame + minimumFire <= settle) {
                if (index + 1 >= enemy.knownCount || index + 1 >= MIDGAME_PLAN_PAIRS) incomplete = true;
                else visit(visit, legalPlacements(rival.field), index + 1, &next);
            }
        }
    };
    walk(walk, enemy.firstReach, 0, nullptr);
    if (incomplete || lethal.empty() || stop()) return -1;

    std::stable_sort(holds.begin(), holds.end(), [](const Plan* a, const Plan* b) {
        return a->points != b->points ? a->points > b->points : a->lockFrame < b->lockFrame;
    });
    for (const Plan* hold : holds) {
        bool offsets = true;
        for (const Plan& counter : lethal) {
            if (stop()) return -1;
            const auto d = playOut(o, own, hold, enemy, &counter, p, seed, true, nullptr, &bounded);
            const auto& self = d.player[me];
            const auto& rival = d.player[him];
            if (!followed(self, o.self().moves, hold) || self.fallen || self.incoming.total() || self.scoring.allClear ||
                rival.moves != o.opponent().moves + counter.count || rival.chain.chains != counter.chains ||
                rival.scoring.score - o.opponent().scoring.score != counter.points) {
                offsets = false;
                break;
            }
        }
        if (offsets) return stop() ? -1 : hold->id[0];
    }
    return -1;
}

template<class Stop>
int prefer(const Observation& o, unsigned legal, int rate, uint64_t seed, const Params& p,
           const PlanSet& own, const PlanSet& enemy, const Plan& original, int room, bool discounted, int enemyMain,
           Stop outOfTime) {
    bool expired = false;
    const auto stop = [&] { return expired = expired || outOfTime(); };
    if (!p.readMidgame || !p.checkKnownExchange || p.handWait || p.x3PokeNet != 3 || p.x3PokeRead || p.x3PokeUnanswered ||
        o.rules.cancel || o.self().phase != Phase::Next || o.self().wait <= 0 || own.midAir || o.busyLeft[o.me] ||
        own.knownCount < 2 || own.knownCount > 4 || o.self().incoming.total() || o.opponent().incoming.total() ||
        enemy.midAir != (o.opponent().phase == Phase::Control) || (enemy.midAir && o.opponent().pair.locked) ||
        o.self().scoring.allClear || !known(original, own) ||
        original.count != 1 || original.fireDepth != 1 || original.chains < 1 || original.chains > 4 ||
        original.allClear || original.sent >= 2 * W || stop()) return -1;
    if (o.opponent().scoring.allClear)
        return surviveBonus(o, legal, seed, p, own, enemy, original, stop);
    const bool compareReserve = own.knownCount == 3 && room >= own.knownCount;
    if (!compareReserve && !discounted) return -1;

    const int me = o.me, him = 1 - me;
    KnownReplay bounded{stop};
    const auto replay = [&](const Plan* line, const Plan* reply, const ReplayCheckpoint* start = nullptr,
                            ReplayCheckpoint* ownNext = nullptr) {
        return playOut(o, own, line, enemy, reply, p, seed, true, nullptr, &bounded, start, ownNext);
    };
    const auto ours = [&](const Duel& d, const Plan* line) { return followed(d.player[me], o.self().moves, line); };
    const auto theirs = [&](const Duel& d, const Plan* line) { return followed(d.player[him], o.opponent().moves, line); };
    const unsigned quiet = quietSetupChoices(o, own, legal);
    std::vector<const Plan*> holds, pressure{&original}, replies;
    for (const auto* list : {&own.plans, &own.tactical, &own.keeping}) for (const Plan& plan : *list) {
        if (!known(plan, own)) continue;
        if (plan.id[0] == original.id[0] && list != &own.keeping) add(pressure, plan);
        if ((quiet >> plan.id[0] & 1) && !plan.allClear && plan.fireDepth == plan.count && plan.chains >= 5)
            add(holds, plan);
    }
    if (holds.empty()) return -1;
    for (const auto* list : {&enemy.plans, &enemy.tactical})
        for (const Plan& plan : *list) if (known(plan, enemy)) add(replies, plan);
    unsigned tested = 0, opens = 0;
    const auto opensQuiet = [&](int id) {
        const unsigned bit = 1u << id;
        if (!(tested & bit)) {
            tested |= bit;
            Plan first;
            first.count = 1;
            first.id[0] = int8_t(id);
            const auto d = replay(&first, nullptr);
            if (ours(d, &first) && !d.player[me].chain.chains && !d.player[me].scoring.allClear) opens |= bit;
        }
        return (opens & bit) != 0;
    };

    // Prefer a visible quiet route only if it dominates every retained, reachable reply,
    // including each retained continuation of the poke and a reply without placements.
    if (compareReserve) {
        std::vector<const Plan*> reachable{nullptr};
        for (const Plan* reply : replies) {
            if (stop()) return -1;
            if (theirs(replay(nullptr, reply), reply)) reachable.push_back(reply);
        }
        std::stable_sort(holds.begin(), holds.end(), [](const Plan* a, const Plan* b) {
            return a->points != b->points ? a->points > b->points : a->lockFrame < b->lockFrame;
        });
        const auto measure = [&](const Plan* line, const Plan* reply) {
            const auto d = replay(line, reply);
            const auto& a = d.player[me];
            const auto& b = d.player[him];
            const bool complete = ours(d, line) && theirs(d, reply) && !a.scoring.allClear && !b.scoring.allClear;
            int reserve = 0;
            if (complete) buildValue(a.field, o.rules, o.timing, p, &reserve);
            return Result{complete, a.fallen, a.incoming.total(), b.fallen, b.incoming.confirmed,
                          int64_t(a.scoring.score) - o.self().scoring.score + reserve};
        };
        if (reachable.size() > 1) for (const Plan* hold : holds) {
            if (stop()) return -1;
            if (hold->points < int64_t(p.x3ChokeMin) * rate ||
                int64_t(original.mainAfter) * 100 >= int64_t(hold->points) * p.x3PokeKeep ||
                !opensQuiet(hold->id[0])) continue;
            bool dominates = true, improves = false;
            for (const Plan* reply : reachable) {
                const Result kept = measure(hold, reply);
                for (const Plan* attack : pressure) {
                    if (stop()) return -1;
                    const Result poked = measure(attack, reply);
                    if (!noWorse(kept, poked)) { dominates = false; break; }
                    improves |= !noWorse(poked, kept);
                }
                if (!dominates) break;
            }
            if (dominates && improves) return stop() ? -1 : hold->id[0];
        }
    }

    // A main chain discounted by the forecast may still be a concrete counterexample.
    // The quiet route must offset each retained exposed main that the poke leaves intact.
    if (!discounted || stop()) return -1;
    for (const Plan& plan : enemy.keeping) if (known(plan, enemy)) add(replies, plan);
    struct Threat { const Plan* line; Duel afterPoke; };
    std::vector<Threat> threats;
    for (const Plan* fire : replies) {
        if (stop()) return -1;
        if (fire->chains < 5 || fire->count != fire->fireDepth || fire->allClear ||
            int64_t(fire->mainAfter) * 100 >= int64_t(enemyMain) * p.x3PokeKeep) continue;
        const Duel d = replay(&original, fire);
        const auto& a = d.player[me];
        const auto& b = d.player[him];
        if (a.moves != o.self().moves + 1 || !theirs(d, fire) ||
            b.fallen || b.incoming.total() || b.scoring.allClear || a.fallen + a.incoming.total() < W) continue;
        threats.push_back({fire, d});
    }
    if (threats.empty()) return -1;
    const auto offset = [&](const Duel& d, const Plan& line, const Plan* foe) {
        const auto& a = d.player[me];
        const auto& b = d.player[him];
        return a.moves == o.self().moves + line.count && a.phase == DuelPhase::Control && a.chain.chains &&
            !a.fallen && !a.incoming.total() && !a.scoring.allClear &&
            b.moves == o.opponent().moves + foe->count && b.chain.chains == foe->chains;
    };
    const Plan* response = nullptr;
    int least = 0;
    for (const Plan* hold : holds) {
        if (stop()) return -1;
        if (hold->count != 2 || !opensQuiet(hold->id[0])) continue;
        int surplus = INT32_MAX;
        for (const Threat& t : threats) {
            if (stop()) return -1;
            const auto d = replay(hold, t.line);
            if (!offset(d, *hold, t.line) || d.player[me].chain.chains != hold->chains) { surplus = -1; break; }
            surplus = std::min(surplus, d.player[him].fallen + d.player[him].incoming.total());
        }
        if (surplus > least) least = surplus, response = hold;
    }
    if (!response) return -1;
    const auto pressureAt = [&](const Threat& t) { return t.afterPoke.player[me].fallen + t.afterPoke.player[me].incoming.total(); };
    const auto strongest = std::max_element(threats.begin(), threats.end(), [&](const Threat& a, const Threat& b) {
        return pressureAt(a) < pressureAt(b);
    });
    // If the normal input paths after the poke can offset even this counterexample,
    // or the search cannot finish verifying its branches, leave the original choice.
    Plan follow = original;
    const auto canOffset = [&](const auto& walk, const ReplayCheckpoint& base, int index) -> bool {
        const auto& before = base.duel.player[me];
        if (index >= own.knownCount || before.fallen) return false;
        for (unsigned places = legalPlacements(before.field); places; places &= places - 1) {
            if (stop()) return true;
            follow.id[index] = int8_t(std::countr_zero(places));
            follow.count = int8_t(index + 1);
            ReplayCheckpoint point;
            const Duel next = replay(&follow, strongest->line, &base, &point);
            if (next.player[me].moves != o.self().moves + index + 1) return true;
            if (!point.duel.rules) {
                if (offset(next, follow, strongest->line)) return true;
                continue;
            }
            const auto& after = point.duel.player[me];
            // Waiting in Control cannot remove incoming or undo an earlier fall.
            if (after.chain.chains && !after.fallen && !after.incoming.total() &&
                offset(replay(&follow, strongest->line, &point), follow, strongest->line)) return true;
            if (walk(walk, point, index + 1)) return true;
        }
        return false;
    };
    ReplayCheckpoint prefix;
    replay(&original, strongest->line, nullptr, &prefix);
    return !prefix.duel.rules || canOffset(canOffset, prefix, 1) || stop() ? -1 : response->id[0];
}

} // namespace juraic::ai::known_poke
