#pragma once
// Memory first: both seats' public state read from the game's own objects, with no picture needed to find, to prove or
// to use it. (The path of sample14.h, motion_sample14.h and published_next14.h takes every value only where the picture
// tracker agrees; this one is for a player that goes by memory and keeps the pictures as a check.)
//
// Read only what a person sees on the screen: each seat's field (14 rows), the pair in play (colours, cell, half,
// direction, whether it is in the player's hands, the count of pairs put) and the two pairs shown as next. No future
// pair beyond those (readListedPairs14 aside, only while primaryListedPairs14 is set), the garbage random state only while primaryGarbageRandom14 is set,
// and the local opponent's logical input only for the seat selected by primaryInputSeat14. Read/query access only.
//
// How a seat is found: a board of 14 rows (scan14.h finds them by their own cell format) has the object that operates
// the pair right beside it (owner14.h), which names the player and the field objects; the three name each other and
// carry the classes of the verified program version (owned_pose14.h reads them twice). A board whose owner is gone
// (a round that is over leaves its last board in the heap) has no such live cycle. The seat number comes from the
// player object. A seat is bound when exactly one player object with exactly one board reads so.
//
// What is copied and what is looked at: the scan copies the program's data to find the boards by their format, and the
// binding copies the 8 KiB beside each board to find the class mark of its owner - of these copies only the format,
// the mark and the three object addresses are looked at. From then on a seat is read by the few small reads of
// readPrimarySeat14. The optional seat input and listed pairs each have their own read; the ordinary path stays within the shown window
// (tests/realgame_memory_primary_tests.cpp counts the bytes).
#include "primary_input14.h"
#include "owned_pose14.h"
#include "owner14.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace play::realgame {

// An address with `n` bytes from it that is neither near either end nor in the program's own image.
inline bool primaryHeap14(const FieldProcess& process, uint64_t a, uint64_t n) {
    return a >= 4096 && a <= UINT64_MAX - n && !(a >= process.imageBase && a - process.imageBase < process.image.imageSize);
}

// The local game constructs both controllers with the same player and field links.
inline constexpr uint64_t PRIMARY_LOCAL_OPERATE14 = 0x2ad2c0;
inline constexpr uint64_t PRIMARY_CPU_OPERATE14 = 0x2acb48;
inline bool primaryLocalOperate14(uint64_t mark, uint64_t image) {
    return mark == image + PRIMARY_LOCAL_OPERATE14 || mark == image + PRIMARY_CPU_OPERATE14;
}

struct PrimarySeatBinding14 {
    uint64_t player = 0, field = 0, top = 0; // the player and field objects, and the board (the address scan14.h gives)
    uint64_t operate = 0;                    // the object that operates the pair, as it was when the seat was bound (for the records)
    // The board was the one the player object's own cells are in (primaryBoardOf14) when the seat was bound: the seat
    // is read only while they still are.
    bool ownCells = false;
    bool bound() const { return player && field && top; }
};
struct PrimaryBinding14 {
    FieldContext context; // (roundKnown and a round number are set by bindPrimary14: owner14.h asks for them)
    std::array<PrimarySeatBinding14, 2> seat;
    bool both() const { return seat[0].bound() && seat[1].bound(); }
};

// Why a seat, or the place of its pair, could not be read (for the records: what the game shows then is to be learned).
struct PrimaryFault14 {
    enum Code : uint8_t { None, Objects, Changing, Flags, Place, Colours, Board };
    uint8_t code = None, seat = 0, operating = 0;
    uint32_t tick = 0, phase = 0, fieldState = 0, playerState = 0, locked = 0;
    std::array<uint32_t, 2> colours{};
    std::array<uint8_t, 34> pose{}; // the bytes the place of the pair is read from
    // The seat was not read at all (with Place and Colours it is: only where its pair is, or what it is, was not).
    bool unread() const { return code == Objects || code == Changing || code == Flags || code == Board; }
    // ... and for a reason that lasts: its objects, or its board, are not where they were bound any more. (Flags may be
    // of old objects whose class marks are still there, or of a moment of the game: PrimaryRebind14 gives it longer.)
    bool gone() const { return code == Objects || code == Board; }
};
inline std::optional<OwnedPublicPose14> readPrimaryPose14(FieldContext context, uint64_t player, const FieldRead14& read, bool& placed, PrimaryFault14* fault, std::optional<uint32_t>* garbageRandom = nullptr);

// What became of each board when the seats were bound (for the records: a round whose seats do not bind is not played).
struct PrimaryBindNote14 {
    struct Board {
        uint64_t top = 0;
        bool board = false;       // it read as a board
        int pages = 0, got = 0;   // the pages beside it, and those of them that could be read
        int owners = 0, seat = -1; // objects that operate a pair found beside it; the seat of the last one whose player read
    };
    std::vector<Board> boards;
    // The player objects looked at for a seat no board had an owner beside (bindPrimary14): the seat each names (-1: it
    // did not read as a live player), and the board its links lead to (0: none, or more than one).
    struct Player {
        uint64_t at = 0, top = 0;
        int seat = -1;
        bool cells = false; // the board is the one the player object's own cells are in (primaryBoardOf14), told with a 'c'
    };
    std::vector<Player> players;
    std::string text() const {
        std::string out;
        char one[96];
        for (const Board& b : boards) {
            std::snprintf(one, sizeof one, "%s%llx:%d:%d/%d:%d:%d", out.empty() ? "" : " ", static_cast<unsigned long long>(b.top), int(b.board), b.got, b.pages, b.owners, b.seat);
            out += one;
        }
        for (const Player& p : players) {
            std::snprintf(one, sizeof one, "%sp%llx:%d:%llx%s", out.empty() ? "" : " ", static_cast<unsigned long long>(p.at), p.seat, static_cast<unsigned long long>(p.top),
                          p.cells ? "c" : "");
            out += one;
        }
        return out.empty() ? "-" : out;
    }
};

