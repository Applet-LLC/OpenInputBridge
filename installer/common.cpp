// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// See common.h.

#include "common.h"

#include <string>
#include <vector>
#include <cstdlib>
#include <cwctype>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace OpenInputBridge {

const DriverInfo& GetDriverInfo(DriverType type)
{
    static const DriverInfo kKeyboard = {
        L"oib_kbd",
        L"OpenInputBridgeKeyboard",
        L"{4D36E96B-E325-11CE-BFC1-08002BE10318}",
        L"kbdclass",
    };
    static const DriverInfo kMouse = {
        L"oib_mou",
        L"OpenInputBridgeMouse",
        L"{4D36E96F-E325-11CE-BFC1-08002BE10318}",
        L"mouclass",
    };

    return (type == DriverType::Keyboard) ? kKeyboard : kMouse;
}

bool IsRunningElevated()
{
    HANDLE token = nullptr;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    TOKEN_ELEVATION elevation{};
    DWORD returnedSize = 0;
    BOOL isElevated = FALSE;

    if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &returnedSize)) {
        isElevated = elevation.TokenIsElevated;
    }

    CloseHandle(token);
    return isElevated != FALSE;
}

bool IsSupportedWindowsEnvironment()
{
    SYSTEM_INFO systemInfo{};
    GetNativeSystemInfo(&systemInfo); // native architecture even if this process runs under emulation.
    if (systemInfo.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_AMD64) {
        return false;
    }

    HKEY key = nullptr;
    if (RegOpenKeyExW(
            HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0,
            KEY_QUERY_VALUE, &key
            ) != ERROR_SUCCESS) {
        return false;
    }

    wchar_t buildNumberText[32]{};
    DWORD size = sizeof(buildNumberText);
    DWORD type = 0;
    LONG result = RegQueryValueExW(
        key, L"CurrentBuildNumber", nullptr, &type,
        reinterpret_cast<BYTE*>(buildNumberText), &size
        );
    RegCloseKey(key);

    if (result != ERROR_SUCCESS || type != REG_SZ) {
        return false;
    }

    constexpr unsigned long kMinimumSupportedBuildNumber = 18362; // Windows 10 1903.
    return wcstoul(buildNumberText, nullptr, 10) >= kMinimumSupportedBuildNumber;
}

bool ServiceExists(const wchar_t* serviceName)
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        return false;
    }

    SC_HANDLE service = OpenServiceW(scm, serviceName, SERVICE_QUERY_STATUS);
    bool exists = service != nullptr;

    if (service != nullptr) {
        CloseServiceHandle(service);
    }
    CloseServiceHandle(scm);
    return exists;
}

bool StopAndWaitService(const wchar_t* serviceName, DWORD timeoutMs)
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        return false;
    }

    SC_HANDLE service = OpenServiceW(scm, serviceName, SERVICE_QUERY_STATUS | SERVICE_STOP);
    if (service == nullptr) {
        // Not registered at all: nothing to stop.
        CloseServiceHandle(scm);
        return true;
    }

    SERVICE_STATUS status{};
    bool stopped = false;

    if (QueryServiceStatus(service, &status) && status.dwCurrentState == SERVICE_STOPPED) {
        stopped = true;
    } else if (ControlService(service, SERVICE_CONTROL_STOP, &status) || GetLastError() == ERROR_SERVICE_NOT_ACTIVE) {
        const DWORD pollIntervalMs = 200;
        DWORD waited = 0;

        while (waited <= timeoutMs) {
            if (!QueryServiceStatus(service, &status) || status.dwCurrentState == SERVICE_STOPPED) {
                stopped = true;
                break;
            }
            Sleep(pollIntervalMs);
            waited += pollIntervalMs;
        }
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return stopped;
}

