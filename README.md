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

コア機能（ドライバ本体・インストーラ・コード署名）は実装済みで、実機での動作確認も進めています。他ドキュメント中の「M0」「M5」等の表記は、以下のマイルストーンを指しています。実装済みでも実機テストがまだ済んでいない部分は、状態欄にその旨を明記しています。

| マイルストーン | 内容 | 状態 |
|---|---|---|
| M0 | ドライバ骨格（常時20個のコントロールデバイスを作成） | ✅ 完了・実機確認済み |
| M1 | キーボード/マウスへのフィルタアタッチ・素通し | ✅ 完了・実機確認済み |
| M2 | スロット管理・`IOCTL_GET_HARDWARE_ID` | ✅ 実装済み。スロット割当は実機確認済みだが、`IOCTL_GET_HARDWARE_ID`自体は未テスト |
| M3 | フィルタビットマスク・捕捉キュー・`IOCTL_READ`/`IOCTL_SET_EVENT` | ✅ 実装・実機テスト済み。実機テストで捕捉キューのイベントクリア漏れとマウスフィルタの照合方式（overlap/superset）の2件のバグを発見・修正済み |
| M4 | `IOCTL_WRITE`（合成入力の注入／捕捉ストロークの解放） | ✅ 実装・実機テスト済み |
| M5 | `IOCTL_SET_PRECEDENCE`/`IOCTL_GET_PRECEDENCE`（precedenceフックチェーン） | ✅ 実装済み。配送モデルは作者本人の技術記事で確認済み（[docs/CLEAN_ROOM.md](docs/CLEAN_ROOM.md)）だが、**複数プロセス同時アタッチでの実機テストは未実施**、フィルタのビット照合規則も実物との検証待ち（[docs/PROTOCOL.md](docs/PROTOCOL.md)参照） |
| M6 | インストーラ（`DiInstallDriver`/`DiUninstallDriver` + UpperFilters登録） | ✅ 完了・実機テスト済み（インストール/アンインストール/再起動サイクル、サービス登録、UpperFilters順序を確認） |
| M7 | コード署名 | 🔶 EV署名は完了・動作確認済み。HLKテストを実施しWHQL署名の取得を目指す |
| M8 | 無改変のoblitum/Interceptionライブラリ・実アプリでの互換性テスト | 🚧 進行中。[`tests/upstream_lib/`](tests/upstream_lib/)のサンプルでM0/M2（部分）/M3/M4の基本動作は実機確認済みだが、AutoHotkeyのInterception fork等、実際の消費者アプリでの検証は未実施 |
| M9 | デバイス数上限（現行20台）の撤廃（将来対応） | 📋 未着手。現行上限のうちキーボード10台は実機で確認済み、マウス10台は検証機材の都合で未確認（[docs/PROTOCOL.md](docs/PROTOCOL.md)参照） |

詳細なタスク管理は [Issues](../../issues) / [Projects](../../projects) を参照してください。

**M7（WHQL署名）について**: Microsoftはスタンドアロンのattestation signingを既に提供しておらず、WHQL署名を得るにはHLKテストの実施・提出が前提となります。本ドライバは現在キーボード/マウス両クラスに対応する単一バイナリですが、HLKの試験要件・結果次第では、キーボード用・マウス用でドライバを分割する可能性があります。

## アーキテクチャ概要

- KMDF（Kernel-Mode Driver Framework）ベースのフィルタドライバ
- キーボード/マウスのデバイスクラススタックに上位フィルタとしてアタッチし、`IOCTL_INTERNAL_*_CONNECT` によるクラスサービスコールバックの差し替えで入力を捕捉/再注入
- 物理デバイスの有無によらず常時20個（キーボード×10、マウス×10）のコントロールデバイスを公開し、上位のユーザーモードライブラリとの互換性を維持
- 詳細なプロトコル仕様は [`docs/PROTOCOL.md`](docs/PROTOCOL.md) を参照
- 互換性のため当面はoblitum/Interceptionライブラリの仕様どおりキーボード10台・マウス10台（計20デバイス）が上限ですが、将来的には既存クライアントとの後方互換を保ったまま、この上限を撤廃する拡張を計画しています

## ライセンスと配布方針

本リポジトリは**フルオープンソース**です。

| 配布物 | ライセンス / 価格 |
|---|---|
| ソースコード一式（本リポジトリ） | [MIT License](LICENSE) ・無償 |
| セルフビルド版バイナリ（テスト署名モードで動作） | 無償・自己責任でビルド |
| WHQL署名付きドライバ | **有償**（詳細は近日公開） |

