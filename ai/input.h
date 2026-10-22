#pragma once
// From a placement to buttons: the button of every frame that brings the pair
// from where it is now to the placement and locks it there as early as possible.
//
// 1. The straight way (the fastest input of the Timing tables): hold down,
//    rotate at once, tap the direction every second frame. It is played on a
//    copy of the pair with stepPair; when nothing gets in its way, that is the
//    answer and nothing is searched.
// 2. Otherwise (a column of 12 on the way, a floor kick or a quick turn is
//    needed, the pair is already somewhere else) a search: without the down
//    key the pair is moved by taps (a frame with buttons, a frame without) and
//    one-frame waits, the states with the fewest frames first; from every state
//    the straight way is tried as the rest of the path. The number of states is
//    limited; when the limit is reached the best path found so far is given,
//    or none.
//
// The path is exact: it is made with stepPair itself, so playing its buttons
// from the same state locks the pair in its last frame.
//
// tapHold above 1: every tap is pressed that many frames and let go as many
// before the next (for a game played through a pad whose frames now and then
// take the buttons of the frame before or after: a tap is then neither lost
// nor run into the next one; below the auto-repeat's delay).
#include "../game/control.h"
#include <algorithm>
#include <functional>
#include <vector>

namespace juraic::ai {

struct InputPath {
    std::vector<uint8_t> buttons; // Button bits, one per frame; the pair locks in the last frame
    int softDropRows = 0;         // rows of soft drop on the way (the drop bonus)
    int states = 0;               // states the search made (0 = the straight way)
    int frames() const { return int(buttons.size()); }
};

class InputPlanner {
public:
    int maxStates = 1024;
    int tapHold = 1; // frames a tap is pressed, and let go before the next (1: the fastest)
    // With tapHold above 1: the frames each kind of tap held as the way begins (turns, moves) has been held so far; it
    // is held on to tapHold frames before it is let go (0: as long as a tap is - let go at once).
    int heldFor[2] = {0, 0};
    // With tapHold above 1: the down button is held only once the pair is turned and moved where it goes (the pair
    // falls by itself until then, and can still be moved as it lies before it locks).
    bool downWhenPlaced = false;
    // No way takes the pair's axis up into the 13th row (a floor kick or a quick turn that lifts it there) nor turns the
    // pair quickly, and no way locks it with its axis there; a pair that is up there already is planned as it may.
    bool keepLow = false;
    // No turn is pressed while a quick turn's lock lasts (PairControl::quickLock): a game that takes turns again sooner
    // than Timing::quickTurnLock says does not take a turn the way did not count on.
    bool waitQuickLock = false;
    // planSure: the cells of the 13th and 14th rows that may be the other way (columns, a bit each), and the frames after
    // the pair is shown elsewhere than a way has it by which a way from where it is then begins.
    uint8_t unsure13 = 0, unsure14 = 0;
    int seenAfter = 0;
    int64_t sureWays = 0, steadyWays = 0, probingWays = 0, seenWays = 0, blindWays = 0; // planSure's ways of each kind (for tools)

