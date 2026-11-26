#pragma once
// Two players at the level of whole moves, in the real order of time.
//
// playTurn (game/turn.h) runs one move of one player to its end at once, so the
// opponent sees the garbage of a chain only when the chain is over. Here every
// wave of both players happens in its own frame, as in Round: garbage is
// offset wave by wave, the rate is the one of the frame of the wave, a chain
// started while the opponent's chain is running takes from garbage that is
// not confirmed yet, and garbage falls on a player whose move ends in the frame
// the opponent's chain ends or later.
//
// The frames of one move (Round::frame of the step in which it happens):
//   S   the pair appears (it can be moved in this frame)
//   L   the pair locks. Fastest input: S + placeFrames - (landFrames + nextFrames)
//   W1  L + splitFrames + landFrames: the first wave, or nothing clears
//   Wk+1 = Wk + waveFrames(longest fall of wave k)
//   D   the frame in which nothing clears any more (W1 without a chain): the
//       chain ends, the garbage it sent is confirmed, and at the end of this
//       frame, after both players have moved, the own confirmed garbage falls
//   S'  D + garbageFrames + nextFrames: the next pair
// Inside a frame: player 0, player 1, then the garbage of both (Round::step).
//
// Only frames in which something happens are played. tests/ai_tests.cpp plays
// the same moves here and in Round and compares.
//
// The fastest input of the Timing tables is the straight way: down held, the
// rotation at once, the direction every second frame. Past a column twelve
// high, beside one that stands in the way of the rotation, or to a placement
// whose axis piece comes to rest in the thirteenth row, there is no such way:
// the pair is lifted by a floor kick or goes round, and locks up to 14 frames
// later than the tables say (2 frames earlier in a few cases). Counted with
// a probe on the pairs of saved rounds: with no column higher than
// nine from the column the pair appears in to its placement, one column
// further on both sides included, the tables always hold; with one of ten,
// 85 of 1,039 placements in the twelfth row lock 2 frames later; from eleven
// on the other cases begin. With tallPaths the frame of the lock is then
// taken from the buttons InputPlanner finds.
#include "input.h"
#include "observe.h"

