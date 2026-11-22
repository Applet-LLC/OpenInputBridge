// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// M0 (driver skeleton): DriverEntry creates the WDFDRIVER and this binary's share of the 20
// always-present control devices. Filter FDO attach (EvtDeviceAdd, IOCTL_INTERNAL_*_CONNECT
// hijack) lands in M1 (kbdfilter.c / mousefilter.c). Slot table lands in M2 (slots.c). IOCTL
// handling lands in M3/M4/M5 (ioctl.c). See the project plan for the full milestone list.
//
// Shared verbatim between oib_kbd.vcxproj and oib_mou.vcxproj (see driver.h's comment on
// OIB_BUILD_KEYBOARD/OIB_BUILD_MOUSE) — the only per-binary logic is which single filter type
// gets attached below, gated on the same two build-time defines.

#include "driver.h"
#include "ioctl.h"
#include "slots.h"
#include "oib_mgmt.h"

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

// Management device (oib_mgmt.h): SYSTEM and Administrators only. Unlike the control devices
// above it must never be reachable by ordinary users, since it can hide/show them.
static const WCHAR OibMgmtDeviceSddl[] = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)";

// Parameters OibCreateControlDevices was first called with at DriverEntry, kept so that
// IOCTL_OIB_SHOW_DEVICES can re-create exactly the same set of control devices later.
static WDFDRIVER OibDriverHandle;
static ULONG OibCtlActiveSlotCount;
static ULONG OibCtlDeviceNumberBase;
static WCHAR OibCtlDeviceNameBase[OIB_DEVICE_NAME_BASE_MAX_CHARS + 1];

// The control devices currently in existence (NULL entries = not created / already deleted).
// Written only from DriverEntry and the management queue, which is sequential.
static WDFDEVICE OibControlDevices[OIB_TOTAL_DEVICE_SLOT_COUNT];

// Guards OibDevicesHidden against OibAttachFileContextIfVisible. Never held across a
// WdfObjectDelete or a device creation. Lock order: OibVisibilityLock, then a slot's
// InstancesLock (OibSlotAnyOpenInstances / OibSlotAttachFileContext).
static WDFSPINLOCK OibVisibilityLock;
static BOOLEAN OibDevicesHidden;

static NTSTATUS OibCreateManagementDevice(_In_ WDFDRIVER Driver);
static NTSTATUS OibHideControlDevices(VOID);
static NTSTATUS OibShowControlDevices(VOID);
static VOID OibDeleteAllControlDevices(VOID);

// This binary's own device setup class GUID (see docs/PROTOCOL.md and oib_kbd.inx/oib_mou.inx) —
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

// True for exactly the characters OibReadConfiguredDeviceNameBase accepts in a configured base
// name: plain ASCII alnum plus '_'/'-'. Deliberately excludes '\\' and anything else that could
// change the shape of the \Device\<base>NN / \DosDevices\<base>NN names built from it (e.g. an
// embedded path separator turning one path component into several).
static
BOOLEAN
OibIsValidDeviceNameBaseChar(
    _In_ WCHAR Character
    )
{
    return (Character >= L'0' && Character <= L'9') ||
        (Character >= L'A' && Character <= L'Z') ||
        (Character >= L'a' && Character <= L'z') ||
        Character == L'_' ||
        Character == L'-';
}

