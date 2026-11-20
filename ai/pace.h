#pragma once
// X3Pace: the part of X3 that is about time, not about which move is good. The tactics (ai/tactics.h, X3Agent) say
// where a pair goes; this class says when the choice is made and how much search fits into it:
//
//   - the clock of a choice (Params::thinkBudget): how fast this machine searches is learned from the searches it
//     has made, and a search that would not end in its time is made narrower (search(), learn(), chooseAhead());
//   - the second choice of a pair, made when the third pair has come into the window, or, with
//     PlacementAgent::againFresh, once more as the pair appears (again());
//   - the search of the next choice begun while the pair of this one falls (airborne(), Params::thinkPonder);
//   - the late view of the opponent and the slow hands of the levels below the strongest (Params::seeLate,
//     handWait: remember(), sight());
//   - the record of a choice for the game's replays (Note), and the counters by which the game says that the AI
//     was short of time on this machine.
//
// The measuring tools run without a clock (thinkBudget 0): there a search runs to its depth, and what this class
// does is to call the tactics in the frame the choice is due.
#include "agent.h"
#include "build.h"
#include <array>
#include <atomic>
#include <future>
#include <mutex>

namespace juraic::ai {

class X3Pace : public PlacementAgent {
public:
    explicit X3Pace(const Params& params) : p(params) {
        choicePairLimit = std::clamp(params.knownPairs, 3, MAX_KNOWN);
        ahead = params.thinkAhead, pathCheck = params.x3PathCheck != 0, againLate = params.thinkLate, holdSecond = params.thinkHold != 0;
        equivalentSetupRoute = params.equivalentSetupRoute;
        handWait = params.handWait > 0 ? params.handWait : 0;
        autoAhead = params.thinkAhead <= 0 && params.thinkBudget > 0 && params.thinkFit >= 1;
        budgetSet = params.thinkBudget;
        if (autoAhead) ahead = AHEAD_MOST;
    }
    ~X3Pace() override {
        finish();
        dropEarly();
        dropRival();
    }

    void startRound(int seat, uint64_t seed) override {
        if (p.x3Salt) seed += uint64_t(p.x3Salt) * 0x9E3779B97F4A7C15ull; // (a twin: Params::x3Salt)
        PlacementAgent::startRound(seat, seed);
        dropEarly();
        dropRival();
        rivalStamp = -1;
        earlySeed = 0, earlyPairs = 0;
        roundSeed = seed;
        for (Seen& s : past) s.frame = -1;
        for (int i = 0; i < 2; ++i) sentSoFar[i] = 0, waveMoves[i] = -1, waveCount[i] = 0;
        firstSeen = viewFrame = -1;
        if (p.thinkBudget > 0 && p.thinkFit >= 1 && unitNs == 0) learnSpeed();
        if (autoAhead) chooseAhead();
    }

    uint8_t frame(const Observation& o) override {
        if (p.seeLate > 0) remember(o);
        return PlacementAgent::frame(o);
    }

