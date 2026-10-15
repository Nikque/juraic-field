#pragma once
// The mode in which Juraic plays 2P of the real game (play/realgame_win.h), the Windows build: the pad Juraic presses
// the real game's buttons with. An Xbox 360 pad of the driver ViGEmBus (installed by the person; this program carries
// only its client, ViGEmClient, third_party/), plugged in while the mode plays and pulled out when it stops or the
// program ends. This program does not read it as a pad of its own (Pads::skipXInput).
#include "../game/control.h"
#include <windows.h>
#include <ViGEm/Client.h>
#include <string>

namespace play {

class VirtualPad {
public:
    // Which button of the pad turns the pair which way in the real game (as set there): A right and B left, or the
    // other way round.
    bool aTurnsRight = true;

    VirtualPad() = default;
    VirtualPad(const VirtualPad&) = delete;
    VirtualPad& operator=(const VirtualPad&) = delete;
    ~VirtualPad() { stop(); }

    // Plugs the pad in. False, with the reason in `error`, when it cannot be.
    bool start(std::wstring& error) {
        stop();
        client = vigem_alloc();
        if (!client) return error = L"仮想のパッドを作れません（メモリーが足りません）", false;
        VIGEM_ERROR e = vigem_connect(client);
        if (e == VIGEM_ERROR_BUS_NOT_FOUND) return stop(), error = L"ViGEmBus のドライバーが入っていません（入れてから、もう一度選んでください）", false;
        if (!VIGEM_SUCCESS(e)) return stop(), error = L"ViGEmBus のドライバーにつなげません（" + code(e) + L"）", false;
        pad = vigem_target_x360_alloc();
        if (!pad) return stop(), error = L"仮想のパッドを作れません", false;
        e = vigem_target_add(client, pad);
        if (!VIGEM_SUCCESS(e)) return stop(), error = L"仮想のパッドを差し込めません（" + code(e) + L"）", false;
        added = true;
        ULONG index = 0;
        slot = VIGEM_SUCCESS(vigem_target_x360_get_user_index(client, pad, &index)) ? int(index) : -1;
        last = 0xFFFF;
        press(0);
        return true;
    }
    void stop() {
        if (client && pad && added) {
            XUSB_REPORT report{};
            vigem_target_x360_update(client, pad, report); // (nothing held as it goes)
            vigem_target_remove(client, pad);
        }
        if (pad) vigem_target_free(pad);
        if (client) {
            vigem_disconnect(client);
            vigem_free(client);
        }
        client = nullptr, pad = nullptr, added = false, slot = -1;
    }
    bool running() const { return added; }
    // The number XInput gives the pad (-1: not known).
    int xinputSlot() const { return slot; }

    // Holds the buttons of the game (game/control.h Button bits: the directions on the cross, the turns on A and B).
    void press(uint8_t buttons) {
        unsigned b = 0;
        if (buttons & juraic::BTN_LEFT) b |= XUSB_GAMEPAD_DPAD_LEFT;
        if (buttons & juraic::BTN_RIGHT) b |= XUSB_GAMEPAD_DPAD_RIGHT;
        if (buttons & juraic::BTN_DOWN) b |= XUSB_GAMEPAD_DPAD_DOWN;
        if (buttons & juraic::BTN_CW) b |= aTurnsRight ? XUSB_GAMEPAD_A : XUSB_GAMEPAD_B;
        if (buttons & juraic::BTN_CCW) b |= aTurnsRight ? XUSB_GAMEPAD_B : XUSB_GAMEPAD_A;
        hold(USHORT(b));
    }
    // Holds buttons of the pad as they are (XUSB_GAMEPAD_*): for pressing through the menus of the real game by hand.
    void hold(USHORT buttons) {
        if (!added || buttons == last) return;
        XUSB_REPORT report{};
        report.wButtons = buttons;
        if (VIGEM_SUCCESS(vigem_target_x360_update(client, pad, report))) last = buttons;
    }

private:
    PVIGEM_CLIENT client = nullptr;
    PVIGEM_TARGET pad = nullptr;
    bool added = false;
    int slot = -1;
    USHORT last = 0xFFFF;

    static std::wstring code(VIGEM_ERROR e) {
        wchar_t text[16];
        swprintf_s(text, L"%08X", unsigned(e));
        return text;
    }
};

} // namespace play
