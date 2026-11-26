#pragma once
#include "gaze.h"

namespace juraic::ai {

inline std::array<uint8_t, W> smallDropOrder(unsigned mask) {
    constexpr unsigned columns = (1u << W) - 1;
    std::array<uint8_t, W> order{};
    int index = 0;
    for (unsigned left : {mask, columns ^ mask})
        for (; left; left &= left - 1) order[index++] = uint8_t(std::countr_zero(left));
    return order;
}

// Take a confirmed small attack only when each possible drop leaves an answer
// with the next visible pair. It must fire before the opponent's earliest new
// confirmation and offset his largest visible follow-up. The move must gain a
// row over the original fire or use a side chain and leave the main chain ready.
template<class Stop>
int smallReceive(const Observation& o, const PlanSet& mine, const PlanSet& theirs, const Plan& immediate,
                 unsigned legal, int arriving, const Params& p, uint64_t seed, Stop stop, int preferred = -1) {
    const int me = o.me, him = 1 - me;
    const int garbage = o.self().incoming.confirmed;
    if (!p.readMidgame || p.handWait > 0 || o.state != RoundState::Playing || o.self().field.f.count() < 48 ||
        garbage <= 0 || garbage > W || garbage > o.rules.maxGarbagePerDrop || arriving != garbage ||
        o.self().incoming.pending || o.opponent().incoming.total() || o.self().scoring.allClear || o.opponent().scoring.allClear ||
        immediate.fireDepth != 1 || immediate.count != 1 || immediate.chains < 5 || immediate.allClear ||
        immediate.mainAfter >= immediate.points / 2 || mine.knownCount < 2 || theirs.knownCount < 2 || stop()) return -1;
    if (!mine.midAir && (o.self().phase != Phase::Next || o.busyLeft[me])) return -1;

    constexpr unsigned columns = (1u << W) - 1;
    const int64_t limit = o.frame + 6000;
    Duel initial(o, seed);
    static thread_local SpawnInputCache inputs;
    inputs.configure(o.timing, o.rules.speedPercent);
    const auto places = [&](const Duel& d, const PlanSet& set, int k) {
        return k == 0 && set.firstInAir() ? set.firstReach : legalPlacements(d.player[set.who].field);
    };
    const auto placeKnown = [&](Duel& d, const PlanSet& set, int k, int id) {
        const int who = set.who;
        if (k == 0 && set.firstInAir()) {
            d.place(who, placement(id), set.pair[k], set.firstLock[id], set.firstSoft[id], set.firstRest[id]);
            return true;
        }
        const DuelPlayer& player = d.player[who];
        const SpawnInput input = inputs.get(player.field, placement(id));
        if (!input.frames) return false;
        d.place(who, placement(id), set.pair[k], player.spawnFrame + input.frames - 1, input.soft, input.rested);
        return true;
    };
    const auto clears = [&](const GameField& field, Pair pair, int id) {
        GameField next = field;
        juraic::place(next, placement(id), pair);
        Wave wave;
        return findWaveFor(next.f, wave, o.rules.clearCount);
    };

    Duel own = initial;
    if (!mine.midAir && !toPairOf(own, me, limit)) return -1;
    if (own.player[me].incoming.confirmed != garbage || own.player[me].incoming.pending) return -1;
    const unsigned firstPlaces = legal & places(own, mine, 0);
    if (preferred >= 0 && (!(firstPlaces >> preferred & 1) || clears(own.player[me].field, mine.pair[0], preferred))) return -1;
    Duel firing = own;
    const int original = immediate.id[0];
    if (!(firstPlaces >> original & 1) || !placeKnown(firing, mine, 0, original) || !toPairOf(firing, me, limit)) return -1;
    const auto& fired = firing.player[me];
    const int originalSent = firing.player[him].incoming.total() + garbage;
    const int originalPoints = fired.scoring.score - own.player[me].scoring.score;
    if (fired.chain.chains < 5 || fired.scoring.allClear || fired.fallen || fired.incoming.total() || originalSent <= garbage) return -1;
    const auto mainSized = [&](int points) {
        return int64_t(points) * 100 >= int64_t(originalPoints) * p.x3ChokeKeep;
    };

    // A second pair may appear before our own garbage animation has finished.
    // Read both observed opponent pairs, including consecutive attacks.
    Duel opponent = initial;
    if (!theirs.midAir && !toPairOf(opponent, him, limit)) return -1;
    if (opponent.player[him].scoring.allClear || opponent.player[me].incoming.total() != garbage) return -1;
    int strongest = 0;
    int64_t firstConfirmed = limit;
    const auto threat = [&](const Duel& attack, int alreadySent) {
        if (attack.player[him].scoring.allClear) return false;
        const int sent = attack.player[me].incoming.total() - garbage;
        if (sent > alreadySent) {
            strongest = std::max(strongest, sent);
            // No garbage is incoming to him, so there is only the next delay.
            firstConfirmed = std::min(firstConfirmed, attack.player[him].spawnFrame - o.timing.nextFrames);
        }
        return true;
    };
    for (unsigned starts = places(opponent, theirs, 0); starts; starts &= starts - 1) {
        if (stop()) return -1;
        const int id = std::countr_zero(starts);
        Duel first = opponent;
        if (!placeKnown(first, theirs, 0, id)) continue;
        if (!toPairOf(first, him, limit)) {
            if (first.player[him].phase == DuelPhase::Dead) continue;
            return -1;
        }
        if (!threat(first, 0)) return -1;
        const int firstSent = first.player[me].incoming.total() - garbage;
        for (unsigned replies = places(first, theirs, 1); replies; replies &= replies - 1) {
            if (stop()) return -1;
            const int reply = std::countr_zero(replies);
            if (!clears(first.player[him].field, theirs.pair[1], reply)) continue;
            Duel attack = first;
            if (!placeKnown(attack, theirs, 1, reply)) continue;
            if (!toPairOf(attack, him, limit)) {
                if (attack.player[him].phase == DuelPhase::Dead) continue;
                return -1;
            }
            if (!threat(attack, firstSent)) return -1;
        }
    }
    if (strongest > originalSent) return -1;

    const auto mainReady = [&](const Duel& afterSide) {
        if (mine.knownCount < 3) return false;
        const auto& before = afterSide.player[me];
        for (unsigned starts = places(afterSide, mine, 2); starts; starts &= starts - 1) {
            if (stop()) return false;
            const int id = std::countr_zero(starts);
            if (!clears(before.field, mine.pair[2], id)) continue;
            Duel main = afterSide;
            if (!placeKnown(main, mine, 2, id) || !toPairOf(main, me, limit)) continue;
            const auto& after = main.player[me];
            if (!after.scoring.allClear && mainSized(after.scoring.score - before.scoring.score)) return true;
        }
        return false;
    };
    // Keep a quiet ordinary answer that already works for every possible drop.
    for (unsigned starts = firstPlaces; starts;) {
        if (stop()) return -1;
        const int id = preferred >= 0 && (starts >> preferred & 1) ? preferred : std::countr_zero(starts);
        starts &= ~(1u << id);
        if (clears(own.player[me].field, mine.pair[0], id)) continue;
        Duel placed = own;
        if (!placeKnown(placed, mine, 0, id) || placed.player[me].at >= firstConfirmed || placed.player[me].field.dead()) continue;

        bool safe = true;
        for (unsigned mask = 1; mask <= columns && safe; ++mask) {
            if (std::popcount(mask) != garbage) continue;
            if (stop()) return -1;
            Duel drop = placed;
            if (garbage < W && !o.nextGarbageOrder[me]) drop.nextGarbageOrder[me] = smallDropOrder(mask);
            safe = false;
            if (!toPairOf(drop, me, limit)) break;
            const auto& next = drop.player[me];
            if (next.fallen != garbage || next.incoming.total()) break;
            for (unsigned fires = places(drop, mine, 1); fires; fires &= fires - 1) {
                if (stop()) return -1;
                const int fire = std::countr_zero(fires);
                if (!clears(next.field, mine.pair[1], fire)) continue;
                Duel answer = drop;
                if (!placeKnown(answer, mine, 1, fire) || answer.player[me].at >= firstConfirmed || !toPairOf(answer, me, limit)) continue;
                const auto& after = answer.player[me];
                const int sent = answer.player[him].incoming.total();
                const int points = after.scoring.score - next.scoring.score;
                const bool growsMain = sent >= originalSent + W && mainSized(points);
                const bool side = after.chain.chains <= 4 && sent >= garbage;
                if (after.fallen == garbage && !after.incoming.total() && !after.scoring.allClear && sent >= strongest &&
                    (growsMain || (side && mainReady(answer)))) {
                    safe = true;
                    break;
                }
            }
            if (o.nextGarbageOrder[me]) break;
        }
        if (safe && !stop()) return id;
    }
    return -1;
}

} // namespace juraic::ai