namespace juraic::ai {

constexpr uint64_t DUEL_GARBAGE_STREAM = 0x6475656C; // garbage columns inside the AI's reading

enum class DuelPhase : uint8_t {
    Spawn,   // the next pair appears in frame `at`
    Control, // a pair is in the player's hands: place() is awaited (no event)
    Check,   // in frame `at` the landing or a wave is over: the next wave, or the end of the chain
    Settle,  // at the end of frame `at` the confirmed garbage falls
    Dead,
};

struct DuelPlayer : PlayerCore {
    DuelPhase phase = DuelPhase::Spawn;
    int64_t at = 0;          // frame of the next event (Spawn, Check, Settle)
    int64_t spawnFrame = -1; // S of the pair in play; -1 = observed in mid-air
    int64_t lockFrame = -1;  // L of the last pair put
    int nextPair = 0;        // number of the pair that appears next
    int moves = 0;           // pairs locked so far
    ChainState chain;        // the chain in progress, or the last one
    int dropped = 0;         // garbage pieces taken by the last drop
    int fallen = 0;          // garbage pieces taken by all drops since the Duel was made
    int64_t settleFrame = -1; // the frame its last landing or chain was over in (what a chain sent is confirmed then); -1 = none in this Duel
};

// The spawn input on a fixed board depends on occupied cells, timing, speed
// and target. Colours affect the chain after lock, which is always simulated.
struct SpawnInput {
    int frames = 0, soft = 0, rested = 0;
};

inline SpawnInput heldPlacementInput(const GameField& field, PairControl pair, Placement target, const Timing& timing, int speed) {
    static thread_local InputPlanner planner;
    static thread_local InputPath path;
    if (!planner.plan(field, pair, target, timing, speed, path)) return {};
    for (const uint8_t b : path.buttons) stepPair(pair, field, b, timing, speed);
    return {path.frames(), path.softDropRows, restedFrames(pair, timing)};
}

inline SpawnInput tallSpawnInput(const GameField& field, Placement target, const Timing& timing, int speed) {
    return heldPlacementInput(field, spawnPair(timing), target, timing, speed);
}

// The way over soonest to `target` that a search finds (InputPlanner::planSure with fastest: FastestSearch, within
// FASTEST_READ_STATES states, and no more than `states` - the seat's own search within a frame - when that is above 0):
// the frames from `from` to the end of its landing as FastestSearch counts it (end; 0: none found sooner than the straight
// way, or no way), to its lock (lock), and its points of the down key (soft). The results of the last field, frame and pair
// are kept: the plans of a choice put the same pair on the same field again and again.
struct FastestWay {
    int end = 0, lock = 0, soft = 0;
};
constexpr int FASTEST_READ_STATES = 4000;
inline FastestWay fastestEnd(const GameField& field, const PairControl& from, int64_t fromFrame, Placement target, const Timing& timing,
                             int speed, int states = 0) {
    const int limit = states > 0 ? std::min(states, FASTEST_READ_STATES) : FASTEST_READ_STATES;
    struct Kept {
        GameField field;
        int64_t frame = -1;
        Timing timing{};
        int speed = -1, limit = -1;
        PairControl from{}; // (every field of the pair: where it is, how it moves, and the clocks of its landing)
        unsigned known = 0;
        FastestWay way[PLACEMENTS]{};
    };
    static thread_local Kept kept;
    if (kept.frame != fromFrame || kept.speed != speed || kept.limit != limit || !(kept.from == from) || !(kept.timing == timing) ||
        !(kept.field == field))
        kept.field = field, kept.frame = fromFrame, kept.timing = timing, kept.speed = speed, kept.limit = limit, kept.from = from, kept.known = 0;
    const int id = placementId(target);
    if (kept.known >> id & 1) return kept.way[id];
    kept.known |= 1u << id;
    FastestWay& way = kept.way[id] = {};
    static thread_local InputPlanner planner;
    static thread_local InputPath path;
    if (!planner.plan(field, from, target, timing, speed, path)) return way;
    const int now = FastestSearch::ends(field, from, path.buttons, timing, speed);
    static thread_local FastestSearch search;
    search.maxStates = limit, search.low = false, search.stop = nullptr;
    if (now <= 1 || search.estimate(field, from, target, timing, speed) >= now) return way;
    static thread_local std::vector<uint8_t> buttons;
    int soft = 0, found = 0;
    if (search.find(field, from, target, timing, speed, now - 1, buttons, soft, found)) way = {found, int(buttons.size()), soft};
    return way;
}

inline SpawnInput picturePlacementInput(InputPlanner& planner, const GameField& field, PairControl from,
                                         Placement target, const Timing& timing, int speed) {
    InputPath path;
    if (!planner.planSure(field, from, target, timing, speed, path) ||
        planner.lostBySkip(field, from, target, timing, speed, path)) return {};
    for (const auto button : path.buttons) stepPair(from, field, button, timing, speed);
    if (!from.locked || placementId(from.placement()) != placementId(target)) return {};
    return {path.frames(), path.softDropRows, restedFrames(from, timing)};
}

class SpawnInputCache {
public:
    explicit SpawnInputCache(InputPlanner* picture = nullptr) : picture(picture) {}
    void configure(const Timing& timing, int speed) {
        if (slots.empty()) slots.resize(512);
        else if (speed == speedPercent && timing == clock) return;
        clock = timing;
        speedPercent = speed;
        for (auto& slot : slots) slot.known = 0;
    }

