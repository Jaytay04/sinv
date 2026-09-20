#ifndef HYPERSINV_EFI_RUNTIME_COMPANION_H
#define HYPERSINV_EFI_RUNTIME_COMPANION_H

/*
 * Optional WDM-facing companion for the EFI resident hypervisor.
 *
 * This header intentionally contains only the adapter surface.  It does not
 * expose any EFI key or claim to authenticate the reserved mailbox tag.  The
 * standalone WDM companion owns its device ACL; an integration may call
 * Hvr1CompanionHandleIoctl from a METHOD_BUFFERED dispatch path.
 */

#include <ntddk.h>

#include "hvr1_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HVR1_COMPANION HVR1_COMPANION;

typedef NTSTATUS (*HVR1_POWER_PREPARE_CALLBACK)(
    _In_opt_ PVOID CallbackContext,
    _In_ HVR1_POWER_EVENT Event);

/* No VMX instruction is executed during create. */
NTSTATUS Hvr1CompanionCreate(
    _Outptr_ HVR1_COMPANION **Companion);

/* Destruction is accepted only after the root reports OFFLINE. */
NTSTATUS Hvr1CompanionDestroy(
    _Inout_ HVR1_COMPANION *Companion);

/* Returns the fixed mailbox contract; no physical address is disclosed. */
NTSTATUS Hvr1CompanionGetMailboxDescriptor(
    _In_ HVR1_COMPANION *Companion,
    _Out_ HVR1_MAILBOX_DESCRIPTOR *Descriptor);

/*
 * Dispatches one bounded IOCTL.  Input and output are already METHOD_BUFFERED
 * kernel buffers owned by the parent WDM/KMDF driver.  The function is valid
 * at PASSIVE_LEVEL only because PREPARE and scan may issue a root VMCALL.
 */
NTSTATUS Hvr1CompanionHandleIoctl(
    _Inout_ HVR1_COMPANION *Companion,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_opt_(InputBytes) const VOID *Input,
    _In_ ULONG InputBytes,
    _Out_writes_bytes_to_(OutputBytes, *ReturnedBytes) VOID *Output,
    _In_ ULONG OutputBytes,
    _Out_ ULONG *ReturnedBytes);

/*
 * Power clients register a nonpageable callback that quiesces devices and
 * submits the shutdown mailbox.  If no callback is installed,
 * an active runtime is denied power transition.  The callback must return
 * success only after Hvr1CompanionGetState reports OFFLINE.
 */
NTSTATUS Hvr1CompanionRegisterPowerCallback(
    _Inout_ HVR1_COMPANION *Companion,
    _In_ HVR1_POWER_PREPARE_CALLBACK Callback,
    _In_opt_ PVOID CallbackContext);

NTSTATUS Hvr1CompanionPrepareForPower(
    _Inout_ HVR1_COMPANION *Companion,
    _In_ HVR1_POWER_EVENT Event);

/* Bugcheck-safe marker only.  This function never calls VMXOFF or VMCALL. */
VOID Hvr1CompanionRecordCrashMarker(
    _Inout_ HVR1_COMPANION *Companion,
    _In_ ULONG BugCheckCode,
    _In_reads_(4) const ULONG_PTR Parameters[4]);

/* Returns FALSE while root execution, shutdown, or quarantine is possible. */
BOOLEAN Hvr1CompanionCanUnload(
    _In_ const HVR1_COMPANION *Companion);

HVR1_COMPANION_STATE Hvr1CompanionGetState(
    _In_ const HVR1_COMPANION *Companion);

typedef struct HVR1_DETACH_BARRIER {
    volatile LONG PreparedCount;
    volatile LONG DetachedCount;
    LONG ExpectedCount;
    LONG Reserved;
} HVR1_DETACH_BARRIER;

/*
 * Low-level CPL0 VMCALL ABI.  Each wrapper loads the existing
 * HV_EFI_CALL_MAGIC into RAX and the operation into RCX.  QUERY (opcode 3)
 * returns token0..3 in RAX/RBX/RCX/RDX, ABI in R8, capabilities in R9,
 * generation in R10, and the current root CPU index in R11.  DETACH issues
 * PREPARE (opcode 4) and COMMIT (opcode 5) in one interrupts-disabled assembly
 * transaction.  PREPARE accepts token0 in RDX, token1 in RBX, token2 in R8,
 * token3 in R9, pivot RIP/RSP in R10/R11, and generation in R12.  COMMIT uses
 * its returned VMCS PA and guest CR0/CR4 without returning through C.  The
 * native pivot publishes the caller's ACK only after VMCLEAR and VMXOFF.
 */
UINT64 Hvr1Cpl0QuerySession(
    _Out_ HVR1_RUNTIME_SESSION *Session);

UINT64 Hvr1Cpl0Detach(
    _In_ const HVR1_PREPARE_ARGUMENTS *Arguments,
    _Out_ volatile LONG *AckAddress,
    _Inout_ HVR1_DETACH_BARRIER *Barrier);

UINT64 Hvr1Cpl0Scan(
    _In_ const HVR1_SCAN_ARGUMENTS *Arguments);

UINT64 Hvr1Cpl0ProcessLocate(
    _In_ const HVR1_PROCESS_ARGUMENTS *Arguments);

UINT64 Hvr1Cpl0ShadowControl(
    _In_ const HVR1_SHADOW_ARGUMENTS *Arguments);

UINT64 Hvr1Cpl0AnalysisControl(
    _In_ const HVR1_ANALYSIS_ARGUMENTS *Arguments);

/* Entry and continuation labels used by PREPARE/COMMIT's host-state pivot. */
VOID Hvr1NativeDetachPivot(VOID);
VOID Hvr1Cpl0DetachContinuation(VOID);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_EFI_RUNTIME_COMPANION_H */
