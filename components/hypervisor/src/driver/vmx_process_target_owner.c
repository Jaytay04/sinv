#include "vmx_process_target_owner.h"

#include "vmx_cr3_target_provider.h"
#include "vmx_process_target_policy.h"

NTKERNELAPI
NTSTATUS
PsLookupProcessByProcessId(
    _In_ HANDLE ProcessId,
    _Outptr_ PEPROCESS* Process);

static BOOLEAN Zero(const void* Memory, SIZE_T Size)
{
    const UCHAR* Bytes = (const UCHAR*)Memory;
    SIZE_T Index;
    if (Memory == NULL)
        return FALSE;
    for (Index = 0; Index < Size; ++Index)
        if (Bytes[Index] != 0)
            return FALSE;
    return TRUE;
}

static BOOLEAN ProcessAlive(PEPROCESS Process)
{
    return Process != NULL && PsGetProcessExitStatus(Process) == STATUS_PENDING;
}

static LONG ReadState(const VMX_PROCESS_TARGET_OWNER* Owner)
{
    return InterlockedCompareExchange(
        (volatile LONG*)&Owner->State, 0, 0);
}

static void Expire(VMX_PROCESS_TARGET_OWNER* Owner)
{
    LONG State;
    do {
        State = ReadState(Owner);
        if (State != (LONG)HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED &&
            State != (LONG)HYPERVISOR_PROCESS_TARGET_STATE_ACTIVE)
            return;
    } while (InterlockedCompareExchange(&Owner->State,
        (LONG)HYPERVISOR_PROCESS_TARGET_STATE_EXPIRED, State) != State);
}

static VMX_PROCESS_TARGET_OWNER_STATUS ConfigureOwnedProcess(
    VMX_PROCESS_TARGET_OWNER* Owner, PEPROCESS Process,
    uint64_t ProcessId, uint64_t TargetGeneration)
{
    VMX_CR3_TARGET_SET TargetSet = { 0 };
    VMX_CR3_TARGET_STATUS CaptureStatus;
    ULONG64 StartKey;

    if (!ProcessAlive(Process)) {
        ObDereferenceObject(Process);
        return VMX_PROCESS_TARGET_OWNER_EXITED;
    }
    StartKey = PsGetProcessStartKey(Process);
    if (StartKey == 0) {
        ObDereferenceObject(Process);
        return VMX_PROCESS_TARGET_OWNER_INVARIANT;
    }
    CaptureStatus = VmxPersistentProcessCr3TargetCapture(
        Process, TargetGeneration, &TargetSet);
    if (CaptureStatus != VMX_CR3_TARGET_OK) {
        ObDereferenceObject(Process);
        return VMX_PROCESS_TARGET_OWNER_CAPTURE_FAILED;
    }

    if ((uint64_t)(ULONG_PTR)PsGetProcessId(Process) != ProcessId) {
        RtlSecureZeroMemory(&TargetSet, sizeof(TargetSet));
        ObDereferenceObject(Process);
        return VMX_PROCESS_TARGET_OWNER_INVARIANT;
    }
    Owner->Process = Process;
    Owner->ProcessId = (HANDLE)(ULONG_PTR)ProcessId;
    Owner->ProcessStartKey = (uint64_t)StartKey;
    Owner->TargetSet = TargetSet;
    KeMemoryBarrier();
    InterlockedExchange(&Owner->State,
        (LONG)HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED);
    RtlSecureZeroMemory(&TargetSet, sizeof(TargetSet));
    if (!ProcessAlive(Process))
        Expire(Owner);
    return ReadState(Owner) ==
            (LONG)HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED
        ? VMX_PROCESS_TARGET_OWNER_OK
        : VMX_PROCESS_TARGET_OWNER_EXITED;
}

VMX_PROCESS_TARGET_OWNER_STATUS VmxProcessTargetOwnerConfigure(
    VMX_PROCESS_TARGET_OWNER* Owner, uint64_t ProcessId,
    uint64_t TargetGeneration)
{
    PEPROCESS Process = NULL;
    NTSTATUS Status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL || Owner == NULL ||
        ProcessId == 0 || ProcessId > (uint64_t)(ULONG_PTR)-1 ||
        TargetGeneration == 0 || !Zero(Owner, sizeof(*Owner)))
        return VMX_PROCESS_TARGET_OWNER_INVALID;
    Status = PsLookupProcessByProcessId(
        (HANDLE)(ULONG_PTR)ProcessId, &Process);
    if (!NT_SUCCESS(Status))
        return VMX_PROCESS_TARGET_OWNER_NOT_FOUND;
    return ConfigureOwnedProcess(
        Owner, Process, ProcessId, TargetGeneration);
}

