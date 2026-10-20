#pragma once
// TCP with lines: the one line every program of the arena speaks (docs/PROTOCOL.md). Sockets do not block;
// whoever owns them calls pump() now and then, takes the lines that have come with nextLine() and puts
// lines to send with send().
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace arena {

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket NO_SOCKET = INVALID_SOCKET;
inline void closeSocket(Socket s) { closesocket(s); }
inline bool wouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
#else
using Socket = int;
constexpr Socket NO_SOCKET = -1;
inline void closeSocket(Socket s) { close(s); }
inline bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }
#endif

struct NetStart {
    NetStart() {
#ifdef _WIN32
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
#endif
    }
    ~NetStart() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

inline void noBlockNoDelay(Socket s) {
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
}

inline int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/// One connection. It owns its socket: closed when the line is closed, given another socket or gone.
struct Line {
    Socket s = NO_SOCKET;
    std::string in, out; // what has come (from inAt on: not taken as lines yet), what is to go (from outAt on)
    size_t inAt = 0, outAt = 0;
    bool closed = false;
    int64_t heardAt = 0; // nowMs() of the last bytes that came
    static constexpr size_t MOST_IN = 1 << 22; // more than this come and not taken as lines closes the connection

    Line() = default;
    Line(const Line&) = delete;
    Line& operator=(const Line&) = delete;
    Line(Line&& o) noexcept { take(o); }
    Line& operator=(Line&& o) noexcept {
        if (this != &o) drop(), take(o);
        return *this;
    }
    ~Line() { drop(); }

    bool open() const { return s != NO_SOCKET && !closed; }
    void send(const std::string& line) {
        if (open()) out += line, out += '\n';
    }
    // Reads what has come and writes what can go. False once the connection is gone (nothing is read after).
    bool pump() {
        if (!open()) return false;
        if (inAt) in.erase(0, inAt), inAt = 0; // (the lines taken since the last time, at once)
        char buf[65536];
        for (;;) {
            const int n = int(recv(s, buf, sizeof buf, 0));
            if (n > 0) {
                in.append(buf, size_t(n)), heardAt = nowMs();
                if (in.size() > MOST_IN) return closed = true, false;
                continue;
            }
            if (n == 0 || !wouldBlock()) closed = true;
            break;
        }
        while (outAt < out.size() && !closed) {
            const size_t left = out.size() - outAt;
            const int n = int(::send(s, out.data() + outAt, int(left > 1 << 20 ? 1 << 20 : left), 0));
            if (n > 0) {
                outAt += size_t(n);
                continue;
            }
            if (!wouldBlock()) closed = true;
            break;
        }
        if (outAt == out.size() || outAt > 1 << 20) out.erase(0, outAt), outAt = 0;
        return !closed;
    }
    bool nextLine(std::string& line) {
        const size_t nl = in.find('\n', inAt);
        if (nl == std::string::npos) return false;
        line.assign(in, inAt, nl - inAt);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        inAt = nl + 1;
        return true;
    }
    // Sends what is still to go, as far as it goes at once, and closes the socket.
    void close() {
        pump();
        drop();
    }

private:
    void drop() {
        if (s != NO_SOCKET) closeSocket(s);
        s = NO_SOCKET, closed = true;
    }
    void take(Line& o) {
        s = o.s, in = std::move(o.in), out = std::move(o.out), inAt = o.inAt, outAt = o.outAt, closed = o.closed, heardAt = o.heardAt;
        o.s = NO_SOCKET, o.closed = true, o.inAt = o.outAt = 0;
    }
};

// A socket connected to host:port (NO_SOCKET: none), not blocking once connected. The name is looked up and the connect
// waited for here, for as long as they take.
inline Socket connectSocket(const std::string& host, int port) {
    addrinfo hints{}, *list = nullptr;
    hints.ai_family = AF_UNSPEC, hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &list) != 0) return NO_SOCKET;
    Socket s = NO_SOCKET;
    for (addrinfo* a = list; a; a = a->ai_next) {
        s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == NO_SOCKET) continue;
        if (connect(s, a->ai_addr, int(a->ai_addrlen)) == 0) break;
        closeSocket(s), s = NO_SOCKET;
    }
    freeaddrinfo(list);
    if (s != NO_SOCKET) noBlockNoDelay(s);
    return s;
}
// Connects to host:port, waiting for it.
inline bool connectTo(const std::string& host, int port, Line& line) {
    line = Line{};
    line.s = connectSocket(host, port), line.heardAt = nowMs();
    return line.s != NO_SOCKET;
}

// A connection made on a thread of its own, so that the caller does not wait for the name to be looked up nor for the
// server to answer (a server that does not answer keeps a connect waiting for many seconds). Given up with the Call: a
// socket that comes after is closed by the thread.
class Call {
public:
    Call(const std::string& host, int port) : state(std::make_shared<State>()) {
        std::thread([s = state, host, port] {
            const Socket made = connectSocket(host, port);
            std::lock_guard<std::mutex> lock(s->m);
            if (s->given) {
                if (made != NO_SOCKET) closeSocket(made);
            } else {
                s->socket = made, s->done = true;
            }
        }).detach();
    }
    ~Call() {
        std::lock_guard<std::mutex> lock(state->m);
        state->given = true;
        if (state->socket != NO_SOCKET) closeSocket(state->socket);
    }
    Call(const Call&) = delete;
    Call& operator=(const Call&) = delete;
    // 0: still on its way; 1: connected, the socket given to `line`; -1: no connection.
    int take(Line& line) {
        std::lock_guard<std::mutex> lock(state->m);
        if (!state->done) return 0;
        if (state->socket == NO_SOCKET) return -1;
        line = Line{};
        line.s = state->socket, line.heardAt = nowMs(), state->socket = NO_SOCKET;
        return 1;
    }

private:
    struct State {
        std::mutex m;
        bool done = false, given = false;
        Socket socket = NO_SOCKET;
    };
    std::shared_ptr<State> state;
};

// Listens on address:port (IPv4; "127.0.0.1" = this machine only, "0.0.0.0" = every network of it).
inline Socket listenOn(const std::string& address, int port) {
    Socket s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == NO_SOCKET) return NO_SOCKET;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET, a.sin_port = htons(uint16_t(port));
    if (inet_pton(AF_INET, address.c_str(), &a.sin_addr) != 1 || bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || listen(s, 64) != 0) {
        closeSocket(s);
        return NO_SOCKET;
    }
    noBlockNoDelay(s);
    return s;
}

inline Socket acceptFrom(Socket listener) {
    sockaddr_storage a{};
#ifdef _WIN32
    int n = sizeof a;
#else
    socklen_t n = sizeof a;
#endif
    Socket s = accept(listener, reinterpret_cast<sockaddr*>(&a), &n);
    if (s != NO_SOCKET) noBlockNoDelay(s);
    return s;
}

// Waits until one of the sockets has something to read, or `ms` milliseconds have passed.
inline void waitFor(const std::vector<Socket>& sockets, int ms) {
    bool any = false;
    for (Socket s : sockets) any = any || s != NO_SOCKET;
    if (!any) { // (select with no socket at all returns at once on Windows)
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        return;
    }
    fd_set set;
    FD_ZERO(&set);
    Socket most = 0;
    for (Socket s : sockets)
        if (s != NO_SOCKET) {
            FD_SET(s, &set);
            most = s > most ? s : most;
        }
    timeval t{ms / 1000, (ms % 1000) * 1000};
    select(int(most + 1), &set, nullptr, nullptr, &t);
}

} // namespace arena
