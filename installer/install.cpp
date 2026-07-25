// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Installer: stages the driver package (drivers\OpenInputBridge.inf/.cat/.sys, expected
// alongside this executable) into the driver store and registers the
// SERVICE_KERNEL_DRIVER/SERVICE_SYSTEM_START service via DiInstallDriver, then separately
// registers OpenInputBridge as an upper filter for both the Keyboard and Mouse device setup
// classes (see common.h for why that second step can't be part of the INF). See
// driver/OpenInputBridge.inx and the project plan's "4. インストール方式" section.

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

int RunInstall()
{
    if (!IsRunningElevated()) {
        wprintf(L"[ERROR] This installer must be run as Administrator.\n");
        return 1;
    }

    std::filesystem::path infPath = GetModuleDirectory() / L"drivers" / InfFileName;

    if (!std::filesystem::exists(infPath)) {
        wprintf(L"[ERROR] Driver package not found: %s\n", infPath.c_str());
        return 1;
    }

    BOOL needReboot = FALSE;

    if (!DiInstallDriverW(nullptr, infPath.c_str(), 0, &needReboot)) {
        wprintf(L"[ERROR] DiInstallDriver failed: %lu\n", GetLastError());
        return 1;
    }
    wprintf(L"Installed the %s driver package (service registered, SERVICE_SYSTEM_START).\n", ServiceName);

    bool filtersOk = AppendUpperFilter(KeyboardClassRegistryPath, ServiceName);
    filtersOk = AppendUpperFilter(MouseClassRegistryPath, ServiceName) && filtersOk;

    if (!filtersOk) {
        wprintf(
            L"[ERROR] Failed to register '%s' as an upper filter for the Keyboard/Mouse "
            L"device classes.\n",
            ServiceName
            );
        return 1;
    }
    wprintf(L"Registered '%s' as an upper filter for the Keyboard and Mouse device classes.\n", ServiceName);

    wprintf(
        L"\nInstallation complete. A REBOOT is required before %s takes effect: Windows only "
        L"rebuilds device filter chains (UpperFilters) at boot / device stack (re)construction "
        L"time, not immediately.%s\n",
        ServiceName,
        needReboot ? L" (DiInstallDriver also reported a reboot is needed.)" : L""
        );

    return 0;
}

} // namespace OpenInputBridge
