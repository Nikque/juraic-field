# 引き継ぎ 9: クイックターンの後の空押し、13・14 段目の見立て、拍の合図（枝 realgame15-wip・realgame-2p）

この資料は作業の引き継ぎ用です（製品の文書ではありません。`main` に取り込む前に消す）。前の資料は `HANDOFF_realgame8.md`（「資料 8」）、実機のモードの直しの一覧は `FIXES_realgame.md`。

## 1. 今の状態（2026-10-22）

- 枝 `realgame15-wip` と `realgame-2p` は同じ物（`3dc659d` の上にこの資料のコミット）。`realgame-2p` は `3bc5eb5` からの早送り。`main` とタグは変えていない（公開した最新は v0.9.6.1 のまま）。
- この資料の窓はクラウドで作業した。クラウドでは実機の記録、WSL の道具、Windows の組み立てが使えないので、そこでしかできない確かめは 4 節にまとめた。
- 試験の時間: `tests/realgame_tests.cpp` は Linux（g++ -O2）で 38〜62 分（ほかの組み立てと並べて流したとき）。

## 2. 入れたもの

| コミット | 部品 | 中身 | 試験 |
|---|---|---|---|
| `5077afb` | Driver・Tracker の時間の決まり | 実機が止まった後の自由落下（`shownInAir`・`overdue`・`resume`）、実機のモードの `quickTurnLock` 7（`realGameTiming`）、`InputPlanner` の状態の鍵の `quickLock` を 4 ビット | `testPausedRound`・`testTurnAfterQuickTurn` |
| `f11d70c`・`90e6924` | AI の押し方・Driver | クイックターンのロックの間は回しを押さない（`InputPlanner::waitQuickLock`、`Driver::prepare` で入れる） | `testWaysInQuickTurnLock`、`testTurnAfterQuickTurn` に光 50・長さ 3・種 121 の回戦 |
| `2a3ddc1` | 対戦（実機のモードではない） | 戦の前の拍の合図: 低い拍 2 つと高い拍 1 つを 30 フレームおき、高い拍が最初の組の出るフレームで聞こえるよう 2 フレーム前に鳴らす。対戦・ネット対戦・アリーナ、観戦では鳴らさない（`play/start_beat.h`、`play/audio.h` の `SND_BEAT`・`SND_BEAT_GO`、`play/main.cpp`） | `frame_tests` の `testStartBeat` |
| `0dd7b39`・`a577dc4` | Follower・Driver | エンジンの 13・14 段目を AI に見せるものと同じにする（`Driver::shareHidden`）、食い違う升を「分からない」とする（`unsure13`・`unsure14`、書き置き `not sure`）、12 段まで埋まった列の 13 段目に入ったお邪魔を推し量る（`Follower::withGarbage`、`SideModel::unsure`） | `testGarbageIntoRow13`、`testUnseenPairsUnderGarbage`・`testModeUsesThirteenthRow` に 13・14 段目の数え |
| `79b1bf5`・`3dc659d` | AI の押し方・Driver・読み（Tracker） | 分からない升に左右されない道（`InputPlanner::planSure`）、絵で分からない升を知る（`Driver::explain`、書き置き `the picture tells`、`found`）、逆さの組（子だけ見え、軸が 13 段目）を手の中の組として読む（`readPair` の `upsideDown`、`PairSeen::axisHidden`） | `testSureWays`・`testUpsideDownPair`・`testUnsureCells` |

細部（元になった数、測った数）は `FIXES_realgame.md` の 2 節の 10-22 の行と、4.3 の系列。

## 3. クラウドで確かめたこと

| 確かめ | 結果 |
|---|---|
| `g++ -std=c++20 -O2 -pthread -I. tests/realgame_tests.cpp -o rt && ./rt .` | 通過（`3dc659d` のソース） |
| 写し（`DriveScenario` の `noiseStream` を 17・27・37 にした物、check が投げない形） | 3 つとも落ちた check なし。`79b1bf5` の前と比べ、組の外れは合わせて増減なし（乱れ 17 の揺れるパッドの形 10 → 9、乱れ 37 の光・長さ 3 の回戦 0 → 1） |
| cmake（Release）と ctest | 20 本中 19 本通過。`net_udp_on_this_machine` はクラウドのコンテナに IPv6 が無く `::1` で待てないため落ちる（直したファイルを含まない） |
| SDL のゲーム本体（Linux、SDL 3.2.24 を作業場で組み立てた物） | `play/main.cpp` を含め組み立てられた（警告は前からの `ai/gaze.h` の物だけ） |
| 13・14 段目の見立て（作り物の対戦、組を始める時点で本当の盤面と比べた升、乱れ 4 通り） | クイックターンの回戦 1,102 升でエンジン 108 → 41、お邪魔の回戦 188 升で 120 → 23 |
| 分からない升の上の道（`testSureWays`、高い盤面 300 枚） | 2,781 本: そのまま着く 2,043、替えた 38、調べる道 0、見えて分かるが狙いに届かない 319、どれも無い 381 |
| 作り物の対戦での `explain` | どの流れでも 0 回（分からない升が効くのは、組が見えない段にいるときがほとんど） |

## 4. 確かめていないこと（ローカルでしかできない）

