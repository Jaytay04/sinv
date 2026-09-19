#include "loader_service.h"

#include "driver_interface.h"
#include <string.h>

LOADER_PATH_COMPOSE_RESULT LoaderComposeSiblingDriverPath(
    const wchar_t *module_path,
    const wchar_t *driver_name,
    wchar_t *driver_path,
    size_t capacity)
{
    size_t module_length;
    size_t driver_length;
    size_t separator_index;
    size_t directory_length;
    size_t required;

    if (module_path == NULL || driver_name == NULL ||
        driver_path == NULL || capacity == 0u) {
        return LOADER_PATH_COMPOSE_INVALID_ARGUMENT;
    }
    driver_path[0] = L'\0';

    module_length = wcslen(module_path);
    driver_length = wcslen(driver_name);
    if (module_length == 0u || driver_length == 0u) {
        return LOADER_PATH_COMPOSE_INVALID_ARGUMENT;
    }
    if (module_path[module_length - 1u] == L'\\' ||
        module_path[module_length - 1u] == L'/') {
        return LOADER_PATH_COMPOSE_NO_DIRECTORY;
    }
    for (separator_index = module_length; separator_index != 0u; ) {
        --separator_index;
        if (module_path[separator_index] == L'\\' ||
            module_path[separator_index] == L'/') {
            break;
        }
    }
    if (module_path[separator_index] != L'\\' &&
        module_path[separator_index] != L'/') {
        return LOADER_PATH_COMPOSE_NO_DIRECTORY;
    }
    for (size_t index = 0u; index < driver_length; ++index) {
        if (driver_name[index] == L'\\' || driver_name[index] == L'/' ||
            driver_name[index] == L':') {
            return LOADER_PATH_COMPOSE_INVALID_ARGUMENT;
        }
    }

    directory_length = separator_index + 1u;
    if (directory_length > SIZE_MAX - 1u ||
        driver_length > SIZE_MAX - directory_length - 1u) {
        return LOADER_PATH_COMPOSE_OVERFLOW;
    }
    required = directory_length + driver_length + 1u;
    if (required > capacity) {
        return LOADER_PATH_COMPOSE_BUFFER_TOO_SMALL;
    }

    wmemcpy(driver_path, module_path, directory_length);
    wmemcpy(driver_path + directory_length, driver_name, driver_length);
    driver_path[required - 1u] = L'\0';
    return LOADER_PATH_COMPOSE_SUCCESS;
}

#define LOADER_POLL_DELAY_MIN_MS 50u
#define LOADER_POLL_DELAY_MAX_MS 1000u

LOADER_UNLOAD_IOCTL_RESULT LoaderClassifyUnloadResponse(
    int transport_succeeded,
    uint32_t status)
{
    if (!transport_succeeded) {
        return LOADER_UNLOAD_IOCTL_TRANSPORT_FAILURE;
    }
    if (status == ERR_SUCCESS) {
        return LOADER_UNLOAD_IOCTL_SUCCESS;
    }
    if (status == ERR_DRIVER_NOT_LOADED) {
        return LOADER_UNLOAD_IOCTL_NOT_LOADED;
    }
    return LOADER_UNLOAD_IOCTL_DOMAIN_FAILURE;
}

LOADER_SERVICE_POLL_DECISION LoaderEvaluateServiceStatus(uint32_t state)
{
    if (state == LOADER_SERVICE_STOPPED) {
        return LOADER_SERVICE_POLL_STOPPED;
    }
    return LOADER_SERVICE_POLL_WAIT;
}

LOADER_SERVICE_START_DECISION LoaderEvaluateServiceStart(uint32_t state)
{
    if (state == LOADER_SERVICE_RUNNING) {
        return LOADER_SERVICE_START_RUNNING;
    }
    if (state == LOADER_SERVICE_START_PENDING) {
        return LOADER_SERVICE_START_WAIT;
    }
    return LOADER_SERVICE_START_FAILED;
}

int LoaderServiceTypeIsKernelDriver(uint32_t service_type)
{
    return service_type == LOADER_SERVICE_KERNEL_DRIVER;
}
#define WIRE_FLAGS_LOADED (VMX_PERSISTENT_PUBLIC_RECORD_ACTIVE | \
    VMX_PERSISTENT_PUBLIC_RECORD_PUBLISHED | \
    VMX_PERSISTENT_PUBLIC_RECORD_LEVEL_ENTERED | VMX_PERSISTENT_PUBLIC_RECORD_LEVEL_LEFT)
