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
#include <aux_klib.h>

#include "efi_runtime_companion.h"
#include "hvr1_auth.h"
#include "runtime_analysis_abi.h"
#include "runtime_scanner.h"
#include "runtime_process_abi.h"
#include "runtime_shadow_abi.h"

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
    ULONG PayloadSize;
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
    KMUTEX Lock;

    /* The mailbox is contiguous and nonpaged because the EFI root consumes
     * its physical address on each processor during the rendezvous. */
    HVR1_MAILBOX *Mailbox;
    PHYSICAL_ADDRESS MailboxPhysical;

    /* Shadow-EPT commands rendezvous on every logical processor. Each root
     * receives a distinct mailbox so response publication cannot race. */
    HVR1_MAILBOX *ShadowMailboxes;
    PHYSICAL_ADDRESS ShadowMailboxesPhysical;
    SIZE_T ShadowMailboxesBytes;

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
    volatile LONG LastScanDiagnostic;
    volatile LONG ShadowRollback;
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
        (mailbox->Operation != HVR1_MAILBOX_OPERATION_RUNTIME_FRAME &&
         mailbox->Operation != HVR1_MAILBOX_OPERATION_RUNTIME_SCAN &&
         mailbox->Operation != HVR1_MAILBOX_OPERATION_PROCESS_LOCATE &&
         mailbox->Operation != HVR1_MAILBOX_OPERATION_SHADOW_EPT &&
         mailbox->Operation != HVR1_MAILBOX_OPERATION_ANALYSIS) ||
        mailbox->Flags != 0 ||
        mailbox->Status != 0 ||
        mailbox->Reserved != 0 ||
        mailbox->TotalSize < HVR1_MAILBOX_HEADER_BYTES ||
        mailbox->TotalSize > HVR1_MAILBOX_BYTES ||
        mailbox->TotalSize > InputBytes ||
        mailbox->PayloadSize > HVR1_MAILBOX_MAX_PAYLOAD_BYTES ||
        mailbox->TotalSize !=
            HVR1_MAILBOX_HEADER_BYTES + mailbox->PayloadSize ||
        mailbox->Generation == 0 ||
        mailbox->Sequence == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    if ((mailbox->Operation == HVR1_MAILBOX_OPERATION_RUNTIME_FRAME &&
         mailbox->PayloadSize != 0) ||
        (mailbox->Operation == HVR1_MAILBOX_OPERATION_RUNTIME_SCAN &&
         mailbox->PayloadSize != sizeof(HV_EFI_RUNTIME_SCAN_MAILBOX)) ||
        (mailbox->Operation == HVR1_MAILBOX_OPERATION_PROCESS_LOCATE &&
         mailbox->PayloadSize != sizeof(HV_EFI_RUNTIME_PROCESS_MAILBOX)) ||
        (mailbox->Operation == HVR1_MAILBOX_OPERATION_SHADOW_EPT &&
         mailbox->PayloadSize != sizeof(HV_EFI_RUNTIME_SHADOW_MAILBOX)) ||
        (mailbox->Operation == HVR1_MAILBOX_OPERATION_ANALYSIS &&
         mailbox->PayloadSize != sizeof(HV_EFI_ANALYSIS_MAILBOX))) {
        return STATUS_INVALID_PARAMETER;
    }

    View->TotalSize = mailbox->TotalSize;
    View->Operation = mailbox->Operation;
    View->PayloadSize = mailbox->PayloadSize;
    View->Generation = mailbox->Generation;
    View->Sequence = mailbox->Sequence;
    return STATUS_SUCCESS;
}

