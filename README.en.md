# Juraic Field

[日本語 README](README.md)

Juraic Field is a falling-block puzzle program for versus and solo play: building chains alone, two players on
one PC, matches against the versus AI "Juraic", watching two AIs, matches between two people over a network,
replays, and a generator of chain boards. It runs on Windows 10 / 11 (64-bit), on Linux (x86-64), on macOS (Apple silicon) and on Android (arm64). The screens are in Japanese.

- `JuraicField.exe` (`JuraicField` on Linux, `JuraicField.app` on macOS): the game
- `JuraicFieldGenerator.exe` / `random_19_chain.exe`: the chain board generator (GUI / CLI; the GUI on Windows only)

The rules follow those of Puyo Puyo Tsu. Juraic Field is an unofficial program made by an individual and is
not affiliated with SEGA CORPORATION. "Puyo Puyo" is a registered trademark of SEGA CORPORATION.

## Getting and starting it

1. Unpack the zip from the [release page](https://github.com/Nikque/juraic-field/releases) into a
   new folder (do not start it from inside the zip).
2. Start `JuraicField.exe`. Nothing is installed and no runtime is needed. Windows may warn about an unknown
   publisher the first time.
3. Choose from the menu. Keys and controller buttons can be looked up and changed under 操作の割り当て.

To remove it, delete the folder (nothing is written to the registry). Settings and bindings are kept in
`JuraicField.ini` next to the executable; delete that file to start from the defaults again.

### Linux

1. Unpack `juraic-field-…-linux-x86_64.tar.gz` from the release page (`tar xzf juraic-field-…-linux-x86_64.tar.gz`).
2. Start `./JuraicField` in the unpacked folder. Nothing is installed.

- It should run on Linux of the generation of Ubuntu 22.04 and later (glibc 2.35 or later), on an X11 or Wayland
  desktop; the sound goes through PulseAudio, PipeWire or ALSA.
- It plays what the Windows version plays (without the window of the board generator; the CLI `random_19_chain`
  is there). Settings files, replays and bindings are those of the Windows version.
- Pictures of your own: png, jpg, bmp; music: mp3, wav, flac (fewer than on Windows).
- The text is drawn with the font that comes with it, BIZ UDPGothic.
- **It has been tried on the WSL of Windows only (Ubuntu 22.04, WSLg).** A Linux machine of its own and controllers
  have not been tried yet. A network match with the Windows version was played on one PC (the Linux version in
  the WSL opened the room, the Windows version came in), scripts on both sides, the same to its end on both;
  between two machines, and with people at the screens, not yet.

### macOS

1. Unpack `juraic-field-…-macos-arm64.zip` from the release page and open `JuraicField.app` (move it where you like).
   It is for Macs with Apple silicon (M1 and later), macOS 11 or later.
2. It is not signed nor notarized by an Apple developer, so the first time macOS refuses to open it (it cannot
   check the developer). Open System Settings, Privacy & Security, and press "Open Anyway" near the bottom.

- Settings, replays and your own pictures and music are in `~/Library/Application Support/JuraicField` (the files
  are those of the Windows version). It reads the formats the Linux version reads.
- **It has been tried on the Mac build machines of GitHub only (Apple silicon)**: it builds, the tests pass (replays
  saved on Windows play the same), AI matches give the checksums of Windows, and the self-test pictures, taken without
  a window, are those of the Linux version byte for byte. A person playing it in a window, the sound and controllers
  have not been tried yet.

### Android

`juraic-field-…-android-readme.txt` next to the APK on the release page tells how to install, connect and play (in
Japanese, with a short part in English).

1. Open the APK from the release page on the device and install it (Android asks once to allow apps from that source).
   Android 8 or later, arm64.
2. Play with a controller (USB-C or Bluetooth) or a keyboard. There is no touch control (the keyboard of the screen
   appears only to type the name and the address of a network match). The back button is Esc.

- Landscape. The pictures, the music and the font are copied out of the APK the first time (it takes a moment).
- Settings, replays and your own pictures and music are in `Android/data/io.github.nikque.juraicfield/files` (a file
  manager or a PC on USB reaches it). It reads the formats the Linux version reads.
- On the CPU of a phone the AI's search may be narrower within its time, so a level may play weaker (expected; the
  levels are the same).
