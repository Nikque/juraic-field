// The virtual pad of the mode in which Juraic plays 2P of the real game (play/realgame_win.h, the Windows build):
//  1. it stays plugged in while the screen of the mode is open, so that the real game keeps it as the pad of 2P - after
//     a press that picked it for 2P and when the AI stops - and it is pulled out as the mode is left;
//  2. a press asked for (to pick it for 2P in the real game's menus) holds the chosen button PRESS_HOLD frames after
//     PRESS_WAIT frames, as XInput reads the pad, and nothing before or after; it can be called off.
//  0. reading a window (one made here): its pictures are followed on the thread of the play, the record is written,
//     and reading stops with the thread ended (this part plugs in no pad and runs always);
//  3. reading a window that shows the real game's screen of the players with the row of 2P to take (the colours of its
//     points): the pad, plugged in as reading begins, presses A, and the record tells so.
// Parts 1 and 2 plug a pad of the driver ViGEmBus into this machine, which a running game would see: they run only with
// JURAIC_TEST_VIRTUAL_PAD=1 set and the driver installed, and are skipped otherwise (exit code 77).
#define NOMINMAX
#include <windows.h>
#include <xinput.h>
#include "../play/realgame_win.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) std::printf("FAILED: %s\n", what), ++failures;
}

// The window made here, painted another grey each time (a window is captured only when it changes, as the real game
// does every frame), with the points of the real game's screen of the players in their colours when `menuShown`.
static bool menuShown = false;
static LRESULT CALLBACK paintGrey(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    if (m != WM_PAINT) return DefWindowProcW(w, m, wp, lp);
    PAINTSTRUCT ps;
    const HDC dc = BeginPaint(w, &ps);
    const int grey = int(GetTickCount() / 16 % 200) + 30;
    const HBRUSH b = CreateSolidBrush(RGB(grey, grey, grey));
    FillRect(dc, &ps.rcPaint, b);
    DeleteObject(b);
    if (menuShown) {
        RECT inside;
        GetClientRect(w, &inside);
        const play::realgame::MenuLayout menus = play::realgame::measuredMenus();
        for (const auto* part : {&menus.players, &menus.waiting})
            for (const play::realgame::MenuPoint& q : *part) {
                const int x = int(q.x * float(inside.right)), y = int(q.y * float(inside.bottom));
                const RECT r{x - 8, y - 8, x + 9, y + 9};
                const HBRUSH c = CreateSolidBrush(RGB(q.rgb.r, q.rgb.g, q.rgb.b));
                FillRect(dc, &r, c);
                DeleteObject(c);
            }
    }
    EndPaint(w, &ps);
    return 0;
}

// Messages of the window made here for `ms` milliseconds, painting it again every 16.
static void pumpFor(HWND w, int ms) {
    const DWORD until = GetTickCount() + DWORD(ms);
    while (GetTickCount() < until) {
        InvalidateRect(w, nullptr, FALSE);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) TranslateMessage(&msg), DispatchMessageW(&msg);
        Sleep(16);
    }
}

static HWND makeWindow() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = paintGrey;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hbrBackground = HBRUSH(GetStockObject(GRAY_BRUSH));
    wc.lpszClassName = L"JuraicRealgameReadTest";
    RegisterClassW(&wc);
    const HWND w = CreateWindowExW(0, wc.lpszClassName, L"read test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 640, 360, nullptr, nullptr, wc.hInstance, nullptr);
    pumpFor(w, 300);
    return w;
}

static void testReading() {
    const HWND w = makeWindow();
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::filesystem::path folder = std::filesystem::path(temp) / L"juraic-realgame-read-test";
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
    {
        play::RealGameMode m;
        m.padOnReading = false;
        std::wstring error;
        const bool started = m.startReading(w, folder, "a window made by the test", error);
        if (!started) std::wprintf(L"the capture did not start: %ls\n", error.c_str());
        check(started, "a window is not read");
        // (the window found again by its program, as the real game's is found when the screen of the mode opens)
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        check(_wcsicmp(play::windowProgram(w).c_str(), self) == 0 && play::findProgramWindow(self) == w, "a window is not found by its program");
        pumpFor(w, 1500);
        int64_t pictures = 0;
        bool open = false;
        {
            const play::RealGameMode::Hold hold(m.state);
            pictures = m.pictures, open = m.record.isOpen();
        }
        m.pump();
        check(pictures > 10 && m.framesWithout < 30, "the pictures are not followed on the thread of the play");
        check(open, "the record of the reading is not open");
        const std::filesystem::path recorded = m.record.folder();
        m.stopReading();
        check(m.reading == play::RealGameMode::Reading::Off && !m.record.isOpen(), "reading does not stop");
        check(std::filesystem::file_size(recorded / "readings.bin", ignored) > 1000, "the readings are not written to the record");
        std::printf("a window read: %lld pictures followed on the thread of the play in 1.5 s\n", (long long)pictures);
    }
    DestroyWindow(w);
    std::filesystem::remove_all(folder, ignored);
}

int main() {
    testReading();
    const char* wanted = std::getenv("JURAIC_TEST_VIRTUAL_PAD");
    if (!wanted || std::string(wanted) != "1")
        return failures ? (std::printf("%d FAILED\n", failures), 1) : (std::printf("skipped the pad: set JURAIC_TEST_VIRTUAL_PAD=1 to plug in a virtual pad\n"), 77);
    using Mode = play::RealGameMode;
    Mode m;
    std::wstring error;
    if (!m.pressLater(error)) return failures ? 1 : (std::printf("skipped the pad: the driver ViGEmBus cannot be reached\n"), 77);
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

    // 3. The screen of the players read from a window: the pad presses A.
    {
        menuShown = true;
        const HWND w = makeWindow();
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        const std::filesystem::path folder = std::filesystem::path(temp) / L"juraic-realgame-menu-test";
        std::error_code ignored;
        std::filesystem::remove_all(folder, ignored);
        Mode r;
        std::wstring why;
        check(r.startReading(w, folder, "the screen of the players", why) && r.pad.running(), "reading does not begin with the pad plugged in");
        pumpFor(w, 2000);
        int presses = 0;
        play::realgame::Menu seen;
        {
            const Mode::Hold hold(r.state);
            presses = r.presser.presses, seen = r.menu;
        }
        const std::filesystem::path recorded = r.record.folder();
        r.leave();
        std::ifstream events(recorded / "events.txt", std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(events)), std::istreambuf_iterator<char>());
        events.close();
        check(seen == play::realgame::Menu::PlayersWaiting && presses >= 1 && text.find("the pad of 2P presses A") != std::string::npos,
              "on the screen of the players with the row of 2P to take, the pad does not press A");
        std::printf("the screen of the players read from a window: the pad pressed A %d times in 2 s\n", presses);
        DestroyWindow(w);
        menuShown = false;
        std::filesystem::remove_all(folder, ignored);
    }

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
