#include "gpu_guest_filter.h"

#include <stdio.h>
#include <string.h>

#define TEST_CASES 7u

typedef struct TEST_PCI_CONTEXT {
    uint32_t Calls;
    uint32_t Fail;
    uint32_t ConfigAddress;
    uint16_t DataPort;
    uint32_t Width;
    uint32_t IsInput;
    uint32_t Value;
} TEST_PCI_CONTEXT;

static HV_GPU_GUEST_FILTER_POLICY gPolicy;

#define CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: gpu-guest-filter: %s\n", (message)); \
            return 1; \
        } \
    } while (0)

static uint64_t IoQualification(uint16_t port, uint32_t width,
                                uint32_t isInput, uint64_t flags)
{
    uint32_t sizeEncoding = width == 1u ? 0u : (width == 2u ? 1u : 3u);
    return ((uint64_t)port << 16) | (uint64_t)sizeEncoding |
           (isInput != 0u ? UINT64_C(8) : UINT64_C(0)) | flags;
}

static uint32_t TestPciAccess(void *context, uint32_t configAddress,
                              uint16_t dataPort, uint32_t widthBytes,
                              uint32_t isInput, uint32_t *value)
{
    TEST_PCI_CONTEXT *pci = (TEST_PCI_CONTEXT *)context;
    ++pci->Calls;
    pci->ConfigAddress = configAddress;
    pci->DataPort = dataPort;
    pci->Width = widthBytes;
    pci->IsInput = isInput;
    if (pci->Fail != 0u) return 0u;
    if (isInput != 0u) *value = pci->Value;
    else pci->Value = *value;
    return 1u;
}

static void ResetPolicy(void)
{
    gPolicy.Size = (uint32_t)sizeof(gPolicy);
    gPolicy.Segment = 0u;
    gPolicy.Bus = 2u;
    gPolicy.Device = 3u;
    gPolicy.Function = 1u;
    gPolicy.Reserved[0] = 0u;
    gPolicy.Reserved[1] = 0u;
    gPolicy.Reserved[2] = 0u;
    gPolicy.EcamPage = UINT64_C(0xe0000000);
    gPolicy.Bar0.Base = UINT64_C(0xf0000000);
    gPolicy.Bar0.Bytes = UINT64_C(0x01000000);
    gPolicy.Bar2.Base = UINT64_C(0xe1000000);
    gPolicy.Bar2.Bytes = UINT64_C(0x02000000);
}

static uint32_t TargetAddress(uint32_t registerOffset)
{
    return UINT32_C(0x80000000) |
           ((uint32_t)gPolicy.Bus << 16) |
           ((uint32_t)gPolicy.Device << 11) |
           ((uint32_t)gPolicy.Function << 8) |
           (registerOffset & UINT32_C(0xfc));
}

static uint32_t OtherAddress(uint32_t registerOffset)
{
    return UINT32_C(0x80000000) | (UINT32_C(3) << 16) |
           (UINT32_C(4) << 11) | (UINT32_C(2) << 8) |
           (registerOffset & UINT32_C(0xfc));
}

static int TestPolicyBounds(void)
{
    HV_GPU_GUEST_FILTER_POLICY policy;
    ResetPolicy();
    CHECK(HvGpuGuestFilterValidatePolicy(&gPolicy) == 1,
          "valid segment-zero ECAM and BAR policy accepted");

    policy = gPolicy;
    policy.Segment = 1u;
    CHECK(HvGpuGuestFilterValidatePolicy(&policy) == 0,
          "nonzero PCI segment rejected");
    policy = gPolicy;
    policy.Device = 32u;
    CHECK(HvGpuGuestFilterValidatePolicy(&policy) == 0,
          "device number overflow rejected");
    policy = gPolicy;
    policy.EcamPage += 1u;
    CHECK(HvGpuGuestFilterValidatePolicy(&policy) == 0,
          "unaligned ECAM page rejected");
    policy = gPolicy;
    policy.Bar0.Base = policy.EcamPage;
    CHECK(HvGpuGuestFilterValidatePolicy(&policy) == 0,
          "overlapping ECAM and BAR rejected");
    policy = gPolicy;
    policy.Bar2.Base = policy.Bar0.Base + policy.Bar0.Bytes -
                       HV_GPU_GUEST_FILTER_PAGE_SIZE;
    CHECK(HvGpuGuestFilterValidatePolicy(&policy) == 0,
          "overlapping BAR windows rejected");
    policy = gPolicy;
    policy.Bar0.Base = HV_GPU_GUEST_FILTER_PHYS_LIMIT -
                       HV_GPU_GUEST_FILTER_PAGE_SIZE;
    policy.Bar0.Bytes = UINT64_C(0x2000);
    CHECK(HvGpuGuestFilterValidatePolicy(&policy) == 0,
          "BAR range above 39-bit address limit rejected");
    return 0;
}

