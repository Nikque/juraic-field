# 引き継ぎ 7: クロスチェックの受け入れと 0.9.6 の公開（枝 realgame-2p）

この資料は作業の引き継ぎ用です（製品の文書ではありません。`main` に取り込む前に消す）。次の窓が最初に読む資料です。経緯は `HANDOFF_realgame6.md`（「資料 6」）の 9 節、実機のモードの過去の直しの一覧は `FIXES_realgame.md`、0.9.5 の公開の手順は `HANDOFF_realgame4.md`（「資料 4」）の 10 節。

## 1. 今の状態（2026-10-21 未明）

- **2026-10-21 朝（13 個目の窓）のクロスチェックの受け入れと確かめは 6 節、0.9.6 の公開（07:19、Latest）は 7 節。** この節の下は、その前の状態。
- リポジトリ `C:\puyowork\puyo-19-chain-generator`、枝 `realgame-2p`。先頭はこの資料のコミット。その前が `e96bdc1`（Version 0.9.6）、`8dd92f3`（実機のモードの最後の直し・試験・資料）。作業ツリーに書きかけは無い。
- **push 済みは `51f69ac` まで。** `main` = `origin/main` = `aa9ac8a`（v0.9.5。作業ツリー `C:\puyowork\wt-main095`）。公開した最新は v0.9.5。タグ `v0.9.6` は無い。
- push していないコミットの履歴は書き直してある（資料 6 の 9.10。番号の対応表もそこ）。控えの枝 `backup/realgame-2p-before-rewrite` と `refs/original/…` は **push しない**。
- 版の番号は 0.9.6（`CMakeLists.txt`・`gui/generator_gui.rc`・`CHANGELOG.md` の「v0.9.6 — 2026-10-21」）。
- 組み立て（どちらも先頭と同じソース、警告 0）: `C:\puyowork\build\Release\JuraicField.exe`（10-21 01:01:08、公開版と同じ設定）、`C:\puyowork\build-rgdebug\Release\JuraicField.exe`（01:12:33、`JURAIC_REALGAME_DEBUG=ON`。利用者のプレイ試験用）。
- 確かめ: 資料 6 の 9.6（`ALL OK (rg45)`、写し 7 つと本体 FAILED 0、16 通りの表 407 組中 398、全記録の放置 F 合計 21、全 24 記録の Tracker の出力が前と同じ）。
- **クロスチェックは別の窓が行う**（依頼文 `C:\puyowork\PROMPT_crosscheck_20261021.md`、資料 `C:\puyowork\CROSSCHECK_20261021.md`。どちらもリポジトリの外）。クロスチェックの窓はコードを変えず、報告だけをする（作業用のフォルダー `C:\puyowork\crosscheck-20261021\`、報告は `REPORT.md` か、利用者が貼る）。
- 次に使う名前: Windows `rg46`、WSL 一式 `zi`。動作中のもの: なし。

## 2. 利用者の指示（2026-10-21、この資料を書いた窓が受けたもの）

| 言葉 | どうするか |
|---|---|
| 「クロスチェックの受け入れも別窓で行います。修正点を修正したら0.96へアップデートしてpush releaseです。必要な資料をまとめ、引継ぎ用プロンプトを作成してください」 | **この窓の仕事: クロスチェックの報告を受け、直すべき所をすべて直し、0.9.6 として push・公開する**（push・公開は利用者が許した。公開の手順は 4.4） |
| 「当たり前ですけど公開の文章に今後の予定については入れないでください」 | **公開の文章**（README・CHANGELOG・リリースの本文・配布物の説明・コミットの文・push する枝の資料）に、今後の予定を書かない |
| 「クロスチェックはソースのチェックなんだから先に全部直してからじゃないと意味がないです」「他にも修正が必要なものがあったらいちいち私に聞かずに全て修正してから…」 | 見つけた不具合（報告のもの、自分で見つけたもの、試験の写しの落ち）は、聞かずにすべて直してから先へ進む |
| 「履歴を書き直す」（push していないコミットの扱いを問われて） | 済み（資料 6 の 9.10） |
| 対戦場（Juraic Arena）のメニューは 0.9.6 の変更履歴と README に「書かなくていいです」 | 0.9.6 の CHANGELOG・README・リリースの本文に書かない |

前からの決まりは資料 6 の 2 節と `FIXES_realgame.md` の 5 節（自由落下は 0、A は画面で切り替え・開くたびに「使わない」、B、C は取りやめ、撃つかは AI の責任、押しは 2 フレーム、など）。

## 3. 守ること

- **自由落下（2P の組を操作せずに放置すること）は、どんな状況でも 0。**
- 直すときは、落ちる試験を先に確かめてから直す。新しい試験の判定は、写し 7 つで測った数から決める。
- `ai/` を変えたら X3 のチェックサム（`tidy_verify2`）で、ふつうの Juraic の手が変わらないことを確かめる。
- ソースの注記は「コードが何をするか」だけ（誰が決めたか・言ったかは書かない）。
- 公開の文章に元のゲームの名前を出さない（README の注記とシミュレーターの 3 つのアドレスだけの例外）。元のゲームの版の言葉も出さない。「道具」は使わず「ツール」、「相殺」はそのまま（公開の後の語の確かめ `bench\release_download_check.ps1` が見る）。
- **push は枝とタグの名前を書いて行う**（`git push origin realgame-2p main v0.9.6`）。`--all`・`--tags`・`--mirror` は使わない。控えの枝は push しない。
- 消すときは、捨てるフォルダー（`C:\puyowork\_trash-<日付>\`）へ移す。動いている測定は止めない。AI どうしの対戦の測定は、流す前に利用者に聞く。予定が変わったら（終わる時刻、足す確かめ）すぐ利用者に知らせる。
- 大きい出力・ダウンロードは D ドライブへ（C が狭い）。WSL のビルドは `/mnt/d` の上だと遅い。
- Git Bash の heredoc・`sed`・`py -c` の中では `\` が食われる。C++ の文字列やパスを含む書き換えは、Write・Edit の道具か、ファイルに書いた Python で行う。CRLF のファイル（`play/realgame_read.h`・`ai/agent.h`・`HANDOFF_realgame6.md`・`FIXES_realgame.md` など）は改行を保つ。
- 報告はすべて日本語で、表でまとめる。

## 4. 手順

### 4.1 クロスチェックの報告を受ける

1. 報告を読む。見つけたことを 1 つずつ、ソースで確かめる（読む・小さい試験を流す）。報告の言うことを、確かめずに正しいとしない。
2. 分ける: (a) 不具合、(b) 整理（使われない物・まとめられる物）、(c) 好み、(d) 問題ではない（理由を書く）。
3. (a) は落ちる試験を先に書いてから直す。(b) は動きを変えない形で入れ、試験と、Tracker を変えたら全記録の比べ（`order\w11\records11.sh`）で確かめる。(c) と、利用者の決め（`FIXES_realgame.md` の 5 節）を変えることになる物は入れずに、表で利用者に示す。
4. **直す前に、`FIXES_realgame.md` の 3 節（試して取りやめたもの）と 4 節（同じ所を何度も直した系列）に同じ形がないかを見る。** 同じ形なら、前にやめた理由が今も成り立つかを確かめてから決める。
5. 直したものは `FIXES_realgame.md` の 2 節に足す（日付、コミット、部品、中身、元、試験）。

### 4.2 確かめ一式（直したら、コミットの前）

資料 6 の 6 節のとおり:

- **WSL 一式**: `bash /mnt/c/puyowork/bench/tmp/realgame-tools/order/w10/checks10b.sh zi`（全記録の Driver の流し直し、16 通りの表、写し 7 つと本体）。結果: `~/rgt/w10/verify_zi.txt`（写し 7 つと本体がすべて FAILED 0）、`~/rgt/w10/matrix_zifin.txt`（`order\w11\matsum.sh` で数える。`matrix_zhfin.txt` の 407 組中 398 から外れを増やさない）、`~/rgt/w8/allrec_zi.txt`（F の合計 21 から、どの記録でも増やさない）。最後の `grep FAILED` が何も見つけないと終了の値は 1 になる（落ちではない）。
- `driveRound` の組が固まった所の書き方を変えたら、16 通りの表を作る `order\make_shake2.py` が探す文も合わせる（10-21 に合わせた）。表の段が数十秒で終わったら `~/rgt/w10/mkshake_zi.txt` を見る。
- **Tracker を変えたら**: `order\w11\records11.sh`（前のコミットとの比べ。組を減らさない、両側の並びが変わらない）。
- **Windows**: `order\w13\final_rg45.ps1` を写して名前を替える（`sed 's/rg45/rg46/g'`）。Release の組み立て・`realgame_tests`・パッドの試験（77）・デバッグの組み立て・`tidy_verify2.ps1 rg46 checksums_x3only_d20.txt` を順に流し、まとめは `C:\puyowork\bench\out\rg46_summary.txt`（45 分ほど。最後が `ALL OK (rg46)`）。

### 4.3 コミット

- 変更履歴の 0.9.6 の項を直しに合わせる（公開の文章の決まり: 3 節）。資料 6 か、この資料に節を足す。
- コミットの文は英語、ファイルに書いて `git commit -q -F <ファイル>`、最後の行は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`（その時の決まりに従う）。
- デバッグの組み立てを作り直す（4.2 の Windows の台本が作る）。

