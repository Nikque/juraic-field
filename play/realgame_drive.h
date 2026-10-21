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
// pair is locked nothing is pressed until the next pair is seen; the AI is still shown every frame (an AI of a lower
// level sees the other side as it was some frames before: ai/pace.h).
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
    static void prepare(juraic::ai::Agent* agent) {
        if (auto* placing = dynamic_cast<juraic::ai::PlacementAgent*>(agent)) placing->holdSecond = true, placing->tapHold = 2, placing->waitQuickLock = true;
    }

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
    static juraic::GameField hiddenFromEngine(const juraic::GameField& followed, const juraic::GameField& engine) {
        juraic::GameField g = followed;
        for (int x = 0; x < juraic::W; ++x) {
            bool same = true;
            for (int y = 0; y < 12 && same; ++y) same = followed.f.get(x, y) == engine.f.get(x, y);
            if (!same) continue;
            g.f.set(x, 12, engine.f.get(x, 12));
            g.row14[size_t(x)] = engine.row14[size_t(x)];
        }
        return g;
    }

    // The placement the pair in play is brought to (-1: none).
    int target() const {
        const auto* placing = placer();
        return active && placing ? placing->targetPlacement() : -1;
    }

    // In each frame of the clock `now`, after the readings of the screen up to it are in `f`: the buttons to hold on
    // the pad from now on (the game takes them in its frame now + lag).
    uint8_t tick(const Follower& f, int64_t now) {
        const Tracker& t = f.tracker;
        if (nextAgent && !active) {
            if (agent) std::thread([before = std::move(agent)]() mutable { before.reset(); }).detach();
            agent = std::move(nextAgent);
            agent->startRound(me, seed++);
            noted = -2;
        }
        if (!agent) return held = 0;
        if (t.rounds != round) beginRound(t);
        takePair(f);
        keepShot(f);
        watchWindow(t);
        if (!active) {
            wait(f, now);
            if (blindDue(f, now)) beginBlind(f, now);
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

private:
    // (Tracker counts the pairs of a round from 0 only as a new round begins.)
    void beginRound(const Tracker& t) {
        following = untilLocked = false;
        liftedFor = shownAt = doubtAt = -1;
        windowReadAt = windowPauseAt = -1;
        own = juraic::Player{};
        past.clear();
        round = t.rounds, pairsSeen = 0, blindFor = -1, shiftBegun = -1;
        agent->startRound(me, seed++);
        stop();
        shots.clear(); // (a pair of the round before is not fitted: its pictures may be cut short)
    }
    // A pair seen, or one the window tells has come (the pair begun unseen let go): begun.
    void takePair(const Follower& f) {
        const Tracker& t = f.tracker;
        const SideTrack& side = t.side[me];
        // (a pair begun unseen is numbered one past those Tracker has counted, and stays so when the window moving on -
        //  not its count - tells it has been put)
        const int counted = side.pairs + side.unseen; // (with the pairs the window tells were put unseen)
        if (side.pairs > pairsSeen) {
            pairsSeen = side.pairs, blindFor = -1;
            begin(f, side);
        } else if (blindFor >= 0 && side.pairs == blindFor && t.spawned[me]) {
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
        //  reached the next pair as it appeared, 11 times in 44 pairs)
        const bool unseenInAir = blindFor == pairsSeen && !(side.clear && side.loose == 0);
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
    std::deque<Past> past;
    bool active = false;
    uint8_t held = 0;
    int round = -1, pairsSeen = 0;
    int begunRound = -1; // the round of the last pair begun (the engine's field is of it)
    int blindFor = -1;      // the pair begun unseen, until the screen shows it (-1: none)
    int64_t blindAt = -1;   // ... the frame the engine had it appear
    int64_t shiftBegun = -1; // the window's last move on (SideTrack::shiftedAt) as the pair in play was begun
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
    // Where the state after the frame `frame` of the game is in past (past.size(): not there). (The frames of past follow
    // one another: the place is told by the first of them.)
    size_t pastIndex(int64_t frame) const {
        if (past.empty() || frame < past.front().frame || frame > past.back().frame) return past.size();
        const size_t i = size_t(frame - past.front().frame);
        if (past[i].frame == frame) return i;
        for (size_t k = 0; k < past.size(); ++k)
            if (past[k].frame == frame) return k;
        return past.size();
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
    }

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
        //  standing, with the engine's 13th and 14th rows where the twelve are the same - begun on the engine's field,
        //  which knows of no garbage, the pair was planned over a column the garbage had made twelve high and locked
        //  where it appeared, 3 pairs of 78 in rounds drawn so)
        const SideTrack& side = t.side[me];
        const juraic::GameField engineShown = Follower::shown(own.field);
        if (side.loose == 0 && !(side.resting == engineShown) && Follower::fits(side.resting, engineShown)) {
            own.field = hiddenFromEngine(side.resting, own.field);
            note(t.frame, "2P pair " + std::to_string(side.pairs + side.unseen + 1) + ": the field with the garbage the screen showed");
        }
        // (the colours learnt since the last pair begun: from the third pair begun unseen in a row on, the engine had
        //  none, put pairs of no colour, and its field stayed lower than the game's)
        learnPairs(t);
        carefulDown(true);
        own.wait = 1;
        next = appears + 1;
        pairsSeen = t.side[me].pairs + t.side[me].unseen + 1;
        blindFor = pairsSeen, blindAt = appears;
        start(f, now + lag, "2P pair " + std::to_string(pairsSeen) + " begun unseen: the screen not showing it, the engine had it appear in frame " +
                                std::to_string(appears) + ", lag " + std::to_string(lag));
    }
    // The pair in play begun (begin, beginBlind) with the engine as it is set, noted so (`line`): the frames of the game
    // before `at` have passed - the agent decides them, the game took what was held.
    void start(const Follower& f, int64_t at, const std::string& line) {
        startOwn = own, startNext = next, shiftBegun = f.tracker.side[me].shiftedAt;
        past.clear();
        checked = shownAt = doubtAt = -1;
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
        f.observeAt(me, rules, own, next, o);
        tellHeld();
        const auto* placing = placer();
        const int64_t revisions = placing ? placing->revisedChoices : 0;
        const uint8_t wanted = uint8_t(agent->frame(o) & ~juraic::BTN_DROP);
        if (placing && placing->revisedChoices != revisions)
            note(next, "2P pair " + std::to_string(pairsSeen) + " choice renewed: the field or known pairs changed");
        // (a tap the agent lets go before its end - its way planned again, a picture put right - is held to its end: one
        //  that went out for a frame as a pair was put right was lost by a frame of the game under load now and then)
        if (send) held = tapToEnd(next - lag, wanted);
        juraic::Incoming nobody;
        juraic::advancePlayer(own, nobody, held, tsumo, rules, f.tracker.timing, f.tracker.roundFrame(next));
        past.push_back({next, own, held});
        if (past.size() > 240) past.pop_front();
        ++next;
        if (own.phase != juraic::Phase::Next && own.phase != juraic::Phase::Control) stop(), following = true, untilLocked = true, lockedAt = f.tracker.frame;
        return wanted == held;
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
                f.observeAt(me, rules, own, now, o);
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
        f.observeAt(me, rules, waiting, now, o);
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
        if (m.lying && m.index == pairsSeen - 1 && !turnOnItsWay && f.tracker.frame >= lockedAt + LOCK_DOWN) return true;
        // (a clear picture shows the pair in the air: the next pair comes only after it lies, whatever the engine foresees
        //  - the game stopped for 390 frames, the engine went on and foresaw the next pair, and the pair fell by itself
        //  for 320 frames on the real game once it went on)
        if (shownInAir(f)) return false;
        return following && own.phase == juraic::Phase::Next && own.wait <= 2;
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
            f.observeAt(me, rules, own, next, o);
            tellHeld();
            placing->resume(o);
        }
        start(f, now + lag, "2P pair " + std::to_string(pairsSeen) + " taken up again: the engine put it in frame " + std::to_string(lockedAt) + ", the picture shows " +
                                controlText(seen));
    }

    void steer(const Follower& f) {
        if (auto* placing = placer()) {
            juraic::ai::Observation o;
            f.observeAt(me, rules, own, next, o);
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
    Fit fitScore(const Follower& f, int by, bool swap, int placed) const {
        const Tracker& t = f.tracker;
        juraic::Player p = startOwn;
        std::vector<juraic::Player> states; // by the frame of the game from startNext on
        const int64_t last = shots.back().frame;
        Fit out;
        // (on to the lock, at most up to the frame the next pair was seen in)
        for (int64_t g = startNext; g <= std::max(last, t.frame); ++g) {
            uint8_t b = padAt(g - by);
            if (swap) b = swapTurns(b);
            juraic::Incoming nobody;
            const juraic::PlayerEvents e = juraic::advancePlayer(p, nobody, b, tsumo, rules, t.timing, t.roundFrame(g));
            if (e.locked) out.placed = placed >= 0 && juraic::placementId(p.pair.placement()) == placed;
            if (g <= last) states.push_back(p);
            if (p.phase != juraic::Phase::Next && p.phase != juraic::Phase::Control) break;
        }
        const auto at = [&](int64_t g) -> const juraic::Player* {
            if (g < startNext || g - startNext >= int64_t(states.size())) return nullptr;
            const juraic::Player& s = states[size_t(g - startNext)];
            return s.phase == juraic::Phase::Control ? &s : nullptr;
        };
        for (const Shot& s : shots) {
            const juraic::Player* row = at(s.frame);
            const juraic::Player* column = at(s.frame - MOVE_SHOWN);
            const juraic::Player* way = at(s.frame - TURN_SHOWN);
            if (!row || !column || !way) continue;
            if (row->colors.axis == row->colors.child) return {-1, false};
            out.pictures += row->pair.y == s.at.y && row->pair.upper == s.at.upper && column->pair.x == s.at.x && way->pair.dir == s.at.dir;
        }
        return out;
    }

    // The lag and the way of the turns that fit the pictures of the last pairs (fit), taken when they fit them better.
    void fit(const Follower& f) {
        if (!takeLag || int(shots.size()) < FIT_SHOTS) return shots.clear();
        // (the pair fitted is the one before the pair just seen; where the screen found it locked)
        const SideModel& m = f.model[me];
        const int placed = m.followed == pairsSeen - 2 ? juraic::placementId(m.placed) : -1;
        FitTable table{};
        std::array<std::array<bool, 2>, 2 * FIT_REACH + 1> lockedThere{};
        int bestHere = 0, bestHereScore = -1;
        for (int d = -FIT_REACH; d <= FIT_REACH; ++d)
            for (int flip = 0; flip < 2; ++flip) {
                const int by = lag + d;
                const Fit score = by < 0 ? Fit{} : fitScore(f, by, turnsSwapped != (flip != 0), placed);
                if (score.pictures < 0) return shots.clear(); // (a pair of one colour tells nothing of the turns)
                table[size_t(d + FIT_REACH)][size_t(flip)] = score.pictures;
                lockedThere[size_t(d + FIT_REACH)][size_t(flip)] = score.placed;
                if (flip == 0 && (score.pictures > bestHereScore || (score.pictures == bestHereScore && std::abs(d) < std::abs(bestHere))))
                    bestHere = d, bestHereScore = score.pictures;
            }
        const int64_t at = f.tracker.frame;
        const size_t here = size_t(bestHere + FIT_REACH);
        note(at, "lag measured: " + std::to_string(lag + bestHere) + " frames (pictures of pair " + std::to_string(pairsSeen - 1) + ": " + std::to_string(bestHereScore) +
                     " of " + std::to_string(shots.size()) + " right, " + std::to_string(table[size_t(FIT_REACH)][0]) + " with the lag " + std::to_string(lag) +
                     "; the turns the other way round " + std::to_string(table[here][1]) + (placed >= 0 ? std::string(", locked where found: ") + (lockedThere[here][0] ? "as sent" : "-") +
                     (lockedThere[here][1] ? " other way" : "") : std::string()) + ")");
        lags.push_back(lag + bestHere);
        if (lags.size() > 15) lags.erase(lags.begin());
        shots.clear();
        // The way of the turns, at the lag that fits this pair best.
        const int told = table[here][1] - table[here][0] + PLACED_WEIGHT * (int(lockedThere[here][1]) - int(lockedThere[here][0]));
        bool high = false; // (the engine had the axis in the 13th row or above)
        for (const Past& q : past) high = high || (q.state.phase == juraic::Phase::Control && q.state.pair.y >= 12);
        turnsOther += high ? std::clamp(told, -TURN_STEP, TURN_STEP) : told;
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

    // The picture `seen` (pair colours `c`) of the frame g of the game shows the pair where the engine had it: its row as
    // in g, its column as in g or MOVE_SHOWN frames before, the way of its child as in g or up to TURN_LATEST frames
    // before.
    bool shows(int64_t g, const juraic::PairControl& seen, juraic::Pair c) const {
        const auto at = [&](int64_t frame) -> const juraic::PairControl* {
            const size_t i = pastIndex(frame);
            return i < past.size() && past[i].state.phase == juraic::Phase::Control ? &past[i].state.pair : nullptr;
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
        if (i == past.size() || past[i].state.phase != juraic::Phase::Control) return;
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
        if (shownAt >= 0 && t.frame - shownAt <= DOUBT_FRAMES) {
            const bool twice = doubtAt >= 0 && t.frame - doubtAt <= DOUBT_FRAMES && alike(doubt, seen, c, int(t.frame - doubtAt));
            doubtAt = t.frame, doubt = seen;
            if (!twice) return;
        }
        doubtAt = -1;
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
            stop(), following = true, untilLocked = true, lockedAt = t.frame;
            return;
        }
        steer(f);
    }
};

} // namespace play::realgame
