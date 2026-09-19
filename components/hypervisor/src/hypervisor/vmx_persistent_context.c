#include "vmx_persistent_context.h"
#include "vmx_msr_bitmap_policy.h"
#include <string.h>
static bool VmxPersistentAllocatorValid(
    const VMX_PERSISTENT_ALLOCATOR* allocator)
{
    return allocator != NULL && allocator->AllocatePool != NULL &&
           allocator->FreePool != NULL &&
           allocator->AllocateContiguous != NULL &&
           allocator->FreeContiguous != NULL &&
           allocator->GetPhysicalAddress != NULL;
}
static size_t VmxPersistentRegionSize(const VMX_BASIC_CAPABILITIES* caps)
{
    return caps->vmcs_size > VMX_PERSISTENT_REGION_SIZE
        ? (size_t)caps->vmcs_size : (size_t)VMX_PERSISTENT_REGION_SIZE;
}
/* The staged object owns no caller-visible context object. */
typedef void (*VmxPersistentFreeFn)(void*, void*);
typedef struct _VMX_PERSISTENT_RELEASED {
    void* Memory;
    VmxPersistentFreeFn Free;
} VMX_PERSISTENT_RELEASED;
static void VmxPersistentRelease(
    const VMX_PERSISTENT_ALLOCATOR* allocator, void* memory,
    VmxPersistentFreeFn free_fn, VMX_PERSISTENT_RELEASED* released,
    size_t* released_count)
{
    size_t i;
    if (memory == NULL) return;
    for (i = 0; i < *released_count; ++i)
        if (released[i].Memory == memory && released[i].Free == free_fn) return;
    released[*released_count].Memory = memory;
    released[*released_count].Free = free_fn;
    ++*released_count;
    free_fn(allocator->Context, memory);
}
/* Reverse order; dedupe only identical (pointer, callback-family) releases. */
static void VmxPersistentUnwind(
    const VMX_PERSISTENT_ALLOCATOR* allocator,
    VMX_PERSISTENT_CONTEXT* staged)
{
    VMX_PERSISTENT_RELEASED released[6];
    size_t released_count = 0;

    if ((staged->OwnedResources & VMX_PERSISTENT_RESOURCE_MSR_BITMAP) != 0) {
        VmxPersistentRelease(allocator, staged->MsrBitmap,
                             allocator->FreeContiguous, released,
                             &released_count);
        staged->MsrBitmap = NULL; staged->MsrBitmapPhysicalAddress = 0;
        staged->OwnedResources &= ~VMX_PERSISTENT_RESOURCE_MSR_BITMAP;
    }
    if ((staged->OwnedResources & VMX_PERSISTENT_RESOURCE_VMCS) != 0) {
        VmxPersistentRelease(allocator, staged->VmcsRegion,
                             allocator->FreeContiguous, released,
                             &released_count);
        staged->VmcsRegion = NULL; staged->VmcsPhysicalAddress = 0;
        staged->OwnedResources &= ~VMX_PERSISTENT_RESOURCE_VMCS;
    }
    if ((staged->OwnedResources & VMX_PERSISTENT_RESOURCE_VMXON) != 0) {
        VmxPersistentRelease(allocator, staged->VmxonRegion,
                             allocator->FreeContiguous, released,
                             &released_count);
        staged->VmxonRegion = NULL; staged->VmxonPhysicalAddress = 0;
        staged->OwnedResources &= ~VMX_PERSISTENT_RESOURCE_VMXON;
    }
    if ((staged->OwnedResources & VMX_PERSISTENT_RESOURCE_GUEST_STACK) != 0) {
        VmxPersistentRelease(allocator, staged->GuestStack,
                             allocator->FreePool, released, &released_count);
        staged->GuestStack = NULL; staged->GuestStackSize = 0;
        staged->OwnedResources &= ~VMX_PERSISTENT_RESOURCE_GUEST_STACK;
    }
    if ((staged->OwnedResources & VMX_PERSISTENT_RESOURCE_STACK) != 0) {
        VmxPersistentRelease(allocator, staged->HostStack,
                             allocator->FreePool, released, &released_count);
        staged->HostStack = NULL; staged->HostStackSize = 0;
        staged->OwnedResources &= ~VMX_PERSISTENT_RESOURCE_STACK;
    }
    if ((staged->OwnedResources & VMX_PERSISTENT_RESOURCE_LAUNCH) != 0) {
        VmxPersistentRelease(allocator, staged->LaunchStorage,
                             allocator->FreePool, released, &released_count);
        staged->LaunchStorage = NULL; staged->LaunchStorageSize = 0;
        staged->OwnedResources &= ~VMX_PERSISTENT_RESOURCE_LAUNCH;
    }
}
static bool VmxPersistentIdentityValid(const VMX_PROCESSOR_IDENTITY* identity)
{
    return identity != NULL && identity->Number < VMX_PROCESSOR_GROUP_MAX_NUMBER;
}
static bool VmxPersistentRegionAliases(const VMX_PERSISTENT_CONTEXT* staged)
{
    return (staged->MsrBitmap != NULL &&
            (staged->MsrBitmap == staged->VmxonRegion ||
             staged->MsrBitmap == staged->VmcsRegion ||
             staged->MsrBitmap == staged->LaunchStorage ||
             staged->MsrBitmap == staged->HostStack ||
             staged->MsrBitmap == staged->GuestStack)) ||
           staged->VmxonRegion == staged->VmcsRegion ||
           staged->VmxonRegion == staged->LaunchStorage ||
           staged->VmxonRegion == staged->HostStack ||
           staged->VmxonRegion == staged->GuestStack ||
           staged->VmcsRegion == staged->LaunchStorage ||
           staged->VmcsRegion == staged->HostStack ||
           staged->VmcsRegion == staged->GuestStack ||
           (staged->VmxonRegion != NULL && staged->VmcsRegion != NULL &&
            staged->VmxonPhysicalAddress == staged->VmcsPhysicalAddress) ||
           (staged->MsrBitmapPhysicalAddress != 0 &&
            (staged->MsrBitmapPhysicalAddress ==
                 staged->VmxonPhysicalAddress ||
             staged->MsrBitmapPhysicalAddress ==
                 staged->VmcsPhysicalAddress));
}

