#include "vmx_ept_persistent_status_model.h"

#include <string.h>

static int Zero(const void* value, size_t size)
{
    const unsigned char* bytes = (const unsigned char*)value;
    size_t index;
    for (index = 0; index < size; ++index)
        if (bytes[index] != 0)
            return 0;
    return 1;
}

static int Separate(const void* first, size_t first_size,
    const void* second, size_t second_size)
{
    uintptr_t a = (uintptr_t)first, b = (uintptr_t)second;
    return first && second && first_size && second_size &&
        a <= UINTPTR_MAX - first_size && b <= UINTPTR_MAX - second_size &&
        (a + first_size <= b || b + second_size <= a);
}

static int EvidenceValid(const VMX_EPT_PERSISTENT_STATUS_VIEW* view)
{
    const VMX_EPT_PERSISTENT_STATUS_START_EVIDENCE* start = &view->Start;
    const VMX_EPT_PERSISTENT_STATUS_DESTROY_EVIDENCE* destroy = &view->Destroy;
    if ((!view->StartEvidenceValid && !Zero(start, sizeof(*start))) ||
        (!view->DestroyEvidenceValid && !Zero(destroy, sizeof(*destroy))))
        return 0;
    if (view->StartEvidenceValid &&
        (start->AttachCalls > 1 || start->StartCalls > 1 ||
         start->EptInitializeCalls > 1 || start->RollbackCalls > 1 ||
         start->DiagnosticValid > 1 ||
         (start->DiagnosticValid &&
          (start->DiagnosticKind > 11 || start->DiagnosticNumber >= 64))))
        return 0;
    if (view->DestroyEvidenceValid &&
        (destroy->CompositionDestroyCalls > 1 || destroy->DetachCalls > 1 ||
         destroy->PhaseAfterCalls > 1))
        return 0;
    return 1;
}

static uint32_t Classify(const VMX_EPT_PERSISTENT_STATUS_VIEW* view)
{
    if (view->PhaseQueryStatus != 0)
        return VMX_EPT_PERSISTENT_STATUS_BLOCKED;
    if (view->RuntimePhase > VMX_EPT_PERSISTENT_STATUS_PHASE_TEARDOWN_PENDING)
        return VMX_EPT_PERSISTENT_STATUS_MALFORMED;
    if (view->EptZero) {
        if (view->EptStage != VMX_EPT_PERSISTENT_STATUS_STAGE_ZERO ||
            view->EptValidateStatus != 0)
            return VMX_EPT_PERSISTENT_STATUS_MALFORMED;
        if (view->RuntimePhase == VMX_EPT_PERSISTENT_STATUS_PHASE_DETACHED)
            return VMX_EPT_PERSISTENT_STATUS_DETACHED;
        if (view->RuntimePhase ==
            VMX_EPT_PERSISTENT_STATUS_PHASE_TEARDOWN_PENDING)
            return VMX_EPT_PERSISTENT_STATUS_TEARDOWN_PENDING;
        return VMX_EPT_PERSISTENT_STATUS_MALFORMED;
    }
    if (view->EptValidateStatus != 0 ||
        view->EptStage < VMX_EPT_PERSISTENT_STATUS_STAGE_READY ||
        view->EptStage > VMX_EPT_PERSISTENT_STATUS_STAGE_BLOCKED)
        return VMX_EPT_PERSISTENT_STATUS_MALFORMED;
    if (view->EptStage == VMX_EPT_PERSISTENT_STATUS_STAGE_BLOCKED)
        return VMX_EPT_PERSISTENT_STATUS_BLOCKED;
    if (view->EptStage == VMX_EPT_PERSISTENT_STATUS_STAGE_READY &&
        view->RuntimePhase == VMX_EPT_PERSISTENT_STATUS_PHASE_ATTACHED)
        return VMX_EPT_PERSISTENT_STATUS_READY;
    if ((view->EptStage == VMX_EPT_PERSISTENT_STATUS_STAGE_SESSION_ONLY &&
         view->RuntimePhase == VMX_EPT_PERSISTENT_STATUS_PHASE_ATTACHED) ||
        (view->EptStage == VMX_EPT_PERSISTENT_STATUS_STAGE_COMPOSITION_GONE &&
         (view->RuntimePhase == VMX_EPT_PERSISTENT_STATUS_PHASE_ATTACHED ||
          view->RuntimePhase ==
            VMX_EPT_PERSISTENT_STATUS_PHASE_TEARDOWN_PENDING)))
        return VMX_EPT_PERSISTENT_STATUS_TEARDOWN_PENDING;
    return VMX_EPT_PERSISTENT_STATUS_MALFORMED;
}

int VmxEptPersistentStatusModelProject(
    const VMX_EPT_PERSISTENT_STATUS_VIEW* view,
    VMX_EPT_PERSISTENT_STATUS_RECORD* output)
{
    VMX_EPT_PERSISTENT_STATUS_RECORD record = {0};
    if (!view || !output || !Separate(view, sizeof(*view), output, sizeof(*output)) ||
        !Zero(output, sizeof(*output)) ||
        view->Version != VMX_EPT_PERSISTENT_STATUS_MODEL_VERSION ||
        view->Size != sizeof(*view) || view->Reserved[0] || view->Reserved[1] ||
        view->ReservedState || view->EptZero > 1 ||
        view->StartEvidenceValid > 1 || view->DestroyEvidenceValid > 1 ||
        !EvidenceValid(view))
        return 0;
    record.Version = VMX_EPT_PERSISTENT_STATUS_MODEL_VERSION;
    record.Size = sizeof(record);
    record.Classification = Classify(view);
    record.Flags = (view->PhaseQueryStatus == 0 ?
        VMX_EPT_PERSISTENT_STATUS_FLAG_RUNTIME_AUTHENTICATED : 0) |
        (view->EptZero ? VMX_EPT_PERSISTENT_STATUS_FLAG_EPT_ZERO : 0) |
        (!view->EptZero && view->EptValidateStatus == 0 ?
            VMX_EPT_PERSISTENT_STATUS_FLAG_EPT_AUTHENTICATED : 0) |
        (view->StartEvidenceValid ? VMX_EPT_PERSISTENT_STATUS_FLAG_START_EVIDENCE : 0) |
        (view->DestroyEvidenceValid ? VMX_EPT_PERSISTENT_STATUS_FLAG_DESTROY_EVIDENCE : 0);
    if (record.Classification == VMX_EPT_PERSISTENT_STATUS_TEARDOWN_PENDING ||
        record.Classification == VMX_EPT_PERSISTENT_STATUS_BLOCKED)
        record.Flags |= VMX_EPT_PERSISTENT_STATUS_FLAG_CLEANUP_REQUIRED;
    record.PhaseQueryStatus = view->PhaseQueryStatus;
    record.EptValidateStatus = view->EptValidateStatus;
    record.RuntimePhase = view->RuntimePhase;
    record.EptStage = view->EptStage;
    record.Start = view->Start;
    record.Destroy = view->Destroy;
    memcpy(output, &record, sizeof(record));
    return 1;
}
