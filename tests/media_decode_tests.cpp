// Tests of the pictures and the music the game reads outside Windows (play/media_decode.h), the libraries built here
// with their memory counted:
//   - music longer than the limit: no more than the limit is kept, and the whole piece is never held at once
//   - a FLAC file with an ID3 tag before it: the same samples as without the tag
//   - a picture of 16 bits a channel when the memory for its 8-bit pixels cannot be had: nothing is kept
// Built with OLD_WAY, the same cases are read the way play/media.h read them before (to see them fail).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// The memory the libraries take: what is held now, the most held at once; an allocation of failSize bytes fails.
static size_t live = 0, peak = 0, failSize = 0;
static void* countedMalloc(size_t n) {
    if (failSize && n == failSize) return nullptr;
    auto* p = static_cast<unsigned char*>(std::malloc(n + 16));
    if (!p) return nullptr;
    std::memcpy(p, &n, sizeof n);
    live += n, peak = std::max(peak, live);
    return p + 16;
}
static void countedFree(void* q) {
    if (!q) return;
    auto* p = static_cast<unsigned char*>(q) - 16;
    size_t n;
    std::memcpy(&n, p, sizeof n);
    live -= n;
    std::free(p);
}
static void* countedRealloc(void* q, size_t n) {
    if (!q) return countedMalloc(n);
    if (failSize && n == failSize) return nullptr;
    auto* p = static_cast<unsigned char*>(q) - 16;
    size_t old;
    std::memcpy(&old, p, sizeof old);
    auto* moved = static_cast<unsigned char*>(std::realloc(p, n + 16));
    if (!moved) return nullptr;
    std::memcpy(moved, &n, sizeof n);
    live = live - old + n, peak = std::max(peak, live);
    return moved + 16;
}
#define STBI_MALLOC(sz) countedMalloc(sz)
#define STBI_REALLOC(p, sz) countedRealloc(p, sz)
#define STBI_FREE(p) countedFree(p)
#define DRFLAC_MALLOC(sz) countedMalloc(sz)
#define DRFLAC_REALLOC(p, sz) countedRealloc(p, sz)
#define DRFLAC_FREE(p) countedFree(p)
#define DRWAV_MALLOC(sz) countedMalloc(sz)
#define DRWAV_REALLOC(p, sz) countedRealloc(p, sz)
#define DRWAV_FREE(p) countedFree(p)
#define DRMP3_MALLOC(sz) countedMalloc(sz)
#define DRMP3_REALLOC(p, sz) countedRealloc(p, sz)
#define DRMP3_FREE(p) countedFree(p)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../third_party/stb/stb_image.h"
#define DR_FLAC_IMPLEMENTATION
#include "../third_party/dr_libs/dr_flac.h"
#define DR_MP3_IMPLEMENTATION
#include "../third_party/dr_libs/dr_mp3.h"
#define DR_WAV_IMPLEMENTATION
#include "../third_party/dr_libs/dr_wav.h"
#undef STB_IMAGE_IMPLEMENTATION
#undef DR_FLAC_IMPLEMENTATION
#undef DR_MP3_IMPLEMENTATION
#undef DR_WAV_IMPLEMENTATION
#ifndef OLD_WAY
#include "../play/media_decode.h"
#endif

static void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

