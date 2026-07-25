// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// See kbdfilter.h. M1 scope: attach as an upper filter and pass every keyboard event through
// unmodified. Capture/withholding per the active filter bitmask lands in M3; re-injection via
// IOCTL_WRITE lands in M4.

#include "kbdfilter.h"

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

    UNREFERENCED_PARAMETER(Driver);

    // Framework inherits device flags/characteristics from the lower (kbdclass) device.
    WdfFdoInitSetFilter(DeviceInit);
    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_KEYBOARD);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, OIB_KBD_FILTER_CONTEXT);

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

    // TODO(M2): assign this FDO to a free keyboard slot (0..OIB_KEYBOARD_SLOT_COUNT-1) in the
    // global slot table (slots.c), so the corresponding \Device\interceptionNN control device
    // can route IOCTLs to it.

    return STATUS_SUCCESS;
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

    hDevice = WdfWdmDeviceGetWdfDeviceHandle(DeviceObject);
    filterContext = OibGetKbdFilterContext(hDevice);

    // TODO(M3): consult this slot's active filter bitmask here; strokes that should be
    // captured are queued instead of forwarded, and this slot's "unempty" event
    // (IOCTL_SET_EVENT) is signaled on empty->non-empty transition. For now (M1):
    // unconditional pass-through to kbdclass's real callback.
    (*(PSERVICE_CALLBACK_ROUTINE)(ULONG_PTR)filterContext->UpperConnectData.ClassService)(
        filterContext->UpperConnectData.ClassDeviceObject,
        InputDataStart,
        InputDataEnd,
        InputDataConsumed
        );
}
