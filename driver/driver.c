// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// M0 (driver skeleton): DriverEntry creates the WDFDRIVER and the 20 always-present control
// devices. Filter FDO attach (EvtDeviceAdd, IOCTL_INTERNAL_*_CONNECT hijack) lands in M1
// (kbdfilter.c / mousefilter.c). Slot table lands in M2 (slots.c). IOCTL handling lands in
// M3/M4/M5 (ioctl.c). See the project plan for the full milestone list.

#include "driver.h"
#include "kbdfilter.h"
#include "mousefilter.h"
#include "ioctl.h"

#include <ntstrsafe.h>

// Grants SYSTEM and built-in Administrators full access, and Everyone (World) generic
// read/write. The upstream library opens each \\.\interceptionNN with plain GENERIC_READ
// and no elevation of its own (see docs/PROTOCOL.md), so ordinary unprivileged user-mode
// processes must be able to open these control devices. This DACL choice is a first cut;
// revisit if M5's black-box observation of the real driver's ACL turns out to differ.
static const WCHAR OibControlDeviceSddl[] = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;WD)";

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    NTSTATUS status;
    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDFDRIVER driver;

    WDF_DRIVER_CONFIG_INIT(&config, OibEvtDeviceAdd);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = OibEvtDriverContextCleanup;

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        &attributes,
        &config,
        &driver
        );

    if (!NT_SUCCESS(status)) {
        return status;
    }

    // Create all OIB_DEVICE_SLOT_COUNT control devices unconditionally before DriverEntry
    // returns (see OibCreateControlDevices for why partial success is not acceptable here).
    status = OibCreateControlDevices(driver);

    return status;
}

NTSTATUS
OibEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    // TODO(M1): PnP calls this once per keyboard/mouse stack as devices are enumerated.
    // Determine whether DeviceInit targets a Keyboard-class or Mouse-class stack (e.g. via
    // the resource/compatible IDs available on PWDFDEVICE_INIT at this point) and dispatch
    // to OibKbdEvtDeviceAdd or OibMouEvtDeviceAdd accordingly.
    UNREFERENCED_PARAMETER(Driver);
    UNREFERENCED_PARAMETER(DeviceInit);

    return STATUS_NOT_IMPLEMENTED;
}

VOID
OibEvtDriverContextCleanup(
    _In_ WDFOBJECT DriverObject
    )
{
    UNREFERENCED_PARAMETER(DriverObject);
    // TODO: release any driver-global resources (slot table lock, etc.) allocated outside WDF's
    // own object lifetime management, once slots.c/slots.h's slot table is implemented (M2).
}

NTSTATUS
OibCreateControlDevices(
    _In_ WDFDRIVER Driver
    )
{
    NTSTATUS status;
    UNICODE_STRING sddlString;
    ULONG index;

    RtlInitUnicodeString(&sddlString, OibControlDeviceSddl);

    // Must create all OIB_DEVICE_SLOT_COUNT devices unconditionally, independent of how many
    // physical keyboards/mice are attached: the unmodified upstream interception_create_context()
    // fails outright if any one of its 20 CreateFileA calls fails (see docs/PROTOCOL.md). If
    // any slot below fails, we return failure and let DriverEntry fail the load — WDF tears down
    // the already-created control devices (parented to the WDFDRIVER) as part of that unwind, so
    // there is no partial, half-usable device set left behind.
    for (index = 0; index < OIB_DEVICE_SLOT_COUNT; index++) {
        PWDFDEVICE_INIT deviceInit;
        WDF_OBJECT_ATTRIBUTES attributes;
        WDF_IO_QUEUE_CONFIG queueConfig;
        WDFDEVICE controlDevice;
        POIB_CONTROL_DEVICE_CONTEXT context;
        WCHAR deviceNameBuffer[sizeof(L"\\Device\\interception99")];
        WCHAR symlinkNameBuffer[sizeof(L"\\DosDevices\\interception99")];
        UNICODE_STRING deviceName;
        UNICODE_STRING symlinkName;

        status = RtlStringCbPrintfW(
            deviceNameBuffer,
            sizeof(deviceNameBuffer),
            L"\\Device\\interception%02lu",
            index
            );
        if (!NT_SUCCESS(status)) {
            return status;
        }
        RtlInitUnicodeString(&deviceName, deviceNameBuffer);

        deviceInit = WdfControlDeviceInitAllocate(Driver, &sddlString);
        if (deviceInit == NULL) {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        WdfDeviceInitSetIoType(deviceInit, WdfDeviceIoBuffered);

        status = WdfDeviceInitAssignName(deviceInit, &deviceName);
        if (!NT_SUCCESS(status)) {
            WdfDeviceInitFree(deviceInit);
            return status;
        }

        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, OIB_CONTROL_DEVICE_CONTEXT);

        status = WdfDeviceCreate(&deviceInit, &attributes, &controlDevice);
        if (!NT_SUCCESS(status)) {
            // deviceInit is freed by the framework on failure of WdfDeviceCreate itself.
            return status;
        }

        context = OibGetControlDeviceContext(controlDevice);
        context->SlotIndex = index;
        context->IsKeyboard = (BOOLEAN)(index < OIB_KEYBOARD_SLOT_COUNT);

        WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
        queueConfig.EvtIoDeviceControl = OibCtlEvtIoDeviceControl;

        status = WdfIoQueueCreate(controlDevice, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
        if (!NT_SUCCESS(status)) {
            return status;
        }

        status = RtlStringCbPrintfW(
            symlinkNameBuffer,
            sizeof(symlinkNameBuffer),
            L"\\DosDevices\\interception%02lu",
            index
            );
        if (!NT_SUCCESS(status)) {
            return status;
        }
        RtlInitUnicodeString(&symlinkName, symlinkNameBuffer);

        status = WdfDeviceCreateSymbolicLink(controlDevice, &symlinkName);
        if (!NT_SUCCESS(status)) {
            return status;
        }

        WdfControlFinishInitializing(controlDevice);
    }

    return STATUS_SUCCESS;
}
