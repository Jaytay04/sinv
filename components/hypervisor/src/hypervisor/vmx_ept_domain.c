#include "vmx_ept_domain.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int RangesSeparate(
    const void* first,
    size_t first_size,
    const void* second,
    size_t second_size)
{
    uintptr_t first_begin = (uintptr_t)first;
    uintptr_t second_begin = (uintptr_t)second;
    uintptr_t first_end;
    uintptr_t second_end;
    if (first_begin > UINTPTR_MAX - first_size ||
        second_begin > UINTPTR_MAX - second_size)
        return 0;
    first_end = first_begin + first_size;
    second_end = second_begin + second_size;
    return first_end <= second_begin || second_end <= first_begin;
}

static int DomainZero(const VMX_EPT_DOMAIN* domain)
{
    const unsigned char* bytes = (const unsigned char*)domain;
    size_t i;
    for (i = 0; i < sizeof(*domain); ++i)
        if (bytes[i] != 0)
            return 0;
    return 1;
}

static int BindingSeparate(
    const void* object,
    size_t object_size,
    const VMX_EPT_STORAGE_BINDING* binding)
{
    size_t bound_bytes;
    size_t index_bytes;
    if (binding->VirtualBytes > SIZE_MAX ||
        binding->PageCount > SIZE_MAX / sizeof(VMX_EPT_BOUND_PAGE) ||
        binding->PageCount > SIZE_MAX / sizeof(VMX_EPT_PHYSICAL_PAGE))
        return 0;
    bound_bytes = (size_t)binding->PageCount * sizeof(VMX_EPT_BOUND_PAGE);
    index_bytes = (size_t)binding->PageCount * sizeof(VMX_EPT_PHYSICAL_PAGE);
    return RangesSeparate(object, object_size, binding->VirtualBase,
            (size_t)binding->VirtualBytes) &&
        RangesSeparate(object, object_size, binding->PagesByOrdinal,
            bound_bytes) &&
        RangesSeparate(object, object_size, binding->PhysicalIndex,
            index_bytes);
}

static int InitializeInputsSeparate(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* identity,
    const VMX_EPT_IDENTITY_PLAN* plan,
    const VMX_EPT_POPULATE_CONFIG* populate,
    const VMX_EPT_STORAGE_BINDING* binding,
    const VMX_EPT_DOMAIN* domain)
{
    return RangesSeparate(capabilities, sizeof(*capabilities), identity,
            sizeof(*identity)) &&
        RangesSeparate(capabilities, sizeof(*capabilities), plan,
            sizeof(*plan)) &&
        RangesSeparate(capabilities, sizeof(*capabilities), populate,
            sizeof(*populate)) &&
        RangesSeparate(capabilities, sizeof(*capabilities), binding,
            sizeof(*binding)) &&
        RangesSeparate(identity, sizeof(*identity), plan, sizeof(*plan)) &&
        RangesSeparate(identity, sizeof(*identity), populate,
            sizeof(*populate)) &&
        RangesSeparate(identity, sizeof(*identity), binding,
            sizeof(*binding)) &&
        RangesSeparate(plan, sizeof(*plan), populate, sizeof(*populate)) &&
        RangesSeparate(plan, sizeof(*plan), binding, sizeof(*binding)) &&
        RangesSeparate(populate, sizeof(*populate), binding,
            sizeof(*binding)) &&
        RangesSeparate(domain, sizeof(*domain), capabilities,
            sizeof(*capabilities)) &&
        RangesSeparate(domain, sizeof(*domain), identity, sizeof(*identity)) &&
        RangesSeparate(domain, sizeof(*domain), plan, sizeof(*plan)) &&
        RangesSeparate(domain, sizeof(*domain), populate, sizeof(*populate)) &&
        RangesSeparate(domain, sizeof(*domain), binding, sizeof(*binding));
}

static int InitializeStorageSeparate(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* identity,
    const VMX_EPT_IDENTITY_PLAN* plan,
    const VMX_EPT_POPULATE_CONFIG* populate,
    const VMX_EPT_STORAGE_BINDING* binding_object,
    const VMX_EPT_STORAGE_BINDING* storage,
    const VMX_EPT_DOMAIN* domain)
{
    return BindingSeparate(capabilities, sizeof(*capabilities), storage) &&
        BindingSeparate(identity, sizeof(*identity), storage) &&
        BindingSeparate(plan, sizeof(*plan), storage) &&
        BindingSeparate(populate, sizeof(*populate), storage) &&
        BindingSeparate(binding_object, sizeof(*binding_object), storage) &&
        BindingSeparate(domain, sizeof(*domain), storage);
}

