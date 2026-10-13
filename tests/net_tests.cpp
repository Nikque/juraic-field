// Tests of a match over a line (play/net.h), with a line inside the program that delays, loses and
// reorders (FakeLine): two ends call, agree on the match, and play rounds - each its own seat every
// frame, the other from the buttons that arrive. Both must see every round end the same way, and the
// round played again from the buttons each end kept must be that round.
// And what an end does with datagrams that are not those of a match played by the rules: callers a host
// cannot use, frames further ahead than a round allows, a delay no round can take, a clock that is no time.
#include "../play/net.h"
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace juraic;
using namespace play;

static void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

struct Hands {
    Rng rng;
    uint8_t held = 0;
    explicit Hands(uint64_t seed) : rng(seed, 55) {}
    uint8_t next() {
        if (rng.below(4) == 0) held = uint8_t(rng.below(32) | (rng.below(3) ? BTN_DOWN : 0));
        return held;
    }
};

// One end as the game drives it: the peer, the round, and the little state between rounds.
struct End {
    NetPeer peer;
    LinkedRound link;
    Rng seeds{1};
    Hands hands{1};
    bool inRound = false;
    int pause = 0;       // ticks until the next round begins
    int rounds = 0;      // rounds ended
    long waits = 0;      // ticks the own seat could not play
    std::vector<std::string> ends; // how each round ended, as this end saw it
    std::vector<std::vector<uint8_t>> kept[2]; // the buttons of each seat in each round

    void beginMatch() { seeds = Rng(peer.match.seed); }
    void beginRound(const Rules& rules) {
        peer.startRound();
        link.start(rules, seeds.next() % 1000000000ull, peer.match.delay);
        inRound = true;
    }
    void tick(const Rules& rules, int roundsWanted) {
        peer.tick();
        if (peer.state != NetPeer::Joined) return;
        if (!inRound) {
            if (rounds < roundsWanted && --pause <= 0) beginRound(rules);
            return;
        }
        const auto each = [](int, const PlayerEvents&) {};
        playArrived(link, peer, each);
        if (link.round.state == RoundState::Playing) {
            if (link.canPlay(peer.seat)) playOwn(link, peer, hands.next(), each);
            else ++waits;
            playArrived(link, peer, each);
        }
        if (link.round.state != RoundState::Playing) {
            ends.push_back(std::to_string(int(link.round.state)) + " at " + std::to_string(link.round.frame));
            for (int s = 0; s < 2; ++s) kept[s].push_back(peer.buttons[s]);
            inRound = false, ++rounds, pause = 40;
        }
    }
};

static void playMatches(int delay, int jitter, int loss, int fixedDelay, int rounds, uint64_t seed, long& waits, int& delaySet) {
    auto wire = std::make_shared<FakeWire>();
    wire->delay = delay, wire->jitter = jitter, wire->loss = loss, wire->rng = Rng(seed, 9);
    Rules rules;
    rules.colors = 3;
    End end[2];
    NetMatch match;
    match.id = uint32_t(seed) | 1, match.seed = seed, match.rules = "colors=3\n";
    end[0].peer.wait(std::make_unique<FakeLine>(wire, 0), match, "host", 12, fixedDelay);
    end[1].peer.call(std::make_unique<FakeLine>(wire, 1), "guest", 12);
    end[0].hands = Hands(seed * 2 + 1), end[1].hands = Hands(seed * 2 + 2);
    bool begun[2] = {false, false};
    const std::string tag = " (line " + std::to_string(delay) + "+-" + std::to_string(jitter) + ", loss " + std::to_string(loss) + "%)";
    for (int tick = 0; tick < 400000 && (end[0].rounds < rounds || end[1].rounds < rounds); ++tick)
        for (int m = 0; m < 2; ++m) {
            End& e = end[m];
            // the guest begins when it has the match, half a round trip later; the host when the guest's first packet shows it has
            if (!begun[m] && e.peer.state == NetPeer::Joined && (m == 1 || e.peer.hisPlaying)) begun[m] = true, e.beginMatch(), e.pause = m == 1 ? e.peer.roundTrip / 2 : 0;
            if (begun[m]) e.tick(rules, rounds);
            else e.peer.tick();
            check(e.peer.state != NetPeer::Apart, "the two ends disagree about a round" + tag);
            check(e.peer.state != NetPeer::Lost && e.peer.state != NetPeer::Left && e.peer.state != NetPeer::Refused, "the line was given up" + tag);
        }
    check(end[0].rounds == rounds && end[1].rounds == rounds, "the rounds were not all played" + tag);
    check(end[0].peer.match.delay == end[1].peer.match.delay && end[1].peer.match.rules == "colors=3\n" && end[1].peer.match.name[0] == "host" && end[0].peer.match.name[1] == "guest",
          "the guest did not get the host's match" + tag);
    check(end[0].ends == end[1].ends, "the two ends saw a round end differently" + tag);
    for (int r = 0; r < rounds; ++r) {
        // (each end kept the other's buttons as far as it needed them: up to the end of the round they are the same)
        Rng seeds(seed);
        uint64_t roundSeed = 0;
        for (int k = 0; k <= r; ++k) roundSeed = seeds.next() % 1000000000ull;
        for (int m = 0; m < 2; ++m) {
            LinkedRound again;
            again.start(rules, roundSeed, end[m].peer.match.delay);
            PlayerEvents ev;
            for (size_t f = 0; again.round.state == RoundState::Playing; ++f)
                for (int s = 0; s < 2; ++s) {
                    if (again.round.state != RoundState::Playing) break;
                    check(f < end[m].kept[s][size_t(r)].size(), "an end did not keep the buttons of a whole round" + tag);
                    again.play(s, end[m].kept[s][size_t(r)][f], ev);
                }
            check(std::to_string(int(again.round.state)) + " at " + std::to_string(again.round.frame) == end[m].ends[size_t(r)], "a round played again from its buttons ended differently" + tag);
        }
    }
    waits = end[0].waits + end[1].waits;
    delaySet = end[0].peer.match.delay;
    check(loss == 0 || wire->lost > 0, "the line lost nothing");
}

