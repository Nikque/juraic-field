#ifdef JURAIC_DECISION_METRICS
#include "eval_fixed.h"
#endif
// Matches between two AIs without a screen.
//
//   ai_match [--pairs N] [--threads T] [--seed S] [--a NAME] [--b NAME]
//            [--param name=value]... [--param-a name=value]... [--param-b name=value]...
//            [--set key=value]... [--max-frames F] [--replays DIR N] [--same-seeds] [--self-test]
//
// --param changes a value of ai/params.h for both sides, --param-a and
// --param-b for one side (a name=value pair, or a file of such lines).
// --primary-routes uses the real game foreground search limit for Ex (also in its reading).
// --same-seeds gives both agents the same random numbers in a round: for a
// setting measured against the AI itself (--a x3 --b x3 --param-a ...). X3 has
// little chance of its own, so the two then play the same placements until the
// setting makes one of them choose otherwise, and the match counts what the
// setting changes. Rounds in which it changes nothing are draws, many of them
// at the frame limit (two X3 without any difference, 40 rounds: 37 draws, 21
// of them at the limit; with their own seeds 19 to 21), so give such a match a
// smaller --max-frames.
//
// --own-tsumo is for measuring only (the levels of the AI):
// seat 0 and seat 1 each draw from a tsumo of their own, and
// an agent sees of the other's tsumo only what the other's window shows. In
// the two rounds of a pair each agent plays each of the two tsumo once. Such
// rounds are not rounds of the game (it has one tsumo): --replays saves none.
//
// One pair = two rounds with the same seed (the same tsumo), the seats
// exchanged. The result does not depend on the number of threads: pair k uses
// only random numbers made from (seed, k), every thread has its own agents, and
// the results are put together in the order of k. The last line printed is a
// checksum of all rounds.
//
// --set changes a rule by the names of the game's settings (colors, clearCount,
// targetPoints, marginSeconds, chainRequirement, ...; see play/settings.h).
// --replays saves the first N pairs as replays of the game (two rounds each)
// with a file <replay>.expected next to each; "JuraicField.exe --check-replay
// <replay>" plays the replay and compares.
// --self-test plays a small match three times (1 thread, 1 thread, 8 threads)
// and fails when the results differ.
// --kinds prints how many choices of each kind (ai/pace.h X3Pace::Kind) the X3 agents made, and for an X3 with
// Params::x3Crush in how many choices the crush was looked for and how far it got. --trace-crush prints
// every pair an X3 placed by a crush (Params::x3Crush), both fields as it locked, and what followed in the next ten
// seconds: the garbage that fell on either field, the largest chain the opponent fired, and who won the round.
// Neither changes a move.
#ifndef _MSC_VER
#define sscanf_s sscanf
#endif
#include "../ai/agents.h"
#include "../play/memory/primary_timing14.h"
#include "../ai/answer.h"
#include "../generator/thread_pool.h"
#include "../play/replay.h"
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <system_error>
#include <thread>

using namespace juraic;

