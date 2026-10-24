# ビルドとテスト

Visual Studio 2022 などの C++20 コンパイラーと CMake 3.16 以降を使います。x64 では SSE4.1 と POPCNT を使います（PEXT/PDEP は実行時に CPU を判定して使い分けます）。それ以外の CPU 向けに 64bit 演算だけの実装も入っています（`core/field_bits.h`）。盤面ジェネレーターの GUI は Windows（MSVC）だけです。ゲーム本体は Windows では Direct2D・XAudio2 で、Linux では SDL 3 で動きます（下）。ジェネレーターの CLI、AI のツール（`ai_match` など）、盤面コア・ゲーム・AI・通信のテストは GCC と Clang でもビルドできます。macOS では、ゲーム本体はアプリ（`JuraicField.app`）になります。GitHub Actions の `.github/workflows/macos.yml` が、Apple シリコンの Mac でビルドとテストを行います。Android 版は `android/`（Gradle）で作ります。SDL 3 の配布物を展開したフォルダーを `-PsdlSource=` で渡し、SDL の Android 用の部品（Java と Gradle のラッパー）もそこから使います。手順は `.github/workflows/android.yml` にあります。

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cd build/Release
.\JuraicField.exe
```

性能測定は必ず **Release** で行ってください。ビルドすると、`media/` の絵と曲が exe の隣（`background`・`bgm`）に写されます。

Windows では、SDL 3 があると、PlayStation のパッドと Switch の Pro コントローラー・Joy-Con をそれで読みます。これらのパッドは、ほかのソフトに頼まれると独自の形で送るようになり（Pro コントローラーは USB では必ず）、Windows の汎用の読み方では、押していない方向やボタンが押されているように読めてしまうためです。SDL 3 が無くてもビルドはでき、その時はこれらのパッドも汎用の読み方で読みます。SDL 3 は静的ライブラリーとしてビルドし、`CMAKE_PREFIX_PATH` で指します。

```powershell
# SDL 3（https://github.com/libsdl-org/SDL/releases の SDL3-3.4.18.tar.gz を展開したフォルダーで）
cmake -S . -B build -A x64 -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_INSTALL_PREFIX=C:/sdl3
cmake --build build --config Release
cmake --install build --config Release

# Juraic Field
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/sdl3
```

Linux では、SDL 3 があればゲーム本体もビルドされます（無ければゲーム本体だけを飛ばします）。SDL 3 は静的ライブラリーとしてビルドしておき、`CMAKE_PREFIX_PATH` で指します。絵・曲・文字の読み込みに使う 1 ファイルのライブラリー（stb_image・stb_truetype・dr_flac・dr_mp3・dr_wav）は `third_party/` に入っています。

```bash
# SDL 3（https://github.com/libsdl-org/SDL/releases の SDL3-3.4.18.tar.gz を展開したフォルダーで）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_X11_XTEST=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_PREFIX=$HOME/sdl3
cmake --build build && cmake --install build

