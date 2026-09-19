#include "vmx_cr3_boundary_roundtrip.h"

#include "driver_interface.h"
#include "vmx_cr3_status_abi.h"
#include "vmx_diagnostic.h"

typedef struct _VMX_CR3_RESIDENCY_SAMPLE {
    uint64_t Transitions;
    uint32_t Resident;
    uint32_t MatchedIndex;
    uint16_t Group;
    uint8_t Number;
    uint8_t Reserved;
} VMX_CR3_RESIDENCY_SAMPLE;

typedef struct _VMX_CR3_RESIDENCY_WORK {
    WORK_QUEUE_ITEM Item;
    KEVENT Complete;
    VMX_PERSISTENT_DRIVER_OWNER* Owner;
    VMX_CR3_BOUNDARY_ROUNDTRIP_STATUS Status;
    VMX_CR3_RESIDENCY_SAMPLE TargetSample;
    UCHAR DiagnosticByte;
    uint16_t Group;
    uint8_t Number;
    BOOLEAN TargetProcessContext;
    PEPROCESS ExpectedProcess;
} VMX_CR3_RESIDENCY_WORK;

static BOOLEAN Zero(const void* Memory, size_t Size)
{
    const UCHAR* Bytes = (const UCHAR*)Memory;
    size_t Index;

    if (Memory == NULL)
        return FALSE;
    for (Index = 0; Index < Size; ++Index)
        if (Bytes[Index] != 0)
            return FALSE;
    return TRUE;
}

static BOOLEAN Separate(
    const void* Left,
    size_t LeftSize,
    const void* Right,
    size_t RightSize)
{
    const UCHAR* LeftBytes = (const UCHAR*)Left;
    const UCHAR* RightBytes = (const UCHAR*)Right;

    if (Left == NULL || Right == NULL)
        return FALSE;
    return LeftBytes + LeftSize <= RightBytes ||
        RightBytes + RightSize <= LeftBytes;
}

static VMX_CR3_BOUNDARY_ROUNDTRIP_STATUS CaptureSample(
    VMX_PERSISTENT_DRIVER_OWNER* Owner,
    VMX_CR3_RESIDENCY_SAMPLE* OutSample)
{
    VMX_PERSISTENT_DRIVER_CR3_SNAPSHOT Snapshot = { 0 };
    HYPERVISOR_CR3_STATUS_HEADER Header = { 0 };
    HYPERVISOR_CR3_STATUS_RECORD Record = { 0 };
    VMX_PERSISTENT_STATUS Status;
    VMX_PERSISTENT_STATUS ReleaseStatus;
    VMX_CR3_RESIDENCY_SAMPLE Staged = { 0 };
    BOOLEAN Valid = FALSE;

    if (Owner == NULL || OutSample == NULL ||
        !Zero(OutSample, sizeof(*OutSample)))
        return VMX_CR3_BOUNDARY_ROUNDTRIP_INVALID;

    Status = VmxPersistentDriverOwnerAcquireCr3StatusSnapshot(
        Owner, &Snapshot);
    if (Status != VMX_PERSISTENT_STATUS_OK)
        return Status == VMX_PERSISTENT_STATUS_NOT_ACTIVE ||
            Status == VMX_PERSISTENT_STATUS_NOT_READY
                ? VMX_CR3_BOUNDARY_ROUNDTRIP_NOT_ACTIVE
                : VMX_CR3_BOUNDARY_ROUNDTRIP_SNAPSHOT_FAILED;

    if (Snapshot.Buffer != NULL &&
        Snapshot.Size >= sizeof(Header) + sizeof(Record)) {
        RtlCopyMemory(&Header, Snapshot.Buffer, sizeof(Header));
        RtlCopyMemory(&Record,
            (const UCHAR*)Snapshot.Buffer + sizeof(Header), sizeof(Record));
        Valid =
            Header.Version == HYPERVISOR_CR3_STATUS_VERSION &&
            Header.HeaderSize == HYPERVISOR_CR3_STATUS_HEADER_SIZE &&
            Header.RecordSize == HYPERVISOR_CR3_STATUS_RECORD_SIZE &&
            Header.TotalSize == Snapshot.Size &&
            Header.ContextCount != 0 &&
            Header.TargetCount == 1 &&
            (Header.Flags & HYPERVISOR_CR3_STATUS_FLAG_AUTHENTICATED) != 0 &&
            Header.Reserved32 == 0 &&
            Record.SystemIndex == 0 &&
            Record.Resident <= 1 &&
            Record.Pcid <= HYPERVISOR_CR3_STATUS_MAX_PCID &&
            Record.Reserved == 0 &&
            ((Record.Resident != 0 && Record.MatchedIndex == 0) ||
             (Record.Resident == 0 &&
              Record.MatchedIndex == HYPERVISOR_CR3_STATUS_NO_INDEX));
        if (Valid) {
            Staged.Transitions = Record.TransitionSequence;
            Staged.Resident = Record.Resident;
            Staged.MatchedIndex = Record.MatchedIndex;
            Staged.Group = Record.Group;
            Staged.Number = Record.Number;
        }
    }

    ReleaseStatus =
        VmxPersistentDriverOwnerReleaseCr3StatusSnapshot(&Snapshot);
    RtlSecureZeroMemory(&Record, sizeof(Record));
    RtlSecureZeroMemory(&Header, sizeof(Header));
    if (!Valid || ReleaseStatus != VMX_PERSISTENT_STATUS_OK) {
        RtlSecureZeroMemory(&Staged, sizeof(Staged));
        return VMX_CR3_BOUNDARY_ROUNDTRIP_SNAPSHOT_FAILED;
    }

    *OutSample = Staged;
    RtlSecureZeroMemory(&Staged, sizeof(Staged));
    return VMX_CR3_BOUNDARY_ROUNDTRIP_OK;
}

