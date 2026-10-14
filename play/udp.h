#pragma once
// The line of a match between two machines as a UDP socket (play/net.h, Line).
//
// The host listens on a port, on IPv4 and IPv6 alike, and talks to the first machine whose datagram
// is one of this game - until it has a guest, a sender it has no use for is forgotten again
// (NetPeer::tick); the guest sends to the host's address. Nothing here opens a port in a router
// or asks a firewall for anything: the host must be reachable at the address the guest is given
// (the same network, a VPN, a forwarded port, or IPv6 with the port let through).
// Anybody can send to an open port, and as much as he likes: what one frame takes from the socket is bounded
// (MOST_A_FRAME), the rest waits for the next frame or is dropped by the system.
// Windows: Winsock; <winsock2.h> must come before <windows.h>. Elsewhere: the sockets of POSIX.
#include "net.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace play {

constexpr int NET_PORT = 50777; // the port a host listens on unless the settings name another

#ifdef _WIN32
using UdpSocket = SOCKET;
using AddressLength = int;
constexpr UdpSocket NO_SOCKET = INVALID_SOCKET;
inline void closeSocket(UdpSocket s) { closesocket(s); }
inline bool startSockets() {
    static bool tried = false, good = false;
    if (!tried) {
        WSADATA data;
        tried = true, good = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    return good;
}
#else
using UdpSocket = int;
using AddressLength = socklen_t;
constexpr UdpSocket NO_SOCKET = -1;
inline void closeSocket(UdpSocket s) { close(s); }
inline bool startSockets() { return true; }
#endif

// "host", "host:port", "[v6]", "[v6]:port" or a bare IPv6 address. port stays as it is when none is named.
inline void splitAddress(const std::string& text, std::string& host, int& port) {
    std::string t;
    for (char c : text)
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') t += c;
    const auto number = [&port](const std::string& s) {
        const int v = std::atoi(s.c_str());
        if (v > 0 && v < 65536) port = v;
    };
    if (!t.empty() && t[0] == '[') {
        const size_t close = t.find(']');
        host = t.substr(1, close == std::string::npos ? std::string::npos : close - 1);
        if (close != std::string::npos && close + 2 < t.size() && t[close + 1] == ':') number(t.substr(close + 2));
        return;
    }
    const size_t first = t.find(':'), last = t.rfind(':');
    if (first != std::string::npos && first == last) host = t.substr(0, first), number(t.substr(first + 1));
    else host = t;
}

struct UdpLine : Line {
    UdpSocket s = NO_SOCKET;
    sockaddr_storage peer{};
    AddressLength peerLength = 0;   // 0: nobody yet (a host that waits)
    bool listening = false;
    // The datagrams taken from the socket since the last tick, whoever sent them and whatever they were: a match
    // brings one or two a frame, and the game must come back to its screen however many there are.
    static constexpr int MOST_A_FRAME = 256;
    int taken = 0;

    ~UdpLine() override {
        if (s != NO_SOCKET) closeSocket(s);
    }

    // The host: any machine may call on this port (local, for tests: only programs of this machine - 1: at ::1,
    // 2: at 127.0.0.1, which reaches this socket as an IPv4 address written as an IPv6 one, as every IPv4 caller does).
    bool listen(int port, int local = 0) {
        if (!startSockets()) return false;
        s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
        if (s == NO_SOCKET) return false;
#ifdef _WIN32
        DWORD off = 0;
#else
        int off = 0;
#endif
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&off), sizeof off); // (IPv4 too)
        sockaddr_in6 any{};
        any.sin6_family = AF_INET6, any.sin6_port = htons(static_cast<unsigned short>(port));
        if (local == 1) any.sin6_addr = in6addr_loopback;
        if (local == 2) {
            const unsigned char mapped[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 127, 0, 0, 1};
            std::memcpy(&any.sin6_addr, mapped, 16);
        }
        if (bind(s, reinterpret_cast<const sockaddr*>(&any), sizeof any) != 0) return fail();
        listening = true;
        return ready();
    }
    // The guest: the host at this name or address.
    bool call(const std::string& host, int port) {
        if (!startSockets()) return false;
        addrinfo hints{}, *found = nullptr;
        hints.ai_family = AF_UNSPEC, hints.ai_socktype = SOCK_DGRAM, hints.ai_protocol = IPPROTO_UDP;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0 || !found) return false;
        std::memcpy(&peer, found->ai_addr, found->ai_addrlen);
        peerLength = AddressLength(found->ai_addrlen);
        s = socket(found->ai_family, SOCK_DGRAM, IPPROTO_UDP);
        freeaddrinfo(found);
        if (s == NO_SOCKET) return false;
        return ready();
    }
    void send(const Datagram& d) override {
        if (s == NO_SOCKET || !peerLength) return;
        sendto(s, reinterpret_cast<const char*>(d.data()), int(d.size()), 0, reinterpret_cast<const sockaddr*>(&peer), peerLength);
    }
    void tick() override { taken = 0; }
    bool receive(Datagram& d) override {
        if (s == NO_SOCKET) return false;
        while (taken < MOST_A_FRAME) {
            char buffer[2048];
            sockaddr_storage from{};
            AddressLength length = sizeof from;
#ifdef _WIN32
            const int n = recvfrom(s, buffer, sizeof buffer, 0, reinterpret_cast<sockaddr*>(&from), &length);
            if (n == SOCKET_ERROR) {
                // (not ours to mind, the next datagram is read: a datagram that was refused on the way there comes
                //  back as an error on a UDP socket of Windows; so does one whose time ran out on the way; and one
                //  longer than anything said here is cut off and reported so - it is none of this game)
                const int error = WSAGetLastError();
                if (error != WSAECONNRESET && error != WSAENETRESET && error != WSAEMSGSIZE) return false; // (nothing is waiting)
                ++taken;
                continue;
            }
#else
            // (MSG_TRUNC: the length of the whole datagram is returned, so one longer than the buffer - none of
            //  this game - is passed over as on Windows rather than read cut off)
            const ssize_t whole = recvfrom(s, buffer, sizeof buffer, MSG_TRUNC, reinterpret_cast<sockaddr*>(&from), &length);
            if (whole < 0) {
                // (a refusal that came back from the way there is passed over, the next datagram is read)
                if (errno != ECONNREFUSED && errno != EINTR) return false; // (nothing is waiting)
                ++taken;
                continue;
            }
            if (whole > ssize_t(sizeof buffer)) {
                ++taken;
                continue;
            }
            const int n = int(whole);
#endif
            ++taken;
            if (n < 4 || buffer[0] != 'J' || buffer[1] != 'F') continue;
            if (!peerLength) {
                if (!listening) continue;
                std::memcpy(&peer, &from, size_t(length)), peerLength = length; // (the first caller is taken for the guest; forget() lets go of it)
            } else if (length != peerLength || std::memcmp(&from, &peer, size_t(length)) != 0) {
                continue; // (somebody else)
            }
            d.assign(buffer, buffer + n);
            return true;
        }
        return false;
    }
    // The host waits for a guest again.
    void forget() override {
        if (listening) peerLength = 0;
    }

