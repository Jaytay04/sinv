#include "test_common.h"
#include "vmx_ept_storage_owner.h"

#include <stddef.h>

#define RAW_REQUIRED ((UINT64_C(1) << 6) | (UINT64_C(1) << 14) | \
    (UINT64_C(1) << 16) | (UINT64_C(1) << 20) | \
    (UINT64_C(1) << 25) | (UINT64_C(1) << 26))
#define PAGE_COUNT 3

static _Alignas(4096) unsigned char g_tables[PAGE_COUNT][4096];
static VMX_EPT_BOUND_PAGE g_bound[PAGE_COUNT];
static VMX_EPT_PHYSICAL_PAGE g_index[PAGE_COUNT];

typedef struct FAKE_ALLOCATOR {
    VMX_EPT_CAPABILITIES* MutableCapabilities;
    VMX_EPT_IDENTITY_CONFIG* MutableIdentity;
    VMX_EPT_POPULATE_CONFIG* MutablePopulate;
    VMX_PERSISTENT_ALLOCATOR* MutableAllocator;
    uint32_t AllocationCall;
    uint32_t MutateInputsOnCall;
    uint32_t FailCall;
    uint32_t AliasCall;
    uint32_t UnalignedTables;
    uint32_t BadPhysicalPage;
    uint32_t FreeCount;
    uint32_t FreeKind[4];
    void* FreePointer[4];
} FAKE_ALLOCATOR;

_Static_assert(VMX_EPT_STORAGE_OWNER_READY == 1, "ready stage");
_Static_assert(VMX_EPT_STORAGE_REGION_INVALID == -4, "region status");
_Static_assert(offsetof(VMX_EPT_STORAGE_OWNER, Domain) >
    offsetof(VMX_EPT_STORAGE_OWNER, Tables), "domain follows allocations");

static void* FakeContiguous(void* context, size_t size, uint64_t highest)
{
    FAKE_ALLOCATOR* fake = (FAKE_ALLOCATOR*)context;
    (void)highest;
    fake->AllocationCall++;
    if (fake->MutateInputsOnCall == fake->AllocationCall) {
        memset(fake->MutableCapabilities, 0,
            sizeof(*fake->MutableCapabilities));
        memset(fake->MutableIdentity, 0, sizeof(*fake->MutableIdentity));
        memset(fake->MutablePopulate, 0, sizeof(*fake->MutablePopulate));
        memset(fake->MutableAllocator, 0, sizeof(*fake->MutableAllocator));
    }
    if (fake->FailCall == fake->AllocationCall || size != sizeof(g_tables))
        return 0;
    return fake->UnalignedTables ? (void*)&g_tables[0][1] : (void*)g_tables;
}

static void* FakePool(void* context, size_t size)
{
    FAKE_ALLOCATOR* fake = (FAKE_ALLOCATOR*)context;
    fake->AllocationCall++;
    if (fake->FailCall == fake->AllocationCall ||
        size != PAGE_COUNT * sizeof(VMX_EPT_BOUND_PAGE))
        return 0;
    if (fake->AliasCall == fake->AllocationCall)
        return g_tables;
    return fake->AllocationCall == 2 ? (void*)g_bound : (void*)g_index;
}

static void RecordFree(FAKE_ALLOCATOR* fake, uint32_t kind, void* memory)
{
    TEST_ASSERT_TRUE(fake->FreeCount < 4, "free log capacity");
    fake->FreeKind[fake->FreeCount] = kind;
    fake->FreePointer[fake->FreeCount] = memory;
    fake->FreeCount++;
}

static void FakeFreePool(void* context, void* memory)
{
    RecordFree((FAKE_ALLOCATOR*)context, 1, memory);
}

static void FakeFreeContiguous(void* context, void* memory)
{
    RecordFree((FAKE_ALLOCATOR*)context, 2, memory);
}

