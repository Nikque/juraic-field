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

// Reads what rulesText wrote. A word it does not know is passed over; false when a value is out of range.
inline bool readRules(const std::string& text, ArenaRules& a) {
    std::istringstream in(text);
    std::string word;
    juraic::Rules& r = a.rules;
    bool ok = true;
    while (in >> word) {
        const size_t eq = word.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = word.substr(0, eq), value = word.substr(eq + 1);
        const long v = std::strtol(value.c_str(), nullptr, 10);
        if (key == "colors") r.colors = int(v), ok = ok && v >= 1 && v <= juraic::MAX_COLORS;
        else if (key == "clearCount") r.clearCount = int(v), ok = ok && v >= 2 && v <= 72;
        else if (key == "tsumoCorrection") r.tsumoCorrection = juraic::TsumoCorrection(v), ok = ok && v >= 0 && v <= 2;
        else if (key == "targetPoints") r.targetPoints = int(v), ok = ok && v >= 1;
        else if (key == "marginSeconds") r.marginSeconds = int(v);
        else if (key == "marginStepSeconds") r.marginStepSeconds = int(v);
        else if (key == "marginRates") {
            int n = 0;
            std::istringstream list(value);
            std::string item;
            while (std::getline(list, item, ',') && n < juraic::MAX_MARGIN_STEPS) r.marginRates[size_t(n++)] = std::atoi(item.c_str());
            r.marginSteps = n;
        } else if (key == "chainRequirement") r.chainRequirement = int(v);
        else if (key == "allClearBonus") r.allClearBonus = int(v);
        else if (key == "softDropBonus") r.softDropBonus = v != 0;
        else if (key == "maxGarbagePerDrop") r.maxGarbagePerDrop = int(v), ok = ok && v >= 1;
        else if (key == "speedPercent") r.speedPercent = int(v), ok = ok && v >= 1;
        else if (key == "preInput") r.preInput = v != 0;
        else if (key == "winsNeeded") r.winsNeeded = int(v), ok = ok && v >= 1;
        else if (key == "setsNeeded") r.setsNeeded = int(v), ok = ok && v >= 1;
        else if (key == "quickDrop") a.quickDrop = v != 0;
        else if (key == "roundFrameLimit") a.roundFrameLimit = v;
    }
    return ok;
}

} // namespace arena
