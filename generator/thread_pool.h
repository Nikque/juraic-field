#pragma once
// Runs one function on several threads and joins them all, also when a thread cannot be started:
// a std::thread still joinable when it is destroyed ends the program (std::terminate).
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace juraic {

// Runs `work` on `count` new threads made by `start(work)` and joins them. If making a thread throws,
// `abort()` is called (it must make the threads already started end soon), those threads are joined
// and the exception is passed on. `start` is a parameter so that a test can make one start fail.
template <class Work, class Abort, class Start>
void runOnThreads(int count, Work& work, Abort&& abort, Start&& start) {
    std::vector<std::thread> pool;
    pool.reserve(count > 0 ? size_t(count) : 0); // before any thread: push_back below does not reallocate
    try {
        for (int i = 0; i < count; ++i) pool.push_back(start(work));
    } catch (...) {
        abort();
        for (std::thread& t : pool) t.join();
        throw;
    }
    for (std::thread& t : pool) t.join();
}

template <class Work, class Abort> void runOnThreads(int count, Work& work, Abort&& abort) {
    runOnThreads(count, work, std::forward<Abort>(abort), [](Work& w) { return std::thread(w); });
}

// A worker may itself need to start threads. Return its error to the caller
// after the other workers have stopped and joined. Abort must not throw.
template <class Work, class Abort> void runWorkers(int count, Work& work, Abort&& abort) {
    if (count <= 1) {
        work();
        return;
    }
    std::exception_ptr failure;
    std::once_flag failed;
    std::once_flag aborted;
    auto requestAbort = [&] { std::call_once(aborted, abort); };
    auto guarded = [&] {
        try {
            work();
        } catch (...) {
            std::call_once(failed, [&] {
                failure = std::current_exception();
                requestAbort();
            });
        }
    };
    runOnThreads(count, guarded, requestAbort);
    if (failure) std::rethrow_exception(failure);
}

} // namespace juraic
