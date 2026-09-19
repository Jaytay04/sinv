#include "vmx_ept_hook_lifecycle.h"

#include <ntddk.h>

#include "vmx_kernel_adapters.h"

typedef struct HOOK_INVALIDATION_CONTEXT {
    VMX_EPT_INSTALLATION_OWNER* Installation;
    VMX_EPT_EXECUTION_REPORT Execution;
    int32_t InvalidationStatus;
} HOOK_INVALIDATION_CONTEXT;

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

static int Separate(const void* first, SIZE_T first_size,
    const void* second, SIZE_T second_size)
{
    UINT_PTR a = (UINT_PTR)first;
    UINT_PTR b = (UINT_PTR)second;
    return first != NULL && second != NULL && first_size != 0 &&
        second_size != 0 && a <= (UINT_PTR)-1 - first_size &&
        b <= (UINT_PTR)-1 - second_size &&
        (a + first_size <= b || b + second_size <= a);
}

static int InstallationSeparate(
    const VMX_EPT_INSTALLATION_OWNER* installation,
    const void* external, SIZE_T external_size)
{
    return installation != NULL && installation->GraphBytes <= SIZE_MAX &&
        Separate(installation, sizeof(*installation), external, external_size) &&
        Separate(installation->Graph, (SIZE_T)installation->GraphBytes,
            external, external_size);
}

static VMX_EPT_HOOK_LIFECYCLE_STATUS MapOwner(
    VMX_EPT_HOOK_OWNER_STATUS status)
{
    switch (status) {
    case VMX_EPT_HOOK_OWNER_OK:
        return VMX_EPT_HOOK_LIFECYCLE_OK;
    case VMX_EPT_HOOK_OWNER_NOT_FOUND:
        return VMX_EPT_HOOK_LIFECYCLE_NOT_FOUND;
    case VMX_EPT_HOOK_OWNER_BUSY:
        return VMX_EPT_HOOK_LIFECYCLE_BUSY;
    case VMX_EPT_HOOK_OWNER_UNSUPPORTED:
        return VMX_EPT_HOOK_LIFECYCLE_UNSUPPORTED;
    case VMX_EPT_HOOK_OWNER_ALLOCATION_FAILED:
        return VMX_EPT_HOOK_LIFECYCLE_ALLOCATION_FAILED;
    case VMX_EPT_HOOK_OWNER_DIRTY:
        return VMX_EPT_HOOK_LIFECYCLE_DIRTY;
    case VMX_EPT_HOOK_OWNER_INVALID:
        return VMX_EPT_HOOK_LIFECYCLE_INVALID;
    default:
        return VMX_EPT_HOOK_LIFECYCLE_INVARIANT;
    }
}

