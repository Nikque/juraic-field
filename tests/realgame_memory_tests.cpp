// The reading of the real game's memory (play/realgame_memory_win.h, the Windows build), built only with
// JURAIC_REALGAME_MEMORY=1 (CMake: JURAIC_REALGAME_MEMORY=ON):
//  0. the program of the game (the first argument) holds those parts only when they are built: without them it holds
//     neither their text (MEMORY_PARTS_MARK) nor the name of any function that reads another process's memory or tells
//     what it holds - imported or asked for by name. In any build, neither it nor the other programs named (rgmem)
//     holds the name of a function that writes to another process, changes its pages, makes memory or a thread in it
//     or debugs it;
//  1. with them, a process is opened with the rights to read alone: a process started here (this program again, which
//     waits) is opened, the system gives the handle those rights and no other, and writing to the process, changing
//     its pages, making memory or a thread in it, ending it and taking a handle of it with that handle are all
//     refused (the thread asked of the system itself: CreateRemoteThread opens the process again by itself when the
//     handle has not the right);
//  2. what is read of it is what it holds: the start of its program image and the sections its header tells, the bytes
//     of a table it fills, and the regions of its address space - the table's among the regions of data, its code's
//     not.
#define NOMINMAX
#include <windows.h>
#ifndef JURAIC_REALGAME_MEMORY
#define JURAIC_REALGAME_MEMORY 0
#endif
#if JURAIC_REALGAME_MEMORY
#include "../play/realgame_memory_win.h"
#endif
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) std::printf("FAILED: %s\n", what), ++failures;
}

// Whether the bytes of the file hold the text `text` (as it is, or in letters of 16 bits).
static bool holds(const std::vector<char>& bytes, const std::string& text) {
    if (std::search(bytes.begin(), bytes.end(), text.begin(), text.end()) != bytes.end()) return true;
    std::string wide;
    for (char c : text) wide += c, wide += '\0';
    return std::search(bytes.begin(), bytes.end(), wide.begin(), wide.end()) != bytes.end();
}

// The names that are in a program only when it reads another process's memory, lists its regions or asks what its
// handles and its block of the process environment are.
static const char* const READING_NAMES[] = {"ReadProcessMemory", "VirtualQueryEx", "NtQueryInformationProcess", "NtQueryObject", "Toolhelp32ReadProcessMemory"};

// The names of the functions that write to another process, change its pages, make memory or a thread in it, run
// code in one of its threads or debug it.
static const char* const WRITING_NAMES[] = {"WriteProcessMemory", "NtWriteVirtualMemory", "VirtualProtectEx", "NtProtectVirtualMemory", "VirtualAllocEx",
                                            "NtAllocateVirtualMemory", "CreateRemoteThread", "NtCreateThreadEx", "RtlCreateUserThread", "QueueUserAPC",
                                            "SetThreadContext", "DebugActiveProcess"};

