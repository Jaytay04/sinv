#include "test_common.h"
#include "vmx_persistent_lifecycle.h"
#include <string.h>
#include <stdlib.h>
typedef struct _FAKE_LIFECYCLE {
    int InlineEvents[64];
    int* Events;
    size_t EventCapacity;
    size_t EventCount;
    int FailKind;
    size_t FailIndex;
    uint64_t ErrorMask;
    bool ActivatePost; bool ActivateMalformed; bool ActivateZeroOwned; bool PrepareMalformed;
    bool StopResidual;
    bool StopFail; bool StopFailClears;
    bool StopBadSuccess;
    bool DestroyBadSuccess; bool DestroyFailClears;
    int IdentityMutationKind;
    VMX_PERSISTENT_LIFECYCLE* Lifecycle; bool ReenterStart; bool ReenterStop; bool Reentered; VMX_PERSISTENT_STATUS ReenterResult;
} FAKE_LIFECYCLE;
static void Event(FAKE_LIFECYCLE* fake, int value)
{
    if (fake->Events == NULL) { fake->Events = fake->InlineEvents; fake->EventCapacity = sizeof(fake->InlineEvents) / sizeof(fake->InlineEvents[0]); }
    if (fake->EventCount < fake->EventCapacity) fake->Events[fake->EventCount] = value;
    ++fake->EventCount;
}
static bool ShouldFail(const FAKE_LIFECYCLE* fake, size_t index)
{
    return fake->FailIndex == index || (index < 64 && (fake->ErrorMask & (UINT64_C(1) << index)) != 0);
}
static void Reenter(FAKE_LIFECYCLE* fake)
{
    if (fake->Lifecycle != NULL && !fake->Reentered && (fake->ReenterStart || fake->ReenterStop)) {
        fake->Reentered = true;
        fake->ReenterResult = fake->ReenterStart ? VmxPersistentLifecycleStart(fake->Lifecycle) : VmxPersistentLifecycleStop(fake->Lifecycle);
    }
}
static int CpuIndex(const VMX_PERSISTENT_CPU_RECORD* cpu)
{
    return (int)cpu->Identity.SystemIndex;
}
static void MaybeMutateIdentity(FAKE_LIFECYCLE* fake, VMX_PERSISTENT_CPU_RECORD* cpu, int kind) { if (fake->IdentityMutationKind == kind) cpu->Identity.SystemIndex += 1000u; }
static VMX_PERSISTENT_STATUS FakePrepare(void* context,
                                         VMX_PERSISTENT_CPU_RECORD* cpu)
{
    FAKE_LIFECYCLE* fake = (FAKE_LIFECYCLE*)context;
    int index = CpuIndex(cpu);
    Event(fake, 10 + index); Reenter(fake); MaybeMutateIdentity(fake, cpu, 1);
    if (fake->FailKind == 1 && ShouldFail(fake, (size_t)index)) {
        cpu->OwnedMask = 1;
        cpu->CleanupRequired = true;
        return 101;
    }
    if (fake->PrepareMalformed && ShouldFail(fake, (size_t)index)) { cpu->Active = true; cpu->CleanupRequired = true; } return VMX_PERSISTENT_STATUS_OK;
}
static VMX_PERSISTENT_STATUS FakeActivate(void* context,
                                          VMX_PERSISTENT_CPU_RECORD* cpu)
{
    FAKE_LIFECYCLE* fake = (FAKE_LIFECYCLE*)context;
    int index = CpuIndex(cpu);
    Event(fake, 20 + index);
    if (fake->FailKind == 2 && ShouldFail(fake, (size_t)index)) {
        cpu->OwnedMask = fake->ActivatePost ? 2u : 0u;
        cpu->Active = fake->ActivatePost;
        cpu->CleanupRequired = fake->ActivatePost;
        return 202;
    }
    cpu->OwnedMask = fake->ActivateZeroOwned ? 0u : 4u;
    cpu->Active = true;
    if (fake->ActivateMalformed) cpu->CleanupRequired = true;
    MaybeMutateIdentity(fake, cpu, 2);
    return VMX_PERSISTENT_STATUS_OK;
}
static VMX_PERSISTENT_STATUS FakeStop(void* context,
                                      VMX_PERSISTENT_CPU_RECORD* cpu)
{
    FAKE_LIFECYCLE* fake = (FAKE_LIFECYCLE*)context;
    int index = CpuIndex(cpu);
    Event(fake, 30 + index); Reenter(fake); MaybeMutateIdentity(fake, cpu, 3);
    if (fake->StopBadSuccess) { cpu->Active = true; cpu->CleanupRequired = true; return VMX_PERSISTENT_STATUS_OK; }
    if (fake->StopFail && ShouldFail(fake, (size_t)index)) {
        cpu->Active = !fake->StopFailClears;
        cpu->CleanupRequired = !fake->StopFailClears;
        cpu->OwnedMask = 8u;
        return 303;
    }
    cpu->Active = false;
    cpu->CleanupRequired = false;
    if (!fake->StopResidual) cpu->OwnedMask = 0;
    return VMX_PERSISTENT_STATUS_OK;
}
static VMX_PERSISTENT_STATUS FakeDestroy(void* context,
                                         VMX_PERSISTENT_CPU_RECORD* cpu)
{
    FAKE_LIFECYCLE* fake = (FAKE_LIFECYCLE*)context;
    int index = CpuIndex(cpu);
    Event(fake, 40 + index); MaybeMutateIdentity(fake, cpu, 4);
    if (fake->DestroyFailClears && ShouldFail(fake, (size_t)index)) { cpu->Active = true; cpu->CleanupRequired = true; cpu->OwnedMask = 0; return 404; }
    if (fake->DestroyBadSuccess) { cpu->Active = true; cpu->CleanupRequired = true; } else cpu->OwnedMask = 0;
    return VMX_PERSISTENT_STATUS_OK;
}
static VMX_PROCESSOR_TOPOLOGY_SNAPSHOT MakeSnapshot(
    VMX_PROCESSOR_IDENTITY* identities, size_t count)
{
    VMX_PROCESSOR_TOPOLOGY_SNAPSHOT snapshot = { identities, count, NULL, NULL };
    return snapshot;
}
static void InitIdentities(VMX_PROCESSOR_IDENTITY* identities, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        identities[i].Group = (uint16_t)(i / 2);
        identities[i].Number = (uint8_t)(i % 2);
        identities[i].Reserved = 0;
        identities[i].SystemIndex = (uint32_t)i;
    }
}
static VMX_PERSISTENT_CALLBACKS MakeCallbacks(FAKE_LIFECYCLE* fake)
{
    VMX_PERSISTENT_CALLBACKS callbacks = {
        fake, FakePrepare, FakeActivate, FakeStop, FakeDestroy
    };
    return callbacks;
}
static VMX_PERSISTENT_STATUS Setup(VMX_PERSISTENT_LIFECYCLE* lifecycle,
                                   VMX_PERSISTENT_CPU_RECORD* storage,
                                   VMX_PROCESSOR_IDENTITY* identities,
                                   size_t count, FAKE_LIFECYCLE* fake)
{
    VMX_PROCESSOR_TOPOLOGY_SNAPSHOT snapshot = MakeSnapshot(identities, count);
    if (fake->Events == NULL) { fake->Events = fake->InlineEvents; fake->EventCapacity = sizeof(fake->InlineEvents) / sizeof(fake->InlineEvents[0]); }
    VMX_PERSISTENT_CALLBACKS callbacks = MakeCallbacks(fake);
    return VmxPersistentLifecycleInitialize(&snapshot, storage, count,
                                            &callbacks, lifecycle);
}
static int test_success_and_reverse_stop(void)
{
    VMX_PROCESSOR_IDENTITY identities[3];
    VMX_PERSISTENT_CPU_RECORD storage[3];
    VMX_PERSISTENT_LIFECYCLE lifecycle;
    FAKE_LIFECYCLE fake = { 0 };
    InitIdentities(identities, 3);
    TEST_ASSERT_INT_EQ(Setup(&lifecycle, storage, identities, 3, &fake), 0, "initialize");
    TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStart(&lifecycle), 0, "start N/N");
    TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_LOADED, "loaded state");
    TEST_ASSERT_UINT_EQ(storage[2].Identity.Group, 1, "copied group");
    TEST_ASSERT_TRUE(storage[0].Active && storage[1].Active && storage[2].Active, "all active"); TEST_ASSERT_UINT_EQ(storage[0].Stage, VMX_PERSISTENT_CPU_ACTIVE, "active stage"); TEST_ASSERT_TRUE(fake.Events[2] == 12 && fake.Events[3] == 20, "prepare phase precedes activate");
    TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), 0, "stop");
    TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_UNLOADED, "unloaded state"); TEST_ASSERT_UINT_EQ(storage[0].Stage, VMX_PERSISTENT_CPU_INITIAL, "initial stage");
    TEST_ASSERT_UINT_EQ(fake.Events[fake.EventCount - 3], 32, "reverse stop first");
    TEST_ASSERT_UINT_EQ(fake.Events[fake.EventCount - 1], 30, "reverse stop last");
    return 0;
}
static int test_invalid_preserves_output(void)
{
    VMX_PROCESSOR_IDENTITY identities[2];
    VMX_PERSISTENT_CPU_RECORD storage[2];
    VMX_PERSISTENT_LIFECYCLE lifecycle;
    VMX_PERSISTENT_LIFECYCLE before;
    FAKE_LIFECYCLE fake = { 0 };
    VMX_PROCESSOR_TOPOLOGY_SNAPSHOT empty = { NULL, 0, NULL, NULL };
    VMX_PERSISTENT_CALLBACKS callbacks = MakeCallbacks(&fake);
    (void)memset(&lifecycle, 0xA5, sizeof(lifecycle)); before = lifecycle;
    InitIdentities(identities, 2);
    TEST_ASSERT_INT_EQ(VmxPersistentLifecycleInitialize(&empty, storage, 2, &callbacks, &lifecycle), VMX_PERSISTENT_STATUS_INVALID, "empty snapshot");
    TEST_ASSERT_TRUE(memcmp(&lifecycle, &before, sizeof(lifecycle)) == 0, "invalid preserves output");
    TEST_ASSERT_INT_EQ(VmxPersistentLifecycleInitialize(&(VMX_PROCESSOR_TOPOLOGY_SNAPSHOT){ identities, 2, NULL, NULL }, storage, 1, &callbacks, &lifecycle), VMX_PERSISTENT_STATUS_INVALID, "capacity");
    callbacks.Destroy = NULL;
    TEST_ASSERT_INT_EQ(VmxPersistentLifecycleInitialize(&(VMX_PROCESSOR_TOPOLOGY_SNAPSHOT){ identities, 2, NULL, NULL }, storage, 2, &callbacks, &lifecycle), VMX_PERSISTENT_STATUS_INVALID, "callbacks");
    return 0;
}
static int test_prepare_failure_and_activate_phases(void)
{
    VMX_PROCESSOR_IDENTITY identities[3];
    VMX_PERSISTENT_CPU_RECORD storage[3];
    VMX_PERSISTENT_LIFECYCLE lifecycle;
    FAKE_LIFECYCLE fake = { 0 };
    InitIdentities(identities, 3);
    TEST_ASSERT_INT_EQ(Setup(&lifecycle, storage, identities, 3, &fake), 0, "prepare setup");
    fake.PrepareMalformed = true; fake.FailIndex = 1;
    TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStart(&lifecycle), VMX_PERSISTENT_STATUS_INVARIANT, "malformed prepare");
    TEST_ASSERT_INT_EQ(lifecycle.FirstOperationStatus, VMX_PERSISTENT_STATUS_INVARIANT, "prepare invariant"); TEST_ASSERT_UINT_EQ(lifecycle.FirstOperationIndex, 1, "prepare index");
    TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_UNLOADED, "clean prepare rollback"); TEST_ASSERT_UINT_EQ(storage[1].Stage, VMX_PERSISTENT_CPU_FAILED, "prepare failed stage"); TEST_ASSERT_UINT_EQ(fake.Events[fake.EventCount - 1], 31, "malformed prepare stopped");
    (void)memset(&fake, 0, sizeof(fake)); fake.FailKind = 2; fake.FailIndex = 1;
    TEST_ASSERT_INT_EQ(Setup(&lifecycle, storage, identities, 3, &fake), 0, "pre activate setup");
    TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStart(&lifecycle), 202, "pre VMXON failure");
    TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_UNLOADED, "pre failure rollback"); TEST_ASSERT_UINT_EQ(storage[1].Stage, VMX_PERSISTENT_CPU_FAILED, "activate failed stage");
    (void)memset(&fake, 0, sizeof(fake)); fake.FailKind = 2; fake.FailIndex = 1; fake.ActivatePost = true; fake.StopResidual = true;
    TEST_ASSERT_INT_EQ(Setup(&lifecycle, storage, identities, 3, &fake), 0, "post activate setup");
    TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStart(&lifecycle), 202, "post VMXON failure");
    TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_UNLOADED, "post failure rollback"); TEST_ASSERT_UINT_EQ(storage[1].Stage, VMX_PERSISTENT_CPU_FAILED, "post failed stage");
    TEST_ASSERT_UINT_EQ(fake.Events[fake.EventCount - 4], 31, "post reverse stop");
    TEST_ASSERT_UINT_EQ(fake.Events[fake.EventCount - 1], 40, "post reverse destroy");
    return 0;
}
static int test_residual_cleanup_retry(void)
{
    VMX_PROCESSOR_IDENTITY identities[2];
    VMX_PERSISTENT_CPU_RECORD storage[2];
    VMX_PERSISTENT_LIFECYCLE lifecycle;
    FAKE_LIFECYCLE fake = { 0 };
    InitIdentities(identities, 2);
    TEST_ASSERT_INT_EQ(Setup(&lifecycle, storage, identities, 2, &fake), 0, "contract setup"); fake.FailKind = 2; fake.FailIndex = 1; fake.ActivatePost = true; fake.StopBadSuccess = true;
    TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStart(&lifecycle), 202, "stop contract failure");
    TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_ERROR, "error contract state"); TEST_ASSERT_INT_EQ(lifecycle.FirstCleanupStatus, VMX_PERSISTENT_STATUS_INVARIANT, "invariant status"); TEST_ASSERT_UINT_EQ(lifecycle.FirstCleanupIndex, 1, "invariant index"); TEST_ASSERT_TRUE(fake.EventCount == 6 && fake.Events[0] == 10 && fake.Events[1] == 11 && fake.Events[2] == 20 && fake.Events[3] == 21 && fake.Events[4] == 31 && fake.Events[5] == 30, "stale stop exact order and no destroy");
    fake.StopBadSuccess = false; TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), 0, "clean retry current status"); TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_UNLOADED, "clean retry unloaded");
    (void)memset(&fake, 0, sizeof(fake)); fake.FailKind = 2; fake.FailIndex = 1; fake.ActivatePost = true; fake.StopResidual = true; fake.DestroyBadSuccess = true;
    TEST_ASSERT_INT_EQ(Setup(&lifecycle, storage, identities, 2, &fake), 0, "destroy contract setup"); TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStart(&lifecycle), 202, "destroy contract failure");
    TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_ERROR, "error destroy state"); TEST_ASSERT_INT_EQ(lifecycle.FirstCleanupStatus, VMX_PERSISTENT_STATUS_INVARIANT, "destroy invariant");
    fake.DestroyBadSuccess = false; TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), 0, "destroy clean retry"); TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_UNLOADED, "destroy retry unloaded"); TEST_ASSERT_INT_EQ(lifecycle.FirstCleanupStatus, VMX_PERSISTENT_STATUS_INVARIANT, "historical cleanup retained");
    return 0;
}
static int test_activation_invariants(void)
{
    VMX_PROCESSOR_IDENTITY ids[1]; VMX_PERSISTENT_CPU_RECORD storage[1]; VMX_PERSISTENT_LIFECYCLE lifecycle; FAKE_LIFECYCLE fake = { 0 };
    InitIdentities(ids, 1); TEST_ASSERT_INT_EQ(Setup(&lifecycle, storage, ids, 1, &fake), 0, "activation invariant setup"); fake.ActivateMalformed = true;
    TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStart(&lifecycle), VMX_PERSISTENT_STATUS_INVARIANT, "cleanup-required activate rejected"); TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_UNLOADED, "malformed activation rolled back"); TEST_ASSERT_INT_EQ(lifecycle.FirstOperationStatus, VMX_PERSISTENT_STATUS_INVARIANT, "activation invariant recorded"); TEST_ASSERT_UINT_EQ(storage[0].Identity.SystemIndex, 0, "activation identity retained"); TEST_ASSERT_TRUE(fake.EventCount == 3 && fake.Events[2] == 30, "activation rollback stop");
    fake = (FAKE_LIFECYCLE){ 0 }; fake.ActivateZeroOwned = true; TEST_ASSERT_INT_EQ(Setup(&lifecycle, storage, ids, 1, &fake), 0, "zero-owned activation setup");
    TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStart(&lifecycle), VMX_PERSISTENT_STATUS_INVARIANT, "zero-owned activate rejected"); TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_UNLOADED, "zero-owned activation rolled back");
    fake = (FAKE_LIFECYCLE){ 0 }; TEST_ASSERT_INT_EQ(Setup(&lifecycle, storage, ids, 1, &fake), 0, "residual status setup"); TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStart(&lifecycle), 0, "residual status start"); fake.StopResidual = true; fake.DestroyBadSuccess = true; TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), VMX_PERSISTENT_STATUS_INVARIANT, "residual status invariant"); TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_ERROR, "residual status error"); fake.DestroyBadSuccess = false; fake.StopResidual = false; TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), 0, "residual status retry");
    return 0;
}
static int test_identity_mutation_and_unsafe_stop(void)
{
    VMX_PROCESSOR_IDENTITY ids[1]; VMX_PERSISTENT_CPU_RECORD storage[1]; VMX_PERSISTENT_LIFECYCLE lifecycle; FAKE_LIFECYCLE fake = { 0 };
    InitIdentities(ids, 1); TEST_ASSERT_INT_EQ(Setup(&lifecycle, storage, ids, 1, &fake), 0, "prepare identity setup"); fake.IdentityMutationKind = 1;
    TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStart(&lifecycle), VMX_PERSISTENT_STATUS_INVARIANT, "prepare identity mutation"); TEST_ASSERT_UINT_EQ(storage[0].Identity.SystemIndex, 0, "prepare identity restored"); TEST_ASSERT_INT_EQ(lifecycle.FirstOperationStatus, VMX_PERSISTENT_STATUS_INVARIANT, "prepare mutation recorded");
    fake = (FAKE_LIFECYCLE){ 0 }; fake.IdentityMutationKind = 2; TEST_ASSERT_INT_EQ(Setup(&lifecycle, storage, ids, 1, &fake), 0, "activate identity setup"); TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStart(&lifecycle), VMX_PERSISTENT_STATUS_INVARIANT, "activate identity mutation"); TEST_ASSERT_UINT_EQ(storage[0].Identity.SystemIndex, 0, "activate identity restored");
    fake = (FAKE_LIFECYCLE){ 0 }; TEST_ASSERT_INT_EQ(Setup(&lifecycle, storage, ids, 1, &fake), 0, "stop identity setup"); TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStart(&lifecycle), 0, "stop identity start"); fake.IdentityMutationKind = 3;
    TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), VMX_PERSISTENT_STATUS_INVARIANT, "stop identity mutation"); TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_ERROR, "stop mutation error"); TEST_ASSERT_UINT_EQ(storage[0].Identity.SystemIndex, 0, "stop identity restored"); TEST_ASSERT_UINT_EQ(fake.EventCount, 3, "unsafe stop no destroy"); fake.IdentityMutationKind = 0;
    TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), 0, "stop mutation retry"); TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_UNLOADED, "stop mutation retry unloaded"); TEST_ASSERT_INT_EQ(lifecycle.FirstCleanupStatus, VMX_PERSISTENT_STATUS_INVARIANT, "cleanup history retained");
    fake = (FAKE_LIFECYCLE){ 0 }; TEST_ASSERT_INT_EQ(Setup(&lifecycle, storage, ids, 1, &fake), 0, "destroy identity setup"); TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStart(&lifecycle), 0, "destroy identity start"); fake.StopResidual = true; fake.IdentityMutationKind = 4;
    TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), VMX_PERSISTENT_STATUS_INVARIANT, "destroy identity mutation"); TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_ERROR, "destroy mutation error"); TEST_ASSERT_UINT_EQ(storage[0].Identity.SystemIndex, 0, "destroy identity restored"); TEST_ASSERT_UINT_EQ(fake.EventCount, 4, "destroy mutation event"); fake.IdentityMutationKind = 0; fake.StopResidual = false;
    TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), 0, "destroy mutation retry"); TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_UNLOADED, "destroy mutation retry unloaded"); TEST_ASSERT_INT_EQ(lifecycle.FirstCleanupStatus, VMX_PERSISTENT_STATUS_INVARIANT, "destroy cleanup history retained");
    return 0;
}
static int test_destroy_failure_restores_ownership(void)
{
    VMX_PROCESSOR_IDENTITY ids[1]; VMX_PERSISTENT_CPU_RECORD storage[1]; VMX_PERSISTENT_LIFECYCLE lifecycle; FAKE_LIFECYCLE fake = { 0 };
    InitIdentities(ids, 1); TEST_ASSERT_INT_EQ(Setup(&lifecycle, storage, ids, 1, &fake), 0, "destroy failure setup"); TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStart(&lifecycle), 0, "destroy failure start"); fake.StopResidual = true; fake.DestroyFailClears = true;
    TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), 404, "destroy failure status"); TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_ERROR, "destroy failure error"); TEST_ASSERT_TRUE(storage[0].Active && storage[0].CleanupRequired && storage[0].OwnedMask == 4, "destroy failure ownership restored"); TEST_ASSERT_UINT_EQ(storage[0].Identity.SystemIndex, 0, "destroy failure identity retained"); TEST_ASSERT_TRUE(fake.EventCount == 4 && fake.Events[2] == 30 && fake.Events[3] == 40, "destroy failure exact pass"); fake.DestroyFailClears = false;
    TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), 0, "destroy failure retry"); TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_UNLOADED, "destroy failure retry unloaded"); TEST_ASSERT_TRUE(fake.EventCount == 6 && fake.Events[4] == 30 && fake.Events[5] == 40, "destroy failure stop then destroy retry"); TEST_ASSERT_TRUE(storage[0].OwnedMask == 0 && storage[0].Identity.SystemIndex == 0, "destroy retry clean identity");
    return 0;
}
static int test_stop_error_clears_flags(void)
{
    VMX_PROCESSOR_IDENTITY ids[1]; VMX_PERSISTENT_CPU_RECORD storage[1]; VMX_PERSISTENT_LIFECYCLE lifecycle; FAKE_LIFECYCLE fake = { 0 };
    InitIdentities(ids, 1); TEST_ASSERT_INT_EQ(Setup(&lifecycle, storage, ids, 1, &fake), 0, "unsafe stop setup"); TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStart(&lifecycle), 0, "unsafe stop start"); fake.StopFail = true; fake.StopFailClears = true; fake.FailIndex = 0;
    TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), 303, "stop failure remains unsafe"); TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_ERROR, "unsafe stop error"); TEST_ASSERT_TRUE(!storage[0].Active && storage[0].CleanupRequired && storage[0].OwnedMask == 8, "retryable stop flag forced"); TEST_ASSERT_UINT_EQ(fake.EventCount, 3, "unsafe stop no destroy"); fake.StopFail = false;
    TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), 0, "unsafe stop retry"); TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_UNLOADED, "unsafe stop retry unloaded"); TEST_ASSERT_UINT_EQ(fake.EventCount, 4, "retry stop only");
    return 0;
}
static int test_prepare_failure_matrix(void)
{
    VMX_PROCESSOR_IDENTITY ids[4]; VMX_PERSISTENT_CPU_RECORD storage[4]; VMX_PERSISTENT_LIFECYCLE lifecycle; size_t k, i; InitIdentities(ids, 4);
    for (k = 0; k < 4; ++k) { FAKE_LIFECYCLE fake = { 0 }; TEST_ASSERT_INT_EQ(Setup(&lifecycle, storage, ids, 4, &fake), 0, "prepare matrix setup"); fake.FailKind = 1; fake.FailIndex = k; TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStart(&lifecycle), 101, "prepare matrix status"); TEST_ASSERT_UINT_EQ(lifecycle.FirstOperationIndex, k, "prepare matrix index"); TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_UNLOADED, "prepare matrix rollback"); TEST_ASSERT_UINT_EQ(fake.EventCount, k + 2, "prepare matrix count"); for (i = 0; i <= k; ++i) TEST_ASSERT_UINT_EQ(fake.Events[i], 10 + i, "prepare matrix order"); TEST_ASSERT_UINT_EQ(fake.Events[k + 1], 30 + k, "prepare matrix stop"); }
    return 0;
}
static int test_activate_failure_matrix(void)
{
    VMX_PROCESSOR_IDENTITY ids[4]; VMX_PERSISTENT_CPU_RECORD storage[4]; VMX_PERSISTENT_LIFECYCLE lifecycle; size_t mode, k, i, pos; InitIdentities(ids, 4);
    for (mode = 0; mode < 2; ++mode) for (k = 0; k < 4; ++k) { FAKE_LIFECYCLE fake = { 0 }; TEST_ASSERT_INT_EQ(Setup(&lifecycle, storage, ids, 4, &fake), 0, "activate matrix setup"); fake.FailKind = 2; fake.FailIndex = k; fake.ActivatePost = mode != 0; fake.StopResidual = mode != 0; TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStart(&lifecycle), 202, "activate matrix status"); TEST_ASSERT_UINT_EQ(lifecycle.FirstOperationIndex, k, "activate matrix index"); TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_UNLOADED, "activate matrix rollback"); pos = 4; for (i = 0; i <= k; ++i) TEST_ASSERT_UINT_EQ(fake.Events[pos++], 20 + i, "activate matrix forward"); if (mode == 0) for (i = k; i-- > 0;) TEST_ASSERT_UINT_EQ(fake.Events[pos++], 30 + i, "activate pre stop"); else { for (i = k + 1; i-- > 0;) TEST_ASSERT_UINT_EQ(fake.Events[pos++], 30 + i, "activate post stop"); for (i = k + 1; i-- > 0;) TEST_ASSERT_UINT_EQ(fake.Events[pos++], 40 + i, "activate post destroy"); } TEST_ASSERT_UINT_EQ(fake.EventCount, pos, "activate matrix exact count"); }
    return 0;
}
static int test_stop_failure_matrix(void)
{
    VMX_PROCESSOR_IDENTITY ids[4]; VMX_PERSISTENT_CPU_RECORD storage[4]; VMX_PERSISTENT_LIFECYCLE lifecycle; size_t k, i, before; InitIdentities(ids, 4);
    for (k = 0; k < 4; ++k) { FAKE_LIFECYCLE fake = { 0 }; TEST_ASSERT_INT_EQ(Setup(&lifecycle, storage, ids, 4, &fake), 0, "stop matrix setup"); TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStart(&lifecycle), 0, "stop matrix start"); fake.StopFail = true; fake.FailIndex = k; before = fake.EventCount; TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), 303, "stop matrix status"); TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_ERROR, "stop matrix error"); TEST_ASSERT_UINT_EQ(lifecycle.FirstCleanupIndex, k, "stop matrix history index"); TEST_ASSERT_UINT_EQ(fake.EventCount, before + 4, "stop matrix count"); for (i = 0; i < 4; ++i) TEST_ASSERT_UINT_EQ(fake.Events[before + i], 33 - i, "stop matrix reverse"); before = fake.EventCount; TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStart(&lifecycle), VMX_PERSISTENT_STATUS_NOT_READY, "error start rejected"); TEST_ASSERT_UINT_EQ(fake.EventCount, before, "error start no events"); fake.StopFail = false; TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), 0, "stop matrix retry"); TEST_ASSERT_UINT_EQ(fake.EventCount, before + 1, "stop retry residual only"); TEST_ASSERT_UINT_EQ(fake.Events[fake.EventCount - 1], 30 + k, "stop retry index"); before = fake.EventCount; TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), 0, "stop unloaded idempotent"); TEST_ASSERT_UINT_EQ(fake.EventCount, before, "stop unloaded no events"); }
    return 0;
}
static int test_destroy_failure_matrix(void)
{
    VMX_PROCESSOR_IDENTITY ids[4]; VMX_PERSISTENT_CPU_RECORD storage[4]; VMX_PERSISTENT_LIFECYCLE lifecycle; size_t k, i, before; InitIdentities(ids, 4);
    for (k = 0; k < 4; ++k) { FAKE_LIFECYCLE fake = { 0 }; TEST_ASSERT_INT_EQ(Setup(&lifecycle, storage, ids, 4, &fake), 0, "destroy matrix setup"); TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStart(&lifecycle), 0, "destroy matrix start"); fake.StopResidual = true; fake.DestroyFailClears = true; fake.FailIndex = k; TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), 404, "destroy matrix status"); TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_ERROR, "destroy matrix error"); TEST_ASSERT_UINT_EQ(lifecycle.FirstCleanupIndex, k, "destroy matrix history index"); TEST_ASSERT_UINT_EQ(fake.EventCount, 16, "destroy matrix count"); for (i = 0; i < 4; ++i) { TEST_ASSERT_UINT_EQ(fake.Events[8 + i], 33 - i, "destroy matrix stop order"); TEST_ASSERT_UINT_EQ(fake.Events[12 + i], 43 - i, "destroy matrix destroy order"); } before = fake.EventCount; fake.DestroyFailClears = false; TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), 0, "destroy matrix retry"); TEST_ASSERT_UINT_EQ(fake.EventCount, before + 2, "destroy retry residual only"); TEST_ASSERT_UINT_EQ(fake.Events[before], 30 + k, "destroy retry stop"); TEST_ASSERT_UINT_EQ(fake.Events[before + 1], 40 + k, "destroy retry destroy"); TEST_ASSERT_INT_EQ(lifecycle.FirstCleanupStatus, 404, "destroy history retained"); TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_UNLOADED, "destroy retry unloaded"); before = fake.EventCount; TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), 0, "destroy retry idempotent"); TEST_ASSERT_UINT_EQ(fake.EventCount, before, "destroy retry no new events"); }
    return 0;
}
static int test_multiple_cleanup_and_reentrancy(void)
{
    VMX_PROCESSOR_IDENTITY ids[4]; VMX_PERSISTENT_CPU_RECORD storage[4]; VMX_PERSISTENT_LIFECYCLE lifecycle; FAKE_LIFECYCLE fake = { 0 }; size_t before; InitIdentities(ids, 4);
    TEST_ASSERT_INT_EQ(Setup(&lifecycle, storage, ids, 4, &fake), 0, "multiple cleanup setup"); TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStart(&lifecycle), 0, "multiple cleanup start"); fake.StopFail = true; fake.FailIndex = SIZE_MAX; fake.ErrorMask = (UINT64_C(1) << 1) | (UINT64_C(1) << 3); TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), 303, "multiple cleanup status"); TEST_ASSERT_UINT_EQ(lifecycle.FirstCleanupIndex, 3, "reverse first cleanup"); TEST_ASSERT_INT_EQ(lifecycle.FirstCleanupStatus, 303, "first cleanup status"); TEST_ASSERT_UINT_EQ(fake.EventCount, 12, "multiple cleanup exact count"); TEST_ASSERT_UINT_EQ(fake.Events[8], 33, "multiple cleanup stop3"); TEST_ASSERT_UINT_EQ(fake.Events[9], 32, "multiple cleanup stop2"); TEST_ASSERT_UINT_EQ(fake.Events[10], 31, "multiple cleanup stop1"); TEST_ASSERT_UINT_EQ(fake.Events[11], 30, "multiple cleanup stop0"); fake.StopFail = false; before = fake.EventCount; TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), 0, "multiple cleanup retry current"); TEST_ASSERT_UINT_EQ(fake.EventCount, before + 2, "multiple cleanup residual only"); TEST_ASSERT_INT_EQ(lifecycle.FirstCleanupStatus, 303, "multiple cleanup history"); TEST_ASSERT_UINT_EQ(lifecycle.FirstCleanupIndex, 3, "multiple cleanup history index"); TEST_ASSERT_UINT_EQ(lifecycle.State, VMX_PERSISTENT_UNLOADED, "multiple cleanup retry unloaded"); before = fake.EventCount; TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), 0, "multiple cleanup idempotent"); TEST_ASSERT_UINT_EQ(fake.EventCount, before, "multiple cleanup no new events");
    fake = (FAKE_LIFECYCLE){ 0 }; TEST_ASSERT_INT_EQ(Setup(&lifecycle, storage, ids, 4, &fake), 0, "reentrant setup"); fake.Lifecycle = &lifecycle; fake.ReenterStart = true; TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStart(&lifecycle), 0, "reentrant start"); TEST_ASSERT_INT_EQ(fake.ReenterResult, VMX_PERSISTENT_STATUS_BUSY, "reentrant start busy"); TEST_ASSERT_UINT_EQ(fake.EventCount, 8, "reentrant start no nested events"); fake.Reentered = false; fake.ReenterStart = false; fake.ReenterStop = true; TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), 0, "reentrant stop"); TEST_ASSERT_INT_EQ(fake.ReenterResult, VMX_PERSISTENT_STATUS_BUSY, "reentrant stop busy"); TEST_ASSERT_UINT_EQ(fake.EventCount, 12, "reentrant stop no nested events");
    return 0;
}
static int test_large_dynamic_identity(void)
{
    enum { count = 40 }; VMX_PROCESSOR_IDENTITY ids[count]; VMX_PERSISTENT_CPU_RECORD storage[count]; VMX_PERSISTENT_LIFECYCLE lifecycle; FAKE_LIFECYCLE fake = { 0 }; size_t i; size_t capacity = count * 3; int* events = (int*)calloc(capacity, sizeof(*events)); TEST_ASSERT_TRUE(events != NULL, "large event allocation"); if (events == NULL) return 1;
    InitIdentities(ids, count); fake.Events = events; fake.EventCapacity = capacity; TEST_ASSERT_INT_EQ(Setup(&lifecycle, storage, ids, count, &fake), 0, "large setup"); TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStart(&lifecycle), 0, "large start"); TEST_ASSERT_INT_EQ(VmxPersistentLifecycleStop(&lifecycle), 0, "large stop"); TEST_ASSERT_UINT_EQ(lifecycle.Count, count, "large count"); TEST_ASSERT_UINT_EQ(fake.EventCount, capacity, "large exact events"); for (i = 0; i < count; ++i) { TEST_ASSERT_TRUE(VmxProcessorIdentityEqual(&storage[i].Identity, &ids[i]), "large identity"); TEST_ASSERT_UINT_EQ(fake.Events[i], 10 + i, "large prepare events"); TEST_ASSERT_UINT_EQ(fake.Events[count + i], 20 + i, "large activate events"); TEST_ASSERT_UINT_EQ(fake.Events[capacity - 1 - i], 30 + i, "large reverse stop events"); } free(events);
    return 0;
}
TEST_MAIN_BEGIN()
    TEST_RUN(test_success_and_reverse_stop);
    TEST_RUN(test_invalid_preserves_output);
    TEST_RUN(test_prepare_failure_and_activate_phases);
    TEST_RUN(test_residual_cleanup_retry);
    TEST_RUN(test_activation_invariants);
    TEST_RUN(test_identity_mutation_and_unsafe_stop);
    TEST_RUN(test_stop_error_clears_flags);
    TEST_RUN(test_destroy_failure_restores_ownership);
    TEST_RUN(test_prepare_failure_matrix);
    TEST_RUN(test_activate_failure_matrix);
    TEST_RUN(test_stop_failure_matrix);
    TEST_RUN(test_destroy_failure_matrix);
    TEST_RUN(test_multiple_cleanup_and_reentrancy);
    TEST_RUN(test_large_dynamic_identity);
TEST_MAIN_END()
