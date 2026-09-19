#include "test_common.h"
#include "vmx_ept_root_coordinator.h"

#include <stddef.h>
#include <stdint.h>

#define RAW_REQUIRED ((UINT64_C(1) << 6) | (UINT64_C(1) << 14) | \
    (UINT64_C(1) << 16) | (UINT64_C(1) << 20) | \
    (UINT64_C(1) << 25) | (UINT64_C(1) << 26))
#define CONTEXT_COUNT 3
#define PAGE_COUNT 3

static _Alignas(4096) unsigned char g_tables[PAGE_COUNT][4096];
static VMX_EPT_BOUND_PAGE g_bound[PAGE_COUNT];
static VMX_EPT_PHYSICAL_PAGE g_index[PAGE_COUNT];

static int InitializeDomain(VMX_EPT_DOMAIN* domain)
{
    VMX_EPT_CAPABILITIES caps = { 0 };
    VMX_EPT_IDENTITY_CONFIG identity = { 0 };
    VMX_EPT_IDENTITY_PLAN plan;
    VMX_EPT_POPULATE_CONFIG populate = { 0 };
    VMX_EPT_STORAGE_BINDING binding = { 0 };
    uint32_t i;
    memset(domain, 0, sizeof(*domain));
    memset(g_tables, 0, sizeof(g_tables));
    memset(g_bound, 0, sizeof(g_bound));
    memset(g_index, 0, sizeof(g_index));
    TEST_ASSERT_INT_EQ(VmxEptDecodeCapabilities(RAW_REQUIRED, &caps),
        VMX_EPT_STATUS_OK, "decode capabilities");
    identity.MappedBytes = UINT64_C(0x200000);
    identity.PhysicalAddressBits = 48;
    identity.LeafKind = VMX_EPT_ENTRY_LEAF_2MB;
    TEST_ASSERT_INT_EQ(VmxEptBuildIdentityPlan(&caps, &identity, &plan),
        VMX_EPT_STATUS_OK, "build plan");
    populate.Permissions = VMX_EPT_PERMISSION_MASK;
    populate.MemoryType = VMX_EPT_MEMORY_TYPE_WB;
    for (i = 0; i < PAGE_COUNT; ++i) {
        uint64_t physical = UINT64_C(0x9000) +
            (uint64_t)i * VMX_EPT_TABLE_PAGE_SIZE;
        g_bound[i].PhysicalAddress = physical;
        g_bound[i].PhysicalRank = i;
        g_index[i].PhysicalAddress = physical;
        g_index[i].Ordinal = i;
    }
    binding.VirtualBase = g_tables;
    binding.VirtualBytes = sizeof(g_tables);
    binding.PagesByOrdinal = g_bound;
    binding.PhysicalIndex = g_index;
    binding.PageCount = PAGE_COUNT;
    return VmxEptDomainInitialize(&caps, &identity, &plan, &populate,
        &binding, domain) == VMX_EPT_DOMAIN_OK ? 0 : 1;
}

static void Identities(VMX_PROCESSOR_IDENTITY* identities)
{
    memset(identities, 0, CONTEXT_COUNT * sizeof(*identities));
    identities[0].Group = 0;
    identities[0].Number = 0;
    identities[0].SystemIndex = 0;
    identities[1].Group = 1;
    identities[1].Number = 0;
    identities[1].SystemIndex = 1;
    identities[2].Group = 1;
    identities[2].Number = 1;
    identities[2].SystemIndex = 2;
}

static int InitializeCoordinator(VMX_EPT_DOMAIN* domain,
    VMX_EPT_ROOT_RECORD* records,
    VMX_EPT_ROOT_COORDINATOR* coordinator)
{
    VMX_PROCESSOR_IDENTITY identities[CONTEXT_COUNT];
    if (InitializeDomain(domain) != 0)
        return 1;
    Identities(identities);
    memset(records, 0, CONTEXT_COUNT * sizeof(*records));
    memset(coordinator, 0, sizeof(*coordinator));
    return VmxEptRootCoordinatorInitialize(domain, identities, CONTEXT_COUNT,
        records, coordinator) == VMX_EPT_ROOT_OK ? 0 : 1;
}

