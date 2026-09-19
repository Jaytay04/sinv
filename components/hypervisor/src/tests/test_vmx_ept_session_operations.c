#include "test_common.h"
#include "vmx_ept_session_operations.h"

#define RAW_REQUIRED ((UINT64_C(1) << 6) | (UINT64_C(1) << 14) | \
    (UINT64_C(1) << 16) | (UINT64_C(1) << 20) | \
    (UINT64_C(1) << 25) | (UINT64_C(1) << 26))
#define PAGE_COUNT 3u
#define CONTEXT_COUNT 2u

static _Alignas(16) unsigned char g_graph[4096];
static _Alignas(4096) unsigned char g_tables[PAGE_COUNT][4096];
static VMX_EPT_BOUND_PAGE g_bound[PAGE_COUNT];
static VMX_EPT_PHYSICAL_PAGE g_physical[PAGE_COUNT];

typedef struct FAKE {
    uint32_t PoolCalls;
    uint32_t FreeCalls;
} FAKE;

static void* Pool(void* context, size_t size)
{
    FAKE* fake = (FAKE*)context;
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
    ((FAKE*)context)->FreeCalls++;
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
    ((FAKE*)context)->FreeCalls++;
}

static uint64_t Physical(void* context, const void* memory)
{
    (void)context;
    return UINT64_C(0x9000) +
        (uint64_t)((uintptr_t)memory - (uintptr_t)g_tables);
}

static VMX_PERSISTENT_ALLOCATOR Allocator(FAKE* fake)
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

static void Initialize(FAKE* fake, VMX_EPT_SESSION_OWNER* owner,
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

static VMX_EPT_EXECUTION_REPORT Report(const VMX_EPT_SESSION_OWNER* owner)
{
    VMX_EPT_EXECUTION_REPORT report = { 0 };
    report.Type = VMX_EPT_INVALIDATION_SINGLE_CONTEXT;
    report.CurrentIrql = VMX_EPT_EXECUTION_MAX_IRQL;
    report.Attempted = 1;
    report.InstructionResult = VMX_EPT_INSTRUCTION_SUCCESS;
    report.EptPointer = owner->Storage->Domain.EptPointer;
    return report;
}

static void CleanUse(VMX_EPT_SESSION_OWNER* owner, uint32_t index)
{
    VMX_EPT_INVALIDATION_REQUEST request;
    VMX_EPT_INVALIDATION_DESCRIPTOR descriptor;
    VMX_EPT_EXECUTION_REPORT report = Report(owner);
    TEST_ASSERT_INT_EQ(VmxEptCoverageBuildInvalidation(owner->Coverage,
        &request, &descriptor), VMX_EPT_COVERAGE_OK, "build invalidation");
    TEST_ASSERT_INT_EQ(VmxEptCoverageAcceptReport(owner->Coverage, index,
        &report), VMX_EPT_COVERAGE_OK, "accept invalidation");
}

static void EnterAndUse(VMX_EPT_SESSION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity)
{
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareEnter(owner, identity),
        VMX_EPT_SESSION_OPERATION_OK, "prepare mutation root");
    TEST_ASSERT_INT_EQ(VmxEptSessionCompleteEnter(owner, identity),
        VMX_EPT_SESSION_OPERATION_OK, "complete mutation root");
    TEST_ASSERT_INT_EQ(VmxEptSessionMarkUse(owner, identity),
        VMX_EPT_SESSION_OPERATION_OK, "mark mutation use");
}

static int test_identity_authentication(void)
{
    FAKE fake = { 0 };
    VMX_EPT_SESSION_OWNER owner;
    VMX_PROCESSOR_IDENTITY ids[CONTEXT_COUNT];
    VMX_PROCESSOR_IDENTITY changed;
    uint32_t magic;
    Identities(ids);
    Initialize(&fake, &owner, ids);
    TEST_ASSERT_INT_EQ(VmxEptSessionMarkUse(&owner, &ids[0]),
        VMX_EPT_SESSION_OPERATION_NOT_READY, "use requires root");
    changed = ids[0];
    changed.Group++;
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareEnter(&owner, &changed),
        VMX_EPT_SESSION_OPERATION_IDENTITY_MISMATCH, "group mismatch");
    changed = ids[0];
    changed.Reserved = 1;
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareEnter(&owner, &changed),
        VMX_EPT_SESSION_OPERATION_INVALID, "reserved rejected");
    changed = ids[0];
    changed.Number = VMX_PROCESSOR_GROUP_MAX_NUMBER;
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareEnter(&owner, &changed),
        VMX_EPT_SESSION_OPERATION_INVALID, "processor number rejected");
    changed = ids[0];
    changed.SystemIndex = CONTEXT_COUNT;
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareEnter(&owner, &changed),
        VMX_EPT_SESSION_OPERATION_INVALID, "index rejected");
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareEnter(&owner,
        &owner.RootRecords[0].Identity), VMX_EPT_SESSION_OPERATION_INVALID,
        "graph alias rejected");
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareEnter(&owner,
        (const VMX_PROCESSOR_IDENTITY*)owner.Storage->Tables),
        VMX_EPT_SESSION_OPERATION_INVALID, "table alias rejected");
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareEnter(&owner,
        (const VMX_PROCESSOR_IDENTITY*)&owner),
        VMX_EPT_SESSION_OPERATION_INVALID, "owner alias rejected");
    magic = owner.Magic;
    owner.Magic = 0;
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareEnter(&owner, &ids[0]),
        VMX_EPT_SESSION_OPERATION_INVALID, "corrupt owner rejected");
    owner.Magic = magic;
    TEST_ASSERT_INT_EQ(VmxEptSessionOwnerDestroy(&owner),
        VMX_EPT_SESSION_OK, "destroy identity owner");
    return 0;
}

