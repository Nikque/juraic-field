#pragma once
// Building the main chain: a beam search over the pairs that are known and,
// after them, several made-up sequences of pairs ("queues"). For every first
// placement the largest chain found below it is taken in each queue and the
// queues are added up; the first placement with the largest sum is the move.
// (The shape of the search is Ama's: width 250, depth 16, six queues. The default is eight queues of a table of
// its own, mixPair below; Ama's six are beamFixedQueues 1.)
//
// Every queue is searched on its own, with its own memory, and the results are
// put together in the order of the queues, so the answer does not depend on the
// number of threads. Inside a queue the nodes of a layer are kept in one fixed
// order (value, then the order they were made in), so it does not depend on
// the compiler either. Integers only.
#include "lv38_tail.h"
#include "lv38_head.h"
#include "params.h"
#include "../core/rng.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace juraic::ai {

constexpr uint64_t QUEUE_STREAM = 0x7175657565; // random streams of the made-up pairs

// The placements of `legal` that give different fields for `pair`: a pair of one colour gives the same fields down and
// left as up and right (placements 0 to 10).
inline unsigned differentFields(unsigned legal, Pair pair) { return pair.axis == pair.child ? legal & ((1u << 11) - 1) : legal; }

// The key by which the beam search takes two fields for the same: the 13 rows, and the 14th row when a piece lies there
// (it changes the placements left on that column). A field with nothing in the 14th row has the key of its 13 rows.
inline size_t fieldKey(const GameField& g) {
    const size_t rows = KeyHash()(g.f.key());
    uint64_t top = 0;
    for (int x = 0; x < W; ++x) top |= uint64_t(g.row14[size_t(x)] & 15) << (4 * x);
    if (!top) return rows;
    return rows ^ size_t(splitmix64(top));
}

// The bases human players build on: the forms a round is started with, as they
// are named and drawn in guides for players (folds such as GTR and its
// relatives, stairs, stacked rows of three, and others), and as seen in match
// videos (two players of a master league match made the first of them at the
// bottom left in ten of ten ordinary rounds). Each is written as rows from the
// top down; a letter is a group of one colour, '.' a cell the form says nothing
// about. A form stands in a bottom corner (as written on the left, mirrored on
// the right).
inline constexpr const char* const BASE_FORMS[] = {
    "ab./aab/bb.",                 // GTR
    "..b./aaab/bbb.",              // new GTR
    "..b./.aab/abb.",              // sullen GTR
    "abb/aab/bb.",                 // a GTR with the joint in the fold's colour
    "abc./aabc/ccbb",              // LLR
    "ab../aabb/bb..",              // delta GTR
    ".bc.d./aabccd/abbcdd",        // "daa" stacking
    "..b./.aa./bbab",              // fron
    ".b../aa../babb",              // tema
    "aaaccc/bbbddd",               // rows of three, stacked
    "ab.b/aabb",                   // submarine, two columns
    "ab./aa./bbb",                 // cushion and L
    "aabb/accb",                   //
    "aabb/cccb",                   //
    "bcda./abcda/abcda/abcda",     // stairs, three and one
    ".bcda./.bcda./abcda./aabcda", // stairs, one and three
    ".bc.a./.bcda./.abcda/aabcda", // stairs, two and two
    "..c.../..b.d./aaaccd/bbbcdd", //
    "aaa.../bbda.a/bcccaa",        //
    "a...../a...ba/abbbaa",        //
};

// How many pieces of the field stand where one of the forms wants them, for the
// form and corner that fit best. A form fits as long as every piece on its cells
// agrees with it: one colour for one letter, different colours for letters that
// touch, no garbage.
// `forms`: only the first so many of the list (0 = all of them).
inline int baseCells(const BitField& f, int forms = 0) {
    int best = 0;
    int left = forms > 0 ? forms : int(sizeof(BASE_FORMS) / sizeof(*BASE_FORMS));
    for (const char* form : BASE_FORMS) {
        if (left-- <= 0) break;
        int rows = 1, width = 0;
        for (const char* c = form; *c; ++c) rows += *c == '/';
        for (const char* c = form; *c && *c != '/'; ++c) ++width;
        for (int side = 0; side < 2; ++side) {
            Cell colour[4] = {EMPTY, EMPTY, EMPTY, EMPTY};
            int n = 0;
            bool fits = true;
            const auto at = [&](int x, int row) { return form[row * (width + 1) + x]; };
            for (int row = 0; row < rows && fits; ++row)
                for (int x = 0; x < width && fits; ++x) {
                    const char letter = at(x, row);
                    if (letter == '.') continue;
                    const Cell v = f.get(side ? W - 1 - x : x, rows - 1 - row);
                    if (v == EMPTY) continue;
                    Cell& c = colour[letter - 'a'];
                    if (v == GARBAGE || (c != EMPTY && c != v)) fits = false;
                    c = v, ++n;
                }
            // letters that touch must differ
            for (int row = 0; row < rows && fits; ++row)
                for (int x = 0; x < width && fits; ++x) {
                    const char letter = at(x, row);
                    if (letter == '.') continue;
                    const Cell c = colour[letter - 'a'];
                    if (c == EMPTY) continue;
                    if (x + 1 < width && at(x + 1, row) != '.' && at(x + 1, row) != letter && colour[at(x + 1, row) - 'a'] == c) fits = false;
                    if (row + 1 < rows && at(x, row + 1) != '.' && at(x, row + 1) != letter && colour[at(x, row + 1) - 'a'] == c) fits = false;
                }
            if (fits && n > best) best = n;
        }
    }
    return best;
}

// Thousandths: the chance that a pair holds one of k colours out of `colors` (both pieces drawn alike).
constexpr int pairHolds(int k, int colors) { return k >= colors ? 1000 : 1000 - 1000 * (colors - k) * (colors - k) / (colors * colors); }

struct BuildCuts {
    lv38_tail::Shape tail;
    lv38_head::Shape head;
    int main = 0;

    int count() const { return int(tail.points > 0) + int(head.valid()); }
    int value() const { return tail.value() + (head.valid() ? lv38_tail::Shape{head.fire.points, head.fire.chains, head.fire.added}.value() : 0); }
};

