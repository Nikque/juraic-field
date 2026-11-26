#pragma once

#include "exchange.h"

namespace juraic::ai::side_response {

struct Ready {
    GameField field;
    int moves = -1;
    int next = -1;
    int reserve = 0;
    int incoming = 0;
};

struct Window {
    int64_t firstWave = 0;
    int64_t firstConfirm = 0;
};

// The other player can ignore the exchange or place either visible pair at any speed.
// Overestimate his reach: even a geometrically legal placement without known buttons
// counts here. The lower bound on the first unseen turn uses the full landing animation:
// time spent on the ground before locking already counts towards the elapsed input.
template<class Stop>
Window quietWindow(const Observation& o, const Duel& afterChain, const PlanSet& theirs, Stop stop) {
    const DuelPlayer& enemy = afterChain.player[1 - o.me];
    if (afterChain.state != RoundState::Playing || enemy.phase != DuelPhase::Control ||
        enemy.spawnFrame < o.frame || enemy.fallen || enemy.incoming.total() || enemy.scoring.allClear ||
        theirs.knownCount != 2 || theirs.midAir) return {};
    bool reached = false;
    for (unsigned open = legalPlacements(enemy.field); open; open &= open - 1) {
        if (stop()) return {};
        GameField first = enemy.field;
        place(first, placement(std::countr_zero(open)), theirs.pair[0]);
        if (anyClear(first.f, o.rules)) return {};
        if (first.dead()) continue;
        for (unsigned follow = legalPlacements(first); follow; follow &= follow - 1) {
            if (stop()) return {};
            GameField second = first;
            place(second, placement(std::countr_zero(follow)), theirs.pair[1]);
            if (anyClear(second.f, o.rules)) return {};
            reached |= !second.dead();
        }
    }
    if (!reached) return {};
    const int64_t firstWave = enemy.spawnFrame + 2 * o.timing.nextFrames + 3 * (o.timing.landFrames - 1);
    return {firstWave, firstWave + o.timing.waveFrames(0)};
}

template<class Stop>
int prefer(const Observation& o, const Duel& afterChain, const PlanSet& mine, const PlanSet& theirs,
           int ordinary, unsigned legal, int arriving, const Params& p, uint64_t seed, Ready& ready, Stop stop) {
    if (!p.readMidgame || !p.checkKnownExchange || p.handWait || o.players != 2 ||
        mine.knownCount < 2 ||
        ordinary < 0 || !(legal >> ordinary & 1u) || o.busyLeft[o.me] ||
        !chainShown(o.opponent()) || !o.self().incoming.pending || o.self().incoming.confirmed ||
        o.opponent().incoming.total() || o.self().scoring.allClear || o.opponent().scoring.allClear ||
        arriving <= W || stop()) return ordinary;
    if (o.self().phase != Phase::Next || o.self().wait != 1 || mine.midAir) return ordinary;
    const auto current = appearingPair(o.self(), o.rules, o.timing);
    if (current.queued || current.held) return ordinary;

    const bool follow = ready.next >= 0 && o.self().moves == ready.moves && o.self().field == ready.field &&
        arriving == ready.incoming && (legal >> ready.next & 1u);
    KnownReplay replay{stop};
    int minimum = follow ? ready.reserve : 0;
    if (!follow) {
        int main = 0;
        for (const Plan& plan : mine.plans) {
            if (plan.id[0] == ordinary && plan.count == 2 && plan.fireDepth == 2 && plan.chains >= 5 && !plan.allClear &&
                plan.fallen == 0 && plan.sent >= W &&
                plan.points > main && int64_t(plan.mainAfter) * 100 < int64_t(plan.points) * p.x3KeepMain) {
                ReplayCheckpoint finish;
                const Duel d = playOut(o, mine, &plan, theirs, nullptr, p, seed, true, nullptr, &replay, nullptr, &finish);
                const auto& ours = d.player[o.me];
                if (finish.duel.rules && ours.moves == o.self().moves + 2 && ours.chain.chains == plan.chains &&
                    !ours.fallen && !ours.scoring.allClear && d.player[1 - o.me].incoming.total() >= W)
                    main = plan.points;
            }
            if (stop()) return ordinary;
        }
        if (!main) return ordinary;
        minimum = int((int64_t(main) * p.x3KeepMain + 99) / 100);
    }

    const Window window = quietWindow(o, afterChain, theirs, stop);
    if (!window.firstWave || stop()) return ordinary;
    int preferred = ordinary;
    Ready proposed;
    int64_t earliest = window.firstConfirm;
    int strongest = 0;
    struct Settled {
        int64_t at = 0;
        int reserve = 0;
    };
    const auto evaluate = [&](const Plan& path, const ReplayCheckpoint* before) {
        ReplayCheckpoint after;
        const Duel d = playOut(o, mine, &path, theirs, nullptr, p, seed, true, nullptr, &replay, before, &after);
        const auto& ours = d.player[o.me];
        const auto& enemy = d.player[1 - o.me];
        if (!after.duel.rules || ours.moves != o.self().moves + path.count || !ours.chain.chains ||
            ours.chain.chains > 2 || ours.fallen || enemy.fallen || ours.incoming.total() || enemy.incoming.total() ||
            ours.scoring.allClear || enemy.scoring.allClear || ours.spawnFrame > window.firstConfirm ||
            ours.spawnFrame - o.timing.nextFrames - o.timing.waveFrames(0) >= window.firstWave || stop()) return Settled{};
        Trials trials;
        detect(ours.field, o.rules, o.timing, 1, trials, nullptr, false);
        const Trial* key = trials.best();
        return key && key->points >= minimum && key->chains >= o.rules.chainRequirement
            ? Settled{ours.spawnFrame, key->points} : Settled{};
    };
    const auto shortContinuations = [&](int root, const auto& use) {
        Plan path;
        path.count = 1;
        path.id[0] = int8_t(root);
        ReplayCheckpoint initial;
        const Duel d = playOut(o, mine, &path, theirs, nullptr, p, seed, true, nullptr, &replay, nullptr, &initial);
        const auto& ours = d.player[o.me];
        if (!initial.duel.rules || ours.moves != o.self().moves + 1 || ours.chain.chains || ours.fallen ||
            ours.scoring.allClear || ours.spawnFrame >= window.firstConfirm) return false;
        path.count = 2;
        for (unsigned next = legalPlacements(ours.field); next; next &= next - 1) {
            if (stop()) return false;
            const int id = std::countr_zero(next);
            path.id[1] = int8_t(id);
            const auto settled = evaluate(path, &initial);
            if (settled.reserve && use(ours, id, settled)) return true;
        }
        return false;
    };
    if (follow) {
        Plan saved, normal;
        saved.count = normal.count = 1;
        saved.id[0] = int8_t(ready.next);
        normal.id[0] = int8_t(ordinary);
        if (ordinary == ready.next || evaluate(normal, nullptr).reserve ||
            shortContinuations(ordinary, [](const DuelPlayer&, int, const Settled&) { return true; })) return ordinary;
        return evaluate(saved, nullptr).reserve && !stop() ? ready.next : ordinary;
    }
    for (unsigned first = legal; first; first &= first - 1) {
        if (stop()) return ordinary;
        const int root = std::countr_zero(first);
        shortContinuations(root, [&](const DuelPlayer& ours, int id, const Settled& settled) {
            // Keep the current root if the same short response is available from it.
            if (proposed.next >= 0) {
                if (root != ordinary && preferred == ordinary) return false;
                if ((root != ordinary || preferred == ordinary) &&
                    (settled.at > earliest || (settled.at == earliest && settled.reserve <= strongest))) return false;
            }
            preferred = root;
            proposed = {ours.field, ours.moves, id, minimum, arriving};
            earliest = settled.at;
            strongest = settled.reserve;
            return false;
        });
    }
    if (stop() || proposed.next < 0) return ordinary;
    ready = proposed;
    return preferred;
}

} // namespace juraic::ai::side_response
