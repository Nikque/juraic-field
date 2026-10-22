#pragma once
// The mode in which Juraic plays 2P of the real game (play/realgame_win.h): what the screen shows (play/realgame_read.h,
// Tracker) followed with the rules of the game. Where each pair has come to rest is found among the placements of the
// game: the pieces at rest are the field it appeared over with the pair put there. The chain it starts, its points
// and the garbage it sends and offsets are reckoned as the game reckons them (game/turn.h). So the pieces of each side
// are known before the screen shows them again; when it does, standing still as the next pair appears, they are
// checked (only garbage may have come above them). And the notice is checked against what the chains reckoned can have
// sent: a notice of more is taken only when it has read the same for a long time (a light of the screen over the
// notice changes what it shows).
//
// The 13th and 14th rows are not on the screen (it shows twelve): their pieces are those the placements followed put
// there - in the 13th row where the screen shows the column twelve high, in the 14th always (they never move) - and
// garbage that fell into the 13th row of a column twelve high as far as the screen tells (withGarbage).
//
// What the screen does not tell: the points of the down key (a point a row, which count towards garbage), so the
// garbage a chain sends is reckoned short by a little at times (Follower::slack).
#include "realgame_read.h"

namespace play::realgame {

struct SideModel {
    bool known = false;      // the pieces of `core` are those of the game, from the last new pair on
    juraic::PlayerCore core; // the pieces at rest as the game makes them, the points, the garbage above the field
    int followed = -1;       // the number of the last pair whose placement was found
    juraic::Placement placed{};
    int chains = 0;          // of the chain that pair started
    int budget = 0;          // garbage the chains of the other side reckoned have still to bring to the notice of this side
    // The pair in play lying where the pieces at rest are the field it appeared over and it: where it would be locked.
    // It may still move; it is locked when the pieces change otherwise (its chain, garbage) or the next pair appears.
    bool lying = false;
    int index = 0;            // its number
    juraic::Placement at{};
    juraic::Pair colors{};
    juraic::GameField base;   // the field it appeared over
    juraic::GameField with;   // ... and it put there
    int sends = 0;            // the garbage its chain would send
    int inAir = 0;            // clear readings in a row with one piece more at rest and a piece of a colour in the air
    uint8_t unsure = 0;       // columns read twelve high whose 13th row is a guess (garbage that may have fallen there)
    int64_t lyingSince = -1;  // the frame the pair in play was first found lying
    // The chain of the pair last found put, played with the rules of the game from the frame it was found lying, a frame
    // a reading: what the AI is shown of this side while it runs (complete).
    juraic::Player run;
    bool running = false;
    int64_t runAt = -1; // the frame of the clock `run` is of
};

class Follower {
public:
    Tracker tracker;
    juraic::Rules rules;        // those of the real game
    SideModel model[2];
    int notice[2] = {};         // the notice taken (Tracker's, unless more than the chains reckoned can have sent)
    int slack = 3;              // pieces the notice may rise beyond what was reckoned, and a tenth of the notice more
    int noticeLong = 60;        // frames after which a notice that has read the same is taken whatever it shows
    int placements = 0;         // pairs whose placement was found
    int checks = 0, misses = 0; // pieces at rest checked against the game as a new pair appeared, and found other than it
    int held = 0;               // notices held back (more than reckoned) until they had read the same for noticeLong frames

    void reset() {
        tracker.reset();
        for (SideModel& m : model) m = SideModel{};
        notice[0] = notice[1] = 0;
        placements = checks = misses = held = 0;
        round = tracker.rounds;
    }

    // The most the notice of side s can show from the chains reckoned; -1: not known (a pair of the other side came to
    // rest and its placement was not found).
    int ceiling(int s) const {
        const SideTrack& o = tracker.side[1 - s];
        const SideModel& m = model[1 - s];
        const bool reckoned = m.known && (m.followed == o.pairs - 1 || m.lying || (o.inHand && o.resting == o.under));
        if (!reckoned) return -1;
        const int most = notice[s] + model[s].budget + (m.lying ? m.sends : 0);
        return most + slack + most / 10;
    }

