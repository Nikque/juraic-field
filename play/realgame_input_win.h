#pragma once
// The mode in which Juraic plays 2P of the real game (play/realgame_win.h), the Windows build: the way the buttons go
// to the real game. One of: the virtual pad of the driver ViGEmBus (play/realgame_pad_win.h), or the keys of the
// keyboard with no driver (play/realgame_keys_win.h) - by scan code, by virtual key code, or as messages of the real
// game's window. Whoever presses the buttons sees one pad, whichever way they go.
#include "realgame_keys_win.h"
#include "realgame_pad_win.h"
#include <atomic>
#include <string>
#include <vector>

namespace play {

class RealPad {
public:
    enum Way { WAY_PAD, WAY_KEYS, WAY_KEYS_VIRTUAL, WAY_KEYS_WINDOW, WAYS };
    static constexpr const wchar_t* WAY_NAMES[WAYS] = {L"仮想パッド（ViGEmBus・非推奨）", L"キーボード入力（スキャンコード）", L"キーボード入力（仮想キーコード）", L"キーボード入力（ウィンドウメッセージ）"};
    static constexpr const char* WAY_RECORD_NAMES[WAYS] = {"the virtual pad", "keys by scan code", "keys by virtual key code", "keys as messages of the window"};

    VirtualPad pad;
    KeySender keys;

    RealPad() = default;
    RealPad(const RealPad&) = delete;
    RealPad& operator=(const RealPad&) = delete;

    int way() const { return way_; }
    bool byKeys() const { return way_ != WAY_PAD; }
    // Another way: what the way before held is let go (a pad pulled out). The buttons go this way once it is started.
    void setWay(int to) {
        stop();
        way_ = to < 0 || to >= WAYS ? int(WAY_PAD) : to;
    }
    // The real game's window and its program: where keys go. Keys aimed at another window are stopped.
    void aim(HWND w, std::wstring programOfIt) {
        if (keys.running() && w != window_) keys.stop(), last_ = 0;
        window_ = w, program_ = std::move(programOfIt);
    }
    // Plugs the pad in, or makes the keys ready. False, with the reason in `error`, when it cannot be.
    bool start(std::wstring& error) {
        if (!byKeys()) return pad.start(error);
        last_ = 0;
        return keys.start(window_, program_, way_ == WAY_KEYS ? KeySender::SCAN : way_ == WAY_KEYS_VIRTUAL ? KeySender::VIRTUAL : KeySender::WINDOW, error);
    }
    void stop() {
        pad.stop();
        keys.stop();
        last_ = 0;
    }
    bool running() const { return byKeys() ? keys.running() : pad.running(); }
    // The buttons held now, as those of the pad (XUSB_GAMEPAD_*) whichever way they go.
    USHORT heldButtons() const { return byKeys() ? last_.load() : pad.heldButtons(); }
    // The number XInput gives the pad (-1: not known, or the buttons go by keys).
    int xinputSlot() const { return byKeys() ? -1 : pad.xinputSlot(); }

    // Holds the buttons of the game (game/control.h Button bits), as VirtualPad::press: by keys, the turn the pad sends
    // on its B is the real game's key for a turn to the right, the one on its A the key for a turn to the left (so the
    // real game has the two buttons of the pad).
    // false: the virtual pad did not take them; by keys, they were not sent (the real game's window not in front) or the
    // system did not take them all.
    bool press(uint8_t buttons) {
        if (!byKeys()) return pad.press(buttons);
        const GameKeys& k = keys.keys;
        std::vector<int> held;
        unsigned b = 0;
        if (buttons & juraic::BTN_LEFT) held.push_back(k.left), b |= XUSB_GAMEPAD_DPAD_LEFT;
        if (buttons & juraic::BTN_RIGHT) held.push_back(k.right), b |= XUSB_GAMEPAD_DPAD_RIGHT;
        if (buttons & juraic::BTN_DOWN) held.push_back(k.down), b |= XUSB_GAMEPAD_DPAD_DOWN;
        // (BTN_CW on the pad's A, BTN_CCW on its B, as VirtualPad::press)
        if (buttons & juraic::BTN_CW) held.push_back(k.turnLeft), b |= XUSB_GAMEPAD_A;
        if (buttons & juraic::BTN_CCW) held.push_back(k.turnRight), b |= XUSB_GAMEPAD_B;
        // (keys not sent - the real game's window not in front - or not all taken by the system: the buttons held are
        //  those of the keys that are down, and the next buttons asked for are sent again)
        const bool took = keys.hold(held);
        last_ = took ? USHORT(b) : heldOf(false);
        return took;
    }
    // Holds buttons of the pad as they are (XUSB_GAMEPAD_*), for the menus of the real game: by keys, A is the key
    // that says yes in the menus, B the one that goes back, Start the key of the pause.
    bool hold(USHORT buttons) {
        if (!byKeys()) return pad.hold(buttons);
        const GameKeys& k = keys.keys;
        std::vector<int> held;
        if (buttons & XUSB_GAMEPAD_DPAD_LEFT) held.push_back(k.left);
        if (buttons & XUSB_GAMEPAD_DPAD_RIGHT) held.push_back(k.right);
        if (buttons & XUSB_GAMEPAD_DPAD_DOWN) held.push_back(k.down);
        if (buttons & XUSB_GAMEPAD_A) held.push_back(GameKeys::MENU_OK);
        if (buttons & XUSB_GAMEPAD_B) held.push_back(GameKeys::MENU_BACK);
        if (buttons & XUSB_GAMEPAD_START) held.push_back(k.pause);
        const bool took = keys.hold(held);
        last_ = took ? buttons : heldOf(true);
        return took;
    }

private:
    // The buttons of the pad whose keys are down now: those of the play, or those of the menus.
    USHORT heldOf(bool menus) const {
        const GameKeys& k = keys.keys;
        const std::array<bool, 256> down = keys.held();
        const auto is = [&down](int key) { return key > 0 && key < 256 && down[size_t(key)]; };
        unsigned b = 0;
        if (is(k.left)) b |= XUSB_GAMEPAD_DPAD_LEFT;
        if (is(k.right)) b |= XUSB_GAMEPAD_DPAD_RIGHT;
        if (is(k.down)) b |= XUSB_GAMEPAD_DPAD_DOWN;
        if (is(menus ? GameKeys::MENU_OK : k.turnLeft)) b |= XUSB_GAMEPAD_A;
        if (is(menus ? GameKeys::MENU_BACK : k.turnRight)) b |= XUSB_GAMEPAD_B;
        if (menus && is(k.pause)) b |= XUSB_GAMEPAD_START;
        return USHORT(b);
    }
    int way_ = WAY_KEYS;
    HWND window_ = nullptr;
    std::wstring program_;
    std::atomic<USHORT> last_{0};
};

} // namespace play
