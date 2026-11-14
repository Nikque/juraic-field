#pragma once
// The mode in which Juraic plays 2P of the real game (play/realgame_win.h): which pictures the thread of the play
// follows, and when it drives the pad. Plain C++ (tests/realgame_tests.cpp runs it with pictures that come late, twice,
// out of their order or not at all, and a pad the game takes at times of its own).
//
// Times are in 100 ns of the clock of the pictures (QueryPerformanceCounter on Windows); a frame is 1/60 s of it.
// - The first picture is of the frame of the time it was shown, to the nearest frame (the capture gives that time);
//   each one after it is as many frames after the last one followed as lie between the times the two were shown, to the
//   nearest frame: the pictures' own count of the game's frames. A picture shown at the time of the last one followed or
//   before, or less than half a frame after it, is not followed: the same picture twice, or an older one, which would
//   put the pair in play back where it was.
// - The game reads the pad once a frame, just after it shows a picture (in the records of the real game buttons set up
//   to the time a picture was shown were in the game a frame sooner than buttons set a tenth of a frame after it). The
//   pad is driven after the pictures that came have been followed, and its buttons are set SEND_AFTER after the last
//   picture was shown (sendAfter), or as soon as the driver has decided when that is later: away from where the game
//   reads them, so that the time the capture, the reading and the driver took - a few milliseconds more or less - does
//   not decide which frame of the game takes them. (Set as soon as decided, they were set from half a frame to a
//   twentieth of a frame before the game read them in one record, and to either side of it in the records made on a
//   busy machine.)
// - The pad is driven as the frame of the game that reads the buttons, counted as the pictures count: the frame of the
//   last picture followed, and one on for each time the game has read the pad between that picture shown and the buttons
//   set (frameSet; READ_BY after each frame of 1/60 s from the picture shown). (Driven as the frame of the clock it was
//   driven in, the frame of the drive and the frame of the picture were two counts of one time, each cut at another
//   point of a frame: the pictures' time goes round the clock's frame once in six minutes on the real game, and each
//   time it passed one of those points the buttons reached the game a frame sooner or later as the driver counted them,
//   the driver's lag following some pairs late - five pairs of one round went elsewhere than the AI chose between two
//   such changes.)
// - In a frame no picture came in, the pad is driven all the same, FILL_AT after the picture was due to be shown, a
//   frame after the last one, and a frame after each such drive: the taps and the lets-go planned for the frames go out
//   in them, not when the next picture comes (in the records of the real game 0.9% of the frames had no picture - up to
//   17% on a busy machine -, and a tap of two frames was held three or more in 6 to 7% of the taps, nearly all of them
//   over such a frame). FILL_AT is after the time a picture's buttons are set and well before the game reads the pad
//   again. The picture after one left out comes in before that time, as a rule: the pad is then driven at once for the
//   frame with no picture (behind), when the game does not read it for SEND_EARLY yet, and for the picture after.
// - Until then the driver's engine plays on as the AI planned, as under a light over the screen (no pair left to fall by
//   itself for a few frames of no picture: held as the last drive left it, a tap ran into the pad's own repeat). No
//   picture for STALL_FRAMES frames (the window gone, made small or covered, the game stopped drawing): the pad holds
//   nothing until the next picture comes - by then no new pair can be begun from the screen either.
#include <algorithm>
#include <cstdint>

namespace play::realgame {

constexpr int64_t TIME_SECOND = 10000000; // (100 ns)
// The frame of the clock a time is in, the frame of a picture shown at a time, and the time a frame of the clock begins.
inline int64_t frameAt(int64_t time) { return time / TIME_SECOND * 60 + time % TIME_SECOND * 60 / TIME_SECOND; }
inline int64_t frameShown(int64_t time) { return (time * 60 + TIME_SECOND / 2) / TIME_SECOND; }
inline int64_t timeOfFrame(int64_t frame) { return frame / 60 * TIME_SECOND + frame % 60 * TIME_SECOND / 60; }

class PlayPace {
public:
    static constexpr int64_t FRAME = TIME_SECOND / 60;
    static constexpr int64_t FILL_AFTER = FRAME * 27 / 20; // (with sendAfter -1: after the last picture came)
    static constexpr int64_t FILL_AT = FRAME * 3 / 4;
    static constexpr int64_t READ_BY = FRAME / 10;
    static constexpr int64_t SEND_AFTER = FRAME * 3 / 10;
    static constexpr int64_t SEND_EARLY = FRAME / 5;
    static constexpr int STALL_FRAMES = 60;
    static constexpr int RECOUNT_FRAMES = 120;
    bool fill = true; // the pad is driven in the frames no picture came in
    // The pad driven for a picture is set no sooner than this long after the picture was shown (-1: at once, as soon as
    // the driver has decided): the buttons then reach the game always as far from where it reads the pad in its frame,
    // not sooner or later by the time the capture, the reading and the driver took. (In rounds drawn with the time
    // buttons are set in near where the game reads the pad, the pairs where the AI chose went from 99.45% to 99.95% and
    // 2P won 72 rounds of 200 for 59 so. A time that brings the buttons nearer to where it reads them does the
    // opposite.)
    int64_t sendAfter = SEND_AFTER;

