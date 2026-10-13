#pragma once
// A match of two people on two machines: the line between them and what is said over it.
//
// Only buttons travel. Both machines play the same round from the same seed, rules and delay
// (game/link.h): each its own seat at once, the other seat from the buttons that have arrived.
//
//   Line      datagrams to the one other machine and from it. They may be lost, come twice or out
//             of order; nothing here needs more. FakeLine is a line inside one program that loses,
//             delays and reorders by a seed (tests, and the self test of the game); UdpLine (udp.h)
//             is a UDP socket. Another way to connect is another Line.
//   NetPeer   one end of a match. The host waits, the guest calls. The host's rules, the seed of the
//             match and the delay go to the guest once; then every packet carries the buttons of the
//             own seat that the other has not acknowledged yet, so a lost packet costs nothing but
//             the time to the next one (sixty a second).
//
// A datagram: 'J' 'F', the version of this protocol, the kind, then what the kind holds. Numbers are
// little-endian. A datagram of another version or of another match is passed over.
#include "../game/link.h"
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace play {

using Datagram = std::vector<uint8_t>;

struct Line {
    virtual ~Line() = default;
    virtual void send(const Datagram& d) = 0;
    virtual bool receive(Datagram& d) = 0; // false: nothing has arrived
    virtual void tick() {}                  // once a frame
    virtual void forget() {}                // a host: the machine it heard is gone, or was no guest; the next caller is taken for one
};

// ---- a line inside one program ----------------------------------------------------------------
// Two ends that share a wire. A datagram sent at one end comes out at the other `delay` ticks
// later, give or take `jitter`; of a hundred, `loss` never arrive. Later ones may overtake.
struct FakeWire {
    struct Held {
        int64_t due;
        Datagram data;
    };
    std::deque<Held> toward[2];
    juraic::Rng rng{1};
    int delay = 0, jitter = 0, loss = 0;
    int64_t now = 0;
    long lost = 0; // the datagrams that never arrived (for tests)
};
struct FakeLine : Line {
    std::shared_ptr<FakeWire> wire;
    int end = 0;
    FakeLine(std::shared_ptr<FakeWire> w, int e) : wire(std::move(w)), end(e) {}
    void send(const Datagram& d) override {
        FakeWire& w = *wire;
        if (w.loss > 0 && int(w.rng.below(100)) < w.loss) {
            ++w.lost;
            return;
        }
        const int late = w.delay + (w.jitter > 0 ? int(w.rng.below(uint64_t(2 * w.jitter + 1))) - w.jitter : 0);
        w.toward[1 - end].push_back(FakeWire::Held{w.now + (late < 0 ? 0 : late), d});
    }
    bool receive(Datagram& d) override {
        std::deque<FakeWire::Held>& q = wire->toward[end];
        for (size_t i = 0; i < q.size(); ++i)
            if (q[i].due <= wire->now) {
                d = std::move(q[i].data);
                q.erase(q.begin() + std::ptrdiff_t(i));
                return true;
            }
        return false;
    }
    // (the wire has one clock: the end 0 moves it)
    void tick() override {
        if (end == 0) ++wire->now;
    }
};

// ---- what is said ---------------------------------------------------------------------------
constexpr uint8_t NET_VERSION = 1;
enum NetKind : uint8_t {
    NET_HELLO = 1, // guest: I am here (my name, the version of my game's frames)
    NET_WELCOME,   // host: the match (its number, seed, delay, rules, the names, the round trip of the line)
    NET_REFUSE,    // host: no match (why)
    NET_PLAY,      // both: the state of my end, and the buttons of my seat
    NET_BYE,       // both: I am leaving
    NET_PING,      // both: a clock, sent back as NET_PONG
    NET_PONG,
};

struct Writer {
    Datagram d;
    explicit Writer(NetKind kind) { d = {'J', 'F', NET_VERSION, uint8_t(kind)}; }
    Writer& u8(uint8_t v) { return d.push_back(v), *this; }
    Writer& u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) d.push_back(uint8_t(v >> (8 * i)));
        return *this;
    }
    Writer& u64(uint64_t v) { return u32(uint32_t(v)).u32(uint32_t(v >> 32)); }
    Writer& text(const std::string& s) {
        u32(uint32_t(s.size()));
        d.insert(d.end(), s.begin(), s.end());
        return *this;
    }
};
struct Reader {
    const Datagram& d;
    size_t at = 4;
    bool ok = true;
    explicit Reader(const Datagram& data) : d(data) { ok = d.size() >= 4 && d[0] == 'J' && d[1] == 'F' && d[2] == NET_VERSION; }
    uint8_t kind() const { return ok ? d[3] : uint8_t(0); }
    uint8_t u8() {
        if (at + 1 > d.size()) return ok = false, uint8_t(0);
        return d[at++];
    }
    uint32_t u32() {
        if (at + 4 > d.size()) return ok = false, 0u;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= uint32_t(d[at + size_t(i)]) << (8 * i);
        at += 4;
        return v;
    }
    uint64_t u64() {
        const uint64_t low = u32();
        return low | uint64_t(u32()) << 32;
    }
    std::string text(size_t most = 4096) {
        const uint32_t n = u32();
        if (!ok || n > most || at + n > d.size()) return ok = false, std::string();
        std::string s(d.begin() + std::ptrdiff_t(at), d.begin() + std::ptrdiff_t(at + n));
        at += n;
        return s;
    }
};

