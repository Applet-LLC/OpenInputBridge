// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// See slots.h. A single spinlock-protected array is enough here: assignment/release only
// happens on PnP arrival/removal (infrequent), and lookups (OibSlotAcquireFilterDevice) hold
// the lock only long enough to copy a handle and take a reference, never while touching the
// filter device itself.
//
// The table is always sized at OIB_TOTAL_DEVICE_SLOT_COUNT (20) regardless of which binary
// this is compiled into — only the first OibActiveSlotCount entries are ever initialized/used
// (see OibSlotTableInitialize), so no dynamic allocation is needed even though the split
// between keyboard.sys and mouse.sys is a runtime, registry-configured value (docs/DECISIONS.md
// 2026-08-02 entry) rather than a compile-time constant.

#include "slots.h"

typedef struct _OIB_SLOT_ENTRY
{
    WDFDEVICE FilterDevice; // NULL if unassigned. Guarded by OibSlotTableLock.

    // Guards both OpenInstances and the Filter/UnemptyEvent/capture-queue fields of every
    // OIB_FILE_CONTEXT linked into it (see slots.h).
    WDFSPINLOCK InstancesLock;
    LIST_ENTRY OpenInstances;
} OIB_SLOT_ENTRY;

static OIB_SLOT_ENTRY OibSlotTable[OIB_TOTAL_DEVICE_SLOT_COUNT];
static WDFSPINLOCK OibSlotTableLock;

// This binary's own share of the 20 slots (0..OIB_TOTAL_DEVICE_SLOT_COUNT), set once by
// OibSlotTableInitialize. Only OibSlotTable[0..OibActiveSlotCount-1] is ever touched.
static ULONG OibActiveSlotCount;

NTSTATUS
OibSlotTableInitialize(
    _In_ WDFDRIVER Driver,
    _In_ ULONG ActiveSlotCount
    )
{
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES attributes;
    ULONG index;

    OibActiveSlotCount = ActiveSlotCount;

    RtlZeroMemory(OibSlotTable, sizeof(OibSlotTable));

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Driver;

    status = WdfSpinLockCreate(&attributes, &OibSlotTableLock);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    for (index = 0; index < OibActiveSlotCount; index++) {
        InitializeListHead(&OibSlotTable[index].OpenInstances);

        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = Driver;

        status = WdfSpinLockCreate(&attributes, &OibSlotTable[index].InstancesLock);
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS
OibSlotAssign(
    _In_ WDFDEVICE FilterDevice,
    _Out_ ULONG *AssignedSlotIndex
    )
{
    ULONG index;
    NTSTATUS status = STATUS_DEVICE_NOT_READY;

    *AssignedSlotIndex = OIB_SLOT_INDEX_NONE;

    WdfSpinLockAcquire(OibSlotTableLock);

    for (index = 0; index < OibActiveSlotCount; index++) {
        if (OibSlotTable[index].FilterDevice == NULL) {
            OibSlotTable[index].FilterDevice = FilterDevice;
            *AssignedSlotIndex = index;
            status = STATUS_SUCCESS;
            break;
        }
    }

    WdfSpinLockRelease(OibSlotTableLock);

    return status;
}

ULONG
OibGetConfiguredKeyboardSlotCount(
    VOID
    )
{
#if defined(OIB_BUILD_KEYBOARD)
    return OibActiveSlotCount;
#elif defined(OIB_BUILD_MOUSE)
    return OIB_TOTAL_DEVICE_SLOT_COUNT - OibActiveSlotCount;
#endif
}

VOID
OibSlotRelease(
    _In_ ULONG SlotIndex
    )
{
    if (SlotIndex >= OibActiveSlotCount) {
        return;
    }

    WdfSpinLockAcquire(OibSlotTableLock);
    OibSlotTable[SlotIndex].FilterDevice = NULL;
    WdfSpinLockRelease(OibSlotTableLock);
}

NTSTATUS
OibSlotAcquireFilterDevice(
    _In_ ULONG SlotIndex,
    _Out_ WDFDEVICE *FilterDevice
    )
{
    WDFDEVICE device;

    if (SlotIndex >= OibActiveSlotCount) {
        *FilterDevice = NULL;
        return STATUS_INVALID_PARAMETER;
    }

    WdfSpinLockAcquire(OibSlotTableLock);

    device = OibSlotTable[SlotIndex].FilterDevice;
    if (device != NULL) {
        WdfObjectReference(device);
    }

    WdfSpinLockRelease(OibSlotTableLock);

    *FilterDevice = device;

    return (device != NULL) ? STATUS_SUCCESS : STATUS_NO_SUCH_DEVICE;
}

VOID
OibSlotReleaseFilterDeviceReference(
    _In_ WDFDEVICE FilterDevice
    )
{
    WdfObjectDereference(FilterDevice);
}

VOID
OibSlotAttachFileContext(
    _In_ ULONG SlotIndex,
    _In_ PLIST_ENTRY FileContextLinkage
    )
{
    if (SlotIndex >= OibActiveSlotCount) {
        return;
    }

    WdfSpinLockAcquire(OibSlotTable[SlotIndex].InstancesLock);
    InsertTailList(&OibSlotTable[SlotIndex].OpenInstances, FileContextLinkage);
    WdfSpinLockRelease(OibSlotTable[SlotIndex].InstancesLock);
}

VOID
OibSlotDetachFileContext(
    _In_ ULONG SlotIndex,
    _In_ PLIST_ENTRY FileContextLinkage
    )
{
    if (SlotIndex >= OibActiveSlotCount) {
        return;
    }

    WdfSpinLockAcquire(OibSlotTable[SlotIndex].InstancesLock);
    RemoveEntryList(FileContextLinkage);
    WdfSpinLockRelease(OibSlotTable[SlotIndex].InstancesLock);
}

VOID
OibSlotLockInstances(
    _In_ ULONG SlotIndex
    )
{
    NT_ASSERT(SlotIndex < OibActiveSlotCount);
    WdfSpinLockAcquire(OibSlotTable[SlotIndex].InstancesLock);
}

VOID
OibSlotUnlockInstances(
    _In_ ULONG SlotIndex
    )
{
    NT_ASSERT(SlotIndex < OibActiveSlotCount);
    WdfSpinLockRelease(OibSlotTable[SlotIndex].InstancesLock);
}

PLIST_ENTRY
OibSlotGetInstancesListHead(
    _In_ ULONG SlotIndex
    )
{
    NT_ASSERT(SlotIndex < OibActiveSlotCount);
    return &OibSlotTable[SlotIndex].OpenInstances;
}