static VMX_CR3_BOUNDARY_ROUNDTRIP_STATUS ObservedWritePinned(
    VMX_PERSISTENT_DRIVER_OWNER* Owner,
    uint16_t Group,
    uint8_t Number,
    UCHAR* OutDiagnosticByte,
    VMX_CR3_RESIDENCY_SAMPLE* OutSample)
{
    GROUP_AFFINITY Affinity = { 0 };
    GROUP_AFFINITY Previous = { 0 };
    PROCESSOR_NUMBER Mapped = { 0 };
    PROCESSOR_NUMBER Current = { 0 };
    VMX_PERSISTENT_STATUS PersistentStatus;
    VMX_CR3_BOUNDARY_ROUNDTRIP_STATUS Status;
    ULONG CurrentIndex;

    if (Owner == NULL || OutDiagnosticByte == NULL || OutSample == NULL ||
        *OutDiagnosticByte != 0 || !Zero(OutSample, sizeof(*OutSample)) ||
        Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER ||
        !NT_SUCCESS(KeGetProcessorNumberFromIndex(0, &Mapped)) ||
        Mapped.Group != Group || Mapped.Number != Number)
        return VMX_CR3_BOUNDARY_ROUNDTRIP_INVALID;

    Affinity.Group = Group;
    Affinity.Mask = ((KAFFINITY)1) << Number;
    KeSetSystemGroupAffinityThread(&Affinity, &Previous);
    CurrentIndex = KeGetCurrentProcessorNumberEx(&Current);
    if (CurrentIndex != 0 || Current.Group != Group ||
        Current.Number != Number) {
        Status = VMX_CR3_BOUNDARY_ROUNDTRIP_INVARIANT;
        goto restore;
    }

    PersistentStatus = VmxPersistentDriverOwnerDiagnosticWrite(
        Owner, 0, OutDiagnosticByte);
    if (PersistentStatus != VMX_PERSISTENT_STATUS_OK) {
        Status = PersistentStatus == VMX_PERSISTENT_STATUS_NOT_ACTIVE ||
            PersistentStatus == VMX_PERSISTENT_STATUS_NOT_READY
                ? VMX_CR3_BOUNDARY_ROUNDTRIP_NOT_ACTIVE
                : VMX_CR3_BOUNDARY_ROUNDTRIP_INVARIANT;
        goto restore;
    }
    if (*OutDiagnosticByte != VMX_V3D_DIAGNOSTIC_WRITE_VALUE) {
        Status = VMX_CR3_BOUNDARY_ROUNDTRIP_INVARIANT;
        goto restore;
    }

    Status = CaptureSample(Owner, OutSample);

restore:
    KeRevertToUserGroupAffinityThread(&Previous);
    return Status;
}