    // What the last choice was (for tests and tools).
    // (Whoever prints a kind keeps a name for each: tools/ai_match.cpp, play/realgame_drive.h.)
    enum Kind { None, Win, Answer, AnswerBuild, Poke, Build, Fire, Clear, Crush, CrushBuild, KINDS };
    Kind kind = None;
    bool guardedNow = false; // the last choice: x3Guard narrowed the placements the beam search chose among
    int lastChoice = -1;
    int64_t counts[KINDS]{};
    int64_t cutSearches = 0; // searches that the clock stopped (Params::thinkBudget), and "wins" not taken for that reason
    int64_t narrowSearches = 0, shortPlans = 0; // searches made narrower to end in time, choices whose plans were made with two pairs
    int64_t thinkCount = 0; // choices made
    int64_t rivalSearches = 0, rivalReused = 0; // searches of the opponent's field made, and choices that took the one of an earlier choice
    int64_t earlyTaken = 0; // searches begun while the pair before fell (Params::thinkPonder) that the next choice took
    // Second choices (Params::thinkAgain): those whose searches got their full width, and those the
    // clock made narrower or stopped (the first choice then stands).
    int64_t againMade = 0, againPoor = 0;
    int64_t againNarrow = 0, againCut = 0; // ... and their searches among narrowSearches and cutSearches
    // again(), with the clock: how often it was asked; and not begun because the screen showed no pair the first choice
    // did not know, because the speed of the machine is not known yet, because its plans and its search would not be
    // done in time - and for those the milliseconds they were reckoned to need and the milliseconds there were, summed.
    int64_t againAsked = 0, againNothingNew = 0, againNoSpeed = 0, againNoTime = 0, againNeedMs = 0, againHaveMs = 0;
    int64_t rivalMissed = 0; // thinkRivalFull: choices whose search of the opponent's field was not done in their time
    int widthOfFirst() const { return firstWidth; } // the width of the search of the own field of the last first choice
    // What the clock decided in a choice (Params::thinkBudget): with these and the seed of the round
    // the choice can be made again by a tool that has no clock, unless the clock stopped a search
    // (cut > 0: where each queue stood then is not kept).
    struct Note {
        int move = 0;            // the moves the player had made
        int pairs = 0;           // the pairs the plans were made with
        int id = -1;             // the placement chosen
        int cut = 0;             // searches that the clock stopped
        std::vector<int> widths{}; // the width of every search that the clock may make wider or narrower, in their order
    };
    bool noting = false;                      // the game: the choices of the round are written to `notes`
    std::vector<Note> notes;                  // (a thread writes them: read through takeNotes)
    const std::vector<Note>* script = nullptr; // a tool, thinkBudget 0: the notes of the round that is played again
    std::vector<Note> takeNotes() {
        finish();
        std::vector<Note> taken;
        taken.swap(notes);
        return taken;
    }
    int budgetMs() const { return p.thinkBudget; }

protected:
    Params p;
    Builder builder;
    uint64_t roundSeed = 0;
    // Params::thinkBudget, the time a choice may take. A machine too slow for the searches of a
    // choice gets narrower ones of the same depth, not wide ones that the clock stops after a few
    // layers (measured: stopped at a fifth of the time they need, X3 won 67 of 400 rounds against
    // itself). How fast the machine is, is learned from the searches it has made.
    Builder::Clock::time_point began; // when the choice that is being made was begun
    int64_t unitNs = 0;               // nanoseconds a search takes for one node of width, one layer and one queue (0 = not known yet)
    int64_t slowNs = 0;               // ... the same, following a slower search at once and a faster one only little by little (chooseAhead)
    int64_t plansNs = 0;              // nanoseconds the plans of both players take with three pairs on the screen (0 = not known yet)
    const Note* scripted = nullptr;   // script: the note of the choice that is being made (null = it has none)
    size_t widthAt = 0;               // ... and how many of its widths were used
    int budgetNow = 0;                // the milliseconds of the choice that is being made (thinkBudget, or less for a second choice)
    // The second choice of a pair (Params::thinkAgain, PlacementAgent::again).
    int firstPairs = 0;               // the pairs of the own tsumo the first choice knew
    int againBudget = 0;              // the milliseconds the second choice has
    int64_t againPlansNs = 0;         // nanoseconds the plans of a second choice take (0 = not known yet)
    bool poor = false;                // the choice that is being made: the clock stopped a search of it
    int ownWidth = 0;                 // ... the width of its last search of the own field (0 = it has made none)
    int firstWidth = 0;               // ... and that of the first choice of the pair
    Rng drawn{0};                     // the agent's random numbers as they stood before the first choice of the pair

    // Params::seeLate: the opponent is seen late. Every frame of the round is kept as long as it may be asked for.
    struct Seen {
        int64_t frame = -1;
        Player player[2];
        int64_t sent[2]{}; // the garbage the waves of each player had sent since the round began
    };
    std::vector<Seen> past;       // a ring: frame f lies in past[f % past.size()]
    int64_t sentSoFar[2]{};       // ... counted up to the frame shown last
    int waveMoves[2] = {-1, -1};  // the wave of each player counted last: his moves then,
    int waveCount[2] = {0, 0};    // and the number of the wave in its chain
    int64_t firstSeen = -1;       // the first frame of the round that was shown
    int64_t viewFrame = -1;       // the frame the opponent is seen at now (it never goes back)
    Observation lateView;         // what sight() returns

