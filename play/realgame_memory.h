#pragma once
// What the mode in which Juraic plays 2P of the real game reads of the real game's memory (only in a build with
// JURAIC_REALGAME_MEMORY=1; the reading itself is the Windows part, play/realgame_memory_win.h): the regions of the
// address space of its process, which of them hold data, and the sections of its program image as the header of the
// image tells them. Pages that can be run as code are never data: what they hold is not read.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace play::realgame {

// The text of these parts: a program holds it only when they are built in (tests/realgame_memory_tests.cpp).
inline constexpr const char MEMORY_PARTS_MARK[] = "the reading of the real game's memory";

// A region of the address space of a process as VirtualQueryEx tells it, its values as there.
struct MemoryRegion {
    uint64_t base = 0, size = 0, allocationBase = 0;
    uint32_t state = 0, protect = 0, type = 0;
};
// The values of those fields (Windows' own: MEM_COMMIT, MEM_PRIVATE, PAGE_READWRITE and the rest).
inline constexpr uint32_t REGION_COMMITTED = 0x1000, REGION_RESERVED = 0x2000;
inline constexpr uint32_t REGION_PRIVATE = 0x20000, REGION_MAPPED = 0x40000, REGION_IMAGE = 0x1000000;
inline constexpr uint32_t PAGE_ACCESS = 0xff, PAGE_READ_WRITE = 0x04, PAGE_WRITE_COPY = 0x08, PAGE_RUN = 0xf0;
inline constexpr uint32_t PAGE_GUARDED = 0x100, PAGE_UNCACHED = 0x200, PAGE_WRITE_COMBINED = 0x400;

// Pages that can be run as code.
inline bool runnable(const MemoryRegion& r) { return (r.protect & PAGE_RUN) != 0; }
// Committed pages of data, which the process writes and reads as it runs: never pages that can be run, never guard
// pages, never memory shared with a device (uncached or write-combined).
inline bool dataRegion(const MemoryRegion& r) {
    const uint32_t access = r.protect & PAGE_ACCESS;
    return r.state == REGION_COMMITTED && (access == PAGE_READ_WRITE || access == PAGE_WRITE_COPY) &&
           !(r.protect & (PAGE_GUARDED | PAGE_UNCACHED | PAGE_WRITE_COMBINED));
}

// A section of a program image as the header of the image tells it.
struct ImageSection {
    std::string name;
    uint32_t address = 0, size = 0, flags = 0; // from the start of the image; its bytes in memory; its characteristics
    bool code() const { return (flags & (0x20u | 0x20000000u)) != 0; } // holds code, or can be run
    bool writable() const { return (flags & 0x80000000u) != 0; }
};
// The header of a program image of 64 bits, read from the first bytes of the image.
struct ImageHeader {
    bool ok = false;
    uint16_t machine = 0;
    uint32_t timeStamp = 0, imageSize = 0;
    std::vector<ImageSection> sections;
};
// The header in `bytes` (`size` of them, from the start of the image); ok false when they hold none of 64 bits.
inline ImageHeader readImageHeader(const uint8_t* bytes, size_t size) {
    ImageHeader h;
    const auto u16 = [&](size_t at) -> uint32_t { return at + 2 <= size ? uint32_t(bytes[at]) | uint32_t(bytes[at + 1]) << 8 : 0; };
    const auto u32 = [&](size_t at) -> uint32_t { return at + 4 <= size ? u16(at) | u16(at + 2) << 16 : 0; };
    if (size < 0x40 || u16(0) != 0x5a4d) return h; // "MZ"
    const size_t nt = u32(0x3c);
    if (u32(nt) != 0x4550 || u16(nt + 24) != 0x20b) return h; // "PE\0\0", the optional header of 64 bits
    h.machine = uint16_t(u16(nt + 4));
    const uint32_t count = u16(nt + 6);
    h.timeStamp = u32(nt + 8);
    h.imageSize = u32(nt + 24 + 56);
    const size_t table = nt + 24 + u16(nt + 20);
    if (table + size_t(count) * 40 > size) return h;
    for (uint32_t i = 0; i < count; ++i) {
        const size_t s = table + size_t(i) * 40;
        ImageSection section;
        const char* name = reinterpret_cast<const char*>(bytes + s);
        section.name.assign(name, strnlen(name, 8));
        section.size = u32(s + 8), section.address = u32(s + 12), section.flags = u32(s + 36);
        h.sections.push_back(section);
    }
    h.ok = true;
    return h;
}

} // namespace play::realgame
