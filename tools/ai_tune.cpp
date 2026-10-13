// Tunes weights of ai/params.h by toko (SPSA).
//
//   ai_tune [--iterations N] [--games G] [--threads T] [--seed S] [--known K]
//           [--params FILE] [--param name=value]... [--tune name,name,...]
//           [--step A] [--perturb C] [--out FILE] [--self-test]
//
// Every iteration moves all tuned weights a little, each up or down at random,
// and plays G games with the weights moved that way and G games with them
// moved the opposite way, on the same tsumo. The weights then go a step
// towards the side that did better, in proportion to the difference of the
// average objective (tools/toko_game.h: what could be fired after 5, 10, ...
// 50 moves; large and early is good, failing is bad).
//
//   --perturb C  how far the weights are moved for the two sides, as a part of
//                their size at the start (0.1 = 10 %); shrinks slowly
//   --step A     the size of a step for a difference of 1000 points, as a part
//                of the weight's size at the start; shrinks over the run
//   --tune       the weights to tune (default: the ev... weights but the profile)
//   --param      fixes a value, e.g. a smaller beam so that a game is short
//   --out        the weights of the end, as a file for --params
//
// Game j of iteration k uses the tsumo of seed S + (k - 1) * G + j. S is
// 1000000 unless given: keep these seeds apart from the ones used to measure.
// The result is the same for any number of threads. (The tuner itself uses
// floating point; the AI does not.)
#ifndef _MSC_VER
#define sscanf_s sscanf
#endif
#include "toko_game.h"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

using namespace juraic;

