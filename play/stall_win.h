#pragma once
// The watch over the window's thread, in the build with the parts for finding faults (JURAIC_REALGAME_DEBUG): when no
// frame has been played for QUIET_MS and the window does not answer within ANSWER_MS (Windows would call it not
// responding), where every thread of the program is is written to hang\<date and time>.txt in the folder of one's own
// files - the module, the function and the line of each frame of its stack, the names from the program's PDB next to
// it. While the stall lasts it is written again every AGAIN_MS, SAMPLES times at most, and the time the window answered
// again is added. Windows x64 only.
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace play {

class StallWatch {
public:
    static constexpr int QUIET_MS = 2000, ANSWER_MS = 1000, AGAIN_MS = 5000, SAMPLES = 6;
    static constexpr int FRAMES = 48;   // frames of a stack written
    static constexpr int THREADS = 256; // threads written at most

    StallWatch() = default;
    StallWatch(const StallWatch&) = delete;
    StallWatch& operator=(const StallWatch&) = delete;
    ~StallWatch() { stop(); }

    // Begins to watch the thread that calls it, whose window is `window`; the files go to `folder`\hang.
    void start(HWND window, const std::filesystem::path& folder) {
        stop();
        watched = window, own = GetCurrentThreadId(), out = folder / L"hang";
        last = clock(), running = true;
        watcher = std::thread([this] { watch(); });
    }
    void stop() {
        running = false;
        if (watcher.joinable()) watcher.join();
    }
    // A frame of the window's thread; `screen`: the screen it is on, for the file.
    void beat(const char* screen) {
        shown.store(screen);
        last = clock();
    }

private:
    struct Stack {
        DWORD id = 0;
        int frames = 0;
        DWORD64 pc[FRAMES]{};
    };
    HWND watched = nullptr;
    DWORD own = 0;
    std::filesystem::path out;
    std::atomic<int64_t> last{0};
    std::atomic<const char*> shown{"-"};
    std::atomic<bool> running{false};
    std::thread watcher;

    static int64_t clock() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void watch() {
        const HANDLE me = GetCurrentProcess();
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const bool symbols = SymInitializeW(me, std::filesystem::path(exe).parent_path().c_str(), TRUE) != FALSE;
        std::vector<Stack> stacks(THREADS); // (made before any thread is stopped: nothing is allocated while one is)
        std::filesystem::path file;
        int samples = 0;
        int64_t began = 0, sampled = 0;
        while (running) {
            Sleep(250);
            const int64_t now = clock(), quiet = now - last;
            if (quiet < QUIET_MS) {
                if (!file.empty()) write(file, "the window answered again after " + std::to_string((now - began) / 1000.0) + " s\n");
                file.clear(), samples = 0;
                continue;
            }
            if (!file.empty() && (samples >= SAMPLES || now - sampled < AGAIN_MS)) continue;
            DWORD_PTR answer = 0;
            if (SendMessageTimeoutW(watched, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, ANSWER_MS, &answer)) continue; // (a dialog, the window moved: it answers)
            if (file.empty()) {
                began = now - quiet;
                std::error_code ignored;
                std::filesystem::create_directories(out, ignored);
                file = out / (stamp(true) + L".txt");
            }
            sampled = clock(), ++samples;
            const int count = take(stacks);
            std::string text = "== " + narrow(stamp(false)) + ": no frame for " + std::to_string(quiet) + " ms, the window does not answer (screen " + shown.load() +
                               ", sample " + std::to_string(samples) + ")\n";
            for (int k = 0; k < count; ++k) text += describe(me, stacks[size_t(k)], symbols);
            write(file, text);
        }
        if (symbols) SymCleanup(me);
    }

    // The stacks of the threads of this program but the watcher's, each stopped while it is walked.
    int take(std::vector<Stack>& stacks) const {
        const HANDLE shot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (shot == INVALID_HANDLE_VALUE) return 0;
        const DWORD pid = GetCurrentProcessId(), watcherId = GetCurrentThreadId();
        THREADENTRY32 t{};
        t.dwSize = sizeof t;
        int count = 0;
        // (the window's thread first)
        for (int pass = 0; pass < 2; ++pass)
            for (BOOL more = Thread32First(shot, &t); more && count < THREADS; more = Thread32Next(shot, &t)) {
                if (t.th32OwnerProcessID != pid || t.th32ThreadID == watcherId || (t.th32ThreadID == own) != (pass == 0)) continue;
                const HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, t.th32ThreadID);
                if (!h) continue;
                Stack& s = stacks[size_t(count)];
                s.id = t.th32ThreadID, s.frames = 0;
                if (SuspendThread(h) != DWORD(-1)) {
                    CONTEXT c{};
                    c.ContextFlags = CONTEXT_FULL;
                    if (GetThreadContext(h, &c)) s.frames = walk(c, s.pc);
                    ResumeThread(h);
                }
                CloseHandle(h);
                ++count;
            }
        CloseHandle(shot);
        return count;
    }
    // The return addresses of a stopped thread, by the unwind tables (nothing allocated).
    static int walk(CONTEXT c, DWORD64* pc) {
        int n = 0;
#if defined(_M_X64)
        __try {
            while (n < FRAMES && c.Rip) {
                pc[n++] = c.Rip;
                DWORD64 base = 0;
                PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(c.Rip, &base, nullptr);
                if (!f) { // (a function with no frame: its return address is on top of the stack)
                    c.Rip = *reinterpret_cast<const DWORD64*>(c.Rsp);
                    c.Rsp += 8;
                    continue;
                }
                void* data = nullptr;
                DWORD64 frame = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, c.Rip, f, &c, &data, &frame, nullptr);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
#else
        (void)c, (void)pc;
#endif
        return n;
    }
    std::string describe(HANDLE me, const Stack& s, bool symbols) const {
        std::string text = "thread " + std::to_string(s.id) + (s.id == own ? " (the window's)" : "");
        if (const HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, s.id)) {
            PWSTR name = nullptr;
            if (SUCCEEDED(GetThreadDescription(h, &name)) && name) {
                if (*name) text += " \"" + narrow(name) + "\"";
                LocalFree(name);
            }
            CloseHandle(h);
        }
        text += "\n";
        for (int i = 0; i < s.frames; ++i) {
            const DWORD64 pc = s.pc[i];
            char line[160];
            HMODULE module = nullptr;
            wchar_t path[MAX_PATH] = L"?";
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(pc), &module))
                GetModuleFileNameW(module, path, MAX_PATH);
            std::snprintf(line, sizeof line, "  %2d %s+0x%llx", i, narrow(std::filesystem::path(path).filename().wstring()).c_str(),
                          static_cast<unsigned long long>(pc - reinterpret_cast<DWORD64>(module)));
            text += line;
            if (symbols) {
                alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256]{};
                auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
                symbol->SizeOfStruct = sizeof(SYMBOL_INFO), symbol->MaxNameLen = 255;
                DWORD64 off = 0;
                if (SymFromAddr(me, pc, &off, symbol)) text += "  " + std::string(symbol->Name) + "+0x" + hex(off);
                IMAGEHLP_LINE64 at{};
                at.SizeOfStruct = sizeof at;
                DWORD col = 0;
                if (SymGetLineFromAddr64(me, pc, &col, &at)) text += "  " + std::filesystem::path(at.FileName).filename().string() + ":" + std::to_string(at.LineNumber);
            }
            text += "\n";
        }
        return text;
    }

    static std::string hex(DWORD64 v) {
        char b[24];
        std::snprintf(b, sizeof b, "%llx", static_cast<unsigned long long>(v));
        return b;
    }
    static std::string narrow(const std::wstring& w) {
        std::string s;
        for (const wchar_t c : w) s += c < 128 ? char(c) : '?';
        return s;
    }
    // The local time: for a file name (20261021-012416) or for a line (2026-10-21 01:24:16).
    static std::wstring stamp(bool forName) {
        const std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &now);
        wchar_t b[32];
        std::wcsftime(b, 32, forName ? L"%Y%m%d-%H%M%S" : L"%Y-%m-%d %H:%M:%S", &local);
        return b;
    }
    static void write(const std::filesystem::path& file, const std::string& text) {
        std::ofstream f(file, std::ios::app | std::ios::binary);
        f << text;
    }
};

} // namespace play
