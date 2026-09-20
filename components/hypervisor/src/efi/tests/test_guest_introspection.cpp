#include "guest_pt_walker.h"
#include "runtime_process.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s\n", message); \
            return 1; \
        } \
    } while (0)

namespace {

struct Memory {
    uint8_t *Bytes;
    size_t Size;
};

static uint8_t g_memory[16 * 1024 * 1024];

HV_EFI_GUEST_PT_STATUS ReadPhysical(
    void *context, uint64_t physical, void *destination, size_t size)
{
    auto *memory = static_cast<Memory *>(context);
    if (memory == nullptr || destination == nullptr || size == 0 ||
        physical > memory->Size ||
        size > memory->Size - static_cast<size_t>(physical)) {
        return HV_EFI_GUEST_PT_PHYSICAL_RANGE;
    }
    memcpy(destination, memory->Bytes + physical, size);
    return HV_EFI_GUEST_PT_OK;
}

void Store16(Memory &memory, uint64_t address, uint16_t value)
{
    memcpy(memory.Bytes + address, &value, sizeof(value));
}

void Store32(Memory &memory, uint64_t address, uint32_t value)
{
    memcpy(memory.Bytes + address, &value, sizeof(value));
}

void Store64(Memory &memory, uint64_t address, uint64_t value)
{
    memcpy(memory.Bytes + address, &value, sizeof(value));
}

HV_EFI_GUEST_PT_WALKER MakeWalker(Memory &memory, uint64_t cr3)
{
    const HV_EFI_GUEST_PHYSICAL_READER reader = { ReadPhysical, &memory };
    HV_EFI_GUEST_PT_WALKER walker{};
    if (HvEfiGuestPtWalkerInitialize(
            &reader, cr3, 0, 48, &walker) != HV_EFI_GUEST_PT_OK) {
        abort();
    }
    return walker;
}

int TestWalker()
{
    memset(g_memory, 0, sizeof(g_memory));
    Memory memory{g_memory, sizeof(g_memory)};
    Store64(memory, 0x1000, 0x2000 | 7);
    Store64(memory, 0x2000, 0x3000 | 7);
    Store64(memory, 0x3000, 0x4000 | 7);
    Store64(memory, 0x4000 + 5 * 8, 0x8000 | 1);
    memory.Bytes[0x8000 + 0x123] = 0x5a;
    HV_EFI_GUEST_PT_WALKER walker = MakeWalker(memory, 0x1abc);
    uint64_t physical = 0;
    TEST_ASSERT(HvEfiGuestPtTranslate(
        &walker, 5 * 4096 + 0x123, &physical) == HV_EFI_GUEST_PT_OK &&
        physical == 0x8123, "4-KiB translation strips CR3 PCID");
    uint8_t value = 0;
    TEST_ASSERT(HvEfiGuestPtRead(
        &walker, 5 * 4096 + 0x123, &value, 1) == HV_EFI_GUEST_PT_OK &&
        value == 0x5a, "translated read uses physical reader");

    Store64(memory, 0x4000 + 6 * 8, 0xa000 | 1);
    memory.Bytes[0x8fff] = 0x11;
    memory.Bytes[0xa000] = 0x22;
    uint8_t acrossPages[2]{};
    TEST_ASSERT(HvEfiGuestPtRead(
        &walker, 6 * 4096 - 1, acrossPages, sizeof(acrossPages)) ==
            HV_EFI_GUEST_PT_OK &&
        acrossPages[0] == 0x11 && acrossPages[1] == 0x22,
        "virtual reads cross noncontiguous physical pages page-by-page");

    Store64(memory, 0x4000 + 8 * 8,
            0xb000 | 7 | (UINT64_C(1) << 63));
    HV_EFI_GUEST_PT_TRANSLATION detailed{};
    TEST_ASSERT(HvEfiGuestPtTranslateEx(
        &walker, 8 * 4096 + 0x55, &detailed) == HV_EFI_GUEST_PT_OK &&
        detailed.PhysicalAddress == 0xb055 && detailed.PageSize == 4096 &&
        (detailed.Access & (HV_EFI_GUEST_PT_ACCESS_READ |
                            HV_EFI_GUEST_PT_ACCESS_WRITE |
                            HV_EFI_GUEST_PT_ACCESS_USER)) ==
            (HV_EFI_GUEST_PT_ACCESS_READ |
             HV_EFI_GUEST_PT_ACCESS_WRITE |
             HV_EFI_GUEST_PT_ACCESS_USER) &&
        (detailed.Access & HV_EFI_GUEST_PT_ACCESS_EXECUTE) == 0,
        "extended translation reports effective R/W/X and user access");
    HV_EFI_GUEST_PT_RANGE ranges[8]{};
    uint32_t rangeCount = 0;
    uint32_t pagesRead = 0;
    uint32_t truncated = 0;
    TEST_ASSERT(HvEfiGuestPtEnumerateUserRanges(
        &walker, ranges, 8, 16, &rangeCount, &pagesRead, &truncated) ==
            HV_EFI_GUEST_PT_OK && rangeCount == 3 && pagesRead == 4 &&
        truncated == 0 && ranges[2].VirtualAddress == 8 * 4096 &&
        ranges[2].PhysicalAddress == 0xb000,
        "present user mappings are enumerated without linear VA probing");
    TEST_ASSERT(HvEfiGuestPtEnumerateUserRanges(
        &walker, ranges, 1, 16, &rangeCount, &pagesRead, &truncated) ==
            HV_EFI_GUEST_PT_LIMIT_REACHED && rangeCount == 1 &&
        truncated == 1,
        "range enumeration stops at a caller-owned capacity bound");

    Store64(memory, 0x1000 + 256 * 8, 0x2000 | 1);
    TEST_ASSERT(HvEfiGuestPtTranslate(
        &walker, UINT64_C(0xffff800000005123), &physical) ==
            HV_EFI_GUEST_PT_OK && physical == 0x8123,
        "canonical upper-half address translated");

    Store64(memory, 0x4000 + 7 * 8, (UINT64_C(1) << 50) | 1);
    TEST_ASSERT(HvEfiGuestPtTranslate(
        &walker, 7 * 4096, &physical) == HV_EFI_GUEST_PT_RESERVED_BITS,
        "page-table address bits beyond MAXPHYADDR rejected");

    Store64(memory, 0x3000 + 1 * 8, 0x200000 | 0x81);
    TEST_ASSERT(HvEfiGuestPtTranslate(
        &walker, 0x200000 + 0x5678, &physical) == HV_EFI_GUEST_PT_OK &&
        physical == 0x205678, "2-MiB PDE translation");
    Store64(memory, 0x3000 + 1 * 8, 0x200000 | 0x1081);
    TEST_ASSERT(HvEfiGuestPtTranslate(
        &walker, 0x200000 + 0x5678, &physical) == HV_EFI_GUEST_PT_OK &&
        physical == 0x205678, "2-MiB PDE permits large-page PAT bit");
    Store64(memory, 0x3000 + 1 * 8, 0x200000 | 0x2081);
    TEST_ASSERT(HvEfiGuestPtTranslate(
        &walker, 0x200000 + 0x5678, &physical) ==
            HV_EFI_GUEST_PT_RESERVED_BITS,
        "2-MiB PDE rejects reserved address bits");
    Store64(memory, 0x2000 + 1 * 8, 0x40000000 | 0x81);
    TEST_ASSERT(HvEfiGuestPtTranslate(
        &walker, 0x40000000 + 0x123456, &physical) == HV_EFI_GUEST_PT_OK &&
        physical == 0x40123456, "1-GiB PDPTE translation");
    Store64(memory, 0x2000 + 1 * 8, 0x40000000 | 0x1081);
    TEST_ASSERT(HvEfiGuestPtTranslate(
        &walker, 0x40000000 + 0x123456, &physical) == HV_EFI_GUEST_PT_OK &&
        physical == 0x40123456, "1-GiB PDPTE permits large-page PAT bit");
    Store64(memory, 0x2000 + 1 * 8, 0x40000000 | 0x2081);
    TEST_ASSERT(HvEfiGuestPtTranslate(
        &walker, 0x40000000 + 0x123456, &physical) ==
            HV_EFI_GUEST_PT_RESERVED_BITS,
        "1-GiB PDPTE rejects reserved address bits");
    TEST_ASSERT(HvEfiGuestPtTranslate(
        &walker, UINT64_C(0x0001000000000000), &physical) ==
            HV_EFI_GUEST_PT_NON_CANONICAL,
        "non-canonical address rejected");
    const HV_EFI_GUEST_PHYSICAL_READER reader = { ReadPhysical, &memory };
    HV_EFI_GUEST_PT_WALKER rejected{};
    TEST_ASSERT(HvEfiGuestPtWalkerInitialize(
        &reader, 0x1000, HV_EFI_GUEST_CR4_LA57, 48, &rejected) ==
            HV_EFI_GUEST_PT_LA57_UNSUPPORTED,
        "LA57 rejected fail-safe");
    return 0;
}

hypersinv::windows::KernelProfile MakeProfile()
{
    hypersinv::windows::KernelProfile profile{};
    profile.SchemaVersion = 1;
    profile.BuildNumber = 26200;
    profile.Ubr = 9445;
    profile.ImageTimestamp = 0x12345678;
    profile.ImageSize = 0x200000;
    for (uint32_t index = 0; index < 16; ++index)
        profile.PdbGuid[index] = static_cast<uint8_t>(index + 1);
    profile.PdbAge = 1;
    profile.EprocessSize = 0x400;
    profile.EprocessPcb = 0;
    profile.EprocessActiveProcessLinks = 0x100;
    profile.EprocessUniqueProcessId = 0x180;
    profile.EprocessImageFileName = 0x200;
    profile.EprocessImageFileNameSize = 15;
    profile.KprocessDirectoryTableBase = 0x28;
    profile.EprocessDirectoryTableBase = 0x28;
    profile.PsActiveProcessHeadRva = 0x1000;
    return profile;
}

void BuildSyntheticKernel(Memory &memory,
                          const hypersinv::windows::KernelProfile &profile)
{
    constexpr uint64_t kernel = 0x400000;
    Store16(memory, kernel, 0x5a4d);
    Store32(memory, kernel + 0x3c, 0x80);
    Store32(memory, kernel + 0x80, 0x00004550);
    Store32(memory, kernel + 0x80 + 8, profile.ImageTimestamp);
    Store16(memory, kernel + 0x80 + 24, 0x20b);
    Store32(memory, kernel + 0x80 + 80, profile.ImageSize);
    Store32(memory, kernel + 0x80 + 184, 0x3000);
    Store32(memory, kernel + 0x80 + 188, 28);
    Store32(memory, kernel + 0x3000 + 12, 2);
    Store32(memory, kernel + 0x3000 + 16, 24);
    Store32(memory, kernel + 0x3000 + 20, 0x4000);
    Store32(memory, kernel + 0x4000, 0x53445352);
    memcpy(memory.Bytes + kernel + 0x4000 + 4,
                profile.PdbGuid, 16);
    Store32(memory, kernel + 0x4000 + 20, profile.PdbAge);
}

void BuildProcessList(Memory &memory,
                      const hypersinv::windows::KernelProfile &profile)
{
    constexpr uint64_t head = 0x401000;
    constexpr uint64_t first = 0x600000;
    constexpr uint64_t second = 0x601000;
    const uint64_t firstLink = first + profile.EprocessActiveProcessLinks;
    const uint64_t secondLink = second + profile.EprocessActiveProcessLinks;
    Store64(memory, head, firstLink);
    Store64(memory, head + 8, secondLink);
    Store64(memory, firstLink, secondLink);
    Store64(memory, firstLink + 8, head);
    Store64(memory, secondLink, head);
    Store64(memory, secondLink + 8, firstLink);
    Store64(memory, first + profile.EprocessUniqueProcessId, 4);
    Store64(memory, first + profile.EprocessDirectoryTableBase, 0x9007);
    memcpy(memory.Bytes + first + profile.EprocessImageFileName,
                "System", 6);
    Store64(memory, second + profile.EprocessUniqueProcessId, 4242);
    Store64(memory, second + profile.EprocessDirectoryTableBase, 0xa123);
    memcpy(memory.Bytes + second + profile.EprocessImageFileName,
                "target.exe", 10);
}

HV_EFI_RUNTIME_PROCESS_REQUEST MakeRequest()
{
    HV_EFI_RUNTIME_PROCESS_REQUEST request{};
    request.Size = sizeof(request);
    request.Version = HV_EFI_RUNTIME_PROCESS_VERSION;
    request.NameLength = 10;
    request.Sequence = 1;
    request.KernelBase = 0x400000;
    request.BuildNumber = 26200;
    request.Ubr = 9445;
    memcpy(request.ImageFileName, "target.exe", 10);
    return request;
}

int TestProcessEnumeration()
{
    memset(g_memory, 0, sizeof(g_memory));
    Memory memory{g_memory, sizeof(g_memory)};
    Store64(memory, 0x1000, 0x2000 | 1);
    Store64(memory, 0x2000, 0x3000 | 1);
    Store64(memory, 0x3000 + 2 * 8, 0x400000 | 0x81);
    Store64(memory, 0x3000 + 3 * 8, 0x600000 | 0x81);
    const auto profile = MakeProfile();
    BuildSyntheticKernel(memory, profile);
    BuildProcessList(memory, profile);
    HV_EFI_GUEST_PT_WALKER walker = MakeWalker(memory, 0x1000);
    uint16_t mz = 0;
    TEST_ASSERT(HvEfiGuestPtRead(&walker, 0x400000, &mz, sizeof(mz)) ==
                    HV_EFI_GUEST_PT_OK && mz == 0x5a4d,
                "synthetic kernel mapping readable");
    uint64_t headFlink = 0;
    TEST_ASSERT(HvEfiGuestPtRead(&walker, 0x401000, &headFlink,
                                 sizeof(headFlink)) == HV_EFI_GUEST_PT_OK &&
                    headFlink == 0x600100,
                "synthetic process head readable");
    HV_EFI_RUNTIME_PROCESS_STATE state{};
    state.Marker = HV_EFI_RUNTIME_PROCESS_STATE_MARKER;
    auto request = MakeRequest();
    HV_EFI_RUNTIME_PROCESS_RESPONSE response{};
    const HV_EFI_RUNTIME_PROCESS_STATUS located =
        HvEfiGuestProcessLocateWithWalker(
            &state, &walker, &profile, 7, &request, &response);
    if (located != HV_EFI_RUNTIME_PROCESS_SUCCESS)
        fprintf(stderr, "locate status=%u enumerated=%u stored=%u\n",
                static_cast<unsigned>(located), response.EnumeratedCount,
                response.StoredCount);
    TEST_ASSERT(located == HV_EFI_RUNTIME_PROCESS_SUCCESS,
        "target process located");
    TEST_ASSERT(response.ProcessId == 4242 &&
                response.DirectoryTableBase == 0xa000 &&
                response.EprocessVirtualAddress == 0x601000 &&
                response.EnumeratedCount == 2 && response.StoredCount == 2,
                "PID, CR3, EPROCESS, and private records returned");

    request.Sequence = 2;
    request.NameLength = 7;
    memset(request.ImageFileName, 0, sizeof(request.ImageFileName));
    memcpy(request.ImageFileName, "missing", 7);
    state.StoredCount = 0;
    state.EnumeratedCount = 0;
    TEST_ASSERT(HvEfiGuestProcessLocateWithWalker(
        &state, &walker, &profile, 7, &request, &response) ==
            HV_EFI_RUNTIME_PROCESS_NOT_FOUND,
        "complete list reports not found");

    const uint64_t firstLink = 0x600000 + profile.EprocessActiveProcessLinks;
    const uint64_t secondLink = 0x601000 + profile.EprocessActiveProcessLinks;
    Store64(memory, secondLink, firstLink);
    state.StoredCount = 0;
    state.EnumeratedCount = 0;
    TEST_ASSERT(HvEfiGuestProcessLocateWithWalker(
        &state, &walker, &profile, 7, &request, &response) ==
            HV_EFI_RUNTIME_PROCESS_CYCLE_DETECTED,
        "malformed non-head cycle detected");
    return 0;
}

} // namespace

int main()
{
    if (TestWalker() != 0 || TestProcessEnumeration() != 0) return 1;
    puts("PASS: guest paging and process-enumeration contracts");
    return 0;
}
