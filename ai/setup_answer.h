#pragma once
#include "exchange.h"

namespace juraic::ai {

// A few frames gained on a quiet placement should not cost a main chain
// which the visible pairs can use to answer a visible main on the other field.
template<class Stop>
int keepSetupAnswer(const Observation& o, const PlanSet& mine, const PlanSet& theirs,
                    int original, int faster, const Params& p, uint64_t seed, Stop stop) {
    if (original == faster || original < 0 || faster < 0 || !p.readMidgame || !p.checkKnownExchange ||
        o.rules.cancel || p.handWait || o.self().scoring.allClear || o.opponent().scoring.allClear ||
        o.self().incoming.total() || o.opponent().incoming.total() || mine.knownCount < 2 ||
        p.x3ChokeMin <= 0) return faster;
    if (stop()) return original;
    const auto stronger = [](const Plan* old, const Plan& plan) {
        return !old || plan.sent > old->sent || (plan.sent == old->sent && plan.endFrame < old->endFrame);
    };
    const auto main = [&](const Plan& plan, const PlanSet& set) {
        return plan.fireDepth && plan.fireDepth == plan.count && plan.count <= set.knownCount &&
               !plan.fallen && !plan.allClear && plan.sent >= p.x3ChokeMin;
    };
    const Plan* prepared = nullptr;
    const Plan* quick = nullptr;
    for (const auto* plans : {&mine.plans, &mine.tactical, &mine.keeping}) {
        for (const Plan& s : *plans) {
            if (s.count < 2 || !main(s, mine)) continue;
            if (s.id[0] == original && stronger(prepared, s)) prepared = &s;
            if (s.id[0] == faster && stronger(quick, s)) quick = &s;
        }
    }
    if (!prepared) return faster;
    std::array<const Plan*, MIDGAME_PLAN_PAIRS> threats{};
    for (const auto* plans : {&theirs.plans, &theirs.tactical, &theirs.keeping}) {
        for (const Plan& t : *plans) {
            if (!main(t, theirs)) continue;
            auto& atDepth = threats[t.count - 1];
            if (stronger(atDepth, t)) atDepth = &t;
        }
    }
    KnownReplay bounded;
    bounded.stop = stop;
    const auto answers = [&](const Plan* own, const Plan& foe) {
        if (!own || stop()) return false;
        const Duel d = playOut(o, mine, own, theirs, &foe, p, seed, true, nullptr, &bounded);
        const auto& self = d.player[o.me];
        const auto& rival = d.player[1 - o.me];
        return !stop() && d.state != (o.me ? RoundState::Won0 : RoundState::Won1) &&
               self.moves == o.self().moves + own->count && self.chain.chains == own->chains &&
               rival.moves == o.opponent().moves + foe.count && rival.chain.chains == foe.chains &&
               !self.field.dead() && !self.fallen && !self.incoming.total() &&
               !self.scoring.allClear && !rival.scoring.allClear;
    };
    // A smaller or earlier fire on the faster branch may also cover the threat.
    const auto fasterAnswers = [&](const Plan& foe) {
        if (!quick) return false;
        if (answers(quick, foe)) return true;
        for (const auto* plans : {&mine.plans, &mine.tactical, &mine.keeping}) {
            for (const Plan& s : *plans) {
                if (stop()) return false;
                if (s.id[0] == faster && &s != quick && s.count >= 2 && main(s, mine) && answers(&s, foe)) return true;
            }
        }
        return false;
    };
    for (const Plan* foe : threats) {
        if (stop()) return original;
        if (foe && answers(prepared, *foe) && !fasterAnswers(*foe)) return original;
    }
    return stop() ? original : faster;
}

} // namespace juraic::ai
