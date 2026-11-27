#pragma once
#include "decision_metrics.h"

#include "exchange.h"
#include <limits>

namespace juraic::ai::lv38_threat {

// For each firing depth, keep the fastest pressure and the largest pressure.
// The latter can bury a key even when the quicker attack only clears a row.
inline std::array<const Plan*, 4> threats(const PlanSet& rival) {
    std::array<const Plan*, 4> result{};
    for (const auto* list : {&rival.plans, &rival.tactical, &rival.keeping}) {
        for (const Plan& fire : *list) {
            if (fire.count < 1 || fire.count > 2 || fire.count > rival.knownCount || fire.count != fire.fireDepth ||
                fire.chains < 1 || fire.chains > 4 || fire.sent < 2 * W || fire.allClear) continue;
            const int i = 2 * (fire.count - 1);
            const Plan*& quick = result[i];
            const Plan*& strong = result[i + 1];
            if (!quick || fire.endFrame < quick->endFrame ||
                (fire.endFrame == quick->endFrame && fire.sent > quick->sent)) quick = &fire;
            if (!strong || fire.sent > strong->sent ||
                (fire.sent == strong->sent && fire.endFrame < strong->endFrame)) strong = &fire;
        }
    }
    for (int i : {0, 2})
        if (result[i] && samePlacements(*result[i], *result[i + 1])) result[i + 1] = nullptr;
    return result;
}

struct Outcome {
    int reserve = 0;
    int enemyReserve = 0;
    int burden = 0;
    int returned = 0;
    bool reachable = false;