static bool VmxPersistentBitmapPhysicalValid(
    const VMX_BASIC_CAPABILITIES* caps,
    uint64_t physical_address)
{
    return caps != NULL && physical_address != 0 &&
           (physical_address & (VMX_MSR_BITMAP_SIZE - 1u)) == 0 &&
           physical_address <= caps->HighestPhysicalAddress &&
           physical_address <= UINT64_MAX - (VMX_MSR_BITMAP_SIZE - 1u) &&
           physical_address + (VMX_MSR_BITMAP_SIZE - 1u) <=
               caps->HighestPhysicalAddress;
}
VMX_PERSISTENT_CONTEXT_STATUS VmxPersistentContextCreate(
    const VMX_PROCESSOR_IDENTITY* identity,
    uint64_t vmx_basic,
    size_t launch_storage_size,
    const VMX_PERSISTENT_ALLOCATOR* allocator,
    VMX_PERSISTENT_CONTEXT* out_context)
{
    VMX_BASIC_CAPABILITIES caps;
    VMX_PERSISTENT_CONTEXT staged;
    size_t region_size;
    if (out_context == NULL || !VmxPersistentIdentityValid(identity) ||
        launch_storage_size == 0 || !VmxPersistentAllocatorValid(allocator)) {
        return out_context == NULL || !VmxPersistentIdentityValid(identity) ||
               launch_storage_size == 0
            ? VMX_PERSISTENT_CONTEXT_INVALID
            : VMX_PERSISTENT_CONTEXT_INVALID_ALLOCATOR;
    }
    if (!VmxParseBasicCapabilities(vmx_basic, &caps)) {
        return VMX_PERSISTENT_CONTEXT_INVALID_CAPABILITIES;
    }
    (void)memset(&staged, 0, sizeof(staged));
    staged.Identity = *identity;
    staged.Capabilities = caps;
    staged.Version = VMX_PERSISTENT_CONTEXT_VERSION;
    staged.LaunchStorageSize = launch_storage_size;
    staged.HostStackSize = VMX_PERSISTENT_HOST_STACK_SIZE;
    staged.LaunchStorage = allocator->AllocatePool(allocator->Context,
                                                    launch_storage_size);
    if (staged.LaunchStorage == NULL) {
        return VMX_PERSISTENT_CONTEXT_ALLOCATION_FAILED;
    }
    staged.OwnedResources |= VMX_PERSISTENT_RESOURCE_LAUNCH;
    (void)memset(staged.LaunchStorage, 0, launch_storage_size);
    staged.HostStack = allocator->AllocatePool(allocator->Context,
                                               VMX_PERSISTENT_HOST_STACK_SIZE);
    if (staged.HostStack == NULL) {
        VmxPersistentUnwind(allocator, &staged);
        return VMX_PERSISTENT_CONTEXT_ALLOCATION_FAILED;
    }
    staged.OwnedResources |= VMX_PERSISTENT_RESOURCE_STACK;
    if (staged.HostStack == staged.LaunchStorage) {
        VmxPersistentUnwind(allocator, &staged);
        return VMX_PERSISTENT_CONTEXT_REGION_INVALID;
    }
    (void)memset(staged.HostStack, 0, VMX_PERSISTENT_HOST_STACK_SIZE);
    staged.GuestStackSize = VMX_PERSISTENT_GUEST_STACK_SIZE;
    staged.GuestStack = allocator->AllocatePool(allocator->Context,
                                                VMX_PERSISTENT_GUEST_STACK_SIZE);
    if (staged.GuestStack == NULL) {
        VmxPersistentUnwind(allocator, &staged);
        return VMX_PERSISTENT_CONTEXT_ALLOCATION_FAILED;
    }
    staged.OwnedResources |= VMX_PERSISTENT_RESOURCE_GUEST_STACK;
    if (staged.GuestStack == staged.HostStack ||
        staged.GuestStack == staged.LaunchStorage) {
        VmxPersistentUnwind(allocator, &staged);
        return VMX_PERSISTENT_CONTEXT_REGION_INVALID;
    }
    (void)memset(staged.GuestStack, 0, VMX_PERSISTENT_GUEST_STACK_SIZE);
    region_size = VmxPersistentRegionSize(&caps);
    staged.VmxonRegion = allocator->AllocateContiguous(
        allocator->Context, region_size, caps.HighestPhysicalAddress);
    if (staged.VmxonRegion == NULL) {
        VmxPersistentUnwind(allocator, &staged);
        return VMX_PERSISTENT_CONTEXT_ALLOCATION_FAILED;
    }
    staged.OwnedResources |= VMX_PERSISTENT_RESOURCE_VMXON;
    if (VmxPersistentRegionAliases(&staged)) {
        VmxPersistentUnwind(allocator, &staged);
        return VMX_PERSISTENT_CONTEXT_REGION_INVALID;
    }
    if (!VmxInitializeRegion(staged.VmxonRegion, caps.revision_id,
                             region_size)) {
        VmxPersistentUnwind(allocator, &staged);
        return VMX_PERSISTENT_CONTEXT_REGION_INVALID;
    }
    staged.VmxonPhysicalAddress = allocator->GetPhysicalAddress(
        allocator->Context, staged.VmxonRegion);
    if (VmxValidateRegionContract(&caps, staged.VmxonRegion,
                                  staged.VmxonPhysicalAddress,
                                  region_size) != VMX_REGION_VALID) {
        VmxPersistentUnwind(allocator, &staged);
        return VMX_PERSISTENT_CONTEXT_REGION_INVALID;
    }
    staged.VmcsRegion = allocator->AllocateContiguous(
        allocator->Context, region_size, caps.HighestPhysicalAddress);
    if (staged.VmcsRegion == NULL) {
        VmxPersistentUnwind(allocator, &staged);
        return VMX_PERSISTENT_CONTEXT_ALLOCATION_FAILED;
    }
    staged.OwnedResources |= VMX_PERSISTENT_RESOURCE_VMCS;
    if (VmxPersistentRegionAliases(&staged)) {
        VmxPersistentUnwind(allocator, &staged);
        return VMX_PERSISTENT_CONTEXT_REGION_INVALID;
    }
    if (!VmxInitializeRegion(staged.VmcsRegion, caps.revision_id,
                             region_size)) {
        VmxPersistentUnwind(allocator, &staged);
        return VMX_PERSISTENT_CONTEXT_REGION_INVALID;
    }
    staged.VmcsPhysicalAddress = allocator->GetPhysicalAddress(
        allocator->Context, staged.VmcsRegion);
    if (VmxValidateRegionContract(&caps, staged.VmcsRegion,
                                  staged.VmcsPhysicalAddress,
                                  region_size) != VMX_REGION_VALID ||
        VmxPersistentRegionAliases(&staged)) {
        VmxPersistentUnwind(allocator, &staged);
        return VMX_PERSISTENT_CONTEXT_REGION_INVALID;
    }
    staged.MsrBitmap = allocator->AllocateContiguous(
        allocator->Context, VMX_MSR_BITMAP_SIZE,
        caps.HighestPhysicalAddress);
    if (staged.MsrBitmap == NULL) {
        VmxPersistentUnwind(allocator, &staged);
        return VMX_PERSISTENT_CONTEXT_ALLOCATION_FAILED;
    }
    staged.OwnedResources |= VMX_PERSISTENT_RESOURCE_MSR_BITMAP;
    if (((uintptr_t)staged.MsrBitmap & (VMX_MSR_BITMAP_SIZE - 1u)) != 0 ||
        VmxPersistentRegionAliases(&staged) ||
        VmxMsrBitmapPolicyBuild(
            (uint8_t*)staged.MsrBitmap, VMX_MSR_BITMAP_SIZE) !=
                VMX_MSR_BITMAP_POLICY_OK) {
        VmxPersistentUnwind(allocator, &staged);
        return VMX_PERSISTENT_CONTEXT_REGION_INVALID;
    }
    staged.MsrBitmapPhysicalAddress = allocator->GetPhysicalAddress(
        allocator->Context, staged.MsrBitmap);
    if (!VmxPersistentBitmapPhysicalValid(
            &caps, staged.MsrBitmapPhysicalAddress) ||
        VmxPersistentRegionAliases(&staged)) {
        VmxPersistentUnwind(allocator, &staged);
        return VMX_PERSISTENT_CONTEXT_REGION_INVALID;
    }
    staged.ProcessorState.IsValid = false;
    staged.Published = true;
    *out_context = staged;
    return VMX_PERSISTENT_CONTEXT_OK;
}
VMX_PERSISTENT_CONTEXT_STATUS VmxPersistentContextDestroy(
    const VMX_PERSISTENT_ALLOCATOR* allocator,
    VMX_PERSISTENT_CONTEXT* context)
{
    if (context == NULL) {
        return VMX_PERSISTENT_CONTEXT_INVALID;
    }
    if (context->CpuState.VmxActive || context->CpuState.VmxCurrent ||
        context->CpuState.RestorationRequired || context->CpuState.Cr0Changed ||
        context->CpuState.Cr4Changed || context->CpuState.GdtrRestoreRequired ||
        context->CpuState.IdtrRestoreRequired || context->CpuState.AffinityPinned ||
        context->CpuState.IrqlRaised ||
        (context->CpuState.ShutdownRequested && !context->CpuState.ShutdownProven)) {
        return VMX_PERSISTENT_CONTEXT_BUSY;
    }
    if (context->OwnedResources == 0) {
        return VMX_PERSISTENT_CONTEXT_OK;
    }
    if (!VmxPersistentAllocatorValid(allocator)) {
        return VMX_PERSISTENT_CONTEXT_INVALID_ALLOCATOR;
    }
    VmxPersistentUnwind(allocator, context);
    context->Published = false;
    (void)memset(&context->Capabilities, 0, sizeof(context->Capabilities));
    (void)memset(&context->ProcessorState, 0, sizeof(context->ProcessorState));
    (void)memset(&context->CpuState, 0, sizeof(context->CpuState));
    return VMX_PERSISTENT_CONTEXT_OK;
}
