#include "runtime_scanner.h"

#include "../driver/efi_runtime_companion/hvr1_protocol.h"
#include "efi_hypervisor.h"

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_InterlockedCompareExchange)
#pragma code_seg(push, ".hvscan$m")
#endif

namespace {

constexpr uint64_t kWritable = UINT64_C(2);

void ZeroBytes(void *memory, size_t size)
{
    volatile uint8_t *bytes = static_cast<volatile uint8_t *>(memory);
    while (size-- != 0) {
        *bytes++ = 0;
    }
}

void CopyBytes(void *destination, const void *source, size_t size)
{
    uint8_t *to = static_cast<uint8_t *>(destination);
    const uint8_t *from = static_cast<const uint8_t *>(source);
    while (size-- != 0) {
        *to++ = *from++;
    }
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

bool TryAcquire(volatile uint32_t *value)
{
#if defined(_MSC_VER)
    return _InterlockedCompareExchange(
        reinterpret_cast<volatile long *>(value), 1, 0) == 0;
#else
    uint32_t expected = 0;
    return __atomic_compare_exchange_n(
        value, &expected, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
#endif
}

void Release(volatile uint32_t *value)
{
    Barrier();
#if defined(_MSC_VER)
    _InterlockedExchange(reinterpret_cast<volatile long *>(value), 0);
#else
    __atomic_store_n(value, 0, __ATOMIC_RELEASE);
#endif
}

bool AddChecked(uint64_t left, uint64_t right, uint64_t *sum)
{
    if (sum == nullptr || left > UINT64_MAX - right) {
        return false;
    }
    *sum = left + right;
    return true;
}

bool RequestValid(const HV_EFI_RUNTIME_SCAN_REQUEST *request)
{
    if (request == nullptr ||
        request->Size != sizeof(*request) ||
        request->Version != HV_EFI_RUNTIME_SCAN_VERSION ||
        request->PatternLength == 0 ||
        request->PatternLength > HV_EFI_RUNTIME_SCAN_MAX_PATTERN_BYTES ||
        request->MaxResults == 0 ||
        request->MaxResults > HV_EFI_RUNTIME_SCAN_MAX_RESULTS ||
        request->Sequence == 0 ||
        (request->CallbackType != HV_EFI_RUNTIME_SCAN_CALLBACK_FIRST_MATCH &&
         request->CallbackType != HV_EFI_RUNTIME_SCAN_CALLBACK_SINGLE &&
         request->CallbackType != HV_EFI_RUNTIME_SCAN_CALLBACK_ALL)) {
        return false;
    }
    if (request->PatternLength < 64 &&
        (request->WildcardMask >> request->PatternLength) != 0) {
        return false;
    }
    const uint64_t end = request->EndAddress == 0
        ? HV_EFI_SCANNER_PHYSICAL_LIMIT : request->EndAddress;
    if (request->StartAddress >= end || end > HV_EFI_SCANNER_PHYSICAL_LIMIT) {
        return false;
    }
    return request->MaxBytesToScan <= HV_EFI_RUNTIME_SCAN_MAX_BUDGET_BYTES;
}

HV_EFI_SCANNER_STATUS CompileRequestPattern(
    const HV_EFI_RUNTIME_SCAN_REQUEST *request,
    HV_EFI_SCANNER_PATTERN_BYTE *storage,
    HV_EFI_SCANNER_COMPILED_PATTERN *compiled)
{
    char text[HV_EFI_RUNTIME_SCAN_MAX_PATTERN_BYTES * 3];
    uint32_t cursor = 0;
    for (uint32_t index = 0; index < request->PatternLength; ++index) {
        if (index != 0) {
            text[cursor++] = ' ';
        }
        if ((request->WildcardMask & (UINT64_C(1) << index)) != 0) {
            text[cursor++] = '?';
            text[cursor++] = '?';
        } else {
            const uint8_t high = request->Pattern[index] >> 4;
            const uint8_t low = request->Pattern[index] & 0x0f;
            text[cursor++] = static_cast<char>(
                high < 10 ? '0' + high : 'A' + high - 10);
            text[cursor++] = static_cast<char>(
                low < 10 ? '0' + low : 'A' + low - 10);
        }
    }
    text[cursor] = '\0';
    const HV_EFI_SCANNER_STATUS status = HvEfiScannerCompilePattern(
        text, storage, HV_EFI_RUNTIME_SCAN_MAX_PATTERN_BYTES, compiled);
    ZeroBytes(text, sizeof(text));
    return status;
}

void InitializeResponse(
    uint64_t generation,
    const HV_EFI_RUNTIME_SCAN_REQUEST *request,
    HV_EFI_RUNTIME_SCAN_RESPONSE *response)
{
    ZeroBytes(response, sizeof(*response));
    response->Size = sizeof(*response);
    response->Version = HV_EFI_RUNTIME_SCAN_VERSION;
    response->Status = HV_EFI_RUNTIME_SCAN_INVALID_REQUEST;
    response->Generation = generation;
    response->Sequence = request != nullptr ? request->Sequence : 0;
    response->ResultCapacity = request != nullptr ? request->MaxResults : 0;
}

HV_EFI_RUNTIME_SCAN_STATUS MapScannerStatus(HV_EFI_SCANNER_STATUS status)
{
    switch (status) {
    case HV_EFI_SCANNER_RANGE_NOT_ALLOWED:
        return HV_EFI_RUNTIME_SCAN_RANGE_DENIED;
    case HV_EFI_SCANNER_ALREADY_MAPPED:
    case HV_EFI_SCANNER_NOT_MAPPED:
    case HV_EFI_SCANNER_WRITE_PROTECT_REQUIRED:
    case HV_EFI_SCANNER_NO_EXECUTE_REQUIRED:
        return HV_EFI_RUNTIME_SCAN_MAPPING_FAILED;
    case HV_EFI_SCANNER_INVARIANT_VIOLATION:
        return HV_EFI_RUNTIME_SCAN_INVARIANT_VIOLATION;
    default:
        return HV_EFI_RUNTIME_SCAN_SCANNER_FAILED;
    }
}

bool RangeIntersection(
    const HV_EFI_SCANNER_RAM_RANGE &allowed,
    uint64_t requestStart,
    uint64_t requestEnd,
    uint64_t *start,
    uint64_t *end)
{
    uint64_t allowedEnd;
    if (allowed.Size == 0 ||
        !AddChecked(allowed.PhysicalBase, allowed.Size, &allowedEnd)) {
        return false;
    }
    *start = allowed.PhysicalBase > requestStart
        ? allowed.PhysicalBase : requestStart;
    *end = allowedEnd < requestEnd ? allowedEnd : requestEnd;
    return *start < *end;
}

HV_EFI_RUNTIME_SCAN_STATUS ScanRange(
    HV_EFI_RUNTIME_SCAN_STATE *state,
    HV_EFI_SCANNER_WINDOW *window,
    const HV_EFI_SCANNER_RAM_RANGE *allowedRanges,
    uint32_t allowedRangeCount,
    const HV_EFI_SCANNER_COMPILED_PATTERN *pattern,
    const HV_EFI_RUNTIME_SCAN_REQUEST *request,
    uint64_t rangeStart,
    uint64_t rangeEnd,
    uint64_t budgetEnd,
    uint32_t *matchCount,
    uint32_t *multiple,
    uint64_t *nextAddress)
{
    uint64_t cursor = rangeStart;
    const uint64_t effectiveEnd = rangeEnd < budgetEnd ? rangeEnd : budgetEnd;
    while (cursor < effectiveEnd &&
           effectiveEnd - cursor >= pattern->Length) {
        uint64_t match = 0;
        const HV_EFI_SCANNER_STATUS scannerStatus =
            HvEfiScannerFindPhysicalFirst(
                window, allowedRanges, allowedRangeCount,
                cursor, effectiveEnd - cursor, pattern, &match);
        if (scannerStatus == HV_EFI_SCANNER_NOT_FOUND) {
            *nextAddress = effectiveEnd;
            return HV_EFI_RUNTIME_SCAN_SUCCESS;
        }
        if (scannerStatus != HV_EFI_SCANNER_OK || match < cursor ||
            match >= effectiveEnd) {
            return MapScannerStatus(scannerStatus);
        }

        if (*matchCount < request->MaxResults) {
            state->Results[*matchCount].PhysicalAddress = match;
            state->Results[*matchCount].Status = HV_EFI_RUNTIME_SCAN_SUCCESS;
            state->Results[*matchCount].Reserved = 0;
            ++(*matchCount);
        } else {
            *nextAddress = match;
            return HV_EFI_RUNTIME_SCAN_RESULT_CAPACITY;
        }

        if (request->CallbackType ==
                HV_EFI_RUNTIME_SCAN_CALLBACK_FIRST_MATCH) {
            *nextAddress = match + 1;
            return HV_EFI_RUNTIME_SCAN_SUCCESS;
        }
        if (request->CallbackType == HV_EFI_RUNTIME_SCAN_CALLBACK_SINGLE &&
            *matchCount > 1) {
            *multiple = 1;
            *nextAddress = match + 1;
            return HV_EFI_RUNTIME_SCAN_MULTIPLE_MATCHES;
        }
        if (match == UINT64_MAX) {
            return HV_EFI_RUNTIME_SCAN_INVARIANT_VIOLATION;
        }
        cursor = match + 1;
        *nextAddress = cursor;
    }
    if (cursor < effectiveEnd) {
        *nextAddress = effectiveEnd;
    }
    return HV_EFI_RUNTIME_SCAN_SUCCESS;
}

void SetWritable(
    HV_EFI_SCANNER_WINDOW *window,
    const HV_EFI_SCANNER_MAPPING *mapping,
    bool writable)
{
    for (uint32_t index = 0; index < mapping->PageCount; ++index) {
        if (writable) {
            window->PageTableEntries[index] |= kWritable;
        } else {
            window->PageTableEntries[index] &= ~kWritable;
        }
    }
    Barrier();
    for (uint32_t index = 0; index < mapping->PageCount; ++index) {
        window->InvalidatePage(
            window->InvalidateContext,
            window->VirtualBase +
                static_cast<uint64_t>(index) * HV_EFI_SCANNER_PAGE_SIZE);
    }
}

} // namespace

extern "C" HV_EFI_RUNTIME_SCAN_STATUS HvEfiRuntimeScannerWriteRequest(
    HV_EFI_RUNTIME_SCAN_MAILBOX *mailbox,
    size_t mailboxCapacity,
    uint64_t generation,
    const HV_EFI_RUNTIME_SCAN_REQUEST *request)
{
    if (mailbox == nullptr ||
        mailboxCapacity < sizeof(*mailbox) || generation == 0 ||
        !RequestValid(request)) {
        return HV_EFI_RUNTIME_SCAN_INVALID_REQUEST;
    }
    ZeroBytes(mailbox, sizeof(*mailbox));
    mailbox->Magic = HV_EFI_RUNTIME_SCAN_MAGIC;
    mailbox->Version = HV_EFI_RUNTIME_SCAN_VERSION;
    mailbox->HeaderSize = static_cast<uint16_t>(
        offsetof(HV_EFI_RUNTIME_SCAN_MAILBOX, Request));
    mailbox->Generation = generation;
    CopyBytes(&mailbox->Request, request, sizeof(*request));
    Barrier();
    mailbox->State = HV_EFI_RUNTIME_SCAN_MAILBOX_REQUEST_READY;
    return HV_EFI_RUNTIME_SCAN_SUCCESS;
}

extern "C" HV_EFI_RUNTIME_SCAN_STATUS HvEfiRuntimeScannerReadResponse(
    const HV_EFI_RUNTIME_SCAN_MAILBOX *mailbox,
    size_t mailboxCapacity,
    uint64_t expectedGeneration,
    uint64_t expectedSequence,
    HV_EFI_RUNTIME_SCAN_RESPONSE *response)
{
    if (mailbox == nullptr || response == nullptr ||
        mailboxCapacity < sizeof(*mailbox) ||
        mailbox->Magic != HV_EFI_RUNTIME_SCAN_MAGIC ||
        mailbox->Version != HV_EFI_RUNTIME_SCAN_VERSION ||
        mailbox->HeaderSize != offsetof(HV_EFI_RUNTIME_SCAN_MAILBOX, Request) ||
        mailbox->State != HV_EFI_RUNTIME_SCAN_MAILBOX_RESPONSE_READY ||
        mailbox->Generation != expectedGeneration ||
        mailbox->Response.Size != sizeof(mailbox->Response) ||
        mailbox->Response.Version != HV_EFI_RUNTIME_SCAN_VERSION ||
        mailbox->Response.Generation != expectedGeneration ||
        mailbox->Response.Sequence != expectedSequence ||
        mailbox->Response.ResultCount > HV_EFI_RUNTIME_SCAN_MAX_RESULTS ||
        mailbox->Response.ResultCount > mailbox->Response.ResultCapacity) {
        return HV_EFI_RUNTIME_SCAN_INVALID_REQUEST;
    }
    Barrier();
    CopyBytes(response, &mailbox->Response, sizeof(*response));
    return static_cast<HV_EFI_RUNTIME_SCAN_STATUS>(response->Status);
}

extern "C" HV_EFI_RUNTIME_SCAN_STATUS HvEfiRuntimeScannerExecute(
    HV_EFI_RUNTIME_SCAN_STATE *state,
    HV_EFI_SCANNER_WINDOW *window,
    const HV_EFI_SCANNER_RAM_RANGE *allowedRanges,
    uint32_t allowedRangeCount,
    uint64_t generation,
    const HV_EFI_RUNTIME_SCAN_REQUEST *request,
    HV_EFI_RUNTIME_SCAN_RESPONSE *response)
{
    InitializeResponse(generation, request, response);
    if (state == nullptr || window == nullptr || allowedRanges == nullptr ||
        allowedRangeCount == 0 || response == nullptr || generation == 0 ||
        state->Marker != HV_EFI_RUNTIME_SCAN_STATE_MARKER ||
        !RequestValid(request)) {
        return HV_EFI_RUNTIME_SCAN_INVALID_REQUEST;
    }
    if (!TryAcquire(&state->Busy)) {
        response->Status = HV_EFI_RUNTIME_SCAN_BUSY;
        return HV_EFI_RUNTIME_SCAN_BUSY;
    }

    HV_EFI_RUNTIME_SCAN_STATUS status = HV_EFI_RUNTIME_SCAN_SUCCESS;
    if (request->Sequence <= state->LastSequence) {
        status = HV_EFI_RUNTIME_SCAN_REPLAY_REJECTED;
        goto complete;
    }
    ZeroBytes(state->Results, sizeof(state->Results));

    HV_EFI_SCANNER_PATTERN_BYTE patternStorage[
        HV_EFI_RUNTIME_SCAN_MAX_PATTERN_BYTES];
    HV_EFI_SCANNER_COMPILED_PATTERN pattern;
    ZeroBytes(patternStorage, sizeof(patternStorage));
    ZeroBytes(&pattern, sizeof(pattern));
    {
        const HV_EFI_SCANNER_STATUS scannerStatus = CompileRequestPattern(
            request, patternStorage, &pattern);
        if (scannerStatus != HV_EFI_SCANNER_OK) {
            status = MapScannerStatus(scannerStatus);
            goto complete;
        }
    }

    {
        const uint64_t requestEnd = request->EndAddress == 0
            ? HV_EFI_SCANNER_PHYSICAL_LIMIT : request->EndAddress;
        const uint64_t budget = request->MaxBytesToScan == 0
            ? HV_EFI_RUNTIME_SCAN_DEFAULT_BUDGET_BYTES
            : request->MaxBytesToScan;
        uint64_t budgetEnd;
        if (!AddChecked(request->StartAddress, budget, &budgetEnd) ||
            budgetEnd > requestEnd) {
            budgetEnd = requestEnd;
        }
        uint32_t matchCount = 0;
        uint32_t multiple = 0;
        uint64_t nextAddress = request->StartAddress;
        for (uint32_t index = 0; index < allowedRangeCount; ++index) {
            uint64_t rangeStart;
            uint64_t rangeEnd;
            if (!RangeIntersection(
                    allowedRanges[index], request->StartAddress,
                    requestEnd, &rangeStart, &rangeEnd)) {
                continue;
            }
            if (rangeStart >= budgetEnd) {
                break;
            }
            status = ScanRange(
                state, window, allowedRanges, allowedRangeCount,
                &pattern, request, rangeStart, rangeEnd, budgetEnd,
                &matchCount, &multiple, &nextAddress);
            if (status != HV_EFI_RUNTIME_SCAN_SUCCESS ||
                (request->CallbackType ==
                    HV_EFI_RUNTIME_SCAN_CALLBACK_FIRST_MATCH &&
                    matchCount != 0)) {
                break;
            }
        }

        response->ScannedBytes = budgetEnd - request->StartAddress;
        response->NextAddress = nextAddress < budgetEnd
            ? nextAddress : budgetEnd;
        response->ResultCount = matchCount;
        CopyBytes(response->Results, state->Results,
                  static_cast<size_t>(matchCount) * sizeof(state->Results[0]));
        if (status == HV_EFI_RUNTIME_SCAN_SUCCESS) {
            if (multiple != 0) {
                status = HV_EFI_RUNTIME_SCAN_MULTIPLE_MATCHES;
            } else if (request->CallbackType ==
                           HV_EFI_RUNTIME_SCAN_CALLBACK_FIRST_MATCH &&
                       matchCount != 0) {
                status = HV_EFI_RUNTIME_SCAN_SUCCESS;
            } else if (budgetEnd < requestEnd) {
                status = HV_EFI_RUNTIME_SCAN_MORE_DATA;
                response->NextAddress = budgetEnd;
            } else if (matchCount == 0) {
                status = HV_EFI_RUNTIME_SCAN_NOT_FOUND;
            }
        }
    }

complete:
    response->Status = status;
    state->LastSequence = request->Sequence;
    ZeroBytes(state->Results, sizeof(state->Results));
    Release(&state->Busy);
    return status;
}

extern "C" uint64_t HvEfiRuntimeScannerResidentCommand(
    HV_EFI_CONTEXT *context,
    uint64_t mailboxPhysical,
    uint64_t mailboxBytes,
    uint64_t expectedSequence)
{
    if (context == nullptr || mailboxPhysical == 0 ||
        mailboxBytes < HVR1_MAILBOX_HEADER_BYTES +
            sizeof(HV_EFI_RUNTIME_SCAN_MAILBOX) ||
        mailboxBytes > HVR1_MAILBOX_BYTES || expectedSequence == 0 ||
        mailboxPhysical > UINT64_MAX -
            (HVR1_MAILBOX_HEADER_BYTES +
             sizeof(HV_EFI_RUNTIME_SCAN_MAILBOX)) ||
        context->RuntimeScannerStatePhysical == 0 ||
        context->RuntimeScannerRamRangesPhysical == 0 ||
        context->RuntimeScannerRamRangeCount == 0) {
        return HV_EFI_RUNTIME_SCAN_INVALID_REQUEST;
    }

    HV_EFI_SCANNER_WINDOW window;
    ZeroBytes(&window, sizeof(window));
    HV_EFI_SCANNER_STATUS scannerStatus = HvEfiScannerInitializeNativeWindow(
        reinterpret_cast<uint64_t *>(
            static_cast<uintptr_t>(context->PageTablePhysical)),
        HV_EFI_PAGE_TABLE_PAGES, context->PageTablePhysical, &window);
    if (scannerStatus != HV_EFI_SCANNER_OK) {
        return MapScannerStatus(scannerStatus);
    }

    const HV_EFI_SCANNER_RAM_RANGE *ranges =
        reinterpret_cast<const HV_EFI_SCANNER_RAM_RANGE *>(
            static_cast<uintptr_t>(
                context->RuntimeScannerRamRangesPhysical));
    const uint64_t commandBytes = HVR1_MAILBOX_HEADER_BYTES +
        sizeof(HV_EFI_RUNTIME_SCAN_MAILBOX);
    HV_EFI_SCANNER_MAPPING mapping;
    ZeroBytes(&mapping, sizeof(mapping));
    scannerStatus = HvEfiScannerMapReadOnly(
        &window, ranges, context->RuntimeScannerRamRangeCount,
        mailboxPhysical, commandBytes, &mapping);
    if (scannerStatus != HV_EFI_SCANNER_OK) {
        return MapScannerStatus(scannerStatus);
    }

    HV_EFI_RUNTIME_SCAN_MAILBOX localMailbox;
    const HVR1_MAILBOX *outer =
        reinterpret_cast<const HVR1_MAILBOX *>(mapping.View);
    const bool outerValid =
        outer->Magic == HVR1_PROTOCOL_MAGIC &&
        outer->Version == HVR1_PROTOCOL_VERSION &&
        outer->HeaderSize == HVR1_MAILBOX_HEADER_BYTES &&
        outer->TotalSize == commandBytes &&
        outer->Operation == HVR1_MAILBOX_OPERATION_RUNTIME_SCAN &&
        outer->Flags == 0 && outer->Generation == context->RuntimeGeneration &&
        outer->Sequence == expectedSequence &&
        outer->PayloadOffset == HVR1_MAILBOX_HEADER_BYTES &&
        outer->PayloadSize == sizeof(HV_EFI_RUNTIME_SCAN_MAILBOX) &&
        outer->Status == 0 && outer->Reserved == 0;
    if (outerValid) {
        CopyBytes(
            &localMailbox,
            mapping.View + HVR1_MAILBOX_PAYLOAD_DATA_OFFSET,
            sizeof(localMailbox));
    } else {
        ZeroBytes(&localMailbox, sizeof(localMailbox));
    }
    scannerStatus = HvEfiScannerUnmap(&window, &mapping);
    if (scannerStatus != HV_EFI_SCANNER_OK) {
        ZeroBytes(&localMailbox, sizeof(localMailbox));
        return MapScannerStatus(scannerStatus);
    }

    HV_EFI_RUNTIME_SCAN_STATUS status = HV_EFI_RUNTIME_SCAN_INVALID_REQUEST;
    if (outerValid &&
        localMailbox.Magic == HV_EFI_RUNTIME_SCAN_MAGIC &&
        localMailbox.Version == HV_EFI_RUNTIME_SCAN_VERSION &&
        localMailbox.HeaderSize ==
            offsetof(HV_EFI_RUNTIME_SCAN_MAILBOX, Request) &&
        localMailbox.State == HV_EFI_RUNTIME_SCAN_MAILBOX_REQUEST_READY &&
        localMailbox.Reserved == 0 &&
        localMailbox.Generation == context->RuntimeGeneration &&
        localMailbox.Request.Sequence == expectedSequence) {
        localMailbox.State = HV_EFI_RUNTIME_SCAN_MAILBOX_PROCESSING;
        HV_EFI_RUNTIME_SCAN_STATE *state =
            reinterpret_cast<HV_EFI_RUNTIME_SCAN_STATE *>(
                static_cast<uintptr_t>(
                    context->RuntimeScannerStatePhysical));
        status = HvEfiRuntimeScannerExecute(
            state, &window, ranges,
            context->RuntimeScannerRamRangeCount,
            context->RuntimeGeneration, &localMailbox.Request,
            &localMailbox.Response);
        localMailbox.State = HV_EFI_RUNTIME_SCAN_MAILBOX_RESPONSE_READY;
    }

    ZeroBytes(&mapping, sizeof(mapping));
    scannerStatus = HvEfiScannerMapReadOnly(
        &window, ranges, context->RuntimeScannerRamRangeCount,
        mailboxPhysical, commandBytes, &mapping);
    if (scannerStatus != HV_EFI_SCANNER_OK) {
        ZeroBytes(&localMailbox, sizeof(localMailbox));
        return MapScannerStatus(scannerStatus);
    }
    SetWritable(&window, &mapping, true);
    CopyBytes(const_cast<uint8_t *>(mapping.View) +
                  HVR1_MAILBOX_PAYLOAD_DATA_OFFSET,
              &localMailbox, sizeof(localMailbox));
    SetWritable(&window, &mapping, false);
    scannerStatus = HvEfiScannerUnmap(&window, &mapping);
    ZeroBytes(&localMailbox, sizeof(localMailbox));
    if (scannerStatus != HV_EFI_SCANNER_OK) {
        return MapScannerStatus(scannerStatus);
    }
    return status;
}

#if defined(_MSC_VER)
#pragma code_seg(pop)
#endif
