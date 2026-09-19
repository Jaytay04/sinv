#include "test_common.h"
#include "vmx_ept_session_config.h"

#include <string.h>

#define RAW_REQUIRED ((UINT64_C(1) << 6) | (UINT64_C(1) << 14) | \
    (UINT64_C(1) << 16) | (UINT64_C(1) << 20) | \
    (UINT64_C(1) << 25) | (UINT64_C(1) << 26))
#define RAW_1GB (UINT64_C(1) << 17)

static VMX_EPT_SESSION_CONFIG_INPUT Input(
    uint64_t raw, uint64_t highest, uint32_t bits)
{
    VMX_EPT_SESSION_CONFIG_INPUT input = { 0 };
    input.EptVpidCapabilities = raw;
    input.HighestPresentPhysicalAddress = highest;
    input.PhysicalAddressBits = bits;
    return input;
}

static void AssertPreserved(
    VMX_EPT_SESSION_CONFIG_INPUT input,
    VMX_EPT_SESSION_CONFIG_STATUS expected,
    const char* message)
{
    VMX_EPT_SESSION_CONFIG config;
    VMX_EPT_SESSION_CONFIG before;
    memset(&config, 0xA5, sizeof(config));
    before = config;
    TEST_ASSERT_INT_EQ(VmxEptSessionConfigBuild(&input, &config),
        expected, message);
    TEST_ASSERT(memcmp(&config, &before, sizeof(config)) == 0,
        "failure preserves full config");
}

static int test_two_megabyte_policy(void)
{
    VMX_EPT_SESSION_CONFIG_INPUT input = Input(
        RAW_REQUIRED | (UINT64_C(1) << 63),
        UINT64_C(0x100000FFF), 48);
    VMX_EPT_SESSION_CONFIG config;
    TEST_ASSERT_INT_EQ(VmxEptSessionConfigBuild(&input, &config),
        VMX_EPT_SESSION_CONFIG_OK, "build 2MB policy");
    TEST_ASSERT_UINT_EQ(config.Capabilities.Raw,
        input.EptVpidCapabilities, "raw capabilities retained");
    TEST_ASSERT_UINT_EQ(config.Identity.MappedBytes,
        UINT64_C(0x100200000), "extent rounded to 2MB");
    TEST_ASSERT_UINT_EQ(config.Identity.PhysicalAddressBits, 48,
        "physical width retained");
    TEST_ASSERT_UINT_EQ(config.Identity.LeafKind,
        VMX_EPT_ENTRY_LEAF_2MB, "2MB leaf selected");
    TEST_ASSERT_UINT_EQ(config.Populate.Permissions,
        VMX_EPT_PERMISSION_MASK, "RWX permissions");
    TEST_ASSERT_UINT_EQ(config.Populate.MemoryType,
        VMX_EPT_MEMORY_TYPE_UC, "conservative UC leaves");
    TEST_ASSERT_UINT_EQ(config.Populate.EnableAccessedDirty, 0,
        "A/D disabled");
    TEST_ASSERT_UINT_EQ(config.Populate.IgnorePat, 0, "PAT honored");
    TEST_ASSERT_INT_EQ(VmxEptValidateIdentityPlan(&config.Capabilities,
        &config.Identity, &config.Plan), VMX_EPT_STATUS_OK,
        "embedded plan authentic");
    TEST_ASSERT_INT_EQ(VmxEptSessionConfigValidate(&input, &config),
        VMX_EPT_SESSION_CONFIG_OK, "exact config validates");
    return 0;
}

static int test_hookable_two_megabyte_policy(void)
{
    VMX_EPT_SESSION_CONFIG_INPUT input = Input(
        RAW_REQUIRED | RAW_1GB, UINT64_C(0xFFFFFFFF), 48);
    VMX_EPT_SESSION_CONFIG config;
    TEST_ASSERT_INT_EQ(VmxEptSessionConfigBuild(&input, &config),
        VMX_EPT_SESSION_CONFIG_OK, "build exact hookable policy");
    TEST_ASSERT_UINT_EQ(config.Identity.LeafKind,
        VMX_EPT_ENTRY_LEAF_2MB, "2MB retained for hook splits");
    TEST_ASSERT_UINT_EQ(config.Identity.MappedBytes,
        UINT64_C(0x100000000), "exact 4GB extent");
    TEST_ASSERT_UINT_EQ(config.Plan.LeafEntries, 2048, "2MB hookable leaves");

    input.HighestPresentPhysicalAddress = UINT64_C(0x100000FFF);
    TEST_ASSERT_INT_EQ(VmxEptSessionConfigBuild(&input, &config),
        VMX_EPT_SESSION_CONFIG_OK, "unaligned hookable policy");
    TEST_ASSERT_UINT_EQ(config.Identity.LeafKind,
        VMX_EPT_ENTRY_LEAF_2MB, "2MB policy remains hookable");
    TEST_ASSERT_UINT_EQ(config.Identity.MappedBytes,
        UINT64_C(0x100200000), "fallback extent");
    return 0;
}

