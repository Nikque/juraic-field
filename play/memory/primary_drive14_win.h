#pragma once
// Memory first, on the real game: the reader of primary_runtime14_win.h and the player of primary_play14.h on a thread
// of their own. Each new frame of the game's counter is read (a millisecond after the game made it, at most), the AI is
// shown it, and the buttons it answers go to the pad at once - through `press`, the only way anything goes to the game
// (the virtual pad; nothing is written to the program, which is opened for reading and asking only).
//
// The buttons are those of one frame (taps of a frame each): a pad set a millisecond after a frame of the game has the
// rest of that frame to be read by the game in the next.
#include "../../ai/pace.h"
#include "primary_play14.h"
#include "primary_runtime14_win.h"
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>

namespace play::realgame {

class PrimaryDrive14 {
public:
    struct Setup {
        std::wstring program;         // the real game's program (the whole path)
        int me = 1;                   // the seat played
        int delay = 0;                // frames a button takes to reach the game beyond its next frame
        bool cancel = false;          // the ways over soonest, with the cancels of the other model (primaryTiming14)
        bool turnsSwapped = true;     // the two turns are sent the other way round (the real game turns its pair so)
        std::filesystem::path record; // a folder for the record of the play (empty: none)
        uint64_t seed = 1;            // of the agent's rounds
        // With a record: the bytes of each seat's field and player objects with every frame (PrimaryRuntime14::Raw14),
        // into raw.bin of the folder until it is this long - for finding where the score, the waiting garbage and
        // the chain are kept; boards.bin (PrimaryRuntime14::Board14) ends at the same length, and wide.bin, opened
        // with them, at WIDE_BYTES. 0: none of the three.
        uint64_t rawBytes = 0;
        // The pairs beyond the window are read from the game's own list and shown to the agent (readListedPairs14).
        bool listedPairs = false;
        bool garbageRandom = false;
        bool opponentInput = false;
    };
    struct Status {
        PrimaryRuntime14::Status reader;
        bool cpuSeat = false;      // most recent verified controller of the selected seat
        bool round = false;       // a round is being played, as the memory shows it
        long rounds = 0, frames = 0, skipped = 0, filled = 0, recovered = 0, corrections = 0, repaired = 0;
        int delay = 0, measured = -1; // the frames a button is reckoned to take, and as last measured (PrimaryPlayer14::measured)
        long delayChanges = 0;        // times the delay was set to what was measured
        int pairs = 0;            // pairs the seat has put in this round
        int target = -1;          // the placement the pair in play is brought to
        uint8_t held = 0;         // the buttons held (as the AI names them)
        uint64_t slowestUs = 0;   // the longest time from a sample read to its buttons set on the pad
        // The AI's thinking so far, as X3Pace counts it: its choices, the searches the clock stopped, those made
        // narrower to end in time, the choices whose plans knew two pairs only, the second choices made and those the
        // clock left poor (all 0 for an agent that does not count them).
        std::array<int64_t, 6> thinking{};
        std::array<int, 2> notice{};
    };
    // Sets the buttons on the pad (game/control.h Button bits, the turns as they go out). Called on the thread of the play.
    std::function<void(uint8_t)> press;
    // The garbage waiting above each field, as far as known (empty: none known). Called on the thread of the play.
    std::function<std::array<int, 2>()> notice;
    // The trial of buttons set late, while set: a side button or a turn newly pressed is set 0, 2, .. 14 ms after its
    // frame was read, one after the other. The record (us - read_us of a row beside where the pair is in the game's
    // next frame) then shows how late the buttons may be set and still be taken in the next frame, whichever way they
    // go to the game. (Taps set too late are lost, and the player puts the pair right.)
    std::atomic<bool> lateTrial{false};
    // The pad was set by someone else meanwhile (the driver of the pictures took over and gave it back): the buttons of the
    // next frame are set whatever was sent before.
    void resend() { resend_ = true; }