- **Tried on the machines of GitHub only**: the APK builds; on an emulator (x86_64, Android 14) it opens, draws the
  menu, runs for 45 seconds, opens its sound and copies out its files. A real device, controllers, keyboards, hearing
  the sound, playing and network matches have not been tried yet.

## Playing

### The menu

| Entry | What it is |
| --- | --- |
| とこ（1 人プレイ） | building chains alone: take a move back, play the same pieces again, practise from a board file, a preview of the chain |
| 対戦（2 人） | two players on one PC |
| 対戦（人 対 AI） | a person (1P) against the AI Juraic |
| 観戦（AI 対 AI） | two AIs to watch |
| ネット対戦（人 対 人） | two people on two PCs, connected directly (see below) |
| リプレイを見る | play a saved replay |
| 設定 | rules, controls, AI, display and sound |
| 操作の割り当て | bindings of two keyboard layouts and of controllers |
| 盤面ジェネレーター | opens `JuraicFieldGenerator.exe` |

### Controls (as shipped)

| Action | Keyboard 1 | Keyboard 2 |
| --- | --- | --- |
| move left / right | ← → | A D |
| drop faster | ↓ | S |
| rotate left / right | Z / X | F / G |
| pause | P | T |

Controllers (Xbox ones and others) work too. With the setting クイックドロップ on, up (↑ on keyboard 1, W on
keyboard 2, up on a controller) drops the pair at once; with it off, ↑ also rotates right in solo play. On Windows,
PlayStation pads and the Switch Pro Controller and Joy-Con are read through SDL 3 and taken by the places of their
buttons, as Xbox pads are (the lower button rotates left, the right one rotates right).

In solo play: Backspace takes a move back, R plays the same pieces again, N takes new pieces, O practises from a
board file, E empties the board, Ctrl+C / Ctrl+V copy and paste the seed, F5 saves a replay. A board file of the
generator (`.jfchain`, `.jfboard`) can also be dropped onto the window (a file dropped during a network match is
not opened).

### Settings

Four parts. ↑↓ choose, Enter or → opens a part, ←→ change a value (a key held down goes on after a moment, the
cursor keys of the lists too; past either end a value comes in again at the other). A grey item has no effect as the other settings
stand. Rounds and sets may be set up to 99; between the two fields a match shows those won as marks (★, ◆) up to
five, and as numbers ("★ won / needed") beyond that.

| Part | Items |
| --- | --- |
| ルール (rules) | colours (3 to 5), pieces that clear, correction of the first pairs, game speed, drop bonus, garbage rate, margin time, chain requirement, all-clear bonus, garbage that falls at once, rounds to win a set, sets to win a match, delay of a network match |
| 操作 (controls) | pre-input, diagonal input, quick drop |
| AI | search (automatic / by hand) with its width, depth, number of made-up sequences and threads, when a choice begins, its time, whether a building move may fire the main chain, the kind of AI and its level (the 2P side, and the 1P side when two AIs are watched) |
| 表示と音 (display and sound) | volume, chain preview, landing position, pictures (1P field, 2P field, the rest) and how dark, music (match, solo, menu) and its volume |

### Replays

- When a match ends its replay is saved as `.jfreplay` in the folder `replays` next to the executable; in solo
  play F5 saves one.
- リプレイを見る plays one, and so does dropping the file onto the window. Space pauses, → held plays four times as
  fast, R starts again.
- A replay is a text file with the seed of the pieces, the rules and the buttons of every frame: the same seed
  and buttons give the same game.

### Pictures and music

- Three pictures and nine pieces of music come with the program and are used from the start (in the repository:
  `media/`, copied next to the executable by the build; how they were made and their licence:
  [media/README.md](media/README.md)).
- A picture can be chosen for each of three places (the field of 1P, the field of 2P, the rest) and music for each
  of three scenes (a match, solo play, the menu).
