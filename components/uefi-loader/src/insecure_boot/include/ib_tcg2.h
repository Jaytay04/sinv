/*
 * EFI_TCG2_PROTOCOL over a TPM 2.0 behind a Command Response Buffer.
 *
 * The protocol is a table of function pointers whose state must be reachable
 * from the `This` they are handed.  ib_tcg2_instance_t therefore puts the table
 * first and the state straight after it, which makes the address of one the
 * address of the other.
 *
 * The pure parts (hashing, PE/COFF measurement, event log encoding) build with
 * no EFI headers in use; the parts that call firmware live in the *_efi.c files
 * and are only built for the EFI target.
 */
#ifndef IB_TCG2_H
#define IB_TCG2_H

#include <stddef.h>
#include <stdint.h>

#include "ib_hash.h"
#include "ib_tcg2_protocol.h"
#include "ib_tcglog.h"
#include "ib_tpm2.h"
#include "ib_tpm_crb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Protocol constants. */
#define IB_TCG2_MAX_PCR_INDEX        23u
#define IB_TCG2_REPLAYED_PCR_COUNT   IB_TCGLOG_PCR_COUNT
#define IB_TCG2_VERSION_MAJOR         1u
#define IB_TCG2_VERSION_MINOR         1u
#define IB_TCG2_VERSION_1_0_MINOR     0u
#define IB_TCG2_EVENT_HEADER_LEN      14u
#define IB_TCG2_EVENT_HEADER_VERSION  1u

/* Log constants. */
#define IB_TCG2_LOG_SPEC_ID_SIGNATURE "Spec ID Event03\0"
#define IB_TCG2_LOG_LEGACY_DIGEST_LEN 20u
#define IB_TCG2_LOG_EV_NO_ACTION      0x00000003u
#define IB_TCG2_LOG_SPEC_VERSION_MAJOR 2u
#define IB_TCG2_LOG_SPEC_VERSION_MINOR 0u
#define IB_TCG2_LOG_SPEC_ERRATA        0u
#define IB_TCG2_LOG_UINTN_SIZE         2u

#define IB_TCG2_LOG_HEADROOM          (64u * 1024u)
#define IB_TCG2_FINAL_EVENTS_CAPACITY (64u * 1024u)

/* Final events table constants. */
#define IB_TCG2_FINAL_EVENTS_VERSION  1u
#define IB_TCG2_FINAL_EVENTS_HEADER_LEN 16u
#define IB_TCG2_FINAL_EVENTS_COUNT_AT   8u

/* PE/COFF constants. */
#define IB_TCG2_PE_DOS_MAGIC        "MZ"
#define IB_TCG2_PE_OFFSET_AT         0x3cu
#define IB_TCG2_PE_MAGIC            "PE\0\0"
#define IB_TCG2_PE_COFF_HEADER_LEN   20u
#define IB_TCG2_PE_SECTION_COUNT_AT   2u
#define IB_TCG2_PE_OPTIONAL_LEN_AT   16u
#define IB_TCG2_PE_OPTIONAL_MAGIC_32 0x010bu
#define IB_TCG2_PE_OPTIONAL_MAGIC_64 0x020bu
#define IB_TCG2_PE_SIZE_OF_HEADERS_AT 60u
#define IB_TCG2_PE_CHECKSUM_AT        64u
#define IB_TCG2_PE_RVA_COUNT_AT_32    92u
#define IB_TCG2_PE_RVA_COUNT_AT_64   108u
#define IB_TCG2_PE_CERTIFICATE_INDEX   4u
#define IB_TCG2_PE_DIRECTORY_LEN       8u
#define IB_TCG2_PE_SECTION_HEADER_LEN 40u
#define IB_TCG2_PE_RAW_SIZE_AT        16u
#define IB_TCG2_PE_RAW_OFFSET_AT      20u

/* Upper bound the fixed section workspace can hold.  The PE/COFF loader on
 * Windows accepts at most 96 sections, so no image that can be started exceeds
 * it; a larger count is reported as a malformed image. */
#define IB_TCG2_PE_MAX_SECTIONS 96u

/* ------------------------------------------------------------------------- */
/* Errors                                                                    */
/* ------------------------------------------------------------------------- */
typedef enum {
    IB_TCG2_OK = 0,
    IB_TCG2_ERR_UNSUPPORTED_BANK = 1,
    IB_TCG2_ERR_TOO_MANY_BANKS = 2,
    IB_TCG2_ERR_NO_BANKS = 3,
    IB_TCG2_ERR_EVENT_TOO_LARGE = 4,
    IB_TCG2_ERR_MALFORMED_IMAGE = 5,
    IB_TCG2_ERR_DUMP = 6,
    IB_TCG2_ERR_TPM = 7,
    IB_TCG2_ERR_REPLY = 8,
    IB_TCG2_ERR_COMMAND_TOO_LONG = 9,
    IB_TCG2_ERR_UEFI = 10,
    /* C-only: a caller-provided buffer was too small.  The Rust original
     * allocates, so this has no counterpart there. */
    IB_TCG2_ERR_BUFFER_TOO_SMALL = 11
} ib_tcg2_code;

