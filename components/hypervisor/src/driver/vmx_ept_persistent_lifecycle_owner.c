#include "vmx_ept_persistent_lifecycle_owner.h"

#include "vmx_ept_installation_lifecycle.h"
#include "vmx_ept_installation_execution.h"
#include "vmx_ept_invalidation_execution.h"
#include "vmx_ept_session_operations.h"
#include "vmx_kernel_adapters.h"

#define ROOT_OPERATION_ROLLBACK UINT32_C(1)
#define ROOT_OPERATION_RESUME_RELEASE UINT32_C(2)
#define ROOT_OPERATION_RELEASE UINT32_C(3)

typedef struct INSTALLATION_ACTIVATION_CONTEXT {
    VMX_EPT_INSTALLATION_OWNER* Installation;
    VMX_EPT_INSTALLATION_REPORT Report;
    int32_t Status;
} INSTALLATION_ACTIVATION_CONTEXT;

typedef struct INITIAL_INVALIDATION_CONTEXT {
    VMX_EPT_INSTALLATION_OWNER* Installation;
    VMX_EPT_EXECUTION_REPORT Execution;
    int32_t Status;
} INITIAL_INVALIDATION_CONTEXT;

typedef struct ROOT_OPERATION_CONTEXT {
    VMX_EPT_INSTALLATION_OWNER* Installation;
    uint32_t Operation;
    int32_t Status;
} ROOT_OPERATION_CONTEXT;

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
    UINT_PTR first_begin = (UINT_PTR)first;
    UINT_PTR second_begin = (UINT_PTR)second;

    return first != NULL && second != NULL && first_size != 0 &&
        second_size != 0 &&
        first_begin <= (UINT_PTR)-1 - first_size &&
        second_begin <= (UINT_PTR)-1 - second_size &&
        (first_begin + first_size <= second_begin ||
         second_begin + second_size <= first_begin);
}

static void Publish(VMX_EPT_PERSISTENT_LIFECYCLE_OWNER* owner,
    VMX_PERSISTENT_DRIVER_OWNER* runtime_owner, uint32_t stage)
{
    owner->RuntimeOwner = runtime_owner;
    owner->RuntimeOwnerContract = runtime_owner;
    owner->Version = VMX_EPT_PERSISTENT_LIFECYCLE_VERSION;
    owner->VersionContract = ~owner->Version;
    owner->Stage = stage;
    owner->StageContract = ~stage;
    owner->MagicContract = ~VMX_EPT_PERSISTENT_LIFECYCLE_MAGIC;
    KeMemoryBarrier();
    owner->Magic = VMX_EPT_PERSISTENT_LIFECYCLE_MAGIC;
}

static void SetStage(VMX_EPT_PERSISTENT_LIFECYCLE_OWNER* owner,
    uint32_t stage)
{
    owner->Stage = stage;
    KeMemoryBarrier();
    owner->StageContract = ~stage;
}

static int RootsActive(const VMX_EPT_COMPOSITION_OWNER* composition)
{
    const VMX_EPT_SESSION_OWNER* session = &composition->Session;
    const VMX_EPT_ROOT_COORDINATOR* root = session->Root;
    const VMX_EPT_DOMAIN* domain = &session->Storage->Domain;
    uint32_t index;

    if (composition->Stage != VMX_EPT_COMPOSITION_OWNER_READY ||
        root->PendingTransition != VMX_EPT_ROOT_TRANSITION_NONE ||
        root->PendingIndex != VMX_EPT_ROOT_NO_INDEX ||
        root->OwnedCount != composition->Count ||
        domain->Stage != VMX_EPT_DOMAIN_STAGE_ROOT_OWNED ||
        domain->PendingGeneration != 0 ||
        domain->MutationGeneration != domain->InvalidatedGeneration)
        return 0;
    for (index = 0; index < composition->Count; ++index) {
        const VMX_EPT_COVERAGE_RECORD* coverage =
            &session->CoverageRecords[index];
        if (root->Records[index].RootOwned != 1 ||
            composition->Installation.Records[index].
                NativeContext.RootOwned != 1 ||
            coverage->RequiredGeneration != domain->MutationGeneration ||
            coverage->AcknowledgedGeneration !=
                domain->InvalidatedGeneration)
            return 0;
    }
    return 1;
}

