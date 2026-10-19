#pragma once
// A match as a player's machine or a watcher follows it from the lines of the server (docs/PROTOCOL.md):
// MATCH, RULES, ROUND, T (a pair of the tsumo), B (buttons of a seat), END, SEED, RATING, MATCHEND.
// It plays the round with the pairs and buttons that have arrived; a player's machine adds the buttons
// of its own seat itself (playOwn).
#include "lines.h"
#include "round.h"
#include "rules_text.h"
#include "sha256.h"
#include <string>
#include <vector>

namespace arena {

struct MatchView {
    std::string id;
    int seat = -1; // the own seat; -1: a watcher
    std::string name[2], kind[2];
    double rating[2]{1500, 1500}, rd[2]{350, 350};
    int delay = 2;
    ArenaRules rules;
    int wins[2]{};
    bool on = false;      // a match is followed
    bool matchOver = false;
    std::string matchEnd; // why it ended

    int roundNo = 0;
    bool roundOn = false;     // the round is being played (ROUND came, END not yet)
    bool roundEnded = false;  // END came
    ArenaRound round;
    std::string commitment;   // of the round's tsumo seed (ROUND)
    bool seedChecked = false, seedGood = false; // SEED came; it matches the commitment
    std::vector<uint8_t> buttons[2]; // per seat: arrived (or, the own seat, played)
    std::string result, reason;      // END of the round
    int64_t endFrame = 0;
    std::string ratingLine;          // the last RATING line

    // Takes a line of the server. False: it was no line of a match.
    bool take(const std::vector<std::string>& w) {
        if (w.empty()) return false;
        const std::string& k = w[0];
        if (k == "MATCH" && w.size() >= 13) {
            *this = MatchView{};
            id = w[1], seat = int(toNumber(w[2], -1));
            for (int i = 0; i < 2; ++i) {
                name[i] = w[3 + 4 * size_t(i)], kind[i] = w[4 + 4 * size_t(i)];
                rating[i] = std::atof(w[5 + 4 * size_t(i)].c_str()), rd[i] = std::atof(w[6 + 4 * size_t(i)].c_str());
            }
            delay = int(toNumber(w[11], 2)), on = true;
            (void)w[12];
            return true;
        }
        if (!on) return false;
        if (k == "RULES") {
            std::string text;
            for (size_t i = 1; i < w.size(); ++i) text += (i > 1 ? " " : "") + w[i];
            readRules(text, rules);
            return true;
        }
        if (k == "ROUND" && w.size() >= 4) {
            roundNo = int(toNumber(w[1], 0));
            round.startPartial(rules.rules, toUnsigned(w[2]), delay);
            commitment = w[3];
            buttons[0].clear(), buttons[1].clear();
            roundOn = true, roundEnded = false, seedChecked = seedGood = false;
            result.clear(), reason.clear(), endFrame = 0;
            return true;
        }
        if (k == "T" && w.size() >= 4 && toNumber(w[1]) == roundNo) {
            const std::string& p = w[3];
            if (p.size() == 2) round.addPair(int(toNumber(w[2], -1)), juraic::Pair{juraic::Cell(p[0] - '0'), juraic::Cell(p[1] - '0')});
            return true;
        }
        if (k == "B" && w.size() >= 5 && toNumber(w[1]) == roundNo) {
            const int s = int(toNumber(w[2], -1));
            bool ok = false;
            const std::vector<uint8_t> b = fromHex(w[4], &ok);
            if (ok && (s == 0 || s == 1) && s != seat && toNumber(w[3]) == int64_t(buttons[s].size())) buttons[s].insert(buttons[s].end(), b.begin(), b.end());
            return true;
        }
        if (k == "END" && w.size() >= 7 && toNumber(w[1]) == roundNo) {
            result = w[2], reason = w[3], endFrame = toNumber(w[4], 0);
            wins[0] = int(toNumber(w[5], 0)), wins[1] = int(toNumber(w[6], 0));
            roundEnded = true;
            return true;
        }
        if (k == "SEED" && w.size() >= 4 && toNumber(w[1]) == roundNo) {
            seedChecked = true, seedGood = seedCommitment(toUnsigned(w[2]), w[3]) == commitment;
            return true;
        }
        if (k == "RATING") {
            ratingLine.clear();
            for (size_t i = 1; i < w.size(); ++i) ratingLine += (i > 1 ? " " : "") + w[i];
            if (w.size() >= 7)
                for (int i = 0; i < 2; ++i)
                    if (w[1 + 3 * size_t(i)] == name[i]) rating[i] = std::atof(w[2 + 3 * size_t(i)].c_str()), rd[i] = std::atof(w[3 + 3 * size_t(i)].c_str());
            return true;
        }
        if (k == "MATCHEND" && w.size() >= 4) {
            wins[0] = int(toNumber(w[1], 0)), wins[1] = int(toNumber(w[2], 0)), matchEnd = w[3];
            matchOver = true, roundOn = false;
            return true;
        }
        return false;
    }

    bool playing() const { return roundOn && !round.over() && round.link.round.frame < rules.roundFrameLimit; }

    // Plays the frames of a seat whose buttons have arrived, as far as the round lets it go.
    // each(seat, events) after every frame. Returns the frames played.
    // most: at most this many frames (a watcher plays at the speed of the game).
    template <class Each> int playArrived(int s, Each&& each, int most = 1 << 30) {
        int n = 0;
        PlayerEvents e;
        while (n < most && roundOn && size_t(round.played(s)) < buttons[s].size() && round.played(s) < rules.roundFrameLimit &&
               round.play(s, buttons[s][size_t(round.played(s))], e))
            ++n, each(s, e);
        return n;
    }
    // A player's machine: the next frame of its own seat with these buttons. False: it has to wait.
    template <class Each> bool playOwn(uint8_t b, Each&& each) {
        if (seat < 0 || !playing() || round.played(seat) >= rules.roundFrameLimit) return false;
        b &= rules.mask();
        PlayerEvents e;
        if (!round.play(seat, b, e)) return false;
        buttons[seat].push_back(b);
        each(seat, e);
        return true;
    }
};

} // namespace arena