static int TestIoBitmapArmAndReadback(void)
{
    uint8_t bitmapA[HV_GPU_GUEST_FILTER_IO_BITMAP_BYTES];
    uint8_t bitmapB[HV_GPU_GUEST_FILTER_IO_BITMAP_BYTES];
    uint8_t originalA[HV_GPU_GUEST_FILTER_IO_BITMAP_BYTES];
    uint8_t originalB[HV_GPU_GUEST_FILTER_IO_BITMAP_BYTES];
    uint8_t expectedA[HV_GPU_GUEST_FILTER_IO_BITMAP_BYTES];
    uint32_t port;

    for (port = 0u; port < HV_GPU_GUEST_FILTER_IO_BITMAP_BYTES; ++port) {
        bitmapA[port] = (uint8_t)(port * 7u + 3u);
        bitmapB[port] = (uint8_t)(port * 11u + 1u);
    }
    memcpy(originalA, bitmapA, sizeof(bitmapA));
    memcpy(originalB, bitmapB, sizeof(bitmapB));
    memcpy(expectedA, bitmapA, sizeof(bitmapA));

    for (port = HV_GPU_GUEST_FILTER_PCI_CONFIG_FIRST;
         port <= HV_GPU_GUEST_FILTER_PCI_CONFIG_LAST; ++port) {
        expectedA[(uint32_t)port >> 3] = (uint8_t)(
            expectedA[(uint32_t)port >> 3] |
            (uint8_t)(UINT8_C(1) << ((uint32_t)port & UINT32_C(7))));
    }
    CHECK(HvGpuGuestFilterArmIoBitmaps(bitmapA, bitmapB,
              HV_GPU_GUEST_FILTER_IO_BITMAP_BYTES) == 1,
          "bitmap arming succeeds with exact ABI");
    CHECK(memcmp(bitmapA, expectedA, sizeof(bitmapA)) == 0,
          "bitmap A changes only the required CF8-CFF bits");
    CHECK(memcmp(bitmapB, originalB, sizeof(bitmapB)) == 0,
          "bitmap B remains byte-for-byte unchanged");
    CHECK(HvGpuGuestFilterIoBitmapsValid(bitmapA, bitmapB,
              HV_GPU_GUEST_FILTER_IO_BITMAP_BYTES) == 1,
          "readback sees all eight PCI ports intercepted");

    memcpy(bitmapA, originalA, sizeof(bitmapA));
    memcpy(bitmapB, originalB, sizeof(bitmapB));
    CHECK(HvGpuGuestFilterArmIoBitmaps(bitmapA, bitmapB,
              HV_GPU_GUEST_FILTER_IO_BITMAP_BYTES - 1u) == 0,
          "wrong bitmap size rejected");
    CHECK(memcmp(bitmapA, originalA, sizeof(bitmapA)) == 0 &&
          memcmp(bitmapB, originalB, sizeof(bitmapB)) == 0,
          "invalid bitmap call has no side effects");
    CHECK(HvGpuGuestFilterArmIoBitmaps(bitmapA, bitmapA,
              HV_GPU_GUEST_FILTER_IO_BITMAP_BYTES) == 0 &&
          memcmp(bitmapA, originalA, sizeof(bitmapA)) == 0,
          "aliased maps are rejected without changing any bits");
    return 0;
}

