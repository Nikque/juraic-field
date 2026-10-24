#pragma once
// The mode in which Juraic plays 2P of the real game (play/realgame_win.h): the AI plays one side of the real game
// through the buttons of a pad (play/realgame_pad_win.h on Windows), frame after frame. Plain C++, tested on every system
// (tests/realgame_tests.cpp, against rounds of this program drawn, read and played through a pad that is late).
//
// The own pair is followed with the engine (game/player.h): from the frame its new pair is seen (Tracker), as the game
// moves it with the buttons sent, each taken by the game `lag` frames after it was sent. The real game shows the height
// of its pair in the frame it is in, its column from the frame after a move (the pair, between two columns, is not to be
// read in the frame it moves) and the way of its child some frames after a turn (MOVE_SHOWN, TURN_SHOWN). Every clear
// picture of the pair is checked against the engine so: when it shows the pair elsewhere (a button taken late or early,
// or not taken) the engine's pair is put where the picture shows it, the frames since are played again with the buttons
// sent for them, and the way to the placement chosen is planned again from there (ai::PlacementAgent::steer). Once the
// engine has put the pair, the down button is held until the screen shows it lying or the next pair is about to come
// (holdDown), and nothing else is pressed until the next pair is begun: as the screen shows it, as the engine foresees it
// when the screen is sure (beginEarly), or unseen when the window tells it has come (beginBlind); the AI is still shown
// every frame (an AI of a lower level sees the other side as it was some frames before: ai/pace.h).
//
// The lag and the way the two turns go are taken from the pictures: as a pair is over, the buttons that went to the pad
// for it are given to the engine from where the pair began, with each lag near the driver's and the two turns either way
// round; the one whose pair the pictures show where it is most often, over the last pairs, is the driver's from the next
// pair on (fit). So, with the pictures of the six records of the real game, 96 to 99% of the pictures of a record show
// the pair where the engine has it, each pair at its best lag nearly all of them. (A lag taken from the first picture
// that showed a move was a frame more than the game's; turns found the other way round from two such pictures in a row
// were never found so, each put right as it came, and the turns stayed the wrong way round through four records.)
#include "../ai/agent.h"
#include "realgame_follow.h"
#include <algorithm>
#include <array>
#include <deque>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace play::realgame {

class Driver {
public:
    int me = 1;                               // the seat played
    // Frames from a button sent to the frame of the game that takes it. The driver begins with it and takes the lag that
    // fits the pictures best (fit) from the next pair on, unless takeLag is false.
    int lag = 2;
    bool takeLag = true;
    juraic::Rules rules;                      // the real game's
    std::unique_ptr<juraic::ai::Agent> agent; // a PlacementAgent
    uint64_t seed = 1;                        // of the agent's rounds
    int played = 0;                           // pairs played
    int corrected = 0;                        // pictures that showed the pair elsewhere than the engine had it
    int explained = 0;                        // ... of the others, those a cell of the 13th or 14th row not sure of explained (explain)
    bool unsureWays = true; // the AI plans its ways with the cells not sure of (tellUnsure), and a picture can tell them (explain)
    std::vector<int> lags;                    // the lag the pictures of each pair fitted best, the last ones (at most 15)
    // The buttons of the two turns are sent the other way round: the game turns its pair so (fit).
    bool turnsSwapped = false;
    // What the real game shows of a move and of a turn of its pair, frames after it took them (its records: the column
    // of the frame before in a picture, the way of the child of two frames before, sometimes three).
    static constexpr int MOVE_SHOWN = 1, TURN_SHOWN = 2;
    // A pair begun unseen (beginBlind): this many frames after the engine foresaw it, and taken again from the screen
    // when the screen shows it more than BLIND_SLACK frames from then.
    static constexpr int BLIND_AFTER = 3, BLIND_SLACK = 8;
    // A turn taken in the frame of a move or just after another turn is shown later still: the way before it was seen 5
    // or 6 frames after it in a fifth of the pictures of those frames of the eighth record (8 frames twice). A picture of
    // the pair turned the way the engine had it up to TURN_LATEST frames before puts nothing right.
    static constexpr int TURN_LATEST = 6, TURN_RUN = 3;
    // A turn that changes the column of the pair in its frame (pushed aside from a wall or a higher column, a move with
    // it) is shown later than a move: the column before in the frame of the turn, the column between in the next, the
    // column after in the one after (on the real game, turned so in the 12th row, its child in the hidden 13th row).
    // A picture up to KICK_SHOWN frames after such a turn showing any column from the one before it to the one after
    // puts nothing right. (Put right for it on the real game, the pair was turned once more: 13 pairs in a record went
    // down with the child below or in another column than the AI chose, at the end of rounds with the field nearly full.)
    static constexpr int KICK_SHOWN = 2;
    // The fit: lags from FIT_REACH less to FIT_REACH more than the driver's are tried, a pair with fewer than FIT_SHOTS
    // clear pictures is not counted; the pictures of the last FIT_PAIRS pairs are added up, and a lag is taken when its
    // engine shows FIT_MARGIN pictures more right than the driver's. The way of the turns, which does not change, is told
    // by every pair since the driver began (the pair is not to be read while it turns, and is soon locked after: a picture
    // or two of a pair tell of it, its records): the pictures more right with the turns the other way round, and
    // PLACED_WEIGHT for each pair locked where only that way brings it; at TURN_MARGIN the turns go the other way round
    // (the records played again: at 2 the turns of one record went to and fro seven times, at 3 and 4 never).
    // A pair whose axis went up into the 13th row tells TURN_STEP at most either way (there a turn the other way round,
    // with a kick or a quick turn, can bring a pair to where the pictures show it: one such pair turned them the other
    // way round at the end of a round drawn with a pad a frame early or late now and then, 10 pictures of 15 and the
    // lock, and they stayed so). Capped for every pair, a pad that turns the other way round from the start was found a
    // pair later, and 2 pairs of 80 went elsewhere for it.
    static constexpr int FIT_REACH = 3, FIT_SHOTS = 4, FIT_PAIRS = 3, FIT_MARGIN = 4, PLACED_WEIGHT = 4, TURN_MARGIN = 3, TURN_STEP = 2;
    // When set: what the driver does, as lines with the frame of the clock they are of (the record of a reading,
    // play/realgame_log.h) - each pair begun, the placement it is brought to, a pair put where the picture shows it,
    // the lag the pictures of a pair fitted.
    std::vector<std::pair<int64_t, std::string>>* notes = nullptr;

    // Another AI to play from the next pair on (the level changed while the AI plays): it takes over when no pair is in
    // play, the one before let go on a thread of its own (it waits for a search it may have begun, as long as its
    // level lets it think: the pad is not to wait for that).
    void replaceAgent(std::unique_ptr<juraic::ai::Agent> given) {
        prepare(given.get());
        nextAgent = std::move(given);
    }

    // An AI that plays 2P of the real game: while its second choice is on its way, the pair goes straight down in every
    // frame in which that loses nothing (PlacementAgent::holdSecond, Params::thinkHold). The third pair is known only as
    // the pair appears (the window of the next pairs moves until then), so the second choice comes after the pair has
    // set out for the first (on the real game 26 pairs of 147 changed their placement 1 to 6 frames after they were seen).
    // Its taps are pressed two frames and let go two (PlacementAgent::tapHold): on a machine under load the real game's
    // frames take the buttons of the frame before or after now and then, and a tap of one frame is then lost or runs into
    // the next one (rounds drawn as the real game, a frame sooner or later in one frame of 8: 83.7% of the pairs where
    // the AI chose with taps of one frame, 97.7% with taps of two; 98.4% and 97.8% with no frame sooner or later).
    // No turn is pressed while a quick turn's lock lasts (PlacementAgent::waitQuickLock): the real game takes turns again
    // 4 to 7 frames after a quick turn, the engine 8 frames after (realGameTiming), and a turn pressed in between, one the
    // engine does not take, turned the pair once more on a round that takes turns again sooner.
    // No way is taken that a frame of the clock the driver is not called in leaves with no way to the placement, when the
    // way that holds the down button later has none such (PlacementAgent::spareFrame): the next call plans the way again
    // from where the pair is (steer), but not when that frame was the last before it would lock (in a record of the real
    // game, the driver not called in 14% of its frames: 3 pairs of 590 went down a column short so).
    // A pair begun before it appears (early) is chosen before the third pair shows in the own window: the second choice
    // is asked for in the air too, when the screen shows that pair after the pair has appeared (PlacementAgent::
    // askInAir; in rounds drawn of X3 against X3 the choices that knew two pairs only went from 25% to 0.6%).
    static void prepare(juraic::ai::Agent* agent) {
        if (auto* placing = dynamic_cast<juraic::ai::PlacementAgent*>(agent))
            placing->holdSecond = true, placing->tapHold = 2, placing->waitQuickLock = true, placing->spareFrame = true, placing->askInAir = true;
    }

    // The next pair is begun before the screen shows it, when the engine foresees the frame it appears in (beginEarly):
    // its first button reaches the game in that frame. (Begun as the screen showed it, every pair took its first button
    // 4 frames after it appeared, the pad 5 frames late, and handled so a pair took 18.9 frames to 14.7 played directly:
    // in rounds drawn of X3 against X3, 2P won 109 of 360 so, 116 begun early.) Taken again from the screen when it
    // shows the pair appear in another frame.
    bool early = true;
    // The number of the pair last begun (as Tracker counts the pairs of a round: the first 1), in play or put.
    int begunPair() const { return pairsSeen; }

    // The middle of the lags the pictures of the last pairs fitted best; -1: none yet.
    int measuredLag() const {
        if (lags.empty()) return -1;
        std::vector<int> sorted = lags;
        std::sort(sorted.begin(), sorted.end());
        return sorted[sorted.size() / 2];
    }

    // Nothing is pressed from the next frame on until a new own pair is seen (the buttons of this frame are sent: the
    // pair locks with them).
    void stop() { active = false; }
    bool playing() const { return active; }
    // The field a pair is begun with: `followed` (the twelve rows the screen shows; the 13th and 14th, which it does not,
    // as Follower made them out) with the 13th and 14th rows of `engine` (the field the engine left: it put the pairs,
    // where the screen cannot tell one piece in the 13th row of one column from one in another, nor see a pair put whole
    // into those rows) on every column whose twelve rows are the same in both.
    // Not garbage in the 13th row: the engine knows of none, and garbage there is what Follower guessed when a pair was
    // begun (Follower::withGarbage) - taken back so after Follower had found it was not there, a pair was planned over it
    // and went down a column short, in rounds drawn with garbage.
    static juraic::GameField hiddenFromEngine(const juraic::GameField& followed, const juraic::GameField& engine) {
        juraic::GameField g = followed;
        for (int x = 0; x < juraic::W; ++x) {
            bool same = true;
            for (int y = 0; y < 12 && same; ++y) same = followed.f.get(x, y) == engine.f.get(x, y);
            if (!same) continue;
            if (engine.f.get(x, 12) != juraic::GARBAGE) g.f.set(x, 12, engine.f.get(x, 12));
            g.row14[size_t(x)] = engine.row14[size_t(x)];
        }
        return g;
    }

