#ifndef HYPERSINV_HVR1_PROTOCOL_H
#define HYPERSINV_HVR1_PROTOCOL_H

/*
 * HVR1 is the narrow transport ABI between the Windows kernel companion and
 * the EFI resident root.  It is deliberately independent of the existing
 * driver IOCTL numbers and of the EFI runtime-control frame.  AuthTag is an
 * HMAC-SHA-256 over the boot nonce followed by the canonical mailbox header.
 * The exact EFI session ABI does not carry this mailbox to the root: the
 * LocalSystem-only companion authenticates the request before using the
 * root-owned 256-bit VM-call token on every processor.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HVR1_PROTOCOL_MAGIC UINT32_C(0x31525648) /* "HVR1" in LE */
#define HVR1_PROTOCOL_VERSION UINT16_C(1)
#define HVR1_MAILBOX_BYTES UINT32_C(65536)
#define HVR1_MAILBOX_HEADER_BYTES UINT32_C(84)
#define HVR1_MAILBOX_TAG_BYTES UINT32_C(32)
#define HVR1_AUTH_KEY_BYTES UINT32_C(32)
#define HVR1_AUTH_NONCE_BYTES UINT32_C(16)
#define HVR1_MAILBOX_ACK_SLOTS UINT32_C(4096)
#define HVR1_MAILBOX_ACK_BYTES (HVR1_MAILBOX_ACK_SLOTS * UINT32_C(4))
#define HVR1_MAILBOX_MAX_PAYLOAD_BYTES \
    (HVR1_MAILBOX_BYTES - HVR1_MAILBOX_HEADER_BYTES - HVR1_MAILBOX_ACK_BYTES)
#define HVR1_MAILBOX_ACK_OFFSET UINT32_C(49152)
#define HVR1_MAILBOX_ACK_NOT_STARTED UINT32_C(0)
#define HVR1_MAILBOX_ACK_OK UINT32_C(1)
#define HVR1_MAILBOX_ACK_FAILURE UINT32_C(2)
#define HVR1_MAILBOX_ACK_PREPARED UINT32_C(3)

/* These opcodes describe the EFI VMCALL ABI, not user-mode IOCTLs. */
#define HVR1_EFI_CALL_QUERY_SESSION UINT32_C(3)
#define HVR1_EFI_CALL_SHUTDOWN_PREPARE UINT32_C(4)
#define HVR1_EFI_CALL_SHUTDOWN_COMMIT UINT32_C(5)

/* The existing EFI bootstrap and interrupt-guard opcodes remain 1 and 2. */
#define HVR1_EFI_CALL_MAGIC UINT64_C(0x55454649564d5831) /* "UEFIVMX1" */
#define HVR1_RUNTIME_ABI_VERSION UINT64_C(1)
#define HVR1_RUNTIME_CAP_SHUTDOWN (UINT64_C(1) << 1)

typedef enum HVR1_MAILBOX_OPERATION {
    HVR1_MAILBOX_OPERATION_RUNTIME_FRAME = 1
} HVR1_MAILBOX_OPERATION;

typedef enum HVR1_IOCTL_OPERATION {
    HVR1_IOCTL_OPERATION_SHUTDOWN = 1,
    HVR1_IOCTL_OPERATION_QUERY_STATUS = 2
} HVR1_IOCTL_OPERATION;

typedef enum HVR1_POWER_EVENT {
    HVR1_POWER_EVENT_SUSPEND = 1,
    HVR1_POWER_EVENT_HIBERNATE = 2,
    HVR1_POWER_EVENT_SHUTDOWN = 3
} HVR1_POWER_EVENT;

typedef enum HVR1_COMPANION_STATE {
    HVR1_COMPANION_STATE_INITIALIZED = 0,
    HVR1_COMPANION_STATE_SESSION_READY = 1,
    HVR1_COMPANION_STATE_SHUTDOWN_PREPARING = 2,
    HVR1_COMPANION_STATE_SHUTDOWN_COMMITTING = 3,
    HVR1_COMPANION_STATE_OFFLINE = 4,
    HVR1_COMPANION_STATE_QUARANTINED = 5
} HVR1_COMPANION_STATE;

typedef enum HVR1_CPL0_STATUS {
    HVR1_CPL0_STATUS_OK = 0,
    HVR1_CPL0_STATUS_REJECTED = 1,
    HVR1_CPL0_STATUS_STALE_TOKEN = 2,
    HVR1_CPL0_STATUS_INVALID_MAILBOX = 3,
    HVR1_CPL0_STATUS_NOT_READY = 4,
    HVR1_CPL0_STATUS_ALREADY_OFFLINE = 5,
    HVR1_CPL0_STATUS_CPU_FAILURE = 6
} HVR1_CPL0_STATUS;