static int test_enter_use_release(void)
{
    FAKE fake = { 0 };
    VMX_EPT_SESSION_OWNER owner;
    VMX_PROCESSOR_IDENTITY ids[CONTEXT_COUNT];
    Identities(ids);
    Initialize(&fake, &owner, ids);
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareEnter(&owner, &ids[0]),
        VMX_EPT_SESSION_OPERATION_OK, "prepare enter");
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareEnter(&owner, &ids[1]),
        VMX_EPT_SESSION_OPERATION_BUSY, "one pending transition");
    TEST_ASSERT_INT_EQ(VmxEptSessionCompleteEnter(&owner, &ids[1]),
        VMX_EPT_SESSION_OPERATION_INVALID, "wrong completion identity");
    TEST_ASSERT_INT_EQ(VmxEptSessionCancelEnter(&owner, &ids[0]),
        VMX_EPT_SESSION_OPERATION_OK, "cancel enter");
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareEnter(&owner, &ids[0]),
        VMX_EPT_SESSION_OPERATION_OK, "retry prepare enter");
    TEST_ASSERT_INT_EQ(VmxEptSessionCompleteEnter(&owner, &ids[0]),
        VMX_EPT_SESSION_OPERATION_OK, "complete enter");
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareEnter(&owner, &ids[0]),
        VMX_EPT_SESSION_OPERATION_BUSY, "duplicate enter");
    TEST_ASSERT_INT_EQ(VmxEptSessionMarkUse(&owner, &ids[0]),
        VMX_EPT_SESSION_OPERATION_OK, "mark use");
    TEST_ASSERT_INT_EQ(VmxEptSessionMarkUse(&owner, &ids[0]),
        VMX_EPT_SESSION_OPERATION_BUSY, "duplicate use");
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareRelease(&owner, &ids[0]),
        VMX_EPT_SESSION_OPERATION_DIRTY, "dirty release blocked");
    CleanUse(&owner, 0);
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareRelease(&owner, &ids[0]),
        VMX_EPT_SESSION_OPERATION_OK, "prepare clean release");
    TEST_ASSERT_INT_EQ(VmxEptSessionCancelEnter(&owner, &ids[0]),
        VMX_EPT_SESSION_OPERATION_INVALID, "enter cancel cannot bypass release");
    TEST_ASSERT_UINT_EQ(owner.Root->PendingTransition,
        VMX_EPT_ROOT_TRANSITION_RELEASE, "release remains pending");
    TEST_ASSERT_INT_EQ(VmxEptSessionCancelRelease(&owner, &ids[0]),
        VMX_EPT_SESSION_OPERATION_OK, "cancel release");
    TEST_ASSERT_TRUE(owner.CoverageRecords[0].RequiredGeneration != 0,
        "cancel preserves coverage");
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareRelease(&owner, &ids[0]),
        VMX_EPT_SESSION_OPERATION_OK, "retry prepare release");
    TEST_ASSERT_INT_EQ(VmxEptSessionCompleteRelease(&owner, &ids[0]),
        VMX_EPT_SESSION_OPERATION_OK, "complete release");
    TEST_ASSERT_UINT_EQ(owner.RootRecords[0].RootOwned, 0u, "root released");
    TEST_ASSERT_UINT_EQ(owner.CoverageRecords[0].RequiredGeneration, 0u,
        "coverage released");
    TEST_ASSERT_INT_EQ(VmxEptSessionOwnerDestroy(&owner),
        VMX_EPT_SESSION_OK, "destroy released owner");
    TEST_ASSERT_UINT_EQ(fake.FreeCalls, 4u, "all allocations freed");
    return 0;
}

