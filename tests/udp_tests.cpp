// Tests of the UDP line (play/udp.h) with sockets of this machine alone: the host listens at ::1, so nothing
// leaves the machine and no other machine can call.
//   - a datagram longer than anything said here is passed over, and the one behind it is read by the same call
//   - a host that waits is not held by a caller it cannot use: after one of another version, and after something
//     that is no call at all, the next caller joins
//   - once two ends have joined, a third socket is passed over, whatever it sends
//   - what one frame takes from the socket is bounded, however much has been sent to it
#include "../play/udp.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace play;

static void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
static void rest(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// A host line on a port that is free (a few are tried), at ::1.
static std::unique_ptr<UdpLine> listenSomewhere(int& port) {
    for (port = 50811; port < 50831; ++port) {
        auto line = std::make_unique<UdpLine>();
        if (line->listen(port, 1)) return line;
    }
    throw std::runtime_error("no port of this machine could be listened on (50811 to 50830, at ::1)");
}
static std::unique_ptr<UdpLine> callTo(int port) {
    auto line = std::make_unique<UdpLine>();
    check(line->call("::1", port), "a socket could not be made to call ::1");
    return line;
}

static void testLongDatagram() {
    int port = 0;
    const std::unique_ptr<UdpLine> host = listenSomewhere(port);
    const std::unique_ptr<UdpLine> caller = callTo(port);
    Datagram longOne(3000, uint8_t(0));
    longOne[0] = 'J', longOne[1] = 'F';
    for (int i = 0; i < 3; ++i) caller->send(longOne);
    const Datagram word = Writer(NET_PING).u64(77).d;
    caller->send(word);
    rest(200);
    Datagram got;
    check(host->receive(got), "a datagram behind three that are too long was not read by the same call");
    check(got == word, "what was read behind the long datagrams is not what was sent");
    check(!host->receive(got), "more was read than was sent");
    std::cout << "udp: datagrams too long to be of this game are passed over (port " << port << ")\n";
}

// Both ends ticked until `done` says so (a millisecond between the ticks; five seconds at most).
template <class Done> static bool tickUntil(NetPeer& a, NetPeer& b, Done&& done) {
    for (int i = 0; i < 5000; ++i) {
        a.tick(), b.tick();
        if (done()) return true;
        rest(1);
    }
    return false;
}

static void testCallers() {
    for (int way = 0; way < 2; ++way) {
        const std::string tag = way == 0 ? " (after a caller of another version)" : " (after something that is no call)";
        int port = 0;
        NetPeer host, stranger, guest;
        NetMatch m;
        m.id = 21;
        host.wait(listenSomewhere(port), m, "h", 12);
        if (way == 0) {
            stranger.call(callTo(port), "old", 11);
            check(tickUntil(host, stranger, [&] { return stranger.state == NetPeer::Refused; }), "a caller of another version was not refused over UDP");
        } else {
            const std::unique_ptr<UdpLine> noise = callTo(port);
            noise->send(Writer(NET_PING).u64(1).d);
            noise->send(Datagram{'J', 'F', uint8_t(NET_VERSION + 1), uint8_t(NET_HELLO)});
            rest(100);
            for (int i = 0; i < 20; ++i) host.tick();
        }
        check(host.state == NetPeer::Waiting, "the host does not wait any more" + tag);
        guest.call(callTo(port), "g", 12);
        check(tickUntil(host, guest, [&] { return host.state == NetPeer::Joined && guest.state == NetPeer::Joined && host.hisPlaying; }),
              "the next caller did not join" + tag);
        check(host.match.name[1] == "g" && guest.match.id == m.id, "the match is not that of the host and the next caller" + tag);
        // (a third socket: what would end the match if it came from the guest)
        const std::unique_ptr<UdpLine> third = callTo(port);
        for (int i = 0; i < 3; ++i) third->send(Writer(NET_BYE).u32(m.id).d);
        third->send(Writer(NET_HELLO).u32(12).text("x").d);
        rest(100);
        for (int i = 0; i < 60; ++i) host.tick(), guest.tick(), rest(1);
        check(host.state == NetPeer::Joined && guest.state == NetPeer::Joined && host.match.name[1] == "g", "a third socket disturbed the match" + tag);
        guest.leave();
        check(tickUntil(host, guest, [&] { return host.state == NetPeer::Left; }), "the host did not notice that the guest left over UDP");
    }
    std::cout << "udp: a host hears the next caller after one it refused and after one that did not call; a third socket does not disturb a match\n";
}

// A machine that sends datagrams by the thousand - anybody can, to an open port - must not keep the game in the
// loop that reads them: only so many are taken from the socket in a frame, the others wait for the next.
static void testFlood() {
    int port = 0;
    const std::unique_ptr<UdpLine> host = listenSomewhere(port);
    const std::unique_ptr<UdpLine> guest = callTo(port), stranger = callTo(port);
    const Datagram first = Writer(NET_PING).u64(1).d, second = Writer(NET_PING).u64(2).d, noise = Writer(NET_PING).u64(3).d;
    guest->send(first);
    rest(100);
    Datagram got;
    host->tick();
    check(host->receive(got) && got == first && !host->receive(got), "the first datagram of a guest was not read");
    for (int i = 0; i < 400; ++i) stranger->send(noise);
    rest(100);
    guest->send(second);
    rest(200);
    host->tick();
    check(!host->receive(got), "a datagram behind 400 of another machine came out in the same frame: nothing bounds what a frame reads");
    int frames = 1;
    bool came = false;
    for (; frames < 10 && !came; ++frames) host->tick(), came = host->receive(got);
    check(came && got == second && frames <= 3, "the datagram behind the flood did not come out in the next frames (" + std::to_string(frames) + ")");
    std::cout << "udp: a frame takes " << UdpLine::MOST_A_FRAME << " datagrams from the socket at most; what a guest sent behind 400 of another machine comes out a frame later\n";
}

int main() {
    try {
        testLongDatagram();
        testCallers();
        testFlood();
        std::cout << "udp tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
