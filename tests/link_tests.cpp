// Tests of a round of two machines (game/link.h).
//
//  1. Whatever the order in which the two seats are played, every frame of each seat is the frame of
//     a plain reference that plays both seats together and keeps one account of the garbage, with the
//     exchange of a frame taking effect `delay` frames later. And the round ends the same way.
//  2. Garbage never waits above both fields at once, however the attacks cross.
//  3. Two machines, each playing its own seat ahead of the other, see the same round.
#include "../game/link.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace juraic;

static void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

// One seat after one of its frames, as a number.
static uint64_t stamp(const Player& p) {
    uint64_t h = uint64_t(KeyHash()(p.field.f.key()));
    const auto mix = [&h](uint64_t v) { h = (h ^ v) * 0x9E3779B97F4A7C15ull, h ^= h >> 29; };
    for (int x = 0; x < W; ++x) mix(uint64_t(p.field.row14[size_t(x)]));
    mix(uint64_t(p.scoring.score)), mix(uint64_t(p.scoring.pool)), mix(uint64_t(p.scoring.allClear));
    mix(uint64_t(p.incoming.pending)), mix(uint64_t(p.incoming.confirmed));
    mix(uint64_t(p.phase)), mix(uint64_t(p.wait)), mix(uint64_t(p.moves)), mix(uint64_t(p.nextPair));
    mix(uint64_t(p.pair.x)), mix(uint64_t(p.pair.y)), mix(uint64_t(p.pair.dir));
    return h;
}

// The buttons of a seat, frame after frame: random, held for a few frames, much soft drop.
struct Hands {
    Rng rng;
    uint8_t held = 0;
    explicit Hands(uint64_t seed, int seat) : rng(seed, 100 + uint64_t(seat)) {}
    uint8_t next() {
        if (rng.below(4) == 0) held = uint8_t(rng.below(32) | (rng.below(3) ? BTN_DOWN : 0));
        return held;
    }
};

// The reference: both seats frame by frame, one account of the garbage.
struct Reference {
    Round round;
    int delay = 1;
    Incoming account[2];
    struct Made {
        int garbage;
        bool ended;
    };
    std::vector<Made> made[2]; // by frame
    std::vector<uint64_t> stamps[2];
    long sent = 0, offsets = 0, fell = 0, both = 0;

    void step(const uint8_t buttons[2]) {
        const int64_t frame = round.frame;
        if (frame >= delay)
            for (int seat = 0; seat < 2; ++seat) {
                const Made m = made[seat][size_t(frame - delay)];
                if (m.garbage) {
                    const int rest = offset(account[seat], m.garbage);
                    account[1 - seat].pending += rest;
                    sent += rest, offsets += m.garbage - rest;
                }
                if (m.ended) account[1 - seat].confirm();
            }
        if (account[0].total() && account[1].total()) ++both;
        PlayerEvents e[2];
        for (int seat = 0; seat < 2; ++seat) {
            Player& p = round.player[seat];
            p.incoming = {};
            Incoming out;
            e[seat] = advancePlayer(p, out, buttons[seat], round.tsumo, round.rules, round.timing, frame);
            made[seat].push_back(Made{out.total(), e[seat].chainEnded});
            p.incoming = account[seat];
        }
        for (int seat = 0; seat < 2; ++seat) {
            Player& p = round.player[seat];
            settlePlayer(p, round.rules, round.timing, round.garbageRng[seat], e[seat]);
            account[seat] = p.incoming;
            fell += e[seat].garbageFell;
            stamps[seat].push_back(stamp(p));
        }
        ++round.frame;
        const bool dead0 = round.player[0].phase == Phase::Dead, dead1 = round.player[1].phase == Phase::Dead;
        if (dead0 || dead1) round.state = dead0 && dead1 ? RoundState::Draw : dead0 ? RoundState::Won1 : RoundState::Won0;
    }
};

static Rules rulesOf(int game) {
    Rules rules;
    if (game % 2) rules.colors = 3; // more chains
    if (game % 5 == 4) rules.targetPoints = 20; // much garbage
    return rules;
}

