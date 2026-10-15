// Tests of the reading of the real game's screen (play/realgame_read.h), with pictures drawn here from rounds of this
// program as the table says the screen shows them:
//
//  1. The table as text: written and read again it is the same; a line not understood is named.
//  2. Pictures of every frame of rounds of two AIs and of random buttons, drawn at four sizes: the pieces at rest, the
//     pair in play (where it is, which way it turns, its colours), the window of the next pairs and the garbage
//     notice read are those of the round.
//  3. The same pictures followed frame after frame (Tracker): a new pair is seen in the frame before the one in which
//     it can be moved, of each side and of no other frame; what the AI is then shown (observeSpawn) is what the round
//     shows it (ai/observe.h) - the own field and phase, the frame, the pairs, the other side as far as one picture
//     tells. Pieces of a chain that move keep a field from standing still.
//  4. A pair of one colour is read the way it was turned before; a picture saved and read again is the same.
//  5. A point of the table measured on the real game (measuredLayout): a piece with its face and shine is read, a
//     small dot, the background and the mark where the pair appears (red or orange) are not, a piece over that mark is
//     (red with its shine, green without), pale grey (bright or dim) is garbage, a bright light of the screen of no
//     colour of a piece is UNSURE. A reading with such a point takes nothing of that side's field.
#include "../ai/agents.h"
#include "../play/realgame_read.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace juraic;
using namespace play::realgame;

static void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

// ---- pictures drawn as the table says ----------------------------------------------------------------------------

struct Canvas {
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;
    float sx = 1, sy = 1; // base pixels to pixels
    Canvas(int w, int h, const Layout& l) : width(w), height(h), pixels(size_t(w) * size_t(h) * 4, 0), sx(float(w) / float(l.baseWidth)), sy(float(h) / float(l.baseHeight)) {}
    Picture picture() const { return {pixels.data(), width, height, width * 4}; }
    // The base rectangle (x0, y0) - (x1, y1), cut below `top` (in base pixels: nothing above it is drawn).
    void fill(float x0, float y0, float x1, float y1, Rgb c, float top = -1e9f) {
        y0 = std::max(y0, top);
        const int i0 = std::max(0, int(std::lround(x0 * sx))), i1 = std::min(width, int(std::lround(x1 * sx)));
        const int j0 = std::max(0, int(std::lround(y0 * sy))), j1 = std::min(height, int(std::lround(y1 * sy)));
        for (int j = j0; j < j1; ++j)
            for (int i = i0; i < i1; ++i) {
                uint8_t* p = &pixels[(size_t(j) * size_t(width) + size_t(i)) * 4];
                p[0] = uint8_t(c.b), p[1] = uint8_t(c.g), p[2] = uint8_t(c.r), p[3] = 255;
            }
    }
};

static Rgb colorOf(const Layout& l, Cell cell) {
    for (const ColorRef& c : l.colors)
        if (c.cell == cell) return c.rgb;
    throw std::runtime_error("no colour in the table for a piece");
}

const Rgb ABOVE{40, 40, 60};    // what the screen shows above the field (no colour of the table)
const Rgb LIGHT{120, 230, 230}; // a light the screen draws over the field

// A piece of the field whose middle is `height` cells above the floor of the field.
static void piece(Canvas& c, const Layout& l, const SideLayout& d, int x, float height, Cell cell) {
    const float cx = d.left + (float(x) + 0.5f) * d.cellWidth, cy = d.bottom - height * d.cellHeight;
    const float hw = d.cellWidth * 0.43f, hh = d.cellHeight * 0.43f;
    c.fill(cx - hw, cy - hh, cx + hw, cy + hh, colorOf(l, cell), d.bottom - d.shownRows * d.cellHeight);
}

static void mark(Canvas& c, const Layout& l, Spot at, const Mark& m) {
    for (const Probe& q : m.probes) {
        const float r = l.patch + 2;
        if (q.rgb != Rgb{}) c.fill(at.x + q.dx - r, at.y + q.dy - r, at.x + q.dx + r, at.y + q.dy + r, q.rgb);
    }
}

