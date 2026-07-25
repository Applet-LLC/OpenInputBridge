// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Keyboard class-stack filter FDO: attaches as an upper filter to the Keyboard device setup
// class stack (class GUID 4d36e96b-e325-11ce-bfc1-08002be10318, registered via UpperFilters,
// see installer/), hijacks IOCTL_INTERNAL_KEYBOARD_CONNECT to substitute our own
// KbFilter_ServiceCallback in CONNECT_DATA.ClassService, and forwards to the saved original
// callback for pass-through (M1), or withholds/queues per the active filter bitmask (M3),
// and re-injects via the saved callback on IOCTL_WRITE (M4). Pattern taken from Microsoft's
// public kbfiltr.c sample (see docs/CLEAN_ROOM.md) reimplemented from scratch for this project.

#pragma once

#include "driver.h"

EVT_WDF_DEVICE_FILE_CREATE OibKbdEvtDeviceFileCreate;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL OibKbdEvtInternalDeviceControl;

// TODO(M1): AddDevice for the keyboard filter FDO (WdfFdoInitSetFilter + WdfDeviceCreate),
// on success register into the slot table (slots.c, M2).
NTSTATUS OibKbdEvtDeviceAdd(_In_ WDFDRIVER Driver, _Inout_ PWDFDEVICE_INIT DeviceInit);

// TODO(M1): the substituted ClassService callback. Signature must match
// PSERVICE_CALLBACK_ROUTINE / KEYBOARD_INPUT_DATA as used by kbdclass.
VOID OibKbFilterServiceCallback(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PVOID InputDataStart,
    _In_ PVOID InputDataEnd,
    _Inout_ PULONG InputDataConsumed
    );