static int TestTargetConfigHidden(void)
{
    HV_GPU_GUEST_FILTER_VCPU vcpu = { 0u };
    TEST_PCI_CONTEXT pci = { 0 };
    uint64_t rax;
    uint32_t advance;

    ResetPolicy();
    vcpu.ConfigAddress = TargetAddress(UINT32_C(0x40));
    rax = UINT64_C(0x1122334455667788);
    advance = 99u;
    CHECK(HvGpuGuestFilterHandleIo(&gPolicy, &vcpu,
              IoQualification(HV_GPU_GUEST_FILTER_PCI_DATA_FIRST, 4u, 1u, 0u),
              1u, &rax, &advance, TestPciAccess, &pci) ==
              HV_GPU_GUEST_FILTER_IO_HANDLED,
          "target dword read is handled locally");
    CHECK(rax == UINT64_C(0x00000000ffffffff) && advance == 1u &&
          pci.Calls == 0u,
          "target reads return all ones without a physical callback");

    rax = UINT64_C(0x123456789abcde55);
    CHECK(HvGpuGuestFilterHandleIo(&gPolicy, &vcpu,
              IoQualification(HV_GPU_GUEST_FILTER_PCI_DATA_FIRST, 2u, 0u, 0u),
              1u, &rax, &advance, TestPciAccess, &pci) ==
              HV_GPU_GUEST_FILTER_IO_HANDLED,
          "target word write is discarded locally");
    CHECK(rax == UINT64_C(0x123456789abcde55) && advance == 1u &&
          pci.Calls == 0u,
          "target writes preserve guest registers and never reach hardware");

    vcpu.ConfigAddress = TargetAddress(UINT32_C(0xa4));
    rax = UINT64_C(0xaabbccdd11223344);
    CHECK(HvGpuGuestFilterHandleIo(&gPolicy, &vcpu,
              IoQualification(UINT16_C(0x0cfa), 2u, 1u, 0u),
              0u, &rax, &advance, TestPciAccess, &pci) ==
              HV_GPU_GUEST_FILTER_IO_HANDLED,
          "partial selector read supports a word at CFA");
    CHECK((rax & UINT64_C(0xffff)) ==
              ((uint64_t)(vcpu.ConfigAddress >> 16) & UINT64_C(0xffff)) &&
          ((rax >> 16) & UINT64_C(0xffff)) == UINT64_C(0x1122) &&
          (rax >> 32) == UINT64_C(0xaabbccdd) && pci.Calls == 0u,
          "selector word read updates only AX and returns stored bytes");
    return 0;
}

static int TestSelectorAndCf9(void)
{
    HV_GPU_GUEST_FILTER_VCPU vcpu = { 0u };
    TEST_PCI_CONTEXT pci = { 0 };
    uint32_t selector;
    uint64_t rax;
    uint32_t advance = 0u;

    ResetPolicy();
    selector = TargetAddress(UINT32_C(0x88));
    rax = (uint64_t)selector;
    CHECK(HvGpuGuestFilterHandleIo(&gPolicy, &vcpu,
              IoQualification(HV_GPU_GUEST_FILTER_PCI_CONFIG_FIRST,
                              4u, 0u, 0u),
              1u, &rax, &advance, TestPciAccess, &pci) ==
              HV_GPU_GUEST_FILTER_IO_HANDLED,
          "dword CF8 selector write is virtualized");
    CHECK(vcpu.ConfigAddress == selector && pci.Calls == 0u && advance == 1u,
          "CF8 selector is saved per vCPU and never forwarded");

    rax = UINT64_C(0xdeadbeef);
    CHECK(HvGpuGuestFilterHandleIo(&gPolicy, &vcpu,
              IoQualification(HV_GPU_GUEST_FILTER_PCI_CONFIG_FIRST,
                              4u, 1u, 0u),
              1u, &rax, &advance, TestPciAccess, &pci) ==
              HV_GPU_GUEST_FILTER_IO_HANDLED,
          "dword CF8 selector read is virtualized");
    CHECK(rax == (uint64_t)selector && pci.Calls == 0u,
          "selector read returns the vCPU-local selector");

    rax = UINT64_C(0x12345678);
    CHECK(HvGpuGuestFilterHandleIo(&gPolicy, &vcpu,
              IoQualification(UINT16_C(0x0cf9), 1u, 0u, 0u),
              1u, &rax, &advance, TestPciAccess, &pci) ==
              HV_GPU_GUEST_FILTER_IO_INJECT_GP,
          "standalone CF9 OUT cannot update selector or reset platform");
    CHECK(vcpu.ConfigAddress == selector && rax == UINT64_C(0x12345678) &&
          advance == 0u && pci.Calls == 0u,
          "CF9 rejection has no register or physical side effects");

    rax = UINT64_C(0xabcdef0100000000) | UINT64_C(0x5a);
    CHECK(HvGpuGuestFilterHandleIo(&gPolicy, &vcpu,
              IoQualification(HV_GPU_GUEST_FILTER_PCI_CONFIG_FIRST,
                              1u, 0u, 0u),
              1u, &rax, &advance, TestPciAccess, &pci) ==
              HV_GPU_GUEST_FILTER_IO_HANDLED,
          "byte selector write is supported");
    CHECK(vcpu.ConfigAddress == ((selector & UINT32_C(0xffffff00)) |
                                 UINT32_C(0x5a)),
          "byte selector write changes only its selected byte");
    return 0;
}

