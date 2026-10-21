# 引き継ぎ 8: 0.9.6.1（コントローラーの直し）の後（枝 realgame-2p）

この資料は作業の引き継ぎ用です（製品の文書ではありません。`main` に取り込む前に消す）。前の資料は `HANDOFF_realgame7.md`（「資料 7」）、実機のモードの直しの一覧は `FIXES_realgame.md`。

## 1. 今の状態（2026-10-21 夜）

- 公開した最新: **v0.9.6.1**（https://github.com/Nikque/juraic-field/releases/tag/v0.9.6.1 、10-21 22:32、Latest。Mac の zip は 22:4x に足した）。配布物は `C:\puyowork\artifacts\v0.9.6.1\`、本文は `C:\puyowork\artifacts\release-notes-v0.9.6.1.md`。`main` = タグ `v0.9.6.1` の物 = `fc4e9e5`（`realgame-2p` の `3db961f` の取り込み。違いは引き継ぎの資料の削除だけ）。
- 作業ツリー: `C:\puyowork\wt-rg2p`（枝 `realgame-2p`）。組み立て: `C:\puyowork\build-rg2p`（公開版と同じ設定）、`C:\puyowork\build-rg2p-debug`（`JURAIC_REALGAME_DEBUG=ON`、PDB つき）。WSL の組み立て: `~/juraic-build-rg2p`。
- 本体の作業ツリー `C:\puyowork\puyo-19-chain-generator` と、それを指す `C:\puyowork\build`・`build-rgdebug`・`~/juraic-build` は使わない（別の枝に切り替わっている）。
- 確かめの台本の写し: `C:\puyowork\bench\rg2p-tools\`（Windows `final_rg49.ps1`、WSL `checks_zl.sh`・`verify_zl.sh`・`mkshake_rg2p.sh`・`make_shake3_rg2p.py`、配布物 `make_release_zip_0961.ps1`・`make_linux_tar_rg2p.sh`）。次に使う名前: Windows `rg50`、WSL `zm`。

## 2. 0.9.6.1 で入れたもの

| コミット | 中身 | 試験 |
|---|---|---|
| `3f387a2` | Windows のパッドの XInput を `xinput1_4.dll` で読む（前は `windowsapp.lib` の先に並んだため `XInputUap.dll` の物で、仮想の Xbox 360 パッドの 120 ms の押しを窓が前にあるとき 40 回中 5 回、ほかの窓が前のとき 0 回しか見なかった）。実機のモードの「13 段目を通る動き」の行を外し、Driver は 13 段目を使う（0.9.5 と同じ）。デバッグの組み立てに、止まった場所を書く見張り | `tests/pad_tests.cpp`（直す前に落ちた）、`testModeUsesThirteenthRow`（直す前に落ちた）、`bench\rg2p-tools\stallcheck.ps1` |
| `76230f8` | Version 0.9.6.1 | — |
| `3db961f` | 見張りは窓が返事をしていても、画面が 3 秒止まれば書く（COM の呼び出しで待つスレッドは返事をする） | `stallcheck.ps1 -Answering`（直す前に記録 0 件） |

- 元の報告: DualShock 4 で先行入力が効かない、ほかの作業に移ると駄目（0.9.5 でも同じ）。試験用の版 `juraic-field-0.9.1-test3`（mingw で作った物、`xinput1_4.dll`）だけ快適だった。試験版 1（`D:\juraic-win\artifacts\juraic-field-0.9.6-test1-windows-x64.zip`）で、報告した人の DualShock 4 の操作が直ったことを確かめていただいた。

## 3. 確かめ（0.9.6.1）

| 確かめ | 結果 |
|---|---|
| Windows `rg49` | Release・デバッグの組み立ては警告 0、`realgame_tests` 通過、パッドの試験 77（仮想のパッドを差さない飛ばし）、X3 のチェックサム 4 つが `checksums_x3only_d20.txt` と同じ、`ai_tests` 通過、`verify_build` 悪い行 0（43 行）、ctest 37 本通過: `ALL OK (rg49)`（22:28） |
| WSL `zl` | 写し 7 つと本体がすべて FAILED 0。16 通りの表 442 組中 436（外れ 6、うち 5 はお邪魔 6 の回戦）、放置 0。全 26 記録の放置 F の合計 20 |
| Linux | 警告 0、ctest 29 本通過、配布物の中の自己テスト 0・3003 |
| Mac・Android | タグの run（Mac `39607270247`、Android `39607269992`）とも成功。Mac は自動テスト 29 本、X3 のチェックサム 4 つが Windows と同じ。Android の APK は公開用の鍵（証明書 `d818408a…0cb6`）で v2 の署名、版 0.9.6.1、エミュレーターの記録に落ち 0 件 |

## 4. 残っていること

- **応答なし（フリーズ）**: 0.9.5 から。Windows のログ（イベント 1002）に 10-19・10-20 のデバッグの組み立てと 10-21 の 0.9.5 の公開版で 3 回（起動から 39〜600 秒）。報告では、観戦（AI 対 AI）中にほかの窓へ移ると起き、性能に余裕の無い PC で起きやすい。この PC では台本（窓の前後・最小化・クリック・ドラッグ、観戦中の最小化と覆い各 20 秒 3 回）で出ていない。試験版 2（`juraic-field-0.9.6.1-test2-windows-x64.zip`、直した見張り入り）でも記録が出なかった。外から見張るツール `D:\juraic-win\artifacts\JuraicHangCheck-1.zip`（ソース `C:\puyowork\bench\rg2p-tools\hangcheck\`）を渡した。見立て（未確認）: 窓のスレッドが AI の計算を待つ所（`ai/agent.h` の 2 度目の選びの前の `job.wait()`、`finish()`）が、裏で遅く動かされると長引く。
- 記録の位置を関数名に直す: `C:\puyowork\bench\rg2p-tools\resolve\resolve.exe <exe> <PDB のフォルダー> <記録>`。試験版の exe と PDB は `D:\juraic-win\artifacts\test-096-1\symbols\`・`test-0961-2\symbols\`。
- **Alt キーだけを押して離すと、窓のメニューの操作に入り、次のキーまでゲームが止まる**（台本で見つけた。直していない）。
- 資料 7 の 5 節の残り（実機での確かめなど）。