    // The columns whose 13th or 14th row the pair in play was begun with is not sure of (shareHidden), and the field it
    // was begun with.
    uint8_t unsureColumns() const { return uint8_t(unsure13 | unsure14); }
    const juraic::GameField& begunField() const { return startOwn.field; }

    // The placement the pair in play is brought to (-1: none); once the engine has put it, the one it was brought to then,
    // until the next pair is begun. (Told only while it was in play, a placement the AI chose again in the call in which
    // the engine put the pair was never told: tools counted the pair against the placement before.)
    int target() const {
        const auto* placing = placer();
        return active && placing ? placing->targetPlacement() : putAim;
    }

    // In each frame of the clock `now`, after the readings of the screen up to it are in `f`: the buttons to hold on
    // the pad from now on (the game takes them in its frame now + lag). `f` is told where the engine locked the pairs
    // (Follower::tell).
    uint8_t tick(Follower& f, int64_t now) {
        const uint8_t out = drive(f, now);
        if (lockedPair >= 0) f.tell(me, lockedPair, lockedPlacement);
        return out;
    }

private:
    uint8_t drive(const Follower& f, int64_t now) {
        const Tracker& t = f.tracker;
        if (nextAgent && !active) {
            if (agent) std::thread([before = std::move(agent)]() mutable { before.reset(); }).detach();
            agent = std::move(nextAgent);
            agent->startRound(me, seed++);
            noted = -2;
        }
        if (!agent) return held = 0;
        if (t.rounds != round) beginRound(t);
        undoBegunAgain(f);
        takePair(f);
        keepShot(f);
        watchFitted(f);
        watchWindow(t);
        watchOther(t, now);
        if (stoppedFor == pairsSeen && otherMovedAt == now) stoppedFor = -1; // (the game goes on: the pair begun again is played on)
        if (!t.side[me].clear) unclearAt = now;
        if (!active) {
            wait(f, now);
            if (blindDue(f, now)) beginBlind(f, now);
            else if (earlyDue(f, now)) beginEarly(f, now);
            else if (stoppedFor == pairsSeen && following && unshown() && windowWaits(t.side[me])) beginAgain(f, now);
            else if (stoppedFor != pairsSeen && stopped(f, now)) {
                stoppedFor = pairsSeen;
                note(t.frame, "2P pair " + std::to_string(pairsSeen) + " not shown, the game stopped: the window and the other side stand still");
                beginAgain(f, now);
            }
        }
        if (!active && overdue(f)) resume(f, now);
        if (!active) return holdDown(f, now);
        correct(f);
        const int64_t at = now + lag;
        // (frames of the game whose buttons were sent before: they took what was held. A frame the clock went past without
        //  a call - the thread of the play a frame late - had the agent's buttons chosen for it and not sent: the way to
        //  the placement is planned again from where the engine has the pair, so that a move chosen for it is not lost)
        bool same = true;
        while (active && next < at) same = play(f, false) && same;
        if (!same && active) steer(f);
        if (active && next == at) play(f, true);
        noteAim(now);
        return press(now, held);
    }

    // (Tracker counts the pairs of a round from 0 only as a new round begins.)
    void beginRound(const Tracker& t) {
        following = untilLocked = false;
        lockedPair = -1;
        liftedFor = shownAt = doubtAt = -1;
        windowReadAt = windowPauseAt = -1;
        own = juraic::Player{};
        past.clear();
        round = t.rounds, pairsSeen = 0, blindFor = -1, earlyFor = -1, againFor = -1, stoppedFor = -1, shiftBegun = -1, spawnTaken = -1, putAim = -1, unsure13 = unsure14 = 0;
        found.clear();
        agent->startRound(me, seed++);
        stop();
        shots.clear(), fitted.reset(); // (a pair of the round before is not fitted: its pictures may be cut short)
    }
    // A pair seen, or one the window tells has come (the pair begun unseen let go): begun.
    void takePair(const Follower& f) {
        const Tracker& t = f.tracker;
        const SideTrack& side = t.side[me];
        // (a pair begun unseen is numbered one past those Tracker has counted, and stays so when the window moving on -
        //  not its count - tells it has been put)
        const int counted = side.pairs + side.unseen; // (with the pairs the window tells were put unseen)
        // (a new pair seen since the last call - not Tracker::spawned, which tells of the last reading only: the thread of
        //  the play on Windows reads the pictures that came and then drives once: in rounds drawn so, with pairs that came
        //  later than foreseen, pairs were begun from the screen 145 times read so and 165 times read one by one)
        const bool spawned = side.spawnedAt != spawnTaken;
        spawnTaken = side.spawnedAt;
        // (a pair begun early: the window moving on as it appears tells no other pair has come; the screen showing it
        //  appear in another frame than the engine foresaw - garbage it knows nothing of fell first -, it is taken from it)
        if (earlyFor >= 0 && side.shiftedAt > shiftBegun && !earlyShifted) shiftBegun = side.shiftedAt, earlyShifted = true;
        if (earlyFor >= 0 && side.pairs == earlyFor && spawned) {
            const bool off = side.spawnedAt != earlyAt;
            earlyFor = -1, blindFor = -1;
            if (off) shots.clear(), begin(f, side);
        } else if (side.pairs > pairsSeen) {
            pairsSeen = side.pairs, blindFor = -1, earlyFor = -1;
            begin(f, side);
        } else if (blindFor >= 0 && side.pairs == blindFor && spawned) {
            // (the screen shows the pair begun unseen: taken from it if it came at another time than foreseen, or the
            //  engine has put it already)
            blindFor = -1;
            if (!active || std::abs(side.spawnedAt - blindAt) > BLIND_SLACK) begin(f, side);
        } else if (blindFor >= 0 && ((side.unseen > 0 && counted >= blindFor) || side.shiftedAt > shiftBegun)) {
            // (the window tells the pair begun unseen has been put, the screen never showing it: the next one may be
            //  begun unseen too - kept waiting for the screen to show this one, the next pair under the same light, 80
            //  frames long, was left alone 11 times in a round of 34 pairs drawn so. The window moving on again tells
            //  so too, when what it was counted by was one ahead.)
            blindFor = -1;
        }
    }
    // The window of the next pairs as it last stood still, and the readings in a row since that showed it otherwise.
    void watchWindow(const Tracker& t) {
        const SideTrack& side = t.side[me];
        if (side.nextRead[0] && side.nextRead[1] && side.nextStill >= t.windowStill) windowReadAt = t.frame, standing[0] = side.next[0], standing[1] = side.next[1];
        if (t.frame != oddAt) {
            // (readings in a row of the window otherwise than it last stood, or not read whole)
            const auto same = [](juraic::Pair a, juraic::Pair b) { return a.axis == b.axis && a.child == b.child; };
            const bool asStood = side.nextRead[0] && side.nextRead[1] && same(side.next[0], standing[0]) && same(side.next[1], standing[1]);
            oddAt = t.frame, windowOddFor = asStood ? 0 : windowOddFor + 1;
        }
    }
    // No pair in play in the engine: the down button for the pair it has put, until the screen shows it lying (or the
    // window tells the next pair is coming).
    uint8_t holdDown(const Follower& f, int64_t now) {
        const Tracker& t = f.tracker;
        const SideTrack& side = t.side[me];
        // (whether or not the screen's pair is followed: a pair not found - turned otherwise than the engine has it, a
        //  field read wrong - is still in the air, and let go it fell by itself for 45 frames on the real game. So
        //  too a pair begun unseen, which the screen has not shown: the engine, which knows of no garbage, may have
        //  put it on a column the game's garbage made higher, the game's pair still in the air - let go, it fell by
        //  itself once in 31 pairs of rounds drawn so. Not followed on the screen, it is held until a clear picture
        //  shows nothing above the pieces at rest: held on to the engine's next pair, a few frames late, the button
        //  reached the next pair as it appeared, 11 times in 44 pairs. So too a pair begun early the screen has not shown:
        //  the game may have stopped before it appeared, the engine going on)
        const bool unseenInAir = unshown() && !(side.clear && side.loose == 0);
        // (the game stopped: it takes no button, and the down button held on went on to the pair as it appeared)
        if (stoppedFor == pairsSeen) return press(now, held = tapToEnd(now, 0));
        // A pair read as lying can lift again while earlier buttons arrive. Resume its down button when a clear
        // picture shows it in the air; one such pair was left for 10 frames after a light of 80 frames.
        const bool heldBefore = untilLocked;
        if (shownInAir(f)) untilLocked = true;
        if (!heldBefore && untilLocked) note(t.frame, "2P pair " + std::to_string(pairsSeen) + " held down again: the screen shows it in the air");
        // So too, the window not moved on since the pair appeared, a pair Follower no longer finds lying, or takes as
        // put with no chain and nothing of it at rest: a clear picture that shows pieces of a colour above the pieces
        // at rest shows it in the air still (read lying, lifted by a turn on its way, let go by Tracker on a picture
        // of the turn: on the real game it fell by itself for 35 frames; turned out of the 13th row over a lower
        // column, another lay 28 frames before it locked). Held down until Follower finds it lying - put, until a
        // clear picture shows nothing of a colour above the pieces at rest. Not once pieces of the field it appeared
        // over have gone, nor for pieces in the air with a pair put at rest: a chain Follower did not reckon (held
        // so, the button went on to the next pair, 5 times in the records).
        const SideModel& m = f.model[me];
        if (following && side.pairs == pairsSeen && side.clear && side.shiftedAt <= side.spawnedAt + t.pairGap && contains(side.resting, side.under)) {
            const bool put = m.followed == pairsSeen - 1;
            const bool air = colouredAbove(t.last.side[me], side.resting) > 0;
            if (air && (put ? m.chains == 0 && side.resting == side.under : !m.lying)) {
                if (!untilLocked) note(t.frame, "2P pair " + std::to_string(pairsSeen) + " held down again: pieces of a colour in the air after it was taken as " + (put ? "put" : "lying"));
                untilLocked = true, liftedFor = pairsSeen;
            }
            else if (!air && put && liftedFor == pairsSeen) untilLocked = false, liftedFor = -1;
        }
        if (untilLocked && (side.pairs == pairsSeen || unseenInAir) && !lies(f)) {
            // The window starts moving before the next pair appears. Release while it moves: waiting for it to
            // stand still sent the old down button to 26 next pairs in 16 rounds with lights of 30 to 120 frames.
            // An unread window alone cannot prove a lock. Keep untilLocked, and resume after nextSlide frames if
            // neither the field nor the window has confirmed it (a window hidden for longer must not leave a pair).
            // (as the real game draws it, the window moving shows one of its pairs read otherwise and the other not
            //  read, never both unread: 9 frames before a pair, the down button held on went on to that pair)
            const bool moving = (!side.nextRead[0] && !side.nextRead[1]) || windowOddFor >= 2;
            if (moving && windowReadAt > startNext && windowReadAt > windowPauseAt) windowPauseAt = t.frame;
            if (windowPauseAt >= startNext && now < windowPauseAt + t.timing.nextSlide) return press(now, held = tapToEnd(now, 0));
            // (nor from the button that would reach the next pair, once the window has moved on since the pair in play was
            //  begun and tells the frame the next one appears in: the pair is put, the window moves on only after that -
            //  held again after the window moved, the engine running a chain the game did not have, the down button went on
            //  to the next pair in a round drawn with words over the field. Not for a move of the window before the pair
            //  was begun: for a pair begun unseen that move was its own coming, and let go so it fell by itself.)
            const int64_t comes = appeared(t);
            if (comes >= 0 && side.shiftedAt > shiftBegun && now + lag >= comes) return press(now, held = tapToEnd(now, 0));
            return press(now, held = tapToEnd(now, juraic::BTN_DOWN));
        }
        untilLocked = false;
        return press(now, held = tapToEnd(now, 0));
    }
    struct Past {
        int64_t frame;        // the state after this frame of the game,
        juraic::Player state; // ... and the buttons it took
        uint8_t buttons;
    };
    struct Shot {
        int64_t frame;           // a clear picture of the pair in play
        juraic::PairControl at;  // ... where it shows it
    };
    juraic::Player own;
    juraic::Tsumo tsumo;
    int64_t next = 0; // the frame of the game the engine plays next
    // shareHidden: the columns whose 13th and whose 14th row the pair in play was begun with is not sure of, and the
    // twelve rows it was begun with
    uint8_t unsure13 = 0, unsure14 = 0;
    juraic::GameField unsureRows;
    // A cell of the 13th or 14th row a picture told (explain), with the twelve rows of its column then.
    struct Found {
        int x;
        bool row14;
        juraic::Cell cell;
        uint16_t rows;
    };
    std::vector<Found> found;
    std::deque<Past> past;
    bool active = false;
    uint8_t held = 0;
    int round = -1, pairsSeen = 0;
    int begunRound = -1; // the round of the last pair begun (the engine's field is of it)
    int blindFor = -1;      // the pair begun unseen, until the screen shows it (-1: none)
    int64_t blindAt = -1;   // ... the frame the engine had it appear
    int earlyFor = -1;          // the pair begun early, until the screen shows it (-1: none)
    int64_t earlyAt = -1;       // ... the frame the engine had it appear
    bool earlyShifted = false;  // ... the window has moved on since it was begun
    int stoppedFor = -1;        // the pair not shown when the game stopped (stopped), until the game goes on (-1: none)
    int againFor = -1;          // the pair begun again (beginAgain), until the screen shows it (-1: none)
    int64_t otherMovedAt = -1;  // the last frame of the clock the other side was read otherwise than before, or not clear
    juraic::GameField otherResting; // ... and as it was read then: the pieces at rest,
    bool otherInHand = false;       // ... a pair in hand,
    juraic::PairControl otherAt{};  // ... and where
    juraic::Player putOwn;      // ... the engine as it had put it,
    int64_t putNext = 0;        // ... and the frame of the game it played next then
    juraic::Placement putPlacement{}; // ... and where it had put it (told to Follower)
    int64_t unclearAt = -1000000; // the last frame of the clock the own side was read unclear (earlyDue)
    int64_t shiftBegun = -1; // the window's last move on (SideTrack::shiftedAt) as the pair in play was begun
    int64_t spawnTaken = -1; // SideTrack::spawnedAt as the last call found it (takePair)
    int64_t checked = -1;
    int64_t shownAt = -1;       // the last clear picture of the pair in play that showed it where the engine had it (correct)
    int64_t doubtAt = -1;       // the last clear picture of the pair in play showed it otherwise than the engine (correct)
    juraic::PairControl doubt{}; // ... where
    int noted = -2; // the placement last noted (notes)
    int64_t steeredSeen = 0, againSeen = 0; // the agent's counts of the nearest placements taken and the second choices taken, as last noted
    // After its pair is locked the engine goes on without buttons (its chain, then the wait for the next pair), so that
    // the AI is shown when the next pair comes and chooses before it appears, as in a match of this program - not as it
    // appears, the pair waiting for the choice (long on a machine under load). It stops two frames before the pair it
    // foresees (the pair may come later: garbage the engine does not know of falls first).
    bool following = false;
    // The engine has the pair locked: until the screen shows it lying where it locks (or the engine foresees the next
    // pair as the button would come) the down button is held. The engine ahead of the game, or its pictures not to be
    // read just then, the pair let go of it fell on slowly by itself (seen on the real game: 100 frames for one pair).
    bool untilLocked = false;
    int64_t lockedAt = -1; // the frame of the clock the engine locked the pair in (lies)
    int putAim = -1;       // ... the placement it was brought to then (target)
    bool sentFor = false;   // a frame of buttons has been sent for the pair in play (play)
    int lockedPair = -1;                 // the number (from 0) of the pair the engine locked last in the round (-1: none),
    juraic::Placement lockedPlacement{}; // ... and where (told to Follower: tick)
    int64_t airSeenAt = -1; // overdue: the last clear picture that showed the pair put in the air,
    int airHeight = 0;      // ... the half cells of its axis then
    int liftedFor = -1; // the pair held down again as a clear picture showed pieces of a colour in the air after it was put
    int64_t windowReadAt = -1, windowPauseAt = -1; // last stable window, and the bounded release when it became unread
    juraic::Pair standing[2]{};                     // ... the window as it stood then
    int64_t oddAt = -1;                             // the reading windowOddFor last counted
    int windowOddFor = 0;                           // readings in a row of the window otherwise than it stood (or not read whole)
    std::unique_ptr<juraic::ai::Agent> nextAgent; // (replaceAgent)
    juraic::Rng noGarbage{1, 1}; // (settlePlayer lets garbage fall by it: there is none in the engine)

