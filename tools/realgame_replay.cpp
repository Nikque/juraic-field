// realgame_replay <record folder>...: records of the mode in which Juraic plays 2P of the real game (play/realgame_log.h)
// played again through Follower and Driver, for checking the reading and the following of a build against the records:
// the readings go into Follower in their order, and at each pad record the AI drove Driver::tick is called with the
// frame of the clock the record has. The AI is a script: each pair is brought to the placement the record noted for it
// first ("2P pair N: brought to ..." in events.txt). The driver's engine takes the buttons the record says went to the pad
// (Driver::gate), so that it plays what the game took, and tells Follower where it locked each pair as the driver of the
// record did. Built from the root of the tree:
//   g++ -std=c++20 -O2 -msse4.1 -mpopcnt -pthread -I. tools/realgame_replay.cpp -o realgame_replay
// Prints for each record: the readings, the rounds and the pairs Tracker saw, the pairs Follower found put and the
// pieces at rest it checked and found otherwise, the readings that did not fit, the pictures that put the driver's
// pair right, the lag the pictures fitted, the pairs of 2P that appeared with a pair to come that no window showed before one
// known (shown to the AI as known before observeWith kept the known pairs together); and, for records of the second kind (JFRGLOG2), the times of the pictures and
// the pad (frames from shown to came in, to read, to the pad driven and set; pictures not followed; why the pad was
// driven) and how many placements the replay told Follower otherwise than the record's driver.
#include "../ai/agents.h"
#include "../play/realgame_drive.h"
#include "../play/realgame_log.h"
#include "../play/realgame_pace.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <string>

using namespace play::realgame;

