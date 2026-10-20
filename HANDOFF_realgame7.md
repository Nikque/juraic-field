# 引き継ぎ 7: クロスチェックの受け入れと 0.9.6 の公開（枝 realgame-2p）

この資料は作業の引き継ぎ用です（製品の文書ではありません。`main` に取り込む前に消す）。次の窓が最初に読む資料です。経緯は `HANDOFF_realgame6.md`（「資料 6」）の 9 節、実機のモードの過去の直しの一覧は `FIXES_realgame.md`、0.9.5 の公開の手順は `HANDOFF_realgame4.md`（「資料 4」）の 10 節。

## 1. 今の状態（2026-10-21 未明）

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
