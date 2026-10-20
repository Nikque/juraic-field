#pragma once
// Pieces of the text the arena speaks and keeps: words of a line, buttons as hex digits (two a frame) and
// as runs ("3x0 2x5": three frames of no button, two of BTN_DOWN | BTN_LEFT).
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace arena {

// The words of a line, split at spaces; a CR at the end is dropped.
inline std::vector<std::string> words(const std::string& line) {
    std::vector<std::string> out;
    std::string w;
    for (char c : line) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!w.empty()) out.push_back(w), w.clear();
        } else {
            w += c;
        }
    }
    if (!w.empty()) out.push_back(w);
    return out;
}

inline long long toNumber(const std::string& s, long long fallback = -1) {
    if (s.empty()) return fallback;
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    return end && *end == 0 ? v : fallback;
}
inline uint64_t toUnsigned(const std::string& s) { return std::strtoull(s.c_str(), nullptr, 10); }

inline std::string toHex(const std::vector<uint8_t>& b, size_t from = 0, size_t count = SIZE_MAX) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (size_t i = from; i < b.size() && i - from < count; ++i) s += digits[b[i] >> 4], s += digits[b[i] & 15];
    return s;
}
inline int hexDigit(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
// Empty (and ok false) when the text is not hex digits in pairs.
inline std::vector<uint8_t> fromHex(const std::string& s, bool* ok = nullptr) {
    std::vector<uint8_t> b;
    bool good = s.size() % 2 == 0;
    if (good) b.reserve(s.size() / 2);
    for (size_t i = 0; good && i + 1 < s.size(); i += 2) {
        const int hi = hexDigit(s[i]), lo = hexDigit(s[i + 1]);
        good = hi >= 0 && lo >= 0;
        if (good) b.push_back(uint8_t(hi * 16 + lo));
    }
    if (ok) *ok = good;
    return good ? b : std::vector<uint8_t>{};
}

inline std::string toRuns(const std::vector<uint8_t>& b) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < b.size();) {
        size_t j = i;
        while (j < b.size() && b[j] == b[i]) ++j;
        if (!s.empty()) s += ' ';
        s += std::to_string(j - i) + 'x';
        if (b[i] >= 16) s += digits[b[i] >> 4];
        s += digits[b[i] & 15];
        i = j;
    }
    return s;
}
inline std::vector<uint8_t> fromRuns(const std::string& s) {
    std::vector<uint8_t> b;
    for (const std::string& w : words(s)) {
        const size_t x = w.find('x');
        if (x == std::string::npos) continue;
        const long long n = toNumber(w.substr(0, x), 0);
        const unsigned long v = std::strtoul(w.substr(x + 1).c_str(), nullptr, 16);
        for (long long i = 0; i < n && n < 100000000; ++i) b.push_back(uint8_t(v));
    }
    return b;
}

} // namespace arena
