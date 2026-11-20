#pragma once
#include "scan14.h"
#include "../realgame_memory_win.h"
namespace play::realgame {
// Probe at lifecycle cadence, not every video frame. This opens read/query rights only.
// The caller must synchronize the follower even on failure (the output then has no process).
inline bool openFieldProcess14(const std::wstring& selectedProgram,ProcessMemory& memory,FieldProcess& into,std::wstring& error) {
    memory.close();into={};
    const auto pids=programProcesses(selectedProgram);
    if(pids.size()!=1) {error=L"Selected program must have exactly one running process";return false;}
    if(!memory.open(pids[0],error))return false;
    const auto fail=[&](const wchar_t* why){error=why;memory.close();return false;};
    if(memory.granted()!=ProcessMemory::READ_RIGHTS || _wcsicmp(memory.program().c_str(),selectedProgram.c_str())!=0)
        return fail(L"Process identity or read-only handle rights changed");
    FILETIME creation{},exit{},kernel{},user{};DWORD exitCode=0;
    if(!GetProcessTimes(memory.handle,&creation,&exit,&kernel,&user) || !GetExitCodeProcess(memory.handle,&exitCode) || exitCode!=STILL_ACTIVE)
        return fail(L"Process is no longer available");
    const uint64_t base=memory.imageBase();std::array<uint8_t,4096> header{};
    if(!base || !memory.read(base,header.data(),header.size()))return fail(L"Image metadata unavailable");
    const auto image=readImageHeader(header.data(),header.size());
    if(!verifiedFieldImage(image))return fail(L"Unverified program version");
    into={memory.id,(uint64_t(creation.dwHighDateTime)<<32)|creation.dwLowDateTime,base,image};
    error.clear();return true;
}
} // namespace play::realgame