    void step(const Reading& r, int64_t at) {
        const Before previous[2] = {before(tracker.side[0]), before(tracker.side[1])};
        tracker.step(r, at);
        if (tracker.rounds != round) {
            round = tracker.rounds;
            for (SideModel& m : model) m = SideModel{};
            notice[0] = notice[1] = 0;
        }
        for (int s = 0; s < 2; ++s) {
            const SideTrack& t = tracker.side[s];
            SideModel& m = model[s];
            if (m.running) playOn(m, at);
            if (tracker.spawned[s]) {
                m.running = false;
                if (!m.lying && m.known && previous[s].pairs == t.pairs - 1 && m.followed < t.pairs - 2)
                    recover(s, previous[s], at);
                if (m.lying) lock(s, at);
                // A new pair: the pieces it appears over, against the game (only garbage may have come above them).
                if (m.known && m.followed == t.pairs - 2) {
                    ++checks;
                    if (!fits(withHidden(t.under, m.core.field), m.core.field)) ++misses;
                }
                // (the 13th and 14th rows are not on the screen: their pieces, put there by the moves followed, stay; and
                //  garbage the screen shows fallen since may have reached the 13th row of a column read twelve high)
                const juraic::GameField was = m.core.field;
                m.core.field = withHidden(t.under, was);
                if (m.known && t.under.f.count() > shown(was).f.count() && fits(m.core.field, was))
                    m.core.field = withGarbage(t.under, was, rules.maxGarbagePerDrop / juraic::W, m.unsure);
                else
                    m.unsure &= twelveHigh(t.under);
                m.known = true;
                // (its chain has ended: what it sent is in the notice of the other side)
                model[1 - s].budget = 0;
            }
            // (not only while the pair is in hand: a pair that splits is no longer seen as one while a piece falls)
            if (t.clear && m.known && m.followed < t.pairs - 1) find(s, at);
            takeNotice(s, at);
        }
    }

    // What the AI of seat `me` is shown as its new pair appears (observeSpawn), with the notices taken here and the
    // points as the game reckons them.
    bool observe(int me, const juraic::Rules& real, juraic::ai::Observation& o) const {
        if (!observeSpawn(tracker, me, real, o)) return false;
        complete(o);
        return true;
    }
    // ... and with the own side as the caller follows it at the frame `at` of the clock (observeWith).
    void observeAt(int me, const juraic::Rules& real, const juraic::Player& own, int64_t at, juraic::ai::Observation& o) const {
        observeWith(tracker, me, real, &own, at, o);
        complete(o);
    }

private:
    int round = 0;

    // What recover needs of a side as it was before a reading: its pairs, and the colours of the pair in hand and the
    // field it appeared over.
    struct Before {
        int pairs = 0;
        juraic::Pair colors{};
        juraic::GameField under;
    };
    static Before before(const SideTrack& t) { return {t.pairs, t.colors, t.under}; }

