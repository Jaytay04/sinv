#include "vmx_ept_installation_adapter.h"

#include <intrin.h>

static int Span(const void* value, SIZE_T size, UINT_PTR* begin, UINT_PTR* end)
{
    UINT_PTR first = (UINT_PTR)value;
    if (value == NULL || size == 0 || first > (UINT_PTR)-1 - size)
        return 0;
    *begin = first;
    *end = first + size;
    return 1;
}

static int Separate(const void* first, SIZE_T first_size,
    const void* second, SIZE_T second_size)
{
    UINT_PTR first_begin, first_end, second_begin, second_end;
    return Span(first, first_size, &first_begin, &first_end) &&
        Span(second, second_size, &second_begin, &second_end) &&
        (first_end <= second_begin || second_end <= first_begin);
}

static int IdentityMatches(const VMX_EPT_INSTALL_NATIVE_CONTEXT* context,
    const VMX_PROCESSOR_IDENTITY* identity)
{
    return identity->Reserved == 0 && identity->Group == context->Group &&
        identity->Number == context->Number &&
        identity->SystemIndex == context->SystemIndex;
}

static int Snapshot(const VMX_EPT_INSTALL_NATIVE_CONTEXT* context,
    VMX_EPT_INSTALL_NATIVE_CONTEXT* snapshot)
{
    if (!Separate(context, sizeof(*context), snapshot, sizeof(*snapshot)))
        return 0;
    RtlCopyMemory(snapshot, context, sizeof(*snapshot));
    return snapshot->Magic == VMX_EPT_INSTALL_NATIVE_MAGIC &&
        snapshot->Version == VMX_EPT_INSTALL_NATIVE_VERSION &&
        snapshot->Group < VMX_EPT_NATIVE_MAX_GROUPS &&
        snapshot->Number < VMX_EPT_NATIVE_MAX_PROCESSORS_PER_GROUP &&
        snapshot->Reserved8 == 0 &&
        snapshot->SystemIndex < VMX_EPT_NATIVE_MAX_SYSTEM_PROCESSORS &&
        snapshot->CurrentVmcsPhysicalAddress != 0 &&
        (snapshot->CurrentVmcsPhysicalAddress & VMX_EPT_INSTALL_PAGE_MASK) == 0 &&
        snapshot->NativeContext != NULL && snapshot->NativePlatform != NULL &&
        snapshot->Reserved[0] == 0 && snapshot->Reserved[1] == 0;
}

static VMX_EPT_INSTALL_NATIVE_STATUS Current(
    const VMX_EPT_INSTALL_NATIVE_CONTEXT* context)
{
    VMX_EPT_INSTALL_NATIVE_CONTEXT snapshot;
    VMX_PROCESSOR_IDENTITY identity = { 0 };
    uint64_t current_vmcs;
    VMX_EPT_NATIVE_STATUS native_status;
    if (KeGetCurrentIrql() != DISPATCH_LEVEL)
        return VMX_EPT_INSTALL_NATIVE_WRONG_IRQL;
    if (!Snapshot(context, &snapshot))
        return VMX_EPT_INSTALL_NATIVE_INVALID;
    identity.Group = snapshot.Group;
    identity.Number = snapshot.Number;
    identity.SystemIndex = snapshot.SystemIndex;
    native_status = VmxEptNativePlatformValidate(
        snapshot.NativeContext, &identity, snapshot.NativePlatform);
    if (native_status == VMX_EPT_NATIVE_WRONG_PROCESSOR)
        return VMX_EPT_INSTALL_NATIVE_WRONG_PROCESSOR;
    if (native_status != VMX_EPT_NATIVE_OK)
        return VMX_EPT_INSTALL_NATIVE_STATE_MISMATCH;
    __vmx_vmptrst(&current_vmcs);
    return current_vmcs == snapshot.CurrentVmcsPhysicalAddress ?
        VMX_EPT_INSTALL_NATIVE_OK : VMX_EPT_INSTALL_NATIVE_NO_CURRENT_VMCS;
}

static int MapInstructionOutcome(unsigned char result)
{
    if (result == 0) return VMX_VMWRITE_SUCCESS;
    if (result == 1) return VMX_VMWRITE_FAIL_VALID;
    return VMX_VMWRITE_FAIL_INVALID;
}

