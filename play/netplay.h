#pragma once
// A match of two people over a line, as the game plays it: from the call to the end of the match and
// the next one. No window and no devices here - the owner gives the buttons of the own seat once a
// frame and draws what stands here - so the game, its self test and the test of two programs on one
// machine all play the same thing.
//
//   Connecting   the host waits for a guest, or the guest calls; then the match is agreed
//   Ready        the round is about to begin (as in a match on one machine)
//   Play         the own seat every frame, the other from what has arrived (game/link.h)
//   RoundEnd, SetEnd, MatchEnd   as in a match on one machine; at MatchEnd both may ask for another
//   Over         the match ended early: the line is gone, the other left, or the two disagree
//
// The host is 1P, the guest 2P. The rules are the host's. There is no pause.
#include "net.h"
#include "replay.h"
#include "view_state.h"

namespace play {

struct NetPlay {
    enum Stage { Off, Connecting, Ready, Play, RoundEnd, SetEnd, MatchEnd, Over } stage = Off;
    NetPeer peer;
    juraic::LinkedRound link;
    View view[2];
    Settings settings;   // the own settings with the rules of the match (the host's) in them
    Settings ownSettings; // (the guest: what the host's rules are put into for every match)
    Replay replay;       // the rounds of the match so far
    juraic::Rng seeds{1};
    int wins[2]{}, sets[2]{}, timer = 0;
    int startIn = -1;      // Connecting: frames until the match begins (-1: not agreed yet)
    int waited = 0;        // Play: frames in a row the own seat has had to wait for the other
    long waitedInAll = 0;  // ... and in the whole match
    bool askedLast = false; // this end asked for another match after the other had
    bool matchEnded = false; // set in the frame a match ends (the owner saves the replay and clears it)
    std::wstring over;     // Over: why

    int seat() const { return peer.seat; }
    bool on() const { return stage != Off; }

    // What a match takes from the host's settings: the rules, and two of the settings that stand with the buttons -
    // the one the round itself reads (Rules::preInput: with another value on each machine the two would play
    // different rounds from the same buttons, and the match would be stopped), and the quick drop, a button that
    // only one of the two would have otherwise (the owner of the match turns it into buttons: play/main.cpp,
    // personButtons). The rest - the lever of four ways, the AI, the pictures and the sound - is each machine's own,
    // and is not sent.
    static bool travels(const SettingItem& item) { return item.section == SEC_RULES || item.key == "preInput" || item.key == "quickDrop"; }
    // The rules of a match as they travel: one "name=value" a line.
    static std::string rulesText(Settings s) {
        SettingsTable table(s);
        std::string text;
        for (auto& item : table.items)
            if (travels(item)) text += item.key + '=' + std::to_string(item.value(s)) + '\n';
        return text;
    }
    void takeRules(const std::string& text) {
        settings = ownSettings;
        SettingsTable table(settings);
        for (size_t at = 0; at < text.size();) {
            const size_t end = text.find('\n', at);
            const std::string line = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
            for (auto& item : table.items)
                if (travels(item) && line.compare(0, item.key.size() + 1, item.key + '=') == 0) setByText(table, settings, line);
            if (end == std::string::npos) break;
            at = end + 1;
        }
        table.apply(settings);
    }

    // fixedDelay: the delay of the match in frames, 0 = set by the time the line takes.
    void host(std::unique_ptr<Line> line, const Settings& s, const std::string& name, uint64_t seed, int fixedDelay) {
        *this = NetPlay{};
        settings = ownSettings = s;
        NetMatch m;
        m.id = uint32_t(seed ^ seed >> 32) | 1, m.seed = seed, m.rules = rulesText(s);
        peer.wait(std::move(line), m, name, TIMING_VERSION, fixedDelay);
        stage = Connecting;
    }
    void join(std::unique_ptr<Line> line, const Settings& s, const std::string& name) {
        *this = NetPlay{};
        settings = ownSettings = s;
        peer.call(std::move(line), name, TIMING_VERSION);
        stage = Connecting;
    }
    void leave() {
        peer.leave();
        stage = Off;
    }
    // At the end of a match: this end wants another.
    void askAgain() {
        if (stage != MatchEnd || peer.again) return;
        askedLast = peer.hisAgain;
        peer.again = true;
    }

