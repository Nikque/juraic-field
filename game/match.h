#pragma once
// A round of one player (toko) or two players, one frame at a time.
// Everything that happens follows from the seed, the rules and the buttons of
// every frame, so a replay is exactly those three things.
#include "player.h"

namespace juraic {

constexpr uint64_t GARBAGE_STREAM = 0x6F6A616D61; // random streams of the garbage columns

enum class RoundState : uint8_t { Playing, Won0, Won1, Draw };

struct Round {
    Rules rules;
    Timing timing = DEFAULT_TIMING;
    int players = 2;
    uint64_t seed = 0;
    Tsumo tsumo;
    // For measuring only (tools/ai_match --own-tsumo): each player draws from a tsumo of his own, player 1 from
    // tsumo1. The game always plays with the one tsumo; a replay does not hold a second seed.
    bool ownTsumo = false;
    Tsumo tsumo1;
    Rng garbageRng[2]{Rng(0), Rng(0)};
    int64_t frame = 0; // frames played so far
    Player player[2];
    Incoming nobody; // where the garbage of a single player goes
    RoundState state = RoundState::Playing;

    void start(const Rules& r, uint64_t roundSeed, int playerCount = 2, const Timing& t = DEFAULT_TIMING) {
        rules = r, timing = t, players = playerCount, seed = roundSeed;
        tsumo = makeTsumo(rules, seed);
        ownTsumo = false;
        frame = 0;
        nobody = {};
        state = RoundState::Playing;
        for (int i = 0; i < 2; ++i) {
            garbageRng[i] = Rng(seed, GARBAGE_STREAM + uint64_t(i));
            startPlayer(player[i], timing);
        }
    }

    // After start(), before the first frame: player 1 draws from a tsumo of his own, made from `seed1` (ownTsumo).
    void splitTsumo(uint64_t seed1) {
        ownTsumo = true;
        tsumo1 = makeTsumo(rules, seed1);
    }
    const Tsumo& tsumoOf(int i) const { return ownTsumo && i == 1 ? tsumo1 : tsumo; }

    // Plays one frame. buttons[i] = Button bits of player i.
    void step(const uint8_t buttons[2], PlayerEvents events[2]) {
        if (state != RoundState::Playing) return;
        for (int i = 0; i < players; ++i)
            events[i] = advancePlayer(player[i], players == 2 ? player[1 - i].incoming : nobody, buttons[i], tsumoOf(i), rules,
                                      timing, frame);
        for (int i = 0; i < players; ++i) settlePlayer(player[i], rules, timing, garbageRng[i], events[i]);
        ++frame;
        const bool dead0 = player[0].phase == Phase::Dead, dead1 = players == 2 && player[1].phase == Phase::Dead;
        if (dead0 || dead1) state = dead0 && dead1 ? RoundState::Draw : dead0 ? RoundState::Won1 : RoundState::Won0;
    }
};

} // namespace juraic