VMX_PROCESS_TARGET_OWNER_STATUS VmxProcessTargetOwnerConfigureReferenced(
    VMX_PROCESS_TARGET_OWNER* Owner, PEPROCESS Process,
    uint64_t TargetGeneration)
{
    uint64_t ProcessId;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL || Owner == NULL ||
        Process == NULL || TargetGeneration == 0 ||
        !Zero(Owner, sizeof(*Owner)))
        return VMX_PROCESS_TARGET_OWNER_INVALID;
    ProcessId = (uint64_t)(ULONG_PTR)PsGetProcessId(Process);
    if (ProcessId == 0)
        return VMX_PROCESS_TARGET_OWNER_INVARIANT;
    ObReferenceObject(Process);
    return ConfigureOwnedProcess(
        Owner, Process, ProcessId, TargetGeneration);
}

VMX_PROCESS_TARGET_OWNER_STATUS VmxProcessTargetOwnerActivate(
    VMX_PROCESS_TARGET_OWNER* Owner, VMX_CR3_TARGET_SET* OutTargetSet)
{
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || Owner == NULL ||
        OutTargetSet == NULL || !Zero(OutTargetSet, sizeof(*OutTargetSet)))
        return VMX_PROCESS_TARGET_OWNER_INVALID;
    if (!ProcessAlive(Owner->Process))
        Expire(Owner);
    if (InterlockedCompareExchange(&Owner->State,
            (LONG)HYPERVISOR_PROCESS_TARGET_STATE_ACTIVE,
            (LONG)HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED) !=
            (LONG)HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED)
        return ReadState(Owner) ==
                (LONG)HYPERVISOR_PROCESS_TARGET_STATE_EXPIRED
            ? VMX_PROCESS_TARGET_OWNER_EXITED
            : VMX_PROCESS_TARGET_OWNER_BUSY;
    *OutTargetSet = Owner->TargetSet;
    return VMX_PROCESS_TARGET_OWNER_OK;
}

VMX_PROCESS_TARGET_OWNER_STATUS VmxProcessTargetOwnerDeactivate(
    VMX_PROCESS_TARGET_OWNER* Owner)
{
    LONG State;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || Owner == NULL)
        return VMX_PROCESS_TARGET_OWNER_INVALID;
    State = ReadState(Owner);
    if (State == (LONG)HYPERVISOR_PROCESS_TARGET_STATE_EXPIRED)
        return VMX_PROCESS_TARGET_OWNER_OK;
    if (InterlockedCompareExchange(&Owner->State,
            (LONG)HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED,
            (LONG)HYPERVISOR_PROCESS_TARGET_STATE_ACTIVE) !=
            (LONG)HYPERVISOR_PROCESS_TARGET_STATE_ACTIVE)
        return VMX_PROCESS_TARGET_OWNER_BUSY;
    if (!ProcessAlive(Owner->Process))
        Expire(Owner);
    return VMX_PROCESS_TARGET_OWNER_OK;
}

void VmxProcessTargetOwnerNotifyExit(
    VMX_PROCESS_TARGET_OWNER* Owner, PEPROCESS Process)
{
    if (Owner == NULL || Process == NULL)
        return;
    KeMemoryBarrier();
    if (Owner->Process == Process)
        Expire(Owner);
}

VMX_PROCESS_TARGET_OWNER_STATUS VmxProcessTargetOwnerQuery(
    VMX_PROCESS_TARGET_OWNER* Owner,
    HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE* OutResponse)
{
    HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE Response = { 0 };
    LONG Before;
    LONG After;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL || Owner == NULL ||
        OutResponse == NULL || !Zero(OutResponse, sizeof(*OutResponse)))
        return VMX_PROCESS_TARGET_OWNER_INVALID;
    if (Owner->Process != NULL && !ProcessAlive(Owner->Process))
        Expire(Owner);
    do {
        Before = ReadState(Owner);
        KeMemoryBarrier();
        Response.Version = HYPERVISOR_PROCESS_TARGET_VERSION;
        Response.Size = HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE_SIZE;
        Response.Status = HYPERVISOR_PROCESS_TARGET_STATUS_SUCCESS;
        Response.State = (uint32_t)Before;
        if (Before != (LONG)HYPERVISOR_PROCESS_TARGET_STATE_EMPTY) {
            Response.ProcessId = (uint64_t)(ULONG_PTR)Owner->ProcessId;
            Response.ProcessStartKey = Owner->ProcessStartKey;
            Response.TargetGeneration = Owner->TargetSet.Generation;
            Response.Flags = HYPERVISOR_PROCESS_TARGET_FLAG_REFERENCED |
                HYPERVISOR_PROCESS_TARGET_FLAG_ROOT_CAPTURED;
            if (Before == (LONG)HYPERVISOR_PROCESS_TARGET_STATE_ACTIVE)
                Response.Flags |= HYPERVISOR_PROCESS_TARGET_FLAG_ROUTE_ACTIVE;
        }
        KeMemoryBarrier();
        After = ReadState(Owner);
        if (Before != After)
            RtlSecureZeroMemory(&Response, sizeof(Response));
    } while (Before != After);
    *OutResponse = Response;
    return VMX_PROCESS_TARGET_OWNER_OK;
}

