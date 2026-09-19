#include "vmx_ept_composition_owner.h"

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
    return first != NULL && second != NULL &&
        first_size != 0 && second_size != 0 &&
        a <= (UINT_PTR)-1 - first_size &&
        b <= (UINT_PTR)-1 - second_size &&
        (a + first_size <= b || b + second_size <= a);
}

static int OwnedGraphsSeparate(const VMX_EPT_COMPOSITION_OWNER* owner,
    const void* object, SIZE_T size)
{
    const VMX_EPT_STORAGE_OWNER* storage = owner->Session.Storage;
    SIZE_T page_bytes, physical_bytes;
    if (owner->Session.GraphBytes > SIZE_MAX ||
        storage->TableBytes > SIZE_MAX ||
        storage->PageCount > SIZE_MAX / sizeof(*storage->PagesByOrdinal) ||
        storage->PageCount > SIZE_MAX / sizeof(*storage->PhysicalIndex))
        return 0;
    page_bytes = (SIZE_T)storage->PageCount *
        sizeof(*storage->PagesByOrdinal);
    physical_bytes = (SIZE_T)storage->PageCount *
        sizeof(*storage->PhysicalIndex);
    if (!Separate(owner->Session.Graph, (SIZE_T)owner->Session.GraphBytes,
            object, size) ||
        !Separate(storage->Tables, (SIZE_T)storage->TableBytes, object, size) ||
        !Separate(storage->PagesByOrdinal, page_bytes, object, size) ||
        !Separate(storage->PhysicalIndex, physical_bytes, object, size))
        return 0;
    return owner->Stage == VMX_EPT_COMPOSITION_OWNER_SESSION_ONLY ||
        (owner->Installation.GraphBytes <= SIZE_MAX &&
        Separate(owner->Installation.Graph,
            (SIZE_T)owner->Installation.GraphBytes, object, size));
}

static void Publish(VMX_EPT_COMPOSITION_OWNER* owner,
    uint32_t stage, uint32_t count)
{
    owner->Count = count;
    owner->Version = VMX_EPT_COMPOSITION_OWNER_VERSION;
    owner->Stage = stage;
    KeMemoryBarrier();
    owner->Magic = VMX_EPT_COMPOSITION_OWNER_MAGIC;
}

static int OwnerValid(const VMX_EPT_COMPOSITION_OWNER* owner)
{
    if (owner == NULL ||
        owner->Magic != VMX_EPT_COMPOSITION_OWNER_MAGIC ||
        owner->Version != VMX_EPT_COMPOSITION_OWNER_VERSION ||
        owner->Count == 0 ||
        owner->Count != owner->Session.Count ||
        owner->Reserved[0] != 0 || owner->Reserved[1] != 0 ||
        VmxEptSessionOwnerValidate(&owner->Session) != VMX_EPT_SESSION_OK)
        return 0;
    if (owner->Stage == VMX_EPT_COMPOSITION_OWNER_READY)
        return VmxEptInstallationOwnerValidate(&owner->Installation) ==
                VMX_EPT_INSTALLATION_OWNER_OK &&
            owner->Installation.Session == &owner->Session &&
            owner->Installation.SessionContract == &owner->Session &&
            owner->Installation.Count == owner->Count;
    if (owner->Stage == VMX_EPT_COMPOSITION_OWNER_SESSION_ONLY)
        return Zero(&owner->Installation, sizeof(owner->Installation));
    return 0;
}

static VMX_EPT_COMPOSITION_OWNER_STATUS MapInstallationDestroy(
    VMX_EPT_INSTALLATION_OWNER_STATUS status)
{
    if (status == VMX_EPT_INSTALLATION_OWNER_BUSY)
        return VMX_EPT_COMPOSITION_OWNER_BUSY;
    if (status == VMX_EPT_INSTALLATION_OWNER_DIRTY)
        return VMX_EPT_COMPOSITION_OWNER_DIRTY;
    return VMX_EPT_COMPOSITION_OWNER_INSTALLATION_FAILED;
}

