#pragma once
#include "known_poke.h"

namespace juraic::ai::answer_window {

// Keep a first placement that can either clear the current attack in one
// short wave or fire a known main on the next pair. The second action is
// decided anew. No continuation depends on guessing the opponent's input.
template<class Stop>
int prefer(const Observation& o, const PlanSet& mine, const PlanSet& theirs, const Plan& ordinary,
           const Plan* immediate, const Duel& afterChain, unsigned legal, int arriving, const Params& p, uint64_t seed, const Stop& expired, PictureRoutes* picture = nullptr) {
    const int me = o.me, him = 1 - me;
    const auto& self = o.self();
    const auto& rival = o.opponent();
    const auto& incoming = afterChain.player[me].incoming;
    const auto nextRival = afterChain.player[him].spawnFrame;
    if (!p.readMidgame || !p.checkKnownExchange || p.handWait ||
        mine.midAir || ownPairInAir(o) || self.phase != Phase::Next || self.wait <= 0 || o.busyLeft[me] || !chainShown(rival) ||
        rival.chain.chains > 4 || arriving < W || arriving >= 2 * W || self.incoming.confirmed ||
        self.incoming.pending != arriving || incoming.confirmed != arriving || incoming.pending ||
        rival.incoming.total() || self.scoring.allClear || rival.scoring.allClear ||
        afterChain.state != RoundState::Playing || afterChain.player[him].phase != DuelPhase::Control || nextRival <= o.frame ||
        mine.knownCount < 2 || mine.knownCount > 3 || !theirs.knownCount || p.x3ChokeMin <= 0 ||
        ordinary.count != 2 || ordinary.fireDepth != 2 || ordinary.chains > 4 || ordinary.allClear || expired()) return -1;
    Pair shown[3];
    if (copySearchPairs(o, me, true, shown, mine.knownCount) != mine.knownCount ||
        !std::equal(shown, shown + mine.knownCount, mine.pair, samePair) ||
        !(appearingPair(self, o.rules, o.timing) == spawnPair(o.timing))) return -1;

    const auto bit = [](int id) { return 1u << id; };
    const unsigned quiet = quietSetupChoices(o, mine, legal);
    const int original = ordinary.id[0];
    if (!(quiet & bit(original))) return -1;
    const auto completes = [&](const Duel& a, const Plan& line) {
        const auto& own = a.player[me];
        return known_poke::followed(own, self.moves, &line) && !own.scoring.allClear && !a.player[him].scoring.allClear;
    };
    KnownReplay bounded{expired};
    bounded.picture = picture;
    const auto look = [&](const Plan& own, const Plan* other = nullptr, int64_t* start = nullptr,
                          const ReplayCheckpoint* previous = nullptr, ReplayCheckpoint* next = nullptr) {
        if (!start) return playOut(o, mine, &own, theirs, other, p, seed, true, nullptr, &bounded, previous, next);
        // Known replay can report the check frame of the second set's final placement.
        return playOut(o, theirs, other, mine, &own, p, seed, true, start, &bounded);
    };
    const auto noLoss = [&](const Duel& d) { return !d.player[me].fallen && !d.player[me].incoming.total(); };
    const Duel standing = look(ordinary);
    if (expired() || !completes(standing, ordinary) || !noLoss(standing)) return -1;
    const auto reserve = [&](const Duel& d) {
        int main = 0;
        readMainChain(d.player[me].field, o.rules, o.timing, p, &main);
        return main;
    };
    const int minimumReserve = reserve(standing);
    if (immediate && (immediate->chains <= 4 || int64_t(immediate->mainAfter) * 100 >= int64_t(minimumReserve) * p.x3PokeKeep)) return -1;
    const int minimum = std::max({p.x3ChokeMin * o.rules.rateAt(o.frame),
        int(int64_t(minimumReserve) * std::clamp(p.x3ChokeKeep, 0, 100) / 100), immediate ? immediate->points : 0});

    std::vector<const Plan*> replies;
    for (const auto* list : {&theirs.plans, &theirs.tactical, &theirs.keeping}) {
        for (const Plan& t : *list) {
            if (t.count != 1 || t.fireDepth != 1) continue;
            if (t.allClear) return -1;
            if (t.sent > 0) known_poke::add(replies, t);
        }
    }
    if (replies.empty()) return -1;
    std::vector<int> ordinaryLoss;
    bool loses = false;
    for (const Plan* t : replies) {
        if (expired()) return -1;
        const Duel d = look(ordinary, t);
        const auto& a = d.player[me];
        const auto& b = d.player[him];
        if (!completes(d, ordinary) || !known_poke::followed(b, rival.moves, t)) return -1;
        ordinaryLoss.push_back(a.fallen + a.incoming.total());
        loses = loses || a.fallen > 0;
    }
    if (!loses) return -1;

    // Veto the replacement if the ordinary first move can already fire a
    // comparable known main. Check all remaining shown placements, not only
    // the representatives kept by Gaze.
    Plan path;
    const auto hasOwnMain = [&](const auto& visit, unsigned choices, int depth, const ReplayCheckpoint* from) -> bool {
        for (; choices && !expired(); choices &= choices - 1) {
            path.id[depth] = int8_t(std::countr_zero(choices));
            const auto examine = [&](const Duel& d, const ReplayCheckpoint& next) {
                const auto& a = d.player[me];
                if (a.moves != self.moves + depth + 1 || !next.duel.rules || a.field.dead()) return false;
                if (a.scoring.allClear || (a.chain.chains >= 5 && a.scoring.score - self.scoring.score >= minimum)) return true;
                return depth + 1 < mine.knownCount && visit(visit, legalPlacements(a.field), depth + 1, &next);
            };
            path.count = int8_t(depth + 1);
            ReplayCheckpoint next;
            const auto d = look(path, nullptr, nullptr, from, &next);
            const int before = from ? from->duel.player[me].fallen : 0;
            const int remainder = (d.player[me].fallen - before) % W;
            const bool drawMatters = !o.nextGarbageOrder[me] && remainder > 0 && depth + 1 < mine.knownCount;
            if (!drawMatters) {
                if (examine(d, next)) return true;
                continue;
            }
            for (unsigned mask = 1; mask < 1u << W && !expired(); ++mask) {
                if (std::popcount(mask) != remainder) continue;
                const auto order = smallDropOrder(mask);
                path.count = int8_t(depth + 1);
                ReplayCheckpoint restored;
                if (from) {
                    auto copy = *from;
                    copy.duel.nextGarbageOrder[me] = order;
                    const auto other = look(path, nullptr, nullptr, &copy, &restored);
                    if (examine(other,restored)) return true;
                } else {
                    auto view = o;
                    view.nextGarbageOrder[me] = order;
                    const auto other = playOut(view, mine, &path, theirs, nullptr, p, seed, true, nullptr, &bounded, nullptr, &restored);
                    if (examine(other,restored)) return true;
                }
            }
        }
        return false;
    };
    if (hasOwnMain(hasOwnMain, bit(original), 0, nullptr) || expired()) return -1;

    std::vector<const Plan*> mains, shortReplies;
    for (const auto* list : {&mine.plans, &mine.tactical, &mine.keeping}) for (const Plan& a : *list) {
        if (a.fireDepth != 2 || a.count != 2 || a.allClear || !(quiet & bit(a.id[0])) || a.id[0] == original) continue;
        if (a.chains >= 5 && a.points >= minimum) known_poke::add(mains, a);
        if (a.chains == 1) known_poke::add(shortReplies, a);
    }
    unsigned ready = 0;
    for (const Plan* main : mains) {
        if (expired()) return -1;
        const Duel d = look(*main);
        if (completes(d, *main) && noLoss(d) && d.player[me].lockFrame < nextRival) ready |= bit(main->id[0]);
    }
    unsigned choices = 0;
    for (const Plan* quick : shortReplies) {
        if (expired()) return -1;
        const auto candidate = bit(quick->id[0]);
        if (!(ready & candidate) || (choices & candidate)) continue;
        int64_t ignition = -1;
        const Duel d = look(*quick, nullptr, &ignition);
        const auto& a = d.player[me];
        if (ignition < 0 || ignition >= nextRival || !completes(d, *quick) || !noLoss(d) ||
            a.spawnFrame >= standing.player[me].spawnFrame || reserve(d) < minimumReserve) continue;
        bool safe = true;
        for (size_t i = 0; i < replies.size(); ++i) {
            if (expired()) return -1;
            const Duel exchange = look(*quick, replies[i]);
            const auto& own = exchange.player[me];
            if (!completes(exchange, *quick) || own.fallen || own.incoming.total() > ordinaryLoss[i]) { safe = false; break; }
        }
        if (safe) choices |= candidate;
    }
    return std::popcount(choices) == 1 && !expired() ? std::countr_zero(choices) : -1;
}

} // namespace juraic::ai::answer_window
