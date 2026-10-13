#pragma once
// Two players at the level of whole moves, in the real order of time.
//
// playTurn (game/turn.h) runs one move of one player to its end at once, so the
// opponent sees the garbage of a chain only when the chain is over. Here every
// wave of both players happens in its own frame, as in Round: garbage is
// offset wave by wave, the rate is the one of the frame of the wave, a chain
// started while the opponent's chain is running takes from garbage that is
// not confirmed yet, and garbage falls on a player whose move ends in the frame
// the opponent's chain ends or later.
//
// The frames of one move (Round::frame of the step in which it happens):
//   S   the pair appears (it can be moved in this frame)
//   L   the pair locks. Fastest input: S + placeFrames - (landFrames + nextFrames)
//   W1  L + splitFrames + landFrames: the first wave, or nothing clears
//   Wk+1 = Wk + waveFrames(longest fall of wave k)
//   D   the frame in which nothing clears any more (W1 without a chain): the
//       chain ends, the garbage it sent is confirmed, and at the end of this
//       frame, after both players have moved, the own confirmed garbage falls
//   S'  D + garbageFrames + nextFrames: the next pair
// Inside a frame: player 0, player 1, then the garbage of both (Round::step).
//
// Only frames in which something happens are played. tests/ai_tests.cpp plays
// the same moves here and in Round and compares.
//
// The fastest input of the Timing tables is the straight way: down held, the
// rotation at once, the direction every second frame. Past a column twelve
// high, beside one that stands in the way of the rotation, or to a placement
// whose axis piece comes to rest in the thirteenth row, there is no such way:
// the pair is lifted by a floor kick or goes round, and locks up to 14 frames
// later than the tables say (2 frames earlier in a few cases). Counted with
// a probe on the pairs of saved rounds: with no column higher than
// nine from the column the pair appears in to its placement, one column
// further on both sides included, the tables always hold; with one of ten,
// 85 of 1,039 placements in the twelfth row lock 2 frames later; from eleven
// on the other cases begin. With tallPaths the frame of the lock is then
// taken from the buttons InputPlanner finds.
#include "input.h"
#include "observe.h"

namespace juraic::ai {

constexpr uint64_t DUEL_GARBAGE_STREAM = 0x6475656C; // garbage columns inside the AI's reading

enum class DuelPhase : uint8_t {
    Spawn,   // the next pair appears in frame `at`
    Control, // a pair is in the player's hands: place() is awaited (no event)
    Check,   // in frame `at` the landing or a wave is over: the next wave, or the end of the chain
    Settle,  // at the end of frame `at` the confirmed garbage falls
    Dead,
};

struct DuelPlayer : PlayerCore {
    DuelPhase phase = DuelPhase::Spawn;
    int64_t at = 0;          // frame of the next event (Spawn, Check, Settle)
    int64_t spawnFrame = -1; // S of the pair in play; -1 = observed in mid-air
    int64_t lockFrame = -1;  // L of the last pair put
    int nextPair = 0;        // number of the pair that appears next
    int moves = 0;           // pairs locked so far
    ChainState chain;        // the chain in progress, or the last one
    int dropped = 0;         // garbage pieces taken by the last drop
    int fallen = 0;          // garbage pieces taken by all drops since the Duel was made
};

struct Duel {
    const Rules* rules = nullptr;   // of the Observation, which must outlive the Duel
    const Timing* timing = nullptr;
    int64_t frame = 0; // every frame before this one has been played (as Round::frame)
    RoundState state = RoundState::Playing;
    DuelPlayer player[2];
    // The columns of garbage that does not fill a row are random in the game;
    // the AI cannot know them, so its reading takes them from here.
    Rng garbageRng[2]{Rng(0), Rng(0)};
    // Params::x3TallPaths. 0: placeFast takes the frame of the lock from the Timing tables, whatever
    // the field. n: from the buttons of InputPlanner when a column of n pieces or more stands between
    // the column the pair appears in and its placement (both pieces), or next to them.
    int tallPaths = 0;
    // Params::handWait of the player who reads, for his own seat: player i leaves a pair alone this many
    // frames before its first button, so placeFast locks it that many frames later. 0 = the fastest input.
    // (On the safe side: the pair falls a little while it waits and a wait may end early, so the real lock is
    // never later than this and a few frames earlier at most. The rows of soft drop are counted as with the
    // fastest input.)
    int wait[2] = {0, 0};

