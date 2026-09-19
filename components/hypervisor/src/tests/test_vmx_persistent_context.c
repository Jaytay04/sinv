#include "test_common.h"
#include "vmx_msr_bitmap_policy.h"
#include "vmx_persistent_context.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FAKE_MAX_REGIONS 3u
#define FAKE_REGION_BYTES (4u * VMX_REGION_MIN_SIZE)
#define FAKE_MAX_EVENTS 10u
static _Alignas(VMX_REGION_ALIGNMENT) unsigned char g_regions[40][FAKE_MAX_REGIONS][FAKE_REGION_BYTES];
typedef enum _FAKE_PHYSICAL_MODE {
    PHYS_VALID, PHYS_VMXON_MISALIGNED, PHYS_VMXON_RANGE,
    PHYS_VMXON_EXTENT, PHYS_VMCS_MISALIGNED,
    PHYS_BITMAP_MISALIGNED, PHYS_BITMAP_RANGE,
    PHYS_PA_ALIAS
} FAKE_PHYSICAL_MODE;
typedef struct _FAKE_ALLOCATOR {
    size_t Slot, Calls, RegionCount, PoolCount, AllocCount, FreeCount, PhysicalCalls;
    size_t FailAt, AllocOrder[FAKE_MAX_EVENTS], FreeOrder[FAKE_MAX_EVENTS];
    size_t PoolFreeCount, ContiguousFreeCount, CrossAliasStage, CrossAliasPool;
    size_t RequestedSize[FAKE_MAX_REGIONS];
    uint64_t RequestedHighest[FAKE_MAX_REGIONS], PhysicalSeen[FAKE_MAX_REGIONS];
    void* Pools[3];
    void* FreedPools[3]; size_t FreedPoolCount;
    size_t PoolSize[3];
    size_t GuestAlias;
    bool VaAlias;
    FAKE_PHYSICAL_MODE PhysicalMode;
} FAKE_ALLOCATOR;
static void* PoolAlloc(void* context, size_t size)
{
    FAKE_ALLOCATOR* f = (FAKE_ALLOCATOR*)context;
    void* memory;
    if (++f->Calls == f->FailAt) return NULL;
    memory = f->GuestAlias != 0 && f->PoolCount == 2 ? f->Pools[f->GuestAlias - 1] : calloc(1, size);
    f->Pools[f->PoolCount] = memory;
    f->PoolSize[f->PoolCount] = size;
    f->AllocOrder[f->AllocCount++] = ++f->PoolCount;
    return memory;
}
static void PoolFree(void* context, void* memory)
{
    FAKE_ALLOCATOR* f = (FAKE_ALLOCATOR*)context;
    size_t i; f->PoolFreeCount++; f->FreeOrder[f->FreeCount++] = memory == f->Pools[0] ? 1u : memory == f->Pools[1] ? 2u : 3u;
    for (i = 0; i < f->FreedPoolCount; ++i) if (f->FreedPools[i] == memory) return;
    f->FreedPools[f->FreedPoolCount++] = memory; free(memory);
}
static void* ContiguousAlloc(void* context, size_t size, uint64_t highest)
{
    FAKE_ALLOCATOR* f = (FAKE_ALLOCATOR*)context;
    size_t index = f->RegionCount;
    if (++f->Calls == f->FailAt || size < VMX_REGION_MIN_SIZE ||
        size > FAKE_REGION_BYTES || index >= FAKE_MAX_REGIONS) return NULL;
    f->RequestedSize[index] = size;
    f->RequestedHighest[index] = highest;
    f->AllocOrder[f->AllocCount++] = index + 4u;
    f->RegionCount++;
    if (f->CrossAliasStage == index + 1u) return f->Pools[f->CrossAliasPool - 1u];
    (void)memset(g_regions[f->Slot][f->VaAlias ? 0u : index], 0xA5, size);
    return g_regions[f->Slot][f->VaAlias ? 0u : index];
}
static void ContiguousFree(void* context, void* memory)
{
    FAKE_ALLOCATOR* f = (FAKE_ALLOCATOR*)context;
    (void)memory;
    f->ContiguousFreeCount++; f->FreeOrder[f->FreeCount++] = f->RegionCount - f->ContiguousFreeCount + 4u;
}
static uint64_t Physical(void* context, const void* memory)
{
    FAKE_ALLOCATOR* f = (FAKE_ALLOCATOR*)context;
    size_t i;
    uint64_t value = 0;
    f->PhysicalCalls++;
    for (i = 0; i < f->RegionCount; ++i)
        if (memory == g_regions[f->Slot][f->VaAlias ? 0u : i]) break;
    if (i >= f->RegionCount) return 0;
    value = UINT64_C(0x200000) + (f->VaAlias ? f->PhysicalCalls - 1 : i) * VMX_REGION_ALIGNMENT;
    if (f->PhysicalMode == PHYS_VMXON_MISALIGNED && i == 0) value++;
    if (f->PhysicalMode == PHYS_VMXON_RANGE && i == 0) value = UINT64_C(0x100000000);
    if (f->PhysicalMode == PHYS_VMXON_EXTENT && i == 0) value = UINT64_MAX - 4095u;
    if (f->PhysicalMode == PHYS_VMCS_MISALIGNED && i == 1) value++;
    if (f->PhysicalMode == PHYS_BITMAP_MISALIGNED && i == 2) value++;
    if (f->PhysicalMode == PHYS_BITMAP_RANGE && i == 2)
        value = UINT64_C(0x100000000);
    if (f->PhysicalMode == PHYS_PA_ALIAS) value = UINT64_C(0x200000);
    f->PhysicalSeen[i] = value;
    return value;
}
static VMX_PERSISTENT_ALLOCATOR MakeAllocator(FAKE_ALLOCATOR* fake)
{
    VMX_PERSISTENT_ALLOCATOR allocator = {
        fake, PoolAlloc, PoolFree, ContiguousAlloc, ContiguousFree, Physical
    };
    return allocator;
}
static uint64_t BasicSize(bool restricts_to_32bit, uint32_t region_size)
{
    uint64_t basic = UINT64_C(0x123) | ((uint64_t)region_size << VMX_BASIC_VMCS_SIZE_SHIFT) |
        ((uint64_t)VMX_BASIC_MEMORY_TYPE_WB << VMX_BASIC_MEMORY_TYPE_SHIFT);
    return restricts_to_32bit ? basic | VMX_BASIC_PHYS_ADDR_32 : basic;
}
static uint64_t Basic(bool restricts_to_32bit)
{
    return BasicSize(restricts_to_32bit, VMX_REGION_MIN_SIZE);
}
static VMX_PROCESSOR_IDENTITY Identity(uint32_t index)
{
    VMX_PROCESSOR_IDENTITY identity = {
        (uint16_t)(index / VMX_PROCESSOR_GROUP_MAX_NUMBER),
        (uint8_t)(index % VMX_PROCESSOR_GROUP_MAX_NUMBER), 0, index
    };
    return identity;
}
static void AssertUnchanged(const VMX_PERSISTENT_CONTEXT* context,
                            const VMX_PERSISTENT_CONTEXT* before,
                            const FAKE_ALLOCATOR* fake)
{
    TEST_ASSERT_TRUE(memcmp(context, before, sizeof(*context)) == 0,
                     "context preserved");
    TEST_ASSERT_UINT_EQ(fake->Calls, 0, "no allocation calls");
    TEST_ASSERT_UINT_EQ(fake->PhysicalCalls, 0, "no physical calls");
}
static int test_invalid_inputs_and_callbacks(void)
{
    FAKE_ALLOCATOR fake = {0};
    VMX_PERSISTENT_ALLOCATOR allocator = MakeAllocator(&fake);
    VMX_PERSISTENT_CONTEXT context, before;
    VMX_PROCESSOR_IDENTITY identity = Identity(2);
    VMX_PERSISTENT_CONTEXT_STATUS status;
    size_t i;
    memset(&context, 0xA5, sizeof(context));
    before = context;
    status = VmxPersistentContextCreate(NULL, Basic(false), 32, &allocator, &context);
    TEST_ASSERT_INT_EQ(status, VMX_PERSISTENT_CONTEXT_INVALID, "null identity");
    AssertUnchanged(&context, &before, &fake);
    status = VmxPersistentContextCreate(&identity, Basic(false), 0, &allocator, &context);
    TEST_ASSERT_INT_EQ(status, VMX_PERSISTENT_CONTEXT_INVALID, "zero launch size");
    AssertUnchanged(&context, &before, &fake);
    status = VmxPersistentContextCreate(&identity, Basic(false), 32, &allocator, NULL);
    TEST_ASSERT_INT_EQ(status, VMX_PERSISTENT_CONTEXT_INVALID, "null output");
    AssertUnchanged(&context, &before, &fake);
    status = VmxPersistentContextCreate(&identity, Basic(false) | VMX_BASIC_RESERVED_BIT31,
                                        32, &allocator, &context);
    TEST_ASSERT_INT_EQ(status, VMX_PERSISTENT_CONTEXT_INVALID_CAPABILITIES, "invalid basic");
    AssertUnchanged(&context, &before, &fake);
    status = VmxPersistentContextCreate(&identity, Basic(false), 32, NULL, &context);
    TEST_ASSERT_INT_EQ(status, VMX_PERSISTENT_CONTEXT_INVALID_ALLOCATOR, "null allocator");
    AssertUnchanged(&context, &before, &fake);
    for (i = 0; i < 5; ++i) {
        fake = (FAKE_ALLOCATOR){0}; allocator = MakeAllocator(&fake);
        if (i == 0) allocator.AllocatePool = NULL;
        if (i == 1) allocator.FreePool = NULL;
        if (i == 2) allocator.AllocateContiguous = NULL;
        if (i == 3) allocator.FreeContiguous = NULL;
        if (i == 4) allocator.GetPhysicalAddress = NULL;
        memset(&context, 0xA5, sizeof(context)); before = context;
        status = VmxPersistentContextCreate(&identity, Basic(false), 32, &allocator, &context);
        TEST_ASSERT_INT_EQ(status, VMX_PERSISTENT_CONTEXT_INVALID_ALLOCATOR, "missing callback");
        AssertUnchanged(&context, &before, &fake);
    }
    return 0;
}
static int test_allocation_boundaries(void)
{
    size_t fail, i;
    for (fail = 1; fail <= 6; ++fail) {
        FAKE_ALLOCATOR fake = {0};
        VMX_PERSISTENT_ALLOCATOR allocator = MakeAllocator(&fake);
        VMX_PERSISTENT_CONTEXT context, before;
        VMX_PROCESSOR_IDENTITY identity = Identity((uint32_t)fail);
        memset(&context, 0xA5, sizeof(context)); before = context; fake.FailAt = fail;
        TEST_ASSERT_INT_EQ(VmxPersistentContextCreate(&identity, Basic(false), 32,
            &allocator, &context), VMX_PERSISTENT_CONTEXT_ALLOCATION_FAILED, "allocation boundary");
        TEST_ASSERT_TRUE(memcmp(&context, &before, sizeof(context)) == 0, "boundary output preserved");
        TEST_ASSERT_UINT_EQ(fake.Calls, fail, "boundary allocation count");
        TEST_ASSERT_UINT_EQ(fake.AllocCount, fail - 1, "boundary successful allocations");
        TEST_ASSERT_UINT_EQ(fake.FreeCount, fail - 1, "boundary free count");
        for (i = 0; i < fail - 1; ++i) TEST_ASSERT_UINT_EQ(fake.AllocOrder[i], i + 1, "allocation order");
        for (i = 0; i < fail - 1; ++i) TEST_ASSERT_UINT_EQ(fake.FreeOrder[i], fail - 1 - i, "unwind order");
    }
    return 0;
}
static int ExpectRegionFailure(FAKE_PHYSICAL_MODE mode, bool addr32,
                               size_t frees, const size_t* order)
{
    FAKE_ALLOCATOR fake = {0};
    VMX_PERSISTENT_ALLOCATOR allocator = MakeAllocator(&fake);
    VMX_PERSISTENT_CONTEXT context, before;
    VMX_PROCESSOR_IDENTITY identity = Identity(7);
    size_t i;
    fake.PhysicalMode = mode;
    memset(&context, 0xA5, sizeof(context)); before = context;
    TEST_ASSERT_INT_EQ(VmxPersistentContextCreate(&identity, mode == PHYS_VMXON_EXTENT ? BasicSize(true, 8191) : Basic(addr32), 32,
        &allocator, &context), VMX_PERSISTENT_CONTEXT_REGION_INVALID, "region rejection");
    TEST_ASSERT_TRUE(memcmp(&context, &before, sizeof(context)) == 0, "region output preserved");
    TEST_ASSERT_UINT_EQ(fake.FreeCount, frees, "region free count");
    for (i = 0; i < frees; ++i) TEST_ASSERT_UINT_EQ(fake.FreeOrder[i], order[i], "region free order");
    return 0;
}
static int test_physical_boundaries_and_aliases(void)
{
    const size_t vmxon_unwind[] = {4, 3, 2, 1};
    const size_t vmcs_unwind[] = {5, 4, 3, 2, 1};
    const size_t bitmap_unwind[] = {6, 5, 4, 3, 2, 1};
    FAKE_ALLOCATOR fake = {0};
    VMX_PERSISTENT_ALLOCATOR allocator = MakeAllocator(&fake);
    VMX_PERSISTENT_CONTEXT context;
    VMX_PROCESSOR_IDENTITY identity = Identity(8);
    TEST_ASSERT_INT_EQ(ExpectRegionFailure(PHYS_VMXON_MISALIGNED, false, 4, vmxon_unwind), 0, "VMXON PA alignment");
    TEST_ASSERT_INT_EQ(ExpectRegionFailure(PHYS_VMXON_RANGE, true, 4, vmxon_unwind), 0, "VMXON PA width");
    TEST_ASSERT_INT_EQ(ExpectRegionFailure(PHYS_VMXON_EXTENT, false, 4, vmxon_unwind), 0, "VMXON PA extent");
    TEST_ASSERT_INT_EQ(ExpectRegionFailure(PHYS_VMCS_MISALIGNED, false, 5, vmcs_unwind), 0, "VMCS PA alignment");
    TEST_ASSERT_INT_EQ(ExpectRegionFailure(PHYS_BITMAP_MISALIGNED, false, 6, bitmap_unwind), 0, "bitmap PA alignment");
    TEST_ASSERT_INT_EQ(ExpectRegionFailure(PHYS_BITMAP_RANGE, true, 6, bitmap_unwind), 0, "bitmap PA width");
    fake.VaAlias = true;
    TEST_ASSERT_INT_EQ(VmxPersistentContextCreate(&identity, Basic(false), 32,
        &allocator, &context), VMX_PERSISTENT_CONTEXT_REGION_INVALID, "VA alias rejection");
    TEST_ASSERT_UINT_EQ(fake.FreeCount, 4, "VA alias family frees");
    TEST_ASSERT_TRUE(fake.FreeOrder[0] == 5 && fake.FreeOrder[1] == 3 && fake.FreeOrder[2] == 2 && fake.FreeOrder[3] == 1, "VA alias unwind");
    memset(&fake, 0, sizeof(fake)); allocator = MakeAllocator(&fake); fake.PhysicalMode = PHYS_PA_ALIAS;
    TEST_ASSERT_INT_EQ(VmxPersistentContextCreate(&identity, Basic(false), 32,
        &allocator, &context), VMX_PERSISTENT_CONTEXT_REGION_INVALID, "PA alias rejection");
    TEST_ASSERT_UINT_EQ(fake.FreeCount, 5, "PA alias unique frees");
    TEST_ASSERT_TRUE(fake.FreeOrder[0] == 5 && fake.FreeOrder[1] == 4 && fake.FreeOrder[2] == 3 && fake.FreeOrder[3] == 2 && fake.FreeOrder[4] == 1, "PA alias unwind");
    return 0;
}
static int test_guest_stack_aliases(void)
{
    size_t alias;
    for (alias = 1; alias <= 2; ++alias) {
        FAKE_ALLOCATOR fake = {0}; VMX_PERSISTENT_ALLOCATOR allocator = MakeAllocator(&fake);
        VMX_PERSISTENT_CONTEXT context, before; VMX_PROCESSOR_IDENTITY identity = Identity(12);
        fake.GuestAlias = alias; memset(&context, 0xA5, sizeof(context)); before = context;
        TEST_ASSERT_INT_EQ(VmxPersistentContextCreate(&identity, Basic(false), 32,
            &allocator, &context), VMX_PERSISTENT_CONTEXT_REGION_INVALID, "guest alias rejection");
        TEST_ASSERT_TRUE(memcmp(&context, &before, sizeof(context)) == 0, "guest alias preserves");
        TEST_ASSERT_UINT_EQ(fake.Calls, 3, "guest alias pool calls");
        TEST_ASSERT_UINT_EQ(fake.FreeCount, 2, "guest alias family frees");
        TEST_ASSERT_UINT_EQ(fake.FreeOrder[0], alias == 1 ? 1 : 2, "guest alias first free");
        TEST_ASSERT_UINT_EQ(fake.FreeOrder[1], alias == 1 ? 2 : 1, "guest alias final free");
    }
    return 0;
}
static int test_cross_family_aliases(void)
{
    size_t stage, pool, i, frees;
    for (stage = 1; stage <= 3; ++stage) for (pool = 1; pool <= 3; ++pool) {
        FAKE_ALLOCATOR fake = {0}; VMX_PERSISTENT_ALLOCATOR allocator = MakeAllocator(&fake);
        VMX_PERSISTENT_CONTEXT context, before; VMX_PROCESSOR_IDENTITY identity = Identity(13);
        fake.CrossAliasStage = stage; fake.CrossAliasPool = pool;
        memset(&context, 0xA5, sizeof(context)); before = context;
        TEST_ASSERT_INT_EQ(VmxPersistentContextCreate(&identity, Basic(false), 32,
            &allocator, &context), VMX_PERSISTENT_CONTEXT_REGION_INVALID, "cross-family alias");
        TEST_ASSERT_TRUE(memcmp(&context, &before, sizeof(context)) == 0, "cross alias preserves");
        frees = stage + 3u;
        TEST_ASSERT_UINT_EQ(fake.PoolFreeCount, 3, "cross alias pool family");
        TEST_ASSERT_UINT_EQ(fake.ContiguousFreeCount, stage, "cross alias contiguous family");
        TEST_ASSERT_UINT_EQ(fake.FreeCount, frees, "cross alias release count");
        for (i = 0; i < frees; ++i) TEST_ASSERT_UINT_EQ(fake.FreeOrder[i], frees - i, "cross alias order");
    }
    return 0;
}
static int test_requested_region_contract(void)
{
    FAKE_ALLOCATOR fake = {0};
    VMX_PERSISTENT_ALLOCATOR allocator = MakeAllocator(&fake);
    VMX_PERSISTENT_CONTEXT context = {0};
    VMX_PROCESSOR_IDENTITY identity = Identity(65);
    uint32_t revision;
    TEST_ASSERT_INT_EQ(VmxPersistentContextCreate(&identity, Basic(true), 48,
        &allocator, &context), VMX_PERSISTENT_CONTEXT_OK, "region contract create");
    TEST_ASSERT_TRUE(fake.PoolSize[0] == 48 && fake.PoolSize[1] == VMX_PERSISTENT_HOST_STACK_SIZE && fake.PoolSize[2] == VMX_PERSISTENT_GUEST_STACK_SIZE, "requested launch/host/guest sizes");
    TEST_ASSERT_TRUE(fake.AllocOrder[0] == 1 && fake.AllocOrder[1] == 2 && fake.AllocOrder[2] == 3 && fake.AllocOrder[3] == 4 && fake.AllocOrder[4] == 5 && fake.AllocOrder[5] == 6, "successful allocation order");
    TEST_ASSERT_TRUE(fake.RequestedSize[0] == 4096 && fake.RequestedSize[1] == 4096 && fake.RequestedSize[2] == VMX_MSR_BITMAP_SIZE, "requested 4K regions");
    TEST_ASSERT_TRUE(fake.RequestedHighest[0] == UINT32_MAX && fake.RequestedHighest[1] == UINT32_MAX && fake.RequestedHighest[2] == UINT32_MAX, "requested highest PA");
    TEST_ASSERT_UINT_EQ(context.Capabilities.HighestPhysicalAddress, UINT32_MAX, "capabilities copied");
    TEST_ASSERT_TRUE(fake.PhysicalSeen[0] == context.VmxonPhysicalAddress && fake.PhysicalSeen[1] == context.VmcsPhysicalAddress && fake.PhysicalSeen[2] == context.MsrBitmapPhysicalAddress, "captured PAs");
    TEST_ASSERT_UINT_EQ(context.GuestStackSize, VMX_PERSISTENT_GUEST_STACK_SIZE, "guest stack size");
    TEST_ASSERT_TRUE(context.GuestStack != context.HostStack && context.GuestStack != context.LaunchStorage, "guest stack distinct");
    TEST_ASSERT_UINT_EQ(context.OwnedResources, VMX_PERSISTENT_RESOURCE_LAUNCH | VMX_PERSISTENT_RESOURCE_STACK | VMX_PERSISTENT_RESOURCE_GUEST_STACK | VMX_PERSISTENT_RESOURCE_VMXON | VMX_PERSISTENT_RESOURCE_VMCS | VMX_PERSISTENT_RESOURCE_MSR_BITMAP, "all resources owned");
    identity.Group = 99; identity.Number = 99; identity.SystemIndex = 999;
    TEST_ASSERT_UINT_EQ(context.Identity.SystemIndex, 65, "identity copied");
    TEST_ASSERT_UINT_EQ(context.Identity.Group, 1, "identity group copied");
    TEST_ASSERT_UINT_EQ(context.Identity.Number, 1, "identity number copied");
    memcpy(&revision, context.VmxonRegion, sizeof(revision));
    TEST_ASSERT_UINT_EQ(revision, 0x123, "VMXON revision");
    TEST_ASSERT_UINT_EQ(((unsigned char*)context.VmxonRegion)[VMX_REGION_MIN_SIZE - 1], 0, "VMXON zero tail");
    memcpy(&revision, context.VmcsRegion, sizeof(revision));
    TEST_ASSERT_UINT_EQ(revision, 0x123, "VMCS revision");
    TEST_ASSERT_UINT_EQ(((unsigned char*)context.VmcsRegion)[VMX_REGION_MIN_SIZE - 1], 0, "VMCS zero tail");
    TEST_ASSERT_INT_EQ(VmxPersistentContextDestroy(&allocator, &context), VMX_PERSISTENT_CONTEXT_OK, "region contract destroy");
    TEST_ASSERT_UINT_EQ(fake.FreeOrder[0], 6, "destroy bitmap first");
    TEST_ASSERT_UINT_EQ(fake.FreeOrder[1], 5, "destroy VMCS second");
    TEST_ASSERT_UINT_EQ(fake.FreeOrder[2], 4, "destroy VMXON third");
    TEST_ASSERT_UINT_EQ(fake.FreeOrder[3], 3, "destroy guest fourth");
    TEST_ASSERT_UINT_EQ(fake.FreeOrder[4], 2, "destroy host fifth");
    TEST_ASSERT_UINT_EQ(fake.FreeOrder[5], 1, "destroy launch last");
    TEST_ASSERT_UINT_EQ(context.OwnedResources, 0, "destroy ownership cleared");
    TEST_ASSERT_FALSE(context.Published, "destroy publication cleared");
    TEST_ASSERT_INT_EQ(VmxPersistentContextDestroy(&allocator, &context), VMX_PERSISTENT_CONTEXT_OK, "destroy idempotent");
    TEST_ASSERT_UINT_EQ(fake.FreeCount, 6, "destroy idempotent no frees");
    return 0;
}
static int test_many_independent_contexts(void)
{
    static FAKE_ALLOCATOR fakes[40];
    VMX_PERSISTENT_ALLOCATOR allocators[40];
    VMX_PERSISTENT_CONTEXT contexts[40];
    size_t i, j;
    for (i = 0; i < 40; ++i) {
        uint32_t system_index = (uint32_t)(48u + i);
        VMX_PROCESSOR_IDENTITY identity = Identity(system_index);
        fakes[i].Slot = i; allocators[i] = MakeAllocator(&fakes[i]);
        TEST_ASSERT_INT_EQ(VmxPersistentContextCreate(&identity, Basic(false), i + 1,
            &allocators[i], &contexts[i]), VMX_PERSISTENT_CONTEXT_OK, "many create");
        TEST_ASSERT_UINT_EQ(contexts[i].Identity.SystemIndex, system_index, "many identity");
        TEST_ASSERT_UINT_EQ(contexts[i].Identity.Group, system_index / VMX_PROCESSOR_GROUP_MAX_NUMBER, "many group");
        TEST_ASSERT_UINT_EQ(contexts[i].Identity.Number, system_index % VMX_PROCESSOR_GROUP_MAX_NUMBER, "many number");
        for (j = 0; j < i; ++j) { TEST_ASSERT_TRUE(contexts[i].LaunchStorage != contexts[j].LaunchStorage, "independent launch storage"); TEST_ASSERT_TRUE(contexts[i].GuestStack != contexts[j].GuestStack, "independent guest stack"); }
    }
    for (i = 0; i < 40; ++i) {
        TEST_ASSERT_INT_EQ(VmxPersistentContextDestroy(&allocators[i], &contexts[i]), VMX_PERSISTENT_CONTEXT_OK, "many destroy");
        TEST_ASSERT_UINT_EQ(fakes[i].FreeCount, 6, "many exact frees");
    }
    return 0;
}
static int test_destroy_obligations_and_retries(void)
{
    FAKE_ALLOCATOR fake = {0};
    VMX_PERSISTENT_ALLOCATOR allocator = MakeAllocator(&fake), invalid = {0};
    VMX_PERSISTENT_CONTEXT context = {0}, before;
    VMX_PROCESSOR_IDENTITY identity = Identity(11);
    bool* guards[] = { &context.CpuState.VmxActive, &context.CpuState.VmxCurrent,
        &context.CpuState.RestorationRequired, &context.CpuState.Cr0Changed,
        &context.CpuState.Cr4Changed, &context.CpuState.GdtrRestoreRequired,
        &context.CpuState.IdtrRestoreRequired, &context.CpuState.AffinityPinned,
        &context.CpuState.IrqlRaised, &context.CpuState.ShutdownRequested };
    size_t i;
    TEST_ASSERT_INT_EQ(VmxPersistentContextCreate(&identity, Basic(false), 16,
        &allocator, &context), VMX_PERSISTENT_CONTEXT_OK, "destroy setup");
    before = context;
    TEST_ASSERT_INT_EQ(VmxPersistentContextDestroy(&invalid, &context), VMX_PERSISTENT_CONTEXT_INVALID_ALLOCATOR, "invalid allocator retry");
    TEST_ASSERT_TRUE(memcmp(&context, &before, sizeof(context)) == 0, "invalid allocator preserves");
    TEST_ASSERT_INT_EQ(VmxPersistentContextDestroy(&allocator, &context), VMX_PERSISTENT_CONTEXT_OK, "destroy retry");
    for (i = 0; i < sizeof(guards) / sizeof(*guards); ++i) {
        *guards[i] = true; before = context;
        TEST_ASSERT_INT_EQ(VmxPersistentContextDestroy(&allocator, &context), VMX_PERSISTENT_CONTEXT_BUSY, "obligation busy");
        TEST_ASSERT_TRUE(memcmp(&context, &before, sizeof(context)) == 0, "obligation preserves");
        *guards[i] = false;
    }
    context.CpuState.ShutdownRequested = true; context.CpuState.ShutdownProven = true;
    TEST_ASSERT_INT_EQ(VmxPersistentContextDestroy(&allocator, &context), VMX_PERSISTENT_CONTEXT_OK, "proven shutdown idempotence");
    TEST_ASSERT_INT_EQ(VmxPersistentContextDestroy(&allocator, &context), VMX_PERSISTENT_CONTEXT_OK, "repeated destroy");
    return 0;
}
TEST_MAIN_BEGIN()
    TEST_RUN(test_invalid_inputs_and_callbacks);
    TEST_RUN(test_allocation_boundaries);
    TEST_RUN(test_physical_boundaries_and_aliases);
    TEST_RUN(test_guest_stack_aliases);
    TEST_RUN(test_cross_family_aliases);
    TEST_RUN(test_requested_region_contract);
    TEST_RUN(test_many_independent_contexts);
    TEST_RUN(test_destroy_obligations_and_retries);
TEST_MAIN_END()
