#include "test_common.h"
#include "vmx_persistent_kernel.h"
#include "vmx_persistent_guarded_platform.h"
#include <stdlib.h>
#include <string.h>

typedef struct _FAKE {
    VMX_PROCESSOR_IDENTITY Identities[65];
    VMX_PROCESSOR_IDENTITY Current;
    VMX_PROCESSOR_IDENTITY SeenTarget;
    size_t Count, Allocations, Frees, Events[8], EventCount;
    VMX_PERSISTENT_KERNEL_STATUS PinStatus, QueryStatus, RestoreStatus;
    uint64_t PinPreviousMask;
    int32_t OperationStatus;
    bool Passive, PinSetsToken, RestoreClears, UseAllocationResult;
    void* AllocationResult;
} FAKE;

static VMX_PROCESSOR_IDENTITY Id(uint32_t index)
{
    VMX_PROCESSOR_IDENTITY value = {
        (uint16_t)(index / 64u), (uint8_t)(index % 64u), 0, index};
    return value;
}
static void Event(FAKE* fake, size_t value) { fake->Events[fake->EventCount++] = value; }
static bool QueryCount(void* context, size_t* count)
{ FAKE* f = (FAKE*)context; *count = f->Count; return true; }
static bool Map(void* context, uint32_t index, VMX_PROCESSOR_IDENTITY* identity)
{ FAKE* f = (FAKE*)context; if (index >= f->Count) return false; *identity = f->Identities[index]; return true; }
static void* Allocate(void* context, size_t size)
{ FAKE* f = (FAKE*)context; f->Allocations++; return f->UseAllocationResult ? f->AllocationResult : malloc(size); }
static void Free(void* context, void* memory)
{ FAKE* f = (FAKE*)context; f->Frees++; if (!f->UseAllocationResult) free(memory); }
static bool Passive(void* context)
{ FAKE* f = (FAKE*)context; Event(f, 1); return f->Passive; }
static VMX_PERSISTENT_KERNEL_STATUS Pin(
    void* context, const VMX_PROCESSOR_IDENTITY* target,
    VMX_PERSISTENT_AFFINITY_TOKEN* token)
{
    FAKE* f = (FAKE*)context; Event(f, 2); f->SeenTarget = *target;
    if (f->PinSetsToken) {
        token->PreviousGroup = 7;
        token->PreviousMask = f->PinPreviousMask;
        token->PreviousReserved[0] = 0x11;
        token->PreviousReserved[1] = 0x22;
        token->PreviousReserved[2] = 0x33;
        token->Pinned = true;
    }
    return f->PinStatus;
}
static VMX_PERSISTENT_KERNEL_STATUS Query(
    void* context, VMX_PROCESSOR_IDENTITY* current)
{ FAKE* f = (FAKE*)context; Event(f, 3); *current = f->Current; return f->QueryStatus; }
static VMX_PERSISTENT_KERNEL_STATUS Restore(
    void* context, VMX_PERSISTENT_AFFINITY_TOKEN* token)
{
    FAKE* f = (FAKE*)context; Event(f, 5);
    if (f->RestoreStatus == 0 && f->RestoreClears) token->Pinned = false;
    return f->RestoreStatus;
}
static int32_t Operation(void* context, const VMX_PROCESSOR_IDENTITY* target)
{ FAKE* f = (FAKE*)context; Event(f, 4); f->SeenTarget = *target; return f->OperationStatus; }
static VMX_PERSISTENT_KERNEL_PLATFORM Platform(FAKE* fake)
{
    VMX_PERSISTENT_KERNEL_PLATFORM p = {
        fake, QueryCount, Map, Allocate, Free, Passive, Pin, Query, Restore};
    return p;
}
static FAKE ValidFake(void)
{
    FAKE f = {0}; size_t i; f.Count = 65; for (i = 0; i < f.Count; ++i) f.Identities[i] = Id((uint32_t)i);
    f.Current = f.Identities[64]; f.PinPreviousMask = 0x80;
    f.Passive = f.PinSetsToken = f.RestoreClears = true; return f;
}
static void AssertEvents(const FAKE* f, const size_t* expected, size_t count)
{
    size_t i; TEST_ASSERT_UINT_EQ(f->EventCount, count, "event count");
    for (i = 0; i < count; ++i) TEST_ASSERT_UINT_EQ(f->Events[i], expected[i], "event order");
}

