#pragma once
// One player, one frame at a time.
//
//   Next --> Control --> [Split] --> Land --> [Wave ...] --> Decide --> [Garbage] --> Next
//
// Control : the pair is in the player's hands (control.h) until it locks.
// Split   : one piece of a horizontal pair falls further.
// Land    : landing animation; when it ends the first wave is looked for.
// Wave    : one wave of the chain. Score and garbage change when the wave starts.
// Decide  : the chain (if any) is over. At the end of this frame, after both
//           players have moved, confirmed garbage falls. A pair that locks even
//           one frame before the opponent's chain ends is safe from that chain;
//           in the same frame it is not.
// Garbage : garbage is falling.
// Next    : the next pair is coming. The player loses when it cannot appear.
//
// The field always holds the result of the phase (pieces already put, cleared,
// fallen); the phase and its remaining frames tell a renderer what to animate.
// All lengths come from Timing: lock to next pair is landFrames + nextFrames,
// plus the split, the waves and the garbage.
#include "control.h"
#include "tsumo.h"
#include "turn.h"

namespace juraic {

enum class Phase : uint8_t { Next, Control, Split, Land, Wave, Decide, Garbage, Dead };

struct Player : PlayerCore {
    Phase phase = Phase::Next;
    int wait = 0;        // frames left in the phase (not used in Control, Decide, Dead)
    int nextPair = 0;    // index into the tsumo of the pair that appears next
    Pair colors{};       // the pair in play
    PairControl pair;
    uint8_t buttons = 0; // buttons of the previous frame
    ChainState chain;    // the chain in progress
    PlaceResult placed;  // where the last pair came to rest
    WaveReport wave;     // the wave in progress
    int dropped[W]{};    // garbage that fell in the last drop, by column
    int moves = 0;       // pairs locked so far
    // Pre-input (Rules::preInput): what the buttons asked for while no pair was in the player's hands.
    uint8_t sideHeld = 0;   // frames the direction has been held (PairControl::repeat goes on from it)
    uint8_t turnQueued = 0; // the rotation button pressed last, BTN_CW or BTN_CCW
    uint8_t turnAge = 0;    // frames since then
    uint8_t sideQueued = 0; // the direction of the last move asked for, BTN_LEFT or BTN_RIGHT
    uint8_t sideAge = 0;    // frames since then
};

struct PlayerEvents {
    ControlEvents control;
    bool spawned = false, locked = false, waveStarted = false, chainEnded = false, allClear = false;
    bool garbageFell = false, died = false;
};

// Before the first frame of a round.
inline void startPlayer(Player& p, const Timing& timing) {
    p = Player{};
    p.wait = timing.firstSpawn + 1; // the first pair appears on frame firstSpawn
}

namespace detail {
inline void startWaveOrEnd(Player& p, Incoming& opponent, const Rules& rules, const Timing& timing, int64_t frame,
                           PlayerEvents& e) {
    if (chainWave(p, opponent, rules, rules.rateAt(frame), p.chain, p.wave)) {
        p.phase = Phase::Wave;
        p.wait = timing.waveFrames(p.wave.longestFall);
        e.waveStarted = true;
        return;
    }
    e.chainEnded = p.chain.chains > 0;
    e.allClear = chainEnd(p, opponent, p.chain);
    p.phase = Phase::Decide;
}

// A frame in which no pair is in the player's hands: what the buttons ask for is kept in mind for
// the pair to come. A rotation is asked for by pressing its button; a move by pressing a direction,
// and by holding one until its auto-repeat has come round (it then waits for the pair, frame by frame).
inline void remember(Player& p, uint8_t previous, uint8_t buttons, const Timing& timing) {
    const uint8_t pressed = uint8_t(buttons & ~previous);
    if (p.turnAge < 255) ++p.turnAge;
    const uint8_t turn = pressed & (BTN_CW | BTN_CCW);
    if (turn == BTN_CW || turn == BTN_CCW) p.turnQueued = turn, p.turnAge = 0;

    const int want = (buttons & BTN_LEFT ? -1 : 0) + (buttons & BTN_RIGHT ? 1 : 0);
    const uint8_t side = want < 0 ? BTN_LEFT : BTN_RIGHT;
    bool asked = false;
    if (!want) p.sideHeld = 0;
    else if (pressed & side) p.sideHeld = 0, asked = true;
    else {
        if (p.sideHeld < 255) ++p.sideHeld;
        asked = p.sideHeld >= timing.repeatDelay;
    }
    if (p.sideAge < 255) ++p.sideAge;
    if (asked) p.sideQueued = side, p.sideAge = 0;
}
} // namespace detail

// The pair as it appears in the hands of p, whose `buttons` are those of the frame before:
// what advancePlayer makes of it, for anyone who wants to know before the frame is played.
inline PairControl appearingPair(const Player& p, const Rules& rules, const Timing& timing) {
    PairControl pair = spawnPair(timing);
    pair.held = p.buttons;
    if (rules.preInput) {
        // A direction that is still held counts on from where it is; when its auto-repeat has come
        // round it stands one frame short of it, so that the pair moves in its first frame and
        // every repeatInterval frames from there.
        pair.repeat = uint8_t(p.sideHeld < timing.repeatDelay ? p.sideHeld : timing.repeatDelay - 1);
        if (p.turnQueued && p.turnAge < timing.preFrames) pair.queued |= p.turnQueued;
        if (p.sideQueued && p.sideAge < timing.preFrames) pair.queued |= p.sideQueued;
    }
    return pair;
}

// First half of a frame: everything except the fall of garbage.
inline PlayerEvents advancePlayer(Player& p, Incoming& opponent, uint8_t buttons, const Tsumo& tsumo, const Rules& rules,
                                  const Timing& timing, int64_t frame) {
    PlayerEvents e;
    const uint8_t previous = p.buttons;
    if (p.phase == Phase::Dead || p.phase == Phase::Decide) return p.buttons = buttons, e;
    if (p.phase != Phase::Control) {
        const bool appears = p.phase == Phase::Next && p.wait <= 1;
        const PairControl pair = appears ? appearingPair(p, rules, timing) : PairControl{};
        p.buttons = buttons;
        if (!appears && rules.preInput) detail::remember(p, previous, buttons, timing);
        if (--p.wait > 0) return e;
        switch (p.phase) {
        case Phase::Split:
            p.phase = Phase::Land;
            p.wait = timing.landFrames;
            return e;
        case Phase::Land:
        case Phase::Wave:
            detail::startWaveOrEnd(p, opponent, rules, timing, frame, e);
            return e;
        case Phase::Garbage:
            p.phase = Phase::Next;
            p.wait = timing.nextFrames;
            return e;
        default: // Phase::Next: the pair appears and can be moved in this very frame
            if (p.field.dead()) {
                p.phase = Phase::Dead;
                e.died = true;
                return e;
            }
            p.colors = tsumo.pair(p.nextPair++);
            p.pair = pair;
            p.turnQueued = p.sideQueued = 0;
            p.phase = Phase::Control;
            e.spawned = true;
        }
    } else {
        p.buttons = buttons;
    }
    e.control = stepPair(p.pair, p.field, buttons, timing, rules.speedPercent);
    if (e.control.dropBonus && rules.softDropBonus) {
        p.scoring.score += e.control.dropBonus;
        p.scoring.pool += e.control.dropBonus;
    }
    if (!e.control.locked) return e;
    e.locked = true;
    p.sideHeld = p.pair.repeat; // (the count of a direction that is still held goes on)
    p.turnQueued = p.sideQueued = 0;
    ++p.moves;
    p.chain = {};
    const int axisX = p.pair.x;
    p.placed = place(p.field, p.pair.placement(), p.colors);
    const int split = timing.splitFrames(p.placed.split, axisX, p.placed.lockY);
    // The landing animation has run since the pair came to rest (Timing::lockWait): a pair that lay
    // on the ground before it locked has less of it left, or none.
    const int rested = int(p.pair.groundFrames);
    const int land = timing.landFrames - rested < timing.lockWait ? timing.lockWait : timing.landFrames - rested;
    p.phase = split ? Phase::Split : Phase::Land;
    p.wait = split ? split : land;
    return e;
}

// Second half of a frame, after both players have advanced.
inline void settlePlayer(Player& p, const Rules& rules, const Timing& timing, Rng& garbageRng, PlayerEvents& e) {
    if (p.phase != Phase::Decide) return;
    int heights[W];
    p.field.heights(heights);
    if (dropGarbage(p.field, p.incoming, rules, garbageRng, p.dropped)) {
        e.garbageFell = true;
        if (const int frames = timing.garbageFrames(heights, p.dropped)) {
            p.phase = Phase::Garbage;
            p.wait = frames;
            return;
        }
    }
    p.phase = Phase::Next;
    p.wait = timing.nextFrames;
}

} // namespace juraic