namespace {

inline constexpr wchar_t UpperFiltersValueNameInternal[] = L"UpperFilters";

// Parses a REG_MULTI_SZ buffer (a sequence of null-terminated strings, itself terminated by
// an extra empty string / double null) into individual entries.
std::vector<std::wstring> ReadMultiSz(HKEY key, const wchar_t* valueName)
{
    std::vector<std::wstring> entries;
    DWORD type = 0;
    DWORD byteSize = 0;

    if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &byteSize) != ERROR_SUCCESS ||
        type != REG_MULTI_SZ || byteSize < sizeof(wchar_t)) {
        return entries;
    }

    std::vector<wchar_t> buffer(byteSize / sizeof(wchar_t));

    if (RegQueryValueExW(
            key, valueName, nullptr, &type,
            reinterpret_cast<LPBYTE>(buffer.data()), &byteSize
            ) != ERROR_SUCCESS) {
        return entries;
    }

    const wchar_t* current = buffer.data();
    const wchar_t* end = buffer.data() + buffer.size();

    while (current < end && *current != L'\0') {
        std::wstring entry(current);
        current += entry.size() + 1;
        entries.push_back(std::move(entry));
    }

    return entries;
}

bool WriteMultiSz(HKEY key, const wchar_t* valueName, const std::vector<std::wstring>& entries)
{
    std::vector<wchar_t> buffer;

    for (const std::wstring& entry : entries) {
        buffer.insert(buffer.end(), entry.begin(), entry.end());
        buffer.push_back(L'\0');
    }
    buffer.push_back(L'\0'); // final double-null terminator, even for an empty list.

    LONG result = RegSetValueExW(
        key,
        valueName,
        0,
        REG_MULTI_SZ,
        reinterpret_cast<const BYTE*>(buffer.data()),
        static_cast<DWORD>(buffer.size() * sizeof(wchar_t))
        );

    return result == ERROR_SUCCESS;
}

bool EqualsCaseInsensitive(const std::wstring& a, const wchar_t* b)
{
    return _wcsicmp(a.c_str(), b) == 0;
}

bool ValueExists(HKEY key, const wchar_t* valueName)
{
    return RegQueryValueExW(key, valueName, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

// Instance subkeys under a class key are 4-digit zero-padded decimal numbers ("0000", "0001",
// ...), as opposed to the class key's other, non-numeric subkeys (e.g. "Properties").
bool IsInstanceSubkeyName(const wchar_t* name)
{
    size_t length = wcslen(name);
    if (length != 4) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        if (iswdigit(name[i]) == 0) {
            return false;
        }
    }
    return true;
}

// Applies the insert-before-entryName / remove-entryName edit described in common.h to a
// single already-open registry key's UpperFilters value. Returns false only on an actual
// registry write failure; a no-op edit (nothing to change) returns true without writing.
bool ModifyUpperFiltersAtKey(HKEY key, const wchar_t* entryName, bool add, const wchar_t* insertBeforeName)
{
    std::vector<std::wstring> entries = ReadMultiSz(key, UpperFiltersValueNameInternal);

    std::vector<std::wstring> withoutEntry;
    withoutEntry.reserve(entries.size());
    for (std::wstring& entry : entries) {
        if (!EqualsCaseInsensitive(entry, entryName)) {
            withoutEntry.push_back(std::move(entry));
        }
    }

    std::vector<std::wstring> result;
    if (add) {
        result.reserve(withoutEntry.size() + 1);
        bool inserted = false;
        for (std::wstring& entry : withoutEntry) {
            if (!inserted && EqualsCaseInsensitive(entry, insertBeforeName)) {
                result.emplace_back(entryName);
                inserted = true;
            }
            result.push_back(std::move(entry));
        }
        if (!inserted) {
            result.insert(result.begin(), entryName);
        }
    } else {
        result = std::move(withoutEntry);
    }

    if (result.size() == entries.size()) {
        // Re-check whether anything actually differs (add can be a no-op if entryName was
        // already immediately before insertBeforeName).
        bool unchanged = true;
        for (size_t i = 0; i < result.size(); ++i) {
            if (!EqualsCaseInsensitive(result[i], entries[i].c_str())) {
                unchanged = false;
                break;
            }
        }
        if (unchanged) {
            return true;
        }
    }

    return WriteMultiSz(key, UpperFiltersValueNameInternal, result);
}

} // namespace

