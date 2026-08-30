; Copyright (c) 2026 OpenInputBridge Contributors
; SPDX-License-Identifier: MIT
; Licensed under the MIT License. See LICENSE file in the project root for full license text.
;
; M8: デバイス単位のマウスボタン置換・合成注入。マウスAの左クリックだけをブロックし、
; SendMouseButtonEventで右クリックとして合成注入する。マウスBは素通しのまま。
; IOCTL_WRITE経由の書き戻しが意図したデバイスの経路だけを通ることの確認が目的。
; 詳細・合格基準はREADME.md参照。

#Requires AutoHotkey v2.0
#Include Lib\AutoHotInterception.ahk
Persistent

; 実行前にidentify_mouse.ahkで実際のスロット番号を確認し、以下を書き換えること。
MOUSE_A_ID := 11
MOUSE_B_ID := 12

; ボタン番号(0=左/1=右/2=中央 の想定)はAutoHotInterception側の実装に依存する。
; Lib\AutoHotInterception.ahkのSubscribeMouseButton/SendMouseButtonEvent実装を実機で
; 確認したうえで、想定と異なれば下記の値を修正すること。
LEFT_BUTTON := 0
RIGHT_BUTTON := 1

AHI := AutoHotInterception()

AHI.SubscribeMouseButton(MOUSE_A_ID, LEFT_BUTTON, true, SwapToRight)

TrayTip("remap_per_mouse", Format("MOUSE_A_ID={1}: 左クリック→右クリックとして注入 / MOUSE_B_ID={2}: 素通し。Escで終了。", MOUSE_A_ID, MOUSE_B_ID))

SwapToRight(state) {
    AHI.SendMouseButtonEvent(MOUSE_A_ID, RIGHT_BUTTON, state)
}

Esc::ExitApp()