static VMX_EPT_DOMAIN_STATUS MapStatus(VMX_EPT_STATUS status)
{
    return status == VMX_EPT_STATUS_UNSUPPORTED ?
        VMX_EPT_DOMAIN_UNSUPPORTED : VMX_EPT_DOMAIN_INVALID;
}

static int DomainAuthentic(const VMX_EPT_DOMAIN* domain)
{
    uint64_t expected_pointer;
    if (domain == 0 || domain->Magic != VMX_EPT_DOMAIN_MAGIC ||
        domain->Version != VMX_EPT_DOMAIN_VERSION || domain->Reserved32 != 0 ||
        domain->Reserved[0] != 0 || domain->Reserved[1] != 0)
        return 0;
    if (domain->Stage != VMX_EPT_DOMAIN_STAGE_READY &&
        domain->Stage != VMX_EPT_DOMAIN_STAGE_ROOT_OWNED)
        return 0;
    if (domain->MutationGeneration == 0 ||
        domain->InvalidatedGeneration > domain->MutationGeneration ||
        (domain->PendingGeneration != 0 &&
         domain->PendingGeneration != domain->MutationGeneration))
        return 0;
    if (domain->Stage == VMX_EPT_DOMAIN_STAGE_READY &&
        domain->PendingGeneration != 0)
        return 0;
    if (!BindingSeparate(domain, sizeof(*domain), &domain->Binding) ||
        VmxEptValidateIdentityPlan(&domain->Capabilities, &domain->Identity,
            &domain->Plan) != VMX_EPT_STATUS_OK ||
        VmxEptValidateStorageBinding(&domain->Capabilities, &domain->Identity,
            &domain->Plan, &domain->Populate,
            &domain->Binding) != VMX_EPT_STATUS_OK)
        return 0;
    if (domain->Binding.PageCount == 0 ||
        domain->Pointer.Pml4PhysicalAddress !=
            domain->Binding.PagesByOrdinal[0].PhysicalAddress ||
        domain->Pointer.PhysicalAddressBits !=
            domain->Identity.PhysicalAddressBits ||
        domain->Pointer.MemoryType != VMX_EPT_MEMORY_TYPE_WB ||
        domain->Pointer.EnableAccessedDirty !=
            domain->Populate.EnableAccessedDirty ||
        domain->Pointer.Reserved != 0)
        return 0;
    return VmxEptBuildPointer(&domain->Capabilities, &domain->Pointer,
        &expected_pointer) == VMX_EPT_STATUS_OK &&
        expected_pointer == domain->EptPointer;
}

VMX_EPT_DOMAIN_STATUS VmxEptDomainValidate(const VMX_EPT_DOMAIN* domain)
{
    return DomainAuthentic(domain) ? VMX_EPT_DOMAIN_OK : VMX_EPT_DOMAIN_INVALID;
}

VMX_EPT_DOMAIN_STATUS VmxEptDomainInitialize(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* identity,
    const VMX_EPT_IDENTITY_PLAN* plan,
    const VMX_EPT_POPULATE_CONFIG* populate,
    const VMX_EPT_STORAGE_BINDING* binding,
    VMX_EPT_DOMAIN* domain)
{
    VMX_EPT_CAPABILITIES capabilities_snapshot;
    VMX_EPT_IDENTITY_CONFIG identity_snapshot;
    VMX_EPT_IDENTITY_PLAN plan_snapshot;
    VMX_EPT_POPULATE_CONFIG populate_snapshot;
    VMX_EPT_STORAGE_BINDING binding_snapshot;
    VMX_EPT_DOMAIN staged;
    VMX_EPT_STATUS status;
    if (capabilities == 0 || identity == 0 || plan == 0 || populate == 0 ||
        binding == 0 || domain == 0 ||
        !InitializeInputsSeparate(capabilities, identity, plan, populate,
            binding, domain))
        return VMX_EPT_DOMAIN_INVALID;
    capabilities_snapshot = *capabilities;
    identity_snapshot = *identity;
    plan_snapshot = *plan;
    populate_snapshot = *populate;
    binding_snapshot = *binding;
    if (!InitializeStorageSeparate(capabilities, identity, plan, populate,
            binding, &binding_snapshot, domain) ||
        !DomainZero(domain))
        return VMX_EPT_DOMAIN_INVALID;
    status = VmxEptValidateStorageBinding(
        &capabilities_snapshot, &identity_snapshot, &plan_snapshot,
        &populate_snapshot, &binding_snapshot);
    if (status != VMX_EPT_STATUS_OK)
        return MapStatus(status);
    memset(&staged, 0, sizeof(staged));
    staged.Capabilities = capabilities_snapshot;
    staged.Identity = identity_snapshot;
    staged.Plan = plan_snapshot;
    staged.Populate = populate_snapshot;
    staged.Binding = binding_snapshot;
    staged.Pointer.Pml4PhysicalAddress =
        binding_snapshot.PagesByOrdinal[0].PhysicalAddress;
    staged.Pointer.PhysicalAddressBits = identity_snapshot.PhysicalAddressBits;
    staged.Pointer.MemoryType = VMX_EPT_MEMORY_TYPE_WB;
    staged.Pointer.EnableAccessedDirty = populate_snapshot.EnableAccessedDirty;
    status = VmxEptBuildPointer(
        &staged.Capabilities, &staged.Pointer, &staged.EptPointer);
    if (status != VMX_EPT_STATUS_OK)
        return MapStatus(status);
    status = VmxEptPopulateIdentityMap(
        &capabilities_snapshot, &identity_snapshot, &plan_snapshot,
        &populate_snapshot, &binding_snapshot);
    if (status != VMX_EPT_STATUS_OK)
        return MapStatus(status);
    staged.Magic = VMX_EPT_DOMAIN_MAGIC;
    staged.Version = VMX_EPT_DOMAIN_VERSION;
    staged.Stage = VMX_EPT_DOMAIN_STAGE_READY;
    staged.MutationGeneration = 1;
    staged.InvalidatedGeneration = 1;
    if (!DomainAuthentic(&staged))
        return VMX_EPT_DOMAIN_INVALID;
    *domain = staged;
    return VMX_EPT_DOMAIN_OK;
}

