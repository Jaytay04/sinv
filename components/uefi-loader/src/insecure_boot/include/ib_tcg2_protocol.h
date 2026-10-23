/*
 * EFI_TCG2_PROTOCOL definitions, reduced to what the insecure-boot port uses.
 *
 * Field order and packing follow the TCG EFI Protocol Specification: the
 * capability structure is a byte-packed, length-prefixed record and the event
 * structure opens with its own total length.
 */
#ifndef IB_TCG2_PROTOCOL_H
#define IB_TCG2_PROTOCOL_H

#include "ib_uefi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef ib_u16 ib_tpm_alg_id;

#define IB_TPM_ALG_ERROR     0x0000u
#define IB_TPM_ALG_RSA       0x0001u
#define IB_TPM_ALG_SHA1      0x0004u
#define IB_TPM_ALG_HMAC      0x0005u
#define IB_TPM_ALG_AES       0x0006u
#define IB_TPM_ALG_KEYEDHASH 0x0008u
#define IB_TPM_ALG_XOR       0x000Au
#define IB_TPM_ALG_SHA256    0x000Bu
#define IB_TPM_ALG_SHA384    0x000Cu
#define IB_TPM_ALG_SHA512    0x000Du
#define IB_TPM_ALG_NULL      0x0010u
#define IB_TPM_ALG_SM3_256   0x0012u

typedef ib_u32 ib_efi_tcg2_event_algorithm_bitmap;

#define IB_EFI_TCG2_BOOT_HASH_ALG_SHA1    0x00000001u
#define IB_EFI_TCG2_BOOT_HASH_ALG_SHA256  0x00000002u
#define IB_EFI_TCG2_BOOT_HASH_ALG_SHA384  0x00000004u
#define IB_EFI_TCG2_BOOT_HASH_ALG_SHA512  0x00000008u
#define IB_EFI_TCG2_BOOT_HASH_ALG_SM3_256 0x00000010u

static const ib_efi_guid_t ib_guid_tcg2_protocol = {
    0x607f766c, 0x7455, 0x42be,
    { 0x93, 0x0b, 0xe4, 0xd7, 0x6d, 0xb2, 0x72, 0x0f }
};

static const ib_efi_guid_t ib_guid_tcg_protocol = {
    0xf8ed6d1a, 0x97f0, 0x4e1c,
    { 0x9b, 0x2c, 0x2f, 0x0a, 0x4b, 0x8f, 0x5a, 0x3a }
};

typedef enum {
    IbTcg2EventLogFormatTcg12 = 1,
    IbTcg2EventLogFormatTcg2 = 2
} ib_efi_tcg2_event_log_format;

#define IB_EFI_TCG2_EXTEND_ONLY   0x0000000000000001ULL
#define IB_EFI_TCG2_PE_COFF_IMAGE 0x0000000000000010ULL

typedef struct {
    ib_u8 major;
    ib_u8 minor;
} ib_efi_tcg2_version;

#pragma pack(push, 1)
typedef struct {
    ib_u8  size;
    ib_efi_tcg2_version structure_version;
    ib_efi_tcg2_version protocol_version;
    ib_u32 hash_algorithm_bitmap;
    ib_u32 supported_event_logs;
    ib_boolean tpm_present_flag;
    ib_u16 max_command_size;
    ib_u16 max_response_size;
    ib_u32 manufacturer_id;
    ib_u32 number_of_pcr_banks;
    ib_u32 active_pcr_banks;
} ib_efi_tcg2_boot_service_capability;

typedef struct {
    ib_u32 header_size;
    ib_u16 header_version;
    ib_u32 pcr_index;
    ib_u32 event_type;
} ib_efi_tcg2_event_header;

typedef struct {
    ib_u32 size;
    ib_efi_tcg2_event_header header;
    ib_u8 event[1];
} ib_efi_tcg2_event;
#pragma pack(pop)

typedef struct ib_efi_tcg2_protocol ib_efi_tcg2_protocol;

typedef ib_efi_status (IB_EFIAPI *ib_efi_tcg2_get_capability)(
    ib_efi_tcg2_protocol *self,
    ib_efi_tcg2_boot_service_capability *protocol_capability);
typedef ib_efi_status (IB_EFIAPI *ib_efi_tcg2_get_event_log)(
    ib_efi_tcg2_protocol *self,
    ib_efi_tcg2_event_log_format event_log_format,
    ib_efi_physical_address *event_log_location,
    ib_efi_physical_address *event_log_last_entry,
    ib_boolean *event_log_truncated);
typedef ib_efi_status (IB_EFIAPI *ib_efi_tcg2_hash_log_extend_event)(
    ib_efi_tcg2_protocol *self,
    ib_u64 flags,
    ib_efi_physical_address data_to_hash,
    ib_u64 data_to_hash_len,
    ib_efi_tcg2_event *event);
typedef ib_efi_status (IB_EFIAPI *ib_efi_tcg2_submit_command)(
    ib_efi_tcg2_protocol *self,
    ib_u32 input_parameter_block_size,
    ib_u8 *input_parameter_block,
    ib_u32 output_parameter_block_size,
    ib_u8 *output_parameter_block);
typedef ib_efi_status (IB_EFIAPI *ib_efi_tcg2_get_active_pcr_banks)(
    ib_efi_tcg2_protocol *self,
    ib_efi_tcg2_event_algorithm_bitmap *active_pcr_banks);
typedef ib_efi_status (IB_EFIAPI *ib_efi_tcg2_set_active_pcr_banks)(
    ib_efi_tcg2_protocol *self,
    ib_efi_tcg2_event_algorithm_bitmap active_pcr_banks);
typedef ib_efi_status (IB_EFIAPI *ib_efi_tcg2_get_result_of_set_active_pcr_banks)(
    ib_efi_tcg2_protocol *self,
    ib_u32 *operation_present,
    ib_u32 *response);

struct ib_efi_tcg2_protocol {
    ib_efi_tcg2_get_capability                    get_capability;
    ib_efi_tcg2_get_event_log                     get_event_log;
    ib_efi_tcg2_hash_log_extend_event             hash_log_extend_event;
    ib_efi_tcg2_submit_command                    submit_command;
    ib_efi_tcg2_get_active_pcr_banks              get_active_pcr_banks;
    ib_efi_tcg2_set_active_pcr_banks              set_active_pcr_banks;
    ib_efi_tcg2_get_result_of_set_active_pcr_banks get_result_of_set_active_pcr_banks;
};

#ifdef __cplusplus
}
#endif

#endif /* IB_TCG2_PROTOCOL_H */
