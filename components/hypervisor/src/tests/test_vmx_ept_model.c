#include "test_common.h"
#include "ept.h"
#include "vmx_ept_model.h"

#include <stddef.h>

#define RAW_REQUIRED ((UINT64_C(1) << 6) | (UINT64_C(1) << 14) | \
    (UINT64_C(1) << 16) | (UINT64_C(1) << 20) | \
    (UINT64_C(1) << 25) | (UINT64_C(1) << 26))
#define RAW_OPTIONAL ((UINT64_C(1) << 0) | (UINT64_C(1) << 8) | \
    (UINT64_C(1) << 17) | (UINT64_C(1) << 21))

_Static_assert(sizeof(VMX_EPT_CAPABILITIES) == 16, "capability ABI");
_Static_assert(offsetof(VMX_EPT_CAPABILITIES, Flags) == 8, "flags offset");
_Static_assert(sizeof(VMX_EPT_POINTER_CONFIG) == 16, "EPTP config ABI");
_Static_assert(offsetof(VMX_EPT_POINTER_CONFIG, PhysicalAddressBits) == 8,
    "physical width offset");
_Static_assert(VMX_EPT_MEMORY_TYPE_UC == EPT_MT_UNCACHEABLE, "UC encoding");
_Static_assert(VMX_EPT_MEMORY_TYPE_WB == EPT_MT_WRITE_BACK, "WB encoding");
_Static_assert(sizeof(VMX_EPT_ENTRY_CONFIG) == 24, "entry config ABI");
_Static_assert(offsetof(VMX_EPT_ENTRY_CONFIG, Kind) == 12, "kind offset");
_Static_assert(offsetof(VMX_EPT_ENTRY_CONFIG, Reserved) == 16, "reserved offset");
_Static_assert(VMX_EPT_PERMISSION_READ == EPT_READ, "read encoding");
_Static_assert(VMX_EPT_PERMISSION_WRITE == EPT_WRITE, "write encoding");
_Static_assert(VMX_EPT_PERMISSION_EXECUTE == EPT_EXECUTE, "execute encoding");
_Static_assert(sizeof(VMX_EPT_IDENTITY_CONFIG) == 24, "identity config ABI");
_Static_assert(offsetof(VMX_EPT_IDENTITY_CONFIG, LeafKind) == 12,
    "identity kind offset");
_Static_assert(sizeof(VMX_EPT_IDENTITY_PLAN) == 72, "identity plan ABI");
_Static_assert(offsetof(VMX_EPT_IDENTITY_PLAN, Pml4Entries) == 40,
    "identity count offset");
_Static_assert(sizeof(VMX_EPT_OWNED_PAGE) == 24, "owned page ABI");
_Static_assert(VMX_EPT_TABLE_ENTRIES == EPT_PML4_ENTRIES,
    "table entry count");
_Static_assert(VMX_EPT_TABLE_PAGE_SIZE == EPT_PML4_SIZE,
    "table page size");

static VMX_EPT_CAPABILITIES Caps(uint64_t raw)
{
    VMX_EPT_CAPABILITIES caps = { 0 };
    TEST_ASSERT_INT_EQ(VmxEptDecodeCapabilities(raw, &caps),
        VMX_EPT_STATUS_OK, "decode");
    return caps;
}

static VMX_EPT_POINTER_CONFIG Config(uint64_t pa)
{
    VMX_EPT_POINTER_CONFIG config = { 0 };
    config.Pml4PhysicalAddress = pa;
    config.PhysicalAddressBits = 48;
    config.MemoryType = VMX_EPT_MEMORY_TYPE_WB;
    return config;
}

static int test_decode_and_validate(void)
{
    VMX_EPT_CAPABILITIES caps = Caps(RAW_REQUIRED | RAW_OPTIONAL |
        (UINT64_C(1) << 63));
    TEST_ASSERT_UINT_EQ(caps.Raw, RAW_REQUIRED | RAW_OPTIONAL |
        (UINT64_C(1) << 63), "raw preserved");
    TEST_ASSERT_UINT_EQ(caps.Flags, VMX_EPT_CAP_PROJECT_REQUIRED |
        VMX_EPT_CAP_EXECUTE_ONLY | VMX_EPT_CAP_EPTP_UC |
        VMX_EPT_CAP_PAGE_1GB | VMX_EPT_CAP_ACCESSED_DIRTY,
        "known capability flags");
    TEST_ASSERT_INT_EQ(VmxEptValidateCapabilities(&caps),
        VMX_EPT_STATUS_OK, "baseline supported");
    TEST_ASSERT_INT_EQ(VmxEptDecodeCapabilities(0, NULL),
        VMX_EPT_STATUS_INVALID, "null decode output");
    return 0;
}

