#pragma once
// The buttons with which the turn of a pair is over soonest at a placement, looked for among the buttons of every frame.
//
// What is made short is not the frame of the lock but the end of the landing animation after it: the animation runs
// from the pair's last landing, and at the lock what is left of it is waited for (game/player.h: Timing::landFrames
// less restedFrames(), Timing::lockWait at least). A pair that has lain on the ground before it locks has that time
// behind it, so what it does there - a slide to another column, a turn - costs nothing. With the rules of the other
// model (Timing::landThenLock and those after it) a pair dropped with the down button lies before it locks, and the
// animation runs from the first time it lay over its axis's own column for as long as it stays within half a step of
// lying so: touching a column one row higher on the way and sliding down from it is then the fastest way to a lower one.
//
// InputPlanner's straight way holds the down button, turns at once and taps the direction every second frame. The
// shorter ways this search finds on a field with steps: a turn against a stack or a wall that pushes the pair a column
// aside while it keeps falling (a move stops the down button for two frames, a push does not), a pair moved or turned
// on the ground before the down button locks it, a pair turned upside down in a groove (the axis sinks with the turn).
//
// Every frame is made with stepPair itself, so the buttons played from the same state lock the pair in their last
// frame. A direction and a turn are pressed for one frame (one held from the frame before is let go first; a turn
// of the other button may follow in the next frame), the down button is held or not in any frame - with the rules of
// the other model it is held in every frame that begins with the pair in the air.
//
// With the rules read off records of the real game (Timing::restBoth and turnDropsRest; game/timing.h) the animation runs
// only while the pair lies with both pieces standing on something, a turn drops it and a pair moved off a ledge loses
// it: what is left is the pair slid along level ground before the down button locks it (two frames a column, all of
// them behind it at the lock, eight at most), and the quick turn in a groove. A quick turn that takes the child from
// above to below is left out of the ways then (Timing::quickRise: where the pair is while it is held is known, what
// can be done with it meanwhile is not).
//
// Order: the soonest end; of the ways that end as soon, the one that locks the pair soonest (a way that lets the pair
// lie longer than it must gains nothing by the rules and loses those frames should the game count otherwise); then
// those reached with fewer presses.
// What a state still needs at least (ahead()):
//  - to the lock (least()): a column changed by a move costs two frames without the down button - unless a turn can
//    push the pair that way from the column (a wall or a higher stack on the other side); the axis comes down one half
//    step a frame at most, by the fall; a quick turn in a groove alone takes it down three more at once (child from
//    below to above), which only a placement there with the pair upright can use; then Timing::lockWait;
//  - or the landing animation from the landing it runs from: the last one, which finds the axis in the row it rests
//    in at the placement - with Timing::firstOwnLanding a lying over its own column in a column from which the ground
//    steps down to the placement.
#include "../game/control.h"
#include <algorithm>
#include <atomic>
#include <queue>
#include <unordered_map>
#include <vector>

namespace juraic::ai {

class FastestSearch {
public:
    int maxStates = 50000; // states taken from the queue before the search gives up
    // No quick turn, and the axis never in the 13th row (InputPlanner::keepLow).
    bool low = false;
    int states = 0;  // states the last search took from the queue
    bool gaveUp = false; // the last search stopped at maxStates (it found no way, and there may be one)
    // Set from another thread, the search gives up at once (a search made ahead whose pair is no longer the one to come).
    const std::atomic<bool>* stop = nullptr;

    // What a search from `from` counts on at least: no way it finds ends sooner (InputPlanner::prepareFastest begins no
    // search when the straight way ends no later; the tests: how far below the end of the way it finds).
    int estimate(const GameField& field, const PairControl& from, Placement target, const Timing& timing, int speedPercent) {
        setUp(field, target, timing, speedPercent);
        return ahead(from);
    }

    // The frames from `from` to the end of the landing animation of the pair that `buttons` lock (0: they lock none).
    static int ends(const GameField& field, PairControl p, const std::vector<uint8_t>& buttons, const Timing& timing, int speedPercent) {
        for (const uint8_t b : buttons) {
            if (p.locked) return 0;
            stepPair(p, field, b, timing, speedPercent);
        }
        return p.locked ? int(buttons.size()) + after(p, timing) : 0;
    }

