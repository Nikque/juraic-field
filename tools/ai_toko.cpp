// Toko by the beam search (ai/build.h): how large a chain it builds, and how fast.
//
//   ai_toko [--games N] [--seed S] [--threads T] [--known K] [--goal P] [--moves M]
//           [--params FILE] [--param name=value]... [--set key=value]... [--list] [--self-test]
//
// Game i uses the tsumo of seed S + i. Before every move the search is given
// the field and the next K pairs (2 = as Ama's own test loop, 3 = as in a
// match); the game ends with the first chain of P points or more (78000), when
// the player loses, or after M moves (100). A game that ends without such a
// chain counts as failed.
//
// The games are played on T threads, one search thread each; the result is the
// same for any T. Time depends on the load of the machine; nodes do not.
// --param changes a value of ai/params.h, --set a rule (names of play/settings.h).
// --list prints one line per game.
//
// Another AI on the same pairs (through a tool of its own, outside this repository):
//   --write-tsumo FILE   writes the tsumo of the N games (four colours), one line each, and stops
//   --replay FILE        plays the placements another AI chose (its output file) on this
//                        engine instead of searching, and reports them in the same form,
//                        with what does not agree: placements this engine does not
//                        allow, and games whose last chain it counts differently
#ifndef _MSC_VER
#define sscanf_s sscanf
#endif
#include "toko_game.h"
#include "../play/settings.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

using namespace juraic;
using toko::CHECKPOINTS;
using toko::GameResult;

namespace {

struct Options {
    int games = 20, threads = 1, known = 2, goal = 78000, moves = 100;
    uint64_t seed = 1;
    play::Settings settings;
    ai::Params params;
    bool list = false;
    std::string writeTsumo, replay;
    toko::Run run() const { return {&settings.rules, &params, known, goal, moves}; }
};

// One line of the other AI's output: <seed> <moves> <points> <ms per move> then <column><direction> per move.
bool replayGame(const Options& opt, const std::string& line, uint64_t& seed, GameResult& g) {
    std::istringstream in(line);
    int count = 0;
    if (!(in >> seed >> count >> g.theirPoints >> g.theirMs)) return false;
    const Rules& rules = opt.settings.rules;
    const Tsumo tsumo = makeTsumo(rules, seed);
    PlayerCore me;
    Incoming nobody;
    Rng garbage(seed, 1);
    for (int& p : g.potential) p = -1;
    int64_t frame = 0;
    std::string move;
    for (int i = 0; i < count && in >> move; ++i) {
        if (move.size() != 2 || move[0] < '0' || move[0] > '5' || move[1] < '0' || move[1] > '3') return false;
        const Placement target{uint8_t(move[0] - '0'), uint8_t(move[1] - '0')};
        if (!(legalPlacements(me.field) >> placementId(target) & 1)) {
            g.illegal = true;
            break;
        }
        if (!toko::playMove(opt.run(), me, nobody, target, tsumo.pair(i), frame, garbage, g)) break;
    }
    if (!g.points) g.frames = frame;
    return true;
}

uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v;
    h *= 0xFF51AFD7ED558CCDull;
    return h ^ h >> 32;
}

uint64_t run(const Options& opt, std::vector<GameResult>& results, double& seconds) {
    results.assign(size_t(opt.games), {});
    std::atomic_int next{0};
    const auto start = std::chrono::steady_clock::now();
    const toko::Run run = opt.run();
    const auto worker = [&] {
        ai::Builder builder;
        for (;;) {
            const int i = next.fetch_add(1);
            if (i >= opt.games) break;
            results[size_t(i)] = toko::playGame(run, opt.seed + uint64_t(i), builder);
        }
    };
    if (opt.threads <= 1) {
        worker();
    } else {
        std::vector<std::thread> pool;
        for (int i = 0; i < opt.threads; ++i) pool.emplace_back(worker);
        for (std::thread& t : pool) t.join();
    }
    seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    uint64_t sum = 0x9E3779B97F4A7C15ull;
    for (const GameResult& g : results) sum = mix(mix(mix(mix(sum, uint64_t(g.moves)), uint64_t(g.largest)), uint64_t(g.frames)), uint64_t(g.nodes));
    return sum;
}

