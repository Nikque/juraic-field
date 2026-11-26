#pragma once
// The AIs by name, for the tools and the game.
#include "level.h"
#include "tactics.h"

namespace juraic::ai {

// An AI from outside this repository (a measuring tool that includes these files): asked first when set.
inline std::unique_ptr<Agent> (*extraAgent)(const std::string& name, const Params& params) = nullptr;

// The AI that hardly pokes: X3 with eight settings of its own. It pokes about once in 1,600 choices and builds its main
// chain undisturbed. Measured with the default of the time: it won 370 of 720 rounds against it for 349, the same
// random numbers, and as many as it against the fixed Ama over 480. Whatever `params` says about these eight settings
// is replaced, so a change of the default does not reach it; the rest of `params` is taken as given.
inline Params quietPoke(Params p) {
    p.evFireHeight = 300, p.x3PokeGain = 75, p.xWildChance = 0, p.xWildEvery = 2, p.x3PokeNet = 0, p.x3PokeFrom = 0, p.x3KeepMain = 85, p.x3Crowd = 1000;
    return p;
}

inline bool explicitLv38Ex(const std::string& name) { return name == "lv38ex" || name == "lv38Ex"; }

// The level a name asks for: "lv1" to "lv38" (ai/level.h), and "lv1q" to "lv36q" for the same levels of the AI that
// hardly pokes. 0: the name is no level.
inline int levelOf(const std::string& name) {
    if (explicitLv38Ex(name)) return DEVELOPMENT_LEVEL;
    const bool quiet = !name.empty() && name.back() == 'q';
    const size_t digits = name.size() - int(quiet);
    if (digits < 3 || digits > 4 || name[0] != 'l' || name[1] != 'v' || name[2] == '0') return 0;
    int level = 0;
    for (size_t i = 2; i < digits; ++i) {
        if (name[i] < '0' || name[i] > '9') return 0;
        level = level * 10 + (name[i] - '0');
    }
    return level >= 1 && level <= (quiet ? LEVELS : DEVELOPMENT_LEVEL) ? level : 0;
}
// Is that name the AI that hardly pokes, at its top (x3q) or at a level (lv1q to lv36q)?
inline bool quietOf(const std::string& name) { return name == "x3q" || (levelOf(name) && name.back() == 'q'); }

// The settings the X3 of that name plays with, `params` being those of X3 itself: x3 (and lv36, the top level) as
// given, x3q (and lv36q) with the settings of quietPoke, lv1 to lv35 with those of the level, lv1q to lv35q with both.
// (The levels were measured for X3 itself, one against the next; with the settings of quietPoke they are the same
// steps of hands, sight and search, and nothing was measured.)
inline Params namedDefaults(const std::string& name) {
    Params p;
    const int level = levelOf(name);
    p.x3PokeNet = defaultPokeNet(level);
    if (level == 38) {
        p.lv38FastSoft = p.lv38FastBusy = p.lv38TakeSmall = p.lv38TwoWaveReselect = 1;
        p.lv38FastStates = 4000;
        if (explicitLv38Ex(name)) {
            p.lv38TailMin = 560;
            p.lv38CutAhead = 2;
            p.lv38CutNear = 200;
            p.lv38ExFlip = 1;
        }
    }
    return p;
}
inline Params applyNamedDefaults(const std::string& name, Params params) {
    if (levelOf(name) != DEVELOPMENT_LEVEL) return params;
    const Params raw;
    const Params named = namedDefaults(name);
    for (const auto& entry : PARAM_ITEMS)
        if (params.*entry.member == raw.*entry.member) params.*entry.member = named.*entry.member;
    return params;
}

// The game and arena already applied the named defaults before reading explicit options.
inline Params configuredParams(const std::string& name, const Params& params) {
    return levelParams(quietOf(name) ? quietPoke(params) : params, levelOf(name));
}

// Returns null for an unknown name. `params` is for x3, x3q and the levels.
inline Params agentParams(const std::string& name, const Params& params) {
    return configuredParams(name, applyNamedDefaults(name, params));
}
inline std::unique_ptr<Agent> makeConfiguredAgent(const std::string& name, const Params& params) {
    if (extraAgent)
        if (std::unique_ptr<Agent> extra = extraAgent(name, params)) return extra;
    if (name == "x3" || name == "x3q" || levelOf(name)) {
        const Params configured = configuredParams(name, params);
        auto agent = std::make_unique<X3Agent>(configured);
        agent->namedLv38Ex = explicitLv38Ex(name);
        agent->fastestWays = agent->namedLv38Ex;
        agent->exFlip = agent->namedLv38Ex && configured.lv38ExFlip > 0;
        agent->fastQuietInput = name == "lv38" && params.lv38FastInput;
        if (agent->fastQuietInput) {
            agent->fastSoft = params.lv38FastSoft != 0;
            agent->fastStates = params.lv38FastStates;
            agent->fastBusy = params.lv38FastBusy != 0;
        }
        return agent;
    }
    if (name == "random") return std::make_unique<RandomAgent>();
    return nullptr;
}
inline std::unique_ptr<Agent> makeAgent(const std::string& name, const Params& params) {
    return makeConfiguredAgent(name, applyNamedDefaults(name, params));
}
inline std::unique_ptr<Agent> makeAgent(const std::string& name) { return makeConfiguredAgent(name, namedDefaults(name)); }
inline const char* agentNames() { return "x3, x3q, lv1 to lv38, lv38ex, lv1q to lv36q, random"; }

} // namespace juraic::ai
