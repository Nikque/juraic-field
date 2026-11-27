#pragma once

#include "decision_metrics.h"
#include "decision_position.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace juraic::ai::decision_trace {

struct Files {
    std::filesystem::path profile = env("JURAIC_PROFILE_FILE");
    std::filesystem::path positions = env("JURAIC_POSITION_FILE");
    std::mutex lock;
    std::ofstream csv, binary;

    static std::filesystem::path env(const char* name) {
        const char* value = std::getenv(name);
        return value ? std::filesystem::path(value) : std::filesystem::path{};
    }
};

inline Files& files() {
    static Files output;
    return output;
}

struct Summary {
    uint64_t roundSeed;
    int seat;
    int64_t frame;
    int move;
    bool second;
    int placement, kind, phase;
    unsigned legal;
    bool quiet;
    int colours;
    int64_t ns, shortPlans, cutSearches;
    bool twoWaveIncomplete;
};

inline bool empty(const std::filesystem::path& path) {
    return !std::filesystem::exists(path) || std::filesystem::file_size(path) == 0;
}

inline void append(const Summary& d, const decision_position::Record* position) {
    auto& output = files();
    std::ostringstream line;
    if (!output.profile.empty()) {
        line << d.roundSeed << ',' << d.seat << ',' << d.frame << ',' << d.move << ',' << d.second << ','
             << d.placement << ',' << d.kind << ',' << d.phase << ',' << d.legal << ',' << d.quiet << ','
             << d.colours << ',' << d.ns << ',' << d.shortPlans << ',' << d.cutSearches << ',' << d.twoWaveIncomplete;
        const auto* active = metrics::current();
        for (size_t i = 0; i < std::size(metrics::names); ++i)
            line << ',' << active->ns[i] << ',' << active->exclusiveNs[i] << ',' << active->calls[i] << ',' << active->cuts[i];
        line << '\n';
    }
    std::scoped_lock guard(output.lock);
    if (!output.profile.empty()) {
        if (!output.csv.is_open()) {
            const bool header = empty(output.profile);
            output.csv.open(output.profile, std::ios::app);
            if (header) {
                output.csv << "round_seed,seat,frame,move,second,placement,kind,phase,legal,quiet,colours,ns,short_plans,cut_searches,two_wave_incomplete";
                for (const auto* part : metrics::names)
                    output.csv << ',' << part << "_ns," << part << "_exclusive_ns," << part << "_calls," << part << "_cuts";
                output.csv << '\n';
            }
        }
        if (!(output.csv << line.str())) throw std::runtime_error("Could not write the decision metrics CSV");
    }
    if (position) {
        if (!output.binary.is_open()) {
            const bool header = empty(output.positions);
            output.binary.open(output.positions, std::ios::binary | std::ios::app);
            if (header && !decision_position::writeHeader(output.binary))
                throw std::runtime_error("Could not write the decision positions header");
        }
        if (!decision_position::write(output.binary, *position))
            throw std::runtime_error("Could not write a decision position");
    }
}

} // namespace juraic::ai::decision_trace
