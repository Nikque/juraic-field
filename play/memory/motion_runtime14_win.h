#pragma once
#include "motion_sample14.h"
#include "../../core/diagnostic_trace.h"
#include "motion_clock14.h"
#include "pagequery14_win.h"
#include <condition_variable>
#include <mutex>
#include <thread>
namespace play::realgame {
struct MotionBinding14 {
    FieldContext context;
    uint64_t generation=0;
    int64_t roundStart=-1;
    std::array<uint64_t,2> players{},fields{};
};
// The current-state poller never performs a heap scan. A separate board worker
// supplies its proved roots, so a slow scan cannot hold up pose updates.
class MemoryMotionRuntime14 {
    mutable std::mutex mutex_;std::condition_variable wake_;std::thread thread_;
    bool stop_=true;uint64_t revision_=0;MotionBinding14 binding_;
    std::optional<MemoryMotionSample14> sample_;
    static uint64_t clockMs(){LARGE_INTEGER t{},f{};QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);
        return uint64_t(t.QuadPart/f.QuadPart)*1000+uint64_t(t.QuadPart%f.QuadPart)*1000/uint64_t(f.QuadPart);}
    void run(){
        PageQueryReader14 reader;uint64_t revision=UINT64_MAX;MotionBinding14 binding;
        std::array<OwnedPoseGate14,2> gates;std::array<bool,2> bound{};
        std::array<MotionClock14,2> clocks;
        for(;;){
            bool changed=false;
            {std::unique_lock<std::mutex> hold(mutex_);
                wake_.wait_for(hold,std::chrono::milliseconds(bound[0] || bound[1]?4:100),[&]{return stop_ || revision_!=revision;});
                if(stop_)return;
                if(revision_!=revision){revision=revision_;binding=binding_;changed=true;}
            }
            if(changed){gates={};bound={};clocks={};reader.close();std::wstring why;
                    if(binding.context.roundKnown && !binding.context.discoveryOnly && binding.roundStart>=0 &&
                       binding.generation && reader.open(binding.context.process,why)){
                        for(size_t s=0;s<2;++s)if(binding.players[s] && binding.fields[s])
                            bound[s]=gates[s].bind({binding.context,binding.generation,binding.players[s],binding.fields[s],int(s)});
                    }
            }
            std::optional<MemoryMotionSample14> result;
            if(bound[0] || bound[1]){
                MemoryMotionSample14 m;m.context=binding.context;m.generation=binding.generation;m.roundStart=binding.roundStart;
                for(size_t seat=0;seat<2;++seat)if(bound[seat]){
                    m.readBegin100ns[seat]=juraic::inputdiag::clock();
                    auto p=readOwnedPublicPose14(binding.context,binding.players[seat],[&](uint64_t a,void* b,size_t n){return reader.checked(a,b,n);});
                    m.readEnd100ns[seat]=juraic::inputdiag::clock();
                    const auto at=clockMs();
                    if(p)m.nativeTick[seat]=p->count108;
                    if(!p)gates[seat].unreadable();
                    else if(!gates[seat].observe(*p,binding.generation,at))bound[seat]=false;
                    m.bound[seat]=bound[seat];
                    if(p && p->lockedPairs<UINT32_MAX)m.pairs[seat]=p->lockedPairs+1;
                    if(p)m.poseFrame[seat]=clocks[seat].observe(p->count108,at);
                    m.running[seat]=gates[seat].running(binding.context,binding.generation,int(seat),at);
                    m.advancing[seat]=gates[seat].advancing(binding.context,binding.generation,int(seat),at);
                    m.pose[seat]=gates[seat].settled(binding.context,binding.generation,int(seat),at);
                    if(int(seat)==juraic::inputdiag::trace.seat.load())juraic::inputdiag::trace.emit(juraic::inputdiag::NATIVE,{
                        int64_t(seat),int64_t(binding.generation),binding.roundStart,m.readBegin100ns[seat],m.readEnd100ns[seat],
                        p?int64_t(p->count108):-1,m.poseFrame[seat],m.pairs[seat],p?p->x:-1,p?p->y:-1,p?int64_t(p->upper):-1,p?p->dir:-1,
                        p?int64_t(p->operating):-1,p?int64_t(p->horizontalMoving):-1,m.bound[seat],m.advancing[seat],m.running[seat],bool(m.pose[seat])});
                }
                m.observedMs=clockMs();m.frame=int64_t(m.observedMs*60/1000);result=std::move(m);
            }
            {std::lock_guard<std::mutex> hold(mutex_);if(revision==revision_)sample_=std::move(result);}
        }
    }
public:
    ~MemoryMotionRuntime14(){stop();}
    void start(){stop();{std::lock_guard<std::mutex> hold(mutex_);stop_=false;binding_={};sample_.reset();++revision_;}
        thread_=std::thread([this]{try{run();}catch(...){std::lock_guard<std::mutex> hold(mutex_);sample_.reset();}});}
    void stop(){{std::lock_guard<std::mutex> hold(mutex_);stop_=true;sample_.reset();}wake_.notify_all();if(thread_.joinable())thread_.join();}
    void configure(const MotionBinding14& b){
        {std::lock_guard<std::mutex> hold(mutex_);
            if(sameFieldContext(binding_.context,b.context) && binding_.generation==b.generation &&
               binding_.players==b.players && binding_.fields==b.fields && binding_.roundStart==b.roundStart)return;
            binding_=b;++revision_;sample_.reset();}
        wake_.notify_one();
    }
    std::optional<MemoryMotionSample14> sample()const{std::lock_guard<std::mutex> hold(mutex_);return sample_;}
};
}