#define WIRE_RESIDUAL_REQUIRED (VMX_PERSISTENT_PUBLIC_RESIDUAL_CONTEXT_RESOURCES | \
    VMX_PERSISTENT_PUBLIC_RESIDUAL_RESTORE | VMX_PERSISTENT_PUBLIC_RESIDUAL_VMX_ACTIVE | \
    VMX_PERSISTENT_PUBLIC_RESIDUAL_VMCS_CURRENT)
#define WIRE_RESIDUAL_ALLOWED (WIRE_RESIDUAL_REQUIRED | \
    VMX_PERSISTENT_PUBLIC_RESIDUAL_CR0 | VMX_PERSISTENT_PUBLIC_RESIDUAL_CR4)

static int WireDiagnosticValid(uint32_t source, uint32_t outcome,
    uint32_t error_valid, uint32_t error, uint32_t first, uint32_t last)
{
    if (source == VMX_PERSISTENT_PUBLIC_DIAGNOSTIC_NONE)
        return outcome == 0u && error_valid == 0u && error == 0u;
    return source >= first && source <= last && (outcome == LOADER_PERSISTENT_INSTRUCTION_FAIL_VALID || outcome == LOADER_PERSISTENT_INSTRUCTION_FAIL_INVALID) &&
        error_valid <= 1u && (error_valid || error == 0u) &&
        (outcome == LOADER_PERSISTENT_INSTRUCTION_FAIL_VALID || (error_valid == 0u && error == 0u));
}
static int WireHeaderValid(const VMX_PERSISTENT_PUBLIC_STATUS_HEADER *h)
{
    uint32_t known = VMX_PERSISTENT_PUBLIC_HEADER_HAS_OPERATION_ERROR |
        VMX_PERSISTENT_PUBLIC_HEADER_HAS_CLEANUP_ERROR | VMX_PERSISTENT_PUBLIC_HEADER_BUSY;
    if ((h->Flags & ~known) != 0u || h->LifecycleState > LOADER_PERSISTENT_STATUS_ERROR) return 0;
    if ((h->Flags & VMX_PERSISTENT_PUBLIC_HEADER_HAS_OPERATION_ERROR) != 0u) {
        if (h->FirstOperationStatus < LOADER_PERSISTENT_STATUS_INVARIANT || h->FirstOperationStatus >= 0 ||
            h->FirstOperationIndex >= h->ContextCount) return 0;
    } else if (h->FirstOperationStatus != 0 ||
               h->FirstOperationIndex != VMX_PERSISTENT_PUBLIC_STATUS_NO_INDEX) return 0;
    if ((h->Flags & VMX_PERSISTENT_PUBLIC_HEADER_HAS_CLEANUP_ERROR) != 0u) {
        if (h->FirstCleanupStatus < LOADER_PERSISTENT_STATUS_INVARIANT || h->FirstCleanupStatus >= 0 ||
            h->FirstCleanupIndex >= h->ContextCount) return 0;
    } else if (h->FirstCleanupStatus != 0 ||
               h->FirstCleanupIndex != VMX_PERSISTENT_PUBLIC_STATUS_NO_INDEX) return 0;
    return 1;
}
static int WireRecordValid(const VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD *r)
{
    const uint32_t record_flags = (UINT32_C(1) << 5) - 1u;
    const uint32_t residual_flags = (UINT32_C(1) << 11) - 1u;
    return r->Number < 64u && r->LifecycleStage <= LOADER_PERSISTENT_CPU_FAILED &&
        (r->Flags & ~record_flags) == 0u && (r->OwnedMask & ~UINT32_C(7)) == 0u &&
        (r->ContextOwnedResources & ~UINT32_C(63)) == 0u &&
        (r->ResidualFlags & ~residual_flags) == 0u && r->OperationStage <= LOADER_PERSISTENT_OPERATION_FAILED &&
        r->PrimaryStatus >= LOADER_PERSISTENT_OPERATION_INVARIANT && r->PrimaryStatus <= 0 &&
        r->CleanupStatus >= LOADER_PERSISTENT_OPERATION_INVARIANT && r->CleanupStatus <= 0 && r->Reserved == 0u &&
        WireDiagnosticValid(r->PrimarySource,r->PrimaryOutcome,
            r->PrimaryErrorValid,r->PrimaryError,1u,4u) &&
        WireDiagnosticValid(r->CleanupSource,r->CleanupOutcome,
            r->CleanupErrorValid,r->CleanupError,5u,6u);
}
static int WireRecordLoaded(const VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD *r)
{
    uint32_t diagnostic = r->PrimarySource | r->PrimaryOutcome | r->PrimaryErrorValid |
        r->PrimaryError | r->CleanupSource | r->CleanupOutcome |
        r->CleanupErrorValid | r->CleanupError;
    return r->LifecycleStage == LOADER_PERSISTENT_CPU_ACTIVE && r->Flags == WIRE_FLAGS_LOADED &&
        r->OwnedMask == LOADER_PERSISTENT_OWNED_LOADED &&
        r->ContextOwnedResources == LOADER_PERSISTENT_CONTEXT_RESOURCES_ALL &&
        (r->ResidualFlags & WIRE_RESIDUAL_REQUIRED) == WIRE_RESIDUAL_REQUIRED &&
        (r->ResidualFlags & ~WIRE_RESIDUAL_ALLOWED) == 0u &&
        r->OperationStage == LOADER_PERSISTENT_OPERATION_RESIDENT &&
        r->PrimaryStatus == 0 && r->CleanupStatus == 0 && diagnostic == 0u;
}
LOADER_LOAD_VERIFY_DECISION LoaderDecideLoadVerification(
    int query_valid, LOADER_PERSISTENT_WIRE_CLASS classification)
{
    return query_valid && classification == LOADER_PERSISTENT_WIRE_LOADED
        ? LOADER_LOAD_VERIFY_ACCEPT : LOADER_LOAD_VERIFY_ROLLBACK;
}
LOADER_PERSISTENT_WIRE_CLASS LoaderClassifyPersistentStatus(
    const void *status_bytes, size_t status_size)
{
    const unsigned char *bytes = status_bytes;
    VMX_PERSISTENT_PUBLIC_STATUS_HEADER h;
    size_t records_size, required, i, j;
    int loaded = 1;
    if (bytes == NULL || status_size < sizeof(h)) return LOADER_PERSISTENT_WIRE_SHORT;
    memcpy(&h,bytes,sizeof(h));
    if (h.Version != VMX_PERSISTENT_PUBLIC_STATUS_VERSION ||
        h.HeaderSize != sizeof(h) || h.RecordSize != sizeof(VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD) ||
        h.Reserved != 0u) return LOADER_PERSISTENT_WIRE_MALFORMED;
    if ((size_t)h.ContextCount > (SIZE_MAX - sizeof(h)) / sizeof(VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD))
        return LOADER_PERSISTENT_WIRE_OVERFLOW;
    records_size = (size_t)h.ContextCount * sizeof(VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD);
    required = sizeof(h) + records_size;
    if (required > UINT32_MAX) return LOADER_PERSISTENT_WIRE_OVERFLOW;
    if (status_size < required) return LOADER_PERSISTENT_WIRE_SHORT;
    if (status_size != required || h.TotalSize != required || !WireHeaderValid(&h))
        return LOADER_PERSISTENT_WIRE_MALFORMED;
    if (h.ContextCount == 0u)
        return h.LifecycleState == LOADER_PERSISTENT_STATUS_UNLOADED && h.Flags == 0u
            ? LOADER_PERSISTENT_WIRE_DETACHED : LOADER_PERSISTENT_WIRE_RESIDUAL_ERROR;
    if (h.LifecycleState != LOADER_PERSISTENT_STATUS_LOADED || h.Flags != 0u) loaded = 0;
    for (i=0;i<h.ContextCount;i++) {
        VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD r;
        memcpy(&r,bytes+sizeof(h)+i*sizeof(r),sizeof(r));
        if (r.SystemIndex != (uint32_t)i || !WireRecordValid(&r))
            return LOADER_PERSISTENT_WIRE_MALFORMED;
        for (j=0;j<i;j++) {
            VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD previous;
            memcpy(&previous,bytes+sizeof(h)+j*sizeof(previous),sizeof(previous));
            if (r.Group == previous.Group && r.Number == previous.Number)
                return LOADER_PERSISTENT_WIRE_MALFORMED;
        }
        if (!WireRecordLoaded(&r)) loaded=0;
    }
    return loaded ? LOADER_PERSISTENT_WIRE_LOADED : LOADER_PERSISTENT_WIRE_RESIDUAL_ERROR;
}