### 4.4 0.9.6 の公開（資料 4 の 10 節と同じ手順）

1. `CHANGELOG.md` の「v0.9.6 — 2026-10-21」の日付を、公開する日に合わせる（変えたらコミット）。
2. **`main` へ取り込む**: 作業ツリー `C:\puyowork\wt-main095`（枝 `main`）で `git merge --no-ff realgame-2p`（文: `Merge realgame-2p (version 0.9.6) into main`）。引き継ぎの資料（`HANDOFF_realgame*.md`・`PROMPT_realgame*.md`）は `main` では消してあるので「変更と削除」の衝突になる → `main` では消したままにする（`git rm`）。新しく入る `HANDOFF_realgame6.md`・`HANDOFF_realgame7.md`・`PROMPT_realgame13.md`・`FIXES_realgame.md` も、引き継ぎの資料として `main` からは消す。取り込みの後、`git diff realgame-2p main --stat` が、それらの資料の消しだけであることを確かめる。
3. 注釈つきのタグ `v0.9.6` を `main` の取り込みのコミットに付ける。
4. 確かめ: `main` の木は `realgame-2p` から資料を除いたものなので、4.2 の確かめと組み立てはそのまま使える。もし 2 で資料以外の違いが出たら、`main` の木で組み立てて確かめ直す。
5. **push**: `git push origin realgame-2p main v0.9.6`（名前を書いて。控えは押さない）。
6. **GitHub のビルド機**: タグと `main` で Mac（`.github/workflows/macos.yml`）と Android（`android.yml`）が動く。`gh run list`・`gh run watch` で見る（ほかの確かめと並べて待つ）。確かめ: Mac で試験が通り、X3 のチェックサム 4 つが Windows と同じ。Android の APK が公開用の鍵で署名され（証明書 `d818408a…0cb6`、`apksigner` v2）、版が 0.9.6、エミュレーターで起動。**0.9.5 では、タグの Mac の run で `realgame_screen_read` が落ちた**（試験の中の乱数の取り方が clang で逆順になった。`9e0297a` で直した）。今回の試験で足した所にも、関数の引数の中で乱数を 2 回取る形が無いかを、push の前に読んで確かめる。
7. **配布物**（`C:\puyowork\artifacts\v0.9.6\` に集める。0.9.5 は `artifacts\v0.9.5\` の 6 つ）:
   - Windows: `bench\make_release_zip_095.ps1` を写して版を替え（`_096`）、`juraic-field-v0.9.6-windows-x64.zip`。台本は SDL3・ViGEmClient のライセンスと 47 連鎖の見本を足し、自己テスト −7 も流す。
   - Linux: WSL で `bench\make_linux_tar.sh 0.9.6`（`juraic-field-v0.9.6-linux-x86_64.tar.gz`）。
   - Mac: Mac の run の成果物を落とし（`gh run download`、D ドライブへ）、`bench\make_mac_zip.py 0.9.6 <日付> <結果のフォルダー>`（`juraic-field-v0.9.6-macos-arm64.zip`。zip の中の実行権を保つ）。
   - Android: タグの Android の run の APK（`juraic-field-v0.9.6-android-arm64.apk`）と説明（`juraic-field-v0.9.6-android-readme.txt`、0.9.5 のものを手本に版を替える）。
   - `SHA256SUMS-v0.9.6.txt`。
   - 説明のひな形: `bench\zip_readme_template.txt`・`linux_readme_template.txt`・`mac_readme_template.txt`（0.9.6 で変える所があれば直す）。
8. **リリースの本文** `C:\puyowork\artifacts\release-notes-v0.9.6.md`: `release-notes-v0.9.5.md` を手本に、0.9.6 の変更（CHANGELOG の項）と、確かめたこと・確かめていないこと（実機での確かめはまだ、など）。**今後の予定は書かない。対戦場は書かない。** 元のゲームの名前・版の言葉・「道具」を使わない。
9. **公開**: `gh release create v0.9.6 <6 つのファイル> --title "Juraic Field 0.9.6" --notes-file C:\puyowork\artifacts\release-notes-v0.9.6.md`（Latest にする）。
10. 公開の後: `bench\release_download_check.ps1`（公開した文章と配布物の語の確かめ、落とし直し）。落とし直した 6 つが一覧と同じ、Windows の zip の自己テスト 0・3003、Linux の tar.gz を展開して 3003、Mac の zip の実行権。語が見つかったら、直して配布物を作り直し、差し替える（0.9.5 では CHANGELOG の「道具」で 1 回差し替えた）。
11. 利用者に表で報告する（公開の URL、配布物、確かめ、確かめていないこと）。この資料に公開の節を足してコミットする（その push は利用者に聞く）。

## 5. 残っていること（どれも落とさない）

資料 6 の 7 節の表（1〜15）と 9.7 の表（16〜23）。0.9.6 の公開とは別に残るもの:

- 0.9.6 の直し（A・B・同じフレームの押し・`overdue`）の、実機での確かめ（利用者の次のプレイ試験の記録で）。
- 光の後に宙に見えた組を、乱れの玉 1 枚で取ることがあるか（9.7 の 22）。
- A で、作り物の戦の最後の組が外れる形（9.7 の 23）。
- 控えの枝 `backup/realgame-2p-before-rewrite` と `refs/original` を消すか（利用者に聞いてから）。

## 6. クロスチェックの受け入れ（2026-10-21 朝、13 個目の窓）

報告は `C:\puyowork\crosscheck-20261021\REPORT.md`（リポジトリの外）。利用者の指示（「ソースコードの整理や高速化の指摘も含め、まずは全て修正し、その後置きミスが減ったかなどの影響も調べてください」）で、整理と高速化の候補も入れた。ツールと台本は `C:\puyowork\bench\tmp\realgame-tools\order\w14\`、WSL の出力は `~/rgt/w14/`。

### 6.1 見つけたことと扱い

| ID | 分け | 扱い | 試験 |
|---|---|---|---|
| F01 | 不具合（放置） | Driver: 戦の 1 組目も見えないまま始める（`appeared`・`blindDue`。`FIXES_realgame.md` 2 節） | `testFirstPairLit`（直す前 1 組目の操作の無いフレーム 224・放置 4 → 32・0） |
| F02 | 不具合（範囲外の添字） | `MatchView`: 席が -1・0・1 の外の対戦を追わない（`broken`）。`ArenaPlay`: 人の接続で席が無い・観戦で席がある対戦も終える | `tests/arena_tests.cpp`（新しい ctest `arena_lines_on_this_machine`） |
| F03 | 不具合 | `readRules` は数でない値・範囲の外の値（上限も）があると 1 つも代入せずに false。呼び元は対戦を追わない。T の色は 1〜5 だけ | 同上 |
| F04 | 不具合 | `Line::pump` は受け取りが 4 MiB を超えたら読むのをやめる。閉じた接続からは読まない | 同上 |
| F07 | 不具合 | 席の操作は戦のフレームの上限＋64（サーバーが取る分）まで、END の後は取らない。観戦の待ちの行も 4 MiB まで（超えたら接続を終える） | 同上 |
| F08 | 不具合 | END の後も、届いていた操作のフレームを再生する（観戦は 1 フレームに 2 つずつ） | 同上 |
| F09 | 不具合 | `Line` は自分の socket を持ち、閉じる・別の socket を受ける・消えるときに閉じる（写しは不可、移しは可）。サーバーに切られた接続はすぐ閉じる | 同上 |
| F10 | 不具合 | 接続（名前の引き・connect）を別のスレッドで（`arena::Call`）。画面は Connecting のまま進み、つながらなければ Over で知らせる | 同上 |
| F11 | 不具合（デバッグの組み立てとツールだけ） | 読みの表の割合が 0 でも割らない | `testTableText` |
| F05 | 試験の判定＋元の直し | 負けで終わった組を一律に除く数えをやめた。外れの元は「画面が見せたお邪魔をエンジンが知らない」ことだったので、見えないまま始める組の盤面に入れた（`FIXES_realgame.md` 2 節） | `testUnseenPairsUnderGarbage`（両形とも外れ 4 % 以下） |
| F06 | 試験の判定 | 低い道の形の近い所への替えに上限（15 %）。13 段目を通れば届いたかを数えた（`steeredLow`、0）。前の注記の見立てが誤りだったので直した | `testLateHoldsNothingAway` |
| 報告 5 節 | 試験の判定 | 13 段目の組は 1 組ずつ照らす（`climbing`）、13 段目の試験の当たり 98 %、絵の 1 枚・押しの取り消しの試験に絶対の下限、絵が読めたことと位置直しが増えないこと、`keepgoing.py` は落ちたら最後の「全部通った」を出さず終了の値 1 | 写し 7 つの数から |
| （自分で見つけた） | 不具合（放置） | 確かめ一式の写し salt27 で、エンジンが組を固めた直後にパッドが下の 1 フレームを落とし、画面が寝ていると見て下を離した組が、寝たまま固まらずに 10 フレーム押されなかった。Driver: エンジンが固めた後 2 フレームは画面で寝ていても下を離さない（`FIXES_realgame.md` 2 節） | `testDownLostAtLock`（その回戦: 放置 1 → 0） |
| F12 | 公開の文章 | 6.6 の履歴の書き直し（利用者の決め） | — |
| 整理 9 件 | 整理 | 8 件は入れた（`FIXES_realgame.md` 2 節）。対戦場の写しの使われない物（`listenOn` など）は、写し元（`juraic-arena`）と同じに保つため残し、`play/arena/SOURCE.md` に訳を書いた | まとめ（`DriveScenario`）だけを当てた写しの出力が先頭と同じ |
| 高速化 6 件 | 整理 | 5 件と 1 件の一部を入れた。`observeWith` は `gprof` で 68,011 回呼ばれて自分の時間がほぼ 0 だったので、色の集合の作り方だけ（器の使い回しは入れない）。`keepsWays` の広い控えは入れない（報告も「状態の同一性の確認が必要」） | 6.4 |

`juraic-arena`（写し元、まだコミットの無い作業中のリポジトリ）にも、同じ 4 つのファイル（`src/arena/net.h`・`follow.h`・`lines.h`・`rules_text.h`）を写した（利用者の決め）。前のファイルは `C:\puyowork\_trash-2026-10-21\juraic-arena-before-fixes\`。そちらのプログラム 4 つと試験を WSL の g++ で組み立てて、試験は `ALL OK`。コミットはしていない。

### 6.2 置きミスと放置への影響（作り物の対戦と記録の流し直し。実機では確かめていない）

| 確かめ | 直す前（`04808ee`） | 直した後 |
|---|---|---|
| 16 通りの表（お邪魔の有無 × 光 4 通り × 種 2、`matrix_zkfin.txt`） | 407 組中 398（外れ 9、どれもお邪魔 6 の回戦） | **423 組中 418（外れ 5、どれもお邪魔 6 の回戦）**、放置 0 |
| `testUnseenPairsUnderGarbage`（13 段目を許す形 / 使わない形） | 78 組中 75 / 70 組中 66 | 70 組中 68 / 71 組中 69 |
| `testPairsLitAsTheyAppear` | 74 組中 74（見えないまま始めた組 12） | 75 組中 75（11） |
| `testFirstPairLit`（新しい試験、光 80・40 の回戦 4 つ） | 1 組目の操作の無いフレーム 224、放置 4 | 32、放置 0（45 組中 45） |
| ほかの試験の出力 | — | 1 行も変わらない |
| 全 24 記録の Driver の流し直しの放置 F（`allrec_zk.txt`） | 合計 21 | **合計 20**（`20261017-161654` が 1 → 0、ほかは同じ） |
| `testDownLostAtLock`（新しい試験、見つけた回戦と地面で 1 フレーム落とす回戦 4 つ） | 放置 1 | 放置 0、下が次の組に届いた組 0（195 組中 192） |
| 全 24 記録の Tracker の出力（`records11.sh`） | — | 4 つのツールとも 24 記録すべて同じ |

- `20261017-161654` の 1 戦目の 1 組目は、前の Driver では窓が動いた後も何も押さなかった組（F）。今は窓が動いて 4 フレーム後から押す（F01 は実機の記録にもあった形）。
- 16 通りの表で組の数が増えたのは、置き違いで早く終わっていた戦が長く続いた分。

### 6.3 速さ（高速化の候補を入れた後）

記録 3 つの流し直し（`rgdrive`、記録を WSL の中に写し、5 回の最小の CPU の時間）: `20261019-201726` 0.39 → 0.28 秒、`20261020-054131` 0.39 → 0.29 秒、`20261020-183601` 0.69 → 0.71 秒。押しの一致の割合はどれも同じ。壁時計では 13〜28 秒かかるが、大半は Windows 側の記録のファイルの読み込みで、計算ではない（`gprof` で計算は 0.3 秒ほど）。`observeWith` は 68,011 回呼ばれて自分の時間がほぼ 0。

### 6.4 組み立てと試験の写し

- Windows の Release・デバッグの組み立て、Linux（WSL、`~/juraic-build`）の組み立て: どれも警告 0。WSL の g++ の `-Wall -Wextra` でも `realgame_tests` は警告 0（前からあった `ai/pace.h` の 1 件も直した）。
- **確かめ一式（直しのすべての後）**: WSL `zk`（`order` の `w14/checks14.sh zk`）: 写し 7 つと本体がすべて FAILED 0、16 通りの表 423 組中 418（`zi` と 1 行も同じ、どの回戦も放置 0）、全記録の F 合計 20（記録ごとに `zi` と同じ）。Windows `rg47`（`w14/final_rg47.ps1`）: Release とデバッグの組み立ては警告 0、`realgame_tests` 通過、パッドの試験 77、X3 のチェックサム 4 つが `checksums_x3only_d20.txt` と同じ、`ai_tests`・`verify_build`（43 行）、ctest 37 本が 1 回目で全部通過: `ALL OK (rg47)`（06:48）。デバッグの組み立て `C:\puyowork\build-rgdebug\Release\JuraicField.exe` は 06:07:07。
- `realgame_tests` の出力は Windows と WSL で同じ。
- 写しの乱数の流れ: 前の書き方では `testWordsBehindPairs`・`testKicksShownLate` などが乱数の流れ 7 を位置の引数で明示して渡していて、`variants3.py` の写し salt17/27/37 でも流れが替わらなかった。`DriveScenario` では既定と同じ 7 を書かないので、写しではそれらの試験も流れが替わる（試す回戦が増えた）。そこで見つかったのが 6.1 の「自分で見つけた」放置。整理と高速化だけを当てた写し（動きを変える直し 2 つを外した `order\w14\no_behav.py`）の試験の出力は `04808ee` と同じ（新しい試験 2 つが落ちるだけ）で、整理と高速化は動きを変えていない。
- 写しの段の台本は新しい試験の形に合わせた（`order\w14\checks14.sh`・`verify14.sh`・`mkshake14.sh`・`make_shake3.py`・`shake_tracker3.py`、`order\variants3.py` は前の形も読める）。

### 6.5 履歴の書き直し（3 回目。利用者の決め: push していないものも、push 済みのものも書き直す）

- 対象: `realgame-2p` と `main` の `4f38b09`（前の番号 `96916a0`、push 済み）から先頭までの 36 コミットと、タグ `v0.9.5`。台本は `order\w14\rewrite3.sh`（`rewrite3_tree.py`・`rewrite3_msg.py`・`rewrite3_check.py`）。
- 変えたのは資料（`*.md`）だけ: 利用者のフォルダーの道（`C:\Users\<名前>\`）を `%USERPROFILE%\` に、版の先の話を書いた所を消し、別の仕事の窓とその資料の道を消し、例外の外の元のゲームの名前を含む窓の題名を「対戦場の窓」に、玉の呼び名を「玉の数」にした。資料とコミットの文の中のコミット番号は新しい番号にした。
- 確かめ: 36 コミットとも資料の外のファイルは前と同じ。`main` と `v0.9.5` の木は前と同じ。新しい `realgame-2p`・`main`・`v0.9.5` に、利用者の名前・予定・別の仕事・例外の外の名前は無い（`git grep`、`git log -S`）。
- 番号の対応（全部は `order\w14\rewrite3_map.txt`）:

| 前 | 後 | コミット |
|---|---|---|
| `96916a0` | `4f38b09` | 13 回目の記録の引き継ぎ（利用者の名前の入った道が最初に入った所） |
| `e248973` | `62f7bf3` | Version 0.9.5 |
| `5e3e005` | `e7e5ee3` | `main` への取り込み（0.9.5、タグ `v0.9.5`） |
| `7d64cee` | `aa9ac8a` | `main` の先頭 |
| `bc56b00` | `51f69ac` | push 済みの `realgame-2p` の先頭 |
| `943d186` | `e96bdc1` | Version 0.9.6 |
| `540dbb6` | `04808ee` | クロスチェックの受け入れの引き継ぎ |
| `4b9f7ed` | `2e4cdeb` | クロスチェックの受け入れ（この窓の直し） |

- 控え（push しない）: `refs/backup-rewrite3/heads/realgame-2p`・`refs/backup-rewrite3/heads/main`・`refs/backup-rewrite3/tags/v0.9.5` と `refs/original-rewrite3/`。前の書き直しの控え（枝 `backup/realgame-2p-before-rewrite`、`refs/original/`）もそのまま。
- `realgame-2p`・`main`・`v0.9.5` は push 済みの番号が変わったので、push には強制が要る（名前を書いて、前の番号を確かめる形で行う）。GitHub は前の番号のコミットを、番号を知る人にはしばらく見せることがある。

## 7. 0.9.6 の公開（2026-10-21 朝、13 個目の窓）

### 7.1 push と公開

- `main` へ取り込み（`Merge realgame-2p (version 0.9.6) into main`。資料の消しだけが `realgame-2p` との違い）、注釈つきのタグ `v0.9.6`。今の番号は `79879fa`、タグの物 `ec20da5`（公開したときは `473c91f`、タグの物 `10247fb`。7.5 で書き直した）。
- push（名前を書いて。書き直した 3 つは前の番号を確かめる強制）: `realgame-2p`、`main`、`v0.9.5`、`v0.9.6`（新しく）。GitHub の `git ls-remote` で 4 つとも確かめた。控えの枝・`refs/backup-rewrite*/`・`refs/original*/` は push していない。そのときの番号と今の番号は 7.5 の表。
- 公開: https://github.com/Nikque/juraic-field/releases/tag/v0.9.6 （題「Juraic Field v0.9.6」、Latest、10-21 07:19）。題は、4.4 の 9 に書いた「Juraic Field 0.9.6」ではなく、これまでの公開（「Juraic Field v0.9.5」など）の形に合わせた。本文は `C:\puyowork\artifacts\release-notes-v0.9.6.md`（試験の本数: Windows 37、Linux 29、Mac 29）。

### 7.2 配布物（`C:\puyowork\artifacts\v0.9.6\`、公開したものと同じ）

| ファイル | 大きさ | SHA-256 の頭 | 作り方 |
|---|---|---|---|
| `juraic-field-v0.9.6-windows-x64.zip` | 84,991,580 | `d70c0659` | `bench\make_release_zip_096.ps1`（`C:\puyowork\artifacts\` にも同じもの） |
| `juraic-field-v0.9.6-linux-x86_64.tar.gz` | 87,931,284 | `ad235802` | WSL `bench\make_linux_tar.sh 0.9.6`（作ったときの自己テスト 0・3003 とも 0） |
| `juraic-field-v0.9.6-macos-arm64.zip` | 87,858,299 | `c53960aa` | Mac の run `39504282642` の成果物（`D:\juraic-linux\mac-ci\v096\macos-results`）から `bench\make_mac_zip.py 0.9.6 20261021 …`。46 個、実行権は `JuraicField` と `random_19_chain`（0.9.5 と同じ形） |
| `juraic-field-v0.9.6-android-arm64.apk` | 89,994,970 | `ec6b1a18` | Android の run `39504282559` の `app-release.apk`（`D:\juraic-linux\android-ci\v096`） |
| `juraic-field-v0.9.6-android-readme.txt` | 8,391 | `123dcc6d` | 0.9.5 のものの版を替えた |
| `SHA256SUMS-v0.9.6.txt` | 519 | — | 上の 5 つ（Windows の zip が 1 行目） |

### 7.3 確かめ

| 確かめ | 結果 |
|---|---|
| Mac の run（タグ） | 成功。試験 29 本すべて通過（`realgame_screen_read` 732 秒も通過）、X3 のチェックサム 4 つが Windows と同じ、自己テスト 20 個すべて 0 |
| Android の run（タグ） | 成功。公開用の鍵（証明書 `d818408a…0cb6`）で v2 の署名、版 0.9.6、エミュレーター（Android 14）でメニューまで起動。アプリの落ちの記録なし |
| `main` の Mac・Android の run | どちらも成功 |
| 本文と Android の説明の語 | 見つからない（4.4 の 8 と 3 節に書いた、公開の文章に書かない語。`release_download_check.ps1` の語に、先の話と対戦場の語を足して） |
| 配布物の中の文書の語（Windows・Linux・Mac） | 0.9.5 と同じ所だけ（README の断り書き、README.en、URL_FORMATS、CHANGELOG の前の版の項）。新しく増えた所なし。README・CHANGELOG に対戦場の記述なし |
| `release_download_check.ps1 0.9.6` | 落とした Windows の zip が一覧・手元のものと同じ。展開した写しで自己テスト 0・3003 とも 0 |
| 落とし直した 6 つ | 一覧（`sha256sum -c`）と手元のものと同じ |
| Linux の tar.gz（落とし直したもの、WSL で展開） | 自己テスト 3003 は 0。自己テスト 0 は、WSL の音の出口（`/mnt/wslg/PulseServer`）が無い間は 10（BGM がどの場面でも鳴らない）、ある間は 0。0.9.5 の tar.gz と交互に 4 回ずつ流して、0.9.5 も同じ（出口の無い間の 1 回目はどちらも 10、出口ができた後はどちらも 0）。0.9.0 の組み立てのときも同じ 10 が出ていた。音を開くコードは 0.9.5 から変わっていない |

- タグ `v0.9.5` を押し直したので、その Mac と Android の run も動いた。Android は成功、Mac は `realgame_screen_read` で落ちた。0.9.5 のタグの最初の run（`39109432038`）と同じ落ちで、木も同じ（タグの後の `9e0297a` で直したもの。0.9.5 の公開の Mac の配布物はその後のコミットから作った）。
- 実機での確かめは、まだしていない（このモードを 0.9.6 の組み立てで実機につないで試すこと、置き違いの数、放置が無くなったこと）。Linux・Mac の実機、Android の端末とコントローラーも確かめていない。
- `C:\puyowork\juraic-arena` への同じ直しは入れたが、その置き場にはコミットが 1 つも無い（もとから）ので、ファイルに入れたまま。
- この節のコミットは、利用者に聞いてから push した（その後 7.4・7.5 で書き直した）。

### 7.4 先頭の資料のコミット 2 つの書き直し（4 回目。利用者の決め）

- 6.5 の説明の行が、3 回目の書き直しで消した版の先の話の語句を、そのまま引いていた（push 済みの 2 つのコミットに入っていた）。その行を語句を引かない形に直し、先頭の資料だけのコミット 2 つを書き直した（ここに「`main` とタグには入っていない」と書いたのは誤りだった。`8eaf8a7` は 0.9.6 の取り込みの 2 つ目の親として `main` とタグ `v0.9.6` にも入っていた。7.5 で直した）。この窓で書いたほかの資料の行とコミットの文には、同じものは無い。
- 番号の対応: `8eaf8a7` → `99359a9`（6.5 の書き直しの引き継ぎ）、`65833d2` → この節のコミット（0.9.6 の公開の引き継ぎ）。7.1 の `realgame-2p` の番号に新しい番号を書き足した。
- 控え（push しない）: `refs/backup-rewrite4/heads/realgame-2p`（`65833d2`）。
- `realgame-2p` の push は、前の番号（`65833d2`）を確かめる強制で行った（`d0881dc`。7.5 で書き直した）。

### 7.5 10-15 の資料からの書き直し（5 回目。利用者の決め: `main`・`realgame-2p`・`v0.9.5`・`v0.9.6`）

- 7.4 の後で見つけたこと: (1) `8eaf8a7` は `main` の 0.9.6 の取り込み（`473c91f`）の 2 つ目の親で、`main` とタグ `v0.9.6` に残っていた。(2) 3 回目の書き直しの範囲（`e754918` から先）より前の、10-15 の `e35018e`・`e46efc9` の `HANDOFF_realgame.md` に、版の先の話の行があった（`e754918` で消えるまで）。`main`・`realgame-2p`・`v0.9.5`・`v0.9.6` の履歴に入っていた（`v0.9.2` には入っていない）。
- 手順（台本は `order\w14\rewrite5_pre.sh`・`rewrite5.sh`・`rewrite5_tree.py`・`rewrite5_msg.py`・`rewrite5_check2.py`）: (a) 控えを作り、`main` の取り込みを 2 つ目の親だけ `905d83b` に替えて作り直し（木・文・作者・日時は同じ。`6fa17b1`）、`v0.9.6` を同じ中身で付け直した（`91954b3`）。(b) `e35018e` から先の `realgame-2p` と `main` を `git filter-branch` で書き直し、タグも合わせた。直したのは引き継ぎの資料（`HANDOFF_realgame*.md`・`PROMPT_realgame*.md`・`FIXES_realgame.md`）だけで、ゲームの文書（README・CHANGELOG など）は読まない。版の先の話の行を消し、資料とコミットの文の中の番号を新しい番号にした。
- 確かめ: 91 コミットを、作者・日時・題・親のつながりで新旧の組にした。85 が新しい番号、6 は同じ番号（`e35018e` と別の流れの `main` の側のもの: 0.9.2・0.9.3 のころ）。どの組も引き継ぎの資料の外のファイルは同じ。`main`・`v0.9.5`・`v0.9.6` の木は前と同じ（0.9.6 の配布物と「Source code」の中身は変わらない）。タグの物は指す先のほかは同じ。先頭の木にも履歴にも、版の先の話の行は無い。前の書き直しで消した語も無い。先頭の資料の違いは番号の置き換えだけ。
- 番号の対応（全部は `order\w14\rewrite5_map.txt`）。この資料の 6 節と 7 節の番号は新しい番号に置き換わっている（6.5 の表の「前」の列と、この表の「前」の列は、書き直す前のまま）:

| 前 | 後 | コミット |
|---|---|---|
| `e35018e` | `71a8852` | 10-15 の引き継ぎ（版の先の話の行が最初に入った所） |
| `e46efc9` | `78192ba` | 0.9.2 の引き継ぎ |
| `e754918` | `4f38b09` | 13 回目の記録の引き継ぎ（その行が消えた所） |
| `15499f8` | `e7e5ee3` | `main` への取り込み（0.9.5、タグ `v0.9.5`） |
| `ae6fb60` | `e96bdc1` | Version 0.9.6 |
| `c5e420b` | `2e4cdeb` | クロスチェックの受け入れ |
| `8eaf8a7`・`905d83b` | `99359a9` | 6.5 の書き直しの引き継ぎ |
| `473c91f`・`6fa17b1` | `79879fa` | `main` への取り込み（0.9.6、タグ `v0.9.6`） |
| `65833d2`・`d0881dc` | `86db799` | 0.9.6 の公開の引き継ぎ |

- タグの物: `v0.9.5` は `9f882d4` → `2ac6c82`、`v0.9.6` は `10247fb` → `91954b3` → `ec20da5`。
- 控え（push しない）: `refs/backup-rewrite5/`（`main` `473c91f`、`realgame-2p` `d0881dc`、`v0.9.5` `9f882d4`、`v0.9.6` `10247fb`）と `refs/original-rewrite5/`。手元だけの枝 `arena` と `backup/realgame-2p-before-rewrite` は書き直していないので、前のコミットを指したまま。
- push は 4 つとも前の番号（`main` `473c91f`・`realgame-2p` `d0881dc`・`v0.9.5` `9f882d4`・`v0.9.6` `10247fb`）を確かめる強制で行う。タグの push でビルド機がまた動く（`v0.9.5` の Mac は 7.3 と同じ理由で落ちる）。
