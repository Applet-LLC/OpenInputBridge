// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// See slots.h. A single spinlock-protected array is enough here: assignment/release only
// happens on PnP arrival/removal (infrequent), and lookups (OibSlotAcquireFilterDevice) hold
// the lock only long enough to copy a handle and take a reference, never while touching the
// filter device itself.

#include "slots.h"

typedef struct _OIB_SLOT_ENTRY
{
    WDFDEVICE FilterDevice; // NULL if unassigned.
} OIB_SLOT_ENTRY;

static OIB_SLOT_ENTRY OibSlotTable[OIB_DEVICE_SLOT_COUNT];
static WDFSPINLOCK OibSlotTableLock;

NTSTATUS
OibSlotTableInitialize(
    _In_ WDFDRIVER Driver
    )
{
    WDF_OBJECT_ATTRIBUTES attributes;

    RtlZeroMemory(OibSlotTable, sizeof(OibSlotTable));

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Driver;

    return WdfSpinLockCreate(&attributes, &OibSlotTableLock);
}

NTSTATUS
OibSlotAssign(
    _In_ BOOLEAN IsKeyboard,
    _In_ WDFDEVICE FilterDevice,
    _Out_ ULONG *AssignedSlotIndex
    )
{
    ULONG start = IsKeyboard ? 0 : OIB_KEYBOARD_SLOT_COUNT;
    ULONG end = IsKeyboard ? OIB_KEYBOARD_SLOT_COUNT : OIB_DEVICE_SLOT_COUNT;
    ULONG index;
    NTSTATUS status = STATUS_DEVICE_NOT_READY;

    *AssignedSlotIndex = OIB_SLOT_INDEX_NONE;

    WdfSpinLockAcquire(OibSlotTableLock);

    for (index = start; index < end; index++) {
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

VOID
OibSlotRelease(
    _In_ ULONG SlotIndex
    )
{
    if (SlotIndex >= OIB_DEVICE_SLOT_COUNT) {
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

    if (SlotIndex >= OIB_DEVICE_SLOT_COUNT) {
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