    SpawnInput get(const GameField& field, Placement target) {
        const FieldBits occupied = field.f.occupied();
        unsigned ceiling = 0;
        for (int x = 0; x < W; ++x) ceiling |= unsigned(field.row14[x] != EMPTY) << x;
        uint64_t key = occupied.low() ^ std::rotl(occupied.high(), 25) ^ uint64_t(ceiling);
        Slot& slot = slots[splitmix64(key) % slots.size()];
        if (!slot.known || slot.occupied != occupied || slot.ceiling != ceiling) {
            slot.occupied = occupied;
            slot.ceiling = ceiling;
            slot.known = 0;
        }
        const int id = placementId(target);
        if (!(slot.known >> id & 1)) {
            slot.input[id] = picture ? picturePlacementInput(*picture, field, spawnPair(clock), target, clock, speedPercent)
                                     : tallSpawnInput(field, target, clock, speedPercent);
            slot.known |= 1u << id; // Includes unreachable routes, as in the ordinary table fallback.
        }
        return slot.input[id];
    }

private:
    struct Slot {
        FieldBits occupied{};
        unsigned ceiling = 0, known = 0;
        std::array<SpawnInput, PLACEMENTS> input{};
    };
    InputPlanner* picture;
    std::vector<Slot> slots;
    Timing clock{};
    int speedPercent = 0;
};

// Own button timing in a picture-driven tactical replay. Later pairs start
// without the currently held tap. The other player's timing stays independent.
struct PictureRoutes {
    InputPlanner first, later;
    SpawnInputCache next{&later};

    PictureRoutes(int hold, const int held[2], bool spare, bool quick, bool down) {
        for (auto* planner : {&first, &later}) {
            planner->tapHold = hold;
            planner->spareFrame = spare;
            planner->waitQuickLock = quick;
            planner->downWhenPlaced = down;
        }
        first.heldFor[0] = held[0], first.heldFor[1] = held[1];
    }
    PictureRoutes(const PictureRoutes&) = delete;
    PictureRoutes& operator=(const PictureRoutes&) = delete;
};

struct DuelExchangeTrace {
    struct Fire {
        int waves = 0, points = 0, sent = 0, offset = 0;
        int64_t ignition = -1, confirmed = -1;
    };
    int initialMoves[2]{};
    std::array<std::array<Fire, 4>, 2> turns{};