| 確かめ | わけ | 道具（前の窓の物） |
|---|---|---|
| 全記録の流し直し | `79b1bf5` で Tracker の読み（`readPair`）、`0dd7b39` で Follower を変えた。資料の決まり（4.5）: 数えた組が減らないこと、両側の並びの一致 | `records11.sh` |
| 13 段目で動いた組の数え | `5077afb` の時点で 25 記録の 13 段目で動いた組 54 組中 25、低い組 3,294 組中 3,136 がエンジンの置き場と同じ。今の物でいくつか | `order\w15\rgclimb2` |
| 書き置きの数 | 実機で「分からない」升がどれだけ出て（`not sure`）、絵で分かったか（`the picture tells`） | 記録の流し直しの `events.txt` |
| 放置（自由落下） | Tracker の読みを変えたので、下ボタンの決まりの確かめ（4.2）と同じく `rgidle` の E・F | `rgidle` |
| 16 通りの表 | `5077afb` で 437 組中 430。空押しの直しで 1 組戻る見込み | WSL の確かめの台本（資料 8 の 1 節: `checks_zl.sh`・`verify_zl.sh`。次の名前は前の窓の記録で決める） |
| Windows の組み立てと確かめ | X3 のチェックサム 4 つが `checksums_x3only_d20.txt` と同じこと（`ai/input.h`・`ai/agent.h` を変えた。新しい設定はどれも既定で切りなので、ふつうの Juraic は変わらないはず）、`ALL OK` | `bench\rg2p-tools\final_rg49.ps1` の写し（次の名前は前の窓の記録で決める） |
| デバッグの組み立て | 作り直し | `C:\puyowork\build-rg2p-debug` |
| 拍の合図の聞こえ方 | 2 フレームの見込み（`StartBeat::LEAD`）が PC の音の遅れに合うか。音量 | Windows の組み立てで対戦を始める |

## 5. 未決のこと

- `planSure`・`explain`（分からない升の道、絵で知る）は作り物の対戦では外れを変えなかった。記録で `the picture tells` がほとんど出ず外れも変わらないとき、残すか外すか。
- 見えない組がお邪魔で蹴られる外れの直し方。
- 13 段目の覚え（Follower）の外れ: 確かめられた回の 3 割ほど（資料の 4.3）。今回の見立ての一本化と推し量りの後、記録でどれだけ残るか。

## 6. ローカルの確かめの結果（2026-10-22）

対象のソースは `6e5963a`（`7ae9b78` に試験の変数の名前の直し 1 つ）。記録は 26 個（`build\Release\realgame` と `build-rgdebug\Release\realgame`）と、新しい `20261022-123849`。

| 確かめ | 結果 |
|---|---|
| Windows `rg51`（`final_rg50.ps1` の写し） | Release の組み立て警告 0、`realgame_tests` 通過、パッド 77、X3 のチェックサム 4 つが `checksums_x3only_d20.txt` と同じ、`ai_tests`・`verify_build`（43 行）・ctest 37 本（`net_udp_on_this_machine` を含む）: `ALL OK (rg51)` |
| デバッグの組み立て | 警告 0。`JuraicField.exe` 10-22 12:22:53（台本の中の 1 回は、ゲームを動かしている間で `media` を写せず exit 1。閉じた後に流し直して通った） |
| 試験の組み立て（MSVC） | `testUpsideDownPair` の変数 `far`（Windows のヘッダーの空のマクロ）で error C2513。`away` に替えた（`6e5963a`） |
| WSL `zn` の写し 7 つ（base・A・B2・AB・salt17・salt27・salt37）と本体 | すべて FAILED 0、本体 exit 0 |
| 16 通りの表 `zn` | 444 組中 439（`zm` は 437 組中 430）。お邪魔なし・光 50・種 121 は 37 組中 36 → 43 組中 43、光 120・種 121 は 39 組中 38 → 41 組中 41。残る外れ 5 はお邪魔 6 の回戦 |
| Tracker の道具 4 つ（`rgspawns`・`rgspawns1p`・`rgnum`・`rgwords`）を全 26 記録で `5077afb` と `6e5963a` で流す | 出力が 1 行も違わない（数えた組は減らず、両側の並びも同じ） |
| `rgidle`（同じ比べ） | E・F の数は全記録で同じ。違いは 2 記録の B・A の行の細部（`20261016-234223` の B の落ちたフレーム 19 → 20、`20261021-111913` の A の「手の中の組」なし → あり） |
| Driver の流し直し（`rgdrive15`・`rgdrive16`）の F | 合わせて 20 → 20（`20261016-195818` 19、`20261017-022810` 1）。始め直し 94 → 95 |
| 13 段目で動いた組（`rgclimb2 --var lock7:qtl=7`、25 記録） | `5077afb` 54 組中 25 → `6e5963a` 53 組中 26。下だけの組 15 組中 11 → 16 組中 12。低い組 3,294 組中 3,136 のまま。既定の時間の決まり（ロック 3）の形では 17 → 19 |
| 流し直しの書き置き（`rgdrive16`、26 記録） | `not sure` 361 回（288 組）、`the picture tells` 0 回、`explain` 0 回。`not sure` の組で記録の狙いと置き場が違った組は 288 組中 114（40 %）、ほかの組は 3,348 組中 814（24 %）。狙いは 2 度目の選びでも変わるので、この違いは置きミスの数ではない |
| 新しい記録 `20261022-123849`（`7ae9b78` のデバッグ版、Lv 36、16 戦） | 書き置き: `not sure` 42、`the picture tells` 0、始め直し 0、位置直し 18。`rgidle`: E 1（35130428〜35130504、読めた絵 2 枚、玉 1 つだけの形）、F 0。流し直し: 590 組、F 0、押しの一致 91.3 % |
| 拍の合図 | 戦の前・後の待ち（90・150 フレーム）を決めるファイルは 0.9.6.1 から変わっていない。`frame_tests` で高い拍は最初の組の出る 2 フレーム前（戦の 97 フレーム目に組が出る）。聞こえ方と音量は利用者の確かめ |

確かめていないこと: `20261022-123849` の E 1 つが自由落下かどうか、その記録の置きミスの中身、拍の合図の聞こえ方。
