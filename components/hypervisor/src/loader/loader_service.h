/*
 * Pure decisions shared by the loader and deterministic unit tests.
 */
#ifndef HYPERSINV_LOADER_SERVICE_H
#define HYPERSINV_LOADER_SERVICE_H

#include <stdint.h>
#include <stddef.h>
#include <wchar.h>
#include "vmx_persistent_public_status.h"
#include "vmx_ept_status_abi.h"
#include "vmx_cr3_status_abi.h"

typedef enum LOADER_UNLOAD_IOCTL_RESULT {
    LOADER_UNLOAD_IOCTL_TRANSPORT_FAILURE = 0,
    LOADER_UNLOAD_IOCTL_SUCCESS = 1,
    LOADER_UNLOAD_IOCTL_NOT_LOADED = 2,
    LOADER_UNLOAD_IOCTL_DOMAIN_FAILURE = 3
} LOADER_UNLOAD_IOCTL_RESULT;

typedef enum LOADER_SERVICE_POLL_DECISION {
    LOADER_SERVICE_POLL_STOPPED = 0,
    LOADER_SERVICE_POLL_WAIT = 1
} LOADER_SERVICE_POLL_DECISION;

typedef enum LOADER_SCM_CLEANUP_POLICY {
    LOADER_SCM_CLEANUP_FORBIDDEN = 0,
    LOADER_SCM_CLEANUP_STOP_AND_DELETE = 1
} LOADER_SCM_CLEANUP_POLICY;

typedef enum LOADER_PERSISTENT_STATUS_PROOF {
    LOADER_PERSISTENT_STATUS_UNAVAILABLE = 0,
    LOADER_PERSISTENT_STATUS_DETACHED = 1,
    LOADER_PERSISTENT_STATUS_MALFORMED = 2,
    LOADER_PERSISTENT_STATUS_SHORT = 3,
    LOADER_PERSISTENT_STATUS_BUSY = 4,
    LOADER_PERSISTENT_STATUS_TIMEOUT = 5
} LOADER_PERSISTENT_STATUS_PROOF;
typedef enum LOADER_SERVICE_ABSENCE_DECISION {
    LOADER_SERVICE_ABSENCE_FAILED = 0,
    LOADER_SERVICE_ABSENCE_WAIT = 1,
    LOADER_SERVICE_ABSENCE_CONFIRMED = 2
} LOADER_SERVICE_ABSENCE_DECISION;

typedef enum LOADER_LOAD_VERIFY_DECISION {
    LOADER_LOAD_VERIFY_ACCEPT = 0,
    LOADER_LOAD_VERIFY_ROLLBACK = 1
} LOADER_LOAD_VERIFY_DECISION;

typedef enum LOADER_PERSISTENT_WIRE_CLASS {
    LOADER_PERSISTENT_WIRE_DETACHED = 0,
    LOADER_PERSISTENT_WIRE_LOADED = 1,
    LOADER_PERSISTENT_WIRE_RESIDUAL_ERROR = 2,
    LOADER_PERSISTENT_WIRE_MALFORMED = 3,
    LOADER_PERSISTENT_WIRE_SHORT = 4,
    LOADER_PERSISTENT_WIRE_OVERFLOW = 5
} LOADER_PERSISTENT_WIRE_CLASS;

typedef enum LOADER_EPT_WIRE_CLASS {
    LOADER_EPT_WIRE_DETACHED = 0,
    LOADER_EPT_WIRE_READY = 1,
    LOADER_EPT_WIRE_RESIDUAL = 2,
    LOADER_EPT_WIRE_MALFORMED = 3,
    LOADER_EPT_WIRE_SHORT = 4
} LOADER_EPT_WIRE_CLASS;

typedef enum LOADER_CR3_WIRE_CLASS {
    LOADER_CR3_WIRE_OBSERVED = 0,
    LOADER_CR3_WIRE_INACTIVE = 1,
    LOADER_CR3_WIRE_MALFORMED = 2,
    LOADER_CR3_WIRE_SHORT = 3,
    LOADER_CR3_WIRE_OVERFLOW = 4
} LOADER_CR3_WIRE_CLASS;

/* Truthful operator-facing composition of the authoritative Runtime and EPT
 * status queries. READY and DETACHED require matching canonical evidence from
 * both queries. Contradictory but well-formed evidence is RESIDUAL; failed or
 * malformed queries are UNVERIFIED. */
typedef enum LOADER_OPERATOR_RUNTIME_CLASS {
    LOADER_OPERATOR_RUNTIME_UNVERIFIED = 0,
    LOADER_OPERATOR_RUNTIME_DETACHED = 1,
    LOADER_OPERATOR_RUNTIME_READY = 2,
    LOADER_OPERATOR_RUNTIME_RESIDUAL = 3
} LOADER_OPERATOR_RUNTIME_CLASS;

typedef enum LOADER_SERVICE_START_DECISION {
    LOADER_SERVICE_START_FAILED = 0,
    LOADER_SERVICE_START_WAIT = 1,
    LOADER_SERVICE_START_RUNNING = 2
} LOADER_SERVICE_START_DECISION;
typedef enum LOADER_SERVICE_REUSE_POLICY {
    LOADER_SERVICE_REUSE_REJECT = 0,
    LOADER_SERVICE_REUSE_REFRESH_STOPPED = 1
} LOADER_SERVICE_REUSE_POLICY;

