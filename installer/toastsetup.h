// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Optional toast-notification feature: pops a Windows toast, labeled "OpenInputBridge", telling
// the logged-on user when a process just opened a \\.\interceptionNN control device. Depends on
// the audit-log feature (auditlog.h) already being enabled — this feature only registers a
// Scheduled Task that reacts to the 4656 events that feature's SACL causes Windows to emit; it
// does not set up any auditing itself.
//
// Mechanism (see docs/DECISIONS.md's 2026-08-08 and 2026-08-09 entries for the full rationale
// and the alternatives considered and rejected, including why this is a per-user Scheduled Task
// and not a LocalSystem service, and why the Start Menu shortcut is created here in C++ via
// IShellLink rather than by a WiX installer):
//   1. A dedicated AUMID ("OpenInputBridge.AuditNotifier") is registered under
//      HKLM\SOFTWARE\Classes\AppUserModelId so toasts shown under it are labeled "OpenInputBridge"
//      for every user on the machine, not just whichever account this installer runs as. See
//      installer/toast-helper/main.cpp for the process-side half of this
//      (SetCurrentProcessExplicitAppUserModelID) and toastsetup.cpp's CreateStartMenuShortcut for
//      the Start Menu shortcut Windows also expects to find for a non-packaged toast-capable app.
//   2. A Scheduled Task is registered with a custom event trigger on Security-log event ID
//      4656 (any object — the exact \\.\interceptionNN device is checked by
//      OibToastHelper.exe itself via its --object-name argument, not by the trigger's XPath; see
//      toastsetup.cpp's BuildToastTaskXml for why), running in whichever interactive user's
//      session is currently logged on (Principal GroupId = BUILTIN\Users), which launches
//      OibToastHelper.exe with the triggering event's ObjectName/ProcessId/SubjectUserName.
//   3. Clicking a toast's body reveals the accessing process's binary in Explorer (selected,
//      via "explorer.exe /select,<path>"). This works through a custom "oib-reveal:" URI
//      protocol (RegisterRevealProtocol) rather than full COM toast activation
//      (INotificationActivationCallback) — protocol activation needs no persistent listener or
//      COM server registration, just a registry command Windows invokes when the toast (built
//      with activationType="protocol") is clicked. See docs/DECISIONS.md's 2026-08-11 entry.

#pragma once

#include <string>

namespace OpenInputBridge {

// Must match installer/toast-helper/main.cpp's own copy of this string exactly (that project
// can't include this header — see common.h's KeyboardSlotCountValueName comment for why this
// installer and other same-repo-but-separately-built executables keep such constants in sync as
// separate, commented definitions rather than sharing a header).
inline constexpr wchar_t ToastAppUserModelId[] = L"OpenInputBridge.AuditNotifier";

// Name of the toast-triggering Scheduled Task. Exposed publicly (see AuditLogReapplyTaskName's
// comment in auditlog.h for the same reasoning) so verify.cpp can check whether it's registered
// as a proxy for "is the toast-notification feature enabled".
inline constexpr wchar_t ToastNotifyTaskName[] = L"OpenInputBridgeToastNotify";

// Registers the AUMID and the toast-triggering Scheduled Task. Requires elevation and requires
// OibToastHelper.exe to already be present next to this executable.
int RunEnableToast();

// Unregisters the Scheduled Task and the AUMID.
int RunDisableToast();

// Toast-suppression allowlist (--allow-process/--disallow-process/--list-allowed-processes):
// full executable paths of processes that should NOT produce a toast when they open a
// \\.\interceptionNN control device, for legitimate Interception-protocol clients the user
// already trusts and doesn't want repeated alerts about. Stored in the registry (HKLM, so it
// applies regardless of which user's session the toast task runs in) rather than a plain file,
// matching this feature's other machine-wide settings. Full-path matching (not filename-only)
// was chosen deliberately: this list only suppresses a notification, not the access itself or
// its Security-event-log record (every open is still audited exactly as before, allowlisted or
// not — see installer/toast-helper/main.cpp's IsProcessAllowlisted), so it isn't a security
// boundary, but full paths still avoid one obviously-avoidable false negative (a differently
// located exe that happens to share a trusted one's filename). See docs/DECISIONS.md's
// 2026-08-10 entry. All three require elevation.
int RunAllowProcess(const std::wstring& fullPath);
int RunDisallowProcess(const std::wstring& fullPath);
int RunListAllowedProcesses();

} // namespace OpenInputBridge
