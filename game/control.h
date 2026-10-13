#pragma once
// The falling pair, one frame at a time: movement, rotation, wall kick, floor
// kick, quick turn, falling, landing and locking.
//
// Position model (as in arcade Tsu and in [P] of game/timing.h): the axis
// piece is in a cell (x, y); `upper` says it is drawn half a cell above that
// cell. Collisions use the cell only. One step down goes from the upper half
// to the lower half of the same cell, or from the lower half to the upper half
// of the cell below, which must be free. A pair in the lower half that cannot
// step down is on the ground.
//
// Every number comes from Timing. Order inside a frame: rotation, horizontal
// move, fall.
#include "field.h"
#include "timing.h"

namespace juraic {

enum Button : uint8_t {
    BTN_LEFT = 1,
    BTN_RIGHT = 2,
    BTN_DOWN = 4,
    BTN_CCW = 8, // rotate left
    BTN_CW = 16, // rotate right
    // Quick drop: the pair goes straight down and locks in this frame. Measured on the
    // real game with the option on (timing.h [M]): after the lock the next pair comes as
    // after a landing with the down key; held as a pair appears, the pair drops in its
    // first frame. Whoever reads the buttons leaves the bit out when the option is off.
    BTN_DROP = 32,
};

constexpr int HALF_STEP = 128; // gain that makes one half-cell step
constexpr int SPAWN_Y = 11;    // the axis piece appears in the 12th row, upper half

struct PairControl {
    int8_t x = SPAWN_X, y = SPAWN_Y; // cell of the axis piece
    uint8_t dir = UP;                // where the child piece is
    bool upper = true;               // drawn half a cell above the cell
    bool grounded = false;
    bool locked = false;
    int16_t gain = 0;         // 1/256 cells fallen since the last step
    uint8_t moveWait = 0;     // frames a horizontal move still holds the pair
    uint8_t landings = 0;     // times the pair has come to rest on the ground
    uint8_t groundFrames = 0; // frames on the ground since the last landing
    uint8_t quickWindow = 0;  // frames left for the second press of a quick turn
    uint8_t quickLock = 0;    // frames during which rotation is not accepted
    uint8_t repeat = 0;       // frames the direction has been held
    uint8_t held = 0;         // buttons of the previous frame
    // Pre-input (Rules::preInput): what was asked for shortly before the pair appeared, done in
    // its first frame. BTN_CW or BTN_CCW: a rotation; BTN_LEFT or BTN_RIGHT: one move.
    uint8_t queued = 0;

