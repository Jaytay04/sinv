#include "vmx_ept_installation_coordinator.h"

#include "vmx.h"

#include <stdint.h>
#include <string.h>

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
    const VMX_EPT_STORAGE_OWNER* storage;
    size_t page_bytes;
    if (owner == NULL || object == NULL ||
        VmxEptSessionOwnerValidate(owner) != VMX_EPT_SESSION_OK ||
        owner->GraphBytes > SIZE_MAX)
        return 0;
    storage = owner->Storage;
    if (storage->PageCount > SIZE_MAX / sizeof(*storage->PagesByOrdinal))
        return 0;
    page_bytes = (size_t)storage->PageCount *
        sizeof(*storage->PagesByOrdinal);
    return Separate(owner, sizeof(*owner), object, size) &&
        Separate(owner->Graph, (size_t)owner->GraphBytes, object, size) &&
        Separate(storage->Tables, (size_t)storage->TableBytes, object, size) &&
        Separate(storage->PagesByOrdinal, page_bytes, object, size) &&
        Separate(storage->PhysicalIndex,
            (size_t)storage->PageCount * sizeof(*storage->PhysicalIndex),
            object, size);
}

static VMX_EPT_INSTALLATION_STATUS MapSession(
    VMX_EPT_SESSION_OPERATION_STATUS status)
{
    switch (status) {
    case VMX_EPT_SESSION_OPERATION_OK:
        return VMX_EPT_INSTALLATION_OK;
    case VMX_EPT_SESSION_OPERATION_IDENTITY_MISMATCH:
        return VMX_EPT_INSTALLATION_IDENTITY_MISMATCH;
    case VMX_EPT_SESSION_OPERATION_UNSUPPORTED:
        return VMX_EPT_INSTALLATION_UNSUPPORTED;
    case VMX_EPT_SESSION_OPERATION_NOT_READY:
        return VMX_EPT_INSTALLATION_NOT_READY;
    case VMX_EPT_SESSION_OPERATION_BUSY:
        return VMX_EPT_INSTALLATION_BUSY;
    case VMX_EPT_SESSION_OPERATION_DIRTY:
        return VMX_EPT_INSTALLATION_DIRTY;
    case VMX_EPT_SESSION_OPERATION_INSTRUCTION_FAILED:
        return VMX_EPT_INSTALLATION_INSTRUCTION_FAILED;
    case VMX_EPT_SESSION_OPERATION_OVERFLOW:
        return VMX_EPT_INSTALLATION_OVERFLOW;
    default:
        return VMX_EPT_INSTALLATION_INVALID;
    }
}

static VMX_EPT_INSTALLATION_STATUS PublishInstructionFailure(
    const VMX_EPT_INSTALLATION_PLATFORM* platform,
    VMX_EPT_INSTALLATION_REPORT* staged,
    VMX_EPT_INSTALLATION_REPORT* report)
{
    uint32_t error_read;

    if (staged->Outcome == VMX_VMWRITE_FAIL_VALID) {
        error_read = platform->ReadInstructionError(
            platform->Context, &staged->VmInstructionError);
        if (error_read > 1) {
            staged->VmInstructionError = 0;
            *report = *staged;
            return VMX_EPT_INSTALLATION_PLATFORM_INVALID;
        }
        staged->VmInstructionErrorValid = error_read;
    }
    *report = *staged;
    if (staged->Outcome == VMX_VMWRITE_FAIL_VALID ||
        staged->Outcome == VMX_VMWRITE_FAIL_INVALID)
        return VMX_EPT_INSTALLATION_INSTRUCTION_FAILED;
    return VMX_EPT_INSTALLATION_PLATFORM_INVALID;
}

static VMX_EPT_INSTALLATION_STATUS ReadField(
    const VMX_EPT_INSTALLATION_PLATFORM* platform,
    size_t field,
    uint32_t stage,
    uint32_t verify,
    uint64_t* value,
    VMX_EPT_INSTALLATION_REPORT* staged,
    VMX_EPT_INSTALLATION_REPORT* report)
{
    staged->Attempted = 1;
    staged->Stage = stage;
    staged->FailedField = field;
    staged->Outcome = (uint32_t)platform->Read(
        platform->Context, field, value);
    if (verify != 0)
        ++staged->VerifyCalls;
    else
        ++staged->ReadCalls;
    if (staged->Outcome == VMX_VMWRITE_SUCCESS) {
        staged->FailedField = 0;
        return VMX_EPT_INSTALLATION_OK;
    }
    return PublishInstructionFailure(platform, staged, report);
}

