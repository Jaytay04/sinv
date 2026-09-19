#include "vmx_persistent_kernel.h"
static bool TopologyCallbacksValid(const VMX_PERSISTENT_KERNEL_PLATFORM* platform)
{
    return platform != NULL && platform->QueryActiveCount != NULL &&
           platform->MapSystemIndex != NULL && platform->Allocate != NULL &&
           platform->Free != NULL;
}
static bool AffinityCallbacksValid(const VMX_PERSISTENT_KERNEL_PLATFORM* platform)
{
    return platform != NULL && platform->CheckPassive != NULL &&
           platform->Pin != NULL && platform->QueryCurrent != NULL &&
           platform->Restore != NULL;
}
VMX_PERSISTENT_KERNEL_STATUS VmxPersistentKernelTopologyProvider(
    const VMX_PERSISTENT_KERNEL_PLATFORM* platform,
    VMX_PROCESSOR_TOPOLOGY_PROVIDER* out_provider)
{
    VMX_PROCESSOR_TOPOLOGY_PROVIDER provider;
    if (!TopologyCallbacksValid(platform) || out_provider == NULL) {
        return VMX_PERSISTENT_KERNEL_INVALID;
    }
    provider.Context = platform->Context;
    provider.QueryActiveCount = platform->QueryActiveCount;
    provider.MapIdentity = platform->MapSystemIndex;
    provider.Allocate = platform->Allocate;
    provider.Free = platform->Free;
    *out_provider = provider;
    return VMX_PERSISTENT_KERNEL_OK;
}
static bool AffinityArgumentsValid(
    const VMX_PERSISTENT_KERNEL_PLATFORM* platform,
    const VMX_PROCESSOR_IDENTITY* target,
    VmxPersistentKernelOperationFn operation,
    const VMX_PERSISTENT_KERNEL_RESULT* out_result)
{
    return AffinityCallbacksValid(platform) && target != NULL &&
           operation != NULL && out_result != NULL &&
           target->Number < VMX_PROCESSOR_GROUP_MAX_NUMBER &&
           target->Reserved == 0;
}
VMX_PERSISTENT_KERNEL_STATUS VmxPersistentKernelExecutePinned(
    const VMX_PERSISTENT_KERNEL_PLATFORM* platform,
    const VMX_PROCESSOR_IDENTITY* target,
    void* operation_context,
    VmxPersistentKernelOperationFn operation,
    VMX_PERSISTENT_KERNEL_RESULT* out_result)
{
    VMX_PERSISTENT_KERNEL_RESULT result = {0};
    VMX_PERSISTENT_AFFINITY_TOKEN token = {0};
    VMX_PROCESSOR_IDENTITY expected, current = {0};
    VMX_PERSISTENT_KERNEL_STATUS status;
    if (!AffinityArgumentsValid(platform, target, operation, out_result)) {
        return VMX_PERSISTENT_KERNEL_INVALID;
    }
    expected = *target;
    if (!platform->CheckPassive(platform->Context)) {
        result.FinalStatus = result.PrimaryStatus =
            VMX_PERSISTENT_KERNEL_NOT_PASSIVE;
        *out_result = result;
        return result.FinalStatus;
    }
    status = platform->Pin(platform->Context, &expected, &token);
    if (status != VMX_PERSISTENT_KERNEL_OK) {
        result.PrimaryStatus = VMX_PERSISTENT_KERNEL_PIN_FAILED;
        if (!token.Pinned) {
            result.FinalStatus = result.PrimaryStatus;
            *out_result = result;
            return result.FinalStatus;
        }
        goto restore_affinity;
    }
    result.PinSucceeded = true;
    if (!token.Pinned) {
        result.PrimaryStatus = VMX_PERSISTENT_KERNEL_INVARIANT;
    } else {
        status = platform->QueryCurrent(platform->Context, &current);
        if (status != VMX_PERSISTENT_KERNEL_OK) {
            result.PrimaryStatus = VMX_PERSISTENT_KERNEL_VERIFY_FAILED;
        } else if (!VmxProcessorIdentityEqual(&current, &expected)) {
            result.PrimaryStatus = VMX_PERSISTENT_KERNEL_IDENTITY_MISMATCH;
        } else {
            result.IdentityVerified = result.OperationInvoked = true;
            result.OperationStatus = operation(operation_context, &expected);
            if (result.OperationStatus != 0) {
                result.PrimaryStatus = VMX_PERSISTENT_KERNEL_OPERATION_FAILED;
            }
        }
    }
restore_affinity:
    status = platform->Restore(platform->Context, &token);
    result.RestoreStatus = status;
    if (status == VMX_PERSISTENT_KERNEL_OK && !token.Pinned) {
        result.AffinityRestored = true;
        result.FinalStatus = result.PrimaryStatus;
    } else {
        if (status == VMX_PERSISTENT_KERNEL_OK) {
            result.RestoreStatus = VMX_PERSISTENT_KERNEL_INVARIANT;
        }
        result.FinalStatus = VMX_PERSISTENT_KERNEL_RESTORE_FAILED;
    }
    *out_result = result;
    return result.FinalStatus;
}

