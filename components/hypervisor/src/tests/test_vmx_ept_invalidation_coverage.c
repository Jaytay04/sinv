#include "test_common.h"
#include "vmx_ept_invalidation_coverage.h"

#include <stddef.h>

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
        VMX_EPT_STATUS_OK, "build identity plan");
    populate.Permissions = VMX_EPT_PERMISSION_MASK;
    populate.MemoryType = VMX_EPT_MEMORY_TYPE_WB;
    for (i = 0; i < PAGE_COUNT; ++i) {
        uint64_t physical = UINT64_C(0xC000) +
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
    identities[0].SystemIndex = 0;
    identities[1].Group = 1;
    identities[1].SystemIndex = 1;
    identities[2].Group = 1;
    identities[2].Number = 1;
    identities[2].SystemIndex = 2;
}

static int InitializeAll(VMX_EPT_DOMAIN* domain,
    VMX_EPT_ROOT_RECORD* root_records, VMX_EPT_ROOT_COORDINATOR* root,
    VMX_EPT_COVERAGE_RECORD* coverage_records,
    VMX_EPT_INVALIDATION_COVERAGE* coverage)
{
    VMX_PROCESSOR_IDENTITY identities[CONTEXT_COUNT];
    TEST_ASSERT_INT_EQ(InitializeDomain(domain), 0, "initialize domain");
    Identities(identities);
    memset(root_records, 0, CONTEXT_COUNT * sizeof(*root_records));
    memset(root, 0, sizeof(*root));
    memset(coverage_records, 0, CONTEXT_COUNT * sizeof(*coverage_records));
    memset(coverage, 0, sizeof(*coverage));
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorInitialize(domain, identities,
        CONTEXT_COUNT, root_records, root), VMX_EPT_ROOT_OK,
        "initialize root coordinator");
    TEST_ASSERT_INT_EQ(VmxEptCoverageInitialize(root, coverage_records, coverage),
        VMX_EPT_COVERAGE_OK, "initialize coverage");
    return 0;
}

static int Enter(VMX_EPT_ROOT_COORDINATOR* root, uint32_t index)
{
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorPrepareEnter(root, index),
        VMX_EPT_ROOT_OK, "prepare root enter");
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorCompleteEnter(root, index),
        VMX_EPT_ROOT_OK, "complete root enter");
    return 0;
}

static int Release(VMX_EPT_INVALIDATION_COVERAGE* coverage, uint32_t index)
{
    TEST_ASSERT_INT_EQ(VmxEptCoveragePrepareRelease(coverage, index),
        VMX_EPT_COVERAGE_OK, "prepare covered release");
    TEST_ASSERT_INT_EQ(VmxEptCoverageCompleteRelease(coverage, index),
        VMX_EPT_COVERAGE_OK, "complete covered release");
    return 0;
}

static VMX_EPT_EXECUTION_REPORT Report(
    const VMX_EPT_DOMAIN* domain, uint32_t result)
{
    VMX_EPT_EXECUTION_REPORT report = { 0 };
    report.Type = VMX_EPT_INVALIDATION_SINGLE_CONTEXT;
    report.CurrentIrql = VMX_EPT_EXECUTION_MAX_IRQL;
    report.Attempted = 1;
    report.InstructionResult = result;
    report.EptPointer = domain->EptPointer;
    return report;
}

static int DestroyAll(VMX_EPT_DOMAIN* domain,
    VMX_EPT_ROOT_COORDINATOR* root,
    VMX_EPT_INVALIDATION_COVERAGE* coverage)
{
    TEST_ASSERT_INT_EQ(VmxEptCoverageDestroy(coverage),
        VMX_EPT_COVERAGE_OK, "destroy coverage");
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorDestroy(root),
        VMX_EPT_ROOT_OK, "destroy root coordinator");
    TEST_ASSERT_INT_EQ(VmxEptDomainDestroy(domain),
        VMX_EPT_DOMAIN_OK, "destroy domain");
    return 0;
}