static uint32_t NativeSupportsActivation(void* opaque)
{
    uint64_t vmx_basic;
    uint64_t primary;
    uint64_t secondary;
    uint32_t primary_allowed;
    uint32_t secondary_allowed;

    if (Current((const VMX_EPT_INSTALL_NATIVE_CONTEXT*)opaque) !=
            VMX_EPT_INSTALL_NATIVE_OK)
        return 2;
    vmx_basic = __readmsr(IA32_VMX_BASIC);
    primary = __readmsr((uint32_t)VmxSelectTrueControl(
        vmx_basic, IA32_VMX_TRUE_PROCBASED_CTLS,
        IA32_VMX_PROCBASED_CTLS));
    secondary = __readmsr(IA32_VMX_PROCBASED_CTLS2);
    primary_allowed = (uint32_t)(primary >> 32);
    secondary_allowed = (uint32_t)(secondary >> 32);
    return (primary_allowed & CPU_BASED_ACTIVATE_SECONDARY_CTLS) != 0 &&
        (secondary_allowed & SECONDARY_ENABLE_EPT) != 0 ? 1u : 0u;
}

static int NativeRead(void* opaque, size_t field, uint64_t* output)
{
    size_t value;
    unsigned char result;
    if ((field != VMCS_CTRL_EPTP && field != VMCS_CTRL_CPU_BASED &&
            field != VMCS_CTRL_SECONDARY) ||
        !Separate(opaque, sizeof(VMX_EPT_INSTALL_NATIVE_CONTEXT),
            output, sizeof(*output)) ||
        Current((const VMX_EPT_INSTALL_NATIVE_CONTEXT*)opaque) !=
            VMX_EPT_INSTALL_NATIVE_OK)
        return VMX_VMWRITE_NOT_ATTEMPTED;
    result = __vmx_vmread(field, &value);
    if (result != 0)
        return MapInstructionOutcome(result);
    *output = (uint64_t)value;
    return VMX_VMWRITE_SUCCESS;
}

static int NativeWrite(void* opaque, size_t field, size_t value)
{
    unsigned char result;
    if (((field == VMCS_CTRL_EPTP && value != 0) ||
            (field == VMCS_CTRL_SECONDARY &&
                (value & SECONDARY_ENABLE_EPT) != 0) ||
            (field == VMCS_CTRL_CPU_BASED &&
                (value & CPU_BASED_ACTIVATE_SECONDARY_CTLS) != 0)) == 0 ||
        Current((const VMX_EPT_INSTALL_NATIVE_CONTEXT*)opaque) !=
            VMX_EPT_INSTALL_NATIVE_OK)
        return VMX_VMWRITE_NOT_ATTEMPTED;
    result = __vmx_vmwrite(field, value);
    return MapInstructionOutcome(result);
}

static uint32_t NativeReadError(void* opaque, uint64_t* error)
{
    size_t value;
    if (!Separate(opaque, sizeof(VMX_EPT_INSTALL_NATIVE_CONTEXT),
            error, sizeof(*error)) ||
        Current((const VMX_EPT_INSTALL_NATIVE_CONTEXT*)opaque) !=
            VMX_EPT_INSTALL_NATIVE_OK ||
        __vmx_vmread(VMCS_VM_INSTRUCTION_ERROR, &value) != 0)
        return 0;
    *error = (uint64_t)value;
    return 1;
}

VMX_EPT_INSTALL_NATIVE_STATUS VmxEptInstallationNativeInitialize(
    const VMX_EPT_NATIVE_CONTEXT* native_context,
    const VMX_EPT_EXECUTION_PLATFORM* native_platform,
    const VMX_PROCESSOR_IDENTITY* identity,
    uint64_t current_vmcs_physical_address,
    VMX_EPT_INSTALL_NATIVE_CONTEXT* context,
    VMX_EPT_INSTALLATION_PLATFORM* platform)
{
    VMX_EPT_INSTALL_NATIVE_CONTEXT zero = { 0 }, staged = { 0 };
    VMX_EPT_INSTALLATION_PLATFORM zero_platform = { 0 };
    VMX_EPT_INSTALLATION_PLATFORM staged_platform = { 0 };
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_INSTALL_NATIVE_WRONG_IRQL;
    if (!Separate(native_context, sizeof(*native_context), native_platform,
            sizeof(*native_platform)) ||
        !Separate(native_context, sizeof(*native_context), identity,
            sizeof(*identity)) ||
        !Separate(native_context, sizeof(*native_context), context,
            sizeof(*context)) ||
        !Separate(native_context, sizeof(*native_context), platform,
            sizeof(*platform)) ||
        !Separate(native_platform, sizeof(*native_platform), identity,
            sizeof(*identity)) ||
        !Separate(native_platform, sizeof(*native_platform), context,
            sizeof(*context)) ||
        !Separate(native_platform, sizeof(*native_platform), platform,
            sizeof(*platform)) ||
        !Separate(identity, sizeof(*identity), context, sizeof(*context)) ||
        !Separate(identity, sizeof(*identity), platform, sizeof(*platform)) ||
        !Separate(context, sizeof(*context), platform, sizeof(*platform)) ||
        RtlCompareMemory(context, &zero, sizeof(*context)) != sizeof(*context) ||
        RtlCompareMemory(platform, &zero_platform, sizeof(*platform)) !=
            sizeof(*platform) ||
        !VmxEptNativeContextValidate(native_context) ||
        current_vmcs_physical_address == 0 ||
        (current_vmcs_physical_address & VMX_EPT_INSTALL_PAGE_MASK) != 0 ||
        identity->Reserved != 0 || identity->Group != native_context->Group ||
        identity->Number != native_context->Number ||
        identity->SystemIndex != native_context->SystemIndex)
        return VMX_EPT_INSTALL_NATIVE_INVALID;
    staged.Version = VMX_EPT_INSTALL_NATIVE_VERSION;
    staged.Group = identity->Group; staged.Number = identity->Number;
    staged.SystemIndex = identity->SystemIndex;
    staged.CurrentVmcsPhysicalAddress = current_vmcs_physical_address;
    staged.NativeContext = native_context; staged.NativePlatform = native_platform;
    staged_platform.Context = context;
    staged_platform.ContextBytes = sizeof(*context);
    staged_platform.Write = NativeWrite;
    staged_platform.Read = NativeRead;
    staged_platform.SupportsActivation = NativeSupportsActivation;
    staged_platform.ReadInstructionError = NativeReadError;
    staged_platform.CurrentVmcsValid = 1;
    RtlCopyMemory(context, &staged, sizeof(staged));
    *platform = staged_platform;
    KeMemoryBarrier();
    (void)InterlockedExchange((volatile LONG*)&context->Magic,
        (LONG)VMX_EPT_INSTALL_NATIVE_MAGIC);
    return VMX_EPT_INSTALL_NATIVE_OK;
}

