#pragma once
#include "follow14.h"
#include <chrono>
#include <cstring>
namespace play::realgame {
// Does not require nonempty cells: early/empty fields are candidates, never automatic seat assignments.
inline bool findFields14(uint64_t base,const uint8_t* bytes,size_t size,std::set<uint64_t>& into,size_t limit=4096) {
    if(!bytes || size>UINT64_MAX-base) return false;
    for(size_t at=size_t((4-base%4)%4);at<=size && size-at>=440;at+=4) {
        if((memoryValue(bytes+at,4)>>16)!=9 || (memoryValue(bytes+at+4,4)>>16)!=10 ||
           (memoryValue(bytes+at+64,4)>>16)!=25) continue;
        juraic::GameField f;
        if(decodeField14(bytes+at,size-at,f)) {
            into.insert(base+at+64);
            if(into.size()>limit) return false;
        }
    } return true;
}
// The addresses, at every fourth byte, of the 8 bytes `mark` (the mark of a class: the objects of that class).
inline bool findMarks14(uint64_t base,const uint8_t* bytes,size_t size,uint64_t mark,std::set<uint64_t>& into,size_t limit=4096) {
    if(!bytes || size>UINT64_MAX-base) return false;
    for(size_t at=size_t((4-base%4)%4);at<=size && size-at>=8;at+=4) {
        uint64_t value=0;std::memcpy(&value,bytes+at,8);
        if(value==mark) {into.insert(base+at);if(into.size()>limit) return false;}
    } return true;
}
// Complete admissible data regions only. Worker steps may use up to 16MiB; the caller checks lifecycle between steps.
// A failed/truncated scan never produces an installable catalog.
class FieldScan14 {
public:
    FieldScan14(const FieldContext& context,uint64_t generation,const std::vector<MemoryRegion>& regions,
                uint64_t maxBytes=uint64_t(2)<<30,size_t maxCandidates=4096,size_t chunkBytes=65536) : maxCandidates_(maxCandidates),chunkBytes_(chunkBytes) {
        result_.context=context;result_.generation=generation;
        if(!verifiedFieldImage(context.process.image)) {failed_=true;return;}
        if(chunkBytes_<440 || chunkBytes_>(16u<<20)) {failed_=true;return;}
        uint64_t total=0;
        for(const auto& r:regions) if(dataRegion(r)) {
            if(!r.size || r.base>UINT64_MAX-r.size) {failed_=true;return;}
            if(r.size>maxBytes-total) {failed_=tooLarge_=true;return;}
            total+=r.size;regions_.push_back(r);
        }
        std::sort(regions_.begin(),regions_.end(),[](const auto&a,const auto&b){return a.base<b.base;});
        for(size_t i=1;i<regions_.size();++i) if(regions_[i-1].base+regions_[i-1].size>regions_[i].base) {failed_=true;return;}
        plannedBytes_=total;
        buffer_.resize(chunkBytes_+439);
    }
    // The scan also collects where the 8 bytes `mark` stand (marks()). Asked before the first step; 0: nothing more.
    void findAlso(uint64_t mark) {mark_=mark;}
    // Bytes that cannot be read do not end the scan (a region freed or changed while it runs): they are passed over.
    void skipUnread() {skipUnread_=true;}
    std::vector<uint64_t> marks() const {return done() && !failed_ ? std::vector<uint64_t>(marked_.begin(),marked_.end()) : std::vector<uint64_t>();}
    bool done() const {return failed_ || region_==regions_.size();}
    bool failed() const {return failed_;}
    // The admissible regions came to more than maxBytes: nothing is scanned.
    bool tooLarge() const {return tooLarge_;}
    uint64_t bytesRead() const {return bytesRead_;}
    uint64_t plannedBytes() const {return plannedBytes_;}
    bool step(const FieldRead14& read) {
        if(done()) return false;
        const auto& r=regions_[region_];
        const size_t count=size_t(std::min<uint64_t>(chunkBytes_,r.size-offset_));
        std::copy_n(tail_.data(),tailSize_,buffer_.data());
        if(!read(r.base+offset_,buffer_.data()+tailSize_,count)) {
            if(!skipUnread_) {failed_=true;return false;}
            // (read again in pieces of 64 KiB; a piece that cannot be read is taken as zeros, which are no board)
            for(size_t at=0;at<count;at+=65536) {
                const size_t piece=std::min<size_t>(65536,count-at);
                if(!read(r.base+offset_+at,buffer_.data()+tailSize_+at,piece)) std::fill_n(buffer_.data()+tailSize_+at,piece,uint8_t(0));
            }
        }
        bytesRead_+=count;
        const size_t n=tailSize_+count;
        if(!findFields14(r.base+offset_-tailSize_,buffer_.data(),n,addresses_,maxCandidates_)) {failed_=true;return false;}
        // (too many places with the mark: none of them is taken, the boards are found as without it)
        if(mark_ && !findMarks14(r.base+offset_-tailSize_,buffer_.data(),n,mark_,marked_,maxCandidates_)) mark_=0,marked_.clear();
        tailSize_=std::min<size_t>(439,n);
        std::copy_n(buffer_.data()+n-tailSize_,tailSize_,tail_.data());
        offset_+=count;
        if(offset_==r.size) {
            const uint64_t end=r.base+r.size;
            ++region_;offset_=0;
            if(region_==regions_.size() || regions_[region_].base!=end) tailSize_=0;
        }
        return true;
    }
    FieldCatalog14 catalog() const {
        auto c=result_;c.complete=done() && !failed_;
        c.visibleTop.assign(addresses_.begin(),addresses_.end());return c;
    }
private:
    FieldCatalog14 result_;
    std::vector<MemoryRegion> regions_;
    std::set<uint64_t> addresses_,marked_;
    uint64_t mark_=0;
    size_t region_=0,tailSize_=0,maxCandidates_=4096;
    uint64_t offset_=0,bytesRead_=0,plannedBytes_=0;
    bool failed_=false,tooLarge_=false;
    std::array<uint8_t,439> tail_{};
    size_t chunkBytes_=65536;
    std::vector<uint8_t> buffer_;
    bool skipUnread_=false;
};
} // namespace play::realgame
