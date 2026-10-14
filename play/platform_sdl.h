#pragma once
// The window of the game outside Windows, with SDL: opening it, showing a frame drawn by play/raster.h, the keys
// (by the virtual key codes of play/keys.h), the clipboard, choosing a file, the folder of the program. And the
// two formatting functions of the C library of Windows the game uses, for the C library of the system.
#include <SDL3/SDL.h>
#include "keyboard.h"
#include "raster.h"
#include "settings.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace play {

template <size_t N, class... A> int swprintf_s(wchar_t (&out)[N], const wchar_t* format, A... a) { return std::swprintf(out, N, format, a...); }
template <class... A> int swscanf_s(const wchar_t* text, const wchar_t* format, A... a) { return std::swscanf(text, format, a...); }

struct Window {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    int textureW = 0, textureH = 0;
    bool typing = false; // the characters typed are asked for (the keyboard of the screen is shown on Android)
};

// The folder the program is in.
inline std::filesystem::path programFolder() {
#ifdef __ANDROID__
    if (const char* inside = SDL_GetAndroidInternalStoragePath()) return std::filesystem::path(inside);
    return std::filesystem::current_path();
#elif defined(__APPLE__)
    if (const char* base = SDL_GetBasePath()) return std::filesystem::path(base);
    return std::filesystem::current_path();
#else
    std::error_code failed;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", failed);
    return failed ? std::filesystem::current_path() : self.parent_path();
#endif
}
// The folder of the files that come with the game, and that of the files the game writes and of one's own pictures
// and music: both the folder of the program, except in an application bundle of macOS, whose Resources cannot be
// written to (nor be next to the folders a person puts files in): there ~/Library/Application Support/JuraicField,
// with the folders for one's own pictures and music made when they are not there.
inline void programFolders(std::filesystem::path& data, std::filesystem::path& user) {
    data = user = programFolder();
#ifdef __ANDROID__
    // Android: the files that come with the game are assets of the APK, below "game" with their list game/files.txt; they are
    // copied into the own folder of the app on the first start (and when the list is another: a new version). The
    // settings, the replays and one's own pictures and music are in the folder of the app that a file manager or a PC
    // on USB can reach (Android/data/<the app>/files), else in the own folder.
    data = programFolder() / "game";
    std::error_code ignored;
    size_t listSize = 0;
    if (void* list = SDL_LoadFile("game/files.txt", &listSize)) {
        const std::string wanted(static_cast<const char*>(list), listSize);
        SDL_free(list);
        std::ifstream had(data / "files.txt", std::ios::binary);
        const std::string have((std::istreambuf_iterator<char>(had)), std::istreambuf_iterator<char>());
        if (have != wanted) {
            std::istringstream lines(wanted);
            for (std::string line; std::getline(lines, line);) {
                const size_t space = line.rfind(' ');
                if (line.rfind("version ", 0) == 0 || space == std::string::npos) continue;
                const std::string name = line.substr(0, space);
                size_t size = 0;
                void* bytes = SDL_LoadFile(("game/" + name).c_str(), &size);
                if (!bytes) continue;
                std::filesystem::create_directories((data / name).parent_path(), ignored);
                std::ofstream(data / name, std::ios::binary).write(static_cast<const char*>(bytes), std::streamsize(size));
                SDL_free(bytes);
            }
            std::ofstream(data / "files.txt", std::ios::binary) << wanted;
        }
    }
    const char* outside = (SDL_GetAndroidExternalStorageState() & SDL_ANDROID_EXTERNAL_STORAGE_WRITE) ? SDL_GetAndroidExternalStoragePath() : nullptr;
    user = outside ? std::filesystem::path(outside) : programFolder() / "user";
    for (const char* folder : {"background/1p", "background/2p", "background/other", "bgm/versus", "bgm/toko", "bgm/menu", "replays"})
        std::filesystem::create_directories(user / folder, ignored);
#endif
#ifdef __APPLE__
    if (data.filename().empty() ? data.parent_path().filename() != "Resources" : data.filename() != "Resources") return;
    char* pref = SDL_GetPrefPath("", "JuraicField");
    if (!pref) return;
    user = std::filesystem::path(pref);
    SDL_free(pref);
    std::error_code ignored;
    for (const char* folder : {"background/1p", "background/2p", "background/other", "bgm/versus", "bgm/toko", "bgm/menu", "replays"})
        std::filesystem::create_directories(user / folder, ignored);
#endif
}