// One seat after one of its frames, as a number: the two machines compare them to notice a round
// that has gone two ways (it must not happen; if it does, the match is stopped rather than played on).
inline uint64_t seatStamp(const juraic::Player& p) {
    uint64_t h = uint64_t(juraic::KeyHash()(p.field.f.key()));
    const auto mix = [&h](uint64_t v) { h = (h ^ v) * 0x9E3779B97F4A7C15ull, h ^= h >> 29; };
    for (int x = 0; x < juraic::W; ++x) mix(uint64_t(p.field.row14[size_t(x)]));
    mix(uint64_t(p.scoring.score)), mix(uint64_t(p.scoring.pool)), mix(uint64_t(p.incoming.pending)), mix(uint64_t(p.incoming.confirmed));
    mix(uint64_t(p.phase)), mix(uint64_t(p.wait)), mix(uint64_t(p.moves));
    return h;
}

// The match as the host sets it.
struct NetMatch {
    uint32_t id = 0;     // told apart from an earlier match of the same two
    uint64_t seed = 0;   // of the match: the seeds of its rounds come from it
    int delay = 2;       // frames (game/link.h)
    std::string rules;   // "name=value" lines, as a replay has them
    std::string name[2]; // host, guest
};

// The delay of a match for a line with this round trip, in frames (ticks are frames): the way there, the
// swing seen on it, and two frames to spare - never under 2, never over what a round allows.
inline int delayFor(int roundTrip, int roundTripMax) {
    const int way = (roundTrip + 1) / 2, swing = roundTripMax > roundTrip ? (roundTripMax - roundTrip + 1) / 2 : 0;
    const int delay = way + swing + 2;
    return delay < 2 ? 2 : delay > juraic::MAX_LINK_DELAY ? juraic::MAX_LINK_DELAY : delay;
}
// A delay as a round takes it (game/link.h, LinkedRound::start). What a match is given - by the host's settings,
// or by the host's word to the guest - is brought into that range before anything is played with it or written
// into a replay.
inline int heldDelay(uint32_t said) { return said < 1 ? 1 : said > uint32_t(juraic::MAX_LINK_DELAY) ? juraic::MAX_LINK_DELAY : int(said); }

