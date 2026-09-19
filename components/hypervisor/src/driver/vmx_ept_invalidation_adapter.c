#include "vmx_ept_invalidation_adapter.h"

#include <stdint.h>

static int Span(const void* object, SIZE_T size,
    UINT_PTR* begin, UINT_PTR* end)
{
    UINT_PTR first = (UINT_PTR)object;
    if (object == NULL || size == 0 || first > (UINT_PTR)-1 - size)
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

static int ContextSeparate(
    const VMX_EPT_SESSION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    const VMX_EPT_NATIVE_CONTEXT* context,
    const VMX_EPT_EXECUTION_PLATFORM* platform,
    const VMX_EPT_EXECUTION_REPORT* report)
{
    const VMX_EPT_STORAGE_OWNER* storage;
    SIZE_T page_bytes;

    if (owner == NULL || identity == NULL || context == NULL ||
        platform == NULL || report == NULL ||
        VmxEptSessionOwnerValidate(owner) != VMX_EPT_SESSION_OK ||
        owner->GraphBytes > SIZE_MAX)
        return 0;
    storage = owner->Storage;
    if (storage->PageCount > SIZE_MAX / sizeof(*storage->PagesByOrdinal))
        return 0;
    page_bytes = (SIZE_T)storage->PageCount *
        sizeof(*storage->PagesByOrdinal);
    return Separate(owner, sizeof(*owner), context, sizeof(*context)) &&
        Separate(owner->Graph, (SIZE_T)owner->GraphBytes,
            context, sizeof(*context)) &&
        Separate(storage->Tables, (SIZE_T)storage->TableBytes,
            context, sizeof(*context)) &&
        Separate(storage->PagesByOrdinal, page_bytes,
            context, sizeof(*context)) &&
        Separate(storage->PhysicalIndex,
            (SIZE_T)storage->PageCount * sizeof(*storage->PhysicalIndex),
            context, sizeof(*context)) &&
        Separate(identity, sizeof(*identity), context, sizeof(*context)) &&
        Separate(context, sizeof(*context), platform, sizeof(*platform)) &&
        Separate(context, sizeof(*context), report, sizeof(*report));
}

static VMX_EPT_KERNEL_INVALIDATION_STATUS MapOperation(
    VMX_EPT_SESSION_OPERATION_STATUS status)
{
    switch (status) {
    case VMX_EPT_SESSION_OPERATION_OK:
        return VMX_EPT_KERNEL_INVALIDATION_OK;
    case VMX_EPT_SESSION_OPERATION_IDENTITY_MISMATCH:
        return VMX_EPT_KERNEL_INVALIDATION_IDENTITY_MISMATCH;
    case VMX_EPT_SESSION_OPERATION_UNSUPPORTED:
        return VMX_EPT_KERNEL_INVALIDATION_UNSUPPORTED;
    case VMX_EPT_SESSION_OPERATION_NOT_READY:
        return VMX_EPT_KERNEL_INVALIDATION_NOT_READY;
    case VMX_EPT_SESSION_OPERATION_BUSY:
        return VMX_EPT_KERNEL_INVALIDATION_BUSY;
    case VMX_EPT_SESSION_OPERATION_DIRTY:
        return VMX_EPT_KERNEL_INVALIDATION_DIRTY;
    case VMX_EPT_SESSION_OPERATION_INSTRUCTION_FAILED:
        return VMX_EPT_KERNEL_INVALIDATION_INSTRUCTION_FAILED;
    case VMX_EPT_SESSION_OPERATION_OVERFLOW:
        return VMX_EPT_KERNEL_INVALIDATION_OVERFLOW;
    default:
        return VMX_EPT_KERNEL_INVALIDATION_INVALID;
    }
}

static VMX_EPT_KERNEL_INVALIDATION_STATUS MapNative(
    VMX_EPT_NATIVE_STATUS status)
{
    switch (status) {
    case VMX_EPT_NATIVE_OK:
        return VMX_EPT_KERNEL_INVALIDATION_OK;
    case VMX_EPT_NATIVE_WRONG_IRQL:
        return VMX_EPT_KERNEL_INVALIDATION_WRONG_IRQL;
    case VMX_EPT_NATIVE_WRONG_PROCESSOR:
        return VMX_EPT_KERNEL_INVALIDATION_WRONG_PROCESSOR;
    case VMX_EPT_NATIVE_STATE_MISMATCH:
        return VMX_EPT_KERNEL_INVALIDATION_PLATFORM_INVALID;
    default:
        return VMX_EPT_KERNEL_INVALIDATION_INVALID;
    }
}

static VMX_EPT_KERNEL_INVALIDATION_STATUS MapCoordinator(
    VMX_EPT_INVALIDATION_COORDINATOR_STATUS status)
{
    switch (status) {
    case VMX_EPT_INVALIDATION_COORDINATOR_OK:
        return VMX_EPT_KERNEL_INVALIDATION_OK;
    case VMX_EPT_INVALIDATION_COORDINATOR_IDENTITY_MISMATCH:
        return VMX_EPT_KERNEL_INVALIDATION_IDENTITY_MISMATCH;
    case VMX_EPT_INVALIDATION_COORDINATOR_UNSUPPORTED:
        return VMX_EPT_KERNEL_INVALIDATION_UNSUPPORTED;
    case VMX_EPT_INVALIDATION_COORDINATOR_NOT_READY:
        return VMX_EPT_KERNEL_INVALIDATION_NOT_READY;
    case VMX_EPT_INVALIDATION_COORDINATOR_BUSY:
        return VMX_EPT_KERNEL_INVALIDATION_BUSY;
    case VMX_EPT_INVALIDATION_COORDINATOR_DIRTY:
        return VMX_EPT_KERNEL_INVALIDATION_DIRTY;
    case VMX_EPT_INVALIDATION_COORDINATOR_INSTRUCTION_FAILED:
        return VMX_EPT_KERNEL_INVALIDATION_INSTRUCTION_FAILED;
    case VMX_EPT_INVALIDATION_COORDINATOR_OVERFLOW:
        return VMX_EPT_KERNEL_INVALIDATION_OVERFLOW;
    case VMX_EPT_INVALIDATION_COORDINATOR_WRONG_IRQL:
        return VMX_EPT_KERNEL_INVALIDATION_WRONG_IRQL;
    case VMX_EPT_INVALIDATION_COORDINATOR_NOT_VMX_ROOT:
        return VMX_EPT_KERNEL_INVALIDATION_NOT_VMX_ROOT;
    case VMX_EPT_INVALIDATION_COORDINATOR_PLATFORM_INVALID:
        return VMX_EPT_KERNEL_INVALIDATION_PLATFORM_INVALID;
    default:
        return VMX_EPT_KERNEL_INVALIDATION_INVALID;
    }
}

VMX_EPT_KERNEL_INVALIDATION_STATUS VmxEptKernelCoordinateInvalidation(
    VMX_EPT_SESSION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    const VMX_EPT_NATIVE_CONTEXT* context,
    const VMX_EPT_EXECUTION_PLATFORM* platform,
    VMX_EPT_EXECUTION_REPORT* report)
{
    VMX_EPT_SESSION_OPERATION_STATUS operation_status;
    VMX_EPT_NATIVE_STATUS native_status;

    if (KeGetCurrentIrql() != DISPATCH_LEVEL)
        return VMX_EPT_KERNEL_INVALIDATION_WRONG_IRQL;
    if (!ContextSeparate(owner, identity, context, platform, report))
        return VMX_EPT_KERNEL_INVALIDATION_INVALID;
    operation_status = VmxEptSessionValidateInvalidationExecution(
        owner, identity, platform, report);
    if (operation_status != VMX_EPT_SESSION_OPERATION_OK)
        return MapOperation(operation_status);
    native_status = VmxEptNativePlatformValidate(
        context, identity, platform);
    if (native_status != VMX_EPT_NATIVE_OK)
        return MapNative(native_status);
    return MapCoordinator(VmxEptCoordinateInvalidation(
        owner, identity, platform, report));
}
