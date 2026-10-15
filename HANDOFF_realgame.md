# 引き継ぎ: 実機の 2P を Juraic が操作（枝 realgame-2p）

この資料は作業の引き継ぎ用です。`main` に取り込む前に消してください（製品の文書ではありません）。

## 1. 今の状態

- 枝 `realgame-2p`、先頭 `747a950`（この資料のコミットを除く）。`main`（0.9.1 = `2781bf6`）から 13 コミット。
- 第 1 段（画面を読んで表示）・第 2 段（仮想のパッドで 2P を操作）・第 3 段（打ち直し・遅れ・レベル）まで、コードは全部入っている。
- **Windows ではまだ一度もビルドしていない**（クラウドの Linux で作ったため）。実機でも一度も動かしていない。

## 2. ファイル

| ファイル | 中身 | OS |
| --- | --- | --- |
| `play/realgame_read.h` | 画面の 1 枚を表（Layout）で読む。両側の盤面、操作中の組、NEXT・NEXT2、おじゃまの予告。「不明」の点（光・煙など）。Tracker（フレームを追う、新しい組の検出、遅れて見えた組）。observeSpawn / observeWith（AI に渡す Observation） | 共通 |
| `play/realgame_follow.h` | Follower。組の置き方をゲームの 22 通りから見つけ、連鎖・おじゃまをエンジンで計算。次の組で画面と照らす。予告を計算の上限と照らす。14 段目は追った置き方から持つ | 共通 |
| `play/realgame_drive.h` | Driver。AI が 2P を操作。自分の組をエンジンで追い、遅れ（lag）を見込んでボタンを送る。画面と食い違えば置き直して道を引き直す（打ち直し）。遅れを画面から測る | 共通 |
| `play/realgame_capture_win.h` | Windows Graphics Capture で、選んだ窓の中身だけを取り込む | Windows |
| `play/realgame_pad_win.h` | ViGEmBus の Xbox 360 パッド（ViGEmClient） | Windows |
| `play/realgame_win.h` | モードの状態（行、読み取り、操作、パッド） | Windows |
| `play/main.cpp` | モードの画面（updateRealGame / drawRealGame / chooseRealGame） | Windows |
| `tools/realgame_read.cpp` | 保存した画像（BMP・JPEG・PNG）を表で読む道具。`--points` で読んだ点を描いた画像、`--write-layout` で組み込みの表をテキストに | 共通 |
| `tests/realgame_tests.cpp` | ctest `realgame_screen_read` | 共通 |
| `third_party/ViGEmClient/` | ViGEmClient（MIT、2023-09-08 の最後の版）。ライセンスは `licenses/ViGEmClient-LICENSE.txt` | Windows |

既存のファイルで変えた所:

- `ai/agent.h`: `PlacementAgent::steer`（道の引き直し）と `targetPlacement` を足しただけ。既存の動きは同じ。チェックサム 4 本は同じだった。
- `play/pad.h`: `Pads::skipXInput`（自分の仮想のパッドを読まない。-1 の間は今までどおり）。
- `CMakeLists.txt`: MSVC のゲームに `vigem_client`（setupapi）・d3d11・dxgi・dwmapi。`realgame_read` の道具、`realgame_tests` の試験。
- `README.md` / `README.en.md`: ライセンスの行に ViGEmClient。

## 3. 確かめたこと（Linux、g++ 13）

- ビルドで新しい警告は 0（新しいファイルは `-Wextra -Wconversion -Wshadow` でも g++・clang++ とも 0）。
- ctest は 17 本中 16 本。落ちる `net_udp_on_this_machine` は作業前から（クラウドに IPv6 が無いため）。
- チェックサム 4 本は作業前と同じ（`ef98990a96e40722` / `f5c959723b0be6f3` / `96c196b773816620` / `ff2131250cdf9c0b`）。
- 試験の主な結果:
  - Juraic Field の対戦を 4 つの大きさで描いて読み、元と同じ。新しい組は 380 回とも正しいフレームで見つかった。
  - 置き方が分かった組は 379 組中 375 組。計算した盤面と画面の照らし合わせは 363 回すべて合った。
  - 2P を AI が「遅れるパッド」越しに操作した: 遅れが設定どおりなら 114 組すべてが選んだ所に置かれ、測った遅れも設定どおり。時々 1 フレームずれるパッドでは 108 組中 105 組。
- 実機の画像 12 枚（チャットで受け取った。リポジトリには入れていない）を道具で読んだ:
  - 遊んでいる最中の画像は、盤面・落ちている組・NEXT・予告が見たとおり。
  - 光の輪・煙・連鎖の光の下では、無い組を読むことがある。その時は「不明」の点が出て、その画面は使わない作り。
  - 表は組み込み（`measuredLayout`）。測った画像は 2000×1125 で届いたもの（元は 2272×1278 で保存、窓の中身は 2240×1260、同じ 16:9）。
