#pragma once
// Every frame count of the game in one place (1 frame = 1/60 second).
//
// The game and the AI read time only from a Timing object, never from literal
// numbers, so another specification (arcade Tsu, another simulator, a change
// after a new measurement) is one more Timing value.
//
// Sources of the default values (the eSports version of the real game, Tsu rule):
//  [R] measured tables by Ratty, https://puyo-camp.jp/posts/71019 (2018-12)
//  [V] frame-by-frame measurement of 60 fps videos
//      (https://www.youtube.com/watch?v=aInHUow40q8 and ...?v=-yAgqHYTyD0)
//  [A] arcade Tsu, https://puyonexus.com/wiki/Puyo_Puyo_Tsu/Frame_Data_Tables
//      -- not confirmed for eSports, kept until someone measures it
//  [K] https://puyo-camp.jp/posts/135700
//  [M] the real game on this machine, the picture taken frame by frame and the
//      inputs written down with their clock
#include "../core/field_bits.h"
#include <cstdlib>

namespace juraic {

struct Timing {
    // ---- the falling pair, frame by frame ----
    // The pair moves in half cells. Each frame it gains this many 1/256 of a
    // cell; it steps down whenever half a cell has been gained.
    int fallPerFrame = 10;      // [V] 25.6 frames per cell
    int softDropPerFrame = 128; // [V][R] half a cell per frame
    // The pair appears half a cell above the 12th row and falls from the frame
    // it appears: with gain 0 the steps come 12, 25, 38, 51, 63, 76 frames later. [V]
    int spawnGain = 0;
    // Frames the pair may rest on the ground before it locks, counted from each
    // landing, and the number of landings that locks it at once. Standing on
    // the child piece right after a floor kick is a landing too.
    // Measured on the real game (30 fps recordings): left alone
    // after landing, the pair locked 28 to 31 frames later (two samples); spun
    // on the floor, it rested 18, 12, 8 and 10 frames without locking and
    // locked the moment it stood up after the fourth kick from the ground,
    // its eighth landing. The arcade values are the same.
    //
    // [M], 19 pairs left alone from the landing: the next pair was seen 57 frames
    // after the pair came to rest, and 56 in all six cases in which 8/256 of a cell
    // was left over from the last step (the others: 0, 2, 4, 6). And two pairs that
    // lay on the ground untouched and then got the down key, 27 and 41 frames after
    // they had come to rest: both locked there (the point of a lock by the down key
    // was added) and the next pair was seen 13 frames later. So a pair left alone is
    // not locked after 30 frames, as the 27 frames from a landing with the down key
    // to the next pair had suggested: it locks 44 frames after it came to rest (43
    // with 8/256 left over), and the next pair is seen 13 frames after a lock; after
    // a landing with the down key the bounce of the landing, 14 frames, comes first.
    // In the numbers of this engine: the pair locks when the fall it would have made
    // since its last step reaches lockGain (10 a frame from what was left over: 437
    // and 438 fit), which is 32 frames after a step down would have been due again;
    // the landing animation (landFrames) runs from the landing, and what is left of
    // it at the lock is waited for, lockWait frames at least, before nextFrames.
    int lockGain = 438;
    int lockWait = 2;
    int maxLandings = 8;
    // Points of the down key at the end of a drop, beside the point for every row
    // (Rules::softDropBonus). [M], a versus match read frame by frame with its score:
    // a pair that lands with the down key held gets 2 points in the frame after (15
    // pairs; 11 rows down to the floor made 13 points), a pair that came to rest by
    // itself and is locked with the down key gets 2 when that is 7 frames later and
    // 1 when it is 27 or 41 frames later, a pair left alone gets none, a quick drop
    // none (4 pairs). Here: dropLockPoints for a lock by the down key, and
    // dropLandPoints more when a step down would not yet have been due again since
    // the landing.
    int dropLockPoints = 1;
    int dropLandPoints = 1;
    // Horizontal auto-repeat: frames before the first repeat, then the interval.
    // [M]: the second move came 10 frames after the first and the next ones 2
    // frames apart, in all eight holds looked at. (Arcade Tsu: 8 and 2 [A].)
    int repeatDelay = 10;
    int repeatInterval = 2;
    // Pre-input (Rules::preInput): what is asked for in the last so many frames
    // before a pair can be moved is done in the pair's first frame: a rotation
    // (the button pressed; the last press counts, a button only held does nothing)
    // and one move (a direction pressed, or the auto-repeat of a held direction
    // coming round). The button may have been let go again. [M]: a rotation pressed
    // 10.7 frames before the pair was seen turned it, presses 11.3, 11.4 and more
    // frames before did not; with the 2.05 to 2.3 frames from an input to the
    // picture that is 9 frames and 10 frames before the first frame. The window
    // opens one frame before the next-pair display begins to move. A direction
    // pressed before the window and let go in it did nothing (three cases), one
    // held 12 frames and let go 6 frames before the first frame moved the pair.
    int preFrames = 9;
    // A horizontal move takes this many frames, during which the down key is ignored.
    int moveFrames = 2; // [R] two frames per column
    // A floor kick (and a quick turn) lifts the pair exactly one cell, keeping
    // its half-cell state, and sets the gain to this value: the next half step
    // comes about 7 frames later. Measured on the real game, 30 fps recording:
    // kicked while still half a cell above the ground, the pair
    // stayed half a cell above the next row for 6 to 8 frames and then came to
    // rest; kicked on the ground, it stood one row higher at once. So a pair
    // kicked before it has finished its last half step stays in the upper cell
    // for about 19 frames (7 + 13), which is what makes hops over three and
    // four columns possible; kicked from rest it has 7 frames (three columns
    // with the first move in the frame of the kick). Same as puyoai (half a step).
    int liftGain = 64;
    // Gain right after a quick turn: the next half step comes a full 12.8 frames
    // later. Measured (30 fps recordings, six quick turns): turning
    // the child from above to below puts the pair half a cell higher, from
    // below to above half a cell lower, and 12 frames after each turn it steps
    // down half a cell. Quick turns repeated faster than that keep the pair up.
    int quickTurnGain = 0;
    // A second rotation press within this many frames makes a quick turn.
    int quickTurnWindow = 20; // puyoai (arcade)
    // No rotation is accepted for this many frames after a quick turn starts.
    int quickTurnLock = 3; // [K] accepted again from the 4th frame
    // After the lock: the landing animation, then (split, waves, garbage) and
    // the time for the next pair to appear. Their sum, 28, is measured [V][R];
    // the division is the arcade one (16-frame bounce [A], 12 frames in puyoai).
    int landFrames = 16;
    int nextFrames = 12;
    // The window of the next pairs moves on before the pair appears. [M], six
    // appearances (three in a game alone, three in a versus match, one of them after
    // a chain), all the same: the window begins to move 7 frames before the pair is
    // seen on the field; of the pair that comes into it the upper piece shows 4 frames
    // before, both colours can be read 3 frames before, and it stands in its place
    // 1 frame before. The pair is seen on the field one frame before it can be moved.
    // Here a pair can be moved in the frame it appears, so counted from that frame:
    // the window moves from nextSlide frames before it (play/view.h, for drawing
    // only), and the new pair is on the screen from nextShown frames before it (the
    // AI knows it from then on: ai/observe.h).
    int nextSlide = 8;
    int nextShown = 4;
    // Extra frames before the very first pair of a round.
    int firstSpawn = 7; // [R]