    // A way to `target` when the cells unsure13 and unsure14 may be the other way (each of them on a field of its own):
    //  - plan()'s way, when it locks the pair at the target on each of those fields too (sureWays);
    //  - else such a way planned on one of those fields (steadyWays);
    //  - else a way with which, on each field where it locks the pair elsewhere, the pair is shown otherwise than on the
    //    field given (a piece of it in the twelve rows a screen shows) early enough that a way from where it is seenAfter
    //    frames later reaches the target there: plan()'s way, one planned on one of those fields, or one that begins
    //    with a tap that shows it (probingWays);
    //  - else one with which it is shown otherwise before it locks, to go elsewhere from there (seenWays: the target out
    //    of its reach on such a field);
    //  - else plan()'s way (blindWays).
    bool planSure(const GameField& field, const PairControl& from, Placement target, const Timing& timing, int speedPercent, InputPath& out) {
        if (!plan(field, from, target, timing, speedPercent, out)) return false;
        if (!unsure13 && !unsure14) return true;
        std::vector<GameField> others;
        // (not a cell the pair is in: it is empty)
        const auto in = [&](int x, int y) { return (from.x == x && from.y == y) || (from.childX() == x && from.childY() == y); };
        for (int x = 0; x < W; ++x) {
            if ((unsure13 >> x & 1) && !in(x, ROW_13)) {
                GameField g = field;
                g.f.set(x, ROW_13, g.f.get(x, ROW_13) == EMPTY ? GARBAGE : EMPTY);
                others.push_back(g);
            }
            if ((unsure14 >> x & 1) && !in(x, ROW_13 + 1)) {
                GameField g = field;
                g.row14[size_t(x)] = g.row14[size_t(x)] == EMPTY ? GARBAGE : EMPTY;
                others.push_back(g);
            }
        }
        const auto locksAt = [&](const GameField& g, const InputPath& way) {
            PairControl p = from;
            return lands(g, p, way, timing, speedPercent) && p.x == target.x && p.dir == target.dir;
        };
        const auto steady = [&](const InputPath& way) {
            for (const GameField& g : others)
                if (!locksAt(g, way)) return false;
            return true;
        };
        if (steady(out)) return ++sureWays, true;
        std::vector<InputPath> ways{out};
        for (const GameField& g : others) {
            InputPath way;
            if (plan(g, from, target, timing, speedPercent, way) && locksAt(field, way)) ways.push_back(std::move(way));
        }
        const auto shortest = [&](auto&& good) {
            int best = -1;
            for (size_t i = 0; i < ways.size(); ++i)
                if (good(ways[i]) && (best < 0 || ways[i].frames() < ways[size_t(best)].frames())) best = int(i);
            return best;
        };
        if (const int best = shortest(steady); best >= 0) return out = ways[size_t(best)], ++steadyWays, true;
        // (a tap first that may show it: as long as a tap is, let go as long)
        for (const uint8_t tap : {BTN_LEFT, BTN_RIGHT, BTN_CW, BTN_CCW}) {
            const bool turn = tap & (BTN_CW | BTN_CCW);
            if ((from.held & tap) || (turn && waitQuickLock && from.quickLock)) continue;
            InputPath way, rest;
            PairControl p = from;
            for (int k = 0; k < 2 * tapHold && !p.locked; ++k) {
                way.buttons.push_back(k < tapHold ? tap : 0);
                stepPair(p, field, way.buttons.back(), timing, speedPercent);
            }
            if (p.locked || !plan(field, p, target, timing, speedPercent, rest)) continue;
            way.buttons.insert(way.buttons.end(), rest.buttons.begin(), rest.buttons.end());
            way.softDropRows = rest.softDropRows;
            if (locksAt(field, way)) ways.push_back(std::move(way));
        }
        // (how a way shows the cells: 2 every field it goes astray on early enough to reach the target, 1 before it
        //  locks, 0 not every one of them)
        const auto shows = [&](const InputPath& way) {
            int least = 2;
            for (const GameField& g : others)
                if (!locksAt(g, way)) least = std::min(least, shownApart(field, g, from, way, target, timing, speedPercent));
            return least;
        };
        int best = 0, kind = shows(ways[0]);
        for (size_t i = 1; i < ways.size(); ++i)
            if (const int k = shows(ways[i]); k > kind || (k == kind && ways[i].frames() < ways[size_t(best)].frames())) best = int(i), kind = k;
        out = ways[size_t(best)];
        return ++(kind == 2 ? probingWays : kind == 1 ? seenWays : blindWays), true;
    }

    // `from` is the pair before the first frame of the path (PairControl::held =
    // the buttons of the frame before). For a pair that is about to appear:
    // spawnPair(timing) with held set.
    bool plan(const GameField& field, const PairControl& from, Placement target, const Timing& timing, int speedPercent,
              InputPath& out) {
        out.buttons.clear();
        out.softDropRows = 0;
        out.states = 0;
        if (from.locked) return false;
        low = keepLow && from.y < ROW_13;

        bool clean;
        int soft;
        int best = finish(field, from, target, timing, speedPercent, clean, soft, nullptr); // frames of the best path, 0 = none
        int bestNode = -1; // the state the best path leaves the search from; -1 = the straight way from `from`
        if (!best || !clean) {
            search(field, from, target, timing, speedPercent, best, bestNode);
            out.states = int(nodes.size());
        }
        if (!best) return false;

        // The buttons: back from the state to the start, then the straight way.
        PairControl p = from;
        if (bestNode >= 0) {
            macros.clear();
            for (int n = bestNode; n > 0; n = nodes[size_t(n)].parent) macros.push_back(nodes[size_t(n)].macro);
            const auto press = [&](uint8_t b) {
                if (p.locked) return;
                out.buttons.push_back(b);
                stepPair(p, field, b, timing, speedPercent);
            };
            if (rootRelease)
                for (int k = 0; k < tapHold; ++k) press(0);
            for (size_t i = macros.size(); i-- > 0;) {
                if (macros[i] == WAIT) {
                    press(0);
                    continue;
                }
                for (int k = 0; k < tapHold; ++k) press(TAPS[macros[i]]);
                for (int k = 0; k < tapHold; ++k) press(0);
            }
        }
        if (!p.locked) finish(field, p, target, timing, speedPercent, clean, out.softDropRows, &out.buttons);
        return true;
    }

private:
    static constexpr int MAX_FRAMES = 600;
    static constexpr uint8_t TAPS[8] = {BTN_LEFT,           BTN_RIGHT,           BTN_CW,              BTN_CCW,
                                        BTN_LEFT | BTN_CW,  BTN_LEFT | BTN_CCW,  BTN_RIGHT | BTN_CW,  BTN_RIGHT | BTN_CCW};
    static constexpr uint8_t WAIT = 8;
    static constexpr uint8_t TAP_BUTTONS = BTN_LEFT | BTN_RIGHT | BTN_CW | BTN_CCW;
    static constexpr int ROW_13 = 12; // (row index)
    bool low = false; // keepLow for the way being planned