- mingw-w64 で `play/realgame_pad_win.h` と ViGEmClient をコンパイル・リンクできた。ViGEmClient は大文字の `Windows.h` / `SetupAPI.h` を読むので、Linux では別名を足した。MSVC では要らない見込み。

## 4. Windows でやること（この順で）

1. `git fetch origin realgame-2p` して取り込む。
2. ビルドと `tidy_verify2.ps1`。
   - 一度もコンパイルしていない所: `realgame_capture_win.h`（C++/WinRT の Windows.Graphics.Capture）、`realgame_win.h`、`main.cpp` のモードの部分。
   - ViGEmClient の `.cpp` が警告を出すかもしれない（第三者のコード）。出たら、警告を抑える方法を決めてから直す。
   - `IsCursorCaptureEnabled` は Windows SDK 10.0.19041 以降が要る。
3. 自己テストの画像: −7（モードの画面）は行が増えたので変わる。ほかの 19 枚は同じはず。
4. ViGEmBus のドライバーを入れる（ユーザー）。
5. 実機で試す。
   1. 実機をオフラインの 2P 対戦の入口まで進める。
   2. Juraic Field のモード「実機の 2P を Juraic が操作」で「読み始める」を選び、10 秒以内に実機の窓をクリックする。
   3. 「パッドを手で押す」で、実機のにんずうセレクトの 2P に仮想のパッドを選ぶ（矢印・Z=A・X=B・Enter=Start・Backspace=Back、Esc で終わり）。
   4. 「2P を操作する」を選ぶ。
   5. 画面の「画面から測った遅れ」に「遅れ」の行を合わせる。回転が逆なら「回転ボタン」を切り替える。
6. モードの画面の数字を見る。
   - 「計算した盤面と画面の照らし合わせ」で合わない回数が多い: 読み違い。
   - 「止めた予告」が多い: 予告の読みか計算がずれている。
   - 「画面で位置を直した回数」が多い: 遅れが合っていないか、エンジンと実機の時間がずれている。
   - 「読んだ画面」が 1 秒に 60 枚近いか。
7. うまく読めない場面があれば、S キーで実機の画面を保存し、`realgame_read 画像 --points 印付き.bmp` で表と合っているか見る。表は `realgame_layout.txt`（ユーザーのフォルダー）で差し替えられる。`realgame_read --write-layout realgame_layout.txt` で組み込みの表が書き出せる。

## 5. 見込みが外れるかもしれない所

- 実機が、窓が後ろにある間もパッドの入力を受けるか。「パッドを手で押す」の間は Juraic Field の窓が前に出ている。
- 仮想のパッドが XInput の 1 番になった時、実機が 1P に割り当てないか。
- Windows Graphics Capture の画像に窓の枠が入る。窓の中身だけを切り出す計算（`DWMWA_EXTENDED_FRAME_BOUNDS` とクライアント領域の差）が実機の窓で合うか。
- 新しい組が出た瞬間に光がかかると、組を見つけるのが遅れる。光が消えてから 20 フレーム以内に両方の粒が見えれば数えるが、それ以外は数え漏れになる。
- 緑の光の輪のような、組の色に近い光は「不明」にならず、組と読む（動いている間は使わない）。
- 表の色・位置は 2 つのステージの画像だけで測った。別のステージや別のキャラクターの背景で外れることがある。
- 第 2 段は、新しい組が画面に出てから AI が考え始める（実機より考える時間が短い）。NEXT が動き出す所から考え始める改良はしていない。

## 6. ユーザーが決めること

- 「遅れ」の最初の値（今は 2）、回転ボタンの向きの最初の値（今は A で右回転）、窓を選ぶ待ち時間（10 秒、前に 0.5 秒）、新しい対戦とみなす空の時間（30 フレーム）。
- 版 1.0 にすることと公開。CHANGELOG と README のモードの説明（まだ書いていない）。
- この資料を消す時（`main` に取り込む前）。

## 7. 守ること（最初の指示から）