static NTSTATUS
Hvr1ValidateAnalysisPayload(
    _In_ const HVR1_MAILBOX *Mailbox,
    _In_ const HVR1_MAILBOX_VIEW *View)
{
    const HV_EFI_ANALYSIS_MAILBOX *analysis;
    const HV_EFI_ANALYSIS_REQUEST *request;
    ULONG index;
    if (Mailbox == NULL || View == NULL ||
        View->Operation != HVR1_MAILBOX_OPERATION_ANALYSIS ||
        View->PayloadSize != sizeof(HV_EFI_ANALYSIS_MAILBOX))
        return STATUS_INVALID_PARAMETER;
    analysis = (const HV_EFI_ANALYSIS_MAILBOX *)Mailbox->Payload;
    request = &analysis->Request;
    if (analysis->Magic != HV_EFI_ANALYSIS_MAGIC ||
        analysis->Version != HV_EFI_ANALYSIS_VERSION ||
        analysis->HeaderSize != offsetof(HV_EFI_ANALYSIS_MAILBOX, Request) ||
        analysis->State != HV_EFI_ANALYSIS_MAILBOX_REQUEST_READY ||
        analysis->Reserved != 0 ||
        analysis->Generation != View->Generation ||
        request->Size != sizeof(*request) ||
        request->Version != HV_EFI_ANALYSIS_VERSION ||
        request->Operation < HV_EFI_ANALYSIS_START ||
        request->Operation > HV_EFI_ANALYSIS_STOP ||
        request->Sequence != View->Sequence ||
        request->SignatureCount > HV_EFI_ANALYSIS_MAX_SIGNATURES ||
        request->RegionCount > HV_EFI_ANALYSIS_MAX_CONFIG_REGIONS ||
        request->ResultLimit > HV_EFI_ANALYSIS_MAX_RESULTS ||
         request->NodeLimit > HV_EFI_ANALYSIS_MAX_NODES ||
         request->MaxBytes > HV_EFI_ANALYSIS_MAX_SCAN_BYTES ||
         request->TimeoutTsc > HV_EFI_ANALYSIS_MAX_TIMEOUT_TSC ||
         request->Flags != 0)
        return STATUS_INVALID_PARAMETER;
    if (request->Operation == HV_EFI_ANALYSIS_START &&
        (request->ProcessId == 0 || request->ProcessId == 4 ||
         request->ProcessId > MAXULONG))
        return STATUS_INVALID_PARAMETER;
    for (index = 0; index < request->SignatureCount; ++index) {
        const HV_EFI_ANALYSIS_MEMORY_SIGNATURE *signature =
            &request->Signatures[index];
        if (signature->Length == 0 ||
            signature->Length > HV_EFI_ANALYSIS_PATTERN_BYTES ||
            signature->Reserved != 0 ||
            (signature->Flags & ~HV_EFI_ANALYSIS_SIGNATURE_FLAGS_ALL) != 0)
            return STATUS_INVALID_PARAMETER;
    }
    for (index = 0; index < request->RegionCount; ++index) {
        const HV_EFI_ANALYSIS_MEMORY_REGION *region =
            &request->Regions[index];
        if (region->BaseVirtualAddress < 0x10000 || region->Size == 0 ||
            region->BaseVirtualAddress > 0x00007fffffffffffULL ||
            region->Size - 1 >
                0x00007fffffffffffULL - region->BaseVirtualAddress ||
             region->RegionType > HV_EFI_ANALYSIS_REGION_PRIVATE ||
             (region->Protection &
              ~HV_EFI_ANALYSIS_PROTECTION_ALL) != 0 ||
             region->Reserved != 0)
            return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
Hvr1ValidateShadowPayload(
    _In_ const HVR1_MAILBOX *Mailbox,
    _In_ const HVR1_MAILBOX_VIEW *View)
{
    const HV_EFI_RUNTIME_SHADOW_MAILBOX *shadow;
    const HV_EFI_RUNTIME_SHADOW_REQUEST *request;
    ULONG index;

    if (Mailbox == NULL || View == NULL ||
        View->Operation != HVR1_MAILBOX_OPERATION_SHADOW_EPT ||
        View->PayloadSize != sizeof(HV_EFI_RUNTIME_SHADOW_MAILBOX))
        return STATUS_INVALID_PARAMETER;
    shadow = (const HV_EFI_RUNTIME_SHADOW_MAILBOX *)Mailbox->Payload;
    request = &shadow->Request;
    if (shadow->Magic != HV_EFI_RUNTIME_SHADOW_MAGIC ||
        shadow->Version != HV_EFI_RUNTIME_SHADOW_VERSION ||
        shadow->HeaderSize !=
            offsetof(HV_EFI_RUNTIME_SHADOW_MAILBOX, Request) ||
        shadow->State != HV_EFI_RUNTIME_SHADOW_MAILBOX_REQUEST_READY ||
        shadow->Reserved != 0 || shadow->Generation != View->Generation ||
        request->Size != sizeof(*request) ||
        request->Version != HV_EFI_RUNTIME_SHADOW_VERSION ||
        request->Sequence != View->Sequence || request->Flags != 0 ||
        request->Operation < HV_EFI_RUNTIME_SHADOW_BIND ||
        request->Operation > HV_EFI_RUNTIME_SHADOW_QUERY_STATUS ||
        request->RangeCount > HV_EFI_SHADOW_EPT_MAX_RANGES)
        return STATUS_INVALID_PARAMETER;
    if (request->Operation == HV_EFI_RUNTIME_SHADOW_BIND) {
        if (request->ProcessId == 0) return STATUS_INVALID_PARAMETER;
        for (index = 0; index < request->RangeCount; ++index) {
            const UINT64 base = request->Ranges[index].PhysicalBase;
            const UINT64 size = request->Ranges[index].Size;
            if (size == 0 || (base & 0xfff) != 0 || (size & 0xfff) != 0 ||
                base >= ((UINT64)1 << HV_EFI_EPT_GUEST_PHYSICAL_BITS) ||
                size > ((UINT64)1 << HV_EFI_EPT_GUEST_PHYSICAL_BITS) - base)
                return STATUS_INVALID_PARAMETER;
        }
    } else if (request->ProcessId != 0 || request->RangeCount != 0) {
        return STATUS_INVALID_PARAMETER;
    }
    for (index = request->RangeCount;
         index < HV_EFI_SHADOW_EPT_MAX_RANGES; ++index) {
        if (request->Ranges[index].PhysicalBase != 0 ||
            request->Ranges[index].Size != 0)
            return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
Hvr1ValidateProcessPayload(
    _In_ const HVR1_MAILBOX *Mailbox,
    _In_ const HVR1_MAILBOX_VIEW *View)
{
    const HV_EFI_RUNTIME_PROCESS_MAILBOX *process;
    const HV_EFI_RUNTIME_PROCESS_REQUEST *request;
    ULONG index;

    if (Mailbox == NULL || View == NULL ||
        View->Operation != HVR1_MAILBOX_OPERATION_PROCESS_LOCATE ||
        View->PayloadSize != sizeof(HV_EFI_RUNTIME_PROCESS_MAILBOX))
        return STATUS_INVALID_PARAMETER;
    process = (const HV_EFI_RUNTIME_PROCESS_MAILBOX *)Mailbox->Payload;
    request = &process->Request;
    if (process->Magic != HV_EFI_RUNTIME_PROCESS_MAGIC ||
        process->Version != HV_EFI_RUNTIME_PROCESS_VERSION ||
        process->HeaderSize !=
            offsetof(HV_EFI_RUNTIME_PROCESS_MAILBOX, Request) ||
        process->State != HV_EFI_RUNTIME_PROCESS_MAILBOX_REQUEST_READY ||
        process->Reserved != 0 || process->Generation != View->Generation ||
        request->Size != sizeof(*request) ||
        request->Version != HV_EFI_RUNTIME_PROCESS_VERSION ||
        request->NameLength == 0 || request->NameLength > 15 ||
        request->Sequence != View->Sequence || request->KernelBase != 0 ||
        request->BuildNumber != 0 || request->Ubr != 0 ||
        request->Flags != 0 || request->Reserved != 0)
        return STATUS_INVALID_PARAMETER;
    for (index = 0; index < request->NameLength; ++index) {
        const UCHAR value = (UCHAR)request->ImageFileName[index];
        if (value < 0x20 || value > 0x7e) return STATUS_INVALID_PARAMETER;
    }
    for (; index < HV_EFI_RUNTIME_PROCESS_NAME_BYTES; ++index) {
        if (request->ImageFileName[index] != '\0')
            return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
Hvr1ValidateScanPayload(
    _In_ const HVR1_MAILBOX *Mailbox,
    _In_ const HVR1_MAILBOX_VIEW *View)
{
    const HV_EFI_RUNTIME_SCAN_MAILBOX *scan;
    const HV_EFI_RUNTIME_SCAN_REQUEST *request;
    UINT64 validWildcardMask;

    if (Mailbox == NULL || View == NULL ||
        View->Operation != HVR1_MAILBOX_OPERATION_RUNTIME_SCAN ||
        View->PayloadSize != sizeof(HV_EFI_RUNTIME_SCAN_MAILBOX)) {
        return STATUS_INVALID_PARAMETER;
    }
    scan = (const HV_EFI_RUNTIME_SCAN_MAILBOX *)Mailbox->Payload;
    request = &scan->Request;
    if (scan->Magic != HV_EFI_RUNTIME_SCAN_MAGIC ||
        scan->Version != HV_EFI_RUNTIME_SCAN_VERSION ||
        scan->HeaderSize != offsetof(HV_EFI_RUNTIME_SCAN_MAILBOX, Request) ||
        scan->State != HV_EFI_RUNTIME_SCAN_MAILBOX_REQUEST_READY ||
        scan->Reserved != 0 || scan->Generation != View->Generation ||
        request->Size != sizeof(*request) ||
        request->Version != HV_EFI_RUNTIME_SCAN_VERSION ||
        request->PatternLength == 0 ||
        request->PatternLength > HV_EFI_RUNTIME_SCAN_MAX_PATTERN_BYTES ||
        request->MaxResults == 0 ||
        request->MaxResults > HV_EFI_RUNTIME_SCAN_MAX_RESULTS ||
        request->Sequence != View->Sequence ||
        request->MaxBytesToScan > HV_EFI_RUNTIME_SCAN_MAX_BUDGET_BYTES ||
        (request->CallbackType != HV_EFI_RUNTIME_SCAN_CALLBACK_FIRST_MATCH &&
         request->CallbackType != HV_EFI_RUNTIME_SCAN_CALLBACK_SINGLE &&
         request->CallbackType != HV_EFI_RUNTIME_SCAN_CALLBACK_ALL) ||
        (request->EndAddress != 0 &&
         request->StartAddress >= request->EndAddress)) {
        return STATUS_INVALID_PARAMETER;
    }
    validWildcardMask = request->PatternLength == 64
        ? ~(UINT64)0
        : (((UINT64)1 << request->PatternLength) - 1);
    if ((request->WildcardMask & ~validWildcardMask) != 0)
        return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}

static BOOLEAN
Hvr1AsciiNameEqual(
    _In_reads_bytes_(NameBytes) const UCHAR *Name,
    _In_ SIZE_T NameBytes,
    _In_z_ const char *Expected)
{
    SIZE_T index = 0;
    while (index < NameBytes && Name[index] != 0 && Expected[index] != 0) {
        UCHAR left = Name[index];
        UCHAR right = (UCHAR)Expected[index];
        if (left >= 'A' && left <= 'Z') left = (UCHAR)(left + ('a' - 'A'));
        if (right >= 'A' && right <= 'Z') right = (UCHAR)(right + ('a' - 'A'));
        if (left != right) return FALSE;
        ++index;
    }
    return index < NameBytes && Name[index] == 0 && Expected[index] == 0;
}

static BOOLEAN
Hvr1ValidateKernelPe(
    _In_ PVOID ImageBase)
{
    const IMAGE_DOS_HEADER *dos;
    const IMAGE_NT_HEADERS64 *nt;

    if (ImageBase == NULL ||
        (((ULONG_PTR)ImageBase) & (PAGE_SIZE - 1)) != 0)
        return FALSE;
    dos = (const IMAGE_DOS_HEADER *)ImageBase;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 ||
        dos->e_lfanew > 0x100000)
        return FALSE;
    nt = (const IMAGE_NT_HEADERS64 *)((const UCHAR *)ImageBase +
                                      dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE &&
        nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
        nt->OptionalHeader.SizeOfImage >= 0x100000 &&
        nt->OptionalHeader.SizeOfHeaders >= sizeof(IMAGE_DOS_HEADER) &&
        nt->OptionalHeader.SizeOfHeaders < nt->OptionalHeader.SizeOfImage;
}

static NTSTATUS
Hvr1ReadUbr(
    _Out_ ULONG *Ubr)
{
    static const WCHAR keyPathText[] =
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    static const WCHAR valueNameText[] = L"UBR";
    UNICODE_STRING keyPath;
    UNICODE_STRING valueName;
    OBJECT_ATTRIBUTES attributes;
    HANDLE key = NULL;
    ULONG returned = 0;
    union {
        ULONGLONG Alignment;
        UCHAR Bytes[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    } storage;
    PKEY_VALUE_PARTIAL_INFORMATION value;
    NTSTATUS status;

    if (Ubr == NULL) return STATUS_INVALID_PARAMETER;
    *Ubr = 0;
    RtlInitUnicodeString(&keyPath, keyPathText);
    InitializeObjectAttributes(&attributes, &keyPath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    status = ZwOpenKey(&key, KEY_QUERY_VALUE, &attributes);
    if (!NT_SUCCESS(status)) return status;
    RtlInitUnicodeString(&valueName, valueNameText);
    RtlZeroMemory(&storage, sizeof(storage));
    status = ZwQueryValueKey(key, &valueName, KeyValuePartialInformation,
                             &storage, sizeof(storage), &returned);
    ZwClose(key);
    value = (PKEY_VALUE_PARTIAL_INFORMATION)&storage;
    if (!NT_SUCCESS(status) || value->Type != REG_DWORD ||
        value->DataLength != sizeof(ULONG))
        return NT_SUCCESS(status) ? STATUS_OBJECT_TYPE_MISMATCH : status;
    RtlCopyMemory(Ubr, value->Data, sizeof(*Ubr));
    return STATUS_SUCCESS;
}

static NTSTATUS
Hvr1LocateKernelIdentity(
    _Out_ UINT64 *KernelBase,
    _Out_ ULONG *BuildNumber,
    _Out_ ULONG *Ubr)
{
    PAUX_MODULE_EXTENDED_INFO modules = NULL;
    RTL_OSVERSIONINFOW version;
    ULONG bytes = 0;
    ULONG allocatedBytes = 0;
    ULONG count;
    ULONG index;
    NTSTATUS status;

    if (KernelBase == NULL || BuildNumber == NULL || Ubr == NULL)
        return STATUS_INVALID_PARAMETER;
    *KernelBase = 0;
    *BuildNumber = 0;
    *Ubr = 0;
    status = AuxKlibInitialize();
    if (!NT_SUCCESS(status)) return status;
    status = AuxKlibQueryModuleInformation(
        &bytes, sizeof(AUX_MODULE_EXTENDED_INFO), NULL);
    if (!NT_SUCCESS(status) || bytes == 0 ||
        bytes % sizeof(AUX_MODULE_EXTENDED_INFO) != 0)
        return NT_SUCCESS(status) ? STATUS_DATA_ERROR : status;
    modules = (PAUX_MODULE_EXTENDED_INFO)ExAllocatePool2(
        POOL_FLAG_PAGED | POOL_FLAG_UNINITIALIZED, bytes, HVR1_POOL_TAG);
    if (modules == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    allocatedBytes = bytes;
    RtlZeroMemory(modules, bytes);
    status = AuxKlibQueryModuleInformation(
        &bytes, sizeof(AUX_MODULE_EXTENDED_INFO), modules);
    if (!NT_SUCCESS(status)) goto done;
    count = bytes / sizeof(AUX_MODULE_EXTENDED_INFO);
    for (index = 0; index < count; ++index) {
        const UCHAR *name;
        if (modules[index].FileNameOffset >=
            sizeof(modules[index].FullPathName))
            continue;
        name = modules[index].FullPathName + modules[index].FileNameOffset;
        if (!Hvr1AsciiNameEqual(
                name, sizeof(modules[index].FullPathName) -
                    modules[index].FileNameOffset, "ntoskrnl.exe") &&
            !Hvr1AsciiNameEqual(
                name, sizeof(modules[index].FullPathName) -
                    modules[index].FileNameOffset, "ntkrnlmp.exe"))
            continue;
        if (!Hvr1ValidateKernelPe(modules[index].BasicInfo.ImageBase)) {
            status = STATUS_INVALID_IMAGE_FORMAT;
            goto done;
        }
        *KernelBase = (UINT64)(ULONG_PTR)
            modules[index].BasicInfo.ImageBase;
        break;
    }
    if (*KernelBase == 0) {
        status = STATUS_NOT_FOUND;
        goto done;
    }
    RtlZeroMemory(&version, sizeof(version));
    version.dwOSVersionInfoSize = sizeof(version);
    status = RtlGetVersion(&version);
    if (!NT_SUCCESS(status) || version.dwBuildNumber == 0) goto done;
    *BuildNumber = version.dwBuildNumber;
    status = Hvr1ReadUbr(Ubr);

done:
    if (modules != NULL) {
        RtlSecureZeroMemory(modules, allocatedBytes);
        ExFreePool(modules);
    }
    if (!NT_SUCCESS(status)) {
        *KernelBase = 0;
        *BuildNumber = 0;
        *Ubr = 0;
    }
    return status;
}

static VOID
Hvr1AcquireLock(
    _Inout_ HVR1_COMPANION *Companion)
{
    (VOID)KeWaitForSingleObject(
        &Companion->Lock, Executive, KernelMode, FALSE, NULL);
}

static VOID
Hvr1ReleaseLock(
    _Inout_ HVR1_COMPANION *Companion)
{
    (VOID)KeReleaseMutex(&Companion->Lock, FALSE);
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
    if (view.Operation != HVR1_MAILBOX_OPERATION_RUNTIME_FRAME ||
        view.PayloadSize != 0)
        return STATUS_INVALID_PARAMETER;
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

static NTSTATUS
Hvr1SubmitScanLocked(
    _Inout_ HVR1_COMPANION *Companion,
    _In_ const HVR1_IOCTL_REQUEST *Request,
    _In_reads_bytes_(MailboxBytes) const VOID *MailboxInput,
    _In_ ULONG MailboxBytes,
    _Out_writes_bytes_to_(OutputBytes, *ReturnedBytes) VOID *Output,
    _In_ ULONG OutputBytes,
    _Out_ ULONG *ReturnedBytes)
{
    HVR1_MAILBOX_VIEW view;
    HVR1_RUNTIME_SESSION session;
    HVR1_SCAN_ARGUMENTS arguments;
    HV_EFI_RUNTIME_SCAN_MAILBOX *scan;
    GROUP_AFFINITY targetAffinity;
    GROUP_AFFINITY previousAffinity;
    PROCESSOR_NUMBER processorNumber;
    ULONG processorIndex;
    UINT64 scanStatus;
    LONG scanDiagnostic;
    NTSTATUS status;

    status = Hvr1ValidateMailbox(MailboxInput, MailboxBytes, &view);
    if (!NT_SUCCESS(status) ||
        view.Operation != HVR1_MAILBOX_OPERATION_RUNTIME_SCAN)
        return NT_SUCCESS(status) ? STATUS_INVALID_PARAMETER : status;
    status = Hvr1ValidateScanPayload(
        (const HVR1_MAILBOX *)MailboxInput, &view);
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
    if (view.Sequence <= (UINT64)Companion->LastSequence)
        return STATUS_INVALID_PARAMETER;
    if (Companion->State == HVR1_COMPANION_STATE_OFFLINE)
        return STATUS_DEVICE_NOT_READY;
    if (Companion->State == HVR1_COMPANION_STATE_SHUTDOWN_PREPARING ||
        Companion->State == HVR1_COMPANION_STATE_SHUTDOWN_COMMITTING)
        return STATUS_DEVICE_BUSY;
    if (Companion->State == HVR1_COMPANION_STATE_QUARANTINED)
        return STATUS_ACCESS_DENIED;
    if (Output == NULL || OutputBytes < view.TotalSize)
        return STATUS_BUFFER_TOO_SMALL;

    RtlZeroMemory(Companion->Mailbox, HVR1_MAILBOX_BYTES);
    RtlCopyMemory(Companion->Mailbox, MailboxInput, view.TotalSize);
    RtlZeroMemory(&session, sizeof(session));
    RtlZeroMemory(&arguments, sizeof(arguments));
    RtlZeroMemory(&targetAffinity, sizeof(targetAffinity));
    RtlZeroMemory(&previousAffinity, sizeof(previousAffinity));

    KeGetCurrentProcessorNumberEx(&processorNumber);
    processorIndex = KeGetProcessorIndexFromNumber(&processorNumber);
    if (processorIndex == INVALID_PROCESSOR_INDEX ||
        processorNumber.Number >= sizeof(KAFFINITY) * 8)
        return STATUS_INVALID_DEVICE_STATE;
    targetAffinity.Group = processorNumber.Group;
    targetAffinity.Mask = ((KAFFINITY)1) << processorNumber.Number;
    KeSetSystemGroupAffinityThread(&targetAffinity, &previousAffinity);

    (VOID)Hvr1Cpl0QuerySession(&session);
    if (session.Token[0] == 0 ||
        session.AbiVersion != HVR1_RUNTIME_ABI_VERSION ||
        (session.Capabilities & HVR1_RUNTIME_CAP_SCAN) == 0 ||
        session.Generation != view.Generation ||
        session.ProcessorIndex != processorIndex) {
        KeRevertToUserGroupAffinityThread(&previousAffinity);
        RtlSecureZeroMemory(&session, sizeof(session));
        return STATUS_ACCESS_DENIED;
    }

    RtlCopyMemory(arguments.Token, session.Token, sizeof(arguments.Token));
    arguments.MailboxPhysical =
        (UINT64)Companion->MailboxPhysical.QuadPart;
    arguments.MailboxBytes = HVR1_MAILBOX_BYTES;
    arguments.Generation = session.Generation;
    arguments.Sequence = view.Sequence;
    scanStatus = Hvr1Cpl0Scan(&arguments);
    KeRevertToUserGroupAffinityThread(&previousAffinity);
    RtlSecureZeroMemory(&arguments, sizeof(arguments));
    RtlSecureZeroMemory(&session, sizeof(session));

    scan = (HV_EFI_RUNTIME_SCAN_MAILBOX *)Companion->Mailbox->Payload;
    scanDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_NONE;
    if (scanStatus > HV_EFI_RUNTIME_SCAN_INVARIANT_VIOLATION)
        scanDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_STATUS;
    else if (scan->Magic != HV_EFI_RUNTIME_SCAN_MAGIC)
        scanDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_MAGIC;
    else if (scan->Version != HV_EFI_RUNTIME_SCAN_VERSION)
        scanDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_VERSION;
    else if (scan->HeaderSize !=
             offsetof(HV_EFI_RUNTIME_SCAN_MAILBOX, Request))
        scanDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_HEADER_SIZE;
    else if (scan->State != HV_EFI_RUNTIME_SCAN_MAILBOX_RESPONSE_READY)
        scanDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_STATE;
    else if (scan->Generation != view.Generation)
        scanDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_GENERATION;
    else if (scan->Request.Sequence != view.Sequence)
        scanDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_REQUEST_SEQUENCE;
    else if (scan->Response.Size != sizeof(scan->Response))
        scanDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_SIZE;
    else if (scan->Response.Version != HV_EFI_RUNTIME_SCAN_VERSION)
        scanDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_VERSION;
    else if (scan->Response.Status != (ULONG)scanStatus)
        scanDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_STATUS;
    else if (scan->Response.Generation != view.Generation)
        scanDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_GENERATION;
    else if (scan->Response.Sequence != view.Sequence)
        scanDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_SEQUENCE;
    else if (scan->Response.ResultCount > HV_EFI_RUNTIME_SCAN_MAX_RESULTS)
        scanDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESULT_COUNT;
    InterlockedExchange(
        &Companion->LastScanDiagnostic,
        scanDiagnostic |
            ((LONG)(scanStatus & UINT64_C(0xff)) << 8) |
            ((LONG)(scan->State & 0xff) << 16));
    if (scanDiagnostic != HVR1_SCAN_RESPONSE_DIAGNOSTIC_NONE) {
        InterlockedExchange(&Companion->LastStatus,
                            (LONG)STATUS_DATA_ERROR);
        return STATUS_DATA_ERROR;
    }

    Companion->Mailbox->Status = (ULONG)scanStatus;
    RtlZeroMemory(Companion->Mailbox->AuthTag, HVR1_MAILBOX_TAG_BYTES);
    if (!Hvr1ComputeMailboxAuthTag(
            Companion->AuthenticationKey,
            Companion->AuthenticationNonce,
            Companion->Mailbox,
            Companion->Mailbox->AuthTag)) {
        InterlockedExchange(&Companion->LastStatus,
                            (LONG)STATUS_UNSUCCESSFUL);
        return STATUS_UNSUCCESSFUL;
    }
    RtlCopyMemory(Output, Companion->Mailbox, view.TotalSize);
    *ReturnedBytes = view.TotalSize;
    InterlockedExchange64(&Companion->LastSequence, (LONG64)view.Sequence);
    InterlockedExchange(&Companion->LastStatus, (LONG)STATUS_SUCCESS);
    InterlockedExchange(&Companion->LastScanDiagnostic,
                        HVR1_SCAN_RESPONSE_DIAGNOSTIC_NONE);
    InterlockedExchange(&Companion->State,
                        HVR1_COMPANION_STATE_SESSION_READY);
    return STATUS_SUCCESS;
}

static NTSTATUS
Hvr1SubmitProcessLocked(
    _Inout_ HVR1_COMPANION *Companion,
    _In_ const HVR1_IOCTL_REQUEST *Request,
    _In_reads_bytes_(MailboxBytes) const VOID *MailboxInput,
    _In_ ULONG MailboxBytes,
    _Out_writes_bytes_to_(OutputBytes, *ReturnedBytes) VOID *Output,
    _In_ ULONG OutputBytes,
    _Out_ ULONG *ReturnedBytes)
{
    HVR1_MAILBOX_VIEW view;
    HVR1_RUNTIME_SESSION session;
    HVR1_PROCESS_ARGUMENTS arguments;
    HV_EFI_RUNTIME_PROCESS_MAILBOX *process;
    GROUP_AFFINITY targetAffinity;
    GROUP_AFFINITY previousAffinity;
    PROCESSOR_NUMBER processorNumber;
    ULONG processorIndex;
    UINT64 processStatus;
    UINT64 kernelBase;
    ULONG buildNumber;
    ULONG ubr;
    LONG processDiagnostic;
    NTSTATUS status;

    status = Hvr1ValidateMailbox(MailboxInput, MailboxBytes, &view);
    if (!NT_SUCCESS(status) ||
        view.Operation != HVR1_MAILBOX_OPERATION_PROCESS_LOCATE)
        return NT_SUCCESS(status) ? STATUS_INVALID_PARAMETER : status;
    status = Hvr1ValidateProcessPayload(
        (const HVR1_MAILBOX *)MailboxInput, &view);
    if (!NT_SUCCESS(status)) return status;
    if (!Hvr1VerifyMailboxAuthTag(
            Companion->AuthenticationKey,
            Companion->AuthenticationNonce,
            (const HVR1_MAILBOX *)MailboxInput))
        return STATUS_ACCESS_DENIED;
    if (Request->ExpectedGeneration != 0 &&
        Request->ExpectedGeneration != view.Generation)
        return STATUS_RETRY;
    if (view.Sequence <= (UINT64)Companion->LastSequence)
        return STATUS_INVALID_PARAMETER;
    if (Companion->State == HVR1_COMPANION_STATE_OFFLINE)
        return STATUS_DEVICE_NOT_READY;
    if (Companion->State == HVR1_COMPANION_STATE_SHUTDOWN_PREPARING ||
        Companion->State == HVR1_COMPANION_STATE_SHUTDOWN_COMMITTING)
        return STATUS_DEVICE_BUSY;
    if (Companion->State == HVR1_COMPANION_STATE_QUARANTINED)
        return STATUS_ACCESS_DENIED;
    if (Output == NULL || OutputBytes < view.TotalSize)
        return STATUS_BUFFER_TOO_SMALL;

    status = Hvr1LocateKernelIdentity(&kernelBase, &buildNumber, &ubr);
    if (!NT_SUCCESS(status)) return status;
    RtlZeroMemory(Companion->Mailbox, HVR1_MAILBOX_BYTES);
    RtlCopyMemory(Companion->Mailbox, MailboxInput, view.TotalSize);
    process = (HV_EFI_RUNTIME_PROCESS_MAILBOX *)Companion->Mailbox->Payload;
    process->Request.KernelBase = kernelBase;
    process->Request.BuildNumber = buildNumber;
    process->Request.Ubr = ubr;
    RtlZeroMemory(&session, sizeof(session));
    RtlZeroMemory(&arguments, sizeof(arguments));
    RtlZeroMemory(&targetAffinity, sizeof(targetAffinity));
    RtlZeroMemory(&previousAffinity, sizeof(previousAffinity));

    KeGetCurrentProcessorNumberEx(&processorNumber);
    processorIndex = KeGetProcessorIndexFromNumber(&processorNumber);
    if (processorIndex == INVALID_PROCESSOR_INDEX ||
        processorNumber.Number >= sizeof(KAFFINITY) * 8)
        return STATUS_INVALID_DEVICE_STATE;
    targetAffinity.Group = processorNumber.Group;
    targetAffinity.Mask = ((KAFFINITY)1) << processorNumber.Number;
    KeSetSystemGroupAffinityThread(&targetAffinity, &previousAffinity);

    (VOID)Hvr1Cpl0QuerySession(&session);
    if (session.Token[0] == 0 ||
        session.AbiVersion != HVR1_RUNTIME_ABI_VERSION ||
        (session.Capabilities & HVR1_RUNTIME_CAP_PROCESS) == 0 ||
        session.Generation != view.Generation ||
        session.ProcessorIndex != processorIndex) {
        KeRevertToUserGroupAffinityThread(&previousAffinity);
        RtlSecureZeroMemory(&session, sizeof(session));
        return STATUS_ACCESS_DENIED;
    }
    RtlCopyMemory(arguments.Token, session.Token, sizeof(arguments.Token));
    arguments.MailboxPhysical = (UINT64)Companion->MailboxPhysical.QuadPart;
    arguments.MailboxBytes = HVR1_MAILBOX_BYTES;
    arguments.Generation = session.Generation;
    arguments.Sequence = view.Sequence;
    processStatus = Hvr1Cpl0ProcessLocate(&arguments);
    KeRevertToUserGroupAffinityThread(&previousAffinity);
    RtlSecureZeroMemory(&arguments, sizeof(arguments));
    RtlSecureZeroMemory(&session, sizeof(session));

    process = (HV_EFI_RUNTIME_PROCESS_MAILBOX *)Companion->Mailbox->Payload;
    processDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_NONE;
    if (processStatus > HV_EFI_RUNTIME_PROCESS_INVARIANT_VIOLATION)
        processDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_STATUS;
    else if (process->Magic != HV_EFI_RUNTIME_PROCESS_MAGIC)
        processDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_MAGIC;
    else if (process->Version != HV_EFI_RUNTIME_PROCESS_VERSION)
        processDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_VERSION;
    else if (process->HeaderSize !=
             offsetof(HV_EFI_RUNTIME_PROCESS_MAILBOX, Request))
        processDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_HEADER_SIZE;
    else if (process->State != HV_EFI_RUNTIME_PROCESS_MAILBOX_RESPONSE_READY)
        processDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_STATE;
    else if (process->Generation != view.Generation)
        processDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_GENERATION;
    else if (process->Request.Sequence != view.Sequence ||
             process->Request.KernelBase != kernelBase ||
             process->Request.BuildNumber != buildNumber ||
             process->Request.Ubr != ubr)
        processDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_REQUEST_SEQUENCE;
    else if (process->Response.Size != sizeof(process->Response))
        processDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_SIZE;
    else if (process->Response.Version != HV_EFI_RUNTIME_PROCESS_VERSION)
        processDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_VERSION;
    else if (process->Response.Status != (ULONG)processStatus)
        processDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_STATUS;
    else if (process->Response.Generation != view.Generation)
        processDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_GENERATION;
    else if (process->Response.Sequence != view.Sequence)
        processDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_SEQUENCE;
    else if (process->Response.StoredCount >
             HV_EFI_RUNTIME_PROCESS_MAX_PRIVATE_RECORDS)
        processDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESULT_COUNT;
    InterlockedExchange(
        &Companion->LastScanDiagnostic,
        processDiagnostic |
            ((LONG)(processStatus & UINT64_C(0xff)) << 8) |
            ((LONG)(process->State & 0xff) << 16));
    if (processDiagnostic != HVR1_SCAN_RESPONSE_DIAGNOSTIC_NONE) {
        InterlockedExchange(&Companion->LastStatus,
                            (LONG)STATUS_DATA_ERROR);
        return STATUS_DATA_ERROR;
    }

    Companion->Mailbox->Status = (ULONG)processStatus;
    RtlZeroMemory(Companion->Mailbox->AuthTag, HVR1_MAILBOX_TAG_BYTES);
    if (!Hvr1ComputeMailboxAuthTag(
            Companion->AuthenticationKey,
            Companion->AuthenticationNonce,
            Companion->Mailbox,
            Companion->Mailbox->AuthTag)) {
        InterlockedExchange(&Companion->LastStatus,
                            (LONG)STATUS_UNSUCCESSFUL);
        return STATUS_UNSUCCESSFUL;
    }
    RtlCopyMemory(Output, Companion->Mailbox, view.TotalSize);
    *ReturnedBytes = view.TotalSize;
    InterlockedExchange64(&Companion->LastSequence, (LONG64)view.Sequence);
    InterlockedExchange(&Companion->LastStatus, (LONG)STATUS_SUCCESS);
    InterlockedExchange(&Companion->LastScanDiagnostic,
                        HVR1_SCAN_RESPONSE_DIAGNOSTIC_NONE);
    InterlockedExchange(&Companion->State,
                        HVR1_COMPANION_STATE_SESSION_READY);
    return STATUS_SUCCESS;
}

static NTSTATUS
Hvr1SubmitAnalysisLocked(
    _Inout_ HVR1_COMPANION *Companion,
    _In_ const HVR1_IOCTL_REQUEST *Request,
    _In_reads_bytes_(MailboxBytes) const VOID *MailboxInput,
    _In_ ULONG MailboxBytes,
    _Out_writes_bytes_to_(OutputBytes, *ReturnedBytes) VOID *Output,
    _In_ ULONG OutputBytes,
    _Out_ ULONG *ReturnedBytes)
{
    HVR1_MAILBOX_VIEW view;
    HVR1_RUNTIME_SESSION session;
    HVR1_ANALYSIS_ARGUMENTS arguments;
    HV_EFI_ANALYSIS_MAILBOX *analysis;
    GROUP_AFFINITY targetAffinity;
    GROUP_AFFINITY previousAffinity;
    PROCESSOR_NUMBER processorNumber;
    ULONG processorIndex;
    UINT64 rootStatus;
    LONG diagnostic;
    NTSTATUS status = Hvr1ValidateMailbox(
        MailboxInput, MailboxBytes, &view);
    if (!NT_SUCCESS(status) ||
        view.Operation != HVR1_MAILBOX_OPERATION_ANALYSIS)
        return NT_SUCCESS(status) ? STATUS_INVALID_PARAMETER : status;
    status = Hvr1ValidateAnalysisPayload(
        (const HVR1_MAILBOX *)MailboxInput, &view);
    if (!NT_SUCCESS(status)) return status;
    if (!Hvr1VerifyMailboxAuthTag(
            Companion->AuthenticationKey, Companion->AuthenticationNonce,
            (const HVR1_MAILBOX *)MailboxInput)) return STATUS_ACCESS_DENIED;
    if (Request->ExpectedGeneration != 0 &&
        Request->ExpectedGeneration != view.Generation) return STATUS_RETRY;
    if (view.Sequence <= (UINT64)Companion->LastSequence)
        return STATUS_INVALID_PARAMETER;
    if (Companion->State != HVR1_COMPANION_STATE_SESSION_READY)
        return STATUS_DEVICE_NOT_READY;
    if (Output == NULL || OutputBytes < view.TotalSize)
        return STATUS_BUFFER_TOO_SMALL;

    RtlZeroMemory(Companion->Mailbox, HVR1_MAILBOX_BYTES);
    RtlCopyMemory(Companion->Mailbox, MailboxInput, view.TotalSize);
    RtlZeroMemory(&session, sizeof(session));
    RtlZeroMemory(&arguments, sizeof(arguments));
    RtlZeroMemory(&targetAffinity, sizeof(targetAffinity));
    RtlZeroMemory(&previousAffinity, sizeof(previousAffinity));
    KeGetCurrentProcessorNumberEx(&processorNumber);
    processorIndex = KeGetProcessorIndexFromNumber(&processorNumber);
    if (processorIndex == INVALID_PROCESSOR_INDEX ||
        processorNumber.Number >= sizeof(KAFFINITY) * 8)
        return STATUS_INVALID_DEVICE_STATE;
    targetAffinity.Group = processorNumber.Group;
    targetAffinity.Mask = ((KAFFINITY)1) << processorNumber.Number;
    KeSetSystemGroupAffinityThread(&targetAffinity, &previousAffinity);
    (VOID)Hvr1Cpl0QuerySession(&session);
    if (session.Token[0] == 0 ||
        session.AbiVersion != HVR1_RUNTIME_ABI_VERSION ||
        (session.Capabilities & HVR1_RUNTIME_CAP_ANALYSIS) == 0 ||
        session.Generation != view.Generation ||
        session.ProcessorIndex != processorIndex) {
        KeRevertToUserGroupAffinityThread(&previousAffinity);
        RtlSecureZeroMemory(&session, sizeof(session));
        return STATUS_ACCESS_DENIED;
    }
    RtlCopyMemory(arguments.Token, session.Token, sizeof(arguments.Token));
    arguments.MailboxPhysical = (UINT64)Companion->MailboxPhysical.QuadPart;
    arguments.MailboxBytes = HVR1_MAILBOX_BYTES;
    arguments.Generation = session.Generation;
    arguments.Sequence = view.Sequence;
    rootStatus = Hvr1Cpl0AnalysisControl(&arguments);
    KeRevertToUserGroupAffinityThread(&previousAffinity);
    RtlSecureZeroMemory(&arguments, sizeof(arguments));
    RtlSecureZeroMemory(&session, sizeof(session));

    analysis = (HV_EFI_ANALYSIS_MAILBOX *)Companion->Mailbox->Payload;
    diagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_NONE;
    if (rootStatus > HV_EFI_ANALYSIS_MULTIPLE_MATCHES)
        diagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_STATUS;
    else if (analysis->Magic != HV_EFI_ANALYSIS_MAGIC)
        diagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_MAGIC;
    else if (analysis->Version != HV_EFI_ANALYSIS_VERSION)
        diagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_VERSION;
    else if (analysis->HeaderSize !=
             offsetof(HV_EFI_ANALYSIS_MAILBOX, Request))
        diagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_HEADER_SIZE;
    else if (analysis->State != HV_EFI_ANALYSIS_MAILBOX_RESPONSE_READY)
        diagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_STATE;
    else if (analysis->Generation != view.Generation)
        diagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_GENERATION;
    else if (analysis->Request.Sequence != view.Sequence)
        diagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_REQUEST_SEQUENCE;
    else if (analysis->Response.Size != sizeof(analysis->Response))
        diagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_SIZE;
    else if (analysis->Response.Version != HV_EFI_ANALYSIS_VERSION)
        diagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_VERSION;
    else if (analysis->Response.Status != (ULONG)rootStatus)
        diagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_STATUS;
    else if (analysis->Response.Generation != view.Generation)
        diagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_GENERATION;
    else if (analysis->Response.Sequence != view.Sequence)
        diagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_SEQUENCE;
    else if (analysis->Response.ReturnedResultCount >
                 HV_EFI_ANALYSIS_RESPONSE_RESULTS ||
             analysis->Response.ReturnedRegionCount >
                 HV_EFI_ANALYSIS_RESPONSE_REGIONS ||
             analysis->Response.ReturnedNodeCount >
                 HV_EFI_ANALYSIS_RESPONSE_NODES ||
             analysis->Response.ReturnedFieldCount >
                 HV_EFI_ANALYSIS_RESPONSE_FIELDS)
        diagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESULT_COUNT;
    InterlockedExchange(
        &Companion->LastScanDiagnostic,
        diagnostic | ((LONG)(rootStatus & 0xff) << 8) |
            ((LONG)(analysis->State & 0xff) << 16));
    if (diagnostic != HVR1_SCAN_RESPONSE_DIAGNOSTIC_NONE) {
        InterlockedExchange(&Companion->LastStatus, (LONG)STATUS_DATA_ERROR);
        return STATUS_DATA_ERROR;
    }
    Companion->Mailbox->Status = (ULONG)rootStatus;
    RtlZeroMemory(Companion->Mailbox->AuthTag, HVR1_MAILBOX_TAG_BYTES);
    if (!Hvr1ComputeMailboxAuthTag(
            Companion->AuthenticationKey, Companion->AuthenticationNonce,
            Companion->Mailbox, Companion->Mailbox->AuthTag))
        return STATUS_UNSUCCESSFUL;
    RtlCopyMemory(Output, Companion->Mailbox, view.TotalSize);
    *ReturnedBytes = view.TotalSize;
    InterlockedExchange64(&Companion->LastSequence, (LONG64)view.Sequence);
    InterlockedExchange(&Companion->LastStatus, (LONG)STATUS_SUCCESS);
    InterlockedExchange(&Companion->LastScanDiagnostic,
                        HVR1_SCAN_RESPONSE_DIAGNOSTIC_NONE);
    return STATUS_SUCCESS;
}

static ULONG_PTR
Hvr1ShadowOnCpu(
    _In_ ULONG_PTR ContextValue)
{
    HVR1_COMPANION *companion = (HVR1_COMPANION *)ContextValue;
    HVR1_CPU_STATE *cpu;
    HVR1_SHADOW_ARGUMENTS arguments;
    ULONG processorIndex;
    UINT64 status;

    if (!Hvr1ProcessorIndex(&processorIndex) ||
        processorIndex >= companion->CpuCount) {
        InterlockedIncrement(&companion->FailedCpuCount);
        return 0;
    }
    cpu = &companion->CpuStates[processorIndex];
    if (InterlockedCompareExchange(&cpu->Claimed, 1, 0) != 0) {
        Hvr1RecordCpuFailure(companion, cpu, STATUS_DEVICE_BUSY);
        return 0;
    }
    RtlZeroMemory(&cpu->Session, sizeof(cpu->Session));
    (VOID)Hvr1Cpl0QuerySession(&cpu->Session);
    if (cpu->Session.Token[0] == 0 ||
        cpu->Session.AbiVersion != HVR1_RUNTIME_ABI_VERSION ||
        (cpu->Session.Capabilities & HVR1_RUNTIME_CAP_SHADOW_EPT) == 0 ||
        cpu->Session.Generation != (UINT64)companion->RendezvousGeneration ||
        cpu->Session.ProcessorIndex != processorIndex) {
        Hvr1RecordCpuFailure(companion, cpu, STATUS_ACCESS_DENIED);
        return 0;
    }
    RtlZeroMemory(&arguments, sizeof(arguments));
    RtlCopyMemory(arguments.Token, cpu->Session.Token,
                  sizeof(arguments.Token));
    arguments.MailboxPhysical =
        (UINT64)companion->ShadowMailboxesPhysical.QuadPart +
        ((UINT64)processorIndex * HVR1_MAILBOX_BYTES);
    arguments.MailboxBytes = HVR1_MAILBOX_BYTES;
    arguments.Generation = cpu->Session.Generation;
    arguments.Sequence = companion->Mailbox->Sequence;
    status = Hvr1Cpl0ShadowControl(&arguments);
    cpu->Prepare.Status = status;
    if (status <= HV_EFI_RUNTIME_SHADOW_FAILED_SAFE) {
        InterlockedExchange(
            (volatile LONG *)&companion->Mailbox->AckByProcessor[processorIndex],
            HVR1_MAILBOX_ACK_OK);
        InterlockedIncrement(&companion->AcknowledgedCpuCount);
    } else {
        InterlockedExchange(
            (volatile LONG *)&companion->Mailbox->AckByProcessor[processorIndex],
            HVR1_MAILBOX_ACK_FAILURE);
        /* Preserve the root-mode transport diagnostic in Prepare.Status.
         * Hvr1RecordCpuFailure would replace it with the generic NTSTATUS. */
        InterlockedIncrement(&companion->FailedCpuCount);
        InterlockedCompareExchange(&companion->FirstCpuFailure,
                                   (LONG)STATUS_UNSUCCESSFUL, 0);
    }
    RtlSecureZeroMemory(&arguments, sizeof(arguments));
    return 0;
}

static VOID
Hvr1ResetShadowCpuState(
    _Inout_ HVR1_COMPANION *Companion)
{
    ULONG processor;
    for (processor = 0; processor < Companion->CpuCount; ++processor) {
        Companion->CpuStates[processor].Claimed = 0;
        RtlZeroMemory(&Companion->CpuStates[processor].Session,
                      sizeof(Companion->CpuStates[processor].Session));
        RtlZeroMemory(&Companion->CpuStates[processor].Prepare,
                      sizeof(Companion->CpuStates[processor].Prepare));
        Companion->Mailbox->AckByProcessor[processor] =
            HVR1_MAILBOX_ACK_NOT_STARTED;
    }
    InterlockedExchange(&Companion->AcknowledgedCpuCount, 0);
    InterlockedExchange(&Companion->FailedCpuCount, 0);
    InterlockedExchange(&Companion->FirstCpuFailure, 0);
}

static NTSTATUS
Hvr1SubmitShadowLocked(
    _Inout_ HVR1_COMPANION *Companion,
    _In_ const HVR1_IOCTL_REQUEST *Request,
    _In_reads_bytes_(MailboxBytes) const VOID *MailboxInput,
    _In_ ULONG MailboxBytes,
    _Out_writes_bytes_to_(OutputBytes, *ReturnedBytes) VOID *Output,
    _In_ ULONG OutputBytes,
    _Out_ ULONG *ReturnedBytes)
{
    HVR1_MAILBOX_VIEW view;
    HV_EFI_RUNTIME_SHADOW_MAILBOX *shadow;
    ULONG activeCpuCount;
    ULONG processor;
    UINT64 rootStatus;
    LONG shadowDiagnostic;
    NTSTATUS status;

    status = Hvr1ValidateMailbox(MailboxInput, MailboxBytes, &view);
    if (!NT_SUCCESS(status) ||
        view.Operation != HVR1_MAILBOX_OPERATION_SHADOW_EPT)
        return NT_SUCCESS(status) ? STATUS_INVALID_PARAMETER : status;
    status = Hvr1ValidateShadowPayload(
        (const HVR1_MAILBOX *)MailboxInput, &view);
    if (!NT_SUCCESS(status)) return status;
    if (!Hvr1VerifyMailboxAuthTag(
            Companion->AuthenticationKey, Companion->AuthenticationNonce,
            (const HVR1_MAILBOX *)MailboxInput)) return STATUS_ACCESS_DENIED;
    if (Request->ExpectedGeneration != 0 &&
        Request->ExpectedGeneration != view.Generation) return STATUS_RETRY;
    if (view.Sequence <= (UINT64)Companion->LastSequence)
        return STATUS_INVALID_PARAMETER;
    if (Companion->State != HVR1_COMPANION_STATE_SESSION_READY)
        return STATUS_DEVICE_NOT_READY;
    if (Output == NULL || OutputBytes < view.TotalSize)
        return STATUS_BUFFER_TOO_SMALL;

    InterlockedExchange(&Companion->LastScanDiagnostic,
                        HVR1_SCAN_RESPONSE_DIAGNOSTIC_NONE);

    activeCpuCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    if (activeCpuCount == 0 || activeCpuCount > Companion->CpuCount ||
        activeCpuCount > HVR1_MAX_IPI_CPUS)
        return STATUS_TOO_MANY_CONTEXT_IDS;
    RtlZeroMemory(Companion->Mailbox, HVR1_MAILBOX_BYTES);
    RtlCopyMemory(Companion->Mailbox, MailboxInput, view.TotalSize);
    for (processor = 0; processor < activeCpuCount; ++processor) {
        HVR1_MAILBOX *cpuMailbox = (HVR1_MAILBOX *)(
            (PUCHAR)Companion->ShadowMailboxes +
            ((SIZE_T)processor * HVR1_MAILBOX_BYTES));
        RtlZeroMemory(cpuMailbox, HVR1_MAILBOX_BYTES);
        RtlCopyMemory(cpuMailbox, MailboxInput, view.TotalSize);
    }
    /* Publish every physically distinct command frame before any target CPU
     * is allowed to enter root and consume it. */
    KeMemoryBarrier();
    InterlockedExchange(&Companion->ExpectedCpuCount, (LONG)activeCpuCount);
    InterlockedExchange64(&Companion->RendezvousGeneration,
                          (LONG64)view.Generation);
    Hvr1ResetShadowCpuState(Companion);
    (VOID)KeIpiGenericCall(Hvr1ShadowOnCpu, (ULONG_PTR)Companion);
    KeMemoryBarrier();

    rootStatus = Companion->CpuStates[0].Prepare.Status;
    for (processor = 1; processor < activeCpuCount; ++processor) {
        if (Companion->CpuStates[processor].Prepare.Status != rootStatus) {
            InterlockedExchange(
                &Companion->LastScanDiagnostic,
                HVR1_SCAN_RESPONSE_DIAGNOSTIC_SHADOW_CPU_STATUS |
                    ((LONG)(rootStatus & UINT64_C(0xff)) << 8) |
                    ((LONG)(Companion->CpuStates[processor].Prepare.Status &
                            UINT64_C(0xff)) << 16));
            InterlockedIncrement(&Companion->FailedCpuCount);
            break;
        }
    }

    if (Companion->FailedCpuCount != 0 ||
        Companion->AcknowledgedCpuCount != (LONG)activeCpuCount) {
        if (Companion->LastScanDiagnostic ==
                HVR1_SCAN_RESPONSE_DIAGNOSTIC_NONE) {
            InterlockedExchange(
                &Companion->LastScanDiagnostic,
                HVR1_SCAN_RESPONSE_DIAGNOSTIC_SHADOW_RENDEZVOUS |
                    ((LONG)(rootStatus & UINT64_C(0xff)) << 8) |
                    ((LONG)(Companion->AcknowledgedCpuCount & 0xff) << 16));
        }
        /* A partial bind is never retained. Convert the trusted in-kernel
         * copy to UNBIND and rendezvous again so every CPU returns to the
         * identity EPT before reporting failure. */
        for (processor = 0; processor < activeCpuCount; ++processor) {
            HVR1_MAILBOX *cpuMailbox = (HVR1_MAILBOX *)(
                (PUCHAR)Companion->ShadowMailboxes +
                ((SIZE_T)processor * HVR1_MAILBOX_BYTES));
            shadow = (HV_EFI_RUNTIME_SHADOW_MAILBOX *)cpuMailbox->Payload;
            shadow->State = HV_EFI_RUNTIME_SHADOW_MAILBOX_REQUEST_READY;
            shadow->Request.Operation = HV_EFI_RUNTIME_SHADOW_UNBIND;
            shadow->Request.ProcessId = 0;
            shadow->Request.RangeCount = 0;
            RtlZeroMemory(shadow->Request.Ranges,
                          sizeof(shadow->Request.Ranges));
        }
        Hvr1ResetShadowCpuState(Companion);
        (VOID)KeIpiGenericCall(Hvr1ShadowOnCpu, (ULONG_PTR)Companion);
        InterlockedExchange(&Companion->LastStatus,
                            (LONG)STATUS_UNSUCCESSFUL);
        InterlockedExchange64(&Companion->RendezvousGeneration, 0);
        return STATUS_UNSUCCESSFUL;
    }

    RtlCopyMemory(Companion->Mailbox, Companion->ShadowMailboxes,
                  view.TotalSize);
    shadow = (HV_EFI_RUNTIME_SHADOW_MAILBOX *)Companion->Mailbox->Payload;
    shadowDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_NONE;
    if (shadow->Magic != HV_EFI_RUNTIME_SHADOW_MAGIC)
        shadowDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_MAGIC;
    else if (shadow->Version != HV_EFI_RUNTIME_SHADOW_VERSION)
        shadowDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_VERSION;
    else if (shadow->HeaderSize !=
             offsetof(HV_EFI_RUNTIME_SHADOW_MAILBOX, Request))
        shadowDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_HEADER_SIZE;
    else if (shadow->State != HV_EFI_RUNTIME_SHADOW_MAILBOX_RESPONSE_READY)
        shadowDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_STATE;
    else if (shadow->Generation != view.Generation)
        shadowDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_GENERATION;
    else if (shadow->Request.Sequence != view.Sequence)
        shadowDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_REQUEST_SEQUENCE;
    else if (shadow->Response.Size != sizeof(shadow->Response))
        shadowDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_SIZE;
    else if (shadow->Response.Version != HV_EFI_RUNTIME_SHADOW_VERSION)
        shadowDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_VERSION;
    else if (shadow->Response.Status > HV_EFI_RUNTIME_SHADOW_FAILED_SAFE ||
             shadow->Response.Status != (ULONG)rootStatus)
        shadowDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_STATUS;
    else if (shadow->Response.Generation != view.Generation)
        shadowDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_GENERATION;
    else if (shadow->Response.Sequence != view.Sequence)
        shadowDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_SEQUENCE;
    else if (shadow->Response.RangeCount > HV_EFI_SHADOW_EPT_MAX_RANGES)
        shadowDiagnostic = HVR1_SCAN_RESPONSE_DIAGNOSTIC_SHADOW_RANGE_COUNT;
    InterlockedExchange(
        &Companion->LastScanDiagnostic,
        shadowDiagnostic |
            ((LONG)(rootStatus & UINT64_C(0xff)) << 8) |
            ((LONG)(shadow->State & 0xff) << 16));
    if (shadowDiagnostic != HVR1_SCAN_RESPONSE_DIAGNOSTIC_NONE) {
        InterlockedExchange(&Companion->LastStatus, (LONG)STATUS_DATA_ERROR);
        InterlockedExchange64(&Companion->RendezvousGeneration, 0);
        return STATUS_DATA_ERROR;
    }
    Companion->Mailbox->Status = shadow->Response.Status;
    RtlZeroMemory(Companion->Mailbox->AuthTag, HVR1_MAILBOX_TAG_BYTES);
    if (!Hvr1ComputeMailboxAuthTag(
            Companion->AuthenticationKey, Companion->AuthenticationNonce,
            Companion->Mailbox, Companion->Mailbox->AuthTag))
        return STATUS_UNSUCCESSFUL;
    RtlCopyMemory(Output, Companion->Mailbox, view.TotalSize);
    *ReturnedBytes = view.TotalSize;
    InterlockedExchange64(&Companion->LastSequence, (LONG64)view.Sequence);
    Hvr1ResetShadowCpuState(Companion);
    InterlockedExchange(&Companion->ExpectedCpuCount, 0);
    InterlockedExchange64(&Companion->RendezvousGeneration, 0);
    InterlockedExchange(&Companion->LastStatus, (LONG)STATUS_SUCCESS);
    InterlockedExchange(&Companion->LastScanDiagnostic,
                        HVR1_SCAN_RESPONSE_DIAGNOSTIC_NONE);
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
    SIZE_T shadowMailboxBytes;
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
    KeInitializeMutex(&context->Lock, 0);
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
    shadowMailboxBytes = (SIZE_T)context->CpuCount * HVR1_MAILBOX_BYTES;
    context->ShadowMailboxes =
        (HVR1_MAILBOX *)MmAllocateContiguousMemorySpecifyCache(
            shadowMailboxBytes, lowAddress, highAddress, boundaryAddress,
            MmCached);
    if (context->ShadowMailboxes == NULL) {
        MmFreeContiguousMemorySpecifyCache(
            context->Mailbox, HVR1_MAILBOX_BYTES, MmCached);
        RtlSecureZeroMemory(context, sizeof(*context));
        ExFreePool(context);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    context->ShadowMailboxesBytes = shadowMailboxBytes;
    RtlZeroMemory(context->ShadowMailboxes, shadowMailboxBytes);
    context->ShadowMailboxesPhysical =
        MmGetPhysicalAddress(context->ShadowMailboxes);
    if (context->ShadowMailboxesPhysical.QuadPart == 0) {
        MmFreeContiguousMemorySpecifyCache(
            context->ShadowMailboxes, shadowMailboxBytes, MmCached);
        MmFreeContiguousMemorySpecifyCache(
            context->Mailbox, HVR1_MAILBOX_BYTES, MmCached);
        RtlSecureZeroMemory(context, sizeof(*context));
        ExFreePool(context);
        return STATUS_INSUFFICIENT_RESOURCES;
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
            context->ShadowMailboxes, context->ShadowMailboxesBytes,
            MmCached);
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
    if (Companion->ShadowMailboxes != NULL)
        MmFreeContiguousMemorySpecifyCache(
            Companion->ShadowMailboxes, Companion->ShadowMailboxesBytes,
            MmCached);
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

    Hvr1AcquireLock(Companion);
    RtlZeroMemory(Descriptor, sizeof(*Descriptor));
    Descriptor->Size = sizeof(*Descriptor);
    Descriptor->Version = HVR1_PROTOCOL_VERSION;
    Descriptor->MailboxBytes = HVR1_MAILBOX_BYTES;
    Descriptor->MailboxHeaderBytes = HVR1_MAILBOX_HEADER_BYTES;
    generation = (UINT64)Companion->LastGeneration + 1;
    if (generation == 0) {
        Hvr1ReleaseLock(Companion);
        return STATUS_INTEGER_OVERFLOW;
    }
    Descriptor->Generation = generation;
    RtlCopyMemory(Descriptor->SessionKey, Companion->AuthenticationKey,
                  HVR1_AUTH_KEY_BYTES);
    RtlCopyMemory(Descriptor->SessionNonce, Companion->AuthenticationNonce,
                  HVR1_AUTH_NONCE_BYTES);
    Descriptor->NextSequence = (UINT64)Companion->LastSequence + 1;
    if (Descriptor->NextSequence == 0) {
        RtlSecureZeroMemory(Descriptor, sizeof(*Descriptor));
        Hvr1ReleaseLock(Companion);
        return STATUS_INTEGER_OVERFLOW;
    }
    Hvr1ReleaseLock(Companion);
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
    Status->Reserved = (ULONG)Companion->LastScanDiagnostic;
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
        Output == NULL)
        return STATUS_INVALID_PARAMETER;

    request = (const HVR1_IOCTL_REQUEST *)Input;
    if (request->Size != sizeof(*request) ||
        request->Version != HVR1_PROTOCOL_VERSION ||
        (request->Operation != HVR1_IOCTL_OPERATION_SHUTDOWN &&
         request->Operation != HVR1_IOCTL_OPERATION_SCAN &&
         request->Operation != HVR1_IOCTL_OPERATION_PROCESS_LOCATE &&
         request->Operation != HVR1_IOCTL_OPERATION_SHADOW_EPT &&
         request->Operation != HVR1_IOCTL_OPERATION_ANALYSIS) ||
        request->Flags != 0)
        return STATUS_INVALID_PARAMETER;

    Hvr1AcquireLock(Companion);
    if (request->Operation == HVR1_IOCTL_OPERATION_SHUTDOWN) {
        if (OutputBytes < sizeof(HVR1_IOCTL_STATUS)) {
            status = STATUS_BUFFER_TOO_SMALL;
        } else {
            status = Hvr1SubmitShutdownLocked(
                Companion,
                request,
                (const UCHAR *)Input + sizeof(*request),
                InputBytes - sizeof(*request));
            (VOID)Hvr1WriteStatus(
                Companion, (HVR1_IOCTL_STATUS *)Output);
            *ReturnedBytes = sizeof(HVR1_IOCTL_STATUS);
        }
    } else if (request->Operation == HVR1_IOCTL_OPERATION_SCAN) {
        status = Hvr1SubmitScanLocked(
            Companion,
            request,
            (const UCHAR *)Input + sizeof(*request),
            InputBytes - sizeof(*request),
            Output,
            OutputBytes,
            ReturnedBytes);
    } else if (request->Operation == HVR1_IOCTL_OPERATION_PROCESS_LOCATE) {
        status = Hvr1SubmitProcessLocked(
            Companion,
            request,
            (const UCHAR *)Input + sizeof(*request),
            InputBytes - sizeof(*request),
            Output,
            OutputBytes,
            ReturnedBytes);
    } else if (request->Operation == HVR1_IOCTL_OPERATION_SHADOW_EPT) {
        status = Hvr1SubmitShadowLocked(
            Companion,
            request,
            (const UCHAR *)Input + sizeof(*request),
            InputBytes - sizeof(*request),
            Output,
            OutputBytes,
            ReturnedBytes);
    } else {
        status = Hvr1SubmitAnalysisLocked(
            Companion,
            request,
            (const UCHAR *)Input + sizeof(*request),
            InputBytes - sizeof(*request),
            Output,
            OutputBytes,
            ReturnedBytes);
    }
    Hvr1ReleaseLock(Companion);
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

    Hvr1AcquireLock(Companion);
    if (Companion->State == HVR1_COMPANION_STATE_OFFLINE ||
        Companion->State == HVR1_COMPANION_STATE_QUARANTINED ||
        Companion->PowerCallback != NULL) {
        Hvr1ReleaseLock(Companion);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Companion->PowerCallback = Callback;
    Companion->PowerCallbackContext = CallbackContext;
    Hvr1ReleaseLock(Companion);
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

    Hvr1AcquireLock(Companion);
    state = Companion->State;
    if (state == HVR1_COMPANION_STATE_INITIALIZED ||
        state == HVR1_COMPANION_STATE_SESSION_READY) {
        callback = Companion->PowerCallback;
        callbackContext = Companion->PowerCallbackContext;
        Hvr1ReleaseLock(Companion);
        if (callback == NULL)
            return STATUS_DEVICE_BUSY;

        status = callback(callbackContext, Event);
        Hvr1AcquireLock(Companion);
        if (!NT_SUCCESS(status) ||
            Companion->State != HVR1_COMPANION_STATE_OFFLINE) {
            InterlockedExchange(&Companion->State,
                                HVR1_COMPANION_STATE_QUARANTINED);
            InterlockedExchange(&Companion->LastStatus,
                                (LONG)(NT_SUCCESS(status)
                                    ? STATUS_DEVICE_BUSY : status));
            Hvr1ReleaseLock(Companion);
            return NT_SUCCESS(status) ? STATUS_DEVICE_BUSY : status;
        }
        Hvr1ReleaseLock(Companion);
        return STATUS_SUCCESS;
    }
    if (state == HVR1_COMPANION_STATE_OFFLINE) {
        Hvr1ReleaseLock(Companion);
        return STATUS_SUCCESS;
    }
    Hvr1ReleaseLock(Companion);
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
