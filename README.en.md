# Juraic Field

[日本語 README](README.md)

Juraic Field is a falling-block puzzle program for versus and solo play. It has solo chain building ("toko"), two players on one PC, matches against the versus AI "Juraic", AI-versus-AI matches to watch, network matches between two people, saving and playing back replays, and a generator of chain boards. It runs on Windows 10 / 11 (64-bit), Linux (x86-64), macOS (Apple silicon) and Android (arm64). The screens are in Japanese.

- `JuraicField.exe` (`JuraicField` on Linux, `JuraicField.app` on macOS): the game
- `JuraicFieldGenerator.exe` / `random_19_chain.exe`: the chain board generator (GUI / CLI; the GUI is Windows only)

The rules follow those of Puyo Puyo Tsu. Juraic Field is an unofficial program made by an individual and is
not affiliated with SEGA CORPORATION. "Puyo Puyo" is a registered trademark of SEGA CORPORATION.

## Getting and starting it

1. Unpack the zip from the [release page](https://github.com/Nikque/juraic-field/releases) into a new folder. Do not start it from inside the zip.
2. Start `JuraicField.exe`. Nothing is installed, and no extra runtime is needed. Windows may warn about an unknown publisher the first time.
3. Choose from the menu. You can check and change the key and controller bindings under 操作の割り当て in the menu.

To remove it, delete the unpacked folder. Nothing is written to the registry. Settings and bindings are saved in `JuraicField.ini` next to the executable. To go back to the defaults, delete that file.

### Linux

1. Unpack `juraic-field-…-linux-x86_64.tar.gz` from the release page (`tar xzf juraic-field-…-linux-x86_64.tar.gz`).
2. Start `./JuraicField` in the unpacked folder. Nothing is installed.

- It should run on Linux of the same generation as Ubuntu 22.04 or later (glibc 2.35 or later), on an X11 or Wayland desktop.
- Sound goes through PulseAudio, PipeWire or ALSA.
- You can play everything the Windows version has. The board generator window is missing; the CLI `random_19_chain` is included instead.
- Settings files, replays and bindings use the same format as the Windows version and can be used as they are.
- Your own pictures can be png, jpg or bmp, and music mp3, wav or flac. These are fewer formats than on Windows.
- Text is drawn with the bundled font BIZ UDPGothic.
- **It has been tested only on WSL on Windows (Ubuntu 22.04, WSLg).** It has not yet been tested on a Linux PC outside WSL or with controllers.
- A network match with the Windows version was tested on one PC. The Linux version in WSL opened the room, the Windows version joined, and a scripted match ran identically on both sides to the end. Matches between two PCs, and matches played by people at the screens, have not been tested yet.

### macOS

1. Unpack `juraic-field-…-macos-arm64.zip` from the release page, move `JuraicField.app` wherever you like, and open it. It is for Macs with Apple silicon (M1 or later) running macOS 11 or later.
2. It is not signed or notarized by an Apple developer, so the first time macOS refuses to open it, saying it cannot verify the developer. Open System Settings → Privacy & Security and press "Open Anyway" near the bottom; after that it opens.

- Settings, replays and your own pictures and music are in `~/Library/Application Support/JuraicField`. The files use the same format as on Windows.
- It reads the same picture and music formats as the Linux version.
- **It has been tested only on GitHub's Mac build machines (Apple silicon).** What was tested:
  - it builds;
  - the tests pass (including playback of replays saved on Windows);
  - AI matches give the same checksums as on Windows;
  - the self-test pictures, taken without opening a window, are byte for byte the same as those of the Linux version.
- A person playing it in a window on a Mac, sound and controllers have not been tested yet.

### Android

`juraic-field-…-android-readme.txt`, next to the APK on the release page, explains how to install, connect and play (in
Japanese, with a short part in English).

1. Open the APK from the release page on the device and install it. The first time, Android asks you to allow apps from that source. It is for arm64 devices with Android 8 or later.
2. Play with a controller (connected by USB-C or Bluetooth) or a keyboard. There is no touch control. The on-screen keyboard appears only for typing the name and the address of a network match. The device's back button works as Esc.

- The screen is landscape.
- The bundled pictures, music and font are extracted into the app the first time it opens. This takes a moment.
- Settings, replays and your own pictures and music are in `Android/data/io.github.nikque.juraicfield/files`. You can reach that folder with a file manager app or from a PC connected by USB.
- It reads the same picture and music formats as the Linux version.
- On a phone CPU the AI's search may be narrower within its time per move, so a level may play weaker. This is expected; the levels themselves are the same.
- **It has been tested only on GitHub's build machines.** What was tested:
  - the APK builds;
  - on an emulator (x86_64, Android 14) it opens, draws the menu, keeps running for 45 seconds, opens its sound output and extracts the pictures, music and font.
- A real device, controllers, keyboards, listening to the sound, playing a match and network matches have not been tested yet.

## Playing

### The menu

| Entry | What it is |
| --- | --- |
| とこ（1 人プレイ） | building chains alone: take a move back, play the same pieces again, practise from a board file, a preview of the chain |
| 対戦（2 人） | two players on one PC |
| 対戦（人 対 AI） | a person (1P) against the AI Juraic |
| 観戦（AI 対 AI） | two AIs to watch |
| 実機の 2P を Juraic が操作 | Windows only: Juraic plays 2P of the real game through a virtual pad (see [Juraic playing 2P of the real game](#juraic-playing-2p-of-the-real-game-windows-a-trial)) |
| ネット対戦（人 対 人） | two people on two PCs, connected directly (see [Matches over a network](#matches-over-a-network)) |
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

- Controllers (Xbox ones and others) work too. You can change the bindings under 操作の割り当て in the menu.
- On Windows, PlayStation pads and the Switch Pro Controller and Joy-Con are read through SDL 3. Like Xbox pads, they are handled by button position (the bottom button rotates left, the right button rotates right).
- With the setting クイックドロップ (quick drop) on, up drops the pair at once. Up is ↑ on keyboard 1, W on keyboard 2 and up on a controller.
- With クイックドロップ off, ↑ also rotates right in solo play.

In solo play these keys also work:

- Backspace: take a move back
- R: play the same pieces again
- N: new pieces
- O: practise from a board file
- E: empty board
- Ctrl+C / Ctrl+V: copy and paste the seed
- F5: save a replay

A board file of the generator (`.jfchain`, `.jfboard`) can also be opened by dropping it onto the window.

### Settings

The settings have four parts.

- Choose a part in the list on the left with ↑↓, and open it with Enter or →.
- Inside a part, choose an item with ↑↓ and change its value with ←→. Past either end, a value wraps around to the other end.
- Any key held down repeats after a short wait.
- A grey item has no effect with the other settings as they are.

| Part | Items |
| --- | --- |
| ルール (rules) | colours (3 to 5), pieces that clear, correction of the first pairs, game speed, drop bonus, garbage rate, margin time, chain requirement, all-clear bonus, garbage that falls at once, rounds to win a set, sets to win a match, delay of a network match |
| 操作 (controls) | pre-input, diagonal input, quick drop |
| AI | search (automatic / by hand) with its width, depth, number of made-up sequences and threads, when a choice begins, its time, whether a building move may fire the main chain, the kind of AI and its level (the 2P side, and the 1P side when two AIs are watched) |
| 表示と音 (display and sound) | volume, chain preview, landing position, pictures (1P field, 2P field, the rest) and how dark, music (match, solo, menu) and its volume |

先取本数 is the number of rounds needed to win a set, and 先取セット数 is the number of sets needed to win a match. The default is one set of two rounds; both can be set up to 99.

During a match, the number won is shown between the two fields: up to five as a row of marks (★ and ◆), and beyond that as numbers ("★ won / needed").

### Replays

- When a match ends, its replay is saved as `.jfreplay` in the folder `replays` next to the executable. In solo play, F5 saves one.
- To play a replay, choose it under リプレイを見る in the menu, or drop the file onto the window. During a network match, a dropped file is not opened.
- During playback, Space pauses, holding → plays at four times the speed, and R starts again from the beginning.
- A replay is a text file that records only the seed of the pieces, the rules and the buttons of every frame. It relies on the same seed and buttons always giving the same game.

### Pictures and music

- Three pictures and nine pieces of music come with the program and are used from the start. In the repository they are in `media/`; the build copies them to `background` and `bgm` next to the executable. How they were made and their licence: [media/README.md](media/README.md).
- A background can be chosen separately for three places (the 1P field, the 2P field, the rest).
- Music can be chosen separately for three scenes (a match, solo play, the menu and similar screens).
- Your own pictures and music can be chosen in the settings once you put them in these folders next to the executable:
  - pictures: `1p`, `2p`, `other` in `background`
  - music: `versus`, `toko`, `menu` in `bgm`
- Pictures can be png, jpg, bmp or webp, and music mp3, wav, m4a, aac, wma or flac. ogg is not supported.
- Music is repeated.

## The versus AI "Juraic"

- It reads the opponent's field and the pieces it can see. It builds chains, answers attacks, and fires first when it reads a win.
- There are two kinds, with different play styles:
  - **Juraic 催促（小）**: once its field fills up, it attacks with small chains while keeping its main chain (the default).
  - **Juraic 催促（極小）**: it hardly attacks on its own, and grows its main chain before firing it.
- There are 36 levels (the default is Lv 36).
  - Lv 1 to 9: for complete beginners
  - Lv 10 to 20: for beginners
  - Lv 21 to 25: beginner to intermediate
  - Lv 26 to 30: intermediate to advanced
  - Lv 31 to 35: advanced to expert
  - Lv 36: for experts
- The lower the level, the slower it places pairs, the later it sees the opponent's field, and the less it reads ahead.
- Neighbouring levels were set by matches of 催促（小） against itself, so that the higher level wins about six rounds in ten. The levels of 催促（極小） use the same settings and were not measured.
- In matches against the AI and in AI-versus-AI matches, you can change the kind and the level of the AI with mouse clicks on the screen shown after the match. The change applies from the next match. Holding the button down on a level keeps changing it.
- It uses up to 24 CPU threads: two fewer than the logical cores of the PC, or one fewer on PCs with fewer than eight logical cores.
- Each decision is kept within 180 ms; on a slow PC the search is made narrower. After a round in which the AI was short of time, the top of the screen shows 処理落ち: AI が弱くなった可能性 ("slowdown: the AI may have played weaker").
- A measurement: against the public AI [Ama](https://github.com/citrus610/ama), made to play the same moves every time, 催促（小） at Lv 36 won 1,093 of 1,440 rounds. This was the version of 2026-10-14, with the search size used for measuring.
- The tools of that measurement are not in this repository: the program that seats Ama as an opponent, and the Ama made to play the same moves every time.
- Parts of the AI follow Ama. Which parts is listed in [licenses/Ama-NOTICE.txt](licenses/Ama-NOTICE.txt).

## Juraic playing 2P of the real game (Windows, a trial)

- In an offline two-player match of the real game running on the same PC, the program reads the real game's window, and Juraic plays 2P through a virtual pad. 1P is a person. It does not work in online matches.
- 実機の 2P を Juraic が操作 in the menu looks for the real game's window and chooses the 2P slot, the character and the control type. Once a match begins, it plays 2P.
- The kind and the level of the AI can be changed on that screen. Each time it opens, they start from those of the 2P side in the settings.
- The virtual pad needs the ViGEmBus driver. It is not included in this program, so install it yourself, as described in [Installing the virtual pad driver (ViGEmBus)](#installing-the-virtual-pad-driver-vigembus).
- **Juraic in this mode is weaker than Juraic in the matches of this program.** It knows the fields and the pairs only by reading the real game's screen, so it sometimes reads them wrong or cannot read them for a while. The pad's presses also reach the real game with a delay, and the way buttons are pressed and timed differs.
- The strength of the levels and the measurement in the section [The versus AI "Juraic"](#the-versus-ai-juraic) do not hold in this mode. The win rate against the real game has not been measured.
- The following parts for investigating bugs are built only with `JURAIC_REALGAME_DEBUG=ON` in CMake. The released packages do not have them.
  - The record of each reading, written to `realgame\<date and time>` next to the executable. It can exceed 2 GB in one session.
  - Saving a picture of the real game's screen with the S key
  - The table file `realgame_layout.txt`
  - The internal counters on the screen

### Installing the virtual pad driver (ViGEmBus)

You need it only for "Juraic playing 2P of the real game". ViGEmBus is not included in Juraic Field (its author is Nefarius Software Solutions).

1. Open the official release page: <https://github.com/nefarius/ViGEmBus/releases/tag/v1.22.0>
2. Under "Assets" at the bottom of the page, download `ViGEmBus_1.22.0_x64_x86_arm64.exe`.
3. Open the downloaded file and follow the installer. Answer "Yes" when Windows asks for administrator permission, and restart the PC if asked.
4. Start Juraic Field again and choose 実機の 2P を Juraic が操作 again.

- ViGEmBus development ended in November 2023; v1.22.0 is the last version. Do not use copies downloaded from anywhere other than the official page.
- **To check it is installed**: Device Manager shows "Nefarius Virtual Gamepad Emulation Bus" under "System devices".
- **To remove it**: Settings → Apps → Installed apps → "ViGEm Bus Driver" → Uninstall. Other software may also use ViGEmBus, so check before removing it.
- If the game says the ViGEmBus driver is not installed, start Juraic Field again after installing it. If a restart was requested, start it after the restart.
- Juraic Field does not read this virtual pad as one of its own controllers.
- Opening `仮想パッドのドライバーの入れ方.cmd` in the Windows zip shows the same instructions. It also checks whether the driver is installed.

## Matches over a network

- Two PCs are connected directly. One player chooses 部屋を立てる (open a room); the other enters that PC's address and chooses 部屋に入る (join the room).
- There is no lobby or other way to find opponents. Contact each other and exchange the address yourselves.
- The player who opens the room is 1P, and the one who joins is 2P. Both need the same version of Juraic Field.
- The rules of the player who opens the room are used: the items of ルール in the settings, and 先行入力 (pre-input) and クイックドロップ (quick drop) of 操作.
- To type the name or the address, press Enter on its row; finish with Enter or Esc. Ctrl+V pastes an address.
- The address is hidden as ● marks, so that it does not appear on a stream. F1 switches between showing and hiding it.
- A network match cannot be paused. Pressing Esc on the keyboard twice leaves the match.
- After a match, when both players press the confirm button, another match starts.

**Delay**

- Your own pair moves on the screen at once. The opponent's field is shown slightly in the past.
- Garbage (its announcement, offsetting and confirmation) takes effect later by the number of frames shown at the top of the screen as 遅延 n フレーム ("delay n frames"; 60 frames are one second).
- Offsetting follows the same rules as a match on one PC. Even if both players attack at the same moment, garbage never remains on both sides.
- n is set automatically from the speed of the connection when the two PCs connect. It can also be fixed at 1 to 30 with the setting ネット対戦の遅延（部屋を立てる側）.
- When the opponent's input arrives late, your side also stops until it arrives. If it stops for long, 通信を待っています… ("waiting for the connection") is shown.
- If nothing arrives for eight seconds, 相手との通信が途切れました ("the connection to the opponent was lost") is shown and the match ends.
- While the window is being moved or resized, the connection also stops.

### Connecting (what the one who opens the room prepares)

The connection uses UDP port 50777. This is the port that the PC opening the room listens on; you can change it with ポート（部屋を立てる時） on the network match screen or with `netPort=` in `JuraicField.ini`. The player who joins writes `address:port` only when the port is not 50777.

The player who joins needs no preparation. The player who opens the room must make sure that the other PC's traffic can reach their PC.

1. **Windows firewall**: the first time you choose 部屋を立てる, Windows may ask for permission. Press "Allow access". If you closed that dialog, open Settings → Privacy & security → Windows Security → Firewall & network protection → Allow an app through firewall, and check that `JuraicField.exe` is ticked.
2. **On the same home network**: no router setting is needed. On the screen of the opened room, press F1 to show この PC のアドレス (this PC's address, such as 192.168.…) and tell it to the other player.
3. **From different homes (port forwarding)**: on the router of the home that opens the room, add a setting that passes traffic coming from outside to UDP port 50777 on to the PC running the game. Routers call this feature by different names, such as port forwarding, port mapping, static IP masquerade or virtual server. The names and places of the router's screens differ by router, so see your router's manual as well.
   1. Find the address of the PC running the game. In a terminal (Command Prompt), run `ipconfig` and note the "IPv4 Address" (for example 192.168.1.20) and the "Default Gateway" (for example 192.168.1.1).
   2. Enter the Default Gateway address in the browser's address bar to open the router's settings. Sign in with the administrator name and password.
   3. In the port forwarding settings, register protocol UDP, port 50777 (both external and internal), and the IPv4 address from step 1 as the destination.
   4. Tell the other player not the address from step 1, but the address your home has as seen from outside (the global IP address). The router's settings show it in a field such as "WAN IP address".
   5. The other player enters that address in 相手のアドレス (the opponent's address) and chooses 部屋に入る.
4. **When port forwarding is not possible, or does not get through**: on shared lines of apartment buildings, on mobile lines, and on lines of types such as "v6 Plus" and DS-Lite that share one IPv4 address among customers, IPv4 port forwarding may be impossible, or the usable port numbers may be limited. In that case you can:
   - install a VPN on both PCs and use the addresses inside the VPN (a VPN is software that connects distant PCs as if they were on the same network);
   - use IPv6 addresses if both lines support IPv6 (the router's IPv6 filter may need a setting that lets UDP 50777 through);
   - swap who opens the room and who joins.

Do not show your global IP address on a stream or in public. After playing, it is safer to remove the setting you added to the router.

## Chain board generator

A program that randomly generates boards that fire exactly the requested chain when one last pair is placed: 1 to 19 chains on 6 × 13 fields and 1 to 47 chains on 10 × 18 fields, in three to five colours. Each board found is appended to `19chain_urls.txt` as a URL for external chain simulators. The shipped settings generate 100 boards of a 19-chain with 76 pieces in four colours.

- **Windows GUI**: `JuraicFieldGenerator.exe` — edit settings, generate and cancel, choose results, play back chains, edit boards. Usage: [GUI_README.md](GUI_README.md) (Japanese).
- **CLI**: `random_19_chain.exe` (`run.cmd`); also included in the Linux version.
- URL formats: [URL_FORMATS.md](URL_FORMATS.md)

The conditions the boards satisfy, how the search works, the settings and the generation speed are in [GENERATOR.en.md](GENERATOR.en.md).

## Build and test

How to build (Windows, Linux, Mac, Android), how to run the tests, and the folder layout: [BUILDING.en.md](BUILDING.en.md).

## License

[MIT License](LICENSE) © 2026 Nikque

- The bundled pictures and music (`media/`) can be used under the same terms. How they were made is in [media/README.md](media/README.md).
- Parts of the versus AI follow [Ama](https://github.com/citrus610/ama) (MIT License, Copyright (c) 2023
  citrus610). Which parts is listed in [licenses/Ama-NOTICE.txt](licenses/Ama-NOTICE.txt), and Ama's licence is in
  [licenses/Ama-LICENSE.txt](licenses/Ama-LICENSE.txt).
- The Linux version contains SDL 3 (zlib License), stb (MIT or public domain), dr_libs (public domain or MIT-0)
  and BIZ UDPGothic (SIL Open Font License 1.1). Their licences are in `licenses/`.
- The Windows version also contains SDL 3, for reading controllers.
- The Windows version contains ViGEmClient (MIT License) for the virtual pad of the mode in which Juraic plays 2P of the real game. The driver ViGEmBus (BSD-3-Clause) is not included; the user installs it.
- No warranty. The author is not responsible for anything that happens from using this program.
