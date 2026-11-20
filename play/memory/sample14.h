#pragma once
#include "follow14.h"
#include "published_next14.h"
#include "../realgame_read.h"
namespace play::realgame {
// The worker's proof is necessary but not sufficient: the consumer's current picture must still
// describe the same process, round, pair numbers and complete visible resting fields on both seats.
struct MemorySample14 {
    Follow14Result value;
    int64_t frame=-1,roundStart=-1;
    std::array<int,2> pairs{},unseen{};
    std::array<std::optional<PublishedNextVerified14>,2> next;
};
inline bool usableMemorySample14(const MemorySample14& s,const Tracker& t,uint64_t nowMs,uint32_t pid) {
    if(!pid || s.value.context.discoveryOnly || s.value.context.process.pid!=pid || !s.value.context.roundKnown || !s.value.context.round ||
       !t.fromStart || t.roundStart<0 || s.roundStart!=t.roundStart || s.frame<0 || s.frame>t.frame || t.frame-s.frame>15 ||
       nowMs<s.value.observedMs || nowMs-s.value.observedMs>250)return false;
    for(size_t side=0;side<2;++side){
        const auto& v=t.side[side];
        if(!t.still(int(side)) || v.pairs!=s.pairs[side] || v.unseen!=s.unseen[side])return false;
        for(int y=0;y<12;++y)for(int x=0;x<6;++x)
            if(s.value.fields[side].f.get(x,y)!=v.resting.f.get(x,y))return false;
    }return true;
}
} // namespace play::realgame
