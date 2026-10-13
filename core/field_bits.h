#pragma once
// One bit per cell of the 6 x 13 field (plus the 14th row), packed for SIMD.
//
// Layout: one 16-bit lane per column. Lane x (0..5) is column x counted from
// the left, bit y of a lane is row y counted from the bottom (0 = floor).
// Bits 0..11 are the twelve visible rows, bit 12 is the hidden 13th row and
// bit 13 is the 14th row. Lanes 6 and 7 are always zero.
//
// Vertical neighbours are a 16-bit shift inside each lane, horizontal
// neighbours are a 2-byte shift of the whole register. A horizontal shift can
// push bits into lane 6; callers AND the result with a field mask (whose
// lanes 6 and 7 are zero), so no explicit wall guard is needed.
#include <cstdint>
#include <bit>

// JURAIC_FORCE_PORTABLE selects the plain 64-bit implementation (used by the tests).
#if !defined(JURAIC_FORCE_PORTABLE) && (defined(_M_X64) || defined(__SSE4_1__))
#define JURAIC_SSE 1
#include <immintrin.h>
#else
#define JURAIC_SSE 0
#endif

namespace juraic {

constexpr int W = 6;        // columns
constexpr int H = 13;       // rows a piece can rest in
constexpr int CLEAR_H = 12; // rows that connect and clear

constexpr uint16_t COLUMN_12 = 0x0FFF; // visible rows of one column
constexpr uint16_t COLUMN_13 = 0x1FFF; // rows a piece can rest in

#if JURAIC_SSE

struct FieldBits {
    __m128i v;

    FieldBits() : v(_mm_setzero_si128()) {}
    explicit FieldBits(__m128i value) : v(value) {}

    static FieldBits fromColumns(const uint16_t c[W]) {
        return FieldBits(_mm_setr_epi16(short(c[0]), short(c[1]), short(c[2]),
                                        short(c[3]), short(c[4]), short(c[5]), 0, 0));
    }
    static FieldBits fill(uint16_t column) {
        const short s = short(column);
        return FieldBits(_mm_setr_epi16(s, s, s, s, s, s, 0, 0));
    }
    static FieldBits cell(int x, int y) {
        // (one bit, straight into its half of the register: no way through memory)
        const uint64_t bit = uint64_t(1) << ((x & 3) * 16 + y);
        return FieldBits(_mm_set_epi64x(int64_t(x < 4 ? 0 : bit), int64_t(x < 4 ? bit : 0)));
    }
    // The cells of column x in the rows that are the bits of `rows` (bit y = row y).
    static FieldBits columnCells(int x, uint16_t rows) {
        const uint64_t bits = uint64_t(rows) << ((x & 3) * 16);
        return FieldBits(_mm_set_epi64x(int64_t(x < 4 ? 0 : bits), int64_t(x < 4 ? bits : 0)));
    }
    void columns(uint16_t c[W]) const {
        alignas(16) uint16_t lanes[8];
        _mm_store_si128(reinterpret_cast<__m128i*>(lanes), v);
        for (int x = 0; x < W; ++x) c[x] = lanes[x];
    }
    uint16_t column(int x) const { return uint16_t(x < 4 ? low() >> (16 * x) : high() >> (16 * (x - 4))); }
    bool get(int x, int y) const { return (column(x) >> y) & 1; }
    // Bit x is set when column x holds a cell of the set.
    unsigned columnsUsed() const {
        // (two bits a lane say that it is empty; the even ones of the others are pushed together)
        unsigned used = ~unsigned(_mm_movemask_epi8(_mm_cmpeq_epi16(v, _mm_setzero_si128()))) & 0x555;
        used = (used | used >> 1) & 0x333;
        used = (used | used >> 2) & 0x0F0F;
        return (used | used >> 4) & 0x3F;
    }

    uint64_t low() const { return uint64_t(_mm_cvtsi128_si64(v)); }                      // columns 0..3
    uint64_t high() const { return uint64_t(_mm_cvtsi128_si64(_mm_srli_si128(v, 8))); }  // columns 4..5
    static FieldBits fromHalves(uint64_t low, uint64_t high) {
        return FieldBits(_mm_set_epi64x(int64_t(high), int64_t(low)));
    }

