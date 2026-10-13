#pragma once
// The pictures behind the game and the music: files of the folders "background" and "bgm" next to
// the executable, chosen among on the settings screen (play/settings.h keeps the names). The
// package comes with three pictures and nine pieces of music (media/ in the sources, copied there
// by the build); more can be put beside them.
//   background: .png .jpg .jpeg .bmp .webp   (read with the Windows Imaging Component; drawn so
//               that it covers its place - a field, or the whole canvas -, its middle kept, and
//               darkened by the setting)
//   bgm:        .mp3 .wav .m4a .aac .wma .flac   (read with Media Foundation; the whole file is
//               turned into samples once, on a thread of its own, and then played again and
//               again)
// Windows reads .webp only where its extension for it is installed (it comes with Windows 11);
// a file that cannot be read is said so on the screen. Not .ogg and .opus: Media Foundation
// does not read them in a program like this one, with the Web Media Extensions installed
// either (MF_E_UNSUPPORTED_BYTESTREAM_TYPE).
#include <d2d1.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wincodec.h>
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>
#include <vector>

namespace play {

inline const wchar_t* const PICTURE_ENDINGS[] = {L".png", L".jpg", L".jpeg", L".bmp", L".webp"};
inline const wchar_t* const MUSIC_ENDINGS[] = {L".mp3", L".wav", L".m4a", L".aac", L".wma", L".flac"};

// The names of the files of a folder that end in one of `endings`, in the order of their names.
template <size_t N> std::vector<std::wstring> filesOf(const std::filesystem::path& folder, const wchar_t* const (&endings)[N]) {
    std::vector<std::wstring> names;
    std::error_code ignored;
    for (std::filesystem::directory_iterator it(folder, ignored), end; !ignored && it != end; it.increment(ignored)) {
        if (!it->is_regular_file(ignored)) continue;
        std::wstring ending = it->path().extension().wstring();
        for (wchar_t& c : ending) c = wchar_t(std::towlower(c));
        for (const wchar_t* e : endings)
            if (ending == e) names.push_back(it->path().filename().wstring());
    }
    std::sort(names.begin(), names.end());
    return names;
}

// A picture behind the game: one bitmap of the render target, read when it is first drawn.
class Backdrop {
    ID2D1Bitmap* bitmap = nullptr;
    std::filesystem::path shown, failed; // the file the bitmap was made of; a file that could not be read (not tried again)

    bool load(ID2D1RenderTarget* rt, const std::filesystem::path& file) {
        IWICImagingFactory* wic = nullptr;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        IWICFormatConverter* converter = nullptr;
        bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
                  SUCCEEDED(wic->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
                  SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(wic->CreateFormatConverter(&converter)) &&
                  SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeMedianCut)) &&
                  SUCCEEDED(rt->CreateBitmapFromWicBitmap(converter, nullptr, &bitmap));
        if (converter) converter->Release();
        if (frame) frame->Release();
        if (decoder) decoder->Release();
        if (wic) wic->Release();
        return ok;
    }

public:
    // The bitmap belongs to a render target: called whenever that is another.
    void release() {
        if (bitmap) bitmap->Release();
        bitmap = nullptr;
        shown.clear(), failed.clear();
    }
    // Draws the picture of `file` over `area`, darkened by `dim` percent.
    // False: there is no such picture (nothing is drawn).
    bool draw(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, const std::filesystem::path& file, const D2D1_RECT_F& area, int dim) {
        if (file != shown) {
            if (file == failed) return false;
            if (bitmap) bitmap->Release();
            bitmap = nullptr;
            shown.clear();
            if (!load(rt, file)) {
                failed = file;
                return false;
            }
            shown = file;
        }
        const D2D1_SIZE_F size = bitmap->GetSize();
        const float width = area.right - area.left, height = area.bottom - area.top;
        if (size.width <= 0 || size.height <= 0 || width <= 0 || height <= 0) return false;
        // (the part of the picture that has the shape of the area, from its middle)
        const float scale = std::max(width / size.width, height / size.height), w = width / scale, h = height / scale;
        const D2D1_RECT_F from = D2D1::RectF((size.width - w) / 2, (size.height - h) / 2, (size.width + w) / 2, (size.height + h) / 2);
        rt->DrawBitmap(bitmap, area, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, from);
        if (dim > 0) {
            brush->SetColor(D2D1::ColorF(0, 0, 0, float(dim) / 100.f));
            rt->FillRectangle(area, brush);
        }
        return true;
    }
};

// A piece of music as samples (16 bits), read from a file. Media Foundation must be started
// (MFStartup) and COM initialised on the calling thread. Empty when the file cannot be read.
struct MusicSamples {
    std::vector<uint8_t> pcm;
    WAVEFORMATEX format{};
};
inline MusicSamples readMusic(const std::filesystem::path& file) {
    MusicSamples out;
    IMFSourceReader* reader = nullptr;
    if (FAILED(MFCreateSourceReaderFromURL(file.c_str(), nullptr, &reader))) return out;
    const DWORD stream = DWORD(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    IMFMediaType* wanted = nullptr;
    IMFMediaType* given = nullptr;
    HRESULT hr = MFCreateMediaType(&wanted);
    if (SUCCEEDED(hr)) hr = wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    if (SUCCEEDED(hr)) hr = wanted->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    if (SUCCEEDED(hr)) hr = wanted->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(stream, nullptr, wanted);
    if (SUCCEEDED(hr)) hr = reader->GetCurrentMediaType(stream, &given);
    UINT32 channels = 0, rate = 0, bits = 0;
    if (SUCCEEDED(hr)) {
        given->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
        given->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
        given->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
    }
    if (wanted) wanted->Release();
    if (given) given->Release();
    const size_t limit = size_t(600) << 20; // (about an hour of stereo at 44.1 kHz: more is not read)
    while (SUCCEEDED(hr) && channels && rate && bits == 16 && out.pcm.size() < limit) {
        DWORD flags = 0;
        IMFSample* sample = nullptr;
        hr = reader->ReadSample(stream, 0, nullptr, &flags, nullptr, &sample);
        if (SUCCEEDED(hr) && sample) {
            IMFMediaBuffer* buffer = nullptr;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))) {
                BYTE* data = nullptr;
                DWORD size = 0;
                if (SUCCEEDED(buffer->Lock(&data, nullptr, &size))) {
                    out.pcm.insert(out.pcm.end(), data, data + size);
                    buffer->Unlock();
                }
                buffer->Release();
            }
        }
        if (sample) sample->Release();
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
    }
    reader->Release();
    if (FAILED(hr) || !channels || !rate || bits != 16) out.pcm.clear();
    out.format.wFormatTag = WAVE_FORMAT_PCM;
    out.format.nChannels = WORD(channels);
    out.format.nSamplesPerSec = rate;
    out.format.wBitsPerSample = 16;
    out.format.nBlockAlign = WORD(channels * 2);
    out.format.nAvgBytesPerSec = rate * channels * 2;
    out.pcm.resize(out.pcm.size() / (out.format.nBlockAlign ? out.format.nBlockAlign : 1) * (out.format.nBlockAlign ? out.format.nBlockAlign : 1));
    return out;
}

} // namespace play
