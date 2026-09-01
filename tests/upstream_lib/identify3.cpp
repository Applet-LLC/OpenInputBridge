// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// Raw-protocol counterpart to identify2.cpp, for exercising a control-device base name other
// than the fixed "interception" that third_party/interception/library/interception.c's
// interception_create_context() hardcodes (its device_name buffer and sprintf call have no
// parameter for this -- see that function). Since this repo's vendoring policy leaves
// third_party/interception/ completely untouched (docs/CLEAN_ROOM.md), this tool does not link
// against interception.dll/interception.h at all: it opens \\.\<base>NN directly and speaks the
// wire protocol (IOCTL codes + KEYBOARD_INPUT_DATA/MOUSE_INPUT_DATA) exactly as documented in
// docs/PROTOCOL.md, which itself was derived purely by reading interception.c/.h (no driver
// source or disassembly consulted).
//
// Usage: identify3.exe [device-name-base]
//   device-name-base  Base name of the 20 \\.\<base>NN control devices to open (default:
//                     "interception", matching driver/common/driver.h's
//                     OIB_DEFAULT_DEVICE_NAME_BASE). Pass whatever DeviceNameBase was set to
//                     (e.g. "oib" -- see tests/device_name_base_oib.reg) to talk to a renamed
//                     OpenInputBridge install side by side with the real Interception driver.
//
// Same fixed 10 keyboard / 10 mouse assumption as identify2.cpp and the original upstream
// samples (does not query IOCTL_GET_KEYBOARD_SLOT_COUNT) -- see docs/PROTOCOL.md's "デバイス構成"
// section on why this is only safe against an unconfigured (default 10/10) install.

#include <windows.h>
#include <winioctl.h>
#include <utils.h>

#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>