    ~PrimaryDrive14() { stop(); }
    // The agent begins to play the seat. (Its taps a frame each; the second choice of a pair asked for in the air too,
    // the third pair being shown only as the pair appears.)
    void start(const Setup& setup, std::unique_ptr<juraic::ai::Agent> agent) {
        stop();
        setup_ = setup;
        primaryListedPairs14 = setup.listedPairs;
        primaryGarbageRandom14 = setup.garbageRandom;
        primaryInputSeat14 = setup.opponentInput ? 1 - setup.me : -1;
        prepare(agent.get(), setup.cancel);
        {
            std::lock_guard<std::mutex> hold(lock_);
            stop_ = false, agent_ = std::move(agent), next_.reset(), status_ = {};
        }
        if (!setup_.record.empty()) {
            std::error_code ignored;
            std::filesystem::create_directories(setup_.record, ignored);
            recordedField_.clear();
            record_.open(setup_.record / L"primary-play.tsv");
            recordHead(record_);
            faults_.open(setup_.record / L"primary-faults.tsv"), faultLines_ = 0;
            faults_ << "us\ttick\tseat\tcode\toperating\tpairs\tphase\tfield_state\tplayer_state\tcolour0\tcolour1\tpose_bytes\n";
            // (the AI's thinking: a row for each round, written as the next one begins or a second after it is over)
            thinking_.open(setup_.record / L"primary-thinking.tsv");
            thinking_ << "us\trounds\tchoices\tstopped_by_clock\tmade_narrower\ttwo_pairs_known\tsecond_made\tsecond_poor"
                         "\tall_switched\tall_no_path\tall_held\tall_again_ways\tall_ahead_used\tall_ahead_missed\tall_faster_ways\tall_frames_saved"
                         "\tall_again_asked\tall_again_nothing_new\tall_again_no_speed\tall_again_no_time\tall_again_need_ms\tall_again_have_ms"
                         "\tahead_frames\tbudget_ms\tfirst_width\tall_rival_searches\tall_rival_reused\tall_rival_missed\tall_level\tall_revised\n";
            // (the reader: a row whenever it has scanned, bound or let go of the seats)
            reader_.open(setup_.record / L"primary-reader.tsv"), readerLine_.clear();
            reader_ << "us\tstate\tscans\tbinds\trebinds\tboards\tscan_ms\tscan_bytes\tplayer0\tfield0\tboard0\tplayer1\tfield1\tboard1\toperate0\toperate1\tbind_note\tabsent_pages\tpaged_in\trecord_dropped\n";
            if (setup_.rawBytes) {
                raw_.open(setup_.record / L"raw.bin", std::ios::binary);
                rawWritten_ = 0;
                std::ofstream(setup_.record / L"raw-format.txt")
                    << "raw.bin: records of 1040 bytes, little endian: serial (8), frame counter (4), seat (1), field object read (1: 0x300 bytes, 2: its first 0x2d4 only, 0: not), "
                       "player object read (1), a zero (1), the field object's first 0x300 bytes, the player object's first 0x100 bytes.\n"
                       "A record for each seat with every new sample (the serial of primary-play.tsv).\n"
                       "boards.bin: records of 472 bytes, little endian: serial (8), frame counter of seat 0 (4), of seat 1 (4), which of the tables of the board's cell format "
                       "the last scan found (2), of how many (2), read (1), three zeros, the table's address (8, that of its visible top row: the bytes begin 64 before it), "
                       "its 440 bytes (14 rows of 32 bytes from the 14th row down, six cells of 4 bytes in each).\n"
                       "A record for a table whenever it is not as it was in its last record (and its first one after each scan).\n";
                boards_.open(setup_.record / L"boards.bin", std::ios::binary);
                boardsWritten_ = 0;
                std::ofstream(setup_.record / L"wide-format.txt")
                    << "wide.bin: records of " << (24 + PrimaryRuntime14::Wide14::FIELD + PrimaryRuntime14::Wide14::PLAYER)
                    << " bytes, little endian: serial (8), frame counter (4), seat (1), a zero (1), which 4 KiB pieces of the bytes from the field object on were read (2: bit k "
                       "the k-th piece, bit 15 the player object), how many of those bytes were taken (2; the rest are zeros), six zeros, " << PrimaryRuntime14::Wide14::FIELD
                    << " bytes from the start of the field object on (the object, and what lies behind it as far as the board of the seat), the first "
                    << PrimaryRuntime14::Wide14::PLAYER << " bytes of the player object.\n"
                       "A record for a seat in every frame while garbage falls on it and for 40 frames after, when what it is doing changes, and every 20th frame.\n";
                wide_.open(setup_.record / L"wide.bin", std::ios::binary);
                wideWritten_ = 0;
            }
        }
        runtime_.raw(raw_.is_open());
        runtime_.boards(boards_.is_open());
        runtime_.wide(wide_.is_open());
        runtime_.start(setup_.program);
        recordPlay_ = record_.is_open();
        recordThinking_ = thinking_.is_open();
        if (recordPlay_ || recordThinking_ || reader_.is_open() || faults_.is_open() ||
            raw_.is_open() || boards_.is_open() || wide_.is_open()) {
            recordStop_ = false;
            recordDropped_ = 0;
            recorder_ = std::thread([this] { writeRecords(); });
        }
        worker_ = std::thread([this] { run(); });
    }
    // Another agent from the next pair on (the level changed while the AI plays). cancel: as Setup::cancel, for this
    // agent (a level and its Ex differ in nothing else).
    void replaceAgent(std::unique_ptr<juraic::ai::Agent> agent, bool cancel) {
        prepare(agent.get(), cancel);
        std::lock_guard<std::mutex> hold(lock_);
        next_ = std::move(agent), nextCancel_ = cancel;
    }
    void stop() {
        {
            std::lock_guard<std::mutex> hold(lock_);
            stop_ = true;
        }
        if (worker_.joinable()) worker_.join();
        runtime_.stop();
        if (recorder_.joinable()) {
            {
                std::lock_guard<std::mutex> hold(recordLock_);
                recordStop_ = true;
            }
            recordReady_.notify_one();
            recorder_.join();
        }
        if (record_.is_open()) record_.close();
        if (raw_.is_open()) raw_.close();
        if (boards_.is_open()) boards_.close();
        if (wide_.is_open()) wide_.close();
        if (faults_.is_open()) faults_.close();
        if (thinking_.is_open()) thinking_.close();
        if (reader_.is_open()) reader_.close();
        std::lock_guard<std::mutex> hold(lock_);
        agent_.reset(), next_.reset();
    }
    bool running() const {
        std::lock_guard<std::mutex> hold(lock_);
        return !stop_;
    }
    Status status() const {
        Status s;
        {
            std::lock_guard<std::mutex> hold(lock_);
            s = status_;
        }
        s.reader = runtime_.status();
        return s;
    }
    std::optional<PrimarySample14> sample() const { return runtime_.sample(); }