ソースコードは誰でも自由に読む・改変する・自分でビルドして使うことができます。一方で、Windowsカーネルドライバとして一般利用者が手軽に・安全に導入できる**WHQL署名をつけたデバイスドライバ**は、証明書取得・認定・継続的なサポートのコストを賄うため有償で提供します。

なお `third_party/interception/` に取り込んでいるoblitum/Interceptionのユーザーモードライブラリ（`interception.c` / `interception.h`）は、無改変のまま元のLGPLライセンスを維持しています。

## インストール

配布されたzip（`OpenInputBridge.zip`）を展開すると、`drivers\`（`.inf`/`.cat`/`.sys`）と
`OpenInputBridgeSetup.exe` が含まれています。

1. `OpenInputBridgeSetup.exe` を実行します（管理者権限が必要なマニフェストが付与されているため、
   実行すると自動的にUACの昇格プロンプトが表示されます）。
2. 完了後、**再起動してください。** `UpperFilters`（デバイスクラスへのフィルタ登録）はOS起動時の
   デバイススタック構築時にのみ反映されるため、再起動なしでは有効になりません。
3. `sc query OpenInputBridge` で `SERVICE_KERNEL_DRIVER` / `SERVICE_SYSTEM_START` として
   登録されていることを確認できます。

アンインストールは `OpenInputBridgeSetup.exe /uninstall` を管理者権限で実行し、同様に再起動してください。

`OpenInputBridgeSetup.exe` は静的にCRTをリンクしているため、別途Visual C++
再頒布可能パッケージをインストールする必要はありません。

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
| `Debug` | ドライバ・インストーラのみビルド（テスト署名、開発用）。署名・パッケージングは実行されません |
| `Release` | ドライバ・インストーラをビルドし、両方をこのプロジェクトのEV証明書で署名して`packaging\Signed\`に集約、`dist\OpenInputBridge.zip`を作成（WHQL署名を取得する前の配布物） |
| `ReleaseWHQL` | インストーラのみEV署名して集約し、`packaging\Signed\Drivers\`はHLK/WHQL申請から返ってきたファイルを**手動で配置したまま上書きしない**でzip作成（WHQL署名取得後の配布物） |

EWDKを使う場合はビルド環境をセットアップしたコマンドプロンプトから、そのままソリューション全体をビルドできます。

```bat
K:\BuildEnv\SetupBuildEnv.cmd amd64
msbuild OpenInputBridge.sln /p:Configuration=Release /p:Platform=x64
```

同じ環境変数を継いだまま `devenv OpenInputBridge.sln` でVisual Studio IDEを開いても操作できます。

`ReleaseWHQL`構成を使う前に、HLK/WHQL申請から返ってきた `OpenInputBridge.inf` / `openinputbridge.cat` /
`OpenInputBridge.sys` を `packaging\Signed\Drivers\` に手動でコピーしておいてください
（`Signed\Symbol\` はこちらの手元のビルドから毎回自動で更新されます）。

署名・パッケージングの内部的な処理内容（個別のnmakeターゲット等）は `packaging/sign.mak` のコメントを
参照してください。ロードマップは [ステータス](#ステータス) を参照してください。

### 動作確認（`tests/upstream_lib/`）

無改変の`third_party/interception/library/interception.c`と、実機での動作確認用サンプル
（`identify`/`identify2`）も同じ`OpenInputBridge.sln`のDebug/Release構成でビルドされます。
インストール済みのOpenInputBridgeに対してキーボード/マウスの捕捉・パススルーが実際に機能しているかを
手元で確認できます。詳細は [`tests/upstream_lib/README.md`](tests/upstream_lib/README.md) を参照してください。

## 貢献

Issue / Pull Request 歓迎です。カーネルドライバというセキュリティ上センシティブな領域のプロジェクトのため、変更内容によってはレビューに時間がかかる場合があります。

## クリーンルーム方針

本プロジェクトは以下のみを参照して実装しています。

- Interception のLGPL公開ライブラリソースコード（プロトコルの一次情報源）
- Microsoft公式のWDK/KMDFドキュメントおよび公開サンプルドライバ
- 実物ドライバに対するブラックボックスなI/O挙動観察（逆アセンブル・逆コンパイルは一切行わない）

詳細な証跡は [`docs/CLEAN_ROOM.md`](docs/CLEAN_ROOM.md) に記録しています。

## License

自作部分は [MIT License](LICENSE) です。`third_party/interception/` 配下は oblitum/Interceptionライブラリ の LGPL に従います。
