#include "test_common.h"
#include "vmx_vpid_executor.h"

#include <stddef.h>

#define RAW_REQUIRED ((UINT64_C(1) << 32) | (UINT64_C(1) << 41) | \
    (UINT64_C(1) << 42))

typedef struct FAKE_PLATFORM {
    uint32_t Irql;
    int32_t RootOwned;
    uint32_t Result;
    uint32_t Calls;
} FAKE_PLATFORM;

static VMX_VPID_EXECUTION_PLATFORM* g_mutable_platform;
static uint32_t g_poison_calls;

static int32_t PoisonIsRoot(void* Context)
{
    (void)Context;
    ++g_poison_calls;
    return 0;
}

static uint32_t PoisonExecute(void* Context, uint32_t Type,
    const VMX_VPID_INVALIDATION_DESCRIPTOR* Descriptor)
{
    (void)Context;
    (void)Type;
    (void)Descriptor;
    ++g_poison_calls;
    return VMX_VPID_INSTRUCTION_NOT_ATTEMPTED;
}

static uint32_t GetIrql(void* Context)
{
    if (g_mutable_platform != NULL) {
        g_mutable_platform->IsVmxRootOwned = PoisonIsRoot;
        g_mutable_platform->Execute = PoisonExecute;
    }
    return ((FAKE_PLATFORM*)Context)->Irql;
}

static int32_t IsRoot(void* Context)
{
    return ((FAKE_PLATFORM*)Context)->RootOwned;
}

static uint32_t Execute(void* Context, uint32_t Type,
    const VMX_VPID_INVALIDATION_DESCRIPTOR* Descriptor)
{
    FAKE_PLATFORM* Fake = (FAKE_PLATFORM*)Context;
    (void)Type; (void)Descriptor;
    Fake->Calls += 1;
    return Fake->Result;
}

static VMX_VPID_CAPABILITIES Caps(void)
{
    VMX_VPID_CAPABILITIES Caps = { 0 };
    TEST_ASSERT_INT_EQ(VmxVpidDecodeCapabilities(RAW_REQUIRED, &Caps),
        VMX_VPID_CAPABILITY_OK, "decode");
    return Caps;
}

static int test_exact_descriptors(void)
{
    VMX_VPID_CAPABILITIES Capabilities = Caps();
    VMX_VPID_INVALIDATION_REQUEST Request = { 0 };
    VMX_VPID_INVALIDATION_DESCRIPTOR Descriptor = { 0 };
    Request.Type = VMX_VPID_INVALIDATION_SINGLE_CONTEXT;
    Request.Vpid = 7;
    TEST_ASSERT_INT_EQ(VmxVpidBuildInvalidationDescriptor(
        &Capabilities, &Request, &Descriptor), VMX_VPID_INVALIDATION_OK,
        "single-context descriptor");
    TEST_ASSERT_UINT_EQ(Descriptor.VpidAndReserved, 7, "descriptor VPID");
    TEST_ASSERT_UINT_EQ(Descriptor.LinearAddress, 0, "single linear zero");
    TEST_ASSERT_INT_EQ(VmxVpidValidateInvalidationDescriptor(
        &Capabilities, &Request, &Descriptor), VMX_VPID_INVALIDATION_OK,
        "exact validation");
    Request.Type = VMX_VPID_INVALIDATION_ALL_CONTEXTS;
    Request.Vpid = 0;
    TEST_ASSERT_INT_EQ(VmxVpidBuildInvalidationDescriptor(
        &Capabilities, &Request, &Descriptor), VMX_VPID_INVALIDATION_OK,
        "all-context descriptor");
    TEST_ASSERT_UINT_EQ(Descriptor.VpidAndReserved, 0, "all descriptor zero");
    return 0;
}

static int test_invalid_requests_preserve_output(void)
{
    VMX_VPID_CAPABILITIES Capabilities = Caps();
    VMX_VPID_INVALIDATION_REQUEST Request = { 0 };
    VMX_VPID_INVALIDATION_DESCRIPTOR Descriptor = {
        UINT64_C(0xa5a5), UINT64_C(0x5a5a) };
    Request.Type = VMX_VPID_INVALIDATION_SINGLE_CONTEXT;
    TEST_ASSERT_INT_EQ(VmxVpidBuildInvalidationDescriptor(
        &Capabilities, &Request, &Descriptor), VMX_VPID_INVALIDATION_INVALID,
        "zero VPID rejected");
    TEST_ASSERT_UINT_EQ(Descriptor.VpidAndReserved, UINT64_C(0xa5a5),
        "failure preserves descriptor");
    Request.Vpid = 1; Request.LinearAddress = UINT64_C(0x1000);
    TEST_ASSERT_INT_EQ(VmxVpidBuildInvalidationDescriptor(
        &Capabilities, &Request, &Descriptor), VMX_VPID_INVALIDATION_INVALID,
        "linear address rejected for approved type");
    Request.Type = 3; Request.LinearAddress = 0;
    TEST_ASSERT_INT_EQ(VmxVpidBuildInvalidationDescriptor(
        &Capabilities, &Request, &Descriptor), VMX_VPID_INVALIDATION_INVALID,
        "unsupported request type rejected");
    return 0;
}

