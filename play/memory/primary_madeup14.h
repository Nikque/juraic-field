#pragma once
// For tests and tools: a round of this program's engine as primary14.h reads the real game, a sample after each frame.
// The synthetic sample follows these properties of the real game's memory:
//  - the frame counter is that of the frame just played;
//  - a pair is in the player's hands one frame before a button moves it, where it appears (the engine has it appear in
//    the frame it first moves in: the frame before that, the engine's last frame of waiting, is shown so);
//  - a field is busy during a chain and falling garbage and at the end of the landing, until `quiet` frames before its
//    next pair is in the hands; its last busy frame is marked 8, a wave 1, falling garbage 10;
//  - a seat whose round is over has another number than a seat that plays.
//  - windowBefore: before the first pair of a round the window of the next pairs stands and shows the first three pairs
//    (the real game shows them so in the frame before that pair is in the hands; here from the first frame on).
// Nothing here reads a program.
#include "../../game/match.h"
#include "primary14.h"

namespace play::realgame {

inline PrimarySample14 madeUpSample14(const juraic::Round& r, uint64_t generation = 1, int quiet = 10, bool windowBefore = false) {
    using juraic::Phase;
    PrimarySample14 s;
    s.generation = generation, s.serial = uint64_t(r.frame);
    s.observedUs = uint64_t(r.frame) * 16667, s.observedMs = s.observedUs / 1000;
    const bool over = r.state != juraic::RoundState::Playing;
    for (int i = 0; i < 2; ++i) {
        const juraic::Player& q = r.player[i];
        PrimarySeat14 seat;
        OwnedPublicPose14& p = seat.pose;
        p.seat = i;
        p.count108 = uint32_t(r.frame - 1);
        p.fieldState = over ? 9u : 1u, p.playerState = 1;
        p.lockedPairs = uint32_t(q.moves);
        const bool control = q.phase == Phase::Control;
        const bool shown = q.phase == Phase::Next && q.wait == 1 && !q.field.dead();
        p.operating = !over && (control || shown);
        const juraic::PairControl at = control ? q.pair : juraic::PairControl{};
        p.x = at.x, p.y = at.y, p.upper = at.upper, p.dir = at.dir;
        const juraic::Pair colors = control ? q.colors : shown ? r.tsumoOf(i).pair(q.nextPair) : juraic::Pair{};
        p.currentColors = {int(colors.axis), int(colors.child)};
        const bool busy = q.phase == Phase::Wave || q.phase == Phase::Garbage || q.phase == Phase::Decide || (q.phase == Phase::Next && q.wait > quiet);
        p.fieldActive = busy ? 1 : 0;
        p.phase = q.phase == Phase::Wave ? 1u : q.phase == Phase::Garbage ? 10u : q.phase == Phase::Next && q.wait == quiet + 1 ? 8u : 0u;
        seat.field = q.field, seat.placed = p.operating;
        if (p.operating) {
            const int next = control ? q.nextPair : q.nextPair + 1;
            seat.pairsKnown = true;
            seat.pairs = {colors, r.tsumoOf(i).pair(next), r.tsumoOf(i).pair(next + 1)};
        } else if (windowBefore && !over && q.moves == 0 && q.phase == Phase::Next) {
            // (before the first pair of the round the window stands and shows it with the two after it)
            seat.windowKnown = true;
            seat.window = {r.tsumoOf(i).pair(0), r.tsumoOf(i).pair(1), r.tsumoOf(i).pair(2)};
        }
        s.seat[size_t(i)] = seat;
    }
    return s;
}

} // namespace play::realgame
