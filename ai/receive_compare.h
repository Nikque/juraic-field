#pragma once
#include "known_poke.h"
#include "receive.h"

namespace juraic::ai::receive_compare {

inline Plan placing(int a, int b = -1, int c = -1) {
    Plan line;
    line.count = 1;
    line.id[0] = int8_t(a);
    if (b >= 0) line.id[line.count++] = int8_t(b);
    if (c >= 0) line.id[line.count++] = int8_t(c);
    return line;
}

struct Exchange {
    int pressure = 0, ownReserve = 0, rivalReserve = INT32_MAX;
    int64_t confirm = INT64_MAX, ready = INT64_MAX;
};

// For a small incoming attack, compare immediate and delayed main chains only
// when the usual placement has a proved small answer and a real counter to it.
// Both alternatives spend the same three observed pairs.
template<class Stop>
int prefer(const Observation& o, const PlanSet& mine, const PlanSet& theirs, int ordinary, unsigned legal,
           int arriving, const Params& p, uint64_t seed, Stop outOfTime) {
    const int me = o.me, him = 1 - me;
    if (!p.readMidgame || !p.checkKnownExchange || p.handWait || o.self().phase != Phase::Next ||
        o.busyLeft[me] || !chainShown(o.opponent()) || o.opponent().chain.chains > 4 || mine.midAir ||
        theirs.midAir || mine.knownCount != 3 || theirs.knownCount != 2 || arriving <= 0 || arriving >= W ||
        o.self().incoming.confirmed || o.self().incoming.pending != arriving || o.opponent().incoming.total() ||
        o.self().scoring.allClear || o.opponent().scoring.allClear || ordinary < 0 || !(legal >> ordinary & 1u) ||
        !(quietSetupChoices(o, mine, 1u << ordinary) & (1u << ordinary))) return -1;
    bool expired = false;
    const auto stop = [&] { return expired = expired || outOfTime(); };
    if (stop()) return -1;
    KnownReplay bounded{stop};
    Observation view = o;
    const auto replay = [&](const Plan* own, const Plan* rival, unsigned mask = 0, int64_t* check = nullptr) {
        if (mask) view.nextGarbageOrder[me] = smallDropOrder(mask);
        else view.nextGarbageOrder[me] = o.nextGarbageOrder[me];
        return playOut(view, mine, own, theirs, rival, p, seed, true, check, &bounded);
    };
    const auto finished = [&](const Duel& d, int own, int foe) {
        const auto& a = d.player[me];
        const auto& b = d.player[him];
        return a.moves == o.self().moves + own && b.moves == o.opponent().moves + foe &&
            a.phase == DuelPhase::Control && !a.field.dead() && !a.scoring.allClear && !b.scoring.allClear &&
            (b.phase == DuelPhase::Control || b.phase == DuelPhase::Dead);
    };
    const auto pressure = [&](const Duel& d) { return d.player[him].fallen + d.player[him].incoming.total(); };
    const auto clear = [&](const Duel& d) { return !d.player[me].incoming.total(); };
    const auto confirm = [&](const Duel& d) { return d.player[me].spawnFrame - o.timing.nextFrames; };
    Params joined = p;
    joined.evKeyed = 0;
    joined.trialMaxAdd = 1;
    std::vector<std::pair<GameField, int>> reserves;
    const auto reserve = [&](const DuelPlayer& a) {
        if (a.phase == DuelPhase::Dead) return 0;
        for (const auto& [field, points] : reserves) if (field == a.field) return points;
        int points = 0;
        buildValue(a.field, o.rules, o.timing, joined, &points);
        reserves.emplace_back(a.field, points);
        return points;
    };
    const auto layouts = [&](int fall, const auto& visit) {
        if (!fall || fall % W == 0 || o.nextGarbageOrder[me]) return visit(0);
        for (unsigned mask = 1; mask < 1u << W; ++mask)
            if (std::popcount(mask) == fall % W && (stop() || !visit(mask))) return false;
        return !stop();
    };

    std::vector<const Plan*> direct;
    for (const auto* list : {&mine.plans, &mine.tactical})
        for (const Plan& line : *list) if (line.count == 1 && line.fireDepth == 1 && line.chains >= 5 && !line.allClear &&
            (legal >> line.id[0] & 1u)) known_poke::add(direct, line);
    if (direct.empty()) return -1;
    std::stable_sort(direct.begin(), direct.end(), [](const Plan* a, const Plan* b) { return a->sent > b->sent; });
    const auto first = placing(ordinary);
    const auto held = replay(&first, nullptr);
    if (!finished(held, 1, 0) || held.player[me].fallen || held.player[me].chain.chains) return -1;
    Plan side;
    int64_t sideReady = INT64_MAX;
    for (unsigned choices = legalPlacements(held.player[me].field); choices; choices &= choices - 1) {
        if (stop()) return -1;
        const auto path = placing(ordinary, std::countr_zero(choices));
        const auto d = replay(&path, nullptr);
        const auto& a = d.player[me];
        if (finished(d, 2, 0) && clear(d) && !a.fallen && !pressure(d) && a.chain.chains && a.chain.chains <= 4 &&
            a.spawnFrame < sideReady) side = path, sideReady = a.spawnFrame;
    }
    if (!side.count) return -1;

    const auto idle = replay(nullptr, nullptr);
    std::vector<Plan> replies(1);
    Plan threat;
    int worstDebt = 0;
    for (unsigned starts = legalPlacements(idle.player[him].field); starts; starts &= starts - 1) {
        if (stop()) return -1;
        const auto enemy = placing(std::countr_zero(starts));
        const auto d = replay(nullptr, &enemy);
        const auto& b = d.player[him];
        if (b.scoring.allClear) return -1;
        if (b.moves != o.opponent().moves + 1 || b.phase != DuelPhase::Control) continue;
        replies.push_back(enemy);
        if (b.chain.chains >= 5) {
            int64_t check;
            const auto against = replay(&side, &enemy, 0, &check);
            const auto& a = against.player[me];
            int height[W];
            a.field.heights(height);
            const int debt = a.incoming.total();
            if (a.moves == o.self().moves + 2 && check > a.lockFrame && check < sideReady &&
                height[DEATH_X] + std::min(debt, o.rules.maxGarbagePerDrop) / W > DEATH_Y &&
                debt > worstDebt) threat = enemy, worstDebt = debt;
        }
        for (unsigned seconds = legalPlacements(b.field); seconds; seconds &= seconds - 1) {
            if (stop()) return -1;
            const auto both = placing(enemy.id[0], std::countr_zero(seconds));
            const auto steps = replay(nullptr, &both);
            if (steps.player[him].scoring.allClear) return -1;
            if (steps.player[him].moves == o.opponent().moves + 2 && steps.player[him].phase == DuelPhase::Control)
                replies.push_back(both);
        }
    }
    if (!threat.count) return -1;

    // Find the delayed three-pair answers to the concrete threat before
    // comparing all of them against the rival's observed alternatives.
    std::vector<Plan> recoveries;
    const auto beforeSide = replay(&first, &threat);
    if (!finished(beforeSide, 1, 1) || beforeSide.player[me].fallen || clear(beforeSide)) return -1;
    for (unsigned seconds = legalPlacements(beforeSide.player[me].field); seconds; seconds &= seconds - 1) {
        if (stop()) return -1;
        const auto two = placing(ordinary, std::countr_zero(seconds));
        const auto prefix = replay(&two, &threat);
        if (!finished(prefix, 2, 1) || prefix.player[me].fallen > arriving) continue;
        if (clear(prefix)) return -1;
        if (!layouts(prefix.player[me].fallen, [&](unsigned mask) {
            const auto d = mask ? replay(&two, &threat, mask) : prefix;
            if (!finished(d, 2, 1)) return true;
            for (unsigned thirds = legalPlacements(d.player[me].field); thirds; thirds &= thirds - 1) {
                if (stop()) return false;
                const auto three = placing(ordinary, two.id[1], std::countr_zero(thirds));
                const auto end = replay(&three, &threat, mask);
                if (!finished(end, 3, 1) || !clear(end) || end.player[me].fallen > arriving) continue;
                if (end.player[me].chain.chains < 5 || end.player[me].dropped) return false;
                if (std::none_of(recoveries.begin(), recoveries.end(), [&](const Plan& other) { return samePlacements(three, other); }))
                    recoveries.push_back(three);
            }
            return true;
        })) return -1;
    }
    if (recoveries.empty()) return -1;

    int require = 0;
    std::vector<Exchange> baselines(replies.size());
    for (size_t i = 0; i < replies.size(); ++i) {
        const auto& reply = replies[i];
        const Plan* enemy = reply.count ? &reply : nullptr;
        auto& baseline = baselines[i];
        for (const Plan& recover : recoveries) {
            if (stop()) return -1;
            const auto two = placing(recover.id[0], recover.id[1]);
            const auto start = replay(&two, enemy);
            if (!finished(start, 2, reply.count) || start.player[me].fallen > arriving) continue;
            if (!layouts(start.player[me].fallen, [&](unsigned mask) {
                const auto d = replay(&recover, enemy, mask);
                const auto& a = d.player[me];
                if (stop()) return false;
                if (!finished(d, 3, reply.count) || !clear(d) ||
                    a.chain.chains < 5 || a.dropped || a.fallen > arriving) return true;
                if (d.player[him].fallen % W && !o.nextGarbageOrder[him]) return false;
                baseline.pressure = std::max(baseline.pressure, pressure(d));
                baseline.rivalReserve = std::min(baseline.rivalReserve, reserve(d.player[him]));
                baseline.ownReserve = std::max(baseline.ownReserve, reserve(a));
                baseline.confirm = std::min(baseline.confirm, confirm(d));
                baseline.ready = std::min(baseline.ready, a.spawnFrame);
                return true;
            })) return -1;
        }
        if (baseline.confirm == INT64_MAX) return -1;
        require = std::max(require, baseline.ownReserve);
    }

    const Plan* fire = direct.front();
    const auto alone = replay(fire, nullptr);
    if (!finished(alone, 1, 0) || !clear(alone) || alone.player[me].fallen) return -1;
    Plan earlier;
    int bestReserve = require - 1;
    int64_t earlyReady = INT64_MAX;
    // Rank continuations without new rival placements. Verify one fixed line
    // afterwards so the chosen inputs never depend on an unseen reply.
    for (unsigned seconds = legalPlacements(alone.player[me].field); seconds; seconds &= seconds - 1) {
        if (stop()) return -1;
        const auto two = placing(fire->id[0], std::countr_zero(seconds));
        const auto prefix = replay(&two, nullptr);
        if (!finished(prefix, 2, 0) || !clear(prefix) || prefix.player[me].fallen) continue;
        for (unsigned thirds = legalPlacements(prefix.player[me].field); thirds; thirds &= thirds - 1) {
            if (stop()) return -1;
            const auto three = placing(fire->id[0], two.id[1], std::countr_zero(thirds));
            const auto d = replay(&three, nullptr);
            const auto& a = d.player[me];
            if (!finished(d, 3, 0) || !clear(d) || a.fallen || pressure(d) < pressure(alone)) continue;
            const int kept = reserve(a);
            if (kept > bestReserve || (kept == bestReserve && kept >= require && a.spawnFrame < earlyReady))
                bestReserve = kept, earlyReady = a.spawnFrame, earlier = three;
        }
    }
    if (!earlier.count) return -1;
    for (size_t i = 0; i < replies.size(); ++i) {
        if (stop()) return -1;
        const auto& reply = replies[i];
        const auto enemy = reply.count ? &reply : nullptr;
        const auto firstExchange = replay(fire, enemy);
        if (!finished(firstExchange, 1, reply.count) || !clear(firstExchange) || firstExchange.player[me].fallen ||
            firstExchange.player[me].chain.chains != fire->chains ||
            confirm(firstExchange) >= baselines[i].confirm || pressure(firstExchange) <= baselines[i].pressure) return -1;
        const auto d = replay(&earlier, enemy);
        const auto& a = d.player[me];
        const auto& b = d.player[him];
        if (!finished(d, 3, reply.count) || !clear(d) || a.fallen || b.fallen % W ||
            pressure(d) < pressure(firstExchange) || a.spawnFrame > baselines[i].ready ||
            reserve(a) < baselines[i].ownReserve || reserve(b) > baselines[i].rivalReserve) return -1;
    }
    return stop() ? -1 : fire->id[0];
}

} // namespace juraic::ai::receive_compare
