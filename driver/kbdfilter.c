// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// See kbdfilter.h. Attaches as an upper filter (M1); per-record capture against each open
// instance's active filter bitmask (M3, via ioctl.c's OibDispatchKeyboardStroke) now decides
// pass-through vs. queuing. Re-injection via IOCTL_WRITE lands in M4.

#include "kbdfilter.h"
#include "ioctl.h"

NTSTATUS
OibKbdEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    WDF_IO_QUEUE_CONFIG ioQueueConfig;
    WDFDEVICE hDevice;
    POIB_KBD_FILTER_CONTEXT filterContext;
    ULONG slotIndex;

    UNREFERENCED_PARAMETER(Driver);

    // Framework inherits device flags/characteristics from the lower (kbdclass) device.
    WdfFdoInitSetFilter(DeviceInit);
    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_KEYBOARD);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, OIB_KBD_FILTER_CONTEXT);
    deviceAttributes.EvtCleanupCallback = OibKbdEvtFilterDeviceCleanup;

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &hDevice);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    // Parallel, not sequential: the PS/2 port driver can send a request to the top of the
    // stack while waiting on an outstanding request of its own — a sequential queue can
    // deadlock against that (see Microsoft's kbfiltr sample, docs/CLEAN_ROOM.md).
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig, WdfIoQueueDispatchParallel);
    ioQueueConfig.EvtIoInternalDeviceControl = OibKbdEvtInternalDeviceControl;

    status = WdfIoQueueCreate(hDevice, &ioQueueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    // Best-effort: if every keyboard slot is already taken (an 11th keyboard), this device
    // still attaches and filters/passes through normally, it's just unreachable via any
    // \\.\interceptionNN control device until a slot frees up. Not a device-creation failure.
    (VOID) OibSlotAssign(TRUE, hDevice, &slotIndex);

    filterContext = OibGetKbdFilterContext(hDevice);
    filterContext->SlotIndex = slotIndex;

    return STATUS_SUCCESS;
}

VOID
OibKbdEvtFilterDeviceCleanup(
    _In_ WDFOBJECT Device
    )
{
    POIB_KBD_FILTER_CONTEXT filterContext = OibGetKbdFilterContext((WDFDEVICE)Device);

    // Safe even if OibSlotAssign never succeeded for this device (OIB_SLOT_INDEX_NONE).
    OibSlotRelease(filterContext->SlotIndex);
}

VOID
OibKbdEvtInternalDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
{
    WDFDEVICE hDevice;
    POIB_KBD_FILTER_CONTEXT filterContext;
    PCONNECT_DATA connectData;
    NTSTATUS status = STATUS_SUCCESS;
    size_t length;
    WDF_REQUEST_SEND_OPTIONS options;
    BOOLEAN sent;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    hDevice = WdfIoQueueGetDevice(Queue);
    filterContext = OibGetKbdFilterContext(hDevice);

    switch (IoControlCode) {
    case IOCTL_INTERNAL_KEYBOARD_CONNECT:
        // Only one class driver (kbdclass) is expected to connect to this stack.
        if (filterContext->UpperConnectData.ClassService != NULL) {
            status = STATUS_SHARING_VIOLATION;
            break;
        }

        status = WdfRequestRetrieveInputBuffer(Request, sizeof(CONNECT_DATA), &connectData, &length);
        if (!NT_SUCCESS(status)) {
            break;
        }

        // Save kbdclass's real callback/device object, then substitute our own so every
        // keystroke reported by the port/HID driver below routes through
        // OibKbFilterServiceCallback first (see docs/PROTOCOL.md).
        filterContext->UpperConnectData = *connectData;

        connectData->ClassDeviceObject = WdfDeviceWdmGetDeviceObject(hDevice);

#pragma warning(push)
#pragma warning(disable:4152) // nonstandard extension: function/data pointer conversion
        connectData->ClassService = OibKbFilterServiceCallback;
#pragma warning(pop)

        break;

    default:
        // Pass everything else straight through, unmodified. (IOCTL_INTERNAL_KEYBOARD_DISCONNECT
        // and the i8042-specific hook IOCTLs are PS/2-port-specific initialization concerns
        // unrelated to filtering KEYBOARD_INPUT_DATA and are intentionally not handled here —
        // see Microsoft's kbfiltr sample's own note that hooking CONNECT_DATA is sufficient.)
        break;
    }

    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    // Fire-and-forget: forward down the stack, we don't need to post-process the completion.
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);

    sent = WdfRequestSend(Request, WdfDeviceGetIoTarget(hDevice), &options);
    if (!sent) {
        status = WdfRequestGetStatus(Request);
        WdfRequestComplete(Request, status);
    }
}

VOID
OibKbFilterServiceCallback(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PKEYBOARD_INPUT_DATA InputDataStart,
    _In_ PKEYBOARD_INPUT_DATA InputDataEnd,
    _Inout_ PULONG InputDataConsumed
    )
{
    WDFDEVICE hDevice;
    POIB_KBD_FILTER_CONTEXT filterContext;
    PKEYBOARD_INPUT_DATA stroke;

    hDevice = WdfWdmDeviceGetWdfDeviceHandle(DeviceObject);
    filterContext = OibGetKbdFilterContext(hDevice);

    // The port/HID driver below can batch several records into one callback invocation, but
    // capture is a per-record decision (some of a batch might be captured while the rest pass
    // through), so walk the batch one record at a time. Each pass-through record is forwarded
    // to kbdclass's real callback individually rather than re-batching consecutive
    // pass-through runs — simpler, and batches from real hardware are typically only 1-2
    // records anyway; revisit only if profiling ever shows this matters.
    for (stroke = InputDataStart; stroke < InputDataEnd; stroke++) {
        BOOLEAN captured = FALSE;

        if (filterContext->SlotIndex != OIB_SLOT_INDEX_NONE) {
            OibDispatchKeyboardStroke(filterContext->SlotIndex, stroke, &captured);
        }

        if (!captured) {
            ULONG consumedByLower = 0;

            (*(PSERVICE_CALLBACK_ROUTINE)(ULONG_PTR)filterContext->UpperConnectData.ClassService)(
                filterContext->UpperConnectData.ClassDeviceObject,
                stroke,
                stroke + 1,
                &consumedByLower
                );
        }
    }

    // Every record in the batch has now either been forwarded or queued for later release via
    // IOCTL_WRITE (M4) — from kbdclass's/i8042prt's point of view, all of it is "consumed" now
    // regardless of which path a given record took.
    *InputDataConsumed = (ULONG)(InputDataEnd - InputDataStart);
}
