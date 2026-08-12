// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// See verify.h.

#include "verify.h"
#include "common.h"
#include "auditlog.h"
#include "toastsetup.h"

#include <cstdio>

namespace OpenInputBridge {

int RunVerifyInstall()
{
    if (!IsRunningElevated()) {
        wprintf(L"[ERROR] This installer must be run as Administrator.\n");
        return 1;
    }

    bool ok = VerifyDriverFilterIntegrity(GetDriverInfo(DriverType::Keyboard));
    ok = VerifyDriverFilterIntegrity(GetDriverInfo(DriverType::Mouse)) && ok;

    bool auditLogEnabled = ScheduledTaskExists(AuditLogReapplyTaskName);
    bool toastEnabled = ScheduledTaskExists(ToastNotifyTaskName);

    if (!auditLogEnabled || !toastEnabled) {
        wprintf(
            L"\nNOTE: This device driver can be opened by any process, even one without "
            L"administrator privileges -- that's required for compatibility with the "
            L"Interception protocol (see docs/SECURITY_CONSIDERATIONS.md). Consider enabling "
            L"logging and/or toast notifications (--enable-audit-log / --enable-toast) so "
            L"you're aware of what's accessing it.\n"
            );
    }

    return ok ? 0 : 1;
}

} // namespace OpenInputBridge
