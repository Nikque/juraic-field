// Speed and success rate of the 10 x 19 search (generator/grid_generator.h),
// one thread, attempt by attempt.
//   grid_bench chain attempts [beam_width] [candidates_per_parent] [colors] [extra] [seed] [variant]
// variant: the guidance of the last steps (see the table below; 0: as Grid10x19).
#include "../generator/grid_generator.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>

using namespace juraic;

template <int GUIDE_STEPS, int SLACK, int STEPS, int DISTANCE, int RAMP = 100, int GREEDY = 0, int FAR = 0> struct Variant : Grid10x19 {
    static constexpr int GUIDE_MIN_PIECES = GUIDE_STEPS ? 182 : 1 << 20, GUIDED_STEPS = GUIDE_STEPS;
    static constexpr int PLAN_MAX_SLACK = SLACK, PLAN_STEPS = STEPS, PLAN_DISTANCE = DISTANCE, PLAN_RAMP = RAMP, PLAN_GREEDY = GREEDY, PLAN_FAR_FIRST_EXTRA = FAR;
};

template <class G> int run(int chain, int attempts, int beam, int perParent, int colors, int extra, uint64_t seed) {
    int found = 0;
    std::map<int, int> failedAt; // depth at which a failed attempt ended
    const auto started = std::chrono::steady_clock::now();
    for (int attempt = 1; attempt <= attempts; ++attempt) {
        Rng rng(seed, uint64_t(attempt));
        std::atomic_int depth{0};
        const auto s = gridGenerateOne<G>(colors, chain, beam, perParent, extra, extra, rng, nullptr, &depth);
        if (!s) {
            ++failedAt[depth.load()];
            continue;
        }
        ++found;
        const std::string problem = gridVerifySolution(*s);
        if (!problem.empty()) {
            std::cerr << "invalid board: " << problem << '\n';
            return 1;
        }
    }
    const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::cout << "chain=" << chain << " beam=" << beam << " candidates=" << perParent << " colors=" << colors
              << " extra=" << extra << " guide=" << G::GUIDED_STEPS << " plan(slack<=" << G::PLAN_MAX_SLACK
              << ", steps=" << G::PLAN_STEPS << ", distance=" << G::PLAN_DISTANCE << ", ramp=" << G::PLAN_RAMP << ", greedy=" << G::PLAN_GREEDY << "): " << found << "/" << attempts
              << " found, " << total << " s, " << total / attempts << " s per attempt\n";
    std::cout << "  failed at depth:";
    for (auto [d, n] : failedAt) std::cout << ' ' << d << ':' << n;
    std::cout << '\n';
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: grid_bench chain attempts [beam_width] [candidates_per_parent] [colors] [extra] [seed] [variant]\n";
        return 2;
    }
    const int chain = std::atoi(argv[1]), attempts = std::atoi(argv[2]);
    const int beam = argc > 3 ? std::atoi(argv[3]) : 48, perParent = argc > 4 ? std::atoi(argv[4]) : 2;
    const int colors = argc > 5 ? std::atoi(argv[5]) : 4, extra = argc > 6 ? std::atoi(argv[6]) : 0;
    const uint64_t seed = argc > 7 ? std::strtoull(argv[7], nullptr, 10) : 20261017;
    const int variant = argc > 8 ? std::atoi(argv[8]) : 0;
    const auto go = [&]<class G>() { return run<G>(chain, attempts, beam, perParent, colors, extra, seed); };
    switch (variant) {
    case 0: return go.template operator()<Grid10x19>();
    case 1: return go.template operator()<Variant<0, -1, 0, 0>>();  // no guidance at all
    case 2: return go.template operator()<Variant<6, -1, 0, 0>>();  // the guidance of generator.cpp
    case 3: return go.template operator()<Variant<0, 8, 6, 2, 100>>();
    case 11: return go.template operator()<Variant<0, 8, 6, 2, 80>>();
    case 12: return go.template operator()<Variant<0, 8, 6, 2, 60>>();
    case 13: return go.template operator()<Variant<0, 8, 6, 2, 40>>();
    case 14: return go.template operator()<Variant<0, 8, 6, 3, 60>>();
    case 15: return go.template operator()<Variant<0, 8, 6, 1, 60>>();
    case 16: return go.template operator()<Variant<0, 8, 10, 2, 60>>();
    case 17: return go.template operator()<Variant<0, 8, 4, 2, 60>>();
    case 18: return go.template operator()<Variant<0, 8, 8, 2, 60>>();
    case 19: return go.template operator()<Variant<0, 8, 12, 2, 60>>();
    case 20: return go.template operator()<Variant<0, 8, 10, 2, 70>>();
    case 21: return go.template operator()<Variant<0, 8, 10, 2, 50>>();
    case 22: return go.template operator()<Variant<0, 8, 14, 2, 60>>();
    case 23: return go.template operator()<Variant<0, 8, 10, 3, 60>>();
    case 30: return go.template operator()<Variant<0, 8, 12, 2, 60, 1>>();
    case 31: return go.template operator()<Variant<0, 8, 12, 2, 100, 1>>();
    case 32: return go.template operator()<Variant<0, 8, 10, 2, 100, 1>>();
    case 33: return go.template operator()<Variant<0, 8, 14, 2, 100, 1>>();
    case 34: return go.template operator()<Variant<0, 8, 12, 3, 100, 1>>();
    case 35: return go.template operator()<Variant<0, 8, 12, 2, 80, 1>>();
    case 40: return go.template operator()<Variant<0, 8, 12, 2, 100, 2>>();
    case 41: return go.template operator()<Variant<0, 8, 12, 2, 60, 2>>();
    case 42: return go.template operator()<Variant<0, 8, 8, 2, 100, 2>>();
    case 43: return go.template operator()<Variant<0, 8, 6, 1, 100, 2>>();
    case 44: return go.template operator()<Variant<0, 8, 12, 3, 100, 2>>();
    case 50: return go.template operator()<Variant<0, 40, 12, 2, 100, 2>>();
    case 51: return go.template operator()<Variant<0, 20, 12, 2, 100, 2>>();
    case 60: return go.template operator()<Variant<0, 20, 16, 2, 100, 2>>();
    case 61: return go.template operator()<Variant<0, 20, 8, 2, 100, 2>>();
    case 62: return go.template operator()<Variant<0, 20, 12, 3, 100, 2>>();
    case 63: return go.template operator()<Variant<0, 20, 16, 3, 100, 2>>();
    case 64: return go.template operator()<Variant<0, 20, 20, 3, 100, 2>>();
    case 65: return go.template operator()<Variant<0, 20, 24, 4, 100, 2>>();
    case 70: return go.template operator()<Variant<0, 20, 12, 2, 100, 2, 1 << 20>>(); // residual at random
    case 71: return go.template operator()<Variant<0, 20, 12, 2, 100, 2, 80>>();
    case 72: return go.template operator()<Variant<0, 20, 12, 2, 100, 2, 100>>();
    case 4: return go.template operator()<Variant<0, 8, 6, 2>>();
    case 5: return go.template operator()<Variant<0, 8, 6, 3>>();
    case 6: return go.template operator()<Variant<0, 8, 4, 2>>();
    case 7: return go.template operator()<Variant<0, 8, 8, 2>>();
    case 8: return go.template operator()<Variant<0, 8, 10, 2>>();
    case 9: return go.template operator()<Variant<0, 8, 12, 3>>();
    case 10: return go.template operator()<Variant<0, 8, 3, 1>>();
    default: std::cerr << "unknown variant\n"; return 2;
    }
}
