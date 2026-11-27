#pragma once

#include "../ai/decision_position.h"
#include "../ai/tactics.h"

namespace juraic::ai::decision_position {

// Replays a recorded placement decision with the caller's parameters.
// Scripted widths and depth are available when thinkBudget is disabled.
// Cut searches record the fact that they stopped, together with the width.
class ReplayAgent : public X3Agent {
public:
    using X3Agent::X3Agent;

    Context captureContext() const {
        Context c;
        const auto snapshotCarry = [](const TwoWaveCarry& v) { return Carry{v.moves, v.field, v.pairs, v.suffix}; };
        c.roundSeed = roundSeed;
        c.random = choosingAgain ? drawn : rng;
        c.choosingAgain = choosingAgain;
        c.setupPicked = setupPicked;
        c.namedLv38Ex = namedLv38Ex;
        c.lastChoice = lastChoice;
        c.firstWidth = firstWidth;
        c.unitNs = unitNs;
        c.slowNs = slowNs;
        c.plansNs = plansNs;
        c.againBudget = againBudget;
        c.input = {tailInput.hold, {tailInput.held[0], tailInput.held[1]}, tailInput.spare,
                   tailInput.waitQuick, tailInput.downAtTarget, tailInput.uncertain};
        c.twoWaveTurn = twoWaveTurn;
        c.carry = snapshotCarry(twoWaveCarry);
        c.next = snapshotCarry(twoWaveNext);
        c.sideReady = sideReady;
        c.held.depth = held.depth;
        if (held.depth) {
            c.held.field = held.field;
            std::copy_n(held.pair, held.pairs, c.held.pair);
            c.held.pairs = held.pairs;
            c.held.seed = held.seed;
            c.held.built = held.built;
        }
        return c;
    }

    int replay(const Record& r, bool useTrace = true) {
        const auto& c = r.context;
        const auto restoredCarry = [](const Carry& v) { return TwoWaveCarry{v.moves, v.field, v.pairs, v.suffix}; };
        roundSeed = c.roundSeed;
        rng = drawn = c.random;
        choosingAgain = c.choosingAgain;
        setupPicked = c.setupPicked;
        namedLv38Ex = c.namedLv38Ex;
        lastChoice = c.lastChoice;
        firstWidth = c.firstWidth;
        unitNs = c.unitNs;
        slowNs = c.slowNs;
        plansNs = c.plansNs;
        againBudget = c.againBudget;
        tailInput = {c.input.hold, {c.input.held[0], c.input.held[1]}, c.input.spare,
                     c.input.waitQuick, c.input.downAtTarget, c.input.uncertain};
        twoWaveTurn = c.twoWaveTurn;
        twoWaveCarry = restoredCarry(c.carry);
        twoWaveNext = restoredCarry(c.next);
        sideReady = c.sideReady;
        held.depth = c.held.depth;
        if (held.depth) {
            held.field = c.held.field;
            std::copy_n(c.held.pair, c.held.pairs, held.pair);
            held.pairs = c.held.pairs;
            held.seed = c.held.seed;
            held.built = c.held.built;
        }
        const auto* saved = script;
        std::vector<Note> recorded;
        if (useTrace) recorded.push_back({r.observation.self().moves, r.trace.pairs, r.selected, r.trace.cut, r.trace.widths});
        script = useTrace ? &recorded : nullptr;
        const int id = choose(r.observation, r.pair, r.legal);
        script = saved;
        scripted = nullptr;
        return id;
    }
};

} // namespace juraic::ai::decision_position
