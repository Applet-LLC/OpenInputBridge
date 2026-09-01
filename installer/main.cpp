// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Entry point: OpenInputBridgeSetup.exe installs by default, or uninstalls with /uninstall.
// With no keyboard/mouse argument, both drivers are installed/uninstalled in sequence
// (oib_kbd.sys is the first driver to be installed or the last one to be removed, so a
// failure partway through still leaves a consistent, well-understood state); pass "keyboard"
// or "mouse" to act on only that one driver. --slots=N (install + an explicit driver type
// only) sets that driver's share of the 20 total slots — see common.h/install.cpp and
// docs/DECISIONS.md's 2026-08-02 entry.
//
// The --enable-audit-log/--disable-audit-log and --enable-toast/--disable-toast pairs are
// independent, optional features (see auditlog.h/toastsetup.h) invoked separately from the
// driver install/uninstall above — a WiX installer's CustomActions call these the same way it
// calls plain install/uninstall, once per Feature the user selected. --apply-audit-sacl is not
// meant to be run interactively: it's the command the audit-log feature's own Scheduled Task
// re-invokes on every service start (see auditlog.h). --verify-install (see verify.h) is meant
// to be run once, after both of the above, by packaging/setup.bat.
//
// Every invocation (regardless of which of the above) is gated on
// common.h's IsSupportedWindowsEnvironment() first, unless --skip-version-check is also passed
// — this driver has only ever been built/tested for x64 Windows 11+ (Windows 10 is explicitly
// NOT supported; see that function's own comment and docs/DECISIONS.md's 2026-09-01 entry), and
// installing a kernel driver on an unsupported combination risks a BSOD or silent input-handling
// corruption, not just "might not work". --skip-version-check exists for the Pro/Subscription editions' own
// WiX installers, which call this executable from a CustomAction sequence that may already
// have its own, equivalent precondition checks earlier in the UI flow.

#include "common.h"
#include "auditlog.h"
#include "toastsetup.h"
#include "verify.h"

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <optional>
#include <string>
#include <vector>

namespace {

const wchar_t* const kUsage =
    L"Usage: OpenInputBridgeSetup.exe [/uninstall] [keyboard|mouse] [--slots=N]\n"
    L"       OpenInputBridgeSetup.exe --enable-audit-log | --disable-audit-log\n"
    L"       OpenInputBridgeSetup.exe --enable-toast | --disable-toast\n"
    L"       OpenInputBridgeSetup.exe --allow-process <full path> | --disallow-process <full path>\n"
    L"       OpenInputBridgeSetup.exe --list-allowed-processes\n"
    L"       OpenInputBridgeSetup.exe --verify-install\n";

const wchar_t* const kUnsupportedEnvironmentMessage = L"This is the wrong Windows version. It's for Windows 11.\n";

} // namespace

