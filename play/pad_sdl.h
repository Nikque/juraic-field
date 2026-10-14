#pragma once
// Game controllers outside Windows, read through SDL. A controller SDL knows the layout of (a gamepad) is read by
// the places of its buttons and counted as an Xbox pad, as on Windows: the lower main button is A (bit 0), the right
// one B (1), then X, Y, LB, RB, Back, Start, the left stick, the right stick (bits 2 to 9). Any other controller is
// read as it is: its buttons, its first hat switch and its first two axes. Reduced to the word of play/pad.h.
#include <SDL3/SDL.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace play {

constexpr int PAD_UP = 32, PAD_DOWN = 33, PAD_LEFT = 34, PAD_RIGHT = 35;

class Pads {
    bool started = false;
    std::map<SDL_JoystickID, SDL_Gamepad*> gamepads;
    std::map<SDL_JoystickID, SDL_Joystick*> others;
    std::vector<uint64_t> state; // the gamepads first, then the others
    std::vector<bool> xbox;

    static uint64_t readGamepad(SDL_Gamepad* g) {
        static const SDL_GamepadButton order[10] = {SDL_GAMEPAD_BUTTON_SOUTH,         SDL_GAMEPAD_BUTTON_EAST,           SDL_GAMEPAD_BUTTON_WEST,
                                                    SDL_GAMEPAD_BUTTON_NORTH,         SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,  SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
                                                    SDL_GAMEPAD_BUTTON_BACK,          SDL_GAMEPAD_BUTTON_START,          SDL_GAMEPAD_BUTTON_LEFT_STICK,
                                                    SDL_GAMEPAD_BUTTON_RIGHT_STICK};
        uint64_t bits = 0;
        for (int i = 0; i < 10; ++i)
            if (SDL_GetGamepadButton(g, order[i])) bits |= 1ull << i;
        // (the stick as XInput is read on Windows: up beyond 20000, the sides beyond 16000; SDL counts down as plus)
        const int x = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX), y = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -20000) bits |= 1ull << PAD_UP;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > 20000) bits |= 1ull << PAD_DOWN;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -16000) bits |= 1ull << PAD_LEFT;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > 16000) bits |= 1ull << PAD_RIGHT;
        return bits;
    }
    static uint64_t readJoystick(SDL_Joystick* j) {
        uint64_t bits = 0;
        const int buttons = std::min(SDL_GetNumJoystickButtons(j), 32);
        for (int i = 0; i < buttons; ++i)
            if (SDL_GetJoystickButton(j, i)) bits |= 1ull << i;
        if (SDL_GetNumJoystickHats(j) > 0) {
            const Uint8 hat = SDL_GetJoystickHat(j, 0);
            if (hat & SDL_HAT_UP) bits |= 1ull << PAD_UP;
            if (hat & SDL_HAT_DOWN) bits |= 1ull << PAD_DOWN;
            if (hat & SDL_HAT_LEFT) bits |= 1ull << PAD_LEFT;
            if (hat & SDL_HAT_RIGHT) bits |= 1ull << PAD_RIGHT;
        }
        // (the first two axes as the raw controllers of Windows are read: beyond a fifth from the middle at the sides,
        //  beyond 0.15 of the way up or down)
        if (SDL_GetNumJoystickAxes(j) >= 2) {
            const float x = (float(SDL_GetJoystickAxis(j, 0)) + 32768.f) / 65535.f, y = (float(SDL_GetJoystickAxis(j, 1)) + 32768.f) / 65535.f;
            if (x < 0.2f) bits |= 1ull << PAD_LEFT;
            if (x > 0.8f) bits |= 1ull << PAD_RIGHT;
            if (y < 0.15f) bits |= 1ull << PAD_UP;
            if (y > 0.85f) bits |= 1ull << PAD_DOWN;
        }
        return bits;
    }

public:
    ~Pads() {
        for (auto& [id, g] : gamepads) SDL_CloseGamepad(g);
        for (auto& [id, j] : others) SDL_CloseJoystick(j);
    }
    // Reads every pad. Call once per game frame.
    void update() {
        if (!started) started = true, SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1"), SDL_InitSubSystem(SDL_INIT_GAMEPAD);
        SDL_UpdateGamepads();
        int count = 0;
        SDL_JoystickID* ids = SDL_GetJoysticks(&count);
        std::map<SDL_JoystickID, bool> present;
        for (int i = 0; i < count; ++i) {
            const SDL_JoystickID id = ids[i];
            present[id] = true;
            if (gamepads.count(id) || others.count(id)) continue;
            if (SDL_IsGamepad(id)) {
                if (SDL_Gamepad* g = SDL_OpenGamepad(id)) gamepads[id] = g;
            } else if (SDL_Joystick* j = SDL_OpenJoystick(id)) {
                others[id] = j;
            }
        }
        SDL_free(ids);
        for (auto it = gamepads.begin(); it != gamepads.end();) it = present.count(it->first) ? std::next(it) : (SDL_CloseGamepad(it->second), gamepads.erase(it));
        for (auto it = others.begin(); it != others.end();) it = present.count(it->first) ? std::next(it) : (SDL_CloseJoystick(it->second), others.erase(it));
        state.clear();
        xbox.clear();
        for (auto& [id, g] : gamepads) state.push_back(readGamepad(g)), xbox.push_back(true);
        for (auto& [id, j] : others) state.push_back(readJoystick(j)), xbox.push_back(false);
    }
    int count() const { return int(state.size()); }
    uint64_t raw(int index) const { return index >= 0 && index < count() ? state[size_t(index)] : 0; }
    bool isXbox(int index) const { return index >= 0 && index < count() && xbox[size_t(index)]; }
    // One line per controller, for diagnostics.
    std::wstring describe() const {
        std::wstring out;
        for (int i = 0; i < count(); ++i)
            out += L"pad " + std::to_wstring(i + 1) + (isXbox(i) ? L" (gamepad)" : L" (joystick)") + (raw(i) ? L" pressed; " : L"; ");
        return out.empty() ? L"no controller" : out;
    }
};

} // namespace play
