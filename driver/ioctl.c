// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// See ioctl.h and docs/PROTOCOL.md. All 8 control-device IOCTLs are implemented here:
// IOCTL_GET_HARDWARE_ID (M2), IOCTL_SET_FILTER/GET_FILTER/SET_EVENT/READ and the stroke
// dispatch used by kbdfilter.c/mousefilter.c (M3), IOCTL_WRITE (M4), and
// IOCTL_SET_PRECEDENCE/GET_PRECEDENCE (M5 — see the caveat on those declarations in ioctl.h).

#include "ioctl.h"
#include "kbdfilter.h"
#include "mousefilter.h"

static VOID OibFileContextQueuePush(_Inout_ POIB_FILE_CONTEXT FileContext, _In_ PVOID StrokeData, _In_ SIZE_T StrokeSize, _Out_ PBOOLEAN BecameNonEmpty);
static ULONG OibFileContextQueueDrain(_Inout_ POIB_FILE_CONTEXT FileContext, _Out_writes_bytes_(OutputBufferSize) PVOID OutputBuffer, _In_ SIZE_T OutputBufferSize, _In_ SIZE_T StrokeSize);
static POIB_FILE_CONTEXT OibFindNextChainRecipient(_In_ ULONG SlotIndex, _In_opt_ POIB_FILE_CONTEXT After, _In_ USHORT RequiredFilterBits);
static USHORT OibComputeKeyboardRequiredFilterBits(_In_ USHORT RawFlags);
static USHORT OibComputeMouseRequiredFilterBits(_In_ PMOUSE_INPUT_DATA Stroke);

// Monotonically increasing counter, one tick per successfully-opened \\.\interceptionNN
// handle (OibCtlEvtFileCreate). Breaks Precedence ties in the hook chain — see
// "Precedence hook chain" in ioctl.h.
static LONG OibNextAttachSequence = 0;

VOID
OibCtlEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
{
    WDFFILEOBJECT fileObject = WdfRequestGetFileObject(Request);

    switch (IoControlCode) {
    case IOCTL_GET_HARDWARE_ID:
        OibCtlHandleGetHardwareId(Request, WdfIoQueueGetDevice(Queue), OutputBufferLength);
        return;

    case IOCTL_SET_FILTER:
        OibCtlHandleSetFilter(Request, fileObject);
        return;

    case IOCTL_GET_FILTER:
        OibCtlHandleGetFilter(Request, fileObject, OutputBufferLength);
        return;

    case IOCTL_SET_EVENT:
        OibCtlHandleSetEvent(Request, fileObject, InputBufferLength);
        return;

    case IOCTL_READ:
        OibCtlHandleRead(Request, fileObject, OutputBufferLength);
        return;

    case IOCTL_WRITE:
        OibCtlHandleWrite(Request, WdfIoQueueGetDevice(Queue), fileObject, InputBufferLength);
        return;

    case IOCTL_SET_PRECEDENCE:
        OibCtlHandleSetPrecedence(Request, fileObject);
        return;

    case IOCTL_GET_PRECEDENCE:
        OibCtlHandleGetPrecedence(Request, fileObject, OutputBufferLength);
        return;

    default:
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        return;
    }
}