namespace {

constexpr uint64_t MATCH_STREAM = 0x6D61746368; // seeds of the rounds of a match
constexpr uint64_t APART_STREAM = 0x6170617274; // ... of the tsumo of seat 1 (--own-tsumo)

struct Options {
    int pairs = 100, threads = 1;
    uint64_t seed = 1;
    std::string a = "random", b = "random";
    ai::Params paramsA, paramsB;
    play::Settings settings;
    int64_t maxFrames = 60 * 60 * 10; // ten minutes: then the round is a draw
    std::filesystem::path replayDirectory;
    int replays = 0;
    bool sameSeeds = false; // --same-seeds: B draws the random numbers of A
    bool ownTsumo = false;  // --own-tsumo: each seat draws from a tsumo of its own
    bool kinds = false;      // --kinds
    bool traceCrush = false; // --trace-crush
    bool traceAnswers = false; // --trace-answers
    bool primaryRoutes = false; // --primary-routes uses the live foreground limit for Ex routes.
    bool exA = false, exB = false; // --ex-a, --ex-b: that agent takes the way over soonest (PlacementAgent::fastestWays, the Ex of play/level_ex.h)
};

void configureRoutes(ai::Agent* agent, bool ex, bool primaryRoutes) {
    if (auto* placing = dynamic_cast<ai::PlacementAgent*>(agent)) {
        placing->fastestWays = ex;
        if (ex && primaryRoutes) placing->fastestStates = play::realgame::PRIMARY_FASTEST_NOW14;
    }
}

struct RoundRequest {
    int pair = -1, aSeat = -1;
    std::filesystem::path directory, ledger;
    std::string attempt;
};

// --trace-crush: a pair placed by a crush, and what followed in the CRUSH_FOLLOW frames after its lock.
constexpr int64_t CRUSH_FOLLOW = 10 * FPS;
struct CrushFollow {
    int seat = 0;
    int64_t from = 0;
    ai::X3Agent::CrushNote note;
    int cells[2]{}; // the crusher's field and the opponent's as the pair locked
    int hisFallen = 0, ownFallen = 0, hisWaves = 0, hisSent = 0;
    int ownWaves = 0, ownSent = 0; // the crusher's own chain after the lock
    int placed = -1;               // the placement the pair locked at (Placement of game/field.h as an id)
    int64_t firstFall = -1; // frames after the lock until garbage first fell on him (-1: none)
    int waitingEnd = 0;     // garbage still waiting above him at the end
    std::string fields;     // both fields as the pair locked, the crusher's on the left
};

// --trace-answers: a chain fired, and what the other player did about it until its garbage was settled.
//  - His field is looked at when it is at rest: as the chain begins, or - his pair landing then - as he next waits for a
//    pair or holds one. (A chain of his own running then, or none of that before the chain is over: not counted.)
//  - Followed until the chain has ended and no garbage waits above him while he waits for a pair or holds one (what was
//    to fall has fallen, or he has offset it); ANSWER_LONGEST frames after its end at the most.
//  - A chain the attacker fires before that makes the two one attack: the first of them is not counted.
// ANSWER_LEAST: chains that made less garbage than this are not reported (a piece or two is often taken on purpose).
// ANSWER_SHORT: an attack that leaves him this many pairs or fewer is a short one - he answers with what stands; with
// more he builds on while the chain runs, which the chance of the pieces for a chain that stands does not tell.
constexpr int64_t ANSWER_LONGEST = 30 * FPS;
constexpr int ANSWER_LEAST = 6;
constexpr int ANSWER_SHORT = 4;
constexpr int ANSWER_PAIR_FRAMES = 50; // the frames of a pair, for the pairs he has while a chain runs
struct AnswerFollow {
    int attacker = 0;
    int64_t from = 0, ended = -1; // the frame of its first wave, and the one it ended in (-1: the round ended first)
    bool busy = false;            // a chain of the other player's own ran when his field was to be looked at, or it never was
    bool looked = false;          // his field has been looked at
    bool settled = false;         // followed to its end
    bool mixed = false;           // the attacker fired again before it was settled
    ai::Replies replies;          // the chains his field held when it was looked at (ai/answer.h)
    Pair known[3];                // the pairs he had on the screen then, the one in his hands first
    int knownCount = 0;
    // The chains the pairs on his screen fire, every sequence of placements tried (ai/gaze.h): the points, the pair
    // that fires (1 to 3), the main chain left.
    struct OnScreen {
        int points = 0, depth = 0, mainAfter = 0;
    };
    std::vector<OnScreen> screen;
    int movesAtStart = 0; // the pairs he had locked when his field was looked at
    int pairs = 0;        // the pairs he could fire with before the garbage fell: one in ANSWER_PAIR_FRAMES frames of the chain, and the next
    int waitingBefore = 0, pool = 0, rate = 0; // the garbage above him before the chain, his pool, the points of a garbage piece
    int sent = 0;         // the garbage the chain made
    int fallen = 0;       // the garbage that fell on him from the first wave on
    int hisWaves = 0;     // the longest chain he fired in that time
    int hisCells = 0;     // the pieces on his field when it was looked at
    std::string fields;   // both fields then, his on the left
};

struct RoundResult {
    RoundState state = RoundState::Playing; // Playing = stopped at maxFrames
    int64_t frames = 0;
    int score[2]{}, moves[2]{}, maxChain[2]{}, sent[2]{};
    int chains[2]{}, smallChains[2]{}; // chains fired, and those of them with three waves or fewer
    int fallen[2]{};                   // garbage that fell on the player's field
    int64_t bigAt[2] = {-1, -1};       // the frame the player's first chain of five waves or more began in (-1: none)
    int bigCells[2]{};                 // the cells on his field before it
    std::string summary; // play::roundSummary
    std::vector<CrushFollow> crushes; // --trace-crush
    std::vector<AnswerFollow> answers; // --trace-answers
};

// seed1: null = one tsumo for both; else player 1 draws from the tsumo of that seed (--own-tsumo).
// crushes: record the pairs placed by a crush (--trace-crush).
RoundResult playRound(const Rules& rules, uint64_t seed, ai::Agent* agent[2], const uint64_t agentSeed[2], int64_t maxFrames,
                      play::RoundLog* log, const uint64_t* seed1 = nullptr, bool crushes = false, bool answers = false) {
    Round r;
    r.start(rules, seed);
    if (seed1) r.splitTsumo(*seed1);
    for (int i = 0; i < 2; ++i) agent[i]->startRound(i, agentSeed[i]);
    if (log) *log = play::RoundLog{}, log->seed = seed;
    RoundResult result;
    ai::Observation o;
    PlayerEvents e[2];
    int waves[2] = {0, 0}; // the waves so far of the chain a player fired last
    int64_t began[2] = {0, 0}; // the frame the chain a player fired last began in, and the cells on his field before it
    int beganCells[2] = {0, 0};
    while (r.state == RoundState::Playing && r.frame < maxFrames) {
        ai::observe(r, 0, o);
        uint8_t buttons[2];
        buttons[0] = agent[0]->frame(o);
        o.me = 1;
        buttons[1] = agent[1]->frame(o);
        if (log) log->frames.push_back(uint16_t(buttons[0] | buttons[1] << 8));
        const int cellsBefore[2] = {r.player[0].field.f.count(), r.player[1].field.f.count()};
        const int movesBefore[2] = {r.player[0].moves, r.player[1].moves};
        r.step(buttons, e);
        int dropped[2] = {0, 0};
        for (int i = 0; i < 2; ++i)
            if (e[i].waveStarted) {
                result.maxChain[i] = std::max(result.maxChain[i], r.player[i].chain.chains);
                result.sent[i] += r.player[i].wave.sent;
                if (r.player[i].chain.chains == 1) ++result.chains[i], result.smallChains[i] += waves[i] >= 1 && waves[i] <= 3;
                if (r.player[i].chain.chains == 1) began[i] = r.frame, beganCells[i] = cellsBefore[i];
                if (r.player[i].chain.chains == 5 && result.bigAt[i] < 0) result.bigAt[i] = began[i], result.bigCells[i] = beganCells[i];
                waves[i] = r.player[i].chain.chains;
            }
        for (int i = 0; i < 2; ++i)
            if (e[i].garbageFell)
                for (int x = 0; x < W; ++x) result.fallen[i] += r.player[i].dropped[x], dropped[i] += r.player[i].dropped[x];
        if (answers) {
            // (his field at rest: the chains it holds, the pairs on his screen and what they fire)
            const auto look = [&](AnswerFollow& f) {
                const int him = 1 - f.attacker;
                const Player& q = r.player[him];
                ai::Observation seen;
                ai::observe(r, him, seen);
                const ai::Params params;
                ai::repliesOf(q.field, rules, seen.timing, params, f.replies);
                if (q.phase == Phase::Control) f.known[f.knownCount++] = q.colors;
                const ai::SeenTsumo& shown = seen.pairsOf(him);
                for (int k = q.nextPair; f.knownCount < 3 && shown.known(k); ++k) f.known[f.knownCount++] = shown.pair(k);
                ai::Gaze gaze;
                ai::PlanSet set;
                gaze.plans(seen, him, false, 3, params, 1, set);
                for (const ai::Plan& s : set.plans)
                    if (s.fireDepth) f.screen.push_back({s.points, s.fireDepth, s.mainAfter});
                f.movesAtStart = q.moves, f.pool = q.scoring.pool, f.hisCells = q.field.f.count(), f.looked = true;
                for (int y = H - 1; y >= 0; --y) {
                    for (int k = 0; k < 2; ++k) {
                        for (int x = 0; x < W; ++x) f.fields += ".12345x"[std::min<int>(r.player[k == 0 ? him : f.attacker].field.f.get(x, y), 6)];
                        f.fields += k ? "\n" : "   ";
                    }
                }
            };
            for (int i = 0; i < 2; ++i) {
                if (!e[i].waveStarted || r.player[i].chain.chains != 1) continue;
                // (the attacker fires again before an attack of his was settled: that one is not counted)
                for (AnswerFollow& old : result.answers)
                    if (old.attacker == i && !old.settled) old.mixed = true;
                const Player& q = r.player[1 - i];
                AnswerFollow f;
                f.attacker = i, f.from = r.frame, f.rate = rules.rateAt(r.frame);
                f.waitingBefore = std::max(0, q.incoming.total() - r.player[i].wave.sent);
                f.busy = q.phase == Phase::Wave;
                if (!f.busy && (q.phase == Phase::Next || q.phase == Phase::Control)) look(f);
                result.answers.push_back(std::move(f));
            }
            for (AnswerFollow& f : result.answers) {
                if (f.settled) continue;
                const int him = 1 - f.attacker;
                const Player& q = r.player[him];
                const bool resting = q.phase == Phase::Next || q.phase == Phase::Control;
                if (f.ended < 0) {
                    // (his pair was landing as the chain began: looked at once his field is at rest)
                    if (!f.busy && !f.looked) {
                        if (q.phase == Phase::Wave) f.busy = true;
                        else if (resting) look(f);
                    }
                    if (e[f.attacker].waveStarted) f.sent += r.player[f.attacker].wave.sent;
                    if (e[f.attacker].chainEnded) {
                        f.ended = r.frame;
                        // (the pairs he has time for while the chain runs, and the one in his hands or to come)
                        if (f.looked) f.pairs = int((r.frame - f.from) / ANSWER_PAIR_FRAMES) + 1;
                        else f.busy = true;
                    }
                }
                f.fallen += dropped[him];
                if (e[him].waveStarted) f.hisWaves = std::max(f.hisWaves, q.chain.chains);
                if (f.ended >= 0 && ((resting && q.incoming.total() == 0) || q.phase == Phase::Dead || r.frame - f.ended > ANSWER_LONGEST)) f.settled = true;
            }
        }
        if (!crushes) continue;
        for (CrushFollow& f : result.crushes) {
            if (r.frame - f.from > CRUSH_FOLLOW) continue;
            const int him = 1 - f.seat;
            f.hisFallen += dropped[him], f.ownFallen += dropped[f.seat];
            if (dropped[him] && f.firstFall < 0) f.firstFall = r.frame - f.from;
            if (e[him].waveStarted) f.hisWaves = std::max(f.hisWaves, r.player[him].chain.chains), f.hisSent += r.player[him].wave.sent;
            if (e[f.seat].waveStarted) f.ownWaves = std::max(f.ownWaves, r.player[f.seat].chain.chains), f.ownSent += r.player[f.seat].wave.sent;
            f.waitingEnd = r.player[him].incoming.total();
        }
        for (int i = 0; i < 2; ++i) {
            const auto* x3 = dynamic_cast<const ai::X3Agent*>(agent[i]);
            if (!x3 || r.player[i].moves == movesBefore[i] || x3->kind != ai::X3Agent::Crush) continue;
            CrushFollow f;
            f.seat = i, f.from = r.frame, f.note = x3->crushNote, f.placed = placementId(r.player[i].pair.placement());
            for (int k = 0; k < 2; ++k) f.cells[k] = r.player[k == 0 ? i : 1 - i].field.f.count();
            for (int y = H - 1; y >= 0; --y) {
                for (int k = 0; k < 2; ++k) {
                    for (int x = 0; x < W; ++x) f.fields += ".12345x"[std::min<int>(r.player[k == 0 ? i : 1 - i].field.f.get(x, y), 6)];
                    f.fields += k ? "\n" : "   ";
                }
            }
            result.crushes.push_back(f);
        }
    }
    for (int i = 0; i < 2; ++i) result.smallChains[i] += waves[i] >= 1 && waves[i] <= 3;
    result.state = r.state;
    result.frames = r.frame;
    for (int i = 0; i < 2; ++i) result.score[i] = r.player[i].scoring.score, result.moves[i] = r.player[i].moves;
    result.summary = play::roundSummary(r);
    return result;
}

struct MatchResult {
    std::vector<RoundResult> rounds; // 2k: A in seat 0; 2k + 1: B in seat 0
    double seconds = 0;
    uint64_t checksum = 0;
    int64_t kinds[2][ai::X3Pace::KINDS]{}; // --kinds: the choices of A and of B by kind
    ai::X3Agent::CrushCounts crush[2];     // ... and how far the crush got in them
};

uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v;
    h *= 0xFF51AFD7ED558CCDull;
    return h ^ h >> 32;
}

