// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
//
// poolalloc.h: zero-initialised pool allocation helpers shared by every source file in this
// driver (driver.h, license.c, licensestate.c).

#pragma once

#include <ntddk.h>

// Zero-initialised pool allocation. The default build targets Windows 10 2004+ / Windows 11 and
// uses ExAllocatePool2. The Windows 10 compatibility build (OibWin10Compat=true in the vcxproj,
// which defines OIB_WIN10_COMPAT and POOL_ZERO_DOWN_LEVEL_SUPPORT and lowers KMDF to 1.19 for
// Windows 10 1607) cannot import ExAllocatePool2, which does not exist before 2004; there
// ExAllocatePoolZero resolves, via the WDK's inline wrapper, to ExAllocatePoolWithTagPriority
// and zeroes the buffer itself where the OS does not. DriverEntry must call
// ExInitializeDriverRuntime first in that build. Non-paged allocations use NonPagedPoolNx
// (NX, as ExAllocatePool2's POOL_FLAG_NON_PAGED does).
#ifdef OIB_WIN10_COMPAT
#define OibAllocPagedZero(Size, Tag)    ExAllocatePoolZero(PagedPool, (Size), (Tag))
#define OibAllocNonPagedZero(Size, Tag) ExAllocatePoolZero(NonPagedPoolNx, (Size), (Tag))
#else
#define OibAllocPagedZero(Size, Tag)    ExAllocatePool2(POOL_FLAG_PAGED, (Size), (Tag))
#define OibAllocNonPagedZero(Size, Tag) ExAllocatePool2(POOL_FLAG_NON_PAGED, (Size), (Tag))
#endif