    void complete(juraic::ai::Observation& o) const {
        for (int s = 0; s < 2; ++s) {
            juraic::Player& p = o.player[s];
            p.field = withHidden(p.field, model[s].core.field); // (the 13th and 14th rows as the moves followed put them)
            if (s != o.me && model[s].running) {
                const juraic::Player& run = model[s].run;
                p.field = run.field, p.phase = run.phase, p.wait = run.wait, p.chain = run.chain, p.wave = run.wave, p.placed = run.placed;
            }
            p.incoming = {};
            (tracker.still(1 - s) ? p.incoming.confirmed : p.incoming.pending) = notice[s];
            if (model[s].known) p.scoring = model[s].core.scoring;
        }
    }

public:
    // `read` (the twelve rows the screen shows) with the pieces of `game` in the rows it does not show where they can
    // rest: in the 13th row of a column read twelve high, and in the 14th row.
    static juraic::GameField withHidden(const juraic::GameField& read, const juraic::GameField& game) {
        juraic::GameField g = read;
        int h[juraic::W];
        read.heights(h);
        for (int x = 0; x < juraic::W; ++x)
            if (h[x] == 12 && game.f.get(x, 12) != juraic::EMPTY) g.f.set(x, 12, game.f.get(x, 12));
        g.row14 = game.row14;
        return g;
    }
    // ... and the twelve rows of `g` alone.
    static juraic::GameField shown(const juraic::GameField& g) {
        juraic::GameField s;
        for (int i = 0; i < 3; ++i) s.f.plane[size_t(i)] = g.f.plane[size_t(i)] & juraic::MASK_12;
        return s;
    }
    // `read` is `game` with garbage above it, or nothing more.
    static bool fits(const juraic::GameField& read, const juraic::GameField& game) {
        if (!contains(read, game)) return false;
        int h[juraic::W], r[juraic::W];
        game.heights(h);
        read.heights(r);
        for (int x = 0; x < juraic::W; ++x)
            for (int y = h[x]; y < r[x]; ++y)
                if (read.f.get(x, y) != juraic::GARBAGE) return false;
        return true;
    }
    // The columns of `read` twelve high.
    static uint8_t twelveHigh(const juraic::GameField& read) {
        int h[juraic::W];
        read.heights(h);
        uint8_t columns = 0;
        for (int x = 0; x < juraic::W; ++x)
            if (h[x] == 12) columns |= uint8_t(1u << x);
        return columns;
    }
    // `read` over `before` with garbage fallen on it since (fits): withHidden, and in the 13th row of a column read twelve
    // high the garbage that reached it as far as the screen tells. Garbage falls as many pieces on every column, the rest
    // one each on as many others (game/turn.h dropGarbage; `most`: the most on every column, Rules::maxGarbagePerDrop / W),
    // up to the 13th row: a column not read twelve high shows how many fell on it, as many as on every column or one more.
    // `unsure`: the columns read twelve high whose 13th row is a guess (garbage when as many as every column took can
    // have reached it).
    static juraic::GameField withGarbage(const juraic::GameField& read, const juraic::GameField& before, int most, uint8_t& unsure) {
        juraic::GameField g = withHidden(read, before);
        unsure &= twelveHigh(read);
        int was[juraic::W], now[juraic::W];
        before.heights(was);
        read.heights(now);
        int lo = 0, hi = most; // the pieces every column took
        for (int x = 0; x < juraic::W; ++x)
            if (now[x] < 12) lo = std::max(lo, now[x] - was[x] - 1), hi = std::min(hi, now[x] - was[x]);
        if (lo > hi) return g; // (not garbage as the game lets it fall)
        for (int x = 0; x < juraic::W; ++x) {
            if (now[x] != 12 || was[x] > 12 || g.f.get(x, 12) != juraic::EMPTY) continue;
            const int need = 13 - was[x]; // (the pieces that put one into the 13th row)
            if (hi >= need) g.f.set(x, 12, juraic::GARBAGE);
            if (lo < need && hi + 1 >= need) unsure |= uint8_t(1u << x);
            else unsure &= uint8_t(~(1u << x));
        }
        return g;
    }

private:
    // The move of side s with the pair put at p over `base`: the chain, the garbage sent and offset (into `core` and
    // `other`, the garbage above the other field).
    juraic::TurnResult move(juraic::PlayerCore& core, juraic::Incoming& other, const juraic::GameField& base, juraic::Placement p,
                            juraic::Pair colors, int64_t at) const {
        core.field = base;
        juraic::place(core.field, p, colors);
        juraic::TurnResult result;
        juraic::resolveChain(core, other, rules, tracker.timing, tracker.roundFrame(at), result);
        return result;
    }

