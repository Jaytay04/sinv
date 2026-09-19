#include "test_common.h"
#include "vmx_ept_domain.h"

#include <stddef.h>
#include <stdint.h>

#define RAW_REQUIRED ((UINT64_C(1) << 6) | (UINT64_C(1) << 14) | \
    (UINT64_C(1) << 16) | (UINT64_C(1) << 20) | \
    (UINT64_C(1) << 25) | (UINT64_C(1) << 26))
#define DOMAIN_PAGES 3

static _Alignas(4096) unsigned char g_storage[DOMAIN_PAGES][4096];
static VMX_EPT_BOUND_PAGE g_pages[DOMAIN_PAGES];
static VMX_EPT_PHYSICAL_PAGE g_index[DOMAIN_PAGES];

_Static_assert(VMX_EPT_DOMAIN_STAGE_READY == 1, "ready stage");
_Static_assert(VMX_EPT_DOMAIN_STAGE_ROOT_OWNED == 2, "root stage");
_Static_assert(VMX_EPT_DOMAIN_INSTRUCTION_FAILED == -6,
    "instruction failure status");
_Static_assert(offsetof(VMX_EPT_DOMAIN, MutationGeneration) == 16,
    "generation offset");

static void BuildFixture(
    VMX_EPT_CAPABILITIES* caps,
    VMX_EPT_IDENTITY_CONFIG* identity,
    VMX_EPT_IDENTITY_PLAN* plan,
    VMX_EPT_POPULATE_CONFIG* populate,
    VMX_EPT_STORAGE_BINDING* binding)
{
    static const uint64_t addresses[DOMAIN_PAGES] = {
        UINT64_C(0x9000), UINT64_C(0x3000), UINT64_C(0x7000)
    };
    uint32_t i;
    uint32_t j;
    memset(caps, 0, sizeof(*caps));
    TEST_ASSERT_INT_EQ(VmxEptDecodeCapabilities(RAW_REQUIRED, caps),
        VMX_EPT_STATUS_OK, "decode capabilities");
    memset(identity, 0, sizeof(*identity));
    identity->MappedBytes = UINT64_C(0x200000);
    identity->PhysicalAddressBits = 48;
    identity->LeafKind = VMX_EPT_ENTRY_LEAF_2MB;
    TEST_ASSERT_INT_EQ(VmxEptBuildIdentityPlan(caps, identity, plan),
        VMX_EPT_STATUS_OK, "build plan");
    TEST_ASSERT_UINT_EQ(plan->TotalTablePages, DOMAIN_PAGES,
        "fixture page count");
    memset(populate, 0, sizeof(*populate));
    populate->Permissions = VMX_EPT_PERMISSION_MASK;
    populate->MemoryType = VMX_EPT_MEMORY_TYPE_WB;
    memset(g_pages, 0, sizeof(g_pages));
    memset(g_index, 0, sizeof(g_index));
    for (i = 0; i < DOMAIN_PAGES; ++i) {
        g_pages[i].PhysicalAddress = addresses[i];
        g_index[i].PhysicalAddress = addresses[i];
        g_index[i].Ordinal = i;
    }
    for (i = 0; i < DOMAIN_PAGES; ++i) {
        for (j = i + 1; j < DOMAIN_PAGES; ++j) {
            if (g_index[j].PhysicalAddress < g_index[i].PhysicalAddress) {
                VMX_EPT_PHYSICAL_PAGE swap = g_index[i];
                g_index[i] = g_index[j];
                g_index[j] = swap;
            }
        }
    }
    for (i = 0; i < DOMAIN_PAGES; ++i)
        g_pages[g_index[i].Ordinal].PhysicalRank = i;
    memset(binding, 0, sizeof(*binding));
    binding->VirtualBase = g_storage;
    binding->VirtualBytes = plan->TotalTableBytes;
    binding->PagesByOrdinal = g_pages;
    binding->PhysicalIndex = g_index;
    binding->PageCount = DOMAIN_PAGES;
}

