// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// DriverEntry and the 20 always-present control devices (\Device\interceptionNN, NN=00..19).
// See docs/PROTOCOL.md for the wire protocol this driver must expose, and the "M0" milestone
// in the project plan for the current implementation target.

#pragma once

#include <ntddk.h>
#include <wdf.h>

// Total number of always-present control device slots — fixed by the upstream wire protocol
// (docs/PROTOCOL.md), independent of how the two binaries below split it between themselves.
#define OIB_TOTAL_DEVICE_SLOT_COUNT 20

// Default keyboard/mouse split when the registry value below is absent, unreadable, or out of
// range — matches the split the combined single-binary driver used before the split
// (docs/DECISIONS.md's 2026-07-30 entry).
#define OIB_DEFAULT_KEYBOARD_SLOT_COUNT 10

// REG_DWORD under this service's own \Parameters key (both the keyboard and mouse services
// carry the same value — see docs/DECISIONS.md's 2026-08-02 entry). Holds the keyboard side's
// share (0..OIB_TOTAL_DEVICE_SLOT_COUNT); the mouse side's own share is always
// OIB_TOTAL_DEVICE_SLOT_COUNT minus this same number, so the two binaries can never disagree
// about where the boundary is as long as the installer keeps both copies in sync (installer/).
#define OIB_KEYBOARD_SLOT_COUNT_VALUE_NAME L"KeyboardSlotCount"

// REG_SZ under this service's own \Parameters key, overriding the "interception" in
// \Device\interceptionNN / \DosDevices\interceptionNN with a caller-chosen base name (still
// followed by the same 2-digit slot number). Purely opt-in: absent, empty, too long, or
// containing anything outside [A-Za-z0-9_-] all fall back to OIB_DEFAULT_DEVICE_NAME_BASE, i.e.
// today's fixed names. Exists so this driver's control devices can be renamed to coexist
// side by side with the real Interception driver for evaluation (same device slot must still
// not be opened from both at once — see docs/COEXISTENCE.md for why two kbfiltr-type filters
// racing to capture the same physical keystroke is unsupported regardless of naming).
#define OIB_DEVICE_NAME_BASE_VALUE_NAME L"DeviceNameBase"
#define OIB_DEFAULT_DEVICE_NAME_BASE L"interception"

// Longest base name OibReadConfiguredDeviceNameBase will accept from the registry (arbitrary
// but generous — real device/symlink object names have no practical length limit anywhere near
// this).
#define OIB_DEVICE_NAME_BASE_MAX_CHARS 32

// Longest \Device\<base>NN / \DosDevices\<base>NN name OibCreateControlDevices can produce,
// including the NUL: the longer "\DosDevices\" prefix + the configured base name + the 2-digit
// slot number + NUL.
#define OIB_DEVICE_OR_SYMLINK_NAME_MAX_CHARS (12 + OIB_DEVICE_NAME_BASE_MAX_CHARS + 2 + 1)

// This header is shared, unmodified, between the oib_kbd.vcxproj and oib_mou.vcxproj projects
// (see docs/DECISIONS.md's 2026-07-30 entry on why the single-binary/Class=System driver was
// split in two): each project defines exactly one of OIB_BUILD_KEYBOARD/OIB_BUILD_MOUSE
// (PreprocessorDefinitions). OIB_IS_KEYBOARD_BUILD is the only compile-time fact either binary
// needs about its own kind; how many of the 20 slots it actually owns, and at what device
// number its own range starts, are both resolved at DriverEntry time from the registry value
// above (see driver.c's OibReadConfiguredKeyboardSlotCount) and passed down to
// OibSlotTableInitialize/OibCreateControlDevices as plain parameters — ioctl.c/slots.c have no
// compile-time knowledge of the split and need no changes to serve either binary.
#if defined(OIB_BUILD_KEYBOARD)
#define OIB_IS_KEYBOARD_BUILD TRUE
#elif defined(OIB_BUILD_MOUSE)
#define OIB_IS_KEYBOARD_BUILD FALSE
#else
#error "Define exactly one of OIB_BUILD_KEYBOARD or OIB_BUILD_MOUSE (project PreprocessorDefinitions)."
#endif

// Pool tag for allocations made by this driver (shows up in pool tag tools as "OIB " for
// easy identification against other drivers' allocations when debugging).
#define OIB_POOL_TAG 'BIO '

// Per-control-device context: identifies which \Device\interceptionNN slot this WDFDEVICE is
// (this binary's own SlotIndex space — see slots.h), so ioctl.c's EvtIoDeviceControl can look
// up the slot's assigned filter FDO (slots.c, M2) without re-deriving NN from the device name
// at request time.
typedef struct _OIB_CONTROL_DEVICE_CONTEXT
{
    ULONG SlotIndex;
    BOOLEAN IsKeyboard;
} OIB_CONTROL_DEVICE_CONTEXT, *POIB_CONTROL_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(OIB_CONTROL_DEVICE_CONTEXT, OibGetControlDeviceContext)

DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD OibEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP OibEvtDriverContextCleanup;

// Creates this binary's ActiveSlotCount always-present control devices (\Device\<DeviceNameBase>NN +
// \DosDevices\<DeviceNameBase>NN symbolic links, NN = index + DeviceNumberBase) at driver load
// time. DeviceNameBase is normally OIB_DEFAULT_DEVICE_NAME_BASE — see
// OIB_DEVICE_NAME_BASE_VALUE_NAME above for how a caller overrides it. Must succeed
// unconditionally, independent of how many physical keyboards/mice are attached, because the
// unmodified upstream interception_create_context() fails if any of the 20 CreateFileA calls
// (across both binaries) fails.
NTSTATUS OibCreateControlDevices(
    _In_ WDFDRIVER Driver,
    _In_ ULONG ActiveSlotCount,
    _In_ ULONG DeviceNumberBase,
    _In_ PCWSTR DeviceNameBase
    );
