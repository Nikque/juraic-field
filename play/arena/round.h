#pragma once
// A round of the arena: a round of two machines (Juraic Field, game/link.h) with two changes that keep
// what is not on the screen away from the players.
//
//   1. The tsumo comes from one seed and the random columns of the garbage from another. A player's
//      machine needs the garbage random numbers to play its own seat at once, and the seed they are
//      made from can be worked back from them (core/rng.h is made of steps that can be undone), so they
//      must not tell the seed of the tsumo.
//   2. A player's machine is not given the seed of the tsumo at all. The server sends the pairs one by
//      one, a little before a screen shows them (serverWants), and a seat waits when its next frame
//      would show a pair that has not arrived (pairsNeeded).
//
// The server plays the same round with the whole tsumo (startFull) and decides the result; a player's
// machine plays it with the pairs that have arrived (startPartial). With the same buttons, both play the
// same frames: tests/arena_tests.cpp.
#include "../../game/link.h"

namespace arena {

using juraic::LinkedRound;
using juraic::Pair;
using juraic::Phase;
using juraic::Player;
using juraic::PlayerEvents;
using juraic::Rules;
using juraic::Timing;

// The pairs of the tsumo a seat's next frame may show or take (numbered as drawn, not folded at 128):
// the window shows nextPair and nextPair + 1, and the pair after them in the last Timing::nextShown
// frames before a pair appears (ai/observe.h shownPairs); the frame in which the pair appears takes
// nextPair. +1: the frame about to be played counts its wait down first.
inline int pairsNeeded(const Player& p, const Timing& t) {
    if (p.phase == Phase::Dead) return 0;
    return p.nextPair + 2 + (p.phase == Phase::Next && p.wait <= t.nextShown + 1 ? 1 : 0);
}

// The pairs the server sends of a seat in this state: those its screen shows while the pair is in the
// player's hands, and one more from the moment the pair has locked (the earliest moment at which the
// frame that shows it is settled; it is shown 11 frames later at the soonest).
inline int serverWants(const Player& p) {
    if (p.phase == Phase::Dead) return 0;
    return p.nextPair + 2 + (p.phase == Phase::Control ? 0 : 1);
}

constexpr int FIRST_PAIRS = 3; // sent before the first frame (the third is shown 4 frames before the first pair)

struct ArenaRound {
    LinkedRound link;
    uint64_t tsumoSeed = 0, garbageSeed = 0; // tsumoSeed: the server only
    bool full = false;                       // the whole tsumo is here (the server)
    int known = 0;                           // the pairs 0 .. known-1 have arrived (a player's machine)
    int64_t waits[2]{};                      // frames a seat could not be played for a pair that had not arrived

    void startFull(const Rules& rules, uint64_t tsumo, uint64_t garbage, int delay) {
        link.start(rules, tsumo, delay);
        tsumoSeed = tsumo, full = true, known = 1 << 30;
        setGarbage(garbage);
    }
    void startPartial(const Rules& rules, uint64_t garbage, int delay) {
        link.start(rules, 0, delay);
        link.round.tsumo = juraic::Tsumo{};
        tsumoSeed = 0, full = false, known = 0;
        setGarbage(garbage);
    }
    // A pair has arrived (pairs come in order; a pair that is not the next one is passed over).
    bool addPair(int index, Pair p) {
        if (full || index != known) return false;
        const size_t i = size_t(index % juraic::TSUMO_PAIRS) * 2;
        link.round.tsumo.piece[i] = p.axis, link.round.tsumo.piece[i + 1] = p.child;
        ++known;
        return true;
    }
    Pair pair(int index) const { return link.round.tsumo.pair(index); }

    bool over() const { return link.round.state != juraic::RoundState::Playing; }
    const Player& player(int seat) const { return link.round.player[seat]; }
    int64_t played(int seat) const { return link.played[seat]; }

    // The seat can play its next frame: the other seat is far enough, the round goes on, and the pairs
    // that frame may show have arrived.
    bool canPlay(int seat) const {
        if (!link.canPlay(seat)) return false;
        return full || known >= pairsNeeded(link.round.player[seat], link.round.timing);
    }
    // Plays the next frame of the seat. False (nothing done) when it cannot be played yet.
    bool play(int seat, uint8_t buttons, PlayerEvents& e) {
        if (!link.canPlay(seat)) return false;
        if (!full && known < pairsNeeded(link.round.player[seat], link.round.timing)) {
            ++waits[seat];
            return false;
        }
        return link.play(seat, buttons, e);
    }

private:
    void setGarbage(uint64_t garbage) {
        garbageSeed = garbage;
        for (int i = 0; i < 2; ++i) link.round.garbageRng[i] = juraic::Rng(garbage, juraic::GARBAGE_STREAM + uint64_t(i));
        waits[0] = waits[1] = 0;
    }
};

} // namespace arena