static void InitializeDomain(VMX_EPT_DOMAIN* domain)
{
    VMX_EPT_CAPABILITIES caps;
    VMX_EPT_IDENTITY_CONFIG identity;
    VMX_EPT_IDENTITY_PLAN plan;
    VMX_EPT_POPULATE_CONFIG populate;
    VMX_EPT_STORAGE_BINDING binding;
    BuildFixture(&caps, &identity, &plan, &populate, &binding);
    memset(g_storage, 0xA5, sizeof(g_storage));
    memset(domain, 0, sizeof(*domain));
    TEST_ASSERT_INT_EQ(VmxEptDomainInitialize(
        &caps, &identity, &plan, &populate, &binding, domain),
        VMX_EPT_DOMAIN_OK, "initialize domain");
}

static VMX_EPT_EXECUTION_REPORT SuccessReport(const VMX_EPT_DOMAIN* domain)
{
    VMX_EPT_EXECUTION_REPORT report = { 0 };
    report.Type = VMX_EPT_INVALIDATION_SINGLE_CONTEXT;
    report.CurrentIrql = VMX_EPT_EXECUTION_MAX_IRQL;
    report.Attempted = 1;
    report.InstructionResult = VMX_EPT_INSTRUCTION_SUCCESS;
    report.EptPointer = domain->EptPointer;
    return report;
}

static void MakeCleanRoot(VMX_EPT_DOMAIN* domain)
{
    VMX_EPT_INVALIDATION_REQUEST request;
    VMX_EPT_INVALIDATION_DESCRIPTOR descriptor;
    VMX_EPT_EXECUTION_REPORT report;
    TEST_ASSERT_INT_EQ(VmxEptDomainPrepareRootEnter(domain),
        VMX_EPT_DOMAIN_OK, "prepare root enter");
    TEST_ASSERT_INT_EQ(VmxEptDomainAdoptRoot(domain), VMX_EPT_DOMAIN_OK,
        "adopt root");
    TEST_ASSERT_INT_EQ(VmxEptDomainBeginUse(domain), VMX_EPT_DOMAIN_OK,
        "mark EPTP use");
    TEST_ASSERT_INT_EQ(VmxEptDomainBuildInvalidation(
        domain, &request, &descriptor), VMX_EPT_DOMAIN_OK,
        "build initial invalidation");
    report = SuccessReport(domain);
    TEST_ASSERT_INT_EQ(VmxEptDomainCommitInvalidation(domain, &report),
        VMX_EPT_DOMAIN_OK, "commit initial invalidation");
}

static int test_initialize_and_clean_destroy(void)
{
    VMX_EPT_DOMAIN domain;
    uint64_t* pml4;
    InitializeDomain(&domain);
    TEST_ASSERT_INT_EQ(VmxEptDomainValidate(&domain), VMX_EPT_DOMAIN_OK,
        "domain authentic");
    TEST_ASSERT_UINT_EQ(domain.Stage, VMX_EPT_DOMAIN_STAGE_READY,
        "initial ready");
    TEST_ASSERT_UINT_EQ(domain.MutationGeneration, 1,
        "initial generation");
    TEST_ASSERT_UINT_EQ(domain.InvalidatedGeneration, 1,
        "initial publication clean");
    TEST_ASSERT_UINT_EQ(domain.Pointer.Pml4PhysicalAddress,
        g_pages[0].PhysicalAddress, "root physical address");
    TEST_ASSERT_UINT_EQ(domain.EptPointer,
        g_pages[0].PhysicalAddress | UINT64_C(0x1E), "expected EPTP");
    pml4 = (uint64_t*)(void*)g_storage[0];
    TEST_ASSERT_UINT_EQ(pml4[0], g_pages[1].PhysicalAddress | UINT64_C(7),
        "populated root link");
    TEST_ASSERT_INT_EQ(VmxEptDomainDestroy(&domain), VMX_EPT_DOMAIN_OK,
        "unused domain destroys without VMX");
    InitializeDomain(&domain);
    MakeCleanRoot(&domain);
    TEST_ASSERT_INT_EQ(VmxEptDomainPrepareRootRelease(&domain),
        VMX_EPT_DOMAIN_OK, "prepare clean release");
    TEST_ASSERT_INT_EQ(VmxEptDomainCompleteRootRelease(&domain),
        VMX_EPT_DOMAIN_OK, "complete clean release");
    TEST_ASSERT_INT_EQ(VmxEptDomainDestroy(&domain), VMX_EPT_DOMAIN_OK,
        "destroy invalidated domain");
    TEST_ASSERT_UINT_EQ(domain.Magic, 0, "destroy zeros domain");
    return 0;
}