private:
    bool fail() {
        closeSocket(s);
        s = NO_SOCKET;
        return false;
    }
    bool ready() {
#ifdef _WIN32
        u_long on = 1;
        if (ioctlsocket(s, FIONBIO, &on) != 0) return fail();
#else
        const int flags = fcntl(s, F_GETFL, 0);
        if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) != 0) return fail();
        // (Linux counts the whole cost of a datagram against the receive buffer: the default one of 212,992 bytes
        //  keeps 278 small datagrams, and a datagram of the other end behind a flood of 400 was dropped. The size
        //  asked for is cut to what the system allows; Linux keeps twice that, 555 small ones by default.)
        const int most = 1 << 20;
        setsockopt(s, SOL_SOCKET, SO_RCVBUF, &most, sizeof most);
#endif
        return true;
    }
};

// The addresses of this machine, for telling a guest where to call (those of its networks; an address a
// router gives the whole house to the outside is not among them).
inline std::vector<std::string> ownAddresses() {
    std::vector<std::string> out;
#ifndef _WIN32
    // (the addresses of the network cards; the name of the machine often leads to a loopback address alone here)
    ifaddrs* cards = nullptr;
    if (getifaddrs(&cards) != 0) return out;
    for (const ifaddrs* c = cards; c; c = c->ifa_next) {
        if (!c->ifa_addr) continue;
        char text[INET6_ADDRSTRLEN]{};
        if (c->ifa_addr->sa_family == AF_INET) {
            const in_addr& a = reinterpret_cast<const sockaddr_in*>(c->ifa_addr)->sin_addr;
            if ((ntohl(a.s_addr) >> 24) == 127) continue; // (this machine alone)
            inet_ntop(AF_INET, &a, text, sizeof text);
        } else if (c->ifa_addr->sa_family == AF_INET6) {
            const in6_addr& a = reinterpret_cast<const sockaddr_in6*>(c->ifa_addr)->sin6_addr;
            if (IN6_IS_ADDR_LINKLOCAL(&a) || IN6_IS_ADDR_LOOPBACK(&a)) continue;
            inet_ntop(AF_INET6, &a, text, sizeof text);
        }
        if (text[0]) out.push_back(text);
    }
    freeifaddrs(cards);
    return out;
#else
    if (!startSockets()) return out;
    char name[256];
    if (gethostname(name, sizeof name) != 0) return out;
    addrinfo hints{}, *found = nullptr;
    hints.ai_family = AF_UNSPEC, hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(name, nullptr, &hints, &found) != 0) return out;
    for (const addrinfo* a = found; a; a = a->ai_next) {
        char text[INET6_ADDRSTRLEN]{};
        if (a->ai_family == AF_INET) {
            inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(a->ai_addr)->sin_addr, text, sizeof text);
        } else if (a->ai_family == AF_INET6) {
            const sockaddr_in6* v6 = reinterpret_cast<const sockaddr_in6*>(a->ai_addr);
            if (IN6_IS_ADDR_LINKLOCAL(&v6->sin6_addr)) continue; // (of no use to another machine without the name of the network card)
            inet_ntop(AF_INET6, &v6->sin6_addr, text, sizeof text);
        }
        if (text[0]) out.push_back(text);
    }
    freeaddrinfo(found);
    return out;
#endif
}

} // namespace play
