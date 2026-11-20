#pragma once
#include "follow14_win.h"
#include <chrono>
#include <span>
namespace play::realgame {
struct DataReadTiming14 {uint64_t queryNs=0,copyNs=0,bytes=0;size_t queries=0,copies=0,failures=0;};
// Data protection is checked before copying. A field pass may reuse a covering region for at most 2ms;
// those permissions are discarded at the end of the pass and checked afresh for the second pass.
class FieldDataReader14 {
    using Clock=std::chrono::steady_clock;
    struct Lease {MemoryRegion region;Clock::time_point checked;};
public:
    explicit FieldDataReader14(const ProcessMemory& memory):memory_(memory){}
    bool checked(uint64_t address,void* into,size_t bytes){std::vector<Lease> unused;return copy(address,into,bytes,unused,false);}
    bool pass(std::span<const uint64_t> visibleTop,std::span<Field14Bytes> into){
        if(visibleTop.size()!=into.size() || visibleTop.size()>4096)return false;
        std::vector<Lease> leases;leases.reserve(8);
        for(size_t i=0;i<visibleTop.size();++i){if(visibleTop[i]<64 || !copy(visibleTop[i]-64,into[i].data(),into[i].size(),leases,true))return false;}
        return true;
    }
    DataReadTiming14 timing;
private:
    bool copy(uint64_t address,void* into,size_t bytes,std::vector<Lease>& leases,bool reuse){
        if(!memory_.handle || !into || !bytes || address>UINT64_MAX-bytes){++timing.failures;return false;}
        const auto end=address+bytes;
        for(uint64_t at=address;at<end;){
            const auto now=Clock::now();const Lease* covering=nullptr;
            if(reuse)for(const auto& lease:leases)
                if(now>=lease.checked && now-lease.checked<=std::chrono::milliseconds(2) && lease.region.base<=at &&
                   lease.region.size<=UINT64_MAX-lease.region.base && lease.region.base+lease.region.size>at){covering=&lease;break;}
            MemoryRegion region;
            if(covering)region=covering->region;
            else {
                MEMORY_BASIC_INFORMATION info{};const auto begin=Clock::now();++timing.queries;
                const auto got=VirtualQueryEx(memory_.handle,reinterpret_cast<LPCVOID>(at),&info,sizeof info);
                const auto queried=Clock::now();timing.queryNs+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(queried-begin).count());
                if(got!=sizeof info){++timing.failures;return false;}
                region={uint64_t(uintptr_t(info.BaseAddress)),uint64_t(info.RegionSize),uint64_t(uintptr_t(info.AllocationBase)),info.State,info.Protect,info.Type};
                if(!dataRegion(region) || region.base>at || region.size>UINT64_MAX-region.base || region.base+region.size<=at){++timing.failures;return false;}
                if(reuse)leases.push_back({region,queried});
            }
            at=std::min(end,region.base+region.size);
        }
        const auto begin=Clock::now();++timing.copies;const bool ok=memory_.read(address,into,bytes);
        timing.copyNs+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-begin).count());
        if(ok)timing.bytes+=bytes;else ++timing.failures;
        return ok;
    }
    const ProcessMemory& memory_;
};
} // namespace play::realgame