VMX_EPT_INSTALL_NATIVE_STATUS VmxEptInstallationNativeValidate(
    const VMX_EPT_INSTALL_NATIVE_CONTEXT* context,
    const VMX_PROCESSOR_IDENTITY* identity,
    const VMX_EPT_INSTALLATION_PLATFORM* platform)
{
    VMX_EPT_INSTALL_NATIVE_CONTEXT snapshot;
    VMX_EPT_INSTALLATION_PLATFORM platform_snapshot;
    VMX_EPT_INSTALL_NATIVE_STATUS status;
    if (!Separate(context, sizeof(*context), identity, sizeof(*identity)) ||
        !Separate(context, sizeof(*context), platform, sizeof(*platform)) ||
        !Separate(identity, sizeof(*identity), platform, sizeof(*platform)) ||
        !Snapshot(context, &snapshot))
        return VMX_EPT_INSTALL_NATIVE_INVALID;
    RtlCopyMemory(&platform_snapshot, platform, sizeof(platform_snapshot));
    if (!IdentityMatches(&snapshot, identity) ||
        platform_snapshot.Context != context ||
        platform_snapshot.ContextBytes != sizeof(*context) ||
        platform_snapshot.Write != NativeWrite ||
        platform_snapshot.Read != NativeRead ||
        platform_snapshot.SupportsActivation != NativeSupportsActivation ||
        platform_snapshot.ReadInstructionError != NativeReadError ||
        platform_snapshot.CurrentVmcsValid != 1 ||
        platform_snapshot.Reserved32 != 0 || platform_snapshot.Reserved != 0)
        return VMX_EPT_INSTALL_NATIVE_STATE_MISMATCH;
    status = Current(context);
    return status;
}

static int OwnerSeparate(const VMX_EPT_SESSION_OWNER* owner,
    const void* object, SIZE_T size)
{
    const VMX_EPT_STORAGE_OWNER* storage;
    SIZE_T page_bytes;
    if (owner == NULL || object == NULL ||
        VmxEptSessionOwnerValidate(owner) != VMX_EPT_SESSION_OK ||
        owner->GraphBytes > SIZE_MAX)
        return 0;
    storage = owner->Storage;
    if (storage->PageCount > SIZE_MAX / sizeof(*storage->PagesByOrdinal))
        return 0;
    page_bytes = (SIZE_T)storage->PageCount * sizeof(*storage->PagesByOrdinal);
    return Separate(owner, sizeof(*owner), object, size) &&
        Separate(owner->Graph, (SIZE_T)owner->GraphBytes, object, size) &&
        Separate(storage->Tables, (SIZE_T)storage->TableBytes, object, size) &&
        Separate(storage->PagesByOrdinal, page_bytes, object, size) &&
        Separate(storage->PhysicalIndex,
            (SIZE_T)storage->PageCount * sizeof(*storage->PhysicalIndex), object, size);
}

