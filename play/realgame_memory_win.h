#pragma once
// The real game's process read (the Windows build, only with JURAIC_REALGAME_MEMORY=1): opened with the rights to read
// its memory and to ask what it is (ProcessMemory::READ_RIGHTS) and no other - never one to write to it, to change its
// pages, to make memory or a thread in it, to end it or to take a handle of it. Nothing is written to the process and
// nothing is run in it.
#include "realgame_memory.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <winternl.h>
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

namespace play::realgame {

static_assert(REGION_COMMITTED == MEM_COMMIT && REGION_RESERVED == MEM_RESERVE && REGION_PRIVATE == MEM_PRIVATE && REGION_MAPPED == MEM_MAPPED &&
              REGION_IMAGE == MEM_IMAGE);
static_assert(PAGE_READ_WRITE == PAGE_READWRITE && PAGE_WRITE_COPY == PAGE_WRITECOPY &&
              PAGE_RUN == (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY) && PAGE_GUARDED == PAGE_GUARD &&
              PAGE_UNCACHED == PAGE_NOCACHE && PAGE_WRITE_COMBINED == PAGE_WRITECOMBINE);

struct ProcessMemory {
    static constexpr DWORD READ_RIGHTS = PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION;

    ProcessMemory() = default;
    ProcessMemory(const ProcessMemory&) = delete;
    ProcessMemory& operator=(const ProcessMemory&) = delete;
    ~ProcessMemory() { close(); }

    // Opens the process `processId` to read. False, with the reason in `error`, when it cannot be.
    bool open(DWORD processId, std::wstring& error) {
        close();
        handle = OpenProcess(READ_RIGHTS, FALSE, processId);
        if (!handle) return error = L"OpenProcess: " + std::to_wstring(GetLastError()), false;
        id = processId;
        return true;
    }
    void close() {
        if (handle) CloseHandle(handle);
        handle = nullptr, id = 0;
    }
    bool isOpen() const { return handle != nullptr; }

    // `bytes` bytes at `address` into `into`; false when not all of them could be read (`got`: how many were).
    bool read(uint64_t address, void* into, size_t bytes, size_t* got = nullptr) const {
        SIZE_T n = 0;
        const bool all = handle && ReadProcessMemory(handle, reinterpret_cast<LPCVOID>(address), into, bytes, &n) && n == bytes;
        if (got) *got = size_t(n);
        return all;
    }
    // The production reader admits data pages only, checking every region covered before copying any bytes.
    bool readData(uint64_t address, void* into, size_t bytes) const {
        if (!handle || !into || !bytes || address > UINT64_MAX - bytes) return false;
        const uint64_t end = address + bytes;
        for (uint64_t at = address; at < end;) {
            MEMORY_BASIC_INFORMATION m{};
            if (VirtualQueryEx(handle, reinterpret_cast<LPCVOID>(at), &m, sizeof m) != sizeof m) return false;
            const MemoryRegion r{uint64_t(uintptr_t(m.BaseAddress)), uint64_t(m.RegionSize), uint64_t(uintptr_t(m.AllocationBase)), m.State, m.Protect, m.Type};
            if (!dataRegion(r) || r.base > at || r.size > UINT64_MAX - r.base || r.base + r.size <= at) return false;
            at = std::min(end, r.base + r.size);
        }
        return read(address, into, bytes);
    }
    // The regions of the address space that are reserved or committed, from the lowest up.
    std::vector<MemoryRegion> regions() const {
        std::vector<MemoryRegion> all;
        MEMORY_BASIC_INFORMATION m;
        for (uint64_t at = 0; handle && VirtualQueryEx(handle, reinterpret_cast<LPCVOID>(at), &m, sizeof m) == sizeof m;) {
            const uint64_t base = uint64_t(uintptr_t(m.BaseAddress)), next = base + uint64_t(m.RegionSize);
            if (m.State != MEM_FREE) all.push_back({base, uint64_t(m.RegionSize), uint64_t(uintptr_t(m.AllocationBase)), m.State, m.Protect, m.Type});
            if (next <= at) break;
            at = next;
        }
        return all;
    }
    // The rights the system gave the handle (0: not told).
    DWORD granted() const {
        using Query = LONG(NTAPI*)(HANDLE, OBJECT_INFORMATION_CLASS, PVOID, ULONG, PULONG);
        const auto query = reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryObject"));
        PUBLIC_OBJECT_BASIC_INFORMATION info{};
        return handle && query && query(handle, ObjectBasicInformation, &info, sizeof info, nullptr) >= 0 ? info.GrantedAccess : 0;
    }
    // The start of the program image of the process (0: not told), from its block of the process environment, which the
    // system tells with the right to ask what the process is.
    uint64_t imageBase() const {
        using Query = LONG(NTAPI*)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
        const auto query = reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
        PROCESS_BASIC_INFORMATION info{};
        if (!handle || !query || query(handle, ProcessBasicInformation, &info, sizeof info, nullptr) < 0 || !info.PebBaseAddress) return 0;
        uint64_t base = 0;
        return read(uint64_t(uintptr_t(info.PebBaseAddress)) + offsetof(PEB, Reserved3) + sizeof(PVOID), &base, sizeof base) ? base : 0;
    }
    // The program of the process (empty: not told).
    std::wstring program() const {
        wchar_t path[MAX_PATH * 2];
        DWORD size = DWORD(std::size(path));
        return handle && QueryFullProcessImageNameW(handle, 0, path, &size) ? std::wstring(path, size) : std::wstring();
    }

    HANDLE handle = nullptr;
    DWORD id = 0;
};

// The processes of the program `program` (its full path, letters of either case): from the list of the processes,
// which opens none of them, those of its file name, each then asked only for its program.
inline std::vector<DWORD> programProcesses(const std::wstring& program) {
    std::vector<DWORD> found;
    const std::wstring name = std::filesystem::path(program).filename().wstring();
    const HANDLE list = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (list == INVALID_HANDLE_VALUE) return found;
    PROCESSENTRY32W e{};
    e.dwSize = sizeof e;
    for (BOOL more = Process32FirstW(list, &e); more; more = Process32NextW(list, &e)) {
        if (_wcsicmp(e.szExeFile, name.c_str()) != 0) continue;
        const HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, e.th32ProcessID);
        if (!p) continue;
        wchar_t path[MAX_PATH * 2];
        DWORD size = DWORD(std::size(path));
        if (QueryFullProcessImageNameW(p, 0, path, &size) && _wcsicmp(path, program.c_str()) == 0) found.push_back(e.th32ProcessID);
        CloseHandle(p);
    }
    CloseHandle(list);
    return found;
}

} // namespace play::realgame
