#pragma once
#include "decision_metrics.h"

#include "exchange.h"
#include "receive.h"

namespace juraic::ai::lv38_lifeline {

enum class Survival { Unknown, Dangerous, Safe };

// For a single partial row, testing the death column first covers the lethal
// layout. When the actual order is known, replay that order instead.
template<class Stop>
int prefer(const Observation& o, const Duel& afterChain, const PlanSet& mine, const PlanSet& theirs,
           int ordinary, unsigned legal, int arriving, const Params& p, uint64_t seed, Stop stop) {
    metrics::Scope scope(metrics::Part::Lifeline);
    const int me = o.me, him = 1 - me;
    const bool pending = chainShown(o.opponent()) && !o.self().incoming.confirmed &&
        o.self().incoming.pending && afterChain.player[him].phase == DuelPhase::Control &&
        !afterChain.player[him].scoring.allClear && afterChain.player[him].chain.chains <= 4;
    const bool confirmed = o.self().incoming.confirmed == arriving && !o.self().incoming.pending &&
        (o.opponent().phase == Phase::Next || o.opponent().phase == Phase::Control);
    if (!p.lv38Midgame || o.players != 2 || p.handWait || ordinary < 0 || !(legal >> ordinary & 1u) ||
        !mine.knownCount || o.busyLeft[me] || (!pending && !confirmed) || o.opponent().incoming.total() ||
        o.self().scoring.allClear || o.opponent().scoring.allClear || arriving <= 0 || arriving >= W ||
        arriving > o.rules.maxGarbagePerDrop || stop()) return ordinary;
    int heights[W];
    o.self().field.heights(heights);
    if (heights[DEATH_X] != DEATH_Y) return ordinary;

    Observation view = o;
    if (!view.nextGarbageOrder[me]) view.nextGarbageOrder[me] = smallDropOrder(1u << DEATH_X);
    KnownReplay bounded{stop};
    const auto play = [&](const Plan& path, ReplayCheckpoint& finish, const ReplayCheckpoint* start = nullptr) {
        return playOut(view, mine, &path, theirs, nullptr, p, seed, true, nullptr, &bounded, start, &finish);
    };
    const auto assess = [&](const Duel& d, const ReplayCheckpoint& finish, int depth) {
        const auto& own = d.player[me];
        if (own.moves != o.self().moves + depth || stop()) return Survival::Unknown;
        if (own.phase == DuelPhase::Dead && own.fallen > 0) return Survival::Dangerous;
        if (!finish.duel.rules || own.phase != DuelPhase::Control || own.incoming.total()) return Survival::Unknown;
        // If more waves could arrive, the complete original attack has not
        // been resolved. Check through the opponent's next spawn.
        if (d.player[him].phase == DuelPhase::Check || d.player[him].phase == DuelPhase::Settle) {
            Plan ended;
            const auto end = playOut(view, mine, &ended, theirs, nullptr, p, seed, true, nullptr, &bounded, &finish);
            if (end.player[me].incoming.total() || stop()) return Survival::Unknown;
        }
        return Survival::Safe;
    };

    Plan regular;
    regular.count = 1;
    regular.id[0] = int8_t(ordinary);
    ReplayCheckpoint prefix;
    const auto before = play(regular, prefix);
    const auto safety = [&](const auto& visit, Plan path, const Duel& replayed, const ReplayCheckpoint& checkpoint) -> Survival {
        const auto outcome = assess(replayed, checkpoint, path.count);
        if (outcome != Survival::Unknown || !checkpoint.duel.rules ||
            path.count >= std::min(mine.knownCount, 3) || stop()) return outcome;
        bool danger = false;
        const int index = path.count++;
        const auto& field = replayed.player[me].field;
        for (unsigned moves = knownPlacements(field, mine, index, p); moves; moves &= moves - 1) {
            if (stop()) return Survival::Unknown;
            path.id[index] = int8_t(std::countr_zero(moves));
            ReplayCheckpoint finish;
            const auto d = play(path, finish, &checkpoint);
            if (d.player[me].moves != o.self().moves + path.count) continue;
            const auto following = visit(visit, path, d, finish);
            if (following != Survival::Dangerous) return following;
            danger = true;
        }
        return danger ? Survival::Dangerous : Survival::Unknown;
    };
    if (safety(safety, regular, before, prefix) != Survival::Dangerous || stop()) return ordinary;

    // Ordinary play has no proved safe continuation in the available window.
    // Prefer a short wipe of the incoming packet; use a main only to survive.
    int best = ordinary, least = 5, retained = -1, sent = -1;
    const auto candidate = [&](const Plan& path, const ReplayCheckpoint* start = nullptr) {
        ReplayCheckpoint finish;
        const auto d = play(path, finish, start);
        const auto& own = d.player[me];
        if (!own.chain.chains || own.scoring.allClear || own.fallen || assess(d, finish, path.count) != Survival::Safe) return;
        const int type = own.chain.chains <= 4 ? path.count : path.count + 2;
        if (type > least) return;
        int reserve = 0;
        if (type < 3) buildValue(own.field, o.rules, o.timing, p, &reserve);
        const int pressure = d.player[him].incoming.total() + d.player[him].fallen;
        if (type < least || reserve > retained || (reserve == retained && pressure > sent)) {
            best = path.id[0], least = type, retained = reserve, sent = pressure;
        }
    };
    const unsigned roots = legal & (mine.firstInAir() ? mine.firstReach : legalPlacements(o.self().field));
    unsigned quiet = 0;
    for (unsigned moves = roots & ~(1u << ordinary); moves; moves &= moves - 1) {
        if (stop()) return ordinary;
        Plan path;
        path.count = 1;
        path.id[0] = int8_t(std::countr_zero(moves));
        GameField field = o.self().field;
        place(field, placement(path.id[0]), mine.pair[0]);
        if (anyClear(field.f, o.rules)) candidate(path);
        else if (!field.dead()) quiet |= 1u << path.id[0];
    }
    if (least == 1 || mine.knownCount < 2) return stop() ? ordinary : best;
    for (; quiet; quiet &= quiet - 1) {
        if (stop()) return ordinary;
        Plan path;
        path.count = 1;
        path.id[0] = int8_t(std::countr_zero(quiet));
        ReplayCheckpoint afterFirst;
        const auto d = play(path, afterFirst);
        const auto& own = d.player[me];
        if (!afterFirst.duel.rules || own.moves != o.self().moves + 1 || own.fallen || !own.incoming.total()) continue;
        path.count = 2;
        for (unsigned moves = knownPlacements(own.field, mine, 1, p); moves; moves &= moves - 1) {
            if (stop()) return ordinary;
            path.id[1] = int8_t(std::countr_zero(moves));
            GameField field = own.field;
            place(field, placement(path.id[1]), mine.pair[1]);
            if (anyClear(field.f, o.rules)) candidate(path, &afterFirst);
        }
    }
    return stop() ? ordinary : best;
}

// A wave can be on its way before any garbage appears above us. The
// opponent's own confirmed garbage stays in the replay so later waves
// cancel it before sending the excess.
template<class Stop>
int preferUnannouncedWave(const Observation& o, const Duel& afterChain, const PlanSet& mine, const PlanSet& theirs,
                          int ordinary, unsigned legal, int arriving, const Params& p, uint64_t seed, Stop stop) {
    metrics::Scope scope(metrics::Part::UnannouncedWave);
    const int me = o.me, him = 1 - me;
    if (!p.lv38Midgame || o.players != 2 || p.handWait || o.busyLeft[me] ||
        o.self().incoming.total() || !chainShown(o.opponent()) || o.self().scoring.allClear ||
        o.opponent().scoring.allClear || afterChain.player[him].scoring.allClear ||
        afterChain.player[him].phase != DuelPhase::Control || afterChain.player[him].chain.chains > 4 ||
        mine.knownCount < 3 || ordinary < 0 || !(legal >> ordinary & 1u) ||
        arriving <= 0 || arriving >= W || arriving > o.rules.maxGarbagePerDrop || stop()) return ordinary;
    int heights[W];
    o.self().field.heights(heights);
    if (heights[DEATH_X] != DEATH_Y) return ordinary;

    Observation view = o;
    KnownReplay bounded{stop};
    // Surviving the drop is not enough. Reach the next spawn after all
    // three shown pairs, with no remaining garbage from this chain.
    const auto survives = [&](const auto& visit, Plan path, int id, const ReplayCheckpoint* start, Plan& witness) -> bool {
        if (stop()) return false;
        path.id[path.count++] = int8_t(id);
        ReplayCheckpoint finish;
        const auto d = playOut(view, mine, &path, theirs, nullptr, p, seed, true, nullptr, &bounded, start, &finish);
        const auto& own = d.player[me];
        if (!finish.duel.rules || own.moves != o.self().moves + path.count || own.phase != DuelPhase::Control)
            return false;
        if (path.count == 3) {
            if (own.incoming.total() || d.player[him].phase != DuelPhase::Control) return false;
            witness = path;
            return true;
        }
        for (unsigned moves = knownPlacements(own.field, mine, path.count, p); moves; moves &= moves - 1)
            if (visit(visit, path, std::countr_zero(moves), &finish, witness)) return true;
        return false;
    };
    const auto order = view.nextGarbageOrder[me];
    if (!order) view.nextGarbageOrder[me] = smallDropOrder(1u << DEATH_X);
    Plan witness;
    if (survives(survives, Plan{}, ordinary, nullptr, witness) || stop()) return ordinary;
    constexpr unsigned columns = (1u << W) - 1;
    for (unsigned roots = legal & knownPlacements(o.self().field, mine, 0, p) & ~(1u << ordinary);
         roots; roots &= roots - 1) {
        if (stop()) return ordinary;
        const int id = std::countr_zero(roots);
        view.nextGarbageOrder[me] = order ? *order : smallDropOrder(1u << DEATH_X);
        if (!survives(survives, Plan{}, id, nullptr, witness)) continue;
        if (!order) {
            // The proof uses one sequence for every order. A placement made
            // before the fall cannot depend on its still-hidden columns.
            bool proved = true;
            for (unsigned mask = 1; mask < columns; ++mask) {
                if (std::popcount(mask) > arriving || mask == (1u << DEATH_X)) continue;
                if (stop()) return ordinary;
                view.nextGarbageOrder[me] = smallDropOrder(mask);
                ReplayCheckpoint finish;
                const auto d = playOut(view, mine, &witness, theirs, nullptr, p, seed, true, nullptr, &bounded,
                                       nullptr, &finish);
                const auto& own = d.player[me];
                if (!finish.duel.rules || own.moves != o.self().moves + witness.count ||
                    own.phase != DuelPhase::Control || own.incoming.total() || d.player[him].phase != DuelPhase::Control) {
                    proved = false;
                    break;
                }
            }
            if (!proved) continue;
        }
        return stop() ? ordinary : id;
    }
    return ordinary;
}

} // namespace juraic::ai::lv38_lifeline
