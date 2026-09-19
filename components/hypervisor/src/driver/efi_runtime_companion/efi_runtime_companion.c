/*
 * Optional Windows kernel companion for the EFI resident hypervisor.
 *
 * This module owns the transport and rendezvous policy.  The WDM wrapper in
 * hvr1_driver.c owns the device object and ACL.  This module authenticates
 * each mailbox with a boot-scoped HMAC-SHA-256 session before the exact EFI
 * VMCALL ABI uses the root-owned token on every processor.
 */

#include <ntddk.h>
#include <bcrypt.h>

#include "efi_runtime_companion.h"
#include "hvr1_auth.h"

#define HVR1_POOL_TAG '1RHV'
#define HVR1_PIVOT_STACK_BYTES 4096u
#define HVR1_MAX_IPI_CPUS HVR1_MAILBOX_ACK_SLOTS

typedef struct HVR1_CRASH_MARKER {
    volatile LONG Recorded;
    ULONG BugCheckCode;
    ULONG Cpu;
    ULONG_PTR Parameters[4];
    volatile LONG64 Sequence;
} HVR1_CRASH_MARKER;

typedef struct HVR1_MAILBOX_VIEW {
    ULONG TotalSize;
    ULONG Operation;
    UINT64 Generation;
    UINT64 Sequence;
} HVR1_MAILBOX_VIEW;

typedef struct HVR1_CPU_STATE {
    volatile LONG Claimed;
    UINT64 PivotStackBase;
    UINT64 PivotStackTop;
    UINT64 VmcsOperand;
    HVR1_RUNTIME_SESSION Session;
    HVR1_PREPARE_RESULT Prepare;
} HVR1_CPU_STATE;

typedef struct HVR1_COMPANION {
    FAST_MUTEX Lock;

    /* The mailbox is contiguous and nonpaged because the EFI root consumes
     * its physical address on each processor during the rendezvous. */
    HVR1_MAILBOX *Mailbox;
    PHYSICAL_ADDRESS MailboxPhysical;

    /* Per-CPU pivot stacks remain resident until the root is OFFLINE. */
    HVR1_CPU_STATE *CpuStates;
    PUCHAR PivotStacks;
    ULONG CpuCount;

    volatile LONG State;
    volatile LONG LastStatus;
    volatile LONG ExpectedCpuCount;
    volatile LONG AcknowledgedCpuCount;
    volatile LONG FailedCpuCount;
    volatile LONG FirstCpuFailure;
    volatile LONG64 LastGeneration;
    volatile LONG64 LastSequence;
    volatile LONG64 RendezvousGeneration;
    HVR1_DETACH_BARRIER DetachBarrier;

    UCHAR AuthenticationKey[HVR1_AUTH_KEY_BYTES];
    UCHAR AuthenticationNonce[HVR1_AUTH_NONCE_BYTES];

    HVR1_POWER_PREPARE_CALLBACK PowerCallback;
    PVOID PowerCallbackContext;
    HVR1_CRASH_MARKER Crash;
} HVR1_COMPANION;

static NTSTATUS
Hvr1MapCpl0Status(
    _In_ UINT64 Status)
{
    switch ((HVR1_CPL0_STATUS)Status) {
    case HVR1_CPL0_STATUS_OK:
        return STATUS_SUCCESS;
    case HVR1_CPL0_STATUS_STALE_TOKEN:
    case HVR1_CPL0_STATUS_REJECTED:
    case HVR1_CPL0_STATUS_INVALID_MAILBOX:
        return STATUS_ACCESS_DENIED;
    case HVR1_CPL0_STATUS_NOT_READY:
        return STATUS_DEVICE_NOT_READY;
    case HVR1_CPL0_STATUS_ALREADY_OFFLINE:
        return STATUS_DEVICE_NOT_READY;
    case HVR1_CPL0_STATUS_CPU_FAILURE:
    default:
        return STATUS_UNSUCCESSFUL;
    }
}

