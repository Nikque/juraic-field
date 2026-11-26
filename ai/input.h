#pragma once
#include "params.h"
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
#include "fastest.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <vector>

namespace juraic::ai {

struct InputPath {
    std::vector<uint8_t> buttons; // Button bits, one per frame; the pair locks in the last frame
    int softDropRows = 0;         // rows of soft drop on the way (the drop bonus)
    int states = 0;               // states the search made (0 = the straight way)
    int frames() const { return int(buttons.size()); }
};

constexpr int reversePlacement(int id) {
    return (id + PLACEMENTS / 2) % PLACEMENTS;
}

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
    // With tapHold above 1: planSure gives no way in which a frame a tap begins in, taking the buttons of the frame before
    // instead (a frame its sender is not called in), leaves the pair no way to the target (lostBySkip), when the way that
    // holds the down button only once the pair is turned and moved where it goes (downWhenPlaced) is not lost so.
    bool spareFrame = false;
    int64_t spareWays = 0; // ways planSure took so (for tools)
    // planSure gives the way whose landing animation is over soonest among the buttons of every frame (ai/fastest.h:
    // turns that push the pair aside as it falls, a pair moved on the ground, a pair turned over in a groove) when that
    // is sooner than with the way above. With taps of one frame and every cell of the field sure only. false: the way
    // above as it is.
    bool fastest = false;
    int fastestStates = 50000;                  // (FastestSearch::maxStates of a search made in planSure itself)
    int64_t fasterWays = 0, framesSaved = 0;    // ways the search made shorter, and by how many frames in all (for tools)
    // prepareFastest: the search for the way over soonest of a pair that is still to come, on a thread of its own, so
    // that planSure has it when the pair appears (a search can take longer than the frame its buttons are for).
    int fastestAhead = 0;                       // (FastestSearch::maxStates of a search made ahead; 0: none is made)
    // planSure waits for a search made ahead for its pair that is still under way (for tests and tools that play frames
    // faster than the clock: the same ways whatever the threads do). false: it searches itself within fastestStates.
    bool aheadWaits = false;
    int64_t aheadUsed = 0, aheadMissed = 0;     // ways planSure took from a search made before / searched itself (for tools)

    // The search planSure would make for these, begun now on a thread of its own (nothing with `fastest` off). Asked
    // again for the same pair it does nothing; asked for another, the search under way is given up.
    void prepareFastest(const GameField& field, const PairControl& from, Placement target, const Timing& timing, int speedPercent) {
        if (!fastest || fastestAhead <= 0 || tapHold != 1 || unsure13 || unsure14 || from.locked) return;
        collect();
        if (cached && cached->is(field, from, target, speedPercent) && (cached->ahead || !cached->gaveUp)) return;
        if (ahead.job.valid() && ahead.asked.is(field, from, target, speedPercent)) return;
        ahead.halt();
        InputPath straight;
        if (!planOver(field, from, target, timing, speedPercent, straight)) return;
        Found a;
        a.field = field, a.from = from, a.target = target, a.speed = speedPercent, a.ahead = true;
        a.now = FastestSearch::ends(field, from, straight.buttons, timing, speedPercent);
        quickest.low = low;
        if (a.now <= 1 || quickest.estimate(field, from, target, timing, speedPercent) >= a.now) {
            cached = std::move(a); // (no way can be over sooner: nothing to search)
            return;
        }
        ahead.asked = a;
        ahead.stop = std::make_shared<std::atomic<bool>>(false);
        ahead.job = std::async(std::launch::async, [a, timing, speedPercent, states = fastestAhead, lowWay = low, stop = ahead.stop]() mutable {
            FastestSearch search;
            search.maxStates = states, search.low = lowWay, search.stop = stop.get();
            a.found = search.find(a.field, a.from, a.target, timing, speedPercent, a.now - 1, a.buttons, a.soft, a.end);
            a.states = search.states, a.gaveUp = search.gaveUp;
            return a;
        });
    }
    // A search made ahead is under way: the thread of prepareFastest has not finished (PlacementAgent, fastestAgain: a way
    // not waited for is left as it is while one is; and tools and tests).
    bool preparing() const { return ahead.job.valid() && ahead.job.wait_for(std::chrono::seconds(0)) != std::future_status::ready; }

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
    //  Then, with spareFrame, the way that holds the down button later when the one found is lost by a frame left out.
    bool planSure(const GameField& field, const PairControl& from, Placement target, const Timing& timing, int speedPercent, InputPath& out) {
        if (!planOver(field, from, target, timing, speedPercent, out)) return false;
        if (fastest && tapHold == 1 && !unsure13 && !unsure14) {
            // (the end of the landing animation of the way above: only a way that is over sooner replaces it)
            const int now = FastestSearch::ends(field, from, out.buttons, timing, speedPercent);
            if (aheadWaits && ahead.job.valid() && ahead.asked.is(field, from, target, speedPercent)) ahead.job.wait();
            collect();
            if (cached && cached->is(field, from, target, speedPercent) && cached->now == now) {
                ++aheadUsed; // (worked out ahead, or by a call before this one)
            } else {
                Found a;
                a.field = field, a.from = from, a.target = target, a.speed = speedPercent, a.now = now;
                quickest.maxStates = fastestStates, quickest.low = low, quickest.stop = nullptr;
                a.found = now > 1 && quickest.find(field, from, target, timing, speedPercent, now - 1, a.buttons, a.soft, a.end);
                a.states = quickest.states, a.gaveUp = quickest.gaveUp;
                cached = std::move(a);
                ++aheadMissed;
            }
            out.states += cached->states;
            if (cached->found && cached->end < now) {
                ++fasterWays, framesSaved += now - cached->end;
                out.buttons = cached->buttons, out.softDropRows = cached->soft;
            }
        }
        if (spareFrame && tapHold > 1 && !downWhenPlaced && lostBySkip(field, from, target, timing, speedPercent, out)) {
            InputPath later;
            downWhenPlaced = true;
            const bool found = planOver(field, from, target, timing, speedPercent, later);
            downWhenPlaced = false;
            if (found && !lostBySkip(field, from, target, timing, speedPercent, later)) out = std::move(later), ++spareWays;
        }
        if (fastest && tapHold == 2 && spareFrame && !unsure13 && !unsure14)
            shortenPictureWay(field, from, target, timing, speedPercent, out);
        return true;
    }