static int Enter(VMX_EPT_ROOT_COORDINATOR* coordinator, uint32_t index)
{
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorPrepareEnter(coordinator, index),
        VMX_EPT_ROOT_OK, "prepare enter");
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorCompleteEnter(coordinator, index),
        VMX_EPT_ROOT_OK, "complete enter");
    return 0;
}

static int Release(VMX_EPT_ROOT_COORDINATOR* coordinator, uint32_t index)
{
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorPrepareRelease(coordinator, index),
        VMX_EPT_ROOT_OK, "prepare release");
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorCompleteRelease(coordinator, index),
        VMX_EPT_ROOT_OK, "complete release");
    return 0;
}

static int test_first_enter_last_release(void)
{
    VMX_EPT_DOMAIN domain;
    VMX_EPT_ROOT_RECORD records[CONTEXT_COUNT];
    VMX_EPT_ROOT_COORDINATOR coordinator;
    TEST_ASSERT_INT_EQ(InitializeCoordinator(&domain, records, &coordinator),
        0, "initialize coordinator");
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorValidate(&coordinator),
        VMX_EPT_ROOT_OK, "coordinator authentic");
    TEST_ASSERT_UINT_EQ(coordinator.PendingIndex, VMX_EPT_ROOT_NO_INDEX,
        "no pending transition");
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorPrepareEnter(&coordinator, 0),
        VMX_EPT_ROOT_OK, "prepare first enter");
    TEST_ASSERT_UINT_EQ(domain.Stage, VMX_EPT_DOMAIN_STAGE_READY,
        "prepare does not publish root");
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorCompleteEnter(&coordinator, 0),
        VMX_EPT_ROOT_OK, "complete first enter");
    TEST_ASSERT_UINT_EQ(domain.Stage, VMX_EPT_DOMAIN_STAGE_ROOT_OWNED,
        "first enter publishes domain root");
    TEST_ASSERT_INT_EQ(Enter(&coordinator, 1), 0, "enter second context");
    TEST_ASSERT_UINT_EQ(coordinator.OwnedCount, 2, "two roots owned");
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorPrepareEnter(&coordinator, 1),
        VMX_EPT_ROOT_BUSY, "duplicate enter rejected");
    TEST_ASSERT_INT_EQ(Release(&coordinator, 0), 0, "release nonlast");
    TEST_ASSERT_UINT_EQ(domain.Stage, VMX_EPT_DOMAIN_STAGE_ROOT_OWNED,
        "nonlast release preserves domain root");
    TEST_ASSERT_INT_EQ(Release(&coordinator, 1), 0, "release last");
    TEST_ASSERT_UINT_EQ(domain.Stage, VMX_EPT_DOMAIN_STAGE_READY,
        "last release clears domain root");
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorDestroy(&coordinator),
        VMX_EPT_ROOT_OK, "destroy coordinator");
    TEST_ASSERT_TRUE(memcmp(records,
        (VMX_EPT_ROOT_RECORD[CONTEXT_COUNT]){{0}}, sizeof(records)) == 0,
        "destroy zeros records");
    TEST_ASSERT_INT_EQ(VmxEptDomainDestroy(&domain),
        VMX_EPT_DOMAIN_OK, "domain remains clean");
    return 0;
}

