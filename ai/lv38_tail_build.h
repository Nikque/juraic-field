#pragma once
#include "decision_metrics.h"
#include "preparation.h"
#include "lv38_tail.h"
#include <cstdlib>

namespace juraic::ai::lv38_tail_build {

struct Note {
    int ordinary = -1, selected = -1, ownCells = 0, eligibleRoots = 0;
    int points = 0, waves = 0, retained = 0;
    int64_t ordinaryValue = 0, selectedValue = 0, bestValue = 0;
    bool stopped = false;
};

inline int64_t score(const BuildResult& beam, const Params& p, unsigned quiet, bool timedSum, int id) {
    if (!beam.cut && p.x3ByValue && (beam.valued & quiet) == quiet) return beam.value[id];
    const int depth = beam.cut ? std::min(beam.layers, HORIZONS) : beam.depth;
    const int64_t sum = beam.cut ? beam.sumBy[id][depth - 1] : beam.sum[id];
    if (!timedSum) return sum;
    const int from = std::max(1, p.x3SoonFrom);
    const int to = std::min({HORIZONS, depth, p.x3SoonTo > 0 ? p.x3SoonTo : depth});
    if (from > to) return sum;
    int64_t early = 0;
    for (int n = from - 1; n < to; ++n) early += beam.sumBy[id][n];
    return (sum * (1000 - p.x3Soon) + early / (to - from + 1) * p.x3Soon) / 1000;
}

template<class Stop>
int prefer(const Observation& o, const PlanSet& mine, const BuildResult& beam, int ordinary, unsigned allowed,
           const Params& p, bool timedSum, Stop stop, Note& note) {
    metrics::Scope scope(metrics::Part::TailRank);
    if (ordinary < 0 || !mine.pairCount || o.players != 2 || p.handWait ||
        o.self().incoming.total() || o.opponent().incoming.total() || o.self().scoring.allClear ||
        o.opponent().scoring.allClear || o.busyLeft[o.me] || o.busyChain[1 - o.me] ||
        (o.self().phase != Phase::Control && o.self().phase != Phase::Next) ||
        (beam.cut && beam.layers < 2)) return ordinary;
    const unsigned quiet = quietSetupChoices(o, mine, allowed & beam.alive);
    if (!(quiet >> ordinary & 1)) return ordinary;

    note.ordinary = note.selected = ordinary;
    note.ownCells = o.self().field.f.normal().count();
    const auto expired = [&] {
        if (stop()) note.stopped = true;
        return note.stopped;
    };
    if (expired()) return ordinary;
    std::array<int64_t, PLACEMENTS> values{};
    std::array<int, PLACEMENTS> ranked{};
    int count = 0;
    for (unsigned ids = quiet; ids; ids &= ids - 1) {
        const int id = std::countr_zero(ids);
        ranked[count++] = id;
        values[id] = score(beam, p, quiet, timedSum, id);
    }
    note.ordinaryValue = note.selectedValue = values[ordinary];
    note.eligibleRoots = count;
    if (!lv38_tail::inWindow(note.ownCells + 2, p.lv38CutFrom)) {
        note.bestValue = values[ordinary];
        for (int i = 0; i < count; ++i) note.bestValue = std::max(note.bestValue, values[ranked[i]]);
        return ordinary;
    }
    std::sort(ranked.begin(), ranked.begin() + count, [&](int a, int b) {
        if (values[a] != values[b]) return values[a] > values[b];
        if ((a == ordinary) != (b == ordinary)) return a == ordinary;
        return a < b;
    });
    note.bestValue = values[ranked[0]];
    const auto nearBest = [&](int id) {
        return (note.bestValue - values[id]) * 1000 <= std::abs(note.bestValue) * p.lv38CutNear;
    };
    const auto shape = [&](int id, int& retained) {
        GameField field = o.self().field;
        juraic::place(field, placement(id), mine.pair[0]);
        Trials trials;
        detect(field, o.rules, o.timing, 2, trials, nullptr, false);
        for (int i = 0; i < trials.count; ++i) {
            const Trial& t = trials.item[i];
            if (!lv38_tail::eligible(t.chains, t.points, p.lv38TailMin)) continue;
            KeyPlan main;
            readMainChain(field, o.rules, o.timing, p, nullptr, &main);
            return lv38_tail::bestShape(field, trials, main, o.rules, o.timing, &retained,
                                        p.lv38TailMin, p.lv38TailOfMain > 0);
        }
        return lv38_tail::Shape{};
    };

    int retained = 0, pick = ordinary;
    auto best = shape(ordinary, retained);
    for (int i = 0; i < count && nearBest(ranked[i]); ++i) {
        if (expired()) return ordinary;
        const int id = ranked[i];
        if (id == ordinary) continue;
        int remains = 0;
        const auto next = shape(id, remains);
        if (next.value() <= best.value()) continue;
        best = next;
        pick = id;
        retained = remains;
    }
    if (expired() || pick == ordinary) return ordinary;
    note.selected = pick;
    note.selectedValue = values[pick];
    note.points = best.points;
    note.waves = best.waves;
    note.retained = retained;
    return pick;
}

} // namespace juraic::ai::lv38_tail_build
