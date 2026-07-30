// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// See mousefilter.h. Mirrors kbdfilter.c using IOCTL_INTERNAL_MOUSE_CONNECT and
// MOUSE_INPUT_DATA instead of the keyboard equivalents, including M3's per-record capture
// dispatch (ioctl.c's OibDispatchMouseStroke).

#include "mousefilter.h"
#include "ioctl.h"

// See kbdfilter.c's OibKbdEvtDeviceAdd for why this function (and OibMouEvtFilterDeviceCleanup
// below) must not be marked pageable via #pragma alloc_text(PAGE, ...) without first splitting
// the OibSlotAssign/OibSlotRelease spinlock section into a separate non-paged function.
NTSTATUS
OibMouEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    WDF_IO_QUEUE_CONFIG ioQueueConfig;
    WDFDEVICE hDevice;
    POIB_MOU_FILTER_CONTEXT filterContext;
    ULONG slotIndex;

    UNREFERENCED_PARAMETER(Driver);

    WdfFdoInitSetFilter(DeviceInit);
    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_MOUSE);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, OIB_MOU_FILTER_CONTEXT);
    deviceAttributes.EvtCleanupCallback = OibMouEvtFilterDeviceCleanup;

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &hDevice);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    // Parallel, not sequential — same deadlock concern as kbdfilter.c's OibKbdEvtDeviceAdd.
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig, WdfIoQueueDispatchParallel);
    ioQueueConfig.EvtIoInternalDeviceControl = OibMouEvtInternalDeviceControl;

    status = WdfIoQueueCreate(hDevice, &ioQueueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    // Best-effort: if every mouse slot is already taken (an 11th mouse), this device still
    // attaches and filters/passes through normally, it's just unreachable via any
    // \\.\interceptionNN control device until a slot frees up. Not a device-creation failure.
    (VOID) OibSlotAssign(FALSE, hDevice, &slotIndex);

    filterContext = OibGetMouFilterContext(hDevice);
    filterContext->SlotIndex = slotIndex;

    return STATUS_SUCCESS;
}

VOID
OibMouEvtFilterDeviceCleanup(
    _In_ WDFOBJECT Device
    )
{
    POIB_MOU_FILTER_CONTEXT filterContext = OibGetMouFilterContext((WDFDEVICE)Device);

    // Safe even if OibSlotAssign never succeeded for this device (OIB_SLOT_INDEX_NONE).
    OibSlotRelease(filterContext->SlotIndex);
}

VOID
OibMouEvtInternalDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
{
    WDFDEVICE hDevice;
    POIB_MOU_FILTER_CONTEXT filterContext;
    PCONNECT_DATA connectData;
    NTSTATUS status = STATUS_SUCCESS;
    size_t length;
    WDF_REQUEST_SEND_OPTIONS options;
    BOOLEAN sent;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    hDevice = WdfIoQueueGetDevice(Queue);
    filterContext = OibGetMouFilterContext(hDevice);

    switch (IoControlCode) {
    case IOCTL_INTERNAL_MOUSE_CONNECT:
        // Only one class driver (mouclass) is expected to connect to this stack.
        if (filterContext->UpperConnectData.ClassService != NULL) {
            status = STATUS_SHARING_VIOLATION;
            break;
        }

        status = WdfRequestRetrieveInputBuffer(Request, sizeof(CONNECT_DATA), &connectData, &length);
        if (!NT_SUCCESS(status)) {
            break;
        }

        filterContext->UpperConnectData = *connectData;

        connectData->ClassDeviceObject = WdfDeviceWdmGetDeviceObject(hDevice);

#pragma warning(push)
#pragma warning(disable:4152) // nonstandard extension: function/data pointer conversion
        connectData->ClassService = OibMouFilterServiceCallback;
#pragma warning(pop)

        break;

    default:
        // Pass everything else straight through, unmodified — see kbdfilter.c's equivalent
        // comment for why the i8042-specific hook IOCTLs are intentionally not handled here.
        break;
    }

    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);

    sent = WdfRequestSend(Request, WdfDeviceGetIoTarget(hDevice), &options);
    if (!sent) {
        status = WdfRequestGetStatus(Request);
        WdfRequestComplete(Request, status);
    }
}

VOID
OibMouFilterServiceCallback(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PMOUSE_INPUT_DATA InputDataStart,
    _In_ PMOUSE_INPUT_DATA InputDataEnd,
    _Inout_ PULONG InputDataConsumed
    )
{
    WDFDEVICE hDevice;
    POIB_MOU_FILTER_CONTEXT filterContext;
    PMOUSE_INPUT_DATA stroke;

    hDevice = WdfWdmDeviceGetWdfDeviceHandle(DeviceObject);
    filterContext = OibGetMouFilterContext(hDevice);

    // Per-record dispatch — see kbdfilter.c's OibKbFilterServiceCallback for the rationale
    // (capture is per-record, not per-batch).
    for (stroke = InputDataStart; stroke < InputDataEnd; stroke++) {
        BOOLEAN captured = FALSE;

        if (filterContext->SlotIndex != OIB_SLOT_INDEX_NONE) {
            OibDispatchMouseStroke(filterContext->SlotIndex, stroke, &captured);
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

    *InputDataConsumed = (ULONG)(InputDataEnd - InputDataStart);
}
