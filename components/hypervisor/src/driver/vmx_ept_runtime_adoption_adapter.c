#include "vmx_ept_runtime_adoption_adapter.h"

typedef struct ADAPTER_SPAN {
    UINT_PTR Begin;
    UINT_PTR End;
} ADAPTER_SPAN;

typedef struct ADAPTER_CONTEXT {
    VMX_PERSISTENT_RUNTIME* Runtime;
    VMX_EPT_INSTALLATION_OWNER* Owner;
    const VMX_PERSISTENT_ROOT_PROOF* Proof;
    VMX_PERSISTENT_KERNEL_RESULT Kernel;
    VMX_EPT_RUNTIME_ADOPTION_REPORT Adoption;
    int32_t AdoptionStatus;
    uint32_t VisitorInvoked;
    uint32_t LogicalLease;
} ADAPTER_CONTEXT;

static int Span(const void* value, SIZE_T size, ADAPTER_SPAN* span)
{
    UINT_PTR begin = (UINT_PTR)value;
    if (value == NULL || size == 0 || begin > (UINT_PTR)-1 - size)
        return 0;
    span->Begin = begin;
    span->End = begin + size;
    return 1;
}

static int Separate(const void* first, SIZE_T first_size,
    const void* second, SIZE_T second_size)
{
    ADAPTER_SPAN a, b;
    return Span(first, first_size, &a) && Span(second, second_size, &b) &&
        (a.End <= b.Begin || b.End <= a.Begin);
}

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

static int PlanMatchesProof(const VMX_EPT_RUNTIME_ROOT_PLAN* plan,
    const VMX_PERSISTENT_ROOT_PROOF* proof)
{
    return plan != NULL && proof != NULL &&
        plan->Version == VMX_EPT_RUNTIME_ROOT_PLAN_VERSION &&
        VmxProcessorIdentityEqual(&plan->Identity, &proof->Identity) &&
        plan->VmxonPhysicalAddress == proof->VmxonPhysicalAddress &&
        plan->VmcsPhysicalAddress == proof->VmcsPhysicalAddress &&
        plan->AdoptExistingRoot == 1 && plan->NativeEnterRequired == 0 &&
        plan->Reserved[0] == 0 && plan->Reserved[1] == 0;
}

static int ArgumentsValid(VMX_PERSISTENT_RUNTIME* runtime,
    VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    VMX_EPT_RUNTIME_ADAPTER_REPORT* report)
{
    SIZE_T graph_bytes;
    if (runtime == NULL || owner == NULL || identity == NULL ||
        report == NULL || !Zero(report, sizeof(*report)) ||
        !Separate(runtime, sizeof(*runtime), owner, sizeof(*owner)) ||
        !Separate(runtime, sizeof(*runtime), identity, sizeof(*identity)) ||
        !Separate(runtime, sizeof(*runtime), report, sizeof(*report)) ||
        !Separate(owner, sizeof(*owner), identity, sizeof(*identity)) ||
        !Separate(owner, sizeof(*owner), report, sizeof(*report)) ||
        !Separate(identity, sizeof(*identity), report, sizeof(*report)) ||
        VmxEptInstallationOwnerValidate(owner) !=
            VMX_EPT_INSTALLATION_OWNER_OK ||
        owner->GraphBytes > SIZE_MAX)
        return 0;
    graph_bytes = (SIZE_T)owner->GraphBytes;
    if (!Separate(owner->Graph, graph_bytes, runtime, sizeof(*runtime)) ||
        !Separate(owner->Graph, graph_bytes, identity, sizeof(*identity)) ||
        !Separate(owner->Graph, graph_bytes, report, sizeof(*report)) ||
        identity->Reserved != 0 ||
        identity->Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER ||
        identity->SystemIndex >= owner->Count)
        return 0;
    return VmxProcessorIdentityEqual(identity,
        &owner->Records[identity->SystemIndex].Identity);
}