namespace {

constexpr uint64_t TUNE_STREAM = 0x74756E65; // which way each weight is moved

struct Options {
    int iterations = 50, games = 32, threads = 1, known = 2;
    uint64_t seed = 1000000;
    double step = 0.01, perturb = 0.1;
    Rules rules;
    ai::Params params;
    std::vector<const ai::ParamItem*> tuned;
    std::string out;
};

// The average objective of the first `games` tsumo of an iteration for the two sides.
void play(const Options& opt, const ai::Params side[2], uint64_t firstSeed, double average[2]) {
    std::vector<int> value(size_t(opt.games) * 2);
    std::atomic_int next{0};
    const auto worker = [&] {
        ai::Builder builder;
        for (;;) {
            const int j = next.fetch_add(1);
            if (j >= opt.games * 2) break;
            const toko::Run run{&opt.rules, &side[j & 1], opt.known, 78000, 100};
            value[size_t(j)] = toko::objective(toko::playGame(run, firstSeed + uint64_t(j / 2), builder));
        }
    };
    if (opt.threads <= 1) {
        worker();
    } else {
        std::vector<std::thread> pool;
        for (int i = 0; i < opt.threads; ++i) pool.emplace_back(worker);
        for (std::thread& t : pool) t.join();
    }
    int64_t sum[2] = {0, 0};
    for (int j = 0; j < opt.games * 2; ++j) sum[j & 1] += value[size_t(j)];
    for (int s = 0; s < 2; ++s) average[s] = double(sum[s]) / opt.games;
}

std::string tune(const Options& opt, bool quiet) {
    const size_t n = opt.tuned.size();
    std::vector<double> theta(n), scale(n);
    for (size_t i = 0; i < n; ++i) {
        theta[i] = opt.params.*opt.tuned[i]->member;
        scale[i] = std::max(std::fabs(theta[i]), 20.0);
    }
    const double stability = opt.iterations / 10.0;
    const auto rounded = [&](const std::vector<double>& t, double sign, const std::vector<int>& delta, double c) {
        ai::Params p = opt.params;
        for (size_t i = 0; i < n; ++i) p.*opt.tuned[i]->member = int(std::lround(t[i] + sign * c * scale[i] * delta[i]));
        return p;
    };
    const std::vector<int> none(n, 0);
    for (int k = 1; k <= opt.iterations; ++k) {
        const double c = opt.perturb / std::pow(double(k), 0.101);
        const double a = opt.step * std::pow((1.0 + stability) / (k + stability), 0.602);
        Rng rng(opt.seed, TUNE_STREAM + uint64_t(k));
        std::vector<int> delta(n);
        for (int& d : delta) d = rng.below(2) ? 1 : -1;
        const ai::Params side[2] = {rounded(theta, 1, delta, c), rounded(theta, -1, delta, c)};
        double average[2];
        play(opt, side, opt.seed + uint64_t(k - 1) * uint64_t(opt.games), average);
        const double difference = (average[0] - average[1]) / 1000.0;
        for (size_t i = 0; i < n; ++i) theta[i] += a * scale[i] * delta[i] * difference;
        if (!quiet) {
            std::cout << "iteration " << k << ": " << average[0] << " against " << average[1] << " ->";
            const ai::Params now = rounded(theta, 0, none, 0);
            for (size_t i = 0; i < n; ++i) std::cout << ' ' << opt.tuned[i]->name << '=' << now.*opt.tuned[i]->member;
            std::cout << std::endl;
        }
    }
    std::ostringstream text;
    ai::saveParams(text, rounded(theta, 0, none, 0));
    return text.str();
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    bool test = false;
    std::string lines, error, names = "evChain,evKey,evFireHeight,evLink,evCentre,evValley,evHill,evWaste";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--iterations") opt.iterations = std::atoi(value().c_str());
        else if (arg == "--games") opt.games = std::atoi(value().c_str());
        else if (arg == "--threads") opt.threads = std::atoi(value().c_str());
        else if (arg == "--seed") opt.seed = std::strtoull(value().c_str(), nullptr, 10);
        else if (arg == "--known") opt.known = std::atoi(value().c_str());
        else if (arg == "--step") opt.step = std::atof(value().c_str());
        else if (arg == "--perturb") opt.perturb = std::atof(value().c_str());
        else if (arg == "--tune") names = value();
        else if (arg == "--out") opt.out = value();
        else if (arg == "--self-test") test = true;
        else if (arg == "--param" || arg == "--params") {
            // one name=value pair, or a file of such lines
            if (!ai::addParamText(lines, value())) {
                std::cerr << "cannot read the parameter file\n";
                return 2;
            }
        } else {
            std::cerr << "unknown argument: " << arg << "\nusage: ai_tune [--iterations N] [--games G] [--threads T] [--seed S] [--known K] "
                         "[--params FILE] [--param name=value]... [--tune name,name,...] [--step A] [--perturb C] [--out FILE] [--self-test]\n";
            return 2;
        }
    }
    std::istringstream in(lines);
    if (!ai::loadParams(in, opt.params, error)) {
        std::cerr << "parameter " << error << '\n';
        return 2;
    }
    std::istringstream list(names);
    for (std::string name; std::getline(list, name, ',');) {
        const ai::ParamItem* found = nullptr;
        for (const ai::ParamItem& item : ai::PARAM_ITEMS)
            if (name == item.name) found = &item;
        if (!found) {
            std::cerr << "unknown parameter to tune: " << name << '\n';
            return 2;
        }
        opt.tuned.push_back(found);
    }
    if (opt.iterations < 1 || opt.games < 1 || opt.threads < 1 || opt.known < 1 || opt.known > 8 || opt.tuned.empty()) {
        std::cerr << "iterations, games and threads must be at least 1, known 1 .. 8\n";
        return 2;
    }
    if (test) {
        // A small run twice: 1 thread and 4 threads.
        opt.iterations = 3, opt.games = 4, opt.params.beamWidth = 20, opt.params.beamDepth = 6;
        opt.threads = 1;
        const std::string a = tune(opt, true);
        opt.threads = 4;
        const std::string b = tune(opt, true);
        ai::Params start = opt.params;
        std::ostringstream first;
        ai::saveParams(first, start);
        std::cout << "self test: the weights after 3 iterations are " << (a == b ? "the same" : "DIFFERENT") << " on 1 and 4 threads, and "
                  << (a != first.str() ? "have moved" : "HAVE NOT MOVED") << '\n';
        return a == b && a != first.str() ? 0 : 1;
    }
    const std::string result = tune(opt, false);
    std::cout << result;
    if (!opt.out.empty() && !(std::ofstream(opt.out, std::ios::binary) << result)) {
        std::cerr << "cannot write " << opt.out << '\n';
        return 1;
    }
    return 0;
}
