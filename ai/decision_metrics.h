#pragma once
#include <array>
#include <chrono>
#include <cstdint>
namespace juraic::ai::metrics {

enum class Part {
    Answer,
    AnswerBuild,
    AnswerLegal,
    AnswerWindow,
    Choke,
    CommittedAnswer,
    Crush,
    CutAhead,
    CutBuild,
    CutQueues,
    EarlierWinFire,
    EvalActual,
    EvalBatch,
    EvalCalls,
    EvalHits,
    EvalRepeat,
    ExActual,
    ExBound,
    ExBoundActual,
    ExBoundCalls,
    ExCalls,
    ExchangePoke,
    ExSearch,
    FireAnswer,
    FireOrLose,
    Foundation,
    FullOffsetNext,
    KnownPoke,
    KnownPrefix,
    LegacyPoke,
    Lifeline,
    MidgamePressure,
    PartialBuild,
    PartialVeto,
    Preparation,
    ProtectAnswer,
    ReadyBuild,
    ReadyCover,
    ReceiveCompare,
    Reselect,
    Response,
    RivalPlanExtend,
    SelfPlanExtend,
    SetupSpeed,
    Shelter,
    ShelterNarrow,
    SideResponse,
    SmallReceive,
    SplitMain,
    TailAnswer,
    TailRank,
    TailValue,
    TakeSmall,
    Threat,
    TwoWave,
    Unannounced,
    UnannouncedWave,
    Win,
    WinRecheck,
    SelfBeam,
    RivalBeam,
    Candidates,
    ThirdPair,
    UnscopedPoll,
    StalePoll, PokeSearch, CrushSearch, WinSearch, PokeSearchReuse, CrushSearchReuse, WinSearchReuse, Count
};
inline constexpr std::array<const char*, static_cast<size_t>(Part::Count)> names = {
    "Answer",
    "AnswerBuild",
    "AnswerLegal",
    "AnswerWindow",
    "Choke",
    "CommittedAnswer",
    "Crush",
    "CutAhead",
    "CutBuild",
    "CutQueues",
    "EarlierWinFire",
    "EvalActual",
    "EvalBatch",
    "EvalCalls",
    "EvalHits",
    "EvalRepeat",
    "ExActual",
    "ExBound",
    "ExBoundActual",
    "ExBoundCalls",
    "ExCalls",
    "ExchangePoke",
    "ExSearch",
    "FireAnswer",
    "FireOrLose",
    "Foundation",
    "FullOffsetNext",
    "KnownPoke",
    "KnownPrefix",
    "LegacyPoke",
    "Lifeline",
    "MidgamePressure",
    "PartialBuild",
    "PartialVeto",
    "Preparation",
    "ProtectAnswer",
    "ReadyBuild",
    "ReadyCover",
    "ReceiveCompare",
    "Reselect",
    "Response",
    "RivalPlanExtend",
    "SelfPlanExtend",
    "SetupSpeed",
    "Shelter",
    "ShelterNarrow",
    "SideResponse",
    "SmallReceive",
    "SplitMain",
    "TailAnswer",
    "TailRank",
    "TailValue",
    "TakeSmall",
    "Threat",
    "TwoWave",
    "Unannounced",
    "UnannouncedWave",
    "Win",
    "WinRecheck",
    "SelfBeam",
    "RivalBeam",
    "Candidates",
    "ThirdPair",
    "UnscopedPoll",
    "StalePoll", "PokeSearch", "CrushSearch", "WinSearch", "PokeSearchReuse", "CrushSearchReuse", "WinSearchReuse"
};
#ifdef JURAIC_DECISION_METRICS
struct Sample {
    std::array<uint64_t, names.size()> ns{}, exclusiveNs{}, calls{}, cuts{};
    bool collectBoards = false;
    uint64_t number = 0;
};
inline thread_local Sample* sample = nullptr;
inline thread_local uint64_t serial = 0;
class Scope;
inline thread_local Scope* enclosing = nullptr;

struct Decision {
    Sample observation;
    Sample* previous = sample;
    Scope* previousScope = enclosing;
    Decision(bool enabled, bool boards) {
        observation.collectBoards = boards;
        observation.number = ++serial;
        sample = enabled ? &observation : nullptr;
        enclosing = nullptr;
    }
    ~Decision() { sample = previous; enclosing = previousScope; }
};
inline Sample* current() { return sample; }
inline bool active() { return sample != nullptr; }
inline uint64_t id() { return sample ? sample->number : 0; }
inline void add(Part part, uint64_t count = 1) {
    if (sample) sample->calls[static_cast<size_t>(part)] += count;
}

class Scope {
    using Clock = std::chrono::steady_clock;
    Scope* parent = nullptr;
    size_t index;
    uint64_t childNs = 0;
    bool stopped = false;
    Part searchOwner = Part::Count;
    Part enclosingSearch() const {
        for (auto* p = this; p; p = p->parent) {
            switch (static_cast<Part>(p->index)) {
            case Part::ExchangePoke: case Part::LegacyPoke: case Part::KnownPoke: return Part::PokeSearch;
            case Part::Crush: return Part::CrushSearch;
            case Part::Win: case Part::WinRecheck: return Part::WinSearch;
            default: break;
            }
        }
        return Part::Count;
    }
    Clock::time_point start;
public:
    explicit Scope(Part part) : index(static_cast<size_t>(part)) {
        if (!sample) return;
        parent = enclosing;
        if (parent && (part == Part::SelfBeam || part == Part::RivalBeam ||
                       part == Part::SelfPlanExtend || part == Part::RivalPlanExtend)) {
            searchOwner = parent->enclosingSearch();
            if (searchOwner != Part::Count) add(searchOwner);
        }
        enclosing = this;
        ++sample->calls[index];
        start = Clock::now();
    }
    Scope(const Scope&) = delete;
    ~Scope() {
        if (!sample) return;
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
        sample->ns[index] += elapsed;
        sample->exclusiveNs[index] += elapsed - childNs;
        // These three totals overlap the ordinary beam/plan component times.
        if (searchOwner != Part::Count) {
            auto i = static_cast<size_t>(searchOwner);
            sample->ns[i] += elapsed;
            sample->cuts[i] += stopped;
        }
        if (parent) parent->childNs += elapsed;
        enclosing = parent;
    }
    void reused() const {
        switch (enclosingSearch()) {
        case Part::PokeSearch: add(Part::PokeSearchReuse); break;
        case Part::CrushSearch: add(Part::CrushSearchReuse); break;
        case Part::WinSearch: add(Part::WinSearchReuse); break;
        default: break;
        }
    }
    void cut() {
        if (sample && !stopped) { ++sample->cuts[index]; stopped = true; }
    }
};
inline void reused() { if (enclosing) enclosing->reused(); }
inline void markCut() {
    if (enclosing) enclosing->cut();
    else add(Part::UnscopedPoll);
}
#else
struct Scope { explicit Scope(Part) {} void cut() {} };
inline void markCut() {}
inline void reused() {}
inline void add(Part, uint64_t = 1) {}
#endif
} // namespace juraic::ai::metrics