// A worker reuses these arrays across consecutive field evaluations. detect()
// writes the active prefix before any trial or intermediate field is read.
struct BuildTrials {
    Trials trials;
    BitField after[W * MAX_COLORS];
    FirstWaves first;
};

// The value of a field for building (larger is better). Only the field: what
// was lost on the way to it is kept in the node. mainPoints, when given,
// receives the points of the largest chain a trial drop starts (the main chain
// as far as it can be seen on the field; with evKeyed, key pieces included).
// (The points of a trial drop and the terms for valleys and hills are put together
// as in Ama's evaluation: licenses/Ama-NOTICE.txt.)
inline int buildValue(BuildTrials& scratch, const GameField& g, const Rules& rules, const Timing& timing, const Params& p, int* mainPoints = nullptr,
                      KeyPlan* mainTrigger = nullptr, bool building = false, BuildCuts* cuts = nullptr) {
    int h[W];
    g.heights(h);
    int v = 0;
    // The chain that a few pieces would start: long, cheap to start, started high up.
    auto& [trials, after, first] = scratch;
    const Links k = g.f.links();
    const bool useKeys = p.evKeyed && p.keyRuns > 0 && !(p.keyNoneFrom > 0 && g.f.count() >= p.keyNoneFrom);
    detect(g, rules, timing, p.trialMaxAdd, trials, useKeys ? after : nullptr, false, useKeys ? &first : nullptr, &k, h); // (the frames of the chains are not used here)
    int best = 0, longest = 0;
    const bool cutWindow = (building || cuts) && p.lv38Midgame && lv38_tail::inWindow(g.f.normal().count(), p.lv38CutFrom);
    const bool tail = building && cutWindow && p.lv38Plan5 > 0 && lv38PartEnabled(p.lv38TailValue, Lv38Part::TailValue);
    const bool head = cutWindow && (p.lv38Plan12 > 0 || p.lv38HeadCut > 0);
    const bool scanTail = tail || (cuts && cutWindow && p.lv38Plan5 > 0);
    const bool readMain = mainPoints || mainTrigger || cuts || scanTail || head;
    KeyPlan main;
    int mainTrialIndex = -1;
    const auto recordMain = [&](const Trial& t, int index, const KeyPlan* keyed = nullptr) {
        if (!readMain || t.chains < rules.chainRequirement || t.points <= main.t.points) return;
        if (keyed) main = *keyed;
        else main = KeyPlan{t};
        mainTrialIndex = index;
    };
    for (int i = 0; i < trials.count; ++i) {
        const Trial& t = trials.item[i];
        recordMain(t, i);
        if (t.chains > longest) longest = t.chains;
        if (t.chains < 2) continue;
        const int value = t.chains * p.evChain - t.added * p.evKey + t.y * p.evFireHeight;
        if (value > best) best = value;
    }
    // A key piece left out is a debt: the fuller the field, the less room and time to pay it. (With
    // two or more left out a short attack is hard to answer, with one it is easier. So one at most
    // is counted; from keyNoneFrom cells on, none.)
    if (useKeys) {
        // The same chains carried on with a key piece (detect.h): a chain that one piece more
        // would make longer is worth the waves that stand, evKeyedWave for each wave the key
        // piece adds, less evKeyed for it. Tried for the longest chains first (those one wave
        // shorter after them), within keyRuns trials.
        int runs = p.keyRuns;
        for (int waves = longest; waves >= 1 && waves + 1 >= longest && runs > 0; --waves)
            for (int i = 0; i < trials.count && runs > 0; ++i) {
                if (trials.item[i].chains != waves) continue;
                KeyPlan plan, cur;
                cur.t = trials.item[i];
                addKey(g.f, h, rules, timing, cur, after[i], runs, plan, &trials, false, trials.quiet ? &first : nullptr,
                       i, p.lv38Midgame && p.lv38KeyRunsFair);
                if (!plan.key) continue;
                const Trial& t = plan.t;
                // (the main chain as the moves by kind see it: a chain cut on purpose is still the main chain)
                recordMain(t, i, &plan);
                if (t.chains < 2) continue;
                // (the waves that stand count in full, the waves the key piece adds count evKeyedWave each)
                const int value = waves * p.evChain + (t.chains - waves) * p.evKeyedWave - t.added * p.evKey + t.y * p.evFireHeight - p.evKeyed;
                if (value > best) best = value;
            }
    }
    if (mainPoints) *mainPoints = main.t.points;
    if (mainTrigger) *mainTrigger = main;
    if (cuts) *cuts = BuildCuts{{}, {}, main.t.points};
    if (scanTail && main.t.points > 0) {
        if (tail) {
            traceLv38Part(Lv38Part::TailValue);
            countComponent(CountedComponent::TailValue);
        }
        const auto side = lv38_tail::bestShape(g, trials, main, rules, timing, nullptr, p.lv38TailMin, p.lv38TailOfMain > 0);
        if (cuts) cuts->tail = side;
        if (tail) best += int(int64_t(p.evChain) * side.value() * p.lv38CutWeight / 10000);
    }
    if (head && main.key && mainTrialIndex >= 0) {
        const auto cut = lv38_head::shape(g, trials.item[mainTrialIndex], after[mainTrialIndex], main, rules, timing);
        if (cut.valid()) {
            if (cuts) cuts->head = cut;
            const int waves = cut.fire.chains;
            const Trial& t = main.t;
            const int value = waves * p.evChain + (t.chains - waves) * p.evKeyedWave - t.added * p.evKey + t.y * p.evFireHeight;
            if (building) best = std::max(best, value + int(int64_t(p.evKeyed) * (p.lv38CutWeight - 100) / 100));
        }
    }
    v += best;
    if (p.evStand) {
        // An answer at hand. The main chain may be cut by key pieces, but a short attack must be
        // answered with what stands (seen: at 56 and 62 cells a chain of thirteen cut by key pieces
        // and nothing else; two to eight garbage pieces fell and closed the field). From evStandFrom
        // cells on, a field on which no single piece fires evStandPoints or more is worth less.
        if (g.f.count() >= p.evStandFrom) {
            int stand = 0;
            for (int i = 0; i < trials.count; ++i)
                if (trials.item[i].added == 1 && trials.item[i].points > stand) stand = trials.item[i].points;
            if (stand < p.evStandPoints) v -= p.evStand;
        }
    }
    if (p.evFire) {
        // Can the main chain be fired when it is needed? The colours of which a single piece
        // starts (nearly) the largest chain: with none of them it takes two or three pieces
        // of one colour, with one a pair holds it less than every second time.
        int most = 0;
        for (int i = 0; i < trials.count; ++i)
            if (trials.item[i].chains >= 2 && trials.item[i].points > most) most = trials.item[i].points;
        unsigned fire = 0;
        for (int i = 0; i < trials.count && most > 0; ++i) {
            const Trial& t = trials.item[i];
            if (t.added != 1 || int64_t(t.points) * 100 < int64_t(most) * p.evFireKeep) continue;
            for (int c = 0; c < MAX_COLORS; ++c)
                if (rules.palette[size_t(c)] == t.color) fire |= 1u << c;
        }
        const int colors = rules.colorsInPlay();
        v += p.evFire * pairHolds(std::popcount(fire), colors) / 1000;
    }
    // Pieces of one colour next to each other.
    v += p.evLink * (BitField::cells(k.u) + BitField::cells(k.r));
    if (p.evThird && h[DEATH_X] > p.evThirdFrom) v -= p.evThird * (h[DEATH_X] - p.evThirdFrom) * (h[DEATH_X] - p.evThirdFrom);
    if (p.evBase) v += p.evBase * baseCells(g.f, p.evBaseForms);
    if (p.evCentre) {
        // From each wall towards the middle the columns should not rise.
        const int half = W / 2;
        for (int x = 1; x < half; ++x)
            if (h[x] > h[x - 1]) v -= p.evCentre * (h[x] - h[x - 1]);
        for (int x = W - 2; x >= half; --x)
            if (h[x] > h[x + 1]) v -= p.evCentre * (h[x] - h[x + 1]);
    }
    if (p.evCliff) {
        // A step of two rows is what a pair put upright fills; a higher one must be built up to.
        for (int x = 0; x + 1 < W; ++x) {
            const int step = std::abs(h[x] - h[x + 1]) - p.evCliffFrom;
            if (step > 0) v -= p.evCliff * step * step;
        }
    }
    // The shape of the top: no deep valleys, no towers.
    for (int x = 1; x + 1 < W; ++x) {
        if (h[x] < h[x - 1] && h[x] < h[x + 1]) v -= p.evValley * (std::min(h[x - 1], h[x + 1]) - h[x]);
        if (h[x] > h[x - 1] && h[x] > h[x + 1]) v -= p.evHill * (h[x] - std::max(h[x - 1], h[x + 1]));
    }
    return v;
}

