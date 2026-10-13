#pragma once
// Trial drops: what chain would start if a few pieces of one colour were added
// on top of one column? For every column and colour the smallest number (one
// up to maxAdd) that makes something clear is found, and that chain is run.
// Few pieces needed = a chain that can be fired soon; the largest of them = the
// chain being built. (The idea is takapt's and Ama's "quiet" search.)
//
// The field must be settled. A piece is only added in the twelve visible rows.
// Any clear count and colour count of Rules; integers only.
//
// The usual field (four clear, no group of four standing, no colour the rules
// do not have) goes a shorter way to the same answer: see detect and addKey.
// The plain way stays for every other field, and the tests compare the two.
#include "../game/field.h"
#include "../game/timing.h"

namespace juraic::ai {

struct Trial {
    int8_t x = 0;     // the column
    Cell color = 0;
    int8_t added = 0; // pieces added
    int8_t y = 0;     // row of the first piece added (the height of the column)
    int chains = 0;
    int points = 0;   // score of the chain (no all clear bonus)
    int frames = 0;   // from the start of the first wave to the end of the chain
    int cleared = 0;  // coloured pieces cleared, the added ones included
};

struct Trials {
    int count = 0;
    Trial item[W * MAX_COLORS]; // at most one for a column and a colour
    // Set by detect on the usual field (see above): alone[x] has bit c set when a single piece of
    // colour c on column x clears. addKey, given these trials, then has less to try.
    bool quiet = false;
    uint8_t alone[W]{};