static void testAgainstReference() {
    Rng pick(20261013, 7);
    long frames = 0, sent = 0, offsets = 0, fell = 0, waits = 0;
    int results[4]{};
    uint64_t sum = 0;
    const int delays[] = {1, 2, 3, 5, 8, 12, 20, MAX_LINK_DELAY};
    for (int game = 0; game < 240; ++game) {
        const int delay = delays[game % 8];
        const uint64_t seed = pick.next() % 1000000000ull;
        const Rules rules = rulesOf(game);

        Reference ref;
        ref.round.start(rules, seed);
        ref.delay = delay;
        {
            Hands hands[2] = {Hands(seed, 0), Hands(seed, 1)};
            while (ref.round.state == RoundState::Playing && ref.round.frame < 60000) {
                const uint8_t buttons[2] = {hands[0].next(), hands[1].next()};
                ref.step(buttons);
            }
        }
        check(ref.round.state != RoundState::Playing, "a round of random play did not end");
        check(ref.both == 0, "garbage waited above both fields at once (delay " + std::to_string(delay) + ", game " + std::to_string(game) + ")");

        // The linked round, its seats played in a random order: runs of one seat, as far as it may go.
        LinkedRound link;
        link.start(rules, seed, delay);
        Hands hands[2] = {Hands(seed, 0), Hands(seed, 1)};
        while (link.round.state == RoundState::Playing) {
            const int seat = int(pick.below(2)), run = 1 + int(pick.below(uint64_t(2 * delay)));
            for (int k = 0; k < run; ++k) {
                if (!link.canPlay(seat)) {
                    ++waits;
                    break;
                }
                PlayerEvents e;
                link.play(seat, hands[seat].next(), e);
                const Player& p = link.round.player[seat];
                const size_t f = size_t(link.played[seat] - 1);
                check(p.incoming.pending >= 0 && p.incoming.confirmed >= 0, "negative garbage above a field");
                check(link.ahead[seat].pending >= 0 && link.ahead[seat].confirmed >= 0, "negative garbage in the account");
                // (a seat may play on past the frame the round ends in, before it knows: those frames count for nothing)
                if (f < ref.stamps[seat].size())
                    check(stamp(p) == ref.stamps[seat][f], "seat " + std::to_string(seat) + " differs from the reference in frame " + std::to_string(f) + " (delay " +
                                                               std::to_string(delay) + ", game " + std::to_string(game) + ")");
            }
            check(link.played[0] < 70000 && link.played[1] < 70000, "the linked round did not end");
        }
        check(link.round.state == ref.round.state && link.round.frame == ref.round.frame,
              "the linked round ended otherwise than the reference (delay " + std::to_string(delay) + ", game " + std::to_string(game) + ")");
        ++results[int(ref.round.state)];
        frames += long(ref.round.frame), sent += ref.sent, offsets += ref.offsets, fell += ref.fell;
        sum = sum * 0x9E3779B97F4A7C15ull + ref.stamps[0].back() + ref.stamps[1].back() + uint64_t(ref.round.frame);
    }
    check(sent > 100 && fell > 50 && offsets > 0 && results[1] > 20 && results[2] > 20, "the rounds are not exercising the garbage");
    check(waits > 100, "the seats never had to wait for each other");
    std::cout << "linked rounds against the reference: " << frames << " frames, " << sent << " garbage sent, " << offsets << " offset, " << fell
              << " drops, wins " << results[1] << '/' << results[2] << ", draws " << results[3] << ", checksum " << std::hex << uint32_t(sum ^ sum >> 32)
              << std::dec << '\n';
}

