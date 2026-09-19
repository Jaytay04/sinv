#include "vmx_ept_persistent_device_teardown.h"

#include "vmx_ept_persistent_device_hooks.h"
#include "vmx_ept_hook_roundtrip.h"

static int StorageZero(const void* storage, SIZE_T size)
{
    const UCHAR* bytes = (const UCHAR*)storage;
    SIZE_T index;

    if (storage == NULL)
        return 0;
    for (index = 0; index < size; ++index)
        if (bytes[index] != 0)
            return 0;
    return 1;
}

static VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_STATUS
FinishDetached(VMX_PERSISTENT_DEVICE_CONTEXT* context)
{
    VMX_PERSISTENT_DRIVER_OWNER_PHASE phase =
        VMX_PERSISTENT_DRIVER_OWNER_UNINITIALIZED;
    HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE target = { 0 };
    VMX_PROCESS_TARGET_OWNER_STATUS target_status;

    if (VmxPersistentDriverOwnerQueryPhase(&context->Owner, &phase) !=
            VMX_PERSISTENT_STATUS_OK)
        return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_PHASE_FAILED;
    if (phase != VMX_PERSISTENT_DRIVER_OWNER_DETACHED ||
        !StorageZero(&context->EptLifecycle,
            sizeof(context->EptLifecycle)) ||
        !StorageZero(&context->EptHooks, sizeof(context->EptHooks)) ||
        !StorageZero(&context->VpidLifecycle,
            sizeof(context->VpidLifecycle)) ||
        !StorageZero(&context->EptHookRoundtrip,
            sizeof(context->EptHookRoundtrip)))
        return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_INVARIANT;
    target_status = VmxProcessTargetOwnerQuery(
        &context->ProcessTarget, &target);
    if (target_status != VMX_PROCESS_TARGET_OWNER_OK)
        return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_INVARIANT;
    if (target.State == HYPERVISOR_PROCESS_TARGET_STATE_ACTIVE) {
        target_status = VmxProcessTargetOwnerDeactivate(
            &context->ProcessTarget);
        if (target_status != VMX_PROCESS_TARGET_OWNER_OK)
            return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_INVARIANT;
        RtlSecureZeroMemory(&target, sizeof(target));
        target_status = VmxProcessTargetOwnerQuery(
            &context->ProcessTarget, &target);
        if (target_status != VMX_PROCESS_TARGET_OWNER_OK)
            return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_INVARIANT;
    }
    if (target.State == HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED ||
        target.State == HYPERVISOR_PROCESS_TARGET_STATE_EXPIRED) {
        if (VmxProcessTargetOwnerRelease(&context->ProcessTarget) !=
                VMX_PROCESS_TARGET_OWNER_OK)
            return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_INVARIANT;
    } else if (target.State != HYPERVISOR_PROCESS_TARGET_STATE_EMPTY) {
        return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_INVARIANT;
    }
    RtlSecureZeroMemory(&target, sizeof(target));
    return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_OK;
}

VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_STATUS
VmxEptPersistentDeviceTeardown(VMX_PERSISTENT_DEVICE_CONTEXT* context)
{
    VMX_PERSISTENT_DRIVER_OWNER_PHASE phase =
        VMX_PERSISTENT_DRIVER_OWNER_UNINITIALIZED;
    VMX_EPT_PERSISTENT_LIFECYCLE_STATUS ept_status;
    VMX_EPT_PERSISTENT_DEVICE_HOOK_STATUS hook_status;
    VMX_PERSISTENT_STATUS runtime_status;
    uint32_t stage;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_WRONG_IRQL;
    if (context == NULL)
        return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_INVALID;
    runtime_status = VmxPersistentDriverOwnerQueryPhase(
        &context->Owner, &phase);
    if (runtime_status != VMX_PERSISTENT_STATUS_OK)
        return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_PHASE_FAILED;

    if (StorageZero(&context->EptLifecycle,
            sizeof(context->EptLifecycle))) {
        if (!StorageZero(&context->EptHooks, sizeof(context->EptHooks)) ||
        !StorageZero(&context->VpidLifecycle,
            sizeof(context->VpidLifecycle)) ||
        !StorageZero(&context->EptHookRoundtrip,
            sizeof(context->EptHookRoundtrip)))
            return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_INVARIANT;
        if (phase == VMX_PERSISTENT_DRIVER_OWNER_DETACHED)
            return FinishDetached(context);
        if (phase == VMX_PERSISTENT_DRIVER_OWNER_ATTACHED) {
            runtime_status = VmxPersistentDriverOwnerStop(&context->Owner);
            if (runtime_status != VMX_PERSISTENT_STATUS_OK)
                return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_STOP_FAILED;
        } else if (phase !=
                VMX_PERSISTENT_DRIVER_OWNER_TEARDOWN_PENDING) {
            return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_INVARIANT;
        }
        runtime_status = VmxPersistentDriverOwnerDetach(&context->Owner);
        if (runtime_status != VMX_PERSISTENT_STATUS_OK)
            return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_DETACH_FAILED;
        return FinishDetached(context);
    }

    if (VmxEptPersistentLifecycleOwnerValidate(
            &context->EptLifecycle) !=
            VMX_EPT_PERSISTENT_LIFECYCLE_OK)
        return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_INVALID;
    stage = context->EptLifecycle.Stage;
    if (stage == VMX_EPT_PERSISTENT_LIFECYCLE_BLOCKED)
        return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_BLOCKED;
    if (stage == VMX_EPT_PERSISTENT_LIFECYCLE_READY ||
        stage == VMX_EPT_PERSISTENT_LIFECYCLE_SESSION_ONLY ||
        stage == VMX_EPT_PERSISTENT_LIFECYCLE_ADOPTION_FAILED ||
        stage == VMX_EPT_PERSISTENT_LIFECYCLE_ROOTS_RELEASE_PENDING ||
        stage == VMX_EPT_PERSISTENT_LIFECYCLE_ROOTS_RELEASED) {
        if (phase != VMX_PERSISTENT_DRIVER_OWNER_ATTACHED)
            return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_INVARIANT;
        if (stage == VMX_EPT_PERSISTENT_LIFECYCLE_READY) {
            hook_status = VmxEptPersistentDeviceHookDrain(context);
            if (hook_status != VMX_EPT_PERSISTENT_DEVICE_HOOK_OK)
                return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_HOOK_FAILED;
            if (VmxEptHookRoundtripRelease(context) !=
                    VMX_EPT_HOOK_ROUNDTRIP_OK)
                return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_HOOK_FAILED;
            if (!StorageZero(&context->VpidLifecycle,
                    sizeof(context->VpidLifecycle)) &&
                VmxVpidPersistentOwnerDestroy(&context->VpidLifecycle) !=
                    VMX_VPID_PERSISTENT_OK)
                return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_VPID_FAILED;
        } else if (!StorageZero(&context->EptHooks,
                sizeof(context->EptHooks)) ||
            !StorageZero(&context->VpidLifecycle,
                sizeof(context->VpidLifecycle)) ||
            !StorageZero(&context->EptHookRoundtrip,
                sizeof(context->EptHookRoundtrip))) {
            return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_INVARIANT;
        }
        runtime_status = VmxPersistentDriverOwnerStop(&context->Owner);
        if (runtime_status != VMX_PERSISTENT_STATUS_OK)
            return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_STOP_FAILED;
    } else if (stage == VMX_EPT_PERSISTENT_LIFECYCLE_COMPOSITION_GONE) {
        if (!StorageZero(&context->EptHooks, sizeof(context->EptHooks)) ||
        !StorageZero(&context->VpidLifecycle,
            sizeof(context->VpidLifecycle)) ||
        !StorageZero(&context->EptHookRoundtrip,
            sizeof(context->EptHookRoundtrip)))
            return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_INVARIANT;
        if (phase == VMX_PERSISTENT_DRIVER_OWNER_ATTACHED) {
            runtime_status = VmxPersistentDriverOwnerStop(&context->Owner);
            if (runtime_status != VMX_PERSISTENT_STATUS_OK)
                return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_STOP_FAILED;
        } else if (phase !=
                VMX_PERSISTENT_DRIVER_OWNER_TEARDOWN_PENDING) {
            return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_INVARIANT;
        }
    } else {
        return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_INVARIANT;
    }

    ept_status = VmxEptPersistentDeviceLifecycleDestroy(context);
    if (ept_status != VMX_EPT_PERSISTENT_LIFECYCLE_OK) {
        if (ept_status == VMX_EPT_PERSISTENT_LIFECYCLE_BLOCKED_STATUS)
            return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_BLOCKED;
        if (context->EptDestroyReport.DetachCalls != 0 &&
            context->EptDestroyReport.DetachStatus !=
                VMX_PERSISTENT_STATUS_OK)
            return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_DETACH_FAILED;
        return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_EPT_FAILED;
    }
    return FinishDetached(context);
}