- Own files go into the folders `1p`, `2p`, `other` below `background` and `versus`, `toko`, `menu` below `bgm`
  next to the executable: png, jpg, bmp, webp for pictures; mp3, wav, m4a, aac, wma, flac for music (not ogg).
  Music is repeated.

## The versus AI "Juraic"

- It reads the opponent's field and the pieces on the screen: it builds chains, answers attacks, and fires first
  when it reads a win.
- Two kinds: **Juraic 催促（小）** attacks with small chains once its field fills up, keeping its main chain (the
  default); **Juraic 催促（極小）** hardly attacks on its own and builds its main chain.
- 36 levels (the default is Lv 36, the strongest). The lower the level, the slower the hands, the later the
  opponent's field is seen, and the smaller the search. Neighbouring levels were set so that the higher one wins
  about six rounds in ten, by matches of 催促（小） against itself (the levels of 催促（極小） use the same settings
  and were not measured).
- After a match against the AI, or between two AIs, the kind and the level of the AI can be changed with the mouse
  on the screen of the result (from the next match on; the button held down on a level goes on changing it).
- It uses up to 24 threads (two fewer than the logical processors; one fewer on machines with fewer than eight).
  A choice is kept within 180 ms; on a slow machine the search is made narrower, and after a round in which the
  AI was short of time the screen says so (処理落ち: AI が弱くなった可能性).
