#pragma once
#include "owned_pose14.h"
namespace play::realgame {
// A consumer-side freshness/identity gate, not a memory reader or an input
// sender. A decoded object alone never supplies the independently established
// round/seat binding. Native update stagnation suspends a binding without
// making a pause or a torn read cause another heap scan.
struct OwnedPoseBinding14 {
    FieldContext context;
    uint64_t generation=0,player=0,field=0;
    int seat=-1;
};
class OwnedPoseGate14 {
    std::optional<OwnedPoseBinding14> binding_;
    std::optional<OwnedPublicPose14> value_;
    uint64_t observed_=0,advanced_=0,candidateSince_=0;
    uint32_t tick_=0,locks_=0;
    bool haveTick_=false,progress_=false,confirmed_=false;
    static bool processMatches(FieldContext a,FieldContext b){
        a.discoveryOnly=b.discoveryOnly=false;
        return sameFieldContext(a,b);
    }
    static bool samePublicState(const OwnedPublicPose14& a,const OwnedPublicPose14& b){
        return a.owner.operate==b.owner.operate && a.count108==b.count108 && a.lockedPairs==b.lockedPairs &&
            a.x==b.x && a.y==b.y && a.upper==b.upper && a.dir==b.dir && a.horizontalMoving==b.horizontalMoving &&
            a.operating==b.operating && a.currentColors==b.currentColors && a.playerState==b.playerState && a.fieldState==b.fieldState;
    }
public:
    static constexpr uint64_t MAX_AGE_MS=50;
    static constexpr uint64_t MIN_STABLE_MS=2;
    void reset(){binding_.reset();value_.reset();observed_=advanced_=candidateSince_=0;tick_=locks_=0;haveTick_=progress_=confirmed_=false;}
    bool bind(const OwnedPoseBinding14& b){
        reset();
        if(b.context.discoveryOnly || !b.context.roundKnown || !b.context.round || !b.generation ||
           !b.context.process.pid || !b.context.process.creationTime || !b.context.process.imageBase ||
           !verifiedFieldImage(b.context.process.image) || b.seat<0 || b.seat>1 || b.player<4096 || b.field<4096)return false;
        binding_=b;return true;
    }
    // Each observation is a fresh, two-pass decoded sample. A backwards native
    // counter/lock serial revokes the binding; a missing read only revokes its
    // output. Rebinding is the caller's explicit lifecycle operation.
    bool observe(const OwnedPublicPose14& p,uint64_t generation,uint64_t now){
        if(!binding_ || generation!=binding_->generation || !processMatches(p.owner.context,binding_->context) ||
           p.seat!=binding_->seat || p.owner.player!=binding_->player || p.owner.field!=binding_->field || !p.owner.operate){reset();return false;}
        if(haveTick_ && (now<observed_ || p.count108<tick_ || p.lockedPairs<locks_)){reset();return false;}
        // The native counter increments at the start of its update. Two reads
        // inside that update can both precede the pose change. Require a later
        // poll of the same public state; do not equate counter advance with a
        // completed pose update. This is still a prerequisite, not an atomic
        // frame guarantee or an input calibration certificate.
        const bool same=value_ && samePublicState(*value_,p);
        if(!same)candidateSince_=now;
        confirmed_=same && now>=candidateSince_ && now-candidateSince_>=MIN_STABLE_MS;
        if(!haveTick_){tick_=p.count108;advanced_=now;haveTick_=true;}
        else if(p.count108!=tick_){tick_=p.count108;advanced_=now;progress_=true;}
        locks_=p.lockedPairs;observed_=now;value_=p;return true;
    }
    void unreadable(){value_.reset();confirmed_=false;}
    bool advancing(const FieldContext& scope,uint64_t generation,int seat,uint64_t now)const{
        return binding_ && value_ && progress_ && !scope.discoveryOnly && sameFieldContext(scope,binding_->context) &&
            generation==binding_->generation && seat==binding_->seat && now>=observed_ && now-observed_<=MAX_AGE_MS &&
            now>=advanced_ && now-advanced_<=MAX_AGE_MS && value_->playerState==1 && value_->fieldState==1;
    }
    bool running(const FieldContext& scope,uint64_t generation,int seat,uint64_t now)const{
        return advancing(scope,generation,seat,now) && value_->operating;
    }
    // Pair number and current colours must also agree with the consumer's
    // current public observation. This gate does not synthesize a new pair or
    // hide an unknown NEXT from the AI.
    std::optional<OwnedPublicPose14> settled(const FieldContext& scope,uint64_t generation,int seat,uint64_t now)const{
        if(!running(scope,generation,seat,now) || !confirmed_ || scope.discoveryOnly || !sameFieldContext(scope,binding_->context) ||
           generation!=binding_->generation || seat!=binding_->seat ||
           now<observed_ || now-observed_>MAX_AGE_MS || now<advanced_ || now-advanced_>MAX_AGE_MS)return {};
        const auto& p=*value_;
        if(!p.operating || p.horizontalMoving || p.playerState!=1 || p.fieldState!=1 ||
           p.x<0 || p.x>5 || p.y<0 || p.y>13 || p.dir<0 || p.dir>3 ||
           (p.dir==1 && p.x==5) || (p.dir==3 && p.x==0) || (p.dir==0 && p.y==13) || (p.dir==2 && p.y==0) ||
           p.currentColors[0]<1 || p.currentColors[0]>5 || p.currentColors[1]<1 || p.currentColors[1]>5)return {};
        // Keep discoveryOnly intact. Even passing these prerequisites does not
        // certify native input latency or authorize a replacement controller.
        return p;
    }
    std::optional<OwnedPublicPose14> current(const FieldContext& scope,uint64_t generation,
                                            int seat,int pair,std::array<int,2> colors,uint64_t now)const{
        auto p=settled(scope,generation,seat,now);
        if(!p || pair<1 || uint64_t(p->lockedPairs)+1!=uint64_t(pair) || p->currentColors!=colors)return {};
        return p;
    }
};
}
