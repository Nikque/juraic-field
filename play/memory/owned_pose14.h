#pragma once
#include "owner14.h"
#include <cmath>
namespace play::realgame {
// Decoded PUBLIC current state only. Structural validity does not authorize input.
// There is no input-word, future-pair, random-number, or target-write access here.
struct OwnedPublicPose14 {
    OwnerCandidate14 owner;
    int seat=-1,x=-1,y=-1,dir=-1;
    bool upper=false;
    // The collision column advances before the two-frame horizontal drawing
    // interpolation finishes. Preserve that distinction instead of rounding it.
    bool horizontalMoving=false;
    bool operating=false;
    uint32_t lockedPairs=0;
    uint32_t fieldState=0,playerState=0,phase=0,count108=0,count10c=0;
    uint8_t fieldActive=0,fieldHold=0;
    std::array<int,2> currentColors{};
    bool cpuControlled=false; // the verified local CPU controller owns this seat
};
inline std::optional<OwnedPublicPose14> readOwnedPublicPose14(FieldContext context,uint64_t player,const FieldRead14& read){
    if(!context.process.pid || !context.process.creationTime || !context.process.imageBase ||
       !verifiedFieldImage(context.process.image) || context.process.imageBase>UINT64_MAX-context.process.image.imageSize ||
       player<4096 || player>UINT64_MAX-160)return {};
    const auto image=context.process.imageBase;
    const auto word=[](const auto& a,size_t at){uint64_t v=0;std::memcpy(&v,a.data()+at,8);return v;};
    const auto integer=[](const auto& a,size_t at){uint32_t v=0;std::memcpy(&v,a.data()+at,4);return v;};
    const auto number=[](const auto& a,size_t at){float v=0;std::memcpy(&v,a.data()+at,4);return v;};
    const auto heap=[&](uint64_t a,uint64_t n){return a>=4096 && a<=UINT64_MAX-n && !(a>=image && a-image<context.process.image.imageSize);};
    struct Pass {std::array<uint8_t,160> p{};std::array<uint8_t,64> o{};std::array<uint8_t,88> f{};
        std::array<uint8_t,34> pose{};std::array<uint8_t,8> colors{},phase{},counts{};
        uint32_t locked=0;uint8_t operating=0,active=0,hold=0;};
    Pass a,b;uint64_t operate=0,field=0;
    const auto pass=[&](Pass& v,bool first){
        if(!read(player,v.p.data(),v.p.size()) || word(v.p,0)!=image+0x2ac500)return false;
        // Player's current-operator accessor (vtable +0x140) returns +0x98.
        // +0x40 may instead retain the copied/base operator; its coordinates
        // can look correct while its control flag describes a different object.
        if(first){operate=word(v.p,152);field=word(v.p,128);}
        if(!heap(operate,0xee) || !heap(field,0x2d4) || word(v.p,152)!=operate || word(v.p,128)!=field)return false;
        return read(operate,v.o.data(),v.o.size()) && read(field,v.f.data(),v.f.size()) &&
            word(v.o,0)==image+0x2ad2c0 && word(v.f,0)==image+0x2b1470 &&
            word(v.o,40)==player && word(v.o,48)==field && word(v.f,80)==player &&
            read(operate+0xcc,v.pose.data(),v.pose.size()) && read(operate+0xa8,v.colors.data(),v.colors.size()) &&
            read(operate+0x9d,&v.operating,1) && read(operate+0xa0,&v.locked,4) &&
            read(field+0x2cc,v.phase.data(),v.phase.size()) && read(field+0x108,v.counts.data(),v.counts.size()) &&
            read(field+0x11c,&v.active,1) && read(field+0x18d,&v.hold,1);
    };
    if(!pass(a,true) || !pass(b,false))return {};
    if(a.pose!=b.pose || a.colors!=b.colors || a.phase!=b.phase || a.counts!=b.counts || a.active!=b.active || a.hold!=b.hold ||
       a.operating!=b.operating || a.locked!=b.locked || b.operating>1 ||
       a.p[0x5e]!=b.p[0x5e] || integer(a.p,0x50)!=integer(b.p,0x50))return {};
    const float x=number(b.pose,0),y=number(b.pose,4),shownX=number(b.pose,16),shownY=number(b.pose,20);
    if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(shownX) || !std::isfinite(shownY) ||
       x<0 || x>5 || y< -2 || y>12 || std::abs(2*x-std::round(2*x))>.001f || std::abs(y-std::round(y))>.001f ||
       shownX!=x || (shownY!=y && shownY!=y-.5f) || b.pose[32]<1 || b.pose[32]>6 ||
       std::abs(x-float(b.pose[32]-1))>.5f || b.pose[33]!=int(y)+3 || integer(b.pose,28)>3 ||
       b.p[0x5e]>1 || integer(b.phase,0)>10 || integer(b.phase,4)>31 || integer(b.p,0x50)>15 || b.active>1 || b.hold>1)return {};
    const auto c0=integer(b.colors,0),c1=integer(b.colors,4);
    if(c0>6 || c1>6 || (b.operating && (!c0 || !c1 || c0>5 || c1>5)))return {};
    context.discoveryOnly=true;
    OwnedPublicPose14 out;out.owner={context,0,operate,player,field};
    out.seat=b.p[0x5e];out.x=int(b.pose[32])-1;out.y=11-int(y);out.upper=shownY==y-.5f;out.dir=int(integer(b.pose,28));
    out.horizontalMoving=x!=float(out.x);
    out.operating=b.operating!=0;out.lockedPairs=b.locked;
    out.phase=integer(b.phase,0);out.fieldState=integer(b.phase,4);out.playerState=integer(b.p,0x50);
    out.count108=integer(b.counts,0);out.count10c=integer(b.counts,4);out.fieldActive=b.active;out.fieldHold=b.hold;
    out.currentColors={int(c0),int(c1)};return out;
}
}
