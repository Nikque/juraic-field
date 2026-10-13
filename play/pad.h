#pragma once
// Game controllers. Xbox pads are read with XInput (it works whether or not
// the window has the focus and needs no start-up time); any other HID pad
// (Switch Pro Controller, PlayStation pads, arcade sticks) is read through
// Windows.Gaming.Input as a raw controller with buttons, hat switches and axes.
//
// Every pad is reduced to one 64-bit word: bits 0..31 are its buttons, bits
// 32..35 the direction (hat switch or first stick): up, down, left, right.
// XInput buttons are numbered A 0, B 1, X 2, Y 3, LB 4, RB 5, Back 6, Start 7,
// left stick 8, right stick 9. Which bit means what in the game is decided by
// the bindings in input.h.
#include <windows.h>
#include <xinput.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>
#include <string>
#include <vector>

namespace play {

constexpr int PAD_UP = 32, PAD_DOWN = 33, PAD_LEFT = 34, PAD_RIGHT = 35;

class Pads {
    using Raw = winrt::Windows::Gaming::Input::RawGameController;
    using Gamepad = winrt::Windows::Gaming::Input::Gamepad;
    using Switch = winrt::Windows::Gaming::Input::GameControllerSwitchPosition;
    std::vector<Raw> list;
    int refresh = 0;
    bool failed = false;
    std::vector<uint64_t> state; // Xbox pads first, then the others
    std::vector<bool> xbox;

    static bool readXInput(DWORD index, uint64_t& bits) {
        XINPUT_STATE s{};
        if (XInputGetState(index, &s) != ERROR_SUCCESS) return false;
        const WORD b = s.Gamepad.wButtons;
        static const WORD order[10] = {XINPUT_GAMEPAD_A,             XINPUT_GAMEPAD_B,              XINPUT_GAMEPAD_X,    XINPUT_GAMEPAD_Y,
                                       XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER, XINPUT_GAMEPAD_BACK, XINPUT_GAMEPAD_START,
                                       XINPUT_GAMEPAD_LEFT_THUMB,    XINPUT_GAMEPAD_RIGHT_THUMB};
        bits = 0;
        for (int i = 0; i < 10; ++i)
            if (b & order[i]) bits |= 1ull << i;
        if ((b & XINPUT_GAMEPAD_DPAD_UP) || s.Gamepad.sThumbLY > 20000) bits |= 1ull << PAD_UP;
        if ((b & XINPUT_GAMEPAD_DPAD_DOWN) || s.Gamepad.sThumbLY < -20000) bits |= 1ull << PAD_DOWN;
        if ((b & XINPUT_GAMEPAD_DPAD_LEFT) || s.Gamepad.sThumbLX < -16000) bits |= 1ull << PAD_LEFT;
        if ((b & XINPUT_GAMEPAD_DPAD_RIGHT) || s.Gamepad.sThumbLX > 16000) bits |= 1ull << PAD_RIGHT;
        return true;
    }
    static uint64_t readRaw(const Raw& raw) {
        bool buttons[32]{};
        Switch switches[8]{};
        double axes[16];
        for (double& a : axes) a = 0.5;
        const int nb = std::min(raw.ButtonCount(), 32), ns = std::min(raw.SwitchCount(), 8), na = std::min(raw.AxisCount(), 16);
        raw.GetCurrentReading(winrt::array_view<bool>(buttons, buttons + nb), winrt::array_view<Switch>(switches, switches + ns),
                              winrt::array_view<double>(axes, axes + na));
        uint64_t bits = 0;
        for (int i = 0; i < nb; ++i)
            if (buttons[i]) bits |= 1ull << i;
        for (int i = 0; i < ns; ++i) {
            const Switch p = switches[i];
            if (p == Switch::Up || p == Switch::UpLeft || p == Switch::UpRight) bits |= 1ull << PAD_UP;
            if (p == Switch::Down || p == Switch::DownLeft || p == Switch::DownRight) bits |= 1ull << PAD_DOWN;
            if (p == Switch::Left || p == Switch::UpLeft || p == Switch::DownLeft) bits |= 1ull << PAD_LEFT;
            if (p == Switch::Right || p == Switch::UpRight || p == Switch::DownRight) bits |= 1ull << PAD_RIGHT;
        }
        if (na >= 2) {
            if (axes[0] < 0.2) bits |= 1ull << PAD_LEFT;
            if (axes[0] > 0.8) bits |= 1ull << PAD_RIGHT;
            if (axes[1] < 0.15) bits |= 1ull << PAD_UP;
            if (axes[1] > 0.85) bits |= 1ull << PAD_DOWN;
        }
        return bits;
    }

public:
    // Reads every pad. Call once per game frame.
    void update() {
        state.clear();
        xbox.clear();
        for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
            uint64_t bits;
            if (readXInput(i, bits)) state.push_back(bits), xbox.push_back(true);
        }
        if (failed) return;
        try {
            if (--refresh < 0) {
                // The system fills this list a moment after the first call and when pads come and go.
                refresh = 30;
                list.clear();
                for (const Raw& raw : Raw::RawGameControllers())
                    if (!Gamepad::FromGameController(raw)) list.push_back(raw); // Xbox layout: XInput has it
            }
            for (const Raw& raw : list) state.push_back(readRaw(raw)), xbox.push_back(false);
        } catch (const winrt::hresult_error&) {
            failed = true; // no support for other controllers on this system
        }
    }
    int count() const { return int(state.size()); }
    uint64_t raw(int index) const { return index >= 0 && index < count() ? state[size_t(index)] : 0; }
    bool isXbox(int index) const { return index >= 0 && index < count() && xbox[size_t(index)]; }
    // One line per controller, for diagnostics.
    std::wstring describe() const {
        std::wstring out;
        for (int i = 0; i < count(); ++i)
            out += L"pad " + std::to_wstring(i + 1) + (isXbox(i) ? L" (XInput)" : L" (raw)") + (raw(i) ? L" pressed; " : L"; ");
        return out.empty() ? L"no controller" : out;
    }
};

} // namespace play
