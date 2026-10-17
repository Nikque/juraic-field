// Text output and the run of the 10 x 18 generator (grid_generator.h, run_loop.h).
#include "generator.h"
#include "run_loop.h"

namespace juraic {

namespace {
constexpr const char* BOARD_FILE = "boards_10x18.txt";
constexpr const char* CODE_PREFIX = "10x18:";
using G10 = Grid10x19;
} // namespace

std::string wideBoardCode(const GridField<G10>& field) {
    std::string code = CODE_PREFIX;
    for (int y = G10::H - 1; y >= 0; --y)
        for (int x = 0; x < G10::W; ++x) code.push_back(colorChar(field.get(x, y)));
    return code;
}

void printWideSolution(std::ostream& out, const WideSolution& solution, uint64_t seed) {
    GridField<G10> copy = solution.field;
    int chains = 0;
    for (GridFieldWave<G10> w; stepWave(copy, w);) ++chains;
    const int pieces = solution.field.count();
    out << "seed=" << seed << "\n";
    out << "chains=" << chains << "\n";
    out << "pieces=" << pieces << ", extra_pieces=" << pieces - 4 * solution.targetChain << "\n";
    out << "field=" << G10::W << 'x' << G10::CLEAR_H << "\n";
    out << "field (top row first; columns left to right):\n";
    for (int y = G10::H - 1; y >= 0; --y) {
        for (int x = 0; x < G10::W; ++x) out << colorChar(solution.field.get(x, y)) << ' ';
        out << '\n';
    }
    out << "board code: " << wideBoardCode(solution.field) << "\n";
    out << "trigger cells (x from left, y from bottom, 1-based):";
    forEachCell(solution.trigger, [&](int x, int y) { out << " (" << x + 1 << ',' << y + 1 << ')'; });
    out << "\n";
    out << "last pair options (V vertical, H horizontal):";
    for (int id = 0; id < gridPairPositions<G10>(); ++id) {
        if (!(solution.firePairs >> id & 1)) continue;
        out << ' ' << (id < G10::W ? 'V' : 'H');
        forEachCell(gridPairCells(solution.field, id), [&](int x, int y) { out << '(' << x + 1 << ',' << y + 1 << ')'; });
    }
    out << "\n";
}

// Called by runGenerator, which reports what this throws.
int runWideGenerator(const GeneratorConfig& config, const GeneratorHooks* hooks,
                     const std::filesystem::path& outputDirectory) {
    run_detail::RunParts<WideSolution> parts;
    parts.historyFile = BOARD_FILE;
    parts.historyPrefix = CODE_PREFIX;
    parts.keyName = "盤面コード";
    parts.generate = [&](Rng& rng, const std::atomic_bool* stop, std::atomic_int* depth, const std::atomic_bool* cancel) {
        return gridGenerateOne<G10>(config.colorCount, config.targetChain, config.beamWidth, config.candidatesPerParent,
                                    config.minExtraPieces, config.maxExtraPieces, rng, stop, depth, cancel);
    };
    parts.key = [](const WideSolution& s) { return wideBoardCode(s.field); };
    parts.verify = [](const WideSolution& s) { return gridVerifySolution(s); };
    parts.print = [](std::ostream& out, const WideSolution& s, uint64_t seed) { printWideSolution(out, s, seed); };
    if (hooks && hooks->savedWide) parts.saved = hooks->savedWide;
    return run_detail::runLoop(config, hooks, outputDirectory, parts);
}

} // namespace juraic
