/*
 * TPM 2.0 over the TCG Command Response Buffer interface.
 *
 * A port of Linux drivers/char/tpm/tpm_crb.c.  Register layout, flag bits,
 * access order and firmware workarounds are preserved; only the plumbing is
 * adapted to UEFI (identity-mapped physical addresses, boot-services time, and
 * _DSM through the ib_uacpi surface).
 */
#ifndef IB_TPM_CRB_H
#define IB_TPM_CRB_H

#include <stddef.h>
#include <stdint.h>

#include "ib_uacpi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IB_TPM_CRB_TPM2_SIGNATURE "TPM2"
#define IB_TPM_CRB_HID            "MSFT0101"

#define IB_TPM_CRB_CONTROL_ADDRESS 40u
#define IB_TPM_CRB_START_METHOD_AT 48u
#define IB_TPM_CRB_TABLE_FIXED_LEN 52u
#define IB_TPM_CRB_SMC_LEN         12u
#define IB_TPM_CRB_FFA_LEN         12u
#define IB_TPM_CRB_PLUTON_LEN      16u

#define IB_TPM_CRB_HEAD_LEN 0x40u
#define IB_TPM_CRB_TAIL_LEN 0x30u

#define IB_TPM_CRB_MAX_RESOURCES 3u

/* Start method selectors, as stored in the TPM2 table. */
#define IB_TPM_CRB_START_ACPI_START                  2u
#define IB_TPM_CRB_START_MEMORY_MAPPED               6u
#define IB_TPM_CRB_START_COMMAND_BUFFER              7u
#define IB_TPM_CRB_START_COMMAND_BUFFER_WITH_ACPI    8u
#define IB_TPM_CRB_START_COMMAND_BUFFER_WITH_ARM_SMC 11u
#define IB_TPM_CRB_START_COMMAND_BUFFER_WITH_PLUTON  13u
#define IB_TPM_CRB_START_CRB_WITH_ARM_FFA            15u

typedef struct {
    uint32_t raw;
} ib_tpm_crb_start_method_t;

ib_tpm_crb_start_method_t ib_tpm_crb_start_method_from_raw(uint32_t raw);
uint32_t ib_tpm_crb_start_method_raw(ib_tpm_crb_start_method_t method);
const char *ib_tpm_crb_start_method_name(ib_tpm_crb_start_method_t method);
void ib_tpm_crb_start_method_display(ib_tpm_crb_start_method_t method,
                                     char *buffer, size_t capacity);
int ib_tpm_crb_start_method_has_idle(ib_tpm_crb_start_method_t method);
int ib_tpm_crb_start_method_uses_start_register(ib_tpm_crb_start_method_t method);
int ib_tpm_crb_start_method_uses_acpi_start(ib_tpm_crb_start_method_t method);

/* Error detail shared by every operation. */
typedef enum {
    IB_TPM_CRB_OK = 0,
    IB_TPM_CRB_ERR_TABLE_TOO_SHORT = 1,
    IB_TPM_CRB_ERR_NOT_COMMAND_RESPONSE_BUFFER = 2,
    IB_TPM_CRB_ERR_UNSUPPORTED_START_METHOD = 3,
    IB_TPM_CRB_ERR_NO_MEMORY_RESOURCE = 4,
    IB_TPM_CRB_ERR_UNMAPPABLE_REGION = 5,
    IB_TPM_CRB_ERR_MISALIGNED_REGISTERS = 6,
    IB_TPM_CRB_ERR_BUFFER_SIZE_MISMATCH = 7,
    IB_TPM_CRB_ERR_TIMEOUT = 8,
    IB_TPM_CRB_ERR_DEVICE_ERROR = 9,
    IB_TPM_CRB_ERR_COMMAND_TOO_LONG = 10,
    IB_TPM_CRB_ERR_RESPONSE_TOO_LONG = 11,
    IB_TPM_CRB_ERR_MALFORMED_RESPONSE = 12,
    IB_TPM_CRB_ERR_CANCELLED = 13,
    IB_TPM_CRB_ERR_ACPI = 14,
    IB_TPM_CRB_ERR_START_METHOD_FAILED = 15
} ib_tpm_crb_code;

