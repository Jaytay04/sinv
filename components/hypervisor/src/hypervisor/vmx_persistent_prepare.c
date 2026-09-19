#include "vmx_persistent_prepare.h"

typedef struct _VMX_PERSISTENT_PREPARE_CALL {
    const VMX_PERSISTENT_PREPARE_PLATFORM* Platform;
    VMX_PERSISTENT_CONTEXT* Context;
} VMX_PERSISTENT_PREPARE_CALL;

static int32_t InitializeOnCpu(void* operation_context,
                               const VMX_PROCESSOR_IDENTITY* target)
{
    VMX_PERSISTENT_PREPARE_CALL* call = operation_context;
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch = NULL;
    if (call == NULL || target == NULL ||
        !VmxProcessorIdentityEqual(target, &call->Context->Identity))
        return VMX_PERSISTENT_LAUNCH_INIT_INVALID;
    return (int32_t)VmxPersistentLaunchStorageInitialize(
        call->Context, call->Platform->Cookie, call->Platform->Dispatcher,
        call->Platform->DispatcherContext, &launch);
}

static int32_t DestroyOnCpu(void* operation_context,
                            const VMX_PROCESSOR_IDENTITY* target)
{
    VMX_PERSISTENT_PREPARE_CALL* call = operation_context;
    if (call == NULL || target == NULL ||
        !VmxProcessorIdentityEqual(target, &call->Context->Identity))
        return VMX_PERSISTENT_CONTEXT_INVALID;
    return (int32_t)VmxPersistentContextDestroy(
        call->Platform->Allocator, call->Context);
}

static int PlatformValid(const VMX_PERSISTENT_PREPARE_PLATFORM* platform)
{
    return platform != NULL && platform->Kernel != NULL &&
           platform->Kernel->CheckPassive != NULL &&
           platform->Kernel->Pin != NULL &&
           platform->Kernel->QueryCurrent != NULL &&
           platform->Kernel->Restore != NULL && platform->Allocator != NULL &&
           platform->Allocator->AllocatePool != NULL &&
           platform->Allocator->FreePool != NULL &&
           platform->Allocator->AllocateContiguous != NULL &&
           platform->Allocator->FreeContiguous != NULL &&
           platform->Allocator->GetPhysicalAddress != NULL &&
           platform->Cookie != 0 && platform->Dispatcher != NULL &&
           platform->DispatcherContext != NULL && platform->KernelResult != NULL;
}

static int PairValid(const VMX_PERSISTENT_CONTEXT* context,
                     const VMX_PERSISTENT_CPU_RECORD* record)
{
    return context != NULL && record != NULL &&
           record->Identity.Number < VMX_PROCESSOR_GROUP_MAX_NUMBER &&
           record->Identity.Reserved == 0 && context->Identity.Reserved == 0 &&
           VmxProcessorIdentityEqual(&context->Identity, &record->Identity);
}

static int KernelFailureRecorded(const VMX_PERSISTENT_KERNEL_RESULT* result)
{
    return result->FinalStatus != VMX_PERSISTENT_KERNEL_OK ||
           result->PrimaryStatus != VMX_PERSISTENT_KERNEL_OK ||
           result->RestoreStatus != VMX_PERSISTENT_KERNEL_OK ||
           result->OperationStatus != 0;
}

VMX_PERSISTENT_STATUS VmxPersistentPrepareContext(
    void* operation_context, VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record)
{
    const VMX_PERSISTENT_PREPARE_PLATFORM* platform = operation_context;
    VMX_PERSISTENT_PREPARE_CALL call;
    VMX_PERSISTENT_KERNEL_RESULT result = {0};
    if (!PlatformValid(platform) || !PairValid(context, record))
        return VMX_PERSISTENT_STATUS_INVALID;
    if (record->Active || record->CleanupRequired || record->OwnedMask != 0 ||
        !context->Published || context->OwnedResources == 0)
        return VMX_PERSISTENT_STATUS_INVARIANT;
    record->OwnedMask = VMX_PERSISTENT_OWNER_CONTEXT;
    call.Platform = platform; call.Context = context;
    if (VmxPersistentKernelExecutePinned(
            platform->Kernel, &record->Identity, &call, InitializeOnCpu,
            &result) != VMX_PERSISTENT_KERNEL_OK) {
        *platform->KernelResult = result;
        return VMX_PERSISTENT_STATUS_NOT_READY;
    }
    *platform->KernelResult = result;
    return VMX_PERSISTENT_STATUS_OK;
}

VMX_PERSISTENT_STATUS VmxPersistentDestroyPreparedContext(
    void* operation_context, VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record)
{
    const VMX_PERSISTENT_PREPARE_PLATFORM* platform = operation_context;
    VMX_PERSISTENT_PREPARE_CALL call;
    VMX_PERSISTENT_KERNEL_RESULT result = {0};
    VMX_PERSISTENT_KERNEL_RESULT start_failure;
    int preserve_start_failure;
    if (!PlatformValid(platform) || !PairValid(context, record))
        return VMX_PERSISTENT_STATUS_INVALID;
    if (record->Active || record->CleanupRequired ||
        (record->OwnedMask & ~VMX_PERSISTENT_OWNER_CONTEXT) != 0)
        return VMX_PERSISTENT_STATUS_INVARIANT;
    if (record->OwnedMask == 0)
        return context->OwnedResources == 0 && !context->Published
            ? VMX_PERSISTENT_STATUS_OK : VMX_PERSISTENT_STATUS_INVARIANT;
    start_failure = *platform->KernelResult;
    preserve_start_failure = KernelFailureRecorded(&start_failure);
    call.Platform = platform; call.Context = context;
    if (VmxPersistentKernelExecutePinned(
            platform->Kernel, &record->Identity, &call, DestroyOnCpu,
            &result) != VMX_PERSISTENT_KERNEL_OK) {
        *platform->KernelResult = preserve_start_failure
            ? start_failure : result;
        return VMX_PERSISTENT_STATUS_NOT_READY;
    }
    *platform->KernelResult = preserve_start_failure
        ? start_failure : result;
    if (context->OwnedResources != 0 || context->Published)
        return VMX_PERSISTENT_STATUS_INVARIANT;
    record->OwnedMask = 0;
    return VMX_PERSISTENT_STATUS_OK;
}