    Duel() = default;
    // The observation must be of two players.
    explicit Duel(const Observation& o, uint64_t garbageSeed = 0) : rules(&o.rules), timing(&o.timing), frame(o.frame), state(o.state) {
        for (int i = 0; i < 2; ++i) {
            garbageRng[i] = Rng(garbageSeed, DUEL_GARBAGE_STREAM + uint64_t(i));
            const Player& s = o.player[i];
            DuelPlayer& p = player[i];
            static_cast<PlayerCore&>(p) = s;
            p.nextPair = s.nextPair;
            p.moves = s.moves;
            p.chain = s.chain;
            // A phase with `wait` frames left ends in the step of frame + wait - 1.
            const int64_t end = frame + s.wait - 1;
            switch (s.phase) {
            case Phase::Next: p.phase = DuelPhase::Spawn, p.at = end; break;
            case Phase::Control: p.phase = DuelPhase::Control; break;
            case Phase::Split: p.phase = DuelPhase::Check, p.at = end + timing->landFrames; break;
            case Phase::Land:
            case Phase::Wave: p.phase = DuelPhase::Check, p.at = end; break;
            case Phase::Decide: p.phase = DuelPhase::Settle, p.at = frame; break; // (not seen between frames of a Round)
            case Phase::Garbage: p.phase = DuelPhase::Spawn, p.at = end + timing->nextFrames; break;
            case Phase::Dead: p.phase = DuelPhase::Dead; break;
            }
        }
    }

    // Plays the frames before `until`, but stops after a frame in which a pair
    // appeared: returns the players (bit i) whose pair appeared in frame
    // `frame - 1` and who now need place(). Returns 0 when `until` is reached
    // or the round is over (see state).
    int run(int64_t until) {
        while (state == RoundState::Playing && frame < until) {
            int64_t next = until;
            for (const DuelPlayer& p : player)
                if (p.phase != DuelPhase::Control && p.phase != DuelPhase::Dead && p.at < next) next = p.at;
            if (next >= until) break;
            frame = next;
            int spawned = 0;
            for (int i = 0; i < 2; ++i) {
                DuelPlayer& p = player[i];
                if (p.at != frame) continue;
                if (p.phase == DuelPhase::Spawn) {
                    if (p.field.dead()) {
                        p.phase = DuelPhase::Dead;
                    } else {
                        ++p.nextPair;
                        p.spawnFrame = frame;
                        p.phase = DuelPhase::Control;
                        spawned |= 1 << i;
                    }
                } else if (p.phase == DuelPhase::Check) {
                    WaveReport w;
                    Incoming& opponent = player[1 - i].incoming;
                    if (chainWave(p, opponent, *rules, rules->rateAt(frame), p.chain, w)) {
                        p.at = frame + timing->waveFrames(w.longestFall);
                    } else {
                        chainEnd(p, opponent, p.chain);
                        p.phase = DuelPhase::Settle;
                    }
                }
            }
            for (int i = 0; i < 2; ++i) {
                DuelPlayer& p = player[i];
                if (p.phase != DuelPhase::Settle || p.at != frame) continue;
                int heights[W], placed[W];
                p.field.heights(heights);
                p.dropped = dropGarbage(p.field, p.incoming, *rules, garbageRng[i], placed);
                p.fallen += p.dropped;
                p.phase = DuelPhase::Spawn;
                p.at = frame + (p.dropped ? timing->garbageFrames(heights, placed) : 0) + timing->nextFrames;
            }
            ++frame;
            const bool dead0 = player[0].phase == DuelPhase::Dead, dead1 = player[1].phase == DuelPhase::Dead;
            if (dead0 || dead1) state = dead0 && dead1 ? RoundState::Draw : dead0 ? RoundState::Won1 : RoundState::Won0;
            if (spawned) return spawned;
        }
        if (state == RoundState::Playing && frame < until) frame = until;
        return 0;
    }

