// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// See ioctl.h and docs/PROTOCOL.md. Implementation lands across M2 (GET_HARDWARE_ID),
// M3 (SET_EVENT, READ, SET/GET_FILTER), M4 (WRITE), M5 (SET/GET_PRECEDENCE).

#include "ioctl.h"

VOID
OibCtlEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
{
    UNREFERENCED_PARAMETER(Queue);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    switch (IoControlCode) {
    case IOCTL_SET_PRECEDENCE:
    case IOCTL_GET_PRECEDENCE:
    case IOCTL_SET_FILTER:
    case IOCTL_GET_FILTER:
    case IOCTL_SET_EVENT:
    case IOCTL_WRITE:
    case IOCTL_READ:
    case IOCTL_GET_HARDWARE_ID:
        // TODO: dispatch to per-IOCTL handlers, see ioctl.h.
        WdfRequestComplete(Request, STATUS_NOT_IMPLEMENTED);
        break;

    default:
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        break;
    }
}
