// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Uninstaller: reverses install.cpp, for one driver (see common.h's DriverInfo), in the order
// that's safest against leaving the system in a half-removed state if interrupted partway
// through:
//   1. Stop the running service (nothing should still be attached to it after this).
//   2. Remove the driver from its device class's UpperFilters (and its instance subkeys),
//      leaving other entries intact — doing this before touching the service/driver package
//      avoids a window where the class stack still references a filter whose service no
//      longer exists (which can otherwise surface as Device Manager Code 19 until reboot).
//   3. Delete the service registration directly. This can't be left to DiUninstallDriverW: the
//      service was created out-of-band via SetupInstallServicesFromInfSectionW (see
//      install.cpp/common.h), not by DiInstallDriverW itself, so DiUninstallDriverW has no
//      record of it to reverse.
//   4. Remove the driver package from the Driver Store via DiUninstallDriverW.
//   5. Regardless of how steps 2-4 went, run common.h's VerifyDriverFilterIntegrity as a final
//      safety net: it must never be possible to walk away from an uninstall attempt with the
//      driver still registered as an upper filter but its file gone (self-heals if found —
//      the same check --verify-install runs after a fresh install). Step 3 failing alone is
//      harmless (an orphaned, unreferenced service registration doesn't load or do anything),
//      so it's reported but doesn't block steps 4-5 from still running.
// Like installation, this requires a reboot to take full effect.

#include "common.h"

#include <newdev.h>

#include <filesystem>
#include <cstdio>
#include <string>

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

int RunUninstallOne(const DriverInfo& driver)
{
    if (!StopAndWaitService(driver.ServiceName, 10000)) {
        wprintf(L"[WARNING] Could not confirm '%s' stopped; continuing anyway.\n", driver.ServiceName);
    }

    bool ok = true;

    if (!ModifyUpperFilters(driver.ClassGuidString, driver.ServiceName, false, nullptr)) {
        wprintf(
            L"[ERROR] Failed to remove '%s' from its device class's upper filters.\n",
            driver.ServiceName
            );
        ok = false;
    } else {
        wprintf(L"Removed '%s' from its device class's upper filters.\n", driver.ServiceName);
    }

    // Not fatal to the overall uninstall: a service registration with no UpperFilters entry (and
    // soon no Driver Store package) pointing at it is inert -- nothing loads or references it --
    // so this is worth reporting but not worth aborting the rest of the uninstall over.
    if (!DeleteServiceRegistration(driver.ServiceName)) {
        wprintf(
            L"[WARNING] Failed to delete the '%s' service registration: %lu (harmless -- an "
            L"orphaned, unreferenced service registration doesn't load or do anything).\n",
            driver.ServiceName, GetLastError()
            );
    } else {
        wprintf(L"Deleted the %s service registration.\n", driver.ServiceName);
    }

    std::filesystem::path infPath =
        GetModuleDirectory() / driver.PackageName / (std::wstring(driver.PackageName) + L".inf");
    BOOL needReboot = FALSE;

    if (!std::filesystem::exists(infPath)) {
        wprintf(
            L"[WARNING] Driver package not found at %s — skipping DiUninstallDriver (the "
            L"service and UpperFilters entries above are already removed; the Driver Store "
            L"copy, if any, will need to be cleaned up separately, e.g. via pnputil).\n",
            infPath.c_str()
            );
    } else if (!DiUninstallDriverW(nullptr, infPath.c_str(), 0, &needReboot)) {
        wprintf(L"[ERROR] DiUninstallDriver failed: %lu\n", GetLastError());
        ok = false;
    } else {
        wprintf(L"Removed the %s driver package from the Driver Store.\n", driver.ServiceName);
    }

    // Final step, run unconditionally regardless of how the steps above went: the one state that
    // must never be left behind is "still registered as an upper filter, but the driver file it
    // points at is gone" -- that makes the corresponding device class stop responding entirely
    // after the next reboot. Self-heals (removes the stale filter registration) if found -- the
    // same check --verify-install runs after a fresh install (common.h's
    // VerifyDriverFilterIntegrity).
    if (!VerifyDriverFilterIntegrity(driver)) {
        ok = false;
    }

    if (ok) {
        wprintf(
            L"\nUninstallation of %s complete. A REBOOT is required to fully unload it and "
            L"rebuild the device filter chains without it.%s\n",
            driver.ServiceName,
            needReboot ? L" (DiUninstallDriver also reported a reboot is needed.)" : L""
            );
    }

    return ok ? 0 : 1;
}

} // namespace

int RunUninstall(DriverType type)
{
    if (!IsRunningElevated()) {
        wprintf(L"[ERROR] This uninstaller must be run as Administrator.\n");
        return 1;
    }

    return RunUninstallOne(GetDriverInfo(type));
}

} // namespace OpenInputBridge