static VMX_EPT_COMPOSITION_OWNER_STATUS MapSessionDestroy(
    VMX_EPT_KERNEL_CONFIGURED_SESSION_STATUS status)
{
    if (status == VMX_EPT_KERNEL_CONFIGURED_SESSION_BUSY)
        return VMX_EPT_COMPOSITION_OWNER_BUSY;
    if (status == VMX_EPT_KERNEL_CONFIGURED_SESSION_DIRTY)
        return VMX_EPT_COMPOSITION_OWNER_DIRTY;
    return VMX_EPT_COMPOSITION_OWNER_SESSION_FAILED;
}

VMX_EPT_COMPOSITION_OWNER_STATUS VmxEptCompositionOwnerValidate(
    const VMX_EPT_COMPOSITION_OWNER* owner)
{
    return OwnerValid(owner) ? VMX_EPT_COMPOSITION_OWNER_OK :
        VMX_EPT_COMPOSITION_OWNER_INVALID;
}

VMX_EPT_COMPOSITION_OWNER_STATUS VmxEptCompositionOwnerInitialize(
    const VMX_EPT_VMCS_BINDING* bindings,
    uint32_t count,
    VMX_EPT_COMPOSITION_OWNER* owner,
    VMX_EPT_COMPOSITION_OWNER_REPORT* out_report)
{
    VMX_EPT_COMPOSITION_OWNER_REPORT report = { 0 };
    VMX_EPT_KERNEL_CONFIGURED_SESSION_STATUS session_status;
    VMX_EPT_INSTALLATION_OWNER_STATUS installation_status;
    VMX_EPT_COMPOSITION_OWNER_STATUS result;
    SIZE_T binding_bytes;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_COMPOSITION_OWNER_WRONG_IRQL;
    if (bindings == NULL || owner == NULL || out_report == NULL ||
        count == 0 || count > VMX_EPT_NATIVE_MAX_SYSTEM_PROCESSORS ||
        count > SIZE_MAX / sizeof(*bindings) ||
        !Zero(owner, sizeof(*owner)) ||
        !Zero(out_report, sizeof(*out_report)))
        return VMX_EPT_COMPOSITION_OWNER_INVALID;
    binding_bytes = (SIZE_T)count * sizeof(*bindings);
    if (!Separate(owner, sizeof(*owner), bindings, binding_bytes) ||
        !Separate(owner, sizeof(*owner), out_report, sizeof(*out_report)) ||
        !Separate(bindings, binding_bytes, out_report, sizeof(*out_report)))
        return VMX_EPT_COMPOSITION_OWNER_INVALID;

    session_status = VmxEptKernelConfiguredSessionInitialize(&owner->Session);
    report.SessionCalls = 1;
    report.SessionStatus = session_status;
    if (session_status != VMX_EPT_KERNEL_CONFIGURED_SESSION_OK) {
        result = VMX_EPT_COMPOSITION_OWNER_SESSION_FAILED;
        goto publish_report;
    }

    installation_status = VmxEptInstallationOwnerInitialize(
        &owner->Session, bindings, count, &owner->Installation);
    report.InstallationCalls = 1;
    report.InstallationStatus = installation_status;
    if (installation_status != VMX_EPT_INSTALLATION_OWNER_OK) {
        session_status = VmxEptKernelConfiguredSessionDestroy(&owner->Session);
        report.SessionCalls = 2;
        report.SessionStatus = session_status;
        if (session_status == VMX_EPT_KERNEL_CONFIGURED_SESSION_OK) {
            RtlSecureZeroMemory(owner, sizeof(*owner));
            result = VMX_EPT_COMPOSITION_OWNER_INSTALLATION_FAILED;
        } else {
            Publish(owner, VMX_EPT_COMPOSITION_OWNER_SESSION_ONLY,
                owner->Session.Count);
            report.StageAfter = owner->Stage;
            result = VMX_EPT_COMPOSITION_OWNER_INVARIANT;
        }
        goto publish_report;
    }

    Publish(owner, VMX_EPT_COMPOSITION_OWNER_READY, count);
    report.StageAfter = owner->Stage;
    result = OwnerValid(owner) ? VMX_EPT_COMPOSITION_OWNER_OK :
        VMX_EPT_COMPOSITION_OWNER_INVARIANT;

publish_report:
    *out_report = report;
    return result;
}

