// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// OibToastHelper.exe: shows a single Windows toast notification, labeled "OpenInputBridge", then
// exits. Invoked by the Scheduled Task installer/toastsetup.cpp registers, once per
// \\.\interceptionNN open the audit-log feature (installer/auditlog.cpp) causes Windows to
// record. See docs/DECISIONS.md's 2026-08-08 entry for why this is a small standalone native
// helper using WinRT toast APIs directly via their raw COM/ABI headers (not C++/WinRT, not a
// NuGet-distributed package, not PowerShell) — it needs no build-time dependency beyond Windows
// SDK headers already used elsewhere in this repo, and gets a dedicated, properly-branded AUMID
// rather than borrowing e.g. PowerShell's own.
//
// Usage: OibToastHelper.exe --object-name <NT device path> --process-id <pid> --user <user>
// (all three supplied by the Scheduled Task's ValueQueries — see toastsetup.cpp's
// BuildToastTaskXml.)
//
// --object-name is checked against "\Device\interception" here, not in the Scheduled Task's
// event-trigger XPath, so this exits silently (not an error) if invoked for some unrelated
// audited object — see toastsetup.cpp's BuildToastTaskXml for why the trigger itself can't do
// this filtering.
//
// Builds with <SubSystem>Windows</SubSystem> + wmainCRTStartup (see the .vcxproj) so it never
// flashes a console window when Task Scheduler launches it — it has no need for one either way.

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <roapi.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>
#include <windows.data.xml.dom.h>
#include <windows.ui.notifications.h>

#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "runtimeobject.lib")
#pragma comment(lib, "Shell32.lib")

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;
using ABI::Windows::Data::Xml::Dom::IXmlDocument;
using ABI::Windows::Data::Xml::Dom::IXmlDocumentIO;
using ABI::Windows::UI::Notifications::IToastNotification;
using ABI::Windows::UI::Notifications::IToastNotificationFactory;
using ABI::Windows::UI::Notifications::IToastNotificationManagerStatics;
using ABI::Windows::UI::Notifications::IToastNotifier;

namespace {

// Must match installer/toastsetup.h's ToastAppUserModelId exactly — see that header's comment
// on why this and other same-repo-but-separately-built executables keep such constants in sync
// as separate, commented definitions rather than sharing a header.
const wchar_t kAppUserModelId[] = L"OpenInputBridge.AuditNotifier";

const wchar_t kDeviceObjectPrefix[] = L"\\Device\\interception";

// Must match installer/toastsetup.cpp's own copy of this string exactly — see kAppUserModelId's
// comment above for why constants shared across the installer and this separately-built helper
// are kept in sync as separate, commented definitions rather than via a shared header.
const wchar_t kRevealProtocolName[] = L"oib-reveal";

struct Args {
    std::wstring objectName;
    std::wstring processId;
    std::wstring processName;
    std::wstring user;
};

Args ParseArgs(int argc, wchar_t* argv[])
{
    Args args;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (_wcsicmp(argv[i], L"--object-name") == 0) {
            args.objectName = argv[i + 1];
        } else if (_wcsicmp(argv[i], L"--process-id") == 0) {
            args.processId = argv[i + 1];
        } else if (_wcsicmp(argv[i], L"--process-name") == 0) {
            args.processName = argv[i + 1];
        } else if (_wcsicmp(argv[i], L"--user") == 0) {
            args.user = argv[i + 1];
        }
    }
    return args;
}

struct ProcessInfo {
    std::wstring displayName;
    // Full path, when known (empty in the PID-fallback path if QueryFullProcessImageNameW also
    // fails) — used only for the allowlist check below, which is deliberately full-path-based
    // rather than filename-based (see IsProcessAllowlisted's comment).
    std::wstring fullPath;
    // True when the accessing process is OpenInputBridgeSetup.exe itself. Its own
    // --enable-audit-log / --apply-audit-sacl (installer/auditlog.cpp, run once at setup and
    // again on every boot via the reapply Scheduled Task) opens every \\.\interceptionNN device
    // to (re)apply the SACL — and that open matches the very audit criteria it just set, so it
    // self-triggers a 4656 for each of the (up to 20) devices, every single time. That's
    // routine self-maintenance, not a genuine "something is using the interception protocol"
    // event worth a notification, so wmain skips showing a toast for it.
    bool isOwnInstaller = false;
};

