#pragma once
// Rule settings of a game: everything the player may change on the settings
// screen. A replay stores this struct, so a replay made with other settings
// still plays back correctly.
//
// The defaults are the rules of the present version of the real game.
// ("The real game" in these sources is the commercial game whose rules this program
// follows; README.md names it.)
// Frame counts are not here: they are in timing.h.
#include "../core/bit_field.h"
#include <array>
#include <cstdint>

namespace juraic {

constexpr int FPS = 60;

// The two pieces of a falling pair. The child turns around the axis.
struct Pair {
    Cell axis, child;
};

// Restriction on the first pairs of the tsumo sequence.
enum class TsumoCorrection : uint8_t {
    None,       // plain shuffle
    TwoPairs,   // the first two pairs use at most three colours (the present version and the other newer ones)
    ThreePairs, // the first three pairs use at most three colours (the arcade version)
};

constexpr int MAX_MARGIN_STEPS = 16;

struct Rules {
    // Colours in play: palette[0 .. colors-1] (cell codes 1..5).
    int colors = 4;
    std::array<Cell, MAX_COLORS> palette{1, 2, 3, 4, 5};
    // Pieces of one colour that must be connected to clear (2..72).
    int clearCount = 4;
    TsumoCorrection tsumoCorrection = TsumoCorrection::TwoPairs;

    // Points that make one garbage piece ("ojama rate").
    int targetPoints = 70;
    // Margin time: from marginSeconds on, the rate becomes marginRates[0], and
    // every marginStepSeconds it moves to the next entry; the last entry stays.
    // The default table is the one of the real game for 70 points.
    int marginSeconds = 96;
    int marginStepSeconds = 16;
    int marginSteps = 11;
    std::array<int, MAX_MARGIN_STEPS> marginRates{52, 34, 25, 16, 12, 8, 6, 4, 3, 2, 1};

    // A chain shorter than this sends no garbage ("rensa shibari"); 1 = off.
    // A chain that reaches it attacks with its full score.
    int chainRequirement = 1;
    // Points added to the first wave of the next chain after an all clear.
    // They are divided by the rate of that moment like any other points.
    int allClearBonus = 2100;
    // One point per row of soft drop, counted towards garbage.
    bool softDropBonus = true;
    // Garbage pieces that fall at once (five rows).
    int maxGarbagePerDrop = 30;
    // Game speed in percent (100 = normal).
    int speedPercent = 100;
    // Pre-input, as measured on the real game (timing.h [M]): a rotation pressed
    // shortly before a pair appears is done as it appears, and a direction held
    // as it appears moves the pair in its first frame, the auto-repeat counting
    // on from where the direction was pressed. Off: a button held from before
    // does nothing until it is pressed again, and the auto-repeat starts anew.
    bool preInput = true;
    // Rounds needed to win a match.
    int winsNeeded = 2;
    // Sets needed to win a match: a set is won with winsNeeded rounds, and the rounds are counted
    // anew in the next set. 1 = a match is one set.
    int setsNeeded = 1;

    // The colours in play, never fewer than one nor more than the palette holds.
    int colorsInPlay() const { return colors < 1 ? 1 : colors > MAX_COLORS ? MAX_COLORS : colors; }
    // The pieces at the head of the tsumo that tsumoCorrection keeps to three colours.
    int correctedPieces() const { return tsumoCorrection == TsumoCorrection::TwoPairs ? 4 : tsumoCorrection == TsumoCorrection::ThreePairs ? 6 : 0; }

    // Points per garbage piece at the given frame of the round.
    int rateAt(int64_t frame) const {
        const int64_t seconds = frame / FPS;
        if (marginSteps <= 0 || seconds < marginSeconds) return targetPoints;
        const int64_t step = marginStepSeconds > 0 ? (seconds - marginSeconds) / marginStepSeconds : marginSteps - 1;
        const int rate = marginRates[size_t(step < marginSteps ? step : marginSteps - 1)];
        return rate < 1 ? 1 : rate;
    }
};

} // namespace juraic
