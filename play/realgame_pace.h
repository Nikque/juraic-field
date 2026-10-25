#pragma once
// The mode in which Juraic plays 2P of the real game (play/realgame_win.h): which pictures the thread of the play
// follows, and when it drives the pad. Plain C++ (tests/realgame_tests.cpp runs it with pictures that come late, twice,
// out of their order or not at all, and a pad the game takes at times of its own).
//
// Times are in 100 ns of the clock of the pictures (QueryPerformanceCounter on Windows); a frame is 1/60 s of it.
// - A picture is of the frame of the time it was shown, to the nearest frame (the capture gives that time). A picture
//   shown at the time of the last one followed or before is not followed: the same picture twice, or an older one, which
//   would put the pair in play back where it was. One shown half a frame or more after it, of the same frame to the
//   nearest (shown near the middle between two frames), is of the frame after it: a picture seen again need not be a
//   game that stood still.
// - The pad is driven after the pictures that came have been followed, as the frame of the clock it is driven in (the
//   frame the game takes the buttons sent then from: in 32 records of the real game played again, the engine of the
//   driver had the pair where 89.2% of the pictures showed it so, 89.9% with the frame of the newest picture instead,
//   and was put right 975 and 1059 times).
// - In a frame no picture came in, the pad is driven all the same, FILL_AFTER after the last picture came and a frame
//   after each such drive: the taps and the lets-go planned for the frames go out in them, not when the next picture
//   comes (in the records of the real game 0.9% of the frames had no picture, and a tap of two frames was held three or
//   more in 6 to 7% of the taps, nearly all of them over such a frame).
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
    static constexpr int64_t FILL_AFTER = FRAME * 27 / 20;
    static constexpr int STALL_FRAMES = 60;
    bool fill = true; // the pad is driven in the frames no picture came in
    // The pad driven for a picture is set no sooner than this long after the picture was shown (-1: at once, as soon as
    // the driver has decided): the buttons then reach the game always as far from where it reads the pad in its frame,
    // not sooner or later by the time the capture, the reading and the driver took. (In rounds drawn with the time
    // buttons are set in near where the game reads the pad, the pairs where the AI chose went from 99.45% to 99.95% and
    // 2P won 72 rounds of 200 for 59 so. A time that brings the buttons nearer to where it reads them does the
    // opposite: which time keeps them far from it on the real game, records of the second kind tell - RECORD_TIME and
    // RECORD_SENT.)
    int64_t sendAfter = -1;

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
            if (frame <= followed) {
                if (shown - lastShown < FRAME / 2) {
                    ++twice;
                    return -1;
                }
                frame = followed + 1, ++next;
            }
            missing += frame - followed - 1;
        }
        followed = frame, lastShown = shown, came = time, fills = 0, stalledNow = false;
        return frame;
    }
    // The frame of the last picture followed (-1: none).
    int64_t last() const { return followed; }
    // The time by which the pad is to be driven when no picture comes (-1: none yet, or not filling).
    int64_t due() const { return fill && came >= 0 ? came + FILL_AFTER + int64_t(fills) * FRAME : -1; }
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