// Resolves display info for the accessing process. Prefers the event's own ProcessName field
// (passed in directly, via toastsetup.cpp's ValueQueries) over re-querying by PID: real-machine
// testing found the triggering process — especially OpenInputBridgeSetup.exe itself, which
// completes a whole --enable-audit-log/--disable-audit-log/--apply-audit-sacl run (all 20
// devices) in well under a second — has very often already exited by the time this task's
// Action actually runs, making a PID-based OpenProcess() lookup a race. That race is exactly
// why the isOwnInstaller self-noise filter below used to fail open unpredictably (sometimes
// catching OpenInputBridgeSetup.exe's own accesses, sometimes not, depending on scheduling
// luck) when it depended on that lookup succeeding. See docs/DECISIONS.md's 2026-08-10 entry.
ProcessInfo ResolveProcessInfo(const std::wstring& processIdText, const std::wstring& processNameText)
{
    ProcessInfo info;

    if (!processNameText.empty()) {
        info.fullPath = processNameText;
        info.displayName = std::filesystem::path(processNameText).filename().wstring();
        // Both the x64 and native ARM64 installer exe (installer/OpenInputBridgeSetup_arm64.vcxproj)
        // run this same self-maintenance --apply-audit-sacl pass — see docs/DECISIONS.md's ARM64
        // entry for why a native ARM64 build exists at all.
        info.isOwnInstaller =
            _wcsicmp(info.displayName.c_str(), L"OpenInputBridgeSetup.exe") == 0 ||
            _wcsicmp(info.displayName.c_str(), L"OpenInputBridgeSetup-arm64.exe") == 0;
        return info;
    }

    // Fallback only: the manifest-based Security-Auditing provider is expected to always supply
    // ProcessName, so this path shouldn't normally be reached. Best-effort, and — since the
    // triggering process may already be gone by now — this is the one case isOwnInstaller can't
    // be determined reliably; it's left false (show the toast) rather than guessed.
    unsigned long pid = wcstoul(processIdText.c_str(), nullptr, 10);
    if (pid == 0) {
        info.displayName = L"unknown process";
        return info;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        info.displayName = L"PID " + processIdText;
        return info;
    }

    wchar_t imagePath[MAX_PATH];
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(process, 0, imagePath, &size)) {
        info.fullPath = imagePath;
        info.displayName = std::filesystem::path(imagePath).filename().wstring();
    } else {
        info.displayName = L"PID " + processIdText;
    }

    CloseHandle(process);
    return info;
}

// Must match installer/toastsetup.cpp's own copies of these two strings exactly — see
// kAppUserModelId's comment above for why constants shared across the installer and this
// separately-built helper are kept in sync as separate, commented definitions rather than via a
// shared header.
const wchar_t kToastAllowlistKeyPath[] = L"SOFTWARE\\OpenInputBridge";
const wchar_t kToastAllowlistValueName[] = L"ToastAllowedProcessPaths";

// Full-path match against the toast-suppression allowlist installer/toastsetup.cpp's
// --allow-process manages (HKLM\SOFTWARE\OpenInputBridge\ToastAllowedProcessPaths, REG_MULTI_SZ).
// Full path rather than filename-only, unlike isOwnInstaller above: that check exists purely to
// silence this program's own routine self-maintenance noise (a fixed, known filename is fine for
// that), while this one lets an administrator opt arbitrary third-party software out of
// notifications, where matching by filename alone would let anything sharing that name — not
// just the specific trusted executable — go unnotified too. Suppresses only the toast, not the
// underlying Security-event-log record, which is unaffected either way. Returns false (i.e. "no
// path to compare, don't suppress") if fullPath is empty — the PID-fallback path in
// ResolveProcessInfo can leave it that way.
bool IsProcessAllowlisted(const std::wstring& fullPath)
{
    if (fullPath.empty()) {
        return false;
    }

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kToastAllowlistKeyPath, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD sizeBytes = 0;
    bool allowlisted = false;
    if (RegQueryValueExW(key, kToastAllowlistValueName, nullptr, &type, nullptr, &sizeBytes) == ERROR_SUCCESS &&
        type == REG_MULTI_SZ && sizeBytes > 0) {
        std::vector<wchar_t> buffer(sizeBytes / sizeof(wchar_t));
        if (RegQueryValueExW(
                key, kToastAllowlistValueName, nullptr, nullptr,
                reinterpret_cast<BYTE*>(buffer.data()), &sizeBytes
                ) == ERROR_SUCCESS) {
            for (const wchar_t* p = buffer.data(); *p != L'\0'; p += wcslen(p) + 1) {
                if (_wcsicmp(p, fullPath.c_str()) == 0) {
                    allowlisted = true;
                    break;
                }
            }
        }
    }

    RegCloseKey(key);
    return allowlisted;
}

