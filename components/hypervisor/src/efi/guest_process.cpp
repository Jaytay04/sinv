#include "runtime_process.h"

#include "../driver/efi_runtime_companion/hvr1_protocol.h"
#include "efi_hypervisor.h"

#if HYPERSINV_KERNEL_PROFILE_AVAILABLE
#if defined(_MSC_VER)
#pragma const_seg(push, ".hvscan$p")
#endif
#include "kernel_profile_gen.h"
#if defined(_MSC_VER)
#pragma const_seg(pop)
#endif
#endif

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(__cpuid)
#pragma code_seg(push, ".hvscan$m")
#endif

namespace {

constexpr uint64_t kWritable = UINT64_C(2);
constexpr uint64_t kPageMask = UINT64_C(4095);
constexpr uint32_t kDosMagic = UINT32_C(0x5a4d);
constexpr uint32_t kPeSignature = UINT32_C(0x00004550);
constexpr uint16_t kPe64Magic = UINT16_C(0x20b);
constexpr uint32_t kCodeViewType = UINT32_C(2);
constexpr uint32_t kRsds = UINT32_C(0x53445352);

void ZeroBytes(void *memory, size_t size)
{
    volatile uint8_t *bytes = static_cast<volatile uint8_t *>(memory);
    while (size-- != 0) *bytes++ = 0;
}

void CopyBytes(void *destination, const void *source, size_t size)
{
    uint8_t *to = static_cast<uint8_t *>(destination);
    const uint8_t *from = static_cast<const uint8_t *>(source);
    while (size-- != 0) *to++ = *from++;
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
    if (sum == nullptr || left > UINT64_MAX - right) return false;
    *sum = left + right;
    return true;
}

bool Canonical48(uint64_t address)
{
    const bool sign = (address & (UINT64_C(1) << 47)) != 0;
    const uint64_t high = address >> 48;
    return sign ? high == UINT64_C(0xffff) : high == 0;
}

uint16_t Load16(const uint8_t *value)
{
    return static_cast<uint16_t>(value[0]) |
        static_cast<uint16_t>(value[1]) << 8;
}

uint32_t Load32(const uint8_t *value)
{
    return static_cast<uint32_t>(value[0]) |
        static_cast<uint32_t>(value[1]) << 8 |
        static_cast<uint32_t>(value[2]) << 16 |
        static_cast<uint32_t>(value[3]) << 24;
}

bool EqualBytes(const uint8_t *left, const uint8_t *right, size_t size)
{
    for (size_t index = 0; index < size; ++index) {
        if (left[index] != right[index]) return false;
    }
    return true;
}

char AsciiLower(char value)
{
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value + ('a' - 'A')) : value;
}

bool NameMatches(const char image[HV_EFI_RUNTIME_PROCESS_NAME_BYTES],
                 const HV_EFI_RUNTIME_PROCESS_REQUEST *request)
{
    for (uint32_t index = 0; index < request->NameLength; ++index) {
        if (image[index] == '\0' ||
            AsciiLower(image[index]) !=
                AsciiLower(request->ImageFileName[index])) {
            return false;
        }
    }
    return request->NameLength == 15 ||
        image[request->NameLength] == '\0';
}

bool RequestValid(const HV_EFI_RUNTIME_PROCESS_REQUEST *request)
{
    if (request == nullptr || request->Size != sizeof(*request) ||
        request->Version != HV_EFI_RUNTIME_PROCESS_VERSION ||
        request->NameLength == 0 || request->NameLength > 15 ||
        request->Sequence == 0 || request->KernelBase == 0 ||
        request->BuildNumber == 0 || request->Flags != 0 ||
        request->Reserved != 0 || !Canonical48(request->KernelBase) ||
        (request->KernelBase & kPageMask) != 0) {
        return false;
    }
    for (uint32_t index = 0; index < request->NameLength; ++index) {
        const unsigned char value = static_cast<unsigned char>(
            request->ImageFileName[index]);
        if (value < 0x20 || value > 0x7e) return false;
    }
    for (uint32_t index = request->NameLength;
         index < HV_EFI_RUNTIME_PROCESS_NAME_BYTES; ++index) {
        if (request->ImageFileName[index] != '\0') return false;
    }
    return true;
}

