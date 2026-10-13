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

// The level a name asks for: "lv1" to "lv36" (ai/level.h), and "lv1q" to "lv36q" for the same levels of the AI that
// hardly pokes. 0: the name is no level.
inline int levelOf(const std::string& name) {
    const size_t digits = name.size() - (!name.empty() && name.back() == 'q' ? 1 : 0);
    if (digits < 3 || digits > 4 || name[0] != 'l' || name[1] != 'v' || name[2] == '0') return 0;
    int level = 0;
    for (size_t i = 2; i < digits; ++i) {
        if (name[i] < '0' || name[i] > '9') return 0;
        level = level * 10 + (name[i] - '0');
    }
    return level >= 1 && level <= LEVELS ? level : 0;
}
// Is that name the AI that hardly pokes, at its top (x3q) or at a level (lv1q to lv36q)?
inline bool quietOf(const std::string& name) { return name == "x3q" || (levelOf(name) && name.back() == 'q'); }

// The settings the X3 of that name plays with, `params` being those of X3 itself: x3 (and lv36, the top level) as
// given, x3q (and lv36q) with the settings of quietPoke, lv1 to lv35 with those of the level, lv1q to lv35q with both.
// (The levels were measured for X3 itself, one against the next; with the settings of quietPoke they are the same
// steps of hands, sight and search, and nothing was measured.)
inline Params agentParams(const std::string& name, const Params& params) { return levelParams(quietOf(name) ? quietPoke(params) : params, levelOf(name)); }

// Returns null for an unknown name. `params` is for x3, x3q and the levels.
inline std::unique_ptr<Agent> makeAgent(const std::string& name, const Params& params = Params{}) {
    if (extraAgent)
        if (std::unique_ptr<Agent> extra = extraAgent(name, params)) return extra;
    if (name == "x3" || name == "x3q" || levelOf(name)) return std::make_unique<X3Agent>(agentParams(name, params)); // (x3q: the AI that does not poke)
    if (name == "random") return std::make_unique<RandomAgent>();
    return nullptr;
}
inline const char* agentNames() { return "x3, x3q, lv1 to lv36, lv1q to lv36q, random"; }

} // namespace juraic::ai