static int TestNonTargetForwardingAndHostBridgeGuard(void)
{
    HV_GPU_GUEST_FILTER_VCPU vcpu = { 0u };
    TEST_PCI_CONTEXT pci = { 0 };
    uint64_t rax;
    uint32_t advance;

    ResetPolicy();
    vcpu.ConfigAddress = OtherAddress(UINT32_C(0x20));
    pci.Value = UINT32_C(0x89abcdef);
    rax = UINT64_C(0xfeedface12345678);
    CHECK(HvGpuGuestFilterHandleIo(&gPolicy, &vcpu,
              IoQualification(HV_GPU_GUEST_FILTER_PCI_DATA_FIRST,
                              4u, 1u, 0u),
              0u, &rax, &advance, TestPciAccess, &pci) ==
              HV_GPU_GUEST_FILTER_IO_HANDLED,
          "non-target dword config read is forwarded");
    CHECK(pci.Calls == 1u && pci.ConfigAddress == vcpu.ConfigAddress &&
          pci.DataPort == HV_GPU_GUEST_FILTER_PCI_DATA_FIRST &&
          pci.Width == 4u && pci.IsInput == 1u &&
          rax == UINT64_C(0xfeedface89abcdef) && advance == 1u,
          "callback arguments and legacy-mode RAX preservation are exact");

    pci.Calls = 0u;
    rax = UINT64_C(0x1111222233334444);
    CHECK(HvGpuGuestFilterHandleIo(&gPolicy, &vcpu,
              IoQualification(HV_GPU_GUEST_FILTER_PCI_DATA_FIRST,
                              4u, 1u, 0u),
              1u, &rax, &advance, TestPciAccess, &pci) ==
              HV_GPU_GUEST_FILTER_IO_HANDLED,
          "non-target dword read is supported in long mode");
    CHECK(rax == UINT64_C(0x0000000089abcdef),
          "long-mode IN EAX zero extends RAX");

    pci.Calls = 0u;
    pci.Value = UINT32_C(0x89abcdef);
    rax = UINT64_C(0xfeedface12345678);
    CHECK(HvGpuGuestFilterHandleIo(&gPolicy, &vcpu,
              IoQualification(HV_GPU_GUEST_FILTER_PCI_DATA_FIRST,
                              1u, 1u, 0u),
              1u, &rax, &advance, TestPciAccess, &pci) ==
              HV_GPU_GUEST_FILTER_IO_HANDLED && pci.Width == 1u &&
          rax == UINT64_C(0xfeedface123456ef),
          "byte config read forwards width and preserves other RAX bytes");

    pci.Calls = 0u;
    rax = UINT64_C(0xfeedface12345678);
    CHECK(HvGpuGuestFilterHandleIo(&gPolicy, &vcpu,
              IoQualification(HV_GPU_GUEST_FILTER_PCI_DATA_FIRST,
                              2u, 1u, 0u),
              1u, &rax, &advance, TestPciAccess, &pci) ==
              HV_GPU_GUEST_FILTER_IO_HANDLED && pci.Width == 2u &&
          rax == UINT64_C(0xfeedface1234cdef),
          "word config read forwards width and preserves other RAX bytes");

    vcpu.ConfigAddress = OtherAddress(UINT32_C(0x50));
    pci.Calls = 0u;
    rax = UINT64_C(0x1234);
    CHECK(HvGpuGuestFilterHandleIo(&gPolicy, &vcpu,
              IoQualification(HV_GPU_GUEST_FILTER_PCI_DATA_FIRST,
                              4u, 0u, 0u),
              1u, &rax, &advance, TestPciAccess, &pci) ==
              HV_GPU_GUEST_FILTER_IO_HANDLED,
          "non-target device writes continue through callback");
    CHECK(pci.Calls == 1u && pci.IsInput == 0u &&
          pci.Value == UINT32_C(0x1234),
          "non-target write callback receives guest value");

    vcpu.ConfigAddress = UINT32_C(0x80000000) | UINT32_C(0x50);
    pci.Calls = 0u;
    rax = UINT64_C(0x5555);
    CHECK(HvGpuGuestFilterHandleIo(&gPolicy, &vcpu,
              IoQualification(HV_GPU_GUEST_FILTER_PCI_DATA_FIRST,
                              4u, 0u, 0u),
              1u, &rax, &advance, TestPciAccess, &pci) ==
              HV_GPU_GUEST_FILTER_IO_INJECT_GP,
          "all host-bridge config writes fail closed");
    CHECK(pci.Calls == 0u && rax == UINT64_C(0x5555) && advance == 0u,
          "host-bridge write rejection never reaches physical callback");

    vcpu.ConfigAddress = UINT32_C(0x80000000) | UINT32_C(0x50);
    pci.Calls = 0u;
    pci.Value = UINT32_C(0x12345678);
    rax = 0u;
    CHECK(HvGpuGuestFilterHandleIo(&gPolicy, &vcpu,
              IoQualification(HV_GPU_GUEST_FILTER_PCI_DATA_FIRST,
                              4u, 1u, 0u),
              1u, &rax, &advance, TestPciAccess, &pci) ==
              HV_GPU_GUEST_FILTER_IO_HANDLED && pci.Calls == 1u,
          "host-bridge config reads remain forwarded");
    return 0;
}