std::wstring XmlEscape(const std::wstring& text)
{
    std::wstring escaped;
    escaped.reserve(text.size());
    for (wchar_t ch : text) {
        switch (ch) {
            case L'&': escaped += L"&amp;"; break;
            case L'<': escaped += L"&lt;"; break;
            case L'>': escaped += L"&gt;"; break;
            case L'"': escaped += L"&quot;"; break;
            default: escaped += ch; break;
        }
    }
    return escaped;
}

// Percent-encodes text (first converted to UTF-8, then each non-unreserved byte encoded
// individually — the standard way to percent-encode text that may contain non-ASCII
// characters, e.g. a path under a non-English username) for embedding in the toast's "launch"
// URI (see ShowToast). Only this program's own UriDecode below ever has to parse the result
// (via oib-reveal:'s registered command, --reveal), so exact RFC 3986 conformance doesn't
// matter here — round-tripping correctly through both does.
std::wstring UriEncode(const std::wstring& text)
{
    int utf8Length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8Length <= 0) {
        return {};
    }
    std::vector<char> utf8(utf8Length);
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, utf8.data(), utf8Length, nullptr, nullptr);

    std::wstring encoded;
    for (int i = 0; i + 1 < utf8Length; ++i) { // -1: WideCharToMultiByte's trailing null terminator.
        unsigned char byte = static_cast<unsigned char>(utf8[i]);
        bool isUnreserved =
            (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
            (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' || byte == '.' || byte == '~';
        if (isUnreserved) {
            encoded += static_cast<wchar_t>(byte);
        } else {
            wchar_t buffer[4];
            swprintf_s(buffer, L"%%%02X", byte);
            encoded += buffer;
        }
    }
    return encoded;
}

std::wstring UriDecode(const std::wstring& text)
{
    std::string utf8Bytes;
    utf8Bytes.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'%' && i + 2 < text.size()) {
            wchar_t hex[3] = { text[i + 1], text[i + 2], 0 };
            utf8Bytes += static_cast<char>(wcstoul(hex, nullptr, 16));
            i += 2;
        } else {
            utf8Bytes += static_cast<char>(text[i]);
        }
    }

    int wideLength = MultiByteToWideChar(CP_UTF8, 0, utf8Bytes.c_str(), -1, nullptr, 0);
    if (wideLength <= 0) {
        return {};
    }
    std::vector<wchar_t> wide(wideLength);
    MultiByteToWideChar(CP_UTF8, 0, utf8Bytes.c_str(), -1, wide.data(), wideLength);
    return wide.data();
}

// Opens Explorer with path selected within its containing folder (clicking a toast's body —
// see ShowToast's "launch" URI and RegisterRevealProtocol in toastsetup.cpp) rather than a
// plain "open the folder" — lets whoever's reading the notification jump straight to
// inspecting the actual accessing binary. Uses an absolute path to explorer.exe (like
// installer/common.cpp's RunSystem32Tool) rather than relying on PATH search.
void RevealInExplorer(const std::wstring& path)
{
    wchar_t windowsDirectory[MAX_PATH];
    GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
    std::wstring explorerPath = std::wstring(windowsDirectory) + L"\\explorer.exe";

    std::wstring arguments = L"/select,\"" + path + L"\"";
    ShellExecuteW(nullptr, L"open", explorerPath.c_str(), arguments.c_str(), nullptr, SW_SHOWNORMAL);
}

const wchar_t kDebounceMutexName[] = L"Local\\OpenInputBridge.ToastDebounce";
constexpr ULONGLONG kDebounceWindow100ns = 3ULL * 10'000'000; // 3 seconds, in FILETIME units.