    int64_t margin(int rate) const {
        return int64_t(reserve) - enemyReserve + int64_t(rate) * (returned - burden);
    }
    bool holds(int main, int rate) const {
        const bool keepsMain = int64_t(reserve) * 100 >= int64_t(main) * 83;
        const bool outfires = returned >= 5 * W && int64_t(returned - 4 * W) * rate > enemyReserve;
        return reachable && burden <= W && (keepsMain || outfires);
    }
    bool crushed(int main, int rate) const {
        return !reachable || burden > W ||
            (int64_t(reserve) * 100 < int64_t(main) * 75 && !holds(main, rate));
    }
};

// A second pair of ours that appears at `answerSpawn` reacts to the rival's chain only when the choice of it
// (seenBeforeChoice: `ahead`, `seeLate`) sees the frame his last pair is checked in, both as read (`check`) and as he
// goes the straight way (`seen`, rivalSeenCheck). A rival the reading has dead with no such frame (`check` -1) needs no
// reaction.
inline bool reactsInTime(int64_t answerSpawn, int64_t check, int64_t seen, int ahead = 1, int seeLate = 0) {
    return check < 0 || (seenBeforeChoice(check, answerSpawn, ahead, seeLate) && seen >= 0 &&
                         seenBeforeChoice(seen, answerSpawn, ahead, seeLate));
}

// Read the ordinary move first and keep it if the visible threats can be
// answered. Compare a small number of nearby roots only after a loss is found.
template<class Stop>
int prefer(const Observation& o, const PlanSet& mine, const PlanSet& theirs, const BuildResult& build,
           int ordinary, unsigned choices, const Params& p, uint64_t seed, Stop stop, int ahead = 1) {
    metrics::Scope scope(metrics::Part::Threat);
    const int me = o.me, him = 1 - me;
    if (!p.lv38Midgame || o.players != 2 || p.handWait || build.cut || ordinary < 0 || !(choices >> ordinary & 1) ||
        !mine.knownCount || !theirs.knownCount || o.busyLeft[me] || o.self().incoming.total() ||
        o.opponent().incoming.total() || o.self().scoring.allClear || o.opponent().scoring.allClear ||
        chainRunning(o, him) || stop()) return ordinary;
    const auto attacks = threats(theirs);
    if (std::none_of(attacks.begin(), attacks.end(), [](const Plan* fire) { return fire != nullptr; })) return ordinary;

    int main = 0;
    readMainChain(o.self().field, o.rules, o.timing, p, &main);
    if (main < 6000 || o.self().field.f.count() + 2 * W < 52 || stop()) return ordinary;
    const int rate = o.rules.rateAt(o.frame);
    KnownReplay replay{stop};
    struct Root {
        Plan path[6]{};
        int count = 1;
        bool valid = false;
    };
    const auto root = [&](int id) {
        Root moves;
        Plan& first = moves.path[0];
        first.count = 1;
        first.id[0] = int8_t(id);
        GameField field = o.self().field;
        place(field, placement(id), mine.pair[0]);
        if (field.dead() || anyClear(field.f, o.rules)) return moves;
        moves.valid = true;
        if (mine.knownCount < 2) return moves;

        // One quiet continuation tests whether the garbage really lands. Up
        // to four additional continuations cover offsets and a winning main.
        std::array<const Plan*, 5> follow{};
        for (const auto* list : {&mine.plans, &mine.tactical, &mine.keeping}) {
            for (const Plan& plan : *list) {
                if (plan.count != 2 || plan.id[0] != id || plan.allClear) continue;
                if (!plan.fireDepth) {
                    if (!follow[0] || plan.mainAfter > follow[0]->mainAfter) follow[0] = &plan;
                    continue;
                }
                if (plan.fireDepth != 2 || plan.sent <= 0) continue;
                const int i = plan.chains > 4 ? 4 : 1;
                if (!follow[i] || plan.sent > follow[i]->sent) follow[i] = &plan;
                if (i == 4) continue;
                if (!follow[2] || plan.mainAfter > follow[2]->mainAfter) follow[2] = &plan;
                if (!follow[3] || plan.endFrame < follow[3]->endFrame) follow[3] = &plan;
            }
        }
        if (!follow[0]) {
            int reserve = -1;
            for (unsigned open = knownPlacements(field, mine, 1, p); open; open &= open - 1) {
                if (stop()) return Root{};
                const int next = std::countr_zero(open);
                GameField quiet = field;
                place(quiet, placement(next), mine.pair[1]);
                if (quiet.dead() || anyClear(quiet.f, o.rules)) continue;
                int points = 0;
                readMainChain(quiet, o.rules, o.timing, p, &points);
                if (points <= reserve) continue;
                reserve = points;
                moves.path[1] = first;
                moves.path[1].count = 2;
                moves.path[1].id[1] = int8_t(next);
            }
            if (reserve >= 0) ++moves.count;
        }
        for (const Plan* next : follow) {
            if (!next) continue;
            Plan* const end = moves.path + moves.count;
            if (std::none_of(moves.path, end, [&](const Plan& path) { return samePlacements(path, *next); }))
                moves.path[moves.count++] = *next;
        }
        return moves;
    };
    const auto exchange = [&](const Root& our, const Plan& attack, uint64_t lane) {
        Outcome best;
        for (int i = 0; i < our.count; ++i) {
            if (stop()) break;
            const Plan& answer = our.path[i];
            int64_t check = -1, answerSpawn = -1;
            int ownPlacements = 0;
            replay.acceptPlacement = [&](const Duel&, int who, Placement, Pair, int64_t from, int64_t) {
                if (who == me && ++ownPlacements == 2) answerSpawn = from;
                return true;
            };
            const Duel d = playOut(o, mine, &answer, theirs, &attack, p, lane, true, &check, &replay);
            replay.acceptPlacement = {};
            const DuelPlayer& self = d.player[me];
            const DuelPlayer& enemy = d.player[him];
            if (stop() || self.phase != DuelPhase::Control || self.moves != o.self().moves + answer.count ||
                self.scoring.allClear || self.field.dead()) continue;
            if (enemy.phase != DuelPhase::Dead && (enemy.moves != o.opponent().moves + attack.count ||
                enemy.phase != DuelPhase::Control || enemy.chain.chains != attack.chains || check < 0)) continue;
            // Do not assume the next decision can react to a chain that has
            // not yet shown its first wave.
            if (answer.fireDepth == 2) {
                if (!seenBeforeChoice(check, answerSpawn, ahead, p.seeLate) || !self.chain.chains) continue;
                const int64_t seen = check < 0 ? check : rivalSeenCheck(o, mine, &answer, theirs, &attack, p, lane, check, stop);
                if (!reactsInTime(answerSpawn, check, seen, ahead, p.seeLate)) continue;
            }
            const int burden = self.fallen + self.incoming.total();
            if (answer.count == 1 && mine.knownCount >= 2 && self.incoming.total()) continue;
            Outcome now;
            now.reachable = true;
            now.burden = burden;
            now.returned = enemy.fallen + enemy.incoming.total();
            readMainChain(self.field, o.rules, o.timing, p, &now.reserve);
            readMainChain(enemy.field, o.rules, o.timing, p, &now.enemyReserve);
            if (!best.reachable || (now.holds(main, rate) && !best.holds(main, rate)) ||
                (now.holds(main, rate) == best.holds(main, rate) && now.margin(rate) > best.margin(rate))) best = now;
        }
        return best;
    };

    const Root baseline = root(ordinary);
    if (!baseline.valid || stop()) return ordinary;
    std::array<Outcome, 4> current;
    unsigned verified = 0;
    bool exposed = false;
    for (size_t i = 0; i < attacks.size(); ++i) {
        if (!attacks[i]) continue;
        // Confirm the opponent's geometric plan independently of our move.
        const Duel alone = playOut(o, mine, nullptr, theirs, attacks[i], p, seed, true, nullptr, &replay);
        if (stop()) return ordinary;
        const auto& enemy = alone.player[him];
        if (enemy.moves != o.opponent().moves + attacks[i]->count || enemy.chain.chains != attacks[i]->chains ||
            alone.player[me].incoming.total() < 2 * W) continue;
        verified |= 1u << i;
        current[i] = exchange(baseline, *attacks[i], seed);
        exposed |= current[i].crushed(main, rate);
    }
    if (!exposed || stop()) return ordinary;

    int chosen = ordinary;
    int64_t bestMargin = std::numeric_limits<int64_t>::min();
    unsigned nearby = choices & build.alive & ~(1u << ordinary);
    for (int tried = 0; tried < 5 && nearby && !stop(); ++tried) {
        int id = std::countr_zero(nearby);
        for (unsigned open = nearby; open; open &= open - 1) {
            const int other = std::countr_zero(open);
            if (build.sum[other] > build.sum[id]) id = other;
        }
        nearby &= ~(1u << id);
        if (build.sum[id] * 100 < build.sum[ordinary] * 94) break;
        const Root alternative = root(id);
        if (!alternative.valid) continue;
        bool holds = true;
        int64_t worst = std::numeric_limits<int64_t>::max();
        for (size_t i = 0; i < attacks.size() && holds; ++i) {
            if (!(verified >> i & 1)) continue;
            const Outcome saved = exchange(alternative, *attacks[i], seed);
            holds = saved.holds(main, rate) &&
                (!current[i].reachable || saved.margin(rate) >= current[i].margin(rate) - int64_t(W) * rate);
            // Incomplete rows use an unknown column order. Recheck the bad
            // exchanges with a different order before changing the build.
            if (holds && current[i].crushed(main, rate)) {
                const Outcome again = exchange(alternative, *attacks[i], seed ^ 0xb4cd098cab1fd785ULL);
                holds = again.holds(main, rate);
            }
            worst = std::min(worst, saved.margin(rate));
        }
        if (holds && worst > bestMargin) chosen = id, bestMargin = worst;
    }
    return stop() ? ordinary : chosen;
}

} // namespace juraic::ai::lv38_threat
