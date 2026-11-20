#pragma once
#include "../../ai/agent.h"
#include <algorithm>
#include <deque>
#include <optional>

namespace play::realgame {
// Experimental input path. Caller supplies a current, independently validated
// Observation; this class does not read processes or authorize native samples.
// Only already sent buttons are replayed. Future pairs and opponent inputs are
// never supplied to prediction. PrimaryPlayer14 (primary_play14.h) plays by it.
class PredictiveInput {
    struct Command {int64_t at;uint8_t buttons;};
    std::deque<Command> sent_;
    juraic::ai::Agent& agent_;
    std::optional<juraic::Player> expected_;
    int64_t last_=-1;
    uint8_t held_=0,applied_=0;
    // The own side the expected one was played on from, and the pairs known then (undelivered).
    juraic::Player expectedFrom_;
    juraic::Tsumo expectedKnown_{};
    static bool sameControl(const juraic::Player& a,const juraic::Player& b){
        return a.phase==b.phase && a.nextPair==b.nextPair && a.field==b.field &&
            (a.phase!=juraic::Phase::Control || (a.colors.axis==b.colors.axis && a.colors.child==b.colors.child && a.pair==b.pair));
    }
    uint8_t at(int64_t frame)const{
        uint8_t b=applied_;for(const auto& c:sent_){if(c.at>frame)break;b=c.buttons;}return b;
    }
public:
    // Frames spent in the physical input queue; zero means a direct local game.
    const int delay;
    long corrections=0,unknown=0;
    // The caller shows a side without a pair in its hands as waiting for its pair only (the landing, a chain and
    // falling garbage are not told apart): what was expected is then held against what is shown only while a pair
    // is in the hands.
    bool pairOnly=false;
    // A pair shown in the hands that the engine, played on by the buttons sent, has put already (the game locks it later
    // than the engine reckons): the down button stays held, so that it does not lie there until it locks by itself.
    bool downInHand=false;
    juraic::ai::Observation predicted;
    bool predictionComplete=false;
    explicit PredictiveInput(juraic::ai::Agent& agent,int inputDelay):agent_(agent),delay(std::clamp(inputDelay,0,30)){}
    uint8_t plannedAt(int64_t gameFrame)const{return at(gameFrame);}
    // The own side as the model has it after the frame the buttons last given are for (nothing: not known). For records.
    const std::optional<juraic::Player>& expected()const{return expected_;}
    void reset(){sent_.clear();expected_.reset();last_=-1;held_=applied_=0;predictionComplete=false;again_=false;}
    uint8_t tick(const juraic::ai::Observation& current){return advance(current,false);}
    // A frame the sender was not called in, made up afterwards: the buttons stayed as they were for it. The AI is shown
    // the frame all the same (it misses no frame: not the one before its pair appears), what it answers is not sent, and
    // the way of its pair is planned again in the next frame from where the buttons that did go out bring it.
    uint8_t stay(const juraic::ai::Observation& current){return advance(current,true);}
    // The buttons last given went out too late for the frame they were for: that frame went by with the buttons before
    // them. The buttons kept for it and the side expected after it are made so, and the way of the pair is planned again
    // in the next frame (as after stay).
    void undelivered(){
        if(sent_.empty())return;
        Command& last=sent_.back();
        const uint8_t before=sent_.size()>1?sent_[sent_.size()-2].buttons:applied_;
        if(last.buttons==before)return;
        last.buttons=before;
        if(expected_){
            expected_=expectedFrom_;juraic::Incoming nobody;
            juraic::advancePlayer(*expected_,nobody,before,expectedKnown_,predicted.rules,predicted.timing,predicted.frame);
        }
        again_=true;
    }
private:
    bool again_=false;
    uint8_t advance(const juraic::ai::Observation& current,bool keep){
        if(current.me<0 || current.me>=current.players || current.players>2 || current.frame<0 || current.state!=juraic::RoundState::Playing){reset();return 0;}
        if(current.frame==last_)return held_;
        if(current.frame<last_){reset();return 0;}
        while(!sent_.empty() && sent_.front().at<current.frame){applied_=sent_.front().buttons;sent_.pop_front();}
        predicted=current;
        juraic::Tsumo known{};
        // Populate only the pairs already visible to this observation.
        const auto& visible=current.mine();
        for(int i=std::max(0,visible.count()-juraic::TSUMO_PAIRS);i<visible.count();++i){
            const auto pair=visible.pair(i);known.piece[(i%juraic::TSUMO_PAIRS)*2]=pair.axis;known.piece[(i%juraic::TSUMO_PAIRS)*2+1]=pair.child;
        }
        auto& mine=predicted.player[current.me];
        predictionComplete=true;
        for(int k=0;k<delay;++k){
            if(mine.phase==juraic::Phase::Decide || (mine.phase==juraic::Phase::Next && !visible.known(mine.nextPair))){predictionComplete=false;++unknown;break;}
            juraic::Incoming nobody;
            juraic::advancePlayer(mine,nobody,at(current.frame+k),known,current.rules,current.timing,current.frame+k);
        }
        predicted.frame=current.frame+delay;
        if(!predictionComplete){if(!keep)held_=0;expected_.reset();}
        else {
            const bool off=expected_ && (current.frame!=last_+1 || (pairOnly?mine.phase==juraic::Phase::Control && !sameControl(*expected_,mine):!sameControl(*expected_,mine)));
            if(off || (again_ && mine.phase==juraic::Phase::Control)){
                ++corrections;if(auto* placing=dynamic_cast<juraic::ai::PlacementAgent*>(&agent_))placing->steer(predicted);
            }
            again_=keep;
            uint8_t answered=agent_.frame(predicted);
            if(downInHand && current.self().phase==juraic::Phase::Control && mine.phase!=juraic::Phase::Control && mine.phase!=juraic::Phase::Dead)answered|=juraic::BTN_DOWN;
            if(!keep)held_=answered;
            expected_=mine;
            if(mine.phase==juraic::Phase::Decide || (mine.phase==juraic::Phase::Next && !visible.known(mine.nextPair)))expected_.reset();
            else {expectedFrom_=mine;expectedKnown_=known;juraic::Incoming nobody;juraic::advancePlayer(*expected_,nobody,held_,known,current.rules,current.timing,predicted.frame);}
        }
        sent_.push_back({predicted.frame,held_});last_=current.frame;return held_;
    }
};
} // namespace play::realgame
