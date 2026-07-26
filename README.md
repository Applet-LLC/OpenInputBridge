# OpenInputBridge

**Windows用キーボード/マウス入力インターセプトドライバ。[Interception](https://github.com/oblitum/Interception) が公開しているユーザーモードAPI（LGPL）とプロトコル互換のカーネルドライバを、クリーンルームで独自実装するプロジェクトです。**

> ⚠️ 本プロジェクトは [oblitum/Interception](https://github.com/oblitum/Interception) およびその作者 Francisco Lopes 氏とは**無関係の非公式プロジェクト**です。"Interception" の名称・商標は使用していません。

## これは何か

Interception は、キーボード/マウスの入力をカーネルレベルで捕捉・改変・再注入できる著名なWindows用ドライバです。ユーザーモードのC APIライブラリ（`interception.h` / `interception.c`）はLGPLで公開されていますが、実際に入力をフックするカーネルドライバ本体は、商用ライセンスを購入できればソースコードを入手可能ですが、それは公開できず制限が多いものでした。

OpenInputBridge は、この**カーネルドライバ部分**を、

- 公開されているLGPLライブラリのソースコード（＝両者間の通信プロトコル仕様そのもの）
- Microsoft公式のWDK公開サンプル・ドキュメント（kbfiltr / moufiltr 等）

のみを根拠に、クリーンルームで独自に再実装するプロジェクトです。プロプライエタリなドライバのバイナリを逆アセンブルする、あるいはそのソースを参照することは一切行っていません（詳細は [`docs/CLEAN_ROOM.md`](docs/CLEAN_ROOM.md)）。

これにより、Interception 互換のドライバ配下で、既存のオープンソースなユーザーモードライブラリや、それに依存する既存アプリケーション（AutoHotkeyのInterception fork等）を無改変で動かせることを目指します。

## ステータス

コア機能（ドライバ本体・インストーラ・コード署名）は実装済みです。他ドキュメント中の「M0」「M5」等の表記は、以下のマイルストーンを指しています。

| マイルストーン | 内容 | 状態 |
|---|---|---|
| M0 | ドライバ骨格（常時20個のコントロールデバイスを作成） | ✅ 完了 |
| M1 | キーボード/マウスへのフィルタアタッチ・素通し | ✅ 完了 |
| M2 | スロット管理・`IOCTL_GET_HARDWARE_ID` | ✅ 完了 |
| M3 | フィルタビットマスク・捕捉キュー・`IOCTL_READ`/`IOCTL_SET_EVENT` | ✅ 完了 |
| M4 | `IOCTL_WRITE`（合成入力の注入／捕捉ストロークの解放） | ✅ 完了 |
| M5 | `IOCTL_SET_PRECEDENCE`/`IOCTL_GET_PRECEDENCE`（precedenceフックチェーン） | ✅ 実装済み。配送モデルは作者本人の技術記事で確認済み（[docs/CLEAN_ROOM.md](docs/CLEAN_ROOM.md)）だが、フィルタのビット照合規則は引き続き実物との検証待ち（[docs/PROTOCOL.md](docs/PROTOCOL.md)参照） |
| M6 | インストーラ（`DiInstallDriver`/`DiUninstallDriver` + UpperFilters登録） | ✅ 完了 |
| M7 | コード署名 | 🔶 EV署名は完了。WHQL署名は申請待ち |
| M8 | 無改変のoblitum/Interceptionライブラリ・実アプリでの互換性テスト | 🚧 進行中 |
| M9 | デバイス数上限（現行20台）の撤廃（将来対応） | 📋 未着手 |

詳細なタスク管理は [Issues](../../issues) / [Projects](../../projects) を参照してください。

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

### ドライバのビルド（要WDK）

`driver/` はKMDF（Kernel-Mode Driver Framework）ベースのWindowsカーネルドライバです。ビルドには以下が必要です。

- Visual Studio 2022以降
- Windows Driver Kit (WDK)あるいはEWDK（Enterprise WDK、ISOイメージをマウントして使うスタンドアロン版）が必要です。

詳細な手順は今後追記します。署名・パッケージング手順は `packaging/sign.mak` を参照してください。ロードマップは [ステータス](#ステータス) を参照してください。

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
