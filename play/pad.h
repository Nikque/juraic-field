#pragma once
// Game controllers. Xbox pads are read with XInput (it works whether or not
// the window has the focus and needs no start-up time); any other HID pad
// (pads of other makers, arcade sticks) is read through Windows.Gaming.Input
// as a raw controller with buttons, hat switches and axes.
// The pads of Sony and Nintendo (PlayStation pads, Switch Pro Controller, Joy-Con)
// are read through SDL when the game is built with it (JURAIC_PADS_SDL): such a
// pad sends reports of its own kind once a program has asked for them (the Pro
// Controller always on USB), and Windows then reads them as noise - a direction
// and buttons held that nobody holds. SDL reads those reports; such a pad counts
// as an Xbox pad, as outside Windows (play/pad_sdl.h).
//
// Every pad is reduced to one 64-bit word: bits 0..31 are its buttons, bits
// 32..35 the direction (hat switch or first stick): up, down, left, right.
// XInput buttons are numbered A 0, B 1, X 2, Y 3, LB 4, RB 5, Back 6, Start 7,
// left stick 8, right stick 9. Which bit means what in the game is decided by
// the bindings in input.h. Outside Windows the pads are those of SDL (play/pad_sdl.h), reduced to the same word.
#ifndef _WIN32
#include "pad_sdl.h"
#else
#include <windows.h>
#include <xinput.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#ifdef JURAIC_PADS_SDL
#include <SDL3/SDL.h>
#include <map>
#endif

namespace play {

constexpr int PAD_UP = 32, PAD_DOWN = 33, PAD_LEFT = 34, PAD_RIGHT = 35;

class Pads {
public:
    // A kind of pad: its vendor and product numbers.
    struct Id {
        uint16_t vendor, product;
        bool operator==(const Id&) const = default;
    };
    // Is a raw controller of Windows read: not when SDL reads a pad of its kind.
    static bool rawWanted(uint16_t vendor, uint16_t product, const std::vector<Id>& bySdl) {
        return std::find(bySdl.begin(), bySdl.end(), Id{vendor, product}) == bySdl.end();
    }
#ifdef JURAIC_PADS_SDL
    // The hints SDL is started with: of its drivers only those of the pads of Sony and Nintendo (Xbox pads stay with
    // XInput, every other pad with Windows.Gaming.Input), and the timer of Windows left as it is (SDL would set it to
    // 1 ms for the whole program).
    static std::vector<std::pair<const char*, const char*>> sdlHints() {
        return {{SDL_HINT_JOYSTICK_HIDAPI, "0"},
                {SDL_HINT_JOYSTICK_HIDAPI_PS4, "1"},
                {SDL_HINT_JOYSTICK_HIDAPI_PS5, "1"},
                {SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "1"},
                {SDL_HINT_JOYSTICK_HIDAPI_SWITCH2, "1"},
                {SDL_HINT_JOYSTICK_HIDAPI_JOY_CONS, "1"},
                {SDL_HINT_JOYSTICK_HIDAPI_NINTENDO_CLASSIC, "1"},
                {SDL_HINT_JOYSTICK_RAWINPUT, "0"},
                {SDL_HINT_JOYSTICK_WGI, "0"},
                {SDL_HINT_XINPUT_ENABLED, "0"},
                {SDL_HINT_JOYSTICK_DIRECTINPUT, "0"},
                {SDL_HINT_JOYSTICK_GAMEINPUT, "0"},
                {SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1"},
                {SDL_HINT_TIMER_RESOLUTION, "0"},
                {SDL_HINT_NO_SIGNAL_HANDLERS, "1"}};
    }
#endif

private:
    using Raw = winrt::Windows::Gaming::Input::RawGameController;
    using Gamepad = winrt::Windows::Gaming::Input::Gamepad;
    using Switch = winrt::Windows::Gaming::Input::GameControllerSwitchPosition;
    std::vector<Raw> list;
    std::vector<Id> listIds; // (of list)
    int refresh = 0;
    bool failed = false;
    std::vector<uint64_t> state; // Xbox pads first, then the others, then those SDL reads
    std::vector<bool> xbox, bySdl;
#ifdef JURAIC_PADS_SDL
    bool sdlStarted = false, sdlFailed = false;
    std::map<SDL_JoystickID, SDL_Gamepad*> sdlPads;
    std::vector<Id> sdlIds; // (of sdlPads)

    // Opens the pads SDL has found and closes those that are gone.
    void updateSdl() {
        if (sdlFailed) return;
        if (!sdlStarted) {
            sdlStarted = true;
            for (const auto& [name, value] : sdlHints()) SDL_SetHint(name, value);
            if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
                sdlFailed = true; // the pads of Sony and Nintendo stay raw controllers
                return;
            }
            // (the pads are read by their state: nothing is queued for events nobody takes)
            SDL_SetJoystickEventsEnabled(false);
            SDL_SetGamepadEventsEnabled(false);
        }
        SDL_UpdateGamepads();
        int count = 0;
        SDL_JoystickID* ids = SDL_GetJoysticks(&count);
        std::map<SDL_JoystickID, bool> present;
        for (int i = 0; i < count; ++i) {
            present[ids[i]] = true;
            if (!sdlPads.count(ids[i]) && SDL_IsGamepad(ids[i]))
                if (SDL_Gamepad* g = SDL_OpenGamepad(ids[i])) sdlPads[ids[i]] = g;
        }
        SDL_free(ids);
        for (auto it = sdlPads.begin(); it != sdlPads.end();) it = present.count(it->first) ? std::next(it) : (SDL_CloseGamepad(it->second), sdlPads.erase(it));
        sdlIds.clear();
        for (auto& [id, g] : sdlPads) sdlIds.push_back({SDL_GetGamepadVendor(g), SDL_GetGamepadProduct(g)});
        SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    }
    // A pad SDL knows the layout of, as play/pad_sdl.h reads it: by the places of its buttons, as an Xbox pad.
    static uint64_t readSdl(SDL_Gamepad* g) {
        static const SDL_GamepadButton order[10] = {SDL_GAMEPAD_BUTTON_SOUTH,         SDL_GAMEPAD_BUTTON_EAST,           SDL_GAMEPAD_BUTTON_WEST,
                                                    SDL_GAMEPAD_BUTTON_NORTH,         SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,  SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
                                                    SDL_GAMEPAD_BUTTON_BACK,          SDL_GAMEPAD_BUTTON_START,          SDL_GAMEPAD_BUTTON_LEFT_STICK,
                                                    SDL_GAMEPAD_BUTTON_RIGHT_STICK};
        uint64_t bits = 0;
        for (int i = 0; i < 10; ++i)
            if (SDL_GetGamepadButton(g, order[i])) bits |= 1ull << i;
        // (the stick as XInput is read: up beyond 20000, the sides beyond 16000; SDL counts down as plus)
        const int x = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX), y = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -20000) bits |= 1ull << PAD_UP;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > 20000) bits |= 1ull << PAD_DOWN;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -16000) bits |= 1ull << PAD_LEFT;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > 16000) bits |= 1ull << PAD_RIGHT;
        return bits;
    }