static int test_capability_rejections(void)
{
    const uint64_t required_bits[] = {
        UINT64_C(1) << 6, UINT64_C(1) << 14, UINT64_C(1) << 16,
        UINT64_C(1) << 20, UINT64_C(1) << 25, UINT64_C(1) << 26
    };
    size_t i;
    VMX_EPT_CAPABILITIES caps;
    TEST_ASSERT_INT_EQ(VmxEptValidateCapabilities(NULL),
        VMX_EPT_STATUS_INVALID, "null capabilities");
    for (i = 0; i < sizeof(required_bits) / sizeof(required_bits[0]); ++i) {
        caps = Caps(RAW_REQUIRED & ~required_bits[i]);
        TEST_ASSERT_INT_EQ(VmxEptValidateCapabilities(&caps),
            VMX_EPT_STATUS_UNSUPPORTED, "required capability missing");
    }
    caps = Caps(RAW_REQUIRED);
    caps.Flags ^= VMX_EPT_CAP_EPTP_UC;
    TEST_ASSERT_INT_EQ(VmxEptValidateCapabilities(&caps),
        VMX_EPT_STATUS_INVALID, "forged flags");
    caps = Caps(RAW_REQUIRED);
    caps.Reserved = 1;
    TEST_ASSERT_INT_EQ(VmxEptValidateCapabilities(&caps),
        VMX_EPT_STATUS_INVALID, "reserved capability field");
    return 0;
}

static int test_build_pointer(void)
{
    VMX_EPT_CAPABILITIES caps = Caps(RAW_REQUIRED | RAW_OPTIONAL);
    VMX_EPT_POINTER_CONFIG config = Config(UINT64_C(0x0000123456789000));
    uint64_t output = UINT64_MAX;
    uint64_t expected = EPTP_MAKE(config.Pml4PhysicalAddress,
        EPT_MT_WRITE_BACK);
    TEST_ASSERT_INT_EQ(VmxEptBuildPointer(&caps, &config, &output),
        VMX_EPT_STATUS_OK, "WB EPTP");
    TEST_ASSERT_UINT_EQ(output, expected, "legacy WB encoding pin");
    config.MemoryType = VMX_EPT_MEMORY_TYPE_UC;
    config.EnableAccessedDirty = 1;
    expected = EPTP_MAKE(config.Pml4PhysicalAddress, EPT_MT_UNCACHEABLE) |
        EPTP_ENABLE_ACCESSED_DIRTY;
    TEST_ASSERT_INT_EQ(VmxEptBuildPointer(&caps, &config, &output),
        VMX_EPT_STATUS_OK, "UC and accessed/dirty EPTP");
    TEST_ASSERT_UINT_EQ(output, expected, "legacy UC/AD encoding pin");
    return 0;
}

static void ExpectBuildFailure(VMX_EPT_CAPABILITIES caps,
    VMX_EPT_POINTER_CONFIG config, VMX_EPT_STATUS expected)
{
    uint64_t output = UINT64_C(0xA5A5A5A5A5A5A5A5);
    TEST_ASSERT_INT_EQ(VmxEptBuildPointer(&caps, &config, &output), expected,
        "build rejection");
    TEST_ASSERT_UINT_EQ(output, UINT64_C(0xA5A5A5A5A5A5A5A5),
        "failure preserves output");
}