static int32_t InvalidatePinned(void* opaque,
    const VMX_PROCESSOR_IDENTITY* identity)
{
    HOOK_INVALIDATION_CONTEXT* context =
        (HOOK_INVALIDATION_CONTEXT*)opaque;
    KIRQL previous_irql;

    if (context == NULL || identity == NULL ||
        KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_KERNEL_INVALIDATION_INVALID;
    RtlZeroMemory(&context->Execution, sizeof(context->Execution));
    KeRaiseIrql(DISPATCH_LEVEL, &previous_irql);
    context->InvalidationStatus =
        VmxEptInstallationOwnerCoordinateInvalidation(
            context->Installation, identity, &context->Execution);
    KeLowerIrql(previous_irql);
    return context->InvalidationStatus;
}

static VMX_EPT_HOOK_LIFECYCLE_STATUS DriveInvalidation(
    VMX_EPT_HOOK_OWNER* hooks,
    VMX_EPT_INSTALLATION_OWNER* installation,
    VMX_EPT_HOOK_LIFECYCLE_REPORT* report)
{
    VMX_EPT_SESSION_OWNER* session = hooks->Session;
    uint32_t index;

    for (index = 0; index < installation->Count; ++index) {
        VMX_EPT_COVERAGE_RECORD* coverage =
            &session->CoverageRecords[index];
        HOOK_INVALIDATION_CONTEXT context;
        VMX_PERSISTENT_KERNEL_RESULT kernel;
        VMX_PERSISTENT_KERNEL_STATUS status;

        if (coverage->RequiredGeneration == 0 ||
            coverage->AcknowledgedGeneration ==
                session->Storage->Domain.MutationGeneration)
            continue;
        RtlZeroMemory(&context, sizeof(context));
        RtlZeroMemory(&kernel, sizeof(kernel));
        context.Installation = installation;
        context.InvalidationStatus = VMX_EPT_KERNEL_INVALIDATION_INVALID;
        ++report->InvalidationCalls;
        status = VmxPersistentKernelExecutePinned(
            &KernelPersistentPlatform,
            &installation->Records[index].Identity,
            &context, InvalidatePinned, &kernel);
        report->KernelStatus = status;
        report->InvalidationStatus = context.InvalidationStatus;
        if (status != VMX_PERSISTENT_KERNEL_OK ||
            context.InvalidationStatus != VMX_EPT_KERNEL_INVALIDATION_OK) {
            report->FirstFailureIndex = index;
            return VMX_EPT_HOOK_LIFECYCLE_INVALIDATION_FAILED;
        }
        ++report->InvalidationSuccesses;
    }

    report->Generation = session->Storage->Domain.MutationGeneration;
    if (session->Storage->Domain.PendingGeneration != 0 ||
        session->Storage->Domain.InvalidatedGeneration !=
            session->Storage->Domain.MutationGeneration)
        return VMX_EPT_HOOK_LIFECYCLE_DIRTY;
    return VMX_EPT_HOOK_LIFECYCLE_OK;
}

static int ArgumentsValid(VMX_EPT_HOOK_OWNER* hooks,
    VMX_EPT_INSTALLATION_OWNER* installation,
    VMX_EPT_HOOK_LIFECYCLE_REPORT* report)
{
    return hooks != NULL && installation != NULL && report != NULL &&
        Zero(report, sizeof(*report)) &&
        VmxEptHookOwnerValidate(hooks) == VMX_EPT_HOOK_OWNER_OK &&
        VmxEptInstallationOwnerValidate(installation) ==
            VMX_EPT_INSTALLATION_OWNER_OK &&
        installation->Session == hooks->Session &&
        installation->Count == hooks->Session->Count &&
        VmxEptHookOwnerValidateExternalSpan(
            hooks, report, sizeof(*report)) == VMX_EPT_HOOK_OWNER_OK &&
        InstallationSeparate(installation, report, sizeof(*report));
}

static VMX_EPT_HOOK_LIFECYCLE_STATUS FinishPrepared(
    VMX_EPT_HOOK_OWNER* hooks,
    VMX_EPT_INSTALLATION_OWNER* installation,
    uint32_t hook_id,
    VMX_EPT_HOOK_LIFECYCLE_REPORT* report)
{
    VMX_EPT_HOOK_LIFECYCLE_STATUS status =
        DriveInvalidation(hooks, installation, report);
    VMX_EPT_HOOK_OWNER_STATUS owner_status;
    if (status != VMX_EPT_HOOK_LIFECYCLE_OK)
        return status;
    owner_status = VmxEptHookOwnerCommit(hooks, hook_id);
    report->OwnerStatus = owner_status;
    return MapOwner(owner_status);
}

VMX_EPT_HOOK_LIFECYCLE_STATUS VmxEptHookLifecycleAdd(
    VMX_EPT_HOOK_OWNER* hooks,
    VMX_EPT_INSTALLATION_OWNER* installation,
    uint64_t guest_physical_page,
    uint8_t trap_permissions,
    VMX_EPT_HOOK_LIFECYCLE_REPORT* out_report)
{
    VMX_EPT_HOOK_LIFECYCLE_REPORT report;
    VMX_EPT_HOOK_OWNER_STATUS owner_status;
    VMX_EPT_HOOK_LIFECYCLE_STATUS status;
    uint32_t hook_id = VMX_EPT_HOOK_LIFECYCLE_NO_INDEX;
    uint32_t stage = VMX_EPT_HOOK_STAGE_FREE;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_HOOK_LIFECYCLE_WRONG_IRQL;
    if (!ArgumentsValid(hooks, installation, out_report) ||
        guest_physical_page > UINT64_MAX - (VMX_EPT_HOOK_PAGE_BYTES - 1) ||
        (guest_physical_page & (VMX_EPT_HOOK_PAGE_BYTES - 1)) != 0 ||
        trap_permissions == 0 ||
        (trap_permissions & ~VMX_EPT_HOOK_PERMISSION_MASK) != 0)
        return VMX_EPT_HOOK_LIFECYCLE_INVALID;
    RtlZeroMemory(&report, sizeof(report));
    report.HookId = VMX_EPT_HOOK_LIFECYCLE_NO_INDEX;
    report.FirstFailureIndex = VMX_EPT_HOOK_LIFECYCLE_NO_INDEX;

    owner_status = VmxEptHookOwnerQuery(
        hooks, guest_physical_page, &hook_id, &stage);
    report.OwnerStatus = owner_status;
    report.StageBefore = stage;
    if (owner_status == VMX_EPT_HOOK_OWNER_NOT_FOUND) {
        owner_status = VmxEptHookOwnerPrepare(hooks,
            guest_physical_page, trap_permissions, &hook_id);
        report.OwnerStatus = owner_status;
        if (owner_status != VMX_EPT_HOOK_OWNER_OK) {
            status = MapOwner(owner_status);
            goto done;
        }
        stage = VMX_EPT_HOOK_STAGE_PREPARED;
    } else if (owner_status != VMX_EPT_HOOK_OWNER_OK) {
        status = MapOwner(owner_status);
        goto done;
    } else if (stage != VMX_EPT_HOOK_STAGE_PREPARED) {
        status = VMX_EPT_HOOK_LIFECYCLE_BUSY;
        goto done;
    }

    report.HookId = hook_id;
    status = FinishPrepared(hooks, installation, hook_id, &report);
done:
    if (VmxEptHookOwnerQuery(hooks, guest_physical_page,
            &hook_id, &stage) == VMX_EPT_HOOK_OWNER_OK)
        report.StageAfter = stage;
    *out_report = report;
    return status;
}

VMX_EPT_HOOK_LIFECYCLE_STATUS VmxEptHookLifecycleRemove(
    VMX_EPT_HOOK_OWNER* hooks,
    VMX_EPT_INSTALLATION_OWNER* installation,
    uint64_t guest_physical_page,
    VMX_EPT_HOOK_LIFECYCLE_REPORT* out_report)
{
    VMX_EPT_HOOK_LIFECYCLE_REPORT report;
    VMX_EPT_HOOK_OWNER_STATUS owner_status;
    VMX_EPT_HOOK_LIFECYCLE_STATUS status;
    uint32_t hook_id = VMX_EPT_HOOK_LIFECYCLE_NO_INDEX;
    uint32_t stage = VMX_EPT_HOOK_STAGE_FREE;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_HOOK_LIFECYCLE_WRONG_IRQL;
    if (!ArgumentsValid(hooks, installation, out_report) ||
        (guest_physical_page & (VMX_EPT_HOOK_PAGE_BYTES - 1)) != 0)
        return VMX_EPT_HOOK_LIFECYCLE_INVALID;
    RtlZeroMemory(&report, sizeof(report));
    report.HookId = VMX_EPT_HOOK_LIFECYCLE_NO_INDEX;
    report.FirstFailureIndex = VMX_EPT_HOOK_LIFECYCLE_NO_INDEX;

    owner_status = VmxEptHookOwnerQuery(
        hooks, guest_physical_page, &hook_id, &stage);
    report.OwnerStatus = owner_status;
    report.StageBefore = stage;
    if (owner_status != VMX_EPT_HOOK_OWNER_OK) {
        status = MapOwner(owner_status);
        goto done;
    }
    report.HookId = hook_id;

    if (stage == VMX_EPT_HOOK_STAGE_PREPARED) {
        status = FinishPrepared(hooks, installation, hook_id, &report);
        if (status != VMX_EPT_HOOK_LIFECYCLE_OK)
            goto done;
        stage = VMX_EPT_HOOK_STAGE_COMMITTED;
    }
    if (stage == VMX_EPT_HOOK_STAGE_COMMITTED) {
        owner_status = VmxEptHookOwnerBeginUnhook(
            hooks, guest_physical_page, &hook_id);
        report.OwnerStatus = owner_status;
        if (owner_status != VMX_EPT_HOOK_OWNER_OK) {
            status = MapOwner(owner_status);
            goto done;
        }
        stage = VMX_EPT_HOOK_STAGE_ROLLBACK_PENDING;
    }
    if (stage != VMX_EPT_HOOK_STAGE_ROLLBACK_PENDING) {
        status = VMX_EPT_HOOK_LIFECYCLE_INVARIANT;
        goto done;
    }

    status = DriveInvalidation(hooks, installation, &report);
    if (status != VMX_EPT_HOOK_LIFECYCLE_OK)
        goto done;
    owner_status = VmxEptHookOwnerCompleteUnhook(hooks, hook_id);
    report.OwnerStatus = owner_status;
    status = MapOwner(owner_status);
done:
    if (VmxEptHookOwnerQuery(hooks, guest_physical_page,
            &hook_id, &stage) == VMX_EPT_HOOK_OWNER_OK)
        report.StageAfter = stage;
    else
        report.StageAfter = VMX_EPT_HOOK_STAGE_FREE;
    *out_report = report;
    return status;
}

VMX_EPT_HOOK_LIFECYCLE_STATUS VmxEptHookLifecycleDrain(
    VMX_EPT_HOOK_OWNER* hooks,
    VMX_EPT_INSTALLATION_OWNER* installation,
    VMX_EPT_HOOK_LIFECYCLE_REPORT* out_report)
{
    VMX_EPT_HOOK_LIFECYCLE_REPORT report;
    VMX_EPT_HOOK_LIFECYCLE_REPORT operation;
    VMX_EPT_HOOK_OWNER_STATUS owner_status;
    VMX_EPT_HOOK_LIFECYCLE_STATUS status;
    uint64_t guest_physical_page;
    uint32_t hook_id, stage;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_HOOK_LIFECYCLE_WRONG_IRQL;
    if (!ArgumentsValid(hooks, installation, out_report))
        return VMX_EPT_HOOK_LIFECYCLE_INVALID;
    RtlZeroMemory(&report, sizeof(report));
    report.HookId = VMX_EPT_HOOK_LIFECYCLE_NO_INDEX;
    report.FirstFailureIndex = VMX_EPT_HOOK_LIFECYCLE_NO_INDEX;

    for (;;) {
        owner_status = VmxEptHookOwnerQueryFirst(
            hooks, &guest_physical_page, &hook_id, &stage);
        if (owner_status == VMX_EPT_HOOK_OWNER_NOT_FOUND)
            break;
        if (owner_status != VMX_EPT_HOOK_OWNER_OK) {
            report.OwnerStatus = owner_status;
            status = MapOwner(owner_status);
            goto done;
        }
        RtlZeroMemory(&operation, sizeof(operation));
        status = VmxEptHookLifecycleRemove(hooks, installation,
            guest_physical_page, &operation);
        report = operation;
        if (status != VMX_EPT_HOOK_LIFECYCLE_OK)
            goto done;
    }
    owner_status = VmxEptHookOwnerDestroy(hooks);
    report.OwnerStatus = owner_status;
    status = MapOwner(owner_status);
done:
    *out_report = report;
    return status;
}