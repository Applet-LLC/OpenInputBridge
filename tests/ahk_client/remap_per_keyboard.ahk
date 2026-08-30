; Copyright (c) 2026 OpenInputBridge Contributors
; SPDX-License-Identifier: MIT
; Licensed under the MIT License. See LICENSE file in the project root for full license text.
;
; M8: デバイス単位のキーリマップ。キーボードAだけ「1」→「a」、キーボードBだけ「2」→「b」に
; 置き換える。書き換え内容自体よりも、片方への変更がもう片方に漏れていないことの確認が目的。
; 詳細・合格基準はREADME.md参照。

#Requires AutoHotkey v2.0
#Include Lib\AutoHotInterception.ahk
Persistent

; 実行前にidentify_kbd.ahkで実際のスロット番号を確認し、以下を書き換えること。
KB_A_ID := 1
KB_B_ID := 4

AHI := AutoHotInterception()

AHI.SubscribeKey(KB_A_ID, GetKeySC("1"), true, RemapA)
AHI.SubscribeKey(KB_B_ID, GetKeySC("2"), true, RemapB)

TrayTip("remap_per_keyboard", Format("KB_A_ID={1}: '1'->'a' / KB_B_ID={2}: '2'->'b'。Escで終了。", KB_A_ID, KB_B_ID))

RemapA(state) {
    if (state = 1)
        Send("a")
}

RemapB(state) {
    if (state = 1)
        Send("b")
}

Esc::ExitApp()
