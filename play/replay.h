#pragma once
// A replay is the seed, the rules and the buttons of every frame; playing it
// back runs the same game again (game/match.h is deterministic).
//
// Text file:
//   JURAIC_FIELD_REPLAY <1|2> <version>      (2: a match of two machines, which has the line "link")
//   players <1|2>
//   link <delay>                  a match of two machines: the frames by which garbage is late (game/link.h)
//   <name>=<value> ...            the rule settings (settings.h)
//   ROUND <seed> <frames> [BOARD <84 digits>]
//   <count>x<buttons0>,<buttons1> ...   runs of equal frames
//   END
//   AI <seat 0|1> <name> <seed> <width> <depth> <queues> <ahead> <choices>      (a seat an AI played; after the END of its round)
//   <moves>:<pairs>:<placement>:<frames late>:<searches cut>[:<width>.<width>...] ...   one for every choice
//   AIEND
//
// The AI lines are what the game's AI needs to make its choices again (in a measuring tool): the
// seed of the agent, the search it was given, and what the clock decided in each choice (ai/pace.h,
// X3Pace::Note; frames late = the frames the pair waited for its choice, ai/agent.h). Playing a
// replay back does not need them, and a replay without them is read as before. A reader that does
// not know them passes over them: they hold no '=' and none of its words.
// Two choices in a row with the same <moves>: the pair's first choice and its
// second, made when the third pair came into the window (Params::thinkAgain). <frames late> of the
// second: the frames after the pair appeared at which that choice came (0 = before it appeared,
// -1 = not while the pair was in the air), PlacementAgent::againLog.
#include "../game/link.h"
#include "settings.h"
#include <sstream>

