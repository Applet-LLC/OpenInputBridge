; Copyright (c) 2026 OpenInputBridge Contributors
; SPDX-License-Identifier: MIT
; Licensed under the MIT License. See LICENSE file in the project root for full license text.
;
; M8: キーボード2台の捕捉ログ（非ブロッキング、素通し）。tests/upstream_lib/identify2.exeのAHK版。
; 詳細・合格基準はこのディレクトリのREADME.md参照。

#Requires AutoHotkey v2.0
#Include Lib\AutoHotInterception.ahk
Persistent

AHI := AutoHotInterception()

myGui := Gui("+Resize", "OIB M8 - identify_kbd")
lv := myGui.Add("ListView", "w520 h400", ["Time", "Slot", "Code", "State"])
myGui.Add("Text", , "Escで終了。キーボードを交互に操作し、正しいスロット番号で記録されるか確認してください。")
myGui.Show()

; キーボードのスロットは1〜10固定（docs/PROTOCOL.md）。台数が2台でも、接続されていない
; スロットを購読してもイベントは来ないだけなので、10個すべてを購読して構わない。
Loop 10 {
    id := A_Index
    AHI.SubscribeKeyboard(id, false, LogKey.Bind(id))
}

LogKey(id, code, state) {
    lv.Add(, FormatTime(, "HH:mm:ss.fff"), id, Format("0x{:02X}", code), state)
    lv.Modify(lv.GetCount(), "Vis")
}

Esc::ExitApp()
