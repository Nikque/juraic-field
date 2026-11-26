#pragma once
// Memory first, the player: what primary14.h reads is made into what the AI is shown (PrimaryObserver14), and the AI
// is played on it frame by frame of the game's own counter (PrimaryPlayer14), its buttons planned ahead by the frames
// they take to reach the game (predictive_input.h) and the timers of its pair kept from its own buttons
// (pose_input_model.h).
//
// From memory: both fields (14 rows), each seat's pair in play (cell, half, direction, colours), the count of the pairs
// put, the two pairs shown as next, the game's frame counter, what the seat's field is doing (its mark of being
// busy and the number of what it is busy with), the pieces the game counts in each field, the height of its sixth
// column, the pieces it counts in each column, the garbage the chain a field is busy with makes in all, the game's
// account of the garbage waiting above each field (PrimarySeat14::tray*) and whether the all-clear bonus waits.
// Given by the caller: the garbage waiting above each field as a number (`notice`; PrimaryDrive14 takes it from
// PrimarySeat14::notice where that was read). Left out: the scores, how far a chain has come.
//
// Garbage that has fallen is not in the board read until the seat's next pair has been put (records of the real game,
// 19 falls of 19), but the game's count of the pieces has it at once: what the count has more than the board is
// garbage lying on the field. Which columns it fell on is read too: the game counts the pieces of each column
// (PrimarySeat14::columnPieces), and what a column has more than the board is garbage lying on it
// (PrimaryObserver14::withFallen). Where those counts were not read: a sixth of the garbage on every column, and of the
// pieces left over only one on the sixth column, whose height is read - the field is then shown without the others,
// and marked as not sure (hidden()).
//
// How the frames of the real game relate to the frames of the engine:
//  - A sample of the counter t is the game after its frame t. The AI is shown it as the frame t + 1 to play.
//  - A new pair is in the player's hands (the mark `operating`) one frame before a button moves it: in that first frame
//    it is where it appears, whatever is held (301 pairs of 301). The engine has a pair appear in the frame it first
//    moves in, so that first frame is shown to the AI as the last frame before its pair appears (Phase::Next, wait 1).
//  - Between two pairs the field is busy for a while (the end of the landing, a chain, garbage falling). The next pair
//    is in the player's hands `quietFrames` frames after the last busy frame (10, in every gap read without a frame
//    missing; the frame before that is marked 8 when no garbage falls). So from the last busy frame on the frame the
//    pair appears in is known exactly, 11 frames ahead; before it only that it is no sooner.
//  - A pair dropped with the down button lies for one frame in the player's hands before the mark goes
//    (Timing::landThenLock has it so; with the frame counts of DEFAULT_TIMING the engine locks it a frame sooner, and
//    the pair shown lying is then taken up once more for that frame).
#include "../../ai/agent.h"
#include "../../ai/observe.h"
#include "pose_input_model.h"
#include "predictive_input.h"
#include "primary14.h"
#include "primary_timing14.h"
#include <functional>
#include <memory>

namespace play::realgame {

class PrimaryObserver14 {
public:
    juraic::Rules rules = juraic::realGameRules(); // those of the real game
    juraic::Timing timing; // the frame counts the AI reckons with
    int quietFrames = 10;  // from the last busy frame of a field to the first frame its next pair is in the player's hands
    // The counter in the frame the first pair of a round is in the hands (not moved yet): 10 in every round of the real
    // game read from its beginning (8 of 8). The counter stands at 0 while the round is made ready and goes on from there.
    int64_t firstTick = 10;

    // (what is known of the round is forgotten; the seats as last read are kept)
    void reset() {
        begun_ = timed_ = false, generation_ = 0, start_ = 0, tick_ = -1;
        put_ = {}, operated_ = {}, shown_ = {}, busyLast_ = {}, hand_ = {}, slid_ = {};
        lay_ = {-1, -1}, busy_ = {-1, -1}, due_ = {-1, -1}, seen_ = {-1, -1}, placed_ = {true, true};
        pairs_.clear(), learned_.clear();
        hidden_ = {}, fallColumns_ = {}, chainBase_ = {-1, -1}, made_ = {-1, -1};
        running_ = {}, beforeKnown_ = {}, phaseWas_ = {};
        allClear_ = {}, beforePieces_ = {-1, -1}, bonus_ = {}, rest_ = {}, bonusRead_ = {};
        colours_ = {};
        ground_ = {-1, -1}, restless_ = {}, cleared_ = {-1, -1};
        arrived_ = {}, noticeWas_ = {}, hisStood_ = {}, hisCame_ = {}, hisWas_ = {};
    }
    // The colours in play, as the AI is told them (Rules::palette: its searches make up pairs of the first
    // Rules::colors of them, and try those on a field): four of the game's five are in play in a round, another four
    // in the next. `seen`: the colours the round has shown so far (by their cell code). Each of them that is not among
    // the first Rules::colors of `rules` takes the place of the first of those that has not been shown; the others
    // stay where they are (so rules whose colours are those shown are left as they are).
    static void coloursInPlay(const std::array<bool, juraic::MAX_COLORS + 1>& seen, juraic::Rules& rules) {
        const size_t inPlay = size_t(rules.colorsInPlay());
        for (size_t c = 1; c <= size_t(juraic::MAX_COLORS); ++c) {
            if (!seen[c]) continue;
            size_t at = 0;
            while (at < rules.palette.size() && rules.palette[at] != juraic::Cell(c)) ++at;
            if (at < inPlay || at == rules.palette.size()) continue;
            for (size_t k = 0; k < inPlay; ++k) {
                const size_t code = size_t(rules.palette[k]);
                if (code >= 1 && code <= size_t(juraic::MAX_COLORS) && !seen[code]) {
                    std::swap(rules.palette[k], rules.palette[at]);
                    break;
                }
            }
        }
    }
    // The colours a field holds, added to `seen`.
    static void coloursOf(const juraic::GameField& field, std::array<bool, juraic::MAX_COLORS + 1>& seen) {
        for (int x = 0; x < juraic::W; ++x) {
            for (int y = 0; y < 13; ++y) {
                const int v = int(field.f.get(x, y));
                if (v >= 1 && v <= juraic::MAX_COLORS) seen[size_t(v)] = true;
            }
            const int top = int(field.row14[size_t(x)]);
            if (top >= 1 && top <= juraic::MAX_COLORS) seen[size_t(top)] = true;
        }
    }
    PrimaryObserver14() = default;
    PrimaryObserver14(const juraic::Rules& r, const juraic::Timing& t) : rules(r), timing(t) {}

    // The frame counter of the seat `me` in the sample (what a caller steps by), or -1 when the seat was not read.
    static int64_t tickOf(const PrimarySample14& s, int me) { return s.seat[size_t(me)] ? int64_t(s.seat[size_t(me)]->pose.count108) : -1; }
    // A round is being played on the seat (the numbers a seat has from its first pair to the end of the round).
    static bool playing(const OwnedPublicPose14& p) { return p.fieldState == 1 && p.playerState == 1; }
    // The round is being made ready on the seat: its objects are new, its counter stands at 0 (for two seconds and more
    // in the real game, then it goes on and the seat plays), no pair yet.
    static bool ready(const OwnedPublicPose14& p) { return p.fieldState == 0 && p.playerState == 0 && p.count108 == 0 && !p.operating && !p.lockedPairs; }
    // The first two pairs of a round as the window holds them before its first pair is in the hands: its first record
    // empty, the pair to come in the second, the one after it in the third (so while the round is made ready and while
    // the window then slides; records of the real game, 7 rounds of 7). Nothing when the records are not so.
    static std::optional<std::array<juraic::Pair, 2>> firstPairs(const PrimaryWindow14& w) {
        if (w.stage != PrimaryWindow14::Kind && w.stage != PrimaryWindow14::Sliding) return {};
        if (w.records[0][0] != 0 || w.records[1][0] != 0x201 || w.records[2][0] != 0x201) return {};
        std::array<juraic::Pair, 2> out;
        for (size_t k = 0; k < 2; ++k) {
            const uint32_t axis = w.records[k + 1][1], child = w.records[k + 1][2];
            if (axis < 1 || axis > 5 || child < 1 || child > 5) return {};
            out[k] = {juraic::Cell(axis), juraic::Cell(child)};
        }
        return out;
    }
    // The pair is where a pair appears, as it appears.
    static bool appearing(const OwnedPublicPose14& p) {
        const juraic::PairControl born;
        return p.x == born.x && p.y == born.y && p.upper == born.upper && p.dir == born.dir && !p.horizontalMoving;
    }

    // Garbage that has fallen on a seat and is not in its board yet (see the head of this file).
    struct Hidden {
        int pieces = 0;   // pieces the game counts in the field beyond those of the board
        int rows = 0;     // ... of them, whole rows: put on every column of the field shown
        bool sure = true; // none left over whose column is not known (the field shown is the field)
        bool byColumns = false; // put on the columns the game counts them in (PrimarySeat14::columnPieces), not by rows
    };
    // Last column counts verified during this fall while some garbage was above row 13.
    struct FallColumns {
        int64_t tick = -1;
        uint32_t pairs = 0;
        juraic::GameField board{};
        std::array<uint8_t, juraic::W> columns{};