VOID
OibCtlHandleGetHardwareId(
    _In_ WDFREQUEST Request,
    _In_ WDFDEVICE ControlDevice,
    _In_ size_t OutputBufferLength
    )
{
    POIB_CONTROL_DEVICE_CONTEXT ctlContext;
    WDFDEVICE filterDevice;
    NTSTATUS status;
    PVOID outputBuffer = NULL;
    size_t outputBufferSize = 0;
    size_t bytesReturned = 0;

    ctlContext = OibGetControlDeviceContext(ControlDevice);

    if (OutputBufferLength != 0) {
        status = WdfRequestRetrieveOutputBuffer(Request, 0, &outputBuffer, &outputBufferSize);
        if (!NT_SUCCESS(status)) {
            WdfRequestComplete(Request, status);
            return;
        }
    }

    status = OibSlotAcquireFilterDevice(ctlContext->SlotIndex, &filterDevice);
    if (NT_SUCCESS(status)) {
        PDEVICE_OBJECT pdo = WdfDeviceWdmGetPhysicalDevice(filterDevice);
        ULONG requiredLength = 0;

        // Ask directly into the caller's buffer first: if it's already big enough (the
        // common case — hardware IDs are short), this is a single call with no extra
        // allocation.
        status = IoGetDeviceProperty(
            pdo,
            DevicePropertyHardwareID,
            (ULONG)outputBufferSize,
            outputBuffer,
            &requiredLength
            );

        if (status == STATUS_BUFFER_TOO_SMALL && outputBufferSize > 0) {
            // Caller's buffer is too small for the full property. Rather than failing
            // outright, fetch the full value into a temporary buffer and hand back as much
            // as fits — see docs/PROTOCOL.md ("呼び出し元バッファサイズに収まる範囲で返す").
            PVOID tempBuffer = ExAllocatePool2(POOL_FLAG_PAGED, requiredLength, OIB_POOL_TAG);

            if (tempBuffer == NULL) {
                status = STATUS_INSUFFICIENT_RESOURCES;
            } else {
                ULONG actualLength = 0;

                status = IoGetDeviceProperty(pdo, DevicePropertyHardwareID, requiredLength, tempBuffer, &actualLength);
                if (NT_SUCCESS(status)) {
                    size_t copyLength = min((size_t)actualLength, outputBufferSize);
                    RtlCopyMemory(outputBuffer, tempBuffer, copyLength);
                    bytesReturned = copyLength;
                }

                ExFreePoolWithTag(tempBuffer, OIB_POOL_TAG);
            }
        } else if (NT_SUCCESS(status)) {
            bytesReturned = requiredLength;
        }

        OibSlotReleaseFilterDeviceReference(filterDevice);
    } else {
        // No physical device currently assigned to this slot: succeed with zero bytes rather
        // than fail — see docs/PROTOCOL.md on why empty slots must not fail control-device
        // IOCTLs (the unmodified upstream library must be able to treat every one of the 20
        // \\.\interceptionNN devices as usable regardless of what's physically plugged in).
        status = STATUS_SUCCESS;
        bytesReturned = 0;
    }

    WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}

