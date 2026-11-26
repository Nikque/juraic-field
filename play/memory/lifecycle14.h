#pragma once
#include "follow14.h"
#include "../realgame_read.h"
namespace play::realgame {
// Image-derived lifecycle notifications. A mid-round attachment waits for a witnessed round start.
class FieldLifecycle14 {
public:
    // Data discovery can identify a witnessed round before the exact first-spawn clock is known.
    // Production control retains the strict default and additionally checks its current sample.
    explicit FieldLifecycle14(bool requireFrameOrigin=true):requireFrameOrigin_(requireFrameOrigin){}
    void process(const FieldProcess& p) {
        FieldContext a{process_,0,false},b{p,0,false};
        if(!sameFieldContext(a,b)) {process_=p;resetTracking();}
    }
    bool step(const Reading& reading,int64_t frame,uint64_t observedMs) {
        if(frame<0 || (haveFrame_ && frame==frame_))return false;
        if(haveFrame_ && (frame<frame_ || observedMs<observedMs_ || observedMs-observedMs_>MAX_GAP_MS))resetTracking();
        tracker_.step(reading,frame);frame_=frame;observedMs_=observedMs;haveFrame_=true;++sequence_;
        bool empty=true;
        for(int s=0;s<2;++s) {
            const auto& t=tracker_.side[s];
            empty=empty && t.clear && t.resting.f.empty() && !t.inHand && !t.loose && !t.words.any;
        }
        // Revoke immediately when a possible gap starts; do not keep the old epoch for the 30-frame confirmation.
        if(empty && !emptyLatched_ && (tracker_.side[0].pairs || tracker_.side[1].pairs))blocked_=true;
        emptyLatched_=empty;
        if(tracker_.rounds!=rounds_) {rounds_=tracker_.rounds;++epoch_;blocked_=false;}
        return true;
    }
    VisiblePair14 visible(uint64_t nowMs) const {
        VisiblePair14 e;e.context.process=process_;e.context.round=epoch_;e.sequence=sequence_;e.observedMs=observedMs_;
        const bool recent=haveFrame_ && nowMs>=observedMs_ && nowMs-observedMs_<=MAX_GAP_MS;
        e.context.roundKnown=recent && !blocked_ && tracker_.fromStart && rounds_>0 && (!requireFrameOrigin_ || tracker_.roundStart>=0);
        e.settled=e.context.roundKnown && tracker_.still(0) && tracker_.still(1);
        for(int s=0;s<2;++s)for(int y=0;y<12;++y)for(int x=0;x<6;++x)
            e.cells[size_t(s)][size_t(y*6+x)]=tracker_.side[s].clear?int8_t(tracker_.side[s].resting.f.get(x,y)):-1;
        return e;
    }
    // Address/format discovery has its own local epoch. It needs fresh images
    // and temporal seat proofs, not the exact native round origin. This scope
    // MUST NOT authorize control; usableMemorySample14 still requires fromStart
    // and the exact matching roundStart, independently of this flag.
    VisiblePair14 discoveryVisible(uint64_t nowMs) const {
        auto e=visible(nowMs);
        e.context.discoveryOnly=true;
        e.context.roundKnown=process_.pid && epoch_ && haveFrame_ && nowMs>=observedMs_ && nowMs-observedMs_<=MAX_GAP_MS;
        e.settled=e.context.roundKnown && tracker_.still(0) && tracker_.still(1);
        return e;
    }
    const Tracker& tracker() const {return tracker_;}
    uint64_t epoch() const {return epoch_;}
    uint64_t sequence() const {return sequence_;}
    static constexpr uint64_t MAX_GAP_MS=1500;
private:
    void resetTracking(){tracker_.reset();rounds_=0;blocked_=true;emptyLatched_=false;haveFrame_=false;++epoch_;}
    bool requireFrameOrigin_=true;
    FieldProcess process_;
    Tracker tracker_;
    uint64_t epoch_=0,sequence_=0,observedMs_=0;
    int64_t frame_=-1;
    int rounds_=0;
    bool haveFrame_=false,blocked_=true,emptyLatched_=false;
};
} // namespace play::realgame
