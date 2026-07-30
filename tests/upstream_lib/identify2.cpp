// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// A more detailed companion to oblitum/Interceptionライブラリ's samples/identify/identify.cpp,
// for interactively confirming M0/M2/M3/M4 against a real OpenInputBridge install.
// Self-authored (does not touch third_party/interception/ in any way -- it's a plain consumer
// of the unmodified public interception.h/interception.dll, same as the original sample; there
// is no reason to modify identify.cpp itself, and this repo's vendoring policy wouldn't allow
// it anyway). See third_party/README.md and docs/CLEAN_ROOM.md.
//
// What's different from the original identify sample, and why:
//
// 1. Prints richer detail per stroke (hex code/state for keyboard, button state + x/y for
//    mouse) instead of just the device index, since "does it work" needs to be visibly
//    verifiable, not just "did anything happen at all".
// 2. Mouse filter covers left/right/middle button down AND up (the original only asked for
//    LEFT_BUTTON_DOWN), so all three main buttons are actually exercised by this tool.
// 3. Round-robin drain (sweep every device with a non-blocking interception_receive() each
//    time interception_wait() wakes up, repeating the sweep until a full pass finds nothing
//    left) rather than handling only the single device interception_wait() happened to name
//    before waiting again. This means no device's queue depends on which one
//    WaitForMultipleObjects (used internally by interception_wait()) happened to report first
//    when several are signaled at once, which seemed a plausible cause the first time output
//    appeared to stall -- though real testing traced that specific symptom to a console
//    QuickEdit-mode selection pause instead (clicking inside the console window itself pauses
//    its output until the selection is cleared -- unrelated to identify.cpp or the driver; see
//    this directory's README). Kept anyway as a reasonable property in its own right.

#include <interception.h>
#include <utils.h>

#include <iostream>
#include <iomanip>

namespace {

enum ScanCode
{
    SCANCODE_ESC = 0x01
};

void PrintKeyboardStroke(InterceptionDevice device, const InterceptionKeyStroke &keyStroke)
{
    using namespace std;

    cout << "INTERCEPTION_KEYBOARD(" << (device - INTERCEPTION_KEYBOARD(0)) << ") code=0x"
         << hex << setw(2) << setfill('0') << keyStroke.code << dec
         << (keyStroke.state & INTERCEPTION_KEY_UP ? " UP" : " DOWN")
         << (keyStroke.state & INTERCEPTION_KEY_E0 ? " E0" : "")
         << (keyStroke.state & INTERCEPTION_KEY_E1 ? " E1" : "")
         << endl;
}

void PrintMouseStroke(InterceptionDevice device, const InterceptionMouseStroke &mouseStroke)
{
    using namespace std;

    cout << "INTERCEPTION_MOUSE(" << (device - INTERCEPTION_MOUSE(0)) << ") state=0x"
         << hex << setw(3) << setfill('0') << mouseStroke.state << dec
         << " rolling=" << mouseStroke.rolling
         << " x=" << mouseStroke.x << " y=" << mouseStroke.y
         << endl;
}

// Non-blocking: drains everything currently queued on every one of the 20 devices, in a fixed
// round-robin order, repeating full sweeps until one comes up empty. Returns true if Esc was
// seen on a keyboard (caller should stop).
bool DrainAllDevices(InterceptionContext context)
{
    using namespace std;

    InterceptionStroke stroke;
    bool sawEscape = false;
    bool anyThisSweep;

    do {
        anyThisSweep = false;

        for (InterceptionDevice device = INTERCEPTION_KEYBOARD(0); device <= INTERCEPTION_MOUSE(INTERCEPTION_MAX_MOUSE - 1); ++device) {
            while (interception_receive(context, device, &stroke, 1) > 0) {
                anyThisSweep = true;

                if (interception_is_keyboard(device)) {
                    const InterceptionKeyStroke &keyStroke = *reinterpret_cast<const InterceptionKeyStroke *>(&stroke);

                    PrintKeyboardStroke(device, keyStroke);

                    if (keyStroke.code == SCANCODE_ESC && !(keyStroke.state & INTERCEPTION_KEY_UP)) {
                        sawEscape = true;
                    }
                } else if (interception_is_mouse(device)) {
                    PrintMouseStroke(device, *reinterpret_cast<const InterceptionMouseStroke *>(&stroke));
                } else {
                    cout << "UNRECOGNIZED(" << device << ")" << endl;
                }

                interception_send(context, device, &stroke, 1);
            }
        }
    } while (anyThisSweep && !sawEscape);

    return sawEscape;
}

} // namespace

int main()
{
    using namespace std;

    raise_process_priority();

    InterceptionContext context = interception_create_context();
    if (!context) {
        cerr << "interception_create_context failed -- is OpenInputBridge installed and running? "
             << "(sc.exe query OpenInputBridge should show STATE: RUNNING)" << endl;
        return 1;
    }

    interception_set_filter(context, interception_is_keyboard, INTERCEPTION_FILTER_KEY_DOWN | INTERCEPTION_FILTER_KEY_UP);
    interception_set_filter(
        context, interception_is_mouse,
        INTERCEPTION_FILTER_MOUSE_LEFT_BUTTON_DOWN | INTERCEPTION_FILTER_MOUSE_LEFT_BUTTON_UP |
        INTERCEPTION_FILTER_MOUSE_RIGHT_BUTTON_DOWN | INTERCEPTION_FILTER_MOUSE_RIGHT_BUTTON_UP |
        INTERCEPTION_FILTER_MOUSE_MIDDLE_BUTTON_DOWN | INTERCEPTION_FILTER_MOUSE_MIDDLE_BUTTON_UP
        );

    cout << "identify2: type on any keyboard, click any mouse button. Esc to quit." << endl;

    bool quit = false;
    while (!quit) {
        // Blocks until at least one of the 20 devices has something; which one it names is
        // irrelevant here since DrainAllDevices() below sweeps every device regardless.
        interception_wait(context);

        quit = DrainAllDevices(context);
    }

    interception_destroy_context(context);

    return 0;
}