void report(const Options& opt, const std::vector<GameResult>& results, double seconds, uint64_t checksum, bool searched = true) {
    int done = 0, over100 = 0, dead = 0;
    int64_t points = 0, largest = 0, moves = 0, frames = 0, nodes = 0, allMoves = 0, ns = 0, worst = 0;
    std::vector<int> moveList;
    for (size_t i = 0; i < results.size(); ++i) {
        const GameResult& g = results[i];
        if (opt.list)
            std::cout << "seed " << opt.seed + i << ": " << g.moves << " moves, " << (g.points ? "chain of " : "FAILED, largest chain ")
                      << (g.points ? g.points : g.largest) << " points, " << g.frames << " frames" << (g.dead ? ", lost" : "") << '\n';
        largest += g.largest, nodes += g.nodes, allMoves += g.moves, ns += g.ns, worst = std::max(worst, g.worstNs), dead += g.dead;
        if (!g.points) continue;
        ++done, points += g.points, moves += g.moves, frames += g.frames, over100 += g.points >= 100000;
        moveList.push_back(g.moves);
    }
    std::sort(moveList.begin(), moveList.end());
    const int n = int(results.size());
    std::cout << n << " games from seed " << opt.seed << ", " << opt.known << " pairs known, goal " << opt.goal << " points, at most " << opt.moves << " moves";
    if (searched)
        std::cout << "; beam " << opt.params.beamWidth << " x " << opt.params.beamDepth << ", " << opt.params.beamQueues
                  << (opt.params.beamFixedQueues ? " fixed" : " random") << " queues";
    std::cout << '\n';
    std::cout << "reached the goal: " << done << " (" << 100.0 * done / n << " %), failed: " << n - done << " (" << dead << " lost)\n";
    if (done)
        std::cout << "of those: " << double(points) / done << " points on average, " << over100 << " of 100000 or more (" << 100.0 * over100 / n
                  << " % of all games), " << double(moves) / done << " moves on average, median " << moveList[moveList.size() / 2] << ", "
                  << double(frames) / done << " frames before the chain\n";
    std::cout << "largest chain of a game, failed games included: " << double(largest) / n << " points on average\n";
    std::cout << "largest chain a trial drop starts, by moves played (games still going):";
    for (int c = 0; c < CHECKPOINTS; ++c) {
        int64_t sum = 0;
        int count = 0;
        for (const GameResult& g : results)
            if (g.potential[c] >= 0) sum += g.potential[c], ++count;
        if (count) std::cout << "  " << 5 * (c + 1) << ": " << sum / count << " (" << count << ")";
    }
    std::cout << '\n';
    if (!searched) return;
    std::cout << "search: " << double(nodes) / double(allMoves ? allMoves : 1) << " nodes per move; " << double(ns) / 1e6 / double(allMoves ? allMoves : 1)
              << " ms per move, longest " << double(worst) / 1e6 << " ms (" << opt.threads << " games at a time); " << seconds << " s in all\n";
    std::cout << "checksum " << std::hex << checksum << std::dec << '\n';
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    bool test = false;
    play::SettingsTable table(opt.settings);
    std::string lines, error;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--games") opt.games = std::atoi(value().c_str());
        else if (arg == "--threads") opt.threads = std::atoi(value().c_str());
        else if (arg == "--seed") opt.seed = std::strtoull(value().c_str(), nullptr, 10);
        else if (arg == "--known") opt.known = std::atoi(value().c_str());
        else if (arg == "--goal") opt.goal = std::atoi(value().c_str());
        else if (arg == "--moves") opt.moves = std::atoi(value().c_str());
        else if (arg == "--list") opt.list = true;
        else if (arg == "--self-test") test = true;
        else if (arg == "--write-tsumo") opt.writeTsumo = value();
        else if (arg == "--replay") opt.replay = value();
        else if (arg == "--param" || arg == "--params") {
            // one name=value pair, or a file of such lines
            if (!ai::addParamText(lines, value())) {
                std::cerr << "cannot read the parameter file\n";
                return 2;
            }
        } else if (arg == "--set") {
            const std::string pair = value();
            if (!play::setByText(table, opt.settings, pair)) {
                std::cerr << "unknown setting: " << pair << '\n';
                return 2;
            }
        } else {
            std::cerr << "unknown argument: " << arg << "\nusage: ai_toko [--games N] [--seed S] [--threads T] [--known K] [--goal P] [--moves M] "
                         "[--params FILE] [--param name=value]... [--set key=value]... [--list] [--self-test]\n";
            return 2;
        }
    }
    table.apply(opt.settings);
    std::istringstream in(lines);
    if (!ai::loadParams(in, opt.params, error)) {
        std::cerr << "parameter " << error << '\n';
        return 2;
    }
    if (opt.games < 1 || opt.threads < 1 || opt.known < 1 || opt.known > 8 || opt.moves < 1) {
        std::cerr << "games, threads and moves must be at least 1, known 1 .. 8\n";
        return 2;
    }
    std::vector<GameResult> results;
    double seconds;
    if (!opt.writeTsumo.empty()) {
        const Rules& rules = opt.settings.rules;
        std::ofstream out(opt.writeTsumo, std::ios::binary);
        for (int i = 0; i < opt.games; ++i) {
            const Tsumo tsumo = makeTsumo(rules, opt.seed + uint64_t(i));
            out << opt.seed + uint64_t(i) << ' ';
            for (const Cell c : tsumo.piece) {
                int index = 0;
                while (index < MAX_COLORS - 1 && rules.palette[size_t(index)] != c) ++index;
                out << index;
            }
            out << '\n';
        }
        return out ? 0 : 1;
    }
    if (!opt.replay.empty()) {
        std::ifstream replayFile(opt.replay);
        std::string line;
        int illegal = 0, countedOtherwise = 0, bad = 0;
        double ms = 0;
        bool first = true;
        while (std::getline(replayFile, line)) {
            if (line.empty()) continue;
            GameResult g;
            uint64_t seed = 0;
            if (!replayGame(opt, line, seed, g)) {
                ++bad;
                continue;
            }
            if (first) opt.seed = seed, first = false;
            illegal += g.illegal, ms += g.theirMs;
            // The other AI says the game ended with a chain of theirPoints (0 = it did not).
            if (!g.illegal && g.theirPoints != g.points) {
                ++countedOtherwise;
                if (opt.list) std::cout << "seed " << seed << ": the other AI counts " << g.theirPoints << " points, this engine " << g.points << " (largest " << g.largest << ")\n";
            }
            results.push_back(g);
        }
        if (results.empty()) {
            std::cerr << "no game could be read\n";
            return 1;
        }
        opt.games = int(results.size());
        std::cout << "replay of " << opt.replay << ": " << results.size() << " games, " << bad << " lines not understood, " << illegal
                  << " games with a placement this engine does not allow, " << countedOtherwise
                  << " games whose last chain the two count differently; the other AI took " << ms / double(results.size()) << " ms per move\n";
        report(opt, results, 0, 0, false);
        return 0;
    }
    if (test) {
        // A small search, three times: 1 thread, 1 thread, 4 threads.
        opt.games = 8, opt.params.beamWidth = 30, opt.params.beamDepth = 8, opt.moves = 40;
        opt.threads = 1;
        const uint64_t a = run(opt, results, seconds), b = run(opt, results, seconds);
        opt.threads = 4;
        const uint64_t c = run(opt, results, seconds);
        std::cout << "self test: checksums " << std::hex << a << ' ' << b << ' ' << c << std::dec << (a == b && a == c ? ": the same\n" : ": DIFFERENT\n");
        return a == b && a == c ? 0 : 1;
    }
    const uint64_t checksum = run(opt, results, seconds);
    report(opt, results, seconds, checksum);
    return 0;
}
