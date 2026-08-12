// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Post-install self-check (--verify-install), meant to run once after driver install and the
// audit-log/toast setup steps are done — see packaging/setup.bat, which calls this as its final
// step.

#pragma once

namespace OpenInputBridge {

// Two independent checks, both non-fatal to each other (both always run; the return value only
// reflects the first one):
//
//   1. Driver filter integrity: for each driver type, common.h's VerifyDriverFilterIntegrity
//      confirms that if it's registered as an upper filter, its service's ImagePath actually
//      points at a file that exists on disk. A filter registered but missing on disk would make
//      the corresponding device class (keyboard or mouse) stop responding *entirely* after the
//      next reboot — Windows tries and fails to load a nonexistent filter driver ahead of
//      kbdclass/mouclass in the stack — so rather than leave that landmine in place, this
//      removes the filter registration itself and reports the corrective action taken. This can
//      only happen from an incomplete/interrupted install (DiInstallDriverW/
//      SetupInstallServicesFromInfSectionW succeeded, but the driver package that was staged
//      from is now gone somehow) — a normal install run start-to-finish never leaves this state.
//      uninstall.cpp's RunUninstallOne runs the same check as its own final step, for the same
//      reason.
//
//   2. Audit-log/toast-notification reminder: if either feature (auditlog.h/toastsetup.h) isn't
//      enabled, prints an informational note. This driver, like the real Interception driver it
//      is protocol-compatible with, must let any unprivileged process open its control devices
//      (see docs/SECURITY_CONSIDERATIONS.md) — worth surfacing to anyone who skipped enabling
//      visibility into that.
//
// Requires elevation (checked internally) only because check 1 can write to the registry
// (removing a bad filter entry); reading state for either check does not itself require it.
int RunVerifyInstall();

} // namespace OpenInputBridge
