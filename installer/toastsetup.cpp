// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// See toastsetup.h.

#include "toastsetup.h"
#include "common.h"

#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#pragma comment(lib, "Ole32.lib")

namespace OpenInputBridge {

namespace {

using Microsoft::WRL::ComPtr;

inline constexpr wchar_t ToastTaskName[] = L"OpenInputBridgeToastNotify";
inline constexpr wchar_t ToastHelperExeName[] = L"OibToastHelper.exe";

// Name of both the Start Menu subfolder (under the all-users Programs folder) and the .lnk
// file created there — see CreateStartMenuShortcut's own comment for why this exists at all.
inline constexpr wchar_t StartMenuFolderName[] = L"OpenInputBridge";
inline constexpr wchar_t ShortcutFileName[] = L"OpenInputBridge Notification Helper.lnk";

// HKLM (not HKCU) so the AUMID resolves to "OpenInputBridge" for every user on the machine, not
// only whichever account happens to run this installer — the toast itself can be triggered by
// any logged-on user opening a control device, not just an administrator.
inline constexpr wchar_t AumidRegistryKeyPrefix[] = L"SOFTWARE\\Classes\\AppUserModelId\\";

// Must match installer/toast-helper/main.cpp's own copies of these two strings exactly — see
// ToastAppUserModelId's comment in toastsetup.h for why constants shared across this installer
// and the separately-built toast helper are kept in sync as separate, commented definitions
// rather than via a shared header.
inline constexpr wchar_t ToastAllowlistKeyPath[] = L"SOFTWARE\\OpenInputBridge";
inline constexpr wchar_t ToastAllowlistValueName[] = L"ToastAllowedProcessPaths";

std::filesystem::path GetToastHelperPath()
{
    std::wstring installerPath = GetInstallerExecutablePath();
    return std::filesystem::path(installerPath).parent_path() / ToastHelperExeName;
}

// See installer/toast-helper/main.cpp for the icon file this expects to sit next to it — that
// asset itself is out of scope here (it's staged alongside OibToastHelper.exe by whatever
// installed this exe in the first place: this CLI installer's own driver-package layout for the
// OSS repo, or the Pro/Subscription repos' WiX installer projects, which vendor/rebuild the same
// driver-package).
std::wstring GetToastIconPath()
{
    return (GetToastHelperPath().parent_path() / L"OibToastHelper.ico").wstring();
}

// Windows.UI.Notifications requires a Start Menu shortcut pointing at the exe that raises a
// toast under a given AUMID for a non-packaged desktop app — without one, the AUMID identity
// registered below can fail to resolve reliably (see docs/DECISIONS.md's 2026-08-08 entry).
// OibToastHelper.exe is never meant to be launched by a user directly (it does nothing useful
// without the --object-name/--process-id/--user arguments the Scheduled Task passes it), so
// this shortcut exists purely for that identity requirement, not as a real Start Menu entry a
// user would click.
//
// Uses the all-users Programs folder (FOLDERID_CommonPrograms) rather than the per-user one:
// this installer already requires elevation (IsRunningElevated), matching a perMachine install.
std::filesystem::path GetShortcutPath()
{
    wchar_t* commonProgramsPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_CommonPrograms, 0, nullptr, &commonProgramsPath))) {
        return {};
    }

    std::filesystem::path path = std::filesystem::path(commonProgramsPath) / StartMenuFolderName / ShortcutFileName;
    CoTaskMemFree(commonProgramsPath);
    return path;
}

bool CreateStartMenuShortcut(const std::filesystem::path& targetPath)
{
    std::filesystem::path shortcutPath = GetShortcutPath();
    if (shortcutPath.empty()) {
        wprintf(L"[ERROR] Failed to resolve the all-users Start Menu folder.\n");
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(shortcutPath.parent_path(), ec);

    // CoInitializeEx may legitimately return RPC_E_CHANGED_MODE / S_FALSE if COM was already
    // initialized (with a different or the same apartment model) elsewhere in this process;
    // either way SUCCEEDED() is true and CoUninitialize below is still the correct matching
    // call. Only a genuine failure (e.g. E_OUTOFMEMORY) should abort here.
    HRESULT coInitResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(coInitResult)) {
        wprintf(L"[ERROR] CoInitializeEx failed: 0x%08lX\n", static_cast<unsigned long>(coInitResult));
        return false;
    }

    bool ok = false;
    ComPtr<IShellLinkW> shellLink;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink));

    if (SUCCEEDED(hr)) {
        shellLink->SetPath(targetPath.c_str());
        shellLink->SetWorkingDirectory(targetPath.parent_path().c_str());
        shellLink->SetDescription(L"OpenInputBridge toast-notification identity helper (not meant to be run directly).");

        ComPtr<IPersistFile> persistFile;
        hr = shellLink.As(&persistFile);
        if (SUCCEEDED(hr)) {
            hr = persistFile->Save(shortcutPath.c_str(), TRUE);
            ok = SUCCEEDED(hr);
        }
    }

    CoUninitialize();

    if (!ok) {
        wprintf(L"[ERROR] Failed to create Start Menu shortcut: 0x%08lX\n", static_cast<unsigned long>(hr));
    }
    return ok;
}

