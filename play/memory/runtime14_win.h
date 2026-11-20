#pragma once
#include "datareader14_win.h"
#include "lifecycle14.h"
#include "regionplan14.h"
#include "sample14.h"
#include "motion_sample14.h"
#include "motion_runtime14_win.h"
#include "pagequery14_win.h"
#include "service14.h"
#include "worker_diag14.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <tuple>
namespace play::realgame {
// The pad thread never waits for a scan, VirtualQueryEx, ReadProcessMemory, or the worker to finish.
// Only small records cross the mutex; all process access and lifecycle proof stay on this worker.
class MemoryRuntime14 {
public:
    struct Status {Follow14State state=Follow14State::NoProcess;std::string reason="not_started";uint64_t scans=0,accepted=0,bytes=0,total=0;};
    ~MemoryRuntime14(){stop();}
    void start(std::wstring program,uint32_t pid){
        stop();{std::lock_guard<std::mutex> hold(lock_);stop_=false;program_=std::move(program);pid_=pid;status_={};}
        motionWorker_.start();
        worker_=std::thread([this]{
            try{run();}
            catch(...){motionWorker_.configure({});std::lock_guard<std::mutex> hold(lock_);sample_.reset();status_.state=Follow14State::NoProcess;status_.reason="worker_failed";}
        });
    }
    void stop(){
        {std::lock_guard<std::mutex> hold(lock_);stop_=true;sample_.reset();queue_.clear();}
        wake_.notify_all();if(worker_.joinable())worker_.join();
        motionWorker_.stop();
        std::lock_guard<std::mutex> hold(lock_);status_={};
    }
    void enabled(bool on){enabled_=on;motionWorker_.configure({});{std::lock_guard<std::mutex> hold(lock_);sample_.reset();}wake_.notify_all();}
    void submit(const Reading& reading,int64_t frame){
        std::lock_guard<std::mutex> hold(lock_);if(stop_)return;
        if(queue_.size()>=600){queue_.clear();sample_.reset();motionWorker_.configure({});reset_=true;}
        queue_.push_back({reading,frame});wake_.notify_one();
    }
    std::optional<MemorySample14> sample() const {std::lock_guard<std::mutex> hold(lock_);return sample_;}
    std::optional<MemoryMotionSample14> motion() const {return motionWorker_.sample();}
    Status status() const {std::lock_guard<std::mutex> hold(lock_);return status_;}
    static uint64_t clockMs(){LARGE_INTEGER t{},f{};QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);
        return uint64_t(t.QuadPart/f.QuadPart)*1000+uint64_t(t.QuadPart%f.QuadPart)*1000/uint64_t(f.QuadPart);}
private:
    struct Frame {Reading reading;int64_t frame;};
    mutable std::mutex lock_;std::condition_variable wake_;std::thread worker_;
    bool stop_=true,reset_=false;std::atomic<bool> enabled_{true};std::wstring program_;uint32_t pid_=0;
    std::deque<Frame> queue_;std::optional<MemorySample14> sample_;MemoryMotionRuntime14 motionWorker_;Status status_;
    void run(){
        ProcessMemory memory;FieldDataReader14 data(memory);PageQueryReader14 fast;FieldProcess current;FieldLifecycle14 life;
        std::array<uint64_t,2> players{},fields{};uint64_t poseGeneration=0;
        Follow14Config cfg;cfg.scanChunkBytes=16u<<20;cfg.maxScanBytes=uint64_t(4)<<30;
        FieldFollowService14 service(cfg);uint64_t pollAt=0,lastSequence=0,scans=0,accepted=0;
        FieldContext scope{};bool haveScope=false,full=false,profile=false;size_t completedProfiles=0;
        std::array<std::tuple<uint64_t,int64_t,uint64_t,int,int,uint32_t,uint32_t>,2> nextReadDiagnostic{};
        std::array<int64_t,15> workerDiagnostic{};bool haveWorkerDiagnostic=false;
        for(;;){
            std::deque<Frame> frames;bool reset=false;
            {std::unique_lock<std::mutex> hold(lock_);
                if(!service.scanning())wake_.wait_for(hold,std::chrono::milliseconds(100),[&]{return stop_ || reset_ || !queue_.empty();});
                if(stop_)return;frames.swap(queue_);reset=std::exchange(reset_,false);
            }
            auto now=clockMs();
            if(reset){life=FieldLifecycle14{};life.process(current);service=FieldFollowService14(cfg);lastSequence=0;players={};fields={};++poseGeneration;motionWorker_.configure({});}
            if(now>=pollAt || (!enabled_ && current.pid)){
                pollAt=now+500;FieldProcess fresh;std::wstring why;
                if(enabled_ && openFieldProcess14(program_,memory,fresh,why) && fresh.pid!=pid_){fresh={};memory.close();}
                if(!enabled_){fresh={};memory.close();}
                if(!sameFieldContext({current,0,false},{fresh,0,false})){
                    current=fresh;life.process(current);service.synchronize(life.visible(now).context);lastSequence=0;
                    players={};fields={};++poseGeneration;motionWorker_.configure({});fast.close();if(current.pid)fast.open(current,why);
                    std::lock_guard<std::mutex> hold(lock_);sample_.reset();
                }
            }
            for(const auto& f:frames)life.step(f.reading,f.frame,std::min(uint64_t(f.frame)*1000/60,clockMs()));
            auto visible=life.visible(clockMs());const bool image=visible.sequence!=lastSequence;
            if(!haveScope || !sameFieldContext(scope,visible.context)){
                scope=visible.context;haveScope=true;full=false;completedProfiles=0;
                players={};fields={};++poseGeneration;motionWorker_.configure({});
            }
            auto pair=service.tick(visible.context,image?&visible:nullptr,clockMs(),[&]{
                auto plan=planFieldRegions14(current,memory.regions(),full);profile=plan.profiled;return plan.regions;
            },[&](uint64_t a,void* b,size_t n){return data.checked(a,b,n);},
              [&](std::span<const uint64_t> a,std::span<Field14Bytes> b){return data.pass(a,b);});
            if(image)lastSequence=visible.sequence;
            if(service.scanSerial()!=scans){scans=service.scanSerial();if(profile && service.lastCatalog().complete)++completedProfiles;}
            if(service.state()==Follow14State::NeedScan && std::string(service.reason())=="visible_mismatch" && completedProfiles>=2)full=true;
            DWORD exit=0;const bool alive=memory.handle && GetExitCodeProcess(memory.handle,&exit) && exit==STILL_ACTIVE;
            // A board/seat association is established by the existing temporal
            // proof, not by heap proximity alone. Once bound, pose failures
            // suspend output without restarting the board scan.
            const FieldRead14 poseRead=[&](uint64_t a,void* b,size_t n){return fast.checked(a,b,n);};
            if(pair && alive && enabled_ && !scope.discoveryOnly && fast.granted()==PageQueryReader14::RIGHTS){
                for(size_t seat=0;seat<2;++seat)if(!players[seat]){
                    const auto owners=discoverOwnerCandidates14(scope,pair->binding.visibleTop[seat],poseRead);
                    std::vector<OwnedPublicPose14> candidates;
                    for(const auto& owner:owners){auto p=readOwnedPublicPose14(scope,owner.player,poseRead);
                        if(p && p->seat==int(seat) && p->owner.field==owner.field)candidates.push_back(*p);}
                    if(candidates.size()==1){const auto& p=candidates[0];
                        players[seat]=p.owner.player;fields[seat]=p.owner.field;
                    }
                }
            }
            if(alive && enabled_ && scope.roundKnown && !scope.discoveryOnly)
                motionWorker_.configure({scope,poseGeneration,life.tracker().roundStart,players,fields});
            else motionWorker_.configure({});
            std::array<std::optional<PublishedNextVerified14>,2> publicNext;
            const auto nextMotion=(pair && alive && enabled_ && service.current(*pair,clockMs()))?motionWorker_.sample():std::nullopt;
            for(int seat=0;seat<2;++seat){
                PublishedNextAudit14 audit;
                auto& trace=juraic::inputdiag::trace;
                if(nextMotion)publicNext[size_t(seat)]=readPublishedNext14(*nextMotion,life.tracker(),seat,clockMs(),current.pid,poseRead,trace.active()?&audit:nullptr);
                if(trace.active()){
                    const auto& t=life.tracker();const auto gen=nextMotion?nextMotion->generation:0;
                    auto key=std::tuple{trace.session(),t.roundStart,gen,t.side[seat].pairs,audit.stage,audit.beforeReject,current.pid};
                    if(key!=nextReadDiagnostic[size_t(seat)]){
                        nextReadDiagnostic[size_t(seat)]=key;const auto& n=publicNext[size_t(seat)];
                        trace.emit(juraic::inputdiag::NEXT_READ,{seat,t.roundStart,int64_t(gen),t.side[seat].pairs,t.frame,audit.stage,
                            audit.beforeReject,n?n->frame:-1,n?int64_t(n->observedMs):0,n?n->tick:0,current.pid});
                    }
                }
            }
            {
            std::lock_guard<std::mutex> hold(lock_);
            // Revoke even if no new picture arrived; expiry is checked again by the consumer.
            if(!enabled_ || !alive || service.state()!=Follow14State::Ready)sample_.reset();
            if(pair && alive && enabled_ && service.current(*pair,clockMs())){
                MemorySample14 s;s.value=*pair;s.frame=life.tracker().frame;s.roundStart=life.tracker().roundStart;
                for(size_t i=0;i<2;++i){s.pairs[i]=life.tracker().side[i].pairs;s.unseen[i]=life.tracker().side[i].unseen;}
                s.next=std::move(publicNext);
                sample_=std::move(s);++accepted;
            }
            status_={service.state(),service.reason(),scans,accepted,service.scanBytes(),service.scanTotal()};
            }
            auto& trace=juraic::inputdiag::trace;
            if(trace.active()){
                const auto& t=life.tracker();const auto q=visibleQuality14(visible,cfg);
                const int state=int(service.state()),reason=workerReason14(service.reason());
                const std::array<int64_t,15> key{int64_t(trace.session()),current.pid,int64_t(visible.context.round),visible.context.roundKnown,
                    t.fromStart,t.rounds,t.roundStart,t.side[0].pairs,t.side[1].pairs,state,reason,service.scanning(),int64_t(scans),visible.settled,q.sufficient};
                if(!haveWorkerDiagnostic||key!=workerDiagnostic){workerDiagnostic=key;haveWorkerDiagnostic=true;
                    trace.emit(juraic::inputdiag::MEMORY_WORKER,{current.pid,int64_t(visible.context.round),visible.context.roundKnown,t.fromStart,
                        t.rounds,t.roundStart,t.frame,t.side[0].pairs,t.side[1].pairs,state,reason,service.scanning(),int64_t(scans),
                        int64_t(service.scanBytes()),int64_t(service.scanTotal()),int64_t(service.lastCatalog().visibleTop.size()),
                        visible.settled,q.sufficient,q.known[0],q.pieces[0],q.colors[0],q.known[1],q.pieces[1],q.colors[1],
                        int64_t(visible.observedMs),int64_t(clockMs()),int64_t(visible.sequence)});
                }
            }
        }
    }
};
} // namespace play::realgame
