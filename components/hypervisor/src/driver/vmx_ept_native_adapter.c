#include "vmx_ept_native_adapter.h"

extern uint32_t VmxEptInveptAsm(
    uint64_t type,
    const VMX_EPT_INVALIDATION_DESCRIPTOR* descriptor);

static int NativeSpanValid(const void* value, SIZE_T size)
{
    UINT_PTR first = (UINT_PTR)value;
    return value != NULL && size != 0 && first <= (UINT_PTR)-1 - size;
}

static int NativeRangesSeparate(
    const void* first,
    SIZE_T first_size,
    const void* second,
    SIZE_T second_size)
{
    UINT_PTR first_begin;
    UINT_PTR second_begin;
    UINT_PTR first_end;
    UINT_PTR second_end;

    if (!NativeSpanValid(first, first_size) ||
        !NativeSpanValid(second, second_size))
        return 0;
    first_begin = (UINT_PTR)first;
    second_begin = (UINT_PTR)second;
    first_end = first_begin + first_size;
    second_end = second_begin + second_size;
    return first_end <= second_begin || second_end <= first_begin;
}

static int NativeSnapshot(
    const VMX_EPT_NATIVE_CONTEXT* context,
    VMX_EPT_NATIVE_CONTEXT* snapshot)
{
    if (!NativeSpanValid(context, sizeof(*context)) ||
        !NativeSpanValid(snapshot, sizeof(*snapshot)) ||
        !NativeRangesSeparate(context, sizeof(*context),
            snapshot, sizeof(*snapshot)))
        return 0;
    RtlCopyMemory(snapshot, context, sizeof(*snapshot));
    return VmxEptNativeContextValidate(snapshot);
}

static int NativeCurrentProcessorMatches(
    const VMX_EPT_NATIVE_CONTEXT* snapshot)
{
    PROCESSOR_NUMBER number;
    ULONG system_index;

    if (KeGetCurrentIrql() != DISPATCH_LEVEL)
        return 0;
    system_index = KeGetCurrentProcessorNumberEx(&number);
    return number.Group == snapshot->Group &&
        number.Number == snapshot->Number &&
        system_index == snapshot->SystemIndex;
}

static uint32_t NativeGetCurrentIrql(void* opaque)
{
    UNREFERENCED_PARAMETER(opaque);
    return (uint32_t)KeGetCurrentIrql();
}

static int32_t NativeIsVmxRootOwned(void* opaque)
{
    VMX_EPT_NATIVE_CONTEXT snapshot;
    if (!NativeSnapshot((const VMX_EPT_NATIVE_CONTEXT*)opaque, &snapshot) ||
        !NativeCurrentProcessorMatches(&snapshot))
        return -1;
    return snapshot.RootOwned == 1 ? 1 : 0;
}

static uint32_t NativeExecute(
    void* opaque,
    uint32_t type,
    const VMX_EPT_INVALIDATION_DESCRIPTOR* descriptor)
{
    VMX_EPT_NATIVE_CONTEXT snapshot;

    if ((type != VMX_EPT_INVALIDATION_SINGLE_CONTEXT &&
         type != VMX_EPT_INVALIDATION_ALL_CONTEXTS) ||
        !NativeSpanValid(descriptor, sizeof(*descriptor)) ||
        !NativeSnapshot((const VMX_EPT_NATIVE_CONTEXT*)opaque, &snapshot) ||
        !NativeRangesSeparate(opaque, sizeof(snapshot),
            descriptor, sizeof(*descriptor)) ||
        !NativeCurrentProcessorMatches(&snapshot) ||
        snapshot.RootOwned != 1)
        return VMX_EPT_INSTRUCTION_NOT_ATTEMPTED;
    return VmxEptInveptAsm((uint64_t)type, descriptor);
}

