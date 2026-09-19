#include "vmx_ept_invalidation_execution.h"

static int Span(const void* value, SIZE_T size, UINT_PTR* begin, UINT_PTR* end)
{
    UINT_PTR first = (UINT_PTR)value;
    if (value == NULL || size == 0 || first > (UINT_PTR)-1 - size)
        return 0;
    *begin = first;
    *end = first + size;
    return 1;
}

static int Separate(const void* first, SIZE_T first_size,
    const void* second, SIZE_T second_size)
{
    UINT_PTR first_begin, first_end, second_begin, second_end;
    return Span(first, first_size, &first_begin, &first_end) &&
        Span(second, second_size, &second_begin, &second_end) &&
        (first_end <= second_begin || second_end <= first_begin);
}

static VMX_EPT_KERNEL_INVALIDATION_STATUS Resolve(
    VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    VMX_EPT_EXECUTION_REPORT* report,
    VMX_EPT_INSTALLATION_BINDING_RECORD** record,
    VMX_PROCESSOR_IDENTITY* resolved_identity)
{
    VMX_PROCESSOR_IDENTITY snapshot;
    SIZE_T graph_bytes;
    VMX_EPT_INSTALLATION_BINDING_RECORD* selected;

    if (KeGetCurrentIrql() != DISPATCH_LEVEL)
        return VMX_EPT_KERNEL_INVALIDATION_WRONG_IRQL;
    if (owner == NULL || identity == NULL || report == NULL ||
        record == NULL || resolved_identity == NULL ||
        VmxEptInstallationOwnerValidate(owner) !=
            VMX_EPT_INSTALLATION_OWNER_OK ||
        owner->GraphBytes > SIZE_MAX)
        return VMX_EPT_KERNEL_INVALIDATION_INVALID;
    graph_bytes = (SIZE_T)owner->GraphBytes;
    if (!Separate(owner, sizeof(*owner), identity, sizeof(*identity)) ||
        !Separate(owner, sizeof(*owner), report, sizeof(*report)) ||
        !Separate(owner->Graph, graph_bytes, identity, sizeof(*identity)) ||
        !Separate(owner->Graph, graph_bytes, report, sizeof(*report)) ||
        !Separate(identity, sizeof(*identity), report, sizeof(*report)))
        return VMX_EPT_KERNEL_INVALIDATION_INVALID;
    snapshot = *identity;
    if (snapshot.Reserved != 0 || snapshot.SystemIndex >= owner->Count)
        return VMX_EPT_KERNEL_INVALIDATION_INVALID;
    selected = &owner->Records[snapshot.SystemIndex];
    if (!VmxProcessorIdentityEqual(&snapshot, &selected->Identity))
        return VMX_EPT_KERNEL_INVALIDATION_IDENTITY_MISMATCH;
    if (owner->Session->Root->PendingTransition !=
            VMX_EPT_ROOT_TRANSITION_NONE)
        return VMX_EPT_KERNEL_INVALIDATION_BUSY;
    if (selected->NativeContext.RootOwned != 1 ||
        owner->Session->RootRecords[snapshot.SystemIndex].RootOwned != 1)
        return VMX_EPT_KERNEL_INVALIDATION_NOT_READY;
    *record = selected;
    *resolved_identity = snapshot;
    return VMX_EPT_KERNEL_INVALIDATION_OK;
}

VMX_EPT_KERNEL_INVALIDATION_STATUS
VmxEptInstallationOwnerCoordinateInvalidation(
    VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    VMX_EPT_EXECUTION_REPORT* report)
{
    VMX_EPT_INSTALLATION_BINDING_RECORD* record;
    VMX_PROCESSOR_IDENTITY snapshot;
    VMX_EPT_KERNEL_INVALIDATION_STATUS status =
        Resolve(owner, identity, report, &record, &snapshot);

    if (status != VMX_EPT_KERNEL_INVALIDATION_OK)
        return status;
    return VmxEptKernelCoordinateInvalidation(
        owner->Session,
        &snapshot,
        &record->NativeContext,
        &record->NativePlatform,
        report);
}
