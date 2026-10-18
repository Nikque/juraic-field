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
//  5. The AI plays 2P of rounds of this program through their screen and a pad (Driver), its buttons reaching the round
//     `lag` frames after they are sent: every pair goes where the AI chose; with a pad that is now and then a frame
//     early or late, the pair is found elsewhere at times, put right and still brought where the AI chose. The lag
//     measured from the screen is that of the pad.
//  6. The record of a reading (play/realgame_log.h): the readings and the buttons written are read back the same, the
//     lines of events.txt are there, pictures are kept every PICTURE_EVERY frames and when asked for (at most one in
//     PICTURE_GAP frames, MAX_PICTURES in all) and written as they were; the driver noting what it does sends the same
//     buttons as without, and notes each pair begun, the placement chosen, a pair put right and the lag measured.
//  7. The real game's menus (play/realgame_menu.h): the screens of the players and of the characters and their states
//     told by the colours of their points at any size of the picture, nothing else taken for them; the pad of 2P
//     presses A while the row of 2P is to be taken or 2P is to decide, MenuPresser::GAP pictures apart.
//  4. A pair of one colour is read the way it was turned before; a picture saved and read again is the same.
//     Followed with the rules of the game (Follower): where each pair came to rest is found, the pieces reckoned are
//     those read as the next pair appears, and a notice of more than the chains can have sent is held back.
//  5. A point of the table measured on the real game (measuredLayout): a piece with its face and shine is read, a
//     small dot, the background and the mark where the pair appears (red or orange) are not, a piece over that mark is
//     (red with its shine, green without), pale grey (bright or dim) is garbage, a bright light of the screen of no
//     colour of a piece is UNSURE. A reading with such a point takes nothing of that side's field.
#ifdef _WIN32
// (as in play/main.cpp the headers come after those of Windows, whose macros - near, far, small - are no names there)
#define NOMINMAX
#include <windows.h>
#endif
#include "../ai/agents.h"
#include "../play/realgame_drive.h"
#include "../play/realgame_log.h"
#include "../play/realgame_menu.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

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
// quietLanding: a pair that has come to rest whole is drawn still where it lies (the real game shows it so until the
// next pair, a lock that starts no chain: its records), not as pieces that move.
static Drawn draw(Canvas& c, const Layout& l, const Round& r, bool ready, Rng& noise, bool quietLanding = false) {
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
        if (!ready && (p.phase == Phase::Split || (p.phase == Phase::Land && !quietLanding) || p.phase == Phase::Wave || p.phase == Phase::Garbage)) {
            // Something moves: a piece between two places, a piece at rest hidden.
            out.moving[s] = true;
            int h[W];
            p.field.heights(h);
            const int x = int(noise.below(W));
            if (h[x] < 11) {
                // (one after the other: the order in which the arguments of a call are worked out is not fixed by C++)
                const Cell colour = l.colors[noise.below(5)].cell;
                const float half = 0.5f * float(noise.below(4));
                piece(c, l, d, x, float(h[x]) + 1.f + half, colour);
            }
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
    long frames = 0, resting = 0, pairs = 0, nexts = 0, notices = 0, spawns = 0, observed = 0, otherInAir = 0, garbageShown = 0, movingFrames = 0, unclear = 0, moves = 0, placed = 0, checks = 0, misses = 0, held = 0;
};

// One frame of the round, the buttons of the AIs (null: random buttons).
static void stepRound(Round& r, ai::Agent* const agents[2], ai::Observation& o, Rng& hands, uint8_t held[2]) {
    uint8_t buttons[2];
    for (int s = 0; s < 2; ++s) {
        if (agents[s]) {
            ai::observe(r, s, o);
            buttons[s] = agents[s]->frame(o);
        } else {
            if (hands.below(4) == 0) {
                // (one after the other, as above)
                const uint8_t move = uint8_t(hands.below(32));
                const bool down = hands.below(3) != 0;
                held[s] = uint8_t(move | (down ? BTN_DOWN : 0));
            }
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
    Follower follow;
    const Tracker& tracker = follow.tracker;
    int64_t clock = 1000; // (the frames of the clock do not begin at 0)
    // The screen before the round: both fields empty, the windows standing.
    for (int f = 0; f < 45; ++f) {
        draw(canvas, l, r, true, noise);
        follow.step(read(canvas.picture(), l), clock++);
        check(!tracker.spawned[0] && !tracker.spawned[1], "a pair seen before the round");
    }
    check(tracker.fromStart && tracker.knows(1, 0) && tracker.knows(1, 1) && !tracker.knows(1, 2), "the first two pairs are not learnt before the round");
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
        follow.step(got, clock);
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
            check(follow.observe(s, rules, o), where + "nothing is observed");
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
    n.moves += r.player[0].moves + r.player[1].moves, n.placed += follow.placements;
    n.checks += follow.checks, n.misses += follow.misses, n.held += follow.held;
}

static void testRounds() {
    const auto started = std::chrono::steady_clock::now();
    const Layout l = sampleLayout();
    ai::Params narrow;
    narrow.beamWidth = 30, narrow.beamDepth = 8, narrow.beamQueues = 3;
    Counts n;
    const int sizes[4][2] = {{BASE_WIDTH, BASE_HEIGHT}, {1920, 1080}, {2560, 1440}, {1280, 720}};
    for (int k = 0; k < 4; ++k) {
        const std::unique_ptr<ai::Agent> x3 = ai::makeAgent("x3", narrow), random = ai::makeAgent("random");
        check(x3 && random, "the agents x3 and random are not there");
        playAndRead(l, sizes[k][0], sizes[k][1], 31 + uint64_t(k), x3.get(), random.get(), 2500, n);
        playAndRead(l, sizes[k][0], sizes[k][1], 41 + uint64_t(k), nullptr, random.get(), 1500, n);
    }
    check(n.spawns > 300 && n.otherInAir > 50 && n.garbageShown > 50 && n.movingFrames > 1000 && n.unclear > 300, "the rounds read show too little");
    check(n.placed * 10 >= n.moves * 9, "the placements of fewer than nine pairs in ten are found: " + std::to_string(n.placed) + " of " + std::to_string(n.moves));
    check(n.checks > 200 && n.misses == 0, "the pieces reckoned are not those read as the next pair appears: " + std::to_string(n.misses) + " of " + std::to_string(n.checks));
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::cout << "rounds drawn at four sizes and read (" << n.frames << " frames, " << seconds << " s): the pieces at rest " << n.resting << " times, the pair in play " << n.pairs
              << ", the window " << n.nexts << ", the notice " << n.notices << " (" << n.garbageShown << " with garbage); " << n.spawns
              << " new pairs seen in the frame before they can be moved, and observed as the round shows them (the other side's pair in the air "
              << n.otherInAir << " times); " << n.unclear << " readings of a side under a light, not taken; the placements of " << n.placed << " pairs of "
              << n.moves << " found, the pieces reckoned as read " << n.checks - n.misses << " times of " << n.checks << ", " << n.held << " notices held back\n";
}

// The window of one side read as other pairs of the round. On the real game the person's side (1P) counted its pairs
// three short after pieces read wrong (its thirteenth record, fourth round) and learnt what its window showed at the
// wrong places; 2P, which is given the same pairs, then waited for colours that did not come, counted none of its
// pairs from its fifteenth on, and four of them fell with nothing played. Each side follows the pairs its own window
// showed; those the other side learnt are taken only where it has learnt none.
static void testSidesLearnApart() {
    const Layout l = sampleLayout();
    const Rules rules;
    ai::Params narrow;
    narrow.beamWidth = 30, narrow.beamDepth = 8, narrow.beamQueues = 3;
    int seen = 0, missed = 0, shifted = 0;
    for (const uint64_t seed : {77, 78}) {
        const std::unique_ptr<ai::Agent> one = ai::makeAgent("x3", narrow), two = ai::makeAgent("x3", narrow);
        ai::Agent* agents[2] = {one.get(), two.get()};
        Round r;
        r.start(rules, seed);
        for (int s = 0; s < 2; ++s) agents[s]->startRound(s, seed + uint64_t(s));
        Canvas canvas(1280, 720, l);
        Rng noise(seed, 7), hands(seed, 8);
        Follower follow;
        const Tracker& t = follow.tracker;
        int64_t clock = 1000;
        for (int f = 0; f < 45; ++f, ++clock) {
            draw(canvas, l, r, true, noise);
            follow.step(read(canvas.picture(), l), clock);
        }
        uint8_t held[2] = {};
        ai::Observation o;
        for (int f = 0; f < 2500 && r.state == RoundState::Playing; ++f, ++clock) {
            const Drawn shown = draw(canvas, l, r, false, noise);
            // (1P's window from its sixth pair on: the pairs three after those the round shows there)
            const Player& p1 = r.player[0];
            if (p1.nextPair >= 6 && shown.nextShown[0]) {
                const SideLayout& d = l.side[0];
                const int first = p1.phase == Phase::Next && p1.wait <= r.timing.nextShown ? 1 : 0;
                const float size = d.cellWidth * 0.35f;
                for (int n = 0; n < 2; ++n) {
                    const Pair q = r.tsumo.pair(p1.nextPair + first + n + 3);
                    for (int k = 0; k < 2; ++k)
                        canvas.fill(d.next[n][k].x - size, d.next[n][k].y - size, d.next[n][k].x + size, d.next[n][k].y + size, colorOf(l, k ? q.child : q.axis));
                }
                ++shifted;
            }
            follow.step(read(canvas.picture(), l), clock);
            const Player& p2 = r.player[1];
            if (p2.phase == Phase::Next && p2.wait == 1 && !p2.field.dead()) {
                ++seen;
                missed += !(t.spawned[1] && t.side[1].pairs == p2.nextPair + 1);
            }
            stepRound(r, agents, o, hands, held);
        }
    }
    check(shifted > 500 && seen >= 40 && missed == 0,
          "with the window of 1P read as other pairs, pairs of 2P not counted as they appear: " + std::to_string(missed) + " of " + std::to_string(seen));
    std::cout << "the window of 1P read as other pairs: the " << seen << " pairs of 2P counted as they appear\n";
}

// ---- 4. small things ---------------------------------------------------------------------------------------------

static void testOneColour() {
    // A pair of one colour upside down, in the air: read so while it was so before, and the other way round when it was.
    SideRead s;
    for (auto& column : s.point)
        for (int k = 0; k < play::realgame::POINTS; ++k) column[k] = k >= 25 ? HIDDEN : EMPTY;
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
    // The eyes of a face of joined red pieces, under the shine: the strong colour lighter than the table's red (mean
    // 219 67 64 in a picture of the real game, 85 away from it), with the piece's warm white face. As not sure, the
    // reading of that side was taken by Tracker in none of 313 frames, and four pairs fell with nothing played.
    const Rgb shine{219, 67, 64}, face{250, 238, 230}, yellow{233, 143, 21};
    // Both points of a cell, drawn as `at` draws one: the lower point, then the upper one.
    const auto cell = [&](int x, int y, Rgb low, int lowShare, Rgb high, int highShare, int whiteShare) {
        Canvas c(2000, 1125, l);
        const float half = l.patch, width = 2 * half + 1;
        const Spot a = pointSpot(d, x, 2 * y), b = pointSpot(d, x, 2 * y + 1);
        c.fill(a.x - 3 * half, b.y - 3 * half, a.x + 3 * half, a.y + 3 * half, Rgb{38, 46, 72});
        for (const auto& [p, colour, share] : {std::tuple{a, low, lowShare}, std::tuple{b, high, highShare}}) {
            const float from = p.x - half;
            c.fill(from, p.y - half, from + width * float(share) / 100, p.y + half + 1, colour);
            if (share) c.fill(from + width * float(share) / 100, p.y - half, from + width * float(share + whiteShare) / 100, p.y + half + 1, face);
        }
        const SideRead r = read(c.picture(), l).side[1];
        return std::pair{r.point[x][2 * y], r.point[x][2 * y + 1]};
    };
    check(cell(0, 1, red, 50, shine, 40, 40) == std::pair<Cell, Cell>{1, 1}, "the eyes of a face of red pieces (the strong colour lighter than the table) are not read as red");
    // (lighter still, 237 77 74: 108 from the red of the table and 85 from its yellow; by its hue it is red. A point
    //  on the border of a yellow piece and a green one, 165 202 62, is near neither by its hue)
    check(cell(3, 1, red, 50, Rgb{237, 77, 74}, 30, 50) == std::pair<Cell, Cell>{1, 1}, "a red piece lit lighter still is read as yellow");
    check(at(3, 1, Rgb{165, 202, 62}, 25, face, 50) == UNSURE, "a point between a yellow piece and a green one is read as a piece");
    // (the hue alone is not enough: the edge of a red piece above the screen over the face of a yellow one, and words
    //  the screen draws over the field, have the hue of a colour too; the other point of the cell has to be a piece of
    //  that colour)
    check(cell(2, 11, yellow, 50, shine, 40, 40) == std::pair<Cell, Cell>{4, UNSURE}, "the edge of a red piece over the face of a yellow one is read as red");
    check(cell(2, 3, shine, 40, Rgb{38, 46, 72}, 0, 40) == std::pair<Cell, Cell>{UNSURE, EMPTY}, "a lit red with no piece in the rest of its cell is read as red");
    check(at(2, 6, shine, 70, white, 0) == UNSURE && at(2, 6, shine, 70, Rgb{235, 235, 245}, 20) == UNSURE,
          "a bright colour near no colour of the table, without the warm white of a piece's face, is read as a piece");
    check(at(DEATH_X, DEATH_Y, Rgb{227, 64, 64}, 85, white, 0) == EMPTY && at(DEATH_X, DEATH_Y, Rgb{222, 63, 61}, 85, white, 0) == EMPTY,
          "the mark where the pair appears, in its lighter reds, is read as a piece");
    std::cout << "the measured table: pieces, small dots, the background, the mark where the pair appears and garbage read as they should\n";
}

// A reading with a point not sure (a light of the screen) takes nothing of that side's field: the pieces neither
// change nor stand still in it, no new pair is seen in it, and the frames after it go on as if it had not been. The
// notice is taken when it has read the same for Tracker::noticeFrames frames.
static void testUnclearFrames() {
    Reading clear;
    for (SideRead& side : clear.side) {
        for (auto& column : side.point)
            for (int k = 0; k < play::realgame::POINTS; ++k) column[k] = pointHeight(k) > 12 ? HIDDEN : EMPTY;
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
    // A new pair hidden by a light as it appeared: taken when it is seen after it, moved already; not without the light.
    Reading empty = clear;
    for (auto& column : empty.side[1].point)
        for (int k = 0; k < play::realgame::POINTS; ++k) column[k] = pointHeight(k) > 12 ? HIDDEN : EMPTY;
    Reading moved = empty;
    moved.side[1].point[3][21] = moved.side[1].point[3][22] = 2, moved.side[1].point[3][23] = 3; // (axis 2 half a cell up in the fourth column, child 3 above it)
    Reading movedInFog = moved;
    movedInFog.side[1].point[0][3] = UNSURE, movedInFog.side[1].unsure = 1;
    for (int light = 0; light < 2; ++light) {
        Tracker u;
        for (int f = 0; f < u.roundGap + 5; ++f) u.step(empty, at++);
        check(u.fromStart && u.knows(1, 0) && u.pairOf(1, 0).axis == 2 && u.pairOf(1, 0).child == 3, "the first pairs are not learnt before the round");
        if (light) u.step(movedInFog, at++);
        u.step(moved, at++);
        check(u.spawned[1] == (light == 1) && u.late[1] == (light == 1) && u.side[1].pairs == light, light ? "a pair hidden as it appeared is not taken when seen after the light"
                                                                                                          : "a pair away from where pairs appear is taken for a new one without a light");
    }
    std::cout << "a reading with a point not sure takes nothing of the field and does not stop what follows; the notice is taken when it has stayed\n";
}

// What the real game showed (its record of 2026-10-16): a pair just appeared, moved at once, its axis read as another
// colour in one picture - it is still the pair in hand, not a new one. And the window of the next pairs moves for some
// frames as a pair appears, reading as other pairs on its way: the pairs after a new one are learnt from the window once
// it stands still.
static void testPairsSeenOnce() {
    Reading empty;
    for (SideRead& side : empty.side) {
        for (auto& column : side.point)
            for (int k = 0; k < play::realgame::POINTS; ++k) column[k] = pointHeight(k) > 12 ? HIDDEN : EMPTY;
        side.next[0] = {1, 5}, side.next[1] = {3, 1};
        side.nextRead[0] = side.nextRead[1] = true;
    }
    const auto window = [](Reading r, Pair next, Pair after) {
        for (SideRead& side : r.side) side.next[0] = next, side.next[1] = after;
        return r;
    };
    int64_t at = 5000;
    Tracker t;
    for (int f = 0; f < t.roundGap + 5; ++f) t.step(empty, at++);
    check(t.knows(1, 0) && t.knows(1, 1) && t.pairOf(1, 0).axis == 1 && t.pairOf(1, 1).axis == 3, "the first pairs are not learnt before the round");
    // The first pair of 1P appears (axis 1 half a cell up where pairs appear, its child above the screen) as the window
    // moves on: on its way it reads as 3 1 / 4 4, then stands still at 3 1 / 2 2.
    Reading coming = window(empty, {3, 4}, {4, 4});
    coming.side[0].point[SPAWN_X][23] = 1;
    coming.side[1] = empty.side[1];
    t.step(coming, at++);
    check(t.spawned[0] && t.side[0].pairs == 1, "the first pair is not seen");
    Reading wrong = window(coming, {3, 1}, {2, 2}); // (the axis read in one picture as the colour of the pair that comes next)
    wrong.side[0].point[SPAWN_X][23] = 3;
    t.step(wrong, at++);
    Reading moved = window(empty, {3, 1}, {2, 2});
    moved.side[0].point[SPAWN_X - 1][23] = 1;
    for (int f = 0; f < 3; ++f) t.step(moved, at++);
    check(t.side[0].pairs == 1, "a pair in hand read as another colour in one picture is taken for a new pair (" + std::to_string(t.side[0].pairs) + " pairs)");
    check(t.knows(1, 1) && t.knows(1, 2) && t.pairOf(1, 1).axis == 3 && t.pairOf(1, 1).child == 1 && t.pairOf(1, 2).axis == 2 && t.pairOf(1, 2).child == 2 && t.doubts == 0,
          "the pairs after the new one are learnt from the window on its way (" + std::to_string(t.doubts) + " readings did not fit)");
    // Between two rounds both fields stand empty, a point read as a piece in one picture of it (a light): the next round
    // is seen begin all the same.
    Tracker u;
    for (int f = 0; f < u.roundGap + 5; ++f) u.step(empty, at++);
    Reading played = empty;
    played.side[0].point[0][0] = played.side[0].point[0][1] = played.side[1].point[2][0] = played.side[1].point[2][1] = 4;
    Reading first = empty;
    first.side[0].point[SPAWN_X][23] = 1; // (the round has a pair: a round ends only after one)
    u.step(first, at++);
    for (int f = 0; f < 40; ++f) u.step(played, at++);
    const int rounds = u.rounds;
    Reading stray = empty;
    stray.side[0].point[4][9] = 2;
    for (int f = 0; f < u.roundGap / 2; ++f) u.step(empty, at++);
    u.step(stray, at++);
    for (int f = 0; f < u.roundGap / 2 + 5; ++f) u.step(empty, at++);
    check(u.rounds == rounds + 1, "a point read as a piece in one picture between two rounds hides the round that begins");
    // A pair that appears under a light, first seen when the light is gone half a cell below where pairs appear, its
    // child still above the screen, falling by itself: it is the pair that came. (On the real game, after a chain, such a
    // pair was taken for none - its own points counted as pieces falling, there being no pair in hand to leave them out -
    // and no pair of 2P was seen for the rest of the round: 20 seconds that nothing played.)
    {
        Tracker v;
        for (int f = 0; f < v.roundGap + 5; ++f) v.step(empty, at++);
        Reading still = empty;
        still.side[1].point[0][0] = still.side[1].point[0][1] = 4; // (a piece at rest)
        for (int f = 0; f < 10; ++f) v.step(still, at++);
        Reading lit = still;
        lit.side[1].unsure = 3;
        for (int f = 0; f < 4; ++f) v.step(lit, at++);
        Reading pair = still;
        pair.side[1].point[SPAWN_X][22] = pair.side[1].point[SPAWN_X][23] = 1; // (the axis, 1, a cell below the top; its child above the screen)
        for (int f = 0; f < 6; ++f) v.step(pair, at++);
        check(v.side[1].pairs == 1 && v.side[1].inHand, "a pair first seen half a cell below where pairs appear after a light, its child above the screen, is not seen");
    }
    std::cout << "a pair read as another colour in one picture is the same pair; the window of the next pairs is read once it stands still; a round is seen begin through a point read wrong in one picture; a pair first seen after a light, its child above the screen, is the pair that came\n";
}

// The screens before a round (the title, the menus) may read as two empty fields and a window standing still. On the
// real game (a record of a reading) the first two pairs were learnt so from a menu, the first pair of the round, of
// other colours, was taken for none, and nothing played 2P through the whole round. The window taken is the one that
// stands still last before the first pair: one read for a few pictures (moving) is not; the window moved on as the
// first pair comes (the second pair first) gives the third. A first pair of other colours than those learnt, seen
// where pairs appear in two clear readings in a row, is the first pair all the same; seen in one only, it is none.
static void testWindowBeforeRound() {
    Reading empty;
    for (SideRead& side : empty.side) {
        for (auto& column : side.point)
            for (int k = 0; k < play::realgame::POINTS; ++k) column[k] = pointHeight(k) > 12 ? HIDDEN : EMPTY;
        side.nextRead[0] = side.nextRead[1] = true;
    }
    const auto window = [](Reading r, Pair next, Pair after) {
        for (SideRead& side : r.side) side.next[0] = next, side.next[1] = after;
        return r;
    };
    const auto is = [](const Tracker& t, int i, Pair p) { return t.knows(1, i) && t.pairOf(1, i).axis == p.axis && t.pairOf(1, i).child == p.child; };
    const Reading menu = window(empty, {1, 4}, {4, 4});
    Reading lit = menu;
    lit.side[0].unsure = lit.side[1].unsure = 5;
    Reading unread = empty;
    for (SideRead& side : unread.side) side.nextRead[0] = side.nextRead[1] = false;
    int64_t at = 20000;
    {
        Tracker t;
        for (int f = 0; f < t.roundGap + 5; ++f) t.step(menu, at++);
        check(is(t, 0, {1, 4}) && is(t, 1, {4, 4}), "the window of a screen before the round is not learnt as it was on the real game");
        for (int f = 0; f < 20; ++f) t.step(lit, at++);
        // The round's screen: the window stands at 5 5 / 3 5, reads for three pictures as 5 5 / 5 5 and 3 3 / 3 3 (moving).
        for (int f = 0; f < 20; ++f) t.step(window(empty, {5, 5}, {3, 5}), at++);
        for (int f = 0; f < 3; ++f) t.step(window(empty, {5, 5}, {5, 5}), at++);
        for (int f = 0; f < 3; ++f) t.step(window(empty, {3, 3}, {3, 3}), at++);
        for (int f = 0; f < 20; ++f) t.step(window(empty, {5, 5}, {3, 5}), at++);
        check(is(t, 0, {5, 5}) && is(t, 1, {3, 5}), "the first pairs are not those of the window that stood last before the round");
        // The window moves on as the first pair comes, and stands at 3 5 / 5 1 for nine pictures before it appears.
        for (int f = 0; f < 9; ++f) t.step(window(empty, {3, 5}, {5, 1}), at++);
        check(is(t, 0, {5, 5}) && is(t, 1, {3, 5}) && is(t, 2, {5, 1}), "the window moved on before the first pair is taken for the first pairs");
        Reading first = window(empty, {3, 5}, {5, 1});
        first.side[0].point[SPAWN_X][23] = first.side[1].point[SPAWN_X][23] = 5; // (the axis half a cell up where pairs appear, its child above the screen)
        t.step(first, at++);
        check(t.spawned[0] && t.spawned[1] && t.side[0].pairs == 1 && t.side[1].pairs == 1, "the first pair of a round is not seen after a window learnt on a screen before it");
    }
    {
        // No window read on the round's screen: the first pair, of other colours than those learnt, is the first pair.
        Tracker t;
        for (int f = 0; f < t.roundGap + 5; ++f) t.step(menu, at++);
        for (int f = 0; f < 20; ++f) t.step(lit, at++);
        for (int f = 0; f < 20; ++f) t.step(unread, at++);
        Reading first = unread;
        first.side[0].point[SPAWN_X][23] = first.side[1].point[SPAWN_X][23] = 5;
        t.step(first, at++);
        check(!t.spawned[0] && !t.spawned[1], "a first pair of other colours than those learnt is taken in one reading");
        const int64_t seen = at - 1;
        t.step(first, at++);
        check(t.side[0].pairs == 1 && t.side[1].pairs == 1 && t.side[1].inHand && t.side[1].spawnedAt == seen,
              "a first pair of other colours than those learnt, seen where pairs appear in two readings, is not the first pair");
        check(t.side[1].colors.axis == 5 && !is(t, 0, {1, 4}), "the pairs learnt on a screen before the round are kept after its first pair showed them wrong");
    }
    {
        // ... seen in one reading only (a light, a point read wrong): no pair, and what was learnt stays.
        Tracker t;
        for (int f = 0; f < t.roundGap + 5; ++f) t.step(menu, at++);
        Reading stray = menu;
        stray.side[1].point[SPAWN_X][23] = 5;
        t.step(stray, at++);
        for (int f = 0; f < 10; ++f) t.step(menu, at++);
        check(t.side[1].pairs == 0 && is(t, 0, {1, 4}), "a point read where pairs appear in one reading is taken for the first pair");
    }
    std::cout << "the window taken before a round is the one that stood last; the first pair of other colours than learnt is seen in two readings\n";
}

// After garbage fell under a light (on the real game: the other side's chain), the next pair was first seen in a clear
// reading half a cell below where pairs appear, both pieces showing, of the colours of the pair that comes: it is the
// pair that came. (Its own pieces, with no pair in hand, counted as pieces above the field, and the pieces that fell
// before the light were still counted so in the first reading after it: no pair of 2P was seen to the end of the round.)
// And the window moves on before a pair appears: the pair after the two it showed is learnt then.
static void testPairsAfterLight() {
    Reading empty;
    for (SideRead& side : empty.side) {
        for (auto& column : side.point)
            for (int k = 0; k < play::realgame::POINTS; ++k) column[k] = pointHeight(k) > 12 ? HIDDEN : EMPTY;
        side.next[0] = {1, 5}, side.next[1] = {3, 1};
        side.nextRead[0] = side.nextRead[1] = true;
    }
    const auto window = [](Reading r, Pair next, Pair after) {
        for (SideRead& side : r.side) side.next[0] = next, side.next[1] = after;
        return r;
    };
    const auto is = [](const Tracker& t, int i, Pair p) { return t.knows(1, i) && t.pairOf(1, i).axis == p.axis && t.pairOf(1, i).child == p.child; };
    int64_t at = 40000;
    Tracker t;
    for (int f = 0; f < t.roundGap + 5; ++f) t.step(empty, at++);
    // The first pair of 2P (1 5) appears; the window moves on to 3 1 / 2 2 and stands.
    Reading first = window(empty, {3, 1}, {2, 2});
    first.side[1].point[SPAWN_X][23] = 1;
    for (int f = 0; f < 4; ++f) t.step(first, at++);
    check(t.side[1].pairs == 1 && is(t, 1, {3, 1}) && is(t, 2, {2, 2}), "the pairs after the first one are not learnt");
    // It comes to rest in the first column; then, before the next pair appears, the window moves on to 2 2 / 4 5.
    Reading rest = window(empty, {3, 1}, {2, 2});
    rest.side[1].point[0][0] = rest.side[1].point[0][1] = 1, rest.side[1].point[0][2] = rest.side[1].point[0][3] = 5;
    for (int f = 0; f < 30; ++f) t.step(rest, at++);
    for (int f = 0; f < 3; ++f) t.step(window(rest, {2, 2}, {4, 5}), at++);
    check(t.side[1].pairs == 1 && is(t, 3, {4, 5}), "the pair the window shows as it moves on before a pair appears is not learnt");
    // Garbage falls (pieces above the field), under a light it comes to rest, and the next pair (3 1) is first seen
    // after the light a cell and a half below where pairs appear, both pieces showing.
    Reading falling = window(rest, {2, 2}, {4, 5});
    for (int x = 1; x < W; ++x) falling.side[1].point[x][12] = falling.side[1].point[x][13] = GARBAGE;
    for (int f = 0; f < 3; ++f) t.step(falling, at++);
    Reading landed = window(rest, {2, 2}, {4, 5});
    for (int x = 1; x < W; ++x) landed.side[1].point[x][0] = landed.side[1].point[x][1] = GARBAGE;
    Reading lit = landed;
    lit.side[1].unsure = 4;
    for (int f = 0; f < 6; ++f) t.step(lit, at++);
    Reading seen = landed;
    seen.side[1].point[SPAWN_X][19] = seen.side[1].point[SPAWN_X][20] = 3; // (the axis, its child above it)
    seen.side[1].point[SPAWN_X][21] = seen.side[1].point[SPAWN_X][22] = 1;
    for (int f = 0; f < 4; ++f) t.step(seen, at++);
    check(t.side[1].pairs == 2 && t.side[1].inHand && t.side[1].late, "a pair first seen after a light below where pairs appear, both pieces showing, is not seen");
    check(t.doubts == 0, "the pairs learnt did not fit (" + std::to_string(t.doubts) + " readings)");
    // That pair comes to rest on the garbage; the next one (2 2) appears where pairs appear, a piece still read above
    // the field in the reading before: it is seen in its second reading there, as the pair that came in the first.
    Reading rested = landed;
    rested.side[1].point[SPAWN_X][2] = rested.side[1].point[SPAWN_X][3] = 3, rested.side[1].point[SPAWN_X][4] = rested.side[1].point[SPAWN_X][5] = 1;
    for (int f = 0; f < 10; ++f) t.step(rested, at++);
    Reading leftover = rested;
    leftover.side[1].point[4][14] = leftover.side[1].point[4][15] = GARBAGE; // (a piece above the field)
    t.step(leftover, at++);
    Reading next = rested;
    next.side[1].point[SPAWN_X][23] = 2; // (the axis half a cell up where pairs appear, its child above the screen)
    const int64_t came = at;
    t.step(next, at++);
    t.step(next, at++);
    check(t.side[1].pairs == 3 && t.side[1].inHand && t.side[1].spawnedAt == came, "a pair where pairs appear, a piece above the field in the reading before, is not seen in its second reading");
    std::cout << "a pair first seen after a light, both pieces showing, is the pair that came; the pair the window shows as it moves on is learnt\n";
}

// A pair of 2P is never left alone. (1) A pair put out of sight - both pieces in the 13th and 14th rows - changes nothing
// the screen shows, and the next pair, of the same colour below, appears where it appeared: once the window has moved
// on after the pair in hand appeared, that pair is put (on the real game, its thirteenth record, fifth round, the next
// pair was taken for it, still where pairs appear, and fell with nothing played). (2) A pair where pairs appear, after
// the window moved on, of other colours than those learnt (a window read wrong): seen there in two readings in a row
// over pieces standing still, it is the pair that came, of the colours read.
static void testPairsNeverLeft() {
    Reading empty;
    for (SideRead& side : empty.side) {
        for (auto& column : side.point)
            for (int k = 0; k < play::realgame::POINTS; ++k) column[k] = pointHeight(k) > 12 ? HIDDEN : EMPTY;
        side.next[0] = {5, 4}, side.next[1] = {5, 3};
        side.nextRead[0] = side.nextRead[1] = true;
    }
    // (the fifth column twelve high)
    for (int k = 0; k < 24; ++k) empty.side[1].point[4][k] = k / 2 % 2 ? 1 : 2;
    const auto window = [](Reading r, Pair next, Pair after) {
        for (SideRead& side : r.side) side.next[0] = next, side.next[1] = after;
        return r;
    };
    int64_t at = 50000;
    Tracker t;
    for (int f = 0; f < t.roundGap + 5; ++f) t.step(empty, at++);
    // The first pair (5 4) appears, its child above the screen; the window moves on to 5 3 / 2 2.
    Reading first = window(empty, {5, 3}, {2, 2});
    first.side[1].point[SPAWN_X][23] = 5;
    for (int f = 0; f < 4; ++f) t.step(first, at++);
    check(t.side[1].pairs == 1 && t.side[1].inHand, "the first pair is not seen");
    // It goes up the fifth column, out of sight (the screen shows no change), and is put there.
    Reading gone = window(empty, {5, 3}, {2, 2});
    for (int f = 0; f < 40; ++f) t.step(gone, at++);
    // The window moves on to 2 2 / 4 1, and the next pair (5 3) appears where pairs appear, its child above the screen.
    for (int f = 0; f < 6; ++f) t.step(window(empty, {2, 2}, {4, 1}), at++);
    Reading second = window(empty, {2, 2}, {4, 1});
    second.side[1].point[SPAWN_X][23] = 5;
    for (int f = 0; f < 3; ++f) t.step(second, at++);
    check(t.side[1].pairs == 2 && t.side[1].inHand, "a pair after one put out of sight, where pairs appear as that one did, is not seen");
    // It comes to rest in the first column. The window moves on to 4 1 / 3 3 and the next pair appears: 4 1 for the
    // window read before, but of another axis on the screen (3: as if the window had been read wrong).
    Reading rest = window(empty, {2, 2}, {4, 1});
    rest.side[1].point[0][0] = rest.side[1].point[0][1] = 5, rest.side[1].point[0][2] = rest.side[1].point[0][3] = 3;
    for (int f = 0; f < 30; ++f) t.step(rest, at++);
    for (int f = 0; f < 6; ++f) t.step(window(rest, {4, 1}, {3, 3}), at++);
    Reading other = window(rest, {4, 1}, {3, 3});
    other.side[1].point[SPAWN_X][23] = 3;
    for (int f = 0; f < 3; ++f) t.step(other, at++);
    check(t.side[1].pairs == 3 && t.side[1].inHand && t.side[1].colors.axis == 3,
          "a pair of other colours than learnt, where pairs appear after the window moved on, is not seen as the pair that came");
    std::cout << "a pair of 2P is never left alone: after one put out of sight, and of other colours than learnt\n";
}

// The real game shows twelve rows; a piece rests in the 13th too (the AI builds into it). A pair put there, one piece
// in the 12th row of one column and the other in the 13th of a column twelve high, is found (the screen does not tell
// it from the pair standing on the second column, its child in the 13th row there), and a piece in the 13th row stays
// in the field followed and in what the AI is shown, as the next pair appears. The driver takes the 13th and 14th rows
// of a column from its engine, which put the pair, where the twelve rows the screen shows are the engine's.
static void testRow13Kept() {
    Reading empty;
    for (SideRead& side : empty.side) {
        for (auto& column : side.point)
            for (int k = 0; k < play::realgame::POINTS; ++k) column[k] = pointHeight(k) > 12 ? HIDDEN : EMPTY;
        side.next[0] = {5, 5}, side.next[1] = {1, 2};
        side.nextRead[0] = side.nextRead[1] = true;
    }
    const auto window = [](Reading r, Pair next, Pair after) {
        for (SideRead& side : r.side) side.next[0] = next, side.next[1] = after;
        return r;
    };
    Follower f;
    int64_t at = 50000;
    for (int k = 0; k < f.tracker.roundGap + 5; ++k) f.step(empty, at++);
    // 2P's field: the first column twelve high, the second eleven (two colours in turn: nothing clears).
    Reading tall = empty;
    for (int y = 0; y < 12; ++y) tall.side[1].point[0][2 * y] = tall.side[1].point[0][2 * y + 1] = Cell(1 + y % 2);
    for (int y = 0; y < 11; ++y) tall.side[1].point[1][2 * y] = tall.side[1].point[1][2 * y + 1] = Cell(3 + y % 2);
    for (int k = 0; k < 5; ++k) f.step(tall, at++);
    // The first pair (5 5) appears, and comes to rest lying across the first two columns: one piece in the 12th row of
    // the second column, the other in the 13th of the first, which the screen does not show.
    Reading first = window(tall, {1, 2}, {3, 3});
    first.side[1].point[SPAWN_X][23] = 5;
    for (int k = 0; k < 3; ++k) f.step(first, at++);
    check(f.tracker.side[1].pairs == 1, "the first pair is not seen");
    Reading landed = window(tall, {1, 2}, {3, 3});
    landed.side[1].point[1][22] = landed.side[1].point[1][23] = 5;
    for (int k = 0; k < 6; ++k) f.step(landed, at++);
    // The next pair (1 2) appears.
    Reading next = window(landed, {3, 3}, {4, 4});
    next.side[1].point[SPAWN_X][23] = 1;
    f.step(next, at++);
    check(f.tracker.spawned[1] && f.tracker.side[1].pairs == 2, "the second pair is not seen");
    check(f.model[1].followed == 0, "a pair put with a piece in the 13th row is not found where it was put");
    const auto row13 = [](const GameField& g) {
        int n = 0;
        for (int x = 0; x < W; ++x) n += g.f.get(x, 12) == 5;
        return n;
    };
    check(row13(f.model[1].core.field) == 1, "the piece in the 13th row is not in the field followed");
    ai::Observation o;
    check(f.observe(1, f.rules, o) && row13(o.self().field) == 1, "the piece in the 13th row is not in what the AI is shown");
    // The engine put the pair lying, its child in the 13th row of the first column; its row 14 holds a piece the screen
    // knows nothing of. The fourth column the engine has otherwise than the screen: there it is the field followed.
    GameField engine = f.model[1].core.field;
    for (int x = 0; x < W; ++x) engine.f.set(x, 12, EMPTY);
    engine.f.set(0, 12, 5), engine.row14[0] = 3;
    GameField followed = f.model[1].core.field;
    followed.f.set(3, 0, 4);
    engine.f.set(3, 0, 2), engine.f.set(3, 1, 2);
    const GameField taken = Driver::hiddenFromEngine(followed, engine);
    check(taken.f.get(0, 12) == 5 && taken.f.get(1, 12) == EMPTY && taken.row14[0] == 3, "the driver does not take the 13th and 14th rows from its engine");
    check(taken.f.get(3, 0) == 4 && taken.f.get(3, 1) == EMPTY, "the driver takes a column from its engine that the screen shows otherwise");
    std::cout << "a pair put with a piece in the 13th row, which the screen does not show, is found and the piece kept\n";
}

// A pair lying with both pieces shown, then one picture of it with a piece missing (a piece that moves drawn over it,
// as in a lock or the first picture of a chain), where a placement with the other piece in the 13th row of a column
// twelve high fits too: the pair is still where it lay, and nothing is put in the 13th row. The same picture read in
// readings in a row is the pair as the screen shows it, its other piece in the 13th row.
static void testLyingPieceHidden() {
    for (const int partials : {1, 3}) {
        Reading empty;
        for (SideRead& side : empty.side) {
            for (auto& column : side.point)
                for (int k = 0; k < play::realgame::POINTS; ++k) column[k] = pointHeight(k) > 12 ? HIDDEN : EMPTY;
            side.next[0] = {5, 5}, side.next[1] = {1, 2};
            side.nextRead[0] = side.nextRead[1] = true;
        }
        const auto window = [](Reading r, Pair next, Pair after) {
            for (SideRead& side : r.side) side.next[0] = next, side.next[1] = after;
            return r;
        };
        Follower f;
        int64_t at = 50000;
        for (int k = 0; k < f.tracker.roundGap + 5; ++k) f.step(empty, at++);
        // 2P's field: the second column twelve high, the third two high, the fourth eleven (a pair lifted on it reaches
        // the 13th row of the second; colours in turn: nothing clears).
        Reading base = empty;
        for (int y = 0; y < 12; ++y) base.side[1].point[1][2 * y] = base.side[1].point[1][2 * y + 1] = Cell(1 + y % 2);
        for (int y = 0; y < 11; ++y) base.side[1].point[3][2 * y] = base.side[1].point[3][2 * y + 1] = Cell(1 + y % 2);
        for (int y = 0; y < 2; ++y) base.side[1].point[2][2 * y] = base.side[1].point[2][2 * y + 1] = Cell(3 + y % 2);
        for (int k = 0; k < 5; ++k) f.step(base, at++);
        // The first pair (5 5) appears and comes to rest upright on the third column, in its third and fourth rows.
        Reading first = window(base, {1, 2}, {3, 3});
        first.side[1].point[SPAWN_X][23] = 5;
        for (int k = 0; k < 3; ++k) f.step(first, at++);
        check(f.tracker.side[1].pairs == 1, "the first pair is not seen");
        Reading landed = window(base, {1, 2}, {3, 3});
        for (int k = 4; k < 8; ++k) landed.side[1].point[2][k] = 5;
        for (int k = 0; k < 4; ++k) f.step(landed, at++);
        check(f.model[1].lying && f.model[1].at.x == 2 && f.model[1].at.dir == UP, "the pair lying upright on the third column is not found");
        // Its upper piece not shown: as the pair lying across the second and third columns, one piece in the 13th row of
        // the second.
        Reading partial = landed;
        partial.side[1].point[2][6] = partial.side[1].point[2][7] = EMPTY;
        for (int k = 0; k < partials; ++k) f.step(partial, at++);
        // A piece at rest under it not shown (a piece that moves): the pair is locked where it lay.
        Reading locked = partials == 1 ? landed : partial;
        locked.side[1].point[1][22] = locked.side[1].point[1][23] = EMPTY;
        f.step(locked, at++);
        check(f.model[1].followed == 0, "the pair is not locked where it lay");
        for (int k = 0; k < 6; ++k) f.step(partials == 1 ? landed : partial, at++);
        Reading next = window(partials == 1 ? landed : partial, {3, 3}, {4, 4});
        next.side[1].point[SPAWN_X][23] = 1;
        f.step(next, at++);
        check(f.tracker.spawned[1] && f.tracker.side[1].pairs == 2, "the second pair is not seen");
        int row13 = 0;
        for (int x = 0; x < W; ++x) row13 += f.model[1].core.field.f.get(x, 12) != EMPTY;
        ai::Observation o;
        check(f.observe(1, f.rules, o), "nothing is observed as the second pair appears");
        if (partials == 1) {
            check(row13 == 0 && f.misses == 0, "one picture of the pair lying with a piece missing puts a piece in the 13th row");
            check(o.self().field.f == f.model[1].core.field.f && o.self().field.f.get(2, 3) == 5 && o.self().field.f.get(1, 12) == EMPTY,
                  "the AI is shown a piece in the 13th row that the pair lying did not put there");
        } else {
            check(row13 == 1 && f.model[1].core.field.f.get(1, 12) == 5 && f.misses == 0,
                  "the pair shown with a piece missing in readings in a row is not taken with its other piece in the 13th row");
        }
    }
    std::cout << "one picture of a pair lying with a piece missing does not move it into the 13th row; readings in a row do\n";
}

// The AI that plays 2P of the real game learns its third pair only as the pair appears (the window moves until then),
// so its second choice comes after the pair has set out: while it is on its way the pair goes straight down where that
// loses nothing (PlacementAgent::holdSecond) - for the AI the driver is given and the one that takes over from it.
static void testSecondChoiceHeld() {
    const std::unique_ptr<ai::Agent> given = ai::makeAgent("x3");
    Driver::prepare(given.get());
    const auto* placing = dynamic_cast<const ai::PlacementAgent*>(given.get());
    check(placing && placing->holdSecond, "the AI given to the driver does not go straight down while its second choice is on its way");
    Driver d;
    d.replaceAgent(ai::makeAgent("lv5"));
    Follower f;
    d.tick(f, 100);
    const auto* taken = dynamic_cast<const ai::PlacementAgent*>(d.agent.get());
    check(taken && taken->holdSecond, "the AI that takes over does not go straight down while its second choice is on its way");
    std::cout << "the AI playing 2P goes straight down while its second choice is on its way\n";
}

// A corrected field or known pair invalidates a choice, including a choice still running on another thread.
static void testChoiceAfterCorrection() {
    struct Choosing : ai::PlacementAgent {
        std::atomic<int> choices{0};
        std::atomic<bool> blocked{false}, entered{false};
        int choose(const ai::Observation& o, Pair pair, unsigned) override {
            ++choices;
            entered = true;
            while (blocked) std::this_thread::yield();
            return o.self().field.f.empty() && pair.axis == 1 ? 0 : 1;
        }
        ~Choosing() override { blocked = false; finish(); }
    };
    ai::Observation o;
    o.me = 1;
    o.tsumo.pairs = {{1, 2}, {3, 4}, {2, 1}};
    Player& p = o.player[1];
    p.phase = Phase::Next, p.wait = 8;
    Choosing a;
    a.ahead = 10;
    a.startRound(1, 1);
    a.frame(o);
    check(a.choices == 1, "the first choice was not made");
    p.field.f.set(5, 0, GARBAGE);
    p.wait = 1;
    a.frame(o);
    check(a.choices == 2 && a.targetPlacement() == 1, "a choice of the field before garbage fell is used on the corrected field");
    a.startRound(1, 2);
    p.field = {}, p.wait = 8;
    a.frame(o);
    o.tsumo.pairs[0] = {3, 2};
    p.wait = 1;
    a.frame(o);
    check(a.choices == 4 && a.targetPlacement() == 1, "a choice of other pair colours is used after the colours are corrected");
    // An unfinished choice of the old field cannot block the pad or be used when it returns.
    Choosing slow;
    slow.ahead = 10, slow.thinking = ai::Thinking::Free, slow.holdSecond = true;
    slow.startRound(1, 3);
    slow.blocked = true;
    o.tsumo.pairs[0] = {1, 2};
    p.field = {}, p.wait = 8;
    slow.frame(o);
    while (!slow.entered) std::this_thread::yield();
    p.field.f.set(5, 0, GARBAGE), p.wait = 1;
    const uint8_t held = slow.frame(o);
    check(held != 0, "the pair is left alone while a stale asynchronous choice is being replaced");
    p.phase = Phase::Control, p.nextPair = 1, p.colors = {1, 2}, p.pair = spawnPair(o.timing);
    for (int k = 0; k < 20; ++k)
        check(slow.frame(o) != 0, "the pair is left alone during an unfinished stale choice");
    slow.blocked = false;
    for (int k = 0; k < 100000 && slow.targetPlacement() < 0; ++k) {
        slow.frame(o);
        std::this_thread::yield();
    }
    check(slow.choices == 2 && slow.targetPlacement() == 1, "the asynchronous choice of the old field survives its correction");
    std::cout << "choices are renewed after the field or pair colours change, without waiting for an unfinished choice\n";
}

static void testViewsOfBothSides() {
    Tracker t;
    t.pairsOf[0] = {{1, 2}, {2, 3}, {3, 4}};
    t.pairsOf[1] = {{3, 4}, {4, 1}, {1, 2}};
    t.knownOf[0] = t.knownOf[1] = {1, 1, 1};
    ai::Observation o;
    for (int me = 0; me < 2; ++me) {
        observeWith(t, me, Rules{}, nullptr, 0, o);
        for (int s = 0; s < 2; ++s)
            for (int k = 0; k < 3; ++k) {
                const Pair got = o.pairsOf(s).pair(k), expected = t.pairOf(s, k);
                check(got.axis == expected.axis && got.child == expected.child, "the AI sees the other side's window through its own pair numbering");
            }
    }
    std::cout << "the AI sees the pairs of both sides as each side's window showed them\n";
}

static void testPredictionResetAtRound() {
    struct Watching : ai::PlacementAgent {
        GameField lastField;
        uint8_t frame(const ai::Observation& o) override {
            lastField = o.self().field;
            return PlacementAgent::frame(o);
        }
        int choose(const ai::Observation&, Pair, unsigned legal) override { return std::countr_zero(legal); }
    };
    Follower f;
    f.tracker.rounds = 1, f.tracker.roundStart = 0;
    f.tracker.pairsOf[1] = {{1, 2}, {3, 4}};
    f.tracker.knownOf[1] = {1, 1};
    f.tracker.side[1].pairs = 1;
    f.tracker.side[1].spawnedAt = 0;
    f.model[1].known = true;
    Driver d;
    auto agent = std::make_unique<Watching>();
    Watching* watching = agent.get();
    d.agent = std::move(agent);
    for (int at = 0; at < 100; ++at) {
        f.tracker.frame = at;
        d.tick(f, at);
    }
    check(watching->lastField.f.count() == 2, "the previous round has no predicted placement");
    f.tracker.rounds = 2, f.tracker.roundStart = 100, f.tracker.frame = 101;
    f.tracker.side[1] = {};
    f.model[1] = {};
    d.tick(f, 101);
    check(watching->lastField.f.empty(), "the AI is shown the previous round's predicted field after a new round begins");
    std::cout << "a new round discards the previous round's predicted field\n";
}

static void testChainAfterMissedLanding() {
    for (int scenario = 0; scenario < 3; ++scenario) {
        Reading r;
        for (SideRead& side : r.side) {
            for (auto& column : side.point)
                for (int k = 0; k < play::realgame::POINTS; ++k) column[k] = pointHeight(k) > 12 ? HIDDEN : EMPTY;
            side.next[0] = {1, 2}, side.next[1] = {3, 4};
            side.nextRead[0] = side.nextRead[1] = true;
        }
        Follower f;
        int64_t at = 50000;
        for (int k = 0; k < 40; ++k) f.step(r, at++);
        for (int k = 0; k < 6; ++k) r.side[1].point[0][k] = 1;
        if (scenario != 1)
            for (int k = 0; k < 12; ++k) r.side[1].point[1][k] = GARBAGE;
        for (int k = 0; k < 5; ++k) f.step(r, at++);
        r.side[1].next[0] = {3, 4}, r.side[1].next[1] = {2, 2};
        r.side[1].point[SPAWN_X][23] = 1;
        for (int k = 0; k < 4; ++k) f.step(r, at++);
        check(f.tracker.side[1].pairs == 1, "the firing pair was not seen");
        // The landing and the whole chain are hidden. Only its result and the next pair are seen.
        at += 120;
        for (int k = 0; k < 6; ++k) r.side[1].point[0][k] = k < 2 ? 2 : EMPTY;
        if (scenario == 2) r.side[1].point[0][0] = r.side[1].point[0][1] = 5;
        r.side[1].point[SPAWN_X][23] = 3;
        r.side[1].next[0] = {2, 2}, r.side[1].next[1] = {4, 4};
        for (int k = 0; k < 4; ++k) f.step(r, at++);
        check(f.tracker.side[1].pairs == 2, "the pair after the hidden chain was not seen");
        if (scenario != 0) {
            check(f.model[1].followed < 0, "an ambiguous or mismatching field was accepted as a recovered placement");
            continue;
        }
        check(f.model[1].followed == 0 && f.model[1].chains == 1, "a chain whose landing was hidden is lost even though its resulting field identifies the move");
        check(f.model[1].core.scoring.score == 40, "the hidden chain's score was not recovered");
    }
    std::cout << "a hidden landing is recovered from the next field only when its resolved move is identified\n";
}

// Before the pair appears, the AI sees only the pairs the screen has shown. The third pair is learnt afterwards.
static void testShownPairsOnly() {
    Reading empty;
    for (SideRead& side : empty.side) {
        for (auto& column : side.point)
            for (int k = 0; k < play::realgame::POINTS; ++k) column[k] = pointHeight(k) > 12 ? HIDDEN : EMPTY;
        side.next[0] = {2, 3}, side.next[1] = {4, 2};
        side.nextRead[0] = side.nextRead[1] = true;
    }
    Tracker t;
    int64_t at = 30000;
    for (int f = 0; f < t.roundGap + 5; ++f) t.step(empty, at++);
    // The first pair of 2P appears; the window moves on to 4 2 / 1 5 and stands there.
    Reading first = empty;
    for (SideRead& side : first.side) side.next[0] = {4, 2}, side.next[1] = {1, 5};
    first.side[1].point[SPAWN_X][23] = 2;
    for (int f = 0; f < 5; ++f) t.step(first, at++);
    check(t.side[1].pairs == 1 && t.knows(1, 2) && t.pairOf(1, 2).axis == 1 && t.pairOf(1, 2).child == 5, "the pairs after the first one are not learnt");
    // The driver's engine has the pair at rest and waits for the next (pair 1), 20 frames before it appears.
    Player own;
    own.phase = Phase::Next, own.wait = 20, own.nextPair = own.moves = 1;
    ai::Observation o;
    observeWith(t, 1, Rules{}, &own, at, o);
    check(o.mine().count() == 3, "the AI waiting for its pair is shown " + std::to_string(o.mine().count()) + " pairs, of which the screen has shown 3");
    for (int i = 0; i < o.mine().count(); ++i)
        check(o.mine().pair(i).axis == t.pairOf(1, i).axis && o.mine().pair(i).child == t.pairOf(1, i).child, "a pair the AI is shown is not the one the screen showed");
    std::cout << "the AI is shown the pairs the screen has shown and no other\n";
}

// The notice against what the chains followed can have sent (Follower): with nothing sent, a notice of a few pieces is
// taken at once, one of many (a light over it) only when it has read the same for noticeLong frames.
static void testNoticeCeiling() {
    Reading empty;
    for (SideRead& side : empty.side) {
        for (auto& column : side.point)
            for (int k = 0; k < play::realgame::POINTS; ++k) column[k] = pointHeight(k) > 12 ? HIDDEN : EMPTY;
        side.next[0] = {2, 3}, side.next[1] = {4, 2};
        side.nextRead[0] = side.nextRead[1] = true;
    }
    Reading coming = empty;
    coming.side[0].point[SPAWN_X][23] = 2; // (the first pair of 1P where pairs appear)
    Follower f;
    int64_t at = 10;
    for (int k = 0; k < f.tracker.roundGap + 5; ++k) f.step(empty, at++);
    f.step(coming, at++);
    check(f.tracker.spawned[0] && f.ceiling(1) == f.slack, "the notice of 2P has no ceiling while 1P has sent nothing");
    Reading lit = coming;
    lit.side[1].notice = 720;
    for (int k = 0; k < 10; ++k) f.step(lit, at++);
    check(f.notice[1] == 0 && f.tracker.side[1].notice == 720, "a notice of more than can have been sent is taken at once");
    for (int k = 0; k < f.noticeLong; ++k) f.step(lit, at++);
    check(f.notice[1] == 720 && f.held == 1, "a notice of more than can have been sent is not taken when it stays");
    Reading few = coming;
    few.side[1].notice = 2;
    for (int k = 0; k < f.tracker.noticeFrames; ++k) f.step(few, at++);
    check(f.notice[1] == 2 && f.held == 1, "a notice of less is not taken at once");
    std::cout << "the notice: more than the chains followed can have sent is held back until it stays\n";
}

struct DriveCounts {
    long frames = 0, locks = 0, hits = 0, corrected = 0, played = 0, lagRight = 0, lagRounds = 0;
    long lagTaken = 0;  // rounds that ended with the driver using the lag of the pad
    long turnsFound = 0; // rounds that ended with the driver turning the other way round (the pad turns so)
    long downAtSpawn = 0; // pairs of 2P that took the down button in the frame they appeared (sent before they were seen)
    long leftAlone = 0;   // times a pair of 2P in the air took no button for 10 frames in a row (left to fall by itself)
    long control = 0;     // frames a pair of 2P was in play (Phase::Control)
    std::vector<int64_t> appeared; // the frames of the clock pairs of 2P appeared in
};

// A round of this program whose 2P the AI plays through the screen and a pad `lag` frames late (shaky: now and then a
// frame sooner or later). startLag: the lag the driver begins with (-1: the pad's). swapped: the pad turns the other way
// round from what the driver sends (as a real game whose buttons are the other way round). turnShownLate: a pair turned
// is drawn the way it was for that many frames more (as the real game shows a turn: seen in its record). later: the
// AI given to the driver in frame laterAt of the round. hidden: a pair turned is not drawn for that many frames (the
// real game's pair is not to be read while it turns: seen in its record, 9 frames for two turns). movedHidden: a pair
// moved to the side is not drawn for that many frames (the real game's pair, between two columns in the frame it moves,
// is not to be read then: 92 moves of 97 in three of its records). quietLanding: as draw. skipOneIn: in one frame of
// that many (at random) the driver is not called and the pad holds what it held (the thread of the play a frame late:
// 4.6% of the frames of a record of the real game, the next frame called twice). crowdedLate: a turn taken in the frame
// of a move, or up to three frames after another turn, is shown that many frames late, the pair drawn the way it was
// before the first of them (the real game in its eighth record: the way before such a turn still shown 5 or 6 frames
// after it in a fifth of the pictures of those frames). litAsAppears: one pair of 2P in three lit from 8 frames before it
// appears, for that many frames. garbageAsLit: that many pieces of garbage fall on 2P before each of those pairs (sent
// as the pair before is locked: the engine of the driver, which knows of no garbage, foresees the pair sooner).
static void driveRound(uint64_t seed, int lag, bool shaky, int frames, DriveCounts& n, std::vector<std::pair<int64_t, std::string>>* notes = nullptr,
                       std::vector<uint8_t>* sentOut = nullptr, int startLag = -1, bool swapped = false, int turnShownLate = 0,
                       std::unique_ptr<ai::Agent> given = nullptr, bool keepLag = false, int lit = 0, std::unique_ptr<ai::Agent> later = nullptr,
                       int laterAt = 0, int hidden = 0, int movedHidden = 0, bool quietLanding = false, int skipOneIn = 0, int crowdedLate = 0,
                       int litAsAppears = 0, int garbageAsLit = 0) {
    const Layout l = sampleLayout();
    const Rules rules;
    Round r;
    r.start(rules, seed);
    Canvas canvas(1280, 720, l);
    Rng noise(seed, 7), shake(seed, 9), skipper(seed, 11);
    ai::Params narrow;
    narrow.beamWidth = 30, narrow.beamDepth = 8, narrow.beamQueues = 3;
    const std::unique_ptr<ai::Agent> other = ai::makeAgent("x3", narrow);
    other->startRound(0, seed);
    Follower follow;
    Driver d;
    d.lag = startLag >= 0 ? startLag : lag, d.rules = rules, d.agent = given ? std::move(given) : ai::makeAgent("x3", narrow);
    d.notes = notes;
    d.takeLag = !keepLag;
    int64_t clock = 1000;
    for (int f = 0; f < 45; ++f, ++clock) {
        draw(canvas, l, r, true, noise);
        follow.step(read(canvas.picture(), l), clock);
        check(d.tick(follow, clock) == 0, "buttons before the round");
    }
    std::vector<uint8_t> sent(size_t(frames) + 100, 0); // by the frame of the clock they were sent in
    const int64_t first = clock;
    ai::Observation o;
    int target = -1, played = 0; // (the engine of the Driver plays `lag` frames ahead of the round: the target of the pair it played last)
    int oldFor = 0, hideFor = 0, spawns = 0; // (spawns: pairs of 2P that have appeared in the round)
    int lastTurn = -100;                     // the frame of the round the pair of 2P last turned in
    int64_t begunAt = -1;                    // the frame of the clock the driver began its last pair in
    bool leftOver = false;                   // the pair of 2P that appeared last still takes buttons sent before it was begun
    uint8_t oldDir = 0;
    int alone = 0; // frames in a row the pair of 2P in the air took no button
    int litFor = 0; // litAsAppears: frames the light over 2P stays
    for (int f = 0; f < frames && r.state == RoundState::Playing; ++f, ++clock) {
        if (oldFor > 0 || hideFor > 0) {
            Round shown = r;
            if (oldFor > 0) shown.player[1].pair.dir = oldDir, --oldFor;
            if (hideFor > 0 && shown.player[1].phase == Phase::Control) shown.player[1].phase = Phase::Split, --hideFor;
            draw(canvas, l, shown, false, noise, quietLanding);
        } else {
            draw(canvas, l, r, false, noise, quietLanding);
        }
        // (litAsAppears: one pair of 2P in three lit from 8 frames before it appears, for that many frames - on the real game
        //  the screen of 2P read nothing for 30 frames as a pair appeared)
        if (litAsAppears && r.player[1].phase == Phase::Next && r.player[1].wait == 8 && spawns % 3 == 2) litFor = litAsAppears;
        if (f % 12 < lit || litFor > 0) {
            // (a light over the field of 2P: nothing of it is read in this picture)
            const SideLayout& s = l.side[1];
            canvas.fill(s.left, s.bottom - 2 * s.cellHeight, s.left + s.cellWidth, s.bottom - s.cellHeight, LIGHT);
            if (litFor > 0) --litFor;
        }
        follow.step(read(canvas.picture(), l), clock);
        if (later && f == laterAt) d.replaceAgent(std::move(later)); // (another AI chosen on the screen of the mode)
        // (a frame the driver is not called in: the pad holds what it held)
        if (skipOneIn && f > 0 && skipper.below(uint64_t(skipOneIn)) == 0) sent[size_t(clock - first)] = sent[size_t(clock - first - 1)];
        else sent[size_t(clock - first)] = d.tick(follow, clock);
        if (d.played != played) played = d.played, target = -1, begunAt = clock;
        if (d.target() >= 0) target = d.target();
        // The next frame of the round takes what was sent `lag` frames before it (a shaky pad: now and then a frame
        // sooner or later).
        int64_t from = clock + 1 - lag;
        if (shaky && shake.below(8) == 0) from += shake.below(2) ? 1 : -1;
        from = std::min(from, clock);
        uint8_t buttons[2];
        ai::observe(r, 0, o);
        buttons[0] = other->frame(o);
        buttons[1] = from >= first ? sent[size_t(from - first)] : 0;
        if (swapped) buttons[1] = uint8_t((buttons[1] & ~(BTN_CW | BTN_CCW)) | (buttons[1] & BTN_CW ? BTN_CCW : 0) | (buttons[1] & BTN_CCW ? BTN_CW : 0));
        PlayerEvents e[2];
        const uint8_t dirBefore = r.player[1].pair.dir;
        const int8_t columnBefore = r.player[1].pair.x;
        alone = r.player[1].phase == Phase::Control && buttons[1] == 0 ? alone + 1 : 0;
        if (alone == 10) ++n.leftAlone;
        n.control += r.player[1].phase == Phase::Control;
        r.step(buttons, e);
        if (turnShownLate && e[1].control.rotated && r.player[1].phase == Phase::Control) oldDir = dirBefore, oldFor = turnShownLate;
        if (crowdedLate && e[1].control.rotated && r.player[1].phase == Phase::Control) {
            if (r.player[1].pair.x != columnBefore || f - lastTurn <= 3) {
                if (oldFor == 0) oldDir = dirBefore;
                oldFor = crowdedLate;
            }
            lastTurn = f;
        }
        if (movedHidden && r.player[1].phase == Phase::Control && r.player[1].pair.x != columnBefore && !e[1].spawned) hideFor = std::max(hideFor, movedHidden);
        // (from the frame a pair appears in, until the frames take buttons the driver sent once it had begun that pair)
        if (e[1].spawned) ++spawns, leftOver = true, n.appeared.push_back(clock);
        if (leftOver && d.played >= spawns && from >= begunAt) leftOver = false;
        if (leftOver && (buttons[1] & BTN_DOWN)) ++n.downAtSpawn, leftOver = false;
        if (hidden && e[1].control.rotated && r.player[1].phase == Phase::Control) hideFor = hidden;
        // (a pair the AI found no placement for - the field full - is not counted)
        if (e[1].locked && target >= 0) n.locks += 1, n.hits += placementId(r.player[1].pair.placement()) == target;
        if (garbageAsLit && e[1].locked && spawns % 3 == 2) r.player[1].incoming.confirmed += garbageAsLit;
        ++n.frames;
    }
    n.corrected += d.corrected, n.played += d.played;
    n.lagRounds += 1, n.lagRight += d.measuredLag() == lag;
    n.lagTaken += d.lag == lag, n.turnsFound += d.turnsSwapped;
    if (sentOut) *sentOut = sent;
}

// An agent that chooses the first placement it may, and notes how many frames before its pair appears it chose (the
// wait of its own side then: 1 = in the frame the pair appears). It chooses `ahead` frames before, as the game's AI.
struct EarlyAgent : ai::PlacementAgent {
    std::vector<int>* waits;
    explicit EarlyAgent(std::vector<int>* noted) : waits(noted) { ahead = 12; }
    ~EarlyAgent() override { finish(); }
    int choose(const ai::Observation& o, Pair, unsigned legal) override {
        waits->push_back(o.self().wait);
        for (int id = 0; id < PLACEMENTS; ++id)
            if (legal >> id & 1) return id;
        return 0;
    }
};

// X3 that counts its choices.
struct CountedX3 : ai::X3Agent {
    std::atomic<int>* chosen;
    CountedX3(const ai::Params& p, std::atomic<int>* count) : ai::X3Agent(p), chosen(count) {}
    int choose(const ai::Observation& o, Pair pair, unsigned legal) override {
        ++*chosen;
        return ai::X3Agent::choose(o, pair, legal);
    }
};

static void testDriver() {
    DriveCounts exact, shaky;
    for (int lag = 1; lag <= 3; ++lag) driveRound(60 + uint64_t(lag), lag, false, 2500, exact);
    check(exact.lagRight == exact.lagRounds, "the lag measured is not the lag of the pad");
    check(exact.locks >= 60 && exact.hits == exact.locks && exact.corrected == 0,
          "through a pad that is late as said, a pair is not brought where the AI chose (" + std::to_string(exact.hits) + " of " + std::to_string(exact.locks) +
              ") or found elsewhere than the engine had it (" + std::to_string(exact.corrected) + ")");
    for (int k = 0; k < 3; ++k) driveRound(70 + uint64_t(k), 2, true, 2500, shaky);
    check(shaky.locks >= 60 && shaky.corrected > 0 && shaky.hits * 10 >= shaky.locks * 9,
          "through a shaky pad, pairs are not put right: " + std::to_string(shaky.hits) + " of " + std::to_string(shaky.locks) + " where the AI chose, " +
              std::to_string(shaky.corrected) + " found elsewhere");
    // The lag the driver begins with is not the pad's: it takes the lag it measures.
    DriveCounts learnt;
    for (const int padLag : {4, 6}) driveRound(80 + uint64_t(padLag), padLag, false, 2500, learnt, nullptr, nullptr, 2);
    // The lag begun with a frame more than the pad's (as on the real game: 6 for 5): the pictures show the pair a frame
    // ahead of the engine, which takes the lag from the pictures that show it in another column, and puts no pair
    // elsewhere for it (116 times in these rounds when each such picture was put right; the heights and the turns of the
    // rounds drawn here would tell of the lag too, but those of the real game do not, and are not taken).
    DriveCounts over;
    for (int k = 0; k < 2; ++k) driveRound(84 + uint64_t(k), 5, false, 2500, over, nullptr, nullptr, 6);
    check(over.lagTaken == 2 && over.corrected <= 10 && over.hits * 10 >= over.locks * 9,
          "beginning with a lag a frame more than the pad's, pairs are put elsewhere for it (" + std::to_string(over.corrected) + " times), or the lag is not taken (" +
              std::to_string(over.lagTaken) + " of 2 rounds), or pairs are not brought where the AI chose (" + std::to_string(over.hits) + " of " + std::to_string(over.locks) + ")");
    // A turn shown two frames late (as the real game shows it): pictures of the pair the way it was are no pair elsewhere.
    DriveCounts late;
    for (int k = 0; k < 2; ++k) driveRound(96 + uint64_t(k), 5, false, 2500, late, nullptr, nullptr, -1, false, 2);
    check(late.turnsFound == 0 && late.corrected <= 10 && late.hits * 10 >= late.locks * 9,
          "with turns shown two frames late, pairs are put elsewhere (" + std::to_string(late.corrected) + " times), the turns are taken the other way round (" +
              std::to_string(late.turnsFound) + "), or pairs are not brought where the AI chose (" + std::to_string(late.hits) + " of " + std::to_string(late.locks) + ")");
    // The AI chooses before its pair appears, as in a match of this program (the pair is not kept waiting for its choice,
    // which takes a while on a machine under load): after its pair is locked the engine goes on, and the AI is shown
    // when the next pair comes.
    {
        std::vector<int> waits;
        DriveCounts n;
        std::vector<std::pair<int64_t, std::string>> notes;
        driveRound(101, 5, false, 1500, n, &notes, nullptr, -1, false, 0, std::make_unique<EarlyAgent>(&waits));
        int before = 0;
        for (const int wait : waits) before += wait > 1;
        check(waits.size() >= 10 && before * 10 >= int(waits.size()) * 8,
              "the AI chooses as its pair appears, not before (" + std::to_string(before) + " of " + std::to_string(waits.size()) + " choices before)");
        // (begun with the lag of the pad, nothing is to be taken from the pictures: a pair at another height alone - the
        //  real game and the engine a frame apart in when a fall begins - tells nothing of the lag)
        int taken = 0;
        for (const auto& [at, line] : notes) taken += line.find("lag taken from the pictures") != std::string::npos;
        check(taken == 0, "begun with the lag of the pad, another lag is taken from the pictures (" + std::to_string(taken) + " times)");
    }
    // The engine a little ahead of the game, the lag kept 2 frames less than the pad's (on the real game it is 4 to 6
    // frames, the driver's one of them): the pictures show the pair where the engine had it 2 frames before. Each of
    // them put right, the buttons in flight were played again for the wrong frames and the pair sent to and fro - 125
    // times in 27 pairs, a pair 92 frames against 64 (on the real game a pair took 80 frames, 57 for a player). A
    // pair where the engine has it a frame or two before or after is put nowhere else. (A light over the field of 2P in 4
    // frames of 12.) The frames a pair is in play are set against each other: the frames of the round besides - chains,
    // garbage, the round over sooner when 2P loses - are as the choices of the two AIs make them (with the pictures
    // drawn otherwise, 2P lost the round of the lag right at its 1736th frame, nothing chained: 52 frames a pair against
    // 73 with the engine ahead, 19 frames in play for each).
    {
        DriveCounts right, ahead;
        driveRound(106, 5, false, 2500, right, nullptr, nullptr, -1, false, 0, nullptr, true, 4);
        driveRound(106, 5, false, 2500, ahead, nullptr, nullptr, 3, false, 0, nullptr, true, 4);
        const double perRight = double(right.control) / double(std::max(1L, right.locks)), perAhead = double(ahead.control) / double(std::max(1L, ahead.locks));
        check(right.locks >= 20 && ahead.locks >= 20 && perAhead <= perRight * 1.15 && ahead.corrected <= 10 && ahead.hits * 10 >= ahead.locks * 9,
              "with the engine ahead of the game, a pair is in play " + std::to_string(perAhead) + " frames, with the lag right " + std::to_string(perRight) + ", put elsewhere " +
                  std::to_string(ahead.corrected) + " times, brought where the AI chose " + std::to_string(ahead.hits) + " of " + std::to_string(ahead.locks));
    }
    // The AI or its level changed on the screen of the mode while the AI plays (it plays from the moment the window of
    // the real game is read: changed after that, the level the AI began with went on playing - seen on the real game,
    // the record of a match said level 1 for 36 chosen): the new AI plays from the next pair on.
    {
        ai::Params narrow;
        narrow.beamWidth = 30, narrow.beamDepth = 8, narrow.beamQueues = 3;
        std::atomic<int> before{0}, after{0};
        DriveCounts n;
        driveRound(107, 5, false, 2500, n, nullptr, nullptr, -1, false, 0, std::make_unique<CountedX3>(narrow, &before), false, 0,
                   std::make_unique<CountedX3>(narrow, &after), 1200);
        check(before > 0 && after >= 10 && n.hits * 10 >= n.locks * 9,
              "the AI given while the AI plays does not play (" + std::to_string(after.load()) + " choices; the one before " + std::to_string(before.load()) +
                  "), or pairs are not brought where the AI chose (" + std::to_string(n.hits) + " of " + std::to_string(n.locks) + ")");
    }
    // A pair not to be read while it turns (as on the real game: 9 frames for two turns, a move sent with them): the
    // first picture that shows it moved may come long after the move was taken, which is no measure of the lag (its
    // record: 12 frames measured for 5 or 6, the lag taken 11, the pair put right 40 times). A move is measured only from
    // a picture that follows one of the pair not moved.
    {
        DriveCounts n;
        std::vector<std::pair<int64_t, std::string>> notes;
        for (int k = 0; k < 2; ++k) driveRound(108 + uint64_t(k), 5, false, 2500, n, &notes, nullptr, -1, false, 0, nullptr, false, 0, nullptr, 0, 8);
        int off = 0, measured = 0;
        for (const auto& [at, line] : notes)
            if (line.rfind("lag measured: ", 0) == 0) ++measured, off += std::abs(std::atoi(line.c_str() + 14) - 5) > 1;
        check(measured >= 10 && off == 0 && n.lagTaken == 2 && n.hits * 10 >= n.locks * 9,
              "with the pair not to be read as it turns, " + std::to_string(off) + " lags of " + std::to_string(measured) + " measured far from the pad's, the lag kept in " +
                  std::to_string(n.lagTaken) + " rounds of 2, " + std::to_string(n.hits) + " pairs of " + std::to_string(n.locks) + " brought where the AI chose");
    }
    // A machine under load, all of it at once: a pad now and then a frame early or late, turns shown two frames late, the
    // lag begun a frame more than the pad's.
    DriveCounts heavy;
    for (int k = 0; k < 2; ++k) driveRound(98 + uint64_t(k), 5, true, 2500, heavy, nullptr, nullptr, 6, false, 2);
    check(heavy.turnsFound == 0 && heavy.hits * 100 >= heavy.locks * 85,
          "on a machine under load the turns are taken the other way round (" + std::to_string(heavy.turnsFound) + "), or pairs are not brought where the AI chose (" +
              std::to_string(heavy.hits) + " of " + std::to_string(heavy.locks) + ")");
    check(learnt.turnsFound == 0, "a pair found elsewhere through a lag not yet taken is taken for turns the other way round (" + std::to_string(learnt.turnsFound) + " of 2 rounds)");
    check(learnt.lagTaken == 2 && learnt.locks >= 40 && learnt.hits * 10 >= learnt.locks * 9,
          "beginning with a lag of 2 through a pad 4 or 6 frames late, the driver does not take the lag measured (" + std::to_string(learnt.lagTaken) + " of 2 rounds) or does not bring the pairs where the AI chose (" +
              std::to_string(learnt.hits) + " of " + std::to_string(learnt.locks) + ")");
    // The pad turns the other way round from what the driver sends: it finds so from the screen and sends the other button.
    DriveCounts swapped;
    for (int k = 0; k < 2; ++k) driveRound(90 + uint64_t(k), 3, false, 2500, swapped, nullptr, nullptr, -1, true);
    check(swapped.turnsFound == 2 && swapped.locks >= 40 && swapped.hits * 10 >= swapped.locks * 9,
          "through a pad that turns the other way round, the driver does not find so (" + std::to_string(swapped.turnsFound) + " of 2 rounds) or does not bring the pairs where the AI chose (" +
              std::to_string(swapped.hits) + " of " + std::to_string(swapped.locks) + ")");
    check(exact.turnsFound == 0 && shaky.turnsFound == 0, "through a pad that turns as sent, the driver takes it to turn the other way round");
    // The real game as its six records showed it, each picture of the pair of 2P set against the engine given the
    // buttons of the record: a button taken 4 frames after it is sent (3 to 6 in the records, the same through a round),
    // the pair not to be read in the frame it moves to the side and in the two after a turn (the height shown at once),
    // the two turns the other way round from what is sent, a pair come to rest shown still where it lies until the next
    // pair; the driver begins with a lag of 6 (with these, 96 to 99% of the pictures of a record are where the engine has
    // the pair).
    DriveCounts real;
    for (int k = 0; k < 2; ++k) driveRound(112 + uint64_t(k), 4, false, 2500, real, nullptr, nullptr, 6, true, 0, nullptr, false, 0, nullptr, 0, 2, 1, true);
    check(real.turnsFound == 2 && real.lagTaken == 2 && real.locks >= 40 && real.hits * 10 >= real.locks * 9,
          "through a pad as the real game: the turns the other way round found in " + std::to_string(real.turnsFound) + " rounds of 2, the lag of 4 taken in " +
              std::to_string(real.lagTaken) + ", pairs brought where the AI chose " + std::to_string(real.hits) + " of " + std::to_string(real.locks) + ", put right " +
              std::to_string(real.corrected) + " times");
    // The down button held for a pair the engine has locked goes no further than that pair: a pair that appears takes
    // none of it (on the real game it reached 22 pairs of 39 in a record, held until the next pair was seen, and a pair
    // that had fallen by it in its first frames found no way to its placement behind a high column: four pairs went
    // down where they appeared).
    check(real.downAtSpawn == 0, "through a pad as the real game, " + std::to_string(real.downAtSpawn) + " pairs of " + std::to_string(real.locks) + " took the down button as they appeared");
    // The thread of the play a frame late now and then (4.6% of the frames of a record of the real game, the next frame
    // called twice): the button the AI chose for a frame the driver was not called in is not lost. (Lost, the engine went
    // on without it as the game did, nothing was put right, and the pair went down a column short of its placement: five
    // pairs of 45 in that record, the one move of their way chosen for such a frame.)
    DriveCounts skipped;
    for (int k = 0; k < 2; ++k) driveRound(112 + uint64_t(k), 5, false, 2500, skipped, nullptr, nullptr, 6, true, 0, nullptr, false, 0, nullptr, 0, 2, 1, true, 20);
    check(skipped.locks >= 40 && skipped.hits * 100 >= skipped.locks * 98,
          "with the driver not called in one frame of 20, pairs brought where the AI chose " + std::to_string(skipped.hits) + " of " + std::to_string(skipped.locks));
    // A turn taken with a move, or just after another turn, shown up to 6 frames late (the real game's eighth record):
    // a picture of the pair the way it was before is no pair turned otherwise. (Put right for it, the turns in flight were
    // made again: five pairs of 89 in that record, each to go down in the sixth column with its child below, went down with
    // the child on the left.)
    DriveCounts crowded;
    for (int k = 0; k < 2; ++k)
        driveRound(112 + uint64_t(k), 5, false, 2500, crowded, nullptr, nullptr, 6, true, 0, nullptr, false, 0, nullptr, 0, 2, 1, true, 20, 6);
    check(crowded.locks >= 40 && crowded.hits * 100 >= crowded.locks * 98 && crowded.corrected * 4 <= crowded.locks,
          "with turns shown 6 frames late after a move or a turn, pairs brought where the AI chose " + std::to_string(crowded.hits) + " of " + std::to_string(crowded.locks) +
              ", put right " + std::to_string(crowded.corrected) + " times");
    std::cout << "the AI playing 2P through the screen and a pad: late as said, " << exact.hits << " pairs of " << exact.locks
              << " where it chose and none found elsewhere, the lag measured as it was; shaky, " << shaky.corrected << " pictures found a pair elsewhere and " << shaky.hits << " of "
              << shaky.locks << " went where it chose\n";
}

// On a machine under load the real game's frames take the buttons of the frame before or after now and then (its
// twelfth record: 79% of the pictures where the engine had the pair, 93 to 98% in the records before), and a tap of
// one frame is then lost or runs into the next one; the driver does not always see it in time (a turn shown late, or
// the engine's pair locked before the picture shows it otherwise). The AI of the mode (Driver::prepare) presses each
// tap two frames and lets go two (PlacementAgent::tapHold), and the driver takes turns that far apart as one run.
static void testTapsHeld() {
    ai::Params narrow;
    narrow.beamWidth = 30, narrow.beamDepth = 8, narrow.beamQueues = 3;
    const auto prepared = [&] {
        std::unique_ptr<ai::Agent> a = ai::makeAgent("x3", narrow);
        Driver::prepare(a.get());
        return a;
    };
    // (a pad as the real game - the turns the other way round, a turn and a move not to be read -, a frame sooner or
    //  later in one frame of 8: 83.7% where the AI chose with taps of a frame, over 3 x 6 rounds; 92.1% with taps of two)
    DriveCounts shaken;
    for (int k = 0; k < 2; ++k)
        driveRound(112 + uint64_t(k), 5, true, 2500, shaken, nullptr, nullptr, 6, true, 0, prepared(), false, 0, nullptr, 0, 2, 1, true);
    check(shaken.locks >= 40 && shaken.hits * 100 >= shaken.locks * 88,
          "through a pad as the real game under load, pairs of the mode's AI brought where it chose " + std::to_string(shaken.hits) + " of " + std::to_string(shaken.locks));
    // (no pair is left to fall by itself: a pair the engine has put, the screen not yet showing it so, is held down
    //  until the screen shows it lying or the next pair is foreseen - whether or not the screen's pair is followed)
    check(shaken.leftAlone == 0, "through a pad as the real game under load, pairs of 2P left to fall by themselves " + std::to_string(shaken.leftAlone) + " times");
    // (turns with a move or after another shown 6 frames late, the way of the turns known as the mode knows it: taps of two
    //  frames space the turns 4 to 5 frames apart, which the driver took for no run - put right, they were turned again:
    //  11 pairs of 361 elsewhere in 10 rounds, none when taken as one run)
    DriveCounts crowded;
    for (const uint64_t seed : {300, 302})
        driveRound(seed, 5, false, 2500, crowded, nullptr, nullptr, 6, false, 0, prepared(), false, 0, nullptr, 0, 2, 1, true, 20, 6);
    check(crowded.locks >= 40 && crowded.hits * 100 >= crowded.locks * 98,
          "with turns shown 6 frames late, pairs of the mode's AI brought where it chose " + std::to_string(crowded.hits) + " of " + std::to_string(crowded.locks) +
              ", put right " + std::to_string(crowded.corrected) + " times");
    check(dynamic_cast<ai::PlacementAgent&>(*prepared()).tapHold == 2, "the AI of the mode taps a frame each");
    std::cout << "the AI of the mode holds its taps: under load " << shaken.hits << " of " << shaken.locks << " where it chose, with turns shown late " << crowded.hits
              << " of " << crowded.locks << "\n";
}

// The screen of 2P not to be read as a pair appears (a light over it, 30 frames: the real game in its thirteenth record,
// third round). The driver begins no pair the screen has not shown, and the engine, which foresees the pair, stops two
// frames before it (it may come later: garbage the engine does not know of falls first) - so the pair fell by itself
// until the screen showed it, seen late. Once the window has moved on (the pair has come) and the time the engine
// foresaw has gone by with the screen not read, the driver begins the pair it foresees, of the colours learnt; the
// screen puts it right when it shows it.
static void testPairsLitAsTheyAppear() {
    ai::Params narrow;
    narrow.beamWidth = 30, narrow.beamDepth = 8, narrow.beamQueues = 3;
    DriveCounts lit;
    for (int k = 0; k < 2; ++k) {
        std::unique_ptr<ai::Agent> a = ai::makeAgent("x3", narrow);
        Driver::prepare(a.get());
        driveRound(120 + uint64_t(k), 5, false, 2500, lit, nullptr, nullptr, 6, false, 0, std::move(a), false, 0, nullptr, 0, 2, 1, true, 0, 0, 30);
    }
    check(lit.locks >= 40 && lit.leftAlone == 0,
          "with the screen of 2P lit as a pair appears, pairs of 2P left to fall by themselves " + std::to_string(lit.leftAlone) + " times (" + std::to_string(lit.locks) + " pairs)");
    check(lit.hits * 100 >= lit.locks * 90, "with the screen of 2P lit as a pair appears, pairs brought where the AI chose " + std::to_string(lit.hits) + " of " + std::to_string(lit.locks));
    // Garbage falls on 2P before each of the pairs lit, which the engine does not know of: it foresees the pair sooner
    // than it comes. A pair begun unseen is begun as it appears, after the window of the next pairs moved on (begun at
    // the frame foreseen, the engine ran it down by itself to where it locks while the game's pair was in the air).
    DriveCounts fallen;
    std::vector<std::pair<int64_t, std::string>> notes;
    for (int k = 0; k < 2; ++k) {
        std::unique_ptr<ai::Agent> a = ai::makeAgent("x3", narrow);
        Driver::prepare(a.get());
        driveRound(120 + uint64_t(k), 5, false, 2500, fallen, &notes, nullptr, 6, false, 0, std::move(a), false, 0, nullptr, 0, 2, 1, true, 0, 0, 30, 6);
    }
    int unseen = 0, off = 0;
    const std::string begun = "begun unseen: the screen not read, the engine had it appear in frame ";
    for (const auto& [at, line] : notes) {
        const size_t i = line.find(begun);
        if (i == std::string::npos) continue;
        const int64_t foreseen = std::atoll(line.c_str() + i + begun.size());
        int64_t nearest = 1 << 30;
        for (const int64_t frame : fallen.appeared) nearest = std::min(nearest, std::abs(frame - foreseen));
        ++unseen, off += nearest > Driver::BLIND_SLACK;
    }
    check(unseen >= 5 && off == 0, "with garbage falling on 2P before a pair its screen is lit for, " + std::to_string(off) + " pairs of " + std::to_string(unseen) +
                                       " begun unseen are begun more than " + std::to_string(Driver::BLIND_SLACK) + " frames from where they appeared");
    std::cout << "the screen of 2P lit as a pair appears: no pair left alone, " << lit.hits << " of " << lit.locks << " where the AI chose; with garbage before it, "
              << unseen << " pairs begun unseen as they appeared\n";
}

// ---- 7. the menus of the real game --------------------------------------------------------------------------------

static void testMenus() {
    const MenuLayout m = measuredMenus();
    const Layout l = sampleLayout();
    // (each point painted as a square of 11 x 11 pixels in its colour, the rest grey)
    const auto paint = [](std::vector<uint8_t>& px, int w, int h, const std::vector<MenuPoint>& points, int shift) {
        for (const MenuPoint& q : points) {
            const int cx = int(q.x * float(w)), cy = int(q.y * float(h));
            for (int y = cy - 5; y <= cy + 5; ++y)
                for (int x = cx - 5; x <= cx + 5; ++x) {
                    uint8_t* p = &px[(size_t(y) * size_t(w) + size_t(x)) * 4];
                    p[0] = uint8_t(std::min(255, q.rgb.b + shift)), p[1] = uint8_t(q.rgb.g), p[2] = uint8_t(q.rgb.r), p[3] = 255;
                }
        }
    };
    for (const auto& [w, h] : std::vector<std::pair<int, int>>{{2272, 1278}, {2240, 1260}, {1280, 720}}) {
        const auto read = [&](std::initializer_list<const std::vector<MenuPoint>*> parts, int shift = 0) {
            Canvas c(w, h, l);
            for (size_t i = 0; i < c.pixels.size(); i += 4) c.pixels[i] = c.pixels[i + 1] = c.pixels[i + 2] = 90, c.pixels[i + 3] = 255;
            for (const auto* part : parts) paint(c.pixels, w, h, *part, shift);
            return readMenu(c.picture(), m);
        };
        const std::string size = std::to_string(w) + "x" + std::to_string(h);
        check(read({}) == Menu::None, size + ": a grey picture is taken for a menu");
        check(read({&m.players}) == Menu::Players && read({&m.players, &m.waiting}) == Menu::PlayersWaiting, size + ": the screen of the players is not read");
        check(read({&m.players, &m.waiting, &m.padded}) == Menu::Players, size + ": the row of 2P taken by a pad is taken to be still to take");
        check(read({&m.characters}) == Menu::Characters && read({&m.characters, &m.decided}) == Menu::Decided, size + ": the screen of the characters is not read");
        check(read({&m.players, &m.waiting}, 80) == Menu::None, size + ": a screen of other colours is taken for the screen of the players");
        check(read({&m.waiting}) == Menu::None && read({&m.decided}) == Menu::None, size + ": a part of a screen alone is taken for the screen");
    }
    MenuPresser presser;
    int presses = 0, first = -1;
    for (int f = 0; f < MenuPresser::STEADY + 3 * (MenuPresser::GAP + 1); ++f)
        if (presser.step(Menu::PlayersWaiting)) presses += 1, first = first < 0 ? f : first;
    check(first == MenuPresser::STEADY - 1 && presses == 4,
          "on the screen of the players with 2P to be taken, A is not pressed once it has read so STEADY times, and every GAP pictures after (" + std::to_string(presses) + ")");
    MenuPresser idle;
    int none = 0;
    for (const Menu menu : {Menu::None, Menu::Players, Menu::Decided})
        for (int f = 0; f < 100; ++f) none += idle.step(menu);
    check(none == 0, "A is pressed where 2P has nothing to do");
    MenuPresser characters;
    int when = -1;
    for (int f = 0; f < MenuPresser::STEADY + 3 && when < 0; ++f)
        if (characters.step(Menu::Characters)) when = f;
    check(when == MenuPresser::STEADY - 1 && !characters.step(Menu::Characters), "on the screen of the characters with 2P to decide, A is not pressed, or pressed again at once");
    // (the screen moving on after 2P decided read once as 2P to decide - seen on the real game: no press)
    MenuPresser leaving;
    int stray = 0;
    for (int f = 0; f < 30; ++f) stray += leaving.step(Menu::Decided);
    stray += leaving.step(Menu::Characters);
    for (int f = 0; f < 30; ++f) stray += leaving.step(Menu::None);
    check(stray == 0, "a screen read once as a menu where 2P has to is pressed");
    std::cout << "the menus of the real game read by their points at three sizes; A pressed only where 2P has to\n";
}

// ---- 6. the record of a reading -----------------------------------------------------------------------------------

static bool hasNote(const std::vector<std::pair<int64_t, std::string>>& notes, const std::string& part) {
    for (const auto& [at, line] : notes)
        if (line.find(part) != std::string::npos) return true;
    return false;
}

static void testRecord(const std::string& folder) {
    const std::filesystem::path dir = std::filesystem::path(folder) / "realgame-record-test";
    std::filesystem::remove_all(dir);
    Rng rng(11);
    Reading a, b;
    for (int s = 0; s < 2; ++s)
        for (int x = 0; x < W; ++x)
            for (int k = 0; k < play::realgame::POINTS; ++k) a.side[s].point[x][k] = Cell(rng.below(9)), b.side[s].point[x][k] = Cell(rng.below(9));
    a.side[1].notice = 17, a.side[0].next[1] = {3, 4}, a.side[0].nextRead[1] = true, b.side[0].unsure = 5;
    const Layout l = sampleLayout();
    Canvas pic(40, 24, l);
    for (uint8_t& v : pic.pixels) v = uint8_t(rng.below(256));
    for (size_t i = 3; i < pic.pixels.size(); i += 4) pic.pixels[i] = 255; // (a picture of a window is opaque; BMP keeps no opacity)
    {
        Record r;
        check(r.open(dir, "the head\nof the record"), "the record is not made");
        r.reading(a, 1000, 2240, 1260);
        r.pad({1000, BTN_LEFT | BTN_CW, 0x1004, 1});
        r.reading(b, 1001, 2240, 1260);
        r.event(1001, "a line");
        // Pictures: frame 1000 kept (none before), 1010 not (too soon), 1020 asked for and kept, 1030 asked for and not
        // (too soon after 1020), then 1000 + PICTURE_EVERY kept.
        check(r.offer(pic.picture(), 1000) && !r.offer(pic.picture(), 1010), "the pictures at any time are not kept every PICTURE_EVERY frames");
        r.ask("miss");
        check(r.offer(pic.picture(), 1020), "the picture asked for is not kept");
        r.ask("doubt");
        check(!r.offer(pic.picture(), 1030), "a picture asked for is kept sooner than PICTURE_GAP frames after the last");
        check(r.offer(pic.picture(), 1000 + Record::PICTURE_EVERY), "the picture at any time is not kept after PICTURE_EVERY frames");
        r.close();
    }
    RecordReader in;
    check(in.open(dir / "readings.bin"), "readings.bin is not read back");
    Reading back;
    int64_t frame = 0;
    int w = 0, h = 0;
    PadRecord p;
    const auto same = [](const Reading& x, const Reading& y) { return std::memcmp(&x.side[0], &y.side[0], sizeof(SideRead)) == 0 && std::memcmp(&x.side[1], &y.side[1], sizeof(SideRead)) == 0; };
    check(in.next(back, frame, w, h, p) == RECORD_READING && frame == 1000 && w == 2240 && h == 1260 && same(back, a), "the first reading is read back otherwise");
    check(in.next(back, frame, w, h, p) == RECORD_PAD && p.frame == 1000 && p.buttons == (BTN_LEFT | BTN_CW) && p.pad == 0x1004 && p.by == 1, "the buttons are read back otherwise");
    check(in.next(back, frame, w, h, p) == RECORD_READING && frame == 1001 && same(back, b), "the second reading is read back otherwise");
    check(in.next(back, frame, w, h, p) == 0, "readings.bin has more than was written");
    in.close();
    std::ifstream events(dir / "events.txt", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(events)), std::istreambuf_iterator<char>());
    events.close();
    check(text.rfind("the head\nof the record\n", 0) == 0 && text.find("1001 a line\n") != std::string::npos, "events.txt does not have the head and the line");
    for (const std::string& name : std::vector<std::string>{"1000.bmp", "1020-miss.bmp", std::to_string(1000 + Record::PICTURE_EVERY) + ".bmp"}) {
        std::vector<uint8_t> pixels;
        check(loadBmp(dir / name, pixels, w, h) && w == pic.width && h == pic.height && pixels == pic.pixels, "the picture " + name + " is not written as it was");
        check(text.find("picture " + name) != std::string::npos, "events.txt does not name the picture " + name);
    }
    check(!std::filesystem::exists(dir / "1010.bmp") && !std::filesystem::exists(dir / "1030-doubt.bmp"), "a picture is kept that was not wanted");
    // At most MAX_PICTURES.
    {
        Record r;
        check(r.open(dir / "many", ""), "the record is not made");
        int taken = 0;
        for (int i = 0; i < Record::MAX_PICTURES + 20; ++i) {
            taken += r.offer(pic.picture(), int64_t(i) * Record::PICTURE_EVERY);
            std::this_thread::sleep_for(std::chrono::milliseconds(3)); // (time for the writer: none dropped as four behind)
        }
        r.close();
        int files = 0;
        for (const auto& e : std::filesystem::directory_iterator(dir / "many")) files += e.path().extension() == ".bmp";
        check(taken == Record::MAX_PICTURES && files == Record::MAX_PICTURES, "not MAX_PICTURES pictures are kept: " + std::to_string(taken) + ", " + std::to_string(files) + " written");
    }
    std::filesystem::remove_all(dir);
    // The driver noting what it does: the same buttons, and the notes.
    DriveCounts plain, noted, shakyNoted;
    std::vector<uint8_t> sentPlain, sentNoted;
    std::vector<std::pair<int64_t, std::string>> notes, shakyNotes;
    driveRound(62, 2, false, 1500, plain, nullptr, &sentPlain);
    driveRound(62, 2, false, 1500, noted, &notes, &sentNoted);
    check(sentPlain == sentNoted && plain.hits == noted.hits, "the driver noting what it does sends other buttons");
    check(hasNote(notes, " begun") && hasNote(notes, "brought to column") && hasNote(notes, "lag measured: 2 frames"), "the driver does not note the pairs begun, the placements or the lag");
    driveRound(71, 2, true, 1500, shakyNoted, &shakyNotes);
    check(shakyNoted.corrected == 0 || hasNote(shakyNotes, "put right: the engine had"), "the driver does not note a pair put right");
    std::cout << "the record of a reading: read back the same, pictures kept as they should be; the driver noting sends the same buttons (" << notes.size() << " notes)\n";
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
        testNoticeCeiling();
        testPairsSeenOnce();
        testWindowBeforeRound();
        testShownPairsOnly();
        testChoiceAfterCorrection();
        testViewsOfBothSides();
        testPredictionResetAtRound();
        testChainAfterMissedLanding();
        testPairsAfterLight();
        testRow13Kept();
        testLyingPieceHidden();
        testSecondChoiceHeld();
        testDriver();
        testTapsHeld();
        testPairsLitAsTheyAppear();
        testPicture(argc > 1 ? argv[1] : ".");
        testRecord(argc > 1 ? argv[1] : ".");
        testMenus();
        testRounds();
        testSidesLearnApart();
        testPairsNeverLeft();
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
    std::cout << "all realgame tests passed\n";
    return 0;
}
