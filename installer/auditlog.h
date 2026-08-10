// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Optional audit-logging feature: makes Windows record (via the standard Security event log,
// event IDs 4656/4663) which process opens which \\.\interceptionNN control device and when.
// This is entirely a user-mode/OS-configuration feature — driver/ is not touched at all.
//
// Mechanism (see docs/DECISIONS.md's 2026-08-08 and 2026-08-09 entries for the full rationale,
// the alternatives considered and rejected, and a real bug found via actual-machine testing):
//   1. A SACL (audit ACE) is added to each currently-present \\.\interceptionNN device via
//      SetNamedSecurityInfoW, and the "Kernel Object", "File System", and "Handle Manipulation"
//      audit subcategories are all turned on via auditpol.exe, addressed by subcategory GUID
//      rather than English display name — auditpol matches against the *localized* name, so the
//      English string silently fails to match (and the whole call fails) on a non-English
//      Windows install. All three subcategories turned out to be needed for a genuine
//      Interception-protocol client's plain CreateFileA(GENERIC_READ) open of a *control*
//      device (as opposed to a regular NTFS file, where "File System" alone was enough in the
//      same real-machine testing) to actually produce 4656/4663 — see docs/DECISIONS.md's
//      2026-08-10 entry for how each was isolated. A SACL alone is not enough if the relevant
//      subcategories are off system-wide, and vice versa.
//   2. The SACL lives on the device object, not on anything persisted to disk, so it is lost
//      every time the control devices are re-created — i.e. every time the OpenInputBridge
//      services (re)start, which for these SERVICE_SYSTEM_START drivers only reliably happens at
//      boot. A Scheduled Task (created here via schtasks.exe, not a persistent service) reapplies
//      it automatically on every boot (a BootTrigger, not an event trigger on the service's SCM
//      "started" event — that event turned out to never fire for SERVICE_SYSTEM_START drivers,
//      since the I/O Manager loads them directly during kernel init rather than the SCM starting
//      them the way it does ordinary Win32 services; see auditlog.cpp's BuildReapplyTaskXml) by
//      re-invoking this same executable with --apply-audit-sacl.

#pragma once

namespace OpenInputBridge {

// Adds the audit SACL to all currently-present control devices, turns on the "Kernel Object",
// "File System", and "Handle Manipulation" audit subcategories, and registers the
// reapply-on-service-start Scheduled Task. Requires elevation (checked internally, same as
// RunInstall/RunUninstall).
int RunEnableAuditLog();

// Removes the SACL from all currently-present control devices and unregisters the reapply
// task. Deliberately does NOT turn the "Kernel Object"/"File System"/"Handle Manipulation"
// audit subcategories back off: those settings are machine-wide (auditpol has no per-object
// scope), so touching them here could silently disable auditing another feature/administrator
// relies on.
int RunDisableAuditLog();

// Internal: re-applies the SACL to all currently-present control devices only (no auditpol,
// no task (re)registration). This is the command the reapply Scheduled Task itself invokes,
// running as SYSTEM, each time an OpenInputBridge service starts.
int RunApplyAuditSacl();

// Diagnostic (--dump-audit-sacl): reads back and prints whatever SACL is actually live on each
// control device right now, independent of what this installer last set — for troubleshooting
// audit events not appearing as expected. See docs/DECISIONS.md's 2026-08-10 entry.
int RunDumpAuditSacl();

} // namespace OpenInputBridge
