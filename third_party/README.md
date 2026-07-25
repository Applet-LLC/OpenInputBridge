<!--
Copyright (c) 2026 OpenInputBridge Contributors
SPDX-License-Identifier: MIT
Licensed under the MIT License. See LICENSE file in the project root for full license text.
-->

# third_party/

## interception/

`interception/` は [oblitum/Interception](https://github.com/oblitum/Interception) の
`library/` を **無改変で** vendor した git submodule です（`interception.c` / `interception.h` /
ビルドスクリプト一式）。

- **SPDX-License-Identifier: LGPL-2.1-or-later**（upstreamのライセンス表記に準拠。詳細は
  upstreamリポジトリのライセンス表記・`licenses/` を参照）
- このディレクトリ配下は OpenInputBridge プロジェクト自体のMITライセンス（[/LICENSE](../LICENSE)）の
  **対象外**です
- OpenInputBridgeドライバは、このライブラリが実装するワイヤプロトコル（`docs/PROTOCOL.md` 参照）と
  互換になるよう独自に実装されています。このディレクトリのファイルは改変せずそのまま使用してください。
  改変が必要な場合はLGPLの再頒布条件（改変版ソースの公開義務等）に従ってください。