namespace {

constexpr int kKeyboardCount = 10;
constexpr int kMouseCount = 10;
constexpr int kDeviceCount = kKeyboardCount + kMouseCount;

// docs/PROTOCOL.md's "IOCTLコード" table.
#define IOCTL_SET_FILTER          CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SET_EVENT           CTL_CODE(FILE_DEVICE_UNKNOWN, 0x810, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_WRITE               CTL_CODE(FILE_DEVICE_UNKNOWN, 0x820, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_READ                CTL_CODE(FILE_DEVICE_UNKNOWN, 0x840, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_DRIVER_IDENTITY CTL_CODE(FILE_DEVICE_UNKNOWN, 0xA00, METHOD_BUFFERED, FILE_ANY_ACCESS)

// docs/PROTOCOL.md's "データ構造" section -- standard NT DDK structures, defined locally (same
// as interception.c does) rather than pulling WDK headers into this plain user-mode SDK build.
// Natural alignment, deliberately no #pragma pack (see that section's alignment note).
struct KeyboardInputData
{
    USHORT UnitId;
    USHORT MakeCode;
    USHORT Flags;
    USHORT Reserved;
    ULONG  ExtraInformation;
};

struct MouseInputData
{
    USHORT UnitId;
    USHORT Flags;
    USHORT ButtonFlags;
    USHORT ButtonData;
    ULONG  RawButtons;
    LONG   LastX;
    LONG   LastY;
    ULONG  ExtraInformation;
};

// docs/PROTOCOL.md's IOCTL_GET_DRIVER_IDENTITY section. Natural alignment (ULONG x3 then
// BOOLEAN) matters here specifically -- see that section's alignment note.
struct OibDriverIdentity
{
    ULONG   Signature;
    ULONG   VersionMajor;
    ULONG   VersionMinor;
    BOOLEAN IsKeyboard;
};

constexpr ULONG kOibSignature = 0x3142494F; // "OIB1"

// Raw KEYBOARD_INPUT_DATA.Flags bits (standard NT DDK meaning, not interception.h's shifted
// filter-bit numbering below).
constexpr USHORT kKeyFlagBreak = 0x0001;
constexpr USHORT kKeyFlagE0 = 0x0002;
constexpr USHORT kKeyFlagE1 = 0x0004;

// IOCTL_SET_FILTER bit values, taken as-is from third_party/interception/library/interception.h's
// InterceptionFilterKeyState/InterceptionFilterMouseState enums (see docs/PROTOCOL.md's
// IOCTL_SET_FILTER note on why the keyboard DOWN/UP pair -- unlike E0/E1 -- is NOT simply the
// raw Flags bit shifted left by one).
constexpr USHORT kFilterKeyDown = 0x0001;
constexpr USHORT kFilterKeyUp = 0x0002;
constexpr USHORT kFilterMouseLeftDown = 0x0001;
constexpr USHORT kFilterMouseLeftUp = 0x0002;
constexpr USHORT kFilterMouseRightDown = 0x0004;
constexpr USHORT kFilterMouseRightUp = 0x0008;
constexpr USHORT kFilterMouseMiddleDown = 0x0010;
constexpr USHORT kFilterMouseMiddleUp = 0x0020;

enum ScanCode
{
    SCANCODE_ESC = 0x01
};

std::string BuildDevicePath(const std::string &baseName, int index)
{
    std::ostringstream oss;
    oss << "\\\\.\\" << baseName << std::setw(2) << std::setfill('0') << index;
    return oss.str();
}

// Prints whatever IOCTL_GET_DRIVER_IDENTITY reports for one already-opened handle, so the
// operator can visually confirm they're actually talking to OpenInputBridge (and not, say, the
// real Interception driver still answering under the same renamed base by coincidence).
void PrintDriverIdentity(HANDLE handle, const char *label)
{
    using namespace std;

    OibDriverIdentity identity = {};
    DWORD bytesReturned = 0;

    if (!DeviceIoControl(handle, IOCTL_GET_DRIVER_IDENTITY, NULL, 0, &identity, sizeof(identity), &bytesReturned, NULL) ||
        bytesReturned != sizeof(identity) || identity.Signature != kOibSignature) {
        cout << label << ": IOCTL_GET_DRIVER_IDENTITY did not return an OpenInputBridge signature "
             << "-- this device may not be an OpenInputBridge control device." << endl;
        return;
    }

    cout << label << ": OpenInputBridge v" << identity.VersionMajor << "." << identity.VersionMinor
         << " (IsKeyboard=" << (identity.IsKeyboard ? "true" : "false") << ")" << endl;
}

void PrintKeyboardRecord(int deviceIndex, const KeyboardInputData &data)
{
    using namespace std;

    cout << "KEYBOARD(" << deviceIndex << ") code=0x" << hex << setw(2) << setfill('0') << data.MakeCode
         << dec << setfill(' ')
         << (data.Flags & kKeyFlagBreak ? " UP" : " DOWN")
         << (data.Flags & kKeyFlagE0 ? " E0" : "")
         << (data.Flags & kKeyFlagE1 ? " E1" : "")
         << endl;
}

void PrintMouseRecord(int deviceIndex, const MouseInputData &data)
{
    using namespace std;

    cout << "MOUSE(" << (deviceIndex - kKeyboardCount) << ") buttonFlags=0x"
         << hex << setw(3) << setfill('0') << data.ButtonFlags << dec << setfill(' ')
         << " buttonData=" << data.ButtonData
         << " x=" << data.LastX << " y=" << data.LastY
         << endl;
}

// Non-blocking: drains everything currently queued on every one of the 20 devices, in a fixed
// round-robin order, repeating full sweeps until one comes up empty -- same shape as identify2's
// DrainAllDevices, but reading/writing raw KEYBOARD_INPUT_DATA/MOUSE_INPUT_DATA via IOCTL_READ/
// IOCTL_WRITE directly instead of going through interception_receive()/interception_send()'s
// InterceptionKeyStroke/InterceptionMouseStroke translation. Writing each drained record straight
// back on the same handle it came from is what keeps the keyboard/mouse working normally while
// this tool runs (docs/PROTOCOL.md's IOCTL_WRITE note: reinjects below the writer's own chain
// position, reaching real hardware I/O if nothing else downstream claims it).
bool DrainAllDevices(HANDLE (&handles)[kDeviceCount])
{
    BYTE buffer[16 * sizeof(MouseInputData)];
    bool sawEscape = false;
    bool anyThisSweep;

    do {
        anyThisSweep = false;

        for (int i = 0; i < kDeviceCount; ++i) {
            DWORD bytesReturned = 0;

            if (!DeviceIoControl(handles[i], IOCTL_READ, NULL, 0, buffer, sizeof(buffer), &bytesReturned, NULL) ||
                bytesReturned == 0) {
                continue;
            }

            anyThisSweep = true;

            if (i < kKeyboardCount) {
                const KeyboardInputData *records = reinterpret_cast<const KeyboardInputData *>(buffer);
                size_t count = bytesReturned / sizeof(KeyboardInputData);

                for (size_t r = 0; r < count; ++r) {
                    PrintKeyboardRecord(i, records[r]);

                    if (records[r].MakeCode == SCANCODE_ESC && !(records[r].Flags & kKeyFlagBreak)) {
                        sawEscape = true;
                    }
                }
            } else {
                const MouseInputData *records = reinterpret_cast<const MouseInputData *>(buffer);
                size_t count = bytesReturned / sizeof(MouseInputData);

                for (size_t r = 0; r < count; ++r) {
                    PrintMouseRecord(i, records[r]);
                }
            }

            DWORD writtenBytes = 0;
            DeviceIoControl(handles[i], IOCTL_WRITE, buffer, bytesReturned, NULL, 0, &writtenBytes, NULL);
        }
    } while (anyThisSweep && !sawEscape);

    return sawEscape;
}

} // namespace

