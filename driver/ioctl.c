// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// See ioctl.h and docs/PROTOCOL.md. IOCTL_GET_HARDWARE_ID is implemented (M2); the rest land
// across M3 (SET_EVENT, READ, SET/GET_FILTER), M4 (WRITE), M5 (SET/GET_PRECEDENCE).

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
    UNREFERENCED_PARAMETER(InputBufferLength);

    switch (IoControlCode) {
    case IOCTL_GET_HARDWARE_ID:
        OibCtlHandleGetHardwareId(Request, WdfIoQueueGetDevice(Queue), OutputBufferLength);
        return;

    case IOCTL_SET_PRECEDENCE:
    case IOCTL_GET_PRECEDENCE:
    case IOCTL_SET_FILTER:
    case IOCTL_GET_FILTER:
    case IOCTL_SET_EVENT:
    case IOCTL_WRITE:
    case IOCTL_READ:
        // TODO: dispatch to per-IOCTL handlers, see ioctl.h.
        WdfRequestComplete(Request, STATUS_NOT_IMPLEMENTED);
        return;

    default:
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        return;
    }
}

VOID
OibCtlHandleGetHardwareId(
    _In_ WDFREQUEST Request,
    _In_ WDFDEVICE ControlDevice,
    _In_ size_t OutputBufferLength
    )
{
    POIB_CONTROL_DEVICE_CONTEXT ctlContext;
    WDFDEVICE filterDevice;
    NTSTATUS status;
    PVOID outputBuffer = NULL;
    size_t outputBufferSize = 0;
    size_t bytesReturned = 0;

    ctlContext = OibGetControlDeviceContext(ControlDevice);

    if (OutputBufferLength != 0) {
        status = WdfRequestRetrieveOutputBuffer(Request, 0, &outputBuffer, &outputBufferSize);
        if (!NT_SUCCESS(status)) {
            WdfRequestComplete(Request, status);
            return;
        }
    }

    status = OibSlotAcquireFilterDevice(ctlContext->SlotIndex, &filterDevice);
    if (NT_SUCCESS(status)) {
        PDEVICE_OBJECT pdo = WdfDeviceWdmGetPhysicalDevice(filterDevice);
        ULONG requiredLength = 0;

        // Ask directly into the caller's buffer first: if it's already big enough (the
        // common case — hardware IDs are short), this is a single call with no extra
        // allocation.
        status = IoGetDeviceProperty(
            pdo,
            DevicePropertyHardwareID,
            (ULONG)outputBufferSize,
            outputBuffer,
            &requiredLength
            );

        if (status == STATUS_BUFFER_TOO_SMALL && outputBufferSize > 0) {
            // Caller's buffer is too small for the full property. Rather than failing
            // outright, fetch the full value into a temporary buffer and hand back as much
            // as fits — see docs/PROTOCOL.md ("呼び出し元バッファサイズに収まる範囲で返す").
            PVOID tempBuffer = ExAllocatePool2(POOL_FLAG_PAGED, requiredLength, OIB_POOL_TAG);

            if (tempBuffer == NULL) {
                status = STATUS_INSUFFICIENT_RESOURCES;
            } else {
                ULONG actualLength = 0;

                status = IoGetDeviceProperty(pdo, DevicePropertyHardwareID, requiredLength, tempBuffer, &actualLength);
                if (NT_SUCCESS(status)) {
                    size_t copyLength = min((size_t)actualLength, outputBufferSize);
                    RtlCopyMemory(outputBuffer, tempBuffer, copyLength);
                    bytesReturned = copyLength;
                }

                ExFreePoolWithTag(tempBuffer, OIB_POOL_TAG);
            }
        } else if (NT_SUCCESS(status)) {
            bytesReturned = requiredLength;
        }

        OibSlotReleaseFilterDeviceReference(filterDevice);
    } else {
        // No physical device currently assigned to this slot: succeed with zero bytes rather
        // than fail — see docs/PROTOCOL.md on why empty slots must not fail control-device
        // IOCTLs (the unmodified upstream library must be able to treat every one of the 20
        // \\.\interceptionNN devices as usable regardless of what's physically plugged in).
        status = STATUS_SUCCESS;
        bytesReturned = 0;
    }

    WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}