typedef struct {
    ib_tpm_crb_code code;
    size_t length;
    ib_tpm_crb_start_method_t start_method;
    uint64_t start;
    uint64_t len;
    uint64_t address;
    uint32_t command;
    uint32_t response;
    const char *what;
    ib_uacpi_error_t acpi;
    uint64_t returned;
} ib_tpm_crb_error_t;

void ib_tpm_crb_error_message(const ib_tpm_crb_error_t *error,
                              char *buffer, size_t capacity);

typedef struct {
    uint64_t control_address;
    ib_tpm_crb_start_method_t start_method;
    int has_pluton;
    uint64_t pluton_start;
    uint64_t pluton_reply;
} ib_tpm_crb_tpm2_table_t;

size_t ib_tpm_crb_table_fixed_len(void);
int ib_tpm_crb_table_parse(const uint8_t *bytes, size_t length,
                           ib_tpm_crb_tpm2_table_t *out, ib_tpm_crb_error_t *error);

/* A region of physically addressed device memory. */
typedef struct {
    uintptr_t base;
    size_t length;
} ib_tpm_crb_region_t;

/* Region construction. */
ib_tpm_crb_code ib_tpm_crb_region_new(uint64_t start, uint64_t length,
                                      ib_tpm_crb_region_t *out,
                                      ib_tpm_crb_error_t *error);
ib_tpm_crb_code ib_tpm_crb_region_registers(uint64_t start, uint64_t length,
                                            ib_tpm_crb_region_t *out,
                                            ib_tpm_crb_error_t *error);
ib_tpm_crb_code ib_tpm_crb_region_subregion(const ib_tpm_crb_region_t *region,
                                            size_t offset, size_t length,
                                            ib_tpm_crb_region_t *out,
                                            ib_tpm_crb_error_t *error);
uint32_t ib_tpm_crb_region_read32(const ib_tpm_crb_region_t *region, size_t offset);
void ib_tpm_crb_region_write32(const ib_tpm_crb_region_t *region, size_t offset,
                               uint32_t value);
ib_tpm_crb_code ib_tpm_crb_region_read_bytes(const ib_tpm_crb_region_t *region,
                                             size_t offset, uint8_t *destination,
                                             size_t length, ib_tpm_crb_error_t *error);
ib_tpm_crb_code ib_tpm_crb_region_write_bytes(const ib_tpm_crb_region_t *region,
                                              size_t offset, const uint8_t *source,
                                              size_t length, ib_tpm_crb_error_t *error);

/* The driver. */
typedef struct {
    ib_tpm_crb_start_method_t start_method;
    ib_uacpi_device_t device;
    ib_uacpi_hardware_id_t hardware_id;
    int has_head;
    ib_tpm_crb_region_t head;
    ib_tpm_crb_region_t tail;
    int has_pluton;
    ib_tpm_crb_region_t pluton_start;
    ib_tpm_crb_region_t pluton_reply;
} ib_tpm_crb_interface_t;

typedef struct {
    ib_tpm_crb_interface_t interface;
    ib_tpm_crb_region_t command;
    ib_tpm_crb_region_t response;
    uint32_t command_size;
} ib_tpm_crb_t;

ib_tpm_crb_code ib_tpm_crb_probe(ib_tpm_crb_t *out, int *found,
                                 ib_tpm_crb_error_t *error);
ib_tpm_crb_start_method_t ib_tpm_crb_start_method(const ib_tpm_crb_t *tpm);
const char *ib_tpm_crb_hardware_id(const ib_tpm_crb_t *tpm);
uint32_t ib_tpm_crb_command_size(const ib_tpm_crb_t *tpm);
int ib_tpm_crb_interface_id(const ib_tpm_crb_t *tpm, uint64_t *out);

ib_tpm_crb_code ib_tpm_crb_transmit(ib_tpm_crb_t *tpm,
                                    const uint8_t *command, size_t command_length,
                                    uint8_t *response, size_t response_capacity,
                                    size_t *response_length,
                                    ib_tpm_crb_error_t *error);

#ifdef __cplusplus
}
#endif

#endif /* IB_TPM_CRB_H */
