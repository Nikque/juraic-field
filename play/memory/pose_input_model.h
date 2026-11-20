#pragma once
#include "../../ai/observe.h"
namespace play::realgame {
// Reconstruct own control timers from the command history, not from native
// memory. Only x/y/half-cell/direction are taken from the supplied pose. The
// surrounding public observation (phase, fields, visible pairs) is the caller's.
class PoseInputModel {
    juraic::Player previous_;
    juraic::Tsumo known_;
    int64_t frame_=-1;
public:
    long repaired=0;
    void reset(){frame_=-1;known_={};previous_={};}
    // placed false: where the pair is was not read in this frame (the source's place of it is not to be gone by): the
    // pair is where the buttons sent have brought it.
    juraic::ai::Observation observe(const juraic::ai::Observation& source,uint8_t plannedPrevious,bool placed=true){
        if(source.me<0 || source.me>=source.players || source.players>2 || source.frame<0 || source.state!=juraic::RoundState::Playing){reset();return source;}
        auto out=source;auto& mine=out.player[out.me];auto model=previous_;
        if(frame_>=0 && source.frame==frame_+1){
            juraic::Incoming nobody;
            juraic::advancePlayer(model,nobody,plannedPrevious,known_,source.rules,source.timing,frame_);
        }else if(source.frame!=frame_)model=juraic::Player{};
        const bool samePair=model.phase==juraic::Phase::Control && mine.phase==juraic::Phase::Control && model.nextPair==mine.nextPair &&
            model.colors.axis==mine.colors.axis && model.colors.child==mine.colors.child;
        const auto seen=!placed && samePair?model.pair:mine.pair;
        auto derived=samePair?model.pair:juraic::PairControl{};
        const bool sameHeight=derived.y==seen.y && derived.upper==seen.upper;
        const bool samePose=sameHeight && derived.x==seen.x && derived.dir==seen.dir;
        derived.x=seen.x;derived.y=seen.y;derived.upper=seen.upper;derived.dir=seen.dir;
        // (a pair off to the side or turned otherwise at the same height - a button a frame late - keeps the clocks of its fall)
        if(samePair && !samePose){++repaired;if(!sameHeight){derived.gain=0;derived.groundFrames=0;}}
        if(mine.phase==juraic::Phase::Control && (!samePair || !samePose)){
            const juraic::detail::Cells cells(mine.field);
            const bool grounded=!derived.upper && !cells.canStepDown(derived);
            if(!grounded || !derived.grounded)derived.groundFrames=0;
            derived.grounded=grounded;
        }
        mine.pair=derived;mine.buttons=plannedPrevious;
        mine.sideHeld=model.sideHeld;mine.turnQueued=model.turnQueued;mine.turnAge=model.turnAge;
        mine.sideQueued=model.sideQueued;mine.sideAge=model.sideAge;
        previous_=mine;frame_=source.frame;known_={};const auto& visible=source.mine();
        for(int i=std::max(0,visible.count()-juraic::TSUMO_PAIRS);i<visible.count();++i){const auto pair=visible.pair(i);
            known_.piece[(i%juraic::TSUMO_PAIRS)*2]=pair.axis;known_.piece[(i%juraic::TSUMO_PAIRS)*2+1]=pair.child;}
        return out;
    }
};
} // namespace play::realgame