VMX_EPT_NATIVE_STATUS VmxEptNativePlatformInitialize(
    VMX_EPT_NATIVE_CONTEXT* context,
    uint16_t group,
    uint8_t number,
    uint32_t system_index,
    VMX_EPT_EXECUTION_PLATFORM* platform)
{
    VMX_EPT_NATIVE_CONTEXT zero_context = { 0 };
    VMX_EPT_NATIVE_CONTEXT staged_context = { 0 };
    VMX_EPT_EXECUTION_PLATFORM staged_platform = { 0 };

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_NATIVE_WRONG_IRQL;
    if (!NativeSpanValid(context, sizeof(*context)) ||
        !NativeSpanValid(platform, sizeof(*platform)) ||
        !NativeRangesSeparate(context, sizeof(*context),
            platform, sizeof(*platform)))
        return VMX_EPT_NATIVE_INVALID;
    if (RtlCompareMemory(context, &zero_context, sizeof(*context)) !=
        sizeof(*context))
        return VMX_EPT_NATIVE_STATE_MISMATCH;

    staged_context.Magic = VMX_EPT_NATIVE_CONTEXT_MAGIC;
    staged_context.Version = VMX_EPT_NATIVE_CONTEXT_VERSION;
    staged_context.Group = group;
    staged_context.Number = number;
    staged_context.SystemIndex = system_index;
    if (!VmxEptNativeContextValidate(&staged_context))
        return VMX_EPT_NATIVE_INVALID;

    staged_context.Magic = 0;
    staged_platform.Context = context;
    staged_platform.GetCurrentIrql = NativeGetCurrentIrql;
    staged_platform.IsVmxRootOwned = NativeIsVmxRootOwned;
    staged_platform.Execute = NativeExecute;

    RtlCopyMemory(context, &staged_context, sizeof(*context));
    *platform = staged_platform;
    KeMemoryBarrier();
    (void)InterlockedExchange(
        (volatile LONG*)&context->Magic,
        (LONG)VMX_EPT_NATIVE_CONTEXT_MAGIC);
    return VMX_EPT_NATIVE_OK;
}

VMX_EPT_NATIVE_STATUS VmxEptNativePlatformValidate(
    const VMX_EPT_NATIVE_CONTEXT* context,
    const VMX_PROCESSOR_IDENTITY* identity,
    const VMX_EPT_EXECUTION_PLATFORM* platform)
{
    VMX_EPT_NATIVE_CONTEXT context_snapshot;
    VMX_PROCESSOR_IDENTITY identity_snapshot;
    VMX_EPT_EXECUTION_PLATFORM platform_snapshot;

    if (KeGetCurrentIrql() != DISPATCH_LEVEL)
        return VMX_EPT_NATIVE_WRONG_IRQL;
    if (!NativeSpanValid(context, sizeof(*context)) ||
        !NativeSpanValid(identity, sizeof(*identity)) ||
        !NativeSpanValid(platform, sizeof(*platform)) ||
        !NativeRangesSeparate(context, sizeof(*context),
            identity, sizeof(*identity)) ||
        !NativeRangesSeparate(context, sizeof(*context),
            platform, sizeof(*platform)) ||
        !NativeRangesSeparate(identity, sizeof(*identity),
            platform, sizeof(*platform)))
        return VMX_EPT_NATIVE_INVALID;

    RtlCopyMemory(&context_snapshot, context, sizeof(context_snapshot));
    RtlCopyMemory(&identity_snapshot, identity, sizeof(identity_snapshot));
    RtlCopyMemory(&platform_snapshot, platform, sizeof(platform_snapshot));
    if (!VmxEptNativeContextValidate(&context_snapshot) ||
        identity_snapshot.Reserved != 0 ||
        identity_snapshot.Group != context_snapshot.Group ||
        identity_snapshot.Number != context_snapshot.Number ||
        identity_snapshot.SystemIndex != context_snapshot.SystemIndex ||
        platform_snapshot.Context != context ||
        platform_snapshot.GetCurrentIrql != NativeGetCurrentIrql ||
        platform_snapshot.IsVmxRootOwned != NativeIsVmxRootOwned ||
        platform_snapshot.Execute != NativeExecute ||
        platform_snapshot.Reserved != 0)
        return VMX_EPT_NATIVE_STATE_MISMATCH;
    return NativeCurrentProcessorMatches(&context_snapshot) ?
        VMX_EPT_NATIVE_OK : VMX_EPT_NATIVE_WRONG_PROCESSOR;
}

VMX_EPT_NATIVE_STATUS VmxEptNativeRootTransition(
    VMX_EPT_NATIVE_CONTEXT* context,
    uint32_t expected_owned,
    uint32_t desired_owned)
{
    VMX_EPT_NATIVE_CONTEXT snapshot;
    LONG previous;

    if (expected_owned > 1 || desired_owned > 1 ||
        expected_owned == desired_owned)
        return VMX_EPT_NATIVE_INVALID;
    if (KeGetCurrentIrql() != DISPATCH_LEVEL)
        return VMX_EPT_NATIVE_WRONG_IRQL;
    if (!NativeSnapshot(context, &snapshot))
        return VMX_EPT_NATIVE_INVALID;
    if (!NativeCurrentProcessorMatches(&snapshot))
        return VMX_EPT_NATIVE_WRONG_PROCESSOR;

    previous = InterlockedCompareExchange(
        (volatile LONG*)&context->RootOwned,
        (LONG)desired_owned,
        (LONG)expected_owned);
    return previous == (LONG)expected_owned ?
        VMX_EPT_NATIVE_OK : VMX_EPT_NATIVE_STATE_MISMATCH;
}
