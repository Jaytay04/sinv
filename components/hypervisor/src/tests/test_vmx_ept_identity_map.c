#include "test_common.h"
#include "vmx_ept_identity_map.h"

#include <stddef.h>

#define RAW_REQUIRED ((UINT64_C(1) << 6) | (UINT64_C(1) << 14) | \
    (UINT64_C(1) << 16) | (UINT64_C(1) << 20) | \
    (UINT64_C(1) << 25) | (UINT64_C(1) << 26))
#define RAW_OPTIONAL ((UINT64_C(1) << 0) | (UINT64_C(1) << 8) | \
    (UINT64_C(1) << 17) | (UINT64_C(1) << 21))
#define MAX_PAGES 5

static _Alignas(4096) unsigned char g_storage[MAX_PAGES][4096];
static VMX_EPT_BOUND_PAGE g_pages[MAX_PAGES];
static VMX_EPT_PHYSICAL_PAGE g_index[MAX_PAGES];

_Static_assert(sizeof(VMX_EPT_BOUND_PAGE) == 16, "bound page ABI");
_Static_assert(sizeof(VMX_EPT_PHYSICAL_PAGE) == 16, "physical index ABI");
_Static_assert(sizeof(VMX_EPT_POPULATE_CONFIG) == 16, "populate config ABI");
_Static_assert(offsetof(VMX_EPT_BOUND_PAGE, PhysicalRank) == 8,
    "physical rank offset");

static VMX_EPT_CAPABILITIES Caps(uint64_t raw)
{
    VMX_EPT_CAPABILITIES caps = { 0 };
    TEST_ASSERT_INT_EQ(VmxEptDecodeCapabilities(raw, &caps),
        VMX_EPT_STATUS_OK, "decode capabilities");
    return caps;
}

static VMX_EPT_IDENTITY_CONFIG Identity(uint8_t kind, uint64_t bytes)
{
    VMX_EPT_IDENTITY_CONFIG identity = { 0 };
    identity.MappedBytes = bytes;
    identity.PhysicalAddressBits = 48;
    identity.LeafKind = kind;
    return identity;
}

static VMX_EPT_POPULATE_CONFIG Populate(void)
{
    VMX_EPT_POPULATE_CONFIG populate = { 0 };
    populate.Permissions = VMX_EPT_PERMISSION_MASK;
    populate.MemoryType = VMX_EPT_MEMORY_TYPE_WB;
    return populate;
}

static void BuildBinding(
    const VMX_EPT_IDENTITY_PLAN* plan,
    VMX_EPT_STORAGE_BINDING* binding)
{
    static const uint64_t addresses[MAX_PAGES] = {
        UINT64_C(0x9000), UINT64_C(0x3000), UINT64_C(0x7000),
        UINT64_C(0x5000), UINT64_C(0xB000)
    };
    uint32_t i;
    uint32_t j;
    TEST_ASSERT_TRUE(plan->TotalTablePages <= MAX_PAGES, "fixture page cap");
    memset(g_pages, 0, sizeof(g_pages));
    memset(g_index, 0, sizeof(g_index));
    for (i = 0; i < plan->TotalTablePages; ++i) {
        g_pages[i].PhysicalAddress = addresses[i];
        g_index[i].PhysicalAddress = addresses[i];
        g_index[i].Ordinal = i;
    }
    for (i = 0; i < plan->TotalTablePages; ++i) {
        for (j = i + 1; j < plan->TotalTablePages; ++j) {
            if (g_index[j].PhysicalAddress < g_index[i].PhysicalAddress) {
                VMX_EPT_PHYSICAL_PAGE swap = g_index[i];
                g_index[i] = g_index[j];
                g_index[j] = swap;
            }
        }
    }
    for (i = 0; i < plan->TotalTablePages; ++i)
        g_pages[g_index[i].Ordinal].PhysicalRank = i;
    memset(binding, 0, sizeof(*binding));
    binding->VirtualBase = g_storage;
    binding->VirtualBytes = plan->TotalTableBytes;
    binding->PagesByOrdinal = g_pages;
    binding->PhysicalIndex = g_index;
    binding->PageCount = plan->TotalTablePages;
}

static uint64_t* Page(uint32_t ordinal)
{
    return (uint64_t*)(void*)g_storage[ordinal];
}

