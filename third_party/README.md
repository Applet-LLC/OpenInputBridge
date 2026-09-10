<!--
Copyright (c) 2026 OpenInputBridge Contributors
SPDX-License-Identifier: MIT
Licensed under the MIT License. See LICENSE file in the project root for full license text.
-->

# third_party/

## interception/

`interception/` は [Applet-LLC/Interception](https://github.com/Applet-LLC/Interception)
（oblitum/Interceptionの公開fork。内容は無改変のまま維持しており、詳細は
そのリポジトリの`packaging/PATCHES.md`を参照）の`library/`を **無改変で** 取り込んだ
git submodule です（`interception.c` / `interception.h` / ビルドスクリプト一式）。
fork元は[oblitum/Interception](https://github.com/oblitum/Interception)。

- **SPDX-License-Identifier: LGPL-3.0-only**（oblitum/Interceptionライブラリの実際のライセンスファイル
  `licenses/non-commercial-usage/LGPL 3.0.txt` に基づく表記。以前このファイルでは
  「LGPL-2.1-or-later」と誤記していたため訂正した）
- このディレクトリ配下は OpenInputBridge プロジェクト自体のMITライセンス（[/LICENSE](../LICENSE)）の
  **対象外**です
- OpenInputBridgeドライバは、このライブラリが実装するプロトコル（`docs/PROTOCOL.md` 参照）と
  互換になるよう独自に実装されています。このディレクトリのファイルは改変せずそのまま使用してください。
  改変が必要な場合はLGPLの再頒布条件（改変版ソースの公開義務等）に従ってください。
- EV署名済みの`interception.dll`（x64/ARM64）を配布物の`redist\`に同梱する仕組みについては
  [`packaging/Update-InterceptionRedist.ps1`](../packaging/Update-InterceptionRedist.ps1)を参照。
