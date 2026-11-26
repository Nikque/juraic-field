#pragma once
// The answers a field holds, and how likely its player is to have one in time.
//
// Whether a small chain is a crush (the other side has no answer), a poke (his only answer is his main chain) or a
// loss (he answers and sends more back), and how large an answer should be (can the other side answer it in turn?),
// all hang on one question: can this field, with the pairs its player places before an attack lands, fire a chain of
// so many points that leaves him a main chain of so many?
//
// The chains a field holds are those of the trial drops (detect.h): pieces of one colour added on one column. A
// player gets the pairs on the screen for sure, and after them pairs nobody has seen: a piece of those has each
// colour of the rules with the same chance. From that the chance that he has the pieces for one of the chains that
// are enough is worked out - in whole numbers, parts of a million.
//
// What is left out: that the pieces have to reach their column (other pieces may be in the way), that a pair which
// does not help has to be put somewhere, and chains that need pieces of two colours. So the chance is that of having
// the pieces, an upper bound of having the answer.
#include "build.h"
#include "detect.h"
#include "params.h"
#include <cmath>
#include <vector>

namespace juraic::ai {

struct Reply {
    int points = 0;    // score of the chain
    int frames = 0;    // from its first wave to its end
    int8_t added = 0;  // the pieces of `color` that start it, on column x
    int8_t x = 0;
    Cell color = 0;
    int mainAfter = 0; // points of the largest chain the field holds after it (buildValue)
};

struct Replies {
    int count = 0;
    Reply item[W * MAX_COLORS];
    int main = 0; // points of the largest chain the field holds as it stands (buildValue)
};

// The chains the trial drops start on a settled field, each with what it leaves.
inline void repliesOf(const GameField& field, const Rules& rules, const Timing& timing, const Params& p, Replies& out) {
    out.count = 0, out.main = 0;
    Trials trials;
    BitField after[W * MAX_COLORS];
    detect(field, rules, timing, p.trialMaxAdd, trials, after);
    buildValue(field, rules, timing, p, &out.main);
    for (int i = 0; i < trials.count; ++i) {
        const Trial& t = trials.item[i];
        if (t.chains < rules.chainRequirement) continue;
        GameField left = field;
        left.f = after[i];
        Reply& r = out.item[out.count++];
        r.points = t.points, r.frames = t.frames, r.added = t.added, r.x = t.x, r.color = t.color, r.mainAfter = 0;
        buildValue(left, rules, timing, p, &r.mainAfter);
    }
}

constexpr int64_t CHANCE_SURE = 1000000; // chances are parts of a million

// The chance that none of `pieces` unseen pieces has one of `of` given colours, of `colors` colours.
inline int64_t chanceNone(int of, int pieces, int colors) {
    if (of >= colors) return pieces > 0 ? 0 : CHANCE_SURE;
    int64_t v = CHANCE_SURE;
    for (int i = 0; i < pieces; ++i) v = v * (colors - of) / colors;
    return v;
}
// The chance that `wanted` or more of `pieces` unseen pieces have one given colour.
inline int64_t chanceAtLeast(int wanted, int pieces, int colors) {
    if (wanted <= 0) return CHANCE_SURE;
    if (pieces < wanted || colors < 1) return 0;
    int64_t fewer = 0; // the chance of exactly j of them, j below `wanted`
    for (int j = 0; j < wanted; ++j) {
        int64_t v = CHANCE_SURE;
        for (int i = 0; i < pieces - j; ++i) v = v * (colors - 1) / colors;
        for (int i = 0; i < j; ++i) v = v * (pieces - i) / (i + 1) / colors;
        fewer += v;
    }
    return fewer >= CHANCE_SURE ? 0 : CHANCE_SURE - fewer;
}
// The limit least[code] of the colour of the rules' palette at `index` (0: none, or a code outside 1..MAX_COLORS).
inline int limitOf(const int* least, const Rules& rules, int index) {
    const Cell code = rules.palette[size_t(index)];
    return code >= 1 && code <= MAX_COLORS ? least[code] : 0;
}
// The chance that, of `pieces` unseen pieces of the colours in play of `rules`, no colour c has least[c] or more of
// them (least is indexed by cell code; least[c] 0: any number of colour c).
inline int64_t chanceNoneOf(const int* least, int pieces, const Rules& rules) {
    if (pieces < 0) pieces = 0;
    const int colors = rules.colorsInPlay();
    int limited = 0;
    for (int i = 0; i < colors; ++i) limited += limitOf(least, rules, i) > 0;
    // fit[m]: the chance that m pieces spread over the colours taken so far keep under their limits
    std::vector<double> fit(size_t(pieces) + 1, limited < colors ? 1.0 : 0.0), next(size_t(pieces) + 1);
    fit[0] = 1.0;
    int spread = colors - limited; // the colours taken so far: at first those without a limit
    for (int i = colors - 1; i >= 0; --i) {
        const int limit = limitOf(least, rules, i);
        if (limit <= 0) continue;
        ++spread;
        for (int m = 0; m <= pieces; ++m) {
            if (spread == 1) {
                next[size_t(m)] = m < limit ? fit[0] : 0.0;
                continue;
            }
            const double in = 1.0 / spread, out = 1.0 - in;
            double term = std::pow(out, m), sum = 0; // the chance that k of the m pieces have this colour, from k = 0
            for (int k = 0; k < limit && k <= m; ++k) {
                sum += term * fit[size_t(m - k)];
                term = term * (m - k) / (k + 1) * in / out;
            }
            next[size_t(m)] = sum;
        }
        fit.swap(next);
    }
    return std::llround(fit[size_t(pieces)] * double(CHANCE_SURE));
}

// The chance (parts of a million) that the player of the field `r` was made of has the pieces for an answer among the
// `pairs` pairs he places before an attack lands: a chain of `need` points or more that leaves a main chain of `keep`
// points or more. known, knownCount: the pairs he gets first, in order (those on the screen); the pairs after them
// are unseen.
inline int64_t answerChance(const Replies& r, int need, int keep, int pairs, const Pair* known, int knownCount, const Rules& rules) {
    if (pairs <= 0) return 0;
    const int colors = rules.colorsInPlay();
    int have[MAX_COLORS + 3]{}; // pieces of each colour in the known pairs he places
    const int seen = pairs < knownCount ? pairs : knownCount;
    for (int i = 0; i < seen; ++i) {
        if (known[i].axis <= MAX_COLORS) ++have[known[i].axis];
        if (known[i].child <= MAX_COLORS) ++have[known[i].child];
    }
    const int unseen = (pairs - seen) * 2;
    int least[MAX_COLORS + 3]{}; // by colour: the fewest unseen pieces of it that make an answer (0: none does)
    for (int i = 0; i < r.count; ++i) {
        const Reply& q = r.item[i];
        if (q.points < need || q.mainAfter < keep || q.color > MAX_COLORS) continue;
        const int more = q.added - have[q.color];
        if (more <= 0) return CHANCE_SURE;
        if (!least[q.color] || more < least[q.color]) least[q.color] = more;
    }
    int limited = 0, single = 0, wanted = 0; // (of the colours in play: no unseen piece has another colour)
    for (int i = 0; i < colors; ++i) {
        const int limit = limitOf(least, rules, i);
        if (!limit) continue;
        ++limited, single += limit == 1, wanted = limit;
    }
    if (!limited) return 0;
    if (single == limited) return CHANCE_SURE - chanceNone(single, unseen, colors);
    if (limited == 1) return chanceAtLeast(wanted, unseen, colors);
    return CHANCE_SURE - chanceNoneOf(least, unseen, rules);
}

// The largest chain among the answers of `r` that leaves a main chain of `keep` points or more: its points (0: none).
inline int largestAnswer(const Replies& r, int keep) {
    int best = 0;
    for (int i = 0; i < r.count; ++i)
        if (r.item[i].mainAfter >= keep && r.item[i].points > best) best = r.item[i].points;
    return best;
}

} // namespace juraic::ai