static int EptBytesZero(const void *value, size_t size)
{
    const unsigned char *bytes = (const unsigned char *)value;
    size_t index;
    for (index = 0; index < size; ++index)
        if (bytes[index] != 0u) return 0;
    return 1;
}

static int EptEvidenceValid(const HYPERVISOR_EPT_STATUS_RESPONSE *status)
{
    const uint32_t flags = status->Flags;
    if ((flags & HYPERVISOR_EPT_STATUS_FLAG_START_EVIDENCE) == 0u) {
        if (!EptBytesZero(&status->Start, sizeof(status->Start))) return 0;
    } else if (status->Start.AttachCalls > 1u ||
        status->Start.StartCalls > 1u ||
        status->Start.EptInitializeCalls > 1u ||
        status->Start.RollbackCalls > 1u ||
        status->Start.DiagnosticValid > 1u ||
        (status->Start.DiagnosticValid != 0u &&
         (status->Start.DiagnosticKind > 11u ||
          status->Start.DiagnosticNumber >= 64u))) {
        return 0;
    }
    if ((flags & HYPERVISOR_EPT_STATUS_FLAG_DESTROY_EVIDENCE) == 0u) {
        if (!EptBytesZero(&status->Destroy, sizeof(status->Destroy))) return 0;
    } else if (status->Destroy.CompositionDestroyCalls > 1u ||
        status->Destroy.DetachCalls > 1u ||
        status->Destroy.PhaseAfterCalls > 1u) {
        return 0;
    }
    return 1;
}

