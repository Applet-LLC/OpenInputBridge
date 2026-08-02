// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// M0 (driver skeleton): DriverEntry creates the WDFDRIVER and this binary's share of the 20
// always-present control devices. Filter FDO attach (EvtDeviceAdd, IOCTL_INTERNAL_*_CONNECT
// hijack) lands in M1 (kbdfilter.c / mousefilter.c). Slot table lands in M2 (slots.c). IOCTL
// handling lands in M3/M4/M5 (ioctl.c). See the project plan for the full milestone list.
//
// Shared verbatim between keyboard.vcxproj and mouse.vcxproj (see driver.h's comment on
// OIB_BUILD_KEYBOARD/OIB_BUILD_MOUSE) — the only per-binary logic is which single filter type
// gets attached below, gated on the same two build-time defines.

#include "driver.h"
#include "ioctl.h"
#include "slots.h"

#include <ntstrsafe.h>

#if defined(OIB_BUILD_KEYBOARD)
#include "kbdfilter.h"
#elif defined(OIB_BUILD_MOUSE)
#include "mousefilter.h"
#endif

// Grants SYSTEM and built-in Administrators full access, and Everyone (World) generic
// read/write. The upstream library opens each \\.\interceptionNN with plain GENERIC_READ
// and no elevation of its own (see docs/PROTOCOL.md), so ordinary unprivileged user-mode
// processes must be able to open these control devices. This DACL choice is a first cut;
// revisit if M5's black-box observation of the real driver's ACL turns out to differ.
static const WCHAR OibControlDeviceSddl[] = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;WD)";

// This binary's own device setup class GUID (see docs/PROTOCOL.md and keyboard.inx/mouse.inx) —
// used to confirm OibEvtDeviceAdd is being called for the class stack this binary is registered
// as an upper filter under (installer/), before attaching anything. IoGetDeviceProperty(...,
// DevicePropertyClassGuid, ...) returns the class GUID as a printable string (not a binary
// GUID), so this is compared as a string too.
#if defined(OIB_BUILD_KEYBOARD)
static const UNICODE_STRING OibTargetClassGuidString =
    RTL_CONSTANT_STRING(L"{4D36E96B-E325-11CE-BFC1-08002BE10318}");
#elif defined(OIB_BUILD_MOUSE)
static const UNICODE_STRING OibTargetClassGuidString =
    RTL_CONSTANT_STRING(L"{4D36E96F-E325-11CE-BFC1-08002BE10318}");
#endif

// Reads the keyboard side's share of the 20 slots (OIB_KEYBOARD_SLOT_COUNT_VALUE_NAME under
// this service's own \Parameters key — see driver.h). Both the keyboard and mouse services'
// registrations carry the same value (installer/ keeps them in sync), so either binary can
// read it directly and know exactly where the boundary is, without asking the other one.
// Falls back to OIB_DEFAULT_KEYBOARD_SLOT_COUNT if the key/value is absent, unreadable, or out
// of range — this is what makes the feature purely opt-in (an unconfigured system behaves
// exactly as it did before this existed: 10 keyboard / 10 mouse).
static
ULONG
OibReadConfiguredKeyboardSlotCount(
    _In_ WDFDRIVER Driver
    )
{
    NTSTATUS status;
    WDFKEY parametersKey;
    DECLARE_CONST_UNICODE_STRING(valueName, OIB_KEYBOARD_SLOT_COUNT_VALUE_NAME);
    ULONG value;

    status = WdfDriverOpenParametersRegistryKey(
        Driver, KEY_QUERY_VALUE, WDF_NO_OBJECT_ATTRIBUTES, &parametersKey
        );
    if (!NT_SUCCESS(status)) {
        return OIB_DEFAULT_KEYBOARD_SLOT_COUNT;
    }

    status = WdfRegistryQueryULong(parametersKey, &valueName, &value);
    WdfRegistryClose(parametersKey);

    if (!NT_SUCCESS(status) || value > OIB_TOTAL_DEVICE_SLOT_COUNT) {
        return OIB_DEFAULT_KEYBOARD_SLOT_COUNT;
    }

    return value;
}

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
    ULONG keyboardSlotCount;
    ULONG activeSlotCount;
    ULONG deviceNumberBase;

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

    // Resolve this binary's own share of the 20 slots (see docs/DECISIONS.md's 2026-08-02
    // entry) before touching the slot table or creating any control device.
    keyboardSlotCount = OibReadConfiguredKeyboardSlotCount(driver);