    bool empty() const { return _mm_testz_si128(v, v) != 0; }
    bool any() const { return !empty(); }
    // True when the two sets share no cell.
    bool disjoint(FieldBits o) const { return _mm_testz_si128(v, o.v) != 0; }
    // True when every cell of this set is also in o.
    bool subsetOf(FieldBits o) const { return _mm_testc_si128(o.v, v) != 0; }
    int count() const { return std::popcount(low()) + std::popcount(high()); }
    bool operator==(FieldBits o) const {
        return _mm_movemask_epi8(_mm_cmpeq_epi8(v, o.v)) == 0xFFFF;
    }

    FieldBits operator&(FieldBits o) const { return FieldBits(_mm_and_si128(v, o.v)); }
    FieldBits operator|(FieldBits o) const { return FieldBits(_mm_or_si128(v, o.v)); }
    FieldBits operator^(FieldBits o) const { return FieldBits(_mm_xor_si128(v, o.v)); }
    // this AND NOT o
    FieldBits without(FieldBits o) const { return FieldBits(_mm_andnot_si128(o.v, v)); }
    FieldBits& operator&=(FieldBits o) { v = _mm_and_si128(v, o.v); return *this; }
    FieldBits& operator|=(FieldBits o) { v = _mm_or_si128(v, o.v); return *this; }
    FieldBits& operator^=(FieldBits o) { v = _mm_xor_si128(v, o.v); return *this; }

    // Each cell moves one step; cells leaving a 16-bit lane are dropped.
    FieldBits up() const { return FieldBits(_mm_slli_epi16(v, 1)); }
    FieldBits down() const { return FieldBits(_mm_srli_epi16(v, 1)); }
    FieldBits right() const { return FieldBits(_mm_slli_si128(v, 2)); }
    FieldBits left() const { return FieldBits(_mm_srli_si128(v, 2)); }

    // Per column: multiply by a power of two, i.e. shift that column up.
    FieldBits scaled(FieldBits factor) const { return FieldBits(_mm_mullo_epi16(v, factor.v)); }

    // For every column: as many cells from the floor up as the column holds (where its cells rest after
    // gravity). All columns at once, in the register.
    FieldBits settled() const {
#if (defined(__AVX512BITALG__) || defined(JURAIC_BITALG)) && defined(__AVX512BW__) && defined(__AVX512VL__)
        // (the bits of each lane counted, and 1 shifted up by that count, less 1)
        const __m128i one = _mm_set1_epi16(1);
        return FieldBits(_mm_sub_epi16(_mm_sllv_epi16(one, _mm_popcnt_epi16(v)), one));
#else
        // The bits of each byte from a table of half bytes; the two bytes of a lane added, the sum n (0..14)
        // in both bytes of the lane; then (1 << n) - 1, its low and its high byte from two tables.
        const __m128i nibble = _mm_set1_epi8(0x0F);
        const __m128i bits = _mm_setr_epi8(0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4);
        const __m128i perByte = _mm_add_epi8(_mm_shuffle_epi8(bits, _mm_and_si128(v, nibble)),
                                             _mm_shuffle_epi8(bits, _mm_and_si128(_mm_srli_epi16(v, 4), nibble)));
        const __m128i n = _mm_add_epi8(perByte, _mm_or_si128(_mm_srli_epi16(perByte, 8), _mm_slli_epi16(perByte, 8)));
        const __m128i lowTable = _mm_setr_epi8(0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, -1, -1, -1, -1, -1, -1, -1, -1);
        const __m128i highTable = _mm_setr_epi8(0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F);
        const __m128i lowByte = _mm_set1_epi16(0x00FF);
        return FieldBits(_mm_or_si128(_mm_and_si128(_mm_shuffle_epi8(lowTable, n), lowByte),
                                      _mm_andnot_si128(lowByte, _mm_shuffle_epi8(highTable, n))));
#endif
    }
};

#else // portable fallback: two 64-bit halves (columns 0..3 and 4..5)

struct FieldBits {
    uint64_t lo = 0, hi = 0;
    static constexpr uint64_t LO_BIT0 = 0x0001000100010001ull, HI_BIT0 = 0x0000000000010001ull;

    FieldBits() = default;
    FieldBits(uint64_t l, uint64_t h) : lo(l), hi(h) {}