    // For the fit: the engine as the pair was begun and the frame it began with, the clear pictures of the pair, the
    // buttons that went to the pad by the frame of the clock they were sent in (the last of them), the pictures each
    // engine tried showed right for each of the last pairs.
    juraic::Player startOwn;
    int64_t startNext = 0;
    std::vector<Shot> shots;
    std::deque<std::pair<int64_t, uint8_t>> pads;
    using FitTable = std::array<std::array<int, 2>, 2 * FIT_REACH + 1>; // [lag - driver's + FIT_REACH][turns the other way]
    std::deque<FitTable> fits;
    int turnsOther = 0; // what the pairs since the driver began tell for the turns the other way round (fit)

    // The agent as one that brings its pairs to placements (null for another).
    juraic::ai::PlacementAgent* placer() { return dynamic_cast<juraic::ai::PlacementAgent*>(agent.get()); }
    const juraic::ai::PlacementAgent* placer() const { return dynamic_cast<const juraic::ai::PlacementAgent*>(agent.get()); }
    static constexpr uint8_t TAP_KINDS[2] = {juraic::BTN_CW | juraic::BTN_CCW, juraic::BTN_LEFT | juraic::BTN_RIGHT}; // (turns, moves)
    static constexpr const char* WAYS[4] = {"up", "right", "down", "left"}; // (of the child)

    static uint8_t swapTurns(uint8_t b) {
        return uint8_t((b & ~(juraic::BTN_CW | juraic::BTN_CCW)) | (b & juraic::BTN_CW ? juraic::BTN_CCW : 0) | (b & juraic::BTN_CCW ? juraic::BTN_CW : 0));
    }
    // The buttons held as they go to the pad (the two turns the other way round when the game turns so), kept for the fit.
    uint8_t press(int64_t now, uint8_t b) {
        const uint8_t out = turnsSwapped ? swapTurns(b) : b;
        if (!pads.empty() && pads.back().first >= now) pads.back() = {now, out};
        else pads.emplace_back(now, out);
        while (pads.size() > 600) pads.pop_front();
        return out;
    }
    // A turn or a move held in the frame before, for fewer frames than a tap is held
    // (PlacementAgent::tapHold), is held on to its end. (The last tap of a way, in the frame the engine locks the pair, went
    // out for one frame: lost by a frame of the game under load, the pair locked turned otherwise or a column short.) So
    // too one given earlier in this frame (the driver called twice in a frame: taken back by the second call, the last
    // turn of a way, given as the engine locked the pair, lasted part of a frame, and the real game did not take it).
    uint8_t tapToEnd(int64_t now, uint8_t b) const {
        const auto* placing = placer();
        const int hold = placing ? placing->tapHold : 1;
        for (int k = 0; k < 2 && hold > 1; ++k) {
            const uint8_t given = !pads.empty() && pads.back().first == now ? pads.back().second & TAP_KINDS[k] : 0;
            if (given && !(b & TAP_KINDS[k])) {
                b |= uint8_t((turnsSwapped ? swapTurns(given) : given) & TAP_KINDS[k]);
                continue;
            }
            const uint8_t last = padAt(now - 1) & TAP_KINDS[k];
            if (!last || (b & TAP_KINDS[k])) continue;
            int run = 0;
            for (int64_t q = now - 1; run < hold && (padAt(q) & TAP_KINDS[k]) == last; --q) ++run;
            if (run < hold) b |= uint8_t((turnsSwapped ? swapTurns(last) : last) & TAP_KINDS[k]);
        }
        return b;
    }
    // The buttons the pad held in the frame `at` of the clock (0 before any). (pads: by the frame, one after the other)
    uint8_t padAt(int64_t at) const {
        const auto after = std::upper_bound(pads.begin(), pads.end(), at, [](int64_t frame, const std::pair<int64_t, uint8_t>& p) { return frame < p.first; });
        return after == pads.begin() ? 0 : std::prev(after)->second;
    }
    // ... as the agent gave them (the two turns as it names them).
    uint8_t givenAt(int64_t at) const { return turnsSwapped ? swapTurns(padAt(at)) : padAt(at); }
    // Where the state after the frame `frame` of the game is in past (past.size(): not there). (The frames of past follow
    // one another: the place is told by the first of them.)
    size_t pastIndex(int64_t frame) const { return indexIn(past, frame); }
    static size_t indexIn(const std::deque<Past>& in, int64_t frame) {
        if (in.empty() || frame < in.front().frame || frame > in.back().frame) return in.size();
        const size_t i = size_t(frame - in.front().frame);
        if (in[i].frame == frame) return i;
        for (size_t k = 0; k < in.size(); ++k)
            if (in[k].frame == frame) return k;
        return in.size();
    }