static int test_topology_provider(void)
{
    FAKE f = ValidFake(); VMX_PERSISTENT_KERNEL_PLATFORM p = Platform(&f);
    VMX_PROCESSOR_TOPOLOGY_PROVIDER provider, before; VMX_PROCESSOR_TOPOLOGY_SNAPSHOT snapshot = {0};
    memset(&provider, 0xA5, sizeof provider); before = provider;
    TEST_ASSERT_INT_EQ(VmxPersistentKernelTopologyProvider(&p, &provider), 0, "provider");
    TEST_ASSERT_TRUE(VmxProcessorTopologySnapshotCreate(&provider, &snapshot), "snapshot");
    TEST_ASSERT_UINT_EQ(snapshot.Count, 65, "count"); TEST_ASSERT_UINT_EQ(snapshot.Processors[63].Number, 63, "cpu63");
    TEST_ASSERT_UINT_EQ(snapshot.Processors[64].Group, 1, "group1"); TEST_ASSERT_UINT_EQ(snapshot.Processors[64].SystemIndex, 64, "index64");
    VmxProcessorTopologySnapshotDestroy(&snapshot);
    TEST_ASSERT_UINT_EQ(f.Allocations, 1, "allocation"); TEST_ASSERT_UINT_EQ(f.Frees, 1, "free");
    p.MapSystemIndex = NULL;
    TEST_ASSERT_INT_EQ(VmxPersistentKernelTopologyProvider(&p, &provider), VMX_PERSISTENT_KERNEL_INVALID, "missing map");
    TEST_ASSERT_TRUE(memcmp(&provider, &before, sizeof provider) != 0, "valid provider changed");
    memset(&provider, 0xA5, sizeof provider); before = provider;
    TEST_ASSERT_INT_EQ(VmxPersistentKernelTopologyProvider(&p, &provider), VMX_PERSISTENT_KERNEL_INVALID, "invalid provider");
    TEST_ASSERT_TRUE(memcmp(&provider, &before, sizeof provider) == 0, "invalid preserves");
    return 0;
}

static int test_success_and_primary_failures(void)
{
    const size_t success_events[] = {1, 2, 3, 4, 5}, verify_events[] = {1, 2, 3, 5};
    FAKE f = ValidFake(); VMX_PERSISTENT_KERNEL_PLATFORM p = Platform(&f);
    VMX_PERSISTENT_KERNEL_RESULT r; VMX_PROCESSOR_IDENTITY target = Id(64);
    TEST_ASSERT_INT_EQ(VmxPersistentKernelExecutePinned(&p, &target, &f, Operation, &r), 0, "success");
    AssertEvents(&f, success_events, 5); TEST_ASSERT_TRUE(r.PinSucceeded && r.IdentityVerified && r.OperationInvoked && r.AffinityRestored, "success flags");
    TEST_ASSERT_TRUE(VmxProcessorIdentityEqual(&f.SeenTarget, &target), "target forwarded");

    f = ValidFake(); p = Platform(&f); f.PinPreviousMask = 0;
    TEST_ASSERT_INT_EQ(VmxPersistentKernelExecutePinned(
        &p, &target, &f, Operation, &r), 0, "opaque zero previous mask");
    AssertEvents(&f, success_events, 5);
    TEST_ASSERT_TRUE(r.PinSucceeded && r.OperationInvoked &&
                     r.AffinityRestored, "zero-mask affinity restored");

    f = ValidFake(); p = Platform(&f); f.QueryStatus = -99;
    TEST_ASSERT_INT_EQ(VmxPersistentKernelExecutePinned(&p, &target, &f, Operation, &r), VMX_PERSISTENT_KERNEL_VERIFY_FAILED, "query failure");
    AssertEvents(&f, verify_events, 4); TEST_ASSERT_TRUE(r.AffinityRestored && !r.OperationInvoked, "query cleanup");

    f = ValidFake(); p = Platform(&f); f.Current = Id(63);
    TEST_ASSERT_INT_EQ(VmxPersistentKernelExecutePinned(&p, &target, &f, Operation, &r), VMX_PERSISTENT_KERNEL_IDENTITY_MISMATCH, "identity mismatch");
    AssertEvents(&f, verify_events, 4);

    f = ValidFake(); p = Platform(&f); f.OperationStatus = 77;
    TEST_ASSERT_INT_EQ(VmxPersistentKernelExecutePinned(&p, &target, &f, Operation, &r), VMX_PERSISTENT_KERNEL_OPERATION_FAILED, "operation failure");
    TEST_ASSERT_INT_EQ(r.OperationStatus, 77, "operation detail"); AssertEvents(&f, success_events, 5);
    return 0;
}

