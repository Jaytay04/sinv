#include "test_common.h"
#include "vmx_ept_invalidation_coordinator.h"

#define RAW_REQUIRED ((UINT64_C(1) << 6) | (UINT64_C(1) << 14) | \
    (UINT64_C(1) << 16) | (UINT64_C(1) << 20) | \
    (UINT64_C(1) << 25) | (UINT64_C(1) << 26))
#define PAGE_COUNT 3u
#define CONTEXT_COUNT 2u

static _Alignas(16) unsigned char g_graph[4096];
static _Alignas(4096) unsigned char g_tables[PAGE_COUNT][4096];
static VMX_EPT_BOUND_PAGE g_bound[PAGE_COUNT];
static VMX_EPT_PHYSICAL_PAGE g_physical[PAGE_COUNT];

typedef struct FAKE_ALLOCATOR {
    uint32_t PoolCalls;
    uint32_t FreeCalls;
} FAKE_ALLOCATOR;

typedef struct FAKE_EXECUTOR {
    uint32_t Irql;
    int32_t RootOwned;
    uint32_t InstructionResult;
    uint32_t IrqlCalls;
    uint32_t RootCalls;
    uint32_t ExecuteCalls;
    VMX_PROCESSOR_IDENTITY* IdentityToMutate;
} FAKE_EXECUTOR;

static void* Pool(void* context, size_t size)
{
    FAKE_ALLOCATOR* fake = (FAKE_ALLOCATOR*)context;
    fake->PoolCalls++;
    if (fake->PoolCalls == 1u && size <= sizeof(g_graph))
        return g_graph;
    if (fake->PoolCalls == 2u && size == sizeof(g_bound))
        return g_bound;
    if (fake->PoolCalls == 3u && size == sizeof(g_physical))
        return g_physical;
    return NULL;
}

static void FreePool(void* context, void* memory)
{
    (void)memory;
    ((FAKE_ALLOCATOR*)context)->FreeCalls++;
}

static void* Contiguous(void* context, size_t size, uint64_t highest)
{
    (void)context;
    (void)highest;
    return size == sizeof(g_tables) ? g_tables : NULL;
}

static void FreeContiguous(void* context, void* memory)
{
    (void)memory;
    ((FAKE_ALLOCATOR*)context)->FreeCalls++;
}

static uint64_t Physical(void* context, const void* memory)
{
    (void)context;
    return UINT64_C(0x9000) +
        (uint64_t)((uintptr_t)memory - (uintptr_t)g_tables);
}

static VMX_PERSISTENT_ALLOCATOR Allocator(FAKE_ALLOCATOR* fake)
{
    VMX_PERSISTENT_ALLOCATOR allocator = { 0 };
    allocator.Context = fake;
    allocator.AllocatePool = Pool;
    allocator.FreePool = FreePool;
    allocator.AllocateContiguous = Contiguous;
    allocator.FreeContiguous = FreeContiguous;
    allocator.GetPhysicalAddress = Physical;
    return allocator;
}

static uint32_t CurrentIrql(void* context)
{
    FAKE_EXECUTOR* fake = (FAKE_EXECUTOR*)context;
    fake->IrqlCalls++;
    if (fake->IdentityToMutate != NULL)
        fake->IdentityToMutate->Group++;
    return fake->Irql;
}

static int32_t RootOwned(void* context)
{
    FAKE_EXECUTOR* fake = (FAKE_EXECUTOR*)context;
    fake->RootCalls++;
    return fake->RootOwned;
}

static uint32_t Execute(void* context, uint32_t type,
    const VMX_EPT_INVALIDATION_DESCRIPTOR* descriptor)
{
    FAKE_EXECUTOR* fake = (FAKE_EXECUTOR*)context;
    fake->ExecuteCalls++;
    TEST_ASSERT_UINT_EQ(type, VMX_EPT_INVALIDATION_SINGLE_CONTEXT,
        "single-context execution");
    TEST_ASSERT_TRUE(descriptor->EptPointer != 0, "descriptor EPTP");
    return fake->InstructionResult;
}

static VMX_EPT_EXECUTION_PLATFORM Platform(FAKE_EXECUTOR* fake)
{
    VMX_EPT_EXECUTION_PLATFORM platform = { 0 };
    platform.Context = fake;
    platform.GetCurrentIrql = CurrentIrql;
    platform.IsVmxRootOwned = RootOwned;
    platform.Execute = Execute;
    return platform;
}

static void Identities(VMX_PROCESSOR_IDENTITY ids[CONTEXT_COUNT])
{
    memset(ids, 0, CONTEXT_COUNT * sizeof(*ids));
    ids[0].Group = 0;
    ids[0].Number = 1;
    ids[0].SystemIndex = 0;
    ids[1].Group = 1;
    ids[1].Number = 2;
    ids[1].SystemIndex = 1;
}

