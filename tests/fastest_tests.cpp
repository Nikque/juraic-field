// The ways of ai/fastest.h (InputPlanner::fastest) and the rules of the other model (Timing::landThenLock and those after
// it, OTHER_MODEL_TIMING):
//  1. the fastest buttons another AI for the real game publishes for fourteen fields (https://hahho.dev/puyo-ai-controls,
//     read 2026-11-15), played with those rules, lock each pair in the frame and at the placement that model has;
//  2. the ways of the search are never over later than the straight way, nor than the published buttons;
//  3. they are over as soon as a search of every button without an estimate finds;
//  4. a table of the ends, printed.
#include "../ai/input.h"
#include "../game/match.h"
#include "../play/memory/primary_timing14.h"
#include <chrono>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

using namespace juraic;
using namespace juraic::ai;

static void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

static const Timing& TM = DEFAULT_TIMING;
static const Timing& OM = OTHER_MODEL_TIMING;
static const char* timingName(const Timing& t) { return t.landThenLock ? "other model" : "default"; }

// A field of garbage pieces from its rows, the lowest first ('O' a piece).
static GameField fieldOf(const std::vector<std::string>& rows) {
    GameField g;
    for (size_t y = 0; y < rows.size(); ++y)
        for (int x = 0; x < W; ++x)
            if (rows[y][size_t(x)] == 'O') g.f.set(x, int(y), GARBAGE);
    return g;
}

static const std::vector<std::vector<std::string>> FIELDS = {
    {},                                                                                           // 0: empty
    {"OOOO..", "OOO...", "OO....", "OO....", "OO....", "OO....", ".O...."},                       // 1
    {".OOOOO", ".OOOO.", "..OOO.", "..OOO.", "..OOO.", "..OOO.", "..OOO.", "...O..", "...O.."},   // 2
    {"OOOOOO", "O.OO.O", "O.OO.O", "O.OO.."},                                                     // 3
    {"OOOOOO", "OOOOOO", "OOOOO.", ".OOOO.", ".OO.O.", ".O..O."},                                 // 4
    {"O.O.OO", "O.O.OO"},                                                                         // 5
    {"OOOOOO", "OOO.OO", "OO..O.", "OO..O.", "OO....", ".O....", ".O....", ".O....", ".O....", ".O....", ".O....", ".O....",
     ".O...."},                                                                                   // 6
    {"OOOOOO", "OOOOO.", "....O."},                                                               // 7
};

// The published samples: the field, the placement, the buttons as that page lists them (frame from 1, button, + pressed
// or - let go), the frame the pair locks in and the end of that model's replay, both counted from the pair's first frame.
struct Sample {
    const char* name;
    size_t field;
    Placement target;
    const char* events;
    int lock, end;
    bool split; // (a piece falls on after the lock: the time of that fall is in their end only)
};
static const Sample SAMPLES[] = {
    {"1", 1, {5, DOWN}, "1D+ 15CW+ 16CW- 17CW+ 18CW- 20R+ 21R- 24R+ 25R- 26R+ 27R-", 29, 32, false},
    {"2", 1, {5, LEFT}, "1D+ 19CCW+ 20CCW- 20CW+ 21CCW+ 21CW- 22CCW- 22CW+ 23CCW+ 23CW- 24CCW-", 30, 38, false},
    {"3", 2, {0, UP}, "1D+ 9CW+ 10CCW+ 10CW- 11CCW- 18CW+ 19CW- 20CW+ 21CW- 23D- 24CW+ 25CCW+ 25CW- 26CCW- 31D+", 32, 36, false},
    {"4", 2, {5, UP}, "1R+ 1D+ 2R- 3R+ 4R- 12CCW+ 13CCW- 14CCW+ 15CCW- 18CW+ 19CCW+ 19CW- 20CCW-", 26, 37, false},
    {"5", 2, {5, DOWN}, "1R+ 2R- 3R+ 4R- 5D+ 12CCW+ 13CCW- 14CCW+ 15CCW- 18CW+ 19CCW+ 19CW- 20CCW- 26D- 28CW+ 29CCW+ 29CW- 30CCW- 35D+", 36, 37, false},
    {"6", 3, {1, UP}, "1L+ 2L- 3D+ 16CW+ 17CW- 18CW+ 19CW- 20CW+ 21CCW+ 21CW- 22CCW-", 28, 36, false},
    {"7", 4, {0, UP}, "1L+ 2L- 3L+ 4L- 5D+ 14CW+ 15CW- 16CW+ 17CW- 18CW+ 19CCW+ 19CW- 20CCW-", 26, 34, false},
    {"8", 5, {1, UP}, "1L+ 2L- 3D+ 20CW+ 21CW- 22CW+ 23CW- 25D- 26CW+ 27CCW+ 27CW- 28CCW- 33D+", 34, 38, false},
    {"9", 5, {3, UP}, "1R+ 2R- 3D+ 20CW+ 21CW- 22CW+ 23CW- 25D- 26CW+ 27CCW+ 27CW- 28CCW- 33D+", 34, 38, false},
    {"10", 1, {5, UP}, "1D+ 18CW+ 19CCW+ 19CW- 20CCW- 22R+ 23R- 26R+ 27R- 28R+ 29R-", 31, 34, false},
    {"11", 6, {4, RIGHT}, "1R+ 1CCW+ 2R- 2CCW- 3D+ 9CW+ 10CW- 11CW+ 12CW-", 18, 56, true},
    {"12", 0, {5, UP}, "1D+ 24R+ 25R- 26R+ 27R- 28R+ 29R-", 30, 38, false},
    {"13", 0, {0, UP}, "1D+ 24L+ 25L- 26L+ 27L-", 28, 38, false},
    {"14", 7, {2, RIGHT}, "1D+ 19CW+ 20CW-", 26, 34, false},
};

// The buttons of every frame up to `frames` from the events of a sample.
static std::vector<uint8_t> buttonsOf(const char* events, int frames) {
    std::vector<uint8_t> out(size_t(frames), 0);
    std::istringstream in(events);
    std::string token;
    std::vector<std::pair<int, std::pair<uint8_t, bool>>> list;
    while (in >> token) {
        size_t digits = 0;
        const int frame = std::stoi(token, &digits);
        const std::string button = token.substr(digits, token.size() - digits - 1);
        const uint8_t bit = button == "D" ? BTN_DOWN : button == "L" ? BTN_LEFT : button == "R" ? BTN_RIGHT : button == "CW" ? BTN_CW : BTN_CCW;
        check(button == "D" || button == "L" || button == "R" || button == "CW" || button == "CCW", "a button of a sample: " + token);
        list.push_back({frame, {bit, token.back() == '+'}});
    }
    uint8_t held = 0;
    size_t next = 0;
    for (int f = 1; f <= frames; ++f) {
        for (; next < list.size() && list[next].first == f; ++next) held = list[next].second.second ? uint8_t(held | list[next].second.first) : uint8_t(held & ~list[next].second.first);
        out[size_t(f) - 1] = held;
    }
    return out;
}

static std::string name(Placement t) {
    static const char* const DIRS[4] = {"up", "right", "down", "left"};
    return "column " + std::to_string(t.x + 1) + " " + DIRS[t.dir];
}

// The pair as it appears, or `rows` rows above the highest column after falling there with the down button.
static PairControl startOf(const GameField& g, int rows, const Timing& t) {
    PairControl p = spawnPair(t);
    if (rows < 0) return p;
    int h[W], top = 0;
    g.heights(h);
    for (int x = 0; x < W; ++x) top = std::max(top, h[x]);
    while (p.y > top + rows) stepPair(p, g, BTN_DOWN, t);
    return p;
}

