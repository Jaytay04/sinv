#include "test_common.h"
#include "vmx_ept_executor.h"

#include <stddef.h>

#define RAW_REQUIRED ((UINT64_C(1) << 6) | (UINT64_C(1) << 14) | \
    (UINT64_C(1) << 16) | (UINT64_C(1) << 20) | \
    (UINT64_C(1) << 25) | (UINT64_C(1) << 26))

_Static_assert(VMX_EPT_INSTRUCTION_NOT_ATTEMPTED == 0, "not attempted");
_Static_assert(VMX_EPT_INSTRUCTION_SUCCESS == 1, "success");
_Static_assert(VMX_EPT_INSTRUCTION_FAIL_VALID == 2, "fail valid");
_Static_assert(VMX_EPT_INSTRUCTION_FAIL_INVALID == 3, "fail invalid");
_Static_assert(VMX_EPT_EXECUTION_MAX_IRQL == 2, "DISPATCH IRQL boundary");
_Static_assert(sizeof(VMX_EPT_EXECUTION_REPORT) == 32, "report ABI");
_Static_assert(offsetof(VMX_EPT_EXECUTION_REPORT, EptPointer) == 16,
    "report EPTP offset");

typedef struct FAKE_EXECUTOR {
    uint32_t Irql;
    int32_t RootOwned;
    uint32_t InstructionResult;
    uint32_t IrqlCalls;
    uint32_t RootCalls;
    uint32_t ExecuteCalls;
    uint32_t Sequence;
    uint32_t IrqlSequence;
    uint32_t RootSequence;
    uint32_t ExecuteSequence;
    uint32_t ObservedType;
    VMX_EPT_INVALIDATION_DESCRIPTOR ObservedDescriptor;
    VMX_EPT_EXECUTION_PLATFORM* PlatformToMutate;
    VMX_EPT_INVALIDATION_REQUEST* RequestToMutate;
} FAKE_EXECUTOR;

static uint32_t FakeGetCurrentIrql(void* context)
{
    FAKE_EXECUTOR* fake = (FAKE_EXECUTOR*)context;
    fake->IrqlCalls++;
    fake->IrqlSequence = ++fake->Sequence;
    if (fake->PlatformToMutate != 0) {
        fake->PlatformToMutate->IsVmxRootOwned = 0;
        fake->PlatformToMutate->Execute = 0;
    }
    if (fake->RequestToMutate != 0)
        fake->RequestToMutate->Type = VMX_EPT_INVALIDATION_ALL_CONTEXTS;
    return fake->Irql;
}

static int32_t FakeIsVmxRootOwned(void* context)
{
    FAKE_EXECUTOR* fake = (FAKE_EXECUTOR*)context;
    fake->RootCalls++;
    fake->RootSequence = ++fake->Sequence;
    return fake->RootOwned;
}

static uint32_t FakeExecute(
    void* context,
    uint32_t type,
    const VMX_EPT_INVALIDATION_DESCRIPTOR* descriptor)
{
    FAKE_EXECUTOR* fake = (FAKE_EXECUTOR*)context;
    fake->ExecuteCalls++;
    fake->ExecuteSequence = ++fake->Sequence;
    fake->ObservedType = type;
    fake->ObservedDescriptor = *descriptor;
    return fake->InstructionResult;
}

static VMX_EPT_CAPABILITIES Caps(void)
{
    VMX_EPT_CAPABILITIES caps = { 0 };
    TEST_ASSERT_INT_EQ(VmxEptDecodeCapabilities(RAW_REQUIRED, &caps),
        VMX_EPT_STATUS_OK, "decode capabilities");
    return caps;
}

static VMX_EPT_INVALIDATION_REQUEST Single(void)
{
    VMX_EPT_INVALIDATION_REQUEST request = { 0 };
    request.Type = VMX_EPT_INVALIDATION_SINGLE_CONTEXT;
    request.Pointer.Pml4PhysicalAddress = UINT64_C(0x1000);
    request.Pointer.PhysicalAddressBits = 48;
    request.Pointer.MemoryType = VMX_EPT_MEMORY_TYPE_WB;
    return request;
}

static VMX_EPT_EXECUTION_PLATFORM Platform(FAKE_EXECUTOR* fake)
{
    VMX_EPT_EXECUTION_PLATFORM platform = { 0 };
    platform.Context = fake;
    platform.GetCurrentIrql = FakeGetCurrentIrql;
    platform.IsVmxRootOwned = FakeIsVmxRootOwned;
    platform.Execute = FakeExecute;
    return platform;
}

