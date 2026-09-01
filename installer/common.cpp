// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// See common.h.

#include "common.h"

#include <wincrypt.h>

#include <string>
#include <vector>
#include <cstdlib>
#include <cwctype>
#include <cstdio>
#include <filesystem>
#include <fstream>

#pragma comment(lib, "Crypt32.lib")

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

    constexpr unsigned long kMinimumSupportedBuildNumber = 22000; // Windows 11, original release.
    return wcstoul(buildNumberText, nullptr, 10) >= kMinimumSupportedBuildNumber;
}

DriverSignatureLevel GetDriverSignatureLevel(const std::wstring& catalogPath)
{
    DWORD encoding = 0;
    DWORD contentType = 0;
    DWORD formatType = 0;
    HCERTSTORE store = nullptr;
    HCRYPTMSG msg = nullptr;

    BOOL queried = CryptQueryObject(
        CERT_QUERY_OBJECT_FILE, catalogPath.c_str(),
        CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED, CERT_QUERY_FORMAT_FLAG_BINARY, 0,
        &encoding, &contentType, &formatType, &store, &msg, nullptr
        );

    if (!queried || store == nullptr) {
        if (store != nullptr) {
            CertCloseStore(store, 0);
        }
        if (msg != nullptr) {
            CryptMsgClose(msg);
        }
        return DriverSignatureLevel::Unsigned;
    }

    // A WHQL/HLK cross-signed catalog's own signature embeds a certificate whose subject
    // contains this well-known name -- checking the certificates CryptQueryObject already
    // pulled out of the signature (no chain-building, no network/AIA lookups) is sufficient to
    // tell it apart from a plain EV or local test certificate.
    const wchar_t whqlSubjectSubstring[] = L"Windows Hardware Compatibility Publisher";
    bool foundWhql = false;

    PCCERT_CONTEXT cert = nullptr;
    while ((cert = CertEnumCertificatesInStore(store, cert)) != nullptr) {
        wchar_t subjectName[512]{};
        CertGetNameStringW(
            cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr,
            subjectName, static_cast<DWORD>(std::size(subjectName))
            );
        if (wcsstr(subjectName, whqlSubjectSubstring) != nullptr) {
            foundWhql = true;
            CertFreeCertificateContext(cert); // breaking out early -- not auto-freed by the next enum call.
            break;
        }
    }

    CertCloseStore(store, 0);
    CryptMsgClose(msg);

    return foundWhql ? DriverSignatureLevel::Whql : DriverSignatureLevel::NonWhql;
}

bool IsTestSigningEnabled()
{
    struct SystemCodeIntegrityInformationT {
        ULONG Length;
        ULONG CodeIntegrityOptions;
    };
    constexpr ULONG kSystemCodeIntegrityInformationClass = 103;
    constexpr ULONG kCodeIntegrityOptionTestSign = 0x00000002;

    using NtQuerySystemInformationProc = LONG(WINAPI*)(ULONG, PVOID, ULONG, PULONG);

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        return false;
    }

    auto queryProc = reinterpret_cast<NtQuerySystemInformationProc>(
        GetProcAddress(ntdll, "NtQuerySystemInformation"));
    if (queryProc == nullptr) {
        return false;
    }

    SystemCodeIntegrityInformationT info{ sizeof(info), 0 };
    ULONG returnLength = 0;
    LONG status = queryProc(kSystemCodeIntegrityInformationClass, &info, sizeof(info), &returnLength);

    return status >= 0 && (info.CodeIntegrityOptions & kCodeIntegrityOptionTestSign) != 0;
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

} // namespace

bool VerifyDriverFilterIntegrity(const DriverInfo& driver)
{
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
        L"after the next reboot, so the filter registration has been removed.\n",
        driver.ServiceName, driver.InsertBeforeClassDriver
        );

    ModifyUpperFilters(driver.ClassGuidString, driver.ServiceName, false, driver.InsertBeforeClassDriver);
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