VMX_EPT_DOMAIN_STATUS VmxEptDomainPrepareRootEnter(
    const VMX_EPT_DOMAIN* domain)
{
    if (!DomainAuthentic(domain))
        return VMX_EPT_DOMAIN_INVALID;
    return domain->Stage == VMX_EPT_DOMAIN_STAGE_READY ?
        VMX_EPT_DOMAIN_OK : VMX_EPT_DOMAIN_BUSY;
}

VMX_EPT_DOMAIN_STATUS VmxEptDomainAdoptRoot(VMX_EPT_DOMAIN* domain)
{
    VMX_EPT_DOMAIN_STATUS status = VmxEptDomainPrepareRootEnter(domain);
    if (status != VMX_EPT_DOMAIN_OK)
        return status;
    domain->Stage = VMX_EPT_DOMAIN_STAGE_ROOT_OWNED;
    return VMX_EPT_DOMAIN_OK;
}

VMX_EPT_DOMAIN_STATUS VmxEptDomainBeginMutation(VMX_EPT_DOMAIN* domain)
{
    if (!DomainAuthentic(domain))
        return VMX_EPT_DOMAIN_INVALID;
    if (domain->Stage != VMX_EPT_DOMAIN_STAGE_ROOT_OWNED)
        return VMX_EPT_DOMAIN_NOT_READY;
    if (domain->PendingGeneration != 0)
        return VMX_EPT_DOMAIN_BUSY;
    if (domain->MutationGeneration == UINT64_MAX)
        return VMX_EPT_DOMAIN_OVERFLOW;
    domain->MutationGeneration++;
    return VMX_EPT_DOMAIN_OK;
}

VMX_EPT_DOMAIN_STATUS VmxEptDomainBeginUse(VMX_EPT_DOMAIN* domain)
{
    return VmxEptDomainBeginMutation(domain);
}

VMX_EPT_DOMAIN_STATUS VmxEptDomainBuildInvalidation(
    VMX_EPT_DOMAIN* domain,
    VMX_EPT_INVALIDATION_REQUEST* request,
    VMX_EPT_INVALIDATION_DESCRIPTOR* descriptor)
{
    VMX_EPT_INVALIDATION_REQUEST staged_request;
    VMX_EPT_INVALIDATION_DESCRIPTOR staged_descriptor;
    VMX_EPT_STATUS status;
    if (!DomainAuthentic(domain) || request == 0 || descriptor == 0)
        return VMX_EPT_DOMAIN_INVALID;
    if (!RangesSeparate(request, sizeof(*request), descriptor,
            sizeof(*descriptor)) ||
        !RangesSeparate(request, sizeof(*request), domain, sizeof(*domain)) ||
        !RangesSeparate(descriptor, sizeof(*descriptor), domain, sizeof(*domain)) ||
        !BindingSeparate(request, sizeof(*request), &domain->Binding) ||
        !BindingSeparate(descriptor, sizeof(*descriptor), &domain->Binding))
        return VMX_EPT_DOMAIN_INVALID;
    if (domain->Stage != VMX_EPT_DOMAIN_STAGE_ROOT_OWNED ||
        domain->MutationGeneration == domain->InvalidatedGeneration)
        return VMX_EPT_DOMAIN_NOT_READY;
    memset(&staged_request, 0, sizeof(staged_request));
    staged_request.Type = VMX_EPT_INVALIDATION_SINGLE_CONTEXT;
    staged_request.Pointer = domain->Pointer;
    status = VmxEptBuildInvalidationDescriptor(
        &domain->Capabilities, &staged_request, &staged_descriptor);
    if (status != VMX_EPT_STATUS_OK)
        return MapStatus(status);
    domain->PendingGeneration = domain->MutationGeneration;
    *request = staged_request;
    *descriptor = staged_descriptor;
    return VMX_EPT_DOMAIN_OK;
}