#endif

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
#ifdef JURAIC_PADS_SDL
    ~Pads() {
        for (auto& [id, g] : sdlPads) SDL_CloseGamepad(g);
    }
#endif
    // Reads every pad. Call once per game frame.
    void update() {
        state.clear();
        xbox.clear();
        bySdl.clear();
#ifdef JURAIC_PADS_SDL
        updateSdl();
        const std::vector<Id>& sdlKinds = sdlIds;
#else
        const std::vector<Id> sdlKinds;
#endif
        for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
            uint64_t bits;
            if (readXInput(i, bits)) state.push_back(bits), xbox.push_back(true), bySdl.push_back(false);
        }
        try {
            if (!failed && --refresh < 0) {
                // The system fills this list a moment after the first call and when pads come and go.
                refresh = 30;
                list.clear();
                listIds.clear();
                for (const Raw& raw : Raw::RawGameControllers())
                    if (!Gamepad::FromGameController(raw)) // Xbox layout: XInput has it
                        list.push_back(raw), listIds.push_back({raw.HardwareVendorId(), raw.HardwareProductId()});
            }
            for (size_t i = 0; !failed && i < list.size(); ++i)
                if (rawWanted(listIds[i].vendor, listIds[i].product, sdlKinds)) state.push_back(readRaw(list[i])), xbox.push_back(false), bySdl.push_back(false);
        } catch (const winrt::hresult_error&) {
            failed = true; // no support for other controllers on this system
        }
#ifdef JURAIC_PADS_SDL
        for (auto& [id, g] : sdlPads) state.push_back(readSdl(g)), xbox.push_back(true), bySdl.push_back(true);
#endif
    }
    int count() const { return int(state.size()); }
    uint64_t raw(int index) const { return index >= 0 && index < count() ? state[size_t(index)] : 0; }
    bool isXbox(int index) const { return index >= 0 && index < count() && xbox[size_t(index)]; }
    // One line per controller, for diagnostics.
    std::wstring describe() const {
        std::wstring out;
        for (int i = 0; i < count(); ++i)
            out += L"pad " + std::to_wstring(i + 1) + (bySdl[size_t(i)] ? L" (SDL)" : isXbox(i) ? L" (XInput)" : L" (raw)") + (raw(i) ? L" pressed; " : L"; ");
        return out.empty() ? L"no controller" : out;
    }
};

} // namespace play
#endif