inline int buildValue(const GameField& g, const Rules& rules, const Timing& timing, const Params& p, int* mainPoints = nullptr,
                      KeyPlan* mainTrigger = nullptr, bool building = false, BuildCuts* cuts = nullptr) {
    BuildTrials scratch;
    return buildValue(scratch, g, rules, timing, p, mainPoints, mainTrigger, building, cuts);
}

constexpr int HORIZONS = 24; // depths for which a search also reports what a shallower one finds
constexpr int QUEUES_TOLD = 32; // queues of which a search also reports what each of them found by itself

struct BuildAnswerWindow {
    int64_t confirm = 0;
    int64_t offsetPoints = 0, returnPoints = 0;
    int main = 0;
    std::array<int64_t, PLACEMENTS> nextSpawn{};
};

struct BuildResult {
    int best = -1;                 // the placement to make; -1 = every placement loses, or there is none
    int64_t sum[PLACEMENTS]{};     // over the queues: the largest chain found below each first placement
    // The same for a search of fewer placements: sumBy[id][n] and pointsBy[id][n] are sum[id] and
    // points[id] of the search of depth n + 1 (the layers of a search do not depend on how deep it
    // goes on; from the depth of this search on they stay as they are).
    int64_t sumBy[PLACEMENTS][HORIZONS]{};
    int pointsBy[PLACEMENTS][HORIZONS]{};
    int points[PLACEMENTS]{};      // the largest of them in any queue
    // The same queue by queue, for the first QUEUES_TOLD queues: each[q][id] is the largest chain queue q found below
    // first placement id (-1: the placement was not made or loses at once), and `told` is the number of queues that
    // are there. sum[id] is their sum and points[id] the largest of them, as long as the search has no more queues.
    int each[QUEUES_TOLD][PLACEMENTS]{};
    int told = 0;
    unsigned alive = 0;            // bit id: first placement id does not lose (in any queue)
    int64_t value[PLACEMENTS]{};   // over the queues: the value of the best field in the deepest layer that still has one below each first placement
    unsigned valued = 0;           // bit id: value[id] is there (a field was kept below it in every queue)
    int64_t nodes = 0;             // placements tried
    bool cut = false;              // the clock stopped a queue before its depth (Builder::until): what was found is that of the layers made
    int layers = 0;                // cut: the layers that every queue made in full (sumBy and pointsBy are right up to that depth)
    int depth = 0;                 // the placements the search was asked for (Params::beamDepth)
    unsigned shortOffset[PLACEMENTS]{}; // queue bits: a short full offset was found
    unsigned shortReturn[PLACEMENTS]{}; // queue bits: the offset also sends a row back
    unsigned cutQueues[PLACEMENTS][HORIZONS]{}; // queue bits for a cut that keeps the main, by depth

    // In how many of the queues told the largest chain found below first placement id is worth `goal` points or
    // more: the queues in which a chain of that size can be fired within the depth of the search. (Params::x3Enough)
    int enough(int id, int64_t goal) const {
        int n = 0;
        for (int q = 0; q < told; ++q) n += each[q][id] >= goal;
        return n;
    }
};

