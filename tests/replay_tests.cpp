// Tests of reading a replay file (play/replay.h): what the game writes is read back as it was, and a file that
// is not whole, or that says two things at once, is refused rather than played as something else.
//   - a replay of one machine and a replay of two machines (first line "... 2 ...", the line "link") are read
//   - a replay of two machines without its delay, with a delay no round can take, or with one player is refused;
//     so is a replay of one machine that names a delay
//   - a file that ends in the middle of a line or of a round is refused
// The files are written into the folder "replay-tests" of the directory the test runs in; nothing is deleted.
#include "../play/replay.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace play;

static void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

static std::filesystem::path written(const std::string& name, const std::string& text) {
    const std::filesystem::path folder = "replay-tests";
    std::filesystem::create_directories(folder);
    const std::filesystem::path path = folder / name;
    std::ofstream(path, std::ios::binary) << text;
    return path;
}
static bool reads(const std::string& name, const std::string& text, Replay* out = nullptr) {
    Replay r;
    const bool ok = loadReplay(written(name, text), r);
    if (out) *out = r;
    return ok;
}

int main() {
    try {
        const std::string v = std::to_string(TIMING_VERSION), one = "JURAIC_FIELD_REPLAY 1 " + v + "\n", two = "JURAIC_FIELD_REPLAY 2 " + v + "\n";
        const std::string round = "ROUND 1 3\n2x1,0 1x0,8\nEND\n";

        Replay r;
        check(reads("one-2p.jfreplay", one + "players 2\n" + round, &r) && r.players == 2 && r.linkDelay == 0 && r.rounds.size() == 1 && r.rounds[0].frames.size() == 3,
              "a replay of one machine with two players was not read");
        check(reads("one-1p.jfreplay", one + "players 1\n" + round, &r) && r.players == 1 && r.linkDelay == 0, "a replay of a game alone was not read");
        check(reads("two.jfreplay", two + "players 2\nlink 5\n" + round + round, &r) && r.players == 2 && r.linkDelay == 5 && r.rounds.size() == 2,
              "a replay of two machines was not read");
        check(reads("two-most.jfreplay", two + "players 2\nlink " + std::to_string(juraic::MAX_LINK_DELAY) + "\n" + round, &r) && r.linkDelay == juraic::MAX_LINK_DELAY,
              "a replay of two machines with the largest delay was not read");

        // Two machines: the delay is part of the game that was played.
        check(!reads("two-no-link.jfreplay", two + "players 2\n" + round), "a replay of two machines without its delay was read (it would be played as a match of one machine)");
        check(!reads("two-link-0.jfreplay", two + "players 2\nlink 0\n" + round), "a replay of two machines with the delay 0 was read");
        check(!reads("two-link-31.jfreplay", two + "players 2\nlink " + std::to_string(juraic::MAX_LINK_DELAY + 1) + "\n" + round), "a replay with a delay beyond what a round takes was read");
        check(!reads("two-link-minus.jfreplay", two + "players 2\nlink -3\n" + round), "a replay with a delay below 0 was read");
        check(!reads("two-link-word.jfreplay", two + "players 2\nlink x\n" + round), "a replay whose delay is no number was read");
        check(!reads("two-1p.jfreplay", two + "players 1\nlink 5\n" + round), "a replay of two machines with one player was read");
        check(!reads("one-link.jfreplay", one + "players 2\nlink 5\n" + round), "a replay of one machine that names a delay was read");

        // A file that is not whole.
        check(!reads("cut-players.jfreplay", one + "players 2\n" + round + "players"), "a file that ends after the word players was read");
        check(!reads("cut-link.jfreplay", two + "players 2\n" + round + "link"), "a file that ends after the word link was read");
        check(!reads("players-word.jfreplay", one + "players x\n" + round), "a replay whose number of players is no number was read");
        check(!reads("players-3.jfreplay", one + "players 3\n" + round), "a replay of three players was read");
        check(!reads("cut-round.jfreplay", one + "players 2\nROUND 1 3\n2x1,0"), "a round without its end was read");
        check(!reads("short-round.jfreplay", one + "players 2\nROUND 1 3\n2x1,0\nEND\n"), "a round with fewer frames than it names was read");
        check(!reads("no-round.jfreplay", one + "players 2\n"), "a replay without a round was read");
        check(!reads("old.jfreplay", "JURAIC_FIELD_REPLAY 1 " + std::to_string(OLDEST_VERSION - 1) + "\nplayers 2\n" + round), "a replay of a version that is not read any more was read");
        check(!reads("other-word.jfreplay", "SOME_OTHER_REPLAY 1 " + v + "\nplayers 2\n" + round), "a file with another first word was read");

        // What the game writes is what it reads: both kinds.
        for (const int delay : {0, 7}) {
            Replay made;
            made.players = 2, made.linkDelay = delay;
            RoundLog log;
            log.seed = 4242;
            for (int i = 0; i < 300; ++i) log.frames.push_back(uint16_t((i / 7 % 5) | (i / 11 % 3) << 8));
            made.rounds.push_back(log), made.rounds.push_back(log);
            const std::filesystem::path path = written(delay ? "saved-two.jfreplay" : "saved-one.jfreplay", "");
            Replay back;
            check(saveReplay(path, made) && loadReplay(path, back), "a replay the game writes was not read back");
            check(back.players == 2 && back.linkDelay == delay && back.rounds.size() == 2 && back.rounds[1].seed == 4242 && back.rounds[1].frames == log.frames,
                  "a replay read back is not the one that was written (delay " + std::to_string(delay) + ")");
        }
        std::cout << "replay tests passed: both kinds are read back; a replay of two machines without a delay a round can take, and a file that is not whole, are refused\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