static int test_build_rejections(void)
{
    VMX_EPT_CAPABILITIES caps = Caps(RAW_REQUIRED);
    VMX_EPT_POINTER_CONFIG config = Config(UINT64_C(0x1000));
    uint64_t output = 7;
    TEST_ASSERT_INT_EQ(VmxEptBuildPointer(NULL, &config, &output),
        VMX_EPT_STATUS_INVALID, "null capabilities");
    TEST_ASSERT_INT_EQ(VmxEptBuildPointer(&caps, NULL, &output),
        VMX_EPT_STATUS_INVALID, "null config");
    TEST_ASSERT_INT_EQ(VmxEptBuildPointer(&caps, &config, NULL),
        VMX_EPT_STATUS_INVALID, "null output");
    config.Pml4PhysicalAddress = 0;
    ExpectBuildFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config = Config(UINT64_C(0x1234));
    ExpectBuildFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config = Config(UINT64_C(0x1000)); config.PhysicalAddressBits = 35;
    ExpectBuildFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config.PhysicalAddressBits = 53;
    ExpectBuildFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config = Config(UINT64_C(1) << 36); config.PhysicalAddressBits = 36;
    ExpectBuildFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config = Config(UINT64_C(0x1000)); config.MemoryType = 5;
    ExpectBuildFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config = Config(UINT64_C(0x1000)); config.Reserved = 1;
    ExpectBuildFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config = Config(UINT64_C(0x1000)); config.EnableAccessedDirty = 2;
    ExpectBuildFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config = Config(UINT64_C(0x1000)); config.MemoryType = VMX_EPT_MEMORY_TYPE_UC;
    ExpectBuildFailure(caps, config, VMX_EPT_STATUS_UNSUPPORTED);
    config = Config(UINT64_C(0x1000)); config.EnableAccessedDirty = 1;
    ExpectBuildFailure(caps, config, VMX_EPT_STATUS_UNSUPPORTED);
    config = Config(UINT64_C(0x000FFFFFFFFFF000));
    config.PhysicalAddressBits = 52;
    TEST_ASSERT_INT_EQ(VmxEptBuildPointer(&caps, &config, &output),
        VMX_EPT_STATUS_OK, "52-bit boundary accepted");
    return 0;
}


static VMX_EPT_ENTRY_CONFIG EntryConfig(uint8_t kind, uint64_t pa)
{
    VMX_EPT_ENTRY_CONFIG config = { 0 };
    config.TargetPhysicalAddress = pa;
    config.PhysicalAddressBits = 48;
    config.Kind = kind;
    config.Permissions = VMX_EPT_PERMISSION_MASK;
    if (kind != VMX_EPT_ENTRY_NON_LEAF)
        config.MemoryType = VMX_EPT_MEMORY_TYPE_WB;
    return config;
}

static int test_build_entries(void)
{
    VMX_EPT_CAPABILITIES caps = Caps(RAW_REQUIRED | RAW_OPTIONAL);
    VMX_EPT_ENTRY_CONFIG config;
    uint64_t entry;
    const uint8_t memory_types[] = {
        VMX_EPT_MEMORY_TYPE_UC, VMX_EPT_MEMORY_TYPE_WC,
        VMX_EPT_MEMORY_TYPE_WT, VMX_EPT_MEMORY_TYPE_WP,
        VMX_EPT_MEMORY_TYPE_WB
    };
    size_t i;

    config = EntryConfig(VMX_EPT_ENTRY_NON_LEAF, UINT64_C(0x4000));
    TEST_ASSERT_INT_EQ(VmxEptBuildEntry(&caps, &config, &entry),
        VMX_EPT_STATUS_OK, "non-leaf");
    TEST_ASSERT_UINT_EQ(entry, UINT64_C(0x4000) | EPT_READ_WRITE_EXECUTE,
        "non-leaf encoding");

    config = EntryConfig(VMX_EPT_ENTRY_LEAF_4KB, 0);
    TEST_ASSERT_INT_EQ(VmxEptBuildEntry(&caps, &config, &entry),
        VMX_EPT_STATUS_OK, "4KB physical page zero");
    TEST_ASSERT_UINT_EQ(entry, EPT_READ_WRITE_EXECUTE |
        ((uint64_t)EPT_MT_WRITE_BACK << EPT_MT_SHIFT), "4KB encoding");

    config = EntryConfig(VMX_EPT_ENTRY_LEAF_2MB, UINT64_C(0x200000));
    TEST_ASSERT_INT_EQ(VmxEptBuildEntry(&caps, &config, &entry),
        VMX_EPT_STATUS_OK, "2MB leaf");
    TEST_ASSERT_UINT_EQ(entry, UINT64_C(0x200000) |
        EPT_READ_WRITE_EXECUTE | EPT_PS_LARGE_PAGE |
        ((uint64_t)EPT_MT_WRITE_BACK << EPT_MT_SHIFT), "2MB encoding");

    config = EntryConfig(VMX_EPT_ENTRY_LEAF_1GB, UINT64_C(0x40000000));
    TEST_ASSERT_INT_EQ(VmxEptBuildEntry(&caps, &config, &entry),
        VMX_EPT_STATUS_OK, "1GB leaf");
    TEST_ASSERT_TRUE((entry & EPT_PS_LARGE_PAGE) != 0, "1GB large bit");

    config = EntryConfig(VMX_EPT_ENTRY_LEAF_4KB, UINT64_C(0x1000));
    config.Flags = VMX_EPT_ENTRY_FLAG_IGNORE_PAT |
        VMX_EPT_ENTRY_FLAG_AD_ENABLED | VMX_EPT_ENTRY_FLAG_ACCESSED |
        VMX_EPT_ENTRY_FLAG_DIRTY;
    TEST_ASSERT_INT_EQ(VmxEptBuildEntry(&caps, &config, &entry),
        VMX_EPT_STATUS_OK, "leaf PAT and AD");
    TEST_ASSERT_TRUE((entry & (EPT_IGNORE_PAT | EPT_ACCESSED | EPT_DIRTY)) ==
        (EPT_IGNORE_PAT | EPT_ACCESSED | EPT_DIRTY), "PAT and AD encoding");

    config = EntryConfig(VMX_EPT_ENTRY_NON_LEAF, UINT64_C(0x5000));
    config.Flags = VMX_EPT_ENTRY_FLAG_AD_ENABLED |
        VMX_EPT_ENTRY_FLAG_ACCESSED;
    TEST_ASSERT_INT_EQ(VmxEptBuildEntry(&caps, &config, &entry),
        VMX_EPT_STATUS_OK, "accessed non-leaf");
    TEST_ASSERT_TRUE((entry & EPT_ACCESSED) != 0, "non-leaf accessed bit");

    config = EntryConfig(VMX_EPT_ENTRY_LEAF_4KB, UINT64_C(0x1000));
    config.Permissions = VMX_EPT_PERMISSION_EXECUTE;
    TEST_ASSERT_INT_EQ(VmxEptBuildEntry(&caps, &config, &entry),
        VMX_EPT_STATUS_OK, "execute-only supported");

    for (i = 0; i < sizeof(memory_types) / sizeof(memory_types[0]); ++i) {
        config = EntryConfig(VMX_EPT_ENTRY_LEAF_4KB, UINT64_C(0x1000));
        config.MemoryType = memory_types[i];
        TEST_ASSERT_INT_EQ(VmxEptBuildEntry(&caps, &config, &entry),
            VMX_EPT_STATUS_OK, "architectural leaf memory type");
        TEST_ASSERT_UINT_EQ((entry >> EPT_MT_SHIFT) & 7, memory_types[i],
            "memory type encoding");
    }
    return 0;
}