// The threads of one search beside the one that asked for it. A job is cut into parts (each(): the
// queues of the search, or a few dozen fields of one layer of a queue), and every thread that has
// nothing to do takes the next part of any job, the thread that gave a job among them: no thread
// waits while a part is left, whichever queue it belongs to. (Before, the threads beyond one a queue
// were dealt out to the queues: nine threads for six queues took 87 ms where six took 95, since the
// search waits for its slowest queue.) Which thread does which part does not change what is found.
class Crew {
public:
    explicit Crew(int helpers) : Crew(helpers, [](auto&& fn) { return std::thread(std::forward<decltype(fn)>(fn)); }) {}
    // start(fn) makes the thread of a helper (a parameter so that a test can make one fail). When one cannot be
    // made, the helpers already started are closed and joined before the error is passed on: a joinable
    // std::thread destroyed with the vector would end the program.
    template <class Start>
    Crew(int helpers, Start&& start) {
        try {
            threads.reserve(size_t(helpers > 0 ? helpers : 0)); // (push_back below does not reallocate)
            for (int i = 0; i < helpers; ++i) threads.push_back(start([this] { work(nullptr); }));
        } catch (...) {
            close();
            throw;
        }
    }
    ~Crew() { close(); }
    Crew(const Crew&) = delete;
    Crew& operator=(const Crew&) = delete;
    int helpers() const { return int(threads.size()); }

    // fn(k) for k = 0 .. parts - 1, each once, by this thread and the others; back when all are done.
    // quick: the parts are short, and a thread that waits for a job of its own may take them. A part
    // that is not quick (a whole queue) is begun only by a thread that has nothing of its own to wait
    // for, or by the thread that gave it: a queue must not wait for a thread gone into another one.
    template <class F>
    void each(size_t parts, F&& fn, bool quick = true) {
        if (threads.empty() || parts < 2) {
            for (size_t k = 0; k < parts; ++k) fn(k);
            return;
        }
        Job job;
        job.parts = parts, job.quick = quick;
        job.run = [&fn](size_t k) { fn(k); };
        {
            std::lock_guard<std::mutex> lock(m);
            jobs.push_back(&job);
        }
        cv.notify_all();
        work(&job);
    }

private:
    void close() {
        {
            std::lock_guard<std::mutex> lock(m);
            closing = true;
        }
        cv.notify_all();
        for (std::thread& t : threads) t.join();
    }
    struct Job {
        size_t parts = 0, next = 0; // parts in all, and the first that nobody has taken
        int active = 0;             // parts that are being worked on
        bool quick = true;
        std::function<void(size_t)> run;
    };
    // own = null: a helper, until the crew closes. Otherwise the thread that gave `own`, until all of it is done.
    void work(Job* own) {
        std::unique_lock<std::mutex> lock(m);
        for (;;) {
            Job* job = nullptr;
            if (own && own->next < own->parts) job = own;
            else
                for (Job* j : jobs)
                    if (j->next < j->parts && (j->quick || !own)) {
                        job = j;
                        break;
                    }
            if (!job) {
                if (own ? own->active == 0 : closing) break;
                cv.wait(lock);
                continue;
            }
            const size_t k = job->next++;
            ++job->active;
            lock.unlock();
            job->run(k);
            lock.lock();
            if (--job->active == 0 && job->next >= job->parts) cv.notify_all();
        }
        if (own) jobs.erase(std::find(jobs.begin(), jobs.end(), own));
    }

    std::mutex m;
    std::condition_variable cv;
    std::vector<Job*> jobs;
    std::vector<std::thread> threads;
    bool closing = false;
};

class Builder {
public:
    // The searches of this Builder stop at a time on the clock: the first layer is always made
    // (every first placement that does not lose is known), from then on a layer that is not
    // finished at that time is dropped and the answer is that of the layers before it. The answer
    // then depends on how fast the machine is. A time that is far away changes nothing.
    using Clock = std::chrono::steady_clock;
    // (until() may be called while a search of this Builder runs on another thread: it then ends at the new time.)
    void until(Clock::time_point time) { stopAt.store(time.time_since_epoch().count(), std::memory_order_relaxed), timed.store(true, std::memory_order_relaxed); }
    void untimed() { timed.store(false, std::memory_order_relaxed); }

    // known[0] is the pair to place now; after the known pairs each queue goes
    // on with its own. `seed` makes the random queues (the caller builds it from
    // what the agent may know). threads = 1: no thread is started. With more, the
    // queues and the fields of their layers are shared out among them (Crew): any
    // number of threads helps, and the answer is the same with every number.
    BuildResult search(const GameField& root, const Pair* known, int knownCount, const Rules& rules, const Timing& timing, const Params& p,
                       uint64_t seed, int threads = 1, const BuildAnswerWindow* answer = nullptr, bool cutBuild = false) {
        const int queues = p.queues();
        const int cutDepth = cutBuild && p.lv38Midgame ? std::clamp(p.lv38CutQueues, 0, HORIZONS) : 0;
        const int slots = cutDepth ? std::clamp(p.lv38CutSlots, 0, p.width()) : 0;
        if (int(work.size()) < queues) work.resize(size_t(queues));
        // The layers of the known pairs are the same in every queue (the same pairs on the same
        // fields; a queue draws its own pairs only after them): they are searched once, and each
        // queue goes on from there. The answer is that of every queue searching them itself.
        const int depth = p.depth();
        const int shared = queues > 1 && p.beamShare ? std::min(knownCount, depth) : 0;
        const std::atomic<Clock::rep>* stop = timed.load(std::memory_order_relaxed) ? &stopAt : nullptr;
        // The helpers stay from one search of this Builder to the next: in the game a choice makes two or three
        // searches, each with up to a few dozen threads, and starting and joining them every time would be work for
        // nothing. They wait without using a processor between searches, and end
        // with the Builder. A search that asks for another number of threads gets a crew of that size.
        const int helpers = threads > 1 ? threads - 1 : 0;
        if (helpers > 0 && (!crew || crew->helpers() != helpers)) crew = std::make_unique<Crew>(helpers);
        Crew* help = helpers > 0 ? crew.get() : nullptr;
        if (shared) {
            start(common, root, p, slots);
            layers(common, 0, shared, known, knownCount, rules, timing, p, seed, 0, stop, help, answer, cutDepth, slots);
        }
        const auto run = [&](size_t q) {
            Work& w = work[q];
            if (shared) resume(w, common, p, slots);
            else start(w, root, p, slots);
            layers(w, shared, depth, known, knownCount, rules, timing, p, seed, int(q), stop, help, answer, cutDepth, slots);
        };
        // (a queue is a long part: begun by a thread with nothing else to do, see Crew::each)
        if (help && queues > 1) help->each(size_t(queues), run, false);
        else
            for (int q = 0; q < queues; ++q) run(size_t(q));
        BuildResult r;
        r.alive = ~0u;
        r.depth = depth;
        r.told = queues < QUEUES_TOLD ? queues : QUEUES_TOLD;
        for (int q = 0; q < queues; ++q) {
            const Work& w = work[size_t(q)];
            r.nodes += w.nodes;
            if (w.cut && (!r.cut || w.layers < r.layers)) r.layers = w.layers;
            r.cut = r.cut || w.cut;
            for (int id = 0; id < PLACEMENTS; ++id) {
                if (q < QUEUES_TOLD) r.each[q][id] = w.best[id];
                if (answer && q < QUEUES_TOLD) {
                    if (w.shortOffset >> id & 1u) r.shortOffset[id] |= 1u << q;
                    if (w.shortReturn >> id & 1u) r.shortReturn[id] |= 1u << q;
                }
                if (cutDepth && q < QUEUES_TOLD)
                    for (int n = 0; n < cutDepth; ++n)
                        if (w.cutPlacements[n] >> id & 1u) r.cutQueues[id][n] |= 1u << q;
                if (w.best[id] < 0) {
                    r.alive &= ~(1u << id);
                    continue;
                }
                r.sum[id] += w.best[id];
                if (w.best[id] > r.points[id]) r.points[id] = w.best[id];
                int by = 0;
                for (int n = 0; n < HORIZONS; ++n) {
                    if (w.at[id][n] > by) by = w.at[id][n];
                    r.sumBy[id][n] += by;
                    if (by > r.pointsBy[id][n]) r.pointsBy[id][n] = by;
                }
            }
        }
        r.alive &= (1u << PLACEMENTS) - 1;
        r.valued = r.alive;
        for (int q = 0; q < queues; ++q)
            for (int id = 0; id < PLACEMENTS; ++id) {
                if (work[size_t(q)].deepest[id] == INT32_MIN) r.valued &= ~(1u << id);
                else r.value[id] += work[size_t(q)].deepest[id];
            }
        for (int id = 0; id < PLACEMENTS; ++id) // equal sums: the smaller number
            if ((r.alive >> id & 1) && (r.best < 0 || r.sum[id] > r.sum[r.best])) r.best = id;
        return r;
    }