#include "recorded_round.h"

bool runMatch(const Options& opt, MatchResult& match) {
    if (!ai::makeAgent(opt.a) || !ai::makeAgent(opt.b)) {
        std::cerr << "unknown agent (known: " << ai::agentNames() << ")\n";
        return false;
    }
    match.rounds.assign(size_t(opt.pairs) * 2, {});
    std::atomic_int next{0};
    std::atomic_bool failed{false};
    std::mutex kindsLock;
    const auto start = std::chrono::steady_clock::now();
    const auto worker = [&] {
        const std::unique_ptr<ai::Agent> a = ai::makeConfiguredAgent(opt.a, opt.paramsA), b = ai::makeConfiguredAgent(opt.b, opt.paramsB);
        configureRoutes(a.get(), opt.exA, opt.primaryRoutes);
        configureRoutes(b.get(), opt.exB, opt.primaryRoutes);
        for (;;) {
            const int k = next.fetch_add(1);
            if (k >= opt.pairs) break;
            Rng rng(opt.seed, MATCH_STREAM + uint64_t(k));
            const uint64_t seed = rng.next() % 1000000000ull;
            // (--own-tsumo: the tsumo of seat 1, from a stream of its own, so that the other numbers of the pair stay what they are)
            const uint64_t seed1 = Rng(opt.seed, APART_STREAM + uint64_t(k)).next() % 1000000000ull;
            play::Replay replay{opt.settings, 2, play::TIMING_VERSION, {}};
            const bool save = k < opt.replays;
            std::string expected;
            for (int game = 0; game < 2; ++game) {
                ai::Agent* agent[2] = {game ? b.get() : a.get(), game ? a.get() : b.get()};
                const uint64_t seedA = rng.next(), drawnB = rng.next();
                // (--same-seeds: both agents draw the same random numbers; see the top of the file)
                const uint64_t seedB = opt.sameSeeds ? seedA : drawnB;
                const uint64_t agentSeed[2] = {game ? seedB : seedA, game ? seedA : seedB};
                play::RoundLog log;
                RoundResult& result = match.rounds[size_t(k) * 2 + size_t(game)];
                result = playRound(opt.settings.rules, seed, agent, agentSeed, opt.maxFrames, save ? &log : nullptr, opt.ownTsumo ? &seed1 : nullptr,
                                   opt.traceCrush, opt.traceAnswers);
                if (save) replay.rounds.push_back(std::move(log)), expected += result.summary + '\n';
            }
            if (save) {
                const std::filesystem::path file =
                    opt.replayDirectory / ("match-" + std::to_string(opt.seed) + "-" + std::to_string(k) + ".jfreplay");
                if (!play::saveReplay(file, replay)) failed = true;
                std::ofstream expectedOut(file.string() + ".expected", std::ios::binary);
                expectedOut << expected;
                expectedOut.flush();
                if (!expectedOut) failed = true;
            }
        }
        const std::lock_guard<std::mutex> hold(kindsLock);
        for (int who = 0; who < 2; ++who)
            if (const auto* x3 = dynamic_cast<const ai::X3Agent*>(who ? b.get() : a.get())) {
                for (int k = 0; k < ai::X3Pace::KINDS; ++k) match.kinds[who][k] += x3->counts[k];
                ai::X3Agent::CrushCounts& c = match.crush[who];
                const ai::X3Agent::CrushCounts& n = x3->crushCounts;
                c.asked += n.asked, c.none += n.none, c.spends += n.spends, c.answered += n.answered, c.outlasted += n.outlasted, c.fired += n.fired,
                    c.built += n.built;
            }
    };
    try {
        runWorkers(opt.threads, worker, [&] { next = opt.pairs; });
    } catch (const std::exception& e) {
        std::cerr << "match failed: " << e.what() << '\n';
        return false;
    } catch (...) {
        std::cerr << "match failed with an unknown exception\n";
        return false;
    }
    match.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    match.checksum = 0x9E3779B97F4A7C15ull;
    for (const RoundResult& r : match.rounds)
        for (const char c : r.summary) match.checksum = mix(match.checksum, uint8_t(c));
    if (failed) std::cerr << "a replay could not be saved\n";
    return !failed;
}