// The objects that operate a pair beside a board, each with the player and field objects it names - what owner14.h
// finds, with the 8 KiB beside the board read a page at a time: a page there that cannot be read (the game's objects are
// large, and a page of one that has not been used yet is not there to read) no longer hides the object beside the
// board. (Records of the real game: a round in nine or so found its two boards and bound neither seat for as long as it
// lasted, so that nothing was pressed in it.) Of what is copied only the class mark and the three addresses are looked at.
inline std::vector<OwnerCandidate14> primaryOwners14(FieldContext context, uint64_t top, const FieldRead14& read, PrimaryBindNote14::Board* note = nullptr) {
    std::vector<OwnerCandidate14> found;
    if (note) *note = {}, note->top = top;
    if (!context.process.pid || !context.process.creationTime || !context.process.imageBase || !context.roundKnown || !context.round ||
        !verifiedFieldImage(context.process.image) || context.process.imageBase > UINT64_MAX - context.process.image.imageSize || top < 4096 || top > UINT64_MAX - 4096)
        return found;
    juraic::GameField grid;
    if (!readStableField14(top, read, grid)) return found;
    if (note) note->board = true;
    context.discoveryOnly = true;
    constexpr uint64_t PAGE = 4096, SPAN = 8192;
    std::array<uint8_t, SPAN> nearby{}, there{};
    const uint64_t from = top - 4096;
    for (uint64_t a = from; a < from + SPAN;) {
        const uint64_t end = std::min(from + SPAN, (a / PAGE + 1) * PAGE);
        const size_t at = size_t(a - from), n = size_t(end - a);
        const bool got = read(a, nearby.data() + at, n);
        if (got) std::fill(there.begin() + std::ptrdiff_t(at), there.begin() + std::ptrdiff_t(at + n), uint8_t(1));
        if (note) ++note->pages, note->got += got;
        a = end;
    }
    const auto image = context.process.imageBase;
    const auto objectAddress = [&](uint64_t a) { return primaryHeap14(context.process, a, 160); };
    const auto word = [](const uint8_t* bytes, size_t at) { uint64_t value = 0; std::memcpy(&value, bytes + at, 8); return value; };
    for (size_t at = 0; at + 64 <= nearby.size(); at += 4) {
        if (!there[at] || !there[at + 63]) continue;
        const uint64_t mark = word(nearby.data(), at);
        if (!primaryLocalOperate14(mark, image)) continue;
        const auto operate = from + at;
        const auto player = word(nearby.data(), at + 40), field = word(nearby.data(), at + 48);
        if (!objectAddress(player) || !objectAddress(field)) continue;
        // (the three objects name each other, read twice: a destructor may leave the last board as it was in the heap)
        std::array<uint8_t, 160> p{}, p2{};
        std::array<uint8_t, 88> f{}, f2{};
        std::array<uint8_t, 64> o{}, o2{};
        const auto cycle = [&](auto& pp, auto& ff, auto& oo) {
            return read(player, pp.data(), pp.size()) && read(field, ff.data(), ff.size()) && read(operate, oo.data(), oo.size()) && word(pp.data(), 0) == image + 0x2ac500 &&
                   word(ff.data(), 0) == image + 0x2b1470 && word(oo.data(), 0) == mark &&
                   word(pp.data(), mark == image + PRIMARY_CPU_OPERATE14 ? 152 : 64) == operate && word(pp.data(), 128) == field &&
                   word(ff.data(), 80) == player && word(oo.data(), 40) == player && word(oo.data(), 48) == field;
        };
        if (!cycle(p, f, o) || !cycle(p2, f2, o2)) continue;
        found.push_back({context, top, operate, player, field});
        if (found.size() > 16) return {}; // (too many to tell which)
    }
    if (note) note->owners = int(found.size());
    return found;
}

// Where a player object names things of its board (records of the real game, 24 bindings of 2026-11-17: the offset in
// the player object, and what stands there less the address of the board as scan14.h gives it). Every one of the 24
// had the second or the last of them; which of them an object has differs from round to round.
struct PrimaryPlayerLink14 {
    uint64_t at;
    int64_t less;
};
inline constexpr PrimaryPlayerLink14 PRIMARY_PLAYER_LINKS14[] = {{0xc0, -0x344}, {0xc8, -0x2a4}, {0xd8, 0x1cc}, {0x88, 0x1cc}};

// The board of a player object, by the game's own pointers and not by what lies beside what: the player object names
// (+0xc8) an object of the class image + 0x2a6460, which names the player back (+0x20) and the cells of its field
// (+0x58: an array of 128 cells of four bytes, eight to a row; the board as scan14.h gives it is its 26th cell, 0x64
// further). Records of the real game: every such object within the bytes recorded had the board there, wherever the
// game had put the array. 0: the player object names no such object, or what it names does not read as a board.
// Three pointers and the board's own cells are read, nothing else.
inline uint64_t primaryBoardOf14(const FieldProcess& process, uint64_t player, const FieldRead14& read) {
    const auto word = [&](uint64_t at, uint64_t& value) { return at >= 4096 && at <= UINT64_MAX - 8 && read(at, &value, 8); };
    uint64_t view = 0, mark = 0, back = 0, cells = 0, again = 0;
    if (player > UINT64_MAX - 0x100 || !word(player + 0xc8, view) || view > UINT64_MAX - 0x60) return 0;
    if (!word(view, mark) || mark != process.imageBase + 0x2a6460 || !word(view + 0x20, back) || back != player) return 0;
    if (!word(view + 0x58, cells) || cells < 4096 || cells > UINT64_MAX - 0x200) return 0;
    // (read twice: the objects of a round that is ending are taken apart one by one)
    if (!word(player + 0xc8, again) || again != view || !word(view + 0x58, again) || again != cells) return 0;
    juraic::GameField grid;
    return readStableField14(cells + 0x64, read, grid) ? cells + 0x64 : 0;
}

// The seats bound from the boards a scan found. A seat with two live players, or a player with two boards, stays unbound.
//
// players: the places the scan found the class mark of a player object at (FieldScan14::findAlso), for a seat no board
// had an owner beside. The objects of a seat are made one by one for every round and mostly lie side by side, the
// object that operates the pair right behind the board - but not always (records of the real game: in two rounds of
// twelve a seat's board had no such object within 4 KiB, and the seat stayed unbound to the end of the round). Such a
// seat is bound from its player object: one that reads as a live player of that seat (the same proof as for a seat
// read frame by frame: the three objects name each other and carry their classes, read twice), with a board among
// those found that its links name (PRIMARY_PLAYER_LINKS14) - one board only, one such player only, and not the board
// of the other seat.
inline PrimaryBinding14 bindPrimary14(const FieldProcess& process, const std::vector<uint64_t>& boards, const FieldRead14& read, PrimaryBindNote14* note = nullptr,
                                      const std::vector<uint64_t>& players = {}) {
    PrimaryBinding14 out;
    out.context.process = process, out.context.round = 1, out.context.roundKnown = true;
    std::array<std::vector<PrimarySeatBinding14>, 2> found;
    if (note) note->boards.clear();
    for (const uint64_t top : boards) {
        PrimaryBindNote14::Board told;
        for (const OwnerCandidate14& owner : primaryOwners14(out.context, top, read, &told)) {
            bool placed = false; // (the seat is the player object's; where its pair is does not matter here)
            const auto pose = readPrimaryPose14(out.context, owner.player, read, placed, nullptr);
            if (!pose || pose->owner.field != owner.field || pose->seat < 0 || pose->seat > 1) continue;
            told.seat = pose->seat;
            // (boards close together - records of the real game: two 4024 bytes apart, and seat 0's object beside both,
            //  so that the seat had two boards and stayed unbound for the whole round: the object is of the board its
            //  player's own cells are in, when the player names them)
            const uint64_t own = primaryBoardOf14(process, owner.player, read);
            if (own && own != top) continue;
            found[size_t(pose->seat)].push_back({owner.player, owner.field, top, owner.operate, own != 0});
        }
        if (note && note->boards.size() < 8) note->boards.push_back(told);
    }
    for (size_t s = 0; s < 2; ++s) {
        if (found[s].empty()) continue;
        bool one = true;
        for (const PrimarySeatBinding14& b : found[s])
            if (b.player != found[s][0].player || b.top != found[s][0].top || b.field != found[s][0].field) one = false;
        if (one) out.seat[s] = found[s][0];
    }
    // (one board for both seats is the board of neither)
    if (out.seat[0].bound() && out.seat[1].bound() && out.seat[0].top == out.seat[1].top) out.seat[0] = out.seat[1] = PrimarySeatBinding14{};
    if (note) note->players.clear();
    if (players.empty() || (!found[0].empty() && !found[1].empty())) return out;
    std::array<std::vector<PrimarySeatBinding14>, 2> byPlayer;
    for (const uint64_t player : players) {
        bool placed = false;
        PrimaryFault14 why;
        const auto pose = readPrimaryPose14(out.context, player, read, placed, &why);
        if (!pose || pose->seat < 0 || pose->seat > 1) {
            // (-1: it read, with a seat that is none; -10 - PrimaryFault14::Code: it did not read, and why)
            if (note && note->players.size() < 8) note->players.push_back({player, 0, pose ? -1 : -10 - int(why.code)});
            continue;
        }
        const size_t s = size_t(pose->seat);
        if (!found[s].empty()) continue; // (a seat the boards have bound, or left unbound as not to be told)
        // (the board the player's own cells are in, wherever it lies and whether the scan found it or not; only a
        //  player object that names no cells is asked for the things it names beside a board)
        uint64_t top = primaryBoardOf14(process, player, read);
        const bool cells = top != 0;
        if (!cells) {
            bool one = true;
            for (const PrimaryPlayerLink14& link : PRIMARY_PLAYER_LINKS14) {
                uint64_t named = 0;
                if (!read(player + link.at, &named, 8)) continue;
                for (const uint64_t board : boards)
                    if (named == uint64_t(int64_t(board) + link.less)) one = one && (!top || top == board), top = board;
            }
            juraic::GameField grid;
            if (!top || !one || !readStableField14(top, read, grid)) top = 0;
        }
        if (note && note->players.size() < 8) note->players.push_back({player, top, pose->seat, cells});
        if (top) byPlayer[s].push_back({player, pose->owner.field, top, pose->owner.operate, cells});
    }
    for (size_t s = 0; s < 2; ++s) {
        if (!found[s].empty() || byPlayer[s].size() != 1) continue;
        const uint64_t other = out.seat[1 - s].bound() ? out.seat[1 - s].top : byPlayer[1 - s].size() == 1 && found[1 - s].empty() ? byPlayer[1 - s][0].top : 0;
        if (byPlayer[s][0].top != other) out.seat[s] = byPlayer[s][0];
    }
    return out;
}

