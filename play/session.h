#pragma once
// What the game plays, without a window or a device of the system: a round with what is drawn of it (Game), toko,
// a match on one machine with its AI seats (Versus), the viewer of a replay, and the two checks that run without a
// window (--check-replay, --net-test). play/main.cpp gives them the buttons of the frame and draws them.
#include "../ai/agents.h"
#include "netplay.h"
#include "replay.h"
#include "settings.h"
#include "udp.h"
#include "view_state.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

namespace play {
using namespace juraic;

// ---- a round being played, recorded and drawn ------------------------------------------
struct Game {
    Round round;
    View view[2];
    RoundLog log;
    // The replay of a match of two machines (Replay::linkDelay): its rounds are played with the garbage late, both
    // seats in step; `round` is then a copy of the linked round after every frame.
    std::unique_ptr<LinkedRound> linked;

    // timing: the frame counts of now, or those a replay was recorded with (Replay::timing).
    void start(const Rules& rules, uint64_t seed, int players, const GameField* board = nullptr, const Timing& timing = DEFAULT_TIMING, int linkDelay = 0) {
        round.start(rules, seed, players, timing);
        linked.reset();
        if (linkDelay > 0 && players == 2) linked = std::make_unique<LinkedRound>(), linked->start(rules, seed, linkDelay, timing);
        log = RoundLog{};
        log.seed = seed;
        if (board) round.player[0].field = *board, log.hasBoard = true, log.board = *board;
        for (int i = 0; i < 2; ++i) view[i].reset(round.player[i]);
    }
    // A round of a replay as it was recorded: its seed, the board it began on if it had one, and - a match of two
    // machines - the delay its garbage travelled with. Whatever plays a replay starts its rounds here.
    void startReplay(const Replay& replay, const RoundLog& of) {
        start(replay.settings.rules, of.seed, replay.players, of.hasBoard ? &of.board : nullptr, replay.timing(), replay.linkDelay);
    }
    void step(const uint8_t buttons[2], PlayerEvents events[2]) {
        if (round.state != RoundState::Playing) return;
        log.frames.push_back(uint16_t(buttons[0] | buttons[1] << 8));
        if (linked) {
            for (int i = 0; i < 2; ++i) linked->play(i, buttons[i], events[i]);
            round = linked->round;
        } else {
            round.step(buttons, events);
        }
        for (int i = 0; i < round.players; ++i) view[i].update(round.player[i], events[i]);
    }
};

// Plays random legal placements with plain key presses (self test only).
struct ScriptBot {
    Rng rng{7};
    Placement goal{};
    int moves = -1, taps = 0;
    bool early = false; // true: a direction is held also while no pair is in the hands, by turns (what pre-input takes up)
    uint8_t operator()(const Player& p, int64_t frame) {
        if (p.phase != Phase::Control) return early ? uint8_t(frame / 32 % 3 == 0 ? BTN_LEFT : frame / 32 % 3 == 1 ? BTN_RIGHT : BTN_CW) : uint8_t(0);
        if (p.moves != moves) {
            moves = p.moves, taps = 0;
            const unsigned legal = legalPlacements(p.field);
            int id = 0;
            if (legal) do id = int(rng.below(PLACEMENTS)); while (!(legal >> id & 1));
            goal = placement(id);
        }
        uint8_t b = frame % 3 ? BTN_DOWN : 0;
        if (taps++ % 2 == 0) {
            if (goal.dir == RIGHT && p.pair.dir == UP) b |= BTN_CW;
            if (goal.dir == LEFT && p.pair.dir == UP) b |= BTN_CCW;
            if (goal.dir == DOWN && p.pair.dir != DOWN) b |= BTN_CW;
            if (p.pair.x < goal.x) b |= BTN_RIGHT;
            if (p.pair.x > goal.x) b |= BTN_LEFT;
        }
        return b;
    }
};

struct Prediction {
    int chains = 0, points = 0;         // the placement the pair is over now
    int bestChains = 0, bestPoints = 0; // the best of all placements of this pair
};

// ---- toko -------------------------------------------------------------------
// The start of a move, to go back to: the round, what is drawn of it, and how many frames of buttons were
// recorded by then. Not the record itself: it only grows at its end, so cutting it to that length gives the
// record of then. (A whole Game kept here would have its record copied every frame, and once more for every
// move of the history.)
struct MoveStart {
    Round round;
    View view[2];
    size_t frames = 0;
    void take(const Game& g) {
        round = g.round, view[0] = g.view[0], view[1] = g.view[1];
        frames = g.log.frames.size();
    }
    void give(Game& g) const {
        g.round = round, g.view[0] = view[0], g.view[1] = view[1];
        g.log.frames.resize(frames);
    }
};
struct Toko {
    Game game;
    MoveStart before;               // one frame ago (to save a move's start)
    std::vector<MoveStart> history; // the start of every move
    uint64_t seed = 1;
    bool hasBoard = false; // practice: start from startField
    GameField startField;
    std::wstring boardName;
    int maxChain = 0, lastChain = 0, lastPoints = 0;
    Prediction prediction;
    bool paused = false;