    // ---- whole actions (the tables of [R]) ----
    // Extra frames when the pair splits and one piece falls `distance` cells.
    // Equal to the arcade free-fall table plus 9.
    int split[12] = {0, 19, 24, 28, 31, 34, 37, 40, 42, 44, 46, 48};
    // One wave: no piece falls afterwards (quick) / the longest fall is 1 cell /
    // every further cell. Distances of 10 and more are extrapolated in [R] too.
    int waveQuick = 55;
    int waveBase = 80;
    int wavePerCell = 2;
    // What happens inside a wave, for drawing only (the game uses the totals
    // above). Measured frame by frame on a 60 fps video of the real game
    // (https://www.youtube.com/watch?v=RWWs-JokGto, seven waves of one chain),
    // counted from the frame the score of the wave appears:
    // the cleared pieces blink from frame 7 (two frames hidden, two shown),
    // show their popping faces from frame 24 and are gone from frame 44.
    // In frame 55 the pieces above start to fall, half a cell per frame, and
    // the next wave starts 23 frames after the last one has landed
    // (55 + 2 * cells + 23 = the totals above); they bounce for about 16 of those.
    int popBlink = 7;
    int popBlinkPeriod = 4;
    int popFace = 24;
    int popGone = 44;
    int waveFallPerFrame = 128; // in 1/256 of a cell
    // Garbage: frames until a garbage piece has settled in the cell (x, y);
    // the slowest cell of a drop counts. [R] has no value for row 14 of the
    // third column (a piece cannot rest there); 21 is a filler.
    int garbageCell[14][W] = {
        {60, 59, 64, 56, 62, 58}, {58, 57, 62, 54, 60, 56}, {56, 55, 60, 52, 58, 54}, {54, 53, 57, 50, 56, 52},
        {52, 51, 55, 48, 53, 49}, {50, 48, 53, 46, 51, 47}, {47, 46, 50, 44, 49, 45}, {45, 44, 47, 42, 46, 42},
        {42, 41, 44, 39, 43, 40}, {39, 38, 41, 36, 40, 37}, {36, 35, 38, 33, 37, 34}, {32, 31, 34, 30, 33, 30},
        {28, 27, 29, 26, 28, 26}, {22, 21, 21, 20, 22, 21}};
    // Extra frames of the shaking screen by the number of garbage pieces dropped.
    int garbageShake[31] = {0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  5,  6,  8,  9,  11, 12,
                            13, 14, 15, 16, 18, 19, 20, 21, 21, 22, 23, 24, 24, 25, 25};

