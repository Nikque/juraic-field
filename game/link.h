#pragma once
// A round of two players on two machines (a match over a network).
//
// Each machine plays its own seat at once and the other seat as far as that seat's buttons have
// arrived, so the two seats are not at the same frame. What one seat does to the other - garbage -
// therefore takes effect `delay` frames late, and for both seats alike:
//
//   The garbage a wave makes and the end of a chain in frame f are exchanged as in a round on one
//   machine (turn.h: the garbage first offsets what waits above the own field, the rest goes above
//   the opponent's; the end of a chain confirms it), in the order of the frames and seat 0 first
//   within a frame - but the exchange of frame f shows above the fields, and counts for the garbage
//   that falls, from frame f + delay on. A seat can play frame f once the other has played frame
//   f - delay.
//
// Nothing else is shared between the fields, so each seat plays at once and never goes back.
// Because the exchange itself is the one of a single machine, two attacks that cross on the line
// offset each other as they would there: garbage never waits above both fields at once.
// The price: the own offset shows `delay` frames late above the own field too.
//
// The delay must not be longer than the shortest wave (Timing::waveQuick): between a wave and the
// moment its garbage is exchanged, the seat that made it then has no garbage falling, which is what
// lets the exchange be worked out as soon as both seats have played the frame (commit).
//
// Everything follows from the seed, the rules, the delay and the buttons of every frame of each
// seat, in whatever order the seats are played: tests/link_tests.cpp.
// A round on one machine is game/match.h, and is not touched by any of this.
#include "match.h"
#include <deque>

namespace juraic {

constexpr int MAX_LINK_DELAY = 30;

struct LinkedRound {
    Round round;           // rules, tsumo, the two players; frame = the frames exchanged, state = how it ended
    int delay = 1;
    int64_t played[2]{};   // the frames each seat has played
    int64_t diedAt[2]{-1, -1};
    // The garbage above each field as the exchange has left it (the frames below round.frame), without
    // the garbage that has fallen since: `delay` frames ahead of what the seat itself sees
    // (Player::incoming).
    Incoming ahead[2];
    struct Made {
        int64_t frame;
        int garbage; // made by a wave in that frame (before offsetting)
        bool ended;  // the chain ended in that frame
    };
    std::deque<Made> made[2]; // of the frames not exchanged yet
    // What an exchange does above the field of a seat, from frame `at` of that seat on.
    struct Effect {
        int64_t at;
        int add;                         // garbage of the opponent's wave
        int fromPending, fromConfirmed;  // offset by the own wave
        bool confirm;                    // the opponent's chain has ended
    };
    std::deque<Effect> effects[2];

    void start(const Rules& rules, uint64_t seed, int linkDelay, const Timing& t = DEFAULT_TIMING) {
        round.start(rules, seed, 2, t);
        delay = linkDelay < 1 ? 1 : linkDelay > MAX_LINK_DELAY ? MAX_LINK_DELAY : linkDelay;
        for (int i = 0; i < 2; ++i) played[i] = 0, diedAt[i] = -1, ahead[i] = {}, made[i].clear(), effects[i].clear();
    }

    // The seat can play its next frame: the other seat has played far enough, and the round is not over.
    bool canPlay(int seat) const { return round.state == RoundState::Playing && played[seat] < played[1 - seat] + delay; }

    // Plays the next frame of a seat. False (and nothing done) when it cannot be played yet.
    bool play(int seat, uint8_t buttons, PlayerEvents& e) {
        if (!canPlay(seat)) return false;
        Player& p = round.player[seat];
        const int64_t frame = played[seat];
        std::deque<Effect>& due = effects[seat];
        for (; !due.empty() && due.front().at <= frame; due.pop_front()) {
            const Effect& x = due.front();
            p.incoming.pending += x.add - x.fromPending;
            p.incoming.confirmed -= x.fromConfirmed;
            if (x.confirm) p.incoming.confirm();
        }
        // (the frame itself is the one of a single machine, with nothing waiting above the field and nobody on
        // the other side: what a wave makes comes out whole, and is exchanged when both seats have played the frame)
        const Incoming own = p.incoming;
        p.incoming = {};
        Incoming out;
        e = advancePlayer(p, out, buttons, round.tsumoOf(seat), round.rules, round.timing, frame);
        p.incoming = own;
        settlePlayer(p, round.rules, round.timing, round.garbageRng[seat], e);
        ahead[seat].confirmed -= own.confirmed - p.incoming.confirmed; // (what fell)
        if (out.total() || e.chainEnded) made[seat].push_back(Made{frame, out.total(), e.chainEnded});
        if (e.died) diedAt[seat] = frame;
        ++played[seat];
        commit();
        return true;
    }

    // Exchanges the garbage of every frame both seats have played, and ends the round in the frame a seat
    // died (both in the same frame: a draw, as on one machine).
    void commit() {
        while (round.state == RoundState::Playing && round.frame < played[0] && round.frame < played[1]) {
            const int64_t frame = round.frame, at = frame + delay;
            for (int seat = 0; seat < 2; ++seat) {
                if (made[seat].empty() || made[seat].front().frame != frame) continue;
                const Made m = made[seat].front();
                made[seat].pop_front();
                Incoming& mine = ahead[seat];
                Incoming& his = ahead[1 - seat];
                if (m.garbage) {
                    const Incoming before = mine;
                    const int sent = offset(mine, m.garbage);
                    if (sent < m.garbage) effects[seat].push_back(Effect{at, 0, before.pending - mine.pending, before.confirmed - mine.confirmed, false});
                    if (sent) his.pending += sent, effects[1 - seat].push_back(Effect{at, sent, 0, 0, false});
                }
                if (m.ended) his.confirm(), effects[1 - seat].push_back(Effect{at, 0, 0, 0, true});
            }
            ++round.frame;
            const bool dead0 = diedAt[0] == frame, dead1 = diedAt[1] == frame;
            if (dead0 || dead1) round.state = dead0 && dead1 ? RoundState::Draw : dead0 ? RoundState::Won1 : RoundState::Won0;
        }
    }
};

} // namespace juraic
