// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Shared constants and helpers used by both install.cpp and uninstall.cpp.
//
// Driver package installation follows the same two-step pattern as the sibling Applet LLC
// "nodoka_subscribe" project's DriverManager tool (addid/DriverManager/DriverManager.cpp,
// proven in production for its own kbdaddid/mouaddid upper filters — the same architecture as
// OpenInputBridge, hooking IOCTL_INTERNAL_*_CONNECT the same way):
//   1. DiInstallDriverW stages the driver package (drivers\OpenInputBridge.inf/.cat/.sys) into
//      the Driver Store.
//   2. SetupInstallServicesFromInfSectionW is called separately, against the *staged* copy of
//      the INF in the Driver Store, to actually run [DefaultInstall.NTamd64.Services] and
//      create the SERVICE_KERNEL_DRIVER/SERVICE_SYSTEM_START service.
// Calling DiInstallDriverW alone is NOT sufficient: without a [Manufacturer]/[Models] device
// match (this is a primitive driver — see driver/OpenInputBridge.inx), its own device
// install phase never runs, so step 2 has to be done explicitly.
//
// Class-level UpperFilters registration is deliberately NOT part of the INF (InfVerif's
// DCH-compliance rule for primitive drivers forbids writing to a registry path outside the
// driver's own HKR-relative scope, and "affects every keyboard/mouse system-wide" is exactly
// that) — so it's done here instead, as a plain registry-API call. OpenInputBridge must be
// positioned immediately BEFORE kbdclass/mouclass in that list (not merely present in it): it
// intercepts IOCTL_INTERNAL_KEYBOARD_CONNECT/IOCTL_INTERNAL_MOUSE_CONNECT, which the class
// driver sends down the stack, so it has to sit below the class driver to see it — see
// Microsoft's kbfiltr.inx sample comment this technique is based on, and
// docs/CLEAN_ROOM.md/docs/PROTOCOL.md.

#pragma once

#include <windows.h>

namespace OpenInputBridge {

// Service name / driver package file names. Must match driver/OpenInputBridge.vcxproj's
// TargetName and driver/OpenInputBridge.inx.
inline constexpr wchar_t ServiceName[] = L"OpenInputBridge";
inline constexpr wchar_t InfFileName[] = L"OpenInputBridge.inf";

// Device setup class GUIDs, in the plain "{...}" string form used both for registry path
// construction and as SetupDiClassNameFromGuid-style identifiers. See docs/PROTOCOL.md.
inline constexpr wchar_t KeyboardClassGuidString[] = L"{4D36E96B-E325-11CE-BFC1-08002BE10318}";
inline constexpr wchar_t MouseClassGuidString[] = L"{4D36E96F-E325-11CE-BFC1-08002BE10318}";

// True if the current process token is elevated. Installing/removing a driver service and
// editing HKLM\SYSTEM both require this.
bool IsRunningElevated();

// True if a service named serviceName is currently registered with the SCM (regardless of
// its running state).
bool ServiceExists(const wchar_t* serviceName);

// Stops serviceName and waits (up to timeoutMs) for it to reach SERVICE_STOPPED. Returns true
// if the service ends up stopped, or didn't exist in the first place (nothing to stop).
bool StopAndWaitService(const wchar_t* serviceName, DWORD timeoutMs);

// Inserts (add=true) or removes (add=false) entryName in the UpperFilters REG_MULTI_SZ value,
// for classGuidString's class key AND every numbered device-instance subkey under it (0000,
// 0001, ...) — both need updating for the change to reliably reach already-enumerated
// devices, not just ones plugged in after this runs (a device's own instance key can carry a
// copy of UpperFilters that overrides the class-level default once the device has already
// been enumerated). When adding, entryName is positioned immediately before insertBeforeName
// (case-insensitive — e.g. "kbdclass"/"mouclass") in each key's list if present there, or at
// the very front of that key's list otherwise. Leaves any other entries in the list
// untouched, and removes any pre-existing occurrence of entryName first, so re-running this
// (e.g. after an earlier install that predates this position-aware logic) self-corrects
// rather than leaving a stale duplicate.
bool ModifyUpperFilters(const wchar_t* classGuidString, const wchar_t* entryName, bool add, const wchar_t* insertBeforeName);

// Entry points, implemented in install.cpp / uninstall.cpp, called from main.cpp.
int RunInstall();
int RunUninstall();

} // namespace OpenInputBridge
