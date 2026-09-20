#include "runtime_scanner.h"

#include <stdint.h>
#include <stdio.h>

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s\n", message); \
            return 1; \
        } \
    } while (0)

namespace {

void NoopInvalidate(void *, uint64_t) {}

struct FIXTURE {
    alignas(4096) uint8_t Data[16384];
    alignas(4096) uint64_t PageTables[
        HV_EFI_SCANNER_REQUIRED_PAGE_TABLE_PAGES * 512];
    HV_EFI_SCANNER_WINDOW Window;
    HV_EFI_SCANNER_RAM_RANGE Range;
    HV_EFI_RUNTIME_SCAN_STATE State;
};

bool Initialize(FIXTURE *fixture)
{
    for (size_t index = 0; index < sizeof(*fixture); ++index) {
        reinterpret_cast<uint8_t *>(fixture)[index] = 0;
    }
    const uint64_t pageTablePhysical = UINT64_C(0x00200000);
    if (HvEfiScannerPrepareHostPageTables(
            fixture->PageTables,
            HV_EFI_SCANNER_REQUIRED_PAGE_TABLE_PAGES,
            pageTablePhysical) == 0 ||
        HvEfiScannerInitializeWindow(
            fixture->PageTables,
            HV_EFI_SCANNER_REQUIRED_PAGE_TABLE_PAGES,
            pageTablePhysical, 1, 1, NoopInvalidate, nullptr,
            &fixture->Window) != HV_EFI_SCANNER_OK) {
        return false;
    }
    fixture->Window.VirtualBase = static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(fixture->Data));
    fixture->Range.PhysicalBase = UINT64_C(0x01000000);
    fixture->Range.Size = 4096;
    fixture->State.Marker = HV_EFI_RUNTIME_SCAN_STATE_MARKER;
    return true;
}

HV_EFI_RUNTIME_SCAN_REQUEST Request(
    uint16_t callbackType,
    uint64_t sequence,
    uint64_t start,
    uint64_t end,
    uint64_t budget)
{
    HV_EFI_RUNTIME_SCAN_REQUEST request = {};
    request.Size = sizeof(request);
    request.Version = HV_EFI_RUNTIME_SCAN_VERSION;
    request.CallbackType = callbackType;
    request.PatternLength = 4;
    request.MaxResults = 8;
    request.StartAddress = start;
    request.EndAddress = end;
    request.WildcardMask = UINT64_C(1) << 2;
    request.MaxBytesToScan = budget;
    request.Sequence = sequence;
    request.Pattern[0] = 0x48;
    request.Pattern[1] = 0x8b;
    request.Pattern[2] = 0;
    request.Pattern[3] = 0x90;
    return request;
}

int TestFirstAndAllMatches()
{
    FIXTURE fixture;
    TEST_ASSERT(Initialize(&fixture), "fixture initializes");
    const uint32_t offsets[] = {100, 500, 900};
    for (uint32_t offset : offsets) {
        fixture.Data[offset] = 0x48;
        fixture.Data[offset + 1] = 0x8b;
        fixture.Data[offset + 2] = static_cast<uint8_t>(offset);
        fixture.Data[offset + 3] = 0x90;
    }

    HV_EFI_RUNTIME_SCAN_RESPONSE response = {};
    HV_EFI_RUNTIME_SCAN_REQUEST request = Request(
        HV_EFI_RUNTIME_SCAN_CALLBACK_FIRST_MATCH, 1,
        fixture.Range.PhysicalBase,
        fixture.Range.PhysicalBase + fixture.Range.Size,
        fixture.Range.Size);
    TEST_ASSERT(HvEfiRuntimeScannerExecute(
        &fixture.State, &fixture.Window, &fixture.Range, 1, 7,
        &request, &response) == HV_EFI_RUNTIME_SCAN_SUCCESS,
        "first-match command succeeds");
    TEST_ASSERT(response.ResultCount == 1 &&
                response.Results[0].PhysicalAddress ==
                    fixture.Range.PhysicalBase + offsets[0],
                "first match physical address returned");

    request = Request(
        HV_EFI_RUNTIME_SCAN_CALLBACK_ALL, 2,
        fixture.Range.PhysicalBase,
        fixture.Range.PhysicalBase + fixture.Range.Size,
        fixture.Range.Size);
    TEST_ASSERT(HvEfiRuntimeScannerExecute(
        &fixture.State, &fixture.Window, &fixture.Range, 1, 7,
        &request, &response) == HV_EFI_RUNTIME_SCAN_SUCCESS,
        "all-match command succeeds");
    TEST_ASSERT(response.ResultCount == 3,
                "all-match command returns every match");
    for (uint32_t index = 0; index < 3; ++index) {
        TEST_ASSERT(response.Results[index].PhysicalAddress ==
            fixture.Range.PhysicalBase + offsets[index],
            "all-match physical address ordering is stable");
    }
    return 0;
}