static int test_populate_2mb(void)
{
    VMX_EPT_CAPABILITIES caps = Caps(RAW_REQUIRED | RAW_OPTIONAL);
    VMX_EPT_IDENTITY_CONFIG identity = Identity(
        VMX_EPT_ENTRY_LEAF_2MB, UINT64_C(513) * UINT64_C(0x200000));
    VMX_EPT_IDENTITY_PLAN plan;
    VMX_EPT_POPULATE_CONFIG populate = Populate();
    VMX_EPT_STORAGE_BINDING binding;
    TEST_ASSERT_INT_EQ(VmxEptBuildIdentityPlan(&caps, &identity, &plan),
        VMX_EPT_STATUS_OK, "2MB plan");
    BuildBinding(&plan, &binding);
    memset(g_storage, 0xA5, sizeof(g_storage));
    TEST_ASSERT_INT_EQ(VmxEptPopulateIdentityMap(
        &caps, &identity, &plan, &populate, &binding),
        VMX_EPT_STATUS_OK, "populate 2MB");
    TEST_ASSERT_UINT_EQ(Page(0)[0], g_pages[1].PhysicalAddress | 7,
        "PML4 link");
    TEST_ASSERT_UINT_EQ(Page(1)[0], g_pages[2].PhysicalAddress | 7,
        "PDPT first link");
    TEST_ASSERT_UINT_EQ(Page(1)[1], g_pages[3].PhysicalAddress | 7,
        "PDPT second link");
    TEST_ASSERT_UINT_EQ(Page(2)[0], UINT64_C(0xB7), "first 2MB leaf");
    TEST_ASSERT_UINT_EQ(Page(2)[1], UINT64_C(0x2000B7), "second 2MB leaf");
    TEST_ASSERT_UINT_EQ(Page(2)[511], UINT64_C(0x3FE000B7),
        "full PD last leaf");
    TEST_ASSERT_UINT_EQ(Page(3)[0], UINT64_C(0x400000B7),
        "partial PD leaf");
    TEST_ASSERT_UINT_EQ(Page(3)[1], 0, "partial PD remainder zero");
    TEST_ASSERT_UINT_EQ(Page(0)[511], 0, "root remainder zero");
    return 0;
}

static int test_populate_4kb_and_1gb(void)
{
    VMX_EPT_CAPABILITIES caps = Caps(RAW_REQUIRED | RAW_OPTIONAL);
    VMX_EPT_IDENTITY_CONFIG identity = Identity(
        VMX_EPT_ENTRY_LEAF_4KB, UINT64_C(513) * UINT64_C(0x1000));
    VMX_EPT_IDENTITY_PLAN plan;
    VMX_EPT_POPULATE_CONFIG populate = Populate();
    VMX_EPT_STORAGE_BINDING binding;
    TEST_ASSERT_INT_EQ(VmxEptBuildIdentityPlan(&caps, &identity, &plan),
        VMX_EPT_STATUS_OK, "4KB plan");
    BuildBinding(&plan, &binding);
    TEST_ASSERT_INT_EQ(VmxEptPopulateIdentityMap(
        &caps, &identity, &plan, &populate, &binding),
        VMX_EPT_STATUS_OK, "populate 4KB");
    TEST_ASSERT_UINT_EQ(Page(2)[0], g_pages[3].PhysicalAddress | 7,
        "PD to first PT");
    TEST_ASSERT_UINT_EQ(Page(2)[1], g_pages[4].PhysicalAddress | 7,
        "PD to second PT");
    TEST_ASSERT_UINT_EQ(Page(3)[0], UINT64_C(0x37), "first 4KB leaf");
    TEST_ASSERT_UINT_EQ(Page(3)[511], UINT64_C(0x1FF037),
        "full PT last leaf");
    TEST_ASSERT_UINT_EQ(Page(4)[0], UINT64_C(0x200037),
        "partial PT leaf");

    identity = Identity(VMX_EPT_ENTRY_LEAF_1GB, UINT64_C(0x40000000));
    TEST_ASSERT_INT_EQ(VmxEptBuildIdentityPlan(&caps, &identity, &plan),
        VMX_EPT_STATUS_OK, "1GB plan");
    BuildBinding(&plan, &binding);
    populate.EnableAccessedDirty = 1;
    populate.IgnorePat = 1;
    TEST_ASSERT_INT_EQ(VmxEptPopulateIdentityMap(
        &caps, &identity, &plan, &populate, &binding),
        VMX_EPT_STATUS_OK, "populate 1GB");
    TEST_ASSERT_UINT_EQ(Page(1)[0], UINT64_C(0xF7), "1GB leaf flags");
    return 0;
}

static int StorageIs(unsigned char value, uint64_t bytes)
{
    uint64_t i;
    const unsigned char* storage = (const unsigned char*)g_storage;
    for (i = 0; i < bytes; ++i)
        if (storage[i] != value)
            return 0;
    return 1;
}

