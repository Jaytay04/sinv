#include "vmx_ept_persistent_device_status.h"

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

static int Separate(const void* first, SIZE_T first_size,
    const void* second, SIZE_T second_size)
{
    UINT_PTR a = (UINT_PTR)first, b = (UINT_PTR)second;
    return first && second && first_size && second_size &&
        a <= (UINT_PTR)-1 - first_size && b <= (UINT_PTR)-1 - second_size &&
        (a + first_size <= b || b + second_size <= a);
}

static void CopyStart(const VMX_EPT_PERSISTENT_DEVICE_START_REPORT* source,
    VMX_EPT_PERSISTENT_STATUS_START_EVIDENCE* target)
{
    target->AttachStatus = source->AttachStatus;
    target->StartStatus = source->StartStatus;
    target->EptInitializeStatus = source->EptInitializeStatus;
    target->RollbackStatus = source->RollbackStatus;
    target->AttachCalls = source->AttachCalls;
    target->StartCalls = source->StartCalls;
    target->EptInitializeCalls = source->EptInitializeCalls;
    target->RollbackCalls = source->RollbackCalls;
    target->EptStageAfter = source->EptStageAfter;
    target->DiagnosticValid = source->StartDiagnostic.Valid;
    target->DiagnosticKind = source->StartDiagnostic.Kind;
    target->DiagnosticSystemIndex = source->StartDiagnostic.SystemIndex;
    target->DiagnosticGroup = source->StartDiagnostic.Group;
    target->DiagnosticNumber = source->StartDiagnostic.Number;
    target->DiagnosticCpuStage = source->StartDiagnostic.CpuStage;
    target->DiagnosticLifecycleStatus = source->StartDiagnostic.LifecycleStatus;
}

static void CopyDestroy(const VMX_EPT_PERSISTENT_LIFECYCLE_REPORT* source,
    VMX_EPT_PERSISTENT_STATUS_DESTROY_EVIDENCE* target)
{
    target->CompositionDestroyStatus = source->CompositionDestroyStatus;
    target->DetachStatus = source->DetachStatus;
    target->PhaseAfterStatus = source->PhaseAfterStatus;
    target->CompositionDestroyCalls = source->CompositionDestroyCalls;
    target->DetachCalls = source->DetachCalls;
    target->PhaseAfterCalls = source->PhaseAfterCalls;
    target->StageBefore = source->StageBefore;
    target->StageAfter = source->StageAfter;
    target->RuntimePhaseBefore = source->RuntimePhaseBefore;
    target->RuntimePhaseAfter = source->RuntimePhaseAfter;
}

C_ASSERT((uint32_t)VMX_EPT_PERSISTENT_STATUS_PHASE_DETACHED ==
    (uint32_t)VMX_PERSISTENT_DRIVER_OWNER_DETACHED);
C_ASSERT((uint32_t)VMX_EPT_PERSISTENT_STATUS_PHASE_ATTACHED ==
    (uint32_t)VMX_PERSISTENT_DRIVER_OWNER_ATTACHED);
C_ASSERT((uint32_t)VMX_EPT_PERSISTENT_STATUS_PHASE_TEARDOWN_PENDING ==
    (uint32_t)VMX_PERSISTENT_DRIVER_OWNER_TEARDOWN_PENDING);
C_ASSERT(VMX_EPT_PERSISTENT_STATUS_STAGE_READY ==
    VMX_EPT_PERSISTENT_LIFECYCLE_READY);
C_ASSERT(VMX_EPT_PERSISTENT_STATUS_STAGE_SESSION_ONLY ==
    VMX_EPT_PERSISTENT_LIFECYCLE_SESSION_ONLY);
C_ASSERT(VMX_EPT_PERSISTENT_STATUS_STAGE_COMPOSITION_GONE ==
    VMX_EPT_PERSISTENT_LIFECYCLE_COMPOSITION_GONE);
C_ASSERT(VMX_EPT_PERSISTENT_STATUS_STAGE_BLOCKED ==
    VMX_EPT_PERSISTENT_LIFECYCLE_BLOCKED);

VMX_EPT_PERSISTENT_DEVICE_STATUS_RESULT
VmxEptPersistentDeviceStatusProject(
    VMX_PERSISTENT_DEVICE_CONTEXT* context,
    VMX_EPT_PERSISTENT_STATUS_RECORD* output)
{
    VMX_EPT_PERSISTENT_STATUS_VIEW view = {0};
    VMX_PERSISTENT_DRIVER_OWNER_PHASE phase =
        VMX_PERSISTENT_DRIVER_OWNER_UNINITIALIZED;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_PERSISTENT_DEVICE_STATUS_WRONG_IRQL;
    if (!context || !output ||
        !Separate(context, sizeof(*context), output, sizeof(*output)))
        return VMX_EPT_PERSISTENT_DEVICE_STATUS_INVALID;
    view.Version = VMX_EPT_PERSISTENT_STATUS_MODEL_VERSION;
    view.Size = sizeof(view);
    view.PhaseQueryStatus = VmxPersistentDriverOwnerQueryPhase(
        &context->Owner, &phase);
    view.RuntimePhase = (uint32_t)phase;
    view.EptZero = StorageZero(&context->EptLifecycle,
        sizeof(context->EptLifecycle));
    if (!view.EptZero) {
        view.EptValidateStatus = VmxEptPersistentLifecycleOwnerValidate(
            &context->EptLifecycle);
        view.EptStage = context->EptLifecycle.Stage;
    }
    view.StartEvidenceValid = !StorageZero(&context->EptStartReport,
        sizeof(context->EptStartReport));
    if (view.StartEvidenceValid) {
        if (context->EptStartReport.Reserved != 0)
            return VMX_EPT_PERSISTENT_DEVICE_STATUS_INVALID;
        CopyStart(&context->EptStartReport, &view.Start);
    }
    view.DestroyEvidenceValid = !StorageZero(&context->EptDestroyReport,
        sizeof(context->EptDestroyReport));
    if (view.DestroyEvidenceValid)
        CopyDestroy(&context->EptDestroyReport, &view.Destroy);
    if (!VmxEptPersistentStatusModelProject(&view, output))
        return VMX_EPT_PERSISTENT_DEVICE_STATUS_MODEL_FAILED;
    return VMX_EPT_PERSISTENT_DEVICE_STATUS_OK;
}
