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
