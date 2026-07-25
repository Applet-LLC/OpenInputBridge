// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// See mousefilter.h. Mirrors kbdfilter.c using IOCTL_INTERNAL_MOUSE_CONNECT and
// MOUSE_INPUT_DATA instead of the keyboard equivalents. Implementation lands in M1/M3/M4.

#include "mousefilter.h"

NTSTATUS
OibMouEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    UNREFERENCED_PARAMETER(Driver);
    UNREFERENCED_PARAMETER(DeviceInit);

    // TODO(M1): same pattern as OibKbdEvtDeviceAdd, targeting IOCTL_INTERNAL_MOUSE_CONNECT
    // and registering into a free mouse slot (slots.c, M2).

    return STATUS_NOT_IMPLEMENTED;
}

VOID
OibMouFilterServiceCallback(
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

    // TODO(M1/M3): same pattern as OibKbFilterServiceCallback, MOUSE_INPUT_DATA variant.
}
