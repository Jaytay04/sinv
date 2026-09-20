#ifndef HYPERSINV_EFI_RUNTIME_PROCESS_ABI_H
#define HYPERSINV_EFI_RUNTIME_PROCESS_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HV_EFI_RUNTIME_PROCESS_MAGIC UINT32_C(0x31505648) /* "HVP1" */
#define HV_EFI_RUNTIME_PROCESS_VERSION UINT16_C(1)
#define HV_EFI_RUNTIME_PROCESS_NAME_BYTES UINT32_C(16)
#define HV_EFI_RUNTIME_PROCESS_MAX_PRIVATE_RECORDS UINT32_C(512)

typedef enum HV_EFI_RUNTIME_PROCESS_STATUS {
    HV_EFI_RUNTIME_PROCESS_SUCCESS = 0,
    HV_EFI_RUNTIME_PROCESS_NOT_FOUND = 1,
    HV_EFI_RUNTIME_PROCESS_INVALID_REQUEST = 2,
    HV_EFI_RUNTIME_PROCESS_BUSY = 3,
    HV_EFI_RUNTIME_PROCESS_REPLAY_REJECTED = 4,
    HV_EFI_RUNTIME_PROCESS_PROFILE_UNAVAILABLE = 5,
    HV_EFI_RUNTIME_PROCESS_PROFILE_MISMATCH = 6,
    HV_EFI_RUNTIME_PROCESS_TRANSLATION_FAILED = 7,
    HV_EFI_RUNTIME_PROCESS_CORRUPT_LIST = 8,
    HV_EFI_RUNTIME_PROCESS_CYCLE_DETECTED = 9,
    HV_EFI_RUNTIME_PROCESS_ITERATION_LIMIT = 10,
    HV_EFI_RUNTIME_PROCESS_PHYSICAL_RANGE = 11,
    HV_EFI_RUNTIME_PROCESS_MAPPING_FAILED = 12,
    HV_EFI_RUNTIME_PROCESS_INVARIANT_VIOLATION = 13
} HV_EFI_RUNTIME_PROCESS_STATUS;

typedef enum HV_EFI_RUNTIME_PROCESS_MAILBOX_STATE {
    HV_EFI_RUNTIME_PROCESS_MAILBOX_EMPTY = 0,
    HV_EFI_RUNTIME_PROCESS_MAILBOX_REQUEST_READY = 1,
    HV_EFI_RUNTIME_PROCESS_MAILBOX_PROCESSING = 2,
    HV_EFI_RUNTIME_PROCESS_MAILBOX_RESPONSE_READY = 3
} HV_EFI_RUNTIME_PROCESS_MAILBOX_STATE;

#define HV_EFI_RUNTIME_PROCESS_RESPONSE_TRUNCATED UINT32_C(1)

#pragma pack(push, 1)
typedef struct HV_EFI_RUNTIME_PROCESS_REQUEST {
    uint32_t Size;
    uint16_t Version;
    uint16_t NameLength;
    uint64_t Sequence;
    uint64_t KernelBase;
    uint32_t BuildNumber;
    uint32_t Ubr;
    uint32_t Flags;
    uint32_t Reserved;
    char ImageFileName[HV_EFI_RUNTIME_PROCESS_NAME_BYTES];
} HV_EFI_RUNTIME_PROCESS_REQUEST;

typedef struct HV_EFI_RUNTIME_PROCESS_RESPONSE {
    uint32_t Size;
    uint16_t Version;
    uint16_t Reserved16;
    uint32_t Status;
    uint32_t Flags;
    uint64_t Generation;
    uint64_t Sequence;
    uint64_t ProcessId;
    uint64_t DirectoryTableBase;
    uint64_t EprocessVirtualAddress;
    uint32_t EnumeratedCount;
    uint32_t StoredCount;
} HV_EFI_RUNTIME_PROCESS_RESPONSE;

typedef struct HV_EFI_RUNTIME_PROCESS_MAILBOX {
    uint32_t Magic;
    uint16_t Version;
    uint16_t HeaderSize;
    volatile uint32_t State;
    uint32_t Reserved;
    uint64_t Generation;
    HV_EFI_RUNTIME_PROCESS_REQUEST Request;
    HV_EFI_RUNTIME_PROCESS_RESPONSE Response;
} HV_EFI_RUNTIME_PROCESS_MAILBOX;
#pragma pack(pop)

#ifdef __cplusplus
}
static_assert(sizeof(HV_EFI_RUNTIME_PROCESS_REQUEST) == 56,
              "runtime process request ABI drift");
static_assert(sizeof(HV_EFI_RUNTIME_PROCESS_RESPONSE) == 64,
              "runtime process response ABI drift");
static_assert(sizeof(HV_EFI_RUNTIME_PROCESS_MAILBOX) == 144,
              "runtime process mailbox ABI drift");
#else
typedef char hv_efi_runtime_process_request_size[
    sizeof(HV_EFI_RUNTIME_PROCESS_REQUEST) == 56 ? 1 : -1];
typedef char hv_efi_runtime_process_response_size[
    sizeof(HV_EFI_RUNTIME_PROCESS_RESPONSE) == 64 ? 1 : -1];
typedef char hv_efi_runtime_process_mailbox_size[
    sizeof(HV_EFI_RUNTIME_PROCESS_MAILBOX) == 144 ? 1 : -1];
#endif

#endif /* HYPERSINV_EFI_RUNTIME_PROCESS_ABI_H */
