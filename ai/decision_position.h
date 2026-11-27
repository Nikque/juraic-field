#pragma once

#include "side_response.h"
#include <array>
#include <cstdint>
#include <istream>
#include <ostream>
#include <type_traits>
#include <utility>
#include <vector>

namespace juraic::ai::decision_position {

struct Input {
    int hold = 1;
    int held[2]{};
    bool spare = false, waitQuick = false, downAtTarget = false, uncertain = false;
};

struct Carry {
    int moves = -1;
    GameField field;
    std::array<Pair, 2> pairs{};
    Plan suffix;
};

struct HeldRival {
    int depth = 0;
    GameField field;
    Pair pair[MAX_KNOWN]{};
    int pairs = 0;
    uint64_t seed = 0;
    BuildResult built;
};

struct Context {
    uint64_t roundSeed = 0;
    Rng random{0};
    bool choosingAgain = false, setupPicked = false, namedLv38Ex = false;
    int lastChoice = -1, firstWidth = 0;
    int64_t unitNs = 0, slowNs = 0, plansNs = 0;
    int againBudget = 0;
    Input input;
    int twoWaveTurn = -1;
    Carry carry, next;
    side_response::Ready sideReady;
    HeldRival held;
};

struct Trace {
    int pairs = 0, cut = 0;
    std::vector<int> widths;
};

struct Record {
    Observation observation;
    Pair pair{};
    unsigned legal = 0;
    Context context;
    Trace trace;
    int selected = -1, kind = 0;
};

enum class Read { Record, End, Error };

namespace detail {

// Local experiment format. These value types contain scalar members and
// fixed arrays. Player and the board use the compiler's native layout,
// including the SIMD bitboards. Both builds must use the same ABI.
template<class T>
bool writeValue(std::ostream& out, const T& v) {
    static_assert(std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>);
    return bool(out.write(reinterpret_cast<const char*>(&v), sizeof v));
}
template<class T>
bool readValue(std::istream& in, T& v) {
    static_assert(std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>);
    return bool(in.read(reinterpret_cast<char*>(&v), sizeof v));
}

template<class T>
bool writeList(std::ostream& out, const std::vector<T>& list, uint32_t maximum) {
    static_assert(std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>);
    if (list.size() > maximum || !writeValue(out, uint32_t(list.size()))) return false;
    return list.empty() || bool(out.write(reinterpret_cast<const char*>(list.data()), std::streamsize(list.size() * sizeof(T))));
}
template<class T>
bool readList(std::istream& in, std::vector<T>& list, uint32_t maximum) {
    static_assert(std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>);
    uint32_t size = 0;
    if (!readValue(in, size) || size > maximum) return false;
    list.resize(size);
    return list.empty() || bool(in.read(reinterpret_cast<char*>(list.data()), std::streamsize(list.size() * sizeof(T))));
}

template<class O, class IO>
bool observationValues(O& o, IO io) {
    return io(o.rules, o.timing, o.players, o.me, o.frame, o.state, o.player, o.ownTsumo,
              o.busyLeft, o.busyChain, o.fastestRead, o.fastestReadStates, o.ownWayTarget,
              o.ownWayCheck, o.ownWayLock, o.ownWaySoft, o.ownAir, o.ownAirPair, o.ownAirSeat);
}

inline constexpr uint32_t maxPairs = 1u << 20;
inline constexpr uint32_t maxWidths = 1024;
inline constexpr std::array<uint32_t, 14> signature{
    0x4A445031u, sizeof(Rules), sizeof(Timing), sizeof(Player), sizeof(FieldBits), sizeof(PairControl),
    sizeof(Pair), sizeof(Rng), sizeof(unsigned), sizeof(Context), sizeof(Input), sizeof(Carry),
    sizeof(side_response::Ready), sizeof(HeldRival)
};

} // namespace detail

inline bool writeHeader(std::ostream& out) { return detail::writeValue(out, detail::signature); }

inline bool readHeader(std::istream& in) {
    auto signature = detail::signature;
    return detail::readValue(in, signature) && signature == detail::signature;
}

// The caller can give each worker a stream or serialize calls to this function.
inline bool write(std::ostream& out, const Record& r) {
    using namespace detail;
    const auto values = [&](const auto&... item) { return (writeValue(out, item) && ...); };
    const auto& o = r.observation;
    if (!observationValues(o, values)) return false;
    for (const auto& order : o.nextGarbageOrder)
        if (!values(uint8_t(order.has_value())) || (order && !values(*order))) return false;
    const auto& c = r.context;
    const auto& held = c.held;
    return writeList(out, o.tsumo.pairs, maxPairs) && writeList(out, o.tsumo1.pairs, maxPairs) &&
           values(r.pair, r.legal, c.roundSeed, c.random, c.choosingAgain, c.setupPicked, c.namedLv38Ex,
                  c.lastChoice, c.firstWidth, c.unitNs, c.slowNs, c.plansNs, c.againBudget,
                  c.input, c.twoWaveTurn, c.carry, c.next, c.sideReady, held.depth) &&
           (!held.depth || values(held.field, held.pair, held.pairs, held.seed, held.built)) &&
           values(r.trace.pairs, r.trace.cut, r.selected, r.kind) && writeList(out, r.trace.widths, maxWidths);
}

inline Read read(std::istream& in, Record& out) {
    using namespace detail;
    if (in.peek() == std::char_traits<char>::eof()) return in.eof() && !in.bad() ? Read::End : Read::Error;
    Record r;
    auto& o = r.observation;
    const auto values = [&](auto&... item) { return (readValue(in, item) && ...); };
    if (!observationValues(o, values) || o.players < 1 || o.players > 2 || o.me < 0 || o.me >= o.players)
        return Read::Error;
    for (auto& order : o.nextGarbageOrder) {
        uint8_t present = 0;
        if (!values(present) || present > 1 || (present && !values(order.emplace()))) return Read::Error;
    }
    auto& c = r.context;
    auto& held = c.held;
    if (!readList(in, o.tsumo.pairs, maxPairs) || !readList(in, o.tsumo1.pairs, maxPairs) ||
        !values(r.pair, r.legal, c.roundSeed, c.random, c.choosingAgain, c.setupPicked, c.namedLv38Ex,
                c.lastChoice, c.firstWidth, c.unitNs, c.slowNs, c.plansNs, c.againBudget,
                c.input, c.twoWaveTurn, c.carry, c.next, c.sideReady, held.depth) ||
        (held.depth && !values(held.field, held.pair, held.pairs, held.seed, held.built)) ||
        !values(r.trace.pairs, r.trace.cut, r.selected, r.kind) || !readList(in, r.trace.widths, maxWidths))
        return Read::Error;
    if (held.depth < 0 || held.pairs < 0 || held.pairs > MAX_KNOWN ||
        c.carry.suffix.count < 0 || c.carry.suffix.count > 2 || c.next.suffix.count < 0 || c.next.suffix.count > 2 ||
        r.legal >> PLACEMENTS || r.selected < -1 || r.selected >= PLACEMENTS)
        return Read::Error;
    out = std::move(r);
    return Read::Record;
}

} // namespace juraic::ai::decision_position