    void shortenPictureWay(const GameField& field, const PairControl& from, Placement target, const Timing& timing,
                           int speedPercent, InputPath& out) {
        const int now = FastestSearch::ends(field, from, out.buttons, timing, speedPercent);
        if (now <= 1 || quickest.estimate(field, from, target, timing, speedPercent) >= now) return;
        // At the beginning of a new plan a released button may only have been up for one frame.
        constexpr uint8_t kind[] = {BTN_CW | BTN_CCW, BTN_LEFT | BTN_RIGHT};
        uint8_t changed = 0;
        for (int k = 0; k < 2; ++k)
            if (!(from.held & kind[k]) || heldFor[k] == 1) changed |= kind[k];
        const auto survivesSkip = [&](const std::vector<uint8_t>& buttons) {
            return reachesAfterMissedInput(field, from, target, timing, speedPercent, buttons);
        };
        InputPath other;
        int end = 0;
        quickest.maxStates = fastestStates, quickest.low = low, quickest.stop = nullptr;
        const bool found = quickest.search<true>(field, from, target, timing, speedPercent, now - 1,
            other.buttons, other.softDropRows, end, changed, downWhenPlaced, survivesSkip);
        out.states += quickest.states;
        if (found && !lostBySkip(field, from, target, timing, speedPercent, other)) {
            out.buttons = std::move(other.buttons), out.softDropRows = other.softDropRows;
            ++fasterWays, framesSaved += now - end;
        }
    }

    bool canReverse(Pair pair, bool allowHeld = false) const {
        return pair.axis >= 1 && pair.axis <= MAX_COLORS && pair.axis == pair.child &&
               (tapHold == 1 || (allowHeld && tapHold == 2 && spareFrame)) && !unsure13 && !unsure14;
    }