/* Exact register results from QUERY_RUNTIME_SESSION (opcode 3). */
typedef struct HVR1_RUNTIME_SESSION {
    uint64_t Token[4];
    uint64_t AbiVersion;
    uint64_t Capabilities;
    uint64_t Generation;
    uint64_t ProcessorIndex;
} HVR1_RUNTIME_SESSION;

/* Exact register inputs and outputs for PREPARE_NATIVE_DETACH (opcode 4). */
typedef struct HVR1_PREPARE_ARGUMENTS {
    uint64_t Token[4];
    uint64_t PivotRip;
    uint64_t PivotRsp;
    uint64_t Generation;
} HVR1_PREPARE_ARGUMENTS;

typedef struct HVR1_PREPARE_RESULT {
    uint64_t VmcsPhysical;
    uint64_t GuestCr0;
    uint64_t GuestCr4;
    uint64_t Status;
    uint64_t ProcessorIndex;
    uint64_t Generation;
    uint64_t AbiVersion;
    uint64_t Capabilities;
} HVR1_PREPARE_RESULT;

/* COMMIT inputs are loaded into the fixed registers by the MASM wrapper. */
typedef struct HVR1_COMMIT_ARGUMENTS {
    uint64_t VmcsOperand;
    uint64_t GuestCr0;
    uint64_t GuestCr4;
    uint64_t ReservedContinuationRip;
    uint64_t ReservedContinuationRsp;
    uint64_t AckAddress;
} HVR1_COMMIT_ARGUMENTS;

/* HVR1_IOCTL_* use METHOD_BUFFERED and require read/write access. */
#define HVR1_DEVICE_TYPE UINT32_C(0x00000022)
#define HVR1_IOCTL_INDEX_GET_MAILBOX UINT32_C(0x800)
#define HVR1_IOCTL_INDEX_SUBMIT UINT32_C(0x801)
#define HVR1_IOCTL_INDEX_QUERY_STATUS UINT32_C(0x802)
#define HVR1_METHOD_BUFFERED UINT32_C(0)
#define HVR1_FILE_READ_WRITE UINT32_C(3)
#define HVR1_CTL_CODE(DeviceType, Function, Method, Access) \
    (((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method))
#define HVR1_IOCTL_GET_MAILBOX \
    HVR1_CTL_CODE(HVR1_DEVICE_TYPE, HVR1_IOCTL_INDEX_GET_MAILBOX, \
                  HVR1_METHOD_BUFFERED, HVR1_FILE_READ_WRITE)
#define HVR1_IOCTL_SUBMIT \
    HVR1_CTL_CODE(HVR1_DEVICE_TYPE, HVR1_IOCTL_INDEX_SUBMIT, \
                  HVR1_METHOD_BUFFERED, HVR1_FILE_READ_WRITE)
#define HVR1_IOCTL_QUERY_STATUS \
    HVR1_CTL_CODE(HVR1_DEVICE_TYPE, HVR1_IOCTL_INDEX_QUERY_STATUS, \
                  HVR1_METHOD_BUFFERED, HVR1_FILE_READ_WRITE)

#pragma pack(push, 1)

/*
 * The mailbox is fixed-size and nonpaged in the companion.  TotalSize covers
 * HeaderSize plus PayloadSize.  AuthTag is supplied by the LocalSystem
 * service and verified in constant time by the companion before shutdown is
 * admitted.  The current shutdown operation requires an empty payload.
 */
typedef struct HVR1_MAILBOX {
    uint32_t Magic;
    uint16_t Version;
    uint16_t HeaderSize;
    uint32_t TotalSize;
    uint32_t Operation;
    uint32_t Flags;
    uint64_t Generation;
    uint64_t Sequence;
    uint32_t PayloadOffset;
    uint32_t PayloadSize;
    uint32_t Status;
    uint32_t Reserved;
    uint8_t AuthTag[HVR1_MAILBOX_TAG_BYTES];
    uint8_t Payload[HVR1_MAILBOX_MAX_PAYLOAD_BYTES];
    uint32_t AckByProcessor[HVR1_MAILBOX_ACK_SLOTS];
} HVR1_MAILBOX;

typedef struct HVR1_IOCTL_REQUEST {
    uint32_t Size;
    uint32_t Version;
    uint32_t Operation;
    uint32_t Flags;
    uint64_t ExpectedGeneration;
} HVR1_IOCTL_REQUEST;

typedef struct HVR1_MAILBOX_DESCRIPTOR {
    uint32_t Size;
    uint32_t Version;
    uint32_t MailboxBytes;
    uint32_t MailboxHeaderBytes;
    uint64_t Generation;
    uint8_t SessionKey[HVR1_AUTH_KEY_BYTES];
    uint8_t SessionNonce[HVR1_AUTH_NONCE_BYTES];
} HVR1_MAILBOX_DESCRIPTOR;

typedef struct HVR1_IOCTL_STATUS {
    uint32_t Size;
    uint32_t Version;
    uint32_t State;
    uint32_t LastStatus;
    uint64_t Generation;
    uint32_t ExpectedCpuCount;
    uint32_t AcknowledgedCpuCount;
    uint32_t CrashRecorded;
    uint32_t Reserved;
    uint32_t CrashCode;
    uint32_t CrashCpu;
    uint64_t CrashSequence;
} HVR1_IOCTL_STATUS;

#pragma pack(pop)

#define HVR1_MAILBOX_MAGIC_OFFSET 0u
#define HVR1_MAILBOX_VERSION_OFFSET 4u
#define HVR1_MAILBOX_HEADER_SIZE_OFFSET 6u
#define HVR1_MAILBOX_TOTAL_SIZE_OFFSET 8u
#define HVR1_MAILBOX_OPERATION_OFFSET 12u
#define HVR1_MAILBOX_FLAGS_OFFSET 16u
#define HVR1_MAILBOX_GENERATION_OFFSET 20u
#define HVR1_MAILBOX_SEQUENCE_OFFSET 28u
#define HVR1_MAILBOX_PAYLOAD_OFFSET_OFFSET 36u
#define HVR1_MAILBOX_PAYLOAD_SIZE_OFFSET 40u
#define HVR1_MAILBOX_STATUS_OFFSET 44u
#define HVR1_MAILBOX_RESERVED_OFFSET 48u
#define HVR1_MAILBOX_TAG_OFFSET 52u
#define HVR1_MAILBOX_PAYLOAD_DATA_OFFSET 84u
#define HVR1_MAILBOX_ACK_DATA_OFFSET 49152u

#ifdef __cplusplus
}

