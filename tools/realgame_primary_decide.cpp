// A choice of the AI made again on a position of a record of the real game (primary-play.tsv and boards.bin of
// play/memory/primary_drive14_win.h). Reads the records only.
//
//   realgame_primary_decide <folder of a record> <round> <pairs put> [name=value ...]
//       The position: the row of that round in which the seat played has put so many pairs and waits for the next (the
//       first one with `wait` frames or fewer to go), its field as the row has it, the other field as boards.bin had it
//       then, the pairs as the rows of the round show them. The AI (x3 with the search the game measures it with, no
//       clock) chooses a placement for the pair to come; told: the kind of the choice, the plans that fire, what the
//       other side holds, and how the "win" was reckoned.
//       The settings: wait=N (10); known=N, the pairs of the seat the AI knows from the one to come on (2; 3 for the
//       second choice of a pair); palette=asis|seen|both (both: once with the colours 1 to 4 as the rules are made, once
//       with the colours the round has shown, PrimaryObserver14::coloursInPlay); timing=plain|old|oldcancel|default (old:
//       the frame counts up to the records of 2026-11-16, oldcancel: those with the other model's rules); seed=N;
//       threads=N; remaining name=value arguments are settings of the AI (ai/params.h).
//
// Not in the records, and made up here: where the other seat's pair in the hands is (shown where a pair appears), the
// frames the other seat waits for its pair (13), the garbage waiting above the other field (none).
#include "../ai/agents.h"
#include "../play/memory/primary_play14.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace juraic;

struct Row {
    long tick = 0;
    long long serial = 0, us = 0;
    int operating = 0, pairs = 0, wait = 0, otherOperating = 0, otherPairs = 0, notice = 0, pending = 0, hidden = 0, round = 0;
    std::string next, field;
};

static std::vector<Row> readRecord(const std::string& folder) {
    std::ifstream in(folder + "/primary-play.tsv");
    std::vector<Row> rows;
    std::string line;
    if (!std::getline(in, line)) return rows;
    std::map<std::string, size_t> column;
    {
        std::istringstream head(line);
        std::string name;
        for (size_t i = 0; std::getline(head, name, '\t'); ++i) {
            if (!name.empty() && name.back() == '\r') name.pop_back();
            column[name] = i;
        }
    }
    std::string field;
    long before = -1;
    int round = 1;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> cell;
        std::istringstream cells(line);
        std::string one;
        while (std::getline(cells, one, '\t')) cell.push_back(one);
        const auto text = [&](const char* name) {
            const auto at = column.find(name);
            return at == column.end() || at->second >= cell.size() ? std::string() : cell[at->second];
        };
        const auto number = [&](const char* name) { return std::atoll(text(name).c_str()); };
        Row r;
        r.us = number("us");
        r.tick = long(number("tick")), r.serial = number("serial"), r.operating = int(number("operating")), r.pairs = int(number("pairs"));
        r.wait = int(number("shown_wait")), r.otherOperating = int(number("other_operating")), r.otherPairs = int(number("other_pairs"));
        r.notice = int(number("notice")), r.pending = int(number("pending")), r.hidden = int(number("hidden"));
        r.next = text("next");
        const std::string cells84 = text("field");
        if (cells84 != "=") field = cells84;
        r.field = field;
        if (before >= 0 && r.tick < before) ++round;
        before = r.tick, r.round = round;
        rows.push_back(std::move(r));
    }
    return rows;
}

static GameField fieldOf(const std::string& digits) {
    GameField g;
    if (digits.size() < 84) return g;
    for (int y = 13; y >= 0; --y)
        for (int x = 0; x < W; ++x) {
            const int v = digits[size_t(13 - y) * 6 + size_t(x)] - '0';
            if (v <= 0) continue;
            if (y == 13) g.row14[size_t(x)] = Cell(v);
            else g.f.set(x, y, Cell(v));
        }
    return g;
}