static int32_t AcquireLease(void* opaque,
    const VMX_PROCESSOR_IDENTITY* identity,
    VMX_PERSISTENT_ROOT_PROOF* out_proof)
{
    ADAPTER_CONTEXT* context = (ADAPTER_CONTEXT*)opaque;
    if (context == NULL || context->Proof == NULL || identity == NULL ||
        out_proof == NULL || context->LogicalLease != 0 ||
        !Zero(out_proof, sizeof(*out_proof)) ||
        !VmxProcessorIdentityEqual(identity, &context->Proof->Identity))
        return VMX_EPT_RUNTIME_ADOPTION_INVALID;
    *out_proof = *context->Proof;
    context->LogicalLease = 1;
    return VMX_EPT_RUNTIME_ADOPTION_OK;
}

static int32_t AdoptExisting(void* opaque,
    const VMX_EPT_RUNTIME_ROOT_PLAN* plan,
    uint32_t* out_published)
{
    ADAPTER_CONTEXT* context = (ADAPTER_CONTEXT*)opaque;
    if (context == NULL || context->LogicalLease != 1 ||
        out_published == NULL || *out_published != 0 ||
        !PlanMatchesProof(plan, context->Proof))
        return VMX_EPT_INSTALLATION_LIFECYCLE_INVALID;
    return VmxEptInstallationAdoptExisting(
        context->Owner, &plan->Identity, out_published);
}

static int32_t RollbackBookkeeping(void* opaque,
    const VMX_EPT_RUNTIME_ROOT_PLAN* plan)
{
    ADAPTER_CONTEXT* context = (ADAPTER_CONTEXT*)opaque;
    if (context == NULL || context->LogicalLease != 1 ||
        !PlanMatchesProof(plan, context->Proof))
        return VMX_EPT_INSTALLATION_LIFECYCLE_INVALID;
    return VmxEptInstallationRollbackAdoption(
        context->Owner, &plan->Identity);
}

static int32_t ReleaseLease(void* opaque,
    const VMX_PROCESSOR_IDENTITY* identity)
{
    ADAPTER_CONTEXT* context = (ADAPTER_CONTEXT*)opaque;
    if (context == NULL || context->Proof == NULL || identity == NULL ||
        context->LogicalLease != 1 ||
        !VmxProcessorIdentityEqual(identity, &context->Proof->Identity))
        return VMX_EPT_RUNTIME_ADOPTION_RELEASE_FAILED;
    context->LogicalLease = 0;
    return VMX_EPT_RUNTIME_ADOPTION_OK;
}

