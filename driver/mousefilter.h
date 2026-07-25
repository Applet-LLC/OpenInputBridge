// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Mouse class-stack filter FDO: mouse-side equivalent of kbdfilter.h. Attaches as an upper
// filter to the Mouse device setup class stack (class GUID
// 4d36e96f-e325-11ce-bfc1-08002be10318, registered via UpperFilters, see installer/),
// hijacks IOCTL_INTERNAL_MOUSE_CONNECT the same way kbdfilter hijacks
// IOCTL_INTERNAL_KEYBOARD_CONNECT. See docs/PROTOCOL.md and docs/CLEAN_ROOM.md.

#pragma once

#include "driver.h"
#include <kbdmou.h>
#include <ntddmou.h>

// Per-filter-FDO context. Mirrors OIB_KBD_FILTER_CONTEXT in kbdfilter.h.
typedef struct _OIB_MOU_FILTER_CONTEXT
{
    CONNECT_DATA UpperConnectData;

    // TODO(M2): slot index (OIB_KEYBOARD_SLOT_COUNT..OIB_DEVICE_SLOT_COUNT-1) this FDO is
    // assigned to in the global slot table, set on successful OibMouEvtDeviceAdd and cleared
    // on removal.
} OIB_MOU_FILTER_CONTEXT, *POIB_MOU_FILTER_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(OIB_MOU_FILTER_CONTEXT, OibGetMouFilterContext)

EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL OibMouEvtInternalDeviceControl;

// AddDevice for the mouse filter FDO. Called from OibEvtDeviceAdd (driver.c) once it has
// determined DeviceInit targets the Mouse device setup class.
NTSTATUS OibMouEvtDeviceAdd(_In_ WDFDRIVER Driver, _Inout_ PWDFDEVICE_INIT DeviceInit);

// The substituted ClassService callback, MOUSE_INPUT_DATA variant of
// OibKbFilterServiceCallback.
VOID OibMouFilterServiceCallback(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PMOUSE_INPUT_DATA InputDataStart,
    _In_ PMOUSE_INPUT_DATA InputDataEnd,
    _Inout_ PULONG InputDataConsumed
    );
