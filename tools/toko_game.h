#pragma once
// One game of toko played by the beam search (ai/build.h), for the tools
// that measure it (ai_toko) and tune its weights (ai_tune).
#include "../ai/build.h"
#include "../game/tsumo.h"
#include "../game/turn.h"
#include <chrono>

namespace juraic::toko {

constexpr int CHECKPOINTS = 10; // the largest chain a trial drop starts, after 5, 10, ... 50 moves

// How a game is played: the search is given the next `known` pairs; the game
// ends with the first chain of `goal` points or more, when the player loses,
// or after `moves` moves.
struct Run {
    const Rules* rules = nullptr;
    const ai::Params* params = nullptr;
    int known = 2, goal = 78000, moves = 100;
};

struct GameResult {
    int moves = 0;       // pairs placed
    int points = 0;      // the chain that ended the game (0 = failed)
    int largest = 0;     // the largest chain of the game
    int64_t frames = 0;  // from the first pair to the appearance of the pair that fired
    int64_t nodes = 0;
    int64_t ns = 0, worstNs = 0;
    bool dead = false;
    int potential[CHECKPOINTS]{}; // -1 = the game was over by then
    // a game replayed from another AI's placements only (ai_toko --replay)
    bool illegal = false; // a placement this engine does not allow (the game stops there)
    int theirPoints = 0;  // the last chain as the other AI counted it
    double theirMs = 0;
};

// The largest chain a trial drop starts now, after every fifth move.
inline void checkpoint(const Run& run, const PlayerCore& me, GameResult& g) {
    if (g.moves % 5 || g.moves / 5 > CHECKPOINTS) return;
    ai::Trials trials;
    ai::detect(me.field, *run.rules, DEFAULT_TIMING, run.params->trialMaxAdd, trials);
    g.potential[g.moves / 5 - 1] = trials.best() ? trials.best()->points : 0;
}

// One move of a game: the pair is put and the numbers of the game follow. False when the game is over with it
// (the goal is reached, or the field is lost).
inline bool playMove(const Run& run, PlayerCore& me, Incoming& nobody, Placement at, Pair pair, int64_t& frame, Rng& garbage, GameResult& g) {
    const int64_t before = frame;
    const TurnResult turn = playTurn(me, nobody, at, pair, *run.rules, DEFAULT_TIMING, frame, garbage);
    frame += turn.frames;
    ++g.moves;
    g.largest = std::max(g.largest, turn.points);
    if (turn.points >= run.goal) {
        g.points = turn.points;
        g.frames = before;
        return false;
    }
    if (turn.dead) {
        g.dead = true;
        return false;
    }
    checkpoint(run, me, g);
    return true;
}

inline GameResult playGame(const Run& run, uint64_t seed, ai::Builder& builder) {
    using Clock = std::chrono::steady_clock;
    const Rules& rules = *run.rules;
    const Timing& timing = DEFAULT_TIMING;
    const Tsumo tsumo = makeTsumo(rules, seed);
    PlayerCore me;
    Incoming nobody;
    Rng garbage(seed, 1);
    GameResult g;
    for (int& p : g.potential) p = -1;
    int64_t frame = 0;
    for (int move = 0; move < run.moves; ++move) {
        Pair known[8];
        for (int i = 0; i < run.known; ++i) known[i] = tsumo.pair(move + i);
        const auto t0 = Clock::now();
        const ai::BuildResult r = builder.search(me.field, known, run.known, rules, timing, *run.params, seed * 1000 + uint64_t(move), 1);
        const int64_t t = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
        g.ns += t, g.worstNs = std::max(g.worstNs, t), g.nodes += r.nodes;
        if (r.best < 0) {
            g.dead = true;
            break;
        }
        if (!playMove(run, me, nobody, placement(r.best), known[0], frame, garbage, g)) break;
    }
    if (!g.points) g.frames = frame;
    return g;
}

// One number for a game, for tuning: large and early is good, failing is bad.
// The average over the checkpoints (after 5, 10, ... 50 moves) of what could be
// fired then, in points and at most `cap`: the largest chain a trial drop
// starts while the game goes on, the chain that ended the game from then on,
// nothing after a game that failed.
inline int objective(const GameResult& g, int cap = 120000) {
    int64_t sum = 0;
    for (int c = 0; c < CHECKPOINTS; ++c) {
        const int v = g.potential[c] >= 0 ? g.potential[c] : g.points;
        sum += v < cap ? v : cap;
    }
    return int(sum / CHECKPOINTS);
}

} // namespace juraic::toko