LOADER_EPT_WIRE_CLASS LoaderClassifyEptStatus(
    const void *status_bytes, size_t status_size)
{
    const uint32_t known_flags =
        HYPERVISOR_EPT_STATUS_FLAG_RUNTIME_AUTHENTICATED |
        HYPERVISOR_EPT_STATUS_FLAG_EPT_ZERO |
        HYPERVISOR_EPT_STATUS_FLAG_EPT_AUTHENTICATED |
        HYPERVISOR_EPT_STATUS_FLAG_START_EVIDENCE |
        HYPERVISOR_EPT_STATUS_FLAG_DESTROY_EVIDENCE |
        HYPERVISOR_EPT_STATUS_FLAG_CLEANUP_REQUIRED;
    HYPERVISOR_EPT_STATUS_RESPONSE status;
    uint32_t expected_flags, expected_class;
    int ept_zero;

    if (status_bytes == NULL || status_size < sizeof(status))
        return LOADER_EPT_WIRE_SHORT;
    if (status_size != sizeof(status)) return LOADER_EPT_WIRE_MALFORMED;
    memcpy(&status, status_bytes, sizeof(status));
    if (status.Version != HYPERVISOR_EPT_STATUS_VERSION ||
        status.Size != HYPERVISOR_EPT_STATUS_RESPONSE_SIZE ||
        status.RuntimePhase > 4u || status.EptStage > 4u ||
        (status.Flags & ~known_flags) != 0u || !EptEvidenceValid(&status))
        return LOADER_EPT_WIRE_MALFORMED;

    ept_zero = (status.Flags & HYPERVISOR_EPT_STATUS_FLAG_EPT_ZERO) != 0u;
    expected_flags = status.Flags &
        (HYPERVISOR_EPT_STATUS_FLAG_START_EVIDENCE |
         HYPERVISOR_EPT_STATUS_FLAG_DESTROY_EVIDENCE);
    if (status.PhaseQueryStatus == 0)
        expected_flags |= HYPERVISOR_EPT_STATUS_FLAG_RUNTIME_AUTHENTICATED;
    if (ept_zero)
        expected_flags |= HYPERVISOR_EPT_STATUS_FLAG_EPT_ZERO;
    else if (status.EptValidateStatus == 0)
        expected_flags |= HYPERVISOR_EPT_STATUS_FLAG_EPT_AUTHENTICATED;

    if (status.PhaseQueryStatus != 0) {
        expected_class = HYPERVISOR_EPT_STATUS_BLOCKED;
    } else if (ept_zero) {
        if (status.EptStage != 0u || status.EptValidateStatus != 0)
            expected_class = HYPERVISOR_EPT_STATUS_MALFORMED;
        else if (status.RuntimePhase == 1u)
            expected_class = HYPERVISOR_EPT_STATUS_DETACHED;
        else if (status.RuntimePhase == 4u)
            expected_class = HYPERVISOR_EPT_STATUS_TEARDOWN_PENDING;
        else
            expected_class = HYPERVISOR_EPT_STATUS_MALFORMED;
    } else if (status.EptValidateStatus != 0 ||
        status.EptStage < 1u || status.EptStage > 4u) {
        expected_class = HYPERVISOR_EPT_STATUS_MALFORMED;
    } else if (status.EptStage == 4u) {
        expected_class = HYPERVISOR_EPT_STATUS_BLOCKED;
    } else if (status.EptStage == 1u && status.RuntimePhase == 3u) {
        expected_class = HYPERVISOR_EPT_STATUS_READY;
    } else if ((status.EptStage == 2u && status.RuntimePhase == 3u) ||
        (status.EptStage == 3u &&
         (status.RuntimePhase == 3u || status.RuntimePhase == 4u))) {
        expected_class = HYPERVISOR_EPT_STATUS_TEARDOWN_PENDING;
    } else {
        expected_class = HYPERVISOR_EPT_STATUS_MALFORMED;
    }
    if (expected_class == HYPERVISOR_EPT_STATUS_TEARDOWN_PENDING ||
        expected_class == HYPERVISOR_EPT_STATUS_BLOCKED)
        expected_flags |= HYPERVISOR_EPT_STATUS_FLAG_CLEANUP_REQUIRED;
    if (status.Flags != expected_flags || status.Classification != expected_class)
        return LOADER_EPT_WIRE_MALFORMED;
    if (expected_class == HYPERVISOR_EPT_STATUS_DETACHED)
        return LOADER_EPT_WIRE_DETACHED;
    if (expected_class == HYPERVISOR_EPT_STATUS_READY)
        return LOADER_EPT_WIRE_READY;
    return LOADER_EPT_WIRE_RESIDUAL;
}