int wmain(int argc, wchar_t* argv[])
{
    // --skip-version-check is filtered out here rather than handled inline below, so every
    // other command (the argc==2/argc==3 dispatch, and the install/uninstall argument loop)
    // can keep working against a plain positional argument list, unaware this flag exists.
    std::vector<std::wstring> args;
    bool skipVersionCheck = false;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--skip-version-check") == 0) {
            skipVersionCheck = true;
        } else {
            args.emplace_back(argv[i]);
        }
    }

    if (!skipVersionCheck && !OpenInputBridge::IsSupportedWindowsEnvironment()) {
        wprintf(L"%s", kUnsupportedEnvironmentMessage);
        return 1;
    }

    bool uninstall = false;
    bool typeSpecified = false;
    OpenInputBridge::DriverType type = OpenInputBridge::DriverType::Keyboard;
    std::optional<ULONG> requestedSlots;

    // These are standalone commands (each takes over the whole invocation), not modifiers
    // combined with the driver install/uninstall arguments above.
    if (args.size() == 1) {
        if (_wcsicmp(args[0].c_str(), L"--enable-audit-log") == 0) {
            return OpenInputBridge::RunEnableAuditLog();
        }
        if (_wcsicmp(args[0].c_str(), L"--disable-audit-log") == 0) {
            return OpenInputBridge::RunDisableAuditLog();
        }
        if (_wcsicmp(args[0].c_str(), L"--apply-audit-sacl") == 0) {
            return OpenInputBridge::RunApplyAuditSacl();
        }
        if (_wcsicmp(args[0].c_str(), L"--dump-audit-sacl") == 0) {
            return OpenInputBridge::RunDumpAuditSacl();
        }
        if (_wcsicmp(args[0].c_str(), L"--enable-toast") == 0) {
            return OpenInputBridge::RunEnableToast();
        }
        if (_wcsicmp(args[0].c_str(), L"--disable-toast") == 0) {
            return OpenInputBridge::RunDisableToast();
        }
        if (_wcsicmp(args[0].c_str(), L"--list-allowed-processes") == 0) {
            return OpenInputBridge::RunListAllowedProcesses();
        }
        if (_wcsicmp(args[0].c_str(), L"--verify-install") == 0) {
            return OpenInputBridge::RunVerifyInstall();
        }
    }

    if (args.size() == 2) {
        if (_wcsicmp(args[0].c_str(), L"--allow-process") == 0) {
            return OpenInputBridge::RunAllowProcess(args[1]);
        }
        if (_wcsicmp(args[0].c_str(), L"--disallow-process") == 0) {
            return OpenInputBridge::RunDisallowProcess(args[1]);
        }
    }

    for (const std::wstring& arg : args) {
        if (_wcsicmp(arg.c_str(), L"/uninstall") == 0 || _wcsicmp(arg.c_str(), L"-uninstall") == 0) {
            uninstall = true;
        } else if (_wcsicmp(arg.c_str(), L"keyboard") == 0) {
            type = OpenInputBridge::DriverType::Keyboard;
            typeSpecified = true;
        } else if (_wcsicmp(arg.c_str(), L"mouse") == 0) {
            type = OpenInputBridge::DriverType::Mouse;
            typeSpecified = true;
        } else if (_wcsnicmp(arg.c_str(), L"--slots=", 8) == 0) {
            const wchar_t* numberText = arg.c_str() + 8;
            wchar_t* endPtr = nullptr;
            unsigned long parsed = wcstoul(numberText, &endPtr, 10);

            if (numberText[0] == L'\0' || *endPtr != L'\0') {
                wprintf(L"[ERROR] --slots requires a non-negative integer: %s\n", arg.c_str());
                return 1;
            }
            requestedSlots = static_cast<ULONG>(parsed);
        } else if (_wcsicmp(arg.c_str(), L"/?") == 0 || _wcsicmp(arg.c_str(), L"-help") == 0 || _wcsicmp(arg.c_str(), L"/help") == 0) {
            wprintf(L"%s", kUsage);
            wprintf(L"  keyboard|mouse      : act on only this one driver (default: both).\n");
            wprintf(L"  --slots=N           : (install only, requires keyboard|mouse) this driver's\n");
            wprintf(L"                        share of the 20 total \\\\.\\interceptionNN slots (0-20).\n");
            wprintf(L"                        The other driver's share is kept in sync automatically.\n");
            wprintf(L"  --enable-audit-log  : turn on Security-event-log auditing of control device access.\n");
            wprintf(L"  --disable-audit-log : turn it back off.\n");
            wprintf(L"  --enable-toast      : turn on toast notifications (requires audit-log enabled).\n");
            wprintf(L"  --disable-toast     : turn them back off.\n");
            wprintf(L"  --allow-process <full path>    : suppress toasts for this process (audit log is unaffected).\n");
            wprintf(L"  --disallow-process <full path> : undo the above.\n");
            wprintf(L"  --list-allowed-processes       : show the current toast-suppression allowlist.\n");
            wprintf(L"  --verify-install                : check driver/audit-log/toast state after installing.\n");
            wprintf(L"  --skip-version-check             : bypass the Windows version/architecture check\n");
            wprintf(L"                                     (for the Pro/Subscription installers' own use).\n");
            return 0;
        } else {
            wprintf(L"[ERROR] Unrecognized argument: %s\n", arg.c_str());
            wprintf(L"%s", kUsage);
            return 1;
        }
    }

    if (requestedSlots.has_value() && !typeSpecified) {
        wprintf(L"[ERROR] --slots requires keyboard or mouse to also be specified.\n");
        wprintf(L"%s", kUsage);
        return 1;
    }
    if (requestedSlots.has_value() && uninstall) {
        wprintf(L"[ERROR] --slots is not valid with /uninstall.\n");
        wprintf(L"%s", kUsage);
        return 1;
    }

    if (typeSpecified) {
        return uninstall
            ? OpenInputBridge::RunUninstall(type)
            : OpenInputBridge::RunInstall(type, requestedSlots);
    }

    if (uninstall) {
        int keyboardResult = OpenInputBridge::RunUninstall(OpenInputBridge::DriverType::Keyboard);
        if (keyboardResult != 0) {
            return keyboardResult;
        }
        return OpenInputBridge::RunUninstall(OpenInputBridge::DriverType::Mouse);
    }

    int keyboardResult = OpenInputBridge::RunInstall(OpenInputBridge::DriverType::Keyboard);
    if (keyboardResult != 0) {
        return keyboardResult;
    }
    return OpenInputBridge::RunInstall(OpenInputBridge::DriverType::Mouse);
}