static std::vector<char> fileBytes(const char* program) {
    std::ifstream file(program, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

static void testGameProgram(const char* program) {
    const std::vector<char> bytes = fileBytes(program);
    check(bytes.size() > 100000, "the program of the game cannot be read");
    if (bytes.size() <= 100000) return;
    int names = 0;
    for (const char* name : READING_NAMES) names += holds(bytes, name);
    const bool mark = holds(bytes, "the reading of the real game's memory");
    if (!JURAIC_REALGAME_MEMORY) {
        check(names == 0, "the game built without the reading of the memory holds the name of a function that reads a process");
        check(!mark, "the game built without the reading of the memory holds the text of those parts");
    }
    std::printf("the program of the game: %d of the names of the reading, %s the text of those parts\n", names, mark ? "with" : "without");
}

static void testNoWriting(const char* program) {
    const std::vector<char> bytes = fileBytes(program);
    check(bytes.size() > 10000, "a program named cannot be read");
    int names = 0;
    for (const char* name : WRITING_NAMES)
        if (holds(bytes, name)) std::printf("%s holds %s\n", program, name), ++names;
    check(names == 0, "a program holds the name of a function that writes to another process or runs code in it");
}

#if JURAIC_REALGAME_MEMORY
using play::realgame::ProcessMemory;

// The table the process started here fills, in its writable data.
static uint8_t table[4096];
static uint8_t tableByte(size_t i) { return uint8_t(i * 7 + 13); }

// The process started here: fills the table, tells where its program image, the table and its code are, and waits
// until its input is closed.
static int child() {
    for (size_t i = 0; i < sizeof table; ++i) table[i] = tableByte(i);
    std::printf("%llx %llx %llx\n", (unsigned long long)uintptr_t(GetModuleHandleW(nullptr)), (unsigned long long)uintptr_t(table),
                (unsigned long long)uintptr_t(&child));
    std::fflush(stdout);
    char c;
    DWORD n;
    while (ReadFile(GetStdHandle(STD_INPUT_HANDLE), &c, 1, &n, nullptr) && n) {
    }
    return 0;
}

struct Child {
    PROCESS_INFORMATION info{};
    HANDLE input = nullptr; // (its input: closed, it ends)
    uint64_t base = 0, tableAt = 0, codeAt = 0;
    bool start() {
        SECURITY_ATTRIBUTES inherit{sizeof inherit, nullptr, TRUE};
        HANDLE inRead, inWrite, outRead, outWrite;
        if (!CreatePipe(&inRead, &inWrite, &inherit, 0) || !CreatePipe(&outRead, &outWrite, &inherit, 0)) return false;
        SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0), SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        std::wstring line = L"\"" + std::wstring(self) + L"\" --child";
        STARTUPINFOW si{sizeof si};
        si.dwFlags = STARTF_USESTDHANDLES, si.hStdInput = inRead, si.hStdOutput = outWrite, si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        const bool made = CreateProcessW(self, line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &info);
        CloseHandle(inRead), CloseHandle(outWrite);
        input = inWrite;
        std::string told;
        char c;
        DWORD n;
        while (made && ReadFile(outRead, &c, 1, &n, nullptr) && n && c != '\n') told += c;
        CloseHandle(outRead);
        unsigned long long b = 0, t = 0, f = 0;
        if (!made || std::sscanf(told.c_str(), "%llx %llx %llx", &b, &t, &f) != 3) return false;
        base = b, tableAt = t, codeAt = f;
        return true;
    }
    ~Child() {
        if (input) CloseHandle(input);
        if (info.hProcess) WaitForSingleObject(info.hProcess, 5000), CloseHandle(info.hProcess), CloseHandle(info.hThread);
    }
};

static void testOpenedToRead() {
    Child c;
    check(c.start(), "the process to read is not started");
    if (!c.base) return;
    ProcessMemory m;
    std::wstring error;
    check(m.open(c.info.dwProcessId, error), "the process cannot be opened to read");
    if (!m.isOpen()) return;

    // 1. The rights: those to read and no other; everything else refused.
    const DWORD granted = m.granted();
    check(granted == ProcessMemory::READ_RIGHTS, "the handle has rights other than those to read, or not all of them");
    check((ProcessMemory::READ_RIGHTS & (PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_CREATE_THREAD | PROCESS_DUP_HANDLE | PROCESS_TERMINATE)) == 0,
          "the rights to read hold a right to change the process");
    uint8_t bytes[16]{};
    SIZE_T n = 0;
    check(!WriteProcessMemory(m.handle, reinterpret_cast<LPVOID>(c.tableAt), bytes, sizeof bytes, &n) && GetLastError() == ERROR_ACCESS_DENIED && n == 0,
          "writing to the process is not refused");
    DWORD old = 0;
    check(!VirtualProtectEx(m.handle, reinterpret_cast<LPVOID>(c.tableAt), sizeof table, PAGE_READONLY, &old) && GetLastError() == ERROR_ACCESS_DENIED,
          "changing the pages of the process is not refused");
    check(!VirtualAllocEx(m.handle, nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) && GetLastError() == ERROR_ACCESS_DENIED,
          "making memory in the process is not refused");
    using CreateThread = LONG(NTAPI*)(PHANDLE, ACCESS_MASK, PVOID, HANDLE, PVOID, PVOID, ULONG, SIZE_T, SIZE_T, SIZE_T, PVOID);
    const auto createThread = reinterpret_cast<CreateThread>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateThreadEx"));
    const auto sleep = reinterpret_cast<PVOID>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "Sleep"));
    HANDLE thread = nullptr;
    const LONG status = createThread ? createThread(&thread, THREAD_ALL_ACCESS, nullptr, m.handle, sleep, nullptr, 1 /* suspended */, 0, 0, 0, nullptr) : 0;
    check(createThread && status == LONG(0xC0000022) && !thread, "starting a thread in the process is not refused"); // (STATUS_ACCESS_DENIED)
    if (thread) TerminateThread(thread, 0), CloseHandle(thread);
    check(!TerminateProcess(m.handle, 9) && GetLastError() == ERROR_ACCESS_DENIED, "ending the process is not refused");
    HANDLE taken = nullptr;
    check(!DuplicateHandle(m.handle, HANDLE(uintptr_t(4)), GetCurrentProcess(), &taken, 0, FALSE, DUPLICATE_SAME_ACCESS) && GetLastError() == ERROR_ACCESS_DENIED,
          "taking a handle of the process is not refused");
    if (taken) CloseHandle(taken);
    check(WaitForSingleObject(c.info.hProcess, 0) == WAIT_TIMEOUT, "the process read does not run on");

    // 2. What is read.
    const uint64_t base = m.imageBase();
    check(base == c.base, "the start of the program image is not where the process has it");
    uint8_t page[4096];
    const bool header = m.read(base, page, sizeof page);
    const play::realgame::ImageHeader image = play::realgame::readImageHeader(page, header ? sizeof page : 0);
    check(header && image.ok && image.machine == IMAGE_FILE_MACHINE_AMD64 && image.imageSize > c.tableAt - base, "the header of the program image is not read");
    const auto sectionOf = [&](uint64_t at) -> const play::realgame::ImageSection* {
        for (const auto& s : image.sections)
            if (at >= base + s.address && at < base + s.address + s.size) return &s;
        return nullptr;
    };
    const auto* code = sectionOf(c.codeAt);
    const auto* data = sectionOf(c.tableAt);
    check(code && code->code() && !code->writable(), "the section of the code is not told as code");
    check(data && !data->code() && data->writable(), "the section of the table is not told as writable data");
    std::vector<uint8_t> got(sizeof table);
    check(m.read(c.tableAt, got.data(), got.size()), "the table cannot be read");
    size_t same = 0;
    for (size_t i = 0; i < got.size(); ++i) same += got[i] == tableByte(i);
    check(same == got.size(), "the bytes read are not those of the table");
    std::fill(got.begin(), got.end(), 0);
    check(m.readData(c.tableAt, got.data(), got.size()) && got[0] == tableByte(0) && got.back() == tableByte(got.size() - 1),
          "the data-only reader cannot read a data table");
    check(!m.readData(c.codeAt, got.data(), 16), "the data-only reader admitted executable pages");
    check(!m.readData(UINT64_MAX - 3, got.data(), 16), "the data-only reader admitted an overflowing range");
    uint64_t nothing = 0;
    size_t gotNothing = 1;
    check(!m.read(0x10000 - 8, &nothing, sizeof nothing, &gotNothing) && gotNothing == 0, "an address with nothing there reads as if it held something");

    const std::vector<play::realgame::MemoryRegion> regions = m.regions();
    const auto regionOf = [&](uint64_t at) -> const play::realgame::MemoryRegion* {
        for (const auto& r : regions)
            if (at >= r.base && at < r.base + r.size) return &r;
        return nullptr;
    };
    const auto* tableRegion = regionOf(c.tableAt);
    const auto* codeRegion = regionOf(c.codeAt);
    check(tableRegion && play::realgame::dataRegion(*tableRegion) && tableRegion->type == MEM_IMAGE, "the region of the table is not one of data");
    check(codeRegion && !play::realgame::dataRegion(*codeRegion) && play::realgame::runnable(*codeRegion), "the region of the code is told as data");
    bool ordered = !regions.empty();
    for (size_t i = 1; i < regions.size(); ++i) ordered = ordered && regions[i].base >= regions[i - 1].base + regions[i - 1].size;
    check(ordered, "the regions are not from the lowest up, apart");

    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    check(_wcsicmp(m.program().c_str(), self) == 0, "the program of the process read is not told");
    const std::vector<DWORD> found = play::realgame::programProcesses(self);
    check(std::count(found.begin(), found.end(), c.info.dwProcessId) == 1 && std::count(found.begin(), found.end(), GetCurrentProcessId()) == 1,
          "the processes of a program are not found");
    std::printf("a process opened to read: rights 0x%lx, %zu regions (%zu of data), %zu sections\n", granted, regions.size(),
                size_t(std::count_if(regions.begin(), regions.end(), play::realgame::dataRegion)), image.sections.size());
}
#endif

int main(int argc, char** argv) {
#if JURAIC_REALGAME_MEMORY
    if (argc == 2 && std::string(argv[1]) == "--child") return child();
#endif
    if (argc < 2) return std::printf("usage: realgame_memory_tests <the program of the game> [<another program>...]\n"), 2;
    testGameProgram(argv[1]);
    for (int i = 1; i < argc; ++i) testNoWriting(argv[i]);
#if JURAIC_REALGAME_MEMORY
    testOpenedToRead();
#endif
    std::printf(failures ? "%d FAILED\n" : "realgame memory tests passed\n", failures);
    return failures ? 1 : 0;
}