static int test_multi_context_initial_use(void)
{
    VMX_EPT_DOMAIN domain;
    VMX_EPT_ROOT_RECORD roots[CONTEXT_COUNT];
    VMX_EPT_ROOT_COORDINATOR root;
    VMX_EPT_COVERAGE_RECORD records[CONTEXT_COUNT];
    VMX_EPT_INVALIDATION_COVERAGE coverage;
    VMX_EPT_INVALIDATION_REQUEST request;
    VMX_EPT_INVALIDATION_DESCRIPTOR descriptor;
    VMX_EPT_EXECUTION_REPORT report;
    TEST_ASSERT_INT_EQ(InitializeAll(&domain, roots, &root, records, &coverage),
        0, "initialize initial-use fixture");
    TEST_ASSERT_INT_EQ(Enter(&root, 0), 0, "enter group zero");
    TEST_ASSERT_INT_EQ(Enter(&root, 1), 0, "enter group one zero");
    TEST_ASSERT_INT_EQ(Enter(&root, 2), 0, "enter group one one");
    TEST_ASSERT_INT_EQ(VmxEptCoverageMarkUse(&coverage, 0),
        VMX_EPT_COVERAGE_OK, "mark first EPTP use");
    TEST_ASSERT_UINT_EQ(domain.MutationGeneration, 2,
        "first use reserves generation");
    TEST_ASSERT_INT_EQ(VmxEptCoverageMarkUse(&coverage, 1),
        VMX_EPT_COVERAGE_OK, "join current generation");
    TEST_ASSERT_UINT_EQ(domain.MutationGeneration, 2,
        "second use shares generation");
    TEST_ASSERT_INT_EQ(VmxEptCoverageBuildInvalidation(
        &coverage, &request, &descriptor), VMX_EPT_COVERAGE_OK,
        "build shared invalidation");
    TEST_ASSERT_INT_EQ(VmxEptCoveragePrepareRelease(&coverage, 2),
        VMX_EPT_COVERAGE_DIRTY, "pending coverage blocks every release");
    report = Report(&domain, VMX_EPT_INSTRUCTION_SUCCESS);
    TEST_ASSERT_INT_EQ(VmxEptCoverageAcceptReport(&coverage, 0, &report),
        VMX_EPT_COVERAGE_OK, "accept first context");
    TEST_ASSERT_UINT_EQ(domain.PendingGeneration, 2,
        "first context cannot commit global generation");
    TEST_ASSERT_INT_EQ(VmxEptCoverageAcceptReport(&coverage, 0, &report),
        VMX_EPT_COVERAGE_BUSY, "duplicate acknowledgment rejected");
    report.InstructionResult = VMX_EPT_INSTRUCTION_FAIL_VALID;
    TEST_ASSERT_INT_EQ(VmxEptCoverageAcceptReport(&coverage, 1, &report),
        VMX_EPT_COVERAGE_INSTRUCTION_FAILED,
        "instruction failure remains retryable");
    TEST_ASSERT_UINT_EQ(records[1].AcknowledgedGeneration, 1,
        "failure preserves acknowledgment");
    report.InstructionResult = VMX_EPT_INSTRUCTION_SUCCESS;
    TEST_ASSERT_INT_EQ(VmxEptCoverageAcceptReport(&coverage, 1, &report),
        VMX_EPT_COVERAGE_OK, "final context commits generation");
    TEST_ASSERT_UINT_EQ(domain.PendingGeneration, 0, "pending cleared at N of N");
    TEST_ASSERT_UINT_EQ(domain.InvalidatedGeneration, 2,
        "global generation committed at N of N");
    TEST_ASSERT_INT_EQ(Release(&coverage, 0), 0, "release first used context");
    TEST_ASSERT_UINT_EQ(records[0].RequiredGeneration, 0,
        "release clears context coverage");
    TEST_ASSERT_INT_EQ(Release(&coverage, 1), 0, "release second used context");
    TEST_ASSERT_INT_EQ(Release(&coverage, 2), 0, "release unused context");
    return DestroyAll(&domain, &root, &coverage);
}