    // A way from `from` (PairControl::held = the buttons of the frame before) that locks the pair at `target` and whose
    // landing animation is over `limit` frames from `from` at most: its buttons, one a frame up to the lock, the points
    // of the down key on it and that end. false: there is none, or the search gave up.
    bool find(const GameField& field, const PairControl& from, Placement target, const Timing& timing, int speedPercent, int limit,
              std::vector<uint8_t>& buttons, int& softDropRows, int& end) {
        states = 0;
        gaveUp = false;
        if (from.locked || limit <= 0) return false;
        setUp(field, target, timing, speedPercent);
        const detail::Cells cells(field);
        nodes.clear(), best.clear();
        std::priority_queue<uint64_t, std::vector<uint64_t>, std::greater<uint64_t>> open;
        const auto push = [&](const PairControl& p, int parent, int frames, int taps, uint8_t held, bool goal) {
            const int total = frames + (goal ? after(p, timing) : ahead(p));
            if (total > limit) return;
            // (the frame of the lock: that of a way found, else what the state needs at least)
            const int lock = goal ? frames : frames + least(p);
            if (!goal) {
                const auto [at, fresh] = best.try_emplace(key(p), int(nodes.size()));
                if (!fresh) {
                    const Node& old = nodes[size_t(at->second)];
                    if (old.frames < frames || (old.frames == frames && old.taps <= taps)) return;
                    at->second = int(nodes.size());
                }
            }
            nodes.push_back({p, parent, uint16_t(frames), uint16_t(taps), held, goal});
            open.push(uint64_t(total & 0xfff) << 50 | uint64_t(lock & 0x3ff) << 40 | uint64_t(taps & 0xff) << 32 | uint64_t(nodes.size() - 1));
        };
        push(from, -1, 0, 0, 0, false);
        int found = -1;
        while (!open.empty()) {
            const int n = int(open.top() & 0xffffffffu);
            open.pop();
            const Node node = nodes[size_t(n)]; // (a copy: the nodes grow below)
            if (node.goal) {
                found = n;
                break;
            }
            if (best.find(key(node.p))->second != n) continue; // (a better way to this state came after it)
            if (++states > maxStates) return gaveUp = true, false;
            if (stop && (states & 127) == 0 && stop->load(std::memory_order_relaxed)) return gaveUp = true, false;
            for (const uint8_t side : SIDES) {
                // (a direction held is let go first; during a move none is taken)
                if (side && ((node.p.held & side) || node.p.moveWait)) continue;
                for (const uint8_t turn : TURNS) {
                    // (a turn held is let go first; during a quick turn's lock none is taken)
                    if (turn && ((node.p.held & turn) || node.p.quickLock)) continue;
                    for (const uint8_t down : DOWNS) {
                        // (with the rules of the other model the down button is always held in the air: let go there, the
                        //  pair only comes down later)
                        if (!down && !node.p.grounded && !hover) continue;
                        const uint8_t b = uint8_t(side | turn | down);
                        PairControl q = node.p;
                        const ControlEvents e = stepPair(q, cells, b, timing, speedPercent);
                        if (low && (e.quickTurned || q.y >= ROW_14 - 1)) continue;
                        if (noRise && e.quickTurned && q.dir == DOWN) continue;
                        const int taps = node.taps + (side ? 1 : 0) + (turn ? 1 : 0);
                        if (!q.locked) push(q, n, node.frames + 1, taps, b, false);
                        else if (q.x == target.x && q.dir == target.dir) push(q, n, node.frames + 1, taps, b, true);
                    }
                }
            }
        }
        if (found < 0) return false;
        buttons.assign(size_t(nodes[size_t(found)].frames), 0);
        for (int n = found; nodes[size_t(n)].parent >= 0; n = nodes[size_t(n)].parent) buttons[size_t(nodes[size_t(n)].frames) - 1] = nodes[size_t(n)].held;
        // (played once more from the start: the points of the down key, and the way must lock the pair there)
        PairControl p = from;
        softDropRows = 0;
        for (const uint8_t b : buttons) {
            if (p.locked) return false;
            softDropRows += stepPair(p, field, b, timing, speedPercent).dropBonus;
        }
        end = int(buttons.size()) + after(p, timing);
        return p.locked && p.x == target.x && p.dir == target.dir;
    }

private:
    static constexpr uint8_t SIDES[3] = {0, BTN_LEFT, BTN_RIGHT};
    static constexpr uint8_t TURNS[3] = {0, BTN_CW, BTN_CCW};
    static constexpr uint8_t DOWNS[2] = {BTN_DOWN, 0};
    struct Node {
        PairControl p;
        int parent;
        uint16_t frames, taps;
        uint8_t held; // the buttons of the frame that led here
        bool goal;    // locked at the target
    };
    std::vector<Node> nodes;
    struct Key {
        uint64_t a, b;
        bool operator==(const Key& o) const { return a == o.a && b == o.b; }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const { return size_t((k.a ^ (k.b + 0x9E3779B97F4A7C15ull)) * 0x9E3779B97F4A7C15ull >> 16); }
    };
    std::unordered_map<Key, int, KeyHash> best; // a state's node of the fewest frames (then presses) so far
    int goalX = 0, restY = 0, sunk = 0, natural = 0, landFrames = 0, lockWait = 0;
    int moveLost = 2;       // frames without the down button a move costs at least
    bool floorLimited = false; // Timing::floorKickLimit: the clock of a lock by itself is part of a state
    bool firstOwn = false;  // Timing::firstOwnLanding
    bool hover = true;      // the down button may be let go in the air (not with Timing::landThenLock)
    int turnLock = 0;       // Timing::turnLockFrames
    int grooveLevel = 0;    // the axis is between the neighbours of the target column from this level down (half steps from the floor)
    bool level = false;     // Timing::restBoth and turnDropsRest: the clock that counts at the lock began in the row the pair rests in
    bool splits = false;    // ... and the placement leaves a piece to fall on: no clock counts for it
    bool noRise = false;    // no quick turn that takes the child from above to below (Timing::quickRise)
    bool landLock = false;  // Timing::landThenLock
    int maxLandings = 8;    // Timing::maxLandings
    bool slowDownTurn = false;
    int quickFall = 0;      // Timing::quickFall
    uint8_t goalDir = UP;   // the direction of the target
    int ownTop[W] = {};     // the highest the axis can lie over its own column in each column, in half steps from the floor (quick turns counted)
    bool nearby[W] = {};      // ... from where the ground steps down to the target, so that a clock begun there can run on to the lock
    bool run[W] = {};         // `level`: the columns of the axis from which the pair, turned as at the target, slides to it on level ground
    bool pushed[2][W] = {}; // [to the right][column]: a turn can push the pair a column that way from the column

