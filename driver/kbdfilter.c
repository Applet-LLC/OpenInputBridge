// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// See kbdfilter.h. Implementation lands in M1 (pass-through attach) and M3/M4 (capture/reinject).

#include "kbdfilter.h"

NTSTATUS
OibKbdEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    UNREFERENCED_PARAMETER(Driver);
    UNREFERENCED_PARAMETER(DeviceInit);

    // TODO(M1):
    //   WdfFdoInitSetFilter(DeviceInit);
    //   WdfDeviceCreate(...);
    //   Register IOCTL_INTERNAL_KEYBOARD_CONNECT handling (queue or EvtIoInternalDeviceControl)
    //   to save CONNECT_DATA.ClassService/ClassDeviceObject and substitute
    //   OibKbFilterServiceCallback before forwarding the IRP down the stack.
    //   On success, assign this FDO to a free keyboard slot (slots.c, M2).

    return STATUS_NOT_IMPLEMENTED;
}

VOID
OibKbFilterServiceCallback(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PVOID InputDataStart,
    _In_ PVOID InputDataEnd,
    _Inout_ PULONG InputDataConsumed
    )
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(InputDataStart);
    UNREFERENCED_PARAMETER(InputDataEnd);
    UNREFERENCED_PARAMETER(InputDataConsumed);

    // TODO(M1): pass through unmodified to the saved original ClassService (see
    // docs/PROTOCOL.md). TODO(M3): consult the active filter bitmask for this device's
    // slot; strokes matching the filter are queued instead of forwarded, and the
    // slot's "unempty" event (IOCTL_SET_EVENT) is signaled on empty->non-empty transition.
}