VOID
OibCtlHandleWrite(
    _In_ WDFREQUEST Request,
    _In_ WDFDEVICE ControlDevice,
    _In_ WDFFILEOBJECT FileObject,
    _In_ size_t InputBufferLength
    )
{
    POIB_CONTROL_DEVICE_CONTEXT ctlContext;
    POIB_FILE_CONTEXT writerContext;
    WDFDEVICE filterDevice;
    NTSTATUS filterDeviceStatus;
    PVOID inputBuffer = NULL;
    size_t inputBufferSize = 0;
    size_t strokeSize;
    ULONG strokeCount;
    ULONG deliveredCount = 0;
    ULONG i;
    NTSTATUS status;

    ctlContext = OibGetControlDeviceContext(ControlDevice);
    writerContext = OibGetFileContext(FileObject);
    strokeSize = ctlContext->IsKeyboard ? sizeof(KEYBOARD_INPUT_DATA) : sizeof(MOUSE_INPUT_DATA);

    if (InputBufferLength != 0) {
        status = WdfRequestRetrieveInputBuffer(Request, 0, &inputBuffer, &inputBufferSize);
        if (!NT_SUCCESS(status)) {
            WdfRequestComplete(Request, status);
            return;
        }
    }

    strokeCount = (ULONG)(inputBufferSize / strokeSize);

    // Resolved once up front; only actually needed if/when some record below falls all the
    // way through the chain to real hardware delivery. Its absence (no physical device
    // currently assigned to this slot) is not an error — the chain portion below still runs
    // normally either way, matching OibCtlHandleGetHardwareId's empty-slot precedent.
    filterDeviceStatus = OibSlotAcquireFilterDevice(ctlContext->SlotIndex, &filterDevice);

    // Injecting synthetic input and releasing/replacing a stroke previously captured via
    // IOCTL_READ are the same operation here: both re-enter the precedence hook chain
    // starting strictly below the writer's own position (see "Precedence hook chain" in
    // ioctl.h) — record by record, since each one independently either gets caught by a
    // lower-precedence instance's filter or falls through to real hardware delivery.
    for (i = 0; i < strokeCount; i++) {
        PVOID strokeAt = (PUCHAR)inputBuffer + ((SIZE_T)i * strokeSize);
        POIB_FILE_CONTEXT recipient;
        BOOLEAN becameNonEmpty;
        USHORT requiredBits = 0;

        if (ctlContext->IsKeyboard) {
            requiredBits = OibComputeKeyboardRequiredFilterBits(((PKEYBOARD_INPUT_DATA)strokeAt)->Flags);
        } else {
            requiredBits = OibComputeMouseRequiredFilterBits((PMOUSE_INPUT_DATA)strokeAt);
        }

        OibSlotLockInstances(ctlContext->SlotIndex);
        recipient = (requiredBits != 0)
            ? OibFindNextChainRecipient(ctlContext->SlotIndex, writerContext, requiredBits)
            : NULL;

        if (recipient != NULL) {
            OibFileContextQueuePush(recipient, strokeAt, strokeSize, &becameNonEmpty);

            if (becameNonEmpty && recipient->UnemptyEvent != NULL) {
                KeSetEvent(recipient->UnemptyEvent, IO_NO_INCREMENT, FALSE);
            }
        }
        OibSlotUnlockInstances(ctlContext->SlotIndex);

        if (recipient != NULL) {
            // Caught by a lower-precedence instance: stays in the chain, not delivered to
            // hardware (yet) — see OibCtlHandleWrite's own header comment.
            deliveredCount++;
            continue;
        }

        // Fell off the bottom of the chain: final delivery to the real ClassService, if this
        // slot currently has a physical device assigned.
        if (NT_SUCCESS(filterDeviceStatus)) {
            ULONG consumed = 0;

            if (ctlContext->IsKeyboard) {
                POIB_KBD_FILTER_CONTEXT kbdContext = OibGetKbdFilterContext(filterDevice);

                if (kbdContext->UpperConnectData.ClassService != NULL) {
                    PKEYBOARD_INPUT_DATA stroke = (PKEYBOARD_INPUT_DATA)strokeAt;

#pragma warning(push)
#pragma warning(disable:4055) // PVOID -> PSERVICE_CALLBACK_ROUTINE conversion, same as kbdfilter.c
                    (*(PSERVICE_CALLBACK_ROUTINE)(ULONG_PTR)kbdContext->UpperConnectData.ClassService)(
                        kbdContext->UpperConnectData.ClassDeviceObject,
                        stroke,
                        stroke + 1,
                        &consumed
                        );
#pragma warning(pop)
                }
            } else {
                POIB_MOU_FILTER_CONTEXT mouContext = OibGetMouFilterContext(filterDevice);

                if (mouContext->UpperConnectData.ClassService != NULL) {
                    PMOUSE_INPUT_DATA stroke = (PMOUSE_INPUT_DATA)strokeAt;

#pragma warning(push)
#pragma warning(disable:4055)
                    (*(PSERVICE_CALLBACK_ROUTINE)(ULONG_PTR)mouContext->UpperConnectData.ClassService)(
                        mouContext->UpperConnectData.ClassDeviceObject,
                        stroke,
                        stroke + 1,
                        &consumed
                        );
#pragma warning(pop)
                }
            }
        }

        deliveredCount++;
    }

    if (NT_SUCCESS(filterDeviceStatus)) {
        OibSlotReleaseFilterDeviceReference(filterDevice);
    }

    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, (ULONG_PTR)(deliveredCount * strokeSize));
}

