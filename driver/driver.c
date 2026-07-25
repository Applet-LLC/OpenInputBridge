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

// Keyboard/Mouse device setup class GUIDs (see docs/PROTOCOL.md and OpenInputBridge.inx).
// Used to tell apart which class stack OibEvtDeviceAdd is being called for, since this one
// driver is registered as an upper filter under both classes' UpperFilters (installer/).
// IoGetDeviceProperty(..., DevicePropertyClassGuid, ...) returns the class GUID as a
// printable string (not a binary GUID), so these are compared as strings too.
static const UNICODE_STRING OibKeyboardClassGuidString =
    RTL_CONSTANT_STRING(L"{4D36E96B-E325-11CE-BFC1-08002BE10318}");
static const UNICODE_STRING OibMouseClassGuidString =
    RTL_CONSTANT_STRING(L"{4D36E96F-E325-11CE-BFC1-08002BE10318}");

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
    NTSTATUS status;
    PDEVICE_OBJECT pdo;
    WCHAR classGuidBuffer[64];
    ULONG resultLength = 0;
    UNICODE_STRING classGuidString;

    // PnP calls this once per keyboard/mouse stack as devices are enumerated, since this
    // driver is registered as an upper filter under both the Keyboard and Mouse device setup
    // classes (installer/). Determine which class this particular stack belongs to before
    // creating anything, per the pattern in Microsoft's kbfiltr/moufiltr samples ("query the
    // device properties ... and based on that, decide to create a filter device object").
    pdo = WdfFdoInitWdmGetPhysicalDevice(DeviceInit);
    if (pdo == NULL) {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    status = IoGetDeviceProperty(
        pdo,
        DevicePropertyClassGuid,
        sizeof(classGuidBuffer),
        classGuidBuffer,
        &resultLength
        );
    if (!NT_SUCCESS(status)) {
        return status;
    }

    RtlInitUnicodeString(&classGuidString, classGuidBuffer);

    if (RtlEqualUnicodeString(&classGuidString, &OibKeyboardClassGuidString, TRUE)) {
        return OibKbdEvtDeviceAdd(Driver, DeviceInit);
    }

    if (RtlEqualUnicodeString(&classGuidString, &OibMouseClassGuidString, TRUE)) {
        return OibMouEvtDeviceAdd(Driver, DeviceInit);
    }

    // Not a class we filter (shouldn't normally happen given how this driver is installed,
    // but defensively: per the kbfiltr sample's own guidance, if we're not interested in
    // filtering this instance we simply return success without creating a framework device).
    return STATUS_SUCCESS;
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