        bool losingTop(const PrimarySeat14& seat, int lag) const {
            const int64_t elapsed = int64_t(seat.pose.count108) - tick;
            if (tick < 0 || elapsed < 1 || elapsed > 2 || pairs != seat.pose.lockedPairs || !(board == seat.field)) return false;
            int removed = 0;
            for (size_t x = 0; x < columns.size(); ++x) {
                if (columns[x] == seat.columnPieces[x]) continue;
                if (columns[x] != 14 || seat.columnPieces[x] != 13 || board.row14[x] != juraic::EMPTY) return false;
                ++removed;
            }
            return lag > 0 && lag <= removed;
        }
    };
    // The field of a seat as it is shown: its board, with the garbage that has fallen and is not in it. Nothing is added
    // while the counts and the board are not of the same moment: the field busy with a chain and no pair in the hands,
    // and the frames after a pair whose chain the board has cleared already while the counts still have its pieces -
    // then the game counts more pieces than garbage beyond the board (records of the real game: eleven such waits of
    // fourteen frames; after each of eleven falls of garbage the two counts were the same number beyond the board).
    // Garbage on its way down is added from the first frame of its fall: the game counts it in the field from then on
    // and has it above the field no more, and the fall takes 40 to 50 frames.
    static Hidden withFallen(const PrimarySeat14& seat, juraic::GameField& field, FallColumns* fall = nullptr) {
        Hidden h;
        field = seat.field;
        const OwnedPublicPose14& p = seat.pose;
        const bool falling = !p.operating && p.fieldActive && p.phase == 10 && seat.chainGarbage == 0;
        if (fall && !falling) fall->tick = -1;
        if (!seat.counted || (!p.operating && p.fieldActive && (p.phase == 1 || p.phase == 2 || seat.chainGarbage > 0))) return h;
        int inBoard = 0, garbageInBoard = 0, heights[juraic::W];
        for (int x = 0; x < juraic::W; ++x) {
            heights[x] = 0;
            for (int y = 0; y < 13; ++y)
                if (field.f.get(x, y) != juraic::EMPTY) ++inBoard, garbageInBoard += field.f.get(x, y) == juraic::GARBAGE, heights[x] = y + 1;
            if (field.row14[size_t(x)] != juraic::EMPTY) ++inBoard, garbageInBoard += field.row14[size_t(x)] == juraic::GARBAGE;
        }
        h.pieces = seat.pieces - inBoard;
        const int lag = seat.garbagePieces - garbageInBoard - h.pieces;
        // A piece above row 13 disappears from its column and the total one or two frames before the garbage
        // count catches up. The remaining pieces are still on the same board and their columns are known.
        if (h.pieces <= 0 || (lag != 0 && !(falling && fall && seat.columnsKnown && fall->losingTop(seat, lag)))) return Hidden{};
        h.rows = h.pieces / 6;
        // The columns the garbage fell on, when the pieces the game counts in each column were read
        // (PrimarySeat14::columnPieces): what a column has more than the board is garbage lying on it. (Not above the
        // 13th row: what would lie there is left out, and the field shown is then not sure.)
        if (seat.columnsKnown) {
            int more[juraic::W], all = 0;
            bool fits = true;
            for (int x = 0; x < juraic::W; ++x) {
                int inColumn = field.row14[size_t(x)] != juraic::EMPTY ? 1 : 0;
                for (int y = 0; y < 13; ++y) inColumn += field.f.get(x, y) != juraic::EMPTY;
                more[x] = int(seat.columnPieces[size_t(x)]) - inColumn, all += more[x];
                fits = fits && more[x] >= 0;
            }
            if (fits && all == h.pieces) {
                int left = 0;
                for (int x = 0; x < juraic::W; ++x)
                    for (int k = 0; k < more[x]; ++k) {
                        if (heights[x] < 13) field.f.set(x, heights[x]++, juraic::GARBAGE);
                        else ++left;
                    }
                h.sure = left == 0, h.byColumns = true;
                if (fall && falling && lag == 0) {
                    if (left) *fall = {int64_t(p.count108), p.lockedPairs, seat.field, seat.columnPieces};
                    else fall->tick = -1;
                }
                return h;
            }
        }
        if (lag != 0) return Hidden{}; // (with inconsistent totals, only the verified columns can be used)
        int left = h.pieces - 6 * h.rows;
        for (int x = 0; x < juraic::W; ++x)
            for (int k = 0; k < h.rows && heights[x] < 13; ++k) field.f.set(x, heights[x]++, juraic::GARBAGE); // (none above the 13th row)
        // (the sixth column is known as it is: a piece left over that fell on it)
        if (left > 0 && seat.sixthHeight == heights[5] + 1 && heights[5] < 13) field.f.set(5, heights[5]++, juraic::GARBAGE), --left;
        h.sure = left == 0 && seat.sixthHeight == heights[5];
        return h;
    }
    // What the last sample observed had of garbage not in the board of the seat.
    const Hidden& hidden(int seat) const { return hidden_[size_t(seat)]; }

    // The garbage waiting above a seat: `notice` is what the caller read of it. While the other seat's field is busy with
    // a chain, what that chain makes in all (PrimarySeat14::chainGarbage) cannot fall yet: the notice as it stood when
    // the chain began can (Incoming::confirmed), the rest cannot (Incoming::pending) - what the chain has sent to the
    // notice and is still there, and what is still to come of it: the chain's garbage in all less what has come to the
    // notice since it began and what it has taken from above its own field, and of that only what is left once the
    // garbage still above its own field is gone, which the chain takes from first.
    // (`bonus`: the garbage of an all-clear bonus the chain takes, which the game's count of the chain's garbage leaves out)
    juraic::Incoming waiting(int seat, int notice, const PrimarySeat14& other, int bonus = 0) {
        juraic::Incoming in;
        const size_t k = size_t(seat);
        const int all = int(other.chainGarbage) + bonus;
        const bool chain = other.counted && all > 0;
        int& base = chainBase_[k];
        rest_[k] = 0;
        if (!chain) {
            base = -1;
            in.confirmed = notice;
            return in;
        }
        // (the garbage above the chain's own field)
        const int his = other.trayKnown ? other.trayConfirmed + other.trayPending + other.trayFlying : other.noticeKnown ? other.notice : 0;
        if (base < 0) base = notice, arrived_[k] = 0, noticeWas_[k] = notice, hisStood_[k] = his, hisCame_[k] = 0, hisWas_[k] = his;
        // (what the notice has more than in the last sample has come from the chain; what it has less, the seat's own
        //  chain has cancelled)
        if (notice > noticeWas_[k]) arrived_[k] += notice - noticeWas_[k];
        if (his > hisWas_[k]) hisCame_[k] += his - hisWas_[k];
        noticeWas_[k] = notice, hisWas_[k] = his;
        if (notice < base) base = notice; // (cancelled by the seat's own chain meanwhile)
        in.confirmed = base;
        const int taken = std::max(0, hisStood_[k] + hisCame_[k] - his);
        const int rest = all - arrived_[k] - taken;
        in.pending = notice - base + std::max(0, rest - his);
        rest_[k] = std::max(0, rest); // (it takes from the garbage above the chain's own field first: observe)
        return in;
    }
    // The same where the game's own account of the seat's garbage was read (PrimarySeat14::trayKnown): what can fall
    // is what the game has as such; what cannot yet is what the other seat's chain has sent so far (arrived or on its
    // way) and what is still to come of it. Still to come: the chain's garbage in all (PrimarySeat14::chainGarbage)
    // less what the chain has made so far, and of that only what is left once the garbage still above the chain's own
    // field is gone, which the chain takes from first. Made so far: what has been seen on its way to this seat since
    // the chain began, and what the chain has taken from above its own field - the garbage that stood there when it
    // began and what has been seen on its way there since, less what is there now. (A seat without the account: as above.)
    juraic::Incoming waiting(int seat, int notice, const PrimarySeat14& own, const PrimarySeat14& other, int bonus = 0) {
        int& made = made_[size_t(seat)];
        rest_[size_t(seat)] = 0;
        // (a sample without the game's account, or without the other seat's numbers, does not end the count of what the
        //  chain has sent: begun anew, what was sent before would be counted to come once more)
        if (!own.trayKnown) return waiting(seat, notice, other, bonus);
        const uint32_t all = other.chainGarbage + uint32_t(bonus);
        chainBase_[size_t(seat)] = -1;
        juraic::Incoming in;
        in.confirmed = own.trayConfirmed;
        in.pending = own.trayPending + own.trayFlying;
        if (!other.counted) return in;
        if (all == 0) {
            made = -1;
            return in;
        }
        const size_t k = size_t(seat);
        const int his = other.trayKnown ? other.trayConfirmed + other.trayPending + other.trayFlying : other.noticeKnown ? other.notice : 0;
        const Flight mine{own.trayFlying, own.trayConfirmed + own.trayPending};
        const Flight theirs{other.trayFlying, other.trayConfirmed + other.trayPending};
        if (made < 0 || other.chainGarbage != chainOf_[k]) {
            made = 0, came_[k] = 0, stood_[k] = his, hisFlying_[k] = {}; // (the chain begins: its garbage is there before its first step)
        } else {
            made += mine.sentSince(flying_[k]);
            if (other.trayKnown) came_[k] += theirs.sentSince(hisFlying_[k]);
        }
        chainOf_[k] = other.chainGarbage, flying_[k] = mine;
        if (other.trayKnown) hisFlying_[k] = theirs;
        const int taken = std::max(0, stood_[k] + came_[k] - his);
        const int rest = int(all) - made - taken;
        if (rest > his) in.pending += rest - his;
        rest_[k] = std::max(0, rest); // (it takes from the garbage above the chain's own field first: observe)
        return in;
    }