static void ExpectEntryFailure(VMX_EPT_CAPABILITIES caps,
    VMX_EPT_ENTRY_CONFIG config, VMX_EPT_STATUS expected)
{
    uint64_t entry = UINT64_C(0xA5A5A5A5A5A5A5A5);
    TEST_ASSERT_INT_EQ(VmxEptBuildEntry(&caps, &config, &entry), expected,
        "entry rejection");
    TEST_ASSERT_UINT_EQ(entry, UINT64_C(0xA5A5A5A5A5A5A5A5),
        "entry failure preserves output");
}

static int test_entry_rejections(void)
{
    VMX_EPT_CAPABILITIES caps = Caps(RAW_REQUIRED);
    VMX_EPT_CAPABILITIES full_caps = Caps(RAW_REQUIRED | RAW_OPTIONAL);
    VMX_EPT_ENTRY_CONFIG config = EntryConfig(
        VMX_EPT_ENTRY_LEAF_4KB, UINT64_C(0x1000));
    uint64_t entry = UINT64_C(0x55);
    TEST_ASSERT_INT_EQ(VmxEptBuildEntry(NULL, &config, &entry),
        VMX_EPT_STATUS_INVALID, "null caps");
    TEST_ASSERT_INT_EQ(VmxEptBuildEntry(&caps, NULL, &entry),
        VMX_EPT_STATUS_INVALID, "null config");
    TEST_ASSERT_INT_EQ(VmxEptBuildEntry(&caps, &config, NULL),
        VMX_EPT_STATUS_INVALID, "null output");
    TEST_ASSERT_UINT_EQ(entry, UINT64_C(0x55), "null failures preserve output");

    config.Kind = 0;
    ExpectEntryFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config = EntryConfig(VMX_EPT_ENTRY_LEAF_4KB, UINT64_C(0x1000));
    config.PhysicalAddressBits = 35;
    ExpectEntryFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config.PhysicalAddressBits = 53;
    ExpectEntryFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config = EntryConfig(VMX_EPT_ENTRY_LEAF_4KB, UINT64_C(1) << 48);
    ExpectEntryFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config = EntryConfig(VMX_EPT_ENTRY_LEAF_4KB, UINT64_C(0x1001));
    ExpectEntryFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config = EntryConfig(VMX_EPT_ENTRY_LEAF_2MB, UINT64_C(0x201000));
    ExpectEntryFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config = EntryConfig(VMX_EPT_ENTRY_LEAF_1GB, UINT64_C(0x40100000));
    ExpectEntryFailure(full_caps, config, VMX_EPT_STATUS_INVALID);

    config = EntryConfig(VMX_EPT_ENTRY_LEAF_4KB, UINT64_C(0x1000));
    config.Permissions = 0;
    ExpectEntryFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config.Permissions = VMX_EPT_PERMISSION_WRITE;
    ExpectEntryFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config.Permissions = UINT8_C(8);
    ExpectEntryFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config.Permissions = VMX_EPT_PERMISSION_EXECUTE;
    ExpectEntryFailure(caps, config, VMX_EPT_STATUS_UNSUPPORTED);

    config = EntryConfig(VMX_EPT_ENTRY_LEAF_4KB, UINT64_C(0x1000));
    config.MemoryType = 2;
    ExpectEntryFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config = EntryConfig(VMX_EPT_ENTRY_NON_LEAF, UINT64_C(0x1000));
    config.MemoryType = VMX_EPT_MEMORY_TYPE_WB;
    ExpectEntryFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config.MemoryType = 0;
    config.Flags = VMX_EPT_ENTRY_FLAG_IGNORE_PAT;
    ExpectEntryFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config.Flags = VMX_EPT_ENTRY_FLAG_AD_ENABLED | VMX_EPT_ENTRY_FLAG_DIRTY;
    ExpectEntryFailure(caps, config, VMX_EPT_STATUS_INVALID);

    config = EntryConfig(VMX_EPT_ENTRY_LEAF_4KB, UINT64_C(0x1000));
    config.Flags = UINT8_C(16);
    ExpectEntryFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config.Flags = VMX_EPT_ENTRY_FLAG_ACCESSED;
    ExpectEntryFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config.Flags = VMX_EPT_ENTRY_FLAG_AD_ENABLED;
    ExpectEntryFailure(caps, config, VMX_EPT_STATUS_UNSUPPORTED);
    config.Flags = 0;
    config.Reserved = 1;
    ExpectEntryFailure(caps, config, VMX_EPT_STATUS_INVALID);

    config = EntryConfig(VMX_EPT_ENTRY_LEAF_1GB, UINT64_C(0x40000000));
    ExpectEntryFailure(caps, config, VMX_EPT_STATUS_UNSUPPORTED);
    config = EntryConfig(VMX_EPT_ENTRY_LEAF_4KB, UINT64_C(0x000FFFFFFFFFF000));
    config.PhysicalAddressBits = 52;
    TEST_ASSERT_INT_EQ(VmxEptBuildEntry(&caps, &config, &entry),
        VMX_EPT_STATUS_OK, "52-bit entry boundary");
    return 0;
}