VOID
OibCtlEvtFileCreate(
    _In_ WDFDEVICE Device,
    _In_ WDFREQUEST Request,
    _In_ WDFFILEOBJECT FileObject
    )
{
    POIB_CONTROL_DEVICE_CONTEXT ctlContext = OibGetControlDeviceContext(Device);
    POIB_FILE_CONTEXT fileContext = OibGetFileContext(FileObject);

    // Self-referencing until (if) OibSlotAttachFileContext links it for real, so that
    // OibCtlEvtFileClose's OibSlotDetachFileContext is always safe to call, even if the
    // allocation below fails and we never actually attach.
    InitializeListHead(&fileContext->SlotLinkage);

    fileContext->SlotIndex = ctlContext->SlotIndex;
    fileContext->IsKeyboard = ctlContext->IsKeyboard;
    fileContext->Filter = 0;
    fileContext->Precedence = 0;
    fileContext->AttachSequence = InterlockedIncrement(&OibNextAttachSequence);
    fileContext->UnemptyEvent = NULL;
    fileContext->QueueHead = 0;
    fileContext->QueueCount = 0;

    // Non-paged: the capture queue is written to from OibDispatchKeyboardStroke/
    // OibDispatchMouseStroke, which can run at DISPATCH_LEVEL (the PS/2 port driver's
    // keyboard/mouse service callback chain runs there) — see slots.h.
    fileContext->Queue = ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        OIB_CAPTURE_QUEUE_CAPACITY * sizeof(OIB_STROKE_RECORD),
        OIB_POOL_TAG
        );

    if (fileContext->Queue == NULL) {
        WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
        return;
    }

    OibSlotAttachFileContext(fileContext->SlotIndex, &fileContext->SlotLinkage);

    WdfRequestComplete(Request, STATUS_SUCCESS);
}

VOID
OibCtlEvtFileClose(
    _In_ WDFFILEOBJECT FileObject
    )
{
    POIB_FILE_CONTEXT fileContext = OibGetFileContext(FileObject);

    OibSlotDetachFileContext(fileContext->SlotIndex, &fileContext->SlotLinkage);

    if (fileContext->UnemptyEvent != NULL) {
        ObDereferenceObject(fileContext->UnemptyEvent);
        fileContext->UnemptyEvent = NULL;
    }

    if (fileContext->Queue != NULL) {
        ExFreePoolWithTag(fileContext->Queue, OIB_POOL_TAG);
        fileContext->Queue = NULL;
    }
}

VOID
OibCtlHandleSetFilter(
    _In_ WDFREQUEST Request,
    _In_ WDFFILEOBJECT FileObject
    )
{
    POIB_FILE_CONTEXT fileContext = OibGetFileContext(FileObject);
    NTSTATUS status;
    PUSHORT filterValue;
    size_t length;

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(USHORT), &filterValue, &length);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    OibSlotLockInstances(fileContext->SlotIndex);
    fileContext->Filter = *filterValue;
    OibSlotUnlockInstances(fileContext->SlotIndex);

    WdfRequestComplete(Request, STATUS_SUCCESS);
}

VOID
OibCtlHandleGetFilter(
    _In_ WDFREQUEST Request,
    _In_ WDFFILEOBJECT FileObject,
    _In_ size_t OutputBufferLength
    )
{
    POIB_FILE_CONTEXT fileContext = OibGetFileContext(FileObject);
    NTSTATUS status;
    PUSHORT outputValue;
    size_t outputBufferSize;
    USHORT filter;

    UNREFERENCED_PARAMETER(OutputBufferLength);

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(USHORT), &outputValue, &outputBufferSize);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    OibSlotLockInstances(fileContext->SlotIndex);
    filter = fileContext->Filter;
    OibSlotUnlockInstances(fileContext->SlotIndex);

    *outputValue = filter;

    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(USHORT));
}

VOID
OibCtlHandleSetPrecedence(
    _In_ WDFREQUEST Request,
    _In_ WDFFILEOBJECT FileObject
    )
{
    POIB_FILE_CONTEXT fileContext = OibGetFileContext(FileObject);
    NTSTATUS status;
    PLONG precedenceValue;
    size_t length;

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(LONG), &precedenceValue, &length);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    OibSlotLockInstances(fileContext->SlotIndex);
    fileContext->Precedence = *precedenceValue;
    OibSlotUnlockInstances(fileContext->SlotIndex);

    WdfRequestComplete(Request, STATUS_SUCCESS);
}

VOID
OibCtlHandleGetPrecedence(
    _In_ WDFREQUEST Request,
    _In_ WDFFILEOBJECT FileObject,
    _In_ size_t OutputBufferLength
    )
{
    POIB_FILE_CONTEXT fileContext = OibGetFileContext(FileObject);
    NTSTATUS status;
    PLONG outputValue;
    size_t outputBufferSize;
    LONG precedence;

    UNREFERENCED_PARAMETER(OutputBufferLength);

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(LONG), &outputValue, &outputBufferSize);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    OibSlotLockInstances(fileContext->SlotIndex);
    precedence = fileContext->Precedence;
    OibSlotUnlockInstances(fileContext->SlotIndex);

    *outputValue = precedence;

    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(LONG));
}