// The boards of boards.bin as they stood at `serial`: the cells of each table (84 digits), by its address.
static std::map<uint64_t, std::string> boardsAt(const std::string& folder, long long serial) {
    std::map<uint64_t, std::string> latest;
    std::ifstream in(folder + "/boards.bin", std::ios::binary);
    std::vector<char> rec(472);
    while (in.read(rec.data(), std::streamsize(rec.size()))) {
        uint64_t at = 0, address = 0;
        std::memcpy(&at, rec.data(), 8), std::memcpy(&address, rec.data() + 24, 8);
        if ((long long)at > serial) continue;
        std::string cells;
        for (int r = 0; r < 14; ++r)
            for (int x = 0; x < 6; ++x) cells.push_back(char('0' + (uint8_t(rec[size_t(32 + r * 32 + x * 4)]) & 255)));
        latest[address] = cells;
    }
    return latest;
}

// The board the seat `seat` was bound to at the time `us` of the reader's clock (primary-reader.tsv; 0: not told).
static uint64_t boundBoard(const std::string& folder, long long us, int seat) {
    std::ifstream in(folder + "/primary-reader.tsv");
    std::string line;
    if (!std::getline(in, line)) return 0;
    std::map<std::string, size_t> column;
    {
        std::istringstream head(line);
        std::string name;
        for (size_t i = 0; std::getline(head, name, '\t'); ++i) {
            if (!name.empty() && name.back() == '\r') name.pop_back();
            column[name] = i;
        }
    }
    const auto state = column.find("state"), board = column.find(seat ? "board1" : "board0");
    if (state == column.end() || board == column.end()) return 0;
    uint64_t found = 0;
    while (std::getline(in, line)) {
        std::vector<std::string> cell;
        std::istringstream cells(line);
        std::string one;
        while (std::getline(cells, one, '\t')) cell.push_back(one);
        if (cell.empty() || std::atoll(cell[0].c_str()) > us) break;
        if (state->second >= cell.size() || board->second >= cell.size()) continue;
        // (the seats let go: the board of the binding before is not that of this time)
        found = cell[state->second] == "bound" ? std::strtoull(cell[board->second].c_str(), nullptr, 16) : 0;
    }
    return found;
}

static void showFields(const GameField& mine, const GameField& other) {
    static const char* SIGNS = ".RGBYPo";
    for (int y = 13; y >= 0; --y) {
        std::string a, b;
        for (int x = 0; x < W; ++x) {
            const int m = int(y == 13 ? mine.row14[size_t(x)] : mine.f.get(x, y)), t = int(y == 13 ? other.row14[size_t(x)] : other.f.get(x, y));
            a.push_back(SIGNS[m > 6 ? 0 : m]), b.push_back(SIGNS[t > 6 ? 0 : t]);
        }
        if (a == "......" && b == "......") continue;
        std::printf("   %2d  %s    %s\n", y + 1, a.c_str(), b.c_str());
    }
}

// The largest chain a trial drop starts on a field, in points (0: none).
static int direct(const GameField& g, const Rules& rules, const Timing& timing, const ai::Params& p) {
    ai::Trials trials;
    ai::detect(g, rules, timing, p.trialMaxAdd, trials);
    int best = 0;
    for (int i = 0; i < trials.count; ++i)
        if (trials.item[i].chains >= rules.chainRequirement && trials.item[i].points > best) best = trials.item[i].points;
    return best;
}