    // One frame. buttons: those of the own seat. each(seat, events) after every frame a seat has played.
    template <class Each> void tick(uint8_t buttons, Each&& each) {
        if (stage == Off) return;
        peer.tick();
        if (stage == Over) return;
        if (peer.state == NetPeer::Lost || peer.state == NetPeer::Left || peer.state == NetPeer::Apart || peer.state == NetPeer::Refused) {
            over = peer.state == NetPeer::Lost      ? L"相手との通信が途切れました"
                   : peer.state == NetPeer::Left    ? L"相手が退出しました"
                   : peer.state == NetPeer::Apart   ? L"2 台のゲームの進み方が食い違ったため、対戦を止めました"
                   : peer.reason == "version"       ? L"相手とソフトの版が違うため、対戦できません"
                                                    : L"相手に断られました";
            // (a match broken off in the middle of a round: the rounds that ended are kept)
            matchEnded = stage != Connecting && stage != MatchEnd && !replay.rounds.empty();
            stage = Over;
            return;
        }
        switch (stage) {
        case Connecting:
            if (peer.state != NetPeer::Joined) return;
            if (startIn < 0) {
                // The guest has the match when the host's word arrives, and waits half a round trip; the host begins
                // when the guest's first packet shows that it has: so both begin at about the same moment.
                if (peer.host && !peer.hisPlaying) return;
                startIn = peer.host ? 0 : peer.roundTrip / 2;
            }
            if (startIn-- > 0) return;
            beginMatch();
            return;
        case Ready:
            if (--timer <= 0) stage = Play;
            return;
        case Play: {
            const auto seen = [&](int s, const juraic::PlayerEvents& e) {
                view[s].update(link.round.player[s], e);
                each(s, e);
            };
            playArrived(link, peer, seen);
            if (link.round.state == juraic::RoundState::Playing) {
                if (playOwn(link, peer, buttons, seen)) waited = 0;
                else ++waited, ++waitedInAll;
                playArrived(link, peer, seen);
            }
            if (link.round.state != juraic::RoundState::Playing) roundOver();
            return;
        }
        case RoundEnd: {
            if (--timer > 0) return;
            const juraic::Rules& rules = settings.rules;
            const int winner = wins[0] >= rules.winsNeeded ? 0 : wins[1] >= rules.winsNeeded ? 1 : -1;
            if (winner < 0) beginRound();
            else if (++sets[winner] >= rules.setsNeeded) stage = MatchEnd, matchEnded = true;
            else stage = SetEnd, timer = 150;
            return;
        }
        case SetEnd:
            if (--timer <= 0) wins[0] = wins[1] = 0, beginRound();
            return;
        case MatchEnd:
            if (peer.again && peer.hisAgain) {
                // (the end that asked last knows first that both have asked: it waits for the other to hear of it)
                startIn = askedLast ? peer.roundTrip / 2 : 0;
                peer.startMatch();
                stage = Connecting;
            }
            return;
        default: return;
        }
    }

private:
    void beginMatch() {
        if (!peer.host) takeRules(peer.match.rules);
        seeds = juraic::Rng(peer.match.seed, 1000 + peer.matches);
        wins[0] = wins[1] = sets[0] = sets[1] = 0;
        waitedInAll = 0;
        replay = Replay{settings, 2, TIMING_VERSION, {}};
        replay.linkDelay = peer.match.delay;
        beginRound();
    }
    void beginRound() {
        peer.startRound();
        link.start(settings.rules, seeds.next() % 1000000000ull, peer.match.delay);
        for (int i = 0; i < 2; ++i) view[i].reset(link.round.player[i]);
        stage = Ready, timer = 90, waited = 0;
    }
    void roundOver() {
        if (link.round.state == juraic::RoundState::Won0) ++wins[0];
        if (link.round.state == juraic::RoundState::Won1) ++wins[1];
        RoundLog log;
        log.seed = link.round.seed;
        // (the buttons of both seats up to the frame the round ended in: a seat that had played on has more, which count for nothing)
        for (size_t f = 0; f < size_t(link.round.frame); ++f) log.frames.push_back(uint16_t(peer.buttons[0][f] | peer.buttons[1][f] << 8));
        replay.rounds.push_back(std::move(log));
        stage = RoundEnd, timer = 150;
    }
};

} // namespace play
