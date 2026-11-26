#pragma once
// Every frame count of the game in one place (1 frame = 1/60 second).
//
// The game and the AI read time only from a Timing object, never from literal
// numbers, so another specification (the arcade version, another simulator, a change
// after a new measurement) is one more Timing value.
//
// Sources of the default values (the present version of the real game):
//  [R] measured tables published by Ratty (2018-12)
//  [V] frame-by-frame measurement of 60 fps videos
//      (https://www.youtube.com/watch?v=aInHUow40q8 and ...?v=-yAgqHYTyD0)
//  [A] the frame data tables of the arcade version (a players' wiki)
//      -- not confirmed for the present version, kept until someone measures it
//  [K] a published article on the quick turn
//  [P] a published AI framework for the real game (its model of the arcade version)
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
    // The fall goes on being counted while the pair lies: a pair that loses its ground - turned or moved off what it
    // lay on - steps down when the step it was counting towards comes due, as if it had been falling meanwhile; one
    // that came due while it lay is made in the frame it loses its ground (a record of the real game: a pair that lay
    // four frames with 100/256 gained, 140, was half a cell lower in the frame it was moved off the column it lay on;
    // over twelve records the pictures fit so, and none fit better with the count gone round). (A record of the real game: a
    // pair lying across two columns for four frames, its child on the higher one, 70/256 gained before it came to
    // rest there, was turned upright and was half a cell lower two frames after the turn - 70 + 6 x 10 = 130;
    // counted on only from the turn the step comes six frames after it, and the move sent four frames after the
    // turn found the column beside it in the way.) A floor kick of a pair that lies leaves that count as it is,
    // liftGain is for a pair kicked in the air. (Two records: a pair that had lain four frames, 2/256 left of its
    // last step, kicked, turned on four frames later, was still a row up four frames after that turn and went over
    // the column beside it - 42 + 8 x 10 = 122; from liftGain it would have stepped down two frames before. Another,
    // kicked so, had stepped down before its move, which the count begun at 0 would not have had it do.)
    // 0: the fall is counted on only once the pair has lost its ground (the frame tables above).
    int fallOnGround = 0;
    // A quick turn whose first press was asked for before the pair appeared (preFrames), the second within 10 frames of
    // its first frame: the child goes below the axis, which stays where it is, when the cell below it is empty. A
    // second press later in the window: not known - the engine takes no turn until the window is over. (Two
    // records of the real game, pairs that appeared a frame or two later than the buttons were sent for, between two
    // columns twelve high, over a column of nine: the first press reached the game before the pair, the second four
    // frames later, and the pair stood with its axis where it had appeared and its child below - one in nine clear
    // pictures; the three moves sent for the other, which the engine had a row up, came to nothing. A pair whose first
    // press came in its first frame went up a row, as the quick turn of the tables has it.) 0: any quick turn with
    // the child above lifts the axis.
    int queuedTurnBelow = 0;
    int lockWait = 2;
    int maxLandings = 8;
    // The real game: the down button on the ground keeps the count of the fall; the pair leaves the hands after eight floor
    // kicks or a lock by itself, and locks once the wait of its last turn is over.
    int groundDropReady = 0;
    int floorKickLimit = 0;
    int quickFirstEndsTurn = 0; // the first press of a quick turn ends the wait of the turn before it
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
    // frames apart, in all eight holds looked at. (The arcade version: 8 and 2 [A].)
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
    // with the first move in the frame of the kick). Same as [P] (half a step).
    int liftGain = 64;
    // Gain right after a quick turn: the next half step comes a full 12.8 frames
    // later. Measured (30 fps recordings, six quick turns): turning
    // the child from above to below puts the pair half a cell higher, from
    // below to above half a cell lower, and 12 frames after each turn it steps
    // down half a cell. Quick turns repeated faster than that keep the pair up.
    int quickTurnGain = 0;
    // A second rotation press within this many frames makes a quick turn.
    int quickTurnWindow = 20; // [P] (arcade)
    // No rotation is accepted for this many frames after a quick turn starts.
    int quickTurnLock = 3; // [K] accepted again from the 4th frame
    // After the lock: the landing animation, then (split, waves, garbage) and
    // the time for the next pair to appear. Their sum, 28, is measured [V][R];
    // the division is the arcade one (16-frame bounce [A], 12 frames in [P]).
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

    // ---- rules of another model of the real game (off: the rules above) ----
    // The model of an AI that plays the real game by its memory publishes its fastest buttons frame by frame for fourteen
    // fields (https://hahho.dev/puyo-ai-controls, read 2026-11-15). Played here, the pairs are where that model has them
    // in every frame until this engine locks them - always earlier than that model does. With the six rules below the
    // pairs are where it has them, and lock when it locks them, in all fourteen (one frame of one sample apart) and in
    // the 56 replays of that page made with buttons a person can press (directions held, buttons pressed before the
    // pair appears: those parts of this engine agree with that model as they are). None of the six has been measured
    // on the real game here: each is a setting of its own, off in DEFAULT_TIMING.
    // A pair that comes to rest by a step of its fall does not lock in that frame, the down button held or not: it locks
    // in a later frame that finds it lying. (Above: it locks in the frame of the landing.) What follows from it: a pair
    // dropped with the down button lies for a frame, and a move pressed in that frame slides it along the ground.
    bool landThenLock = false;
    // No lock by the down button for this many frames from the frame of a turn (its animation). 0: at once.
    int turnLockFrames = 0;
    // A pair lying over its axis's own column with the down button held steps down in the very frame a move takes it off
    // a ledge. (A pair that lies on its child beside it only does not: it steps when the move is over.)
    bool readyOnGround = false;
    // A quick turn: child from below to above, the axis goes to the upper half of the cell below (two half steps down
    // from the upper half, one from the lower), and the fall of that frame goes on from there; child from above to
    // below, the axis goes up one cell in the same half. (Above: Timing::quickTurnGain and the comment of liftGain.)
    bool quickTurnFlip = false;
    // For quickFallFrames frames from the frame of a quick turn the pair gains quickFall more a frame (1/256 of a cell;
    // it may step down more than once in a frame).
    int quickFall = 0, quickFallFrames = 0;
    // The landing animation runs from the first frame that finds the pair lying with its axis's own column under it
    // (under the axis, or under its child below it), and on while the pair lies so or is one half step above where it
    // would; when it is neither, the animation is dropped and starts anew with the next such frame. (Above: from the
    // last landing.) What follows from it: a pair that touches a column one row higher first and slides down from it
    // has the animation behind it (the pair steps down in the frame of the move, Timing::readyOnGround, and is half a
    // step above the lower column); a pair that drops further, or whose child is turned out from under it, has not;
    // a quick turn in a groove keeps it (Timing::quickTurnFlip leaves the pair half a step above the ground). A turn
    // that is no quick turn and leaves the pair no longer lying so drops it too, whatever the fall of that frame does.
    // The author of that model describes it so; the fourteen samples fit, and none of them shows the dropping itself.
    bool firstOwnLanding = false;

    // ---- what records of the real game show (off: the rules above) ----
    // The real game played through its memory, a row for every frame with the pair as memory has it and the buttons
    // sent (play/memory/primary_drive14_win.h; tools/realgame_record_check.cpp holds the records against this engine).
    // Each is a setting of its own, off in DEFAULT_TIMING.
    // With firstOwnLanding: the landing animation runs only while the pair lies with both pieces standing on something
    // (it would lock with no piece falling on). A pair lying on its axis with its child out over a lower column has no
    // clock running (seven pairs lay so for seven frames and the next pair came no sooner; 456 of 458 pairs fit).
    bool restBoth = false;
    // With firstOwnLanding: a turn that is no quick turn drops the clock of the landing animation, also when the pair
    // lies on as it did; it starts anew in that frame. (One pair in the records, and what the author of the other
    // model says of it; a quick turn in a groove keeps the clock: one pair.)
    bool turnDropsRest = false;
    // After a floor kick that leaves the pair in the air (it was half a cell above the ground) the down button does not
    // make it fall: the pair steps down when its own fall brings the next half step, from Timing::liftGain, the frame of
    // the kick not counted - seven frames in the upper half, the half step in the eighth (twenty kicks, all so).
    bool liftWaits = false;
    // With firstOwnLanding: a pair that lay and was kicked up from the floor (turned child down with no room below) has
    // no clock of the landing animation until it is turned again - lying on its child for the seven frames after that
    // turn, the next pair came no sooner (six pairs of six in the records; a turn of a lying pair with no kick starts
    // the clock anew: seventeen of seventeen, and so does a turn after such a kick: seven pairs).
    bool liftEndsRest = false;
    // A pair that lies and is turned so that nothing is under it any more falls half a cell in that very frame,
    // whatever is held - also in the second frame of a move; not when the turn takes the child below the axis (records of
    // the real game: 71 such turns of 71, four of them with no down button).
    bool turnOffFalls = false;
    // Timing::liftWaits also for a pair that stands on its child after the kick: moved off that ledge with the
    // down button held, it hangs until its own half step comes, in the eighth frame after the kick (two pairs).
    bool liftHolds = false;
    // Timing::quickRise: the down button makes the pair fall at once, in the frame of the quick turn too (a pair in
    // the upper half was back where it was in that frame: three pairs; one frame later: four).
    bool riseYields = false;
    // Timing::quickRise, a pair that cannot fall from where the quick turn left it (its child stands on the column):
    // the down button pressed while it is held there ends the hold, and the pair lies where it is, in the lower half -
    // it does not go up into the upper half (seven pairs: none went up; of 28 held without the button every one did,
    // in the sixth frame).
    bool riseStays = false;
    // Timing::riseStays, the down button let go again: the pair is held anew, and goes up into the upper half as after
    // the quick turn itself (two pairs: the button pressed one frame and two, the first was in the upper half seven
    // frames after the button was let go, the second not yet after five, when it was turned).
    bool riseAgain = false;
    // Timing::quickRise: the count of the pair's own fall goes on in the frame of the quick turn and while the pair is
    // held in the lower half (three pairs made their next half step of their own in the thirteenth frame from the frame
    // of the turn, as ten of 128 a frame make it; without it the model had that step five frames later).
    bool riseCounts = false;
    // Timing::quickFall only after a quick turn that takes the child from below to above, and with the count of the
    // fall kept as it is elsewhere (Timing::fallOnGround): a pair that fell with the down button held made two half
    // steps in the third frame after such a turn, as 32/256 a frame from the frame of the turn make it.
    bool quickFallUpOnly = false;
    int quickDownDrag = 0; // 子が下になる反転中、下入力による落下から引く量（1/256マス）。
    // A quick turn that takes the child from above to below and the axis up: the axis goes into the lower half of the
    // cell above and is held there for this many frames
    // (it neither falls nor lies), then it is in the upper half and falls on from there (ten turns: five frames in the
    // lower half, the upper half in the sixth; with the down button held it is down again in that same frame).
    // 0: as above (Timing::quickTurnFlip or not).
    int quickRise = 0;

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
    // The same tables: every member equal.
    bool operator==(const Timing&) const = default;
};

