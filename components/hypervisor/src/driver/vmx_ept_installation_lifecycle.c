#include "vmx_ept_installation_lifecycle.h"

#include "vmx_ept_native_adapter.h"
#include "vmx_ept_session_operations.h"

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

static VMX_EPT_INSTALLATION_LIFECYCLE_STATUS MapSession(
    VMX_EPT_SESSION_OPERATION_STATUS status)
{
    switch (status) {
    case VMX_EPT_SESSION_OPERATION_OK:
        return VMX_EPT_INSTALLATION_LIFECYCLE_OK;
    case VMX_EPT_SESSION_OPERATION_IDENTITY_MISMATCH:
        return VMX_EPT_INSTALLATION_LIFECYCLE_STATE_MISMATCH;
    case VMX_EPT_SESSION_OPERATION_UNSUPPORTED:
        return VMX_EPT_INSTALLATION_LIFECYCLE_UNSUPPORTED;
    case VMX_EPT_SESSION_OPERATION_NOT_READY:
        return VMX_EPT_INSTALLATION_LIFECYCLE_NOT_READY;
    case VMX_EPT_SESSION_OPERATION_BUSY:
        return VMX_EPT_INSTALLATION_LIFECYCLE_BUSY;
    case VMX_EPT_SESSION_OPERATION_DIRTY:
        return VMX_EPT_INSTALLATION_LIFECYCLE_DIRTY;
    case VMX_EPT_SESSION_OPERATION_INSTRUCTION_FAILED:
        return VMX_EPT_INSTALLATION_LIFECYCLE_INSTRUCTION_FAILED;
    case VMX_EPT_SESSION_OPERATION_OVERFLOW:
        return VMX_EPT_INSTALLATION_LIFECYCLE_OVERFLOW;
    default:
        return VMX_EPT_INSTALLATION_LIFECYCLE_INVALID;
    }
}

static VMX_EPT_INSTALLATION_LIFECYCLE_STATUS MapNative(
    VMX_EPT_NATIVE_STATUS status)
{
    switch (status) {
    case VMX_EPT_NATIVE_OK:
        return VMX_EPT_INSTALLATION_LIFECYCLE_OK;
    case VMX_EPT_NATIVE_WRONG_IRQL:
        return VMX_EPT_INSTALLATION_LIFECYCLE_WRONG_IRQL;
    case VMX_EPT_NATIVE_WRONG_PROCESSOR:
        return VMX_EPT_INSTALLATION_LIFECYCLE_WRONG_PROCESSOR;
    case VMX_EPT_NATIVE_STATE_MISMATCH:
        return VMX_EPT_INSTALLATION_LIFECYCLE_STATE_MISMATCH;
    default:
        return VMX_EPT_INSTALLATION_LIFECYCLE_INVALID;
    }
}

static VMX_EPT_INSTALLATION_LIFECYCLE_STATUS Resolve(
    VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    VMX_EPT_INSTALLATION_BINDING_RECORD** record,
    VMX_PROCESSOR_IDENTITY* resolved_identity)
{
    VMX_PROCESSOR_IDENTITY snapshot;
    SIZE_T graph_bytes;
    VMX_EPT_INSTALLATION_BINDING_RECORD* selected;
    VMX_EPT_NATIVE_STATUS native_status;

    if (KeGetCurrentIrql() != DISPATCH_LEVEL)
        return VMX_EPT_INSTALLATION_LIFECYCLE_WRONG_IRQL;
    if (owner == NULL || identity == NULL || record == NULL ||
        resolved_identity == NULL ||
        VmxEptInstallationOwnerValidate(owner) !=
            VMX_EPT_INSTALLATION_OWNER_OK ||
        owner->GraphBytes > SIZE_MAX ||
        !Separate(owner, sizeof(*owner), identity, sizeof(*identity)))
        return VMX_EPT_INSTALLATION_LIFECYCLE_INVALID;
    graph_bytes = (SIZE_T)owner->GraphBytes;
    if (!Separate(owner->Graph, graph_bytes, identity, sizeof(*identity)))
        return VMX_EPT_INSTALLATION_LIFECYCLE_INVALID;
    snapshot = *identity;
    if (snapshot.Reserved != 0 || snapshot.SystemIndex >= owner->Count)
        return VMX_EPT_INSTALLATION_LIFECYCLE_INVALID;
    selected = &owner->Records[snapshot.SystemIndex];
    if (!VmxProcessorIdentityEqual(&snapshot, &selected->Identity))
        return VMX_EPT_INSTALLATION_LIFECYCLE_STATE_MISMATCH;
    native_status = VmxEptNativePlatformValidate(
        &selected->NativeContext, &snapshot, &selected->NativePlatform);
    if (native_status != VMX_EPT_NATIVE_OK)
        return MapNative(native_status);
    *record = selected;
    *resolved_identity = snapshot;
    return VMX_EPT_INSTALLATION_LIFECYCLE_OK;
}

static int PendingExact(const VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity, uint32_t transition)
{
    return owner->Session->Root->PendingTransition == transition &&
        owner->Session->Root->PendingIndex == identity->SystemIndex;
}

VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationPrepareEnter(
    VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity)
{
    VMX_EPT_INSTALLATION_BINDING_RECORD* record;
    VMX_PROCESSOR_IDENTITY snapshot;
    VMX_EPT_INSTALLATION_LIFECYCLE_STATUS status =
        Resolve(owner, identity, &record, &snapshot);
    if (status != VMX_EPT_INSTALLATION_LIFECYCLE_OK)
        return status;
    if (record->NativeContext.RootOwned != 0 ||
        owner->Session->RootRecords[snapshot.SystemIndex].RootOwned != 0)
        return VMX_EPT_INSTALLATION_LIFECYCLE_STATE_MISMATCH;
    return MapSession(VmxEptSessionPrepareEnter(owner->Session, &snapshot));
}

VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationCompleteEnter(
    VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    uint32_t native_succeeded)
{
    VMX_EPT_INSTALLATION_BINDING_RECORD* record;
    VMX_PROCESSOR_IDENTITY snapshot;
    VMX_EPT_INSTALLATION_LIFECYCLE_STATUS status;
    if (native_succeeded > 1)
        return VMX_EPT_INSTALLATION_LIFECYCLE_INVALID;
    status = Resolve(owner, identity, &record, &snapshot);
    if (status != VMX_EPT_INSTALLATION_LIFECYCLE_OK)
        return status;
    if (!PendingExact(owner, &snapshot, VMX_EPT_ROOT_TRANSITION_ENTER))
        return VMX_EPT_INSTALLATION_LIFECYCLE_STATE_MISMATCH;
    if (!native_succeeded) {
        if (record->NativeContext.RootOwned != 0)
            return VMX_EPT_INSTALLATION_LIFECYCLE_STATE_MISMATCH;
        return MapSession(VmxEptSessionCancelEnter(owner->Session, &snapshot));
    }
    status = MapNative(VmxEptNativeRootTransition(
        &record->NativeContext, 0, 1));
    if (status != VMX_EPT_INSTALLATION_LIFECYCLE_OK)
        return status;
    status = MapSession(VmxEptSessionCompleteEnter(owner->Session, &snapshot));
    return status == VMX_EPT_INSTALLATION_LIFECYCLE_OK ? status :
        VMX_EPT_INSTALLATION_LIFECYCLE_INVARIANT;
}

VMX_EPT_INSTALLATION_LIFECYCLE_STATUS
VmxEptInstallationRecoverEnterAfterVmxoff(
    VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity)
{
    VMX_EPT_INSTALLATION_BINDING_RECORD* record;
    VMX_PROCESSOR_IDENTITY snapshot;
    VMX_EPT_INSTALLATION_LIFECYCLE_STATUS status =
        Resolve(owner, identity, &record, &snapshot);
    if (status != VMX_EPT_INSTALLATION_LIFECYCLE_OK)
        return status;
    if (!PendingExact(owner, &snapshot, VMX_EPT_ROOT_TRANSITION_ENTER))
        return VMX_EPT_INSTALLATION_LIFECYCLE_STATE_MISMATCH;
    if (record->NativeContext.RootOwned == 1) {
        status = MapNative(VmxEptNativeRootTransition(
            &record->NativeContext, 1, 0));
        if (status != VMX_EPT_INSTALLATION_LIFECYCLE_OK)
            return status;
    } else if (record->NativeContext.RootOwned != 0) {
        return VMX_EPT_INSTALLATION_LIFECYCLE_STATE_MISMATCH;
    }
    return MapSession(VmxEptSessionCancelEnter(owner->Session, &snapshot));
}

static int AdoptionOutputSeparate(
    const VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    const uint32_t* out_published)
{
    SIZE_T graph_bytes;
    if (owner == NULL || identity == NULL || out_published == NULL ||
        owner->GraphBytes > SIZE_MAX)
        return 0;
    graph_bytes = (SIZE_T)owner->GraphBytes;
    return Separate(owner, sizeof(*owner), out_published,
            sizeof(*out_published)) &&
        Separate(owner->Graph, graph_bytes, out_published,
            sizeof(*out_published)) &&
        Separate(identity, sizeof(*identity), out_published,
            sizeof(*out_published));
}

VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationAdoptExisting(
    VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    uint32_t* out_published)
{
    VMX_EPT_INSTALLATION_BINDING_RECORD* record;
    VMX_PROCESSOR_IDENTITY snapshot;
    VMX_EPT_INSTALLATION_LIFECYCLE_STATUS status;

    if (out_published == NULL)
        return VMX_EPT_INSTALLATION_LIFECYCLE_INVALID;
    status = Resolve(owner, identity, &record, &snapshot);
    if (status != VMX_EPT_INSTALLATION_LIFECYCLE_OK)
        return status;
    if (!AdoptionOutputSeparate(owner, identity, out_published) ||
        *out_published != 0)
        return VMX_EPT_INSTALLATION_LIFECYCLE_INVALID;
    if (record->NativeContext.RootOwned != 0 ||
        owner->Session->RootRecords[snapshot.SystemIndex].RootOwned != 0)
        return VMX_EPT_INSTALLATION_LIFECYCLE_STATE_MISMATCH;
    status = MapSession(VmxEptSessionPrepareEnter(
        owner->Session, &snapshot));
    if (status != VMX_EPT_INSTALLATION_LIFECYCLE_OK)
        return status;
    *out_published = 1;
    status = MapNative(VmxEptNativeRootTransition(
        &record->NativeContext, 0, 1));
    if (status != VMX_EPT_INSTALLATION_LIFECYCLE_OK) {
        if (MapSession(VmxEptSessionCancelEnter(
                owner->Session, &snapshot)) !=
            VMX_EPT_INSTALLATION_LIFECYCLE_OK)
            return VMX_EPT_INSTALLATION_LIFECYCLE_INVARIANT;
        *out_published = 0;
        return status;
    }
    status = MapSession(VmxEptSessionCompleteEnter(
        owner->Session, &snapshot));
    return status == VMX_EPT_INSTALLATION_LIFECYCLE_OK ? status :
        VMX_EPT_INSTALLATION_LIFECYCLE_INVARIANT;
}

VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationRollbackAdoption(
    VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity)
{
    VMX_EPT_INSTALLATION_BINDING_RECORD* record;
    VMX_PROCESSOR_IDENTITY snapshot;
    VMX_EPT_INSTALLATION_LIFECYCLE_STATUS status =
        Resolve(owner, identity, &record, &snapshot);
    if (status != VMX_EPT_INSTALLATION_LIFECYCLE_OK)
        return status;
    if (!PendingExact(owner, &snapshot, VMX_EPT_ROOT_TRANSITION_ENTER) ||
        owner->Session->RootRecords[snapshot.SystemIndex].RootOwned != 0)
        return VMX_EPT_INSTALLATION_LIFECYCLE_STATE_MISMATCH;
    if (record->NativeContext.RootOwned == 1) {
        status = MapNative(VmxEptNativeRootTransition(
            &record->NativeContext, 1, 0));
        if (status != VMX_EPT_INSTALLATION_LIFECYCLE_OK)
            return status;
    } else if (record->NativeContext.RootOwned != 0) {
        return VMX_EPT_INSTALLATION_LIFECYCLE_STATE_MISMATCH;
    }
    status = MapSession(VmxEptSessionCancelEnter(
        owner->Session, &snapshot));
    return status == VMX_EPT_INSTALLATION_LIFECYCLE_OK ? status :
        VMX_EPT_INSTALLATION_LIFECYCLE_INVARIANT;
}

VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationPrepareRelease(
    VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity)
{
    VMX_EPT_INSTALLATION_BINDING_RECORD* record;
    VMX_PROCESSOR_IDENTITY snapshot;
    VMX_EPT_INSTALLATION_LIFECYCLE_STATUS status =
        Resolve(owner, identity, &record, &snapshot);
    if (status != VMX_EPT_INSTALLATION_LIFECYCLE_OK)
        return status;
    if (record->NativeContext.RootOwned != 1 ||
        owner->Session->RootRecords[snapshot.SystemIndex].RootOwned != 1)
        return VMX_EPT_INSTALLATION_LIFECYCLE_STATE_MISMATCH;
    return MapSession(VmxEptSessionPrepareRelease(owner->Session, &snapshot));
}

VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationCompleteRelease(
    VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    uint32_t native_succeeded)
{
    VMX_EPT_INSTALLATION_BINDING_RECORD* record;
    VMX_PROCESSOR_IDENTITY snapshot;
    VMX_EPT_INSTALLATION_LIFECYCLE_STATUS status;
    if (native_succeeded > 1)
        return VMX_EPT_INSTALLATION_LIFECYCLE_INVALID;
    status = Resolve(owner, identity, &record, &snapshot);
    if (status != VMX_EPT_INSTALLATION_LIFECYCLE_OK)
        return status;
    if (!PendingExact(owner, &snapshot, VMX_EPT_ROOT_TRANSITION_RELEASE))
        return VMX_EPT_INSTALLATION_LIFECYCLE_STATE_MISMATCH;
    if (!native_succeeded) {
        if (record->NativeContext.RootOwned != 1)
            return VMX_EPT_INSTALLATION_LIFECYCLE_STATE_MISMATCH;
        return MapSession(VmxEptSessionCancelRelease(owner->Session, &snapshot));
    }
    if (record->NativeContext.RootOwned == 1) {
        status = MapNative(VmxEptNativeRootTransition(
            &record->NativeContext, 1, 0));
        if (status != VMX_EPT_INSTALLATION_LIFECYCLE_OK)
            return status;
    } else if (record->NativeContext.RootOwned != 0) {
        return VMX_EPT_INSTALLATION_LIFECYCLE_STATE_MISMATCH;
    }
    status = MapSession(VmxEptSessionCompleteRelease(owner->Session, &snapshot));
    return status == VMX_EPT_INSTALLATION_LIFECYCLE_OK ? status :
        VMX_EPT_INSTALLATION_LIFECYCLE_INVARIANT;
}