// Idempotent: succeeds silently if the shortcut/folder are already gone.
void RemoveStartMenuShortcut()
{
    std::filesystem::path shortcutPath = GetShortcutPath();
    if (shortcutPath.empty()) {
        return;
    }

    std::error_code ec;
    std::filesystem::remove(shortcutPath, ec);
    // Only removes the OpenInputBridge subfolder itself, not CommonPrograms — fails harmlessly
    // (non-empty directory) if anything else ever gets placed in it.
    std::filesystem::remove(shortcutPath.parent_path(), ec);
}

// Reads the REG_MULTI_SZ toast-suppression allowlist; empty if the key/value doesn't exist yet
// (nothing allowlisted is the default, matching a fresh install).
std::vector<std::wstring> ReadToastAllowlist()
{
    std::vector<std::wstring> paths;

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ToastAllowlistKeyPath, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return paths;
    }

    DWORD type = 0;
    DWORD sizeBytes = 0;
    if (RegQueryValueExW(key, ToastAllowlistValueName, nullptr, &type, nullptr, &sizeBytes) == ERROR_SUCCESS &&
        type == REG_MULTI_SZ && sizeBytes > 0) {
        std::vector<wchar_t> buffer(sizeBytes / sizeof(wchar_t));
        if (RegQueryValueExW(
                key, ToastAllowlistValueName, nullptr, nullptr,
                reinterpret_cast<BYTE*>(buffer.data()), &sizeBytes
                ) == ERROR_SUCCESS) {
            for (const wchar_t* p = buffer.data(); *p != L'\0'; p += wcslen(p) + 1) {
                paths.emplace_back(p);
            }
        }
    }

    RegCloseKey(key);
    return paths;
}

bool WriteToastAllowlist(const std::vector<std::wstring>& paths)
{
    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE, ToastAllowlistKeyPath, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr
        );
    if (result != ERROR_SUCCESS) {
        wprintf(L"[ERROR] Failed to open/create HKLM\\%s: %ld\n", ToastAllowlistKeyPath, result);
        return false;
    }

    // REG_MULTI_SZ format: each string null-terminated, the whole sequence additionally
    // null-terminated (an empty list is thus a single L'\0').
    std::vector<wchar_t> buffer;
    for (const std::wstring& path : paths) {
        buffer.insert(buffer.end(), path.begin(), path.end());
        buffer.push_back(L'\0');
    }
    buffer.push_back(L'\0');

    result = RegSetValueExW(
        key, ToastAllowlistValueName, 0, REG_MULTI_SZ,
        reinterpret_cast<const BYTE*>(buffer.data()), static_cast<DWORD>(buffer.size() * sizeof(wchar_t))
        );
    RegCloseKey(key);

    if (result != ERROR_SUCCESS) {
        wprintf(L"[ERROR] Failed to write %s: %ld\n", ToastAllowlistValueName, result);
        return false;
    }
    return true;
}

bool RegisterAumid()
{
    std::wstring keyPath = std::wstring(AumidRegistryKeyPrefix) + ToastAppUserModelId;

    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr
        );

    if (result != ERROR_SUCCESS) {
        wprintf(L"[ERROR] Failed to create AppUserModelId registry key: %ld\n", result);
        return false;
    }

    const wchar_t displayName[] = L"OpenInputBridge";
    std::wstring iconPath = GetToastIconPath();

    bool ok =
        RegSetValueExW(
            key, L"DisplayName", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(displayName), sizeof(displayName)
            ) == ERROR_SUCCESS &&
        RegSetValueExW(
            key, L"IconUri", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(iconPath.c_str()),
            static_cast<DWORD>((iconPath.size() + 1) * sizeof(wchar_t))
            ) == ERROR_SUCCESS;

    RegCloseKey(key);

    if (!ok) {
        wprintf(L"[ERROR] Failed to write AppUserModelId registry values.\n");
    }
    return ok;
}

