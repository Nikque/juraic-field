#pragma once
#include "decision_metrics.h"

#include "preparation.h"
#include <tuple>

namespace juraic::ai::lv38_answer_build {

struct Note {
    enum Status { Inactive, VisibleAnswer, NoRoute, TimedOut, Ranked };
    Status status = Inactive;
    int64_t frame = -1, confirm = -1;
    int arriving = 0, depth = 0, queues = 0, ordinary = -1, selected = -1;
    unsigned eligible = 0;
    int offsetOnly[PLACEMENTS]{}, returned[PLACEMENTS]{};
    int64_t sum[PLACEMENTS]{};
};

inline bool closesOnFirstDrop(const GameField& field, const Rules& rules, int arriving) {
    int heights[W];
    field.heights(heights);
    const int rows = (std::min(arriving, rules.maxGarbagePerDrop) + W - 1) / W;
    return heights[DEATH_X] + rows > DEATH_Y;
}

template<class Stop>
int prefer(const Observation& o, const Duel& afterChain, const PlanSet& mine, const PlanSet& theirs,
           unsigned legal, int ordinary, int arriving, int rate, const Params& p, uint64_t seed,
           uint64_t beamSeed, Builder& builder, Stop stop, Note& note) {
    metrics::Scope scope(metrics::Part::AnswerBuild);
    const int me = o.me;
    const auto& rival = afterChain.player[1 - me];
    const auto& self = o.self();
    if (arriving <= 0 || arriving >= 30 || !mine.pairCount || !mine.knownCount || p.handWait ||
        o.busyLeft[me] || self.incoming.confirmed || self.scoring.allClear || o.opponent().scoring.allClear ||
        (self.phase != Phase::Next && self.phase != Phase::Control) || self.field.f.normal().count() < 40 ||
        afterChain.state != RoundState::Playing || rival.phase != DuelPhase::Control || rival.fallen ||
        rival.scoring.allClear || stop()) return ordinary;

    const int64_t confirm = rival.spawnFrame - o.timing.nextFrames;
    const int64_t start = o.frame + (self.phase == Phase::Next ? std::max(0, self.wait - 1) : 0);
    if (confirm <= start) return ordinary;
    const bool guarded = p.lv38Plan7Guard > 0;
    if (guarded && closesOnFirstDrop(self.field, o.rules, arriving)) return ordinary;
    // Count the current pair and the final reply as well as estimated complete
    // moves. Every intervening quiet placement must settle before confirmation.
    const int depth = std::min({5, p.depth(), p.movesIn(confirm - start, self.field.f.count()) + 2});
    if (depth < 2) return ordinary;
    note.frame = o.frame;
    note.confirm = confirm;
    note.arriving = arriving;
    note.depth = depth;
    note.ordinary = note.selected = ordinary;
    note.status = Note::NoRoute;

    BuildAnswerWindow window;
    window.confirm = confirm;
    window.offsetPoints = int64_t(arriving) * rate - self.scoring.pool;
    window.returnPoints = window.offsetPoints + int64_t(W) * rate;
    readMainChain(self.field, o.rules, o.timing, p, &window.main);
    KnownReplay replay{stop};
    const Plan* immediateMain = nullptr;
    Wave firstWave;
    if (guarded) {
        for (const Plan& plan : mine.plans)
            if (plan.id[0] == ordinary && plan.count == 1 && plan.fireDepth == 1 && plan.chains >= 5) {
                immediateMain = &plan;
                break;
            }
        if (immediateMain) {
            if (mine.knownCount < 2 || immediateMain->allClear) return ordinary;
            GameField fired = self.field;
            juraic::place(fired, placement(ordinary), mine.pair[0]);
            if (!findWaveFor(fired.f, firstWave, o.rules.clearCount, false)) return ordinary;
        }
    }
    for (const auto* plans : {&mine.plans, &mine.keeping, &mine.tactical}) {
        for (const Plan& plan : *plans) {
            if (stop()) {
                note.status = Note::TimedOut;
                return ordinary;
            }
            if (!plan.count || plan.count > mine.knownCount || !(legal >> plan.id[0] & 1u)) continue;
            if (plan.allClear) return ordinary;
            if (plan.fireDepth != plan.count || plan.chains < 1 || plan.chains > 4 ||
                plan.points < window.offsetPoints || int64_t(plan.mainAfter) * 2 < window.main) continue;
            const Duel d = playOut(o, mine, &plan, theirs, nullptr, p, seed, true, nullptr, &replay);
            const auto& own = d.player[me];
            if (d.state != RoundState::Playing || own.phase != DuelPhase::Control ||
                own.moves != self.moves + plan.count || own.chain.chains < 1 || own.chain.chains > 4 ||
                own.scoring.allClear || own.fallen || own.incoming.total()) continue;
            int reserve = 0;
            readMainChain(own.field, o.rules, o.timing, p, &reserve);
            if (int64_t(reserve) * 2 >= window.main && !stop()) {
                note.status = Note::VisibleAnswer;
                return ordinary;
            }
        }
    }

    unsigned ready = 0;
    for (unsigned quiet = quietSetupChoices(o, mine, legal); quiet; quiet &= quiet - 1) {
        if (stop()) {
            note.status = Note::TimedOut;
            return ordinary;
        }
        const int id = std::countr_zero(quiet);
        Plan first;
        first.count = 1;
        first.id[0] = int8_t(id);
        ReplayCheckpoint spawn;
        const Duel d = playOut(o, mine, &first, theirs, nullptr, p, seed, true, nullptr, &replay, nullptr, &spawn);
        const auto& own = d.player[me];
        if (!spawn.duel.rules || d.state != RoundState::Playing || own.moves != self.moves + 1 ||
            own.phase != DuelPhase::Control || own.fallen || own.chain.chains || own.scoring.allClear ||
            own.spawnFrame - o.timing.nextFrames >= confirm) continue;
        if (guarded && closesOnFirstDrop(own.field, o.rules, arriving)) continue;
        if (immediateMain) {
            bool keepsFire = false;
            const auto existing = self.field.f.normal();
            for (unsigned ids = legalPlacements(own.field); ids && !stop(); ids &= ids - 1) {
                const int next = std::countr_zero(ids);
                GameField fired = own.field;
                juraic::place(fired, placement(next), mine.pair[1]);
                Wave wave;
                if (!findWaveFor(fired.f, wave, o.rules.clearCount, false) ||
                    (wave.cleared & existing).empty() || (wave.cleared & existing) != (firstWave.cleared & existing)) continue;
                Plan continuation = first;
                continuation.count = 2;
                continuation.id[1] = int8_t(next);
                int64_t ignition = -1;
                const Duel result = playOut(o, theirs, nullptr, mine, &continuation, p, seed, true,
                                            &ignition, &replay, &spawn);
                const auto& answer = result.player[me];
                if (result.state == RoundState::Playing && answer.moves == self.moves + 2 &&
                    ignition >= 0 && ignition <= confirm && !answer.fallen && !answer.incoming.total() &&
                    answer.chain.chains >= immediateMain->chains &&
                    answer.scoring.score - own.scoring.score >= immediateMain->points) {
                    keepsFire = true;
                    break;
                }
            }
            if (!keepsFire) continue;
        }
        window.nextSpawn[id] = own.spawnFrame;
        ready |= 1u << id;
    }
    if (!ready) return ordinary;

    Params q = p;
    q.beamDepth = depth;
    q.beamQueues = 8;
    q.beamLastValues = 0;
    {
        // (the third column kept low as in the search of the own field: X3Pace::thirdLimit)
        int h[W];
        self.field.heights(h);
        q.beamThirdMax = (p.x3ThirdStrict ? h[DEATH_X] > p.x3ThirdMax : h[DEATH_X] >= p.x3ThirdMax) ? 12 : p.x3ThirdMax;
    }
    const BuildResult b = builder.search(self.field, mine.pair, mine.knownCount, o.rules, o.timing,
                                         q, beamSeed, p.searchThreads, &window);
    if (stop() || b.cut) {
        note.status = Note::TimedOut;
        return ordinary;
    }
    note.queues = b.told;
    note.eligible = ready & b.alive;
    int64_t bestSum = 0;
    for (int id = 0; id < PLACEMENTS; ++id) {
        note.sum[id] = b.sum[id];
        note.returned[id] = std::popcount(b.shortReturn[id]);
        note.offsetOnly[id] = std::popcount(b.shortOffset[id] & ~b.shortReturn[id]);
        if (legal >> id & 1u) bestSum = std::max(bestSum, b.sum[id]);
    }
    int best = -1;
    const int minQueues = guarded ? std::max(1, p.lv38Plan7MinQueues) : 1;
    for (unsigned choices = note.eligible; choices; choices &= choices - 1) {
        const int id = std::countr_zero(choices);
        if (note.returned[id] + note.offsetOnly[id] < minQueues) continue;
        if (p.lv38Plan7Near > 0 && (bestSum - b.sum[id]) * 1000 > bestSum * p.lv38Plan7Near) continue;
        const auto rank = [&](int move) {
            const int returned = note.returned[move], offset = note.offsetOnly[move];
            return guarded ? std::tuple(returned + offset, returned, b.sum[move]) : std::tuple(returned, offset, b.sum[move]);
        };
        if (best < 0 || rank(id) > rank(best) || (rank(id) == rank(best) && id == ordinary)) best = id;
    }
    note.status = Note::Ranked;
    if (best >= 0) note.selected = best;
    return note.selected;
}

} // namespace juraic::ai::lv38_answer_build
