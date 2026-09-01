# OpenInputBridge

**Windows用キーボード/マウス入力インターセプトドライバ。[Interception](https://github.com/oblitum/Interception) が公開しているユーザーモードAPI（LGPL）とプロトコル互換のカーネルドライバを、クリーンルームで独自実装するプロジェクトです。**

> ⚠️ 本プロジェクトは [oblitum/Interception](https://github.com/oblitum/Interception) およびその作者 Francisco Lopes 氏とは**無関係の非公式プロジェクト**です。"Interception" の名称・商標は使用していません。

## これは何か

まずオリジナル実装である Interception のことから。Interceptionはキーボード/マウスの入力をカーネルレベルで捕捉・改変・再注入できる著名なWindows用ドライバです。ユーザーモードのC APIライブラリ（`interception.h` / `interception.c`）はLGPLで公開されています。しかしながら、実際に入力をフックするカーネルドライバ本体は、商用ライセンスを購入できればソースコードが入手可能となります。しかし、それは改造はできるものの、公開できずクローズドなものでした。

OpenInputBridge は、この**カーネルドライバ部分**を、

- 公開されているLGPLライブラリのソースコード（＝両者間の通信プロトコル仕様そのもの）
- Microsoft公式のWDK公開サンプル・ドキュメント（kbfiltr / moufiltr 等）

のみを根拠に、クリーンルームで独自に再実装するプロジェクトです。プロプライエタリなドライバのバイナリを逆アセンブルする、あるいはそのソースを参照することは一切行っていません（詳細は [`docs/CLEAN_ROOM.md`](docs/CLEAN_ROOM.md)）。

これにより、Interception 互換のドライバ配下で、既存のオープンソースなユーザーモードライブラリや、それに依存する既存アプリケーション（AutoHotkeyのInterception fork等）を無改変で動かせることを目指します。

## ステータス

コア機能（ドライバ本体・インストーラ・コード署名）は実装済みで、実機での動作確認もほぼ完了済です。他ドキュメント中の「M0」「M5」等の表記は、以下のマイルストーンを指しています。実装済みでも実機テストがまだ済んでいない部分は、状態欄にその旨を明記しています。

| マイルストーン | 内容 | 状態 |
|---|---|---|
| M0 | ドライバ骨格（常時20個のコントロールデバイスを作成） | ✅ 完了・実機確認済み |
| M1 | キーボード/マウスへのフィルタアタッチ・素通し | ✅ 完了・実機確認済み |
| M2 | スロット管理・`IOCTL_GET_HARDWARE_ID` | ✅ 完了・実機テスト済み（`hardwareid.exe`でキーボード/マウス複数台のハードウェアID取得を確認） |
| M3 | フィルタビットマスク・捕捉キュー・`IOCTL_READ`/`IOCTL_SET_EVENT` | ✅ 実装・実機テスト済み。実機テストで捕捉キューのイベントクリア漏れとマウスフィルタの照合方式（overlap/superset）の2件のバグを発見・修正済み |
| M4 | `IOCTL_WRITE`（合成入力の注入／捕捉ストロークの解放） | ✅ 実装・実機テスト済み |
| M5 | `IOCTL_SET_PRECEDENCE`/`IOCTL_GET_PRECEDENCE`（precedenceフックチェーン） | ✅ 完了・実機テスト済み。`tests/precedence_blackbox/`の`precedence_probe`による複数プロセス同時アタッチの実機テストで、配送順序・チェーン継続・捕捉による遮断・同precedenceでのアタッチ順タイブレークすべて想定通りと確認済み。フィルタのビット照合規則自体は引き続き実物との検証待ち（[docs/PROTOCOL.md](docs/PROTOCOL.md)参照） |
| M6 | インストーラ（`DiInstallDriver`/`DiUninstallDriver` + UpperFilters登録） | ✅ 完了・実機テスト済み（インストール/アンインストール/再起動サイクル、サービス登録、UpperFilters順序を確認） |
| M7 | コード署名 | ✅ EV署名・WHQL署名ともに完了。HLKでSystemクラス扱い（69件のテスト対象）と判明したため、キーボード用（`oib_kbd.sys`）・マウス用（`oib_mou.sys`）の2ドライバに分割し（[`docs/DECISIONS.md`](docs/DECISIONS.md)の2026-08-01付エントリ参照）、分割後のHLK再提出・WHQL署名取得も完了（`oib_kbd.cat`/`oib_mou.cat`ともにWindows Hardware Compatibility Publisherの署名を確認済み） |
| M8 | 無改変のoblitum/Interceptionライブラリ・実アプリでの互換性テスト | ✅ 完了・実機確認済み。[`tests/upstream_lib/`](tests/upstream_lib/)に、`third_party/interception/samples/`配下の全サンプル（`identify`/`hardwareid`/`caps2esc`/`axes`/`cadstop`/`mathpointer`/`x2y`）と自作の`identify2`をビルドするプロジェクトを用意し、M0/M2/M3/M4の基本動作・実際のキーリマップ（キーボード/マウス双方）・`INTERCEPTION_FILTER_KEY_ALL`での広範なフィルタ照合・絶対座標指定と高頻度連続WRITEまで実機確認済み。特にkbdclassより下でのCtrl+Alt+Del捕捉がWindows 11でも機能することを確認（`cadstop`。Win32のフックでは原理的に不可能な動作）。無関係な複数ツール（`x2y`+`cadstop`）を同時にアタッチしても互いに干渉せず動作することも実機確認済み。実際のアプリでの検証として、Interceptionデバイスドライバモードを持つキーリマップソフト[kanata](https://github.com/jtroo/kanata)での動作確認を実施し、tap-holdおよびキーリマップ動作を確認済み。さらに[`tests/ahk_client/`](tests/ahk_client/)で、AutoHotkeyのInterception fork[AutoHotInterception](https://github.com/evilC/AutoHotInterception)（v0.9.2）を無改変で動かし、物理キーボード2台・マウス2台の構成で、デバイスごとの入力捕捉・出力（合成注入）・デバイス単位のリマップ（クロストーク無し）・4台同時稼働のいずれも実機確認済み（2026-08-30）。ただし今回のキーボード2台は型番が異なっていたため、`docs/PROTOCOL.md`記載の「同一VID/PIDの個体はスロット番号でのみ区別できる」という制約自体の検証は今後の課題として残る |
| M9 | デバイス総数上限（現行20台）の撤廃（将来対応） | 📋 未着手。20台のうちキーボード/マウスへの**配分比率**は`KeyboardSlotCount`で可変にできるようになりました（2026-08-02、[docs/DECISIONS.md](docs/DECISIONS.md)参照）が、これはM9とは別物で、合計20台という上限そのものはまだ撤廃していません。現行上限のうちキーボード10台は実機で確認済み、マウス10台は検証機材の都合で未確認（[docs/PROTOCOL.md](docs/PROTOCOL.md)参照）。総数上限を増やすには、Interception側headerファイルに固定値が入っているので、これを変えるのが簡単ですが、クライアントはbuildやり直しとなります。結局のところ、クライアント向けに新たなAPIを提供して、buildしてもらい、動作モード切替えなどの方法をとることになると考えます。 |

詳細なタスク管理は [Issues](../../issues) / [Projects](../../projects) を参照してください。

**M7（WHQL署名）について**: Microsoftはスタンドアロンのattestation signingを公開用には推奨しておらず、広く一般に使ってもらう場合には、WHQL署名が必要です。WHQL署名を得るにはHLKテストの実施・提出が前提となります。本ドライバは当初キーボード/マウス両クラスに対応する単一バイナリ（`Class=System`）でしたが、HLKでのテスト対象が69件と膨大になることが判明したため、キーボード用（`oib_kbd.sys`, `Class=Keyboard`）・マウス用（`oib_mou.sys`, `Class=Mouse`）の2ドライバに分割しました。分割後の両ドライバについてHLK再提出・WHQL署名取得が完了しています。

## アーキテクチャ概要

- KMDF（Kernel-Mode Driver Framework）ベースのフィルタドライバ。キーボード用（`oib_kbd.sys`, `Class=Keyboard`）・マウス用（`oib_mou.sys`, `Class=Mouse`）の2バイナリで構成（`driver/common/`の共通ロジックを両方から参照）。`keyboard.sys`/`mouse.sys`という素直な名前ではないのは、Windows標準搭載の`keyboard.inf`/`mouse.inf`（PS/2キーボード/マウスのインボックスドライバ）とDriver Store上で名前が衝突するため（[docs/DECISIONS.md](docs/DECISIONS.md)の2026-08-02付エントリ参照）
- それぞれ対応するデバイスクラススタックに上位フィルタとしてアタッチし、`IOCTL_INTERNAL_*_CONNECT` によるクラスサービスコールバックの差し替えで入力を捕捉/再注入
- Interceptionでは、キーボードとマウスの両方のドライバのインストールが必要であり、それを踏襲しています。
- 物理デバイスの有無によらず常時20個のコントロールデバイスを公開し、上位のユーザーモードライブラリとの互換性を維持。既定ではキーボード×10・マウス×10だが、OpenInputBridgeでは、この配分を`KeyboardSlotCount`レジストリ値（インストーラの`--slots=N`）で変更可能（詳細は[インストール](#インストール)・[`docs/PROTOCOL.md`](docs/PROTOCOL.md)参照）
- 新規クライアントが実際の配分やOpenInputBridge自体の識別を確認できるよう、`IOCTL_GET_KEYBOARD_SLOT_COUNT`/`IOCTL_GET_DRIVER_IDENTITY`という独自拡張IOCTLも用意（[`docs/PROTOCOL.md`](docs/PROTOCOL.md)参照）
- 詳細なプロトコル仕様は [`docs/PROTOCOL.md`](docs/PROTOCOL.md) を参照
- 互換性のため当面はoblitum/Interceptionライブラリの仕様どおり計20デバイス（上記の配分次第でキーボード/マウスそれぞれ最大20台）が上限です。将来的には、この総数の上限を撤廃する拡張を計画しています。既存クライアントでは、headerファイルで個数が決め打ちとなっているため、後方互換を保ったままは実現できず、動作モードを切り替えるなどの工夫が必要だろうと考えます。

## ライセンスと配布方針

本リポジトリは**フルオープンソース**です。

| 配布物 | ライセンス / 価格 |
|---|---|
| ソースコード一式（本リポジトリ） | [MIT License](LICENSE) ・無償 |
| セルフビルド版バイナリ（テスト署名モードで動作） | 無償・自己責任でビルド |
| WHQL署名付きドライバ | **有償**（詳細は近日公開） |

ソースコードは誰でも自由に読む・改変する・自分でビルドして使うことができます。一方で、Windowsカーネルドライバとして一般利用者が手軽に・安全に導入できる**WHQL署名をつけたデバイスドライバ**は、証明書取得・認定・継続的なサポートのコストを賄うため有償で提供します。なお本プロジェクトでは、セルフビルド版バイナリは提供していません。

また `third_party/interception/` に取り込んでいるoblitum/Interceptionのユーザーモードライブラリ（`interception.c` / `interception.h`）は、無改変のまま元のLGPLライセンスを維持しています。

## インストール

ソリューションの`Packaging`プロジェクトをbuildして作られたzip（`OpenInputBridge.zip`）を展開すると、`oib_kbd\`・`oib_mou\`（それぞれ`.inf`/`.cat`/`.sys`）と
`OpenInputBridgeSetup.exe`・`setup.bat` が含まれています。

**注意**: このzipにはドライバ本体とインストーラのみが含まれ、クライアント側の`interception.dll`は配布していません。Interception互換の既存アプリを動かすには、[oblitum/Interception](https://github.com/oblitum/Interception)のLGPL公開ライブラリから別途`interception.dll`を入手してください（本リポジトリの`third_party/interception/`はこのライブラリのソースを無改変で取り込んだもので、interception.dllはbuild可能です。また、[`tests/upstream_lib/`](tests/upstream_lib/)が実際にこの方法でビルドしています）。

**事前準備(テスト署名/EV署名の場合)**: ドライバの署名がテスト署名、または（WHQL取得前の）EV署名の場合は、
インストール前に管理者権限のコマンドプロンプトで以下を実行し、再起動しておいてください。

```bat
bcdedit /set TESTSIGNING ON
```

なお、セキュアブートやBitLockerを利用されている場合には、BitLockerをWindowsの設定で解除して、
セキュアブートをBIOS画面で無効にする必要があります。誤って先にセキュアブートを解除すると、
起動時にBitLockerのキーを求められます。
WHQL署名済みドライバではこの手順は不要です。

### `setup.bat`によるインストール手順

- setup.batを実行すれば、インストールされます。以下は、batファイルでの実施内容です。

- x64かつWindows 11以降かを確認し（Windows 10は、最新の22H2であっても**非対応**です。
  インストールするとキーボード/マウスが再起動後に使用不可となります）、満たさなければ
  「This is the wrong Windows version. It's for Windows 11.」と表示してその場で中止する
- 条件を満たせば、自ら管理者権限へ昇格し（.batファイル自体にはUACを自動表示するマニフェストが
  無いため）、キーボード用・マウス用の両方をインストールする
- [監査ログ・通知機能](#監査ログ・通知機能)を既定で有効化する（内部的には
  `OpenInputBridgeSetup.exe --enable-audit-log`・`--enable-toast`を順に実行するだけの単純な
  ラッパー）
- 最後に`--verify-install`でインストール結果を確認する（フィルタ登録はあるのに実体の
  ドライバファイルが見当たらない場合は自動的に登録を削除し、監査ログ・通知機能を有効化して
  いない場合はその旨のメッセージを表示する）

監査ログ・通知機能が不要な場合は、インストール後に`OpenInputBridgeSetup.exe --disable-audit-log`・
`--disable-toast`で無効化できます。片方のドライバだけをインストールしたい、`--slots=N`で
キーボード/マウスの配分を変えたい、といった場合は`setup.bat`を使わず後述のように
`OpenInputBridgeSetup.exe`を直接、必要な引数を付けて実行してください。

`OpenInputBridgeSetup.exe` は静的にCRTをリンクしているため、別途Visual C++
再頒布可能パッケージをインストールする必要はありません。

完了後、**再起動してください。** `UpperFilters`（デバイスクラスへのフィルタ登録）はOS起動時の
デバイススタック構築時にのみ反映されるため、再起動なしでは有効になりません。

再起動後、管理者権限のコマンドプロンプトから以下を確認できます（`sc`コマンド自体の実行に
管理者権限が必要です）。

- `sc query OpenInputBridgeKeyboard` / `sc query OpenInputBridgeMouse`: TYPEが
  `KERNEL_DRIVER`、STATEが`RUNNING`であること
- `sc config ... start=`は次回起動時の開始種別を変えるだけで、現在の動作中の状態には影響しない
- `sc stop`もこれらのサービスに対しては無効（エラー1052）。動作中のドライバの状態を`sc`から
  直接変更することはできない

`OpenInputBridgeSetup.exe`を直接実行する場合も、管理者権限が必要なマニフェストが付与されているため、
実行すると自動的にUACの昇格プロンプトが表示されます。引数なしでキーボード用・マウス用の両方が
インストールされます（`keyboard`/`mouse`を引数に指定すると片方だけも可能ですが、HLK実施時のみにお使いください。アーキテクチャー上、動作には両方のドライバがインストールされている必要があります。）。上記のWindows
バージョン/アーキテクチャの確認は、`setup.bat`経由かどうかに関わらず`OpenInputBridgeSetup.exe`
自身が毎回の実行時に行います（`--skip-version-check`で無効化できますが、これはPro/Subscription版
の独自インストーラーが自前で同等のチェックを行っている場合などに使う想定で、通常は指定不要です）。

### アンインストール手順

監査ログ・トースト通知機能を有効化していた場合は、管理者権限コマンドプロンプトにて、`OpenInputBridgeSetup.exe --disable-audit-log`・`OpenInputBridgeSetup.exe --disable-toast`も実行してください。タスクスケジューラーのタスクが無くなります。

デバイスドライバのアンインストールは `OpenInputBridgeSetup.exe /uninstall` を管理者権限で実行し、再起動してください

引数なしで両方、`keyboard`/`mouse`指定で片方だけアンインストールできます。なお片方だけアンインストールすると、通常状態とは異なるので、アーキテクチャー上、正常動作しません。HLK実施時に両方インストールしてしまった時に使う用途です。

`pnputil -d oemXX.inf`は絶対に使用しないでください。infに含まれず、インストーラーが設定している情報が残ったままとなり、次回再起動時に、キーボードやマウスが使用不可となります。

### インストール時に、キーボード/マウスの配分を変える場合

既定では`\\.\interception00`〜`19`の20個を
キーボード10個・マウス10個に均等配分しますが、`--slots=N`（`keyboard`/`mouse`指定と併用、
インストール時のみ）でこの配分を変更できます。

例えば`OpenInputBridgeSetup.exe install
keyboard --slots=15`とすると、キーボード15個・マウス5個の配分になり、もう片方が
既にインストール済みならその配分も自動的に追随します。

この配分は本家Interceptionの
ライブラリが前提とする固定の10/10分割とは異なるため、**古い（この仕様を知らない）
クライアントは既定以外の配分では正しく動作しません**（クラッシュはしませんが、
境界がズレた範囲を誤ったデバイス種別として扱い、捕捉できない・データを誤解釈する、
といったサイレントな不具合になります）。詳細は
[`docs/PROTOCOL.md`](docs/PROTOCOL.md)を参照してください。


## 監査ログ・通知機能

`OpenInputBridgeSetup.exe`には、Interceptionプロトコル互換のコントロールデバイス
（`\\.\interceptionNN`）を開いたプロセスをWindows標準のセキュリティイベントログに記録する
監査ログ機能と、その発生をトースト通知でリアルタイムに知らせる通知機能を用意しています
（いずれもデバイスドライバ自体は改変せず、OS標準機能のみで実装しています）。`setup.bat`経由で
インストールした場合は既定で両方とも有効になります。設計判断の詳細は
[`docs/DECISIONS.md`](docs/DECISIONS.md)、既知の限界・検討した代替案は
[`docs/SECURITY_CONSIDERATIONS.md`](docs/SECURITY_CONSIDERATIONS.md)を参照してください。

**重要**: これらの機能は、zipを展開した場所にある`OpenInputBridgeSetup.exe`自身のフルパスを
タスクスケジューラーのタスクに登録して動作します。そのため、**zipファイルを展開した場所は
インストール後も削除・移動しないでください**。展開した場所がそのまま実質的なインストール先に
なります。

**監査ログの確認方法**: 監査ログが実際に記録されているかは、イベントビューアーで確認できます。
「イベントビューアー → Windowsログ → セキュリティ」を開き、イベントID **4656**、
`ObjectName`が`\Device\interceptionNN`（`NN`は00〜19の数字）になっているものを探し、
`プロセス名`が意図したプロセスかどうかを確認してください。心当たりのないプロセス名が
記録されていた場合、そのプロセスが実際にInterceptionプロトコル互換のキー/マウス入力を
観測・注入できる状態にあることを意味します。

**トースト通知の利用方法**: トースト通知では、デバイスドライバを開いたプロセス名を表示します。プロセス名が分からない場合は、Unknownとなりますが、判明している場合、トースト通知をクリックすると、該当プロセス(実行ファイル)が存在するフォルダをエクスプローラーで開きます。その実行ファイルが意図して起動したものであるか確認が可能です。もしも知らない実行ファイルであった場合、まだ開いたばかりなので、キーボードやマウス操作をほとんど覗かれていません。タスクマネージャーから該当プロセスを終了させたり、不要なプログラムであればアンインストールされることをお勧めします。

以下は管理者権限のコマンドプロンプトから実行してください。

```bat
:: 監査ログの有効化・無効化
OpenInputBridgeSetup.exe --enable-audit-log
OpenInputBridgeSetup.exe --disable-audit-log

:: トースト通知の有効化・無効化（有効化には監査ログの有効化が前提）
OpenInputBridgeSetup.exe --enable-toast
OpenInputBridgeSetup.exe --disable-toast

:: 信頼しているプロセスについては通知だけを抑制できます（監査ログ自体は引き続き全件記録されます）
OpenInputBridgeSetup.exe --allow-process "C:\full\path\to\app.exe"
OpenInputBridgeSetup.exe --disallow-process "C:\full\path\to\app.exe"
OpenInputBridgeSetup.exe --list-allowed-processes

:: インストール後の状態確認（setup.batが最後に自動実行します）
OpenInputBridgeSetup.exe --verify-install
```

## ビルド方法

### リポジトリの取得

`third_party/interception` は [oblitum/Interception](https://github.com/oblitum/Interception) の `library/` を無改変で取り込んだgit submoduleです。**submoduleごと取得してください。**

```sh
git clone --recurse-submodules https://github.com/Applet-LLC/OpenInputBridge.git
```

すでに通常の `git clone` 済みの場合は、以下でsubmoduleを取得できます。

```sh
git submodule update --init --recursive
```

### ビルド（要WDK、`OpenInputBridge.sln`）

`driver/` はKMDF（Kernel-Mode Driver Framework）ベースのWindowsカーネルドライバです。ビルドには以下が必要です。

- Visual Studio 2022以降
- Windows Driver Kit (WDK)あるいはEWDK（Enterprise WDK、ISOイメージをマウントして使うスタンドアロン版）

リポジトリ直下の `OpenInputBridge.sln` に、ドライバ・インストーラ・署名/パッケージングの3プロジェクトを
まとめてあります。ドライバ／インストーラのビルドから、EV署名・`Signed\`への集約・配布用zip作成
（`dist\OpenInputBridge.zip`）までを、ソリューション構成の切り替えだけで一括処理できます。

| ソリューション構成 | 内容 |
|---|---|
| `Debug` | ドライバ・インストーラのみビルド（テスト署名、開発用）。署名・パッケージングは実行されません。**自分でビルドして試す場合はこちら**（前掲の表の「セルフビルド版バイナリ」に対応） |
| `Release` | ドライバ・インストーラをビルドし、両方をこのプロジェクトのEV証明書で署名して`packaging\Signed\`に集約、`dist\OpenInputBridge.zip`を作成（WHQL署名を取得する前の配布物） |
| `ReleaseWHQL` | インストーラのみEV署名して集約し、`packaging\Signed\oib_kbd\`・`packaging\Signed\oib_mou\`はHLK/WHQL申請から返ってきたファイルを**手動で配置したまま上書きしない**でzip作成（WHQL署名取得後の配布物） |

`Release`/`ReleaseWHQL`はこのプロジェクト（Applet LLC）名義のEV証明書を前提としており、リポジトリ所有者以外の環境では意図した形で完走しません。証明書が無い環境で`Release`/`ReleaseWHQL`を使うと、`packaging\sign.mak`の署名コマンドは失敗しますが、失敗を無視する作りになっているため**ビルド自体は正常終了し、未署名（テスト署名ですらない）のバイナリがそのまま`dist\OpenInputBridge.zip`に入ってしまいます**。この状態のバイナリはテスト署名を有効にしたWindowsでもロードできません。EV証明書を持たない場合は、必ず`Debug`構成を使ってください。

EWDKを使う場合はビルド環境をセットアップしたコマンドプロンプトから、そのままソリューション全体をビルドできます。自分でビルドして試す場合（EV証明書なし）は`Debug`構成を指定してください。

```bat
K:\BuildEnv\SetupBuildEnv.cmd amd64
msbuild OpenInputBridge.sln /p:Configuration=Debug /p:Platform=x64
```

同じ環境変数を継いだまま `devenv OpenInputBridge.sln` でVisual Studio IDEを開いても操作できます。

`ReleaseWHQL`構成を使う前に、HLK/WHQL申請から返ってきた `oib_kbd.inf`/`oib_kbd.cat`/`oib_kbd.sys` を
`packaging\Signed\oib_kbd\` に、`oib_mou.inf`/`oib_mou.cat`/`oib_mou.sys` を
`packaging\Signed\oib_mou\` に、それぞれ手動でコピーしておいてください
（`Signed\Symbol\` はこちらの手元のビルドから毎回自動で更新されます）。

署名・パッケージングの内部的な処理内容（個別のnmakeターゲット等）は `packaging/sign.mak` のコメントを
参照してください。ロードマップは [ステータス](#ステータス) を参照してください。

### 動作確認（`tests/upstream_lib/`・`tests/precedence_blackbox/`）

無改変の`third_party/interception/library/interception.c`と、その全サンプル
（`identify`/`hardwareid`/`caps2esc`/`axes`/`cadstop`/`mathpointer`/`x2y`）、および実機テスト用に
自作した`identify2`・`precedence_probe`も、同じ`OpenInputBridge.sln`のDebug/Release構成で
ビルドされます。インストール済みのOpenInputBridgeに対して、キーボード/マウスの捕捉・
パススルーはもちろん、実際のキーリマップ・precedenceフックチェーン・Ctrl+Alt+Del捕捉まで
手元で確認できます。詳細は [`tests/upstream_lib/README.md`](tests/upstream_lib/README.md)・
[`tests/precedence_blackbox/README.md`](tests/precedence_blackbox/README.md) を参照してください。

## 貢献

Issue / Pull Request 歓迎です。カーネルドライバというセキュリティ上センシティブな領域のプロジェクトのため、変更内容によってはレビューに時間がかかる場合があります。

## クリーンルーム方針

本プロジェクトは以下のみを参照して実装しています。

- Interception のLGPL公開ライブラリソースコード（プロトコルの一次情報源）
- Microsoft公式のWDK/KMDFドキュメントおよび公開サンプルドライバ
- 実物ドライバに対するブラックボックスなI/O挙動観察（逆アセンブル・逆コンパイルは一切行わない）

詳細な証跡は [`docs/CLEAN_ROOM.md`](docs/CLEAN_ROOM.md) に記録しています。

## 利用に関する注意

Interceptionプロトコル互換ドライバは、その性質上、インストール後は管理者権限のない一般ユーザー
プロセスからでもシステム全体のキーボード/マウス入力を観測・注入できます（本家Interceptionと同じ、
プロトコル互換性のための意図的な仕様）。この性質は、不正アクセス・チート行為・本人の同意のない
監視（キーロガー的用途）など、悪用され得る形でも利用可能であることを意味します。

本ソフトウェアを、第三者への不正アクセス、オンラインゲーム等におけるチート行為やアンチチート
機構の回避、本人の同意のない監視・記録目的で使用しないでください。

技術的な検知可能性・悪用リスクの詳細な調査結果、および現時点で未着手の対策案については
[`docs/SECURITY_CONSIDERATIONS.md`](docs/SECURITY_CONSIDERATIONS.md) にまとめています。

## 免責事項

MIT Licenseにもありますが、本ソフトウェアのソースコード、および生成物であるバイナリ(EV署名版、WHQL署名版を入れたサブスクリプション版、Pro版を含む)を使用して、何らかの不利益や事故が発生したとしても、その責任は開発者や販売元ではなく、利用者にあります。例えば他のアンチウィルスソフトやアンチチートソフトによって、本ドライバが阻害されるようなことがあっても、善処には努めますが、それに対して改善を約束することはできません。なお、バグなどが起因して不具合に遭遇した場合、改良可能な不具合であれば、誠意をもって改善に努めたいと思います。

## License

自作部分は [MIT License](LICENSE) です。`third_party/interception/` 配下は oblitum/Interceptionライブラリ の LGPL に従います。