// The present version of the real game.
inline constexpr Timing DEFAULT_TIMING{};

// DEFAULT_TIMING with the six rules of the other model (Timing::landThenLock and those after it).
inline constexpr Timing otherModel(Timing t) {
    t.landThenLock = true, t.turnLockFrames = 7, t.readyOnGround = true, t.quickTurnFlip = true;
    t.quickFall = 32, t.quickFallFrames = 8, t.firstOwnLanding = true;
    return t;
}
inline constexpr Timing OTHER_MODEL_TIMING = otherModel(Timing{});

// The frame counts with which the real game's records agree best (play/memory/primary_timing14.h has the reasons one
// by one): of the other model's rules a pair that lies a frame before it locks, no lock for seven frames after a turn,
// the quick turn of a pair with its child below, the clock of the landing from the first lying; then the four rules
// read off the records, and the fall counted on while a pair lies (Timing::fallOnGround, read off the pictures of the
// real game: it has the two pairs of the records here that stepped as they were moved off a ledge do so, thirteen
// frames after they came to rest). Not of them: a step in every frame a pair is moved off a ledge (29 of 32 did not),
// the faster fall after a quick turn (not seen either way).
inline constexpr Timing recordedRules(Timing t) {
    t.fallOnGround = 1, t.queuedTurnBelow = 1; // (as the mode that reads the pictures has them: realGameTiming, play/realgame_read.h)
    t.landThenLock = true, t.turnLockFrames = 7, t.quickTurnFlip = true, t.firstOwnLanding = true;
    t.readyOnGround = false, t.quickFall = 32, t.quickFallFrames = 8, t.quickFallUpOnly = true;
    t.restBoth = true, t.turnDropsRest = true, t.liftWaits = true, t.quickRise = 5;
    t.liftEndsRest = true, t.turnOffFalls = true, t.liftHolds = true, t.riseYields = true, t.riseStays = true, t.riseCounts = true, t.riseAgain = true;
    t.quickTurnLock = 7; // as read on the real game
    t.groundDropReady = 1, t.floorKickLimit = 1, t.quickFirstEndsTurn = 1;
    t.lockWait = 6; // a pair that has rested nine frames before the lock can precede the next by 19 frames
    t.nextShown = 2; // the new pair is known two frames before the first frame it can be moved in
    return t;
}

} // namespace juraic