static int test_modification_retry_and_gates(void)
{
    VMX_EPT_DOMAIN domain;
    VMX_EPT_INVALIDATION_REQUEST request;
    VMX_EPT_INVALIDATION_DESCRIPTOR descriptor;
    VMX_EPT_EXECUTION_REPORT report;
    InitializeDomain(&domain);
    MakeCleanRoot(&domain);
    TEST_ASSERT_INT_EQ(VmxEptDomainPrepareRootEnter(&domain),
        VMX_EPT_DOMAIN_BUSY, "root already owned");
    TEST_ASSERT_INT_EQ(VmxEptDomainAdoptRoot(&domain), VMX_EPT_DOMAIN_BUSY,
        "double root adoption");
    TEST_ASSERT_INT_EQ(VmxEptDomainBeginMutation(&domain), VMX_EPT_DOMAIN_OK,
        "mark modified");
    TEST_ASSERT_UINT_EQ(domain.MutationGeneration, 3,
        "modified generation");
    TEST_ASSERT_INT_EQ(VmxEptDomainPrepareRootRelease(&domain),
        VMX_EPT_DOMAIN_DIRTY, "dirty root cannot prepare release");
    TEST_ASSERT_INT_EQ(VmxEptDomainCompleteRootRelease(&domain),
        VMX_EPT_DOMAIN_DIRTY, "dirty root cannot complete release");
    TEST_ASSERT_INT_EQ(VmxEptDomainBuildInvalidation(
        &domain, &request, &descriptor), VMX_EPT_DOMAIN_OK,
        "build modified invalidation");
    TEST_ASSERT_UINT_EQ(request.Type, VMX_EPT_INVALIDATION_SINGLE_CONTEXT,
        "single request");
    TEST_ASSERT_UINT_EQ(descriptor.EptPointer, domain.EptPointer,
        "descriptor EPTP");
    TEST_ASSERT_UINT_EQ(domain.PendingGeneration, 3,
        "pending generation");
    TEST_ASSERT_INT_EQ(VmxEptDomainBeginMutation(&domain), VMX_EPT_DOMAIN_BUSY,
        "pending blocks modification");
    report = SuccessReport(&domain);
    report.InstructionResult = VMX_EPT_INSTRUCTION_FAIL_VALID;
    TEST_ASSERT_INT_EQ(VmxEptDomainCommitInvalidation(&domain, &report),
        VMX_EPT_DOMAIN_INSTRUCTION_FAILED, "instruction failure retryable");
    TEST_ASSERT_UINT_EQ(domain.PendingGeneration, 3,
        "failure preserves pending");
    TEST_ASSERT_INT_EQ(VmxEptDomainBuildInvalidation(
        &domain, &request, &descriptor), VMX_EPT_DOMAIN_OK,
        "pending request rebuild is idempotent");
    report = SuccessReport(&domain);
    TEST_ASSERT_INT_EQ(VmxEptDomainCommitInvalidation(&domain, &report),
        VMX_EPT_DOMAIN_OK, "retry commit");
    TEST_ASSERT_UINT_EQ(domain.PendingGeneration, 0, "pending cleared");
    TEST_ASSERT_UINT_EQ(domain.InvalidatedGeneration, 3,
        "generation invalidated");
    TEST_ASSERT_INT_EQ(VmxEptDomainPrepareRootRelease(&domain),
        VMX_EPT_DOMAIN_OK, "prepare release after retry");
    TEST_ASSERT_INT_EQ(VmxEptDomainCompleteRootRelease(&domain),
        VMX_EPT_DOMAIN_OK, "complete release after retry");
    TEST_ASSERT_INT_EQ(VmxEptDomainBeginMutation(&domain),
        VMX_EPT_DOMAIN_NOT_READY, "modification requires root");
    TEST_ASSERT_INT_EQ(VmxEptDomainDestroy(&domain), VMX_EPT_DOMAIN_OK,
        "destroy after retry");
    return 0;
}

