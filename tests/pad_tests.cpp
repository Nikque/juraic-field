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
    }

    std::printf(failures ? "%d FAILED\n" : "pad tests passed\n", failures);
    return failures ? 1 : 0;
}
