#pragma once
#include "follow14.h"
namespace play::realgame {
struct FieldRegionPlan14 {std::vector<MemoryRegion> regions;bool profiled=false;};
// Version-specific search priority learned from the saved, independently matched fields.
// A region's size never assigns a seat: current screen matching and temporal proof remain mandatory.
inline FieldRegionPlan14 planFieldRegions14(const FieldProcess& process,const std::vector<MemoryRegion>& all,bool full=false) {
    std::vector<MemoryRegion> arenas;
    if(!full && verifiedFieldImage(process.image))for(const auto& r:all)
        // Measured live: 500MiB + one 4KiB allocator page. Other captures have leading metadata pages.
        if(dataRegion(r) && r.type==REGION_PRIVATE && r.size<=(uint64_t(500)<<20)+65536 &&
           r.size>=(uint64_t(500)<<20)-65536)arenas.push_back(r);
    if(arenas.size()!=1)return {all,false};
    auto chosen=arenas;
    // Keep the main image's data in the census as well; it contains a retained typed field in saved captures.
    for(const auto& r:all)if(dataRegion(r) && r.type==REGION_IMAGE && r.allocationBase==process.imageBase)chosen.push_back(r);
    return {chosen,true};
}
} // namespace play::realgame