// The music of `file`, at most `limit` bytes, as the game reads it.
static bool readMusic(const std::filesystem::path& file, size_t limit, std::vector<uint8_t>& pcm, unsigned& channels, unsigned& rate) {
#ifdef OLD_WAY
    // (play/media.h before: the whole piece decoded, then the limit)
    const std::string name = file.string();
    uint64_t frames = 0;
    int16_t* samples = nullptr;
    const std::string ending = file.extension().string();
    if (ending == ".flac") samples = drflac_open_file_and_read_pcm_frames_s16(name.c_str(), &channels, &rate, reinterpret_cast<drflac_uint64*>(&frames), nullptr);
    else samples = drwav_open_file_and_read_pcm_frames_s16(name.c_str(), &channels, &rate, reinterpret_cast<drwav_uint64*>(&frames), nullptr);
    pcm.clear();
    if (samples && channels && rate) {
        const size_t bytes = std::min(size_t(frames) * channels * 2, limit / (channels * 2) * (channels * 2));
        pcm.assign(reinterpret_cast<const uint8_t*>(samples), reinterpret_cast<const uint8_t*>(samples) + bytes);
    }
    if (samples) ending == ".flac" ? drflac_free(samples, nullptr) : drwav_free(samples, nullptr);
    return samples != nullptr;
#else
    return play::decodeMusic(file, limit, pcm, channels, rate);
#endif
}
// The pixels of a picture as the game reads them (RGBA, 8 bits).
static bool readPicture(const std::string& name, std::vector<uint8_t>& rgba, int& w, int& h) {
#ifdef OLD_WAY
    int n = 0;
    unsigned char* p = stbi_load(name.c_str(), &w, &h, &n, 4);
    if (!p) return false;
    rgba.assign(p, p + size_t(w) * size_t(h) * 4);
    stbi_image_free(p);
    return true;
#else
    return play::decodePicture(name, [&](const uint8_t* p, int width, int height) {
        w = width, h = height;
        rgba.assign(p, p + size_t(w) * size_t(h) * 4);
    });
#endif
}

static void put16(std::string& s, uint32_t v) { s += char(v & 255), s += char(v >> 8 & 255); }
static void put32(std::string& s, uint32_t v) { put16(s, v & 0xFFFF), put16(s, v >> 16); }
static void put32be(std::string& s, uint32_t v) { s += char(v >> 24), s += char(v >> 16 & 255), s += char(v >> 8 & 255), s += char(v & 255); }

// A WAV of `frames` frames of 16-bit stereo at 44.1 kHz, sample k of channel c being k * 3 + c.
static void writeWav(const std::filesystem::path& file, uint32_t frames) {
    std::string s = "RIFF";
    put32(s, 36 + frames * 4), s += "WAVEfmt ", put32(s, 16), put16(s, 1), put16(s, 2), put32(s, 44100), put32(s, 44100 * 4), put16(s, 4), put16(s, 16);
    s += "data", put32(s, frames * 4);
    for (uint32_t k = 0; k < frames; ++k) put16(s, uint16_t(k * 3)), put16(s, uint16_t(k * 3 + 1));
    std::ofstream(file, std::ios::binary) << s;
}

static uint32_t crc(const std::string& s) {
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : s) {
        c ^= b;
        for (int i = 0; i < 8; ++i) c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}
// A PNG of w x h pixels of 16 bits for each of R, G, B and A (stored without compression): channel c of the pixel
// (x, y) has x * 7 + y * 3 + c in its top byte.
static void writePng16(const std::filesystem::path& file, int w, int h) {
    std::string raw;
    for (int y = 0; y < h; ++y) {
        raw += char(0);
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 4; ++c) raw += char((x * 7 + y * 3 + c) & 255), raw += char(0x55);
    }
    std::string z = "\x78\x01";
    for (size_t at = 0; at < raw.size();) {
        const size_t n = std::min<size_t>(65535, raw.size() - at);
        z += char(at + n == raw.size() ? 1 : 0), put16(z, uint16_t(n)), put16(z, uint16_t(~n));
        z.append(raw, at, n), at += n;
    }
    uint32_t a = 1, b = 0;
    for (unsigned char ch : raw) a = (a + ch) % 65521, b = (b + a) % 65521;
    put32be(z, b << 16 | a);
    const auto chunk = [](const std::string& type, const std::string& data) {
        std::string s;
        put32be(s, uint32_t(data.size()));
        s += type + data;
        put32be(s, crc(type + data));
        return s;
    };
    std::string head;
    put32be(head, uint32_t(w)), put32be(head, uint32_t(h));
    head += char(16), head += char(6), head += char(0), head += char(0), head += char(0);
    std::ofstream(file, std::ios::binary) << std::string("\x89PNG\r\n\x1a\n", 8) + chunk("IHDR", head) + chunk("IDAT", z) + chunk("IEND", "");
}

