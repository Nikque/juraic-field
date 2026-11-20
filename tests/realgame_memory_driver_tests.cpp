#define NOMINMAX
#include <windows.h>
#include "../play/memory/runtime14_win.h"
#include "../play/realgame_drive.h"
#include <cstdio>
using namespace play::realgame;
using namespace juraic;
static constexpr Cell RED=1,GREEN=2,BLUE=3,YELLOW=4,PURPLE=5;
static int checks=0,failures=0;
static void check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
struct Spy : ai::Agent {
    std::vector<ai::Observation> observations;
    void startRound(int,uint64_t) override {}
    uint8_t frame(const ai::Observation& o) override {observations.push_back(o);return 0;}
};
struct Fixture {
    Follower f;std::array<GameField,2> fields;
    Fixture(){
        f.tracker.frame=100;f.tracker.rounds=1;f.tracker.roundStart=0;f.tracker.fromStart=true;
        for(size_t s=0;s<2;++s){
            for(int y=0;y<12;++y)fields[s].f.set(0,y,Cell(1+y%5));
            fields[s].f.set(0,12,GREEN);fields[s].row14[0]=PURPLE;
            auto& t=f.tracker.side[s];t.clear=true;t.inHand=true;t.pairs=1;t.changedAt=80;t.spawnedAt=100;
            t.under=t.resting=Follower::shown(fields[s]);t.colors={RED,BLUE};t.pair.seen=true;t.pair.at=PairControl{};t.lastSeen=t.pair;
            f.model[s].known=true;f.model[s].core.field=t.under;f.model[s].core.field.f.set(0,12,YELLOW);f.model[s].core.field.row14[0]=RED;
            f.model[s].unsure=1;
        }
    }
    MemorySample14 sample(){
        MemorySample14 s;s.value.context.process.pid=77;s.value.context.process.creationTime=42;s.value.context.roundKnown=true;
        s.value.context.round=2;s.value.fields=fields;s.value.observedMs=1000;s.frame=100;s.roundStart=0;s.pairs={1,1};return s;
    }
};
static void testRelearntColours() {
    Tracker tracker;
    Reading reading;
    for (auto& side : reading.side) {
        for (auto& column : side.point)
            for (int k = 0; k < play::realgame::POINTS; ++k) column[k] = pointHeight(k) > 12 ? HIDDEN : EMPTY;
        side.next[0] = {PURPLE, RED};
        side.next[1] = {BLUE, BLUE};
        side.nextRead[0] = side.nextRead[1] = true;
    }
    int64_t frame = 1000;
    for (int i = 0; i < tracker.roundGap + 5; ++i) tracker.step(reading, frame++);
    reading.side[0].next[0] = {BLUE, BLUE};
    reading.side[0].next[1] = {YELLOW, RED};
    for (int i = 0; i < tracker.windowMove + tracker.pairGap; ++i) tracker.step(reading, frame++);
    const Tracker before = tracker;
    reading.side[0].next[1] = {GREEN, RED};
    for (int i = 0; i < 30; ++i) tracker.step(reading, frame++);
    check(before.knows(0, 2) && tracker.knows(0, 2) && tracker.relearnt[0] == 1 &&
              before.pairsLearnt() == tracker.pairsLearnt() && before.knownCount() == tracker.knownCount() &&
              before.aligns == tracker.aligns,
          "stable NEXT changes the colours of an already known pair in place");

    Fixture fixture;
    Driver driver;
    driver.me = 0;
    auto spy = std::make_unique<Spy>();
    auto* observed = spy.get();
    driver.agent = std::move(spy);
    auto own = fixture.f.tracker.side[0];
    own.pairs = 3;
    own.colors = own.pair.colors = own.lastSeen.colors = {YELLOW, RED};
    const auto tick = [&](const Tracker& learned, int64_t at) {
        fixture.f.tracker = learned;
        fixture.f.tracker.frame = at;
        fixture.f.tracker.roundStart = 0;
        fixture.f.tracker.fromStart = true;
        fixture.f.tracker.side[0] = own;
        driver.tick(fixture.f, at);
    };
    tick(before, 100);
    check(!observed->observations.empty() && observed->observations.back().self().colors.axis == YELLOW,
          "driver starts with the original published colours");
    observed->observations.clear();
    tick(tracker, 101);
    check(!observed->observations.empty() && observed->observations.back().self().colors.axis == GREEN,
          "driver updates the in-flight pair after the same NEXT index is relearnt");
}
int main(){
    testRelearntColours();
    Fixture a;auto s=a.sample();check(usableMemorySample14(s,a.f.tracker,1100,77),"fresh current base accepted");
    const auto logged=memoryFieldText14(a.fields[0]);
    check(std::count(logged.begin(),logged.end(),'\n')==14 && logged.substr(0,14)=="500000\n200000\n","memory record retains rows 14 and 13");
    auto bad=s;bad.value.context.process.pid++;check(!usableMemorySample14(bad,a.f.tracker,1100,77),"other process refused");
    bad=s;bad.value.context.discoveryOnly=true;check(!usableMemorySample14(bad,a.f.tracker,1100,77),"diagnostic discovery cannot authorize control even with matching current fields and round origin");
    check(!usableMemorySample14(s,a.f.tracker,1251,77),"sample expires");
    check(!usableMemorySample14(s,a.f.tracker,999,77),"future sample refused");
    bad=s;bad.roundStart++;check(!usableMemorySample14(bad,a.f.tracker,1100,77),"other round refused");
    bad=s;bad.pairs[1]++;check(!usableMemorySample14(bad,a.f.tracker,1100,77),"other own pair refused");
    bad=s;bad.pairs[0]++;check(!usableMemorySample14(bad,a.f.tracker,1100,77),"other opponent pair refused");
    bad=s;bad.unseen[1]++;check(!usableMemorySample14(bad,a.f.tracker,1100,77),"unseen pair mismatch refused");
    bad=s;bad.value.fields[0].f.set(1,0,GREEN);check(!usableMemorySample14(bad,a.f.tracker,1100,77),"changed opponent visible base refused");
    bad=s;bad.frame=101;check(!usableMemorySample14(bad,a.f.tracker,1100,77),"future image refused");
    a.f.tracker.frame=116;check(!usableMemorySample14(s,a.f.tracker,1100,77),"old image refused");a.f.tracker.frame=100;
    a.f.tracker.side[1].resting.f.set(1,0,RED);check(!usableMemorySample14(s,a.f.tracker,1100,77),"landing cannot become current pair base");
    a.f.tracker.side[1].resting=a.f.tracker.side[1].under;
    check(a.f.useMemoryFields(a.fields),"both full bases injected");
    check(a.f.memoryField(1) && a.f.model[1].core.field==a.fields[1] && a.f.model[0].core.field==a.fields[0],"both models keep 14 rows");
    check(a.f.model[0].unsure==0 && a.f.model[1].unsure==0,"confirmed hidden rows clear guesses");
    ai::Observation o;Player own;own.field=a.f.tracker.side[1].under;own.phase=Phase::Control;
    own.field.f.set(0,12,YELLOW);own.field.row14[0]=RED;
    a.f.observeAt(1,Rules{},own,100,o);
    check(o.player[1].field==a.fields[1] && o.player[0].field==a.fields[0],"AI receives full memory fields on both seats");
    auto future=own;future.phase=Phase::Next;future.moves=1;future.field.row14[0]=BLUE;
    a.f.observeAt(1,Rules{},future,101,o);
    check(o.player[1].field==future.field,"earlier memory cannot erase a newly placed hidden piece in the input engine");
    future.phase=Phase::Control;future.moves=2;a.f.observeAt(1,Rules{},future,101,o);
    check(o.player[1].field==future.field,"memory of previous pair cannot become an early future pair's base");
    Driver d;auto spy=std::make_unique<Spy>();auto* seen=spy.get();d.agent=std::move(spy);d.lag=2;
    d.tick(a.f,100);
    check(d.begunField()==a.fields[1],"driver begins with actual hidden rows");
    check(!seen->observations.empty() && seen->observations.front().player[1].field==a.fields[1],"driver agent plans using actual field");
    for(int seat=0;seat<2;++seat){
        a.f.model[seat].core.scoring.allClear=true;
        a.f.observeAt(1,Rules{},future,101,o);check(o.player[seat].scoring.allClear,"own or opponent pending all-clear bonus reaches the AI");
        check(a.f.useMemoryFields(a.fields),"field refresh with a pending all-clear bonus accepted");
        a.f.observeAt(1,Rules{},future,101,o);check(o.player[seat].scoring.allClear,"memory field refresh does not erase a pending all-clear bonus");
        a.f.model[seat].core.scoring.allClear=false;
        a.f.observeAt(1,Rules{},future,101,o);check(!o.player[seat].scoring.allClear,"consumed all-clear bonus is not retained by memory adoption");
    }
    check(d.lag==2,"memory does not pretend pad latency disappeared");
    Fixture left;
    left.fields[0].f.set(1, 0, RED);
    left.f.tracker.side[0].under = left.f.tracker.side[0].resting = Follower::shown(left.fields[0]);
    check(left.f.useMemoryFields(left.fields), "different left and right memory fields accepted");
    Driver leftDriver;
    leftDriver.me = 0;
    auto leftSpy = std::make_unique<Spy>();
    auto* leftSeen = leftSpy.get();
    leftDriver.agent = std::move(leftSpy);
    leftDriver.tick(left.f, 100);
    check(leftDriver.begunField() == left.fields[0] && !(leftDriver.begunField() == left.fields[1]),
          "seat zero driver adopts the left memory field");
    check(!leftSeen->observations.empty() && leftSeen->observations.front().me == 0 &&
              leftSeen->observations.front().self().field == left.fields[0],
          "seat zero AI sees its own memory field");
    a.fields[1].row14[0]=BLUE;check(a.f.useMemoryFields(a.fields),"new hidden-row correction accepted");
    ++a.f.tracker.frame;d.tick(a.f,101);
    check(d.begunField().row14[0]==BLUE,"driver updates old hidden-row history while same pair is in flight");
    a.f.clearMemoryFields();check(!a.f.memoryField(0) && !a.f.memoryField(1),"fallback revokes source flags");
    auto mismatch=a.fields;mismatch[1].f.set(1,0,GREEN);
    const auto before=a.f.model[0].core.field;
    check(!a.f.useMemoryFields(mismatch) && a.f.model[0].core.field==before && !a.f.memoryField(0),"reject both fields transactionally");
    a.f.tracker.side[0].clear=false;check(!a.f.useMemoryFields(a.fields),"animation suspends authority");
    a.f.reset();check(!a.f.memoryField(1) && a.f.memoryUpdates==0,"round reset drops memory authority");
    Fixture landed;landed.fields[1].f.set(1,0,RED);landed.f.tracker.side[1].resting=Follower::shown(landed.fields[1]);
    const auto ownBefore=landed.f.model[1].core.field;
    check(landed.f.useMemoryFields(landed.fields),"both resting fields accepted while one seat has just landed");
    check(landed.f.memoryField(0) && !landed.f.memoryField(1) && landed.f.memoryRestingField(1),"only current-pair base is offered to each driver");
    check(landed.f.model[1].core.field==ownBefore,"landed field never replaces the in-flight pair's base");
    auto wrong=landed.fields;wrong[0].f.set(2,0,RED);
    check(!landed.f.useMemoryFields(wrong) && !landed.f.memoryRestingField(0),"failed revalidation revokes previous authority");
    Fixture hidden;hidden.f.tracker.side[1].pair.childHidden=true;
    check(hidden.f.useMemoryFields(hidden.fields) && hidden.f.memoryRestingField(1) && !hidden.f.memoryField(1),
          "partly hidden pair does not prove an unlocked current-pair base");
    MemoryRuntime14 worker;worker.enabled(false);worker.start(L"",0);
    const auto begin=std::chrono::steady_clock::now();Reading reading;
    for(int i=0;i<650;++i)worker.submit(reading,i);
    const auto duration=std::chrono::steady_clock::now()-begin;
    check(duration<std::chrono::seconds(1),"capture submits without waiting for scans");
    check(!worker.sample(),"disabled worker never publishes");
    check(!worker.motion(),"disabled worker never publishes native motion");worker.stop();
    check(!worker.sample(),"stop revokes sample and joins worker");
    check(!worker.motion(),"stop also revokes and joins the independent motion worker");
    std::printf("memory driver integration: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
