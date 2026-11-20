#pragma once
// The falling pair, one frame at a time: movement, rotation, wall kick, floor
// kick, quick turn, falling, landing and locking.
//
// Position model (as in the arcade version and in [P] of game/timing.h): the axis
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
    uint8_t landings = 0;     // landings so far; with Timing::floorKickLimit, floor kicks so far
    uint8_t groundFrames = 0; // frames on the ground since the last landing
    uint8_t quickWindow = 0;  // frames left for the second press of a quick turn
    uint8_t quickLock = 0;    // frames during which rotation is not accepted
    uint8_t repeat = 0;       // frames the direction has been held
    uint8_t held = 0;         // buttons of the previous frame
    // Pre-input (Rules::preInput): what was asked for shortly before the pair appeared, done in
    // its first frame. BTN_CW or BTN_CCW: a rotation; BTN_LEFT or BTN_RIGHT: one move.
    uint8_t queued = 0;
    // The rules of the other model (Timing::landThenLock and those after it): all 0 with those rules off.
    uint8_t turnWait = 0;  // frames left in which the down button does not lock the pair (Timing::turnLockFrames)
    uint8_t quickFast = 0; // 反転に伴う落下量の補正が残るフレーム数（Timing::quickFallFrames）
    bool owned = false;    // the pair has lain with its axis's own column under it (Timing::firstOwnLanding)
    uint8_t ownFrames = 0; // frames since it first did, counted as groundFrames is
    // What records of the real game show (Timing::liftWaits, quickRise): both 0 with those rules off.
    bool lifted = false;   // kicked up from the floor and still to make its next half step by itself: the down button does nothing
    uint8_t rise = 0;      // frames left in the lower half of the cell a quick turn took the axis up to (it neither falls nor lies)
    bool restless = false; // kicked up from the floor as it lay and not turned since: no clock of the landing animation (Timing::liftEndsRest)

    uint8_t liftFall = 0; // the fall counted before a floor kick; a turn that ends the wait of the lift goes on from it
    bool locking = false; // the lock is decided: only the wait of the last turn is left

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

// The count of the fall of a pair that lay, with Timing::fallOnGround (it went on while the pair lay, `fall` a frame), as
// the pair loses its ground by a move or a turn: a step that came due while it lay is made in that frame (the
// count just short of the step, which the frame's fall completes); none due yet, the count as it went on. A floor
// kick, after which the pair stands on its child, keeps a step that came due so: it is made as the pair loses that
// ground. (A record of the real game: a pair that lay eight frames with 50/256 of its step fallen - 130 -, kicked
// upright, turned on four frames later, was half a cell lower four frames after that turn, and the move sent then
// over the column beside it, twelve high, came to nothing; with the count gone round at the kick the engine had
// it a row up for seven frames more.)
inline int16_t dueOn(const PairControl& p, int fall) {
    const int total = p.gain + p.groundFrames * fall;
    return int16_t(total >= HALF_STEP ? HALF_STEP - 1 : total);
}

// With Timing::queuedTurnBelow, the first press of a quick turn that was asked for before the pair appeared is kept in
// PairControl::quickWindow: for the first QUEUED_MARK frames of the window it stands that much above
// Timing::quickTurnWindow (stepPair ends the window then and takes no turn for what was left of it).
constexpr int QUEUED_MARK = 11;
inline bool queuedFirst(const PairControl& p, const Timing& t) { return p.quickWindow > t.quickTurnWindow; }