int LoaderEptStatusProvesReady(
    int query_valid, LOADER_EPT_WIRE_CLASS classification)
{
    return query_valid && classification == LOADER_EPT_WIRE_READY;
}

int LoaderEptStatusProvesDetached(
    int query_valid, LOADER_EPT_WIRE_CLASS classification)
{
    return query_valid && classification == LOADER_EPT_WIRE_DETACHED;
}

LOADER_CR3_WIRE_CLASS LoaderClassifyCr3Status(
    const void *status_bytes, size_t status_size)
{
    const uint32_t known_flags = HYPERVISOR_CR3_STATUS_FLAG_AUTHENTICATED |
        HYPERVISOR_CR3_STATUS_FLAG_PCIDE_ENABLED |
        HYPERVISOR_CR3_STATUS_FLAG_ANY_RESIDENT |
        HYPERVISOR_CR3_STATUS_FLAG_ANY_TRANSITION;
    HYPERVISOR_CR3_STATUS_HEADER header;
    uint64_t transitions = 0u;
    uint32_t residents = 0u;
    size_t total, index, prior;

    if (status_bytes == NULL || status_size < sizeof(header))
        return LOADER_CR3_WIRE_SHORT;
    memcpy(&header, status_bytes, sizeof(header));
    if (header.ContextCount >
        (SIZE_MAX - HYPERVISOR_CR3_STATUS_HEADER_SIZE) /
            HYPERVISOR_CR3_STATUS_RECORD_SIZE)
        return LOADER_CR3_WIRE_OVERFLOW;
    total = HYPERVISOR_CR3_STATUS_HEADER_SIZE +
        (size_t)header.ContextCount * HYPERVISOR_CR3_STATUS_RECORD_SIZE;
    if (total > UINT32_MAX) return LOADER_CR3_WIRE_OVERFLOW;
    if (status_size < total) return LOADER_CR3_WIRE_SHORT;
    if (status_size != total ||
        header.Version != HYPERVISOR_CR3_STATUS_VERSION ||
        header.HeaderSize != HYPERVISOR_CR3_STATUS_HEADER_SIZE ||
        header.RecordSize != HYPERVISOR_CR3_STATUS_RECORD_SIZE ||
        header.TotalSize != total || header.OwnerGeneration == 0u ||
        header.TargetGeneration == 0u || header.ContextCount == 0u ||
        header.TargetCount == 0u ||
        header.TargetCount > HYPERVISOR_CR3_STATUS_MAX_TARGETS ||
        header.PhysicalAddressBits < HYPERVISOR_CR3_STATUS_MIN_PHYSICAL_BITS ||
        header.PhysicalAddressBits > HYPERVISOR_CR3_STATUS_MAX_PHYSICAL_BITS ||
        (header.Flags & ~known_flags) != 0u ||
        (header.Flags & HYPERVISOR_CR3_STATUS_FLAG_AUTHENTICATED) == 0u ||
        header.ResidentCount > header.ContextCount || header.Reserved32 != 0u)
        return LOADER_CR3_WIRE_MALFORMED;

    for (index = 0u; index < header.ContextCount; ++index) {
        HYPERVISOR_CR3_STATUS_RECORD record;
        memcpy(&record, (const unsigned char*)status_bytes +
            sizeof(header) + index * sizeof(record), sizeof(record));
        if (record.SystemIndex != index || record.Number >= 64u ||
            record.Resident > 1u || record.Reserved != 0u ||
            record.Pcid > HYPERVISOR_CR3_STATUS_MAX_PCID ||
            (!record.Resident &&
             (record.MatchedIndex != HYPERVISOR_CR3_STATUS_NO_INDEX ||
              record.Pcid != 0u)) ||
            (record.Resident && record.MatchedIndex >= header.TargetCount) ||
            ((header.Flags & HYPERVISOR_CR3_STATUS_FLAG_PCIDE_ENABLED) == 0u &&
             record.Pcid != 0u))
            return LOADER_CR3_WIRE_MALFORMED;
        for (prior = 0u; prior < index; ++prior) {
            HYPERVISOR_CR3_STATUS_RECORD previous;
            memcpy(&previous, (const unsigned char*)status_bytes +
                sizeof(header) + prior * sizeof(previous), sizeof(previous));
            if (previous.Group == record.Group &&
                previous.Number == record.Number)
                return LOADER_CR3_WIRE_MALFORMED;
        }
        residents += record.Resident;
        if (UINT64_MAX - transitions < record.TransitionSequence)
            return LOADER_CR3_WIRE_OVERFLOW;
        transitions += record.TransitionSequence;
    }
    if (residents != header.ResidentCount ||
        transitions != header.TotalTransitions ||
        (((header.Flags & HYPERVISOR_CR3_STATUS_FLAG_ANY_RESIDENT) != 0u) !=
            (residents != 0u)) ||
        (((header.Flags & HYPERVISOR_CR3_STATUS_FLAG_ANY_TRANSITION) != 0u) !=
            (transitions != 0u)))
        return LOADER_CR3_WIRE_MALFORMED;
    return transitions != 0u ? LOADER_CR3_WIRE_OBSERVED :
        LOADER_CR3_WIRE_INACTIVE;
}