// The pair in play and the two shown as next, as the player's object of the next pairs holds them: three records of
// (kind, axis, child), read twice with the object's own marks before and after. Exactly the reads of
// published_next14.h (nothing of the pairs beyond the window, nor of where they come from).
// What became of a read of that window, for the records of the real game (what the window holds before the first pair of
// a round, and while it slides, is to be learned from them): how far the read came, the three numbers that say whether
// the window stands, and the three records as read - the same bytes a read that succeeds takes, nothing more.
struct PrimaryWindow14 {
    enum Stage : uint8_t { None, Read, Pointer, Head, Marks, Sliding, Records, Changing, Kind, Colours };
    uint8_t stage = None; // None: not asked
    std::array<uint32_t, 3> slide{};
    std::array<std::array<uint32_t, 3>, 3> records{};
    bool operator==(const PrimaryWindow14&) const = default;
};
inline std::optional<std::array<juraic::Pair, 3>> readPublicPairs14(const FieldContext& context, const OwnedPublicPose14& p, const FieldRead14& read, PrimaryWindow14* why = nullptr) {
    PrimaryWindow14 told;
    const auto failed = [&](uint8_t stage) {
        told.stage = stage;
        if (why) *why = told;
        return std::optional<std::array<juraic::Pair, 3>>();
    };
    const auto base = context.process.imageBase;
    const auto heap = [&](uint64_t a, uint64_t n) { return primaryHeap14(context.process, a, n); };
    struct Meta {
        uint64_t pointer = 0, primary = 0, secondary = 0, owner = 0;
        std::array<uint32_t, 3> slide{};
        bool operator==(const Meta&) const = default;
    };
    uint64_t root = 0;
    uint8_t stopped = PrimaryWindow14::Read;
    const auto meta = [&](Meta& v) {
        if (!read(p.owner.player + 0x90, &v.pointer, 8) || v.pointer < 0x1020) return stopped = PrimaryWindow14::Pointer, false;
        const auto candidate = v.pointer - 0x20;
        if (root && candidate != root) return stopped = PrimaryWindow14::Changing, false;
        root = candidate;
        if (!heap(root, 0xf4) || !read(root, &v.primary, 8) || !read(root + 0x20, &v.secondary, 8) || !read(root + 0x28, &v.owner, 8) ||
            !read(root + 0x38, v.slide.data(), 12))
            return stopped = PrimaryWindow14::Head, false;
        if (v.primary != base + 0x2ac288 || v.secondary != base + 0x2ac300 || v.owner != p.owner.player) return stopped = PrimaryWindow14::Marks, false;
        told.slide = v.slide;
        // (slide: the window is standing, not on its way to the next pairs)
        if (v.slide[0] != 0 || v.slide[2] != 0) return stopped = PrimaryWindow14::Sliding, false;
        return true;
    };
    using Record = std::array<uint32_t, 3>;
    std::array<Record, 3> x{}, y{};
    const auto records = [&](auto& into) { return read(root + 0x88, into[0].data(), 12) && read(root + 0xc0, into[1].data(), 12) && read(root + 0xe8, into[2].data(), 12); };
    Meta a, b;
    if (!meta(a)) {
        // (the player's window, on its way: its three records as they are then, for the record only)
        if (why && stopped == PrimaryWindow14::Sliding && records(x) && records(y) && x == y) told.records = x;
        return failed(stopped);
    }
    if (!records(x) || !records(y)) return failed(PrimaryWindow14::Records);
    told.records = x;
    if (x != y) return failed(PrimaryWindow14::Changing);
    if (!meta(b)) return failed(stopped);
    if (a != b) return failed(PrimaryWindow14::Changing);
    std::array<juraic::Pair, 3> out;
    for (size_t i = 0; i < 3; ++i) {
        if (x[i][0] != 0x201) return failed(PrimaryWindow14::Kind);
        if (x[i][1] < 1 || x[i][1] > 5 || x[i][2] < 1 || x[i][2] > 5) return failed(PrimaryWindow14::Colours);
        out[i] = {juraic::Cell(x[i][1]), juraic::Cell(x[i][2])};
    }
    told.stage = PrimaryWindow14::Read;
    if (why) *why = told;
    return out;
}

