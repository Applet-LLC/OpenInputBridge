// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// The 8 control-device IOCTLs, plus the per-open-instance (WDFFILEOBJECT) state they operate
// on: filter bitmask, "unempty" event, and capture queue. Codes and semantics are defined in
// docs/PROTOCOL.md, derived from reading (unmodified, LGPL)
// third_party/interception/library/interception.c — see docs/CLEAN_ROOM.md for what was and
// wasn't consulted to arrive at these definitions.

#pragma once

#include "driver.h"
#include "slots.h"
#include <ntddkbd.h>
#include <ntddmou.h>

#define IOCTL_SET_PRECEDENCE  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_PRECEDENCE  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SET_FILTER      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_FILTER      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x808, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SET_EVENT       CTL_CODE(FILE_DEVICE_UNKNOWN, 0x810, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_WRITE           CTL_CODE(FILE_DEVICE_UNKNOWN, 0x820, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_READ            CTL_CODE(FILE_DEVICE_UNKNOWN, 0x840, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_HARDWARE_ID CTL_CODE(FILE_DEVICE_UNKNOWN, 0x880, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Number of stroke records the capture queue can hold before OibDispatch{Keyboard,Mouse}Stroke
// starts dropping the oldest queued record to make room for new ones. A named, single-point
// constant rather than a magic number scattered across the queue math below.
#define OIB_CAPTURE_QUEUE_CAPACITY 256

// One record's worth of raw stroke storage: big enough for either a KEYBOARD_INPUT_DATA or a
// MOUSE_INPUT_DATA. The queue stores raw bytes (not a tagged union) because a given slot's
// queue only ever holds one kind (keyboard slots only ever see KEYBOARD_INPUT_DATA, and vice
// versa), so tagging would be redundant — callers derive the record size themselves from
// IsKeyboard (sizeof(KEYBOARD_INPUT_DATA) vs sizeof(MOUSE_INPUT_DATA)).
typedef union _OIB_STROKE_RECORD
{
    KEYBOARD_INPUT_DATA Keyboard;
    MOUSE_INPUT_DATA Mouse;
} OIB_STROKE_RECORD, *POIB_STROKE_RECORD;

// Per-open-instance ("\\.\interceptionNN" handle) state. One of these exists per WDFFILEOBJECT
// on a control device — i.e. independently per process (or even per handle within a process)
// that has that device open, matching the real protocol's per-context model (see
// docs/PROTOCOL.md). Everything except SlotIndex/IsKeyboard is guarded by the owning slot's
// InstancesLock (slots.h) rather than a lock of its own — see slots.h's design note.
typedef struct _OIB_FILE_CONTEXT
{
    ULONG SlotIndex;   // which \Device\interceptionNN slot this handle belongs to (fixed for
                        // the handle's lifetime; copied from the control device's own context
                        // at file-create time).
    BOOLEAN IsKeyboard;

    LIST_ENTRY SlotLinkage; // linked into the owning slot's OpenInstances list (slots.h).

    USHORT Filter;     // InterceptionFilter bitmask; 0 (NONE) = capture nothing (the default).

    LONG Precedence;   // InterceptionPrecedence; 0 by default. Higher values sit closer to the
                        // top of the per-slot hook chain — see "Precedence hook chain" below.

    LONG AttachSequence; // Assigned from a monotonically increasing counter when this handle is
                          // opened (OibCtlEvtFileCreate). Breaks ties between instances with
                          // equal Precedence: whichever attached first sits higher in the chain,
                          // matching the real driver's documented default-precedence-0 behavior
                          // (see docs/CLEAN_ROOM.md's 2026-07-26 entry).

    PKEVENT UnemptyEvent; // referenced via IOCTL_SET_EVENT; NULL if never set.

    // Capture queue: fixed-capacity circular buffer of raw stroke records, allocated from
    // non-paged pool (touched from DISPATCH_LEVEL — the PS/2 port driver can call the
    // keyboard/mouse service callback there) at file-create time and freed at file-close.
    POIB_STROKE_RECORD Queue;
    ULONG QueueHead;   // index of the oldest queued record.
    ULONG QueueCount;  // number of records currently queued (0..OIB_CAPTURE_QUEUE_CAPACITY).
} OIB_FILE_CONTEXT, *POIB_FILE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(OIB_FILE_CONTEXT, OibGetFileContext)

EVT_WDF_DEVICE_FILE_CREATE OibCtlEvtFileCreate;
EVT_WDF_FILE_CLOSE OibCtlEvtFileClose;

EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL OibCtlEvtIoDeviceControl;

// IOCTL_GET_HARDWARE_ID (M2): resolves the control device's assigned slot (if any) to its
// filter FDO's lower PDO and returns that PDO's hardware ID property, truncated to whatever
// fits in the caller's output buffer.
VOID OibCtlHandleGetHardwareId(_In_ WDFREQUEST Request, _In_ WDFDEVICE ControlDevice, _In_ size_t OutputBufferLength);

