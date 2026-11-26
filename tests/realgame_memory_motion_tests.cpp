#include "../play/memory/motion_sample14.h"
#include "../play/memory/motion_clock14.h"
#include <cstdio>
using namespace play::realgame;
struct Fixture {
    MemoryMotionSample14 sample;Tracker tracker;
    Fixture(int seat=1){
        auto& c=sample.context;c.process={77,100,0x140000000};c.process.image={true,0x8664,0x66a8c684,0x6fd000};c.round=2;c.roundKnown=true;
        sample.generation=3;sample.observedMs=1670;sample.frame=100;sample.roundStart=10;
        sample.poseFrame[size_t(seat)]=100;
        sample.bound[size_t(seat)]=sample.running[size_t(seat)]=sample.advancing[size_t(seat)]=true;sample.pairs[size_t(seat)]=20;
        tracker.fromStart=true;tracker.roundStart=10;tracker.frame=96;tracker.side[seat].pairs=20;tracker.side[seat].colors={2,5};
        OwnedPublicPose14 p;p.owner={c,0,0x20000,0x10000,0x30000};p.owner.context.discoveryOnly=true;
        p.seat=seat;p.x=4;p.y=6;p.dir=1;p.upper=true;p.operating=true;p.lockedPairs=19;p.count108=500;
        p.playerState=p.fieldState=1;p.currentColors={2,5};sample.pose[size_t(seat)]=p;
    }
    auto get(int seat=1,uint64_t now=1670){return memoryControlPose14(sample,tracker,seat,now,77);}
};
int main(){int checks=0,failed=0;const auto check=[&](bool ok,const char* name){++checks;if(!ok){++failed;std::printf("FAIL %s\n",name);}};
    {MotionClock14 c;check(c.observe(10,1000)==60 && c.observe(10,1016)==60,"repeated poll does not relabel an unchanged native update");check(c.observe(11,1017)==61,"next native update advances one frame");check(c.observe(11,6000)==61 && c.observe(12,6017)==361,"pause stays frozen and resume anchors to the new host interval");check(c.observe(0,7000)==420,"new match counter resets its clock");}
    {Fixture f;auto p=f.get();check(p && p->frame==100 && p->pair==20 && p->at.x==4 && p->at.y==6 && p->at.upper && p->at.dir==1,"current native pose reaches driver coordinates");}
    {Fixture f;check(bool(f.get(1)) && !f.get(0),"only the bound seat supplies its observed pose");}
    {Fixture f;check(!f.get(-1) && !f.get(2),"invalid seat cannot index arrays");}
    {Fixture f;f.sample.running[1]=false;check(!f.get(),"lock or pause blocks pose correction");}
    {Fixture f;f.sample.bound[1]=false;check(!f.get(),"unbound native owner cannot supply a correction");}
    {Fixture f;f.sample.pose[1].reset();check(!f.get(),"missing sample has no cached pose fallback");}
    {Fixture f;check(bool(f.get(1,1720)) && !f.get(1,1721),"sample expires at the consumer too");}
    {Fixture f;check(!f.get(1,1669),"future timestamp rejected");}
    {Fixture f;f.sample.context.discoveryOnly=true;check(!f.get(),"discovery-only seat evidence cannot drive");}
    {Fixture f;f.sample.roundStart++;check(!f.get(),"other match rejected");}
    {Fixture f;f.tracker.fromStart=false;check(!f.get(),"mid-round image attachment waits for a witnessed start");}
    {Fixture f;f.sample.context.process.pid++;check(!f.get(),"other window process rejected");}
    {Fixture f;f.sample.context.process.creationTime=0;check(!f.get(),"unknown process lifetime rejected");}
    {Fixture f;f.sample.context.process.image.timeStamp++;check(!f.get(),"unsupported game version rejected");}
    {Fixture f;f.sample.generation=0;check(!f.get(),"missing temporal proof generation rejected");}
    {Fixture f;f.tracker.frame=80;check(!f.get(),"current memory cannot extend expired image-derived NEXT knowledge");}
    {Fixture f;f.tracker.frame=102;check(!f.get(),"old native frame rejected");f.tracker.frame=101;check(bool(f.get()),"one-frame WGC presentation lead accepted");}
    {Fixture f;f.sample.frame=110;check(!f.get(),"native pose cannot come from a future host frame");}
    {Fixture f;f.tracker.side[1].pairs++;check(!f.get(),"image and native pair serial must agree");}
    {Fixture f;f.tracker.side[1].unseen=1;check(!f.get(),"unresolved image pair count cannot authorize correction");}
    {Fixture f;f.tracker.side[1].colors={5,2};check(!f.get(),"axis and child cannot be interchanged");}
    {Fixture f;f.sample.pose[1]->horizontalMoving=true;check(!f.get(),"half-column drawing interpolation is not rounded into a correction");}
    {Fixture f;f.sample.pose[1]->operating=false;check(!f.get(),"inert copied object cannot supply an active pose");}
    {Fixture f;f.sample.pose[1]->owner.context.process.creationTime++;check(!f.get(),"same PID with another lifetime rejected at bridge");}
    {Fixture f;f.sample.pose[1]->owner.player=0;check(!f.get(),"unowned pose rejected");}
    {Fixture f;f.sample.pose[1]->x=5;check(!f.get(),"child outside board rejected");}
    {Fixture f;f.sample.pose[1]->y=-1;check(!f.get(),"below-floor axis rejected");}
    {Fixture f;f.sample.pose[1]->playerState=5;check(!f.get(),"post-match player rejected");}
    std::printf("native motion consumer: %d checks, %d failures\n",checks,failed);return failed?1:0;
}
