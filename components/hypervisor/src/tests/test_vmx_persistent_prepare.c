#include "test_common.h"
#include "vmx_persistent_prepare.h"
#include <stdlib.h>

typedef struct _FAKE {
    VMX_PROCESSOR_IDENTITY Current;
    VMX_PERSISTENT_KERNEL_STATUS PinStatus, QueryStatus, RestoreStatus;
    VMX_PERSISTENT_KERNEL_RESULT KernelResult;
    size_t Events[16], EventCount, RegionCount, PoolFrees, RegionFrees;
    bool Passive, SetToken, ClearToken;
} FAKE;

static _Alignas(VMX_REGION_ALIGNMENT) unsigned char g_regions[3][VMX_REGION_MIN_SIZE];

static VMX_PROCESSOR_IDENTITY Identity(void)
{ VMX_PROCESSOR_IDENTITY id = {1, 1, 0, 65}; return id; }
static void Event(FAKE* f, size_t event)
{ f->Events[f->EventCount++] = event; }
static bool Passive(void* c)
{ FAKE* f = c; Event(f, 1); return f->Passive; }
static VMX_PERSISTENT_KERNEL_STATUS Pin(
    void* c, const VMX_PROCESSOR_IDENTITY* target,
    VMX_PERSISTENT_AFFINITY_TOKEN* token)
{
    FAKE* f = c; Event(f, 2);
    if (!VmxProcessorIdentityEqual(target, &f->Current)) return -90;
    if (f->SetToken) { token->PreviousMask = 1; token->Pinned = true; }
    return f->PinStatus;
}
static VMX_PERSISTENT_KERNEL_STATUS Query(void* c, VMX_PROCESSOR_IDENTITY* id)
{ FAKE* f = c; Event(f, 3); *id = f->Current; return f->QueryStatus; }
static VMX_PERSISTENT_KERNEL_STATUS Restore(
    void* c, VMX_PERSISTENT_AFFINITY_TOKEN* token)
{
    FAKE* f = c; Event(f, 4);
    if (f->RestoreStatus == 0 && f->ClearToken) token->Pinned = false;
    return f->RestoreStatus;
}
static void* PoolAlloc(void* c, size_t size)
{ (void)c; return calloc(1, size); }
static void PoolFree(void* c, void* p)
{ FAKE* f = c; f->PoolFrees++; free(p); }
static void* RegionAlloc(void* c, size_t size, uint64_t highest)
{
    FAKE* f = c; (void)highest;
    if (size != VMX_REGION_MIN_SIZE || f->RegionCount >= 3) return NULL;
    return g_regions[f->RegionCount++];
}
static void RegionFree(void* c, void* p)
{ FAKE* f = c; (void)p; f->RegionFrees++; }
static uint64_t Physical(void* c, const void* p)
{
    size_t i;
    (void)c;
    for (i = 0; i < 3; ++i) {
        if (p == g_regions[i]) return UINT64_C(0x200000) + (uint64_t)i * VMX_REGION_MIN_SIZE;
    }
    return 0;
}
static uint32_t Dispatcher(VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* context,
                           VMX_V3D_GUEST_GPRS* gprs, void* dispatcher_context)
{ (void)context; (void)gprs; (void)dispatcher_context; return 0; }