static int test_width_boundaries(void)
{
    VMX_EPT_SESSION_CONFIG config;
    VMX_EPT_SESSION_CONFIG_INPUT input = Input(
        RAW_REQUIRED | RAW_1GB, (UINT64_C(1) << 36) - 1, 36);
    TEST_ASSERT_INT_EQ(VmxEptSessionConfigBuild(&input, &config),
        VMX_EPT_SESSION_CONFIG_OK, "36-bit exact boundary");
    TEST_ASSERT_UINT_EQ(config.Identity.MappedBytes,
        UINT64_C(1) << 36, "36-bit extent");

    input.HighestPresentPhysicalAddress = UINT64_C(1) << 36;
    AssertPreserved(input, VMX_EPT_SESSION_CONFIG_INVALID,
        "36-bit address overflow");
    input = Input(RAW_REQUIRED | RAW_1GB,
        (UINT64_C(1) << 48) - 1, 52);
    TEST_ASSERT_INT_EQ(VmxEptSessionConfigBuild(&input, &config),
        VMX_EPT_SESSION_CONFIG_OK, "four-level exact boundary");
    TEST_ASSERT_UINT_EQ(config.Identity.MappedBytes,
        UINT64_C(1) << 48, "four-level extent");
    input.HighestPresentPhysicalAddress = UINT64_C(1) << 48;
    AssertPreserved(input, VMX_EPT_SESSION_CONFIG_INVALID,
        "four-level address overflow");
    return 0;
}

static int test_rejections_and_preservation(void)
{
    VMX_EPT_SESSION_CONFIG_INPUT input = Input(
        RAW_REQUIRED, UINT64_C(0x3FFFFFFF), 48);
    VMX_EPT_SESSION_CONFIG config;
    union {
        VMX_EPT_SESSION_CONFIG_INPUT Input;
        VMX_EPT_SESSION_CONFIG Config;
    } alias;
    TEST_ASSERT_INT_EQ(VmxEptSessionConfigBuild(NULL, &config),
        VMX_EPT_SESSION_CONFIG_INVALID, "null input");
    TEST_ASSERT_INT_EQ(VmxEptSessionConfigBuild(&input, NULL),
        VMX_EPT_SESSION_CONFIG_INVALID, "null output");
    alias.Input = input;
    TEST_ASSERT_INT_EQ(VmxEptSessionConfigBuild(
        &alias.Input, &alias.Config), VMX_EPT_SESSION_CONFIG_INVALID,
        "input/output alias");
    input.Reserved = 1;
    AssertPreserved(input, VMX_EPT_SESSION_CONFIG_INVALID, "reserved input");
    input = Input(RAW_REQUIRED, UINT64_C(0x3FFFFFFF), 35);
    AssertPreserved(input, VMX_EPT_SESSION_CONFIG_INVALID, "width below 36");
    input.PhysicalAddressBits = 53;
    AssertPreserved(input, VMX_EPT_SESSION_CONFIG_INVALID, "width above 52");
    input = Input(RAW_REQUIRED & ~(UINT64_C(1) << 20),
        UINT64_C(0x3FFFFFFF), 48);
    AssertPreserved(input, VMX_EPT_SESSION_CONFIG_UNSUPPORTED,
        "required capability missing");
    input = Input(RAW_REQUIRED, UINT64_MAX, 48);
    AssertPreserved(input, VMX_EPT_SESSION_CONFIG_OVERFLOW,
        "inclusive address addition overflow");
    return 0;
}

static int test_validator_mutations(void)
{
    VMX_EPT_SESSION_CONFIG_INPUT input = Input(
        RAW_REQUIRED | RAW_1GB, UINT64_C(0xFFFFFFFF), 48);
    VMX_EPT_SESSION_CONFIG config;
    VMX_EPT_SESSION_CONFIG exact;
#define EXPECT_MUTATION(statement) do { \
    config = exact; statement; \
    TEST_ASSERT_INT_EQ(VmxEptSessionConfigValidate(&input, &config), \
        VMX_EPT_SESSION_CONFIG_INVALID, #statement); \
} while (0)
    TEST_ASSERT_INT_EQ(VmxEptSessionConfigBuild(&input, &exact),
        VMX_EPT_SESSION_CONFIG_OK, "validator fixture");
    EXPECT_MUTATION(config.Capabilities.Raw ^= UINT64_C(1));
    EXPECT_MUTATION(config.Capabilities.Flags ^= VMX_EPT_CAP_PAGE_1GB);
    EXPECT_MUTATION(config.Capabilities.Reserved = 1);
    EXPECT_MUTATION(config.Identity.MappedBytes += VMX_EPT_POLICY_LEAF_1GB);
    EXPECT_MUTATION(config.Identity.PhysicalAddressBits = 47);
    EXPECT_MUTATION(config.Identity.LeafKind = VMX_EPT_ENTRY_LEAF_1GB);
    EXPECT_MUTATION(config.Identity.Reserved8[1] = 1);
    EXPECT_MUTATION(config.Identity.Reserved = 1);
    EXPECT_MUTATION(config.Populate.Permissions = VMX_EPT_PERMISSION_READ);
    EXPECT_MUTATION(config.Populate.MemoryType = VMX_EPT_MEMORY_TYPE_WB);
    EXPECT_MUTATION(config.Populate.EnableAccessedDirty = 1);
    EXPECT_MUTATION(config.Populate.IgnorePat = 1);
    EXPECT_MUTATION(config.Populate.Reserved32 = 1);
    EXPECT_MUTATION(config.Populate.Reserved = 1);
    EXPECT_MUTATION(config.Plan.TotalTablePages++);
    EXPECT_MUTATION(config.Plan.Reserved = 1);
#undef EXPECT_MUTATION
    TEST_ASSERT_INT_EQ(VmxEptSessionConfigValidate(NULL, &exact),
        VMX_EPT_SESSION_CONFIG_INVALID, "validator null input");
    TEST_ASSERT_INT_EQ(VmxEptSessionConfigValidate(&input, NULL),
        VMX_EPT_SESSION_CONFIG_INVALID, "validator null config");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_two_megabyte_policy);
    TEST_RUN(test_hookable_two_megabyte_policy);
    TEST_RUN(test_width_boundaries);
    TEST_RUN(test_rejections_and_preservation);
    TEST_RUN(test_validator_mutations);
TEST_MAIN_END()