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

// One connection.
struct Line {
    Socket s = NO_SOCKET;
    std::string in, out;
    bool closed = false;
    int64_t heardAt = 0; // nowMs() of the last bytes that came
    static constexpr size_t MOST_IN = 1 << 22; // a line longer than this closes the connection

    bool open() const { return s != NO_SOCKET && !closed; }
    void send(const std::string& line) {
        if (open()) out += line, out += '\n';
    }
    // Reads what has come and writes what can go. False once the connection is gone.
    bool pump() {
        if (s == NO_SOCKET) return false;
        char buf[65536];
        for (;;) {
            const int n = int(recv(s, buf, sizeof buf, 0));
            if (n > 0) {
                in.append(buf, size_t(n)), heardAt = nowMs();
                if (in.size() > MOST_IN) closed = true;
                continue;
            }
            if (n == 0 || !wouldBlock()) closed = true;
            break;
        }
        while (!out.empty() && !closed) {
            const int n = int(::send(s, out.data(), int(out.size() > 1 << 20 ? 1 << 20 : out.size()), 0));
            if (n > 0) {
                out.erase(0, size_t(n));
                continue;
            }
            if (!wouldBlock()) closed = true;
            break;
        }
        return !closed;
    }
    bool nextLine(std::string& line) {
        const size_t nl = in.find('\n');
        if (nl == std::string::npos) return false;
        line = in.substr(0, nl);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        in.erase(0, nl + 1);
        return true;
    }
    void close() {
        if (s != NO_SOCKET) {
            pump();
            closeSocket(s);
        }
        s = NO_SOCKET, closed = true;
    }
};

// Connects to host:port (blocking for the connect itself, then not blocking).
inline bool connectTo(const std::string& host, int port, Line& line) {
    addrinfo hints{}, *list = nullptr;
    hints.ai_family = AF_UNSPEC, hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &list) != 0) return false;
    Socket s = NO_SOCKET;
    for (addrinfo* a = list; a; a = a->ai_next) {
        s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == NO_SOCKET) continue;
        if (connect(s, a->ai_addr, int(a->ai_addrlen)) == 0) break;
        closeSocket(s), s = NO_SOCKET;
    }
    freeaddrinfo(list);
    if (s == NO_SOCKET) return false;
    noBlockNoDelay(s);
    line = Line{};
    line.s = s, line.heardAt = nowMs();
    return true;
}

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