    void start(const Settings& s) {
        game.start(s.rules, seed, 1, hasBoard ? &startField : nullptr);
        history.clear();
        before.take(game);
        maxChain = lastChain = lastPoints = 0;
        prediction = {};
        paused = false;
    }
    static void outcome(const Player& p, const Rules& rules, Placement at, int& chains, int& points) {
        PlayerCore core = p;
        Incoming nobody;
        Rng rng(1);
        const TurnResult t = playTurn(core, nobody, at, p.colors, rules, DEFAULT_TIMING, 0, rng);
        chains = t.chains, points = t.points;
    }
    void predict() {
        const Player& p = game.round.player[0];
        prediction = {};
        if (p.phase != Phase::Control) return;
        const unsigned legal = legalPlacements(p.field);
        const int here = placementId(p.pair.placement());
        if (legal >> here & 1) outcome(p, game.round.rules, p.pair.placement(), prediction.chains, prediction.points);
        for (int id = 0; id < PLACEMENTS; ++id) {
            if (!(legal >> id & 1)) continue;
            int chains, points;
            outcome(p, game.round.rules, placement(id), chains, points);
            if (points > prediction.bestPoints) prediction.bestChains = chains, prediction.bestPoints = points;
        }
    }
    PlayerEvents step(uint8_t buttons) {
        PlayerEvents e[2];
        if (paused || game.round.state != RoundState::Playing) return e[0];
        before.take(game);
        const uint8_t b[2] = {buttons, 0};
        game.step(b, e);
        const Player& p = game.round.player[0];
        if (e[0].spawned) history.push_back(before);
        if (e[0].waveStarted) {
            lastChain = p.chain.chains;
            lastPoints = p.chain.chains == 1 ? p.wave.points : lastPoints + p.wave.points;
            maxChain = std::max(maxChain, lastChain);
        }
        return e[0];
    }
    // Back to the start of the current move; a second press soon after goes one move further back.
    void undo() {
        if (history.empty()) return;
        if (history.size() > 1 && game.round.frame - history.back().round.frame < 40) history.pop_back();
        history.back().give(game); // the recorded buttons go back with it
        history.pop_back();
        before.take(game);
        paused = false;
    }
};

// The name of the AI of a seat (ai/agents.h) for the settings: `style` says which of the two AIs it is - Juraic as it
// is, or the one that hardly pokes -, `level` its level. At the top level they are x3 and x3q; below it lv1 to
// lv35, and lv1q to lv35q.
inline std::string aiOfLevel(int level, int style) {
    return level >= ai::LEVELS ? std::string(style ? "x3q" : "x3") : "lv" + std::to_string(level < 1 ? 1 : level) + (style ? "q" : "");
}

// ---- versus ------------------------------------------------------------------------------
struct Versus {
    // (SetEnd: a set is won and the match goes on - only with Rules::setsNeeded above 1)
    enum State { Ready, Play, RoundEnd, SetEnd, MatchEnd } state = Ready;
    Game game;
    Replay replay; // the rounds played so far
    int wins[2]{}, timer = 0, device[2]{};
    int sets[2]{};     // the sets won so far (wins: the rounds won in the set that is played)
    bool weak = false; // in the round that ended last an AI was short of time (App::judgeThinking): said on the screen
    bool picked[2]{};  // the AI of the seat was changed at the end of the match: it is seated anew for the next
    bool paused = false;
    Rng seeds{1};
    // A seat is played by a person (null) or by an AI.
    std::unique_ptr<ai::Agent> agent[2];
    std::wstring agentName[2];
    Rng agentSeeds{1};
    ai::Observation seen;
    AiLog seatLog[2];       // the AI of each seat as the replay names it, with the seed of its round
    bool notesKept = true;  // the round kept last has its AI's notes (keepNotes)
    int64_t logged[2][5]{}; // what the AI of each seat had counted of its thinking when a round was judged last (App::judgeThinking)

