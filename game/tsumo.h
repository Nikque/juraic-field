#pragma once
// The tsumo sequence (the pairs dealt to the players).
//
// Tsu rule: 128 pairs (256 pieces) that repeat, every colour the same number of
// times, and the first pairs restricted to three colours. Both players of a
// round get the same sequence.
//
// The sequences of the real game come from a fixed table of 65536 entries whose
// generator is not public (https://puyo-camp.jp/posts/86154), so this is a plain
// shuffle with the same published properties, not the same sequences.
#include "../core/rng.h"
#include "rules.h"

namespace juraic {

constexpr int TSUMO_PAIRS = 128;
constexpr uint64_t TSUMO_STREAM = 0x7573756D6F; // random stream of the tsumo

struct Tsumo {
    std::array<Cell, 2 * TSUMO_PAIRS> piece{};
    Pair pair(int index) const {
        const size_t i = size_t(index % TSUMO_PAIRS) * 2;
        return {piece[i], piece[i + 1]};
    }
};

inline Tsumo makeTsumo(const Rules& rules, uint64_t seed) {
    Rng rng(seed, TSUMO_STREAM);
    Tsumo t;
    const int colors = rules.colorsInPlay();
    // 256 is not a multiple of 3 or 5: the colours that get one more are random.
    std::array<Cell, MAX_COLORS> order = rules.palette;
    rng.shuffle(order.data(), size_t(colors));
    for (size_t i = 0; i < t.piece.size(); ++i) t.piece[i] = order[i % size_t(colors)];
    rng.shuffle(t.piece.data(), t.piece.size());

    const size_t head = size_t(rules.correctedPieces());
    // Keep the first three colours that appear; a piece of another colour in
    // the head changes places with the nearest later piece of a kept colour.
    // Swapping keeps the colour counts.
    bool kept[8]{};
    int keptCount = 0;
    size_t from = head;
    for (size_t i = 0; i < head; ++i) {
        if (kept[t.piece[i]]) continue;
        if (keptCount < 3) {
            kept[t.piece[i]] = true;
            ++keptCount;
            continue;
        }
        while (!kept[t.piece[from]]) ++from;
        std::swap(t.piece[i], t.piece[from++]);
    }
    return t;
}

} // namespace juraic
