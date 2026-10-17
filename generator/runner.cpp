// Configuration, text output and the run of the 6 x 13 generator (run_loop.h).
#include "generator.h"
#include "run_loop.h"
#include <limits>

namespace juraic {

namespace {
constexpr const char* URL_FILE = "19chain_urls.txt";
}

char colorChar(Cell color) { return ".RGBYP"[color]; }

std::string fieldToUrl(const BitField& field) {
    // The board code of the site below: two cells (left * 8 + right) per character,
    // from the 13th row down, three characters per row; leading zeros dropped.
    static constexpr char DIGITS[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-";
    std::string encoded;
    for (int y = H - 1; y >= 0; --y)
        for (int x = 0; x < W; x += 2) {
            const char digit = DIGITS[field.get(x, y) * 8u + field.get(x + 1, y)];
            if (encoded.empty() && digit == '0') continue;
            encoded.push_back(digit);
        }
    return "https://ishikawapuyo.net/simu/pe.html?" + encoded;
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

void printSolution(std::ostream& out, const Solution& solution, uint64_t seed) {
    BitField copy = solution.field;
    const int pieces = solution.field.count();
    out << "seed=" << seed << "\n";
    out << "chains=" << runChain(copy).chains << "\n";
    out << "pieces=" << pieces << ", extra_pieces=" << pieces - 4 * solution.targetChain << "\n";
    out << "field (top row first; columns left to right):\n";
    for (int y = H - 1; y >= 0; --y) {
        for (int x = 0; x < W; ++x) out << colorChar(solution.field.get(x, y)) << ' ';
        out << '\n';
    }
    out << "URL: " << fieldToUrl(solution.field) << "\n";
    out << "trigger cells (x from left, y from bottom, 1-based):";
    forEachCell(solution.trigger, [&](int x, int y) { out << " (" << x + 1 << ',' << y + 1 << ')'; });
    out << "\n";
    // Every way the board can be completed by one last pair that fires the chain.
    out << "last pair options (V vertical, H horizontal):";
    for (int id = 0; id < PAIR_POSITIONS; ++id) {
        if (!(solution.firePairs >> id & 1)) continue;
        out << ' ' << (id < W ? 'V' : 'H');
        forEachCell(pairCells(solution.field, id), [&](int x, int y) { out << '(' << x + 1 << ',' << y + 1 << ')'; });
    }
    out << "\n";
}

uint64_t parseSeed(const std::string& text) {
    if (text.empty() || text.front() == '-')
        throw std::runtime_error("initial_seed must be an unsigned 64-bit integer");
    size_t used = 0;
    const unsigned long long value = std::stoull(text, &used, 10);
    if (used != text.size()) throw std::runtime_error("invalid trailing characters in initial_seed");
    return uint64_t(value);
}

int parsePositiveInt(const std::string& text, const std::string& key) {
    size_t used = 0;
    const long long value = std::stoll(text, &used, 10);
    if (used != text.size() || value < 1 || value > std::numeric_limits<int>::max())
        throw std::runtime_error(key + " must be a positive integer");
    return int(value);
}

GeneratorConfig loadConfig(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open config file: " + pathToUtf8(path));

    GeneratorConfig config;
    std::unordered_set<std::string> seen;
    std::string line;
    for (int lineNumber = 1; std::getline(file, line); ++lineNumber) {
        // Accept a UTF-8 byte order mark.
        if (lineNumber == 1 && line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        const size_t comment = line.find_first_of("#;");
        if (comment != std::string::npos) line.erase(comment);
        line = run_detail::trim(std::move(line));
        if (line.empty()) continue;
        // Section headers are skipped; key names are unique in the whole file.
        if (line.front() == '[' && line.back() == ']') continue;
        const size_t equal = line.find('=');
        if (equal == std::string::npos)
            throw std::runtime_error("expected key=value at config.ini line " + std::to_string(lineNumber));
        std::string key = run_detail::trim(line.substr(0, equal));
        const std::string value = run_detail::trim(line.substr(equal + 1));
        if (key.empty() || value.empty())
            throw std::runtime_error("empty key or value at config.ini line " + std::to_string(lineNumber));
        if (!seen.insert(key).second) throw std::runtime_error("duplicate config key: " + key);
        const auto orZero = [&] { return value == "0" ? 0 : parsePositiveInt(value, key); };
        if (key == "initial_seed") config.initialSeed = parseSeed(value);
        else if (key == "color_count") config.colorCount = parsePositiveInt(value, key);
        else if (key == "beam_width") config.beamWidth = parsePositiveInt(value, key);
        else if (key == "restarts") config.restarts = parsePositiveInt(value, key);
        else if (key == "candidates_per_parent") config.candidatesPerParent = parsePositiveInt(value, key);
        else if (key == "max_extra_pieces") config.maxExtraPieces = orZero();
        else if (key == "min_extra_pieces") config.minExtraPieces = orZero();
        else if (key == "target_chain") config.targetChain = parsePositiveInt(value, key);
        else if (key == "target_success_count") config.targetSuccessCount = parsePositiveInt(value, key);
        else if (key == "threads") config.threads = orZero();
        else if (key == "field_columns") config.fieldColumns = parsePositiveInt(value, key);
        else if (key == "field_rows") config.fieldRows = parsePositiveInt(value, key);
        else throw std::runtime_error("unknown config key: " + key);
    }
    if (file.bad()) throw std::runtime_error("failed reading config file: " + pathToUtf8(path));

    for (const char* key : {"initial_seed", "color_count", "beam_width", "restarts", "candidates_per_parent",
                            "target_success_count"})
        if (!seen.contains(key)) throw std::runtime_error(std::string("missing config key: ") + key);
    if (config.colorCount < 3 || config.colorCount > 5) throw std::runtime_error("color_count must be 3, 4 or 5");
    if (!(config.fieldColumns == W && config.fieldRows == CLEAR_H) &&
        !(config.fieldColumns == Grid10x19::W && config.fieldRows == Grid10x19::CLEAR_H))
        throw std::runtime_error("field_columns and field_rows must be 6 and 12, or 10 and 18");
    if (config.targetChain < MIN_TARGET_CHAIN || config.targetChain > config.maxChain())
        throw std::runtime_error("target_chain must be between 1 and " + std::to_string(config.maxChain()));
    if (config.maxExtraPieces > config.maxPieces() - 4 * config.targetChain)
        throw std::runtime_error("max_extra_pieces exceeds " + std::to_string(config.maxPieces()) + " - 4 * target_chain");
    if (config.minExtraPieces > config.maxExtraPieces)
        throw std::runtime_error("min_extra_pieces must not exceed max_extra_pieces");
    if (config.threads > 256) throw std::runtime_error("threads must be at most 256");
    return config;
}

int effectiveThreads(const GeneratorConfig& config) {
    if (config.threads > 0) return config.threads;
    // Automatic: half of the logical processors, which is the number of cores
    // on a processor with two threads per core.
    return int(std::max(1u, std::thread::hardware_concurrency() / 2));
}

int runGenerator(const std::filesystem::path& configPath, const GeneratorHooks* hooks,
                 const std::filesystem::path& outputDirectory) {
    ConsoleOutputEncoding consoleEncoding;
    try {
        const GeneratorConfig config = loadConfig(configPath);
        if (config.wide()) return runWideGenerator(config, hooks, outputDirectory);
        run_detail::RunParts<Solution> parts;
        parts.historyFile = URL_FILE;
        parts.historyPrefix = "https://ishikawapuyo.net/simu/pe.html?";
        parts.keyName = "URL";
        parts.generate = [&](Rng& rng, const std::atomic_bool* stop, std::atomic_int* depth, const std::atomic_bool* cancel) {
            return generateOne(config, rng, stop, depth, cancel);
        };
        parts.key = [](const Solution& s) { return fieldToUrl(s.field); };
        parts.verify = [](const Solution& s) { return verifySolution(s); };
        parts.print = [](std::ostream& out, const Solution& s, uint64_t seed) { printSolution(out, s, seed); };
        if (hooks && hooks->saved) parts.saved = hooks->saved;
        return run_detail::runLoop(config, hooks, outputDirectory, parts);
    } catch (const std::exception& e) {
        std::cerr << "Configuration or generation error: " << e.what() << "\n";
        return 2;
    }
}

} // namespace juraic
