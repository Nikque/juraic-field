// The controllers of the Windows game (play/pad.h, play/input.h): which pads SDL reads, that a pad read by SDL is not read a
// second time as a raw controller of Windows nor a pad XInput reads a second time through SDL, and that a button held as a
// game begins waits to be let go. With --live: what
// every connected pad gives, for ten seconds.
#define NOMINMAX // (as play/main.cpp: pad.h uses std::min)
#include "../play/input.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

using namespace play;

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) ++failures, std::printf("FAILED: %s\n", what);
}
static const char* hint(const char* name) {
    for (const auto& [n, v] : Pads::sdlHints())
        if (std::strcmp(n, name) == 0) return v;
    return nullptr;
}
static bool is(const char* value, const char* wanted) { return value && std::strcmp(value, wanted) == 0; }

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--live") == 0) {
        Pads pads;
        for (int frame = 0; frame < 600; ++frame) {
            pads.update();
            if (frame % 60 == 59) {
                std::printf("%2d s:", frame / 60 + 1);
                for (int i = 0; i < pads.count(); ++i) std::printf("  pad %d%s %016llx", i + 1, pads.isXbox(i) ? " (Xbox layout)" : "", (unsigned long long)pads.raw(i));
                std::printf("  | %ls\n", pads.describe().c_str());
            }
            std::this_thread::sleep_for(std::chrono::microseconds(16667));
        }
        return 0;
    }

    // SDL reads the pads of Sony and Nintendo by their own reports, and nothing else: Xbox pads stay with XInput and
    // every other pad with Windows.Gaming.Input, as they were.
    check(is(hint(SDL_HINT_JOYSTICK_HIDAPI), "0"), "the HIDAPI drivers of SDL are off unless named");
    for (const char* on : {SDL_HINT_JOYSTICK_HIDAPI_PS4, SDL_HINT_JOYSTICK_HIDAPI_PS5, SDL_HINT_JOYSTICK_HIDAPI_SWITCH, SDL_HINT_JOYSTICK_HIDAPI_SWITCH2,
                           SDL_HINT_JOYSTICK_HIDAPI_JOY_CONS, SDL_HINT_JOYSTICK_HIDAPI_NINTENDO_CLASSIC})
        check(is(hint(on), "1"), on);
    for (const char* off : {SDL_HINT_JOYSTICK_RAWINPUT, SDL_HINT_JOYSTICK_WGI, SDL_HINT_XINPUT_ENABLED, SDL_HINT_JOYSTICK_DIRECTINPUT, SDL_HINT_JOYSTICK_GAMEINPUT})
        check(is(hint(off), "0"), off);
    // (SDL would set the timer of Windows to 1 ms for the whole program: the game's timing stays as it was)
    check(is(hint(SDL_HINT_TIMER_RESOLUTION), "0"), "the timer resolution of Windows is left alone");

    // XInput is read through xinput1_4.dll, the XInput of programs of the desktop. (Through windowsapp.lib XInputGetState
    // was that of XInputUap.dll, the XInput of store apps: a virtual Xbox 360 pad's button held 120 ms was seen 5 times
    // of 40 with the window in front and never with another window in front; through xinput1_4.dll 40 times of 40.)
    check(Pads::xinputLibrary() == L"xinput1_4.dll", "XInput is read through xinput1_4.dll");

    // A raw controller of Windows is read unless SDL reads the same kind of pad.
    const std::vector<Pads::Id> bySdl = {{0x057E, 0x2009}, {0x054C, 0x09CC}};
    check(!Pads::rawWanted(0x057E, 0x2009, bySdl), "a Pro Controller read by SDL is not read raw");
    check(!Pads::rawWanted(0x054C, 0x09CC, bySdl), "a DualShock 4 read by SDL is not read raw");
    check(Pads::rawWanted(0x0F0D, 0x00C1, bySdl), "a HORIPAD is read raw as before");
    check(Pads::rawWanted(0x054C, 0x0CE6, bySdl), "a pad SDL has not opened is read raw as before");
    check(Pads::rawWanted(0x057E, 0x2009, {}), "with SDL reading nothing, every raw controller is read as before");

    // A pad of Sony or Nintendo that XInput reads already (made an Xbox pad by another program) is not read a second time
    // through SDL: as in the record of a DualShock 4 read twice, Windows listing it with the layout of an Xbox pad and
    // XInput having one pad.
    {
        const Pads::Id ds4{0x054C, 0x05C4}, pro{0x057E, 0x2009}, xbox{0x045E, 0x02EA};
        check(!Pads::sdlWanted(ds4, {ds4}, 1, 1), "a DualShock 4 that XInput reads is read through SDL as well");
        check(!Pads::sdlWanted(ds4, {xbox, ds4}, 2, 2), "with an Xbox pad as well, a DualShock 4 that XInput reads is read through SDL");
        check(Pads::sdlWanted(ds4, {}, 1, 1), "a DualShock 4 Windows does not list as an Xbox pad is not read through SDL");
        check(Pads::sdlWanted(ds4, {xbox, ds4}, 2, 1), "a DualShock 4 no XInput number reads is not read through SDL");
        check(Pads::sdlWanted(pro, {ds4}, 1, 1), "a Pro Controller is not read through SDL when another kind is in XInput");
    }

    // The buttons held as a game begins (on a controller the button that chose it in the menu is a rotation): none of
    // them counts until it has been let go.
    {
        HeldAtStart h;
        check(h(juraic::BTN_CW) == juraic::BTN_CW, "before a game begins, the buttons pass");
        h.begin();
        check(h(juraic::BTN_CW) == 0, "the button held as the game began does not count");
        check(h(juraic::BTN_CW | juraic::BTN_LEFT) == juraic::BTN_LEFT, "a button pressed after the beginning counts while the other is held");
        check(h(0) == 0 && h(juraic::BTN_CW) == juraic::BTN_CW, "let go and pressed again, the button counts");
        h.begin();
        check(h(0) == 0 && h(juraic::BTN_CCW) == juraic::BTN_CCW, "with nothing held as the game began, the first press counts");
        // The countdown before a match, in which the game does not take the buttons: what is let go and pressed
        // there counts as the play begins (pre-input); what is held all through it does not.
        h.begin();
        h.watch(juraic::BTN_CW), h.watch(0), h.watch(juraic::BTN_LEFT | juraic::BTN_CW);
        check(h(juraic::BTN_LEFT | juraic::BTN_CW) == (juraic::BTN_LEFT | juraic::BTN_CW), "let go in the countdown and pressed again in it, the buttons count as the play begins");
        h.begin();
        h.watch(juraic::BTN_CW), h.watch(juraic::BTN_CW | juraic::BTN_LEFT);
        check(h(juraic::BTN_CW | juraic::BTN_LEFT) == juraic::BTN_LEFT, "held from the menu all through the countdown, the button does not count; one pressed in it does");
    }

    // The devices of two people: a device a player has chosen stays that player's, whatever the other is given.
    {
        const auto chosen = [](int first, int second, int padCount, int want0, int want1) {
            InputConfig config;
            config.device[0] = first, config.device[1] = second;
            int device[2] = {-9, -9};
            chooseDevices(config, padCount, 2, device);
            return device[0] == want0 && device[1] == want1;
        };
        check(chosen(-1, 2, 1, 0, 2), "2P chose controller 1: 1P, choosing nothing, does not take it");
        check(chosen(-1, 0, 0, 1, 0), "2P chose keyboard 1: 1P, choosing nothing, gets keyboard 2");
        check(chosen(-1, 3, 2, 2, 3), "2P chose controller 2: 1P, choosing nothing, gets controller 1");
        check(chosen(-1, 2, 2, 3, 2), "2P chose controller 1: 1P, choosing nothing, gets controller 2");
        check(chosen(2, -1, 1, 2, 0), "1P chose controller 1: 2P, choosing nothing, gets keyboard 1");
        check(chosen(-1, -1, 2, 2, 3) && chosen(-1, -1, 1, 2, 0) && chosen(-1, -1, 0, 0, 1), "nobody chose: controllers first, then keyboard 1 and 2");
        check(chosen(3, -1, 1, 2, 0), "a chosen controller that is gone counts as none chosen");
        check(chosen(2, 2, 1, 2, 0), "both chose the same controller: 2P gets keyboard 1");
    }

    // A controller's binding is a bit of the 64 of Pads::raw: 64 to 255 (a key code's range) are not taken from the
    // settings file, and a bit beyond the 64 is never held (not the bit the shift would wrap round to).
    {
        check(InputConfig::fits(LAYOUT_PAD, 0) && InputConfig::fits(LAYOUT_PAD, 63) && InputConfig::fits(LAYOUT_XBOX, 63),
              "a controller's buttons 0 and 63 can be bound");
        check(!InputConfig::fits(LAYOUT_PAD, 64) && !InputConfig::fits(LAYOUT_XBOX, 64) && !InputConfig::fits(LAYOUT_PAD, 255) &&
                  !InputConfig::fits(LAYOUT_PAD, -1),
              "a controller's binding of 64, 255 or -1 is taken");
        check(InputConfig::fits(LAYOUT_KEYS1, 255) && InputConfig::fits(LAYOUT_KEYS2, 64) && !InputConfig::fits(LAYOUT_KEYS1, 256),
              "the key codes 64 to 255 are not taken for a keyboard, or 256 is");
        check(padHeld(1ull << 63, 63) && padHeld(1, 0) && !padHeld(~0ull, 64) && !padHeld(~0ull, 255) && !padHeld(~0ull, -1),
              "a button beyond the 64 bits of a controller is held");
    }

    std::printf(failures ? "%d FAILED\n" : "pad tests passed\n", failures);
    return failures ? 1 : 0;
}
