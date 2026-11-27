#pragma once
#include "decision_metrics.h"
#include "build.h"
#include "observe.h"
#include <bit>

namespace juraic::ai::lv38_fire_or_lose {

struct Decision {
    int64_t frame = 0;
    int move = 0, ordinary = -1, selected = -1;
    int colours = 0, points = 0, waves = 0, main = 0;
};

struct Landed {
    GameField field;
    TurnResult chain;
};

inline Landed settle(GameField field, int id, Pair pair, const Observation& o) {
    PlayerCore player{field};
    Incoming receiver;
    place(player.field, placement(id), pair);
    TurnResult chain;
    resolveChain(player, receiver, o.rules, o.timing, o.frame, chain);
    return {player.field, chain};
}

inline bool rivalIsFiring(const Observation& o) {
    const Player& p = o.opponent();
    if (o.busyChain[1 - o.me] || chainShown(p) ||
        (p.phase == Phase::Decide && p.chain.chains)) return true;
    if (p.phase != Phase::Split && p.phase != Phase::Land) return false;
    Wave wave;
    return findWaveFor(p.field.f, wave, o.rules.clearCount, false);
}

inline Decision evaluate(const Observation& o, unsigned legal, int ordinary, const Params& p) {
    metrics::Scope scope(metrics::Part::FireOrLose);
    Decision decision{o.frame, o.self().moves + 1, ordinary};
    if (ordinary < 0 || ordinary >= PLACEMENTS || !(legal >> ordinary & 1u) ||
        o.players != 2 || o.self().phase != Phase::Control ||
        o.self().incoming.total() || rivalIsFiring(o)) return decision;
    const auto& own = o.self();
    decision.colours = own.field.f.normal().count();
    if (decision.colours < p.lv38FireOrLose) return decision;
    Pair pairs[3];
    if (copySearchPairs(o, o.me, true, pairs, 3) != 3) return decision;

    readMainChain(own.field, o.rules, o.timing, p, &decision.main);
    const auto substantial = [&](const TurnResult& chain) {
        return chain.chains >= 5 && int64_t(chain.points) * 100 >= int64_t(decision.main) * 80;
    };
    int best = -1;
    for (unsigned ids = legal; ids; ids &= ids - 1) {
        const int id = std::countr_zero(ids);
        const auto candidate = settle(own.field, id, pairs[0], o);
        if (substantial(candidate.chain) &&
            (candidate.chain.points > decision.points || (candidate.chain.points == decision.points && id == ordinary))) {
            best = id;
            decision.points = candidate.chain.points;
            decision.waves = candidate.chain.chains;
        }
    }
    if (best < 0 || best == ordinary) return decision;

    const GameField afterOrdinary = settle(own.field, ordinary, pairs[0], o).field;
    // Each visible pair is tried on the same settled board, immediately after the ordinary move.
    if (!afterOrdinary.dead()) {
        for (int k = 1; k <= 2; ++k) {
            for (unsigned ids = legalPlacements(afterOrdinary); ids; ids &= ids - 1) {
                const int id = std::countr_zero(ids);
                if (substantial(settle(afterOrdinary, id, pairs[k], o).chain)) return decision;
            }
        }
    }
    decision.selected = best;
    return decision;
}

} // namespace juraic::ai::lv38_fire_or_lose
