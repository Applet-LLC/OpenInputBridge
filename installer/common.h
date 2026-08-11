// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Shared constants and helpers used by both install.cpp and uninstall.cpp.
//
// Driver package installation follows the same two-step pattern as the sibling Applet LLC
// "nodoka_subscribe" project's DriverManager tool (addid/DriverManager/DriverManager.cpp,
// proven in production for its own kbdaddid/mouaddid upper filters — the same architecture as
// OpenInputBridge post-split, hooking IOCTL_INTERNAL_*_CONNECT the same way):
//   1. DiInstallDriverW stages the driver package (<exeDir>\<PackageName>\<PackageName>.inf/
//      .cat/.sys) into the Driver Store.
//   2. SetupInstallServicesFromInfSectionW is called separately, against the *staged* copy of
//      the INF in the Driver Store, to actually run [DefaultInstall.NTamd64.Services] and
//      create the SERVICE_KERNEL_DRIVER/SERVICE_SYSTEM_START service.
// Calling DiInstallDriverW alone is NOT sufficient: without a [Manufacturer]/[Models] device
// match (this is a primitive driver — see driver/keyboard/oib_kbd.inx and
// driver/mouse/oib_mou.inx), its own device install phase never runs, so step 2 has to be done
// explicitly.
//
// Class-level UpperFilters registration is deliberately NOT part of either INF (InfVerif's
// DCH-compliance rule for primitive drivers forbids writing to a registry path outside the
// driver's own HKR-relative scope, and "affects every keyboard/mouse system-wide" is exactly
// that) — so it's done here instead, as a plain registry-API call, same as kbdaddid/mouaddid's
// own DriverManager.cpp. Each driver must be positioned immediately BEFORE its class driver
// (kbdclass/mouclass) in that list (not merely present in it): it intercepts
// IOCTL_INTERNAL_KEYBOARD_CONNECT/IOCTL_INTERNAL_MOUSE_CONNECT, which the class driver sends
// down the stack, so it has to sit below the class driver to see it — see Microsoft's
// kbfiltr.inx sample comment this technique is based on, and docs/CLEAN_ROOM.md/
// docs/PROTOCOL.md.
//
// See docs/DECISIONS.md's 2026-07-30 entry for why OpenInputBridge, originally one binary
// registered under both the Keyboard and Mouse classes with Class=System in its INF, was split
// into this pair of independent driver packages (oib_kbd.sys/Class=Keyboard,
// oib_mou.sys/Class=Mouse) instead. Package base names are "oib_kbd"/"oib_mou" rather than the
// more obvious "keyboard"/"mouse" specifically to avoid colliding with the inbox
// keyboard.inf/mouse.inf every Windows install already carries in the Driver Store — see
// oib_kbd.inx's header comment and docs/DECISIONS.md's 2026-08-02 entry (second one).

#pragma once

#include <windows.h>

#include <optional>
#include <string>

namespace OpenInputBridge {

enum class DriverType {
    Keyboard,
    Mouse,
};

struct DriverInfo {
    // Package/base file name: matches the resulting <PackageName>.sys/.inf/.cat and the
    // <PackageName>.{vcxproj,inx} they're built from (which live under driver/keyboard/ and
    // driver/mouse/ respectively — those folder names don't need to match PackageName, only
    // the files inside do). Also the staged package's subfolder name next to this installer
    // executable (<exeDir>\<PackageName>\<PackageName>.inf), matching the kbdaddid/mouaddid
    // DriverManager.cpp convention.
    const wchar_t* PackageName;

    // SCM service name. Deliberately distinct from PackageName (unlike kbdaddid/mouaddid, which
    // use the same string for both) so it reads unambiguously as belonging to OpenInputBridge
    // in services.msc/sc.exe output.
    const wchar_t* ServiceName;

