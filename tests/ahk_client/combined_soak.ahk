; Copyright (c) 2026 OpenInputBridge Contributors
; SPDX-License-Identifier: MIT
; Licensed under the MIT License. See LICENSE file in the project root for full license text.
;
; M8: キーボード2台・マウス2台すべてに同時にリマップ規則を適用した状態での長時間動作テスト。
; tests/upstream_lib/README.mdに記録済みの「無関係な複数ツール同時アタッチでも干渉しない」
; 実機確認結果の、AHK経由での再現に相当する。詳細・合格基準はREADME.md参照。

#Requires AutoHotkey v2.0
#Include Lib\AutoHotInterception.ahk
Persistent

; 実行前にidentify_kbd.ahk / identify_mouse.ahkで実際のスロット番号を確認し、以下を書き換えること。
KB_A_ID := 1
KB_B_ID := 4
MOUSE_A_ID := 11
MOUSE_B_ID := 12
LEFT_BUTTON := 0
RIGHT_BUTTON := 1

AHI := AutoHotInterception()

eventCount := Map(KB_A_ID, 0, KB_B_ID, 0, MOUSE_A_ID, 0, MOUSE_B_ID, 0)
roleText := Map(KB_A_ID, "Keyboard A (1->a)", KB_B_ID, "Keyboard B (2->b)",
    MOUSE_A_ID, "Mouse A (L->R)", MOUSE_B_ID, "Mouse B (pass-through)")
rowOf := Map()
startTick := A_TickCount

myGui := Gui("+Resize", "OIB M8 - combined_soak")
lv := myGui.Add("ListView", "w420 h140", ["Slot", "Role", "Event count"])
for id, role in roleText
    rowOf[id] := lv.Add(, id, role, 0)
elapsedText := myGui.Add("Text", , "経過時間: 0s")
myGui.Show()

AHI.SubscribeKey(KB_A_ID, GetKeySC("1"), true, RemapA)
AHI.SubscribeKey(KB_B_ID, GetKeySC("2"), true, RemapB)
AHI.SubscribeMouseButton(MOUSE_A_ID, LEFT_BUTTON, true, SwapToRight)
AHI.SubscribeMouseButtons(MOUSE_B_ID, false, CountMouseB)

SetTimer(UpdateElapsed, 1000)

RemapA(state) {
    Bump(KB_A_ID)
    if (state = 1)
        Send("a")
}

RemapB(state) {
    Bump(KB_B_ID)
    if (state = 1)
        Send("b")
}

SwapToRight(state) {
    Bump(MOUSE_A_ID)
    AHI.SendMouseButtonEvent(MOUSE_A_ID, RIGHT_BUTTON, state)
}

CountMouseB(code, state) {
    Bump(MOUSE_B_ID)
}

Bump(id) {
    eventCount[id] += 1
    lv.Modify(rowOf[id], , id, roleText[id], eventCount[id])
}

UpdateElapsed() {
    elapsedText.Text := Format("経過時間: {}s", Round((A_TickCount - startTick) / 1000))
}

Esc::ExitApp()
