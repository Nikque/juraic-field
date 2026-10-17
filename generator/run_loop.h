#pragma once
// The run loop of the generator, for both field sizes (runner.cpp, grid_runner.cpp):
// threads, attempts committed in numerical order, de-duplication against the
// history file of the output directory, hooks and the console progress line.
#include "generator.h"
#include "../app/console_output.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_set>

namespace juraic::run_detail {

inline std::string trim(std::string value) {
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    const auto first = std::find_if(value.begin(), value.end(), notSpace);
    if (first == value.end()) return {};
    const auto last = std::find_if(value.rbegin(), value.rend(), notSpace).base();
    return std::string(first, last);
}

// The lines of the history file that start with `prefix`.
inline std::unordered_set<std::string> loadHistory(const std::filesystem::path& path, const std::string& prefix) {
    std::unordered_set<std::string> lines;
    std::ifstream input(path);
    if (!input) {
        if (std::filesystem::exists(path)) throw std::runtime_error("cannot read output file: " + pathToUtf8(path));
        return lines; // First run: there is no output file yet.
    }
    for (std::string line; std::getline(input, line);) {
        line = trim(std::move(line));
        if (line.compare(0, prefix.size(), prefix) == 0) lines.insert(std::move(line));
    }
    if (input.bad()) throw std::runtime_error("failed reading output file: " + pathToUtf8(path));
    return lines;
}

// Appends lines to the history file, flushing after each so that an
// interrupted run keeps what it found. The file is opened on first use.
class HistoryWriter {
    std::filesystem::path path;
    std::ofstream output;

public:
    explicit HistoryWriter(std::filesystem::path p) : path(std::move(p)) {}
    void append(const std::string& line) {
        if (!output.is_open()) {
            // An edited/copied file may not end with a newline. Keep lines separate.
            bool needsNewline = false;
            if (std::filesystem::exists(path)) {
                std::ifstream previous(path, std::ios::binary);
                if (!previous) throw std::runtime_error("cannot read output file: " + pathToUtf8(path));
                previous.seekg(0, std::ios::end);
                if (previous.tellg() > 0) {
                    previous.seekg(-1, std::ios::end);
                    char last = 0;
                    if (!previous.get(last)) throw std::runtime_error("failed reading output file: " + pathToUtf8(path));
                    needsNewline = last != '\n';
                }
            }
            output.open(path, std::ios::binary | std::ios::app);
            if (!output) throw std::runtime_error("cannot open output file: " + pathToUtf8(path));
            if (needsNewline) output << '\n';
        }
        output << line << '\n';
        output.flush();
        if (!output) throw std::runtime_error("failed writing output file: " + pathToUtf8(path));
    }
};

// What differs between the field sizes.
template <class Sol> struct RunParts {
    const char* historyFile;  // in the output directory
    std::string historyPrefix; // lines of the history file that are keys
    const char* keyName;      // what is appended to the history file, for the messages
    std::function<std::optional<Sol>(Rng&, const std::atomic_bool* stop, std::atomic_int* depth, const std::atomic_bool* cancel)> generate;
    std::function<std::string(const Sol&)> key;
    std::function<std::string(const Sol&)> verify;
    std::function<void(std::ostream&, const Sol&, uint64_t seed)> print;
    std::function<void(const Sol&, uint64_t seed, int count)> saved; // may be empty
};

// Exit codes: 0 target reached, 1 attempt limit reached, 3 cancelled; errors are thrown.
template <class Sol>
int runLoop(const GeneratorConfig& config, const GeneratorHooks* hooks, const std::filesystem::path& outputDirectory,
            const RunParts<Sol>& parts) {
    const int threads = effectiveThreads(config);
    std::cout << "seed=" << config.initialSeed << ", colors=" << config.colorCount
              << ", beam_width=" << config.beamWidth << ", restarts=" << config.restarts
              << ", candidates_per_parent=" << config.candidatesPerParent
              << ", target_success_count=" << config.targetSuccessCount
              << ", max_extra_pieces=" << config.maxExtraPieces << ", min_extra_pieces=" << config.minExtraPieces
              << ", target_chain=" << config.targetChain << ", threads=" << threads;
    if (config.wide()) std::cout << ", field=" << config.fieldColumns << 'x' << config.fieldRows;
    std::cout << "\n";

    const auto outputPath = outputDirectory / parts.historyFile;
    std::unordered_set<std::string> seenKeys = loadHistory(outputPath, parts.historyPrefix);
    HistoryWriter writer(outputPath);

    // Attempt k always uses random stream k, and attempts are committed in
    // numerical order, so the results do not depend on the thread count.
    std::mutex mutex;
    std::map<int, std::optional<Sol>> finished;
    std::atomic_int nextAttempt{1}, depth{0};
    std::atomic_bool stop{false};
    std::exception_ptr failure;
    int committed = 0, successCount = 0, duplicateCount = 0;
    bool progressShown = false;
    auto lastReport = std::chrono::steady_clock::time_point{};

    const auto cancelled = [&] { return hooks && hooks->cancel && hooks->cancel->load(); };
    const auto report = [&](int attempt, bool force) { // call with the mutex held
        const auto now = std::chrono::steady_clock::now();
        if (!force && now - lastReport < std::chrono::milliseconds(300)) return;
        lastReport = now;
        GeneratorProgress p{attempt, config.restarts, depth.load(), config.targetChain,
                            successCount, config.targetSuccessCount, "探索中"};
        if (hooks && hooks->progress) {
            hooks->progress(p);
            return;
        }
        std::ostringstream status;
        status << "再試行 " << p.attempt << '/' << p.totalAttempts << " | 段階 " << p.depth << '/'
               << p.targetChain << " | 生成 " << p.saved << '/' << p.target;
        writeProgressLine(status.str());
        progressShown = true;
    };
    const auto commit = [&](int attempt, std::optional<Sol> result) { // mutex held
        finished.emplace(attempt, std::move(result));
        for (auto it = finished.find(committed + 1); it != finished.end() && !stop;
             it = finished.find(committed + 1)) {
            const std::optional<Sol> solution = std::move(it->second);
            finished.erase(it);
            ++committed;
            if (solution) {
                if (progressShown) std::cout << '\n';
                progressShown = false;
                const std::string key = parts.key(*solution);
                if (!seenKeys.insert(key).second) {
                    ++duplicateCount;
                    std::cout << "既存または今回の生成結果と重複したため保存をスキップします。\n";
                } else {
                    const std::string problem = parts.verify(*solution);
                    if (!problem.empty()) throw std::logic_error("generated board failed verification: " + problem);
                    writer.append(key);
                    ++successCount;
                    parts.print(std::cout, *solution, config.initialSeed);
                    std::cout.flush();
                    if (parts.saved) parts.saved(*solution, config.initialSeed, successCount);
                    std::cout << parts.keyName << "を" << parts.historyFile << "に追記しました。\n"
                              << "今回のユニークな生成数: " << successCount << '/' << config.targetSuccessCount
                              << "\n";
                }
            }
            if (successCount == config.targetSuccessCount || committed == config.restarts) stop = true;
        }
    };
    const auto worker = [&] {
        try {
            for (;;) {
                const int attempt = nextAttempt.fetch_add(1);
                if (attempt > config.restarts || stop || cancelled()) break;
                {
                    std::lock_guard lock(mutex);
                    report(attempt, attempt == 1);
                }
                Rng rng(config.initialSeed, uint64_t(attempt));
                std::optional<Sol> result = parts.generate(rng, &stop, &depth, hooks ? hooks->cancel : nullptr);
                if (stop || cancelled()) break; // an interrupted attempt is not a result
                std::lock_guard lock(mutex);
                commit(attempt, std::move(result));
            }
        } catch (...) {
            std::lock_guard lock(mutex);
            if (!failure) failure = std::current_exception();
            stop = true;
        }
    };

    if (threads == 1) {
        worker();
    } else {
        std::vector<std::thread> pool;
        for (int i = 0; i < threads; ++i) pool.emplace_back(worker);
        for (auto& t : pool) t.join();
    }
    if (failure) std::rethrow_exception(failure);
    if (progressShown) std::cout << '\n';

    if (successCount == config.targetSuccessCount) {
        std::cout << "目標数 " << config.targetSuccessCount << " 件のユニークな" << config.targetChain
                  << "連鎖盤面を生成しました。\n"
                  << "試行数: " << committed << ", 重複スキップ数: " << duplicateCount << "\n";
        return 0;
    }
    if (cancelled()) {
        std::cout << "Cancelled; saved results are retained.\n" << std::flush;
        return 3;
    }
    std::cerr << "試行上限に到達しました。ユニークな生成数: " << successCount << '/' << config.targetSuccessCount
              << ", 試行数: " << committed << ", 重複スキップ数: " << duplicateCount << "\n"
              << "config.iniのrestartsを増やすか、initial_seedを変更してください。\n";
    return 1;
}

} // namespace juraic::run_detail