    // Player i (in Control) puts the pair with the fastest input. The pair must
    // have appeared in this Duel (spawnFrame known), and the way to the
    // placement must be the straight one the Timing tables describe.
    void placeFast(int i, Placement target, Pair pair) { put(i, target, pair, -1, 0, 0); }

    // The same with the frame of the lock and the rows of soft drop given (a
    // pair observed in mid-air, a path that is not the straight one, waiting).
    // The first wave must not lie before `frame`. rested: the frames the pair will have lain on the ground
    // at that lock; the landing waited for after the lock is shorter by them (0 = none, or not counted).
    void place(int i, Placement target, Pair pair, int64_t lockFrame, int softDropRows, int rested = 0) {
        put(i, target, pair, lockFrame, softDropRows, rested);
    }

private:
    void put(int i, Placement target, Pair pair, int64_t lockFrame, int softDropRows, int rested) {
        DuelPlayer& p = player[i];
        const bool fastest = lockFrame < 0; // (placeFast: the frame of the lock is worked out here)
        int buttonFrames = 0, buttonSoft = 0; // the fastest buttons, when the tables do not describe the way (tallPaths)
        if (lockFrame < 0 && tallPaths > 0) {
            int h[W], top = 0;
            p.field.heights(h);
            const int childX = target.childX();
            const int from = std::min(int(target.x), std::min(childX, SPAWN_X)), to = std::max(int(target.x), std::max(childX, SPAWN_X));
            for (int x = from > 0 ? from - 1 : 0; x <= to + 1 && x < W; ++x) top = h[x] > top ? h[x] : top;
            if (top >= tallPaths) {
                static thread_local InputPlanner planner;
                static thread_local InputPath path;
                if (planner.plan(p.field, spawnPair(*timing), target, *timing, rules->speedPercent, path)) {
                    buttonFrames = path.frames(), buttonSoft = path.softDropRows;
                    // (a pair that lay on the ground before its lock has that much less of the landing left: advancePlayer)
                    PairControl q = spawnPair(*timing);
                    for (const uint8_t b : path.buttons) stepPair(q, p.field, b, *timing, rules->speedPercent);
                    rested = int(q.groundFrames);
                }
            }
        }
        const PlaceResult r = juraic::place(p.field, target, pair);
        if (lockFrame < 0 && buttonFrames > 0) {
            lockFrame = p.spawnFrame + buttonFrames - 1; // (the first button moves the pair in the frame it appears)
            softDropRows = buttonSoft;
        } else if (lockFrame < 0) {
            lockFrame = p.spawnFrame + timing->placeFrames(target.x, r.lockY) - timing->landFrames - timing->nextFrames;
            softDropRows = SPAWN_Y - r.lockY + timing->dropLockPoints + timing->dropLandPoints; // a point for every row with the down key, and those of landing with it
        }
        if (fastest && wait[i] > 0) lockFrame += wait[i]; // (slow hands)
        if (rules->softDropBonus && softDropRows > 0) {
            p.scoring.score += softDropRows;
            p.scoring.pool += softDropRows;
        }
        ++p.moves;
        p.chain = {};
        p.lockFrame = lockFrame;
        p.phase = DuelPhase::Check;
        const int split = timing->splitFrames(r.split, target.x, r.lockY);
        int land = timing->landFrames;
        if (rested > 0 && !split) land = land - rested < timing->lockWait ? timing->lockWait : land - rested;
        p.at = lockFrame + split + land;
    }
};

} // namespace juraic::ai