    // A same-colour pair can reach the same field with its pieces exchanged.
    // Keep the regular path on ties and bound the extra search within the input frame.
    bool planEquivalent(const GameField& field, const PairControl& from, int& id, unsigned legal, Pair pair,
                        const Timing& timing, const Rules& rules, InputPath& out) {
        const bool planned = (legal >> id & 1) && planSure(field, from, placement(id), timing, rules.speedPercent, out);
        const int reverse = reversePlacement(id);
        if (!canReverse(pair, true) || !(legal >> reverse & 1)) return planned;
        if (tapHold == 2)
            return planHeldEquivalent(field, from, id, pair, timing, rules, out, planned);

        const Placement target = placement(reverse);
        GameField placed = field;
        const PlaceResult drop = place(placed, target, pair);
        Wave wave;
        if (findWaveFor(placed.f, wave, rules.clearCount, false)) return planned;

        InputPath other;
        constexpr int REVERSE_STATES = 256;
        const int stateLimit = planned ? std::min(maxStates, REVERSE_STATES) : maxStates;
        if (!plan(field, from, target, timing, rules.speedPercent, other, stateLimit)) return planned;
        const PathTiming alternative = pathTiming(field, from, target, drop, timing, rules.speedPercent, other);
        if (!alternative.check) return planned;
        if (planned) {
            GameField currentField = field;
            const Placement wanted = placement(id);
            const PlaceResult currentDrop = place(currentField, wanted, pair);
            const PathTiming current = pathTiming(field, from, wanted, currentDrop, timing, rules.speedPercent, out);
            if (current.check && (other.frames() > out.frames() || alternative.check >= current.check ||
                                  (rules.softDropBonus && alternative.soft < current.soft))) return true;
        }
        other.softDropRows = alternative.soft;
        out = std::move(other);
        id = reverse;
        return true;
    }

    // Ex: both orientations go by their fastest route and must settle the same quiet field.
    bool planFastestEquivalent(const GameField& field, const PairControl& from, int& id, unsigned legal, Pair pair,
                               const Timing& timing, const Rules& rules, InputPath& out) {
        const bool planned = (legal >> id & 1) && planSure(field, from, placement(id), timing, rules.speedPercent, out);
        if (!canReverse(pair, true)) return planned;
        const int reverse = reversePlacement(id);
        if (!(legal >> reverse & 1)) return planned;

        if (tapHold == 2)
            return planHeldEquivalent(field, from, id, pair, timing, rules, out, planned, true);

        const Placement original = placement(id), alternative = placement(reverse);
        GameField settled = field, otherBoard = field;
        const PlaceResult originalDrop = place(settled, original, pair);
        const PlaceResult otherDrop = place(otherBoard, alternative, pair);
        Wave wave;
        if (settled.dead() || !(settled == otherBoard) || findWaveFor(settled.f, wave, rules.clearCount, false))
            return planned;

        InputPath other;
        if (!planSure(field, from, alternative, timing, rules.speedPercent, other)) return planned;
        const PathTiming faster = pathTiming(field, from, alternative, otherDrop, timing, rules.speedPercent, other);
        if (!faster.check) return planned;
        if (planned) {
            const PathTiming current = pathTiming(field, from, original, originalDrop, timing, rules.speedPercent, out);
            countComponent(CountedComponent::ExFlip);
            if (current.check && faster.check >= current.check) return true;
        }
        id = reverse;
        out = std::move(other);
        return true;
    }

    // Compare two picture-input paths on their actual frame and scoring rules.
    bool planHeldEquivalent(const GameField& field, const PairControl& from, int& id, Pair pair, const Timing& timing,
                            const Rules& rules, InputPath& ordinary, bool planned, bool countEx = false) {
        if (!planned) return false;
        const int reverse = reversePlacement(id);
        const Placement first = placement(id), second = placement(reverse);
        GameField current = field, alternate = field;
        const PlaceResult originalDrop = place(current, first, pair);
        const PlaceResult reversedDrop = place(alternate, second, pair);
        Wave wave;
        if (current.dead() || !(current == alternate) || findWaveFor(current.f, wave, rules.clearCount, false)) return true;
        const PathTiming original = pathTiming(field, from, first, originalDrop, timing, rules.speedPercent, ordinary);
        if (!original.check) return true;
        InputPath other;
        if (!planSure(field, from, second, timing, rules.speedPercent, other)) return true;
        const PathTiming reversed = pathTiming(field, from, second, reversedDrop, timing, rules.speedPercent, other);
        if (countEx) countComponent(CountedComponent::ExFlip);
        if (!reversed.check || reversed.check >= original.check || (rules.softDropBonus && reversed.soft < original.soft) ||
            (!countEx && other.frames() > ordinary.frames())) return true;
        if (!reachesAfterMissedInput(field, from, first, timing, rules.speedPercent, ordinary.buttons) ||
            !reachesAfterMissedInput(field, from, second, timing, rules.speedPercent, other.buttons) ||
            lostBySkip(field, from, second, timing, rules.speedPercent, other)) return true;
        id = reverse;
        other.softDropRows = reversed.soft;
        ordinary = std::move(other);
        return true;
    }