VOID
OibCtlHandleSetEvent(
    _In_ WDFREQUEST Request,
    _In_ WDFFILEOBJECT FileObject,
    _In_ size_t InputBufferLength
    )
{
    POIB_FILE_CONTEXT fileContext = OibGetFileContext(FileObject);
    NTSTATUS status;
    PVOID inputBuffer;
    PHANDLE handles;
    size_t length;
    PKEVENT eventObject = NULL;
    PKEVENT previousEvent;

    UNREFERENCED_PARAMETER(InputBufferLength);

    // Wire format is HANDLE[2] (only the first element is real, see docs/PROTOCOL.md); we
    // only need the first, so require just sizeof(HANDLE).
    status = WdfRequestRetrieveInputBuffer(Request, sizeof(HANDLE), &inputBuffer, &length);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    handles = (PHANDLE)inputBuffer;

    // This handler runs synchronously in the calling thread's context (a directly-opened
    // local handle doing DeviceIoControl), so handles[0] resolves against the right
    // process's handle table with no extra attach step needed.
    status = ObReferenceObjectByHandle(
        handles[0],
        EVENT_MODIFY_STATE,
        *ExEventObjectType,
        KernelMode,
        (PVOID *)&eventObject,
        NULL
        );
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    OibSlotLockInstances(fileContext->SlotIndex);
    previousEvent = fileContext->UnemptyEvent;
    fileContext->UnemptyEvent = eventObject;
    OibSlotUnlockInstances(fileContext->SlotIndex);

    if (previousEvent != NULL) {
        // The real library only calls this once per handle; don't leak a reference if a
        // caller (mis)uses the IOCTL a second time.
        ObDereferenceObject(previousEvent);
    }

    WdfRequestComplete(Request, STATUS_SUCCESS);
}

VOID
OibCtlHandleRead(
    _In_ WDFREQUEST Request,
    _In_ WDFFILEOBJECT FileObject,
    _In_ size_t OutputBufferLength
    )
{
    POIB_FILE_CONTEXT fileContext = OibGetFileContext(FileObject);
    NTSTATUS status;
    PVOID outputBuffer = NULL;
    size_t outputBufferSize = 0;
    size_t strokeSize = fileContext->IsKeyboard ? sizeof(KEYBOARD_INPUT_DATA) : sizeof(MOUSE_INPUT_DATA);
    ULONG recordsCopied;

    if (OutputBufferLength != 0) {
        status = WdfRequestRetrieveOutputBuffer(Request, 0, &outputBuffer, &outputBufferSize);
        if (!NT_SUCCESS(status)) {
            WdfRequestComplete(Request, status);
            return;
        }
    }

    OibSlotLockInstances(fileContext->SlotIndex);
    recordsCopied = OibFileContextQueueDrain(fileContext, outputBuffer, outputBufferSize, strokeSize);
    // The user-mode library's "unempty" event is manual-reset (see interception_create_context's
    // CreateEventA(..., TRUE, ...)) and is level-triggered on "queue non-empty": we're the only
    // side that can ever clear it back down, so a drain that empties the queue must do so here,
    // still under the same lock as the drain itself (a concurrent push taking the lock right
    // after us will correctly re-set it for its own new data). Without this, the event latches
    // signaled forever after the very first stroke, and every later WaitForMultipleObjects call
    // in interception_wait returns immediately with nothing new to read.
    if (fileContext->QueueCount == 0 && fileContext->UnemptyEvent != NULL) {
        KeClearEvent(fileContext->UnemptyEvent);
    }
    OibSlotUnlockInstances(fileContext->SlotIndex);

    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, (ULONG_PTR)(recordsCopied * strokeSize));
}