    // name = one of ai/agents.h, or empty for a person. live: the match is played at the speed of
    // the screen (a self-test plays its frames as fast as they come).
    void seat(int i, const std::string& name, const Settings& s, bool live = true) {
        // One choice of the AI takes far longer than a frame (40 x 10, three queues: about 90 ms,
        // at most about 300 seen). So the AI chooses in the pause before
        // its pair appears (twelve frames, thinkAhead) and on a thread of its own: no frame waits
        // for it, and a pair whose choice is not made when it appears is left alone until it is
        // (ai/agent.h, Thinking::Free). In a self-test the frame waits for the choice instead,
        // which gives the buttons of the tools whatever the speed of the machine.
        ai::Params params;
        params.beamWidth = 40, params.beamDepth = 10, params.beamQueues = 3;
        // (a match on the screen: the search of the settings. Automatic = the search the AI is measured
        // with (ai/params.h: 250 wide, 16 deep, eight queues), begun 40 wide and made as wide as the machine
        // has time for, up to that width; its depth and its queues are not cut: against Ama a search 10 deep
        // with three queues won 137 of 240 rounds where the measured one won 168 (10 deep with six queues
        // 147, 16 deep with three 138). By hand = the values set, neither wider nor narrower.)
        const ai::Params measured;
        if (live && s.aiAuto) params.beamDepth = measured.beamDepth, params.beamQueues = measured.beamQueues, params.thinkWidthMax = measured.beamWidth;
        if (live && !s.aiAuto) params.beamWidth = s.aiWidth, params.beamDepth = s.aiDepth, params.beamQueues = s.aiQueues, params.thinkFit = 0, params.thinkPlans = 0;
        // (when a choice is begun: the frames of the settings, or — 0, automatic — as late as this
        // machine's searches still get their full width, which the AI sets for every round
        // (ai/pace.h, chooseAhead). A self-test: 12 frames before the pair.)
        params.thinkAhead = live ? s.aiLead : 12;
        params.x3BuildHold = s.aiBuildHold; // (the AI settings: a placement that builds does not fire the main chain while another is left)
        // A level below the top plays with the search of its level, whatever the settings above say, and never a
        // wider one (ai/level.h; on a machine too slow for it the clock still makes it narrower). The threads below
        // follow its number of queues.
        params = ai::agentParams(name, params);
        // (each queue of the beam search on a thread of its own: the same choice in about a third of
        // the time; on a machine with fewer threads than that, one of them is left to the game)
        const int cores = int(std::thread::hardware_concurrency());
        params.searchThreads = cores > 0 ? std::max(1, std::min(params.beamQueues, cores - 1)) : params.beamQueues;
        // (the automatic search: every thread of the machine but one — but two from eight on —, and
        // three a queue at most. The choices are the same with any number, and since the threads share
        // the work of all queues (ai/build.h, Crew) every one of them helps.)
        if (live && s.aiAuto && cores > 0) params.searchThreads = std::max(1, std::min(3 * params.beamQueues, cores - (cores >= 8 ? 2 : 1)));
        // (set by hand: as many as said. Those beyond one a queue help to value the fields of a layer,
        // ai/build.h; the choices are the same, a wide search takes less time)
        if (live && s.aiThreads > 0) params.searchThreads = s.aiThreads;
        // On a slower machine the searches are cut short rather than the pair kept waiting: they stop
        // 180 ms after the choice was begun, 20 ms before the pair appears. (Not in a
        // self-test: there the buttons must not depend on the machine.)
        // (begun later than 200 ms before the pair, a choice has less: what is left 20 ms before the pair)
        if (live) params.thinkBudget = s.aiLead > 0 ? std::min(s.aiBudget, s.aiLead * 1000 / 60 - 20) : s.aiBudget;
        agent[i] = name.empty() ? nullptr : ai::makeAgent(name, params);
        // (for the replay: what the AI needs to make its choices again, replay.h)
        seatLog[i] = AiLog{name, 0, params.beamWidth, params.beamDepth, params.beamQueues, params.thinkAhead, {}};
        if (ai::X3Agent* x3 = dynamic_cast<ai::X3Agent*>(agent[i].get())) x3->noting = true;
        for (int64_t& held : logged[i]) held = 0;
        if (ai::PlacementAgent* placing = dynamic_cast<ai::PlacementAgent*>(agent[i].get())) {
            placing->thinking = live ? ai::Thinking::Free : ai::Thinking::Thread;
            if (placing->ahead < 1) placing->ahead = 12; // (an AI that does not choose the moment itself)
        }
        // (the AI of the game is called Juraic; in the tools, the replays and the sources it is x3, x3q for the one
        //  that hardly pokes, and lv1 to lv35 and lv1q to lv35q for their levels below the top. The two names on the
        //  screen are 催促（小） for x3 and 催促（極小） for x3q.)
        const bool juraic = name == "x3" || name == "x3q" || ai::levelOf(name);
        const int level = ai::levelOf(name) ? ai::levelOf(name) : ai::LEVELS;
        agentName[i] = juraic ? std::wstring(ai::quietOf(name) ? L"Juraic 催促（極小） Lv " : L"Juraic 催促（小） Lv ") + std::to_wstring(level)
                              : L"AI（" + std::wstring(name.begin(), name.end()) + L"）";
    }
    void startMatch(const Settings& s, uint64_t seed) {
        seeds = Rng(seed);
        agentSeeds = Rng(seed, ai::AGENT_STREAM);
        wins[0] = wins[1] = sets[0] = sets[1] = 0;
        weak = false;
        replay = Replay{s, 2, TIMING_VERSION, {}};
        notesKept = true;
        startRound(s);
    }
    void startRound(const Settings& s) {
        keepNotes();
        game.start(s.rules, seeds.next() % 1000000000ull, 2);
        for (int i = 0; i < 2; ++i) {
            if (!agent[i]) continue;
            agent[i]->startRound(i, seatLog[i].seed = agentSeeds.next());
            // (the moment of its choices in this round, which an AI may set as the round begins: for the replay)
            if (const ai::PlacementAgent* placing = dynamic_cast<const ai::PlacementAgent*>(agent[i].get())) seatLog[i].ahead = placing->ahead;
        }
        state = Ready, timer = 90, paused = false;
    }
    // The round is over (or left): it goes to the replay, with the AI of its seats.
    void keepRound() {
        for (int i = 0; i < 2; ++i)
            if (agent[i]) game.log.ai[i] = seatLog[i];
        replay.rounds.push_back(game.log);
        notesKept = false;
    }
    // The round has ended: its win is counted and it goes to the replay.
    void roundOver() {
        if (game.round.state == RoundState::Won0) ++wins[0];
        if (game.round.state == RoundState::Won1) ++wins[1];
        keepRound();
        state = RoundEnd;
    }
    // After a round: the seat that has won the set with it (the set is counted, here), or -1 while the set goes on.
    int setWon(const Rules& rules) {
        const int seat = wins[0] >= rules.winsNeeded ? 0 : wins[1] >= rules.winsNeeded ? 1 : -1;
        if (seat >= 0) ++sets[seat];
        return seat;
    }
    // What the AI seats decided by the clock in the round kept last, for the replay. Before the
    // next round begins and before the replay is saved, not as the round ends: an AI may be in
    // the middle of a choice then, and this waits for it.
    void keepNotes() {
        if (notesKept || replay.rounds.empty()) return;
        notesKept = true;
        for (int i = 0; i < 2; ++i) {
            ai::X3Agent* x3 = dynamic_cast<ai::X3Agent*>(agent[i].get());
            AiLog& a = replay.rounds.back().ai[i];
            if (!x3 || a.name.empty()) continue;
            for (const ai::X3Agent::Note& n : x3->takeNotes()) a.choices.push_back(AiChoice{n.move, n.pairs, n.id, 0, n.cut, n.widths});
            for (size_t k = 0; k + 1 < x3->lateLog.size(); k += 2)
                for (AiChoice& c : a.choices)
                    if (c.move == x3->lateLog[k]) c.late = x3->lateLog[k + 1];
            // (a second choice of a move, the note after that of its first: the frames after the pair appeared at which it came)
            for (size_t k = 0; k + 1 < x3->againLog.size(); k += 2)
                for (size_t c = a.choices.size(); c-- > 1;)
                    if (a.choices[c].move == x3->againLog[k] && a.choices[c - 1].move == x3->againLog[k]) {
                        a.choices[c].late = x3->againLog[k + 1];
                        break;
                    }
        }
    }
    // The buttons of an AI seat for the frame that is played next.
    uint8_t agentButtons(int i) {
        ai::observe(game.round, i, seen);
        return agent[i]->frame(seen);
    }
};

// ---- watching a replay --------------------------------------------------------------------
struct Viewer {
    Replay replay;
    Game game;
    size_t round = 0, frame = 0;
    bool paused = false, finished = false;
    std::wstring name;
    int wins[2]{};        // rounds won so far in the set on the screen, for the stars between the fields
    int sets[2]{};        // the sets that are over (a replay with Rules::setsNeeded above 1)
    bool counted = false; // the round on the screen is in `wins`

