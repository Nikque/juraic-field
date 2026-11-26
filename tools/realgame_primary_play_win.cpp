// Generate offline input records from a simulated round.
#include "ai/agents.h"
#include "play/memory/primary_drive14_win.h"
#include "play/memory/primary_madeup14.h"
#include <cstdio>
#include <deque>
#include <iostream>
using namespace play::realgame;
using namespace juraic;

// The trial: see `what` above. An agent that is shown each frame and answers with buttons, as an AI does.
class TrialAgent : public ai::Agent {
public:
    explicit TrialAgent(const std::filesystem::path& file) : out(file) {
        out << "frame\tround\tpair\ttrial\thome\tat_frames\tside\tstate\tx\ty\tupper\tframes_to_lie\tbuttons\theights\n";
    }
    void startRound(int, uint64_t) override { ++round, pair = -1; }
    uint8_t frame(const ai::Observation& o) override {
        const Player& p = o.self();
        if (p.phase != Phase::Control) return last = 0;
        if (p.nextPair - 1 != pair) {
            // (a new pair: its column, and how many frames from lying its sideways tap is sent)
            pair = p.nextPair - 1, ++trial;
            static const int HOMES[5] = {0, 5, 1, 4, 3};
            home = HOMES[trial % 5], offset = (trial / 5) % 5, tapped = false, side = 0;
            // (as many taps as columns to go, whatever is seen meanwhile: buttons that reach the game late would
            //  otherwise be pressed too often; then up to WAIT frames for the pair to be seen there)
            taps = std::abs(home - int(p.pair.x)), way = home > p.pair.x ? BTN_RIGHT : BTN_LEFT, waited = 0, dropping = false;
        }
        int h[W];
        p.field.heights(h);
        const int x = p.pair.x;
        uint8_t b = 0;
        const char* state = "move";
        int toLie = -1;
        if (!dropping && taps > 0) {
            // (taps of one frame toward the column, a frame let go between two)
            if (!(last & way)) b = way, --taps;
        } else if (!dropping && x != home && waited < WAIT) {
            state = "wait", ++waited;
        } else {
            dropping = true;
            state = "drop";
            toLie = 2 * (int(p.pair.y) - h[x]) + (p.pair.upper ? 1 : 0); // (half steps, one a frame with the down button)
            b = BTN_DOWN;
            if (!side) {
                // (to a column no higher, so that the pair could be moved lying; the lower one first)
                const int left = x > 0 && h[x - 1] <= h[x] ? h[x - 1] : 99, right = x < W - 1 && h[x + 1] <= h[x] ? h[x + 1] : 99;
                side = left == 99 && right == 99 ? -1 : left <= right ? BTN_LEFT : BTN_RIGHT;
            }
            if (side > 0 && !tapped && toLie <= offset) b |= uint8_t(side), tapped = true, state = "tap";
        }
        out << o.frame << '\t' << round << '\t' << pair << '\t' << trial << '\t' << home << '\t' << offset << '\t' << side << '\t' << state << '\t' << x << '\t' << int(p.pair.y) << '\t'
            << p.pair.upper << '\t' << toLie << '\t' << int(b) << '\t';
        for (int c = 0; c < W; ++c) out << h[c] << (c + 1 < W ? "," : "\n");
        if (++lines % 60 == 0) out.flush();
        return last = b;
    }

private:
    std::ofstream out;
    static constexpr int WAIT = 10;
    int round = 0, pair = -1, trial = -1, home = 0, offset = 0, side = 0, taps = 0, waited = 0;
    uint8_t way = 0;
    bool tapped = false, dropping = false;
    uint8_t last = 0;
    long lines = 0;
};

// The same on a made-up round of this program's engine (play/memory/primary_madeup14.h), for trying the trial and the
// scripts that read its records before the real game: `program` "sim" plays so, the other side an AI of random
// placements; `turns` then says whether the round has the rules of the other model (1) or not (0), and `seconds` is
// its frames. No program is read and no pad is plugged in.
static int simulated(const std::filesystem::path& folder, int frames, const std::wstring& what, int delay, bool otherRules) {
    std::ofstream record(folder / L"primary-play.tsv");
    PrimaryDrive14::recordHead(record);
    std::unique_ptr<ai::Agent> own;
    if (what == L"trial") own = std::make_unique<TrialAgent>(folder / L"trial.tsv");
    else own = ai::makeAgent("random");
    auto other = ai::makeAgent("random");
    auto* placing = dynamic_cast<ai::PlacementAgent*>(own.get());
    uint64_t seed = 1;
    long played = 0;
    std::string lastField;
    while (played < frames) {
        Round round;
        round.start(Rules{}, seed, 2, otherRules ? OTHER_MODEL_TIMING : DEFAULT_TIMING);
        other->startRound(0, seed);
        // (the player as the play on the real game makes it: the agent prepared so, the real game's rules - with the
        //  cancel for the ways over soonest - and its frame counts)
        const std::unique_ptr<PrimaryPlayer14> made = makePrimaryPlayer14(*own, 1, what == L"trial" ? 0 : delay, what == L"cancel");
        PrimaryPlayer14& player = *made;
        player.seed = seed++;
        std::deque<uint8_t> pad(size_t(delay), 0);
        uint8_t command = 0;
        for (; played < frames && round.state == RoundState::Playing; ++played) {
            pad.push_back(command);
            uint8_t buttons[2]{other->frame(ai::observe(round, 0)), pad.front()};
            pad.pop_front();
            PlayerEvents events[2];
            round.step(buttons, events);
            const PrimarySample14 s = madeUpSample14(round);
            const std::array<int, 2> notice{round.player[0].incoming.confirmed, round.player[1].incoming.confirmed};
            command = player.frame(s, notice);
            // The row describes the current pose and the effective input for the next game frame.
            const uint8_t nextApplied = pad.empty() ? command : pad.front();
            PrimaryDrive14::recordRow(record, s.observedUs, s, 1, player, placing ? placing->targetPlacement() : -1, command, command, lastField, 0, 0, nextApplied);
        }
    }
    std::cout << "a made-up round was played; nothing was read and no pad was plugged in\n";
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    const std::wstring what = argc >= 5 ? argv[4] : L"";
    if (argc < 5 || argc > 7 || (what != L"trial" && what != L"plain" && what != L"cancel")) {
        std::cout << "usage: realgame_primary_play sim <new folder> <frames> trial|plain|cancel [delay] [turns]\n";
        return 2;
    }
    const std::filesystem::path folder = argv[2];
    int seconds = 0, delay = 0, turns = 1;
    try {
        seconds = std::stoi(argv[3]);
        if (argc >= 6) delay = std::stoi(argv[5]);
        if (argc >= 7) turns = std::stoi(argv[6]);
    } catch (...) {
        seconds = 0;
    }
    const bool sim = std::wstring(argv[1]) == L"sim";
    if (!sim || seconds < 1 || delay < 0 || delay > 9 || std::filesystem::exists(folder)) {
        std::cout << "the folder must not exist yet, the frame count must be positive, the delay 0 to 9\n";
        return 2;
    }
    std::filesystem::create_directories(folder);
    return simulated(folder, seconds, what, delay, turns != 0);
}