namespace {

int placementOf(int column, const std::string& turn) {
    static const char* turns[] = {"up", "right", "down", "left"};
    for (int id = 0; id < juraic::PLACEMENTS; ++id) {
        const juraic::Placement p = juraic::placement(id);
        if (p.x == column - 1 && turn == turns[p.dir]) return id;
    }
    return -1;
}

// The placements of the record, by the round and the pair (as Tracker counts them).
struct ScriptAgent : juraic::ai::PlacementAgent {
    const std::map<std::pair<int, int>, int>* plan = nullptr;
    const Follower* follow = nullptr;
    ScriptAgent() { ahead = 12; }
    ~ScriptAgent() override { finish(); }
    int choose(const juraic::ai::Observation& o, juraic::Pair, unsigned legal) override {
        const auto it = plan->find({follow->tracker.rounds, int(o.self().nextPair) + 1});
        if (it != plan->end() && it->second >= 0 && (legal >> it->second & 1)) return it->second;
        for (int id = 0; id < juraic::PLACEMENTS; ++id)
            if (legal >> id & 1) return id;
        return 0;
    }
};

// Frames of 1/60 s between two times of the record, to a tenth.
double frames(int64_t from, int64_t to) { return double(to - from) * 60.0 / double(TIME_SECOND); }

void replay(const std::filesystem::path& folder) {
    std::ifstream ev(folder / "events.txt");
    std::string line;
    int startLag = 6, round = 0;
    bool swapped = false;
    std::map<std::pair<int, int>, int> plan;
    const std::regex rLag(R"(^lag (\d+) to begin with)"), rAim(R"(^(\d+) 2P pair (\d+): brought to column (\d), child (\w+))");
    std::smatch m;
    while (std::getline(ev, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find("a round begins") != std::string::npos) ++round;
        if (line.find("the AI plays 2P:") != std::string::npos && line.find("swapped") != std::string::npos) swapped = true;
        if (std::regex_search(line, m, rLag)) startLag = std::stoi(m[1]);
        else if (std::regex_search(line, m, rAim)) plan.emplace(std::make_pair(round, std::stoi(m[2])), placementOf(std::stoi(m[3]), m[4]));
    }
    RecordReader in;
    if (!in.open(folder / "readings.bin")) {
        std::printf("%s: readings.bin not read\n", folder.string().c_str());
        return;
    }
    Follower follow;
    Driver d;
    d.lag = startLag, d.turnsSwapped = swapped;
    auto agent = std::make_unique<ScriptAgent>();
    agent->plan = &plan, agent->follow = &follow;
    Driver::prepare(agent.get());
    d.agent = std::move(agent);
    Reading r;
    int64_t frame = 0;
    int width = 0, height = 0;
    PadRecord p;
    long readings = 0, ticks = 0, spawns = 0, holes = 0, unknownOwn = 0, knownOn = 0, notFollowed = 0, timed = 0, drives = 0, tells = 0, toldOtherwise = 0;
    long why[5] = {};
    double came = 0, read = 0, set = 0;
    int64_t lastShown = 0;
    std::map<int, int> ours;                // (the pairs the replay's driver told, in this round)
    int toldRound = -1;
    while (const uint8_t kind = in.next(r, frame, width, height, p)) {
        if (kind == RECORD_TIME) {
            ++timed, came += frames(in.time.shown, in.time.came), read += frames(in.time.came, in.time.read), lastShown = in.time.shown;
            notFollowed += !in.time.followed;
        } else if (kind == RECORD_READING) {
            follow.step(r, frame), ++readings;
            if (follow.tracker.spawned[1]) {
                // (a pair from the one that appears on that no window showed, with one known after it: as the observation
                //  was before observeWith kept the known pairs together, a pair of the first colour shown as known)
                const Tracker& t = follow.tracker;
                bool hole = false, gap = false;
                for (int i = t.side[1].pairs - 1; i < int(t.pairsLearnt()) && !hole; ++i) {
                    if (!t.knows(1, i)) gap = true;
                    else hole = gap;
                }
                int known = t.side[1].pairs - 1;
                while (t.knows(1, known)) ++known;
                ++spawns, holes += hole, unknownOwn += !t.knows(1, t.side[1].pairs - 1), knownOn += known - (t.side[1].pairs - 1);
            }
        } else if (kind == RECORD_PAD && p.by == 1) {
            const uint8_t sent = p.buttons;
            d.gate = [&d, sent](int64_t, uint8_t) { return d.turnsSwapped ? Driver::otherTurns(sent) : sent; };
            d.tick(follow, p.frame);
            ++ticks;
            if (follow.tracker.rounds != toldRound) toldRound = follow.tracker.rounds, ours.clear();
            if (d.toldPair() >= 0) ours[d.toldPair()] = juraic::placementId(d.toldPlacement());
        } else if (kind == RECORD_SENT) {
            ++drives, ++why[std::min<int>(4, in.sent.why)];
            set += frames(lastShown, in.sent.set);
        } else if (kind == RECORD_TELL) {
            ++tells;
            const auto it = ours.find(in.tell.pair);
            toldOtherwise += it != ours.end() && it->second != in.tell.placement;
        }
    }
    std::printf("%s: readings %ld rounds %d pairs of 2P seen %ld (with a pair to come no window showed, before one known: %ld; the pair itself not known: %ld; known from it on: %.2f) | Follower found %d placements, checked %d (otherwise %d), held %d | doubts %d | driver ticks %ld put right %d explained %d lag %d (measured %d) turns %s",
                folder.filename().string().c_str(), readings, follow.tracker.rounds, spawns, holes, unknownOwn, spawns ? double(knownOn) / double(spawns) : 0.0, follow.placements, follow.checks, follow.misses, follow.held, follow.tracker.doubts, ticks, d.corrected,
                d.explained, d.lag, d.measuredLag(), d.turnsSwapped ? "swapped" : "as sent");
    if (timed)
        std::printf(" | pictures timed %ld: came %.2f read %.2f frames after shown, not followed %ld | pad driven %ld (pictures %ld, none in the frame %ld, none for long %ld, menu %ld), set %.2f frames after the picture shown | told %ld, otherwise %ld",
                    timed, came / double(timed), read / double(timed), notFollowed, drives, why[1], why[2], why[3], why[4], drives ? set / double(drives) : 0.0, tells, toldOtherwise);
    std::printf("\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return std::printf("realgame_replay <record folder>...\n"), 2;
    for (int i = 1; i < argc; ++i) replay(argv[i]);
    return 0;
}