    static void prepare(juraic::ai::Agent* agent, bool cancel) {
        if (auto* placing = dynamic_cast<juraic::ai::PlacementAgent*>(agent)) {
            placing->tapHold = 1, placing->holdSecond = true, placing->holdLevel = true, placing->waitQuickLock = true, placing->askInAir = true, placing->fastestWays = cancel;
            // The search for the way over soonest is made ahead, on a thread of its own, once the placement of the pair
            // to come is chosen (PlacementAgent::fastestAhead). One made in the frame the buttons are for - the pair
            // put right in the air, a placement chosen as the pair appears - is kept short: a search of 4000 states
            // took up to 19 ms on the fields of the records of the real game, and buttons set 12 ms after a frame did
            // not reach the game in it. A search that gives up leaves the straight way.
            placing->fastestStates = PRIMARY_FASTEST_NOW14, placing->fastestAhead = PRIMARY_FASTEST_AHEAD14;
            // The way made ahead is kept while a second choice is on its way and that way itself goes straight down
            // (records of the real game: a pair in five went to another placement in the air, and every pair held
            // straight down for a second choice had its way planned again within the frame - mostly the straight
            // one). A way planned in the air is searched once more as the one made ahead is, from where the pair is
            // a few frames on.
            placing->holdKeepsWay = cancel, placing->fastestAgain = cancel ? PRIMARY_FASTEST_AGAIN14 : 0;
            // (a choice dropped stops its searches; a first choice that comes late is asked for again in the air; the way
            //  over soonest is searched on while a second choice is awaited; with the pairs beyond the window read, the
            //  choice is made once more as the pair appears, by the fields as they are then)
            placing->cutStale = true, placing->retryInAir = true, placing->searchWhileSecond = cancel;
            placing->againFresh = primaryListedPairs14.load();
        }
    }
    // The record of the play: a row for every frame played (also written by tools that play made-up rounds).
    static void recordHead(std::ostream& to) {
        to << "us\tread_us\tserial\ttick\toperating\tpairs\tx\ty\tupper\tdir\tphase\tactive\tshown_phase\tshown_wait\tfore_frame\tfore_phase\tfore_wait\tfore_x\tfore_y\tfore_upper\tfore_dir"
              "\ttarget\tbuttons\tsent\tcorrections\trepaired\trecovered\tskipped\tfilled\tother_operating\tother_pairs\tplaced\tfield_state\tplayer_state\texp_phase\texp_x\texp_y\texp_upper"
              "\texp_dir\tnotice\tpending\tpieces\tgarbage_pieces\tsixth\tchain\tother_chain\thidden\thidden_sure\tcolumns\tother_columns\ttray\tother_tray\tnext\twindow\tfield\tinput_delay\tmeasured_delay\tmeasured_run"
              "\ttray_flying\ttray_pending\ttray_confirmed\tother_tray_flying\tother_tray_pending\tother_tray_confirmed\tother_shown_phase\tother_shown_wait\tbonus\tother_bonus\tshown_bonus\tother_shown_bonus\tgarbage_random\tother_garbage_random\tother_input\tscore\tscore_pool\tscore_wave\tother_score\tother_score_pool\tother_score_wave\tdriver_begin_us\tdriver_reply_us\n";
    }
    // (lastField: the own field as the last row had it - a row has "=" for a field that is the same; 84 digits, the 14th row first)
    static void recordRow(std::ostream& to, uint64_t now, const PrimarySample14& s, int me, const PrimaryPlayer14& player, int target, uint8_t buttons, uint8_t out, std::string& lastField,
                          uint64_t driverBegin = 0, uint64_t driverReply = 0) {
        if (!s.seat[size_t(me)]) return;
        const PrimarySeat14& seat = *s.seat[size_t(me)];
        const OwnedPublicPose14& p = seat.pose;
        const auto three = [](const std::array<juraic::Pair, 3>& v) {
            std::string joined;
            for (const juraic::Pair& pair : v) joined += std::to_string(int(pair.axis)) + std::to_string(int(pair.child)) + (&pair == &v[2] ? "" : " ");
            return joined;
        };
        // (the window with no pair in the hands: its three pairs when it stands; else "!", how far its read came
        //  (PrimaryWindow14::Stage), the three numbers that say whether it stands, and its three records as read)
        std::string window = "-";
        if (seat.windowKnown) window = three(seat.window);
        else if (!p.operating && seat.windowRead.stage > PrimaryWindow14::Read) {
            const PrimaryWindow14& w = seat.windowRead;
            char text[200];
            std::snprintf(text, sizeof text, "!%d:%x,%x,%x:%x.%x.%x,%x.%x.%x,%x.%x.%x", int(w.stage), w.slide[0], w.slide[1], w.slide[2], w.records[0][0], w.records[0][1],
                          w.records[0][2], w.records[1][0], w.records[1][1], w.records[1][2], w.records[2][0], w.records[2][1], w.records[2][2]);
            window = text;
        }
        std::string cells;
        for (int y = 13; y >= 0; --y)
            for (int x = 0; x < 6; ++x) cells.push_back(char('0' + int(y == 13 ? seat.field.row14[size_t(x)] : seat.field.f.get(x, y))));
        const juraic::Player& shown = player.shown().self();
        const juraic::Player& fore = player.foreseen().self();
        const auto& other = s.seat[size_t(1 - me)];
        to << now << '\t' << s.observedUs << '\t' << s.serial << '\t' << p.count108 << '\t' << p.operating << '\t' << p.lockedPairs << '\t' << p.x << '\t' << p.y << '\t' << p.upper << '\t'
           << p.dir << '\t' << p.phase << '\t' << int(p.fieldActive) << '\t' << int(shown.phase) << '\t' << shown.wait << '\t' << player.foreseen().frame << '\t' << int(fore.phase)
           << '\t' << fore.wait << '\t' << int(fore.pair.x) << '\t' << int(fore.pair.y) << '\t' << fore.pair.upper << '\t' << int(fore.pair.dir) << '\t' << target << '\t' << int(buttons)
           << '\t' << int(out) << '\t' << player.corrections() << '\t' << player.repaired() << '\t' << player.recovered << '\t' << player.skipped << '\t' << player.filled << '\t'
           << (other ? int(other->pose.operating) : -1) << '\t' << (other ? int(other->pose.lockedPairs) : -1) << '\t' << int(seat.placed) << '\t' << p.fieldState << '\t'
           << p.playerState << '\t';
        // (the own side as the model has it after the frame these buttons are for: what the next row is held against)
        if (const auto& e = player.expected()) to << int(e->phase) << '\t' << int(e->pair.x) << '\t' << int(e->pair.y) << '\t' << e->pair.upper << '\t' << int(e->pair.dir);
        else to << "-1\t-1\t-1\t0\t-1";
        // (the garbage waiting as shown, what cannot fall yet of it; the pieces the game counts in the field and those of
        //  them that are garbage, the height of the sixth column, the garbage the chain of the field - and of the other
        //  seat's - makes; the pieces counted beyond the board, and whether their columns are all known)
        to << '\t' << shown.incoming.confirmed << '\t' << shown.incoming.pending << '\t' << (seat.counted ? seat.pieces : -1) << '\t' << (seat.counted ? seat.garbagePieces : -1) << '\t'
           << (seat.counted ? seat.sixthHeight : -1) << '\t' << (seat.counted ? int64_t(seat.chainGarbage) : -1) << '\t' << (other && other->counted ? int64_t(other->chainGarbage) : -1) << '\t'
           << player.observer.hidden(me).pieces << '\t' << int(player.observer.hidden(me).sure);
        // (the pieces the game counts in each column of the field, and of the other seat's: "-" where they were not read)
        const auto six = [](const PrimarySeat14& of) {
            if (!of.columnsKnown) return std::string("-");
            std::string counts;
            for (size_t x = 0; x < 6; ++x) counts += (x ? "." : "") + std::to_string(int(of.columnPieces[x]));
            return counts;
        };
        to << '\t' << six(seat) << '\t' << (other ? six(*other) : std::string("-"));
        // (the garbage waiting above the field and above the other seat's, as the game's own object has it: -1 not read)
        to << '\t' << (seat.noticeKnown ? seat.notice : -1) << '\t' << (other && other->noticeKnown ? other->notice : -1);
        to << '\t' << (seat.pairsKnown ? three(seat.pairs) : "-") << '\t' << window << '\t' << (cells == lastField ? "=" : cells) << '\t' << player.delay() << '\t' << player.measured() << '\t' << player.measuredRun();
        // (the game's own account of the garbage waiting above the field, and above the other seat's: on its way,
        //  not to fall yet, to fall; -1 not read)
        const auto account = [&to](const PrimarySeat14* of) {
            if (of && of->trayKnown) to << '\t' << of->trayFlying << '\t' << of->trayPending << '\t' << of->trayConfirmed;
            else to << "\t-1\t-1\t-1";
        };
        account(&seat), account(other ? &*other : nullptr);
        // (the other seat as the AI is shown it: its phase - juraic::Phase - and the frames that phase still lasts)
        to << '\t' << int(player.shown().player[size_t(1 - me)].phase) << '\t' << player.shown().player[size_t(1 - me)].wait;
        // (the all-clear bonus waiting for the seat's next chain and for the other seat's, as read - -1 not read - and as the AI is shown them)
        to << '\t' << (seat.bonusKnown ? int(seat.bonusWaits) : -1) << '\t' << (other && other->bonusKnown ? int(other->bonusWaits) : -1) << '\t'
           << int(player.shown().self().scoring.allClear) << '\t' << int(player.shown().player[size_t(1 - me)].scoring.allClear);
        to << '\t' << (seat.garbageRandom ? int64_t(*seat.garbageRandom) : -1)
           << '\t' << (other && other->garbageRandom ? int64_t(*other->garbageRandom) : -1)
           << '\t' << (other && other->input ? int(*other->input) : -1);
        const auto score = [&to](const PrimarySeat14* of) {
            if (of && of->score) to << '\t' << of->score->total << '\t' << of->score->pool << '\t' << of->score->lastWave;
            else to << "\t-1\t-1\t-1";
        };
        score(&seat), score(other ? &*other : nullptr);
        to << '\t' << driverBegin << '\t' << driverReply;
        to << '\n';
        lastField = cells;
    }
    static constexpr int DELAY_RUN = 3, DELAY_MOST = 12;
    // Buttons set later than this after the sample they answer was read do not reach the game in its next frame
    // (PrimaryPlayer14::undelivered; records of the real game: buttons set 12 ms after a frame did not reach it).
    static constexpr uint64_t LATE_US = 12000;
    static uint8_t swapTurns(uint8_t b) {
        return uint8_t((b & ~(juraic::BTN_CW | juraic::BTN_CCW)) | (b & juraic::BTN_CW ? juraic::BTN_CCW : 0) | (b & juraic::BTN_CCW ? juraic::BTN_CW : 0));
    }

private:
    Setup setup_;
    PrimaryRuntime14 runtime_;
    mutable std::mutex lock_;
    std::thread worker_;
    bool stop_ = true;
    std::unique_ptr<juraic::ai::Agent> agent_, next_;
    bool nextCancel_ = false; // Setup::cancel for next_ (taken into setup_ on the thread of the play, with the agent)
    Status status_;
    std::ofstream record_, raw_, faults_, reader_, boards_, wide_, thinking_;
    std::thread recorder_;
    std::mutex recordLock_;
    std::condition_variable recordReady_;
    bool recordPlay_ = false, recordThinking_ = false, recordStop_ = true;
    long recordDropped_ = 0;
    std::vector<std::pair<bool, std::string>> recordLines_;
    bool queueRecord(bool thinking, std::string line) {
        std::lock_guard<std::mutex> hold(recordLock_);
        if (recordLines_.size() >= 8192) {
            ++recordDropped_;
            return false;
        }
        recordLines_.emplace_back(thinking, std::move(line));
        if (recordLines_.size() == 32) recordReady_.notify_one();
        return true;
    }
    std::array<int64_t, 6> thought_{}, thoughtAll_{}; // X3Pace's counters as last noted, and what was noted in all
    long thoughtRounds_ = 0;
    std::array<int, 2> trayLast_{-1, -1}; // the garbage waiting above each seat as its object last had it (-1: not read)
    // Notes the AI's thinking since it was last noted (X3Pace's counters). Called on the thread of the play while no
    // search of the agent runs: as a round begins (the agent has ended its searches), and a second after a round is
    // over (no choice is asked for then).
    void noteThinking() {
        const auto* pace = dynamic_cast<const juraic::ai::X3Pace*>(agent_.get());
        if (!pace) return;
        const std::array<int64_t, 6> now{pace->thinkCount, pace->cutSearches, pace->narrowSearches, pace->shortPlans, pace->againMade, pace->againPoor};
        if (now[0] < thought_[0]) thought_ = {}; // (another agent took over: counted from its first choice)
        std::array<int64_t, 6> d{};
        for (size_t i = 0; i < now.size(); ++i) d[i] = now[i] - thought_[i];
        thought_ = now;
        if (d[0] == 0) return;
        for (size_t i = 0; i < d.size(); ++i) thoughtAll_[i] += d[i];
        ++thoughtRounds_;
        {
            std::lock_guard<std::mutex> hold(lock_);
            status_.thinking = thoughtAll_;
        }
        if (!recordThinking_) return;
        std::ostringstream note;
        note << PrimaryRuntime14::clockUs() << '\t' << thoughtRounds_;
        for (const int64_t v : d) note << '\t' << v;
        // (the ways of the pairs, the agent's counts so far: pairs that went to their second choice in the air and second
        //  choices no way led to, frames held straight down for one, ways searched once more in the air and taken, ways
        //  taken from a search made ahead and ways searched in the frame, ways the search made shorter and by how many frames)
        const auto* placing = dynamic_cast<const juraic::ai::PlacementAgent*>(agent_.get());
        if (placing)
            note << '\t' << placing->againSwitched << '\t' << placing->againNoPath << '\t' << placing->againHeld << '\t' << placing->againWays << '\t' << placing->ways().aheadUsed
                      << '\t' << placing->ways().aheadMissed << '\t' << placing->ways().fasterWays << '\t' << placing->ways().framesSaved;
        // (the second choices asked for and why they were not begun; when a choice is begun, its time and the width its
        //  search of the own field got, as they stand now; the searches of the other field made, taken from an earlier
        //  choice, and not done in the time of the choice)
        note << '\t' << pace->againAsked << '\t' << pace->againNothingNew << '\t' << pace->againNoSpeed << '\t' << pace->againNoTime << '\t' << pace->againNeedMs << '\t'
                  << pace->againHaveMs << '\t' << pace->ahead << '\t' << pace->budgetMs() << '\t' << pace->widthOfFirst() << '\t' << pace->rivalSearches << '\t'
                  << pace->rivalReused << '\t' << pace->rivalMissed;
        if (placing) note << '\t' << placing->againLevel << '\t' << placing->revisedChoices;
        note << '\n';
        queueRecord(true, std::move(note).str());
    }
    uint64_t boardsWritten_ = 0, wideWritten_ = 0;
    static constexpr uint64_t WIDE_BYTES = uint64_t(400) << 20; // the wide record ends at this size
    std::string readerLine_;
    long faultLines_ = 0;
    std::string recordedField_;
    uint64_t rawWritten_ = 0;
    unsigned lateStep_ = 0; // (of the trial of buttons set late)
    std::atomic<bool> resend_{false};