/*
 * Validate before copying any bytes into the driver-owned mailbox.  The
 * caller's buffer is a METHOD_BUFFERED system buffer, but it remains
 * untrusted input.  Cryptographic authentication is performed separately,
 * after all structural bounds are known and before any state transition.
 */
static NTSTATUS
Hvr1ValidateMailbox(
    _In_reads_bytes_(InputBytes) const VOID *Input,
    _In_ SIZE_T InputBytes,
    _Out_ HVR1_MAILBOX_VIEW *View)
{
    const HVR1_MAILBOX *mailbox;

    if (Input == NULL || View == NULL || InputBytes < HVR1_MAILBOX_HEADER_BYTES)
        return STATUS_INVALID_BUFFER_SIZE;

    mailbox = (const HVR1_MAILBOX *)Input;
    if (mailbox->Magic != HVR1_PROTOCOL_MAGIC ||
        mailbox->Version != HVR1_PROTOCOL_VERSION ||
        mailbox->HeaderSize != HVR1_MAILBOX_HEADER_BYTES ||
        mailbox->PayloadOffset != HVR1_MAILBOX_HEADER_BYTES ||
        mailbox->Operation != HVR1_MAILBOX_OPERATION_RUNTIME_FRAME ||
        mailbox->Flags != 0 ||
        mailbox->Status != 0 ||
        mailbox->Reserved != 0 ||
        mailbox->TotalSize < HVR1_MAILBOX_HEADER_BYTES ||
        mailbox->TotalSize > HVR1_MAILBOX_BYTES ||
        mailbox->TotalSize > InputBytes ||
        mailbox->PayloadSize != 0 ||
        mailbox->TotalSize != HVR1_MAILBOX_HEADER_BYTES ||
        mailbox->Generation == 0 ||
        mailbox->Sequence == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    View->TotalSize = mailbox->TotalSize;
    View->Operation = mailbox->Operation;
    View->Generation = mailbox->Generation;
    View->Sequence = mailbox->Sequence;
    return STATUS_SUCCESS;
}

static BOOLEAN
Hvr1ProcessorIndex(
    _Out_ ULONG *ProcessorIndex)
{
    PROCESSOR_NUMBER number;

    if (ProcessorIndex == NULL) {
        return FALSE;
    }
    KeGetCurrentProcessorNumberEx(&number);
    *ProcessorIndex = KeGetProcessorIndexFromNumber(&number);
    return *ProcessorIndex != INVALID_PROCESSOR_INDEX;
}

static VOID
Hvr1RecordCpuFailure(
    _Inout_ HVR1_COMPANION *Companion,
    _Inout_ HVR1_CPU_STATE *Cpu,
    _In_ NTSTATUS Status)
{
    Cpu->Prepare.Status = (UINT64)Status;
    InterlockedIncrement(&Companion->FailedCpuCount);
    InterlockedCompareExchange(&Companion->FirstCpuFailure,
                               (LONG)Status,
                               0);
}

/*
 * Every active processor performs QUERY, PREPARE, and the immediately
 * following COMMIT on itself.  PREPARE must not be moved outside this
 * callback: root token and VMCS ownership are processor-specific.
 */
static ULONG_PTR
Hvr1ShutdownOnCpu(
    _In_ ULONG_PTR ContextValue)
{
    HVR1_COMPANION *companion = (HVR1_COMPANION *)ContextValue;
    HVR1_CPU_STATE *cpu;
    HVR1_PREPARE_ARGUMENTS prepareArguments;
    ULONG processorIndex;
    UINT64 token0;
    UINT64 status;
    NTSTATUS ntStatus;

    if (!Hvr1ProcessorIndex(&processorIndex) ||
        processorIndex >= companion->CpuCount) {
        InterlockedIncrement(&companion->FailedCpuCount);
        InterlockedCompareExchange(&companion->FirstCpuFailure,
                                   (LONG)STATUS_INVALID_DEVICE_STATE,
                                   0);
        return 0;
    }
    cpu = &companion->CpuStates[processorIndex];
    if (InterlockedCompareExchange(&cpu->Claimed, 1, 0) != 0) {
        Hvr1RecordCpuFailure(companion, cpu, STATUS_DEVICE_BUSY);
        return 0;
    }

    /* QUERY_SESSION returns four token words and the current root CPU. */
    token0 = Hvr1Cpl0QuerySession(&cpu->Session);
    if (token0 == 0 || cpu->Session.AbiVersion != HVR1_RUNTIME_ABI_VERSION ||
        (cpu->Session.Capabilities & HVR1_RUNTIME_CAP_SHUTDOWN) == 0 ||
        cpu->Session.Generation == 0) {
        Hvr1RecordCpuFailure(companion, cpu, STATUS_ACCESS_DENIED);
        return 0;
    }
    if (InterlockedCompareExchange64(&companion->RendezvousGeneration,
                                     (LONG64)cpu->Session.Generation,
                                     0) != 0 &&
        companion->RendezvousGeneration != (LONG64)cpu->Session.Generation) {
        Hvr1RecordCpuFailure(companion, cpu, STATUS_REVISION_MISMATCH);
        return 0;
    }

    /* Reserve this unique slot before root can publish ACK_OK. */
    if (InterlockedCompareExchange(
            (volatile LONG *)&companion->Mailbox->AckByProcessor[processorIndex],
            HVR1_MAILBOX_ACK_PREPARED,
            HVR1_MAILBOX_ACK_NOT_STARTED) != HVR1_MAILBOX_ACK_NOT_STARTED) {
        Hvr1RecordCpuFailure(companion, cpu, STATUS_DEVICE_BUSY);
        return 0;
    }

    RtlZeroMemory(&prepareArguments, sizeof(prepareArguments));
    RtlCopyMemory(prepareArguments.Token,
                  cpu->Session.Token,
                  sizeof(prepareArguments.Token));
    prepareArguments.PivotRip = (UINT64)(ULONG_PTR)&Hvr1NativeDetachPivot;
    prepareArguments.PivotRsp = cpu->PivotStackTop;
    prepareArguments.Generation = cpu->Session.Generation;
    status = Hvr1Cpl0Detach(
        &prepareArguments,
        (volatile LONG *)&companion->Mailbox->AckByProcessor[processorIndex],
        &companion->DetachBarrier);
    if (status != HVR1_CPL0_STATUS_OK ||
        companion->Mailbox->AckByProcessor[processorIndex] !=
            HVR1_MAILBOX_ACK_OK) {
        ntStatus = Hvr1MapCpl0Status(status);
        if (NT_SUCCESS(ntStatus))
            ntStatus = STATUS_UNSUCCESSFUL;
        Hvr1RecordCpuFailure(companion, cpu, ntStatus);
    }
    return 0;
}

static NTSTATUS
Hvr1SubmitShutdownLocked(
    _Inout_ HVR1_COMPANION *Companion,
    _In_ const HVR1_IOCTL_REQUEST *Request,
    _In_reads_bytes_(MailboxBytes) const VOID *MailboxInput,
    _In_ ULONG MailboxBytes)
{
    HVR1_MAILBOX_VIEW view;
    ULONG activeCpuCount;
    ULONG processor;
    LONG acknowledged = 0;
    LONG failed = 0;
    NTSTATUS status;

    status = Hvr1ValidateMailbox(MailboxInput, MailboxBytes, &view);
    if (!NT_SUCCESS(status))
        return status;
    if (!Hvr1VerifyMailboxAuthTag(
            Companion->AuthenticationKey,
            Companion->AuthenticationNonce,
            (const HVR1_MAILBOX *)MailboxInput))
        return STATUS_ACCESS_DENIED;
    if (Request->ExpectedGeneration != 0 &&
        Request->ExpectedGeneration != view.Generation)
        return STATUS_RETRY;
    if (Companion->LastGeneration != 0 &&
        view.Generation <= (UINT64)Companion->LastGeneration)
        return STATUS_INVALID_PARAMETER;
    if (view.Sequence <= (UINT64)Companion->LastSequence)
        return STATUS_INVALID_PARAMETER;
    if (Companion->State == HVR1_COMPANION_STATE_OFFLINE)
        return STATUS_DEVICE_NOT_READY;
    if (Companion->State == HVR1_COMPANION_STATE_SHUTDOWN_PREPARING ||
        Companion->State == HVR1_COMPANION_STATE_SHUTDOWN_COMMITTING)
        return STATUS_DEVICE_BUSY;
    if (Companion->State == HVR1_COMPANION_STATE_QUARANTINED)
        return STATUS_ACCESS_DENIED;

    activeCpuCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    if (activeCpuCount == 0 || activeCpuCount > Companion->CpuCount ||
        activeCpuCount > HVR1_MAX_IPI_CPUS)
        return STATUS_TOO_MANY_CONTEXT_IDS;
    RtlZeroMemory(Companion->Mailbox, HVR1_MAILBOX_BYTES);
    RtlCopyMemory(Companion->Mailbox, MailboxInput, view.TotalSize);
    for (processor = 0; processor < Companion->CpuCount; ++processor) {
        Companion->CpuStates[processor].Claimed = 0;
        RtlZeroMemory(&Companion->CpuStates[processor].Session,
                      sizeof(Companion->CpuStates[processor].Session));
        RtlZeroMemory(&Companion->CpuStates[processor].Prepare,
                      sizeof(Companion->CpuStates[processor].Prepare));
    }
    InterlockedExchange(&Companion->ExpectedCpuCount, (LONG)activeCpuCount);
    InterlockedExchange(&Companion->AcknowledgedCpuCount, 0);
    InterlockedExchange(&Companion->FailedCpuCount, 0);
    InterlockedExchange(&Companion->FirstCpuFailure, 0);
    InterlockedExchange64(&Companion->RendezvousGeneration,
                          (LONG64)view.Generation);
    InterlockedExchange(&Companion->DetachBarrier.PreparedCount, 0);
    InterlockedExchange(&Companion->DetachBarrier.DetachedCount, 0);
    Companion->DetachBarrier.ExpectedCount = (LONG)activeCpuCount;
    Companion->DetachBarrier.Reserved = 0;
    InterlockedExchange(&Companion->State,
                        HVR1_COMPANION_STATE_SHUTDOWN_COMMITTING);

    /* The callback performs the complete per-CPU QUERY/PREPARE/COMMIT. */
    (VOID)KeIpiGenericCall(Hvr1ShutdownOnCpu, (ULONG_PTR)Companion);
    KeMemoryBarrier();
    for (processor = 0; processor < activeCpuCount; ++processor) {
        if (Companion->Mailbox->AckByProcessor[processor] ==
            HVR1_MAILBOX_ACK_OK)
            ++acknowledged;
        else if (Companion->Mailbox->AckByProcessor[processor] ==
                 HVR1_MAILBOX_ACK_FAILURE)
            ++failed;
    }
    InterlockedExchange(&Companion->AcknowledgedCpuCount, acknowledged);
    InterlockedExchange(&Companion->FailedCpuCount,
                        Companion->FailedCpuCount + failed);

    if (Companion->FirstCpuFailure != 0 ||
        Companion->FailedCpuCount != 0 ||
        Companion->AcknowledgedCpuCount != Companion->ExpectedCpuCount) {
        InterlockedExchange(&Companion->LastStatus,
                            Companion->FirstCpuFailure != 0
                                ? Companion->FirstCpuFailure
                                : (LONG)STATUS_DEVICE_BUSY);
        InterlockedExchange(&Companion->State,
                            HVR1_COMPANION_STATE_QUARANTINED);
        return STATUS_DEVICE_BUSY;
    }

    InterlockedExchange64(&Companion->LastGeneration,
                          (LONG64)view.Generation);
    InterlockedExchange64(&Companion->LastSequence,
                          (LONG64)view.Sequence);
    InterlockedExchange64(&Companion->RendezvousGeneration, 0);
    InterlockedExchange(&Companion->LastStatus, (LONG)STATUS_SUCCESS);
    InterlockedExchange(&Companion->State, HVR1_COMPANION_STATE_OFFLINE);
    return STATUS_SUCCESS;
}

NTSTATUS
Hvr1CompanionCreate(
    _Outptr_ HVR1_COMPANION **Companion)
{
    PHYSICAL_ADDRESS lowAddress;
    PHYSICAL_ADDRESS highAddress;
    PHYSICAL_ADDRESS boundaryAddress;
    SIZE_T stackBytes;
    ULONG processor;
    HVR1_COMPANION *context;

    if (Companion == NULL)
        return STATUS_INVALID_PARAMETER;
    *Companion = NULL;

    context = (HVR1_COMPANION *)ExAllocatePool2(
        POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED,
        sizeof(*context),
        HVR1_POOL_TAG);
    if (context == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(context, sizeof(*context));
    ExInitializeFastMutex(&context->Lock);
    context->State = HVR1_COMPANION_STATE_INITIALIZED;
    if (!NT_SUCCESS(BCryptGenRandom(
            NULL, context->AuthenticationKey,
            HVR1_AUTH_KEY_BYTES, BCRYPT_USE_SYSTEM_PREFERRED_RNG)) ||
        !NT_SUCCESS(BCryptGenRandom(
            NULL, context->AuthenticationNonce,
            HVR1_AUTH_NONCE_BYTES, BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        RtlSecureZeroMemory(context, sizeof(*context));
        ExFreePool(context);
        return STATUS_UNSUCCESSFUL;
    }

    lowAddress.QuadPart = 0;
    highAddress.QuadPart = (LONGLONG)~(ULONGLONG)0;
    boundaryAddress.QuadPart = 0;
    context->Mailbox = (HVR1_MAILBOX *)MmAllocateContiguousMemorySpecifyCache(
        HVR1_MAILBOX_BYTES,
        lowAddress,
        highAddress,
        boundaryAddress,
        MmCached);
    if (context->Mailbox == NULL) {
        RtlSecureZeroMemory(context, sizeof(*context));
        ExFreePool(context);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(context->Mailbox, HVR1_MAILBOX_BYTES);
    context->MailboxPhysical = MmGetPhysicalAddress(context->Mailbox);
    if (context->MailboxPhysical.QuadPart == 0) {
        MmFreeContiguousMemorySpecifyCache(
            context->Mailbox, HVR1_MAILBOX_BYTES, MmCached);
        RtlSecureZeroMemory(context, sizeof(*context));
        ExFreePool(context);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    context->CpuCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    if (context->CpuCount == 0 || context->CpuCount > HVR1_MAX_IPI_CPUS) {
        MmFreeContiguousMemorySpecifyCache(
            context->Mailbox, HVR1_MAILBOX_BYTES, MmCached);
        RtlSecureZeroMemory(context, sizeof(*context));
        ExFreePool(context);
        return STATUS_TOO_MANY_CONTEXT_IDS;
    }
    stackBytes = (SIZE_T)context->CpuCount * HVR1_PIVOT_STACK_BYTES;
    context->CpuStates = (HVR1_CPU_STATE *)ExAllocatePool2(
        POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED,
        (SIZE_T)context->CpuCount * sizeof(HVR1_CPU_STATE),
        HVR1_POOL_TAG);
    context->PivotStacks = (PUCHAR)ExAllocatePool2(
        POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED,
        stackBytes,
        HVR1_POOL_TAG);
    if (context->CpuStates == NULL || context->PivotStacks == NULL) {
        if (context->PivotStacks != NULL)
            ExFreePool(context->PivotStacks);
        if (context->CpuStates != NULL)
            ExFreePool(context->CpuStates);
        MmFreeContiguousMemorySpecifyCache(
            context->Mailbox, HVR1_MAILBOX_BYTES, MmCached);
        RtlSecureZeroMemory(context, sizeof(*context));
        ExFreePool(context);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(context->CpuStates,
                  (SIZE_T)context->CpuCount * sizeof(HVR1_CPU_STATE));
    RtlZeroMemory(context->PivotStacks, stackBytes);
    for (processor = 0; processor < context->CpuCount; ++processor) {
        UINT64 stackBase = (UINT64)(ULONG_PTR)(
            context->PivotStacks +
            ((SIZE_T)processor * HVR1_PIVOT_STACK_BYTES));
        context->CpuStates[processor].PivotStackBase = stackBase;
        context->CpuStates[processor].PivotStackTop =
            (stackBase + HVR1_PIVOT_STACK_BYTES) & ~(UINT64)0x0f;
    }

    *Companion = context;
    return STATUS_SUCCESS;
}

NTSTATUS
Hvr1CompanionDestroy(
    _Inout_ HVR1_COMPANION *Companion)
{
    if (Companion == NULL || !Hvr1CompanionCanUnload(Companion))
        return STATUS_DEVICE_BUSY;

    if (Companion->PivotStacks != NULL)
        ExFreePool(Companion->PivotStacks);
    if (Companion->CpuStates != NULL)
        ExFreePool(Companion->CpuStates);
    if (Companion->Mailbox != NULL)
        MmFreeContiguousMemorySpecifyCache(
            Companion->Mailbox, HVR1_MAILBOX_BYTES, MmCached);
    RtlSecureZeroMemory(Companion, sizeof(*Companion));
    ExFreePool(Companion);
    return STATUS_SUCCESS;
}

NTSTATUS
Hvr1CompanionGetMailboxDescriptor(
    _In_ HVR1_COMPANION *Companion,
    _Out_ HVR1_MAILBOX_DESCRIPTOR *Descriptor)
{
    UINT64 generation;

    if (Companion == NULL || Descriptor == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Descriptor, sizeof(*Descriptor));
    Descriptor->Size = sizeof(*Descriptor);
    Descriptor->Version = HVR1_PROTOCOL_VERSION;
    Descriptor->MailboxBytes = HVR1_MAILBOX_BYTES;
    Descriptor->MailboxHeaderBytes = HVR1_MAILBOX_HEADER_BYTES;
    generation = (UINT64)Companion->LastGeneration + 1;
    if (generation == 0)
        return STATUS_INTEGER_OVERFLOW;
    Descriptor->Generation = generation;
    RtlCopyMemory(Descriptor->SessionKey, Companion->AuthenticationKey,
                  HVR1_AUTH_KEY_BYTES);
    RtlCopyMemory(Descriptor->SessionNonce, Companion->AuthenticationNonce,
                  HVR1_AUTH_NONCE_BYTES);
    return STATUS_SUCCESS;
}

static NTSTATUS
Hvr1WriteStatus(
    _In_ const HVR1_COMPANION *Companion,
    _Out_ HVR1_IOCTL_STATUS *Status)
{
    if (Companion == NULL || Status == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Status, sizeof(*Status));
    Status->Size = sizeof(*Status);
    Status->Version = HVR1_PROTOCOL_VERSION;
    Status->State = (ULONG)Companion->State;
    Status->LastStatus = (ULONG)Companion->LastStatus;
    Status->Generation = (UINT64)Companion->LastGeneration;
    Status->ExpectedCpuCount = (ULONG)Companion->ExpectedCpuCount;
    Status->AcknowledgedCpuCount = (ULONG)Companion->AcknowledgedCpuCount;
    Status->CrashRecorded = (ULONG)Companion->Crash.Recorded;
    Status->CrashCode = Companion->Crash.BugCheckCode;
    Status->CrashCpu = Companion->Crash.Cpu;
    Status->CrashSequence = (UINT64)Companion->Crash.Sequence;
    return STATUS_SUCCESS;
}

NTSTATUS
Hvr1CompanionHandleIoctl(
    _Inout_ HVR1_COMPANION *Companion,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_opt_(InputBytes) const VOID *Input,
    _In_ ULONG InputBytes,
    _Out_writes_bytes_to_(OutputBytes, *ReturnedBytes) VOID *Output,
    _In_ ULONG OutputBytes,
    _Out_ ULONG *ReturnedBytes)
{
    NTSTATUS status;
    const HVR1_IOCTL_REQUEST *request;

    if (Companion == NULL || ReturnedBytes == NULL)
        return STATUS_INVALID_PARAMETER;
    *ReturnedBytes = 0;

    if (IoControlCode == HVR1_IOCTL_GET_MAILBOX) {
        if (Output == NULL || OutputBytes < sizeof(HVR1_MAILBOX_DESCRIPTOR))
            return STATUS_BUFFER_TOO_SMALL;
        status = Hvr1CompanionGetMailboxDescriptor(
            Companion, (HVR1_MAILBOX_DESCRIPTOR *)Output);
        if (NT_SUCCESS(status))
            *ReturnedBytes = sizeof(HVR1_MAILBOX_DESCRIPTOR);
        return status;
    }

    if (IoControlCode == HVR1_IOCTL_QUERY_STATUS) {
        if (Output == NULL || OutputBytes < sizeof(HVR1_IOCTL_STATUS))
            return STATUS_BUFFER_TOO_SMALL;
        status = Hvr1WriteStatus(Companion, (HVR1_IOCTL_STATUS *)Output);
        if (NT_SUCCESS(status))
            *ReturnedBytes = sizeof(HVR1_IOCTL_STATUS);
        return status;
    }

    if (IoControlCode != HVR1_IOCTL_SUBMIT ||
        Input == NULL || InputBytes < sizeof(HVR1_IOCTL_REQUEST) +
            HVR1_MAILBOX_HEADER_BYTES ||
        Output == NULL || OutputBytes < sizeof(HVR1_IOCTL_STATUS))
        return STATUS_INVALID_PARAMETER;

    request = (const HVR1_IOCTL_REQUEST *)Input;
    if (request->Size != sizeof(*request) ||
        request->Version != HVR1_PROTOCOL_VERSION ||
        request->Operation != HVR1_IOCTL_OPERATION_SHUTDOWN ||
        request->Flags != 0)
        return STATUS_INVALID_PARAMETER;

    ExAcquireFastMutex(&Companion->Lock);
    status = Hvr1SubmitShutdownLocked(
        Companion,
        request,
        (const UCHAR *)Input + sizeof(*request),
        InputBytes - sizeof(*request));
    (VOID)Hvr1WriteStatus(Companion, (HVR1_IOCTL_STATUS *)Output);
    *ReturnedBytes = sizeof(HVR1_IOCTL_STATUS);
    ExReleaseFastMutex(&Companion->Lock);
    return status;
}

NTSTATUS
Hvr1CompanionRegisterPowerCallback(
    _Inout_ HVR1_COMPANION *Companion,
    _In_ HVR1_POWER_PREPARE_CALLBACK Callback,
    _In_opt_ PVOID CallbackContext)
{
    if (Companion == NULL || Callback == NULL)
        return STATUS_INVALID_PARAMETER;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;

    ExAcquireFastMutex(&Companion->Lock);
    if (Companion->State == HVR1_COMPANION_STATE_OFFLINE ||
        Companion->State == HVR1_COMPANION_STATE_QUARANTINED ||
        Companion->PowerCallback != NULL) {
        ExReleaseFastMutex(&Companion->Lock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Companion->PowerCallback = Callback;
    Companion->PowerCallbackContext = CallbackContext;
    ExReleaseFastMutex(&Companion->Lock);
    return STATUS_SUCCESS;
}

NTSTATUS
Hvr1CompanionPrepareForPower(
    _Inout_ HVR1_COMPANION *Companion,
    _In_ HVR1_POWER_EVENT Event)
{
    HVR1_POWER_PREPARE_CALLBACK callback;
    PVOID callbackContext;
    NTSTATUS status;
    LONG state;

    if (Companion == NULL || Event < HVR1_POWER_EVENT_SUSPEND ||
        Event > HVR1_POWER_EVENT_SHUTDOWN)
        return STATUS_INVALID_PARAMETER;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;

    ExAcquireFastMutex(&Companion->Lock);
    state = Companion->State;
    if (state == HVR1_COMPANION_STATE_INITIALIZED ||
        state == HVR1_COMPANION_STATE_SESSION_READY) {
        callback = Companion->PowerCallback;
        callbackContext = Companion->PowerCallbackContext;
        ExReleaseFastMutex(&Companion->Lock);
        if (callback == NULL)
            return STATUS_DEVICE_BUSY;

        status = callback(callbackContext, Event);
        ExAcquireFastMutex(&Companion->Lock);
        if (!NT_SUCCESS(status) ||
            Companion->State != HVR1_COMPANION_STATE_OFFLINE) {
            InterlockedExchange(&Companion->State,
                                HVR1_COMPANION_STATE_QUARANTINED);
            InterlockedExchange(&Companion->LastStatus,
                                (LONG)(NT_SUCCESS(status)
                                    ? STATUS_DEVICE_BUSY : status));
            ExReleaseFastMutex(&Companion->Lock);
            return NT_SUCCESS(status) ? STATUS_DEVICE_BUSY : status;
        }
        ExReleaseFastMutex(&Companion->Lock);
        return STATUS_SUCCESS;
    }
    if (state == HVR1_COMPANION_STATE_OFFLINE) {
        ExReleaseFastMutex(&Companion->Lock);
        return STATUS_SUCCESS;
    }
    ExReleaseFastMutex(&Companion->Lock);
    return state == HVR1_COMPANION_STATE_QUARANTINED
        ? STATUS_ACCESS_DENIED
        : STATUS_DEVICE_BUSY;
}

VOID
Hvr1CompanionRecordCrashMarker(
    _Inout_ HVR1_COMPANION *Companion,
    _In_ ULONG BugCheckCode,
    _In_reads_(4) const ULONG_PTR Parameters[4])
{
    ULONG index;

    if (Companion == NULL || Parameters == NULL)
        return;
    if (InterlockedCompareExchange(&Companion->Crash.Recorded, 1, 0) != 0)
        return;

    Companion->Crash.BugCheckCode = BugCheckCode;
    Companion->Crash.Cpu = KeGetCurrentProcessorNumberEx(NULL);
    for (index = 0; index < 4; ++index)
        Companion->Crash.Parameters[index] = Parameters[index];
    InterlockedIncrement64(&Companion->Crash.Sequence);
    /* Deliberately no VMCALL/VMXOFF: bugcheck code must only leave evidence. */
}

BOOLEAN
Hvr1CompanionCanUnload(
    _In_ const HVR1_COMPANION *Companion)
{
    LONG state;

    if (Companion == NULL)
        return FALSE;
    state = Companion->State;
    return state == HVR1_COMPANION_STATE_INITIALIZED ||
        state == HVR1_COMPANION_STATE_OFFLINE;
}

HVR1_COMPANION_STATE
Hvr1CompanionGetState(
    _In_ const HVR1_COMPANION *Companion)
{
    if (Companion == NULL)
        return HVR1_COMPANION_STATE_QUARANTINED;
    return (HVR1_COMPANION_STATE)Companion->State;
}
