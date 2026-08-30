; Copyright (c) 2026 OpenInputBridge Contributors
; SPDX-License-Identifier: MIT
; Licensed under the MIT License. See LICENSE file in the project root for full license text.
;
; M8: マウス2台の捕捉ログ（非ブロッキング、素通し）。詳細・合格基準はREADME.md参照。

#Requires AutoHotkey v2.0
#Include Lib\AutoHotInterception.ahk
Persistent

AHI := AutoHotInterception()

myGui := Gui("+Resize", "OIB M8 - identify_mouse")
lv := myGui.Add("ListView", "w560 h400", ["Time", "Slot", "Type", "Detail"])
myGui.Add("Text", , "Escで終了。マウスを交互に操作し、正しいスロット番号で記録されるか確認してください。")
myGui.Show()

; マウスのスロットは11〜20固定（docs/PROTOCOL.md）。
Loop 10 {
    id := 10 + A_Index
    AHI.SubscribeMouseButtons(id, false, LogButton.Bind(id))
    AHI.SubscribeMouseMoveRelative(id, false, LogMove.Bind(id))
}

LogButton(id, code, state) {
    lv.Add(, FormatTime(, "HH:mm:ss.fff"), id, "Button", Format("code={} state={}", code, state))
    lv.Modify(lv.GetCount(), "Vis")
}

LogMove(id, x, y) {
    lv.Add(, FormatTime(, "HH:mm:ss.fff"), id, "Move", Format("dx={} dy={}", x, y))
    lv.Modify(lv.GetCount(), "Vis")
}

Esc::ExitApp()