static VMX_EPT_INVALIDATION_DESCRIPTOR Descriptor(
    VMX_EPT_CAPABILITIES* caps,
    VMX_EPT_INVALIDATION_REQUEST* request)
{
    VMX_EPT_INVALIDATION_DESCRIPTOR descriptor;
    TEST_ASSERT_INT_EQ(VmxEptBuildInvalidationDescriptor(
        caps, request, &descriptor), VMX_EPT_STATUS_OK, "build descriptor");
    return descriptor;
}

static VMX_EPT_EXECUTION_REPORT SentinelReport(void)
{
    VMX_EPT_EXECUTION_REPORT report;
    memset(&report, 0xA5, sizeof(report));
    return report;
}

static void ExpectPreserved(
    const VMX_EPT_EXECUTION_REPORT* report,
    const char* label)
{
    const unsigned char* bytes = (const unsigned char*)report;
    size_t i;
    for (i = 0; i < sizeof(*report); ++i)
        TEST_ASSERT_UINT_EQ(bytes[i], 0xA5, label);
}

static int test_execution_platform_snapshot(void)
{
    VMX_EPT_CAPABILITIES caps = Caps();
    VMX_EPT_INVALIDATION_REQUEST request = Single();
    VMX_EPT_INVALIDATION_DESCRIPTOR descriptor = Descriptor(&caps, &request);
    FAKE_EXECUTOR fake = { 0 };
    VMX_EPT_EXECUTION_PLATFORM platform;
    VMX_EPT_EXECUTION_REPORT report = SentinelReport();
    fake.Irql = 2;
    fake.RootOwned = 1;
    fake.InstructionResult = VMX_EPT_INSTRUCTION_SUCCESS;
    platform = Platform(&fake);
    fake.PlatformToMutate = &platform;
    fake.RequestToMutate = &request;

    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(
        &platform, &caps, &request, &descriptor, &report),
        VMX_EPT_EXECUTION_OK, "single success");
    TEST_ASSERT_UINT_EQ(fake.IrqlCalls, 1, "IRQL once");
    TEST_ASSERT_UINT_EQ(fake.RootCalls, 1, "root once");
    TEST_ASSERT_UINT_EQ(fake.ExecuteCalls, 1, "execute once");
    TEST_ASSERT_UINT_EQ(fake.IrqlSequence, 1, "IRQL first");
    TEST_ASSERT_UINT_EQ(fake.RootSequence, 2, "root second");
    TEST_ASSERT_UINT_EQ(fake.ExecuteSequence, 3, "execute third");
    TEST_ASSERT_TRUE(platform.IsVmxRootOwned == NULL &&
        platform.Execute == NULL, "caller execution platform was mutated");
    TEST_ASSERT_UINT_EQ(request.Type, VMX_EPT_INVALIDATION_ALL_CONTEXTS,
        "caller request was mutated");
    TEST_ASSERT_UINT_EQ(fake.ObservedType,
        VMX_EPT_INVALIDATION_SINGLE_CONTEXT, "observed single type");
    TEST_ASSERT_UINT_EQ(fake.ObservedDescriptor.EptPointer,
        descriptor.EptPointer, "observed EPTP");
    TEST_ASSERT_UINT_EQ(report.Type,
        VMX_EPT_INVALIDATION_SINGLE_CONTEXT, "staged report type");
    TEST_ASSERT_UINT_EQ(report.CurrentIrql, 2, "report IRQL");
    TEST_ASSERT_UINT_EQ(report.Attempted, 1, "report attempted");
    TEST_ASSERT_UINT_EQ(report.InstructionResult,
        VMX_EPT_INSTRUCTION_SUCCESS, "report result");
    TEST_ASSERT_UINT_EQ(report.EptPointer, descriptor.EptPointer,
        "report EPTP");
    TEST_ASSERT_UINT_EQ(report.Reserved, 0, "report reserved");
    return 0;
}