static_assert(sizeof(HVR1_MAILBOX) == HVR1_MAILBOX_BYTES,
              "HVR1 mailbox size drift");
static_assert(offsetof(HVR1_MAILBOX, Payload) == HVR1_MAILBOX_HEADER_BYTES,
              "HVR1 mailbox payload offset drift");
static_assert(offsetof(HVR1_MAILBOX, AckByProcessor) ==
                  HVR1_MAILBOX_ACK_OFFSET,
              "HVR1 mailbox ACK offset drift");
static_assert(sizeof(HVR1_IOCTL_REQUEST) == 24,
              "HVR1 request ABI drift");
static_assert(sizeof(HVR1_MAILBOX_DESCRIPTOR) == 72,
              "HVR1 descriptor ABI drift");
static_assert(sizeof(HVR1_IOCTL_STATUS) == 56,
              "HVR1 status ABI drift");
static_assert(sizeof(HVR1_RUNTIME_SESSION) == 64,
              "HVR1 runtime session ABI drift");
static_assert(sizeof(HVR1_PREPARE_ARGUMENTS) == 56,
              "HVR1 prepare argument ABI drift");
static_assert(sizeof(HVR1_PREPARE_RESULT) == 64,
              "HVR1 prepare result ABI drift");
static_assert(sizeof(HVR1_COMMIT_ARGUMENTS) == 48,
              "HVR1 commit argument ABI drift");
#else
typedef char hvr1_mailbox_size_assert[
    sizeof(HVR1_MAILBOX) == HVR1_MAILBOX_BYTES ? 1 : -1];
typedef char hvr1_mailbox_payload_offset_assert[
    offsetof(HVR1_MAILBOX, Payload) == HVR1_MAILBOX_HEADER_BYTES ? 1 : -1];
typedef char hvr1_mailbox_ack_offset_assert[
    offsetof(HVR1_MAILBOX, AckByProcessor) == HVR1_MAILBOX_ACK_OFFSET
        ? 1 : -1];
typedef char hvr1_runtime_session_size_assert[
    sizeof(HVR1_RUNTIME_SESSION) == 64 ? 1 : -1];
typedef char hvr1_prepare_arguments_size_assert[
    sizeof(HVR1_PREPARE_ARGUMENTS) == 56 ? 1 : -1];
typedef char hvr1_prepare_result_size_assert[
    sizeof(HVR1_PREPARE_RESULT) == 64 ? 1 : -1];
typedef char hvr1_commit_arguments_size_assert[
    sizeof(HVR1_COMMIT_ARGUMENTS) == 48 ? 1 : -1];
#endif

#endif /* HYPERSINV_HVR1_PROTOCOL_H */