static int test_partial_rollback_and_pending(void)
{
    VMX_EPT_DOMAIN domain;
    VMX_EPT_ROOT_RECORD records[CONTEXT_COUNT];
    VMX_EPT_ROOT_COORDINATOR coordinator;
    TEST_ASSERT_INT_EQ(InitializeCoordinator(&domain, records, &coordinator),
        0, "initialize rollback coordinator");
    TEST_ASSERT_INT_EQ(Enter(&coordinator, 0), 0, "enter zero");
    TEST_ASSERT_INT_EQ(Enter(&coordinator, 1), 0, "enter one");
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorPrepareEnter(&coordinator, 2),
        VMX_EPT_ROOT_OK, "prepare later enter");
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorPrepareRelease(&coordinator, 1),
        VMX_EPT_ROOT_BUSY, "pending blocks another transition");
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorCompleteEnter(&coordinator, 1),
        VMX_EPT_ROOT_INVALID, "wrong completion rejected");
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorCancel(&coordinator, 1),
        VMX_EPT_ROOT_INVALID, "wrong cancel rejected");
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorCancel(&coordinator, 2),
        VMX_EPT_ROOT_OK, "failed native enter cancelled");
    TEST_ASSERT_UINT_EQ(records[2].RootOwned, 0, "cancel preserves record");
    TEST_ASSERT_INT_EQ(Release(&coordinator, 1), 0, "rollback one");
    TEST_ASSERT_INT_EQ(Release(&coordinator, 0), 0, "rollback zero");
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorDestroy(&coordinator),
        VMX_EPT_ROOT_OK, "destroy rollback coordinator");
    TEST_ASSERT_INT_EQ(VmxEptDomainDestroy(&domain),
        VMX_EPT_DOMAIN_OK, "destroy rollback domain");
    return 0;
}

static int test_rejections_and_authentication(void)
{
    VMX_EPT_DOMAIN domain;
    VMX_EPT_ROOT_RECORD records[CONTEXT_COUNT];
    VMX_EPT_ROOT_COORDINATOR coordinator;
    VMX_EPT_ROOT_COORDINATOR copy;
    VMX_PROCESSOR_IDENTITY identities[CONTEXT_COUNT];
    TEST_ASSERT_INT_EQ(InitializeDomain(&domain), 0, "initialize reject domain");
    Identities(identities);
    identities[2] = identities[1];
    identities[2].SystemIndex = 2;
    memset(records, 0, sizeof(records));
    memset(&coordinator, 0, sizeof(coordinator));
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorInitialize(&domain, identities,
        CONTEXT_COUNT, records, &coordinator), VMX_EPT_ROOT_INVALID,
        "duplicate group number rejected");
    TEST_ASSERT_TRUE(memcmp(records,
        (VMX_EPT_ROOT_RECORD[CONTEXT_COUNT]){{0}}, sizeof(records)) == 0,
        "duplicate rejection rolls back records");
    Identities(identities);
    identities[2].SystemIndex = 7;
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorInitialize(&domain, identities,
        CONTEXT_COUNT, records, &coordinator), VMX_EPT_ROOT_INVALID,
        "noncanonical system index rejected");
    Identities(identities);
    records[0].RootOwned = 1;
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorInitialize(&domain, identities,
        CONTEXT_COUNT, records, &coordinator), VMX_EPT_ROOT_INVALID,
        "nonzero records rejected");
    memset(records, 0, sizeof(records));
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorInitialize(&domain, identities,
        CONTEXT_COUNT, records, &coordinator), VMX_EPT_ROOT_OK,
        "valid initialization");
    copy = coordinator;
    copy.OwnedCount = 1;
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorValidate(&copy),
        VMX_EPT_ROOT_INVALID, "owned count authenticated");
    records[2].Identity.Group = records[1].Identity.Group;
    records[2].Identity.Number = records[1].Identity.Number;
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorValidate(&coordinator),
        VMX_EPT_ROOT_INVALID, "record identity authenticated");
    records[2].Identity.Group = identities[2].Group;
    records[2].Identity.Number = identities[2].Number;
    TEST_ASSERT_INT_EQ(Enter(&coordinator, 0), 0, "enter for busy destroy");
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorDestroy(&coordinator),
        VMX_EPT_ROOT_BUSY, "owned root blocks destroy");
    TEST_ASSERT_INT_EQ(Release(&coordinator, 0), 0, "release for destroy");
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorDestroy(&coordinator),
        VMX_EPT_ROOT_OK, "destroy authenticated coordinator");
    TEST_ASSERT_INT_EQ(VmxEptDomainDestroy(&domain),
        VMX_EPT_DOMAIN_OK, "destroy reject domain");
    return 0;
}

