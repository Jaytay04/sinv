/*
 * vmx_runtime_context.c — Runtime V2 launch context helpers and lifecycle
 *
 * Pure implementation using an injectable VMX_ALLOCATION_INTERFACE.
 * No WDK dependency — testable in user mode with fake allocators.
 */

#include "vmx_runtime_context.h"

#include <string.h>  /* memset */

/*---------------------------------------------------------------------------
 * Pure helpers
 *---------------------------------------------------------------------------*/

uint64_t VmxAlignDown16(uint64_t value)
{
    return value & ~((uint64_t)16 - 1);
}

bool VmxIsAddressInStack(uint64_t addr, const void* stack_base, size_t stack_size)
{
    uint64_t base = (uint64_t)(uintptr_t)stack_base;
    uint64_t size = (uint64_t)stack_size;
    uint64_t end;

    if (stack_base == NULL || size == 0 ||
        base > UINT64_MAX - size) {
        return false;
    }
    end = base + size;
    return addr >= base && addr < end;
}

static bool VmxComputeHostStackLayout(
    const void* stack_base,
    uint64_t* stack_top,
    uint64_t* host_rsp)
{
    uint64_t base;
    uint64_t top;
    uint64_t aligned_top;

    if (stack_base == NULL || stack_top == NULL || host_rsp == NULL) {
        return false;
    }
    base = (uint64_t)(uintptr_t)stack_base;
    if (!VmxIsAddressCanonical(base) ||
        base > UINT64_MAX - VMX_DEDICATED_HOST_STACK_SIZE) {
        return false;
    }
    top = base + VMX_DEDICATED_HOST_STACK_SIZE;
    aligned_top = VmxAlignDown16(top);
    if (aligned_top < VMX_HOST_STACK_SLOT_SIZE) {
        return false;
    }
    *host_rsp = aligned_top - VMX_HOST_STACK_SLOT_SIZE;
    if (!VmxIsAddressCanonical(top) ||
        !VmxIsAddressCanonical(*host_rsp) ||
        !VmxIsAddressInStack(*host_rsp, stack_base,
                             VMX_DEDICATED_HOST_STACK_SIZE)) {
        return false;
    }
    *stack_top = top;
    return true;
}

static bool VmxComputeHostStackLayoutFromTop(
    uint64_t stack_top,
    uint64_t* stack_base,
    uint64_t* host_rsp)
{
    uint64_t base;
    uint64_t aligned_top;

    if (stack_base == NULL || host_rsp == NULL ||
        !VmxIsAddressCanonical(stack_top) ||
        stack_top < VMX_DEDICATED_HOST_STACK_SIZE) {
        return false;
    }
    base = stack_top - VMX_DEDICATED_HOST_STACK_SIZE;
    aligned_top = VmxAlignDown16(stack_top);
    if (!VmxIsAddressCanonical(base) ||
        aligned_top < VMX_HOST_STACK_SLOT_SIZE) {
        return false;
    }
    *host_rsp = aligned_top - VMX_HOST_STACK_SLOT_SIZE;
    if (!VmxIsAddressCanonical(*host_rsp) ||
        !VmxIsAddressInStack(*host_rsp, (const void*)(uintptr_t)base,
                             VMX_DEDICATED_HOST_STACK_SIZE)) {
        return false;
    }
    *stack_base = base;
    return true;
}
VMX_RUNTIME_LAUNCH_CONTEXT* VmxRuntimeContextFromAssembly(
    VMX_LAUNCH_CONTEXT* inner)
{
    uint64_t stack_base;
    uint64_t expected_host_rsp;

    if (inner == NULL ||
        inner->Magic != VMX_LAUNCH_CONTEXT_MAGIC ||
        inner->Size != VMX_LAUNCH_CONTEXT_SIZE ||
        inner->Version != VMX_LAUNCH_ABI_VERSION ||
        !VmxComputeHostStackLayoutFromTop(inner->HostStackTop,
                                           &stack_base,
                                           &expected_host_rsp) ||
        inner->HostRsp != expected_host_rsp) {
        return NULL;
    }

    /*
     * The validation above touches only the fixed 352-byte inner ABI.
     * Callers must supply a complete outer object before using the cast.
     */
    return (VMX_RUNTIME_LAUNCH_CONTEXT*)inner;
}
/*---------------------------------------------------------------------------
 * VmxLaunchCreateContextInternal
 *---------------------------------------------------------------------------*/