    // Fastest time from the appearance of a pair to the appearance of the next
    // one when the pair locks with the axis piece in column x, row y (0 = floor)
    // with the down key held: 28 frames at the spawn cell, two more per column
    // and row. (The row before a split: [R] counts by where the pair stopped.)
    int placeBase = 28;
    int placePerColumn = 2;
    int placePerRow = 2;
    int placeFrames(int x, int y) const {
        return placeBase + placePerColumn * std::abs(x - 2) + placePerRow * (11 - y);
    }
    // A split near the top happens during the rotation; [R] adds these frames
    // by the cell of the axis piece.
    int splitFrames(int distance, int axisX, int axisY) const {
        if (distance <= 0) return 0;
        const int base = distance <= 11 ? split[distance] : split[11] + 2 * (distance - 11);
        const int reach = 3 - std::abs(axisX - 2) - (11 - axisY); // 3 at the spawn cell
        return base + (axisY >= 9 && axisY <= 11 && reach > 0 ? 2 * reach : 0);
    }
    // For drawing: frames after the lock at which the falling piece of a split
    // has come down `distance` cells. The rest of the split time passes after
    // it has landed. Measured on the same video as the waves, two splits:
    // one of two cells (0.6 cells after 6 frames, 1.3 after 10, landed after
    // 14 or 15) and one of three cells (one cell after 9 frames, two after 14,
    // landed after 18 or 19).
    int splitRest = 10;
    // The bounce of a piece that has landed, for drawing: one shape per frame
    // (0 normal, 1 flat and wide, 2 narrow, 3 a little flat). Read off the same
    // video frame by frame. bounceLock starts in the frame the pair locks;
    // bounceFall starts in the frame after a piece of a split or of a wave has
    // come down (one sample each).
    int bounceLock[12] = {1, 3, 2, 2, 2, 3, 1, 1, 0, 2, 2, 2};
    int bounceFall[16] = {1, 1, 1, 2, 2, 1, 1, 1, 1, 2, 2, 1, 3, 2, 0, 3};
    static constexpr int BOUNCE_LOCK = 12, BOUNCE_FALL = 16;
    int splitFall(int distance) const { return distance <= 0 ? 0 : splitFrames(distance, 0, 0) - splitRest; }
    int waveFrames(int longestFall) const {
        return longestFall <= 0 ? waveQuick : waveBase + wavePerCell * (longestFall - 1);
    }
    // placed[x] garbage pieces were added on top of columns that were heights[x] high.
    int garbageFrames(const int heights[W], const int placed[W]) const {
        int total = 0, slowest = 0;
        for (int x = 0; x < W; ++x) {
            if (placed[x] <= 0) continue;
            total += placed[x];
            const int y = heights[x] < 13 ? heights[x] : 13;
            if (garbageCell[y][x] > slowest) slowest = garbageCell[y][x];
        }
        return total ? slowest + garbageShake[total < 30 ? total : 30] : 0;
    }
};

// The eSports version of the real game, Tsu rule.
inline constexpr Timing DEFAULT_TIMING{};

} // namespace juraic