// Reads OIB_DEVICE_NAME_BASE_VALUE_NAME (REG_SZ) from this service's own \Parameters key, for
// renaming \Device\interceptionNN / \DosDevices\interceptionNN to \Device\<base>NN /
// \DosDevices\<base>NN (see driver.h). Always leaves DeviceNameBase holding a valid,
// NUL-terminated name: falls back to OIB_DEFAULT_DEVICE_NAME_BASE if the key/value is absent,
// unreadable, empty, longer than OIB_DEVICE_NAME_BASE_MAX_CHARS, or contains any character
// OibIsValidDeviceNameBaseChar rejects — this is what makes the feature purely opt-in.
static
VOID
OibReadConfiguredDeviceNameBase(
    _In_ WDFDRIVER Driver,
    _Out_writes_z_(OIB_DEVICE_NAME_BASE_MAX_CHARS + 1) PWCHAR DeviceNameBase
    )
{
    NTSTATUS status;
    WDFKEY parametersKey;
    DECLARE_CONST_UNICODE_STRING(valueName, OIB_DEVICE_NAME_BASE_VALUE_NAME);
    WCHAR rawBuffer[OIB_DEVICE_NAME_BASE_MAX_CHARS + 1];
    UNICODE_STRING rawValue;
    ULONG charCount;
    ULONG i;
    BOOLEAN valid;

    RtlInitEmptyUnicodeString(&rawValue, rawBuffer, sizeof(rawBuffer));

    status = WdfDriverOpenParametersRegistryKey(
        Driver, KEY_QUERY_VALUE, WDF_NO_OBJECT_ATTRIBUTES, &parametersKey
        );
    if (NT_SUCCESS(status)) {
        status = WdfRegistryQueryUnicodeString(parametersKey, &valueName, NULL, &rawValue);
        WdfRegistryClose(parametersKey);
    }

    if (NT_SUCCESS(status)) {
        charCount = (ULONG)(rawValue.Length / sizeof(WCHAR));
        // Some REG_SZ writers include the terminating NUL in the value's byte length; drop it
        // from the count so the validation loop below doesn't see a spurious NUL character.
        if (charCount > 0 && rawBuffer[charCount - 1] == L'\0') {
            charCount--;
        }

        valid = (charCount > 0) && (charCount <= OIB_DEVICE_NAME_BASE_MAX_CHARS);
        for (i = 0; valid && i < charCount; i++) {
            valid = OibIsValidDeviceNameBaseChar(rawBuffer[i]);
        }

        if (valid) {
            RtlCopyMemory(DeviceNameBase, rawBuffer, charCount * sizeof(WCHAR));
            DeviceNameBase[charCount] = L'\0';
            return;
        }
    }

    RtlCopyMemory(
        DeviceNameBase, OIB_DEFAULT_DEVICE_NAME_BASE, sizeof(OIB_DEFAULT_DEVICE_NAME_BASE)
        );
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
    WCHAR deviceNameBase[OIB_DEVICE_NAME_BASE_MAX_CHARS + 1];

#ifdef OIB_WIN10_COMPAT
    // Required before any ExAllocatePoolZero call in the Windows 10 compatibility build
    // (POOL_ZERO_DOWN_LEVEL_SUPPORT); see driver.h's OibAllocPagedZero comment.
    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);