static VOID TargetWorkItem(_In_ PVOID Parameter)
{
    VMX_CR3_RESIDENCY_WORK* Work = (VMX_CR3_RESIDENCY_WORK*)Parameter;

    Work->TargetProcessContext =
        Work->ExpectedProcess != NULL &&
        PsGetCurrentProcess() == Work->ExpectedProcess;
    if (!Work->TargetProcessContext) {
        Work->Status = VMX_CR3_BOUNDARY_ROUNDTRIP_INVARIANT;
    } else {
        Work->Status = ObservedWritePinned(
            Work->Owner, Work->Group, Work->Number,
            &Work->DiagnosticByte, &Work->TargetSample);
    }
    KeSetEvent(&Work->Complete, IO_NO_INCREMENT, FALSE);
}

VMX_CR3_BOUNDARY_ROUNDTRIP_STATUS VmxCr3BoundaryRoundtripExecute(
    VMX_PERSISTENT_DRIVER_OWNER* Owner,
    PEPROCESS TargetProcess,
    VMX_CR3_BOUNDARY_ROUNDTRIP_RESULT* OutResult)
{
    VMX_CR3_BOUNDARY_ROUNDTRIP_RESULT Result = { 0 };
    VMX_CR3_RESIDENCY_SAMPLE Identity = { 0 };
    VMX_CR3_RESIDENCY_SAMPLE Before = { 0 };
    VMX_CR3_RESIDENCY_SAMPLE Target = { 0 };
    VMX_CR3_RESIDENCY_SAMPLE After = { 0 };
    VMX_CR3_RESIDENCY_WORK Work = { 0 };
    VMX_CR3_BOUNDARY_ROUNDTRIP_STATUS Status;
    UCHAR DiagnosticByte = 0;
    NTSTATUS WaitStatus;
    BOOLEAN CallerIsTarget;
    BOOLEAN SystemIsTarget;

    if (Owner == NULL || TargetProcess == NULL || OutResult == NULL ||
        !Zero(OutResult, sizeof(*OutResult)) ||
        !Separate(Owner, sizeof(*Owner), OutResult, sizeof(*OutResult)))
        return VMX_CR3_BOUNDARY_ROUNDTRIP_INVALID;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_CR3_BOUNDARY_ROUNDTRIP_WRONG_IRQL;
    if (PsInitialSystemProcess == NULL)
        return VMX_CR3_BOUNDARY_ROUNDTRIP_INVARIANT;
    CallerIsTarget = PsGetCurrentProcess() == TargetProcess;
    SystemIsTarget = TargetProcess == PsInitialSystemProcess;
    if (CallerIsTarget == SystemIsTarget)
        return VMX_CR3_BOUNDARY_ROUNDTRIP_INVARIANT;

    Status = CaptureSample(Owner, &Identity);
    if (Status != VMX_CR3_BOUNDARY_ROUNDTRIP_OK)
        goto publish;
    Result.ProcessorGroup = Identity.Group;
    Result.ProcessorNumber = Identity.Number;

    Status = ObservedWritePinned(
        Owner, Identity.Group, Identity.Number, &DiagnosticByte, &Before);
    Result.BaselineByteMatch =
        DiagnosticByte == VMX_V3D_DIAGNOSTIC_WRITE_VALUE;
    if (Result.BaselineByteMatch != 0)
        Result.Stage =
            HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_BASELINE_WRITE_RETURNED;
    if (Status != VMX_CR3_BOUNDARY_ROUNDTRIP_OK) {
        if (Status != VMX_CR3_BOUNDARY_ROUNDTRIP_NOT_ACTIVE)
            Status = VMX_CR3_BOUNDARY_ROUNDTRIP_BASELINE_FAILED;
        goto publish;
    }

    Result.BeforeTransitions = Before.Transitions;
    Result.BeforeResident = Before.Resident;
    Result.BeforeMatchedIndex = Before.MatchedIndex;
    Result.Stage = HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_BEFORE_CAPTURED;
    if (Before.Group != Identity.Group || Before.Number != Identity.Number ||
        Before.Resident != (CallerIsTarget ? 1u : 0u) ||
        Before.MatchedIndex != (CallerIsTarget
            ? 0u : HYPERVISOR_CR3_STATUS_NO_INDEX)) {
        Status = VMX_CR3_BOUNDARY_ROUNDTRIP_INVARIANT;
        goto publish;
    }

    Work.Owner = Owner;
    Work.ExpectedProcess = PsInitialSystemProcess;
    Work.Group = Identity.Group;
    Work.Number = Identity.Number;
    KeInitializeEvent(&Work.Complete, NotificationEvent, FALSE);
    ExInitializeWorkItem(&Work.Item, TargetWorkItem, &Work);
    ExQueueWorkItem(&Work.Item, DelayedWorkQueue);
    WaitStatus = KeWaitForSingleObject(
        &Work.Complete, Executive, KernelMode, FALSE, NULL);
    Result.TargetByteMatch =
        Work.DiagnosticByte == VMX_V3D_DIAGNOSTIC_WRITE_VALUE;
    if (Result.TargetByteMatch != 0)
        Result.Stage =
            HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_TARGET_WRITE_RETURNED;
    if (WaitStatus != STATUS_SUCCESS || !Work.TargetProcessContext ||
        Work.Status != VMX_CR3_BOUNDARY_ROUNDTRIP_OK) {
        Status = VMX_CR3_BOUNDARY_ROUNDTRIP_TARGET_FAILED;
        goto publish;
    }

    Target = Work.TargetSample;
    Result.TargetTransitions = Target.Transitions;
    Result.TargetResident = Target.Resident;
    Result.TargetMatchedIndex = Target.MatchedIndex;
    Result.Stage = HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_TARGET_CAPTURED;
    if (Target.Group != Before.Group || Target.Number != Before.Number ||
        Target.Transitions <= Before.Transitions ||
        Target.Resident != (SystemIsTarget ? 1u : 0u) ||
        Target.MatchedIndex != (SystemIsTarget
            ? 0u : HYPERVISOR_CR3_STATUS_NO_INDEX)) {
        Status = VMX_CR3_BOUNDARY_ROUNDTRIP_INVARIANT;
        goto publish;
    }

    DiagnosticByte = 0;
    Status = ObservedWritePinned(
        Owner, Identity.Group, Identity.Number, &DiagnosticByte, &After);
    Result.CallerByteMatch =
        DiagnosticByte == VMX_V3D_DIAGNOSTIC_WRITE_VALUE;
    if (Result.CallerByteMatch != 0)
        Result.Stage =
            HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_CALLER_WRITE_RETURNED;
    if (Status != VMX_CR3_BOUNDARY_ROUNDTRIP_OK) {
        Status = VMX_CR3_BOUNDARY_ROUNDTRIP_CALLER_FAILED;
        goto publish;
    }

    Result.AfterTransitions = After.Transitions;
    Result.AfterResident = After.Resident;
    Result.AfterMatchedIndex = After.MatchedIndex;
    Result.Stage = HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_CALLER_CAPTURED;
    if (After.Group != Before.Group || After.Number != Before.Number ||
        After.Transitions <= Target.Transitions ||
        After.Resident != (CallerIsTarget ? 1u : 0u) ||
        After.MatchedIndex != (CallerIsTarget
            ? 0u : HYPERVISOR_CR3_STATUS_NO_INDEX)) {
        Status = VMX_CR3_BOUNDARY_ROUNDTRIP_INVARIANT;
        goto publish;
    }

    Result.Stage = HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_COMPLETE;
    Status = VMX_CR3_BOUNDARY_ROUNDTRIP_OK;

publish:
    *OutResult = Result;
    RtlSecureZeroMemory(&Work, sizeof(Work));
    RtlSecureZeroMemory(&Identity, sizeof(Identity));
    RtlSecureZeroMemory(&Before, sizeof(Before));
    RtlSecureZeroMemory(&Target, sizeof(Target));
    RtlSecureZeroMemory(&After, sizeof(After));
    RtlSecureZeroMemory(&Result, sizeof(Result));
    return Status;
}