static void Initialize(FAKE_ALLOCATOR* fake, VMX_EPT_SESSION_OWNER* owner,
    VMX_PROCESSOR_IDENTITY ids[CONTEXT_COUNT])
{
    VMX_EPT_CAPABILITIES capabilities = { 0 };
    VMX_EPT_IDENTITY_CONFIG identity = { 0 };
    VMX_EPT_POPULATE_CONFIG populate = { 0 };
    VMX_PERSISTENT_ALLOCATOR allocator = Allocator(fake);
    memset(owner, 0, sizeof(*owner));
    memset(g_graph, 0xA5, sizeof(g_graph));
    memset(g_tables, 0xA5, sizeof(g_tables));
    memset(g_bound, 0xA5, sizeof(g_bound));
    memset(g_physical, 0xA5, sizeof(g_physical));
    TEST_ASSERT_INT_EQ(VmxEptDecodeCapabilities(RAW_REQUIRED, &capabilities),
        VMX_EPT_STATUS_OK, "decode capabilities");
    identity.MappedBytes = UINT64_C(0x200000);
    identity.PhysicalAddressBits = 48;
    identity.LeafKind = VMX_EPT_ENTRY_LEAF_2MB;
    populate.Permissions = VMX_EPT_PERMISSION_MASK;
    populate.MemoryType = VMX_EPT_MEMORY_TYPE_WB;
    TEST_ASSERT_INT_EQ(VmxEptSessionOwnerInitialize(&capabilities, &identity,
        &populate, ids, CONTEXT_COUNT, &allocator, owner),
        VMX_EPT_SESSION_OK, "initialize owner");
}

static void EnterUse(VMX_EPT_SESSION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity)
{
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareEnter(owner, identity),
        VMX_EPT_SESSION_OPERATION_OK, "prepare enter");
    TEST_ASSERT_INT_EQ(VmxEptSessionCompleteEnter(owner, identity),
        VMX_EPT_SESSION_OPERATION_OK, "complete enter");
    TEST_ASSERT_INT_EQ(VmxEptSessionMarkUse(owner, identity),
        VMX_EPT_SESSION_OPERATION_OK, "mark use");
}

static VMX_EPT_EXECUTION_REPORT Sentinel(void)
{
    VMX_EPT_EXECUTION_REPORT report;
    memset(&report, 0xA5, sizeof(report));
    return report;
}

static void Preserved(const VMX_EPT_EXECUTION_REPORT* report,
    const char* label)
{
    const unsigned char* bytes = (const unsigned char*)report;
    size_t i;
    for (i = 0; i < sizeof(*report); ++i)
        TEST_ASSERT_UINT_EQ(bytes[i], 0xA5, label);
}

static void Release(VMX_EPT_SESSION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity)
{
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareRelease(owner, identity),
        VMX_EPT_SESSION_OPERATION_OK, "prepare release");
    TEST_ASSERT_INT_EQ(VmxEptSessionCompleteRelease(owner, identity),
        VMX_EPT_SESSION_OPERATION_OK, "complete release");
}

