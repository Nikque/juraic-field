# Build and test

A C++20 compiler (e.g. Visual Studio 2022) and CMake 3.16 or later are needed.

- On x64 the code uses SSE4.1 and POPCNT. PEXT/PDEP is used or not according to the CPU, decided at run time.
- For other CPUs, a plain 64-bit implementation is included as well (`core/field_bits.h`).
- The GUI of the board generator is Windows (MSVC) only.
- The game runs on Direct2D and XAudio2 on Windows, and on SDL 3 on Linux. The Linux steps follow the Windows
  SDL 3 steps.
- The generator's CLI, the AI tools (`ai_match` and others) and the tests of the core, the game, the AI and
  the network code also build with GCC and Clang.
- On macOS the game is built as an app (`JuraicField.app`). The GitHub Actions workflow
  `.github/workflows/macos.yml` builds and tests it on Macs with Apple silicon.
- The Android version is built from `android/` with Gradle. Pass the unpacked SDL 3 release folder with
  `-PsdlSource=`; SDL's Android parts (its Java code and the Gradle wrapper) are taken from there too. The
  steps are in `.github/workflows/android.yml`.

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Then start the game with `.\JuraicField.exe` in `build/Release`.

Always measure performance with a **Release** build. The build copies the pictures and the music of `media/`
next to the executable (`background`, `bgm`).

On Windows, when SDL 3 is found, PlayStation pads and the Switch Pro Controller and Joy-Con are read through it.
Once another program has asked these pads to send reports of their own kind (the Pro Controller on USB always
does), the generic reading of Windows sees directions and buttons held that nobody holds.

Without SDL 3 the game still builds, and it reads those pads the generic way. Build SDL 3 as a static library
and point `CMAKE_PREFIX_PATH` at it:

```powershell
# SDL 3 (in the unpacked SDL3-3.4.18.tar.gz of https://github.com/libsdl-org/SDL/releases)
cmake -S . -B build -A x64 -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_INSTALL_PREFIX=C:/sdl3
cmake --build build --config Release
cmake --install build --config Release

# Juraic Field
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/sdl3
```

On Linux the game is built when SDL 3 is found; without it, only the game is left out. Build SDL 3 as a static
library and point `CMAKE_PREFIX_PATH` at it. The single-file libraries that read pictures, music and fonts
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

The development packages SDL 3 needs to build on Ubuntu 22.04: `cmake ninja-build pkg-config libasound2-dev libpulse-dev
libpipewire-0.3-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev
libxkbcommon-dev libwayland-dev libdecor-0-dev libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev
libegl1-mesa-dev libdbus-1-dev libudev-dev`

On Linux the build also copies the font (`media/fonts`) to `fonts`.

| Test | What it checks |
| --- | --- |
| `core_and_generator` (SSE and portable versions) | the bitboard clearing, gravity and score on random boards (with gaps, garbage and the 13th row) against a cell-by-cell reference; the candidates of the backward step against the full chain check of v0.3.0 (`tests/reference/`) |
| `game_rules`, `game_independent_simulation` | the rules of the game move by move (pieces, placements, chain waves, score, passing garbage) against a cell-by-cell implementation, and the same records again with an independent simulator in Python |
| `game_frames` | the game frame by frame (frames of falling and locking, pre-input, reachable places) against measured tables and exhaustive inputs |
| `game_linked_rounds` | the late garbage of network matches: in whatever order the two seats advance, every frame equals a reference with a single ledger, and garbage never remains on both sides |
| `net_match_over_a_line` | over a fake line inside the program that delays, loses and reorders, the two ends play the same match; refusing another version, leaving, disconnecting, a rematch; handling data that breaks the rules (the next opponent after one that cannot join, too many frames sent ahead, a delay out of range, values impossible as times) |
| `net_udp_on_this_machine` | real UDP sockets used only inside this PC (`::1`): datagrams that are too long are skipped, the next opponent can join after one that cannot join or after unrelated data, datagrams from other senders have no effect during a match, the number of datagrams read per frame is limited |
| `replay_files` | reading replay files: what the game wrote reads back as it is; network replays with no delay or a delay out of range, and cut-off files, are refused |
| `ai`, `ai_match_same_result`, `ai_toko_same_result`, `ai_tune_same_result` | the versus AI (search, reading, decisions, clock, levels, setting switches) and self-tests of the AI tools |
| `all_chain_lengths` | generates the 228 settings of 1-19 chains × 3/4/5 colours × extra pieces (none, one, the largest number, a range up to it) and verifies every board independently |
| `grid_field_and_search` | the field type and search also used for 10 × 18 (`generator/grid_field.h`, `grid_generator.h`): on 6 × 13, clearing, gravity, score, the reach rule and the backward candidates match the current core, and each attempt makes the same board as the current search; on 10 × 19, clearing and gravity on random boards against a cell-by-cell reference, and the hidden 19th row, the game-over cells, the reach rule and the generated boards |
| `all_chain_lengths_10x18` | on 10 × 18, generates 1-47 chains × 4/5 colours × extra pieces (none, one, the largest number, a range up to it) and 1-47 chains in three colours × extra pieces (none, one, a range up to the largest), and verifies every board independently |
| `config_and_generation_integration` | reading the settings, refusing invalid values, appending to the history, skipping duplicates, the same output for any thread count (including 10 × 18 boards, and the independent verifier catching a rewritten board) |
| `independent_output_verification` | independent verification of the bundled example output |
| GUI tests (6) | settings, file formats, playback, editing, special pieces, an integration test with a real window and the independent verification of its output, the console code page |

