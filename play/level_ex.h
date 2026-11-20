#pragma once
// The levels that come twice where the pairs can be cancelled.
//
// With the cancel (Rules::cancel in a game of this program; the real game read by its memory with the row of the
// screen on "the cancels") only level 36 is offered twice: as they are - the straight way to every
// placement, as without the cancel - and "Ex", the same AI with the way that is over soonest (PlacementAgent::
// fastestWays: a pair moved on the ground before it locks, turns that push it aside). Below LEVEL_EX_FROM a level is
// only itself, and without the cancel every level is. A seat keeps the mark apart from its level (Settings::aiEx,
// aiEx1), so the level stays the number of ai/level.h.
#include "../ai/level.h"

#include <string>

namespace play {

constexpr int LEVEL_EX_FROM = 36;

// The AI of this level takes the ways over soonest: the seat is marked Ex, the level has an Ex and the pairs can be
// cancelled.
constexpr bool levelEx(int level, bool ex, bool cancel) { return cancel && ex && level == LEVEL_EX_FROM; }

struct LevelPick {
    int level;
    bool ex;
};

// The choices of a seat in the order a screen steps through them: Lv 1 .. Lv 35, Lv 36, and with the cancel Lv 36Ex
// after it; the last choice is Lv 37.
constexpr int levelPicks(bool cancel) { return juraic::ai::LAST_LEVEL + int(cancel); }
constexpr int levelPickIndex(int level, bool ex, bool cancel) {
    const int l = std::clamp(level, 1, juraic::ai::LAST_LEVEL);
    return l - 1 + int(cancel && (l > LEVEL_EX_FROM || (l == LEVEL_EX_FROM && ex)));
}
constexpr LevelPick levelStep(int level, bool ex, int by, bool cancel) {
    const int n = levelPicks(cancel);
    const int i = ((levelPickIndex(level, ex, cancel) + by) % n + n) % n;
    if (!cancel) return {i + 1, ex};
    if (i == LEVEL_EX_FROM) return {LEVEL_EX_FROM, true};
    return {i + 1 - int(i > LEVEL_EX_FROM), false};
}

static_assert(levelPicks(false) == 37 && levelPicks(true) == 38);
static_assert(levelStep(36, false, 1, true).ex && levelStep(36, true, 1, true).level == 37);
static_assert(!levelEx(37, true, true) && !levelStep(1, false, -1, true).ex);
static_assert(levelStep(37, false, 1, true).level == 1 && levelStep(1, false, -1, true).level == 37);
static_assert(levelStep(37, false, -1, true).ex && levelStep(37, false, -1, false).level == 36);
static_assert(levelStep(36, true, levelPicks(true), true).ex);
static_assert(levelStep(36, true, 1, false).ex);

// "36" or "36Ex": the level as a screen and a record name it.
inline std::wstring levelNumber(int level, bool ex) { return std::to_wstring(level) + (ex ? L"Ex" : L""); }

} // namespace play