static void testDelayFor() {
    check(delayFor(0, 0) == 2 && delayFor(4, 4) == 4 && delayFor(6, 10) == 7 && delayFor(200, 200) == MAX_LINK_DELAY, "the delay for a line is not what it should be");
}

// The wrong version is refused; a guest that leaves is noticed; a line gone silent is given up.
static void testEnds() {
    {
        auto wire = std::make_shared<FakeWire>();
        NetPeer host, guest;
        host.wait(std::make_unique<FakeLine>(wire, 0), NetMatch{}, "h", 12);
        guest.call(std::make_unique<FakeLine>(wire, 1), "g", 11);
        for (int i = 0; i < 100; ++i) host.tick(), guest.tick();
        check(guest.state == NetPeer::Refused && guest.reason == "version" && host.state == NetPeer::Waiting, "a guest of another version was not refused");
    }
    for (int way = 0; way < 2; ++way) {
        auto wire = std::make_shared<FakeWire>();
        wire->delay = 3;
        NetPeer host, guest;
        NetMatch m;
        m.id = 5;
        host.wait(std::make_unique<FakeLine>(wire, 0), m, "h", 12);
        guest.call(std::make_unique<FakeLine>(wire, 1), "g", 12);
        for (int i = 0; i < 200; ++i) host.tick(), guest.tick();
        check(host.state == NetPeer::Joined && guest.state == NetPeer::Joined && host.hisPlaying, "the two ends did not join");
        check(host.match.delay == delayFor(host.roundTrip, host.roundTripMax) && host.roundTrip >= 5 && host.roundTrip <= 8, "the host did not take the time of the line: " + std::to_string(host.roundTrip));
        if (way == 0) {
            guest.leave();
            for (int i = 0; i < 20; ++i) host.tick();
            check(host.state == NetPeer::Left, "the host did not notice that the guest left");
        } else {
            wire->loss = 100;
            for (int i = 0; i < host.silenceLimit + 10; ++i) host.tick(), guest.tick();
            check(host.state == NetPeer::Lost && guest.state == NetPeer::Lost, "a dead line was not given up");
        }
    }
    // Another match: both ends ask, in either order, and both begin it.
    for (int first = 0; first < 2; ++first) {
        auto wire = std::make_shared<FakeWire>();
        wire->delay = 2, wire->loss = 30, wire->rng = Rng(3, uint64_t(first));
        NetPeer end[2];
        NetMatch m;
        m.id = 9;
        end[0].wait(std::make_unique<FakeLine>(wire, 0), m, "h", 12);
        end[1].call(std::make_unique<FakeLine>(wire, 1), "g", 12);
        for (int i = 0; i < 300; ++i) end[0].tick(), end[1].tick();
        check(end[0].state == NetPeer::Joined && end[1].state == NetPeer::Joined, "the two ends did not join over a line that loses");
        end[first].again = true;
        for (int i = 0; i < 100; ++i)
            for (NetPeer& e : end) {
                e.tick();
                check(e.matches == 0, "a match began that only one end asked for");
            }
        end[1 - first].again = true;
        for (int i = 0; i < 300; ++i)
            for (NetPeer& e : end) {
                e.tick();
                if (e.again && e.hisAgain) e.startMatch();
            }
        check(end[0].matches == 1 && end[1].matches == 1 && !end[0].again && !end[1].again && !end[0].hisAgain && !end[1].hisAgain, "the next match did not begin at both ends");
    }
    std::cout << "ends: another version is refused, leaving and a dead line are noticed, the next match begins at both ends\n";
}

