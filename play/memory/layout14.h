#pragma once
#include "../realgame_memory.h"
#include "../../game/field.h"
#include <array>
#include <functional>
namespace play::realgame {
// Only the verified 14-row layout is needed at run time. Discovery, pointer-path and 12-row-only
// decoders stay in the private research archive instead of being dependencies of the game.
struct MemoryImageId {
    uint16_t machine=0;uint32_t timestamp=0,size=0;
    bool matches(const ImageHeader& h) const {return h.ok && machine==h.machine && timestamp==h.timeStamp && size==h.imageSize;}
};
struct Pair14Binding {
    MemoryImageId image;uint32_t pid=0;uint64_t roundEpoch=0;
    std::array<uint64_t,2> visibleTop{};bool layoutVerified=false;
};
inline uint32_t memoryValue(const uint8_t* bytes,int unit){
    if(unit==1)return bytes[0];
    if(unit==2)return uint32_t(bytes[0])|uint32_t(bytes[1])<<8;
    return uint32_t(bytes[0])|uint32_t(bytes[1])<<8|uint32_t(bytes[2])<<16|uint32_t(bytes[3])<<24;
}
inline std::string memoryFieldText14(const juraic::GameField& field){
    std::string out;out.reserve(98);
    for(int y=13;y>=0;--y){for(int x=0;x<6;++x)
        out.push_back(char('0'+int(y==13?field.row14[size_t(x)]:field.f.get(x,y))));out.push_back('\n');}
    return out;
}
} // namespace play::realgame