VMX_EPT_DOMAIN_STATUS VmxEptDomainValidateInvalidationReport(
    const VMX_EPT_DOMAIN* domain,
    const VMX_EPT_EXECUTION_REPORT* report)
{
    if (!DomainAuthentic(domain) || report == 0 ||
        !RangesSeparate(domain, sizeof(*domain), report, sizeof(*report)))
        return VMX_EPT_DOMAIN_INVALID;
    if (domain->Stage != VMX_EPT_DOMAIN_STAGE_ROOT_OWNED ||
        domain->PendingGeneration == 0 ||
        domain->PendingGeneration != domain->MutationGeneration)
        return VMX_EPT_DOMAIN_NOT_READY;
    if (report->Type != VMX_EPT_INVALIDATION_SINGLE_CONTEXT ||
        report->CurrentIrql > VMX_EPT_EXECUTION_MAX_IRQL ||
        report->Attempted != 1 || report->EptPointer != domain->EptPointer ||
        report->Reserved != 0)
        return VMX_EPT_DOMAIN_INVALID;
    if (report->InstructionResult == VMX_EPT_INSTRUCTION_SUCCESS)
        return VMX_EPT_DOMAIN_OK;
    if (report->InstructionResult == VMX_EPT_INSTRUCTION_FAIL_VALID ||
        report->InstructionResult == VMX_EPT_INSTRUCTION_FAIL_INVALID)
        return VMX_EPT_DOMAIN_INSTRUCTION_FAILED;
    return VMX_EPT_DOMAIN_INVALID;
}

VMX_EPT_DOMAIN_STATUS VmxEptDomainCommitInvalidation(
    VMX_EPT_DOMAIN* domain,
    const VMX_EPT_EXECUTION_REPORT* report)
{
    VMX_EPT_DOMAIN_STATUS status =
        VmxEptDomainValidateInvalidationReport(domain, report);
    if (status != VMX_EPT_DOMAIN_OK)
        return status;
    domain->InvalidatedGeneration = domain->PendingGeneration;
    domain->PendingGeneration = 0;
    return VMX_EPT_DOMAIN_OK;
}
VMX_EPT_DOMAIN_STATUS VmxEptDomainPrepareRootRelease(
    const VMX_EPT_DOMAIN* domain)
{
    if (!DomainAuthentic(domain))
        return VMX_EPT_DOMAIN_INVALID;
    if (domain->Stage != VMX_EPT_DOMAIN_STAGE_ROOT_OWNED)
        return VMX_EPT_DOMAIN_NOT_READY;
    if (domain->PendingGeneration != 0 ||
        domain->MutationGeneration != domain->InvalidatedGeneration)
        return VMX_EPT_DOMAIN_DIRTY;
    return VMX_EPT_DOMAIN_OK;
}

VMX_EPT_DOMAIN_STATUS VmxEptDomainCompleteRootRelease(VMX_EPT_DOMAIN* domain)
{
    VMX_EPT_DOMAIN_STATUS status = VmxEptDomainPrepareRootRelease(domain);
    if (status != VMX_EPT_DOMAIN_OK)
        return status;
    domain->Stage = VMX_EPT_DOMAIN_STAGE_READY;
    return VMX_EPT_DOMAIN_OK;
}

VMX_EPT_DOMAIN_STATUS VmxEptDomainDestroy(VMX_EPT_DOMAIN* domain)
{
    if (!DomainAuthentic(domain))
        return VMX_EPT_DOMAIN_INVALID;
    if (domain->Stage != VMX_EPT_DOMAIN_STAGE_READY)
        return VMX_EPT_DOMAIN_BUSY;
    if (domain->MutationGeneration != domain->InvalidatedGeneration)
        return VMX_EPT_DOMAIN_DIRTY;
    memset(domain, 0, sizeof(*domain));
    return VMX_EPT_DOMAIN_OK;
}