bool ProfileValid(const hypersinv::windows::KernelProfile *profile)
{
    return profile != nullptr && profile->SchemaVersion == 1 &&
        profile->BuildNumber != 0 && profile->ImageTimestamp != 0 &&
        profile->ImageSize != 0 && profile->PdbAge != 0 &&
        profile->EprocessSize != 0 &&
        profile->EprocessImageFileNameSize == 15 &&
        profile->PsActiveProcessHeadRva < profile->ImageSize &&
        static_cast<uint64_t>(profile->EprocessActiveProcessLinks) + 16 <=
            profile->EprocessSize &&
        static_cast<uint64_t>(profile->EprocessUniqueProcessId) + 8 <=
            profile->EprocessSize &&
        static_cast<uint64_t>(profile->EprocessImageFileName) + 15 <=
            profile->EprocessSize &&
        static_cast<uint64_t>(profile->EprocessDirectoryTableBase) + 8 <=
            profile->EprocessSize &&
        static_cast<uint64_t>(profile->EprocessPcb) +
            profile->KprocessDirectoryTableBase ==
                profile->EprocessDirectoryTableBase;
}

HV_EFI_RUNTIME_PROCESS_STATUS MapWalker(HV_EFI_GUEST_PT_STATUS status)
{
    switch (status) {
    case HV_EFI_GUEST_PT_OK:
        return HV_EFI_RUNTIME_PROCESS_SUCCESS;
    case HV_EFI_GUEST_PT_PHYSICAL_RANGE:
        return HV_EFI_RUNTIME_PROCESS_PHYSICAL_RANGE;
    case HV_EFI_GUEST_PT_MAPPING_FAILED:
        return HV_EFI_RUNTIME_PROCESS_MAPPING_FAILED;
    default:
        return HV_EFI_RUNTIME_PROCESS_TRANSLATION_FAILED;
    }
}

HV_EFI_RUNTIME_PROCESS_STATUS ReadGuest(
    const HV_EFI_GUEST_PT_WALKER *walker,
    uint64_t base,
    uint64_t offset,
    void *destination,
    size_t size)
{
    uint64_t address;
    if (!AddChecked(base, offset, &address) || !Canonical48(address))
        return HV_EFI_RUNTIME_PROCESS_CORRUPT_LIST;
    return MapWalker(HvEfiGuestPtRead(walker, address, destination, size));
}

