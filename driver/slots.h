// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Global slot table mapping the OIB_DEVICE_SLOT_COUNT always-present control devices
// (\Device\interceptionNN) to the filter FDO currently attached to that physical
// keyboard/mouse, if any. Slots 0..OIB_KEYBOARD_SLOT_COUNT-1 are keyboards, the remainder
// are mice. This mapping has no direct precedent in Microsoft's kbfiltr/moufiltr samples
// (see project plan, "M2 / slots.c" — highest design-risk piece of this driver) because
// those samples assume one filter instance per one exposed device, whereas here the control
// devices must exist and stay openable independent of physical device presence.

#pragma once

#include "driver.h"

// Sentinel meaning "not assigned to any slot". Distinct from any real slot index
// (0..OIB_DEVICE_SLOT_COUNT-1), including 0 — a filter FDO's context field holding this must
// not be left at its zero-initialized default and mistaken for slot 0.
#define OIB_SLOT_INDEX_NONE ((ULONG)-1)

// Must be called once from DriverEntry, before PnP can start calling AddDevice or the control
// devices can start receiving I/O.
NTSTATUS OibSlotTableInitialize(_In_ WDFDRIVER Driver);

// Assigns FilterDevice to the first free slot of the requested kind (keyboard:
// 0..OIB_KEYBOARD_SLOT_COUNT-1, mouse: OIB_KEYBOARD_SLOT_COUNT..OIB_DEVICE_SLOT_COUNT-1) and
// returns it via *AssignedSlotIndex. If every slot of that kind is already taken (an 11th
// keyboard or mouse — upstream's own wire protocol hard-caps at 10 of each kind, see
// docs/PROTOCOL.md), returns STATUS_DEVICE_NOT_READY and sets *AssignedSlotIndex to
// OIB_SLOT_INDEX_NONE; callers should treat this as graceful degradation (the filter FDO
// still attaches and passes input through normally, it's just unreachable via any
// \\.\interceptionNN control device), not a reason to fail device creation.
//
// Does not take a reference on FilterDevice — the caller's own EvtCleanupCallback is expected
// to call OibSlotRelease (unconditionally, even with OIB_SLOT_INDEX_NONE — see below) before
// the device is destroyed.
NTSTATUS OibSlotAssign(_In_ BOOLEAN IsKeyboard, _In_ WDFDEVICE FilterDevice, _Out_ ULONG *AssignedSlotIndex);

// Clears SlotIndex's assignment. Safe (a no-op) when SlotIndex is OIB_SLOT_INDEX_NONE, so
// filter FDO cleanup callbacks can call this unconditionally without checking whether
// OibSlotAssign ever succeeded for this device.
VOID OibSlotRelease(_In_ ULONG SlotIndex);

// Looks up the filter FDO currently assigned to SlotIndex and, if one is assigned, returns it
// with an extra reference held (the caller must call OibSlotReleaseFilterDeviceReference when
// done with it). The reference is what keeps the device from being torn down by a concurrent
// PnP removal while a control-device IOCTL handler is still using it.
//
// Returns STATUS_NO_SUCH_DEVICE if the slot is unassigned (a normal, expected condition — no
// physical device currently in that slot — not an error to surface to the user-mode caller;
// see docs/PROTOCOL.md on why empty slots must not fail control-device IOCTLs) or
// STATUS_INVALID_PARAMETER if SlotIndex is out of range.
NTSTATUS OibSlotAcquireFilterDevice(_In_ ULONG SlotIndex, _Out_ WDFDEVICE *FilterDevice);

// Releases a reference acquired by OibSlotAcquireFilterDevice.
VOID OibSlotReleaseFilterDeviceReference(_In_ WDFDEVICE FilterDevice);

// --- Per-slot list of open \\.\interceptionNN handles (M3) ---
//
// Each slot also owns a list of the control-device file objects (WDFFILEOBJECT-backed
// "OIB_FILE_CONTEXT" records, see ioctl.h) currently open on it, plus a dedicated spinlock
// guarding that list AND every listed context's own filter/event/capture-queue state. One
// lock per slot (rather than one global lock) keeps stroke dispatch on a busy keyboard from
// contending with an unrelated mouse slot. slots.c intentionally stays agnostic of what a
// "file context" contains — it only threads LIST_ENTRY linkage — so the field definitions
// live in ioctl.h alongside the IOCTLs that operate on them.

// Links FileContextLinkage into SlotIndex's list of open instances. Called from
// ioctl.c's OibCtlEvtFileCreate.
VOID OibSlotAttachFileContext(_In_ ULONG SlotIndex, _In_ PLIST_ENTRY FileContextLinkage);

// Unlinks FileContextLinkage from SlotIndex's list. Called from ioctl.c's OibCtlEvtFileClose.
VOID OibSlotDetachFileContext(_In_ ULONG SlotIndex, _In_ PLIST_ENTRY FileContextLinkage);

// Acquires/releases SlotIndex's instances lock. Callers must hold this while reading or
// writing any OIB_FILE_CONTEXT belonging to this slot (Filter, UnemptyEvent, capture queue)
// or while walking the list returned by OibSlotGetInstancesListHead — including from
// DISPATCH_LEVEL (the keyboard/mouse service callback can run there), so anything touched
// while holding this lock must be non-paged.
VOID OibSlotLockInstances(_In_ ULONG SlotIndex);
VOID OibSlotUnlockInstances(_In_ ULONG SlotIndex);

// Returns SlotIndex's list head, for iteration (e.g. via the standard
// for (entry = head->Flink; entry != head; entry = entry->Flink) idiom) while the instances
// lock (above) is held.
PLIST_ENTRY OibSlotGetInstancesListHead(_In_ ULONG SlotIndex);
