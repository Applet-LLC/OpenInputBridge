// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// See mousefilter.h. Mirrors kbdfilter.c using IOCTL_INTERNAL_MOUSE_CONNECT and
// MOUSE_INPUT_DATA instead of the keyboard equivalents. M1 scope: attach as an upper filter
// and pass every mouse event through unmodified.

#include "mousefilter.h"

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

    UNREFERENCED_PARAMETER(Driver);

    WdfFdoInitSetFilter(DeviceInit);
    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_MOUSE);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, OIB_MOU_FILTER_CONTEXT);

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

    // TODO(M2): assign this FDO to a free mouse slot
    // (OIB_KEYBOARD_SLOT_COUNT..OIB_DEVICE_SLOT_COUNT-1) in the global slot table (slots.c).

    return STATUS_SUCCESS;
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

    hDevice = WdfWdmDeviceGetWdfDeviceHandle(DeviceObject);
    filterContext = OibGetMouFilterContext(hDevice);

    // TODO(M3): consult this slot's active filter bitmask; for now (M1), unconditional
    // pass-through to mouclass's real callback — see kbdfilter.c's equivalent for the M3 plan.
    (*(PSERVICE_CALLBACK_ROUTINE)(ULONG_PTR)filterContext->UpperConnectData.ClassService)(
        filterContext->UpperConnectData.ClassDeviceObject,
        InputDataStart,
        InputDataEnd,
        InputDataConsumed
        );
}
