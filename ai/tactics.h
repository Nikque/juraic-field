#pragma once
// X3: moves by kind. What a strong player does in the middle game is made of a
// few kinds of moves, and each kind is looked for on purpose and judged with
// the real times of the Duel:
//
//   win      a placement that wins against every answer, and whose chain outlasts what the
//            opponent can build while it runs;
//   clear    an all clear the pairs on the screen get by a small chain;
//   answer   garbage is on its way: the plan that lets none of it fall, and
//            among those the most efficient one: the main chain kept, few
//            pieces spent, the most garbage left above the opponent. When no
//            plan avoids all of it, the one that lets the least fall. When the
//            opponent's chain still runs for many moves and nothing on the
//            screen answers it yet, the beam search builds towards an answer
//            that is ready in time;
//   poke     no garbage on its way, and the own field is filling: a chain that keeps
//            much of the main chain, when the opponent's worst reply leaves the
//            position no worse. A reply in which he spends his own main chain does
//            not count against it when X3, building on while his chain runs and
//            firing second, offsets it;
//   fire     (rules that are switched off) the chain in hand on a field that is full;
//   build    otherwise, the placement of the beam search.
//
// Garbage that falls is weighed heavily, even one row (the principle:
// avoid it whenever possible; an efficient answer that puts the opponent under
// pressure is best; taking it on purpose to counter is an exception and is not
// done here).
#include "exchange.h"
#include "pace.h"

namespace juraic::ai {

// The tactics of X3: which placement, given the plans of both players and the searches. When a choice is made, how
// long it may take and what the opponent is seen as is X3Pace's (pace.h).
class X3Agent : public X3Pace {
public:
    explicit X3Agent(const Params& params = Params{}) : X3Pace(params) {}
    // (a thread of X3Pace may be inside decide(): it must have ended before the members below go)
    ~X3Agent() override {
        finish();
        dropEarly();
    }
    void startRound(int seat, uint64_t seed) override {
        X3Pace::startRound(seat, seed);
        held.depth = 0;
    }

protected:
    Gaze gaze;
    PlanSet mine, theirs;

    // The beam search for the own field. depth 0 = the usual depth.
    // percent: of the time of the choice, when it is to end (Params::thinkBudget).
    // joined: fields are valued by the chains that stand, without key pieces (x3ExtendJoined).
    BuildResult searchBuild(const Observation& o, int depth, int percent = 100, bool joined = false) {
        Params q = p;
        if (joined) q.evKeyed = 0;
        if (depth > 0 && depth < q.beamDepth) q.beamDepth = depth;
        // Keep the third column low, unless it is already higher (then no placement would be left).
        int h[W];
        o.self().field.heights(h);
        q.beamThirdMax = thirdLimit(h);
        Pair known[3];
        for (int i = 0; i < mine.pairCount; ++i) known[i] = mine.pair[i];
        return search(builder, o.self().field, known, mine.pairCount, o, q, roundSeed * 1000 + uint64_t(o.self().moves), percent, p.thinkFit >= 1);
    }

    // x3Soon: what the search found below first placement `id`, with an eye on when it is fired - its sum (the largest
    // chain found within the depth of the search, over the queues) mixed with the mean of the same sum as it stood after
    // each placement from x3SoonFrom to x3SoonTo. A search too short for that (or stopped by the clock before): its sum.
    int64_t soonSum(const BuildResult& b, int id) const {
        const int depth = b.cut ? b.layers : b.depth;
        int to = p.x3SoonTo > 0 && p.x3SoonTo < depth ? p.x3SoonTo : depth;
        if (to > HORIZONS) to = HORIZONS;
        const int from = p.x3SoonFrom > 1 ? p.x3SoonFrom : 1;
        if (from > to) return b.sum[id];
        int64_t all = 0;
        for (int n = from - 1; n < to; ++n) all += b.sumBy[id][n];
        return (b.sum[id] * (1000 - p.x3Soon) + all / (to - from + 1) * p.x3Soon) / 1000;
    }

    // The placement of a beam search among `legal`, with a little chance (Params::b0Noise). -1 = none.
    // soon: the search was made to build with (its full depth): Params::x3Soon applies.
    int pickBuild(const Observation& o, const BuildResult& b, unsigned legal, bool soon = false) {
        if (b.best < 0) return -1;
        if (p.x3ByValue) {
            // The best fields ahead. (The first pairs of a round: any placement, Params::b0OpeningNoise.)
            const bool opening = o.self().moves < p.b0OpeningMoves && p.b0OpeningNoise > 0;
            int pick = -1;
            int64_t bestValue = 0;
            uint64_t bestTie = 0;
            for (int id = 0; id < PLACEMENTS; ++id) {
                if (!(b.valued >> id & 1) || !(legal >> id & 1)) continue;
                const int64_t value = opening ? 0 : b.value[id];
                const uint64_t tie = rng.next();
                if (pick < 0 || value > bestValue || (value == bestValue && tie > bestTie)) pick = id, bestValue = value, bestTie = tie;
            }
            if (pick >= 0) return pick;
        }
        const int noise = o.self().moves < p.b0OpeningMoves ? p.b0OpeningNoise : p.b0Noise;
        const uint64_t range = 1 + uint64_t(b.sum[b.best]) * uint64_t(noise > 0 ? noise : 0) / 1000;
        int pick = -1;
        int64_t bestSum = 0;
        int64_t bestReached = 0;
        uint64_t bestTie = 0;
        int64_t added[PLACEMENTS]{}, reach[PLACEMENTS]{}; // (for x3Soon: what was added to the sum of each placement, and the two it is told apart by)
        uint64_t ties[PLACEMENTS]{};
        for (int id = 0; id < PLACEMENTS; ++id) {
            if (!(b.alive >> id & 1) || !(legal >> id & 1)) continue;
            const int64_t mixed = p.x3ValueMix > 0 && (b.valued >> id & 1) ? b.value[id] * p.x3ValueMix / 1000 : 0;
            added[id] = mixed + int64_t(rng.below(range));
            const int64_t sum = b.sum[id] + added[id];
            const uint64_t tie = rng.next();
            // A search that the clock stopped early has found few chains or none, and equal sums
            // would be told apart by chance: then by the fields it did reach.
            const int64_t reached = b.cut && (b.valued >> id & 1) ? b.value[id] : 0;
            reach[id] = reached, ties[id] = tie;
            if (pick < 0 || sum > bestSum || (sum == bestSum && (reached > bestReached || (reached == bestReached && tie > bestTie))))
                pick = id, bestSum = sum, bestReached = reached, bestTie = tie;
        }
        if (soon && p.x3Soon > 0 && pick >= 0 && b.pointsBy[pick][0] < p.beamEndPoints) {
            // x3Soon. The placement the sums choose fires no chain that ends a line of the search (the main chain) with
            // the pair in hand: among the placements that fire none either, the one with the largest sum as soonSum
            // counts it (the same chance and the same numbers for equal ones as above).
            int timed = -1;
            int64_t bestTimed = 0;
            for (int id = 0; id < PLACEMENTS; ++id) {
                if (!(b.alive >> id & 1) || !(legal >> id & 1) || b.pointsBy[id][0] >= p.beamEndPoints) continue;
                const int64_t sum = soonSum(b, id) + added[id];
                if (timed < 0 || sum > bestTimed || (sum == bestTimed && (reach[id] > reach[timed] || (reach[id] == reach[timed] && ties[id] > ties[timed]))))
                    timed = id, bestTimed = sum;
            }
            if (timed >= 0) pick = timed;
        }
        return pick;
    }