    // Made-up pairs as the game deals them, without the luck: a bag holds every pair of two
    // colours once (the first and the second piece told apart, so one pair in `colors` is of one
    // colour and each pair of two different colours is there twice), and a queue draws the bag
    // empty in an order of its own, then a new bag in another order. The pair number n of queue q;
    // the same whatever the seed of the search.
    static Pair bagPair(const Rules& rules, int colors, int q, int n) {
        const int size = colors * colors;
        int8_t order[MAX_COLORS * MAX_COLORS];
        for (int i = 0; i < size; ++i) order[i] = int8_t(i);
        Rng rng(uint64_t(q) * 1000 + uint64_t(n / size), QUEUE_STREAM);
        for (int i = size - 1; i > 0; --i) std::swap(order[i], order[rng.below(uint64_t(i) + 1)]);
        const int pair = order[n % size];
        return {rules.palette[size_t(pair / colors)], rules.palette[size_t(pair % colors)]};
    }

    // The six pairs of two different colours of four, ordered so that two pairs in a row share no colour where that can
    // be (the three ways to split four colours in two and two, one after the other): the round of turnPair and mixPair.
    static constexpr int8_t TWO_COLORS[6][2] = {{0, 1}, {2, 3}, {0, 2}, {1, 3}, {0, 3}, {1, 2}};

    // Made-up pairs of four colours that keep every colour coming: the six pairs of TWO_COLORS in turn. Queue q starts
    // at pair q of the round, so the six queues begin with the six pairs. The pair number n of queue q.
    static Pair turnPair(const Rules& rules, int q, int n) {
        const int8_t* c = TWO_COLORS[(q + n) % 6];
        return {rules.palette[size_t(c[0])], rules.palette[size_t(c[1])]};
    }

    // Made-up pairs of four colours with pairs of one colour among them, as often as the tsumo holds them
    // (one pair in four): a round of eight pairs - three of two different
    // colours, one of one colour, three of two colours, one of one colour - in which the six pairs of two
    // colours come in the order of turnPair, and the pairs of one colour go through the four colours. Queue q
    // starts at place q of the round, so eight queues begin with the six pairs of two colours and two pairs
    // of one colour; a queue has every colour eight times in sixteen pairs. The pair number n of queue q
    // (for any number of queues: queue q + 8 starts where queue q does, with other pairs of one colour).
    static Pair mixPair(const Rules& rules, int q, int n) {
        const int at = q + n; // the place counted from the start of queue 0; before it stand at / 4 pairs of one colour
        if (at % 4 == 3) {
            const Cell one = rules.palette[size_t((at / 4 + q) % 4)];
            return {one, one};
        }
        const int8_t* c = TWO_COLORS[(at - at / 4) % 6];
        return {rules.palette[size_t(c[0])], rules.palette[size_t(c[1])]};
    }

    // Do the made-up pairs of a search depend on its seed? (Not with the fixed sequences of four colours.)
    static bool seeded(const Rules& rules, const Params& p) {
        const int colors = rules.colorsInPlay();
        const int queues = p.queues();
        if (p.beamFixedQueues == 4 && colors == 4) return false;
        return p.beamFixedQueues != 2 && !(p.beamFixedQueues && colors == 4 && queues <= 6);
    }

