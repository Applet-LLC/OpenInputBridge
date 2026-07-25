// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// The 8 control-device IOCTLs. Codes and semantics are defined in docs/PROTOCOL.md, derived
// from reading (unmodified, LGPL) third_party/interception/library/interception.c — see
// docs/CLEAN_ROOM.md for what was and wasn't consulted to arrive at these definitions.

#pragma once

#include "driver.h"
#include "slots.h"

#define IOCTL_SET_PRECEDENCE  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_PRECEDENCE  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SET_FILTER      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_FILTER      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x808, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SET_EVENT       CTL_CODE(FILE_DEVICE_UNKNOWN, 0x810, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_WRITE           CTL_CODE(FILE_DEVICE_UNKNOWN, 0x820, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_READ            CTL_CODE(FILE_DEVICE_UNKNOWN, 0x840, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_HARDWARE_ID CTL_CODE(FILE_DEVICE_UNKNOWN, 0x880, METHOD_BUFFERED, FILE_ANY_ACCESS)

EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL OibCtlEvtIoDeviceControl;

// IOCTL_GET_HARDWARE_ID (M2): resolves the control device's assigned slot (if any) to its
// filter FDO's lower PDO and returns that PDO's hardware ID property, truncated to whatever
// fits in the caller's output buffer. Split out from OibCtlEvtIoDeviceControl's switch
// because it's the one IOCTL implemented so far with real (not STATUS_NOT_IMPLEMENTED) logic.
VOID OibCtlHandleGetHardwareId(_In_ WDFREQUEST Request, _In_ WDFDEVICE ControlDevice, _In_ size_t OutputBufferLength);

// TODO(M3/M4/M5): dispatch table, one handler per remaining IOCTL above:
//   IOCTL_SET_EVENT        -> ObReferenceObjectByHandle in caller context, store PKEVENT (M3)
//   IOCTL_READ             -> drain this slot's capture queue, non-blocking (M3)
//   IOCTL_WRITE             -> call saved ClassService for this slot's FDO (M4)
//   IOCTL_SET/GET_FILTER    -> per-file-object filter bitmask (M3)
//   IOCTL_SET/GET_PRECEDENCE -> per-file-object precedence; ordering policy pending
//                               black-box validation against the real driver (M5)
// All operate against the slot looked up via the control device's own context (SlotIndex,
// set at creation time — see driver.c) through the slot table in slots.c. An empty slot
// (no physical device currently assigned) must not fail these calls — see docs/PROTOCOL.md.
