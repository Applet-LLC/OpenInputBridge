// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Entry point: OpenInputBridgeSetup.exe installs by default, or uninstalls with /uninstall.

#include "common.h"

#include <cstdio>
#include <cwchar>

int wmain(int argc, wchar_t* argv[])
{
    bool uninstall = false;

    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"/uninstall") == 0 || _wcsicmp(argv[i], L"-uninstall") == 0) {
            uninstall = true;
        } else if (_wcsicmp(argv[i], L"/?") == 0 || _wcsicmp(argv[i], L"-help") == 0 || _wcsicmp(argv[i], L"/help") == 0) {
            wprintf(L"Usage: OpenInputBridgeSetup.exe [/uninstall]\n");
            return 0;
        } else {
            wprintf(L"[ERROR] Unrecognized argument: %s\n", argv[i]);
            wprintf(L"Usage: OpenInputBridgeSetup.exe [/uninstall]\n");
            return 1;
        }
    }

    return uninstall ? OpenInputBridge::RunUninstall() : OpenInputBridge::RunInstall();
}