VMX_EPT_COMPOSITION_OWNER_STATUS VmxEptCompositionOwnerDestroy(
    VMX_EPT_COMPOSITION_OWNER* owner,
    VMX_EPT_COMPOSITION_OWNER_REPORT* out_report)
{
    VMX_EPT_COMPOSITION_OWNER_REPORT report = { 0 };
    VMX_EPT_INSTALLATION_OWNER_STATUS installation_status;
    VMX_EPT_KERNEL_CONFIGURED_SESSION_STATUS session_status;
    VMX_EPT_COMPOSITION_OWNER_STATUS result;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_COMPOSITION_OWNER_WRONG_IRQL;
    if (owner == NULL || out_report == NULL ||
        !Separate(owner, sizeof(*owner), out_report, sizeof(*out_report)) ||
        !Zero(out_report, sizeof(*out_report)))
        return VMX_EPT_COMPOSITION_OWNER_INVALID;
    if (Zero(owner, sizeof(*owner))) {
        *out_report = report;
        return VMX_EPT_COMPOSITION_OWNER_OK;
    }
    if (!OwnerValid(owner) ||
        !OwnedGraphsSeparate(owner, out_report, sizeof(*out_report)))
        return VMX_EPT_COMPOSITION_OWNER_INVALID;

    report.StageBefore = owner->Stage;
    if (owner->Stage == VMX_EPT_COMPOSITION_OWNER_READY) {
        installation_status =
            VmxEptInstallationOwnerDestroy(&owner->Installation);
        report.InstallationCalls = 1;
        report.InstallationStatus = installation_status;
        if (installation_status != VMX_EPT_INSTALLATION_OWNER_OK) {
            result = MapInstallationDestroy(installation_status);
            report.StageAfter = owner->Stage;
            goto publish_report;
        }
        KeMemoryBarrier();
        owner->Stage = VMX_EPT_COMPOSITION_OWNER_SESSION_ONLY;
        if (!OwnerValid(owner)) {
            result = VMX_EPT_COMPOSITION_OWNER_INVARIANT;
            report.StageAfter = owner->Stage;
            goto publish_report;
        }
    }

    session_status = VmxEptKernelConfiguredSessionDestroy(&owner->Session);
    report.SessionCalls = 1;
    report.SessionStatus = session_status;
    if (session_status != VMX_EPT_KERNEL_CONFIGURED_SESSION_OK) {
        result = MapSessionDestroy(session_status);
        report.StageAfter = owner->Stage;
        goto publish_report;
    }
    RtlSecureZeroMemory(owner, sizeof(*owner));
    report.StageAfter = 0;
    result = VMX_EPT_COMPOSITION_OWNER_OK;

publish_report:
    *out_report = report;
    return result;
}

VMX_EPT_COMPOSITION_OWNER_STATUS VmxEptCompositionOwnerValidateExternalSpan(
    const VMX_EPT_COMPOSITION_OWNER* owner,
    const void* external, SIZE_T external_size)
{
    return OwnerValid(owner) &&
        Separate(owner, sizeof(*owner), external, external_size) &&
        OwnedGraphsSeparate(owner, external, external_size) ?
        VMX_EPT_COMPOSITION_OWNER_OK : VMX_EPT_COMPOSITION_OWNER_INVALID;
}
