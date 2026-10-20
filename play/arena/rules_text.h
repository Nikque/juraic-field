#pragma once
// The rules of the arena as text: one line of "name=value" words (the names of Juraic Field's settings,
// play/settings.h there), sent to the players before a match and kept in its record.
#include "../../game/rules.h"
#include <cstdlib>
#include <sstream>
#include <string>

namespace arena {

// The engine the rounds are played with (vendor/VENDOR.md). Raised when game/ or core/ change: a record
// names it, and a player whose engine has another name is not let in.
constexpr const char* ENGINE_NAME = "juraic-field-7d64cee";

// The rules of the arena: those of Juraic Field, a match won with three rounds. quickDrop: whether the
// quick drop button (BTN_DROP) counts; it is no rule of the engine but is decided by the arena for both.
struct ArenaRules {
    juraic::Rules rules;
    bool quickDrop = false;
    int64_t roundFrameLimit = 36000; // ten minutes: then the round is a draw
    ArenaRules() { rules.winsNeeded = 3, rules.setsNeeded = 1; }
    uint8_t mask() const { return quickDrop ? 63 : 31; }
};

inline std::string rulesText(const ArenaRules& a) {
    const juraic::Rules& r = a.rules;
    std::ostringstream s;
    s << "colors=" << r.colors << " clearCount=" << r.clearCount << " tsumoCorrection=" << int(r.tsumoCorrection)
      << " targetPoints=" << r.targetPoints << " marginSeconds=" << r.marginSeconds << " marginStepSeconds=" << r.marginStepSeconds
      << " marginRates=";
    for (int i = 0; i < r.marginSteps; ++i) s << (i ? "," : "") << r.marginRates[size_t(i)];
    s << " chainRequirement=" << r.chainRequirement << " allClearBonus=" << r.allClearBonus << " softDropBonus=" << (r.softDropBonus ? 1 : 0)
      << " maxGarbagePerDrop=" << r.maxGarbagePerDrop << " speedPercent=" << r.speedPercent << " preInput=" << (r.preInput ? 1 : 0)
      << " winsNeeded=" << r.winsNeeded << " setsNeeded=" << r.setsNeeded << " quickDrop=" << (a.quickDrop ? 1 : 0)
      << " roundFrameLimit=" << a.roundFrameLimit;
    return s.str();
}

// Reads what rulesText wrote. A word it does not know is passed over. False when a value is not a number or out of
// range: `a` is then left as it was.
inline bool readRules(const std::string& text, ArenaRules& a) {
    std::istringstream in(text);
    std::string word;
    ArenaRules got = a;
    juraic::Rules& r = got.rules;
    bool ok = true;
    // (the number of `text`, when it is one from low to high)
    const auto number = [&](const std::string& text, long long low, long long high, long long& v) {
        char* end = nullptr;
        v = std::strtoll(text.c_str(), &end, 10);
        ok = ok && !text.empty() && end && *end == 0 && v >= low && v <= high;
        return ok;
    };
    while (in >> word) {
        const size_t eq = word.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = word.substr(0, eq), value = word.substr(eq + 1);
        long long v = 0;
        const auto take = [&](int& to, long long low, long long high) {
            if (number(value, low, high, v)) to = int(v);
        };
        if (key == "colors") take(r.colors, 1, juraic::MAX_COLORS);
        else if (key == "clearCount") take(r.clearCount, 2, 72);
        else if (key == "tsumoCorrection" && number(value, 0, 2, v)) r.tsumoCorrection = juraic::TsumoCorrection(v);
        else if (key == "targetPoints") take(r.targetPoints, 1, 9990);
        else if (key == "marginSeconds") take(r.marginSeconds, 0, 99999);
        else if (key == "marginStepSeconds") take(r.marginStepSeconds, 0, 99999);
        else if (key == "marginRates") {
            int n = 0;
            std::istringstream list(value);
            std::string item;
            while (n < juraic::MAX_MARGIN_STEPS && std::getline(list, item, ',') && number(item, 0, 9990, v)) r.marginRates[size_t(n++)] = int(v);
            ok = ok && list.eof();
            r.marginSteps = n;
        } else if (key == "chainRequirement") take(r.chainRequirement, 1, 99);
        else if (key == "allClearBonus") take(r.allClearBonus, 0, 99999);
        else if (key == "softDropBonus" && number(value, 0, 1, v)) r.softDropBonus = v != 0;
        else if (key == "maxGarbagePerDrop") take(r.maxGarbagePerDrop, 1, 999);
        else if (key == "speedPercent") take(r.speedPercent, 1, 1000);
        else if (key == "preInput" && number(value, 0, 1, v)) r.preInput = v != 0;
        else if (key == "winsNeeded") take(r.winsNeeded, 1, 99);
        else if (key == "setsNeeded") take(r.setsNeeded, 1, 99);
        else if (key == "quickDrop" && number(value, 0, 1, v)) got.quickDrop = v != 0;
        else if (key == "roundFrameLimit" && number(value, 1, 216000, v)) got.roundFrameLimit = v; // (an hour at most)
    }
    if (ok) a = got;
    return ok;
}

} // namespace arena
