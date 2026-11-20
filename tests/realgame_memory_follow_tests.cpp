#include "../play/memory/scan14.h"
#include "../play/memory/follow14_win.h"
#include "../play/memory/service14.h"
#include "../play/memory/regionplan14.h"
#include <cstdio>
#include <map>
using namespace play::realgame;
using Bytes=std::array<uint8_t,440>;
static int failures=0,checks=0;
static void check(bool pass,const char* name) {++checks;if(!pass){++failures;std::printf("FAIL %s\n",name);}}
static Bytes encode(const std::string& rows) {
    Bytes b{};
    for(int y=0;y<14;++y)for(int x=0;x<6;++x) {
        const uint32_t v=(uint32_t(9+y*8+x)<<16)|uint32_t(rows[size_t(y*7+x)]-'0');
        std::memcpy(b.data()+y*32+x*4,&v,4);
    }return b;
}
static FieldContext ctx() {
    FieldContext c;
    c.process.pid=67120;c.process.creationTime=100;c.process.imageBase=0x7ff697e00000;
    c.process.image.ok=true;c.process.image.machine=0x8664;c.process.image.timeStamp=0x66a8c684;c.process.image.imageSize=0x6fd000;
    c.round=1;c.roundKnown=true;return c;
}
struct Fixture {
    FieldContext c=ctx();
    FieldFollower14 follower;
    std::array<uint64_t,2> tops{0x10000,0x20000};
    // Saved live observations, including the independently checked 1P hidden cells.
    std::map<uint64_t,Bytes> memory{{tops[0]-64,encode("500000/200000/400000/320000/430000/240000/230000/250000/430000/430000/520000/550000/320000/225000")},
        {tops[1]-64,encode("000000/000000/000005/000002/000042/000055/000052/000045/000044/000035/000025/405235/305322/602332")}};
    int reads=0;
    FieldRead14 read=[this](uint64_t a,void* into,size_t n){
        ++reads;const auto it=memory.find(a);if(it==memory.end() || n!=440)return false;
        std::memcpy(into,it->second.data(),n);return true;
    };
    Fixture(){follower.synchronize(c);install();}
    void install(std::vector<uint64_t> addresses={}) {
        if(addresses.empty()) addresses={tops[0],tops[1]};
        check(follower.install({c,follower.generation(),addresses,true}),"install catalog");
    }
    VisiblePair14 evidence(uint64_t seq) {
        VisiblePair14 e;e.context=c;e.sequence=seq;e.observedMs=seq*10;e.settled=true;
        for(size_t s=0;s<2;++s){juraic::GameField field;decodeField14(memory.at(tops[s]-64).data(),440,field);
            for(int y=0;y<12;++y)for(int x=0;x<6;++x)e.cells[s][size_t(y*6+x)]=int8_t(field.f.get(x,y));}
        return e;
    }
    std::optional<Follow14Result> observe(uint64_t seq){return follower.observe(evidence(seq),seq*10,read);}
    // Synthetic local fixture change only; no process API is used by this test.
    void change(int side){auto& b=memory.at(tops[size_t(side)]-64);b[11*32+4]=b[11*32+4]==1?2:1;}
    std::optional<Follow14Result> ready(){observe(1);change(0);change(1);return observe(2);}
};
int main() {
    {
        Fixture f;auto ready=f.ready();check(bool(ready),"ready before transient landing");
        auto e=f.evidence(3);int n=0;std::vector<uint64_t> order;
        const auto torn=[&](uint64_t a,void* into,size_t size){
            order.push_back(a);if(!f.read(a,into,size))return false;
            if(++n==3)static_cast<uint8_t*>(into)[32]^=1;return true;
        };
        check(!f.follower.observe(e,30,torn) && f.follower.state()==Follow14State::NeedEvidence,
              "landing between passes suspends output without discarding addresses");
        check(!f.follower.current(*ready,30),"transient landing revokes previous output");
        check(order==std::vector<uint64_t>{f.tops[0]-64,f.tops[1]-64,f.tops[0]-64,f.tops[1]-64},
              "second pass checks both seats after first pass, without duplicate pair reads");
        check(bool(f.observe(4)),"stable frame recovers retained binding after transient landing");
        int batches=0,scalarReads=0;
        const FieldReadPass14 batch=[&](std::span<const uint64_t> addresses,std::span<Field14Bytes> bytes){
            ++batches;for(size_t i=0;i<addresses.size();++i)if(!f.read(addresses[i]-64,bytes[i].data(),bytes[i].size()))return false;
            return batches!=2;
        };
        check(!f.follower.observe(f.evidence(5),50,[&](uint64_t,void*,size_t){++scalarReads;return false;},batch)
              && batches==2 && scalarReads==0 && f.follower.state()==Follow14State::NeedScan,
              "failed second batch never publishes a partial pair");
    }
    {
        auto c=ctx();MemoryRegion arena{0x100000000,uint64_t(500)<<20,0x100000000,REGION_COMMITTED,PAGE_READ_WRITE,REGION_PRIVATE};
        MemoryRegion image{c.process.imageBase+0x400000,4096,c.process.imageBase,REGION_COMMITTED,PAGE_READ_WRITE,REGION_IMAGE};
        auto unrelated=arena;unrelated.base+=uint64_t(1)<<30;unrelated.size=uint64_t(1)<<30;
        const std::vector<MemoryRegion> all{arena,image,unrelated};
        auto p=planFieldRegions14(c.process,all);check(p.profiled && p.regions.size()==2,"prioritize observed arena and keep image decoy");
        auto header=all;header[0].base+=0x3000;header[0].size-=0x3000;
        check(planFieldRegions14(c.process,header).profiled,"allocator header pages do not defeat observed arena profile");
        auto trailing=all;trailing[0].size+=4096;
        check(planFieldRegions14(c.process,trailing).profiled,"measured 500MiB plus 4KiB arena uses priority scan");
        check(!planFieldRegions14(c.process,all,true).profiled && planFieldRegions14(c.process,all,true).regions.size()==3,"unmatched profile expands to all data");
        auto duplicate=arena;duplicate.base+=uint64_t(2)<<30;auto ambiguous=all;ambiguous.push_back(duplicate);
        check(!planFieldRegions14(c.process,ambiguous).profiled,"ambiguous arena shape does not choose first region");
        c.process.image.timeStamp++;check(!planFieldRegions14(c.process,all).profiled,"unknown version has no arena heuristic");
    }
    {
        Fixture f;auto ready=f.ready();check(bool(ready),"ready before render delay");
        auto oldPicture=f.evidence(3);f.change(0);f.change(1);
        check(!f.follower.observe(oldPicture,30,f.read) && f.follower.state()==Follow14State::NeedEvidence && !f.follower.current(*ready,30),"render delay revokes output without throwing away census");
        check(bool(f.observe(4)),"fresh rendered picture recovers without full scan");
        oldPicture=f.evidence(5);f.change(0);f.follower.observe(oldPicture,50,f.read);
        oldPicture.sequence=6;oldPicture.observedMs=850;
        check(!f.follower.observe(oldPicture,850,f.read) && f.follower.state()==Follow14State::NeedScan,"persistent mismatch requests new scan");
    }
    {
        Fixture f;f.observe(1);auto animation=f.evidence(2);animation.settled=false;animation.cells[0].fill(-1);
        check(!f.follower.observe(animation,20,f.read),"animation suspends output");
        f.change(0);f.change(1);check(bool(f.observe(3)),"stable changes after animation still confirm same addresses");
    }
    {
        Fixture f;check(!f.observe(1) && f.follower.state()==Follow14State::Confirming,"one picture cannot bind");
        check(!f.observe(2),"repeated static picture cannot bind");f.change(0);
        check(!f.observe(3),"both seats must change");f.change(1);auto r=f.observe(4);
        check(bool(r) && r->binding.visibleTop==f.tops,"distinct changes bind both seats");
        check(r && r->fields[0].f.get(0,12)==2 && r->fields[0].row14[0]==5,"preserve hidden green/purple");
        check(r && f.follower.current(*r,40) && !f.follower.current(*r,1000),"result expires without new evidence");
        check(!f.observe(4),"replayed evidence revokes readiness");
    }
    {
        Fixture f;check(bool(f.ready()),"ready before round change");auto old=f.c;const auto oldGeneration=f.follower.generation();
        ++f.c.round;f.follower.synchronize(f.c);const int before=f.reads;
        check(f.follower.state()==Follow14State::NeedScan && !f.observe(3) && before==f.reads,"round change refuses retained valid old grids");
        check(!f.follower.install({old,oldGeneration,{f.tops[0],f.tops[1]},true}),"late old scan cannot rebind");
        // Old round copies stay valid in local memory. New round grids have different colors and addresses.
        const auto stale=f.tops;f.tops={0x30000,0x40000};
        for(size_t s=0;s<2;++s)f.memory[f.tops[s]-64]=f.memory.at(stale[s]-64);
        f.change(0);f.change(1);f.install({stale[0],stale[1],f.tops[0],f.tops[1]});
        check(!f.observe(3),"relocated round first evidence");f.change(0);f.change(1);
        for(size_t s=0;s<2;++s)f.memory[f.tops[s]-64][11*32+4]=f.memory.at(stale[s]-64)[11*32+4];
        // These changes return to the retained copies, which must make the result ambiguous.
        check(!f.observe(4) && f.follower.state()==Follow14State::Ambiguous,"old copy reappearing in matches is not pruned");
        f.memory[f.tops[0]-64][11*32+4]=3;f.memory[f.tops[1]-64][11*32+4]=4;
        check(!f.observe(5),"ambiguity clears confirmation");f.change(0);f.change(1);
        f.memory[f.tops[0]-64][11*32+4]=4;f.memory[f.tops[1]-64][11*32+4]=3;
        auto r=f.observe(6);check(r && r->binding.visibleTop==f.tops && r->binding.roundEpoch==2,"rebind relocated round");
    }
    {
        Fixture f;check(bool(f.ready()),"ready before PID reuse");const auto generation=f.follower.generation();
        ++f.c.process.creationTime;f.follower.synchronize(f.c);
        check(f.follower.generation()!=generation && !f.observe(3),"PID reuse invalidates addresses");
        f.install();check(!f.observe(3),"restart needs new evidence");f.change(0);f.change(1);
        check(bool(f.observe(4)),"restart rebind after confirmation");
        f.c.process.imageBase+=0x10000;f.follower.synchronize(f.c);check(!f.observe(5),"ASLR invalidates scan");
        f.c.process.image.machine=0x14c;f.follower.synchronize(f.c);
        check(f.follower.state()==Follow14State::Unsupported && !f.observe(6),"unknown image never reads");
        f.c.process.pid=0;f.follower.synchronize(f.c);check(f.follower.state()==Follow14State::NoProcess,"process loss clears binding");
    }
    {
        Fixture f;f.c.roundKnown=false;f.follower.synchronize(f.c);const int n=f.reads;
        check(f.follower.state()==Follow14State::RoundUnknown && !f.observe(1) && n==f.reads,"no round signal no publication");
    }
    {
        Fixture f;f.memory[0x30000-64]=f.memory.at(f.tops[0]-64);f.install({f.tops[0],f.tops[1],0x30000,0x30000});
        check(!f.observe(1) && f.follower.state()==Follow14State::Ambiguous,"duplicate matching retained copy blocks assignment");
        f.memory.erase(0x30000-64);
        check(!f.observe(2) && f.follower.state()==Follow14State::NeedScan,"missing decoy forces rescan");
    }
    {
        Fixture f;check(bool(f.ready()),"ready before stale evidence");auto e=f.evidence(3);
        check(!f.follower.observe(e,1000,f.read) && f.follower.state()==Follow14State::NeedEvidence,"stale evidence refuses output");
        e=f.evidence(4);e.cells[1].fill(-1);check(!f.follower.observe(e,40,f.read),"unknown visible cells are not empty");
        e=f.evidence(5);e.cells[1].fill(0);check(!f.follower.observe(e,50,f.read),"empty board waits for evidence");
        e=f.evidence(6);e.cells[0][0]=99;check(!f.follower.observe(e,60,f.read),"invalid visible color rejected");
        e=f.evidence(7);e.context.round++;check(!f.follower.observe(e,70,f.read),"other-round evidence rejected");
        e=f.evidence(8);e.settled=false;check(!f.follower.observe(e,80,f.read),"clear animation not evidence");
        e=f.evidence(9);check(!f.follower.observe(e,80,f.read),"future timestamp refused");
    }
    {
        Fixture f;f.observe(1);auto known=f.evidence(2);f.memory.at(f.tops[1]-64)[0]=99;
        check(!f.follower.observe(known,20,f.read) && f.follower.state()==Follow14State::NeedScan,"unknown hidden cell rejects pair");
        f.memory.at(f.tops[1]-64)[0]=0;
        f.install();int n=0;const auto torn=[&](uint64_t a,void* into,size_t size){if(!f.read(a,into,size))return false;if(++n==2)static_cast<uint8_t*>(into)[32]^=1;return true;};
        check(!f.follower.observe(f.evidence(3),30,torn),"changing candidate refuses pair");
        check(!f.follower.install({f.c,f.follower.generation(),{f.tops[0],f.tops[1]},false}),"partial scan not installable");
    }
    {
        Fixture f;
        // The field straddles a 64KiB chunk, and then a region boundary.
        const uint64_t base=0x80000;std::vector<uint8_t> data(131072);
        const size_t offset=65536-220;std::copy(f.memory.begin()->second.begin(),f.memory.begin()->second.end(),data.begin()+offset);
        const auto read=[&](uint64_t a,void* out,size_t size){if(a<base || a-base>data.size() || size>data.size()-size_t(a-base))return false;std::memcpy(out,data.data()+size_t(a-base),size);return true;};
        const MemoryRegion all{base,data.size(),base,REGION_COMMITTED,PAGE_READ_WRITE,REGION_PRIVATE};
        FieldScan14 scan(f.c,f.follower.generation(),{all});
        check(!scan.catalog().complete,"unfinished scan not installable");while(!scan.done())scan.step(read);
        check(scan.catalog().complete && scan.catalog().visibleTop==std::vector<uint64_t>{base+offset+64},"find crossing chunk boundary");
        auto a=all,b=all;a.size=65536;b.base+=65536;b.size=65536;
        FieldScan14 split(f.c,1,{a,b});while(!split.done())split.step(read);
        check(split.catalog().complete && split.catalog().visibleTop==scan.catalog().visibleTop,"find crossing adjacent region boundary");
        {
            // The scan also collects where a class mark stands, when asked: inside a chunk, across two chunks, not at
            // an address that is no fourth byte; without being asked, nothing.
            auto marked=data;const uint64_t mark=0x1406fc500ull;
            for(const size_t at:{size_t(4096),size_t(8188),size_t(70001)})std::memcpy(marked.data()+at,&mark,8);
            const auto readMarked=[&](uint64_t a,void* out,size_t size){if(a<base || a-base>marked.size() || size>marked.size()-size_t(a-base))return false;std::memcpy(out,marked.data()+size_t(a-base),size);return true;};
            FieldScan14 with(f.c,1,{all},uint64_t(2)<<30,4096,8192);with.findAlso(mark);while(!with.done())with.step(readMarked);
            check(with.catalog().complete && with.catalog().visibleTop==scan.catalog().visibleTop && with.marks()==std::vector<uint64_t>{base+4096,base+8188},"class marks found beside the boards, across a chunk boundary too");
            FieldScan14 without(f.c,1,{all});while(!without.done())without.step(readMarked);
            check(without.catalog().complete && without.marks().empty(),"no class marks unless asked for");
        }
        auto exec=all;exec.protect=0x40;FieldScan14 noCode(f.c,1,{exec});
        check(noCode.done() && noCode.plannedBytes()==0,"executable data excluded");
        FieldScan14 budget(f.c,1,{all},100);check(budget.failed() && !budget.catalog().complete,"byte budget truncation fails closed");
        FieldScan14 broken(f.c,1,{all});broken.step([](uint64_t,void*,size_t){return false;});
        check(broken.failed() && !broken.catalog().complete,"unreadable page not skipped silently");
        FieldScan14 overflow(f.c,1,{all},uint64_t(2)<<30,0);while(!overflow.done())overflow.step(read);
        check(overflow.failed() && !overflow.catalog().complete,"candidate overflow fails closed");
        std::set<uint64_t> empty;const auto zeros=encode("000000/000000/000000/000000/000000/000000/000000/000000/000000/000000/000000/000000/000000/000000");
        check(findFields14(base,zeros.data(),zeros.size(),empty) && empty.size()==1,"empty typed field remains a candidate");
        check(findFields14(base,zeros.data(),zeros.size(),empty) && empty.size()==1,"overlapping scans deduplicate addresses");
        data.assign(2u<<20,0);const size_t largeOffset=(1u<<20)-220;
        std::copy(f.memory.begin()->second.begin(),f.memory.begin()->second.end(),data.begin()+largeOffset);
        auto large=all;large.size=data.size();FieldScan14 largeScan(f.c,1,{large},uint64_t(4)<<30,4096,1u<<20);
        while(!largeScan.done())largeScan.step(read);
        check(largeScan.catalog().complete && largeScan.catalog().visibleTop==std::vector<uint64_t>{base+largeOffset+64},"1MiB live scan preserves chunk boundary field");
    }
    {
        Fixture f;FieldFollowService14 service;
        const auto regions=[&](){std::vector<MemoryRegion> r;for(const auto& [address,bytes]:f.memory)
            r.push_back({address,bytes.size(),address,REGION_COMMITTED,PAGE_READ_WRITE,REGION_PRIVATE});return r;};
        auto e=f.evidence(1);check(!service.tick(f.c,&e,10,regions,f.read) && service.scanning(),"service yields after one scan chunk");
        e=f.evidence(2);check(!service.tick(f.c,&e,20,regions,f.read) && !service.scanning() && service.state()==Follow14State::Confirming,"service installs completed census");
        f.change(0);f.change(1);e=f.evidence(3);auto result=service.tick(f.c,&e,30,regions,f.read);
        check(result && service.current(*result,30),"service publishes verified pair");
        ++f.c.round;e=f.evidence(4);check(!service.tick(f.c,&e,40,regions,f.read) && service.scanning(),"service restarts scan on round event");
        check(result && !service.current(*result,40),"old result revoked immediately on round event");
        ++f.c.process.creationTime;e=f.evidence(5);
        check(!service.tick(f.c,&e,50,regions,f.read) && service.scanning(),"service cancels partial scan at restart");
        f.c.roundKnown=false;e=f.evidence(6);const auto n=f.reads;
        check(!service.tick(f.c,&e,60,regions,f.read) && !service.scanning() && f.reads==n,"service stops scanning on uncertain lifecycle");
    }
    std::printf("address following: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