    // The pair number `index` (0 = the pair to place now) of queue q.
    // `rng` is the queue's own (used only by the random queues, once per pair in order).
    static Pair queuePair(const Pair* known, int knownCount, const Rules& rules, const Params& p, int q, int index, Rng& rng) {
        if (index < knownCount) return known[index];
        const int colors = rules.colorsInPlay();
        if (p.beamFixedQueues == 2) return bagPair(rules, colors, q, index - knownCount);
        if (p.beamFixedQueues == 3 && colors == 4 && q < 6) return turnPair(rules, q, index - knownCount);
        if (p.beamFixedQueues == 4 && colors == 4) return mixPair(rules, q, index - knownCount);
        if (p.beamFixedQueues && colors == 4 && q < 6) {
            // Two of the four colours as one pair, the other two as the next, in turn. (The table is Ama's: licenses/Ama-NOTICE.txt.)
            static constexpr int8_t CHOICE[6][4] = {{0, 1, 2, 3}, {0, 2, 1, 3}, {0, 3, 1, 2}, {1, 2, 0, 3}, {1, 3, 0, 2}, {2, 3, 0, 1}};
            const int8_t* c = CHOICE[q];
            const int turn = (index - knownCount) & 1;
            return {rules.palette[size_t(c[2 * turn])], rules.palette[size_t(c[2 * turn + 1])]};
        }
        const Cell a = rules.palette[rng.below(uint64_t(colors))];
        return {a, rules.palette[rng.below(uint64_t(colors))]};
    }

private:
    struct Node {
        GameField g;
        int value = 0;  // buildValue of the field - lost
        int lost = 0;   // what the way here has cost
        int order = 0;  // the order the nodes of a layer were made in
        int8_t first = -1;
        int64_t answerSpawn = 0;
        int cutMain = 0, fullValue = 0;
        bool cutShape = false, extra = false;
    };
    // A field made by a part of a layer, with its key, and what that part found on the way.
    struct Made {
        Node node;
        uint64_t key = 0;
    };
    struct Tally {
        int best[PLACEMENTS]; // as Work::best, of the fields of this part alone
        int at[PLACEMENTS];   // as Work::at at the depth of the layer
        int64_t nodes = 0;
        unsigned shortOffset = 0, shortReturn = 0;
        bool late = false;    // the clock was up before the part began: nothing made
        Tally() {
            for (int id = 0; id < PLACEMENTS; ++id) best[id] = -1, at[id] = 0;
        }
    };
    struct Work {
        unsigned digRoots = 0; // First placements which open the field with a small clear.
        unsigned shortOffset = 0, shortReturn = 0;
        std::array<unsigned, HORIZONS> cutPlacements{};
        std::vector<Node> layer, next;
        std::vector<std::vector<Made>> made; // the fields of a layer by the part that made them (with a Crew)
        std::vector<Tally> tally;
        std::vector<uint64_t> seen;
        int best[PLACEMENTS]{}; // -1 = this first placement was not made or loses at once
        int at[PLACEMENTS][HORIZONS]{}; // the largest chain fired with the placement of that number below it
        int deepest[PLACEMENTS]{}; // the best value of a field below it in the deepest layer that has one; INT32_MIN = none
        int64_t nodes = 0;
        bool cut = false; // the clock stopped the search: no further layer
        int layers = 0;   // layers made in full
    };
    std::vector<Work> work;
    Work common; // the layers of the known pairs, when the queues share them
    std::atomic<Clock::rep> stopAt{0}; // (the time on the clock, as the count of its time_since_epoch)
    std::atomic<bool> timed{false};
    std::unique_ptr<Crew> crew; // the threads beside the one that searches, kept between searches (null: none were asked for yet)
    static bool expired(const std::atomic<Clock::rep>* stop) {
        return stop && Clock::now().time_since_epoch().count() >= stop->load(std::memory_order_relaxed);
    }

    // A table of the fields of one layer: true when the key is new.
    static bool insert(std::vector<uint64_t>& seen, uint64_t key) {
        key |= 1;
        const size_t mask = seen.size() - 1;
        for (size_t i = size_t(key >> 20) & mask;; i = (i + 1) & mask) {
            if (seen[i] == key) return false;
            if (!seen[i]) return seen[i] = key, true;
        }
    }

    static size_t slotsFor(const Params& p, int cutSlots) {
        const int width = p.width() + cutSlots;
        size_t slots = 1024;
        while (slots < size_t(width) * PLACEMENTS * 2) slots *= 2;
        return slots;
    }
    // A search that starts at the root.
    static void start(Work& w, const GameField& root, const Params& p, int cutSlots) {
        w.digRoots = 0;
        w.shortOffset = w.shortReturn = 0;
        w.cutPlacements.fill(0);
        w.seen.assign(slotsFor(p, cutSlots), 0);
        w.nodes = 0, w.cut = false, w.layers = 0;
        for (int id = 0; id < PLACEMENTS; ++id) {
            w.best[id] = -1, w.deepest[id] = INT32_MIN;
            for (int& points : w.at[id]) points = 0;
        }
        w.layer.clear();
        w.layer.push_back(Node{root});
    }
    // A search that goes on from the layers another has made. (The placements tried there count
    // for every queue, as when each made them itself.)
    static void resume(Work& w, const Work& from, const Params& p, int cutSlots) {
        w.digRoots = from.digRoots;
        w.shortOffset = from.shortOffset;
        w.shortReturn = from.shortReturn;
        w.cutPlacements = from.cutPlacements;
        w.seen.assign(slotsFor(p, cutSlots), 0);
        w.nodes = from.nodes, w.cut = from.cut, w.layers = from.layers;
        for (int id = 0; id < PLACEMENTS; ++id) {
            w.best[id] = from.best[id], w.deepest[id] = from.deepest[id];
            for (int n = 0; n < HORIZONS; ++n) w.at[id][n] = from.at[id][n];
        }
        w.layer = from.layer;
    }

    static bool betterNode(const Node& a, const Node& b) {
        return a.value != b.value ? a.value > b.value : a.order < b.order;
    }

    static void keepCutSlots(std::vector<Node>& base, std::vector<Node>& marked, int slots) {
        std::sort(marked.begin(), marked.end(), betterNode);
        const size_t primary = base.size();
        int kept = 0;
        for (auto& node : marked) {
            if (kept == slots) break;
            const auto keptBefore = base.begin() + primary;
            const auto same = [&](const Node& ordinary) { return ordinary.order == node.order; };
            if (std::any_of(base.begin(), keptBefore, same)) continue;
            node.extra = true;
            base.push_back(node);
            ++kept;
        }
    }

    // Keep one continuation of a small digging move alive. The chain found
    // underneath still has to compete with those under the ordinary moves.
    static void trimLayer(std::vector<Node>& nodes, size_t keep, unsigned digRoots) {
        if (nodes.size() <= keep) return;
        const auto middle = nodes.begin() + std::ptrdiff_t(keep);
        if (!digRoots) {
            std::nth_element(nodes.begin(), middle, nodes.end(), betterNode);
        } else {
            const Node* best = nullptr;
            for (const Node& node : nodes) {
                if ((digRoots >> node.first & 1) && (!best || betterNode(node, *best))) best = &node;
            }
            const int order = best ? best->order : -1;
            std::nth_element(nodes.begin(), middle, nodes.end(), [&](const Node& a, const Node& b) {
                const bool keepA = a.order == order, keepB = b.order == order;
                return keepA != keepB ? keepA : betterNode(a, b);
            });
        }
        nodes.resize(keep);
    }

