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

// TODO(M2): slot table types and accessors:
//   - OibSlotAssign(deviceType, filterFdoContext) -> slot index or failure if no free slot
//   - OibSlotRelease(slotIndex)
//   - OibSlotGetFdoContext(slotIndex) -> NULL if unassigned
// Guarded by a WDFSPINLOCK/WDFWAITLOCK since PnP arrival/removal and control-device I/O
// run on different threads concurrently.