static int test_validate_entry(void)
{
    VMX_EPT_CAPABILITIES caps = Caps(RAW_REQUIRED | RAW_OPTIONAL);
    VMX_EPT_ENTRY_CONFIG config = EntryConfig(
        VMX_EPT_ENTRY_LEAF_2MB, UINT64_C(0x200000));
    const uint64_t mutation_bits[] = {
        UINT64_C(1) << 0, UINT64_C(1) << 3, UINT64_C(1) << 7,
        UINT64_C(1) << 8, UINT64_C(1) << 9, UINT64_C(1) << 10,
        UINT64_C(1) << 11, UINT64_C(1) << 20, UINT64_C(1) << 52,
        UINT64_C(1) << 63
    };
    uint64_t entry;
    size_t i;
    TEST_ASSERT_INT_EQ(VmxEptBuildEntry(&caps, &config, &entry),
        VMX_EPT_STATUS_OK, "validator fixture");
    TEST_ASSERT_INT_EQ(VmxEptValidateEntry(&caps, &config, entry),
        VMX_EPT_STATUS_OK, "exact entry");
    for (i = 0; i < sizeof(mutation_bits) / sizeof(mutation_bits[0]); ++i)
        TEST_ASSERT_INT_EQ(VmxEptValidateEntry(
            &caps, &config, entry ^ mutation_bits[i]),
            VMX_EPT_STATUS_INVALID, "raw mutation rejected");
    TEST_ASSERT_INT_EQ(VmxEptValidateEntry(NULL, &config, entry),
        VMX_EPT_STATUS_INVALID, "validator null caps");
    TEST_ASSERT_INT_EQ(VmxEptValidateEntry(&caps, NULL, entry),
        VMX_EPT_STATUS_INVALID, "validator null config");
    return 0;
}
static VMX_EPT_IDENTITY_CONFIG IdentityConfig(uint8_t kind, uint64_t bytes)
{
    VMX_EPT_IDENTITY_CONFIG config = { 0 };
    config.MappedBytes = bytes;
    config.PhysicalAddressBits = 48;
    config.LeafKind = kind;
    return config;
}

