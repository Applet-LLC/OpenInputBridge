// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// See verify.h.

#include "verify.h"
#include "common.h"
#include "auditlog.h"
#include "toastsetup.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace OpenInputBridge {

namespace {

// Reads serviceName's ImagePath (REG_SZ or REG_EXPAND_SZ) from
// SYSTEM\CurrentControlSet\Services\<serviceName>. Returns false if the service key or the
// value itself doesn't exist.
bool TryGetServiceImagePath(const wchar_t* serviceName, std::wstring& outImagePath)
{
    std::wstring keyPath = std::wstring(L"SYSTEM\\CurrentControlSet\\Services\\") + serviceName;

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD sizeBytes = 0;
    bool ok = false;

    if (RegQueryValueExW(key, L"ImagePath", nullptr, &type, nullptr, &sizeBytes) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ) && sizeBytes > 0) {
        std::vector<wchar_t> buffer(sizeBytes / sizeof(wchar_t) + 1, L'\0');
        if (RegQueryValueExW(
                key, L"ImagePath", nullptr, nullptr,
                reinterpret_cast<BYTE*>(buffer.data()), &sizeBytes
                ) == ERROR_SUCCESS) {
            outImagePath = buffer.data();
            ok = true;
        }
    }

    RegCloseKey(key);
    return ok;
}

// Resolves a kernel-driver ImagePath value to an actual filesystem path. Confirmed
// empirically against a real installed service (SetupInstallServicesFromInfSectionW's own
// output): a REG_EXPAND_SZ value using a literal "\SystemRoot\..." NT-path prefix, e.g.
// "\SystemRoot\System32\DriverStore\FileRepository\oib_kbd.inf_.../oib_kbd.sys" — not a
// "%SystemRoot%"-style environment-variable token, so ExpandEnvironmentStringsW alone
// (harmless no-op here, but still run first in case some other install path ever does use
// that form) does not resolve it; the "\SystemRoot\" prefix itself is handled explicitly.
// "\??\"-prefixed and bare-relative (e.g. "system32\drivers\oib_kbd.sys") forms are handled
// too, as other plausible ImagePath shapes for a kernel driver.
std::filesystem::path ResolveServiceImagePath(const std::wstring& imagePath)
{
    wchar_t expanded[MAX_PATH * 2];
    std::wstring path = ExpandEnvironmentStringsW(imagePath.c_str(), expanded, static_cast<DWORD>(std::size(expanded))) > 0
        ? expanded
        : imagePath;

    wchar_t windowsDirectory[MAX_PATH];
    GetWindowsDirectoryW(windowsDirectory, MAX_PATH);

    const wchar_t systemRootPrefix[] = L"\\SystemRoot\\";
    const wchar_t ntPathPrefix[] = L"\\??\\";

    if (_wcsnicmp(path.c_str(), systemRootPrefix, wcslen(systemRootPrefix)) == 0) {
        path = std::wstring(windowsDirectory) + L"\\" + path.substr(wcslen(systemRootPrefix));
    } else if (path.rfind(ntPathPrefix, 0) == 0) {
        path = path.substr(wcslen(ntPathPrefix));
    }

    std::filesystem::path resolved(path);
    if (!resolved.is_absolute()) {
        // A bare path relative to the Windows directory, e.g. "system32\drivers\oib_kbd.sys".
        resolved = std::filesystem::path(windowsDirectory) / path;
    }
    return resolved;
}

// See verify.h's item 1. Returns false (and, hopefully, corrects the problem) only when the
// dangerous "registered but missing on disk" state was actually found.
bool VerifyDriverFilterIntegrity(DriverType type)
{
    const DriverInfo& driver = GetDriverInfo(type);

    if (!IsRegisteredAsUpperFilter(driver.ClassGuidString, driver.ServiceName)) {
        // Not registered at all -- nothing to verify (never installed, or already uninstalled).
        return true;
    }

    std::wstring imagePath;
    bool fileExists = TryGetServiceImagePath(driver.ServiceName, imagePath) &&
        std::filesystem::exists(ResolveServiceImagePath(imagePath));

    if (fileExists) {
        wprintf(L"[OK] %s is registered as an upper filter and its driver file is present.\n", driver.ServiceName);
        return true;
    }

    wprintf(
        L"[ERROR] %s is registered as an upper filter ahead of %s, but its driver file is "
        L"missing. Leaving this in place would make your keyboard/mouse stop working entirely "
        L"after the next reboot, so the filter registration has been removed. This means the "
        L"installation was not completed correctly -- please reinstall.\n",
        driver.ServiceName, driver.InsertBeforeClassDriver
        );

    ModifyUpperFilters(driver.ClassGuidString, driver.ServiceName, false, driver.InsertBeforeClassDriver);
    return false;
}

} // namespace

int RunVerifyInstall()
{
    if (!IsRunningElevated()) {
        wprintf(L"[ERROR] This installer must be run as Administrator.\n");
        return 1;
    }

    bool ok = VerifyDriverFilterIntegrity(DriverType::Keyboard);
    ok = VerifyDriverFilterIntegrity(DriverType::Mouse) && ok;

    bool auditLogEnabled = ScheduledTaskExists(AuditLogReapplyTaskName);
    bool toastEnabled = ScheduledTaskExists(ToastNotifyTaskName);

    if (!auditLogEnabled || !toastEnabled) {
        wprintf(
            L"\nNOTE: This device driver can be opened by any process, even one without "
            L"administrator privileges -- that's required for compatibility with the "
            L"Interception protocol (see docs/SECURITY_CONSIDERATIONS.md). Consider enabling "
            L"logging and/or toast notifications (--enable-audit-log / --enable-toast) so "
            L"you're aware of what's accessing it.\n"
            );
    }

    return ok ? 0 : 1;
}

} // namespace OpenInputBridge
