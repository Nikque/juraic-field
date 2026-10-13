#pragma once
// Sound effects made in code (no sound files): short waveforms synthesised at
// start-up and played through XAudio2. Music - a file of the folder bgm that is
// chosen in the settings (play/media.h) - is played through one more voice.
#include <windows.h>
#include <xaudio2.h>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace play {

// The sound of a wave has a pitch of its own for every chain up to the nineteenth (the most a field of
// 6 x 13 holds with four to a group).
inline constexpr int CHAIN_SOUNDS = 19;
enum Sound { SND_MOVE, SND_ROTATE, SND_LAND, SND_GARBAGE, SND_ALL_CLEAR, SND_OVER, SND_SELECT, SND_CHAIN_1, SND_COUNT = SND_CHAIN_1 + CHAIN_SOUNDS };

class Audio {
    static constexpr int RATE = 44100, VOICES = 12;
    static constexpr double PI = 3.14159265358979;
    IXAudio2* engine = nullptr;
    IXAudio2MasteringVoice* master = nullptr;
    IXAudio2SourceVoice* voice[VOICES]{};
    int next = 0;
    std::vector<int16_t> pcm[SND_COUNT];
    IXAudio2SourceVoice* music = nullptr;
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
    }
    // The volume of the sound effects (the music has its own: setMusicVolume).
    void setVolume(int percent) {
        for (IXAudio2SourceVoice* v : voice)
            if (v) v->SetVolume(float(percent) / 100.0f);
    }
    // Music: samples (play/media.h readMusic) played again and again until stopMusic or the next startMusic.
    // (the samples are shared with whoever keeps them for another start: nothing is copied)
    bool startMusic(std::shared_ptr<const std::vector<uint8_t>> samples, const WAVEFORMATEX& format, int percent) {
        stopMusic();
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
    }
    void stopMusic() {
        if (music) music->Stop(), music->FlushSourceBuffers(), music->DestroyVoice();
        music = nullptr;
        musicPcm.reset();
    }
    void setMusicVolume(int percent) {
        if (music) music->SetVolume(float(percent) / 100.0f);
    }
    bool musicOn() const { return music != nullptr; }
    void play(int sound) {
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
    }
    void chain(int n) { play(SND_CHAIN_1 + (n < 1 ? 0 : n > CHAIN_SOUNDS ? CHAIN_SOUNDS - 1 : n - 1)); }
    // (for a check outside the game: the samples of a sound)
    const std::vector<int16_t>& samples(int sound) const { return pcm[sound]; }
};

} // namespace play