int TestBudgetContinuationAndReplay()
{
    FIXTURE fixture;
    TEST_ASSERT(Initialize(&fixture), "continuation fixture initializes");
    fixture.Data[900] = 0x48;
    fixture.Data[901] = 0x8b;
    fixture.Data[902] = 0x11;
    fixture.Data[903] = 0x90;

    HV_EFI_RUNTIME_SCAN_REQUEST request = Request(
        HV_EFI_RUNTIME_SCAN_CALLBACK_FIRST_MATCH, 10,
        fixture.Range.PhysicalBase,
        fixture.Range.PhysicalBase + fixture.Range.Size,
        512);
    HV_EFI_RUNTIME_SCAN_RESPONSE response = {};
    TEST_ASSERT(HvEfiRuntimeScannerExecute(
        &fixture.State, &fixture.Window, &fixture.Range, 1, 9,
        &request, &response) == HV_EFI_RUNTIME_SCAN_MORE_DATA &&
        response.ResultCount == 0 &&
        response.NextAddress == fixture.Range.PhysicalBase + 512,
        "bounded command returns a continuation address");

    request.StartAddress = response.NextAddress;
    request.Sequence = 11;
    TEST_ASSERT(HvEfiRuntimeScannerExecute(
        &fixture.State, &fixture.Window, &fixture.Range, 1, 9,
        &request, &response) == HV_EFI_RUNTIME_SCAN_SUCCESS &&
        response.ResultCount == 1 &&
        response.Results[0].PhysicalAddress ==
            fixture.Range.PhysicalBase + 900,
        "continuation finds the later match");
    TEST_ASSERT(HvEfiRuntimeScannerExecute(
        &fixture.State, &fixture.Window, &fixture.Range, 1, 9,
        &request, &response) == HV_EFI_RUNTIME_SCAN_REPLAY_REJECTED,
        "replayed sequence is rejected");
    return 0;
}

int TestSingleAndCapacity()
{
    FIXTURE fixture;
    TEST_ASSERT(Initialize(&fixture), "single fixture initializes");
    const uint32_t offsets[] = {100u, 200u};
    for (uint32_t offset : offsets) {
        fixture.Data[offset] = 0x48;
        fixture.Data[offset + 1] = 0x8b;
        fixture.Data[offset + 2] = 0x22;
        fixture.Data[offset + 3] = 0x90;
    }
    HV_EFI_RUNTIME_SCAN_REQUEST request = Request(
        HV_EFI_RUNTIME_SCAN_CALLBACK_SINGLE, 1,
        fixture.Range.PhysicalBase,
        fixture.Range.PhysicalBase + fixture.Range.Size,
        fixture.Range.Size);
    HV_EFI_RUNTIME_SCAN_RESPONSE response = {};
    TEST_ASSERT(HvEfiRuntimeScannerExecute(
        &fixture.State, &fixture.Window, &fixture.Range, 1, 3,
        &request, &response) == HV_EFI_RUNTIME_SCAN_MULTIPLE_MATCHES &&
        response.ResultCount == 2,
        "single mode rejects an ambiguous signature");

    request.CallbackType = HV_EFI_RUNTIME_SCAN_CALLBACK_ALL;
    request.MaxResults = 1;
    request.Sequence = 2;
    TEST_ASSERT(HvEfiRuntimeScannerExecute(
        &fixture.State, &fixture.Window, &fixture.Range, 1, 3,
        &request, &response) == HV_EFI_RUNTIME_SCAN_RESULT_CAPACITY &&
        response.ResultCount == 1,
        "all mode reports bounded result capacity");
    return 0;
}

int TestMailboxHelpersAndBusy()
{
    FIXTURE fixture;
    TEST_ASSERT(Initialize(&fixture), "mailbox fixture initializes");
    HV_EFI_RUNTIME_SCAN_REQUEST request = Request(
        HV_EFI_RUNTIME_SCAN_CALLBACK_FIRST_MATCH, 44,
        fixture.Range.PhysicalBase,
        fixture.Range.PhysicalBase + fixture.Range.Size,
        fixture.Range.Size);
    HV_EFI_RUNTIME_SCAN_MAILBOX mailbox = {};
    TEST_ASSERT(HvEfiRuntimeScannerWriteRequest(
        &mailbox, sizeof(mailbox), 12, &request) ==
        HV_EFI_RUNTIME_SCAN_SUCCESS &&
        mailbox.State == HV_EFI_RUNTIME_SCAN_MAILBOX_REQUEST_READY,
        "mailbox request publishes last");

    fixture.State.Busy = 1;
    HV_EFI_RUNTIME_SCAN_RESPONSE response = {};
    TEST_ASSERT(HvEfiRuntimeScannerExecute(
        &fixture.State, &fixture.Window, &fixture.Range, 1, 12,
        &request, &response) == HV_EFI_RUNTIME_SCAN_BUSY,
        "concurrent scan is rejected without mutation");
    fixture.State.Busy = 0;

    mailbox.Response = response;
    mailbox.Response.Generation = 12;
    mailbox.Response.Sequence = 44;
    mailbox.Response.Size = sizeof(mailbox.Response);
    mailbox.Response.Version = HV_EFI_RUNTIME_SCAN_VERSION;
    mailbox.Response.ResultCapacity = request.MaxResults;
    mailbox.State = HV_EFI_RUNTIME_SCAN_MAILBOX_RESPONSE_READY;
    TEST_ASSERT(HvEfiRuntimeScannerReadResponse(
        &mailbox, sizeof(mailbox), 12, 44, &response) ==
        HV_EFI_RUNTIME_SCAN_BUSY,
        "mailbox response is read with exact identity");
    return 0;
}

} // namespace

int main()
{
    if (TestFirstAndAllMatches() != 0 ||
        TestBudgetContinuationAndReplay() != 0 ||
        TestSingleAndCapacity() != 0 ||
        TestMailboxHelpersAndBusy() != 0) {
        return 1;
    }
    puts("PASS: authenticated runtime scanner command contracts");
    return 0;
}
