#pragma once
// Sound effects made in code (no sound files): short waveforms synthesised at
// start-up and played through XAudio2 (elsewhere through audio streams of SDL). Music - a file of the folder bgm
// that is chosen in the settings (play/media.h) - is played through one more voice.
#ifdef _WIN32
#include <windows.h>
#include <xaudio2.h>
#else
#include <SDL3/SDL.h>
#include <algorithm>
#endif
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace play {

// The sound of a wave has a pitch of its own for every chain up to the nineteenth (the most a field of
// 6 x 13 holds with four to a group).
inline constexpr int CHAIN_SOUNDS = 19;
enum Sound { SND_MOVE, SND_ROTATE, SND_LAND, SND_GARBAGE, SND_ALL_CLEAR, SND_OVER, SND_SELECT, SND_BEAT, SND_BEAT_GO, SND_CHAIN_1, SND_COUNT = SND_CHAIN_1 + CHAIN_SOUNDS };

// The form of the samples of a piece of music (16 bits).
#ifdef _WIN32
using PcmFormat = WAVEFORMATEX;
#else
struct PcmFormat {
    uint16_t wFormatTag = 0, nChannels = 0;
    uint32_t nSamplesPerSec = 0, nAvgBytesPerSec = 0;
    uint16_t nBlockAlign = 0, wBitsPerSample = 0;
};
#endif

class Audio {
    static constexpr int RATE = 44100, VOICES = 12;
    static constexpr double PI = 3.14159265358979;
#ifdef _WIN32
    IXAudio2* engine = nullptr;
    IXAudio2MasteringVoice* master = nullptr;
    IXAudio2SourceVoice* voice[VOICES]{};
    IXAudio2SourceVoice* music = nullptr;
#else
    SDL_AudioDeviceID device = 0;
    SDL_AudioStream* voice[VOICES]{};
    SDL_AudioStream* music = nullptr;
    size_t musicAt = 0; // (the byte of the music given to the stream next)
    // More music for the stream, from where it stands, again from the beginning at the end (on the thread of the sound).
    static void moreMusic(void* self, SDL_AudioStream* stream, int wanted, int) {
        Audio& a = *static_cast<Audio*>(self);
        const std::vector<uint8_t>& p = *a.musicPcm;
        while (wanted > 0 && !p.empty()) {
            const size_t n = std::min(size_t(wanted), p.size() - a.musicAt);
            SDL_PutAudioStreamData(stream, p.data() + a.musicAt, int(n));
            a.musicAt = (a.musicAt + n) % p.size(), wanted -= int(n);
        }
    }
#endif
    int next = 0;
    std::vector<int16_t> pcm[SND_COUNT];
    std::shared_ptr<const std::vector<uint8_t>> musicPcm; // (the voice plays from it: kept as long as the voice is)

    // Adds a tone that glides from f0 to f1 Hz. shape: 0 sine, 1 soft square, 2 noise.
    static void tone(std::vector<int16_t>& out, double start, double f0, double f1, double seconds, double volume, int shape) {
        const size_t begin = size_t(start * RATE), n = size_t(seconds * RATE);
        if (out.size() < begin + n) out.resize(begin + n);
        double phase = 0;
        uint32_t noise = 0x12345678u;
        for (size_t i = 0; i < n; ++i) {
            const double t = double(i) / double(n);
            phase += 2 * PI * (f0 + (f1 - f0) * t) / RATE;
            double v = std::sin(phase);
            if (shape == 1) v = std::tanh(3 * v) * 0.6;
            if (shape == 2) {
                noise = noise * 1664525u + 1013904223u;
                v = (double(noise >> 16) / 32768.0 - 1.0) * (0.4 + 0.6 * std::sin(phase));
            }
            const double envelope = std::min(1.0, double(i) / (0.004 * RATE)) * std::exp(-4.0 * t);
            const double mixed = out[begin + i] + v * envelope * volume * 30000.0;
            out[begin + i] = int16_t(std::max(-32000.0, std::min(32000.0, mixed)));
        }
    }

    // The two notes of a chain as tone() makes them (shape 1), made at eight times the rate and brought
    // down through a low-pass (a windowed sinc, 18 kHz). For the high chains: at 44.1 kHz the overtones
    // of their notes lie above half the rate and would come back as tones that do not belong to them
    // (chain 19 is 4186 Hz and 6279 Hz; the seventh overtone of the second, 43.9 kHz, would sound at
    // 147 Hz).
    static void fineNotes(std::vector<int16_t>& out, double f) {
        constexpr int OVER = 8, HALF = 128;
        const double rate = double(RATE) * OVER;
        const double starts[2] = {0, 0.07}, pitch[2] = {f, f * 1.5}, seconds[2] = {0.12, 0.20};
        std::vector<double> wide(size_t((0.07 + 0.20) * rate) + 1, 0.0);
        for (int k = 0; k < 2; ++k) {
            const size_t begin = size_t(starts[k] * rate), n = size_t(seconds[k] * rate);
            double phase = 0;
            for (size_t i = 0; i < n && begin + i < wide.size(); ++i) {
                const double t = double(i) / double(n);
                phase += 2 * PI * pitch[k] / rate;
                const double envelope = std::min(1.0, double(i) / (0.004 * rate)) * std::exp(-4.0 * t);
                wide[begin + i] += std::tanh(3 * std::sin(phase)) * 0.6 * envelope * 0.3 * 30000.0;
            }
        }
        double kernel[2 * HALF + 1], sum = 0;
        for (int k = -HALF; k <= HALF; ++k) {
            const double x = 2 * PI * 18000.0 / rate * k, u = PI * k / (HALF + 1);
            kernel[k + HALF] = (k == 0 ? 1.0 : std::sin(x) / x) * (0.42 + 0.5 * std::cos(u) + 0.08 * std::cos(2 * u)); // (a Blackman window)
            sum += kernel[k + HALF];
        }
        out.assign(wide.size() / OVER, 0);
        for (size_t j = 0; j < out.size(); ++j) {
            double v = 0;
            for (int k = -HALF; k <= HALF; ++k) {
                const long long at = (long long)(j) * OVER + k;
                if (at >= 0 && at < (long long)(wide.size())) v += wide[size_t(at)] * kernel[k + HALF];
            }
            out[j] = int16_t(std::max(-32000.0, std::min(32000.0, v / sum)));
        }
    }

public:
#ifndef _WIN32
    // (the streams are let go of before the samples and this object are gone: the sound runs on a thread of its own)
    ~Audio() {
        stopMusic();
        for (SDL_AudioStream*& v : voice)
            if (v) SDL_DestroyAudioStream(v), v = nullptr;
        if (device) SDL_CloseAudioDevice(device), device = 0;
    }
#endif
    bool init() {
        tone(pcm[SND_MOVE], 0, 620, 500, 0.045, 0.25, 0);
        tone(pcm[SND_ROTATE], 0, 860, 1080, 0.05, 0.22, 0);
        tone(pcm[SND_LAND], 0, 190, 80, 0.10, 0.5, 0);
        tone(pcm[SND_LAND], 0, 400, 200, 0.05, 0.12, 2);
        tone(pcm[SND_GARBAGE], 0, 90, 50, 0.30, 0.6, 0);
        tone(pcm[SND_GARBAGE], 0, 300, 120, 0.25, 0.35, 2);
        const double notes[] = {523.25, 659.25, 783.99, 1046.5, 1318.5};
        for (int i = 0; i < 5; ++i) tone(pcm[SND_ALL_CLEAR], 0.08 * i, notes[i], notes[i], 0.22, 0.3, 1);
        tone(pcm[SND_OVER], 0, 420, 90, 0.7, 0.45, 1);
        tone(pcm[SND_SELECT], 0, 720, 760, 0.06, 0.25, 0);
        // The beat before a round (play/start_beat.h): A5 twice, then A6, as soft squares (the overtones carry it over the
        // music; as sines of 0.07 and 0.12 s at 0.22 they were hardly heard in a match).
        tone(pcm[SND_BEAT], 0, 880, 880, 0.16, 0.9, 1);
        tone(pcm[SND_BEAT_GO], 0, 1760, 1760, 0.28, 0.9, 1);
        // Chain n: two rising notes, one whole tone higher for every chain: C5 (523 Hz) for the first,
        // A#6 (1865 Hz) for the twelfth, C8 (4186 Hz) for the nineteenth.
        for (int n = 0; n < CHAIN_SOUNDS; ++n) {
            const double f = 440.0 * std::pow(2.0, (n * 2 + 3) / 12.0);
            if (n >= 12) {
                fineNotes(pcm[SND_CHAIN_1 + n], f);
                continue;
            }
            tone(pcm[SND_CHAIN_1 + n], 0, f, f, 0.12, 0.3, 1);
            tone(pcm[SND_CHAIN_1 + n], 0.07, f * 1.5, f * 1.5, 0.20, 0.3, 1);
        }
#ifndef _WIN32
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return false;
        device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
        if (!device) return false;
        const SDL_AudioSpec format{SDL_AUDIO_S16, 1, RATE};
        for (auto& v : voice)
            if (!(v = SDL_CreateAudioStream(&format, nullptr)) || !SDL_BindAudioStream(device, v)) return false;
        SDL_ResumeAudioDevice(device);
        return true;
#else
        if (FAILED(XAudio2Create(&engine, 0, XAUDIO2_DEFAULT_PROCESSOR))) return false;
        if (FAILED(engine->CreateMasteringVoice(&master))) return false;
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 1;
        format.nSamplesPerSec = RATE;
        format.wBitsPerSample = 16;
        format.nBlockAlign = 2;
        format.nAvgBytesPerSec = RATE * 2;
        for (auto& v : voice)
            if (FAILED(engine->CreateSourceVoice(&v, &format))) return false;
        return true;
#endif
    }
    // The volume of the sound effects (the music has its own: setMusicVolume).
    void setVolume(int percent) {
#ifdef _WIN32
        for (IXAudio2SourceVoice* v : voice)
            if (v) v->SetVolume(float(percent) / 100.0f);
#else
        for (SDL_AudioStream* v : voice)
            if (v) SDL_SetAudioStreamGain(v, float(percent) / 100.0f);
#endif
    }
    // Music: samples (play/media.h readMusic) played again and again until stopMusic or the next startMusic.
    // (the samples are shared with whoever keeps them for another start: nothing is copied)
    bool startMusic(std::shared_ptr<const std::vector<uint8_t>> samples, const PcmFormat& format, int percent) {
        stopMusic();
#ifndef _WIN32
        if (!device || !samples || samples->empty()) return false;
        musicPcm = std::move(samples), musicAt = 0;
        const SDL_AudioSpec from{SDL_AUDIO_S16, int(format.nChannels), int(format.nSamplesPerSec)};
        music = SDL_CreateAudioStream(&from, nullptr);
        if (!music) return musicPcm.reset(), false;
        SDL_SetAudioStreamGain(music, float(percent) / 100.0f);
        SDL_SetAudioStreamGetCallback(music, moreMusic, this);
        if (!SDL_BindAudioStream(device, music)) return stopMusic(), false;
        return true;
#else
        if (!engine || !samples || samples->empty()) return false;
        musicPcm = std::move(samples);
        if (FAILED(engine->CreateSourceVoice(&music, &format))) return music = nullptr, musicPcm.reset(), false;
        XAUDIO2_BUFFER buffer{};
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.AudioBytes = UINT32(musicPcm->size());
        buffer.pAudioData = musicPcm->data();
        buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
        music->SetVolume(float(percent) / 100.0f);
        return SUCCEEDED(music->SubmitSourceBuffer(&buffer)) && SUCCEEDED(music->Start());
#endif
    }
    void stopMusic() {
#ifdef _WIN32
        if (music) music->Stop(), music->FlushSourceBuffers(), music->DestroyVoice();
#else
        if (music) SDL_DestroyAudioStream(music); // (the stream is let go of by the device first: the samples are not read after this)
#endif
        music = nullptr;
        musicPcm.reset();
    }
    void setMusicVolume(int percent) {
#ifdef _WIN32
        if (music) music->SetVolume(float(percent) / 100.0f);
#else
        if (music) SDL_SetAudioStreamGain(music, float(percent) / 100.0f);
#endif
    }
    bool musicOn() const { return music != nullptr; }
    void play(int sound) {
#ifndef _WIN32
        if (!device || sound < 0 || sound >= SND_COUNT) return;
        SDL_AudioStream* v = voice[next];
        next = (next + 1) % VOICES;
        SDL_ClearAudioStream(v);
        SDL_PutAudioStreamData(v, pcm[sound].data(), int(pcm[sound].size() * 2));
#else
        if (!master || sound < 0 || sound >= SND_COUNT) return;
        IXAudio2SourceVoice* v = voice[next];
        next = (next + 1) % VOICES;
        v->Stop();
        v->FlushSourceBuffers();
        XAUDIO2_BUFFER buffer{};
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.AudioBytes = UINT32(pcm[sound].size() * 2);
        buffer.pAudioData = reinterpret_cast<const BYTE*>(pcm[sound].data());
        if (SUCCEEDED(v->SubmitSourceBuffer(&buffer))) v->Start();
#endif
    }
    void chain(int n) { play(SND_CHAIN_1 + (n < 1 ? 0 : n > CHAIN_SOUNDS ? CHAIN_SOUNDS - 1 : n - 1)); }
    // (for a check outside the game: the samples of a sound)
    const std::vector<int16_t>& samples(int sound) const { return pcm[sound]; }
};

} // namespace play