    // waitQuickLock: a turn pressed now would not be taken.
    bool locked(const PairControl& p) const { return waitQuickLock && p.quickLock; }

    // A step that keepLow forbids: a quick turn, or the axis lifted into the 13th row.
    bool tooHigh(const ControlEvents& e, const PairControl& p) const { return low && (e.quickTurned || p.y >= ROW_13); }

    // The buttons of `way` played with the pair `p` on `g`, then the down button: true when it locks.
    static bool lands(const GameField& g, PairControl& p, const InputPath& way, const Timing& timing, int speedPercent) {
        for (size_t i = 0; i < way.buttons.size() && !p.locked; ++i) stepPair(p, g, way.buttons[i], timing, speedPercent);
        for (int k = 0; k < MAX_FRAMES && !p.locked; ++k) stepPair(p, g, BTN_DOWN, timing, speedPercent);
        return p.locked;
    }
    // Where a screen of twelve rows shows the pieces of the pair (none of a piece in the 13th or 14th row).
    static bool shownAlike(const PairControl& a, const PairControl& b) {
        const bool axis = a.y < ROW_13, child = a.childY() < ROW_13;
        if (axis != (b.y < ROW_13) || child != (b.childY() < ROW_13)) return false;
        if ((axis || child) && (a.upper != b.upper || a.locked != b.locked)) return false;
        return (!axis || (a.x == b.x && a.y == b.y)) && (!child || (a.childX() == b.childX() && a.childY() == b.childY()));
    }
    // planSure: `way` played on `field` and on `g`, where it locks the pair elsewhere: 2 when the pair is shown otherwise
    // on `g` than on `field` in a frame from which, seenAfter frames later (the way played on there), a way reaches the
    // target; 1 when it is shown otherwise before it locks; 0 not.
    int shownApart(const GameField& field, const GameField& g, const PairControl& from, const InputPath& way, Placement target, const Timing& timing,
                    int speedPercent) {
        PairControl a = from, b = from;
        const auto button = [&](size_t i) { return i < way.buttons.size() ? way.buttons[i] : uint8_t(BTN_DOWN); };
        size_t i = 0;
        for (; i < size_t(MAX_FRAMES) && !b.locked; ++i) {
            if (!a.locked) stepPair(a, field, button(i), timing, speedPercent);
            stepPair(b, g, button(i), timing, speedPercent);
            if (!shownAlike(a, b)) break;
        }
        if (b.locked) return 0;
        for (int k = 0; k < seenAfter && !b.locked; ++k) stepPair(b, g, button(++i), timing, speedPercent);
        InputPath rest;
        return !b.locked && plan(g, b, target, timing, speedPercent, rest) ? 2 : 1;
    }

    struct Node {
        PairControl pair;
        int parent;
        int frames; // from the start
        uint8_t macro;
    };
    std::vector<Node> nodes;
    std::vector<uint64_t> open; // frames (with the columns still to go) << 32 | node: the smallest first
    std::vector<uint64_t> seenKey;
    std::vector<uint32_t> seenStamp;
    std::vector<uint8_t> macros;
    uint32_t stamp = 0;
    bool rootRelease = false;

    static uint8_t turnButton(uint8_t dir, uint8_t want) {
        const int d = (want - dir) & 3;
        return d == 0 ? 0 : d == 3 ? BTN_CCW : BTN_CW;
    }