struct Probe : ai::X3Agent {
    explicit Probe(const ai::Params& q) : ai::X3Agent(q) {}
    static const char* kindName(Kind k) {
        static const char* NAMES[] = {"none", "win", "answer", "answer by building on", "poke", "build", "fire", "all clear", "crush", "crush by building on"};
        static_assert(std::size(NAMES) == size_t(KINDS), "a name for every kind of choice (ai/pace.h X3Pace::Kind)");
        return int(k) >= 0 && int(k) < int(KINDS) ? NAMES[int(k)] : "?";
    }
    void tell(const ai::Observation& o) {
        const unsigned legal = legalPlacements(o.self().field);
        const int id = choose(o, o.mine().pair(o.self().nextPair), legal);
        const Placement at = placement(id < 0 ? 0 : id);
        const int rate = o.rules.rateAt(o.frame);
        std::printf("   the choice: %s, placement %d (column %d, direction %d)\n", kindName(kind), id, int(at.x) + 1, int(at.dir));
        // The plans that fire, the largest by the pair that fires it.
        for (int depth = 1; depth <= 3; ++depth) {
            const ai::Plan* big = nullptr;
            for (const ai::Plan& s : mine.plans)
                if (s.fireDepth == depth && (!big || s.sent > big->sent)) big = &s;
            if (big)
                std::printf("   own plans: the largest chain fired by pair %d of the screen: %d waves, %d points, %d garbage left above the other field (first placement %d)\n", depth,
                            int(big->chains), big->points, big->sent, int(big->id[0]));
        }
        int hisPlans = 0, hisMost = 0;
        for (const ai::Plan& x : theirs.plans)
            if (x.fireDepth) ++hisPlans, hisMost = x.sent > hisMost ? x.sent : hisMost;
        std::printf("   the other side: %d pairs known, %d plans, %d of them fire (the largest leaves %d garbage)\n", theirs.pairCount, int(theirs.plans.size()), hisPlans, hisMost);
        int ownMain = 0, hisMain = 0;
        ai::buildValue(o.self().field, o.rules, o.timing, p, &ownMain);
        ai::buildValue(o.opponent().field, o.rules, o.timing, p, &hisMain);
        std::printf("   main chains as the AI values them (key pieces counted): own %d points (%d garbage), other %d points (%d garbage)\n", ownMain, ownMain / rate, hisMain, hisMain / rate);
        // How a "win" is reckoned below the placement chosen and below the one whose plan sends most.
        const ai::Plan* most = nullptr;
        for (const ai::Plan& s : mine.plans)
            if (s.fireDepth && (!most || s.sent > most->sent)) most = &s;
        if (!most) return;
        const uint64_t seed = roundSeed + uint64_t(o.self().moves);
        // (the answer is the one outlasts itself reckons - its moves, rate and points carried; -1: it did not get that far)
        Rival rival;
        int answer = -1;
        const bool wins = outlasts(o, most->id[0], rate, seed, rival, nullptr, &answer);
        if (!rival.searched) {
            std::printf("   \"win\" below placement %d: %s without a search of the other field\n", int(most->id[0]), wins ? "taken" : "not taken");
            return;
        }
        if (answer < 0) {
            std::printf("   \"win\" below placement %d (%d garbage): %s -> %s\n", int(most->id[0]), most->sent,
                        // (why outlasts ended, as it told it: Rival::unanswered)
                        rival.unanswered ? "no pair of the other side in time to answer the chain"
                                         : "the search of its field was stopped before the moves it has were searched",
                        wins ? "outlasts: a win" : "does not outlast: no win");
            return;
        }
        std::printf("   \"win\" below placement %d (%d garbage): the answer of the other side as outlasts reckons it %d garbage (the search of its field %d deep%s); "
                    "%d must be left to close its field%s -> %s\n",
                    int(most->id[0]), most->sent, answer, rival.depth, rival.built.cut ? ", stopped" : "", rival.need,
                    p.x3OutlastSettled ? " (x3OutlastSettled: more after a large answer)" : "", wins ? "outlasts: a win" : "does not outlast: no win");
    }
};

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cout << "usage: realgame_primary_decide <folder of a record> <round> <pairs put> [wait=N] [known=N] [palette=asis|seen|both] [timing=plain|old|oldcancel|default] [seed=N] [threads=N] [name=value ...]\n";
        return 2;
    }
    const std::string folder = argv[1];
    const int round = std::atoi(argv[2]), put = std::atoi(argv[3]);
    int wait = 10, known = 2, threads = 8;
    constexpr int me = 1;
    uint64_t seed = 1;
    std::string palette = "both", timingName = "plain", settings;
    for (int i = 4; i < argc; ++i) {
        const std::string a = argv[i];
        const size_t eq = a.find('=');
        if (eq == std::string::npos) return std::cout << "not name=value: " << a << "\n", 2;
        const std::string name = a.substr(0, eq), value = a.substr(eq + 1);
        if (name == "wait") wait = std::atoi(value.c_str());
        else if (name == "known") known = std::atoi(value.c_str());
        else if (name == "palette") palette = value;
        else if (name == "timing") timingName = value;
        else if (name == "seed") seed = std::strtoull(value.c_str(), nullptr, 10);
        else if (name == "threads") threads = std::atoi(value.c_str());
        else settings += a + "\n";
    }
    const Timing timing = timingName == "old" ? play::realgame::primaryTimingBefore14(false) : timingName == "oldcancel" ? play::realgame::primaryTimingBefore14(true) :
                          timingName == "default" ? DEFAULT_TIMING : play::realgame::primaryTiming14(false);

    const std::vector<Row> rows = readRecord(folder);
    if (round < 1 || put < 0 || put > 100000 || known < 1 || known > 100000)
        return std::cerr << "round, pair index or visible pair count is out of range\n", 2;
    if (rows.empty()) return std::cout << "no rows in " << folder << "/primary-play.tsv\n", 1;
    // The pairs of the round, as its rows show them (the pair in the hands and the two after it).
    std::vector<Pair> sequence;
    std::vector<bool> shown;
    const Row* at = nullptr;
    const Row* last = nullptr;
    for (const Row& r : rows) {
        if (r.round != round) continue;
        if (r.pairs < 0 || r.pairs > 100000 || r.otherPairs < -1 || r.otherPairs > 100000)
            return std::cerr << "pair count out of range\n", 1;
        if (r.operating && r.next.size() == 8 && r.next[2] == ' ' && r.next[5] == ' ' &&
            r.next.substr(0,2).find_first_not_of("12345") == r.next.npos &&
            r.next.substr(3,2).find_first_not_of("12345") == r.next.npos &&
            r.next.substr(6,2).find_first_not_of("12345") == r.next.npos) {
            for (int k = 0; k < 3; ++k) {
                const size_t index = size_t(r.pairs + k);
                if (sequence.size() <= index) sequence.resize(index + 1, Pair{Cell(1), Cell(1)}), shown.resize(index + 1, false);
                sequence[index] = Pair{Cell(r.next[size_t(k) * 3] - '0'), Cell(r.next[size_t(k) * 3 + 1] - '0')}, shown[index] = true;
            }
        }
        if (r.pairs == put && !r.operating) {
            last = &r;
            if (!at && r.wait <= wait) at = &r;
        }
    }
    if (!at) at = last;
    if (!at) return std::cout << "round " << round << " has no row with " << put << " pairs put and none in the hands\n", 1;
    // The other seat as the row read it. A row in which it was not read (-1) takes it from the last row of the round
    // before it that read it, and says so; with none, the position is not made.
    int otherPairs = at->otherPairs, otherOperating = at->otherOperating;
    const Row* otherRow = at;
    if (otherPairs < 0) {
        const Row* read = nullptr;
        for (const Row& r : rows)
            if (r.round == round && r.serial <= at->serial && r.otherPairs >= 0) read = &r;
        if (!read) return std::cout << "the other seat is not read in the row of round " << round << " with " << put << " pairs put, nor in a row before it\n", 1;
        std::cout << "(the other seat is not read in the row: taken from the last row that read it, serial " << read->serial << ")\n";
        otherPairs = read->otherPairs, otherOperating = read->otherOperating, otherRow = read;
    }
    // The pairs the AI knows: its own from the one to come on, and those the other seat's window has shown.
    int count = put + known;
    if (otherPairs + (otherOperating ? 3 : 2) > count) count = otherPairs + (otherOperating ? 3 : 2);
    if (count > int(sequence.size())) count = int(sequence.size());
    for (int i = put; i < count; ++i)
        if (!shown[size_t(i)]) count = i;
    if (count <= put) return std::cout << "the pair to come is not in the rows of the round\n", 1;

    const auto validField = [](const std::string& cells) {
        return cells.size() == 84 && cells.find_first_not_of("0123456") == cells.npos;
    };
    if (!validField(at->field)) return std::cerr << "own field is missing from this frame\n", 1;
    const GameField mine = fieldOf(at->field);
    GameField other;
    {
        const std::map<uint64_t, std::string> boards = boardsAt(folder, otherRow->serial);
        // (the own table: the one most like the field of the row; the other one is the other seat's)
        int bestSame = -1;
        uint64_t own = 0;
        for (const auto& [address, cells] : boards) {
            int same = 0;
            for (size_t i = 0; i < 84 && i < otherRow->field.size(); ++i) same += cells[i] == otherRow->field[i];
            if (same > bestSame) bestSame = same, own = address;
        }
        bool found = false;
        // (the other seat's table: the one the seats were bound to then; not told, any table that is not the own)
        const uint64_t his = boundBoard(folder, otherRow->us, 1 - me);
        for (const auto& [address, cells] : boards)
            if ((his ? address == his : address != own) && validField(cells)) other = fieldOf(cells), found = true;
        if (!his) std::cout << "(the board the other seat was bound to is not told by primary-reader.tsv: a table of boards.bin that is not the own)\n";
        if (!found) return std::cout << "boards.bin has no table of the other seat at serial " << otherRow->serial << "\n", 1;
        if (bestSame != 84) std::cout << "(the own table of boards.bin is not as the field of the row: " << bestSame << " of 84 cells the same)\n";
    }

    ai::Params params;
    params.searchThreads = threads;
    {
        std::istringstream in(settings);
        std::string wrong;
        if (!ai::loadParams(in, params, wrong)) return std::cout << "not a setting of the AI: " << wrong << "\n", 2;
    }
    params = ai::agentParams("x3", params);

    std::array<bool, MAX_COLORS + 1> seen{};
    play::realgame::PrimaryObserver14::coloursOf(mine, seen), play::realgame::PrimaryObserver14::coloursOf(other, seen);
    for (int i = 0; i < count; ++i)
        if (shown[size_t(i)])
            for (const Cell c : {sequence[size_t(i)].axis, sequence[size_t(i)].child})
                if (int(c) >= 1 && int(c) <= MAX_COLORS) seen[size_t(c)] = true;

    std::printf("%s, round %d, %d pairs put: tick %ld (%.1f s), serial %lld, %d frames to the pair; the other seat %d pairs put%s\n", folder.c_str(), round, put, at->tick, at->tick / 60.0,
                at->serial, at->wait, otherPairs, otherOperating ? ", a pair in its hands" : "");
    std::printf("   the seat played: %d\n", me);
    std::printf("   garbage shown: %d waiting, %d not to fall yet%s; pairs known:", at->notice, at->pending, at->hidden ? " (garbage fallen that the board has not: left out here)" : "");
    for (int i = put; i < count; ++i) std::printf(" %d%d", int(sequence[size_t(i)].axis), int(sequence[size_t(i)].child));
    std::printf("; colours shown:");
    for (int c = 1; c <= MAX_COLORS; ++c)
        if (seen[size_t(c)]) std::printf(" %d", c);
    std::printf("\n");
    showFields(mine, other);

    for (int pass = 0; pass < 2; ++pass) {
        const bool fitted = pass == 1;
        if ((palette == "asis" && fitted) || (palette == "seen" && !fitted)) continue;
        ai::Observation o;
        o.rules = Rules{}, o.timing = timing, o.players = 2, o.me = me;
        if (fitted) play::realgame::PrimaryObserver14::coloursInPlay(seen, o.rules);
        o.frame = at->tick - 3; // (the counter stands at 10 in the frame the first pair is in the hands: primary_play14.h)
        o.state = RoundState::Playing;
        o.tsumo.pairs.assign(sequence.begin(), sequence.begin() + count);
        Player& self = o.player[me];
        self.field = mine, self.moves = put, self.nextPair = put, self.phase = Phase::Next, self.wait = at->wait < 1 ? 1 : at->wait;
        self.incoming.confirmed = at->notice, self.incoming.pending = at->pending;
        Player& he = o.player[1 - me];
        he.field = other, he.moves = otherPairs;
        if (otherOperating && otherPairs >= 0 && otherPairs < count) {
            he.phase = Phase::Control, he.nextPair = otherPairs + 1, he.colors = sequence[size_t(otherPairs)];
            he.pair = PairControl{}, he.pair.gain = 0;
        } else {
            he.phase = Phase::Next, he.nextPair = otherPairs, he.wait = 13;
        }
        std::printf("-- the colours in play as the AI is told them: %d %d %d %d%s\n", int(o.rules.palette[0]), int(o.rules.palette[1]), int(o.rules.palette[2]), int(o.rules.palette[3]),
                    fitted ? " (those the round has shown)" : " (the rules as they are made)");
        std::printf("   the largest chain a trial drop starts: own %d points, other %d points\n", direct(mine, o.rules, o.timing, params), direct(other, o.rules, o.timing, params));
        Probe probe(params);
        probe.startRound(me, seed);
        probe.tell(o);
    }
    return 0;
}