static int test_output_and_report_validation(void)
{
    VMX_EPT_DOMAIN domain;
    VMX_EPT_INVALIDATION_REQUEST request;
    VMX_EPT_INVALIDATION_DESCRIPTOR descriptor;
    VMX_EPT_EXECUTION_REPORT report;
    unsigned char request_sentinel[sizeof(request)];
    unsigned char descriptor_sentinel[sizeof(descriptor)];
    InitializeDomain(&domain);
    memset(&request, 0xA5, sizeof(request));
    memset(&descriptor, 0xA5, sizeof(descriptor));
    memcpy(request_sentinel, &request, sizeof(request));
    memcpy(descriptor_sentinel, &descriptor, sizeof(descriptor));
    TEST_ASSERT_INT_EQ(VmxEptDomainBeginUse(&domain),
        VMX_EPT_DOMAIN_NOT_READY, "use requires root");
    TEST_ASSERT_INT_EQ(VmxEptDomainBuildInvalidation(
        &domain, &request, &descriptor), VMX_EPT_DOMAIN_NOT_READY,
        "build requires root");
    TEST_ASSERT_TRUE(memcmp(&request, request_sentinel, sizeof(request)) == 0,
        "request preserved");
    TEST_ASSERT_TRUE(memcmp(&descriptor, descriptor_sentinel,
        sizeof(descriptor)) == 0, "descriptor preserved");
    TEST_ASSERT_INT_EQ(VmxEptDomainPrepareRootEnter(&domain),
        VMX_EPT_DOMAIN_OK, "prepare validation root");
    TEST_ASSERT_INT_EQ(VmxEptDomainAdoptRoot(&domain), VMX_EPT_DOMAIN_OK,
        "adopt for validation");
    TEST_ASSERT_INT_EQ(VmxEptDomainBeginMutation(&domain), VMX_EPT_DOMAIN_OK,
        "mark validation modification");
    TEST_ASSERT_INT_EQ(VmxEptDomainBuildInvalidation(&domain,
        (VMX_EPT_INVALIDATION_REQUEST*)(void*)&domain, &descriptor),
        VMX_EPT_DOMAIN_INVALID, "request cannot alias domain");
    TEST_ASSERT_INT_EQ(VmxEptDomainBuildInvalidation(
        &domain, &request, &descriptor), VMX_EPT_DOMAIN_OK,
        "build valid request");
    report = SuccessReport(&domain);
    report.Attempted = 0;
    TEST_ASSERT_INT_EQ(VmxEptDomainCommitInvalidation(&domain, &report),
        VMX_EPT_DOMAIN_INVALID, "not-attempted report invalid");
    report = SuccessReport(&domain);
    report.CurrentIrql = VMX_EPT_EXECUTION_MAX_IRQL + 1;
    TEST_ASSERT_INT_EQ(VmxEptDomainCommitInvalidation(&domain, &report),
        VMX_EPT_DOMAIN_INVALID, "high IRQL report invalid");
    report = SuccessReport(&domain);
    report.EptPointer++;
    TEST_ASSERT_INT_EQ(VmxEptDomainCommitInvalidation(&domain, &report),
        VMX_EPT_DOMAIN_INVALID, "wrong EPTP report invalid");
    report = SuccessReport(&domain);
    report.InstructionResult = VMX_EPT_INSTRUCTION_NOT_ATTEMPTED;
    TEST_ASSERT_INT_EQ(VmxEptDomainCommitInvalidation(&domain, &report),
        VMX_EPT_DOMAIN_INVALID, "invalid result rejected");
    report = SuccessReport(&domain);
    TEST_ASSERT_INT_EQ(VmxEptDomainCommitInvalidation(&domain, &report),
        VMX_EPT_DOMAIN_OK, "valid report accepted");
    TEST_ASSERT_INT_EQ(VmxEptDomainPrepareRootRelease(&domain),
        VMX_EPT_DOMAIN_OK, "prepare validated release");
    TEST_ASSERT_INT_EQ(VmxEptDomainCompleteRootRelease(&domain),
        VMX_EPT_DOMAIN_OK, "complete validated release");
    TEST_ASSERT_INT_EQ(VmxEptDomainDestroy(&domain), VMX_EPT_DOMAIN_OK,
        "destroy validated domain");
    return 0;
}

