#pragma once
#include "owned_pose_gate14.h"
#include "../realgame_read.h"
namespace play::realgame {
// The worker has independently associated each typed owner with a temporally
// verified seat/board binding. Values retain their structural discovery marker;
// this packet adds the lifecycle and consumer proof instead of toggling it off.
struct MemoryMotionSample14 {
    FieldContext context;
    uint64_t generation=0,observedMs=0;
    int64_t frame=-1,roundStart=-1;
    std::array<int64_t,2> poseFrame{-1,-1},readBegin100ns{},readEnd100ns{};
    std::array<uint32_t,2> nativeTick{};
    std::array<bool,2> bound{},advancing{},running{};
    std::array<uint32_t,2> pairs{};
    std::array<std::optional<OwnedPublicPose14>,2> pose;
};
inline bool usableMemoryMotion14(const MemoryMotionSample14& s,const Tracker& t,uint64_t now,uint32_t pid){
    return pid && s.context.process.pid==pid && s.context.process.creationTime && verifiedFieldImage(s.context.process.image) &&
        !s.context.discoveryOnly && s.context.roundKnown && s.context.round && s.generation &&
        t.fromStart && t.roundStart>=0 && s.roundStart==t.roundStart && s.frame+1>=t.frame &&
        now>=s.observedMs && now-s.observedMs<=OwnedPoseGate14::MAX_AGE_MS &&
        s.frame<=int64_t(now*60/1000)+1 && t.frame>=0 && int64_t(now*60/1000)-t.frame<=15;
}
struct MemoryControlPose14 {
    int64_t frame=-1,readBegin100ns=0,readEnd100ns=0;
    uint32_t nativeTick=0;
    int pair=0;
    juraic::Pair colors;
    juraic::PairControl at;
};
inline std::optional<MemoryControlPose14> memoryControlPose14(const MemoryMotionSample14& s,const Tracker& t,int seat,uint64_t now,uint32_t pid){
    if(seat<0 || seat>1 || !usableMemoryMotion14(s,t,now,pid) || !s.bound[size_t(seat)] || !s.running[size_t(seat)] || !s.pose[size_t(seat)])return {};
    const auto& p=*s.pose[size_t(seat)];const auto& side=t.side[seat];
    if(s.poseFrame[size_t(seat)]<0 || s.poseFrame[size_t(seat)]>s.frame || s.frame-s.poseFrame[size_t(seat)]>3)return {};
    if(uint64_t(p.lockedPairs)+1!=uint64_t(s.pairs[size_t(seat)]))return {};
    auto context=p.owner.context;context.discoveryOnly=s.context.discoveryOnly;
    if(!sameFieldContext(context,s.context) || !p.owner.player || !p.owner.field || !p.owner.operate ||
       !p.operating || p.horizontalMoving || p.playerState!=1 || p.fieldState!=1 ||
       p.x<0 || p.x>5 || p.y<0 || p.y>13 || p.dir<0 || p.dir>3 ||
       (p.dir==1 && p.x==5) || (p.dir==3 && p.x==0) || (p.dir==0 && p.y==13) || (p.dir==2 && p.y==0))return {};
    if(p.seat!=seat || uint64_t(p.lockedPairs)+1!=uint64_t(side.pairs) || side.unseen ||
       p.currentColors!=std::array<int,2>{side.colors.axis,side.colors.child})return {};
    MemoryControlPose14 out;out.frame=s.poseFrame[size_t(seat)];out.nativeTick=p.count108;out.pair=side.pairs;
    out.readBegin100ns=s.readBegin100ns[size_t(seat)];out.readEnd100ns=s.readEnd100ns[size_t(seat)];
    out.colors={juraic::Cell(p.currentColors[0]),juraic::Cell(p.currentColors[1])};
    out.at.x=int8_t(p.x);out.at.y=int8_t(p.y);out.at.upper=p.upper;out.at.dir=uint8_t(p.dir);return out;
}
}
