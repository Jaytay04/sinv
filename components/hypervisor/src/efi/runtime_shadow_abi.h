#ifndef HYPERSINV_EFI_RUNTIME_SHADOW_ABI_H
#define HYPERSINV_EFI_RUNTIME_SHADOW_ABI_H

#include <stddef.h>
#include <stdint.h>

#include "shadow_ept.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HV_EFI_RUNTIME_SHADOW_MAGIC UINT32_C(0x31534856) /* "VHS1" */
#define HV_EFI_RUNTIME_SHADOW_VERSION UINT16_C(1)

typedef enum HV_EFI_RUNTIME_SHADOW_OPERATION {
    HV_EFI_RUNTIME_SHADOW_BIND = 1,
    HV_EFI_RUNTIME_SHADOW_UNBIND = 2,
    HV_EFI_RUNTIME_SHADOW_QUERY_STATUS = 3
} HV_EFI_RUNTIME_SHADOW_OPERATION;

typedef enum HV_EFI_RUNTIME_SHADOW_STATUS {
    HV_EFI_RUNTIME_SHADOW_SUCCESS = 0,
    HV_EFI_RUNTIME_SHADOW_INVALID_REQUEST = 1,
    HV_EFI_RUNTIME_SHADOW_BUSY = 2,
    HV_EFI_RUNTIME_SHADOW_REPLAY_REJECTED = 3,
    HV_EFI_RUNTIME_SHADOW_PROCESS_NOT_CACHED = 4,
    HV_EFI_RUNTIME_SHADOW_KERNEL_REJECTED = 5,
    HV_EFI_RUNTIME_SHADOW_PHYSICAL_RANGE = 6,
    HV_EFI_RUNTIME_SHADOW_PRIVATE_RANGE = 7,
    HV_EFI_RUNTIME_SHADOW_CAPACITY = 8,
    HV_EFI_RUNTIME_SHADOW_INVARIANT = 9,
    HV_EFI_RUNTIME_SHADOW_MAPPING_FAILED = 10,
    HV_EFI_RUNTIME_SHADOW_FAILED_SAFE = 11,
    /* Internal transport diagnostics. They are never published as a valid
     * mailbox response; the companion records them if a CPU cannot publish. */
    HV_EFI_RUNTIME_SHADOW_WINDOW_INIT_FAILED = 12,
    HV_EFI_RUNTIME_SHADOW_REQUEST_MAP_FAILED = 13,
    HV_EFI_RUNTIME_SHADOW_REQUEST_UNMAP_FAILED = 14,
    HV_EFI_RUNTIME_SHADOW_RESPONSE_MAP_FAILED = 15,
    HV_EFI_RUNTIME_SHADOW_RESPONSE_UNMAP_FAILED = 16,
    HV_EFI_RUNTIME_SHADOW_CALL_ARGUMENT_INVALID = 17,
    HV_EFI_RUNTIME_SHADOW_CONTEXT_INVALID = 18,
    HV_EFI_RUNTIME_SHADOW_PAYLOAD_INVALID = 19,
    HV_EFI_RUNTIME_SHADOW_ENVELOPE_INVALID = 20
} HV_EFI_RUNTIME_SHADOW_STATUS;

typedef enum HV_EFI_RUNTIME_SHADOW_MAILBOX_STATE {
    HV_EFI_RUNTIME_SHADOW_MAILBOX_EMPTY = 0,
    HV_EFI_RUNTIME_SHADOW_MAILBOX_REQUEST_READY = 1,
    HV_EFI_RUNTIME_SHADOW_MAILBOX_PROCESSING = 2,
    HV_EFI_RUNTIME_SHADOW_MAILBOX_RESPONSE_READY = 3
} HV_EFI_RUNTIME_SHADOW_MAILBOX_STATE;

#pragma pack(push, 1)
typedef struct HV_EFI_RUNTIME_SHADOW_REQUEST {
    uint32_t Size;
    uint16_t Version;
    uint16_t Operation;
    uint64_t Sequence;
    uint64_t ProcessId;
    uint32_t RangeCount;
    uint32_t Flags;
    HV_EFI_SHADOW_EPT_RANGE Ranges[HV_EFI_SHADOW_EPT_MAX_RANGES];
} HV_EFI_RUNTIME_SHADOW_REQUEST;

typedef struct HV_EFI_RUNTIME_SHADOW_RESPONSE {
    uint32_t Size;
    uint16_t Version;
    uint16_t Reserved16;
    uint32_t Status;
    uint32_t State;
    uint64_t Generation;
    uint64_t Sequence;
    uint64_t ProcessId;
    uint32_t RangeCount;
    uint32_t Active;
    uint32_t UsedTablePages;
    uint32_t PendingSynchronization;
    uint64_t ViolationCount;
    uint64_t WriteRedirectCount;
    uint64_t SynchronizationCount;
} HV_EFI_RUNTIME_SHADOW_RESPONSE;

typedef struct HV_EFI_RUNTIME_SHADOW_MAILBOX {
    uint32_t Magic;
    uint16_t Version;
    uint16_t HeaderSize;
    volatile uint32_t State;
    uint32_t Reserved;
    uint64_t Generation;
    HV_EFI_RUNTIME_SHADOW_REQUEST Request;
    HV_EFI_RUNTIME_SHADOW_RESPONSE Response;
} HV_EFI_RUNTIME_SHADOW_MAILBOX;
#pragma pack(pop)

#ifdef __cplusplus
}
static_assert(sizeof(HV_EFI_RUNTIME_SHADOW_REQUEST) == 544,
              "runtime shadow request ABI drift");
static_assert(sizeof(HV_EFI_RUNTIME_SHADOW_RESPONSE) == 80,
              "runtime shadow response ABI drift");
static_assert(sizeof(HV_EFI_RUNTIME_SHADOW_MAILBOX) == 648,
              "runtime shadow mailbox ABI drift");
#endif

#endif /* HYPERSINV_EFI_RUNTIME_SHADOW_ABI_H */
