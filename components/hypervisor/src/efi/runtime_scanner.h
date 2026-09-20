#ifndef HYPERSINV_EFI_RUNTIME_SCANNER_H
#define HYPERSINV_EFI_RUNTIME_SCANNER_H

/*
 * Bounded runtime command adapter for the read-only physical scanner.
 *
 * The authenticated HVR1 companion places one request in its physically
 * contiguous mailbox and triggers the command with the existing VMCALL exit.
 * This adapter does not authenticate the outer request; the companion HMAC
 * and the processor-local EFI runtime token are verified before entry.
 *
 * Scanned RAM is exposed only through scanner.h's read-only, NX host window.
 * The mailbox is mapped writable only while the fixed response is copied
 * back.  No guest EPT permission is changed: the mailbox is ordinary guest
 * RAM, while all hypervisor-owned scanner state remains absent from guest EPT.
 */

#include <stddef.h>
#include <stdint.h>

#include "scanner.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HV_EFI_RUNTIME_SCAN_MAGIC UINT32_C(0x31535648) /* "HVS1" */
#define HV_EFI_RUNTIME_SCAN_VERSION UINT16_C(1)
#define HV_EFI_RUNTIME_SCAN_MAX_PATTERN_BYTES UINT32_C(64)
#define HV_EFI_RUNTIME_SCAN_MAX_RESULTS UINT32_C(128)
#define HV_EFI_RUNTIME_SCAN_DEFAULT_BUDGET_BYTES UINT64_C(0x04000000)
#define HV_EFI_RUNTIME_SCAN_MAX_BUDGET_BYTES UINT64_C(0x10000000)
#define HV_EFI_RUNTIME_SCAN_STATE_MARKER UINT64_C(0x48565343414e3031)

typedef enum HV_EFI_RUNTIME_SCAN_CALLBACK_TYPE {
    /* Return one address and stop at the first match. */
    HV_EFI_RUNTIME_SCAN_CALLBACK_FIRST_MATCH = 1,
    /* Require exactly one match in the bounded invocation. */
    HV_EFI_RUNTIME_SCAN_CALLBACK_SINGLE = 2,
    /* Return every match up to MaxResults. */
    HV_EFI_RUNTIME_SCAN_CALLBACK_ALL = 3
} HV_EFI_RUNTIME_SCAN_CALLBACK_TYPE;

typedef enum HV_EFI_RUNTIME_SCAN_STATUS {
    HV_EFI_RUNTIME_SCAN_SUCCESS = 0,
    HV_EFI_RUNTIME_SCAN_NOT_FOUND = 1,
    HV_EFI_RUNTIME_SCAN_MORE_DATA = 2,
    HV_EFI_RUNTIME_SCAN_INVALID_REQUEST = 3,
    HV_EFI_RUNTIME_SCAN_BUSY = 4,
    HV_EFI_RUNTIME_SCAN_REPLAY_REJECTED = 5,
    HV_EFI_RUNTIME_SCAN_GENERATION_MISMATCH = 6,
    HV_EFI_RUNTIME_SCAN_RANGE_DENIED = 7,
    HV_EFI_RUNTIME_SCAN_RESULT_CAPACITY = 8,
    HV_EFI_RUNTIME_SCAN_MULTIPLE_MATCHES = 9,
    HV_EFI_RUNTIME_SCAN_MAPPING_FAILED = 10,
    HV_EFI_RUNTIME_SCAN_SCANNER_FAILED = 11,
    HV_EFI_RUNTIME_SCAN_INVARIANT_VIOLATION = 12
} HV_EFI_RUNTIME_SCAN_STATUS;

typedef enum HV_EFI_RUNTIME_SCAN_MAILBOX_STATE {
    HV_EFI_RUNTIME_SCAN_MAILBOX_EMPTY = 0,
    HV_EFI_RUNTIME_SCAN_MAILBOX_REQUEST_READY = 1,
    HV_EFI_RUNTIME_SCAN_MAILBOX_PROCESSING = 2,
    HV_EFI_RUNTIME_SCAN_MAILBOX_RESPONSE_READY = 3
} HV_EFI_RUNTIME_SCAN_MAILBOX_STATE;

#pragma pack(push, 1)

/* WildcardMask bit N marks Pattern[N] as a wildcard. */
typedef struct HV_EFI_RUNTIME_SCAN_REQUEST {
    uint32_t Size;
    uint16_t Version;
    uint16_t CallbackType;
    uint32_t PatternLength;
    uint32_t MaxResults;
    uint64_t StartAddress;
    uint64_t EndAddress;
    uint64_t WildcardMask;
    uint64_t MaxBytesToScan;
    uint64_t Sequence;
    uint8_t Pattern[HV_EFI_RUNTIME_SCAN_MAX_PATTERN_BYTES];
} HV_EFI_RUNTIME_SCAN_REQUEST;