static uint64_t FakePhysical(void* context, const void* memory)
{
    FAKE_ALLOCATOR* fake = (FAKE_ALLOCATOR*)context;
    uintptr_t offset = (uintptr_t)memory - (uintptr_t)g_tables;
    uint32_t page = (uint32_t)(offset / VMX_EPT_TABLE_PAGE_SIZE);
    if (fake->BadPhysicalPage == page + 1u)
        return UINT64_C(0x9000) + offset + VMX_EPT_TABLE_PAGE_SIZE;
    return UINT64_C(0x9000) + offset;
}

static VMX_PERSISTENT_ALLOCATOR Allocator(FAKE_ALLOCATOR* fake)
{
    VMX_PERSISTENT_ALLOCATOR allocator = { 0 };
    allocator.Context = fake;
    allocator.AllocatePool = FakePool;
    allocator.FreePool = FakeFreePool;
    allocator.AllocateContiguous = FakeContiguous;
    allocator.FreeContiguous = FakeFreeContiguous;
    allocator.GetPhysicalAddress = FakePhysical;
    return allocator;
}

static VMX_EPT_CAPABILITIES Caps(void)
{
    VMX_EPT_CAPABILITIES caps = { 0 };
    TEST_ASSERT_INT_EQ(VmxEptDecodeCapabilities(RAW_REQUIRED, &caps),
        VMX_EPT_STATUS_OK, "decode caps");
    return caps;
}

static VMX_EPT_IDENTITY_CONFIG Identity(void)
{
    VMX_EPT_IDENTITY_CONFIG identity = { 0 };
    identity.MappedBytes = UINT64_C(0x200000);
    identity.PhysicalAddressBits = 48;
    identity.LeafKind = VMX_EPT_ENTRY_LEAF_2MB;
    return identity;
}

static VMX_EPT_POPULATE_CONFIG Populate(void)
{
    VMX_EPT_POPULATE_CONFIG populate = { 0 };
    populate.Permissions = VMX_EPT_PERMISSION_MASK;
    populate.MemoryType = VMX_EPT_MEMORY_TYPE_WB;
    return populate;
}

