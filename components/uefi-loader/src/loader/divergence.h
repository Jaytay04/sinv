/* divergence.h - Portable raw-extend divergence manifest. */
#ifndef DIVERGENCE_H
#define DIVERGENCE_H

#include <stddef.h>
#include <stdint.h>

#include "replay.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Little-endian wire format:
 *
 *   header (16 bytes)
 *     magic[4]       "IBDV"
 *     version        u16 = 1
 *     flags          u16 = 0
 *     record_count   u32
 *     payload_size   u32
 *
 *   record (20 bytes + digest tuples)
 *     record_size    u32
 *     event_index    u32
 *     pcr_index      u32
 *     event_type     u32
 *     digest_count   u32
 *     digests[]      { algorithm u16, size u16, bytes[size] }
 */
#define DIVERGENCE_MAGIC0 'I'
#define DIVERGENCE_MAGIC1 'B'
#define DIVERGENCE_MAGIC2 'D'
#define DIVERGENCE_MAGIC3 'V'
#define DIVERGENCE_VERSION 1u
#define DIVERGENCE_HEADER_SIZE 16u
#define DIVERGENCE_RECORD_HEADER_SIZE 20u

typedef enum {
    DIVERGENCE_OK = 0,
    DIVERGENCE_ERR_ARGUMENT,
    DIVERGENCE_ERR_REPLAY,
    DIVERGENCE_ERR_OVERFLOW,
    DIVERGENCE_ERR_NOMEM,
    DIVERGENCE_ERR_MAGIC,
    DIVERGENCE_ERR_VERSION,
    DIVERGENCE_ERR_TRUNCATED,
    DIVERGENCE_ERR_STRUCTURE,
    DIVERGENCE_ERR_NOT_FOUND
} divergence_status_t;

typedef struct {
    uint16_t version;
    uint16_t flags;
    uint32_t record_count;
    const uint8_t *records;
    size_t records_size;
} divergence_manifest_view_t;

typedef struct {
    uint32_t event_index;
    uint32_t pcr_index;
    uint32_t event_type;
    uint32_t digest_count;
    const uint8_t *digests;
    size_t digests_size;
} divergence_record_view_t;

/* Build one record for every event classified as REPLAY_EVENT_RAW_EXTEND. */
divergence_status_t divergence_build(const tcglog_t *log,
                                     uint32_t active_bank_mask,
                                     uint8_t **out, size_t *out_size);

/* Validate a complete manifest and return a zero-copy view. */
divergence_status_t divergence_read(const uint8_t *bytes, size_t size,
                                    divergence_manifest_view_t *out);

/* Return a zero-copy view of one manifest record. */
divergence_status_t divergence_record_at(
    const divergence_manifest_view_t *manifest, uint32_t record_index,
    divergence_record_view_t *out);

/* Return one digest from a record by algorithm ID. */
const uint8_t *divergence_record_digest(
    const divergence_record_view_t *record, uint16_t algorithm,
    size_t *digest_size);

#ifdef __cplusplus
}
#endif

#endif /* DIVERGENCE_H */