    // For a committed quiet move in the standard game. Compare the first
    // check frame, including split and landing, on the same settled board.
    bool accelerateQuiet(const GameField& field, const PairControl& from, int& id, unsigned legal, Pair pair,
                         const Timing& timing, const Rules& rules, InputPath& path, bool allowSoftLoss = false,
                         int searchStates = 0, int* stateLimitHits = nullptr, int* boardRejections = nullptr) {
        if (tapHold != 1 || downWhenPlaced || waitQuickLock || unsure13 || unsure14 ||
            spareFrame || fastest || rules.cancel || timing != DEFAULT_TIMING)
            return false;
        const auto target = placement(id);
        GameField settled = field;
        const auto drop = place(settled, target, pair);
        Wave wave;
        if (settled.dead() || findWaveFor(settled.f, wave, rules.clearCount, false)) return false;
        const auto ordinary = pathTiming(field, from, target, drop, timing, rules.speedPercent, path);
        if (!ordinary.check) return false;
        int best = ordinary.check, selected = id;
        InputPath improved;
        // Fixed foreground budget, spent only on the committed pair.
        const int stateLimit = searchStates > 0 ? searchStates : 512;
        for (int option : {id, reversePlacement(id)}) {
            if (option != id && (!canReverse(pair) || !(legal >> option & 1))) continue;
            const auto where = placement(option);
            GameField board = field;
            const auto placed = place(board, where, pair);
            if (!(board == settled)) {
                if (boardRejections) ++*boardRejections;
                continue;
            }
            const int split = timing.splitFrames(placed.split, where.x, placed.lockY);
            Timing objective = timing;
            if (split) objective.landFrames = timing.lockWait;
            const int fixedTime = split ? split + timing.landFrames - timing.lockWait : 0;
            const int limit = best - fixedTime - 1;
            quickest.low = keepLow;
            if (limit <= 0 || quickest.estimate(field, from, where, objective, rules.speedPercent) > limit) continue;
            quickest.maxStates = stateLimit;
            quickest.stop = nullptr;
            InputPath next;
            int end = 0;
            if (!quickest.find(field, from, where, objective, rules.speedPercent, limit,
                               next.buttons, next.softDropRows, end)) {
                if (stateLimitHits && quickest.gaveUp) ++*stateLimitHits;
                continue;
            }
            const auto checked = pathTiming(field, from, where, placed, timing, rules.speedPercent, next);
            if (!checked.check || checked.check >= best ||
                (!allowSoftLoss && rules.softDropBonus && checked.soft < ordinary.soft)) continue;
            best = checked.check;
            selected = option;
            next.softDropRows = checked.soft;
            improved = std::move(next);
        }
        if (best == ordinary.check) return false;
        path = std::move(improved);
        id = selected;
        return true;
    }

    // A frame of `way` from `from` in which a tap begins, taking the buttons of the frame before instead, leaves the
    // pair no way to `target` (locks it elsewhere, or no way leads there from where it is then).
    bool lostBySkip(const GameField& field, const PairControl& from, Placement target, const Timing& timing, int speedPercent, const InputPath& way) {
        PairControl p = from;
        InputPath rest;
        for (size_t i = 0; i < way.buttons.size() && !p.locked; ++i) {
            const uint8_t before = i ? way.buttons[i - 1] : from.held;
            if (way.buttons[i] & ~before & TAP_BUTTONS) {
                PairControl skipped = p;
                stepPair(skipped, field, before, timing, speedPercent);
                if (skipped.locked ? skipped.x != target.x || skipped.dir != target.dir : !plan(field, skipped, target, timing, speedPercent, rest)) return true;
            }
            stepPair(p, field, way.buttons[i], timing, speedPercent);
        }
        return false;
    }