    // Fills `o` from the sample, for the AI of seat `me`. False when the seat was not read, the other seat has not been
    // read for long, or no round is being played (a seat is not in a round, or neither seat has had a pair yet): `o` is
    // then not to be used.
    bool observe(const PrimarySample14& s, int me, const std::array<int, 2>& notice, juraic::ai::Observation& o) {
        if (me < 0 || me > 1 || !s.seat[size_t(me)]) return false;
        const int64_t tick = tickOf(s, me);
        // A new round: another binding, a counter or a count of pairs put that went back.
        if (!begun_ || s.generation != generation_ || tick < tick_) {
            reset();
            kept_ = {}, begun_ = true, generation_ = s.generation, fresh_ = true;
        }
        tick_ = tick;
        // The seats: one not read in this sample (it changed between its two reads) is taken as it was last read, for
        // up to STALE frames - a sample without the other seat is no reason to begin the round anew.
        std::array<const PrimarySeat14*, 2> seats{};
        std::array<bool, 2> stale{};
        for (size_t i = 0; i < 2; ++i) {
            if (s.seat[i]) kept_[i] = s.seat[i], keptAt_[i] = tick;
            else if (!kept_[i] || tick < keptAt_[i] || tick - keptAt_[i] > STALE) return false;
            else stale[i] = true;
            seats[i] = &*kept_[i];
        }
        bool back = false;
        for (size_t i = 0; i < 2; ++i) back = back || seats[i]->pose.lockedPairs < put_[i];
        if (back) {
            reset();
            begun_ = true, generation_ = s.generation, tick_ = tick, fresh_ = true;
        }
        // (the round is over, not begun yet, or the numbers of a seat are out of place in this sample: nothing is shown,
        //  and what is known of the round is kept - a new round is told by the counter, the binding and the count of
        //  pairs put, above)
        if (!(playing(seats[0]->pose) || ready(seats[0]->pose)) || !(playing(seats[1]->pose) || ready(seats[1]->pose))) return false;
        // (each seat's field as it is shown: its board with the garbage that has fallen and is not in it yet)
        std::array<juraic::GameField, 2> fields;
        std::array<Hidden, 2> fallen;
        for (size_t i = 0; i < 2; ++i) fallen[i] = withFallen(*seats[i], fields[i], &fallColumns_[i]);
        bool any = false, opening = true;
        for (size_t i = 0; i < 2; ++i) {
            const PrimarySeat14& seat = *seats[i];
            const OwnedPublicPose14& p = seat.pose;
            const int64_t at = stale[i] ? tick : int64_t(p.count108);
            const int index = int(p.lockedPairs); // (the number of the pair in play, or of the one that comes)
            placed_[i] = !p.operating || seat.placed;
            // The all-clear bonus as the game has it, where it was read: it goes in the frame the pair of the chain that
            // takes it is put (without it: told from the boards, below).
            bool took = false;
            if (seat.bonusKnown && !stale[i]) took = bonusRead_[i] && allClear_[i] && !seat.bonusWaits && !p.operating, allClear_[i] = seat.bonusWaits;
            if (!stale[i]) bonusRead_[i] = seat.bonusKnown;
            bool put = false; // (the frame the pair was put in was seen)
            // Before the first pair of the round: the window holds that pair and the one after it from the time the
            // round is made ready, and once it stands, a frame before the pair, the third as well.
            if (p.operating || p.lockedPairs || at >= firstTick) opening = false;
            else if (!stale[i]) {
                if (seat.windowKnown)
                    for (int k = 0; k < 3; ++k) learn(k, seat.window[size_t(k)]);
                else if (const auto two = firstPairs(seat.windowRead))
                    learn(0, (*two)[0]), learn(1, (*two)[1]);
            }
            // Between two pairs the window stands with the pair just put and the two after it, slides, and stands again
            // a frame before the next pair is in the hands, with that pair and the two after it: the third of them is
            // known a frame sooner so. (Only a window seen to slide since the pair was put is taken as the new one.)
            if (!p.operating && p.lockedPairs && !stale[i]) {
                if (seat.windowRead.stage == PrimaryWindow14::Sliding) slid_[i] = true;
                else if (seat.windowKnown && slid_[i])
                    for (int k = 0; k < 3; ++k) learn(index + k, seat.window[size_t(k)]);
            } else if (!stale[i]) {
                slid_[i] = false;
            }
            if (p.operating) {
                // (a pair newly in the hands: in its first frame it is shown, and moved from the next frame on)
                const bool taken = !operated_[i] || p.lockedPairs != put_[i];
                if (taken) hand_[i] = juraic::PairControl{}, lay_[i] = ground_[i] = -1, restless_[i] = false;
                const juraic::PairControl was = hand_[i]; // (where the pair was last read)
                bonus_[i] = 0;
                if (seat.placed) hand_[i] = control(p);
                // (its place not read in that frame: it is the first frame when the seat had no pair the frame before)
                if (taken) shown_[i] = seat.placed ? appearing(p) && (busy_[i] < 0 || at <= busy_[i] + quietFrames) : !operated_[i] && seen_[i] == at - 1;
                else shown_[i] = false;
                if (seat.placed) learn(index, {juraic::Cell(p.currentColors[0]), juraic::Cell(p.currentColors[1])});
                if (seat.pairsKnown) learn(index + 1, seat.pairs[1]), learn(index + 2, seat.pairs[2]);
                // (the pairs beyond the window, when they are read: PrimarySeat14::listed)
                for (size_t k = 0; seat.pairsKnown && seat.listedKnown && k < LISTED14; ++k) learn(index + 3 + int(k), seat.listed[k]);
                // (the board while the pair is in the hands: what the pair is put on - chainOf)
                if (!stale[i]) {
                    // (with the garbage that has fallen and is not in the board yet: the board has it when the pair is put)
                    before_[i] = fields[i];
                    beforeKnown_[i] = known(index) && fallen[i].sure, running_[i] = Running{};
                    if (beforeKnown_[i]) beforePair_[i] = pairs_[size_t(index)];
                    beforePieces_[i] = piecesIn(before_[i]);
                }
                // (the first pair of the round appears in the frame Timing::firstSpawn of the round)
                if (index == 0 && shown_[i] && !timed_) start_ = at + 1 - timing.firstSpawn, timed_ = true;
                // the frames the pair has lain (the landing animation runs from the first of them)
                if (seat.placed && !stale[i]) lie(i, at, fields[i], was, !taken);
            } else {
                if (operated_[i]) {
                    put = true;
                    // (locked in this frame: the next pair is in the hands when the landing is over, if nothing else happens)
                    // (the landing animation has run since the pair began to lie - Timing::firstOwnLanding: with its axis's
                    //  own column under it -, and what is left of it is waited for, Timing::lockWait frames at least, as
                    //  advancePlayer has it; the lock in the frame before this one)
                    const int64_t from = timing.firstOwnLanding ? lay_[i] : ground_[i];
                    const int rested = from >= 0 ? int(std::max<int64_t>(1, std::min<int64_t>(at - from, 255))) : 1;
                    int land = std::max(timing.lockWait, timing.landFrames - rested);
                    // (a piece that falls apart from the pair: the frames of its fall (Timing::split, by the rows it falls),
                    //  and after them the landing counted from the pair's first frame lying, a frame sooner with the axis
                    //  in the 13th row - records of the real game: 109 pairs of 112 so, the other 3 the axis in the 13th
                    //  row. The placement is the one that puts the pair on the board it was put on and leaves the board of
                    //  this frame, or a chain that ends in it (chainOf) - none when no placement does, or when two that do
                    //  fall apart otherwise: garbage not in the board, a board not read whole.)
                    Running chained;
                    if (beforeKnown_[i] && !stale[i]) {
                        int rows = -1;
                        bool top = false;
                        const unsigned legal = juraic::legalPlacements(before_[i]);
                        for (int id = 0; id < juraic::PLACEMENTS && rows != -2; ++id) {
                            if (!(legal >> id & 1)) continue;
                            juraic::GameField g = before_[i];
                            const juraic::PlaceResult r = juraic::place(g, juraic::placement(id), beforePair_[i]);
                            if (!(g == seat.field)) continue;
                            if (rows >= 0 && (r.split != rows || (r.lockY >= 12) != top)) rows = -2;
                            else rows = r.split, top = r.lockY >= 12;
                        }
                        chained = chainOf(before_[i], beforePair_[i], seat.field, rules, timing);
                        if (rows == -1 && chained.waves) rows = chained.split, top = chained.splitTop;
                        if (rows > 0) {
                            const int fall = rows <= 11 ? timing.split[rows] : timing.split[11] + 2 * (rows - 11);
                            land = fall + timing.landFrames - 1 - (top ? 1 : 0);
                        }
                    }
                    due_[i] = at - 1 + land + timing.nextFrames;
                    lay_[i] = ground_[i] = -1, restless_[i] = false, cleared_[i] = -1, busy_[i] = -1, busyLast_[i] = false;
                    // (the chain the pair begins: the board has its end in the frame the pair is put)
                    running_[i] = Running{};
                    if (beforeKnown_[i] && !stale[i]) {
                        running_[i] = chained;
                        running_[i].base = seat.counted ? seat.pieces : -1, running_[i].waveAt = at + 1 + land;
                    }
                    beforeKnown_[i] = false;
                    // The all-clear bonus that waits for the seat's next chain: the board has the end of the chain of the
                    // pair just put, and a field that chain leaves empty gives the bonus; a chain that does not takes it.
                    // (The garbage of the bonus is not in the game's count of the chain's garbage: `bonus_`, while that chain runs.)
                    if (!stale[i] && !seat.bonusKnown) {
                        const int left = piecesIn(seat.field);
                        const bool chained = beforePieces_[i] >= 0 && left < beforePieces_[i] + 2;
                        bonus_[i] = chained && allClear_[i] ? rules.allClearBonus / rules.rateAt(tick - start_ + 1) : 0;
                        if (left == 0) allClear_[i] = true;
                        else if (chained) allClear_[i] = false;
                    }
                    beforePieces_[i] = -1;
                }
                if (took) bonus_[i] = rules.allClearBonus / rules.rateAt(tick - start_ + 1);
                if (Running& r = running_[i]; r.waves && !stale[i]) {
                    // The wave that runs: one more each time the field begins to clear, and no earlier one than the
                    // pieces the game still counts in the field allow. Over: the field quiet again after a wave.
                    if (p.phase == 1 && phaseWas_[i] != 1) r.wave = r.entered++, r.waveAt = at;
                    if (r.base >= 0 && seat.counted) {
                        int done = 0;
                        while (done < r.waves && r.gone[size_t(done)] <= r.base - seat.pieces) ++done;
                        if (done - 1 > r.wave) r.wave = done - 1, r.entered = done, r.waveAt = at;
                    }
                    if (r.entered && (!p.fieldActive || p.phase == LAST_BUSY)) r = Running{};
                }
                if (!p.fieldActive && busy_[i] >= 0) bonus_[i] = 0; // (the chain is over)
                if (stale[i]) {
                } else if (p.fieldActive) {
                    busy_[i] = at, busyLast_[i] = p.phase == LAST_BUSY;
                } else if (busy_[i] >= 0 && !busyLast_[i] && seen_[i] >= 0 && at > seen_[i] + 1 && seen_[i] == busy_[i]) {
                    // (frames not seen between the last busy frame seen and this quiet one, and that one was not marked
                    //  as the last: the last busy frame is one of those not seen - the latest of them, so that no pair
                    //  is reckoned to come sooner than it does)
                    busy_[i] = at - 1, busyLast_[i] = true;
                }
                shown_[i] = false;
            }
            // (a pair put while the seat was not read: what was kept of the chain and of the landing before it is of
            //  another pair)
            if (p.lockedPairs != put_[i] && !put) {
                running_[i] = Running{}, due_[i] = busy_[i] = -1, bonus_[i] = 0;
                // (with the next pair in the hands already, the board and the lying of this sample are that pair's)
                if (!p.operating) lay_[i] = ground_[i] = -1, restless_[i] = false, beforeKnown_[i] = false, beforePieces_[i] = -1;
                cleared_[i] = -1;
            }
            // (the frame the seat's field last began to clear)
            if (!stale[i] && !p.operating && p.fieldActive && p.phase == 1 && phaseWas_[i] != 1) cleared_[i] = at;
            if (!stale[i]) seen_[i] = at, phaseWas_[i] = p.phase;
            operated_[i] = p.operating, put_[i] = p.lockedPairs;
            if (p.operating || put_[i]) any = true;
        }
        // Neither seat has had a pair yet: the first one is in the hands in the frame firstTick of the counter. Once it is
        // known what it is, the AI is shown the frames before it, so that it has its placement when the pair appears.
        // (The frames of the round are counted so that the first pair appears in the frame Timing::firstSpawn - or later,
        //  so that the frame the round is made ready in, which lasts seconds, is the frame 0: the AI has that time for
        //  its first placement.)
        if (!any && opening && known(0)) {
            if (!timed_) start_ = std::min<int64_t>(firstTick + 1 - timing.firstSpawn, 1), timed_ = true;
            any = tick - start_ + 1 >= 0;
        }
        if (!any) return false;
        if (fresh_) ++round_, fresh_ = false;
        o.rules = rules, o.timing = timing, o.players = 2, o.me = me;
        o.frame = tick - start_ + 1; // (the frame that is played next)
        o.state = juraic::RoundState::Playing;
        o.tsumo1.pairs.clear();
        o.busyLeft[0] = o.busyLeft[1] = 0, o.busyChain[0] = o.busyChain[1] = false;
        // The pairs shown to each seat: as far as they are known without a gap from that seat's next pair on. (A round
        // joined on its way, with one seat three pairs and more ahead of the other: the pairs between the two windows
        // have not been seen. Shown to the AI as pairs they would stand for pairs they are not: the own choice made with
        // them would be dropped as its pair appears and the window shows what they are - the pair then falls for the time
        // of a new choice -, and the reading of the other seat would put pairs he does not have.) Two seats shown as
        // many share one list; otherwise each has its own (Observation::ownTsumo).
        std::array<size_t, 2> shownPairs{pairs_.size(), pairs_.size()};
        for (size_t i = 0; i < 2; ++i)
            for (size_t k = size_t(seats[i]->pose.lockedPairs); k < pairs_.size(); ++k)
                if (!learned_[k]) {
                    shownPairs[i] = k;
                    break;
                }
        o.ownTsumo = shownPairs[0] != shownPairs[1];
        o.tsumo.pairs.assign(pairs_.begin(), pairs_.begin() + std::ptrdiff_t(o.ownTsumo ? shownPairs[0] : shownPairs[size_t(me)]));
        if (o.ownTsumo) o.tsumo1.pairs.assign(pairs_.begin(), pairs_.begin() + std::ptrdiff_t(shownPairs[1]));
        for (size_t i = 0; i < 2; ++i) {
            const PrimarySeat14& seat = *seats[i];
            const OwnedPublicPose14& p = seat.pose;
            const int64_t at = stale[i] ? tick : int64_t(p.count108);
            // A stale seat, a different frame or an ongoing fall cannot supply the order for this forecast.
            auto& order = o.nextGarbageOrder[i];
            order.reset();
            if (!stale[i] && at == tick && playing(p) && p.phase != 10 && seat.garbageRandom && seat.garbageCursor)
                order = primaryGarbageOrder14(*seat.garbageRandom, *seat.garbageCursor);
            juraic::Player q;
            hidden_[i] = fallen[i], q.field = fields[i];
            coloursOf(seat.field, colours_);
            q.incoming = waiting(int(i), notice[i], seat, *seats[1 - i], bonus_[1 - i]);
            q.moves = int(p.lockedPairs);
            if (seat.score) {
                q.scoring.score = seat.score->total;
                q.scoring.pool = seat.score->pool;
            }
            q.scoring.allClear = allClear_[i];
            if (p.operating && !shown_[i]) {
                q.phase = juraic::Phase::Control;
                q.nextPair = int(p.lockedPairs) + 1;
                // (a pair whose place was not read in this frame: where it was last read, its colours as the window showed them)
                q.colors = seat.placed ? juraic::Pair{juraic::Cell(p.currentColors[0]), juraic::Cell(p.currentColors[1])}
                                       : known(int(p.lockedPairs)) ? pairs_[size_t(p.lockedPairs)] : juraic::Pair{juraic::Cell(1), juraic::Cell(1)};
                q.pair = hand_[i];
                // (the other seat's pair: the frames it has lain, as the engine counts them; the own seat's timers come
                //  from its own buttons)
                if (int(i) != me) {
                    q.pair.grounded = ground_[i] >= 0;
                    q.pair.groundFrames = uint8_t(ground_[i] >= 0 ? std::min<int64_t>(at - ground_[i] + 1, 255) : 0);
                    q.pair.owned = lay_[i] >= 0;
                    q.pair.ownFrames = uint8_t(lay_[i] >= 0 ? std::min<int64_t>(at - lay_[i] + 1, 255) : 0);
                    q.pair.restless = restless_[i];
                }
            } else if (const int64_t end = int(i) != me && !p.operating ? chainEnd(i) : -1; end > at) {
                // The other seat's chain, with the frames it still runs: the garbage it makes is sure when it ends and
                // falls after that, not before. (The board holds no group that clears: its chain is played already.)
                // (With the number of its waves: the AI's reading makes the garbage of a chain sure when the chain ends,
                //  and a chain of no wave is none to it - juraic::chainEnd.)
                q.phase = juraic::Phase::Land;
                q.nextPair = int(p.lockedPairs);
                q.wait = int(end - at);
                q.chain.chains = std::max(1, running_[i].waves);
            } else {
                q.phase = juraic::Phase::Next;
                q.nextPair = int(p.lockedPairs);
                // The frames to the frame the pair first moves in (1: the next one).
                int64_t left;
                bool exact = false; // (the frames are those of a chain the model plays: not cut short)
                if (p.operating) left = 1; // (shown: it moves in the next frame)
                else if (!p.lockedPairs && at < firstTick) left = firstTick + 1 - at; // (the first pair of the round)
                else if (p.fieldActive) {
                    // (busy: no sooner than that - nor than the landing of the pair just put is over, nor than the
                    //  seat's chain is, where the model plays it; a chain is told by the garbage it makes as well as by
                    //  the field's phase, which is neither 1 nor 2 in its first frames)
                    const bool chain = chaining(i, seat);
                    left = quietFrames + 1 + (chain ? CHAIN_MORE : 0);
                    if (due_[i] >= 0 && due_[i] + 1 - at > left) left = due_[i] + 1 - at;
                    // (a chain the model does not play: the wave the field last began to clear takes a wave's frames at least)
                    if (chain && !running_[i].waves && cleared_[i] >= 0)
                        left = std::max<int64_t>(left, cleared_[i] + timing.waveFrames(1) - at + timing.nextFrames);
                    if (const int64_t over = chainEnd(i); over > at && over - at + timing.nextFrames > left) left = over - at + timing.nextFrames, exact = true;
                }
                else if (busy_[i] >= 0) left = busy_[i] + quietFrames + 1 - at;                                    // (quiet again: known)
                else left = due_[i] >= 0 && due_[i] + 1 - at > quietFrames + 3 ? due_[i] + 1 - at : quietFrames + 3; // (not busy yet)
                const int64_t most = exact ? 6000 : 200;
                q.wait = int(left < 2 && !p.operating ? 2 : left > most ? most : left);
                // The own seat with its field still busy - a pair landing, a chain, garbage on its way down: the AI's
                // reading ends that first (Observation::busyLeft), so that the garbage sure above the seat falls before
                // its next pair and what its chain sent is sure when the chain ends, not before.
                if (int(i) == me && !p.operating && p.lockedPairs && (p.fieldActive || (due_[i] >= 0 && due_[i] + 1 - at > timing.nextFrames))) {
                    o.busyLeft[i] = std::max(1, q.wait - timing.nextFrames);
                    o.busyChain[i] = (p.fieldActive && chaining(i, seat)) || chainEnd(i) > at;
                }
                // The other seat's chain that the model does not play to the board the game has: a chain all the same,
                // over no sooner than the field is quiet (the pair comes Timing::nextFrames after its end).
                if (int(i) != me && !p.operating && p.fieldActive && chaining(i, seat)) {
                    q.phase = juraic::Phase::Land;
                    q.wait = std::max(1, q.wait - timing.nextFrames);
                    q.chain.chains = 1;
                } else if (int(i) != me && !p.operating && p.lockedPairs && due_[i] >= 0 && due_[i] + 1 - at > timing.nextFrames) {
                    // The other seat's pair landing, without a chain: shown landing, as the game has it - the garbage
                    // that waits above the seat falls when the landing is over, before its next pair, and the AI's
                    // reading lets it fall only after a landing.
                    q.phase = juraic::Phase::Land;
                    q.wait = int(due_[i] + 1 - at - timing.nextFrames);
                }
            }
            o.player[i] = q;
        }
        // What is left of a running chain takes from the garbage above the chain's own field first (waiting): the seat
        // is shown without that garbage - the board shown is the chain's end, and the AI's reading plays no wave on it.
        for (size_t i = 0; i < 2; ++i) {
            juraic::Incoming& in = o.player[i].incoming;
            const int take = std::min(rest_[1 - i], in.total()), pending = std::min(take, in.pending);
            in.pending -= pending, in.confirmed -= take - pending;
        }
        // Garbage that waits for a chain to end is sure once the seat that sent it is not shown in a chain: the AI's
        // reading makes it sure at the end of a chain alone, and would never let it fall.
        for (size_t i = 0; i < 2; ++i)
            if ((o.player[i].phase != juraic::Phase::Land || o.player[i].chain.chains == 0) && !o.busyChain[i]) o.player[1 - i].incoming.confirm();
        // (the colours in play: those of the pairs the windows have shown and of the pieces on both fields)
        coloursInPlay(colours_, o.rules);
        return true;
    }
    // In the last sample observed, the seat's pair (if one is in its hands) was read where it is.
    bool placed(int seat) const { return placed_[size_t(seat)]; }
    // The seat's pair is in the player's hands and not moved yet (the frame before its first): shown to the AI as about to appear.
    bool shownOnly(int seat) const { return shown_[size_t(seat)]; }
    // The rounds begun so far: one more as a new round (a counter or a count of pairs put that went back, another
    // binding) is first observed with something to show.
    uint64_t round() const { return round_; }
    // A new round has been told and not yet observed with something to show: round() is still that of the round before.
    bool roundComing() const { return fresh_; }

private:
    static constexpr int CHAIN_MORE = 20; // a field busy with a chain: this much later at least (a wave takes far longer)
    static constexpr uint32_t LAST_BUSY = 8; // what a field is busy with in its last busy frame, when no garbage falls after it
    static constexpr int64_t STALE = 120;    // a seat not read is taken as it was last read for so many frames
    std::array<bool, 2> busyLast_{};      // the last busy frame seen was marked so
    std::array<int64_t, 2> seen_{-1, -1}; // the counter of the last sample of the seat
    std::array<std::optional<PrimarySeat14>, 2> kept_; // each seat as last read
    std::array<int64_t, 2> keptAt_{-1, -1};            // ... and the counter then
    std::array<bool, 2> placed_{true, true};           // the pair in the seat's hands was read where it is (true with none)
    std::array<juraic::PairControl, 2> hand_{};        // where the pair in the seat's hands was last read
    bool begun_ = false, timed_ = false;
    uint64_t generation_ = 0;
    int64_t start_ = 0, tick_ = -1;      // the game's counter in the frame 0 of the round; the counter of the last sample
    std::array<uint32_t, 2> put_{};      // pairs put, as last read
    std::array<bool, 2> operated_{};     // a pair was in the seat's hands in the last sample
    std::array<bool, 2> shown_{};        // ... in its first frame: shown, not moved yet
    std::array<bool, 2> slid_{};         // the seat's window has been seen to slide since its last pair was put
    // The counter in the first frame of the clock of the landing animation of the pair in play (PairControl::owned,
    // ownFrames; -1: none), and in the frame it last landed (PairControl::grounded, groundFrames; -1: not lying).
    std::array<int64_t, 2> lay_{-1, -1}, ground_{-1, -1};
    std::array<bool, 2> restless_{}; // ... kicked up from the floor as it lay, and neither turned nor moved since (Timing::liftEndsRest)
    std::array<int64_t, 2> cleared_{-1, -1}; // the counter of the frame the seat's field last began to clear since its last pair was put
    uint64_t round_ = 0; // the rounds begun (round())
    bool fresh_ = false; // a new round told and not yet observed with something to show (roundComing())
    std::array<int64_t, 2> busy_{-1, -1}; // the counter of the last busy frame since the last pair was put
    std::array<int64_t, 2> due_{-1, -1};  // the counter of the frame the next pair is in the hands, if nothing else happens
    std::array<FallColumns, 2> fallColumns_{};
    std::array<Hidden, 2> hidden_{};      // garbage that has fallen and is not in the board, as the last sample had it
    std::array<int, 2> chainBase_{-1, -1}; // the notice of the seat as it stood when the other seat's chain began (-1: no chain)
    // With the game's own account of the garbage, for the seat that receives: what the other seat's chain has sent it
    // so far (-1: no chain); the garbage that stood above the chain's own field when it began and what has come there
    // since; the garbage last seen on its way to the seat and to the chain's field; the garbage of that chain in all.
    struct Flight {
        int flying = 0, arrived = 0;
        int sentSince(const Flight& before) const {
            // The last wave may have arrived and the next one launched between reads. Otherwise an increase
            // adds only the new pieces, and a decrease is cancellation of pieces already counted as sent.
            if (arrived - before.arrived >= before.flying) return flying;
            return std::max(0, flying - before.flying);
        }
    };
    std::array<int, 2> made_{-1, -1}, stood_{}, came_{};
    std::array<Flight, 2> flying_{}, hisFlying_{};
    std::array<uint32_t, 2> chainOf_{};
    // Without that account, for the seat that receives: what has come to its notice since the other seat's chain began,
    // and its notice in the last sample; the garbage above the chain's own field when it began, what has come there
    // since, and what was there in the last sample.
    std::array<int, 2> arrived_{}, noticeWas_{}, hisStood_{}, hisCame_{}, hisWas_{};
    // The chain a seat's pair has begun, as the model plays it. The game writes the end of a chain into the board in the
    // frame the pair is put, and counts the pieces down wave by wave (records of the real game: a wave of a chain of
    // fourteen every 80 to 88 frames, Timing::waveFrames; the first one Timing::landFrames after the pair is put; the
    // field's phase 1 while a wave clears, 2 while its pieces fall).
    struct Running {
        int waves = 0;                // (0: no chain, or not one the model plays to the board the game has)
        std::array<int, 32> frames{}; // the frames of each wave
        std::array<int, 32> gone{};   // the pieces gone from the field once the wave has cleared, from the first wave on
        int base = -1;                // the pieces the game counted in the field in the frame the pair was put (-1: not read)
        int wave = 0, entered = 0;    // the wave that runs, and how often the field was seen to begin clearing
        int64_t waveAt = 0;           // the counter of the frame that wave began in
        int split = -1;               // the rows a piece of the pair fell apart from it as it was put (-1: not known)
        bool splitTop = false;        // ... with its axis in the 13th row or above
    };
    std::array<Running, 2> running_{};
    std::array<juraic::GameField, 2> before_{}; // the board as it was while the seat's pair was in its hands
    std::array<juraic::Pair, 2> beforePair_{};  // ... and that pair
    std::array<bool, 2> beforeKnown_{};
    std::array<bool, 2> allClear_{};            // the all-clear bonus waits for the seat's next chain
    std::array<int, 2> bonus_{};                // the garbage of the bonus the seat's running chain takes (0: none)
    std::array<bool, 2> bonusRead_{};           // the bonus was read in the last sample of the seat
    std::array<int, 2> rest_{};                 // what the chain of the other seat still makes, by the seat that receives (waiting)
    std::array<int, 2> beforePieces_{-1, -1};   // the pieces of `before_` (-1: no board of the pair in the hands)
    static int piecesIn(const juraic::GameField& field) {
        int n = 0;
        for (int x = 0; x < juraic::W; ++x) {
            for (int y = 0; y < 13; ++y) n += field.f.get(x, y) != juraic::EMPTY;
            n += field.row14[size_t(x)] != juraic::EMPTY;
        }
        return n;
    }
    std::array<uint32_t, 2> phaseWas_{};        // the field's phase in the last sample of the seat
    // The chain of the pair put on `before` that leaves the board `after`: the placement is the one whose chain, played
    // by the model, ends in that board (waves 0: none does) - the first such; with the rows a piece of the pair fell apart
    // as it was put, not known when two that do fall apart otherwise.
    static Running chainOf(const juraic::GameField& before, juraic::Pair pair, const juraic::GameField& after, const juraic::Rules& rules, const juraic::Timing& timing) {
        const unsigned legal = juraic::legalPlacements(before);
        Running found;
        // (top: garbage in the 13th row goes with the piece under it when that clears - the game's board after one
        //  recorded chain of fifteen is the model's only so; the model of a wave leaves the 13th row alone)
        for (int top = 0; top < 2; ++top)
        for (int id = 0; id < juraic::PLACEMENTS; ++id) {
            if (!(legal >> id & 1)) continue;
            juraic::GameField g = before;
            const juraic::PlaceResult placed = juraic::place(g, juraic::placement(id), pair);
            Running r;
            juraic::Wave w;
            int gone = 0;
            while (r.waves < int(r.frames.size()) && juraic::findWaveFor(g.f, w, rules.clearCount)) {
                juraic::FieldBits erased = w.cleared | w.garbage;
                if (top) erased = erased | (w.cleared.up() & g.f.colorMask(juraic::GARBAGE));
                r.frames[size_t(r.waves)] = timing.waveFrames(juraic::longestFall(g.f, erased));
                gone += erased.count();
                r.gone[size_t(r.waves++)] = gone;
                g.f.erase(erased);
                if (!g.f.drop()) break;
            }
            if (!r.waves || !(g == after)) continue;
            r.split = placed.split, r.splitTop = placed.lockY >= 12;
            if (!found.waves) found = r;
            else if (found.split != r.split || found.splitTop != r.splitTop) found.split = -1;
        }
        return found;
    }
    // The seat's field is busy with a chain: by its phase, by the garbage the chain makes, or by the bonus it takes.
    bool chaining(size_t i, const PrimarySeat14& seat) const {
        const OwnedPublicPose14& p = seat.pose;
        return p.phase == 1 || p.phase == 2 || seat.chainGarbage > 0 || bonus_[i] > 0;
    }
    // The frames the pair in the seat's hands has lain, as the engine counts them (lay_, ground_, restless_), from where
    // it is read now (hand_) and where it was read before (`was`; `same`: of the same pair), on `field`.
    void lie(size_t i, int64_t at, const juraic::GameField& field, const juraic::PairControl& was, bool same) {
        const juraic::detail::Cells cells(field);
        const juraic::PairControl& h = hand_[i];
        const bool grounded = ground_[i] >= 0;
        // (lifted: by a floor kick or a quick turn, after which it lands anew; turned: by a turn that is no quick turn)
        const bool lifted = same && 2 * h.y + int(h.upper) > 2 * was.y + int(was.upper);
        const bool quick = same && ((h.dir == juraic::UP && was.dir == juraic::DOWN) || (h.dir == juraic::DOWN && was.dir == juraic::UP));
        const bool turned = same && h.dir != was.dir && !quick;
        const bool kicked = lifted && turned && h.dir == juraic::DOWN;
        if (cells.canStepDown(h)) ground_[i] = -1;
        else if (ground_[i] < 0 || lifted) ground_[i] = at;
        // (Timing::liftEndsRest: kicked up from the floor as it lay, no clock until the pair is turned or moved)
        if (kicked && grounded && timing.liftEndsRest) restless_[i] = true;
        else if (turned || (same && h.x != was.x)) restless_[i] = false;
        // (lying with its axis's own column under it; Timing::restBoth: a pair on its side, both of its pieces)
        const auto own = [&](const juraic::PairControl& q) {
            if (timing.restBoth && (q.dir == juraic::LEFT || q.dir == juraic::RIGHT)) return !cells.free(q.x, q.y - 1) && !cells.free(q.childX(), q.y - 1);
            return q.dir == juraic::DOWN ? !cells.free(q.x, q.y - 2) : !cells.free(q.x, q.y - 1);
        };
        const bool lies = !cells.canStepDown(h) && own(h);
        bool within = lies; // (lying so, or half a step above where it would)
        if (!lies && h.upper) {
            juraic::PairControl q = h;
            q.upper = false;
            within = !cells.canStepDown(q) && own(q);
        }
        if (restless_[i]) lay_[i] = -1;
        else if (turned && timing.turnDropsRest) lay_[i] = lies ? at : -1; // (the clock dropped by the turn, begun anew)
        else if (lay_[i] < 0) lay_[i] = lies ? at : -1;
        else if (!within) lay_[i] = -1;
    }
    // The counter of the frame after the last of the chain the seat's field is busy with (-1: none known).
    int64_t chainEnd(size_t i) const {
        const Running& r = running_[i];
        if (!r.waves || r.wave >= r.waves) return -1;
        int64_t end = r.waveAt;
        for (int k = r.wave; k < r.waves; ++k) end += r.frames[size_t(k)];
        return end;
    }
    std::vector<juraic::Pair> pairs_;    // the pairs shown so far, by their number (both seats draw the same)
    std::vector<bool> learned_;          // ... which of them a window has shown (the others stand for pairs never seen)
    std::array<bool, juraic::MAX_COLORS + 1> colours_{}; // the colours the round has shown so far, by their cell code (coloursInPlay)
    bool known(int index) const { return index >= 0 && size_t(index) < learned_.size() && learned_[size_t(index)]; }

