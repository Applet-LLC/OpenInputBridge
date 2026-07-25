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

// Total number of always-present control device slots. Kept as a single named constant
// (not hardcoded at each call site) so a future backward-compatible extension beyond the
// upstream-mandated 20 slots (see docs/PROTOCOL.md "future extensibility") only requires
// changing this value and the slot-table sizing, not every call site.
#define OIB_KEYBOARD_SLOT_COUNT 10
#define OIB_MOUSE_SLOT_COUNT    10
#define OIB_DEVICE_SLOT_COUNT   (OIB_KEYBOARD_SLOT_COUNT + OIB_MOUSE_SLOT_COUNT)

// Pool tag for allocations made by this driver (shows up in pool tag tools as "OIB " for
// easy identification against other drivers' allocations when debugging).
#define OIB_POOL_TAG 'BIO '

// Per-control-device context: identifies which \Device\interceptionNN slot this WDFDEVICE is
// (0..OIB_DEVICE_SLOT_COUNT-1), so ioctl.c's EvtIoDeviceControl can look up the slot's assigned
// filter FDO (slots.c, M2) without re-deriving NN from the device name at request time.
typedef struct _OIB_CONTROL_DEVICE_CONTEXT
{
    ULONG SlotIndex;
    BOOLEAN IsKeyboard;
} OIB_CONTROL_DEVICE_CONTEXT, *POIB_CONTROL_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(OIB_CONTROL_DEVICE_CONTEXT, OibGetControlDeviceContext)

DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD OibEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP OibEvtDriverContextCleanup;

// Creates the OIB_DEVICE_SLOT_COUNT always-present control devices (\Device\interceptionNN +
// \DosDevices\interceptionNN symbolic links) at driver load time. Must succeed unconditionally,
// independent of how many physical keyboards/mice are attached, because the unmodified upstream
// interception_create_context() fails if any of the 20 CreateFileA calls fails.
NTSTATUS OibCreateControlDevices(_In_ WDFDRIVER Driver);
