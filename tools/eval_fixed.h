#pragma once
#include "../ai/agents.h"
#include "decision_position_replay.h"
#include <iostream>
#include <fstream>
#include <stdexcept>

namespace juraic::eval_fixed {

using Clock = std::chrono::steady_clock;

struct Result {
    uint64_t code = 0;
    uint64_t calls = 0;

    void add(uint64_t x) { code = code * 0x9E3779B97F4A7C15ull + x; }
};

inline std::vector<GameField> fields() {
    Rng rng(71820, 13);
    std::vector<GameField> results;
    for (int i = 0; i < 128; ++i) {
        GameField board;
        for (int x = 0; x < W; ++x)
            for (int y = 0, n = 2 + int(rng.next() % 10); y < n; ++y)
                board.f.set(x, y, i % 3 && rng.next() % 7 == 0 ? GARBAGE : Cell(1 + rng.next() % 4));
        runChain(board.f);
        results.push_back(board);
    }
    return results;
}

inline void mixField(Result& sum, const BitField& board) {
    for (const FieldBits& plane : board.plane) {
        sum.add(plane.low());
        sum.add(plane.high());
    }
}

#if defined(_MSC_VER)
#define JURAIC_FIXED_NOINLINE __declspec(noinline)
#else
#define JURAIC_FIXED_NOINLINE __attribute__((noinline))
#endif

JURAIC_FIXED_NOINLINE inline void evalChild(const GameField& field, const Rules& rules, const Timing& timing,
                                            const ai::Params& params, ai::BuildTrials& scratch, Result& sum) {
    sum.add(ai::buildValue(scratch, field, rules, timing, params, nullptr, nullptr, true));
}
JURAIC_FIXED_NOINLINE inline void eraseChild(BitField field, FieldBits erased, Result& sum) {
    sum.add(field.eraseDrop(erased, field.occupied()));
    mixField(sum, field);
}
JURAIC_FIXED_NOINLINE inline void resolveChild(BitField field, const Rules& rules, const Timing& timing, Result& sum) {
    ai::Trial trial;
    ai::resolveBuildChild(field, rules, timing, trial);
    sum.add(trial.points);
    sum.add(trial.chains);
    mixField(sum, field);
}
JURAIC_FIXED_NOINLINE inline void mainOnly(const GameField& field, const Rules& rules, const Timing& timing, const ai::Params& params,
                                           Result& sum) {
    int points = 0;
    ai::KeyPlan trigger;
    ai::readMainChain(field, rules, timing, params, &points, &trigger);
    sum.add(points);
    sum.add(trigger.t.chains);
    sum.add(trigger.key);
    sum.add(trigger.t.x);
    sum.add(trigger.keyX);
    sum.add(trigger.keyColor);
}

inline std::vector<ai::decision_position::Record> snapshots(const char* file) {
    namespace pos = ai::decision_position;
    std::ifstream in(file, std::ios::binary);
    if (!pos::readHeader(in)) throw std::runtime_error("Invalid fixed positions header");
    std::vector<pos::Record> selected;
    pos::Record r;
    for (;;) {
        const auto status = pos::read(in, r);
        if (status == pos::Read::End) break;
        if (status == pos::Read::Error) throw std::runtime_error("Invalid fixed position");
        selected.push_back(r);
    }
    if (selected.empty()) throw std::runtime_error("No fixed position");
    return selected;
}

inline std::pair<Result, int64_t> bench(std::string_view id, const char* file) {
    Result result;
    Clock::time_point start;
    const Rules rules;
    const Timing timing;
    ai::Params params = ai::levelParams({}, 38);
    auto board = fields();
    if (id == "a1") {
        ai::BuildTrials scratch;
        start = Clock::now();
        for (int rep = 0; rep < 400; ++rep)
            for (const auto& g : board) { evalChild(g, rules, timing, params, scratch, result); ++result.calls; }
    } else if (id == "a2") {
        std::vector<FieldBits> erasures;
        for (size_t i = 0; i < board.size(); ++i) {
            FieldBits set;
            for (int x = 0; x < W; ++x)
                if (i & (1u << x)) {
                    const int nearTop = detail::countBits(board[i].f.occupied().column(x)) - 1;
                    set |= FieldBits::cell(x, i % 2 ? std::max(0, nearTop) : 0);
                }
            erasures.push_back(set);
        }
        start = Clock::now();
        for (int rep = 0; rep < 8000; ++rep)
            for (size_t i = 0; i < board.size(); ++i) { eraseChild(board[i].f, erasures[i], result); ++result.calls; }
    } else if (id == "a3") {
        std::vector<BitField> children;
        for (const auto& g : board)
            for (unsigned legal = legalPlacements(g); legal; legal &= legal - 1) {
                GameField child = g;
                place(child, placement(std::countr_zero(legal)), {2, 3});
                children.push_back(child.f);
            }
        start = Clock::now();
        for (int rep = 0; rep < 1200; ++rep)
            for (const auto& child : children) { resolveChild(child, rules, timing, result); ++result.calls; }
    } else if (id == "a4") {
        const auto observed = snapshots(file);
        start = Clock::now();
        for (int rep = 0; rep < 1400; ++rep)
            for (const auto& r : observed) {
                const auto& o = r.observation;
                mainOnly(o.self().field, o.rules, o.timing, params, result);
                mainOnly(o.opponent().field, o.rules, o.timing, params, result);
                result.calls += 2;
            }
    } else if (id == "a5") {
        using namespace ai;
        FastestSearch route;
        auto fastest = recordedRules(timing);
        std::vector<std::pair<GameField, PairControl>> ways;
        for (int i = 0; i < 48; ++i) {
            GameField g = board[size_t(i)];
            if (i >= 32) {
                g = {};
                for (int x = 0; x < W; ++x)
                    for (int y = 0; y < i % 8; ++y) g.f.set(x, y, Cell(1 + (x + y) % 4));
            }
            ways.emplace_back(g, spawnPair(fastest));
        }
        std::vector<uint8_t> buttons;
        start = Clock::now();
        for (int rep = 0; rep < 8; ++rep)
            for (size_t i = 0; i < ways.size(); ++i) {
                const auto& [g, from] = ways[i];
                route.maxStates = i % 3 == 0 ? 24 : i % 3 == 1 ? 600 : 4000;
                int end = 0, soft = 0;
                buttons.clear();
                const bool found = route.find(g, from, placement(int(i % PLACEMENTS)), fastest, 100, 64, buttons, soft, end);
                result.add(found);
                result.add(route.gaveUp);
                result.add(route.states);
                result.add(end);
                result.add(soft);
                for (uint8_t b : buttons) result.add(b);
                ++result.calls;
            }
    } else if (id == "a6") {
        const auto observed = snapshots(file);
        ai::Gaze gaze;
        ai::PlanSet set;
        start = Clock::now();
        for (int rep = 0; rep < 8; ++rep)
            for (const auto& r : observed) {
                const auto& o = r.observation;
                for (int side = 0; side < 2; ++side) {
                    gaze.plans(o, side, side == o.me, 3, params, r.context.roundSeed + o.self().moves, set);
                    for (auto* plans : {&set.plans,&set.keeping,&set.tactical}) {
                        result.add(plans->size());
                        for (const auto& p : *plans) {
                            result.add(p.count);
                            for (int k = 0; k < p.count; ++k) result.add(p.id[k]);
                            result.add(p.value);
                        }
                    }
                    ++result.calls;
                }
            }
    } else if (id == "decision-a4" || id == "decision-a6") {
        auto observed = snapshots(file);
        const bool ex = id == "decision-a6";
        if (ex) params = ai::agentParams("lv38ex", {});
        params.thinkBudget = 0;
        params.searchThreads = 1;
        params.beamWidth = 40;
        params.beamDepth = 6;
        params.beamQueues = 2;
        ai::decision_position::ReplayAgent agent(params);
        start = Clock::now();
        for (size_t n = 0; n < observed.size(); n += 3) {
            result.add(agent.replay(observed[n], false));
            result.add(agent.kind);
            ++result.calls;
        }
    } else throw std::runtime_error("Unknown fixed benchmark");
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
    return {result,ns};
}

inline int run(int argc, char** argv) {
    try {
        if (argc < 3) throw std::runtime_error("--eval-fixed requires a benchmark name");
        const std::string_view name = argv[2];
        const bool needsFile = name == "a4" || name == "a6" || name.starts_with("decision-");
        if (needsFile && argc != 4) throw std::runtime_error("A fixed positions file is required");
        const auto [result, ns] = bench(name, needsFile ? argv[3] : nullptr);
        std::cout << name << ',' << result.calls << ',' << ns << ',' << result.code << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fixed benchmark: " << e.what() << '\n';
        return 2;
    }
}

#undef JURAIC_FIXED_NOINLINE
}
