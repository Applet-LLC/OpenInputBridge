<!--
Copyright (c) 2026 OpenInputBridge Contributors
SPDX-License-Identifier: MIT
Licensed under the MIT License. See LICENSE file in the project root for full license text.
-->

# WHQL 申請の CodeQL 解析と DVL 作成（oib-whql）

oib_kbd と oib_mou を WHQL に申請するときの、HLK 版フォルダの作成から、CodeQL 解析と DVL（Driver Verification Log）の作成までを、`tools/oib-whql` で実行する。対象は x64 と ARM64 の 2 つの CPU と、kbd と mou の 2 つのドライバ。1 つの CPU の解析結果（SARIF）から、ドライバごとに DVL を作るので、1 回の実行で DVL が 4 本できる。

このリポジトリでは、OSS 版と、同じコードに WHQL 署名を取る Pro 版を同じ手順で扱う。Subscription 版は OpenInputBridge-Subscription に同じツールがある。Windows 10 用のドライバは Attestation 署名なので、対象外。

署名が戻ったあとの、`driver\keyboard\whql`、`driver\mouse\whql` にファイルを置く作業は、このツールの対象外にしている。

## 作業の流れ

1. ドライバを Release|x64 と Release|ARM64 でビルドする。
2. `oib-whql prepare --version 1.04`: HLK 版フォルダを作り、署名前のドライバとシンボルを置く。
3. `oib-whql run --hlk "<2 で作ったフォルダ>"`: CodeQL の解析と DVL の作成を、x64、ARM64 の順に行う。
4. `oib-whql verify --hlk "<同じフォルダ>"`: 成果物がそろっているか確かめる。
5. HLK 版フォルダの `<ドライバ>\<cpu>\` にある DVL.XML を、HLK クライアントの `c:\dvl` にコピーして、HLK で申請する。
6. 署名済みのパッケージが戻ったら、`Signed\` に置く。

## 事前に必要なもの

- `E:\codeql-home\codeql-2.20.1\codeql.cmd`（版は 2.20.1 に固定している）
- `K:\BuildEnv\SetupBuildEnv.cmd`（`driver\build_codeql_*.bat` が呼ぶ）

パスと版は、実行ファイルと同じ場所の `oib-whql.json` で変えられる。

## 使い方

`OpenInputBridge.sln` の `oib-whql` プロジェクトをビルドして使う。リリース用のフルビルドには含めていないので、使うときにプロジェクトを右クリックしてビルドするか、スタートアッププロジェクトにして実行する。出力は `tools\oib-whql\bin\<プラットフォーム>\<構成>\net8.0-windows\oib-whql.exe` で、`oib-whql.json` も同じ場所にコピーされる。VS から実行するときは、プロジェクトのプロパティの「デバッグ」にあるアプリケーション引数に、下のコマンドの `check` 以降を入れる。引数なしで実行すると、使い方を表示する。

```
oib-whql check
oib-whql prepare --version 1.04
oib-whql run     --hlk "E:\HLK\<版フォルダ>"
oib-whql verify  --hlk "E:\HLK\<版フォルダ>"
```

- `check`: 前提のものがそろっているかだけを確かめる。最初に実行する。
- `prepare`: HLK 版フォルダを作り、ドライバとシンボルを置く。
- `run`: x64、ARM64 の順に処理する。`--arch x64|arm64` で絞れる。片方の CPU が失敗しても、もう片方は続け、最後に一覧を出す。
- `verify`: HLK 版フォルダの成果物（driver、Symbol、SARIF、DVL）がそろっていて、中身が妥当かを確かめる。

## prepare がやること

`--version 1.04` を付けると、フォルダ名を「`<今日の日付> OIB 1.04`」にして、`E:\HLK`（`oib-whql.json` の `HlkRoot`）の下に作る。名前を自分で決めるときは、`--hlk` でフォルダを指定する。日が変わると `--version` からの名前がずれるので、`run` と `verify` には、`--hlk` で同じフォルダを指定する。

フォルダの中は、次の形になる。

```
<HLK 版フォルダ>\oib_kbd\x64\driver\   oib_kbd.cat、oib_kbd.inf、oib_kbd.sys（署名前）
                           \Signed\    空（署名済みのパッケージが戻ったら、ここに置く）
                           \Symbol\    oib_kbd.pdb
