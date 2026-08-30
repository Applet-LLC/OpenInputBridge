; Copyright (c) 2026 OpenInputBridge Contributors
; SPDX-License-Identifier: MIT
; Licensed under the MIT License. See LICENSE file in the project root for full license text.
;
; M8: GUIテストベンチ。identify_*.ahk / remap_per_*.ahk / combined_soak.ahkの内容を1つのGUIに
; まとめ、4台（キーボード2・マウス2）を繋ぎ直さずに、リマップ規則のON/OFFをその場で切り替えな
; がら確認できるようにしたもの。セッション内容をログファイルに書き出す。
; 詳細・合格基準はREADME.md参照。
;
; 各Unsubscribe*関数の正確なシグネチャはLib\AutoHotInterception.ahk（実機導入時に配置される
; サードパーティ実装）に依存するため未検証。実行前に該当関数が存在することを確認し、必要なら
; 修正すること。

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

logPath := A_ScriptDir "\session_" FormatTime(, "yyyyMMdd_HHmmss") ".log"
logFile := FileOpen(logPath, "w", "UTF-8")

eventCount := Map(KB_A_ID, 0, KB_B_ID, 0, MOUSE_A_ID, 0, MOUSE_B_ID, 0)
roleText := Map(KB_A_ID, "Keyboard A", KB_B_ID, "Keyboard B",
    MOUSE_A_ID, "Mouse A", MOUSE_B_ID, "Mouse B")
remapEnabled := Map(KB_A_ID, false, KB_B_ID, false, MOUSE_A_ID, false)

myGui := Gui("+Resize", "OIB M8 - testbench")
lv := myGui.Add("ListView", "w520 h140", ["Slot", "Device", "Last event", "Count"])
rowOf := Map()
for id, role in roleText
    rowOf[id] := lv.Add(, id, role, "-", 0)

cbA := myGui.Add("Checkbox", "y+10", "Keyboard A: remap 1->a")
cbA.OnEvent("Click", (*) => ToggleKeyRemap(KB_A_ID, GetKeySC("1"), RemapA, cbA.Value))
cbB := myGui.Add("Checkbox", , "Keyboard B: remap 2->b")
cbB.OnEvent("Click", (*) => ToggleKeyRemap(KB_B_ID, GetKeySC("2"), RemapB, cbB.Value))
cbM := myGui.Add("Checkbox", , "Mouse A: left -> right")
cbM.OnEvent("Click", (*) => ToggleMouseRemap(cbM.Value))

myGui.Add("Text", , "Mouse Bは常に素通し（比較対象）。ログ: " logPath)
myGui.Show()

; まずは全デバイスを非ブロッキングで購読し、実イベントが発生することを可視化する
; （チェックボックスON時に、対象のキー/ボタンだけブロッキング購読へ切り替える）。
Loop 10 {
    id := A_Index
    AHI.SubscribeKeyboard(id, false, LogKeyboard.Bind(id))
}
Loop 10 {
    id := 10 + A_Index
    AHI.SubscribeMouseButtons(id, false, LogMouseButton.Bind(id))
}

ToggleKeyRemap(id, code, callback, enabled) {
    global remapEnabled, AHI
    remapEnabled[id] := enabled
    if (enabled) {
        AHI.UnsubscribeKeyboard(id)
        AHI.SubscribeKey(id, code, true, callback)
    } else {
        AHI.UnsubscribeKey(id, code)
        AHI.SubscribeKeyboard(id, false, LogKeyboard.Bind(id))
    }
    WriteLog(Format("[config] slot={} remap={}", id, enabled ? "ON" : "OFF"))
}

ToggleMouseRemap(enabled) {
    global remapEnabled, AHI
    remapEnabled[MOUSE_A_ID] := enabled
    if (enabled) {
        AHI.UnsubscribeMouseButtons(MOUSE_A_ID)
        AHI.SubscribeMouseButton(MOUSE_A_ID, LEFT_BUTTON, true, SwapToRight)
    } else {
        AHI.UnsubscribeMouseButton(MOUSE_A_ID, LEFT_BUTTON)
        AHI.SubscribeMouseButtons(MOUSE_A_ID, false, LogMouseButton.Bind(MOUSE_A_ID))
    }
    WriteLog(Format("[config] slot={} remap={}", MOUSE_A_ID, enabled ? "ON" : "OFF"))
}

LogKeyboard(id, code, state) {
    Touch(id, Format("key code=0x{:02X} state={}", code, state))
}

LogMouseButton(id, code, state) {
    Touch(id, Format("button code={} state={}", code, state))
}

RemapA(state) {
    Touch(KB_A_ID, Format("remap 1->a state={}", state))
    if (state = 1)
        Send("a")
}

RemapB(state) {
    Touch(KB_B_ID, Format("remap 2->b state={}", state))
    if (state = 1)
        Send("b")
}

SwapToRight(state) {
    Touch(MOUSE_A_ID, Format("remap L->R state={}", state))
    AHI.SendMouseButtonEvent(MOUSE_A_ID, RIGHT_BUTTON, state)
}

Touch(id, detail) {
    global eventCount, rowOf, roleText
    eventCount[id] += 1
    lv.Modify(rowOf[id], , id, roleText[id], detail, eventCount[id])
    WriteLog(Format("slot={} {}", id, detail))
}

WriteLog(line) {
    global logFile
    logFile.WriteLine(FormatTime(, "HH:mm:ss.fff") " " line)
    ; AHK v2のFileオブジェクトは書き込みを8KBバッファに溜め込み、Close()するまでディスクに
    ; 反映しない。スクリプト実行中でもログをリアルタイムに追えるよう、Read(0)で都度フラッシュする。
    logFile.Read(0)
}

Esc::ExitApp()

OnExit((*) => logFile.Close())
