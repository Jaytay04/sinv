#include "test_common.h"
#include "vmx_ept_session_owner.h"
#include <stddef.h>
#define RAW_REQUIRED ((UINT64_C(1) << 6) | (UINT64_C(1) << 14) | \
    (UINT64_C(1) << 16) | (UINT64_C(1) << 20) | \
    (UINT64_C(1) << 25) | (UINT64_C(1) << 26))
#define PAGE_COUNT 3u
#define CONTEXT_COUNT 3u
static _Alignas(16) unsigned char g_graph[4096];
static _Alignas(4096) unsigned char g_tables[PAGE_COUNT][4096];
static VMX_EPT_BOUND_PAGE g_bound[PAGE_COUNT];
static VMX_EPT_PHYSICAL_PAGE g_index[PAGE_COUNT];
typedef struct FAKE {
    uint32_t PoolCall, ContiguousCall, FailPool, FailContiguous, AliasPool;
    uint32_t UnalignedGraph;
    void* AliasPointer;
    uint32_t FreeCount, FreeKind[8];
    void* FreePointer[8];
} FAKE;
static void RecordFree(FAKE* f, uint32_t kind, void* p)
{
    TEST_ASSERT_TRUE(f->FreeCount < 8u, "free capacity");
    f->FreeKind[f->FreeCount] = kind;
    f->FreePointer[f->FreeCount++] = p;
}
static void* Pool(void* context, size_t size)
{
    FAKE* f = (FAKE*)context;
    f->PoolCall++;
    if (f->PoolCall == f->FailPool) return 0;
    if (f->PoolCall == 1u) {
        TEST_ASSERT_TRUE(size <= sizeof(g_graph), "graph size");
        return f->UnalignedGraph ? (void*)(g_graph + 1) : (void*)g_graph;
    }
    if (f->PoolCall == f->AliasPool)
        return f->AliasPointer != 0 ? f->AliasPointer : (void*)g_graph;
    if (f->PoolCall == 2u && size == sizeof(g_bound)) return g_bound;
    if (f->PoolCall == 3u && size == sizeof(g_index)) return g_index;
    return 0;
}
static void FreePool(void* context, void* memory)
{
    RecordFree((FAKE*)context, 1u, memory);
}
static void* Contiguous(void* context, size_t size, uint64_t highest)
{
    FAKE* f = (FAKE*)context;
    (void)highest;
    f->ContiguousCall++;
    if (f->FailContiguous || size != sizeof(g_tables)) return 0;
    return g_tables;
}
static void FreeContiguous(void* context, void* memory)
{
    RecordFree((FAKE*)context, 2u, memory);
}
static uint64_t Physical(void* context, const void* memory)
{
    uintptr_t offset = (uintptr_t)memory - (uintptr_t)g_tables;
    (void)context;
    return UINT64_C(0x9000) + offset;
}
static VMX_PERSISTENT_ALLOCATOR Allocator(FAKE* f)
{
    VMX_PERSISTENT_ALLOCATOR a = { 0 };
    a.Context = f; a.AllocatePool = Pool; a.FreePool = FreePool;
    a.AllocateContiguous = Contiguous;
    a.FreeContiguous = FreeContiguous;
    a.GetPhysicalAddress = Physical;
    return a;
}
static VMX_EPT_CAPABILITIES Caps(void)
{
    VMX_EPT_CAPABILITIES c = { 0 };
    TEST_ASSERT_INT_EQ(VmxEptDecodeCapabilities(RAW_REQUIRED, &c),
        VMX_EPT_STATUS_OK, "decode");
    return c;
}
static VMX_EPT_IDENTITY_CONFIG Identity(void)
{
    VMX_EPT_IDENTITY_CONFIG i = { 0 };
    i.MappedBytes = UINT64_C(0x200000);
    i.PhysicalAddressBits = 48;
    i.LeafKind = VMX_EPT_ENTRY_LEAF_2MB;
    return i;
}
static VMX_EPT_POPULATE_CONFIG Populate(void)
{
    VMX_EPT_POPULATE_CONFIG p = { 0 };
    p.Permissions = VMX_EPT_PERMISSION_MASK;
    p.MemoryType = VMX_EPT_MEMORY_TYPE_WB;
    return p;
}
static void Identities(VMX_PROCESSOR_IDENTITY ids[CONTEXT_COUNT])
{
    memset(ids, 0, CONTEXT_COUNT * sizeof(*ids));
    ids[0].Group = 0; ids[0].Number = 0; ids[0].SystemIndex = 0;
    ids[1].Group = 1; ids[1].Number = 0; ids[1].SystemIndex = 1;
    ids[2].Group = 1; ids[2].Number = 1; ids[2].SystemIndex = 2;
}
static VMX_EPT_SESSION_STATUS Initialize(FAKE* f,
    VMX_EPT_SESSION_OWNER* owner, VMX_PROCESSOR_IDENTITY* ids)
{
    VMX_EPT_CAPABILITIES c = Caps();
    VMX_EPT_IDENTITY_CONFIG i = Identity();
    VMX_EPT_POPULATE_CONFIG p = Populate();
    VMX_PERSISTENT_ALLOCATOR a = Allocator(f);
    memset(owner, 0, sizeof(*owner));
    memset(g_graph, 0xA5, sizeof(g_graph));
    memset(g_tables, 0xA5, sizeof(g_tables));
    memset(g_bound, 0xA5, sizeof(g_bound));
    memset(g_index, 0xA5, sizeof(g_index));
    return VmxEptSessionOwnerInitialize(
        &c, &i, &p, ids, CONTEXT_COUNT, &a, owner);
}
static VMX_EPT_EXECUTION_REPORT Report(const VMX_EPT_SESSION_OWNER* owner)
{
    VMX_EPT_EXECUTION_REPORT r = { 0 };
    r.Type = VMX_EPT_INVALIDATION_SINGLE_CONTEXT;
    r.CurrentIrql = VMX_EPT_EXECUTION_MAX_IRQL;
    r.Attempted = 1;
    r.InstructionResult = VMX_EPT_INSTRUCTION_SUCCESS;
    r.EptPointer = owner->Storage->Domain.EptPointer;
    return r;
}
static int test_publish_and_destroy(void)
{
    FAKE f = { 0 };
    VMX_PROCESSOR_IDENTITY ids[CONTEXT_COUNT];
    VMX_EPT_SESSION_OWNER owner;
    Identities(ids);
    TEST_ASSERT_INT_EQ(Initialize(&f, &owner, ids), VMX_EPT_SESSION_OK,
        "initialize");
    TEST_ASSERT_INT_EQ(VmxEptSessionOwnerValidate(&owner),
        VMX_EPT_SESSION_OK, "validate");
    TEST_ASSERT_UINT_EQ(f.PoolCall, 3u, "graph plus metadata pools");
    TEST_ASSERT_UINT_EQ(f.ContiguousCall, 1u, "one table allocation");
    TEST_ASSERT_TRUE(owner.Graph == g_graph, "graph owned");
    TEST_ASSERT_TRUE(owner.Root->Domain == &owner.Storage->Domain,
        "root binds storage domain");
    TEST_ASSERT_TRUE(owner.Coverage->Root == owner.Root,
        "coverage binds root");
    TEST_ASSERT_UINT_EQ(owner.RootRecords[1].Identity.Group, 1u,
        "group copied");
    ids[1].Group = 9;
    TEST_ASSERT_INT_EQ(VmxEptSessionOwnerValidate(&owner),
        VMX_EPT_SESSION_OK, "input identities not retained");
    TEST_ASSERT_INT_EQ(VmxEptSessionOwnerDestroy(&owner),
        VMX_EPT_SESSION_OK, "destroy");
    TEST_ASSERT_UINT_EQ(f.FreeCount, 4u, "all owned allocations freed");
    TEST_ASSERT_TRUE(f.FreePointer[0] == g_index, "index first");
    TEST_ASSERT_TRUE(f.FreePointer[1] == g_bound, "bound second");
    TEST_ASSERT_TRUE(f.FreePointer[2] == g_tables, "tables third");
    TEST_ASSERT_TRUE(f.FreePointer[3] == g_graph, "graph last");
    TEST_ASSERT_UINT_EQ(owner.Magic, 0u, "owner zero");
    TEST_ASSERT_INT_EQ(VmxEptSessionOwnerDestroy(&owner),
        VMX_EPT_SESSION_OK, "zero idempotent");
    return 0;
}
static int test_coverage_blocks_and_retries(void)
{
    FAKE f = { 0 };
    VMX_PROCESSOR_IDENTITY ids[CONTEXT_COUNT];
    VMX_EPT_SESSION_OWNER owner;
    VMX_EPT_INVALIDATION_REQUEST request;
    VMX_EPT_INVALIDATION_DESCRIPTOR descriptor;
    VMX_EPT_EXECUTION_REPORT report;
    uint32_t i;
    Identities(ids);
    TEST_ASSERT_INT_EQ(Initialize(&f, &owner, ids), VMX_EPT_SESSION_OK,
        "initialize active");
    for (i = 0; i < CONTEXT_COUNT; ++i) {
        TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorPrepareEnter(owner.Root, i),
            VMX_EPT_ROOT_OK, "prepare enter");
        TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorCompleteEnter(owner.Root, i),
            VMX_EPT_ROOT_OK, "complete enter");
        TEST_ASSERT_INT_EQ(VmxEptCoverageMarkUse(owner.Coverage, i),
            VMX_EPT_COVERAGE_OK, "mark use");
    }
    TEST_ASSERT_INT_EQ(VmxEptSessionOwnerDestroy(&owner),
        VMX_EPT_SESSION_BUSY, "owned graph preserved");
    TEST_ASSERT_UINT_EQ(f.FreeCount, 0u, "busy frees nothing");
    TEST_ASSERT_INT_EQ(VmxEptCoverageBuildInvalidation(owner.Coverage,
        &request, &descriptor), VMX_EPT_COVERAGE_OK, "build invalidation");
    report = Report(&owner);
    for (i = 0; i < CONTEXT_COUNT; ++i)
        TEST_ASSERT_INT_EQ(VmxEptCoverageAcceptReport(owner.Coverage, i,
            &report), VMX_EPT_COVERAGE_OK, "accept report");
    TEST_ASSERT_INT_EQ(VmxEptCoveragePrepareRelease(owner.Coverage, 0),
        VMX_EPT_COVERAGE_OK, "prepare release");
    TEST_ASSERT_INT_EQ(VmxEptSessionOwnerDestroy(&owner),
        VMX_EPT_SESSION_BUSY, "pending release preserves graph");
    TEST_ASSERT_INT_EQ(VmxEptCoverageCancelRelease(owner.Coverage, 0),
        VMX_EPT_COVERAGE_OK, "cancel release");
    TEST_ASSERT_TRUE(owner.CoverageRecords[0].RequiredGeneration != 0,
        "cancel preserves coverage");
    for (i = 0; i < CONTEXT_COUNT; ++i) {
        TEST_ASSERT_INT_EQ(VmxEptCoveragePrepareRelease(owner.Coverage, i),
            VMX_EPT_COVERAGE_OK, "prepare clean release");
        TEST_ASSERT_INT_EQ(VmxEptCoverageCompleteRelease(owner.Coverage, i),
            VMX_EPT_COVERAGE_OK, "complete clean release");
    }
    TEST_ASSERT_INT_EQ(VmxEptSessionOwnerDestroy(&owner),
        VMX_EPT_SESSION_OK, "clean retry destroys");
    return 0;
}
static int test_failures_unwind_without_publication(void)
{
    VMX_PROCESSOR_IDENTITY ids[CONTEXT_COUNT];
    VMX_EPT_SESSION_OWNER owner;
    FAKE f = { 0 };
    Identities(ids);
    f.FailPool = 1;
    TEST_ASSERT_INT_EQ(Initialize(&f, &owner, ids),
        VMX_EPT_SESSION_ALLOCATION_FAILED, "graph allocation failure");
    TEST_ASSERT_UINT_EQ(owner.Magic, 0u, "graph failure unpublished");
    TEST_ASSERT_UINT_EQ(f.FreeCount, 0u, "nothing to free");

    memset(&f, 0, sizeof(f)); f.UnalignedGraph = 1;
    TEST_ASSERT_INT_EQ(Initialize(&f, &owner, ids),
        VMX_EPT_SESSION_REGION_INVALID, "unaligned graph rejected");
    TEST_ASSERT_UINT_EQ(f.FreeCount, 1u, "unaligned graph released");

    memset(&f, 0, sizeof(f)); f.FailContiguous = 1;
    TEST_ASSERT_INT_EQ(Initialize(&f, &owner, ids),
        VMX_EPT_SESSION_ALLOCATION_FAILED, "table allocation failure");
    TEST_ASSERT_UINT_EQ(owner.Magic, 0u, "table failure unpublished");
    TEST_ASSERT_UINT_EQ(f.FreeCount, 1u, "graph released");

    memset(&f, 0, sizeof(f)); f.FailPool = 2;
    TEST_ASSERT_INT_EQ(Initialize(&f, &owner, ids),
        VMX_EPT_SESSION_ALLOCATION_FAILED, "metadata allocation failure");
    TEST_ASSERT_UINT_EQ(f.FreeCount, 2u, "table then graph released");

    memset(&f, 0, sizeof(f)); f.AliasPool = 2;
    TEST_ASSERT_INT_EQ(Initialize(&f, &owner, ids),
        VMX_EPT_SESSION_ALLOCATION_FAILED, "graph alias rejected");
    TEST_ASSERT_UINT_EQ(f.FreeCount, 2u, "alias not double freed");
    TEST_ASSERT_TRUE(f.FreePointer[1] == g_graph, "graph freed once");

    memset(&f, 0, sizeof(f)); f.AliasPool = 2; f.AliasPointer = &owner;
    TEST_ASSERT_INT_EQ(Initialize(&f, &owner, ids),
        VMX_EPT_SESSION_ALLOCATION_FAILED, "output owner alias rejected");
    TEST_ASSERT_UINT_EQ(f.FreeCount, 2u, "output owner remains live");
    TEST_ASSERT_UINT_EQ(owner.Magic, 0u, "output owner alias unpublished");

    memset(&f, 0, sizeof(f)); Identities(ids);
    ids[2] = ids[1]; ids[2].SystemIndex = 2;
    TEST_ASSERT_INT_EQ(Initialize(&f, &owner, ids),
        VMX_EPT_SESSION_REGION_INVALID, "duplicate identity");
    TEST_ASSERT_UINT_EQ(owner.Magic, 0u, "identity failure unpublished");
    TEST_ASSERT_UINT_EQ(f.FreeCount, 4u, "reverse full unwind");
    return 0;
}
static int test_authentication_and_inputs(void)
{
    FAKE f = { 0 };
    VMX_PROCESSOR_IDENTITY ids[CONTEXT_COUNT];
    VMX_EPT_SESSION_OWNER owner, corrupt;
    Identities(ids);
    TEST_ASSERT_INT_EQ(Initialize(&f, &owner, ids), VMX_EPT_SESSION_OK,
        "initialize auth");
    corrupt = owner; corrupt.Count++;
    TEST_ASSERT_INT_EQ(VmxEptSessionOwnerValidate(&corrupt),
        VMX_EPT_SESSION_INVALID, "count authentic");
    corrupt = owner; corrupt.RootRecords++;
    TEST_ASSERT_INT_EQ(VmxEptSessionOwnerValidate(&corrupt),
        VMX_EPT_SESSION_INVALID, "record pointer authentic");
    corrupt = owner; corrupt.AllocatorContract.FreePool = 0;
    TEST_ASSERT_INT_EQ(VmxEptSessionOwnerValidate(&corrupt),
        VMX_EPT_SESSION_INVALID, "allocator contract authentic");
    TEST_ASSERT_INT_EQ(VmxEptSessionOwnerDestroy(&owner),
        VMX_EPT_SESSION_OK, "destroy auth");
    memset(&owner, 0xA5, sizeof(owner));
    TEST_ASSERT_INT_EQ(Initialize(&f, &owner, ids),
        VMX_EPT_SESSION_INVALID, "nonzero output rejected");
    return 0;
}
int main(void)
{
    int result = 0;
    result |= test_publish_and_destroy();
    result |= test_coverage_blocks_and_retries();
    result |= test_failures_unwind_without_publication();
    result |= test_authentication_and_inputs();
    if (result == 0)
        printf("All EPT session-owner tests passed.\n");
    return result;
}