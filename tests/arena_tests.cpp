// Tests of what a player's machine takes from a server of Juraic Arena (play/arena/, play/arena_play.h), with sockets
// of this machine alone: the server is played here, at 127.0.0.1, so nothing leaves the machine.
//   - a match whose seat, rules or pairs are out of range is not followed
//   - the buttons of a seat that come are bounded by the frames of a round, and none are taken after its end
//   - what a connection keeps of what has come is bounded, also the lines a watcher holds back
//   - the frames that came before the end of a round are played all the same
//   - a connection the server has closed is closed
//   - the server is called on a thread of its own: the game does not wait for it
#include "../play/arena_play.h"
#include <chrono>
#include <cstdio>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace play;

static void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
static void rest(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
// The most a step of these tests waits (each goes on as soon as it can: the limit is for a machine under load).
constexpr int WAIT_MS = 10000;
// Waits up to `ms` milliseconds for `done`, calling `each` in between.
static bool until(const std::function<bool()>& done, int ms, const std::function<void()>& each = [] {}) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!done()) {
        if (std::chrono::steady_clock::now() > end) return false;
        each();
        rest(1);
    }
    return true;
}

// A server of this machine: listens at 127.0.0.1 on a port that is free (a few are tried) and takes one caller.
struct LocalServer {
    arena::Socket listener = arena::NO_SOCKET;
    int port = 0;
    arena::Line line;
    LocalServer() {
        ArenaPlay::startSockets();
        for (int p = 50841; p < 50861 && listener == arena::NO_SOCKET; ++p) listener = arena::listenOn("127.0.0.1", p), port = p;
        check(listener != arena::NO_SOCKET, "no port of this machine could be listened on (50841 to 50860, at 127.0.0.1)");
    }
    ~LocalServer() { arena::closeSocket(listener); }
    LocalServer(const LocalServer&) = delete;
    LocalServer& operator=(const LocalServer&) = delete;
    void accept() {
        arena::Socket s = arena::NO_SOCKET;
        check(until([&] { return (s = arena::acceptFrom(listener)) != arena::NO_SOCKET; }, WAIT_MS), "the caller did not come");
        line.s = s, line.closed = false, line.in.clear(), line.out.clear();
    }
    void say(const std::string& text) {
        line.send(text);
        line.pump();
    }
    // The next line the caller sent (`each` meanwhile).
    std::string heard(const std::function<void()>& each = [] {}) {
        std::string text;
        check(until([&] { line.pump(); return line.nextLine(text); }, WAIT_MS, each), "the caller sent nothing");
        return text;
    }
};

static const char* const MATCH_LINE = "MATCH m1 %d a person 1500 350 b engine 1500 350 2 x";
static std::string matchLine(int seat) {
    char text[128];
    std::snprintf(text, sizeof text, MATCH_LINE, seat);
    return text;
}
static std::string frames(int n, uint8_t b = 0) { return arena::toHex(std::vector<uint8_t>(size_t(n), b)); }

static void testOutOfRange() {
    using arena::words;
    arena::MatchView m;
    m.take(words(matchLine(2)));
    check(!m.on, "a match with the own seat 2 is followed");
    m.take(words(matchLine(-1)));
    check(m.on && m.seat == -1, "a match watched is not followed");
    m.take(words(matchLine(0)));
    m.take(words("RULES targetPoints=0 speedPercent=2147483647 roundFrameLimit=-1"));
    check(!m.on && m.rules.rules.targetPoints == 70 && m.rules.rules.speedPercent == 100 && m.rules.roundFrameLimit == 36000,
          "rules out of range are taken: targetPoints " + std::to_string(m.rules.rules.targetPoints) + ", speedPercent " + std::to_string(m.rules.rules.speedPercent) +
              ", roundFrameLimit " + std::to_string(m.rules.roundFrameLimit));
    // (a value that does not fit in an int is out of range, not cut to one that does)
    m.take(words(matchLine(0)));
    m.take(words("RULES colors=4294967300"));
    check(!m.on && m.rules.rules.colors == 4, "a number of colours too large for an int is taken as " + std::to_string(m.rules.rules.colors));
    m.take(words(matchLine(0)));
    m.take(words("RULES " + arena::rulesText(arena::ArenaRules{})));
    m.take(words("ROUND 1 77 c"));
    m.take(words("T 1 0 12"));
    check(m.on && m.round.known == 1, "a pair of colours in range is not taken");
    m.take(words("T 1 1 19"));
    check(!m.on && m.round.known <= 1, "a pair of colour 9 is taken");
    std::cout << "a match whose seat, rules or pairs are out of range is not followed\n";
}