int main(int argc, char **argv)
{
    using namespace std;

    string baseName = (argc >= 2) ? argv[1] : "interception";

    raise_process_priority();

    HANDLE handles[kDeviceCount] = {};
    HANDLE events[kDeviceCount] = {};

    for (int i = 0; i < kDeviceCount; ++i) {
        string path = BuildDevicePath(baseName, i);

        handles[i] = CreateFileA(path.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (handles[i] == INVALID_HANDLE_VALUE) {
            cerr << "CreateFileA(" << path << ") failed (error " << GetLastError() << ") -- "
                 << "is a driver publishing DeviceNameBase=\"" << baseName << "\" installed and running?"
                 << endl;
            return 1;
        }

        events[i] = CreateEventW(NULL, FALSE, FALSE, NULL);

        HANDLE eventPair[2] = { events[i], NULL };
        DWORD bytesReturned = 0;
        if (!DeviceIoControl(handles[i], IOCTL_SET_EVENT, eventPair, sizeof(eventPair), NULL, 0, &bytesReturned, NULL)) {
            cerr << "IOCTL_SET_EVENT failed on " << path << " (error " << GetLastError() << ")" << endl;
            return 1;
        }

        USHORT filter = (i < kKeyboardCount)
            ? static_cast<USHORT>(kFilterKeyDown | kFilterKeyUp)
            : static_cast<USHORT>(
                  kFilterMouseLeftDown | kFilterMouseLeftUp | kFilterMouseRightDown |
                  kFilterMouseRightUp | kFilterMouseMiddleDown | kFilterMouseMiddleUp
                  );
        if (!DeviceIoControl(handles[i], IOCTL_SET_FILTER, &filter, sizeof(filter), NULL, 0, &bytesReturned, NULL)) {
            cerr << "IOCTL_SET_FILTER failed on " << path << " (error " << GetLastError() << ")" << endl;
            return 1;
        }
    }

    PrintDriverIdentity(handles[0], "keyboard");
    PrintDriverIdentity(handles[kKeyboardCount], "mouse");

    cout << "identify3: base=\"" << baseName << "\". type on any keyboard, click any mouse button. Esc to quit."
         << endl;

    bool quit = false;
    while (!quit) {
        // Blocks until at least one of the 20 devices has something; which one it names is
        // irrelevant here since DrainAllDevices() below sweeps every device regardless.
        WaitForMultipleObjects(kDeviceCount, events, FALSE, INFINITE);

        quit = DrainAllDevices(handles);
    }

    for (int i = 0; i < kDeviceCount; ++i) {
        CloseHandle(handles[i]);
        CloseHandle(events[i]);
    }

    return 0;
}
