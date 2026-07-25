// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// M6: reverses install.cpp — removes "OpenInputBridge" from both class GUIDs' UpperFilters
// REG_MULTI_SZ values (leaving other entries intact) and deletes the service. Also requires
// a reboot to take effect, for the same reason as installation.

#include <windows.h>
#include <cstdio>

int wmain(int argc, wchar_t* argv[])
{
    (void)argc;
    (void)argv;

    // TODO(M6): implement the reverse of install.cpp's steps 1-2.
    wprintf(L"OpenInputBridge uninstaller: not yet implemented.\n");

    return 1;
}