static void ExpectPlanFailure(
    VMX_EPT_CAPABILITIES caps,
    VMX_EPT_IDENTITY_CONFIG config,
    VMX_EPT_STATUS expected)
{
    VMX_EPT_IDENTITY_PLAN plan;
    memset(&plan, 0xA5, sizeof(plan));
    TEST_ASSERT_INT_EQ(VmxEptBuildIdentityPlan(&caps, &config, &plan),
        expected, "plan failure status");
    TEST_ASSERT_UINT_EQ(plan.MappedBytes, UINT64_C(0xA5A5A5A5A5A5A5A5),
        "plan preserved");
}

static int test_identity_plan_geometry(void)
{
    VMX_EPT_CAPABILITIES caps = Caps(RAW_REQUIRED | RAW_OPTIONAL);
    VMX_EPT_IDENTITY_CONFIG config = IdentityConfig(
        VMX_EPT_ENTRY_LEAF_2MB, UINT64_C(0x100000000));
    VMX_EPT_IDENTITY_PLAN plan;
    TEST_ASSERT_INT_EQ(VmxEptBuildIdentityPlan(&caps, &config, &plan),
        VMX_EPT_STATUS_OK, "2MB plan");
    TEST_ASSERT_UINT_EQ(plan.LeafBytes, UINT64_C(0x200000), "2MB leaf");
    TEST_ASSERT_UINT_EQ(plan.LeafEntries, 2048, "2MB leaf count");
    TEST_ASSERT_UINT_EQ(plan.Pml4Entries, 1, "2MB PML4 entries");
    TEST_ASSERT_UINT_EQ(plan.Pml4Pages, 1, "root ownership");
    TEST_ASSERT_UINT_EQ(plan.PdptPages, 1, "2MB PDPT pages");
    TEST_ASSERT_UINT_EQ(plan.PdPages, 4, "2MB PD pages");
    TEST_ASSERT_UINT_EQ(plan.PtPages, 0, "2MB PT pages");
    TEST_ASSERT_UINT_EQ(plan.TotalTablePages, 6, "2MB total pages");
    TEST_ASSERT_UINT_EQ(plan.TotalTableBytes, UINT64_C(0x6000),
        "2MB table bytes");

    config = IdentityConfig(VMX_EPT_ENTRY_LEAF_4KB, UINT64_C(0x40000000));
    TEST_ASSERT_INT_EQ(VmxEptBuildIdentityPlan(&caps, &config, &plan),
        VMX_EPT_STATUS_OK, "4KB plan");
    TEST_ASSERT_UINT_EQ(plan.LeafEntries, 262144, "4KB leaf count");
    TEST_ASSERT_UINT_EQ(plan.PtPages, 512, "4KB PT pages");
    TEST_ASSERT_UINT_EQ(plan.PdPages, 1, "4KB PD pages");
    TEST_ASSERT_UINT_EQ(plan.PdptPages, 1, "4KB PDPT pages");
    TEST_ASSERT_UINT_EQ(plan.TotalTablePages, 515, "4KB total pages");

    config = IdentityConfig(VMX_EPT_ENTRY_LEAF_1GB,
        UINT64_C(513) * UINT64_C(0x40000000));
    TEST_ASSERT_INT_EQ(VmxEptBuildIdentityPlan(&caps, &config, &plan),
        VMX_EPT_STATUS_OK, "1GB plan");
    TEST_ASSERT_UINT_EQ(plan.LeafEntries, 513, "1GB leaf count");
    TEST_ASSERT_UINT_EQ(plan.Pml4Entries, 2, "1GB PML4 entries");
    TEST_ASSERT_UINT_EQ(plan.PdptPages, 2, "1GB PDPT pages");
    TEST_ASSERT_UINT_EQ(plan.TotalTablePages, 3, "1GB total pages");

    config.MappedBytes = UINT64_C(1) << 48;
    TEST_ASSERT_INT_EQ(VmxEptBuildIdentityPlan(&caps, &config, &plan),
        VMX_EPT_STATUS_OK, "four-level boundary");
    TEST_ASSERT_UINT_EQ(plan.PdptPages, 512, "maximum PDPT pages");
    TEST_ASSERT_UINT_EQ(plan.TotalTablePages, 513, "maximum 1GB plan");
    return 0;
}

