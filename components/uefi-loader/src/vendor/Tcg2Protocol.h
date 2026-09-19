/*
 * Tcg2Protocol.h — Minimal vendored EFI TCG2 protocol definitions.
 *
 * Reduced subset of the EFI TCG2 Protocol (UEFI 2.x + TCG EFI Protocol
 * Specification) sufficient for P2 (compile/link) and extended later by the
 * loader (P4) and ib-tcg-dump (P3). Includes the TPM algorithm identifiers
 * shared conceptually with src/common/tcglog.h.
 */
#ifndef TCG2_PROTOCOL_H
#define TCG2_PROTOCOL_H

#include "Uefi.h"

/* ------------------------------------------------------------------------- */
/* TPM algorithm identifiers (TPM_ALG_ID)                                    */
/* ------------------------------------------------------------------------- */
typedef UINT16 TPM_ALG_ID;

#define TPM_ALG_ERROR     0x0000u
#define TPM_ALG_RSA       0x0001u
#define TPM_ALG_SHA1      0x0004u
#define TPM_ALG_HMAC      0x0005u
#define TPM_ALG_AES       0x0006u
#define TPM_ALG_KEYEDHASH 0x0008u
#define TPM_ALG_XOR       0x000Au
#define TPM_ALG_SHA256    0x000Bu
#define TPM_ALG_SHA384    0x000Cu
#define TPM_ALG_SHA512    0x000Du
#define TPM_ALG_NULL      0x0010u
#define TPM_ALG_SM3_256   0x0012u

/* PCR-bank bitmap (EFI_TCG2_EVENT_ALGORITHM_BITMAP). */
typedef UINT32 EFI_TCG2_EVENT_ALGORITHM_BITMAP;
#define EFI_TCG2_BOOT_HASH_ALG_SHA1    0x00000001u
#define EFI_TCG2_BOOT_HASH_ALG_SHA256  0x00000002u
#define EFI_TCG2_BOOT_HASH_ALG_SHA384  0x00000004u
#define EFI_TCG2_BOOT_HASH_ALG_SHA512  0x00000008u
#define EFI_TCG2_BOOT_HASH_ALG_SM3_256 0x00000010u

/* ------------------------------------------------------------------------- */
/* Protocol GUID                                                              */
/* ------------------------------------------------------------------------- */
#define EFI_TCG2_PROTOCOL_GUID \
  { 0x607f766c, 0x7455, 0x42be, { 0x93, 0x0b, 0xe4, 0xd7, 0x6d, 0xb2, 0x72, 0x0f } }

/* ------------------------------------------------------------------------- */
/* Event log format                                                           */
/* ------------------------------------------------------------------------- */
typedef enum {
    EFI_TCG2_EVENT_LOG_FORMAT_TCG_1_2 = 1, /* SHA-1 ("Spec ID Event00") */
    EFI_TCG2_EVENT_LOG_FORMAT_TCG_2    = 2  /* crypto-agile ("Spec ID Event03") */
} EFI_TCG2_EVENT_LOG_FORMAT;

/* HashLogExtendEvent flags from the TCG EFI protocol. */
#define EFI_TCG2_EXTEND_ONLY   0x0000000000000001ULL
#define EFI_TCG2_PE_COFF_IMAGE 0x0000000000000010ULL

/* ------------------------------------------------------------------------- */
/* Capability / event structures                                              */
/* ------------------------------------------------------------------------- */
typedef struct {
    UINT8 Major;
    UINT8 Minor;
} EFI_TCG2_VERSION;

typedef struct {
    UINT8  Size;
    EFI_TCG2_VERSION StructureVersion;
    EFI_TCG2_VERSION ProtocolVersion;
    UINT32 HashAlgorithmBitmap;
    UINT32 SupportedEventLogs;
    BOOLEAN TPMPresentFlag;
    UINT16 MaxCommandSize;
    UINT16 MaxResponseSize;
    UINT32 ManufacturerID;
    UINT32 NumberOfPcrBanks;
    UINT32 ActivePcrBanks;
} EFI_TCG2_BOOT_SERVICE_CAPABILITY;

#pragma pack(push, 1)
typedef struct {
    UINT32 HeaderSize;
    UINT16 HeaderVersion;
    UINT32 PCRIndex;
    UINT32 EventType;
} EFI_TCG2_EVENT_HEADER;

typedef struct {
    UINT32 Size;
    EFI_TCG2_EVENT_HEADER Header;
    UINT8 Event[1];
} EFI_TCG2_EVENT;
#pragma pack(pop)

/* ------------------------------------------------------------------------- */
/* EFI_TCG2_PROTOCOL                                                          */
/* ------------------------------------------------------------------------- */
typedef struct _EFI_TCG2_PROTOCOL EFI_TCG2_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_TCG2_GET_CAPABILITY)(
    EFI_TCG2_PROTOCOL *This,
    EFI_TCG2_BOOT_SERVICE_CAPABILITY *ProtocolCapability);

typedef EFI_STATUS (EFIAPI *EFI_TCG2_GET_EVENT_LOG)(
    EFI_TCG2_PROTOCOL *This,
    EFI_TCG2_EVENT_LOG_FORMAT EventLogFormat,
    EFI_PHYSICAL_ADDRESS *EventLogLocation,
    EFI_PHYSICAL_ADDRESS *EventLogLastEntry,
    BOOLEAN *EventLogTruncated);

typedef EFI_STATUS (EFIAPI *EFI_TCG2_HASH_LOG_EXTEND_EVENT)(
    EFI_TCG2_PROTOCOL *This,
    UINT64 Flags,
    EFI_PHYSICAL_ADDRESS DataToHash,
    UINT64 DataToHashLen,
    EFI_TCG2_EVENT *EfiTcgEvent);

typedef EFI_STATUS (EFIAPI *EFI_TCG2_SUBMIT_COMMAND)(
    EFI_TCG2_PROTOCOL *This,
    UINT32 InputParameterBlockSize,
    UINT8 *InputParameterBlock,
    UINT32 OutputParameterBlockSize,
    UINT8 *OutputParameterBlock);

typedef EFI_STATUS (EFIAPI *EFI_TCG2_GET_ACTIVE_PCR_BANKS)(
    EFI_TCG2_PROTOCOL *This,
    UINT32 *ActivePcrBanks);

typedef EFI_STATUS (EFIAPI *EFI_TCG2_SET_ACTIVE_PCR_BANKS)(
    EFI_TCG2_PROTOCOL *This,
    UINT32 ActivePcrBanks);

typedef EFI_STATUS (EFIAPI *EFI_TCG2_GET_RESULT_OF_SET_ACTIVE_PCR_BANKS)(
    EFI_TCG2_PROTOCOL *This,
    UINT32 *OperationPresent,
    UINT32 *Response);

struct _EFI_TCG2_PROTOCOL {
    EFI_TCG2_GET_CAPABILITY                      GetCapability;
    EFI_TCG2_GET_EVENT_LOG                       GetEventLog;
    EFI_TCG2_HASH_LOG_EXTEND_EVENT               HashLogExtendEvent;
    EFI_TCG2_SUBMIT_COMMAND                      SubmitCommand;
    EFI_TCG2_GET_ACTIVE_PCR_BANKS                GetActivePcrBanks;
    EFI_TCG2_SET_ACTIVE_PCR_BANKS                SetActivePcrBanks;
    EFI_TCG2_GET_RESULT_OF_SET_ACTIVE_PCR_BANKS  GetResultOfSetActivePcrBanks;
};

#endif /* TCG2_PROTOCOL_H */