static int test_preflight_and_platform_retry(void)
{
    FAKE_ALLOCATOR allocator = { 0 };
    FAKE_EXECUTOR executor = { 0 };
    VMX_EPT_SESSION_OWNER owner;
    VMX_PROCESSOR_IDENTITY ids[CONTEXT_COUNT];
    VMX_PROCESSOR_IDENTITY changed;
    VMX_EPT_EXECUTION_PLATFORM platform;
    VMX_EPT_EXECUTION_REPORT report;
    Identities(ids);
    Initialize(&allocator, &owner, ids);
    EnterUse(&owner, &ids[0]);
    platform = Platform(&executor);
    executor.RootOwned = 1;
    executor.InstructionResult = VMX_EPT_INSTRUCTION_SUCCESS;

    changed = ids[0];
    changed.Group++;
    report = Sentinel();
    TEST_ASSERT_INT_EQ(VmxEptCoordinateInvalidation(
        &owner, &changed, &platform, &report),
        VMX_EPT_INVALIDATION_COORDINATOR_IDENTITY_MISMATCH,
        "identity rejected before build");
    TEST_ASSERT_UINT_EQ(owner.Storage->Domain.PendingGeneration, 0u,
        "identity rejection preserves pending");
    Preserved(&report, "identity rejection preserves report");

    report = Sentinel();
    TEST_ASSERT_INT_EQ(VmxEptCoordinateInvalidation(
        &owner, &ids[0], &platform,
        (VMX_EPT_EXECUTION_REPORT*)(void*)owner.Storage->Tables),
        VMX_EPT_INVALIDATION_COORDINATOR_INVALID,
        "owned report span rejected");
    TEST_ASSERT_INT_EQ(VmxEptCoordinateInvalidation(
        &owner, &ids[0],
        (const VMX_EPT_EXECUTION_PLATFORM*)(const void*)owner.Graph, &report),
        VMX_EPT_INVALIDATION_COORDINATOR_INVALID,
        "owned platform span rejected");
    TEST_ASSERT_UINT_EQ(executor.IrqlCalls, 0u, "preflight no callbacks");

    platform.Reserved = 1;
    TEST_ASSERT_INT_EQ(VmxEptCoordinateInvalidation(
        &owner, &ids[0], &platform, &report),
        VMX_EPT_INVALIDATION_COORDINATOR_PLATFORM_INVALID,
        "invalid platform preserves retry");
    TEST_ASSERT_TRUE(owner.Storage->Domain.PendingGeneration != 0,
        "platform failure retains pending generation");
    Preserved(&report, "invalid platform preserves report");

    platform.Reserved = 0;
    executor.InstructionResult = VMX_EPT_INSTRUCTION_NOT_ATTEMPTED;
    TEST_ASSERT_INT_EQ(VmxEptCoordinateInvalidation(
        &owner, &ids[0], &platform, &report),
        VMX_EPT_INVALIDATION_COORDINATOR_PLATFORM_INVALID,
        "invalid instruction result");
    TEST_ASSERT_UINT_EQ(report.Attempted, 1u, "attempted report published");
    TEST_ASSERT_UINT_EQ(report.InstructionResult,
        VMX_EPT_INSTRUCTION_NOT_ATTEMPTED, "invalid result published");

    executor.InstructionResult = VMX_EPT_INSTRUCTION_SUCCESS;
    TEST_ASSERT_INT_EQ(VmxEptCoordinateInvalidation(
        &owner, &ids[0], &platform, &report),
        VMX_EPT_INVALIDATION_COORDINATOR_OK, "retry succeeds");
    TEST_ASSERT_UINT_EQ(owner.Storage->Domain.PendingGeneration, 0u,
        "single used context commits");
    Release(&owner, &ids[0]);
    TEST_ASSERT_INT_EQ(VmxEptSessionOwnerDestroy(&owner),
        VMX_EPT_SESSION_OK, "destroy retry owner");
    return 0;
}