    const Seen* seenAt(int64_t frame) const {
        if (past.empty() || frame < 0) return nullptr;
        const Seen& s = past[size_t(frame % int64_t(past.size()))];
        return s.frame == frame ? &s : nullptr;
    }
    // That side as a frame shows it: a chain is running, or about to - the pair that fires it has locked.
    // (Rules that clear another number than four: only a chain that runs.)
    static bool firing(const Player& q, const Rules& rules) {
        if (q.phase == Phase::Wave || (q.phase == Phase::Land && q.chain.chains > 0)) return true;
        return (q.phase == Phase::Split || q.phase == Phase::Land) && rules.clearCount == 4 && q.field.f.hasClear();
    }
    // Before every frame of the round: keeps what the screen shows, and moves the frame the opponent is seen at.
    void remember(const Observation& o) {
        if (o.players != 2) return;
        if (past.empty()) past.resize(size_t(p.seeLate) + 2);
        // (the garbage a wave sends is on the screen as long as the wave runs: counted once for every wave)
        for (int i = 0; i < 2; ++i) {
            const Player& q = o.player[i];
            if (q.phase != Phase::Wave || (q.moves == waveMoves[i] && q.chain.chains == waveCount[i])) continue;
            sentSoFar[i] += q.wave.sent, waveMoves[i] = q.moves, waveCount[i] = q.chain.chains;
        }
        Seen& s = past[size_t(o.frame % int64_t(past.size()))];
        s.frame = o.frame, s.player[0] = o.player[0], s.player[1] = o.player[1], s.sent[0] = sentSoFar[0], s.sent[1] = sentSoFar[1];
        if (firstSeen < 0) firstSeen = o.frame;
        // seeLate frames back, not before the round was first shown and never back from where it was; and the
        // present while that view shows a chain of his (noticed: its time is then counted as the screen shows it).
        int64_t at = o.frame - p.seeLate;
        if (at < firstSeen) at = firstSeen;
        if (at < viewFrame) at = viewFrame;
        const Seen* then = seenAt(at);
        if (!then || firing(then->player[1 - o.me], o.rules)) at = o.frame;
        viewFrame = at;
    }
    // The screen with the opponent's side as it was in viewFrame. What waits above the two fields is made to
    // fit that view: above the own field, without what his waves have sent since (in that view he has not
    // fired; nothing of it is still waiting for a chain to end, as the view never shows a chain of his); above
    // his, what waited then and what the own waves have sent since, confirmed unless the own chain still runs.
    const Observation& sight(const Observation& o) override {
        if (p.seeLate <= 0 || o.players != 2 || viewFrame < 0 || viewFrame >= o.frame) return o;
        const Seen* then = seenAt(viewFrame);
        const Seen* now = seenAt(o.frame);
        if (!then || !now) return o; // (a frame that was not shown: the present)
        const int me = o.me, him = 1 - me;
        lateView = o;
        Player& his = lateView.player[him];
        his = then->player[him];
        Incoming& above = lateView.player[me].incoming;
        const int64_t left = int64_t(above.total()) - (now->sent[him] - then->sent[him]);
        above.pending = 0, above.confirmed = int(left > 0 ? left : 0);
        const int own = int(now->sent[me] - then->sent[me]);
        if (o.self().phase == Phase::Wave) {
            his.incoming.pending += own;
        } else {
            his.incoming.confirm();
            his.incoming.confirmed += own;
        }
        return lateView;
    }

    bool again(const Observation& o) override {
        if (!p.thinkAgain) return false;
        const Player& self = o.self();
        if (p.thinkBudget > 0) ++againAsked;
        if (pairsShown(o) <= firstPairs && !againAsIs) return ++againNothingNew, false; // (nothing the first choice did not know)
        if (p.thinkBudget <= 0) {
            // A tool: always. A round of the game played again: when the game did (two notes for the move).
            if (!script) return true;
            int count = 0;
            for (const Note& n : *script) count += n.move == int(self.moves);
            return count >= 2;
        }
        // The game: when the plans and the search of the own field at its full width are expected to be
        // done thinkAgainFrames after the pair has appeared (search() keeps three tenths of the time in
        // hand, and 20 ms stay free). The search of the opponent's field is mostly that of the first
        // choice; when it is not, and the time runs short, the second choice is poor and the first stands.
        if (unitNs <= 0) return ++againNoSpeed, false;
        const int queues = p.queues(), depth = p.depth();
        const int width = firstWidth > 0 ? (firstWidth * p.thinkAgainWidth + 99) / 100 : p.width(); // (the width it must reach: choose())
        const int64_t search = unitNs * width * depth * queues;
        const int64_t have = int64_t(self.wait + p.thinkAgainFrames) * 1000000000 / 60 - 20000000;
        if (againPlansNs + search * 10 / 7 > have) return ++againNoTime, againNeedMs += (againPlansNs + search * 10 / 7) / 1000000, againHaveMs += have / 1000000, false;
        againBudget = int(have / 1000000);
        return true;
    }

