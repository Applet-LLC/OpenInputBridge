// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// M6: installer entry point. Responsibilities (see project plan, "4. インストール方式"):
//   1. CreateService(SC_MANAGER, SERVICE_KERNEL_DRIVER, ...) for OpenInputBridge.sys with
//      SERVICE_SYSTEM_START (keyboard/mouse stacks build early in boot; demand-start risks
//      missing the initial stack build).
//   2. For both the Keyboard class GUID (4d36e96b-e325-11ce-bfc1-08002be10318) and the Mouse
//      class GUID (4d36e96f-e325-11ce-bfc1-08002be10318), open
//      HKLM\SYSTEM\CurrentControlSet\Control\Class\{GUID}, read the existing UpperFilters
//      REG_MULTI_SZ value, append "OpenInputBridge" if not already present, write it back.
//      Must not clobber existing entries (e.g. other legitimately installed filters).
//   3. Inform the user a reboot is required — Windows does not rebuild UpperFilters chains
//      for already-enumerated devices without a restart.
//
// See uninstall.cpp for the reverse operation.

#include <windows.h>
#include <cstdio>

int wmain(int argc, wchar_t* argv[])
{
    (void)argc;
    (void)argv;

    // TODO(M6): implement steps 1-3 above.
    wprintf(L"OpenInputBridge installer: not yet implemented.\n");

    return 1;
}