static int test_multigroup_identity(void)
{
    FAKE fake = { 0 };
    VMX_EPT_SESSION_OWNER owner;
    VMX_PROCESSOR_IDENTITY ids[CONTEXT_COUNT];
    Identities(ids);
    Initialize(&fake, &owner, ids);
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareEnter(&owner, &ids[1]),
        VMX_EPT_SESSION_OPERATION_OK, "group one prepare");
    TEST_ASSERT_INT_EQ(VmxEptSessionCompleteEnter(&owner, &ids[1]),
        VMX_EPT_SESSION_OPERATION_OK, "group one complete");
    TEST_ASSERT_INT_EQ(VmxEptSessionMarkUse(&owner, &ids[1]),
        VMX_EPT_SESSION_OPERATION_OK, "group one use");
    CleanUse(&owner, 1);
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareRelease(&owner, &ids[1]),
        VMX_EPT_SESSION_OPERATION_OK, "group one release prepare");
    TEST_ASSERT_INT_EQ(VmxEptSessionCompleteRelease(&owner, &ids[1]),
        VMX_EPT_SESSION_OPERATION_OK, "group one release complete");
    TEST_ASSERT_INT_EQ(VmxEptSessionOwnerDestroy(&owner),
        VMX_EPT_SESSION_OK, "destroy group one owner");
    return 0;
}