static void testButtonsBounded() {
    using arena::words;
    arena::MatchView m;
    m.take(words(matchLine(0)));
    arena::ArenaRules r;
    r.roundFrameLimit = 100;
    m.take(words("RULES " + arena::rulesText(r)));
    m.take(words("ROUND 1 77 c"));
    m.take(words("B 1 1 0 " + frames(60)));
    m.take(words("B 1 1 60 " + frames(120)));
    check(m.buttons[1].size() == 60, std::to_string(m.buttons[1].size()) + " frames of buttons taken in a round of 100 frames");
    // (a seat may play beyond the end of a round: as many frames as the server takes)
    m.take(words("B 1 1 60 " + frames(int(100 + arena::MatchView::MORE_FRAMES - 60))));
    check(m.buttons[1].size() == size_t(100 + arena::MatchView::MORE_FRAMES), "the frames a seat plays beyond the end of a round are not taken");
    m.take(words("END 1 draw time 100 0 0"));
    const size_t before = m.buttons[1].size();
    m.take(words("B 1 1 " + std::to_string(before) + " " + frames(10)));
    check(m.buttons[1].size() == before, "buttons taken after the end of the round");
    std::cout << "the buttons of a seat are bounded by the frames of a round, none taken after its end\n";
}

static void testLineBounded() {
    LocalServer server;
    arena::Line client;
    check(arena::connectTo("127.0.0.1", server.port, client), "no connection to the server of this machine");
    server.accept();
    // (one line longer than a connection keeps, sent as fast as it goes)
    const std::string chunk(1 << 16, 'x');
    size_t sent = 0, most = 0;
    for (int k = 0; k < 3000 && sent < (6u << 20); ++k) {
        if (server.line.out.size() < (1u << 20)) server.line.out += chunk, sent += chunk.size();
        server.line.pump();
        client.pump();
        most = std::max(most, client.in.size());
    }
    check(client.closed && most <= arena::Line::MOST_IN + chunk.size(),
          "a connection kept " + std::to_string(most) + " bytes of a line longer than " + std::to_string(arena::Line::MOST_IN) + " (" + std::to_string(sent) + " sent)");
    std::cout << "a connection keeps at most " << arena::Line::MOST_IN << " bytes of what has come\n";
}

static void testHeldBounded() {
    LocalServer server;
    ArenaPlay a;
    a.watch("127.0.0.1", server.port);
    server.accept();
    const auto tick = [&] { a.tick(0, [](int, const PlayerEvents&) {}); };
    check(server.heard(tick) == "WATCH auto", "the watcher did not ask to watch");
    // (lines a watcher holds back for its lag, from a server that does not stop)
    const std::string line = "X " + std::string(60000, 'y');
    size_t sent = 0, most = 0;
    for (int k = 0; k < 3000 && sent < (6u << 20) && a.stage == ArenaPlay::Watching; ++k) {
        if (server.line.out.size() < (1u << 20)) server.line.send(line), sent += line.size() + 1;
        server.line.pump();
        tick();
        size_t held = 0;
        for (const auto& [at, text] : a.held) held += text.size();
        most = std::max(most, held);
    }
    check(most <= arena::Line::MOST_IN, "a watcher held back " + std::to_string(most) + " bytes of lines (" + std::to_string(sent) + " sent)");
    std::cout << "a watcher holds back at most " << arena::Line::MOST_IN << " bytes of lines\n";
}