    // Pictures not followed (shown when the last one followed was, an older one), pictures taken for the frame after the
    // last one followed, frames left out between two pictures followed, drives in frames no picture came in, and times no
    // picture came for STALL_FRAMES frames.
    long twice = 0, back = 0, next = 0, missing = 0, filled = 0, stalls = 0;

    void reset() {
        const int64_t after = sendAfter;
        *this = PlayPace{fill};
        sendAfter = after;
    }

    // A picture shown at `shown` (frameShown: of the frame `frame`) came in at `time`: the frame it is followed as, or -1
    // when it is not followed.
    int64_t take(int64_t frame, int64_t shown, int64_t time) {
        if (followed >= 0) {
            if (shown <= lastShown) {
                ++(frame == followed ? twice : back);
                return -1;
            }
            // (counted on from the last picture followed, by the frames of 1/60 s between the times the two were shown:
            //  the game's frames are a little longer than the clock's - on the real game by one frame in six minutes -,
            //  and counted by the frame of the clock each was shown in, once in that time a frame of the clock had no
            //  picture though none was left out, and the frames of the play jumped one. After RECOUNT_FRAMES with no
            //  picture, by the clock again.)
            const int64_t step = (shown - lastShown + FRAME / 2) / FRAME;
            if (step == 0) {
                ++twice;
                return -1;
            }
            const int64_t counted = step <= RECOUNT_FRAMES ? followed + step : std::max(frame, followed + 1);
            next += counted > frame;
            missing += counted - followed - 1;
            frame = counted;
        }
        followed = frame, lastShown = shown, came = time, fills = 0, stalledNow = false;
        return frame;
    }
    // The frame of the last picture followed (-1: none).
    int64_t last() const { return followed; }
    // The frame the pad is driven as when its buttons are set at `time`: that of the last picture followed, one on for
    // each time since that picture was shown the game has read the pad by then (READ_BY after each frame from it). -1: no
    // picture followed yet.
    int64_t frameSet(int64_t time) const {
        if (followed < 0) return -1;
        const int64_t after = time - lastShown - READ_BY;
        return followed + (after > 0 ? (after + FRAME - 1) / FRAME : 0);
    }
    // At `time`, the pictures that came followed: the frame the game reads next - that of the last picture followed - is
    // after `driven`, the last frame the pad was driven as, and the game does not read the pad for SEND_EARLY yet. (The
    // picture before the last one was left out and this one came before the pad was driven with none: the pad is driven
    // at once for the frame that picture's buttons were for, then for this picture as for any.)
    bool behind(int64_t time, int64_t driven) const { return fill && sendAfter >= 0 && followed > driven && driven >= 0 && lastShown - time >= SEND_EARLY; }
    // The time by which the pad is to be driven when no picture comes (-1: none yet, or not filling).
    int64_t due() const {
        if (!fill || came < 0) return -1;
        return (sendAfter >= 0 ? lastShown + FRAME + FILL_AT : came + FILL_AFTER) + int64_t(fills) * FRAME;
    }
    // The pad driven at `time` with no picture since the last drive (due() has come).
    void filledAt(int64_t time) {
        ++fills, ++filled;
        if (stalled(time) && !stalledNow) stalledNow = true, ++stalls;
    }
    // No picture has come for STALL_FRAMES frames: the pad is to hold nothing.
    bool stalled(int64_t time) const { return came >= 0 && time - came > STALL_FRAMES * FRAME; }
    // The time the pad driven for the last picture at `time` is set (sendAfter).
    int64_t sendAt(int64_t time) const { return sendAfter >= 0 && came >= 0 ? std::max(time, lastShown + sendAfter) : time; }

    PlayPace() = default;
    explicit PlayPace(bool filling) : fill(filling) {}

private:
    int64_t followed = -1; // the frame of the last picture followed,
    int64_t lastShown = 0; // ... the time it was shown,
    int64_t came = -1;     // ... the time it came in,
    int fills = 0;         // ... and the drives since with no picture
    bool stalledNow = false;
};

} // namespace play::realgame