static int test_mutation_invalidation(void)
{
    FAKE fake = { 0 };
    VMX_EPT_SESSION_OWNER owner;
    VMX_PROCESSOR_IDENTITY ids[CONTEXT_COUNT];
    VMX_PROCESSOR_IDENTITY changed;
    VMX_EPT_INVALIDATION_REQUEST request = { 0 };
    VMX_EPT_INVALIDATION_DESCRIPTOR descriptor = { 0 };
    VMX_EPT_EXECUTION_REPORT report;
    uint64_t generation;
    Identities(ids);
    Initialize(&fake, &owner, ids);
    TEST_ASSERT_INT_EQ(VmxEptSessionBeginMutation(&owner),
        VMX_EPT_SESSION_OPERATION_NOT_READY, "mutation requires use");
    request.Type = UINT32_MAX;
    descriptor.EptPointer = UINT64_MAX;
    TEST_ASSERT_INT_EQ(VmxEptSessionBuildInvalidation(&owner,
        &request, &descriptor), VMX_EPT_SESSION_OPERATION_NOT_READY,
        "not-ready build preserves outputs");
    TEST_ASSERT_UINT_EQ(request.Type, UINT32_MAX, "request preserved");
    TEST_ASSERT_UINT_EQ(descriptor.EptPointer, UINT64_MAX,
        "descriptor preserved");
    memset(&request, 0, sizeof(request));
    memset(&descriptor, 0, sizeof(descriptor));
    EnterAndUse(&owner, &ids[0]);
    EnterAndUse(&owner, &ids[1]);
    TEST_ASSERT_INT_EQ(VmxEptSessionBuildInvalidation(&owner,
        (VMX_EPT_INVALIDATION_REQUEST*)(void*)&owner.RootRecords[0],
        &descriptor), VMX_EPT_SESSION_OPERATION_INVALID,
        "request graph alias rejected");
    TEST_ASSERT_INT_EQ(VmxEptSessionBuildInvalidation(&owner,
        &request, (VMX_EPT_INVALIDATION_DESCRIPTOR*)(void*)owner.Storage->Tables),
        VMX_EPT_SESSION_OPERATION_INVALID, "descriptor table alias rejected");
    TEST_ASSERT_INT_EQ(VmxEptSessionBuildInvalidation(&owner,
        &request, (VMX_EPT_INVALIDATION_DESCRIPTOR*)(void*)&request),
        VMX_EPT_SESSION_OPERATION_INVALID, "output overlap rejected");
    TEST_ASSERT_INT_EQ(VmxEptSessionBuildInvalidation(&owner,
        &request, &descriptor), VMX_EPT_SESSION_OPERATION_OK,
        "build initial invalidation");
    report = Report(&owner);
    changed = ids[0];
    changed.Group++;
    TEST_ASSERT_INT_EQ(VmxEptSessionAcceptInvalidation(&owner,
        &changed, &report), VMX_EPT_SESSION_OPERATION_IDENTITY_MISMATCH,
        "report identity mismatch");
    TEST_ASSERT_INT_EQ(VmxEptSessionAcceptInvalidation(&owner, &ids[0],
        (const VMX_EPT_EXECUTION_REPORT*)(const void*)owner.Storage->Tables),
        VMX_EPT_SESSION_OPERATION_INVALID, "report table alias rejected");
    report.InstructionResult = VMX_EPT_INSTRUCTION_FAIL_VALID;
    TEST_ASSERT_INT_EQ(VmxEptSessionAcceptInvalidation(&owner, &ids[0],
        &report), VMX_EPT_SESSION_OPERATION_INSTRUCTION_FAILED,
        "failed report remains retryable");
    TEST_ASSERT_UINT_EQ(owner.CoverageRecords[0].AcknowledgedGeneration, 1u,
        "failed report preserves acknowledgment");
    report.InstructionResult = VMX_EPT_INSTRUCTION_SUCCESS;
    TEST_ASSERT_INT_EQ(VmxEptSessionAcceptInvalidation(&owner, &ids[0],
        &report), VMX_EPT_SESSION_OPERATION_OK, "accept first report");
    generation = owner.Storage->Domain.PendingGeneration;
    TEST_ASSERT_TRUE(generation != 0, "first report cannot commit generation");
    TEST_ASSERT_INT_EQ(VmxEptSessionAcceptInvalidation(&owner, &ids[0],
        &report), VMX_EPT_SESSION_OPERATION_BUSY,
        "duplicate report rejected");
    TEST_ASSERT_INT_EQ(VmxEptSessionAcceptInvalidation(&owner, &ids[1],
        &report), VMX_EPT_SESSION_OPERATION_OK,
        "final report commits generation");
    TEST_ASSERT_UINT_EQ(owner.Storage->Domain.PendingGeneration, 0u,
        "initial pending generation cleared");
    TEST_ASSERT_UINT_EQ(owner.Storage->Domain.InvalidatedGeneration,
        generation, "initial generation committed");
    TEST_ASSERT_INT_EQ(VmxEptSessionBeginMutation(&owner),
        VMX_EPT_SESSION_OPERATION_OK, "begin authenticated mutation");
    generation = owner.Storage->Domain.MutationGeneration;
    TEST_ASSERT_INT_EQ(VmxEptSessionBeginMutation(&owner),
        VMX_EPT_SESSION_OPERATION_DIRTY,
        "dirty generation blocks next mutation");
    TEST_ASSERT_INT_EQ(VmxEptSessionBuildInvalidation(&owner,
        &request, &descriptor), VMX_EPT_SESSION_OPERATION_OK,
        "build mutation invalidation");
    report.EptPointer++;
    TEST_ASSERT_INT_EQ(VmxEptSessionAcceptInvalidation(&owner, &ids[1],
        &report), VMX_EPT_SESSION_OPERATION_INVALID,
        "mismatched report rejected");
    TEST_ASSERT_UINT_EQ(owner.Storage->Domain.PendingGeneration, generation,
        "mismatch preserves pending generation");
    report.EptPointer--;
    TEST_ASSERT_INT_EQ(VmxEptSessionAcceptInvalidation(&owner, &ids[1],
        &report), VMX_EPT_SESSION_OPERATION_OK, "accept group one report");
    TEST_ASSERT_UINT_EQ(owner.Storage->Domain.PendingGeneration, generation,
        "N minus one cannot commit generation");
    TEST_ASSERT_INT_EQ(VmxEptSessionAcceptInvalidation(&owner, &ids[0],
        &report), VMX_EPT_SESSION_OPERATION_OK, "accept final mutation report");
    TEST_ASSERT_UINT_EQ(owner.Storage->Domain.InvalidatedGeneration,
        generation, "exact N of N commits generation");
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareRelease(&owner, &ids[0]),
        VMX_EPT_SESSION_OPERATION_OK, "prepare first mutation release");
    TEST_ASSERT_INT_EQ(VmxEptSessionCompleteRelease(&owner, &ids[0]),
        VMX_EPT_SESSION_OPERATION_OK, "complete first mutation release");
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareRelease(&owner, &ids[1]),
        VMX_EPT_SESSION_OPERATION_OK, "prepare second mutation release");
    TEST_ASSERT_INT_EQ(VmxEptSessionCompleteRelease(&owner, &ids[1]),
        VMX_EPT_SESSION_OPERATION_OK, "complete second mutation release");
    TEST_ASSERT_INT_EQ(VmxEptSessionOwnerDestroy(&owner),
        VMX_EPT_SESSION_OK, "destroy mutation owner");
    return 0;
}
int main(void)
{
    int result = 0;
    result |= test_identity_authentication();
    result |= test_enter_use_release();
    result |= test_multigroup_identity();
    result |= test_mutation_invalidation();
    return result;
}