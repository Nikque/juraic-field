#include "../play/memory/predictive_input.h"
#include "../play/memory/pose_input_model.h"
#include <cstdio>
#include <map>
using namespace juraic;
struct ProbeAgent:ai::Agent {
    ai::Observation last;int calls=0;
    void startRound(int,uint64_t)override{}
    uint8_t frame(const ai::Observation& o)override{
        last=o;++calls;
        if(o.self().phase!=Phase::Control)return 0;
        const int x=(o.self().nextPair-1)%W;
        return uint8_t(BTN_DOWN | (o.self().pair.x<x?BTN_RIGHT:o.self().pair.x>x?BTN_LEFT:0) |
            ((o.frame%8)==0?BTN_CW:0));
    }
};
int main(){
    int checks=0,failed=0;const auto check=[&](bool b,const char* what){++checks;if(!b){++failed;std::printf("FAIL %s\n",what);}};
    ProbeAgent a;play::realgame::PredictiveInput input(a,4);Round r;r.start(Rules{},50,2);
    std::deque<uint8_t> pad(4,0);std::map<int64_t,Player> predictions;
    int matched=0,bad=0;bool visibleOnly=true,duplicateSafe=true;
    for(int f=0;f<1200 && r.state==RoundState::Playing;++f){
        auto current=ai::observe(r,1);
        if(auto p=predictions.find(r.frame);p!=predictions.end()){
            const auto& expected=p->second;const auto& actual=r.player[1];
            if(expected.phase==Phase::Control && actual.phase==Phase::Control){++matched;bad+=!(expected.pair==actual.pair) || !(expected.field==actual.field) || expected.nextPair!=actual.nextPair;}
        }
        const auto command=input.tick(current);
        if(input.predictionComplete){
            predictions[input.predicted.frame]=input.predicted.self();
            visibleOnly=visibleOnly && input.predicted.mine().count()==current.mine().count();
            for(int i=0;i<current.mine().count();++i){const auto p=current.mine().pair(i),q=input.predicted.mine().pair(i);visibleOnly=visibleOnly && p.axis==q.axis && p.child==q.child;}
        }
        const int calls=a.calls;duplicateSafe=duplicateSafe && input.tick(current)==command && a.calls==calls;
        pad.push_back(command);const uint8_t buttons[]={0,pad.front()};pad.pop_front();PlayerEvents events[2];r.step(buttons,events);
    }
    check(duplicateSafe,"duplicate native update neither searches twice nor advances its path");
    check(matched>100 && bad==0,"queued-input forecasts match later real control states without reading future state");
    check(visibleOnly,"prediction never expands the visible pair window");
    auto current=ai::observe(r,1);current.frame=1;check(input.tick(current)==0,"backward clock revokes the input queue");
    current.frame=20;current.state=RoundState::Won0;const int calls=a.calls;
    check(input.tick(current)==0 && a.calls==calls,"ended round releases buttons without invoking the AI");
    current.state=RoundState::Playing;current.me=2;check(input.tick(current)==0,"invalid seat cannot produce inputs");
    ProbeAgent b;play::realgame::PredictiveInput unknown(b,30);Round hidden;hidden.start(Rules{},7,2);auto empty=ai::observe(hidden,1);
    empty.player[1].wait=1;empty.tsumo.pairs.clear();check(unknown.tick(empty)==0 && !unknown.predictionComplete && b.calls==0,"unknown next piece stops prediction instead of inventing future colors");
    play::realgame::PoseInputModel model;Round modeled;modeled.start(Rules{},50,2);int equal=0,wrong=0;uint8_t previous=0;bool duplicateTimers=true;
    for(int f=0;f<800 && modeled.state==RoundState::Playing;++f){
        auto source=ai::observe(modeled,1);auto& poison=source.player[1];
        poison.pair.gain=30000;poison.pair.groundFrames=250;poison.pair.repeat=250;poison.pair.held=255;
        poison.pair.quickLock=250;poison.pair.quickWindow=250;poison.pair.queued=255;poison.pair.grounded=!poison.pair.grounded;
        poison.buttons=255;poison.sideHeld=250;poison.turnQueued=255;poison.turnAge=250;poison.sideQueued=255;poison.sideAge=250;
        const auto inferred=model.observe(source,previous);
        const auto duplicate=model.observe(source,previous);
        duplicateTimers=duplicateTimers && duplicate.self().pair==inferred.self().pair && duplicate.self().sideHeld==inferred.self().sideHeld;
        if(modeled.player[1].phase==Phase::Control){++equal;wrong+=!(inferred.self().pair==modeled.player[1].pair);}
        previous=modeled.player[1].phase==Phase::Control?uint8_t(BTN_DOWN):0;
        const uint8_t buttons[]={0,previous};PlayerEvents events[2];modeled.step(buttons,events);
    }
    check(equal>50 && wrong==0,"control timers are reconstructed from own inputs even when supplied timer fields are poisoned");
    check(duplicateTimers,"duplicate pose updates retain reconstructed timers without advancing");
    auto invalid=ai::observe(modeled,1);invalid.me=2;check(model.observe(invalid,0).me==2,"invalid pose seat is rejected before indexing the player array");
    {
        // Buttons set too late for their frame (PredictiveInput::undelivered): the frame goes by with the buttons before them.
        ProbeAgent c;play::realgame::PredictiveInput late(c,0);Round q;q.start(Rules{},50,2);
        bool tried=false,undone=false,expectedRight=false;
        for(int f=0;f<600 && q.state==RoundState::Playing && !tried;++f){
            const auto o=ai::observe(q,1);
            uint8_t command=late.tick(o);
            if(late.predictionComplete && o.self().phase==Phase::Control && late.expected() && command!=late.plannedAt(o.frame-1)){
                const uint8_t before=late.plannedAt(o.frame-1);
                late.undelivered();
                undone=late.plannedAt(o.frame)==before;
                command=before,tried=true;
            }
            const uint8_t buttons[]={0,command};PlayerEvents events[2];q.step(buttons,events);
            if(tried)expectedRight=late.expected() && late.expected()->pair==q.player[1].pair && late.expected()->phase==q.player[1].phase;
        }
        check(tried && undone,"buttons set too late: the frame they were for is taken as played with the buttons before them");
        check(expectedRight,"... and the side expected after it is the one the game has");
    }
    {
        // A pair a column off at the same height (a button a frame late): the clocks of its fall are kept.
        play::realgame::PoseInputModel m;Round p;p.start(Rules{},50,2);
        bool tested=false,kept=false,resetOnHeight=false;
        for(int f=0;f<400 && p.state==RoundState::Playing && !tested;++f){
            auto source=ai::observe(p,1);
            const auto& real=p.player[1];
            if(real.phase==Phase::Control && real.pair.gain>0 && real.pair.x>0 && !real.pair.upper){
                play::realgame::PoseInputModel n=m;
                auto shifted=source;shifted.player[1].pair.x-=1;
                const auto a=m.observe(shifted,0);
                kept=a.self().pair.gain==real.pair.gain && m.repaired==1;
                auto higher=source;higher.player[1].pair.y+=1;
                const auto b=n.observe(higher,0);
                resetOnHeight=b.self().pair.gain==0;
                tested=true;
            }else m.observe(source,0);
            const uint8_t buttons[]={0,0};PlayerEvents events[2];p.step(buttons,events);
        }
        check(tested && kept,"a pair a column off at the same height keeps the clocks of its fall");
        check(resetOnHeight,"... one at another height begins them anew");
    }
    std::printf("predictive input: %d checks, %d failures, %d forecasts matched, %d timer reconstructions\n",checks,failed,matched,equal);return failed?1:0;
}