    Fire* get(int seat, int moves) {
        const int turn = moves - initialMoves[seat];
        return turn >= 0 && turn < int(turns[seat].size()) ? &turns[seat][turn] : nullptr;
    }
};

struct DuelDrop {
    int64_t frame = -1;
    int amount = 0;
    GameField field;
};

struct Duel {
    DuelExchangeTrace* exchangeTrace = nullptr;
    std::array<DuelDrop, 2>* firstDrops = nullptr;
    const Rules* rules = nullptr;   // of the Observation, which must outlive the Duel
    const Timing* timing = nullptr;
    int64_t frame = 0; // every frame before this one has been played (as Round::frame)
    RoundState state = RoundState::Playing;
    DuelPlayer player[2];
    // The columns of garbage that does not fill a row are random in the game;
    // the AI cannot know them, so its reading takes them from here.
    Rng garbageRng[2]{Rng(0), Rng(0)};
    bool fullGarbageOrder = false;
    std::array<std::optional<std::array<uint8_t, W>>, 2> nextGarbageOrder{};
    // Params::x3TallPaths. 0: placeFast takes the frame of the lock from the Timing tables, whatever
    // the field. n: from the buttons of InputPlanner when a column of n pieces or more stands between
    // the column the pair appears in and its placement (both pieces), or next to them.
    int tallPaths = 0;
    SpawnInputCache* spawnInputs = nullptr; // Borrowed for one Gaze search, including its copied duels.
    // Params::handWait of the player who reads, for his own seat: player i leaves a pair alone this many
    // frames before its first button, so placeFast locks it that many frames later. 0 = the fastest input.
    // (On the safe side: the pair falls a little while it waits and a wait may end early, so the real lock is
    // never later than this and a few frames earlier at most. The rows of soft drop are counted as with the
    // fastest input.)
    int wait[2] = {0, 0};
    // Observation::fastestRead: the seats whose pairs go the way over soonest, counted from the frame a pair appeared in
    // (or, for the pair the Observation saw in mid-air, from that frame and where it was then: inHand).
    // FASTEST_BOUND: every pair is checked no later than the end of the landing that FastestSearch::estimate counts on at
    // least. FASTEST_OWN: the next pair of the seat (the one in mid-air, or the first to appear: firstMoves) no later than
    // the end of the way a search finds (fastestEnd); the later ones as the straight way.
    uint8_t fastestSeat[2] = {0, 0};
    int firstMoves[2] = {0, 0}; // Player::moves when the Duel was made: the pair it locks next is the next one
    int fastestStates[2] = {0, 0}; // Observation::fastestReadStates
    struct InHand {
        bool known = false;
        PairControl pair;
        int64_t frame = 0;
        int wayTarget = -1;     // Observation::ownWayTarget
        int64_t wayCheck = -1;  // ... ownWayCheck
        int64_t wayLock = -1;   // ... ownWayLock
        int waySoft = 0;        // ... ownWaySoft
        bool appearing = false; // Observation::ownAir: the pair is shown as the one about to appear (the seat as Next)
    };
    InHand inHand[2];
    // The pair seat i puts next is the own pair in mid-air shown as the one about to appear (InHand::appearing): it goes
    // from inHand[i].pair in inHand[i].frame, not from where a pair appears.
    bool appearingInAir(int i) const { return inHand[i].appearing && inHand[i].known && player[i].moves == firstMoves[i]; }

    bool hasRecordedWay(int i, Placement target) const {
        const InHand& hand = inHand[i];
        return fastestSeat[i] != Observation::FASTEST_BOUND && hand.known && player[i].moves == firstMoves[i] &&
               hand.wayTarget == placementId(target) && hand.wayLock >= hand.frame && hand.wayCheck >= hand.wayLock;
    }

    Duel() = default;
    // The observation must be of two players.
    explicit Duel(const Observation& o, uint64_t garbageSeed = 0) : rules(&o.rules), timing(&o.timing), frame(o.frame), state(o.state), nextGarbageOrder(o.nextGarbageOrder) {
        const int airSeat = o.ownAirSeat >= 0 ? o.ownAirSeat : o.me;
        for (int i = 0; i < 2; ++i) {
            garbageRng[i] = Rng(garbageSeed, DUEL_GARBAGE_STREAM + uint64_t(i));
            const Player& s = o.player[i];
            DuelPlayer& p = player[i];
            fastestSeat[i] = o.fastestRead[i];
            firstMoves[i] = s.moves;
            fastestStates[i] = o.fastestReadStates[i];
            const bool appearing = i == airSeat && o.ownAir && s.phase == Phase::Next;
            if (appearing || (fastestSeat[i] && s.phase == Phase::Control)) {
                const bool ownsWay = i == airSeat;
                inHand[i] = {true, appearing ? o.ownAirPair : s.pair, o.frame,
                             ownsWay ? o.ownWayTarget : -1, ownsWay ? o.ownWayCheck : -1,
                             ownsWay ? o.ownWayLock : -1, ownsWay ? o.ownWaySoft : 0, appearing};
            }
            static_cast<PlayerCore&>(p) = s;
            p.nextPair = s.nextPair;
            p.moves = s.moves;
            p.chain = s.chain;
            // A phase with `wait` frames left ends in the step of frame + wait - 1.
            const int64_t end = frame + s.wait - 1;
            switch (s.phase) {
            case Phase::Next:
                // (Observation::busyLeft: the seat's landing or chain ends first, as after a pair put)
                if (o.busyLeft[i] > 0) p.phase = DuelPhase::Check, p.at = frame + o.busyLeft[i] - 1, p.chain.chains = o.busyChain[i] ? 1 : 0;
                else p.phase = DuelPhase::Spawn, p.at = end;
                break;
            case Phase::Control: p.phase = DuelPhase::Control; break;
            case Phase::Split: p.phase = DuelPhase::Check, p.at = end + timing->landFrames; break;
            case Phase::Land:
            case Phase::Wave: p.phase = DuelPhase::Check, p.at = end; break;
            case Phase::Decide: p.phase = DuelPhase::Settle, p.at = frame; break; // (not seen between frames of a Round)
            case Phase::Garbage: p.phase = DuelPhase::Spawn, p.at = end + timing->nextFrames; break;
            case Phase::Dead: p.phase = DuelPhase::Dead; break;
            }
        }
    }