// One end of a match. The owner (the game, or a test) drives it once a frame:
//   peer.tick();                 // reads what has arrived, sends what is due
// and plays the rounds itself with playArrived and playOwn below, through the buttons kept here.
struct NetPeer {
    enum State { Idle, Calling, Waiting, Measuring, Joined, Lost, Refused, Left, Apart } state = Idle;
    // Calling: the guest calls the host; Waiting: the host waits for a guest; Measuring: the host has heard a
    // guest and takes the time of the line for a moment, to set the delay by it; Joined: the match is on;
    // Lost: nothing has arrived for too long; Refused: the host said no (why: `reason`);
    // Left: the other end has left; Apart: the two machines disagree about a round.
    std::unique_ptr<Line> line;
    bool host = false;
    int seat = 0; // the own seat: the host is 1P, the guest 2P
    NetMatch match;
    std::string ownName, reason;
    int timingVersion = 0;
    int64_t ticks = 0, heardAt = 0, sentHelloAt = -1000, measuredFrom = 0;
    int fixedDelay = 0;      // the host: the delay of the match if above 0, whatever the line (0: by the line, delayFor)
    bool hisPlaying = false; // a NET_PLAY of the other end has arrived: it has the match
    // The clock of the line: the ticks a datagram takes there and back, smoothed, and the longest of late.
    int roundTrip = -1, roundTripMax = 0;
    int64_t pingAt = -1000;
    int silenceLimit = 60 * 8; // ticks without a datagram after which the line counts as lost
    // A round trip the other end names (a host in its welcome), as one a line can have: a line slower than the
    // silence allowed is lost anyway.
    int heldTrip(uint32_t said) const { return silenceLimit > 0 && said > uint32_t(silenceLimit) ? silenceLimit : int(said & 0x7FFFFFFFu); }

    // The round both ends are in, and the buttons of both seats in it (own: as played here; other: as arrived).
    uint32_t round = 0;            // counted on through the match and the matches after it
    std::vector<uint8_t> buttons[2];
    size_t acked = 0;              // the frames of the own seat the other end has
    bool hisAgain = false; // the other end wants another match (said at the end of one)
    bool again = false;    // so does this end
    uint32_t matches = 0;  // the matches begun: `again` and `hisAgain` are of this one
    // Stamps of the seats for comparing (seatStamp), of the frames that are multiples of STAMP_EVERY.
    static constexpr int STAMP_EVERY = 64, STAMPS = 32;
    struct Stamp {
        uint32_t round = ~0u;
        int64_t frame = -1;
        uint64_t value = 0;
    };
    Stamp stamps[2][STAMPS], newest[2];

    // (delay 0: the host sets it by the time the line takes)
    void wait(std::unique_ptr<Line> l, const NetMatch& m, const std::string& name, int version, int delay = 0) {
        *this = NetPeer{};
        line = std::move(l), host = true, seat = 0, match = m, ownName = name, timingVersion = version, state = Waiting;
        match.name[0] = name, fixedDelay = delay;
    }
    void call(std::unique_ptr<Line> l, const std::string& name, int version) {
        *this = NetPeer{};
        line = std::move(l), host = false, seat = 1, ownName = name, timingVersion = version, state = Calling;
    }
    void leave() {
        if (line && (state == Joined || state == Measuring))
            for (int i = 0; i < 3; ++i) line->send(Writer(NET_BYE).u32(match.id).d);
        state = Idle;
        line.reset();
    }

    // The next round begins (both ends count the rounds alike).
    void startRound() {
        ++round;
        buttons[0].clear(), buttons[1].clear();
        acked = 0;
    }
    // The next match begins: both ends have asked for it.
    void startMatch() {
        ++matches;
        again = hisAgain = false;
    }
    // The own seat has played a frame with these buttons.
    void played(uint8_t b) { buttons[seat].push_back(b); }
    // A seat has played `frame` (counted from 0) and stands so: kept for comparing, if it is a frame that is compared.
    void stamp(int s, int64_t frame, const juraic::Player& p) {
        if (frame % STAMP_EVERY) return;
        Stamp mine{round, frame, seatStamp(p)};
        stamps[s][size_t(frame / STAMP_EVERY) % STAMPS] = newest[s] = mine;
    }

