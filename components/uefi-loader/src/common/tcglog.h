/*
 * tcglog.h — Portable crypto-agile TCG event-log parser/serializer.
 *
 * This is the single public surface shared by the Windows user-mode tools
 * (ib-tcg-dump, ibaudit) and the UEFI loader (loader.efi). The implementation
 * compiles from the same source for both targets:
 *
 *   host  -> C standard library allocation (malloc/realloc/free)
 *   UEFI  -> EFI Boot Services allocation (define TCGLOG_UEFI)
 *
 * The parser is zero-copy: parsed fields reference the caller's input buffer.
 * The caller MUST keep that buffer alive until tcglog_serialize() (if used)
 * and tcglog_free() have run.
 *
 * Wire format parsed (TCG PC Client Platform Firmware Profile):
 *
 *   [0]  TCG_EfiSpecIdEvent   "Spec ID Event03" header (raw, not wrapped)
 *   [..] TCG_PCR_EVENT2       entries (crypto-agile)
 *
 * SHA-1-era logs (header signature "Spec ID Event00") are rejected with a
 * dedicated, distinguishable status.
 */
#ifndef TCGLOG_H
#define TCGLOG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Allocation hooks. UEFI targets define TCGLOG_UEFI and link the             */
/* tcglog_efi_* allocators; host builds fall back to the C standard library.  */
/* ------------------------------------------------------------------------- */
#if defined(TCGLOG_UEFI)
void *tcglog_efi_alloc(size_t size);
void *tcglog_efi_realloc(void *ptr, size_t size);
void  tcglog_efi_free(void *ptr);
#define TCGLOG_ALLOC(sz)        tcglog_efi_alloc(sz)
#define TCGLOG_REALLOC(p, sz)   tcglog_efi_realloc((p), (sz))
#define TCGLOG_FREE(p)          tcglog_efi_free(p)
#else
#include <stdlib.h>
#define TCGLOG_ALLOC(sz)        malloc(sz)
#define TCGLOG_REALLOC(p, sz)   realloc((p), (sz))
#define TCGLOG_FREE(p)          free(p)
#endif

/* ------------------------------------------------------------------------- */
/* Status codes                                                               */
/* ------------------------------------------------------------------------- */
typedef enum {
    TCGLOG_OK = 0,
    /* Log header is not crypto-agile ("Spec ID Event00" or unknown). The
     * observed 16-byte signature is copied into tcglog_header_t.signature. */
    TCGLOG_ERR_SHA1_ONLY,
    /* Malformed structure: bad PCR index, invalid/duplicate event digest,
     * digest size mismatch with the declared algorithm list, or an EventSize
     * that is inconsistent with the surrounding structure. */
    TCGLOG_ERR_STRUCT,
    /* Buffer ended in the middle of a structure. err_off reports the byte
     * offset of the truncated element. */
    TCGLOG_ERR_TRUNC,
    /* Allocation failure during parse or serialize. */
    TCGLOG_ERR_NOMEM
} tcglog_status;

/* ------------------------------------------------------------------------- */
/* TPM algorithm identifiers (subset mirrored from Tcg2Protocol.h).          */
/* ------------------------------------------------------------------------- */
#define TCGLOG_ALG_ERROR   0x0000u
#define TCGLOG_ALG_SHA1    0x0004u
#define TCGLOG_ALG_SHA256  0x000Bu
#define TCGLOG_ALG_SHA384  0x000Cu
#define TCGLOG_ALG_SHA512  0x000Du

/* Largest digest size any active bank can declare (SHA-512 = 64 bytes). */
#define TCGLOG_MAX_DIGEST_SIZE 64u
/* Upper bound on declared algorithms; guards against corrupt headers. */
#define TCGLOG_MAX_ALG_COUNT   16u

/* ------------------------------------------------------------------------- */
/* TCG event types (subset relevant to boot-log parsing and boundary logic). */
/*                                                                           */
/* Declared as #define constants rather than an enum: several EFI event IDs   */
/* exceed INT_MAX (0x80000000..) and C enum constants would be typed `int`    */
/* (negative), which is fragile against the uint32_t wire values and raises   */
/* signed/unsigned warnings. `u`-suffixed macros keep them uint32_t-typed.    */
/* ------------------------------------------------------------------------- */
#define TCGLOG_EVT_PREBOOT_CERT              0x00000000u
#define TCGLOG_EVT_POST_CODE                 0x00000001u
#define TCGLOG_EVT_UNUSED                    0x00000002u
#define TCGLOG_EVT_NO_ACTION                 0x00000003u
#define TCGLOG_EVT_SEPARATOR                 0x00000004u
#define TCGLOG_EVT_ACTION                    0x00000005u
#define TCGLOG_EVT_EVENT_TAG                 0x00000006u
#define TCGLOG_EVT_S_CRTM_CONTENTS           0x00000007u
#define TCGLOG_EVT_S_CRTM_VERSION            0x00000008u
#define TCGLOG_EVT_CPU_MICROCODE             0x00000009u
#define TCGLOG_EVT_PLATFORM_CONFIG_FLAGS     0x0000000Au
#define TCGLOG_EVT_TABLE_OF_DEVICES          0x0000000Bu
#define TCGLOG_EVT_COMPACT_HASH              0x0000000Cu
#define TCGLOG_EVT_IPL                       0x0000000Du
#define TCGLOG_EVT_IPL_PARTITION_DATA        0x0000000Eu
#define TCGLOG_EVT_NONHOST_CODE              0x0000000Fu
#define TCGLOG_EVT_NONHOST_CONFIG            0x00000010u
#define TCGLOG_EVT_NONHOST_INFO              0x00000011u
#define TCGLOG_EVT_OMIT_BOOT_DEVICE_EVENTS   0x00000012u
#define TCGLOG_EVT_EFI_EVENT_BASE            0x80000000u
#define TCGLOG_EVT_EFI_VARIABLE_DRIVER_CONFIG 0x80000001u
#define TCGLOG_EVT_EFI_VARIABLE_BOOT         0x80000002u
#define TCGLOG_EVT_EFI_BOOT_SERVICES_APPLICATION 0x80000003u
#define TCGLOG_EVT_EFI_BOOT_SERVICES_DRIVER  0x80000004u
#define TCGLOG_EVT_EFI_RUNTIME_SERVICES_DRIVER 0x80000005u
#define TCGLOG_EVT_EFI_GPT_EVENT             0x80000006u
#define TCGLOG_EVT_EFI_ACTION                0x80000007u
#define TCGLOG_EVT_EFI_PLATFORM_FIRMWARE_BLOB 0x80000008u
#define TCGLOG_EVT_EFI_HANDOFF_TABLES        0x80000009u
#define TCGLOG_EVT_EFI_PLATFORM_FIRMWARE_BLOB2 0x8000000Au
#define TCGLOG_EVT_EFI_HANDOFF_TABLES2       0x8000000Bu
#define TCGLOG_EVT_EFI_VARIABLE_BOOT2        0x8000000Cu