    static juraic::PairControl control(const OwnedPublicPose14& p) {
        juraic::PairControl c;
        c.x = int8_t(p.x), c.y = int8_t(p.y), c.upper = p.upper, c.dir = uint8_t(p.dir);
        return c;
    }
    // (a pair before the first one seen - a round joined on its way - is not known: it has been put long ago, and
    //  stands as a pair of the first colour, as the pictures' reader has it)
    void learn(int index, juraic::Pair pair) {
        if (index < 0) return;
        if (int(pairs_.size()) <= index) pairs_.resize(size_t(index) + 1, juraic::Pair{juraic::Cell(1), juraic::Cell(1)}), learned_.resize(size_t(index) + 1, false);
        pairs_[size_t(index)] = pair, learned_[size_t(index)] = true;
        for (const juraic::Cell c : {pair.axis, pair.child})
            if (int(c) >= 1 && int(c) <= juraic::MAX_COLORS) colours_[size_t(c)] = true;
    }
};

// What became of buttons sent to the pad (PrimaryDrive14::press): on the pad; held back (the pad is not the play's for
// now - a press asked for by hand, the driver of the pictures, a seat not to be played); not sent (the pad failed).
enum class PressResult14 : uint8_t { Applied, HeldBack, Failed };
// The buttons of the play known to be on the pad: buttons asked for are sent while they are not known there, and only
// those that reached it are known there.
struct PadButtons14 {
    uint8_t onPad = 0;  // the buttons on the pad (known)
    bool known = false; // the pad has the buttons last sent to it by the play
    bool loose = false; // buttons of the play may be on the pad: some were sent, and no release has reached it since
    bool due(uint8_t asked) const { return !known || asked != onPad; }
    // After `asked` was sent: true when it is on the pad; false otherwise - sent again with the next sample, and the
    // frame it was for went by without it (PrimaryPlayer14::undelivered).
    bool took(uint8_t asked, PressResult14 result) {
        if (asked) loose = true;
        if (result != PressResult14::Applied) return known = false;
        onPad = asked, known = true;
        if (!asked) loose = false;
        return true;
    }
    // The seats are not read: nothing of the play is to be held, and a release is sent until one has reached the pad
    // (one held back or not sent is sent again the next time).
    bool releaseDue() const { return loose; }
    // The pad was set by someone else meanwhile (a press asked for by hand, the driver of the pictures, a seat not to be
    // played): the buttons of the next sample are sent whatever was sent before.
    void lent() { known = false; }
};
// Buttons of the play for a pad that others set too: held back while the pad is not the play's (`heldBack`: a press
// asked for by hand, the driver of the pictures, a seat not to be played), otherwise sent by `send` (true: the pad took them).
template <class Send>
PressResult14 pressShared14(bool heldBack, Send&& send) {
    if (heldBack) return PressResult14::HeldBack;
    return send() ? PressResult14::Applied : PressResult14::Failed;
}
// The seat played by the memory first is read now: a sample that read it came at most PRIMARY_SEAT_FRESH_US14 before
// `nowUs` (both of the reader's clock; 0: none came). A reader may stay bound while it reads nothing for seconds - until
// it binds anew (PrimaryRebind14::goneUs) -, so being bound alone does not tell it.
constexpr uint64_t PRIMARY_SEAT_FRESH_US14 = 100000;
inline bool primarySeatFresh14(uint64_t seatReadUs, uint64_t nowUs) {
    return seatReadUs != 0 && (nowUs <= seatReadUs || nowUs - seatReadUs <= PRIMARY_SEAT_FRESH_US14);
}

// The AI of one seat played on the samples: a call for every sample, buttons for the frame of the game's counter that
// the sample is of. The same buttons again for a sample of the same frame.
class PrimaryPlayer14 {
public:
    PrimaryObserver14 observer;
    // inputDelay: the frames a button takes to reach the game (0: the game takes it in its next frame).
    PrimaryPlayer14(juraic::ai::Agent& agent, int me, int inputDelay) : agent_(agent), input_(agent, inputDelay), me_(me) {
        input_.pairOnly = input_.downInHand = true;
        if (const auto* placing = dynamic_cast<const juraic::ai::PlacementAgent*>(&agent)) fastest_ = placing->fastestWays;
    }
    // The frames a button took to reach the game, as the pair showed it: a tap of a direction or a turn sent on the
    // sample of the counter t moves the pair in the sample t + 1 with no delay (measured: 0), in t + 1 + d with d frames
    // of delay (a match over a network). -1: none measured yet. measuredRun: taps in a row that gave that number.
    int measured() const { return measured_; }
    int measuredRun() const { return measuredRun_; }
    uint64_t seed = 1; // of the agent's next round (Agent::startRound is called as a round is first seen; one more for each)
    long rounds = 0;   // rounds begun so
    // Called as a round begins, after the agent was told (its searches of the round before have ended by then).
    std::function<void()> onRound;
    const juraic::ai::Observation& shown() const { return shown_; }           // what the AI was shown last
    const juraic::ai::Observation& foreseen() const { return input_.predicted; } // ... and where it reckoned its buttons take hold
    bool foreseenWhole() const { return input_.predictionComplete; }             // (false: a pair to come is not known yet)
    int delay() const { return input_.delay(); }
    void setDelay(int frames) {
        input_.setDelay(frames);
        pose_.reset();
        resetMeasure();
        held_ = 0;
    }
    uint8_t plannedAt(int64_t frame) const { return input_.plannedAt(frame); } // the buttons reckoned to reach the game in that frame
    long frames = 0;    // frames of the counter played on
    long skipped = 0;   // ... of them, frames no sample came for
    long filled = 0;    // ... of those, frames played on as the engine has them (the others: begun anew)
    long recovered = 0; // pairs found in the air with no placement, taken up from where they were
    long corrections() const { return input_.corrections; } // frames the pair was not where the buttons sent should have it
    const std::optional<juraic::Player>& expected() const { return input_.expected(); } // the own side as reckoned after the frame the last buttons are for
    long repaired() const { return pose_.repaired; }
    // The buttons of the last frame were set too late to reach the game in it (PredictiveInput::undelivered); a tap
    // measured from this frame is not measured.
    void undelivered() {
        input_.undelivered();
        if (tapAt_ == last_) tapAt_ = -1;
    }

