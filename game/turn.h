#pragma once
// One move of one player at the level of whole actions: put the pair, run the
// chain, exchange garbage, let garbage fall. No animation and no key input;
// time is taken from the Timing tables. The AI searches with this, and the
// frame-by-frame game uses the same pieces (convert, offset, dropGarbage).
#include "../core/rng.h"
#include "field.h"
#include "timing.h"

namespace juraic {

// Garbage waiting above a field.
struct Incoming {
    int pending = 0;   // sent by a chain that is still running: cannot fall yet
    int confirmed = 0; // the chain has ended: falls after the next move
    int total() const { return pending + confirmed; }
    // The opponent's chain has ended.
    void confirm() {
        confirmed += pending;
        pending = 0;
    }
};

// Score of a player and what has not been turned into garbage yet.
struct Scoring {
    int score = 0;
    int pool = 0;          // points carried over (always less than the rate)
    bool allClear = false; // the bonus is waiting for the next chain
};

// Adds points and returns the garbage pieces they make at this rate.
// The remainder is carried in points. (If the rate drops during margin time the
// carried points are simply divided by the new rate next time.)
inline int convert(Scoring& s, int points, int rate) {
    s.score += points;
    s.pool += points;
    const int n = s.pool / rate;
    s.pool -= n * rate;
    return n;
}

// Offsetting: garbage made by a chain first removes the garbage waiting above
// the own field. Returns what is left to send to the opponent.
// The garbage of a chain that is still running goes first ("warikomi": when
// the opponent attacks again while one is offsetting, the later garbage is
// offset first, so the earlier, confirmed garbage is what falls).
inline int offset(Incoming& own, int garbage) {
    const int a = garbage < own.pending ? garbage : own.pending;
    own.pending -= a;
    garbage -= a;
    const int b = garbage < own.confirmed ? garbage : own.confirmed;
    own.confirmed -= b;
    return garbage - b;
}

// Lets up to rules.maxGarbagePerDrop of the confirmed garbage fall: whole rows
// first, then the rest one each in distinct random columns.
// placed[x] = pieces that stayed in column x (the 13th row is the limit).
// Returns the number taken from `incoming`.
inline int dropGarbage(GameField& g, Incoming& incoming, const Rules& rules, Rng& rng, int placed[W]) {
    const int n = incoming.confirmed < rules.maxGarbagePerDrop ? incoming.confirmed : rules.maxGarbagePerDrop;
    for (int x = 0; x < W; ++x) placed[x] = 0;
    if (n <= 0) return 0;
    incoming.confirmed -= n;
    int column[W] = {0, 1, 2, 3, 4, 5};
    rng.partialShuffle(column, size_t(W), size_t(n % W));
    int count[W];
    for (int x = 0; x < W; ++x) count[x] = n / W;
    for (int i = 0; i < n % W; ++i) ++count[column[i]];
    for (int x = 0; x < W; ++x) placed[x] = addGarbage(g, x, count[x]);
    return n;
}

struct PlayerCore {
    GameField field;
    Scoring scoring;
    Incoming incoming;
};

struct TurnResult {
    PlaceResult placed;
    int chains = 0;
    int points = 0;       // score gained by the chain (all clear bonus included)
    int sent = 0;         // garbage added above the opponent's field
    int offsetted = 0;    // own incoming garbage removed
    int dropped = 0;      // garbage that fell on the own field
    bool allClear = false;
    bool dead = false;
    int frames = 0;       // time of the whole move (fastest input)
    int chainEndFrame = 0; // frames from the start of the move to the end of the chain
};

// ---- a chain, wave by wave ------------------------------------------------------
// The frame-by-frame game calls chainWave / chainEnd itself, one wave at a
// time; resolveChain below runs them in a loop.

struct ChainState {
    int chains = 0;
    int held = 0; // points of the waves below the chain requirement
};

struct WaveReport {
    Wave wave;
    int longestFall = 0;
    int points = 0;    // all clear bonus included
    int sent = 0;      // garbage added above the opponent's field
    int offsetted = 0; // own incoming garbage removed
};

// Clears the next wave, scores it and exchanges garbage at the given rate.
// Returns false when nothing clears.
inline bool chainWave(PlayerCore& me, Incoming& opponent, const Rules& rules, int rate, ChainState& chain,
                      WaveReport& report) {
    BitField& f = me.field.f;
    report = {};
    if (!findWaveFor(f, report.wave, rules.clearCount)) return false;
    ++chain.chains;
    const FieldBits erased = report.wave.cleared | report.wave.garbage;
    report.longestFall = longestFall(f, erased);
    f.erase(erased);
    f.drop();
    report.points = gameWaveScore(report.wave, chain.chains);
    if (me.scoring.allClear) {
        report.points += rules.allClearBonus;
        me.scoring.allClear = false;
    }
    if (chain.chains < rules.chainRequirement) {
        // Below the requirement nothing is sent. The points are held: they
        // count in full once the chain reaches the requirement, and are
        // worth no garbage if it ends before.
        me.scoring.score += report.points;
        chain.held += report.points;
        return true;
    }
    me.scoring.pool += chain.held;
    chain.held = 0;
    const int made = convert(me.scoring, report.points, rate);
    report.sent = offset(me.incoming, made);
    report.offsetted = made - report.sent;
    opponent.pending += report.sent;
    return true;
}

// The chain is over: the garbage it sent is confirmed. Returns true on an all clear.
inline bool chainEnd(PlayerCore& me, Incoming& opponent, const ChainState& chain) {
    if (!chain.chains) return false;
    opponent.confirm();
    if (!me.field.f.empty()) return false;
    me.scoring.allClear = true;
    return true;
}

// Runs the chain on a field where the pair has just been put. `frame` is the
// frame of the round at which the first wave starts; the waves add their time
// to r.frames.
inline void resolveChain(PlayerCore& me, Incoming& opponent, const Rules& rules, const Timing& timing,
                         int64_t frame, TurnResult& r) {
    ChainState chain;
    WaveReport w;
    int elapsed = 0;
    while (chainWave(me, opponent, rules, rules.rateAt(frame + elapsed), chain, w)) {
        elapsed += timing.waveFrames(w.longestFall);
        r.points += w.points;
        r.sent += w.sent;
        r.offsetted += w.offsetted;
    }
    r.chains = chain.chains;
    r.frames += elapsed;
    r.allClear = chainEnd(me, opponent, chain);
}

// The whole move. softDropRows = the points of the down key on the way (a point a row, and those of
// a lock by it: Timing::dropLockPoints, dropLandPoints).
inline TurnResult playTurn(PlayerCore& me, Incoming& opponent, Placement p, Pair pair, const Rules& rules,
                           const Timing& timing, int64_t frame, Rng& garbageRng, int softDropRows = 0) {
    TurnResult r;
    r.placed = place(me.field, p, pair);
    r.frames = timing.placeFrames(p.x, r.placed.lockY) + timing.splitFrames(r.placed.split, p.x, r.placed.lockY);
    if (rules.softDropBonus && softDropRows > 0) {
        // Drop bonus: it joins the score and the pool; garbage comes out of it
        // only together with a chain.
        me.scoring.score += softDropRows;
        me.scoring.pool += softDropRows;
    }
    // The first wave starts when the landing animation is over: the time of the
    // move so far without the time the next pair takes to appear.
    resolveChain(me, opponent, rules, timing, frame + r.frames - timing.nextFrames, r);
    r.chainEndFrame = r.frames;
    int h[W], placed[W];
    me.field.heights(h);
    r.dropped = dropGarbage(me.field, me.incoming, rules, garbageRng, placed);
    r.frames += timing.garbageFrames(h, placed);
    r.dead = me.field.dead();
    return r;
}

} // namespace juraic
