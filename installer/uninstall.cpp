// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Uninstaller: reverses install.cpp — removes OpenInputBridge from both device classes'
// UpperFilters first (leaving other entries intact), then removes the driver package (service
// + driver store entry) via DiUninstallDriver, which automatically reverses whatever
// DiInstallDriver did (see common.h and driver/OpenInputBridge.inx). Also requires a reboot to
// take effect, for the same reason installation does.

#include "common.h"

#include <newdev.h>

#include <filesystem>
#include <cstdio>

#pragma comment(lib, "Newdev.lib")

namespace OpenInputBridge {

namespace {

std::filesystem::path GetModuleDirectory()
{
    wchar_t modulePath[MAX_PATH];
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    return std::filesystem::path(modulePath).parent_path();
}

} // namespace

int RunUninstall()
{
    if (!IsRunningElevated()) {
        wprintf(L"[ERROR] This uninstaller must be run as Administrator.\n");
        return 1;
    }

    bool filtersOk = RemoveUpperFilter(KeyboardClassRegistryPath, ServiceName);
    filtersOk = RemoveUpperFilter(MouseClassRegistryPath, ServiceName) && filtersOk;

    if (!filtersOk) {
        wprintf(
            L"[ERROR] Failed to remove '%s' from the Keyboard/Mouse device classes' upper "
            L"filters.\n",
            ServiceName
            );
        return 1;
    }
    wprintf(L"Removed '%s' from the Keyboard and Mouse device classes' upper filters.\n", ServiceName);

    std::filesystem::path infPath = GetModuleDirectory() / L"drivers" / InfFileName;

    if (!std::filesystem::exists(infPath)) {
        wprintf(
            L"[WARNING] Driver package not found at %s — skipping DiUninstallDriver (the "
            L"UpperFilters entries above are already removed; the service, if still "
            L"registered, will need to be cleaned up separately).\n",
            infPath.c_str()
            );
        return 0;
    }

    BOOL needReboot = FALSE;

    if (!DiUninstallDriverW(nullptr, infPath.c_str(), 0, &needReboot)) {
        wprintf(L"[ERROR] DiUninstallDriver failed: %lu\n", GetLastError());
        return 1;
    }
    wprintf(L"Removed the %s driver package.\n", ServiceName);

    wprintf(
        L"\nUninstallation complete. A REBOOT is required to fully unload %s and rebuild the "
        L"device filter chains without it.%s\n",
        ServiceName,
        needReboot ? L" (DiUninstallDriver also reported a reboot is needed.)" : L""
        );

    return 0;
}

} // namespace OpenInputBridge