    void tick() {
        if (!line) return;
        line->tick();
        ++ticks;
        Datagram d;
        while (line->receive(d)) {
            read(d);
            // (a host that still waits has heard no guest: the line does not hold on to whoever sent this - a
            //  caller that was refused, or something that is no call at all, must not keep the next caller out)
            if (state == Waiting) line->forget();
        }
        if (state == Calling && ticks - sentHelloAt >= 10) {
            sentHelloAt = ticks;
            line->send(Writer(NET_HELLO).u32(uint32_t(timingVersion)).text(ownName).d);
        }
        if (state == Measuring) {
            // (half a second of clocks there and back, one every other frame; then the match is set)
            if (ticks % 2 == 0) line->send(Writer(NET_PING).u64(uint64_t(ticks)).d);
            if (ticks - measuredFrom >= 30 && roundTrip >= 0) {
                match.delay = fixedDelay > 0 ? heldDelay(uint32_t(fixedDelay)) : delayFor(roundTrip, roundTripMax);
                state = Joined, heardAt = ticks;
                sendWelcome();
            } else if (ticks - measuredFrom > silenceLimit) {
                state = Waiting, line->forget(); // (the guest has gone again)
            }
        }
        if (state == Joined) {
            sendPlay();
            if (ticks - pingAt >= 30) pingAt = ticks, line->send(Writer(NET_PING).u64(uint64_t(ticks)).d);
            if (ticks - heardAt > silenceLimit) state = Lost;
        }
    }

private:
    void sendPlay() {
        Writer w(NET_PLAY);
        w.u32(match.id).u32(matches).u8(again ? 1 : 0).u32(round);
        w.u32(uint32_t(buttons[1 - seat].size())); // the frames of your seat I have
        // the frames of my seat you do not have yet (never more than fit a datagram comfortably)
        const size_t from = acked < buttons[seat].size() ? acked : buttons[seat].size();
        const size_t count = buttons[seat].size() - from < 600 ? buttons[seat].size() - from : 600;
        w.u32(uint32_t(from)).u32(uint32_t(count));
        for (size_t i = 0; i < count; ++i) w.u8(buttons[seat][from + i]);
        for (int s = 0; s < 2; ++s) w.u32(newest[s].round).u64(uint64_t(newest[s].frame)).u64(newest[s].value);
        line->send(w.d);
    }
    void sendWelcome() {
        line->send(Writer(NET_WELCOME).u32(match.id).u64(match.seed).u32(uint32_t(match.delay)).text(match.rules).text(match.name[0]).text(match.name[1]).u32(uint32_t(roundTrip < 0 ? 0 : roundTrip)).d);
    }
    void read(const Datagram& d) {
        Reader r(d);
        if (!r.ok) return;
        switch (r.kind()) {
        case NET_HELLO: {
            const int version = int(r.u32());
            const std::string name = r.text(64);
            if (!r.ok || !host) return;
            if (version != timingVersion) {
                line->send(Writer(NET_REFUSE).text("version").d);
                return;
            }
            if (state == Waiting) match.name[1] = name, state = Measuring, measuredFrom = ticks, roundTrip = -1, roundTripMax = 0;
            if (state == Joined && !hisPlaying) sendWelcome(); // (again, until the guest's first NET_PLAY shows that it has arrived)
            return;
        }
        case NET_WELCOME: {
            NetMatch m;
            m.id = r.u32(), m.seed = r.u64(), m.delay = heldDelay(r.u32()), m.rules = r.text(), m.name[0] = r.text(64), m.name[1] = r.text(64);
            const int trip = heldTrip(r.u32());
            if (!r.ok || host || state != Calling) return;
            match = m, state = Joined, heardAt = ticks, roundTrip = trip, roundTripMax = trip;
            return;
        }
        case NET_REFUSE:
            if (!host && state == Calling) reason = r.text(64), state = Refused;
            return;
        case NET_BYE:
            if (state == Joined && r.u32() == match.id && r.ok) state = Left;
            return;
        case NET_PING:
            if (state == Joined || state == Calling) line->send(Writer(NET_PONG).u64(r.u64()).d);
            return;
        case NET_PONG: {
            // (a clock this end sent, come back: any other number - one from the future, one older than a line may
            //  be silent, one that is no time at all - is none of this line's, whoever sends it)
            const uint64_t sent = r.u64(), now = uint64_t(ticks);
            if (!r.ok || (state != Joined && state != Measuring) || sent > now || now - sent > uint64_t(silenceLimit > 0 ? silenceLimit : 0)) return;
            const int trip = int(now - sent);
            roundTrip = roundTrip < 0 ? trip : int((int64_t(roundTrip) * 7 + trip + 4) / 8);
            roundTripMax = trip > roundTripMax ? trip : roundTripMax > 0 && ticks % 600 == 0 ? roundTripMax - 1 : roundTripMax;
            return;
        }
        case NET_PLAY: {
            if (state != Joined || r.u32() != match.id) return;
            const uint32_t hisMatch = r.u32();
            const bool wants = r.u8() != 0;
            const uint32_t theirRound = r.u32(), has = r.u32(), from = r.u32(), count = r.u32();
            if (!r.ok || count > 600 || r.at + count > d.size()) return;
            heardAt = ticks, hisPlaying = true;
            // (an end that has already begun the next match had this end's word for it and its own)
            if (hisMatch == matches) hisAgain = wants;
            else if (hisMatch > matches) hisAgain = true;
            if (theirRound == round) {
                if (has > acked && has <= buttons[seat].size()) acked = has;
                std::vector<uint8_t>& his = buttons[1 - seat];
                // (frames that follow on what is here; a datagram that begins later has overtaken one that will still come, or was sent after a loss: the next brings them.
                //  And no frame further ahead of the own seat than a round lets a seat play, game/link.h canPlay: an end that sends more is not playing the game, and what
                //  it sends is not kept. Sent again when the own seat has gone on, it is taken then.)
                const size_t most = buttons[seat].size() + size_t(juraic::MAX_LINK_DELAY);
                for (uint32_t i = 0; i < count; ++i)
                    if (size_t(from) + i == his.size() && his.size() < most) his.push_back(d[r.at + i]);
            }
            r.at += count;
            for (int s = 0; s < 2; ++s) {
                Stamp theirs;
                theirs.round = r.u32(), theirs.frame = int64_t(r.u64()), theirs.value = r.u64();
                if (!r.ok || theirs.frame < 0) continue;
                const Stamp& mine = stamps[s][size_t(theirs.frame / STAMP_EVERY) % STAMPS];
                if (mine.round == theirs.round && mine.frame == theirs.frame && mine.value != theirs.value) state = Apart;
            }
            return;
        }
        default: return;
        }
    }
};

// Plays the other seat of a round as far as its buttons have arrived and the round lets it go.
// each(seat, events) after every frame played.
template <class Each> void playArrived(juraic::LinkedRound& link, NetPeer& peer, Each&& each) {
    const int other = 1 - peer.seat;
    juraic::PlayerEvents e;
    while (size_t(link.played[other]) < peer.buttons[other].size() && link.play(other, peer.buttons[other][size_t(link.played[other])], e)) {
        peer.stamp(other, link.played[other] - 1, link.round.player[other]);
        each(other, e);
    }
}
// Plays the next frame of the own seat with these buttons. False: it has to wait for the other seat (or the round is over).
template <class Each> bool playOwn(juraic::LinkedRound& link, NetPeer& peer, uint8_t buttons, Each&& each) {
    juraic::PlayerEvents e;
    if (!link.play(peer.seat, buttons, e)) return false;
    peer.played(buttons);
    peer.stamp(peer.seat, link.played[peer.seat] - 1, link.round.player[peer.seat]);
    each(peer.seat, e);
    return true;
}

} // namespace play