static int test_pin_passive_and_restore_failures(void)
{
    const size_t passive_events[] = {1}, pin_events[] = {1, 2}, invariant_events[] = {1, 2, 5};
    const size_t success_events[] = {1, 2, 3, 4, 5};
    FAKE f = ValidFake(); VMX_PERSISTENT_KERNEL_PLATFORM p = Platform(&f);
    VMX_PERSISTENT_KERNEL_RESULT r; VMX_PROCESSOR_IDENTITY target = Id(64);

    f.Passive = false;
    TEST_ASSERT_INT_EQ(VmxPersistentKernelExecutePinned(&p, &target, &f, Operation, &r), VMX_PERSISTENT_KERNEL_NOT_PASSIVE, "passive gate");
    AssertEvents(&f, passive_events, 1);

    f = ValidFake(); p = Platform(&f); f.PinStatus = -42; f.PinSetsToken = false;
    TEST_ASSERT_INT_EQ(VmxPersistentKernelExecutePinned(&p, &target, &f, Operation, &r), VMX_PERSISTENT_KERNEL_PIN_FAILED, "pin failure");
    AssertEvents(&f, pin_events, 2); TEST_ASSERT_TRUE(!r.AffinityRestored, "pin no restore");
    f = ValidFake(); p = Platform(&f); f.PinStatus = -42;
    TEST_ASSERT_INT_EQ(VmxPersistentKernelExecutePinned(&p, &target, &f, Operation, &r), VMX_PERSISTENT_KERNEL_PIN_FAILED, "partial pin failure");
    AssertEvents(&f, invariant_events, 3); TEST_ASSERT_TRUE(!r.PinSucceeded && r.AffinityRestored, "partial pin restored");
    f = ValidFake(); p = Platform(&f); f.PinStatus = -42; f.RestoreStatus = -55;
    TEST_ASSERT_INT_EQ(VmxPersistentKernelExecutePinned(&p, &target, &f, Operation, &r), VMX_PERSISTENT_KERNEL_RESTORE_FAILED, "partial pin restore failure");
    TEST_ASSERT_INT_EQ(r.PrimaryStatus, VMX_PERSISTENT_KERNEL_PIN_FAILED, "pin primary preserved"); TEST_ASSERT_INT_EQ(r.RestoreStatus, -55, "pin restore detail");

    f = ValidFake(); p = Platform(&f); f.PinSetsToken = false;
    TEST_ASSERT_INT_EQ(VmxPersistentKernelExecutePinned(&p, &target, &f, Operation, &r), VMX_PERSISTENT_KERNEL_INVARIANT, "pin invariant");
    AssertEvents(&f, invariant_events, 3); TEST_ASSERT_TRUE(r.AffinityRestored && !r.OperationInvoked, "invariant restored");

    f = ValidFake(); p = Platform(&f); f.OperationStatus = 9; f.RestoreStatus = -55;
    TEST_ASSERT_INT_EQ(VmxPersistentKernelExecutePinned(&p, &target, &f, Operation, &r), VMX_PERSISTENT_KERNEL_RESTORE_FAILED, "restore overrides");
    TEST_ASSERT_INT_EQ(r.PrimaryStatus, VMX_PERSISTENT_KERNEL_OPERATION_FAILED, "primary preserved");
    TEST_ASSERT_INT_EQ(r.RestoreStatus, -55, "restore detail"); AssertEvents(&f, success_events, 5);

    f = ValidFake(); p = Platform(&f); f.RestoreClears = false;
    TEST_ASSERT_INT_EQ(VmxPersistentKernelExecutePinned(&p, &target, &f, Operation, &r), VMX_PERSISTENT_KERNEL_RESTORE_FAILED, "stale token");
    TEST_ASSERT_INT_EQ(r.RestoreStatus, VMX_PERSISTENT_KERNEL_INVARIANT, "stale detail");
    return 0;
}