#pragma pack(push, 1)
struct DebounceRecord {
    ULONGLONG timestamp100ns;
    wchar_t processName[MAX_PATH];
};
#pragma pack(pop)

std::filesystem::path GetDebounceMarkerPath()
{
    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData))) {
        return {};
    }
    std::filesystem::path dir = std::filesystem::path(localAppData) / L"OpenInputBridge";
    CoTaskMemFree(localAppData);

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir / L"toast_debounce.dat";
}

// A real Interception-protocol client opens all ~20 control devices essentially simultaneously
// at startup (interception_create_context() loops CreateFileA over every slot) — each open is
// audited separately, so without this, one client launch produces one toast per device instead
// of one toast total (confirmed on real hardware: 20 toasts for a single identify2.exe launch).
// This collapses same-process bursts within a short window into a single toast by persisting
// "which process, when" was last shown across the separate OibToastHelper.exe invocations the
// Scheduled Task spawns per event (this process has no memory of its own — each event is a
// fresh process). Cross-process races between near-simultaneous invocations are resolved with a
// named mutex; a small residual race (e.g. the mutex wait timing out) is acceptable here since
// the worst case is one extra toast, not a correctness problem for a notification feature. See
// docs/DECISIONS.md's 2026-08-10 entry.
bool ShouldSuppressAsDuplicate(const std::wstring& processName)
{
    HANDLE mutex = CreateMutexW(nullptr, FALSE, kDebounceMutexName);
    bool haveMutex = mutex != nullptr && WaitForSingleObject(mutex, 2000) == WAIT_OBJECT_0;

    std::filesystem::path markerPath = GetDebounceMarkerPath();
    bool suppress = false;

    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER nowValue{};
    nowValue.LowPart = now.dwLowDateTime;
    nowValue.HighPart = now.dwHighDateTime;

    if (!markerPath.empty()) {
        DebounceRecord record{};
        std::ifstream in(markerPath, std::ios::binary);
        if (in.read(reinterpret_cast<char*>(&record), sizeof(record))) {
            record.processName[MAX_PATH - 1] = L'\0';
            ULONGLONG elapsed = nowValue.QuadPart >= record.timestamp100ns
                ? nowValue.QuadPart - record.timestamp100ns
                : 0;
            if (elapsed < kDebounceWindow100ns && _wcsicmp(record.processName, processName.c_str()) == 0) {
                suppress = true;
            }
        }
    }

    if (!suppress && !markerPath.empty()) {
        DebounceRecord record{};
        record.timestamp100ns = nowValue.QuadPart;
        wcsncpy_s(record.processName, processName.c_str(), _TRUNCATE);
        std::ofstream out(markerPath, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(&record), sizeof(record));
    }

    if (haveMutex) {
        ReleaseMutex(mutex);
    }
    if (mutex != nullptr) {
        CloseHandle(mutex);
    }

    return suppress;
}

// RoActivateInstance/RoGetActivationFactory both hand back an IInspectable*; the ABI:: headers'
// interfaces are declared via MIDL_INTERFACE (so __uuidof works on them without a separate .lib
// of named IID_* constants), which is what lets ComPtr::As/IID_PPV_ARGS resolve purely from the
// template type below.
template <typename T>
HRESULT ActivateInstance(const wchar_t* runtimeClassName, ComPtr<T>& instance)
{
    ComPtr<IInspectable> inspectable;
    HRESULT hr = RoActivateInstance(HStringReference(runtimeClassName).Get(), &inspectable);
    if (FAILED(hr)) {
        return hr;
    }
    return inspectable.As(&instance);
}