static int test_execution_input_spans(void)
{
    union EXECUTION_ALIAS_STORAGE {
        VMX_EPT_EXECUTION_PLATFORM Platform;
        VMX_EPT_CAPABILITIES Capabilities;
        VMX_EPT_INVALIDATION_REQUEST Request;
        VMX_EPT_INVALIDATION_DESCRIPTOR Descriptor;
        VMX_EPT_EXECUTION_REPORT Report;
        unsigned char Bytes[sizeof(VMX_EPT_EXECUTION_PLATFORM) +
            sizeof(VMX_EPT_EXECUTION_REPORT) + 16];
    } storage, before;
    VMX_EPT_CAPABILITIES caps = Caps();
    VMX_EPT_INVALIDATION_REQUEST request = Single();
    VMX_EPT_INVALIDATION_DESCRIPTOR descriptor = Descriptor(&caps, &request);
    FAKE_EXECUTOR fake = { 0 };
    VMX_EPT_EXECUTION_PLATFORM platform = Platform(&fake);
    VMX_EPT_EXECUTION_REPORT report;

    memset(&storage, 0xA5, sizeof(storage));
    before = storage;
    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(
        (const VMX_EPT_EXECUTION_PLATFORM*)storage.Bytes,
        &caps, &request, &descriptor,
        (VMX_EPT_EXECUTION_REPORT*)storage.Bytes),
        VMX_EPT_EXECUTION_INVALID, "exact platform/report overlap rejected");
    TEST_ASSERT_TRUE(memcmp(&storage, &before, sizeof(storage)) == 0,
        "exact platform/report overlap preserves");

    memset(&storage, 0x5A, sizeof(storage));
    before = storage;
    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(
        (const VMX_EPT_EXECUTION_PLATFORM*)storage.Bytes,
        &caps, &request, &descriptor,
        (VMX_EPT_EXECUTION_REPORT*)(storage.Bytes + 8)),
        VMX_EPT_EXECUTION_INVALID, "partial platform/report overlap rejected");
    TEST_ASSERT_TRUE(memcmp(&storage, &before, sizeof(storage)) == 0,
        "partial platform/report overlap preserves");

    memset(&storage, 0xC3, sizeof(storage));
    memset(&report, 0xC3, sizeof(report));
    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(&platform,
        (const VMX_EPT_CAPABILITIES*)storage.Bytes,
        (const VMX_EPT_INVALIDATION_REQUEST*)storage.Bytes,
        &descriptor, &report), VMX_EPT_EXECUTION_INVALID,
        "capabilities/request overlap rejected");
    TEST_ASSERT_UINT_EQ(report.Type, UINT32_C(0xC3C3C3C3),
        "input overlap preserves report");

    memset(&report, 0x3C, sizeof(report));
    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(&platform,
        (const VMX_EPT_CAPABILITIES*)
            (UINTPTR_MAX - sizeof(VMX_EPT_CAPABILITIES) + 1u),
        &request, &descriptor, &report), VMX_EPT_EXECUTION_INVALID,
        "wrapping capabilities rejected");
    TEST_ASSERT_UINT_EQ(report.Type, UINT32_C(0x3C3C3C3C),
        "wrapping capabilities preserve report");
    TEST_ASSERT_UINT_EQ(fake.IrqlCalls, 0, "span rejection no IRQL callback");
    TEST_ASSERT_UINT_EQ(fake.RootCalls, 0, "span rejection no root callback");
    TEST_ASSERT_UINT_EQ(fake.ExecuteCalls, 0, "span rejection no execute");
    return 0;
}

static int test_all_context_success(void)
{
    VMX_EPT_CAPABILITIES caps = Caps();
    VMX_EPT_INVALIDATION_REQUEST request = { 0 };
    VMX_EPT_INVALIDATION_DESCRIPTOR descriptor;
    FAKE_EXECUTOR fake = { 0 };
    VMX_EPT_EXECUTION_PLATFORM platform;
    VMX_EPT_EXECUTION_REPORT report;
    request.Type = VMX_EPT_INVALIDATION_ALL_CONTEXTS;
    descriptor = Descriptor(&caps, &request);
    fake.RootOwned = 1;
    fake.InstructionResult = VMX_EPT_INSTRUCTION_SUCCESS;
    platform = Platform(&fake);
    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(
        &platform, &caps, &request, &descriptor, &report),
        VMX_EPT_EXECUTION_OK, "all-context success");
    TEST_ASSERT_UINT_EQ(fake.ObservedType,
        VMX_EPT_INVALIDATION_ALL_CONTEXTS, "observed all type");
    TEST_ASSERT_UINT_EQ(fake.ObservedDescriptor.EptPointer, 0,
        "all descriptor zero");
    TEST_ASSERT_UINT_EQ(report.EptPointer, 0, "all report EPTP zero");
    return 0;
}