LOADER_PERSISTENT_STATUS_PROOF LoaderValidateDetachedStatus(
    const void *status_bytes, size_t status_size)
{
    VMX_PERSISTENT_PUBLIC_STATUS_HEADER status;
    if (status_bytes == NULL || status_size != sizeof(status))
        return LOADER_PERSISTENT_STATUS_SHORT;
    memcpy(&status, status_bytes, sizeof(status));
    if (status.Version != VMX_PERSISTENT_PUBLIC_STATUS_VERSION ||
        status.HeaderSize != VMX_PERSISTENT_PUBLIC_STATUS_HEADER_SIZE ||
        status.RecordSize != VMX_PERSISTENT_PUBLIC_STATUS_RECORD_SIZE ||
        status.TotalSize != VMX_PERSISTENT_PUBLIC_STATUS_HEADER_SIZE ||
        status.ContextCount != 0u || status.LifecycleState != LOADER_PERSISTENT_STATUS_UNLOADED ||
        status.Flags != 0u || status.Reserved != 0u ||
        status.FirstOperationStatus != 0 ||
        status.FirstOperationIndex != VMX_PERSISTENT_PUBLIC_STATUS_NO_INDEX ||
        status.FirstCleanupStatus != 0 ||
        status.FirstCleanupIndex != VMX_PERSISTENT_PUBLIC_STATUS_NO_INDEX)
        return LOADER_PERSISTENT_STATUS_MALFORMED;
    return LOADER_PERSISTENT_STATUS_DETACHED;
}

