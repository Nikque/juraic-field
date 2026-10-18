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
    // The fit: lags from FIT_REACH less to FIT_REACH more than the driver's are tried, a pair with fewer than FIT_SHOTS
    // clear pictures is not counted; the pictures of the last FIT_PAIRS pairs are added up, and a lag is taken when its
    // engine shows FIT_MARGIN pictures more right than the driver's. The way of the turns, which does not change, is told
    // by every pair since the driver began (the pair is not to be read while it turns, and is soon locked after: a picture
    // or two of a pair tell of it, its records): the pictures more right with the turns the other way round, and
    // PLACED_WEIGHT for each pair locked where only that way brings it; at TURN_MARGIN the turns go the other way round
    // (the records played again: at 2 the turns of one record went to and fro seven times, at 3 and 4 never).
    static constexpr int FIT_REACH = 3, FIT_SHOTS = 4, FIT_PAIRS = 3, FIT_MARGIN = 4, PLACED_WEIGHT = 4, TURN_MARGIN = 3;
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
    static void prepare(juraic::ai::Agent* agent) {
        if (auto* placing = dynamic_cast<juraic::ai::PlacementAgent*>(agent)) placing->holdSecond = true, placing->tapHold = 2;
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
        const auto* placing = dynamic_cast<const juraic::ai::PlacementAgent*>(agent.get());
        return active && placing ? placing->targetPlacement() : -1;
    }

    // In each frame of the clock `now`, after the readings of the screen up to it are in `f`: the buttons to hold on
    // the pad from now on (the game takes them in its frame now + lag).
    uint8_t tick(const Follower& f, int64_t now) {
        const Tracker& t = f.tracker;
        const SideTrack& side = t.side[me];
        if (nextAgent && !active) {
            if (agent) std::thread([before = std::move(agent)]() mutable { before.reset(); }).detach();
            agent = std::move(nextAgent);
            agent->startRound(me, seed++);
            noted = -2;
        }
        if (!agent) return held = 0;
        const int counted = side.pairs + side.unseen; // (with the pairs the window tells were put unseen)
        if (t.rounds != round || counted < pairsSeen - (blindFor >= 0 ? 1 : 0)) {
            if (t.rounds != round) {
                following = untilLocked = false;
                windowReadAt = windowPauseAt = -1;
                own = juraic::Player{};
                past.clear();
            }
            round = t.rounds, pairsSeen = 0, blindFor = -1;
            agent->startRound(me, seed++);
            stop();
            shots.clear(); // (a pair of the round before is not fitted: its pictures may be cut short)
        }
        if (side.pairs > pairsSeen) {
            pairsSeen = side.pairs, blindFor = -1;
            begin(f, side);
        } else if (blindFor >= 0 && side.pairs == blindFor && t.spawned[me]) {
            // (the screen shows the pair begun unseen: taken from it if it came at another time than foreseen, or the
            //  engine has put it already)
            blindFor = -1;
            if (!active || std::abs(side.spawnedAt - blindAt) > BLIND_SLACK) begin(f, side);
        } else if (blindFor >= 0 && side.unseen > 0 && counted >= blindFor) {
            // (the window tells the pair begun unseen has been put, the screen never showing it: the next one may be
            //  begun unseen too - kept waiting for the screen to show this one, the next pair under the same light, 80
            //  frames long, was left alone 11 times in a round of 34 pairs drawn so)
            blindFor = -1;
        }
        keepShot(f);
        if (side.nextRead[0] && side.nextRead[1] && side.nextStill >= t.windowStill) windowReadAt = t.frame;
        if (!active) {
            wait(f, now);
            if (blindDue(f, now)) beginBlind(f, now);
        }
        if (!active) {
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
            if (following && side.pairs == pairsSeen && side.inHand && side.clear && side.pair.seen && !pairLying(side.pair, side.under)) untilLocked = true;
            if (untilLocked && (side.pairs == pairsSeen || unseenInAir) && !lies(f)) {
                // The window starts moving before the next pair appears. Release while it moves: waiting for it to
                // stand still sent the old down button to 26 next pairs in 16 rounds with lights of 30 to 120 frames.
                // An unread window alone cannot prove a lock. Keep untilLocked, and resume after nextSlide frames if
                // neither the field nor the window has confirmed it (a window hidden for longer must not leave a pair).
                if (!side.nextRead[0] && !side.nextRead[1] && windowReadAt > startNext && windowReadAt > windowPauseAt)
                    windowPauseAt = t.frame;
                if (windowPauseAt >= startNext && now < windowPauseAt + t.timing.nextSlide) return press(now, held = 0);
                return press(now, held = juraic::BTN_DOWN);
            }
            untilLocked = false;
            return press(now, held = 0);
        }
        correct(f);
        if (notes) {
            // (a placement that takes the place of another: the AI's second choice, or the nearest one when no way led
            //  to the one before any more)
            const int aim = target();
            const auto* placing = dynamic_cast<const juraic::ai::PlacementAgent*>(agent.get());
            if (aim != noted) {
                std::string why;
                if (placing && noted >= 0 && aim >= 0)
                    why = placing->steered != steeredSeen ? " (no way to the one before: the nearest)" : placing->againSwitched != againSeen ? " (the second choice)" : "";
                noted = aim, note(now, "2P pair " + std::to_string(pairsSeen) + ": brought to " + placementText(aim) + why);
            }
            if (placing) steeredSeen = placing->steered, againSeen = placing->againSwitched;
        }
        const int64_t at = now + lag;
        // (frames of the game whose buttons were sent before: they took what was held. A frame the clock went past without
        //  a call - the thread of the play a frame late - had the agent's buttons chosen for it and not sent: the way to
        //  the placement is planned again from where the engine has the pair, so that a move chosen for it is not lost)
        bool same = true;
        while (active && next < at) same = play(f, false) && same;
        if (!same && active) steer(f);
        if (active && next == at) play(f, true);
        return press(now, held);
    }