    // Params::thinkPonder. A choice has 180 ms; the pair it places is in the air for half a second
    // and more, in which the machine did nothing. So when a pair is on its way, the search of the
    // own field for the next choice is begun (airborne()): on the field as this pair leaves it, with
    // the pairs that are on the screen, the seed of that choice, and as wide as the time the pair is
    // in the air allows. The next choice takes it when all of that has come true and it is at least
    // as wide as the search the clock would allow then: it is that search, made earlier. Garbage
    // that fell, another depth, a pair that went elsewhere: the search is dropped and the choice
    // makes its own, as before. (Measured with two AIs at the speed of the screen: the one that is
    // behind sees one more of its pairs each time the other's next pair appears, so its search is
    // begun again then, with that pair.)
    struct Early {
        bool on = false; // begun, and not taken or dropped yet
        GameField field;
        Pair pair[MAX_KNOWN]{};
        int pairs = 0;
        uint64_t seed = 0;
        int depth = 0, queues = 0, thirdMax = 0, keyed = 0, width = 0; // of its Params: beamDepth, beamQueues, beamThirdMax, evKeyed, beamWidth
        std::future<std::pair<BuildResult, int64_t>> job;              // the result, and the nanoseconds it took
        bool is(const GameField& g, const Pair* known, int count, uint64_t s, const Params& q) const {
            if (count != pairs || s != seed || q.beamDepth != depth || q.beamQueues != queues || q.beamThirdMax != thirdMax || q.evKeyed != keyed || !(g == field)) return false;
            for (int i = 0; i < count; ++i)
                if (!samePair(known[i], pair[i])) return false;
            return true;
        }
    } early;
    Builder earlyBuilder;
    uint64_t earlySeed = 0; // the seed of the choice an early search was last begun for (or tried), and with how many pairs
    int earlyPairs = 0;
    GameField earlySourceField;
    Pair earlySourceColors{};
    int earlySourceTarget = -1;

    // Params::thinkRivalFull. The search of the opponent's field at the width the AI is measured with, as deep as he
    // may have moves, takes longer than the part of a choice it has (thinkRival). His field changes when he puts a
    // pair, not when a choice is made: so the search is begun in the frame his field is known (X3Agent::watchRival, on
    // the thread of frame()) and runs on a thread of its own; the choice that asks for it waits up to its time, and a
    // search not done by then stays at work for the next choice of the same field. One search at a time; a field that
    // is another stops it.
    struct RivalAhead {
        bool on = false; // begun, and not taken or dropped yet
        GameField field;
        Pair pair[MAX_KNOWN]{};
        int pairs = 0, depth = 0, width = 0;
        uint64_t seed = 0;
        bool seeded = false; // the made-up pairs of the search depend on its seed (Builder::seeded)
        std::future<BuildResult> job;
        bool is(const GameField& g, const Pair* known, int count, uint64_t s, const Params& q) const {
            if (!on || count != pairs || q.beamDepth != depth || q.beamWidth != width || (seeded && s != seed) || !(g == field)) return false;
            for (int i = 0; i < count; ++i)
                if (!samePair(known[i], pair[i])) return false;
            return true;
        }
    } rivalAhead;
    Builder rivalBuilder;
    std::mutex rivalLock;   // rivalAhead: the thread of frame() begins searches, the thread of a choice begins and takes them
    int64_t rivalStamp = -1; // watchRival: what the opponent's side was when a search was last begun for it
    std::array<Pair, MAX_KNOWN> rivalObservedPairs{};
    GameField rivalObservedField;
    void dropRivalLocked() {
        if (rivalAhead.job.valid()) {
            rivalBuilder.until(Builder::Clock::now());
            rivalAhead.job.wait();
            rivalAhead.job = std::future<BuildResult>();
        }
        rivalAhead.on = false;
    }
    void dropRival() {
        std::lock_guard<std::mutex> lock(rivalLock);
        dropRivalLocked();
    }
    // Begins the search, unless it is the one at work. wait false (the thread of frame()): not while a choice holds
    // the searches - false then, and nothing is begun.
    bool beginRival(const GameField& field, const Pair* known, int count, const Rules& rules, const Timing& timing, Params q, uint64_t seed, bool wait,
                    Builder::Clock::time_point end = Builder::Clock::time_point::max()) {
        if (!known || count <= 0 || count > MAX_KNOWN) return false;
        std::unique_lock<std::mutex> lock(rivalLock, std::defer_lock);
        if (wait) lock.lock();
        else if (!lock.try_lock()) return false;
        if (!valuesWanted()) q.beamLastValues = 0;
        if (rivalAhead.is(field, known, count, seed, q)) {
            if (end != Builder::Clock::time_point::max()) rivalBuilder.until(end);
            return true;
        }
        dropRivalLocked();
        RivalAhead& r = rivalAhead;
        r.field = field, r.pairs = count, r.depth = q.beamDepth, r.width = q.beamWidth, r.seed = seed, r.seeded = Builder::seeded(rules, q);
        std::array<Pair, MAX_KNOWN> pairs{};
        for (int i = 0; i < count; ++i) r.pair[i] = pairs[size_t(i)] = known[i];
        rivalBuilder.until(std::min(end, Builder::Clock::now() + std::chrono::seconds(3)));
        r.on = true;
        r.job = std::async(std::launch::async, [this, field, pairs, count, rules, timing, q, seed, threads = p.searchThreads] {
            return rivalBuilder.search(field, pairs.data(), count, rules, timing, q, seed, threads);
        });
        return true;
    }
    // The search begun for this field, once it is done: waited for until `end`. False: none for this field, not done
    // by then (it stays at work), or stopped by its own clock.
    bool takeRival(const GameField& field, const Pair* known, int count, Params q, uint64_t seed, Builder::Clock::time_point end, BuildResult& out) {
        std::lock_guard<std::mutex> lock(rivalLock);
        if (!valuesWanted()) q.beamLastValues = 0;
        if (!rivalAhead.is(field, known, count, seed, q) || !rivalAhead.job.valid()) return false;
        if (!cutStale) {
            if (rivalAhead.job.wait_until(end) != std::future_status::ready) return false;
        } else {
            // (cutStale: a choice dropped meanwhile waits no longer)
            for (;;) {
                const Builder::Clock::time_point soon = Builder::Clock::now() + std::chrono::milliseconds(1);
                if (rivalAhead.job.wait_until(soon < end ? soon : end) == std::future_status::ready) break;
                if (stale.load() || Builder::Clock::now() >= end) return false;
            }
        }
        out = rivalAhead.job.get();
        rivalAhead.job = std::future<BuildResult>(), rivalAhead.on = false;
        return !out.cut;
    }

