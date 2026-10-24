# Build and test

A C++20 compiler (e.g. Visual Studio 2022) and CMake 3.16 or later. On x64 the code uses SSE4.1 and
POPCNT; PEXT/PDEP is chosen at run time. A plain 64-bit implementation is included and tested as well. The GUI of the
generator builds on Windows with MSVC only; the game runs on Direct2D and XAudio2 on Windows and on SDL 3 on
Linux (below); the generator's CLI (with GCC and Clang; the Android version is built from `android/` with Gradle, see
`.github/workflows/android.yml`), the AI tools (`ai_match`
and others) and the tests of the core, the game, the AI and the network code also build with GCC.

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The build copies the pictures and the music of `media/` next to the game.

On Windows, when SDL 3 is found, PlayStation pads and the Switch Pro Controller and Joy-Con are read through it: once
another program has asked them for reports of their own kind (the Pro Controller on USB always), the generic reading
of Windows sees directions and buttons held that nobody holds. Without SDL 3 the game still builds and reads those
pads the generic way. Build SDL 3 as a static library and point `CMAKE_PREFIX_PATH` at it:

```powershell
# SDL 3 (in the unpacked SDL3-3.4.18.tar.gz of https://github.com/libsdl-org/SDL/releases)
cmake -S . -B build -A x64 -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_INSTALL_PREFIX=C:/sdl3
cmake --build build --config Release
cmake --install build --config Release

# Juraic Field
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/sdl3
```

On Linux the game is built when SDL 3 is found (without it, the game alone is left out). Build SDL 3 as a static
library and name it with `CMAKE_PREFIX_PATH`. The libraries of one file that read pictures, music and fonts
(stb_image, stb_truetype, dr_flac, dr_mp3, dr_wav) are in `third_party/`.

```bash
# SDL 3 (in the unpacked SDL3-3.4.18.tar.gz of https://github.com/libsdl-org/SDL/releases)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_X11_XTEST=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_PREFIX=$HOME/sdl3
cmake --build build && cmake --install build

# Juraic Field
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$HOME/sdl3
cmake --build build
ctest --test-dir build --output-on-failure
./build/JuraicField
```

The packages SDL 3 needs to build on Ubuntu 22.04: `cmake ninja-build pkg-config libasound2-dev libpulse-dev
libpipewire-0.3-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev
libxkbcommon-dev libwayland-dev libdecor-0-dev libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev
libegl1-mesa-dev libdbus-1-dev libudev-dev`. On Linux the build also copies the font (`media/fonts`) to `fonts`.

The tests compare the bitboard core with cell-by-cell references (including the unchanged v0.3.0 engine
in `tests/reference/`); check the rules of the game move by move and frame by frame, also against an independent
simulator in Python; play network matches over lines that delay, lose and reorder inside one program, and give
an end datagrams that are not those of a match played by the rules (`game_linked_rounds`,
`net_match_over_a_line`); check the UDP line with sockets of the same machine (`net_udp_on_this_machine`); read replay files back and refuse those that are not whole or name no delay a round can take
(`replay_files`); cover the AI and its tools; generate and independently verify
228 settings of the generator (chains 1-19, 3/4/5 colours, minimum, odd and maximum extras); and cover
configuration, history files, thread independence and the GUI.

The game has self tests that need no window:

```powershell
.\build\Release\JuraicField.exe --screenshot shot.png 0        # the menu as a picture (and the music is checked)
.\build\Release\JuraicField.exe --screenshot shot.png 3003     # a scripted match, saved and played back as a replay
.\build\Release\JuraicField.exe --screenshot shot.png 30008    # a network match over a line inside the program (both ends, the replay, the host's rules)
.\build\Release\JuraicField.exe --check-replay replay.jfreplay # plays a replay and writes how every round ended
```

## Folders

| Place | What it holds |
| --- | --- |
| `core/` | the bitboard core (headers only) |
| `game/` | the rules of the game (headers only): rules, frame counts, the pieces, the controls, one player, a round of two, and the late garbage of a network match |
| `ai/` | the versus AI (headers only): search, evaluation, reading, tactics, the clock, the levels, the settings |
| `play/` | the game itself: screen, sound, input, settings, replays, network; the parts of Windows (Direct2D, XAudio2, Media Foundation, Win32) and of Linux (SDL 3, a drawing engine of its own, `raster.h`) apart by `#ifdef` |
| `generator/`, `app/`, `gui/` | the chain board generator: the search, the CLI, the Windows GUI and editor |
| `media/` | the pictures and the music that come with the game, how they were made, and the font of the Linux version (`fonts/`) |
| `third_party/` | the libraries of one file the Linux version uses (stb, dr_libs), ViGEmClient the Windows version uses for its virtual pad, and their licences |
| `tests/`, `tools/` | the tests; matches between AIs (`ai_match`), measuring and tuning tools, benchmarks |
| `examples/`, `benchmarks/` | examples of the generator and the record of its speed measurements |
| `licenses/` | Ama's licence and the list of what the AI takes from Ama; the licences of what the Linux version contains (SDL 3, stb, dr_libs, BIZ UDPGothic; SDL 3 also in the Windows version) and of ViGEmClient in the Windows version |
