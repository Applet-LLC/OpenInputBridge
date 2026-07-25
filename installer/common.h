// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Shared constants and registry/service helpers used by both install.cpp and uninstall.cpp.
// See the project plan's "4. インストール方式" section: this driver is registered as a
// class-level upper filter (not matched via INF hardware IDs), so installation is this small
// installer's job — the .inx in /driver exists only for signing/cataloging.

#pragma once

#include <windows.h>

namespace OpenInputBridge {

// Service name (also used as the driver's display name) and the driver binary's file name.
// Must match driver/OpenInputBridge.vcxproj's TargetName.
inline constexpr wchar_t ServiceName[] = L"OpenInputBridge";
inline constexpr wchar_t DriverFileName[] = L"OpenInputBridge.sys";

// Device setup class registry paths (relative to HKEY_LOCAL_MACHINE). GUIDs from
// docs/PROTOCOL.md / driver/OpenInputBridge.inx.
inline constexpr wchar_t KeyboardClassRegistryPath[] =
    L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E96B-E325-11CE-BFC1-08002BE10318}";
inline constexpr wchar_t MouseClassRegistryPath[] =
    L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E96F-E325-11CE-BFC1-08002BE10318}";

inline constexpr wchar_t UpperFiltersValueName[] = L"UpperFilters";

// True if the current process token is elevated. Installing/removing a kernel driver service
// and editing HKLM\SYSTEM requires this.
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