    // (depth 0: the search to build with; a shorter one is that of the moves left while the opponent's chain runs)
    int build(const Observation& o, unsigned legal, int depth) { return pickBuild(o, searchBuild(o, depth), legal, depth == 0); }

    Builder theirBuilder;

    // The plans below first placement `id` that fire: is the garbage of the
    // largest still enough to bury the opponent after the largest chain he can
    // build in the frames that chain takes? (He goes on placing pairs while it
    // runs; the pairs after those on the screen are the queues of the beam search.)
    //
    // What he can build does not depend on the first placement, only how many moves he has for it:
    // it is searched once a decision (Rival), as deep as the longest of my chains leaves him moves,
    // and read off for fewer moves (BuildResult::sumBy). (Searched again for every first placement,
    // one decision took up to 2.8 seconds where twenty of them "won".)
    // The search of his field of an earlier choice. As long as he has not placed a pair, the next
    // choice asks for the same search again (the same field, the same pairs; with the fixed
    // sequences of pairs the seed plays no part): then it is taken from here. A search as deep
    // or deeper answers for fewer moves from its tables (BuildResult::sumBy), as one made now.
    struct Held {
        int depth = 0; // 0 = none
        GameField field;
        Pair pair[3]{};
        int pairs = 0;
        uint64_t seed = 0;
        BuildResult built;
    } held;
    struct Rival {
        bool searched = false;
        bool noPair = false; // he holds no pair again in this round
        int64_t from = 0;    // the frame from which he places
        int need = 0;        // garbage pieces that close his field as it is then
        int deepest = 0;     // the most placements that are searched for him
        GameField field;     // his field then
        int cells = 0;       // the cells on it (a move takes him the frames of a field that full: Params::movesIn)
        BuildResult built;   // the search from it, `depth` placements deep
        int depth = 0;
    };
    bool outlasts(const Observation& o, int id, int rate, uint64_t seed, Rival& rival) {
        const Plan* big = nullptr;
        for (const Plan& s : mine.plans)
            if (s.id[0] == id && s.fireDepth && (!big || s.sent > big->sent)) big = &s;
        if (!big) return true;
        const Player& opp = o.opponent();
        if (opp.phase == Phase::Dead || theirs.pairCount == 0) return true;
        const auto movesUntil = [&](int64_t end) {
            const int moves = p.movesIn(end - rival.from, rival.cells);
            return moves < 1 ? 1 : moves < rival.deepest ? moves : rival.deepest;
        };
        Params q = p;
        if (!rival.searched) {
            rival.searched = true;
            GameField field = opp.field;
            rival.from = o.frame, rival.deepest = p.beamDepth;
            if (p.x3OutlastSettled) {
                // The field of the observation holds a pair he has just put before anything it
                // clears is gone: a group of four still standing, a third column that looks closed
                // (seen: such a field was taken for one that cannot answer, and the main chain was
                // fired into a larger one). His field and the time are taken from the frame in which
                // he next holds a pair; and the search looks as far as he has moves.
                const int him = 1 - o.me;
                Duel d(o, seed);
                rival.noPair = d.player[him].phase != DuelPhase::Control && !toPairOf(d, him, o.frame + 6000);
                field = d.player[him].field, rival.from = d.frame, rival.deepest = HORIZONS;
            }
            int h[W];
            field.heights(h);
            rival.need = W * (12 - h[DEATH_X]);
            rival.cells = field.f.count();
            int64_t lastEnd = 0;
            for (const Plan& s : mine.plans)
                if (s.fireDepth && s.endFrame > lastEnd) lastEnd = s.endFrame;
            q.beamDepth = rival.depth = movesUntil(lastEnd);
            if (!rival.noPair) {
                bool same = held.depth >= rival.depth && held.pairs == theirs.pairCount && held.field == field && (held.seed == seed || !Builder::seeded(o.rules, q));
                for (int i = 0; same && i < held.pairs; ++i) same = held.pair[i].axis == theirs.pair[i].axis && held.pair[i].child == theirs.pair[i].child;
                if (same) {
                    rival.built = held.built, rival.depth = held.depth, ++rivalReused;
                } else {
                    rival.built = search(theirBuilder, field, theirs.pair, theirs.pairCount, o, q, seed, p.thinkRival, p.thinkFit >= 2);
                    ++rivalSearches;
                    // (a search the clock stopped is not kept: the next one may get further)
                    held.depth = rival.built.cut ? 0 : rival.depth, held.field = field, held.pairs = theirs.pairCount, held.seed = seed, held.built = rival.built;
                    for (int i = 0; i < theirs.pairCount; ++i) held.pair[i] = theirs.pair[i];
                }
            }
            rival.field = field;
        }
        if (rival.noPair || rival.from > big->endFrame) return true; // no pair of his before my chain ends
        // The search of as many moves as this chain leaves him: the one made, when it is that deep;
        // its tables for fewer; and one of its own beyond the tables (a beamDepth above HORIZONS).
        const int depth = movesUntil(big->endFrame);
        const bool whole = depth >= rival.depth, tabled = !whole && depth <= HORIZONS;
        BuildResult alone;
        if (!whole && !tabled) {
            q.beamDepth = depth;
            alone = search(theirBuilder, rival.field, theirs.pair, theirs.pairCount, o, q, seed, p.thinkRival, p.thinkFit >= 2);
        }
        const BuildResult& b = whole || tabled ? rival.built : alone;
        // A search of his field that the clock stopped before the moves he has were searched has
        // not seen what he builds in that time: the chain is not known to outlast it.
        if (b.cut && !(tabled && depth <= b.layers)) return false;
        // (The largest in any queue is more than the same search finds with the pairs he
        // really gets, in three positions of four; the average over the queues is about as much.)
        const int queues = p.queues();
        int most = 0;
        for (int i = 0; i < PLACEMENTS; ++i) {
            const int64_t sum = tabled ? b.sumBy[i][depth - 1] : b.sum[i];
            const int points = p.x3OutlastMean ? ((b.alive >> i & 1) ? int(sum / queues) : 0) : tabled ? b.pointsBy[i][depth - 1] : b.points[i];
            if (points > most) most = points;
        }
        if (p.x3OutlastScale != 100) most = int(int64_t(most) * p.x3OutlastScale / 100); // (he may build more than this search of his field finds)
        const int answer = (most + opp.scoring.pool) / rate;
        // When he answers with a chain, what is left of mine falls on the field that chain has
        // emptied, not on the one he has now: a third column eleven high needs six pieces to
        // close today and eleven rows after his chain. (Counted: main chains were fired as
        // "wins" with 13 to 117 pieces to spare over an answer, into fields with that column
        // at nine to eleven.)
        int need = rival.need;
        if (p.x3OutlastSettled && answer >= p.x3OutlastAnswer && need < p.x3OutlastLeft) need = p.x3OutlastLeft;
        return big->sent - answer >= need;
    }
    bool outlasts(const Observation& o, int id, int rate, uint64_t seed) {
        Rival rival;
        return outlasts(o, id, rate, seed, rival);
    }

