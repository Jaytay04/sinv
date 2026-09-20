#include "runtime_shadow.h"

#include "../driver/efi_runtime_companion/hvr1_protocol.h"
#include "efi_hypervisor.h"
#include "runtime_process.h"
#include "scanner.h"

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_ReadWriteBarrier)
#pragma code_seg(push, ".hvscan$m")
#endif

namespace {

constexpr uint64_t kWritable = UINT64_C(2);
constexpr uint64_t kPageMask = UINT64_C(4095);
constexpr uint64_t kWindowInitDiagnosticBase = UINT64_C(32);

void Zero(void *memory, size_t bytes)
{
    volatile uint8_t *value = static_cast<volatile uint8_t *>(memory);
    while (bytes-- != 0) *value++ = 0;
}

void Copy(void *destination, const void *source, size_t bytes)
{
    volatile uint8_t *to = static_cast<volatile uint8_t *>(destination);
    const volatile uint8_t *from = static_cast<const volatile uint8_t *>(source);
    while (bytes-- != 0) *to++ = *from++;
}

void Barrier()
{
#if defined(_MSC_VER)
    _ReadWriteBarrier();
    __faststorefence();
#else
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}

bool RangeValid(uint64_t base, uint64_t size)
{
    const uint64_t limit = UINT64_C(1) << HV_EFI_EPT_GUEST_PHYSICAL_BITS;
    return size != 0 && (base & kPageMask) == 0 &&
        (size & kPageMask) == 0 && base < limit && size <= limit - base;
}

bool Intersects(uint64_t leftBase, uint64_t leftSize,
                uint64_t rightBase, uint64_t rightSize)
{
    if (leftSize == 0 || rightSize == 0 ||
        leftBase > UINT64_MAX - leftSize ||
        rightBase > UINT64_MAX - rightSize) return false;
    return leftBase < rightBase + rightSize &&
        rightBase < leftBase + leftSize;
}

bool Allowed(const HV_EFI_SCANNER_RAM_RANGE *ranges, uint32_t count,
             uint64_t base, uint64_t size)
{
    for (uint32_t index = 0; index < count; ++index) {
        if (ranges[index].Size != 0 && base >= ranges[index].PhysicalBase &&
            size <= ranges[index].Size - (base - ranges[index].PhysicalBase))
            return true;
    }
    return false;
}

bool IntersectsAllocation(uint64_t base, uint64_t size,
                          uint64_t allocation, uint32_t pages)
{
    return allocation != 0 && pages != 0 &&
        Intersects(base, size, allocation,
                   static_cast<uint64_t>(pages) * HV_EFI_PAGE_SIZE);
}

bool IntersectsPrivate(const HV_EFI_CONTEXT *context,
                       uint64_t base, uint64_t size)
{
    if (IntersectsAllocation(base, size, context->ResidentCodeBase,
                             HV_EFI_RESIDENT_CODE_PAGES) ||
        IntersectsAllocation(base, size, context->RuntimeScannerCodeBase,
                             context->RuntimeScannerCodePages) ||
        IntersectsAllocation(base, size,
                             context->RuntimeScannerStatePhysical,
                             HV_EFI_RUNTIME_SCANNER_STATE_PAGES) ||
        IntersectsAllocation(base, size,
                             context->RuntimeScannerRamRangesPhysical,
                             context->RuntimeScannerRamRangePages) ||
        IntersectsAllocation(base, size, context->ProcessorRecordsPhysical,
                             context->ProcessorRecordPages) ||
        IntersectsAllocation(base, size, context->PhysicalIommuStatePhysical,
                             context->PhysicalIommuStatePages) ||
        IntersectsAllocation(base, size, context->PhysicalIommuTablePhysical,
                             context->PhysicalIommuTablePages) ||
        IntersectsAllocation(base, size, context->AcpiShadowPhysical, 2))
        return true;
    if (context->ProcessorRecordsPhysical == 0 ||
        context->ProcessorRecordCount == 0 ||
        context->ProcessorRecordCount > HV_EFI_MAX_SUPPORTED_PROCESSORS)
        return true;
    const auto *records = reinterpret_cast<const HV_EFI_PROCESSOR_RECORD *>(
        static_cast<uintptr_t>(context->ProcessorRecordsPhysical));
    for (uint32_t index = 0; index < context->ProcessorRecordCount; ++index) {
        const HV_EFI_PROCESSOR_RECORD *record = &records[index];
        if ((record->StateFlags &
             HV_EFI_PROCESSOR_STATE_RESOURCES_BOUND) == 0) continue;
        if (IntersectsAllocation(base, size, record->ContextPhysical,
                                 HV_EFI_CONTEXT_PAGES) ||
            IntersectsAllocation(base, size, record->VmxonPhysical,
                                 HV_EFI_VMXON_PAGES) ||
            IntersectsAllocation(base, size, record->VmcsPhysical,
                                 HV_EFI_VMCS_PAGES) ||
            IntersectsAllocation(base, size, record->HostStackPhysical,
                                 HV_EFI_HOST_STACK_PAGES) ||
            IntersectsAllocation(base, size, record->GuestStackPhysical,
                                 HV_EFI_GUEST_STACK_PAGES) ||
            IntersectsAllocation(base, size, record->PageTablePhysical,
                                 HV_EFI_PAGE_TABLE_PAGES) ||
            IntersectsAllocation(base, size,
                                 record->DescriptorStoragePhysical,
                                 HV_EFI_DESCRIPTOR_STORAGE_PAGES) ||
            IntersectsAllocation(base, size, record->EptPhysical,
                                 HV_EFI_EPT_TABLE_PAGES) ||
            IntersectsAllocation(base, size, record->ShadowEptPhysical,
                                 HV_EFI_EPT_TABLE_PAGES) ||
            IntersectsAllocation(base, size,
                                 record->ShadowEptStatePhysical,
                                 HV_EFI_SHADOW_EPT_STATE_PAGES)) return true;
    }
    return false;
}

bool RequestValid(const HV_EFI_RUNTIME_SHADOW_REQUEST *request)
{
    if (request == nullptr || request->Size != sizeof(*request) ||
        request->Version != HV_EFI_RUNTIME_SHADOW_VERSION ||
        request->Sequence == 0 || request->Flags != 0 ||
        request->Operation < HV_EFI_RUNTIME_SHADOW_BIND ||
        request->Operation > HV_EFI_RUNTIME_SHADOW_QUERY_STATUS ||
        request->RangeCount > HV_EFI_SHADOW_EPT_MAX_RANGES) return false;
    if (request->Operation == HV_EFI_RUNTIME_SHADOW_BIND) {
        if (request->ProcessId == 0) return false;
        for (uint32_t index = 0; index < request->RangeCount; ++index) {
            if (!RangeValid(request->Ranges[index].PhysicalBase,
                            request->Ranges[index].Size)) return false;
        }
    } else if (request->ProcessId != 0 || request->RangeCount != 0) {
        return false;
    }
    for (uint32_t index = request->RangeCount;
         index < HV_EFI_SHADOW_EPT_MAX_RANGES; ++index) {
        if (request->Ranges[index].PhysicalBase != 0 ||
            request->Ranges[index].Size != 0) return false;
    }
    return true;
}

const HV_EFI_GUEST_PROCESS_RECORD *FindProcess(
    const HV_EFI_CONTEXT *context, uint64_t pid)
{
    if (context->RuntimeScannerStatePhysical == 0) return nullptr;
    const auto *state = reinterpret_cast<const HV_EFI_RUNTIME_PROCESS_STATE *>(
        static_cast<uintptr_t>(context->RuntimeScannerStatePhysical +
            HV_EFI_RUNTIME_PROCESS_STATE_PAGE_OFFSET));
    if (state->Marker != HV_EFI_RUNTIME_PROCESS_STATE_MARKER ||
        state->StoredCount > HV_EFI_RUNTIME_PROCESS_MAX_PRIVATE_RECORDS)
        return nullptr;
    for (uint32_t index = 0; index < state->StoredCount; ++index) {
        if (state->Records[index].ProcessId == pid)
            return &state->Records[index];
    }
    return nullptr;
}

HV_EFI_RUNTIME_SHADOW_STATUS MapStatus(HV_EFI_SHADOW_EPT_STATUS status)
{
    switch (status) {
    case HV_EFI_SHADOW_EPT_OK:
        return HV_EFI_RUNTIME_SHADOW_SUCCESS;
    case HV_EFI_SHADOW_EPT_BUSY:
        return HV_EFI_RUNTIME_SHADOW_BUSY;
    case HV_EFI_SHADOW_EPT_CAPACITY:
        return HV_EFI_RUNTIME_SHADOW_CAPACITY;
    case HV_EFI_SHADOW_EPT_PHYSICAL_RANGE:
        return HV_EFI_RUNTIME_SHADOW_PHYSICAL_RANGE;
    case HV_EFI_SHADOW_EPT_PROTECTED_RANGE:
        return HV_EFI_RUNTIME_SHADOW_PRIVATE_RANGE;
    default:
        return HV_EFI_RUNTIME_SHADOW_INVARIANT;
    }
}

void InitializeResponse(const HV_EFI_CONTEXT *context,
                        const HV_EFI_RUNTIME_SHADOW_REQUEST *request,
                        HV_EFI_RUNTIME_SHADOW_RESPONSE *response)
{
    Zero(response, sizeof(*response));
    response->Size = sizeof(*response);
    response->Version = HV_EFI_RUNTIME_SHADOW_VERSION;
    response->Generation = context != nullptr ? context->RuntimeGeneration : 0;
    response->Sequence = request != nullptr ? request->Sequence : 0;
}

void CaptureResponse(const ShadowEptManager *manager,
                     HV_EFI_RUNTIME_SHADOW_RESPONSE *response)
{
    response->State = manager->State;
    response->ProcessId = manager->TargetProcessId;
    response->RangeCount = manager->RangeCount;
    response->Active = manager->Active;
    response->UsedTablePages = manager->UsedTablePages;
    response->PendingSynchronization = manager->PendingSynchronization;
    response->ViolationCount = manager->ViolationCount;
    response->WriteRedirectCount = manager->WriteRedirectCount;
    response->SynchronizationCount = manager->SynchronizationCount;
}

void SetWritable(HV_EFI_SCANNER_WINDOW *window,
                 const HV_EFI_SCANNER_MAPPING *mapping, bool writable)
{
    for (uint32_t index = 0; index < mapping->PageCount; ++index) {
        if (writable) window->PageTableEntries[index] |= kWritable;
        else window->PageTableEntries[index] &= ~kWritable;
    }
    Barrier();
    for (uint32_t index = 0; index < mapping->PageCount; ++index) {
        window->InvalidatePage(window->InvalidateContext,
            window->VirtualBase +
                static_cast<uint64_t>(index) * HV_EFI_SCANNER_PAGE_SIZE);
    }
}

HV_EFI_RUNTIME_SHADOW_STATUS Execute(
    HV_EFI_CONTEXT *context,
    const HV_EFI_SCANNER_RAM_RANGE *allowedRanges,
    uint32_t allowedRangeCount,
    const HV_EFI_RUNTIME_SHADOW_REQUEST *request,
    HV_EFI_RUNTIME_SHADOW_RESPONSE *response)
{
    InitializeResponse(context, request, response);
    if (!RequestValid(request))
        return HV_EFI_RUNTIME_SHADOW_PAYLOAD_INVALID;
    if (context->ShadowEptStatePhysical == 0 ||
        context->ShadowEptPhysical == 0 || context->ShadowEptPointer == 0)
        return HV_EFI_RUNTIME_SHADOW_CONTEXT_INVALID;
    auto *manager = reinterpret_cast<ShadowEptManager *>(
        static_cast<uintptr_t>(context->ShadowEptStatePhysical));
    HV_EFI_RUNTIME_SHADOW_STATUS result = HV_EFI_RUNTIME_SHADOW_SUCCESS;
    if (request->Operation == HV_EFI_RUNTIME_SHADOW_BIND) {
        /* PID 4 is the Windows System process and is an explicit kernel
         * boundary. Kernel CR3s are never admitted as target processes. */
        if (request->ProcessId == 4) {
            result = HV_EFI_RUNTIME_SHADOW_KERNEL_REJECTED;
        } else {
            const HV_EFI_GUEST_PROCESS_RECORD *process =
                FindProcess(context, request->ProcessId);
            if (process == nullptr || process->DirectoryTableBase == 0) {
                result = HV_EFI_RUNTIME_SHADOW_PROCESS_NOT_CACHED;
            } else {
                for (uint32_t index = 0; index < request->RangeCount; ++index) {
                    const HV_EFI_SHADOW_EPT_RANGE *range =
                        &request->Ranges[index];
                    if (!Allowed(allowedRanges, allowedRangeCount,
                                 range->PhysicalBase, range->Size)) {
                        result = HV_EFI_RUNTIME_SHADOW_PHYSICAL_RANGE;
                        break;
                    }
                    if (IntersectsPrivate(context, range->PhysicalBase,
                                          range->Size)) {
                        result = HV_EFI_RUNTIME_SHADOW_PRIVATE_RANGE;
                        break;
                    }
                }
                if (result == HV_EFI_RUNTIME_SHADOW_SUCCESS) {
                    HV_EFI_SHADOW_EPT_STATUS status =
                        manager->Version == HV_EFI_SHADOW_EPT_VERSION &&
                        manager->State != HV_EFI_SHADOW_EPT_FAILED_SAFE
                        ? HV_EFI_SHADOW_EPT_OK
                        : HV_EFI_SHADOW_EPT_INVALID_STATE;
                    if (status == HV_EFI_SHADOW_EPT_OK &&
                        manager->RangeCount != 0) {
                        status = HvEfiShadowEptReset(manager);
                    }
                    for (uint32_t index = 0;
                         status == HV_EFI_SHADOW_EPT_OK &&
                         index < request->RangeCount; ++index) {
                        status = HvEfiShadowEptAddReadExecuteRange(
                            manager, request->Ranges[index].PhysicalBase,
                            request->Ranges[index].Size, nullptr, 0);
                    }
                    if (status == HV_EFI_SHADOW_EPT_OK) {
                        status = HvEfiShadowEptBind(
                            manager, process->ProcessId,
                            process->DirectoryTableBase);
                    }
                    result = MapStatus(status);
                    if (result == HV_EFI_RUNTIME_SHADOW_SUCCESS) {
                        context->ShadowTargetCr3 = manager->View.TargetCr3;
                        context->ShadowState = manager->State;
                        context->ShadowActive = 0;
                    } else {
                        (void)HvEfiShadowEptUnbind(manager);
                        context->ShadowTargetCr3 = 0;
                        context->ShadowState = manager->State;
                        context->ShadowActive = 0;
                    }
                }
            }
        }
    } else if (request->Operation == HV_EFI_RUNTIME_SHADOW_UNBIND) {
        result = MapStatus(HvEfiShadowEptUnbind(manager));
        context->ShadowTargetCr3 = 0;
        context->ShadowState = manager->State;
        context->ShadowActive = 0;
    }
    CaptureResponse(manager, response);
    response->Active = context->ShadowActive;
    response->ViolationCount = context->ShadowViolationCount;
    response->WriteRedirectCount = context->ShadowWriteRedirectCount;
    response->Status = result;
    return result;
}

} // namespace

