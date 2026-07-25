// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// See slots.h. Implementation lands in M2. This is the piece of the design with no direct
// Microsoft sample precedent (see project plan, architecture note "3. 常時20個制約への対応").

#include "slots.h"

// TODO(M2):
//   - Global slot table: OIB_DEVICE_SLOT_COUNT entries, each holding either NULL (unassigned)
//     or a pointer to the currently-attached filter FDO's device context.
//   - Guarded by a single WDFSPINLOCK (PnP arrival/removal on one thread, control-device I/O
//     on another).
//   - OibSlotAssign(isKeyboard, fdoContext): first-fit into 0..OIB_KEYBOARD_SLOT_COUNT-1 for
//     keyboards or OIB_KEYBOARD_SLOT_COUNT..OIB_DEVICE_SLOT_COUNT-1 for mice; returns
//     STATUS_DEVICE_NOT_READY (or similar) if no free slot (>10 of a kind attached).
//   - OibSlotRelease(slotIndex): clears the assignment on filter FDO removal; the control
//     device itself is NOT destroyed/unlinked, only its association with a physical device.
//   - OibSlotGetFdoContext(slotIndex): NULL if unassigned (control-device handlers must treat
//     this as "succeed with inert/empty results", not as an error — see docs/PROTOCOL.md).