    // The straight way from p: returns the frames until the pair locks at the
    // target, 0 when it locks elsewhere. clean = nothing got in its way.
    int finish(const GameField& field, PairControl p, Placement target, const Timing& timing, int speedPercent, bool& clean, int& soft,
               std::vector<uint8_t>* buttons) const {
        clean = true;
        soft = 0;
        if (tapHold > 1) return finishHeld(field, p, target, timing, speedPercent, clean, soft, buttons);
        for (int frame = 1; frame <= MAX_FRAMES; ++frame) {
            uint8_t b = BTN_DOWN;
            const uint8_t turn = turnButton(p.dir, target.dir);
            if (turn && !(p.held & turn) && !locked(p)) b |= turn;
            const uint8_t side = p.x < target.x ? BTN_RIGHT : p.x > target.x ? BTN_LEFT : 0;
            if (side && !(p.held & side)) b |= side;
            const ControlEvents e = stepPair(p, field, b, timing, speedPercent);
            if (((b & (BTN_CW | BTN_CCW)) && !e.rotated) || ((b & (BTN_LEFT | BTN_RIGHT)) && !e.moved) || e.kicked || e.quickTurned)
                clean = false;
            if (tooHigh(e, p)) return 0;
            soft += e.dropBonus;
            if (buttons) buttons->push_back(b);
            if (e.locked) return p.x == target.x && p.dir == target.dir ? frame : 0;
        }
        return 0;
    }

    // finish with taps of tapHold frames, let go as many frames between them.
    int finishHeld(const GameField& field, PairControl p, Placement target, const Timing& timing, int speedPercent, bool& clean, int& soft,
                   std::vector<uint8_t>* buttons) const {
        constexpr uint8_t KIND[2] = {BTN_CW | BTN_CCW, BTN_LEFT | BTN_RIGHT};
        int on[2] = {0, 0}, off[2] = {tapHold, tapHold}; // frames each kind of tap has been pressed / let go in a row
        for (int k = 0; k < 2; ++k)
            if (p.held & KIND[k]) on[k] = heldFor[k] > 0 ? std::min(heldFor[k], tapHold) : tapHold, off[k] = 0; // (a tap held as the way begins: held to its end, then let go)
        for (int frame = 1; frame <= MAX_FRAMES; ++frame) {
            const uint8_t want[2] = {turnButton(p.dir, target.dir), uint8_t(p.x < target.x ? BTN_RIGHT : p.x > target.x ? BTN_LEFT : 0)};
            uint8_t b = downWhenPlaced && (want[0] || want[1]) ? 0 : BTN_DOWN;
            for (int k = 0; k < 2; ++k) {
                if (p.held & KIND[k]) {
                    if (on[k] < tapHold) b |= uint8_t(p.held & KIND[k]);
                } else if (want[k] && off[k] >= tapHold && !(k == 1 && (b & ~p.held & KIND[0])) && !(k == 0 && locked(p))) {
                    // (a move does not begin in the frame a turn begins: which the game does first is not known)
                    b |= want[k];
                }
            }
            const uint8_t pressed = uint8_t(b & ~p.held);
            const ControlEvents e = stepPair(p, field, b, timing, speedPercent);
            if (((pressed & KIND[0]) && !e.rotated) || ((pressed & KIND[1]) && !e.moved) || e.kicked || e.quickTurned) clean = false;
            if (tooHigh(e, p)) return 0;
            for (int k = 0; k < 2; ++k) {
                if (b & KIND[k]) ++on[k], off[k] = 0;
                else ++off[k], on[k] = 0;
            }
            soft += e.dropBonus;
            if (buttons) buttons->push_back(b);
            if (e.locked) return p.x == target.x && p.dir == target.dir ? frame : 0;
        }
        return 0;
    }

    // Everything of the pair that decides what can still happen (held, repeat
    // and moveWait are zero between the taps).
    static uint64_t pack(const PairControl& p) {
        uint64_t k = uint64_t(p.x);
        k = k << 4 | uint64_t(p.y);
        k = k << 2 | p.dir;
        k = k << 1 | uint64_t(p.upper);
        k = k << 1 | uint64_t(p.grounded);
        k = k << 8 | uint64_t(uint8_t(p.gain));
        k = k << 4 | p.landings;
        k = k << 6 | p.groundFrames;
        k = k << 5 | p.quickWindow;
        k = k << 4 | p.quickLock; // (up to 15: Timing::quickTurnLock is 7 for the real game, play/realgame_read.h)
        return k;
    }

    bool seen(uint64_t key) {
        const size_t mask = seenKey.size() - 1;
        size_t i = size_t(key * 0x9E3779B97F4A7C15ull >> 40) & mask;
        while (seenStamp[i] == stamp) {
            if (seenKey[i] == key) return true;
            i = (i + 1) & mask;
        }
        seenStamp[i] = stamp;
        seenKey[i] = key;
        return false;
    }

