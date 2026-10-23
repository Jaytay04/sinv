/*
 * TPM 2.0 command marshalling and reply decoding.
 *
 * Nothing here allocates or touches a device: a command is built into a buffer
 * the caller owns and a reply is read out of one.  Every field is big-endian.
 *
 * The 1:1 port keeps the Rust crate's three reply failure modes and its exact
 * Display text, including the "0x" prefixed eight-digit response code.
 */
#ifndef IB_TPM2_H
#define IB_TPM2_H

#include <stddef.h>
#include <stdint.h>

#include "ib_tcglog.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IB_TPM2_DIGEST_LEN_MAX  64u
#define IB_TPM2_BANK_COUNT_MAX   5u

#define IB_TPM2_TAG_NO_SESSIONS 0x8001u
#define IB_TPM2_TAG_SESSIONS    0x8002u

#define IB_TPM2_HEADER_LEN 10u
#define IB_TPM2_CODE_AT     6u

#define IB_TPM2_RC_SUCCESS 0u

typedef struct {
    ib_tcglog_algorithm_t algorithm;
    const uint8_t *bytes;
    size_t length;
} ib_tpm2_digest_t;

typedef enum {
    IB_TPM2_OK = 0,
    IB_TPM2_ERR_TRUNCATED = 1,
    IB_TPM2_ERR_REFUSED = 2,
    IB_TPM2_ERR_EMPTY = 3
} ib_tpm2_code;

typedef struct {
    ib_tpm2_code code;
    uint32_t response_code;
} ib_tpm2_error_t;

/* Common reply check: header present and response code zero. */
ib_tpm2_code ib_tpm2_accepted(const uint8_t *reply, size_t length,
                              ib_tpm2_error_t *error);

void ib_tpm2_error_message(const ib_tpm2_error_t *error,
                           char *buffer, size_t capacity);

/* TPM2_PCR_Extend. */
#define IB_TPM2_PCR_EXTEND_CAPACITY 361u
#define IB_TPM2_PCR_READ_CAPACITY    20u
#define IB_TPM2_PCR_REPLY_CAPACITY   ((size_t)(IB_TPM2_HEADER_LEN + 4u + 4u + 2u + 1u + 3u + 4u + 2u + IB_TPM2_DIGEST_LEN_MAX))

int ib_tpm2_pcr_extend(uint8_t *buffer, size_t capacity, uint32_t pcr_index,
                       const ib_tpm2_digest_t *digests, size_t digest_count,
                       size_t *out_length);
int ib_tpm2_pcr_read(uint8_t *buffer, size_t capacity, uint32_t pcr_index,
                     ib_tcglog_algorithm_t algorithm, size_t *out_length);
ib_tpm2_code ib_tpm2_pcr_value(const uint8_t *reply, size_t length,
                               const uint8_t **out, size_t *out_length,
                               ib_tpm2_error_t *error);

/* TPM2_GetCapability. */
#define IB_TPM2_CAPABILITY_COMMAND_CAPACITY 22u
#define IB_TPM2_CAPABILITY_REPLY_CAPACITY \
    ((size_t)(IB_TPM2_HEADER_LEN + 1u + 2u * 4u + IB_TPM2_BANK_COUNT_MAX * (2u + 1u + 8u)))

#define IB_TPM2_PROPERTY_MANUFACTURER      0x00000105u
#define IB_TPM2_PROPERTY_MAX_COMMAND_SIZE  0x0000011Eu
#define IB_TPM2_PROPERTY_MAX_RESPONSE_SIZE 0x0000011Fu

int ib_tpm2_capability_property(uint8_t *buffer, size_t capacity, uint32_t property,
                                size_t *out_length);
int ib_tpm2_capability_pcrs(uint8_t *buffer, size_t capacity, size_t *out_length);
int ib_tpm2_capability_command(uint8_t *buffer, size_t capacity, uint32_t capability,
                               uint32_t property, uint32_t count, size_t *out_length);

ib_tpm2_code ib_tpm2_capability_value(const uint8_t *reply, size_t length,
                                      uint32_t *out, ib_tpm2_error_t *error);
ib_tpm2_code ib_tpm2_capability_manufacturer(const uint8_t *reply, size_t length,
                                             uint32_t *out, ib_tpm2_error_t *error);
ib_tpm2_code ib_tpm2_capability_banks(const uint8_t *reply, size_t length,
                                      ib_tcglog_algorithm_t *banks, size_t capacity,
                                      size_t *out_count, ib_tpm2_error_t *error);

/* Display a TPM_PT_MANUFACTURER value the way the Rust Display impl does. */
void ib_tpm2_manufacturer_display(uint32_t manufacturer, char *buffer, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* IB_TPM2_H */