static int test_authentication_and_initialize_failures(void)
{
    VMX_EPT_CAPABILITIES caps;
    VMX_EPT_IDENTITY_CONFIG identity;
    VMX_EPT_IDENTITY_PLAN plan;
    VMX_EPT_POPULATE_CONFIG populate;
    VMX_EPT_STORAGE_BINDING binding;
    VMX_EPT_DOMAIN domain;
    VMX_EPT_DOMAIN copy;
    BuildFixture(&caps, &identity, &plan, &populate, &binding);
    memset(&domain, 0, sizeof(domain));
    memset(g_storage, 0xA5, sizeof(g_storage));
    binding.VirtualBytes -= VMX_EPT_TABLE_PAGE_SIZE;
    TEST_ASSERT_INT_EQ(VmxEptDomainInitialize(
        &caps, &identity, &plan, &populate, &binding, &domain),
        VMX_EPT_DOMAIN_INVALID, "invalid binding rejected");
    TEST_ASSERT_UINT_EQ(domain.Magic, 0, "failed init preserves domain");
    TEST_ASSERT_UINT_EQ(g_storage[0][0], 0xA5,
        "failed init preserves storage");
    BuildFixture(&caps, &identity, &plan, &populate, &binding);
    memset(&domain, 0xA5, sizeof(domain));
    TEST_ASSERT_INT_EQ(VmxEptDomainInitialize(
        &caps, &identity, &plan, &populate, &binding, &domain),
        VMX_EPT_DOMAIN_INVALID, "nonzero owner rejected");
    InitializeDomain(&domain);
    copy = domain;
    copy.Magic++;
    TEST_ASSERT_INT_EQ(VmxEptDomainValidate(&copy), VMX_EPT_DOMAIN_INVALID,
        "magic authenticated");
    copy = domain;
    copy.Stage = 3;
    TEST_ASSERT_INT_EQ(VmxEptDomainValidate(&copy), VMX_EPT_DOMAIN_INVALID,
        "stage authenticated");
    copy = domain;
    copy.Pointer.Pml4PhysicalAddress += VMX_EPT_TABLE_PAGE_SIZE;
    TEST_ASSERT_INT_EQ(VmxEptDomainValidate(&copy), VMX_EPT_DOMAIN_INVALID,
        "root pointer authenticated");
    copy = domain;
    copy.PendingGeneration = 2;
    TEST_ASSERT_INT_EQ(VmxEptDomainValidate(&copy), VMX_EPT_DOMAIN_INVALID,
        "pending generation authenticated");
    MakeCleanRoot(&domain);
    domain.MutationGeneration = UINT64_MAX;
    domain.InvalidatedGeneration = UINT64_MAX;
    TEST_ASSERT_INT_EQ(VmxEptDomainBeginMutation(&domain),
        VMX_EPT_DOMAIN_OVERFLOW, "generation overflow rejected");
    TEST_ASSERT_INT_EQ(VmxEptDomainPrepareRootRelease(&domain),
        VMX_EPT_DOMAIN_OK, "prepare overflow release");
    TEST_ASSERT_INT_EQ(VmxEptDomainCompleteRootRelease(&domain),
        VMX_EPT_DOMAIN_OK, "complete overflow release");
    TEST_ASSERT_INT_EQ(VmxEptDomainDestroy(&domain), VMX_EPT_DOMAIN_OK,
        "destroy overflow fixture");
    return 0;
}