#endif

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
    OibReadConfiguredDeviceNameBase(driver, deviceNameBase);
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
    OibDriverHandle = driver;
    OibCtlActiveSlotCount = activeSlotCount;
    OibCtlDeviceNumberBase = deviceNumberBase;
    RtlCopyMemory(OibCtlDeviceNameBase, deviceNameBase, sizeof(OibCtlDeviceNameBase));
    OibDevicesHidden = FALSE;

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = driver;
    status = WdfSpinLockCreate(&attributes, &OibVisibilityLock);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = OibCreateControlDevices(driver, activeSlotCount, deviceNumberBase, deviceNameBase);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    // Best effort: the hide/show management device is an optional extra. If it cannot be created
    // the driver still provides the full Interception-compatible interface.
    (VOID)OibCreateManagementDevice(driver);

    return STATUS_SUCCESS;
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
    // oib_kbd.sys, Mouse for oib_mou.sys. Confirm the class before creating anything, per the
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
    _In_ ULONG DeviceNumberBase,
    _In_ PCWSTR DeviceNameBase
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
        WCHAR deviceNameBuffer[OIB_DEVICE_OR_SYMLINK_NAME_MAX_CHARS];
        WCHAR symlinkNameBuffer[OIB_DEVICE_OR_SYMLINK_NAME_MAX_CHARS];
        UNICODE_STRING deviceName;
        UNICODE_STRING symlinkName;

        status = RtlStringCbPrintfW(
            deviceNameBuffer,
            sizeof(deviceNameBuffer),
            L"\\Device\\%ls%02lu",
            DeviceNameBase,
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

        // Remembered immediately (before the remaining setup below can fail) so a failed
        // IOCTL_OIB_SHOW_DEVICES can roll back every device created so far.
        if (index < OIB_TOTAL_DEVICE_SLOT_COUNT) {
            OibControlDevices[index] = controlDevice;
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
            L"\\DosDevices\\%ls%02lu",
            DeviceNameBase,
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

VOID
OibDeleteAllControlDevices(
    VOID
    )
{
    ULONG index;

    for (index = 0; index < OIB_TOTAL_DEVICE_SLOT_COUNT; index++) {
        if (OibControlDevices[index] != NULL) {
            // Removes the device object and its \DosDevices\ symbolic link. The framework
            // defers the actual teardown until the last handle is closed, but hide only gets
            // here when no handle is open (see OibHideControlDevices).
            WdfObjectDelete(OibControlDevices[index]);
            OibControlDevices[index] = NULL;
        }
    }
}

NTSTATUS
OibAttachFileContextIfVisible(
    _In_ ULONG SlotIndex,
    _In_ PLIST_ENTRY FileContextLinkage
    )
{
    NTSTATUS status = STATUS_SUCCESS;

    WdfSpinLockAcquire(OibVisibilityLock);
    if (OibDevicesHidden) {
        status = STATUS_DELETE_PENDING;
    } else {
        OibSlotAttachFileContext(SlotIndex, FileContextLinkage);
    }
    WdfSpinLockRelease(OibVisibilityLock);

    return status;
}

NTSTATUS
OibHideControlDevices(
    VOID
    )
{
    WdfSpinLockAcquire(OibVisibilityLock);

    if (OibDevicesHidden) {
        WdfSpinLockRelease(OibVisibilityLock);
        return STATUS_SUCCESS;
    }

    if (OibSlotAnyOpenInstances()) {
        WdfSpinLockRelease(OibVisibilityLock);
        return STATUS_DEVICE_BUSY;
    }

    // From here on OibAttachFileContextIfVisible refuses new handles, so no client can sneak in
    // while the devices are deleted below (outside the spin lock: deletion needs PASSIVE_LEVEL).
    OibDevicesHidden = TRUE;
    WdfSpinLockRelease(OibVisibilityLock);

    OibDeleteAllControlDevices();

    return STATUS_SUCCESS;
}

NTSTATUS
OibShowControlDevices(
    VOID
    )
{
    NTSTATUS status;

    if (!OibDevicesHidden) {
        return STATUS_SUCCESS;
    }

    // OibDevicesHidden stays TRUE while the devices are created, so nobody can open a
    // half-built set. A name collision here means an old device object is still alive.
    status = OibCreateControlDevices(
        OibDriverHandle,
        OibCtlActiveSlotCount,
        OibCtlDeviceNumberBase,
        OibCtlDeviceNameBase
        );
    if (!NT_SUCCESS(status)) {
        OibDeleteAllControlDevices();
        return status;
    }

    WdfSpinLockAcquire(OibVisibilityLock);
    OibDevicesHidden = FALSE;
    WdfSpinLockRelease(OibVisibilityLock);

    return STATUS_SUCCESS;
}

NTSTATUS
OibCreateManagementDevice(
    _In_ WDFDRIVER Driver
    )
{
    NTSTATUS status;
    UNICODE_STRING sddlString;
    UNICODE_STRING deviceName;
    UNICODE_STRING symlinkName;
    PWDFDEVICE_INIT deviceInit;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDFDEVICE device;

#if defined(OIB_BUILD_KEYBOARD)
    RtlInitUnicodeString(&deviceName, L"\\Device\\" OIB_KBD_MGMT_NAME);
    RtlInitUnicodeString(&symlinkName, L"\\DosDevices\\" OIB_KBD_MGMT_NAME);
#else
    RtlInitUnicodeString(&deviceName, L"\\Device\\" OIB_MOU_MGMT_NAME);
    RtlInitUnicodeString(&symlinkName, L"\\DosDevices\\" OIB_MOU_MGMT_NAME);
#endif
    RtlInitUnicodeString(&sddlString, OibMgmtDeviceSddl);

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

    // Hiding deletes devices, which must happen at PASSIVE_LEVEL.
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ExecutionLevel = WdfExecutionLevelPassive;

    status = WdfDeviceCreate(&deviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    // Sequential: hide/show requests are serialized by the queue itself.
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchSequential);
    queueConfig.EvtIoDeviceControl = OibMgmtEvtIoDeviceControl;

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ExecutionLevel = WdfExecutionLevelPassive;

    status = WdfIoQueueCreate(device, &queueConfig, &attributes, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(device);
        return status;
    }

    status = WdfDeviceCreateSymbolicLink(device, &symlinkName);
    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(device);
        return status;
    }

    WdfControlFinishInitializing(device);

    return STATUS_SUCCESS;
}

VOID
OibMgmtEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
{
    NTSTATUS status;
    size_t information = 0;

    UNREFERENCED_PARAMETER(Queue);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    switch (IoControlCode) {
    case IOCTL_OIB_HIDE_DEVICES:
        status = OibHideControlDevices();
        break;

    case IOCTL_OIB_SHOW_DEVICES:
        status = OibShowControlDevices();
        break;

    case IOCTL_OIB_GET_VISIBILITY:
        {
            OIB_VISIBILITY_INFO *info = NULL;

            status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*info), (PVOID *)&info, NULL);
            if (NT_SUCCESS(status)) {
                WdfSpinLockAcquire(OibVisibilityLock);
                info->Visible = OibDevicesHidden ? 0 : 1;
                WdfSpinLockRelease(OibVisibilityLock);
                info->DeviceCount = OibCtlActiveSlotCount;
                information = sizeof(*info);
            }
        }
        break;

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestCompleteWithInformation(Request, status, information);
}
