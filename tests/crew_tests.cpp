// The helpers of a search (ai/build.h Crew) when one of their threads cannot be started: the ones started are
// closed and joined and the error is passed on (before, the program ended in std::terminate).
#include "../ai/build.h"
#include "../generator/thread_pool.h"
#include <atomic>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <system_error>

static bool testWorkerFailure() {
    std::atomic_int workerIndex{0}, stopped{0}, aborts{0};
    const auto unavailable = std::make_error_code(std::errc::resource_unavailable_try_again);
    auto work = [&] {
        if (workerIndex++ == 0) {
            int started = 0;
            juraic::ai::Crew crew(2, [&](auto&& fn) {
                if (started++ == 1) throw std::system_error(unavailable);
                return std::thread(std::forward<decltype(fn)>(fn));
            });
        }
        ++stopped;
    };
    try {
        juraic::runWorkers(2, work, [&] { ++aborts; });
    } catch (const std::system_error& error) {
        return error.code() == unavailable && workerIndex == 2 && stopped == 1 && aborts == 1;
    }
    return false;
}

// Workers that take numbers from one counter in a loop, as runMatch (tools/ai_match.cpp) does: when one fails, abort()
// ends the numbers - none below the limit is taken after it - and the error is passed on once the others have ended.
static bool testWorkerNumbersStop() {
    constexpr int limit = 20000000;
    std::atomic_int next{0}, aborts{0}, done{0}, after{0};
    std::atomic_bool stopped{false};
    auto work = [&] {
        for (;;) {
            const bool late = stopped.load();
            const int k = next.fetch_add(1);
            if (k >= limit) break;
            if (late) ++after;
            if (k == 0) throw std::runtime_error("worker failed");
            ++done;
        }
    };
    try {
        juraic::runWorkers(2, work, [&] {
            ++aborts;
            next = limit;
            stopped = true;
        });
    } catch (const std::runtime_error&) {
        return aborts == 1 && after == 0 && done < limit - 1;
    }
    return false;
}

static bool testMinimumDepth() {
    using namespace juraic;
    ai::Builder builder;
    ai::Params p;
    p.beamQueues = 2;
    p.beamLastValues = 0;
    const Pair pair{1, 2};
    const Rules rules;
    const GameField field;
    for (int share : {0, 1}) {
        p.beamShare = share;
        p.beamDepth = 1;
        const auto one = builder.search(field, &pair, 1, rules, DEFAULT_TIMING, p, 7);
        if (one.best < 0 || !one.alive || one.nodes <= 0) return false;
        for (int requested : {0, -1}) {
            std::istringstream text("beamDepth=" + std::to_string(requested));
            std::string error;
            if (!ai::loadParams(text, p, error)) return false;
            const auto normalized = builder.search(field, &pair, 1, rules, DEFAULT_TIMING, p, 7);
            if (normalized.depth != 1 || normalized.cut || normalized.best != one.best ||
                normalized.alive != one.alive || normalized.nodes != one.nodes || normalized.valued != one.valued) return false;
        }
    }
    return true;
}

int main() {
    using juraic::ai::Crew;
    for (int failAt : {0, 1, 3}) {
        int made = 0;
        bool passedOn = false;
        try {
            Crew crew(4, [&](auto&& fn) {
                if (made == failAt) throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
                ++made;
                return std::thread(std::forward<decltype(fn)>(fn));
            });
        } catch (const std::system_error&) {
            passedOn = true;
        }
        if (!passedOn || made != failAt) {
            std::cerr << "FAILED: a helper that cannot be started (at " << failAt << ")\n";
            return 1;
        }
    }
    // Started in full: the crew shares out the parts, each part once.
    Crew crew(3);
    std::atomic_int done[50] = {};
    crew.each(50, [&](size_t k) { ++done[k]; });
    for (const auto& d : done)
        if (d != 1) {
            std::cerr << "FAILED: every part once\n";
            return 1;
        }
    if (!testWorkerFailure()) {
        std::cerr << "FAILED: an inner thread startup failure did not return from the worker pool\n";
        return 1;
    }
    if (!testWorkerNumbersStop()) {
        std::cerr << "FAILED: a failed worker did not end the numbers of the others\n";
        return 1;
    }
    if (!testMinimumDepth()) {
        std::cerr << "FAILED: a beam depth below one did not search one layer\n";
        return 1;
    }
    std::cout << "crew threads and minimum beam depth: ok\n";
    return 0;
}