// The line of a host as a UDP socket is one (play/udp.h): the datagrams of every machine that calls arrive at
// it; the first sender is taken for the guest and the others are passed over, until the host forgets him.
struct Door : Line {
    std::deque<std::pair<int, Datagram>> in; // who sent it, and what
    std::deque<Datagram> out[3];             // what the host sent, by the caller it went to
    int guest = -1;
    void send(const Datagram& d) override {
        if (guest >= 0) out[guest].push_back(d);
    }
    bool receive(Datagram& d) override {
        while (!in.empty()) {
            const int who = in.front().first;
            Datagram data = std::move(in.front().second);
            in.pop_front();
            if (guest < 0) guest = who;
            else if (who != guest) continue;
            d = std::move(data);
            return true;
        }
        return false;
    }
    void forget() override { guest = -1; }
};
// One of the machines that call at that door.
struct Caller : Line {
    Door* door;
    int who;
    Caller(Door* d, int w) : door(d), who(w) {}
    void send(const Datagram& d) override { door->in.emplace_back(who, d); }
    bool receive(Datagram& d) override {
        std::deque<Datagram>& q = door->out[who];
        if (q.empty()) return false;
        d = std::move(q.front());
        q.pop_front();
        return true;
    }
};

// A host that waits is not held by a caller it cannot use - one of another version, which it refuses, or
// something that is no call at all: the next caller is heard. Once two ends have joined, a third machine is
// passed over whatever it sends.
static void testDoor() {
    for (int way = 0; way < 4; ++way) {
        const std::string tag = " (way " + std::to_string(way) + ")";
        NetPeer host, stranger, guest;
        auto owned = std::make_unique<Door>();
        Door* door = owned.get();
        NetMatch m;
        m.id = 7;
        host.wait(std::move(owned), m, "h", 12);
        if (way == 0 || way == 3) stranger.call(std::make_unique<Caller>(door, 0), "old", 11); // another version
        if (way == 1) door->in.emplace_back(0, Datagram{'J', 'F', uint8_t(NET_VERSION + 1), uint8_t(NET_HELLO)}); // another protocol
        if (way == 2) door->in.emplace_back(0, Writer(NET_PING).u64(1).d);                     // no call at all
        // (way 3: the stranger and the guest call in the same frame, the stranger first)
        if (way == 3) stranger.tick(), guest.call(std::make_unique<Caller>(door, 1), "g", 12), guest.tick();
        else {
            for (int i = 0; i < 40; ++i) host.tick(), stranger.tick();
            check(host.state == NetPeer::Waiting, "a host took a caller it cannot use" + tag);
            if (way == 0) check(stranger.state == NetPeer::Refused && stranger.reason == "version", "a caller of another version was not refused" + tag);
            guest.call(std::make_unique<Caller>(door, 1), "g", 12);
        }
        for (int i = 0; i < 200; ++i) host.tick(), stranger.tick(), guest.tick();
        check(host.state == NetPeer::Joined && guest.state == NetPeer::Joined && host.match.name[1] == "g" && door->guest == 1,
              "a host that had heard a caller it cannot use did not take the next one" + tag);
        if (way == 3) check(stranger.state == NetPeer::Refused, "a caller of another version was not refused" + tag);
        door->in.emplace_back(2, Writer(NET_BYE).u32(m.id).d);
        door->in.emplace_back(2, Writer(NET_HELLO).u32(12).text("x").d);
        for (int i = 0; i < 20; ++i) host.tick(), guest.tick();
        check(host.state == NetPeer::Joined && guest.state == NetPeer::Joined && door->guest == 1 && host.match.name[1] == "g", "a third machine disturbed a match" + tag);
    }
    std::cout << "door: a caller of another version, of another protocol, and one that does not call do not keep the next caller out; a third machine does not disturb a match\n";
}