    // The pieces of side s since its pair appeared: the pair lying where it would be locked (which of the placements of
    // the game makes them: the field it appeared over - with the pieces of the rows the screen does not show - and the
    // pair, in the twelve rows the screen shows: two pieces more, or one when the other is in the 13th or 14th row),
    // or locked.
    void find(int s, int64_t at) {
        const SideTrack& t = tracker.side[s];
        SideModel& m = model[s];
        // (one piece more when the other went into the 13th or 14th row, which the screen does not show)
        const int more = t.resting.f.count() - t.under.f.count();
        // The pair seen whole in the twelve rows, one piece of it at rest - or a piece of a colour shown in the air above
        // the pieces at rest: the other is in the air over a lower column (turned on the ground, to split as it locks),
        // not in the 13th row. A pair lying is taken out of it when two readings in a row show so (a light beginning over
        // the field, or the first picture of its chain, showed pieces in the air in one picture, and the lying pair was
        // lost). Not once the window has moved on: the next pair, about to appear, is seen where pairs appear before it
        // is counted. (Taken as lying so, on the real game, the down button was let go: a turn on its way lifted one
        // pair into the air, and another, turned out of the 13th row, lay 28 frames before it locked.)
        const bool whole = t.inHand && t.pair.seen && !t.pair.childHidden && !t.pair.axisHidden;
        const bool coming = t.shiftedAt > t.spawnedAt + tracker.pairGap;
        m.inAir = more == 1 && contains(t.resting, t.under) && (whole || (!coming && colouredAbove(tracker.last.side[s], t.resting) > 0)) ? m.inAir + 1 : 0;
        if ((more == 2 || more == 1) && contains(t.resting, t.under)) {
            const bool was = m.lying && m.index == t.pairs - 1;
            const bool lain = was && t.resting == m.with;
            if (m.inAir >= (was ? 2 : 1)) {
                m.lying = false;
                return;
            }
            if (lain) return;
            // The pair lying with both pieces shown, read with one of them missing (a piece that moves drawn over it):
            // still where it lay, unless the screen shows it so in the reading after too (its other piece in the 13th
            // row). Taken from one picture, a placement into the 13th row would keep a piece the game does not have.
            if (more == 1 && m.lying && m.index == t.pairs - 1 && m.with.f.count() - t.under.f.count() == 2 && contains(m.with, t.resting) && at == t.changedAt)
                return;
            const juraic::GameField base = withHidden(t.under, m.core.field), seen = shown(t.resting);
            const unsigned legal = juraic::legalPlacements(base);
            for (juraic::Cell child = 0; child <= juraic::MAX_COLORS; ++child) {
                if (child && t.colors.child) break; // (a child not seen: each colour)
                const juraic::Pair colors{t.colors.axis, t.colors.child ? t.colors.child : child};
                if (!colors.child) continue;
                for (int id = 0; id < juraic::PLACEMENTS; ++id) {
                    if (!(legal >> id & 1)) continue;
                    juraic::GameField g = base;
                    juraic::place(g, juraic::placement(id), colors);
                    if (!(shown(g).f == seen.f)) continue;
                    if (!was) m.lyingSince = at;
                    m.lying = true, m.index = t.pairs - 1, m.at = juraic::placement(id), m.colors = colors, m.base = base, m.with = t.resting;
                    juraic::PlayerCore core = m.core;
                    juraic::Incoming other{0, notice[1 - s]};
                    core.incoming = {0, notice[s]};
                    m.sends = move(core, other, m.base, m.at, m.colors, at).sent;
                    return;
                }
            }
            m.lying = false;
        } else if (m.lying) {
            // Locked: the pieces under it changed (its chain), or more came above them (garbage). Otherwise it is moving
            // still (lifted, or half over a lower column).
            if (!contains(t.resting, shown(m.base)) || t.resting.f.count() > shown(m.base).f.count() + 2) lock(s, at);
            else m.lying = false;
        }
    }