void UnregisterAumid()
{
    std::wstring keyPath = std::wstring(AumidRegistryKeyPrefix) + ToastAppUserModelId;
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, keyPath.c_str());
}

// Builds the Scheduled Task definition XML for the toast-notify task: triggered by Security-log
// event IDs 4656 ("a handle to an object was requested") or 4663 ("an attempt was made to
// access an object"), which the audit-log feature's SACL (auditlog.cpp) causes Windows to emit
// for any \\.\interceptionNN open — including, in principle, opens of any *other* object that
// happens to carry a Kernel-Object SACL elsewhere on the machine, since auditpol's "Kernel
// Object" subcategory is a single machine-wide switch, not scoped to our devices specifically.
// Rather than trying to encode an exact \Device\interceptionNN match in this trigger's XPath
// (Windows' event-query XPath subset has no contains()/starts-with(), so an exact-name filter
// here would mean spelling out all 20 device paths as one huge OR), the ObjectName is instead
// passed through as a parameter (see ValueQueries below) and OibToastHelper.exe itself checks
// the prefix before showing anything — see installer/toast-helper/main.cpp.
//
// Principal uses GroupId=S-1-5-32-545 (BUILTIN\Users) rather than a specific UserId: this is the
// standard construct for "run in whichever interactive user's session is currently logged on,
// do nothing if no one is" (see docs/DECISIONS.md's 2026-08-08 entry for why this, rather than a
// LocalSystem service, is used for the toast half of this feature specifically). Like the
// reapply task's event query (auditlog.cpp's BuildReapplyTaskXml), this has not been
// runtime-verified end-to-end as part of this change.
std::wstring BuildToastTaskXml(const std::wstring& helperPath)
{
    return
        L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n"
        L"<Task version=\"1.4\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\n"
        L"  <RegistrationInfo>\n"
        L"    <Description>OpenInputBridge: shows a toast notification when a process opens a "
        L"\\\\.\\interceptionNN control device (requires the audit-log feature to be enabled). "
        L"See docs/DECISIONS.md's 2026-08-08 entry.</Description>\n"
        L"  </RegistrationInfo>\n"
        L"  <Triggers>\n"
        L"    <EventTrigger>\n"
        L"      <Enabled>true</Enabled>\n"
        L"      <Subscription>&lt;QueryList&gt;&lt;Query Id=\"0\" Path=\"Security\"&gt;"
        L"&lt;Select Path=\"Security\"&gt;*[System[Provider[@Name='Microsoft-Windows-Security-Auditing'] "
        L"and (EventID=4656 or EventID=4663)]]&lt;/Select&gt;"
        L"&lt;/Query&gt;&lt;/QueryList&gt;</Subscription>\n"
        L"      <ValueQueries>\n"
        L"        <Value name=\"objectName\">Event/EventData/Data[@Name='ObjectName']</Value>\n"
        L"        <Value name=\"processId\">Event/EventData/Data[@Name='ProcessId']</Value>\n"
        // Captured directly from the event instead of re-resolved later via OpenProcess(pid) —
        // real-machine testing found the triggering process (especially OpenInputBridgeSetup.exe
        // itself, which completes in well under a second) has often already exited by the time
        // this task's Action actually runs, making PID-based lookup a race: OibToastHelper.exe's
        // self-noise filter silently failed open (showed a toast anyway) whenever the lookup lost
        // that race. The event's own ProcessName field has no such race — it's recorded at the
        // moment of the access itself, not resolved after the fact.
        L"        <Value name=\"processName\">Event/EventData/Data[@Name='ProcessName']</Value>\n"
        L"        <Value name=\"subjectUserName\">Event/EventData/Data[@Name='SubjectUserName']</Value>\n"
        L"      </ValueQueries>\n"
        L"    </EventTrigger>\n"
        L"  </Triggers>\n"
        L"  <Principals>\n"
        L"    <Principal id=\"Author\">\n"
        L"      <GroupId>S-1-5-32-545</GroupId>\n" // BUILTIN\Users — see comment above BuildToastTaskXml.
        L"      <RunLevel>LeastPrivilege</RunLevel>\n"
        L"    </Principal>\n"
        L"  </Principals>\n"
        L"  <Settings>\n"
        L"    <MultipleInstancesPolicy>Parallel</MultipleInstancesPolicy>\n"
        L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\n"
        L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\n"
        L"    <AllowHardTerminate>true</AllowHardTerminate>\n"
        L"    <StartWhenAvailable>false</StartWhenAvailable>\n"
        L"    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>\n"
        L"    <Enabled>true</Enabled>\n"
        L"    <Hidden>true</Hidden>\n"
        L"    <ExecutionTimeLimit>PT30S</ExecutionTimeLimit>\n"
        L"  </Settings>\n"
        L"  <Actions Context=\"Author\">\n"
        L"    <Exec>\n"
        L"      <Command>\"" + helperPath + L"\"</Command>\n"
        L"      <Arguments>--object-name \"$(objectName)\" --process-id \"$(processId)\" "
        L"--process-name \"$(processName)\" --user \"$(subjectUserName)\"</Arguments>\n"
        L"    </Exec>\n"
        L"  </Actions>\n"
        L"</Task>\n";
}

} // namespace

