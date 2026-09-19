#include "vmx_ept_runtime_root_plan.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "vmx_persistent_context.h"
#include "vmx_persistent_activate.h"
#include "vmx_persistent_lifecycle.h"

#define ROOT_CONTEXT_RESOURCES (VMX_PERSISTENT_RESOURCE_LAUNCH | \
    VMX_PERSISTENT_RESOURCE_STACK | VMX_PERSISTENT_RESOURCE_GUEST_STACK | \
    VMX_PERSISTENT_RESOURCE_VMXON | VMX_PERSISTENT_RESOURCE_VMCS | \
    VMX_PERSISTENT_RESOURCE_MSR_BITMAP)
#define ROOT_RECORD_OWNERS (VMX_PERSISTENT_OWNER_CONTEXT | VMX_PERSISTENT_OWNER_RUNTIME)

static int Separate(const void* first, size_t first_size,
    const void* second, size_t second_size)
{
    uintptr_t a = (uintptr_t)first, b = (uintptr_t)second;
    return first != NULL && second != NULL && first_size != 0 &&
        second_size != 0 && a <= UINTPTR_MAX - first_size &&
        b <= UINTPTR_MAX - second_size &&
        (a + first_size <= b || b + second_size <= a);
}

static int RootPhysical(uint64_t value)
{
    return value != 0 &&
        (value & (VMX_PERSISTENT_REGION_SIZE - 1u)) == 0;
}

VMX_EPT_RUNTIME_ROOT_PLAN_STATUS VmxEptRuntimeRootPlanBuild(
    const VMX_PERSISTENT_ROOT_PROOF* proof,
    VMX_EPT_RUNTIME_ROOT_PLAN* out_plan)
{
    VMX_PERSISTENT_ROOT_PROOF snapshot;
    VMX_EPT_RUNTIME_ROOT_PLAN staged;

    if (!Separate(proof, sizeof(*proof), out_plan, sizeof(*out_plan)))
        return VMX_EPT_RUNTIME_ROOT_PLAN_INVALID;
    memcpy(&snapshot, proof, sizeof(snapshot));
    if (snapshot.Version != VMX_PERSISTENT_ROOT_PROOF_VERSION ||
        snapshot.Identity.Reserved != 0 ||
        snapshot.Identity.Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER ||
        snapshot.Reserved[0] != 0 || snapshot.Reserved[1] != 0 ||
        !RootPhysical(snapshot.VmxonPhysicalAddress) ||
        !RootPhysical(snapshot.VmcsPhysicalAddress) ||
        snapshot.VmxonPhysicalAddress == snapshot.VmcsPhysicalAddress ||
        snapshot.ContextVersion != VMX_PERSISTENT_CONTEXT_VERSION ||
        snapshot.ContextPublished > 1 || snapshot.VmxActive > 1 ||
        snapshot.VmxCurrent > 1 || snapshot.RecordActive > 1 ||
        snapshot.CleanupRequired > 1 || snapshot.CurrentVmcsValid > 1 ||
        snapshot.OperationsAuthenticated > 1)
        return VMX_EPT_RUNTIME_ROOT_PLAN_INVALID;
    if (snapshot.ContextOwnedResources != ROOT_CONTEXT_RESOURCES ||
        (snapshot.RecordOwnedMask & ~ROOT_RECORD_OWNERS) != 0 ||
        snapshot.CleanupRequired != 0)
        return VMX_EPT_RUNTIME_ROOT_PLAN_DIRTY;
    if (snapshot.ContextPublished != 1 || snapshot.VmxActive != 1 ||
        snapshot.VmxCurrent != 1 ||
        snapshot.RecordStage != VMX_PERSISTENT_CPU_ACTIVE ||
        snapshot.RecordOwnedMask != ROOT_RECORD_OWNERS ||
        snapshot.RecordActive != 1 || snapshot.CurrentVmcsValid != 1 ||
        snapshot.OperationsAuthenticated != 1 ||
        snapshot.CurrentVmcsPhysicalAddress != snapshot.VmcsPhysicalAddress)
        return VMX_EPT_RUNTIME_ROOT_PLAN_NOT_ACTIVE;

    memset(&staged, 0, sizeof(staged));
    staged.Version = VMX_EPT_RUNTIME_ROOT_PLAN_VERSION;
    staged.Identity = snapshot.Identity;
    staged.VmxonPhysicalAddress = snapshot.VmxonPhysicalAddress;
    staged.VmcsPhysicalAddress = snapshot.VmcsPhysicalAddress;
    staged.AdoptExistingRoot = 1;
    staged.NativeEnterRequired = 0;
    *out_plan = staged;
    return VMX_EPT_RUNTIME_ROOT_PLAN_OK;
}
