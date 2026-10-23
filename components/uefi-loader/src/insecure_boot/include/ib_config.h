/*
 * The ib-config.bin staged configuration.
 *
 * All integers are little-endian and the file is exactly:
 *   0x00 8  magic "IBCONFIG"
 *   0x08 4  format version 1
 *   0x0c 2  Setup offset of the TPM UEFI spec version byte
 */
#ifndef IB_CONFIG_H
#define IB_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IB_CONFIG_FILE_NAME "ib-config.bin"

#define IB_CONFIG_LENGTH          14u
#define IB_CONFIG_VERSION_AT       8u
#define IB_CONFIG_SETUP_OFFSET_AT 12u

typedef struct {
    uint16_t tcg_spec_offset;
} ib_config_t;

typedef enum {
    IB_CONFIG_OK = 0,
    IB_CONFIG_ERR_NOT_A_CONFIG = 1,
    IB_CONFIG_ERR_UNSUPPORTED_VERSION = 2,
    IB_CONFIG_ERR_SIZE_MISMATCH = 3
} ib_config_code;

typedef struct {
    ib_config_code code;
    uint32_t found_version;
    uint32_t expected_version;
    size_t actual;
    size_t expected;
} ib_config_error_t;

ib_config_t ib_config_new(uint16_t tcg_spec_offset);
size_t ib_config_tcg_spec_offset(const ib_config_t *config);

void ib_config_to_bytes(const ib_config_t *config, uint8_t bytes[IB_CONFIG_LENGTH]);

ib_config_code ib_config_parse(const uint8_t *bytes, size_t length,
                               ib_config_t *out, ib_config_error_t *error);

void ib_config_error_message(const ib_config_error_t *error, char *buffer, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* IB_CONFIG_H */
