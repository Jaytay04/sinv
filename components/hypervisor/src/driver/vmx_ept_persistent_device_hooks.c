#include "vmx_ept_persistent_device_hooks.h"

static int Zero(const void* value, SIZE_T size)
{
    const UCHAR* bytes = (const UCHAR*)value;
    SIZE_T index;
    if (value == NULL)
        return 0;
    for (index = 0; index < size; ++index)
        if (bytes[index] != 0)
            return 0;
    return 1;
}

static VMX_EPT_PERSISTENT_DEVICE_HOOK_STATUS MapLifecycle(
    VMX_EPT_HOOK_LIFECYCLE_STATUS status)
{
    switch (status) {
    case VMX_EPT_HOOK_LIFECYCLE_OK:
        return VMX_EPT_PERSISTENT_DEVICE_HOOK_OK;
    case VMX_EPT_HOOK_LIFECYCLE_NOT_FOUND:
        return VMX_EPT_PERSISTENT_DEVICE_HOOK_NOT_FOUND;
    case VMX_EPT_HOOK_LIFECYCLE_BUSY:
        return VMX_EPT_PERSISTENT_DEVICE_HOOK_BUSY;
    case VMX_EPT_HOOK_LIFECYCLE_UNSUPPORTED:
        return VMX_EPT_PERSISTENT_DEVICE_HOOK_UNSUPPORTED;
    case VMX_EPT_HOOK_LIFECYCLE_ALLOCATION_FAILED:
        return VMX_EPT_PERSISTENT_DEVICE_HOOK_ALLOCATION_FAILED;
    case VMX_EPT_HOOK_LIFECYCLE_INVALIDATION_FAILED:
        return VMX_EPT_PERSISTENT_DEVICE_HOOK_INVALIDATION_FAILED;
    case VMX_EPT_HOOK_LIFECYCLE_DIRTY:
        return VMX_EPT_PERSISTENT_DEVICE_HOOK_DIRTY;
    case VMX_EPT_HOOK_LIFECYCLE_WRONG_IRQL:
        return VMX_EPT_PERSISTENT_DEVICE_HOOK_WRONG_IRQL;
    case VMX_EPT_HOOK_LIFECYCLE_INVALID:
        return VMX_EPT_PERSISTENT_DEVICE_HOOK_INVALID;
    default:
        return VMX_EPT_PERSISTENT_DEVICE_HOOK_INVARIANT;
    }
}

static int Ready(VMX_PERSISTENT_DEVICE_CONTEXT* context)
{
    VMX_PERSISTENT_DRIVER_OWNER_PHASE phase =
        VMX_PERSISTENT_DRIVER_OWNER_UNINITIALIZED;
    return context != NULL &&
        VmxPersistentDriverOwnerQueryPhase(&context->Owner, &phase) ==
            VMX_PERSISTENT_STATUS_OK &&
        phase == VMX_PERSISTENT_DRIVER_OWNER_ATTACHED &&
        VmxEptPersistentLifecycleOwnerValidate(&context->EptLifecycle) ==
            VMX_EPT_PERSISTENT_LIFECYCLE_OK &&
        context->EptLifecycle.Stage ==
            VMX_EPT_PERSISTENT_LIFECYCLE_READY &&
        VmxEptCompositionOwnerValidate(
            &context->EptLifecycle.Composition) ==
            VMX_EPT_COMPOSITION_OWNER_OK &&
        context->EptLifecycle.Composition.Stage ==
            VMX_EPT_COMPOSITION_OWNER_READY;
}

static void TraceAddFailure(
    VMX_PERSISTENT_DEVICE_CONTEXT* context,
    uint64_t guest_physical_page,
    VMX_EPT_PERSISTENT_DEVICE_HOOK_STATUS device_status,
    VMX_EPT_HOOK_OWNER_STATUS ensure_status,
    VMX_EPT_HOOK_LIFECYCLE_STATUS lifecycle_status)
{
    VMX_EPT_SESSION_OWNER* session =
        &context->EptLifecycle.Composition.Session;
    VMX_EPT_DOMAIN* domain = &session->Storage->Domain;
    VMX_EPT_HOOK_LOCATION location = { 0 };
    VMX_EPT_HOOK_LOCATION_STATUS location_status;
    VMX_EPT_HOOK_OWNER_STATUS owner_validation;
    uint32_t index;
    uint32_t used = 0;

    for (index = 0; index < session->Count; ++index) {
        used += session->CoverageRecords[index].RequiredGeneration != 0;
    }
    location_status = VmxEptHookLocate2Mb(
        domain, guest_physical_page, &location);
    owner_validation = Zero(&context->EptHooks,
        sizeof(context->EptHooks)) ? VMX_EPT_HOOK_OWNER_NOT_FOUND :
        VmxEptHookOwnerValidate(&context->EptHooks);

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "hypersinv: gate8 hook-add failure device=%d ensure=%d "
        "lifecycle=%d owner=%d owner_validate=%d location=%d "
        "leaf=%u count=%u owned=%u used=%u gpa=%I64X mapped=%I64X "
        "parent=%I64X mutation=%I64u pending=%I64u invalidated=%I64u "
        "stage_before=%u stage_after=%u invalidation=%u/%u\n",
        device_status, ensure_status, lifecycle_status,
        context->EptHookReport.OwnerStatus, owner_validation,
        location_status, domain->Identity.LeafKind, session->Count,
        session->Root->OwnedCount, used, guest_physical_page,
        domain->Identity.MappedBytes, location.ObservedParentEntry,
        domain->MutationGeneration, domain->PendingGeneration,
        domain->InvalidatedGeneration,
        context->EptHookReport.StageBefore,
        context->EptHookReport.StageAfter,
        context->EptHookReport.InvalidationSuccesses,
        context->EptHookReport.InvalidationCalls);
}
static VMX_EPT_PERSISTENT_DEVICE_HOOK_STATUS EnsureOwner(
    VMX_PERSISTENT_DEVICE_CONTEXT* context,
    VMX_EPT_HOOK_OWNER_STATUS* ensure_status)
{
    VMX_EPT_SESSION_OWNER* session =
        &context->EptLifecycle.Composition.Session;
    VMX_EPT_HOOK_OWNER_STATUS status;

    if (!Zero(&context->EptHooks, sizeof(context->EptHooks))) {
        status = VmxEptHookOwnerValidate(&context->EptHooks);
        *ensure_status = status;
        return status == VMX_EPT_HOOK_OWNER_OK ?
            VMX_EPT_PERSISTENT_DEVICE_HOOK_OK :
            VMX_EPT_PERSISTENT_DEVICE_HOOK_INVARIANT;
    }
    status = VmxEptHookOwnerInitialize(session,
        VMX_EPT_PERSISTENT_HOOK_CAPACITY, &session->Allocator,
        &context->EptHooks);
    *ensure_status = status;
    if (status == VMX_EPT_HOOK_OWNER_OK)
        return VMX_EPT_PERSISTENT_DEVICE_HOOK_OK;
    if (status == VMX_EPT_HOOK_OWNER_ALLOCATION_FAILED)
        return VMX_EPT_PERSISTENT_DEVICE_HOOK_ALLOCATION_FAILED;
    return VMX_EPT_PERSISTENT_DEVICE_HOOK_INVARIANT;
}

