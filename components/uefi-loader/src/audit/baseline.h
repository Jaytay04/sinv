/* baseline.h - Detector 6: bounded JSON baseline and drift mapping. */
#ifndef IB_AUDIT_BASELINE_H
#define IB_AUDIT_BASELINE_H

#include <stddef.h>
#include <stdint.h>

#include "sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IB_AUDIT_BASELINE_SCHEMA 1u
#define IB_AUDIT_BASELINE_PCR_COUNT 8u
#define IB_AUDIT_BASELINE_MAX_BANKS 8u
#define IB_AUDIT_BASELINE_MAX_DIGEST 64u
#define IB_AUDIT_BASELINE_MAX_MOK 128u
#define IB_AUDIT_BASELINE_MAX_BOOT_ORDER 128u
#define IB_AUDIT_BASELINE_JSON_MAX (64u * 1024u)

typedef struct {
    uint16_t algorithm;
    uint16_t digest_size;
    uint8_t pcrs[IB_AUDIT_BASELINE_PCR_COUNT][IB_AUDIT_BASELINE_MAX_DIGEST];
} ib_audit_baseline_bank_t;

typedef struct {
    uint32_t bank_count;
    ib_audit_baseline_bank_t banks[IB_AUDIT_BASELINE_MAX_BANKS];
    uint8_t log_sha256[SHA256_DIGEST_SIZE];
    uint32_t mok_count;
    uint8_t mok_sha256[IB_AUDIT_BASELINE_MAX_MOK][SHA256_DIGEST_SIZE];
    uint8_t bootmgfw_sha256[SHA256_DIGEST_SIZE];
    uint32_t boot_order_count;
    uint16_t boot_order[IB_AUDIT_BASELINE_MAX_BOOT_ORDER];
    uint8_t boot_next_present;
    uint16_t boot_next;
} ib_audit_baseline_t;

enum {
    IB_AUDIT_DRIFT_PCR = 1u << 0,
    IB_AUDIT_DRIFT_LOG = 1u << 1,
    IB_AUDIT_DRIFT_MOK = 1u << 2,
    IB_AUDIT_DRIFT_BOOTMGFW = 1u << 3,
    IB_AUDIT_DRIFT_BOOT_ORDER = 1u << 4,
    IB_AUDIT_DRIFT_BOOT_NEXT = 1u << 5
};

enum {
    IB_AUDIT_DIFF_DETECTOR_2 = 1u << 2,
    IB_AUDIT_DIFF_DETECTOR_3 = 1u << 3,
    IB_AUDIT_DIFF_DETECTOR_4 = 1u << 4
};

typedef struct {
    uint32_t drift;
    uint32_t detector_mask;
    uint32_t added_mok_count;
    uint32_t removed_mok_count;
} ib_audit_baseline_diff_t;

typedef enum {
    IB_AUDIT_BASELINE_OK = 0,
    IB_AUDIT_BASELINE_INVALID_ARGUMENT = 1,
    IB_AUDIT_BASELINE_INVALID_STATE = 2,
    IB_AUDIT_BASELINE_BUFFER_TOO_SMALL = 3,
    IB_AUDIT_BASELINE_INVALID_JSON = 4
} ib_audit_baseline_status_t;

ib_audit_baseline_status_t ib_audit_baseline_encode(
    const ib_audit_baseline_t *state, char *json, size_t capacity,
    size_t *json_size);
ib_audit_baseline_status_t ib_audit_baseline_decode(
    const char *json, size_t json_size, ib_audit_baseline_t *state);
ib_audit_baseline_status_t ib_audit_baseline_diff(
    const ib_audit_baseline_t *before, const ib_audit_baseline_t *after,
    ib_audit_baseline_diff_t *diff);

#ifdef __cplusplus
}
#endif

#endif