// The pairs beyond the window, from the game's own list of the round's pairs - read only while this is set (it is not,
// unless whoever plays by the seats sets it: PrimaryDrive14::Setup::listedPairs).
inline std::atomic<bool> primaryListedPairs14{false};
inline std::atomic<bool> primaryGarbageRandom14{false};
// The verified game shuffles the remaining columns by removing one in order for each RNG draw,
// then starts the drop at the cursor left by the previous drop (local records of 2026-11-18 and 19).
inline std::array<uint8_t, juraic::W> primaryGarbageOrder14(uint32_t state, uint8_t cursor) {
    std::array<uint8_t, juraic::W> remaining{0, 1, 2, 3, 4, 5}, order{};
    for (size_t i = 0; i < order.size(); ++i) {
        const size_t count = order.size() - i;
        state = state * 1566083941u + 2531011u;
        const size_t pick = ((state >> 16) * count) >> 16;
        order[(i + order.size() - cursor) % order.size()] = remaining[pick];
        for (size_t j = pick; j + 1 < count; ++j) remaining[j] = remaining[j + 1];
    }
    return order;
}
inline constexpr size_t LISTED14 = 8; // pairs read beyond the window's three
// The pairs `from` .. `from + LISTED14 - 1` of the round. The window object of the player holds, 0x30 from its head, the
// object of the lists (its mark: the image + 0x2aa128): 0x24 into it five colours, one for each number of a list, then
// three lists of 256 pieces. Normal pairs take two colours; the accessor uses the low eight bits of
// the piece index. A regenerating list, scripted colours or shapes, or shapes of another size do not
// have that correspondence between pairs put and entries in this list.
inline bool readListedPairs14(const FieldContext& context, uint64_t player, const FieldRead14& read, int windowAt, const std::array<juraic::Pair, 3>& window, int from,
                              std::array<juraic::Pair, LISTED14>& out) {
    const uint64_t base = context.process.imageBase;
    const auto heap = [&](uint64_t a, uint64_t n) { return primaryHeap14(context.process, a, n); };
    constexpr int PAIRS = 128; // pairs in a list
    if (windowAt < 0 || from < 0) return false;
    windowAt %= PAIRS, from %= PAIRS;
    uint64_t pointer = 0, lists = 0, mark = 0, primary = 0, owner = 0;
    if (!heap(player, 0x98) || !read(player + 0x90, &pointer, 8) || pointer < 0x1020) return false;
    const uint64_t root = pointer - 0x20;
    if (!heap(root, 0x150) || !read(root, &primary, 8) || primary != base + 0x2ac288 || !read(root + 0x28, &owner, 8) || owner != player) return false;
    if (!read(root + 0x30, &lists, 8) || !heap(lists, 0x338) || !read(lists, &mark, 8) || mark != base + 0x2aa128) return false;
    uint8_t regenerate = 0, count = 0;
    uint64_t scriptedColours = 0, scriptedShapes = 0;
    std::array<uint32_t, 16> shapes{};
    if (!read(root + 0xb8, &regenerate, 1) || regenerate || !read(root + 0x138, &scriptedColours, 8) || scriptedColours ||
        !read(root + 0x148, &scriptedShapes, 8) || scriptedShapes || !read(root + 0x44, shapes.data(), sizeof shapes) ||
        !std::all_of(shapes.begin(), shapes.end(), [](uint32_t shape) { return (shape >> 8) == 2; })) return false;
    std::array<uint8_t, 2> limits{};
    if (!read(root + 0x84, &count, 1) || !read(lists + 0x20, limits.data(), limits.size()) ||
        (limits[0] != 2 && limits[0] != 3) || limits[1] != limits[0] + 2 || count < limits[0] || count > limits[1]) return false;
    std::array<uint32_t, 5> colours{};
    if (!read(lists + 0x24, colours.data(), sizeof colours)) return false;
    unsigned seen = 0;
    for (const uint32_t c : colours) {
        if (c < 1 || c > 5 || (seen >> c & 1)) return false;
        seen |= 1u << c;
    }
    std::array<uint8_t, 256> list{};
    if (!read(lists + 0x38 + 0x100u * (count - limits[0]), list.data(), list.size()) ||
        !std::all_of(list.begin(), list.end(), [count](uint8_t c) { return c < count; })) return false;
    const auto pair = [&](int index) {
        const size_t at = size_t(index % PAIRS) * 2;
        return juraic::Pair{juraic::Cell(colours[list[at]]), juraic::Cell(colours[list[at + 1]])};
    };
    for (int k = 0; k < 3; ++k) {
        const auto p = pair(windowAt + k);
        if (p.axis != window[size_t(k)].axis || p.child != window[size_t(k)].child) return false;
    }
    for (size_t k = 0; k < out.size(); ++k) out[k] = pair(from + int(k));
    return true;
}

// How far the column a pair is drawn at may be from the column it is in, in cells (the most seen: one and a half).
inline constexpr float SLIDE14 = 2.5f;

