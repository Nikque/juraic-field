#pragma once
// Random generator of boards that fire an exact chain (1..19 waves).
//
// The search works backwards from the board left after the chain: every step
// inserts one group of four below/inside the existing columns so that this
// group, and nothing else, clears first. See generator.cpp for the argument
// why such a board needs no forward simulation.
#include "../core/bit_field.h"
#include "../core/reach.h"
#include "../core/rng.h"
#include "grid_generator.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace juraic {

constexpr int MIN_TARGET_CHAIN = 1;
constexpr int MAX_TARGET_CHAIN = 19;

struct Solution {
    BitField field;        // the board right after the last pair has been placed
    FieldBits trigger;     // the four pieces of the first wave
    unsigned firePairs = 0; // pair positions (reach.h ids) that can be the last pair
    int targetChain = MAX_TARGET_CHAIN;
};

// The two cells of pair position `id` on this board (its topmost pieces).
FieldBits pairCells(const BitField& field, int id);
// Pair positions that can be the last placement: removing the pair leaves a
// board on which nothing clears, the pair can appear, and the position is
// reachable. Uses the shortcuts that hold for boards made by this generator.
unsigned firePairs(const BitField& field, FieldBits trigger);
// Checks every condition of a valid result directly, by simulation, without
// relying on how the board was made. Returns an empty string when valid.
std::string verifySolution(const Solution& s);

struct GeneratorConfig {
    uint64_t initialSeed = 0;
    int colorCount = 0;
    int beamWidth = 0;
    int restarts = 0;
    int candidatesPerParent = 0;
    int targetSuccessCount = 0;
    int maxExtraPieces = 0;
    int minExtraPieces = 0;
    int targetChain = MAX_TARGET_CHAIN;
    int threads = 0; // 0 = automatic
    // The field: 6 columns and 12 rows (generator.cpp) or 10 columns and 18 rows
    // (Grid10x19, grid_generator.h); both with one hidden row above.
    int fieldColumns = 6, fieldRows = 12;
    bool wide() const { return fieldColumns == Grid10x19::W; }
    int maxChain() const { return wide() ? juraic::maxChain<Grid10x19>() : MAX_TARGET_CHAIN; }
    int maxPieces() const { return wide() ? juraic::maxPieces<Grid10x19>() : W * H; }
};
using WideSolution = GridSolution<Grid10x19>;
GeneratorConfig loadConfig(const std::filesystem::path& path);
int effectiveThreads(const GeneratorConfig& config);

// One independent search. `stop` and `cancel` are polled; a stopped search returns nullopt.
std::optional<Solution> generateOne(const GeneratorConfig& config, Rng& rng,
                                    const std::atomic_bool* stop = nullptr,
                                    std::atomic_int* depthOut = nullptr,
                                    const std::atomic_bool* cancel = nullptr);

// Every board from which `post` results after one wave that clears exactly the
// inserted group of four, with that group. `trigger` is the group that clears
// first on `post` (empty if nothing clears there). Exposed for the tests.
struct Predecessor {
    BitField field;
    FieldBits trigger;
};
std::vector<Predecessor> predecessors(const BitField& post, FieldBits trigger, int colorCount);

struct GeneratorProgress {
    int attempt = 0, totalAttempts = 0, depth = 0, targetChain = 0;
    int saved = 0, target = 0;
    std::string phase;
};
struct GeneratorHooks {
    const std::atomic_bool* cancel = nullptr;
    std::function<void(const GeneratorProgress&)> progress;
    std::function<void(const Solution&, uint64_t seed, int count)> saved;
    std::function<void(const WideSolution&, uint64_t seed, int count)> savedWide; // 10 x 18 fields
};
// Exit codes: 0 target reached, 1 attempt limit reached, 2 error, 3 cancelled.
int runGenerator(const std::filesystem::path& configPath, const GeneratorHooks* hooks = nullptr,
                 const std::filesystem::path& outputDirectory = ".");

// ---- text output -----------------------------------------------------------
char colorChar(Cell color);
std::string fieldToUrl(const BitField& field); // the address of the board at the simulator site (runner.cpp)
std::string pathToUtf8(const std::filesystem::path& path);
uint64_t parseSeed(const std::string& text);
int parsePositiveInt(const std::string& text, const std::string& key);
void printSolution(std::ostream& out, const Solution& solution, uint64_t seed);

// ---- 10 x 18 fields (grid_runner.cpp) ------------------------------------------
// No simulator site shows a field of this size, so a board is saved as this
// code instead of a URL: "10x18:" and the 190 cells from the hidden row down, each
// row left to right, as . R G B Y P.
std::string wideBoardCode(const GridField<Grid10x19>& field);
void printWideSolution(std::ostream& out, const WideSolution& solution, uint64_t seed);
int runWideGenerator(const GeneratorConfig& config, const GeneratorHooks* hooks,
                     const std::filesystem::path& outputDirectory);

} // namespace juraic
