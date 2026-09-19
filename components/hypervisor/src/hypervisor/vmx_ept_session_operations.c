#include "vmx_ept_session_operations.h"

#include <stddef.h>
#include <stdint.h>

static int Span(const void* object, size_t size,
    uintptr_t* begin, uintptr_t* end)
{
    uintptr_t first = (uintptr_t)object;
    if (object == NULL || size == 0 || first > UINTPTR_MAX - size)
        return 0;
    *begin = first;
    *end = first + size;
    return 1;
}

static int Separate(const void* first, size_t first_size,
    const void* second, size_t second_size)
{
    uintptr_t first_begin, first_end, second_begin, second_end;
    return Span(first, first_size, &first_begin, &first_end) &&
        Span(second, second_size, &second_begin, &second_end) &&
        (first_end <= second_begin || second_end <= first_begin);
}

static int OwnerSeparate(const VMX_EPT_SESSION_OWNER* owner,
    const void* object, size_t size)
{
    const VMX_EPT_STORAGE_OWNER* storage = owner->Storage;
    size_t page_bytes = (size_t)storage->PageCount *
        sizeof(*storage->PagesByOrdinal);
    return Separate(owner, sizeof(*owner), object, size) &&
        Separate(owner->Graph, (size_t)owner->GraphBytes, object, size) &&
        Separate(storage->Tables, (size_t)storage->TableBytes, object, size) &&
        Separate(storage->PagesByOrdinal, page_bytes, object, size) &&
        Separate(storage->PhysicalIndex,
            (size_t)storage->PageCount * sizeof(*storage->PhysicalIndex),
            object, size);
}

static VMX_EPT_SESSION_OPERATION_STATUS Resolve(
    VMX_EPT_SESSION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    uint32_t* system_index)
{
    VMX_PROCESSOR_IDENTITY snapshot;
    const VMX_PROCESSOR_IDENTITY* expected;
    if (owner == NULL || identity == NULL || system_index == NULL ||
        !Separate(owner, sizeof(*owner), identity, sizeof(*identity)) ||
        VmxEptSessionOwnerValidate(owner) != VMX_EPT_SESSION_OK ||
        owner->GraphBytes > SIZE_MAX ||
        !OwnerSeparate(owner, identity, sizeof(*identity)))
        return VMX_EPT_SESSION_OPERATION_INVALID;
    snapshot = *identity;
    if (snapshot.Reserved != 0 ||
        snapshot.Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER ||
        snapshot.SystemIndex >= owner->Count)
        return VMX_EPT_SESSION_OPERATION_INVALID;
    expected = &owner->RootRecords[snapshot.SystemIndex].Identity;
    if (!VmxProcessorIdentityEqual(&snapshot, expected))
        return VMX_EPT_SESSION_OPERATION_IDENTITY_MISMATCH;
    *system_index = snapshot.SystemIndex;
    return VMX_EPT_SESSION_OPERATION_OK;
}

static VMX_EPT_SESSION_OPERATION_STATUS MapRoot(VMX_EPT_ROOT_STATUS status)
{
    switch (status) {
    case VMX_EPT_ROOT_OK:
        return VMX_EPT_SESSION_OPERATION_OK;
    case VMX_EPT_ROOT_NOT_READY:
        return VMX_EPT_SESSION_OPERATION_NOT_READY;
    case VMX_EPT_ROOT_BUSY:
        return VMX_EPT_SESSION_OPERATION_BUSY;
    case VMX_EPT_ROOT_DIRTY:
        return VMX_EPT_SESSION_OPERATION_DIRTY;
    default:
        return VMX_EPT_SESSION_OPERATION_INVALID;
    }
}

static VMX_EPT_SESSION_OPERATION_STATUS MapCoverage(
    VMX_EPT_COVERAGE_STATUS status)
{
    switch (status) {
    case VMX_EPT_COVERAGE_OK:
        return VMX_EPT_SESSION_OPERATION_OK;
    case VMX_EPT_COVERAGE_UNSUPPORTED:
        return VMX_EPT_SESSION_OPERATION_UNSUPPORTED;
    case VMX_EPT_COVERAGE_NOT_READY:
        return VMX_EPT_SESSION_OPERATION_NOT_READY;
    case VMX_EPT_COVERAGE_BUSY:
        return VMX_EPT_SESSION_OPERATION_BUSY;
    case VMX_EPT_COVERAGE_DIRTY:
        return VMX_EPT_SESSION_OPERATION_DIRTY;
    case VMX_EPT_COVERAGE_INSTRUCTION_FAILED:
        return VMX_EPT_SESSION_OPERATION_INSTRUCTION_FAILED;
    case VMX_EPT_COVERAGE_OVERFLOW:
        return VMX_EPT_SESSION_OPERATION_OVERFLOW;
    default:
        return VMX_EPT_SESSION_OPERATION_INVALID;
    }
}

VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionPrepareEnter(
    VMX_EPT_SESSION_OWNER* owner, const VMX_PROCESSOR_IDENTITY* identity)
{
    uint32_t index;
    VMX_EPT_SESSION_OPERATION_STATUS status = Resolve(owner, identity, &index);
    return status == VMX_EPT_SESSION_OPERATION_OK ? MapRoot(
        VmxEptRootCoordinatorPrepareEnter(owner->Root, index)) : status;
}

VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionCompleteEnter(
    VMX_EPT_SESSION_OWNER* owner, const VMX_PROCESSOR_IDENTITY* identity)
{
    uint32_t index;
    VMX_EPT_SESSION_OPERATION_STATUS status = Resolve(owner, identity, &index);
    return status == VMX_EPT_SESSION_OPERATION_OK ? MapRoot(
        VmxEptRootCoordinatorCompleteEnter(owner->Root, index)) : status;
}

VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionCancelEnter(
    VMX_EPT_SESSION_OWNER* owner, const VMX_PROCESSOR_IDENTITY* identity)
{
    uint32_t index;
    VMX_EPT_SESSION_OPERATION_STATUS status = Resolve(owner, identity, &index);
    if (status != VMX_EPT_SESSION_OPERATION_OK)
        return status;
    if (owner->Root->PendingTransition != VMX_EPT_ROOT_TRANSITION_ENTER ||
        owner->Root->PendingIndex != index)
        return VMX_EPT_SESSION_OPERATION_INVALID;
    return MapRoot(VmxEptRootCoordinatorCancel(owner->Root, index));
}

VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionMarkUse(
    VMX_EPT_SESSION_OWNER* owner, const VMX_PROCESSOR_IDENTITY* identity)
{
    uint32_t index;
    VMX_EPT_SESSION_OPERATION_STATUS status = Resolve(owner, identity, &index);
    return status == VMX_EPT_SESSION_OPERATION_OK ? MapCoverage(
        VmxEptCoverageMarkUse(owner->Coverage, index)) : status;
}

VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionPrepareInstallation(
    VMX_EPT_SESSION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    uint64_t* ept_pointer)
{
    uint32_t index;
    VMX_EPT_SESSION_OPERATION_STATUS status;
    if (ept_pointer == NULL)
        return VMX_EPT_SESSION_OPERATION_INVALID;
    status = Resolve(owner, identity, &index);
    if (status != VMX_EPT_SESSION_OPERATION_OK ||
        !OwnerSeparate(owner, ept_pointer, sizeof(*ept_pointer)) ||
        !Separate(identity, sizeof(*identity), ept_pointer,
            sizeof(*ept_pointer)))
        return status == VMX_EPT_SESSION_OPERATION_OK ?
            VMX_EPT_SESSION_OPERATION_INVALID : status;
    status = MapCoverage(VmxEptCoverageMarkUse(owner->Coverage, index));
    if (status == VMX_EPT_SESSION_OPERATION_OK)
        *ept_pointer = owner->Storage->Domain.EptPointer;
    return status;
}

VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionBeginMutation(
    VMX_EPT_SESSION_OWNER* owner)
{
    if (owner == NULL ||
        VmxEptSessionOwnerValidate(owner) != VMX_EPT_SESSION_OK)
        return VMX_EPT_SESSION_OPERATION_INVALID;
    return MapCoverage(VmxEptCoverageBeginMutation(owner->Coverage));
}

VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionBuildInvalidation(
    VMX_EPT_SESSION_OWNER* owner,
    VMX_EPT_INVALIDATION_REQUEST* request,
    VMX_EPT_INVALIDATION_DESCRIPTOR* descriptor)
{
    if (owner == NULL || request == NULL || descriptor == NULL ||
        VmxEptSessionOwnerValidate(owner) != VMX_EPT_SESSION_OK ||
        owner->GraphBytes > SIZE_MAX ||
        !OwnerSeparate(owner, request, sizeof(*request)) ||
        !OwnerSeparate(owner, descriptor, sizeof(*descriptor)) ||
        !Separate(request, sizeof(*request), descriptor, sizeof(*descriptor)))
        return VMX_EPT_SESSION_OPERATION_INVALID;
    return MapCoverage(VmxEptCoverageBuildInvalidation(
        owner->Coverage, request, descriptor));
}

VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionValidateInvalidationExecution(
    VMX_EPT_SESSION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    const VMX_EPT_EXECUTION_PLATFORM* platform,
    const VMX_EPT_EXECUTION_REPORT* report)
{
    uint32_t index;
    VMX_EPT_SESSION_OPERATION_STATUS status = Resolve(owner, identity, &index);
    (void)index;
    if (status != VMX_EPT_SESSION_OPERATION_OK)
        return status;
    if (platform == NULL || report == NULL ||
        !OwnerSeparate(owner, platform, sizeof(*platform)) ||
        !OwnerSeparate(owner, report, sizeof(*report)) ||
        !Separate(identity, sizeof(*identity), platform, sizeof(*platform)) ||
        !Separate(identity, sizeof(*identity), report, sizeof(*report)) ||
        !Separate(platform, sizeof(*platform), report, sizeof(*report)))
        return VMX_EPT_SESSION_OPERATION_INVALID;
    return VMX_EPT_SESSION_OPERATION_OK;
}

VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionBuildInvalidationForIdentity(
    VMX_EPT_SESSION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    VMX_EPT_INVALIDATION_REQUEST* request,
    VMX_EPT_INVALIDATION_DESCRIPTOR* descriptor)
{
    VMX_EPT_COVERAGE_RECORD* record;
    VMX_EPT_DOMAIN* domain;
    uint32_t index;
    VMX_EPT_SESSION_OPERATION_STATUS status = Resolve(owner, identity, &index);
    if (status != VMX_EPT_SESSION_OPERATION_OK)
        return status;
    if (request == NULL || descriptor == NULL ||
        !Separate(identity, sizeof(*identity), request, sizeof(*request)) ||
        !Separate(identity, sizeof(*identity), descriptor, sizeof(*descriptor)))
        return VMX_EPT_SESSION_OPERATION_INVALID;
    record = &owner->CoverageRecords[index];
    domain = &owner->Storage->Domain;
    if (record->RequiredGeneration == 0)
        return VMX_EPT_SESSION_OPERATION_NOT_READY;
    if (domain->PendingGeneration != 0) {
        if (record->RequiredGeneration != domain->PendingGeneration)
            return VMX_EPT_SESSION_OPERATION_INVALID;
        if (record->AcknowledgedGeneration == domain->PendingGeneration)
            return VMX_EPT_SESSION_OPERATION_BUSY;
    }
    return VmxEptSessionBuildInvalidation(owner, request, descriptor);
}

VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionAcceptInvalidation(
    VMX_EPT_SESSION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    const VMX_EPT_EXECUTION_REPORT* report)
{
    uint32_t index;
    VMX_EPT_SESSION_OPERATION_STATUS status = Resolve(owner, identity, &index);
    if (status != VMX_EPT_SESSION_OPERATION_OK)
        return status;
    if (report == NULL || !OwnerSeparate(owner, report, sizeof(*report)) ||
        !Separate(identity, sizeof(*identity), report, sizeof(*report)))
        return VMX_EPT_SESSION_OPERATION_INVALID;
    return MapCoverage(VmxEptCoverageAcceptReport(
        owner->Coverage, index, report));
}

VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionPrepareRelease(
    VMX_EPT_SESSION_OWNER* owner, const VMX_PROCESSOR_IDENTITY* identity)
{
    uint32_t index;
    VMX_EPT_SESSION_OPERATION_STATUS status = Resolve(owner, identity, &index);
    return status == VMX_EPT_SESSION_OPERATION_OK ? MapCoverage(
        VmxEptCoveragePrepareRelease(owner->Coverage, index)) : status;
}

VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionCompleteRelease(
    VMX_EPT_SESSION_OWNER* owner, const VMX_PROCESSOR_IDENTITY* identity)
{
    uint32_t index;
    VMX_EPT_SESSION_OPERATION_STATUS status = Resolve(owner, identity, &index);
    return status == VMX_EPT_SESSION_OPERATION_OK ? MapCoverage(
        VmxEptCoverageCompleteRelease(owner->Coverage, index)) : status;
}

VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionCancelRelease(
    VMX_EPT_SESSION_OWNER* owner, const VMX_PROCESSOR_IDENTITY* identity)
{
    uint32_t index;
    VMX_EPT_SESSION_OPERATION_STATUS status = Resolve(owner, identity, &index);
    return status == VMX_EPT_SESSION_OPERATION_OK ? MapCoverage(
        VmxEptCoverageCancelRelease(owner->Coverage, index)) : status;
}