    // The frames of the landing animation left when `p` has locked (game/player.h).
    static int after(const PairControl& p, const Timing& timing) { return std::max(timing.landFrames - restedFrames(p, timing), timing.lockWait); }

    // (every field of a pair on the way that what follows depends on: `queued` and `repeat` are 0 from the second frame
    //  on - a direction is never held -, the down button of the frame before changes nothing, and with
    //  Timing::firstOwnLanding the frames the pair has lain count only for the lock of a pair left alone for long - a way
    //  is played once more before it is given, and dropped when it does not lock the pair where it should)
    Key key(const PairControl& p) const {
        uint64_t k = uint64_t(p.x) & 7;
        const auto add = [&](uint64_t value, int bits) { k = k << bits | (value & ((uint64_t(1) << bits) - 1)); };
        add(uint64_t(p.y), 4), add(p.dir, 2), add(p.upper, 1), add(p.grounded, 1), add(uint64_t(p.gain), 8), add(p.moveWait, 2);
        add(p.landings, 4), add(firstOwn && !floorLimited ? 0 : p.groundFrames, 8), add(p.quickWindow, 6);
        add(p.held & (BTN_LEFT | BTN_RIGHT | BTN_CW | BTN_CCW), 6);
        add(p.turnWait, 3), add(p.quickFast, 4), add(p.owned, 1), add(p.ownFrames, 8);
        return {k, uint64_t(p.lifted) | uint64_t(p.rise) << 1 | uint64_t(p.restless) << 9 | uint64_t(p.liftFall) << 10 | uint64_t(p.locking) << 18 |
                   uint64_t(p.quickLock) << 19};
    }