static void testFramesBeforeEnd() {
    LocalServer server;
    ArenaPlay a;
    a.watch("127.0.0.1", server.port);
    server.accept();
    int played = 0;
    const auto tick = [&] { a.tick(0, [&](int, const PlayerEvents&) { ++played; }); };
    check(server.heard(tick) == "WATCH auto", "the watcher did not ask to watch");
    server.say(matchLine(-1));
    server.say("RULES " + arena::rulesText(arena::ArenaRules{}));
    server.say("ROUND 1 77 c");
    for (int i = 0; i < 4; ++i) server.say("T 1 " + std::to_string(i) + " 12");
    server.say("B 1 0 0 " + frames(20));
    server.say("B 1 1 0 " + frames(20));
    server.say("END 1 draw left 20 0 0");
    // (a watcher takes the lines WATCH_LAG_MS after they came: all of them in one frame here)
    until([&] { return played >= 40; }, int(ArenaPlay::WATCH_LAG_MS) + WAIT_MS, tick);
    check(played == 40, "of 40 frames that came before the end of the round, " + std::to_string(played) + " played");
    std::cout << "the frames that came before the end of a round are played\n";
}

static void testClosedByServer() {
    LocalServer server;
    ArenaPlay a;
    a.play("127.0.0.1", server.port, "someone", "token");
    server.accept();
    const auto tick = [&] { a.tick(0, [](int, const PlayerEvents&) {}); };
    check(server.heard(tick).rfind("LOGIN someone token ", 0) == 0, "the player did not log in");
    server.line.close();
    check(until([&] { return a.stage == ArenaPlay::Over; }, WAIT_MS, tick), "a connection closed by the server is taken as open");
    check(a.line.s == arena::NO_SOCKET, "the socket of a connection the server closed is kept open");
    a.leave();
    check(a.line.s == arena::NO_SOCKET && a.stage == ArenaPlay::Off, "a connection left is not closed");
    std::cout << "a connection the server closed is closed\n";
}

static void testCalledAside() {
    LocalServer server;
    ArenaPlay a;
    a.play("127.0.0.1", server.port, "someone", "token");
    check(a.stage == ArenaPlay::Connecting && !a.line.open(), "the server was called before play returned: the game waited for it");
    server.accept();
    const auto tick = [&] { a.tick(0, [](int, const PlayerEvents&) {}); };
    check(server.heard(tick).rfind("LOGIN someone token ", 0) == 0, "the player did not log in once the call came");
    check(a.line.open(), "the connection is not open once the server heard the player");
    // Left while the call is on its way: at once.
    ArenaPlay b;
    b.play("127.0.0.1", server.port, "other", "token");
    const auto before = std::chrono::steady_clock::now();
    b.leave();
    check(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(1000) && b.stage == ArenaPlay::Off, "leaving while the server is called waits");
    // No server: the game says so.
    ArenaPlay c;
    arena::Socket none = arena::listenOn("127.0.0.1", server.port + 1);
    const int port = server.port + 1;
    arena::closeSocket(none); // (a port no one listens on)
    c.play("127.0.0.1", port, "someone", "token");
    check(until([&] { return c.stage == ArenaPlay::Over; }, WAIT_MS + 5000, [&] { c.tick(0, [](int, const PlayerEvents&) {}); }) && !c.over.empty(),
          "a server that is not there is not told");
    std::cout << "the server is called on a thread of its own\n";
}

int main() {
    int failed = 0;
    for (void (*test)() : {testOutOfRange, testButtonsBounded, testLineBounded, testHeldBounded, testFramesBeforeEnd, testClosedByServer, testCalledAside}) {
        try {
            test();
        } catch (const std::exception& e) {
            std::cout << "FAILED: " << e.what() << std::endl;
            ++failed;
        }
    }
    if (failed) return 1;
    std::cout << "all arena tests passed\n";
    return 0;
}
