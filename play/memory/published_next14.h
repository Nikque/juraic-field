#pragma once
// Read only two NEXT slots already confirmed by a fresh public image. Never supply an unread future slot.
#include "motion_sample14.h"
namespace play::realgame {
struct PublishedNextVerified14 {FieldContext context;int seat=-1;uint64_t observedMs=0;int64_t frame=-1,roundStart=-1;std::array<int,2> currentColors{};std::array<juraic::Pair,2> next{};uint32_t pair=0,tick=0;uint64_t player=0,nextObject=0,generation=0;};
struct PublishedNextAudit14 {int stage=0;uint32_t beforeReject=0;uint64_t root=0,primary=0,secondary=0,owner=0;std::array<uint32_t,3> slide{};};
inline std::optional<PublishedNextVerified14> readPublishedNext14(const MemoryMotionSample14& s,const Tracker& t,int seat,
    uint64_t now,uint32_t pid,const FieldRead14& read,PublishedNextAudit14* audit=nullptr){
    const auto stage=[&](int value){if(audit)audit->stage=value;};stage(1);
    const auto accepted=memoryControlPose14(s,t,seat,now,pid);
    if(!accepted || !s.advancing[size_t(seat)])return {};
    stage(2);
    const auto& side=t.side[seat];const auto& image=t.last.side[seat];const auto& p=*s.pose[seat];
    // This first verifier deliberately requires both colours to have been read from an actual current image.
    // It establishes source correspondence, not new information or a native-NEXT strength gain.
    if(!side.clear || !side.inHand || !side.pair.seen || now*60/1000>uint64_t(t.frame+3) ||
       !image.nextRead[0] || !image.nextRead[1] || p.fieldHold ||
       !image.next[0].axis || !image.next[0].child || !image.next[1].axis || !image.next[1].child)return {};
    stage(3);
    const auto heap=[&](uint64_t a,uint64_t n){const auto base=s.context.process.imageBase;return a>=4096&&a<=UINT64_MAX-n&&!(a>=base&&a-base<s.context.process.image.imageSize);};
    auto before=readOwnedPublicPose14(s.context,p.owner.player,read);
    uint32_t beforeReject=!before?1:0;
    if(before){
        beforeReject|=uint32_t(before->seat!=seat)<<1;
        beforeReject|=uint32_t(before->owner.field!=p.owner.field)<<2;
        beforeReject|=uint32_t(before->owner.operate!=p.owner.operate)<<3;
        beforeReject|=uint32_t(!before->operating)<<4;
        beforeReject|=uint32_t(before->horizontalMoving)<<5;
        beforeReject|=uint32_t(before->lockedPairs!=p.lockedPairs)<<6;
        beforeReject|=uint32_t(before->currentColors!=p.currentColors)<<7;
        beforeReject|=uint32_t(before->playerState!=1)<<8;
        beforeReject|=uint32_t(before->fieldState!=1)<<9;
        beforeReject|=uint32_t(before->fieldHold)<<10;
    }
    if(audit)audit->beforeReject=beforeReject;
    if(beforeReject)return {};
    stage(4);
    struct Meta {uint64_t pointer=0,primary=0,secondary=0,owner=0;std::array<uint32_t,3> slide{};
        bool operator==(const Meta&)const=default;};
    uint64_t root=0;
    auto meta=[&](Meta& v){
        if(!read(p.owner.player+0x90,&v.pointer,8) || v.pointer<0x1020)return false;
        const auto candidate=v.pointer-0x20;if(root && candidate!=root)return false;root=candidate;
        if(!heap(root,0xf4) || !read(root,&v.primary,8) || !read(root+0x20,&v.secondary,8) ||
           !read(root+0x28,&v.owner,8) || !read(root+0x38,v.slide.data(),12))return false;
        if(audit){audit->root=root;audit->primary=v.primary;audit->secondary=v.secondary;audit->owner=v.owner;audit->slide=v.slide;}
        return v.primary==s.context.process.imageBase+0x2ac288 && v.secondary==s.context.process.imageBase+0x2ac300 &&
               v.owner==p.owner.player && v.slide[0]==0 && v.slide[2]==0;
    };
    Meta a,b;if(!meta(a))return {};
    stage(5);
    // No read covers ring +0x44..0x83, RNG/source pointers, third slot +0x110, or dynamic future arrays.
    using Record=std::array<uint32_t,3>;std::array<Record,3>x{},y{};
    auto records=[&](auto& into){return read(root+0x88,into[0].data(),12)&&read(root+0xc0,into[1].data(),12)&&read(root+0xe8,into[2].data(),12);};
    if(!records(x) || !records(y) || x!=y)return {};stage(6);
    if(!meta(b) || a!=b)return {};stage(7);
    auto after=readOwnedPublicPose14(s.context,p.owner.player,read);
    if(!after || after->owner.operate!=before->owner.operate || after->owner.field!=before->owner.field ||
       after->seat!=before->seat || !after->operating || after->lockedPairs!=before->lockedPairs ||
       after->count108!=before->count108 || after->currentColors!=before->currentColors ||
       after->playerState!=1 || after->fieldState!=1 || after->fieldHold)return {};
    stage(8);
    for(const auto& r:x)if(r[0]!=0x201 || r[1]<1 || r[1]>5 || r[2]<1 || r[2]>5)return {};
    stage(9);
    if(std::array<int,2>{int(x[0][1]),int(x[0][2])}!=before->currentColors)return {};
    stage(10);
    PublishedNextVerified14 out;out.context=s.context;out.seat=seat;out.observedMs=now;out.frame=t.frame;out.roundStart=t.roundStart;out.currentColors=before->currentColors;out.pair=uint32_t(side.pairs);out.tick=after->count108;out.player=p.owner.player;out.nextObject=root;out.generation=s.generation;
    for(int i=0;i<2;++i){out.next[i]={juraic::Cell(x[i+1][1]),juraic::Cell(x[i+1][2])};
        if(out.next[i].axis!=image.next[i].axis || out.next[i].child!=image.next[i].child)return {};}
    stage(11);
    return out;
}
inline bool publishedNextMatchesImage14(const PublishedNextVerified14& n,const Tracker& t,int seat){
    if(seat<0 || seat>1)return false;
    const auto& image=t.last.side[seat];
    for(size_t i=0;i<2;++i)
        if(!image.nextRead[i] || image.next[i].axis!=n.next[i].axis || image.next[i].child!=n.next[i].child)return false;
    return true;
}
inline bool usablePublishedNext14(const PublishedNextVerified14& n,const MemoryMotionSample14& m,const Tracker& t,int seat,uint64_t now,uint32_t pid){
    const auto p=memoryControlPose14(m,t,seat,now,pid);
    return p && m.advancing[size_t(seat)] && n.seat==seat && sameFieldContext(n.context,m.context) &&
        n.generation==m.generation && n.roundStart==t.roundStart && n.frame>=0 && n.frame<=t.frame && t.frame-n.frame<=3 &&
        now>=n.observedMs && now-n.observedMs<=50 && n.pair==uint32_t(t.side[seat].pairs) &&
        n.player==m.pose[size_t(seat)]->owner.player && n.currentColors==m.pose[size_t(seat)]->currentColors &&
        publishedNextMatchesImage14(n,t,seat);
}
} // namespace play::realgame
