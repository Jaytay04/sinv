#include "runtime_analysis.h"

#include <stdio.h>
#include <string.h>

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s\n", message); \
            return 1; \
        } \
    } while (0)

namespace {

alignas(4096) uint8_t g_guest[16 * 1024 * 1024];
alignas(4096) uint64_t g_hostTables[
    HV_EFI_SCANNER_REQUIRED_PAGE_TABLE_PAGES * 512];
HV_EFI_ANALYSIS_STATE g_state;
HV_EFI_ANALYSIS_RESPONSE g_response;
HV_EFI_SCANNER_WINDOW *g_activeWindow;

void Invalidate(void *, uint64_t)
{
    const uint64_t entry =
        g_hostTables[HV_EFI_SCANNER_PT_PAGE * 512];
    const uint64_t physical = entry & UINT64_C(0x000ffffffffff000);
    if (g_activeWindow != nullptr && entry != 0) {
        g_activeWindow->VirtualBase = static_cast<uint64_t>(
            reinterpret_cast<uintptr_t>(g_guest + physical));
    }
}

void Store64(uint64_t physical, uint64_t value)
{
    memcpy(g_guest + physical, &value, sizeof(value));
}

void BuildGuest()
{
    memset(g_guest, 0, sizeof(g_guest));
    Store64(0x1000, 0x2000 | 7);
    Store64(0x2000, 0x3000 | 7);
    Store64(0x3000, 0x4000 | 7);
    Store64(0x4000 + 16 * 8, 0x8000 | 5 | (UINT64_C(1) << 63));
    Store64(0x4000 + 17 * 8, 0x9000 | 5);
    g_guest[0x8000] = 'M';
    g_guest[0x8001] = 'Z';
    g_guest[0x8000 + 0x3c] = 0x40;
    g_guest[0x8000 + 0x40] = 'P';
    g_guest[0x8000 + 0x41] = 'E';
    g_guest[0x8000 + 0x58] = 0x0b;
    g_guest[0x8000 + 0x59] = 0x02;
    g_guest[0x8000 + 0x90] = 0x00;
    g_guest[0x8000 + 0x91] = 0x20;
    const uint8_t pattern[] = {0xde, 0xad, 0x44, 0xef};
    memcpy(g_guest + 0x8000 + 0x300, pattern, sizeof(pattern));
    Store64(0x8000 + 0x100, 0x10120);
    Store64(0x8000 + 0x120, 0);
    const float vector[3] = {1.0f, 2.0f, 3.0f};
    memcpy(g_guest + 0x8000 + 0x200, vector, sizeof(vector));
}

void MakeWindow(HV_EFI_SCANNER_WINDOW *window)
{
    memset(g_hostTables, 0, sizeof(g_hostTables));
    (void)HvEfiScannerPrepareHostPageTables(
        g_hostTables, HV_EFI_SCANNER_REQUIRED_PAGE_TABLE_PAGES,
        UINT64_C(0x02000000));
    memset(window, 0, sizeof(*window));
    g_activeWindow = window;
    (void)HvEfiScannerInitializeWindow(
        g_hostTables, HV_EFI_SCANNER_REQUIRED_PAGE_TABLE_PAGES,
        UINT64_C(0x02000000), 1, 1, Invalidate, nullptr, window);
}

HV_EFI_ANALYSIS_REQUEST Request(uint16_t operation, uint64_t sequence)
{
    HV_EFI_ANALYSIS_REQUEST request{};
    request.Size = sizeof(request);
    request.Version = HV_EFI_ANALYSIS_VERSION;
    request.Operation = operation;
    request.Sequence = sequence;
    return request;
}

HV_EFI_ANALYSIS_STATUS Execute(
    HV_EFI_RUNTIME_PROCESS_STATE &processes,
    HV_EFI_SCANNER_WINDOW &window,
    const HV_EFI_ANALYSIS_REQUEST &request)
{
    const HV_EFI_SCANNER_RAM_RANGE allowed = {0, sizeof(g_guest)};
    return HvEfiRuntimeAnalysisExecute(
        &g_state, &processes, &window, &allowed, 1, 7, 0, 48,
        &request, &g_response);
}

int TestSession()
{
    BuildGuest();
    memset(&g_state, 0, sizeof(g_state));
    g_state.Marker = HV_EFI_ANALYSIS_STATE_MARKER;
    HV_EFI_RUNTIME_PROCESS_STATE processes{};
    processes.Marker = HV_EFI_RUNTIME_PROCESS_STATE_MARKER;
    processes.StoredCount = 1;
    processes.Records[0].ProcessId = 42;
    processes.Records[0].DirectoryTableBase = 0x1005;
    HV_EFI_SCANNER_WINDOW window{};
    MakeWindow(&window);

    auto invalidStart = Request(HV_EFI_ANALYSIS_START, 1);
    invalidStart.ProcessId = 42;
    invalidStart.RegionCount = 1;
    invalidStart.Regions[0].BaseVirtualAddress = 0;
    invalidStart.Regions[0].Size = 0x1000;
    TEST_ASSERT(Execute(processes, window, invalidStart) ==
                    HV_EFI_ANALYSIS_INVALID_REQUEST,
                "analysis start validates inline region evidence");

    auto start = Request(HV_EFI_ANALYSIS_START, 1);
    start.ProcessId = 42;
    const HV_EFI_ANALYSIS_STATUS startStatus =
        Execute(processes, window, start);
    TEST_ASSERT(startStatus == HV_EFI_ANALYSIS_SUCCESS &&
        g_response.SessionActive == 1 && g_response.ProcessId == 42 &&
        g_response.TotalRegionCount == 2 &&
        g_response.Regions[0].BaseVirtualAddress == 0x10000 &&
        g_response.Regions[0].Size == 0x1000 &&
        g_response.Regions[0].RegionType == HV_EFI_ANALYSIS_REGION_IMAGE &&
        g_response.Regions[1].RegionType == HV_EFI_ANALYSIS_REGION_IMAGE &&
        (g_response.Regions[0].Protection &
         (HV_EFI_ANALYSIS_PROTECTION_READ |
          HV_EFI_ANALYSIS_PROTECTION_USER)) != 0,
        "analysis start classifies an image with a read-only NX header");

    auto configure = Request(HV_EFI_ANALYSIS_CONFIGURE, 2);
    configure.SignatureCount = 1;
    configure.RegionFilter = HV_EFI_ANALYSIS_REGION_FILTER_ALL;
    memcpy(configure.Signatures[0].Identifier, "needle", 7);
    configure.Signatures[0].Length = 4;
    configure.Signatures[0].Pattern[0] = 0xde;
    configure.Signatures[0].Pattern[1] = 0xad;
    configure.Signatures[0].Pattern[2] = 0;
    configure.Signatures[0].Pattern[3] = 0xef;
    configure.Signatures[0].Mask[0] = 0xff;
    configure.Signatures[0].Mask[1] = 0xff;
    configure.Signatures[0].Mask[2] = 0;
    configure.Signatures[0].Mask[3] = 0xff;
    configure.Signatures[0].Flags = HV_EFI_ANALYSIS_SIGNATURE_READABLE;
    TEST_ASSERT(Execute(processes, window, configure) ==
                    HV_EFI_ANALYSIS_SUCCESS,
                "signature configuration accepted");

    auto scan = Request(HV_EFI_ANALYSIS_SCAN, 3);
    scan.ScanMode = HV_EFI_ANALYSIS_SCAN_ALL;
    scan.ResultLimit = 8;
    scan.MaxBytes = 0x2000;
    TEST_ASSERT(Execute(processes, window, scan) == HV_EFI_ANALYSIS_SUCCESS &&
        g_response.TotalResultCount == 1 &&
        g_response.Results[0].GuestVirtualAddress == 0x10300 &&
        g_response.Results[0].HostPhysicalAddress == 0x8300 &&
        g_response.Results[0].OwningProcessId == 42 &&
        g_response.Results[0].MatchConfidence == 87,
        "wildcard BMH scan reports virtual and translated physical address");

    auto excessiveTimeout = Request(HV_EFI_ANALYSIS_SCAN, 4);
    excessiveTimeout.ScanMode = HV_EFI_ANALYSIS_SCAN_FIRST;
    excessiveTimeout.TimeoutTsc = HV_EFI_ANALYSIS_MAX_TIMEOUT_TSC + 1;
    TEST_ASSERT(Execute(processes, window, excessiveTimeout) ==
                    HV_EFI_ANALYSIS_INVALID_REQUEST,
                "caller cannot disable the bounded scan timeout");

    const uint8_t secondPattern[] = {0xde, 0xad, 0x55, 0xef};
    memcpy(g_guest + 0x9000 + 0x300, secondPattern, sizeof(secondPattern));
    auto single = Request(HV_EFI_ANALYSIS_SCAN, 4);
    single.ScanMode = HV_EFI_ANALYSIS_SCAN_SINGLE;
    single.ResultLimit = 8;
    single.MaxBytes = 0x2000;
    TEST_ASSERT(Execute(processes, window, single) ==
                    HV_EFI_ANALYSIS_MULTIPLE_MATCHES &&
                g_response.SessionActive == 1 &&
                g_response.TotalResultCount == 2,
                "single mode reports multiple matches without losing session");

    auto traverse = Request(HV_EFI_ANALYSIS_TRAVERSE, 5);
    traverse.Address = 0x10100;
    traverse.StructureType = HV_EFI_ANALYSIS_TRAVERSE_SINGLY_LINKED;
    traverse.NextOffset = 0;
    traverse.NodeLimit = 8;
    TEST_ASSERT(Execute(processes, window, traverse) ==
                    HV_EFI_ANALYSIS_SUCCESS &&
        g_response.TotalNodeCount == 2 &&
        g_response.Nodes[0] == 0x10100 &&
        g_response.Nodes[1] == 0x10120,
        "bounded singly-linked traversal follows guest pointers");

    auto extract = Request(HV_EFI_ANALYSIS_EXTRACT, 6);
    extract.Address = 0x10200;
    extract.FieldType = HV_EFI_ANALYSIS_FIELD_VECTOR3;
    memcpy(extract.FieldName, "position", 9);
    TEST_ASSERT(Execute(processes, window, extract) ==
                    HV_EFI_ANALYSIS_SUCCESS &&
        g_response.TotalFieldCount == 1 &&
        g_response.Fields[0].Value.Vec3[0] == 1.0f &&
        g_response.Fields[0].Value.Vec3[1] == 2.0f &&
        g_response.Fields[0].Value.Vec3[2] == 3.0f,
        "typed vector extraction preserves values");

    TEST_ASSERT(Execute(processes, window, extract) ==
                    HV_EFI_ANALYSIS_REPLAY_REJECTED,
                "analysis replay is rejected");
    auto stop = Request(HV_EFI_ANALYSIS_STOP, 7);
    TEST_ASSERT(Execute(processes, window, stop) == HV_EFI_ANALYSIS_SUCCESS &&
        g_response.SessionActive == 0 && g_response.ProcessId == 0,
        "analysis stop wipes session state");
    return 0;
}

} // namespace

int main()
{
    if (TestSession() != 0) return 1;
    puts("PASS: dynamic guest-memory analysis contracts");
    return 0;
}