    // x3ChokeHis: may a chain of `points` be fired first, for what the opponent holds now? What he
    // holds: the largest chain a trial drop starts on his field (key pieces included, as buildValue
    // counts the main chain), or one his pairs on the screen fire. (Against a player who holds a
    // chain of his own, firing first loses: he builds on while mine runs and fires the larger one.
    // Against one who holds none it wins the round.)
    bool heHoldsLittle(const Observation& o, int rate, int points) const {
        if (p.x3ChokeHis <= 0) return true;
        int now = 0;
        buildValue(o.opponent().field, o.rules, o.timing, p, &now);
        int64_t his = now;
        for (const Plan& x : theirs.plans)
            if (x.fireDepth && int64_t(x.sent) * rate > his) his = int64_t(x.sent) * rate;
        return his * 100 < int64_t(points) * p.x3ChokeHis;
    }

    // The placements among which the beam search chooses when the best answer does not fire.
    // builds[id] = what falls after the building plan below first placement id (-1 = none),
    // score[id] = what that plan scores as an answer. slack < 0: those that let as little fall as
    // the best; else those that score within slack of the best.
    static unsigned shapeChoices(const int builds[PLACEMENTS], const int64_t score[PLACEMENTS], int bestCost, int64_t bestScore, int slack) {
        unsigned same = 0;
        for (int id = 0; id < PLACEMENTS; ++id)
            if (slack < 0 ? builds[id] == bestCost : builds[id] >= 0 && score[id] >= bestScore - slack) same |= 1u << id;
        return same;
    }

    // x3Enough: the placements among which the search that builds on against a running chain chooses - those of
    // `legal` below which the most queues of `b` fired a chain of `goal` points or more (BuildResult::enough; the depth
    // of that search is the moves his chain leaves, so every chain it found is fired in time). `legal` itself when no
    // queue fired one below any of them, or nothing is asked for: the sums then choose as they do without the switch.
    static unsigned enoughChoices(const BuildResult& b, unsigned legal, int64_t goal) {
        if (goal <= 0) return legal;
        int most = 0;
        unsigned with = 0;
        for (int id = 0; id < PLACEMENTS; ++id) {
            if (!(legal >> id & 1) || !(b.alive >> id & 1)) continue;
            const int queues = b.enough(id, goal);
            if (queues > most) most = queues, with = 0;
            if (queues == most) with |= 1u << id;
        }
        return most > 0 ? with : legal;
    }

    // x3GuardNear: the placements the guard chooses among - of the `guarded` ones (those whose plans answer the
    // opponent's threat about as well as the best of them) the ones whose sum in the beam search lies within
    // `thousandths` of `build`, the largest sum of a placement that builds. None: no guarded placement is that near,
    // and the guard narrows nothing. 0 thousandths: all the guarded ones, whatever the search found below them (x3Guard
    // as it was). (No parameter is named `near` here: play/main.cpp includes windows.h, where that word is a macro.)
    static unsigned guardChoices(const BuildResult& b, unsigned guarded, int64_t build, int thousandths) {
        if (thousandths <= 0) return guarded;
        unsigned nearby = 0;
        for (int id = 0; id < PLACEMENTS; ++id)
            if ((guarded >> id & 1) && (b.alive >> id & 1) && b.sum[id] * 1000 >= build * (1000 - thousandths)) nearby |= 1u << id;
        return nearby;
    }