// Two chains made in the same frame, and one frame apart either way: on a single machine the second
// offsets the first. Here both are made before either seat has seen the other's, and still only one
// field ends up with garbage above it - the difference.
static void testCrossing() {
    for (int delay : {1, 4, 12})
        for (int gap = -2; gap <= 2; ++gap) {
            LinkedRound link;
            link.start(Rules{}, 5, delay);
            // seat 0 makes 10 in frame 20, seat 1 makes 6 in frame 20 + gap; both chains end 60 frames later
            const int64_t at[2] = {20, 20 + gap};
            const int garbage[2] = {10, 6};
            for (int seat = 0; seat < 2; ++seat) {
                link.made[seat].push_back(LinkedRound::Made{at[seat], garbage[seat], false});
                link.made[seat].push_back(LinkedRound::Made{at[seat] + 60, 0, true});
            }
            // (the players themselves stand still: no pair is in play before frame 7, and nothing is pressed)
            for (int64_t f = 0; f < 140; ++f) {
                for (int seat = 0; seat < 2; ++seat) {
                    // only what the account does is looked at: the fields are kept out of it
                    link.round.player[seat].phase = Phase::Wave, link.round.player[seat].wait = 1000;
                    PlayerEvents e;
                    check(link.play(seat, 0, e), "a seat could not play in step with the other");
                }
                // (both seats at the same frame)
                const Incoming& a = link.round.player[0].incoming;
                const Incoming& b = link.round.player[1].incoming;
                check(!(a.total() && b.total()), "garbage above both fields (delay " + std::to_string(delay) + ", gap " + std::to_string(gap) + ")");
            }
            const Incoming& a = link.round.player[0].incoming;
            const Incoming& b = link.round.player[1].incoming;
            check(a.total() == 0 && b.pending == 0 && b.confirmed == 4,
                  "crossing attacks did not offset each other (delay " + std::to_string(delay) + ", gap " + std::to_string(gap) + "): " + std::to_string(a.total()) + " / " +
                      std::to_string(b.total()));
        }
    std::cout << "crossing attacks offset each other: garbage is never above both fields\n";
}

// Two machines. Each plays its own seat every tick and the other seat from the buttons that have
// arrived, which are `lag` ticks old. Both must see the same round, and with a lag below the delay
// the own seat never waits.
static void testTwoMachines() {
    Rng pick(77, 3);
    long ownWaits = 0, lateWaits = 0;
    for (int game = 0; game < 60; ++game) {
        const int delay = 2 + game % 7, lag = game % 2 ? delay - 1 : delay + 2; // (the second: a line slower than the delay)
        const uint64_t seed = pick.next() % 1000000000ull;
        const Rules rules = rulesOf(game);
        std::vector<uint8_t> pressed[2]; // the buttons of each seat, by frame, as its own machine played them
        LinkedRound machine[2];
        Hands hands[2] = {Hands(seed, 0), Hands(seed, 1)};
        for (int m = 0; m < 2; ++m) machine[m].start(rules, seed, delay);
        std::vector<size_t> known[2]; // known[m][tick] = the frames of seat m played after that tick
        for (int tick = 0; tick < 70000 && (machine[0].round.state == RoundState::Playing || machine[1].round.state == RoundState::Playing); ++tick) {
            for (int m = 0; m < 2; ++m) {
                LinkedRound& mine = machine[m];
                // the other seat, as far as its buttons have arrived
                const size_t arrived = tick >= lag ? known[1 - m][size_t(tick - lag)] : 0;
                PlayerEvents e;
                while (size_t(mine.played[1 - m]) < arrived && mine.play(1 - m, pressed[1 - m][size_t(mine.played[1 - m])], e)) {}
                // the own seat: one frame a tick
                if (mine.canPlay(m)) {
                    pressed[m].push_back(hands[m].next());
                    mine.play(m, pressed[m].back(), e);
                } else if (mine.round.state == RoundState::Playing) {
                    ++(lag < delay ? ownWaits : lateWaits);
                }
                known[m].push_back(pressed[m].size());
            }
        }
        for (int m = 0; m < 2; ++m) check(machine[m].round.state != RoundState::Playing, "a machine did not see the round end");
        check(machine[0].round.state == machine[1].round.state && machine[0].round.frame == machine[1].round.frame, "the two machines saw the round end differently");
        // The frames up to the end must be the same on both: played again from the buttons, in step.
        LinkedRound again;
        again.start(rules, seed, delay);
        PlayerEvents e;
        for (int64_t f = 0; again.round.state == RoundState::Playing; ++f)
            for (int seat = 0; seat < 2; ++seat) check(again.play(seat, pressed[seat][size_t(f)], e) || again.round.state != RoundState::Playing, "the round could not be played again in step");
        check(again.round.state == machine[0].round.state && again.round.frame == machine[0].round.frame, "the round played again from its buttons ended differently");
    }
    check(ownWaits == 0, "a seat had to wait although the line was faster than the delay");
    check(lateWaits > 0, "a slow line did not make a seat wait");
    std::cout << "two machines see the same round; a line faster than the delay never makes the own seat wait\n";
}

int main() {
    try {
        testCrossing();
        testAgainstReference();
        testTwoMachines();
        std::cout << "link tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
