#pragma once
// The mode in which Juraic plays 2P of the real game (play/realgame_win.h): the AI plays one side of the real game
// through the buttons of a pad (play/realgame_pad_win.h on Windows), frame after frame. Plain C++, tested on every system
// (tests/realgame_tests.cpp, against rounds of this program drawn, read and played through a pad that is late).
//
// The own pair is followed with the engine (game/player.h): from the frame its new pair is seen (Tracker), as the game
// moves it with the buttons sent, each reaching the game `lag` frames after it was sent. Every clear picture of the pair
// is checked against where the engine had it in the frame of the picture: when it is elsewhere (a button that came
// late or early, or was not taken) the engine's pair is put where the picture shows it, the frames since are played
// again with the buttons sent for them, and the way to the placement chosen is planned again from there
// (ai::PlacementAgent::steer). Once the pair is locked nothing is pressed until the next pair is seen; the AI is still
// shown every frame (an AI of a lower level sees the other side as it was some frames before: ai/pace.h).
//
// The lag is measured too: from the frame a move to the side was first sent for a pair to the first picture that
// shows the pair moved (Driver::measuredLag, the middle of the last measurements).
#include "../ai/agent.h"
#include "realgame_follow.h"
#include <algorithm>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace play::realgame {

class Driver {
public:
    int me = 1;                               // the seat played
    // Frames from a button sent to the picture of the game that shows it taken. The driver begins with it and takes the
    // lag it measures (measuredLag) from the pair after LAGS_TAKEN measurements on (takeLag).
    int lag = 2;
    bool takeLag = true;
    static constexpr int LAGS_TAKEN = 5;
    juraic::Rules rules;                      // the real game's
    std::unique_ptr<juraic::ai::Agent> agent; // a PlacementAgent
    uint64_t seed = 1;                        // of the agent's rounds
    int played = 0;                           // pairs played
    int corrected = 0;                        // pictures that showed the pair elsewhere than the engine had it
    std::vector<int> lags;                    // the lags measured, the last first ones dropped (at most 15)
    // The buttons of the two turns are sent the other way round: found from the screen, when a pair it turned shows the
    // way opposite to the engine's TURNS_OPPOSITE pictures in a row (a turn the game took the other way round).
    bool turnsSwapped = false;
    static constexpr int TURNS_OPPOSITE = 2;
    // When set: what the driver does, as lines with the frame of the clock they are of (the record of a reading,
    // play/realgame_log.h) - each pair begun, the placement it is brought to, a pair put where the picture shows it,
    // a lag measured.
    std::vector<std::pair<int64_t, std::string>>* notes = nullptr;

    // The middle of the lags measured; -1: none yet.
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
    // The placement the pair in play is brought to (-1: none).
    int target() const {
        const auto* placing = dynamic_cast<const juraic::ai::PlacementAgent*>(agent.get());
        return active && placing ? placing->targetPlacement() : -1;
    }

