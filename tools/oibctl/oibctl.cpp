// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// OibCtl.exe: hides/shows OpenInputBridge's \\.\interceptionNN control devices while the drivers
// stay loaded (driver/common/oib_mgmt.h). Meant for running games whose anti-cheat refuses to
// start while an Interception-compatible device is present: hide before the game, show after.
//
//   OibCtl hide | show | status
//   OibCtl run <exe> [args...]                 hide, run the program, wait for it, show
//   OibCtl guard [--timeout <sec>] <name>...   hide, wait for any named process to appear and
//                                              all of them to exit, show
//   OibCtl                                     (no arguments) small dialog with the same hide/show
//
// Exit codes (hide/show/run/guard): 0 ok, 1 control devices in use by a client (nothing changed),
// 2 driver not installed or access denied, 3 other error, 4 guard timed out waiting for the
// process. `status`: 0 visible, 1 hidden, 2 partly hidden, 3 error. `run` returns the exit code
// of the program it ran once that program was started.

#include <windows.h>
#include <winioctl.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

#include "../../driver/common/oib_mgmt.h"

namespace {

enum ExitCode : int {
    kOk = 0,
    kBusy = 1,
    kNoDriver = 2,
    kError = 3,
    kTimeout = 4,
};

// Only ever set from the console control handler; waited on by run/guard.
HANDLE g_stopEvent = nullptr;

struct Target {
    const wchar_t* label;
    const wchar_t* path;
};

const Target kTargets[] = {
    {L"keyboard", OIB_KBD_MGMT_USERMODE_PATH},
    {L"mouse", OIB_MOU_MGMT_USERMODE_PATH},
};

// Result of talking to one management device.
struct DeviceResult {
    bool present = false;   // management device could be opened (driver installed and loaded)
    DWORD error = 0;        // 0, or the Win32 error of the failing step
    bool visible = false;   // only meaningful for a status query
};

HANDLE OpenTarget(const Target& target, DWORD* error)
{
    HANDLE h = CreateFileW(target.path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    *error = (h == INVALID_HANDLE_VALUE) ? GetLastError() : 0;
    return h;
}

DeviceResult SendControl(const Target& target, DWORD code)
{
    DeviceResult result;
    DWORD error = 0;
    HANDLE h = OpenTarget(target, &error);
    if (h == INVALID_HANDLE_VALUE) {
        result.error = error;
        return result;
    }
    result.present = true;

    DWORD returned = 0;
    if (!DeviceIoControl(h, code, nullptr, 0, nullptr, 0, &returned, nullptr)) {
        result.error = GetLastError();
    }
    CloseHandle(h);
    return result;
}

DeviceResult QueryVisibility(const Target& target)
{
    DeviceResult result;
    DWORD error = 0;
    HANDLE h = OpenTarget(target, &error);
    if (h == INVALID_HANDLE_VALUE) {
        result.error = error;
        return result;
    }
    result.present = true;

    OIB_VISIBILITY_INFO info = {};
    DWORD returned = 0;
    if (DeviceIoControl(h, IOCTL_OIB_GET_VISIBILITY, nullptr, 0, &info, sizeof(info), &returned, nullptr)
        && returned == sizeof(info)) {
        result.visible = info.Visible != 0;
    } else {
        result.error = GetLastError();
    }
    CloseHandle(h);
    return result;
}

bool IsMissingError(DWORD error)
{
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

// Applies Hide or Show to every installed driver. Hide is all-or-nothing: if one driver refuses
// (e.g. busy), the ones already hidden are shown again.
int Apply(bool hide, bool quiet)
{
    DeviceResult results[ARRAYSIZE(kTargets)];
    int installed = 0;
    int failure = kOk;

    for (size_t i = 0; i < ARRAYSIZE(kTargets); ++i) {
        results[i] = SendControl(kTargets[i], hide ? IOCTL_OIB_HIDE_DEVICES : IOCTL_OIB_SHOW_DEVICES);
        if (results[i].present) {
            ++installed;
        }
        if (results[i].error == 0) {
            continue;
        }
        if (!results[i].present && IsMissingError(results[i].error)) {
            continue; // that driver simply isn't installed
        }
        if (!quiet) {
            if (results[i].error == ERROR_BUSY) {
                fwprintf(stderr, L"%ls: control devices are in use. Close Interception clients "
                                 L"(AutoHotkey, kanata, ...) and try again.\n", kTargets[i].label);
            } else if (results[i].error == ERROR_ACCESS_DENIED) {
                fwprintf(stderr, L"%ls: access denied. Run as administrator.\n", kTargets[i].label);
            } else {
                fwprintf(stderr, L"%ls: failed (Win32 error %lu).\n", kTargets[i].label, results[i].error);
            }
        }
        if (failure == kOk) {
            failure = (results[i].error == ERROR_BUSY) ? kBusy
                    : (results[i].error == ERROR_ACCESS_DENIED) ? kNoDriver
                    : kError;
        }
    }

    if (installed == 0) {
        if (!quiet) {
            fwprintf(stderr, L"OpenInputBridge driver management device not found. The driver "
                             L"(version 1.03 or later) must be installed and running.\n");
        }
        return kNoDriver;
    }

    if (failure != kOk && hide) {
        for (size_t i = 0; i < ARRAYSIZE(kTargets); ++i) {
            if (results[i].present && results[i].error == 0) {
                SendControl(kTargets[i], IOCTL_OIB_SHOW_DEVICES);
            }
        }
    }

    if (failure == kOk && !quiet) {
        wprintf(L"Control devices %ls.\n", hide ? L"hidden" : L"visible");
    }
    return failure;
}

// 0 visible, 1 hidden, 2 partly, 3 error / not installed.
int Status(bool quiet)
{
    int visible = 0;
    int hidden = 0;

    for (const Target& target : kTargets) {
        DeviceResult r = QueryVisibility(target);
        if (!r.present || r.error != 0) {
            if (!quiet && !(r.error != 0 && IsMissingError(r.error))) {
                fwprintf(stderr, L"%ls: query failed (Win32 error %lu).\n", target.label, r.error);
            }
            if (!r.present && IsMissingError(r.error)) {
                if (!quiet) {
                    wprintf(L"%ls: driver not installed\n", target.label);
                }
                continue;
            }
            return 3;
        }
        if (!quiet) {
            wprintf(L"%ls: %ls\n", target.label, r.visible ? L"visible" : L"hidden");
        }
        (r.visible ? visible : hidden)++;
    }

    if (visible == 0 && hidden == 0) {
        return 3;
    }
    if (hidden == 0) {
        return 0;
    }
    return visible == 0 ? 1 : 2;
}

BOOL WINAPI ConsoleHandler(DWORD type)
{
    switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        if (g_stopEvent != nullptr) {
            SetEvent(g_stopEvent);
        }
        // The main thread restores the devices; give it a moment before the process is killed.
        Sleep(type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ? 0 : 3000);
        return TRUE;
    default:
        return FALSE;
    }
}

std::wstring QuoteArg(const std::wstring& arg)
{
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) {
        return arg;
    }
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
        } else {
            out.append(backslashes, L'\\');
        }
        backslashes = 0;
        out.push_back(c);
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

int Run(int argc, wchar_t** argv)
{
    if (argc < 3) {
        fwprintf(stderr, L"usage: OibCtl run <exe> [args...]\n");
        return kError;
    }

    int rc = Apply(true, false);
    if (rc != kOk) {
        return rc;
    }

    std::wstring commandLine;
    for (int i = 2; i < argc; ++i) {
        if (i > 2) {
            commandLine.push_back(L' ');
        }
        commandLine += QuoteArg(argv[i]);
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    int exitCode = kError;

    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        fwprintf(stderr, L"could not start the program (Win32 error %lu).\n", GetLastError());
    } else {
        CloseHandle(pi.hThread);
        HANDLE waits[2] = {pi.hProcess, g_stopEvent};
        DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        DWORD code = 0;
        if (w == WAIT_OBJECT_0 && GetExitCodeProcess(pi.hProcess, &code)) {
            exitCode = static_cast<int>(code);
        } else {
            exitCode = kOk; // interrupted by the user: the program keeps running, devices come back
        }
        CloseHandle(pi.hProcess);
    }

    Apply(false, false);
    return exitCode;
}

std::wstring NormalizeName(std::wstring name)
{
    for (wchar_t& c : name) {
        c = static_cast<wchar_t>(towlower(c));
    }
    if (name.size() < 4 || name.compare(name.size() - 4, 4, L".exe") != 0) {
        name += L".exe";
    }
    return name;
}

bool AnyProcessRunning(const std::vector<std::wstring>& names)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return false;
    }
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snap, &entry)) {
        do {
            std::wstring exe = NormalizeName(entry.szExeFile);
            for (const std::wstring& n : names) {
                if (exe == n) {
                    found = true;
                    break;
                }
            }
        } while (!found && Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return found;
}

int Guard(int argc, wchar_t** argv)
{
    DWORD timeoutSec = 0; // 0 = wait forever for the process to appear
    std::vector<std::wstring> names;

    for (int i = 2; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--timeout") == 0 && i + 1 < argc) {
            timeoutSec = static_cast<DWORD>(wcstoul(argv[++i], nullptr, 10));
        } else {
            names.push_back(NormalizeName(argv[i]));
        }
    }
    if (names.empty()) {
        fwprintf(stderr, L"usage: OibCtl guard [--timeout <sec>] <process name>...\n");
        return kError;
    }

    int rc = Apply(true, false);
    if (rc != kOk) {
        return rc;
    }
    wprintf(L"Waiting for the process. Run \"OibCtl show\" or press Ctrl+C to stop guarding.\n");

    const DWORD pollMs = 500;
    const DWORD graceMs = 5000; // launcher -> game hand-offs briefly leave no matching process
    DWORD elapsed = 0;
    DWORD absentFor = 0;
    bool seen = false;
    int result = kOk;

    for (;;) {
        if (WaitForSingleObject(g_stopEvent, pollMs) == WAIT_OBJECT_0) {
            break;
        }
        elapsed += pollMs;

        // Someone ran "show" by hand: respect that and stop guarding.
        if (Status(true) == 0) {
            wprintf(L"Control devices were shown manually; guard ends.\n");
            return kOk;
        }

        if (AnyProcessRunning(names)) {
            seen = true;
            absentFor = 0;
        } else if (seen) {
            absentFor += pollMs;
            if (absentFor >= graceMs) {
                break;
            }
        } else if (timeoutSec != 0 && elapsed >= timeoutSec * 1000) {
            fwprintf(stderr, L"The process did not appear within %lu seconds.\n", timeoutSec);
            result = kTimeout;
            break;
        }
    }

    int showRc = Apply(false, false);
    return result != kOk ? result : showRc;
}