// When the players first fired a chain of five waves or more.
void reportFirstBig(const MatchResult& match) {
    int players = 0, fired = 0, before15 = 0, before20 = 0, won20 = 0, lost20 = 0;
    int64_t cells20 = 0, frames = 0;
    for (const RoundResult& r : match.rounds)
        for (int i = 0; i < 2; ++i) {
            ++players;
            if (r.bigAt[i] < 0) continue;
            ++fired, frames += r.bigAt[i];
            before15 += r.bigAt[i] < 15 * FPS;
            if (r.bigAt[i] >= 20 * FPS) continue;
            ++before20, cells20 += r.bigCells[i];
            const bool won = r.state == (i ? RoundState::Won1 : RoundState::Won0), lost = r.state == (i ? RoundState::Won0 : RoundState::Won1);
            won20 += won, lost20 += lost;
        }
    std::cout << "first chain of five waves or more: fired by " << fired << " of " << players << " players of a round, on average at "
              << (fired ? double(frames) / fired / FPS : 0.0) << " s; begun before 15 s by " << before15 << ", before 20 s by " << before20;
    if (before20) std::cout << " (" << double(cells20) / before20 << " cells on the field on average; the round then won " << won20 << ", lost " << lost20 << ")";
    std::cout << '\n';
}

// --trace-answers: for every attack counted (AnswerFollow), what was reckoned of the player attacked against whether
// garbage fell on him.
//  - on the screen: a chain the pairs on his screen fire in time (one of the pairs he places before the garbage falls)
//    offsets all of it - told exactly;
//  - the chance: that he has the pieces for a chain his field holds that offsets all of it (ai::answerChance), the
//    pairs after those on the screen counted;
//  - together: sure with an answer on the screen, else the chance.
// Short attacks (ANSWER_SHORT pairs or fewer for him) and long ones apart. Then cases of the short ones that went
// otherwise than reckoned, with both fields.
void reportAnswers(const Options& opt, const MatchResult& match) {
    static const int EDGE[] = {0, 100000, 300000, 500000, 700000, 900000, 1000001};
    static const char* NAME[] = {"below 10 %", "10 to 30 %", "30 to 50 %", "50 to 70 %", "70 to 90 %", "90 % and more"};
    struct Tally {
        int n = 0, kept = 0;             // attacks, and those after which no garbage fell
        int screenN = 0, screenKept = 0; // ... with an answer on the screen
        int bin[6]{}, binKept[6]{};      // ... without one, by the chance
        int fired = 0;                   // ... in which he fired a chain
        double error = 0;                // squared error of "together"
    } tally[2];
    int busy = 0, small = 0, open = 0, mixed = 0;
    std::vector<std::string> shown[2]; // reckoned below 10 % and nothing fell; reckoned 90 % and more and garbage fell
    for (size_t k = 0; k < match.rounds.size(); ++k)
        for (const AnswerFollow& f : match.rounds[k].answers) {
            if (f.busy) {
                ++busy;
                continue;
            }
            if (!f.settled) {
                ++open;
                continue;
            }
            if (f.mixed) {
                ++mixed;
                continue;
            }
            if (f.sent < ANSWER_LEAST) {
                ++small;
                continue;
            }
            const int need = (f.sent + f.waitingBefore) * f.rate - f.pool;
            bool onScreen = false;
            int largest = 0;
            for (const AnswerFollow::OnScreen& s : f.screen) {
                if (s.depth <= f.pairs && s.points > largest) largest = s.points;
                if (s.depth <= f.pairs && s.points >= need) onScreen = true;
            }
            const int64_t chance = ai::answerChance(f.replies, need, 0, f.pairs, f.known, f.knownCount, opt.settings.rules);
            const int64_t together = onScreen ? ai::CHANCE_SURE : chance;
            const bool kept = f.fallen == 0;
            Tally& t = tally[f.pairs > ANSWER_SHORT];
            ++t.n, t.kept += kept, t.fired += f.hisWaves > 0;
            if (onScreen) {
                ++t.screenN, t.screenKept += kept;
            } else {
                int bin = 0;
                while (chance >= EDGE[bin + 1]) ++bin;
                ++t.bin[bin], t.binKept[bin] += kept;
            }
            const double p = double(together) / double(ai::CHANCE_SURE);
            t.error += (p - kept) * (p - kept);
            const int which = together < EDGE[1] && kept ? 0 : together >= EDGE[5] && !kept ? 1 : -1;
            if (f.pairs <= ANSWER_SHORT && which >= 0 && shown[which].size() < 8) {
                int standing = 0;
                for (int i = 0; i < f.replies.count; ++i) standing = std::max(standing, f.replies.item[i].points);
                std::string pairs;
                for (int i = 0; i < f.knownCount; ++i) pairs += std::string(i ? " " : "") + char('0' + f.known[i].axis) + char('0' + f.known[i].child);
                shown[which].push_back("round " + std::to_string(k) + ", frame " + std::to_string(f.from) + ": " + std::to_string(f.sent) + " garbage (" + std::to_string(f.waitingBefore) +
                                       " waiting before), " + std::to_string(need) + " points to offset it; he had " + std::to_string(f.pairs) + " pairs (on the screen " + pairs +
                                       "), " + std::to_string(f.hisCells) + " pieces; the largest chain on the screen in time " + std::to_string(largest) +
                                       " points, the largest that stands " + std::to_string(standing) + "; he fired " + std::to_string(f.hisWaves) + " waves, " +
                                       std::to_string(f.fallen) + " fell\n" + f.fields);
            }
        }
    std::cout << "answers: not counted: " << small << " chains of fewer than " << ANSWER_LEAST << " garbage, " << busy << " with the other field not at rest in time, " << mixed
              << " the attacker added to, " << open << " not settled with the round\n";
    for (int c = 0; c < 2; ++c) {
        const Tally& t = tally[c];
        std::cout << "  " << (c ? "long attacks (more than " : "short attacks (") << ANSWER_SHORT << (c ? " pairs for him): " : " pairs for him or fewer): ") << t.n << ", no garbage fell after "
                  << t.kept << "; he fired in " << t.fired << '\n';
        if (!t.n) continue;
        std::cout << "    an answer on the screen: " << t.screenKept << " of " << t.screenN << '\n';
        for (int i = 0; i < 6; ++i)
            if (t.bin[i]) std::cout << "    none on the screen, the chance of the pieces " << NAME[i] << ": " << t.binKept[i] << " of " << t.bin[i] << '\n';
        const double share = double(t.kept) / t.n;
        std::cout << "    mean squared error: together " << t.error / t.n << ", the share alone " << share * (1 - share) << '\n';
    }
    for (int w = 0; w < 2; ++w) {
        std::cout << (w ? "short attacks reckoned 90 % and more to be answered, and garbage fell:\n" : "short attacks reckoned below 10 % to be answered, and nothing fell:\n");
        for (const std::string& s : shown[w]) std::cout << s << '\n';
    }
}