# Juraic Field
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$HOME/sdl3
cmake --build build
ctest --test-dir build --output-on-failure
./build/JuraicField
```

Ubuntu 22.04 で SDL 3 のビルドに要る開発用パッケージ: `cmake ninja-build pkg-config libasound2-dev libpulse-dev libpipewire-0.3-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxkbcommon-dev libwayland-dev libdecor-0-dev libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev libdbus-1-dev libudev-dev`。Linux では、ビルドするとフォント（`media/fonts`）も `fonts` に写されます。

| テスト | 内容 |
| --- | --- |
| `core_and_generator`（SSE版・移植版） | 乱数盤面（空洞・おじゃま・13段目を含む）でビットボードの消去・落下・得点をセル単位の参照実装と照合。逆操作の候補を v0.3.0 の全連鎖判定実装（`tests/reference/`）と照合 |
| `game_rules`・`game_independent_simulation` | 手単位のゲームの決まり（ツモ、置き方、連鎖の波、得点、おじゃまの受け渡し）をセル単位の実装と照合し、同じ記録を独立した Python のシミュレーターでも確かめる |
| `game_frames` | フレーム単位のゲーム（落下と固定のフレーム数、先行入力、置ける場所）を、測った表と総当たりの入力で確かめる |
| `game_linked_rounds` | ネット対戦の「遅れて効くおじゃま」: 2 つの席をどの順で進めても、帳簿 1 本の参照実装と毎フレーム同じになること。おじゃまが両方に残らないこと |
| `net_match_over_a_line` | 遅延・欠落・追い越しを起こす回線（プログラムの中の偽の回線）で、2 つの端が同じ対戦になること。版違いの拒否、退出、切断、再戦。決まりどおりでないデータ（部屋に入れない相手の後の次の相手、先のフレームの送りすぎ、範囲の外の遅延、時刻として成り立たない値）の扱い |
| `net_udp_on_this_machine` | 本物の UDP ソケットを、この PC の中だけ（`::1`）で使う: 長すぎるデータグラムを読み飛ばすこと、部屋に入れない相手や関係のないデータの後でも次の相手が入れること、対戦中にほかの送り主のデータグラムが効かないこと、1 フレームに読むデータグラムの数に上限があること |
| `replay_files` | リプレイのファイルの読み込み: ゲームが書いたものがそのまま読めること。ネット対戦のリプレイで遅延が無い・範囲の外のもの、途中で切れたファイルを断ること |
| `ai`・`ai_match_same_result`・`ai_toko_same_result`・`ai_tune_same_result` | 対戦 AI（探索、読み、判断、時計、レベル、設定のスイッチ）と、AI のツールの自己テスト |
| `all_chain_lengths` | 1〜19連鎖 × 3/4/5色 × 余剰（0個・1個・上限・0〜上限）の228設定で生成し、全盤面を独立検証 |
| `grid_field_and_search` | 10列×18段にも使う盤面の型と探索（`generator/grid_field.h`・`grid_generator.h`）: 6列×13段で、今の盤面コアと消去・落下・得点・到達規則・逆操作の候補が一致し、試行ごとに今の探索と同じ盤面を作ること。10列×19段で、乱数盤面の消去・落下をセル単位の参照実装と照合し、隠れた19段目・負けの位置・到達規則と生成した盤面を確かめる |
| `all_chain_lengths_10x18` | 10列×18段で1〜47連鎖 × 4/5色 × 余剰（0個・1個・上限・0〜上限）と、3色の1〜47連鎖 × 余剰（0個・1個・0〜上限）で生成し、全盤面を独立検証 |
| `config_and_generation_integration` | 設定の読み取り・不正値の拒否・履歴追記・重複除外・スレッド数によらず同じ出力（10列×18段の盤面と、書き換えた盤面を独立検証が見抜くことを含む） |
| `independent_output_verification` | 同梱の生成例を独立検証 |
| GUI関連（6 本） | 設定・保存形式・再生・編集・特殊ピース・実ウィンドウでの統合検証と、その成果物の独立検証、コンソールの文字コード |

ゲーム本体には、ウィンドウを出さない自己テストがあります。

```powershell
.\build\Release\JuraicField.exe --screenshot shot.png 0        # メニューの画面を 1 枚保存して終了（BGM が鳴ることも確かめる）
.\build\Release\JuraicField.exe --screenshot shot.png 3003     # 台本どうしの対戦を保存し、リプレイで同じ結果になることを確かめる
.\build\Release\JuraicField.exe --screenshot shot.png 30008    # 偽の回線でのネット対戦: 両端とリプレイ（--check-replay と同じ再生）が同じこと、入った側が立てた側のルールで打つことを確かめる
.\build\Release\JuraicField.exe --check-replay replay.jfreplay # リプレイをウィンドウなしで再生し、各ラウンドの結果を書き出す
```

ジェネレーターの出力は、独立した Python の検証器でも確かめられます。

```powershell
python tests/verify_output.py tests/fixtures/solutions.log --allow-extra
.\build\Release\random_19_chain.exe config.ini > generator.log
python tests/verify_output.py generator.log --allow-extra
```

## フォルダー構成

| 場所 | 内容 |
| --- | --- |
| `core/` | 盤面コア（ヘッダーのみ）: `field_bits.h`（96bitの集合）、`bit_field.h`（盤面・消去・落下・得点）、`reach.h`（到達規則）、`rng.h`（乱数） |
| `game/` | ゲームの決まり（ヘッダーのみ）: ルール、フレーム数、ツモ、操作、1 人ぶんの進行、対戦の 1 ラウンド、ネット対戦用の「遅れて効くおじゃま」 |
| `ai/` | 対戦 AI（ヘッダーのみ）: 探索、盤面の評価、読み、判断、時計、レベル、設定 |
| `play/` | ゲーム本体: 画面、音、入力、設定、リプレイ、通信。Windows の部分（Direct2D・XAudio2・Media Foundation・Win32）と Linux の部分（SDL 3、自前の描画 `raster.h`）は `#ifdef` で分かれています |
| `generator/` | 逆向きビーム探索、最後の1組の判定、全条件検査、設定、並列実行。10列×18段の盤面の型と探索（`grid_field.h`・`grid_generator.h`・`grid_runner.cpp`） |
| `app/` | CLIの入口とコンソール出力 |
| `gui/` | 盤面ジェネレーターの Windows GUI、エディター、URL出力、結果ファイル |
| `media/` | 同梱の絵と曲、その作り方の記録、Linux 版のフォント（`fonts/`） |
| `third_party/` | Linux 版が使う 1 ファイルのライブラリー（stb・dr_libs）、Windows 版が仮想のパッドに使う ViGEmClient と、そのライセンス |
| `tests/` | テスト。`tests/reference/v030_engine.h` は照合用に残した v0.3.0 のエンジン |
| `tools/` | AI どうしの対戦（`ai_match`）、1 人での探索の測定と調整（`ai_toko`・`ai_tune`）、ベンチマーク、到達規則の比較、配布物の作成 |
| `examples/`・`benchmarks/` | 盤面ジェネレーターの例と、その速度の測定の記録 |
| `licenses/` | Ama のライセンスと、AI のうち Ama に倣っている所の一覧。Linux 版に入るもの（SDL 3・stb・dr_libs・BIZ UDPゴシック。SDL 3 は Windows 版にも）と、Windows 版に入る ViGEmClient のライセンス |
