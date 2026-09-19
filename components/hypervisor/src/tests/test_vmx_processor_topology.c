/* Pure topology snapshot contract tests; no WDK or live VMX. */
#include "test_common.h"
#include "vmx_processor_topology.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef struct _FAKE_TOPOLOGY {
    size_t Count;
    VMX_PROCESSOR_IDENTITY Identities[8];
    bool QueryFailure;
    bool MapFailure;
    uint32_t MapFailureIndex;
    bool Drift;
    bool AllocationFailure;
    size_t QueryCalls;
    size_t MapCalls;
    size_t AllocationCalls;
    size_t FreeCalls;
    size_t LastAllocationSize;
} FAKE_TOPOLOGY;
static bool FakeCount(void* context, size_t* count)
{
    FAKE_TOPOLOGY* fake = (FAKE_TOPOLOGY*)context;
    fake->QueryCalls++;
    if (fake->QueryFailure || count == NULL) return false;
    *count = fake->Count + ((fake->Drift && fake->QueryCalls > 1) ? 1u : 0u);
    return true;
}
static bool FakeMap(void* context, uint32_t index,
                    VMX_PROCESSOR_IDENTITY* identity)
{
    FAKE_TOPOLOGY* fake = (FAKE_TOPOLOGY*)context;
    fake->MapCalls++;
    if (identity == NULL || (fake->MapFailure && index == fake->MapFailureIndex) ||
        index >= fake->Count) return false;
    *identity = fake->Identities[index];
    return true;
}
static void* FakeAllocate(void* context, size_t size)
{
    FAKE_TOPOLOGY* fake = (FAKE_TOPOLOGY*)context;
    fake->AllocationCalls++;
    fake->LastAllocationSize = size;
    return fake->AllocationFailure ? NULL : calloc(1, size);
}
static void FakeFree(void* context, void* memory)
{
    FAKE_TOPOLOGY* fake = (FAKE_TOPOLOGY*)context;
    fake->FreeCalls++;
    free(memory);
}
static VMX_PROCESSOR_TOPOLOGY_PROVIDER Provider(FAKE_TOPOLOGY* fake)
{
    VMX_PROCESSOR_TOPOLOGY_PROVIDER provider = {
        fake, FakeCount, FakeMap, FakeAllocate, FakeFree
    };
    return provider;
}
static void InitFake(FAKE_TOPOLOGY* fake, size_t count)
{
    size_t i;
    (void)memset(fake, 0, sizeof(*fake));
    fake->Count = count;
    for (i = 0; i < count && i < 8; ++i) {
        fake->Identities[i].Group = (uint16_t)(i / 2);
        fake->Identities[i].Number = (uint8_t)(i % 2);
        fake->Identities[i].SystemIndex = (uint32_t)i;
    }
}
static int test_one_group_and_lookup(void)
{
    FAKE_TOPOLOGY fake;
    VMX_PROCESSOR_TOPOLOGY_PROVIDER provider;
    VMX_PROCESSOR_TOPOLOGY_SNAPSHOT snapshot = { 0 };
    VMX_PROCESSOR_IDENTITY wanted;
    InitFake(&fake, 3);
    fake.Identities[0].Group = fake.Identities[1].Group = fake.Identities[2].Group = 7; fake.Identities[0].Number = 0; fake.Identities[1].Number = 1; fake.Identities[2].Number = 2;
    provider = Provider(&fake);
    TEST_ASSERT_TRUE(VmxProcessorTopologySnapshotCreate(&provider, &snapshot), "one-group create");
    TEST_ASSERT_UINT_EQ(snapshot.Count, 3, "one-group count");
    TEST_ASSERT_UINT_EQ(snapshot.Processors[2].Number, 2, "one-group number");
    TEST_ASSERT_NOT_NULL(VmxProcessorTopologyLookupSystemIndex(&snapshot, 1), "index lookup"); TEST_ASSERT_NULL(VmxProcessorTopologyLookupSystemIndex(&snapshot, 99), "missing index");
    wanted = snapshot.Processors[1];
    TEST_ASSERT_TRUE(VmxProcessorIdentityEqual(&wanted, VmxProcessorTopologyLookupIdentity(&snapshot, &wanted)), "identity lookup/equality"); wanted.SystemIndex = 99; TEST_ASSERT_NULL(VmxProcessorTopologyLookupIdentity(&snapshot, &wanted), "mismatched identity");
    VmxProcessorTopologySnapshotDestroy(&snapshot);
    return 0;
}
static int test_multi_group(void)
{
    FAKE_TOPOLOGY fake;
    VMX_PROCESSOR_TOPOLOGY_PROVIDER provider;
    VMX_PROCESSOR_TOPOLOGY_SNAPSHOT snapshot = { 0 };
    InitFake(&fake, 4);
    provider = Provider(&fake);
    TEST_ASSERT_TRUE(VmxProcessorTopologySnapshotCreate(&provider, &snapshot), "multi-group create");
    TEST_ASSERT_UINT_EQ(snapshot.Processors[0].Group, 0, "group zero");
    TEST_ASSERT_UINT_EQ(snapshot.Processors[2].Group, 1, "group one");
    TEST_ASSERT_UINT_EQ(snapshot.Processors[3].SystemIndex, 3, "all indexes");
    VmxProcessorTopologySnapshotDestroy(&snapshot);
    return 0;
}
static int test_rejections_preserve_output(void)
{
    FAKE_TOPOLOGY fake;
    VMX_PROCESSOR_TOPOLOGY_PROVIDER provider;
    VMX_PROCESSOR_TOPOLOGY_SNAPSHOT out;
    VMX_PROCESSOR_TOPOLOGY_SNAPSHOT before;
    InitFake(&fake, 2);
    provider = Provider(&fake);
    (void)memset(&out, 0xA5, sizeof(out));
    before = out;
    fake.MapFailure = true;
    fake.MapFailureIndex = 1;
    TEST_ASSERT_FALSE(VmxProcessorTopologySnapshotCreate(&provider, &out), "map failure");
    TEST_ASSERT_TRUE(memcmp(&out, &before, sizeof(out)) == 0, "output preserved");
    fake.MapFailure = false;
    fake.Count = 0;
    TEST_ASSERT_FALSE(VmxProcessorTopologySnapshotCreate(&provider, &out), "zero count");
    fake.QueryFailure = true;
    fake.Count = 2;
    TEST_ASSERT_FALSE(VmxProcessorTopologySnapshotCreate(&provider, &out), "count provider failure"); TEST_ASSERT_UINT_EQ(fake.FreeCalls, 1, "allocated rejection freed once");
    return 0;
}
static int test_rejects_drift_duplicates_and_number(void)
{
    FAKE_TOPOLOGY fake;
    VMX_PROCESSOR_TOPOLOGY_PROVIDER provider;
    VMX_PROCESSOR_TOPOLOGY_SNAPSHOT out = { 0 };
    InitFake(&fake, 2);
    provider = Provider(&fake);
    fake.Drift = true;
    TEST_ASSERT_FALSE(VmxProcessorTopologySnapshotCreate(&provider, &out), "count drift");
    fake.Drift = false;
    fake.Identities[1].Group = fake.Identities[0].Group;
    fake.Identities[1].Number = fake.Identities[0].Number;
    TEST_ASSERT_FALSE(VmxProcessorTopologySnapshotCreate(&provider, &out), "duplicate identity");
    fake.Identities[1].Group = 1;
    fake.Identities[1].Number = 1;
    fake.Identities[1].SystemIndex = 0;
    TEST_ASSERT_FALSE(VmxProcessorTopologySnapshotCreate(&provider, &out), "duplicate index");
    fake.Identities[1].SystemIndex = 1;
    fake.Identities[1].Number = 64;
    TEST_ASSERT_FALSE(VmxProcessorTopologySnapshotCreate(&provider, &out), "invalid number"); TEST_ASSERT_UINT_EQ(fake.FreeCalls, 4, "all allocated rejections freed once");
    return 0;
}
static int test_allocation_failure_overflow_and_destroy(void)
{
    FAKE_TOPOLOGY fake;
    VMX_PROCESSOR_TOPOLOGY_PROVIDER provider;
    VMX_PROCESSOR_TOPOLOGY_SNAPSHOT snapshot = { 0 }; FAKE_TOPOLOGY other;
    InitFake(&fake, 2); provider = Provider(&fake);
    fake.AllocationFailure = true;
    TEST_ASSERT_FALSE(VmxProcessorTopologySnapshotCreate(&provider, &snapshot), "allocation failure");
    fake.AllocationFailure = false;
    fake.Count = (size_t)UINT32_MAX + 1u;
    TEST_ASSERT_FALSE(VmxProcessorTopologySnapshotCreate(&provider, &snapshot), "count exceeds index range"); TEST_ASSERT_UINT_EQ(fake.AllocationCalls, 1, "count bound skips allocation");
    InitFake(&fake, 2);
    TEST_ASSERT_TRUE(VmxProcessorTopologySnapshotCreate(&provider, &snapshot), "destroy setup"); InitFake(&other, 0); provider.Context = &other;
    VmxProcessorTopologySnapshotDestroy(&snapshot);
    VmxProcessorTopologySnapshotDestroy(&snapshot);
    TEST_ASSERT_UINT_EQ(fake.FreeCalls, 1, "destroy exact once"); TEST_ASSERT_UINT_EQ(other.FreeCalls, 0, "captured allocator context");
    TEST_ASSERT_NULL(snapshot.Processors, "destroy clears pointer");
    TEST_ASSERT_UINT_EQ(snapshot.Count, 0, "destroy clears count");
    return 0;
}
TEST_MAIN_BEGIN()
    TEST_RUN(test_one_group_and_lookup);
    TEST_RUN(test_multi_group);
    TEST_RUN(test_rejections_preserve_output);
    TEST_RUN(test_rejects_drift_duplicates_and_number);
    TEST_RUN(test_allocation_failure_overflow_and_destroy);
TEST_MAIN_END()