#if defined(OIB_BUILD_KEYBOARD)
    activeSlotCount = keyboardSlotCount;
    deviceNumberBase = 0;
#elif defined(OIB_BUILD_MOUSE)
    activeSlotCount = OIB_TOTAL_DEVICE_SLOT_COUNT - keyboardSlotCount;
    deviceNumberBase = keyboardSlotCount;
#endif

    // Must precede OibCreateControlDevices: once the control devices exist, I/O could in
    // principle start arriving on them (or PnP could start calling OibEvtDeviceAdd) before
    // DriverEntry returns, and both paths touch the slot table.
    status = OibSlotTableInitialize(driver, activeSlotCount);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    // Create all of this binary's ActiveSlotCount control devices unconditionally before
    // DriverEntry returns (see OibCreateControlDevices for why partial success is not
    // acceptable here).
    status = OibCreateControlDevices(driver, activeSlotCount, deviceNumberBase);

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

    // PnP calls this once per stack as devices are enumerated, since this driver is registered
    // as an upper filter under exactly one device setup class (installer/) — Keyboard for
    // keyboard.sys, Mouse for mouse.sys. Confirm the class before creating anything, per the
    // pattern in Microsoft's kbfiltr/moufiltr samples ("query the device properties ... and
    // based on that, decide to create a filter device object").
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

    if (RtlEqualUnicodeString(&classGuidString, &OibTargetClassGuidString, TRUE)) {
#if defined(OIB_BUILD_KEYBOARD)
        return OibKbdEvtDeviceAdd(Driver, DeviceInit);
#elif defined(OIB_BUILD_MOUSE)
        return OibMouEvtDeviceAdd(Driver, DeviceInit);
#endif
    }

    // Not the class we filter (shouldn't normally happen given how this driver is installed,
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
    _In_ WDFDRIVER Driver,
    _In_ ULONG ActiveSlotCount,
    _In_ ULONG DeviceNumberBase
    )
{
    NTSTATUS status;
    UNICODE_STRING sddlString;
    ULONG index;

    RtlInitUnicodeString(&sddlString, OibControlDeviceSddl);

    // Must create all ActiveSlotCount devices unconditionally, independent of how many
    // physical keyboards/mice are attached: the unmodified upstream interception_create_context()
    // fails outright if any one of its 20 CreateFileA calls (across both binaries) fails (see
    // docs/PROTOCOL.md). If any slot below fails, we return failure and let DriverEntry fail
    // the load — WDF tears down the already-created control devices (parented to the
    // WDFDRIVER) as part of that unwind, so there is no partial, half-usable device set left
    // behind.
    for (index = 0; index < ActiveSlotCount; index++) {
        PWDFDEVICE_INIT deviceInit;
        WDF_OBJECT_ATTRIBUTES attributes;
        WDF_OBJECT_ATTRIBUTES fileAttributes;
        WDF_FILEOBJECT_CONFIG fileConfig;
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
            index + DeviceNumberBase
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

        // Every open of \\.\interceptionNN gets its own OIB_FILE_CONTEXT (filter bitmask,
        // "unempty" event, capture queue — see ioctl.h), matching the real protocol's
        // per-context model.
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&fileAttributes, OIB_FILE_CONTEXT);
        WDF_FILEOBJECT_CONFIG_INIT(&fileConfig, OibCtlEvtFileCreate, OibCtlEvtFileClose, WDF_NO_EVENT_CALLBACK);
        WdfDeviceInitSetFileObjectConfig(deviceInit, &fileConfig, &fileAttributes);

        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, OIB_CONTROL_DEVICE_CONTEXT);

        status = WdfDeviceCreate(&deviceInit, &attributes, &controlDevice);
        if (!NT_SUCCESS(status)) {
            // deviceInit is freed by the framework on failure of WdfDeviceCreate itself.
            return status;
        }

        context = OibGetControlDeviceContext(controlDevice);
        context->SlotIndex = index;
        // Every slot in this binary's table is the same kind (see driver.h's comment on
        // OIB_BUILD_KEYBOARD/OIB_BUILD_MOUSE) — not index-dependent the way it was when one
        // driver's table held both kinds, which also avoids C4296 (index < 0 is always false
        // when OIB_KEYBOARD_SLOT_COUNT is 0, i.e. in the mouse build).
        context->IsKeyboard = OIB_IS_KEYBOARD_BUILD;

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
            index + DeviceNumberBase
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