namespace play {

// The version of a replay. It is raised when Timing or the control rules change - a replay is played with the
// frame counts of the game that reads it, so one recorded with other frame counts cannot be read any more - and
// when the game's AI chooses otherwise: a tool that makes the choices of a replay again
// reads the version to know which AI wrote the AI lines.
//   5: the frame counts and the control rules as measured on the real game (the auto-repeat after 10 frames,
//     a pair left alone locking by Timing::lockGain, the wait after a lock, pre-input, the points of a lock by
//     the down key, the pair that comes into the window of the next pairs shown four frames before a pair
//     appears).
//   6: the rules of 5. The game's AI lets its pair go straight down while its second
//     choice is on its way (ai/agent.h, holdSecond); a tool that makes the choices of a replay again
//     plays one of version 5 without that.
//   7: the rules of 5 and 6. The game's AI sets out for its first choice at once
//     (Params::thinkHold 0): the tool plays a replay of version 6, and only that, with the straight drop.
//   8: the rules of 5 to 7. In its reading the game's AI takes the frame of a lock on a
//     tall field from the buttons (Params::x3TallPaths 10), and it counts one key piece at most.
//     The AI of the versions before counted two on some fields; that code is not in these sources,
//     so a tool built from them does not make the choices of a replay before 8 again.
//   9: the rules of 5 to 8. The game's AI counts the moves that fit into a stretch of
//     frames with the length of a move taken from the cells on the field (Params::x3MoveFramesMid 46,
//     x3MoveFramesLate 40), and so the frames of a pair that does not help in its reading of an exchange
//     (xWildFramesMid, xWildFramesLate); it takes the average over the queues for the chain the opponent
//     can build while its own runs (x3OutlastMean 1); and it values a field with numbers of its own
//     (evKey 160, evFireHeight 300, evValley 75, evHill 75); and it takes the placement with the largest
//     sum without chance (b0Noise 0). The tool plays a replay of version 8 with one length of a move
//     (x3MoveFramesMid, x3MoveFramesLate, xWildFramesMid and xWildFramesLate 0), x3OutlastMean 0, evKey 200,
//     evFireHeight 289, evValley 100, evHill 100 and b0Noise 5.
//  10: the rules of 5 to 9. The game's AI pokes with a chain that sends four pieces of garbage or more
//     (x3MinPoke 4), judges a poke with the values of ai/params.h (x3PokeGain 0 and the six named with it
//     there) and values a field with evFireHeight 289. The tool plays a replay of version 9 with x3MinPoke 6
//     and x3PokeGain 150.
//  11: the rules of 5 to 10. In its reading the game's AI takes the frames a pair in the air has
//     lain on the ground off the wait after its lock, as the game does (Params::x3AirRest 1). The tool plays a replay
//     of version 10 with 0.
//  12: the rules of 5 to 11. The game's AI builds with eight made-up sequences of pairs from a table that
//     holds pairs of one colour (Params::beamFixedQueues 4, beamQueues 8). The tool plays a replay of version 11
//     or before with the six fixed sequences, beamFixedQueues 1 (the number of queues is in the replay's AI line).
//  13: the rules of 5 to 12. In a round that is won already the game's AI puts its pairs where its
//     search puts them (Params::x3WinBuild 1). The tool plays a replay of version 12 or before with 0, the first
//     winning placement without a search.
//  14: the rules of 5 to 13. At its top level the game's AI builds on against a running chain by whether the
//     answer will be large enough, and guards against an attack the pair in his hand fires where that costs next
//     to nothing (Params::x3Enough 110, x3Guard 10, x3GuardNear 20, x3GuardMost 50). The tool plays a replay of
//     version 13 or before with x3Enough 0 and x3Guard 0.
// Replays of the versions before 5 are refused, and so is a file that begins with another first word.
constexpr int TIMING_VERSION = 14;
constexpr int OLDEST_VERSION = 5; // the oldest version that is read

// One choice of an AI seat, and the AI of a seat in a round (name empty = a person, or a replay without AI lines).
struct AiChoice {
    int move = 0, pairs = 0, id = -1, late = 0, cut = 0;
    std::vector<int> widths;
};
struct AiLog {
    std::string name;
    uint64_t seed = 0;
    int width = 0, depth = 0, queues = 0, ahead = 0; // beamWidth, beamDepth, beamQueues, thinkAhead
    std::vector<AiChoice> choices;
};

struct RoundLog {
    uint64_t seed = 0;
    bool hasBoard = false;
    juraic::GameField board; // start field of player 1 (practice)
    std::vector<uint16_t> frames; // buttons of player 1 | buttons of player 2 << 8
    AiLog ai[2];
};

struct Replay {
    Settings settings;
    int players = 1;
    int timingVersion = TIMING_VERSION;
    std::vector<RoundLog> rounds;
    // A match of two machines (play/netplay.h): the frames by which the garbage of one seat reaches the other
    // (game/link.h), 0 = a match on one machine. Its rounds are played back as the two machines played them.
    // Such a replay has another number after its first word, so that a program from before does not play it
    // as a match of one machine.
    int linkDelay = 0;
    juraic::Timing timing() const { return juraic::Timing{}; } // the frame counts its rounds are played with: those of today for every version that is read
};

// One line that tells how a round stands: for comparing a round played again
// with the round as it was played (the game against a replay, a tool against the game).
inline std::string roundSummary(const juraic::Round& r) {
    std::ostringstream s;
    s << "state " << int(r.state) << " frames " << r.frame;
    for (int i = 0; i < r.players; ++i) {
        const juraic::Player& p = r.player[i];
        s << " | score " << p.scoring.score << " moves " << p.moves << " field " << std::hex
          << uint64_t(juraic::KeyHash()(p.field.f.key())) << std::dec << ' ';
        for (int x = 0; x < juraic::W; ++x) s << int(p.field.row14[size_t(x)]);
    }
    return s.str();
}

inline bool saveReplay(const std::filesystem::path& path, Replay& r) {
    std::error_code ignored;
    std::filesystem::create_directories(path.parent_path(), ignored);
    std::ofstream out(path, std::ios::binary);
    out << "JURAIC_FIELD_REPLAY " << (r.linkDelay ? 2 : 1) << ' ' << TIMING_VERSION << "\nplayers " << r.players << '\n';
    if (r.linkDelay) out << "link " << r.linkDelay << '\n';
    SettingsTable table(r.settings);
    for (auto& item : table.items) out << item.key << '=' << item.value(r.settings) << '\n';
    for (const RoundLog& round : r.rounds) {
        out << "ROUND " << round.seed << ' ' << round.frames.size();
        if (round.hasBoard) {
            out << " BOARD ";
            for (int x = 0; x < juraic::W; ++x)
                for (int y = 0; y < juraic::H; ++y) out << int(round.board.f.get(x, y));
            for (int x = 0; x < juraic::W; ++x) out << int(round.board.row14[size_t(x)]);
        }
        out << '\n';
        for (size_t i = 0, column = 0; i < round.frames.size(); ++column) {
            size_t j = i;
            while (j < round.frames.size() && round.frames[j] == round.frames[i]) ++j;
            out << j - i << 'x' << (round.frames[i] & 255) << ',' << (round.frames[i] >> 8) << (column % 12 == 11 ? '\n' : ' ');
            i = j;
        }
        out << "\nEND\n";
        for (int seat = 0; seat < 2; ++seat) {
            const AiLog& a = round.ai[seat];
            if (a.name.empty()) continue;
            out << "AI " << seat << ' ' << a.name << ' ' << a.seed << ' ' << a.width << ' ' << a.depth << ' ' << a.queues << ' ' << a.ahead << ' ' << a.choices.size() << '\n';
            for (size_t i = 0; i < a.choices.size(); ++i) {
                const AiChoice& c = a.choices[i];
                out << c.move << ':' << c.pairs << ':' << c.id << ':' << c.late << ':' << c.cut;
                for (size_t k = 0; k < c.widths.size(); ++k) out << (k ? '.' : ':') << c.widths[k];
                out << (i % 8 == 7 ? '\n' : ' ');
            }
            out << "\nAIEND\n";
        }
    }
    out.flush();
    return bool(out);
}

inline bool loadReplay(const std::filesystem::path& path, Replay& r) {
    std::ifstream in(path, std::ios::binary);
    std::string word;
    int version = 0;
    if (!(in >> word >> version >> r.timingVersion) || word != "JURAIC_FIELD_REPLAY" || (version != 1 && version != 2) || r.timingVersion < OLDEST_VERSION) return false;
    r = Replay{r.settings, 1, r.timingVersion, {}};
    r.settings.rules.setsNeeded = 1; // (a replay from before there were sets does not name them: its match was one set)
    SettingsTable table(r.settings);
    while (in >> word) {
        if (word == "players") {
            if (!(in >> r.players)) return false; // (a file cut off here)
        } else if (word == "link") {
            if (!(in >> r.linkDelay) || r.linkDelay < 1 || r.linkDelay > juraic::MAX_LINK_DELAY) return false;
        } else if (word == "ROUND") {
            RoundLog round;
            size_t frames = 0;
            if (!(in >> round.seed >> frames) || frames > 60 * 60 * 60) return false;
            std::string line;
            std::getline(in, line);
            const size_t board = line.find("BOARD ");
            if (board != std::string::npos && line.size() >= board + 6 + 84) {
                round.hasBoard = true;
                const char* d = line.c_str() + board + 6;
                for (int x = 0; x < juraic::W; ++x)
                    for (int y = 0; y < juraic::H; ++y, ++d)
                        if (*d > '0' && *d <= '6') round.board.f.set(x, y, juraic::Cell(*d - '0'));
                for (int x = 0; x < juraic::W; ++x, ++d) round.board.row14[size_t(x)] = juraic::Cell(*d - '0');
            }
            while (in >> word && word != "END") {
                unsigned count = 0, b0 = 0, b1 = 0;
#ifdef _MSC_VER
                if (sscanf_s(word.c_str(), "%ux%u,%u", &count, &b0, &b1) != 3 ||
#else
                if (std::sscanf(word.c_str(), "%ux%u,%u", &count, &b0, &b1) != 3 ||
#endif
                    round.frames.size() + count > frames) return false;
                round.frames.insert(round.frames.end(), count, uint16_t(b0 | b1 << 8));
            }
            if (word != "END" || round.frames.size() != frames) return false; // (a file cut off in the middle has no END)
            r.rounds.push_back(std::move(round));
        } else if (word == "AI") {
            int seat = -1;
            size_t choices = 0;
            AiLog a;
            if (!(in >> seat >> a.name >> a.seed >> a.width >> a.depth >> a.queues >> a.ahead >> choices) || seat < 0 || seat > 1 || r.rounds.empty()) return false;
            while (in >> word && word != "AIEND") {
                AiChoice c;
                std::istringstream part(word);
                char colon = 0;
                if (!(part >> c.move >> colon >> c.pairs >> colon >> c.id >> colon >> c.late >> colon >> c.cut)) return false;
                for (int width = 0; part >> colon >> width;) c.widths.push_back(width);
                a.choices.push_back(std::move(c));
            }
            if (word != "AIEND" || a.choices.size() != choices) return false;
            r.rounds.back().ai[seat] = std::move(a);
        } else {
            const size_t eq = word.find('=');
            if (eq == std::string::npos) continue;
            for (auto& item : table.items)
                if (item.key == word.substr(0, eq)) {
                    const int v = atoi(word.c_str() + eq + 1);
                    item.value(r.settings) = v < item.low ? item.low : v > item.high ? item.high : v;
                }
        }
    }
    table.apply(r.settings);
    // (the two kinds: a match of two machines has two players and names the delay it was played with - without
    //  it the buttons would be played as a match of one machine, which is another game; any other replay names none)
    if (version == 2 ? r.players != 2 || r.linkDelay < 1 : r.linkDelay != 0) return false;
    return (r.players == 1 || r.players == 2) && !r.rounds.empty();
}

} // namespace play
