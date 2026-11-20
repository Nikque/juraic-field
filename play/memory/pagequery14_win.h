#pragma once
#include "follow14_win.h"
#include <psapi.h>
#include <chrono>
#include <span>
namespace play::realgame {
// Experimental per-page reader. No product caller until live validation is complete.
// No protection cache: every batch checks every covered resident page before any copy.
// PROCESS_QUERY_INFORMATION is necessary for QueryWorkingSetEx; this remains read/query only.
class PageQueryReader14 {
public:
    static constexpr DWORD RIGHTS=ProcessMemory::READ_RIGHTS|PROCESS_QUERY_INFORMATION;
    struct Range {uint64_t address=0;void* into=nullptr;size_t bytes=0;};
    // absent: pages asked for that were not in the working set; pagedIn: of them, read all the same (patient())
    struct Timing {uint64_t queryNs=0,copyNs=0,bytes=0,queries=0,copies=0,failures=0,absent=0,pagedIn=0;} timing;
    // Pages not in the working set are read all the same from now on (the system brings them back as they are read: no
    // byte of the program changes, a read may take milliseconds) - or, as at first, are not read.
    void patient(bool on){patient_=on;}
    bool open(const FieldProcess& process,std::wstring& error){
        close();if(!process.pid || !process.creationTime || !process.imageBase || !verifiedFieldImage(process.image))return false;
        memory_.handle=OpenProcess(RIGHTS,FALSE,process.pid);memory_.id=process.pid;
        if(!memory_.handle)return error=L"page query open: "+std::to_wstring(GetLastError()),false;
        FILETIME created{},exited{},kernel{},user{};
        if(memory_.granted()!=RIGHTS || memory_.imageBase()!=process.imageBase || !GetProcessTimes(memory_.handle,&created,&exited,&kernel,&user) ||
           (uint64_t(created.dwHighDateTime)<<32|created.dwLowDateTime)!=process.creationTime){close();return false;}
        SYSTEM_INFO sys{};GetSystemInfo(&sys);page_=sys.dwPageSize;
        if(!page_ || (page_&(page_-1))){close();return false;}
        return true;
    }
    void close(){memory_.close();page_=0;}
    DWORD granted()const{return memory_.granted();}
    bool checked(uint64_t address,void* into,size_t bytes){Range r{address,into,bytes};return batch({&r,1}) || batch({&r,1});}
    bool pass(std::span<const uint64_t> tops,std::span<Field14Bytes> into){
        if(tops.size()!=into.size())return failed();
        std::vector<Range> ranges;ranges.reserve(tops.size());
        for(size_t i=0;i<tops.size();++i){if(tops[i]<64)return failed();ranges.push_back({tops[i]-64,into[i].data(),into[i].size()});}
        return batch(ranges);
    }
    bool batch(std::span<const Range> ranges){
        DWORD status=0;
        if(!memory_.handle || !page_ || ranges.empty() || ranges.size()>4096 ||
           !GetExitCodeProcess(memory_.handle,&status) || status!=STILL_ACTIVE)return failed();
        std::vector<uint64_t> pages;size_t total=0;
        for(const auto& r:ranges){
            if(!r.address || !r.into || !r.bytes || r.bytes>65536 || r.address>UINT64_MAX-r.bytes ||
               total>(4u<<20)-r.bytes)return failed();
            total+=r.bytes;const auto last=(r.address+r.bytes-1)&~(page_-1);
            for(uint64_t a=r.address&~(page_-1);;a+=page_){pages.push_back(a);if(a==last)break;}
        }
        std::sort(pages.begin(),pages.end());pages.erase(std::unique(pages.begin(),pages.end()),pages.end());
        if(pages.size()>8192)return failed();
        std::vector<PSAPI_WORKING_SET_EX_INFORMATION> info(pages.size());
        for(size_t i=0;i<pages.size();++i)info[i].VirtualAddress=reinterpret_cast<PVOID>(pages[i]);
        const auto begin=Clock::now();++timing.queries;
        const bool ok=QueryWorkingSetEx(memory_.handle,info.data(),DWORD(info.size()*sizeof info[0]))!=FALSE;
        const auto certified=Clock::now();
        timing.queryNs+=elapsed(begin);if(!ok)return failed();
        for(const auto& i:info){
            if(i.VirtualAttributes.Bad)return failed();
            uint32_t protect=uint32_t(i.VirtualAttributes.Win32Protection);
            if(!i.VirtualAttributes.Valid){
                // (not in the working set: its protection is asked of its region)
                ++timing.absent;
                MEMORY_BASIC_INFORMATION m{};
                if(!patient_ || VirtualQueryEx(memory_.handle,i.VirtualAddress,&m,sizeof m)!=sizeof m || m.State!=MEM_COMMIT)return failed();
                protect=uint32_t(m.Protect);++timing.pagedIn;
            }
            const auto access=protect&PAGE_ACCESS;
            if((access!=PAGE_READ_WRITE && access!=PAGE_WRITE_COPY) ||
               (protect&(PAGE_RUN|PAGE_GUARDED|PAGE_UNCACHED|PAGE_WRITE_COMBINED)))return failed();
        }
        for(const auto& r:ranges){if(!patient_ && Clock::now()-certified>std::chrono::milliseconds(2))return failed();
            const auto before=Clock::now();++timing.copies;
            const bool copied=memory_.read(r.address,r.into,r.bytes);timing.copyNs+=elapsed(before);
            if(!copied)return failed();timing.bytes+=r.bytes;
        }return true;
    }
private:
    using Clock=std::chrono::steady_clock;
    static uint64_t elapsed(Clock::time_point t){return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-t).count());}
    bool failed(){++timing.failures;return false;}
    ProcessMemory memory_;uint64_t page_=0;bool patient_=false;
};
static_assert(PageQueryReader14::RIGHTS==0x1410);
} // namespace play::realgame