static int test_initialize_span_admission(void)
{
    typedef union OVERLAPPING_ARRAYS {
        VMX_PROCESSOR_IDENTITY Identities[CONTEXT_COUNT];
        VMX_EPT_ROOT_RECORD Records[CONTEXT_COUNT];
    } OVERLAPPING_ARRAYS;
    VMX_EPT_DOMAIN domain;
    VMX_EPT_ROOT_RECORD records[CONTEXT_COUNT];
    VMX_EPT_ROOT_COORDINATOR coordinator;
    VMX_PROCESSOR_IDENTITY identities[CONTEXT_COUNT];
    OVERLAPPING_ARRAYS overlap;
    OVERLAPPING_ARRAYS before;

    TEST_ASSERT_INT_EQ(InitializeDomain(&domain), 0,
        "initialize span-admission domain");
    Identities(identities);
    memset(records, 0, sizeof(records));
    memset(&coordinator, 0, sizeof(coordinator));
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorInitialize(&domain,
        (const VMX_PROCESSOR_IDENTITY*)(uintptr_t)(
            UINTPTR_MAX - sizeof(identities) + 2u),
        CONTEXT_COUNT, records, &coordinator), VMX_EPT_ROOT_INVALID,
        "wrapping identity array rejected before dereference");
    TEST_ASSERT_TRUE(memcmp(records,
        (VMX_EPT_ROOT_RECORD[CONTEXT_COUNT]){{0}}, sizeof(records)) == 0,
        "wrapping identity array preserves records");
    TEST_ASSERT_UINT_EQ(coordinator.Magic, 0u,
        "wrapping identity array preserves coordinator");

    memset(&overlap, 0, sizeof(overlap));
    Identities(overlap.Identities);
    before = overlap;
    memset(&coordinator, 0, sizeof(coordinator));
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorInitialize(&domain,
        overlap.Identities, CONTEXT_COUNT, overlap.Records, &coordinator),
        VMX_EPT_ROOT_INVALID, "overlapping identity and record arrays rejected");
    TEST_ASSERT_TRUE(memcmp(&overlap, &before, sizeof(overlap)) == 0,
        "overlapping arrays preserve caller storage");
    TEST_ASSERT_UINT_EQ(coordinator.Magic, 0u,
        "overlapping arrays preserve coordinator");

    memset(records, 0, sizeof(records));
    memset(&coordinator, 0, sizeof(coordinator));
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorInitialize(
        (VMX_EPT_DOMAIN*)(uintptr_t)(
            UINTPTR_MAX - sizeof(VMX_EPT_DOMAIN) + 2u),
        identities, CONTEXT_COUNT, records, &coordinator),
        VMX_EPT_ROOT_INVALID, "wrapping domain rejected before dereference");
    TEST_ASSERT_TRUE(memcmp(records,
        (VMX_EPT_ROOT_RECORD[CONTEXT_COUNT]){{0}}, sizeof(records)) == 0,
        "wrapping domain preserves records");
    TEST_ASSERT_UINT_EQ(coordinator.Magic, 0u,
        "wrapping domain preserves coordinator");

    TEST_ASSERT_INT_EQ(VmxEptDomainDestroy(&domain), VMX_EPT_DOMAIN_OK,
        "destroy span-admission domain");
    return 0;
}

int main(void)
{
    int result = 0;
    result |= test_first_enter_last_release();
    result |= test_partial_rollback_and_pending();
    result |= test_rejections_and_authentication();
    result |= test_initialize_span_admission();
    if (result == 0)
        printf("All EPT root-coordinator tests passed.\n");
    return result;
}
