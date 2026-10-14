#pragma once
// Is a key down now: by its virtual key code (Windows numbers them; play/keys.h gives the same numbers elsewhere).
// Windows asks the system wherever the window is; elsewhere the window's part (play/platform_sdl.h) keeps the
// keys it has been told of in `keysDown`.
#ifdef _WIN32
#include <windows.h>
#else
#include "keys.h"
#endif

namespace play {

#ifdef _WIN32
inline bool keyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
#else
inline bool keysDown[256]{};
inline bool keyDown(int vk) { return vk >= 0 && vk < 256 && keysDown[vk]; }
#endif

} // namespace play