    void note(int64_t at, const std::string& line) {
        if (notes) notes->emplace_back(at, line);
    }
    // The placement the pair in play is brought to, noted when it changed - also when the engine locked the pair in the
    // frame it changed (noted before the engine went on, 2 pairs of a record had none noted as they went down) - with why:
    // the AI's second choice, or the nearest one when no way led to the one before any more.
    void noteAim(int64_t now) {
        const auto* placing = placer();
        if (!notes || !placing) return;
        const int aim = placing->targetPlacement();
        if (aim != noted) {
            std::string why;
            if (noted >= 0 && aim >= 0)
                why = placing->steered != steeredSeen ? " (no way to the one before: the nearest)" : placing->againSwitched != againSeen ? " (the second choice)" : "";
            noted = aim, note(now, "2P pair " + std::to_string(pairsSeen) + ": brought to " + placementText(aim) + why);
        }
        steeredSeen = placing->steered, againSeen = placing->againSwitched;
    }
    // Two places of a pair of colours `c` cover the same cells (a pair of one colour either way round).
    static bool samePlace(const juraic::PairControl& a, const juraic::PairControl& b, juraic::Pair c) {
        int ax, ah, cx, ch, sx, sh, tx, th;
        pairHalves(a, ax, ah, cx, ch);
        pairHalves(b, sx, sh, tx, th);
        return (ax == sx && ah == sh && cx == tx && ch == th) || (c.axis == c.child && ax == tx && ah == th && cx == sx && ch == sh);
    }
    static std::string placementText(int id) {
        if (id < 0) return "nothing";
        const juraic::Placement p = juraic::placement(id);
        return "column " + std::to_string(p.x + 1) + ", child " + WAYS[p.dir];
    }
    static std::string controlText(const juraic::PairControl& p) {
        return "column " + std::to_string(p.x + 1) + " row " + std::to_string(p.y + 1) + (p.upper ? " (half up)" : "") + " child " + WAYS[p.dir];
    }

    // A pair begun unseen goes down by itself until it is turned and moved where it goes (PlacementAgent::
    // downWhenPlaced): the engine knows of no garbage that fell while the screen could not be read, and the game's
    // pair, on a field higher than the engine's, lay down before its last move (in rounds drawn with the screen lit as
    // one pair in three appears, the pairs elsewhere than the AI chose were all begun unseen, garbage under most of them).
    void carefulDown(bool on) {
        if (auto* placing = placer()) placing->downWhenPlaced = on;
    }
    // The engine's pairs: those either side has shown (one not known: of the first colour).
    void learnPairs(const Tracker& t) {
        tsumo = juraic::Tsumo{};
        for (size_t i = 0; i < t.pairsLearnt(); ++i) {
            const juraic::Pair p = t.knows(me, int(i)) ? t.pairOf(me, int(i)) : juraic::Pair{1, 1};
            tsumo.piece[(i % juraic::TSUMO_PAIRS) * 2] = p.axis, tsumo.piece[(i % juraic::TSUMO_PAIRS) * 2 + 1] = p.child;
        }
    }

    // The engine begins pair `pair` with the 13th and 14th rows the AI is shown (Follower::withHidden: those Follower
    // made out, and the engine's own in the 13th row where Follower has none there), not those the engine made by
    // putting the pairs, which knows of no garbage: in rounds drawn as the real game, of the pieces in those rows one of
    // them had where the other had none, the engine was right in 49 of 233, Follower in 184. The cells where they
    // differ, those of columns Follower guessed garbage in (SideModel::unsure) and of `guessed` are not sure (unsure13,
    // unsure14), and stay so while their columns' twelve rows stay as they were (begun with the same rows, the engine no
    // longer differs there) - but for those a picture told since (found). The AI plans its ways with them so
    // (PlacementAgent::unsure13).
    void shareHidden(const Follower& f, int pair, uint8_t guessed) {
        const SideModel& m = f.model[me];
        const juraic::GameField rows = Follower::shown(own.field);
        const uint8_t high = Follower::twelveHigh(rows);
        uint8_t kept = 0;
        for (int x = 0; x < juraic::W; ++x)
            if (rows.f.occupied().column(x) == unsureRows.f.occupied().column(x)) kept |= uint8_t(1u << x);
        unsure13 = uint8_t(guessed | (unsure13 & kept & high)), unsure14 &= uint8_t(kept & high);
        unsureRows = rows;
        if (m.known) {
            const juraic::GameField& made = m.core.field;
            const uint8_t both = uint8_t(high & Follower::twelveHigh(Follower::shown(made)));
            for (int x = 0; x < juraic::W; ++x) {
                if (!(both >> x & 1)) continue;
                if (own.field.f.get(x, 12) != made.f.get(x, 12)) unsure13 |= uint8_t(1u << x);
                if (own.field.row14[size_t(x)] != made.row14[size_t(x)]) unsure14 |= uint8_t(1u << x);
            }
            unsure13 |= uint8_t(m.unsure & high);
            own.field = Follower::withHidden(own.field, made);
        }
        std::erase_if(found, [&](const Found& c) { return rows.f.occupied().column(c.x) != c.rows; });
        for (const Found& c : found) (c.row14 ? unsure14 : unsure13) &= uint8_t(~(1u << c.x));
        withFound(own.field);
        tellUnsure();
        if (!unsureColumns()) return;
        std::string columns;
        for (int x = 0; x < juraic::W; ++x)
            if (unsureColumns() >> x & 1) columns += " " + std::to_string(x + 1);
        note(f.tracker.frame, "2P pair " + std::to_string(pair) + ": the 13th and 14th rows not sure over columns" + columns);
    }
    // The cells pictures told (found) in `g`, where their columns' twelve rows are as they were then.
    void withFound(juraic::GameField& g) const {
        const juraic::GameField rows = Follower::shown(g);
        for (const Found& c : found) {
            if (rows.f.occupied().column(c.x) != c.rows) continue;
            if (c.row14) g.row14[size_t(c.x)] = c.cell;
            else g.f.set(c.x, 12, c.cell);
        }
    }
    // What the AI is shown with the own side `p` at the frame `at` (Follower::observeAt), with the cells pictures told.
    void observe(const Follower& f, const juraic::Player& p, int64_t at, juraic::ai::Observation& o) const {
        f.observeAt(me, rules, p, at, o);
        withFound(o.player[me].field);
    }
    // The cells not sure of, for the ways of the AI (PlacementAgent::unsure13, seenAfter: a way planned again after a
    // picture showed the pair otherwise begins lag + SEEN_AFTER frames after the frame shown - the next picture, and the
    // frame the way is planned in).
    static constexpr int SEEN_AFTER = 3;
    void tellUnsure() {
        if (auto* placing = placer())
            placing->unsure13 = unsureWays ? unsure13 : 0, placing->unsure14 = unsureWays ? unsure14 : 0, placing->seenAfter = lag + SEEN_AFTER;
    }
    // A new own pair was seen: the engine takes the side as it was then (the pair about to appear), with the pairs
    // the screen has shown.
    void begin(const Follower& f, const SideTrack& side) {
        const Tracker& t = f.tracker;
        fit(f); // (the pair before, whose pictures are all in)
        const juraic::GameField engine = own.field; // (as the engine left it after the pair before)
        const bool sameRound = begunRound == t.rounds;
        begunRound = t.rounds;
        own = juraic::Player{};
        own.field = f.model[me].known ? f.model[me].core.field : side.under; // (with the 13th and 14th rows the moves followed put there)
        if (sameRound) own.field = hiddenFromEngine(own.field, engine);
        shareHidden(f, side.pairs, 0);
        own.phase = juraic::Phase::Next;
        own.wait = 1;
        own.nextPair = own.moves = side.pairs - 1;
        if (f.model[me].known) own.scoring = f.model[me].core.scoring;
        learnPairs(t);
        carefulDown(false);
        // (seen late, it was in play already in the frame of the picture: it is taken as appearing in that frame)
        next = side.spawnedAt + (side.late ? 0 : 1);
        // (the frames from the picture to now have passed: at least the frames the picture is behind)
        start(f, t.frame + lag, "2P pair " + std::to_string(side.pairs) + " begun" + (side.late ? " (seen late)" : "") + ": seen in frame " +
                                    std::to_string(side.spawnedAt) + ", the engine from frame " + std::to_string(next) + ", lag " + std::to_string(lag));
        begunAt = active ? t.frame : -1;
    }
    // The frame of the picture the pair in play was begun from, as the screen showed it (-1: not begun so). That picture
    // shows it where the engine has it: one picture after it that shows the pair in the 13th row puts nothing right until
    // a second one shows it alike (correct). (Put right for one picture, pairs read upside down in the 13th row a column
    // aside went down there, 3 of the pairs elsewhere than the AI chose in 1440 rounds drawn as the real game. Not for a
    // picture lower: under garbage the engine has the pair a row off at times, and waited for, a pair was left alone.)
    int64_t begunAt = -1;

