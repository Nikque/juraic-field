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
| 実機の 2P を Juraic が操作 | Windows only: Juraic plays 2P of the real game through a virtual pad (see below) |
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

## Juraic playing 2P of the real game (Windows, a trial)

- In an offline match of two players of the real game, running on the same PC, the program reads the real game's
  window and Juraic plays 2P through a virtual pad. 1P is a person. It does not work in online matches.
- 実機の 2P を Juraic が操作 in the menu looks for the real game's window, takes the place of 2P, chooses its
  character and kind of controls, and plays 2P once a match begins. The kind and the level of the AI can be changed
  on that screen (each time it opens they start from those of the 2P side in the settings).
- The virtual pad needs the driver ViGEmBus, which is not included: install it yourself ("Installing the virtual pad driver (ViGEmBus)" below).
- **Juraic in this mode is weaker than Juraic in the matches of this program.** It knows the fields and the pairs
  only by reading the real game's screen, which it sometimes reads wrong or cannot read for a while; the presses of
  the pad reach the real game late, and the way they are pressed and the timing differ from this program's matches.
  The strength of the levels and the measurement above do not hold in this mode, and no rate of wins against the
  real game has been measured.
- The parts for finding faults - the record of each reading (in `realgame\<date and time>` next to the executable;
  it can take more than 2 GB at a time), the key S that saves a picture of the real game, the table read from
  `realgame_layout.txt` and the counters on the screen - are built only with `JURAIC_REALGAME_DEBUG=ON` in CMake;
  the released packages have none of them.

### Installing the virtual pad driver (ViGEmBus)

You need it only for "Juraic playing 2P of the real game". ViGEmBus is not included in Juraic Field (its author is Nefarius Software Solutions).

1. Open the official release page: <https://github.com/nefarius/ViGEmBus/releases/tag/v1.22.0>
2. Under "Assets" at the bottom of the page, download `ViGEmBus_1.22.0_x64_x86_arm64.exe`.
3. Open the downloaded file and follow the installer. Answer "Yes" when Windows asks for administrator permission, and restart the PC if asked.
4. Start Juraic Field again and choose 実機の 2P を Juraic が操作 again.

- ViGEmBus development ended in November 2023; v1.22.0 is the last version. Do not use copies from anywhere other than the official page.
- **To check it is installed**: Device Manager shows "Nefarius Virtual Gamepad Emulation Bus" under "System devices".
- **To remove it**: Settings → Apps → Installed apps → "ViGEm Bus Driver" → Uninstall. Other software may also use ViGEmBus, so check before removing it.
- If the game says the ViGEmBus driver is not installed, start Juraic Field again after installing (after the restart, if one was asked for).
- Juraic Field does not read this virtual pad as one of its own controllers.
- Opening `仮想パッドのドライバーの入れ方.cmd` in the Windows zip shows the same instructions (and checks whether the driver is installed).

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

A program that randomly generates boards (three to five colours) which fire an exact chain when one last pair is placed: 1 to 19 waves on 6 × 13 fields, 1 to 47 waves on 10 × 18 fields. Each board found is appended to `19chain_urls.txt` as a URL for external chain simulators. The shipped settings generate 100 new 19-chain boards of 76 pieces in four colours.

- **Windows GUI**: `JuraicFieldGenerator.exe` — edit settings, generate and cancel, pick results, play back chains, edit boards. Usage: [GUI_README.md](GUI_README.md) (Japanese).
- **CLI**: `random_19_chain.exe` (`run.cmd`); also included in the Linux version.
- URL formats: [URL_FORMATS.md](URL_FORMATS.md)

The conditions the boards satisfy, how the search works, the settings and the generation speed are in [GENERATOR.en.md](GENERATOR.en.md).

## Build and test

How to build (Windows, Linux, Mac, Android), how to run the tests, and the folder layout: [BUILDING.en.md](BUILDING.en.md).

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