static void ExpectInstructionResult(
    uint32_t instruction_result,
    VMX_EPT_EXECUTION_STATUS expected)
{
    VMX_EPT_CAPABILITIES caps = Caps();
    VMX_EPT_INVALIDATION_REQUEST request = Single();
    VMX_EPT_INVALIDATION_DESCRIPTOR descriptor = Descriptor(&caps, &request);
    FAKE_EXECUTOR fake = { 0 };
    VMX_EPT_EXECUTION_PLATFORM platform;
    VMX_EPT_EXECUTION_REPORT report = SentinelReport();
    fake.RootOwned = 1;
    fake.InstructionResult = instruction_result;
    platform = Platform(&fake);
    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(
        &platform, &caps, &request, &descriptor, &report), expected,
        "instruction status");
    TEST_ASSERT_UINT_EQ(fake.ExecuteCalls, 1, "instruction attempted once");
    TEST_ASSERT_UINT_EQ(report.Attempted, 1, "failure attempted");
    TEST_ASSERT_UINT_EQ(report.InstructionResult, instruction_result,
        "failure result published");
}

static int test_instruction_results(void)
{
    ExpectInstructionResult(VMX_EPT_INSTRUCTION_FAIL_VALID,
        VMX_EPT_EXECUTION_INSTRUCTION_FAILED);
    ExpectInstructionResult(VMX_EPT_INSTRUCTION_FAIL_INVALID,
        VMX_EPT_EXECUTION_INSTRUCTION_FAILED);
    ExpectInstructionResult(VMX_EPT_INSTRUCTION_NOT_ATTEMPTED,
        VMX_EPT_EXECUTION_PLATFORM_INVALID);
    ExpectInstructionResult(4, VMX_EPT_EXECUTION_PLATFORM_INVALID);
    return 0;
}

static int test_preinstruction_failures(void)
{
    VMX_EPT_CAPABILITIES caps = Caps();
    VMX_EPT_INVALIDATION_REQUEST request = Single();
    VMX_EPT_INVALIDATION_DESCRIPTOR descriptor = Descriptor(&caps, &request);
    FAKE_EXECUTOR fake = { 0 };
    VMX_EPT_EXECUTION_PLATFORM platform;
    VMX_EPT_EXECUTION_REPORT report;

    fake.Irql = 3;
    fake.RootOwned = 1;
    fake.InstructionResult = VMX_EPT_INSTRUCTION_SUCCESS;
    platform = Platform(&fake);
    report = SentinelReport();
    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(
        &platform, &caps, &request, &descriptor, &report),
        VMX_EPT_EXECUTION_WRONG_IRQL, "high IRQL");
    TEST_ASSERT_UINT_EQ(fake.IrqlCalls, 1, "high IRQL queried");
    TEST_ASSERT_UINT_EQ(fake.RootCalls, 0, "high IRQL no root query");
    TEST_ASSERT_UINT_EQ(fake.ExecuteCalls, 0, "high IRQL no execute");
    ExpectPreserved(&report, "high IRQL report preserved");

    memset(&fake, 0, sizeof(fake));
    platform = Platform(&fake);
    report = SentinelReport();
    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(
        &platform, &caps, &request, &descriptor, &report),
        VMX_EPT_EXECUTION_NOT_VMX_ROOT, "not VMX root");
    TEST_ASSERT_UINT_EQ(fake.RootCalls, 1, "root queried");
    TEST_ASSERT_UINT_EQ(fake.ExecuteCalls, 0, "not root no execute");
    ExpectPreserved(&report, "not root report preserved");

    memset(&fake, 0, sizeof(fake));
    fake.RootOwned = 2;
    platform = Platform(&fake);
    report = SentinelReport();
    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(
        &platform, &caps, &request, &descriptor, &report),
        VMX_EPT_EXECUTION_PLATFORM_INVALID, "invalid root proof");
    TEST_ASSERT_UINT_EQ(fake.ExecuteCalls, 0, "invalid proof no execute");
    ExpectPreserved(&report, "invalid proof report preserved");
    return 0;
}

