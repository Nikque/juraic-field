// Matches between two AIs without a screen.
//
//   ai_match [--pairs N] [--threads T] [--seed S] [--a NAME] [--b NAME]
//            [--param name=value]... [--param-a name=value]... [--param-b name=value]...
//            [--set key=value]... [--max-frames F] [--replays DIR N] [--same-seeds] [--self-test]
//
// --param changes a value of ai/params.h for both sides, --param-a and
// --param-b for one side (a name=value pair, or a file of such lines).
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
#ifndef _MSC_VER
#define sscanf_s sscanf
#endif
#include "../ai/agents.h"
#include "../play/replay.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
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
};

struct RoundResult {
    RoundState state = RoundState::Playing; // Playing = stopped at maxFrames
    int64_t frames = 0;
    int score[2]{}, moves[2]{}, maxChain[2]{}, sent[2]{};
    int chains[2]{}, smallChains[2]{}; // chains fired, and those of them with three waves or fewer
    int fallen[2]{};                   // garbage that fell on the player's field
    std::string summary; // play::roundSummary
};

// seed1: null = one tsumo for both; else player 1 draws from the tsumo of that seed (--own-tsumo).
RoundResult playRound(const Rules& rules, uint64_t seed, ai::Agent* agent[2], const uint64_t agentSeed[2], int64_t maxFrames,
                      play::RoundLog* log, const uint64_t* seed1 = nullptr) {
    Round r;
    r.start(rules, seed);
    if (seed1) r.splitTsumo(*seed1);
    for (int i = 0; i < 2; ++i) agent[i]->startRound(i, agentSeed[i]);
    if (log) *log = play::RoundLog{}, log->seed = seed;
    RoundResult result;
    ai::Observation o;
    PlayerEvents e[2];
    int waves[2] = {0, 0}; // the waves so far of the chain a player fired last
    while (r.state == RoundState::Playing && r.frame < maxFrames) {
        ai::observe(r, 0, o);
        uint8_t buttons[2];
        buttons[0] = agent[0]->frame(o);
        o.me = 1;
        buttons[1] = agent[1]->frame(o);
        if (log) log->frames.push_back(uint16_t(buttons[0] | buttons[1] << 8));
        r.step(buttons, e);
        for (int i = 0; i < 2; ++i)
            if (e[i].waveStarted) {
                result.maxChain[i] = std::max(result.maxChain[i], r.player[i].chain.chains);
                result.sent[i] += r.player[i].wave.sent;
                if (r.player[i].chain.chains == 1) ++result.chains[i], result.smallChains[i] += waves[i] >= 1 && waves[i] <= 3;
                waves[i] = r.player[i].chain.chains;
            }
        for (int i = 0; i < 2; ++i)
            if (e[i].garbageFell)
                for (int x = 0; x < W; ++x) result.fallen[i] += r.player[i].dropped[x];
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
};

uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v;
    h *= 0xFF51AFD7ED558CCDull;
    return h ^ h >> 32;
}

bool runMatch(const Options& opt, MatchResult& match) {
    if (!ai::makeAgent(opt.a) || !ai::makeAgent(opt.b)) {
        std::cerr << "unknown agent (known: " << ai::agentNames() << ")\n";
        return false;
    }
    match.rounds.assign(size_t(opt.pairs) * 2, {});
    std::atomic_int next{0};
    std::atomic_bool failed{false};
    const auto start = std::chrono::steady_clock::now();
    const auto worker = [&] {
        const std::unique_ptr<ai::Agent> a = ai::makeAgent(opt.a, opt.paramsA), b = ai::makeAgent(opt.b, opt.paramsB);
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
                result = playRound(opt.settings.rules, seed, agent, agentSeed, opt.maxFrames, save ? &log : nullptr, opt.ownTsumo ? &seed1 : nullptr);
                if (save) replay.rounds.push_back(std::move(log)), expected += result.summary + '\n';
            }
            if (save) {
                const std::filesystem::path file =
                    opt.replayDirectory / ("match-" + std::to_string(opt.seed) + "-" + std::to_string(k) + ".jfreplay");
                if (!play::saveReplay(file, replay)) failed = true;
                std::ofstream(file.string() + ".expected", std::ios::binary) << expected;
            }
        }
    };
    if (opt.threads <= 1) {
        worker();
    } else {
        std::vector<std::thread> pool;
        for (int i = 0; i < opt.threads; ++i) pool.emplace_back(worker);
        for (std::thread& t : pool) t.join();
    }
    match.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    match.checksum = 0x9E3779B97F4A7C15ull;
    for (const RoundResult& r : match.rounds)
        for (const char c : r.summary) match.checksum = mix(match.checksum, uint8_t(c));
    if (failed) std::cerr << "a replay could not be saved\n";
    return !failed;
}

void report(const Options& opt, const MatchResult& match) {
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
    Options opt;
    bool test = false;
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
        else if (arg == "--replays") {
            opt.replayDirectory = value();
            opt.replays = std::atoi(value().c_str());
        } else if (arg == "--set") {
            const std::string pair = value();
            if (!play::setByText(table, opt.settings, pair)) {
                std::cerr << "unknown setting: " << pair << '\n';
                return 2;
            }
        } else {
            std::cerr << "unknown argument: " << arg << "\nusage: ai_match [--pairs N] [--threads T] [--seed S] [--a NAME] [--b NAME] "
                         "[--set key=value]... [--max-frames F] [--replays DIR N] [--self-test]\nagents: " << ai::agentNames() << '\n';
            return 2;
        }
    }
    table.apply(opt.settings);
    std::istringstream inA(linesA), inB(linesB);
    if (!ai::loadParams(inA, opt.paramsA, error) || !ai::loadParams(inB, opt.paramsB, error)) {
        std::cerr << "parameter " << error << '\n';
        return 2;
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
