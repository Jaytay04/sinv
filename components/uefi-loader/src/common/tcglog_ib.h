/*
 * tcglog_ib.h — The `tcglog.ib` pre-boot container.
 *
 * On-disk/wire contract for the reference measured-boot log that ib-tcg-dump
 * captures and loader.efi replays (design "tcglog.ib container"). Portable:
 * the same source compiles for Windows user-mode tools and the UEFI loader via
 * the TCGLOG_ALLOC/REALLOC/FREE hooks inherited from tcglog.h.
 *
 * Layout (little-endian, fixed 16-byte header + payload):
 *
 *   magic[4]       "IBLG"
 *   version        1
 *   flags          bit0 = crypto-agile payload asserted ("Spec ID Event03")
 *   event_count    number of pre-boot events in the payload (excludes the
 *                  Spec ID header)
 *   payload_len    byte length of payload[]
 *   payload[]      raw "Spec ID Event03" header + TCG_PCR_EVENT2 entries,
 *                  PCR 0-7 only
 *
 * The payload is itself a valid crypto-agile TCG log that tcglog_parse()
 * accepts. loader.efi reads event_count + payload_len to learn where the
 * genuine pre-boot chain ends, then appends its own HashLogExtendEvent
 * entries so the OS observes a continuous log.
 */
#ifndef TCGLOG_IB_H
#define TCGLOG_IB_H

#include <stddef.h>
#include <stdint.h>

#include "tcglog.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TCGLOG_IB_MAGIC0 'I'
#define TCGLOG_IB_MAGIC1 'B'
#define TCGLOG_IB_MAGIC2 'L'
#define TCGLOG_IB_MAGIC3 'G'

#define TCGLOG_IB_VERSION 1u

/* Flags (bit 0): the payload header is "Spec ID Event03" (crypto-agile). */
#define TCGLOG_IB_FLAG_CRYPTO_AGILE 0x0001u

/* Fixed header size in bytes (magic[4] + version[2] + flags[2] + count[4]
 * + payload_len[4]). */
#define TCGLOG_IB_HDR_SIZE 16u

/* EFI_IMAGE_LOAD_EVENT has four little-endian UINT64 fields before its
 * variable-length EFI device path.  Device-path node lengths are UINT16, so
 * this is also a hard upper bound for the path bytes accepted here. */
#define TCGLOG_IB_IMAGE_LOAD_HEADER_SIZE 32u
#define TCGLOG_IB_MAX_IMAGE_DEVICE_PATH_SIZE 65535u
#define TCGLOG_IB_MAX_IMAGE_PATH_CHARS 4096u

typedef struct {
    uint8_t  magic[4];
    uint16_t version;
    uint16_t flags;
    uint32_t event_count; /* pre-boot events (excludes the Spec ID header) */
    uint32_t payload_len;
    /* uint8_t payload[]; -- follows the header, not part of this struct */
} tcglog_ib_hdr_t;

typedef enum {
    TCGLOG_IB_OK = 0,
    TCGLOG_IB_ERR_MAGIC,   /* first 4 bytes are not "IBLG" */
    TCGLOG_IB_ERR_VERSION, /* unsupported container version */
    TCGLOG_IB_ERR_TRUNC,   /* buffer smaller than header, or payload_len overruns */
    TCGLOG_IB_ERR_NOMEM
} tcglog_ib_status;

/* Bounded EFI_IMAGE_LOAD_EVENT path parser status.  The parser is shared by
 * the host boundary detector and the UEFI replay adapter; all returned path
 * bytes borrow the parsed event's event_data buffer. */
typedef enum {
    TCGLOG_IB_IMAGE_PATH_OK = 0,
    TCGLOG_IB_IMAGE_PATH_ERR_ARGUMENT,
    TCGLOG_IB_IMAGE_PATH_ERR_NOT_IMAGE,
    TCGLOG_IB_IMAGE_PATH_ERR_TRUNCATED,
    TCGLOG_IB_IMAGE_PATH_ERR_MALFORMED,
    TCGLOG_IB_IMAGE_PATH_ERR_NO_FILEPATH,
    TCGLOG_IB_IMAGE_PATH_ERR_TRAVERSAL,
    TCGLOG_IB_IMAGE_PATH_ERR_TOO_LONG
} tcglog_ib_image_path_status_t;

/*
 * Extract the single MEDIA_FILEPATH_DP pathname from an EFI image-load
 * event.  The event data must be exactly:
 *
 *   UINT64 ImageLocation, ImageLength, ImageLinkTimeAddress,
 *   UINT64 LengthOfDevicePath, EFI_DEVICE_PATH_PROTOCOL DevicePath[]
 *
 * Every node and the terminating end node are bounded by event_size and the
 * device-path cap above.  The returned UTF-16 path is NUL-terminated and
 * remains owned by `event`; no allocation occurs.  Paths containing `.` or
 * `..` components (or either slash as a component separator) are rejected.
 */
tcglog_ib_image_path_status_t tcglog_ib_extract_image_path(
    const tcglog_event_t *event, const uint16_t **path,
    size_t *path_chars);

/* Return non-zero only for the canonical boot-manager path, compared
 * case-insensitively over ASCII and requiring the UEFI backslash spelling. */
int tcglog_ib_image_path_is_boot_manager(const uint16_t *path,
                                         size_t path_chars);

/* Convenience predicate used by boundary detection. */
int tcglog_ib_event_is_boot_manager(const tcglog_event_t *event);

/*
 * Boundary detection: return the number of pre-boot events to capture.
 *
 * The returned value N means "serialize header + events[0 .. N) into the
 * payload". All selected events carry PCRIndex 0-7. Marker precedence:
 *
 *   1. EV_EFI_BOOT_SERVICES_APPLICATION whose EFI image-load device path is
 *      canonical bootmgfw.efi -> include through that event (N = index + 1).
 *   2. EV_SEPARATOR (0x00000004) -> include through the first separator
 *      (N = index + 1).
 *   3. first event with PCRIndex >= 8 -> include up to, but excluding, it
 *      (N = index).
 *   4. no marker -> the whole log (N = event_count).
 */
uint32_t tcglog_ib_boundary(const tcglog_t *log);

/*
 * Build a tcglog.ib container from a parsed crypto-agile log. On success *out
 * receives a heap buffer (release with TCGLOG_FREE) and *out_len its size.
 * The payload is the pre-boot subset described above; PCR > 7 events are
 * never written (the subset is truncated at the first such event).
 */
tcglog_ib_status tcglog_ib_build(const tcglog_t *log,
                                 uint8_t **out, size_t *out_len);

/*
 * Validate and unwrap a tcglog.ib container. On success *hdr points into `buf`
 * and *payload/*payload_len describe the raw payload bytes (also into `buf`).
 * Does not allocate; the caller keeps `buf` alive.
 */
tcglog_ib_status tcglog_ib_read(const uint8_t *buf, size_t len,
                                const tcglog_ib_hdr_t **hdr,
                                const uint8_t **payload, size_t *payload_len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TCGLOG_IB_H */