// IOCTL_SET_FILTER / IOCTL_GET_FILTER (M3): per-file-object filter bitmask.
VOID OibCtlHandleSetFilter(_In_ WDFREQUEST Request, _In_ WDFFILEOBJECT FileObject);
VOID OibCtlHandleGetFilter(_In_ WDFREQUEST Request, _In_ WDFFILEOBJECT FileObject, _In_ size_t OutputBufferLength);

// IOCTL_SET_EVENT (M3): references the caller-supplied event handle (resolved in the calling
// thread's/process's context, which is where this handler naturally runs) so it can later be
// signaled by OibDispatchKeyboardStroke/OibDispatchMouseStroke.
VOID OibCtlHandleSetEvent(_In_ WDFREQUEST Request, _In_ WDFFILEOBJECT FileObject, _In_ size_t InputBufferLength);

// IOCTL_READ (M3): non-blocking drain of whatever is currently queued for this file object.
VOID OibCtlHandleRead(_In_ WDFREQUEST Request, _In_ WDFFILEOBJECT FileObject, _In_ size_t OutputBufferLength);

// --- Precedence hook chain (M5) ---
//
// Per docs/CLEAN_ROOM.md's 2026-07-26 entry (the Interception author's own public explanation
// of this mechanism, found via Wayback Machine — this is the original implementation's
// documented design, not a guess): the open instances on a slot form an ordered chain, highest
// Precedence first (ties broken by AttachSequence — whoever attached first sits higher). A
// stroke is offered to instances strictly one at a time, starting from the top of the chain:
// the first instance (in chain order) whose filter matches captures it — nobody lower in the
// chain, and never the real hardware/OS, sees it *yet*. Only when that instance later calls
// IOCTL_WRITE to release (or replace, or inject alongside) it does the stroke continue past
// that instance's position, being offered again starting from the next instance below it —
// and so on, until either some instance captures it again or it falls off the bottom of the
// chain, at which point it finally reaches the real ClassService (actual OS delivery).
// Injecting brand-new synthetic input via IOCTL_WRITE follows the exact same rule: it only
// ever travels downward from the calling instance's own chain position, same as
// SetWindowsHookEx's CallNextHookEx convention — a hook cannot reach back up to hooks above
// itself.
//
// OibFindNextChainRecipient (ioctl.c) implements "who's next in the chain, strictly below a
// given position" and is shared by both entry points below.

// Called from kbdfilter.c's/mousefilter.c's service callback for each individual stroke
// record freshly reported by the port/HID driver below (one call per record, even when the
// port driver batches several into one callback invocation — see kbdfilter.c/mousefilter.c
// for why). This is a stroke entering the chain at the top (no instance has seen it yet).
// Finds the highest-chain-position open instance whose active filter bitmask matches, queues
// a copy into it and signals its "unempty" event if this transitions its queue from empty to
// non-empty, then sets *Captured = TRUE. If no open instance's filter matches, sets
// *Captured = FALSE (the stroke should be forwarded to the real ClassService unmodified —
// nothing in the chain wanted it).
//
// The exact bit-matching rule (see OibComputeKeyboardRequiredFilterBits/
// OibComputeMouseRequiredFilterBits in ioctl.c) is still a best-effort reading of the public
// library header, not something confirmed against the real driver — unlike the chain
// ordering above, that part remains a candidate for M5 black-box validation
// (tests/precedence_blackbox/, docs/PROTOCOL.md).
VOID OibDispatchKeyboardStroke(_In_ ULONG SlotIndex, _In_ PKEYBOARD_INPUT_DATA Stroke, _Out_ PBOOLEAN Captured);
VOID OibDispatchMouseStroke(_In_ ULONG SlotIndex, _In_ PMOUSE_INPUT_DATA Stroke, _Out_ PBOOLEAN Captured);

// IOCTL_WRITE (M4/M5): FileObject is the instance releasing/injecting — the stroke re-enters
// the chain starting strictly below FileObject's own chain position (see "Precedence hook
// chain" above), record by record. For each record: if some lower instance's filter matches,
// it's queued there and stays in the chain (not delivered yet); otherwise this resolves the
// slot's assigned filter FDO (if any) and calls the saved UpperConnectData.ClassService
// directly — final delivery to the real hardware input stream. Injecting synthetic input and
// releasing a previously captured stroke (from IOCTL_READ) are the same operation as far as
// the wire protocol and this handler are concerned; see docs/PROTOCOL.md. An empty slot (no
// physical device currently assigned) still walks the chain normally; only the final
// real-hardware delivery step is skipped, and the call still succeeds — matching
// OibCtlHandleGetHardwareId's precedent for empty slots.
VOID OibCtlHandleWrite(_In_ WDFREQUEST Request, _In_ WDFDEVICE ControlDevice, _In_ WDFFILEOBJECT FileObject, _In_ size_t InputBufferLength);

// IOCTL_SET_PRECEDENCE / IOCTL_GET_PRECEDENCE (M5): per-file-object InterceptionPrecedence
// (plain int/LONG), consulted by the chain lookups above.
VOID OibCtlHandleSetPrecedence(_In_ WDFREQUEST Request, _In_ WDFFILEOBJECT FileObject);
VOID OibCtlHandleGetPrecedence(_In_ WDFREQUEST Request, _In_ WDFFILEOBJECT FileObject, _In_ size_t OutputBufferLength);