/* ------------------------------------------------------------------------- */
/* Data model                                                                 */
/* ------------------------------------------------------------------------- */

/* Parsed "Spec ID Event03" header. */
typedef struct {
    /* Raw 16-byte signature. "Spec ID Event03" on success; on
     * TCGLOG_ERR_SHA1_ONLY it holds the observed (non-agile) signature. */
    char signature[16];
    uint32_t platform_class;
    uint8_t  spec_version_minor;
    uint8_t  spec_version_major;
    uint8_t  spec_errata;
    uint8_t  uintn_size;
    uint32_t alg_count;
    /* Pointer into the input buffer: alg_count * 4 bytes of
     * { uint16_t algorithm_id; uint16_t digest_size; } records. */
    const uint8_t *algs;
    uint8_t  vendor_info_size;
    /* Pointer into the input buffer (vendor_info_size bytes). */
    const uint8_t *vendor_info;
} tcglog_header_t;

/* Forward declaration for the event -> header back-reference. */
typedef struct tcglog tcglog_t;

/* One parsed TCG_PCR_EVENT2 entry. */
typedef struct {
    uint32_t pcr_index;     /* 0..23 */
    uint32_t event_type;    /* tcglog_event_type_t */
    uint32_t digest_count;  /* non-zero subset of header algorithms */
    /* Pointer into the input buffer: digest_count TPMT_HA records, each
     * { uint16_t hash_alg; uint8_t digest[digest_size]; }. Digest sizes are
     * resolved against the owning log's header algorithm list. */
    const uint8_t *digests;
    uint32_t event_size;    /* size of event_data in bytes */
    const uint8_t *event_data;
    const tcglog_header_t *header; /* back-reference for digest-size lookup */
} tcglog_event_t;

/* Fully parsed log. Owns only `events`; everything else borrows the input. */
struct tcglog {
    tcglog_header_t header;
    uint32_t        event_count;
    tcglog_event_t *events; /* array of event_count entries (malloc'd) */
};

/* ------------------------------------------------------------------------- */
/* Public API                                                                 */
/* ------------------------------------------------------------------------- */

/*
 * Parse a crypto-agile TCG event log.
 *
 * `buf`/`len` is the raw log (Spec ID Event03 header followed by
 * TCG_PCR_EVENT2 entries). On success *out is populated and the returned
 * status is TCGLOG_OK. On failure, `err_off` (if non-NULL) receives the byte
 * offset of the offending element and no partial events are committed.
 *
 * `out->events` is heap-allocated and must be released with tcglog_free().
 * The input buffer must remain valid for the lifetime of `out`.
 */
tcglog_status tcglog_parse(const uint8_t *buf, size_t len,
                           tcglog_t *out, size_t *err_off);

/*
 * Serialize a parsed log back to bytes. On success *out receives a
 * heap-allocated buffer (release with TCGLOG_FREE) and *out_len its size.
 * For any well-formed input, parse -> serialize is byte-identical.
 */
tcglog_status tcglog_serialize(const tcglog_t *log,
                               uint8_t **out, size_t *out_len);

/*
 * Serialize the header plus a contiguous range of events [start, end).
 * Byte-identical to tcglog_serialize restricted to the given slice. Used by
 * the tcglog.ib container to emit the pre-boot subset (header + PCR[0-7]
 * events). `start`/`end` are 0-based event indices; `end` is exclusive.
 */
tcglog_status tcglog_serialize_range(const tcglog_t *log,
                                     uint32_t start, uint32_t end,
                                     uint8_t **out, size_t *out_len);

/*
 * Release heap memory owned by a parsed log (the events array). The input
 * buffer is not touched.
 */
void tcglog_free(tcglog_t *log);

/*
 * Return the digest bytes for `alg` in event `e`, or NULL if the event does
 * not carry that bank. On success *size (if non-NULL) receives the digest
 * size in bytes.
 */
const uint8_t *tcglog_event_digest(const tcglog_event_t *e, uint16_t alg,
                                   size_t *size);

/*
 * Return the declared digest size (bytes) for `alg` in the header's algorithm
 * list, or 0 if `alg` is not declared.
 */
uint16_t tcglog_alg_digest_size(const tcglog_header_t *h, uint16_t alg);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TCGLOG_H */
