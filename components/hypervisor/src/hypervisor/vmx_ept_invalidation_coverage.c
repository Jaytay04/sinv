#include "vmx_ept_invalidation_coverage.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
static int Zero(const void* object, size_t size)
{
    const unsigned char* bytes = (const unsigned char*)object;
    size_t i;
    for (i = 0; i < size; ++i)
        if (bytes[i] != 0)
            return 0;
    return 1;
}
static int Span(const void* object, size_t size, uintptr_t* begin, uintptr_t* end)
{
    uintptr_t first = (uintptr_t)object;
    if (object == 0 || size == 0 || first > UINTPTR_MAX - size)
        return 0;
    *begin = first;
    *end = first + size;
    return 1;
}
static int Separate(const void* first, size_t first_size,
    const void* second, size_t second_size)
{
    uintptr_t a, b, c, d;
    return Span(first, first_size, &a, &b) &&
        Span(second, second_size, &c, &d) && (b <= c || d <= a);
}
static int SeparateStorage(const void* object, size_t size,
    const VMX_EPT_DOMAIN* domain)
{
    size_t bound_bytes, index_bytes;
    if (domain->Binding.VirtualBytes > SIZE_MAX ||
        domain->Binding.PageCount > SIZE_MAX / sizeof(VMX_EPT_BOUND_PAGE) ||
        domain->Binding.PageCount > SIZE_MAX / sizeof(VMX_EPT_PHYSICAL_PAGE))
        return 0;
    bound_bytes = (size_t)domain->Binding.PageCount * sizeof(VMX_EPT_BOUND_PAGE);
    index_bytes = (size_t)domain->Binding.PageCount *
        sizeof(VMX_EPT_PHYSICAL_PAGE);
    return Separate(object, size, domain->Binding.VirtualBase,
            (size_t)domain->Binding.VirtualBytes) &&
        Separate(object, size, domain->Binding.PagesByOrdinal, bound_bytes) &&
        Separate(object, size, domain->Binding.PhysicalIndex, index_bytes);
}
static int LayoutSeparate(const VMX_EPT_INVALIDATION_COVERAGE* coverage,
    const VMX_EPT_COVERAGE_RECORD* records, uint32_t count,
    const VMX_EPT_ROOT_COORDINATOR* root)
{
    size_t bytes = (size_t)count * sizeof(*records),
        root_bytes = (size_t)count * sizeof(*root->Records);
    VMX_EPT_DOMAIN* domain = root->Domain;
    return Separate(coverage, sizeof(*coverage), records, bytes) &&
        Separate(coverage, sizeof(*coverage), root, sizeof(*root)) &&
        Separate(coverage, sizeof(*coverage), root->Records, root_bytes) &&
        Separate(records, bytes, root, sizeof(*root)) &&
        Separate(records, bytes, root->Records, root_bytes) &&
        Separate(coverage, sizeof(*coverage), domain, sizeof(*domain)) &&
        Separate(records, bytes, domain, sizeof(*domain)) &&
        SeparateStorage(coverage, sizeof(*coverage), domain) &&
        SeparateStorage(records, bytes, domain);
}
static int SeparateCoverageObject(const VMX_EPT_INVALIDATION_COVERAGE* coverage,
    const void* object, size_t size)
{
    size_t bytes = (size_t)coverage->Count * sizeof(*coverage->Records),
        root_bytes = (size_t)coverage->Count * sizeof(*coverage->Root->Records);
    return Separate(object, size, coverage, sizeof(*coverage)) &&
        Separate(object, size, coverage->Records, bytes) &&
        Separate(object, size, coverage->Root, sizeof(*coverage->Root)) &&
        Separate(object, size, coverage->Root->Records, root_bytes);
}static VMX_EPT_COVERAGE_STATUS MapDomain(VMX_EPT_DOMAIN_STATUS status)
{
    switch (status) {
    case VMX_EPT_DOMAIN_OK: return VMX_EPT_COVERAGE_OK;
    case VMX_EPT_DOMAIN_UNSUPPORTED: return VMX_EPT_COVERAGE_UNSUPPORTED;
    case VMX_EPT_DOMAIN_NOT_READY: return VMX_EPT_COVERAGE_NOT_READY;
    case VMX_EPT_DOMAIN_BUSY: return VMX_EPT_COVERAGE_BUSY;
    case VMX_EPT_DOMAIN_DIRTY: return VMX_EPT_COVERAGE_DIRTY;
    case VMX_EPT_DOMAIN_INSTRUCTION_FAILED:
        return VMX_EPT_COVERAGE_INSTRUCTION_FAILED;
    case VMX_EPT_DOMAIN_OVERFLOW: return VMX_EPT_COVERAGE_OVERFLOW;
    default: return VMX_EPT_COVERAGE_INVALID;
    }
}
static VMX_EPT_COVERAGE_STATUS MapRoot(VMX_EPT_ROOT_STATUS status) {
    if (status == VMX_EPT_ROOT_OK) return VMX_EPT_COVERAGE_OK;
    if (status == VMX_EPT_ROOT_NOT_READY) return VMX_EPT_COVERAGE_NOT_READY;
    if (status == VMX_EPT_ROOT_BUSY) return VMX_EPT_COVERAGE_BUSY;
    if (status == VMX_EPT_ROOT_DIRTY) return VMX_EPT_COVERAGE_DIRTY;
    return VMX_EPT_COVERAGE_INVALID; }