    bool stopped() const {
        std::lock_guard<std::mutex> hold(lock_);
        return stop_;
    }
    template <size_t N> static std::string hex(const std::array<uint64_t, N>& v) {
        std::string out;
        char text[24];
        for (const uint64_t a : v) std::snprintf(text, sizeof text, "\t%llx", static_cast<unsigned long long>(a)), out += text;
        return out;
    }
    // Keep file I/O off the thread that has one frame to deliver its buttons.
    void writeRecords() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        uint64_t flushAt = 0;
        for (;;) {
            std::vector<std::pair<bool, std::string>> lines;
            bool done;
            long dropped;
            {
                std::unique_lock<std::mutex> hold(recordLock_);
                recordReady_.wait_for(hold, std::chrono::milliseconds(25), [this] {
                    return recordStop_ || recordLines_.size() >= 32;
                });
                done = recordStop_;
                dropped = recordDropped_;
                lines.swap(recordLines_);
            }
            readDiagnostics(dropped);
            for (const auto& [thinking, line] : lines)
                (thinking ? thinking_ : record_) << line;
            if (const uint64_t now = PrimaryRuntime14::clockUs(); done || now >= flushAt) {
                flushAt = now + 1000000;
                if (recordPlay_) record_.flush();
                if (recordThinking_) thinking_.flush();
                if (faults_.is_open()) faults_.flush();
                if (boards_.is_open()) boards_.flush();
            }
            if (done) return;
        }
    }
    void readDiagnostics(long dropped) {
        if (reader_.is_open()) {
            const PrimaryRuntime14::Status r = runtime_.status();
            const std::string line = r.state + '\t' + std::to_string(r.scans) + '\t' + std::to_string(r.binds) + '\t' + std::to_string(r.rebinds) + '\t' + std::to_string(r.boards) +
                                     '\t' + std::to_string(r.scanMs) + '\t' + std::to_string(r.scanBytes) + hex(r.bound) + hex(r.operate) + '\t' + r.bindNote + '\t' + std::to_string(r.absentPages) +
                                     '\t' + std::to_string(r.pagedIn) + '\t' + std::to_string(dropped);
            if (line != readerLine_)
                readerLine_ = line, reader_ << PrimaryRuntime14::clockUs() << '\t' << line << '\n', reader_.flush();
        }
        // (why a seat, or the place of its pair, was not read - code 1 its objects, 2 changing as it was read, 3 its
        //  flags, 4 the place of the pair, 5 its colours, 6 the board -, with the bytes the place is read from)
        for (const PrimaryFault14& f : runtime_.takeFaults()) {
            if (!faults_.is_open() || faultLines_++ >= 20000) continue;
            static const char* HEX = "0123456789abcdef";
            std::string bytes;
            for (const uint8_t v : f.pose) bytes.push_back(HEX[v >> 4]), bytes.push_back(HEX[v & 15]);
            faults_ << PrimaryRuntime14::clockUs() << '\t' << f.tick << '\t' << int(f.seat) << '\t' << int(f.code) << '\t' << int(f.operating) << '\t' << f.locked << '\t' << f.phase
                    << '\t' << f.fieldState << '\t' << f.playerState << '\t' << f.colours[0] << '\t' << f.colours[1] << '\t' << bytes << '\n';
        }
        if (raw_.is_open()) {
            for (const PrimaryRuntime14::Raw14& r : runtime_.takeRaw()) {
                if (rawWritten_ >= setup_.rawBytes) break;
                const uint8_t head[4] = {r.seat, r.fieldRead, r.playerRead, 0};
                raw_.write(reinterpret_cast<const char*>(&r.serial), 8), raw_.write(reinterpret_cast<const char*>(&r.tick), 4), raw_.write(reinterpret_cast<const char*>(head), 4);
                raw_.write(reinterpret_cast<const char*>(r.field.data()), std::streamsize(r.field.size()));
                raw_.write(reinterpret_cast<const char*>(r.player.data()), std::streamsize(r.player.size()));
                rawWritten_ += 16 + r.field.size() + r.player.size();
            }
            if (rawWritten_ >= setup_.rawBytes) runtime_.raw(false);
        }
        if (boards_.is_open()) {
            for (const PrimaryRuntime14::Board14& b : runtime_.takeBoards()) {
                if (boardsWritten_ >= setup_.rawBytes) break;
                const uint8_t head[4] = {b.read, 0, 0, 0};
                boards_.write(reinterpret_cast<const char*>(&b.serial), 8), boards_.write(reinterpret_cast<const char*>(b.tick.data()), 8);
                boards_.write(reinterpret_cast<const char*>(&b.index), 2), boards_.write(reinterpret_cast<const char*>(&b.count), 2);
                boards_.write(reinterpret_cast<const char*>(head), 4), boards_.write(reinterpret_cast<const char*>(&b.address), 8);
                boards_.write(reinterpret_cast<const char*>(b.bytes.data()), std::streamsize(b.bytes.size()));
                boardsWritten_ += 32 + b.bytes.size();
            }
            if (boardsWritten_ >= setup_.rawBytes) runtime_.boards(false);
        }
        if (wide_.is_open()) {
            for (const PrimaryRuntime14::Wide14& w : runtime_.takeWide()) {
                if (wideWritten_ >= WIDE_BYTES) break;
                const uint8_t head[2] = {w.seat, 0}, zeros[6] = {};
                wide_.write(reinterpret_cast<const char*>(&w.serial), 8), wide_.write(reinterpret_cast<const char*>(&w.tick), 4), wide_.write(reinterpret_cast<const char*>(head), 2);
                wide_.write(reinterpret_cast<const char*>(&w.got), 2), wide_.write(reinterpret_cast<const char*>(&w.size), 2), wide_.write(reinterpret_cast<const char*>(zeros), 6);
                wide_.write(reinterpret_cast<const char*>(w.field.data()), std::streamsize(w.field.size()));
                wide_.write(reinterpret_cast<const char*>(w.player.data()), std::streamsize(w.player.size()));
                wideWritten_ += 24 + w.field.size() + w.player.size();
            }
            if (wideWritten_ >= WIDE_BYTES) runtime_.wide(false);
        }
    }
    void run() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        std::unique_ptr<PrimaryPlayer14> player;
        const auto make = [&] {
            // (the agent's settings as the play began with them: a player before this one may have left the ways over
            //  soonest switched off for a field not sure of, and the new one takes what it finds as what was asked for)
            prepare(agent_.get(), setup_.cancel);
            player = std::make_unique<PrimaryPlayer14>(*agent_, setup_.me, setup_.delay);
            player->observer.rules = juraic::realGameRules(), player->observer.timing = primaryTiming14(setup_.cancel);
            player->seed = setup_.seed;
            player->onRound = [this] { noteThinking(); };
        };
        make();
        uint64_t serial = 0, slowest = 0;
        long delayChanges = 0;
        uint8_t sent = 0;
        bool sentAny = false;
        int idle = 0; // samples in a row with no round played
        while (!stopped()) {
            runtime_.wait(serial, 50);
            const auto fresh = runtime_.since(serial);
            if (fresh.empty()) {
                // (the seats are not read: nothing is held)
                if (!runtime_.sample() && sentAny && sent) send(sent = 0);
                continue;
            }
            // (the newest only: of frames that came while the AI was busy the buttons stayed, which the player reckons
            //  with - PrimaryPlayer14 plays them on as the engine has them)
            const PrimarySample14& s = fresh.back();
            serial = s.serial;
            // The frames a button takes, measured otherwise than reckoned DELAY_RUN taps in a row (a match over a network
            // takes the buttons some frames later): the player is made anew with what was measured, while no pair of
            // the seat is in the hands.
            if (player->measuredRun() >= DELAY_RUN && player->measured() != player->delay() && player->measured() <= DELAY_MOST &&
                !(s.seat[size_t(setup_.me)] && s.seat[size_t(setup_.me)]->pose.operating)) {
                setup_.delay = player->measured();
                const uint64_t seed = player->seed;
                make();
                player->seed = seed;
                ++delayChanges;
            }
            // Another agent takes over while no pair of the seat is in the hands.
            {
                std::unique_lock<std::mutex> hold(lock_);
                if (next_ && !(s.seat[size_t(setup_.me)] && s.seat[size_t(setup_.me)]->pose.operating)) {
                    std::unique_ptr<juraic::ai::Agent> old = std::move(agent_);
                    agent_ = std::move(next_), setup_.cancel = nextCancel_;
                    const uint64_t seed = player->seed;
                    hold.unlock();
                    make();
                    player->seed = seed;
                    // (the one before may still be thinking: let go on a thread of its own)
                    std::thread([gone = std::move(old)]() mutable { gone.reset(); }).detach();
                }
            }
            // The garbage waiting above each field: the game's own number where the seat's object has it (sooner than
            // the pictures, and exact); the one last read for a seat not read in this sample; else what the pictures show.
            std::array<int, 2> waiting = notice ? notice() : std::array<int, 2>{0, 0};
            for (size_t i = 0; i < 2; ++i) {
                if (s.seat[i]) trayLast_[i] = s.seat[i]->noticeKnown ? s.seat[i]->notice : -1;
                if (trayLast_[i] >= 0) waiting[i] = trayLast_[i];
            }
            const uint64_t driverBegin = PrimaryRuntime14::clockUs();
            const uint8_t buttons = player->frame(s, waiting);
            const uint64_t driverReply = PrimaryRuntime14::clockUs();
            const uint8_t out = setup_.turnsSwapped ? swapTurns(buttons) : buttons;
            if (resend_.exchange(false)) sentAny = false;
            if (!sentAny || out != sent) {
                if (lateTrial.load() && (out & ~sent & (juraic::BTN_LEFT | juraic::BTN_RIGHT | juraic::BTN_CW | juraic::BTN_CCW))) {
                    const uint64_t until = s.observedUs + uint64_t(lateStep_++ % 8) * 2000;
                    while (PrimaryRuntime14::clockUs() < until) YieldProcessor();
                }
                send(out), sent = out, sentAny = true;
                // (buttons set too late for the frame they answer: that frame went by with the buttons before them)
                if (!lateTrial.load() && PrimaryRuntime14::clockUs() - s.observedUs > LATE_US) player->undelivered();
            }
            const uint64_t now = PrimaryRuntime14::clockUs();
            const bool round = player->observer.tickOf(s, setup_.me) >= 0 && s.seat[0] && s.seat[1] && PrimaryObserver14::playing(s.seat[0]->pose) && PrimaryObserver14::playing(s.seat[1]->pose);
            if (round && now - s.observedUs > slowest) slowest = now - s.observedUs;
            // (a second after a round is over: its thinking is noted, also when no other round follows)
            idle = round ? 0 : idle + 1;
            if (idle == 60) noteThinking();
            const auto* placing = dynamic_cast<juraic::ai::PlacementAgent*>(agent_.get());
            {
                std::lock_guard<std::mutex> hold(lock_);
                if (s.seat[size_t(setup_.me)]) status_.cpuSeat = s.seat[size_t(setup_.me)]->pose.cpuControlled;
                status_.round = round, status_.rounds = player->rounds, status_.frames = player->frames, status_.skipped = player->skipped, status_.filled = player->filled;
                status_.recovered = player->recovered, status_.corrections = player->corrections(), status_.repaired = player->repaired();
                status_.pairs = s.seat[size_t(setup_.me)] ? int(s.seat[size_t(setup_.me)]->pose.lockedPairs) : status_.pairs;
                status_.target = placing ? placing->targetPlacement() : -1, status_.held = buttons, status_.slowestUs = slowest, status_.notice = waiting;
                status_.delay = player->delay(), status_.measured = player->measured(), status_.delayChanges = delayChanges;
                status_.thinking = thoughtAll_;
            }
            if (recordPlay_) {
                std::ostringstream line;
                std::string field = recordedField_;
                recordRow(line, now, s, setup_.me, *player, placing ? placing->targetPlacement() : -1, buttons, out, field, driverBegin, driverReply);
                if (queueRecord(false, std::move(line).str())) recordedField_ = std::move(field);
            }
        }
        send(0);
    }
    void send(uint8_t buttons) {
        if (press) press(buttons);
    }
};

} // namespace play::realgame