// The public state of a player's objects, as owned_pose14.h reads it - the same reads, the same proof that the three
// objects are the player's, the pair's operator and the field of the verified program version, read twice - but a seat
// is given whenever its objects are there: the place of the pair is taken only where it reads as a place (`placed`),
// and not at all asked of a seat with no pair in its hands. (owned_pose14.h gives no seat at all while the place of the
// last pair reads otherwise - after a pair put right after a turn, through the whole chain that follows; and in the
// frames between two rounds - and so nothing of the field's state, which says when the next pair comes.)
inline std::optional<OwnedPublicPose14> readPrimaryPose14(FieldContext context, uint64_t player, const FieldRead14& read, bool& placed, PrimaryFault14* fault = nullptr, std::optional<uint32_t>* garbageRandom) {
    placed = false;
    if (garbageRandom) garbageRandom->reset();
    const auto failed = [&](PrimaryFault14::Code code) {
        if (fault) fault->code = code;
        return std::optional<OwnedPublicPose14>();
    };
    if (!context.process.pid || !context.process.creationTime || !context.process.imageBase || !verifiedFieldImage(context.process.image) ||
        context.process.imageBase > UINT64_MAX - context.process.image.imageSize || player < 4096 || player > UINT64_MAX - 160)
        return failed(PrimaryFault14::Objects);
    const auto image = context.process.imageBase;
    const auto word = [](const auto& a, size_t at) { uint64_t v = 0; std::memcpy(&v, a.data() + at, 8); return v; };
    const auto integer = [](const auto& a, size_t at) { uint32_t v = 0; std::memcpy(&v, a.data() + at, 4); return v; };
    const auto number = [](const auto& a, size_t at) { float v = 0; std::memcpy(&v, a.data() + at, 4); return v; };
    const auto heap = [&](uint64_t a, uint64_t n) { return primaryHeap14(context.process, a, n); };
    struct Pass {
        std::array<uint8_t, 160> p{};
        std::array<uint8_t, 64> o{};
        std::array<uint8_t, 88> f{};
        std::array<uint8_t, 34> pose{};
        std::array<uint8_t, 8> colors{}, phase{}, counts{};
        uint32_t locked = 0;
        uint8_t operating = 0, active = 0, hold = 0;
    };
    Pass a, b;
    uint64_t operate = 0, field = 0;
    const auto pass = [&](Pass& v, bool first) {
        if (!read(player, v.p.data(), v.p.size()) || word(v.p, 0) != image + 0x2ac500) return false;
        if (first) operate = word(v.p, 152), field = word(v.p, 128);
        if (!heap(operate, 0xee) || !heap(field, 0x2d4) || word(v.p, 152) != operate || word(v.p, 128) != field) return false;
        return read(operate, v.o.data(), v.o.size()) && read(field, v.f.data(), v.f.size()) && primaryLocalOperate14(word(v.o, 0), image) && word(v.f, 0) == image + 0x2b1470 &&
               word(v.o, 40) == player && word(v.o, 48) == field && word(v.f, 80) == player && read(operate + 0xcc, v.pose.data(), v.pose.size()) &&
               read(operate + 0xa8, v.colors.data(), v.colors.size()) && read(operate + 0x9d, &v.operating, 1) && read(operate + 0xa0, &v.locked, 4) &&
               read(field + 0x2cc, v.phase.data(), v.phase.size()) && read(field + 0x108, v.counts.data(), v.counts.size()) && read(field + 0x11c, &v.active, 1) &&
               read(field + 0x18d, &v.hold, 1);
    };
    if (!pass(a, true) || !pass(b, false)) return failed(PrimaryFault14::Objects);
    if (word(a.o, 0) != word(b.o, 0) || a.pose != b.pose || a.colors != b.colors || a.phase != b.phase || a.counts != b.counts || a.active != b.active || a.hold != b.hold || a.operating != b.operating ||
        a.locked != b.locked || a.p[0x5e] != b.p[0x5e] || integer(a.p, 0x50) != integer(b.p, 0x50))
        return failed(PrimaryFault14::Changing);
    const auto c0 = integer(b.colors, 0), c1 = integer(b.colors, 4);
    if (fault) {
        fault->operating = b.operating, fault->tick = integer(b.counts, 0), fault->phase = integer(b.phase, 0), fault->fieldState = integer(b.phase, 4);
        fault->playerState = integer(b.p, 0x50), fault->locked = b.locked, fault->colours = {c0, c1}, fault->pose = b.pose;
    }
    if (b.operating > 1 || b.p[0x5e] > 1 || b.active > 1 || b.hold > 1) return failed(PrimaryFault14::Flags);
    context.discoveryOnly = true;
    OwnedPublicPose14 out;
    out.owner = {context, 0, operate, player, field};
    out.seat = b.p[0x5e];
    out.cpuControlled = word(b.o, 0) == image + PRIMARY_CPU_OPERATE14;
    out.operating = b.operating != 0, out.lockedPairs = b.locked;
    out.phase = integer(b.phase, 0), out.fieldState = integer(b.phase, 4), out.playerState = integer(b.p, 0x50);
    out.count108 = integer(b.counts, 0), out.count10c = integer(b.counts, 4), out.fieldActive = b.active, out.fieldHold = b.hold;
    out.currentColors = {int(c0 <= 6 ? c0 : 0), int(c1 <= 6 ? c1 : 0)};
    // The two player copies already include the seat's garbage RNG at +0x58.
    if (garbageRandom && integer(a.p, 0x58) == integer(b.p, 0x58)) *garbageRandom = integer(b.p, 0x58);
    if (!out.operating) return out; // (no pair in the hands: nothing is asked of where the last one was)
    // The place of the pair in the hands: its column and row as the game counts them (the two bytes), its direction, and
    // the half of the cell from the height it is drawn at. The number of its column across is the one it is drawn at and
    // runs behind the column while the pair slides - half a cell for a step to the side, and for some frames by up to a
    // cell and a half when a turn has pushed it off a wall or a column (records of the real game: 317 frames of 317 read
    // so, the pair then put where its column bytes had it) - so it is asked only to be near the column.
    const float x = number(b.pose, 0), y = number(b.pose, 4), shownX = number(b.pose, 16), shownY = number(b.pose, 20);
    const bool place = std::isfinite(x) && std::isfinite(y) && std::isfinite(shownX) && std::isfinite(shownY) && y >= -2 && y <= 11 &&
                       std::abs(y - std::round(y)) <= .001f && shownX == x && (shownY == y || shownY == y - .5f) && b.pose[32] >= 1 && b.pose[32] <= 6 &&
                       std::abs(x - float(b.pose[32] - 1)) <= SLIDE14 && b.pose[33] == int(y) + 3 && integer(b.pose, 28) <= 3 &&
                       !(integer(b.pose, 28) == 1 && b.pose[32] == 6) && !(integer(b.pose, 28) == 3 && b.pose[32] == 1); // (no child beside the board)
    const bool colours = c0 >= 1 && c0 <= 5 && c1 >= 1 && c1 <= 5;
    if (!place || !colours) {
        if (fault) fault->code = place ? PrimaryFault14::Colours : PrimaryFault14::Place;
        return out; // (in the hands, its place not read: `placed` false)
    }
    out.x = int(b.pose[32]) - 1, out.y = 11 - int(y), out.upper = shownY == y - .5f, out.dir = int(integer(b.pose, 28));
    out.horizontalMoving = x != float(out.x);
    placed = true;
    return out;
}

// Verified game's scoring interface. The total and the points available to convert into garbage
// are capped at 99,999,999. The current unconverted points may exceed the rate between conversions.
struct PrimaryScore14 {
    uint32_t total = 0, pool = 0, lastWave = 0;
    bool operator==(const PrimaryScore14& other) const { return total == other.total && pool == other.pool && lastWave == other.lastWave; }
};
inline std::optional<PrimaryScore14> readPrimaryScore14(const FieldContext& context, uint64_t player, uint64_t operate, const FieldRead14& read) {
    uint64_t score = 0;
    std::array<uint64_t, 2> owner{};
    std::array<uint32_t, 3> values{};
    if (!verifiedFieldImage(context.process.image) || !primaryHeap14(context.process, player, 0xc8) ||
        !read(player + 0xc0, &score, 8) || !primaryHeap14(context.process, score, 0x48) ||
        (operate <= UINT64_MAX - 0x100 && score + 0x48 > operate && score < operate + 0x100) ||
        !read(score, owner.data(), sizeof owner) || owner[0] != context.process.imageBase + 0x2afbd0 || owner[1] != player ||
        !read(score + 0x3c, values.data(), sizeof values) || values[0] > 99999999u || values[1] > 99999999u || values[2] > 99999999u) return {};
    return PrimaryScore14{values[0], values[1], values[2]};
}