bool VmxLaunchCreateContextInternal(
    uint64_t vmx_basic,
    const VMX_ALLOCATION_INTERFACE* alloc,
    VMX_EXIT_DISPATCH_ROUTINE exit_dispatcher,
    VMX_RUNTIME_LAUNCH_CONTEXT** out_context)
{
    VMX_BASIC_CAPABILITIES caps;
    VMX_RUNTIME_LAUNCH_CONTEXT* ctx = NULL;
    void* host_stack = NULL;
    void* vmxon_region = NULL;
    void* vmcs_region = NULL;
    size_t required_region_size;

    if (out_context == NULL || alloc == NULL) {
        if (out_context != NULL) *out_context = NULL;
        return false;
    }
    *out_context = NULL;

    if (!VmxParseBasicCapabilities(vmx_basic, &caps)) {
        return false;
    }

    /* 1. Allocate context */
    ctx = (VMX_RUNTIME_LAUNCH_CONTEXT*)alloc->AllocatePool(
        sizeof(VMX_RUNTIME_LAUNCH_CONTEXT));
    if (ctx == NULL) return false;
    (void)memset(ctx, 0, sizeof(VMX_RUNTIME_LAUNCH_CONTEXT));
    ctx->OwnedResources |= VMX_RESOURCE_CONTEXT_BIT;
    ctx->VmxBasic = vmx_basic;
    ctx->RevisionId = caps.revision_id;
    ctx->VmcsSize = caps.vmcs_size;
    required_region_size = caps.vmcs_size > VMX_REGION_MIN_SIZE
        ? (size_t)caps.vmcs_size : VMX_REGION_MIN_SIZE;

    /* 2. Allocate host stack */
    host_stack = alloc->AllocatePool(VMX_DEDICATED_HOST_STACK_SIZE);
    if (host_stack == NULL) {
        VmxLaunchDestroyContextInternal(alloc, ctx);
        *out_context = NULL;
        return false;
    }
    (void)memset(host_stack, 0, VMX_DEDICATED_HOST_STACK_SIZE);
    ctx->HostStack = host_stack;
    ctx->OwnedResources |= VMX_RESOURCE_HOST_STACK_BIT;

    /* 3. Allocate and validate VMXON region */
    vmxon_region = alloc->AllocateContiguous(required_region_size, caps.HighestPhysicalAddress);
    if (vmxon_region == NULL) {
        VmxLaunchDestroyContextInternal(alloc, ctx);
        *out_context = NULL;
        return false;
    }
    ctx->VmxonRegion = vmxon_region;
    ctx->OwnedResources |= VMX_RESOURCE_VMXON_REGION_BIT;

    if (!VmxInitializeRegion(vmxon_region, caps.revision_id, required_region_size)) {
        VmxLaunchDestroyContextInternal(alloc, ctx);
        *out_context = NULL;
        return false;
    }
    ctx->VmxonPa = alloc->GetPhysicalAddress(vmxon_region);

    if (VmxValidateRegionContract(&caps, vmxon_region, ctx->VmxonPa,
                                  required_region_size) != VMX_REGION_VALID) {
        VmxLaunchDestroyContextInternal(alloc, ctx);
        *out_context = NULL;
        return false;
    }

    /* 4. Allocate and validate VMCS region */
    vmcs_region = alloc->AllocateContiguous(required_region_size, caps.HighestPhysicalAddress);
    if (vmcs_region == NULL) {
        VmxLaunchDestroyContextInternal(alloc, ctx);
        *out_context = NULL;
        return false;
    }
    ctx->VmcsRegion = vmcs_region;
    ctx->OwnedResources |= VMX_RESOURCE_VMCS_REGION_BIT;

    if (!VmxInitializeRegion(vmcs_region, caps.revision_id, required_region_size)) {
        VmxLaunchDestroyContextInternal(alloc, ctx);
        *out_context = NULL;
        return false;
    }
    ctx->VmcsPa = alloc->GetPhysicalAddress(vmcs_region);

    if (VmxValidateRegionContract(&caps, vmcs_region, ctx->VmcsPa,
                                  required_region_size) != VMX_REGION_VALID) {
        VmxLaunchDestroyContextInternal(alloc, ctx);
        *out_context = NULL;
        return false;
    }

    /* Compute and validate HostRsp without overflowing the stack range. */
    uint64_t stack_top;
    uint64_t host_rsp;

    if (!VmxComputeHostStackLayout(host_stack, &stack_top, &host_rsp)) {
        VmxLaunchDestroyContextInternal(alloc, ctx);
        *out_context = NULL;
        return false;
    }
    /* Publish context through reserved host-stack slot */
    ctx->Assembly.HostRsp = host_rsp;
    *((VMX_RUNTIME_LAUNCH_CONTEXT**)(uintptr_t)host_rsp) = ctx;
    ctx->Assembly.HostStackTop = stack_top;

    /* Initialise ABI fields required by MASM */
    ctx->Assembly.Magic   = VMX_LAUNCH_CONTEXT_MAGIC;
    ctx->Assembly.Size    = VMX_LAUNCH_CONTEXT_SIZE;
    ctx->Assembly.Version = VMX_LAUNCH_ABI_VERSION;
    ctx->Assembly.ExitDispatcher = exit_dispatcher;

    *out_context = ctx;
    return true;
}