    static constexpr int FILL = 6; // so many frames without a sample are played on as the engine has them

    uint8_t frame(const PrimarySample14& s, const std::array<int, 2>& notice) {
        if (s.generation != generation_) {
            generation_ = s.generation;
            observer.reset();
            input_.reset();
            pose_.reset();
            resetMeasure();
            seen_ = {}, shown_ = {};
            last_ = otherAt_ = -1;
            aimless_ = 0;
            recoveredPair_ = endedSeat_ = -1;
            held_ = 0;
            playing_ = false;
        }
        const auto& own = s.seat[size_t(me_)];
        if (roundEnded(s) || (own && own->pose.cpuControlled)) {
            if (playing_) {
                observer.reset();
                input_.reset();
                pose_.reset();
                resetMeasure();
                aimless_ = 0;
                recoveredPair_ = -1;
                last_ = -1;
                playing_ = false;
            }
            return held_ = 0;
        }
        const int64_t tick = PrimaryObserver14::tickOf(s, me_);
        if (tick < 0) {
            // (the seat was not read in this sample: the buttons stay - for up to FILL frames of the other seat's counter
            //  since the own seat was last read, which are played on as the engine has them; after that nothing is pressed.
            //  The other seat not read with it then: counted from the frame before the first sample that reads it.)
            const int64_t other = PrimaryObserver14::tickOf(s, 1 - me_);
            if (otherAt_ < 0 && other >= 0) otherAt_ = other - 1;
            if (otherAt_ >= 0 && other >= 0 && other - otherAt_ > FILL) held_ = 0;
            return held_;
        }
        otherAt_ = PrimaryObserver14::tickOf(s, 1 - me_);
        if (playing_ && tick == last_) return held_;
        // (a tap being measured is not carried across a new round or a frame not seen)
        if (last_ >= 0 && tick != last_ + 1) resetMeasure();
        if (!observer.observe(s, me_, notice, seen_)) {
            // (a sample not to be shown within the round played on - the numbers of a seat out of place for a frame:
            //  taken as a frame no sample came for, the buttons staying, for up to FILL frames)
            if (playing_ && !observer.roundComing() && tick > last_ && tick - last_ <= FILL) return held_;
            // (no round: nothing is pressed, and what was planned is forgotten)
            input_.reset(), pose_.reset(), last_ = tick, playing_ = false, aimless_ = 0, recoveredPair_ = -1;
            resetMeasure();
            return held_ = 0;
        }
        // A new round (PrimaryObserver14::round): the agent is told, and what was planned is forgotten.
        if (observer.round() != round_) {
            round_ = observer.round();
            input_.reset(), pose_.reset(), playing_ = false, aimless_ = 0, recoveredPair_ = -1;
            resetMeasure();
            agent_.startRound(me_, seed++), ++rounds;
            if (onRound) onRound();
        }
        // The ways over soonest slide the pair along the ground it comes down on: only on a field that is known. With
        // garbage on it whose columns are not all known the pair is taken the straight way (moved in the air, then down).
        {
            const bool unsure = observer.hidden(me_).pieces > 0 && !observer.hidden(me_).sure;
            if (auto* placing = dynamic_cast<juraic::ai::PlacementAgent*>(&agent_)) placing->fastestWays = fastest_ && !unsure;
        }
        const int64_t gap = playing_ && tick > last_ ? tick - last_ - 1 : 0;
        if (gap > 0) {
            skipped += long(gap), frames += long(gap);
            // Frames no sample came for: the AI is shown them as the engine has them from the last one it saw (its own
            // side played on with the buttons sent), so that it misses none - not the frame before its pair appears.
            // No buttons were sent in them: those of the frame before stayed (PredictiveInput::stay).
            if (gap <= FILL && seen_.frame == shown_.frame + gap + 1)
                for (int64_t k = 0; k < gap; ++k) step(between(), true, true), ++filled;
            else
                input_.reset(), pose_.reset(), aimless_ = 0;
        } else if (playing_ && tick < last_) {
            input_.reset(), pose_.reset(), aimless_ = 0;
        }
        last_ = tick, playing_ = true, ++frames;
        const uint8_t before = held_;
        held_ = step(seen_, false, observer.placed(me_));
        measure(tick, uint8_t(held_ & ~before));
        return held_;
    }

private:
    juraic::ai::Agent& agent_;
    PredictiveInput input_;
    PoseInputModel pose_;
    juraic::ai::Observation seen_, shown_;
    int me_ = 0;
    int64_t last_ = -1;
    uint64_t generation_ = 0;
    bool playing_ = false;
    int endedSeat_ = -1;
    uint64_t endedGeneration_ = 0;
    uint32_t endedTick_ = 0, endedPairs_ = 0;

