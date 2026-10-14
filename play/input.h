#pragma once
// Who plays with what: two keyboard layouts and any number of controllers,
// each action bound to a key or a controller button that the player can change.
#include "../game/control.h"
#include "keyboard.h"
#include "pad.h"
#include <string>

namespace play {

// The actions a player has, in the order they are bound.
// Up to ACT_NEW: the game (the last three in toko only). After that: the
// menus, on controllers only (the keyboard uses Enter and Esc there). The two
// groups are bound apart from each other, so a change of the game's buttons
// never takes the menus away; inside a group no two actions share a button.
enum Action { ACT_LEFT, ACT_RIGHT, ACT_DOWN, ACT_CCW, ACT_CW, ACT_START, ACT_UNDO, ACT_RETRY, ACT_NEW, ACT_MENU_OK, ACT_MENU_BACK, ACT_COUNT };
constexpr int GAME_ACTIONS = ACT_MENU_OK;
inline const wchar_t* const actionNames[ACT_COUNT] = {L"左移動", L"右移動", L"高速落下", L"左回転", L"右回転", L"一時停止",
                                                       L"1 手戻す（とこ）", L"同じツモでやり直す（とこ）", L"新しいツモ（とこ）",
                                                       L"メニューの決定", L"メニューの戻る"};
// The actions that may not share a button with `action`: first and one past the last.
constexpr int groupBegin(int action) { return action < GAME_ACTIONS ? 0 : GAME_ACTIONS; }
constexpr int groupEnd(int action) { return action < GAME_ACTIONS ? GAME_ACTIONS : ACT_COUNT; }

// Binding tables: two keyboard layouts (virtual key codes), Xbox pads and
// other pads (bit numbers of Pads::raw).
enum Layout { LAYOUT_KEYS1, LAYOUT_KEYS2, LAYOUT_XBOX, LAYOUT_PAD, LAYOUT_COUNT };
inline const wchar_t* const layoutNames[LAYOUT_COUNT] = {L"キーボード 1", L"キーボード 2", L"Xbox 系コントローラー", L"その他のコントローラー"};
inline const char* const layoutKeys[LAYOUT_COUNT] = {"keys1", "keys2", "xbox", "pad"};

struct InputConfig {
    // Controllers: rotate left on the lower of the two main buttons, rotate
    // right (and "OK" in the menus) on the right one.
    //  - Xbox: A and B.
    //  - other pads: buttons 2 and 3 of the system's numbering, which are B and
    //    A on pads numbered Y B A X L R ZL ZR - + (and cross and circle on
    //    PlayStation pads). A pad numbered otherwise needs the binding screen.
    static constexpr int DEFAULTS[LAYOUT_COUNT][ACT_COUNT] = {
        {VK_LEFT, VK_RIGHT, VK_DOWN, 'Z', 'X', 'P', VK_BACK, 'R', 'N', VK_RETURN, VK_ESCAPE},
        {'A', 'D', 'S', 'F', 'G', 'T', '1', '2', '3', VK_RETURN, VK_ESCAPE},
        {PAD_LEFT, PAD_RIGHT, PAD_DOWN, 0, 1, 7, 4, 3, 6, 1, 0}, // A, B, Start, LB, Y, Back; menus: B, A
        {PAD_LEFT, PAD_RIGHT, PAD_DOWN, 1, 2, 9, 4, 3, 8, 2, 1}, // B, A, +, L, X, -; menus: A, B
    };
    int bind[LAYOUT_COUNT][ACT_COUNT];
    InputConfig() {
        for (int l = 0; l < LAYOUT_COUNT; ++l) resetLayout(l);
    }
    void resetLayout(int layout) {
        for (int a = 0; a < ACT_COUNT; ++a) bind[layout][a] = DEFAULTS[layout][a];
    }
    // Binds an action. An action of the same group that had this code gets the
    // code the action had before (the two change places).
    void set(int layout, int action, int code) {
        for (int a = groupBegin(action); a < groupEnd(action); ++a)
            if (a != action && bind[layout][a] == code) bind[layout][a] = bind[layout][action];
        bind[layout][action] = code;
    }
    // Do two actions of one group share a code (a file written by an older version)?
    bool clashes(int layout) const {
        for (int a = 0; a < ACT_COUNT; ++a)
            for (int b = groupBegin(a); b < a; ++b)
                if (bind[layout][a] == bind[layout][b]) return true;
        return false;
    }
    // Device of each player: 0 keyboard 1, 1 keyboard 2, 2 + n = controller n. -1 = choose automatically.
    int device[2] = {-1, -1};
};

inline std::wstring deviceName(int device) {
    return device < 2 ? std::wstring(layoutNames[device]) : L"コントローラー " + std::to_wstring(device - 1);
}
inline int layoutOf(int device, const Pads& pads) {
    return device < 2 ? device : pads.isXbox(device - 2) ? LAYOUT_XBOX : LAYOUT_PAD;
}

// Name of a key or controller button for the binding screen.
inline std::wstring codeName(int layout, int code) {
    if (layout >= LAYOUT_XBOX) {
        if (code == PAD_UP) return L"上";
        if (code == PAD_DOWN) return L"下";
        if (code == PAD_LEFT) return L"左";
        if (code == PAD_RIGHT) return L"右";
        static const wchar_t* const xbox[10] = {L"A", L"B", L"X", L"Y", L"LB", L"RB", L"Back", L"Start", L"L スティック", L"R スティック"};
        return layout == LAYOUT_XBOX && code < 10 ? std::wstring(xbox[code]) : L"ボタン " + std::to_wstring(code + 1);
    }
    switch (code) {
    case VK_LEFT: return L"←";
    case VK_RIGHT: return L"→";
    case VK_UP: return L"↑";
    case VK_DOWN: return L"↓";
    case VK_SPACE: return L"Space";
    case VK_RETURN: return L"Enter";
    case VK_SHIFT: return L"Shift";
    case VK_CONTROL: return L"Ctrl";
    }
#ifdef _WIN32
    wchar_t name[32] = L"";
    if (GetKeyNameTextW(LONG(MapVirtualKeyW(UINT(code), MAPVK_VK_TO_VSC) << 16), name, 32) > 0) return name;
#else
    if (const std::wstring name = keyName(code); !name.empty()) return name;
#endif
    return L"キー " + std::to_wstring(code);
}

// The actions held on a device in this frame, as a bit per Action.
inline unsigned actionsOf(int device, const InputConfig& config, const Pads& pads, bool focused) {
    unsigned held = 0;
    const int* bind = config.bind[layoutOf(device, pads)];
    if (device < 2) {
        if (!focused) return 0;
        for (int a = 0; a < ACT_COUNT; ++a)
            if (keyDown(bind[a])) held |= 1u << a;
    } else {
        const uint64_t raw = pads.raw(device - 2);
        for (int a = 0; a < ACT_COUNT; ++a)
            if (raw >> bind[a] & 1) held |= 1u << a;
    }
    return held;
}
// Game buttons (control.h) from held actions.
inline uint8_t gameButtons(unsigned held) { return uint8_t(held & 31u); } // the first five actions are BTN_LEFT .. BTN_CW in order

// Is "up" held on a device: the quick drop, when that setting is on. It has no entry on the
// binding screen: the up key of the two keyboard layouts (↑ and W) and up on a controller.
inline bool upHeld(int device, const Pads& pads, bool focused) {
    if (device < 2) return focused && keyDown(device == 0 ? VK_UP : 'W');
    return (pads.raw(device - 2) >> PAD_UP & 1) != 0;
}

using juraic::Lever; // (the setting "diagonal" off: the directions of a person go through it, control.h)

// The buttons of a person held as a game begins do not count until they have been let go: on a controller the
// button that chose the game in the menu is a rotation, and the game takes a button held in its first frame for
// one pressed then (with pre-input the first pair would turn).
struct HeldAtStart {
    uint8_t waiting = 0; // buttons held over from the beginning, not let go yet
    void begin() { waiting = 0xFF; } // (whatever is held in the next frame is held over)
    uint8_t operator()(uint8_t buttons) {
        waiting &= buttons;
        return uint8_t(buttons & ~waiting);
    }
};

// Devices for the two players when none was chosen: controllers first.
inline void chooseDevices(const InputConfig& config, const Pads& pads, int players, int device[2]) {
    int nextPad = 0;
    bool keys1Used = false;
    for (int i = 0; i < players; ++i) {
        device[i] = config.device[i];
        if (device[i] >= 2 && device[i] - 2 >= pads.count()) device[i] = -1; // that controller is gone
        if (device[i] >= 0) continue;
        if (nextPad < pads.count()) device[i] = 2 + nextPad++;
        else device[i] = keys1Used ? 1 : 0, keys1Used = true;
    }
    if (players == 2 && device[0] == device[1]) device[1] = device[0] == 0 ? 1 : 0;
}

} // namespace play