int RunEnableToast()
{
    if (!IsRunningElevated()) {
        wprintf(L"[ERROR] This installer must be run as Administrator.\n");
        return 1;
    }

    std::filesystem::path helperPath = GetToastHelperPath();
    if (!std::filesystem::exists(helperPath)) {
        wprintf(L"[ERROR] %s not found next to this installer.\n", helperPath.c_str());
        return 1;
    }

    if (!RegisterAumid()) {
        return 1;
    }
    wprintf(L"Registered the OpenInputBridge AppUserModelId.\n");

    if (!CreateStartMenuShortcut(helperPath)) {
        UnregisterAumid();
        return 1;
    }
    wprintf(L"Created the Start Menu shortcut the AUMID needs to resolve reliably.\n");

    if (!RegisterScheduledTaskFromXml(ToastTaskName, BuildToastTaskXml(helperPath.wstring()))) {
        RemoveStartMenuShortcut();
        UnregisterAumid();
        return 1;
    }
    wprintf(L"Registered the '%s' Scheduled Task.\n", ToastTaskName);

    wprintf(
        L"\nNote: toast notifications require the audit-log feature (--enable-audit-log) to "
        L"also be enabled — this feature only reacts to the events that one causes Windows to "
        L"emit.\n"
        );

    return 0;
}

int RunDisableToast()
{
    if (!IsRunningElevated()) {
        wprintf(L"[ERROR] This installer must be run as Administrator.\n");
        return 1;
    }

    UnregisterScheduledTask(ToastTaskName);
    RemoveStartMenuShortcut();
    UnregisterAumid();

    wprintf(L"Removed the toast notification task, Start Menu shortcut, and AppUserModelId registration.\n");
    return 0;
}

int RunAllowProcess(const std::wstring& fullPath)
{
    if (!IsRunningElevated()) {
        wprintf(L"[ERROR] This installer must be run as Administrator.\n");
        return 1;
    }

    std::vector<std::wstring> paths = ReadToastAllowlist();
    for (const std::wstring& existing : paths) {
        if (_wcsicmp(existing.c_str(), fullPath.c_str()) == 0) {
            wprintf(L"Already allowlisted: %s\n", fullPath.c_str());
            return 0;
        }
    }

    paths.push_back(fullPath);
    if (!WriteToastAllowlist(paths)) {
        return 1;
    }
    wprintf(L"Added to the toast-suppression allowlist: %s\n", fullPath.c_str());
    return 0;
}

int RunDisallowProcess(const std::wstring& fullPath)
{
    if (!IsRunningElevated()) {
        wprintf(L"[ERROR] This installer must be run as Administrator.\n");
        return 1;
    }

    std::vector<std::wstring> paths = ReadToastAllowlist();
    size_t before = paths.size();
    paths.erase(
        std::remove_if(paths.begin(), paths.end(), [&fullPath](const std::wstring& existing) {
            return _wcsicmp(existing.c_str(), fullPath.c_str()) == 0;
        }),
        paths.end()
        );

    if (paths.size() == before) {
        wprintf(L"Not in the allowlist: %s\n", fullPath.c_str());
        return 0;
    }

    if (!WriteToastAllowlist(paths)) {
        return 1;
    }
    wprintf(L"Removed from the toast-suppression allowlist: %s\n", fullPath.c_str());
    return 0;
}

int RunListAllowedProcesses()
{
    std::vector<std::wstring> paths = ReadToastAllowlist();
    if (paths.empty()) {
        wprintf(L"The toast-suppression allowlist is empty.\n");
        return 0;
    }

    wprintf(L"Toast-suppression allowlist (%zu entr%s):\n", paths.size(), paths.size() == 1 ? L"y" : L"ies");
    for (const std::wstring& path : paths) {
        wprintf(L"  %s\n", path.c_str());
    }
    return 0;
}

} // namespace OpenInputBridge