static FAKE ValidFake(void)
{
    FAKE f = {0}; f.Current = Identity();
    f.Passive = f.SetToken = f.ClearToken = true; return f;
}
static VMX_PERSISTENT_KERNEL_PLATFORM Kernel(FAKE* f)
{
    VMX_PERSISTENT_KERNEL_PLATFORM p = {0};
    p.Context = f; p.CheckPassive = Passive; p.Pin = Pin;
    p.QueryCurrent = Query; p.Restore = Restore; return p;
}
static VMX_PERSISTENT_ALLOCATOR Allocator(FAKE* f)
{
    VMX_PERSISTENT_ALLOCATOR a = {
        f, PoolAlloc, PoolFree, RegionAlloc, RegionFree, Physical};
    return a;
}
static uint64_t Basic(void)
{
    return UINT64_C(0x123) |
        ((uint64_t)VMX_REGION_MIN_SIZE << VMX_BASIC_VMCS_SIZE_SHIFT) |
        ((uint64_t)VMX_BASIC_MEMORY_TYPE_WB << VMX_BASIC_MEMORY_TYPE_SHIFT);
}
static int Setup(
    FAKE* f, VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record, VMX_PERSISTENT_ALLOCATOR* allocator,
    size_t launch_size)
{
    VMX_PROCESSOR_IDENTITY id = Identity();
    *allocator = Allocator(f); record->Identity = id;
    return VmxPersistentContextCreate(
        &id, Basic(), launch_size, allocator, context);
}
static VMX_PERSISTENT_PREPARE_PLATFORM PreparePlatform(
    VMX_PERSISTENT_KERNEL_PLATFORM* kernel,
    VMX_PERSISTENT_ALLOCATOR* allocator)
{
    VMX_PERSISTENT_PREPARE_PLATFORM p = {
        kernel, allocator, UINT64_C(0x12345678), Dispatcher, kernel,
        &((FAKE*)kernel->Context)->KernelResult};
    return p;
}

static int test_success_destroy_and_idempotence(void)
{
    FAKE f = ValidFake(); VMX_PERSISTENT_KERNEL_PLATFORM k = Kernel(&f);
    VMX_PERSISTENT_ALLOCATOR a; VMX_PERSISTENT_CONTEXT c = {0};
    VMX_PERSISTENT_CPU_RECORD r = {0}; VMX_PERSISTENT_PREPARE_PLATFORM p;
    TEST_ASSERT_INT_EQ(Setup(&f, &c, &r, &a, VMX_V3D_CONTEXT_SIZE), 0, "setup");
    p = PreparePlatform(&k, &a);
    TEST_ASSERT_INT_EQ(VmxPersistentPrepareContext(&p, &c, &r), 0, "prepare");
    TEST_ASSERT_UINT_EQ(r.OwnedMask, VMX_PERSISTENT_OWNER_CONTEXT, "owner");
    TEST_ASSERT_FALSE(r.Active || r.CleanupRequired, "prepare remains inactive");
    TEST_ASSERT_UINT_EQ(((VMX_V3D_PERSISTENT_LAUNCH_CONTEXT*)c.LaunchStorage)->ProtocolStep,
                        VMX_V3D_PROTOCOL_INITIALIZED, "launch initialized");
    TEST_ASSERT_UINT_EQ(f.EventCount, 4, "prepare pin sequence");
    TEST_ASSERT_TRUE(f.KernelResult.OperationInvoked &&
                     f.KernelResult.AffinityRestored, "prepare diagnostics");
    TEST_ASSERT_INT_EQ(VmxPersistentDestroyPreparedContext(&p, &c, &r), 0, "destroy");
    TEST_ASSERT_UINT_EQ(r.OwnedMask, 0, "owner cleared");
    TEST_ASSERT_UINT_EQ(c.OwnedResources, 0, "resources cleared");
    TEST_ASSERT_UINT_EQ(f.PoolFrees, 3, "pool frees");
    TEST_ASSERT_UINT_EQ(f.RegionFrees, 3, "region frees");
    TEST_ASSERT_UINT_EQ(f.EventCount, 8, "destroy pin sequence");
    TEST_ASSERT_INT_EQ(VmxPersistentDestroyPreparedContext(&p, &c, &r), 0, "idempotent");
    TEST_ASSERT_UINT_EQ(f.EventCount, 8, "idempotent no pin");
    return 0;
}