    // The frame the next pair appeared in (as the screen would show it first), when the window of the next pairs has
    // moved on since the last pair appeared (it moves on only after that pair is put): as the engine foresees it, waiting
    // for it, or else the frame after the window stood still moved on (on the real game it stands still a frame before
    // the pair shows - the engine may still run a chain the game did not have). Never before that frame: garbage the
    // engine knows nothing of falls first at times, and the pair comes later than the engine foresees it (begun at the
    // frame foreseen, the engine ran the pair down by itself to where it locks, and nothing was pressed while the
    // game's pair was in the air: in rounds drawn so, pairs left alone 9 times in 32, 1 in 31 after). Nor more than a
    // frame after it: the engine, its field made by pairs begun unseen, may wait longer than the game (5 frames late
    // after three pairs unseen in a row, and the pair was left alone for more than 10 frames, in a round drawn with a
    // light of 120 frames). -1: no pair has come.
    // The first pair of a round, followed from its beginning, appears in the frame the round's first pairs appear in on
    // both sides (Tracker::roundStart: the first pair of either side seen), or else the frame after the window stood still
    // moved on. (Begun only once the screen showed it, a first pair under a light of 80 frames fell by itself for 76 frames
    // in a round drawn so.)
    int64_t appeared(const Tracker& t) const {
        const SideTrack& side = t.side[me];
        if (pairsSeen == 0) {
            if (!t.fromStart || side.pairs > 0 || side.unseen > 0) return -1;
            return t.roundStart >= 0 ? t.roundStart + t.timing.firstSpawn : side.shiftedAt >= 0 ? side.shiftedAt + 1 : -1;
        }
        if (!following || own.phase == juraic::Phase::Control || side.shiftedAt <= side.spawnedAt + t.pairGap) return -1;
        const int64_t shown = side.shiftedAt + 1;
        return own.phase == juraic::Phase::Next && own.wait <= 2 ? std::clamp(next + own.wait - 2, shown, shown + 1) : shown;
    }
    // The next pair has come and the screen does not show it: BLIND_AFTER frames after it appeared, the screen of the own
    // side not read (a light over it). On the real game the screen of 2P read nothing for 30 frames as a pair appeared,
    // and nothing played the pair until the screen showed it. So too with the side read clear: words drawn over the field
    // read as pieces above them, and the pair is not found under them. On the real game the words of an all clear stood
    // over the field of 2P for more than 900 frames, read clear, and every pair that came was left to fall by itself.
    // A pair has come when the window has moved on since the pair in play was begun. (Counted by the pairs the window
    // tells were put unseen, a window of two pairs alike read otherwise for three pictures put that count one ahead, and
    // the window moving on after it was taken for no pair: no pair was begun to the end of the round.)
    bool blindDue(const Follower& f, int64_t now) const {
        const Tracker& t = f.tracker;
        const SideTrack& side = t.side[me];
        const int64_t appears = appeared(t);
        return blindFor < 0 && (pairsSeen == 0 || side.shiftedAt > shiftBegun) && appears >= 0 && now >= appears + BLIND_AFTER;
    }
    // The next pair, begun unseen, of the colours learnt: the engine goes on from where it stopped (a chain it still ran
    // run to its end at once), the pair appearing in the frame it came; the screen puts it right when it shows it.
    void beginBlind(const Follower& f, int64_t now) {
        const Tracker& t = f.tracker;
        const int64_t appears = appeared(t);
        // (as wait: the field dead only as the pair would appear)
        for (int k = 0; k < 600 && own.phase != juraic::Phase::Next && own.phase != juraic::Phase::Control && own.phase != juraic::Phase::Dead; ++k) {
            juraic::Incoming nobody;
            juraic::PlayerEvents e = juraic::advancePlayer(own, nobody, 0, tsumo, rules, t.timing, t.roundFrame(next));
            juraic::settlePlayer(own, rules, t.timing, noGarbage, e);
        }
        if (own.phase != juraic::Phase::Next) return;
        // (garbage the screen showed come to rest after the pair before: the field as the last clear picture showed it,
        //  standing, with the engine's 13th and 14th rows, and garbage in the 13th row of a column read twelve high as
        //  far as the screen tells - begun on the engine's field, which knows of no garbage, the pair was planned over a
        //  column the garbage had made twelve high and locked where it appeared, 3 pairs of 78 in rounds drawn so; with
        //  the 13th row as the engine had it, over a column the garbage had made thirteen high, and went two columns
        //  short)
        const SideTrack& side = t.side[me];
        const juraic::GameField engineShown = Follower::shown(own.field);
        uint8_t guessed = 0;
        if (side.loose == 0 && !(side.resting == engineShown) && Follower::fits(side.resting, engineShown)) {
            own.field = Follower::withGarbage(side.resting, own.field, rules.maxGarbagePerDrop / juraic::W, guessed);
            note(t.frame, "2P pair " + std::to_string(side.pairs + side.unseen + 1) + ": the field with the garbage the screen showed");
        }
        shareHidden(f, side.pairs + side.unseen + 1, guessed);
        // (the colours learnt since the last pair begun: from the third pair begun unseen in a row on, the engine had
        //  none, put pairs of no colour, and its field stayed lower than the game's)
        learnPairs(t);
        carefulDown(true);
        own.wait = 1;
        next = appears + 1;
        pairsSeen = t.side[me].pairs + t.side[me].unseen + 1;
        blindFor = pairsSeen, blindAt = appears;
        earlyFor = -1;
        start(f, now + lag, "2P pair " + std::to_string(pairsSeen) + " begun unseen: the screen not showing it, the engine had it appear in frame " +
                                std::to_string(appears) + ", lag " + std::to_string(lag));
    }
    // The next pair is begun early (early) when its first button, sent now, reaches the game in the frame the engine
    // foresees it in, and the engine is sure of it: the pair before begun as the screen showed it and put where the
    // engine has it - twelve rows as the screen shows them, nothing of it in the air -, no garbage on its way, every
    // picture of the own side clear for EARLY_CLEAR frames, and the other side still. (Under a light garbage fell on
    // 2P: the engine, which knows of none, foresaw the pair as it fell and put it, and the screen showed the pair
    // appear only after the light; it was not taken again, and fell by itself, in rounds drawn so. A chain of the other
    // side may send garbage the notice does not show yet.)
    static constexpr int EARLY_CLEAR = 30;
    bool earlyDue(const Follower& f, int64_t now) const {
        const Tracker& t = f.tracker;
        const SideTrack& side = t.side[me];
        if (!early || pairsSeen == 0 || blindFor >= 0 || earlyFor >= 0 || !following || own.phase != juraic::Phase::Next || own.wait > 2) return false;
        if (f.notice[me] != 0 || side.loose != 0 || side.pairs != pairsSeen || side.unseen != 0) return false;
        if (now - unclearAt < EARLY_CLEAR || !t.still(1 - me)) return false;
        // (nor before the pictures have told the lag and the way of the turns as the driver has them: the first turn of a
        //  pair begun early is taken in the frame it appears in and shown late, and a turn the other way round, or a frame
        //  late, was put right too late - pairs begun so went down turned otherwise, 3 of 77 in rounds drawn with the turns
        //  of the pad the other way round, the lag a frame off as they began, turns shown 6 frames late)
        if (turnsOther > -TURN_MARGIN || int(lags.size()) < FIT_PAIRS || measuredLag() != lag) return false;
        if (!side.clear || !(Follower::shown(own.field) == Follower::shown(side.resting))) return false;
        return now + lag >= next + own.wait - 1; // (the frame the engine has the pair appear in)
    }
    // ... begun with the engine as it stands, the pair appearing in the frame foreseen (as begin: the pair before fitted).
    void beginEarly(const Follower& f, int64_t now) {
        const Tracker& t = f.tracker;
        const int64_t appears = next + own.wait - 2;
        pairsSeen = t.side[me].pairs + 1;
        // (the pair before, whose pictures are all in, kept before start clears its engine - fitted after, as begin does,
        //  its pictures were set against the engine of the pair begun, and the lag of a pad 5 late was taken as 6 -, and
        //  fitted as the next pair is begun: the screen shows where it lies only later)
        fitLater(f);
        fitted = std::make_unique<Fitted>(toFit(pairsSeen - 1));
        shots.clear();
        watchFitted(f);
        learnPairs(t);
        carefulDown(false);
        own.wait = 1;
        next = appears + 1;
        earlyFor = pairsSeen, earlyAt = appears, earlyShifted = false;
        start(f, std::max(now + lag, next), "2P pair " + std::to_string(pairsSeen) + " begun early: the engine has it appear in frame " + std::to_string(appears) + ", lag " +
                                                std::to_string(lag));
    }
    // The pair in play begun (begin, beginBlind, beginEarly) with the engine as it is set, noted so (`line`): the frames
    // of the game before `at` have passed - the agent decides them, the game took what the pad held.
    void start(const Follower& f, int64_t at, const std::string& line) {
        startOwn = own, startNext = next, shiftBegun = f.tracker.side[me].shiftedAt, putAim = -1, sentFor = false;
        past.clear();
        checked = shownAt = doubtAt = begunAt = -1;
        active = true, following = false, untilLocked = false;
        ++played;
        noted = -2;
        note(f.tracker.frame, line);
        bool same = true;
        while (active && next < at) same = play(f, false) && same;
        if (!same) steer(f);
    }

    // One frame of the game: the agent decides it (its buttons are sent), or it took what was held. False when the
    // agent decided other buttons than those the frame took.
    bool play(const Follower& f, bool send) {
        juraic::ai::Observation o;
        observe(f, own, next, o);
        tellHeld();
        const auto* placing = placer();
        const int64_t revisions = placing ? placing->revisedChoices : 0;
        const uint8_t wanted = uint8_t(agent->frame(o) & ~juraic::BTN_DROP);
        if (placing && placing->revisedChoices != revisions)
            note(next, "2P pair " + std::to_string(pairsSeen) + " choice renewed: the field or known pairs changed");
        // (a tap the agent lets go before its end - its way planned again, a picture put right - is held to its end: one
        //  that went out for a frame as a pair was put right was lost by a frame of the game under load now and then)
        if (send) held = tapToEnd(next - lag, wanted), sentFor = true;
        // (a frame whose buttons went out before took what the pad held lag frames before it: a pair begun early and
        //  taken again from the screen, as it was held now, had its first move played twice, a column too far)
        const uint8_t took = send ? held : givenAt(next - lag);
        juraic::Incoming nobody;
        juraic::advancePlayer(own, nobody, took, tsumo, rules, f.tracker.timing, f.tracker.roundFrame(next));
        past.push_back({next, own, took});
        if (past.size() > 240) past.pop_front();
        ++next;
        if (own.phase != juraic::Phase::Next && own.phase != juraic::Phase::Control) lockEngine(f.tracker.frame);
        return wanted == took;
    }

    // The engine has locked the pair in play, in the frame `at` of the clock: nothing is pressed for it but the down
    // button (holdDown), where it locked it is kept for Follower (tick), and the placement it was brought to (target).
    void lockEngine(int64_t at) {
        // (not when no button has been sent for it since it was begun: the placement chosen then moved none - a pair seen
        //  late, put as the frames that had passed were played again, was counted against a placement chosen after it was put)
        const auto* placing = placer();
        putAim = placing && sentFor ? placing->targetPlacement() : -1;
        stop(), following = true, untilLocked = true, lockedAt = at;
        lockedPair = pairsSeen - 1, lockedPlacement = own.pair.placement();
    }

    // Between the own pairs the AI is shown each frame all the same, the own side waiting for its pair; nothing it
    // asks for is pressed.
    void wait(const Follower& f, int64_t now) {
        const Tracker& t = f.tracker;
        if (following) {
            const int64_t at = now + lag;
            // (the field is dead only as the next pair would appear, as the game tells it: a pair locked where pairs
            //  appear may go in its chain - stopped there, the engine foresaw no pair, no pair was begun unseen, and the
            //  game's pair was left alone, in rounds drawn with the screen of 2P lit as pairs appear)
            while (next < at && !(own.phase == juraic::Phase::Next && own.wait <= 2) && own.phase != juraic::Phase::Dead) {
                juraic::Incoming nobody;
                juraic::PlayerEvents e = juraic::advancePlayer(own, nobody, 0, tsumo, rules, t.timing, t.roundFrame(next));
                juraic::settlePlayer(own, rules, t.timing, noGarbage, e); // (the garbage it does not know of: none)
                ++next;
            }
            if (own.phase == juraic::Phase::Control) {
                following = false; // (it appeared in the engine after all: as below)
            } else {
                juraic::ai::Observation o;
                observe(f, own, now, o);
                agent->frame(o);
                return;
            }
        }
        if (t.roundStart < 0 || now <= t.roundStart) return;
        juraic::Player waiting;
        waiting.field = f.model[me].known ? f.model[me].core.field : t.side[me].resting;
        waiting.phase = juraic::Phase::Next;
        waiting.wait = 60; // (not about to appear: the AI does not choose yet)
        waiting.nextPair = waiting.moves = t.side[me].pairs;
        juraic::ai::Observation o;
        observe(f, waiting, now, o);
        agent->frame(o);
    }

