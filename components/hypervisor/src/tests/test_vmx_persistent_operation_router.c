#include "test_common.h"
#include "vmx_persistent_operation_router.h"
#include <stdint.h>
#include <string.h>
#define N 65
typedef struct _LOG { size_t Count; unsigned Events[16]; } LOG;
typedef struct _FAKE { LOG* Log; unsigned Code; unsigned Index;
    VMX_PERSISTENT_STATUS Status; bool Mutate; } FAKE;
static VMX_PERSISTENT_STATUS Fake(void* p, VMX_PERSISTENT_CONTEXT* c,
                                  VMX_PERSISTENT_CPU_RECORD* r)
{
    FAKE* f = (FAKE*)p;
    f->Log->Events[f->Log->Count++] = f->Code * 100u + f->Index;
    if (f->Mutate) { c->Identity.Group++; r->Identity.Number++; }
    return f->Status;
}
static VMX_PROCESSOR_IDENTITY Identity(size_t i)
{
    VMX_PROCESSOR_IDENTITY x = {
        (uint16_t)(i / VMX_PROCESSOR_GROUP_MAX_NUMBER),
        (uint8_t)(i % VMX_PROCESSOR_GROUP_MAX_NUMBER), 0, (uint32_t)i
    };
    return x;
}
static void Initialize(VMX_PERSISTENT_CPU_RECORD* records,
                       VMX_PERSISTENT_CONTEXT* contexts,
                       VMX_PERSISTENT_OPERATION_BINDING* bindings,
                       FAKE fake[N][4], LOG* log)
{
    size_t i, j;
    (void)memset(records, 0, N * sizeof(*records));
    (void)memset(contexts, 0, N * sizeof(*contexts));
    (void)memset(bindings, 0, N * sizeof(*bindings));
    (void)memset(fake, 0, N * 4u * sizeof(**fake));
    for (i = 0; i < N; ++i) {
        records[i].Identity = contexts[i].Identity = Identity(i);
        for (j = 0; j < 4; ++j) {
            fake[i][j].Log = log; fake[i][j].Code = (unsigned)(j + 1);
            fake[i][j].Index = (unsigned)i;
        }
        bindings[i].PrepareContext = &fake[i][0];
        bindings[i].ActivateContext = &fake[i][1];
        bindings[i].StopContext = &fake[i][2];
        bindings[i].DestroyContext = &fake[i][3];
        bindings[i].Operations =
            (VMX_PERSISTENT_BRIDGE_OPS){Fake, Fake, Fake, Fake};
    }
}
static int test_routing(void)
{
    VMX_PERSISTENT_CPU_RECORD records[N]; VMX_PERSISTENT_CONTEXT contexts[N];
    VMX_PERSISTENT_OPERATION_BINDING bindings[N]; FAKE fake[N][4];
    VMX_PERSISTENT_OPERATION_ROUTER router; VMX_PERSISTENT_BRIDGE_OPS ops;
    LOG log = {0}; size_t index = 64;
    Initialize(records, contexts, bindings, fake, &log);
    TEST_ASSERT_INT_EQ(VmxPersistentOperationRouterInitialize(
        records, contexts, bindings, N, &router), 0, "initialize");
    TEST_ASSERT_TRUE(router.Initialized && router.Count == N &&
        router.Records == records, "published");
    ops = VmxPersistentOperationRouterOperations();
    TEST_ASSERT_INT_EQ(ops.Prepare(
        &router, &contexts[index], &records[index]), 0, "prepare");
    TEST_ASSERT_INT_EQ(ops.Activate(
        &router, &contexts[index], &records[index]), 0, "activate");
    TEST_ASSERT_INT_EQ(ops.Stop(
        &router, &contexts[index], &records[index]), 0, "stop");
    TEST_ASSERT_INT_EQ(ops.Destroy(
        &router, &contexts[index], &records[index]), 0, "destroy");
    TEST_ASSERT_TRUE(log.Count == 4 && log.Events[0] == 164 &&
        log.Events[1] == 264 && log.Events[2] == 364 &&
        log.Events[3] == 464, "per-cpu contexts");
    fake[index][1].Status = VMX_PERSISTENT_STATUS_NOT_READY;
    TEST_ASSERT_INT_EQ(ops.Activate(&router, &contexts[index], &records[index]),
        VMX_PERSISTENT_STATUS_NOT_READY, "status propagated");
    fake[index][2].Mutate = true;
    TEST_ASSERT_INT_EQ(ops.Stop(&router, &contexts[index], &records[index]),
        VMX_PERSISTENT_STATUS_INVARIANT, "identity mutation");
    TEST_ASSERT_TRUE(VmxProcessorIdentityEqual(
        &contexts[index].Identity, &records[index].Identity), "identity restored");
    return 0;
}
static int test_invocation_rejections(void)
{
    VMX_PERSISTENT_CPU_RECORD records[N], forged;
    VMX_PERSISTENT_CONTEXT contexts[N];
    VMX_PERSISTENT_OPERATION_BINDING bindings[N]; FAKE fake[N][4];
    VMX_PERSISTENT_OPERATION_ROUTER router; VMX_PERSISTENT_BRIDGE_OPS ops;
    LOG log = {0};
    Initialize(records, contexts, bindings, fake, &log);
    TEST_ASSERT_INT_EQ(VmxPersistentOperationRouterInitialize(
        records, contexts, bindings, N, &router), 0, "initialize");
    ops = VmxPersistentOperationRouterOperations(); forged = records[0];
    TEST_ASSERT_INT_EQ(ops.Prepare(&router,
        (VMX_PERSISTENT_CONTEXT*)((unsigned char*)contexts + 1), &records[0]),
        VMX_PERSISTENT_STATUS_INVALID, "interior context");
    TEST_ASSERT_INT_EQ(ops.Prepare(&router, &contexts[0], &forged),
        VMX_PERSISTENT_STATUS_INVALID, "foreign record");
    TEST_ASSERT_INT_EQ(ops.Prepare(&router, &contexts[N - 1], &records[0]),
        VMX_PERSISTENT_STATUS_INVALID, "index mismatch");
    TEST_ASSERT_INT_EQ(ops.Prepare(NULL, &contexts[0], &records[0]),
        VMX_PERSISTENT_STATUS_INVALID, "null router");
    records[0].Identity.Reserved = 1;
    TEST_ASSERT_INT_EQ(ops.Prepare(&router, &contexts[0], &records[0]),
        VMX_PERSISTENT_STATUS_INVALID, "reserved record");
    TEST_ASSERT_TRUE(log.Count == 0, "no callbacks");
    records[0].Identity.Reserved = 0;
    router.Bindings[0].Operations.Prepare = NULL;
    TEST_ASSERT_INT_EQ(ops.Prepare(&router, &contexts[0], &records[0]),
        VMX_PERSISTENT_STATUS_INVARIANT, "mutated binding");
    router.Bindings = NULL;
    TEST_ASSERT_INT_EQ(ops.Prepare(&router, &contexts[0], &records[0]),
        VMX_PERSISTENT_STATUS_INVARIANT, "missing binding array");
    return 0;
}
static int test_initialize_rejections(void)
{
    VMX_PERSISTENT_CPU_RECORD records[N]; VMX_PERSISTENT_CONTEXT contexts[N];
    VMX_PERSISTENT_OPERATION_BINDING bindings[N]; FAKE fake[N][4]; LOG log = {0};
    VMX_PERSISTENT_OPERATION_ROUTER out, before;
    size_t huge = SIZE_MAX / sizeof(*contexts) + 1u;
    Initialize(records, contexts, bindings, fake, &log);
    (void)memset(&out, 0xA5, sizeof(out)); before = out;
    bindings[1].Operations.Activate = NULL;
    TEST_ASSERT_INT_EQ(VmxPersistentOperationRouterInitialize(
        records, contexts, bindings, N, &out), VMX_PERSISTENT_STATUS_INVALID, "missing op");
    TEST_ASSERT_TRUE(memcmp(&out, &before, sizeof(out)) == 0, "output preserved");
    bindings[1].Operations.Activate = Fake; bindings[1].StopContext = NULL;
    TEST_ASSERT_INT_EQ(VmxPersistentOperationRouterInitialize(
        records, contexts, bindings, N, &out), VMX_PERSISTENT_STATUS_INVALID, "missing context");
    bindings[1].StopContext = &fake[1][2]; records[1].Identity = records[0].Identity;
    contexts[1].Identity = contexts[0].Identity;
    TEST_ASSERT_INT_EQ(VmxPersistentOperationRouterInitialize(
        records, contexts, bindings, N, &out), VMX_PERSISTENT_STATUS_INVALID, "duplicate");
    records[1].Identity = contexts[1].Identity = Identity(1);
    TEST_ASSERT_INT_EQ(VmxPersistentOperationRouterInitialize(
        records, contexts, bindings, N,
        (VMX_PERSISTENT_OPERATION_ROUTER*)contexts),
        VMX_PERSISTENT_STATUS_INVALID, "overlap");
    TEST_ASSERT_INT_EQ(VmxPersistentOperationRouterInitialize(
        records, contexts, bindings, huge, &out),
        VMX_PERSISTENT_STATUS_INVALID, "overflow");
    TEST_ASSERT_TRUE(memcmp(&out, &before, sizeof(out)) == 0, "all preserve");
    return 0;
}
TEST_MAIN_BEGIN()
    TEST_RUN(test_routing);
    TEST_RUN(test_invocation_rejections);
    TEST_RUN(test_initialize_rejections);
TEST_MAIN_END()