// One seat as it is now.
struct PrimarySeat14 {
    OwnedPublicPose14 pose;       // the pair in play (pose.operating: in the player's hands), the counters of the field
    bool placed = false;          // a pair is in the hands and where it is was read (pose.x, y, upper, dir, and its colours)
    juraic::GameField field;      // the pieces at rest, 14 rows
    bool pairsKnown = false;      // `pairs` was read (the window stood still, and its first pair is the pair in play)
    std::array<juraic::Pair, 3> pairs{}; // the pair in play and the two shown as next
    std::optional<uint8_t> input; // 選んだ相手の席の論理入力。0は全て離れている。
    std::optional<PrimaryScore14> score;
    std::optional<uint32_t> garbageRandom; // current seat state; zero is valid
    std::optional<uint8_t> garbageCursor;  // 0..5, only for the verified rule that shuffles all six columns
    // The pairs after those three, from the game's own list (only while primaryListedPairs14 is set, and `pairs` was read).
    bool listedKnown = false;
    std::array<juraic::Pair, LISTED14> listed{};
    // With no pair in the hands: what the window's three records read as, when they read as pairs and the window stands
    // (which pairs they are then - the pair to come first, or the one just put - is to be learned from records of the
    // real game; nothing goes by them yet).
    bool windowKnown = false;
    std::array<juraic::Pair, 3> window{};
    PrimaryWindow14 windowRead; // what became of the read of the window (for the records)
    // Numbers of the field object, read with the seat when they can be (`counted`):
    //  - the pieces in the field and how many of them are garbage, as the game counts them: garbage that has fallen
    //    is in these counts at once, and in the board only when the next pair has been put (the board has had the
    //    count of its pieces in every record without such garbage, 1896 of 1896);
    //  - the height of the sixth column, with such garbage (the same 1896 records; no other column has such a number
    //    in the object);
    //  - the garbage the chain the field is busy with makes in all, there from the frame the chain begins, 0 when it
    //    is over (sooner than the screen shows it; the player goes by it).
    bool counted = false;
    int pieces = 0, garbagePieces = 0, sixthHeight = 0;
    uint32_t chainGarbage = 0;
    // The pieces the game counts in each column (`columnsKnown`: read, and their sum is `pieces`): eight bytes that the
    // field object names (the address at its 0x140, one byte for a column, a 0 before the first and after the sixth).
    // Garbage that has fallen is in them at once, as in `pieces`; a piece in the 14th row is counted with its column.
    // (Records of the real game of 2026-11-17, both seats: the sum of the six was `pieces` in 6804 records of 6811, the
    // seven others at the end of a round, where `pieces` is 0 again; the two bytes beside them 0 in all; the address
    // led to them however the objects of the round lay.)
    bool columnsKnown = false;
    std::array<uint8_t, 6> columnPieces{};
    // The garbage waiting above the field as the game shows it there (`noticeKnown`: read): the number of the object
    // the player names (the address at its 0xb0: an object of the class image + 0x2ac998 that names the player back at
    // its 0x20; the number at its 0x130). Records of the real game of 2026-11-17: the eight such objects within the
    // bytes recorded were named so; the number goes up with every step of the other seat's chain, some frames before
    // the pictures have it, and is exact where the pictures' reading of a large notice is not (880 for 870).
    bool noticeKnown = false;
    int notice = 0;
    // The game's own account of that garbage (`trayKnown`: read), four numbers of the same object for the seat the
    // garbage comes from (at 0x30 + 32 * that seat): whether that seat's chain is running, what the chain's last step
    // sent and is still on its way to the notice, what has arrived and cannot fall yet (the chain still runs), what can
    // fall. Records of the real game of 2026-11-17 (the object lay within the bytes recorded in four rounds, 2,066 and
    // 1,243 records of the 1P seat, 330 of the 2P seat): the number shown follows the last two 4 to 20 frames later;
    // garbage on its way joins the third number some 20 frames after its step; the seat's own chain takes from the
    // third number first, then from the fourth; when the other seat's chain ends the third goes into the fourth in
    // that frame. The chain's garbage of the field object (`chainGarbage`) is that of its points alone: what was sent
    // was more by up to 3 (points held over), and a chain worth no piece by itself sent 3.
    bool trayKnown = false, trayChain = false;
    int trayFlying = 0, trayPending = 0, trayConfirmed = 0;
    // Whether the all-clear bonus waits for the seat's next chain (`bonusKnown`: read): the byte at 0xd0 of the field
    // object. Records of the real game of 2026-11-17 and 11-18 (the 2P seat, five all-clears after which the round went
    // on): 1 in 0.989 of the 1,312 records between the all-clear and the next chain, in none of 1,412 others; in one
    // round followed frame by frame it became 1 in the frame the chain that emptied the field was over, for both seats,
    // and 0 again in the frame the pair of the next chain was put, and it was 0 as each round began.
    bool bonusKnown = false, bonusWaits = false;
};
// A bound seat read once: the pose, then the board, then the pose again - they must agree, so that the board is the one
// the pose belongs to (no frame of the game went by in between). Nothing when a read fails or the objects have changed.
inline std::optional<PrimarySeat14> readPrimarySeat14(const PrimaryBinding14& binding, int seat, const FieldRead14& read, PrimaryFault14* fault = nullptr) {
    if (seat < 0 || seat > 1 || !binding.seat[size_t(seat)].bound()) return {};
    const PrimarySeatBinding14& b = binding.seat[size_t(seat)];
    if (fault) *fault = {}, fault->seat = uint8_t(seat);
    const auto fail = [&](PrimaryFault14::Code code) {
        if (fault) fault->code = code;
        return std::optional<PrimarySeat14>();
    };
    bool placedBefore = false, placedAfter = false;
    const bool readRandom = primaryGarbageRandom14.load(std::memory_order_relaxed);
    std::optional<uint32_t> randomBefore, randomAfter;
    const auto before = readPrimaryPose14(binding.context, b.player, read, placedBefore, fault, readRandom ? &randomBefore : nullptr);
    if (!before) return {};
    if (before->seat != seat || before->owner.field != b.field) return fail(PrimaryFault14::Objects);
    PrimarySeat14 out;
    // (The board is the one the player's own cells are in, when the player names them: with a new round the objects may
    //  be made where they were and the board elsewhere, and the bytes of the old board still read as a board. A seat
    //  bound by those cells is not read once they name no board.)
    if (const uint64_t own = primaryBoardOf14(binding.context.process, b.player, read); (own || b.ownCells) && own != b.top) return fail(PrimaryFault14::Board);
    const auto readCursor = [&]() -> std::optional<uint8_t> {
        uint8_t flags = 0, cursor = 0;
        if (readRandom && primaryHeap14(binding.context.process, b.player, 0xf9) && read(b.player + 0xf8, &flags, 1) && (flags & 4) &&
            read(b.field + 0x1e8, &cursor, 1) && cursor < juraic::W) return cursor;
        return {};
    };
    const bool readInput = !before->cpuControlled && primaryInputSeat14.load(std::memory_order_relaxed) == seat;
    const auto inputBefore = readInput ? readPrimaryInput14(binding.context, b.player, seat, read) : std::nullopt;
    const auto cursorBefore = readCursor();
    if (!readStableField14(b.top, read, out.field)) return fail(PrimaryFault14::Board);
    std::optional<std::array<juraic::Pair, 3>> pairs;
    if (!before->operating || placedBefore) pairs = readPublicPairs14(binding.context, *before, read, &out.windowRead);
    {
        uint16_t counts[2] = {0, 0};
        uint8_t sixth = 0;
        uint32_t chain = 0;
        if (read(b.field + 0x188, counts, 4) && read(b.field + 0x238, &sixth, 1) && read(b.field + 0x254, &chain, 4) && counts[0] <= 6 * 14 && counts[1] <= counts[0] && sixth <= 14 &&
            chain < 100000)
            out.counted = true, out.pieces = counts[0], out.garbagePieces = counts[1], out.sixthHeight = sixth, out.chainGarbage = chain;
        uint8_t bonus = 0;
        if (read(b.field + 0xd0, &bonus, 1) && bonus <= 1) out.bonusKnown = true, out.bonusWaits = bonus == 1;
        // (the pieces of each column: not where the object that operates the pair lies, nor in the program's own image)
        uint64_t columns = 0;
        std::array<uint8_t, 8> eight{};
        const auto image = binding.context.process.imageBase;
        const uint64_t operate = before->owner.operate;
        if (out.counted && read(b.field + 0x140, &columns, 8) && columns >= 4096 && columns <= UINT64_MAX - 8 &&
            !(columns >= image && columns - image < binding.context.process.image.imageSize) && !(columns + 8 > operate && columns < operate + 0x100) &&
            read(columns, eight.data(), eight.size()) && eight[0] == 0 && eight[7] == 0) {
            int sum = 0;
            bool fits = true;
            for (size_t x = 0; x < 6; ++x) sum += eight[x + 1], fits = fits && eight[x + 1] <= 14;
            if (fits && sum == out.pieces) {
                out.columnsKnown = true;
                for (size_t x = 0; x < 6; ++x) out.columnPieces[x] = eight[x + 1];
            }
        }
        // (the garbage waiting above the field: three addresses and the number, not where the object that operates the
        //  pair lies, nor in the program's own image)
        uint64_t tray = 0, mark = 0, back = 0;
        uint32_t waiting = 0;
        if (read(b.player + 0xb0, &tray, 8) && primaryHeap14(binding.context.process, tray, 0x140) &&
            !(tray + 0x134 > operate && tray < operate + 0x100) && read(tray, &mark, 8) && mark == image + 0x2ac998 && read(tray + 0x20, &back, 8) && back == b.player &&
            read(tray + 0x130, &waiting, 4) && waiting < 100000)
            out.noticeKnown = true, out.notice = int(waiting);
        uint32_t account[4] = {0, 0, 0, 0};
        if (out.noticeKnown && read(tray + 0x30 + 32 * uint64_t(1 - seat), account, 16) && account[0] <= 1 && account[1] < 100000 && account[2] < 100000 && account[3] < 100000)
            out.trayKnown = true, out.trayChain = account[0] == 1, out.trayFlying = int(account[1]), out.trayPending = int(account[2]), out.trayConfirmed = int(account[3]);
    }
    std::array<juraic::Pair, LISTED14> listed{};
    const bool listedRead = pairs && before->operating && primaryListedPairs14.load(std::memory_order_relaxed) &&
                            int((*pairs)[0].axis) == before->currentColors[0] && int((*pairs)[0].child) == before->currentColors[1] &&
                            readListedPairs14(binding.context, b.player, read, int(before->lockedPairs), *pairs, int(before->lockedPairs) + 3, listed);
    out.score = readPrimaryScore14(binding.context, b.player, before->owner.operate, read);
    const auto cursorAfter = readCursor();
    if (inputBefore && primaryInputSeat14.load(std::memory_order_relaxed) == seat && readPrimaryInput14(binding.context, b.player, seat, read) == inputBefore)
        out.input = inputBefore->held;
    const auto after = readPrimaryPose14(binding.context, b.player, read, placedAfter, fault, readRandom ? &randomAfter : nullptr);
    if (!after) return {};
    if (after->count108 != before->count108 || after->lockedPairs != before->lockedPairs || after->operating != before->operating || placedAfter != placedBefore ||
        after->x != before->x || after->y != before->y || after->upper != before->upper || after->dir != before->dir || after->currentColors != before->currentColors ||
        after->cpuControlled != before->cpuControlled || after->owner.operate != before->owner.operate || after->owner.field != before->owner.field || after->phase != before->phase ||
        after->fieldState != before->fieldState || after->playerState != before->playerState || after->fieldActive != before->fieldActive ||
        after->fieldHold != before->fieldHold || after->horizontalMoving != before->horizontalMoving)
        return fail(PrimaryFault14::Changing);
    out.pose = *after, out.placed = placedAfter;
    if (randomBefore && randomBefore == randomAfter) {
        out.garbageRandom = randomBefore;
        if (cursorBefore == cursorAfter) out.garbageCursor = cursorBefore;
    }
    // (the first record is the pair in play: a window that shows another is on its way or belongs to another pair)
    if (pairs && !after->operating) out.window = *pairs, out.windowKnown = true;
    else if (pairs && int((*pairs)[0].axis) == after->currentColors[0] && int((*pairs)[0].child) == after->currentColors[1]) out.pairs = *pairs, out.pairsKnown = true;
    if (out.pairsKnown && listedRead) out.listedKnown = true, out.listed = listed;
    return out;
}