HV_EFI_RUNTIME_PROCESS_STATUS ValidateKernelImage(
    const HV_EFI_GUEST_PT_WALKER *walker,
    uint64_t kernelBase,
    const hypersinv::windows::KernelProfile *profile)
{
    uint8_t dos[64];
    HV_EFI_GUEST_PT_STATUS walk =
        HvEfiGuestPtRead(walker, kernelBase, dos, sizeof(dos));
    if (walk != HV_EFI_GUEST_PT_OK) return MapWalker(walk);
    if (Load16(dos) != kDosMagic) return HV_EFI_RUNTIME_PROCESS_PROFILE_MISMATCH;
    const uint32_t peOffset = Load32(dos + 0x3c);
    if (peOffset < sizeof(dos) || peOffset > UINT32_C(0x100000))
        return HV_EFI_RUNTIME_PROCESS_PROFILE_MISMATCH;
    uint64_t peAddress;
    if (!AddChecked(kernelBase, peOffset, &peAddress))
        return HV_EFI_RUNTIME_PROCESS_PROFILE_MISMATCH;
    uint8_t pe[264];
    walk = HvEfiGuestPtRead(walker, peAddress, pe, sizeof(pe));
    if (walk != HV_EFI_GUEST_PT_OK) return MapWalker(walk);
    if (Load32(pe) != kPeSignature || Load16(pe + 24) != kPe64Magic ||
        Load32(pe + 8) != profile->ImageTimestamp ||
        Load32(pe + 80) != profile->ImageSize) {
        return HV_EFI_RUNTIME_PROCESS_PROFILE_MISMATCH;
    }
    const uint32_t debugRva = Load32(pe + 184);
    const uint32_t debugSize = Load32(pe + 188);
    if (debugRva == 0 || debugSize < 28 || debugSize > 28 * 64 ||
        debugSize % 28 != 0 || debugRva >= profile->ImageSize ||
        debugSize > profile->ImageSize - debugRva) {
        return HV_EFI_RUNTIME_PROCESS_PROFILE_MISMATCH;
    }
    for (uint32_t offset = 0; offset < debugSize; offset += 28) {
        uint8_t entry[28];
        HV_EFI_RUNTIME_PROCESS_STATUS status = ReadGuest(
            walker, kernelBase, static_cast<uint64_t>(debugRva) + offset,
            entry, sizeof(entry));
        if (status != HV_EFI_RUNTIME_PROCESS_SUCCESS) return status;
        if (Load32(entry + 12) != kCodeViewType ||
            Load32(entry + 16) < 24) continue;
        const uint32_t codeViewRva = Load32(entry + 20);
        if (codeViewRva == 0 || codeViewRva >= profile->ImageSize ||
            24 > profile->ImageSize - codeViewRva) continue;
        uint8_t codeView[24];
        status = ReadGuest(walker, kernelBase, codeViewRva,
                           codeView, sizeof(codeView));
        if (status != HV_EFI_RUNTIME_PROCESS_SUCCESS) return status;
        if (Load32(codeView) == kRsds &&
            EqualBytes(codeView + 4, profile->PdbGuid, 16) &&
            Load32(codeView + 20) == profile->PdbAge) {
            return HV_EFI_RUNTIME_PROCESS_SUCCESS;
        }
    }
    return HV_EFI_RUNTIME_PROCESS_PROFILE_MISMATCH;
}

HV_EFI_RUNTIME_PROCESS_STATUS ReadLink(
    const HV_EFI_GUEST_PT_WALKER *walker,
    uint64_t link,
    uint64_t *flink,
    uint64_t *blink)
{
    uint64_t links[2];
    if (!Canonical48(link) || (link & 7) != 0)
        return HV_EFI_RUNTIME_PROCESS_CORRUPT_LIST;
    const HV_EFI_GUEST_PT_STATUS walk =
        HvEfiGuestPtRead(walker, link, links, sizeof(links));
    if (walk != HV_EFI_GUEST_PT_OK) return MapWalker(walk);
    if (!Canonical48(links[0]) || !Canonical48(links[1]) ||
        links[0] == 0 || links[1] == 0 ||
        (links[0] & 7) != 0 || (links[1] & 7) != 0) {
        return HV_EFI_RUNTIME_PROCESS_CORRUPT_LIST;
    }
    *flink = links[0];
    *blink = links[1];
    return HV_EFI_RUNTIME_PROCESS_SUCCESS;
}

void StoreRecord(HV_EFI_RUNTIME_PROCESS_STATE *state,
                 const HV_EFI_GUEST_PROCESS_RECORD *record)
{
    if (state->StoredCount < HV_EFI_RUNTIME_PROCESS_MAX_PRIVATE_RECORDS) {
        state->Records[state->StoredCount++] = *record;
    } else {
        state->Truncated = 1;
    }
}

void InitializeResponse(uint64_t generation,
                        const HV_EFI_RUNTIME_PROCESS_REQUEST *request,
                        HV_EFI_RUNTIME_PROCESS_RESPONSE *response)
{
    ZeroBytes(response, sizeof(*response));
    response->Size = sizeof(*response);
    response->Version = HV_EFI_RUNTIME_PROCESS_VERSION;
    response->Status = HV_EFI_RUNTIME_PROCESS_INVALID_REQUEST;
    response->Generation = generation;
    response->Sequence = request != nullptr ? request->Sequence : 0;
}