static int test_prepare_failures_preserve_owner_for_rollback(void)
{
    FAKE f = ValidFake(); VMX_PERSISTENT_KERNEL_PLATFORM k = Kernel(&f);
    VMX_PERSISTENT_ALLOCATOR a; VMX_PERSISTENT_CONTEXT c = {0};
    VMX_PERSISTENT_CPU_RECORD r = {0}; VMX_PERSISTENT_PREPARE_PLATFORM p;
    VMX_PERSISTENT_KERNEL_RESULT failure;
    TEST_ASSERT_INT_EQ(Setup(&f, &c, &r, &a, 64), 0, "small storage setup");
    p = PreparePlatform(&k, &a);
    TEST_ASSERT_INT_EQ(VmxPersistentPrepareContext(&p, &c, &r), VMX_PERSISTENT_STATUS_NOT_READY, "init fails");
    TEST_ASSERT_UINT_EQ(r.OwnedMask, VMX_PERSISTENT_OWNER_CONTEXT, "failure owned");
    TEST_ASSERT_TRUE(c.Published && c.OwnedResources != 0, "resources retained");
    TEST_ASSERT_UINT_EQ(f.EventCount, 4, "failure restored");
    TEST_ASSERT_INT_EQ(f.KernelResult.PrimaryStatus,
                       VMX_PERSISTENT_KERNEL_OPERATION_FAILED, "init detail");
    TEST_ASSERT_INT_EQ(f.KernelResult.OperationStatus,
                       VMX_PERSISTENT_LAUNCH_INIT_STORAGE_INVALID, "init cause");
    failure = f.KernelResult;
    TEST_ASSERT_INT_EQ(VmxPersistentDestroyPreparedContext(&p, &c, &r), 0, "rollback destroy");
    TEST_ASSERT_TRUE(memcmp(&failure, &f.KernelResult, sizeof failure) == 0,
                     "rollback preserves prepare failure");

    f = ValidFake(); k = Kernel(&f); c = (VMX_PERSISTENT_CONTEXT){0}; r = (VMX_PERSISTENT_CPU_RECORD){0};
    TEST_ASSERT_INT_EQ(Setup(&f, &c, &r, &a, VMX_V3D_CONTEXT_SIZE), 0, "passive setup");
    p = PreparePlatform(&k, &a); f.Passive = false;
    TEST_ASSERT_INT_EQ(VmxPersistentPrepareContext(&p, &c, &r), VMX_PERSISTENT_STATUS_NOT_READY, "passive failure");
    TEST_ASSERT_UINT_EQ(r.OwnedMask, VMX_PERSISTENT_OWNER_CONTEXT, "passive owned");
    TEST_ASSERT_UINT_EQ(f.EventCount, 1, "no pin while not passive");
    TEST_ASSERT_INT_EQ(f.KernelResult.FinalStatus,
                       VMX_PERSISTENT_KERNEL_NOT_PASSIVE, "passive detail");
    failure = f.KernelResult; f.Passive = true;
    TEST_ASSERT_INT_EQ(VmxPersistentDestroyPreparedContext(&p, &c, &r), 0, "passive rollback");
    TEST_ASSERT_TRUE(memcmp(&failure, &f.KernelResult, sizeof failure) == 0,
                     "rollback preserves passive failure");
    return 0;
}

