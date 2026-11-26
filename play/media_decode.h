#pragma once
// Pictures and music read with the libraries of one file each (third_party/stb, third_party/dr_libs), the way the game
// reads them outside Windows (play/media.h). The code of the libraries is built once elsewhere (play/third_party_impl.cpp).
#include "../third_party/dr_libs/dr_flac.h"
#include "../third_party/dr_libs/dr_mp3.h"
#include "../third_party/dr_libs/dr_wav.h"
#include "../third_party/stb/stb_image.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <new>
#include <string>
#include <vector>

namespace play {

// The pixels of a picture, 8 bits for each of R, G, B and A, given to use(pixels, width, height) while they are held.
// A picture of 16 bits a channel is read as such and cut to 8 bits here (stb_image's own cut keeps the pixels it read
// when the memory for the cut ones cannot be had). False when the picture cannot be read.
template <class Use> bool decodePicture(const std::string& name, Use&& use) {
    struct Held {
        void* p;
        ~Held() { stbi_image_free(p); }
    };
    int width = 0, height = 0, n = 0;
    if (stbi_is_16_bit(name.c_str())) {
        const Held wide{stbi_load_16(name.c_str(), &width, &height, &n, 4)};
        if (!wide.p) return false;
        std::vector<uint8_t> cut;
        try {
            cut.resize(size_t(width) * size_t(height) * 4);
        } catch (const std::bad_alloc&) {
            return false;
        }
        const auto* from = static_cast<const stbi_us*>(wide.p);
        for (size_t i = 0; i < cut.size(); ++i) cut[i] = uint8_t(from[i] >> 8);
        use(static_cast<const uint8_t*>(cut.data()), width, height);
        return true;
    }
    const Held held{stbi_load(name.c_str(), &width, &height, &n, 4)};
    if (!held.p) return false;
    use(static_cast<const uint8_t*>(held.p), width, height);
    return true;
}

// A FLAC file given to dr_flac from where "fLaC" begins, the ID3 tags before it left out: dr_flac skips such tags
// itself, but then reads the metadata blocks after the first one at the place they would have without the tags.
struct FlacInFile {
    std::FILE* file = nullptr;
    long long start = 0;

    static long long at(std::FILE* f) {
#ifdef _WIN32
        return _ftelli64(f);
#else
        return ftello(f);
#endif
    }
    static bool go(std::FILE* f, long long offset, int origin) {
#ifdef _WIN32
        return _fseeki64(f, offset, origin) == 0;
#else
        return fseeko(f, off_t(offset), origin) == 0;
#endif
    }
    // Opens the file and finds where "fLaC" begins (after any ID3v2 tags: a header of ten bytes, the size of what
    // follows in four bytes of seven bits each, ten bytes more for a footer).
    bool open(const std::string& name) {
        file = std::fopen(name.c_str(), "rb");
        if (!file) return false;
        for (;;) {
            unsigned char head[10];
            if (std::fread(head, 1, 10, file) != 10 || head[0] != 'I' || head[1] != 'D' || head[2] != '3') break;
            long long size = 0;
            for (int i = 6; i < 10; ++i) size = (size << 7) | (head[i] & 0x7F);
            size += 10 + (head[5] & 0x10 ? 10 : 0);
            start += size;
            if (!go(file, start, SEEK_SET)) break;
        }
        return go(file, start, SEEK_SET);
    }
    ~FlacInFile() {
        if (file) std::fclose(file);
    }
    static size_t read(void* me, void* out, size_t bytes) { return std::fread(out, 1, bytes, static_cast<FlacInFile*>(me)->file); }
    static drflac_bool32 seek(void* me, int offset, drflac_seek_origin origin) {
        const FlacInFile& f = *static_cast<FlacInFile*>(me);
        if (origin == DRFLAC_SEEK_SET) return go(f.file, f.start + offset, SEEK_SET);
        return go(f.file, offset, origin == DRFLAC_SEEK_CUR ? SEEK_CUR : SEEK_END);
    }
    static drflac_bool32 tell(void* me, drflac_int64* cursor) {
        const FlacInFile& f = *static_cast<FlacInFile*>(me);
        const long long here = at(f.file);
        if (here < f.start) return DRFLAC_FALSE;
        *cursor = here - f.start;
        return DRFLAC_TRUE;
    }
};

// The samples of a piece of music, 16 bits, the channels one after another, into `pcm`: at most `limit` bytes (whole
// frames), read a part at a time - neither the whole piece nor more than the limit is held at once. By the ending of
// the name: .flac, .wav or .mp3. False when the file cannot be read.
inline bool decodeMusic(const std::filesystem::path& file, size_t limit, std::vector<uint8_t>& pcm, unsigned& channels, unsigned& rate) {
    pcm.clear();
    channels = rate = 0;
    std::wstring ending = file.extension().wstring();
    for (wchar_t& c : ending) c = wchar_t(std::towlower(c));
    const std::string name = file.string();
    constexpr size_t PART = 4096; // frames read at a time
    std::vector<int16_t> part;
    // read(frames, into): the frames read; total: the frames of the piece (0: not known)
    const auto take = [&](uint64_t total, auto&& read) {
        if (!channels || !rate) return false;
        const size_t frameBytes = size_t(channels) * 2, most = limit / frameBytes * frameBytes;
        if (total) pcm.reserve(size_t(std::min<uint64_t>(total * frameBytes, most)));
        part.resize(PART * channels);
        while (pcm.size() < most) {
            const size_t want = std::min(PART, (most - pcm.size()) / frameBytes);
            const size_t got = size_t(read(want, part.data()));
            if (!got) break;
            const auto* bytes = reinterpret_cast<const uint8_t*>(part.data());
            pcm.insert(pcm.end(), bytes, bytes + got * frameBytes);
            if (got < want) break;
        }
        return true;
    };
    if (ending == L".flac") {
        FlacInFile in;
        if (!in.open(name)) return false;
        drflac* flac = drflac_open(&FlacInFile::read, &FlacInFile::seek, &FlacInFile::tell, &in, nullptr);
        if (!flac) return false;
        channels = flac->channels, rate = flac->sampleRate;
        const bool ok = take(flac->totalPCMFrameCount, [&](size_t n, int16_t* into) { return drflac_read_pcm_frames_s16(flac, n, into); });
        drflac_close(flac);
        return ok;
    }
    if (ending == L".wav") {
        drwav wav;
        if (!drwav_init_file(&wav, name.c_str(), nullptr)) return false;
        channels = wav.channels, rate = wav.sampleRate;
        const bool ok = take(wav.totalPCMFrameCount, [&](size_t n, int16_t* into) { return drwav_read_pcm_frames_s16(&wav, n, into); });
        drwav_uninit(&wav);
        return ok;
    }
    if (ending == L".mp3") {
        drmp3 mp3;
        if (!drmp3_init_file(&mp3, name.c_str(), nullptr)) return false;
        channels = mp3.channels, rate = mp3.sampleRate;
        const bool ok = take(0, [&](size_t n, int16_t* into) { return drmp3_read_pcm_frames_s16(&mp3, n, into); });
        drmp3_uninit(&mp3);
        return ok;
    }
    return false;
}

} // namespace play