static int TestUnsupportedAndFailureAreAtomic(void)
{
    HV_GPU_GUEST_FILTER_VCPU vcpu = { 0u };
    TEST_PCI_CONTEXT pci = { 0 };
    HV_GPU_GUEST_FILTER_POLICY invalid;
    uint64_t rax;
    uint32_t advance;
    static const uint64_t rejected[] = {
        UINT64_C(0x10), /* string */
        UINT64_C(0x20), /* REP */
        UINT64_C(0x80), /* reserved */
        UINT64_C(0x40), /* immediate port cannot encode CF8-CFF */
        UINT64_C(0x2)   /* reserved width encoding */
    };
    uint32_t index;

    ResetPolicy();
    vcpu.ConfigAddress = TargetAddress(UINT32_C(0x40));
    for (index = 0u; index <
         (uint32_t)(sizeof(rejected) / sizeof(rejected[0])); ++index) {
        uint64_t flags = rejected[index] == UINT64_C(0x2) ? 0u :
                         rejected[index];
        uint64_t qualification = rejected[index] == UINT64_C(0x2) ?
            (((uint64_t)HV_GPU_GUEST_FILTER_PCI_DATA_FIRST << 16) |
             UINT64_C(2)) :
            IoQualification(HV_GPU_GUEST_FILTER_PCI_DATA_FIRST, 4u, 0u,
                            flags);
        uint32_t oldSelector = vcpu.ConfigAddress;
        rax = UINT64_C(0x1020304050607080);
        advance = 55u;
        CHECK(HvGpuGuestFilterHandleIo(&gPolicy, &vcpu, qualification,
                  1u, &rax, &advance, TestPciAccess, &pci) ==
                  HV_GPU_GUEST_FILTER_IO_INJECT_GP,
              "string, REP, reserved, and invalid width accesses inject GP");
        CHECK(vcpu.ConfigAddress == oldSelector &&
              rax == UINT64_C(0x1020304050607080) && advance == 0u &&
              pci.Calls == 0u,
              "unsupported operations have no partial side effects");
    }

    rax = UINT64_C(0x9999888877776666);
    CHECK(HvGpuGuestFilterHandleIo(&gPolicy, &vcpu,
              IoQualification(UINT16_C(0x0cf7), 2u, 1u, 0u),
              1u, &rax, &advance, TestPciAccess, &pci) ==
              HV_GPU_GUEST_FILTER_IO_INJECT_GP,
          "access crossing into CF8 is rejected");
    CHECK(rax == UINT64_C(0x9999888877776666) && advance == 0u,
          "cross-boundary rejection leaves registers unchanged");

    CHECK(HvGpuGuestFilterHandleIo(&gPolicy, &vcpu,
              IoQualification(UINT16_C(0x0cff), 2u, 1u, 0u),
              1u, &rax, &advance, TestPciAccess, &pci) ==
              HV_GPU_GUEST_FILTER_IO_INJECT_GP,
          "access crossing PCI config-data boundary is rejected");

    vcpu.ConfigAddress = OtherAddress(UINT32_C(0x20));
    pci.Fail = 1u;
    rax = UINT64_C(0xabcdef0123456789);
    CHECK(HvGpuGuestFilterHandleIo(&gPolicy, &vcpu,
              IoQualification(HV_GPU_GUEST_FILTER_PCI_DATA_FIRST,
                              4u, 1u, 0u),
              1u, &rax, &advance, TestPciAccess, &pci) ==
              HV_GPU_GUEST_FILTER_IO_FAILED,
          "failed physical callback is reported for parent latch");
    CHECK(rax == UINT64_C(0xabcdef0123456789) && advance == 0u,
          "callback failure does not commit guest register output");

    invalid = gPolicy;
    invalid.Bar2.Bytes = 0u;
    rax = UINT64_C(0x12345678);
    CHECK(HvGpuGuestFilterHandleIo(&invalid, &vcpu,
              IoQualification(HV_GPU_GUEST_FILTER_PCI_CONFIG_FIRST,
                              4u, 0u, 0u),
              1u, &rax, &advance, TestPciAccess, &pci) ==
              HV_GPU_GUEST_FILTER_IO_FAILED,
          "invalid immutable policy fails closed");
    CHECK(vcpu.ConfigAddress == OtherAddress(UINT32_C(0x20)) &&
          rax == UINT64_C(0x12345678) && advance == 0u,
          "invalid policy causes no filter side effects");
    return 0;
}

