#pragma once
#include "follow14.h"
#include <cstring>
namespace play::realgame {
// Pure summary of the already-observed public image; never a reader or gate.
struct VisibleQuality14 {
    std::array<int,2> known{},pieces{},colors{};
    bool sufficient=true;
};
inline VisibleQuality14 visibleQuality14(const VisiblePair14& v,const Follow14Config& c){
    VisibleQuality14 q;
    for(size_t seat=0;seat<2;++seat){bool colors[7]{};
        for(const auto cell:v.cells[seat]){
            if(cell< -1 || cell>6){q.sufficient=false;continue;}
            if(cell>=0)++q.known[seat];
            if(cell>0){++q.pieces[seat];if(!colors[int(cell)])colors[int(cell)]=true,++q.colors[seat];}
        }
        if(q.known[seat]<c.minKnown||q.pieces[seat]<c.minPieces||q.colors[seat]<c.minColors)q.sufficient=false;
    }
    return q;
}
inline int workerReason14(const char* r){
    constexpr const char* names[]={"waiting","context_changed","waiting_for_visible_evidence","candidate_unreadable",
        "candidate_changed","candidate_invalid","visible_mismatch","ambiguous","confirming","verified"};
    if(!r)return 0;for(int i=0;i<int(std::size(names));++i)if(std::strcmp(r,names[i])==0)return i+1;return 0;
}
}