static int32_t CoordinatePinned(void* opaque,
    const VMX_PROCESSOR_IDENTITY* identity)
{
    ADAPTER_CONTEXT* context = (ADAPTER_CONTEXT*)opaque;
    VMX_EPT_RUNTIME_ADOPTION_PLATFORM platform;
    VMX_EPT_RUNTIME_ADOPTION_REPORT report;
    KIRQL previous_irql;

    if (context == NULL || identity == NULL || context->Proof == NULL ||
        KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_RUNTIME_ADOPTION_INVALID;
    RtlZeroMemory(&platform, sizeof(platform));
    RtlZeroMemory(&report, sizeof(report));
    platform.Context = context;
    platform.ContextBytes = sizeof(*context);
    platform.AcquireLease = AcquireLease;
    platform.AdoptExisting = AdoptExisting;
    platform.RollbackBookkeeping = RollbackBookkeeping;
    platform.ReleaseLease = ReleaseLease;

    KeRaiseIrql(DISPATCH_LEVEL, &previous_irql);
    if (previous_irql != PASSIVE_LEVEL ||
        KeGetCurrentIrql() != DISPATCH_LEVEL) {
        KeLowerIrql(previous_irql);
        return VMX_EPT_RUNTIME_ADOPTION_INVALID;
    }
    context->AdoptionStatus = VmxEptCoordinateRuntimeRootAdoption(
        identity, &platform, &report);
    KeLowerIrql(previous_irql);
    context->Adoption = report;
    if (context->LogicalLease != 0)
        return VMX_EPT_RUNTIME_ADOPTION_RELEASE_FAILED;
    return context->AdoptionStatus;
}

static int32_t VisitRoot(void* opaque,
    const VMX_PROCESSOR_IDENTITY* identity,
    const VMX_PERSISTENT_ROOT_PROOF* proof)
{
    ADAPTER_CONTEXT* context = (ADAPTER_CONTEXT*)opaque;
    VMX_PERSISTENT_KERNEL_STATUS status;
    if (context == NULL || identity == NULL || proof == NULL ||
        !VmxProcessorIdentityEqual(identity, &proof->Identity))
        return VMX_PERSISTENT_KERNEL_INVALID;
    context->Proof = proof;
    context->VisitorInvoked = 1;
    status = VmxPersistentKernelExecutePinned(
        &context->Runtime->GuardedPlatform.Platform, identity, context,
        CoordinatePinned, &context->Kernel);
    context->Proof = NULL;
    return status;
}

VMX_EPT_RUNTIME_ADAPTER_STATUS VmxEptAdoptPersistentRuntimeRoot(
    VMX_PERSISTENT_RUNTIME* runtime,
    VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    VMX_EPT_RUNTIME_ADAPTER_REPORT* out_report)
{
    ADAPTER_CONTEXT context;
    VMX_EPT_RUNTIME_ADAPTER_REPORT staged;
    VMX_PERSISTENT_STATUS runtime_status;
    int32_t visitor_status = VMX_PERSISTENT_KERNEL_INVARIANT;
    VMX_EPT_RUNTIME_ADAPTER_STATUS result;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL ||
        !ArgumentsValid(runtime, owner, identity, out_report))
        return VMX_EPT_RUNTIME_ADAPTER_INVALID;
    RtlZeroMemory(&context, sizeof(context));
    RtlZeroMemory(&staged, sizeof(staged));
    context.Runtime = runtime;
    context.Owner = owner;
    context.AdoptionStatus = VMX_EPT_RUNTIME_ADOPTION_INVALID;
    runtime_status = VmxPersistentRuntimeVisitRootProofLease(
        runtime, identity, VisitRoot, &context, sizeof(context),
        &visitor_status);

    staged.RuntimeStatus = runtime_status;
    staged.VisitorStatus = visitor_status;
    staged.AdoptionStatus = context.AdoptionStatus;
    staged.VisitorInvoked = context.VisitorInvoked;
    staged.Kernel = context.Kernel;
    staged.Adoption = context.Adoption;
    if (runtime_status != VMX_PERSISTENT_STATUS_OK)
        result = VMX_EPT_RUNTIME_ADAPTER_RUNTIME_FAILED;
    else if (context.VisitorInvoked != 1)
        result = VMX_EPT_RUNTIME_ADAPTER_INVARIANT;
    else if (context.Kernel.OperationInvoked &&
        context.AdoptionStatus != VMX_EPT_RUNTIME_ADOPTION_OK)
        result = VMX_EPT_RUNTIME_ADAPTER_ADOPTION_FAILED;
    else if (visitor_status != VMX_PERSISTENT_KERNEL_OK)
        result = VMX_EPT_RUNTIME_ADAPTER_KERNEL_FAILED;
    else if (!context.Kernel.OperationInvoked ||
        context.AdoptionStatus != VMX_EPT_RUNTIME_ADOPTION_OK ||
        context.LogicalLease != 0)
        result = VMX_EPT_RUNTIME_ADAPTER_INVARIANT;
    else
        result = VMX_EPT_RUNTIME_ADAPTER_OK;
    RtlSecureZeroMemory(&context, sizeof(context));
    *out_report = staged;
    return result;
}