// The marks that show `total` garbage pieces, the largest first, as many as there are places (false: more is waiting
// than the places show).
static bool marksFor(const Layout& l, int total, size_t places, std::vector<const Mark*>& out) {
    std::vector<const Mark*> byValue;
    for (const Mark& m : l.marks) byValue.push_back(&m);
    std::sort(byValue.begin(), byValue.end(), [](const Mark* a, const Mark* b) { return a->value > b->value; });
    out.clear();
    for (const Mark* m : byValue)
        while (total >= m->value && out.size() < places) out.push_back(m), total -= m->value;
    return total == 0;
}

struct Drawn {
    bool nextShown[2] = {};     // the window stood (not moving)
    Pair next[2][2]{};          // [side][next, next-next]
    bool noticeFits[2] = {};
    bool moving[2] = {};        // a chain or a lock: pieces drawn that move
};

// One frame of the round `r` as the screen would show it. `ready`: the screen before the round, the window standing.
// `noise`: pieces that move in the phases in which the screen shows them moving (a lock, a chain, garbage).
static Drawn draw(Canvas& c, const Layout& l, const Round& r, bool ready, Rng& noise) {
    Drawn out;
    std::fill(c.pixels.begin(), c.pixels.end(), uint8_t(0));
    for (int s = 0; s < 2; ++s) {
        const SideLayout& d = l.side[s];
        const Player& p = r.player[s];
        const float top = d.bottom - d.shownRows * d.cellHeight;
        c.fill(d.left, top - 3 * d.cellHeight, d.left + W * d.cellWidth, top, ABOVE);
        for (int x = 0; x < W; ++x)
            for (int y = 0; y < H; ++y)
                if (const Cell cell = p.field.f.get(x, y)) piece(c, l, d, x, float(y) + 0.5f, cell);
        const auto pair = [&](const PairControl& at, Pair colors) {
            const float lift = at.upper ? 0.5f : 0.f;
            piece(c, l, d, at.x, float(at.y) + 0.5f + lift, colors.axis);
            piece(c, l, d, at.childX(), float(at.childY()) + 0.5f + lift, colors.child);
        };
        if (p.phase == Phase::Control) pair(p.pair, p.colors);
        else if (p.phase == Phase::Next && p.wait <= 1 && !ready && !p.field.dead()) pair(spawnPair(r.timing), r.tsumo.pair(p.nextPair)); // (one frame before it can be moved)
        if (!ready && (p.phase == Phase::Split || p.phase == Phase::Land || p.phase == Phase::Wave || p.phase == Phase::Garbage)) {
            // Something moves: a piece between two places, a piece at rest hidden.
            out.moving[s] = true;
            int h[W];
            p.field.heights(h);
            const int x = int(noise.below(W));
            if (h[x] < 11) piece(c, l, d, x, float(h[x]) + 1.f + 0.5f * float(noise.below(4)), l.colors[noise.below(5)].cell);
            const int y = int(noise.below(W));
            if (h[y] && noise.below(2)) {
                const float cx = d.left + (float(y) + 0.5f) * d.cellWidth, cy = d.bottom - (float(h[y]) - 0.5f) * d.cellHeight;
                c.fill(cx - d.cellWidth / 2, cy - d.cellHeight / 2, cx + d.cellWidth / 2, cy + d.cellHeight / 2, Rgb{}, top);
            }
            if (noise.below(3) == 0) {
                // A light the screen draws over the field: bright, of no colour of a piece.
                const float cx = d.left + float(noise.below(W - 1)) * d.cellWidth, cy = d.bottom - float(noise.below(12)) * d.cellHeight;
                c.fill(cx, cy - 2 * d.cellHeight, cx + 2 * d.cellWidth, cy, LIGHT, top);
            }
        }
        // The window: it moves on shortly before a pair appears (play/view.h drawNext).
        const int first = p.phase == Phase::Next && p.wait <= r.timing.nextShown && !ready ? 1 : 0;
        out.nextShown[s] = ready || !(p.phase == Phase::Next && p.wait <= r.timing.nextSlide && p.wait > r.timing.nextShown);
        if (out.nextShown[s])
            for (int n = 0; n < 2; ++n) {
                const Pair q = r.tsumo.pair(p.nextPair + first + n);
                out.next[s][n] = q;
                const float size = d.cellWidth * 0.35f;
                for (int k = 0; k < 2; ++k) {
                    const Spot at = d.next[n][k];
                    c.fill(at.x - size, at.y - size, at.x + size, at.y + size, colorOf(l, k ? q.child : q.axis));
                }
            }
        std::vector<const Mark*> marks;
        out.noticeFits[s] = marksFor(l, p.incoming.total(), d.notice.size(), marks);
        for (size_t k = 0; k < marks.size(); ++k) mark(c, l, d.notice[k], *marks[k]);
    }
    return out;
}