void report(const Options& opt, const MatchResult& match) {
    reportFirstBig(match);
    // Seen from A. A draw (or a round stopped at the limit) counts half.
    int wins = 0, losses = 0, draws = 0, unfinished = 0, seat0 = 0;
    double sum = 0, squares = 0;
    int64_t frames = 0, longest = 0;
    int64_t chainSum[2]{}, sentSum[2]{}, moveSum[2]{}, scoreSum[2]{}, firedSum[2]{}, smallSum[2]{}, fallenSum[2]{};
    int chainMax[2]{};
    for (int k = 0; k < opt.pairs; ++k) {
        double pairScore = 0;
        for (int game = 0; game < 2; ++game) {
            const RoundResult& r = match.rounds[size_t(k) * 2 + size_t(game)];
            const int seatA = game; // A sits in seat 0 in the first round of the pair
            const bool won0 = r.state == RoundState::Won0, won1 = r.state == RoundState::Won1;
            const bool aWon = seatA == 0 ? won0 : won1, bWon = seatA == 0 ? won1 : won0;
            wins += aWon, losses += bWon, draws += !aWon && !bWon, unfinished += r.state == RoundState::Playing, seat0 += won0;
            pairScore += aWon ? 1 : bWon ? 0 : 0.5;
            frames += r.frames, longest = std::max(longest, r.frames);
            for (int who = 0; who < 2; ++who) { // 0 = A, 1 = B
                const int seat = who == 0 ? seatA : 1 - seatA;
                chainSum[who] += r.maxChain[seat], sentSum[who] += r.sent[seat], moveSum[who] += r.moves[seat], scoreSum[who] += r.score[seat];
                firedSum[who] += r.chains[seat], smallSum[who] += r.smallChains[seat], fallenSum[who] += r.fallen[seat];
                chainMax[who] = std::max(chainMax[who], r.maxChain[seat]);
            }
        }
        pairScore /= 2;
        sum += pairScore, squares += pairScore * pairScore;
    }
    const int rounds = opt.pairs * 2;
    const double mean = sum / opt.pairs;
    const double variance = opt.pairs > 1 ? (squares - opt.pairs * mean * mean) / (opt.pairs - 1) : 0;
    const double half = 1.96 * std::sqrt((variance > 0 ? variance : 0) / opt.pairs);
    std::cout << "A = " << opt.a << ", B = " << opt.b << ", " << opt.pairs << " pairs (" << rounds << " rounds), seed " << opt.seed << ", "
              << opt.threads << " thread(s)\n";
    if (opt.ownTsumo) std::cout << "each seat draws from a tsumo of its own (--own-tsumo)\n";
    if (opt.exA || opt.exB) std::cout << "Ex (the way over soonest): a " << (opt.exA ? "on" : "off") << ", b " << (opt.exB ? "on" : "off") << ", cancel " << (opt.settings.rules.cancel ? "on" : "off") << "\n";
    std::cout << "A wins " << wins << ", B wins " << losses << ", draws " << draws << " (" << unfinished << " stopped at the frame limit)\n";
    std::cout << "score of A: " << 100 * mean << " %, 95% interval " << 100 * (mean - half) << " .. " << 100 * (mean + half)
              << " (by pairs)\n";
    std::cout << "seat 0 won " << seat0 << " of " << rounds << " rounds\n";
    std::cout << "round length: " << double(frames) / rounds / FPS << " s on average, longest " << double(longest) / FPS << " s\n";
    for (int who = 0; who < 2; ++who)
        std::cout << "AB"[who] << ": longest chain of a round " << double(chainSum[who]) / rounds << " on average, at most " << chainMax[who]
                  << "; garbage sent " << double(sentSum[who]) / rounds << ", moves " << double(moveSum[who]) / rounds << ", score "
                  << double(scoreSum[who]) / rounds << " per round; chains fired " << double(firedSum[who]) / rounds << " a round, "
                  << double(smallSum[who]) / rounds << " of them of three waves or fewer; garbage fallen on its field "
                  << double(fallenSum[who]) / rounds << " a round\n";
    if (opt.kinds) {
        static constexpr const char* names[] = {"none", "win", "answer", "answer building", "poke", "build", "fire", "clear", "crush", "crush building"};
        static_assert(std::size(names) == size_t(ai::X3Pace::KINDS), "a name for every kind of choice");
        for (int who = 0; who < 2; ++who) {
            std::cout << "AB"[who] << " choices:";
            for (int k = 0; k < ai::X3Pace::KINDS; ++k) std::cout << (k ? ", " : " ") << names[k] << ' ' << match.kinds[who][k];
            std::cout << '\n';
            const ai::X3Agent::CrushCounts& c = match.crush[who];
            if (c.asked)
                std::cout << "AB"[who] << " crush looked for in " << c.asked << " choices: no chain of the pairs it may use large enough " << c.none
                          << ", every such chain spends the main chain " << c.spends << ", answered " << c.answered << ", his main chain not offset "
                          << c.outlasted << ", fired " << c.fired << ", built towards " << c.built << '\n';
        }
    }
    if (opt.traceAnswers) reportAnswers(opt, match);
    if (opt.traceCrush) {
        int crushes[2]{}, crushRounds[2]{}, crushWon[2]{}, answered[2]{};
        int64_t fell[2]{};
        for (size_t i = 0; i < match.rounds.size(); ++i) {
            const RoundResult& r = match.rounds[i];
            const int seatA = int(i % 2);
            bool in[2]{};
            for (const CrushFollow& f : r.crushes) {
                const int who = f.seat == seatA ? 0 : 1;
                const bool won = r.state == (f.seat == 0 ? RoundState::Won0 : RoundState::Won1), lost = r.state == (f.seat == 0 ? RoundState::Won1 : RoundState::Won0);
                std::cout << "crush: round " << i << ' ' << "AB"[who] << " frame " << f.from << ", cells " << f.cells[0] << '/' << f.cells[1] << ", sends "
                          << f.note.sent << ", at least " << f.note.left << " left, main chains " << f.note.mine << '/' << f.note.his
                          << " points; read: placement " << f.note.id << ", " << f.note.chains << " waves; placed " << f.placed << ", own chain "
                          << f.ownWaves << " waves sending " << f.ownSent << "; in 10 s " << f.hisFallen << " fell on him (first after " << f.firstFall << " frames), " << f.waitingEnd
                          << " still waiting above him, " << f.ownFallen << " on itself, his largest chain " << f.hisWaves << " waves sending "
                          << f.hisSent << "; round " << (won ? "won" : lost ? "lost" : "drawn") << '\n'
                          << f.fields;
                ++crushes[who], fell[who] += f.hisFallen, answered[who] += f.hisWaves >= 5;
                if (!in[who]) in[who] = true, ++crushRounds[who], crushWon[who] += won;
            }
        }
        for (int who = 0; who < 2; ++who)
            std::cout << "AB"[who] << " crushes: " << crushes[who] << " in " << crushRounds[who] << " rounds (won " << crushWon[who] << " of those rounds); "
                      << (crushes[who] ? double(fell[who]) / crushes[who] : 0.0) << " garbage fell on the opponent in the 10 s after one; he fired a chain of five waves or more in "
                      << answered[who] << " of them\n";
    }
    std::cout << "time: " << match.seconds << " s, " << rounds / match.seconds << " rounds per second, " << double(frames) / match.seconds
              << " frames per second\n";
    std::cout << "checksum " << std::hex << match.checksum << std::dec << '\n';
}