static int RootsReleased(const VMX_EPT_COMPOSITION_OWNER* composition)
{
    const VMX_EPT_SESSION_OWNER* session = &composition->Session;
    const VMX_EPT_ROOT_COORDINATOR* root = session->Root;
    const VMX_EPT_DOMAIN* domain = &session->Storage->Domain;
    uint32_t index;

    if (composition->Stage != VMX_EPT_COMPOSITION_OWNER_READY ||
        root->PendingTransition != VMX_EPT_ROOT_TRANSITION_NONE ||
        root->PendingIndex != VMX_EPT_ROOT_NO_INDEX || root->OwnedCount != 0 ||
        domain->Stage != VMX_EPT_DOMAIN_STAGE_READY ||
        domain->PendingGeneration != 0 ||
        domain->MutationGeneration != domain->InvalidatedGeneration)
        return 0;
    for (index = 0; index < composition->Count; ++index) {
        if (root->Records[index].RootOwned != 0 ||
            composition->Installation.Records[index].
                NativeContext.RootOwned != 0 ||
            session->CoverageRecords[index].RequiredGeneration != 0 ||
            session->CoverageRecords[index].AcknowledgedGeneration != 0)
            return 0;
    }
    return 1;
}

static int OwnerValid(const VMX_EPT_PERSISTENT_LIFECYCLE_OWNER* owner)
{
    VMX_EPT_COMPOSITION_OWNER_STATUS status;

    if (owner == NULL ||
        owner->Magic != VMX_EPT_PERSISTENT_LIFECYCLE_MAGIC ||
        owner->MagicContract != ~VMX_EPT_PERSISTENT_LIFECYCLE_MAGIC ||
        owner->Version != VMX_EPT_PERSISTENT_LIFECYCLE_VERSION ||
        owner->VersionContract != ~owner->Version ||
        owner->StageContract != ~owner->Stage ||
        owner->Reserved[0] != 0 || owner->Reserved[1] != 0 ||
        owner->RuntimeOwner == NULL ||
        owner->RuntimeOwnerContract != owner->RuntimeOwner ||
        owner->DestroyReport == NULL ||
        owner->DestroyReportContract != owner->DestroyReport ||
        !Separate(owner, sizeof(*owner), owner->RuntimeOwner,
            sizeof(*owner->RuntimeOwner)) ||
        !Separate(owner, sizeof(*owner), owner->DestroyReport,
            sizeof(*owner->DestroyReport)) ||
        !Separate(owner->RuntimeOwner, sizeof(*owner->RuntimeOwner),
            owner->DestroyReport, sizeof(*owner->DestroyReport)))
        return 0;
    if (owner->Stage == VMX_EPT_PERSISTENT_LIFECYCLE_COMPOSITION_GONE)
        return Zero(&owner->Composition, sizeof(owner->Composition));
    if (owner->Stage == VMX_EPT_PERSISTENT_LIFECYCLE_BLOCKED)
        return !Zero(&owner->Composition, sizeof(owner->Composition));
    status = VmxEptCompositionOwnerValidate(&owner->Composition);
    if (status != VMX_EPT_COMPOSITION_OWNER_OK)
        return 0;
    if (owner->Stage == VMX_EPT_PERSISTENT_LIFECYCLE_READY)
        return RootsActive(&owner->Composition);
    if (owner->Stage == VMX_EPT_PERSISTENT_LIFECYCLE_ADOPTION_FAILED ||
        owner->Stage == VMX_EPT_PERSISTENT_LIFECYCLE_ROOTS_RELEASE_PENDING)
        return owner->Composition.Stage == VMX_EPT_COMPOSITION_OWNER_READY;
    if (owner->Stage == VMX_EPT_PERSISTENT_LIFECYCLE_ROOTS_RELEASED)
        return RootsReleased(&owner->Composition);
    return owner->Stage == VMX_EPT_PERSISTENT_LIFECYCLE_SESSION_ONLY &&
        owner->Composition.Stage == VMX_EPT_COMPOSITION_OWNER_SESSION_ONLY;
}