// The cells a pair covers, in half cells, with their colours (a pair of one colour turned the other way is the same).
static bool sameCells(const PairControl& a, Pair ca, const PairControl& b, Pair cb) {
    int ax[2], ah[2], bx[2], bh[2];
    pairHalves(a, ax[0], ah[0], ax[1], ah[1]);
    pairHalves(b, bx[0], bh[0], bx[1], bh[1]);
    const Cell ac[2] = {ca.axis, ca.child}, bc[2] = {cb.axis, cb.child};
    const auto has = [&](int x, int h, Cell c) { return (bx[0] == x && bh[0] == h && bc[0] == c) || (bx[1] == x && bh[1] == h && bc[1] == c); };
    return has(ax[0], ah[0], ac[0]) && has(ax[1], ah[1], ac[1]);
}

static bool lyingOn(const PairControl& at, const GameField& g) {
    PairSeen s;
    s.seen = true, s.at = at;
    return pairLying(s, g);
}

// ---- 1. the table as text ----------------------------------------------------------------------------------------

static void testTableText() {
    Layout back;
    std::string error;
    for (const Layout& l : {sampleLayout(), measuredLayout()}) {
        const std::string text = formatLayout(l);
        check(parseLayout(text, back, &error), "the table written as text is not read again: " + error);
        check(formatLayout(back) == text, "the table read again from its text is another one");
    }
    {
        const Layout l = sampleLayout();
        check(parseLayout(formatLayout(l), back) && back == l, "the plain table read again from its text is another one");
    }
    check(parseLayout("# remark only\n\n  base 1920 1080  # and a remark\n", back, &error) && back.baseWidth == 1920 && back.colors.empty(), "a short table is not read");
    for (const char* bad : {"base 0 100", "color 9 1 2 3", "color 1 1 2", "color 1 1 2 300", "mark 6", "mark 6 1 2 3 4", "field 3 1 2 3 4 5",
                            "next 1 3 1 2 3 4", "notice 1 5", "pieces 4", "still 0", "base 10 10 10", "share 8 45", "share 8 45 101", "marked 1 7 1 5 1 2 3",
                            "marked 1 1 14 5 1 2 3", "marked 3 1 1 5 1 2 3", "marked 1 1 1 5", "marked 1 1 1 5 1 2", "markpatch -1"}) {
        check(!parseLayout(std::string("still 2\n") + bad + "\n", back, &error), std::string("a wrong line is read: ") + bad);
        check(error.rfind("line 2:", 0) == 0, std::string("a wrong line is not named: ") + bad + " -> " + error);
    }
    std::cout << "the table as text: written and read again the same (the plain one and the measured one); wrong lines named\n";
}

// ---- 2. and 3. rounds drawn and read ------------------------------------------------------------------------------

struct Counts {
    long frames = 0, resting = 0, pairs = 0, nexts = 0, notices = 0, spawns = 0, observed = 0, otherInAir = 0, garbageShown = 0, movingFrames = 0, unclear = 0;
};

// One frame of the round, the buttons of the AIs (null: random buttons).
static void stepRound(Round& r, ai::Agent* const agents[2], ai::Observation& o, Rng& hands, uint8_t held[2]) {
    uint8_t buttons[2];
    for (int s = 0; s < 2; ++s) {
        if (agents[s]) {
            ai::observe(r, s, o);
            buttons[s] = agents[s]->frame(o);
        } else {
            if (hands.below(4) == 0) held[s] = uint8_t(hands.below(32) | (hands.below(3) ? BTN_DOWN : 0));
            buttons[s] = held[s];
        }
    }
    PlayerEvents e[2];
    r.step(buttons, e);
}

