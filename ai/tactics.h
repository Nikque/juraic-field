#pragma once
#ifdef JURAIC_DECISION_METRICS
#include "decision_trace.h"
#endif
#include "decision_metrics.h"
#include "win_fire.h"
#include "ready_build.h"
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
// Garbage that falls is weighed heavily, even one row. A small attack is taken
// only when a concrete counter is available after each possible drop.
#include "exchange.h"
#include "pace.h"
#include "answer.h"
#include "midgame.h"
#include "preparation.h"
#include "receive.h"
#include "setup_speed.h"
#include "setup_answer.h"
#include "setup_room.h"
#include "known_poke.h"
#include "receive_compare.h"
#include "answer_window.h"
#include "partial_build.h"
#include "answer_commitment.h"
#include "side_response.h"
#include "lv38_response.h"
#include "lv38_lifeline.h"
#include "lv38_threat.h"
#include "lv38_foundation.h"
#include "lv38_ready_cover.h"
#include "lv38_two_wave.h"
#include "lv38_tail_build.h"
#include "lv38_tail_answer.h"
#include "lv38_cut_build.h"
#include "lv38_answer_build.h"
#include "lv38_fire_or_lose.h"

namespace juraic::ai {

// The tactics of X3: which placement, given the plans of both players and the searches. When a choice is made, how
// long it may take and what the opponent is seen as is X3Pace's (pace.h).
class X3Agent : public X3Pace {
#ifdef JURAIC_DECISION_METRICS
public:
    bool recordDecisionMetrics = false;
private:
#endif
    template<metrics::Part part, class Action>
    static decltype(auto) measurePart(Action action) {
        metrics::Scope scope(part);
        return action();
    }

public:
    explicit X3Agent(const Params& params = Params{}) : X3Pace(params) {}
    bool namedLv38Ex = false;
    // (a thread of X3Pace may be inside decide(): it must have ended before the members below go)
    ~X3Agent() override {
        finish();
        dropEarly();
        dropRival();
    }
    void startRound(int seat, uint64_t seed) override {
        X3Pace::startRound(seat, seed);
        held.depth = 0;
        sideReady = {};
        twoWaveTurn = -1;
        twoWaveCarry = {};
        twoWaveNext = {};
        twoWaveNote = {};
        twoWaveFrame = -1;
        twoWaveRetained = false;
        knownPokeCalls = 0;
    }
    uint8_t frame(const Observation& o) override {
        watchRival(o);
        return X3Pace::frame(o);
    }

    // The last crush chosen (x3Crush), for tools: the garbage it sends, the least left above the opponent after his
    // replies that keep his main chain, and the two main chains (points) as they stood. It plays no part in a choice.
    struct CrushNote {
        int sent = 0, left = 0, mine = 0, his = 0;
        int id = -1, chains = 0; // the placement and the waves of the chain, as read
    } crushNote;
    // Choices in which the crush was looked for, by how far it got: no chain of the pairs it may use (x3CrushPairs) sends
    // x3Crush or more; every such chain spends the own main chain; he answers every one that keeps it (or a reading
    // closes the own field); he fires a larger chain at every one that passed and it is not offset; fired; a placement
    // made to build one that a later pair fires. For tools; no part in a choice.
    struct CrushCounts {
        int64_t asked = 0, none = 0, spends = 0, answered = 0, outlasted = 0, fired = 0, built = 0;
    } crushCounts;
    // The last exchange poke chosen (Params::pokeExchange), for tools: the frame of the choice and the moves made before
    // it, the chain (the pair that fires it, waves, garbage sent), his main chain as he can fire it (points, frames), the
    // own moves and main chain reckoned while his runs, the chance of a small answer read from his field, and the cells
    // of both fields. chosen: the last choice made was this one. It plays no part in a choice.
    struct ExchangeNote {
        bool chosen = false;
        int64_t frame = 0;
        int moves = 0, id = -1, depth = 0, chains = 0, sent = 0;
        int64_t his = 0, hisFrames = 0, mineThen = 0, chance = 0;
        int ownMoves = 0, hisCells = 0, ownCells = 0;
    } exchangeNote;
    lv38_tail_build::Note lv38BuildNote;
    lv38_tail_answer::Note lv38TailAnswerNote;
    lv38_answer_build::Note lv38AnswerBuildNote;
    int lv38ResponseNewlyEligible = 0;
    lv38_two_wave::Note twoWaveNote;
    int64_t twoWaveFrame = -1;
    bool twoWaveRetained = false;
    // Optional recorder for the untimed, one-round measurement tool.
    std::vector<lv38_fire_or_lose::Decision>* fireOrLoseLog = nullptr;
    struct TakeSmallNote {
        int64_t frame;
        int move, phase, ordinary, selected, incoming, colours, turns, expected;
    };
    std::vector<TakeSmallNote>* takeSmallLog = nullptr;
    struct CutRankRecord {
        int64_t frame;
        int move;
        int ordinary, selected, depth, queues;
        bool preserved;
    };
    struct CutAnswerRecord {
        int64_t frame;
        int move;
        lv38_tail_answer::Note note;
    };
    struct CutDecisionRecord {
        int64_t frame, nanoseconds;
        int move;
        int colours;
        bool quiet;
        GameField field;
    };
    std::vector<CutRankRecord>* cutRanks = nullptr;
    std::vector<CutAnswerRecord>* cutAnswers = nullptr;
    std::vector<CutDecisionRecord>* cutDecisions = nullptr;
    struct PartNote {
        int64_t frame;
        int move, phase, incoming, arriving, colours;
        const char* part;
        int before, after, kind, waves;
    };
    std::vector<PartNote>* partLog = nullptr;
    std::vector<PartNote>* lv37PartLog = nullptr;
    std::vector<PartNote>* legacyPokeLog = nullptr;
    int knownPokeCalls = 0;
    // Choices in which the exchange poke was looked for (asked) and why candidates were left: no candidate; the chain
    // does not land as it stands (or the reading ends before it is confirmed); a plan of his on the screen offsets it
    // small or sends garbage back; the chance of a small answer from his field; his main chain is not outdone (or the
    // search was stopped short); he can take it; out of time. fired: chosen with the pair in hand; built: chosen as the
    // first placement of a plan whose later pair fires. For tools; no part in a choice.
    struct ExchangeCounts {
        int64_t asked = 0, none = 0, lands = 0, smallReply = 0, chance = 0, outdone = 0, taken = 0, late = 0, fired = 0, built = 0;
    } exchangeCounts;

protected:
    struct TailInput {
        int hold = 1;
        int held[2]{};
        bool spare = false, waitQuick = false, downAtTarget = false, uncertain = false;

        bool supported() const {
            return !uncertain && ((hold == 1 && !spare) || (hold > 1 && spare));
        }

        bool canCompare() const { return !uncertain && (hold == 1 || (hold > 1 && spare)); }

        void configure(std::optional<PictureRoutes>& routes) const {
            if (hold > 1 && spare) routes.emplace(hold, held, spare, waitQuick, downAtTarget);
        }

        template<class Stop>
        unsigned reachable(const Observation& o, unsigned choices, Stop stop) const {
            if (!supported()) return 0;
            if (!spare) return choices;
            InputPlanner planner;
            planner.tapHold = hold;
            planner.heldFor[0] = held[0], planner.heldFor[1] = held[1];
            planner.spareFrame = spare, planner.waitQuickLock = waitQuick, planner.downWhenPlaced = downAtTarget;
            const PairControl* air = ownPairInAir(o);
            const PairControl from = air ? *air : spawnPair(o.timing);
            unsigned reached = 0;
            for (unsigned ids = choices; ids; ids &= ids - 1) {
                if (stop()) return 0;
                const int id = std::countr_zero(ids);
                const Placement target = placement(id);
                InputPath path;
                if (planner.planSure(o.self().field, from, target, o.timing, o.rules.speedPercent, path) &&
                    !planner.lostBySkip(o.self().field, from, target, o.timing, o.rules.speedPercent, path))
                    reached |= 1u << id;
            }
            return reached;
        }
    } tailInput;

    void choiceBegins() override {
        X3Pace::choiceBegins();
        // Capture the driver's input state before the search starts on its own thread.
        tailInput = {tapHold, {tapsHeldFor[0], tapsHeldFor[1]}, spareFrame, waitQuickLock, downWhenPlaced,
                     keepLow || unsure13 || unsure14};
    }

    int choose(const Observation& o, Pair pair, unsigned legal) override {
#ifdef JURAIC_DECISION_METRICS
        if (recordDecisionMetrics) {
            const auto& files = decision_trace::files();
            if (!files.profile.empty() || !files.positions.empty())
                return chooseRecorded(o, pair, legal, !files.profile.empty(), !files.positions.empty());
        }
#endif
        if (!cutDecisions) return X3Pace::choose(o, pair, legal);
        const auto started = Builder::Clock::now();
        const int selected = X3Pace::choose(o, pair, legal);
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Builder::Clock::now() - started).count();
        cutDecisions->push_back({o.frame, ns, o.self().moves + 1, normalColours(o), lv38_cut_build::quiet(o, p), o.self().field});
        return selected;
    }

    static int normalColours(const Observation& o) {
        int colours = o.self().field.f.normal().count();
        for (Cell cell : o.self().field.row14) colours += cell >= 1 && cell <= MAX_COLORS;
        return colours;
    }

#ifdef JURAIC_DECISION_METRICS
    decision_position::Context decisionContext() const {
        using namespace decision_position;
        Context c;
        const auto snapshotCarry = [](const TwoWaveCarry& v) { return Carry{v.moves, v.field, v.pairs, v.suffix}; };
        c.roundSeed = roundSeed;
        c.random = choosingAgain ? drawn : rng;
        c.choosingAgain = choosingAgain;
        c.setupPicked = setupPicked;
        c.namedLv38Ex = namedLv38Ex;
        c.lastChoice = lastChoice;
        c.firstWidth = firstWidth;
        c.unitNs = unitNs;
        c.slowNs = slowNs;
        c.plansNs = plansNs;
        c.againBudget = againBudget;
        c.input = {tailInput.hold, {tailInput.held[0], tailInput.held[1]}, tailInput.spare,
                   tailInput.waitQuick, tailInput.downAtTarget, tailInput.uncertain};
        c.twoWaveTurn = twoWaveTurn;
        c.carry = snapshotCarry(twoWaveCarry);
        c.next = snapshotCarry(twoWaveNext);
        c.sideReady = sideReady;
        c.held.depth = held.depth;
        if (held.depth) {
            c.held.field = held.field;
            std::copy_n(held.pair, held.pairs, c.held.pair);
            c.held.pairs = held.pairs;
            c.held.seed = held.seed;
            c.held.built = held.built;
        }
        return c;
    }

    int chooseRecorded(const Observation& o, Pair pair, unsigned legal, bool profile, bool savePosition) {
        std::optional<decision_position::Record> snapshot;
        const bool localNote = savePosition && !noting;
        const auto noteCount = notes.size();
        if (savePosition) {
            snapshot.emplace();
            snapshot->observation = o;
            snapshot->pair = pair;
            snapshot->legal = legal;
            snapshot->context = decisionContext();
            noting = true;
        }
        const int64_t previousShortPlans = shortPlans, previousCuts = cutSearches;
        metrics::Decision decision(profile, profile && p.thinkBudget <= 0);
        const auto started = Builder::Clock::now();
        const int selected = X3Pace::choose(o, pair, legal);
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Builder::Clock::now() - started).count();
        if (profile) finishEvalRepeats();
        if (snapshot) {
            snapshot->selected = selected;
            snapshot->kind = kind;
            if (notes.size() > noteCount) {
                const auto& last = notes.back();
                snapshot->trace = {last.pairs, last.cut, last.widths};
                if (localNote) notes.pop_back();
            }
            if (localNote) noting = false;
        }
        const bool quiet = lv38_cut_build::quiet(o, p);
        const int colours = normalColours(o);
        const bool twoWaveIncomplete = twoWaveFrame == o.frame && twoWaveNote.status == 4;
        decision_trace::append({roundSeed, o.me, o.frame, o.self().moves + 1, choosingAgain, selected,
                                kind, int(o.self().phase), legal, quiet, colours, ns,
                                shortPlans - previousShortPlans, cutSearches - previousCuts, twoWaveIncomplete},
                               snapshot ? &*snapshot : nullptr);
        if (cutDecisions) cutDecisions->push_back({o.frame, ns, o.self().moves + 1, colours, quiet, o.self().field});
        return selected;
    }
