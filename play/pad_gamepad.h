#pragma once
// The word every pad is reduced to (play/pad.h): its directions in the bits PAD_UP to PAD_RIGHT. Where SDL reads the
// pads (outside Windows, play/pad_sdl.h; on Windows when the game is built with JURAIC_PADS_SDL), a pad SDL knows the
// layout of is read by the places of its buttons, as an Xbox pad (readGamepad).
#include <cstdint>
#if !defined(_WIN32) || defined(JURAIC_PADS_SDL)
#include <SDL3/SDL.h>
#endif

namespace play {

constexpr int PAD_UP = 32, PAD_DOWN = 33, PAD_LEFT = 34, PAD_RIGHT = 35;

#if !defined(_WIN32) || defined(JURAIC_PADS_SDL)
// The lower main button is A (bit 0), the right one B (1), then X, Y, LB, RB, Back, Start, the left stick, the right
// stick (bits 2 to 9); the directions are the cross or the left stick.
inline uint64_t readGamepad(SDL_Gamepad* g) {
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
#endif

} // namespace play