static VMX_EPT_STORAGE_STATUS Initialize(
    FAKE_ALLOCATOR* fake, VMX_EPT_STORAGE_OWNER* owner)
{
    VMX_EPT_CAPABILITIES caps = Caps();
    VMX_EPT_IDENTITY_CONFIG identity = Identity();
    VMX_EPT_POPULATE_CONFIG populate = Populate();
    VMX_PERSISTENT_ALLOCATOR allocator = Allocator(fake);
    memset(owner, 0, sizeof(*owner));
    memset(g_tables, 0xA5, sizeof(g_tables));
    memset(g_bound, 0xA5, sizeof(g_bound));
    memset(g_index, 0xA5, sizeof(g_index));
    return VmxEptStorageOwnerInitialize(
        &caps, &identity, &populate, &allocator, owner);
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

static int test_initialize_destroy(void)
{
    FAKE_ALLOCATOR fake = { 0 };
    VMX_EPT_STORAGE_OWNER owner;
    uint64_t* root;
    TEST_ASSERT_INT_EQ(Initialize(&fake, &owner), VMX_EPT_STORAGE_OK,
        "initialize owner");
    TEST_ASSERT_INT_EQ(VmxEptStorageOwnerValidate(&owner),
        VMX_EPT_STORAGE_OK, "owner authentic");
    TEST_ASSERT_UINT_EQ(fake.AllocationCall, 3, "three allocations");
    TEST_ASSERT_UINT_EQ(owner.PageCount, PAGE_COUNT, "page count");
    TEST_ASSERT_TRUE(owner.Tables == g_tables, "tables owned");
    TEST_ASSERT_TRUE(owner.PagesByOrdinal == g_bound, "bound metadata owned");
    TEST_ASSERT_TRUE(owner.PhysicalIndex == g_index, "index metadata owned");
    TEST_ASSERT_UINT_EQ(g_bound[0].PhysicalAddress, 0x9000,
        "root physical");
    TEST_ASSERT_UINT_EQ(g_bound[2].PhysicalAddress, 0xB000,
        "last physical");
    TEST_ASSERT_UINT_EQ(g_index[2].Ordinal, 2, "reciprocal ordinal");
    root = (uint64_t*)(void*)g_tables[0];
    TEST_ASSERT_UINT_EQ(root[0], UINT64_C(0xA007), "populated root link");
    TEST_ASSERT_INT_EQ(VmxEptStorageOwnerDestroy(&owner),
        VMX_EPT_STORAGE_OK, "destroy unused owner");
    TEST_ASSERT_UINT_EQ(fake.FreeCount, 3, "three frees");
    TEST_ASSERT_UINT_EQ(fake.FreeKind[0], 1, "index pool first");
    TEST_ASSERT_TRUE(fake.FreePointer[0] == g_index, "index first");
    TEST_ASSERT_TRUE(fake.FreePointer[1] == g_bound, "bound second");
    TEST_ASSERT_UINT_EQ(fake.FreeKind[2], 2, "tables contiguous last");
    TEST_ASSERT_TRUE(fake.FreePointer[2] == g_tables, "tables last");
    TEST_ASSERT_UINT_EQ(owner.Magic, 0, "owner zeroed");
    TEST_ASSERT_INT_EQ(VmxEptStorageOwnerDestroy(&owner),
        VMX_EPT_STORAGE_OK, "destroy zero idempotent");
    return 0;
}

static int test_allocation_unwind(void)
{
    uint32_t fail;
    for (fail = 1; fail <= 3; ++fail) {
        FAKE_ALLOCATOR fake = { 0 };
        VMX_EPT_STORAGE_OWNER owner;
        fake.FailCall = fail;
        TEST_ASSERT_INT_EQ(Initialize(&fake, &owner),
            VMX_EPT_STORAGE_ALLOCATION_FAILED, "allocation failure");
        TEST_ASSERT_UINT_EQ(owner.Magic, 0, "failed owner preserved");
        TEST_ASSERT_UINT_EQ(fake.FreeCount, fail - 1, "reverse partial frees");
        if (fail == 3) {
            TEST_ASSERT_TRUE(fake.FreePointer[0] == g_bound,
                "bound freed before tables");
            TEST_ASSERT_TRUE(fake.FreePointer[1] == g_tables,
                "tables freed last");
        }
    }
    return 0;
}

static int test_region_failures(void)
{
    FAKE_ALLOCATOR fake = { 0 };
    VMX_EPT_STORAGE_OWNER owner;
    fake.UnalignedTables = 1;
    TEST_ASSERT_INT_EQ(Initialize(&fake, &owner),
        VMX_EPT_STORAGE_REGION_INVALID, "unaligned tables");
    TEST_ASSERT_UINT_EQ(fake.FreeCount, 1, "unaligned table released");
    TEST_ASSERT_UINT_EQ(fake.FreeKind[0], 2, "contiguous release family");

    memset(&fake, 0, sizeof(fake));
    fake.AliasCall = 2;
    TEST_ASSERT_INT_EQ(Initialize(&fake, &owner),
        VMX_EPT_STORAGE_REGION_INVALID, "pool/table alias");
    TEST_ASSERT_UINT_EQ(fake.FreeCount, 2, "cross-family alias releases");
    TEST_ASSERT_UINT_EQ(fake.FreeKind[0], 1, "pool alias release");
    TEST_ASSERT_UINT_EQ(fake.FreeKind[1], 2, "contiguous alias release");

    memset(&fake, 0, sizeof(fake));
    fake.BadPhysicalPage = 2;
    TEST_ASSERT_INT_EQ(Initialize(&fake, &owner),
        VMX_EPT_STORAGE_REGION_INVALID, "noncontiguous physical pages");
    TEST_ASSERT_UINT_EQ(fake.FreeCount, 3, "physical failure full unwind");
    return 0;
}

static int test_domain_blocks_free_until_clean(void)
{
    FAKE_ALLOCATOR fake = { 0 };
    VMX_EPT_STORAGE_OWNER owner;
    VMX_EPT_INVALIDATION_REQUEST request;
    VMX_EPT_INVALIDATION_DESCRIPTOR descriptor;
    VMX_EPT_EXECUTION_REPORT report;
    TEST_ASSERT_INT_EQ(Initialize(&fake, &owner), VMX_EPT_STORAGE_OK,
        "initialize active owner");
    TEST_ASSERT_INT_EQ(VmxEptDomainPrepareRootEnter(&owner.Domain),
        VMX_EPT_DOMAIN_OK, "prepare root");
    TEST_ASSERT_INT_EQ(VmxEptDomainAdoptRoot(&owner.Domain),
        VMX_EPT_DOMAIN_OK, "adopt root");
    TEST_ASSERT_INT_EQ(VmxEptDomainBeginUse(&owner.Domain),
        VMX_EPT_DOMAIN_OK, "begin EPTP use");
    TEST_ASSERT_INT_EQ(VmxEptStorageOwnerDestroy(&owner),
        VMX_EPT_STORAGE_BUSY, "root owner blocks free");
    TEST_ASSERT_UINT_EQ(fake.FreeCount, 0, "busy preserves allocations");
    TEST_ASSERT_INT_EQ(VmxEptDomainBuildInvalidation(
        &owner.Domain, &request, &descriptor), VMX_EPT_DOMAIN_OK,
        "build cleanup invalidation");
    report = SuccessReport(&owner.Domain);
    TEST_ASSERT_INT_EQ(VmxEptDomainCommitInvalidation(&owner.Domain, &report),
        VMX_EPT_DOMAIN_OK, "commit cleanup invalidation");
    TEST_ASSERT_INT_EQ(VmxEptDomainPrepareRootRelease(&owner.Domain),
        VMX_EPT_DOMAIN_OK, "prepare root release");
    TEST_ASSERT_INT_EQ(VmxEptDomainCompleteRootRelease(&owner.Domain),
        VMX_EPT_DOMAIN_OK, "complete root release");
    TEST_ASSERT_INT_EQ(VmxEptStorageOwnerDestroy(&owner),
        VMX_EPT_STORAGE_OK, "clean owner frees");
    TEST_ASSERT_UINT_EQ(fake.FreeCount, 3, "clean frees allocations");
    return 0;
}

static int test_authentication_and_inputs(void)
{
    FAKE_ALLOCATOR fake = { 0 };
    VMX_EPT_STORAGE_OWNER owner;
    VMX_EPT_STORAGE_OWNER copy;
    VMX_EPT_CAPABILITIES caps = Caps();
    VMX_EPT_IDENTITY_CONFIG identity = Identity();
    VMX_EPT_POPULATE_CONFIG populate = Populate();
    VMX_PERSISTENT_ALLOCATOR allocator = Allocator(&fake);
    memset(&owner, 0xA5, sizeof(owner));
    TEST_ASSERT_INT_EQ(VmxEptStorageOwnerInitialize(
        &caps, &identity, &populate, &allocator, &owner),
        VMX_EPT_STORAGE_INVALID, "nonzero owner rejected");
    TEST_ASSERT_INT_EQ(Initialize(&fake, &owner), VMX_EPT_STORAGE_OK,
        "initialize authentication owner");
    copy = owner;
    copy.Magic++;
    TEST_ASSERT_INT_EQ(VmxEptStorageOwnerValidate(&copy),
        VMX_EPT_STORAGE_INVALID, "magic authenticated");
    copy = owner;
    copy.Allocator.FreePool = 0;
    TEST_ASSERT_INT_EQ(VmxEptStorageOwnerValidate(&copy),
        VMX_EPT_STORAGE_INVALID, "allocator authenticated");
    copy = owner;
    copy.PagesByOrdinal[1].PhysicalRank = 0;
    TEST_ASSERT_INT_EQ(VmxEptStorageOwnerValidate(&copy),
        VMX_EPT_STORAGE_INVALID, "rank authenticated");
    owner.PagesByOrdinal[1].PhysicalRank = 1;
    TEST_ASSERT_INT_EQ(VmxEptStorageOwnerDestroy(&owner),
        VMX_EPT_STORAGE_OK, "destroy authentication owner");
    return 0;
}

static int test_frozen_initialization_inputs(void)
{
    FAKE_ALLOCATOR fake = { 0 };
    VMX_EPT_CAPABILITIES caps = Caps();
    VMX_EPT_IDENTITY_CONFIG identity = Identity();
    VMX_EPT_POPULATE_CONFIG populate = Populate();
    VMX_PERSISTENT_ALLOCATOR allocator = Allocator(&fake);
    VMX_EPT_STORAGE_OWNER owner;

    memset(&owner, 0, sizeof(owner));
    memset(g_tables, 0xA5, sizeof(g_tables));
    memset(g_bound, 0xA5, sizeof(g_bound));
    memset(g_index, 0xA5, sizeof(g_index));
    fake.MutableCapabilities = &caps;
    fake.MutableIdentity = &identity;
    fake.MutablePopulate = &populate;
    fake.MutableAllocator = &allocator;
    fake.MutateInputsOnCall = 1;

    TEST_ASSERT_INT_EQ(VmxEptStorageOwnerInitialize(&caps, &identity,
        &populate, &allocator, &owner), VMX_EPT_STORAGE_OK,
        "allocator callback mutation cannot redirect initialization");
    TEST_ASSERT_UINT_EQ(caps.Raw, 0u,
        "callback mutated caller capabilities");
    TEST_ASSERT_UINT_EQ(identity.MappedBytes, 0u,
        "callback mutated caller identity");
    TEST_ASSERT_UINT_EQ(populate.Permissions, 0u,
        "callback mutated caller population policy");
    TEST_ASSERT_TRUE(allocator.AllocatePool == NULL,
        "callback poisoned caller allocator");
    TEST_ASSERT_INT_EQ(VmxEptStorageOwnerValidate(&owner),
        VMX_EPT_STORAGE_OK, "frozen initialization owner authentic");
    TEST_ASSERT_UINT_EQ(owner.Domain.Identity.MappedBytes,
        UINT64_C(0x200000), "domain retained admitted identity");
    TEST_ASSERT_UINT_EQ(owner.Domain.Populate.Permissions,
        VMX_EPT_PERMISSION_MASK, "domain retained admitted population policy");
    TEST_ASSERT_INT_EQ(VmxEptStorageOwnerDestroy(&owner),
        VMX_EPT_STORAGE_OK, "stored allocator snapshot destroys owner");
    TEST_ASSERT_UINT_EQ(fake.FreeCount, 3u,
        "stored allocator snapshot frees all allocations");
    return 0;
}

static int test_overlapping_configuration_inputs_rejected(void)
{
    typedef union OVERLAPPING_CONFIG {
        VMX_EPT_CAPABILITIES Capabilities;
        VMX_EPT_IDENTITY_CONFIG Identity;
    } OVERLAPPING_CONFIG;
    FAKE_ALLOCATOR fake = { 0 };
    OVERLAPPING_CONFIG overlap;
    VMX_EPT_POPULATE_CONFIG populate = Populate();
    VMX_PERSISTENT_ALLOCATOR allocator = Allocator(&fake);
    VMX_EPT_STORAGE_OWNER owner;

    memset(&overlap, 0, sizeof(overlap));
    overlap.Capabilities = Caps();
    memset(&owner, 0, sizeof(owner));
    TEST_ASSERT_INT_EQ(VmxEptStorageOwnerInitialize(
        &overlap.Capabilities, &overlap.Identity, &populate, &allocator,
        &owner), VMX_EPT_STORAGE_INVALID,
        "overlapping capability and identity rejected");
    TEST_ASSERT_UINT_EQ(fake.AllocationCall, 0u,
        "overlap rejection reaches no allocator callback");
    TEST_ASSERT_UINT_EQ(owner.Magic, 0u,
        "overlap rejection preserves owner");
    return 0;
}

int main(void)
{
    int result = 0;
    result |= test_initialize_destroy();
    result |= test_allocation_unwind();
    result |= test_region_failures();
    result |= test_domain_blocks_free_until_clean();
    result |= test_authentication_and_inputs();
    result |= test_frozen_initialization_inputs();
    result |= test_overlapping_configuration_inputs_rejected();
    if (result == 0)
        printf("All EPT storage-owner tests passed.\n");
    return result;
}
