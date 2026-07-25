// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Keyboard class-stack filter FDO: attaches as an upper filter to the Keyboard device setup
// class stack (class GUID 4d36e96b-e325-11ce-bfc1-08002be10318, registered via UpperFilters,
// see installer/), hijacks IOCTL_INTERNAL_KEYBOARD_CONNECT to substitute our own
// OibKbFilterServiceCallback in CONNECT_DATA.ClassService, and forwards to the saved original
// callback for pass-through (M1), or withholds/queues per the active filter bitmask (M3),
// and re-injects via the saved callback on IOCTL_WRITE (M4). Pattern taken from Microsoft's
// public kbfiltr.c sample (see docs/CLEAN_ROOM.md), reimplemented from scratch for this
// project without the sample's raw-PDO sideband communication (we use the always-present
// \Device\interceptionNN control devices for that instead, see driver.h/slots.h).

#pragma once

#include "driver.h"
#include <kbdmou.h>
#include <ntddkbd.h>

// Per-filter-FDO context. CONNECT_DATA holds the pointer to kbdclass's real callback/device
// object, saved off IOCTL_INTERNAL_KEYBOARD_CONNECT so OibKbFilterServiceCallback and (M4's)
// IOCTL_WRITE handling can both call back into it.
typedef struct _OIB_KBD_FILTER_CONTEXT
{
    CONNECT_DATA UpperConnectData;

    // TODO(M2): slot index (0..OIB_KEYBOARD_SLOT_COUNT-1) this FDO is assigned to in the
    // global slot table, set on successful OibKbdEvtDeviceAdd and cleared on removal.
} OIB_KBD_FILTER_CONTEXT, *POIB_KBD_FILTER_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(OIB_KBD_FILTER_CONTEXT, OibGetKbdFilterContext)

EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL OibKbdEvtInternalDeviceControl;

// AddDevice for the keyboard filter FDO. Called from OibEvtDeviceAdd (driver.c) once it has
// determined DeviceInit targets the Keyboard device setup class.
NTSTATUS OibKbdEvtDeviceAdd(_In_ WDFDRIVER Driver, _Inout_ PWDFDEVICE_INIT DeviceInit);

// The substituted ClassService callback: kbdclass's port-driver-facing callback pointer is
// replaced with this at IOCTL_INTERNAL_KEYBOARD_CONNECT time, so every keystroke reported by
// the port/HID driver below routes through here first.
VOID OibKbFilterServiceCallback(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PKEYBOARD_INPUT_DATA InputDataStart,
    _In_ PKEYBOARD_INPUT_DATA InputDataEnd,
    _Inout_ PULONG InputDataConsumed
    );