static int test_validation_and_platform_rejections(void)
{
    VMX_EPT_CAPABILITIES caps = Caps();
    VMX_EPT_INVALIDATION_REQUEST request = Single();
    VMX_EPT_INVALIDATION_DESCRIPTOR descriptor = Descriptor(&caps, &request);
    FAKE_EXECUTOR fake = { 0 };
    VMX_EPT_EXECUTION_PLATFORM platform = Platform(&fake);
    VMX_EPT_EXECUTION_REPORT report = SentinelReport();
    fake.RootOwned = 1;
    fake.InstructionResult = VMX_EPT_INSTRUCTION_SUCCESS;

    descriptor.Reserved = 1;
    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(
        &platform, &caps, &request, &descriptor, &report),
        VMX_EPT_EXECUTION_INVALID, "invalid descriptor");
    TEST_ASSERT_UINT_EQ(fake.IrqlCalls, 0, "invalid descriptor no callback");
    ExpectPreserved(&report, "invalid descriptor report preserved");
    descriptor.Reserved = 0;

    platform.GetCurrentIrql = NULL;
    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(
        &platform, &caps, &request, &descriptor, &report),
        VMX_EPT_EXECUTION_PLATFORM_INVALID, "missing IRQL callback");
    platform = Platform(&fake);
    platform.IsVmxRootOwned = NULL;
    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(
        &platform, &caps, &request, &descriptor, &report),
        VMX_EPT_EXECUTION_PLATFORM_INVALID, "missing root callback");
    platform = Platform(&fake);
    platform.Execute = NULL;
    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(
        &platform, &caps, &request, &descriptor, &report),
        VMX_EPT_EXECUTION_PLATFORM_INVALID, "missing execute callback");
    platform = Platform(&fake);
    platform.Reserved = 1;
    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(
        &platform, &caps, &request, &descriptor, &report),
        VMX_EPT_EXECUTION_PLATFORM_INVALID, "platform reserved");

    platform = Platform(&fake);
    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(
        &platform, &caps, &request, &descriptor,
        (VMX_EPT_EXECUTION_REPORT*)&request),
        VMX_EPT_EXECUTION_INVALID, "request-output alias");
    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(
        &platform, &caps, &request, &descriptor,
        (VMX_EPT_EXECUTION_REPORT*)&platform),
        VMX_EPT_EXECUTION_INVALID, "platform-output alias");
    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(
        &platform, &caps, &request, &descriptor,
        (VMX_EPT_EXECUTION_REPORT*)&caps),
        VMX_EPT_EXECUTION_INVALID, "capabilities-output alias");
    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(
        &platform, &caps, &request, &descriptor,
        (VMX_EPT_EXECUTION_REPORT*)&descriptor),
        VMX_EPT_EXECUTION_INVALID, "descriptor-output alias");
    TEST_ASSERT_UINT_EQ(fake.IrqlCalls, 0, "aliases no callback");

    caps = Caps();
    caps.Raw &= ~(UINT64_C(1) << 25);
    TEST_ASSERT_INT_EQ(VmxEptDecodeCapabilities(caps.Raw, &caps),
        VMX_EPT_STATUS_OK, "decode unsupported single capability");
    report = SentinelReport();
    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(
        &platform, &caps, &request, &descriptor, &report),
        VMX_EPT_EXECUTION_UNSUPPORTED, "unsupported single context");
    TEST_ASSERT_UINT_EQ(fake.IrqlCalls, 0, "unsupported no callback");
    ExpectPreserved(&report, "unsupported report preserved");
    caps = Caps();

    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(
        NULL, &caps, &request, &descriptor, &report),
        VMX_EPT_EXECUTION_INVALID, "null platform");
    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(
        &platform, NULL, &request, &descriptor, &report),
        VMX_EPT_EXECUTION_INVALID, "null caps");
    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(
        &platform, &caps, NULL, &descriptor, &report),
        VMX_EPT_EXECUTION_INVALID, "null request");
    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(
        &platform, &caps, &request, NULL, &report),
        VMX_EPT_EXECUTION_INVALID, "null descriptor");
    TEST_ASSERT_INT_EQ(VmxEptExecuteInvalidation(
        &platform, &caps, &request, &descriptor, NULL),
        VMX_EPT_EXECUTION_INVALID, "null report");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_execution_platform_snapshot);
    TEST_RUN(test_execution_input_spans);
    TEST_RUN(test_all_context_success);
    TEST_RUN(test_instruction_results);
    TEST_RUN(test_preinstruction_failures);
    TEST_RUN(test_validation_and_platform_rejections);
TEST_MAIN_END()