VMX_PROCESS_TARGET_OWNER_STATUS VmxProcessTargetOwnerBorrowActiveProcess(
    VMX_PROCESS_TARGET_OWNER* Owner, PEPROCESS* OutProcess)
{
    LONG State;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || Owner == NULL ||
        OutProcess == NULL || *OutProcess != NULL)
        return VMX_PROCESS_TARGET_OWNER_INVALID;
    if (!ProcessAlive(Owner->Process))
        Expire(Owner);
    State = ReadState(Owner);
    if (State != (LONG)HYPERVISOR_PROCESS_TARGET_STATE_ACTIVE)
        return State == (LONG)HYPERVISOR_PROCESS_TARGET_STATE_EXPIRED
            ? VMX_PROCESS_TARGET_OWNER_EXITED
            : VMX_PROCESS_TARGET_OWNER_BUSY;
    *OutProcess = Owner->Process;
    return *OutProcess != NULL
        ? VMX_PROCESS_TARGET_OWNER_OK
        : VMX_PROCESS_TARGET_OWNER_INVARIANT;
}

static VMX_PROCESS_TARGET_OWNER_STATUS ReleaseState(
    VMX_PROCESS_TARGET_OWNER* Owner, LONG State)
{
    PEPROCESS Process;

    if (InterlockedCompareExchange(&Owner->State,
            (LONG)HYPERVISOR_PROCESS_TARGET_STATE_EMPTY, State) != State)
        return VMX_PROCESS_TARGET_OWNER_BUSY;
    KeMemoryBarrier();
    Process = Owner->Process;
    Owner->Process = NULL;
    Owner->ProcessId = NULL;
    Owner->ProcessStartKey = 0;
    RtlSecureZeroMemory(&Owner->TargetSet, sizeof(Owner->TargetSet));
    if (Process == NULL)
        return VMX_PROCESS_TARGET_OWNER_INVARIANT;
    ObDereferenceObject(Process);
    return Zero(Owner, sizeof(*Owner))
        ? VMX_PROCESS_TARGET_OWNER_OK
        : VMX_PROCESS_TARGET_OWNER_INVARIANT;
}

VMX_PROCESS_TARGET_OWNER_STATUS VmxProcessTargetOwnerRelease(
    VMX_PROCESS_TARGET_OWNER* Owner)
{
    LONG State;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL || Owner == NULL)
        return VMX_PROCESS_TARGET_OWNER_INVALID;
    State = ReadState(Owner);
    if (State != (LONG)HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED &&
        State != (LONG)HYPERVISOR_PROCESS_TARGET_STATE_EXPIRED)
        return State == (LONG)HYPERVISOR_PROCESS_TARGET_STATE_ACTIVE
            ? VMX_PROCESS_TARGET_OWNER_BUSY
            : VMX_PROCESS_TARGET_OWNER_INVALID;
    return ReleaseState(Owner, State);
}

VMX_PROCESS_TARGET_OWNER_STATUS VmxProcessTargetOwnerReleaseMatched(
    VMX_PROCESS_TARGET_OWNER* Owner, uint64_t ProcessId,
    uint64_t ProcessStartKey, uint64_t TargetGeneration)
{
    LONG State;
    VMX_PROCESS_TARGET_OWNER_STATUS Status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL || Owner == NULL ||
        ProcessId == 0 || ProcessStartKey == 0 || TargetGeneration == 0)
        return VMX_PROCESS_TARGET_OWNER_INVALID;

    for (;;) {
        State = ReadState(Owner);
        if (State != (LONG)HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED &&
            State != (LONG)HYPERVISOR_PROCESS_TARGET_STATE_EXPIRED)
            return State == (LONG)HYPERVISOR_PROCESS_TARGET_STATE_ACTIVE
                ? VMX_PROCESS_TARGET_OWNER_BUSY
                : VMX_PROCESS_TARGET_OWNER_INVALID;
        KeMemoryBarrier();
        if ((uint64_t)(ULONG_PTR)Owner->ProcessId != ProcessId ||
            Owner->ProcessStartKey != ProcessStartKey ||
            Owner->TargetSet.Generation != TargetGeneration)
            return VMX_PROCESS_TARGET_OWNER_MISMATCH;
        KeMemoryBarrier();
        Status = ReleaseState(Owner, State);
        if (Status != VMX_PROCESS_TARGET_OWNER_BUSY)
            return Status;
        if (State != (LONG)HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED ||
            ReadState(Owner) != (LONG)HYPERVISOR_PROCESS_TARGET_STATE_EXPIRED)
            return VMX_PROCESS_TARGET_OWNER_BUSY;
    }
}
