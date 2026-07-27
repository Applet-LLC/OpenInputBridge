// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Installer: stages the driver package (drivers\OpenInputBridge.inf/.cat/.sys, expected
// alongside this executable) into the Driver Store via DiInstallDriverW, then separately runs
// [DefaultInstall.NTamd64.Services] against the *staged* copy of the INF via
// SetupInstallServicesFromInfSectionW to actually create the
// SERVICE_KERNEL_DRIVER/SERVICE_SYSTEM_START service (DiInstallDriverW alone only stages the
// package — see common.h for why), then registers OpenInputBridge as an upper filter
// positioned immediately before kbdclass/mouclass for both the Keyboard and Mouse device
// setup classes. See driver/OpenInputBridge.inx and docs/PROTOCOL.md.

#include "common.h"

#include <newdev.h>
#include <setupapi.h>

#include <filesystem>
#include <cstdio>

#pragma comment(lib, "Newdev.lib")
#pragma comment(lib, "Setupapi.lib")

namespace OpenInputBridge {

namespace {

std::filesystem::path GetModuleDirectory()
{
    wchar_t modulePath[MAX_PATH];
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    return std::filesystem::path(modulePath).parent_path();
}

// DiInstallDriverW stages the package under
// %windir%\System32\DriverStore\FileRepository\OpenInputBridge.inf_<hash>\OpenInputBridge.inf
// (the "_<hash>" suffix is assigned by the Driver Store and not predictable in advance).
// Finds that staged copy so its [DefaultInstall.NTamd64.Services] section can be run directly
// — the source-tree copy next to this .exe was never "installed" from the OS's point of view,
// so running services from that copy would work but leave the service pointing at a
// non-Driver-Store ImagePath; using the staged copy matches what DiInstallDriverW itself
// would have created a service pointing at, had it processed the Services section.
std::filesystem::path FindStagedInfPath()
{
    wchar_t systemDirectory[MAX_PATH];
    GetSystemDirectoryW(systemDirectory, MAX_PATH);

    std::filesystem::path searchPattern =
        std::filesystem::path(systemDirectory) / L"DriverStore" / L"FileRepository" / L"OpenInputBridge.inf_*";

    WIN32_FIND_DATAW findData{};
    HANDLE findHandle = FindFirstFileW(searchPattern.c_str(), &findData);

    if (findHandle == INVALID_HANDLE_VALUE) {
        return {};
    }

    std::filesystem::path staged;
    do {
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            std::filesystem::path candidate =
                std::filesystem::path(systemDirectory) / L"DriverStore" / L"FileRepository" /
                findData.cFileName / InfFileName;
            if (std::filesystem::exists(candidate)) {
                staged = candidate; // Last match wins; in practice there is exactly one.
            }
        }
    } while (FindNextFileW(findHandle, &findData));

    FindClose(findHandle);
    return staged;
}

bool CreateServiceFromStagedInf()
{
    std::filesystem::path stagedInfPath = FindStagedInfPath();

    if (stagedInfPath.empty()) {
        wprintf(L"[ERROR] Could not locate the staged driver package in the Driver Store after DiInstallDriver.\n");
        return false;
    }

    HINF infHandle = SetupOpenInfFileW(stagedInfPath.c_str(), nullptr, INF_STYLE_WIN4, nullptr);

    if (infHandle == INVALID_HANDLE_VALUE) {
        wprintf(L"[ERROR] SetupOpenInfFile failed on %s: %lu\n", stagedInfPath.c_str(), GetLastError());
        return false;
    }

    BOOL serviceInstalled = SetupInstallServicesFromInfSectionW(
        infHandle, L"DefaultInstall.NTamd64.Services", 0
        );

    SetupCloseInfFile(infHandle);

    if (!serviceInstalled) {
        wprintf(L"[ERROR] SetupInstallServicesFromInfSection failed: %lu\n", GetLastError());
        return false;
    }

    return true;
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
    wprintf(L"Staged the %s driver package into the Driver Store.\n", ServiceName);

    // DiInstallDriverW alone does not create the service (this is a primitive driver with no
    // [Manufacturer]/[Models] device match, so its own device-install phase never runs) — run
    // [DefaultInstall.NTamd64.Services] against the staged INF explicitly. See common.h.
    if (!CreateServiceFromStagedInf()) {
        return 1;
    }

    if (!ServiceExists(ServiceName)) {
        wprintf(L"[ERROR] Service registration reported success but '%s' is not registered with the SCM.\n", ServiceName);
        return 1;
    }
    wprintf(L"Registered the %s service (SERVICE_KERNEL_DRIVER, SERVICE_SYSTEM_START).\n", ServiceName);

    // OpenInputBridge hooks IOCTL_INTERNAL_KEYBOARD_CONNECT / IOCTL_INTERNAL_MOUSE_CONNECT, so
    // it must sit below kbdclass/mouclass in the stack to see that IOCTL — i.e. immediately
    // BEFORE them in the UpperFilters list. See common.h.
    bool filtersOk = ModifyUpperFilters(KeyboardClassGuidString, ServiceName, true, L"kbdclass");
    filtersOk = ModifyUpperFilters(MouseClassGuidString, ServiceName, true, L"mouclass") && filtersOk;

    if (!filtersOk) {
        wprintf(
            L"[ERROR] Failed to register '%s' as an upper filter for the Keyboard/Mouse "
            L"device classes.\n",
            ServiceName
            );
        return 1;
    }
    wprintf(
        L"Registered '%s' as an upper filter immediately before kbdclass/mouclass for the "
        L"Keyboard and Mouse device classes.\n",
        ServiceName
        );

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