    void setUp(const GameField& field, Placement target, const Timing& timing, int speedPercent) {
        int h[W];
        field.heights(h);
        const int x = target.x, cx = target.childX();
        goalX = x;
        // (the row of the axis when the pair locks there: place() of game/field.h)
        restY = target.dir == UP ? h[x] : target.dir == DOWN ? h[x] + 1 : std::max(h[x], h[cx]);
        natural = timing.fallPerFrame * speedPercent / 100;
        landFrames = timing.landFrames, lockWait = timing.lockWait;
        firstOwn = timing.firstOwnLanding;
        floorLimited = timing.floorKickLimit != 0;
        hover = !timing.landThenLock;
        turnLock = timing.turnLockFrames;
        goalDir = target.dir;
        // (Timing::readyOnGround: a pair moved off a ledge steps in the frame of the move)
        moveLost = timing.readyOnGround ? 1 : 2;
        const auto wall = [&](int c, int over) { return c < 0 || c >= W || h[c] > over || field.row14[size_t(c)]; };
        for (int c = 0; c < W; ++c) pushed[1][c] = wall(c - 1, h[c]), pushed[0][c] = wall(c + 1, h[c]);
        // The half steps quick turns can take the axis down, beyond its fall, in column c above the row `rest`: only
        // in a groove there, and no more than the groove is deep. One quick turn: three, and what its faster fall adds
        // (Timing::quickFall); a turn back and another one: three more in eight frames at the soonest.
        const bool other = timing.quickTurnFlip || timing.quickFallFrames > 0;
        const int faster = timing.quickFallFrames > 0 ? timing.quickFall * timing.quickFallFrames / HALF_STEP : 0;
        const auto top = [&](int n) { return n < 0 || n >= W || field.row14[size_t(n)] ? ROW_14 + 1 : h[n]; };
        const auto quick = [&](int c, int rest) {
            const int depth = 2 * (std::min(top(c - 1), top(c + 1)) - rest);
            return low || depth <= 0 ? 0 : std::min(depth, 3 + faster + (other ? 3 * (depth / 8) : 0));
        };
        sunk = target.dir == UP ? quick(x, restY) : target.dir == DOWN ? (other ? quick(x, restY) : std::min(1, quick(x, restY))) : 0;
        // (Timing::firstOwnLanding: where the pair can lie over its own column - with its child below it at the highest;
        //  a quick turn in a groove, made on the way down, has it lie there two frames sooner at the most -, and whether
        //  the clock that starts there can run on to the target: the pair must stay within half a step of lying all the
        //  way and a turn that changes how it lies drops the clock, so from column to column the ground may go down one
        //  row at the most, and not up)
        for (int c = 0; c < W; ++c) {
            ownTop[c] = 2 * (h[c] + 1) + std::min(2, quick(c, h[c]));
            nearby[c] = true;
            for (int k = c; k != goalX; k += k < goalX ? 1 : -1) {
                const int drop = h[k] - h[k < goalX ? k + 1 : k - 1];
                if (drop > 1 || drop < 0) nearby[c] = false;
            }
        }
        // (the axis is in the groove of the target column from this level down)
        grooveLevel = 2 * std::min(top(x - 1), top(x + 1)) - 1;
        level = firstOwn && timing.restBoth && timing.turnDropsRest;
        splits = timing.restBoth && (target.dir == LEFT || target.dir == RIGHT) && h[x] != h[cx];
        // (the columns the pair lies in as it lies at the target, with both pieces standing in the same row, and no
        //  column between that is otherwise: the clock that counts at the lock began in one of them)
        {
            const int side = cx - x; // (0: upright)
            const auto lies = [&](int c) {
                if (c < 0 || c >= W || c + side < 0 || c + side >= W) return false;
                return h[c] == h[x] && h[c + side] == h[cx] && !field.row14[size_t(c)] && !field.row14[size_t(c + side)];
            };
            for (int c = 0; c < W; ++c) run[c] = false;
            for (int c = x; lies(c); ++c) run[c] = true;
            for (int c = x; lies(c); --c) run[c] = true;
            run[x] = true;
        }
        noRise = timing.quickRise > 0;
        landLock = timing.landThenLock;
        maxLandings = timing.maxLandings;
        slowDownTurn = timing.quickDownDrag != 0;
        quickFall = timing.quickFallFrames > 0 ? timing.quickFall : 0;
    }

    // The half steps the axis of `p` is above the row it rests in at the target (less those a quick turn can take).
    int above(const PairControl& p) const { return 2 * (p.y - restY) + (p.upper ? 1 : 0) - sunk; }