    // The pair the engine has locked needs the down button no longer: the screen shows it lying where it locks, or the
    // engine foresees the next pair by the frame the button would reach the game. (Held until the next pair was seen, the
    // button reached that pair in its first frames - on the real game 22 pairs of 39 in a record -, and a pair that had
    // fallen by it found no way to its placement behind a high column: four pairs went down where they appeared.)
    // Shown lying, a pair the engine has locked is held down LOCK_DOWN frames of the clock more: the frame of the game the
    // engine locked it in may have taken the buttons of the frame after, the down button lost (let go as the screen showed
    // it lying, a pair whose down button was lost so lay unlocked with no button for 10 frames, in a round drawn with a
    // pad a frame early or late now and then). The next pair comes 13 frames after a lock at the soonest.
    static constexpr int LOCK_DOWN = 2;
    bool lies(const Follower& f) const {
        const SideModel& m = f.model[me];
        // (a turn sent and not yet taken by the game can lift a pair read lying: not lying until it has been)
        bool turnOnItsWay = false;
        for (int k = 0; k <= lag && !turnOnItsWay; ++k) turnOnItsWay = (padAt(f.tracker.frame - k) & (juraic::BTN_CW | juraic::BTN_CCW)) != 0;
        // (under the words of an all clear the words read as pieces under a pair in the air: not taken as lying there -
        //  let go so, a pair the engine had locked a row higher fell by itself for 10 frames, three rounds drawn so)
        const bool words = f.tracker.side[me].words.any;
        if (m.lying && m.index == pairsSeen - 1 && !turnOnItsWay && !words && f.tracker.frame >= lockedAt + LOCK_DOWN) return true;
        // (a clear picture shows the pair in the air: the next pair comes only after it lies, whatever the engine foresees
        //  - the game stopped for 390 frames, the engine went on and foresaw the next pair, and the pair fell by itself
        //  for 320 frames on the real game once it went on)
        if (shownInAir(f)) return false;
        // (nor for a pair the screen has not shown, while the window stands as it stood when the pair was begun: the next
        //  pair comes only after the window has moved on - the game stopped two frames before a pair appeared, the engine
        //  went on and foresaw the next pair, and the pair, lit as the game went on, fell by itself in rounds drawn so)
        if (unshown() && windowWaits(f.tracker.side[me])) return false;
        return following && own.phase == juraic::Phase::Next && own.wait <= 2;
    }
    // The pair in play was begun before the screen showed it (early, or unseen), and the screen has not shown it yet.
    bool unshown() const { return pairsSeen > 0 && (earlyFor == pairsSeen || blindFor == pairsSeen); }
    // The window of the next pairs is read as it last stood still, and has not moved on since the pair in play was begun.
    bool windowWaits(const SideTrack& side) const { return side.shiftedAt <= shiftBegun && side.nextRead[0] && side.nextRead[1] && windowOddFor == 0; }
    // The game has stopped: the engine has put a pair the screen has not shown (begun early, or unseen) and foresaw the next
    // one BLIND_SLACK frames ago, the window still stands as it stood when the pair was begun - it moves on only after the
    // pair is put -, the own side is not read, and the other side, read, has not changed for STOP_FRAMES frames (on the
    // real game the round stopped for 390 frames and for a minute; the other side's pair or chain goes on otherwise - with
    // the own side under a light and the other side not looked at, a chain the engine did not know of made the next pair
    // 70 frames later than foreseen, and the pair taken as stopped was begun again for nothing).
    static constexpr int STOP_FRAMES = 30;
    bool stopped(const Follower& f, int64_t now) const {
        const SideTrack& side = f.tracker.side[me];
        if (!unshown() || !following || side.clear || !windowWaits(side) || side.pairs + side.unseen >= pairsSeen) return false;
        return own.phase == juraic::Phase::Next && own.wait <= 2 && now >= next + own.wait - 2 + BLIND_SLACK && now - otherMovedAt >= STOP_FRAMES;
    }
    // The other side as last read clear (watchOther): the pieces at rest, the pair in hand and where it is.
    void watchOther(const Tracker& t, int64_t now) {
        const SideTrack& o = t.side[1 - me];
        const bool same = o.clear && o.resting == otherResting && o.inHand == otherInHand &&
                          (!o.inHand || (o.pair.seen && o.pair.at.x == otherAt.x && o.pair.at.y == otherAt.y && o.pair.at.upper == otherAt.upper && o.pair.at.dir == otherAt.dir));
        if (same) return;
        otherMovedAt = now; // (not read clear: not known to stand still)
        if (o.clear) otherResting = o.resting, otherInHand = o.inHand, otherAt = o.pair.at;
    }
    // ... the pair is begun again as the engine began it, appearing in the frame the button sent now reaches the game, the
    // down button only once it is turned and moved where it goes, and again each time the engine has put it, until the
    // other side changes (the game goes on: the pair begun again then is played on, and the screen puts it right when it
    // shows it). The engine as it had put the pair first is kept (undoBegunAgain). (Played as the engine had foreseen it,
    // the pair was put while the game stood still, and the game's pair, lit as the game went on, fell by itself; begun
    // again only once the other side changed, 12 frames after the game went on at times, it was left alone until then.)
    void beginAgain(const Follower& f, int64_t now) {
        const int64_t foreseen = next + own.wait - 2;
        const int64_t appears = now + lag - 1;
        if (againFor != pairsSeen) putOwn = own, putNext = next, putPlacement = lockedPlacement, againFor = pairsSeen;
        own = startOwn;
        own.wait = 1;
        next = appears + 1;
        if (earlyFor == pairsSeen) earlyAt = appears;
        else blindAt = appears;
        shots.clear(); // (not fitted: its engine is not begun as it appeared)
        carefulDown(true);
        start(f, now + lag, "2P pair " + std::to_string(pairsSeen) + " begun again: the game went on after it stopped, the engine had the next pair come in frame " +
                                std::to_string(foreseen) + ", lag " + std::to_string(lag));
    }
    // A pair begun again (beginAgain) and not shown yet, and the window starts moving on: the pair had been put (the next
    // pair comes 8 frames after the window starts moving). The engine is taken back to where it had put it, and nothing is
    // pressed for it any more. (Taken back as the window had moved on, a frame before the next pair appeared, the buttons
    // of the pair begun again went on to it, in a round drawn with a chain the engine did not know of under a light.)
    void undoBegunAgain(const Follower& f) {
        const SideTrack& side = f.tracker.side[me];
        if (againFor != pairsSeen || !unshown()) return;
        if (earlyFor == pairsSeen && !earlyShifted) return; // (the window moving on as the pair begun early appears: takePair)
        const bool moving = (!side.nextRead[0] && !side.nextRead[1]) || windowOddFor >= 2;
        if (!moving && side.shiftedAt <= shiftBegun) return;
        own = putOwn, next = putNext, lockedPair = pairsSeen - 1, lockedPlacement = putPlacement;
        againFor = -1;
        stop(), following = true, untilLocked = false;
        note(f.tracker.frame, "2P pair " + std::to_string(pairsSeen) + " not begun again after all: the window moved on, the engine as it had put it");
    }
    // A clear picture shows the pair the engine has put (it still in hand on the screen) in the air.
    bool shownInAir(const Follower& f) const {
        const SideTrack& side = f.tracker.side[me];
        return following && side.pairs == pairsSeen && side.inHand && side.clear && side.pair.seen && !pairLying(side.pair, side.under);
    }
    // The game has stopped and gone on (or is far behind the engine for another reason): RESUME_AFTER frames of the clock
    // after the engine put the pair, two clear pictures in a row - the second no higher than the first - still show it in
    // the air. (On the real game the round stopped for 390 frames, a pair in play: the engine went on and put it, and the
    // game's pair was then in the air for 320 frames.) The pictures show a pair locked for a few frames at most: the
    // lag, the frames a picture is behind.
    static constexpr int RESUME_AFTER = 30;
    bool overdue(const Follower& f) {
        const Tracker& t = f.tracker;
        if (!shownInAir(f) || t.frame - lockedAt <= RESUME_AFTER) {
            airSeenAt = -1;
            return false;
        }
        const juraic::PairControl& seen = t.side[me].pair.at;
        const int height = 2 * seen.y + (seen.upper ? 1 : 0);
        const bool twice = airSeenAt >= 0 && t.frame - airSeenAt <= 2 && height <= airHeight;
        if (t.frame != airSeenAt) airSeenAt = t.frame, airHeight = height;
        return twice;
    }
    // ... the engine takes the pair from the last picture, on the field it was begun on, and the way to the placement the
    // agent chose for it is planned again from there (held down where it was, the pair went down elsewhere than the AI
    // chose, 6 of 24 pairs in rounds drawn so).
    void resume(const Follower& f, int64_t now) {
        const Tracker& t = f.tracker;
        size_t i = past.size();
        while (i > 0 && past[i - 1].state.phase != juraic::Phase::Control) --i;
        if (i == 0) return;
        juraic::Player again = past[i - 1].state;
        const juraic::PairControl& seen = t.side[me].pair.at;
        juraic::PairControl& p = again.pair;
        p = juraic::PairControl{};
        p.x = seen.x, p.y = seen.y, p.dir = seen.dir, p.upper = seen.upper;
        own = again;
        next = t.frame + 1;
        shots.clear(); // (not fitted: its engine is not begun as it appeared)
        airSeenAt = -1;
        if (auto* placing = placer()) {
            juraic::ai::Observation o;
            observe(f, own, next, o);
            tellHeld();
            placing->resume(o);
        }
        start(f, now + lag, "2P pair " + std::to_string(pairsSeen) + " taken up again: the engine put it in frame " + std::to_string(lockedAt) + ", the picture shows " +
                                controlText(seen));
    }

    void steer(const Follower& f) {
        if (auto* placing = placer()) {
            juraic::ai::Observation o;
            observe(f, own, next, o);
            tellHeld();
            placing->steer(o);
        }
    }
    // The agent is told how many frames the turn and the move the engine's pair held in its last frame have been held
    // (PlacementAgent::tapsHeldFor): a way planned again while a tap is held holds it to the end of its tapHold frames.
    // (Let go at once, a tap was cut to one frame when a second choice came or a pair was put right: on the real game
    // 103 pairs of 465 had such a tap, and one lost by a frame of the game left a pair short of its placement.)
    void tellHeld() {
        auto* placing = placer();
        if (!placing) return;
        for (int k = 0; k < 2; ++k) {
            const uint8_t h = own.pair.held & TAP_KINDS[k];
            int run = 0;
            for (size_t i = past.size(); h && i-- > 0 && (past[i].buttons & TAP_KINDS[k]) == h;) ++run;
            placing->tapsHeldFor[k] = run;
        }
    }

    // A clear picture of the pair in play, kept for the fit.
    void keepShot(const Follower& f) {
        const SideTrack& s = f.tracker.side[me];
        if (pairsSeen <= 0 || s.pairs != pairsSeen || !s.inHand || !s.pair.seen || !s.clear) return;
        if (!shots.empty() && shots.back().frame >= f.tracker.frame) return;
        // (not a picture up to TURN_LATEST frames after a turn that changed the column or the row of the engine's pair: a
        //  kick or a lift is shown late, and such pictures took the lag to 8 frames for a pad 5 late in rounds drawn so)
        for (size_t i = past.size(); i-- > 1 && past[i].frame > f.tracker.frame - TURN_LATEST;) {
            const juraic::PairControl& a = past[i].state.pair;
            const juraic::PairControl& b = past[i - 1].state.pair;
            if (past[i].frame <= f.tracker.frame && past[i].state.phase == juraic::Phase::Control && past[i - 1].state.phase == juraic::Phase::Control &&
                a.dir != b.dir && (a.x != b.x || a.y != b.y || a.upper != b.upper))
                return;
        }
        shots.push_back({f.tracker.frame, s.pair.at});
    }