static int test_invalid_preserves_output(void)
{
    FAKE f = ValidFake(); VMX_PERSISTENT_KERNEL_PLATFORM p = Platform(&f);
    VMX_PERSISTENT_KERNEL_RESULT r, before; VMX_PROCESSOR_IDENTITY target = Id(64);
    memset(&r, 0xA5, sizeof r); before = r; target.Number = 64;
    TEST_ASSERT_INT_EQ(VmxPersistentKernelExecutePinned(&p, &target, &f, Operation, &r), VMX_PERSISTENT_KERNEL_INVALID, "bad target");
    TEST_ASSERT_TRUE(memcmp(&r, &before, sizeof r) == 0, "bad target preserves");
    target = Id(64); p.Restore = NULL;
    TEST_ASSERT_INT_EQ(VmxPersistentKernelExecutePinned(&p, &target, &f, Operation, &r), VMX_PERSISTENT_KERNEL_INVALID, "missing callback");
    TEST_ASSERT_TRUE(memcmp(&r, &before, sizeof r) == 0 && f.EventCount == 0, "invalid no calls");
    TEST_ASSERT_INT_EQ(VmxPersistentKernelExecutePinned(NULL, &target, &f, Operation, &r), VMX_PERSISTENT_KERNEL_INVALID, "null platform");
    TEST_ASSERT_INT_EQ(VmxPersistentKernelExecutePinned(&p, &target, &f, Operation, NULL), VMX_PERSISTENT_KERNEL_INVALID, "null output");
    return 0;
}