    // In each frame of the clock `now`, after the readings of the screen up to it are in `f`: the buttons to hold on
    // the pad from now on (they reach the frame now + lag of the game).
    uint8_t tick(const Follower& f, int64_t now) {
        const Tracker& t = f.tracker;
        const SideTrack& side = t.side[me];
        if (!agent) return held = 0;
        if (t.rounds != round || side.pairs < pairsSeen) {
            round = t.rounds, pairsSeen = 0;
            agent->startRound(me, seed++);
            stop();
        }
        if (side.pairs > pairsSeen) {
            pairsSeen = side.pairs;
            begin(f, side);
        }
        measure(f);
        if (!active) {
            wait(f, now);
            if (untilLocked && side.inHand && side.pairs == pairsSeen) return held = juraic::BTN_DOWN;
            untilLocked = false;
            return held = 0;
        }
        correct(f);
        if (notes) {
            const int aim = target();
            if (aim != noted) noted = aim, note(now, "2P pair " + std::to_string(pairsSeen) + ": brought to " + placementText(aim));
        }
        const int64_t at = now + lag;
        // (frames of the game whose buttons were sent before: they took what was held)
        while (active && next < at) play(f, false);
        if (active && next == at) {
            play(f, true);
            if ((held & (juraic::BTN_LEFT | juraic::BTN_RIGHT)) && movedAt < 0) movedAt = now, movedFrom = spawnX;
        }
        return sent(held);
    }

private:
    struct Past {
        int64_t frame;        // the state after this frame of the game,
        juraic::Player state; // ... and the buttons it took
        uint8_t buttons;
    };
    juraic::Player own;
    juraic::Tsumo tsumo;
    int64_t next = 0; // the frame of the game the engine plays next
    std::deque<Past> past;
    bool active = false;
    uint8_t held = 0;
    int round = -1, pairsSeen = 0;
    int64_t checked = -1;
    int64_t movedAt = -1; // the frame of the clock a move to the side was first sent for the pair in play (-1: none)
    int movedFrom = 0;    // ... and the column the pair was then seen in
    int spawnX = juraic::SPAWN_X;
    bool measured = true;
    int noted = -2; // the placement last noted (notes)
    // A picture that shows the pair where the engine has it a frame or two later (or earlier): the lag is that many
    // frames less (more) than the driver's. Seen LAG_HINTS pictures in a row, the lag and the frames of the engine are
    // moved by it at once (nothing is put right for it: putting the pair where the picture shows it and playing the
    // frames since again made the buttons in flight count twice - seen on the real game, begun with 6 for 5).
    static constexpr int LAG_HINTS = 2;
    int lagHint = 0, lagHints = 0;
    // The real game shows a turn some frames after it takes it (the pair the way it was, or not to be read, meanwhile:
    // seen in its record, a move with it shown at once): a picture of the pair where the engine has it, turned the way
    // the engine had it up to TURN_SHOWN frames before, puts nothing right.
    static constexpr int TURN_SHOWN = 3;
    int opposite = 0; // pictures in a row that showed a pair turned the opposite way of the engine's
    // After its pair is locked the engine goes on without buttons (its chain, then the wait for the next pair), so that
    // the AI is shown when the next pair comes and chooses before it appears, as in a match of this program - not as it
    // appears, the pair waiting for the choice (long on a machine under load). It stops two frames before the pair it
    // foresees (the pair may come later: garbage the engine does not know of falls first).
    bool following = false;
    // The engine has the pair locked: until the screen shows it so (the pair no longer in hand) the down button is held.
    // The engine ahead of the game, or its pictures not to be read just then, the pair let go of it fell on slowly by
    // itself (seen on the real game: 100 frames for one pair; on a pad that is a frame early or late now and then, 47
    // pairs of 57 were brought where the AI chose without it, 57 of 63 with it).
    bool untilLocked = false;
    juraic::Rng noGarbage{1, 1}; // (settlePlayer lets garbage fall by it: there is none in the engine)

public:
    // What a picture of a pair the engine had turned (`had`, not UP) tells of the way the game turns: 1 as the engine
    // turned it, -1 the opposite way - its axis where the engine had it, the child on the other side -, 0 nothing (a pair
    // found elsewhere as well is a button taken late or early, not a turn the other way: seen so on the real game).
    static int turnShown(const juraic::PairControl& had, const juraic::PairControl& seen) {
        const bool sameAxis = had.x == seen.x && had.y == seen.y && had.upper == seen.upper;
        if (!sameAxis) return 0;
        return seen.dir == had.dir ? 1 : seen.dir == (had.dir + 2) % 4 ? -1 : 0;
    }

private:
    // The buttons held as they go to the pad: the two turns the other way round when the game turns so.
    uint8_t sent(uint8_t b) const {
        if (!turnsSwapped) return b;
        return uint8_t((b & ~(juraic::BTN_CW | juraic::BTN_CCW)) | (b & juraic::BTN_CW ? juraic::BTN_CCW : 0) | (b & juraic::BTN_CCW ? juraic::BTN_CW : 0));
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
        own = juraic::Player{};
        own.field = f.model[me].known ? f.model[me].core.field : side.under; // (with the 14th row the moves followed put there)
        own.phase = juraic::Phase::Next;
        own.wait = 1;
        own.nextPair = own.moves = side.pairs - 1;
        if (f.model[me].known) own.scoring = f.model[me].core.scoring;
        tsumo = juraic::Tsumo{};
        for (size_t i = 0; i < t.pairs.size(); ++i) {
            const juraic::Pair p = t.known[i] ? t.pairs[i] : juraic::Pair{1, 1};
            tsumo.piece[(i % juraic::TSUMO_PAIRS) * 2] = p.axis, tsumo.piece[(i % juraic::TSUMO_PAIRS) * 2 + 1] = p.child;
        }
        if (takeLag && int(lags.size()) >= LAGS_TAKEN && measuredLag() != lag) {
            note(t.frame, "the lag taken: " + std::to_string(measuredLag()) + " frames (was " + std::to_string(lag) + ")");
            lag = measuredLag();
        }
        // (seen late, it was in play already in the frame of the picture: it is taken as appearing in that frame)
        next = side.spawnedAt + (side.late ? 0 : 1);
        past.clear();
        checked = -1;
        movedAt = -1, measured = side.late, spawnX = side.lastSeen.at.x;
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

    // One frame of the game: the agent decides it (its buttons are sent), or it took what was held. False when the
    // agent decided other buttons than those the frame took.
    bool play(const Follower& f, bool send) {
        juraic::ai::Observation o;
        f.observeAt(me, rules, own, next, o);
        const uint8_t wanted = uint8_t(agent->frame(o) & ~juraic::BTN_DROP);
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
            while (next < at && !(own.phase == juraic::Phase::Next && own.wait <= 2) && !own.field.dead()) {
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

    // The first picture that shows the pair moved to the side after a move was sent: the lag (frames of the clock).
    void measure(const Follower& f) {
        const Tracker& t = f.tracker;
        const SideTrack& side = t.side[me];
        if (measured || movedAt < 0 || !side.pair.seen || t.frame <= movedAt) return;
        if (side.pair.at.x == movedFrom) return;
        measured = true;
        lags.push_back(int(t.frame - movedAt));
        note(t.frame, "lag measured: " + std::to_string(t.frame - movedAt) + " frames");
        if (lags.size() > 15) lags.erase(lags.begin());
    }

    void steer(const Follower& f) {
        if (auto* placing = dynamic_cast<juraic::ai::PlacementAgent*>(agent.get())) {
            juraic::ai::Observation o;
            f.observeAt(me, rules, own, next, o);
            placing->steer(o);
        }
    }

    // A clear picture of the pair against the engine in the frame of the picture.
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
        const bool sameCells = samePlace(had, seen, c);
        // (a pair of two colours turned: the way it shows against the engine's)
        if (c.axis != c.child && had.dir != juraic::UP) {
            const int way = turnShown(had, seen);
            if (way > 0) opposite = 0;
            else if (way < 0 && ++opposite >= TURNS_OPPOSITE) {
                turnsSwapped = !turnsSwapped, opposite = 0;
                note(t.frame, std::string("the turns are taken the other way round: the buttons of the two turns are now sent ") + (turnsSwapped ? "swapped" : "as they are"));
            }
        }
        if (sameCells) {
            lagHint = lagHints = 0;
            return;
        }
        // (a turn not shown yet)
        if (had.x == seen.x && had.y == seen.y && had.upper == seen.upper)
            for (size_t j = i; j-- > 0 && past[j].frame >= t.frame - TURN_SHOWN;)
                if (past[j].state.phase == juraic::Phase::Control && past[j].state.pair.dir == seen.dir) return;
        // (a pair where the engine has it a frame or two later or earlier is put nowhere else: the lag of the game is off
        //  by a frame or two now and then - on the real game 4 to 6 frames - and putting it right played the buttons in
        //  flight again for the wrong frames, the pair sent to and fro: with the lag 2 frames off, 125 times in 27 pairs.
        //  Only a pair in another column tells of the lag: the real game shows a turn late by itself, and begins the fall
        //  of a pair just appeared a frame or two sooner than the engine - its record: a pair at another height alone was
        //  taken for a lag 2 frames less, and the lag fell from 6 to 3)
        for (const int by : {1, -1, 2, -2}) {
            size_t j = 0;
            while (j < past.size() && past[j].frame != t.frame + by) ++j;
            if (j == past.size() || past[j].state.phase != juraic::Phase::Control || !samePlace(past[j].state.pair, seen, c)) continue;
            if (had.x == seen.x || !takeLag) return;
            lagHints = by == lagHint ? lagHints + 1 : 1, lagHint = by;
            if (lagHints >= LAG_HINTS && lag - by >= 0) {
                note(t.frame, "the lag taken from the pictures: " + std::to_string(lag - by) + " frames (was " + std::to_string(lag) + ")");
                lag -= by, next -= by;
                for (Past& p : past) p.frame -= by;
                lagHint = lagHints = 0;
                lags.clear(); // (measured again from now on: the lags before were measured against the other one)
            }
            return;
        }
        lagHint = lagHints = 0;
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
        if (own.phase != juraic::Phase::Control) return stop();
        steer(f);
    }
};

} // namespace play::realgame
