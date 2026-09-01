// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Passive observer for driver/common/slots.c's slot table, built to give this directory's
// README (hot-plug / sleep-resume test procedure) a repeatable, loggable signal instead of
// relying on a human staring at a keyboard for the whole run. See
// docs/THIRD_PARTY_INTERCEPTION_DRIVER_FIX.md for why this exists.
//
// Unlike identify2.cpp/identify3.cpp (tests/upstream_lib/), this tool never calls
// IOCTL_SET_FILTER, IOCTL_READ, or IOCTL_WRITE, so it never joins the precedence hook chain in
// a way that could capture a stroke (its per-instance Filter bitmask stays 0/NONE, the default
// -- see driver/common/ioctl.h's OIB_FILE_CONTEXT). It only polls IOCTL_GET_HARDWARE_ID on every
// \\.\<base>NN control device to see which slot (if any) currently has a physical device
// assigned, and prints/logs a line whenever a slot's mapping changes. Keyboard/mouse input keeps
// flowing completely normally on every device while this runs, including the devices under test.
//
// Same raw-protocol approach as identify3.cpp (no interception.dll dependency, so this can also
// target a renamed DeviceNameBase) -- see that file's header comment for why.
//
// Usage: hotplug_monitor.exe [device-name-base] [poll-interval-ms] [duration-sec] [log-path]
//   device-name-base  Base name of the \\.\<base>NN control devices (default: "interception",
//                     matching driver/common/driver.h's OIB_DEFAULT_DEVICE_NAME_BASE).
//   poll-interval-ms  Polling period in milliseconds (default: 200).
//   duration-sec      Stop automatically after this many seconds (default: 0 = run until
//                     Ctrl+C). Sleep/resume scenarios suspend this process along with everything
//                     else, so a poll simply resumes on wake -- no special handling needed.
//   log-path          Every change line (and the start/stop summary) is also appended here
//                     (default: hotplug_monitor.log, in the current directory).
//
// Unlike identify2.cpp/identify3.cpp's fixed 10/10 assumption (documented there as only safe
// against an unconfigured install), this tool queries IOCTL_GET_KEYBOARD_SLOT_COUNT itself so it
// reports the correct kbd/mouse boundary even against a registry-reconfigured split
// (docs/DECISIONS.md's 2026-08-02 entry) -- appropriate here since slot behavior is this tool's
// entire purpose.

#include <windows.h>
#include <winioctl.h>

#include <chrono>
#include <cstdlib>
#include <cwchar>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// docs/PROTOCOL.md's "IOCTLコード" table / driver/common/ioctl.h. Duplicated here rather than
// including driver/ headers, same as identify3.cpp does (those headers pull in wdf.h).
#define IOCTL_GET_HARDWARE_ID         CTL_CODE(FILE_DEVICE_UNKNOWN, 0x880, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_KEYBOARD_SLOT_COUNT CTL_CODE(FILE_DEVICE_UNKNOWN, 0x900, METHOD_BUFFERED, FILE_ANY_ACCESS)

// driver/common/driver.h's OIB_TOTAL_DEVICE_SLOT_COUNT.
constexpr int kTotalSlotCount = 20;

volatile bool g_stopRequested = false;

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
{
    switch (ctrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
        g_stopRequested = true;
        return TRUE;
    default:
        return FALSE;
    }
}

std::string BuildDevicePath(const std::string &baseName, int index)
{
    std::ostringstream oss;
    oss << "\\\\.\\" << baseName << std::setw(2) << std::setfill('0') << index;
    return oss.str();
}

std::string Timestamp()
{
    using namespace std::chrono;

    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &t);

    std::ostringstream oss;
    oss << std::put_time(&local, "%H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
}

// Queries this handle's IOCTL_GET_HARDWARE_ID and returns the first NUL-terminated string of
// the returned REG_MULTI_SZ, narrowed to ASCII (hardware IDs are always ASCII in practice), or
// "(empty)" if the slot currently has no physical device assigned. Per
// driver/common/ioctl.c's OibCtlHandleGetHardwareId, an empty slot returns STATUS_SUCCESS with
// 0 bytes -- not an error -- so this never needs to distinguish "empty slot" from "IOCTL
// failed" by return value alone; DeviceIoControl itself only fails on a genuinely unexpected
// condition (e.g. the control device disappearing, which shouldn't happen -- these are the
// always-present control devices, not the physical device slots).
std::string QuerySlotState(HANDLE handle)
{
    WCHAR buffer[256] = {};
    DWORD bytesReturned = 0;

    if (!DeviceIoControl(handle, IOCTL_GET_HARDWARE_ID, NULL, 0, buffer, sizeof(buffer), &bytesReturned, NULL)) {
        return "(error " + std::to_string(GetLastError()) + ")";
    }

    if (bytesReturned < sizeof(WCHAR) || buffer[0] == L'\0') {
        return "(empty)";
    }

    size_t charCount = bytesReturned / sizeof(WCHAR);
    std::wstring wide(buffer, wcsnlen(buffer, charCount));

    std::string narrow(wide.size(), '\0');
    for (size_t i = 0; i < wide.size(); ++i) {
        narrow[i] = (wide[i] < 128) ? static_cast<char>(wide[i]) : '?';
    }
    return narrow;
}

