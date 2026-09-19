#include <ntddk.h>
#include "vmx_persistent_runtime_config.h"
#include "vmx_kernel_adapters.h"
#include "vmx_persistent_dispatch.h"
#include "vmx_persistent_launch_abi.h"
#include "vmx.h"

static bool Zero(const void* memory, size_t size)
{
    const unsigned char* bytes = (const unsigned char*)memory;
    size_t index;
    if (memory == NULL) return false;
    for (index = 0; index < size; ++index) if (bytes[index] != 0) return false;
    return true;
}
static bool PlatformEqual(const VMX_PERSISTENT_KERNEL_PLATFORM* left,
                          const VMX_PERSISTENT_KERNEL_PLATFORM* right)
{
    return left->Context == right->Context &&
        left->QueryActiveCount == right->QueryActiveCount &&
        left->MapSystemIndex == right->MapSystemIndex &&
        left->Allocate == right->Allocate && left->Free == right->Free &&
        left->CheckPassive == right->CheckPassive && left->Pin == right->Pin &&
        left->QueryCurrent == right->QueryCurrent &&
        left->Restore == right->Restore;
}
static bool AllocatorEqual(const VMX_PERSISTENT_ALLOCATOR* left,
                           const VMX_PERSISTENT_ALLOCATOR* right)
{
    return left->Context == right->Context &&
        left->AllocatePool == right->AllocatePool &&
        left->FreePool == right->FreePool &&
        left->AllocateContiguous == right->AllocateContiguous &&
        left->FreeContiguous == right->FreeContiguous &&
        left->GetPhysicalAddress == right->GetPhysicalAddress;
}
bool VmxPersistentRuntimeConfigValidate(
    const VMX_PERSISTENT_RUNTIME_CONFIG* config)
{
    const VMX_PERSISTENT_ALLOCATOR* allocator;
    VMX_BASIC_CAPABILITIES capabilities;
    if (config == NULL) return false;
    allocator = VmxKernelPersistentAllocator();
    RtlZeroMemory(&capabilities, sizeof(capabilities));
    return allocator != NULL &&
        PlatformEqual(&config->Kernel, &KernelPersistentPlatform) &&
        AllocatorEqual(&config->Allocator, allocator) &&
        VmxParseBasicCapabilities(config->VmxBasic, &capabilities) &&
        config->LaunchStorageSize == VMX_V3D_CONTEXT_SIZE &&
        config->Cookie != 0 &&
        config->Dispatcher == VmxV3dPersistentDispatch &&
        config->GuestRip ==
            (uint64_t)(uintptr_t)&VmxV3dPersistentGuestContinuation &&
        config->HostRip ==
            (uint64_t)(uintptr_t)&VmxV3dPersistentVmExitEntry;
}
static VMX_PERSISTENT_STATUS BuildApprovedConfig(
    uint64_t vmx_basic,
    bool (*GenerateRandom)(uint8_t* buffer, size_t size),
    VMX_PERSISTENT_RUNTIME_CONFIG* out_config)
{
    const VMX_PERSISTENT_ALLOCATOR* allocator;
    VMX_PERSISTENT_RUNTIME_CONFIG staged;
    VMX_BASIC_CAPABILITIES capabilities;
    uint64_t cookie;

    if (GenerateRandom == NULL || out_config == NULL ||
        !Zero(out_config, sizeof(*out_config)))
        return VMX_PERSISTENT_STATUS_INVALID;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_PERSISTENT_STATUS_NOT_READY;
    RtlZeroMemory(&capabilities, sizeof(capabilities));
    if (!VmxParseBasicCapabilities(vmx_basic, &capabilities))
        return VMX_PERSISTENT_STATUS_NOT_READY;
    cookie = 0;
    if (!GenerateRandom((uint8_t*)&cookie, sizeof(cookie)) ||
        cookie == 0)
        return VMX_PERSISTENT_STATUS_NOT_READY;
    allocator = VmxKernelPersistentAllocator();
    if (allocator == NULL) return VMX_PERSISTENT_STATUS_INVARIANT;
    RtlZeroMemory(&staged, sizeof(staged));
    staged.Kernel = KernelPersistentPlatform;
    staged.Allocator = *allocator;
    staged.VmxBasic = vmx_basic;
    staged.LaunchStorageSize = VMX_V3D_CONTEXT_SIZE;
    staged.Cookie = cookie;
    staged.Dispatcher = VmxV3dPersistentDispatch;
    staged.GuestRip =
        (uint64_t)(uintptr_t)&VmxV3dPersistentGuestContinuation;
    staged.HostRip = (uint64_t)(uintptr_t)&VmxV3dPersistentVmExitEntry;
    if (!VmxPersistentRuntimeConfigValidate(&staged))
        return VMX_PERSISTENT_STATUS_INVARIANT;
    RtlCopyMemory(out_config, &staged, sizeof(staged));
    return VMX_PERSISTENT_STATUS_OK;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeConfigBuildWithSource(
    const VMX_PERSISTENT_RUNTIME_CONFIG_SOURCE* source,
    VMX_PERSISTENT_RUNTIME_CONFIG* out_config)
{
    uint64_t feature_control;
    uint64_t vmx_basic;
    uint64_t cr4;

    if (source == NULL || source->ReadCr4 == NULL ||
        source->CheckCpuidVmx == NULL || source->ReadMsr == NULL ||
        source->GenerateRandom == NULL || out_config == NULL ||
        !Zero(out_config, sizeof(*out_config)))
        return VMX_PERSISTENT_STATUS_INVALID;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_PERSISTENT_STATUS_NOT_READY;
    cr4 = source->ReadCr4();
    if ((cr4 & CR4_CET) != 0 || !source->CheckCpuidVmx())
        return VMX_PERSISTENT_STATUS_NOT_READY;
    if (!source->ReadMsr(IA32_FEATURE_CONTROL, &feature_control) ||
        (feature_control & (FEATURE_CONTROL_LOCK_BIT |
          FEATURE_CONTROL_VMXON_OUTSIDE_SMX)) !=
          (FEATURE_CONTROL_LOCK_BIT | FEATURE_CONTROL_VMXON_OUTSIDE_SMX))
        return VMX_PERSISTENT_STATUS_NOT_READY;
    if (!source->ReadMsr(IA32_VMX_BASIC, &vmx_basic))
        return VMX_PERSISTENT_STATUS_NOT_READY;
    return BuildApprovedConfig(vmx_basic, source->GenerateRandom, out_config);
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeConfigBuildFromApprovedVmxBasic(
    uint64_t vmx_basic,
    VMX_PERSISTENT_RUNTIME_CONFIG* out_config)
{
    return BuildApprovedConfig(vmx_basic,
        KernelPreflightPlatform.GenerateRandom, out_config);
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeConfigBuild(
    VMX_PERSISTENT_RUNTIME_CONFIG* out_config)
{
    const VMX_PERSISTENT_RUNTIME_CONFIG_SOURCE source = {
        KernelPreflightPlatform.ReadCr4,
        KernelPreflightPlatform.CheckCpuidVmx,
        KernelPreflightPlatform.ReadMsr,
        KernelPreflightPlatform.GenerateRandom
    };
    return VmxPersistentRuntimeConfigBuildWithSource(&source, out_config);
}
