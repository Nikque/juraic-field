#pragma once
// The frame counts the AI reckons with when it plays the real game by its memory (play/memory/primary_play14.h), on
// their own: tools that hold records of the real game against the model need nothing else.
#include "../../game/timing.h"

namespace play::realgame {

// The frame counts the AI reckons with when it plays the real game by its memory: those the records of the real game
// agree with best (juraic::recordedRules; tools/realgame_record_check.cpp plays the buttons of every recorded pair on
// the engine and counts the frames that come out otherwise). The same for the ways that use the cancels and for those
// that do not - `cancel` only says which ways are planned (play/memory/primary_drive14_win.h), the game is one.
//
// The rules read off records of the real game are juraic::recordedRules (game/timing.h): the down button on the ground,
// the fall and the count of floor kicks, the wait of a turn before the lock, the clock after landing; the new pair known
// two frames before it is in the hands; no turn for seven frames after a quick turn. Rules::cancel of this program plays
// by them too.
inline juraic::Timing primaryTiming14(bool cancel) {
    (void)cancel;
    juraic::Timing t = juraic::recordedRules(juraic::Timing{});
    t.quickDownDrag = 32;
    return t;
}

// The states of the searches for the way over soonest (ai/fastest.h): of one made in the frame its buttons are for, and
// of one made ahead on a thread of its own (PlacementAgent::fastestStates, fastestAhead).
constexpr int PRIMARY_FASTEST_NOW14 = 600, PRIMARY_FASTEST_AHEAD14 = 300000;
// The frames on from which the way of a pair in the air is searched once more on that thread (PlacementAgent::fastestAgain).
constexpr int PRIMARY_FASTEST_AGAIN14 = 3;

// The frame counts of primaryTiming14 as they were up to the records of 2026-11-16 (for tools that compare the two).
inline juraic::Timing primaryTimingBefore14(bool cancel) {
    juraic::Timing t = cancel ? juraic::otherModel(juraic::Timing{}) : juraic::Timing{};
    t.quickTurnLock = 7; // (as the mode that reads the pictures has it: realGameTiming, play/realgame_read.h)
    t.landThenLock = true, t.turnLockFrames = 7;
    t.lockWait = 8;
    t.readyOnGround = false, t.quickFall = 0, t.quickFallFrames = 0;
    return t;
}

} // namespace play::realgame