    // 実機の敗北表示に入った席では盤面の状態が9、プレイヤーの状態が1になる。
    // 相手の組がまだ動いていても入力を離す。次の回戦を確認するまで判断を再開しない。
    bool roundEnded(const PrimarySample14& sample) {
        for (int seat = 0; seat < 2; ++seat) {
            if (!sample.seat[size_t(seat)]) continue;
            const auto& pose = sample.seat[size_t(seat)]->pose;
            if (pose.fieldState == 9 && pose.playerState == 1) {
                endedSeat_ = seat, endedGeneration_ = sample.generation;
                endedTick_ = pose.count108, endedPairs_ = pose.lockedPairs;
                return true;
            }
        }
        if (endedSeat_ < 0) return false;
        const auto& ended = sample.seat[size_t(endedSeat_)];
        if (sample.generation == endedGeneration_ &&
            (!ended || ended->pose.count108 >= endedTick_ || ended->pose.lockedPairs > endedPairs_)) return true;
        endedSeat_ = -1;
        return false;
    }
    uint64_t round_ = 0; // the observer's round the agent was last told of (PrimaryObserver14::round)
    int aimless_ = 0, recoveredPair_ = -1;
    uint8_t held_ = 0;
    int64_t otherAt_ = -1; // the other seat's counter in the last sample the own seat was read in (-1: not read then)
    bool fastest_ = false; // the agent plans the ways over soonest (as it was set when the player was made)
    int measured_ = -1, measuredRun_ = 0;
    int64_t tapAt_ = -1; // the counter of the sample a tap being measured was sent on (-1: none)
    int64_t lastTap_ = -1; // ... and of the sample the last tap of any kind was sent on
    int tapPair_ = -1, tapX_ = 0, tapDir_ = 0;

