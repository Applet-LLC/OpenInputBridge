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

    // TODO(M0): implement OibCreateControlDevices() below — must create all
    // OIB_DEVICE_SLOT_COUNT control devices unconditionally before DriverEntry returns.
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
    UNREFERENCED_PARAMETER(Driver);

    // TODO(M0): for NN in 00..OIB_DEVICE_SLOT_COUNT-1:
    //   - WdfControlDeviceInitAllocate
    //   - WdfDeviceInitAssignName(L"\\Device\\interceptionNN")
    //   - WdfDeviceInitSetIoType(WdfDeviceIoBuffered)  (matches METHOD_BUFFERED IOCTLs)
    //   - WdfDeviceCreate
    //   - WdfDeviceCreateSymbolicLink(L"\\DosDevices\\interceptionNN")
    //   - WdfControlFinishInitializing
    // Store NN in the control device's own context (see ioctl.c) so EvtIoDeviceControl can
    // look up its assigned slot in the slot table (slots.c, M2).
    // Must succeed for all OIB_DEVICE_SLOT_COUNT devices unconditionally — see driver.h and
    // docs/PROTOCOL.md for why partial success is not acceptable here.

    return STATUS_NOT_IMPLEMENTED;
}