static int TestProtectedMmioRanges(void)
{
    HV_GPU_GUEST_FILTER_POLICY invalid;
    ResetPolicy();
    CHECK(HvGpuGuestFilterRangeProtected(&gPolicy, gPolicy.EcamPage,
              HV_GPU_GUEST_FILTER_PAGE_SIZE) == 1,
          "target ECAM page is protected");
    CHECK(HvGpuGuestFilterPageProtected(&gPolicy, gPolicy.Bar0.Base) == 1,
          "BAR0 first page is protected");
    CHECK(HvGpuGuestFilterPageProtected(&gPolicy,
              gPolicy.Bar2.Base + gPolicy.Bar2.Bytes -
                  HV_GPU_GUEST_FILTER_PAGE_SIZE) == 1,
          "BAR2 last page is protected");
    CHECK(HvGpuGuestFilterRangeProtected(&gPolicy,
              gPolicy.Bar0.Base - HV_GPU_GUEST_FILTER_PAGE_SIZE,
              HV_GPU_GUEST_FILTER_PAGE_SIZE * UINT64_C(2)) == 1,
          "partial overlap is protected");
    CHECK(HvGpuGuestFilterPageProtected(&gPolicy,
              gPolicy.EcamPage + HV_GPU_GUEST_FILTER_PAGE_SIZE) == 0,
          "adjacent unowned page remains unprotected");
    CHECK(HvGpuGuestFilterRangeProtected(&gPolicy,
              HV_GPU_GUEST_FILTER_PHYS_LIMIT - UINT64_C(0x1000),
              UINT64_C(0x2000)) == -1,
          "overflow or out-of-range query fails closed");
    CHECK(HvGpuGuestFilterPageProtected(&gPolicy,
              gPolicy.Bar0.Base + 1u) == -1,
          "unaligned page query is invalid");
    invalid = gPolicy;
    invalid.Bar0.Base = invalid.EcamPage;
    CHECK(HvGpuGuestFilterRangeProtected(&invalid, gPolicy.Bar0.Base,
              HV_GPU_GUEST_FILTER_PAGE_SIZE) == -1,
          "invalid policy cannot authorize an EPT mapping");
    return 0;
}

int HvGpuGuestFilterRunTests(void)
{
    static int (*const cases[TEST_CASES])(void) = {
        TestPolicyBounds,
        TestIoBitmapArmAndReadback,
        TestTargetConfigHidden,
        TestSelectorAndCf9,
        TestNonTargetForwardingAndHostBridgeGuard,
        TestUnsupportedAndFailureAreAtomic,
        TestProtectedMmioRanges
    };
    uint32_t index;

    for (index = 0u; index < TEST_CASES; ++index) {
        if (cases[index]() != 0) {
            fprintf(stderr, "gpu_guest_filter_test: case %u failed\n",
                    (unsigned)index);
            return 1;
        }
    }
    printf("gpu_guest_filter_model_tests: %u cases, PASS\n",
           (unsigned)TEST_CASES);
    return 0;
}