// ---- 1. the published buttons with the rules of the other model ----
static void testPublished() {
    for (const Sample& s : SAMPLES) {
        const GameField g = fieldOf(FIELDS[s.field]);
        const std::vector<uint8_t> buttons = buttonsOf(s.events, s.lock);
        PairControl p = spawnPair(OM);
        int locked = 0;
        for (size_t i = 0; i < buttons.size() && !p.locked; ++i) {
            stepPair(p, g, buttons[i], OM);
            if (p.locked) locked = int(i) + 1;
        }
        const std::string where = std::string("sample ") + s.name;
        check(locked == s.lock, where + ": locked in frame " + std::to_string(locked) + ", published " + std::to_string(s.lock));
        check(p.x == s.target.x && p.dir == s.target.dir, where + ": locked at " + name(p.placement()) + ", published " + name(s.target));
        // (the end: theirs counts one frame less of the animation after a lock that comes late in it)
        const int end = FastestSearch::ends(g, spawnPair(OM), buttons, OM, 100);
        if (!s.split) check(end == s.end || end == s.end + 1, where + ": over in frame " + std::to_string(end) + ", published " + std::to_string(s.end));
        // (with the rules of this engine alone the same buttons lock the pair earlier, somewhere on the way)
        PairControl q = spawnPair(TM);
        int early = 0;
        for (size_t i = 0; i < buttons.size() && !q.locked; ++i) {
            stepPair(q, g, buttons[i], TM);
            if (q.locked) early = int(i) + 1;
        }
        check(early > 0 && early < s.lock, where + ": the rules of this engine do not lock the pair earlier");
    }
    std::cout << "published buttons: " << std::size(SAMPLES) << " samples lock where and when that model has them\n";
}

// ---- 2. never later than the straight way, nor than the published buttons ----
static void testNeverLater(const Timing& t) {
    int ways = 0, sooner = 0, saved = 0, states = 0, gaveUp = 0;
    for (size_t f = 0; f < FIELDS.size(); ++f) {
        const GameField g = fieldOf(FIELDS[f]);
        const unsigned legal = legalPlacements(g);
        for (int id = 0; id < PLACEMENTS; ++id) {
            if (!(legal >> id & 1)) continue;
            const Placement target = placement(id);
            const PairControl from = spawnPair(t);
            InputPlanner plain, fast;
            fast.fastest = true;
            InputPath a, b;
            if (!plain.planSure(g, from, target, t, 100, a)) continue;
            const std::string where = std::string(timingName(t)) + ", field " + std::to_string(f) + ", " + name(target);
            check(fast.planSure(g, from, target, t, 100, b), where + ": no way with the search on");
            PairControl p = from;
            for (size_t i = 0; i < b.buttons.size(); ++i) {
                check(!p.locked, where + ": the pair locks before the last frame");
                stepPair(p, g, b.buttons[i], t);
            }
            check(p.locked && p.x == target.x && p.dir == target.dir, where + ": the buttons do not lock the pair there");
            const int endA = FastestSearch::ends(g, from, a.buttons, t, 100), endB = FastestSearch::ends(g, from, b.buttons, t, 100);
            check(endA > 0 && endB > 0 && endB <= endA, where + ": over later than the straight way (" + std::to_string(endB) + " for " + std::to_string(endA) + ")");
            check((endB < endA) == (fast.fasterWays == 1), where + ": the count of the ways made shorter");
            ++ways, sooner += endB < endA, saved += endA - endB, states += b.states, gaveUp += b.states > fast.fastestStates;
        }
    }
    check(ways > 100, "too few placements tried: " + std::to_string(ways));
    std::cout << "never later (" << timingName(t) << "): " << ways << " ways, " << sooner << " over sooner (" << saved << " frames in all), " << states
              << " states, " << gaveUp << " searches given up\n";
}
static void testNotLaterThanPublished() {
    int same = 0, sooner = 0;
    std::string bad;
    for (const Sample& s : SAMPLES) {
        const GameField g = fieldOf(FIELDS[s.field]);
        const PairControl from = spawnPair(OM);
        const int theirs = FastestSearch::ends(g, from, buttonsOf(s.events, s.lock), OM, 100);
        FastestSearch search;
        search.maxStates = 1500000;
        std::vector<uint8_t> buttons;
        int soft = 0, end = 0;
        const int counted = search.estimate(g, from, s.target, OM, 100);
        const auto began = std::chrono::steady_clock::now();
        const bool found = search.find(g, from, s.target, OM, 100, theirs, buttons, soft, end);
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count();
        std::cout << "  sample " << s.name << ": theirs " << theirs << ", ours " << (found ? std::to_string(end) : search.gaveUp ? "given up" : "none")
                  << " (counted on " << counted << " at the start), " << search.states << " states, " << ms << " ms\n";
        if (found && end < theirs) {
            // (the way, a frame each: l r d, a = turn left, c = turn right, - = nothing)
            std::string way;
            for (const uint8_t b : buttons) {
                std::string f;
                if (b & BTN_LEFT) f += 'l';
                if (b & BTN_RIGHT) f += 'r';
                if (b & BTN_DOWN) f += 'd';
                if (b & BTN_CCW) f += 'a';
                if (b & BTN_CW) f += 'c';
                way += (f.empty() ? "-" : f) + " ";
            }
            std::cout << "    " << way << "\n";
        }
        if (!found) bad += std::string(" ") + s.name + (search.gaveUp ? "(given up)" : "(none)");
        else if (end > theirs) bad += std::string(" ") + s.name + "(later)";
        else ++(end < theirs ? sooner : same);
    }
    std::cout << "beside the published buttons, their rules: " << same << " as soon, " << sooner << " sooner\n";
    check(bad.empty(), "no way as soon as the published one for the samples" + bad);
}

// ---- 3. as soon as every button of every frame, tried without an estimate ----
static uint64_t stateOf(const PairControl& p) {
    // (63 bits: every field of a pair on the way but `locked`, `queued` and `repeat` - no direction is held here)
    uint64_t k = uint64_t(p.x) & 7;
    const auto add = [&](uint64_t value, int bits) { k = k << bits | (value & ((uint64_t(1) << bits) - 1)); };
    add(uint64_t(p.y), 4), add(p.dir, 2), add(p.upper, 1), add(p.grounded, 1), add(p.moveWait, 2), add(p.landings, 4), add(p.quickLock, 2);
    add(uint64_t(p.gain), 8), add(p.groundFrames, 8), add(p.quickWindow, 6), add(p.held, 6);
    add(p.turnWait, 3), add(p.quickFast, 4), add(p.owned, 1), add(p.ownFrames, 8);
    return k;
}
// The soonest end of the landing animation at `target` with any buttons (a direction or a turn held is let go first, as
// in the search), `none` when no way is over before that; -1 when more than `most` states came up (not known).
static int bruteEnd(const GameField& g, const PairControl& from, Placement target, const Timing& t, int none, size_t most, size_t& tried) {
    std::unordered_set<uint64_t> seen;
    std::vector<PairControl> layer{from}, next;
    int best = none;
    for (int frames = 1; frames + t.lockWait < best && !layer.empty(); ++frames) {
        if (seen.size() > most) return -1;
        next.clear();
        for (const PairControl& p : layer)
            for (const uint8_t side : {uint8_t(0), uint8_t(BTN_LEFT), uint8_t(BTN_RIGHT)})
                for (const uint8_t turn : {uint8_t(0), uint8_t(BTN_CW), uint8_t(BTN_CCW)})
                    for (const uint8_t down : {uint8_t(0), uint8_t(BTN_DOWN)}) {
                        if ((p.held & side) || (p.held & turn)) continue;
                        PairControl q = p;
                        stepPair(q, g, uint8_t(side | turn | down), t);
                        if (q.locked) {
                            if (q.x == target.x && q.dir == target.dir) best = std::min(best, frames + std::max(t.landFrames - restedFrames(q, t), t.lockWait));
                        } else if (seen.insert(stateOf(q)).second) {
                            next.push_back(q);
                        }
                    }
        tried += next.size();
        layer.swap(next);
    }
    return best;
}
static void testAsSoonAsAll(const Timing& t, int rows) {
    struct Case {
        size_t field;
        Placement target;
    };
    const Case cases[] = {{1, {5, DOWN}}, {1, {5, UP}}, {1, {5, LEFT}}, {1, {2, UP}}, {2, {0, UP}}, {2, {5, DOWN}}, {3, {1, UP}},
                          {4, {0, UP}},   {5, {1, UP}}, {5, {3, DOWN}}, {7, {2, RIGHT}}, {0, {5, UP}}, {0, {0, DOWN}}};
    size_t tried = 0;
    int states = 0, unknown = 0;
    for (const Case& c : cases) {
        const GameField g = fieldOf(FIELDS[c.field]);
        const PairControl from = startOf(g, rows, t);
        const std::string where = std::string(timingName(t)) + ", field " + std::to_string(c.field) + ", " + name(c.target);
        FastestSearch search;
        search.maxStates = 1000000;
        std::vector<uint8_t> buttons;
        int soft = 0, end = 0;
        const int none = 200;
        const bool found = search.find(g, from, c.target, t, 100, none - 1, buttons, soft, end);
        const int all = bruteEnd(g, from, c.target, t, none, 1500000, tried);
        if (all < 0 || search.gaveUp) {
            ++unknown; // (too many states for one of the two: nothing to compare)
            continue;
        }
        check(found == (all < none), where + ": a way is found by one search only");
        if (found) check(end == all, where + ": over in " + std::to_string(end) + " frames, every button gives " + std::to_string(all));
        if (found) check(FastestSearch::ends(g, from, buttons, t, 100) == end, where + ": the end told is not the end played");
        states += search.states;
    }
    std::cout << "as soon as all (" << timingName(t) << "): " << std::size(cases) - size_t(unknown) << " cases compared, " << unknown << " not (too many states), "
              << states << " states with the estimate, " << tried << " without\n";
}