- A measurement: against the public AI [Ama](https://github.com/citrus610/ama), made to play the same moves in
  every run, 催促（小） at Lv 36 won 1,093 of 1,440 rounds (the version of 2026-10-14, with the search size used
  for measuring). The tools of that measurement - the program that seats Ama as an opponent, and the Ama made
  to play the same moves in every run - are not in this repository.
- Parts of the AI follow Ama: see [licenses/Ama-NOTICE.txt](licenses/Ama-NOTICE.txt).

## Matches over a network

- Two PCs are connected directly. One opens a room (部屋を立てる), the other enters the address of that PC and
  joins (部屋に入る). There is no lobby: the two exchange the address themselves.
- The one who opens the room is 1P and its rules are used (the items of ルール on the settings screen, and of
  操作 the two that are the same for both: 先行入力, pre-input, and クイックドロップ, quick drop). Both need the
  same version of Juraic Field.
- The name and the address are typed after Enter on their row and ended with Enter or Esc; Ctrl+V pastes an
  address. The address is shown as dots unless F1 is pressed (for streams).
- There is no pause. Esc pressed twice leaves the match. After a match, both pressing the confirm button starts
  another.

**Delay**

- The own pair moves at once; the opponent's field is seen a little late.
- Garbage (its announcement, offsetting and confirmation) takes effect later by the number of frames shown at the
  top of the screen (遅延 n フレーム; 60 frames are a second). It is settled by the rules of a match on one PC, so
  two attacks that cross never leave garbage above both fields.
- n is set from the speed of the line when the two connect (or fixed, 1 to 30, by the setting ネット対戦の遅延).
- When the opponent's buttons arrive late the own side waits for them (通信を待っています…); after eight seconds
  without anything the match ends (相手との通信が途切れました). While the window is being moved or resized the
  line stands still too.

### Connecting (what the one who opens the room prepares)

The line is UDP, port 50777 on the PC that opens the room (`netPort=` in `JuraicField.ini` changes it; the one
who joins writes `address:port` only when it is another port). The one who joins prepares nothing.

1. **Windows firewall**: the first time a room is opened Windows may ask; allow the access. If the question was
   closed, look for `JuraicField.exe` among the apps allowed through the firewall in the Windows security
   settings.
2. **On the same network**: no router setting is needed. F1 on the waiting screen shows the addresses of the PC
   (192.168.… and the like) to tell the other player.
3. **From different homes (port forwarding)**: on the router of the one who opens the room, forward UDP port
   50777 to the PC that runs the game - routers call this port forwarding, port mapping or virtual server. Find
   the PC's IPv4 address and the default gateway with `ipconfig`, open the router's pages at the gateway's
   address, enter UDP, 50777 and the PC's address, and tell the other player the address the home has towards
   the outside (the WAN address shown by the router).
4. **Where forwarding is not possible** (shared lines of apartment houses, mobile lines, lines that share one
   IPv4 address among customers): a VPN that puts both PCs on one network, an IPv6 address if both lines have
   IPv6 (the router may have to let UDP 50777 through), or changing who opens the room.

Do not show the outside address in a stream or in public, and take the forwarding out of the router afterwards.

## Chain board generator

A C++20 program that randomly generates boards (three to five colours) which fire an exact
chain of 1 to 19 waves when one last pair is placed. The URL of every board found is appended to
`19chain_urls.txt`. The shipped settings generate 100 new 19-chain boards of 76 pieces in four colours.
With the field set to 10 columns × 18 rows it generates chains of 1 to 47 waves ("10 × 18 fields" below).

All board work is done on bitboards: one 16-bit lane per column, six columns in one SSE register,
three registers for the colour code (48 bytes per board). The same core (`core/`) is used by solo play,
versus play and the AI.

### Windows GUI

`JuraicFieldGenerator.exe` offers settings, asynchronous generation with cancellation, a result list, a
6×13 or 10×19 board and chain playback. The CLI (`random_19_chain.exe` / `run.cmd`) remains available. See
[GUI_README.md](GUI_README.md) (Japanese) for usage and [URL_FORMATS.md](URL_FORMATS.md) for the simulator URLs.
The board editor supports five colours, garbage, point, hard and iron pieces and fixed walls; these
special kinds are never generated.

### What a generated board satisfies

The output is the board **right after the last pair has been placed**.

1. 6 columns × 13 rows, no gaps inside a column, nothing in the 14th row.
2. Firing it gives exactly the requested number of waves, and every wave clears **one group of exactly four**.
3. The 13th row never connects or clears, but falls when the pieces below it disappear.
4. After the chain, the cell where pairs appear (third column, twelfth row) is empty.
5. The board holds `4 * target_chain + extra` pieces; `extra` pieces remain after the chain.
6. There is at least one **last pair**: the two topmost pieces of one column, or the topmost pieces of two
   neighbouring columns, such that without them
   - nothing clears,
   - the cell where pairs appear is empty, and
   - the pair can travel to its place (reach rule below).

The board itself may cover the cell where pairs appear: the last pair may be placed there as long as the
chain frees it again (item 4). For each board the program prints the four pieces that clear first and
**every** last pair that satisfies item 6.

#### Reach rule

The pair appears in the third column (axis in row 12, child in row 13). Whether a position can be reached
is decided from the column heights alone:

- a column of height 11 or less can always be passed;
- a column of height 13 can never be passed (the axis piece cannot enter the 14th row);
- a column of height 12 can be passed only while the axis piece is lifted to the 13th row (wall crossing).
  It is lifted right after the spawn when both neighbours of the third column are 12 or higher (quick
  turn), on a column exactly 11 high (floor kick), or on top of a 12-high column it has already climbed;
- once lifted it may hop over **up to two** lower columns before landing on the 12-high column. The floor
  kick may be made on the other side of the spawn column; the columns crossed on the way back count;
- a vertical pair needs a column of height 11 or less (nothing is left in the 14th row).

Beside the quick turn, the floor kick and a one-column hop from a 12-high column, hops after a floor kick
and over two to four columns are allowed: they are ordinary wall crossings of the real game (`MAX_HOP` in
`core/reach.h`). Every placement this rule allows is also allowed by the movement model of v0.3.0 (kicks
and quick turns searched without gravity); this was checked for all 6.45 million height combinations with
`tools/compare_reach.cpp`.

#### Not guaranteed

- **No build sequence from an empty board is produced** (v0.3.0 did). The guarantee is only that placing
  the last pair fires the chain.
- Frame timing, lock delay and per-platform differences are not modelled. Garbage pieces are not generated.

#### Every board is generated independently

Each attempt uses its own random stream and starts from the board that remains after the chain; one
attempt saves at most one board. Found boards are never varied to mass-produce similar ones. URLs already
in `19chain_urls.txt` are skipped.

### How the search works

The chain is grown backwards. Starting from the residual board (only the extra pieces, nothing clears),
each step inserts a group of four cells into the columns. A step is accepted only if, on the new board,
(i) no group of four or more exists among the old pieces and (ii) no old piece next to the inserted cells
has their colour. Then the inserted group is the only thing that clears, and gravity gives back the
previous board exactly. By induction a board made by n steps clears in exactly n waves of one group of
four, starting with the group inserted last and ending as the residual board - for every chain length,
extra count and colour count. So no chain simulation runs inside the search:

| Condition | Checked during the search |
| --- | --- |
| number of waves, single groups of four, trigger | follows from the construction: not checked |
| spawn cell empty after the chain | the final board is the residual board, which is built with at most 11 pieces in the third column |
| nothing in the 14th row | insertions keep every column at 13 or lower |
| nothing clears without the last pair | equivalent to "the pair holds a trigger piece": one mask test |
| the last pair can appear and reach its place | from the heights, per candidate pair |

Insertion is a per-column multiplication, "is there a group of four or more" is derived from four
same-colour-neighbour masks without flood fill, and candidate positions come from precomputed bit sets.
Each step keeps `candidates_per_parent` uniformly chosen children per board and at most `beam_width`
boards; the children are found by testing random (position, colour) pairs instead of listing them all.
The step before the last keeps every child.

On a nearly full board the last pair has very little room: with 76 pieces a trigger piece of a valid last
pair must sit at row 10 or higher in the third column or at row 12 in the second or fourth, and nowhere
else. So for boards of **72 pieces or more**, the six steps before the last use only insertions that leave
at most one old piece above the new group in one of its columns. This raises the share of successful
attempts for 19 chains / 76 pieces from 24% to 52%. Smaller boards, where the rules leave the trigger almost
free, are searched without this guidance so that their variety is not narrowed.

**Every saved board is checked against the full definition directly** (`verifySolution`), and an
independent Python cell simulator (`tests/verify_output.py`) checks the same conditions, including that
the printed last pairs are exactly all valid ones.

Attempts run on several threads. Attempt k always uses random stream k and results are committed in
order, so **the same settings give the same boards in the same order for any thread count and compiler**
(checked: MSVC and GCC produce the same 2000 URLs).

### Generator settings

| Key | Meaning | Shipped |
| --- | --- | --- |
| `field_columns`, `field_rows` | columns and rows that clear (one hidden row above them): 6 and 12, or 10 and 18 | 6, 12 |
| `target_chain` | number of waves, 1-19 (1-47 on 10 × 18) | 19 |
| `target_success_count` | new unique boards to save in this run | 100 |
| `initial_seed` | random seed (unsigned 64-bit) | 20261004 |
| `color_count` | 3, 4 or 5 | 4 |
| `beam_width` | boards kept per step | 48 |
| `restarts` | maximum number of independent attempts | 100000 |
| `candidates_per_parent` | children kept per board | 2 |
| `min_extra_pieces` / `max_extra_pieces` | range of extra pieces, at most `78 - 4 * target_chain` (`188 - 4 * target_chain` on 10 × 18) | 0 / 0 |
| `threads` | search threads, 0 = automatic (half of the logical processors) | 0 |

The first six keys are required.
Exit codes: 0 target reached, 1 attempt limit reached (URLs found so far are kept), 2 configuration or I/O error.

### Speed

Windows 11 x64, Ryzen 9 9950X, Visual Studio 2022, Release, 2026-10-04, seed 20261004, beam 48 / 2.
Other work was running on the machine, so the numbers are indicative.

| Setting | v0.3.0 (20 boards) | new, 1 thread (2000 boards) | new, 16 threads (2000 boards) |
| --- | ---: | ---: | ---: |
| 19 chains, 76 pieces, 4 colours | 3.81 s (5.2/s) | 1.49 s (1340/s) | 0.17 s |
| 19 chains, 77 pieces | 8.65 s (2.3/s) | 1.83 s (1090/s) | 0.20 s |
| 19 chains, 78 pieces | 17.54 s (1.1/s) | 2.80 s (714/s) | 0.28 s |
| 18 chains, 72-78 pieces | 2.87 s (7.0/s) | 1.29 s (1550/s) | 0.15 s |
| 19 chains, 76 pieces, 5 colours | 5.51 s (3.6/s) | 1.39 s (1440/s) | 0.18 s |

v0.3.0 also searched for a build sequence from an empty board, so this is not a like-for-like comparison.
See [BENCHMARK.md](BENCHMARK.md) (Japanese).

### 10 × 18 fields

With `field_columns = 10` and `field_rows = 18` in `config.ini` (in the GUI: "10列×18段" under "盤面") the
generator works on a field of 10 columns:

- the lower 18 rows connect and clear; the 19th row is hidden: it never connects or clears but falls when the
  pieces below it disappear; nothing is put into a 20th row;
- pairs appear in the fifth column, and the game is over when the fifth or the sixth column reaches the 18th
  row: those two cells are empty after the chain and before the last pair;
- the reach rule is the one above with the rows moved (17 or lower: passable; 19: never; 18: only while the
  axis piece is lifted into the 19th row);
- 1 to 47 chains: before the last pair the fifth and sixth columns hold at most 17 pieces, so a board holds at
  most `8 × 19 + 2 × 17 + 2 = 188` pieces, `188 / 4 = 47` waves; extra pieces up to `188 - 4 * target_chain`.

No simulator site shows a field of this size, so no URL is written; instead a **board code** is appended to
`boards_10x18.txt` (and used to skip duplicates): `10x18:` and the 190 cells from the 19th row down, each
row left to right, as `.` (empty) and `R G B Y P`. The output lists `field=10x18` before the board and a
`board code:` line instead of the URL. The GUI shows, plays and edits these boards.

The search is the same backward insertion, on a field type with one 32-bit word per column
(`generator/grid_field.h`) and the same steps written for any field (`generator/grid_generator.h`); on
6 × 13 that search finds, attempt by attempt, exactly the boards of the 6 × 13 search
(`tests/grid_tests.cpp`). Boards with at most 20 cells to spare follow a plan: before the last 12 steps the
columns more than two away from the fifth and sixth are filled and room for the last steps is kept next to
them; in the last 12 steps the new group goes near the top there; at every step the boards with the least
room, weighted by the distance from the fifth and sixth columns, are kept. For 47 chains in four colours,
attempts without the plan found 0 boards of 300, with it 50 of 200 (`tools/grid_bench.cpp`; the numbers
behind every value are in `generator/grid_field.h`).

Speed (2026-10-17, same machine and settings as above, 100 boards each, all checked by the independent verifier):
47 chains of 188 pieces in four colours 10.98 s on one thread (9.1/s) and 1.13 s on 16 threads (88/s); in
five colours 4.50 s and 0.58 s; 40 chains in four colours 0.56 s on 16 threads.

Of the 555 settings of 1 to 47 chains × three to five colours × extra pieces (none, one, the largest number,
a range up to it), one board each within 20000 attempts or 60 seconds (2026-10-17, four threads): every setting
in four and five colours was found. In three colours no board of 47 chains was found (nor in 100 to 200
attempts of every other setting tried); 44 to 46 chains were found, in up to about 18 seconds; with the
largest number of extra pieces, 8-10, 19-22, 24-27, 34, 41, 42 and 46 chains were not found.

## Build and test

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

## License

[MIT License](LICENSE) © 2026 Nikque

- The pictures and the music in `media/` are under the same terms ([media/README.md](media/README.md)).
- Parts of the versus AI follow [Ama](https://github.com/citrus610/ama) (MIT License, Copyright (c) 2023
  citrus610): see [licenses/Ama-NOTICE.txt](licenses/Ama-NOTICE.txt) and
  [licenses/Ama-LICENSE.txt](licenses/Ama-LICENSE.txt).
- The Linux version contains SDL 3 (zlib License), stb (MIT or public domain), dr_libs (public domain or MIT-0)
  and BIZ UDPGothic (SIL Open Font License 1.1); their licences are in `licenses/`. The Windows version contains
  SDL 3 as well, for reading controllers, and ViGEmClient (MIT License) for the virtual pad of the mode in which Juraic
  plays 2P of the real game (the driver ViGEmBus, BSD-3-Clause, is not included: the user installs it).
- No warranty.
