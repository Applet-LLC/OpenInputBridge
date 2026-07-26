<!--
Copyright (c) 2026 OpenInputBridge Contributors
SPDX-License-Identifier: MIT
Licensed under the MIT License. See LICENSE file in the project root for full license text.
-->

# クリーンルーム実装 証跡ログ

このドキュメントは、OpenInputBridgeの実装にあたって「何を参照したか／何を参照していないか」を
時系列で記録するものです。プロプライエタリな Interception ドライバ本体（.sys）とのIP境界を明確にし、
独立したクリーンルーム実装であることの根拠として維持します。

## 参照してよいもの（許可された情報源）

1. `oblitum/Interception` リポジトリの `library/` ディレクトリ（LGPL、公開ソースコード）
   - これはユーザーモード側のC APIライブラリであり、カーネルドライバとの通信プロトコル（IOCTL番号・
     デバイス名・データ構造）を実装レベルで明示している
2. Microsoft公式のWDK/KMDFドキュメント（Microsoft Learn）および `microsoft/Windows-driver-samples`
   リポジトリ内の公開サンプルドライバ（`input/kbfiltr`, `input/moufiltr` 等）
3. 実物の（商用ライセンス版）Interceptionドライバに対する、**ブラックボックスなI/Oレベルの動作観察**
   （`DeviceIoControl` の入出力、レジストリ変更内容、インストール後の動作結果など、外部から観測可能な
   挙動のみ。逆アセンブル・逆コンパイル・バイナリの静的/動的解析は含まない）

## 参照してはいけないもの（禁止事項）

- Interception カーネルドライバ本体（.sys）のソースコード（非公開のため、そもそも入手不可）
- Interception ドライババイナリの逆アセンブル・逆コンパイル結果
- 商用ライセンス契約で開示される可能性のある非公開の設計資料・内部ドキュメント

## 証跡

### 2026-07-25: 初期調査

以下の公開URLを参照し、プロトコル仕様（`docs/PROTOCOL.md`）およびアーキテクチャ設計の根拠とした。

- `https://github.com/oblitum/Interception` — ライセンス体系（LGPL + 商用ライセンス2種）、リポジトリ構成の確認
- `https://raw.githubusercontent.com/oblitum/Interception/master/library/interception.h` — 公開API・データ構造の確認
- `https://raw.githubusercontent.com/oblitum/Interception/master/library/interception.c` — IOCTLコード・
  デバイス名・通信プロトコルの詳細確認（本プロジェクトの実装の一次情報源）
- `https://deepwiki.com/oblitum/Interception` — 補助的な概要確認（一次情報はinterception.c/.hを優先）
- `https://raw.githubusercontent.com/microsoft/Windows-driver-samples/main/input/kbfiltr/sys/kbfiltr.c` —
  `IOCTL_INTERNAL_KEYBOARD_CONNECT` ハイジャックパターンの確認（Microsoft公式サンプル）
- `https://raw.githubusercontent.com/microsoft/Windows-driver-samples/main/input/moufiltr/moufiltr.c` —
  マウス側の同等パターン確認、KMDFベースであることの確認
- Microsoft Learn: kbdmou DDIリファレンス（`IOCTL_INTERNAL_KEYBOARD_CONNECT`）、デバイスセットアップ
  クラスGUID一覧（キーボード/マウスクラスGUID）、ドライバ署名(attestation signing)ガイド

**この時点で、Interceptionのプロプライエタリなドライババイナリ（.sys）へのアクセス・解析は一切行っていない。**

### 実機ブラックボックステストについて（予定・M5フェーズ）

プロジェクト保有者は実物Interceptionドライバ（商用ライセンス版バイナリ）を別途所持しており、
`IOCTL_SET_PRECEDENCE` / `IOCTL_GET_PRECEDENCE` の複数プロセス間での挙動検証のため、
`tests/precedence_blackbox/` にてブラックボックスI/O観察テストを実施予定。この際も対象は
外部から観測可能なI/O動作（DeviceIoControl呼び出しの結果等）のみとし、バイナリの逆アセンブル等は
行わない。実施時はこのセクションに実施日・観察内容・結論を追記する。

---

今後、新たな情報源を参照した場合は都度このファイルに追記すること。
