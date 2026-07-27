// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Uninstaller: reverses install.cpp, in the order that's safest against leaving the system in
// a half-removed state if interrupted partway through:
//   1. Stop the running service (nothing should still be attached to it after this).
//   2. Remove OpenInputBridge from both device classes' UpperFilters (and their instance
//      subkeys), leaving other entries intact — doing this before touching the service/driver
//      package avoids a window where the class stack still references a filter whose service
//      no longer exists (which can otherwise surface as Device Manager Code 19 until reboot).
//   3. Delete the service registration directly. This can't be left to DiUninstallDriverW: the
//      service was created out-of-band via SetupInstallServicesFromInfSectionW (see
//      install.cpp/common.h), not by DiInstallDriverW itself, so DiUninstallDriverW has no
//      record of it to reverse.
//   4. Remove the driver package from the Driver Store via DiUninstallDriverW.
// Like installation, this requires a reboot to take full effect.

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

// Deletes the service registration itself (distinct from stopping it). Returns true if the
// service ends up not registered, including if it was already absent.
bool DeleteServiceRegistration(const wchar_t* serviceName)
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        return false;
    }

    SC_HANDLE service = OpenServiceW(scm, serviceName, DELETE);
    if (service == nullptr) {
        // Not registered at all: nothing to delete.
        CloseServiceHandle(scm);
        return true;
    }

    BOOL deleted = DeleteService(service);
    DWORD error = GetLastError();

    CloseServiceHandle(service);
    CloseServiceHandle(scm);

    // ERROR_SERVICE_MARKED_FOR_DELETE: a handle elsewhere is still open on it (e.g. the SCM
    // itself, briefly); the registration is still gone as far as a future install is
    // concerned, so treat it the same as outright success.
    return deleted || error == ERROR_SERVICE_MARKED_FOR_DELETE;
}

} // namespace

int RunUninstall()
{
    if (!IsRunningElevated()) {
        wprintf(L"[ERROR] This uninstaller must be run as Administrator.\n");
        return 1;
    }

    if (!StopAndWaitService(ServiceName, 10000)) {
        wprintf(L"[WARNING] Could not confirm '%s' stopped; continuing anyway.\n", ServiceName);
    }

    bool filtersOk = ModifyUpperFilters(KeyboardClassGuidString, ServiceName, false, nullptr);
    filtersOk = ModifyUpperFilters(MouseClassGuidString, ServiceName, false, nullptr) && filtersOk;

    if (!filtersOk) {
        wprintf(
            L"[ERROR] Failed to remove '%s' from the Keyboard/Mouse device classes' upper "
            L"filters.\n",
            ServiceName
            );
        return 1;
    }
    wprintf(L"Removed '%s' from the Keyboard and Mouse device classes' upper filters.\n", ServiceName);

    if (!DeleteServiceRegistration(ServiceName)) {
        wprintf(L"[ERROR] Failed to delete the '%s' service registration: %lu\n", ServiceName, GetLastError());
        return 1;
    }
    wprintf(L"Deleted the %s service registration.\n", ServiceName);

    std::filesystem::path infPath = GetModuleDirectory() / L"drivers" / InfFileName;

    if (!std::filesystem::exists(infPath)) {
        wprintf(
            L"[WARNING] Driver package not found at %s — skipping DiUninstallDriver (the "
            L"service and UpperFilters entries above are already removed; the Driver Store "
            L"copy, if any, will need to be cleaned up separately, e.g. via pnputil).\n",
            infPath.c_str()
            );
        return 0;
    }

    BOOL needReboot = FALSE;

    if (!DiUninstallDriverW(nullptr, infPath.c_str(), 0, &needReboot)) {
        wprintf(L"[ERROR] DiUninstallDriver failed: %lu\n", GetLastError());
        return 1;
    }
    wprintf(L"Removed the %s driver package from the Driver Store.\n", ServiceName);

    wprintf(
        L"\nUninstallation complete. A REBOOT is required to fully unload %s and rebuild the "
        L"device filter chains without it.%s\n",
        ServiceName,
        needReboot ? L" (DiUninstallDriver also reported a reboot is needed.)" : L""
        );

    return 0;
}

} // namespace OpenInputBridge
