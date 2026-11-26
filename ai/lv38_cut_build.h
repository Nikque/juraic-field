#pragma once

#include "lv38_tail_build.h"

#include <tuple>

namespace juraic::ai::lv38_cut_build {

struct Note {
    int depth = 0, queues = 0;
    bool preserved = false;
};

inline bool quiet(const Observation& o, const Params& p) {
#ifdef JURAIC_CUT_SOLO_TRIAL
    const bool players = o.players == 1 || o.players == 2;
#else
    const bool players = o.players == 2;
#endif
    if (!players || p.handWait || o.self().incoming.total() || o.opponent().incoming.total() ||
        o.self().scoring.allClear || o.opponent().scoring.allClear || o.busyLeft[o.me] || o.busyChain[1 - o.me]) return false;
    const auto other = o.opponent().phase;
    return other != Phase::Wave && other != Phase::Decide &&
        (o.self().phase == Phase::Control || o.self().phase == Phase::Next);
}

inline BuildCuts inspect(const GameField& board, const Observation& o, const Params& p) {
    BuildCuts cuts;
    buildValue(board, o.rules, o.timing, p, nullptr, nullptr, false, &cuts);
    return cuts;
}

struct Holdings {
    lv38_tail::Shape tail, lowerTail;
    lv38_head::Shape head;
    int main = 0;
};

inline Holdings measure(const GameField& board, const Rules& rules, const Timing& timing, const Params& p) {
    Holdings result;
    KeyPlan main;
    buildValue(board, rules, timing, p, &result.main, &main);
    if (result.main <= 0) return result;
    Trials trials;
    detect(board, rules, timing, 2, trials, nullptr, false);
    result.tail = lv38_tail::bestShape(board, trials, main, rules, timing);
    result.lowerTail = lv38_tail::bestShape(board, trials, main, rules, timing, nullptr, lv38_head::MIN_POINTS);
    result.head = lv38_head::shape(board, main, rules, timing);
    return result;
}

template<class Stop, class Visit>
void afterQuiet(const GameField& board, const Pair* pairs, int left, const Rules& rules, Stop& stop, Visit& visit) {
    if (stop()) return;
    if (!left) {
        visit(board);
        return;
    }
    unsigned legal = differentFields(legalPlacements(board), pairs[0]);
    while (legal && !stop()) {
        const int id = std::countr_zero(legal);
        legal &= legal - 1;
        GameField next = board;
        place(next, placement(id), pairs[0]);
        if (!next.dead() && !anyClear(next.f, rules)) afterQuiet(next, pairs + 1, left - 1, rules, stop, visit);
    }
}

inline auto shapeRank(const BuildCuts& cut) {
    return std::tuple(cut.count(), cut.value(), cut.main);
}

inline std::pair<int, int> queueRank(const BuildResult& beam, int id, int limit) {
    unsigned found = 0;
    int depthSum = 0;
    for (int depth = 0; depth < limit; ++depth) {
        const unsigned fresh = beam.cutQueues[id][depth] & ~found;
        found |= fresh;
        depthSum += std::popcount(fresh) * (depth + 1);
    }
    return {std::popcount(found), -depthSum};
}

template<class Stop>
int prefer(const Observation& o, const PlanSet& mine, const BuildResult& beam, int ordinary, unsigned allowed,
           const Params& p, bool timedSum, Stop expired, Note& note) {
    if (!p.lv38Midgame || !quiet(o, p) || ordinary < 0 || !mine.knownCount || beam.cut || expired() ||
        !lv38_tail::inWindow(o.self().field.f.normal().count(), p.lv38CutFrom)) return ordinary;
    const unsigned legal = quietSetupChoices(o, mine, allowed & beam.alive);
    if (!(legal >> ordinary & 1u)) return ordinary;
    std::array<int64_t, PLACEMENTS> score{};
    int64_t best = INT64_MIN;
    for (unsigned rest = legal; rest; rest &= rest - 1) {
        const int id = std::countr_zero(rest);
        score[id] = lv38_tail_build::score(beam, p, legal, timedSum, id);
        best = std::max(best, score[id]);
    }
    unsigned nearBestChoices = 0;
    for (unsigned rest = legal; rest; rest &= rest - 1) {
        const int id = std::countr_zero(rest);
        if ((best - score[id]) * 1000 <= std::abs(best) * p.lv38CutNear) nearBestChoices |= 1u << id;
    }
    if (!(nearBestChoices & ~(1u << ordinary))) return ordinary;

    const auto board = [&](int id) {
        GameField next = o.self().field;
        place(next, placement(id), mine.pair[0]);
        return next;
    };
    if (p.lv38CutAhead > 0) {
        countComponent(CountedComponent::CutAhead);
        const bool held = inspect(o.self().field, o, p).count() != 0;
        const GameField original = board(ordinary);
        const int limit = std::min(std::clamp(p.lv38CutAhead, 1, 2) + 1, mine.knownCount);
        for (int depth = 1; depth <= limit && !expired(); ++depth) {
            int main = -1;
            std::vector<BuildCuts> originalCuts;
            auto reference = [&](const GameField& field) {
                const auto cut = inspect(field, o, p);
                main = std::max(main, cut.main);
                if (cut.count()) originalCuts.push_back(cut);
            };
            afterQuiet(original, mine.pair + 1, depth - 1, o.rules, expired, reference);
            if (main < 0) continue;
            const auto fits = [&](const BuildCuts& cut) {
                return cut.count() && int64_t(cut.main) * 1000 >= int64_t(main) * p.lv38CutKeep;
            };
            BuildCuts shape;
            for (const auto& cut : originalCuts)
                if (fits(cut) && shapeRank(cut) > shapeRank(shape)) shape = cut;
            if (held && depth == 1 && shape.count()) return ordinary;
            if (held && depth == 1) countComponent(CountedComponent::Preserve);
            int selected = ordinary;
            for (unsigned rest = nearBestChoices & ~(1u << ordinary); rest && !expired(); rest &= rest - 1) {
                const int id = std::countr_zero(rest);
                auto compare = [&](const GameField& field) {
                    const auto cut = inspect(field, o, p);
                    if (fits(cut) && shapeRank(cut) > shapeRank(shape)) {
                        selected = id;
                        shape = cut;
                    }
                };
                afterQuiet(board(id), mine.pair + 1, depth - 1, o.rules, expired, compare);
            }
            if (shape.count() && !expired()) {
                if (selected != ordinary) note = {depth, 0, held && depth == 1};
                return selected;
            }
        }
    }
    const int depth = std::min({p.lv38CutQueues, beam.depth, HORIZONS});
    if (depth <= 0 || expired()) return ordinary;
    const auto defaultRank = queueRank(beam, ordinary, depth);
    auto rank = std::tuple(defaultRank.first, defaultRank.second, score[ordinary]);
    int selected = ordinary;
    for (unsigned rest = nearBestChoices & ~(1u << ordinary); rest; rest &= rest - 1) {
        const int id = std::countr_zero(rest);
        const auto [queues, early] = queueRank(beam, id, depth);
        const auto next = std::tuple(queues, early, score[id]);
        if (queues > defaultRank.first && next > rank) {
            selected = id;
            rank = next;
        }
    }
    if (expired() || selected == ordinary) return ordinary;
    note = {0, std::get<0>(rank), false};
    for (int layer = 0; layer < depth; ++layer) {
        if (beam.cutQueues[selected][layer]) {
            note.depth = layer + 1;
            break;
        }
    }
    return selected;
}

} // namespace juraic::ai::lv38_cut_build
