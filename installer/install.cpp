// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Installer: stages one driver package (<exeDir>\<PackageName>\<PackageName>.inf/.cat/.sys,
// see common.h's DriverInfo) into the Driver Store via DiInstallDriverW, then separately runs
// [DefaultInstall.NTamd64.Services] against the *staged* copy of the INF via
// SetupInstallServicesFromInfSectionW to actually create the
// SERVICE_KERNEL_DRIVER/SERVICE_SYSTEM_START service (DiInstallDriverW alone only stages the
// package — see common.h for why), then registers the driver as an upper filter positioned
// immediately before its class driver (kbdclass/mouclass). See driver/keyboard/keyboard.inx,
// driver/mouse/mouse.inx, and docs/PROTOCOL.md.

#include "common.h"

#include <newdev.h>
#include <setupapi.h>

#include <filesystem>
#include <cstdio>
#include <string>

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
// %windir%\System32\DriverStore\FileRepository\<PackageName>.inf_<hash>\<PackageName>.inf
// (the "_<hash>" suffix is assigned by the Driver Store and not predictable in advance).
// Finds that staged copy so its [DefaultInstall.NTamd64.Services] section can be run directly
// — the source-tree copy next to this .exe was never "installed" from the OS's point of view,
// so running services from that copy would work but leave the service pointing at a
// non-Driver-Store ImagePath; using the staged copy matches what DiInstallDriverW itself
// would have created a service pointing at, had it processed the Services section.
std::filesystem::path FindStagedInfPath(const DriverInfo& driver)
{
    wchar_t systemDirectory[MAX_PATH];
    GetSystemDirectoryW(systemDirectory, MAX_PATH);

    std::wstring infFileName = std::wstring(driver.PackageName) + L".inf";
    std::filesystem::path searchPattern =
        std::filesystem::path(systemDirectory) / L"DriverStore" / L"FileRepository" /
        (std::wstring(driver.PackageName) + L".inf_*");

    WIN32_FIND_DATAW findData{};
    HANDLE findHandle = FindFirstFileW(searchPattern.c_str(), &findData);

    if (findHandle == INVALID_HANDLE_VALUE) {
        return {};
    }

    std::filesystem::path staged;
    FILETIME stagedWriteTime{};

    do {
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            std::filesystem::path candidate =
                std::filesystem::path(systemDirectory) / L"DriverStore" / L"FileRepository" /
                findData.cFileName / infFileName;
            if (std::filesystem::exists(candidate)) {
                // Repeated installs (e.g. iterative testing) can leave more than one staged
                // generation behind here — this installer doesn't prune superseded ones the
                // way e.g. kbdaddid/mouaddid's DriverManager.cpp does. Picking "whichever
                // FindNextFileW enumerates last" is not guaranteed to be the one just staged
                // by DiInstallDriverW above, and running services from a stale/different
                // generation's INF can fail in confusing ways (e.g. ERROR_SECTION_NOT_FOUND
                // if that generation's layout differs). Prefer the most recently modified one.
                if (staged.empty() || CompareFileTime(&findData.ftLastWriteTime, &stagedWriteTime) > 0) {
                    staged = candidate;
                    stagedWriteTime = findData.ftLastWriteTime;
                }
            }
        }
    } while (FindNextFileW(findHandle, &findData));

    FindClose(findHandle);
    return staged;
}

