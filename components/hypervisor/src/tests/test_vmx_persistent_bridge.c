#include "test_common.h"
#include "vmx_persistent_bridge.h"
#include <stdint.h>
#include <string.h>

#define N 40u
typedef struct _FAKE_OPS { size_t Calls[4]; int Which; void* SeenContext; VMX_PERSISTENT_CONTEXT* SeenPersistent; VMX_PERSISTENT_CPU_RECORD* SeenRecord; VMX_PERSISTENT_STATUS Status; bool MutateRecord, MutateContext; } FAKE_OPS;
static VMX_PROCESSOR_IDENTITY Id(uint32_t n) { VMX_PROCESSOR_IDENTITY x = {(uint16_t)(n / 64u), (uint8_t)(n % 64u), 0, n}; return x; }
static VMX_PERSISTENT_STATUS DoOp(void* context, int which, VMX_PERSISTENT_CONTEXT* persistent, VMX_PERSISTENT_CPU_RECORD* record)
{
    FAKE_OPS* f = (FAKE_OPS*)context; f->Calls[which]++; f->Which = which; f->SeenContext = context; f->SeenPersistent = persistent; f->SeenRecord = record;
    if (f->MutateRecord) record->Identity.SystemIndex++;
    if (f->MutateContext) persistent->Identity.SystemIndex++;
    return f->Status;
}
#define OP(name, n) static VMX_PERSISTENT_STATUS name(void* c, VMX_PERSISTENT_CONTEXT* p, VMX_PERSISTENT_CPU_RECORD* r) { return DoOp(c, n, p, r); }
OP(Prepare, 0) OP(Activate, 1) OP(Stop, 2) OP(Destroy, 3)
static VMX_PERSISTENT_BRIDGE_OPS Ops(void) { VMX_PERSISTENT_BRIDGE_OPS o = {Prepare, Activate, Stop, Destroy}; return o; }
static void Fill(VMX_PERSISTENT_CPU_RECORD* records, VMX_PERSISTENT_CONTEXT* contexts, size_t count, uint32_t first)
{ size_t i; for (i = 0; i < count; ++i) { records[i].Identity = Id(first + (uint32_t)i); contexts[i].Identity = records[i].Identity; } }
static int test_valid_mapping(void)
{
    VMX_PERSISTENT_CPU_RECORD records[N]; VMX_PERSISTENT_CONTEXT contexts[N]; VMX_PERSISTENT_BRIDGE bridge; VMX_PERSISTENT_CALLBACKS callbacks; FAKE_OPS fake = {0}; VMX_PERSISTENT_BRIDGE_OPS ops = Ops(); size_t i; uint32_t index = 33;
    Fill(records, contexts, N, 48); memset(&bridge, 0xA5, sizeof(bridge));
    TEST_ASSERT_INT_EQ(VmxPersistentBridgeInitialize(records, contexts, N, &fake, &ops, &bridge), VMX_PERSISTENT_STATUS_OK, "valid init");
    TEST_ASSERT_TRUE(bridge.Records == records && bridge.Contexts == contexts && bridge.Count == N && bridge.OperationContext == &fake, "bridge mapping");
    TEST_ASSERT_UINT_EQ(records[0].Identity.Group, 0, "group zero"); TEST_ASSERT_UINT_EQ(records[16].Identity.Group, 1, "group one"); TEST_ASSERT_UINT_EQ(records[39].Identity.Number, 23, "last number");
    callbacks = VmxPersistentBridgeCallbacks(&bridge);
    TEST_ASSERT_INT_EQ(callbacks.Prepare(callbacks.Context, &records[index]), 0, "prepare map"); TEST_ASSERT_INT_EQ(callbacks.Activate(callbacks.Context, &records[index]), 0, "activate map"); TEST_ASSERT_INT_EQ(callbacks.Stop(callbacks.Context, &records[index]), 0, "stop map"); TEST_ASSERT_INT_EQ(callbacks.Destroy(callbacks.Context, &records[index]), 0, "destroy map");
    for (i = 0; i < 4; ++i) TEST_ASSERT_UINT_EQ(fake.Calls[i], 1, "one op call");
    TEST_ASSERT_TRUE(fake.SeenContext == &fake && fake.SeenPersistent == &contexts[index] && fake.SeenRecord == &records[index], "mapped pointers");
    return 0;
}
static int test_rejects_bad_inputs(void)
{
    VMX_PERSISTENT_CPU_RECORD records[2], forged; VMX_PERSISTENT_CONTEXT contexts[2]; VMX_PERSISTENT_BRIDGE bridge, before; VMX_PERSISTENT_BRIDGE_OPS ops = Ops(); FAKE_OPS fake = {0}; size_t i;
    Fill(records, contexts, 2, 1); memset(&bridge, 0xA5, sizeof(bridge)); before = bridge;
    TEST_ASSERT_INT_EQ(VmxPersistentBridgeInitialize(records, contexts, 2, &fake, NULL, &bridge), VMX_PERSISTENT_STATUS_INVALID, "missing ops"); TEST_ASSERT_TRUE(memcmp(&bridge, &before, sizeof bridge) == 0, "missing ops preserves");
    for (i = 0; i < 4; ++i) { VMX_PERSISTENT_BRIDGE_OPS missing = ops; if (i == 0) missing.Prepare = NULL; if (i == 1) missing.Activate = NULL; if (i == 2) missing.Stop = NULL; if (i == 3) missing.Destroy = NULL; TEST_ASSERT_INT_EQ(VmxPersistentBridgeInitialize(records, contexts, 2, &fake, &missing, &bridge), VMX_PERSISTENT_STATUS_INVALID, "missing callback"); TEST_ASSERT_TRUE(memcmp(&bridge, &before, sizeof bridge) == 0, "callback preserves"); }
    TEST_ASSERT_INT_EQ(VmxPersistentBridgeInitialize(NULL, contexts, 2, &fake, &ops, &bridge), VMX_PERSISTENT_STATUS_INVALID, "null records"); TEST_ASSERT_INT_EQ(VmxPersistentBridgeInitialize(records, (VMX_PERSISTENT_CONTEXT*)records, 1, &fake, &ops, &bridge), VMX_PERSISTENT_STATUS_INVALID, "aliased arrays"); TEST_ASSERT_INT_EQ(VmxPersistentBridgeInitialize(records, contexts, 0, &fake, &ops, &bridge), VMX_PERSISTENT_STATUS_INVALID, "zero count"); TEST_ASSERT_INT_EQ(VmxPersistentBridgeInitialize(records, contexts, 2, &fake, &ops, NULL), VMX_PERSISTENT_STATUS_INVALID, "null output");
    forged = records[0]; TEST_ASSERT_INT_EQ(VmxPersistentBridgeInitialize(records, contexts, 2, &fake, &ops, &bridge), VMX_PERSISTENT_STATUS_OK, "setup rejects"); TEST_ASSERT_INT_EQ(VmxPersistentBridgePrepare(NULL, &records[0]), VMX_PERSISTENT_STATUS_INVALID, "null bridge");
    TEST_ASSERT_INT_EQ(VmxPersistentBridgePrepare(&bridge, &forged), VMX_PERSISTENT_STATUS_INVALID, "forged record"); TEST_ASSERT_INT_EQ(VmxPersistentBridgePrepare(&bridge, (VMX_PERSISTENT_CPU_RECORD*)((unsigned char*)records + 1)), VMX_PERSISTENT_STATUS_INVALID, "interior record"); TEST_ASSERT_INT_EQ(VmxPersistentBridgePrepare(&bridge, &records[2]), VMX_PERSISTENT_STATUS_INVALID, "unknown record"); TEST_ASSERT_UINT_EQ(fake.Calls[0], 0, "rejected no call");
    contexts[0].Identity.SystemIndex++; before = bridge; TEST_ASSERT_INT_EQ(VmxPersistentBridgeInitialize(records, contexts, 2, &fake, &ops, &bridge), VMX_PERSISTENT_STATUS_INVALID, "mismatched identity"); TEST_ASSERT_TRUE(memcmp(&bridge, &before, sizeof bridge) == 0, "mismatch preserves"); contexts[0].Identity = records[0].Identity; records[1].Identity = records[0].Identity; contexts[1].Identity = records[0].Identity; before = bridge; TEST_ASSERT_INT_EQ(VmxPersistentBridgeInitialize(records, contexts, 2, &fake, &ops, &bridge), VMX_PERSISTENT_STATUS_INVALID, "duplicate identity"); TEST_ASSERT_TRUE(memcmp(&bridge, &before, sizeof bridge) == 0, "duplicate preserves");
    { _Alignas(VMX_PERSISTENT_CONTEXT) unsigned char storage[sizeof(VMX_PERSISTENT_CONTEXT) + sizeof(VMX_PERSISTENT_CPU_RECORD)]; VMX_PERSISTENT_CPU_RECORD* overlap_record = (VMX_PERSISTENT_CPU_RECORD*)(void*)storage; VMX_PERSISTENT_CONTEXT* overlap_context = (VMX_PERSISTENT_CONTEXT*)(void*)(storage + _Alignof(VMX_PERSISTENT_CONTEXT)); before = bridge; TEST_ASSERT_INT_EQ(VmxPersistentBridgeInitialize(overlap_record, overlap_context, 1, &fake, &ops, &bridge), VMX_PERSISTENT_STATUS_INVALID, "partial overlap"); TEST_ASSERT_TRUE(memcmp(&bridge, &before, sizeof bridge) == 0, "overlap preserves"); TEST_ASSERT_UINT_EQ(fake.Calls[0], 0, "overlap no call"); }
    before = bridge; TEST_ASSERT_INT_EQ(VmxPersistentBridgeInitialize(records, contexts, SIZE_MAX / sizeof(*records) + 1, &fake, &ops, &bridge), VMX_PERSISTENT_STATUS_INVALID, "count overflow"); TEST_ASSERT_TRUE(memcmp(&bridge, &before, sizeof bridge) == 0, "overflow preserves"); TEST_ASSERT_UINT_EQ(fake.Calls[0], 0, "overflow no call");
    return 0;
}
static int test_identity_guard_and_status(void)
{
    VMX_PERSISTENT_CPU_RECORD record; VMX_PERSISTENT_CONTEXT context; VMX_PERSISTENT_BRIDGE bridge; VMX_PERSISTENT_BRIDGE_OPS ops = Ops(); FAKE_OPS fake = {0}; VMX_PROCESSOR_IDENTITY identity; VMX_PERSISTENT_STATUS status;
    Fill(&record, &context, 1, 4); TEST_ASSERT_INT_EQ(VmxPersistentBridgeInitialize(&record, &context, 1, &fake, &ops, &bridge), VMX_PERSISTENT_STATUS_OK, "guard setup"); identity = record.Identity; fake.MutateRecord = true; TEST_ASSERT_INT_EQ(VmxPersistentBridgePrepare(&bridge, &record), VMX_PERSISTENT_STATUS_INVARIANT, "record mutation"); TEST_ASSERT_TRUE(memcmp(&record.Identity, &identity, sizeof identity) == 0, "record restored"); fake.MutateRecord = false; fake.MutateContext = true; TEST_ASSERT_INT_EQ(VmxPersistentBridgePrepare(&bridge, &record), VMX_PERSISTENT_STATUS_INVARIANT, "context mutation"); TEST_ASSERT_TRUE(memcmp(&context.Identity, &identity, sizeof identity) == 0, "context restored"); fake.MutateContext = false; fake.Status = VMX_PERSISTENT_STATUS_NOT_READY; status = VmxPersistentBridgePrepare(&bridge, &record); TEST_ASSERT_INT_EQ(status, VMX_PERSISTENT_STATUS_NOT_READY, "status propagated"); TEST_ASSERT_UINT_EQ(fake.Calls[0], 3, "guard calls"); return 0;
}
TEST_MAIN_BEGIN()
    TEST_RUN(test_valid_mapping);
    TEST_RUN(test_rejects_bad_inputs);
    TEST_RUN(test_identity_guard_and_status);
TEST_MAIN_END()
