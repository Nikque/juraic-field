#pragma once
// Private diagnostic build only. No process reader, input sender, or game state access.
#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <mutex>
#include <thread>
#include <vector>
namespace juraic::inputdiag {
using Clock=int64_t(*)();
inline std::atomic<Clock> clockSource{nullptr};
inline int64_t clock(){auto f=clockSource.load(std::memory_order_relaxed);return f?f():
    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()/100;}
enum Kind { CONFIG, SOURCE, NATIVE, GATE, PAD, DRIVE, CAL_ROW, CAL_APPLY, CORRECT,
    THINK, CHOSEN, TARGET, DRIVER_TARGET, AGENT_FRAME, DRIVER_START, ROUND, MARKER, NATIVE_GUARD, RECOVERY,
    NEXT_READ, NEXT_CONSUMER, NEXT_OBSERVATION, WINDOW_WATCH, MEMORY_WORKER };
inline const char* schema[]={
    "CONFIG\tversion seat memory_enabled freeze_native_turns auto_menus",
    "SOURCE\tround tracker_frame enabled board_used bound advancing operating native_pair present usable pose_accepted generation observed_ms sample_frame pose_frame tick read_begin read_end",
    "NATIVE\tseat generation round_start read_begin read_end tick pose_frame pair x y upper dir operating horizontal_moving bound advancing running settled",
    "GATE\tround host_frame driver_pair native_pair requested_logical output_logical reasons memory_enabled bound advancing operating lag swapped native_tick native_pose_frame native_fresh image_gate_ready",
    "PAD\trequested_xusb previous_xusb actual_xusb disposition api_result api_begin api_end",
    "DRIVE\tround host_frame tracker_frame pair gate_decision_calls source_enabled board_used driver_us refresh_us output_buttons actual_xusb reason begin sent_driven sent_set end diag_cost_us dropped_events",
    "CAL_ROW\tround pair tracker_frame old_lag old_swapped candidate_lag same_mapping_score opposite_mapping_score shots",
    "CAL_APPLY\tround pair tracker_frame old_lag old_swapped new_lag new_swapped old_score best_score best_lag best_other apply same_at_best opposite_at_best shots freeze_native_turns duration_us",
    "CORRECT\tround pair sample_pair tick sample_frame host_frame lag outcome next_before next_after x_before y_before upper_before dir_before x_after y_after upper_after dir_after used_delta corrected_delta read_begin read_end gain_before move_wait_before gain_after move_wait_after duration_us",
    "THINK\tagent agent_round decision frame pair reason legal colors_axis colors_child",
    "CHOSEN\tagent agent_round decision frame pair reason target duration_us",
    "TARGET\tagent agent_round decision frame pair target reason previous_target observed_pair",
    "DRIVER_TARGET\tround host_frame driver_pair target agent agent_round decision target_pair owner_matches steered second_switches",
    "AGENT_FRAME\tround driver_pair model_frame agent agent_round target_decision target_pair target wanted sent send_flag next_before next_after",
    "DRIVER_START\tround driver_pair tracker_frame at next prior_target prior_target_pair prior_decision",
    "ROUND\tagent agent_round seat seed",
    "MARKER\taction tracker_frame driver_pair",
    "NATIVE_GUARD\taction host_frame pair tick pose_frame reason observed_x observed_dir current_x current_dir candidate_x candidate_dir explained_frame goal lag",
    "RECOVERY\thost_frame pair reason status goal x y dir next",
    "NEXT_READ\tseat round_start generation tracker_pair tracker_frame stage before_reject packet_frame packet_observed_ms tick pid",
    "NEXT_CONSUMER\tseat round_start tracker_pair tracker_frame status generation packet_pair packet_frame packet_observed_ms now_ms tick pid",
    "NEXT_OBSERVATION\tseat round_start tracker_pair tracker_frame status generation packet_pair packet_frame packet_observed_ms tick observation_frame prefix_count pid",
    "WINDOW_WATCH\ttracker_frame elapsed user_selected remembered memory_enabled memory_state kept",
    "MEMORY_WORKER\tpid epoch round_known from_start tracker_rounds round_start tracker_frame pair0 pair1 state reason scanning scans scan_bytes scan_total last_catalog_count settled sufficient known0 pieces0 colors0 known1 pieces1 colors1 observed_ms audit_now_ms sequence"
};
inline constexpr size_t CAPACITY=16384, MAX_VALUES=40;
inline int gateReasons(bool enabled,bool bound,bool advancing,int driverPair,uint64_t nativePair,bool holdNothing){
    int reason=holdNothing?4:0;
    if(enabled && bound){if(!advancing)reason|=1;if(uint64_t(driverPair)!=nativePair)reason|=2;}
    return reason;
}
struct Event{int64_t time=0;uint64_t seq=0;int kind=0;uint8_t size=0;std::array<int64_t,MAX_VALUES> value{};};
class Trace {
    std::array<Event,CAPACITY> ring_{};size_t begin_=0,size_=0;
    std::mutex mutex_;std::condition_variable wake_;std::thread writer_;std::ofstream out_;
    std::atomic<bool> active_{false};bool stopping_=false;
    std::atomic<uint64_t> sequence_{0},dropped_{0},cost_{0},count_{0},maxCost_{0},highWater_{0},session_{0};
    void run(){
        std::array<Event,256> batch;
        for(;;){size_t n=0;bool done=false;
            {std::unique_lock<std::mutex> hold(mutex_);wake_.wait_for(hold,std::chrono::milliseconds(100),[&]{return size_||stopping_;});
                n=std::min(size_,batch.size());for(size_t i=0;i<n;++i){batch[i]=ring_[begin_];begin_=(begin_+1)%CAPACITY;}
                size_-=n;done=stopping_ && !size_;}
            for(size_t i=0;i<n;++i){const auto& e=batch[i];out_<<e.time<<'\t'<<e.seq<<'\t'<<e.kind;
                for(unsigned j=0;j<e.size;++j)out_<<'\t'<<e.value[j];out_<<'\n';}
            if(done)break;
        }
    }
public:
    std::atomic<int> seat{1};
    ~Trace(){close();}
    bool active()const{return active_.load(std::memory_order_relaxed);}
    uint64_t session()const{return session_.load(std::memory_order_relaxed);}
    uint64_t dropped()const{return dropped_.load(std::memory_order_relaxed);}
    uint64_t cost100ns()const{return cost_.load(std::memory_order_relaxed);}
    bool open(const std::filesystem::path& file,Clock source){
        close();clockSource=source;out_.open(file,std::ios::binary|std::ios::trunc);if(!out_)return false;
        out_<<"# JFINPUTDIAG1, timestamps=QPC100ns, value order follows each schema; data rows: time seq kind values...\n";
        for(int i=0;i<int(std::size(schema));++i)out_<<"# schema "<<i<<'\t'<<schema[i]<<'\n';
        out_<<"# gate reasons bit1=native_not_advancing bit2=pair_mismatch bit4=hold_nothing; PAD disposition 0=coalesced 1=sent 2=api_failure 3=not_added\n";
        out_<<"# TARGET reason 0=first_route 1=late_route 2=NEXT_second 3=steer_same 4=rescue_nearest; THINK reason 0=first_or_renewed 1=NEXT_second; CORRECT outcomes 0=missing 1=pair 2=invalid_frame 3=future 4=duplicate 5=reanchor 6=bad_start 7=bad_history_phase 8=bad_identity 9=adjacent_equal 10=correct_replay 11=correct_then_lock\n";
        begin_=size_=0;stopping_=false;sequence_=dropped_=cost_=count_=maxCost_=highWater_=0;
        ++session_;active_=true;writer_=std::thread([this]{run();});return true;
    }
    void close(){
        active_=false;{std::lock_guard<std::mutex> hold(mutex_);stopping_=true;}wake_.notify_all();
        if(writer_.joinable())writer_.join();
        if(out_.is_open()){out_<<"# footer events="<<count_<<" dropped="<<dropped_<<" enqueue_total_100ns="<<cost_
            <<" enqueue_max_100ns="<<maxCost_<<" queue_high_water="<<highWater_<<" writer_good="<<out_.good()<<'\n';out_.close();}
    }
    void emit(int kind,std::initializer_list<int64_t> values){
        if(!active())return;const auto start=clock();Event e;e.time=start;e.seq=sequence_.fetch_add(1);e.kind=kind;
        if(values.size()>MAX_VALUES){++dropped_;return;}e.size=uint8_t(values.size());std::copy(values.begin(),values.end(),e.value.begin());
        {std::unique_lock<std::mutex> hold(mutex_);
            if(!active() || size_==CAPACITY){++dropped_;return;}
            ring_[(begin_+size_)%CAPACITY]=e;++size_;++count_;
            auto h=highWater_.load();if(size_>h)highWater_=size_;}
        wake_.notify_one();const auto elapsed=uint64_t(std::max<int64_t>(0,clock()-start));cost_+=elapsed;
        auto m=maxCost_.load();while(m<elapsed && !maxCost_.compare_exchange_weak(m,elapsed)){}
    }
};
inline Trace trace;
inline std::atomic<uint64_t> agentSerial{0};
template<class F>struct ScopeExit{F f;~ScopeExit(){f();}};
template<class F>ScopeExit(F)->ScopeExit<F>;
}
