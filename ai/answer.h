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
    unsigned single = 0; // the colours of which one unseen piece more is enough
    int64_t best = 0;
    for (int i = 0; i < r.count; ++i) {
        const Reply& q = r.item[i];
        if (q.points < need || q.mainAfter < keep || q.color > MAX_COLORS) continue;
        const int more = q.added - have[q.color];
        if (more <= 0) return CHANCE_SURE;
        if (more == 1) single |= 1u << q.color;
        else {
            const int64_t c = chanceAtLeast(more, unseen, colors);
            if (c > best) best = c;
        }
    }
    if (single) {
        int of = 0;
        for (unsigned m = single; m; m &= m - 1) ++of;
        const int64_t c = CHANCE_SURE - chanceNone(of, unseen, colors);
        if (c > best) best = c;
    }
    return best;
}

// The largest chain among the answers of `r` that leaves a main chain of `keep` points or more: its points (0: none).
inline int largestAnswer(const Replies& r, int keep) {
    int best = 0;
    for (int i = 0; i < r.count; ++i)
        if (r.item[i].mainAfter >= keep && r.item[i].points > best) best = r.item[i].points;
    return best;
}

} // namespace juraic::ai