    void startRound() {
        game.startReplay(replay, replay.rounds[round]);
        frame = 0, counted = false;
    }
    // From the first round again.
    void restart() {
        round = 0, finished = false, wins[0] = wins[1] = sets[0] = sets[1] = 0;
        startRound();
    }
    // On to the next round. A set that is over is counted, and the rounds are counted anew (Versus::setWon).
    void nextRound() {
        const Rules& rules = replay.settings.rules;
        if (rules.setsNeeded > 1 && std::max(wins[0], wins[1]) >= rules.winsNeeded) ++sets[wins[1] > wins[0]], wins[0] = wins[1] = 0;
        ++round;
        startRound();
    }
    // The round on the screen is over: its winner gets his star, as in the match (Versus::roundOver).
    void countRound() {
        const RoundState s = game.round.state;
        if (counted || s == RoundState::Playing) return;
        counted = true;
        if (s == RoundState::Won0) ++wins[0];
        if (s == RoundState::Won1) ++wins[1];
    }
};

// A replay played from its buttons without a window: for every round one line that tells how it ended
// (roundSummary). --check-replay writes them; a self test compares them with the rounds as they were played.
inline std::string replayResult(const Replay& replay) {
    const auto game = std::make_unique<Game>();
    std::string result;
    for (const RoundLog& log : replay.rounds) {
        game->startReplay(replay, log);
        for (const uint16_t f : log.frames) {
            const uint8_t buttons[2] = {uint8_t(f & 255), uint8_t(f >> 8)};
            PlayerEvents e[2];
            game->step(buttons, e);
        }
        result += roundSummary(game->round) + '\n';
    }
    return result;
}

// Plays a replay without a window, the way the viewer does (loadReplay, then
// Game::step with the recorded buttons), and writes how every round ended to
// <file>.result. Returns 0; 7 when the file cannot be read; 8 when
// <file>.expected exists and says something else (tools/ai_match writes it).
inline int checkReplay(const std::filesystem::path& file) {
    Replay replay;
    if (!loadReplay(file, replay)) return 7;
    const std::string result = replayResult(replay);
    std::filesystem::path resultFile = file, expectedFile = file;
    std::ofstream(resultFile += ".result", std::ios::binary) << result;
    std::ifstream expected(expectedFile += ".expected", std::ios::binary);
    if (!expected) return 0;
    std::stringstream text;
    text << expected.rdbuf();
    return text.str() == result ? 0 : 8;
}

// A match over UDP without a window, a script in the own seat (--net-test): the host listens on this machine only,
// the guest calls. Ticks come every two milliseconds instead of sixty a second. Writes how every round ended and
// what both seats pressed to `out`; the files of the two programs must be equal. Returns 0 when the match was
// played to its end.
inline int netTest(NetPlay& n, int host, const std::string& address, int port, const std::filesystem::path& out) {
    auto line = std::make_unique<UdpLine>();
    if (host ? !line->listen(port, host) : !line->call(address, port)) return 2;
    Settings s;
    s.rules.winsNeeded = 2;
    if (host) n.host(std::move(line), s, "host", 4242, 0);
    else n.join(std::move(line), s, "guest");
    n.peer.silenceLimit = 5000;
    ScriptBot bot;
    bot.rng = Rng(host ? 7 : 99);
    const auto nothing = [](int, const PlayerEvents&) {};
    using Clock = std::chrono::steady_clock;
    const Clock::time_point begun = Clock::now();
    int linger = 1500; // (after the end: the other program may still need the last buttons)
    for (int64_t tick = 0; linger > 0; ++tick) {
#ifdef _WIN32
        while (Clock::now() < begun + std::chrono::milliseconds(2 * tick)) Sleep(1);
#else
        while (Clock::now() < begun + std::chrono::milliseconds(2 * tick)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
        if (tick > 2 * 60 * 500) return 3; // (two minutes with nobody, or a match that does not end)
        if ((n.stage == NetPlay::Ready || n.stage == NetPlay::RoundEnd || n.stage == NetPlay::SetEnd) && n.timer > 2) n.timer = 2;
        n.tick(bot(n.link.round.player[n.seat()], tick), nothing);
        if (n.stage == NetPlay::Over && linger == 1500) return 4;
        if (n.stage == NetPlay::Over) break; // (the other program has left after the end of the match)
        if (n.stage == NetPlay::MatchEnd) --linger;
    }
    std::ofstream file(out, std::ios::binary);
    file << "delay " << n.peer.match.delay << " rounds " << n.replay.rounds.size() << " sets " << n.sets[0] << ' ' << n.sets[1] << '\n';
    for (const RoundLog& log : n.replay.rounds) {
        uint64_t h = log.seed;
        for (const uint16_t b : log.frames) h = (h ^ b) * 0x9E3779B97F4A7C15ull, h ^= h >> 29;
        file << "round seed " << log.seed << " frames " << log.frames.size() << " buttons " << std::hex << h << std::dec << '\n';
    }
    n.leave();
    return 0;
}

} // namespace play