LOADER_OPERATOR_RUNTIME_CLASS LoaderClassifyOperatorRuntime(
    int persistent_query_valid,
    LOADER_PERSISTENT_WIRE_CLASS persistent_classification,
    int ept_query_valid,
    LOADER_EPT_WIRE_CLASS ept_classification)
{
    if (!persistent_query_valid || !ept_query_valid ||
        persistent_classification < LOADER_PERSISTENT_WIRE_DETACHED ||
        persistent_classification > LOADER_PERSISTENT_WIRE_OVERFLOW ||
        ept_classification < LOADER_EPT_WIRE_DETACHED ||
        ept_classification > LOADER_EPT_WIRE_SHORT)
        return LOADER_OPERATOR_RUNTIME_UNVERIFIED;

    if (persistent_classification == LOADER_PERSISTENT_WIRE_MALFORMED ||
        persistent_classification == LOADER_PERSISTENT_WIRE_SHORT ||
        persistent_classification == LOADER_PERSISTENT_WIRE_OVERFLOW ||
        ept_classification == LOADER_EPT_WIRE_MALFORMED ||
        ept_classification == LOADER_EPT_WIRE_SHORT)
        return LOADER_OPERATOR_RUNTIME_UNVERIFIED;

    if (persistent_classification == LOADER_PERSISTENT_WIRE_DETACHED &&
        ept_classification == LOADER_EPT_WIRE_DETACHED)
        return LOADER_OPERATOR_RUNTIME_DETACHED;

    if (persistent_classification == LOADER_PERSISTENT_WIRE_LOADED &&
        ept_classification == LOADER_EPT_WIRE_READY)
        return LOADER_OPERATOR_RUNTIME_READY;

    return LOADER_OPERATOR_RUNTIME_RESIDUAL;
}

LOADER_SCM_CLEANUP_POLICY LoaderDecideScmCleanup(
    int device_opened, LOADER_UNLOAD_IOCTL_RESULT ioctl_result,
    LOADER_PERSISTENT_STATUS_PROOF status_proof)
{
    if (!device_opened || status_proof != LOADER_PERSISTENT_STATUS_DETACHED)
        return LOADER_SCM_CLEANUP_FORBIDDEN;
    if (ioctl_result == LOADER_UNLOAD_IOCTL_SUCCESS ||
        ioctl_result == LOADER_UNLOAD_IOCTL_NOT_LOADED)
        return LOADER_SCM_CLEANUP_STOP_AND_DELETE;
    return LOADER_SCM_CLEANUP_FORBIDDEN;
}
LOADER_SERVICE_ABSENCE_DECISION LoaderEvaluateServiceAbsence(
    int service_opened, uint32_t open_error)
{
    if (service_opened) return LOADER_SERVICE_ABSENCE_WAIT;
    if (open_error == LOADER_ERROR_SERVICE_DOES_NOT_EXIST)
        return LOADER_SERVICE_ABSENCE_CONFIRMED;
    if (open_error == LOADER_ERROR_SERVICE_MARKED_FOR_DELETE)
        return LOADER_SERVICE_ABSENCE_WAIT;
    return LOADER_SERVICE_ABSENCE_FAILED;
}
LOADER_SERVICE_REUSE_POLICY LoaderDecideExistingServiceReuse(uint32_t state)
{
    return state == LOADER_SERVICE_STOPPED
        ? LOADER_SERVICE_REUSE_REFRESH_STOPPED
        : LOADER_SERVICE_REUSE_REJECT;
}
uint32_t LoaderServicePollDelayMs(uint32_t wait_hint_ms)
{
    uint32_t delay = wait_hint_ms / 10u;

    if (delay < LOADER_POLL_DELAY_MIN_MS) {
        delay = LOADER_POLL_DELAY_MIN_MS;
    }
    if (delay > LOADER_POLL_DELAY_MAX_MS) {
        delay = LOADER_POLL_DELAY_MAX_MS;
    }
    return delay;
}
