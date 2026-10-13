#pragma once
// The levels of Juraic: thirty-six, of which the last is X3 as it is (the default of params.h; on the screen
// "Juraic 催促（小）" at Lv 36) and every one below it a weaker X3. The AI that hardly pokes (x3q, "Juraic 催促（極小）")
// plays at the same levels with the same settings (ai/agents.h: lv1q to lv36q), unmeasured. The levels were set one below the other from the top:
// a level is the setting against which the level above it won 59 to 61 % of 1,440 rounds (a tsumo for each
// seat, tools/ai_match --own-tsumo; the rates are in LEVEL_PARENT_PERMILLE). What makes a level weaker, from
// the top down: slower hands (handWait) and a later view of the opponent (seeLate), a narrower and shallower
// search with fewer made-up queues, fewer pieces tried in a trial drop and no key pieces, no second choice for a
// pair, fewer of the pairs on the screen read (x2Depth 3: the pair to place and the next two; 1: the pair to place
// alone), and at two levels a little chance in the choice (b0Noise). Nothing spoils the judgement of the field on
// purpose (a level must not be made by letting the third column grow).
//
// Only neighbours were measured: nothing here says how two levels further apart fare. Level 9 is where the search
// goes from two placements to one, and wins less against level 10 than the others against theirs (37.3 %).
// Level 35 wins 37.6 % against the top; against the top without x3Enough and the guard, 39.9 %.
// The levels were measured with the six fixed sequences of made-up pairs (beamFixedQueues 1) and without the two
// switches the top level plays with, x3Enough and x3Guard; they keep that, whatever the default is.
#include "params.h"

namespace juraic::ai {

constexpr int LEVELS = 36;

struct LevelSettings {
    int handWait, seeLate, beamWidth, beamDepth, beamQueues, trialMaxAdd, keyRuns, thinkAgain, x2Depth, b0Noise;
};

// LEVEL_SETTINGS[n - 1] = level n, for n = 1 .. LEVELS - 1 (level LEVELS changes nothing).
inline constexpr LevelSettings LEVEL_SETTINGS[LEVELS - 1] = {
    {116, 12, 22, 1, 1, 1, 0, 0, 1, 0},   //  1
    {92, 12, 22, 1, 1, 1, 0, 0, 1, 0},    //  2
    {72, 12, 22, 1, 1, 1, 0, 0, 1, 0},    //  3
    {52, 12, 22, 1, 1, 1, 0, 0, 1, 0},    //  4
    {39, 12, 22, 1, 1, 1, 0, 0, 1, 0},    //  5
    {27, 12, 22, 1, 1, 1, 0, 0, 1, 0},    //  6
    {18, 12, 22, 1, 1, 1, 0, 0, 1, 0},    //  7
    {8, 12, 22, 1, 1, 1, 0, 0, 1, 0},     //  8
    {0, 12, 22, 1, 1, 1, 0, 0, 1, 0},     //  9  (one placement searched, the fastest hands)
    {64, 12, 22, 2, 2, 1, 0, 0, 1, 0},    // 10  (from here up: two placements or more)
    {55, 12, 22, 2, 2, 1, 0, 0, 1, 0},    // 11
    {48, 12, 22, 2, 2, 1, 0, 0, 1, 0},    // 12
    {41, 12, 22, 2, 2, 1, 0, 0, 1, 0},    // 13
    {33, 12, 22, 2, 2, 1, 0, 0, 1, 0},    // 14
    {27, 12, 22, 2, 2, 1, 0, 0, 1, 0},    // 15
    {21, 12, 20, 2, 2, 1, 0, 0, 1, 0},    // 16
    {15, 12, 22, 2, 2, 1, 0, 0, 1, 0},    // 17
    {10, 12, 20, 2, 2, 1, 0, 0, 1, 0},    // 18
    {6, 12, 20, 2, 2, 1, 0, 0, 1, 0},     // 19
    {2, 12, 20, 2, 2, 1, 0, 0, 1, 0},     // 20
    {11, 12, 30, 3, 3, 1, 0, 0, 1, 150},  // 21
    {7, 12, 50, 3, 3, 1, 0, 0, 1, 0},     // 22
    {1, 12, 50, 3, 3, 1, 0, 0, 1, 0},     // 23
    {10, 12, 40, 4, 4, 1, 0, 0, 1, 100},  // 24
    {7, 12, 60, 4, 4, 1, 0, 0, 1, 0},     // 25
    {9, 12, 49, 5, 5, 1, 1, 0, 1, 0},     // 26
    {9, 11, 58, 6, 6, 1, 1, 0, 1, 0},     // 27
    {8, 10, 90, 8, 6, 1, 2, 0, 1, 0},     // 28
    {8, 10, 88, 6, 6, 1, 2, 0, 2, 0},     // 29
    {6, 9, 100, 6, 6, 2, 4, 0, 2, 0},     // 30
    {5, 8, 58, 7, 6, 2, 8, 0, 2, 0},      // 31
    {4, 7, 65, 10, 6, 2, 8, 0, 2, 0},     // 32
    {3, 3, 220, 16, 6, 3, 8, 0, 2, 0},    // 33
    {4, 6, 150, 10, 6, 3, 8, 1, 3, 0},    // 34
    {3, 3, 220, 16, 6, 3, 16, 1, 3, 0},   // 35
};

// What the level above won against level n, in thousandths of the 1,440 rounds (index n - 1; level LEVELS has none
// above it). The top against level 35: 898 to 542; without x3Enough and the guard 865 to 575, and with six fixed
// sequences besides 867 to 573.
inline constexpr int LEVEL_PARENT_PERMILLE[LEVELS - 1] = {591, 595, 602, 608, 608, 601, 601, 610, 627, 600, 605, 592, 603, 599, 604, 602, 599, 601,
                                                         599, 594, 603, 606, 590, 595, 596, 595, 607, 607, 594, 608, 604, 600, 600, 594, 624};

// The six groups the screen shows the levels in: 1 to 9, 10 to 20, 21 to 25, 26 to 30,
// 31 to 35, and 36.
constexpr int LEVEL_GROUPS = 6;
constexpr int levelGroup(int level) { return level <= 9 ? 0 : level <= 20 ? 1 : level <= 25 ? 2 : level <= 30 ? 3 : level < LEVELS ? 4 : 5; }

// `p` with the settings of a level. Level LEVELS, and any number that is no level, leaves p as it is.
inline Params levelParams(Params p, int level) {
    if (level < 1 || level >= LEVELS) return p;
    const LevelSettings& l = LEVEL_SETTINGS[level - 1];
    p.handWait = l.handWait, p.seeLate = l.seeLate;
    p.beamWidth = l.beamWidth, p.beamDepth = l.beamDepth, p.beamQueues = l.beamQueues, p.beamFixedQueues = 1;
    p.trialMaxAdd = l.trialMaxAdd, p.keyRuns = l.keyRuns, p.thinkAgain = l.thinkAgain, p.x2Depth = l.x2Depth, p.b0Noise = l.b0Noise;
    p.x3Enough = 0, p.x3Guard = 0; // (the two switches of the top level: the levels were measured without them)
    p.thinkWidthMax = 0; // (the game: the search of a level is never made wider than the level says, however fast the machine)
    return p;
}

} // namespace juraic::ai
