#include "test_common.h"
#include "vmx_ept_invalidation.h"

#include <stddef.h>

#define RAW_REQUIRED ((UINT64_C(1) << 6) | (UINT64_C(1) << 14) | \
    (UINT64_C(1) << 16) | (UINT64_C(1) << 20) | \
    (UINT64_C(1) << 25) | (UINT64_C(1) << 26))
#define RAW_OPTIONAL ((UINT64_C(1) << 8) | (UINT64_C(1) << 21))

_Static_assert(VMX_EPT_INVALIDATION_SINGLE_CONTEXT == 1,
    "single-context type");
_Static_assert(VMX_EPT_INVALIDATION_ALL_CONTEXTS == 2,
    "all-context type");
_Static_assert(sizeof(VMX_EPT_INVALIDATION_REQUEST) == 32,
    "invalidation request ABI");
_Static_assert(offsetof(VMX_EPT_INVALIDATION_REQUEST, Pointer) == 8,
    "pointer offset");
_Static_assert(sizeof(VMX_EPT_INVALIDATION_DESCRIPTOR) == 16,
    "INVEPT descriptor ABI");

static VMX_EPT_CAPABILITIES Caps(uint64_t raw)
{
    VMX_EPT_CAPABILITIES caps = { 0 };
    TEST_ASSERT_INT_EQ(VmxEptDecodeCapabilities(raw, &caps),
        VMX_EPT_STATUS_OK, "decode capabilities");
    return caps;
}

static VMX_EPT_INVALIDATION_REQUEST Single(uint64_t root)
{
    VMX_EPT_INVALIDATION_REQUEST request = { 0 };
    request.Type = VMX_EPT_INVALIDATION_SINGLE_CONTEXT;
    request.Pointer.Pml4PhysicalAddress = root;
    request.Pointer.PhysicalAddressBits = 48;
    request.Pointer.MemoryType = VMX_EPT_MEMORY_TYPE_WB;
    return request;
}

static void ExpectFailure(
    VMX_EPT_CAPABILITIES caps,
    VMX_EPT_INVALIDATION_REQUEST request,
    VMX_EPT_STATUS expected)
{
    VMX_EPT_INVALIDATION_DESCRIPTOR descriptor = {
        UINT64_C(0xA5A5A5A5A5A5A5A5), UINT64_C(0x5A5A5A5A5A5A5A5A)
    };
    TEST_ASSERT_INT_EQ(VmxEptBuildInvalidationDescriptor(
        &caps, &request, &descriptor), expected, "failure status");
    TEST_ASSERT_UINT_EQ(descriptor.EptPointer,
        UINT64_C(0xA5A5A5A5A5A5A5A5), "descriptor preserved");
    TEST_ASSERT_UINT_EQ(descriptor.Reserved,
        UINT64_C(0x5A5A5A5A5A5A5A5A), "reserved preserved");
}

static int test_single_context(void)
{
    VMX_EPT_CAPABILITIES caps = Caps(RAW_REQUIRED | RAW_OPTIONAL);
    VMX_EPT_INVALIDATION_REQUEST request = Single(UINT64_C(0x1000));
    VMX_EPT_INVALIDATION_DESCRIPTOR descriptor;
    TEST_ASSERT_INT_EQ(VmxEptBuildInvalidationDescriptor(
        &caps, &request, &descriptor), VMX_EPT_STATUS_OK,
        "single descriptor");
    TEST_ASSERT_UINT_EQ(descriptor.EptPointer, UINT64_C(0x101E),
        "single EPTP");
    TEST_ASSERT_UINT_EQ(descriptor.Reserved, 0, "single reserved");
    TEST_ASSERT_INT_EQ(VmxEptValidateInvalidationDescriptor(
        &caps, &request, &descriptor), VMX_EPT_STATUS_OK,
        "single exact validation");

    request.Pointer.EnableAccessedDirty = 1;
    TEST_ASSERT_INT_EQ(VmxEptBuildInvalidationDescriptor(
        &caps, &request, &descriptor), VMX_EPT_STATUS_OK,
        "single A/D descriptor");
    TEST_ASSERT_UINT_EQ(descriptor.EptPointer, UINT64_C(0x105E),
        "single A/D EPTP");
    descriptor.EptPointer ^= UINT64_C(1);
    TEST_ASSERT_INT_EQ(VmxEptValidateInvalidationDescriptor(
        &caps, &request, &descriptor), VMX_EPT_STATUS_INVALID,
        "mutated EPTP rejected");
    descriptor.EptPointer ^= UINT64_C(1);
    descriptor.Reserved = 1;
    TEST_ASSERT_INT_EQ(VmxEptValidateInvalidationDescriptor(
        &caps, &request, &descriptor), VMX_EPT_STATUS_INVALID,
        "descriptor reserved rejected");
    return 0;
}