// Plays a round, draws every frame at width x height, reads it and follows it. `a`, `b`: the AIs of the two seats
// (null: random buttons).
static void playAndRead(const Layout& l, int width, int height, uint64_t seed, ai::Agent* a, ai::Agent* b, int frames, Counts& n) {
    const Rules rules;
    Round r;
    r.start(rules, seed);
    Canvas canvas(width, height, l);
    Rng noise(seed, 7), hands(seed, 8);
    ai::Agent* agents[2] = {a, b};
    for (int s = 0; s < 2; ++s)
        if (agents[s]) agents[s]->startRound(s, seed + uint64_t(s));
    Tracker tracker;
    int64_t clock = 1000; // (the frames of the clock do not begin at 0)
    // The screen before the round: both fields empty, the windows standing.
    for (int f = 0; f < 45; ++f) {
        draw(canvas, l, r, true, noise);
        tracker.step(read(canvas.picture(), l), clock++);
        check(!tracker.spawned[0] && !tracker.spawned[1], "a pair seen before the round");
    }
    check(tracker.fromStart && tracker.knows(0) && tracker.knows(1) && !tracker.knows(2), "the first two pairs are not learnt before the round");
    uint8_t held[2] = {};
    int total[2] = {}; // the garbage above each field, and the frame of the clock since which it has been so
    int64_t totalSince[2] = {clock, clock};
    ai::Observation o, mine;
    for (int f = 0; f < frames && r.state == RoundState::Playing; ++f, ++clock) {
        // (now and then a frame is not taken, as by a capture that misses one; not one in which a pair appears)
        const bool appears = (r.player[0].phase == Phase::Next && r.player[0].wait <= 2) || (r.player[1].phase == Phase::Next && r.player[1].wait <= 2);
        if (f % 97 == 13 && !appears) {
            stepRound(r, agents, o, hands, held);
            continue;
        }
        for (int s = 0; s < 2; ++s)
            if (r.player[s].incoming.total() != total[s]) total[s] = r.player[s].incoming.total(), totalSince[s] = clock;
        const Drawn shown = draw(canvas, l, r, false, noise);
        const Reading got = read(canvas.picture(), l);
        tracker.step(got, clock);
        ++n.frames;
        for (int s = 0; s < 2; ++s) {
            const Player& p = r.player[s];
            const SideRead& g = got.side[s];
            const std::string where = "seed " + std::to_string(seed) + ", " + std::to_string(width) + "x" + std::to_string(height) + ", frame " + std::to_string(f) + ", side " + std::to_string(s) + ": ";
            n.movingFrames += shown.moving[s];
            n.unclear += g.unsure > 0;
            check(g.unsure == 0 || shown.moving[s], where + "a point not sure where no light is drawn");
            // The pieces at rest, where nothing moves.
            if (!shown.moving[s]) {
                GameField expected = p.field;
                expected.row14 = {};
                if (p.phase == Phase::Control && !p.pair.upper) {
                    // (a piece of the pair lying on the pieces is among them as one picture shows it)
                    const PairControl& q = p.pair;
                    const int lower = q.dir == DOWN ? 1 : 0;
                    const int x[2] = {lower ? q.childX() : q.x, lower ? q.x : q.childX()}, y[2] = {lower ? q.childY() : q.y, lower ? q.y : q.childY()};
                    const Cell c[2] = {lower ? p.colors.child : p.colors.axis, lower ? p.colors.axis : p.colors.child};
                    for (int k = 0; k < 2; ++k) {
                        int h[W];
                        expected.heights(h);
                        if (y[k] == h[x[k]] && y[k] < H) expected.f.set(x[k], y[k], c[k]);
                    }
                }
                check(restingField(g) == expected, where + "the pieces at rest are not read");
                ++n.resting;
            }
            // The pair in play, where it shows whole (or upright with its child above the screen).
            if (p.phase == Phase::Control) {
                const SideLayout& d = l.side[s];
                const auto visible = [&](int k) { return pointHeight(k) <= d.shownRows; };
                int ax, ah, cx, ch;
                pairHalves(p.pair, ax, ah, cx, ch);
                const bool whole = (visible(ah + 1) && visible(ch + 1)) || (p.pair.dir == UP && visible(ah));
                if (whole && !shown.moving[s]) {
                    const PairSeen seen = readPair(g, p.field, p.colors);
                    check(seen.seen, where + "the pair in play is not seen");
                    const Pair colors{seen.colors.axis, seen.childHidden ? p.colors.child : seen.colors.child};
                    check(sameCells(seen.at, colors, p.pair, p.colors), where + "the pair in play is not read where it is");
                    if (p.colors.axis != p.colors.child) check(seen.at.x == p.pair.x && seen.at.y == p.pair.y && seen.at.dir == p.pair.dir && seen.at.upper == p.pair.upper, where + "the pair is read turned another way");
                    ++n.pairs;
                }
            }
            if (shown.nextShown[s]) {
                for (int k = 0; k < 2; ++k)
                    check(g.nextRead[k] && g.next[k].axis == shown.next[s][k].axis && g.next[k].child == shown.next[s][k].child, where + "the window of the next pairs is not read");
                ++n.nexts;
            } else {
                check(!g.nextRead[0] && !g.nextRead[1], where + "a moving window is read");
            }
            if (shown.noticeFits[s]) {
                check(g.notice == p.incoming.total(), where + "the garbage notice reads " + std::to_string(g.notice) + " for " + std::to_string(p.incoming.total()));
                ++n.notices, n.garbageShown += p.incoming.total() > 0;
            }
            // Followed: a new pair in the frame before it can be moved, and in no other.
            const bool coming = p.phase == Phase::Next && p.wait == 1 && !p.field.dead();
            check(tracker.spawned[s] == coming, where + (coming ? "the new pair is not seen" : "a new pair is seen where none is"));
            if (!coming) continue;
            ++n.spawns;
            check(tracker.side[s].pairs == p.nextPair + 1, where + "the pairs given are not counted");
            check(observeSpawn(tracker, s, rules, o), where + "nothing is observed");
            ai::observe(r, s, mine);
            const Player& me = o.self();
            check(o.me == s && o.frame == r.frame && me.phase == Phase::Next && me.wait == 1 && me.nextPair == p.nextPair, where + "the own phase or the frame is not observed");
            check(me.field.f == p.field.f, where + "the own field is not observed");
            for (int k = 0; k < 3; ++k) {
                const int i = p.nextPair + k;
                check(o.mine().known(i) && o.mine().pair(i).axis == mine.mine().pair(i).axis && o.mine().pair(i).child == mine.mine().pair(i).child, where + "a pair shown is not observed");
            }
            if (shown.noticeFits[s] && clock - totalSince[s] + 1 >= tracker.noticeFrames) {
                // (the notice is taken when it has read the same for Tracker::noticeFrames frames)
                check(me.incoming.total() == p.incoming.total(), where + "the garbage above the own field is not observed");
                // (garbage of a chain that still runs: the other side moves, and it waits for the chain)
                if (p.incoming.pending) check(me.incoming.pending == p.incoming.total(), where + "garbage of a chain that runs is observed as falling");
            }
            const Player& his = r.player[1 - s];
            const Player& seen = o.opponent();
            if (his.phase == Phase::Control && !lyingOn(his.pair, his.field) && tracker.side[1 - s].pair.seen) {
                check(seen.phase == Phase::Control && seen.field.f == his.field.f && sameCells(seen.pair, seen.colors, his.pair, his.colors), where + "the other side's pair in the air is not observed");
                ++n.otherInAir;
            }
            if (his.phase == Phase::Next && his.wait > 1 && !shown.moving[1 - s]) check(seen.field.f == his.field.f && seen.phase != Phase::Control, where + "the other side waiting for a pair is not observed");
            ++n.observed;
        }
        stepRound(r, agents, o, hands, held);
    }
}