static int CoverageAuthentic(const VMX_EPT_INVALIDATION_COVERAGE* coverage)
{
    VMX_EPT_DOMAIN* domain;
    uint32_t used = 0, i;
    if (coverage == 0 || coverage->Magic != VMX_EPT_COVERAGE_MAGIC ||
        coverage->Version != VMX_EPT_COVERAGE_VERSION || coverage->Root == 0 ||
        coverage->Records == 0 || coverage->Count == 0 ||
        coverage->Count > SIZE_MAX / sizeof(*coverage->Records) ||
        coverage->Reserved32 != 0 || coverage->Reserved[0] != 0 ||
        coverage->Reserved[1] != 0 ||
        VmxEptRootCoordinatorValidate(coverage->Root) != VMX_EPT_ROOT_OK ||
        coverage->Count != coverage->Root->Count)
        return 0;
    domain = coverage->Root->Domain;
    if (!LayoutSeparate(coverage, coverage->Records, coverage->Count,
            coverage->Root))
        return 0;
    for (i = 0; i < coverage->Count; ++i) {
        const VMX_EPT_COVERAGE_RECORD* record = &coverage->Records[i];
        if (record->Reserved[0] != 0 || record->Reserved[1] != 0)
            return 0;
        if (record->RequiredGeneration == 0) {
            if (record->AcknowledgedGeneration != 0)
                return 0;
            continue;
        }
        if (coverage->Root->Records[i].RootOwned != 1 ||
            record->RequiredGeneration != domain->MutationGeneration ||
            (record->AcknowledgedGeneration != domain->InvalidatedGeneration &&
             record->AcknowledgedGeneration != domain->MutationGeneration))
            return 0;
        if (domain->MutationGeneration == domain->InvalidatedGeneration &&
            record->AcknowledgedGeneration != record->RequiredGeneration)
            return 0;
        used++;
    }
    if (used > coverage->Root->OwnedCount ||
        (domain->MutationGeneration != domain->InvalidatedGeneration && used == 0))
        return 0;
    return 1;
}
VMX_EPT_COVERAGE_STATUS VmxEptCoverageValidate(const VMX_EPT_INVALIDATION_COVERAGE* coverage)
{
    return CoverageAuthentic(coverage) ?
        VMX_EPT_COVERAGE_OK : VMX_EPT_COVERAGE_INVALID;
}
VMX_EPT_COVERAGE_STATUS VmxEptCoverageInitialize(VMX_EPT_ROOT_COORDINATOR* root,
    VMX_EPT_COVERAGE_RECORD* records, VMX_EPT_INVALIDATION_COVERAGE* coverage)
{
    VMX_EPT_INVALIDATION_COVERAGE staged;
    size_t bytes;
    if (root == 0 || records == 0 || coverage == 0 ||
        VmxEptRootCoordinatorValidate(root) != VMX_EPT_ROOT_OK ||
        root->OwnedCount != 0 ||
        root->PendingTransition != VMX_EPT_ROOT_TRANSITION_NONE ||
        root->Count > SIZE_MAX / sizeof(*records))
        return VMX_EPT_COVERAGE_INVALID;
    bytes = (size_t)root->Count * sizeof(*records);
    if (!Zero(records, bytes) || !Zero(coverage, sizeof(*coverage)) ||
        !LayoutSeparate(coverage, records, root->Count, root))
        return VMX_EPT_COVERAGE_INVALID;
    memset(&staged, 0, sizeof(staged));
    staged.Root = root;
    staged.Records = records;
    staged.Count = root->Count;
    staged.Magic = VMX_EPT_COVERAGE_MAGIC;
    staged.Version = VMX_EPT_COVERAGE_VERSION;
    if (!CoverageAuthentic(&staged))
        return VMX_EPT_COVERAGE_INVALID;
    *coverage = staged;
    return VMX_EPT_COVERAGE_OK;
}
VMX_EPT_COVERAGE_STATUS VmxEptCoverageMarkUse(VMX_EPT_INVALIDATION_COVERAGE* coverage, uint32_t system_index)
{
    VMX_EPT_DOMAIN* domain;
    VMX_EPT_DOMAIN_STATUS status;
    if (!CoverageAuthentic(coverage) || system_index >= coverage->Count)
        return VMX_EPT_COVERAGE_INVALID;
    domain = coverage->Root->Domain;
    if (coverage->Root->PendingTransition != VMX_EPT_ROOT_TRANSITION_NONE ||
        domain->PendingGeneration != 0)
        return VMX_EPT_COVERAGE_BUSY;
    if (coverage->Root->Records[system_index].RootOwned != 1)
        return VMX_EPT_COVERAGE_NOT_READY;
    if (coverage->Records[system_index].RequiredGeneration != 0)
        return VMX_EPT_COVERAGE_BUSY;
    if (domain->MutationGeneration == domain->InvalidatedGeneration) {
        status = VmxEptDomainBeginUse(domain);
        if (status != VMX_EPT_DOMAIN_OK)
            return MapDomain(status);
    }
    coverage->Records[system_index].RequiredGeneration =
        domain->MutationGeneration;
    coverage->Records[system_index].AcknowledgedGeneration =
        domain->InvalidatedGeneration;
    return VMX_EPT_COVERAGE_OK;
}
VMX_EPT_COVERAGE_STATUS VmxEptCoverageBeginMutation(VMX_EPT_INVALIDATION_COVERAGE* coverage)
{
    VMX_EPT_DOMAIN* domain;
    VMX_EPT_DOMAIN_STATUS status;
    uint32_t i, used = 0;
    if (!CoverageAuthentic(coverage))
        return VMX_EPT_COVERAGE_INVALID;
    domain = coverage->Root->Domain;
    if (coverage->Root->PendingTransition != VMX_EPT_ROOT_TRANSITION_NONE ||
        domain->PendingGeneration != 0)
        return VMX_EPT_COVERAGE_BUSY;
    if (domain->MutationGeneration != domain->InvalidatedGeneration)
        return VMX_EPT_COVERAGE_DIRTY;
    for (i = 0; i < coverage->Count; ++i)
        used += coverage->Records[i].RequiredGeneration != 0;
    if (used == 0)
        return VMX_EPT_COVERAGE_NOT_READY;
    status = VmxEptDomainBeginMutation(domain);
    if (status != VMX_EPT_DOMAIN_OK)
        return MapDomain(status);
    for (i = 0; i < coverage->Count; ++i)
        if (coverage->Records[i].RequiredGeneration != 0)
            coverage->Records[i].RequiredGeneration = domain->MutationGeneration;
    return VMX_EPT_COVERAGE_OK;
}
VMX_EPT_COVERAGE_STATUS VmxEptCoverageBuildInvalidation(
    VMX_EPT_INVALIDATION_COVERAGE* coverage, VMX_EPT_INVALIDATION_REQUEST* request,
    VMX_EPT_INVALIDATION_DESCRIPTOR* descriptor)
{
    if (!CoverageAuthentic(coverage) || request == 0 || descriptor == 0 ||
        !SeparateCoverageObject(coverage, request, sizeof(*request)) ||
        !SeparateCoverageObject(coverage, descriptor, sizeof(*descriptor)))
        return VMX_EPT_COVERAGE_INVALID;
    if (coverage->Root->PendingTransition != VMX_EPT_ROOT_TRANSITION_NONE)
        return VMX_EPT_COVERAGE_BUSY;
    return MapDomain(VmxEptDomainBuildInvalidation(
        coverage->Root->Domain, request, descriptor));
}
VMX_EPT_COVERAGE_STATUS VmxEptCoverageAcceptReport(
    VMX_EPT_INVALIDATION_COVERAGE* coverage, uint32_t system_index,
    const VMX_EPT_EXECUTION_REPORT* report)
{
    VMX_EPT_DOMAIN* domain;
    VMX_EPT_DOMAIN_STATUS status;
    uint64_t generation;
    uint32_t i;
    int final = 1;
    if (!CoverageAuthentic(coverage) || system_index >= coverage->Count ||
        report == 0 || !SeparateCoverageObject(coverage, report, sizeof(*report)))
        return VMX_EPT_COVERAGE_INVALID;
    domain = coverage->Root->Domain;
    if (coverage->Root->PendingTransition != VMX_EPT_ROOT_TRANSITION_NONE)
        return VMX_EPT_COVERAGE_BUSY;
    generation = domain->PendingGeneration;
    if (generation == 0 ||
        coverage->Records[system_index].RequiredGeneration != generation)
        return VMX_EPT_COVERAGE_NOT_READY;
    if (coverage->Records[system_index].AcknowledgedGeneration == generation)
        return VMX_EPT_COVERAGE_BUSY;
    status = VmxEptDomainValidateInvalidationReport(domain, report);
    if (status != VMX_EPT_DOMAIN_OK)
        return MapDomain(status);
    for (i = 0; i < coverage->Count; ++i)
        if (i != system_index && coverage->Records[i].RequiredGeneration != 0 &&
            coverage->Records[i].AcknowledgedGeneration != generation)
            final = 0;
    if (final) {
        status = VmxEptDomainCommitInvalidation(domain, report);
        if (status != VMX_EPT_DOMAIN_OK)
            return MapDomain(status);
    }
    coverage->Records[system_index].AcknowledgedGeneration = generation;
    return VMX_EPT_COVERAGE_OK;
}
VMX_EPT_COVERAGE_STATUS VmxEptCoveragePrepareRelease(VMX_EPT_INVALIDATION_COVERAGE* coverage, uint32_t system_index)
{
    VMX_EPT_DOMAIN* domain;
    if (!CoverageAuthentic(coverage) || system_index >= coverage->Count)
        return VMX_EPT_COVERAGE_INVALID;
    domain = coverage->Root->Domain;
    if (domain->PendingGeneration != 0 ||
        domain->MutationGeneration != domain->InvalidatedGeneration)
        return VMX_EPT_COVERAGE_DIRTY;
    return MapRoot(VmxEptRootCoordinatorPrepareRelease(
        coverage->Root, system_index));
}
VMX_EPT_COVERAGE_STATUS VmxEptCoverageCompleteRelease(VMX_EPT_INVALIDATION_COVERAGE* coverage, uint32_t system_index)
{
    VMX_EPT_ROOT_STATUS status;
    if (!CoverageAuthentic(coverage) || system_index >= coverage->Count ||
        coverage->Root->PendingTransition != VMX_EPT_ROOT_TRANSITION_RELEASE ||
        coverage->Root->PendingIndex != system_index)
        return VMX_EPT_COVERAGE_INVALID;
    status = VmxEptRootCoordinatorCompleteRelease(coverage->Root, system_index);
    if (status != VMX_EPT_ROOT_OK)
        return MapRoot(status);
    memset(&coverage->Records[system_index], 0,
        sizeof(coverage->Records[system_index]));
    return VMX_EPT_COVERAGE_OK;
}
VMX_EPT_COVERAGE_STATUS VmxEptCoverageCancelRelease(VMX_EPT_INVALIDATION_COVERAGE* coverage, uint32_t system_index)
{
    if (!CoverageAuthentic(coverage) ||
        coverage->Root->PendingTransition != VMX_EPT_ROOT_TRANSITION_RELEASE ||
        coverage->Root->PendingIndex != system_index)
        return VMX_EPT_COVERAGE_INVALID;
    return MapRoot(VmxEptRootCoordinatorCancel(coverage->Root, system_index));
}
VMX_EPT_COVERAGE_STATUS VmxEptCoverageDestroy(VMX_EPT_INVALIDATION_COVERAGE* coverage)
{
    size_t bytes;
    if (coverage == 0)
        return VMX_EPT_COVERAGE_INVALID;
    if (Zero(coverage, sizeof(*coverage)))
        return VMX_EPT_COVERAGE_OK;
    if (!CoverageAuthentic(coverage))
        return VMX_EPT_COVERAGE_INVALID;
    if (coverage->Root->OwnedCount != 0 ||
        coverage->Root->PendingTransition != VMX_EPT_ROOT_TRANSITION_NONE)
        return VMX_EPT_COVERAGE_BUSY;
    bytes = (size_t)coverage->Count * sizeof(*coverage->Records);
    memset(coverage->Records, 0, bytes);
    memset(coverage, 0, sizeof(*coverage));
    return VMX_EPT_COVERAGE_OK;
}
