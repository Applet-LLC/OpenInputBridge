<!--
Copyright (c) 2026 OpenInputBridge Contributors
SPDX-License-Identifier: MIT
Licensed under the MIT License. See LICENSE file in the project root for full license text.
-->

# ahk_client/

M8「無改変のoblitum/Interceptionライブラリ・実アプリでの互換性テスト」で唯一未実施として
残っている項目、**AutoHotkeyのInterception fork（実際のサードパーティ消費者アプリ）**の検証用。
`tests/upstream_lib/`がoblitum/Interception自身のサンプル群を対象にしているのに対し、こちらは
そのライブラリに依存する外部エコシステムの実アプリを、無改変のままOpenInputBridgeに対して
動かして互換性を確認する。

対象は [evilC/AutoHotInterception](https://github.com/evilC/AutoHotInterception)
（MIT License）。AutoHotkeyからInterceptionプロトコル互換ドライバを扱うためのラッパーで、
`AutoHotInterception.dll`（C#実装）が内部で無改変の`interception.dll`を呼び出す構成になっている。
`tests/upstream_lib/`の各サンプルと同じく、このリポジトリはユーザーモードのクライアント
（`\\.\interceptionNN`を開くだけの存在）であり、ドライバ本体（オリジナルのInterceptionが
提供する`interception.sys`相当）とは無関係。OpenInputBridgeが既に`\\.\interceptionNN`
（00〜19）を公開しているので、AutoHotInterception側は「本物のドライバかOpenInputBridgeか」を
区別せずそのまま動くはず、というのがこのテストで検証したい互換性そのものになる。

対象デバイス構成: 物理キーボード2台・物理マウス2台。可能であれば同一型番（同一VID/PID）の
ペアを1組含めることが望ましい（`docs/PROTOCOL.md`に明記の既知の制約——同一VID/PIDの個体は
`IOCTL_GET_HARDWARE_ID`の文字列だけでは区別できず、区別できるのはスロット番号（接続順）の
みという点を、AutoHotInterception経由でも実際に確認できるため）。

## 重要: 本物のInterceptionドライバを絶対にインストールしないこと

AutoHotInterceptionのセットアップ手順には、本来`install-interception.exe`（本物の
Interceptionドライバのインストーラー、AutoHotInterceptionのリリースzipには含まれず別配布）を
先に実行する記載があるが、**この手順は絶対に行わないこと**。OpenInputBridgeと本物の
Interceptionドライバは、どちらも同じ「kbfiltr型（`IOCTL_INTERNAL_*_CONNECT`を横取りする）」
フィルタドライバであり、`docs/COEXISTENCE.md`に記録されている通り、この種のフィルタドライバ
同士の共存はOpenInputBridgeとして正式サポート対象外（キー捕捉の優先順位が位置依存になる、
`IOCTL_WRITE`での解放・注入が意図しない経路に固定される等の問題が起こり得る）。

以下のセットアップ手順は、AutoHotInterceptionのリリースzipに含まれる**クライアント側の
ファイルのみ**を使い、ドライバのインストールは一切行わない。OpenInputBridgeが既に
インストール・起動済みであることが前提。

## セットアップ手順

1. 前提: OpenInputBridgeがインストール・再起動済みで、`sc query OpenInputBridgeKeyboard` /
   `OpenInputBridgeMouse` の両方がSTATE=RUNNINGであることを確認しておく。
2. AutoHotkey v2をインストールする（[autohotkey.com](https://www.autohotkey.com/)）。
3. [AutoHotInterceptionのReleasesページ](https://github.com/evilC/AutoHotInterception/releases)
   から最新のリリースzipをダウンロードし、`Lib/`フォルダ（`AutoHotInterception.ahk`・
   `AutoHotInterception.dll`・`x86/interception.dll`・`x64/interception.dll`等一式）だけを
   このディレクトリ（`tests/ahk_client/`）直下の`Lib/`にコピーする。`install-interception.exe`は
   このzipには含まれていないはずだが、万一同梱されていても実行しないこと。
4. ダウンロードしたDLLがWindowsにブロックされている場合は、プロパティから「ブロックの解除」を
   行うか、`Lib/`フォルダに対して`Unblock-File`を実行する。
5. `identify_kbd.ahk`を実行し、キーボードを1台だけ操作してイベントが表示されることを確認してから、
   本番の2台構成に進む（`Lib/`の配置ミスがあればここで気付ける）。

`Lib/`はサードパーティのビルド済みバイナリのため、このリポジトリにはコミットしない
（`.gitignore`参照。`third_party/interception`のようなソース取り込み・サブモジュール化は
行わない方針。理由は上記の通り、これはOpenInputBridgeが依存するライブラリではなく、
外部の消費者アプリそのものを無改変で試す対象だから）。

## スクリプト一覧

いずれもキーボードのスロット番号は1〜10、マウスは11〜20（`docs/PROTOCOL.md`のデバイス構成
参照）。どの物理デバイスがどのスロット番号になったかは接続順で決まり、実行のたびに変わり
得るので、各スクリプト冒頭の`KB_A_ID`等の定数は、実行前に`identify_kbd.ahk`/
`identify_mouse.ahk`の出力を見て実際の値に書き換えること。

### `identify_kbd.ahk` — キーボード2台の捕捉ログ（非ブロッキング）

キーボード1〜10の全スロットを素通しのまま購読し、押下のたびにスロット番号・スキャンコード・
状態・タイムスタンプをGUIリストに表示する。`tests/upstream_lib/`の`identify2.exe`のAHK版。

**合格基準**: キーボードA・Bを交互に操作し、それぞれ正しいスロット番号で記録されること。
操作したキーボードは普段通り入力できること（素通し）。

### `identify_mouse.ahk` — マウス2台の捕捉ログ（非ブロッキング）

マウス11〜20の全スロットを素通しのまま購読し、移動・ボタン押下のたびにログを表示する。

**合格基準**: マウスA・Bを交互に操作し、それぞれ正しいスロット番号で記録されること。

### `remap_per_keyboard.ahk` — デバイス単位のキーリマップ

キーボードAでのみ「1」キーを「a」に、キーボードBでのみ「2」キーを「b」に置き換える。

**合格基準**: キーボードAで「1」を押すと「a」が入力され、「2」は普段通り「2」のまま。
キーボードBはその逆（「2」→「b」、「1」はそのまま）。片方の変更がもう片方に漏れていないこと
（クロストークが無いこと）の確認が主目的。

### `remap_per_mouse.ahk` — デバイス単位のマウスボタン置換・合成注入

マウスAの左クリックだけをブロックし、`SendMouseButtonEvent`による合成注入で右クリックに
置き換える。マウスBは素通しのまま。

**合格基準**: マウスAで左クリックすると右クリックとして反映される。マウスBは普段通り
左クリックが機能する。合成注入（`IOCTL_WRITE`経由の書き戻し）が意図したデバイスの経路だけを
通り、もう片方に漏れないことの確認が主目的。

### `combined_soak.ahk` — 4台同時・長時間動作テスト

上記のリマップ規則をキーボード2台・マウス2台すべてに同時適用した状態で動かし続ける。
`tests/upstream_lib/README.md`に記録済みの「無関係な複数ツール同時アタッチでも干渉しない」
実機確認結果の、AHK経由での再現に相当する。

**合格基準**: 数分以上、4台を継続的に操作してもクラッシュ・フリーズ・取りこぼしが無いこと。

### `testbench.ahk` — GUIテストベンチ

上記シナリオを1つのGUIにまとめたもの。デバイスごとのイベント件数・直近のイベント内容を
一覧表示し、チェックボックスでリマップ規則のON/OFFをその場で切り替えられる。セッション内容を
テキストログに書き出す（M8の実機確認記録として`README.md`/`docs/PROTOCOL.md`に転記する際の
証跡にする）。4台を何度も繋ぎ直しながら手動で確認する作業の手間を減らすためのツール。

## 結果の記録

テスト完了後、`README.md`のM8行、および該当すれば`docs/PROTOCOL.md`の既知の制約の節に、
確認できたシナリオ・AutoHotInterceptionのバージョン・見つかった問題（あれば）を追記すること。

## 実機確認結果（2026-08-30）

AutoHotkey v2.0.27（`AutoHotkey_2.0.27_setup.exe`）+
[AutoHotInterception](https://github.com/evilC/AutoHotInterception) v0.9.2（AHK v2版、
無改変のリリースzipから`Lib/`のみ配置）を、インストール済みのOpenInputBridgeに対して実施。
物理キーボード2台（型番違い、スロット1・4に割当）・物理マウス2台の構成で、本ディレクトリの
全スクリプト（`identify_kbd.ahk`/`identify_mouse.ahk`/`remap_per_keyboard.ahk`/
`remap_per_mouse.ahk`/`combined_soak.ahk`/`testbench.ahk`）を実行し、いずれも合格。

- キーボード/マウスそれぞれ2台の入力捕捉・素通しがデバイスごとに正しいスロット番号で記録されること
- デバイス単位のキー/マウスボタンのリマップが、対象デバイスにのみ適用され、もう片方に漏れない
  （クロストークが無い）こと
- 合成入力（`IOCTL_WRITE`経由の書き戻し）が意図したデバイスの経路だけを通ること
- 4台同時・複数のリマップ規則を並行稼働させても、クラッシュ・フリーズ・取りこぼしが無いこと

いずれも実機で確認済み。今回のキーボード2台は型番（VID/PID）が異なっていたため、
`docs/PROTOCOL.md`に記載の「同一VID/PIDの個体はハードウェアID文字列では区別できず、
スロット番号でのみ区別できる」という既知の制約自体は、今回は検証できていない（同一型番ペアでの
確認は今後の課題として残る）。

セットアップ時、ダウンロードしたDLLが未Unblockの状態だと`Lib\CLR.ahk`の`LoadFrom`が
`TargetInvocationException`（0x80131604）で失敗する事象を確認した。本README「重要」節記載の
Unblock手順で解消する、AutoHotInterception側の既知の挙動（OpenInputBridge固有の問題ではない）。