// The frame counter of a bound seat alone (four of the bytes readPrimarySeat14 reads): what a reader looks at every
// millisecond to know that a frame of the game has gone by, before it reads the seat.
inline std::optional<uint32_t> readPrimaryTick14(const PrimaryBinding14& binding, int seat, const FieldRead14& read) {
    if (seat < 0 || seat > 1 || !binding.seat[size_t(seat)].bound()) return {};
    uint32_t tick = 0;
    if (!read(binding.seat[size_t(seat)].field + 0x108, &tick, 4)) return {};
    return tick;
}

// Whether two reads of a seat show the same thing.
inline bool samePrimarySeat14(const std::optional<PrimarySeat14>& a, const std::optional<PrimarySeat14>& b) {
    if (!a || !b) return !a && !b;
    const OwnedPublicPose14 &p = a->pose, &q = b->pose;
    if (a->placed != b->placed || a->windowKnown != b->windowKnown || !(a->windowRead == b->windowRead)) return false;
    for (size_t i = 0; a->windowKnown && i < 3; ++i)
        if (a->window[i].axis != b->window[i].axis || a->window[i].child != b->window[i].child) return false;
    if (p.count108 != q.count108 || p.lockedPairs != q.lockedPairs || p.operating != q.operating || p.x != q.x || p.y != q.y || p.upper != q.upper ||
        p.dir != q.dir || p.horizontalMoving != q.horizontalMoving || p.currentColors != q.currentColors || p.phase != q.phase ||
        p.fieldState != q.fieldState || p.playerState != q.playerState || p.fieldActive != q.fieldActive || p.fieldHold != q.fieldHold ||
        p.owner.player != q.owner.player || p.owner.field != q.owner.field || !(a->field == b->field) || a->pairsKnown != b->pairsKnown)
        return false;
    if (a->counted != b->counted || a->pieces != b->pieces || a->garbagePieces != b->garbagePieces || a->sixthHeight != b->sixthHeight || a->chainGarbage != b->chainGarbage) return false;
    if (a->columnsKnown != b->columnsKnown || a->columnPieces != b->columnPieces) return false;
    if (a->bonusKnown != b->bonusKnown || a->bonusWaits != b->bonusWaits) return false;
    if (a->noticeKnown != b->noticeKnown || a->notice != b->notice) return false;
    if (a->trayKnown != b->trayKnown || a->trayChain != b->trayChain || a->trayFlying != b->trayFlying || a->trayPending != b->trayPending || a->trayConfirmed != b->trayConfirmed) return false;
    for (size_t i = 0; a->pairsKnown && i < 3; ++i)
        if (a->pairs[i].axis != b->pairs[i].axis || a->pairs[i].child != b->pairs[i].child) return false;
    if (!(a->score == b->score)) return false;
    if (a->input != b->input || a->garbageRandom != b->garbageRandom || a->garbageCursor != b->garbageCursor || a->listedKnown != b->listedKnown) return false;
    for (size_t i = 0; a->listedKnown && i < LISTED14; ++i)
        if (a->listed[i].axis != b->listed[i].axis || a->listed[i].child != b->listed[i].child) return false;
    return true;
}