The game has self tests that need no window:

```powershell
.\build\Release\JuraicField.exe --screenshot shot.png 0        # the menu as a picture (and the music is checked)
.\build\Release\JuraicField.exe --screenshot shot.png 3003     # a scripted match, saved and played back as a replay
.\build\Release\JuraicField.exe --screenshot shot.png 30008    # a network match over a line inside the program (both ends, the replay, the host's rules)
.\build\Release\JuraicField.exe --check-replay replay.jfreplay # plays a replay and writes how every round ended
```

The output of the generator can also be checked with the independent verifier in Python:

```powershell
python tests/verify_output.py tests/fixtures/solutions.log --allow-extra
.\build\Release\random_19_chain.exe config.ini > generator.log
python tests/verify_output.py generator.log --allow-extra
```

## Folders

| Place | What it holds |
| --- | --- |
| `core/` | the bitboard core (headers only): `field_bits.h` (96-bit sets), `bit_field.h` (board, clearing, gravity, score), `reach.h` (reach rule), `rng.h` (random numbers) |
| `game/` | the rules of the game (headers only): rules, frame counts, the pieces, the controls, one player, a round of two, and the late garbage of a network match |
| `ai/` | the versus AI (headers only): search, evaluation, reading, tactics, the clock, the levels, the settings |
| `play/` | the game itself: screen, sound, input, settings, replays, network; the parts of Windows (Direct2D, XAudio2, Media Foundation, Win32) and of Linux (SDL 3, a drawing engine of its own, `raster.h`) apart by `#ifdef` |
| `generator/` | backward beam search, last pair check, full condition check, settings, parallel runs; the field type and search for 10 × 18 (`grid_field.h`, `grid_generator.h`, `grid_runner.cpp`) |
| `app/` | the CLI entry point and console output |
| `gui/` | the Windows GUI of the board generator, the editor, URL output, result files |
| `media/` | the pictures and the music that come with the game, how they were made, and the font of the Linux version (`fonts/`) |
| `third_party/` | the single-file libraries the Linux version uses (stb, dr_libs), ViGEmClient the Windows version uses for its virtual pad, and their licences |
| `tests/` | the tests; `tests/reference/v030_engine.h` is the v0.3.0 engine kept for comparison |
| `tools/` | matches between AIs (`ai_match`), measuring and tuning solo search (`ai_toko`, `ai_tune`), benchmarks, the reach rule comparison, making the release packages |
| `examples/`, `benchmarks/` | examples of the generator and the record of its speed measurements |
| `licenses/` | Ama's licence and the list of what the AI takes from Ama; the licences of what the Linux version contains (SDL 3, stb, dr_libs, BIZ UDPGothic; SDL 3 also in the Windows version) and of ViGEmClient in the Windows version |
