// The keys the buttons of the real game go by with no driver (play/realgame_keys_win.h): the keys taken from the text of
// the real game's settings file, what a key is sent as, keys never sent while the window they are for is not in front,
// and the messages of a key as a window of this test gets them. (No key is sent to any window but this test's own.)
#include "../play/realgame_keys_win.h"
#include <cstdio>
#include <vector>

static int failures = 0, checks = 0;
static void check(bool ok, const char* what) {
    ++checks;
    if (!ok) ++failures, std::printf("FAILED: %s\n", what);
}

struct Got {
    UINT message;
    WPARAM key;
    LPARAM bits;
};
static std::vector<Got> got;
static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_KEYDOWN || m == WM_KEYUP) return got.push_back({m, wp, lp}), 0;
    return DefWindowProcW(w, m, wp, lp);
}
static void pump() {
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
}

int main() {
    using play::GameKeys;
    using play::KeySender;
    // The rows of a settings file (the first key of each); a row that is not there keeps its key.
    const std::string text = "<InputConfig Mode=\"0\">\r\n    <Left B0=\"30\" B1=\"75\" B2=\"254\"/>\r\n    <Right B0=\"32\" B1=\"77\" B2=\"255\"/>\r\n"
                             "    <HardDrop B0=\"57\" B1=\"0\" B2=\"251\"/>\r\n    <SoftDrop B0=\"31\" B1=\"47\" B2=\"248\"/>\r\n    <RotateRight B0=\"37\" B1=\"200\" B2=\"252\"/>\r\n"
                             "    <RotateLeft B0=\"36\" B1=\"29\" B2=\"0\"/>\r\n</InputConfig>\r\n";
    GameKeys keys;
    check(keys.left == 203 && keys.right == 205 && keys.down == 208 && keys.turnRight == 45 && keys.turnLeft == 44 && keys.pause == 1, "the keys the real game begins with");
    check(keys.take(text) == 5, "five rows of the six are in the text");
    check(keys.left == 30 && keys.right == 32 && keys.down == 31 && keys.turnRight == 37 && keys.turnLeft == 36, "the first key of each row is taken");
    check(keys.pause == 1, "the row that is not there keeps its key");
    check(GameKeys::keyOf("<Left B1=\"75\"/><Right B0=\"77\"/>", "Left") == 0, "a row without a first key gives none (not the next row's)");
    check(GameKeys::keyOf("<Left B0=\"0\"/>", "Left") == 0 && GameKeys::keyOf("<Left B0=\"999\"/>", "Left") == 0, "no key and a number that is none give none");
    check(GameKeys{}.read(L"Z:\\no\\such\\folder\\config.ini") == 0, "a file that is not there gives no rows");

    // A key: its scan code, the extended set, the virtual key.
    check(KeySender::scanOf(203) == 0x4B && KeySender::extendedOf(203) && KeySender::virtualOf(203) == VK_LEFT, "203 is the arrow to the left");
    check(KeySender::scanOf(205) == 0x4D && KeySender::virtualOf(205) == VK_RIGHT && KeySender::virtualOf(208) == VK_DOWN, "205 and 208 are the arrows right and down");
    check(KeySender::scanOf(44) == 44 && !KeySender::extendedOf(44) && KeySender::virtualOf(44) == 'Z' && KeySender::virtualOf(45) == 'X', "44 and 45 are Z and X");
    check(KeySender::virtualOf(GameKeys::MENU_OK) == VK_RETURN && KeySender::virtualOf(GameKeys::MENU_BACK) == VK_ESCAPE, "the keys of the menus are Enter and Esc");
    check(KeySender::messageOf(203, true) == (LPARAM(1) | LPARAM(0x4B) << 16 | LPARAM(1) << 24), "the message of the arrow going down");
    check(KeySender::messageOf(44, false) == (LPARAM(1) | LPARAM(44) << 16 | LPARAM(0xC0000000)), "the message of Z going up");

    // A window of this test, not in front.
    WNDCLASSW c{};
    c.lpfnWndProc = proc, c.hInstance = GetModuleHandleW(nullptr), c.lpszClassName = L"JuraicKeysTest";
    RegisterClassW(&c);
    const HWND w = CreateWindowExW(0, c.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 10, 10, nullptr, nullptr, c.hInstance, nullptr);
    check(w != nullptr, "a window of the test");
    std::wstring error;
    KeySender none;
    check(!none.start(nullptr, L"", KeySender::SCAN, error) && !error.empty() && !none.running(), "no window: the keys are not begun, with a reason");

    // By scan code and by virtual key code: nothing is sent while the window is not the one in front.
    for (const KeySender::How how : {KeySender::SCAN, KeySender::VIRTUAL}) {
        KeySender s;
        check(s.start(w, L"", how, error) && s.running(), "the keys are begun for the window");
        check(GetForegroundWindow() != w, "the window of the test is not in front");
        s.hold({44, 208});
        s.hold({208});
        s.hold({});
        check(s.sent.load() == 0 && s.refused.load() == 0, "not in front: no key is sent");
        check(s.away.load() == 2, "not in front: the two times keys were asked for are counted");
        const INPUT down = s.inputOf(203, true), up = s.inputOf(44, false);
        check(down.type == INPUT_KEYBOARD && down.ki.wScan == 0x4B && (down.ki.dwFlags & KEYEVENTF_EXTENDEDKEY) && !(down.ki.dwFlags & KEYEVENTF_KEYUP), "the arrow going down as it is sent");
        check((up.ki.dwFlags & KEYEVENTF_KEYUP) && !(up.ki.dwFlags & KEYEVENTF_EXTENDEDKEY) && up.ki.wScan == 44, "Z going up as it is sent");
        check(how == KeySender::SCAN ? (down.ki.dwFlags & KEYEVENTF_SCANCODE) && down.ki.wVk == 0 : !(down.ki.dwFlags & KEYEVENTF_SCANCODE) && down.ki.wVk == VK_LEFT,
              "by scan code the key has no virtual key, by virtual key code no mark of a scan code");
        s.stop();
        check(!s.running(), "the keys are stopped");
    }

    // As messages: the window gets them wherever it is, a key once as it goes down and once as it goes up.
    {
        KeySender s;
        check(s.start(w, L"", KeySender::WINDOW, error), "the keys as messages are begun");
        got.clear();
        s.hold({44, 203});
        s.hold({44, 203});
        pump();
        check(got.size() == 2 && got[0].message == WM_KEYDOWN && got[0].key == 'Z' && got[1].message == WM_KEYDOWN && got[1].key == VK_LEFT, "two keys go down once each");
        check(got.size() == 2 && got[1].bits == KeySender::messageOf(203, true), "with the bits of the key");
        got.clear();
        s.hold({203});
        pump();
        check(got.size() == 1 && got[0].message == WM_KEYUP && got[0].key == 'Z', "the key no longer held goes up");
        got.clear();
        s.stop();
        pump();
        check(got.size() == 1 && got[0].message == WM_KEYUP && got[0].key == VK_LEFT, "stopped: what was held goes up");
        check(s.sent.load() == 4 && s.refused.load() == 0 && s.away.load() == 0, "four messages were sent");
    }
    DestroyWindow(w);
    std::printf("the keys of the real game: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