typedef struct {
    ib_tcg2_code code;
    ib_tcglog_algorithm_t algorithm;
    size_t value;
    const char *command;
    int has_dump;
    ib_tcglog_error_t dump;
    int has_tpm;
    ib_tpm_crb_error_t tpm;
    int has_reply;
    ib_tpm2_error_t reply;
    ib_efi_status status;
} ib_tcg2_error_t;

void ib_tcg2_error_message(const ib_tcg2_error_t *error,
                           char *buffer, size_t capacity);

/* ------------------------------------------------------------------------- */
/* Hashing                                                                   */
/* ------------------------------------------------------------------------- */
typedef struct {
    ib_tcglog_algorithm_t algorithm;
    size_t length;
    uint8_t bytes[IB_TPM2_DIGEST_LEN_MAX];
} ib_tcg2_digest_t;

typedef struct {
    ib_tcg2_digest_t entries[IB_TPM2_BANK_COUNT_MAX];
    size_t length;
} ib_tcg2_digests_t;

typedef struct {
    ib_tcglog_algorithm_t banks[IB_TPM2_BANK_COUNT_MAX];
    size_t length;
    ib_sha1_ctx sha1;
    ib_sha256_ctx sha256;
    ib_sha512_ctx sha384;
    ib_sha512_ctx sha512;
} ib_tcg2_hasher_t;

int ib_tcg2_supported(ib_tcglog_algorithm_t algorithm);

ib_tcg2_code ib_tcg2_hasher_new(ib_tcg2_hasher_t *out,
                                const ib_tcglog_algorithm_t *banks,
                                size_t bank_count, ib_tcg2_error_t *error);
void ib_tcg2_hasher_update(ib_tcg2_hasher_t *hasher,
                           const void *data, size_t length);
void ib_tcg2_hasher_finish(ib_tcg2_hasher_t *hasher, ib_tcg2_digests_t *out);

const uint8_t *ib_tcg2_digest_bytes(const ib_tcg2_digest_t *digest,
                                    size_t *out_length);
size_t ib_tcg2_digests_carry(const ib_tcg2_digests_t *digests,
                             ib_tpm2_digest_t *carried, size_t capacity);

/* ------------------------------------------------------------------------- */
/* PE/COFF measurement                                                       */
/* ------------------------------------------------------------------------- */
ib_tcg2_code ib_tcg2_pecoff_hash(ib_tcg2_hasher_t *hasher,
                                 const uint8_t *image, size_t length,
                                 ib_tcg2_error_t *error);

/* ------------------------------------------------------------------------- */
/* Event log                                                                 */
/* ------------------------------------------------------------------------- */
typedef struct {
    uint8_t *bytes;
    size_t capacity;
    size_t length;
    size_t last;
    int has_last;
    int truncated;
} ib_tcg2_log_t;

/* Encode one record.  `out` may be NULL, in which case only the length is
 * computed.  A non-NULL `out` must have room for the whole record. */
ib_tcg2_code ib_tcg2_event2(uint32_t pcr_index, uint32_t event_type,
                            const ib_tpm2_digest_t *digests, size_t digest_count,
                            const uint8_t *data, size_t data_length,
                            uint8_t *out, size_t capacity, size_t *out_length);
ib_tcg2_code ib_tcg2_event1(uint32_t pcr_index, uint32_t event_type,
                            const uint8_t *digest, size_t digest_length,
                            const uint8_t *data, size_t data_length,
                            uint8_t *out, size_t capacity, size_t *out_length);

/* Length the log would take, without headroom. */
ib_tcg2_code ib_tcg2_log_measure(const ib_tcglog_t *dump,
                                 const ib_tcglog_algorithm_t *banks,
                                 size_t bank_count, size_t *out_length,
                                 ib_tcg2_error_t *error);

/* Encode the records the log starts with into `out`.  With a non-NULL `dump`
 * the dump is reproduced record for record; otherwise the log is the single
 * specification identifier event. */
ib_tcg2_code ib_tcg2_log_encode(const ib_tcglog_t *dump,
                                const ib_tcglog_algorithm_t *banks,
                                size_t bank_count, uint8_t *out,
                                size_t capacity, size_t *out_length,
                                size_t *out_last, int *out_has_last,
                                ib_tcg2_error_t *error);

/* Allocate and build the log, keeping `headroom` bytes spare.  EFI only. */
ib_tcg2_code ib_tcg2_log_init(ib_tcg2_log_t *log, const ib_tcglog_t *dump,
                              const ib_tcglog_algorithm_t *banks,
                              size_t bank_count, size_t headroom,
                              ib_tcg2_error_t *error);
void ib_tcg2_log_release(ib_tcg2_log_t *log);