    // A landing hidden by an animation can be identified later by the field the next pair appears over. Only a
    // resolved chain whose visible result matches (with possible garbage above it) is accepted. Distinct placements
    // must put the same coloured pieces before resolving, or the move is ambiguous and stays unknown.
    void recover(int s, const Before& before, int64_t at) {
        SideModel& m = model[s];
        if (!before.colors.axis || !before.colors.child) return;
        const juraic::GameField base = withHidden(before.under, m.core.field);
        const juraic::GameField seen = shown(tracker.side[s].under);
        const unsigned legal = juraic::legalPlacements(base);
        int found = -1;
        juraic::GameField placed;
        for (int id = 0; id < juraic::PLACEMENTS; ++id) {
            if (!(legal >> id & 1)) continue;
            juraic::PlayerCore core = m.core;
            juraic::Incoming other;
            const juraic::TurnResult result = move(core, other, base, juraic::placement(id), before.colors, at);
            if (!result.chains || !fits(seen, shown(core.field))) continue;
            juraic::GameField candidate = base;
            juraic::place(candidate, juraic::placement(id), before.colors);
            if (found >= 0 && !(candidate == placed)) return;
            found = id, placed = candidate;
        }
        if (found < 0) return;
        m.base = base, m.at = juraic::placement(found), m.colors = before.colors, m.index = before.pairs - 1;
        lock(s, at);
    }

    // The pair of side s that lay is locked where it lay: its move as the game makes it.
    void lock(int s, int64_t at) {
        SideModel& m = model[s];
        SideModel& o = model[1 - s];
        const juraic::Scoring scoring = m.core.scoring;
        m.core.incoming = {0, notice[s]}, o.core.incoming = {0, notice[1 - s]};
        const juraic::TurnResult result = move(m.core, o.core.incoming, m.base, m.at, m.colors, at);
        // (its chain from the frame it was found lying - not one found only as the next pair appeared, whose frame is
        //  not known)
        m.running = result.chains > 0 && m.lyingSince >= 0 && m.lyingSince <= at && m.followed != m.index;
        if (m.running) {
            juraic::Player& p = m.run;
            p = juraic::Player{};
            p.field = m.base, p.scoring = scoring;
            p.placed = juraic::place(p.field, m.at, m.colors);
            const int split = tracker.timing.splitFrames(p.placed.split, m.at.x, p.placed.lockY);
            p.phase = split ? juraic::Phase::Split : juraic::Phase::Land;
            p.wait = split ? split : tracker.timing.landFrames;
            m.runAt = m.lyingSince;
            playOn(m, at);
        }
        m.followed = m.index, m.placed = m.at, m.chains = result.chains, m.lying = false, m.lyingSince = -1;
        ++placements;
        o.budget += result.sent;
    }
    // The chain of `m` played on to the frame `at` of the clock (no garbage, no buttons); over as it is decided.
    void playOn(SideModel& m, int64_t at) const {
        const juraic::Tsumo none;
        while (m.running && m.runAt < at) {
            juraic::Incoming nobody;
            juraic::advancePlayer(m.run, nobody, 0, none, rules, tracker.timing, tracker.roundFrame(m.runAt));
            ++m.runAt;
            if (m.run.phase != juraic::Phase::Split && m.run.phase != juraic::Phase::Land && m.run.phase != juraic::Phase::Wave) m.running = false;
        }
    }

    // The notice of side s: Tracker's (read the same for a few frames), unless more than can have been sent. (Less is
    // always taken: garbage falls, chains offset it.)
    void takeNotice(int s, int64_t at) {
        const SideTrack& t = tracker.side[s];
        if (t.notice == notice[s]) return;
        const int most = ceiling(s);
        const bool take = most < 0 || t.notice <= most;
        if (!take && at - t.noticeSince + 1 < noticeLong) return;
        if (!take) ++held;
        if (t.notice > notice[s]) model[s].budget = std::max(0, model[s].budget - (t.notice - notice[s]));
        notice[s] = t.notice;
    }
};

} // namespace play::realgame
