/*
 * The tcglog.ib replay dump: a TCG event log rewritten for firmware.
 *
 * A dump is a fixed header, a table of fixed-size bank descriptors, a table of
 * fixed-size event descriptors in log order, and a heap those descriptors point
 * into.  Parsing checks the header and both tables; heap regions are checked as
 * each event or digest is reached, so the dump is walked once and never copied.
 */
#ifndef IB_TCGLOG_H
#define IB_TCGLOG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IB_TCGLOG_PCR_COUNT 8u
#define IB_TCGLOG_FILE_NAME "tcglog.ib"

#define IB_TCGLOG_MAGIC "IBTCGLOG"
#define IB_TCGLOG_VERSION 1u

#define IB_TCGLOG_HEADER_SIZE       36u
#define IB_TCGLOG_BANK_ENTRY_SIZE    8u
#define IB_TCGLOG_EVENT_ENTRY_SIZE  28u
#define IB_TCGLOG_DIGEST_ENTRY_SIZE  8u

typedef uint16_t ib_tcglog_algorithm_t;

#define IB_TCGLOG_ALG_SHA1    0x0004u
#define IB_TCGLOG_ALG_SHA256  0x000Bu
#define IB_TCGLOG_ALG_SHA384  0x000Cu
#define IB_TCGLOG_ALG_SHA512  0x000Du
#define IB_TCGLOG_ALG_SM3_256 0x0012u

typedef uint32_t ib_tcglog_event_type_t;

#define IB_TCGLOG_EVENT_TYPE_NO_ACTION 0x00000003u

#define IB_TCGLOG_EVENT_FLAG_LEGACY_ENCODING 0x00000001u

typedef struct {
    uint32_t total_size;
    uint32_t banks_offset;
    uint32_t events_offset;
    uint32_t event_count;
    uint8_t startup_locality;
    uint8_t bank_count;
} ib_tcglog_header_t;

typedef struct {
    uint16_t algorithm;
    uint16_t digest_size;
    uint32_t expected_offset;
} ib_tcglog_bank_entry_t;

typedef struct {
    uint32_t pcr_index;
    uint32_t event_type;
    uint32_t flags;
    uint32_t digest_count;
    uint32_t digests_offset;
    uint32_t data_offset;
    uint32_t data_size;
} ib_tcglog_event_entry_t;

typedef struct {
    uint16_t algorithm;
    uint16_t digest_size;
    uint32_t digest_offset;
} ib_tcglog_digest_entry_t;

typedef struct {
    const uint8_t *bytes;
    size_t length;
    ib_tcglog_header_t header;
} ib_tcglog_t;

typedef struct {
    ib_tcglog_algorithm_t algorithm;
    size_t digest_size;
    const uint8_t *expected;
    size_t expected_length;
} ib_tcglog_bank_t;

typedef struct {
    ib_tcglog_t dump;
    uint32_t index;
    ib_tcglog_event_entry_t entry;
    const uint8_t *data;
    size_t data_length;
} ib_tcglog_event_t;

typedef struct {
    ib_tcglog_algorithm_t algorithm;
    const uint8_t *bytes;
    size_t length;
} ib_tcglog_digest_t;

typedef enum {
    IB_TCGLOG_OK = 0,
    IB_TCGLOG_ERR_NOT_A_DUMP = 1,
    IB_TCGLOG_ERR_UNSUPPORTED_VERSION = 2,
    IB_TCGLOG_ERR_MALFORMED_HEADER = 3,
    IB_TCGLOG_ERR_SIZE_MISMATCH = 4,
    IB_TCGLOG_ERR_OUT_OF_BOUNDS = 5,
    IB_TCGLOG_ERR_MISSING_BANK = 6,
    IB_TCGLOG_ERR_MISSING_DIGEST = 7,
    IB_TCGLOG_ERR_PCR_OUT_OF_RANGE = 8
} ib_tcglog_code;

typedef struct {
    ib_tcglog_code code;
    uint32_t found_version;
    uint32_t expected_version;
    uint32_t declared;
    size_t actual;
    uint32_t offset;
    uint32_t len;
    ib_tcglog_algorithm_t algorithm;
    uint32_t index;
} ib_tcglog_error_t;

