#pragma once
#include "exchange.h"

namespace juraic::ai {

// A reversible quiet pair can lock or become ready sooner in ordinary play.
// The exchange uses the canonical path, so use it only when the alternate
// cannot be selected during that path. A rival's input settings are unknown.
inline bool stablePartialBuildRoute(const Duel& duel, int who, Placement target, Pair pair,
                                    int64_t from, int64_t lock, bool equivalent) {
    if (!equivalent || pair.axis < 1 || pair.axis > MAX_COLORS || pair.axis != pair.child) return true;
    const auto& own = duel.player[who];
    const int reverse = reversePlacement(placementId(target));
    if (own.incoming.total() || !(legalPlacements(own.field) >> reverse & 1u)) return true;
    GameField reversed = own.field;
    juraic::place(reversed, placement(reverse), pair);
    Wave wave;
    if (findWaveFor(reversed.f, wave, duel.rules->clearCount, false)) return true;

    const auto& foe = duel.player[1 - who];
    if (lock < 0 || foe.phase != DuelPhase::Check || foe.lockFrame >= from) return false;
    const bool clears = findWaveFor(foe.field.f, wave, duel.rules->clearCount, false);
    if (!foe.chain.chains && !clears) return false;
    const Timing& timing = *duel.timing;
    const int waveMinimum = std::min({timing.waveFrames(0), timing.waveFrames(1), timing.waveFrames(ROW_14)});
    return lock < foe.at + (clears ? waveMinimum : 0);
}

// When a nearly full field is about to spend its visible main on an incidental
// single, compare the immediate fire with every visible continuation of the
// single against each retained, visible opposing main. The idle beam still
// has to put the immediate fire close to the ordinary build.
template<class Stop>
int preferKnownPartialBuild(const Observation& o, const PlanSet& mine, const PlanSet& theirs, const BuildResult& beam,
                            int chosen, unsigned allowed, const Params& p, uint64_t seed, int rate, Stop stop) {
    if (chosen < 0 || o.players != 2 || !p.readMidgame || !p.checkKnownExchange || p.handWait || o.rules.cancel ||
        beam.cut || beam.told != p.queues() || theirs.pairCount < 2 || mine.pairCount < 2 || mine.pairCount > 3 ||
        o.self().incoming.total() || o.opponent().incoming.total() || o.opponent().chain.chains ||
        o.self().scoring.allClear || o.opponent().scoring.allClear ||
        (o.self().phase != Phase::Control && (o.self().phase != Phase::Next || o.busyLeft[o.me])) || stop()) return chosen;

    const Plan* single = nullptr;
    const Plan* main = nullptr;
    for (const auto* list : {&mine.plans, &mine.tactical, &mine.keeping}) for (const Plan& s : *list) {
        if (s.count != 1 || s.fireDepth != 1) continue;
        if (s.id[0] == chosen && s.chains == 1 && s.sent < W && !s.allClear) single = &s;
        if ((allowed >> s.id[0] & 1u) && s.sent >= p.x3ChokeMin &&
            (!main || s.sent > main->sent || (s.sent == main->sent && s.endFrame < main->endFrame))) main = &s;
    }
    if (!single || !main || main->id[0] == chosen) return chosen;

    // Use the undisturbed beam for the idle comparison only. A synthetic
    // queue never establishes a concrete attack or response below.
    const int immediate = beam.each[0][main->id[0]];
    if (immediate < p.x3ChokeMin * rate ||
        beam.sum[main->id[0]] * 1000 < beam.sum[chosen] * (1000 - p.x3GuardNear)) return chosen;
    for (int q = 0; q < beam.told; ++q)
        if (beam.each[q][main->id[0]] != immediate) return chosen;
    if (beam.enough(chosen, immediate - W * rate + 1) == beam.told) return chosen;

    const int me = o.me, him = 1 - me;
    bool uncertainRoute = false;
    const auto interrupted = [&] { return uncertainRoute || stop(); };
    KnownReplay replay{interrupted};
    replay.acceptPlacement = [&](const Duel& duel, int who, Placement target, Pair pair, int64_t from, int64_t lock) {
        const bool equivalent = who != me || p.equivalentSetupRoute;
        if (stablePartialBuildRoute(duel, who, target, pair, from, lock, equivalent)) return true;
        uncertainRoute = true;
        return false;
    };
    const auto burden = [](const DuelPlayer& player) { return player.fallen + player.incoming.total(); };
    bool improves = false;
    std::vector<const Plan*> checked;
    for (const auto* list : {&theirs.plans, &theirs.tactical, &theirs.keeping}) for (const Plan& threat : *list) {
        if (!threat.fireDepth || threat.fireDepth != threat.count || threat.count > theirs.pairCount || threat.sent < p.x3ChokeMin) continue;
        if (interrupted() || threat.fireDepth == 1) return chosen;
        if (std::any_of(checked.begin(), checked.end(), [&](const Plan* previous) { return samePlacements(*previous, threat); })) continue;
        checked.push_back(&threat);

        const Duel exchange = playOut(o, mine, main, theirs, &threat, p, seed, true, nullptr, &replay);
        const auto& own = exchange.player[me];
        const auto& foe = exchange.player[him];
        const int remaining = burden(own);
        if (interrupted() || own.moves != o.self().moves + 1 || own.chain.chains != main->chains || own.field.dead()) return chosen;
        if (!remaining && (exchange.state == RoundState::Playing || exchange.state == (me ? RoundState::Won1 : RoundState::Won0))) continue;
        const auto sameAttack = [&](const DuelPlayer& player) {
            return player.moves == o.opponent().moves + threat.count && player.chain.chains == threat.chains;
        };
        if (exchange.state != RoundState::Playing || own.phase != DuelPhase::Control || !sameAttack(foe) || remaining < W)
            return chosen;

        Plan route = *single;
        bool verified = false;
        const auto keepsOrdinary = [&](auto&& visit, const ReplayCheckpoint* before) -> bool {
            const Duel settled = playOut(o, mine, &route, theirs, &threat, p, seed, true, nullptr, &replay, before);
            const auto& alternative = settled.player[me];
            const auto& opposing = settled.player[him];
            if (interrupted()) return true;
            if (alternative.moves == o.self().moves + route.count && !alternative.field.dead()) {
                if (!sameAttack(opposing) || (foe.scoring.allClear && !opposing.scoring.allClear) || alternative.scoring.allClear ||
                    alternative.fallen < own.fallen || burden(alternative) < remaining + W) return true;
                verified = true;
            }
            if (route.count >= mine.pairCount) return false;
            ReplayCheckpoint next;
            const Duel prefix = playOut(o, mine, &route, theirs, &threat, p, seed, true, nullptr, &replay, before, &next);
            const auto& waiting = prefix.player[me];
            if (interrupted()) return true;
            if (waiting.moves != o.self().moves + route.count || waiting.phase != DuelPhase::Control ||
                next.next[me] != route.count || waiting.field.dead()) return false;
            const int index = route.count++;
            for (unsigned ids = knownPlacements(waiting.field, mine, index, p); ids; ids &= ids - 1) {
                route.id[index] = int8_t(std::countr_zero(ids));
                if (visit(visit, &next)) { --route.count; return true; }
            }
            --route.count;
            return false;
        };
        if (keepsOrdinary(keepsOrdinary, nullptr) || !verified) return chosen;
        improves = true;
    }
    return improves && !interrupted() ? main->id[0] : chosen;
}

} // namespace juraic::ai