static int test_mutation_and_join(void)
{
    VMX_EPT_DOMAIN domain;
    VMX_EPT_ROOT_RECORD roots[CONTEXT_COUNT];
    VMX_EPT_ROOT_COORDINATOR root;
    VMX_EPT_COVERAGE_RECORD records[CONTEXT_COUNT];
    VMX_EPT_INVALIDATION_COVERAGE coverage;
    VMX_EPT_INVALIDATION_REQUEST request;
    VMX_EPT_INVALIDATION_DESCRIPTOR descriptor;
    VMX_EPT_EXECUTION_REPORT report;
    uint32_t i;
    TEST_ASSERT_INT_EQ(InitializeAll(&domain, roots, &root, records, &coverage),
        0, "initialize mutation fixture");
    for (i = 0; i < CONTEXT_COUNT; ++i)
        TEST_ASSERT_INT_EQ(Enter(&root, i), 0, "enter mutation context");
    TEST_ASSERT_INT_EQ(VmxEptCoverageMarkUse(&coverage, 0),
        VMX_EPT_COVERAGE_OK, "use context zero");
    TEST_ASSERT_INT_EQ(VmxEptCoverageMarkUse(&coverage, 1),
        VMX_EPT_COVERAGE_OK, "use context one");
    TEST_ASSERT_INT_EQ(VmxEptCoverageBuildInvalidation(
        &coverage, &request, &descriptor), VMX_EPT_COVERAGE_OK,
        "build initial coverage");
    report = Report(&domain, VMX_EPT_INSTRUCTION_SUCCESS);
    TEST_ASSERT_INT_EQ(VmxEptCoverageAcceptReport(&coverage, 0, &report),
        VMX_EPT_COVERAGE_OK, "ack initial zero");
    TEST_ASSERT_INT_EQ(VmxEptCoverageAcceptReport(&coverage, 1, &report),
        VMX_EPT_COVERAGE_OK, "ack initial one");
    TEST_ASSERT_INT_EQ(VmxEptCoverageBeginMutation(&coverage),
        VMX_EPT_COVERAGE_OK, "reserve mutation generation");
    TEST_ASSERT_UINT_EQ(records[0].RequiredGeneration, 3,
        "mutation updates first requirement");
    TEST_ASSERT_UINT_EQ(records[0].AcknowledgedGeneration, 2,
        "mutation retains prior acknowledgment");
    TEST_ASSERT_INT_EQ(VmxEptCoverageMarkUse(&coverage, 2),
        VMX_EPT_COVERAGE_OK, "new context joins dirty generation");
    TEST_ASSERT_INT_EQ(VmxEptCoverageBeginMutation(&coverage),
        VMX_EPT_COVERAGE_DIRTY, "uncommitted generation blocks mutation");
    TEST_ASSERT_INT_EQ(VmxEptCoverageBuildInvalidation(
        &coverage, &request, &descriptor), VMX_EPT_COVERAGE_OK,
        "build mutation coverage");
    report = Report(&domain, VMX_EPT_INSTRUCTION_SUCCESS);
    report.EptPointer++;
    TEST_ASSERT_INT_EQ(VmxEptCoverageAcceptReport(&coverage, 2, &report),
        VMX_EPT_COVERAGE_INVALID, "wrong EPTP report rejected");
    report.EptPointer = domain.EptPointer;
    TEST_ASSERT_INT_EQ(VmxEptCoverageAcceptReport(&coverage, 2, &report),
        VMX_EPT_COVERAGE_OK, "ack joined context");
    TEST_ASSERT_INT_EQ(VmxEptCoverageAcceptReport(&coverage, 0, &report),
        VMX_EPT_COVERAGE_OK, "ack mutation zero");
    TEST_ASSERT_UINT_EQ(domain.PendingGeneration, 3,
        "two of three cannot commit");
    TEST_ASSERT_INT_EQ(VmxEptCoverageAcceptReport(&coverage, 1, &report),
        VMX_EPT_COVERAGE_OK, "ack mutation final");
    TEST_ASSERT_UINT_EQ(domain.InvalidatedGeneration, 3,
        "mutation commits only after complete coverage");
    for (i = 0; i < CONTEXT_COUNT; ++i)
        TEST_ASSERT_INT_EQ(Release(&coverage, i), 0, "release mutation context");
    return DestroyAll(&domain, &root, &coverage);
}

