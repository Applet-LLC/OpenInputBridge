// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Management interface shared between the drivers (driver/common/driver.c) and OibCtl.exe
// (tools/oibctl/). Pure constants/types: the includer must already have pulled in <ntddk.h>
// (kernel) or <windows.h> + <winioctl.h> (user mode) for CTL_CODE. See docs/PROTOCOL.md's
// "管理デバイス" section.
//
// Each binary (oib_kbd.sys / oib_mou.sys) owns one management device of its own, opened by
// administrators/SYSTEM only (unlike the \\.\interceptionNN control devices, which are open to
// every user); the IOCTLs additionally carry READ/WRITE access bits and are dispatched by their
// full code. It stays present even while the interceptionNN control devices are hidden.

#pragma once

// User-mode paths. Deliberately free of the word used by the compatibility control devices.
#define OIB_KBD_MGMT_USERMODE_PATH L"\\\\.\\OibKbdManage"
#define OIB_MOU_MGMT_USERMODE_PATH L"\\\\.\\OibMouManage"

// Kernel names (without the "\Device\"/"\DosDevices\" prefix).
#define OIB_KBD_MGMT_NAME L"OibKbdManage"
#define OIB_MOU_MGMT_NAME L"OibMouManage"

// Removes this binary's \Device\interceptionNN control devices and their \DosDevices\ links
// while leaving the driver loaded and the keyboard/mouse filters passing input through.
// Idempotent. Fails with STATUS_DEVICE_BUSY (Win32 ERROR_BUSY) if any control device is
// currently open by a client; nothing is changed in that case.
#define IOCTL_OIB_HIDE_DEVICES    CTL_CODE(FILE_DEVICE_UNKNOWN, 0xB00, METHOD_BUFFERED, FILE_WRITE_ACCESS)

// Re-creates the control devices removed by IOCTL_OIB_HIDE_DEVICES. Idempotent.
#define IOCTL_OIB_SHOW_DEVICES    CTL_CODE(FILE_DEVICE_UNKNOWN, 0xB10, METHOD_BUFFERED, FILE_WRITE_ACCESS)

// Returns an OIB_VISIBILITY_INFO.
#define IOCTL_OIB_GET_VISIBILITY  CTL_CODE(FILE_DEVICE_UNKNOWN, 0xB20, METHOD_BUFFERED, FILE_READ_ACCESS)

typedef struct _OIB_VISIBILITY_INFO
{
    unsigned long Visible;      // 1 = control devices exist, 0 = hidden.
    unsigned long DeviceCount;  // number of control devices this binary manages.
} OIB_VISIBILITY_INFO;