    static FieldBits fromColumns(const uint16_t c[W]) {
        return {uint64_t(c[0]) | uint64_t(c[1]) << 16 | uint64_t(c[2]) << 32 | uint64_t(c[3]) << 48,
                uint64_t(c[4]) | uint64_t(c[5]) << 16};
    }
    static FieldBits fill(uint16_t column) { return {LO_BIT0 * column, HI_BIT0 * column}; }
    static FieldBits cell(int x, int y) {
        uint16_t c[W]{};
        c[x] = uint16_t(1u << y);
        return fromColumns(c);
    }
    static FieldBits columnCells(int x, uint16_t rows) {
        const uint64_t bits = uint64_t(rows) << ((x & 3) * 16);
        return x < 4 ? FieldBits{bits, 0} : FieldBits{0, bits};
    }
    uint16_t column(int x) const { return uint16_t(x < 4 ? lo >> (16 * x) : hi >> (16 * (x - 4))); }
    void columns(uint16_t c[W]) const { for (int x = 0; x < W; ++x) c[x] = column(x); }
    bool get(int x, int y) const { return (column(x) >> y) & 1; }
    unsigned columnsUsed() const {
        unsigned used = 0;
        for (int x = 0; x < W; ++x) used |= unsigned(column(x) != 0) << x;
        return used;
    }

    uint64_t low() const { return lo; }
    uint64_t high() const { return hi; }
    static FieldBits fromHalves(uint64_t low, uint64_t high) { return {low, high}; }

    bool empty() const { return !(lo | hi); }
    bool any() const { return (lo | hi) != 0; }
    bool disjoint(FieldBits o) const { return !((lo & o.lo) | (hi & o.hi)); }
    bool subsetOf(FieldBits o) const { return !((lo & ~o.lo) | (hi & ~o.hi)); }
    int count() const { return std::popcount(lo) + std::popcount(hi); }
    bool operator==(FieldBits o) const { return lo == o.lo && hi == o.hi; }

    FieldBits operator&(FieldBits o) const { return {lo & o.lo, hi & o.hi}; }
    FieldBits operator|(FieldBits o) const { return {lo | o.lo, hi | o.hi}; }
    FieldBits operator^(FieldBits o) const { return {lo ^ o.lo, hi ^ o.hi}; }
    FieldBits without(FieldBits o) const { return {lo & ~o.lo, hi & ~o.hi}; }
    FieldBits& operator&=(FieldBits o) { lo &= o.lo; hi &= o.hi; return *this; }
    FieldBits& operator|=(FieldBits o) { lo |= o.lo; hi |= o.hi; return *this; }
    FieldBits& operator^=(FieldBits o) { lo ^= o.lo; hi ^= o.hi; return *this; }

    // (as in the SSE version: for every column, as many cells from the floor up as it holds)
    FieldBits settled() const {
        FieldBits r = fromHalves(0, 0);
        for (int x = 0; x < W; ++x) {
            const int shift = (x & 3) * 16;
            const uint64_t mask = ((uint64_t(1) << std::popcount(uint16_t((x < 4 ? lo : hi) >> shift))) - 1) << shift;
            r = r | (x < 4 ? fromHalves(mask, 0) : fromHalves(0, mask));
        }
        return r;
    }

    FieldBits up() const { return {(lo << 1) & ~LO_BIT0, (hi << 1) & ~HI_BIT0}; }
    FieldBits down() const { return {(lo >> 1) & ~(LO_BIT0 << 15), (hi >> 1) & ~(HI_BIT0 << 15)}; }
    // Unlike the SSE version nothing is pushed into lanes 6 and 7.
    FieldBits right() const { return {lo << 16, (hi << 16 | lo >> 48) & 0xFFFFFFFFull}; }
    FieldBits left() const { return {lo >> 16 | hi << 48, hi >> 16}; }

    FieldBits scaled(FieldBits factor) const {
        uint16_t a[W], f[W];
        columns(a);
        factor.columns(f);
        for (int x = 0; x < W; ++x) a[x] = uint16_t(a[x] * f[x]);
        return fromColumns(a);
    }
};

#endif

inline const FieldBits MASK_12 = FieldBits::fill(COLUMN_12);

// Calls f(x, y) for every cell of the set, columns left to right, rows bottom up.
template <class F> inline void forEachCell(FieldBits bits, F&& f) {
    uint16_t c[W];
    bits.columns(c);
    for (int x = 0; x < W; ++x)
        for (unsigned m = c[x]; m; m &= m - 1) f(x, std::countr_zero(m));
}

} // namespace juraic