    int childX() const { return dir == RIGHT ? x + 1 : dir == LEFT ? x - 1 : x; }
    int childY() const { return dir == UP ? y + 1 : dir == DOWN ? y - 1 : y; }
    Placement placement() const { return {uint8_t(x), dir}; }
    bool operator==(const PairControl&) const = default;
};

struct ControlEvents {
    bool moved = false, rotated = false, kicked = false, quickTurned = false, landed = false, locked = false;
    int dropBonus = 0; // points of the down key in this frame: a row passed, and the lock (Timing::dropLockPoints)
};

namespace detail {
struct Cells {
    uint16_t occ[W];
    const GameField& g;
    explicit Cells(const GameField& field) : g(field) { field.f.occupied().columns(occ); }
    bool free(int x, int y) const {
        if (x < 0 || x >= W || y < 0 || y > ROW_14) return false;
        return y == ROW_14 ? !g.row14[size_t(x)] : !(occ[x] >> y & 1);
    }
    bool canStepDown(const PairControl& p) const {
        if (p.upper) return true;
        return p.dir == DOWN ? free(p.x, p.y - 2) : free(p.x, p.y - 1) && (p.dir == UP || free(p.childX(), p.y - 1));
    }
};

inline void rotate(PairControl& p, const Cells& c, bool clockwise, const Timing& t, ControlEvents& e) {
    const uint8_t next = uint8_t((p.dir + (clockwise ? 1 : 3)) & 3);
    if (p.dir == UP || p.dir == DOWN) {
        // The child goes to the side; pushed away from a wall or a stack; with
        // both sides closed two presses turn the pair upside down.
        const int side = (p.dir == UP) == clockwise ? 1 : -1;
        if (c.free(p.x + side, p.y)) {
            p.dir = next;
        } else if (c.free(p.x - side, p.y)) {
            p.x = int8_t(p.x - side);
            p.dir = next;
            e.kicked = true;
        } else if (!p.quickWindow) {
            p.quickWindow = uint8_t(t.quickTurnWindow);
            return;
        } else if (p.dir == UP) {
            if (p.y + 1 >= ROW_14) return; // the axis piece never enters the 14th row
            // Quick turn, child from above to below: the axis rises one cell and
            // the pair ends half a cell higher than the two cells it had.
            p.y = int8_t(p.y + 1);
            p.dir = DOWN;
            p.upper = true;
            p.gain = int16_t(t.quickTurnGain);
            p.grounded = false;
            e.quickTurned = true;
        } else {
            // Child from below to above: the axis sinks one cell and the pair
            // ends half a cell lower, when there is room below.
            p.y = int8_t(p.y - 1);
            p.dir = UP;
            if (p.upper) {
                p.upper = false;
            } else if (c.free(p.x, p.y - 1)) {
                p.y = int8_t(p.y - 1);
                p.upper = true;
            }
            p.gain = int16_t(t.quickTurnGain);
            e.quickTurned = true;
        }
        if (e.quickTurned) p.quickLock = uint8_t(t.quickTurnLock);
        p.quickWindow = 0;
        e.rotated = true;
        return;
    }
    if (next == DOWN) {
        if (!c.free(p.x, p.y - 1)) {
            // Floor kick: the axis is lifted exactly one cell (a pair that was
            // still half a cell above stays half a cell above) and the child
            // goes below it. The fall restarts from Timing::liftGain.
            if (p.y + 1 >= ROW_14) return;
            p.y = int8_t(p.y + 1);
            p.gain = int16_t(t.liftGain);
            p.grounded = false; // standing on the child afterwards counts as a new landing
            e.kicked = true;
        }
    } else if (!c.free(p.x, p.y + 1)) {
        return; // a piece in the 14th row is in the way
    }
    p.dir = next;
    e.rotated = true;
}

inline void land(PairControl& p, bool down, const Timing& t, ControlEvents& e) {
    if (!p.grounded) {
        // Every landing starts the grace period again; the last one allowed locks at once.
        p.grounded = true;
        p.groundFrames = 0;
        e.landed = true;
        if (++p.landings >= t.maxLandings) p.locked = true;
    }
    if (down) p.locked = true;
}
} // namespace detail

// Advances the pair by one frame with the buttons held in that frame.
inline ControlEvents stepPair(PairControl& p, const GameField& field, uint8_t buttons, const Timing& t,
                              int speedPercent = 100) {
    ControlEvents e;
    const detail::Cells c(field);
    uint8_t pressed = uint8_t(buttons & ~p.held);
    p.held = buttons;
    // The first frame of a pair with pre-input: what was asked for before it
    // appeared is done now, unless the same kind of button is pressed in this very frame.
    const uint8_t queuedSide = pressed & (BTN_LEFT | BTN_RIGHT) ? uint8_t(0) : uint8_t(p.queued & (BTN_LEFT | BTN_RIGHT));
    if (!(pressed & (BTN_CW | BTN_CCW))) pressed |= uint8_t(p.queued & (BTN_CW | BTN_CCW));
    p.queued = 0;

    if (p.quickWindow) --p.quickWindow;
    if (p.quickLock) {
        --p.quickLock;
    } else {
        const bool cw = pressed & BTN_CW, ccw = pressed & BTN_CCW;
        if (cw != ccw) detail::rotate(p, c, cw, t, e);
    }

    int want = (buttons & BTN_LEFT ? -1 : 0) + (buttons & BTN_RIGHT ? 1 : 0);
    bool fire = false;
    if (!want) {
        p.repeat = 0;
    } else {
        fire = pressed & (want < 0 ? BTN_LEFT : BTN_RIGHT);
        if (fire) p.repeat = 0;
        else if (p.repeat < 255 && ++p.repeat >= t.repeatDelay) fire = (p.repeat - t.repeatDelay) % t.repeatInterval == 0;
    }
    // (the move asked for before the pair appeared; the direction may have been let go since)
    if (queuedSide) want = queuedSide & BTN_LEFT ? -1 : 1, fire = true;
    if (fire && !p.moveWait && c.free(p.x + want, p.y)) {
        if (c.free(p.childX() + want, p.childY())) {
            p.x = int8_t(p.x + want);
            p.moveWait = uint8_t(t.moveFrames);
            e.moved = true;
        } else if (p.dir == UP && p.childY() == ROW_14) {
            // A piece left in the 14th row is a wall for the child only: the
            // child is pushed to the side and the pair moves on.
            p.x = int8_t(p.x + want);
            p.dir = want > 0 ? LEFT : RIGHT;
            p.moveWait = uint8_t(t.moveFrames);
            e.moved = e.rotated = true;
        }
    }

    // During a horizontal move the down key does nothing (two frames per
    // column); the pair keeps falling at its own speed.
    bool down = buttons & BTN_DOWN;
    if (p.moveWait) {
        --p.moveWait;
        down = false;
    }
    if (buttons & BTN_DROP) {
        // Quick drop: straight down to where the pair comes to rest, locked in this frame
        // (also in the frame of a move). No points for the rows: whether the real game
        // gives any has not been measured.
        while (c.canStepDown(p)) {
            if (p.upper) p.upper = false;
            else p.y = int8_t(p.y - 1), p.upper = true;
        }
        detail::land(p, true, t, e);
        down = false; // (no points of the down key for it)
    } else if (!c.canStepDown(p)) {
        // groundFrames: the frames the pair has lain on the ground since its landing, this one
        // counted (also the frame in which a landing is found here, without a step down).
        const bool lay = p.grounded;
        if (lay && p.groundFrames < 255) ++p.groundFrames;
        detail::land(p, down, t, e);
        if (!lay && !p.locked) ++p.groundFrames;
        if (!p.locked) {
            // The pair locks when it would have fallen lockGain since its last step, had the
            // ground not been there (what was left of that step counts).
            const int fallen = p.gain + p.groundFrames * (t.fallPerFrame * speedPercent / 100);
            if (fallen >= t.lockGain) p.locked = true;
        }
    } else {
        p.grounded = false;
        p.gain = int16_t(p.gain + (down ? t.softDropPerFrame : t.fallPerFrame * speedPercent / 100));
        if (p.gain >= HALF_STEP) {
            p.gain = int16_t(p.gain - HALF_STEP < HALF_STEP ? p.gain - HALF_STEP : HALF_STEP - 1);
            if (p.upper) {
                p.upper = false;
            } else {
                p.y = int8_t(p.y - 1);
                p.upper = true;
                if (down) e.dropBonus = 1;
            }
            if (!c.canStepDown(p)) detail::land(p, down, t, e);
        }
    }
    // The points of a lock by the down key (Timing::dropLockPoints, dropLandPoints).
    if (p.locked && down) {
        const int fallen = p.gain + p.groundFrames * (t.fallPerFrame * speedPercent / 100);
        e.dropBonus += t.dropLockPoints + (fallen < HALF_STEP ? t.dropLandPoints : 0);
    }
    e.locked = p.locked;
    return e;
}

// A lever of four ways, for whoever reads the buttons of a person and wants no two directions at
// once (arcade Tsu was played with such a lever; a pad or a keyboard gives down and a direction
// together). Of the directions held together only the one pushed last comes out; when it is let
// go, one that is still held takes over. Of two pushed in the same frame the sideways one counts.
struct Lever {
    uint8_t before = 0, taken = 0;
    uint8_t operator()(uint8_t buttons) {
        constexpr uint8_t WAYS = BTN_LEFT | BTN_RIGHT | BTN_DOWN | BTN_DROP;
        const uint8_t ways = uint8_t(buttons & WAYS), pushed = uint8_t(ways & ~before);
        before = ways;
        const auto lowest = [](uint8_t bits) { return uint8_t(bits & (~bits + 1)); };
        if (pushed) taken = lowest(pushed);
        else if (!(ways & taken)) taken = lowest(ways);
        return uint8_t((buttons & ~WAYS) | taken);
    }
};

// The pair as it appears.
inline PairControl spawnPair(const Timing& t) {
    PairControl p;
    p.gain = int16_t(t.spawnGain);
    return p;
}

} // namespace juraic
