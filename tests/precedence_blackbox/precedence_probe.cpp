// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// M5 (precedence hook chain) interactive test tool. Self-authored; a plain consumer of the
// unmodified public interception.h/interception.dll, same as the samples in tests/upstream_lib/
// (see that directory's InterceptionLib.vcxproj for why a self-authored vcxproj builds against
// it instead of the upstream sample build system). See third_party/README.md and
// docs/CLEAN_ROOM.md.
//
// Run two or more instances simultaneously, each with a different --precedence value (and
// optionally --consume), then type on a keyboard to observe delivery order, chain
// continuation, and consumption against a real OpenInputBridge install. See this directory's
// README.md for concrete test scenarios.
//
// Usage: precedence_probe.exe <precedence> [--consume]
//   <precedence>  Integer passed to interception_set_precedence for every keyboard device
//                 (\\.\interception00 .. 09). Higher runs earlier in the chain; ties broken by
//                 attach order (whichever instance called interception_create_context first).
//   --consume     Don't call interception_send() -- swallow every captured stroke instead of
//                 passing it down the chain (and eventually through to hardware, if nothing
//                 lower down claims it). Omit this to act as a transparent pass-through, which
//                 is what lets the keyboard keep working normally while multiple instances are
//                 attached and testing.
//
// Multiple concurrent opens of the same \\.\interceptionNN from different processes are
// allowed by this driver (confirmed via real-machine testing): unlike what CreateFileA's
// ShareMode=0 argument in interception_create_context() might suggest for an ordinary
// filesystem file, this driver's WDF control devices don't enforce single-open exclusivity, so
// several precedence_probe.exe instances (plus identify.exe etc.) can all run at once.

#include <interception.h>
#include <utils.h>

#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <chrono>

namespace {

enum ScanCode
{
    SCANCODE_ESC = 0x01
};

void PrintTimestamp()
{
    using namespace std;
    using namespace std::chrono;

    // Microsecond resolution, not just seconds: chain continuation between two instances
    // (this process resending, the next one's interception_wait waking up) happens well under
    // a second apart -- often under a millisecond -- so a plain HH:MM:SS marker shows the same
    // value for both and can't be used to eyeball which instance printed first.
    system_clock::time_point now = system_clock::now();
    time_t nowTimeT = system_clock::to_time_t(now);
    microseconds microsPart = duration_cast<microseconds>(now.time_since_epoch() % seconds(1));

    tm localTime;
    localtime_s(&localTime, &nowTimeT);

    cout << setfill('0') << setw(2) << localTime.tm_hour << ':'
         << setw(2) << localTime.tm_min << ':' << setw(2) << localTime.tm_sec << '.'
         << setw(6) << microsPart.count() << setfill(' ');
}

} // namespace

int main(int argc, char **argv)
{
    using namespace std;

    if (argc < 2) {
        cerr << "usage: precedence_probe.exe <precedence> [--consume]" << endl;
        return 1;
    }

    InterceptionPrecedence precedence = static_cast<InterceptionPrecedence>(atoi(argv[1]));
    bool consume = (argc >= 3 && strcmp(argv[2], "--consume") == 0);

    raise_process_priority();

    InterceptionContext context = interception_create_context();
    if (!context) {
        cerr << "interception_create_context failed -- is OpenInputBridge installed and running? "
             << "(sc.exe query OpenInputBridge should show STATE: RUNNING)" << endl;
        return 1;
    }

    for (InterceptionDevice device = INTERCEPTION_KEYBOARD(0); device <= INTERCEPTION_KEYBOARD(INTERCEPTION_MAX_KEYBOARD - 1); ++device) {
        interception_set_precedence(context, device, precedence);
    }

    interception_set_filter(context, interception_is_keyboard, INTERCEPTION_FILTER_KEY_DOWN | INTERCEPTION_FILTER_KEY_UP);

    cout << "precedence_probe: precedence=" << precedence
         << (consume ? " (CONSUMING -- swallows every stroke)" : " (passing through)")
         << ". Type on any keyboard. Esc to quit." << endl;

    InterceptionStroke stroke;
    InterceptionDevice device;
    bool quit = false;

    while (!quit && interception_receive(context, device = interception_wait(context), &stroke, 1) > 0) {
        InterceptionKeyStroke &keyStroke = *reinterpret_cast<InterceptionKeyStroke *>(&stroke);

        PrintTimestamp();
        cout << " [precedence=" << precedence << "] KEYBOARD(" << (device - INTERCEPTION_KEYBOARD(0))
             << ") code=0x" << hex << setw(2) << setfill('0') << keyStroke.code << dec << setfill(' ')
             << (keyStroke.state & INTERCEPTION_KEY_UP ? " UP" : " DOWN") << endl;

        if (keyStroke.code == SCANCODE_ESC && !(keyStroke.state & INTERCEPTION_KEY_UP)) {
            quit = true;
        }

        if (!consume) {
            interception_send(context, device, &stroke, 1);
        }
    }

    interception_destroy_context(context);

    return 0;
}
