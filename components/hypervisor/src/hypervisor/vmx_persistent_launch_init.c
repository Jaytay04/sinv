#include "vmx_persistent_launch_init.h"
#include <limits.h>
#include <string.h>
#define VMX_PERSISTENT_LAUNCH_LOW_MAX ((uintptr_t)UINT64_C(0x00007FFFFFFFFFFF))
#define VMX_PERSISTENT_LAUNCH_HIGH_MIN ((uintptr_t)UINT64_C(0xFFFF800000000000))
#define VMX_PERSISTENT_LAUNCH_REQUIRED_RESOURCES \
    (VMX_PERSISTENT_RESOURCE_LAUNCH | VMX_PERSISTENT_RESOURCE_STACK | \
     VMX_PERSISTENT_RESOURCE_GUEST_STACK | VMX_PERSISTENT_RESOURCE_VMXON | \
     VMX_PERSISTENT_RESOURCE_VMCS | VMX_PERSISTENT_RESOURCE_MSR_BITMAP)
static int VmxPersistentLaunchCanonical(uintptr_t address)
{
    return address <= VMX_PERSISTENT_LAUNCH_LOW_MAX ||
           address >= VMX_PERSISTENT_LAUNCH_HIGH_MIN;
}
static int VmxPersistentLaunchRange(uintptr_t base, size_t size,
                                    uintptr_t* end)
{
    uintptr_t last;
    int low;
    if (size == 0 || (uintptr_t)size > UINTPTR_MAX - base)
        return 0;
    *end = base + (uintptr_t)size;
    if (*end <= base)
        return 0;
    last = *end - 1;
    low = base <= VMX_PERSISTENT_LAUNCH_LOW_MAX;
    if (!VmxPersistentLaunchCanonical(base) ||
        !VmxPersistentLaunchCanonical(last) ||
        !VmxPersistentLaunchCanonical(*end))
        return 0;
    return low ? last <= VMX_PERSISTENT_LAUNCH_LOW_MAX
               : base >= VMX_PERSISTENT_LAUNCH_HIGH_MIN &&
                 last >= VMX_PERSISTENT_LAUNCH_HIGH_MIN;
}
static int VmxPersistentLaunchOverlap(uintptr_t left, uintptr_t left_end,
                                      uintptr_t right, uintptr_t right_end)
{
    return left < right_end && right < left_end;
}
VMX_PERSISTENT_LAUNCH_INIT_STATUS VmxPersistentLaunchStorageInitialize(
    VMX_PERSISTENT_CONTEXT* context,
    uint64_t cookie,
    VMX_V3D_PERSISTENT_DISPATCHER dispatcher,
    void* dispatcher_context,
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT** out_launch)
{
    uintptr_t storage, storage_end, host, host_end, guest, guest_end;
    uintptr_t host_top, guest_top, host_rsp, guest_rsp;
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch;
    if (context == NULL || out_launch == NULL || cookie == 0 ||
        dispatcher == NULL || dispatcher_context == NULL)
        return VMX_PERSISTENT_LAUNCH_INIT_INVALID;
    if (context->Version != VMX_PERSISTENT_CONTEXT_VERSION ||
        !context->Published ||
        (context->OwnedResources & VMX_PERSISTENT_LAUNCH_REQUIRED_RESOURCES) !=
            VMX_PERSISTENT_LAUNCH_REQUIRED_RESOURCES)
        return VMX_PERSISTENT_LAUNCH_INIT_INVALID;
    storage = (uintptr_t)context->LaunchStorage;
    host = (uintptr_t)context->HostStack;
    guest = (uintptr_t)context->GuestStack;
    if (context->LaunchStorage == NULL ||
        context->LaunchStorageSize < VMX_V3D_CONTEXT_SIZE)
        return VMX_PERSISTENT_LAUNCH_INIT_STORAGE_INVALID;
    if ((storage & (VMX_V3D_PERSISTENT_CONTEXT_ALIGNMENT - 1u)) != 0 ||
        !VmxPersistentLaunchRange(storage, VMX_V3D_CONTEXT_SIZE,
                                  &storage_end))
        return VMX_PERSISTENT_LAUNCH_INIT_ALIGNMENT_INVALID;
    if (context->HostStack == NULL || context->GuestStack == NULL ||
        context->HostStackSize < VMX_PERSISTENT_HOST_STACK_SIZE ||
        context->GuestStackSize < VMX_PERSISTENT_GUEST_STACK_SIZE)
        return VMX_PERSISTENT_LAUNCH_INIT_RANGE_INVALID;
    if ((host & (VMX_V3D_X64_STACK_ALIGNMENT - 1u)) != 0 ||
        (guest & (VMX_V3D_X64_STACK_ALIGNMENT - 1u)) != 0 ||
        !VmxPersistentLaunchRange(host, context->HostStackSize, &host_end) ||
        !VmxPersistentLaunchRange(guest, context->GuestStackSize, &guest_end))
        return VMX_PERSISTENT_LAUNCH_INIT_ALIGNMENT_INVALID;
    if (VmxPersistentLaunchOverlap(host, host_end, guest, guest_end) ||
        VmxPersistentLaunchOverlap(host, host_end, storage, storage_end) ||
        VmxPersistentLaunchOverlap(guest, guest_end, storage, storage_end))
        return VMX_PERSISTENT_LAUNCH_INIT_OVERLAP;
    host_top = host_end;
    guest_top = guest_end;
    if (host_top < VMX_V3D_X64_REGISTER_SIZE ||
        guest_top < VMX_V3D_X64_REGISTER_SIZE)
        return VMX_PERSISTENT_LAUNCH_INIT_ALIGNMENT_INVALID;
    host_rsp = host_top - VMX_V3D_X64_REGISTER_SIZE;
    guest_rsp = guest_top - VMX_V3D_X64_REGISTER_SIZE;
    if ((host_top & (VMX_V3D_X64_STACK_ALIGNMENT - 1u)) != 0 ||
        (guest_top & (VMX_V3D_X64_STACK_ALIGNMENT - 1u)) != 0 ||
        (host_rsp & (VMX_V3D_X64_STACK_ALIGNMENT - 1u)) != 8u ||
        (guest_rsp & (VMX_V3D_X64_STACK_ALIGNMENT - 1u)) != 8u ||
        !VmxPersistentLaunchCanonical(host_top) ||
        !VmxPersistentLaunchCanonical(guest_top) ||
        !VmxPersistentLaunchCanonical(host_rsp) ||
        !VmxPersistentLaunchCanonical(guest_rsp))
        return VMX_PERSISTENT_LAUNCH_INIT_ALIGNMENT_INVALID;
    if (context->VmxonPhysicalAddress == 0 ||
        context->VmcsPhysicalAddress == 0 ||
        (context->VmxonPhysicalAddress % VMX_V3D_X64_PAGE_SIZE) != 0 ||
        (context->VmcsPhysicalAddress % VMX_V3D_X64_PAGE_SIZE) != 0 ||
        context->VmxonPhysicalAddress == context->VmcsPhysicalAddress)
        return VMX_PERSISTENT_LAUNCH_INIT_INVALID;
    launch = (VMX_V3D_PERSISTENT_LAUNCH_CONTEXT*)context->LaunchStorage;
    memset(launch, 0, VMX_V3D_CONTEXT_SIZE);
    launch->Header.Magic = VMX_V3D_PERSISTENT_LAUNCH_MAGIC;
    launch->Header.Size = VMX_V3D_CONTEXT_SIZE;
    launch->Header.Version = VMX_V3D_PERSISTENT_LAUNCH_VERSION;
    launch->HostStackTop = host_top;
    launch->HostRsp = host_rsp;
    launch->GuestStackTop = guest_top;
    launch->GuestRsp = guest_rsp;
    launch->GuestRflags = 2u;
    launch->Cookie = cookie;
    launch->Dispatcher = dispatcher;
    launch->DispatcherContext = dispatcher_context;
    launch->VmxonPhysicalAddress = context->VmxonPhysicalAddress;
    launch->VmcsPhysicalAddress = context->VmcsPhysicalAddress;
    launch->ProtocolStep = VMX_V3D_PROTOCOL_INITIALIZED;
    launch->AssemblyAction = VMX_V3D_ASSEMBLY_ACTION_INVALID;
    launch->ValidationFlags = VMX_V3D_VALIDATION_HEADER_OK;
    *out_launch = launch;
    return VMX_PERSISTENT_LAUNCH_INIT_OK;
}