#endif

    struct TwoWaveCarry {
        int moves = -1;
        GameField field;
        std::array<Pair, 2> pairs{};
        Plan suffix;
    } twoWaveCarry, twoWaveNext;
    int twoWaveTurn = -1;
    Gaze gaze;
    PlanSet mine, theirs;
    side_response::Ready sideReady;
    struct ContinuationSearch {
        GameField field;
        std::array<Pair, MAX_KNOWN> remaining{};
        int count, depth;
        uint64_t seed;
        BuildResult result;
    };
    std::vector<ContinuationSearch> continuationSearches;

    // The beam search for the own field. depth 0 = the usual depth.
    // percent: of the time of the choice, when it is to end (Params::thinkBudget).
    // joined: fields are valued by the chains that stand, without key pieces (x3ExtendJoined).
    BuildResult searchBuild(const Observation& o, int depth, int percent = 100, bool joined = false, bool cutBuild = false) {
        Params q = p;
        if (joined) q.evKeyed = 0;
        if (depth > 0 && depth < q.beamDepth) q.beamDepth = depth;
        // Keep the third column low, unless it is already higher (then no placement would be left).
        q.beamThirdMax = thirdLimit(o.self().field);
        return search(builder, o.self().field, mine.pair, mine.knownCount, o, q, roundSeed * 1000 + uint64_t(o.self().moves),
                      percent, p.thinkFit >= 1, cutBuild);
    }
    // The beam search of the own field `field` as the pair in hand leaves it: the pairs after it on the screen (at least
    // one), then the queues; `depth` placements deep, to end at three quarters of the time of the choice. (What can be
    // built on while a chain of his runs.)
    // (placed: the pairs on the screen that were put to get to `field` - the pair in hand alone, or the pairs of a plan)
    BuildResult searchOn(const Observation& o, const GameField& field, int depth, uint64_t seed, int placed = 1) {
        Params q = p;
        q.beamDepth = depth;
        q.beamThirdMax = thirdLimit(field); // (as searchBuild: the third column kept low, of the field searched on)
        const Pair* remaining = mine.pair + placed;
        const int count = mine.knownCount - placed;
        const bool fit = p.thinkFit >= 1;
        // The clock can change the width and cut a search. A recorded
        // decision can also consume a different width on its next search.
        if (p.thinkBudget > 0 || (fit && scripted))
            return search(builder, field, remaining, count, o, q, seed, 75, fit);
        for (const auto& previous : continuationSearches) {
            if (previous.seed != seed || previous.depth != depth || previous.count != count || previous.field != field ||
                !std::equal(remaining, remaining + count, previous.remaining.begin(), [](Pair a, Pair b) {
                    return a.axis == b.axis && a.child == b.child;
                })) continue;
            ownWidth = q.beamWidth;
            metrics::reused();
            return previous.result;
        }
        const auto result = search(builder, field, remaining, count, o, q, seed, 75, fit);
        if (!result.cut) {
            auto& saved = continuationSearches.emplace_back(ContinuationSearch{field, {}, count, depth, seed, result});
            std::copy_n(remaining, count, saved.remaining.begin());
        }
        return result;
    }
    // The largest mean over the queues of the chains a search found below a first placement that lives: within `depth`
    // placements (BuildResult::sumBy), or 0: within the depth of the search.
    int64_t meanBest(const BuildResult& b, int depth = 0) const {
        const int queues = p.queues();
        int64_t most = 0;
        for (int id = 0; id < PLACEMENTS; ++id)
            if (b.alive >> id & 1) most = std::max(most, (depth ? b.sumBy[id][depth - 1] : b.sum[id]) / queues);
        return most;
    }
    // The moves that fit into `frames` frames on a field of `cells` cells (Params::movesIn, each move `extra` frames
    // longer): one at least, `most` at most.
    int movesUpTo(int64_t frames, int cells, int extra, int most) const {
        const int moves = p.movesIn(frames, cells, extra);
        return moves < 1 ? 1 : moves < most ? moves : most;
    }
    // The placements a player has until a chain of the other side is confirmed `frames` frames from now: the moves
    // that fit before it (Params::movesIn) and the pair then in hand, which fires; `most` at most.
    int placementsUpTo(int64_t frames, int cells, int most) const {
        const int moves = frames > 0 ? p.movesIn(frames, cells, 0) : 0;
        return moves + 1 < most ? moves + 1 : most;
    }
    // The last frame a pair of the other side may appear in and still be held when the chain of `plan` is confirmed:
    // nextFrames after that confirmation at the latest, since the landing before the pair is then over before the
    // chain is and takes nothing of it. (A plan without the frame of its confirmation: the frame its next pair
    // appears in - the same frame when nothing falls on the own field after the chain.)
    int64_t heldUntil(const Observation& o, const Plan& plan) const {
        return plan.confirmFrame > 0 ? plan.confirmFrame + o.timing.nextFrames : plan.endFrame - 1;
    }
    // The placements the opponent has until an own chain confirmed in frame `confirm` reaches him, counted as outlasts
    // counts them (placementsUpTo, to nextFrames after the confirmation): `before` is the Duel of the observation played
    // to the frame he next holds a pair (the observation itself when he holds one), `cells` the cells of his field then.
    int repliesUntil(const Observation& o, const Duel& before, int64_t confirm, int cells) const {
        return placementsUpTo(confirm + o.timing.nextFrames - before.frame, cells, INT32_MAX);
    }
    // His replies in the reading of a move: standing still (0, null), then each of his plans.
    int replies() const { return int(theirs.plans.size()) + 1; }
    const Plan* reply(int r) const { return r ? &theirs.plans[size_t(r - 1)] : nullptr; }

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
        Pair pair[MAX_KNOWN]{};
        int pairs = 0;
        uint64_t seed = 0;
        BuildResult built;
    } held;
    struct Rival {
        bool prepared = false, prefetched = false, searched = false;
        bool countedSearch = false, splitTime = false;
        unsigned checkedFire = 0, counteredFire = 0; // exact response to an immediate fire, once per first placement
        bool noPair = false; // he holds no pair again in this round
        int64_t from = 0;    // the frame from which he places
        int64_t previousDrop = INT64_MAX; // earlier garbage already taken before that pair
        int need = 0;        // garbage pieces that close his field as it is then
        int deepest = 0;     // the most placements that are searched for him
        GameField field;     // his field then
        int cells = 0;       // the cells on it (a move takes him the frames of a field that full: Params::movesIn)
        int pool = 0;        // the points he carries then, not yet turned into garbage
        BuildResult built;   // the search from it, `depth` placements deep
        int depth = 0;
        bool unanswered = false; // the last outlasts: he holds no pair in time to answer the chain (no answer searched)
    };
    bool exchangeOutOfTime(int percent = 100) const {
        if (p.thinkBudget <= 0) return false;
        if (stale.load()) { metrics::add(metrics::Part::StalePoll); return true; }
        const bool expired = budgetNow > 0 && Builder::Clock::now() >= began + std::chrono::microseconds(int64_t(budgetNow) * 10 * percent);
        if (expired) metrics::markCut();
        return expired;
    }

    // MidgameBeam::extend made every layer of theirs.tactical in this decision (false: the clock cut it, the deeper
    // replies are missing).
    bool rivalBeamWhole = true;

    // What the replies of theirs.tactical from `from` on show against the immediate fire `fire`: Found, one of them
    // offsets it (offsetsImmediate); None, every one was read and none does; Unfinished, `stop` came before all were read.
    enum class Refutation : uint8_t { None, Found, Unfinished };
    // A reading of `r` over replies the midgame beam made in part (`beamWhole` false) is no complete reading.
    static Refutation withBeam(Refutation r, bool beamWhole) {
        return r == Refutation::None && !beamWhole ? Refutation::Unfinished : r;
    }
    template<class Stop>
    Refutation refutation(const Observation& o, const Plan& fire, size_t from, uint64_t seed, Stop stop) const {
        for (size_t i = from; i < theirs.tactical.size(); ++i) {
            if (stop()) return Refutation::Unfinished;
            const Plan& reply = theirs.tactical[i];
            if (reply.chains <= 4 || reply.sent < fire.sent || !offsetsImmediate(o, fire, reply, seed)) continue;
            return Refutation::Found;
        }
        return Refutation::None;
    }
    // A winning candidate stands after reading `r`: the first one unless a reply offsets it; one taken again after a
    // refuted candidate (`retaken`) only when every reply was read and none offsets it.
    static bool keepsWin(Refutation r, bool retaken) {
        return r == Refutation::None || (r == Refutation::Unfinished && !retaken);
    }

    // The reply, played in full, leaves nothing of the immediate fire on him: both play on with none of it above his
    // field (garbage of his may have reached the own field), or he wins.
    bool offsetsImmediate(const Observation& o, const Plan& fire, const Plan& response, uint64_t seed) const {
        return offsetsIn(o, playOut(o, mine, &fire, theirs, &response, p, seed, true), response);
    }
    // The same judged on `d`, the Duel in which one own pair was put and then the reply was played out.
    static bool offsetsIn(const Observation& o, const Duel& d, const Plan& response) {
        const DuelPlayer& a = d.player[o.me];
        const DuelPlayer& b = d.player[1 - o.me];
        if (a.moves != o.self().moves + 1 || b.moves != o.opponent().moves + response.count || !b.chain.chains) return false;
        if (d.state == (o.me ? RoundState::Won0 : RoundState::Won1)) return true;
        return d.state == RoundState::Playing && a.phase == DuelPhase::Control && b.phase == DuelPhase::Control &&
               a.chain.chains && !b.fallen && !b.incoming.total();
    }

    // An immediate fire fixes the chain. One legal answer that cancels it in full
    // disproves an estimate that this chain buries him. He uses only observed pairs.
    bool knownCounter(const Observation& o, const Plan& fire, uint64_t seed) const {
        if (fire.fireDepth != 1 || fire.count != 1 || fire.allClear || fire.sent <= 0 || p.handWait > 0) return false;
        for (const auto* list : {&theirs.plans, &theirs.keeping, &theirs.tactical}) {
            for (const Plan& response : *list) {
                if (exchangeOutOfTime()) return false;
                if (!response.fireDepth || response.sent < fire.sent) continue;
                if (list == &theirs.keeping && std::any_of(theirs.plans.begin(), theirs.plans.end(),
                    [&](const Plan& other) { return samePlacements(response, other); })) continue;
                if (offsetsImmediate(o, fire, response, seed)) return true;
            }
        }
        return false;
    }
    Params rivalParams(const Rival& rival) const {
        Params q = p;
        q.beamDepth = rival.depth;
        if (p.thinkRivalFull && p.thinkBudget > 0) q.beamWidth = widest();
        return q;
    }

    bool sameRival(const Rival& rival, const Observation& o, const Params& q, uint64_t seed) const {
        return held.depth >= rival.depth && held.pairs == theirs.knownCount && held.field == rival.field &&
               (held.seed == seed || !Builder::seeded(o.rules, q)) &&
               std::equal(held.pair, held.pair + held.pairs, theirs.pair, samePair);
    }

    void prepareRival(const Observation& o, uint64_t seed, Rival& rival, int64_t extraEnd) const {
        rival.prepared = true;
        rival.field = o.opponent().field;
        rival.from = o.frame;
        rival.deepest = p.depth();
        rival.pool = o.opponent().scoring.pool;
        if (p.x3OutlastSettled) {
            const int him = 1 - o.me;
            Duel d(o, seed);
            rival.noPair = d.player[him].phase != DuelPhase::Control && !toPairOf(d, him, o.frame + 6000);
            if (o.opponent().phase == Phase::Garbage) rival.previousDrop = o.frame - 1;
            else if (d.player[him].fallen > 0) rival.previousDrop = d.player[him].settleFrame;
            rival.field = d.player[him].field, rival.from = d.frame, rival.deepest = HORIZONS, rival.pool = d.player[him].scoring.pool;
        }
        int heights[W];
        rival.field.heights(heights);
        rival.need = W * (12 - heights[DEATH_X]);
        rival.cells = rival.field.f.count();
        int64_t until = extraEnd - 1; // (extraEnd: a Plan::endFrame, the frame after the next pair appears)
        for (const Plan& plan : mine.plans)
            if (plan.fireDepth) until = std::max(until, heldUntil(o, plan));
        rival.depth = placementsUpTo(until - rival.from, rival.cells, rival.deepest);
        if (p.thinkRivalFull && p.thinkBudget > 0) rival.depth = rival.deepest;
    }

    void startSplitRival(const Observation& o, uint64_t seed, Rival& rival, int64_t lastEnd) {
        prepareRival(o, seed, rival, lastEnd);
        if (rival.noPair || (p.thinkFit >= 2 && !p.thinkRivalFull)) return;
        const Params q = rivalParams(rival);
        if (sameRival(rival, o, q, seed)) return;
        const auto deadline = began + std::chrono::microseconds(int64_t(budgetNow) * 10 * std::max(p.thinkRival, 75));
        rival.prefetched = beginRival(rival.field, theirs.pair, theirs.knownCount, o.rules, o.timing, q, seed, true, deadline);
    }

    bool outlasts(const Observation& o, int id, int rate, uint64_t seed, Rival& rival, const Plan* attack = nullptr,
                  int* replyGarbage = nullptr) {
        const Plan* big = attack;
        if (!big) {
            for (const Plan& s : mine.plans)
                if (s.id[0] == id && s.fireDepth && (!big || s.sent > big->sent)) big = &s;
        }
        if (!big) return true;
        const Player& opp = o.opponent();
        if (opp.phase == Phase::Dead || theirs.pairCount == 0) return true;
        const auto movesUntil = [&](int64_t last) { return placementsUpTo(last - rival.from, rival.cells, rival.deepest); };
        Params q = p;
        if (!rival.searched) {
            if (!rival.prepared) prepareRival(o, seed, rival, attack ? attack->endFrame : 0);
            rival.searched = true;
            q = rivalParams(rival);
            if (!rival.noPair) {
                if (sameRival(rival, o, q, seed)) {
                    rival.built = held.built, rival.depth = held.depth, ++rivalReused;
                } else if (rival.prefetched || (p.thinkRivalFull && p.thinkBudget > 0)) {
                    if (!rival.prefetched) beginRival(rival.field, theirs.pair, theirs.knownCount, o.rules, o.timing, q, seed, true);
                    BuildResult got;
                    const int percent = rival.splitTime ? std::max(p.thinkRival, 75) : p.thinkRival;
                    const bool made = takeRival(rival.field, theirs.pair, theirs.knownCount, q, seed,
                                                began + std::chrono::microseconds(int64_t(budgetNow) * 10 * percent), got);
                    if (!made) {
                        got = BuildResult{};
                        got.cut = true;
                        got.layers = 0;
                        ++cutSearches;
                        ++rivalMissed;
                        poor = true;
                        againCut += choosingAgain;
                        if (Note* n = note()) ++n->cut;
                    }
                    // (the width of the search begun ahead, where a choice played again from the notes makes this search
                    //  itself with Params::thinkFit 2: it takes the widths of the notes in order)
                    if (Note* n = note(); n && p.thinkFit >= 2) n->widths.push_back(q.beamWidth);
                    rival.built = got;
                    if (!rival.countedSearch) rival.countedSearch = true, ++rivalSearches;
                    held.depth = made ? rival.depth : 0, held.field = rival.field, held.pairs = theirs.knownCount, held.seed = seed;
                    held.built = got;
                    for (int i = 0; i < theirs.knownCount; ++i) held.pair[i] = theirs.pair[i];
                } else {
                    rival.built = search(theirBuilder, rival.field, theirs.pair, theirs.knownCount, o, q, seed, p.thinkRival, p.thinkFit >= 2);
                    ++rivalSearches;
                    held.depth = rival.built.cut ? 0 : rival.depth, held.field = rival.field, held.pairs = theirs.knownCount, held.seed = seed, held.built = rival.built;
                    for (int i = 0; i < theirs.knownCount; ++i) held.pair[i] = theirs.pair[i];
                }
            }
        }
        const auto survivesKnownCounter = [&]() {
            if (!p.checkKnownExchange || big->fireDepth != 1) return true;
            const unsigned bit = 1u << id;
            if (!(rival.checkedFire & bit)) {
                rival.checkedFire |= bit;
                if (knownCounter(o, *big, seed) || exchangeOutOfTime()) rival.counteredFire |= bit;
            }
            return !(rival.counteredFire & bit);
        };
        const int64_t until = heldUntil(o, *big);
        const bool afterDrop = rival.previousDrop < until - o.timing.nextFrames;
        rival.unanswered = rival.noPair || (rival.from > until && !afterDrop);
        if (rival.unanswered) return survivesKnownCounter();
        // The search of as many moves as this chain leaves him: the one made, when it is that deep;
        // its tables for fewer; and one of its own beyond the tables (a beamDepth above HORIZONS) or
        // deeper than the one made (an attack that ends later than those the search was made for).
        const int depth = movesUntil(until);
        const bool whole = depth == rival.depth, tabled = depth < rival.depth && depth <= HORIZONS;
        BuildResult alone;
        if (!whole && !tabled) {
            q.beamDepth = depth;
            alone = search(theirBuilder, rival.field, theirs.pair, theirs.knownCount, o, q, seed, p.thinkRival, p.thinkFit >= 2);
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
        const bool fires = most > 0; // (the search found him a chain: only a wave turns the points he carries into garbage)
        if (p.x3OutlastScale != 100) most = int(int64_t(most) * p.x3OutlastScale / 100); // (he may build more than this search of his field finds)
        const int rateThen = std::min(rate, o.rules.rateAt(big->endFrame)); // (his answer runs after this chain)
        const int answer = fires ? (most + rival.pool) / rateThen : 0;
        if (replyGarbage) *replyGarbage = answer;
        // When he answers with a chain, what is left of mine falls on the field that chain has
        // emptied, not on the one he has now: a third column eleven high needs six pieces to
        // close today and eleven rows after his chain. (Counted: main chains were fired as
        // "wins" with 13 to 117 pieces to spare over an answer, into fields with that column
        // at nine to eleven.)
        int need = rival.need;
        if (p.x3OutlastSettled && answer >= p.x3OutlastAnswer && need < p.x3OutlastLeft) need = p.x3OutlastLeft;
        return big->sent - answer >= need && survivesKnownCounter();
    }
    bool outlasts(const Observation& o, int id, int rate, uint64_t seed) {
        Rival rival;
        return outlasts(o, id, rate, seed, rival);
    }
    // Params::thinkRivalFull, in every frame: when the opponent's side is another - a pair put, a pair in his hands,
    // one more of his pairs known - the search of his field that the next choice will ask for (outlasts) is begun: his
    // field as it stands when he next holds a pair, the pairs the plans of a choice take for him, the seed of the
    // choice to come. (A choice that asks for another search - a field the reading here did not foresee - begins its own.)
    void watchRival(const Observation& o) {
        if (!p.thinkRivalFull || p.thinkBudget <= 0 || !p.x3WinCheck || o.players != 2 || o.state != RoundState::Playing) return;
        const int him = 1 - o.me;
        const Player& opp = o.player[him];
        if (opp.phase == Phase::Dead || o.self().phase == Phase::Dead) return;
        Pair pair[MAX_KNOWN]{};
        const int limit = std::max(opp.phase == Phase::Control ? 1 : 0, p.searchKnownLimit(p.x2Depth));
        const int count = copySearchPairs(o, him, true, pair, limit);
        if (!count) return;
        const int64_t stamp = (int64_t(opp.moves) * (MAX_KNOWN + 1) + count) * 2 + (opp.phase == Phase::Control);
        if (stamp == rivalStamp && (p.knownPairs <= 3 ||
            (rivalObservedField == opp.field && std::equal(pair, pair + count, rivalObservedPairs.begin(), samePair)))) return;
        const uint64_t seed = roundSeed + uint64_t(o.self().moves) + (o.self().phase == Phase::Control ? 1 : 0);
        GameField field = opp.field;
        Params q = p;
        q.beamWidth = widest(), q.beamDepth = p.depth();
        if (p.x3OutlastSettled) {
            Duel d(o, seed);
            if (d.player[him].phase != DuelPhase::Control && !toPairOf(d, him, o.frame + 6000)) return;
            field = d.player[him].field, q.beamDepth = HORIZONS;
        }
        if (beginRival(field, pair, count, o.rules, o.timing, q, seed, false)) {
            rivalStamp = stamp;
            if (p.knownPairs > 3) {
                rivalObservedField = opp.field;
                std::copy_n(pair, count, rivalObservedPairs.begin());
            }
        }
    }

    // x3ChokeHis: may a chain of `points` be fired first, for what the opponent holds now? What he
    // holds: the largest chain a trial drop starts on his field (key pieces included, as buildValue
    // counts the main chain), or one his pairs on the screen fire. (Against a player who holds a
    // chain of his own, firing first loses: he builds on while mine runs and fires the larger one.
    // Against one who holds none it wins the round.)
    bool heHoldsLittle(const Observation& o, int rate, int points) const {
        if (p.x3ChokeHis <= 0) return true;
        int now = 0;
        readMainChain(o.opponent().field, o.rules, o.timing, p, &now);
        int64_t his = now;
        for (const Plan& x : theirs.plans)
            if (x.fireDepth && int64_t(x.sent) * rate > his) his = int64_t(x.sent) * rate;
        return !atLeastPercent(his, p.x3ChokeHis, points);
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

    // x3ChokeRoom: the pairs a field still takes, by its open cells - every column up to the twelfth row, the third to
    // the eleventh (a piece in its twelfth row closes the field). Not every one of them can be reached by a pair: it
    // is the most that fit.
    static int roomPairs(const GameField& g) {
        int h[W], open = 0;
        g.heights(h);
        for (int x = 0; x < W; ++x) {
            const int top = x == DEATH_X ? DEATH_Y : DEATH_Y + 1;
            if (h[x] < top) open += top - h[x];
        }
        return open / 2;
    }

    // exchangePoke (2): the frames from the one in which the own pair after chain `s` appears to the end of his main
    // chain, both counted as Plan::endFrame. His plan on the screen by its own endFrame (hisEnd); a trial drop, whose
    // chain runs `hisFrames` from its lock, taken to lock when `s` does (hisEnd -1).
    static int64_t exchangeSpare(const Plan& s, int64_t hisFrames, int64_t hisEnd) {
        return hisEnd >= 0 ? hisEnd - s.endFrame : hisFrames - (s.endFrame - s.lockFrame);
    }
    // ... his chain of `his` points ends before that pair appears, and a row or more of it is beyond what `s` offsets:
    // that falls before anything is fired second.
    static bool fallsFirst(const Plan& s, int64_t spare, int64_t his, int rate) {
        return spare <= 0 && his - int64_t(s.sent) * rate >= int64_t(W) * rate;
    }
    // secondOffsets: a chain standing on his field, `frames` from its first wave to its end, locked when the last pair of
    // `s` does: the frames from the one in which the own pair after `s` appears to the end of his chain (as Plan::endFrame).
    static int64_t standingSpare(const Plan& s, int frames, const Timing& timing) {
        return frames + timing.landFrames + timing.nextFrames + 1 - (s.endFrame - s.lockFrame);
    }

    // x3Crush: plan `s` keeps a main chain of `keyed` points (x3CrushKeep percent of it), for either player.
    bool keepsMain(const Plan& s, int keyed) const { return atLeastPercent(s.mainAfter, p.x3CrushKeep, keyed); }

    // x3Crush: a chain a trial drop starts on the opponent's field as it stands - a pair that is not on the screen may
    // fire it at a crush, which no plan of his shows.
    struct Standing {
        int points = 0;
        int frames = 0;     // from its first wave to its end
        int added = 0;      // the pieces that start it
        bool keeps = false; // it leaves his main chain (keepsMain)
    };
    std::vector<Standing> standing(const Observation& o, int hisKeyed) const {
        std::vector<Standing> all;
        const GameField& his = o.opponent().field;
        Trials trials;
        BitField after[W * MAX_COLORS];
        detect(his, o.rules, o.timing, p.trialMaxAdd, trials, after);
        for (int i = 0; i < trials.count; ++i) {
            const Trial& t = trials.item[i];
            if (t.chains < o.rules.chainRequirement) continue;
            GameField left = his;
            left.f = after[i];
            int main = 0;
            readMainChain(left, o.rules, o.timing, p, &main);
            all.push_back({t.points, t.frames, t.added, atLeastPercent(main, p.x3CrushKeep, hisKeyed)});
        }
        return all;
    }

    // x3Crush: the placement that crushes the opponent, or -1. Each plan that fires with the pair in hand, sends
    // x3Crush or more and keeps the own main chain is played against every reply of his that keeps his main chain -
    // standing still, and each such plan of his: the least then left above his field (what his reply did not offset)
    // must be x3Crush or more, and no reading may end with the own field closed. A chain that stands on his field,
    // that one piece fires and that leaves his main chain is such a reply too, though no pair on the screen holds the
    // piece: the next one does nearly every second time. Of the plans that pass, the one that scores best as an answer
    // would (what is left above him, the main chain kept, the pieces spent) and against which every larger chain he may
    // fire is offset by firing second (secondOffsets). Kind Crush.
    // With x3CrushPairs 2 or 3 the plans that fire with the second or the third pair on the screen are read in the same
    // way, when no plan of the pair in hand has passed - those of the second pair before those of the third: the first
    // placement of the plan is made (kind CrushBuild), and the next choice reads again.
    int crush(const Observation& o, unsigned legal, int rate, uint64_t seed) {
        metrics::Scope scope(metrics::Part::Crush);
        const int me = o.me;
        if (theirs.pairCount == 0 || o.opponent().phase == Phase::Dead) return -1;
        ++crushCounts.asked;
        const int pairs = p.x3CrushPairs > 1 ? p.x3CrushPairs : 1;
        // The plans of both lists of a player (PlanSet::keeping) that `take` takes, each sequence of placements once.
        const auto both = [](const PlanSet& set, const auto& take) {
            std::vector<const Plan*> all;
            for (const std::vector<Plan>* list : {&set.plans, &set.keeping})
                for (const Plan& x : *list)
                    if (take(x) && std::none_of(all.begin(), all.end(), [&](const Plan* y) { return samePlacements(*y, x); })) all.push_back(&x);
            return all;
        };
        // (most choices have no such chain: then nothing more is read)
        std::vector<const Plan*> chains =
            both(mine, [&](const Plan& s) { return s.fireDepth >= 1 && s.fireDepth <= pairs && s.sent >= p.x3Crush && (legal >> s.id[0] & 1); });
        if (chains.empty()) return ++crushCounts.none, -1;
        int mainKeyed = 0, hisKeyed = 0;
        readMainChain(o.self().field, o.rules, o.timing, p, &mainKeyed);
        std::erase_if(chains, [&](const Plan* s) { return !keepsMain(*s, mainKeyed); });
        if (chains.empty()) return ++crushCounts.spends, -1;
        readMainChain(o.opponent().field, o.rules, o.timing, p, &hisKeyed);
        const int waiting = aboveOther(o, me, p);
        struct Candidate {
            const Plan* plan;
            int64_t score;
            int left;
            size_t afterIndex;
        };
        std::vector<Candidate> passed;
        std::vector<GameField> afterFields;
        const bool reuseQuiet = p.thinkBudget <= 0;
        // His replies that keep his main chain: standing still, his chains by what they send, the largest first (the
        // answer that stops a crush is mostly found by the first reading), and the plans that only build.
        std::vector<const Plan*> keep{nullptr};
        for (const Plan* x : both(theirs, [&](const Plan& x) { return x.fireDepth && keepsMain(x, hisKeyed); })) keep.push_back(x);
        std::stable_sort(keep.begin() + 1, keep.end(), [](const Plan* a, const Plan* b) { return a->sent > b->sent; });
        for (const Plan* x : both(theirs, [](const Plan& x) { return !x.fireDepth; })) keep.push_back(x);
        // ... and the largest chain that stands on his field, that one piece fires and that leaves his main chain.
        const std::vector<Standing> stands = standing(o, hisKeyed);
        int ready = 0;
        for (const Standing& t : stands)
            if (t.keeps && t.added == 1 && t.points > ready) ready = t.points;
        bool anyPassed = false;
        for (int depth = 1; depth <= pairs; ++depth) {
            passed.clear();
            afterFields.clear();
            for (const Plan* own : chains) {
                const Plan& s = *own;
                if (s.fireDepth != depth) continue;
                int least = s.sent - ready / rate; // (a reply after which he has lost leaves him all of it)
                bool safe = true;
                GameField after;
                for (size_t r = 0; r < keep.size() && safe && least >= p.x3Crush; ++r) {
                    const Plan* x = keep[r];
                    const Duel d = playOut(o, mine, &s, theirs, x, p, seed);
                    if (reuseQuiet && r == 0) after = d.player[me].field;
                    const DuelPlayer& he = d.player[1 - me];
                    if (d.player[me].phase == DuelPhase::Dead) safe = false;
                    else if (he.phase != DuelPhase::Dead) least = std::min(least, he.fallen + he.incoming.total() - waiting);
                }
                if (safe && least >= p.x3Crush) {
                    passed.push_back({&s, int64_t(p.x3Surplus) * least + int64_t(p.x3Main) * (s.mainAfter / rate) - int64_t(p.x3Used) * s.used,
                                      least, afterFields.size()});
                    if (reuseQuiet) afterFields.push_back(after);
                }
            }
            if (passed.empty()) continue;
            anyPassed = true;
            std::stable_sort(passed.begin(), passed.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
            for (const Candidate& c : passed)
                if (secondOffsets(o, *c.plan, reuseQuiet ? &afterFields[c.afterIndex] : nullptr, hisKeyed, stands, rate, seed)) {
                    crushNote = {c.plan->sent, c.left, mainKeyed, hisKeyed, c.plan->id[0], c.plan->chains};
                    ++(depth == 1 ? crushCounts.fired : crushCounts.built);
                    return kind = depth == 1 ? Crush : CrushBuild, c.plan->id[0];
                }
        }
        return ++(anyPassed ? crushCounts.outlasted : crushCounts.answered), -1;
    }

    // x3Crush: is every chain the opponent may fire at crush `s` and that the crush alone does not offset, offset by the
    // own main chain built on while his runs? His chains: each plan on the screen in which he spends his main chain,
    // and each chain of `stands` (those that stand on his field) that does - a pair that is not on the screen may fire
    // it, and it is taken to lock when the last pair of the crush does, at the earliest. The own field is the one after
    // the crush with him standing still. The own pairs are placed from the frame the pair after the crush appears
    // (while the chain of the crush runs none is) to the end of his chain: of a chain of his that has ended by then,
    // what the crush does not offset falls before anything is fired second (a row or more: not offset). One search, as
    // deep as the longest of those chains leaves moves, is read off for each of them by the moves it leaves
    // (BuildResult::sumBy: the mean over the queues), as x3PokeRead reads a poke; what it finds must be x3Enough percent
    // of what is to be offset (100 at least: of the answers built against a running chain those of 90 to 110 percent of
    // it won every second round, the larger ones nine of ten). A search the clock stopped short of those moves has not
    // seen what can be built in them: not offset.
    bool secondOffsets(const Observation& o, const Plan& s, const GameField* settled, int hisKeyed,
                       const std::vector<Standing>& stands, int rate, uint64_t seed) {
        struct Fired {
            int64_t over;   // the points of his chain beyond those of the crush
            int64_t frames; // from the frame the own pair after the crush appears to the end of his chain
        };
        std::vector<Fired> fired; // (of either list: PlanSet::keeping; a plan of both counts once or twice alike)
        const int64_t crushed = int64_t(s.sent) * rate;
        int64_t longest = 0;
        const auto fires = [&](int64_t points, int64_t frames) {
            if (points <= crushed) return;
            fired.push_back({points - crushed, frames});
            if (frames > longest) longest = frames;
        };
        for (const std::vector<Plan>* list : {&theirs.plans, &theirs.keeping})
            for (const Plan& x : *list)
                if (x.fireDepth && !keepsMain(x, hisKeyed)) fires(x.points, x.endFrame - s.endFrame);
        for (const Standing& t : stands)
            if (!t.keeps) fires(t.points, standingSpare(s, t.frames, o.timing));
        if (fired.empty()) return true;
        // (over before a pair can be placed: what is beyond the crush falls. Less than a row of it is not minded.)
        const int64_t row = int64_t(W) * rate;
        if (std::any_of(fired.begin(), fired.end(), [&](const Fired& f) { return f.frames <= 0 && f.over >= row; })) return false;
        if (longest <= 0) return true;
        GameField replayed;
        if (!settled) {
            const Duel d = playOut(o, mine, &s, theirs, nullptr, p, seed);
            if (d.player[o.me].phase == DuelPhase::Dead) return false;
            replayed = d.player[o.me].field;
            settled = &replayed;
        }
        const GameField& after = *settled;
        const auto movesFor = [&](int64_t frames) { return movesUpTo(frames, after.f.count(), handWait, p.depth()); };
        BuildResult b;
        const bool searched = mine.knownCount > s.count;
        if (searched) b = searchOn(o, after, movesFor(longest), seed, s.count);
        const int enough = p.x3Enough > 100 ? p.x3Enough : 100;
        for (const Fired& f : fired) {
            if (f.frames <= 0) continue;
            int64_t mineThen = s.mainAfter;
            if (searched) {
                const int depth = movesFor(f.frames);
                if (depth > HORIZONS || (b.cut && depth > b.layers)) return false;
                mineThen = std::max(mineThen, meanBest(b, depth));
            }
            if (mineThen * 100 < f.over * enough) return false;
        }
        return true;
    }

    static bool pressureOwnIntact(const Observation& o, const Plan& attack, const Duel& d) {
        const DuelPlayer& own = d.player[o.me];
        return own.moves == o.self().moves + attack.count && own.chain.chains && !own.scoring.allClear &&
               own.phase != DuelPhase::Dead && !own.field.dead() && !own.fallen && !own.incoming.total();
    }

    // The direct-hit screen estimates the pressure before the rival plays again.
    static bool pressureLands(const Observation& o, const Plan& attack, const Duel& d) {
        const DuelPlayer& rival = d.player[1 - o.me];
        return pressureOwnIntact(o, attack, d) && rival.fallen + rival.incoming.total() >= 2 * W;
    }

    bool pressureReplyRevealed(const Observation& o, const Duel& quiet, const Plan& reply, const Duel& exchange,
                               int64_t firstCheck) const {
        // The first choice may start before the next pair appears. A later
        // revision cannot be assumed when choosing this attack.
        const DuelPlayer& rival = exchange.player[1 - o.me];
        return reply.fireDepth && rival.chain.chains && rival.moves == o.opponent().moves + reply.count &&
               firstCheck >= o.frame && seenBeforeChoice(firstCheck, quiet.player[o.me].spawnFrame, ahead, p.seeLate);
    }

    // pressureReplyRevealed at the frame read (`firstCheck`) and at the frame his last pair is checked in when he goes
    // the straight way (rivalSeenCheck): the rival read with FASTEST_BOUND may show his chain no sooner than that.
    bool pressureReplySeen(const Observation& o, const Plan& attack, const Duel& quiet, const Plan& reply, const Duel& exchange,
                           int64_t firstCheck, uint64_t seed) const {
        if (!pressureReplyRevealed(o, quiet, reply, exchange, firstCheck)) return false;
        const int64_t seen = rivalSeenCheck(o, mine, &attack, theirs, &reply, p, seed, firstCheck,
                                            [this] { return exchangeOutOfTime(75); });
        return pressureReplyRevealed(o, quiet, reply, exchange, seen);
    }

    // Pending garbage can disappear on the very next move.
    // Credit the garbage only if it actually lands against every reachable continuation.
    // If the known pairs or the bounded reading run out first, leave the attack unproved.
    bool pressureAfterReply(const Observation& o, const Plan& own, const Plan& reply, const Duel& d,
                            int required, int remaining, uint64_t seed) const {
        const DuelPlayer& him = d.player[1 - o.me];
        if (!pressureOwnIntact(o, own, d)) return false;
        if (him.fallen >= required) return true;
        if (him.fallen + him.incoming.total() < required || d.state != RoundState::Playing || him.phase != DuelPhase::Control ||
            !remaining || reply.count >= theirs.knownCount || reply.count >= std::size(reply.id)) return false;
        const unsigned legal = legalPlacements(him.field);
        bool checked = false;
        for (int id = 0; id < PLACEMENTS; ++id) {
            if (exchangeOutOfTime(75)) return false;
            if (!(legal >> id & 1)) continue;
            Plan next = reply;
            next.id[next.count++] = int8_t(id);
            const Duel further = playOut(o, mine, &own, theirs, &next, p, seed, true);
            if (further.player[1 - o.me].moves != o.opponent().moves + next.count) continue;
            checked = true;
            if (!pressureAfterReply(o, own, next, further, required, remaining - 1, seed)) return false;
        }
        return checked;
    }

    bool pressureFollowUp(const Observation& o, const Plan& attack, const Duel& quiet, const Plan& response,
                          bool spentMain, uint64_t seed) const {
        const int limit = std::min(mine.knownCount, int(std::size(attack.id)));
        if (attack.count >= limit) return false;
        const DuelPlayer& own = quiet.player[o.me];
        if (own.phase != DuelPhase::Control) return false;
        const auto fire = [&](const GameField& field, const Plan& prefix) {
            const Pair pair = mine.pair[prefix.count];
            const unsigned legal = legalPlacements(field);
            for (int id = 0; id < PLACEMENTS; ++id) {
                if (exchangeOutOfTime(75)) return false;
                if (!(legal >> id & 1)) continue;
                GameField after = field;
                juraic::place(after, placement(id), pair);
                if (!anyClear(after.f, o.rules)) continue;
                Plan combined = prefix;
                combined.id[combined.count++] = int8_t(id);
                const Duel d = playOut(o, mine, &combined, theirs, &response, p, seed, true);
                const int ourPoints = d.player[o.me].scoring.score - own.scoring.score;
                if (spentMain && !atLeastPercent(ourPoints, p.x3PokeKeep, response.points)) continue;
                if (pressureAfterReply(o, combined, response, d, spentMain ? W : 2 * W, 2, seed)) return true;
            }
            return false;
        };
        if (fire(own.field, attack)) return true;
        if (!spentMain || attack.count + 2 > limit) return false;

        // Try a non-firing placement before the follow-up. Read a second one
        // only when we can still place it while the committed chain runs.
        const auto extend = [&](auto&& next, const GameField& field, const Plan& prefix, int setups) -> bool {
            const Pair pair = mine.pair[prefix.count];
            const unsigned legal = legalPlacements(field);
            for (int id = 0; id < PLACEMENTS; ++id) {
                if (exchangeOutOfTime(75)) return false;
                if (!(legal >> id & 1)) continue;
                GameField after = field;
                juraic::place(after, placement(id), pair);
                if (after.dead() || anyClear(after.f, o.rules)) continue;
                Plan setup = prefix;
                setup.id[setup.count++] = int8_t(id);
                if (setups == 1) {
                    if (fire(after, setup)) return true;
                } else {
                    const Duel d = playOut(o, mine, &setup, theirs, &response, p, seed, true);
                    const DuelPlayer& ready = d.player[o.me];
                    const DuelPlayer& rival = d.player[1 - o.me];
                    if (ready.phase == DuelPhase::Control && !ready.fallen && ready.moves == o.self().moves + setup.count &&
                        rival.phase == DuelPhase::Control && ready.spawnFrame < rival.spawnFrame - o.timing.nextFrames &&
                        next(next, ready.field, setup, setups - 1)) return true;
                }
            }
            return false;
        };
        return extend(extend, own.field, attack, 1) ||
               (attack.count + 3 <= limit && extend(extend, own.field, attack, 2));
    }

    int midgamePressure(const Observation& o, unsigned legal, uint64_t seed) {
        metrics::Scope scope(metrics::Part::MidgamePressure);
        const int him = 1 - o.me;
        // A minimum hit must take the opponent to the crowded part of the field.
        // (every response of his is read below: not when the midgame beam made his replies in part)
        if (p.handWait > 0 || !theirs.pairCount || o.opponent().incoming.total() || !rivalBeamWhole ||
            o.opponent().field.f.count() + 2 * W < 50 || exchangeOutOfTime(75)) return -1;
        std::vector<const Plan*> attacks;
        for (const auto* list : {&mine.plans, &mine.tactical})
            for (const Plan& s : *list)
                if (s.chains >= 1 && s.chains <= 4 && !s.allClear && s.sent >= 2 * W && (legal >> s.id[0] & 1))
                    attacks.push_back(&s);
        if (attacks.empty()) return -1;
        std::stable_sort(attacks.begin(), attacks.end(), [](const Plan* a, const Plan* b) {
            if (a->fireDepth != b->fireDepth) return a->fireDepth < b->fireDepth;
            if (a->mainAfter != b->mainAfter) return a->mainAfter > b->mainAfter;
            return a->sent > b->sent;
        });
        std::vector<const Plan*> responses;
        for (const auto* list : {&theirs.plans, &theirs.tactical})
            for (const Plan& s : *list) responses.push_back(&s);
        std::stable_sort(responses.begin(), responses.end(), [](const Plan* a, const Plan* b) { return a->sent > b->sent; });
        Duel before(o, seed);
        if (before.player[him].phase != DuelPhase::Control && !toPairOf(before, him, o.frame + 6000)) return -1;
        const GameField& field = before.player[him].field;
        Replies standingReplies;
        repliesOf(field, o.rules, o.timing, p, standingReplies);
        BuildResult rivalBuild;
        for (const Plan* attack : attacks) {
            if (exchangeOutOfTime(75)) return -1;
            const Duel quiet = playOut(o, mine, attack, theirs, nullptr, p, seed, true);
            if (!pressureLands(o, *attack, quiet)) continue;
            const int64_t confirm = quiet.player[o.me].spawnFrame - o.timing.nextFrames;
            if (confirm < o.frame) continue;
            bool holds = true;
            bool drewMain = false;
            int largestAnswered = 0;
            for (const Plan* response : responses) {
                if (exchangeOutOfTime(75)) return -1;
                int64_t replyCheck;
                const Duel d = playOut(o, mine, attack, theirs, response, p, seed, true, &replyCheck);
                if (pressureAfterReply(o, *attack, *response, d, 2 * W, 2, seed)) continue;
                const DuelPlayer& rival = d.player[him];
                const bool revealed = pressureReplySeen(o, *attack, quiet, *response, d, replyCheck, seed);
                // A large reply must actually use up the rival's chain. An extension
                // can create a main chain larger than the one standing at the start.
                const bool spentMain = revealed && !response->allClear && !rival.scoring.allClear && !rival.fallen &&
                    rival.chain.chains == response->chains && response->sent >= attack->sent + W &&
                    atLeastPercent(response->points, p.x3PokeKeep, standingReplies.main) &&
                    !atLeastPercent(response->mainAfter, p.x3PokeKeep, response->points);
                if (revealed && pressureFollowUp(o, *attack, quiet, *response, spentMain, seed)) {
                    drewMain |= spentMain;
                    if (spentMain) largestAnswered = std::max(largestAnswered, response->points);
                    continue;
                }
                holds = false;
                break;
            }
            if (!holds) continue;
            // The early end of the move estimate errs on the side of giving him another pair.
            // Beyond the enumerated horizon, use standing replies and a short rival beam as
            // warning signs. Neither is a verified placement sequence.
            const int turns = repliesUntil(o, before, confirm, field.f.count());
            if (turns > HORIZONS) continue;
            if (turns > theirs.pairCount) {
                const int erase = quiet.player[him].incoming.total() - 2 * W + 1;
                const int atAttack = o.rules.rateAt(confirm);
                const int need = std::max(1, erase * atAttack - before.player[him].scoring.pool);
                // The known main already has a concrete second-fire answer. Beyond the read,
                // guard against a cheap offset and against a larger main than we answered.
                const int keep = drewMain ? largestAnswered : 0;
                if (answerChance(standingReplies, need, keep, turns, theirs.pair, theirs.knownCount, o.rules) >= CHANCE_SURE / 2 ||
                    (drewMain && answerChance(standingReplies, largestAnswered + 1, 0, turns, theirs.pair, theirs.knownCount, o.rules) >= CHANCE_SURE / 2)) continue;
                if (turns > 4) {
                    if (rivalBuild.depth < turns) {
                        Params replySearch = p;
                        replySearch.beamDepth = turns;
                        replySearch.beamWidth = std::min(p.width(), PLACEMENTS);
                        replySearch.beamQueues = std::min(p.queues(), 3);
                        rivalBuild = search(theirBuilder, field, theirs.pair, theirs.knownCount, o, replySearch, seed, 75, false);
                    }
                    if (rivalBuild.cut && rivalBuild.layers < turns) return -1;
                    const int danger = drewMain ? largestAnswered + 1 : need;
                    if (std::any_of(std::begin(rivalBuild.pointsBy), std::end(rivalBuild.pointsBy), [&](const auto& byTurn) {
                        return byTurn[turns - 1] >= danger;
                    })) continue;
                }
            }
            if (!exchangeOutOfTime(75)) return kind = drewMain ? Poke : attack->fireDepth == 1 ? Crush : CrushBuild, attack->id[0];
        }
        return -1;
    }

    bool maySplitMain(const Observation& o) const {
        return mine.knownCount == 2 && theirs.knownCount == 2 && p.handWait == 0 &&
               !o.self().incoming.total() && !o.opponent().incoming.total() &&
               !o.self().scoring.allClear && !o.opponent().scoring.allClear;
    }

    bool opensMain(const Observation& o, const Plan& opening, uint64_t seed) const {
        const Duel d = playOut(o, mine, &opening, theirs, nullptr, p, seed, true);
        const auto& own = d.player[o.me];
        return own.moves == o.self().moves + 1 && own.phase == DuelPhase::Control && own.chain.chains == 1 &&
               !own.scoring.allClear && !own.fallen && !own.incoming.total() && !d.player[1 - o.me].incoming.total();
    }

    bool hasSplitOpening(const Observation& o, unsigned legal, uint64_t seed) const {
        for (const Plan& plan : mine.plans) {
            if (exchangeOutOfTime(55)) return false;
            if (plan.count == 1 && plan.fireDepth == 1 && plan.chains == 1 && !plan.allClear && !plan.sent &&
                (legal >> plan.id[0] & 1) && opensMain(o, plan, seed)) return true;
        }
        return false;
    }

    static bool splitHolds(const Observation& o, const Plan& attack, const Duel& d) {
        const auto& own = d.player[o.me];
        const auto& enemy = d.player[1 - o.me];
        if (enemy.fallen % W) return false;
        if (d.state == (o.me == 0 ? RoundState::Won0 : RoundState::Won1)) return true;
        if (own.moves != o.self().moves + 2 || own.chain.chains != attack.chains || own.phase != DuelPhase::Control ||
            own.fallen || own.incoming.total() || own.field.dead() || own.scoring.allClear || enemy.scoring.allClear) return false;
        int heights[W];
        enemy.field.heights(heights);
        return enemy.incoming.total() >= W * (12 - heights[DEATH_X]);
    }

    std::vector<Plan> splitCandidates(const Observation& o, unsigned legal, uint64_t seed) const {
        const int rate = o.rules.rateAt(o.frame);
        unsigned singles = 0;
        int ordinary = 0;
        for (const Plan& plan : mine.plans) {
            if (!plan.fireDepth || !(legal >> plan.id[0] & 1)) continue;
            if (plan.count == 1 && plan.chains == 1 && !plan.allClear && plan.sent == 0) singles |= 1u << plan.id[0];
            else ordinary = std::max(ordinary, plan.points);
        }
        if (!singles) return {};
        std::vector<const Plan*> shortlist;
        for (const Plan& plan : mine.tactical)
            if (plan.count == 2 && plan.fireDepth == 2 && plan.chains >= 5 && !plan.allClear &&
                plan.points >= ordinary + 2 * W * rate && (singles >> plan.id[0] & 1)) shortlist.push_back(&plan);
        std::stable_sort(shortlist.begin(), shortlist.end(), [](const Plan* a, const Plan* b) { return a->sent > b->sent; });

        std::vector<Plan> exact;
        for (const Plan* candidate : shortlist) {
            if (exchangeOutOfTime(75)) break;
            Plan opening;
            opening.count = 1;
            opening.id[0] = candidate->id[0];
            if (!opensMain(o, opening, seed)) continue;
            const Duel quiet = playOut(o, mine, candidate, theirs, nullptr, p, seed, true);
            if (quiet.state != RoundState::Playing || !splitHolds(o, *candidate, quiet)) continue;
            Plan plan = *candidate;
            plan.points = quiet.player[o.me].scoring.score - o.self().scoring.score;
            plan.sent = quiet.player[1 - o.me].incoming.total();
            plan.endFrame = quiet.player[o.me].spawnFrame + 1;
            plan.confirmFrame = quiet.player[o.me].settleFrame > 0 ? quiet.player[o.me].settleFrame : 0;
            if (plan.points >= ordinary + 2 * W * rate) exact.push_back(plan);
        }
        return exact;
    }

    bool splitAnswers(const Observation& o, const Plan& attack, uint64_t seed) const {
        const int him = 1 - o.me;
        Duel start(o, seed);
        if (!theirs.midAir && !toPairOf(start, him, o.frame + 6000)) return false;
        const unsigned first = theirs.firstInAir() ? theirs.firstReach : legalPlacements(start.player[him].field);
        bool checked = false;
        for (unsigned ids = first; ids; ids &= ids - 1) {
            if (exchangeOutOfTime(75)) return false;
            Plan reply;
            reply.count = 1;
            reply.id[0] = int8_t(std::countr_zero(ids));
            const Duel d = playOut(o, mine, &attack, theirs, &reply, p, seed, true);
            const auto& enemy = d.player[him];
            if (enemy.moves != o.opponent().moves + 1) continue;
            checked = true;
            if (!splitHolds(o, attack, d)) return false;
            if (enemy.phase != DuelPhase::Control) continue;
            reply.count = 2;
            for (unsigned next = legalPlacements(enemy.field); next; next &= next - 1) {
                if (exchangeOutOfTime(75)) return false;
                reply.id[1] = int8_t(std::countr_zero(next));
                const Duel e = playOut(o, mine, &attack, theirs, &reply, p, seed, true);
                if (e.player[him].moves == o.opponent().moves + 2 && !splitHolds(o, attack, e)) return false;
            }
        }
        return checked && !exchangeOutOfTime(75);
    }

    // A harmless single can free a main chain for the next visible pair.
    int splitMainAttack(const Observation& o, unsigned legal, uint64_t seed) {
        metrics::Scope scope(metrics::Part::SplitMain);
        if (!maySplitMain(o) || exchangeOutOfTime(75)) return -1;
        for (const Plan& attack : splitCandidates(o, legal, seed)) {
            if (exchangeOutOfTime(75)) return -1;
            Rival rival;
            if (outlasts(o, attack.id[0], o.rules.rateAt(o.frame), seed, rival, &attack) && splitAnswers(o, attack, seed))
                return kind = Win, attack.id[0];
        }
        return -1;
    }

#ifdef JURAIC_LV38_REPLAY_AUDIT
    enum AuditBranch { AuditWin = 1, AuditLegal = 2, AuditOffset = 4 };
    unsigned auditBranches = 0;
#endif

    static int partChains(const Observation& o, int id, Pair pair) {
        if (id < 0 || !pair.axis || !pair.child) return 0;
        GameField field = o.self().field;
        juraic::place(field, placement(id), pair);
        Trial trial;
        return runTrialChain(field.f, o.rules, o.timing, trial, false);
    }

    int lv38PartChains(const Observation& o, int id) const {
        return partChains(o, id, mine.knownCount ? mine.pair[0] : Pair{});
    }

    static void recordPart(std::vector<PartNote>* log, const Observation& o, const char* part,
                           int before, int after, int arriving, int nextKind, Pair pair) {
        if (!log || before == after) return;
        const Player& self = o.self();
        int colours = self.field.f.normal().count();
        for (Cell cell : self.field.row14) colours += cell >= 1 && cell <= MAX_COLORS;
        log->push_back({o.frame, self.moves + 1, int(self.phase), self.incoming.total(), arriving, colours,
                        part, before, after, nextKind, partChains(o, after, pair)});
    }

    void recordLv38Part(const Observation& o, const char* part, int before, int after, int arriving, int nextKind) const {
        recordPart(partLog, o, part, before, after, arriving, nextKind, mine.knownCount ? mine.pair[0] : Pair{});
    }

    int recordLv37Part(const Observation& o, const char* part, int before, int after, int arriving, int nextKind) const {
        recordPart(lv37PartLog, o, part, before, after, arriving, nextKind, mine.knownCount ? mine.pair[0] : Pair{});
        return after;
    }

    void noteEquivalentSetup(const Observation& o, Pair pair, int before, int after) override {
        recordPart(lv37PartLog, o, "equivalent_route", before, after, 0, Build, pair);
    }

    struct SmallTakeResult {
        struct Quiet {
            int id = -1, amount = 0, main = 0, height = 0, turns = 0;
            int64_t frame = -1;
        } best;
        int fireWaves = 0, fireSent = -1, replyGarbage = -1;
        bool eligible = false, attackKnown = false, holds = false, outgunned = false;
    };

    SmallTakeResult smallTake(const Observation& o, int ordinary, unsigned legal, bool measure) {
        metrics::Scope scope(metrics::Part::TakeSmall);
        SmallTakeResult result;
        const auto& self = o.self();
        const int incoming = self.incoming.total();
        if (incoming < 1 || incoming >= W || ordinary < 0 || !(legal >> ordinary & 1u) || !mine.knownCount) return result;
        int colors = self.field.f.normal().count();
        for (Cell cell : self.field.row14) colors += cell >= 1 && cell <= MAX_COLORS;
        int heights[W];
        self.field.heights(heights);
        if (colors >= 64 || heights[DEATH_X] > 9 || self.field.row14[DEATH_X] != EMPTY) return result;

        GameField ignited = self.field;
        juraic::place(ignited, placement(ordinary), mine.pair[0]);
        Trial ignition;
        runTrialChain(ignited.f, o.rules, o.timing, ignition, false);
        if (ignition.chains < 5) return result;
        result.eligible = true;
        result.fireWaves = ignition.chains;
        const uint64_t seed = roundSeed + uint64_t(self.moves);
        const auto stop = [&] { return exchangeOutOfTime(92); };
        const Plan* attack = nullptr;
        for (const auto* list : {&mine.plans, &mine.tactical, &mine.keeping}) for (const auto& plan : *list)
            if (plan.id[0] == ordinary && plan.count == 1 && plan.fireDepth == 1 && plan.chains == ignition.chains &&
                (!attack || plan.sent > attack->sent)) attack = &plan;
        if (attack && !stop()) {
            result.attackKnown = true;
            result.fireSent = attack->sent;
            Rival rival;
            result.holds = outlasts(o, ordinary, o.rules.rateAt(o.frame), seed, rival, attack, &result.replyGarbage);
            result.outgunned = !attack->allClear && result.replyGarbage >= 0 && result.replyGarbage > attack->sent;
        }
        if (!measure && (!result.outgunned || self.scoring.allClear)) return result;

        Duel start(o, seed);
        const int me = o.me;
        const bool inHand = mine.midAir;
        const int64_t limit = o.frame + 6000;
        if (!inHand && !toPairOf(start, me, limit)) return result;
        SpawnInputCache inputs;
        inputs.configure(o.timing, o.rules.speedPercent);
        const int depth = std::min(mine.pairCount, mine.knownCount);
        const auto walk = [&](auto&& walk, const Duel& before, int turn, int first) -> void {
            const auto& own = before.player[me];
            const unsigned available = turn == 0 ? legal : legalPlacements(own.field);
            for (unsigned ids = available; ids && !stop(); ids &= ids - 1) {
                const int id = std::countr_zero(ids);
                const auto where = placement(id);
                GameField placed = own.field;
                juraic::place(placed, where, mine.pair[turn]);
                if (placed.dead() || anyClear(placed.f, o.rules)) continue;
                const bool current = turn == 0 && (inHand || before.appearingInAir(me));
                const auto input = current ? SpawnInput{int(mine.firstLock[id] - o.frame + 1), mine.firstSoft[id], mine.firstRest[id]}
                                           : inputs.get(own.field, where);
                if (input.frames <= 0 || (current && !(mine.firstReach >> id & 1u))) continue;
                const int64_t lock = (current ? o.frame : own.spawnFrame) + input.frames - 1;
                const int firstId = turn ? first : id;
                Duel trial = before;
                std::array<DuelDrop, 2> drops{};
                trial.firstDrops = &drops;
                trial.place(me, where, mine.pair[turn], lock, input.soft, input.rested);
                const bool another = toPairOf(trial, me, limit);
                trial.firstDrops = nullptr;
                const DuelDrop& drop = drops[me];
                if (!drop.amount) {
                    if (another && turn + 1 < depth) walk(walk, trial, turn + 1, firstId);
                    continue;
                }
                if (drop.field.dead() || (result.best.id >= 0 && drop.amount > result.best.amount)) continue;
                if (!o.nextGarbageOrder[me] && drop.amount % W) {
                    // (the columns of the pieces beyond whole rows are not known: the third column may take one of them)
                    int placedHeights[W];
                    placed.heights(placedHeights);
                    if (placedHeights[DEATH_X] + drop.amount / W + 1 > DEATH_Y) continue;
                }
                int main = 0;
                readMainChain(drop.field, o.rules, o.timing, p, &main);
                int afterHeights[W];
                drop.field.heights(afterHeights);
                const int height = afterHeights[DEATH_X];
                const auto& best = result.best;
                if (best.id < 0 || std::tuple(drop.amount, -main, height) < std::tuple(best.amount, -best.main, best.height))
                    result.best = {firstId, drop.amount, main, height, turn + 1, drop.frame};
            }
        };
        if (depth > 0) walk(walk, start, 0, -1);
        return result;
    }

    int decide(const Observation& o, unsigned legal) override {
#ifdef JURAIC_LV38_REPLAY_AUDIT
        auditBranches = 0;
#endif
        const int ordinary = decideOrdinary(o, legal);
        if (p.lv38Midgame && p.lv38FireOrLose > 0) {
            lv38_fire_or_lose::Decision fire{o.frame, o.self().moves + 1, ordinary};
            if (kind != Win && !exchangeOutOfTime(96)) fire = lv38_fire_or_lose::evaluate(o, legal, ordinary, p);
            if (fireOrLoseLog) fireOrLoseLog->push_back(fire);
            if (fire.selected >= 0) {
                twoWaveCarry = {};
                twoWaveNext = {};
                exchangeNote.chosen = false;
                return kind = Fire, fire.selected;
            }
        }
        if (!p.lv38Midgame || !p.lv38TakeSmall || ordinary < 0) return ordinary;
        const auto read = smallTake(o, ordinary, legal, false);
        if (!read.outgunned || read.best.id < 0 || read.best.main <= 0 || read.best.amount >= W || exchangeOutOfTime(95))
            return ordinary;
        if (takeSmallLog) {
            int colours = o.self().field.f.normal().count();
            for (Cell cell : o.self().field.row14) colours += cell >= 1 && cell <= MAX_COLORS;
            takeSmallLog->push_back({o.frame, o.self().moves + 1, int(o.self().phase), ordinary, read.best.id,
                                    o.self().incoming.total(), colours, read.best.turns, read.best.amount});
        }
        twoWaveCarry = {};
        twoWaveNext = {};
        recordLv38Part(o, "take_small", ordinary, read.best.id, read.best.amount, Answer);
        return kind = Answer, read.best.id;
    }

    int decideOrdinary(const Observation& o, unsigned legal) {
        continuationSearches.clear();
        lv38BuildNote = {};
        lv38TailAnswerNote = {};
        lv38AnswerBuildNote = {};
        lv38ResponseNewlyEligible = 0;
        const int me = o.me;
        const uint64_t seed = roundSeed + uint64_t(o.self().moves);
        const int rate = o.rules.rateAt(o.frame);
        const bool secondChoice = choosingAgain; // the second choice of this pair (Params::thinkAgain)
        int pairs = beginChoice(o, secondChoice); // (the clock of the choice, or the notes of a round played again: pace.h)
        const bool timedThird = p.readMidgame && p.thinkPlans > 0 && budgetNow > 0 && p.thinkBudget > 0 && pairs >= 3;
        const int firstDepth = timedThird ? 2 : pairs;
        {
            metrics::Scope candidateTimer(metrics::Part::Candidates);
            Gaze::ThirdPair third;
            auto* continuation = timedThird ? &third : nullptr;
            gaze.plans(o, me, true, firstDepth, p, seed, mine, continuation);
            gaze.plans(o, 1 - me, false, firstDepth, p, seed, theirs, continuation);
            if (timedThird && (mine.knownCount >= 3 || theirs.knownCount >= 3)) {
                const auto deadline = began + std::chrono::microseconds(int64_t(budgetNow) * 10 * std::min(p.thinkPlans, 100));
                metrics::Scope thirdTimer(metrics::Part::ThirdPair);
                if (!gaze.tryThirdPair(o, p, seed, mine, theirs, deadline, continuation)) {
                    thirdTimer.cut();
                    pairs = 2;
                    ++shortPlans;
                    if (noting) notes.back().pairs = pairs;
                }
            }
        }
        plansMade(pairs, secondChoice, mine.pairCount >= 3 && theirs.pairCount >= 3);
        return decideWithPlans(o, legal, seed, rate);
    }

    int decideWithPlans(const Observation& o, unsigned legal, uint64_t seed, int rate) {
        const int me = o.me;
        kind = None, guardedNow = false;
        exchangeNote.chosen = false;
        const bool twoWave = p.lv38Midgame && lv38PartEnabled(p.lv38TwoWave, Lv38Part::TwoWave) && !p.handWait;
        const Plan* retained = nullptr;
        if (twoWave) {
            twoWaveNote = {};
            twoWaveFrame = o.frame;
            twoWaveRetained = false;
            if (twoWaveTurn != o.self().moves) {
                twoWaveTurn = o.self().moves;
                twoWaveCarry = twoWaveNext;
            }
            twoWaveNext = {};
            const auto& carry = twoWaveCarry;
            bool matches = carry.moves == twoWaveTurn && carry.field == o.self().field &&
                carry.suffix.count > 0 && carry.suffix.count <= mine.knownCount;
            for (int k = 0; matches && k < carry.suffix.count; ++k)
                matches = carry.pairs[k].axis == mine.pair[k].axis && carry.pairs[k].child == mine.pair[k].child;
            if (matches) retained = &carry.suffix;
            else twoWaveCarry = {};
        }
        const auto checkTwoWave = [&](int ordinary, int arriving) {
            if (!twoWave || guardedNow) return ordinary;
            metrics::Scope scope(metrics::Part::TwoWave);
            traceLv38Part(Lv38Part::TwoWave);
            countComponent(CountedComponent::TwoWave);
            const auto stop = [&] { return exchangeOutOfTime(96); };
            lv38_two_wave::ReplayCache replays;
            auto* cache = p.thinkBudget <= 0 && (p.lv38TwoWaveMainCover || p.lv38TwoWaveReselect > 0) ? &replays : nullptr;
            twoWaveRetained = retained != nullptr;
            int selected = lv38_two_wave::prefer(o, mine, theirs, ordinary, legal, p, seed, stop,
                                                twoWaveNote, retained, cache);
            int blockedMain = -1;
            const auto keepOrdinary = [&] {
                twoWaveNext = {};
                recordLv38Part(o, "two_wave_main_kept", blockedMain, ordinary, arriving, kind);
                return ordinary;
            };
            if (p.lv38TwoWaveNoMain > 0 && ordinary >= 0 && selected != ordinary &&
                lv38PartChains(o, selected) >= 5 && lv38PartChains(o, ordinary) < 5) {
                blockedMain = selected;
                if (p.lv38TwoWaveReselect <= 0 || stop()) return keepOrdinary();
                metrics::Scope reselectScope(metrics::Part::Reselect);

                unsigned quietOrShort = legal;
                for (unsigned bits = legal; bits; bits &= bits - 1) {
                    const int id = std::countr_zero(bits);
                    if (lv38PartChains(o, id) >= 5) quietOrShort &= ~(1u << id);
                }
                const Plan* carry = retained;
                if (carry && !(quietOrShort >> carry->id[0] & 1u)) carry = nullptr;
                twoWaveRetained = carry != nullptr;
                twoWaveNext = {};
                countComponent(CountedComponent::Reselect);
                selected = lv38_two_wave::prefer(o, mine, theirs, ordinary, quietOrShort, p, seed, stop,
                                                twoWaveNote, carry, cache);
                if (selected == ordinary || stop()) return keepOrdinary();
            }
            const char* part = blockedMain >= 0 ? "two_wave_reselect" : "two_wave";
            const auto& route = twoWaveNote.route;
            if (route.count < 2 || route.count > 3 || route.id[0] != selected) {
                recordLv38Part(o, part, ordinary, selected, arriving, kind);
                return selected;
            }
            Plan first;
            first.count = 1;
            first.id[0] = route.id[0];
            KnownReplay replay{stop};
            replay.acceptPlacement = [](const Duel&, int, Placement, Pair, int64_t, int64_t lock) {
                return lock >= 0;
            };
            Duel start(o, seed);
            ReplayCheckpoint spawn{start};
            const auto d = playOut(o, mine, &first, theirs, nullptr, p, seed, true, nullptr, &replay, nullptr, &spawn);
            if (stop()) {
                twoWaveNote.status = 4;
                twoWaveNote.selected = ordinary;
                twoWaveNote.route = {};
                return blockedMain >= 0 ? keepOrdinary() : ordinary;
            }
            const auto& self = spawn.duel.player[o.me];
            if (d.state == RoundState::Playing && self.phase == DuelPhase::Control &&
                self.moves == twoWaveTurn + 1) {
                twoWaveNext.moves = self.moves;
                twoWaveNext.field = self.field;
                twoWaveNext.suffix.count = route.count - 1;
                for (int k = 0; k < twoWaveNext.suffix.count; ++k) {
                    twoWaveNext.suffix.id[k] = route.id[k + 1];
                    twoWaveNext.pairs[k] = mine.pair[k + 1];
                }
            }
            recordLv38Part(o, part, ordinary, selected, arriving, kind);
            return selected;
        };
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
        const auto outOfTime = [&] { return exchangeOutOfTime(55); };
        rivalBeamWhole = true;
        const bool splitEnabled = lv37PartEnabled(p.lv37SplitMain, Lv37Part::SplitMain);
        const bool splitEarly = splitEnabled && p.thinkBudget > 0 && p.readMidgame && p.x3Poke &&
                                o.self().field.f.count() >= 24 &&
                                maySplitMain(o) && hasSplitOpening(o, legal, seed);
        Rival sharedRival;
        std::vector<Plan> splitReady;
        if (splitEarly) {
            metrics::Scope scope(metrics::Part::SplitMain);
            measurePart<metrics::Part::SelfPlanExtend>([&] {
                return MidgameBeam::extend(o, p, seed, mine, outOfTime);
            });
            const auto candidates = splitCandidates(o, legal, seed);
            for (const Plan& attack : candidates)
                if (splitAnswers(o, attack, seed)) splitReady.push_back(attack);
            if (!splitReady.empty()) {
                int64_t lastEnd = 0;
                for (const Plan& attack : splitReady) lastEnd = std::max(lastEnd, attack.endFrame);
                startSplitRival(o, seed, sharedRival, lastEnd);
            }
        }
        // The kinds of moves in their order: each sets `kind` and returns its placement, or -1 (not this kind).
        int id = win(o, legal, rate, seed, sharedRival.prepared ? &sharedRival : nullptr);
        bool readRival = false;
        if (id >= 0 && p.readMidgame && p.checkKnownExchange && p.handWait == 0 && !outOfTime()) {
            metrics::Scope scope(metrics::Part::WinRecheck);
            unsigned remaining = legal;
            const bool recheck = p.lv38Midgame && lv38PartEnabled(p.lv38WinRecheck, Lv38Part::WinRecheck);
            bool retaken = false; // (id is a candidate taken again after a refuted one)
            while (id >= 0) {
                if (retaken) traceLv38Part(Lv38Part::WinRecheck);
                if (exchangeOutOfTime(75)) {
                    if (retaken) recordLv38Part(o, "win_recheck", id, -1, -1, kind);
                    if (retaken) id = -1;
                    break;
                }
                const auto fire = std::find_if(mine.plans.begin(), mine.plans.end(), [&](const Plan& s) {
                    return s.id[0] == id && s.count == 1 && s.fireDepth == 1 && s.chains >= 5 && s.sent > 0 && !s.allClear;
                });
                if (fire == mine.plans.end()) break;
                // The ordinary win test runs before the midgame beam. A visible
                // fourth pair or an initial small clear can open a real counter.
                const size_t firstNew = readRival ? 0 : theirs.tactical.size();
                if (!readRival) {
                    rivalBeamWhole = measurePart<metrics::Part::RivalPlanExtend>([&] {
                        return MidgameBeam::extend(o, p, seed, theirs, outOfTime);
                    });
                    readRival = true;
                }
                const Refutation read = withBeam(refutation(o, *fire, firstNew, seed, [&] { return exchangeOutOfTime(75); }),
                                                 rivalBeamWhole);
                if (keepsWin(read, retaken)) break;
                if (retaken) recordLv38Part(o, "win_recheck", id, -1, -1, kind);
                if (read == Refutation::Unfinished) {
                    id = -1;
                    break;
                }
#ifdef JURAIC_LV38_REPLAY_AUDIT
                auditBranches |= AuditWin;
#endif
                remaining &= ~(1u << id);
                id = remaining ? win(o, remaining, rate, seed, sharedRival.prepared ? &sharedRival : nullptr) : -1;
                if (!recheck) break;
                retaken = true;
                if (exchangeOutOfTime(75)) {
                    recordLv38Part(o, "win_recheck", id, -1, -1, kind);
                    id = -1;
                }
            }
        }
        if (id < 0 && p.x3ClearNow > 0) id = clearNow(legal);
        const bool needsLifeline = p.lv38Midgame && o.self().incoming.total() > 0 &&
            o.self().incoming.total() < W && o.self().field.f.occupied().get(DEATH_X, DEATH_Y - 1);
        const bool checkLifeline = needsLifeline && lv38PartEnabled(p.lv38Lifeline, Lv38Part::Lifeline);
        if (id >= 0 && (!checkLifeline || kind != Win)) {
            if (twoWave) twoWaveCarry = {};
            return id;
        }
        // What is on its way: the opponent's chain that is running, to its end.
        Duel afterChain(o, seed);
        const bool chainRuns = afterChain.player[1 - me].phase == DuelPhase::Check || afterChain.player[1 - me].phase == DuelPhase::Settle;
        if (chainRuns) toPairOf(afterChain, 1 - me, o.frame + 6000);
        const int arriving = afterChain.player[me].incoming.total();
        if (id >= 0) {
            traceLv38Part(Lv38Part::Lifeline);
            const int survive = lv38_lifeline::prefer(o, afterChain, mine, theirs, id, legal, arriving, p, seed,
                                                       [&] { return exchangeOutOfTime(90); });
            recordLv38Part(o, "lifeline_win", id, survive, arriving, Answer);
            return survive == id ? id : (kind = Answer, survive);
        }
        if (p.readMidgame && o.self().field.f.count() >= 24) {
            if (!splitEarly)
                measurePart<metrics::Part::SelfPlanExtend>([&] {
                    return MidgameBeam::extend(o, p, seed, mine, outOfTime);
                });
            if (!readRival)
                rivalBeamWhole = measurePart<metrics::Part::RivalPlanExtend>([&] {
                    return MidgameBeam::extend(o, p, seed, theirs, outOfTime);
                });
        }
        if (arriving > 0) {
            if (p.lv38Midgame && (p.lv38Plan5 > 0 || p.lv38Plan12 > 0 || p.lv38HeadCut > 0) &&
                lv38PartEnabled(p.lv38TailAnswer, Lv38Part::TailAnswer)) {
                traceLv38Part(Lv38Part::TailAnswer);
                countComponent(CountedComponent::TailAnswer);
                const int tail = lv38_tail_answer::prefer(o, mine, theirs, legal, arriving, p, seed,
                    [&] { return exchangeOutOfTime(72); }, lv38TailAnswerNote);
                if (tail >= 0) {
                    if (twoWave) twoWaveCarry = {};
                    kind = lv38TailAnswerNote.depth == 1 ? Answer : AnswerBuild;
                    recordLv38Part(o, "tail_answer", -1, tail, arriving, kind);
                    if (cutAnswers) cutAnswers->push_back({o.frame, o.self().moves + 1, lv38TailAnswerNote});
                    return tail;
                }
            }
            if (p.x3AnswerFire > 0 && o.self().field.f.count() >= p.x3AnswerFire) id = fireAnswer(o, legal, rate, seed);
            if (id < 0) id = answer(o, legal, rate, seed, afterChain, chainRuns, arriving);
            if (p.readMidgame && id >= 0 && (kind == Answer || kind == AnswerBuild || kind == Fire) && arriving <= W) {
                const auto fired = std::find_if(mine.plans.begin(), mine.plans.end(), [&](const Plan& s) {
                    return s.id[0] == id && s.fireDepth == 1;
                });
                const Plan* fire = fired != mine.plans.end() ? &*fired : nullptr;
                const int preferred = fire ? -1 : id;
                if (!fire && kind != Fire) {
                    for (const Plan& plan : mine.plans) {
                        if (plan.fireDepth != 1 || plan.chains < 5 || plan.allClear || !(legal >> plan.id[0] & 1) ||
                            plan.mainAfter >= plan.points / 2) continue;
                        if (!fire || plan.points > fire->points) fire = &plan;
                    }
                }
                if (fire && lv37PartEnabled(p.lv37SmallReceive, Lv37Part::SmallReceive)) {
                    const int receive = measurePart<metrics::Part::SmallReceive>([&] {
                        return smallReceive(o, mine, theirs, *fire, legal, arriving, p, seed,
                                                     [&] { return exchangeOutOfTime(85); }, preferred);
                    });
                    if (receive >= 0 && receive != id) {
                        id = recordLv37Part(o, "small_receive", id, receive, arriving, Answer);
                        kind = Answer;
                    }
                }
            }
            if (p.checkKnownExchange && p.readMidgame && id >= 0 && kind == Answer && arriving < W) {
                if (lv37PartEnabled(p.lv37ReceiveCompare, Lv37Part::ReceiveCompare)) {
                    const int immediate = measurePart<metrics::Part::ReceiveCompare>([&] {
                        return receive_compare::prefer(o, mine, theirs, id, legal, arriving, p, seed,
                                                                  [&] { return exchangeOutOfTime(85); });
                    });
                    if (immediate >= 0) id = recordLv37Part(o, "receive_compare", id, immediate, arriving, kind);
                }
                if (lv37PartEnabled(p.lv37CommittedAnswer, Lv37Part::CommittedAnswer)) {
                    const int committed = measurePart<metrics::Part::CommittedAnswer>([&] {
                        return preferCommittedAnswer(o, mine, theirs, id, legal, arriving, p, seed,
                                                                [&] { return exchangeOutOfTime(85); });
                    });
                    id = recordLv37Part(o, "committed_answer", id, committed, arriving, kind);
                }
            }
            if (p.checkKnownExchange && p.readMidgame && id >= 0 && (kind == Answer || kind == AnswerBuild) &&
                lv37PartEnabled(p.lv37SideResponse, Lv37Part::SideResponse)) {
                const int side = measurePart<metrics::Part::SideResponse>([&] {
                    return side_response::prefer(o, afterChain, mine, theirs, id, legal, arriving, p, seed, sideReady,
                                                 [&] { return exchangeOutOfTime(85); });
                });
                id = recordLv37Part(o, "side_response", id, side, arriving, kind);
            }
            if (p.lv38Midgame && chainRuns && arriving >= W && id >= 0 && (kind == Answer || kind == AnswerBuild) &&
                lv38PartEnabled(p.lv38Response, Lv38Part::Response)) {
                traceLv38Part(Lv38Part::Response);
                const int side = lv38_response::prefer(o, afterChain, mine, theirs, id, legal, arriving, p, seed,
                                                       [&] { return exchangeOutOfTime(85); },
                                                       &lv38ResponseNewlyEligible);
                if (side != id) {
                    recordLv38Part(o, "response", id, side, arriving, Answer);
                    if (!twoWave) return kind = Answer, side;
                    id = side, kind = Answer;
                }
            }
            if (checkLifeline && arriving < W && id >= 0 && (kind == Answer || kind == AnswerBuild)) {
                traceLv38Part(Lv38Part::Lifeline);
                const int survive = lv38_lifeline::prefer(o, afterChain, mine, theirs, id, legal, arriving, p, seed,
                                                           [&] { return exchangeOutOfTime(90); });
                recordLv38Part(o, "lifeline", id, survive, arriving, Answer);
                if (survive != id) return kind = Answer, survive;
            }
            if (!needsLifeline && chainRuns && id >= 0 && (kind == Answer || kind == AnswerBuild) &&
                lv38PartEnabled(p.lv38Unannounced, Lv38Part::Unannounced)) {
                traceLv38Part(Lv38Part::Unannounced);
                const int survive = lv38_lifeline::preferUnannouncedWave(
                    o, afterChain, mine, theirs, id, legal, arriving, p, seed, [&] { return exchangeOutOfTime(90); });
                recordLv38Part(o, "unannounced", id, survive, arriving, Answer);
                if (survive != id) return kind = Answer, survive;
            }
        } else {
            if (p.x3AllClear > 0) id = clearInReach(o, legal);
            if (id < 0 && p.pokeExchange) {
                id = exchangePoke(o, legal, rate, seed);
                if (id >= 0) recordLv37Part(o, "exchange_poke", -1, id, 0, kind);
            }
            if (id < 0 && p.readMidgame && p.x3Poke) {
                id = midgamePressure(o, legal, seed);
                if (id >= 0) recordLv37Part(o, "midgame_pressure", -1, id, 0, kind);
            }
            // Crush: the opponent cannot answer in time an attack that keeps the own main chain, and taking it saturates
            // his field (x3Crush; crush above).
            if (id < 0 && p.x3Crush > 0) id = crush(o, legal, rate, seed);
            if (id < 0 && p.x3Poke && p.pokeExchange != 2) {
                id = poke(o, legal, rate, seed);
                if (id >= 0 && kind == Poke)
                    recordPart(legacyPokeLog, o, "legacy_poke", -1, id, 0, kind, mine.knownCount ? mine.pair[0] : Pair{});
            }
            if (id < 0 && splitEarly && !stale.load()) {
                metrics::Scope scope(metrics::Part::SplitMain);
                // If ordinary wins needed the shared result early, the same
                // search can still finish in the extra attack's own window.
                if (!splitReady.empty() && sharedRival.prefetched) {
                    if (sharedRival.built.cut) sharedRival.searched = false;
                    sharedRival.splitTime = true;
                }
                for (const Plan& attack : splitReady) {
                    const bool holds = outlasts(o, attack.id[0], rate, seed, sharedRival, &attack);
                    if (!holds || stale.load()) continue;
                    id = attack.id[0], kind = Win;
                    recordLv37Part(o, "split_main", -1, id, 0, kind);
                    break;
                }
            }
            if (id < 0 && !splitEarly && splitEnabled && p.readMidgame && p.x3Poke) {
                id = splitMainAttack(o, legal, seed);
                if (id >= 0) recordLv37Part(o, "split_main", -1, id, 0, kind);
            }
        }
        if (id >= 0) {
            if (kind == Answer || kind == AnswerBuild) {
                const int ordinary = checkTwoWave(id, arriving);
                if (p.lv38Midgame && (p.lv38Plan7 > 0 || p.lv38Plan7Guard > 0) && chainRuns && arriving < 30 &&
                    ordinary >= 0 && ordinary == id && !guardedNow && (!twoWave || !twoWaveNote.route.count)) {
                    const auto stop = [&] { return exchangeOutOfTime(); };
                    if (p.thinkBudget > 0 && budgetNow > 0)
                        builder.until(began + std::chrono::microseconds(int64_t(budgetNow) * 1000));
                    else builder.untimed();
                    searching.store(&builder);
                    if (stop()) builder.until(Builder::Clock::now());
                    const int selected = lv38_answer_build::prefer(o, afterChain, mine, theirs, legal, ordinary,
                        arriving, rate, p, seed, roundSeed * 1000 + uint64_t(o.self().moves), builder, stop,
                        lv38AnswerBuildNote);
                    searching.store(nullptr);
                    if (selected != ordinary) return kind = AnswerBuild, selected;
                }
                return ordinary;
            }
            if (twoWave) twoWaveCarry = {};
            return id;
        }
        const auto& settledRival = afterChain.player[1 - me];
        const auto rivalPhase = o.opponent().phase;
        // The settle simulation also runs while a quiet pair is landing.
        // A quiet landing still permits the tail and ready-main checks.
        const bool quietRivalLanding = chainRuns && !arriving &&
            (rivalPhase == Phase::Land || rivalPhase == Phase::Split) &&
            !o.opponent().chain.chains && afterChain.state == RoundState::Playing &&
            settledRival.phase == DuelPhase::Control && !settledRival.chain.chains && !settledRival.fallen;
        unsigned readyChoices = 0;
        id = buildMove(o, legal, rate, seed, arriving, chainRuns, &readyChoices, quietRivalLanding);
        int covered = id;
        if (kind == Build && readyChoices && lv38PartEnabled(p.lv38ReadyCover, Lv38Part::ReadyCover)) {
            traceLv38Part(Lv38Part::ReadyCover);
            covered = lv38_ready_cover::prefer(o, mine, theirs, id, readyChoices, p, seed,
                                               [&] { return exchangeOutOfTime(96); });
            recordLv38Part(o, "ready_cover", id, covered, arriving, kind);
        }
        if (covered != id || !(kind == Build || kind == AnswerBuild)) return covered;
        return (arriving || retained) ? checkTwoWave(id, arriving) : id;
    }

    // Win: against every reply some plan below the same first placement wins (the opponent's side of every
    // exchange: standing still, and answering with pairs not on the screen, then each of his plans). -1: none.
    int earlierWinFire(const Observation& o, int baseline, unsigned winning, int rate, uint64_t seed, Rival& rival) {
        metrics::Scope scope(metrics::Part::EarlierWinFire);
        const auto stop = [&] { return exchangeOutOfTime(85); };
        const auto shown = [&](int who) {
            const auto& player = o.player[who];
            return o.pairsOf(who).count() - player.nextPair + (player.phase == Phase::Control);
        };
        if (!winning || !p.x3WinCheck || p.handWait || stop() ||
            o.self().incoming.total() || o.opponent().incoming.total() || chainRunning(o, 1 - o.me) ||
            o.self().scoring.allClear || o.opponent().scoring.allClear ||
            mine.pairCount != shown(o.me) || theirs.pairCount != shown(1 - o.me) ||
            !((o.self().phase == Phase::Next && !o.busyLeft[o.me]) || o.self().phase == Phase::Control)) return baseline;

        const Plan* delayed = nullptr;
        for (const auto* list : {&mine.plans, &mine.tactical, &mine.keeping}) {
            for (const Plan& plan : *list) {
                if (plan.id[0] != baseline || !plan.fireDepth) continue;
                if (plan.fireDepth == 1 || plan.allClear) return baseline;
                if (!delayed || plan.sent > delayed->sent || (plan.sent == delayed->sent && plan.endFrame < delayed->endFrame))
                    delayed = &plan;
            }
        }
        if (!delayed || delayed->chains < 5) return baseline;

        const Plan* earlier = nullptr;
        for (const Plan& fire : mine.plans) {
            if (!(winning >> fire.id[0] & 1u) || fire.count != 1 || fire.fireDepth != 1 || fire.allClear ||
                fire.chains < 5 || fire.sent < delayed->sent || fire.endFrame >= delayed->endFrame) continue;
            if (!earlier || fire.endFrame < earlier->endFrame || (fire.endFrame == earlier->endFrame && fire.sent > earlier->sent))
                earlier = &fire;
        }
        if (!earlier || stop()) return baseline;
        const Duel alternative = playOut(o, mine, earlier, theirs, nullptr, p, seed, true);
        const Duel regular = playOut(o, mine, delayed, theirs, nullptr, p, seed, true);
        const auto& quick = alternative.player[o.me];
        const auto& late = regular.player[o.me];
        const int sent = alternative.player[1 - o.me].incoming.confirmed;
        if (quick.fallen || quick.chain.chains != earlier->chains || quick.moves != o.self().moves + 1 ||
            late.fallen || late.chain.chains != delayed->chains || late.moves != o.self().moves + delayed->count ||
            quick.spawnFrame >= late.spawnFrame || o.rules.rateAt(quick.spawnFrame) != rate ||
            sent < delayed->sent || sent < regular.player[1 - o.me].incoming.confirmed ||
            stop() || !outlasts(o, earlier->id[0], rate, seed, rival, earlier)) return baseline;
        return knownWinFire(o, mine, theirs, *earlier, p, seed, alternative, stop) ? earlier->id[0] : baseline;
    }

    int win(const Observation& o, unsigned legal, int rate, uint64_t seed, Rival* shared = nullptr) {
        metrics::Scope scope(metrics::Part::Win);
        const int me = o.me;
        Rival local;
        Rival& rival = shared ? *shared : local;
        int worst[PLACEMENTS];
        // (x3WinBuild) the same of the plans that only build: the round is won below that placement whatever
        // the opponent does and without firing anything more - it is won already.
        int wonIdle[PLACEMENTS];
        unsigned has = 0;
        for (const Plan& s : mine.plans) has |= 1u << s.id[0];
        for (int r = 0; r < replies(); ++r) {
            bool wins[PLACEMENTS]{}, quiet[PLACEMENTS]{};
            for (const Plan& s : mine.plans) {
                const int id = s.id[0];
                const bool building = p.x3WinBuild && !s.fireDepth && !quiet[id] && (r == 0 || wonIdle[id]);
                if ((wins[id] || (r && !worst[id])) && !building) continue;
                const Duel d = playOut(o, mine, &s, theirs, reply(r), p, seed);
                if (duelValue(d, me, o, p, rate, seed) >= -(VALUE_LOST / 2)) {
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
                if ((has >> id & 1) && (legal >> id & 1) && worst[id] && wonIdle[id]) won |= 1u << id; // (no chain is fired: x3WinCheck has none to read)
            if (won) {
                const int id = won & (won - 1) ? build(o, won, 0) : -1;
                return kind = Win, id >= 0 ? id : std::countr_zero(won);
            }
        }
        for (int id = 0; id < PLACEMENTS; ++id) {
            if (!(has >> id & 1) || !(legal >> id & 1) || !worst[id]) continue;
            if (p.x3WinCheck && !outlasts(o, id, rate, seed, rival)) continue;
            kind = Win;
            if (!p.readMidgame || !p.checkKnownExchange) return id;
            unsigned winning = 0;
            for (int other = id + 1; other < PLACEMENTS; ++other)
                if ((has >> other & 1) && (legal >> other & 1) && worst[other]) winning |= 1u << other;
            if (!lv37PartEnabled(p.lv37EarlierWinFire, Lv37Part::EarlierWinFire)) return id;
            const int earlier = earlierWinFire(o, id, winning, rate, seed, rival);
            return recordLv37Part(o, "earlier_win_fire", id, earlier, o.self().incoming.total(), kind);
        }
        return -1;
    }

    // An all clear the pair in hand gets by a chain of x3ClearNow waves or fewer is taken,
    // whatever is on its way (an all clear by a chain of one is always taken; a
    // human who misses one by a chain of two or fewer stands out). Seen: the rule below
    // stood behind the answer, and with garbage on its way the all clear was let go. -1: none.
    int clearNow(unsigned legal) {
        const Plan* clear = nullptr;
        for (const Plan& s : mine.plans) {
            if (!s.allClear || s.fireDepth != 1 || s.chains > p.x3ClearNow || !(legal >> s.id[0] & 1)) continue;
            if (!clear || s.points > clear->points) clear = &s;
        }
        if (clear) return kind = Clear, clear->id[0];
        return -1;
    }

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
        // What the surplus of an answer is counted from: what is above the opponent's field before it (aboveOther).
        int aboveHim = 0;
    };
    AnswerBase answerBase(const Observation& o) const {
        AnswerBase a{p};
        a.strict.evKeyed = 0, a.strict.trialMaxAdd = 1;
        a.aboveHim = aboveOther(o, o.me, p);
        if (p.evKeyed) buildValue(o.self().field, o.rules, o.timing, p, &a.mainKeyed);
        int own[W];
        o.self().field.heights(own);
        a.tight = own[DEATH_X] >= 11, a.curve = p.x3FallCurve && !a.tight;
        return a;
    }
    // What plan `s` is worth as an answer, in the Duel it was played out in (against the chain of the
    // opponent that is running; with x3Guard, against a plan of his that has not begun): what falls
    // (cost), the main chain it keeps (main), and the score. False: the field closes.
    bool answerWorth(const Observation& o, const Plan& s, const Duel& d, const AnswerBase& base, int rate, int& cost, int& main, int64_t& score) const {
        const int me = o.me;
        const DuelPlayer& mineAfter = d.player[me];
        if (mineAfter.phase == DuelPhase::Dead) return false;
        cost = mineAfter.fallen + mineAfter.incoming.total();
        const int surplus = d.player[1 - me].fallen + d.player[1 - me].incoming.total() - base.aboveHim;
        main = s.mainAfter;
        if (mineAfter.fallen > 0) // the garbage may have buried it
            readMainChain(mineAfter.field, o.rules, o.timing, p.x3RowStrict && mineAfter.fallen >= W ? base.strict : p, &main);
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
    }

    // x3AnswerFire. Attacked on a field that is nearly full: the answer that keeps the main chain is
    // one more pair on that field, and the next attack, or a pair of the wrong colours, closes it.
    // (Seen: a chain of fifteen that the pair in hand fired, on 66 to 71 cells with four garbage
    // pieces on their way, kept through four moves for small clears; then no pair fired it.) -1: not fired.
    int fireAnswer(const Observation& o, unsigned legal, int rate, uint64_t seed) {
        metrics::Scope scope(metrics::Part::FireAnswer);
        int most = 0;
        const Plan* now = nullptr;
        for (const Plan& s : mine.plans) {
            if (!s.fireDepth || !(legal >> s.id[0] & 1)) continue;
            if (s.points > most) most = s.points;
            if (s.fireDepth == 1 && (!now || s.points > now->points)) now = &s;
        }
        if (now && most >= p.x3ChokeMin * rate && atLeastPercent(now->points, p.x3ChokeKeep, most) && heHoldsLittle(o, rate, now->points)) {
            const Duel d = playOut(o, mine, now, theirs, nullptr, p, seed);
            const DuelPlayer& after = d.player[o.me];
            if (after.phase != DuelPhase::Dead && after.fallen + after.incoming.total() == 0) return kind = Fire, now->id[0];
        }
        return -1;
    }

    // Against a fixed chain that would close the field on the first drop, count
    // queues that cancel it before preferring a surplus.
    unsigned fullOffsetChoices(const Observation& o, const Duel& afterChain, const BuildResult& b,
                               unsigned legal, unsigned ordinary, int arriving, int rate, int64_t surplusGoal,
                                uint64_t seed) {
        metrics::Scope scope(metrics::Part::FullOffsetNext);
        const int me = o.me, him = 1 - me;
        const auto& after = afterChain.player[me];
        const auto& rival = afterChain.player[him];
        if (!p.readMidgame || p.x3Enough <= 100 || p.handWait || b.cut || b.told != p.queues() ||
            !chainShown(o.opponent()) || o.self().incoming.confirmed || o.opponent().incoming.total() ||
            o.self().scoring.allClear || o.opponent().scoring.allClear ||
            (o.self().phase != Phase::Control && (o.self().phase != Phase::Next || o.busyLeft[me])) ||
            afterChain.state != RoundState::Playing || after.fallen || after.incoming.confirmed != arriving ||
            after.incoming.pending || rival.phase != DuelPhase::Control || rival.fallen || rival.scoring.allClear ||
            exchangeOutOfTime(75)) return ordinary;

        int heights[W];
        o.self().field.heights(heights);
        if (heights[DEATH_X] + std::min(arriving, o.rules.maxGarbagePerDrop) / W <= DEATH_Y) return ordinary;
        const int64_t confirm = rival.spawnFrame - o.timing.nextFrames;
        if (confirm <= o.frame) return ordinary;
        const int64_t need = int64_t(arriving) * rate - o.self().scoring.pool;
        if (need <= 0) return ordinary;
        for (int64_t f = o.frame; f <= confirm; f = (f / FPS + 1) * FPS)
            if (o.rules.rateAt(f) != rate) return ordinary;
        if (o.rules.rateAt(confirm) != rate) return ordinary;

        const unsigned full = enoughChoices(b, legal, need);
        if ((ordinary & full) == ordinary) return ordinary;
        // Keep the normal preference for an immediate fire.
        // The alternate ranking applies to quiet builds.
        if (quietSetupChoices(o, mine, ordinary) != ordinary) return ordinary;
        // Keep all-clear routes under the normal answer selection.
        for (const auto* list : {&mine.plans, &mine.tactical})
            if (std::any_of(list->begin(), list->end(), [](const Plan& plan) { return plan.allClear; })) return ordinary;
        unsigned ready = 0;
        for (unsigned choices = full & b.alive; choices; choices &= choices - 1) {
            if (exchangeOutOfTime(75)) return ordinary;
            const int id = std::countr_zero(choices);
            if (b.enough(id, need) <= 0) continue;
            Plan first;
            first.count = 1;
            first.id[0] = int8_t(id);
            const Duel d = playOut(o, mine, &first, theirs, nullptr, p, seed, true);
            const auto& own = d.player[me];
            const auto& foe = d.player[him];
            const bool fromReady = p.lv38Midgame && lv38PartEnabled(p.lv38FullOffsetNext, Lv38Part::FullOffsetNext);
            const auto readyAt = own.spawnFrame - (fromReady ? o.timing.nextFrames : 0);
            if (d.state != RoundState::Playing || own.moves != o.self().moves + 1 || own.phase != DuelPhase::Control ||
                own.chain.chains || own.scoring.allClear || own.fallen || own.spawnFrame < 0 ||
                own.incoming.confirmed != arriving || own.incoming.pending ||
                foe.chain.chains != rival.chain.chains || foe.spawnFrame != rival.spawnFrame) continue;
            const auto readyBefore = [&](int64_t at) {
                return at < confirm && p.movesIn(confirm - at, own.field.f.count(), handWait) >= b.depth - 1;
            };
#ifdef JURAIC_LV38_REPLAY_AUDIT
            if (!readyBefore(own.spawnFrame) && readyBefore(own.spawnFrame - o.timing.nextFrames)) auditBranches |= AuditOffset;
#endif
            if (fromReady) traceLv38Part(Lv38Part::FullOffsetNext);
            if (!readyBefore(readyAt)) continue;
            ready |= 1u << id;
        }
        return ready && !exchangeOutOfTime(75) ? enoughChoices(b, ready, surplusGoal) : ordinary;
    }

    #include "known-prefix-timing.h"

    // Answer: `arriving` garbage pieces are on their way (`afterChain`: the observation played to the end of his
    // chain, when chainRuns says that one runs). The plans are played with the opponent's chain going on and no
    // more from him. -1: every plan closes the field.
    int answer(const Observation& o, unsigned legal, int rate, uint64_t seed, const Duel& afterChain, bool chainRuns, int arriving) {
        metrics::Scope scope(metrics::Part::Answer);
        const int me = o.me;
        const Plan* best = nullptr;
        int64_t bestScore = 0;
        int bestCost = 0;
        int builds[PLACEMENTS]; // what falls after the plan that builds, by first placement; -1 = no such plan
        int64_t buildScore[PLACEMENTS]{}; // ... and what that plan scores as an answer
        int buildMain[PLACEMENTS]{};      // ... and the main chain it keeps
        for (int& c : builds) c = -1;
        const AnswerBase base = answerBase(o);
        // Keep the best immediate full offset under the ordinary answer score.
        const Plan* clean = nullptr;
        int64_t cleanScore = 0;
        const auto checkHold = [&](int id) {
            if (!clean || !lv37PartEnabled(p.lv37ProtectAnswer, Lv37Part::ProtectAnswer)) return id;
            const int safe = protectAnswer(o, id, legal, seed, *clean);
            if (safe != id) kind = Answer;
            return recordLv37Part(o, "protect_answer", id, safe, arriving, kind);
        };
        Rival answering; // (x3AnswerOutlast: the search of his field, made once)
        for (const auto* list : {&mine.plans, &mine.tactical}) for (const Plan& s : *list) {
            if (list == &mine.tactical && !(legal >> s.id[0] & 1u)) {
#ifdef JURAIC_LV38_REPLAY_AUDIT
                auditBranches |= AuditLegal;
#endif
                if (p.lv38Midgame && lv38PartEnabled(p.lv38AnswerLegal, Lv38Part::AnswerLegal)) {
                    metrics::Scope answerLegalScope(metrics::Part::AnswerLegal);
                    traceLv38Part(Lv38Part::AnswerLegal);
                    continue;
                }
            }
            const bool concrete = p.readMidgame && list == &mine.tactical && s.fireDepth > 1;
            const auto completed = [&](const Duel& replay) {
                const auto& own = replay.player[me];
                return !concrete || (own.moves == o.self().moves + s.count && own.chain.chains == s.chains && !own.scoring.allClear);
            };
            if (concrete && exchangeOutOfTime(75)) break;
            const Duel d = playOut(o, mine, &s, theirs, nullptr, p, seed, concrete);
            if (!completed(d)) continue;
            int cost = 0, main = 0;
            int64_t score = 0;
            bool lives = answerWorth(o, s, d, base, rate, cost, main, score);
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
                    const Duel other = playOut(o, mine, &s, theirs, nullptr, p, seed + uint64_t(k) * 7919, concrete);
                    if (!completed(other)) continue;
                    int c = 0, m = 0;
                    int64_t sc = 0;
                    if (!answerWorth(o, s, other, base, rate, c, m, sc)) continue;
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
            bool surplusTaken = false; // (the credit answerWorth gives the surplus is taken off once at most)
            if (p.readMidgame && cost == 0 && s.sent >= W && s.fireDepth == 1 && !s.allClear && knownCounter(o, s, seed)) {
                // A surplus he demonstrably offsets cannot justify spending more of our field.
                const int surplus = d.player[1 - me].fallen + d.player[1 - me].incoming.total() - base.aboveHim;
                score -= int64_t(p.x3Surplus) * std::max(0, surplus);
                surplusTaken = true;
            }
            if (!surplusTaken && p.x3AnswerOutlast && s.fireDepth && base.mainKeyed > 0 && !atLeastPercent(main, p.x3KeepMain, base.mainKeyed)) {
                // The main chain is spent on this answer. What it leaves above him is a gain only when he
                // cannot answer it: else he fires second, with the chain he builds on while this one runs.
                // (outlasts of this plan itself, not of the largest one with the same first placement)
                const int surplus = d.player[1 - me].fallen + d.player[1 - me].incoming.total() - base.aboveHim;
                if (surplus > 0 && !outlasts(o, s.id[0], rate, seed, answering, &s)) score -= int64_t(p.x3Surplus) * surplus;
            }
            if (p.checkKnownExchange && arriving >= W && s.fireDepth == 1 && s.count == 1 && !s.allClear &&
                (legal >> s.id[0] & 1) && cost == 0 && (!clean || score > cleanScore)) clean = &s, cleanScore = score;
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
                        if (builds[i] >= 0 && atLeastPercent(buildMain[i], p.x3ExtendKeep, most)) kept |= 1u << i;
                    if (most >= p.x3ExtendKeepMin * rate && (legal & kept)) keeping = legal & kept;
                }
                // x3Enough. The sums are the mean of what the queues fire in time, and a chain is not worth what
                // it has beyond the garbage on its way: first the placements below which the most queues fire a
                // chain worth that share of it, and the sums among those.
                bool fullOffsetRankChanged = false;
                if (p.x3Enough > 0) {
                    const int64_t goal = int64_t(arriving) * rate * p.x3Enough / 100;
                    const unsigned ordinary = enoughChoices(b, keeping, goal);
                    keeping = !clean && bestCost > 0 && !best->allClear
                        ? fullOffsetChoices(o, afterChain, b, keeping, ordinary, arriving, rate, goal, seed)
                        : ordinary;
                    fullOffsetRankChanged = keeping != ordinary;
                }
                const int id = pickBuild(o, b, keeping);
                // (When no chain that offsets what is coming is found in time, the choice of
                // that short search means nothing: the answer below is taken instead.)
                const int queues = p.queues();
                const bool inTime = !p.x3ExtendCheck || (id >= 0 && b.sum[id] / queues >= int64_t(arriving) * rate);
                if (id >= 0 && inTime) {
                    if (p.checkKnownExchange && !clean && bestCost > 0 && !best->allClear && !fullOffsetRankChanged &&
                        lv37PartEnabled(p.lv37KnownPrefix, Lv37Part::KnownPrefix)) {
                        const auto expired = [&] { return exchangeOutOfTime(75); };
                        const int corrected = measurePart<metrics::Part::KnownPrefix>([&] {
                            return correctKnownPrefix(o, afterChain, b, legal, id, arriving, rate, seed, expired);
                        });
                        if (corrected >= 0) {
                            recordLv37Part(o, "known_prefix", id, corrected, arriving, AnswerBuild);
                            return kind = AnswerBuild, checkHold(corrected);
                        }
                    }
                    return kind = AnswerBuild, checkHold(id);
                }
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
            if (id >= 0) return kind = Answer, checkHold(id);
        }
        if (best) {
            const int held = best->allClear ? best->id[0] : checkHold(best->id[0]);
            // An Answer uses the requested orientation, even if the first pair is monochrome.
            if (held == best->id[0] && bestCost == 0 && chainRuns && tailInput.canCompare() &&
                lv37PartEnabled(p.lv37AnswerWindow, Lv37Part::AnswerWindow)) {
                std::optional<PictureRoutes> routes;
                tailInput.configure(routes);
                const int alternative = measurePart<metrics::Part::AnswerWindow>([&] {
                    return answer_window::prefer(o, mine, theirs, *best, clean, afterChain, legal, arriving, p, seed,
                                                 [&] { return exchangeOutOfTime(85); }, routes ? &*routes : nullptr);
                });
                if (alternative >= 0) return kind = Answer, recordLv37Part(o, "answer_window", held, alternative, arriving, kind);
            }
            return kind = Answer, held;
        }
        return -1;
    }

    // Verify the chosen hold and the alternative against the attack already in progress.
    // For a partial random row with no observed order we cannot know the next field.
    int protectAnswer(const Observation& o, int hold, unsigned legal, uint64_t seed, const Plan& fire) const {
        metrics::Scope scope(metrics::Part::ProtectAnswer);
        const int me = o.me;
        if (hold < 0 || fire.id[0] == hold || !(legal >> fire.id[0] & 1) || mine.pairCount < 2 || p.handWait > 0 ||
            !((o.self().phase == Phase::Next && o.busyLeft[me] == 0) || o.self().phase == Phase::Control)) return hold;
        Plan one;
        one.count = 1;
        one.id[0] = int8_t(hold);
        const Duel waiting = playOut(o, mine, &one, theirs, nullptr, p, seed, true);
        const DuelPlayer& after = waiting.player[me];
        if (after.moves != o.self().moves + 1 || after.chain.chains || after.fallen < W ||
            (after.fallen % W && !o.nextGarbageOrder[me])) return hold;
        if (std::any_of(mine.plans.begin(), mine.plans.end(), [&](const Plan& s) { return s.id[0] == hold && s.allClear; })) return hold;
        if (after.phase != DuelPhase::Dead) {
            if (waiting.state != RoundState::Playing || after.phase != DuelPhase::Control) return hold;
            // An empty superset of the next pair's firing placements proves it cannot fire.
            const unsigned places = legalPlacements(after.field);
            for (int id = 0; id < PLACEMENTS; ++id) {
                if (!(places >> id & 1)) continue;
                GameField next = after.field;
                juraic::place(next, placement(id), mine.pair[1]);
                if (anyClear(next.f, o.rules)) return hold;
            }
        }
        const Duel firing = playOut(o, mine, &fire, theirs, nullptr, p, seed, true);
        const DuelPlayer& saved = firing.player[me];
        return firing.state == RoundState::Playing && saved.phase == DuelPhase::Control && saved.moves == o.self().moves + 1 &&
               saved.chain.chains && !saved.fallen && !saved.incoming.total() ? fire.id[0] : hold;
    }

    // An all clear within reach of the pairs on the screen, by a chain of x3AllClear waves or
    // fewer (with a chain of two or fewer it is never missed, three depends, four or
    // more spent on it is a bad move). Taken now when this pair gets it; else the placement
    // that leads to it. Nothing is on its way. -1: none.
    int clearInReach(const Observation& o, unsigned legal) {
        const int cells = o.self().field.f.count();
        const Plan* clear = nullptr;
        for (const Plan& s : mine.plans) {
            if (!s.allClear || s.chains > p.x3AllClear || !(legal >> s.id[0] & 1)) continue;
            if (s.chains >= 3 && cells + 2 * s.fireDepth > p.x3ClearThree) continue; // too late for so small a chain
            if (!clear || s.fireDepth < clear->fireDepth || (s.fireDepth == clear->fireDepth && s.points > clear->points)) clear = &s;
        }
        if (clear) return kind = Clear, clear->id[0];
        return -1;
    }

    // Poke: a chain that leaves the main chain whole and gains against his best answer. Nothing is on its way.
    // -1: none.
    int poke(const Observation& o, unsigned legal, int rate, uint64_t seed) {
        metrics::Scope scope(metrics::Part::LegacyPoke);
        const int me = o.me;
        // (x3PokeFrom: no poke yet on a field of fewer cells. Nothing of what follows is needed then - the own field and
        // the position valued, his field searched - unless x3PokeAt may fire one whatever the own field holds.)
        if (p.x3PokeFrom > 0 && o.self().field.f.count() < p.x3PokeFrom && !(p.x3PokeAt > 0 && o.opponent().field.f.count() >= p.x3PokeAt)) return -1;
        // The three chains this part compares, named by what they count: mainKeyed and hisKeyed are the main chains
        // as buildValue sees them, a key piece that is still missing counted in; hisDirect is what he can fire as
        // his field stands - a trial drop without a key piece, or one of his pairs on the screen.
        int mainKeyed = 0;
        readMainChain(o.self().field, o.rules, o.timing, p, &mainKeyed);
        const int now = positionValue(o.self(), o.opponent(), o.rules, o.timing, p, rate, seed);
        if (p.x3PokeAt > 0 && o.opponent().field.f.count() >= p.x3PokeAt) {
            // x3PokeAt: his field is nearly full. Whatever he could answer with, he has no room to take it.
            const Plan* poke = nullptr;
            for (const Plan& s : mine.plans) {
                if (s.fireDepth != 1 || s.sent < p.x3MinPoke || !atLeastPercent(s.mainAfter, p.x3PokeKeep, mainKeyed) || !(legal >> s.id[0] & 1)) continue;
                if (!poke || s.sent > poke->sent) poke = &s;
            }
            if (poke) return kind = Poke, poke->id[0];
        }
        const Plan* best = nullptr;
        int bestGain = 0;
        bool bestDiscounted = false;
        // What the opponent can fire at a poke: the largest chain a trial drop starts on his field, and how long it runs.
        int hisDirect = 0, hisFrames = 0;
        if (p.x3PokeRead || p.x3PokeUnanswered || p.x3PokeNet == 3) {
            Trials trials;
            detect(o.opponent().field, o.rules, o.timing, p.trialMaxAdd, trials);
            for (int i = 0; i < trials.count; ++i)
                if (trials.item[i].chains >= o.rules.chainRequirement && trials.item[i].points > hisDirect)
                    hisDirect = trials.item[i].points, hisFrames = trials.item[i].frames + o.timing.landFrames + o.timing.nextFrames + 1; // (as a plan's endFrame - lockFrame: from the lock)
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
            readMainChain(o.opponent().field, o.rules, o.timing, p, &hisKeyed);
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
            const GameField& after = d.player[me].field;
            int64_t mineThen = s.mainAfter;
            if (mine.knownCount > 1) mineThen = std::max(mineThen, meanBest(searchOn(o, after, movesUpTo(hisFrames, after.f.count(), handWait, p.depth()), seed)));
            return mineThen + int64_t(s.sent) * rate >= hisDirect;
        };
        // x3PokeNet: V after reply r of his without the poke. 1: X3 places nothing (as it places nothing
        // after the poke); 2: the best V any of its plans reaches against that reply. Counted once a poke asks.
        std::vector<int> spared;
        const auto without = [&](int r) {
            if (spared.empty()) spared.assign(size_t(replies()), INT32_MIN);
            int& v = spared[size_t(r)];
            if (v != INT32_MIN) return v;
            if (p.x3PokeNet == 1) {
                const Duel d = playOut(o, mine, nullptr, theirs, reply(r), p, seed);
                return v = duelValue(d, me, o, p, rate, seed);
            }
            for (const Plan& a : mine.plans) {
                const Duel d = playOut(o, mine, &a, theirs, reply(r), p, seed);
                const int w = duelValue(d, me, o, p, rate, seed);
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
                if (s.fireDepth != 1 || s.sent < p.x3MinPoke || !atLeastPercent(s.mainAfter, p.x3PokeKeep, mainKeyed)) continue;
                if (!(legal >> s.id[0] & 1) || (best && s.sent <= best->sent)) continue;
                bool answered = false;
                if (ownCells < p.x3PokeFull)
                    for (const Plan& x : theirs.plans) {
                        if (!x.fireDepth || !atLeastPercent(x.mainAfter, p.x3PokeKeep, hisKeyed)) continue;
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
            if (s.fireDepth != 1 || s.sent < p.x3MinPoke || !atLeastPercent(s.mainAfter, p.x3KeepMain, mainKeyed)) continue;
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
                const int v = duelValue(d, me, o, p, rate, seed);
                if (p.x3PokeNet == 3) {
                    // He gives up his main chain to fire at the poke, or the reading (in which X3 places
                    // nothing after the poke) ends lost: when X3, building on while his chain runs and
                    // firing second, offsets it, that reply does not count against the poke.
                    const Plan* x = reply(r);
                    if ((x && x->fireDepth && !atLeastPercent(x->mainAfter, p.x3PokeKeep, hisKeyed)) || v <= VALUE_LOST / 2) {
                        if (covered < 0) covered = offsetsHisMain(s) ? 1 : 0;
                        if (covered) return false;
                    }
                }
                g = v - (p.x3PokeNet == 1 || p.x3PokeNet == 2 ? without(r) : now); // (1, 2: against the same reply without the poke)
                return true;
            };
            for (int r = 0; r < replies(); ++r) {
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
            if (gain >= p.x3PokeGain && (!best || gain > bestGain))
                best = &s, bestGain = gain, bestDiscounted = covered > 0;
        }
        if (best && (legal >> best->id[0] & 1)) {
            if (lv37PartLog) ++knownPokeCalls;
            const int hold = known_poke::prefer(o, legal, rate, seed, p, mine, theirs, *best, roomPairs(o.self().field),
                                              bestDiscounted, hisKeyed, [&] { return exchangeOutOfTime(75); });
            if (hold >= 0) return kind = Build, hold;
            return kind = Poke, best->id[0];
        }
        return -1;
    }

    // Exchange poke (Params::pokeExchange): a small chain, fired when each of his three ways of meeting it is read from
    // his field (as it stands when he next holds a pair) and none is bad for the own side. -1: none.
    //  (1) he offsets it with a small chain that keeps his main chain: no plan of his on the screen that fires and keeps
    //      x3PokeKeep percent of his main chain leaves less than pokeExchangeMin above his field or sends garbage back,
    //      and the chance that the chains standing on his field do so with the pairs he places before the own chain is
    //      confirmed is below pokeExchangeAnswer;
    //  (2) he fires his main chain at it: what the own field builds while his chain runs, with the garbage of the
    //      chain, reaches pokeExchangeMargin percent of his main chain and leaves pokeExchangeLeft garbage or more;
    //  (3) he takes it: his field holds pokeExchangeRoom cells or more with what lands.
    // A chain of a later pair on the screen (pokeExchangeDepth) is built towards by the first placement of its plan.
    int exchangePoke(const Observation& o, unsigned legal, int rate, uint64_t seed) {
        metrics::Scope scope(metrics::Part::ExchangePoke);
        const int me = o.me, him = 1 - me;
        if (!p.pokeExchange || !rivalBeamWhole || p.handWait != 0 || !theirs.pairCount || o.self().incoming.total() || o.opponent().incoming.total() ||
            chainRunning(o, him) || o.self().scoring.allClear || o.opponent().scoring.allClear || exchangeOutOfTime(75)) return -1;
        ++exchangeCounts.asked;
        int mainKeyed = 0;
        readMainChain(o.self().field, o.rules, o.timing, p, &mainKeyed);
        const int depthMost = std::clamp(p.pokeExchangeDepth, 1, 3);
        // (a plan whose chain a later pair fires: the pairs before it must clear nothing)
        const auto clearsBefore = [&](const Plan& s) {
            GameField f = o.self().field;
            for (int k = 0; k + 1 < s.fireDepth; ++k) {
                juraic::place(f, placement(s.id[k]), mine.pair[k]);
                if (anyClear(f.f, o.rules)) return true;
            }
            return false;
        };
        std::vector<const Plan*> chains;
        for (const auto* list : {&mine.plans, &mine.tactical})
            for (const Plan& s : *list) {
                if (s.fireDepth < 1 || s.fireDepth > depthMost || s.count != s.fireDepth || s.chains < 1 || s.chains > 3 || s.allClear ||
                    s.sent < p.pokeExchangeMin || !(legal >> s.id[0] & 1) || !atLeastPercent(s.mainAfter, p.pokeExchangeKeep, mainKeyed)) continue;
                if (s.fireDepth > 1 && clearsBefore(s)) continue;
                chains.push_back(&s);
            }
        if (chains.empty()) return ++exchangeCounts.none, -1;
        std::stable_sort(chains.begin(), chains.end(), [](const Plan* a, const Plan* b) {
            if (a->fireDepth != b->fireDepth) return a->fireDepth < b->fireDepth;
            if (a->mainAfter != b->mainAfter) return a->mainAfter > b->mainAfter;
            return a->sent > b->sent;
        });
        // His field as he next holds a pair, the chains that stand on it and his main chain.
        Duel before(o, seed);
        if (before.player[him].phase != DuelPhase::Control && !toPairOf(before, him, o.frame + 6000)) return ++exchangeCounts.lands, -1;
        const GameField& field = before.player[him].field;
        Replies standing;
        repliesOf(field, o.rules, o.timing, p, standing);
        int hisKeyed = 0;
        readMainChain(o.opponent().field, o.rules, o.timing, p, &hisKeyed);
        // His main chain as he can fire it (points) and how long it runs: a trial drop, or a plan of his on the screen.
        int64_t his = 0, hisFrames = 0, hisEnd = -1; // (hisEnd: Plan::endFrame of his plan on the screen; -1: a trial drop)
        Trials trials;
        detect(o.opponent().field, o.rules, o.timing, p.trialMaxAdd, trials);
        for (int i = 0; i < trials.count; ++i)
            if (trials.item[i].chains >= o.rules.chainRequirement && trials.item[i].points > his)
                his = trials.item[i].points, hisFrames = trials.item[i].frames + o.timing.landFrames + o.timing.nextFrames + 1, hisEnd = -1; // (as a plan's endFrame - lockFrame: from the lock)
        std::vector<const Plan*> keepers; // his plans that fire and keep his main chain
        std::vector<const Plan*> partials;
        for (const auto* list : {&theirs.plans, &theirs.tactical})
            for (const Plan& x : *list) {
                if (!x.fireDepth) continue;
                if (int64_t(x.sent) * rate > his) his = int64_t(x.sent) * rate, hisFrames = x.endFrame - x.lockFrame, hisEnd = x.endFrame;
                if (atLeastPercent(x.mainAfter, p.x3PokeKeep, hisKeyed)) keepers.push_back(&x);
                else if (p.lv38Midgame && lv38PartEnabled(p.lv38PartialVeto, Lv38Part::PartialVeto) &&
                         x.count == x.fireDepth && x.chains >= 1 && x.chains <= 4 && x.sent > 0)
                    partials.push_back(&x);
            }
        const int waiting = o.opponent().incoming.total();
        const auto reaches = [&](const DuelPlayer& q) { return q.fallen + q.incoming.total() - waiting; };
        KnownReplay replay;
        replay.stop = [&] { return exchangeOutOfTime(75); };
        const auto run = [&](const Plan* attack, const Plan* response) {
            return playOut(o, mine, attack, theirs, response, p, seed, true);
        };
        for (const Plan* s : chains) {
            if (exchangeOutOfTime(75)) return ++exchangeCounts.late, -1;
            const Duel quiet = run(s, nullptr);
            const int lands = reaches(quiet.player[him]);
            const int64_t confirm = quiet.player[me].spawnFrame - o.timing.nextFrames;
            if (!pressureOwnIntact(o, *s, quiet) || lands < p.pokeExchangeMin || confirm < o.frame) {
                ++exchangeCounts.lands;
                continue;
            }
            // (1) A small chain of his on the screen.
            bool offset = false;
            for (const Plan* x : keepers) {
                if (exchangeOutOfTime(75)) return ++exchangeCounts.late, -1;
                const Duel d = run(s, x);
                const DuelPlayer& own = d.player[me];
                if (reaches(d.player[him]) < p.pokeExchangeMin || own.fallen || own.incoming.total() || own.phase == DuelPhase::Dead) {
                    offset = true;
                    break;
                }
            }
            // The legacy exchange already accounts for the rival's main
            // chain. A partial reply is an additional veto only when its
            // actual result forces a loss, not because some garbage remains.
            for (const Plan* x : partials) {
                if (offset) break;
                metrics::Scope partialVetoScope(metrics::Part::PartialVeto);
                traceLv38Part(Lv38Part::PartialVeto);
                if (exchangeOutOfTime(75)) return ++exchangeCounts.late, -1;
                const Duel d = playOut(o, mine, s, theirs, x, p, seed, true, nullptr, &replay);
                const DuelPlayer& own = d.player[me];
                if (d.player[him].moves != o.opponent().moves + x->count || !d.player[him].chain.chains) continue;
                if (own.phase != DuelPhase::Dead || !own.dropped || own.fallen != own.dropped ||
                    (own.dropped % W && !o.nextGarbageOrder[me])) continue;
                recordLv38Part(o, "partial_veto", s->id[0], -1, 0, Poke);
                offset = true;
                break;
            }
            if (offset) {
                ++exchangeCounts.smallReply;
                continue;
            }
            // ... or one that stands on his field, fired by the pairs he places before the own chain is confirmed.
            const int turns = repliesUntil(o, before, confirm, field.f.count());
            const int erase = lands - p.pokeExchangeMin + 1;
            const int need = std::max(1, erase * o.rules.rateAt(confirm) - before.player[him].scoring.pool);
            const int keep = int(int64_t(standing.main) * p.x3PokeKeep / 100);
            const int64_t chance = answerChance(standing, need, keep, turns, theirs.pair, theirs.knownCount, o.rules);
            if (chance >= p.pokeExchangeAnswer) {
                ++exchangeCounts.chance;
                continue;
            }
            // (2) His main chain: the own main chain built on while it runs, fired second, with the garbage of this one.
            const GameField& after = quiet.player[me].field;
            const int64_t spare = exchangeSpare(*s, hisFrames, hisEnd);
            const int moves = spare > 0 ? movesUpTo(spare, after.f.count(), handWait, p.depth()) : 0;
            int64_t mineThen = s->mainAfter;
            if (his > 0) {
                if (fallsFirst(*s, spare, his, rate)) {
                    ++exchangeCounts.outdone;
                    continue;
                }
                if (spare > 0 && mine.knownCount > s->count) {
                    if (moves > HORIZONS) {
                        ++exchangeCounts.outdone;
                        continue;
                    }
                    const BuildResult response = searchOn(o, after, moves, seed, s->count);
                    if (response.cut) {
                        ++exchangeCounts.outdone;
                        continue;
                    }
                    mineThen = std::max(mineThen, meanBest(response, moves));
                }
                const int64_t total = mineThen + int64_t(s->sent) * rate;
                if (total * 100 < his * p.pokeExchangeMargin || (total - his) / rate < p.pokeExchangeLeft) {
                    ++exchangeCounts.outdone;
                    continue;
                }
            }
            // (3) He takes it.
            if (field.f.count() + lands < p.pokeExchangeRoom) {
                ++exchangeCounts.taken;
                continue;
            }
            if (exchangeOutOfTime(75)) return ++exchangeCounts.late, -1;
            exchangeNote = {true, o.frame, o.self().moves, s->id[0], s->fireDepth, s->chains, s->sent, his, hisFrames, mineThen, chance,
                            moves, field.f.count(), o.self().field.f.count()};
            ++(s->fireDepth == 1 ? exchangeCounts.fired : exchangeCounts.built);
            return kind = s->fireDepth == 1 ? Poke : CrushBuild, s->id[0];
        }
        return -1;
    }

    unsigned restrictBuildToShelter(int setup, unsigned choices, const MidgameShelter& shelter) const {
        if (!(shelter.safe >> setup & 1u) ||
            !lv38PartEnabled(p.lv38ShelterNarrow, Lv38Part::ShelterNarrow)) return choices;
        metrics::Scope scope(metrics::Part::ShelterNarrow);
        if (choices & ~shelter.safe) traceLv38Part(Lv38Part::ShelterNarrow);
        return choices & shelter.safe;
    }

    // Build: the placement of the beam search, among those the rules below leave it - and, by rules that are switched
    // off, fire. `arriving`: the garbage pieces on their way (an answer was looked for and none lives).
    int buildMove(const Observation& o, unsigned legal, int rate, uint64_t seed, int arriving, bool chainRuns,
                  unsigned* readyChoices = nullptr, bool quietRivalLanding = false) {
        // Near a full field the pairs to come cannot be waited for. The beam search counts on
        // them: its fixed queues bring every colour within two pairs, and a queue in which the
        // field closes costs nothing. (Seen: at 64 cells a chain of twelve could be fired with the
        // pair in hand; the placement chosen gave it up for a larger one that needed two more
        // greens, with room for three pairs, and the field closed.) With x3ChokeFrom, from that
        // many cells on: the largest chain the pairs on the screen can fire is fired when this
        // pair fires it (or nearly: x3ChokeKeep percent); when a later pair on the screen fires
        // a larger one, the placement is chosen among those that keep that.
        unsigned open = legal;
        // x3ChokeRoom. About to choke: the own field has room for the pairs on the screen and x3ChokeRoom more at most.
        // A human moves before his field is that full, and mostly with a poke - anything that does not break much of
        // the main chain, whatever the opponent answers. (Seen in the real game against a person: of eighteen rounds
        // lost, eleven without one chain of two fired, the field full after 29 to 40 seconds and nothing or next to
        // nothing sent at it.) Without a poke the chain in hand is fired first only at an opponent who has room for
        // more than x3ChokeRoom pairs more: one as full must move as soon, and the one who fires second wins.
        const int ownRoom = p.x3ChokeRoom > 0 ? roomPairs(o.self().field) : 0;
        const bool choking = p.x3ChokeRoom > 0 && arriving == 0 && ownRoom <= mine.pairCount + p.x3ChokeRoom;
        if (choking) {
            metrics::Scope scope(metrics::Part::Choke);
            int mainKeyed = 0;
            readMainChain(o.self().field, o.rules, o.timing, p, &mainKeyed);
            const Plan* poke = nullptr;
            for (const Plan& s : mine.plans) {
                if (s.fireDepth != 1 || s.sent < p.x3MinPoke || !atLeastPercent(s.mainAfter, p.x3PokeKeep, mainKeyed) || !(legal >> s.id[0] & 1)) continue;
                if (!poke || s.sent > poke->sent) poke = &s;
            }
            if (poke) return kind = Poke, poke->id[0];
        }
        const bool outwaited = choking && roomPairs(o.opponent().field) > ownRoom + p.x3ChokeRoom;
        if ((p.x3ChokeFrom > 0 && arriving == 0 && o.self().field.f.count() >= p.x3ChokeFrom) || outwaited) {
            metrics::Scope scope(metrics::Part::Choke);
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
            // (outwaited: a chain of any size - the field closes otherwise - and never while a later pair fires it)
            if ((outwaited ? most > 0 : most >= p.x3ChokeMin * rate) && heHoldsLittle(o, rate, most)) {
                // (x3ChokeLast: a later pair on the screen fires it too: no need to fire first yet)
                const bool wait = (p.x3ChokeLast || outwaited) && atLeastPercent(later, p.x3ChokeKeep, most);
                if (!wait && now && atLeastPercent(now->points, p.x3ChokeKeep, most)) return kind = Fire, now->id[0];
                unsigned keep = 0;
                for (int id = 0; id < PLACEMENTS; ++id)
                    if ((legal >> id & 1) && atLeastPercent(under[id], p.x3ChokeKeep, most)) keep |= 1u << id;
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
                const AnswerBase base = answerBase(o);
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
                    if (!answerWorth(o, s, d, base, rate, cost, main, score)) continue;
                    if (!(answered >> s.id[0] & 1) || score > worth[s.id[0]]) worth[s.id[0]] = score;
                    if (!answered || score > most) most = score;
                    answered |= 1u << s.id[0];
                }
                for (int id = 0; id < PLACEMENTS; ++id)
                    if ((answered >> id & 1) && worth[id] >= most - p.x3GuardSlack) guarded |= 1u << id;
            }
        }
        const MidgameShelter shelter = p.readMidgame && arriving == 0 && lv37PartEnabled(p.lv37Shelter, Lv37Part::Shelter)
            ? measurePart<metrics::Part::Shelter>([&] {
                return findMidgameShelter(o, mine, theirs, open, p, seed, [&] { return exchangeOutOfTime(70); }, ahead);
            }) : MidgameShelter{};
        const bool tailBuild = p.lv38Midgame && (p.lv38Plan5 > 0 || p.lv38HeadCut > 0) && !arriving && (!chainRuns || quietRivalLanding) &&
            tailInput.supported();
        int buildPercent = 100;
        if (tailBuild && !p.handWait && !o.self().incoming.total() && !o.opponent().incoming.total() &&
            lv38_tail::inWindow(o.self().field.f.normal().count() + 2, p.lv38CutFrom)) buildPercent = 95;
        const bool cutQueues = tailBuild && p.lv38CutQueues > 0 && lv38_cut_build::quiet(o, p) &&
            lv38_tail::inWindow(o.self().field.f.normal().count(), p.lv38CutFrom);
        const BuildResult b = searchBuild(o, 0, buildPercent, false, cutQueues);
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
            if (big && atLeastPercent(big->points, p.x3FullPercent, future)) return kind = Fire, big->id[0];
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
        const auto withShelter = [&](int id, unsigned choices) {
            if (id < 0 || !(shelter.buried >> id & 1) || !(b.alive & choices & shelter.safe)) return id;
            const int safe = pickBuild(o, b, choices & shelter.safe, true);
            if (safe < 0) return id;
            guardedNow = true;
            return recordLv37Part(o, "shelter", id, safe, arriving, kind);
        };
        const auto pickSetup = [&](int id, unsigned choices) {
            const int sheltered = withShelter(id, choices);
            if (p.readMidgame && p.checkKnownExchange && id >= 0 && sheltered == id && !arriving && !b.cut && !guardedNow) {
                const unsigned allowed = (shelter.safe >> id & 1) ? choices & shelter.safe : choices;
                if (lv37PartEnabled(p.lv37ReadyBuild, Lv37Part::ReadyBuild)) {
                    const int retained = measurePart<metrics::Part::ReadyBuild>([&] {
                        return preserveReadyBuild(o, mine, theirs, id, b.alive & allowed & ~shelter.buried, p, seed,
                                                           [&] { return exchangeOutOfTime(75); });
                    });
                    if (retained != id) return recordLv37Part(o, "preserve_ready_build", id, retained, arriving, kind);
                }
                if (!chainRuns && roomPairs(o.self().field) < mine.pairCount &&
                    lv37PartEnabled(p.lv37PartialBuild, Lv37Part::PartialBuild)) {
                    const int partial = measurePart<metrics::Part::PartialBuild>([&] {
                        return preferKnownPartialBuild(o, mine, theirs, b, id, b.alive & allowed & ~shelter.buried,
                                                                p, seed, rate, [&] { return exchangeOutOfTime(); });
                    });
                    if (partial != id) return recordLv37Part(o, "partial_build", id, partial, arriving, kind);
                }
            }
            if (sheltered != id || id < 0 || !p.readMidgame || (p.setupSideNear <= 0 && p.setupSpeedNear <= 0) ||
                arriving || chainRuns || p.handWait > 0 || o.self().incoming.total() ||
                (o.self().phase != Phase::Control && (o.self().phase != Phase::Next || o.busyLeft[o.me] > 0)) || b.cut) return sheltered;
            const auto stop = [&] { return exchangeOutOfTime(); };
            if (stop() || !(choices >> id & 1) || (shelter.buried >> id & 1)) return id;
            if (shelter.safe >> id & 1) choices &= shelter.safe;
            unsigned setup = quietSetupChoices(o, mine, b.alive & choices & ~shelter.buried);
            if (!(setup >> id & 1)) return id;
            const auto openSetup = [&](unsigned allowed) {
                if (guardedNow || !tailInput.canCompare()) return id;
                if (!lv37PartEnabled(p.lv37SetupSpeed, Lv37Part::SetupSpeed)) return id;
                std::optional<PictureRoutes> routes;
                tailInput.configure(routes);
                const int selected = measurePart<metrics::Part::SetupSpeed>([&] {
                    return preferOpenSetup(o, mine, b, id, allowed, roomPairs(o.self().field), p, stop, routes ? &*routes : nullptr);
                });
                return recordLv37Part(o, "setup_speed", id, selected, arriving, kind);
            };
            if (o.opponent().incoming.total()) return openSetup(setup);
            if (p.setupSideNear > 0 && o.self().field.f.count() >= 24 &&
                lv37PartEnabled(p.lv37Preparation, Lv37Part::Preparation)) {
                metrics::Scope scope(metrics::Part::Preparation);
                setup = preparationChoices(o, mine, theirs, b, id, setup, p, seed, stop, ahead);
                if (!setup || stop()) return id;
            }
            if (!(setup >> id & 1)) {
                const int prepared = measurePart<metrics::Part::Preparation>([&] { return pickBuild(o, b, setup, true); });
                return prepared < 0 || stop() ? id : recordLv37Part(o, "preparation", id, prepared, arriving, kind);
            }
            if (!lv37PartEnabled(p.lv37SetupSpeed, Lv37Part::SetupSpeed)) return id;
            const int chosen = measurePart<metrics::Part::SetupSpeed>([&] {
                return pickFasterSetup(o, mine, b, id, setup, p, stop, namedLv38Ex);
            });
            if (chosen == id) return openSetup(setup);
            const int kept = stop() ? id : measurePart<metrics::Part::SetupSpeed>([&] {
                return keepSetupAnswer(o, mine, theirs, id, chosen, p, seed, stop);
            });
            return recordLv37Part(o, "setup_speed", id, kept, arriving, kind);
        };
        const auto rankTail = [&](int id, unsigned choices) {
            if (!tailBuild || !lv38PartEnabled(p.lv38TailRank, Lv38Part::TailRank)) return id;
            traceLv38Part(Lv38Part::TailRank);
            if (tailInput.spare) {
                const int cells = o.self().field.f.normal().count();
                if (!lv38_tail::inWindow(cells, p.lv38CutFrom) && !lv38_tail::inWindow(cells + 2, p.lv38CutFrom)) return id;
                choices = tailInput.reachable(o, choices & b.alive, [&] { return exchangeOutOfTime(); });
                if (!(choices & (1u << id))) return id;
            }
            const bool timedSum = p.x3Soon > 0 && b.pointsBy[id][0] < p.beamEndPoints;
            if (p.lv38CutAhead > 0 || p.lv38CutQueues > 0) {
                lv38_cut_build::Note note;
                const int picked = lv38_cut_build::prefer(o, mine, b, id, choices, p, timedSum,
                                                          [&] { return exchangeOutOfTime(); }, note);
                if (picked != id) {
                    recordLv38Part(o, note.queues ? "cut_build_queues" : "cut_build_ahead", id, picked, arriving, Build);
                    if (note.preserved) recordLv38Part(o, "cut_kept", id, picked, arriving, Build);
                    if (cutRanks) cutRanks->push_back({o.frame, o.self().moves + 1, id, picked, note.depth,
                                                      note.queues, note.preserved});
                    return picked;
                }
            }
            if (p.lv38Plan5 <= 0) return id;
            countComponent(CountedComponent::TailRank);
            const int ranked = lv38_tail_build::prefer(o, mine, b, id, choices, p, timedSum,
                [&] { return exchangeOutOfTime(); }, lv38BuildNote);
            recordLv38Part(o, "tail_rank", id, ranked, arriving, Build);
            return ranked;
        };
        const auto finishBuild = [&](int id, unsigned choices) {
            const int setup = pickSetup(id, choices);
            if (!p.lv38Midgame || setup < 0 || arriving || guardedNow) return setup;
            const unsigned beforeShelter = choices;
            choices = restrictBuildToShelter(setup, choices, shelter);
            const bool narrowed = choices != beforeShelter;
            const auto finished = [&](int selected) {
                if (narrowed) recordLv38Part(o, "shelter_narrow", setup, selected, arriving, Build);
                return selected;
            };
            const unsigned safe = choices & ~shelter.buried;
            if (chainRuns) {
                if (quietRivalLanding && setup == id && !keepLow && !unsure13 && !unsure14) {
                    const int ranked = rankTail(setup, safe);
                    if (ranked != setup) return finished(ranked);
                    if (readyChoices && !b.cut) *readyChoices = safe & b.alive;
                }
                return setup;
            }
            int threatened = setup;
            if (lv38PartEnabled(p.lv38Threat, Lv38Part::Threat)) {
                traceLv38Part(Lv38Part::Threat);
                threatened = lv38_threat::prefer(o, mine, theirs, b, setup, safe, p, seed,
                                                [&] { return exchangeOutOfTime(97); }, ahead);
                recordLv38Part(o, "threat", setup, threatened, arriving, Build);
            }
            if (threatened != setup || setup != id || keepLow || unsure13 || unsure14) return finished(threatened);
            int foundation = setup;
            if (lv38PartEnabled(p.lv38Foundation, Lv38Part::Foundation)) {
                traceLv38Part(Lv38Part::Foundation);
                foundation = lv38_foundation::prefer(o, mine, theirs, b, setup, safe, p, seed,
                                                     [&] { return exchangeOutOfTime(); });
                recordLv38Part(o, "foundation", setup, foundation, arriving, Build);
            }
            if (foundation != setup) return finished(foundation);
            const int ranked = rankTail(foundation, safe);
            if (ranked != foundation) return finished(ranked);
            if (readyChoices && !b.cut) *readyChoices = safe & b.alive;
            return foundation;
        };
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
                    if (s.fireDepth == 1 && atLeastPercent(s.points, p.x3ChokeKeep, most)) fires |= 1u << s.id[0];
            if ((open & fires) && (open & ~fires)) {
                const int unfired = pickBuild(o, b, open & ~fires, true);
                if (unfired >= 0) return kind = Build, finishBuild(unfired, open & ~fires);
            }
        }
        int id = pickBuild(o, b, open, true);
        if (id < 0 && open != legal) id = pickBuild(o, b, legal, true);
        if (id >= 0) return kind = Build, finishBuild(id, open);
        return (legal >> mine.plans[0].id[0] & 1) || !legal ? mine.plans[0].id[0] : std::countr_zero(legal);
    }
};

} // namespace juraic::ai
