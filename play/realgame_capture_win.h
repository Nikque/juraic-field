#pragma once
// The mode in which Juraic plays 2P of the real game (play/realgame_win.h), the Windows build: the pictures of the
// real game's window, taken by the capture of a window of Windows (Windows.Graphics.Capture, Windows 10 1903 and
// later). Nothing is sent to the window and nothing of the real game is read but its picture. Which window is taken
// is given by the caller (the person brings it to the front; it is not looked for by its name). Only the inside of
// the window is passed on, picture after picture, on a thread of the system.
#include "realgame_read.h"
#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <functional>
#include <mutex>
#include <string>

namespace play {

class WindowCapture {
public:
    // Called on a thread of the system with every picture of the inside of the window (valid during the call) and the
    // time it was shown, in 100 ns (the clock of QueryPerformanceCounter).
    using Sink = std::function<void(const realgame::Picture&, int64_t)>;

    WindowCapture() = default;
    WindowCapture(const WindowCapture&) = delete;
    WindowCapture& operator=(const WindowCapture&) = delete;
    ~WindowCapture() { stop(); }

    // Begins to take the pictures of the window `w`. False, with the reason in `error`, when Windows cannot.
    bool start(HWND w, Sink to, std::wstring& error) {
        namespace capture = winrt::Windows::Graphics::Capture;
        namespace dx = winrt::Windows::Graphics::DirectX;
        stop();
        try {
            if (!capture::GraphicsCaptureSession::IsSupported()) {
                error = L"この Windows では窓の画面を取り込めません（Windows 10 1903 以降が要ります）";
                return false;
            }
            winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                                   d3d.put(), nullptr, context.put()));
            winrt::com_ptr<::IInspectable> inspectable;
            winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(d3d.as<IDXGIDevice>().get(), inspectable.put()));
            device = inspectable.as<dx::Direct3D11::IDirect3DDevice>();
            const auto interop = winrt::get_activation_factory<capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
            winrt::check_hresult(interop->CreateForWindow(w, winrt::guid_of<capture::GraphicsCaptureItem>(), winrt::put_abi(item)));
            {
                std::lock_guard<std::mutex> hold(busy);
                target = w, sink = std::move(to), size = item.Size();
            }
            pool = capture::Direct3D11CaptureFramePool::CreateFreeThreaded(device, dx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
            arrived = pool.FrameArrived([this](const capture::Direct3D11CaptureFramePool& from, const winrt::Windows::Foundation::IInspectable&) { take(from); });
            session = pool.CreateCaptureSession(item);
            try {
                session.IsCursorCaptureEnabled(false); // (Windows 10 2004 and later; before, the pointer is in the picture)
            } catch (const winrt::hresult_error&) {
            }
            session.StartCapture();
        } catch (const winrt::hresult_error& e) {
            error = L"窓の画面を取り込めません: " + std::wstring(e.message());
            stop();
            return false;
        }
        return true;
    }

    void stop() {
        if (pool && arrived) pool.FrameArrived(arrived);
        arrived = {};
        std::lock_guard<std::mutex> hold(busy);
        if (session) session.Close();
        if (pool) pool.Close();
        session = nullptr, pool = nullptr, item = nullptr, device = nullptr;
        staging = nullptr, context = nullptr, d3d = nullptr;
        target = nullptr, sink = nullptr;
    }

    bool running() const { return session != nullptr; }
    HWND window() const { return target; }

private:
    HWND target = nullptr;
    Sink sink;
    winrt::com_ptr<ID3D11Device> d3d;
    winrt::com_ptr<ID3D11DeviceContext> context;
    winrt::com_ptr<ID3D11Texture2D> staging;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice device{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool pool{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession session{nullptr};
    winrt::event_token arrived{};
    winrt::Windows::Graphics::SizeInt32 size{};
    std::mutex busy; // a picture being taken, and stop()

    // A picture has come: the inside of the window copied where the processor can read it and passed on.
    void take(const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool& from) {
        std::lock_guard<std::mutex> hold(busy);
        if (!sink || !context) return;
        try {
            const winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame frame = from.TryGetNextFrame();
            if (!frame) return;
            const winrt::Windows::Graphics::SizeInt32 content = frame.ContentSize();
            if (content.Width != size.Width || content.Height != size.Height) {
                // (the window changed its size: the pictures from the next one on are of the new size)
                size = content;
                from.Recreate(device, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
                return;
            }
            // Where the inside of the window is in the picture of the whole window (the picture is of the window as it
            // shows, its frame included, in the pixels of the screen whatever the scaling of this program).
            RECT inside{}, shown{};
            POINT corner{0, 0};
            const DPI_AWARENESS_CONTEXT was = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            const bool placed = GetClientRect(target, &inside) && ClientToScreen(target, &corner) &&
                                SUCCEEDED(DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS, &shown, sizeof shown));
            if (was) SetThreadDpiAwarenessContext(was);
            if (!placed) return;
            const int left = std::max(0, int(corner.x - shown.left)), top = std::max(0, int(corner.y - shown.top));
            const int width = std::min(int(inside.right), content.Width - left), height = std::min(int(inside.bottom), content.Height - top);
            if (width <= 0 || height <= 0) return;
            const auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
            winrt::com_ptr<ID3D11Texture2D> texture;
            winrt::check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), texture.put_void()));
            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);
            D3D11_TEXTURE2D_DESC had{};
            if (staging) staging->GetDesc(&had);
            if (!staging || had.Width != desc.Width || had.Height != desc.Height || had.Format != desc.Format) {
                D3D11_TEXTURE2D_DESC s = desc;
                s.Usage = D3D11_USAGE_STAGING, s.BindFlags = 0, s.CPUAccessFlags = D3D11_CPU_ACCESS_READ, s.MiscFlags = 0;
                s.MipLevels = 1, s.ArraySize = 1, s.SampleDesc = {1, 0};
                staging = nullptr;
                winrt::check_hresult(d3d->CreateTexture2D(&s, nullptr, staging.put()));
            }
            context->CopyResource(staging.get(), texture.get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped))) return;
            const realgame::Picture picture{static_cast<const uint8_t*>(mapped.pData) + size_t(top) * mapped.RowPitch + size_t(left) * 4, width, height,
                                            int(mapped.RowPitch)};
            sink(picture, frame.SystemRelativeTime().count());
            context->Unmap(staging.get(), 0);
        } catch (const winrt::hresult_error&) {
            // (the window has gone, or the device was lost: no picture; the caller sees that none comes)
        }
    }
};

} // namespace play