    // Does a choice read BuildResult::value of a search that ran to its end?
    bool valuesWanted() const { return p.x3ByValue || p.x3ValueMix > 0; }
    // How high a search of the own field `g` may put the third column: x3ThirdMax, unless the column is that high
    // already (x3ThirdStrict: higher than that) - then no placement would be left.
    int thirdLimit(const GameField& g) const {
        int h[W];
        g.heights(h);
        return (p.x3ThirdStrict ? h[DEATH_X] > p.x3ThirdMax : h[DEATH_X] >= p.x3ThirdMax) ? 12 : p.x3ThirdMax;
    }
    // The widest a search of the own field is made when the clock allows more (Params::thinkWidthMax).
    int widest() const { return p.thinkWidthMax > p.beamWidth ? p.thinkWidthMax : p.beamWidth; }
    // The pairs of the own tsumo the screen shows from the pair that appears next on, as many as the plans are made with
    // (three at most, Params::x2Depth).
    int pairsShown(const Observation& o) const {
        int pairs = 0;
        for (int i = o.self().nextPair; pairs < p.searchKnownLimit(p.x2Depth) && o.mine().known(i); ++i) ++pairs;
        return pairs;
    }
    // The note of the choice that is being made (null: the round is not noted, or no choice has begun).
    Note* note() { return noting && !notes.empty() ? &notes.back() : nullptr; }
    static int64_t nanos(Builder::Clock::duration d) { return std::chrono::duration_cast<std::chrono::nanoseconds>(d).count(); }
    // A time learnt again: the new sample counts a quarter, the first one in full (0 = none yet).
    static int64_t blend(int64_t learnt, int64_t sample) { return learnt ? (3 * learnt + sample) / 4 : sample; }
    // Is `part` at least `percent` percent of `whole`?
    static bool atLeastPercent(int64_t part, int percent, int64_t whole) { return part * 100 >= whole * percent; }

    // Stops the search begun early, if one is at work, and forgets it.
    void dropEarly() {
        if (early.job.valid()) {
            earlyBuilder.until(Builder::Clock::now());
            early.job.wait();
            early.job = std::future<std::pair<BuildResult, int64_t>>();
        }
        early.on = false;
    }

    // PlacementAgent::cutStale: the choice under way is dropped. The search it makes now (`searching`) and the one begun
    // early are stopped at once; what they took is not learnt (`stale`, until the next choice begins), and the one begun
    // early is dropped as the next choice begins (earlyStale). Only with the clock: a tool's searches run to their end.
    std::atomic<bool> stale{false};
    std::atomic<Builder*> searching{nullptr};
    bool earlyStale = false;
    void abandon() override {
        if (p.thinkBudget <= 0) return;
        stale.store(true);
        const Builder::Clock::time_point now = Builder::Clock::now();
        if (Builder* b = searching.load()) b->until(now);
        earlyBuilder.until(now);
        earlyStale = true;
    }
    void choiceBegins() override {
        stale.store(false);
        if (earlyStale) earlyStale = false, dropEarly();
    }

