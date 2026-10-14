#pragma once
// What the drawing of a player's field remembers between frames (play/view.h draws with it): the field a frame
// earlier, the animation that runs, where the pair is drawn. Nothing of a system is used: the rounds of a replay
// are played with it without a window.
#include "../game/match.h"

namespace play {
using namespace juraic;

// What the renderer keeps for one player between frames.
struct View {
    GameField last;        // the field at the end of the previous frame
    GameField from;        // the field the running animation starts from
    FieldBits popping;     // cells that disappear in the running wave
    int total = 0;         // length of the running phase
    float px = 2, py = 11.5f, angle = 0; // where the pair is drawn (smoothed)
    int chainShown = 0, chainTimer = 0;  // "n chain" banner
    // The pair that locked last, for its bounce: frames since the lock, the
    // cells of the two pieces (axis, child), and for the piece of a split the
    // frame (counted from the lock) in which it comes to rest; -1 for the other.
    int sinceLock = 1 << 20;
    // The bounce begins in the frame the pair lands - in the real game a pair that comes down by itself bounces
    // while it can still be moved (counted on video) -, not in the frame it locks: the frames since
    // the last landing of the pair, and how many of them had passed when it locked.
    int sinceLanding = 1 << 20, landedFor = 0;
    int lockX[2]{}, lockY[2]{}, restAt[2]{-1, -1};
    Cell lockColor[2]{};

    void reset(const Player& p) {
        *this = View{};
        last = from = p.field;
    }
    // Call once per game frame, after the frame has been played.
    void update(const Player& p, const PlayerEvents& e) {
        if (e.spawned) px = float(p.pair.x), py = float(p.pair.y) + 0.5f, angle = 0;
        if (e.locked || e.waveStarted || e.garbageFell) from = last, total = p.wait;
        ++sinceLock;
        if (sinceLanding < (1 << 20)) ++sinceLanding;
        if (e.spawned) sinceLanding = 1 << 20;
        if (e.control.landed) sinceLanding = 0;
        if (e.locked) {
            const Timing& t = DEFAULT_TIMING;
            sinceLock = 0;
            // (a lock in the frame of the landing, as with the down key: 0, the bounce begins here as before)
            landedFor = sinceLanding >= (1 << 20) ? 0 : sinceLanding < Timing::BOUNCE_LOCK ? sinceLanding : Timing::BOUNCE_LOCK;
            lockX[0] = p.pair.x, lockY[0] = p.placed.axisY, lockColor[0] = p.colors.axis;
            lockX[1] = p.pair.childX(), lockY[1] = p.placed.childY, lockColor[1] = p.colors.child;
            restAt[0] = restAt[1] = -1;
            if (p.phase == Phase::Split) // a split near the top starts later (see Timing::splitFrames)
                restAt[p.placed.axisY < p.placed.lockY ? 0 : 1] = p.wait - t.splitFrames(p.placed.split, 0, 0) + t.splitFall(p.placed.split);
        }
        if (e.waveStarted) {
            popping = p.wave.wave.cleared | p.wave.wave.garbage;
            chainShown = p.chain.chains, chainTimer = 70;
        }
        if (p.phase == Phase::Land && p.wait == DEFAULT_TIMING.landFrames) total = p.wait;
        if (chainTimer) --chainTimer;
        if (p.phase == Phase::Control) {
            const float tx = float(p.pair.x), ty = float(p.pair.y) + (p.pair.upper ? 0.5f : 0.f);
            px += (tx - px) * 0.55f;
            py += (ty - py) * 0.6f;
            float target = float(p.pair.dir) * 90.f, d = target - angle;
            while (d > 180) d -= 360;
            while (d < -180) d += 360;
            angle += d * 0.5f;
        }
        last = p.field;
    }
};

} // namespace play