    // Plays the frames before `until`, but stops after a frame in which a pair
    // appeared: returns the players (bit i) whose pair appeared in frame
    // `frame - 1` and who now need place(). Returns 0 when `until` is reached
    // or the round is over (see state).
    int run(int64_t until) {
        while (state == RoundState::Playing && frame < until) {
            int64_t next = until;
            for (const DuelPlayer& p : player)
                if (p.phase != DuelPhase::Control && p.phase != DuelPhase::Dead && p.at < next) next = p.at;
            if (next >= until) break;
            if (next < frame) {
                // (a phase given with no frame left: ended in this frame, as are the others that should have ended before)
                next = frame;
                for (DuelPlayer& p : player)
                    if (p.phase != DuelPhase::Control && p.phase != DuelPhase::Dead && p.at < frame) p.at = frame;
            }
            frame = next;
            int spawned = 0;
            for (int i = 0; i < 2; ++i) {
                DuelPlayer& p = player[i];
                if (p.at != frame) continue;
                if (p.phase == DuelPhase::Spawn) {
                    if (p.field.dead()) {
                        p.phase = DuelPhase::Dead;
                    } else {
                        ++p.nextPair;
                        p.spawnFrame = frame;
                        p.phase = DuelPhase::Control;
                        spawned |= 1 << i;
                    }
                } else if (p.phase == DuelPhase::Check) {
                    WaveReport w;
                    Incoming& opponent = player[1 - i].incoming;
                    if (chainWave(p, opponent, *rules, rules->rateAt(frame), p.chain, w)) {
                        if (auto* fire = exchangeTrace ? exchangeTrace->get(i, p.moves) : nullptr) {
                            if (fire->ignition < 0) fire->ignition = frame;
                            fire->waves = p.chain.chains;
                            fire->points += w.points;
                            fire->sent += w.sent;
                            fire->offset += w.offsetted;
                        }
                        p.at = frame + timing->waveFrames(w.longestFall);
                    } else {
                        chainEnd(p, opponent, p.chain);
                        if (exchangeTrace && p.chain.chains) {
                            if (auto* fire = exchangeTrace->get(i, p.moves)) fire->confirmed = frame;
                        }
                        p.phase = DuelPhase::Settle;
                    }
                }
            }
            for (int i = 0; i < 2; ++i) {
                DuelPlayer& p = player[i];
                if (p.phase != DuelPhase::Settle || p.at != frame) continue;
                int heights[W], placed[W];
                p.field.heights(heights);
                auto& order = nextGarbageOrder[size_t(i)];
                if (fullGarbageOrder && p.incoming.confirmed > 0 && rules->maxGarbagePerDrop > 0) {
                    // A paired counterfactual uses the same permutation at each
                    // drop number even when its offsets change the remainder.
                    std::array<uint8_t, W> columns{0, 1, 2, 3, 4, 5};
                    garbageRng[i].partialShuffle(columns.data(), columns.size(), columns.size() - 1);
                    Rng unused = garbageRng[i];
                    p.dropped = dropGarbage(p.field, p.incoming, *rules, unused, placed, order ? &*order : &columns);
                } else {
                    p.dropped = dropGarbage(p.field, p.incoming, *rules, garbageRng[i], placed, order ? &*order : nullptr);
                }
                if (p.dropped > 0) {
                    order.reset();
                    if (firstDrops && !(*firstDrops)[i].amount) (*firstDrops)[i] = {frame, p.dropped, p.field};
                }
                p.fallen += p.dropped;
                p.settleFrame = frame;
                p.phase = DuelPhase::Spawn;
                p.at = frame + (p.dropped ? timing->garbageFrames(heights, placed) : 0) + timing->nextFrames;
            }
            ++frame;
            const bool dead0 = player[0].phase == DuelPhase::Dead, dead1 = player[1].phase == DuelPhase::Dead;
            if (dead0 || dead1) state = dead0 && dead1 ? RoundState::Draw : dead0 ? RoundState::Won1 : RoundState::Won0;
            if (spawned) return spawned;
        }
        if (state == RoundState::Playing && frame < until) frame = until;
        return 0;
    }

