#pragma once
// What the AI may look at: everything that has been on the screen, and nothing
// else. The search takes an Observation, never a Round, so it cannot touch the
// rest of the tsumo, the seed or the random numbers of the garbage columns.
//
// On the screen: both fields, scores and waiting garbage, what each player is
// doing (phase, frames left, the chain in progress, the pair in play and where
// it is), and the next and next-next pair of both players (and the pair after
// those from the moment it moves into the window, shortly before a pair
// appears). Both players draw from one tsumo, so a pair one player has been
// shown is a pair the other one will get. (Not in a match for measuring in
// which each draws from his own: Round::ownTsumo, Observation::pairsOf.)
#include "../game/match.h"
#include <optional>
#include <vector>

namespace juraic::ai {

// Are two pairs the same, piece by piece?
inline bool samePair(Pair a, Pair b) { return a.axis == b.axis && a.child == b.child; }

// The pairs shown so far: 0 .. count() - 1, numbered as the players draw them.
// The numbers are not folded at TSUMO_PAIRS: a pair that has not been shown is
// unknown even when the pair 128 places before it has been (the AI does not
// remember the cycle).
struct SeenTsumo {
    std::vector<Pair> pairs;
    int count() const { return int(pairs.size()); }
    bool known(int index) const { return index >= 0 && index < count(); }
    Pair pair(int index) const { return pairs[size_t(index)]; } // only when known(index)
};

struct Observation {
    Rules rules;
    Timing timing = DEFAULT_TIMING;
    int players = 2;
    int me = 0;        // the seat of the AI
    int64_t frame = 0; // Round::frame: the frame that is played next
    RoundState state = RoundState::Playing;
    Player player[2]; // by seat, as in Round (the order inside a frame depends on the seat)
    SeenTsumo tsumo;
    // A match for measuring in which each player draws from a tsumo of his own (Round::ownTsumo): `tsumo` then
    // holds the pairs of player 0 that his window has shown, tsumo1 those of player 1, and nothing one player
    // is shown tells of the other's pairs. The game: false, and tsumo1 is empty.
    bool ownTsumo = false;
    SeenTsumo tsumo1;
    // Order of the six columns for the next drop, only when the memory reader observed it for this seat.
    std::array<std::optional<std::array<uint8_t, W>>, 2> nextGarbageOrder{};
    // The game read from memory (play/memory/primary_play14.h): a seat shown as Next whose field is still busy - the
    // frames to the end of its landing, its chain or its garbage's fall (0: none, as observe() has it), and whether that
    // is a chain. The reading ends that first (Duel): the garbage sure above the seat falls before its next pair, and
    // what its chain sent is sure when the chain ends.
    int busyLeft[2] = {0, 0};
    bool busyChain[2] = {false, false};

    const Player& self() const { return player[me]; }
    const Player& opponent() const { return player[1 - me]; }
    // The pairs that have been shown of the tsumo player `seat` draws from.
    const SeenTsumo& pairsOf(int seat) const { return ownTsumo && seat == 1 ? tsumo1 : tsumo; }
    const SeenTsumo& mine() const { return pairsOf(me); }
};

// The pair in hand precedes nextPair in a controlling seat; when pondering its
// successor the caller instead starts directly at nextPair.
inline int copySearchPairs(const Observation& o, int seat, bool withHand, Pair* out, int capacity) {
    if (capacity <= 0) return 0;
    const Player& player = o.player[seat];
    const SeenTsumo& visible = o.pairsOf(seat);
    int count = 0;
    if (withHand && player.phase == Phase::Control) out[count++] = player.colors;
    for (int i = player.nextPair; count < capacity && visible.known(i); ++i) out[count++] = visible.pair(i);
    return count;
}

// Number of pairs shown so far. The screen shows, for each player, the pairs
// nextPair and nextPair + 1 (play/view.h drawNext), in every phase; and in the
// last frames before a pair appears the window has moved on and the pair after
// them is in it (Timing::nextShown, measured on the real game).
inline int shownPairs(const Player& p, const Timing& t) {
    return p.nextPair + 2 + (p.phase == Phase::Next && p.wait <= t.nextShown ? 1 : 0);
}
inline int seenPairs(const Round& r) {
    const int first = shownPairs(r.player[0], r.timing), second = r.players == 2 ? shownPairs(r.player[1], r.timing) : 0;
    return first > second ? first : second;
}

// Fills `o` (its memory is used again: this is called every frame).
inline void observe(const Round& r, int me, Observation& o) {
    o.rules = r.rules;
    o.nextGarbageOrder = {};
    o.busyLeft[0] = o.busyLeft[1] = 0, o.busyChain[0] = o.busyChain[1] = false;
    o.timing = r.timing;
    o.players = r.players;
    o.me = me;
    o.frame = r.frame;
    o.state = r.state;
    o.player[0] = r.player[0];
    o.player[1] = r.player[1];
    o.ownTsumo = r.ownTsumo && r.players == 2;
    const int n = o.ownTsumo ? shownPairs(r.player[0], r.timing) : seenPairs(r);
    o.tsumo.pairs.resize(size_t(n));
    for (int i = 0; i < n; ++i) o.tsumo.pairs[size_t(i)] = r.tsumo.pair(i);
    const int n1 = o.ownTsumo ? shownPairs(r.player[1], r.timing) : 0;
    o.tsumo1.pairs.resize(size_t(n1));
    for (int i = 0; i < n1; ++i) o.tsumo1.pairs[size_t(i)] = r.tsumo1.pair(i);
}

inline Observation observe(const Round& r, int me) {
    Observation o;
    observe(r, me, o);
    return o;
}

} // namespace juraic::ai