static void testRounds() {
    const auto started = std::chrono::steady_clock::now();
    const Layout l = sampleLayout();
    ai::Params small;
    small.beamWidth = 30, small.beamDepth = 8, small.beamQueues = 3;
    Counts n;
    const int sizes[4][2] = {{BASE_WIDTH, BASE_HEIGHT}, {1920, 1080}, {2560, 1440}, {1280, 720}};
    for (int k = 0; k < 4; ++k) {
        const std::unique_ptr<ai::Agent> x3 = ai::makeAgent("x3", small), random = ai::makeAgent("random");
        check(x3 && random, "the agents x3 and random are not there");
        playAndRead(l, sizes[k][0], sizes[k][1], 31 + uint64_t(k), x3.get(), random.get(), 2500, n);
        playAndRead(l, sizes[k][0], sizes[k][1], 41 + uint64_t(k), nullptr, random.get(), 1500, n);
    }
    check(n.spawns > 300 && n.otherInAir > 50 && n.garbageShown > 50 && n.movingFrames > 1000 && n.unclear > 300, "the rounds read show too little");
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::cout << "rounds drawn at four sizes and read (" << n.frames << " frames, " << seconds << " s): the pieces at rest " << n.resting << " times, the pair in play " << n.pairs
              << ", the window " << n.nexts << ", the notice " << n.notices << " (" << n.garbageShown << " with garbage); " << n.spawns
              << " new pairs seen in the frame before they can be moved, and observed as the round shows them (the other side's pair in the air "
              << n.otherInAir << " times); " << n.unclear << " readings of a side under a light, not taken\n";
}

