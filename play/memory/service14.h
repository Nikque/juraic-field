#pragma once
#include "scan14.h"
#include <memory>
namespace play::realgame {
// Integration boundary: lifecycle signals and fresh visible observations are supplied by the host.
// One tick scans at most one configured worker chunk. No internal worker thread.
class FieldFollowService14 {
public:
    using RegionQuery=std::function<std::vector<MemoryRegion>()>;
    explicit FieldFollowService14(Follow14Config config={}) : follower_(config),config_(config) {}
    void synchronize(const FieldContext& context) {
        const auto before=follower_.generation();follower_.synchronize(context);
        if(follower_.generation()!=before) {scan_.reset();retryAfter_=0;}
    }
    std::optional<Follow14Result> tick(const FieldContext& context,const VisiblePair14* visible,uint64_t nowMs,
                                     const RegionQuery& regions,const FieldRead14& read,const FieldReadPass14& batch={}) {
        synchronize(context);
        if(follower_.state()==Follow14State::NeedScan) {
            if(!scan_ && nowMs>=retryAfter_)
                scan_=std::make_unique<FieldScan14>(context,follower_.generation(),regions(),config_.maxScanBytes,config_.maxCandidates,config_.scanChunkBytes);
            if(scan_) {
                if(!scan_->done()) scan_->step(read);
                if(scan_->done()) {
                    lastCatalog_=scan_->catalog();++scanSerial_;
                    const bool installed=follower_.install(lastCatalog_);scan_.reset();
                    if(!installed) retryAfter_=nowMs>UINT64_MAX-1000?UINT64_MAX:nowMs+1000;
                }
            }
        }
        if(visible) return follower_.observe(*visible,nowMs,read,batch);
        return {};
    }
    Follow14State state() const {return follower_.state();}
    uint64_t generation() const {return follower_.generation();}
    bool scanning() const {return bool(scan_);}
    uint64_t scanBytes() const {return scan_?scan_->bytesRead():0;}
    uint64_t scanTotal() const {return scan_?scan_->plannedBytes():0;}
    uint64_t scanSerial() const {return scanSerial_;}
    const FieldCatalog14& lastCatalog() const {return lastCatalog_;}
    const char* reason() const {return follower_.reason();}
    uint64_t failingAddress() const {return follower_.failingAddress();}
    bool current(const Follow14Result& result,uint64_t nowMs) const {return follower_.current(result,nowMs);}
private:
    FieldFollower14 follower_;
    Follow14Config config_;
    std::unique_ptr<FieldScan14> scan_;
    uint64_t retryAfter_=0;
    FieldCatalog14 lastCatalog_;
    uint64_t scanSerial_=0;
};
} // namespace play::realgame
