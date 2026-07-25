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

EVT_WDF_DEVICE_FILE_CREATE OibMouEvtDeviceFileCreate;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL OibMouEvtInternalDeviceControl;

// TODO(M1): AddDevice for the mouse filter FDO, mirrors OibKbdEvtDeviceAdd.
NTSTATUS OibMouEvtDeviceAdd(_In_ WDFDRIVER Driver, _Inout_ PWDFDEVICE_INIT DeviceInit);

// TODO(M1): the substituted ClassService callback, MOUSE_INPUT_DATA variant.
VOID OibMouFilterServiceCallback(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PVOID InputDataStart,
    _In_ PVOID InputDataEnd,
    _Inout_ PULONG InputDataConsumed
    );