// `queued`: the turn was asked for before the pair appeared and is done in its first frame.
inline void rotate(PairControl& p, const Cells& c, bool clockwise, const Timing& t, int fall, bool queued, ControlEvents& e) {
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
            p.quickWindow = uint8_t(t.quickTurnWindow + (queued && t.queuedTurnBelow ? QUEUED_MARK : 0));
            return;
        } else if (p.dir == UP && queuedFirst(p, t) && c.free(p.x, p.y - 1)) {
            // Timing::queuedTurnBelow, the first press asked for before the pair appeared: the child goes below
            // the axis, which stays where it is.
            p.dir = DOWN;
            p.gain = int16_t(t.quickTurnGain);
            e.quickTurned = true;
        } else if (p.dir == UP) {
            if (p.y + 1 >= ROW_14) return; // the axis piece never enters the 14th row
            // Quick turn, child from above to below: the axis rises one cell and
            // the pair ends half a cell higher than the two cells it had.
            p.y = int8_t(p.y + 1);
            p.dir = DOWN;
            if (t.quickRise > 0) p.upper = false, p.rise = uint8_t(t.quickRise); // (held in the lower half first)
            else if (!t.quickTurnFlip) p.upper = true; // (Timing::quickTurnFlip: up one cell in the same half)
            p.gain = int16_t(t.quickTurnGain);
            p.grounded = false;
            e.quickTurned = true;
        } else {
            // Child from below to above: the axis sinks one cell and the pair
            // ends half a cell lower, when there is room below.
            p.y = int8_t(p.y - 1);
            p.dir = UP;
            if (t.quickTurnFlip) {
                p.upper = true; // (the cell below, its upper half)
            } else if (p.upper) {
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
            // goes below it. The fall restarts from Timing::liftGain (Timing::fallOnGround: of a
            // pair that lay, the count goes on as it was, a step that came due meanwhile kept: dueOn).
            if (p.y + 1 >= ROW_14) return;
            p.y = int8_t(p.y + 1);
            if (t.groundDropReady) p.liftFall = uint8_t(p.grounded ? dueOn(p, fall) : p.gain);
            if (t.floorKickLimit && ++p.landings >= t.maxLandings) p.locking = true;
            // (Timing::liftHolds: a pair that lay makes its next half step in the eighth frame after the kick too - the frame
            //  of the kick, in which it stands on its child, is counted as a frame on the ground)
            p.gain = t.liftHolds && p.grounded ? int16_t(t.liftGain - fall) : t.fallOnGround && p.grounded ? dueOn(p, fall) : int16_t(t.liftGain);
            if (t.liftEndsRest && p.grounded) p.restless = true;
            p.grounded = false; // standing on the child afterwards counts as a new landing
            p.lifted = t.liftWaits;
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
        if (!t.floorKickLimit && ++p.landings >= t.maxLandings) p.locked = true;
    }
    if (down) p.locked = true;
}
} // namespace detail

