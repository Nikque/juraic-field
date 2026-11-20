#pragma once
#include <cstdint>
namespace play::realgame {
// Timestamp a native update, not every poll of that update. Polling an unchanged
// counter must never make the same pose appear to be a newer game state.
class MotionClock14 {
    uint32_t tick_=0;int64_t frame_=-1;
public:
    int64_t observe(uint32_t tick,uint64_t nowMs){
        const int64_t host=int64_t(nowMs/1000)*60+int64_t(nowMs%1000)*60/1000;
        if(frame_<0 || tick<tick_)frame_=host;
        else if(tick!=tick_){
            const auto expected=frame_+int64_t(tick-tick_);
            frame_=expected>=host-1 && expected<=host?expected:host;
        }
        tick_=tick;return frame_;
    }
};
}