// Both seats as they are now, for whoever plays by them.
struct PrimarySample14 {
    FieldProcess process;
    uint64_t generation = 0;                 // of the binding: another one after every new scan
    uint64_t serial = 0;                     // of the sample: one more with every sample that shows something new
    uint64_t observedMs = 0;                 // the reader's clock when the last seat was read
    uint64_t observedUs = 0;                 // the same clock in microseconds
    std::array<std::optional<PrimarySeat14>, 2> seat;
};

// Following two bound seats frame by frame, a look at a time (a reader looks every millisecond). A look reads the two
// frame counters; the seats are read whole only when a counter has moved on since the last sample, once more in the
// look after that (what the game writes later in its frame is then in), when the last sample lacks a seat, and every
// `wholeEveryUs` (in case something changes while the counters stand). A whole read that shows something new is the
// next sample, with the next serial number.
class PrimaryFollow14 {
public:
    uint64_t wholeEveryUs = 8000;
    struct Look {
        bool whole = false, fresh = false; // the seats were read whole; and that gave a new sample (`last`)
        std::array<PrimaryFault14, 2> fault; // of a whole read: why a seat, or the place of its pair, was not read
        int read = 0;                      // seats a whole read could read
        uint32_t step = 0;                 // by how much the counter of seat 0 went on from the last sample to a new one
    };
    // (after a new binding, or when the seats are gone: the serial numbers go on)
    void reset() { last_.reset(), settle_ = false, wholeAtUs_ = 0; }
    // The samples from now on are numbered after `serial` (a follower made anew goes on where the last one was).
    void continueFrom(uint64_t serial) { serial_ = std::max(serial_, serial); }
    const std::optional<PrimarySample14>& last() const { return last_; } // the newest sample, while the seats can be read
    // clockUs(): the caller's clock in microseconds.
    template <class Clock> Look look(const PrimaryBinding14& binding, uint64_t generation, const FieldRead14& read, Clock&& clockUs) {
        Look out;
        const uint64_t began = clockUs();
        const bool complete = last_ && last_->seat[0] && last_->seat[1];
        bool moved = !complete;
        for (int seat = 0; seat < 2 && !moved; ++seat) {
            const auto tick = readPrimaryTick14(binding, seat, read);
            moved = !tick || *tick != last_->seat[size_t(seat)]->pose.count108;
        }
        if (!moved && !settle_ && began - wholeAtUs_ < wholeEveryUs) return out;
        PrimarySample14 s;
        s.process = binding.context.process, s.generation = generation;
        for (int seat = 0; seat < 2; ++seat) {
            s.seat[size_t(seat)] = readPrimarySeat14(binding, seat, read, &out.fault[size_t(seat)]);
            out.read += s.seat[size_t(seat)] ? 1 : 0;
        }
        s.observedUs = clockUs(), s.observedMs = s.observedUs / 1000;
        wholeAtUs_ = s.observedUs, settle_ = moved, out.whole = true;
        if (!out.read) {
            last_.reset();
            return out;
        }
        if (last_ && samePrimarySeat14(last_->seat[0], s.seat[0]) && samePrimarySeat14(last_->seat[1], s.seat[1])) return out;
        if (last_ && last_->seat[0] && s.seat[0]) out.step = s.seat[0]->pose.count108 - last_->seat[0]->pose.count108;
        s.serial = ++serial_, out.fresh = true;
        last_ = std::move(s);
        return out;
    }

private:
    std::optional<PrimarySample14> last_;
    uint64_t serial_ = 0, wholeAtUs_ = 0;
    bool settle_ = false;
};

// When two bound seats are to be bound anew, told from what the whole reads of them give.
//
// The game makes new objects for every round, and some of them where the old ones were. Records of the real game: of
// eight rounds, in six both seats then read as no player's objects any more; in two the player and field objects of
// seat 0 were made at their old places with its board elsewhere, so that seat 0 read as "its board is not there" and
// only seat 1 as "no player's objects" - and nothing was read for the eight seconds that are waited when the seats are
// not read for another reason, the first pair of the round gone with them. So a seat is taken as made anew when its
// objects or its board are not where they were bound for `remadeUs`, whichever of the two it is, and whatever the
// other seat reads as. A seat whose flags read otherwise than they can (PrimaryFault14::Flags: the old objects may keep
// their class marks while other bytes of them are used anew) is taken so when it is not read for them for `flagsUs`,
// since a moment of the game may show such flags too.
class PrimaryRebind14 {
public:
    uint64_t remadeUs = 150000; // a seat's objects, or its board, not where they were bound for so long: bound anew
    uint64_t flagsUs = 1000000; // a seat not read for its flags (and not read since) for so long: bound anew
    uint64_t goneUs = 8000000;  // neither seat read for so long, for whatever reason: bound anew
    // ... while the other seat is read, not again sooner than this (a seat that stays so after a new binding must not
    // have the seats scanned for over and over: nothing is followed while a scan runs)
    uint64_t againUs = 8000000;
    // After a new binding. (When the seats were last bound anew with one of them read is kept.)
    void reset() { gone_ = {}, lost_ = {}, flagged_ = {}, flaggedAt_ = {}, none_ = false; }
    // After a whole read of the two seats: why each was not read (PrimaryFault14 of the read), the clock in
    // microseconds. True: the seats are to be bound anew now.
    bool after(const std::array<PrimaryFault14, 2>& fault, uint64_t nowUs) {
        bool all = true;
        int which = 0; // (the seats not where they were bound for long, a bit for each)
        for (size_t s = 0; s < 2; ++s) {
            if (!fault[s].unread()) {
                lost_[s] = flagged_[s] = false, all = false;
                continue;
            }
            // (a seat that reads as changing now and then between two reads that find it gone stays gone; so with its flags)
            if (fault[s].gone() && !lost_[s]) lost_[s] = true, gone_[s] = nowUs;
            if (lost_[s] && nowUs - gone_[s] > remadeUs) which |= 1 << s;
            if (fault[s].code == PrimaryFault14::Flags && !flagged_[s]) flagged_[s] = true, flaggedAt_[s] = nowUs;
            if (flagged_[s] && nowUs - flaggedAt_[s] > flagsUs) which |= 1 << s;
        }
        if (!all) none_ = false;
        else if (!none_) none_ = true, noneAt_ = nowUs;
        if (none_ && nowUs - noneAt_ > goneUs) return true;
        if (!which) return false;
        if (all) return true;
        // (the same seat as the last time: not again so soon; another seat - the game made the two anew one after the
        //  other, and the first new binding still had the other seat's old objects - at once)
        if (single_ && which == singleWhich_ && nowUs - singleAt_ < againUs) return false;
        single_ = true, singleAt_ = nowUs, singleWhich_ = which;
        return true;
    }

private:
    std::array<bool, 2> lost_{};     // the seat's objects or board are not where they were bound
    std::array<uint64_t, 2> gone_{}; // ... since then
    std::array<bool, 2> flagged_{};       // the seat was not read for its flags (and has not been read since)
    std::array<uint64_t, 2> flaggedAt_{}; // ... since then
    bool none_ = false, single_ = false;
    int singleWhich_ = 0; // ... and which seats were not where they were bound then
    uint64_t noneAt_ = 0, singleAt_ = 0; // since when neither seat is read; when the seats were last bound anew with one of them read
};

} // namespace play::realgame