/*---------------------------------------------------------------------------
 * VmxLaunchDestroyContextInternal
 *---------------------------------------------------------------------------*/
void VmxLaunchDestroyContextInternal(
    const VMX_ALLOCATION_INTERFACE* alloc,
    VMX_RUNTIME_LAUNCH_CONTEXT* context)
{
    if (context == NULL || alloc == NULL) return;

    if ((context->OwnedResources & VMX_RESOURCE_HOST_STACK_BIT) != 0 &&
        context->HostStack != NULL) {
        uint64_t stack_top;
        uint64_t expected_host_rsp;
        if (VmxComputeHostStackLayout(context->HostStack, &stack_top,
                                      &expected_host_rsp) &&
            context->Assembly.HostRsp == expected_host_rsp) {
            *((VMX_RUNTIME_LAUNCH_CONTEXT**)(uintptr_t)expected_host_rsp) = NULL;
        }
    }
    if ((context->OwnedResources & VMX_RESOURCE_HOST_STACK_BIT) != 0) {
        alloc->FreePool(context->HostStack);
        context->HostStack = NULL;
        context->OwnedResources &= ~VMX_RESOURCE_HOST_STACK_BIT;
    }
    if ((context->OwnedResources & VMX_RESOURCE_VMCS_REGION_BIT) != 0) {
        alloc->FreeContiguous(context->VmcsRegion);
        context->VmcsRegion = NULL;
        context->OwnedResources &= ~VMX_RESOURCE_VMCS_REGION_BIT;
    }
    if ((context->OwnedResources & VMX_RESOURCE_VMXON_REGION_BIT) != 0) {
        alloc->FreeContiguous(context->VmxonRegion);
        context->VmxonRegion = NULL;
        context->OwnedResources &= ~VMX_RESOURCE_VMXON_REGION_BIT;
    }
    if ((context->OwnedResources & VMX_RESOURCE_CONTEXT_BIT) != 0) {
        alloc->FreePool(context);
    }
}