inline bool openWindow(Window& w, const char* title, int width, int height) {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
#ifdef __ANDROID__
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) return false;
#ifdef __ANDROID__
    w.window = SDL_CreateWindow(title, width, height, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
#else
    w.window = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
#endif
    if (!w.window) return false;
    w.renderer = SDL_CreateRenderer(w.window, nullptr);
    if (!w.renderer) return false;
    SDL_SetRenderVSync(w.renderer, 1); // (drawing waits for the display)
#ifndef __ANDROID__
    SDL_StartTextInput(w.window); // (the characters typed: the name and the address of a match over a network)
    w.typing = true;
#endif
    return true;
}
// The characters typed are wanted (a text of the screen before a match over a network is being written) or not. On
// Android this shows and hides the keyboard of the screen; elsewhere they are always taken.
inline void wantTyping(Window& w, bool on) {
#ifdef __ANDROID__
    if (on == w.typing || !w.window) return;
    if (on) SDL_StartTextInput(w.window);
    else SDL_StopTextInput(w.window);
    w.typing = on;
#else
    (void)w, (void)on;
#endif
}
// (SDL itself is not shut down here: the sound and the controllers of the game are let go of when the program ends,
//  after this, and SDL_Quit would have freed their streams before them)
inline void closeWindow(Window& w) {
    if (w.texture) SDL_DestroyTexture(w.texture);
    if (w.renderer) SDL_DestroyRenderer(w.renderer);
    if (w.window) SDL_DestroyWindow(w.window);
    w = Window{};
}
// The size of the inside of the window in pixels of the display.
inline void windowPixels(const Window& w, int& width, int& height) {
    width = height = 0;
    if (w.window) SDL_GetWindowSizeInPixels(w.window, &width, &height);
    if (width <= 0 || height <= 0) width = int(CANVAS_W), height = int(CANVAS_H);
}
// A point of the window (as SDL tells the mouse) on the canvas, which the frame has in its middle, as large as fits.
inline bool canvasPoint(const Window& w, float x, float y, float& cx, float& cy) {
    int ww = 0, wh = 0, pw = 0, ph = 0;
    SDL_GetWindowSize(w.window, &ww, &wh);
    windowPixels(w, pw, ph);
    if (ww <= 0 || wh <= 0) return false;
    const float px = x * float(pw) / float(ww), py = y * float(ph) / float(wh);
    const float scale = std::min(float(pw) / CANVAS_W, float(ph) / CANVAS_H);
    if (scale <= 0) return false;
    cx = (px - (float(pw) - CANVAS_W * scale) / 2) / scale, cy = (py - (float(ph) - CANVAS_H * scale) / 2) / scale;
    return true;
}
// The frame drawn into r, on the window.
inline void showFrame(Window& w, const Raster& r) {
    if (!w.texture || w.textureW != r.width || w.textureH != r.height) {
        if (w.texture) SDL_DestroyTexture(w.texture);
        w.texture = SDL_CreateTexture(w.renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, r.width, r.height);
        w.textureW = r.width, w.textureH = r.height;
        if (!w.texture) return;
        SDL_SetTextureBlendMode(w.texture, SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(w.texture, SDL_SCALEMODE_NEAREST); // (drawn at the size of the window: one pixel for one)
    }
    SDL_UpdateTexture(w.texture, nullptr, r.px.data(), r.width * 4);
    SDL_RenderTexture(w.renderer, w.texture, nullptr, nullptr);
    SDL_RenderPresent(w.renderer);
}
// What the system has to tell, taken in where nothing waits for it (the self test).
inline void pumpEvents() { SDL_PumpEvents(); }

// The virtual key code of a key of the keyboard (0: none the game knows).
inline int virtualKey(SDL_Scancode s) {
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) return 'A' + (s - SDL_SCANCODE_A);
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) return '1' + (s - SDL_SCANCODE_1);
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F12) return VK_F1 + (s - SDL_SCANCODE_F1);
    if (s >= SDL_SCANCODE_KP_1 && s <= SDL_SCANCODE_KP_9) return VK_NUMPAD0 + 1 + (s - SDL_SCANCODE_KP_1);
    switch (s) {
    case SDL_SCANCODE_0: return '0';
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: return VK_RETURN;
    case SDL_SCANCODE_ESCAPE: return VK_ESCAPE;
    case SDL_SCANCODE_BACKSPACE: return VK_BACK;
    case SDL_SCANCODE_TAB: return VK_TAB;
    case SDL_SCANCODE_SPACE: return VK_SPACE;
    case SDL_SCANCODE_MINUS: return VK_OEM_MINUS;
    case SDL_SCANCODE_EQUALS: return VK_OEM_PLUS;
    case SDL_SCANCODE_LEFTBRACKET: return VK_OEM_4;
    case SDL_SCANCODE_RIGHTBRACKET: return VK_OEM_6;
    case SDL_SCANCODE_BACKSLASH:
    case SDL_SCANCODE_NONUSHASH:
    case SDL_SCANCODE_INTERNATIONAL3: return VK_OEM_5;
    case SDL_SCANCODE_SEMICOLON: return VK_OEM_1;
    case SDL_SCANCODE_APOSTROPHE: return VK_OEM_7;
    case SDL_SCANCODE_GRAVE: return VK_OEM_3;
    case SDL_SCANCODE_COMMA: return VK_OEM_COMMA;
    case SDL_SCANCODE_PERIOD: return VK_OEM_PERIOD;
    case SDL_SCANCODE_SLASH: return VK_OEM_2;
    case SDL_SCANCODE_NONUSBACKSLASH:
    case SDL_SCANCODE_INTERNATIONAL1: return VK_OEM_102;
    case SDL_SCANCODE_CAPSLOCK: return VK_CAPITAL;
    case SDL_SCANCODE_SCROLLLOCK: return VK_SCROLL;
    case SDL_SCANCODE_PAUSE: return VK_PAUSE;
    case SDL_SCANCODE_INSERT: return VK_INSERT;
    case SDL_SCANCODE_HOME: return VK_HOME;
    case SDL_SCANCODE_PAGEUP: return VK_PRIOR;
    case SDL_SCANCODE_DELETE: return VK_DELETE;
    case SDL_SCANCODE_END: return VK_END;
    case SDL_SCANCODE_PAGEDOWN: return VK_NEXT;
    case SDL_SCANCODE_RIGHT: return VK_RIGHT;
    case SDL_SCANCODE_LEFT: return VK_LEFT;
    case SDL_SCANCODE_DOWN: return VK_DOWN;
    case SDL_SCANCODE_UP: return VK_UP;
    case SDL_SCANCODE_NUMLOCKCLEAR: return VK_NUMLOCK;
    case SDL_SCANCODE_KP_DIVIDE: return VK_DIVIDE;
    case SDL_SCANCODE_KP_MULTIPLY: return VK_MULTIPLY;
    case SDL_SCANCODE_KP_MINUS: return VK_SUBTRACT;
    case SDL_SCANCODE_KP_PLUS: return VK_ADD;
    case SDL_SCANCODE_KP_0: return VK_NUMPAD0;
    case SDL_SCANCODE_KP_PERIOD: return VK_DECIMAL;
    case SDL_SCANCODE_LCTRL: return VK_LCONTROL;
    case SDL_SCANCODE_RCTRL: return VK_RCONTROL;
    case SDL_SCANCODE_LSHIFT: return VK_LSHIFT;
    case SDL_SCANCODE_RSHIFT: return VK_RSHIFT;
#ifdef __ANDROID__
    case SDL_SCANCODE_AC_BACK: return VK_ESCAPE; // (the back button of Android)
#endif
    case SDL_SCANCODE_LALT: return VK_LMENU;
    case SDL_SCANCODE_RALT: return VK_RMENU;
#ifdef __APPLE__
    case SDL_SCANCODE_LGUI: return VK_LCONTROL; // (Command does there what Ctrl does elsewhere: Command+V pastes)
    case SDL_SCANCODE_RGUI: return VK_RCONTROL;
#endif
    default: return 0;
    }
}
// A key went down or up. Shift, Ctrl and Alt are down while either of the two is (as Windows has them).
inline void noteKey(SDL_Scancode s, bool down) {
    if (const int vk = virtualKey(s)) keysDown[vk] = down;
    keysDown[VK_SHIFT] = keysDown[VK_LSHIFT] || keysDown[VK_RSHIFT];
    keysDown[VK_CONTROL] = keysDown[VK_LCONTROL] || keysDown[VK_RCONTROL];
    keysDown[VK_MENU] = keysDown[VK_LMENU] || keysDown[VK_RMENU];
}
// Every key up (the window has lost the keyboard: what is let go of then is not heard).
inline void forgetAllKeys() {
    for (bool& k : keysDown) k = false;
}