private:
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
    int64_t checked = -1;
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
    int64_t windowReadAt = -1, windowPauseAt = -1; // last stable window, and the bounded release when it became unread
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
    // The buttons the pad held in the frame `at` of the clock (0 before any).
    uint8_t padAt(int64_t at) const {
        for (size_t i = pads.size(); i-- > 0;)
            if (pads[i].first <= at) return pads[i].second;
        return 0;
    }

    void note(int64_t at, const std::string& line) {
        if (notes) notes->emplace_back(at, line);
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
        static const char* turns[] = {"up", "right", "down", "left"};
        const juraic::Placement p = juraic::placement(id);
        return "column " + std::to_string(p.x + 1) + ", child " + turns[p.dir];
    }
    static std::string controlText(const juraic::PairControl& p) {
        static const char* turns[] = {"up", "right", "down", "left"};
        return "column " + std::to_string(p.x + 1) + " row " + std::to_string(p.y + 1) + (p.upper ? " (half up)" : "") + " child " + turns[p.dir];
    }

    int64_t roundFrame(const Tracker& t, int64_t at) const { return t.roundStart >= 0 && at >= t.roundStart ? at - t.roundStart : 0; }

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
        tsumo = juraic::Tsumo{};
        for (size_t i = 0; i < t.pairsLearnt(); ++i) {
            const juraic::Pair p = t.knows(me, int(i)) ? t.pairOf(me, int(i)) : juraic::Pair{1, 1};
            tsumo.piece[(i % juraic::TSUMO_PAIRS) * 2] = p.axis, tsumo.piece[(i % juraic::TSUMO_PAIRS) * 2 + 1] = p.child;
        }
        // (seen late, it was in play already in the frame of the picture: it is taken as appearing in that frame)
        next = side.spawnedAt + (side.late ? 0 : 1);
        startOwn = own, startNext = next;
        past.clear();
        checked = -1;
        active = true, following = false, untilLocked = false;
        ++played;
        noted = -2;
        note(t.frame, "2P pair " + std::to_string(side.pairs) + " begun" + (side.late ? " (seen late)" : "") + ": seen in frame " +
                          std::to_string(side.spawnedAt) + ", the engine from frame " + std::to_string(next) + ", lag " + std::to_string(lag));
        bool same = true;
        // (the frames from the picture to now have passed: the agent decides them, the game took what was held)
        const int64_t at = t.frame + lag; // (at least the frames the picture is behind)
        while (active && next < at) same = play(f, false) && same;
        if (!same) steer(f);
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
    int64_t appeared(const Tracker& t) const {
        const SideTrack& side = t.side[me];
        if (!following || own.phase == juraic::Phase::Control || side.shiftedAt <= side.spawnedAt + t.pairGap) return -1;
        const int64_t shown = side.shiftedAt + 1;
        return own.phase == juraic::Phase::Next && own.wait <= 2 ? std::clamp(next + own.wait - 2, shown, shown + 1) : shown;
    }
    // The next pair has come and the screen does not show it: BLIND_AFTER frames after it appeared, the screen of the own
    // side not read (a light over it). On the real game the screen of 2P read nothing for 30 frames as a pair appeared,
    // and nothing played the pair until the screen showed it.
    bool blindDue(const Follower& f, int64_t now) const {
        const Tracker& t = f.tracker;
        const SideTrack& side = t.side[me];
        const int64_t appears = appeared(t);
        return pairsSeen > 0 && blindFor < 0 && side.pairs + side.unseen == pairsSeen && appears >= 0 && now >= appears + BLIND_AFTER && !side.clear;
    }
    // The next pair, begun unseen, of the colours learnt: the engine goes on from where it stopped (a chain it still ran
    // run to its end at once), the pair appearing in the frame it came; the screen puts it right when it shows it.
    void beginBlind(const Follower& f, int64_t now) {
        const Tracker& t = f.tracker;
        const int64_t appears = appeared(t);
        // (as wait: the field dead only as the pair would appear)
        for (int k = 0; k < 600 && own.phase != juraic::Phase::Next && own.phase != juraic::Phase::Control && own.phase != juraic::Phase::Dead; ++k) {
            juraic::Incoming nobody;
            juraic::PlayerEvents e = juraic::advancePlayer(own, nobody, 0, tsumo, rules, t.timing, roundFrame(t, next));
            juraic::settlePlayer(own, rules, t.timing, noGarbage, e);
        }
        if (own.phase != juraic::Phase::Next) return;
        own.wait = 1;
        next = appears + 1;
        pairsSeen = t.side[me].pairs + t.side[me].unseen + 1;
        blindFor = pairsSeen, blindAt = appears;
        startOwn = own, startNext = next;
        past.clear();
        checked = -1;
        active = true, following = false, untilLocked = false;
        ++played;
        noted = -2;
        note(f.tracker.frame, "2P pair " + std::to_string(pairsSeen) + " begun unseen: the screen not read, the engine had it appear in frame " + std::to_string(appears) +
                                  ", lag " + std::to_string(lag));
        bool same = true;
        const int64_t at = now + lag;
        while (active && next < at) same = play(f, false) && same;
        if (!same) steer(f);
    }

    // One frame of the game: the agent decides it (its buttons are sent), or it took what was held. False when the
    // agent decided other buttons than those the frame took.
    bool play(const Follower& f, bool send) {
        juraic::ai::Observation o;
        f.observeAt(me, rules, own, next, o);
        const auto* placing = dynamic_cast<const juraic::ai::PlacementAgent*>(agent.get());
        const int64_t revisions = placing ? placing->revisedChoices : 0;
        const uint8_t wanted = uint8_t(agent->frame(o) & ~juraic::BTN_DROP);
        if (placing && placing->revisedChoices != revisions)
            note(next, "2P pair " + std::to_string(pairsSeen) + " choice renewed: the field or known pairs changed");
        if (send) held = wanted;
        juraic::Incoming nobody;
        juraic::advancePlayer(own, nobody, held, tsumo, rules, f.tracker.timing, roundFrame(f.tracker, next));
        past.push_back({next, own, held});
        if (past.size() > 240) past.pop_front();
        ++next;
        if (own.phase != juraic::Phase::Next && own.phase != juraic::Phase::Control) stop(), following = true, untilLocked = true;
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
                juraic::PlayerEvents e = juraic::advancePlayer(own, nobody, 0, tsumo, rules, t.timing, roundFrame(t, next));
                juraic::settlePlayer(own, rules, t.timing, noGarbage, e); // (the garbage it does not know of: none)
                ++next;
            }
            if (own.phase == juraic::Phase::Next || own.phase == juraic::Phase::Control) {
                if (own.phase == juraic::Phase::Control) following = false; // (it appeared in the engine after all: as below)
                else {
                    juraic::ai::Observation o;
                    f.observeAt(me, rules, own, now, o);
                    agent->frame(o);
                    return;
                }
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
    bool lies(const Follower& f) const {
        const SideModel& m = f.model[me];
        if (m.lying && m.index == pairsSeen - 1) return true;
        return following && own.phase == juraic::Phase::Next && own.wait <= 2;
    }

    void steer(const Follower& f) {
        if (auto* placing = dynamic_cast<juraic::ai::PlacementAgent*>(agent.get())) {
            juraic::ai::Observation o;
            f.observeAt(me, rules, own, next, o);
            placing->steer(o);
        }
    }

    // A clear picture of the pair in play, kept for the fit.
    void keepShot(const Follower& f) {
        const SideTrack& s = f.tracker.side[me];
        if (pairsSeen <= 0 || s.pairs != pairsSeen || !s.inHand || !s.pair.seen || !s.clear) return;
        if (!shots.empty() && shots.back().frame >= f.tracker.frame) return;
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
            const juraic::PlayerEvents e = juraic::advancePlayer(p, nobody, b, tsumo, rules, t.timing, roundFrame(t, g));
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
        turnsOther += table[here][1] - table[here][0] + PLACED_WEIGHT * (int(lockedThere[here][1]) - int(lockedThere[here][0]));
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
            for (size_t i = past.size(); i-- > 0;)
                if (past[i].frame == frame) return past[i].state.phase == juraic::Phase::Control ? &past[i].state.pair : nullptr;
            return nullptr;
        };
        const juraic::PairControl* row = at(g);
        if (!row) return false;
        if (samePlace(*row, seen, c)) return true;
        if (row->y != seen.y || row->upper != seen.upper) return false;
        bool column = false;
        for (int k = 0; k <= MOVE_SHOWN && !column; ++k)
            if (const juraic::PairControl* p = at(g - k)) column = p->x == seen.x;
        if (!column) return false;
        // (the way: as the engine had it from TURN_LATEST frames before on, and back through turns that came one after the
        //  other into that time, each within TURN_RUN frames of the next - the way before the first of them included: the
        //  real game shows such turns one after the other)
        // (taps held longer - PlacementAgent::tapHold - space the turns of a way so much more)
        const auto* placing = dynamic_cast<const juraic::ai::PlacementAgent*>(agent.get());
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

    // A clear picture of the pair against the engine in the frame of the picture (or up to two frames either side: the
    // pad is a frame early or late now and then, 7% of the frames of a record of the real game, and the pair put right
    // for it had the buttons in flight played again for the wrong frames, sent to and fro).
    void correct(const Follower& f) {
        const Tracker& t = f.tracker;
        const SideTrack& side = t.side[me];
        if (!side.pair.seen || !side.clear || t.frame <= checked) return;
        checked = t.frame;
        size_t i = 0;
        while (i < past.size() && past[i].frame != t.frame) ++i;
        if (i == past.size() || past[i].state.phase != juraic::Phase::Control) return;
        juraic::PairControl& had = past[i].state.pair;
        const juraic::PairControl& seen = side.pair.at;
        const juraic::Pair c = past[i].state.colors;
        for (const int by : {0, 1, -1, 2, -2})
            if (shows(t.frame + by, seen, c)) return;
        ++corrected;
        note(t.frame, "2P pair " + std::to_string(pairsSeen) + " put right: the engine had " + controlText(had) + ", the picture shows " + controlText(seen));
        had.x = seen.x, had.y = seen.y, had.dir = seen.dir, had.upper = seen.upper;
        had.grounded = false, had.moveWait = 0;
        // (the frames since played again with the buttons they took)
        own = past[i].state;
        for (size_t k = i + 1; k < past.size(); ++k) {
            juraic::Incoming nobody;
            juraic::advancePlayer(own, nobody, past[k].buttons, tsumo, rules, t.timing, roundFrame(t, past[k].frame));
            past[k].state = own;
        }
        if (own.phase != juraic::Phase::Control) {
            // (put right, the pair has come to rest in the engine: held down as after any pair the engine has put)
            stop(), following = true, untilLocked = true;
            return;
        }
        steer(f);
    }
};

} // namespace play::realgame
