// The virtual pad of the mode of the real game (play/realgame_pad_win.h) with the client of ViGEmBus replaced here, so
// that no driver is needed: a report the client could not send leaves what the pad holds unknown, and the next buttons
// asked for are sent even when they are those held before it; buttons sent are not sent again.
#define NOMINMAX
#include <windows.h>
#include "../play/realgame_input_win.h"
#include <cstdio>

static int sends = 0;
static bool failNext = false;
static USHORT sentButtons = 0;
static int handles[2];

extern "C" {
PVIGEM_CLIENT vigem_alloc() { return PVIGEM_CLIENT(&handles[0]); }
void vigem_free(PVIGEM_CLIENT) {}
VIGEM_ERROR vigem_connect(PVIGEM_CLIENT) { return VIGEM_ERROR_NONE; }
void vigem_disconnect(PVIGEM_CLIENT) {}
PVIGEM_TARGET vigem_target_x360_alloc() { return PVIGEM_TARGET(&handles[1]); }
void vigem_target_free(PVIGEM_TARGET) {}
VIGEM_ERROR vigem_target_add(PVIGEM_CLIENT, PVIGEM_TARGET) { return VIGEM_ERROR_NONE; }
VIGEM_ERROR vigem_target_remove(PVIGEM_CLIENT, PVIGEM_TARGET) { return VIGEM_ERROR_NONE; }
VIGEM_ERROR vigem_target_x360_get_user_index(PVIGEM_CLIENT, PVIGEM_TARGET, PULONG index) {
    *index = 0;
    return VIGEM_ERROR_NONE;
}
VIGEM_ERROR vigem_target_x360_update(PVIGEM_CLIENT, PVIGEM_TARGET, XUSB_REPORT report) {
    ++sends;
    if (failNext) return failNext = false, VIGEM_ERROR_WINAPI;
    sentButtons = report.wButtons;
    return VIGEM_ERROR_NONE;
}
}

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) std::printf("FAILED: %s\n", what), ++failures;
}

int main() {
    play::VirtualPad pad;
    std::wstring error;
    check(pad.start(error) && sends == 1, "the pad is not plugged in with nothing held");
    pad.press(juraic::BTN_LEFT);
    check(sends == 2 && sentButtons == XUSB_GAMEPAD_DPAD_LEFT, "the left is not sent");
    pad.press(juraic::BTN_LEFT);
    check(sends == 2, "buttons held are sent again");
    failNext = true;
    pad.press(juraic::BTN_RIGHT);
    check(sends == 3 && sentButtons == XUSB_GAMEPAD_DPAD_LEFT, "the right was not tried");
    pad.press(juraic::BTN_LEFT);
    check(sends == 4 && sentButtons == XUSB_GAMEPAD_DPAD_LEFT, "after a report not sent, the buttons held before it are not sent");
    pad.press(juraic::BTN_LEFT);
    check(sends == 4, "buttons sent after the failure are sent again");
    // The same buttons by keys (play/realgame_input_win.h) to a window of this test: not in front, no key is sent, and
    // the buttons are told as not taken - none held -, so that the play sends them again; as messages to a window
    // that is gone, the same. (No key goes to any other window.)
    {
        WNDCLASSW c{};
        c.lpfnWndProc = DefWindowProcW, c.hInstance = GetModuleHandleW(nullptr), c.lpszClassName = L"JuraicPadSendKeysTest";
        RegisterClassW(&c);
        const HWND w = CreateWindowExW(0, c.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 10, 10, nullptr, nullptr, c.hInstance, nullptr);
        play::RealPad keys;
        keys.setWay(play::RealPad::WAY_KEYS);
        keys.aim(w, L"");
        check(w != nullptr && GetForegroundWindow() != w && keys.start(error), "the keys are not begun for a window of the test not in front");
        check(!keys.press(juraic::BTN_LEFT), "keys not sent to a window not in front: told as taken");
        check(keys.heldButtons() == 0, "keys not sent to a window not in front: held");
        keys.setWay(play::RealPad::WAY_KEYS_WINDOW);
        keys.aim(w, L"");
        check(keys.start(error), "the keys as messages are not begun");
        DestroyWindow(w);
        check(!keys.press(juraic::BTN_LEFT | juraic::BTN_CW), "messages refused by a window gone: told as taken");
        check(keys.heldButtons() == 0, "messages refused by a window gone: held");
        keys.stop();
    }
    if (failures) return 1;
    std::printf("a report the pad could not send leaves the next buttons to be sent\n");
    return 0;
}