// Frames of the other seat further ahead of the own than a round lets a seat play (game/link.h: `delay`
// frames, MAX_LINK_DELAY at most) are not kept, however many an end sends; they are taken when the own seat
// has gone on.
static void testAhead() {
    auto wire = std::make_shared<FakeWire>();
    NetPeer host, guest;
    NetMatch m;
    m.id = 11;
    host.wait(std::make_unique<FakeLine>(wire, 0), m, "h", 12);
    guest.call(std::make_unique<FakeLine>(wire, 1), "g", 12);
    for (int i = 0; i < 200; ++i) host.tick(), guest.tick();
    check(host.state == NetPeer::Joined && guest.state == NetPeer::Joined, "the two ends did not join");
    host.startRound(), guest.startRound();
    // (3,000 frames of seat 1 at once, in datagrams of 600, as a changed program might send them)
    const auto flood = [&](size_t from) {
        for (size_t k = 0; k < 5; ++k) {
            Writer w(NET_PLAY);
            w.u32(host.match.id).u32(0).u8(0).u32(host.round).u32(0).u32(uint32_t(from + k * 600)).u32(600);
            for (int i = 0; i < 600; ++i) w.u8(0);
            for (int seat = 0; seat < 2; ++seat) w.u32(~0u).u64(~0ull).u64(0);
            wire->toward[0].push_back(FakeWire::Held{wire->now, w.d});
        }
        host.tick();
    };
    flood(0);
    check(host.state == NetPeer::Joined && host.buttons[1].size() == size_t(MAX_LINK_DELAY), "frames far ahead of the own seat were kept: " + std::to_string(host.buttons[1].size()));
    for (int i = 0; i < 5; ++i) host.played(0);
    flood(size_t(MAX_LINK_DELAY));
    check(host.buttons[1].size() == size_t(MAX_LINK_DELAY) + 5, "frames that follow were not taken when the own seat had gone on: " + std::to_string(host.buttons[1].size()));
    std::cout << "ahead: of 3,000 frames sent at once " << MAX_LINK_DELAY << " are kept, and the next when the own seat goes on\n";
}

// The delay of a match is one a round can take (1 to MAX_LINK_DELAY), whatever the host says and whatever the
// own settings say: both ends play with the number that is in the match, and so does the replay.
static void testDelayKept() {
    for (const uint32_t said : {0u, 1u, 7u, 30u, 31u, 5000u, 0xFFFFFFFFu}) {
        auto wire = std::make_shared<FakeWire>();
        NetPeer guest;
        guest.call(std::make_unique<FakeLine>(wire, 1), "g", 12);
        wire->toward[1].push_back(FakeWire::Held{wire->now, Writer(NET_WELCOME).u32(3).u64(4).u32(said).text("").text("h").text("g").u32(0).d});
        guest.tick();
        const int want = said < 1 ? 1 : said > uint32_t(MAX_LINK_DELAY) ? MAX_LINK_DELAY : int(said);
        check(guest.state == NetPeer::Joined && guest.match.delay == want, "a guest took the delay " + std::to_string(guest.match.delay) + " when the host said " + std::to_string(said));
    }
    {
        auto wire = std::make_shared<FakeWire>();
        NetPeer host, guest;
        host.wait(std::make_unique<FakeLine>(wire, 0), NetMatch{}, "h", 12, 99);
        guest.call(std::make_unique<FakeLine>(wire, 1), "g", 12);
        for (int i = 0; i < 200; ++i) host.tick(), guest.tick();
        check(host.state == NetPeer::Joined && guest.state == NetPeer::Joined && host.match.delay == MAX_LINK_DELAY && guest.match.delay == MAX_LINK_DELAY,
              "a delay set by hand beyond what a round takes was not brought down: " + std::to_string(host.match.delay) + ", " + std::to_string(guest.match.delay));
    }
    std::cout << "delay: a match never has a delay a round cannot take\n";
}