static int test_checked_execution(void)
{
    VMX_VPID_CAPABILITIES Capabilities = Caps();
    VMX_VPID_INVALIDATION_REQUEST Request = {
        VMX_VPID_INVALIDATION_SINGLE_CONTEXT, 9, 0, 0 };
    VMX_VPID_INVALIDATION_DESCRIPTOR Descriptor = { 0 };
    VMX_VPID_EXECUTION_REPORT Report = { 0 };
    FAKE_PLATFORM Fake = { 2, 1, VMX_VPID_INSTRUCTION_SUCCESS, 0 };
    VMX_VPID_EXECUTION_PLATFORM Platform = {
        &Fake, GetIrql, IsRoot, Execute, 0 };
    TEST_ASSERT_INT_EQ(VmxVpidBuildInvalidationDescriptor(
        &Capabilities, &Request, &Descriptor), VMX_VPID_INVALIDATION_OK,
        "build execution descriptor");
    TEST_ASSERT_INT_EQ(VmxVpidExecuteInvalidation(&Platform, &Capabilities,
        &Request, &Descriptor, &Report), VMX_VPID_EXECUTION_OK,
        "exact instruction success");
    TEST_ASSERT_UINT_EQ(Fake.Calls, 1, "executes exactly once");
    TEST_ASSERT_UINT_EQ(Report.Attempted, 1, "attempt recorded");
    TEST_ASSERT_UINT_EQ(Report.Vpid, 9, "report VPID");
    Fake.RootOwned = 0; Fake.Calls = 0; memset(&Report, 0x5a, sizeof(Report));
    TEST_ASSERT_INT_EQ(VmxVpidExecuteInvalidation(&Platform, &Capabilities,
        &Request, &Descriptor, &Report), VMX_VPID_EXECUTION_NOT_VMX_ROOT,
        "root proof required");
    TEST_ASSERT_UINT_EQ(Fake.Calls, 0, "root failure never executes");
    Fake.RootOwned = 1; Fake.Irql = 3;
    TEST_ASSERT_INT_EQ(VmxVpidExecuteInvalidation(&Platform, &Capabilities,
        &Request, &Descriptor, &Report), VMX_VPID_EXECUTION_WRONG_IRQL,
        "IRQL bounded");
    TEST_ASSERT_UINT_EQ(Fake.Calls, 0, "IRQL failure never executes");
    Fake.Irql = 2; Fake.Result = VMX_VPID_INSTRUCTION_FAIL_VALID;
    TEST_ASSERT_INT_EQ(VmxVpidExecuteInvalidation(&Platform, &Capabilities,
        &Request, &Descriptor, &Report),
        VMX_VPID_EXECUTION_INSTRUCTION_FAILED, "fail-valid mapped exactly");
    Fake.Result = VMX_VPID_INSTRUCTION_NOT_ATTEMPTED;
    TEST_ASSERT_INT_EQ(VmxVpidExecuteInvalidation(&Platform, &Capabilities,
        &Request, &Descriptor, &Report), VMX_VPID_EXECUTION_PLATFORM_INVALID,
        "not-attempted cannot manufacture success");
    return 0;
}