// ---- 4. small things ---------------------------------------------------------------------------------------------

static void testOneColour() {
    // A pair of one colour upside down, in the air: read so while it was so before, and the other way round when it was.
    SideRead s;
    for (auto& column : s.point)
        for (int k = 0; k < POINTS; ++k) column[k] = k >= 25 ? HIDDEN : EMPTY;
    s.point[3][10] = s.point[3][11] = s.point[3][12] = s.point[3][13] = 2; // (cells 5 and 6 of column 4)
    const GameField none;
    PairSeen before;
    before.seen = true, before.at.x = 3, before.at.y = 6, before.at.dir = DOWN, before.at.upper = false;
    const PairSeen down = readPair(s, none, {2, 2}, &before);
    check(down.seen && down.at.x == 3 && down.at.y == 6 && down.at.dir == DOWN, "a pair of one colour turned upside down is read the other way");
    before.at.y = 5, before.at.dir = UP;
    const PairSeen up = readPair(s, none, {2, 2}, &before);
    check(up.seen && up.at.y == 5 && up.at.dir == UP, "a pair of one colour standing up is read the other way");
    // Not the pair: its colours are others, or three pieces, or one piece between two places.
    check(!readPair(s, none, {3, 2}).seen, "pieces of other colours are read as the pair");
    s.point[0][0] = s.point[0][1] = 1;
    check(!readPair(s, none, {}).seen, "three pieces are read as a pair");
    s.point[0][0] = s.point[0][1] = EMPTY, s.point[3][13] = EMPTY;
    check(!readPair(s, none, {}).seen, "a piece between two places is read");
    std::cout << "a pair of one colour is read the way it was turned; other pieces are not read as the pair\n";
}

static void testPointRules() {
    const Layout l = measuredLayout();
    const SideLayout& d = l.side[1];
    // A square of the size of the picture the table was measured on, around the lower point of a cell: `colour` over
    // the given share of it (from its left), `white` over the next share, the rest the background of the field.
    const auto at = [&](int x, int y, Rgb colour, int share, Rgb white, int whiteShare) {
        Canvas c(2000, 1125, l);
        const Spot p = pointSpot(d, x, 2 * y);
        const float half = l.patch, from = p.x - half, width = 2 * half + 1;
        c.fill(p.x - 3 * half, p.y - 3 * half, p.x + 3 * half, p.y + 3 * half, Rgb{38, 46, 72});
        c.fill(from, p.y - half, from + width * float(share) / 100, p.y + half + 1, colour);
        c.fill(from + width * float(share) / 100, p.y - half, from + width * float(share + whiteShare) / 100, p.y + half + 1, white);
        return read(c.picture(), l).side[1].point[x][2 * y];
    };
    const Rgb red{158, 23, 24}, green{54, 185, 37}, white{240, 240, 245}, dim{140, 140, 150}, mark{200, 56, 54};
    check(at(0, 0, red, 50, white, 20) == 1 && at(4, 3, green, 60, white, 0) == 2, "a piece with its face and shine is not read");
    check(at(1, 2, green, 15, white, 50) == 2, "the upper part of a piece (its eyes) is not read");
    check(at(0, 0, red, 25, white, 5) == EMPTY, "a small dot of colour is read as a piece");
    check(at(0, 0, Rgb{38, 46, 72}, 100, white, 0) == EMPTY, "the background is read as a piece");
    check(at(DEATH_X, DEATH_Y, mark, 85, white, 0) == EMPTY && at(DEATH_X, DEATH_Y, Rgb{200, 145, 66}, 85, white, 0) == EMPTY,
          "the mark where the pair appears is read as a piece");
    check(at(DEATH_X, DEATH_Y, red, 60, white, 20) == 1 && at(DEATH_X, DEATH_Y, green, 60, white, 0) == 2, "a piece where the mark is is not read");
    check(at(2, 4, red, 70, white, 0) == 1, "two red pieces joined (no white between them) are not read away from the mark");
    check(at(3, 1, white, 60, white, 0) == GARBAGE && at(3, 1, dim, 40, dim, 0) == GARBAGE, "garbage is not read");
    check(at(2, 6, Rgb{120, 230, 230}, 60, white, 0) == UNSURE && at(2, 6, Rgb{230, 150, 200}, 70, white, 0) == UNSURE,
          "a light of the screen over the field is not read as not sure");
    check(at(2, 6, Rgb{60, 60, 70}, 100, white, 0) == EMPTY, "the dark of the field is read as not sure");
    std::cout << "the measured table: pieces, small dots, the background, the mark where the pair appears and garbage read as they should\n";
}