    // The frames `p` still needs at least before it locks at the target.
    int least(const PairControl& p) const {
        int lost = p.moveWait; // frames without the down button: of the move going on, and of the moves to come
        for (int c = p.x; c != goalX; c += c < goalX ? 1 : -1)
            if (!pushed[c < goalX][c]) lost += moveLost;
        const int byItself = std::min(lost, (p.gain + natural * lost) / HALF_STEP); // (half steps of the fall in those frames)
        const int level = 2 * p.y + (p.upper ? 1 : 0), raw = level - 2 * restY;
        // Without another quick turn: one half step a frame, and what is left of the faster fall of a quick turn made.
        const int acceleration = p.dir == DOWN && slowDownTurn ? 0 : int(p.quickFast) * quickFall;
        int frames = lost + std::max(0, raw - (p.gain + acceleration) / HALF_STEP - byItself);
        // With a quick turn still to make: it takes the axis down `sunk` more at the most, is made in the groove, a
        // frame after the axis is in it at the soonest, and keeps the pair from locking while its animation runs.
        if (sunk > 0) {
            const int enter = lost + std::max(0, level - grooveLevel - byItself);
            frames = std::min(frames, std::max(lost + std::max(0, raw - sunk - byItself), turnLock > 0 ? enter + 1 + turnLock : 0));
        }
        // (Timing::landThenLock: a pair still above where it rests lands in one frame and locks in a later one - but for
        //  the landing that locks it at once, Timing::maxLandings)
        if (landLock && raw > 0 && int(p.landings) + 1 < maxLandings) ++frames;
        frames = std::max(1, frames);
        // (Timing::turnLockFrames: no lock while a turn's animation runs - of the turn made, and of the one still to
        //  make when the pair is not turned as at the target)
        if (turnLock > 0) {
            frames = std::max(frames, int(p.turnWait) + 1);
            if (p.dir != goalDir) frames = std::max(frames, turnLock + 1);
        }
        return frames;
    }

    // The frames `p` still needs at least before it lies over its axis's own column in a column the clock can run on from.
    int soonest(const PairControl& p) const {
        int best = 1 << 20;
        const int level = 2 * p.y + (p.upper ? 1 : 0);
        for (int c = 0; c < W; ++c) {
            if (!nearby[c]) continue;
            int lost = p.moveWait;
            for (int k = p.x; k != c; k += k < c ? 1 : -1)
                if (!pushed[k < c][k]) lost += moveLost;
            const int byItself = std::min(lost, (p.gain + natural * lost) / HALF_STEP);
            best = std::min(best, lost + std::max(0, level - ownTop[c] - byItself));
        }
        return std::max(1, best);
    }

    // `level`: the frames `p` still needs at least before it lies in the row it rests in at the target, in a column it
    // can slide to the target from on level ground. A move in the air costs two frames without the down button, the last
    // one before it lies one (a pair moved onto a ledge in the frame it comes level with it lies in that frame).
    int soonestLevel(const PairControl& p) const {
        int best = 1 << 20;
        for (int c = 0; c < W; ++c) {
            if (!run[c]) continue;
            int lost = 0;
            for (int k = p.x; k != c; k += k < c ? 1 : -1)
                if (!pushed[k < c][k]) lost += moveLost;
            if (lost > 0) --lost;
            lost += p.moveWait;
            const int byItself = std::min(lost, (p.gain + natural * lost) / HALF_STEP);
            best = std::min(best, lost + std::max(0, above(p) - byItself));
        }
        return std::max(1, best);
    }

    // The frames `p` still needs at least before its landing animation at the target is over.
    int ahead(const PairControl& p) const {
        // The landing animation: running already, what is left of it; else all of it from the landing it runs from,
        // which is still to come (a pair that lies a frame before it locks has one frame more of it behind it).
        int landing;
        if (level) {
            // (the rules of the records: a pair that locks with a piece to fall on has no clock behind it; else the
            //  clock that counts runs with the pair lying in the row it rests in at the placement - it is dropped by a
            //  step down and by a turn, and a quick turn in a groove takes the axis down `sunk` half steps with it.
            //  Running there already, what is left of it; else all of it from a lying still to come.)
            if (splits) return least(p) + landFrames;
            landing = p.owned && above(p) <= 0 && run[p.x] ? landFrames - int(p.ownFrames) : landFrames - 1 + soonestLevel(p);
        } else if (firstOwn) {
            // (Timing::firstOwnLanding. The clock running now may run on to the lock when the pair is in a column from
            //  which the ground steps down to the target; else the clock that does starts with a lying over its own
            //  column that is still to come, in such a column.)
            landing = p.owned && nearby[p.x] ? landFrames - int(p.ownFrames) : landFrames - 1 + soonest(p);
        } else {
            // (from the last landing, which finds the axis in the row it rests in at the target)
            const bool lies = p.grounded && !p.upper && p.y == restY;
            landing = lies ? landFrames - int(p.groundFrames) : landFrames - 1 + std::max(1, above(p));
        }
        return std::max(least(p) + lockWait, landing);
    }
};

} // namespace juraic::ai