<HLK 版フォルダ>\oib_kbd\arm64\...     同じ形
<HLK 版フォルダ>\oib_mou\x64\...       同じ形
<HLK 版フォルダ>\oib_mou\arm64\...     同じ形
```

コピー元は、`driver\<keyboard|mouse>\<x64|ARM64>\Release\` のビルド出力。`.cat`、`.inf`、`.sys` は、その中の `oib_kbd\`、`oib_mou\` フォルダにあるものを使う。`.pdb` は `Release\` 直下のもの。

ファイルが 1 つでも足りないときは、何もコピーせずに、足りないファイルのパスを示して止まる。すでに `driver\` にファイルがあるときも、止まる（署名に出したものを上書きしないため）。やり直すときは `--force` を付ける。コピーしたファイルの元の更新日時と SHA-256 は、`whql-run.log` に残る。

## run が 1 つの CPU ごとにやること

1. `E:\codeql-home\databases-oib-<cpu>` の中身を空にする（この名前のフォルダしか消さない）。ほかのプロジェクトの DB とぶつからないよう、名前を分けてある。
2. `codeql database create` を実行する。ソースのルートは `driver`、ビルドには `driver\build_codeql_<cpu>.bat` を使う。この bat は kbd と mou の両方をビルドする。
3. DB を検査する。ソースの場所が、このリポジトリの `driver` であること。kbd と mou の両方のソースが入っていること。OSS の DB に Subscription 版だけの `license.c` が入っていないこと。
4. `codeql database analyze` を `mustrun.qls` で実行して SARIF を作り、検査する。
5. SARIF を、HLK 版フォルダのドライバごとのフォルダ（`oib_kbd\<cpu>\`、`oib_mou\<cpu>\`）にコピーする。
6. ドライバごとに DVL を作り、検査して、同じフォルダに `oib_kbd.DVL.XML`、`oib_mou.DVL.XML` として置く。作業中に `driver\keyboard`、`driver\mouse` へ置いた SARIF と DVL は、終わったら消す。
7. 実行の記録を、HLK 版フォルダの `whql-run.log` に追記する。SARIF と DVL の SHA-256 も入る。

取り違えは、フォルダ名と版が合わない時点で、1 つも動かす前に止める。OSS（Pro）のフォルダ名には `Subsc` を入れない。DVL は、項目名のドライバ名と CPU が対象と合うか見る。kbd の DVL に mou の項目が混ざるような取り違えは、エラーになる。

## DVL の作り方

既定の `--dvl cli` は、`dvl.exe /manualCreate oib_kbd <cpu>` を、ドライバのフォルダで実行する。VS2022 の操作は要らない。

OIB のプロジェクトは Code Analysis を実行しないので、`dvl.exe` が作る DVL は、VS2022 の拡張が作るものと同じになる。2026-11-26 に、OSS 1.03 の SARIF から作った kbd の DVL が、HLK に出した DVL とバイト単位で一致することを確かめた（SHA-256 が同じ）。

VS2022 の拡張で作りたいときは、`--dvl manual` を指定する。SARIF を `driver\keyboard`、`driver\mouse` に置いて待つので、表示に従って、`oib_kbd`（ARM64 は `oib_kbd_arm64`）を選び、Driver → Create Driver Verification Log を実行して、Enter を押す。クリップボードのエラーダイアログは、× で閉じてよい。

## HLK 版フォルダの構成

既定は `{driver}\{arch}\`（例: `oib_kbd\x64\`）。過去の版には `{arch}\{driver}\`（例: `x64\oib_kbd\`）のものもある。そのときは、`oib-whql.json` の `HlkLayout` を変えるか、`--layout "{arch}\{driver}"` を付ける。`verify` が確かめる `driver`、`Symbol` のフォルダ名は、大文字小文字と単複の違い（`drivers`、`symbol`）を吸収する。

## 失敗したとき

- `prepare` が、元のファイルが無いと言って止まる: 該当の CPU の Release ビルドが済んでいない。表示されたパスのビルドを先に行う。
- `database create` が失敗する: `whql-run.log` のビルドのエラーを見る。同じ bat を単独で実行すると、原因を追いやすい。
- DB の検査で止まる: 別のリポジトリの DB が残っているか、版違いのビルドが混ざっている。メッセージのとおり、DB のフォルダを確認する。
- DVL の検査で止まる: 項目名のドライバ名や CPU が違うか、SARIF が反映されていない。その CPU だけを、`--arch` で絞ってやり直す。
- DVL に未実行の必須クエリ（`SEMMLE_MUSTRUN_SKIPPED`）の警告が出る: クエリスイートが `mustrun.qls` でない。`oib-whql.json` の `QuerySuite` を確認する。