// processFullPath, when known, makes the toast's body clickable: it opens Explorer with the
// accessing process's binary selected (RevealInExplorer, via the oib-reveal: protocol
// RegisterRevealProtocol in toastsetup.cpp registers). Left empty (no launch/activationType
// attributes added — an ordinary, non-clickable toast) when it isn't: the PID-fallback path in
// ResolveProcessInfo can leave ProcessInfo::fullPath empty, and there's nothing to reveal then.
HRESULT ShowToast(const std::wstring& deviceName, const std::wstring& processName, const std::wstring& userName, const std::wstring& processFullPath)
{
    std::wstring launchAttributes;
    if (!processFullPath.empty()) {
        std::wstring launchUri = std::wstring(kRevealProtocolName) + L":" + UriEncode(processFullPath);
        launchAttributes = L" launch=\"" + XmlEscape(launchUri) + L"\" activationType=\"protocol\"";
    }

    std::wstring toastXml =
        L"<toast" + launchAttributes + L"><visual><binding template=\"ToastGeneric\">"
        L"<text>OpenInputBridge</text>"
        L"<text>" + XmlEscape(deviceName) + L" was opened by " + XmlEscape(processName) +
        L" (" + XmlEscape(userName) + L")</text>"
        L"</binding></visual></toast>";

    ComPtr<IXmlDocument> xmlDocument;
    HRESULT hr = ActivateInstance(RuntimeClass_Windows_Data_Xml_Dom_XmlDocument, xmlDocument);
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<IXmlDocumentIO> xmlDocumentIo;
    hr = xmlDocument.As(&xmlDocumentIo);
    if (FAILED(hr)) {
        return hr;
    }

    hr = xmlDocumentIo->LoadXml(HStringReference(toastXml.c_str()).Get());
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<IToastNotificationFactory> toastFactory;
    hr = RoGetActivationFactory(
        HStringReference(RuntimeClass_Windows_UI_Notifications_ToastNotification).Get(),
        IID_PPV_ARGS(&toastFactory)
        );
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<IToastNotification> toast;
    hr = toastFactory->CreateToastNotification(xmlDocument.Get(), &toast);
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<IToastNotificationManagerStatics> toastStatics;
    hr = RoGetActivationFactory(
        HStringReference(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager).Get(),
        IID_PPV_ARGS(&toastStatics)
        );
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<IToastNotifier> notifier;
    hr = toastStatics->CreateToastNotifierWithId(HStringReference(kAppUserModelId).Get(), &notifier);
    if (FAILED(hr)) {
        return hr;
    }

    return notifier->Show(toast.Get());
}

} // namespace

int wmain(int argc, wchar_t* argv[])
{
    // A completely separate invocation mode: Windows launches this program with these two
    // arguments (see toastsetup.cpp's RegisterRevealProtocol) when the user clicks a toast's
    // body — the full oib-reveal:<encoded path> URI arrives as argv[2] verbatim, prefix
    // included, exactly as registered in the protocol's command string. Never reaches
    // ParseArgs/ShowToast below; this either reveals the file or does nothing.
    if (argc >= 3 && _wcsicmp(argv[1], L"--reveal") == 0) {
        std::wstring uri = argv[2];
        std::wstring prefix = std::wstring(kRevealProtocolName) + L":";
        if (_wcsnicmp(uri.c_str(), prefix.c_str(), prefix.size()) == 0) {
            RevealInExplorer(UriDecode(uri.substr(prefix.size())));
        }
        return 0;
    }

    Args args = ParseArgs(argc, argv);

    if (args.objectName.empty() ||
        _wcsnicmp(args.objectName.c_str(), kDeviceObjectPrefix, wcslen(kDeviceObjectPrefix)) != 0) {
        return 0;
    }

    ProcessInfo processInfo = ResolveProcessInfo(args.processId, args.processName);
    if (processInfo.isOwnInstaller) {
        return 0;
    }

    if (IsProcessAllowlisted(processInfo.fullPath)) {
        return 0;
    }

    if (ShouldSuppressAsDuplicate(processInfo.displayName)) {
        return 0;
    }

    // Required before showing a toast from a non-packaged desktop app under our own AUMID —
    // see toastsetup.h's file header comment for the rest of what registers that AUMID
    // (registry DisplayName/IconUri + a Start Menu shortcut, both set up by the WiX installer
    // projects, not by this helper).
    SetCurrentProcessExplicitAppUserModelID(kAppUserModelId);

    if (FAILED(RoInitialize(RO_INIT_MULTITHREADED))) {
        return 1;
    }

    HRESULT hr = ShowToast(args.objectName, processInfo.displayName, args.user, processInfo.fullPath);

    RoUninitialize();
    return SUCCEEDED(hr) ? 0 : 1;
}