static VOID
OibFileContextQueuePush(
    _Inout_ POIB_FILE_CONTEXT FileContext,
    _In_ PVOID StrokeData,
    _In_ SIZE_T StrokeSize,
    _Out_ PBOOLEAN BecameNonEmpty
    )
{
    ULONG writeIndex;

    NT_ASSERT(StrokeSize <= sizeof(OIB_STROKE_RECORD));

    *BecameNonEmpty = (FileContext->QueueCount == 0);

    if (FileContext->QueueCount == OIB_CAPTURE_QUEUE_CAPACITY) {
        // Full: drop the oldest queued record to make room for this one (see ioctl.h's
        // OIB_CAPTURE_QUEUE_CAPACITY comment).
        FileContext->QueueHead = (FileContext->QueueHead + 1) % OIB_CAPTURE_QUEUE_CAPACITY;
        FileContext->QueueCount--;
    }

    writeIndex = (FileContext->QueueHead + FileContext->QueueCount) % OIB_CAPTURE_QUEUE_CAPACITY;
    RtlCopyMemory(&FileContext->Queue[writeIndex], StrokeData, StrokeSize);
    FileContext->QueueCount++;
}

static ULONG
OibFileContextQueueDrain(
    _Inout_ POIB_FILE_CONTEXT FileContext,
    _Out_writes_bytes_(OutputBufferSize) PVOID OutputBuffer,
    _In_ SIZE_T OutputBufferSize,
    _In_ SIZE_T StrokeSize
    )
{
    ULONG maxRecords = (ULONG)(OutputBufferSize / StrokeSize);
    ULONG recordsToCopy = min(maxRecords, FileContext->QueueCount);
    ULONG i;

    for (i = 0; i < recordsToCopy; i++) {
        ULONG readIndex = (FileContext->QueueHead + i) % OIB_CAPTURE_QUEUE_CAPACITY;
        RtlCopyMemory((PUCHAR)OutputBuffer + ((SIZE_T)i * StrokeSize), &FileContext->Queue[readIndex], StrokeSize);
    }

    FileContext->QueueHead = (FileContext->QueueHead + recordsToCopy) % OIB_CAPTURE_QUEUE_CAPACITY;
    FileContext->QueueCount -= recordsToCopy;

    return recordsToCopy;
}

static USHORT
OibComputeKeyboardRequiredFilterBits(
    _In_ USHORT RawFlags
    )
{
    USHORT required;

    // KEY_UP (bit 0 of the raw flags) selects between the FILTER_KEY_DOWN (0x0001) and
    // FILTER_KEY_UP (0x0002) bits — "down" has no raw bit of its own, it's the absence of UP.
    required = (RawFlags & 0x0001) ? 0x0002 /* INTERCEPTION_FILTER_KEY_UP */
                                    : 0x0001 /* INTERCEPTION_FILTER_KEY_DOWN */;

    // Every other defined raw flag bit (E0, E1, TERMSRV_SET_LED, TERMSRV_SHADOW,
    // TERMSRV_VKPACKET — bits 1-5) has a corresponding filter bit exactly one position to its
    // left (see InterceptionFilterKeyState in interception.h).
    required = (USHORT)(required | ((RawFlags & 0x003E) << 1));

    return required;
}

static USHORT
OibComputeMouseRequiredFilterBits(
    _In_ PMOUSE_INPUT_DATA Stroke
    )
{
    // Button/wheel filter bits map 1:1 (no shift) onto the raw ButtonFlags bits — see
    // InterceptionFilterMouseState in interception.h.
    USHORT required = (USHORT)(Stroke->ButtonFlags & 0x0FFF);

    if (Stroke->LastX != 0 || Stroke->LastY != 0) {
        required |= 0x1000; // INTERCEPTION_FILTER_MOUSE_MOVE (has no raw ButtonFlags bit of
                             // its own; it's a synthetic bit meaning "movement occurred").
    }

    return required;
}

// True if A sits strictly above B in the precedence hook chain (see "Precedence hook chain"
// in ioctl.h): either A's Precedence is greater, or — on a tie — A attached earlier than B.
// Always false when A and B are the same instance.
static BOOLEAN
OibIsHigherPriority(
    _In_ POIB_FILE_CONTEXT A,
    _In_ POIB_FILE_CONTEXT B
    )
{
    if (A->Precedence != B->Precedence) {
        return A->Precedence > B->Precedence;
    }
    return A->AttachSequence < B->AttachSequence;
}

