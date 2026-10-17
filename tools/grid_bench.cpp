// Speed and success rate of the 10 x 19 search (generator/grid_generator.h),
// one thread, attempt by attempt.
//   grid_bench chain attempts [beam_width] [candidates_per_parent] [colors] [extra] [seed] [variant] [dump]
// variant: the values of the plan (the table in main; 0: as the generator uses them).
// dump: print the boards where the first failed attempts ended.
#include "../generator/generator.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>

using namespace juraic;

template <int GUIDE_STEPS, int SLACK, int STEPS, int DISTANCE, int RAMP = 100, int GREEDY = 0, int FAR = 0, int LIVE = 0, int BUMP = 0, int END_STEPS = 0, int END_BEAM = 1> struct Variant : Grid10x19 {
    using ThreeColors = Variant; // (the values given here, for every colour count)
    static constexpr int GUIDE_MIN_PIECES = GUIDE_STEPS ? 182 : 1 << 20, GUIDED_STEPS = GUIDE_STEPS;
    static constexpr int PLAN_MAX_SLACK = SLACK, PLAN_STEPS = STEPS, PLAN_DISTANCE = DISTANCE, PLAN_RAMP = RAMP, PLAN_GREEDY = GREEDY, PLAN_FAR_FIRST_EXTRA = FAR, PLAN_LIVE = LIVE, PLAN_BUMP = BUMP, PLAN_END_STEPS = END_STEPS, PLAN_END_BEAM = END_BEAM;
};

bool dumpDeadEnds = false; // (the ninth argument "dump": the boards where the first failed attempts ended)

// The room left in each column and the top rows of the board (19th row first; * marks the trigger).
template <class G> void dumpBoard(const grid_detail::Node<G>& n) {
    int h[G::W];
    n.field.heights(h);
    std::cout << "    room:";
    for (int x = 0; x < G::W; ++x) std::cout << ' ' << G::H - h[x];
    std::cout << '\n';
    for (int y = G::H - 1; y >= G::H - 7; --y) {
        std::cout << "    ";
        for (int x = 0; x < G::W; ++x) std::cout << colorChar(n.field.get(x, y)) << (n.trigger.get(x, y) ? '*' : ' ');
        std::cout << '\n';
    }
}

template <class G> int run(int chain, int attempts, int beam, int perParent, int colors, int extra, uint64_t seed) {
    int dumped = 0;
    int found = 0;
    std::map<int, int> failedAt; // depth at which a failed attempt ended
    const auto started = std::chrono::steady_clock::now();
    for (int attempt = 1; attempt <= attempts; ++attempt) {
        Rng rng(seed, uint64_t(attempt));
        std::atomic_int depth{0};
        std::vector<grid_detail::Node<G>> dead;
        const auto s = gridGenerateOne<G>(colors, chain, beam, perParent, extra, extra, rng, nullptr, &depth, nullptr, &dead);
        if (!s) {
            ++failedAt[depth.load()];
            if (dumpDeadEnds && dumped < 3 && !dead.empty()) {
                ++dumped;
                std::cout << "attempt " << attempt << " ended at depth " << depth.load() << " with " << dead.size() << " boards:\n";
                for (size_t i = 0; i < dead.size() && i < 3; ++i) dumpBoard(dead[i]);
            }
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
    dumpDeadEnds = argc > 9 && std::string(argv[9]) == "dump";
    const auto go = [&]<class G>() { return run<G>(chain, attempts, beam, perParent, colors, extra, seed); };
    switch (variant) {
    case 0: return go.template operator()<Grid10x19>(); // as shipped (Grid10x19ThreeColors for three colours)
    case 1: return go.template operator()<Variant<0, -1, 0, 0>>();  // no plan
    case 2: return go.template operator()<Variant<6, -1, 0, 0>>();  // the guidance of generator.cpp
    case 3: return go.template operator()<Variant<0, 20, 12, 2, 100, 0, 78>>();  // PLAN_GREEDY 0
    case 4: return go.template operator()<Variant<0, 20, 12, 2, 100, 1, 78>>();  // PLAN_GREEDY 1
    case 5: return go.template operator()<Variant<0, 20, 12, 2, 100, 2, 78>>();  // Grid10x19
    case 6: return go.template operator()<Variant<0, 20, 12, 2, 100, 2, 78, 1>>();  // + PLAN_LIVE
    case 7: return go.template operator()<Variant<0, 20, 12, 4, 100, 2, 78, 1, 20>>();  // + distance 4, PLAN_BUMP 20
    case 8: return go.template operator()<Variant<0, 20, 12, 4, 100, 2, 78, 1, 20, 6, 4>>();  // + the end beam
    case 9: return go.template operator()<Variant<0, 20, 12, 4, 100, 2, 78, 1, 100, 6, 4>>();  // PLAN_BUMP 100
    case 10: return go.template operator()<Grid10x19ThreeColors>();  // the plan of three colours, for every colour count
    default: std::cerr << "unknown variant\n"; return 2;
    }
}
