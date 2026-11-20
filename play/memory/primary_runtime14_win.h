#pragma once
// The worker of primary14.h on Windows: finds the selected program, scans its data once for the boards, binds the two
// seats and then follows them frame by frame of the game's own counter. No picture is involved. Read/query access only
// (the handles of ProcessMemory and PageQueryReader14); nothing is written to the program, nothing runs in it.
//
// Following: every millisecond (a timer of the fine kind; the ordinary waits go by the system's tick of about 16 ms and
// would skip frames) the seats are looked at as PrimaryFollow14 has it - the two frame counters, and the seats whole
// when a counter has moved on. Every sample that shows something new is kept for a while, so that a player who was
// busy for some frames still gets each of them (since, wait).
//
// When the seats' objects are gone (the game makes new ones for every round) the seats are bound again by a new scan
// at once; while there is nothing to bind, again 750 ms after a scan ends (250 ms for QUICK_MS after a round).
#include "datareader14_win.h"
#include "pagequery14_win.h"
#include "primary14.h"
#include "regionplan14.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace play::realgame {

class PrimaryRuntime14 {
public:
    struct Status {
        std::string state = "not_started"; // no_process, no_access, scanning, unbound, bound
        std::wstring why;                   // why the program could not be opened ("OpenProcess: 5": it runs with higher rights)
        uint64_t scans = 0, binds = 0;      // scans made, of them bound
        uint64_t polls = 0, whole = 0, read = 0, samples = 0; // looks at the counters, whole reads, of them with a seat read, new samples
        uint64_t scanMs = 0, scanBytes = 0; // of the last scan
        size_t boards = 0;                  // boards the last scan found
        bool fineTimer = false;             // the counters are looked at every millisecond (else by the system's tick)
        uint64_t lateWaits = 0, longestWaitUs = 0; // waits between two looks of more than 3 ms, and the longest one
        uint64_t steps1 = 0, stepsMore = 0, same = 0; // new samples in which the counter of seat 0 went on by one, by more (frames not seen), not at all
        uint64_t wholeUs = 0;               // the time the last whole read took
        uint64_t faults = 0;                // times a seat, or the place of its pair, began not to be read (takeFaults)
        uint64_t rebinds = 0;               // times the seats were let go to be bound anew (PrimaryRebind14)
        // Pages asked for that were not in the game's working set, and those of them read all the same (PageQueryReader14).
        uint64_t absentPages = 0, pagedIn = 0;
        // The objects the seats are bound to (0 while not bound): seat 0's player, field and board, then seat 1's.
        std::array<uint64_t, 6> bound{};
        std::array<uint64_t, 2> operate{}; // ... and the object that operates each seat's pair, as it was then
        // What became of each board the last scan found when the seats were to be bound (PrimaryBindNote14::text);
        // "over_limit": the last scan was not made, the regions to scan being more than it may read (FieldScan14::tooLarge).
        std::string bindNote = "-";
    };
    static constexpr size_t KEPT = 64; // samples kept for `since`: about a second of play
    // When the seats are bound anew: PrimaryRebind14 (a seat's objects or board not where they were bound for 150 ms -
    // the game has made new ones for a new round -, a seat not read for its flags for a second, or neither seat read
    // for eight seconds). They are then looked for at
    // once, and when not found again every quarter of a second for QUICK_MS.
    static constexpr uint64_t QUICK_MS = 15000;
    ~PrimaryRuntime14() { stop(); }
    void start(std::wstring program) {
        stop();
        {
            std::lock_guard<std::mutex> hold(lock_);
            stop_ = false, program_ = std::move(program), sample_.reset(), recent_.clear(), status_ = {};
        }
        worker_ = std::thread([this] {
            // (an exception - no memory for a scan, say - ends one run, not the reader: it begins anew half a second later)
            for (;;) {
                try {
                    run();
                    return;
                } catch (...) {
                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL); // (the next run begins as not raised)
                    std::lock_guard<std::mutex> hold(lock_);
                    sample_.reset(), status_.state = "worker_failed";
                }
                for (int k = 0; k < 50; ++k) {
                    {
                        std::lock_guard<std::mutex> hold(lock_);
                        if (stop_) return;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
        });
    }
    void stop() {
        {
            std::lock_guard<std::mutex> hold(lock_);
            stop_ = true, sample_.reset();
        }
        wake_.notify_all(), fresh_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
    // Both seats as last read; nothing while they cannot be read.
    std::optional<PrimarySample14> sample() const {
        std::lock_guard<std::mutex> hold(lock_);
        return sample_;
    }
    // For finding where the game keeps what its screen shows beside the fields (the score, the garbage waiting, the
    // chain): with every new sample the bytes of each seat's field object and player object as they are, for a script
    // to look through for those numbers. Not the object that operates the pair (the buttons are in it) nor the window
    // of the next pairs (what lies beyond the window is behind it). Off unless asked for.
    struct Raw14 {
        static constexpr size_t FIELD = 0x300, PLAYER = 0x100;
        uint64_t serial = 0;
        uint32_t tick = 0;
        uint8_t seat = 0, fieldRead = 0, playerRead = 0;
        std::array<uint8_t, FIELD> field{};
        std::array<uint8_t, PLAYER> player{};
    };
    void raw(bool on) {
        std::lock_guard<std::mutex> hold(lock_);
        raw_ = on;
        if (!on) rawRows_.clear();
    }
    // For finding the board the screen shows (garbage that has fallen is not in the bound board until the next pair is
    // put, as records of the real game show): every table of the board's cell format the last scan found - the bound
    // two and any other, BOARDS at most - as it is with a new sample, whenever it has changed since it was last taken.
    // Cells of boards only (the 440 bytes readStableField14 reads of a board). Off unless asked for.
    struct Board14 {
        static constexpr size_t BOARDS = 64;
        uint64_t serial = 0, address = 0;
        std::array<uint32_t, 2> tick{};
        uint16_t index = 0, count = 0; // which of the tables found, of how many
        uint8_t read = 0;
        std::array<uint8_t, 440> bytes{};
    };
    void boards(bool on) {
        std::lock_guard<std::mutex> hold(lock_);
        boards_ = on;
        if (!on) boardRows_.clear();
    }
    // For finding where the game keeps what the screen shows of a field and is not in the board read (garbage that has
    // fallen, the garbage waiting, the score): more of each seat's field object and player object than Raw14 has, and
    // what lies behind the field object as far as the seat's board (the field object holds seven addresses of things
    // there - between its end and the board, 0x355c after it in the records of the real game; none of what the screen
    // shows and the board lacks was in its first 0x1700 bytes).
    // FIELD bytes at most are taken - and fewer when one of the
    // objects that are never recorded lies within them: the bytes end 0x20 before the object that operates a pair (the
    // buttons are in it), before the object of the window of the next pairs (what lies beyond the window is behind
    // it), and before any other bound object. The player object: 0x300 (the field object has been seen 0x310 after it).
    // Not with every frame: in every frame while garbage falls on the seat and for a while after, when what the seat
    // is doing changes, and every 20th frame. Read a page at a time (`got`: which 4 KiB pieces could be read - a page
    // not used yet cannot). Off unless asked for.
    struct Wide14 {
        static constexpr size_t FIELD = 0x3600, PLAYER = 0x300;
        uint64_t serial = 0;
        uint32_t tick = 0;
        uint8_t seat = 0;
        uint16_t size = 0; // bytes of the field object taken (the rest are zeros)
        uint16_t got = 0; // bit k: the k-th 4 KiB piece of the field object's bytes was read; bit 15: the player object's
        std::array<uint8_t, FIELD> field{};
        std::array<uint8_t, PLAYER> player{};
    };
    void wide(bool on) {
        std::lock_guard<std::mutex> hold(lock_);
        wide_ = on;
        if (!on) wideRows_.clear();
    }
    std::vector<Wide14> takeWide() {
        std::lock_guard<std::mutex> hold(lock_);
        std::vector<Wide14> out;
        out.swap(wideRows_);
        return out;
    }
    std::vector<Board14> takeBoards() {
        std::lock_guard<std::mutex> hold(lock_);
        std::vector<Board14> out;
        out.swap(boardRows_);
        return out;
    }
    std::vector<PrimaryFault14> takeFaults() {
        std::lock_guard<std::mutex> hold(lock_);
        std::vector<PrimaryFault14> out;
        out.swap(faults_);
        return out;
    }
    std::vector<Raw14> takeRaw() {
        std::lock_guard<std::mutex> hold(lock_);
        std::vector<Raw14> out;
        out.swap(rawRows_);
        return out;
    }
    // The samples after the one of that serial number, oldest first (those still kept).
    std::vector<PrimarySample14> since(uint64_t serial) const {
        std::lock_guard<std::mutex> hold(lock_);
        std::vector<PrimarySample14> out;
        for (const PrimarySample14& s : recent_)
            if (s.serial > serial) out.push_back(s);
        return out;
    }
    // Waits until there is a sample after the one of that serial number, for `ms` at most. Whether there is one.
    bool wait(uint64_t serial, int ms) const {
        std::unique_lock<std::mutex> hold(lock_);
        const auto newer = [&] { return stop_ || (!recent_.empty() && recent_.back().serial > serial); };
        fresh_.wait_for(hold, std::chrono::milliseconds(ms), newer);
        return !stop_ && !recent_.empty() && recent_.back().serial > serial;
    }
    Status status() const {
        std::lock_guard<std::mutex> hold(lock_);
        return status_;
    }
    static uint64_t clockUs() {
        LARGE_INTEGER t{}, f{};
        QueryPerformanceCounter(&t), QueryPerformanceFrequency(&f);
        return uint64_t(t.QuadPart / f.QuadPart) * 1000000 + uint64_t(t.QuadPart % f.QuadPart) * 1000000 / uint64_t(f.QuadPart);
    }
    static uint64_t clockMs() { return clockUs() / 1000; }

private:
    mutable std::mutex lock_;
    std::condition_variable wake_;
    mutable std::condition_variable fresh_;
    std::thread worker_;
    bool stop_ = true;
    std::wstring program_;
    std::optional<PrimarySample14> sample_;
    std::deque<PrimarySample14> recent_;
    Status status_;
    bool raw_ = false, boards_ = false, wide_ = false;
    std::vector<Raw14> rawRows_;
    std::vector<Board14> boardRows_;
    std::vector<Wide14> wideRows_;
    bool wideWanted() const {
        std::lock_guard<std::mutex> hold(lock_);
        return wide_;
    }
    bool boardsWanted() const {
        std::lock_guard<std::mutex> hold(lock_);
        return boards_;
    }
    std::vector<PrimaryFault14> faults_;
    bool rawWanted() const {
        std::lock_guard<std::mutex> hold(lock_);
        return raw_;
    }

    bool stopped() const {
        std::lock_guard<std::mutex> hold(lock_);
        return stop_;
    }
    void run() {
        ProcessMemory memory;
        FieldDataReader14 data(memory);
        PageQueryReader14 fast;
        FieldProcess current;
        PrimaryBinding14 binding;
        Status status;
        PrimaryFollow14 follow;
        uint64_t generation = 0, pollAt = 0, scanAt = 0, patientAt = 0;
        uint64_t lookedUs = 0, quickUntil = 0;
        PrimaryRebind14 rebind;
        std::vector<uint64_t> tables;                                    // the tables of the board's format the last scan found
        std::vector<std::pair<bool, std::array<uint8_t, 440>>> tableWas; // ... each as it was last taken (first: taken at all)
        // (the wide record: what each seat was doing when it was last taken, and the frames it is still taken in after a fall)
        struct Doing {
            uint32_t phase = ~0u, state = ~0u, put = ~0u;
            bool operating = false, active = false;
            int after = 0;
            bool operator==(const Doing& o) const { return phase == o.phase && state == o.state && put == o.put && operating == o.operating && active == o.active; }
        };
        std::array<Doing, 2> doing{};
        bool raised = false;
        std::array<PrimaryFault14, 2> faulted{};
        {
            // (a run begun anew after an exception goes on with the serial numbers of the last)
            std::lock_guard<std::mutex> hold(lock_);
            if (!recent_.empty()) follow.continueFrom(recent_.back().serial);
        }
        const FieldRead14 quick = [&](uint64_t a, void* b, size_t n) { return fast.checked(a, b, n); };
        const auto publish = [&](std::optional<PrimarySample14> s, bool fresh) {
            {
                std::lock_guard<std::mutex> hold(lock_);
                if (fresh && s) {
                    recent_.push_back(*s);
                    if (recent_.size() > KEPT) recent_.pop_front();
                }
                sample_ = std::move(s), status_ = status;
            }
            if (fresh) fresh_.notify_all();
        };
        // (the worker sleeps nearly all the time; while it follows the seats it goes before ordinary work, so that a
        //  busy machine does not make it miss frames - not while it scans, which takes half a second of a processor)
        const auto raise = [&](bool on) {
            if (on != raised) SetThreadPriority(GetCurrentThread(), on ? THREAD_PRIORITY_HIGHEST : THREAD_PRIORITY_NORMAL), raised = on;
        };
        // A timer of the fine kind (0x2: CREATE_WAITABLE_TIMER_HIGH_RESOLUTION): a wait of one millisecond really is one.
        struct Timer {
            HANDLE h = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002, TIMER_ALL_ACCESS);
            ~Timer() {
                if (h) CloseHandle(h);
            }
        } timer;
        status.fineTimer = timer.h != nullptr;
        const auto unbind = [&] { binding = {}, follow.reset(), lookedUs = 0, rebind.reset(), status.bound = {}, status.operate = {}; };
        for (;;) {
            raise(binding.both());
            LARGE_INTEGER due{};
            due.QuadPart = -10000; // (one millisecond, in units of 100 ns)
            if (binding.both() && timer.h && SetWaitableTimer(timer.h, &due, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(timer.h, 100);
                if (stopped()) return;
            } else {
                std::unique_lock<std::mutex> hold(lock_);
                wake_.wait_for(hold, std::chrono::milliseconds(binding.both() ? 1 : 50), [&] { return stop_; });
                if (stop_) return;
            }
            const uint64_t nowUs = clockUs(), now = nowUs / 1000;
            // The program: looked for twice a second while no seats are followed (it takes milliseconds: frames would be
            // missed); another process, or none, is then found. A program that ends while its seats are followed stops
            // being readable, which drops the binding half a second later.
            if (!binding.both() && now >= pollAt) {
                pollAt = now + 500;
                FieldProcess fresh;
                std::wstring why;
                if (!openFieldProcess14(program_, memory, fresh, why)) fresh = {};
                status.why = why;
                if (!sameFieldContext({current, 0, false}, {fresh, 0, false})) {
                    current = fresh, unbind(), scanAt = 0;
                    fast.close();
                    if (current.pid) fast.open(current, why);
                } else if (current.pid && fast.granted() != PageQueryReader14::RIGHTS) {
                    // (the same process, and its pages could not be opened the last time: once more)
                    fast.close();
                    fast.open(current, why);
                }
            }
            if (!current.pid || fast.granted() != PageQueryReader14::RIGHTS) {
                status.state = current.pid || status.why.rfind(L"OpenProcess: ", 0) == 0 ? "no_access" : "no_process";
                publish({}, false);
                continue;
            }
            if (!binding.both()) {
                if (now < scanAt) continue;
                raise(false);
                status.state = "scanning";
                publish({}, false);
                FieldContext context;
                context.process = current, context.round = 1, context.roundKnown = true;
                const auto plan = planFieldRegions14(current, memory.regions());
                // (the program's data whole when the region its objects are kept in is not told apart: a larger bound)
                FieldScan14 scan(context, ++generation, plan.regions, plan.profiled ? uint64_t(4) << 30 : uint64_t(32) << 30, 4096, 16u << 20);
                const FieldRead14 bulk = [&](uint64_t a, void* b, size_t n) { return data.checked(a, b, n); };
                // (the player objects too, by the mark of their class: for a seat whose board has no owner beside it)
                scan.findAlso(current.imageBase + 0x2ac500);
                // (the game frees and makes memory between two rounds, while this runs: what cannot be read is passed over)
                scan.skipUnread();
                while (!scan.done() && !stopped()) scan.step(bulk);
                const FieldCatalog14 catalog = scan.catalog();
                ++status.scans, status.scanMs = clockMs() - now, status.scanBytes = scan.bytesRead(), status.boards = catalog.visibleTop.size();
                unbind();
                tables.assign(catalog.visibleTop.begin(), catalog.visibleTop.begin() + std::ptrdiff_t(std::min(catalog.visibleTop.size(), Board14::BOARDS)));
                tableWas.assign(tables.size(), {});
                PrimaryBindNote14 note;
                fast.patient(true); // (the binding may take its time: pages not in the working set are read, too)
                if (catalog.complete) binding = bindPrimary14(current, catalog.visibleTop, quick, &note, scan.marks());
                fast.patient(false);
                status.bindNote = scan.tooLarge() ? std::string("over_limit") : note.text();
                if (!binding.both()) {
                    binding = {}, scanAt = clockMs() + (clockMs() < quickUntil ? 250 : 750), status.state = "unbound";
                    publish({}, false);
                    continue;
                }
                ++status.binds;
                for (size_t s = 0; s < 2; ++s) {
                    status.bound[s * 3] = binding.seat[s].player, status.bound[s * 3 + 1] = binding.seat[s].field, status.bound[s * 3 + 2] = binding.seat[s].top;
                    status.operate[s] = binding.seat[s].operate;
                }
                // (the first look after a scan is not a late one)
            } else if (lookedUs) {
                const uint64_t waited = nowUs - lookedUs;
                status.lateWaits += waited > 3000 ? 1 : 0;
                if (waited > status.longestWaitUs) status.longestWaitUs = waited;
            }
            lookedUs = clockUs();
            ++status.polls;
            const uint64_t absentBefore = fast.timing.absent;
            const PrimaryFollow14::Look look = follow.look(binding, generation, quick, [] { return clockUs(); });
            // (a page the look asked for was not in the working set: both seats are read once with such pages read as well,
            //  which brings them back for the looks to come - at most every quarter of a second, the result not used)
            if (fast.timing.absent != absentBefore && clockMs() >= patientAt) {
                patientAt = clockMs() + 250;
                fast.patient(true);
                for (int seat = 0; seat < 2; ++seat) (void)readPrimarySeat14(binding, seat, quick);
                fast.patient(false);
            }
            status.absentPages = fast.timing.absent, status.pagedIn = fast.timing.pagedIn;
            if (!look.whole) continue; // (the counters stand)
            const uint64_t doneUs = clockUs();
            // (why a seat, or the place of its pair, was not read: kept for the record, each kind once as long as it lasts)
            for (size_t seat = 0; seat < 2; ++seat) {
                const PrimaryFault14& f = look.fault[seat];
                if (f.code == PrimaryFault14::None) {
                    faulted[seat] = {};
                    continue;
                }
                if (f.code == faulted[seat].code && f.pose == faulted[seat].pose && f.operating == faulted[seat].operating && f.phase == faulted[seat].phase) continue;
                faulted[seat] = f;
                ++status.faults;
                std::lock_guard<std::mutex> hold(lock_);
                if (faults_.size() < 4096) faults_.push_back(f);
            }
            ++status.whole, status.read += look.read ? 1 : 0, status.state = "bound", status.wholeUs = doneUs - lookedUs;
            // (a round is over and the game has made new objects for the next, which are there at once, two seconds
            //  and more before its first pair: looked for right away, and again every quarter of a second for a while)
            if (rebind.after(look.fault, doneUs)) {
                ++status.rebinds;
                unbind(), scanAt = 0, quickUntil = doneUs / 1000 + QUICK_MS;
                publish({}, false);
                continue;
            }
            if (!look.read) {
                publish({}, false);
                continue;
            }
            if (look.fresh) ++status.samples, ++(look.step == 1 ? status.steps1 : look.step ? status.stepsMore : status.same);
            if (look.fresh && rawWanted()) {
                for (int seat = 0; seat < 2; ++seat) {
                    const auto& read = follow.last()->seat[size_t(seat)];
                    if (!read) continue;
                    Raw14 r;
                    r.serial = follow.last()->serial, r.tick = read->pose.count108, r.seat = uint8_t(seat);
                    r.fieldRead = quick(binding.seat[size_t(seat)].field, r.field.data(), r.field.size());
                    // (not all of it readable: the part the reading of a seat knows to be there - marked 2)
                    if (!r.fieldRead && quick(binding.seat[size_t(seat)].field, r.field.data(), 0x2d4)) r.fieldRead = 2;
                    r.playerRead = quick(binding.seat[size_t(seat)].player, r.player.data(), r.player.size());
                    std::lock_guard<std::mutex> hold(lock_);
                    if (rawRows_.size() < 4096) rawRows_.push_back(r);
                }
            }
            publish(follow.last(), look.fresh);
            // (after the sample is out, so that whoever plays by it does not wait for this)
            if (look.fresh && wideWanted()) {
                const PrimarySample14& s = *follow.last();
                for (size_t seat = 0; seat < 2; ++seat) {
                    if (!s.seat[seat]) continue;
                    const OwnedPublicPose14& p = s.seat[seat]->pose;
                    Doing now = doing[seat];
                    now.phase = p.phase, now.state = p.fieldState, now.put = p.lockedPairs, now.operating = p.operating, now.active = p.fieldActive != 0;
                    const bool falling = p.phase == 10;
                    const bool take = falling || now.after > 0 || !(now == doing[seat]) || p.count108 % 20 == 0;
                    now.after = falling ? 40 : now.after > 0 ? now.after - 1 : 0;
                    doing[seat] = now;
                    if (!take) continue;
                    Wide14 w;
                    w.serial = s.serial, w.tick = p.count108, w.seat = uint8_t(seat);
                    // (where the bytes end: before any object that is never recorded, and before any other bound one)
                    const uint64_t base = binding.seat[seat].field;
                    uint64_t size = w.field.size(), window = 0;
                    std::vector<uint64_t> others{binding.seat[seat].operate, binding.seat[seat].player};
                    for (size_t k = 0; k < 2; ++k) {
                        if (k != seat) others.push_back(binding.seat[k].field), others.push_back(binding.seat[k].player), others.push_back(binding.seat[k].operate);
                        if (s.seat[k]) others.push_back(s.seat[k]->pose.owner.operate);
                        // (the object of the window of the next pairs, as readPublicPairs14 finds it)
                        if (quick(binding.seat[k].player + 0x90, &window, 8) && window >= 0x1020) others.push_back(window - 0x20);
                        else size = 0; // (not known where it is: nothing is taken)
                    }
                    for (const uint64_t other : others)
                        if (other > base && other - base < size + 0x20) size = other - base > 0x20 ? other - base - 0x20 : 0;
                    w.size = uint16_t(size);
                    for (size_t at = 0, piece = 0; at < size; at += 4096, ++piece) {
                        // (4 KiB pieces of the bytes, cut at the pages of the program: a page is read whole or not)
                        const uint64_t a = base + at;
                        const size_t n = std::min<size_t>(4096, size_t(size) - at);
                        const uint64_t page = (a / 4096 + 1) * 4096;
                        const size_t first = size_t(std::min<uint64_t>(n, page - a));
                        const bool got = quick(a, w.field.data() + at, first) && (first == n || quick(a + first, w.field.data() + at + first, n - first));
                        if (got) w.got |= uint16_t(1u << piece);
                        else std::fill(w.field.begin() + std::ptrdiff_t(at), w.field.begin() + std::ptrdiff_t(at + n), uint8_t(0));
                    }
                    if (quick(binding.seat[seat].player, w.player.data(), w.player.size())) w.got |= 0x8000;
                    std::lock_guard<std::mutex> hold(lock_);
                    if (wideRows_.size() < 2048) wideRows_.push_back(w);
                }
            }
            if (look.fresh && boardsWanted()) {
                const PrimarySample14& s = *follow.last();
                for (size_t k = 0; k < tables.size(); ++k) {
                    Board14 b;
                    b.serial = s.serial, b.address = tables[k], b.index = uint16_t(k), b.count = uint16_t(tables.size());
                    for (size_t seat = 0; seat < 2; ++seat) b.tick[seat] = s.seat[seat] ? s.seat[seat]->pose.count108 : 0;
                    b.read = tables[k] >= 64 && quick(tables[k] - 64, b.bytes.data(), b.bytes.size());
                    if (!b.read) b.bytes = {};
                    if (tableWas[k].first && tableWas[k].second == b.bytes) continue;
                    tableWas[k] = {true, b.bytes};
                    std::lock_guard<std::mutex> hold(lock_);
                    if (boardRows_.size() < 8192) boardRows_.push_back(b);
                }
            }
        }
    }
};

} // namespace play::realgame