// The clock of the line (NET_PING, sent back as NET_PONG) and the round trip a host names in its welcome are
// numbers of the other machine. A clock this end cannot have sent - one that is no time at all, one from the
// future, one older than a line may be silent - is passed over, and the time of the line stays one a line can have.
static void testClock() {
    auto wire = std::make_shared<FakeWire>();
    wire->delay = 3;
    NetPeer host, guest;
    NetMatch m;
    m.id = 13;
    host.wait(std::make_unique<FakeLine>(wire, 0), m, "h", 12);
    guest.call(std::make_unique<FakeLine>(wire, 1), "g", 12);
    for (int i = 0; i < 700; ++i) host.tick(), guest.tick();
    check(host.state == NetPeer::Joined && guest.state == NetPeer::Joined && host.ticks > host.silenceLimit + 100, "the two ends did not join");
    for (int i = 0; i < 10; ++i) host.tick(); // (what the guest had sent has arrived; nothing more comes)
    const int before = host.roundTrip, most = host.roundTripMax;
    check(before >= 5 && before <= 8 && most >= before && most <= 8, "the round trip of a line of three frames each way: " + std::to_string(before) + ", at most " + std::to_string(most));
    const auto pong = [&](uint64_t said) {
        wire->toward[0].push_back(FakeWire::Held{wire->now, Writer(NET_PONG).u64(said).d});
        host.tick();
    };
    const std::initializer_list<uint64_t> noClocks = {0x8000000000000000ull, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFF00000000ull, 0x7FFFFFFFFFFFFFFFull, uint64_t(host.ticks) + 50, 1};
    for (const uint64_t said : noClocks) {
        pong(said);
        check(host.state == NetPeer::Joined && host.roundTrip == before && host.roundTripMax == most,
              "a clock this end cannot have sent a moment ago (" + std::to_string(said) + ") changed the time of the line: " + std::to_string(host.roundTrip) + ", at most " + std::to_string(host.roundTripMax));
    }
    pong(uint64_t(host.ticks - 3)); // (sent four frames before it is read)
    check(host.roundTrip == (before * 7 + 4 + 4) / 8 && host.roundTripMax == most, "a clock that came back after four frames was not taken: " + std::to_string(host.roundTrip));
    for (const uint32_t said : {0u, 12u, 0x80000000u, 0xFFFFFFFFu}) {
        auto other = std::make_shared<FakeWire>();
        NetPeer late;
        late.call(std::make_unique<FakeLine>(other, 1), "g", 12);
        other->toward[1].push_back(FakeWire::Held{other->now, Writer(NET_WELCOME).u32(3).u64(4).u32(2).text("").text("h").text("g").u32(said).d});
        late.tick();
        check(late.state == NetPeer::Joined && late.roundTrip >= 0 && late.roundTrip <= late.silenceLimit && late.roundTripMax == late.roundTrip && (said > 12 || late.roundTrip == int(said)),
              "a guest took the round trip " + std::to_string(late.roundTrip) + " when the host said " + std::to_string(said));
    }
    std::cout << "clock: a clock that is no time, one from the future and one older than a line may be silent are passed over; the round trip of a welcome is one a line can have\n";
}

int main() {
    try {
        testDelayFor();
        testEnds();
        testDoor();
        testAhead();
        testDelayKept();
        testClock();
        struct Case {
            int delay, jitter, loss, fixed;
        };
        const Case cases[] = {{0, 0, 0, 0}, {1, 0, 0, 0}, {3, 0, 0, 0}, {3, 2, 0, 0}, {5, 3, 10, 0}, {2, 1, 30, 0}, {8, 6, 5, 0}, {6, 0, 0, 3}, {12, 10, 20, 0}};
        long frames = 0;
        for (const Case& c : cases) {
            long waits = 0;
            int delay = 0;
            playMatches(c.delay, c.jitter, c.loss, c.fixed, 6, 1000 + uint64_t(c.delay * 100 + c.jitter * 10 + c.loss), waits, delay);
            std::cout << "line " << c.delay << "+-" << c.jitter << " frames, loss " << c.loss << "%: delay " << delay << (c.fixed ? " (set by hand)" : "") << ", the own seat waited "
                      << waits << " frames in 6 rounds\n";
            // (a steady line never makes the own seat wait once the delay is set by it)
            if (c.jitter == 0 && c.loss == 0 && !c.fixed) check(waits == 0, "the own seat waited on a steady line");
            ++frames;
        }
        std::cout << "net tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