static int32_t ValidateReport(void* context,
    const VMX_PERSISTENT_VMCS_BINDING* bindings, size_t count)
{
    return context != NULL && bindings != NULL && count != 0 ?
        VMX_PERSISTENT_STATUS_OK : VMX_PERSISTENT_STATUS_INVALID;
}

static VMX_EPT_PERSISTENT_LIFECYCLE_STATUS InitResult(int32_t status)
{
    if (status == VMX_EPT_PERSISTENT_COMPOSITION_RUNTIME_FAILED)
        return VMX_EPT_PERSISTENT_LIFECYCLE_RUNTIME_FAILED;
    if (status == VMX_EPT_PERSISTENT_COMPOSITION_INITIALIZE_FAILED)
        return VMX_EPT_PERSISTENT_LIFECYCLE_INITIALIZE_FAILED;
    return VMX_EPT_PERSISTENT_LIFECYCLE_INVARIANT;
}

static int32_t InvalidatePinned(void* opaque,
    const VMX_PROCESSOR_IDENTITY* identity)
{
    INITIAL_INVALIDATION_CONTEXT* context =
        (INITIAL_INVALIDATION_CONTEXT*)opaque;
    KIRQL previous_irql;

    if (context == NULL || identity == NULL ||
        KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_KERNEL_INVALIDATION_INVALID;
    RtlZeroMemory(&context->Execution, sizeof(context->Execution));
    KeRaiseIrql(DISPATCH_LEVEL, &previous_irql);
    context->Status = VmxEptInstallationOwnerCoordinateInvalidation(
        context->Installation, identity, &context->Execution);
    KeLowerIrql(previous_irql);
    return context->Status;
}

static VMX_EPT_PERSISTENT_LIFECYCLE_STATUS DriveInitialInvalidation(
    VMX_EPT_COMPOSITION_OWNER* composition,
    VMX_EPT_PERSISTENT_LIFECYCLE_REPORT* report,
    uint32_t require_active)
{
    VMX_EPT_SESSION_OWNER* session = &composition->Session;
    uint32_t index;

    for (index = 0; index < composition->Count; ++index) {
        INITIAL_INVALIDATION_CONTEXT context;
        VMX_PERSISTENT_KERNEL_RESULT kernel;
        VMX_PERSISTENT_KERNEL_STATUS kernel_status;
        VMX_EPT_COVERAGE_RECORD* coverage =
            &session->CoverageRecords[index];

        if (coverage->RequiredGeneration ==
                coverage->AcknowledgedGeneration)
            continue;
        RtlZeroMemory(&context, sizeof(context));
        RtlZeroMemory(&kernel, sizeof(kernel));
        context.Installation = &composition->Installation;
        context.Status = VMX_EPT_KERNEL_INVALIDATION_INVALID;
        ++report->InitialInvalidationCalls;
        kernel_status = VmxPersistentKernelExecutePinned(
            &KernelPersistentPlatform,
            &composition->Installation.Records[index].Identity,
            &context, InvalidatePinned, &kernel);
        report->InitialKernelStatus = kernel_status;
        report->InitialInvalidationStatus = context.Status;
        if (kernel_status != VMX_PERSISTENT_KERNEL_OK ||
            context.Status != VMX_EPT_KERNEL_INVALIDATION_OK) {
            report->InitialInvalidationFailureIndex = index;
            return VMX_EPT_PERSISTENT_LIFECYCLE_ADOPTION_FAILED_STATUS;
        }
        ++report->InitialInvalidationSuccesses;
    }
    if (require_active != 0 && !RootsActive(composition))
        return VMX_EPT_PERSISTENT_LIFECYCLE_INVARIANT;
    return VMX_EPT_PERSISTENT_LIFECYCLE_OK;
}

static int32_t ActivatePinned(void* opaque,
    const VMX_PROCESSOR_IDENTITY* identity)
{
    INSTALLATION_ACTIVATION_CONTEXT* context =
        (INSTALLATION_ACTIVATION_CONTEXT*)opaque;
    KIRQL previous_irql;

    if (context == NULL || identity == NULL ||
        KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_KERNEL_INSTALLATION_INVALID;
    RtlZeroMemory(&context->Report, sizeof(context->Report));
    KeRaiseIrql(DISPATCH_LEVEL, &previous_irql);
    context->Status = VmxEptInstallationOwnerCoordinate(
        context->Installation, identity, &context->Report);
    KeLowerIrql(previous_irql);
    return context->Status;
}
static VMX_EPT_PERSISTENT_LIFECYCLE_STATUS AdoptAll(
    VMX_PERSISTENT_DRIVER_OWNER* runtime_owner,
    VMX_EPT_COMPOSITION_OWNER* composition,
    VMX_EPT_PERSISTENT_LIFECYCLE_REPORT* report)
{
    uint32_t index;

    for (index = 0; index < composition->Count; ++index) {
        VMX_PROCESSOR_IDENTITY identity =
            composition->Installation.Records[index].Identity;
        VMX_EPT_RUNTIME_ADAPTER_STATUS adoption_status;
        INSTALLATION_ACTIVATION_CONTEXT activation;
        VMX_PERSISTENT_KERNEL_RESULT kernel;
        VMX_PERSISTENT_KERNEL_STATUS kernel_status;

        RtlZeroMemory(&report->Adoption, sizeof(report->Adoption));
        ++report->AdoptionCalls;
        adoption_status = VmxEptAdoptPersistentRuntimeRoot(
            &runtime_owner->Runtime, &composition->Installation,
            &identity, &report->Adoption);
        report->AdoptionStatus = adoption_status;
        if (adoption_status != VMX_EPT_RUNTIME_ADAPTER_OK) {
            report->AdoptionFailureIndex = index;
            (void)DriveInitialInvalidation(composition, report, 0);
            return VMX_EPT_PERSISTENT_LIFECYCLE_ADOPTION_FAILED_STATUS;
        }
        ++report->AdoptionSuccesses;

        RtlZeroMemory(&activation, sizeof(activation));
        RtlZeroMemory(&kernel, sizeof(kernel));
        activation.Installation = &composition->Installation;
        activation.Status = VMX_EPT_KERNEL_INSTALLATION_INVALID;
        ++report->ActivationCalls;
        ++report->CoverageCalls;
        kernel_status = VmxPersistentKernelExecutePinned(
            &KernelPersistentPlatform, &identity,
            &activation, ActivatePinned, &kernel);
        report->ActivationKernelStatus = kernel_status;
        report->ActivationStatus = activation.Status;
        report->Activation = activation.Report;
        if (activation.Report.CapabilityChecked != 0) {
            report->CoverageStatus = VMX_EPT_SESSION_OPERATION_OK;
            ++report->CoverageSuccesses;
        }
        if (kernel_status != VMX_PERSISTENT_KERNEL_OK ||
            activation.Status != VMX_EPT_KERNEL_INSTALLATION_OK) {
            report->ActivationFailureIndex = index;
            report->AdoptionFailureIndex = index;
            (void)DriveInitialInvalidation(composition, report, 0);
            return VMX_EPT_PERSISTENT_LIFECYCLE_ADOPTION_FAILED_STATUS;
        }
        ++report->ActivationSuccesses;
    }
    return DriveInitialInvalidation(composition, report, 1);
}

static int32_t RunRootOperation(void* opaque,
    const VMX_PROCESSOR_IDENTITY* identity)
{
    ROOT_OPERATION_CONTEXT* context = (ROOT_OPERATION_CONTEXT*)opaque;
    KIRQL previous_irql;

    if (context == NULL || identity == NULL ||
        KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_INSTALLATION_LIFECYCLE_INVALID;
    KeRaiseIrql(DISPATCH_LEVEL, &previous_irql);
    if (context->Operation == ROOT_OPERATION_ROLLBACK) {
        context->Status = VmxEptInstallationRollbackAdoption(
            context->Installation, identity);
    } else if (context->Operation == ROOT_OPERATION_RESUME_RELEASE) {
        context->Status = VmxEptInstallationCompleteRelease(
            context->Installation, identity, 1);
    } else if (context->Operation == ROOT_OPERATION_RELEASE) {
        context->Status = VmxEptInstallationPrepareRelease(
            context->Installation, identity);
        if (context->Status == VMX_EPT_INSTALLATION_LIFECYCLE_OK)
            context->Status = VmxEptInstallationCompleteRelease(
                context->Installation, identity, 1);
    } else {
        context->Status = VMX_EPT_INSTALLATION_LIFECYCLE_INVALID;
    }
    KeLowerIrql(previous_irql);
    return context->Status;
}

static int RootOperation(VMX_EPT_COMPOSITION_OWNER* composition,
    uint32_t index, uint32_t operation,
    VMX_EPT_PERSISTENT_LIFECYCLE_REPORT* report)
{
    ROOT_OPERATION_CONTEXT context;
    VMX_PERSISTENT_KERNEL_RESULT kernel;
    VMX_PERSISTENT_KERNEL_STATUS kernel_status;
    VMX_PROCESSOR_IDENTITY identity =
        composition->Installation.Records[index].Identity;

    RtlZeroMemory(&context, sizeof(context));
    RtlZeroMemory(&kernel, sizeof(kernel));
    context.Installation = &composition->Installation;
    context.Operation = operation;
    context.Status = VMX_EPT_INSTALLATION_LIFECYCLE_INVALID;
    kernel_status = VmxPersistentKernelExecutePinned(
        &KernelPersistentPlatform, &identity,
        &context, RunRootOperation, &kernel);
    report->RootKernelStatus = kernel_status;
    report->RootCleanupStatus = context.Status;
    return kernel_status == VMX_PERSISTENT_KERNEL_OK &&
        context.Status == VMX_EPT_INSTALLATION_LIFECYCLE_OK;
}

static VMX_EPT_PERSISTENT_LIFECYCLE_STATUS CleanupRoots(
    VMX_EPT_COMPOSITION_OWNER* composition,
    VMX_EPT_PERSISTENT_LIFECYCLE_REPORT* report)
{
    VMX_EPT_ROOT_COORDINATOR* root = composition->Session.Root;
    uint32_t index;

    if (root->PendingTransition == VMX_EPT_ROOT_TRANSITION_ENTER) {
        index = root->PendingIndex;
        ++report->RootRollbackCalls;
        if (index >= composition->Count ||
            !RootOperation(composition, index,
                ROOT_OPERATION_ROLLBACK, report)) {
            report->RootCleanupFailureIndex = index;
            return VMX_EPT_PERSISTENT_LIFECYCLE_ROOT_CLEANUP_FAILED;
        }
    }
    if (root->PendingTransition == VMX_EPT_ROOT_TRANSITION_RELEASE) {
        index = root->PendingIndex;
        ++report->RootReleaseCalls;
        if (index >= composition->Count ||
            !RootOperation(composition, index,
                ROOT_OPERATION_RESUME_RELEASE, report)) {
            report->RootCleanupFailureIndex = index;
            return VMX_EPT_PERSISTENT_LIFECYCLE_ROOT_CLEANUP_FAILED;
        }
        ++report->RootReleaseSuccesses;
    }
    for (index = 0; index < composition->Count; ++index) {
        uint32_t session_owned = root->Records[index].RootOwned;
        int32_t native_owned = composition->Installation.Records[index].
            NativeContext.RootOwned;

        if (session_owned == 0 && native_owned == 0)
            continue;
        ++report->RootReleaseCalls;
        if (session_owned != 1 || native_owned != 1 ||
            !RootOperation(composition, index,
                ROOT_OPERATION_RELEASE, report)) {
            report->RootCleanupFailureIndex = index;
            return VMX_EPT_PERSISTENT_LIFECYCLE_ROOT_CLEANUP_FAILED;
        }
        ++report->RootReleaseSuccesses;
    }
    return RootsReleased(composition) ? VMX_EPT_PERSISTENT_LIFECYCLE_OK :
        VMX_EPT_PERSISTENT_LIFECYCLE_INVARIANT;
}

VMX_EPT_PERSISTENT_LIFECYCLE_STATUS VmxEptPersistentLifecycleOwnerValidate(
    const VMX_EPT_PERSISTENT_LIFECYCLE_OWNER* owner)
{
    return OwnerValid(owner) ? VMX_EPT_PERSISTENT_LIFECYCLE_OK :
        VMX_EPT_PERSISTENT_LIFECYCLE_INVALID;
}

VMX_EPT_PERSISTENT_LIFECYCLE_STATUS VmxEptPersistentLifecycleOwnerInitialize(
    VMX_PERSISTENT_DRIVER_OWNER* runtime_owner,
    VMX_EPT_PERSISTENT_LIFECYCLE_OWNER* owner,
    VMX_EPT_PERSISTENT_LIFECYCLE_REPORT* out_report)
{
    VMX_EPT_PERSISTENT_LIFECYCLE_REPORT report = { 0 };
    VMX_EPT_PERSISTENT_COMPOSITION_STATUS composition_status;
    VMX_EPT_PERSISTENT_LIFECYCLE_STATUS result;
    VMX_PERSISTENT_STATUS runtime_status;
    int32_t visited = VMX_PERSISTENT_STATUS_INVALID;
    uint32_t stage;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_PERSISTENT_LIFECYCLE_WRONG_IRQL;
    if (runtime_owner == NULL || owner == NULL || out_report == NULL ||
        !Zero(owner, sizeof(*owner)) || !Zero(out_report, sizeof(*out_report)) ||
        !Separate(runtime_owner, sizeof(*runtime_owner), owner, sizeof(*owner)) ||
        !Separate(runtime_owner, sizeof(*runtime_owner), out_report,
            sizeof(*out_report)) ||
        !Separate(owner, sizeof(*owner), out_report, sizeof(*out_report)))
        return VMX_EPT_PERSISTENT_LIFECYCLE_INVALID;

    report.AdoptionFailureIndex = VMX_EPT_PERSISTENT_LIFECYCLE_NO_INDEX;
    report.ActivationFailureIndex = VMX_EPT_PERSISTENT_LIFECYCLE_NO_INDEX;
    report.InitialInvalidationFailureIndex =
        VMX_EPT_PERSISTENT_LIFECYCLE_NO_INDEX;
    report.RootCleanupFailureIndex = VMX_EPT_PERSISTENT_LIFECYCLE_NO_INDEX;
    runtime_status = VmxPersistentDriverOwnerVisitVmcsBindings(
        runtime_owner, ValidateReport, out_report, sizeof(*out_report),
        &visited);
    if (runtime_status != VMX_PERSISTENT_STATUS_OK ||
        visited != VMX_PERSISTENT_STATUS_OK)
        return VMX_EPT_PERSISTENT_LIFECYCLE_RUNTIME_FAILED;
    report.SpanCalls = 1;
    report.SpanStatus = runtime_status;
    report.SpanVisitorStatus = visited;

    composition_status = VmxEptPersistentCompositionInitialize(
        runtime_owner, &owner->Composition, &report.Initialization);
    report.InitializeCalls = 1;
    report.InitializeStatus = composition_status;
    if (composition_status == VMX_EPT_PERSISTENT_COMPOSITION_OK) {
        result = AdoptAll(runtime_owner, &owner->Composition, &report);
        stage = result == VMX_EPT_PERSISTENT_LIFECYCLE_OK ?
            VMX_EPT_PERSISTENT_LIFECYCLE_READY :
            VMX_EPT_PERSISTENT_LIFECYCLE_ADOPTION_FAILED;
    } else if (VmxEptCompositionOwnerValidate(&owner->Composition) ==
            VMX_EPT_COMPOSITION_OWNER_OK &&
        owner->Composition.Stage == VMX_EPT_COMPOSITION_OWNER_SESSION_ONLY) {
        stage = VMX_EPT_PERSISTENT_LIFECYCLE_SESSION_ONLY;
        result = VMX_EPT_PERSISTENT_LIFECYCLE_INITIALIZE_FAILED;
    } else if (Zero(&owner->Composition, sizeof(owner->Composition))) {
        result = InitResult(composition_status);
        goto done;
    } else {
        stage = VMX_EPT_PERSISTENT_LIFECYCLE_BLOCKED;
        result = VMX_EPT_PERSISTENT_LIFECYCLE_BLOCKED_STATUS;
    }

    owner->DestroyReport = out_report;
    owner->DestroyReportContract = out_report;
    KeMemoryBarrier();
    Publish(owner, runtime_owner, stage);
    report.StageAfter = stage;
    if (!OwnerValid(owner))
        result = VMX_EPT_PERSISTENT_LIFECYCLE_INVARIANT;
    else if (stage == VMX_EPT_PERSISTENT_LIFECYCLE_READY)
        result = VMX_EPT_PERSISTENT_LIFECYCLE_OK;
    else if (stage == VMX_EPT_PERSISTENT_LIFECYCLE_ADOPTION_FAILED &&
        result == VMX_EPT_PERSISTENT_LIFECYCLE_OK)
        result = VMX_EPT_PERSISTENT_LIFECYCLE_ADOPTION_FAILED_STATUS;

done:
    *out_report = report;
    return result;
}

VMX_EPT_PERSISTENT_LIFECYCLE_STATUS VmxEptPersistentLifecycleOwnerDestroy(
    VMX_EPT_PERSISTENT_LIFECYCLE_OWNER* owner,
    VMX_EPT_PERSISTENT_LIFECYCLE_REPORT* out_report)
{
    VMX_EPT_PERSISTENT_LIFECYCLE_REPORT report = { 0 };
    VMX_PERSISTENT_DRIVER_OWNER_PHASE phase;
    VMX_PERSISTENT_STATUS runtime_status;
    VMX_EPT_COMPOSITION_OWNER_STATUS composition_status;
    VMX_EPT_PERSISTENT_LIFECYCLE_STATUS result;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_PERSISTENT_LIFECYCLE_WRONG_IRQL;
    if (owner == NULL || out_report == NULL ||
        !Separate(owner, sizeof(*owner), out_report, sizeof(*out_report)) ||
        !Zero(out_report, sizeof(*out_report)))
        return VMX_EPT_PERSISTENT_LIFECYCLE_INVALID;
    if (Zero(owner, sizeof(*owner))) {
        *out_report = report;
        return VMX_EPT_PERSISTENT_LIFECYCLE_OK;
    }
    if (!OwnerValid(owner))
        return VMX_EPT_PERSISTENT_LIFECYCLE_INVALID;
    if (owner->Stage == VMX_EPT_PERSISTENT_LIFECYCLE_BLOCKED)
        return VMX_EPT_PERSISTENT_LIFECYCLE_BLOCKED_STATUS;
    if (owner->DestroyReport != out_report)
        return VMX_EPT_PERSISTENT_LIFECYCLE_INVALID;
    if (owner->Stage != VMX_EPT_PERSISTENT_LIFECYCLE_COMPOSITION_GONE &&
        VmxEptCompositionOwnerValidateExternalSpan(
            &owner->Composition, out_report, sizeof(*out_report)) !=
                VMX_EPT_COMPOSITION_OWNER_OK)
        return VMX_EPT_PERSISTENT_LIFECYCLE_INVALID;

    report.AdoptionFailureIndex = VMX_EPT_PERSISTENT_LIFECYCLE_NO_INDEX;
    report.ActivationFailureIndex = VMX_EPT_PERSISTENT_LIFECYCLE_NO_INDEX;
    report.InitialInvalidationFailureIndex =
        VMX_EPT_PERSISTENT_LIFECYCLE_NO_INDEX;
    report.RootCleanupFailureIndex = VMX_EPT_PERSISTENT_LIFECYCLE_NO_INDEX;
    report.StageBefore = owner->Stage;
    phase = VMX_PERSISTENT_DRIVER_OWNER_UNINITIALIZED;
    runtime_status = VmxPersistentDriverOwnerQueryPhase(
        owner->RuntimeOwner, &phase);
    report.PhaseBeforeCalls = 1;
    report.PhaseBeforeStatus = runtime_status;
    report.RuntimePhaseBefore = phase;
    if (runtime_status != VMX_PERSISTENT_STATUS_OK) {
        result = VMX_EPT_PERSISTENT_LIFECYCLE_RUNTIME_FAILED;
        goto done;
    }
    if (owner->Stage != VMX_EPT_PERSISTENT_LIFECYCLE_COMPOSITION_GONE &&
        phase != VMX_PERSISTENT_DRIVER_OWNER_ATTACHED) {
        SetStage(owner, VMX_EPT_PERSISTENT_LIFECYCLE_BLOCKED);
        result = VMX_EPT_PERSISTENT_LIFECYCLE_BLOCKED_STATUS;
        goto done;
    }
    if (owner->Stage == VMX_EPT_PERSISTENT_LIFECYCLE_COMPOSITION_GONE &&
        phase != VMX_PERSISTENT_DRIVER_OWNER_ATTACHED &&
        phase != VMX_PERSISTENT_DRIVER_OWNER_TEARDOWN_PENDING) {
        result = VMX_EPT_PERSISTENT_LIFECYCLE_INVARIANT;
        goto done;
    }

    if (owner->Stage == VMX_EPT_PERSISTENT_LIFECYCLE_READY ||
        owner->Stage == VMX_EPT_PERSISTENT_LIFECYCLE_ADOPTION_FAILED)
        SetStage(owner, VMX_EPT_PERSISTENT_LIFECYCLE_ROOTS_RELEASE_PENDING);
    if (owner->Stage == VMX_EPT_PERSISTENT_LIFECYCLE_ROOTS_RELEASE_PENDING) {
        result = CleanupRoots(&owner->Composition, &report);
        if (result != VMX_EPT_PERSISTENT_LIFECYCLE_OK)
            goto done;
        SetStage(owner, VMX_EPT_PERSISTENT_LIFECYCLE_ROOTS_RELEASED);
    }

    if (owner->Stage == VMX_EPT_PERSISTENT_LIFECYCLE_ROOTS_RELEASED ||
        owner->Stage == VMX_EPT_PERSISTENT_LIFECYCLE_SESSION_ONLY) {
        composition_status = VmxEptCompositionOwnerDestroy(
            &owner->Composition, &report.CompositionDestroy);
        report.CompositionDestroyCalls = 1;
        report.CompositionDestroyStatus = composition_status;
        if (composition_status != VMX_EPT_COMPOSITION_OWNER_OK) {
            if (VmxEptCompositionOwnerValidate(&owner->Composition) !=
                    VMX_EPT_COMPOSITION_OWNER_OK) {
                SetStage(owner, VMX_EPT_PERSISTENT_LIFECYCLE_BLOCKED);
                result = VMX_EPT_PERSISTENT_LIFECYCLE_BLOCKED_STATUS;
            } else if (owner->Composition.Stage ==
                    VMX_EPT_COMPOSITION_OWNER_READY) {
                SetStage(owner,
                    VMX_EPT_PERSISTENT_LIFECYCLE_ROOTS_RELEASED);
                result = VMX_EPT_PERSISTENT_LIFECYCLE_COMPOSITION_FAILED;
            } else {
                SetStage(owner,
                    VMX_EPT_PERSISTENT_LIFECYCLE_SESSION_ONLY);
                result = VMX_EPT_PERSISTENT_LIFECYCLE_COMPOSITION_FAILED;
            }
            goto done;
        }
        if (!Zero(&owner->Composition, sizeof(owner->Composition))) {
            SetStage(owner, VMX_EPT_PERSISTENT_LIFECYCLE_BLOCKED);
            result = VMX_EPT_PERSISTENT_LIFECYCLE_BLOCKED_STATUS;
            goto done;
        }
        SetStage(owner, VMX_EPT_PERSISTENT_LIFECYCLE_COMPOSITION_GONE);
    }

    runtime_status = VmxPersistentDriverOwnerDetach(owner->RuntimeOwner);
    report.DetachCalls = 1;
    report.DetachStatus = runtime_status;
    if (runtime_status != VMX_PERSISTENT_STATUS_OK) {
        result = VMX_EPT_PERSISTENT_LIFECYCLE_RUNTIME_FAILED;
        goto done;
    }
    phase = VMX_PERSISTENT_DRIVER_OWNER_UNINITIALIZED;
    runtime_status = VmxPersistentDriverOwnerQueryPhase(
        owner->RuntimeOwner, &phase);
    report.PhaseAfterCalls = 1;
    report.PhaseAfterStatus = runtime_status;
    report.RuntimePhaseAfter = phase;
    if (runtime_status != VMX_PERSISTENT_STATUS_OK ||
        phase != VMX_PERSISTENT_DRIVER_OWNER_DETACHED) {
        result = VMX_EPT_PERSISTENT_LIFECYCLE_INVARIANT;
        goto done;
    }
    RtlSecureZeroMemory(owner, sizeof(*owner));
    result = VMX_EPT_PERSISTENT_LIFECYCLE_OK;

done:
    report.StageAfter = Zero(owner, sizeof(*owner)) ? 0 : owner->Stage;
    *out_report = report;
    return result;
}