static int test_initialize_span_admission(void)
{
    typedef union OVERLAPPING_CONFIG {
        VMX_EPT_CAPABILITIES Capabilities;
        VMX_EPT_IDENTITY_CONFIG Identity;
    } OVERLAPPING_CONFIG;
    VMX_EPT_CAPABILITIES caps;
    VMX_EPT_IDENTITY_CONFIG identity;
    VMX_EPT_IDENTITY_PLAN plan;
    VMX_EPT_POPULATE_CONFIG populate;
    VMX_EPT_STORAGE_BINDING binding;
    VMX_EPT_DOMAIN domain;
    OVERLAPPING_CONFIG overlap;

    BuildFixture(&caps, &identity, &plan, &populate, &binding);
    memset(&overlap, 0, sizeof(overlap));
    overlap.Capabilities = caps;
    memset(&domain, 0, sizeof(domain));
    memset(g_storage, 0xA5, sizeof(g_storage));
    TEST_ASSERT_INT_EQ(VmxEptDomainInitialize(
        &overlap.Capabilities, &overlap.Identity, &plan, &populate,
        &binding, &domain), VMX_EPT_DOMAIN_INVALID,
        "overlapping configuration inputs rejected");
    TEST_ASSERT_UINT_EQ(domain.Magic, 0u,
        "configuration overlap preserves domain");
    TEST_ASSERT_UINT_EQ(g_storage[0][0], 0xA5u,
        "configuration overlap preserves storage");

    BuildFixture(&caps, &identity, &plan, &populate, &binding);
    binding.PagesByOrdinal = (VMX_EPT_BOUND_PAGE*)(void*)&caps;
    memset(&domain, 0, sizeof(domain));
    memset(g_storage, 0xA5, sizeof(g_storage));
    TEST_ASSERT_INT_EQ(VmxEptDomainInitialize(
        &caps, &identity, &plan, &populate, &binding, &domain),
        VMX_EPT_DOMAIN_INVALID,
        "configuration and metadata overlap rejected");
    TEST_ASSERT_UINT_EQ(domain.Magic, 0u,
        "metadata overlap preserves domain");
    TEST_ASSERT_UINT_EQ(g_storage[0][0], 0xA5u,
        "metadata overlap preserves storage");

    BuildFixture(&caps, &identity, &plan, &populate, &binding);
    memset(&domain, 0, sizeof(domain));
    memset(g_storage, 0xA5, sizeof(g_storage));
    TEST_ASSERT_INT_EQ(VmxEptDomainInitialize(
        (const VMX_EPT_CAPABILITIES*)(uintptr_t)(
            UINTPTR_MAX - sizeof(VMX_EPT_CAPABILITIES) + 2u),
        &identity, &plan, &populate, &binding, &domain),
        VMX_EPT_DOMAIN_INVALID, "wrapping configuration span rejected");
    TEST_ASSERT_UINT_EQ(domain.Magic, 0u,
        "wrapping span preserves domain");
    TEST_ASSERT_UINT_EQ(g_storage[0][0], 0xA5u,
        "wrapping span preserves storage");
    return 0;
}

int main(void)
{
    int result = 0;
    result |= test_initialize_and_clean_destroy();
    result |= test_modification_retry_and_gates();
    result |= test_output_and_report_validation();
    result |= test_authentication_and_initialize_failures();
    result |= test_initialize_span_admission();
    if (result == 0)
        printf("All EPT domain ownership tests passed.\n");
    return result;
}