static int test_guarded_platform(void)
{
    struct { VMX_PERSISTENT_GUARDED_PLATFORM Guard; unsigned char Sibling[64]; } owner;
    FAKE f = ValidFake(); VMX_PERSISTENT_KERNEL_PLATFORM source = Platform(&f), bad;
    VMX_PERSISTENT_KERNEL_PLATFORM* p; VMX_PROCESSOR_IDENTITY id = {0};
    VMX_PERSISTENT_AFFINITY_TOKEN token = {0}; size_t count = 0; void* memory;
    VMX_PERSISTENT_GUARDED_PLATFORM before; memset(&owner, 0, sizeof owner);
    TEST_ASSERT_INT_EQ(VmxPersistentGuardedPlatformInitialize(
        &source, &owner, sizeof owner, &owner.Guard), 0, "guard initialize");
    p = &owner.Guard.Platform; source.QueryActiveCount = NULL;
    TEST_ASSERT_TRUE(p->QueryActiveCount(p->Context, &count) && count == 65,
        "copied count delegate"); TEST_ASSERT_TRUE(p->MapSystemIndex(p->Context, 64, &id) &&
        id.Group == 1 && id.Number == 0, "map delegate");
    TEST_ASSERT_TRUE(p->CheckPassive(p->Context), "passive delegate");
    TEST_ASSERT_INT_EQ(p->Pin(p->Context, &id, &token), 0, "pin delegate");
    TEST_ASSERT_INT_EQ(p->QueryCurrent(p->Context, &id), 0, "current delegate");
    TEST_ASSERT_INT_EQ(p->Restore(p->Context, &token), 0, "restore delegate");
    memory = p->Allocate(p->Context, 32); TEST_ASSERT_TRUE(memory != NULL, "nonoverlap allocate");
    p->Free(p->Context, memory); TEST_ASSERT_TRUE(f.Allocations == 1 && f.Frees == 1, "exact free");
    f.UseAllocationResult = true; f.AllocationResult = owner.Sibling;
    TEST_ASSERT_TRUE(p->Allocate(p->Context, sizeof owner.Sibling) == NULL &&
        f.Allocations == 2 && f.Frees == 2, "parent overlap rejected and freed");
    before = owner.Guard; bad = Platform(&f);
    TEST_ASSERT_INT_EQ(VmxPersistentGuardedPlatformInitialize(
        &bad, &owner, sizeof owner, &owner.Guard),
        VMX_PERSISTENT_GUARDED_PLATFORM_BUSY, "reinit busy");
    TEST_ASSERT_TRUE(memcmp(&before, &owner.Guard, sizeof before) == 0, "busy preserves");
    owner.Guard.Source.Free = NULL; count = 77;
    TEST_ASSERT_TRUE(!p->QueryActiveCount(p->Context, &count) && count == 77,
        "source mutation fails closed");
#define CHECK_GUARD_CALLBACK(field) owner.Guard=before;owner.Guard.Platform.field=NULL;count=99;TEST_ASSERT_TRUE(!p->QueryActiveCount(p->Context,&count)&&count==99,"callback mutation " #field)
    owner.Guard=before;owner.Guard.Platform.QueryActiveCount=NULL;id=Id(7);
    TEST_ASSERT_TRUE(!p->MapSystemIndex(p->Context,0,&id)&&id.SystemIndex==7,"callback mutation QueryActiveCount");
    CHECK_GUARD_CALLBACK(MapSystemIndex);
    CHECK_GUARD_CALLBACK(Allocate); CHECK_GUARD_CALLBACK(Free);
    CHECK_GUARD_CALLBACK(CheckPassive); CHECK_GUARD_CALLBACK(Pin);
    CHECK_GUARD_CALLBACK(QueryCurrent); CHECK_GUARD_CALLBACK(Restore);
#undef CHECK_GUARD_CALLBACK
#define CHECK_GUARD_OWNER(code) owner.Guard=before;code;count=98;TEST_ASSERT_TRUE(!p->QueryActiveCount(p->Context,&count)&&count==98,"metadata mutation " #code)
    CHECK_GUARD_OWNER(owner.Guard.Platform.Context=NULL);
    CHECK_GUARD_OWNER(owner.Guard.SourceContract.Free=NULL);
    CHECK_GUARD_OWNER(owner.Guard.Source.Context=NULL);
    CHECK_GUARD_OWNER(owner.Guard.SourceContract.Context=NULL);
    CHECK_GUARD_OWNER(owner.Guard.ForbiddenBegin++);
    CHECK_GUARD_OWNER(owner.Guard.ForbiddenEnd++);
    CHECK_GUARD_OWNER(owner.Guard.ForbiddenBeginContract--);
    CHECK_GUARD_OWNER(owner.Guard.ForbiddenEndContract--);
    CHECK_GUARD_OWNER(owner.Guard.Magic=0); CHECK_GUARD_OWNER(owner.Guard.Version=0);
#undef CHECK_GUARD_OWNER
    owner.Guard = before; owner.Guard.PlatformContract.MapSystemIndex = Map; count = 88;
    TEST_ASSERT_TRUE(!p->QueryActiveCount(p->Context, &count) && count == 88,
        "contract callback mutation preserves output");
    owner.Guard = before; owner.Guard.Platform.MapSystemIndex = NULL; count = 87;
    TEST_ASSERT_TRUE(!p->QueryActiveCount(p->Context, &count) && count == 87,
        "published mutation preserves output");
    memset(&owner.Guard, 0xA5, sizeof owner.Guard); before = owner.Guard;
    bad = Platform(&f); bad.Free = NULL;
    TEST_ASSERT_TRUE(VmxPersistentGuardedPlatformInitialize(&bad, &owner, sizeof owner,
        &owner.Guard) == VMX_PERSISTENT_KERNEL_INVALID &&
        memcmp(&before, &owner.Guard, sizeof before) == 0, "invalid preserves");
    return 0;
}
TEST_MAIN_BEGIN()
    TEST_RUN(test_topology_provider);
    TEST_RUN(test_success_and_primary_failures);
    TEST_RUN(test_pin_passive_and_restore_failures);
    TEST_RUN(test_invalid_preserves_output);
    TEST_RUN(test_guarded_platform);
TEST_MAIN_END()