    void airborne(const Observation& o, Pair colors, int id, int frames) override {
        if (p.thinkBudget <= 0 || !p.thinkPonder || p.thinkFit < 1 || unitNs <= 0) return;
        const Player& self = o.self();
        // The pairs the next choice will see: those on the screen now (the pair in the air is
        // nextPair - 1). A pair of the opponent that appears while this one falls may show one
        // more: the search is then begun again with it, in the time that is left.
        const uint64_t seed = roundSeed * 1000 + uint64_t(self.moves) + 1;
        const int pairs = pairsShown(o);
        if (earlySeed == seed && earlyPairs >= pairs) {
            if (p.knownPairs <= 3) return;
            const auto& visible = o.mine();
            bool same = earlyPairs == pairs && earlySourceTarget == id && samePair(colors, earlySourceColors) && earlySourceField == self.field;
            for (int i = 0; same && i < pairs; ++i) same = samePair(early.pair[i], visible.pair(self.nextPair + i));
            if (same) return;
        }
        dropEarly();
        earlySeed = seed, earlyPairs = pairs;
        if (!pairs) return;
        if (p.knownPairs > 3) {
            earlySourceTarget = id;
            earlySourceColors = colors;
            earlySourceField = self.field;
        }
        Early& e = early;
        e.field = self.field;
        place(e.field, placement(id), colors);
        Trial chain;
        if (o.rules.clearCount != 4 || e.field.f.hasClear()) runTrialChain(e.field.f, o.rules, o.timing, chain, false);
        e.pairs = pairs;
        for (int i = 0; i < pairs; ++i) e.pair[i] = o.mine().pair(self.nextPair + i);
        if (e.field.dead()) return;
        Params q = p; // (as searchBuild and search make them for a choice that builds)
        if (!valuesWanted()) q.beamLastValues = 0;
        q.beamThirdMax = thirdLimit(e.field);
        const int queues = q.queues(), depth = q.depth(), most = widest();
        const int64_t left = int64_t(frames > 0 ? frames : 0) * (1000000000 / 60) * 7 / 10, each = unitNs * depth * queues;
        const int64_t wide = each > 0 ? left / each : most;
        q.beamWidth = int(wide > most ? most : wide < p.thinkWidthMin ? p.thinkWidthMin : wide);
        e.seed = seed;
        e.depth = q.beamDepth, e.queues = q.beamQueues, e.thirdMax = q.beamThirdMax, e.keyed = q.evKeyed, e.width = q.beamWidth;
        earlyBuilder.until(Builder::Clock::now() + std::chrono::seconds(20));
        e.on = true;
        e.job = std::async(std::launch::async, [this, q, rules = o.rules, timing = o.timing] {
            const Builder::Clock::time_point t0 = Builder::Clock::now();
            const BuildResult r = earlyBuilder.search(early.field, early.pair, early.pairs, rules, timing, q, early.seed, p.searchThreads);
            return std::make_pair(r, nanos(Builder::Clock::now() - t0));
        });
    }

    // Params::thinkAhead 0: how many frames before its pair appears a choice is begun, set at the
    // start of every round from how fast this machine searches. A late choice sees the opponent's
    // field nearer to the moment the pair is put (against the fixed Ama, 240 rounds each: begun 12
    // frames before 160 wins, 6 frames before 167, in the frame of the pair 168), an early one has
    // more time: so as late as the searches of a choice still get their full width. What a choice
    // needs then: the plans, a search of the opponent's field, the search of the own field with
    // seven tenths of the time left to it (search()), and the 20 ms that stay free before the
    // pair appears. Not fewer than AHEAD_LEAST frames, not more than the pause before a pair is long.
    static constexpr int AHEAD_LEAST = 4, AHEAD_MOST = 12;
    bool autoAhead = false;
    int budgetSet = 0; // Params::thinkBudget as it was given: the time of a choice is never longer
    void chooseAhead() {
        const int queues = p.queues(), depth = p.depth(), most = widest();
        // (With slowNs: the frames hold for a whole round, and the last few searches of the round
        // before may have been fast ones. Seen once, two AIs at the speed of the screen on a machine
        // under load: 12 frames in the first round, 5 in the second, where 13 of 35 first choices then
        // made their plans with two pairs.)
        const int64_t full = (slowNs > unitNs ? slowNs : unitNs) * most * depth * queues; // one search of the full width
        const int64_t ns = plansNs + full + full * 10 / 7 + 20000000;
        const int64_t frames = (ns * 60 + 999999999) / 1000000000;
        ahead = int(frames < AHEAD_LEAST ? AHEAD_LEAST : frames > AHEAD_MOST ? AHEAD_MOST : frames);
        const int time = ahead * 1000 / 60 - 20;
        p.thinkBudget = time < budgetSet ? time : budgetSet;
    }