static VMX_EPT_KERNEL_INSTALLATION_STATUS MapNative(
    VMX_EPT_INSTALL_NATIVE_STATUS status)
{
    if (status == VMX_EPT_INSTALL_NATIVE_OK) return VMX_EPT_KERNEL_INSTALLATION_OK;
    if (status == VMX_EPT_INSTALL_NATIVE_WRONG_IRQL) return VMX_EPT_KERNEL_INSTALLATION_WRONG_IRQL;
    if (status == VMX_EPT_INSTALL_NATIVE_WRONG_PROCESSOR) return VMX_EPT_KERNEL_INSTALLATION_WRONG_PROCESSOR;
    if (status == VMX_EPT_INSTALL_NATIVE_NO_CURRENT_VMCS) return VMX_EPT_KERNEL_INSTALLATION_NO_CURRENT_VMCS;
    if (status == VMX_EPT_INSTALL_NATIVE_STATE_MISMATCH) return VMX_EPT_KERNEL_INSTALLATION_PLATFORM_INVALID;
    return VMX_EPT_KERNEL_INSTALLATION_INVALID;
}

static VMX_EPT_KERNEL_INSTALLATION_STATUS MapCoordinator(
    VMX_EPT_INSTALLATION_STATUS status)
{
    switch (status) {
    case VMX_EPT_INSTALLATION_OK: return VMX_EPT_KERNEL_INSTALLATION_OK;
    case VMX_EPT_INSTALLATION_IDENTITY_MISMATCH: return VMX_EPT_KERNEL_INSTALLATION_IDENTITY_MISMATCH;
    case VMX_EPT_INSTALLATION_UNSUPPORTED: return VMX_EPT_KERNEL_INSTALLATION_UNSUPPORTED;
    case VMX_EPT_INSTALLATION_NOT_READY: return VMX_EPT_KERNEL_INSTALLATION_NOT_READY;
    case VMX_EPT_INSTALLATION_BUSY: return VMX_EPT_KERNEL_INSTALLATION_BUSY;
    case VMX_EPT_INSTALLATION_DIRTY: return VMX_EPT_KERNEL_INSTALLATION_DIRTY;
    case VMX_EPT_INSTALLATION_INSTRUCTION_FAILED: return VMX_EPT_KERNEL_INSTALLATION_INSTRUCTION_FAILED;
    case VMX_EPT_INSTALLATION_OVERFLOW: return VMX_EPT_KERNEL_INSTALLATION_OVERFLOW;
    case VMX_EPT_INSTALLATION_PLATFORM_INVALID: return VMX_EPT_KERNEL_INSTALLATION_PLATFORM_INVALID;
    default: return VMX_EPT_KERNEL_INSTALLATION_INVALID;
    }
}

VMX_EPT_KERNEL_INSTALLATION_STATUS VmxEptKernelCoordinateInstallation(
    VMX_EPT_SESSION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    const VMX_EPT_INSTALL_NATIVE_CONTEXT* context,
    const VMX_EPT_INSTALLATION_PLATFORM* platform,
    VMX_EPT_INSTALLATION_REPORT* report)
{
    VMX_EPT_INSTALL_NATIVE_CONTEXT snapshot;
    VMX_EPT_INSTALL_NATIVE_STATUS native_status;
    if (KeGetCurrentIrql() != DISPATCH_LEVEL)
        return VMX_EPT_KERNEL_INSTALLATION_WRONG_IRQL;
    if (!Snapshot(context, &snapshot) || !OwnerSeparate(owner, context, sizeof(*context)) ||
        !OwnerSeparate(owner, snapshot.NativeContext, sizeof(*snapshot.NativeContext)) ||
        !OwnerSeparate(owner, snapshot.NativePlatform, sizeof(*snapshot.NativePlatform)) ||
        !OwnerSeparate(owner, platform, sizeof(*platform)) ||
        !OwnerSeparate(owner, report, sizeof(*report)) ||
        !Separate(context, sizeof(*context), snapshot.NativeContext,
            sizeof(*snapshot.NativeContext)) ||
        !Separate(context, sizeof(*context), snapshot.NativePlatform,
            sizeof(*snapshot.NativePlatform)) ||
        !Separate(context, sizeof(*context), report, sizeof(*report)) ||
        !Separate(snapshot.NativeContext, sizeof(*snapshot.NativeContext),
            platform, sizeof(*platform)) ||
        !Separate(snapshot.NativeContext, sizeof(*snapshot.NativeContext),
            report, sizeof(*report)) ||
        !Separate(snapshot.NativePlatform, sizeof(*snapshot.NativePlatform),
            platform, sizeof(*platform)) ||
        !Separate(snapshot.NativePlatform, sizeof(*snapshot.NativePlatform),
            report, sizeof(*report)))
        return VMX_EPT_KERNEL_INSTALLATION_INVALID;
    native_status = VmxEptInstallationNativeValidate(context, identity, platform);
    if (native_status != VMX_EPT_INSTALL_NATIVE_OK)
        return MapNative(native_status);
    return MapCoordinator(VmxEptCoordinateInstallation(
        owner, identity, platform, report));
}
