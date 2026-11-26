#pragma once

#include "exchange.h"
#include <array>
#include <optional>
#include <tuple>

namespace juraic::ai::lv38_two_wave {

constexpr int SCENARIOS = 5;
constexpr int LANES = 3;

struct RouteNote {
    std::array<int8_t, 3> id{};
    int count = 0;
    // 0 not reached, 1 couldn't replay, 2 replayed but rejected,
    // 3 passed comparison but not selected, 4 selected, 5 ordinary route.
    int result = 0;
    int spendAt = -1, waves = 0, lostMain = 0;
    int64_t realized = 0;
};

struct Note {
    // 0 outside, 1 no visible second attack, 2 unchanged, 3 changed,
    // 4 incomplete, 5 no executable baseline.
    int status = 0, ordinary = -1, selected = -1, scenarios = 0, confirmations = 0;
    Plan route;
    int rivalKnown = 0, rivalPlanned = 0;
    int mainSpendRejected = 0;
    int beforeMainReview = -1;
    bool mainReview = false, mainReviewIncomplete = false;
    std::vector<RouteNote> considered;
    std::array<int, SCENARIOS> ordinaryCost{}, selectedCost{};
    std::array<int, SCENARIOS> ordinaryPending{}, selectedPending{}, ordinaryConfirmed{}, selectedConfirmed{};
    std::array<int, SCENARIOS> ordinaryFallen{}, selectedFallen{};
    std::array<int, SCENARIOS> ordinarySent{}, selectedSent{};
    std::array<int, SCENARIOS> ordinaryMain{}, selectedMain{};
    std::array<int64_t, SCENARIOS> ordinaryReady{}, selectedReady{}, rivalReady{};
    std::array<Plan, SCENARIOS> enemyRoute{};
    std::array<int, SCENARIOS> ordinaryWaves{}, selectedWaves{}, ordinaryOffset{}, selectedOffset{};
    std::array<int, SCENARIOS> ordinaryEnemyFall{}, selectedEnemyFall{}, ordinaryEnemyOffset{}, selectedEnemyOffset{};
    std::array<int64_t, SCENARIOS> ordinaryFire{}, selectedFire{}, ordinaryConfirm{}, selectedConfirm{};
    std::array<int64_t, SCENARIOS> enemyFire{}, enemyConfirm{}, ordinaryEnemyReady{};
    std::array<int64_t, SCENARIOS> ordinaryEnemyFire{}, ordinaryEnemyConfirm{};
    std::array<int, 3> visibleMain{};
};

struct Endpoint {
    int pending = 0, confirmed = 0, fallen = 0, sent = 0, main = 0;
    int64_t ready = -1, rivalReady = -1;
    bool dead = false, wins = false;
    int firstWaves = 0, maxWaves = 0, offset = 0, rivalOffset = 0, rivalFall = 0;
    int rivalPressure = 0;
    int64_t fire = -1, confirm = -1, rivalFire = -1, rivalConfirm = -1;
    int burden() const { return pending + confirmed + fallen; }
};

inline int boardMain(const Observation& o, const GameField& field) {
    if (field.dead()) return 0;
    Trials trials;
    detect(field, o.rules, o.timing, 2, trials, nullptr, false);
    int points = 0;
    for (int k = 0; k < trials.count; ++k)
        if (trials.item[k].chains >= o.rules.chainRequirement) points = std::max(points, trials.item[k].points);
    return points;
}

inline bool validPlan(const Plan& plan, const PlanSet& set, int limit) {
    if (plan.count < 1 || plan.count > std::min(limit, set.knownCount)) return false;
    for (int i = 0; i < plan.count; ++i)
        if (plan.id[i] < 0 || plan.id[i] >= PLACEMENTS) return false;
    return true;
}

inline std::optional<Endpoint> read(const Observation& o, const PlanSet& mine, const Plan& path,
                                    const PlanSet& theirs, const Plan* reply, const Params& p,
                                    uint64_t seed, KnownReplay& replay, bool audit = false) {
    const int me = o.me, him = 1 - me;
    DuelExchangeTrace trace;
    trace.initialMoves[me] = o.self().moves;
    trace.initialMoves[him] = o.opponent().moves;
    replay.trace = p.lv38TwoWaveMainCover || audit ? &trace : nullptr;
    const auto d = playOut(o, mine, &path, theirs, reply, p, seed, true, nullptr, &replay);
    replay.trace = nullptr;
    if (replay.stop()) return {};
    const auto& us = d.player[me];
    const auto& rival = d.player[him];
    const bool won = d.state == (me == 0 ? RoundState::Won0 : RoundState::Won1);
    const bool lost = d.state != RoundState::Playing && !won;
    if (us.moves != o.self().moves + path.count ||
        (reply && !won && (rival.moves != o.opponent().moves + reply->count ||
                           rival.chain.chains != reply->chains))) return {};
    if (!lost && !won && (us.phase != DuelPhase::Control || rival.phase != DuelPhase::Control)) return {};
    if (!lost && path.fireDepth == path.count && path.chains && us.chain.chains != path.chains &&
        (!p.lv38TwoWaveMainCover || path.count != 1)) return {};
    const int above = aboveOther(o, me, p);
    Endpoint result{us.incoming.pending, us.incoming.confirmed, us.fallen,
        std::max(0, rival.incoming.total() + rival.fallen - above),
        boardMain(o, us.field), us.spawnFrame, rival.spawnFrame, lost, won};
    if (p.lv38TwoWaveMainCover || audit) {
        result.firstWaves = trace.turns[me][1].waves;
        result.rivalFall = rival.fallen;
        result.rivalPressure = std::max(0, rival.incoming.confirmed + rival.fallen - above);
        for (const auto& fire : trace.turns[me]) {
            result.offset += fire.offset;
            if (fire.waves <= result.maxWaves) continue;
            result.maxWaves = fire.waves;
            result.fire = fire.ignition;
            result.confirm = fire.confirmed;
        }
        for (const auto& fire : trace.turns[him]) result.rivalOffset += fire.offset;
        const auto& enemy = trace.turns[him][reply ? reply->count : 0];
        result.rivalFire = enemy.ignition;
        result.rivalConfirm = enemy.confirmed;
    }
    return result;
}

inline int64_t value(const Endpoint& r, int rate) {
    if (r.wins) return 100000000;
    if (r.dead) return -100000000;
    // Both fallen and outstanding garbage are a cost. A route cannot earn a
    // reduction by simply keeping a future pair in Control.
    const int tempo = int(std::clamp(r.rivalReady - r.ready, int64_t(-180), int64_t(180)));
    return int64_t(rate) * (2 * std::min(r.sent, 48) - 3 * r.burden() - r.fallen - r.confirmed)
        + r.main / 12 + int64_t(tempo) * rate / 8;
}

struct Reading {
    Plan path;
    std::array<Endpoint, SCENARIOS> result{};
    int64_t points = 0;
    size_t trace = 0;
};

// Only one route is used across all rival scenarios and all tested drop orders.
// For each initial placement keep the early and the strongest fires
// independently, plus quiet building and a small clear.
inline std::vector<Plan> routes(const PlanSet& mine, unsigned legal, const Plan* retained, bool mainCover = false) {
    std::array<const Plan*, PLACEMENTS> fast{}, strong{}, reserve{}, smallClear{}, firstFire{};
    for (const auto* list : {&mine.plans, &mine.keeping, &mine.tactical}) {
        for (const auto& path : *list) {
            if (!validPlan(path, mine, 3) || !(legal >> path.id[0] & 1u)) continue;
            const int id = path.id[0];
            if (path.count == 1) {
                if (mainCover && path.fireDepth == 1) firstFire[id] = &path;
                continue;
            }
            if (!path.fireDepth && (!reserve[id] || path.mainAfter > reserve[id]->mainAfter))
                reserve[id] = &path;
            if (path.fireDepth != path.count || path.chains > 4) continue;
            if (!fast[id] || path.lockFrame < fast[id]->lockFrame ||
                (path.lockFrame == fast[id]->lockFrame && path.points > fast[id]->points))
                fast[id] = &path;
            // Points include firepower spent offsetting the first attack.
            // Sent alone would undervalue a short chain that first cancels it.
            const auto* other = strong[id];
            if (!other || std::tie(path.points, path.sent, path.mainAfter) >
                              std::tie(other->points, other->sent, other->mainAfter) ||
                (std::tie(path.points, path.sent, path.mainAfter) ==
                     std::tie(other->points, other->sent, other->mainAfter) && path.lockFrame < other->lockFrame))
                strong[id] = &path;
            if (path.used <= 8 && (!smallClear[id] || path.fallen < smallClear[id]->fallen ||
                (path.fallen == smallClear[id]->fallen && path.mainAfter > smallClear[id]->mainAfter)))
                smallClear[id] = &path;
        }
    }
    std::vector<Plan> out;
    if (retained && validPlan(*retained, mine, 3) && (legal >> retained->id[0] & 1u)) out.push_back(*retained);
    for (unsigned bits = legal; bits; bits &= bits - 1) {
        const int id = std::countr_zero(bits);
        Plan first;
        first.id[0] = int8_t(id); first.count = 1;
        if (firstFire[id]) first = *firstFire[id];
        for (const Plan* path : {static_cast<const Plan*>(&first), fast[id], strong[id], reserve[id], smallClear[id]})
            if (path && std::none_of(out.begin(), out.end(), [&](const Plan& done) {
                    return samePlacements(done, *path);
                })) out.push_back(*path);
    }
    return out;
}

inline bool spendsMain(const Endpoint& a, const Endpoint& b, int saved, int rate) {
    if (a.dead || b.wins || a.maxWaves >= 5 || b.maxWaves <= 4) return false;
    const int64_t realized = int64_t(b.rivalPressure - a.rivalPressure + saved) * rate;
    // An unconfirmed send is unavailable to finance a newly spent main.
    // A lethal ordinary line is judged by actual survival instead.
    return realized < int64_t(a.main) - b.main;
}

inline bool protects(const Reading& base, const Reading& option, int count, int rate, bool mainCover = false) {
    int better = 0, betterAgainstSecond = 0;
    for (int k = 0; k < count; ++k) {
        const auto& a = base.result[k];
        const auto& b = option.result[k];
        if ((b.dead && !a.dead) || (a.wins && !b.wins)) return false;
        const int saved = a.burden() - b.burden();
        if (saved < 0 || b.fallen > a.fallen + W - 1) return false;
        const int more = b.sent - a.sent;
        if (mainCover && spendsMain(a, b, saved, rate)) return false;
        // Already handled pressure cannot be replaced by a bigger speculative
        // attack unless this exact exchange sends enough to justify the spend.
        if (b.main + W * rate < a.main && saved < W && more < 2 * W && !b.wins) return false;
        const bool swing = (saved >= W || (saved >= 0 && more >= 2 * W) || (b.wins && !a.wins)) &&
            value(b, rate) > value(a, rate);
        better += swing;
        betterAgainstSecond += k > 0 && swing;
    }
    return better >= 2 && betterAgainstSecond;
}

template<class Stop>
int select(const Observation& o, const PlanSet& mine, const PlanSet& theirs, int ordinary,
           unsigned legal, const Params& p, uint64_t seed, Stop stop, Note& note, const Plan* retained, bool audit) {
    note = {};
    note.ordinary = note.selected = ordinary;
    note.rivalKnown = theirs.knownCount;
    note.rivalPlanned = theirs.pairCount;
    const bool ourChain = o.self().phase == Phase::Wave || o.busyChain[o.me];
    const bool theirChain = chainShown(o.opponent()) || o.busyChain[1 - o.me];
    if (o.players != 2 || ordinary < 0 || ordinary >= PLACEMENTS ||
        !(legal >> ordinary & 1u) || mine.knownCount < 1 || theirs.knownCount < 1 ||
        ourChain || (theirChain && o.opponent().chain.chains > 4) ||
        (!retained && !o.self().incoming.total() && !theirChain) || stop()) return ordinary;
    // Tactical paths are not an alternative to a confirmed attack that is
    // already of main-chain scale.
    if (o.self().incoming.total() > o.rules.maxGarbagePerDrop) return ordinary;
    KnownReplay replay{stop};
    replay.acceptPlacement = [](const Duel&, int, Placement, Pair, int64_t, int64_t lock) { return lock >= 0; };
    const int me = o.me, him = 1 - me;
    const auto initial = playOut(o, mine, nullptr, theirs, nullptr, p, seed, true, nullptr, &replay);
    if (stop()) { note.status = 4; return ordinary; }
    const int firstBurden = initial.player[me].fallen + initial.player[me].incoming.total();
    if (firstBurden > o.rules.maxGarbagePerDrop || (theirChain && initial.player[him].chain.chains > 4)) return ordinary;
    const Plan *quick = nullptr, *large = nullptr;
    std::array<const Plan*, 3> mainByDepth{};
    const Plan* earlyMain = nullptr;
    int64_t earliest = INT64_MAX;
    int quickSent = 0, largeSent = 0;
    std::vector<const Plan*> inspected;
    for (const auto* list : {&theirs.plans, &theirs.keeping, &theirs.tactical}) {
        for (const auto& path : *list) {
            if (stop()) { note.status = 4; return ordinary; }
            if (p.lv38TwoWaveMainCover && validPlan(path, theirs, 3) &&
                path.fireDepth == path.count && path.chains >= 5) {
                auto*& strongest = mainByDepth[path.count - 1];
                if (!strongest || path.points > strongest->points) strongest = &path;
                if (!earlyMain || path.endFrame < earlyMain->endFrame) earlyMain = &path;
                continue;
            }
            if (!validPlan(path, theirs, 2) || path.fireDepth != path.count || path.chains > 4 || path.allClear ||
                std::any_of(inspected.begin(), inspected.end(), [&](const Plan* other) {
                    return samePlacements(*other, path);
                })) continue;
            inspected.push_back(&path);
            int64_t ignition = -1;
            const auto d = playOut(o, mine, nullptr, theirs, &path, p, seed, true, &ignition, &replay);
            const auto& q = d.player[him];
            const auto& a = d.player[me];
            if (q.moves != o.opponent().moves + path.count || q.chain.chains != path.chains ||
                q.phase != DuelPhase::Control || ignition < o.frame) continue;
            const int extra = a.fallen + a.incoming.total() - firstBurden;
            if (extra <= 0 || extra > o.rules.maxGarbagePerDrop) continue;
            if (ignition < earliest || (ignition == earliest && extra > quickSent)) {
                earliest = ignition; quick = &path; quickSent = extra;
            }
            if (extra > largeSent) { largeSent = extra; large = &path; }
        }
    }
    const Plan *firstMain = nullptr, *largestMain = nullptr;
    int64_t firstMainFire = INT64_MAX;
    int maxMain = 0;
    for (const Plan* path : {mainByDepth[0], mainByDepth[1], mainByDepth[2], earlyMain}) {
        if (!path) continue;
        if (stop()) { note.status = 4; return ordinary; }
        int64_t ignition = -1;
        const auto d = playOut(o, mine, nullptr, theirs, path, p, seed, true, &ignition, &replay);
        const auto& q = d.player[him];
        if (q.moves != o.opponent().moves + path->count || q.chain.chains != path->chains ||
            q.phase != DuelPhase::Control || ignition < o.frame) continue;
        const int points = q.scoring.score - initial.player[him].scoring.score;
        note.visibleMain[path->count - 1] = std::max(note.visibleMain[path->count - 1], points);
        if (ignition < firstMainFire) firstMain = path, firstMainFire = ignition;
        if (points > maxMain) largestMain = path, maxMain = points;
    }
    if (!quick && !firstMain) { note.status = 1; return ordinary; }
    std::array<const Plan*, SCENARIOS> replies{};
    int count = 1;
    for (const Plan* reply : {quick, large, firstMain, largestMain}) {
        if (!reply || std::any_of(replies.begin() + 1, replies.begin() + count,
                                  [&](const Plan* prior) { return samePlacements(*prior, *reply); })) continue;
        replies[count] = reply;
        note.enemyRoute[count++] = *reply;
    }
    note.scenarios = count;
    const int rate = o.rules.rateAt(o.frame);
    const auto evaluate = [&](const Plan& path, uint64_t sampled) -> std::optional<Reading> {
        Reading r;
        r.path = path;
        for (int k = 0; k < count; ++k) {
            const auto end = read(o, mine, path, theirs, replies[k], p, sampled, replay, audit);
            if (!end) return {};
            r.result[k] = *end;
            r.points += value(*end, rate);
        }
        return r;
    };
    std::optional<Reading> base;
    std::vector<Reading> candidates;
    const auto options = routes(mine, legal, retained, p.lv38TwoWaveMainCover != 0);
    note.considered.reserve(options.size());
    for (const auto& path : options) {
        RouteNote trace;
        trace.count = path.count;
        std::copy_n(path.id, path.count, trace.id.begin());
        note.considered.push_back(trace);
    }
    for (size_t k = 0; k < options.size(); ++k) {
        if (stop()) { note.status = 4; return ordinary; }
        const auto& path = options[k];
        auto r = evaluate(path, seed);
        note.considered[k].result = r ? 2 : 1;
        if (!r) continue;
        r->trace = k;
        if (path.id[0] == ordinary) {
            if (!base || r->points > base->points) base = *r;
        } else candidates.push_back(*r);
    }
    if (stop()) { note.status = 4; return ordinary; }
    if (!base) { note.status = 5; return ordinary; }
    const auto save = [&](const Reading& r, bool selected) {
        for (int k = 0; k < count; ++k) {
            const auto& e = r.result[k];
            (selected ? note.selectedCost : note.ordinaryCost)[k] = e.burden();
            (selected ? note.selectedPending : note.ordinaryPending)[k] = e.pending;
            (selected ? note.selectedConfirmed : note.ordinaryConfirmed)[k] = e.confirmed;
            (selected ? note.selectedFallen : note.ordinaryFallen)[k] = e.fallen;
            (selected ? note.selectedSent : note.ordinarySent)[k] = e.sent;
            (selected ? note.selectedMain : note.ordinaryMain)[k] = e.main;
            (selected ? note.selectedReady : note.ordinaryReady)[k] = e.ready;
            (selected ? note.selectedWaves : note.ordinaryWaves)[k] = e.maxWaves;
            (selected ? note.selectedOffset : note.ordinaryOffset)[k] = e.offset;
            (selected ? note.selectedEnemyFall : note.ordinaryEnemyFall)[k] = e.rivalFall;
            (selected ? note.selectedEnemyOffset : note.ordinaryEnemyOffset)[k] = e.rivalOffset;
            (selected ? note.selectedFire : note.ordinaryFire)[k] = e.fire;
            (selected ? note.selectedConfirm : note.ordinaryConfirm)[k] = e.confirm;
            if (!selected) note.ordinaryEnemyReady[k] = e.rivalReady;
            (selected ? note.enemyFire : note.ordinaryEnemyFire)[k] = e.rivalFire;
            (selected ? note.enemyConfirm : note.ordinaryEnemyConfirm)[k] = e.rivalConfirm;
            if (selected) note.rivalReady[k] = e.rivalReady;
        }
    };
    save(*base, false);
    note.considered[base->trace].result = 5;
    note.status = 2;
    std::stable_sort(candidates.begin(), candidates.end(), [](const Reading& a, const Reading& b) {
        return a.points > b.points;
    });
    int checked = 0;
    for (const auto& option : candidates) {
        if (!protects(*base, option, count, rate, p.lv38TwoWaveMainCover != 0)) {
            for (int k = 0; p.lv38TwoWaveMainCover && k < count; ++k) {
                const auto& a = base->result[k];
                const auto& b = option.result[k];
                if (spendsMain(a, b, a.burden() - b.burden(), rate)) {
                    ++note.mainSpendRejected;
                    auto& rejected = note.considered[option.trace];
                    rejected.spendAt = k;
                    rejected.waves = b.maxWaves;
                    rejected.lostMain = a.main - b.main;
                    rejected.realized = int64_t(b.rivalPressure - a.rivalPressure + a.burden() - b.burden()) * rate;
                    break;
                }
            }
            continue;
        }
        note.considered[option.trace].result = 3;
        if (checked++ == 3) break;
        int confirmed = 1;
        for (uint64_t lane = 1; lane < LANES; ++lane) {
            uint64_t key = seed ^ (lane << 32);
            const auto sampled = splitmix64(key);
            const auto a = evaluate(base->path, sampled);
            const auto b = evaluate(option.path, sampled);
            if (stop()) { note.status = 4; return ordinary; }
            if (!a || !b || !protects(*a, *b, count, rate, p.lv38TwoWaveMainCover != 0)) {
                note.considered[option.trace].result = 2;
                break;
            }
            ++confirmed;
        }
        if (confirmed != LANES) continue;
        note.status = 3;
        note.confirmations = confirmed;
        note.route = option.path;
        note.selected = option.path.id[0];
        save(option, true);
        note.considered[option.trace].result = 4;
        return note.selected;
    }
    if (retained && samePlacements(*retained, base->path)) {
        const auto viable = [count](const Reading& r) {
            return std::none_of(r.result.begin(), r.result.begin() + count, [](const Endpoint& e) {
                return e.dead;
            });
        };
        int confirmed = viable(*base);
        for (uint64_t lane = 1; confirmed && lane < LANES; ++lane) {
            uint64_t key = seed ^ (lane << 32);
            const auto r = evaluate(base->path, splitmix64(key));
            if (stop()) { note.status = 4; return ordinary; }
            if (!r || !viable(*r)) break;
            ++confirmed;
        }
        if (confirmed == LANES) {
            note.route = base->path;
            note.confirmations = confirmed;
            save(*base, true);
        }
    }
    return ordinary;
}

template<class Stop>
int prefer(const Observation& o, const PlanSet& mine, const PlanSet& theirs, int ordinary,
           unsigned legal, const Params& p, uint64_t seed, Stop stop, Note& note, const Plan* retained = nullptr) {
    if (!p.lv38TwoWaveMainCover)
        return select(o, mine, theirs, ordinary, legal, p, seed, stop, note, retained, false);

    Params legacy = p;
    legacy.lv38TwoWaveMainCover = 0;
    const int before = select(o, mine, theirs, ordinary, legal, legacy, seed, stop, note, retained, true);
    note.beforeMainReview = before;
    if (note.status != 3 || note.route.count < 1) return before;
    bool spends = false;
    for (int k = 0; k < note.scenarios; ++k)
        if (note.selectedWaves[k] >= 5 && note.selectedWaves[k] > note.ordinaryWaves[k] &&
            note.selectedMain[k] < note.ordinaryMain[k]) spends = true;
    if (!spends) return before;

    note.mainReview = true;
    if (stop()) {
        note.mainReviewIncomplete = true;
        return before;
    }
    Note reviewed;
    const int after = select(o, mine, theirs, ordinary, legal, p, seed, stop, reviewed, retained, false);
    if (reviewed.status < 2 || reviewed.status >= 4 || stop()) {
        note.mainReviewIncomplete = true;
        return before;
    }
    reviewed.beforeMainReview = before;
    reviewed.mainReview = true;
    note = std::move(reviewed);
    return after;
}
} // namespace juraic::ai::lv38_two_wave
