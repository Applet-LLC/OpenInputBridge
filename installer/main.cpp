// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Entry point: OpenInputBridgeSetup.exe installs by default, or uninstalls with /uninstall.
// With no keyboard/mouse argument, both drivers are installed/uninstalled in sequence
// (keyboard.sys is the first driver to be installed or the last one to be removed, so a
// failure partway through still leaves a consistent, well-understood state); pass "keyboard"
// or "mouse" to act on only that one driver. --slots=N (install + an explicit driver type
// only) sets that driver's share of the 20 total slots — see common.h/install.cpp and
// docs/DECISIONS.md's 2026-08-02 entry.

#include "common.h"

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <optional>

namespace {

const wchar_t* const kUsage = L"Usage: OpenInputBridgeSetup.exe [/uninstall] [keyboard|mouse] [--slots=N]\n";

} // namespace

int wmain(int argc, wchar_t* argv[])
{
    bool uninstall = false;
    bool typeSpecified = false;
    OpenInputBridge::DriverType type = OpenInputBridge::DriverType::Keyboard;
    std::optional<ULONG> requestedSlots;

    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"/uninstall") == 0 || _wcsicmp(argv[i], L"-uninstall") == 0) {
            uninstall = true;
        } else if (_wcsicmp(argv[i], L"keyboard") == 0) {
            type = OpenInputBridge::DriverType::Keyboard;
            typeSpecified = true;
        } else if (_wcsicmp(argv[i], L"mouse") == 0) {
            type = OpenInputBridge::DriverType::Mouse;
            typeSpecified = true;
        } else if (_wcsnicmp(argv[i], L"--slots=", 8) == 0) {
            const wchar_t* numberText = argv[i] + 8;
            wchar_t* endPtr = nullptr;
            unsigned long parsed = wcstoul(numberText, &endPtr, 10);

            if (numberText[0] == L'\0' || *endPtr != L'\0') {
                wprintf(L"[ERROR] --slots requires a non-negative integer: %s\n", argv[i]);
                return 1;
            }
            requestedSlots = static_cast<ULONG>(parsed);
        } else if (_wcsicmp(argv[i], L"/?") == 0 || _wcsicmp(argv[i], L"-help") == 0 || _wcsicmp(argv[i], L"/help") == 0) {
            wprintf(L"%s", kUsage);
            wprintf(L"  keyboard|mouse : act on only this one driver (default: both).\n");
            wprintf(L"  --slots=N      : (install only, requires keyboard|mouse) this driver's\n");
            wprintf(L"                   share of the 20 total \\\\.\\interceptionNN slots (0-20).\n");
            wprintf(L"                   The other driver's share is kept in sync automatically.\n");
            return 0;
        } else {
            wprintf(L"[ERROR] Unrecognized argument: %s\n", argv[i]);
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