int QueryKeyboardSlotCount(HANDLE keyboardHandle)
{
    ULONG count = 0;
    DWORD bytesReturned = 0;

    if (!DeviceIoControl(keyboardHandle, IOCTL_GET_KEYBOARD_SLOT_COUNT, NULL, 0, &count, sizeof(count), &bytesReturned, NULL) ||
        bytesReturned != sizeof(count)) {
        std::cerr << "IOCTL_GET_KEYBOARD_SLOT_COUNT failed (error " << GetLastError()
                  << "); assuming the default 10/10 split." << std::endl;
        return 10;
    }

    return static_cast<int>(count);
}

} // namespace

int main(int argc, char **argv)
{
    using namespace std;

    string baseName = (argc >= 2) ? argv[1] : "interception";
    int pollIntervalMs = (argc >= 3) ? atoi(argv[2]) : 200;
    int durationSec = (argc >= 4) ? atoi(argv[3]) : 0;
    string logPath = (argc >= 5) ? argv[4] : "hotplug_monitor.log";

    HANDLE handles[kTotalSlotCount] = {};

    for (int i = 0; i < kTotalSlotCount; ++i) {
        string path = BuildDevicePath(baseName, i);

        handles[i] = CreateFileA(path.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (handles[i] == INVALID_HANDLE_VALUE) {
            cerr << "CreateFileA(" << path << ") failed (error " << GetLastError() << ") -- "
                 << "is a driver publishing DeviceNameBase=\"" << baseName << "\" installed and running?"
                 << endl;
            return 1;
        }
    }

    int keyboardSlotCount = QueryKeyboardSlotCount(handles[0]);
    int mouseSlotCount = kTotalSlotCount - keyboardSlotCount;

    ofstream logFile(logPath, ios::app);
    if (!logFile) {
        cerr << "warning: could not open log file \"" << logPath << "\" -- continuing without logging." << endl;
    }

    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    auto emit = [&](const string &line) {
        cout << line << endl;
        if (logFile) {
            logFile << line << endl;
            logFile.flush();
        }
    };

    {
        ostringstream header;
        header << "=== hotplug_monitor start: base=\"" << baseName << "\" keyboardSlots=" << keyboardSlotCount
               << " mouseSlots=" << mouseSlotCount << " pollIntervalMs=" << pollIntervalMs
               << (durationSec > 0 ? (" durationSec=" + to_string(durationSec)) : string(" (Ctrl+C to stop)"));
        emit(header.str());
    }

    vector<string> previousState(kTotalSlotCount);
    int activeSlotCount = 0;

    for (int i = 0; i < kTotalSlotCount; ++i) {
        previousState[i] = QuerySlotState(handles[i]);
        if (previousState[i] != "(empty)") {
            ++activeSlotCount;
        }

        ostringstream line;
        line << "[" << Timestamp() << "] slot " << setw(2) << setfill('0') << i
             << (i < keyboardSlotCount ? " (kbd)" : " (mou)") << ": " << previousState[i];
        emit(line.str());
    }

    auto startTime = chrono::steady_clock::now();

    while (!g_stopRequested) {
        if (durationSec > 0) {
            auto elapsed = chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - startTime).count();
            if (elapsed >= durationSec) {
                break;
            }
        }

        Sleep(static_cast<DWORD>(pollIntervalMs));

        for (int i = 0; i < kTotalSlotCount; ++i) {
            string current = QuerySlotState(handles[i]);
            if (current == previousState[i]) {
                continue;
            }

            ostringstream line;
            line << "[" << Timestamp() << "] slot " << setw(2) << setfill('0') << i
                 << (i < keyboardSlotCount ? " (kbd)" : " (mou)") << ": " << previousState[i]
                 << " -> " << current;
            emit(line.str());

            if (previousState[i] == "(empty)" && current != "(empty)") {
                ++activeSlotCount;
            } else if (previousState[i] != "(empty)" && current == "(empty)") {
                --activeSlotCount;
            }

            previousState[i] = current;
        }
    }

    {
        ostringstream summary;
        summary << "=== hotplug_monitor stop: active slots at exit = " << activeSlotCount << " / " << kTotalSlotCount;
        emit(summary.str());
    }

    for (int i = 0; i < kTotalSlotCount; ++i) {
        CloseHandle(handles[i]);
    }

    return 0;
}