static int test_identity_plan_rejections(void)
{
    VMX_EPT_CAPABILITIES caps = Caps(RAW_REQUIRED | RAW_OPTIONAL);
    VMX_EPT_CAPABILITIES required = Caps(RAW_REQUIRED);
    VMX_EPT_IDENTITY_CONFIG config = IdentityConfig(
        VMX_EPT_ENTRY_LEAF_2MB, UINT64_C(0x200000));
    VMX_EPT_IDENTITY_PLAN plan;
    memset(&plan, 0xA5, sizeof(plan));
    TEST_ASSERT_INT_EQ(VmxEptBuildIdentityPlan(NULL, &config, &plan),
        VMX_EPT_STATUS_INVALID, "null plan caps");
    TEST_ASSERT_INT_EQ(VmxEptBuildIdentityPlan(&caps, NULL, &plan),
        VMX_EPT_STATUS_INVALID, "null plan config");
    TEST_ASSERT_INT_EQ(VmxEptBuildIdentityPlan(&caps, &config, NULL),
        VMX_EPT_STATUS_INVALID, "null plan output");
    TEST_ASSERT_UINT_EQ(plan.MappedBytes, UINT64_C(0xA5A5A5A5A5A5A5A5),
        "null failure preserves output");

    config.MappedBytes = 0;
    ExpectPlanFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config.MappedBytes = UINT64_C(0x201000);
    ExpectPlanFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config = IdentityConfig(VMX_EPT_ENTRY_NON_LEAF, UINT64_C(0x200000));
    ExpectPlanFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config = IdentityConfig(VMX_EPT_ENTRY_LEAF_1GB, UINT64_C(0x40000000));
    ExpectPlanFailure(required, config, VMX_EPT_STATUS_UNSUPPORTED);
    config = IdentityConfig(VMX_EPT_ENTRY_LEAF_2MB, UINT64_C(1) << 36);
    config.PhysicalAddressBits = 36;
    TEST_ASSERT_INT_EQ(VmxEptBuildIdentityPlan(&caps, &config, &plan),
        VMX_EPT_STATUS_OK, "physical-width boundary");
    config.MappedBytes += UINT64_C(0x200000);
    ExpectPlanFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config = IdentityConfig(VMX_EPT_ENTRY_LEAF_1GB, UINT64_C(1) << 48);
    config.MappedBytes += UINT64_C(0x40000000);
    ExpectPlanFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config = IdentityConfig(VMX_EPT_ENTRY_LEAF_2MB, UINT64_C(0x200000));
    config.PhysicalAddressBits = 35;
    ExpectPlanFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config.PhysicalAddressBits = 53;
    ExpectPlanFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config.PhysicalAddressBits = 48;
    config.Reserved8[1] = 1;
    ExpectPlanFailure(caps, config, VMX_EPT_STATUS_INVALID);
    config.Reserved8[1] = 0;
    config.Reserved = 1;
    ExpectPlanFailure(caps, config, VMX_EPT_STATUS_INVALID);
    return 0;
}

static void ExpectPage(
    const VMX_EPT_CAPABILITIES* caps,
    const VMX_EPT_IDENTITY_CONFIG* config,
    const VMX_EPT_IDENTITY_PLAN* plan,
    uint32_t ordinal,
    uint32_t level,
    uint32_t index,
    uint32_t entries)
{
    VMX_EPT_OWNED_PAGE page;
    TEST_ASSERT_INT_EQ(VmxEptDescribeOwnedPage(
        caps, config, plan, ordinal, &page), VMX_EPT_STATUS_OK,
        "describe owned page");
    TEST_ASSERT_UINT_EQ(page.Ordinal, ordinal, "owned ordinal");
    TEST_ASSERT_UINT_EQ(page.Level, level, "owned level");
    TEST_ASSERT_UINT_EQ(page.LevelIndex, index, "owned level index");
    TEST_ASSERT_UINT_EQ(page.EntryCount, entries, "owned entries");
    TEST_ASSERT_UINT_EQ(page.Reserved, 0, "owned reserved");
}