static int test_execution_input_spans(void)
{
    union EXECUTION_ALIAS_STORAGE {
        VMX_VPID_EXECUTION_PLATFORM Platform;
        VMX_VPID_CAPABILITIES Capabilities;
        VMX_VPID_INVALIDATION_REQUEST Request;
        VMX_VPID_INVALIDATION_DESCRIPTOR Descriptor;
        VMX_VPID_EXECUTION_REPORT Report;
        unsigned char Bytes[sizeof(VMX_VPID_EXECUTION_PLATFORM) +
            sizeof(VMX_VPID_EXECUTION_REPORT) + 16];
    } Storage, Before;
    VMX_VPID_CAPABILITIES Capabilities = Caps();
    VMX_VPID_INVALIDATION_REQUEST Request = {
        VMX_VPID_INVALIDATION_SINGLE_CONTEXT, 9, 0, 0 };
    VMX_VPID_INVALIDATION_DESCRIPTOR Descriptor = { 9, 0 };
    VMX_VPID_EXECUTION_REPORT Report;
    FAKE_PLATFORM Fake = { 2, 1, VMX_VPID_INSTRUCTION_SUCCESS, 0 };
    VMX_VPID_EXECUTION_PLATFORM Platform = {
        &Fake, GetIrql, IsRoot, Execute, 0 };

    (void)memset(&Storage, 0xA5, sizeof(Storage));
    Before = Storage;
    TEST_ASSERT_INT_EQ(VmxVpidExecuteInvalidation(
        (const VMX_VPID_EXECUTION_PLATFORM*)Storage.Bytes,
        &Capabilities, &Request, &Descriptor,
        (VMX_VPID_EXECUTION_REPORT*)Storage.Bytes),
        VMX_VPID_EXECUTION_INVALID, "exact platform/report overlap rejected");
    TEST_ASSERT_TRUE(memcmp(&Storage, &Before, sizeof(Storage)) == 0,
        "exact platform/report overlap preserves");

    (void)memset(&Storage, 0x5A, sizeof(Storage));
    Before = Storage;
    TEST_ASSERT_INT_EQ(VmxVpidExecuteInvalidation(
        (const VMX_VPID_EXECUTION_PLATFORM*)Storage.Bytes,
        &Capabilities, &Request, &Descriptor,
        (VMX_VPID_EXECUTION_REPORT*)(Storage.Bytes + 8)),
        VMX_VPID_EXECUTION_INVALID, "partial platform/report overlap rejected");
    TEST_ASSERT_TRUE(memcmp(&Storage, &Before, sizeof(Storage)) == 0,
        "partial platform/report overlap preserves");

    (void)memset(&Storage, 0xC3, sizeof(Storage));
    (void)memset(&Report, 0xC3, sizeof(Report));
    TEST_ASSERT_INT_EQ(VmxVpidExecuteInvalidation(&Platform,
        (const VMX_VPID_CAPABILITIES*)Storage.Bytes,
        (const VMX_VPID_INVALIDATION_REQUEST*)Storage.Bytes,
        &Descriptor, &Report), VMX_VPID_EXECUTION_INVALID,
        "capabilities/request overlap rejected");
    TEST_ASSERT_UINT_EQ(Report.Type, UINT32_C(0xC3C3C3C3),
        "input overlap preserves report");

    (void)memset(&Report, 0x3C, sizeof(Report));
    TEST_ASSERT_INT_EQ(VmxVpidExecuteInvalidation(&Platform,
        (const VMX_VPID_CAPABILITIES*)
            (UINTPTR_MAX - sizeof(VMX_VPID_CAPABILITIES) + 1u),
        &Request, &Descriptor, &Report), VMX_VPID_EXECUTION_INVALID,
        "wrapping capabilities rejected");
    TEST_ASSERT_UINT_EQ(Report.Type, UINT32_C(0x3C3C3C3C),
        "wrapping capabilities preserve report");
    TEST_ASSERT_UINT_EQ(Fake.Calls, 0, "span rejection executes no instruction");
    return 0;
}

static int test_execution_platform_snapshot(void)
{
    VMX_VPID_CAPABILITIES Capabilities = Caps();
    VMX_VPID_INVALIDATION_REQUEST Request = {
        VMX_VPID_INVALIDATION_SINGLE_CONTEXT, 9, 0, 0 };
    VMX_VPID_INVALIDATION_DESCRIPTOR Descriptor = { 9, 0 };
    VMX_VPID_EXECUTION_REPORT Report = { 0 };
    FAKE_PLATFORM Fake = { 2, 1, VMX_VPID_INSTRUCTION_SUCCESS, 0 };
    VMX_VPID_EXECUTION_PLATFORM Platform = {
        &Fake, GetIrql, IsRoot, Execute, 0 };

    g_mutable_platform = &Platform;
    g_poison_calls = 0;
    TEST_ASSERT_INT_EQ(VmxVpidExecuteInvalidation(&Platform, &Capabilities,
        &Request, &Descriptor, &Report), VMX_VPID_EXECUTION_OK,
        "frozen execution platform completes");
    g_mutable_platform = NULL;
    TEST_ASSERT_TRUE(Platform.IsVmxRootOwned == PoisonIsRoot &&
        Platform.Execute == PoisonExecute,
        "caller execution platform was mutated");
    TEST_ASSERT_UINT_EQ(g_poison_calls, 0,
        "execution snapshot blocks poison");
    TEST_ASSERT_UINT_EQ(Fake.Calls, 1,
        "execution snapshot invokes original instruction");
    return 0;
}

_Static_assert(sizeof(VMX_VPID_INVALIDATION_DESCRIPTOR) == 16,
    "INVVPID descriptor ABI");
_Static_assert(offsetof(VMX_VPID_INVALIDATION_DESCRIPTOR, LinearAddress) == 8,
    "INVVPID linear address offset");

TEST_MAIN_BEGIN()
    TEST_RUN(test_exact_descriptors);
    TEST_RUN(test_invalid_requests_preserve_output);
    TEST_RUN(test_checked_execution);
    TEST_RUN(test_execution_input_spans);
    TEST_RUN(test_execution_platform_snapshot);
TEST_MAIN_END()