int selfTest(Options opt) {
    opt.pairs = 30, opt.replays = 0;
    MatchResult first, second, parallel;
    opt.threads = 1;
    if (!runMatch(opt, first) || !runMatch(opt, second)) return 1;
    opt.threads = 8;
    if (!runMatch(opt, parallel)) return 1;
    bool same = first.checksum == second.checksum && first.checksum == parallel.checksum;
    for (size_t i = 0; i < first.rounds.size(); ++i)
        same = same && first.rounds[i].summary == second.rounds[i].summary && first.rounds[i].summary == parallel.rounds[i].summary;
    int finished = 0;
    for (const RoundResult& r : first.rounds) finished += r.state != RoundState::Playing;
    std::cout << "self test: " << first.rounds.size() << " rounds (" << finished << " finished), checksums " << std::hex << first.checksum << ' '
              << second.checksum << ' ' << parallel.checksum << std::dec << (same ? ": the same\n" : ": DIFFERENT\n");
    return same && finished > 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
#ifdef JURAIC_DECISION_METRICS
    if (argc > 1 && std::string_view(argv[1]) == "--eval-fixed") return juraic::eval_fixed::run(argc, argv);
#endif
    Options opt;
    RoundRequest request;
    bool test = false, cancelSet = false;
    std::string linesA, linesB, error;
    play::SettingsTable table(opt.settings);
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--pairs") opt.pairs = std::atoi(value().c_str());
        else if (arg == "--threads") opt.threads = std::atoi(value().c_str());
        else if (arg == "--seed") opt.seed = std::strtoull(value().c_str(), nullptr, 10);
        else if (arg == "--a") opt.a = value();
        else if (arg == "--b") opt.b = value();
        else if (arg == "--param" || arg == "--param-a" || arg == "--param-b") {
            // one name=value pair, or a file of such lines
            const std::string text = value();
            std::string lines;
            if (!ai::addParamText(lines, text)) {
                std::cerr << "cannot read the parameter file " << text << '\n';
                return 2;
            }
            if (arg != "--param-b") linesA += lines;
            if (arg != "--param-a") linesB += lines;
        }
        else if (arg == "--max-frames") opt.maxFrames = std::atoll(value().c_str());
        else if (arg == "--self-test") test = true;
        else if (arg == "--same-seeds") opt.sameSeeds = true;
        else if (arg == "--own-tsumo") opt.ownTsumo = true;
        else if (arg == "--primary-routes") opt.primaryRoutes = true;
        else if (arg == "--ex-a") opt.exA = true;
        else if (arg == "--ex-b") opt.exB = true;
        else if (arg == "--kinds") opt.kinds = true;
        else if (arg == "--trace-crush") opt.traceCrush = true;
        else if (arg == "--trace-answers") opt.traceAnswers = true;
        else if (arg == "--record-round") {
            const auto index = value(), seat = value();
            if (request.pair >= 0 || !readNonnegative(index, request.pair) || !readNonnegative(seat, request.aSeat) || request.aSeat > 1) {
                std::cerr << "--record-round requires a pair index and A seat 0 or 1\n";
                return 2;
            }
            request.directory = value();
            request.ledger = value();
            request.attempt = value();
        }
        else if (arg == "--replays") {
            opt.replayDirectory = value();
            opt.replays = std::atoi(value().c_str());
        } else if (arg == "--set") {
            const std::string pair = value();
            if (!play::setByText(table, opt.settings, pair)) {
                std::cerr << "invalid setting: " << pair << '\n';
                return 2;
            }
            if (pair.starts_with("cancel=")) cancelSet = true;
        } else {
            std::cerr << "unknown argument: " << arg << "\nusage: ai_match [--pairs N] [--threads T] [--seed S] [--a NAME] [--b NAME] "
                         "[--set key=value]... [--max-frames F] [--replays DIR N] [--kinds] [--trace-crush] [--trace-answers] [--self-test]\nagents: " << ai::agentNames() << '\n';
            return 2;
        }
    }
    table.apply(opt.settings);
    const bool namedExA = ai::explicitLv38Ex(opt.a), namedExB = ai::explicitLv38Ex(opt.b);
    if (namedExA || namedExB) {
        if (cancelSet && !opt.settings.rules.cancel) {
            std::cerr << "lv38ex requires cancel=1\n";
            return 2;
        }
        opt.settings.rules.cancel = true;
        opt.exA = opt.exA || namedExA;
        opt.exB = opt.exB || namedExB;
    }
    opt.paramsA = ai::namedDefaults(opt.a), opt.paramsB = ai::namedDefaults(opt.b);
    std::istringstream inA(linesA), inB(linesB);
    if (!ai::loadParams(inA, opt.paramsA, error) || !ai::loadParams(inB, opt.paramsB, error)) {
        std::cerr << "parameter " << error << '\n';
        return 2;
    }
    if (request.pair >= 0) {
        if (test || opt.threads != 1 || opt.replays || opt.maxFrames < 1 || opt.maxFrames > 216000) return 2;
        return runRecordedRound(opt, request) ? 0 : 1;
    }
    if (opt.ownTsumo && opt.replays > 0) {
        // (the queue of matches asks for replays of every run: not an error)
        std::cout << "--own-tsumo: no replays are saved (a replay holds one seed)\n";
        opt.replays = 0;
    }
    if (opt.pairs < 1 || opt.threads < 1 || opt.maxFrames < 1 || opt.maxFrames > 60 * 60 * 60) {
        std::cerr << "pairs and threads must be at least 1, max-frames 1 .. 216000\n";
        return 2;
    }
    if (test) return selfTest(opt);
    MatchResult match;
    if (!runMatch(opt, match)) return 1;
    report(opt, match);
    return 0;
}
