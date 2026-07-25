// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Uninstaller: reverses install.cpp — removes the service from both device classes'
// UpperFilters (leaving other entries intact) and marks the service itself for deletion. Also
// requires a reboot to take effect, for the same reason installation does (see common.h).

#include "common.h"

#include <cstdio>

namespace OpenInputBridge {

namespace {

bool RemoveDriverService()
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (scm == nullptr) {
        wprintf(L"[ERROR] OpenSCManager failed: %lu\n", GetLastError());
        return false;
    }

    SC_HANDLE service = OpenServiceW(scm, ServiceName, SERVICE_ALL_ACCESS);
    if (service == nullptr) {
        DWORD error = GetLastError();
        CloseServiceHandle(scm);

        if (error == ERROR_SERVICE_DOES_NOT_EXIST) {
            return true; // Nothing to remove.
        }

        wprintf(L"[ERROR] OpenService failed: %lu\n", error);
        return false;
    }

    // Marks the service for deletion. This is a boot-start driver almost certainly still
    // attached to live keyboard/mouse device stacks, so it can't be unloaded on the spot —
    // the actual removal happens once it's no longer running, i.e. after the reboot this
    // function's caller prompts for.
    BOOL deleted = DeleteService(service);
    DWORD deleteError = deleted ? ERROR_SUCCESS : GetLastError();

    CloseServiceHandle(service);
    CloseServiceHandle(scm);

    if (!deleted && deleteError != ERROR_SERVICE_MARKED_FOR_DELETE) {
        wprintf(L"[ERROR] DeleteService failed: %lu\n", deleteError);
        return false;
    }

    return true;
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

    if (!RemoveDriverService()) {
        return 1;
    }
    wprintf(L"Removed service '%s'.\n", ServiceName);

    wprintf(
        L"\nUninstallation complete. A REBOOT is required to fully unload %s and rebuild the "
        L"device filter chains without it.\n",
        ServiceName
        );

    return 0;
}

} // namespace OpenInputBridge