bool CreateServiceFromStagedInf(const DriverInfo& driver)
{
    std::filesystem::path stagedInfPath = FindStagedInfPath(driver);

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

DriverType OtherDriverType(DriverType type)
{
    return (type == DriverType::Keyboard) ? DriverType::Mouse : DriverType::Keyboard;
}

// Resolves the shared KeyboardSlotCount value (docs/DECISIONS.md's 2026-08-02 entry) for this
// install: an explicit --slots for this driver type wins outright; otherwise, whichever
// service (the other one, or this one from a previous install) already has a value configured
// is adopted, so re-running install without --slots never silently resets a custom split;
// falling back to DefaultKeyboardSlotCount only when neither service has one yet.
ULONG ResolveKeyboardSlotCount(DriverType type, std::optional<ULONG> requestedSlots)
{
    if (requestedSlots.has_value()) {
        return (type == DriverType::Keyboard) ? *requestedSlots : (TotalDeviceSlotCount - *requestedSlots);
    }

    ULONG existing = 0;
    if (TryGetKeyboardSlotCount(OtherDriverType(type), existing)) {
        return existing;
    }
    if (TryGetKeyboardSlotCount(type, existing)) {
        return existing;
    }

    return DefaultKeyboardSlotCount;
}

int RunInstallOne(const DriverInfo& driver, DriverType type, std::optional<ULONG> requestedSlots)
{
    std::filesystem::path infPath =
        GetModuleDirectory() / driver.PackageName / (std::wstring(driver.PackageName) + L".inf");

    if (!std::filesystem::exists(infPath)) {
        wprintf(L"[ERROR] Driver package not found: %s\n", infPath.c_str());
        return 1;
    }

    BOOL needReboot = FALSE;

    if (!DiInstallDriverW(nullptr, infPath.c_str(), 0, &needReboot)) {
        wprintf(L"[ERROR] DiInstallDriver failed: %lu\n", GetLastError());
        return 1;
    }
    wprintf(L"Staged the %s driver package into the Driver Store.\n", driver.ServiceName);

    // DiInstallDriverW alone does not create the service (this is a primitive driver with no
    // [Manufacturer]/[Models] device match, so its own device-install phase never runs) — run
    // [DefaultInstall.NTamd64.Services] against the staged INF explicitly. See common.h.
    if (!CreateServiceFromStagedInf(driver)) {
        return 1;
    }

    if (!ServiceExists(driver.ServiceName)) {
        wprintf(L"[ERROR] Service registration reported success but '%s' is not registered with the SCM.\n", driver.ServiceName);
        return 1;
    }
    wprintf(L"Registered the %s service (SERVICE_KERNEL_DRIVER, SERVICE_SYSTEM_START).\n", driver.ServiceName);

    // Keyboard/mouse slot split (docs/DECISIONS.md's 2026-08-02 entry): write the shared
    // KeyboardSlotCount value to this driver's own Parameters key now that its service key
    // exists, and mirror it to the other driver's Parameters key too if that service is
    // already installed, so the two can never disagree about where the boundary is.
    ULONG keyboardSlotCount = ResolveKeyboardSlotCount(type, requestedSlots);

    if (!SetKeyboardSlotCount(type, keyboardSlotCount)) {
        wprintf(L"[ERROR] Failed to write KeyboardSlotCount for '%s'.\n", driver.ServiceName);
        return 1;
    }

    const DriverInfo& otherDriver = GetDriverInfo(OtherDriverType(type));
    if (ServiceExists(otherDriver.ServiceName) && !SetKeyboardSlotCount(OtherDriverType(type), keyboardSlotCount)) {
        wprintf(L"[ERROR] Failed to sync KeyboardSlotCount to '%s'.\n", otherDriver.ServiceName);
        return 1;
    }

    wprintf(
        L"Configured slot split: %lu keyboard / %lu mouse (of %lu total).\n",
        keyboardSlotCount, TotalDeviceSlotCount - keyboardSlotCount, TotalDeviceSlotCount
        );

    // This driver hooks IOCTL_INTERNAL_KEYBOARD_CONNECT/IOCTL_INTERNAL_MOUSE_CONNECT, so it
    // must sit below its class driver in the stack to see that IOCTL — i.e. immediately BEFORE
    // it in the UpperFilters list. See common.h.
    if (!ModifyUpperFilters(driver.ClassGuidString, driver.ServiceName, true, driver.InsertBeforeClassDriver)) {
        wprintf(
            L"[ERROR] Failed to register '%s' as an upper filter immediately before '%s'.\n",
            driver.ServiceName,
            driver.InsertBeforeClassDriver
            );
        return 1;
    }
    wprintf(
        L"Registered '%s' as an upper filter immediately before '%s'.\n",
        driver.ServiceName,
        driver.InsertBeforeClassDriver
        );

    wprintf(
        L"\nInstallation of %s complete. A REBOOT is required before it takes effect: Windows "
        L"only rebuilds device filter chains (UpperFilters) at boot / device stack "
        L"(re)construction time, not immediately.%s\n",
        driver.ServiceName,
        needReboot ? L" (DiInstallDriver also reported a reboot is needed.)" : L""
        );

    return 0;
}

} // namespace

int RunInstall(DriverType type, std::optional<ULONG> requestedSlots)
{
    if (!IsRunningElevated()) {
        wprintf(L"[ERROR] This installer must be run as Administrator.\n");
        return 1;
    }

    if (requestedSlots.has_value() && *requestedSlots > TotalDeviceSlotCount) {
        wprintf(
            L"[ERROR] --slots must be between 0 and %lu (got %lu).\n",
            TotalDeviceSlotCount, *requestedSlots
            );
        return 1;
    }

    return RunInstallOne(GetDriverInfo(type), type, requestedSlots);
}

} // namespace OpenInputBridge