extern "C" uint64_t HvEfiRuntimeShadowResidentCommand(
    HV_EFI_CONTEXT *context,
    uint64_t mailboxPhysical,
    uint64_t mailboxBytes,
    uint64_t expectedSequence)
{
    if (context == nullptr || mailboxPhysical == 0 || expectedSequence == 0 ||
        mailboxBytes < HVR1_MAILBOX_HEADER_BYTES +
            sizeof(HV_EFI_RUNTIME_SHADOW_MAILBOX) ||
        mailboxBytes > HVR1_MAILBOX_BYTES ||
        context->RuntimeScannerRamRangesPhysical == 0 ||
        context->RuntimeScannerRamRangeCount == 0)
        return HV_EFI_RUNTIME_SHADOW_CALL_ARGUMENT_INVALID;

    HV_EFI_SCANNER_WINDOW window;
    Zero(&window, sizeof(window));
    const HV_EFI_SCANNER_STATUS windowStatus =
        HvEfiScannerInitializeNativeWindow(
            reinterpret_cast<uint64_t *>(
                static_cast<uintptr_t>(context->PageTablePhysical)),
            HV_EFI_PAGE_TABLE_PAGES, context->PageTablePhysical, &window);
    if (windowStatus != HV_EFI_SCANNER_OK)
        return kWindowInitDiagnosticBase +
            static_cast<uint64_t>(windowStatus);
    const auto *ranges = reinterpret_cast<const HV_EFI_SCANNER_RAM_RANGE *>(
        static_cast<uintptr_t>(context->RuntimeScannerRamRangesPhysical));
    const uint64_t commandBytes = HVR1_MAILBOX_HEADER_BYTES +
        sizeof(HV_EFI_RUNTIME_SHADOW_MAILBOX);
    HV_EFI_SCANNER_MAPPING mapping;
    Zero(&mapping, sizeof(mapping));
    if (HvEfiScannerMapReadOnly(
            &window, ranges, context->RuntimeScannerRamRangeCount,
            mailboxPhysical, commandBytes, &mapping) != HV_EFI_SCANNER_OK)
        return HV_EFI_RUNTIME_SHADOW_REQUEST_MAP_FAILED;

    HV_EFI_RUNTIME_SHADOW_MAILBOX local;
    Zero(&local, sizeof(local));
    const auto *outer = reinterpret_cast<const HVR1_MAILBOX *>(mapping.View);
    const bool outerValid = outer->Magic == HVR1_PROTOCOL_MAGIC &&
        outer->Version == HVR1_PROTOCOL_VERSION &&
        outer->HeaderSize == HVR1_MAILBOX_HEADER_BYTES &&
        outer->TotalSize == commandBytes &&
        outer->Operation == HVR1_MAILBOX_OPERATION_SHADOW_EPT &&
        outer->Flags == 0 && outer->Generation == context->RuntimeGeneration &&
        outer->Sequence == expectedSequence &&
        outer->PayloadOffset == HVR1_MAILBOX_HEADER_BYTES &&
        outer->PayloadSize == sizeof(local) && outer->Status == 0 &&
        outer->Reserved == 0;
    if (outerValid) Copy(&local,
        mapping.View + HVR1_MAILBOX_PAYLOAD_DATA_OFFSET, sizeof(local));
    if (HvEfiScannerUnmap(&window, &mapping) != HV_EFI_SCANNER_OK)
        return HV_EFI_RUNTIME_SHADOW_REQUEST_UNMAP_FAILED;

    HV_EFI_RUNTIME_SHADOW_STATUS status =
        outerValid ? HV_EFI_RUNTIME_SHADOW_PAYLOAD_INVALID :
                     HV_EFI_RUNTIME_SHADOW_ENVELOPE_INVALID;
    if (outerValid && local.Magic == HV_EFI_RUNTIME_SHADOW_MAGIC &&
        local.Version == HV_EFI_RUNTIME_SHADOW_VERSION &&
        local.HeaderSize == offsetof(HV_EFI_RUNTIME_SHADOW_MAILBOX, Request) &&
        local.State == HV_EFI_RUNTIME_SHADOW_MAILBOX_REQUEST_READY &&
        local.Reserved == 0 &&
        local.Generation == context->RuntimeGeneration &&
        local.Request.Sequence == expectedSequence) {
        local.State = HV_EFI_RUNTIME_SHADOW_MAILBOX_PROCESSING;
        status = Execute(context, ranges,
                         context->RuntimeScannerRamRangeCount,
                         &local.Request, &local.Response);
        local.State = HV_EFI_RUNTIME_SHADOW_MAILBOX_RESPONSE_READY;
    }

    /* The companion supplies one physically distinct mailbox per logical
     * processor. Every processor can therefore publish its own result without
     * racing another root-mode writer. */
    Zero(&mapping, sizeof(mapping));
    if (HvEfiScannerMapReadOnly(
            &window, ranges, context->RuntimeScannerRamRangeCount,
            mailboxPhysical, commandBytes, &mapping) != HV_EFI_SCANNER_OK) {
        Zero(&local, sizeof(local));
        return HV_EFI_RUNTIME_SHADOW_RESPONSE_MAP_FAILED;
    }
    SetWritable(&window, &mapping, true);
    Copy(const_cast<uint8_t *>(mapping.View) +
             HVR1_MAILBOX_PAYLOAD_DATA_OFFSET,
         &local, sizeof(local));
    SetWritable(&window, &mapping, false);
    const HV_EFI_SCANNER_STATUS unmap = HvEfiScannerUnmap(&window, &mapping);
    Zero(&local, sizeof(local));
    return unmap == HV_EFI_SCANNER_OK ? status :
        HV_EFI_RUNTIME_SHADOW_RESPONSE_UNMAP_FAILED;
}

#if defined(_MSC_VER)
#pragma code_seg(pop)
#endif
