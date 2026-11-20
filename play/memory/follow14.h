#pragma once
// Seat binding by fresh visible evidence. No process access or game input here.
#include "layout14.h"
#include <algorithm>
#include <optional>
#include <set>
#include <span>

namespace play::realgame {
inline bool verifiedFieldImage(const ImageHeader& h) {
    return h.ok && h.machine == 0x8664 && h.timeStamp == 0x66a8c684 && h.imageSize == 0x6fd000;
}
// Creation time distinguishes a new process even when Windows reuses its PID.
struct FieldProcess {
    uint32_t pid = 0;
    uint64_t creationTime = 0, imageBase = 0;
    ImageHeader image;
};
struct FieldContext {
    FieldProcess process;
    uint64_t round = 0;
    bool roundKnown = false;
    bool discoveryOnly = false;
};
inline bool sameFieldContext(const FieldContext& a, const FieldContext& b) {
    return a.process.pid == b.process.pid && a.process.creationTime == b.process.creationTime &&
        a.process.imageBase == b.process.imageBase && a.process.image.ok == b.process.image.ok &&
        a.process.image.machine == b.process.image.machine && a.process.image.timeStamp == b.process.image.timeStamp &&
        a.process.image.imageSize == b.process.image.imageSize && a.round == b.round && a.roundKnown == b.roundKnown && a.discoveryOnly == b.discoveryOnly;
}
// Values are bottom-up y=0..11. -1 means obscured/unknown, not an empty cell.
struct VisiblePair14 {
    FieldContext context;
    uint64_t sequence = 0, observedMs = 0;
    std::array<std::array<int8_t,72>,2> cells = [] {
        std::array<std::array<int8_t,72>,2> c; for (auto& s:c) s.fill(-1); return c;
    }();
    bool settled = false;
};
struct FieldCatalog14 {
    FieldContext context;
    uint64_t generation = 0;
    std::vector<uint64_t> visibleTop;
    // True only after the entire chosen data scan completed without truncation/read failure.
    bool complete = false;
};
enum class Follow14State { NoProcess, Unsupported, RoundUnknown, NeedScan, NeedEvidence, Ambiguous, Confirming, Ready };
inline const char* follow14Name(Follow14State s) {
    switch(s) {
    case Follow14State::NoProcess:return "no_process";
    case Follow14State::Unsupported:return "unsupported_image";
    case Follow14State::RoundUnknown:return "round_unknown";
    case Follow14State::NeedScan:return "need_scan";
    case Follow14State::NeedEvidence:return "need_evidence";
    case Follow14State::Ambiguous:return "ambiguous";
    case Follow14State::Confirming:return "confirming";
    case Follow14State::Ready:return "ready";
    } return "invalid";
}
struct Follow14Config {
    uint64_t maxEvidenceAgeMs = 250;
    size_t maxCandidates = 4096;
    uint64_t maxScanBytes = uint64_t(2)<<30;
    size_t scanChunkBytes = 65536;
    int minKnown = 60, minPieces = 8, minColors = 3;
};
struct Follow14Result {
    Pair14Binding binding;
    std::array<juraic::GameField,2> fields;
    FieldContext context;
    uint64_t sequence = 0, observedMs = 0, generation = 0;
};
using FieldRead14 = std::function<bool(uint64_t,void*,size_t)>;
using Field14Bytes = std::array<uint8_t,440>;
using FieldReadPass14 = std::function<bool(std::span<const uint64_t>,std::span<Field14Bytes>)>;
inline bool decodeField14(const uint8_t* bytes, size_t size, juraic::GameField& into) {
    if (size < 440) return false;
    juraic::GameField field;
    for (int y=0;y<14;++y) for (int x=0;x<6;++x) {
        const int r=13-y;
        const auto v=memoryValue(bytes+r*32+x*4,4);
        if ((v>>16)!=unsigned(9+r*8+x) || (v&255)>6) return false;
        if (y==13) field.row14[size_t(x)]=juraic::Cell(v&255);
        else field.f.set(x,y,juraic::Cell(v&255));
    }
    into=field; return true;
}
inline bool readStableField14(uint64_t top,const FieldRead14& read,juraic::GameField& into) {
    if (top<64 || top>UINT64_MAX-376) return false;
    std::array<uint8_t,440> a{},b{};
    return read(top-64,a.data(),a.size()) && read(top-64,b.data(),b.size()) && a==b && decodeField14(a.data(),a.size(),into);
}
inline bool visibleMatch14(const juraic::GameField& f,const std::array<int8_t,72>& ref) {
    for(int y=0;y<12;++y) for(int x=0;x<6;++x)
        if(ref[size_t(y*6+x)]>=0 && f.f.get(x,y)!=ref[size_t(y*6+x)]) return false;
    return true;
}

class FieldFollower14 {
public:
    explicit FieldFollower14(Follow14Config config={}) : cfg(config) {}
    // Must be called on process loss and every lifecycle update, before any consumer reads output.
    void synchronize(const FieldContext& next) {
        if (!haveContext || !sameFieldContext(context,next)) {
            context=next; haveContext=true; ++generation_; catalog.clear(); lastSequence=0;reason_="context_changed";
            dropProof(); state_=Follow14State::NeedScan;
        }
        if(!context.process.pid || !context.process.creationTime || !context.process.imageBase) invalidate(Follow14State::NoProcess);
        else if(!verifiedFieldImage(context.process.image)) invalidate(Follow14State::Unsupported);
        else if(!context.roundKnown || !context.round) invalidate(Follow14State::RoundUnknown);
    }
    uint64_t generation() const {return generation_;}
    Follow14State state() const {return state_;}
    const char* reason() const {return reason_;}
    uint64_t failingAddress() const {return failingAddress_;}
    bool current(const Follow14Result& result,uint64_t nowMs) const {
        return state_==Follow14State::Ready && sameFieldContext(result.context,context) && result.generation==generation_ &&
            result.sequence==lastSequence && nowMs>=result.observedMs && nowMs-result.observedMs<=cfg.maxEvidenceAgeMs;
    }
    bool install(const FieldCatalog14& found) {
        if(!usableContext() || !sameFieldContext(found.context,context) || found.generation!=generation_) return false;
        if(!found.complete || found.visibleTop.size()>cfg.maxCandidates) { invalidate(Follow14State::NeedScan); return false; }
        auto addresses=found.visibleTop;
        std::sort(addresses.begin(),addresses.end()); addresses.erase(std::unique(addresses.begin(),addresses.end()),addresses.end());
        for(auto a:addresses) if(a<64 || a>UINT64_MAX-376) {invalidate(Follow14State::NeedScan);return false;}
        catalog=std::move(addresses); dropProof(); reason_="waiting_for_visible_evidence";state_=Follow14State::NeedEvidence;
        return true;
    }
    // There is no cached 'last good' output. Every returned pair needs current, fresh visual evidence.
    std::optional<Follow14Result> observe(const VisiblePair14& evidence,uint64_t nowMs,const FieldRead14& read,const FieldReadPass14& batch={}) {
        if(!usableContext()) return {};
        if(!sameFieldContext(evidence.context,context)) {dropProof(); if(state_!=Follow14State::NeedScan)state_=Follow14State::NeedEvidence;return {};}
        if(state_==Follow14State::NeedScan) return {};
        if(!evidence.sequence || evidence.sequence<=lastSequence || nowMs<evidence.observedMs ||
           nowMs-evidence.observedMs>cfg.maxEvidenceAgeMs) {
            dropProof(); state_=Follow14State::NeedEvidence;return {};
        }
        lastSequence=evidence.sequence;
        // A landing/animation temporarily suspends output, but its intervening pictures must not erase
        // the first stable observation needed to prove that the same addresses followed a placement.
        if(!evidence.settled) {mismatchSince_.reset();state_=Follow14State::NeedEvidence;return {};}
        if(!informative(evidence)) {dropProof();state_=Follow14State::NeedEvidence;return {};}
        sampleFirst_.resize(catalog.size());sampleSecond_.resize(catalog.size());sampleFields_.resize(catalog.size());
        const auto pass=[&](std::vector<Field14Bytes>& into){
            if(batch)return batch(catalog,into);
            for(size_t i=0;i<catalog.size();++i)if(!read(catalog[i]-64,into[i].data(),into[i].size())){failingAddress_=catalog[i];return false;}
            return true;
        };
        // Interleave the complete candidate set: A1 B1 ... A2 B2. The selected pair is published
        // from these same stable samples, instead of reading it a redundant third/fourth time.
        if(!pass(sampleFirst_) || !pass(sampleSecond_)) {reason_="candidate_unreadable";invalidate(Follow14State::NeedScan);return {};}
        std::array<std::vector<uint64_t>,2> matches;
        // Every catalog entry participates again: do not silently prune temporarily unreadable decoys.
        for(size_t i=0;i<catalog.size();++i) {
            const auto a=catalog[i];
            if(sampleFirst_[i]!=sampleSecond_[i]) {
                reason_="candidate_changed";failingAddress_=a;mismatchSince_.reset();state_=Follow14State::NeedEvidence;return {};
            }
            auto& f=sampleFields_[i];
            if(!decodeField14(sampleSecond_[i].data(),sampleSecond_[i].size(),f)) {reason_="candidate_invalid";failingAddress_=a;invalidate(Follow14State::NeedScan);return {};}
            for(int s=0;s<2;++s) if(visibleMatch14(f,evidence.cells[size_t(s)])) matches[size_t(s)].push_back(a);
        }
        if(matches[0].empty() || matches[1].empty()) {
            reason_="visible_mismatch";
            if(!mismatchSince_)mismatchSince_=nowMs;
            // Rendering and logical locking need not change in the same picture. Revoke output immediately,
            // but allow fresh pictures to catch up before discarding the entire candidate census.
            if(nowMs<*mismatchSince_ || nowMs-*mismatchSince_>=750)invalidate(Follow14State::NeedScan);
            else state_=Follow14State::NeedEvidence;
            return {};
        }
        mismatchSince_.reset();
        if(matches[0].size()!=1 || matches[1].size()!=1 || matches[0][0]==matches[1][0]) {
            reason_="ambiguous";dropProof();state_=Follow14State::Ambiguous;return {};
        }
        const std::array<uint64_t,2> selected{matches[0][0],matches[1][0]};
        Pair14Binding binding{{0x8664,0x66a8c684,0x6fd000},context.process.pid,context.round,selected,true};
        std::array<juraic::GameField,2> fields;
        for(size_t s=0;s<2;++s)fields[s]=sampleFields_[size_t(std::lower_bound(catalog.begin(),catalog.end(),selected[s])-catalog.begin())];
        if(!pending || *pending!=selected) {
            dropProof(); pending=selected; first=evidence.cells;reason_="confirming"; state_=Follow14State::Confirming;return {};
        }
        for(int s=0;s<2;++s) for(size_t c=0;c<72;++c)
            if(first[size_t(s)][c]>=0 && evidence.cells[size_t(s)][c]>=0 && first[size_t(s)][c]!=evidence.cells[size_t(s)][c]) changed[size_t(s)]=true;
        // A repeated still frame cannot certify an old retained copy. Require a real change on each side.
        if(!changed[0] || !changed[1]) {state_=Follow14State::Confirming;return {};}
        failingAddress_=0;reason_="verified";state_=Follow14State::Ready;
        return Follow14Result{binding,fields,context,evidence.sequence,evidence.observedMs,generation_};
    }
private:
    bool usableContext() const {return haveContext && context.process.pid && context.process.creationTime && context.process.imageBase &&
        verifiedFieldImage(context.process.image) && context.roundKnown && context.round;}
    bool informative(const VisiblePair14& e) const {
        for(const auto& s:e.cells) {
            int known=0,pieces=0,ncolors=0; bool colors[7]{};
            for(auto c:s) {
                if(c< -1 || c>6) return false;
                if(c>=0) ++known;
                if(c>0) {++pieces;if(!colors[c]) {colors[c]=true;++ncolors;}}
            }
            if(known<cfg.minKnown || pieces<cfg.minPieces || ncolors<cfg.minColors) return false;
        } return true;
    }
    void dropProof() {pending.reset();changed={};mismatchSince_.reset();}
    void invalidate(Follow14State s) {catalog.clear();dropProof();state_=s;}
    Follow14Config cfg;
    FieldContext context;
    bool haveContext=false;
    uint64_t generation_=0,lastSequence=0;
    Follow14State state_=Follow14State::NoProcess;
    std::vector<uint64_t> catalog;
    std::optional<std::array<uint64_t,2>> pending;
    std::array<std::array<int8_t,72>,2> first{};
    std::array<bool,2> changed{};
    std::optional<uint64_t> mismatchSince_;
    const char* reason_="waiting";
    uint64_t failingAddress_=0;
    std::vector<Field14Bytes> sampleFirst_,sampleSecond_;
    std::vector<juraic::GameField> sampleFields_;
};
} // namespace play::realgame