    int decide(const Observation& o, unsigned legal) override {
        const int me = o.me;
        const uint64_t seed = roundSeed + uint64_t(o.self().moves);
        const int rate = o.rules.rateAt(o.frame);
        const bool secondChoice = choosingAgain; // the second choice of this pair (Params::thinkAgain)
        const int pairs = beginChoice(o, secondChoice); // (the clock of the choice, or the notes of a round played again: pace.h)
        gaze.plans(o, me, true, pairs, p, seed, mine);
        gaze.plans(o, 1 - me, false, pairs, p, seed, theirs);
        plansMade(pairs, secondChoice, mine.pairCount >= 3 && theirs.pairCount >= 3);
        kind = None, guardedNow = false;
        // (the plans are made of every legal placement; `legal` may hold fewer, those the pair has a path to)
        if (p.x3PathCheck) std::erase_if(mine.plans, [&](const Plan& s) { return !(legal >> s.id[0] & 1); });
        if (mine.plans.empty()) {
            // Every plan ends dead: garbage on its way closes the field in the reading, whatever is
            // placed. The game's garbage may fall on other columns, so the pair goes where the search
            // would put it, not on the first legal placement. (Seen: with the two left columns full
            // that was the third column itself, and the round ended there.)
            const int id = p.x3NoPlan && mine.pairCount > 0 ? build(o, legal, 0) : -1;
            return id >= 0 ? id : std::countr_zero(legal);
        }

        // The opponent's side of every exchange: standing still (and answering
        // with pairs not on the screen), then each of his plans.
        const int replies = int(theirs.plans.size()) + 1;
        const auto reply = [&](int r) { return r ? &theirs.plans[size_t(r - 1)] : nullptr; };

        // Win: against every reply some plan below the same first placement wins.
        {
            Rival rival;
            int worst[PLACEMENTS];
            // (x3WinBuild) the same of the plans that only build: the round is won below that placement whatever
            // the opponent does and without firing anything more - it is won already.
            int wonIdle[PLACEMENTS];
            unsigned has = 0;
            for (const Plan& s : mine.plans) has |= 1u << s.id[0];
            for (int r = 0; r < replies; ++r) {
                bool wins[PLACEMENTS]{}, quiet[PLACEMENTS]{};
                for (const Plan& s : mine.plans) {
                    const int id = s.id[0];
                    const bool building = p.x3WinBuild && !s.fireDepth && !quiet[id] && (r == 0 || wonIdle[id]);
                    if ((wins[id] || (r && !worst[id])) && !building) continue;
                    const Duel d = playOut(o, mine, &s, theirs, reply(r), p, seed);
                    if (positionValue(d.player[me], d.player[1 - me], o.rules, o.timing, p, rate, seed) >= -(VALUE_LOST / 2)) {
                        wins[id] = true;
                        if (!s.fireDepth) quiet[id] = true;
                    }
                }
                for (int id = 0; id < PLACEMENTS; ++id) worst[id] = (r == 0 || worst[id]) && wins[id], wonIdle[id] = (r == 0 || wonIdle[id]) && quiet[id];
            }
            // x3WinBuild. A round that is won already: the pair goes where the beam search puts it, among the
            // placements that keep it won - the AI builds on as it does at any other time. (Before, the first of the
            // winning placements was taken without a search, and the pairs were piled up anywhere: it lost nothing,
            // and looked wrong to a person.) A win that still needs a chain to be fired is
            // taken as before.
            if (p.x3WinBuild) {
                unsigned won = 0;
                for (int id = 0; id < PLACEMENTS; ++id)
                    if ((has >> id & 1) && (legal >> id & 1) && worst[id] && wonIdle[id] && (!p.x3WinCheck || outlasts(o, id, rate, seed, rival))) won |= 1u << id;
                if (won) {
                    const int id = won & (won - 1) ? build(o, won, 0) : -1;
                    return kind = Win, id >= 0 ? id : std::countr_zero(won);
                }
            }
            for (int id = 0; id < PLACEMENTS; ++id) {
                if (!(has >> id & 1) || !(legal >> id & 1) || !worst[id]) continue;
                if (p.x3WinCheck && !outlasts(o, id, rate, seed, rival)) continue;
                return kind = Win, id;
            }
        }

        // An all clear the pair in hand gets by a chain of x3ClearNow waves or fewer is taken,
        // whatever is on its way (an all clear by a chain of one is always taken; a
        // human who misses one by a chain of two or fewer stands out). Seen: the rule below
        // stood behind the answer, and with garbage on its way the all clear was let go.
        if (p.x3ClearNow > 0) {
            const Plan* clear = nullptr;
            for (const Plan& s : mine.plans) {
                if (!s.allClear || s.fireDepth != 1 || s.chains > p.x3ClearNow || !(legal >> s.id[0] & 1)) continue;
                if (!clear || s.points > clear->points) clear = &s;
            }
            if (clear) return kind = Clear, clear->id[0];
        }

        // What is on its way: the opponent's chain that is running, to its end.
        Duel afterChain(o, seed);
        const bool chainRuns = afterChain.player[1 - me].phase == DuelPhase::Check || afterChain.player[1 - me].phase == DuelPhase::Settle;
        if (chainRuns) toPairOf(afterChain, 1 - me, o.frame + 6000);
        const int arriving = afterChain.player[me].incoming.total();
        // What an answer is scored against, of the own field as it stands now.
        struct AnswerBase {
            // Under a row of garbage or more every column is covered. What two pieces of one colour or
            // a key piece would still start then hangs on where the garbage of this one reading fell
            // (seen: a chain of thirteen was "left" because two greens on the sixth column would have
            // run it; in the game the garbage fell otherwise). With x3RowStrict the main chain left
            // under a row is what a single piece fires: the Params for that.
            Params strict;
            // With key pieces the points of the main chain include what is not joined yet, and a
            // placement that only builds can make them much larger at once. What an answer is
            // credited with is the main chain it keeps, not one it promises: otherwise letting
            // garbage fall to go on building scores better than a clear that answers it exactly.
            // The main chain that stands now:
            int mainKeyed = 0;
            // A piece or two of garbage are not a row (a single one is better taken than
            // answered, unless the third column is eleven high, where it may close the field):
            // below one row what falls counts by its square, n * n / 6 instead of n (curve). With the
            // third column eleven high (tight) each of them closes the field once in six times, whatever
            // the one fall played out here did: that chance is counted as x3Close.
            bool tight = false, curve = false;
        };
        const auto answerBase = [&] {
            AnswerBase a{p};
            a.strict.evKeyed = 0, a.strict.trialMaxAdd = 1;
            if (p.evKeyed) buildValue(o.self().field, o.rules, o.timing, p, &a.mainKeyed);
            int own[W];
            o.self().field.heights(own);
            a.tight = own[DEATH_X] >= 11, a.curve = p.x3FallCurve && !a.tight;
            return a;
        };
        // What plan `s` is worth as an answer, in the Duel it was played out in (against the chain of the
        // opponent that is running; with x3Guard, against a plan of his that has not begun): what falls
        // (cost), the main chain it keeps (main), and the score. False: the field closes.
        const auto answerWorth = [&](const Plan& s, const Duel& d, const AnswerBase& base, int& cost, int& main, int64_t& score) {
            const DuelPlayer& mineAfter = d.player[me];
            if (mineAfter.phase == DuelPhase::Dead) return false;
            cost = mineAfter.fallen + mineAfter.incoming.total();
            const int surplus = d.player[1 - me].incoming.total() - o.opponent().incoming.total();
            main = s.mainAfter;
            if (mineAfter.fallen > 0) // the garbage may have buried it
                buildValue(mineAfter.field, o.rules, o.timing, p.x3RowStrict && mineAfter.fallen >= W ? base.strict : p, &main);
            if (p.evKeyed && main > base.mainKeyed) main = base.mainKeyed;
            int heights[W];
            mineAfter.field.heights(heights);
            const int danger = heights[DEATH_X] >= p.x3ThirdMax ? heights[DEATH_X] - p.x3ThirdMax + 1 : 0;
            int64_t fall = base.curve && cost < W ? int64_t(p.x3Fall) * cost * cost / W : int64_t(p.x3Fall) * cost;
            if (p.x3FallCurve && base.tight && cost < W) fall += int64_t(p.x3Close) * cost / W;
            score = -fall + int64_t(p.x3Main) * (main / rate) + int64_t(p.x3Surplus) * (surplus > 0 ? surplus : 0) -
                    int64_t(p.x3Used) * s.used - int64_t(p.x3Danger) * danger * danger;
            // Garbage on a field that is nearly full: there is no room left to dig the chain out or
            // to answer the next attack. (Seen in games of a person against the AI: it held a
            // chain of twelve on 67 to 72 cells, kept it and took five to eleven garbage pieces of
            // a chain of two, and the field closed.) With x3Crowd an answer that lets garbage
            // fall loses, for each of four stretches of cells, the price of the stretch for every
            // cell the field then holds above its `from`, up to its `to`. The stretches may lie
            // over one another, so the price of a cell can rise step by step towards a full
            // field (short attacks are minded from the middle game on, and more and
            // more towards the end).
            if (cost > 0) {
                const int cells = mineAfter.field.f.count() + mineAfter.incoming.total();
                const int stretches[4][3] = {{p.x3CrowdFrom, p.x3CrowdTo, p.x3Crowd}, {p.x3CrowdFrom2, p.x3CrowdTo2, p.x3Crowd2},
                                             {p.x3CrowdFrom3, p.x3CrowdTo3, p.x3Crowd3}, {p.x3CrowdFrom4, p.x3CrowdTo4, p.x3Crowd4}};
                for (const int* s4 : stretches) {
                    const int n = (cells < s4[1] ? cells : s4[1]) - s4[0];
                    if (s4[2] > 0 && n > 0) score -= p.x3CrowdCurve && base.curve && cost < W ? int64_t(s4[2]) * n * cost / W : int64_t(s4[2]) * n; // (x3CrowdCurve: a piece or two are not a row)
                }
            }
            return true;
        };
        if (arriving > 0 && p.x3AnswerFire > 0 && o.self().field.f.count() >= p.x3AnswerFire) {
            // x3AnswerFire. Attacked on a field that is nearly full: the answer that keeps the main chain is
            // one more pair on that field, and the next attack, or a pair of the wrong colours, closes it.
            // (Seen: a chain of fifteen that the pair in hand fired, on 66 to 71 cells with four garbage
            // pieces on their way, kept through four moves for small clears; then no pair fired it.)
            int most = 0;
            const Plan* now = nullptr;
            for (const Plan& s : mine.plans) {
                if (!s.fireDepth || !(legal >> s.id[0] & 1)) continue;
                if (s.points > most) most = s.points;
                if (s.fireDepth == 1 && (!now || s.points > now->points)) now = &s;
            }
            if (now && most >= p.x3ChokeMin * rate && int64_t(now->points) * 100 >= int64_t(most) * p.x3ChokeKeep && heHoldsLittle(o, rate, now->points)) {
                const Duel d = playOut(o, mine, now, theirs, nullptr, p, seed);
                const DuelPlayer& after = d.player[me];
                if (after.phase != DuelPhase::Dead && after.fallen + after.incoming.total() == 0) return kind = Fire, now->id[0];
            }
        }
        if (arriving > 0) {
            // Answer. The plans are played with the opponent's chain going on and no more from him.
            const Plan* best = nullptr;
            int64_t bestScore = 0;
            int bestCost = 0;
            int builds[PLACEMENTS]; // what falls after the plan that builds, by first placement; -1 = no such plan
            int64_t buildScore[PLACEMENTS]{}; // ... and what that plan scores as an answer
            int buildMain[PLACEMENTS]{};      // ... and the main chain it keeps
            for (int& c : builds) c = -1;
            const AnswerBase base = answerBase();
            Rival answering; // (x3AnswerOutlast: the search of his field, made once)
            for (const Plan& s : mine.plans) {
                const Duel d = playOut(o, mine, &s, theirs, nullptr, p, seed);
                int cost = 0, main = 0;
                int64_t score = 0;
                bool lives = answerWorth(s, d, base, cost, main, score);
                if (p.x3FallTries > 1 && d.player[me].fallen % W != 0) {
                    // Less than a row of garbage (or what is left over of full rows) falls on columns drawn by chance,
                    // and the play-out above drew them once: whether the main chain is buried, or the field closed,
                    // hangs on that draw and changes from move to move. The plan is played out with other draws too;
                    // its score and the main chain it keeps are the average of the draws it survives, and each draw
                    // that closes the field takes x3Close / x3FallTries off (on a third column eleven high the
                    // chance of that is in the score already).
                    int64_t scores = lives ? score : 0, mains = lives ? main : 0;
                    int alive = lives ? 1 : 0;
                    for (int k = 1; k < p.x3FallTries; ++k) {
                        const Duel other = playOut(o, mine, &s, theirs, nullptr, p, seed + uint64_t(k) * 7919);
                        int c = 0, m = 0;
                        int64_t sc = 0;
                        if (!answerWorth(s, other, base, c, m, sc)) continue;
                        if (!alive) cost = c;
                        scores += sc, mains += m, ++alive;
                    }
                    lives = alive > 0;
                    if (lives) {
                        score = scores / alive, main = int(mains / alive);
                        if (!base.tight) score -= int64_t(p.x3Close) * (p.x3FallTries - alive) / p.x3FallTries;
                    }
                }
                if (!lives) continue;
                if (p.x3AnswerOutlast && s.fireDepth && base.mainKeyed > 0 && int64_t(main) * 100 < int64_t(base.mainKeyed) * p.x3KeepMain) {
                    // The main chain is spent on this answer. What it leaves above him is a gain only when he
                    // cannot answer it: else he fires second, with the chain he builds on while this one runs.
                    const int surplus = d.player[1 - me].incoming.total() - o.opponent().incoming.total();
                    if (surplus > 0 && !outlasts(o, s.id[0], rate, seed, answering)) score -= int64_t(p.x3Surplus) * surplus;
                }
                if (!s.fireDepth) builds[s.id[0]] = cost, buildScore[s.id[0]] = score, buildMain[s.id[0]] = main;
                if (!best || score > bestScore) best = &s, bestScore = score, bestCost = cost; // equal: the first found
            }
            // (x3ExtendRoom: on a field that full, an answer in hand that lets nothing fall is fired, not built on:
            // there is no room for the moves his chain leaves.)
            const bool noRoom = p.x3ExtendRoom > 0 && best && best->fireDepth && bestCost == 0 && o.self().field.f.count() >= p.x3ExtendRoom;
            if (best && (bestCost > 0 || p.x3Extend) && chainRuns && !noRoom) {
                // While his chain runs there may be moves left: build on, towards a
                // chain that is ready in time (the one who fires second fires the larger chain).
                const int movesLeft = p.movesIn(afterChain.frame - o.frame, o.self().field.f.count(), handWait);
                if (movesLeft > mine.pairCount) {
                    const BuildResult b = searchBuild(o, movesLeft - 1, 75, p.x3ExtendJoined != 0); // (when this search gives no placement, another one follows)
                    // The search of the moves that are left counts on its queues: it gives up a
                    // main chain that stands for one it finds with pairs that are not on the screen.
                    // (Seen: with a chain of fifteen on a field of 74 cells that none of the pairs on
                    // the screen fired, and seventeen seconds to wait for its colour, two placements
                    // cleared parts of it; the answer was a chain of five.) With x3ExtendKeep the
                    // placement is taken among those whose building plan keeps that many percent
                    // of the largest main chain any of them keeps.
                    unsigned keeping = legal;
                    if (p.x3ExtendKeep > 0) {
                        int most = 0;
                        for (int i = 0; i < PLACEMENTS; ++i)
                            if (builds[i] >= 0 && buildMain[i] > most) most = buildMain[i];
                        unsigned kept = 0;
                        for (int i = 0; i < PLACEMENTS; ++i)
                            if (builds[i] >= 0 && int64_t(buildMain[i]) * 100 >= int64_t(most) * p.x3ExtendKeep) kept |= 1u << i;
                        if (most >= p.x3ExtendKeepMin * rate && (legal & kept)) keeping = legal & kept;
                    }
                    // x3Enough. The sums are the mean of what the queues fire in time, and a chain is not worth what
                    // it has beyond the garbage on its way: first the placements below which the most queues fire a
                    // chain worth that share of it, and the sums among those.
                    if (p.x3Enough > 0) keeping = enoughChoices(b, keeping, int64_t(arriving) * rate * p.x3Enough / 100);
                    const int id = pickBuild(o, b, keeping);
                    // (When no chain that offsets what is coming is found in time, the choice of
                    // that short search means nothing: the answer below is taken instead.)
                    const int queues = p.queues();
                    const bool inTime = !p.x3ExtendCheck || (id >= 0 && b.sum[id] / queues >= int64_t(arriving) * rate);
                    if (id >= 0 && inTime) return kind = AnswerBuild, id;
                }
            }
            if (best && !best->fireDepth && p.x3AnswerShape) {
                // Nothing is fired: where the pair goes is a question of building. The beam
                // search chooses among the placements that let as little fall; with x3ShapeSlack,
                // among those whose building plan scores about as well as the best answer. (The
                // same number of garbage pieces buries the main chain after one placement and not
                // after another, and the beam search knows nothing of the garbage. Seen: it chose
                // a placement that scored 10,500 less than the best, and the round was over.)
                const unsigned same = shapeChoices(builds, buildScore, bestCost, bestScore, p.x3ShapeSlack);
                const int id = build(o, legal & same, 0);
                if (id >= 0) return kind = Answer, id;
            }
            if (best) return kind = Answer, best->id[0];
        }

        // An all clear within reach of the pairs on the screen, by a chain of x3AllClear waves or
        // fewer (with a chain of two or fewer it is never missed, three depends, four or
        // more spent on it is a bad move). Taken now when this pair gets it; else the placement
        // that leads to it.
        if (p.x3AllClear > 0 && arriving == 0) {
            const int cells = o.self().field.f.count();
            const Plan* clear = nullptr;
            for (const Plan& s : mine.plans) {
                if (!s.allClear || s.chains > p.x3AllClear || !(legal >> s.id[0] & 1)) continue;
                if (s.chains >= 3 && cells + 2 * s.fireDepth > p.x3ClearThree) continue; // too late for so small a chain
                if (!clear || s.fireDepth < clear->fireDepth || (s.fireDepth == clear->fireDepth && s.points > clear->points)) clear = &s;
            }
            if (clear) return kind = Clear, clear->id[0];
        }

        // Poke: a chain that leaves the main chain whole and gains against his best answer.
        // (x3PokeFrom: no poke yet on a field of fewer cells. Nothing of what follows is needed then - the own field and
        // the position valued, his field searched - unless x3PokeAt may fire one whatever the own field holds.)
        const bool pokeEarly = p.x3PokeFrom > 0 && o.self().field.f.count() < p.x3PokeFrom && !(p.x3PokeAt > 0 && o.opponent().field.f.count() >= p.x3PokeAt);
        if (p.x3Poke && arriving == 0 && !pokeEarly) {
            // The three chains this part compares, named by what they count: mainKeyed and hisKeyed are the main chains
            // as buildValue sees them, a key piece that is still missing counted in; hisDirect is what he can fire as
            // his field stands - a trial drop without a key piece, or one of his pairs on the screen.
            int mainKeyed = 0;
            buildValue(o.self().field, o.rules, o.timing, p, &mainKeyed);
            const int now = positionValue(o.self(), o.opponent(), o.rules, o.timing, p, rate, seed);
            if (p.x3PokeAt > 0 && o.opponent().field.f.count() >= p.x3PokeAt) {
                // x3PokeAt: his field is nearly full. Whatever he could answer with, he has no room to take it.
                const Plan* poke = nullptr;
                for (const Plan& s : mine.plans) {
                    if (s.fireDepth != 1 || s.sent < p.x3MinPoke || int64_t(s.mainAfter) * 100 < int64_t(mainKeyed) * p.x3PokeKeep || !(legal >> s.id[0] & 1)) continue;
                    if (!poke || s.sent > poke->sent) poke = &s;
                }
                if (poke) return kind = Poke, poke->id[0];
            }
            const Plan* best = nullptr;
            int bestGain = 0;
            // What the opponent can fire at a poke: the largest chain a trial drop starts on his field, and how long it runs.
            int hisDirect = 0, hisFrames = 0;
            if (p.x3PokeRead || p.x3PokeUnanswered || p.x3PokeNet == 3) {
                Trials trials;
                detect(o.opponent().field, o.rules, o.timing, p.trialMaxAdd, trials);
                for (int i = 0; i < trials.count; ++i)
                    if (trials.item[i].chains >= o.rules.chainRequirement && trials.item[i].points > hisDirect)
                        hisDirect = trials.item[i].points, hisFrames = trials.item[i].frames;
            }
            // Near a full field one mostly pokes; with anything that does not
            // break much of the main chain; and normally not when the opponent holds an answer of
            // the same size, unless one is about to choke. So, with x3PokeUnanswered: a chain fired
            // now that sends x3MinPoke or more and leaves x3PokeKeep percent of the main chain is a
            // poke when no plan of his answers it (nothing of it left above his field) while
            // keeping that much of his own main chain; from x3PokeFull cells on the own field that
            // is not asked. What he may fire back with his main chain is read as with x3PokeRead.
            int hisKeyed = 0;
            int ownCells = 0;
            if (p.x3PokeUnanswered || p.x3PokeNet == 3) {
                buildValue(o.opponent().field, o.rules, o.timing, p, &hisKeyed);
                ownCells = o.self().field.f.count();
                for (const Plan& x : theirs.plans) // (his main chain as his pairs can fire it, when a trial drop does not show it)
                    if (x.fireDepth && int64_t(x.sent) * rate > hisDirect) hisDirect = x.sent * rate, hisFrames = int(x.endFrame - x.lockFrame);
            }
            // He fires his main chain at a poke: while it runs I build on (the pairs after this one, then the
            // queues of the beam search) and fire second. The poke stands when that chain and the poke
            // together offset his. (For x3PokeUnanswered, x3PokeRead and x3PokeNet 3.)
            const auto offsetsHisMain = [&](const Plan& s) {
                const Duel d = playOut(o, mine, &s, theirs, nullptr, p, seed);
                if (d.player[me].phase == DuelPhase::Dead) return false;
                Params q = p;
                const int moves = p.movesIn(hisFrames, d.player[me].field.f.count(), handWait);
                q.beamDepth = moves < 1 ? 1 : moves < p.beamDepth ? moves : p.beamDepth;
                int64_t mineThen = s.mainAfter;
                if (mine.pairCount > 1) {
                    const BuildResult b = search(builder, d.player[me].field, mine.pair + 1, mine.pairCount - 1, o, q, seed, 75, p.thinkFit >= 1);
                    const int queues = p.queues();
                    for (int id = 0; id < PLACEMENTS; ++id)
                        if ((b.alive >> id & 1) && b.sum[id] / queues > mineThen) mineThen = b.sum[id] / queues;
                }
                return mineThen + int64_t(s.sent) * rate >= hisDirect;
            };
            // x3PokeNet: V after reply r of his without the poke. 1: X3 places nothing (as it places nothing
            // after the poke); 2: the best V any of its plans reaches against that reply. Counted once a poke asks.
            std::vector<int> spared;
            const auto without = [&](int r) {
                if (spared.empty()) spared.assign(size_t(replies), INT32_MIN);
                int& v = spared[size_t(r)];
                if (v != INT32_MIN) return v;
                if (p.x3PokeNet == 1) {
                    const Duel d = playOut(o, mine, nullptr, theirs, reply(r), p, seed);
                    return v = positionValue(d.player[me], d.player[1 - me], o.rules, o.timing, p, rate, seed);
                }
                for (const Plan& a : mine.plans) {
                    const Duel d = playOut(o, mine, &a, theirs, reply(r), p, seed);
                    const int w = positionValue(d.player[me], d.player[1 - me], o.rules, o.timing, p, rate, seed);
                    if (w > v) v = w;
                }
                return v;
            };
            // xWildChance: his answer with a pair that is not on the screen, once with his next pair holding
            // it and once with no such pair.
            const bool chance = p.xWildChance && p.xWildEvery > 1;
            Params lucky = p, unlucky = p;
            lucky.xWildEvery = 1, unlucky.xWildEvery = 0;
            // x3PokeFrom, x3PokeMainFrom: no poke yet on a field of fewer cells, or with a smaller main chain.
            const bool tooEarly = (p.x3PokeFrom > 0 && o.self().field.f.count() < p.x3PokeFrom) || (p.x3PokeMainFrom > 0 && mainKeyed < p.x3PokeMainFrom);
            for (const Plan& s : mine.plans) {
                if (tooEarly) break;
                if (p.x3PokeMost > 0 && s.sent > p.x3PokeMost) continue; // (a larger chain is no poke)
                if (p.x3PokeUnanswered) {
                    if (s.fireDepth != 1 || s.sent < p.x3MinPoke || int64_t(s.mainAfter) * 100 < int64_t(mainKeyed) * p.x3PokeKeep) continue;
                    if (!(legal >> s.id[0] & 1) || (best && s.sent <= best->sent)) continue;
                    bool answered = false;
                    if (ownCells < p.x3PokeFull)
                        for (const Plan& x : theirs.plans) {
                            if (!x.fireDepth || int64_t(x.mainAfter) * 100 < int64_t(hisKeyed) * p.x3PokeKeep) continue;
                            const Duel d = playOut(o, mine, &s, theirs, &x, p, seed);
                            const DuelPlayer& he = d.player[1 - me];
                            if (he.phase != DuelPhase::Dead && he.fallen + he.incoming.total() == 0) {
                                answered = true;
                                break;
                            }
                        }
                    if (answered) continue;
                    if (offsetsHisMain(s)) best = &s; // (he fires his main chain at it)
                    continue;
                }
                if (s.fireDepth != 1 || s.sent < p.x3MinPoke || int64_t(s.mainAfter) * 100 < int64_t(mainKeyed) * p.x3KeepMain) continue;
                if (p.x3PokeRead) {
                    if (!(legal >> s.id[0] & 1) || (best && s.sent <= best->sent)) continue;
                    if (offsetsHisMain(s)) best = &s;
                    continue;
                }
                int gain = 0, covered = -1;
                bool counted = false;
                // The gain against reply r, his pairs that are not on the screen read as `q` has them. False: the
                // reply does not count against the poke.
                const auto against = [&](const Params& q, int r, int& g) {
                    const Duel d = playOut(o, mine, &s, theirs, reply(r), q, seed);
                    const int v = positionValue(d.player[me], d.player[1 - me], o.rules, o.timing, p, rate, seed);
                    if (p.x3PokeNet == 3) {
                        // He gives up his main chain to fire at the poke, or the reading (in which X3 places
                        // nothing after the poke) ends lost: when X3, building on while his chain runs and
                        // firing second, offsets it, that reply does not count against the poke.
                        const Plan* x = reply(r);
                        if ((x && x->fireDepth && int64_t(x->mainAfter) * 100 < int64_t(hisKeyed) * p.x3PokeKeep) || v <= VALUE_LOST / 2) {
                            if (covered < 0) covered = offsetsHisMain(s) ? 1 : 0;
                            if (covered) return false;
                        }
                    }
                    g = v - (p.x3PokeNet == 1 || p.x3PokeNet == 2 ? without(r) : now); // (1, 2: against the same reply without the poke)
                    return true;
                };
                for (int r = 0; r < replies; ++r) {
                    int g = 0;
                    if (!chance) {
                        if (!against(p, r, g)) continue;
                    } else {
                        // (a reading that does not count is level: 0)
                        int holds = 0, holdsNot = 0;
                        const bool a = against(lucky, r, holds), b = against(unlucky, r, holdsNot);
                        if (!a && !b) continue;
                        g = int((int64_t(a ? holds : 0) + int64_t(b ? holdsNot : 0) * (p.xWildEvery - 1)) / p.xWildEvery);
                    }
                    if (!counted || g < gain) gain = g, counted = true;
                    if (gain < p.x3PokeGain) break; // already not worth it
                }
                if (gain >= p.x3PokeGain && (!best || gain > bestGain)) best = &s, bestGain = gain;
            }
            if (best && (legal >> best->id[0] & 1)) return kind = Poke, best->id[0];
        }

        // Near a full field the pairs to come cannot be waited for. The beam search counts on
        // them: its fixed queues bring every colour within two pairs, and a queue in which the
        // field closes costs nothing. (Seen: at 64 cells a chain of twelve could be fired with the
        // pair in hand; the placement chosen gave it up for a larger one that needed two more
        // greens, with room for three pairs, and the field closed.) With x3ChokeFrom, from that
        // many cells on: the largest chain the pairs on the screen can fire is fired when this
        // pair fires it (or nearly: x3ChokeKeep percent); when a later pair on the screen fires
        // a larger one, the placement is chosen among those that keep that.
        unsigned open = legal;
        if (p.x3ChokeFrom > 0 && arriving == 0 && o.self().field.f.count() >= p.x3ChokeFrom) {
            int most = 0, later = 0, under[PLACEMENTS]{};
            const Plan* now = nullptr;
            for (const Plan& s : mine.plans) {
                if (!s.fireDepth || !(legal >> s.id[0] & 1)) continue;
                if (s.points > under[s.id[0]]) under[s.id[0]] = s.points;
                if (s.points > most) most = s.points;
                if (s.fireDepth == 1 && (!now || s.points > now->points)) now = &s;
                if (s.fireDepth > 1 && s.points > later) later = s.points;
            }
            // (x3ChokeHis: against an opponent who holds much the whole rule is left out. Narrowing the
            // placements alone would fire the chain too: the placement that fires it now keeps it.)
            if (most >= p.x3ChokeMin * rate && heHoldsLittle(o, rate, most)) {
                // (x3ChokeLast: a later pair on the screen fires it too: no need to fire first yet)
                const bool wait = p.x3ChokeLast && int64_t(later) * 100 >= int64_t(most) * p.x3ChokeKeep;
                if (!wait && now && int64_t(now->points) * 100 >= int64_t(most) * p.x3ChokeKeep) return kind = Fire, now->id[0];
                unsigned keep = 0;
                for (int id = 0; id < PLACEMENTS; ++id)
                    if ((legal >> id & 1) && int64_t(under[id]) * 100 >= int64_t(most) * p.x3ChokeKeep) keep |= 1u << id;
                if (keep) open = keep;
            }
        }
        // x3Guard. Nothing is on its way, but the opponent holds an attack: a chain his next pair fires
        // that leaves x3Guard garbage pieces or more above this field. The beam search, which knows
        // nothing of him, then chooses among the placements whose plans answer that chain about as
        // well as the best of them. (Seen in a game of the professional: he held a chain of four on
        // 20 cells, 33 pieces. X3, building, put its pair where the chain of three its next pair
        // would have fired was gone; he fired, and 30 of the 33 fell on 24 cells.) Only the
        // placements that fire nothing are scored: an answer is kept, not fired at a threat. Whether
        // the pair builds at all is the beam search's to say (below): when it would fire now, the
        // guard narrows nothing.
        unsigned guarded = 0, firesNow = 0;
        if (p.x3Guard > 0 && arriving == 0) {
            const Plan* threat = nullptr;
            for (const Plan& t : theirs.plans) {
                if (!t.fireDepth || t.fireDepth > p.x3GuardPairs || t.sent < p.x3Guard || (p.x3GuardMost > 0 && t.sent > p.x3GuardMost)) continue;
                if (!threat || t.sent > threat->sent) threat = &t;
            }
            if (threat) {
                const AnswerBase base = answerBase();
                int64_t worth[PLACEMENTS]{}, most = 0;
                unsigned answered = 0;
                for (const Plan& s : mine.plans) {
                    if (!(open >> s.id[0] & 1)) continue;
                    if (s.fireDepth == 1) {
                        firesNow |= 1u << s.id[0];
                        continue;
                    }
                    const Duel d = playOut(o, mine, &s, theirs, threat, p, seed);
                    int cost = 0, main = 0;
                    int64_t score = 0;
                    if (!answerWorth(s, d, base, cost, main, score)) continue;
                    if (!(answered >> s.id[0] & 1) || score > worth[s.id[0]]) worth[s.id[0]] = score;
                    if (!answered || score > most) most = score;
                    answered |= 1u << s.id[0];
                }
                for (int id = 0; id < PLACEMENTS; ++id)
                    if ((answered >> id & 1) && worth[id] >= most - p.x3GuardSlack) guarded |= 1u << id;
            }
        }
        const BuildResult b = searchBuild(o, 0);
        if (p.x3FullPercent > 0 && arriving == 0) {
            // Fire: the field is full when the beam search finds little more than what can be fired now.
            const Plan* big = nullptr;
            unsigned fires = 0;
            for (const Plan& s : mine.plans) {
                if (s.fireDepth != 1) continue;
                fires |= 1u << s.id[0];
                if ((legal >> s.id[0] & 1) && (!big || s.points > big->points)) big = &s;
            }
            const int queues = p.queues();
            int64_t future = 0;
            for (int id = 0; id < PLACEMENTS; ++id)
                if ((b.alive >> id & 1) && !(fires >> id & 1) && b.sum[id] / queues > future) future = b.sum[id] / queues;
            if (big && int64_t(big->points) * 100 >= future * p.x3FullPercent) return kind = Fire, big->id[0];
        }
        if (guarded) {
            // (x3Guard: the best the beam search finds below a placement that fires now, and below one that fires nothing)
            int64_t fire = -1, build = -1;
            for (int i = 0; i < PLACEMENTS; ++i) {
                if (!(open >> i & 1) || !(b.alive >> i & 1)) continue;
                int64_t& best = firesNow >> i & 1 ? fire : build;
                if (b.sum[i] > best) best = b.sum[i];
            }
            if (build >= fire && guarded != (open & ~firesNow)) {
                // (x3GuardNear: only among the guarded placements whose sum is nearly that of the best that builds)
                const unsigned nearby = guardChoices(b, guarded, build, p.x3GuardNear);
                if (nearby) open = nearby, guardedNow = true;
            }
        }
        if (p.x3BuildHold && arriving == 0) {
            // The beam search takes the placement below which it found the largest chain, and on a field that is
            // nearly full that is the chain in hand: the placement that fires it now. The reading of a "win" has
            // said no to it above (he answers it), and here it was fired all the same. With x3BuildHold a placement
            // that fires the main chain (x3ChokeKeep percent of the largest chain the pairs on the screen fire,
            // worth x3ChokeMin garbage or more) is left out while another placement lives.
            int most = 0;
            for (const Plan& s : mine.plans)
                if (s.fireDepth && s.points > most) most = s.points;
            unsigned fires = 0;
            if (most >= p.x3ChokeMin * rate)
                for (const Plan& s : mine.plans)
                    if (s.fireDepth == 1 && int64_t(s.points) * 100 >= int64_t(most) * p.x3ChokeKeep) fires |= 1u << s.id[0];
            if ((open & fires) && (open & ~fires)) {
                const int unfired = pickBuild(o, b, open & ~fires, true);
                if (unfired >= 0) return kind = Build, unfired;
            }
        }
        int id = pickBuild(o, b, open, true);
        if (id < 0 && open != legal) id = pickBuild(o, b, legal, true);
        if (id >= 0) return kind = Build, id;
        return mine.plans[0].id[0];
    }

};

} // namespace juraic::ai