    // Player i (in Control) puts the pair with the fastest input. The pair must
    // have appeared in this Duel (spawnFrame known), and the way to the
    // placement must be the straight one the Timing tables describe.
    void placeFast(int i, Placement target, Pair pair) { place(i, target, pair, -1, 0, 0); }

    // The same with the frame of the lock and the rows of soft drop given (a
    // pair observed in mid-air, a path that is not the straight one, waiting).
    // The first wave must not lie before `frame`. rested: the frames the pair will have lain on the ground
    // at that lock; the landing waited for after the lock is shorter by them (0 = none, or not counted).
    // (lockFrame -1: placeFast) ready: the first frame the pair can be moved in, when that is not the frame it appeared
    // in (-1: that frame).
    void place(int i, Placement target, Pair pair, int64_t lockFrame, int softDropRows, int rested = 0, int64_t ready = -1) {
        DuelPlayer& p = player[i];
        const bool fastest = lockFrame < 0; // (placeFast: the frame of the lock is worked out here)
        int buttonFrames = 0, buttonSoft = 0; // the fastest buttons, when the tables do not describe the way (tallPaths)
        // (Timing::landThenLock: the tables are those of DEFAULT_TIMING, so the buttons are taken on any field)
        const bool held = fastest && appearingInAir(i);
        if (fastest && !held && (tallPaths > 0 || timing->landThenLock)) {
            int h[W], top = 0;
            p.field.heights(h);
            const int childX = target.childX();
            const int from = std::min(int(target.x), std::min(childX, SPAWN_X)), to = std::max(int(target.x), std::max(childX, SPAWN_X));
            for (int x = from > 0 ? from - 1 : 0; x <= to + 1 && x < W; ++x) top = h[x] > top ? h[x] : top;
            if (timing->landThenLock || top >= tallPaths) {
                const SpawnInput input = spawnInputs ? spawnInputs->get(p.field, target)
                    : tallSpawnInput(p.field, target, *timing, rules->speedPercent);
                buttonFrames = input.frames, buttonSoft = input.soft, rested = input.rested;
            }
        }
        // (fastestSeat: the way over soonest, from the pair as it appeared - or from `ready` - or as it was seen in mid-air:
        //  the frame its landing can be over in at the earliest, the frame of its lock and its points of the down key; -1:
        //  not counted. FASTEST_BOUND: the lock no sooner than FastestSearch counts on, the points not known. recorded: the
        //  pair goes on with the buttons it is played with, whatever the straight way would make of it - with or without
        //  fastestSeat.)
        int64_t soonest = -1, soonestLock = -1;
        int soonestSoft = -1;
        bool recorded = false;
        if (fastestSeat[i] || inHand[i].wayTarget >= 0) {
            const bool next = p.moves == firstMoves[i];
            const bool seen = inHand[i].known && next;
            if (seen || p.spawnFrame >= 0) {
                const PairControl from = seen ? inHand[i].pair : spawnPair(*timing);
                const int64_t at = seen ? inHand[i].frame : (ready >= 0 ? ready : p.spawnFrame) + wait[i];
                if (fastestSeat[i] == Observation::FASTEST_BOUND) {
                    static thread_local FastestSearch bound;
                    int lock = 0;
                    soonest = at + bound.estimate(p.field, from, target, *timing, rules->speedPercent, &lock) - 1;
                    soonestLock = at + lock - 1;
                } else if (seen && inHand[i].wayTarget == placementId(target) && inHand[i].wayCheck >= 0) {
                    soonest = inHand[i].wayCheck;
                    recorded = true;
                    if (inHand[i].wayLock >= 0) soonestLock = inHand[i].wayLock, soonestSoft = inHand[i].waySoft;
                } else if (next && fastestSeat[i]) {
                    const FastestWay way = fastestEnd(p.field, from, at, target, *timing, rules->speedPercent, fastestStates[i]);
                    if (way.end > 0) soonest = at + way.end - 1, soonestLock = at + way.lock - 1, soonestSoft = way.soft;
                }
            }
        }
        // (the pair in mid-air shown as the one about to appear, Observation::ownAir: the straight way from where it is, on
        //  the field before the pair is put - as Gaze has it for a pair in mid-air, not the tables of a pair that appears)
        if (held) {
            const SpawnInput input = heldPlacementInput(p.field, inHand[i].pair, target, *timing, rules->speedPercent);
            if (input.frames) {
                lockFrame = inHand[i].frame + input.frames - 1;
                softDropRows = input.soft;
                rested = input.rested;
            } else if (soonest >= 0 && soonestLock >= 0 && soonestSoft >= 0) {
                lockFrame = soonestLock;
                softDropRows = soonestSoft;
            } else {
                return;
            }
        }
        const PlaceResult r = juraic::place(p.field, target, pair);
        if (fastest && !held && buttonFrames > 0) {
            lockFrame = p.spawnFrame + buttonFrames - 1; // (the first button moves the pair in the frame it appears)
            softDropRows = buttonSoft;
        } else if (fastest && !held) {
            lockFrame = p.spawnFrame + timing->placeFrames(target.x, r.lockY) - timing->landFrames - timing->nextFrames;
            softDropRows = SPAWN_Y - r.lockY + timing->dropLockPoints + timing->dropLandPoints; // a point for every row with the down key, and those of landing with it
        }
        if (fastest && !held && wait[i] > 0) lockFrame += wait[i]; // (slow hands)
        const int split = timing->splitFrames(r.split, target.x, r.lockY);
        int land = timing->landFrames;
        if (rested > 0 && !split) land = land - rested < timing->lockWait ? timing->lockWait : land - rested;
        int64_t check = lockFrame + split + land;
        // (the soonest way. With a piece left to fall: from its lock, the fall and then all of the landing - game/player.h;
        //  FastestSearch counts the end of such a way as if nothing fell -, not counted without the lock. With its buttons
        //  known: their lock and their points. FASTEST_BOUND: the lock no later than the check.)
        if (split) soonest = soonestLock >= 0 ? soonestLock + split + timing->landFrames : -1;
        if (soonest >= 0 && (recorded || soonest < check)) {
            check = soonest;
            if (soonestSoft >= 0) lockFrame = soonestLock, softDropRows = soonestSoft;
            else if (lockFrame > check) lockFrame = check;
        }
        if (rules->softDropBonus && softDropRows > 0) {
            p.scoring.score += softDropRows;
            p.scoring.pool += softDropRows;
        }
        ++p.moves;
        p.chain = {};
        p.lockFrame = lockFrame;
        p.phase = DuelPhase::Check;
        p.at = check;
    }
};

} // namespace juraic::ai
