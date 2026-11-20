#include "../play/memory/owned_pose_gate14.h"
#include <cstdio>
using namespace play::realgame;
struct Case {
    OwnedPoseBinding14 binding;OwnedPublicPose14 pose;OwnedPoseGate14 gate;
    Case(){
        auto& c=binding.context;c.process={1,10,0x140000000};
        c.process.image={true,0x8664,0x66a8c684,0x6fd000};c.round=2;c.roundKnown=true;
        binding.generation=3;binding.player=0x10000;binding.field=0x30000;binding.seat=1;
        pose.owner={c,0,0x20000,binding.player,binding.field};pose.owner.context.discoveryOnly=true;
        pose.seat=1;pose.x=4;pose.y=6;pose.dir=1;pose.upper=true;
        pose.operating=true;pose.lockedPairs=19;pose.count108=500;pose.playerState=pose.fieldState=1;pose.currentColors={2,5};
        gate.bind(binding);gate.observe(pose,3,1000);
    }
    void advance(){++pose.count108;gate.observe(pose,3,1017);gate.observe(pose,3,1019);}
    auto get(uint64_t now=1019){return gate.current(binding.context,3,1,20,{2,5},now);}
};
int main(){int checks=0,failed=0;const auto check=[&](bool ok,const char* name){++checks;if(!ok){++failed;std::printf("FAIL %s\n",name);}};
    {Case c;check(!c.get(1000),"one frozen sample cannot prove game progress");c.advance();auto p=c.get();check(bool(p),"advancing owned current pair is available");check(p && p->owner.context.discoveryOnly,"gate does not certify an uncalibrated controller");}
    {Case c;c.advance();check(bool(c.get(1067)) && !c.get(1068),"native sample expires at bounded age");}
    {Case c;++c.pose.count108;c.gate.observe(c.pose,3,1017);check(!c.get(1017),"advanced counter alone cannot publish an unfinished native update");c.gate.observe(c.pose,3,1018);check(!c.get(1018),"same-millisecond reads cannot certify settling");c.gate.observe(c.pose,3,1020);check(bool(c.get(1020)),"later consistent poll confirms the public state");}
    {Case c;++c.pose.count108;c.gate.observe(c.pose,3,1017);c.gate.observe(c.pose,3,1018);c.gate.observe(c.pose,3,1019);check(bool(c.get(1019)),"fast polling accumulates a stable interval rather than resetting it");c.gate.observe(c.pose,3,1019);check(bool(c.get(1019)),"duplicate poll preserves previously confirmed stability");}
    {Case c;c.advance();c.gate.observe(c.pose,3,1068);check(!c.get(1068),"fresh reads of a paused counter are still paused");++c.pose.count108;c.gate.observe(c.pose,3,1085);c.gate.observe(c.pose,3,1087);check(bool(c.get(1087)),"resume uses the existing binding without heap rediscovery");}
    {Case c;c.advance();c.gate.unreadable();check(!c.get(),"failed read revokes publication immediately");c.gate.observe(c.pose,3,1030);c.gate.observe(c.pose,3,1032);check(bool(c.get(1032)),"transient read failure need not destroy the owner binding");}
    {Case c;c.advance();--c.pose.count108;check(!c.gate.observe(c.pose,3,1030) && !c.get(1030),"native counter rollback requires lifecycle rebinding");}
    {Case c;c.advance();--c.pose.lockedPairs;check(!c.gate.observe(c.pose,3,1030),"lock serial reset cannot continue the previous match");}
    {Case c;c.advance();check(!c.gate.observe(c.pose,3,1000),"clock rollback revokes the binding");}
    {Case c;c.advance();c.pose.operating=false;++c.pose.lockedPairs;c.gate.observe(c.pose,3,1030);check(!c.get(1030),"lock immediately blocks the old piece even while the image holds it");}
    {Case c;c.advance();c.pose.horizontalMoving=true;c.gate.observe(c.pose,3,1030);check(!c.get(1030),"half-column animation is not a settled steering pose");}
    {Case c;c.advance();c.pose.playerState=5;c.gate.observe(c.pose,3,1030);check(!c.get(1030),"post-match player cannot authorize movement");}
    {Case c;c.advance();c.pose.fieldState=8;c.gate.observe(c.pose,3,1030);check(!c.get(1030),"round result field cannot authorize movement");}
    {Case c;c.advance();c.pose.x=5;c.pose.dir=1;c.gate.observe(c.pose,3,1030);check(!c.get(1030),"child outside the field blocks a transient pose");}
    {Case c;c.advance();c.pose.y=-1;c.gate.observe(c.pose,3,1030);check(!c.get(1030),"axis below the floor cannot become an engine pose");}
    {Case c;c.advance();c.pose.y=13;c.pose.dir=0;c.gate.observe(c.pose,3,1030);check(!c.get(1030),"child above row fourteen is not a control observation");}
    {Case c;c.advance();check(!c.gate.current(c.binding.context,3,1,21,{2,5},1017),"different consumer pair index rejected");check(!c.gate.current(c.binding.context,3,1,20,{5,2},1017),"swapped current colours rejected");}
    {Case c;c.advance();check(!c.gate.current(c.binding.context,3,0,20,{2,5},1017),"foreign seat rejected");check(!c.gate.current(c.binding.context,4,1,20,{2,5},1017),"foreign proof generation rejected");}
    {Case c;c.advance();auto context=c.binding.context;++context.process.creationTime;check(!c.gate.current(context,3,1,20,{2,5},1017),"reused PID belongs to a different process lifetime");++c.pose.owner.context.process.creationTime;check(!c.gate.observe(c.pose,3,1030),"foreign process observation clears binding");}
    {Case c;c.advance();c.pose.owner.player+=4;check(!c.gate.observe(c.pose,3,1030),"another retained player cannot inherit the binding");}
    {Case c;c.advance();auto context=c.binding.context;context.discoveryOnly=true;check(!c.gate.current(context,3,1,20,{2,5},1017),"discovery-only consumer scope rejected");c.binding.context.discoveryOnly=true;check(!c.gate.bind(c.binding),"discovery evidence alone cannot establish a control binding");}
    std::printf("owned pose gate: %d checks, %d failures\n",checks,failed);return failed?1:0;
}