#define LOADER_SERVICE_KERNEL_DRIVER    1u
#define LOADER_SERVICE_STOPPED          1u
#define LOADER_SERVICE_START_PENDING    2u
#define LOADER_SERVICE_STOP_PENDING     3u
#define LOADER_SERVICE_RUNNING          4u
#define LOADER_SERVICE_CONTINUE_PENDING 5u
#define LOADER_SERVICE_PAUSE_PENDING   6u
#define LOADER_SERVICE_PAUSED          7u
#define LOADER_ERROR_SERVICE_DOES_NOT_EXIST 1060u
#define LOADER_ERROR_SERVICE_MARKED_FOR_DELETE 1072u
#define LOADER_PERSISTENT_STATUS_UNLOADED 0u
#define LOADER_PERSISTENT_STATUS_LOADED 3u
#define LOADER_PERSISTENT_CPU_ACTIVE 4u
#define LOADER_PERSISTENT_OPERATION_RESIDENT 7u
#define LOADER_PERSISTENT_OWNED_LOADED 7u
#define LOADER_PERSISTENT_CONTEXT_RESOURCES_ALL 63u
#define LOADER_PERSISTENT_STATUS_ERROR 6u
#define LOADER_PERSISTENT_CPU_FAILED 7u
#define LOADER_PERSISTENT_OPERATION_FAILED 10u
#define LOADER_PERSISTENT_STATUS_INVARIANT (-5)
#define LOADER_PERSISTENT_OPERATION_INVARIANT (-11)
#define LOADER_PERSISTENT_INSTRUCTION_FAIL_VALID 2u
#define LOADER_PERSISTENT_INSTRUCTION_FAIL_INVALID 3u

typedef enum LOADER_PATH_COMPOSE_RESULT {
    LOADER_PATH_COMPOSE_SUCCESS = 0,
    LOADER_PATH_COMPOSE_INVALID_ARGUMENT = 1,
    LOADER_PATH_COMPOSE_NO_DIRECTORY = 2,
    LOADER_PATH_COMPOSE_BUFFER_TOO_SMALL = 3,
    LOADER_PATH_COMPOSE_OVERFLOW = 4
} LOADER_PATH_COMPOSE_RESULT;

LOADER_PATH_COMPOSE_RESULT LoaderComposeSiblingDriverPath(
    const wchar_t *module_path,
    const wchar_t *driver_name,
    wchar_t *driver_path,
    size_t capacity);

LOADER_UNLOAD_IOCTL_RESULT LoaderClassifyUnloadResponse(
    int transport_succeeded,
    uint32_t status);

LOADER_SERVICE_POLL_DECISION LoaderEvaluateServiceStatus(uint32_t state);

LOADER_SERVICE_START_DECISION LoaderEvaluateServiceStart(uint32_t state);

int LoaderServiceTypeIsKernelDriver(uint32_t service_type);

/* SCM stop/delete is forbidden without both a successful/benign unload and the
 * canonical detached V3 status proof. A missing device never permits cleanup. */
/* Classifies only the fixed 48/72-byte V3F1 wire image. Input may be
 * unaligned and is never retained or modified. */
LOADER_LOAD_VERIFY_DECISION LoaderDecideLoadVerification(
    int query_valid, LOADER_PERSISTENT_WIRE_CLASS classification);
LOADER_PERSISTENT_WIRE_CLASS LoaderClassifyPersistentStatus(
    const void *status_bytes, size_t status_size);
LOADER_PERSISTENT_STATUS_PROOF LoaderValidateDetachedStatus(
    const void *status_bytes, size_t status_size);
/* Classifies the fixed, pointer-free 128-byte EPT status response. Input may
 * be unaligned and is never retained or modified. */
LOADER_EPT_WIRE_CLASS LoaderClassifyEptStatus(
    const void *status_bytes, size_t status_size);
int LoaderEptStatusProvesReady(
    int query_valid, LOADER_EPT_WIRE_CLASS classification);
int LoaderEptStatusProvesDetached(
    int query_valid, LOADER_EPT_WIRE_CLASS classification);
/* Classifies an exact, redacted CR3 status wire image. Roots never cross the ABI. */
LOADER_CR3_WIRE_CLASS LoaderClassifyCr3Status(
    const void *status_bytes, size_t status_size);
LOADER_OPERATOR_RUNTIME_CLASS LoaderClassifyOperatorRuntime(
    int persistent_query_valid,
    LOADER_PERSISTENT_WIRE_CLASS persistent_classification,
    int ept_query_valid,
    LOADER_EPT_WIRE_CLASS ept_classification);
LOADER_SCM_CLEANUP_POLICY LoaderDecideScmCleanup(
    int device_opened, LOADER_UNLOAD_IOCTL_RESULT ioctl_result,
    LOADER_PERSISTENT_STATUS_PROOF status_proof);
LOADER_SERVICE_ABSENCE_DECISION LoaderEvaluateServiceAbsence(
    int service_opened, uint32_t open_error);

LOADER_SERVICE_REUSE_POLICY LoaderDecideExistingServiceReuse(uint32_t state);

uint32_t LoaderServicePollDelayMs(uint32_t wait_hint_ms);

#endif /* HYPERSINV_LOADER_SERVICE_H */