// Advances the pair by one frame with the buttons held in that frame. (`c`: the cells of the field, made once by whoever
// plays many frames on the same field.)
inline ControlEvents stepPair(PairControl& p, const detail::Cells& c, uint8_t buttons, const Timing& t, int speedPercent = 100) {
    ControlEvents e;
    const int fall = t.fallPerFrame * speedPercent / 100; // (the pair's own fall in a frame, at the speed of the round)
    uint8_t pressed = uint8_t(buttons & ~p.held);
    p.held = buttons;
    // The first frame of a pair with pre-input: what was asked for before it
    // appeared is done now, unless the same kind of button is pressed in this very frame.
    const uint8_t queuedSide = pressed & (BTN_LEFT | BTN_RIGHT) ? uint8_t(0) : uint8_t(p.queued & (BTN_LEFT | BTN_RIGHT));
    const bool queuedTurn = !(pressed & (BTN_CW | BTN_CCW)) && (p.queued & (BTN_CW | BTN_CCW));
    if (queuedTurn) pressed |= uint8_t(p.queued & (BTN_CW | BTN_CCW));
    p.queued = 0;

    if (p.owned && p.ownFrames < 255) ++p.ownFrames;
    const bool wasLifted = p.lifted;
    const bool layBefore = p.grounded && !p.upper && !c.canStepDown(p);
    if (p.quickWindow) {
        --p.quickWindow;
        // (a window marked for a first press asked for before the pair appeared - detail::QUEUED_MARK -, the mark run
        //  out: what a second press does from here on the real game is not known. No turn is taken for the rest of the
        //  window, so that no way is planned through those frames; a press after it begins a quick turn anew.)
        if (p.quickWindow == t.quickTurnWindow) {
            const int rest = t.quickTurnWindow - detail::QUEUED_MARK;
            p.quickWindow = 0;
            if (rest > p.quickLock) p.quickLock = uint8_t(rest < 15 ? rest : 15);
        }
    }
    if (p.quickLock) {
        --p.quickLock;
    } else if (!p.locking) {
        const bool cw = pressed & BTN_CW, ccw = pressed & BTN_CCW;
        if (cw != ccw) {
            // (Timing::liftEndsRest: a turn after the kick lets the clock start anew; a kick up from the floor, in rotate, ends it)
            const bool before = p.restless;
            const int quickBefore = p.quickWindow;
            p.restless = false;
            detail::rotate(p, c, cw, t, fall, queuedTurn, e);
            if (!e.rotated) p.restless = before;
            if (t.quickFirstEndsTurn && !quickBefore && p.quickWindow && !e.rotated) p.turnWait = 0;
        }
    }
    // (the rules of the other model: a turn keeps the down button from locking the pair for a while, a quick turn
    //  makes it fall faster for a while)
    if (e.rotated || e.kicked) {
        if (t.turnLockFrames > 0) p.turnWait = uint8_t(t.turnLockFrames);
        if (e.quickTurned && t.quickFallFrames > 0 && (!t.quickFallUpOnly || p.dir == UP)) p.quickFast = uint8_t(t.quickFallFrames);
        // (Timing::firstOwnLanding: a turn - not a quick one - that leaves the pair no longer lying over its axis's own
        //  column drops the clock of the landing animation: the child turned out from under the pair, say)
        //  (Timing::turnDropsRest: any turn that is no quick turn drops it)
        if (p.owned && !e.quickTurned && (t.turnDropsRest || c.canStepDown(p) || (p.dir == DOWN ? c.free(p.x, p.y - 2) : c.free(p.x, p.y - 1))))
            p.owned = false, p.ownFrames = 0;
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
    if (fire && !p.locking && !p.moveWait && c.free(p.x + want, p.y)) {
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

    if (t.groundDropReady && e.moved && !e.kicked) p.restless = false;

    // During a horizontal move the down key does nothing (two frames per
    // column); the pair keeps falling at its own speed.
    bool down = buttons & BTN_DOWN;
    if (p.moveWait) {
        --p.moveWait;
        down = false;
    }
    // (Timing::quickRise: held in the lower half of the cell a quick turn took the axis up to; when that is over the
    //  pair is in the upper half and falls on from there in this frame)
    bool held = false;
    // (Timing::riseAgain: a pair turned away from child-below is no longer held - it lies or falls as any pair)
    if (t.riseAgain && p.rise && e.rotated && !e.quickTurned && p.dir != DOWN) p.rise = 0;
    if (t.riseYields && down && (p.rise || (e.quickTurned && t.quickRise > 0 && p.dir == DOWN)) && c.canStepDown(p)) {
        // 子が下になる反転で軸が1段上がった後、落下できる場合。実機はこのフレームから
        // 落下の端数を1/8マスずつ戻す。開始時の端数は持ち上がった軸と元の高さの差。
        if (t.quickDownDrag && e.quickTurned && p.rise) {
            p.quickFast = uint8_t(t.quickFallFrames);
            p.gain = HALF_STEP;
        }
        p.rise = 0; // (Timing::riseYields: the down button takes the pair down from where the quick turn left it)
    } else if (e.quickTurned && t.quickRise > 0 && p.dir == DOWN) {
        held = true; // (the frame of the turn: the pair does not fall in it, and it is not one of the frames held)
    } else if (p.rise && t.riseStays && down && !c.canStepDown(p)) {
        // (Timing::riseStays: it lies where it is, in the lower half; riseAgain: the hold begins anew when the button is let go)
        p.rise = t.riseAgain ? uint8_t(t.quickRise + 1) : 0;
    } else if (p.rise) {
        if (--p.rise == 0) p.upper = true;
        else held = true;
    }
    // (Timing::riseCounts: held so, the pair's own fall is counted on)
    if (held && t.riseCounts && t.quickRise > 0 && p.dir == DOWN && (p.rise || e.quickTurned)) {
        const int counted = p.gain + fall;
        p.gain = int16_t(counted < HALF_STEP ? counted : HALF_STEP - 1);
    }
    // (Timing::liftWaits: kicked up from the floor into the air, the pair makes its next half step by itself - the
    //  down button does nothing until then, and the frame of the kick adds nothing to that step. A turn made while it
    //  waits ends the wait: the down button works again, and the fall of the pair's own starts anew.)
    if (wasLifted && e.rotated) p.lifted = false, p.gain = int16_t(t.groundDropReady ? p.liftFall : 0);
    if (p.lifted && c.canStepDown(p)) {
        down = false;
        if (!wasLifted) held = true;
    } else if (!t.liftHolds) {
        p.lifted = false;
    }
    // (Timing::turnOffFalls: a lying pair turned so that nothing is under it falls half a cell in this frame)
    const bool turnedOff = !t.groundDropReady && t.turnOffFalls && layBefore && e.rotated && !e.kicked && !e.quickTurned &&
                           !e.moved && p.dir != DOWN && c.canStepDown(p);
    if (turnedOff) held = false, p.lifted = false;
    // (Timing::turnLockFrames: during a turn's animation the down button does not lock the pair; it still makes it fall)
    const bool mayLock = down && !p.turnWait;
    if (held) {
        // (neither falling nor lying in this frame)
    } else if (buttons & BTN_DROP) {
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
        detail::land(p, mayLock, t, e);
        if (t.groundDropReady && down && !p.lifted) p.gain = HALF_STEP - 1;
        if (!lay && !p.locked) ++p.groundFrames;
        // (Timing::readyOnGround: lying over its axis's own column with the down button held, the pair steps in the
        //  frame it is moved off a ledge)
        if (t.readyOnGround && (buttons & BTN_DOWN) && p.gain < HALF_STEP - 1 &&
            (p.dir == DOWN ? !c.free(p.x, p.y - 2) : !c.free(p.x, p.y - 1)))
            p.gain = int16_t(HALF_STEP - 1);
        if (!p.locked) {
            // The pair locks when it would have fallen lockGain since its last step, had the
            // ground not been there (what was left of that step counts).
            const int fallen = p.gain + p.groundFrames * fall;
            if (fallen >= t.lockGain) {
                if (t.floorKickLimit) p.locking = true;
                else p.locked = true;
            }
        }
    } else if (t.quickFallFrames > 0 && !t.quickFallUpOnly) {
        // (Timing::quickFall: a faster fall for a while from the frame of a quick turn, and as many steps in a frame as
        //  the gain makes)
        p.grounded = false;
        p.gain = int16_t(p.gain + (down ? t.softDropPerFrame : fall) + (p.quickFast ? t.quickFall : 0));
        bool stepped = false;
        while (p.gain >= HALF_STEP && c.canStepDown(p)) {
            p.gain = int16_t(p.gain - HALF_STEP);
            stepped = true;
            p.lifted = false;
            if (p.upper) {
                p.upper = false;
            } else {
                p.y = int8_t(p.y - 1);
                p.upper = true;
                if (down) e.dropBonus = 1;
            }
        }
        if (stepped && !c.canStepDown(p)) {
            if (p.gain >= HALF_STEP) p.gain = int16_t(HALF_STEP - 1);
            detail::land(p, down && mayLock && !t.landThenLock, t, e);
        }
    } else {
        // (Timing::fallOnGround: the count went on while the pair lay, and a step that came due then is made now)
        if (t.fallOnGround && p.grounded) p.gain = detail::dueOn(p, fall);
        p.grounded = false;
        // (Timing::quickFallUpOnly: the faster fall after a quick turn, which may make two half steps in a frame)
        const bool slowing = t.quickDownDrag && p.quickFast && p.dir == DOWN;
        const int faster = t.quickFallUpOnly && p.quickFast && !slowing ? t.quickFall : 0;
        const int softDrop = t.softDropPerFrame - (slowing ? t.quickDownDrag : 0);
        int gain = p.gain + (down ? softDrop : fall) + faster;
        if (gain < HALF_STEP && turnedOff) gain = HALF_STEP;
        p.gain = int16_t(gain < HALF_STEP ? gain : HALF_STEP - 1);
        if (gain >= HALF_STEP) {
            gain -= HALF_STEP;
            if (p.upper) {
                p.upper = false;
            } else {
                p.y = int8_t(p.y - 1);
                p.upper = true;
                if (down) e.dropBonus = 1;
            }
            if (faster && gain >= HALF_STEP && c.canStepDown(p)) {
                gain -= HALF_STEP;
                if (p.upper) {
                    p.upper = false;
                } else {
                    p.y = int8_t(p.y - 1);
                    p.upper = true;
                    if (down) e.dropBonus = 1;
                }
            }
            p.gain = int16_t(gain < HALF_STEP ? gain : HALF_STEP - 1);
            p.lifted = false;
            if (!c.canStepDown(p)) {
                detail::land(p, mayLock && !t.landThenLock, t, e);
            }
        }
    }
    if (!p.lifted) p.liftFall = 0;
    if (p.locking && !p.turnWait) p.locked = true;
    const bool turnWasWaiting = p.turnWait != 0;
    if (p.turnWait) --p.turnWait;
    if (p.quickFast) --p.quickFast;
    // (Timing::firstOwnLanding: the clock of the landing animation starts in the first frame that finds the pair lying
    //  with its axis's own column under it, runs on while the pair lies so or is one half step above where it would,
    //  and is dropped when it is neither)
    if (t.firstOwnLanding) {
        //  (Timing::restBoth: both pieces of a pair on its side must stand on something)
        const auto own = [&](const PairControl& q) {
            if (t.restBoth && (q.dir == LEFT || q.dir == RIGHT)) return !c.free(q.x, q.y - 1) && !c.free(q.childX(), q.y - 1);
            return q.dir == DOWN ? !c.free(q.x, q.y - 2) : !c.free(q.x, q.y - 1);
        };
        const bool lies = !held && !c.canStepDown(p) && own(p);
        bool within = lies; // (lying so, or half a step above where it would)
        if (!lies && p.upper) {
            PairControl q = p;
            q.upper = false;
            within = !c.canStepDown(q) && own(q);
        }
        if (p.restless || (t.groundDropReady && p.lifted && turnWasWaiting)) {
            p.owned = false, p.ownFrames = 0;
        } else if (!p.owned) {
            if (lies) p.owned = true, p.ownFrames = p.locked ? 0 : 1;
        } else if (!within) {
            p.owned = false, p.ownFrames = 0;
        }
    }
    // The points of a lock by the down key (Timing::dropLockPoints, dropLandPoints).
    if (p.locked && down) {
        const int fallen = p.gain + p.groundFrames * fall;
        e.dropBonus += t.dropLockPoints + (fallen < HALF_STEP ? t.dropLandPoints : 0);
    }
    e.locked = p.locked;
    return e;
}

inline ControlEvents stepPair(PairControl& p, const GameField& field, uint8_t buttons, const Timing& t,
                              int speedPercent = 100) {
    const detail::Cells c(field);
    return stepPair(p, c, buttons, t, speedPercent);
}

// A lever of four ways, for whoever reads the buttons of a person and wants no two directions at
// once (the arcade version was played with such a lever; a pad or a keyboard gives down and a direction
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

// The frames of the landing animation a locked pair has behind it (game/player.h takes them off Timing::landFrames):
// those it lay on the ground since its last landing - with Timing::firstOwnLanding, since it first lay with its axis's
// own column under it.
inline int restedFrames(const PairControl& p, const Timing& t) {
    const int frames = t.firstOwnLanding ? (p.owned ? int(p.ownFrames) : 0) : int(p.groundFrames);
    // The frame of the lock is the first frame of the landing animation, after a floor kick as well.
    return t.floorKickLimit && p.locked && frames < 1 ? 1 : frames;
}

// The pair as it appears.
inline PairControl spawnPair(const Timing& t) {
    PairControl p;
    p.gain = int16_t(t.spawnGain);
    return p;
}

} // namespace juraic
