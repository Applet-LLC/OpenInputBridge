<!--
Copyright (c) 2026 OpenInputBridge Contributors
SPDX-License-Identifier: MIT
Licensed under the MIT License. See LICENSE file in the project root for full license text.
-->

# wire_protocol/

`DeviceIoControl` を直接叩く、oblitum/Interceptionライブラリを介さないプロトコルレベルのテスト。
`docs/PROTOCOL.md` に記載のIOCTL番号・`KEYBOARD_INPUT_DATA`/`MOUSE_INPUT_DATA`構造体を
手組みしたバッファで検証する。M0〜M5の各マイルストーンの受け入れ基準として使用する。