bool ModifyUpperFilters(const wchar_t* classGuidString, const wchar_t* entryName, bool add, const wchar_t* insertBeforeName)
{
    std::wstring classKeyPath =
        std::wstring(L"SYSTEM\\CurrentControlSet\\Control\\Class\\") + classGuidString;

    HKEY classKey = nullptr;
    LONG openResult = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE, classKeyPath.c_str(), 0,
        KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_ENUMERATE_SUB_KEYS, &classKey
        );

    if (openResult != ERROR_SUCCESS) {
        // Class key not present at all: nothing to remove; for add, this would be an
        // unexpected system configuration (every Windows install has both classes).
        return !add;
    }

    bool succeeded = ModifyUpperFiltersAtKey(classKey, entryName, add, insertBeforeName);

    wchar_t subkeyName[MAX_PATH];
    for (DWORD index = 0; ; ++index) {
        DWORD nameLength = MAX_PATH;
        LONG enumResult = RegEnumKeyExW(classKey, index, subkeyName, &nameLength, nullptr, nullptr, nullptr, nullptr);

        if (enumResult == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (enumResult != ERROR_SUCCESS) {
            succeeded = false;
            break;
        }
        if (!IsInstanceSubkeyName(subkeyName)) {
            continue;
        }

        HKEY instanceKey = nullptr;
        if (RegOpenKeyExW(classKey, subkeyName, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &instanceKey) != ERROR_SUCCESS) {
            continue;
        }

        // Only touch the instance subkey if it already carries its own UpperFilters override;
        // otherwise it inherits the class-level default we just updated above, and creating a
        // new value here would needlessly pin it out of sync with future class-level changes.
        if (ValueExists(instanceKey, UpperFiltersValueNameInternal)) {
            succeeded = ModifyUpperFiltersAtKey(instanceKey, entryName, add, insertBeforeName) && succeeded;
        }

        RegCloseKey(instanceKey);
    }

    RegCloseKey(classKey);
    return succeeded;
}

bool IsRegisteredAsUpperFilter(const wchar_t* classGuidString, const wchar_t* entryName)
{
    std::wstring classKeyPath =
        std::wstring(L"SYSTEM\\CurrentControlSet\\Control\\Class\\") + classGuidString;

    HKEY classKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, classKeyPath.c_str(), 0, KEY_QUERY_VALUE, &classKey) != ERROR_SUCCESS) {
        return false;
    }

    std::vector<std::wstring> entries = ReadMultiSz(classKey, UpperFiltersValueNameInternal);
    RegCloseKey(classKey);

    for (const std::wstring& entry : entries) {
        if (EqualsCaseInsensitive(entry, entryName)) {
            return true;
        }
    }
    return false;
}

bool SetKeyboardSlotCount(DriverType type, ULONG keyboardSlotCount)
{
    std::wstring parametersKeyPath =
        std::wstring(L"SYSTEM\\CurrentControlSet\\Services\\") + GetDriverInfo(type).ServiceName + L"\\Parameters";

    HKEY parametersKey = nullptr;
    LONG result = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE, parametersKeyPath.c_str(), 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &parametersKey, nullptr
        );

    if (result != ERROR_SUCCESS) {
        return false;
    }

    result = RegSetValueExW(
        parametersKey, KeyboardSlotCountValueName, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&keyboardSlotCount), sizeof(keyboardSlotCount)
        );

    RegCloseKey(parametersKey);
    return result == ERROR_SUCCESS;
}

bool TryGetKeyboardSlotCount(DriverType type, ULONG& outKeyboardSlotCount)
{
    std::wstring parametersKeyPath =
        std::wstring(L"SYSTEM\\CurrentControlSet\\Services\\") + GetDriverInfo(type).ServiceName + L"\\Parameters";

    HKEY parametersKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, parametersKeyPath.c_str(), 0, KEY_QUERY_VALUE, &parametersKey) != ERROR_SUCCESS) {
        return false;
    }

    ULONG value = 0;
    DWORD size = sizeof(value);
    DWORD type_ = 0;
    LONG result = RegQueryValueExW(
        parametersKey, KeyboardSlotCountValueName, nullptr, &type_,
        reinterpret_cast<LPBYTE>(&value), &size
        );

    RegCloseKey(parametersKey);

    if (result != ERROR_SUCCESS || type_ != REG_DWORD) {
        return false;
    }

    outKeyboardSlotCount = value;
    return true;
}