    // The tap being measured has moved the pair; or a tap begins to be measured (`pressed`: the buttons newly pressed on
    // this sample). One at a time: the first change of the pair's column or direction after a tap is that tap's. A tap
    // that changes nothing (against a wall) is dropped after TAP_WAIT frames, and with its pair.
    static constexpr int TAP_WAIT = 12;
    void resetMeasure() { tapAt_ = lastTap_ = -1; measured_ = -1; measuredRun_ = 0; }
    void measure(int64_t tick, uint8_t pressed) {
        const juraic::Player& mine = seen_.self();
        const bool inHand = mine.phase == juraic::Phase::Control && observer.placed(me_);
        // (a pair in its first frame, shown and not moved yet: where a pair appears, as it appears)
        const bool shown = observer.shownOnly(me_);
        const juraic::PairControl born;
        const int pair = mine.moves, x = shown ? born.x : mine.pair.x, dir = shown ? born.dir : mine.pair.dir;
        if (tapAt_ >= 0) {
            if (!(inHand || shown) || pair != tapPair_ || tick - tapAt_ > TAP_WAIT) {
                tapAt_ = -1;
            } else if (x != tapX_ || dir != tapDir_) {
                const int d = int(tick - tapAt_) - 1;
                if (d >= 0) measuredRun_ = d == measured_ ? measuredRun_ + 1 : 1, measured_ = d;
                tapAt_ = -1;
            }
        }
        if (pressed & (juraic::BTN_LEFT | juraic::BTN_RIGHT | juraic::BTN_CW | juraic::BTN_CCW)) {
            // (a first press that does not move the pair - a quick turn waited for, a wall - is not measured, so that a
            //  move made by a later press is not taken for its answer; a lone press that can move the pair is measured
            //  by its first move even when more presses follow)
            if (tapAt_ < 0 && (inHand || shown) && (lastTap_ < 0 || tick - lastTap_ > TAP_WAIT)) {
                juraic::PairControl probe = shown ? born : mine.pair;
                probe.held = probe.queued = probe.quickWindow = probe.quickLock = 0;
                const int beforeX = probe.x, beforeDir = probe.dir;
                juraic::stepPair(probe, mine.field, uint8_t(pressed & (juraic::BTN_LEFT | juraic::BTN_RIGHT | juraic::BTN_CW | juraic::BTN_CCW)), observer.timing);
                if (probe.x != beforeX || probe.dir != beforeDir)
                    tapAt_ = tick, tapPair_ = pair, tapX_ = x, tapDir_ = dir;
            }
            lastTap_ = tick;
        }
    }