static int test_identity_plan_ownership(void)
{
    VMX_EPT_CAPABILITIES caps = Caps(RAW_REQUIRED | RAW_OPTIONAL);
    VMX_EPT_IDENTITY_CONFIG config = IdentityConfig(
        VMX_EPT_ENTRY_LEAF_2MB, UINT64_C(513) * UINT64_C(0x200000));
    VMX_EPT_IDENTITY_PLAN plan;
    VMX_EPT_OWNED_PAGE page;
    TEST_ASSERT_INT_EQ(VmxEptBuildIdentityPlan(&caps, &config, &plan),
        VMX_EPT_STATUS_OK, "ownership plan");
    ExpectPage(&caps, &config, &plan, 0, VMX_EPT_TABLE_LEVEL_PML4, 0, 1);
    ExpectPage(&caps, &config, &plan, 1, VMX_EPT_TABLE_LEVEL_PDPT, 0, 2);
    ExpectPage(&caps, &config, &plan, 2, VMX_EPT_TABLE_LEVEL_PD, 0, 512);
    ExpectPage(&caps, &config, &plan, 3, VMX_EPT_TABLE_LEVEL_PD, 1, 1);

    memset(&page, 0xA5, sizeof(page));
    TEST_ASSERT_INT_EQ(VmxEptDescribeOwnedPage(
        &caps, &config, &plan, plan.TotalTablePages, &page),
        VMX_EPT_STATUS_INVALID, "ordinal bound");
    TEST_ASSERT_UINT_EQ(page.Ordinal, UINT32_C(0xA5A5A5A5),
        "descriptor preserved");
    TEST_ASSERT_INT_EQ(VmxEptDescribeOwnedPage(
        &caps, &config, &plan, 0, NULL), VMX_EPT_STATUS_INVALID,
        "null descriptor");

    TEST_ASSERT_INT_EQ(VmxEptValidateIdentityPlan(&caps, &config, &plan),
        VMX_EPT_STATUS_OK, "exact plan");
    plan.TotalTablePages++;
    TEST_ASSERT_INT_EQ(VmxEptValidateIdentityPlan(&caps, &config, &plan),
        VMX_EPT_STATUS_INVALID, "mutated total rejected");
    TEST_ASSERT_INT_EQ(VmxEptDescribeOwnedPage(
        &caps, &config, &plan, 0, &page), VMX_EPT_STATUS_INVALID,
        "mutated plan rejected");
    plan.TotalTablePages--;
    plan.Reserved = 1;
    TEST_ASSERT_INT_EQ(VmxEptValidateIdentityPlan(&caps, &config, &plan),
        VMX_EPT_STATUS_INVALID, "reserved plan rejected");

    config = IdentityConfig(VMX_EPT_ENTRY_LEAF_4KB,
        UINT64_C(513) * UINT64_C(0x1000));
    TEST_ASSERT_INT_EQ(VmxEptBuildIdentityPlan(&caps, &config, &plan),
        VMX_EPT_STATUS_OK, "4KB ownership plan");
    ExpectPage(&caps, &config, &plan, 3, VMX_EPT_TABLE_LEVEL_PT, 0, 512);
    ExpectPage(&caps, &config, &plan, 4, VMX_EPT_TABLE_LEVEL_PT, 1, 1);

    config = IdentityConfig(VMX_EPT_ENTRY_LEAF_1GB,
        UINT64_C(513) * UINT64_C(0x40000000));
    TEST_ASSERT_INT_EQ(VmxEptBuildIdentityPlan(&caps, &config, &plan),
        VMX_EPT_STATUS_OK, "1GB ownership plan");
    ExpectPage(&caps, &config, &plan, 0, VMX_EPT_TABLE_LEVEL_PML4, 0, 2);
    ExpectPage(&caps, &config, &plan, 1, VMX_EPT_TABLE_LEVEL_PDPT, 0, 512);
    ExpectPage(&caps, &config, &plan, 2, VMX_EPT_TABLE_LEVEL_PDPT, 1, 1);
    return 0;
}
TEST_MAIN_BEGIN()
    TEST_RUN(test_decode_and_validate);
    TEST_RUN(test_capability_rejections);
    TEST_RUN(test_build_pointer);
    TEST_RUN(test_build_rejections);
    TEST_RUN(test_build_entries);
    TEST_RUN(test_entry_rejections);
    TEST_RUN(test_validate_entry);
    TEST_RUN(test_identity_plan_geometry);
    TEST_RUN(test_identity_plan_rejections);
    TEST_RUN(test_identity_plan_ownership);
TEST_MAIN_END()
