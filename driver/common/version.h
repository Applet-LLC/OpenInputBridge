// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Single source of truth for the OpenInputBridge driver version, shared between:
//   - oib_kbd.rc / oib_mou.rc (VS_VERSION_INFO — what Device Manager shows)
//   - ioctl.c's IOCTL_GET_DRIVER_IDENTITY handler (docs/PROTOCOL.md)
// Deliberately has no other dependencies (no ntddk.h/wdf.h) so the resource compiler can
// #include it directly without pulling in kernel-mode headers it was never meant to process.

#pragma once

#define OIB_VERSION_MAJOR  1
#define OIB_VERSION_MINOR  0
#define OIB_VERSION_STRING "1.00"
