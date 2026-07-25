// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Installer: copies the driver binary into %SystemRoot%\System32\drivers, registers it as a
// SERVICE_KERNEL_DRIVER / SERVICE_SYSTEM_START service (keyboard/mouse stacks build early in
// boot, so a demand-start service risks missing the initial stack build), and registers it as
// an upper filter for both the Keyboard and Mouse device setup classes. See common.h and the
// project plan's "4. インストール方式" section.

#include "common.h"

#include <filesystem>
#include <cstdio>
#include <system_error>

namespace OpenInputBridge {

namespace {

std::filesystem::path GetModuleDirectory()
{
    wchar_t modulePath[MAX_PATH];
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    return std::filesystem::path(modulePath).parent_path();
}

std::filesystem::path GetDriversDirectory()
{
    wchar_t systemDir[MAX_PATH];
    GetSystemDirectoryW(systemDir, MAX_PATH);
    return std::filesystem::path(systemDir) / L"drivers";
}

// Copies DriverFileName from alongside this installer executable into the drivers directory.
bool CopyDriverBinary(std::filesystem::path& installedPath)
{
    std::filesystem::path source = GetModuleDirectory() / DriverFileName;
    std::filesystem::path destination = GetDriversDirectory() / DriverFileName;

    std::error_code ec;
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);

    if (ec) {
        wprintf(L"[ERROR] Failed to copy %s to %s: %hs\n", source.c_str(), destination.c_str(), ec.message().c_str());
        return false;
    }

    installedPath = destination;
    return true;
}

bool CreateOrUpdateDriverService(const std::filesystem::path& driverPath)
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (scm == nullptr) {
        wprintf(L"[ERROR] OpenSCManager failed: %lu\n", GetLastError());
        return false;
    }

    SC_HANDLE service = CreateServiceW(
        scm,
        ServiceName,
        ServiceName,
        SERVICE_ALL_ACCESS,
        SERVICE_KERNEL_DRIVER,
        SERVICE_SYSTEM_START,
        SERVICE_ERROR_NORMAL,
        driverPath.c_str(),
        nullptr, nullptr, nullptr, nullptr, nullptr
        );

    if (service == nullptr) {
        DWORD error = GetLastError();

        if (error != ERROR_SERVICE_EXISTS) {
            wprintf(L"[ERROR] CreateService failed: %lu\n", error);
            CloseServiceHandle(scm);
            return false;
        }

        // Re-running the installer over an existing install: the service already points at
        // the same (just-overwritten) driver file path, nothing more to do here.
        service = OpenServiceW(scm, ServiceName, SERVICE_ALL_ACCESS);
        if (service == nullptr) {
            wprintf(L"[ERROR] OpenService failed: %lu\n", GetLastError());
            CloseServiceHandle(scm);
            return false;
        }
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return true;
}

} // namespace

int RunInstall()
{
    if (!IsRunningElevated()) {
        wprintf(L"[ERROR] This installer must be run as Administrator.\n");
        return 1;
    }

    std::filesystem::path installedDriverPath;
    if (!CopyDriverBinary(installedDriverPath)) {
        return 1;
    }
    wprintf(L"Installed driver binary to %s\n", installedDriverPath.c_str());

    if (!CreateOrUpdateDriverService(installedDriverPath)) {
        return 1;
    }
    wprintf(L"Registered service '%s' (SERVICE_SYSTEM_START).\n", ServiceName);

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
        L"time, not immediately.\n",
        ServiceName
        );

    return 0;
}

} // namespace OpenInputBridge
