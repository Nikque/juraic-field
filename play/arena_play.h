#pragma once
// A seat in Juraic Arena, or a watcher of its matches, as the game plays it: the server is called, the person waits
// for a match the server pairs, plays it at the speed of the screen and waits for the next (play/arena/: what the
// server says and how a round of the arena is played). No window and no devices here - the owner gives the buttons
// of the own seat once a frame and draws what stands here - so the game and its self test play the same thing.
//
//   Connecting   the server is called (on a thread of its own: the screen goes on meanwhile), then the name and its
//                token are given
//   Waiting      the person waits for a match (the server pairs whoever waits)
//   Playing      a match: the own seat every frame, the other from what has arrived; between its rounds, and at its
//                end until the person asks for the next (again)
//   Watching     the matches of the arena, WATCH_LAG_MS behind the players (each match after the one before)
//   Over         no server, the server is gone, would not let the person in, or sent a match that cannot be followed
//                (why: `over`)
#include "arena/follow.h"
#include "arena/tcp.h"
#include "view_state.h"
#include <deque>

namespace play {

struct ArenaPlay {
    enum Stage { Off, Connecting, Waiting, Playing, Watching, Over } stage = Off;
    static constexpr int64_t WATCH_LAG_MS = 3000;
    static constexpr int READY_FRAMES = 90; // "ready" and "go" before a round is played, as in a match on one machine

    arena::Line line;
    arena::MatchView match;
    View view[2];
    bool watcher = false;
    std::string name;
    int ready = 0;          // frames of "ready" left
    int waited = 0;         // frames in a row the own seat has waited (for the other seat or for a pair)
    size_t sentOwn = 0;     // own frames sent to the server
    double startRating[2]{}; // the ratings as the match began
    std::wstring over;      // Over: why
    std::deque<std::pair<int64_t, std::string>> held; // a watcher: the lines that have come, used WATCH_LAG_MS later
    size_t heldBytes = 0;   // ... their length (more than arena::Line::MOST_IN: the server is left)
    std::unique_ptr<arena::Call> calling; // Connecting: the call on its way
    std::string hello;      // ... the line to send once it has come

    bool on() const { return stage != Off; }
    bool inMatch() const { return (stage == Playing || stage == Watching) && match.on; }
    int seat() const { return match.seat < 0 ? 0 : match.seat; }

    // (the sockets of the program, once, before the first call: Winsock on Windows)
    static void startSockets() { static arena::NetStart started; }

    // A person: calls the server with the name and token it registered.
    void play(const std::string& host, int port, const std::string& who, const std::string& token) {
        call(host, port, "LOGIN " + who + " " + token + " " + arena::ENGINE_NAME);
        name = who;
    }
    // A watcher: no name needed.
    void watch(const std::string& host, int port) {
        call(host, port, "WATCH auto");
        watcher = true;
    }
    void leave() {
        if (line.open()) line.send("LEAVE");
        line.close();
        calling.reset();
        stage = Off;
    }
    // At the end of a match: wait for the next.
    void again() {
        if (stage != Playing || !match.matchOver) return;
        line.send("WAIT rated");
        match = arena::MatchView{};
        stage = Waiting;
    }

    // One frame. buttons: those of the own seat. each(seat, events) after every frame a seat has played.
    template <class Each> void tick(uint8_t buttons, Each&& each) {
        if (stage == Off || stage == Over) return;
        if (calling) {
            const int got = calling->take(line);
            if (got == 0) return;
            calling.reset();
            if (got < 0) return end(L"サーバーにつながりませんでした");
            line.send(hello);
            if (watcher) stage = Watching;
        }
        line.pump();
        std::string text;
        while (stage != Over && line.nextLine(text)) {
            if (text.rfind("PING", 0) == 0) line.send("PONG" + text.substr(4));
            else if (watcher) held.emplace_back(arena::nowMs(), text), heldBytes += text.size();
            else take(text);
        }
        if (heldBytes > arena::Line::MOST_IN) return end(L"サーバーから届く行が多すぎます");
        while (stage != Over && !held.empty() && arena::nowMs() - held.front().first >= WATCH_LAG_MS) {
            heldBytes -= held.front().second.size();
            take(held.front().second), held.pop_front();
        }
        if (stage == Over) return;
        if (line.closed) return end(L"サーバーとの接続が切れました");
        if (!match.roundOn) return;
        const auto seen = [&](int s, const juraic::PlayerEvents& e) {
            view[s].update(match.round.player(s), e);
            each(s, e);
        };
        if (match.roundEnded) {
            // (the frames that came before the end of the round are played still, at the speed of a watcher: the last
            //  of the round is shown. The own seat has played its own.)
            for (int s = 0; s < 2; ++s)
                if (s != match.seat) match.playArrived(s, seen, watcher ? 2 : 1 << 30);
            return;
        }
        if (watcher) {
            // (as fast as the game, a little faster when behind)
            match.playArrived(0, seen, 2), match.playArrived(1, seen, 2);
            return;
        }
        if (ready > 0) {
            --ready;
        } else {
            const int other = 1 - match.seat;
            match.playArrived(other, seen);
            if (match.playing() && match.round.canPlay(match.seat) && match.playOwn(buttons, seen)) waited = 0;
            else if (match.playing()) ++waited;
            match.playArrived(other, seen);
        }
        if (sentOwn < match.buttons[match.seat].size()) {
            line.send("B " + std::to_string(match.roundNo) + " " + std::to_string(sentOwn) + " " + arena::toHex(match.buttons[match.seat], sentOwn));
            sentOwn = match.buttons[match.seat].size();
        }
        line.pump();
    }

private:
    // The server called (Connecting); `first` is sent once the call has come. Whatever was followed before is left.
    void call(const std::string& host, int port, const std::string& first) {
        startSockets();
        *this = ArenaPlay{};
        calling = std::make_unique<arena::Call>(host, port);
        hello = first;
        stage = Connecting;
    }
    // Over for `why`, the connection closed.
    void end(const wchar_t* why) {
        over = why;
        stage = Over;
        line.close();
        held.clear(), heldBytes = 0;
    }
    void take(const std::string& text) {
        const std::vector<std::string> w = arena::words(text);
        if (w.empty()) return;
        const std::string& k = w[0];
        if (k.rfind("LOGIN:", 0) == 0) {
            if (w.size() >= 2 && w[1] == "OK") {
                line.send("WAIT rated");
                stage = Waiting;
            } else {
                end(k == "LOGIN:engine" ? L"このソフトの版では対戦場に入れません（サーバーとエンジンの版が違います）"
                    : k == "LOGIN:busy" ? L"同じ名前が、すでに対戦場につながっています"
                                        : L"名前かトークンが違います");
            }
            return;
        }
        if (!match.take(w)) return;
        // (a seat of a player's own, none for a watcher)
        if (match.broken || (k == "MATCH" && watcher != (match.seat < 0))) return end(L"サーバーから届いた対戦の内容が正しくありません");
        if (k == "MATCH") {
            if (!watcher) stage = Playing;
            startRating[0] = match.rating[0], startRating[1] = match.rating[1];
        } else if (k == "ROUND") {
            for (int i = 0; i < 2; ++i) view[i].reset(match.round.player(i));
            ready = watcher ? 0 : READY_FRAMES, sentOwn = 0, waited = 0;
        }
    }
};

} // namespace play