static int test_all_contexts(void)
{
    VMX_EPT_CAPABILITIES caps = Caps(RAW_REQUIRED);
    VMX_EPT_INVALIDATION_REQUEST request = { 0 };
    VMX_EPT_INVALIDATION_DESCRIPTOR descriptor;
    request.Type = VMX_EPT_INVALIDATION_ALL_CONTEXTS;
    TEST_ASSERT_INT_EQ(VmxEptBuildInvalidationDescriptor(
        &caps, &request, &descriptor), VMX_EPT_STATUS_OK,
        "all-context descriptor");
    TEST_ASSERT_UINT_EQ(descriptor.EptPointer, 0, "all-context EPTP zero");
    TEST_ASSERT_UINT_EQ(descriptor.Reserved, 0, "all-context reserved zero");
    TEST_ASSERT_INT_EQ(VmxEptValidateInvalidationDescriptor(
        &caps, &request, &descriptor), VMX_EPT_STATUS_OK,
        "all-context exact validation");
    descriptor.EptPointer = UINT64_C(0x101E);
    TEST_ASSERT_INT_EQ(VmxEptValidateInvalidationDescriptor(
        &caps, &request, &descriptor), VMX_EPT_STATUS_INVALID,
        "all-context nonzero descriptor rejected");

    request.Pointer.Pml4PhysicalAddress = UINT64_C(0x1000);
    ExpectFailure(caps, request, VMX_EPT_STATUS_INVALID);
    memset(&request.Pointer, 0, sizeof(request.Pointer));
    request.Pointer.PhysicalAddressBits = 48;
    ExpectFailure(caps, request, VMX_EPT_STATUS_INVALID);
    memset(&request.Pointer, 0, sizeof(request.Pointer));
    request.Pointer.MemoryType = VMX_EPT_MEMORY_TYPE_WB;
    ExpectFailure(caps, request, VMX_EPT_STATUS_INVALID);
    memset(&request.Pointer, 0, sizeof(request.Pointer));
    request.Pointer.EnableAccessedDirty = 1;
    ExpectFailure(caps, request, VMX_EPT_STATUS_INVALID);
    memset(&request.Pointer, 0, sizeof(request.Pointer));
    request.Pointer.Reserved = 1;
    ExpectFailure(caps, request, VMX_EPT_STATUS_INVALID);
    return 0;
}

static int test_request_rejections(void)
{
    VMX_EPT_CAPABILITIES caps = Caps(RAW_REQUIRED);
    VMX_EPT_INVALIDATION_REQUEST request = Single(UINT64_C(0x1000));
    VMX_EPT_INVALIDATION_DESCRIPTOR descriptor = {
        UINT64_C(0xA5A5A5A5A5A5A5A5), UINT64_C(0x5A5A5A5A5A5A5A5A)
    };
    TEST_ASSERT_INT_EQ(VmxEptBuildInvalidationDescriptor(
        NULL, &request, &descriptor), VMX_EPT_STATUS_INVALID, "null caps");
    TEST_ASSERT_INT_EQ(VmxEptBuildInvalidationDescriptor(
        &caps, NULL, &descriptor), VMX_EPT_STATUS_INVALID, "null request");
    TEST_ASSERT_INT_EQ(VmxEptBuildInvalidationDescriptor(
        &caps, &request, NULL), VMX_EPT_STATUS_INVALID, "null output");
    TEST_ASSERT_INT_EQ(VmxEptValidateInvalidationDescriptor(
        &caps, &request, NULL), VMX_EPT_STATUS_INVALID, "null validator input");
    TEST_ASSERT_UINT_EQ(descriptor.EptPointer,
        UINT64_C(0xA5A5A5A5A5A5A5A5), "null preserves output");

    request.Type = 0;
    ExpectFailure(caps, request, VMX_EPT_STATUS_INVALID);
    request.Type = 3;
    ExpectFailure(caps, request, VMX_EPT_STATUS_INVALID);
    request = Single(UINT64_C(0x1000));
    request.Reserved32 = 1;
    ExpectFailure(caps, request, VMX_EPT_STATUS_INVALID);
    request.Reserved32 = 0;
    request.Reserved = 1;
    ExpectFailure(caps, request, VMX_EPT_STATUS_INVALID);
    request = Single(UINT64_C(0x1001));
    ExpectFailure(caps, request, VMX_EPT_STATUS_INVALID);
    request = Single(UINT64_C(0x1000));
    request.Pointer.MemoryType = VMX_EPT_MEMORY_TYPE_WC;
    ExpectFailure(caps, request, VMX_EPT_STATUS_INVALID);
    request = Single(UINT64_C(0x1000));
    request.Pointer.EnableAccessedDirty = 1;
    ExpectFailure(caps, request, VMX_EPT_STATUS_UNSUPPORTED);

    caps = Caps(RAW_REQUIRED & ~(UINT64_C(1) << 25));
    request = Single(UINT64_C(0x1000));
    ExpectFailure(caps, request, VMX_EPT_STATUS_UNSUPPORTED);
    caps = Caps(RAW_REQUIRED & ~(UINT64_C(1) << 26));
    memset(&request, 0, sizeof(request));
    request.Type = VMX_EPT_INVALIDATION_ALL_CONTEXTS;
    ExpectFailure(caps, request, VMX_EPT_STATUS_UNSUPPORTED);
    caps = Caps(RAW_REQUIRED);
    caps.Flags ^= VMX_EPT_CAP_INVEPT_SINGLE_CONTEXT;
    request = Single(UINT64_C(0x1000));
    ExpectFailure(caps, request, VMX_EPT_STATUS_INVALID);
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_single_context);
    TEST_RUN(test_all_contexts);
    TEST_RUN(test_request_rejections);
TEST_MAIN_END()