static int test_malformed_platform_and_stale_publication(void)
{
    FAKE f = ValidFake(); VMX_PERSISTENT_KERNEL_PLATFORM k = Kernel(&f);
    VMX_PERSISTENT_ALLOCATOR a; VMX_PERSISTENT_CONTEXT c = {0}, before;
    VMX_PERSISTENT_CPU_RECORD r = {0}, record_before;
    VMX_PERSISTENT_PREPARE_PLATFORM p; size_t i;
    TEST_ASSERT_INT_EQ(Setup(&f, &c, &r, &a, VMX_V3D_CONTEXT_SIZE), 0, "setup");
    p = PreparePlatform(&k, &a); before = c; record_before = r;
    for (i = 0; i < 4; ++i) {
        if (i == 0) k.CheckPassive = NULL;
        if (i == 1) k.Pin = NULL;
        if (i == 2) k.QueryCurrent = NULL;
        if (i == 3) k.Restore = NULL;
        TEST_ASSERT_INT_EQ(VmxPersistentPrepareContext(&p, &c, &r),
                           VMX_PERSISTENT_STATUS_INVALID, "missing callback");
        TEST_ASSERT_TRUE(memcmp(&c, &before, sizeof c) == 0 &&
                         memcmp(&r, &record_before, sizeof r) == 0,
                         "malformed preserves");
        k.CheckPassive = Passive; k.Pin = Pin;
        k.QueryCurrent = Query; k.Restore = Restore;
    }
    TEST_ASSERT_UINT_EQ(f.EventCount, 0, "malformed no calls");
    a.FreePool = NULL;
    TEST_ASSERT_INT_EQ(VmxPersistentPrepareContext(&p, &c, &r),
                       VMX_PERSISTENT_STATUS_INVALID, "missing allocator callback");
    TEST_ASSERT_TRUE(memcmp(&c, &before, sizeof c) == 0 &&
                     memcmp(&r, &record_before, sizeof r) == 0, "allocator preserves");
    a.FreePool = PoolFree;
    c.Identity.Reserved = r.Identity.Reserved = 1; before = c; record_before = r;
    TEST_ASSERT_INT_EQ(VmxPersistentPrepareContext(&p, &c, &r),
                       VMX_PERSISTENT_STATUS_INVALID, "bad identity");
    TEST_ASSERT_TRUE(memcmp(&c, &before, sizeof c) == 0 &&
                     memcmp(&r, &record_before, sizeof r) == 0, "identity preserves");
    c.Identity = r.Identity = Identity();
    TEST_ASSERT_INT_EQ(VmxPersistentContextDestroy(&a, &c), 0, "cleanup");
    c.Published = true;
    TEST_ASSERT_INT_EQ(VmxPersistentDestroyPreparedContext(&p, &c, &r),
                       VMX_PERSISTENT_STATUS_INVARIANT, "stale published");
    TEST_ASSERT_TRUE(c.Published && r.OwnedMask == 0, "stale preserved");
    return 0;
}
static int test_restore_destroy_retry_and_invalid(void)
{
    FAKE f = ValidFake(); VMX_PERSISTENT_KERNEL_PLATFORM k = Kernel(&f);
    VMX_PERSISTENT_ALLOCATOR a; VMX_PERSISTENT_CONTEXT c = {0}, before;
    VMX_PERSISTENT_CPU_RECORD r = {0}; VMX_PERSISTENT_PREPARE_PLATFORM p;
    TEST_ASSERT_INT_EQ(Setup(&f, &c, &r, &a, VMX_V3D_CONTEXT_SIZE), 0, "setup");
    p = PreparePlatform(&k, &a); before = c; r.Identity.Number = 2;
    TEST_ASSERT_INT_EQ(VmxPersistentPrepareContext(&p, &c, &r), VMX_PERSISTENT_STATUS_INVALID, "identity invalid");
    TEST_ASSERT_TRUE(memcmp(&c, &before, sizeof c) == 0 && f.EventCount == 0, "invalid preserves");
    r.Identity = Identity(); f.RestoreStatus = -9;
    TEST_ASSERT_INT_EQ(VmxPersistentPrepareContext(&p, &c, &r), VMX_PERSISTENT_STATUS_NOT_READY, "restore failure");
    TEST_ASSERT_UINT_EQ(r.OwnedMask, VMX_PERSISTENT_OWNER_CONTEXT, "restore owner");
    TEST_ASSERT_INT_EQ(f.KernelResult.FinalStatus,
                       VMX_PERSISTENT_KERNEL_RESTORE_FAILED, "restore final");
    TEST_ASSERT_INT_EQ(f.KernelResult.PrimaryStatus,
                       VMX_PERSISTENT_KERNEL_OK, "restore primary");
    f.RestoreStatus = 0; c.CpuState.VmxActive = true;
    TEST_ASSERT_INT_EQ(VmxPersistentDestroyPreparedContext(&p, &c, &r), VMX_PERSISTENT_STATUS_NOT_READY, "busy destroy");
    TEST_ASSERT_UINT_EQ(r.OwnedMask, VMX_PERSISTENT_OWNER_CONTEXT, "busy owner retained");
    c.CpuState.VmxActive = false;
    TEST_ASSERT_INT_EQ(VmxPersistentDestroyPreparedContext(&p, &c, &r), 0, "destroy retry");
    TEST_ASSERT_UINT_EQ(r.OwnedMask, 0, "retry clears owner");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_success_destroy_and_idempotence);
    TEST_RUN(test_prepare_failures_preserve_owner_for_rollback);
    TEST_RUN(test_malformed_platform_and_stale_publication);
    TEST_RUN(test_restore_destroy_retry_and_invalid);
TEST_MAIN_END()