/* Algorithm helpers. */
const char *ib_tcglog_algorithm_name(ib_tcglog_algorithm_t algorithm);
void ib_tcglog_algorithm_display(ib_tcglog_algorithm_t algorithm,
                                 char *buffer, size_t capacity);
size_t ib_tcglog_algorithm_digest_size(ib_tcglog_algorithm_t algorithm);
/* Returns 0 when the algorithm is unknown. */
int ib_tcglog_algorithm_known(ib_tcglog_algorithm_t algorithm);

/* Layout codecs.  Decoders take the byte count so a short buffer fails rather
 * than reading past it. */
void ib_tcglog_header_encode(const ib_tcglog_header_t *header,
                             uint8_t bytes[IB_TCGLOG_HEADER_SIZE]);
int ib_tcglog_header_decode(const uint8_t *bytes, size_t length,
                            ib_tcglog_header_t *out, ib_tcglog_error_t *error);

void ib_tcglog_bank_entry_encode(const ib_tcglog_bank_entry_t *entry,
                                 uint8_t bytes[IB_TCGLOG_BANK_ENTRY_SIZE]);
int ib_tcglog_bank_entry_decode(const uint8_t *bytes, size_t length,
                                ib_tcglog_bank_entry_t *out);

void ib_tcglog_event_entry_encode(const ib_tcglog_event_entry_t *entry,
                                  uint8_t bytes[IB_TCGLOG_EVENT_ENTRY_SIZE]);
int ib_tcglog_event_entry_decode(const uint8_t *bytes, size_t length,
                                 ib_tcglog_event_entry_t *out);

void ib_tcglog_digest_entry_encode(const ib_tcglog_digest_entry_t *entry,
                                   uint8_t bytes[IB_TCGLOG_DIGEST_ENTRY_SIZE]);
int ib_tcglog_digest_entry_decode(const uint8_t *bytes, size_t length,
                                  ib_tcglog_digest_entry_t *out);

/* Dump reading. */
ib_tcglog_code ib_tcglog_parse(const uint8_t *bytes, size_t length,
                               ib_tcglog_t *out, ib_tcglog_error_t *error);

uint8_t ib_tcglog_startup_locality(const ib_tcglog_t *dump);
uint32_t ib_tcglog_event_count(const ib_tcglog_t *dump);
uint8_t ib_tcglog_bank_count(const ib_tcglog_t *dump);

ib_tcglog_code ib_tcglog_bank_at(const ib_tcglog_t *dump, uint32_t index,
                                 ib_tcglog_bank_t *out, ib_tcglog_error_t *error);
ib_tcglog_code ib_tcglog_bank(const ib_tcglog_t *dump,
                              ib_tcglog_algorithm_t algorithm,
                              ib_tcglog_bank_t *out, ib_tcglog_error_t *error);
const uint8_t *ib_tcglog_bank_expected(const ib_tcglog_bank_t *bank,
                                       uint32_t index, size_t *out_length);

ib_tcglog_code ib_tcglog_event_at(const ib_tcglog_t *dump, uint32_t index,
                                  ib_tcglog_event_t *out, ib_tcglog_error_t *error);

uint32_t ib_tcglog_event_index(const ib_tcglog_event_t *event);
uint32_t ib_tcglog_event_pcr_index(const ib_tcglog_event_t *event);
ib_tcglog_event_type_t ib_tcglog_event_type(const ib_tcglog_event_t *event);
uint32_t ib_tcglog_event_flags(const ib_tcglog_event_t *event);
const uint8_t *ib_tcglog_event_data(const ib_tcglog_event_t *event, size_t *out_length);
int ib_tcglog_event_extends_pcr(const ib_tcglog_event_t *event);
uint32_t ib_tcglog_event_digest_count(const ib_tcglog_event_t *event);

ib_tcglog_code ib_tcglog_event_digest_at(const ib_tcglog_event_t *event,
                                         uint32_t index, ib_tcglog_digest_t *out,
                                         ib_tcglog_error_t *error);
ib_tcglog_code ib_tcglog_event_digest(const ib_tcglog_event_t *event,
                                      ib_tcglog_algorithm_t algorithm,
                                      ib_tcglog_digest_t *out,
                                      ib_tcglog_error_t *error);

void ib_tcglog_error_message(const ib_tcglog_error_t *error,
                             char *buffer, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* IB_TCGLOG_H */
