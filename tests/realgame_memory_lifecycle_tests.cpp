#include "../play/memory/lifecycle14.h"
#include <cstdio>
using namespace play::realgame;
int main() {
    int n=0,failed=0;const auto check=[&](bool b,const char* m){++n;if(!b){++failed;std::printf("FAIL %s\n",m);}};
    FieldProcess p;p.pid=7;p.creationTime=42;p.imageBase=0x100000;
    FieldLifecycle14 l;l.process(p);Reading empty;
    for(auto& s:empty.side) {
        for(auto& col:s.point)for(int k=0;k<POINTS;++k)col[k]=pointHeight(k)>12?HIDDEN:juraic::EMPTY;
        s.nextRead[0]=s.nextRead[1]=true;s.next[0]={2,5};s.next[1]={3,4};
    }
    check(!l.visible(0).context.roundKnown,"starts unknown");
    for(int i=0;i<40;++i)l.step(empty,i,uint64_t(i)*1000/60);
    check(l.tracker().fromStart && l.tracker().rounds==1,"empty gap witnesses new round");
    check(!l.visible(650).context.roundKnown,"empty menus alone do not authorize round");
    FieldLifecycle14 discovery(false);discovery.process(p);
    for(int i=0;i<40;++i)discovery.step(empty,i,uint64_t(i)*1000/60);
    check(discovery.visible(650).context.roundKnown && discovery.tracker().roundStart<0,
          "discovery scope can exist without a control frame origin after witnessed empty round");
    check(!l.visible(650).context.roundKnown,"discovery option does not weaken production control lifecycle");
    check(!discovery.visible(3000).context.roundKnown,"discovery scope still expires with stale capture");

    Reading flash=empty;flash.side[0].unsure=1;
    l.step(flash,40,666);l.step(empty,41,683);
    Reading spawn=empty;
    for(auto& s:spawn.side) {s.point[juraic::SPAWN_X][23]=2;s.next[0]={3,4};s.next[1]={1,2};}
    for(int i=42;i<46;++i)l.step(spawn,i,uint64_t(i)*1000/60);
    auto e=l.visible(750);check(e.context.roundKnown,"spawn after witnessed gap identifies round");
    const auto epoch=e.context.round;
    // Tracker itself retains a pair for a while; eventually both empty becomes a gap.
    bool revoked=false;
    for(int i=46;i<200;++i){l.step(empty,i,uint64_t(i)*1000/60);if(!l.visible(uint64_t(i)*1000/60).context.roundKnown)revoked=true;}
    check(revoked,"potential round end revokes old round");
    check(l.epoch()>epoch,"new gap advances epoch");
    l.step(spawn,200,3333);l.step(spawn,201,3350);l.step(spawn,202,3366);
    check(!l.visible(6000).context.roundKnown,"capture timeout revokes round");
    const auto before=l.epoch();l.step(spawn,400,6666);
    check(l.epoch()>before && !l.visible(6666).context.roundKnown,"resumed capture cannot keep unobserved lifecycle");
    const auto restart=l.epoch();++p.creationTime;l.process(p);
    check(l.epoch()>restart && !l.visible(6666).context.roundKnown,"PID reuse resets tracker and epoch");
    l.step(spawn,401,6683);check(!l.visible(6683).context.roundKnown,"mid-round attachment remains unknown");
    check(l.discoveryVisible(6683).context.roundKnown,"fresh mid-round images authorize address discovery only");
    check(l.discoveryVisible(6683).context.discoveryOnly && !l.visible(6683).context.discoveryOnly,"discovery context is explicitly distinct from control context");
    check(!l.discoveryVisible(9000).context.roundKnown,"address discovery cannot reuse stale images");
    FieldLifecycle14 noProcess;noProcess.step(spawn,401,6683);
    check(!noProcess.discoveryVisible(6683).context.roundKnown,"address discovery still requires process identity");
    const auto seq=l.visible(6683).sequence;l.step(spawn,401,6683);
    check(l.visible(6683).sequence==seq,"duplicate capture not fresh evidence");
    std::printf("lifecycle notifications: %d checks, %d failures\n",n,failed);return failed?1:0;
}