// Finds the highest-chain-position open instance on SlotIndex whose Filter matches
// RequiredFilterBits, considering only instances strictly below After's chain position (or
// every instance, if After is NULL — a fresh stroke nobody has claimed yet). Must be called
// with SlotIndex's instances lock held (slots.h).
static POIB_FILE_CONTEXT
OibFindNextChainRecipient(
    _In_ ULONG SlotIndex,
    _In_opt_ POIB_FILE_CONTEXT After,
    _In_ USHORT RequiredFilterBits
    )
{
    PLIST_ENTRY head = OibSlotGetInstancesListHead(SlotIndex);
    PLIST_ENTRY entry;
    POIB_FILE_CONTEXT best = NULL;

    for (entry = head->Flink; entry != head; entry = entry->Flink) {
        POIB_FILE_CONTEXT candidate = CONTAINING_RECORD(entry, OIB_FILE_CONTEXT, SlotLinkage);

        if (candidate->Filter == 0) {
            continue; // FILTER_*_NONE: never capture.
        }

        // Overlap match, not superset: RequiredFilterBits can carry more than one
        // simultaneously-true aspect of a single stroke (a mouse packet routinely has a
        // button transition AND nonzero movement in the same record — real mice essentially
        // always report a little jitter alongside a click), and a listener that only asked
        // for one of those aspects (e.g. just LEFT_BUTTON_DOWN) should still see it. For
        // keyboard, RequiredFilterBits is always exactly one bit, where overlap and superset
        // are equivalent, so this is a no-op change there.
        if ((candidate->Filter & RequiredFilterBits) == 0) {
            continue;
        }

        if (After != NULL && !OibIsHigherPriority(After, candidate)) {
            continue; // not strictly below After's position in the chain.
        }

        if (best == NULL || OibIsHigherPriority(candidate, best)) {
            best = candidate;
        }
    }

    return best;
}

VOID
OibDispatchKeyboardStroke(
    _In_ ULONG SlotIndex,
    _In_ PKEYBOARD_INPUT_DATA Stroke,
    _Out_ PBOOLEAN Captured
    )
{
    USHORT requiredBits;
    POIB_FILE_CONTEXT recipient;
    BOOLEAN becameNonEmpty;

    *Captured = FALSE;
    requiredBits = OibComputeKeyboardRequiredFilterBits(Stroke->Flags);

    OibSlotLockInstances(SlotIndex);

    // Entering the chain at the top: nobody has seen this (fresh, hardware-originated) stroke
    // yet — see "Precedence hook chain" in ioctl.h.
    recipient = OibFindNextChainRecipient(SlotIndex, NULL, requiredBits);

    if (recipient != NULL) {
        OibFileContextQueuePush(recipient, Stroke, sizeof(*Stroke), &becameNonEmpty);

        if (becameNonEmpty && recipient->UnemptyEvent != NULL) {
            KeSetEvent(recipient->UnemptyEvent, IO_NO_INCREMENT, FALSE);
        }

        *Captured = TRUE;
    }

    OibSlotUnlockInstances(SlotIndex);
}

VOID
OibDispatchMouseStroke(
    _In_ ULONG SlotIndex,
    _In_ PMOUSE_INPUT_DATA Stroke,
    _Out_ PBOOLEAN Captured
    )
{
    USHORT requiredBits;
    POIB_FILE_CONTEXT recipient;
    BOOLEAN becameNonEmpty;

    *Captured = FALSE;
    requiredBits = OibComputeMouseRequiredFilterBits(Stroke);

    if (requiredBits == 0) {
        // Nothing about this packet (no button/wheel change, no movement) is capturable.
        return;
    }

    OibSlotLockInstances(SlotIndex);

    recipient = OibFindNextChainRecipient(SlotIndex, NULL, requiredBits);

    if (recipient != NULL) {
        OibFileContextQueuePush(recipient, Stroke, sizeof(*Stroke), &becameNonEmpty);

        if (becameNonEmpty && recipient->UnemptyEvent != NULL) {
            KeSetEvent(recipient->UnemptyEvent, IO_NO_INCREMENT, FALSE);
        }

        *Captured = TRUE;
    }

    OibSlotUnlockInstances(SlotIndex);
}