int main() {
    try {
        const std::filesystem::path folder = std::filesystem::absolute("media-decode-tests");
        std::filesystem::create_directories(folder);
        int failed = 0;
        const auto run = [&](const char* name, auto&& test) {
            try {
                test();
                std::cout << name << ": passed\n";
            } catch (const std::exception& e) {
                std::cout << "FAILED: " << name << ": " << e.what() << '\n';
                ++failed;
            }
        };

        run("music longer than the limit", [&] {
            const std::filesystem::path wav = folder / "long.wav";
            writeWav(wav, 1u << 20); // (4 MiB of samples)
            const size_t limit = 1u << 18;
            std::vector<uint8_t> pcm;
            unsigned channels = 0, rate = 0;
            live = peak = 0;
            check(readMusic(wav, limit, pcm, channels, rate) && channels == 2 && rate == 44100, "the piece was not read");
            check(pcm.size() == limit, std::to_string(pcm.size()) + " bytes kept of a limit of " + std::to_string(limit));
            int16_t last;
            std::memcpy(&last, pcm.data() + pcm.size() - 2, 2);
            check(uint16_t(last) == uint16_t((limit / 4 - 1) * 3 + 1), "the samples kept are not the first ones");
            check(peak < limit, "the libraries held " + std::to_string(peak) + " bytes at once for a limit of " + std::to_string(limit));
            check(live == 0, std::to_string(live) + " bytes of the libraries kept");
        });

#ifdef JURAIC_MEDIA_DIR
        run("FLAC with an ID3 tag before it", [&] {
            const std::filesystem::path plain = std::filesystem::path(JURAIC_MEDIA_DIR) / "bgm" / "menu" / "synth-menu.flac", tagged = folder / "tagged.flac";
            {
                std::ifstream in(plain, std::ios::binary);
                const std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                check(body.size() > 42 && body.compare(0, 4, "fLaC") == 0 && !(uint8_t(body[4]) & 0x80), "the FLAC has no block after its first");
                std::string tag = "ID3";
                tag += char(4), tag += char(0), tag += char(0), tag += char(0), tag += char(0), tag += char(1000 >> 7), tag += char(1000 & 127);
                tag += std::string(1000, '\0');
                std::ofstream(tagged, std::ios::binary) << tag + body;
            }
            std::vector<uint8_t> a, b;
            unsigned ca = 0, ra = 0, cb = 0, rb = 0;
            const size_t limit = 1u << 20;
            check(readMusic(plain, limit, a, ca, ra) && !a.empty(), "the FLAC without a tag was not read");
            const bool read = readMusic(tagged, limit, b, cb, rb);
            check(read && ca == cb && ra == rb && a == b, std::string("the FLAC with a tag ") + (read ? "gives other samples" : "was not read"));
            check(live == 0, std::to_string(live) + " bytes of the libraries kept");
        });
#endif

        run("16-bit picture, no memory for its 8-bit pixels", [&] {
            const std::filesystem::path png = folder / "wide.png";
            const int w = 20, h = 12;
            writePng16(png, w, h);
            std::vector<uint8_t> rgba;
            int pw = 0, ph = 0;
            live = 0;
            failSize = size_t(w) * h * 4; // (what stb_image takes to cut the pixels to 8 bits)
            const bool read = readPicture(png.string(), rgba, pw, ph);
            failSize = 0;
            check(live == 0, std::to_string(live) + " bytes of the picture kept");
            check(read && pw == w && ph == h && rgba.size() == size_t(w) * h * 4 && rgba[4 * (3 * w + 5) + 2] == uint8_t(5 * 7 + 3 * 3 + 2),
                  "the 16-bit picture was not read as it is");
        });

        if (failed) return 1;
        std::cout << "media decode tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
