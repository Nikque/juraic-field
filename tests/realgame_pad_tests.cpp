// The virtual pad of the mode in which Juraic plays 2P of the real game (play/realgame_win.h, the Windows build):
//  1. it stays plugged in while the screen of the mode is open, so that the real game keeps it as the pad of 2P - after
//     a press that picked it for 2P and when the AI stops - and it is pulled out as the mode is left;
//  2. a press asked for (to pick it for 2P in the real game's menus) holds the chosen button PRESS_HOLD frames after
//     PRESS_WAIT frames, as XInput reads the pad, and nothing before or after; it can be called off.
// It plugs a pad of the driver ViGEmBus into this machine, which a running game would see: it runs only with
// JURAIC_TEST_VIRTUAL_PAD=1 set and the driver installed, and is skipped otherwise (exit code 77).
#define NOMINMAX
#include <windows.h>
#include <xinput.h>
#include "../play/realgame_win.h"
#include <cstdio>
#include <cstdlib>
#include <string>

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) std::printf("FAILED: %s\n", what), ++failures;
}

int main() {
    const char* wanted = std::getenv("JURAIC_TEST_VIRTUAL_PAD");
    if (!wanted || std::string(wanted) != "1") return std::printf("skipped: set JURAIC_TEST_VIRTUAL_PAD=1 to plug in a virtual pad\n"), 77;
    using Mode = play::RealGameMode;
    Mode m;
    std::wstring error;
    if (!m.pressLater(error)) return std::printf("skipped: the driver ViGEmBus cannot be reached\n"), 77;
    m.stopPress();
    // What XInput reads of the pad after a frame of the mode (the driver takes a moment to hand a report on). Read
    // through xinput1_4.dll by name: the XInput of windowsapp.lib (XInputUap) gives nothing to a program whose window is
    // not in front, as this one has none.
    using GetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    const HMODULE xinput = LoadLibraryW(L"xinput1_4.dll");
    const GetState getState = xinput ? reinterpret_cast<GetState>(GetProcAddress(xinput, "XInputGetState")) : nullptr;
    if (!getState) return std::printf("FAILED: xinput1_4.dll cannot be read\n"), 1;
    const auto frame = [&]() {
        m.drive(0);
        Sleep(20);
        XINPUT_STATE s{};
        return m.pad.xinputSlot() >= 0 && getState(DWORD(m.pad.xinputSlot()), &s) == ERROR_SUCCESS ? int(s.Gamepad.wButtons) : -1;
    };

    // 2. A press of A, then of Start.
    check(m.pressLater(error) && m.pad.running() && m.pressing(), "a press asked for, the pad is plugged in and waits");
    Sleep(1500); // (Windows shows a pad plugged in a moment later; the wait counts frames of the mode, not time)
    check(m.pressSeconds() == Mode::PRESS_WAIT / 60, "the wait shown is PRESS_WAIT in seconds");
    int early = 0, held = 0, wrong = 0;
    for (int f = 0; f < Mode::PRESS_WAIT + Mode::PRESS_HOLD; ++f) {
        const int b = frame();
        if (f < Mode::PRESS_WAIT - 1) early += b != 0;
        else if (f >= Mode::PRESS_WAIT) held += b == XUSB_GAMEPAD_A, wrong += b != XUSB_GAMEPAD_A && b != 0;
    }
    check(early == 0, "nothing is pressed in the wait");
    check(held >= Mode::PRESS_HOLD - 2 && wrong == 0, "after the wait A is held, PRESS_HOLD frames");
    check(frame() == 0 && !m.pressing(), "after the press the button is let go and no press is asked");
    check(frame() == 0, "and nothing is pressed after it");
    m.row = Mode::ROW_PRESS;
    check(m.change(1) && m.change(1) && m.pressButton == 2, "on the row of the press the button changes: A, B, Start");
    check(m.change(1) && m.pressButton == 0, "and round again to A");
    m.change(-1);
    m.pressLater(error);
    for (int f = 0; f < Mode::PRESS_WAIT + 2; ++f) frame();
    check(frame() == XUSB_GAMEPAD_START, "the button chosen (Start) is the one pressed");
    m.stopPress();
    check(frame() == 0 && !m.pressing(), "called off, the button is let go at once");
    m.pressButton = 0;

    // 1. The pad stays plugged in.
    check(m.pad.running(), "after the presses the pad stays plugged in (the real game keeps it as 2P)");
    m.pressLater(error);
    m.stopPress();
    m.stopPlaying();
    check(m.pad.running(), "the AI stopped (it had not begun), the pad stays plugged in");
    m.leave();
    check(!m.pad.running(), "the mode left, the pad is pulled out");
    m.open(0, 1);
    check(!m.pad.running() && !m.pressing(), "the mode opened, no pad is plugged in before one is asked for");
    std::printf(failures ? "%d FAILED\n" : "realgame pad tests passed\n", failures);
    return failures ? 1 : 0;
}