    // With a Crew: the fields of a layer are made in parts of MAKE_GRAIN fields of the layer before
    // (from MAKE_FROM of them on) and valued in parts of VALUE_GRAIN (from VALUE_FROM on). A part
    // takes some dozens of microseconds; handing it over costs far less.
    static constexpr size_t MAKE_FROM = 64, MAKE_GRAIN = 16, VALUE_FROM = 128, VALUE_GRAIN = 64;

    // The layers `from` to `to` - 1 of queue q (those of the known pairs are the same in every queue).
    // stop: the time at which the search ends (null = it runs to its depth).
    // crew: the threads that work on the parts of a layer together with this one (null = none). The
    // fields of a layer are made first, each new one once and in the order of the layer before, and
    // valued afterwards, every field by itself: what is found does not depend on who does which part.
    static void layers(Work& w, int from, int to, const Pair* known, int knownCount, const Rules& rules, const Timing& timing, const Params& p,
                       uint64_t seed, int q, const std::atomic<Clock::rep>* stop, Crew* crew,
                       const BuildAnswerWindow* answer, int cutDepth, int slots) {
        const int width = p.width();
        // beamSift: the key piece part is most of the time a field takes (about 2.5 of 4.1 microseconds).
        // A layer is valued without it first, and in full only for the best beamSift x width fields.
        Params light = p;
        light.evKeyed = 0;
        const bool sift = p.beamSift > 0 && p.evKeyed > 0;
        Rng rng(seed, QUEUE_STREAM + uint64_t(q));
        for (int depth = from; depth < to && !w.cut && !w.layer.empty(); ++depth) {
            const Pair pair = queuePair(known, knownCount, rules, p, q, depth, rng);
            w.next.clear();
            if (depth) std::fill(w.seen.begin(), w.seen.end(), 0);
            // Params::beamLastValues = 0: the fields of the last layer are made for the chains their
            // placements fire, and for nothing else: no field comes after them, so they need not be
            // told apart, valued or put in order (a sixteenth of the values of a search 16 deep).
            const bool tellCuts = depth < cutDepth;
            const bool readCuts = tellCuts || slots;
            const bool blind = !tellCuts && !p.beamLastValues && depth + 1 == p.depth();
            // The field n of the layer before with the pair on placement `id`, into c. False: no field
            // of the next layer (it loses, its third column is too high, or its chain ends the line).
            // What the chain is worth goes to t whenever the field does not lose.
            const auto child = [&](const Node& n, int id, Node& c, Tally& t) {
                c = n;
                const PlaceResult placed = place(c.g, placement(id), pair);
                Trial chain;
                if (rules.clearCount != 4 || c.g.f.hasClear()) runTrialChain(c.g.f, rules, timing, chain, false);
                if (c.g.dead()) return false;
                if (p.beamThirdMax < 12 && detail::countBits(c.g.f.occupied().column(DEATH_X)) > p.beamThirdMax) return false;
                if (depth == 0) c.first = int8_t(id);
                const int worth = chain.chains >= rules.chainRequirement ? chain.points : 0; // (a chain of fewer waves than Rules::chainRequirement makes no garbage)
                if (worth > t.best[c.first]) t.best[c.first] = worth;
                if (worth > t.at[c.first]) t.at[c.first] = worth;
                if (chain.points >= p.beamEndPoints) return false; // a big chain ends the line
                if (answer) {
                    if (!depth) {
                        c.answerSpawn = chain.chains ? 0 : answer->nextSpawn[id];
                        if (c.answerSpawn - timing.nextFrames >= answer->confirm) c.answerSpawn = 0;
                    } else if (n.answerSpawn > 0) {
                        const auto x = placement(id).x;
                        const int move = timing.placeFrames(x, placed.lockY);
                        c.answerSpawn = n.answerSpawn + move + timing.splitFrames(placed.split, x, placed.lockY);
                        if (chain.chains >= rules.chainRequirement && chain.chains <= 4 && c.g.f.count() &&
                                   chain.points >= answer->offsetPoints && !(t.shortReturn >> c.first & 1u)) {
                            int reserve = 0;
                            buildValue(c.g, rules, timing, p, &reserve);
                            if (int64_t(reserve) * 2 >= answer->main) {
                                t.shortOffset |= 1u << c.first;
                                if (chain.points >= answer->returnPoints) t.shortReturn |= 1u << c.first;
                            }
                        }
                        // Garbage falls at the end of a quiet placement. If it has
                        // already settled, a pair can answer even when its spawn
                        // or lock comes after confirmation. A line that fires
                        // cannot use the same forecast for another reply.
                        if (chain.chains || c.answerSpawn - timing.nextFrames >= answer->confirm)
                            c.answerSpawn = 0;
                    }
                }
                c.lost = n.lost + p.evWaste * chain.chains;
                // A torn pair takes longer (the table of game/timing.h): in that time the other side places more.
                if (placed.split) c.lost += p.evTearFrame * timing.splitFrames(placed.split, placement(id).x, placed.lockY);
                return true;
            };
            // Every placement of the pair on field n, into c: made(c) for each field of the next layer (none when the
            // layer is blind).
            const auto children = [&](const Node& n, Node& c, Tally& t, const auto& made) {
                const unsigned legal = differentFields(legalPlacements(n.g), pair);
                for (int id = 0; id < PLACEMENTS; ++id) {
                    if (!(legal >> id & 1)) continue;
                    ++t.nodes;
                    if (child(n, id, c, t) && !blind) made(c);
                }
            };
            const auto take = [&](const Tally& t) {
                w.nodes += t.nodes;
                w.shortOffset |= t.shortOffset;
                w.shortReturn |= t.shortReturn;
                for (int id = 0; id < PLACEMENTS; ++id) {
                    if (t.best[id] > w.best[id]) w.best[id] = t.best[id];
                    if (depth < HORIZONS && t.at[id] > w.at[id][depth]) w.at[id][depth] = t.at[id];
                }
            };
            const size_t parents = w.layer.size();
            if (!crew || parents < MAKE_FROM) {
                Tally t;
                Node c;
                for (const Node& n : w.layer) {
                    if (depth && expired(stop)) {
                        // Out of time. The chains found so far stand (they are real ones); the fields of
                        // this layer are not all there, so none of them is used.
                        w.cut = true;
                        break;
                    }
                    children(n, c, t, [&](Node& made) {
                        if (!insert(w.seen, fieldKey(made.g))) return;
                        made.order = int(w.next.size());
                        w.next.push_back(made);
                    });
                }
                take(t);
            } else {
                // The same in parts: each makes the fields of its stretch of the layer before, in their
                // order, and the parts are put together in theirs, so every new field comes once and
                // where it came above.
                const size_t parts = (parents + MAKE_GRAIN - 1) / MAKE_GRAIN;
                if (w.made.size() < parts) w.made.resize(parts);
                w.tally.assign(parts, Tally());
                crew->each(parts, [&](size_t k) {
                    std::vector<Made>& out = w.made[k];
                    out.clear();
                    Tally t;
                    if (depth && expired(stop)) {
                        t.late = true;
                    } else {
                        Made c;
                        const size_t end = (k + 1) * MAKE_GRAIN < parents ? (k + 1) * MAKE_GRAIN : parents;
                        for (size_t i = k * MAKE_GRAIN; i < end; ++i)
                            children(w.layer[i], c.node, t, [&](const Node& made) {
                                c.key = fieldKey(made.g);
                                out.push_back(c);
                            });
                    }
                    w.tally[k] = t;
                });
                for (size_t k = 0; k < parts; ++k) {
                    if (w.tally[k].late) w.cut = true; // (out of time, as above)
                    else take(w.tally[k]);
                }
                if (!w.cut)
                    for (size_t k = 0; k < parts; ++k)
                        for (Made& c : w.made[k]) {
                            if (!insert(w.seen, c.key)) continue;
                            c.node.order = int(w.next.size());
                            w.next.push_back(c.node);
                        }
            }
            if (w.cut) break;
            if (blind) {
                w.layers = depth + 1;
                break;
            }
            {
                // The values of the fields (nearly all of the time of a search). False: out of time.
                const Params& how = sift ? light : p;
                const auto value = [&](size_t begin, size_t end) {
                    BuildTrials scratch;
                    for (size_t i = begin; i < end; ++i) {
                        if (depth && i % PLACEMENTS == 0 && expired(stop)) return false;
                        Node& c = w.next[i];
                        if (readCuts) {
                            BuildCuts cuts;
                            c.fullValue = buildValue(scratch, c.g, rules, timing, p, nullptr, nullptr, true, &cuts) - c.lost;
                            c.cutMain = cuts.main;
                            c.cutShape = cuts.count() > 0;
                            c.value = sift ? buildValue(scratch, c.g, rules, timing, how) - c.lost : c.fullValue;
                        } else {
                            c.value = buildValue(scratch, c.g, rules, timing, how, nullptr, nullptr, !sift) - c.lost;
                        }
                    }
                    return true;
                };
                const size_t count = w.next.size();
                bool valued = true;
                if (!crew || count < VALUE_FROM) {
                    valued = value(0, count);
                } else {
                    std::atomic<bool> all{true};
                    crew->each((count + VALUE_GRAIN - 1) / VALUE_GRAIN, [&](size_t k) {
                        if (!all.load(std::memory_order_relaxed)) return;
                        const size_t begin = k * VALUE_GRAIN, end = begin + VALUE_GRAIN < count ? begin + VALUE_GRAIN : count;
                        if (!value(begin, end)) all = false;
                    });
                    valued = all;
                }
                if (!valued) {
                    w.cut = true; // (as above: the layer is not used)
                    break;
                }
            }
            // The best `width`, in one fixed order whatever the library does with equal values.
            if (p.readMidgame && depth == 0 && knownCount > 0 && width >= 16) {
                const auto& root = w.layer.front().g.f;
                const int garbage = root.garbage().count();
                if (garbage) {
                    const int colors = root.normal().count();
                    for (const Node& node : w.next) {
                        if (node.g.f.garbage().count() < garbage && colors + 2 - node.g.f.normal().count() <= 8)
                            w.digRoots |= 1u << node.first;
                    }
                }
            }
            if (tellCuts) {
                int bestMain = 0;
                for (const Node& n : w.next) bestMain = std::max(bestMain, n.cutMain);
                for (const Node& n : w.next)
                    if (n.cutShape && int64_t(n.cutMain) * 1000 >= int64_t(bestMain) * p.lv38CutKeep)
                        w.cutPlacements[depth] |= 1u << n.first;
            }
            std::vector<Node> marked;
            if (slots) {
                for (const Node& n : w.next)
                    if (n.cutShape) {
                        marked.push_back(n);
                        marked.back().value = n.fullValue;
                    }
                // The ordinary beam retains its full width and its own pruning order.
                // Extra parents are appended behind it, so they cannot replace an ordinary node during deduplication.
                const auto extra = std::find_if(w.next.begin(), w.next.end(), [](const Node& n) { return n.extra; });
                w.next.erase(extra, w.next.end());
            }
            if (sift) {
                const size_t keep = size_t(width) * size_t(p.beamSift);
                trimLayer(w.next, keep, w.digRoots);
                BuildTrials scratch;
                for (Node& n : w.next)
                    n.value = readCuts ? n.fullValue : buildValue(scratch, n.g, rules, timing, p, nullptr, nullptr, true) - n.lost;
            }
            trimLayer(w.next, size_t(width), w.digRoots);
            std::sort(w.next.begin(), w.next.end(), betterNode);
            if (slots) keepCutSlots(w.next, marked, slots);
            w.layer.swap(w.next);
            w.layers = depth + 1;
            {
                unsigned here = 0;
                for (const Node& n : w.layer) {
                    if (here >> n.first & 1) {
                        if (n.extra && n.value > w.deepest[n.first]) w.deepest[n.first] = n.value;
                        continue;
                    }
                    here |= 1u << n.first;
                    w.deepest[n.first] = n.value;
                }
            }
        }
    }
};

} // namespace juraic::ai