static VMX_EPT_INSTALLATION_STATUS WriteField(
    const VMX_EPT_INSTALLATION_PLATFORM* platform,
    size_t field,
    uint64_t value,
    uint32_t stage,
    VMX_EPT_INSTALLATION_REPORT* staged,
    VMX_EPT_INSTALLATION_REPORT* report)
{
    if (value > SIZE_MAX)
        return VMX_EPT_INSTALLATION_OVERFLOW;
    staged->Attempted = 1;
    staged->Stage = stage;
    staged->FailedField = field;
    staged->Outcome = (uint32_t)platform->Write(
        platform->Context, field, (size_t)value);
    ++staged->WriteCalls;
    if (staged->Outcome == VMX_VMWRITE_SUCCESS) {
        staged->FailedField = 0;
        return VMX_EPT_INSTALLATION_OK;
    }
    return PublishInstructionFailure(platform, staged, report);
}

VMX_EPT_INSTALLATION_STATUS VmxEptCoordinateInstallation(
    VMX_EPT_SESSION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    const VMX_EPT_INSTALLATION_PLATFORM* platform,
    VMX_EPT_INSTALLATION_REPORT* report)
{
    VMX_PROCESSOR_IDENTITY identity_snapshot;
    VMX_EPT_INSTALLATION_PLATFORM staged_platform;
    VMX_EPT_INSTALLATION_REPORT staged_report;
    VMX_EPT_SESSION_OPERATION_STATUS session_status;
    VMX_EPT_INSTALLATION_STATUS status;
    uint64_t ept_pointer;
    uint32_t supported;

    if (identity == NULL || platform == NULL || report == NULL ||
        !OwnerSeparate(owner, platform, sizeof(*platform)) ||
        !OwnerSeparate(owner, report, sizeof(*report)) ||
        !Separate(identity, sizeof(*identity), platform, sizeof(*platform)) ||
        !Separate(identity, sizeof(*identity), report, sizeof(*report)) ||
        !Separate(platform, sizeof(*platform), report, sizeof(*report)))
        return VMX_EPT_INSTALLATION_INVALID;
    identity_snapshot = *identity;
    staged_platform = *platform;
    if (staged_platform.Context == NULL || staged_platform.ContextBytes == 0 ||
        staged_platform.ContextBytes > SIZE_MAX ||
        staged_platform.Write == NULL || staged_platform.Read == NULL ||
        staged_platform.SupportsActivation == NULL ||
        staged_platform.ReadInstructionError == NULL ||
        staged_platform.CurrentVmcsValid != 1 ||
        staged_platform.Reserved32 != 0 || staged_platform.Reserved != 0 ||
        !OwnerSeparate(owner, staged_platform.Context,
            (size_t)staged_platform.ContextBytes) ||
        !Separate(identity, sizeof(*identity), staged_platform.Context,
            (size_t)staged_platform.ContextBytes) ||
        !Separate(platform, sizeof(*platform), staged_platform.Context,
            (size_t)staged_platform.ContextBytes) ||
        !Separate(report, sizeof(*report), staged_platform.Context,
            (size_t)staged_platform.ContextBytes))
        return VMX_EPT_INSTALLATION_PLATFORM_INVALID;

    supported = staged_platform.SupportsActivation(staged_platform.Context);
    if (supported > 1)
        return VMX_EPT_INSTALLATION_PLATFORM_INVALID;
    if (supported == 0)
        return VMX_EPT_INSTALLATION_UNSUPPORTED;

    session_status = VmxEptSessionPrepareInstallation(
        owner, &identity_snapshot, &ept_pointer);
    if (session_status != VMX_EPT_SESSION_OPERATION_OK)
        return MapSession(session_status);

    memset(&staged_report, 0, sizeof(staged_report));
    staged_report.CapabilityChecked = 1;
    staged_report.EptPointer = ept_pointer;

    status = ReadField(&staged_platform, VMCS_CTRL_CPU_BASED,
        VMX_EPT_INSTALLATION_STAGE_READ_PRIMARY, 0,
        &staged_report.OriginalPrimaryControls, &staged_report, report);
    if (status != VMX_EPT_INSTALLATION_OK)
        return status;
    status = ReadField(&staged_platform, VMCS_CTRL_SECONDARY,
        VMX_EPT_INSTALLATION_STAGE_READ_SECONDARY, 0,
        &staged_report.OriginalSecondaryControls, &staged_report, report);
    if (status != VMX_EPT_INSTALLATION_OK)
        return status;
    if ((staged_report.OriginalSecondaryControls & SECONDARY_ENABLE_EPT) != 0) {
        staged_report.Stage = VMX_EPT_INSTALLATION_STAGE_READ_SECONDARY;
        staged_report.FailedField = VMCS_CTRL_SECONDARY;
        *report = staged_report;
        return VMX_EPT_INSTALLATION_BUSY;
    }

    staged_report.ActivatedPrimaryControls =
        staged_report.OriginalPrimaryControls |
        CPU_BASED_ACTIVATE_SECONDARY_CTLS;
    staged_report.ActivatedSecondaryControls =
        staged_report.OriginalSecondaryControls | SECONDARY_ENABLE_EPT;

    status = WriteField(&staged_platform, VMCS_CTRL_EPTP, ept_pointer,
        VMX_EPT_INSTALLATION_STAGE_WRITE_EPTP, &staged_report, report);
    if (status != VMX_EPT_INSTALLATION_OK)
        return status;
    status = WriteField(&staged_platform, VMCS_CTRL_SECONDARY,
        staged_report.ActivatedSecondaryControls,
        VMX_EPT_INSTALLATION_STAGE_WRITE_SECONDARY, &staged_report, report);
    if (status != VMX_EPT_INSTALLATION_OK)
        return status;
    status = WriteField(&staged_platform, VMCS_CTRL_CPU_BASED,
        staged_report.ActivatedPrimaryControls,
        VMX_EPT_INSTALLATION_STAGE_WRITE_PRIMARY, &staged_report, report);
    if (status != VMX_EPT_INSTALLATION_OK)
        return status;

    status = ReadField(&staged_platform, VMCS_CTRL_EPTP,
        VMX_EPT_INSTALLATION_STAGE_VERIFY_EPTP, 1,
        &staged_report.ObservedEptPointer, &staged_report, report);
    if (status != VMX_EPT_INSTALLATION_OK)
        return status;
    status = ReadField(&staged_platform, VMCS_CTRL_SECONDARY,
        VMX_EPT_INSTALLATION_STAGE_VERIFY_SECONDARY, 1,
        &staged_report.ObservedSecondaryControls, &staged_report, report);
    if (status != VMX_EPT_INSTALLATION_OK)
        return status;
    status = ReadField(&staged_platform, VMCS_CTRL_CPU_BASED,
        VMX_EPT_INSTALLATION_STAGE_VERIFY_PRIMARY, 1,
        &staged_report.ObservedPrimaryControls, &staged_report, report);
    if (status != VMX_EPT_INSTALLATION_OK)
        return status;
    if (staged_report.ObservedEptPointer != ept_pointer ||
        staged_report.ObservedSecondaryControls !=
            staged_report.ActivatedSecondaryControls ||
        staged_report.ObservedPrimaryControls !=
            staged_report.ActivatedPrimaryControls) {
        staged_report.FailedField =
            staged_report.ObservedEptPointer != ept_pointer ?
                VMCS_CTRL_EPTP :
            staged_report.ObservedSecondaryControls !=
                staged_report.ActivatedSecondaryControls ?
                VMCS_CTRL_SECONDARY : VMCS_CTRL_CPU_BASED;
        *report = staged_report;
        return VMX_EPT_INSTALLATION_PLATFORM_INVALID;
    }

    staged_report.Stage = VMX_EPT_INSTALLATION_STAGE_COMPLETE;
    staged_report.Outcome = VMX_VMWRITE_SUCCESS;
    *report = staged_report;
    return VMX_EPT_INSTALLATION_OK;
}