    // planSure's way with the cells not sure of (without spareFrame).
    bool planOver(const GameField& field, const PairControl& from, Placement target, const Timing& timing, int speedPercent, InputPath& out) {
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
              InputPath& out, int stateLimit = -1) {
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
            search(field, from, target, timing, speedPercent, best, bestNode, stateLimit < 0 ? maxStates : stateLimit);
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

    // The key of a state of the pair as the search of the ways keeps them apart (held, repeat and moveWait not in it).
    static uint64_t stateKey(const PairControl& p) { return pack(p); }

private:
    static bool reachesAfterMissedInput(const GameField& field, const PairControl& from, Placement target,
                                        const Timing& timing, int speedPercent, const std::vector<uint8_t>& buttons) {
        PairControl current = from;
        for (size_t i = 0; i < buttons.size(); ++i) {
            if (buttons[i] != current.held) {
                PairControl delayed = current;
                stepPair(delayed, field, current.held, timing, speedPercent);
                for (size_t j = i + 1; j < buttons.size() && !delayed.locked; ++j)
                    stepPair(delayed, field, buttons[j], timing, speedPercent);
                if (!delayed.locked) stepPair(delayed, field, 0, timing, speedPercent);
                if (!delayed.locked || delayed.x != target.x || delayed.dir != target.dir) return false;
            }
            stepPair(current, field, buttons[i], timing, speedPercent);
        }
        return true;
    }

    struct PathTiming {
        int check = 0; // Frames through the landing animation, before checking for a chain.
        int soft = 0;
    };
    static PathTiming pathTiming(const GameField& field, PairControl control, Placement target, const PlaceResult& drop,
                                 const Timing& timing, int speedPercent, const InputPath& path) {
        int soft = 0;
        for (const uint8_t b : path.buttons) {
            if (control.locked) return {};
            soft += stepPair(control, field, b, timing, speedPercent).dropBonus;
        }
        if (!control.locked || control.x != target.x || control.dir != target.dir) return {};
        const int split = timing.splitFrames(drop.split, target.x, drop.lockY);
        const int land = split ? timing.landFrames : std::max(timing.lockWait, timing.landFrames - restedFrames(control, timing));
        // The same settled field has the same waves and next-pair delay.
        return {path.frames() + split + land, soft};
    }

    static constexpr int MAX_FRAMES = 600;
    static constexpr uint8_t TAPS[8] = {BTN_LEFT,           BTN_RIGHT,           BTN_CW,              BTN_CCW,
                                        BTN_LEFT | BTN_CW,  BTN_LEFT | BTN_CCW,  BTN_RIGHT | BTN_CW,  BTN_RIGHT | BTN_CCW};
    static constexpr uint8_t WAIT = 8;
    static constexpr uint8_t TAP_BUTTONS = BTN_LEFT | BTN_RIGHT | BTN_CW | BTN_CCW;
    static constexpr int ROW_13 = 12; // (row index)
    bool low = false; // keepLow for the way being planned
    FastestSearch quickest; // (planSure with `fastest`)
    // What a search for the way over soonest found: for which pair, and the end of the straight way it had to beat (`now`).
    struct Found {
        GameField field;
        PairControl from;
        Placement target{};
        int speed = 100, now = 0;
        bool ahead = false; // made by prepareFastest
        bool found = false, gaveUp = false;
        std::vector<uint8_t> buttons;
        int soft = 0, end = 0, states = 0;
        bool is(const GameField& f, const PairControl& p, Placement t, int s) const { return field == f && from == p && target.x == t.x && target.dir == t.dir && speed == s; }
    };
    std::optional<Found> cached; // the last search finished
    // The search made ahead. (A planner copied takes no search with it; one that goes waits for its thread.)
    struct Ahead {
        std::future<Found> job;
        std::shared_ptr<std::atomic<bool>> stop;
        Found asked;
        void halt() {
            if (stop) stop->store(true);
            if (job.valid()) job.wait();
            job = std::future<Found>();
        }
        Ahead() = default;
        Ahead(const Ahead&) {}
        Ahead& operator=(const Ahead&) { return halt(), *this; }
        ~Ahead() { halt(); }
    } ahead;
    // A search made ahead that has finished becomes the last search finished.
    void collect() {
        if (ahead.job.valid() && ahead.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) cached = ahead.job.get();
    }

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
        k = k << 6 | (p.groundFrames & 63u);
        k = k << 5 | p.quickWindow;
        k = k << 4 | p.quickLock; // (up to 15: Timing::quickTurnLock is 7 for the real game, play/realgame_read.h)
        // (both 0 with the frame counts of this engine: Timing::liftWaits, quickRise)
        k = k << 1 | uint64_t(p.lifted);
        k = k << 3 | p.rise;
        k |= uint64_t(p.liftFall) << 42;
        k |= uint64_t(p.locking) << 50;
        // (both 0 with the frame counts of this engine: Timing::turnLockFrames, quickFallFrames)
        k |= uint64_t(p.turnWait & 15) << 51;
        k |= uint64_t(p.quickFast & 15) << 55;
        // (groundFrames from 64 on: a pair slowed down - Rules::speedPercent below 100 - lies that long; the bits above the
        //  six kept in their place, so that the key of every other state stays what it was)
        k |= uint64_t(p.groundFrames >> 6) << 59;
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
                int& bestNode, int stateLimit) {
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
                if (int(nodes.size()) >= stateLimit) return;
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