static int test_rejections_cancel_and_authentication(void)
{
    VMX_EPT_DOMAIN domain;
    VMX_EPT_ROOT_RECORD roots[CONTEXT_COUNT];
    VMX_EPT_ROOT_COORDINATOR root;
    VMX_EPT_COVERAGE_RECORD records[CONTEXT_COUNT];
    VMX_EPT_INVALIDATION_COVERAGE coverage;
    VMX_EPT_INVALIDATION_COVERAGE copy;
    VMX_EPT_INVALIDATION_REQUEST request;
    VMX_EPT_INVALIDATION_DESCRIPTOR descriptor;
    VMX_EPT_EXECUTION_REPORT report;
    TEST_ASSERT_INT_EQ(InitializeAll(&domain, roots, &root, records, &coverage),
        0, "initialize rejection fixture");
    TEST_ASSERT_INT_EQ(VmxEptCoverageMarkUse(&coverage, 0),
        VMX_EPT_COVERAGE_NOT_READY, "use requires owned root");
    TEST_ASSERT_INT_EQ(VmxEptCoverageBeginMutation(&coverage),
        VMX_EPT_COVERAGE_NOT_READY, "mutation requires used context");
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorPrepareEnter(&root, 0),
        VMX_EPT_ROOT_OK, "prepare enter blocks coverage");
    TEST_ASSERT_INT_EQ(VmxEptCoverageMarkUse(&coverage, 0),
        VMX_EPT_COVERAGE_BUSY, "root transition blocks use");
    TEST_ASSERT_INT_EQ(VmxEptRootCoordinatorCompleteEnter(&root, 0),
        VMX_EPT_ROOT_OK, "complete rejection enter");
    TEST_ASSERT_INT_EQ(VmxEptCoverageMarkUse(&coverage, 0),
        VMX_EPT_COVERAGE_OK, "mark rejection use");
    TEST_ASSERT_INT_EQ(VmxEptCoverageMarkUse(&coverage, 0),
        VMX_EPT_COVERAGE_BUSY, "duplicate use rejected");
    TEST_ASSERT_INT_EQ(VmxEptCoverageBuildInvalidation(&coverage,
        (VMX_EPT_INVALIDATION_REQUEST*)(void*)&records[0], &descriptor),
        VMX_EPT_COVERAGE_INVALID, "output cannot alias coverage record");
    TEST_ASSERT_UINT_EQ(domain.PendingGeneration, 0,
        "alias rejection preserves domain");
    TEST_ASSERT_INT_EQ(VmxEptCoverageBuildInvalidation(
        &coverage, &request, &descriptor), VMX_EPT_COVERAGE_OK,
        "build rejection invalidation");
    report = Report(&domain, VMX_EPT_INSTRUCTION_SUCCESS);
    TEST_ASSERT_INT_EQ(VmxEptCoverageAcceptReport(&coverage, 1, &report),
        VMX_EPT_COVERAGE_NOT_READY, "unused context cannot acknowledge");
    TEST_ASSERT_INT_EQ(VmxEptCoverageAcceptReport(&coverage, 0, &report),
        VMX_EPT_COVERAGE_OK, "clean rejection generation");
    domain.MutationGeneration = UINT64_MAX;
    domain.InvalidatedGeneration = UINT64_MAX;
    records[0].RequiredGeneration = UINT64_MAX;
    records[0].AcknowledgedGeneration = UINT64_MAX;
    TEST_ASSERT_INT_EQ(VmxEptCoverageBeginMutation(&coverage),
        VMX_EPT_COVERAGE_OVERFLOW, "generation overflow mapped exactly");
    domain.MutationGeneration = 2;
    domain.InvalidatedGeneration = 2;
    records[0].RequiredGeneration = 2;
    records[0].AcknowledgedGeneration = 2;
    TEST_ASSERT_INT_EQ(VmxEptCoveragePrepareRelease(&coverage, 0),
        VMX_EPT_COVERAGE_OK, "prepare cancellable release");
    TEST_ASSERT_INT_EQ(VmxEptCoverageCancelRelease(&coverage, 0),
        VMX_EPT_COVERAGE_OK, "cancel failed native release");
    TEST_ASSERT_UINT_EQ(records[0].RequiredGeneration, 2,
        "cancel preserves coverage record");
    copy = coverage;
    copy.Count--;
    TEST_ASSERT_INT_EQ(VmxEptCoverageValidate(&copy),
        VMX_EPT_COVERAGE_INVALID, "count authenticated");
    records[0].AcknowledgedGeneration = 1;
    TEST_ASSERT_INT_EQ(VmxEptCoverageValidate(&coverage),
        VMX_EPT_COVERAGE_INVALID, "clean acknowledgment authenticated");
    records[0].AcknowledgedGeneration = 2;
    TEST_ASSERT_INT_EQ(VmxEptCoverageDestroy(&coverage),
        VMX_EPT_COVERAGE_BUSY, "owned root blocks coverage destroy");
    TEST_ASSERT_INT_EQ(Release(&coverage, 0), 0, "release rejection root");
    return DestroyAll(&domain, &root, &coverage);
}

int main(void)
{
    int result = 0;
    result |= test_multi_context_initial_use();
    result |= test_mutation_and_join();
    result |= test_rejections_cancel_and_authentication();
    if (result == 0)
        printf("All EPT invalidation-coverage tests passed.\n");
    return result;
}