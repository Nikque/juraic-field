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

    // `from` is the pair before the first frame of the path (PairControl::held =
    // the buttons of the frame before). For a pair that is about to appear:
    // spawnPair(timing) with held set.
    bool plan(const GameField& field, const PairControl& from, Placement target, const Timing& timing, int speedPercent,
              InputPath& out) {
        out.buttons.clear();
        out.softDropRows = 0;
        out.states = 0;
        if (from.locked) return false;

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
            if (rootRelease) press(0);
            for (size_t i = macros.size(); i-- > 0;) {
                if (macros[i] != WAIT) press(TAPS[macros[i]]);
                press(0);
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
    static int finish(const GameField& field, PairControl p, Placement target, const Timing& timing, int speedPercent, bool& clean,
                      int& soft, std::vector<uint8_t>* buttons) {
        clean = true;
        soft = 0;
        for (int frame = 1; frame <= MAX_FRAMES; ++frame) {
            uint8_t b = BTN_DOWN;
            const uint8_t turn = turnButton(p.dir, target.dir);
            if (turn && !(p.held & turn)) b |= turn;
            const uint8_t side = p.x < target.x ? BTN_RIGHT : p.x > target.x ? BTN_LEFT : 0;
            if (side && !(p.held & side)) b |= side;
            const ControlEvents e = stepPair(p, field, b, timing, speedPercent);
            if (((b & (BTN_CW | BTN_CCW)) && !e.rotated) || ((b & (BTN_LEFT | BTN_RIGHT)) && !e.moved) || e.kicked || e.quickTurned)
                clean = false;
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
        k = k << 2 | p.quickLock;
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
            stepPair(root.pair, field, 0, timing, speedPercent);
            root.frames = 1;
            if (root.pair.locked) return;
        }
        nodes.push_back(root);
        seen(pack(root.pair));
        if (rootRelease) {
            bool clean;
            int soft;
            const int rest = finish(field, root.pair, target, timing, speedPercent, clean, soft, nullptr);
            if (rest && (!best || 1 + rest < best)) best = 1 + rest, bestNode = 0;
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
                Node n{nodes[size_t(parent)].pair, parent, nodes[size_t(parent)].frames, macro};
                if (macro != WAIT) {
                    stepPair(n.pair, field, TAPS[macro], timing, speedPercent);
                    ++n.frames;
                }
                if (!n.pair.locked) {
                    stepPair(n.pair, field, 0, timing, speedPercent);
                    ++n.frames;
                }
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