    // What an engine shows of the pair last played, given the buttons that went to the pad `by` frames late, the turns as
    // they are named (swap false) or the other way round: the pictures it shows right, and whether it locks the pair
    // where the screen found it locked (`placed`, -1: not found). pictures -1: a pair of one colour (its way does not show).
    struct Fit {
        int pictures = 0;
        bool placed = false;
    };
    // A pair whose pictures are fitted: the engine as it was begun and the frame it began with, its clear pictures, the
    // pairs the engine knew, its number, whether the engine had its axis in the 13th row or above, and where the screen
    // found it locked (-1: not found).
    struct Fitted {
        juraic::Player own;
        int64_t next = 0;
        std::vector<Shot> shots;
        juraic::Tsumo tsumo;
        int pair = 0;
        bool high = false;
        int placed = -1;
    };
    // The pair in play so, numbered `pair`.
    Fitted toFit(int pair) const {
        Fitted p{startOwn, startNext, shots, tsumo, pair, false, -1};
        for (const Past& q : past) p.high = p.high || (q.state.phase == juraic::Phase::Control && q.state.pair.y >= 12);
        return p;
    }
    Fit fitScore(const Follower& f, const Fitted& fitted, int by, bool swap) const {
        const Tracker& t = f.tracker;
        juraic::Player p = fitted.own;
        std::vector<juraic::Player> states; // by the frame of the game from fitted.next on
        const int64_t last = fitted.shots.back().frame;
        Fit out;
        // (on to the lock, at most up to the frame the next pair was seen in)
        for (int64_t g = fitted.next; g <= std::max(last, t.frame); ++g) {
            uint8_t b = padAt(g - by);
            if (swap) b = swapTurns(b);
            juraic::Incoming nobody;
            const juraic::PlayerEvents e = juraic::advancePlayer(p, nobody, b, fitted.tsumo, rules, t.timing, t.roundFrame(g));
            if (e.locked) out.placed = fitted.placed >= 0 && juraic::placementId(p.pair.placement()) == fitted.placed;
            if (g <= last) states.push_back(p);
            if (p.phase != juraic::Phase::Next && p.phase != juraic::Phase::Control) break;
        }
        const auto at = [&](int64_t g) -> const juraic::Player* {
            if (g < fitted.next || g - fitted.next >= int64_t(states.size())) return nullptr;
            const juraic::Player& s = states[size_t(g - fitted.next)];
            return s.phase == juraic::Phase::Control ? &s : nullptr;
        };
        for (const Shot& s : fitted.shots) {
            const juraic::Player* row = at(s.frame);
            const juraic::Player* column = at(s.frame - MOVE_SHOWN);
            const juraic::Player* way = at(s.frame - TURN_SHOWN);
            if (!row || !column || !way) continue;
            if (row->colors.axis == row->colors.child) return {-1, false};
            out.pictures += row->pair.y == s.at.y && row->pair.upper == s.at.upper && column->pair.x == s.at.x && way->pair.dir == s.at.dir;
        }
        return out;
    }

    // The lag and the way of the turns that fit the pictures of the last pairs (fit), taken when they fit them better: the
    // pair before the one begun now (pairsSeen), where the screen found it locked. A pair begun early (beginEarly) is begun
    // before the screen shows where the pair before it lies: that pair is fitted as the next one is begun (fitLater), with
    // where the screen found it locked in the meantime (watchFitted). (Fitted as the pair after it was begun early, 1 of
    // the first 9 pairs fitted had been found locked in a round drawn with the turns of the pad the other way round, 8 of
    // 9 fitted so; told by the pictures alone, the turns the other way round were found 7 pairs later than in the round
    // with no pair begun early.)
    void fit(const Follower& f) {
        fitLater(f);
        Fitted p = toFit(pairsSeen - 1);
        shots.clear();
        const SideModel& m = f.model[me];
        if (m.followed == pairsSeen - 2) p.placed = juraic::placementId(m.placed);
        fitPair(f, p);
    }
    std::unique_ptr<Fitted> fitted; // a pair begun before the pair begun early, to be fitted (fitLater)
    void fitLater(const Follower& f) {
        if (!fitted) return;
        const std::unique_ptr<Fitted> p = std::move(fitted);
        fitPair(f, *p);
    }
    void watchFitted(const Follower& f) {
        const SideModel& m = f.model[me];
        if (fitted && fitted->placed < 0 && m.followed == fitted->pair - 1) fitted->placed = juraic::placementId(m.placed);
    }
    void fitPair(const Follower& f, const Fitted& p) {
        if (!takeLag || int(p.shots.size()) < FIT_SHOTS) return;
        const int placed = p.placed;
        FitTable table{};
        std::array<std::array<bool, 2>, 2 * FIT_REACH + 1> lockedThere{};
        int bestHere = 0, bestHereScore = -1;
        for (int d = -FIT_REACH; d <= FIT_REACH; ++d)
            for (int flip = 0; flip < 2; ++flip) {
                const int by = lag + d;
                const Fit score = by < 0 ? Fit{} : fitScore(f, p, by, turnsSwapped != (flip != 0));
                if (score.pictures < 0) return; // (a pair of one colour tells nothing of the turns)
                table[size_t(d + FIT_REACH)][size_t(flip)] = score.pictures;
                lockedThere[size_t(d + FIT_REACH)][size_t(flip)] = score.placed;
                if (flip == 0 && (score.pictures > bestHereScore || (score.pictures == bestHereScore && std::abs(d) < std::abs(bestHere))))
                    bestHere = d, bestHereScore = score.pictures;
            }
        const int64_t at = f.tracker.frame;
        const size_t here = size_t(bestHere + FIT_REACH);
        note(at, "lag measured: " + std::to_string(lag + bestHere) + " frames (pictures of pair " + std::to_string(p.pair) + ": " + std::to_string(bestHereScore) +
                     " of " + std::to_string(p.shots.size()) + " right, " + std::to_string(table[size_t(FIT_REACH)][0]) + " with the lag " + std::to_string(lag) +
                     "; the turns the other way round " + std::to_string(table[here][1]) + (placed >= 0 ? std::string(", locked where found: ") + (lockedThere[here][0] ? "as sent" : "-") +
                     (lockedThere[here][1] ? " other way" : "") : std::string()) + ")");
        lags.push_back(lag + bestHere);
        if (lags.size() > 15) lags.erase(lags.begin());
        // The way of the turns, at the lag that fits this pair best.
        const int told = table[here][1] - table[here][0] + PLACED_WEIGHT * (int(lockedThere[here][1]) - int(lockedThere[here][0]));
        turnsOther += p.high ? std::clamp(told, -TURN_STEP, TURN_STEP) : told;
        turnsOther = std::max(turnsOther, -2 * TURN_MARGIN); // (a long run as it is does not keep a change from being found)
        if (turnsOther >= TURN_MARGIN) {
            turnsSwapped = !turnsSwapped, turnsOther = 0;
            note(at, std::string("the turns are taken the other way round: the buttons of the two turns are now sent ") + (turnsSwapped ? "swapped" : "as they are"));
            fits.clear(); // (the tables were of the turns before)
            return;
        }
        // The lag, with the turns as they are.
        fits.push_back(table);
        if (fits.size() > size_t(FIT_PAIRS)) fits.pop_front();
        std::array<int, 2 * FIT_REACH + 1> sum{};
        for (const FitTable& one : fits)
            for (size_t i = 0; i < sum.size(); ++i) sum[i] += one[i][0];
        int bestD = 0;
        for (int d = -FIT_REACH; d <= FIT_REACH; ++d) {
            const int s = sum[size_t(d + FIT_REACH)], b = sum[size_t(bestD + FIT_REACH)];
            if (s > b || (s == b && std::abs(d) < std::abs(bestD))) bestD = d;
        }
        if (bestD == 0 || sum[size_t(bestD + FIT_REACH)] < sum[size_t(FIT_REACH)] + FIT_MARGIN) return;
        note(at, "the lag taken from the pictures: " + std::to_string(lag + bestD) + " frames (was " + std::to_string(lag) + ")");
        lag += bestD;
        fits.clear(); // (the tables were of the lag before)
    }

    // The picture `seen` (pair colours `c`) of the frame g of the game shows the pair where the engine had it (`in`: the
    // engine's states, past): its row as in g, its column as in g or MOVE_SHOWN frames before, the way of its child as in g
    // or up to TURN_LATEST frames before.
    bool shows(int64_t g, const juraic::PairControl& seen, juraic::Pair c) const { return shows(past, g, seen, c); }
    bool shows(const std::deque<Past>& in, int64_t g, const juraic::PairControl& seen, juraic::Pair c) const {
        // (in the frame before the first one the engine played the pair is as it appears, before the buttons of its first
        //  frame: a pair begun early takes its first turn in the frame it appears in, and a turn with the move after it
        //  is shown late, the pair still as it appeared - put right for that picture, 10 pairs of 151 went down turned
        //  otherwise in rounds drawn with such turns shown 6 frames late, 5 of 155 taken so)
        static const juraic::PairControl appearing{};
        const auto at = [&](int64_t frame) -> const juraic::PairControl* {
            const size_t i = indexIn(in, frame);
            if (i < in.size()) return in[i].state.phase == juraic::Phase::Control ? &in[i].state.pair : nullptr;
            return frame == startNext - 1 && !in.empty() && in.front().frame == startNext && in.front().state.phase == juraic::Phase::Control ? &appearing : nullptr;
        };
        const juraic::PairControl* row = at(g);
        if (!row) return false;
        if (samePlace(*row, seen, c)) return true;
        // (the row: as in g, or as before a turn that changed it up to TURN_LATEST frames before - a quick turn or a floor
        //  kick is shown late as a turn is: on the real game a quick turn in the 12th row showed its lifted child 6 frames
        //  after the engine had it, and put right for the picture before it, the pair was turned again and went down in
        //  the third column)
        bool rowShown = row->y == seen.y && row->upper == seen.upper;
        for (int64_t q = g; q > g - TURN_LATEST && !rowShown; --q) {
            const juraic::PairControl* after = at(q);
            const juraic::PairControl* before = at(q - 1);
            if (after && before && after->dir != before->dir && (after->y != before->y || after->upper != before->upper)) {
                if (samePlace(*before, seen, c)) return true; // (as it was before that turn: a pair of one colour either way round)
                rowShown = before->y == seen.y && before->upper == seen.upper;
            }
        }
        if (!rowShown) return false;
        bool column = false;
        for (int k = 0; k <= MOVE_SHOWN && !column; ++k)
            if (const juraic::PairControl* p = at(g - k)) column = p->x == seen.x;
        // (KICK_SHOWN frames after such a turn; TURN_LATEST when it changed the row too, shown as late as the row)
        for (int k = 0; k <= TURN_LATEST && !column; ++k) {
            const juraic::PairControl* after = at(g - k);
            const juraic::PairControl* before = at(g - k - 1);
            if (after && before && after->dir != before->dir && after->x != before->x &&
                (k <= KICK_SHOWN || after->y != before->y || after->upper != before->upper))
                column = seen.x >= std::min(before->x, after->x) && seen.x <= std::max(before->x, after->x);
        }
        if (!column) return false;
        // (the way: as the engine had it from TURN_LATEST frames before on, and back through turns that came one after the
        //  other into that time, each within TURN_RUN frames of the next - the way before the first of them included: the
        //  real game shows such turns one after the other)
        // (taps held longer - PlacementAgent::tapHold - space the turns of a way so much more)
        const auto* placing = placer();
        const int run = TURN_RUN + (placing ? 2 * (placing->tapHold - 1) : 0);
        int64_t from = g - TURN_LATEST;
        for (int extended = 0, more = 1; more && extended < 30; ++extended) {
            more = 0;
            for (int64_t q = from; q > from - run && !more; --q) {
                const juraic::PairControl* before = at(q - 1);
                const juraic::PairControl* after = at(q);
                if (before && after && before->dir != after->dir) from = q - 1, more = 1;
            }
        }
        for (int64_t q = from; q <= g; ++q)
            if (const juraic::PairControl* p = at(q); p && p->dir == seen.dir) return true;
        return false;
    }