void SetWritable(HV_EFI_SCANNER_WINDOW *window,
                 const HV_EFI_SCANNER_MAPPING *mapping,
                 bool writable)
{
    for (uint32_t index = 0; index < mapping->PageCount; ++index) {
        if (writable) window->PageTableEntries[index] |= kWritable;
        else window->PageTableEntries[index] &= ~kWritable;
    }
    Barrier();
    for (uint32_t index = 0; index < mapping->PageCount; ++index) {
        window->InvalidatePage(
            window->InvalidateContext,
            window->VirtualBase +
                static_cast<uint64_t>(index) * HV_EFI_SCANNER_PAGE_SIZE);
    }
}

uint32_t NativeMaxPhysicalAddressBits()
{
#if defined(_MSC_VER)
    int registers[4]{};
    __cpuid(registers, static_cast<int>(0x80000000u));
    if (static_cast<uint32_t>(registers[0]) < UINT32_C(0x80000008)) return 0;
    __cpuid(registers, static_cast<int>(0x80000008u));
    return static_cast<uint32_t>(registers[0]) & 0xffu;
#else
    uint32_t eax, ebx, ecx, edx;
    __asm__ __volatile__("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(UINT32_C(0x80000000)), "c"(0));
    if (eax < UINT32_C(0x80000008)) return 0;
    __asm__ __volatile__("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(UINT32_C(0x80000008)), "c"(0));
    return eax & 0xffu;
#endif
}

} // namespace

extern "C" HV_EFI_RUNTIME_PROCESS_STATUS HvEfiGuestProcessLocateWithWalker(
    HV_EFI_RUNTIME_PROCESS_STATE *state,
    const HV_EFI_GUEST_PT_WALKER *walker,
    const hypersinv::windows::KernelProfile *profile,
    uint64_t generation,
    const HV_EFI_RUNTIME_PROCESS_REQUEST *request,
    HV_EFI_RUNTIME_PROCESS_RESPONSE *response)
{
    InitializeResponse(generation, request, response);
    if (state == nullptr || walker == nullptr || !ProfileValid(profile) ||
        generation == 0 || response == nullptr || !RequestValid(request)) {
        return profile == nullptr ? HV_EFI_RUNTIME_PROCESS_PROFILE_UNAVAILABLE
                                  : HV_EFI_RUNTIME_PROCESS_INVALID_REQUEST;
    }
    if (request->BuildNumber != profile->BuildNumber ||
        request->Ubr != profile->Ubr) {
        response->Status = HV_EFI_RUNTIME_PROCESS_PROFILE_MISMATCH;
        return HV_EFI_RUNTIME_PROCESS_PROFILE_MISMATCH;
    }
    HV_EFI_RUNTIME_PROCESS_STATUS status =
        ValidateKernelImage(walker, request->KernelBase, profile);
    if (status != HV_EFI_RUNTIME_PROCESS_SUCCESS) goto complete;

    {
        uint64_t head;
        if (!AddChecked(request->KernelBase,
                        profile->PsActiveProcessHeadRva, &head) ||
            !Canonical48(head)) {
            status = HV_EFI_RUNTIME_PROCESS_PROFILE_MISMATCH;
            goto complete;
        }
        uint64_t current;
        uint64_t ignoredBlink;
        status = ReadLink(walker, head, &current, &ignoredBlink);
        if (status != HV_EFI_RUNTIME_PROCESS_SUCCESS) goto complete;
        uint64_t cycleAnchor = current;
        uint32_t cyclePower = 1;
        uint32_t cycleLength = 0;

        for (uint32_t iteration = 0;
             iteration < HV_EFI_RUNTIME_PROCESS_MAX_ITERATIONS;
             ++iteration) {
            if (current == head) {
                status = HV_EFI_RUNTIME_PROCESS_NOT_FOUND;
                break;
            }
            if (current < profile->EprocessActiveProcessLinks) {
                status = HV_EFI_RUNTIME_PROCESS_CORRUPT_LIST;
                break;
            }
            const uint64_t eprocess =
                current - profile->EprocessActiveProcessLinks;
            if (!Canonical48(eprocess) || (eprocess & 7) != 0) {
                status = HV_EFI_RUNTIME_PROCESS_CORRUPT_LIST;
                break;
            }
            HV_EFI_GUEST_PROCESS_RECORD record{};
            record.EprocessVirtualAddress = eprocess;
            status = ReadGuest(
                walker, eprocess, profile->EprocessUniqueProcessId,
                &record.ProcessId, sizeof(record.ProcessId));
            if (status != HV_EFI_RUNTIME_PROCESS_SUCCESS) break;
            status = ReadGuest(
                walker, eprocess, profile->EprocessImageFileName,
                record.ImageFileName, profile->EprocessImageFileNameSize);
            if (status != HV_EFI_RUNTIME_PROCESS_SUCCESS) break;
            record.ImageFileName[15] = '\0';
            status = ReadGuest(
                walker, eprocess, profile->EprocessDirectoryTableBase,
                &record.DirectoryTableBase,
                sizeof(record.DirectoryTableBase));
            if (status != HV_EFI_RUNTIME_PROCESS_SUCCESS) break;
            record.DirectoryTableBase &= walker->PhysicalMask & ~kPageMask;
            if (record.DirectoryTableBase == 0) {
                status = HV_EFI_RUNTIME_PROCESS_CORRUPT_LIST;
                break;
            }
            ++state->EnumeratedCount;
            StoreRecord(state, &record);
            if (NameMatches(record.ImageFileName, request)) {
                response->ProcessId = record.ProcessId;
                response->DirectoryTableBase = record.DirectoryTableBase;
                response->EprocessVirtualAddress = eprocess;
                status = HV_EFI_RUNTIME_PROCESS_SUCCESS;
                break;
            }

            uint64_t next;
            status = ReadLink(walker, current, &next, &ignoredBlink);
            if (status != HV_EFI_RUNTIME_PROCESS_SUCCESS) break;
            ++cycleLength;
            if (next != head && next == cycleAnchor) {
                status = HV_EFI_RUNTIME_PROCESS_CYCLE_DETECTED;
                break;
            }
            if (cycleLength == cyclePower) {
                cycleAnchor = next;
                cycleLength = 0;
                if (cyclePower <= HV_EFI_RUNTIME_PROCESS_MAX_ITERATIONS / 2)
                    cyclePower *= 2;
            }
            current = next;
            if (iteration + 1 == HV_EFI_RUNTIME_PROCESS_MAX_ITERATIONS)
                status = HV_EFI_RUNTIME_PROCESS_ITERATION_LIMIT;
        }
    }

complete:
    response->Status = status;
    response->EnumeratedCount = state->EnumeratedCount;
    response->StoredCount = state->StoredCount;
    if (state->Truncated != 0)
        response->Flags |= HV_EFI_RUNTIME_PROCESS_RESPONSE_TRUNCATED;
    return status;
}

extern "C" HV_EFI_RUNTIME_PROCESS_STATUS HvEfiRuntimeProcessExecute(
    HV_EFI_RUNTIME_PROCESS_STATE *state,
    HV_EFI_SCANNER_WINDOW *window,
    const HV_EFI_SCANNER_RAM_RANGE *allowedRanges,
    uint32_t allowedRangeCount,
    const hypersinv::windows::KernelProfile *profile,
    uint64_t generation,
    uint64_t guestCr3,
    uint64_t guestCr4,
    uint32_t maxPhysicalAddressBits,
    const HV_EFI_RUNTIME_PROCESS_REQUEST *request,
    HV_EFI_RUNTIME_PROCESS_RESPONSE *response)
{
    InitializeResponse(generation, request, response);
    if (state == nullptr || window == nullptr || allowedRanges == nullptr ||
        allowedRangeCount == 0 || !ProfileValid(profile) || generation == 0 ||
        state->Marker != HV_EFI_RUNTIME_PROCESS_STATE_MARKER ||
        response == nullptr || !RequestValid(request)) {
        return profile == nullptr ? HV_EFI_RUNTIME_PROCESS_PROFILE_UNAVAILABLE
                                  : HV_EFI_RUNTIME_PROCESS_INVALID_REQUEST;
    }
    if (request->BuildNumber != profile->BuildNumber ||
        request->Ubr != profile->Ubr) {
        response->Status = HV_EFI_RUNTIME_PROCESS_PROFILE_MISMATCH;
        return HV_EFI_RUNTIME_PROCESS_PROFILE_MISMATCH;
    }
    if (!TryAcquire(&state->Busy)) {
        response->Status = HV_EFI_RUNTIME_PROCESS_BUSY;
        return HV_EFI_RUNTIME_PROCESS_BUSY;
    }
    HV_EFI_RUNTIME_PROCESS_STATUS status = HV_EFI_RUNTIME_PROCESS_SUCCESS;
    if (request->Sequence <= state->LastSequence) {
        status = HV_EFI_RUNTIME_PROCESS_REPLAY_REJECTED;
        goto complete;
    }
    state->StoredCount = 0;
    state->EnumeratedCount = 0;
    state->Truncated = 0;
    ZeroBytes(state->Records, sizeof(state->Records));

    {
        HV_EFI_GUEST_SCANNER_READER scannerReader{};
        HV_EFI_GUEST_PT_WALKER walker{};
        const HV_EFI_GUEST_PT_STATUS initialized =
            HvEfiGuestPtWalkerInitializeScanner(
                window, allowedRanges, allowedRangeCount, guestCr3, guestCr4,
                maxPhysicalAddressBits, &scannerReader, &walker);
        if (initialized != HV_EFI_GUEST_PT_OK) {
            status = MapWalker(initialized);
            goto complete;
        }
        status = HvEfiGuestProcessLocateWithWalker(
            state, &walker, profile, generation, request, response);
    }

complete:
    response->Status = status;
    response->EnumeratedCount = state->EnumeratedCount;
    response->StoredCount = state->StoredCount;
    if (state->Truncated != 0)
        response->Flags |= HV_EFI_RUNTIME_PROCESS_RESPONSE_TRUNCATED;
    state->LastSequence = request != nullptr ? request->Sequence
                                              : state->LastSequence;
    Release(&state->Busy);
    return status;
}

extern "C" uint64_t HvEfiRuntimeProcessResidentCommand(
    HV_EFI_CONTEXT *context,
    uint64_t mailboxPhysical,
    uint64_t mailboxBytes,
    uint64_t expectedSequence)
{
    if (context == nullptr || mailboxPhysical == 0 || expectedSequence == 0 ||
        mailboxBytes < HVR1_MAILBOX_HEADER_BYTES +
            sizeof(HV_EFI_RUNTIME_PROCESS_MAILBOX) ||
        mailboxBytes > HVR1_MAILBOX_BYTES ||
        context->RuntimeScannerStatePhysical == 0 ||
        context->RuntimeScannerRamRangesPhysical == 0 ||
        context->RuntimeScannerRamRangeCount == 0) {
        return HV_EFI_RUNTIME_PROCESS_INVALID_REQUEST;
    }

    HV_EFI_SCANNER_WINDOW window{};
    HV_EFI_SCANNER_STATUS scannerStatus = HvEfiScannerInitializeNativeWindow(
        reinterpret_cast<uint64_t *>(
            static_cast<uintptr_t>(context->PageTablePhysical)),
        HV_EFI_PAGE_TABLE_PAGES, context->PageTablePhysical, &window);
    if (scannerStatus != HV_EFI_SCANNER_OK)
        return HV_EFI_RUNTIME_PROCESS_MAPPING_FAILED;

    const auto *ranges =
        reinterpret_cast<const HV_EFI_SCANNER_RAM_RANGE *>(
            static_cast<uintptr_t>(context->RuntimeScannerRamRangesPhysical));
    const uint64_t commandBytes = HVR1_MAILBOX_HEADER_BYTES +
        sizeof(HV_EFI_RUNTIME_PROCESS_MAILBOX);
    HV_EFI_SCANNER_MAPPING mapping{};
    scannerStatus = HvEfiScannerMapReadOnly(
        &window, ranges, context->RuntimeScannerRamRangeCount,
        mailboxPhysical, commandBytes, &mapping);
    if (scannerStatus != HV_EFI_SCANNER_OK)
        return HV_EFI_RUNTIME_PROCESS_MAPPING_FAILED;

    HV_EFI_RUNTIME_PROCESS_MAILBOX local{};
    const auto *outer = reinterpret_cast<const HVR1_MAILBOX *>(mapping.View);
    const bool outerValid =
        outer->Magic == HVR1_PROTOCOL_MAGIC &&
        outer->Version == HVR1_PROTOCOL_VERSION &&
        outer->HeaderSize == HVR1_MAILBOX_HEADER_BYTES &&
        outer->TotalSize == commandBytes &&
        outer->Operation == HVR1_MAILBOX_OPERATION_PROCESS_LOCATE &&
        outer->Flags == 0 && outer->Generation == context->RuntimeGeneration &&
        outer->Sequence == expectedSequence &&
        outer->PayloadOffset == HVR1_MAILBOX_HEADER_BYTES &&
        outer->PayloadSize == sizeof(local) && outer->Status == 0 &&
        outer->Reserved == 0;
    if (outerValid) {
        CopyBytes(&local,
                  mapping.View + HVR1_MAILBOX_PAYLOAD_DATA_OFFSET,
                  sizeof(local));
    }
    scannerStatus = HvEfiScannerUnmap(&window, &mapping);
    if (scannerStatus != HV_EFI_SCANNER_OK)
        return HV_EFI_RUNTIME_PROCESS_MAPPING_FAILED;

    HV_EFI_RUNTIME_PROCESS_STATUS status =
        HV_EFI_RUNTIME_PROCESS_INVALID_REQUEST;
    if (outerValid && local.Magic == HV_EFI_RUNTIME_PROCESS_MAGIC &&
        local.Version == HV_EFI_RUNTIME_PROCESS_VERSION &&
        local.HeaderSize == offsetof(HV_EFI_RUNTIME_PROCESS_MAILBOX, Request) &&
        local.State == HV_EFI_RUNTIME_PROCESS_MAILBOX_REQUEST_READY &&
        local.Reserved == 0 &&
        local.Generation == context->RuntimeGeneration &&
        local.Request.Sequence == expectedSequence) {
        local.State = HV_EFI_RUNTIME_PROCESS_MAILBOX_PROCESSING;
        auto *state = reinterpret_cast<HV_EFI_RUNTIME_PROCESS_STATE *>(
            static_cast<uintptr_t>(context->RuntimeScannerStatePhysical +
                HV_EFI_RUNTIME_PROCESS_STATE_PAGE_OFFSET));
#if HYPERSINV_KERNEL_PROFILE_AVAILABLE
        const hypersinv::windows::KernelProfile *profile =
            &hypersinv::windows::kKernelProfile;
#else
        const hypersinv::windows::KernelProfile *profile = nullptr;
#endif
        status = HvEfiRuntimeProcessExecute(
            state, &window, ranges, context->RuntimeScannerRamRangeCount,
            profile, context->RuntimeGeneration, context->GuestCr3,
            context->GuestCr4, NativeMaxPhysicalAddressBits(),
            &local.Request, &local.Response);
        local.State = HV_EFI_RUNTIME_PROCESS_MAILBOX_RESPONSE_READY;
    }

    ZeroBytes(&mapping, sizeof(mapping));
    scannerStatus = HvEfiScannerMapReadOnly(
        &window, ranges, context->RuntimeScannerRamRangeCount,
        mailboxPhysical, commandBytes, &mapping);
    if (scannerStatus != HV_EFI_SCANNER_OK) {
        ZeroBytes(&local, sizeof(local));
        return HV_EFI_RUNTIME_PROCESS_MAPPING_FAILED;
    }
    SetWritable(&window, &mapping, true);
    CopyBytes(const_cast<uint8_t *>(mapping.View) +
                  HVR1_MAILBOX_PAYLOAD_DATA_OFFSET,
              &local, sizeof(local));
    SetWritable(&window, &mapping, false);
    scannerStatus = HvEfiScannerUnmap(&window, &mapping);
    ZeroBytes(&local, sizeof(local));
    return scannerStatus == HV_EFI_SCANNER_OK
        ? status : HV_EFI_RUNTIME_PROCESS_MAPPING_FAILED;
}

#if defined(_MSC_VER)
#pragma code_seg(pop)
#endif