// A reading with a point not sure (a light of the screen) takes nothing of that side's field: the pieces neither
// change nor stand still in it, no new pair is seen in it, and the frames after it go on as if it had not been. The
// notice is taken when it has read the same for Tracker::noticeFrames frames.
static void testUnclearFrames() {
    Reading clear;
    for (SideRead& side : clear.side) {
        for (auto& column : side.point)
            for (int k = 0; k < POINTS; ++k) column[k] = pointHeight(k) > 12 ? HIDDEN : EMPTY;
        side.next[0] = {2, 3}, side.next[1] = {4, 2};
        side.nextRead[0] = side.nextRead[1] = true;
    }
    clear.side[1].point[0][0] = clear.side[1].point[0][1] = clear.side[1].point[0][2] = clear.side[1].point[0][3] = 1;
    Reading fog = clear;
    fog.side[1].point[4][9] = UNSURE, fog.side[1].unsure = 1;
    Reading coming = clear;
    coming.side[1].point[SPAWN_X][23] = 3; // (the pair where pairs appear: its axis half a cell up, its child above the screen)
    Reading comingInFog = coming;
    comingInFog.side[1].point[4][9] = UNSURE, comingInFog.side[1].unsure = 1;
    Tracker t;
    int64_t at = 100;
    for (int f = 0; f < 5; ++f) t.step(clear, at++);
    const GameField before = t.side[1].resting;
    check(t.still(1) && before.f.get(0, 1) == 1, "a field read clear does not stand still");
    t.step(fog, at++);
    check(!t.still(1) && t.side[1].resting == before && !t.side[1].clear, "a reading with a point not sure is taken for the field");
    t.step(clear, at++);
    check(t.still(1), "after a reading with a point not sure the field does not stand still again at once");
    t.step(comingInFog, at++);
    check(!t.spawned[1] && t.side[1].pairs == 0, "a new pair is seen in a reading with a point not sure");
    t.step(coming, at++);
    check(t.spawned[1] && t.side[1].pairs == 1 && t.side[1].lastSeen.colors.axis == 3, "the new pair is not seen in the clear reading after it");
    // The notice: a value read for fewer frames than noticeFrames is not taken.
    Reading noted = clear;
    noted.side[0].notice = 720;
    t.step(noted, at++), t.step(clear, at++);
    check(t.side[0].notice == 0, "a notice read for one frame is taken");
    for (int f = 0; f < t.noticeFrames; ++f) t.step(noted, at++);
    check(t.side[0].notice == 720, "a notice read for noticeFrames frames is not taken");
    std::cout << "a reading with a point not sure takes nothing of the field and does not stop what follows; the notice is taken when it has stayed\n";
}

static void testPicture(const std::string& folder) {
    const Layout l = sampleLayout();
    Canvas c(301, 173, l); // (rows of an odd length)
    Rng rng(5);
    for (uint8_t& b : c.pixels) b = uint8_t(rng.below(256));
    for (size_t i = 3; i < c.pixels.size(); i += 4) c.pixels[i] = 255;
    const std::filesystem::path file = std::filesystem::path(folder) / "realgame-picture.bmp";
    check(saveBmp(file, c.picture()), "the picture is not saved");
    std::vector<uint8_t> back;
    int w = 0, h = 0;
    check(loadBmp(file, back, w, h) && w == c.width && h == c.height && back == c.pixels, "the picture saved and read again is another one");
    std::filesystem::remove(file);
    std::cout << "a picture saved and read again is the same\n";
}

int main(int argc, char** argv) {
    try {
        testTableText();
        testOneColour();
        testPointRules();
        testUnclearFrames();
        testPicture(argc > 1 ? argv[1] : ".");
        testRounds();
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
    std::cout << "all realgame tests passed\n";
    return 0;
}
