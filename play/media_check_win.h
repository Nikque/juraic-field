#pragma once
#include "media.h"
#include "settings.h"
#include <fstream>
#include <wrl/client.h>

namespace play {

// Run the same artwork and music decoders as the game without opening a window or an audio device.
template <typename App>
int checkPackageMedia(const App& app, const std::filesystem::path& report) {
    using Microsoft::WRL::ComPtr;
    std::ofstream log(report, std::ios::binary);
    if (!log) return 2;

    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmap> target;
    ComPtr<ID2D1RenderTarget> render;
    if (!app.factory || FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()))) ||
        FAILED(wic->CreateBitmap(64, 64, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, target.GetAddressOf())) ||
        FAILED(app.factory->CreateWicBitmapRenderTarget(target.Get(), D2D1::RenderTargetProperties(), render.GetAddressOf()))) {
        log << "Background decoder initialization failed\n";
        return 3;
    }

    const Settings defaults;
    Gfx gfx;
    gfx.rt = render.Get();
    bool ok = true;
    for (int k = 0; k < PICTURE_PLACES; ++k) {
        const std::filesystem::path folder(PICTURE_FOLDERS[k]);
        if (!app.settings.background[k] || app.settings.backgroundName[k] != defaults.backgroundName[k]) {
            log << "Default background not selected: " << folder.string() << '\n';
            ok = false;
        }
        for (const std::wstring& name : app.settings.backgroundFiles[k]) {
            const auto relative = folder / name;
            Backdrop picture;
            render->BeginDraw();
            const bool decoded = picture.draw(gfx, app.mediaFile(L"background", relative), rectF(0, 0, 64, 64), 0);
            const bool drawn = SUCCEEDED(render->EndDraw()) && decoded;
            picture.release();
            ok = ok && drawn;
            log << (drawn ? "OK " : "ERROR ") << "background/" << toUtf8(relative.generic_wstring()) << '\n';
        }
    }

    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
        log << "Music decoder initialization failed\n";
        return 4;
    }
    for (int k = 0; k < MUSIC_SCENES; ++k) {
        const std::filesystem::path folder(MUSIC_FOLDERS[k]);
        if (app.settings.musicFile(k).empty() || app.settings.bgmName[k] != defaults.bgmName[k]) {
            log << "Default music not selected: " << folder.string() << '\n';
            ok = false;
        }
        for (const std::wstring& name : app.settings.bgmFiles[k]) {
            const auto relative = folder / name;
            const MusicSamples samples = readMusic(app.mediaFile(L"bgm", relative));
            const bool decoded = !samples.pcm.empty();
            ok = ok && decoded;
            log << (decoded ? "OK " : "ERROR ") << "bgm/" << toUtf8(relative.generic_wstring()) << ' ' << samples.pcm.size() << " bytes PCM\n";
        }
    }
    MFShutdown();
    log.flush();
    return ok && log ? 0 : 5;
}

} // namespace play