    // The chain with the most points; equal points: the first found. Null when there is none.
    const Trial* best() const {
        const Trial* b = nullptr;
        for (int i = 0; i < count; ++i)
            if (!b || item[i].points > b->points) b = &item[i];
        return b;
    }
};

// The first wave of every trial, for addKey (filled by detect on the usual field only, when asked for).
struct FirstWaves {
    BitField field[W * MAX_COLORS]; // the field after it
    uint8_t size[W * MAX_COLORS];   // the pieces it clears
};

inline bool quickTrials = true; // tests switch it off: every field then goes the plain way
// Bit 8 * x of COLUMN_BYTES[u] is set when bit x of u is.
inline constexpr auto COLUMN_BYTES = [] {
    std::array<uint64_t, 1 << W> spread{};
    for (unsigned u = 0; u < (1u << W); ++u)
        for (int x = 0; x < W; ++x)
            if (u >> x & 1) spread[u] |= uint64_t(1) << (8 * x);
    return spread;
}();
// The cells and their neighbours above, below, left and right.
inline FieldBits around(FieldBits b) { return b | b.up() | b.down() | b.left() | b.right(); }

// Runs the chain of a field in which something may clear. Returns the number of waves.
// timed = false: the frames are not counted (Trial::frames stays as it is).
inline int runTrialChain(BitField& f, const Rules& rules, const Timing& timing, Trial& t, bool timed = true) {
    Wave w;
    while (findWaveFor(f, w, rules.clearCount)) {
        ++t.chains;
        const FieldBits erased = w.cleared | w.garbage;
        if (timed) t.frames += timing.waveFrames(longestFall(f, erased));
        t.points += gameWaveScore(w, t.chains);
        t.cleared += w.pieces;
        f.erase(erased);
        // (when nothing falls, what is left stands where it stood: no further group has formed)
        if (!f.drop()) break;
    }
    return t.chains;
}

// The plain way of detect: every piece is put on a copy of the field, and the field is looked at.
inline int detectPlain(const GameField& g, const Rules& rules, const Timing& timing, int maxAdd, Trials& out, BitField* after = nullptr, bool timed = true) {
    out.count = 0;
    out.quiet = false;
    int h[W];
    g.heights(h);
    const int colors = rules.colorsInPlay();
    // Fewer pieces than clear cannot clear by themselves: a colour none of whose pieces touches the
    // cells that would be filled on a column starts nothing there, and need not be tried. (The
    // same answer in less time: on most fields most colours touch few columns.)
    const bool touchOnly = maxAdd < rules.clearCount;
    FieldBits ofColor[MAX_COLORS];
    if (touchOnly)
        for (int c = 0; c < colors; ++c) ofColor[c] = g.f.colorMask(rules.palette[size_t(c)]);
    for (int x = 0; x < W; ++x) {
        FieldBits beside;
        if (touchOnly) {
            const int room = 12 - h[x] < maxAdd ? 12 - h[x] : maxAdd;
            if (room <= 0) continue;
            uint16_t column[W]{};
            column[x] = uint16_t(((1u << room) - 1) << h[x]);
            const FieldBits cells = FieldBits::fromColumns(column);
            beside = cells.left() | cells.right() | cells.down();
        }
        for (int c = 0; c < colors; ++c) {
            if (touchOnly && beside.disjoint(ofColor[c])) continue;
            const Cell color = rules.palette[size_t(c)];
            BitField f = g.f;
            for (int n = 1; n <= maxAdd && h[x] + n <= 12; ++n) {
                f.set(x, h[x] + n - 1, color);
                // (four: the test without a flood fill; any other clear count: the full search)
                Wave w;
                if (rules.clearCount == 4 ? !f.hasClear() : !findWaveFor(f, w, rules.clearCount, false)) continue;
                Trial& t = out.item[out.count];
                t = Trial{int8_t(x), color, int8_t(n), int8_t(h[x])};
                runTrialChain(f, rules, timing, t, timed);
                if (after) after[out.count] = f;
                ++out.count;
                break;
            }
        }
    }
    return out.count;
}

// Fills `out` with every trial drop that starts a chain. Returns their number.
// `after`, when given, receives the field after the chain of each trial.
// timed = false: Trial::frames is not filled in.
//
// On the usual field nothing is put and looked at. No group has more than three pieces there, so
// how many pieces of a colour a column needs follows from the groups of that colour beside the cells
// they would fill: one piece clears when the groups it touches hold three pieces or more together,
// two when they hold two or more, three when there is any. That is worked out for all columns at
// once, by colour. The pieces added then are one group with the groups they touch: it is the first
// wave, and the only thing in it.
inline int detect(const GameField& g, const Rules& rules, const Timing& timing, int maxAdd, Trials& out, BitField* after = nullptr, bool timed = true,
                  FirstWaves* first = nullptr) {
    if (!quickTrials || rules.clearCount != 4 || maxAdd >= 4) return detectPlain(g, rules, timing, maxAdd, out, after, timed);
    const Links k = g.f.links();
    const BitField::Seeds seed = BitField::seeds(k);
    const FieldBits occ = g.f.occupied(), floor = FieldBits::fill(1);
    // (a group of four stands, or a piece hangs in the air: the plain way)
    if ((seed.three | seed.pairU | seed.pairR).any() || !occ.subsetOf(occ.up() | floor)) return detectPlain(g, rules, timing, maxAdd, out, after, timed);
    const int colors = rules.colorsInPlay();
    FieldBits ofColor[MAX_COLORS], known;
    for (int c = 0; c < colors; ++c) {
        const Cell color = rules.palette[size_t(c)];
        if (color < 1 || color > MAX_COLORS) return detectPlain(g, rules, timing, maxAdd, out, after, timed);
        ofColor[c] = g.f.colorMask(color) & MASK_12;
        known |= ofColor[c];
    }
    if (!(g.f.normal() & MASK_12).subsetOf(known)) return detectPlain(g, rules, timing, maxAdd, out, after, timed);
    out.count = 0;
    out.quiet = true;
    const FieldBits upDown = k.u | k.d, leftRight = k.l | k.r;
    const FieldBits linked = upDown | leftRight;                               // a piece of a group of two or three
    const FieldBits middle = (k.u & k.d) | (k.l & k.r) | (upDown & leftRight); // the middle one of three
    const FieldBits three = middle | (middle & k.u).up() | (middle & k.d).down() | (middle & k.r).right() | (middle & k.l).left();
    // The cells the first, second and third piece added to a column would fill (within the twelve rows).
    const FieldBits top1 = (occ.up() | floor).without(occ) & MASK_12, top2 = top1.up() & MASK_12, top3 = top2.up() & MASK_12;
    const FieldBits has2 = top2.down(), has3 = top3.down().down(); // the first cell of the columns that have a second, a third
    uint64_t needs[3] = {0, 0, 0}; // bit 8 * x + c of needs[n - 1]: n pieces of colour number c are the fewest that clear on column x
    for (int c = 0; c < colors; ++c) {
        const FieldBits two = ofColor[c] & linked, all = ofColor[c] & three;
        // Seen from a free cell: a piece of the colour on its left (l), on its right (r), below it (b).
        const FieldBits l = ofColor[c].right(), r = ofColor[c].left(), b = ofColor[c].up();
        const FieldBits side = l | r, both = l & r, any = side | b, pair = both | (side & b);
        const FieldBits side2 = two.right() | two.left(), any2 = side2 | two.up(), any3 = all.right() | all.left() | all.up();
        const FieldBits one = (any3 | (pair & any2) | (both & b)) & top1;
        // ... and seen from the second and the third cell, told at the first one.
        const FieldBits sideOf2 = (side & top2).down(), sideOf3 = (side & top3).down().down();
        FieldBits twice, thrice;
        if (maxAdd >= 2) twice = ((any2 | pair | (side2 & top2).down() | (both & top2).down() | (any & sideOf2)) & has2).without(one);
        if (maxAdd >= 3) thrice = ((any | sideOf2 | sideOf3) & has3).without(one | twice);
        needs[0] |= COLUMN_BYTES[one.columnsUsed()] << c, needs[1] |= COLUMN_BYTES[twice.columnsUsed()] << c, needs[2] |= COLUMN_BYTES[thrice.columnsUsed()] << c;
    }
    int h[W];
    g.heights(h);
    for (int x = 0; x < W; ++x) out.alone[x] = 0;
    // (by column, and by colour on a column, as the plain way finds them)
    for (uint64_t left = needs[0] | needs[1] | needs[2]; left; left &= left - 1) {
        const int at = std::countr_zero(left), x = at >> 3, c = at & 7;
        const int n = 3 - 2 * int(needs[0] >> at & 1) - int(needs[1] >> at & 1);
        const Cell color = rules.palette[size_t(c)];
        if (n == 1) out.alone[x] |= uint8_t(1u << color);
        const FieldBits added = FieldBits::columnCells(x, uint16_t(((1u << n) - 1) << h[x])), within = ofColor[c] | added;
        // (three steps reach every piece of the groups the added ones touch: none has more than three)
        FieldBits group = added;
        for (int step = 0; step < 3; ++step) group = around(group) & within;
        const int size = BitField::cells(group);
        Trial& t = out.item[out.count];
        t = Trial{int8_t(x), color, int8_t(n), int8_t(h[x])};
        Wave w;
        w.cleared = group, w.pieces = size, w.colors = w.groups = 1, w.groupBonus = groupBonus(size);
        const FieldBits erased = group | g.f.garbageNextTo(group);
        t.chains = 1;
        if (timed) t.frames = timing.waveFrames(longestFall(g.f, erased)); // (the added pieces lie on top: nothing falls because of them)
        t.points = gameWaveScore(w, 1);
        t.cleared = size;
        BitField f = g.f;
        f.erase(erased);
        const bool falls = f.drop();
        if (first) first->field[out.count] = f, first->size[out.count] = uint8_t(size);
        if (falls) runTrialChain(f, rules, timing, t, timed);
        if (after) after[out.count] = f;
        ++out.count;
    }
    return out.count;
}

// ---- key pieces ------------------------------------------------------------------
// A chain a human player builds stays cut for most of the time it is built: the
// groups stand ready, and the one piece that makes the next group clear when the
// chain reaches it (the key piece) is put in late, or left out to keep the front
// part of the chain as a small one. A trial drop alone sees only the part before
// the cut. Here the chain of a trial drop is carried on: where it has stopped,
// every piece that would make a group clear from the top of a column is a key
// piece to try; it is put on top of that column of the field as it is now, and
// the whole trial is run again. It counts only when the chain really becomes
// longer, and when the key piece clears nothing by itself (that would be a
// trigger, and a trial drop of its own). One key piece at most is tried for a
// chain.

struct KeyPlan {
    Trial t;          // the chain, with the key piece in place when there is one; x, color, added and y are the trigger's
    bool key = false; // a key piece is part of the plan: keyColor on top of column keyX
    int8_t keyX = 0;
    Cell keyColor = EMPTY;
};

inline bool anyClear(const BitField& f, const Rules& rules) {
    Wave w;
    return rules.clearCount == 4 ? f.hasClear() : findWaveFor(f, w, rules.clearCount, false);
}

// Runs the trial of `k.t` (added pieces of color on column x) with the key piece
// of k on the field: on another column it is there first, on the trigger's
// column it lies on top of the trigger. heights = of `field`.
// False when it cannot be done (no room, a key piece that clears something).
inline bool runWithKey(const BitField& field, const int heights[W], const Rules& rules, const Timing& timing, const KeyPlan& k, Trial& t,
                       BitField& after, bool timed = true) {
    BitField f = field;
    int h[W];
    for (int i = 0; i < W; ++i) h[i] = heights[i];
    const int x = k.t.x;
    if (k.key && k.keyX != x) {
        if (h[k.keyX] >= 12) return false;
        f.set(k.keyX, h[k.keyX]++, k.keyColor);
    }
    if (k.key && anyClear(f, rules)) return false;
    for (int j = 0; j < k.t.added; ++j) {
        if (h[x] >= 12) return false;
        f.set(x, h[x]++, k.t.color);
    }
    if (k.key && k.keyX == x) {
        if (h[x] >= 12 || k.keyColor == k.t.color) return false;
        f.set(x, h[x]++, k.keyColor);
    }
    t = Trial{k.t.x, k.t.color, k.t.added, k.t.y};
    runTrialChain(f, rules, timing, t, timed);
    after = f;
    return t.chains > 0;
}

// Where a chain has stopped (a settled field): the pieces that, on top of a
// column, would make a group clear. Returns their number (at most `most`).
inline int stallKeys(const BitField& stall, const Rules& rules, int8_t keyX[], Cell keyColor[], int most) {
    int h[W];
    stall.heights(h);
    int count = 0;
    for (int x = 0; x < W; ++x) {
        if (h[x] >= 12) continue;
        const Cell next[3] = {x > 0 ? stall.get(x - 1, h[x]) : EMPTY, x + 1 < W ? stall.get(x + 1, h[x]) : EMPTY, h[x] > 0 ? stall.get(x, h[x] - 1) : EMPTY};
        for (int i = 0; i < 3 && count < most; ++i) {
            const Cell c = next[i];
            if (c == EMPTY || c == GARBAGE || (i > 0 && c == next[0]) || (i > 1 && c == next[1])) continue;
            BitField f = stall;
            f.set(x, h[x], c);
            if (anyClear(f, rules)) keyX[count] = int8_t(x), keyColor[count] = c, ++count;
        }
    }
    return count;
}

// The same where four clear and no group of four stands (a chain has just ended there), without
// putting a piece: every group has three pieces at most, and a piece on top of a column joins the
// groups of its colour on its left, on its right and below it. It clears when they hold three
// pieces or more together. All columns at once; the answers come in the order of stallKeys (by
// column: the colour of the piece on the left, then of the one on the right, then of the one below).
inline int stallKeysQuiet(const BitField& stall, int8_t keyX[], Cell keyColor[], int most) {
    const Links k = stall.links();
    const FieldBits upDown = k.u | k.d, leftRight = k.l | k.r;
    const FieldBits linked = upDown | leftRight;                               // a piece of a group of two or three
    const FieldBits middle = (k.u & k.d) | (k.l & k.r) | (upDown & leftRight); // the middle one of three
    const FieldBits three = middle | (middle & k.u).up() | (middle & k.d).down() | (middle & k.r).right() | (middle & k.l).left();
    const FieldBits occ = stall.occupied();
    const FieldBits top = (occ.up() | FieldBits::fill(1)).without(occ) & MASK_12; // the lowest free cell of each column
    const FieldBits normal = stall.normal() & MASK_12;
    // Seen from a free cell: the piece on its left (l), on its right (r) and below it (b).
    const FieldBits nl = normal.right(), nr = normal.left(), nb = normal.up();
    FieldBits lr, lb, rb; // those two are not of one colour
    for (const FieldBits& p : stall.plane) {
        const FieldBits l = p.right(), r = p.left(), b = p.up();
        lr |= l ^ r, lb |= l ^ b, rb |= r ^ b;
    }
    const FieldBits sameLR = (nl & nr).without(lr), sameLB = (nl & nb).without(lb), sameRB = (nr & nb).without(rb);
    const FieldBits l3 = three.right(), r3 = three.left(), b3 = three.up();
    const FieldBits l2 = linked.right(), r2 = linked.left(), b2 = linked.up();
    // A piece of the colour on the left: with a group of three; or with two pieces beside it of which
    // one has a partner; or with all three.
    const FieldBits byLeft = (l3 | (sameLR & r3) | (sameLB & b3) | ((sameLR | sameLB) & (l2 | (sameLR & r2) | (sameLB & b2))) | (sameLR & sameLB)) & top;
    // ... of the colour on the right, when that is another one; of the colour below, when that is a third.
    const FieldBits byRight = (r3 | (sameRB & (b3 | r2 | b2))).without(sameLR) & top;
    const FieldBits byBelow = b3.without(sameLB | sameRB) & top;
    if ((byLeft | byRight | byBelow).empty()) return 0;
    uint16_t left[W], right[W], below[W];
    byLeft.columns(left), byRight.columns(right), byBelow.columns(below);
    int count = 0;
    for (int x = 0; x < W; ++x) {
        const unsigned any = unsigned(left[x]) | right[x] | below[x];
        if (!any) continue;
        const int y = std::countr_zero(any);
        if (left[x] && count < most) keyX[count] = int8_t(x), keyColor[count] = stall.get(x - 1, y), ++count;
        if (right[x] && count < most) keyX[count] = int8_t(x), keyColor[count] = stall.get(x + 1, y), ++count;
        if (below[x] && count < most) keyX[count] = int8_t(x), keyColor[count] = stall.get(x, y - 1), ++count;
    }
    return count;
}

// Carries the plan `cur` (a trial drop, without a key piece), whose chain left the field `stall`,
// on with one key piece. `best` receives the longest chain there is with one (more points on
// equal waves); when there is none it stays a plan without a key piece.
// `runs` is the number of trials that may still be run; it is counted down.
// known: the trials of detect for this field and these heights, of which cur.t is one (null: not
// at hand). When they say the field is the usual one, the key pieces are found without putting
// them (stallKeysQuiet), and the key piece is run without looking first whether it clears
// by itself: the trials have told. timed = false: Trial::frames is not filled in.
// first, index: the first waves of those trials (detect), and which of them cur.t is. A key piece
// of another colour than the trigger, on another column, has no part in the first wave: it comes
// to rest on top of its column of the field after that wave, and the chain is run from there.
// (Only without the frames: the fall of the key piece may be the longest of that wave.)
inline void addKey(const BitField& field, const int heights[W], const Rules& rules, const Timing& timing, const KeyPlan& cur, const BitField& stall,
                   int& runs, KeyPlan& best, const Trials* known = nullptr, bool timed = true, const FirstWaves* first = nullptr, int index = -1) {
    const bool quick = known && known->quiet && rules.clearCount == 4 && quickTrials;
    int8_t keyX[3 * W];
    Cell keyColor[3 * W];
    const int n = quick ? stallKeysQuiet(stall, keyX, keyColor, 3 * W) : stallKeys(stall, rules, keyX, keyColor, 3 * W);
    const int x = cur.t.x;
    for (int i = 0; i < n && runs > 0; ++i) {
        KeyPlan next = cur;
        next.key = true, next.keyX = keyX[i], next.keyColor = keyColor[i];
        --runs;
        Trial t;
        BitField after;
        if (quick && heights[x] + cur.t.added <= 12) {
            // The key piece on the usual field. On another column: no room, or it clears by itself
            // (a trial drop of one piece has said so). On the trigger's column: no room above the
            // trigger, or the trigger's own colour.
            const int kx = keyX[i];
            const Cell color = keyColor[i];
            if (kx != x ? heights[kx] >= 12 || (known->alone[kx] >> color & 1) : heights[x] + cur.t.added >= 12 || color == cur.t.color) continue;
            t = Trial{cur.t.x, cur.t.color, cur.t.added, cur.t.y};
            if (first && !timed && kx != x && color != cur.t.color) {
                after = first->field[index];
                after.set(kx, detail::countBits(after.occupied().column(kx)), color);
                const int size = first->size[index], bonus = groupBonus(size);
                t.chains = 1, t.points = 10 * size * (bonus < 1 ? 1 : bonus), t.cleared = size;
                runTrialChain(after, rules, timing, t, false);
            } else {
                BitField f = field;
                if (kx != x) f.set(kx, heights[kx], color);
                for (int j = 0; j < cur.t.added; ++j) f.set(x, heights[x] + j, cur.t.color);
                if (kx == x) f.set(x, heights[x] + cur.t.added, color);
                runTrialChain(f, rules, timing, t, timed);
                after = f;
            }
            if (t.chains <= 0) continue;
        } else if (!runWithKey(field, heights, rules, timing, next, t, after, timed)) {
            continue;
        }
        if (t.chains <= cur.t.chains) continue;
        next.t = t;
        if (!best.key || t.chains > best.t.chains || (t.chains == best.t.chains && t.points > best.t.points)) best = next;
    }
}

} // namespace juraic::ai