    // How fast this machine searches, before the first choice of a match: without it that choice
    // would be made with the narrow search every match begins with. (A search 40 wide on an empty
    // field: its layers are small and shared out less well among the threads than those of a wide
    // one, so it does not promise too much.)
    void learnSpeed() {
        const Rules rules;
        const Pair known[2] = {{rules.palette[0], rules.palette[1]}, {rules.palette[2], rules.palette[3]}};
        builder.untimed();
        const Builder::Clock::time_point t0 = Builder::Clock::now();
        builder.search(GameField{}, known, 2, rules, DEFAULT_TIMING, p, 1, p.searchThreads);
        const int64_t ns = nanos(Builder::Clock::now() - t0);
        const int queues = p.queues(), depth = p.depth(), width = p.width();
        unitNs = ns / (int64_t(width) * depth * queues);
        if (unitNs < 1) unitNs = 1;
        slowNs = unitNs;
    }
    // What a search took, for one node of width, one layer and one queue.
    void learn(int64_t sample) {
        unitNs = blend(unitNs, sample);
        slowNs = sample > slowNs ? sample : (15 * slowNs + sample) / 16;
    }

    // A beam search that is to end when `percent` of the time of the choice are over.
    BuildResult search(Builder& b, const GameField& field, const Pair* known, int count, const Observation& o, Params q, uint64_t seed, int percent,
                       bool fit) {
        // (the values of the last layer are read only by the two ways of choosing by them, and those of
        // a search that the clock stopped, which never has a last layer)
        if (!valuesWanted()) q.beamLastValues = 0;
        if (p.thinkBudget <= 0) {
            if (fit && scripted && widthAt < scripted->widths.size()) q.beamWidth = scripted->widths[widthAt++];
            if (&b == &builder) ownWidth = q.beamWidth;
            return b.search(field, known, count, o.rules, o.timing, q, seed, p.searchThreads);
        }
        const Builder::Clock::time_point end = began + std::chrono::microseconds(int64_t(budgetNow) * 10 * percent), now = Builder::Clock::now();
        const int queues = q.queues(), depth = q.depth();
        // The width the clock allows now.
        int width = q.beamWidth;
        bool narrower = false;
        if (fit && unitNs > 0) {
            // (seven tenths of the time left: the time of a search varies with the field)
            const int64_t left = nanos(end - now) * 7 / 10, need = unitNs * q.beamWidth * depth * queues;
            if (need > left) {
                const int64_t narrow = left > 0 ? int64_t(q.beamWidth) * left / need : 0;
                width = int(narrow > p.thinkWidthMin ? narrow : p.thinkWidthMin < q.beamWidth ? p.thinkWidthMin : q.beamWidth);
                narrower = true;
            } else if (p.thinkWidthMax > q.beamWidth && need > 0) {
                // Time to spare: as wide as the time allows (a faster machine searches more).
                const int64_t wide = int64_t(q.beamWidth) * left / need;
                if (wide > q.beamWidth) width = int(wide < p.thinkWidthMax ? wide : p.thinkWidthMax);
            }
        }
        if (&b == &builder && fit && early.on) {
            if (early.is(field, known, count, seed, q) && early.width >= width) {
                // The search begun while the last pair fell is this one. It ends when this one must.
                searching.store(&earlyBuilder);
                earlyBuilder.until(end);
                if (stale.load()) earlyBuilder.until(Builder::Clock::now()); // (cutStale: the choice was dropped meanwhile)
                const std::pair<BuildResult, int64_t> got = early.job.get();
                searching.store(nullptr);
                const bool dropped = stale.load(); // (cutStale: what it took is not learnt)
                early.on = false;
                ++earlyTaken;
                const BuildResult& r = got.first;
                const int layers = r.cut ? r.layers : depth;
                if (layers >= 3 && !dropped) learn(got.second / (int64_t(early.width < 1 ? 1 : early.width) * layers * queues));
                if (early.width < q.beamWidth) ++narrowSearches;
                ownWidth = early.width;
                if (Note* n = note()) n->widths.push_back(early.width), n->cut += r.cut;
                cutSearches += dropped ? 0 : r.cut;
                return r;
            }
            dropEarly();
        }
        searching.store(&b);
        b.until(end);
        if (stale.load()) b.until(Builder::Clock::now()); // (cutStale: the choice was dropped meanwhile)
        if (narrower) ++narrowSearches, againNarrow += choosingAgain;
        q.beamWidth = width;
        if (&b == &builder) ownWidth = q.beamWidth;
        if (Note* n = note(); n && fit) n->widths.push_back(q.beamWidth);
        BuildResult r = b.search(field, known, count, o.rules, o.timing, q, seed, p.searchThreads);
        searching.store(nullptr);
        const bool dropped = stale.load(); // (cutStale: the choice was dropped - what the search took is not learnt)
        const int layers = r.cut ? r.layers : depth;
        if (layers >= 3 && !dropped) {
            learn(nanos(Builder::Clock::now() - now) / (int64_t(q.width()) * layers * queues));
        } else if (r.cut && unitNs && !dropped) {
            unitNs += unitNs / 2; // (stopped at once: slower than was thought)
            if (unitNs > slowNs) slowNs = unitNs;
        }
        cutSearches += dropped ? 0 : r.cut;
        if (r.cut) poor = true, againCut += choosingAgain && !dropped;
        if (Note* n = note()) n->cut += r.cut;
        return r;
    }