    // One frame of the AI on what it is shown (stayed: a frame made up afterwards, in which the buttons stayed).
    // placed false: where the pair in the hands is was not read in this frame - it is where the buttons sent brought it.
    uint8_t step(const juraic::ai::Observation& o, bool stayed, bool placed) {
        shown_ = pose_.observe(o, input_.plannedAt(o.frame - 1), placed);
        const uint8_t buttons = stayed ? input_.stay(shown_) : input_.tick(shown_);
        // A pair in the air that the AI has no placement for (it never saw the frame before the pair appeared: a round
        // joined on its way, the first pair of a round with buttons that reach the game late): taken up where it is.
        auto* placing = dynamic_cast<juraic::ai::PlacementAgent*>(&agent_);
        const juraic::Player& mine = input_.predicted.self();
        if (placing && input_.predictionComplete && mine.phase == juraic::Phase::Control && placing->targetPlacement() < 0) {
            if (++aimless_ >= 2 && recoveredPair_ != mine.nextPair && placing->recoveryReady()) {
                recoveredPair_ = mine.nextPair, ++recovered;
                placing->recoverKnownPair(input_.predicted, -1);
            }
        } else {
            aimless_ = 0;
        }
        return buttons;
    }
    // The frame after the one shown last, as the engine has it: the own side played on with the buttons sent for it.
    juraic::ai::Observation between() const {
        juraic::ai::Observation o = shown_;
        for (int& busy : o.busyLeft) busy = busy > 1 ? busy - 1 : 0; // (a frame further on)
        for (int i = 0; i < o.players && i < 2; ++i) {
            // (the other seat a frame further on in what it is doing while no pair is in its hands: no fewer than a frame
            //  left, what follows is read)
            juraic::Player& q = o.player[i];
            if (i != o.me && q.phase != juraic::Phase::Control && q.phase != juraic::Phase::Decide && q.phase != juraic::Phase::Dead && q.wait > 1) --q.wait;
        }
        juraic::Player& mine = o.player[o.me];
        juraic::Tsumo known{};
        const auto& visible = o.mine();
        for (int i = std::max(0, visible.count() - juraic::TSUMO_PAIRS); i < visible.count(); ++i) {
            const auto pair = visible.pair(i);
            known.piece[size_t(i % juraic::TSUMO_PAIRS) * 2] = pair.axis, known.piece[size_t(i % juraic::TSUMO_PAIRS) * 2 + 1] = pair.child;
        }
        if (mine.phase == juraic::Phase::Control || (mine.phase == juraic::Phase::Next && (mine.wait > 1 || visible.known(mine.nextPair)))) {
            juraic::Incoming nobody;
            juraic::advancePlayer(mine, nobody, input_.plannedAt(o.frame), known, o.rules, o.timing, o.frame);
            // (what the engine makes of a pair put - its landing, a chain - is not what is read: the seat waits for its pair)
            if (mine.phase != juraic::Phase::Control && mine.phase != juraic::Phase::Next) mine.phase = juraic::Phase::Next, mine.wait = observer.quietFrames + 3;
        }
        ++o.frame;
        return o;
    }
};

// An agent as the play by the memory has it play the real game: buttons as the real game takes them, a second choice
// waited for in the air, and with `cancel` the ways over soonest.
inline void preparePrimaryAgent14(juraic::ai::Agent* agent, bool cancel) {
    if (auto* placing = dynamic_cast<juraic::ai::PlacementAgent*>(agent)) {
        placing->tapHold = 1, placing->holdSecond = true, placing->holdLevel = true, placing->waitQuickLock = true, placing->askInAir = true, placing->fastestWays = cancel;
        // The search for the way over soonest is made ahead, on a thread of its own, once the placement of the pair
        // to come is chosen (PlacementAgent::fastestAhead). One made in the frame the buttons are for - the pair
        // put right in the air, a placement chosen as the pair appears - is kept short: a search of 4000 states
        // took up to 19 ms on the fields of the records of the real game, and buttons set 12 ms after a frame did
        // not reach the game in it. A search that gives up leaves the straight way.
        placing->fastestStates = PRIMARY_FASTEST_NOW14, placing->fastestAhead = PRIMARY_FASTEST_AHEAD14;
        // The way made ahead is kept while a second choice is on its way and that way itself goes straight down
        // (records of the real game: a pair in five went to another placement in the air, and every pair held
        // straight down for a second choice had its way planned again within the frame - mostly the straight
        // one). A way planned in the air is searched once more as the one made ahead is, from where the pair is
        // a few frames on.
        placing->holdKeepsWay = cancel, placing->fastestAgain = cancel ? PRIMARY_FASTEST_AGAIN14 : 0;
        // (a choice dropped stops its searches; a first choice that comes late is asked for again in the air; the way
        //  over soonest is searched on while a second choice is awaited; with the pairs beyond the window read, the
        //  choice is made once more as the pair appears, by the fields as they are then)
        placing->cutStale = true, placing->retryInAir = true, placing->searchWhileSecond = cancel;
        placing->againFresh = primaryListedPairs14.load();
    }
}
// The player of a seat as the play by the memory makes it: its agent prepared for the real game (preparePrimaryAgent14),
// the rules and the frame counts of the real game - with the cancel where the seat takes the ways over soonest, so that
// the reading takes the other seat's pairs the soonest way too.
inline std::unique_ptr<PrimaryPlayer14> makePrimaryPlayer14(juraic::ai::Agent& agent, int me, int delay, bool cancel) {
    preparePrimaryAgent14(&agent, cancel);
    auto player = std::make_unique<PrimaryPlayer14>(agent, me, delay);
    player->observer.rules = primaryRules14(cancel), player->observer.timing = primaryTiming14(cancel);
    return player;
}

} // namespace play::realgame