typedef struct HV_EFI_RUNTIME_SCAN_RESULT {
    uint64_t PhysicalAddress;
    uint32_t Status;
    uint32_t Reserved;
} HV_EFI_RUNTIME_SCAN_RESULT;

typedef struct HV_EFI_RUNTIME_SCAN_RESPONSE {
    uint32_t Size;
    uint16_t Version;
    uint16_t Reserved16;
    uint32_t Status;
    uint32_t ResultCount;
    uint64_t Generation;
    uint64_t Sequence;
    uint64_t ScannedBytes;
    uint64_t NextAddress;
    uint32_t Reserved32;
    uint32_t ResultCapacity;
    HV_EFI_RUNTIME_SCAN_RESULT Results[HV_EFI_RUNTIME_SCAN_MAX_RESULTS];
} HV_EFI_RUNTIME_SCAN_RESPONSE;

/* This structure occupies the beginning of the authenticated HVR1 payload. */
typedef struct HV_EFI_RUNTIME_SCAN_MAILBOX {
    uint32_t Magic;
    uint16_t Version;
    uint16_t HeaderSize;
    volatile uint32_t State;
    uint32_t Reserved;
    uint64_t Generation;
    HV_EFI_RUNTIME_SCAN_REQUEST Request;
    HV_EFI_RUNTIME_SCAN_RESPONSE Response;
} HV_EFI_RUNTIME_SCAN_MAILBOX;

#pragma pack(pop)

/* One shared, guest-inaccessible runtime page; Busy serializes all vCPUs. */
typedef struct HV_EFI_RUNTIME_SCAN_STATE {
    volatile uint32_t Busy;
    uint32_t Reserved;
    uint64_t LastSequence;
    uint64_t Marker;
    HV_EFI_RUNTIME_SCAN_RESULT Results[HV_EFI_RUNTIME_SCAN_MAX_RESULTS];
} HV_EFI_RUNTIME_SCAN_STATE;

/* Host/guest construction helper. Authentication remains an HVR1 concern. */
HV_EFI_RUNTIME_SCAN_STATUS HvEfiRuntimeScannerWriteRequest(
    HV_EFI_RUNTIME_SCAN_MAILBOX *mailbox,
    size_t mailboxCapacity,
    uint64_t generation,
    const HV_EFI_RUNTIME_SCAN_REQUEST *request);

/* Stable response reader used after State becomes RESPONSE_READY. */
HV_EFI_RUNTIME_SCAN_STATUS HvEfiRuntimeScannerReadResponse(
    const HV_EFI_RUNTIME_SCAN_MAILBOX *mailbox,
    size_t mailboxCapacity,
    uint64_t expectedGeneration,
    uint64_t expectedSequence,
    HV_EFI_RUNTIME_SCAN_RESPONSE *response);

/*
 * Executes one already-authenticated request against a caller-owned window.
 * The function is allocation-free and uses the existing scanner functions.
 */
HV_EFI_RUNTIME_SCAN_STATUS HvEfiRuntimeScannerExecute(
    HV_EFI_RUNTIME_SCAN_STATE *state,
    HV_EFI_SCANNER_WINDOW *window,
    const HV_EFI_SCANNER_RAM_RANGE *allowedRanges,
    uint32_t allowedRangeCount,
    uint64_t generation,
    const HV_EFI_RUNTIME_SCAN_REQUEST *request,
    HV_EFI_RUNTIME_SCAN_RESPONSE *response);

/*
 * Copied-runtime entry used by the resident VMCALL route. It maps only the
 * fixed scan payload within the HVR1 mailbox and returns a status in RAX.
 */
struct HV_EFI_CONTEXT;
uint64_t HvEfiRuntimeScannerResidentCommand(
    struct HV_EFI_CONTEXT *context,
    uint64_t mailboxPhysical,
    uint64_t mailboxBytes,
    uint64_t expectedSequence);

/* Linker-section bounds copied into EfiRuntimeServicesCode before VM launch. */
uint32_t HvEfiRuntimeScannerResidentStart(void);
uint32_t HvEfiRuntimeScannerResidentEnd(void);

#ifdef __cplusplus
}

static_assert(sizeof(HV_EFI_RUNTIME_SCAN_REQUEST) == 120,
              "runtime scan request ABI drift");
static_assert(sizeof(HV_EFI_RUNTIME_SCAN_RESULT) == 16,
              "runtime scan result ABI drift");
static_assert(sizeof(HV_EFI_RUNTIME_SCAN_RESPONSE) == 2104,
              "runtime scan response ABI drift");
static_assert(sizeof(HV_EFI_RUNTIME_SCAN_MAILBOX) == 2248,
              "runtime scan mailbox ABI drift");
static_assert(sizeof(HV_EFI_RUNTIME_SCAN_STATE) <= 4096,
              "runtime scan state must fit in one page");
#endif

#endif /* HYPERSINV_EFI_RUNTIME_SCANNER_H */
