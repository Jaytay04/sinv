#include "runtime_analysis.h"

#include "../driver/efi_runtime_companion/hvr1_protocol.h"
#include "efi_hypervisor.h"

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(__cpuid)
#pragma intrinsic(__rdtsc)
#pragma code_seg(push, ".hvscan$m")
#endif

namespace {

constexpr uint64_t kWritable = UINT64_C(2);
constexpr uint64_t kPageMask = UINT64_C(4095);
constexpr uint64_t kMinimumUserAddress = UINT64_C(0x10000);
constexpr uint64_t kMaximumUserAddress = UINT64_C(0x00007fffffffffff);
constexpr uint64_t kDefaultTimeoutTsc = UINT64_C(2000000000);

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

uint64_t ReadTsc()
{
#if defined(_MSC_VER)
    return __rdtsc();
#else
    uint32_t low;
    uint32_t high;
    __asm__ __volatile__("rdtsc" : "=a"(low), "=d"(high));
    return (static_cast<uint64_t>(high) << 32) | low;
#endif
}

bool AddChecked(uint64_t left, uint64_t right, uint64_t *sum)
{
    if (sum == nullptr || left > UINT64_MAX - right) return false;
    *sum = left + right;
    return true;
}

bool MulChecked(uint64_t left, uint64_t right, uint64_t *product)
{
    if (product == nullptr || (left != 0 && right > UINT64_MAX / left))
        return false;
    *product = left * right;
    return true;
}

bool UserAddress(uint64_t address)
{
    return address >= kMinimumUserAddress && address <= kMaximumUserAddress;
}

bool UserRange(uint64_t address, uint64_t size)
{
    uint64_t end;
    return size != 0 && UserAddress(address) &&
        AddChecked(address, size - 1, &end) && UserAddress(end);
}

bool FixedStringValid(const char *text, uint32_t bytes, bool requireNonempty)
{
    if (text == nullptr || bytes == 0) return false;
    bool terminated = false;
    bool nonempty = false;
    for (uint32_t index = 0; index < bytes; ++index) {
        const uint8_t value = static_cast<uint8_t>(text[index]);
        if (terminated) {
            if (value != 0) return false;
        } else if (value == 0) {
            terminated = true;
        } else {
            if (value < 0x20 || value > 0x7e) return false;
            nonempty = true;
        }
    }
    return terminated && (!requireNonempty || nonempty);
}

bool SignatureValid(const HV_EFI_ANALYSIS_MEMORY_SIGNATURE *signature)
{
    if (signature == nullptr ||
        !FixedStringValid(signature->Identifier,
                          HV_EFI_ANALYSIS_IDENTIFIER_BYTES, true) ||
        signature->Length == 0 ||
        signature->Length > HV_EFI_ANALYSIS_PATTERN_BYTES ||
        (signature->Flags & ~HV_EFI_ANALYSIS_SIGNATURE_FLAGS_ALL) != 0 ||
        (signature->Flags & HV_EFI_ANALYSIS_SIGNATURE_ALIGN_4) != 0 &&
        (signature->Flags & HV_EFI_ANALYSIS_SIGNATURE_ALIGN_8) != 0 ||
        signature->Reserved != 0) {
        return false;
    }
    bool exact = false;
    for (uint32_t index = 0; index < signature->Length; ++index) {
        if (signature->Mask[index] != 0) exact = true;
    }
    for (uint32_t index = signature->Length;
         index < HV_EFI_ANALYSIS_PATTERN_BYTES; ++index) {
        if (signature->Pattern[index] != 0 || signature->Mask[index] != 0)
            return false;
    }
    return exact;
}

bool RegionValid(const HV_EFI_ANALYSIS_MEMORY_REGION *region,
                 uint64_t processId)
{
    return region != nullptr && UserRange(region->BaseVirtualAddress,
                                           region->Size) &&
        (region->ProcessId == 0 || region->ProcessId == processId) &&
        region->RegionType <= HV_EFI_ANALYSIS_REGION_PRIVATE &&
        (region->Protection & ~HV_EFI_ANALYSIS_PROTECTION_ALL) == 0 &&
        region->Reserved == 0;
}

bool RequestValid(const HV_EFI_ANALYSIS_REQUEST *request)
{
    if (request == nullptr || request->Size != sizeof(*request) ||
        request->Version != HV_EFI_ANALYSIS_VERSION ||
        request->Operation < HV_EFI_ANALYSIS_START ||
        request->Operation > HV_EFI_ANALYSIS_STOP ||
        request->Sequence == 0 ||
        request->SignatureCount > HV_EFI_ANALYSIS_MAX_SIGNATURES ||
        request->RegionCount > HV_EFI_ANALYSIS_MAX_CONFIG_REGIONS ||
        request->ResultLimit > HV_EFI_ANALYSIS_MAX_RESULTS ||
        request->NodeLimit > HV_EFI_ANALYSIS_MAX_NODES ||
        (request->RegionFilter & ~HV_EFI_ANALYSIS_REGION_FILTER_ALL) != 0 ||
        request->MaxBytes > HV_EFI_ANALYSIS_MAX_SCAN_BYTES ||
        request->TimeoutTsc > HV_EFI_ANALYSIS_MAX_TIMEOUT_TSC ||
        request->Flags != 0 ||
        !FixedStringValid(request->FieldName,
                          HV_EFI_ANALYSIS_IDENTIFIER_BYTES, false)) {
        return false;
    }
    if (request->Operation == HV_EFI_ANALYSIS_START &&
        (request->ProcessId == 0 || request->ProcessId == 4 ||
         request->ProcessId > UINT32_MAX)) return false;
    if (request->Operation == HV_EFI_ANALYSIS_CONFIGURE ||
        request->Operation == HV_EFI_ANALYSIS_START) {
        for (uint32_t index = 0; index < request->SignatureCount; ++index) {
            if (!SignatureValid(&request->Signatures[index])) return false;
        }
    }
    for (uint32_t index = 0; index < request->RegionCount; ++index) {
        const uint64_t processId = request->Operation == HV_EFI_ANALYSIS_START
            ? request->ProcessId : request->Regions[index].ProcessId;
        if (!RegionValid(&request->Regions[index],
                         processId))
            return false;
    }
    if (request->Operation == HV_EFI_ANALYSIS_SCAN &&
        (request->ScanMode < HV_EFI_ANALYSIS_SCAN_FIRST ||
         request->ScanMode > HV_EFI_ANALYSIS_SCAN_ALL)) return false;
    if (request->Operation == HV_EFI_ANALYSIS_TRAVERSE &&
        (request->StructureType < HV_EFI_ANALYSIS_TRAVERSE_SINGLY_LINKED ||
         request->StructureType > HV_EFI_ANALYSIS_TRAVERSE_GRAPH_BINARY ||
         !UserAddress(request->Address))) return false;
    if (request->Operation == HV_EFI_ANALYSIS_EXTRACT &&
        (request->FieldType < HV_EFI_ANALYSIS_FIELD_FLOAT32 ||
         request->FieldType > HV_EFI_ANALYSIS_FIELD_LENGTH_PREFIXED32 ||
         !UserAddress(request->Address))) return false;
    return true;
}

HV_EFI_ANALYSIS_STATUS MapWalk(HV_EFI_GUEST_PT_STATUS status)
{
    switch (status) {
    case HV_EFI_GUEST_PT_OK:
        return HV_EFI_ANALYSIS_SUCCESS;
    case HV_EFI_GUEST_PT_PHYSICAL_RANGE:
        return HV_EFI_ANALYSIS_RANGE_DENIED;
    case HV_EFI_GUEST_PT_MAPPING_FAILED:
    case HV_EFI_GUEST_PT_READ_FAILED:
        return HV_EFI_ANALYSIS_MAPPING_FAILED;
    case HV_EFI_GUEST_PT_LIMIT_REACHED:
        return HV_EFI_ANALYSIS_LIMIT_REACHED;
    case HV_EFI_GUEST_PT_TABLE_CHANGED:
        return HV_EFI_ANALYSIS_CORRUPT_STRUCTURE;
    default:
        return HV_EFI_ANALYSIS_TRANSLATION_FAILED;
    }
}

uint32_t ToProtection(uint32_t access)
{
    uint32_t protection = HV_EFI_ANALYSIS_PROTECTION_COMMITTED;
    if ((access & HV_EFI_GUEST_PT_ACCESS_READ) != 0)
        protection |= HV_EFI_ANALYSIS_PROTECTION_READ;
    if ((access & HV_EFI_GUEST_PT_ACCESS_WRITE) != 0)
        protection |= HV_EFI_ANALYSIS_PROTECTION_WRITE;
    if ((access & HV_EFI_GUEST_PT_ACCESS_EXECUTE) != 0)
        protection |= HV_EFI_ANALYSIS_PROTECTION_EXECUTE;
    if ((access & HV_EFI_GUEST_PT_ACCESS_USER) != 0)
        protection |= HV_EFI_ANALYSIS_PROTECTION_USER;
    return protection;
}

const HV_EFI_GUEST_PROCESS_RECORD *FindProcess(
    const HV_EFI_RUNTIME_PROCESS_STATE *processState, uint64_t processId)
{
    if (processState == nullptr ||
        processState->Marker != HV_EFI_RUNTIME_PROCESS_STATE_MARKER)
        return nullptr;
    for (uint32_t index = 0; index < processState->StoredCount; ++index) {
        if (processState->Records[index].ProcessId == processId)
            return &processState->Records[index];
    }
    return nullptr;
}

bool LooksLikePeImage(const HV_EFI_GUEST_PT_WALKER *walker,
                      uint64_t address, uint64_t *imageSize)
{
    uint8_t dos[64];
    if (HvEfiGuestPtRead(walker, address, dos, sizeof(dos)) !=
            HV_EFI_GUEST_PT_OK || dos[0] != 'M' || dos[1] != 'Z') return false;
    const uint32_t peOffset = static_cast<uint32_t>(dos[0x3c]) |
        static_cast<uint32_t>(dos[0x3d]) << 8 |
        static_cast<uint32_t>(dos[0x3e]) << 16 |
        static_cast<uint32_t>(dos[0x3f]) << 24;
    if (peOffset < sizeof(dos) || peOffset > UINT32_C(0x100000)) return false;
    uint64_t peAddress;
    if (!AddChecked(address, peOffset, &peAddress)) return false;
    uint8_t pe[88];
    if (HvEfiGuestPtRead(walker, peAddress, pe, sizeof(pe)) !=
            HV_EFI_GUEST_PT_OK || pe[0] != 'P' || pe[1] != 'E' ||
        pe[2] != 0 || pe[3] != 0 || pe[24] != 0x0b || pe[25] != 0x02)
        return false;
    const uint32_t size = static_cast<uint32_t>(pe[80]) |
        static_cast<uint32_t>(pe[81]) << 8 |
        static_cast<uint32_t>(pe[82]) << 16 |
        static_cast<uint32_t>(pe[83]) << 24;
    if (size == 0 || !UserRange(address, size)) return false;
    *imageSize = size;
    return true;
}

void ClassifyImages(HV_EFI_ANALYSIS_STATE *state,
    const HV_EFI_GUEST_PT_WALKER *walker)
{
    for (uint32_t index = 0; index < state->RegionCount; ++index) {
        /* PE header pages are commonly read-only and NX.  Requiring execute
         * permission here silently misses normal Windows image mappings. */
        uint64_t imageSize = 0;
        if (!LooksLikePeImage(walker,
                              state->Regions[index].BaseVirtualAddress,
                              &imageSize)) continue;
        uint64_t imageEnd;
        if (!AddChecked(state->Regions[index].BaseVirtualAddress,
                        imageSize, &imageEnd)) continue;
        for (uint32_t candidate = index;
             candidate < state->RegionCount; ++candidate) {
            const uint64_t base =
                state->Regions[candidate].BaseVirtualAddress;
            if (base >= imageEnd) break;
            state->Regions[candidate].RegionType =
                HV_EFI_ANALYSIS_REGION_IMAGE;
        }
    }
}

HV_EFI_ANALYSIS_STATUS DiscoverRegions(
    HV_EFI_ANALYSIS_STATE *state,
    const HV_EFI_GUEST_PT_WALKER *walker)
{
    auto *ranges = reinterpret_cast<HV_EFI_GUEST_PT_RANGE *>(
        state->Regions);
    uint32_t count = 0;
    uint32_t pages = 0;
    uint32_t truncated = 0;
    const HV_EFI_GUEST_PT_STATUS status = HvEfiGuestPtEnumerateUserRanges(
        walker, ranges, HV_EFI_ANALYSIS_MAX_REGIONS,
        HV_EFI_ANALYSIS_MAX_PT_PAGES, &count, &pages, &truncated);
    if (status != HV_EFI_GUEST_PT_OK &&
        status != HV_EFI_GUEST_PT_LIMIT_REACHED) return MapWalk(status);
    state->RegionsTruncated = truncated;
    uint32_t outputCount = 0;
    for (uint32_t index = 0; index < count; ++index) {
        const HV_EFI_GUEST_PT_RANGE range = ranges[index];
        if ((range.Access & HV_EFI_GUEST_PT_ACCESS_USER) == 0) continue;
        state->Regions[outputCount].BaseVirtualAddress = range.VirtualAddress;
        state->Regions[outputCount].Size = range.Size;
        state->Regions[outputCount].ProcessId =
            static_cast<uint32_t>(state->ProcessId);
        state->Regions[outputCount].RegionType =
            HV_EFI_ANALYSIS_REGION_UNKNOWN;
        state->Regions[outputCount].Protection = ToProtection(range.Access);
        state->Regions[outputCount].Reserved = 0;
        ++outputCount;
    }
    state->RegionCount = outputCount;
    ClassifyImages(state, walker);
    return status == HV_EFI_GUEST_PT_LIMIT_REACHED
        ? HV_EFI_ANALYSIS_MORE_DATA : HV_EFI_ANALYSIS_SUCCESS;
}

void ResetFindings(HV_EFI_ANALYSIS_STATE *state)
{
    state->ScannedBytes = 0;
    state->ResultCount = 0;
    state->NodeCount = 0;
    state->FieldCount = 0;
    ZeroBytes(state->Results, sizeof(state->Results));
    ZeroBytes(state->Nodes, sizeof(state->Nodes));
    ZeroBytes(state->Fields, sizeof(state->Fields));
}

void ResetSession(HV_EFI_ANALYSIS_STATE *state)
{
    state->Active = 0;
    state->Generation = 0;
    state->ProcessId = 0;
    state->TargetCr3 = 0;
    state->RegionFilter = 0;
    state->SignatureCount = 0;
    state->RegionCount = 0;
    state->RegionsTruncated = 0;
    ResetFindings(state);
    ZeroBytes(state->Signatures, sizeof(state->Signatures));
    ZeroBytes(state->Regions, sizeof(state->Regions));
}

bool RegionOverlaps(const HV_EFI_ANALYSIS_MEMORY_REGION &left,
                    const HV_EFI_ANALYSIS_MEMORY_REGION &right)
{
    uint64_t leftEnd;
    uint64_t rightEnd;
    return AddChecked(left.BaseVirtualAddress, left.Size, &leftEnd) &&
        AddChecked(right.BaseVirtualAddress, right.Size, &rightEnd) &&
        left.BaseVirtualAddress < rightEnd &&
        right.BaseVirtualAddress < leftEnd;
}

HV_EFI_ANALYSIS_STATUS Configure(
    HV_EFI_ANALYSIS_STATE *state,
    const HV_EFI_ANALYSIS_REQUEST *request)
{
    for (uint32_t index = 0; index < request->RegionCount; ++index) {
        if (request->Regions[index].ProcessId != 0 &&
            request->Regions[index].ProcessId != state->ProcessId)
            return HV_EFI_ANALYSIS_WRONG_PROCESS;
    }
    state->SignatureCount = request->SignatureCount;
    ZeroBytes(state->Signatures, sizeof(state->Signatures));
    CopyBytes(state->Signatures, request->Signatures,
              sizeof(state->Signatures[0]) * request->SignatureCount);
    state->RegionFilter = request->RegionFilter == 0
        ? HV_EFI_ANALYSIS_REGION_FILTER_ALL : request->RegionFilter;

    /* Region type is evidence supplied by the authenticated kernel agent.
     * Access permissions always remain derived from guest page tables. */
    for (uint32_t supplied = 0; supplied < request->RegionCount; ++supplied) {
        bool overlapped = false;
        for (uint32_t index = 0; index < state->RegionCount; ++index) {
            if (RegionOverlaps(state->Regions[index],
                               request->Regions[supplied])) {
                overlapped = true;
                state->Regions[index].RegionType =
                    request->Regions[supplied].RegionType;
                state->Regions[index].Protection |=
                    request->Regions[supplied].Protection &
                    HV_EFI_ANALYSIS_PROTECTION_GUARD;
            }
        }
        if (!overlapped &&
            (request->Regions[supplied].Protection &
             HV_EFI_ANALYSIS_PROTECTION_RESERVED) != 0) {
            if (state->RegionCount >= HV_EFI_ANALYSIS_MAX_REGIONS) {
                state->RegionsTruncated = 1;
                continue;
            }
            HV_EFI_ANALYSIS_MEMORY_REGION region = request->Regions[supplied];
            region.ProcessId = static_cast<uint32_t>(state->ProcessId);
            region.Protection &= HV_EFI_ANALYSIS_PROTECTION_RESERVED |
                HV_EFI_ANALYSIS_PROTECTION_GUARD;
            state->Regions[state->RegionCount++] = region;
        }
    }
    ResetFindings(state);
    return HV_EFI_ANALYSIS_SUCCESS;
}

bool SignatureAllowsRegion(
    const HV_EFI_ANALYSIS_MEMORY_SIGNATURE &signature,
    const HV_EFI_ANALYSIS_MEMORY_REGION &region,
    uint32_t regionFilter)
{
    if ((region.Protection & HV_EFI_ANALYSIS_PROTECTION_COMMITTED) == 0)
        return false;
    if ((regionFilter & HV_EFI_ANALYSIS_REGION_FILTER(region.RegionType)) == 0)
        return false;
    if ((signature.Flags & HV_EFI_ANALYSIS_SIGNATURE_READABLE) != 0 &&
        (region.Protection & HV_EFI_ANALYSIS_PROTECTION_READ) == 0)
        return false;
    if ((signature.Flags & HV_EFI_ANALYSIS_SIGNATURE_WRITABLE) != 0 &&
        (region.Protection & HV_EFI_ANALYSIS_PROTECTION_WRITE) == 0)
        return false;
    if ((signature.Flags & HV_EFI_ANALYSIS_SIGNATURE_EXECUTABLE) != 0 &&
        (region.Protection & HV_EFI_ANALYSIS_PROTECTION_EXECUTE) == 0)
        return false;
    return true;
}

uint32_t Confidence(const HV_EFI_ANALYSIS_MEMORY_SIGNATURE &signature)
{
    uint32_t exact = 0;
    for (uint32_t index = 0; index < signature.Length; ++index)
        if (signature.Mask[index] != 0) ++exact;
    return 50u + (exact * 50u) / signature.Length;
}

bool AlignmentMatches(const HV_EFI_ANALYSIS_MEMORY_SIGNATURE &signature,
                      uint64_t address)
{
    if ((signature.Flags & HV_EFI_ANALYSIS_SIGNATURE_ALIGN_8) != 0)
        return (address & 7) == 0;
    if ((signature.Flags & HV_EFI_ANALYSIS_SIGNATURE_ALIGN_4) != 0)
        return (address & 3) == 0;
    return true;
}

HV_EFI_ANALYSIS_STATUS StoreMatch(
    HV_EFI_ANALYSIS_STATE *state,
    const HV_EFI_GUEST_PT_WALKER *walker,
    const HV_EFI_ANALYSIS_MEMORY_SIGNATURE &signature,
    uint64_t matchAddress,
    uint32_t limit)
{
    uint64_t resolved;
    if (signature.DataOffset >= 0) {
        if (!AddChecked(matchAddress,
                        static_cast<uint32_t>(signature.DataOffset),
                        &resolved)) return HV_EFI_ANALYSIS_RANGE_DENIED;
    } else {
        const uint64_t magnitude = static_cast<uint64_t>(
            -static_cast<int64_t>(signature.DataOffset));
        if (matchAddress < magnitude) return HV_EFI_ANALYSIS_RANGE_DENIED;
        resolved = matchAddress - magnitude;
    }
    if (!UserAddress(resolved)) return HV_EFI_ANALYSIS_RANGE_DENIED;
    HV_EFI_GUEST_PT_TRANSLATION translation{};
    HV_EFI_GUEST_PT_STATUS walk =
        HvEfiGuestPtTranslateEx(walker, resolved, &translation);
    if (walk != HV_EFI_GUEST_PT_OK) return MapWalk(walk);
    if (state->ResultCount >= limit ||
        state->ResultCount >= HV_EFI_ANALYSIS_MAX_RESULTS)
        return HV_EFI_ANALYSIS_LIMIT_REACHED;
    HV_EFI_ANALYSIS_SCAN_RESULT *result =
        &state->Results[state->ResultCount++];
    result->GuestVirtualAddress = resolved;
    result->HostPhysicalAddress = translation.PhysicalAddress;
    result->OwningProcessId = static_cast<uint32_t>(state->ProcessId);
    CopyBytes(result->MatchedSignature, signature.Identifier,
              sizeof(result->MatchedSignature));
    result->MatchConfidence = Confidence(signature);
    result->Reserved = 0;
    return HV_EFI_ANALYSIS_SUCCESS;
}

HV_EFI_ANALYSIS_STATUS ScanRegion(
    HV_EFI_ANALYSIS_STATE *state,
    const HV_EFI_GUEST_PT_WALKER *walker,
    const HV_EFI_ANALYSIS_MEMORY_REGION &region,
    const HV_EFI_ANALYSIS_MEMORY_SIGNATURE &signature,
    uint32_t mode,
    uint32_t limit,
    uint64_t byteLimit,
    uint64_t startTsc,
    uint64_t timeoutTsc)
{
    HV_EFI_SCANNER_PATTERN_BYTE patternBytes[HV_EFI_ANALYSIS_PATTERN_BYTES];
    HV_EFI_SCANNER_COMPILED_PATTERN pattern;
    ZeroBytes(&pattern, sizeof(pattern));
    if (HvEfiScannerCompileMaskedPattern(
            signature.Pattern, signature.Mask, signature.Length,
            patternBytes, HV_EFI_ANALYSIS_PATTERN_BYTES, &pattern) !=
            HV_EFI_SCANNER_OK) return HV_EFI_ANALYSIS_INVALID_REQUEST;

    uint8_t buffer[2048 + HV_EFI_ANALYSIS_PATTERN_BYTES - 1];
    uint64_t cursor = region.BaseVirtualAddress;
    uint64_t regionEnd;
    if (!AddChecked(region.BaseVirtualAddress, region.Size, &regionEnd))
        return HV_EFI_ANALYSIS_RANGE_DENIED;
    while (cursor < regionEnd) {
        if (ReadTsc() - startTsc > timeoutTsc)
            return HV_EFI_ANALYSIS_TIMEOUT;
        if (state->ScannedBytes >= byteLimit)
            return HV_EFI_ANALYSIS_MORE_DATA;
        const uint64_t remaining = regionEnd - cursor;
        const uint32_t primary = remaining < 2048
            ? static_cast<uint32_t>(remaining) : 2048u;
        uint32_t readBytes = primary;
        const uint64_t overlap = signature.Length - 1;
        if (remaining > primary) {
            const uint64_t tail = remaining - primary < overlap
                ? remaining - primary : overlap;
            readBytes += static_cast<uint32_t>(tail);
        }
        if (state->ScannedBytes > byteLimit - primary)
            return HV_EFI_ANALYSIS_MORE_DATA;
        const HV_EFI_GUEST_PT_STATUS read =
            HvEfiGuestPtRead(walker, cursor, buffer, readBytes);
        if (read != HV_EFI_GUEST_PT_OK) return MapWalk(read);
        state->ScannedBytes += primary;

        uint64_t searchOffset = 0;
        while (searchOffset + signature.Length <= readBytes) {
            uint64_t found = 0;
            const HV_EFI_SCANNER_STATUS scan = HvEfiScannerFindFirst(
                buffer + searchOffset, readBytes - searchOffset,
                &pattern, &found);
            if (scan == HV_EFI_SCANNER_NOT_FOUND) break;
            if (scan != HV_EFI_SCANNER_OK)
                return HV_EFI_ANALYSIS_INVARIANT_VIOLATION;
            found += searchOffset;
            if (found >= primary) break;
            uint64_t match;
            if (!AddChecked(cursor, found, &match))
                return HV_EFI_ANALYSIS_RANGE_DENIED;
            if (AlignmentMatches(signature, match)) {
                HV_EFI_ANALYSIS_STATUS stored = StoreMatch(
                    state, walker, signature, match, limit);
                if (stored != HV_EFI_ANALYSIS_SUCCESS) return stored;
                if (mode == HV_EFI_ANALYSIS_SCAN_FIRST)
                    return HV_EFI_ANALYSIS_SUCCESS;
                if (mode == HV_EFI_ANALYSIS_SCAN_SINGLE &&
                    state->ResultCount > 1)
                    return HV_EFI_ANALYSIS_MULTIPLE_MATCHES;
            }
            searchOffset = found + 1;
        }
        cursor += primary;
    }
    return HV_EFI_ANALYSIS_SUCCESS;
}

HV_EFI_ANALYSIS_STATUS Scan(
    HV_EFI_ANALYSIS_STATE *state,
    const HV_EFI_GUEST_PT_WALKER *walker,
    const HV_EFI_ANALYSIS_REQUEST *request)
{
    if (state->SignatureCount == 0) return HV_EFI_ANALYSIS_INVALID_REQUEST;
    ResetFindings(state);
    const uint32_t limit = request->ResultLimit == 0
        ? HV_EFI_ANALYSIS_MAX_RESULTS : request->ResultLimit;
    const uint64_t byteLimit = request->MaxBytes == 0
        ? HV_EFI_ANALYSIS_DEFAULT_SCAN_BYTES : request->MaxBytes;
    const uint64_t timeout = request->TimeoutTsc == 0
        ? kDefaultTimeoutTsc : request->TimeoutTsc;
    const uint64_t startTsc = ReadTsc();
    for (uint32_t signatureIndex = 0;
         signatureIndex < state->SignatureCount; ++signatureIndex) {
        for (uint32_t regionIndex = 0;
             regionIndex < state->RegionCount; ++regionIndex) {
            const auto &signature = state->Signatures[signatureIndex];
            const auto &region = state->Regions[regionIndex];
            if (!SignatureAllowsRegion(
                    signature, region, state->RegionFilter)) continue;
            const HV_EFI_ANALYSIS_STATUS status = ScanRegion(
                state, walker, region, signature, request->ScanMode,
                limit, byteLimit, startTsc, timeout);
            if (status != HV_EFI_ANALYSIS_SUCCESS) return status;
            if (request->ScanMode == HV_EFI_ANALYSIS_SCAN_FIRST &&
                state->ResultCount != 0) return HV_EFI_ANALYSIS_SUCCESS;
        }
    }
    return state->ResultCount == 0
        ? HV_EFI_ANALYSIS_NOT_FOUND : HV_EFI_ANALYSIS_SUCCESS;
}

bool NodeSeen(const HV_EFI_ANALYSIS_STATE *state, uint64_t node)
{
    for (uint32_t index = 0; index < state->NodeCount; ++index)
        if (state->Nodes[index] == node) return true;
    return false;
}

HV_EFI_ANALYSIS_STATUS StoreNode(HV_EFI_ANALYSIS_STATE *state,
                                 uint64_t node, uint32_t limit)
{
    if (!UserAddress(node) || (node & 7) != 0)
        return HV_EFI_ANALYSIS_CORRUPT_STRUCTURE;
    if (NodeSeen(state, node)) return HV_EFI_ANALYSIS_CYCLE_DETECTED;
    if (state->NodeCount >= limit ||
        state->NodeCount >= HV_EFI_ANALYSIS_MAX_NODES)
        return HV_EFI_ANALYSIS_LIMIT_REACHED;
    state->Nodes[state->NodeCount++] = node;
    return HV_EFI_ANALYSIS_SUCCESS;
}

HV_EFI_ANALYSIS_STATUS ReadPointer(const HV_EFI_GUEST_PT_WALKER *walker,
                                   uint64_t base, uint32_t offset,
                                   uint64_t *pointer)
{
    uint64_t address;
    if (!AddChecked(base, offset, &address) || !UserRange(address, 8))
        return HV_EFI_ANALYSIS_CORRUPT_STRUCTURE;
    const HV_EFI_GUEST_PT_STATUS status =
        HvEfiGuestPtRead(walker, address, pointer, sizeof(*pointer));
    return status == HV_EFI_GUEST_PT_OK
        ? HV_EFI_ANALYSIS_SUCCESS : MapWalk(status);
}

HV_EFI_ANALYSIS_STATUS TraverseList(
    HV_EFI_ANALYSIS_STATE *state,
    const HV_EFI_GUEST_PT_WALKER *walker,
    const HV_EFI_ANALYSIS_REQUEST *request,
    uint32_t limit,
    bool doubly,
    bool backward)
{
    uint64_t current = request->Address;
    while (current != 0) {
        const HV_EFI_ANALYSIS_STATUS stored = StoreNode(state, current, limit);
        if (stored == HV_EFI_ANALYSIS_CYCLE_DETECTED &&
            current == request->Address) return HV_EFI_ANALYSIS_SUCCESS;
        if (stored != HV_EFI_ANALYSIS_SUCCESS) return stored;
        uint64_t next = 0;
        HV_EFI_ANALYSIS_STATUS status = ReadPointer(
            walker, current,
            backward ? request->PreviousOffset : request->NextOffset,
            &next);
        if (status != HV_EFI_ANALYSIS_SUCCESS) return status;
        if (next == 0) return HV_EFI_ANALYSIS_SUCCESS;
        if (doubly) {
            uint64_t backlink = 0;
            status = ReadPointer(
                walker, next,
                backward ? request->NextOffset : request->PreviousOffset,
                &backlink);
            if (status != HV_EFI_ANALYSIS_SUCCESS) return status;
            if (backlink != current)
                return HV_EFI_ANALYSIS_CORRUPT_STRUCTURE;
        }
        current = next;
    }
    return HV_EFI_ANALYSIS_SUCCESS;
}

HV_EFI_ANALYSIS_STATUS TraverseArray(
    HV_EFI_ANALYSIS_STATE *state,
    const HV_EFI_GUEST_PT_WALKER *walker,
    const HV_EFI_ANALYSIS_REQUEST *request,
    uint32_t limit)
{
    if (request->Stride == 0 || request->Stride > UINT32_C(0x100000))
        return HV_EFI_ANALYSIS_INVALID_REQUEST;
    uint64_t count = request->ElementCount;
    if (request->StructureType == HV_EFI_ANALYSIS_TRAVERSE_ARRAY_2D) {
        if (count == 0 || request->SecondaryCount == 0 ||
            !MulChecked(count, request->SecondaryCount, &count))
            return HV_EFI_ANALYSIS_INVALID_REQUEST;
    } else if (count == 0) {
        count = limit;
    }
    if (count > limit) count = limit;
    for (uint64_t index = 0; index < count; ++index) {
        uint64_t offset;
        uint64_t address;
        if (!MulChecked(index, request->Stride, &offset) ||
            !AddChecked(request->Address, offset, &address) ||
            !UserRange(address, request->Stride))
            return HV_EFI_ANALYSIS_RANGE_DENIED;
        if (request->StructureType ==
                HV_EFI_ANALYSIS_TRAVERSE_POINTER_ARRAY) {
            uint64_t pointer = 0;
            HV_EFI_ANALYSIS_STATUS status =
                ReadPointer(walker, address, 0, &pointer);
            if (status != HV_EFI_ANALYSIS_SUCCESS) return status;
            if (pointer == 0) return HV_EFI_ANALYSIS_SUCCESS;
            status = StoreNode(state, pointer, limit);
            if (status != HV_EFI_ANALYSIS_SUCCESS) return status;
        } else {
            if (request->ElementCount == 0) {
                uint64_t marker = 0;
                const HV_EFI_GUEST_PT_STATUS read = HvEfiGuestPtRead(
                    walker, address, &marker,
                    request->Stride < 8 ? request->Stride : 8);
                if (read != HV_EFI_GUEST_PT_OK) return MapWalk(read);
                if (marker == 0) return HV_EFI_ANALYSIS_SUCCESS;
            }
            const HV_EFI_ANALYSIS_STATUS status =
                StoreNode(state, address, limit);
            if (status != HV_EFI_ANALYSIS_SUCCESS) return status;
        }
    }
    return request->ElementCount > limit
        ? HV_EFI_ANALYSIS_LIMIT_REACHED : HV_EFI_ANALYSIS_SUCCESS;
}

HV_EFI_ANALYSIS_STATUS TraverseGraph(
    HV_EFI_ANALYSIS_STATE *state,
    const HV_EFI_GUEST_PT_WALKER *walker,
    const HV_EFI_ANALYSIS_REQUEST *request,
    uint32_t limit)
{
    uint64_t pending[HV_EFI_ANALYSIS_MAX_NODES];
    uint32_t pendingCount = 1;
    pending[0] = request->Address;
    while (pendingCount != 0) {
        const uint64_t node = pending[--pendingCount];
        if (node == 0 || NodeSeen(state, node)) continue;
        HV_EFI_ANALYSIS_STATUS status = StoreNode(state, node, limit);
        if (status != HV_EFI_ANALYSIS_SUCCESS) return status;
        uint64_t children[2]{};
        status = ReadPointer(walker, node, request->Child0Offset,
                             &children[0]);
        if (status != HV_EFI_ANALYSIS_SUCCESS) return status;
        status = ReadPointer(walker, node, request->Child1Offset,
                             &children[1]);
        if (status != HV_EFI_ANALYSIS_SUCCESS) return status;
        for (uint32_t index = 0; index < 2; ++index) {
            if (children[index] == 0 || NodeSeen(state, children[index]))
                continue;
            if (!UserAddress(children[index]) ||
                (children[index] & 7) != 0)
                return HV_EFI_ANALYSIS_CORRUPT_STRUCTURE;
            if (pendingCount >= limit ||
                pendingCount >= HV_EFI_ANALYSIS_MAX_NODES)
                return HV_EFI_ANALYSIS_LIMIT_REACHED;
            pending[pendingCount++] = children[index];
        }
    }
    ZeroBytes(pending, sizeof(pending));
    return HV_EFI_ANALYSIS_SUCCESS;
}

HV_EFI_ANALYSIS_STATUS Traverse(
    HV_EFI_ANALYSIS_STATE *state,
    const HV_EFI_GUEST_PT_WALKER *walker,
    const HV_EFI_ANALYSIS_REQUEST *request)
{
    state->NodeCount = 0;
    ZeroBytes(state->Nodes, sizeof(state->Nodes));
    const uint32_t limit = request->NodeLimit == 0
        ? HV_EFI_ANALYSIS_MAX_NODES : request->NodeLimit;
    switch (request->StructureType) {
    case HV_EFI_ANALYSIS_TRAVERSE_SINGLY_LINKED:
        return TraverseList(state, walker, request, limit, false, false);
    case HV_EFI_ANALYSIS_TRAVERSE_DOUBLY_FORWARD:
        return TraverseList(state, walker, request, limit, true, false);
    case HV_EFI_ANALYSIS_TRAVERSE_DOUBLY_BACKWARD:
        return TraverseList(state, walker, request, limit, true, true);
    case HV_EFI_ANALYSIS_TRAVERSE_CONTIGUOUS_ARRAY:
    case HV_EFI_ANALYSIS_TRAVERSE_POINTER_ARRAY:
    case HV_EFI_ANALYSIS_TRAVERSE_ARRAY_2D:
        return TraverseArray(state, walker, request, limit);
    case HV_EFI_ANALYSIS_TRAVERSE_BINARY_TREE:
    case HV_EFI_ANALYSIS_TRAVERSE_GRAPH_BINARY:
        return TraverseGraph(state, walker, request, limit);
    default:
        return HV_EFI_ANALYSIS_UNSUPPORTED;
    }
}

bool Utf8Valid(const uint8_t *bytes, uint32_t size)
{
    uint32_t index = 0;
    while (index < size) {
        const uint8_t first = bytes[index++];
        if (first < 0x80) continue;
        if (first >= 0xc2 && first <= 0xdf) {
            if (index >= size || (bytes[index++] & 0xc0) != 0x80)
                return false;
            continue;
        }
        if (first >= 0xe0 && first <= 0xef) {
            if (size - index < 2) return false;
            const uint8_t second = bytes[index++];
            const uint8_t third = bytes[index++];
            if ((third & 0xc0) != 0x80 ||
                (first == 0xe0 && (second < 0xa0 || second > 0xbf)) ||
                (first == 0xed && (second < 0x80 || second > 0x9f)) ||
                (first != 0xe0 && first != 0xed &&
                 (second & 0xc0) != 0x80)) return false;
            continue;
        }
        if (first >= 0xf0 && first <= 0xf4) {
            if (size - index < 3) return false;
            const uint8_t second = bytes[index++];
            const uint8_t third = bytes[index++];
            const uint8_t fourth = bytes[index++];
            if ((third & 0xc0) != 0x80 || (fourth & 0xc0) != 0x80 ||
                (first == 0xf0 && (second < 0x90 || second > 0xbf)) ||
                (first == 0xf4 && (second < 0x80 || second > 0x8f)) ||
                (first != 0xf0 && first != 0xf4 &&
                 (second & 0xc0) != 0x80)) return false;
            continue;
        }
        return false;
    }
    return true;
}

uint32_t FixedTypeBytes(uint32_t type)
{
    switch (type) {
    case HV_EFI_ANALYSIS_FIELD_FLOAT32:
    case HV_EFI_ANALYSIS_FIELD_INT32: return 4;
    case HV_EFI_ANALYSIS_FIELD_FLOAT64:
    case HV_EFI_ANALYSIS_FIELD_INT64:
    case HV_EFI_ANALYSIS_FIELD_POINTER: return 8;
    case HV_EFI_ANALYSIS_FIELD_VECTOR3: return 12;
    case HV_EFI_ANALYSIS_FIELD_VECTOR4: return 16;
    case HV_EFI_ANALYSIS_FIELD_MATRIX4X4: return 64;
    default: return 0;
    }
}

HV_EFI_ANALYSIS_STATUS Extract(
    HV_EFI_ANALYSIS_STATE *state,
    const HV_EFI_GUEST_PT_WALKER *walker,
    const HV_EFI_ANALYSIS_REQUEST *request)
{
    if (state->FieldCount >= HV_EFI_ANALYSIS_MAX_FIELDS)
        return HV_EFI_ANALYSIS_LIMIT_REACHED;
    HV_EFI_ANALYSIS_EXTRACTED_FIELD field{};
    CopyBytes(field.FieldName, request->FieldName, sizeof(field.FieldName));
    field.Type = request->FieldType;
    uint32_t bytes = FixedTypeBytes(request->FieldType);
    uint64_t dataAddress = request->Address;
    if (bytes == 0) {
        uint32_t prefix = 0;
        if (request->FieldType == HV_EFI_ANALYSIS_FIELD_LENGTH_PREFIXED8)
            prefix = 1;
        else if (request->FieldType ==
                     HV_EFI_ANALYSIS_FIELD_LENGTH_PREFIXED16) prefix = 2;
        else if (request->FieldType ==
                     HV_EFI_ANALYSIS_FIELD_LENGTH_PREFIXED32) prefix = 4;
        if (prefix != 0) {
            uint8_t lengthBytes[4]{};
            HV_EFI_GUEST_PT_STATUS read = HvEfiGuestPtRead(
                walker, dataAddress, lengthBytes, prefix);
            if (read != HV_EFI_GUEST_PT_OK) return MapWalk(read);
            for (uint32_t index = 0; index < prefix; ++index)
                bytes |= static_cast<uint32_t>(lengthBytes[index]) <<
                    (index * 8);
            if (!AddChecked(dataAddress, prefix, &dataAddress))
                return HV_EFI_ANALYSIS_RANGE_DENIED;
            if (bytes > HV_EFI_ANALYSIS_MAX_STRING_BYTES)
                return HV_EFI_ANALYSIS_LIMIT_REACHED;
        } else {
            bytes = request->ElementCount == 0
                ? HV_EFI_ANALYSIS_MAX_STRING_BYTES : request->ElementCount;
            if (bytes > HV_EFI_ANALYSIS_MAX_STRING_BYTES)
                return HV_EFI_ANALYSIS_INVALID_REQUEST;
        }
    }
    if (bytes == 0 || !UserRange(dataAddress, bytes))
        return HV_EFI_ANALYSIS_RANGE_DENIED;
    HV_EFI_GUEST_PT_STATUS read = HvEfiGuestPtRead(
        walker, dataAddress, field.Value.Bytes, bytes);
    if (read != HV_EFI_GUEST_PT_OK) return MapWalk(read);
    if (request->FieldType == HV_EFI_ANALYSIS_FIELD_ASCII_Z ||
        request->FieldType == HV_EFI_ANALYSIS_FIELD_UTF8_Z) {
        uint32_t length = 0;
        while (length < bytes && field.Value.Bytes[length] != 0) ++length;
        if (length == bytes) return HV_EFI_ANALYSIS_CORRUPT_STRUCTURE;
        bytes = length;
        if (request->FieldType == HV_EFI_ANALYSIS_FIELD_UTF8_Z &&
            !Utf8Valid(field.Value.Bytes, bytes))
            return HV_EFI_ANALYSIS_CORRUPT_STRUCTURE;
    }
    if (request->FieldType == HV_EFI_ANALYSIS_FIELD_POINTER) {
        const uint64_t pointer = field.Value.Pointer;
        if (pointer != 0) {
            if (!UserAddress(pointer) || (pointer & 7) != 0)
                return HV_EFI_ANALYSIS_CORRUPT_STRUCTURE;
            uint64_t ignored = 0;
            read = HvEfiGuestPtTranslate(walker, pointer, &ignored);
            if (read != HV_EFI_GUEST_PT_OK) return MapWalk(read);
        }
    }
    field.ValueBytes = bytes;
    state->Fields[state->FieldCount++] = field;
    return HV_EFI_ANALYSIS_SUCCESS;
}

uint32_t CopyPageBytes(const void *source, uint32_t total, uint32_t offset,
                       void *destination, uint32_t capacity,
                       size_t elementBytes)
{
    if (offset >= total) return 0;
    uint32_t count = total - offset;
    if (count > capacity) count = capacity;
    CopyBytes(destination,
              static_cast<const uint8_t *>(source) +
                  static_cast<size_t>(offset) * elementBytes,
              elementBytes * count);
    return count;
}

void FillResponse(const HV_EFI_ANALYSIS_STATE *state,
                  uint64_t generation,
                  const HV_EFI_ANALYSIS_REQUEST *request,
                  HV_EFI_ANALYSIS_STATUS status,
                  HV_EFI_ANALYSIS_RESPONSE *response)
{
    ZeroBytes(response, sizeof(*response));
    response->Size = sizeof(*response);
    response->Version = HV_EFI_ANALYSIS_VERSION;
    response->Status = status;
    response->SessionActive = state != nullptr ? state->Active : 0;
    response->Generation = generation;
    response->Sequence = request != nullptr ? request->Sequence : 0;
    if (state == nullptr) return;
    response->ProcessId = state->ProcessId;
    response->ScannedBytes = state->ScannedBytes;
    response->TotalRegionCount = state->RegionCount;
    response->TotalResultCount = state->ResultCount;
    response->TotalNodeCount = state->NodeCount;
    response->TotalFieldCount = state->FieldCount;
    response->RegionsTruncated = state->RegionsTruncated;
    const uint32_t regionOffset = request != nullptr
        ? request->RegionOffset : 0;
    const uint32_t resultOffset = request != nullptr
        ? request->ResultOffset : 0;
    response->ReturnedRegionCount = CopyPageBytes(
        state->Regions, state->RegionCount, regionOffset,
        response->Regions, HV_EFI_ANALYSIS_RESPONSE_REGIONS,
        sizeof(state->Regions[0]));
    response->ReturnedResultCount = CopyPageBytes(
        state->Results, state->ResultCount, resultOffset,
        response->Results, HV_EFI_ANALYSIS_RESPONSE_RESULTS,
        sizeof(state->Results[0]));
    response->ReturnedNodeCount = CopyPageBytes(
        state->Nodes, state->NodeCount, resultOffset,
        response->Nodes, HV_EFI_ANALYSIS_RESPONSE_NODES,
        sizeof(state->Nodes[0]));
    response->ReturnedFieldCount = CopyPageBytes(
        state->Fields, state->FieldCount, resultOffset,
        response->Fields, HV_EFI_ANALYSIS_RESPONSE_FIELDS,
        sizeof(state->Fields[0]));
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

extern "C" HV_EFI_ANALYSIS_STATUS HvEfiRuntimeAnalysisExecute(
    HV_EFI_ANALYSIS_STATE *state,
    const HV_EFI_RUNTIME_PROCESS_STATE *processState,
    HV_EFI_SCANNER_WINDOW *window,
    const HV_EFI_SCANNER_RAM_RANGE *allowedRanges,
    uint32_t allowedRangeCount,
    uint64_t generation,
    uint64_t guestCr4,
    uint32_t maxPhysicalAddressBits,
    const HV_EFI_ANALYSIS_REQUEST *request,
    HV_EFI_ANALYSIS_RESPONSE *response)
{
    if (response != nullptr) ZeroBytes(response, sizeof(*response));
    if (state == nullptr || processState == nullptr || window == nullptr ||
        allowedRanges == nullptr || allowedRangeCount == 0 ||
        generation == 0 || response == nullptr ||
        state->Marker != HV_EFI_ANALYSIS_STATE_MARKER ||
        !RequestValid(request)) {
        FillResponse(state, generation, request,
                     HV_EFI_ANALYSIS_INVALID_REQUEST, response);
        return HV_EFI_ANALYSIS_INVALID_REQUEST;
    }
    if (!TryAcquire(&state->Busy)) {
        FillResponse(state, generation, request,
                     HV_EFI_ANALYSIS_BUSY, response);
        return HV_EFI_ANALYSIS_BUSY;
    }
    HV_EFI_ANALYSIS_STATUS status = HV_EFI_ANALYSIS_SUCCESS;
    if (request->Sequence <= state->LastSequence) {
        status = HV_EFI_ANALYSIS_REPLAY_REJECTED;
        goto complete;
    }

    if (request->Operation == HV_EFI_ANALYSIS_START) {
        const HV_EFI_GUEST_PROCESS_RECORD *process =
            FindProcess(processState, request->ProcessId);
        if (process == nullptr || process->DirectoryTableBase == 0) {
            status = HV_EFI_ANALYSIS_NOT_FOUND;
            goto complete;
        }
        ResetSession(state);
        state->Active = 1;
        state->Generation = generation;
        state->ProcessId = request->ProcessId;
        state->TargetCr3 = process->DirectoryTableBase & ~kPageMask;
        state->RegionFilter = HV_EFI_ANALYSIS_REGION_FILTER_ALL;
    } else if (request->Operation == HV_EFI_ANALYSIS_STOP) {
        ResetSession(state);
        goto complete;
    } else if (state->Active == 0 || state->Generation != generation) {
        status = HV_EFI_ANALYSIS_NO_SESSION;
        goto complete;
    } else if (request->ProcessId != 0 &&
               request->ProcessId != state->ProcessId) {
        status = HV_EFI_ANALYSIS_WRONG_PROCESS;
        goto complete;
    }

    {
        HV_EFI_GUEST_SCANNER_READER reader{};
        HV_EFI_GUEST_PT_WALKER walker{};
        const HV_EFI_GUEST_PT_STATUS initialized =
            HvEfiGuestPtWalkerInitializeScanner(
                window, allowedRanges, allowedRangeCount, state->TargetCr3,
                guestCr4, maxPhysicalAddressBits, &reader, &walker);
        if (initialized != HV_EFI_GUEST_PT_OK) {
            status = MapWalk(initialized);
            goto operational_failure;
        }
        switch (request->Operation) {
        case HV_EFI_ANALYSIS_START:
            status = DiscoverRegions(state, &walker);
            if ((status == HV_EFI_ANALYSIS_SUCCESS ||
                 status == HV_EFI_ANALYSIS_MORE_DATA) &&
                (request->SignatureCount != 0 ||
                 request->RegionCount != 0)) {
                const HV_EFI_ANALYSIS_STATUS configured =
                    Configure(state, request);
                if (configured != HV_EFI_ANALYSIS_SUCCESS)
                    status = configured;
            }
            break;
        case HV_EFI_ANALYSIS_CONFIGURE:
            status = Configure(state, request);
            break;
        case HV_EFI_ANALYSIS_SCAN:
            status = Scan(state, &walker, request);
            break;
        case HV_EFI_ANALYSIS_TRAVERSE:
            status = Traverse(state, &walker, request);
            break;
        case HV_EFI_ANALYSIS_EXTRACT:
            status = Extract(state, &walker, request);
            break;
        case HV_EFI_ANALYSIS_RESULTS:
            status = HV_EFI_ANALYSIS_SUCCESS;
            break;
        default:
            status = HV_EFI_ANALYSIS_INVALID_REQUEST;
            break;
        }
    }

operational_failure:
    if (status != HV_EFI_ANALYSIS_SUCCESS &&
        status != HV_EFI_ANALYSIS_NOT_FOUND &&
        status != HV_EFI_ANALYSIS_MORE_DATA &&
        status != HV_EFI_ANALYSIS_LIMIT_REACHED &&
        status != HV_EFI_ANALYSIS_MULTIPLE_MATCHES &&
        request->Operation != HV_EFI_ANALYSIS_START) {
        ResetSession(state);
    }

complete:
    state->LastSequence = request->Sequence;
    FillResponse(state, generation, request, status, response);
    Release(&state->Busy);
    return status;
}

extern "C" uint64_t HvEfiRuntimeAnalysisResidentCommand(
    HV_EFI_CONTEXT *context,
    uint64_t mailboxPhysical,
    uint64_t mailboxBytes,
    uint64_t expectedSequence)
{
    if (context == nullptr || mailboxPhysical == 0 || expectedSequence == 0 ||
        mailboxBytes < HVR1_MAILBOX_HEADER_BYTES +
            sizeof(HV_EFI_ANALYSIS_MAILBOX) ||
        mailboxBytes > HVR1_MAILBOX_BYTES ||
        context->RuntimeAnalysisStatePhysical == 0 ||
        context->RuntimeScannerStatePhysical == 0 ||
        context->RuntimeScannerRamRangesPhysical == 0 ||
        context->RuntimeScannerRamRangeCount == 0) {
        return HV_EFI_ANALYSIS_INVALID_REQUEST;
    }
    HV_EFI_SCANNER_WINDOW window{};
    if (HvEfiScannerInitializeNativeWindow(
            reinterpret_cast<uint64_t *>(
                static_cast<uintptr_t>(context->PageTablePhysical)),
            HV_EFI_PAGE_TABLE_PAGES, context->PageTablePhysical,
            &window) != HV_EFI_SCANNER_OK)
        return HV_EFI_ANALYSIS_MAPPING_FAILED;

    const auto *ranges = reinterpret_cast<const HV_EFI_SCANNER_RAM_RANGE *>(
        static_cast<uintptr_t>(context->RuntimeScannerRamRangesPhysical));
    const uint64_t commandBytes = HVR1_MAILBOX_HEADER_BYTES +
        sizeof(HV_EFI_ANALYSIS_MAILBOX);
    HV_EFI_SCANNER_MAPPING mapping{};
    if (HvEfiScannerMapReadOnly(
            &window, ranges, context->RuntimeScannerRamRangeCount,
            mailboxPhysical, commandBytes, &mapping) != HV_EFI_SCANNER_OK)
        return HV_EFI_ANALYSIS_MAPPING_FAILED;

    auto *state = reinterpret_cast<HV_EFI_ANALYSIS_STATE *>(
        static_cast<uintptr_t>(context->RuntimeAnalysisStatePhysical));
    HV_EFI_ANALYSIS_REQUEST *request = &state->RequestScratch;
    ZeroBytes(request, sizeof(*request));
    const auto *outer = reinterpret_cast<const HVR1_MAILBOX *>(mapping.View);
    const auto *mailbox = reinterpret_cast<const HV_EFI_ANALYSIS_MAILBOX *>(
        mapping.View + HVR1_MAILBOX_PAYLOAD_DATA_OFFSET);
    const bool valid = outer->Magic == HVR1_PROTOCOL_MAGIC &&
        outer->Version == HVR1_PROTOCOL_VERSION &&
        outer->HeaderSize == HVR1_MAILBOX_HEADER_BYTES &&
        outer->TotalSize == commandBytes &&
        outer->Operation == HVR1_MAILBOX_OPERATION_ANALYSIS &&
        outer->Flags == 0 &&
        outer->Generation == context->RuntimeGeneration &&
        outer->Sequence == expectedSequence &&
        outer->PayloadOffset == HVR1_MAILBOX_HEADER_BYTES &&
        outer->PayloadSize == sizeof(HV_EFI_ANALYSIS_MAILBOX) &&
        outer->Status == 0 && outer->Reserved == 0 &&
        mailbox->Magic == HV_EFI_ANALYSIS_MAGIC &&
        mailbox->Version == HV_EFI_ANALYSIS_VERSION &&
        mailbox->HeaderSize == offsetof(HV_EFI_ANALYSIS_MAILBOX, Request) &&
        mailbox->State == HV_EFI_ANALYSIS_MAILBOX_REQUEST_READY &&
        mailbox->Reserved == 0 &&
        mailbox->Generation == context->RuntimeGeneration &&
        mailbox->Request.Sequence == expectedSequence;
    if (valid) CopyBytes(request, &mailbox->Request, sizeof(*request));
    if (HvEfiScannerUnmap(&window, &mapping) != HV_EFI_SCANNER_OK)
        return HV_EFI_ANALYSIS_MAPPING_FAILED;

    HV_EFI_ANALYSIS_STATUS status = HV_EFI_ANALYSIS_INVALID_REQUEST;
    if (valid) {
        auto *processState = reinterpret_cast<HV_EFI_RUNTIME_PROCESS_STATE *>(
            static_cast<uintptr_t>(context->RuntimeScannerStatePhysical +
                HV_EFI_RUNTIME_PROCESS_STATE_PAGE_OFFSET));
        status = HvEfiRuntimeAnalysisExecute(
            state, processState, &window, ranges,
            context->RuntimeScannerRamRangeCount,
            context->RuntimeGeneration, context->GuestCr4,
            NativeMaxPhysicalAddressBits(), request,
            &state->ResponseScratch);
    } else {
        FillResponse(state, context->RuntimeGeneration, request, status,
                     &state->ResponseScratch);
    }

    ZeroBytes(&mapping, sizeof(mapping));
    if (HvEfiScannerMapReadOnly(
            &window, ranges, context->RuntimeScannerRamRangeCount,
            mailboxPhysical, commandBytes, &mapping) != HV_EFI_SCANNER_OK) {
        ZeroBytes(request, sizeof(*request));
        return HV_EFI_ANALYSIS_MAPPING_FAILED;
    }
    SetWritable(&window, &mapping, true);
    auto *writeMailbox = reinterpret_cast<HV_EFI_ANALYSIS_MAILBOX *>(
        const_cast<uint8_t *>(mapping.View) +
            HVR1_MAILBOX_PAYLOAD_DATA_OFFSET);
    writeMailbox->State = HV_EFI_ANALYSIS_MAILBOX_PROCESSING;
    CopyBytes(&writeMailbox->Response, &state->ResponseScratch,
              sizeof(writeMailbox->Response));
    Barrier();
    writeMailbox->State = HV_EFI_ANALYSIS_MAILBOX_RESPONSE_READY;
    SetWritable(&window, &mapping, false);
    const HV_EFI_SCANNER_STATUS unmap =
        HvEfiScannerUnmap(&window, &mapping);
    ZeroBytes(request, sizeof(*request));
    ZeroBytes(&state->ResponseScratch, sizeof(state->ResponseScratch));
    return unmap == HV_EFI_SCANNER_OK
        ? status : HV_EFI_ANALYSIS_MAPPING_FAILED;
}

#if defined(_MSC_VER)
#pragma code_seg(pop)
#endif