// ---- the rule of the settings (Rules::cancel) ----
static void testRule() {
    Rules rules;
    Round without, with;
    without.start(rules, 1);
    rules.cancel = true;
    with.start(rules, 1);
    check(!without.timing.landThenLock && !without.timing.firstOwnLanding && without.timing.turnLockFrames == 0, "a round without the rule has rules of the other model");
    check(with.timing.landThenLock && with.timing.firstOwnLanding && with.timing.restBoth && with.timing.turnDropsRest && with.timing.turnLockFrames == 7 &&
              with.timing.lockWait == 6 && !with.timing.readyOnGround && with.timing.quickFallFrames == 8 && with.timing.quickFallUpOnly,
          "a round with the rule has not the rules read off the records of the real game");
    check(with.timing.landFrames == without.timing.landFrames && with.timing.lockGain == without.timing.lockGain, "the rule changes the frame counts of the round");
    // (a pair dropped with the down button: locked in the frame it lands without the rule, a frame later with it)
    const auto dropped = [](const Timing& t) {
        const GameField g;
        PairControl p = spawnPair(t);
        int frames = 0;
        while (!p.locked && frames < 100) stepPair(p, g, BTN_DOWN, t), ++frames;
        return frames;
    };
    check(dropped(without.timing) == 23 && dropped(with.timing) == 24, "a pair dropped: " + std::to_string(dropped(without.timing)) + " and " + std::to_string(dropped(with.timing)) + " frames");
    std::cout << "the rule: a round with Rules::cancel plays with the rules read off the records of the real game\n";
}

// ---- 4. the ends, printed ----
static void printBeside() {
    std::cout << "frames from the pair's first frame to the end of the landing animation:\n";
    std::cout << "  sample  placement         default: straight fastest   other model: straight fastest published(theirs)  states\n";
    for (const Sample& s : SAMPLES) {
        const GameField g = fieldOf(FIELDS[s.field]);
        int ends[4] = {0, 0, 0, 0}, states = 0;
        int n = 0;
        for (const Timing* t : {&TM, &OM}) {
            const PairControl from = spawnPair(*t);
            InputPlanner plain, fast;
            fast.fastest = true;
            InputPath a, b;
            if (plain.planSure(g, from, s.target, *t, 100, a)) ends[n] = FastestSearch::ends(g, from, a.buttons, *t, 100);
            if (fast.planSure(g, from, s.target, *t, 100, b)) ends[n + 1] = FastestSearch::ends(g, from, b.buttons, *t, 100), states = b.states;
            n += 2;
        }
        const int theirs = FastestSearch::ends(g, spawnPair(OM), buttonsOf(s.events, s.lock), OM, 100);
        char line[200];
        std::snprintf(line, sizeof line, "  %-6s  %-16s  %17d %7d  %22d %7d %9d(%d)  %6d%s\n", s.name, name(s.target).c_str(), ends[0], ends[1], ends[2], ends[3],
                      theirs, s.end, states, s.split ? "  (theirs with the fall of a piece)" : "");
        std::cout << line;
    }
}

// ---- 5. the published buttons and the ways of the search with the frame counts read off the records of the real game ----
// (the counts the AI reckons with when it plays the real game by its memory: primaryTiming14; the search as wide as the
//  one made ahead there). Printed, not checked: the published buttons are those of another model.
static void printReal() {
    const Timing real = play::realgame::primaryTiming14(true);
    std::cout << "with the frame counts of the real game (records): frames from the pair's first frame\n";
    std::cout << "  sample  placement         published: lock end (their model)   their buttons here: lock end  where            here: straight fastest  states  fastest - theirs\n";
    int sooner = 0, same = 0, later = 0, elsewhere = 0;
    for (const Sample& s : SAMPLES) {
        const GameField g = fieldOf(FIELDS[s.field]);
        const std::vector<uint8_t> all = buttonsOf(s.events, 90);
        PairControl p = spawnPair(real);
        int locked = 0;
        for (size_t i = 0; i < all.size() && !p.locked; ++i) {
            stepPair(p, g, all[i], real);
            if (p.locked) locked = int(i) + 1;
        }
        const bool there = locked && p.x == s.target.x && p.dir == s.target.dir;
        const int theirs = locked ? FastestSearch::ends(g, spawnPair(real), std::vector<uint8_t>(all.begin(), all.begin() + locked), real, 100) : 0;
        const PairControl from = spawnPair(real);
        InputPlanner plain;
        InputPath a;
        const int straight = plain.planSure(g, from, s.target, real, 100, a) ? FastestSearch::ends(g, from, a.buttons, real, 100) : 0;
        FastestSearch search;
        search.maxStates = play::realgame::PRIMARY_FASTEST_AHEAD14;
        std::vector<uint8_t> buttons;
        int soft = 0, end = 0;
        const bool found = straight > 1 && search.find(g, from, s.target, real, 100, straight - 1, buttons, soft, end);
        const int fastest = found ? end : straight;
        if (!there) ++elsewhere;
        else if (fastest < theirs) ++sooner;
        else if (fastest == theirs) ++same;
        else ++later;
        char line[260];
        std::snprintf(line, sizeof line, "  %-6s  %-16s  %15d %3d  %31d %3d  %-15s  %14d %7d  %6d%s  %s\n", s.name, name(s.target).c_str(), s.lock, s.end, locked, theirs,
                      there ? "as published" : locked ? name(p.placement()).c_str() : "not locked", straight, fastest, search.states, search.gaveUp ? "*" : " ",
                      there ? std::to_string(fastest - theirs).c_str() : "-");
        std::cout << line;
        if (found) {
            std::string way;
            for (size_t i = 0; i < buttons.size(); ++i) {
                const uint8_t b = buttons[i], before = i ? buttons[i - 1] : 0;
                for (const auto& [bit, text] : {std::pair<uint8_t, const char*>{BTN_DOWN, "D"}, {BTN_LEFT, "L"}, {BTN_RIGHT, "R"}, {BTN_CW, "CW"}, {BTN_CCW, "CCW"}}) {
                    if ((b & bit) && !(before & bit)) way += std::to_string(i + 1) + text + "+ ";
                    if (!(b & bit) && (before & bit)) way += std::to_string(i + 1) + text + "- ";
                }
            }
            std::cout << "          here: " << way << "\n";
        }
    }
    std::cout << "  (* the search gave up at its states) the fastest way here against their buttons played here: sooner " << sooner << ", the same " << same << ", later " << later
              << ", their buttons lock elsewhere or not at all " << elsewhere << "\n";
}