std::wstring GetInstallerExecutablePath()
{
    wchar_t modulePath[MAX_PATH];
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    return modulePath;
}

int RunSystem32Tool(const wchar_t* exeName, const std::wstring& arguments, bool suppressOutput)
{
    wchar_t systemDirectory[MAX_PATH];
    GetSystemDirectoryW(systemDirectory, MAX_PATH);

    std::wstring commandLine =
        L"\"" + std::wstring(systemDirectory) + L"\\" + exeName + L"\" " + arguments;

    STARTUPINFOW startupInfo{ sizeof(startupInfo) };
    PROCESS_INFORMATION processInfo{};

    // Only opened/inherited when suppressing: CreateFileW("NUL") needs an inheritable handle,
    // and CreateProcessW's bInheritHandles must be TRUE for the child to actually receive it —
    // neither is otherwise how this function behaves, so both are scoped to this case only.
    HANDLE nulHandle = INVALID_HANDLE_VALUE;
    if (suppressOutput) {
        SECURITY_ATTRIBUTES inheritable{ sizeof(inheritable), nullptr, TRUE };
        nulHandle = CreateFileW(
            L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable,
            OPEN_EXISTING, 0, nullptr
            );
        if (nulHandle != INVALID_HANDLE_VALUE) {
            startupInfo.dwFlags |= STARTF_USESTDHANDLES;
            startupInfo.hStdInput = nulHandle;
            startupInfo.hStdOutput = nulHandle;
            startupInfo.hStdError = nulHandle;
        }
    }

    // CreateProcessW requires a mutable command-line buffer.
    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    BOOL created = CreateProcessW(
        nullptr, mutableCommandLine.data(), nullptr, nullptr,
        nulHandle != INVALID_HANDLE_VALUE ? TRUE : FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo
        );

    if (nulHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(nulHandle);
    }

    if (!created) {
        wprintf(L"[ERROR] Failed to launch %s: %lu\n", exeName, GetLastError());
        return -1;
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    return static_cast<int>(exitCode);
}

bool RegisterScheduledTaskFromXml(const wchar_t* taskName, const std::wstring& taskXml)
{
    wchar_t tempDirectory[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDirectory);

    std::filesystem::path tempFilePath =
        std::filesystem::path(tempDirectory) / (std::wstring(taskName) + L".xml");

    {
        std::ofstream file(tempFilePath, std::ios::binary);
        if (!file) {
            wprintf(L"[ERROR] Failed to create temporary task definition file.\n");
            return false;
        }
        const unsigned char bom[] = { 0xFF, 0xFE };
        file.write(reinterpret_cast<const char*>(bom), sizeof(bom));
        file.write(reinterpret_cast<const char*>(taskXml.data()), static_cast<std::streamsize>(taskXml.size() * sizeof(wchar_t)));
    }

    std::wstring arguments =
        L"/Create /TN \"" + std::wstring(taskName) + L"\" /XML \"" + tempFilePath.wstring() + L"\" /F";
    int exitCode = RunSystem32Tool(L"schtasks.exe", arguments);

    std::error_code ec;
    std::filesystem::remove(tempFilePath, ec);

    if (exitCode != 0) {
        wprintf(L"[ERROR] schtasks /Create failed for '%s' (exit code %d).\n", taskName, exitCode);
        return false;
    }
    return true;
}

void UnregisterScheduledTask(const wchar_t* taskName)
{
    std::wstring arguments = L"/Delete /TN \"" + std::wstring(taskName) + L"\" /F";
    RunSystem32Tool(L"schtasks.exe", arguments);
}

bool ScheduledTaskExists(const wchar_t* taskName)
{
    std::wstring arguments = L"/Query /TN \"" + std::wstring(taskName) + L"\"";
    return RunSystem32Tool(L"schtasks.exe", arguments, /*suppressOutput=*/true) == 0;
}

} // namespace OpenInputBridge