static void ExpectPreflightFailure(
    const VMX_EPT_CAPABILITIES* caps,
    const VMX_EPT_IDENTITY_CONFIG* identity,
    const VMX_EPT_IDENTITY_PLAN* plan,
    const VMX_EPT_POPULATE_CONFIG* populate,
    const VMX_EPT_STORAGE_BINDING* binding,
    VMX_EPT_STATUS expected)
{
    memset(g_storage, 0xA5, sizeof(g_storage));
    TEST_ASSERT_INT_EQ(VmxEptPopulateIdentityMap(
        caps, identity, plan, populate, binding), expected,
        "preflight failure");
    TEST_ASSERT_TRUE(StorageIs(0xA5, plan->TotalTableBytes),
        "preflight preserves storage");
}

static int test_storage_preflight(void)
{
    VMX_EPT_CAPABILITIES caps = Caps(RAW_REQUIRED | RAW_OPTIONAL);
    VMX_EPT_CAPABILITIES required = Caps(RAW_REQUIRED);
    VMX_EPT_IDENTITY_CONFIG identity = Identity(
        VMX_EPT_ENTRY_LEAF_2MB, UINT64_C(513) * UINT64_C(0x200000));
    VMX_EPT_IDENTITY_PLAN plan;
    VMX_EPT_POPULATE_CONFIG populate = Populate();
    VMX_EPT_STORAGE_BINDING binding;
    TEST_ASSERT_INT_EQ(VmxEptBuildIdentityPlan(&caps, &identity, &plan),
        VMX_EPT_STATUS_OK, "preflight plan");
    BuildBinding(&plan, &binding);
    TEST_ASSERT_INT_EQ(VmxEptValidateStorageBinding(
        &caps, &identity, &plan, &populate, &binding),
        VMX_EPT_STATUS_OK, "valid binding");

    binding.PageCount--;
    ExpectPreflightFailure(&caps, &identity, &plan, &populate, &binding,
        VMX_EPT_STATUS_INVALID);
    BuildBinding(&plan, &binding);
    binding.VirtualBytes -= 4096;
    ExpectPreflightFailure(&caps, &identity, &plan, &populate, &binding,
        VMX_EPT_STATUS_INVALID);
    BuildBinding(&plan, &binding);
    binding.VirtualBase = &g_storage[0][1];
    ExpectPreflightFailure(&caps, &identity, &plan, &populate, &binding,
        VMX_EPT_STATUS_INVALID);
    BuildBinding(&plan, &binding);
    binding.PagesByOrdinal = (const VMX_EPT_BOUND_PAGE*)binding.VirtualBase;
    ExpectPreflightFailure(&caps, &identity, &plan, &populate, &binding,
        VMX_EPT_STATUS_INVALID);

    BuildBinding(&plan, &binding);
    g_pages[0].PhysicalRank = g_pages[1].PhysicalRank;
    ExpectPreflightFailure(&caps, &identity, &plan, &populate, &binding,
        VMX_EPT_STATUS_INVALID);
    BuildBinding(&plan, &binding);
    g_pages[0].PhysicalAddress |= 1;
    ExpectPreflightFailure(&caps, &identity, &plan, &populate, &binding,
        VMX_EPT_STATUS_INVALID);
    BuildBinding(&plan, &binding);
    g_index[1].PhysicalAddress = g_index[0].PhysicalAddress;
    ExpectPreflightFailure(&caps, &identity, &plan, &populate, &binding,
        VMX_EPT_STATUS_INVALID);
    BuildBinding(&plan, &binding);
    g_index[0].Reserved = 1;
    ExpectPreflightFailure(&caps, &identity, &plan, &populate, &binding,
        VMX_EPT_STATUS_INVALID);

    BuildBinding(&plan, &binding);
    populate.Permissions = VMX_EPT_PERMISSION_WRITE;
    ExpectPreflightFailure(&caps, &identity, &plan, &populate, &binding,
        VMX_EPT_STATUS_INVALID);
    populate = Populate();
    populate.MemoryType = 2;
    ExpectPreflightFailure(&caps, &identity, &plan, &populate, &binding,
        VMX_EPT_STATUS_INVALID);
    populate = Populate();
    populate.EnableAccessedDirty = 1;
    ExpectPreflightFailure(&required, &identity, &plan, &populate, &binding,
        VMX_EPT_STATUS_UNSUPPORTED);
    populate = Populate();
    plan.TotalTablePages++;
    ExpectPreflightFailure(&caps, &identity, &plan, &populate, &binding,
        VMX_EPT_STATUS_INVALID);

    TEST_ASSERT_INT_EQ(VmxEptValidateStorageBinding(
        &caps, &identity, &plan, &populate, NULL),
        VMX_EPT_STATUS_INVALID, "null binding");
    TEST_ASSERT_INT_EQ(VmxEptPopulateIdentityMap(
        &caps, &identity, &plan, &populate, NULL),
        VMX_EPT_STATUS_INVALID, "null populate binding");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_populate_2mb);
    TEST_RUN(test_populate_4kb_and_1gb);
    TEST_RUN(test_storage_preflight);
TEST_MAIN_END()