uint64_t ib_tcg2_log_address(const ib_tcg2_log_t *log);
uint64_t ib_tcg2_log_last_entry(const ib_tcg2_log_t *log);
int ib_tcg2_log_truncated(const ib_tcg2_log_t *log);
size_t ib_tcg2_log_length(const ib_tcg2_log_t *log);
size_t ib_tcg2_log_spare(const ib_tcg2_log_t *log);
int ib_tcg2_log_append(ib_tcg2_log_t *log, const uint8_t *entry, size_t length);

/* ------------------------------------------------------------------------- */
/* Final events table                                                        */
/* ------------------------------------------------------------------------- */
typedef struct {
    uint8_t *base;
    size_t capacity;
    size_t length;
    uint64_t events;
} ib_tcg2_final_events_t;

ib_tcg2_code ib_tcg2_final_events_install(ib_tcg2_final_events_t *out,
                                          size_t capacity,
                                          ib_tcg2_error_t *error);
uint64_t ib_tcg2_final_events_address(const ib_tcg2_final_events_t *table);
uint64_t ib_tcg2_final_events_events(const ib_tcg2_final_events_t *table);
size_t ib_tcg2_final_events_spare(const ib_tcg2_final_events_t *table);
int ib_tcg2_final_events_append(ib_tcg2_final_events_t *table,
                                const uint8_t *entry, size_t length);
ib_tcg2_code ib_tcg2_final_events_uninstall(ib_tcg2_final_events_t *table,
                                            ib_tcg2_error_t *error);

/* ------------------------------------------------------------------------- */
/* Protocol instance                                                         */
/* ------------------------------------------------------------------------- */
typedef struct {
    ib_tpm_crb_t tpm;
    ib_efi_tcg2_boot_service_capability capability;
    ib_tcglog_algorithm_t banks[IB_TPM2_BANK_COUNT_MAX];
    size_t bank_count;
    ib_tcg2_log_t log;
    ib_tcg2_final_events_t final_events;
    int log_taken;
} ib_tcg2_state_t;

typedef struct {
    ib_efi_tcg2_protocol protocol;
    ib_tcg2_state_t state;
} ib_tcg2_instance_t;

ib_tcg2_code ib_tcg2_instance_new(ib_tcg2_instance_t *out, ib_tpm_crb_t tpm,
                                  const ib_tcglog_t *dump, size_t headroom,
                                  size_t final_capacity,
                                  ib_tcg2_error_t *error);

/* The shared table every instance is built with. */
ib_efi_tcg2_protocol ib_tcg2_protocol_table(void);

void *ib_tcg2_instance_interface(ib_tcg2_instance_t *instance);
const ib_tcglog_algorithm_t *ib_tcg2_instance_banks(
    const ib_tcg2_instance_t *instance, size_t *out_count);
const ib_efi_tcg2_boot_service_capability *ib_tcg2_instance_capability(
    const ib_tcg2_instance_t *instance);
void ib_tcg2_instance_log(const ib_tcg2_instance_t *instance,
                          uint64_t *address, size_t *length, size_t *spare);
void ib_tcg2_instance_final_events(const ib_tcg2_instance_t *instance,
                                   uint64_t *address, uint64_t *events,
                                   size_t *spare);

/* Withdraws the final events table and hands the TPM back. */
ib_tcg2_code ib_tcg2_instance_release(ib_tcg2_instance_t *instance,
                                      ib_tpm_crb_t *out_tpm,
                                      ib_tcg2_error_t *error);

/* ------------------------------------------------------------------------- */
/* Installation                                                              */
/* ------------------------------------------------------------------------- */
typedef struct {
    size_t v1_found;
    size_t v1_removed;
    size_t v2_found;
    size_t v2_removed;
} ib_tcg2_displacement_t;

typedef struct {
    ib_efi_handle handle;
    const ib_efi_guid_t *guid;
    const void *interface;
} ib_tcg2_displaced_t;

typedef struct {
    ib_efi_handle handle;
    ib_tcg2_instance_t *instance;
    ib_tcg2_displacement_t displacement;
    ib_tcg2_displaced_t *removed;
    size_t removed_count;
    size_t removed_capacity;
} ib_tcg2_t;

/* Installs the protocol and takes away every TCG interface firmware had. */
ib_tcg2_code ib_tcg2_install(ib_tcg2_t *out, ib_tpm_crb_t tpm,
                             const ib_tcglog_t *dump,
                             ib_tcg2_error_t *error);

const ib_tcg2_instance_t *ib_tcg2_instance(const ib_tcg2_t *tcg2);
ib_efi_handle ib_tcg2_handle(const ib_tcg2_t *tcg2);
ib_tcg2_displacement_t ib_tcg2_displaced(const ib_tcg2_t *tcg2);

/* Withdraws the protocol, restores what it displaced, and hands the TPM back. */
ib_tcg2_code ib_tcg2_uninstall(ib_tcg2_t *tcg2, ib_tpm_crb_t *out_tpm,
                               ib_tcg2_error_t *error);

#ifdef __cplusplus
}
#endif

#endif /* IB_TCG2_H */