    // Device setup class GUID this driver registers as an upper filter under, and the class
    // driver it must sit immediately before in that class's UpperFilters list.
    const wchar_t* ClassGuidString;
    const wchar_t* InsertBeforeClassDriver;
};

const DriverInfo& GetDriverInfo(DriverType type);

// Keyboard/mouse slot split (docs/DECISIONS.md's 2026-08-02 entry). Must match
// driver/common/driver.h's OIB_KEYBOARD_SLOT_COUNT_VALUE_NAME / OIB_TOTAL_DEVICE_SLOT_COUNT /
// OIB_DEFAULT_KEYBOARD_SLOT_COUNT exactly — the driver and installer can't share a header
// (kernel-mode driver.h isn't includable from this user-mode executable), so these are
// deliberately kept in sync as separate, commented definitions on both sides.
inline constexpr wchar_t KeyboardSlotCountValueName[] = L"KeyboardSlotCount";
inline constexpr ULONG TotalDeviceSlotCount = 20;
inline constexpr ULONG DefaultKeyboardSlotCount = 10;

// True if the current process token is elevated. Installing/removing a driver service and
// editing HKLM\SYSTEM both require this.
bool IsRunningElevated();

// True if this machine's *native* OS (not this process's own architecture, which matters since
// an x64 process can run under emulation on ARM64 — GetNativeSystemInfo reports the real one)
// is x64, and its build number is at least 18362 (Windows 10 version 1903, "May 2019 Update").
// This is the actual technical floor main.cpp's version gate enforces; see kUnsupportedEnvironmentMessage
// there for why the message shown to users names Windows 11 (the officially supported target)
// instead of citing 1903 — this floor is deliberately kept a bit below that as headroom, not as
// a line customers should read as "supported down to here". A kernel driver for a Windows
// version/architecture combination it was never built or tested against isn't just "maybe
// works" — it can BSOD or silently corrupt input handling, so this is checked before any
// install action proceeds (main.cpp's --skip-version-check exists for the Pro/Subscription
// installers' own use, in case their own MSI's CustomAction sequence needs to bypass this).
bool IsSupportedWindowsEnvironment();

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

// True if entryName currently appears in classGuidString's class-level UpperFilters list (the
// per-*instance* overrides ModifyUpperFilters also maintains are not consulted here — the
// class-level list is the primary, always-present indicator "did this installer register
// itself here" cares about). False if the class key or the value itself doesn't exist.
bool IsRegisteredAsUpperFilter(const wchar_t* classGuidString, const wchar_t* entryName);

// Writes KeyboardSlotCountValueName (REG_DWORD) under type's own service \Parameters key
// (created if the Parameters subkey doesn't exist yet — the service key itself must already
// exist, i.e. this is only called once DiInstallDriverW/SetupInstallServicesFromInfSectionW
// has already run for that service). Returns false only on an actual registry failure.
bool SetKeyboardSlotCount(DriverType type, ULONG keyboardSlotCount);

// Reads KeyboardSlotCountValueName from type's own service \Parameters key. Returns false
// (and leaves outKeyboardSlotCount untouched) if the service, its Parameters key, or the value
// itself doesn't exist — callers should treat that the same as "not configured yet".
bool TryGetKeyboardSlotCount(DriverType type, ULONG& outKeyboardSlotCount);

// Entry points, implemented in install.cpp / uninstall.cpp, called from main.cpp — one driver
// type per call. requestedSlots, if present, is the number of slots (0..TotalDeviceSlotCount)
// the caller explicitly asked for *this* driver type to have (main.cpp's --slots); RunInstall
// derives the shared KeyboardSlotCount value from it (see install.cpp) and keeps both
// services' registrations in sync.
int RunInstall(DriverType type, std::optional<ULONG> requestedSlots = std::nullopt);
int RunUninstall(DriverType type);

// Full path (as returned by GetModuleFileNameW) of the currently-running executable. Used by
// auditlog.cpp/toastsetup.cpp to point Scheduled Task actions back at this same
// OpenInputBridgeSetup.exe (for the audit-SACL reapply task) or at its sibling
// OibToastHelper.exe (for the toast task).
std::wstring GetInstallerExecutablePath();

// Runs a well-known tool from %windir%\System32 by absolute path (deliberately never via PATH
// search, since callers run elevated) and waits for it to exit. Returns the process's exit
// code, or -1 if the process couldn't be started at all. Shared by auditlog.cpp (auditpol.exe,
// schtasks.exe) and toastsetup.cpp (schtasks.exe). suppressOutput redirects the child's
// stdout/stderr/stdin to NUL — for a call like ScheduledTaskExists's below, where a "not found"
// result is an expected, routine outcome, not something schtasks's own console error text
// should narrate.
int RunSystem32Tool(const wchar_t* exeName, const std::wstring& arguments, bool suppressOutput = false);

// Writes taskXml to a temporary file (UTF-16LE with BOM, as schtasks.exe /XML requires) and
// registers/overwrites it as taskName via `schtasks /Create ... /F`. The temp file is removed
// afterward regardless of outcome. Returns false only if schtasks itself reports failure.
bool RegisterScheduledTaskFromXml(const wchar_t* taskName, const std::wstring& taskXml);

// Removes taskName via `schtasks /Delete ... /F`. Idempotent: does not report failure just
// because taskName wasn't registered to begin with.
void UnregisterScheduledTask(const wchar_t* taskName);

// True if taskName is currently registered with Task Scheduler. verify.cpp uses this against
// auditlog.h's AuditLogReapplyTaskName / toastsetup.h's ToastNotifyTaskName as a proxy for
// "is the audit-log/toast-notification feature enabled" — a registered reapply/notify task is
// exactly the artifact RunEnableAuditLog/RunEnableToast create and RunDisableAuditLog/
// RunDisableToast remove, so it's a reliable signal without duplicating either feature's logic.
bool ScheduledTaskExists(const wchar_t* taskName);

} // namespace OpenInputBridge
