#pragma once
// The levels that come twice where the pairs can be cancelled.
//
// With the cancel, Lv36 and Lv37 each have a normal and a fastest-placement choice.
// Juraic 催促（極小） (x3q, style 1) has no Ex: it takes the straight ways with the cancel as well (play/session.h
// seat, play/realgame_win.h cancels()). Without the cancel no level takes the ways over soonest.
#include "../ai/level.h"

#include <algorithm>
#include <string>

namespace play {

constexpr int LEVEL_EX_FROM = 36;
constexpr int styleLevel(int level, int style) {
    return style ? juraic::ai::LEVELS : std::clamp(level, 1, juraic::ai::LAST_LEVEL);
}

constexpr bool levelEx(int level, bool ex, bool cancel) {
    return cancel && ex && level >= LEVEL_EX_FROM && level <= juraic::ai::LAST_LEVEL;
}

// Explicit development names use the same controls without entering the level picker.
constexpr bool seatUsesEx(int level, bool explicitEx, bool selectedEx, bool cancel) {
    return explicitEx ? cancel : levelEx(level, selectedEx, cancel);
}

struct LevelPick {
    int level;
    bool ex;
};

constexpr int levelPicks(bool cancel) {
    return juraic::ai::LAST_LEVEL + (cancel ? juraic::ai::LAST_LEVEL - LEVEL_EX_FROM + 1 : 0);
}
constexpr int levelPickIndex(int level, bool ex, bool cancel) {
    const int l = styleLevel(level, 0);
    return l - 1 + (cancel && l >= LEVEL_EX_FROM ? l - LEVEL_EX_FROM + int(ex) : 0);
}
constexpr LevelPick levelStep(int level, bool ex, int by, bool cancel) {
    const int n = levelPicks(cancel);
    const int i = ((levelPickIndex(level, ex, cancel) + by) % n + n) % n;
    if (!cancel) return {i + 1, ex};
    if (i < LEVEL_EX_FROM - 1) return {i + 1, false};
    const int high = i - (LEVEL_EX_FROM - 1);
    return {LEVEL_EX_FROM + high / 2, bool(high % 2)};
}

static_assert(levelPicks(false) == 38 && levelPicks(true) == 41);
static_assert(styleLevel(37, 1) == 36 && styleLevel(1, 1) == 36);
static_assert(levelStep(36, false, 1, true).ex && levelStep(36, true, 1, true).level == 37);
static_assert(levelEx(37, true, true) && !levelEx(37, true, false));
static_assert(levelStep(37, false, 1, true).ex && levelStep(37, true, 1, true).level == 38);
static_assert(levelStep(1, false, -1, true).level == 38 && levelStep(1, false, -1, true).ex);
static_assert(levelStep(37, false, -1, true).ex && levelStep(37, false, -1, false).level == 36);
static_assert(levelStep(36, true, levelPicks(true), true).ex);
static_assert(levelStep(36, true, 1, false).ex);
static_assert(levelStep(38, false, 1, true).ex && levelStep(38, true, 1, true).level == 1);

// "36" or "36Ex": the level as a screen and a record name it.
inline std::wstring levelNumber(int level, bool ex) { return std::to_wstring(level) + (ex ? L"Ex" : L""); }

struct AiLevelPick {
    int level;
    bool quiet, ex;
};

// Insert the fixed quiet Lv36 between normal Lv36 and Lv36Ex.
constexpr AiLevelPick stepAiLevelPick(int level, bool quiet, bool ex, int by) {
    const int quietIndex = juraic::ai::LEVELS;
    const int regular = levelPickIndex(level, ex, true);
    const int index = quiet ? quietIndex : regular + (regular >= quietIndex);
    const int count = levelPicks(true) + 1;
    const int next = ((index + by) % count + count) % count;
    if (next == quietIndex) return {juraic::ai::LEVELS, true, false};
    const LevelPick pick = levelStep(1, false, next - (next > quietIndex), true);
    return {pick.level, false, pick.ex};
}

inline std::wstring aiLevelName(int level, bool quiet, bool ex) {
    if (quiet) return L"Lv36催促極小";
    if (level == juraic::ai::LEVELS && !ex) return L"Lv36催促小";
    return L"Lv" + levelNumber(level, ex);
}

static_assert(stepAiLevelPick(36, false, false, 1).quiet);
static_assert(stepAiLevelPick(36, true, false, 1).ex);
static_assert(stepAiLevelPick(37, false, false, -1).ex);
static_assert(stepAiLevelPick(38, false, true, 1).level == 1);

} // namespace play
