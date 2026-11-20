#pragma once
#include "follow14.h"
namespace play::realgame {
// Structural discovery only. This is deliberately not a MemorySample14 or an
// authorization for control. Proximity to a field is not proof of seat ownership.
struct OwnerCandidate14 {
    FieldContext context;
    uint64_t visibleTop=0,operate=0,player=0,field=0;
};
inline std::vector<OwnerCandidate14> discoverOwnerCandidates14(FieldContext context,uint64_t top,const FieldRead14& read){
    std::vector<OwnerCandidate14> found;
    if(!context.process.pid || !context.process.creationTime || !context.process.imageBase || !context.roundKnown || !context.round ||
       !verifiedFieldImage(context.process.image) || context.process.imageBase>UINT64_MAX-context.process.image.imageSize || top<4096 || top>UINT64_MAX-4096)return found;
    juraic::GameField grid;if(!readStableField14(top,read,grid))return found;
    context.discoveryOnly=true;
    std::array<uint8_t,8192> nearby{};if(!read(top-4096,nearby.data(),nearby.size()))return found;
    const auto image=context.process.imageBase;
    const auto table=[&](uint64_t rva){return image+rva;};
    const auto objectAddress=[&](uint64_t a){return a>=4096 && a<=UINT64_MAX-160 && !(a>=image && a-image<context.process.image.imageSize);};
    const auto word=[](const uint8_t* bytes,size_t at){uint64_t value=0;std::memcpy(&value,bytes+at,8);return value;};
    for(size_t at=0;at+64<=nearby.size();at+=4){
        if(word(nearby.data(),at)!=table(0x2ad2c0))continue;
        const auto operate=top-4096+at;
        const auto player=word(nearby.data(),at+40),field=word(nearby.data(),at+48);
        if(!objectAddress(player) || !objectAddress(field))continue;
        std::array<uint8_t,160> p{};std::array<uint8_t,88> f{};std::array<uint8_t,64> o{};
        if(!read(player,p.data(),p.size()) || !read(field,f.data(),f.size()) || !read(operate,o.data(),o.size()))continue;
        if(word(p.data(),0)!=table(0x2ac500) || word(f.data(),0)!=table(0x2b1470) || word(o.data(),0)!=table(0x2ad2c0) ||
           word(p.data(),64)!=operate || word(p.data(),128)!=field || word(f.data(),80)!=player ||
           word(o.data(),40)!=player || word(o.data(),48)!=field)continue;
        // Recheck the cycle. A destructor may replace a live class with Work or
        // TaskBase while the last board remains byte-for-byte intact in the heap.
        std::array<uint8_t,160> p2{};std::array<uint8_t,88> f2{};std::array<uint8_t,64> o2{};
        if(!read(player,p2.data(),p2.size()) || !read(field,f2.data(),f2.size()) || !read(operate,o2.data(),o2.size()))continue;
        const auto same=[&](const auto& a,const auto& b,std::initializer_list<size_t> offsets){
            for(auto offset:offsets)if(word(a.data(),offset)!=word(b.data(),offset))return false;return true;
        };
        if(!same(p,p2,{0,64,128}) || !same(f,f2,{0,80}) || !same(o,o2,{0,40,48}))continue;
        found.push_back({context,top,operate,player,field});
        if(found.size()>16)return {}; // Do not truncate an ambiguous set into a false unique result.
    }return found;
}
}
