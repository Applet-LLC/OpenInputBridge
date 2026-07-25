<!--
Copyright (c) 2026 OpenInputBridge Contributors
SPDX-License-Identifier: MIT
Licensed under the MIT License. See LICENSE file in the project root for full license text.
-->

# precedence_blackbox/

実物の（商用ライセンス版）Interceptionドライバに対する、ブラックボックスI/O観察テスト。
複数プロセスが同一デバイスを異なる`IOCTL_SET_PRECEDENCE`値で同時にフックした際の配送順序を
観察し、M5実装の仕様として採用する。逆アセンブル等は行わず、外部から観測可能な
`DeviceIoControl`の入出力のみを対象とする（`docs/CLEAN_ROOM.md`参照）。観察結果・結論は
このディレクトリと`docs/CLEAN_ROOM.md`の両方に記録すること。