static void testAirborneDownwardQuickTurn() {
    const Timing t = play::realgame::primaryTiming14(true);
    const Timing original = recordedRules(Timing{});
    // 20261119-172052/primary-5、回戦2・組36。時刻1800から1809の盤面と操作。
    const int heights[W] = {14, 13, 7, 12, 13, 7};
    GameField field;
    for (int x = 0; x < W; ++x) {
        for (int y = 0; y < std::min(13, heights[x]); ++y) field.f.set(x, y, GARBAGE);
        if (heights[x] == 14) field.row14[size_t(x)] = GARBAGE;
    }
    const int level[] = {23, 22, 21, 21, 20, 19, 18, 18, 17, 16};
    PairControl pair = spawnPair(t);
    for (int i = 0; i < 10; ++i) {
        check(pair.x == 2 && pair.y * 2 + int(pair.upper) == level[i] &&
              pair.dir == (i < 3 ? UP : DOWN) && !pair.locked,
              "子が下になる反転中の落下、時刻" + std::to_string(1800 + i));
        if (i >= 3) check(pair.quickLock == 10 - i, "反転後の回転待ち");
        stepPair(pair, field, i == 0 || i == 2 ? BTN_DOWN | BTN_CW : BTN_DOWN, t);
    }
    check(pair.locked, "時刻1809の下入力で固定");

    // 同じ両壁で、子が上になる反転と、子の直下が埋まった反転は従来の推移を保つ。
    for (Direction before : {DOWN, UP}) {
        GameField obstacle = field;
        PairControl a = spawnPair(t);
        a.y = 10;
        a.dir = before;
        a.upper = false;
        a.quickWindow = 2;
        if (before == UP) obstacle.f.set(2, 9, GARBAGE);
        PairControl b = a;
        for (int i = 0; i < 8; ++i) {
            const uint8_t buttons = i == 0 ? BTN_CW | BTN_DOWN : BTN_DOWN;
            const ControlEvents e = stepPair(a, obstacle, buttons, t);
            stepPair(b, obstacle, buttons, original);
            check(i != 0 || e.quickTurned, "両壁の間で反転");
            check(a == b, "上向きの加速と障害物上の反転待ち");
        }
    }
}