static int test_failure_and_n_of_n(void)
{
    FAKE_ALLOCATOR allocator = { 0 };
    FAKE_EXECUTOR executor = { 0 };
    VMX_EPT_SESSION_OWNER owner;
    VMX_PROCESSOR_IDENTITY ids[CONTEXT_COUNT];
    VMX_EPT_EXECUTION_PLATFORM platform;
    VMX_EPT_EXECUTION_REPORT report;
    uint64_t generation;
    uint32_t execute_calls;
    Identities(ids);
    Initialize(&allocator, &owner, ids);
    EnterUse(&owner, &ids[0]);
    EnterUse(&owner, &ids[1]);
    platform = Platform(&executor);
    executor.RootOwned = 1;
    executor.InstructionResult = VMX_EPT_INSTRUCTION_FAIL_VALID;

    report = Sentinel();
    TEST_ASSERT_INT_EQ(VmxEptCoordinateInvalidation(
        &owner, &ids[0], &platform, &report),
        VMX_EPT_INVALIDATION_COORDINATOR_INSTRUCTION_FAILED,
        "instruction failure retryable");
    generation = owner.Storage->Domain.PendingGeneration;
    TEST_ASSERT_TRUE(generation != 0, "failure preserves pending generation");
    TEST_ASSERT_UINT_EQ(owner.CoverageRecords[0].AcknowledgedGeneration, 1u,
        "failure preserves prior acknowledgment");
    TEST_ASSERT_UINT_EQ(report.Attempted, 1u, "failed report published");

    executor.InstructionResult = VMX_EPT_INSTRUCTION_SUCCESS;
    TEST_ASSERT_INT_EQ(VmxEptCoordinateInvalidation(
        &owner, &ids[0], &platform, &report),
        VMX_EPT_INVALIDATION_COORDINATOR_OK, "first context retry succeeds");
    TEST_ASSERT_UINT_EQ(owner.Storage->Domain.PendingGeneration, generation,
        "first context cannot commit global generation");
    execute_calls = executor.ExecuteCalls;
    report = Sentinel();
    TEST_ASSERT_INT_EQ(VmxEptCoordinateInvalidation(
        &owner, &ids[0], &platform, &report),
        VMX_EPT_INVALIDATION_COORDINATOR_BUSY,
        "duplicate context blocked before execute");
    TEST_ASSERT_UINT_EQ(executor.ExecuteCalls, execute_calls,
        "duplicate performs no instruction");
    Preserved(&report, "duplicate preserves report");

    TEST_ASSERT_INT_EQ(VmxEptCoordinateInvalidation(
        &owner, &ids[1], &platform, &report),
        VMX_EPT_INVALIDATION_COORDINATOR_OK,
        "group one final context commits");
    TEST_ASSERT_UINT_EQ(owner.Storage->Domain.InvalidatedGeneration, generation,
        "exact N of N committed");
    TEST_ASSERT_UINT_EQ(owner.Storage->Domain.PendingGeneration, 0u,
        "N of N clears pending");

    TEST_ASSERT_INT_EQ(VmxEptSessionBeginMutation(&owner),
        VMX_EPT_SESSION_OPERATION_OK, "begin next mutation");
    report = Sentinel();
    executor.Irql = VMX_EPT_EXECUTION_MAX_IRQL + 1u;
    execute_calls = executor.ExecuteCalls;
    TEST_ASSERT_INT_EQ(VmxEptCoordinateInvalidation(
        &owner, &ids[0], &platform, &report),
        VMX_EPT_INVALIDATION_COORDINATOR_WRONG_IRQL,
        "wrong IRQL preserves report");
    TEST_ASSERT_UINT_EQ(executor.ExecuteCalls, execute_calls,
        "wrong IRQL no instruction");
    Preserved(&report, "wrong IRQL report preserved");
    executor.Irql = VMX_EPT_EXECUTION_MAX_IRQL;
    TEST_ASSERT_INT_EQ(VmxEptCoordinateInvalidation(
        &owner, &ids[1], &platform, &report),
        VMX_EPT_INVALIDATION_COORDINATOR_OK,
        "group one acknowledges next generation");
    TEST_ASSERT_TRUE(owner.Storage->Domain.PendingGeneration != 0,
        "N minus one remains pending");
    TEST_ASSERT_INT_EQ(VmxEptCoordinateInvalidation(
        &owner, &ids[0], &platform, &report),
        VMX_EPT_INVALIDATION_COORDINATOR_OK,
        "final retry commits next generation");
    TEST_ASSERT_UINT_EQ(owner.Storage->Domain.PendingGeneration, 0u,
        "retry generation committed");

    Release(&owner, &ids[0]);
    Release(&owner, &ids[1]);
    TEST_ASSERT_INT_EQ(VmxEptSessionOwnerDestroy(&owner),
        VMX_EPT_SESSION_OK, "destroy N of N owner");
    return 0;
}

static int test_identity_snapshot(void)
{
    FAKE_ALLOCATOR allocator = { 0 };
    FAKE_EXECUTOR executor = { 0 };
    VMX_EPT_SESSION_OWNER owner;
    VMX_PROCESSOR_IDENTITY ids[CONTEXT_COUNT];
    VMX_PROCESSOR_IDENTITY caller_identity;
    VMX_EPT_EXECUTION_PLATFORM platform;
    VMX_EPT_EXECUTION_REPORT report = Sentinel();
    Identities(ids);
    Initialize(&allocator, &owner, ids);
    EnterUse(&owner, &ids[0]);
    caller_identity = ids[0];
    platform = Platform(&executor);
    executor.RootOwned = 1;
    executor.InstructionResult = VMX_EPT_INSTRUCTION_SUCCESS;
    executor.IdentityToMutate = &caller_identity;

    TEST_ASSERT_INT_EQ(VmxEptCoordinateInvalidation(
        &owner, &caller_identity, &platform, &report),
        VMX_EPT_INVALIDATION_COORDINATOR_OK,
        "frozen invalidation identity completes");
    TEST_ASSERT_TRUE(!VmxProcessorIdentityEqual(&caller_identity, &ids[0]),
        "caller identity was mutated by callback");
    TEST_ASSERT_UINT_EQ(executor.ExecuteCalls, 1u,
        "frozen coordinator executes once");
    TEST_ASSERT_UINT_EQ(owner.Storage->Domain.PendingGeneration, 0u,
        "frozen identity accepts completed generation");

    Release(&owner, &ids[0]);
    TEST_ASSERT_INT_EQ(VmxEptSessionOwnerDestroy(&owner),
        VMX_EPT_SESSION_OK, "destroy snapshot owner");
    return 0;
}

int main(void)
{
    int result = 0;
    result |= test_preflight_and_platform_retry();
    result |= test_failure_and_n_of_n();
    result |= test_identity_snapshot();
    return result;
}