    void search(const GameField& field, const PairControl& from, Placement target, const Timing& timing, int speedPercent, int& best,
                int& bestNode) {
        size_t slots = 1024;
        while (slots < size_t(maxStates) * 4) slots *= 2;
        if (seenKey.size() != slots) seenKey.assign(slots, 0), seenStamp.assign(slots, 0), stamp = 0;
        if (++stamp == 0) std::fill(seenStamp.begin(), seenStamp.end(), 0u), stamp = 1;
        nodes.clear();
        open.clear();
        const detail::Cells cells(field);

        // The taps start from a frame without buttons.
        Node root{from, -1, 0, WAIT};
        rootRelease = (from.held & TAP_BUTTONS) || from.moveWait;
        if (rootRelease) {
            bool high = false;
            for (int k = 0; k < tapHold && !root.pair.locked; ++k) high = tooHigh(stepPair(root.pair, field, 0, timing, speedPercent), root.pair) || high, ++root.frames;
            if (root.pair.locked || high) return;
        }
        nodes.push_back(root);
        seen(pack(root.pair));
        if (rootRelease) {
            bool clean;
            int soft;
            const int rest = finish(field, root.pair, target, timing, speedPercent, clean, soft, nullptr);
            if (rest && (!best || root.frames + rest < best)) best = root.frames + rest, bestNode = 0;
        }
        // The earliest the pair of a state can lock at the target: two frames per
        // column, and one frame per half cell down to the row where it locks
        // (the down key does nothing during a move; one half step may come by itself).
        int h[W];
        field.heights(h);
        const int lockY = target.dir == UP ? h[target.x] : target.dir == DOWN ? h[target.x] + 1 : std::max(h[target.x], h[target.childX()]);
        const auto priority = [&](const Node& n) {
            const int down = 2 * (n.pair.y - lockY) + n.pair.upper - 1;
            return uint64_t(n.frames + timing.moveFrames * std::abs(n.pair.x - target.x) + (down > 0 ? down : 0));
        };
        open.push_back(priority(root) << 32);

        while (!open.empty()) {
            std::pop_heap(open.begin(), open.end(), std::greater<>());
            const uint64_t top = open.back();
            open.pop_back();
            if (best && int(top >> 32) >= best) break; // nothing left can be earlier
            const int parent = int(uint32_t(top));
            for (uint8_t macro = 0; macro <= WAIT; ++macro) {
                // (taps held: no turn and move together - which of them the game does first in a frame is not known)
                if (tapHold > 1 && macro >= 4 && macro < WAIT) continue;
                if (macro != WAIT && (TAPS[macro] & (BTN_CW | BTN_CCW)) && locked(nodes[size_t(parent)].pair)) continue;
                Node n{nodes[size_t(parent)].pair, parent, nodes[size_t(parent)].frames, macro};
                bool high = false;
                if (macro != WAIT)
                    for (int k = 0; k < tapHold && !n.pair.locked; ++k) high = tooHigh(stepPair(n.pair, field, TAPS[macro], timing, speedPercent), n.pair) || high, ++n.frames;
                for (int k = 0; k < (macro == WAIT ? 1 : tapHold) && !n.pair.locked; ++k)
                    high = tooHigh(stepPair(n.pair, field, 0, timing, speedPercent), n.pair) || high, ++n.frames;
                if (high) continue;
                if (n.pair.locked) {
                    if (n.pair.x == target.x && n.pair.dir == target.dir && (!best || n.frames < best))
                        nodes.push_back(n), best = n.frames, bestNode = int(nodes.size()) - 1;
                    continue;
                }
                if (best && int(priority(n)) >= best) continue;
                if (seen(pack(n.pair))) continue;
                if (int(nodes.size()) >= maxStates) return;
                nodes.push_back(n);
                const int index = int(nodes.size()) - 1;
                // The straight way from here, unless a column stands in it.
                bool blocked = false;
                for (int x = n.pair.x; x != target.x && !blocked;) {
                    x += target.x > x ? 1 : -1;
                    blocked = !cells.free(x, n.pair.y);
                }
                if (!blocked) {
                    bool clean;
                    int soft;
                    const int rest = finish(field, n.pair, target, timing, speedPercent, clean, soft, nullptr);
                    if (rest) {
                        if (n.frames + rest < best || !best) best = n.frames + rest, bestNode = index;
                        continue;
                    }
                }
                open.push_back(priority(n) << 32 | uint64_t(index));
                std::push_heap(open.begin(), open.end(), std::greater<>());
            }
        }
    }
};

} // namespace juraic::ai