int Dialog()
{
    for (;;) {
        int st = Status(true);
        const wchar_t* state = st == 0 ? L"Visible / 表示中"
                             : st == 1 ? L"Hidden / 非表示"
                             : st == 2 ? L"Partly hidden / 一部のみ非表示"
                                       : L"Unknown (driver not installed?) / 不明（ドライバ未導入？）";
        std::wstring text = L"OpenInputBridge control devices: ";
        text += state;
        text += L"\n\nYes = Hide / 非表示にする\nNo = Show / 表示する\nCancel = Close / 閉じる";

        int answer = MessageBoxW(nullptr, text.c_str(), L"OibCtl", MB_YESNOCANCEL | MB_ICONINFORMATION | MB_SETFOREGROUND);
        if (answer == IDCANCEL) {
            return kOk;
        }
        int rc = Apply(answer == IDYES, true);
        if (rc == kBusy) {
            MessageBoxW(nullptr,
                        L"The control devices are in use by a program (AutoHotkey, kanata, ...). "
                        L"Close it and try again.\n"
                        L"コントロールデバイスが使用中です。AutoHotkey / kanata 等を終了してからやり直してください。",
                        L"OibCtl", MB_OK | MB_ICONWARNING);
        } else if (rc != kOk) {
            MessageBoxW(nullptr,
                        L"The operation failed. Is the OpenInputBridge driver (1.03 or later) installed?\n"
                        L"失敗しました。OpenInputBridge ドライバ（1.03 以降）は導入済みですか？",
                        L"OibCtl", MB_OK | MB_ICONERROR);
        }
    }
}

void Usage()
{
    fputws(L"OibCtl - hide/show OpenInputBridge control devices\n"
           L"  OibCtl hide | show | status\n"
           L"  OibCtl run <exe> [args...]\n"
           L"  OibCtl guard [--timeout <sec>] <process name>...\n"
           L"  OibCtl            (dialog)\n"
           L"exit codes: 0 ok, 1 busy, 2 no driver/access denied, 3 error, 4 guard timeout\n"
           L"status:     0 visible, 1 hidden, 2 partly hidden, 3 error\n", stderr);
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    if (argc < 2) {
        return Dialog();
    }

    const wchar_t* cmd = argv[1];
    if (_wcsicmp(cmd, L"hide") == 0) {
        return Apply(true, false);
    }
    if (_wcsicmp(cmd, L"show") == 0) {
        return Apply(false, false);
    }
    if (_wcsicmp(cmd, L"status") == 0) {
        return Status(false);
    }
    if (_wcsicmp(cmd, L"run") == 0) {
        return Run(argc, argv);
    }
    if (_wcsicmp(cmd, L"guard") == 0) {
        return Guard(argc, argv);
    }

    Usage();
    return kError;
}
