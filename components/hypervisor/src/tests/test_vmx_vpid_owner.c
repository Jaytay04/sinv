#include "test_common.h"
#include "vmx_vpid_owner.h"

static const VMX_PROCESSOR_IDENTITY Topology[4] = {
    { 0, 0, 0, 0 }, { 0, 1, 0, 1 },
    { 1, 0, 0, 2 }, { 1, 1, 0, 3 }
};

static int test_multigroup_allocation(void)
{
    VMX_VPID_OWNER Owner = { 0 };
    VMX_VPID_RECORD Records[4] = { 0 };
    uint16_t Vpid = 0;
    size_t Index;
    TEST_ASSERT_INT_EQ(VmxVpidOwnerInitialize(Topology, 4, Records, &Owner),
        VMX_VPID_OWNER_OK, "multi-group initialize");
    for (Index = 0; Index < 4; ++Index) {
        Vpid = 0;
        TEST_ASSERT_INT_EQ(VmxVpidOwnerAcquire(&Owner, &Topology[Index], &Vpid),
            VMX_VPID_OWNER_OK, "multi-group acquire");
        TEST_ASSERT_UINT_EQ(Vpid, Index + 1, "system-order nonzero VPID");
    }
    TEST_ASSERT_INT_EQ(VmxVpidOwnerDestroy(&Owner), VMX_VPID_OWNER_BUSY,
        "destroy rejects owned identifiers");
    return 0;
}

static int test_invalidation_before_reuse(void)
{
    VMX_VPID_OWNER Owner = { 0 };
    VMX_VPID_RECORD Records[4] = { 0 };
    uint16_t Vpid = 0;
    TEST_ASSERT_INT_EQ(VmxVpidOwnerInitialize(Topology, 4, Records, &Owner),
        VMX_VPID_OWNER_OK, "initialize");
    TEST_ASSERT_INT_EQ(VmxVpidOwnerAcquire(&Owner, &Topology[2], &Vpid),
        VMX_VPID_OWNER_OK, "acquire");
    TEST_ASSERT_UINT_EQ(Vpid, 3, "group-one VPID");
    TEST_ASSERT_INT_EQ(VmxVpidOwnerAcquire(&Owner, &Topology[2], &Vpid),
        VMX_VPID_OWNER_ALREADY_OWNED, "duplicate ownership rejected");
    TEST_ASSERT_INT_EQ(VmxVpidOwnerRetire(&Owner, &Topology[2], 3),
        VMX_VPID_OWNER_OK, "retire");
    TEST_ASSERT_INT_EQ(VmxVpidOwnerAcquire(&Owner, &Topology[2], &Vpid),
        VMX_VPID_OWNER_RETIRE_PENDING, "reuse before invalidation rejected");
    TEST_ASSERT_INT_EQ(VmxVpidOwnerRetire(&Owner, &Topology[2], 3),
        VMX_VPID_OWNER_RETIRE_PENDING, "retire retry remains pending");
    TEST_ASSERT_INT_EQ(VmxVpidOwnerAcknowledgeInvalidation(
        &Owner, &Topology[2], 3), VMX_VPID_OWNER_OK,
        "successful invalidation acknowledged");
    TEST_ASSERT_INT_EQ(VmxVpidOwnerAcquire(&Owner, &Topology[2], &Vpid),
        VMX_VPID_OWNER_OK, "reuse after invalidation");
    return 0;
}

static int test_invalid_inputs_and_aliases(void)
{
    VMX_VPID_OWNER Owner = { 0 };
    VMX_VPID_RECORD Records[4] = { 0 };
    VMX_PROCESSOR_IDENTITY Bad[4];
    uint16_t Vpid = 0;
    memcpy(Bad, Topology, sizeof(Bad));
    Bad[3] = Bad[2]; Bad[3].SystemIndex = 3;
    TEST_ASSERT_INT_EQ(VmxVpidOwnerInitialize(Bad, 4, Records, &Owner),
        VMX_VPID_OWNER_INVALID, "duplicate group identity rejected");
    memset(&Owner, 0, sizeof(Owner)); memset(Records, 0, sizeof(Records));
    memcpy(Bad, Topology, sizeof(Bad)); Bad[3].SystemIndex = 7;
    TEST_ASSERT_INT_EQ(VmxVpidOwnerInitialize(Bad, 4, Records, &Owner),
        VMX_VPID_OWNER_INVALID, "noncontiguous system identity rejected");
    TEST_ASSERT_INT_EQ(VmxVpidOwnerInitialize(Topology,
        (size_t)VMX_VPID_MAX + 1, Records, &Owner),
        VMX_VPID_OWNER_INVALID, "VPID exhaustion rejected");
    memset(&Owner, 0, sizeof(Owner)); memset(Records, 0, sizeof(Records));
    TEST_ASSERT_INT_EQ(VmxVpidOwnerInitialize(Topology, 4, Records, &Owner),
        VMX_VPID_OWNER_OK, "valid initialize");
    TEST_ASSERT_INT_EQ(VmxVpidOwnerAcquire(&Owner, &Topology[0],
        (uint16_t*)&Records[0]), VMX_VPID_OWNER_INVALID,
        "aliased output rejected");
    TEST_ASSERT_INT_EQ(VmxVpidOwnerRetire(&Owner, &Topology[0], 2),
        VMX_VPID_OWNER_INVALID, "wrong identity VPID rejected");
    TEST_ASSERT_UINT_EQ(Vpid, 0, "invalid calls preserve local output");
    return 0;
}

static int test_destroy_and_corruption(void)
{
    VMX_VPID_OWNER Owner = { 0 };
    VMX_VPID_RECORD Records[4] = { 0 };
    TEST_ASSERT_INT_EQ(VmxVpidOwnerInitialize(Topology, 4, Records, &Owner),
        VMX_VPID_OWNER_OK, "initialize");
    Records[1].Vpid = Records[0].Vpid;
    TEST_ASSERT_INT_EQ(VmxVpidOwnerValidate(&Owner), VMX_VPID_OWNER_INVALID,
        "identifier alias detected");
    Records[1].Vpid = 2;
    TEST_ASSERT_INT_EQ(VmxVpidOwnerDestroy(&Owner), VMX_VPID_OWNER_OK,
        "free owner destroy");
    TEST_ASSERT_UINT_EQ(Owner.Magic, 0, "owner zero after destroy");
    TEST_ASSERT_UINT_EQ(Records[3].Vpid, 0, "storage zero after destroy");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_multigroup_allocation);
    TEST_RUN(test_invalidation_before_reuse);
    TEST_RUN(test_invalid_inputs_and_aliases);
    TEST_RUN(test_destroy_and_corruption);
TEST_MAIN_END()