VMX_EPT_PERSISTENT_DEVICE_HOOK_STATUS VmxEptPersistentDeviceHookAdd(
    VMX_PERSISTENT_DEVICE_CONTEXT* context,
    uint64_t guest_physical_page,
    uint8_t trap_permissions)
{
    VMX_EPT_PERSISTENT_DEVICE_HOOK_STATUS status;
    VMX_EPT_HOOK_OWNER_STATUS ensure_status = VMX_EPT_HOOK_OWNER_INVALID;
    VMX_EPT_HOOK_LIFECYCLE_STATUS lifecycle_status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_PERSISTENT_DEVICE_HOOK_WRONG_IRQL;
    if (!Ready(context))
        return VMX_EPT_PERSISTENT_DEVICE_HOOK_NOT_READY;
    status = EnsureOwner(context, &ensure_status);
    if (status != VMX_EPT_PERSISTENT_DEVICE_HOOK_OK) {
        TraceAddFailure(context, guest_physical_page, status,
            ensure_status, VMX_EPT_HOOK_LIFECYCLE_INVALID);
        return status;
    }
    RtlSecureZeroMemory(
        &context->EptHookReport, sizeof(context->EptHookReport));
    lifecycle_status = VmxEptHookLifecycleAdd(
        &context->EptHooks,
        &context->EptLifecycle.Composition.Installation,
        guest_physical_page, trap_permissions,
        &context->EptHookReport);
    status = MapLifecycle(lifecycle_status);
    if (status != VMX_EPT_PERSISTENT_DEVICE_HOOK_OK) {
        TraceAddFailure(context, guest_physical_page, status,
            ensure_status, lifecycle_status);
    }
    return status;
}

VMX_EPT_PERSISTENT_DEVICE_HOOK_STATUS VmxEptPersistentDeviceHookRemove(
    VMX_PERSISTENT_DEVICE_CONTEXT* context,
    uint64_t guest_physical_page)
{
    VMX_EPT_HOOK_LIFECYCLE_STATUS lifecycle_status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_PERSISTENT_DEVICE_HOOK_WRONG_IRQL;
    if (!Ready(context))
        return VMX_EPT_PERSISTENT_DEVICE_HOOK_NOT_READY;
    if (Zero(&context->EptHooks, sizeof(context->EptHooks)))
        return VMX_EPT_PERSISTENT_DEVICE_HOOK_NOT_FOUND;
    if (VmxEptHookOwnerValidate(&context->EptHooks) !=
            VMX_EPT_HOOK_OWNER_OK)
        return VMX_EPT_PERSISTENT_DEVICE_HOOK_INVARIANT;
    RtlSecureZeroMemory(
        &context->EptHookReport, sizeof(context->EptHookReport));
    lifecycle_status = VmxEptHookLifecycleRemove(
        &context->EptHooks,
        &context->EptLifecycle.Composition.Installation,
        guest_physical_page, &context->EptHookReport);
    return MapLifecycle(lifecycle_status);
}

VMX_EPT_PERSISTENT_DEVICE_HOOK_STATUS VmxEptPersistentDeviceHookDrain(
    VMX_PERSISTENT_DEVICE_CONTEXT* context)
{
    VMX_EPT_HOOK_LIFECYCLE_STATUS lifecycle_status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_PERSISTENT_DEVICE_HOOK_WRONG_IRQL;
    if (context == NULL)
        return VMX_EPT_PERSISTENT_DEVICE_HOOK_INVALID;
    if (Zero(&context->EptHooks, sizeof(context->EptHooks)))
        return VMX_EPT_PERSISTENT_DEVICE_HOOK_OK;
    if (!Ready(context))
        return VMX_EPT_PERSISTENT_DEVICE_HOOK_NOT_READY;
    RtlSecureZeroMemory(
        &context->EptHookReport, sizeof(context->EptHookReport));
    lifecycle_status = VmxEptHookLifecycleDrain(
        &context->EptHooks,
        &context->EptLifecycle.Composition.Installation,
        &context->EptHookReport);
    return MapLifecycle(lifecycle_status);
}