    // A pair the pictures showed where the engine had it, up to DOUBT_FRAMES frames before, is put right only on two
    // clear pictures in a row that show it alike - in the same column, turned the same way, the later one at most as many
    // half cells lower as frames came between - each otherwise than the engine has it. (Put right on one picture, a pair
    // read where it was not as it turned was turned once more: on the real game 8 pairs of a record went down upright; of
    // its pictures that put a pair right and were followed by no picture of it for three frames, 10 of 26 ended so, of
    // those followed by a picture alike 4 of 52.) A pair not shown so - begun unseen, garbage the engine knows nothing of
    // under it - is put right on its first picture: waiting for a second, in rounds drawn with garbage under the pairs
    // begun unseen 13 pairs of 155 went elsewhere than the AI chose, 9 of 154 put right at once.
    static constexpr int DOUBT_FRAMES = 8;
    static bool alike(const juraic::PairControl& before, const juraic::PairControl& after, juraic::Pair c, int frames) {
        const auto same = [&](const juraic::PairControl& p) {
            const int fell = 2 * (before.y - p.y) + (before.upper ? 1 : 0) - (p.upper ? 1 : 0);
            return p.x == before.x && p.dir == before.dir && fell >= 0 && fell <= frames;
        };
        if (same(after)) return true;
        if (c.axis != c.child || after.childY() >= juraic::ROW_14) return false;
        return same(swappedAxis(after)); // (a pair of one colour read the other way round)
    }
    // A pair of one colour taken the other way round: its child as its axis.
    static juraic::PairControl swappedAxis(const juraic::PairControl& p) {
        juraic::PairControl other = p;
        other.x = int8_t(p.childX()), other.y = int8_t(p.childY()), other.dir = uint8_t((p.dir + 2) % 4);
        return other;
    }

    // A picture to put the pair right for, explained by one cell of the 13th or 14th row not sure of (unsure13, unsure14)
    // the other way: the engine played again from the pair's beginning with that cell so shows the pair where the picture
    // does, and with no other such cell. The engine takes that field, the cell is found (found), and the way to the
    // placement chosen is planned again from where the pair is (put right for the picture, the pair was planned again on
    // the field that had made it go astray: InputPlanner::planSure counts on this).
    bool explain(const Follower& f, const juraic::PairControl& seen, juraic::Pair c) {
        if (!unsureWays || (!unsure13 && !unsure14) || past.empty() || past.front().frame != startNext) return false;
        const Tracker& t = f.tracker;
        std::deque<Past> again, taken;
        juraic::GameField field;
        int x = -1, ways = 0;
        bool row14 = false;
        for (int k = 0; k < 2 * juraic::W; ++k) {
            const int cx = k % juraic::W;
            const bool r14 = k >= juraic::W;
            if (!((r14 ? unsure14 : unsure13) >> cx & 1)) continue;
            juraic::Player p = startOwn;
            if (r14) p.field.row14[size_t(cx)] = p.field.row14[size_t(cx)] == juraic::EMPTY ? juraic::GARBAGE : juraic::EMPTY;
            else p.field.f.set(cx, 12, p.field.f.get(cx, 12) == juraic::EMPTY ? juraic::GARBAGE : juraic::EMPTY);
            const juraic::GameField flipped = p.field;
            again.clear();
            for (const Past& q : past) {
                juraic::Incoming nobody;
                juraic::advancePlayer(p, nobody, q.buttons, tsumo, rules, t.timing, t.roundFrame(q.frame));
                again.push_back({q.frame, p, q.buttons});
            }
            bool shown = false;
            for (const int by : {0, 1, -1, 2, -2}) shown = shown || shows(again, t.frame + by, seen, c);
            if (!shown) continue;
            if (++ways > 1) return false;
            taken = again, field = flipped, x = cx, row14 = r14;
        }
        if (ways != 1) return false;
        ++explained;
        past = taken, own = past.back().state, startOwn.field = field;
        const juraic::Cell cell = row14 ? field.row14[size_t(x)] : field.f.get(x, 12);
        found.push_back({x, row14, cell, Follower::shown(field).f.occupied().column(x)});
        (row14 ? unsure14 : unsure13) &= uint8_t(~(1u << x));
        tellUnsure();
        note(t.frame, "2P pair " + std::to_string(pairsSeen) + ": the picture tells the " + (row14 ? "14th" : "13th") + " row of column " + std::to_string(x + 1) + " is " +
                          (cell == juraic::EMPTY ? "empty" : "not empty") + " (the picture shows " + controlText(seen) + ")");
        if (own.phase != juraic::Phase::Control) {
            lockEngine(t.frame);
            return true;
        }
        steer(f);
        return true;
    }
    // A clear picture of the pair against the engine in the frame of the picture (or up to two frames either side: the
    // pad is a frame early or late now and then, 7% of the frames of a record of the real game, and the pair put right
    // for it had the buttons in flight played again for the wrong frames, sent to and fro). Shown right a little before,
    // put right only when the clear picture before showed it alike (DOUBT_FRAMES, alike).
    void correct(const Follower& f) {
        const Tracker& t = f.tracker;
        const SideTrack& side = t.side[me];
        if (!side.pair.seen || !side.clear || t.frame <= checked) return;
        checked = t.frame;
        const size_t i = pastIndex(t.frame);
        if (i == past.size() || past[i].state.phase != juraic::Phase::Control) {
            // (a pair begun early: the picture of it as it appears, before the first frame the engine played, shows it where
            //  the engine has it, so that one picture after it that shows the pair elsewhere puts nothing right - its first
            //  pictures hidden as it turned, one was put right for a picture of the stack read as a pair, in a round drawn
            //  so, and went down turned otherwise. A pair begun as the screen showed it is begun from that picture, which
            //  tells nothing of the engine.)
            if (earlyAt == startNext - 1 && t.frame == earlyAt && !past.empty() && shows(t.frame, side.pair.at, past.front().state.colors)) shownAt = t.frame, doubtAt = -1;
            return;
        }
        juraic::PairControl& had = past[i].state.pair;
        const juraic::PairControl& seen = side.pair.at;
        const juraic::Pair c = past[i].state.colors;
        // (a pair of one colour: which piece is its axis no picture tells, and the picture is taken either way round -
        //  read the other way round from the engine, a turned pair shown the way before its turn put right, and on the
        //  real game it was turned again and went down with its child up, not right)
        const bool either = c.axis == c.child && seen.childY() < juraic::ROW_14;
        const juraic::PairControl other = either ? swappedAxis(seen) : seen;
        for (const int by : {0, 1, -1, 2, -2})
            if (shows(t.frame + by, seen, c) || (either && shows(t.frame + by, other, c))) {
                shownAt = t.frame, doubtAt = -1;
                return;
            }
        if ((shownAt >= 0 && t.frame - shownAt <= DOUBT_FRAMES) || (begunAt >= 0 && t.frame - begunAt <= DOUBT_FRAMES && seen.y >= 12)) {
            const bool twice = doubtAt >= 0 && t.frame - doubtAt <= DOUBT_FRAMES && alike(doubt, seen, c, int(t.frame - doubtAt));
            doubtAt = t.frame, doubt = seen;
            if (!twice) return;
        }
        doubtAt = -1;
        if (explain(f, seen, c) || (either && explain(f, other, c))) return;
        ++corrected;
        note(t.frame, "2P pair " + std::to_string(pairsSeen) + " put right: the engine had " + controlText(had) + ", the picture shows " + controlText(seen));
        // (put right, a pair of one colour keeps the way the engine has it, and so the piece it has as its axis: the
        //  picture's way, its axis taken where it was seen last, was the other way round for a pair gone down since,
        //  and on the real game its way to a placement was two turns longer, turned at the wall it went down a column
        //  short. Turned in the last TURN_LATEST frames - the turn perhaps taken the other way round -, the axis is the
        //  piece nearer the engine's axis, which a turn leaves where it is, and the way the engine's only as near.)
        bool turned = false;
        for (size_t k = i; k > 0 && k + TURN_LATEST > i && !turned; --k) turned = past[k].state.pair.dir != past[k - 1].state.pair.dir;
        const auto apart = [&](const juraic::PairControl& p) {
            return 2 * std::abs(p.x - had.x) + std::abs(2 * p.y + (p.upper ? 1 : 0) - 2 * had.y - (had.upper ? 1 : 0));
        };
        const bool asEngine = either && (turned ? apart(other) < apart(seen) || (apart(other) == apart(seen) && other.dir == had.dir) : other.dir == had.dir);
        const juraic::PairControl& to = asEngine ? other : seen;
        had.x = to.x, had.y = to.y, had.dir = to.dir, had.upper = to.upper;
        had.grounded = false, had.moveWait = 0;
        // (the frames since played again with the buttons they took)
        own = past[i].state;
        for (size_t k = i + 1; k < past.size(); ++k) {
            juraic::Incoming nobody;
            juraic::advancePlayer(own, nobody, past[k].buttons, tsumo, rules, t.timing, t.roundFrame(past[k].frame));
            past[k].state = own;
        }
        if (own.phase != juraic::Phase::Control) {
            // (put right, the pair has come to rest in the engine: held down as after any pair the engine has put)
            lockEngine(t.frame);
            return;
        }
        steer(f);
    }
};

} // namespace play::realgame
