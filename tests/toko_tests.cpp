// Tests of toko (play/session.h, Toko): going back a move takes back what the move did, also the chains shown beside
// the field - the last chain with its points, and the longest chain, when the move that is taken back made it.
// And the toko of the tools (tools/toko_game.h, ai_toko): a move takes the frames a match of the game takes for it, by
// the rules read off the records of the real game when the cancel is on.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../play/session.h"
#include "../tools/toko_game.h"
#include <iostream>
#include <stdexcept>
#include <string>

using namespace juraic;
using namespace play;

static void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

struct Shown {
    int maxChain = 0, lastChain = 0, lastPoints = 0;
    bool operator==(const Shown&) const = default;
    std::string text() const { return std::to_string(maxChain) + "/" + std::to_string(lastChain) + "/" + std::to_string(lastPoints); }
};
static Shown shown(const Toko& t) { return Shown{t.maxChain, t.lastChain, t.lastPoints}; }

// Pairs dropped at random places until moves have made chains `wanted` times; after each such move the next one is
// taken back at once with the move before it (two presses soon after another), which goes back to the start of the
// move that made the chain: the chains shown must be those of then, and the field that of then.
static void testUndoTakesBackTheChain(int wanted) {
    Settings s;
    s.rules.colors = 3;
    Toko t;
    t.seed = 7;
    t.start(s);
    Rng rng(11, 3);
    Shown atStart = shown(t);  // the chains shown at the start of the move being played
    Round roundAtStart = t.game.round;
    bool chained = false;      // the move being played has made a chain
    int side = 0, steps = 0, undone = 0, longestUndone = 0;
    for (int frame = 0; frame < 200000 && undone < wanted; ++frame) {
        if (t.game.round.state != RoundState::Playing) {
            t.start(s), atStart = shown(t), roundAtStart = t.game.round, chained = false;
            continue;
        }
        const uint8_t move = steps > 0 ? uint8_t(side) : uint8_t(0);
        if (steps > 0) --steps;
        const Round prev = t.game.round; // (the start of a move is kept as the frame before its pair appears)
        const PlayerEvents e = t.step(uint8_t(BTN_DOWN | move));
        if (e.waveStarted) chained = true;
        if (!e.spawned) continue;
        if (chained) {
            const Shown after = shown(t);
            check(after.lastChain > 0, "a chain was not shown");
            t.undo();
            check(shown(t) == atStart, "a move taken back left the chains shown at " + after.text() + ", the start of the move had " + atStart.text());
            check(t.game.round.frame == roundAtStart.frame && t.game.round.player[0].field.f.key() == roundAtStart.player[0].field.f.key(),
                  "a move taken back did not go back to the start of the move that made the chain");
            if (after.maxChain > atStart.maxChain) longestUndone = after.maxChain;
            ++undone;
            // (from there on, the pair goes elsewhere: the next move is another)
            side = rng.below(2) ? BTN_LEFT : BTN_RIGHT, steps = 1 + int(rng.below(3));
            chained = false;
            continue;
        }
        atStart = shown(t), roundAtStart = prev, chained = false;
        side = rng.below(2) ? BTN_LEFT : BTN_RIGHT, steps = int(rng.below(4));
    }
    check(undone == wanted, "the moves did not make chains " + std::to_string(wanted) + " times: " + std::to_string(undone));
    check(longestUndone > 0, "no move taken back had made the longest chain so far");
    std::cout << "toko: " << undone << " moves that made a chain taken back, the chains shown as at their start (one had made the longest, " << longestUndone << ")\n";
}

// Every placement of a pair on an empty field, by toko::playMove and by playTurn with the frame counts of a match of
// the game (game/match.h), the cancel off and on: the same frames; and the tools are given the frame counts of a
// match (with the cancel, the rules read off the records of the real game - which the frames of a whole move do not
// read now, so the frames are the same either way: the counts must agree all the same, for what reads them).
static void testToolTiming() {
    const ai::Params params;
    check(recordedRules(DEFAULT_TIMING) != DEFAULT_TIMING, "the rules of the cancel change no frame count");
    for (const bool cancel : {false, true}) {
        Rules rules;
        rules.cancel = cancel;
        const Timing ofMatch = cancel ? recordedRules(DEFAULT_TIMING) : DEFAULT_TIMING;
        const toko::Run run{&rules, &params, 2, 78000, 100};
        check(run.timing() == ofMatch, std::string("cancel ") + (cancel ? "on" : "off") + ": the tools' toko is not given the frame counts of a match");
        for (int x = 0; x < W; ++x)
            for (int d = 0; d < 4; ++d) {
                const Placement at{uint8_t(x), uint8_t(d)};
                if (at.childX() < 0 || at.childX() >= W) continue;
                const Pair pair{Cell(1), Cell(2)};
                PlayerCore a, b;
                Incoming none;
                Rng ga(5, 1), gb(5, 1);
                int64_t frame = 0;
                toko::GameResult g;
                toko::playMove(run, a, none, at, pair, frame, ga, g);
                const TurnResult inMatch = playTurn(b, none, at, pair, rules, ofMatch, 0, gb);
                check(frame == inMatch.frames, std::string("cancel ") + (cancel ? "on" : "off") + ": a move at " + std::to_string(x) + "/" + std::to_string(d) + " takes " +
                                                   std::to_string(frame) + " frames in the tools, " + std::to_string(inMatch.frames) + " in a match");
            }
    }
    std::cout << "the tools' toko: the frame counts of a match, every placement taking the frames of a match\n";
}

int main() {
    try {
        testUndoTakesBackTheChain(6);
        testToolTiming();
        std::cout << "toko tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