    // The placement of the pair that appears next, among `legal`: the tactics (ai/tactics.h). It begins with
    // beginChoice() and tells plansMade() when its plans are made.
    virtual int decide(const Observation& o, unsigned legal) = 0;

    bool setupChoice() const override { return setupPicked; }

    int choose(const Observation& o, Pair, unsigned legal) override {
        const bool secondChoice = choosingAgain;
        const int first = lastChoice; // (the first choice of the same pair, when this is its second)
        const bool firstSetup = setupPicked;
        // The second choice draws the numbers the first drew: where the third pair changes nothing
        // for it, it is the same choice, and the pair does not turn round by chance.
        if (secondChoice) rng = drawn;
        else drawn = rng;
        int id = decide(o, legal);
        setupPicked = kind == Build;
        if (secondChoice) {
            // A second choice of which the clock stopped a search, or whose search of the own field
            // was much narrower than that of the first (Params::thinkAgainWidth), is not taken for
            // better than the first: the first stands.
            const bool narrow = firstWidth > 0 && ownWidth > 0 && !atLeastPercent(ownWidth, p.thinkAgainWidth, firstWidth);
            if (first >= 0 && (poor || narrow)) id = first, setupPicked = firstSetup, ++againPoor;
            else ++againMade;
        } else {
            firstWidth = ownWidth;
        }
        if (Note* n = note()) n->id = id;
        ++counts[kind];
        ++thinkCount;
        return lastChoice = id;
    }

    bool setupPicked = false;

    // A choice begins (decide() calls this first): the clock is started - the game - or the notes of the round that is
    // played again are looked up - a tool. Returns the pairs the plans of the choice are made with. secondChoice: it is
    // the second choice of the pair (Params::thinkAgain).
    int beginChoice(const Observation& o, bool secondChoice) {
        poor = false, ownWidth = 0;
        if (!secondChoice) firstPairs = pairsShown(o);
        int pairs = p.x2Depth;
        if (p.thinkBudget > 0) {
            // The plans are not stopped by the clock: they are what an answer is made of. With three
            // pairs on the screen they take about twenty times as long as with two; a machine on
            // which that is a large part of the time of a choice makes them with two. (What they
            // took is then forgotten little by little, so that three are tried again.)
            began = Builder::Clock::now();
            budgetNow = secondChoice ? againBudget : p.thinkBudget;
            // (garbage fell, or the pair went elsewhere: the search begun early is of no use, and must not take the threads)
            if (early.on && !(early.field == o.self().field)) dropEarly();
            // (a second choice is made for the third pair: its plans are made with three, and again() has looked at the time)
            if (!secondChoice && pairs >= 3 && p.thinkPlans > 0 && plansNs > int64_t(p.thinkBudget) * 10000 * p.thinkPlans) pairs = 2, plansNs -= plansNs / 50, ++shortPlans;
        } else {
            // (a tool that plays a round of the game again: the pairs of the plans as they were there;
            // of two notes for one move the first is that of the first choice, the last that of the second)
            scripted = nullptr, widthAt = 0;
            if (script)
                for (const Note& n : *script)
                    if (n.move == int(o.self().moves) && (secondChoice || !scripted)) scripted = &n;
            if (scripted && scripted->cut > 0) poor = true; // (as in search(): the clock had stopped a search of it)
            if (scripted && scripted->pairs > 0) pairs = scripted->pairs;
        }
        if (noting) notes.push_back(Note{int(o.self().moves), pairs});
        return pairs;
    }
    // The plans of the choice are made. three: both players had three pairs on the screen for them. (The game: what
    // they took is kept, for beginChoice() and again().)
    void plansMade(int pairs, bool secondChoice, bool three) {
        if (p.thinkBudget <= 0 || stale.load()) return; // (cutStale: a dropped choice is not learnt from)
        const int64_t took = nanos(Builder::Clock::now() - began);
        if (pairs >= 3 && three) plansNs = blend(plansNs, took);
        if (secondChoice) againPlansNs = blend(againPlansNs, took);
    }
};

} // namespace juraic::ai