- 報告は日本語。コミットの文は英語で、最後の行は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。
- push は枝 `realgame-2p` だけ。`main`・`linux`・タグには push しない。`--tags`・`--all` は使わない。リリースはしない。版の番号は変えない。
- Windows 版のほかの動きと、Linux・Mac・Android 版の動きを変えない。AI の手（既定）とレベルは変えない。リプレイの版は 14 のまま。
- 確かめ: チェックサム 4 本（`g++ -std=c++20 -O2 -msse4.1 -mpopcnt -pthread tools/ai_match.cpp -o ai_match`、共通の引数 `--pairs 20 --threads 8 --seed 71 --param beamWidth=40 --param beamDepth=10 --param beamQueues=3`。`--a x3 --b x3` / `--a x3 --b random` / `--a x3 --b x3` に `--param-b evFireHeight=300 --param-b x3PokeGain=75 --param-b xWildChance=0 --param-b xWildEvery=2 --param-b x3PokeNet=0 --param-b x3PokeFrom=0 --param-b x3KeepMain=85 --param-b x3Crowd=1000` / `--a x3 --b x3 --param-a x3BuildHold=1`）、ビルドと ctest、警告 0。
- 画面・文書・ソースの注記・コミットの文に、元のゲームの名前とその版を指す言葉を出さない。「実機」「今の版」と言う。
- ソースの注記には、コードが何をするか・値の理由になる測った数字だけを書く（日付つきの経緯は書かない）。英語、文の形。
- 実機のメモリーを読む・書く、実機のファイルを書き換える方法は使わない。キーボードの合成は使わない。実機にキーボード・マウスの入力は送らない。
- 自分で気付いた不具合は、落ちる試験を先に書いてから直す。
- 対戦の測定（`ai_match` を多く回すもの）は流さない。
- 判断がユーザーのものになる所は決めずに聞く。

## 8. 別件: DualShock 4 が 2 台に見える件（コミット `cc3377f`）

- 原因（診断のログから）: 1 台の DualShock 4 が、XInput（別のプログラムが Xbox のパッドとして見せている）と SDL（0.9.1 から）の両方で読まれ、2 台に見えていた。2 人の対戦では 1 台で両方の席を操作していた。2 つの読みが食い違い、SDL 側だけが方向を 2.5 秒ほど押しっぱなしで返すことが 2 回あった。
- 直したこと（`play/pad.h` の `Pads::sdlWanted`）: Windows が Xbox の並びで一覧に出し、XInput がその数だけパッドを持っている時は、その種類のパッドを SDL で開かない。それ以外は 0.9.1 のまま。
- 試験: `tests/pad_tests.cpp` に規則の試験を足した（Windows で SDL 付きのビルドの時だけ動く）。**まだ一度も走らせていない**。
- Windows で確かめること: ビルドと pad_tests。DualShock 4 をつないで、診断の道具かゲームの「コントローラー N 台を認識しています」が 1 台になるか。2 人の対戦で 1 台が両方の席を動かさないか。方向が勝手に押されたままにならないか。
- この修正は枝 `realgame-2p` に入れた（push してよい枝がここだけのため）。`main` に先に入れるかどうかはユーザーが決める。
- 試した結果: 0.9.1 にこの修正だけを入れた試験版（Linux の mingw-w64 で作ったもの。C++/WinRT の代わりに mingw の Windows.Gaming.Input を呼ぶ短いコードを使った。リポジトリには入れていない）を試した人から「正常に動いた」と報告があった。細かい結果（台数の表示以外）はまだ聞いていない。MSVC のビルドではまだ試していない。
  - 最初の試験では古い exe が開かれていて「変わらない」と報告された。今の版は画面にも窓の題名にも版の番号が出ないため、気付けなかった。試験版の印（窓の題名とタイトル画面の 1 行）を付けて分かった。正式の版に版の番号を出すかどうかはユーザーが決める。
  - 診断で分かったこと: この PC の Windows は、XInput に何も無い時も DualShock 4 を Xbox の並び（Gamepad）として一覧に出す。だから「Xbox の並びで一覧に出る」だけでは XInput が読んでいるとは言えず、`sdlWanted` は XInput の台数と比べている（XInput が 0 台なら SDL で読む）。

## 9. 別件: 対戦の始めに入力が入らないことがある件

- 報告: 0.9.1 で、対戦を始めた時に、たまに入力が入らない。
- 原因（コードから）: 0.9.1 の「ゲームを始めた時に押していたボタンは、一度離すまで効かない」（`HeldAtStart`、`65372c8`）は、ボタンを読んだ最初のフレームで押されていたものを止める。対戦では始めのカウントダウン（`Versus::Ready`）の間ボタンを読まないので、カウントダウンの間に押し始めたボタン（先行入力）も、離すまで止められていた。とこ・ネット対戦は始めからボタンを毎フレーム読むので、この問題は無い。
- 直したこと: カウントダウンの間も、人の席のボタンを `HeldAtStart::watch` で見る（ゲームには渡さない）。メニューから押し続けているボタンは今までどおり離すまで効かない。カウントダウンの間に離して押し直したボタンと、新しく押したボタンは、対戦が動き出したフレームから効く。
- 試験: `tests/pad_tests.cpp`（Windows の MSVC のビルドだけで動く。mingw でビルドして Wine で通した）。
- 0.9.1 にこの修正と DualShock 4 の修正を入れた試験版（試験版 3）を作って渡した。まだ試した結果は聞いていない。CHANGELOG にはまだ書いていない（版をどうするかはユーザーが決める）。
