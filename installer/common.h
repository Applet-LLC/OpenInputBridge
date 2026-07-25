// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Shared constants and helpers used by both install.cpp and uninstall.cpp.
//
// Driver package installation (staging OpenInputBridge.sys into the driver store, creating
// the SERVICE_KERNEL_DRIVER/SERVICE_SYSTEM_START service) is done via DiInstallDriver /
// DiUninstallDriver against driver/OpenInputBridge.inf — see
// https://learn.microsoft.com/windows-hardware/drivers/develop/creating-a-primitive-driver.
// Class-level UpperFilters registration is deliberately NOT part of that INF (InfVerif's
// DCH-compliance rule for primitive drivers forbids writing to a registry path outside the
// driver's own HKR-relative scope, and "affects every keyboard/mouse system-wide" is exactly
// that) — so it's done here instead, as a plain registry-API call alongside (not instead of)
// the DiInstallDriver/DiUninstallDriver calls. See driver/OpenInputBridge.inx's header comment
// for the full reasoning.

#pragma once

#include <windows.h>

namespace OpenInputBridge {

// Service name / driver package file names. Must match driver/OpenInputBridge.vcxproj's
// TargetName and driver/OpenInputBridge.inx.
inline constexpr wchar_t ServiceName[] = L"OpenInputBridge";
inline constexpr wchar_t InfFileName[] = L"OpenInputBridge.inf";

// Device setup class registry paths (relative to HKEY_LOCAL_MACHINE). GUIDs from
// docs/PROTOCOL.md / driver/OpenInputBridge.inx.
inline constexpr wchar_t KeyboardClassRegistryPath[] =
    L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E96B-E325-11CE-BFC1-08002BE10318}";
inline constexpr wchar_t MouseClassRegistryPath[] =
    L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E96F-E325-11CE-BFC1-08002BE10318}";

inline constexpr wchar_t UpperFiltersValueName[] = L"UpperFilters";

// True if the current process token is elevated. DiInstallDriver/DiUninstallDriver and
// editing HKLM\SYSTEM both require this.
bool IsRunningElevated();

// Appends entryName to the UpperFilters REG_MULTI_SZ value under
// HKLM\<classRegistryPath>, creating the value if it doesn't exist yet, and leaving any other
// entries already present (e.g. other legitimately installed filters) untouched. A no-op
// (returns true) if entryName is already present.
bool AppendUpperFilter(const wchar_t* classRegistryPath, const wchar_t* entryName);

// Removes entryName from the UpperFilters REG_MULTI_SZ value under
// HKLM\<classRegistryPath> if present, leaving any other entries untouched. A no-op (returns
// true) if the value or key doesn't exist, or doesn't contain entryName.
bool RemoveUpperFilter(const wchar_t* classRegistryPath, const wchar_t* entryName);

// Entry points, implemented in install.cpp / uninstall.cpp, called from main.cpp.
int RunInstall();
int RunUninstall();

} // namespace OpenInputBridge