static void testRecordedTurns() {
    const Timing t = play::realgame::primaryTiming14(true);
    struct Frame { int x, y, upper, dir, buttons; };
    {
        // 20261117-162513/primary-2 r0 p9: 実機で記録した盤面・入力・位置・固定。
        const std::string cells = "000000000000000000000000000000000000000000000000300000500000220000530030554330333445";
        GameField g;
        for (int y = 0; y < 13; ++y) for (int x = 0; x < W; ++x) g.f.set(x, y, Cell(cells[size_t((13-y)*6+x)] - '0'));
        for (int x = 0; x < W; ++x) g.row14[size_t(x)] = Cell(cells[size_t(x)] - '0');
        const Frame frames[] = {
            {2, 11, 1, 0, 4},
            {2, 11, 0, 0, 4},
            {2, 10, 1, 0, 4},
            {2, 10, 0, 0, 4},
            {2, 9, 1, 0, 4},
            {2, 9, 0, 0, 4},
            {2, 8, 1, 0, 4},
            {2, 8, 0, 0, 4},
            {2, 7, 1, 0, 4},
            {2, 7, 0, 0, 4},
            {2, 6, 1, 0, 4},
            {2, 6, 0, 0, 4},
            {2, 5, 1, 0, 4},
            {2, 5, 0, 0, 4},
            {2, 4, 1, 0, 4},
            {2, 4, 0, 0, 4},
            {2, 3, 1, 0, 4},
            {2, 3, 0, 0, 14},
            {4, 3, 0, 3, 4},
            {4, 3, 0, 3, 6},
            {5, 3, 0, 3, 20},
            {5, 3, 0, 0, 4},
            {5, 2, 1, 0, 4},
            {5, 2, 0, 0, 4},
            {5, 1, 1, 0, 4},
            {5, 1, 0, 0, 4},
            {5, 1, 0, 0, 4},
            {5, 1, 0, 0, 4},
        };
        PairControl p = spawnPair(t); p.gain = 0;
        for (size_t i = 0; i < std::size(frames); ++i) {
            const Frame& v = frames[i];
            check(!p.locked && p.x == v.x && p.y == v.y && int(p.upper) == v.upper && p.dir == v.dir,
                  "実機記録 20261117-162513/primary-2 r0 p9 フレーム " + std::to_string(i));
            stepPair(p, g, uint8_t(v.buttons), t);
        }
        check(p.locked, "実機記録と固定のフレームが一致");
        check(int(std::size(frames)) + std::max(t.landFrames - restedFrames(p,t), t.lockWait) + t.nextFrames == 52, "実機記録と次の組までの時間が一致");
    }
    {
        // 20261117-162513/primary-2 r0 p29: 実機で記録した盤面・入力・位置・固定。
        const std::string cells = "000000000000000000000022000042234234224555533322324523544255222445534534554334333445";
        GameField g;
        for (int y = 0; y < 13; ++y) for (int x = 0; x < W; ++x) g.f.set(x, y, Cell(cells[size_t((13-y)*6+x)] - '0'));
        for (int x = 0; x < W; ++x) g.row14[size_t(x)] = Cell(cells[size_t(x)] - '0');
        const Frame frames[] = {
            {2, 11, 1, 0, 4},
            {2, 11, 0, 0, 6},
            {3, 11, 0, 0, 4},
            {3, 11, 0, 0, 14},
            {4, 11, 0, 3, 4},
            {4, 11, 0, 3, 4},
            {4, 11, 0, 3, 4},
            {4, 11, 0, 3, 4},
            {4, 11, 0, 3, 12},
            {4, 12, 0, 2, 22},
            {5, 11, 1, 3, 12},
            {5, 12, 1, 2, 20},
            {5, 12, 0, 3, 12},
            {5, 12, 0, 2, 4},
            {5, 12, 0, 2, 4},
            {5, 12, 0, 2, 4},
            {5, 12, 0, 2, 4},
            {5, 12, 0, 2, 4},
            {5, 12, 0, 2, 4},
            {5, 12, 0, 2, 4},
        };
        PairControl p = spawnPair(t); p.gain = 0;
        for (size_t i = 0; i < std::size(frames); ++i) {
            const Frame& v = frames[i];
            check(!p.locked && p.x == v.x && p.y == v.y && int(p.upper) == v.upper && p.dir == v.dir,
                  "実機記録 20261117-162513/primary-2 r0 p29 フレーム " + std::to_string(i));
            stepPair(p, g, uint8_t(v.buttons), t);
        }
        check(p.locked, "実機記録と固定のフレームが一致");
        check(int(std::size(frames)) + std::max(t.landFrames - restedFrames(p,t), t.lockWait) + t.nextFrames == 40, "実機記録と次の組までの時間が一致");
    }
    {
        // 20261117-162513/primary-2 r6 p27: 実機で記録した盤面・入力・位置・固定。
        const std::string cells = "000000000000000000000001000525000223000511000552000311000221025233232553232311255331";
        GameField g;
        for (int y = 0; y < 13; ++y) for (int x = 0; x < W; ++x) g.f.set(x, y, Cell(cells[size_t((13-y)*6+x)] - '0'));
        for (int x = 0; x < W; ++x) g.row14[size_t(x)] = Cell(cells[size_t(x)] - '0');
        const Frame frames[] = {
            {2, 11, 1, 0, 22},
            {3, 11, 1, 1, 4},
            {3, 11, 1, 1, 20},
            {3, 11, 0, 2, 4},
            {3, 11, 0, 2, 12},
            {3, 10, 1, 1, 4},
            {3, 10, 0, 1, 5},
            {2, 10, 0, 1, 12},
            {2, 9, 1, 0, 4},
            {2, 9, 0, 0, 4},
            {2, 8, 1, 0, 4},
            {2, 8, 0, 0, 4},
            {2, 7, 1, 0, 20},
            {1, 7, 0, 1, 4},
            {1, 6, 1, 1, 4},
            {1, 6, 0, 1, 4},
            {1, 5, 1, 1, 4},
            {1, 5, 0, 1, 4},
            {1, 4, 1, 1, 4},
            {1, 4, 0, 1, 4},
        };
        PairControl p = spawnPair(t); p.gain = 0;
        for (size_t i = 0; i < std::size(frames); ++i) {
            const Frame& v = frames[i];
            check(!p.locked && p.x == v.x && p.y == v.y && int(p.upper) == v.upper && p.dir == v.dir,
                  "実機記録 20261117-162513/primary-2 r6 p27 フレーム " + std::to_string(i));
            stepPair(p, g, uint8_t(v.buttons), t);
        }
        check(p.locked, "実機記録と固定のフレームが一致");
        check(int(std::size(frames)) + std::max(t.landFrames - restedFrames(p,t), t.lockWait) + t.nextFrames == 46, "実機記録と次の組までの時間が一致");
    }
    {
        // 20261117-162513/primary-2 r6 p41: 実機で記録した盤面・入力・位置・固定。
        const std::string cells = "500000260000530600130360520525515223223511512552252311215221325233232553232311255331";
        GameField g;
        for (int y = 0; y < 13; ++y) for (int x = 0; x < W; ++x) g.f.set(x, y, Cell(cells[size_t((13-y)*6+x)] - '0'));
        for (int x = 0; x < W; ++x) g.row14[size_t(x)] = Cell(cells[size_t(x)] - '0');
        const Frame frames[] = {
            {2, 11, 1, 0, 16},
            {2, 11, 1, 0, 4},
            {2, 11, 0, 0, 16},
            {2, 12, 0, 2, 0},
            {2, 12, 0, 2, 0},
            {2, 12, 0, 2, 1},
            {2, 12, 0, 2, 0},
            {2, 12, 0, 2, 1},
            {2, 12, 1, 2, 0},
            {2, 12, 1, 2, 6},
            {2, 12, 0, 2, 20},
            {3, 12, 0, 3, 6},
            {4, 12, 0, 3, 20},
            {4, 11, 1, 0, 4},
            {4, 11, 0, 0, 4},
            {4, 11, 0, 0, 4},
            {4, 11, 0, 0, 4},
            {4, 11, 0, 0, 4},
            {4, 11, 0, 0, 4},
            {4, 11, 0, 0, 4},
        };
        PairControl p = spawnPair(t); p.gain = 0;
        for (size_t i = 0; i < std::size(frames); ++i) {
            const Frame& v = frames[i];
            check(!p.locked && p.x == v.x && p.y == v.y && int(p.upper) == v.upper && p.dir == v.dir,
                  "実機記録 20261117-162513/primary-2 r6 p41 フレーム " + std::to_string(i));
            stepPair(p, g, uint8_t(v.buttons), t);
        }
        check(p.locked, "実機記録と固定のフレームが一致");
        check(int(std::size(frames)) + std::max(t.landFrames - restedFrames(p,t), t.lockWait) + t.nextFrames == 41, "実機記録と次の組までの時間が一致");
    }
    {
        // 20261117-162513/primary-2 r8 p42: 実機で記録した盤面・入力・位置・固定。
        const std::string cells = "500000300000230000450045320225253525322542324432553554523433224555534223466233442444";
        GameField g;
        for (int y = 0; y < 13; ++y) for (int x = 0; x < W; ++x) g.f.set(x, y, Cell(cells[size_t((13-y)*6+x)] - '0'));
        for (int x = 0; x < W; ++x) g.row14[size_t(x)] = Cell(cells[size_t(x)] - '0');
        const Frame frames[] = {
            {2, 11, 1, 0, 12},
            {3, 11, 0, 3, 6},
            {4, 11, 0, 3, 12},
            {4, 12, 0, 2, 21},
            {3, 12, 0, 3, 4},
            {3, 12, 0, 3, 21},
            {2, 12, 0, 0, 4},
            {2, 12, 0, 0, 5},
            {1, 12, 0, 0, 4},
            {1, 12, 0, 0, 4},
            {1, 12, 0, 0, 4},
            {1, 12, 0, 0, 4},
            {1, 12, 0, 0, 4},
        };
        PairControl p = spawnPair(t); p.gain = 0;
        for (size_t i = 0; i < std::size(frames); ++i) {
            const Frame& v = frames[i];
            check(!p.locked && p.x == v.x && p.y == v.y && int(p.upper) == v.upper && p.dir == v.dir,
                  "実機記録 20261117-162513/primary-2 r8 p42 フレーム " + std::to_string(i));
            stepPair(p, g, uint8_t(v.buttons), t);
        }
        check(p.locked, "実機記録と固定のフレームが一致");
        check(int(std::size(frames)) + std::max(t.landFrames - restedFrames(p,t), t.lockWait) + t.nextFrames == 35, "実機記録と次の組までの時間が一致");
    }
    {
        // 20261117-162513/primary-2 r9 p7: 実機で記録した盤面・入力・位置・固定。
        const std::string cells = "000000000000000000000000500000500000500000100000100000400000410000510000550000110000";
        GameField g;
        for (int y = 0; y < 13; ++y) for (int x = 0; x < W; ++x) g.f.set(x, y, Cell(cells[size_t((13-y)*6+x)] - '0'));
        for (int x = 0; x < W; ++x) g.row14[size_t(x)] = Cell(cells[size_t(x)] - '0');
        const Frame frames[] = {
            {2, 11, 1, 0, 4},
            {2, 11, 0, 0, 4},
            {2, 10, 1, 0, 4},
            {2, 10, 0, 0, 5},
            {1, 10, 0, 0, 20},
            {1, 10, 0, 1, 5},
            {0, 10, 0, 1, 20},
            {0, 11, 0, 2, 14},
            {1, 11, 0, 1, 20},
            {1, 11, 0, 2, 5},
            {0, 11, 0, 2, 4},
            {0, 11, 0, 2, 4},
            {0, 11, 0, 2, 4},
            {0, 11, 0, 2, 4},
            {0, 11, 0, 2, 4},
            {0, 11, 0, 2, 4},
        };
        PairControl p = spawnPair(t); p.gain = 0;
        for (size_t i = 0; i < std::size(frames); ++i) {
            const Frame& v = frames[i];
            check(!p.locked && p.x == v.x && p.y == v.y && int(p.upper) == v.upper && p.dir == v.dir,
                  "実機記録 20261117-162513/primary-2 r9 p7 フレーム " + std::to_string(i));
            stepPair(p, g, uint8_t(v.buttons), t);
        }
        check(p.locked, "実機記録と固定のフレームが一致");
        check(int(std::size(frames)) + std::max(t.landFrames - restedFrames(p,t), t.lockWait) + t.nextFrames == 37, "実機記録と次の組までの時間が一致");
    }
    {
        // 20261117-162513/primary-2 r11 p34: 実機で記録した盤面・入力・位置・固定。
        const std::string cells = "000000500000500000360030440540434360325425335452423352224253454533445325334225344552";
        GameField g;
        for (int y = 0; y < 13; ++y) for (int x = 0; x < W; ++x) g.f.set(x, y, Cell(cells[size_t((13-y)*6+x)] - '0'));
        for (int x = 0; x < W; ++x) g.row14[size_t(x)] = Cell(cells[size_t(x)] - '0');
        const Frame frames[] = {
            {2, 11, 1, 0, 4},
            {2, 11, 0, 0, 4},
            {2, 10, 1, 0, 4},
            {2, 10, 0, 0, 8},
            {3, 10, 0, 3, 4},
            {3, 10, 0, 3, 8},
            {3, 11, 0, 2, 0},
            {3, 11, 0, 2, 22},
            {4, 11, 0, 3, 4},
            {4, 11, 0, 3, 22},
            {5, 10, 1, 0, 4},
            {5, 10, 1, 0, 4},
            {5, 10, 0, 0, 4},
            {5, 9, 1, 0, 4},
            {5, 9, 0, 0, 4},
            {5, 8, 1, 0, 4},
            {5, 8, 0, 0, 4},
        };
        PairControl p = spawnPair(t); p.gain = 0;
        for (size_t i = 0; i < std::size(frames); ++i) {
            const Frame& v = frames[i];
            check(!p.locked && p.x == v.x && p.y == v.y && int(p.upper) == v.upper && p.dir == v.dir,
                  "実機記録 20261117-162513/primary-2 r11 p34 フレーム " + std::to_string(i));
            stepPair(p, g, uint8_t(v.buttons), t);
        }
        check(p.locked, "実機記録と固定のフレームが一致");
        check(int(std::size(frames)) + std::max(t.landFrames - restedFrames(p,t), t.lockWait) + t.nextFrames == 43, "実機記録と次の組までの時間が一致");
    }
    {
        // 20261117-162513/primary-2 r11 p46: 実機で記録した盤面・入力・位置・固定。
        const std::string cells = "040400000000000000000000000000000000000000000000000000000300000200000400000402004355";
        GameField g;
        for (int y = 0; y < 13; ++y) for (int x = 0; x < W; ++x) g.f.set(x, y, Cell(cells[size_t((13-y)*6+x)] - '0'));
        for (int x = 0; x < W; ++x) g.row14[size_t(x)] = Cell(cells[size_t(x)] - '0');
        const Frame frames[] = {
            {2, 11, 1, 0, 4},
            {2, 11, 0, 0, 4},
            {2, 10, 1, 0, 4},
            {2, 10, 0, 0, 12},
            {2, 9, 1, 3, 20},
            {2, 9, 0, 0, 12},
            {2, 8, 1, 3, 4},
            {2, 8, 0, 3, 20},
            {2, 7, 1, 0, 4},
            {2, 7, 0, 0, 20},
            {2, 6, 1, 1, 4},
            {2, 6, 0, 1, 20},
            {2, 5, 1, 2, 4},
            {2, 5, 0, 2, 4},
            {2, 4, 1, 2, 20},
            {2, 4, 0, 3, 12},
            {2, 3, 1, 2, 20},
            {2, 3, 0, 3, 4},
            {2, 2, 1, 3, 12},
            {2, 2, 0, 2, 22},
            {2, 1, 1, 3, 4},
            {2, 1, 0, 3, 12},
            {2, 2, 0, 2, 22},
            {2, 1, 1, 3, 4},
            {2, 1, 0, 3, 12},
            {2, 2, 0, 2, 22},
            {2, 1, 1, 3, 4},
            {2, 1, 0, 3, 12},
            {2, 2, 0, 2, 22},
            {2, 1, 1, 3, 4},
            {2, 1, 0, 3, 12},
            {2, 2, 0, 2, 22},
            {2, 1, 1, 3, 4},
            {2, 1, 0, 3, 12},
            {2, 2, 0, 2, 22},
            {2, 1, 1, 3, 4},
            {2, 1, 0, 3, 12},
            {2, 2, 0, 2, 22},
            {2, 1, 1, 3, 4},
            {2, 1, 0, 3, 12},
            {2, 2, 0, 2, 22},
            {2, 1, 1, 3, 4},
            {2, 1, 0, 3, 12},
            {2, 2, 0, 2, 22},
            {2, 2, 0, 2, 4},
            {2, 2, 0, 2, 4},
            {2, 2, 0, 2, 20},
            {2, 2, 0, 2, 4},
            {2, 2, 0, 2, 4},
            {2, 2, 0, 2, 20},
        };
        PairControl p = spawnPair(t); p.gain = 0;
        for (size_t i = 0; i < std::size(frames); ++i) {
            const Frame& v = frames[i];
            check(!p.locked && p.x == v.x && p.y == v.y && int(p.upper) == v.upper && p.dir == v.dir,
                  "実機記録 20261117-162513/primary-2 r11 p46 フレーム " + std::to_string(i));
            stepPair(p, g, uint8_t(v.buttons), t);
        }
        check(p.locked, "実機記録と固定のフレームが一致");
        check(int(std::size(frames)) + std::max(t.landFrames - restedFrames(p,t), t.lockWait) + t.nextFrames == 77, "実機記録と次の組までの時間が一致");
    }
    {
        // 20261117-162513/primary-2 r11 p47: 実機で記録した盤面・入力・位置・固定。
        const std::string cells = "040400000000000000000000000000000000000000000000000000000300000200003400005402004355";
        GameField g;
        for (int y = 0; y < 13; ++y) for (int x = 0; x < W; ++x) g.f.set(x, y, Cell(cells[size_t((13-y)*6+x)] - '0'));
        for (int x = 0; x < W; ++x) g.row14[size_t(x)] = Cell(cells[size_t(x)] - '0');
        const Frame frames[] = {
            {2, 11, 1, 0, 4},
            {2, 11, 0, 0, 4},
            {2, 10, 1, 0, 4},
            {2, 10, 0, 0, 4},
            {2, 9, 1, 0, 4},
            {2, 9, 0, 0, 4},
            {2, 8, 1, 0, 4},
            {2, 8, 0, 0, 4},
            {2, 7, 1, 0, 4},
            {2, 7, 0, 0, 4},
            {2, 6, 1, 0, 4},
            {2, 6, 0, 0, 4},
            {2, 5, 1, 0, 4},
            {2, 5, 0, 0, 20},
            {2, 5, 0, 1, 4},
            {2, 5, 0, 1, 12},
            {2, 4, 1, 0, 21},
            {0, 4, 0, 1, 12},
            {0, 4, 0, 0, 4},
            {0, 3, 1, 0, 20},
            {0, 3, 0, 1, 4},
            {0, 2, 1, 1, 20},
            {0, 2, 0, 2, 4},
            {0, 1, 1, 2, 20},
            {1, 1, 0, 3, 4},
            {1, 0, 1, 3, 4},
            {1, 0, 0, 3, 20},
            {1, 0, 0, 0, 14},
            {1, 0, 0, 3, 20},
            {1, 0, 0, 0, 4},
            {1, 0, 0, 0, 14},
            {1, 0, 0, 3, 20},
            {1, 0, 0, 0, 4},
            {1, 0, 0, 0, 14},
            {1, 0, 0, 3, 20},
            {1, 0, 0, 0, 4},
            {1, 0, 0, 0, 14},
            {1, 0, 0, 3, 20},
            {1, 0, 0, 0, 4},
            {1, 0, 0, 0, 14},
            {1, 0, 0, 3, 20},
            {1, 0, 0, 0, 4},
            {1, 0, 0, 0, 14},
            {1, 0, 0, 3, 20},
            {1, 0, 0, 0, 4},
            {1, 0, 0, 0, 14},
            {1, 0, 0, 3, 20},
            {1, 0, 0, 0, 4},
            {1, 0, 0, 0, 14},
            {1, 0, 0, 3, 20},
            {1, 0, 0, 0, 4},
            {1, 0, 0, 0, 14},
            {1, 0, 0, 3, 20},
            {1, 0, 0, 0, 4},
            {1, 0, 0, 0, 14},
            {1, 0, 0, 3, 20},
            {1, 0, 0, 0, 4},
            {1, 0, 0, 0, 14},
            {1, 0, 0, 3, 20},
            {1, 0, 0, 3, 4},
            {1, 0, 0, 3, 4},
            {1, 0, 0, 3, 20},
            {1, 0, 0, 3, 4},
            {1, 0, 0, 3, 4},
            {1, 0, 0, 3, 20},
        };
        PairControl p = spawnPair(t); p.gain = 0;
        for (size_t i = 0; i < std::size(frames); ++i) {
            const Frame& v = frames[i];
            check(!p.locked && p.x == v.x && p.y == v.y && int(p.upper) == v.upper && p.dir == v.dir,
                  "実機記録 20261117-162513/primary-2 r11 p47 フレーム " + std::to_string(i));
            stepPair(p, g, uint8_t(v.buttons), t);
        }
        check(p.locked, "実機記録と固定のフレームが一致");
    }
    {
        // 20261117-162513/primary-3 r0 p31: 実機で記録した盤面・入力・位置・固定。
        const std::string cells = "000000500000250000340004360052553242236234553344544523225443235343522432532523544453";
        GameField g;
        for (int y = 0; y < 13; ++y) for (int x = 0; x < W; ++x) g.f.set(x, y, Cell(cells[size_t((13-y)*6+x)] - '0'));
        for (int x = 0; x < W; ++x) g.row14[size_t(x)] = Cell(cells[size_t(x)] - '0');
        const Frame frames[] = {
            {2, 11, 1, 0, 4},
            {2, 11, 0, 0, 4},
            {2, 10, 1, 0, 4},
            {2, 10, 0, 0, 4},
            {2, 9, 1, 0, 16},
            {2, 9, 1, 1, 0},
            {2, 9, 1, 1, 16},
            {2, 10, 1, 2, 0},
            {2, 10, 1, 2, 18},
            {4, 10, 1, 3, 0},
            {4, 10, 1, 3, 8},
            {4, 11, 1, 2, 0},
            {4, 11, 1, 2, 18},
            {5, 11, 1, 3, 0},
            {5, 11, 1, 3, 8},
            {5, 12, 1, 2, 0},
            {5, 12, 1, 2, 21},
            {4, 12, 1, 3, 4},
            {4, 12, 1, 3, 21},
            {3, 12, 1, 0, 4},
            {3, 12, 1, 0, 5},
            {2, 12, 1, 0, 4},
            {2, 12, 1, 0, 5},
            {1, 12, 0, 0, 4},
            {1, 12, 0, 0, 4},
            {1, 12, 0, 0, 4},
        };
        PairControl p = spawnPair(t); p.gain = 0;
        for (size_t i = 0; i < std::size(frames); ++i) {
            const Frame& v = frames[i];
            check(!p.locked && p.x == v.x && p.y == v.y && int(p.upper) == v.upper && p.dir == v.dir,
                  "実機記録 20261117-162513/primary-3 r0 p31 フレーム " + std::to_string(i));
            stepPair(p, g, uint8_t(v.buttons), t);
        }
        check(p.locked, "実機記録と固定のフレームが一致");
        check(int(std::size(frames)) + std::max(t.landFrames - restedFrames(p,t), t.lockWait) + t.nextFrames == 50, "実機記録と次の組までの時間が一致");
    }
    {
        // 20261117-135519/primary r3 p32: 実機で記録した盤面・入力・位置・固定。
        const std::string cells = "000000000006440002650054521055554144446154512422512424415255454125451124545244551211";
        GameField g;
        for (int y = 0; y < 13; ++y) for (int x = 0; x < W; ++x) g.f.set(x, y, Cell(cells[size_t((13-y)*6+x)] - '0'));
        for (int x = 0; x < W; ++x) g.row14[size_t(x)] = Cell(cells[size_t(x)] - '0');
        const Frame frames[] = {
            {2, 11, 1, 0, 4},
            {2, 11, 0, 0, 4},
            {2, 10, 1, 0, 16},
            {2, 10, 1, 1, 0},
            {2, 10, 1, 1, 16},
            {2, 11, 1, 2, 0},
            {2, 11, 1, 2, 18},
            {4, 11, 1, 3, 0},
            {4, 11, 1, 3, 8},
            {4, 12, 1, 2, 0},
            {4, 12, 1, 2, 21},
            {3, 12, 1, 3, 4},
            {3, 12, 1, 3, 21},
            {2, 12, 1, 0, 4},
            {2, 12, 1, 0, 5},
            {1, 12, 1, 0, 4},
            {1, 12, 1, 0, 5},
            {0, 12, 1, 0, 4},
            {0, 12, 1, 0, 4},
            {0, 12, 0, 0, 4},
        };
        PairControl p = spawnPair(t); p.gain = 0;
        for (size_t i = 0; i < std::size(frames); ++i) {
            const Frame& v = frames[i];
            check(!p.locked && p.x == v.x && p.y == v.y && int(p.upper) == v.upper && p.dir == v.dir,
                  "実機記録 20261117-135519/primary r3 p32 フレーム " + std::to_string(i));
            stepPair(p, g, uint8_t(v.buttons), t);
        }
        check(p.locked, "実機記録と固定のフレームが一致");
        check(int(std::size(frames)) + std::max(t.landFrames - restedFrames(p,t), t.lockWait) + t.nextFrames == 46, "実機記録と次の組までの時間が一致");
    }
    {
        // 20261117-134022/primary-26 r5 p34: 実機で記録した盤面・入力・位置・固定。
        const std::string cells = "000004300001440001333044444024311121334312112124416434222634433143442441333211222444";
        GameField g;
        for (int y = 0; y < 13; ++y) for (int x = 0; x < W; ++x) g.f.set(x, y, Cell(cells[size_t((13-y)*6+x)] - '0'));
        for (int x = 0; x < W; ++x) g.row14[size_t(x)] = Cell(cells[size_t(x)] - '0');
        const Frame frames[] = {
            {2, 11, 1, 0, 4},
            {2, 11, 0, 0, 12},
            {3, 11, 0, 3, 20},
            {3, 10, 1, 0, 4},
            {3, 10, 0, 0, 16},
            {3, 9, 1, 0, 4},
            {3, 9, 0, 0, 4},
        };
        PairControl p = spawnPair(t); p.gain = 0;
        for (size_t i = 0; i < std::size(frames); ++i) {
            const Frame& v = frames[i];
            check(!p.locked && p.x == v.x && p.y == v.y && int(p.upper) == v.upper && p.dir == v.dir,
                  "実機記録 20261117-134022/primary-26 r5 p34 フレーム " + std::to_string(i));
            stepPair(p, g, uint8_t(v.buttons), t);
        }
        check(p.locked, "実機記録と固定のフレームが一致");
    }
    {
        // 20261117-134022/primary-26 r1 p31: 実機で記録した盤面・入力・位置・固定。
        const std::string cells = "000000000000000000330001214102332242114232244141412321114424434214431324413221223441";
        GameField g;
        for (int y = 0; y < 13; ++y) for (int x = 0; x < W; ++x) g.f.set(x, y, Cell(cells[size_t((13-y)*6+x)] - '0'));
        for (int x = 0; x < W; ++x) g.row14[size_t(x)] = Cell(cells[size_t(x)] - '0');
        const Frame frames[] = {
            {2, 11, 1, 0, 4},
            {2, 11, 0, 0, 17},
            {1, 11, 0, 1, 4},
            {1, 11, 0, 1, 22},
            {2, 12, 0, 2, 4},
            {2, 12, 0, 2, 6},
            {3, 12, 0, 2, 4},
            {3, 12, 0, 2, 6},
            {4, 12, 0, 2, 4},
            {4, 12, 0, 2, 6},
            {5, 12, 0, 2, 4},
            {5, 12, 0, 2, 4},
        };
        PairControl p = spawnPair(t); p.gain = 0;
        for (size_t i = 0; i < std::size(frames); ++i) {
            const Frame& v = frames[i];
            check(!p.locked && p.x == v.x && p.y == v.y && int(p.upper) == v.upper && p.dir == v.dir,
                  "実機記録 20261117-134022/primary-26 r1 p31 フレーム " + std::to_string(i));
            stepPair(p, g, uint8_t(v.buttons), t);
        }
        check(p.locked, "実機記録と固定のフレームが一致");
        check(int(std::size(frames)) + std::max(t.landFrames - restedFrames(p,t), t.lockWait) + t.nextFrames == 38, "実機記録と次の組までの時間が一致");
    }
    {
        // 20261117-135519/primary r0 p22: 実機で記録した盤面・入力・位置・固定。
        const std::string cells = "000000000000000000000000000000400000510034110054530354541445533151441153433341144411";
        GameField g;
        for (int y = 0; y < 13; ++y) for (int x = 0; x < W; ++x) g.f.set(x, y, Cell(cells[size_t((13-y)*6+x)] - '0'));
        for (int x = 0; x < W; ++x) g.row14[size_t(x)] = Cell(cells[size_t(x)] - '0');
        const Frame frames[] = {
            {2, 11, 1, 0, 4},
            {2, 11, 0, 0, 4},
            {2, 10, 1, 0, 4},
            {2, 10, 0, 0, 4},
            {2, 9, 1, 0, 20},
            {2, 9, 0, 1, 4},
            {2, 8, 1, 1, 4},
            {2, 8, 0, 1, 1},
            {1, 8, 0, 1, 0},
            {1, 8, 0, 1, 22},
            {2, 9, 0, 2, 4},
            {2, 9, 0, 2, 6},
            {3, 9, 0, 2, 4},
            {3, 9, 0, 2, 6},
            {4, 9, 0, 2, 4},
            {4, 9, 0, 2, 4},
            {4, 9, 0, 2, 4},
        };
        PairControl p = spawnPair(t); p.gain = 0;
        for (size_t i = 0; i < std::size(frames); ++i) {
            const Frame& v = frames[i];
            check(!p.locked && p.x == v.x && p.y == v.y && int(p.upper) == v.upper && p.dir == v.dir,
                  "実機記録 20261117-135519/primary r0 p22 フレーム " + std::to_string(i));
            stepPair(p, g, uint8_t(v.buttons), t);
        }
        check(p.locked, "実機記録と固定のフレームが一致");
        check(int(std::size(frames)) + std::max(t.landFrames - restedFrames(p,t), t.lockWait) + t.nextFrames == 44, "実機記録と次の組までの時間が一致");
    }
    {
        // 20261117-135519/primary r3 p65: 実機で記録した盤面・入力・位置・固定。
        const std::string cells = "200000000000000000025050055022662511662661662652666622666666126666526266216566255166";
        GameField g;
        for (int y = 0; y < 13; ++y) for (int x = 0; x < W; ++x) g.f.set(x, y, Cell(cells[size_t((13-y)*6+x)] - '0'));
        for (int x = 0; x < W; ++x) g.row14[size_t(x)] = Cell(cells[size_t(x)] - '0');
        const Frame frames[] = {
            {2, 11, 1, 0, 4},
            {2, 11, 0, 0, 16},
            {2, 11, 0, 1, 4},
            {2, 11, 0, 1, 21},
            {1, 12, 0, 2, 4},
            {1, 12, 0, 2, 5},
            {0, 12, 0, 2, 4},
            {0, 12, 0, 2, 4},
            {0, 12, 0, 2, 4},
            {0, 12, 0, 2, 4},
            {0, 12, 0, 2, 4},
            {0, 11, 1, 2, 4},
            {0, 11, 0, 2, 4},
            {0, 10, 1, 2, 4},
            {0, 10, 0, 2, 4},
        };
        PairControl p = spawnPair(t); p.gain = 0;
        for (size_t i = 0; i < std::size(frames); ++i) {
            const Frame& v = frames[i];
            check(!p.locked && p.x == v.x && p.y == v.y && int(p.upper) == v.upper && p.dir == v.dir,
                  "実機記録 20261117-135519/primary r3 p65 フレーム " + std::to_string(i));
            stepPair(p, g, uint8_t(v.buttons), t);
        }
        check(p.locked, "実機記録と固定のフレームが一致");
        check(int(std::size(frames)) + std::max(t.landFrames - restedFrames(p,t), t.lockWait) + t.nextFrames == 41, "実機記録と次の組までの時間が一致");
    }
    {
        // 20261117-135519/primary r3 p68: 実機で記録した盤面・入力・位置・固定。
        const std::string cells = "200000400000200000225150455122662511662661662652666622666666126666526266216566255166";
        GameField g;
        for (int y = 0; y < 13; ++y) for (int x = 0; x < W; ++x) g.f.set(x, y, Cell(cells[size_t((13-y)*6+x)] - '0'));
        for (int x = 0; x < W; ++x) g.row14[size_t(x)] = Cell(cells[size_t(x)] - '0');
        const Frame frames[] = {
            {2, 11, 1, 0, 4},
            {2, 11, 0, 0, 22},
            {3, 11, 0, 1, 4},
            {3, 11, 0, 1, 22},
            {4, 12, 0, 2, 4},
            {4, 12, 0, 2, 6},
            {5, 12, 0, 2, 4},
            {5, 12, 0, 2, 4},
            {5, 12, 0, 2, 4},
            {5, 12, 0, 2, 4},
            {5, 12, 0, 2, 4},
            {5, 11, 1, 2, 4},
            {5, 11, 0, 2, 4},
        };
        PairControl p = spawnPair(t); p.gain = 0;
        for (size_t i = 0; i < std::size(frames); ++i) {
            const Frame& v = frames[i];
            check(!p.locked && p.x == v.x && p.y == v.y && int(p.upper) == v.upper && p.dir == v.dir,
                  "実機記録 20261117-135519/primary r3 p68 フレーム " + std::to_string(i));
            stepPair(p, g, uint8_t(v.buttons), t);
        }
        check(p.locked, "実機記録と固定のフレームが一致");
        check(int(std::size(frames)) + std::max(t.landFrames - restedFrames(p,t), t.lockWait) + t.nextFrames == 39, "実機記録と次の組までの時間が一致");
    }
}

int main(int argc, char** argv) {
    std::cout << std::unitbuf; // (every line out at once: a run that is stopped still tells how far it came)
    if (argc > 1 && std::string(argv[1]) == "real") {
        printReal();
        return 0;
    }
    try {
        testRule();
        testRecordedTurns();
        testAirborneDownwardQuickTurn();
        if (argc > 1 && std::string(argv[1]) == "rules") {
            std::cout << "実機の回転規則: 成功\n";
            return 0;
        }
        testPublished();
        testNeverLater(TM);
        testNeverLater(OM);
        testNotLaterThanPublished();
        testAsSoonAsAll(TM, 2);
        testAsSoonAsAll(OM, 1);
        printBeside();
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "ALL OK\n";
    return 0;
}