inline std::wstring clipboardText() {
    std::wstring s;
    if (char* text = SDL_GetClipboardText()) {
        s = fromUtf8(text);
        SDL_free(text);
    }
    return s;
}
inline bool setClipboardText(const std::wstring& s) { return SDL_SetClipboardText(toUtf8(s).c_str()); }

// A file chosen in the dialog of the system, as Windows takes its filter ("name\0*.ending\0...\0"), starting in
// `folder`. False when none was chosen (or the system has no such dialog).
inline bool chooseFile(Window& w, const wchar_t* filter, const std::filesystem::path& folder, std::filesystem::path& chosen) {
    std::vector<std::string> names, patterns;
    for (const wchar_t* p = filter; p && *p;) {
        const std::wstring name = p;
        p += name.size() + 1;
        if (!*p) break;
        std::wstring pattern = p;
        p += pattern.size() + 1;
        std::string list; // (SDL: "jfreplay;txt", or "*" for any file)
        for (size_t at = 0; at < pattern.size();) {
            size_t end = pattern.find(L';', at);
            if (end == std::wstring::npos) end = pattern.size();
            std::wstring one = pattern.substr(at, end - at);
            if (one.rfind(L"*.", 0) == 0) one = one.substr(2);
            if (!list.empty()) list += ';';
            list += one == L"*" ? std::string("*") : toUtf8(one);
            at = end + 1;
        }
        names.push_back(toUtf8(name)), patterns.push_back(list);
    }
    std::vector<SDL_DialogFileFilter> filters;
    for (size_t i = 0; i < names.size(); ++i) filters.push_back(SDL_DialogFileFilter{names[i].c_str(), patterns[i].c_str()});
    struct Answer {
        std::atomic<bool> done{false};
        std::string file;
    } answer;
    const auto told = [](void* data, const char* const* files, int) {
        Answer& a = *static_cast<Answer*>(data);
        if (files && files[0]) a.file = files[0];
        a.done = true;
    };
    const std::string start = folder.string();
    SDL_ShowOpenFileDialog(told, &answer, w.window, filters.data(), int(filters.size()), start.empty() ? nullptr : start.c_str(), false);
    while (!answer.done) SDL_PumpEvents(), SDL_Delay(10);
    if (answer.file.empty()) return false;
    chosen = std::filesystem::path(answer.file);
#ifdef __ANDROID__
    // (Android hands over a content:// address: the file is read through SDL and put into older to be opened)
    if (answer.file.rfind("content://", 0) == 0) {
        size_t size = 0;
        void* bytes = SDL_LoadFile(answer.file.c_str(), &size);
        if (!bytes) return false;
        chosen = folder / "opened.jfreplay";
        std::ofstream(chosen, std::ios::binary).write(static_cast<const char*>(bytes), std::streamsize(size));
        SDL_free(bytes);
    }
#endif
    return true;
}

} // namespace play
