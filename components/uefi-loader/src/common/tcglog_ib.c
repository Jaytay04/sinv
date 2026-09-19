/*
 * tcglog_ib.c — Implementation of the `tcglog.ib` pre-boot container.
 *
 * Freestanding like tcglog.c: depends only on <stdint.h>/<stddef.h> and the
 * TCGLOG_ALLOC/REALLOC/FREE hooks (and tcglog_serialize_range). No libc
 * string/stdio functions, so the same source builds for Windows user-mode and
 * the UEFI loader.
 */
#include "tcglog_ib.h"

#define IB_DEVICE_PATH_NODE_HEADER_SIZE 4u
#define IB_MEDIA_DEVICE_PATH_TYPE 0x04u
#define IB_MEDIA_FILEPATH_SUBTYPE 0x04u
#define IB_END_DEVICE_PATH_TYPE 0x7fu
#define IB_END_ENTIRE_DEVICE_PATH_SUBTYPE 0xffu

/* ------------------------------------------------------------------------- */
/* Byte helpers (local; no libc string functions)                            */
/* ------------------------------------------------------------------------- */
static void ib_memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n-- > 0) {
        *d++ = *s++;
    }
}

static void ib_wr_u16(uint8_t *buf, size_t *pos, uint16_t v)
{
    buf[*pos]     = (uint8_t)(v & 0xffu);
    buf[*pos + 1] = (uint8_t)((v >> 8) & 0xffu);
    *pos += 2;
}

static void ib_wr_u32(uint8_t *buf, size_t *pos, uint32_t v)
{
    buf[*pos]     = (uint8_t)(v & 0xffu);
    buf[*pos + 1] = (uint8_t)((v >> 8) & 0xffu);
    buf[*pos + 2] = (uint8_t)((v >> 16) & 0xffu);
    buf[*pos + 3] = (uint8_t)((v >> 24) & 0xffu);
    *pos += 4;
}

static int ib_rd_u16(const uint8_t *buf, size_t len, size_t *pos, uint16_t *out)
{
    if (*pos + 2 > len) {
        return 0;
    }
    *out = (uint16_t)((uint16_t)buf[*pos] | ((uint16_t)buf[*pos + 1] << 8));
    *pos += 2;
    return 1;
}

static int ib_rd_u32(const uint8_t *buf, size_t len, size_t *pos, uint32_t *out)
{
    if (*pos + 4 > len) {
        return 0;
    }
    *out = (uint32_t)buf[*pos]
         | ((uint32_t)buf[*pos + 1] << 8)
         | ((uint32_t)buf[*pos + 2] << 16)
         | ((uint32_t)buf[*pos + 3] << 24);
    *pos += 4;
    return 1;
}

static uint16_t ib_rd_path_char(const uint8_t *buf)
{
    return (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
}

static uint64_t ib_rd_u64_at(const uint8_t *buf)
{
    uint64_t value = 0;
    size_t i;

    for (i = 0; i < 8; i++) {
        value |= ((uint64_t)buf[i]) << (i * 8u);
    }
    return value;
}

static int ib_is_path_separator(uint16_t value)
{
    return value == (uint16_t)'\\' || value == (uint16_t)'/';
}

/* Reject path components that would need normalization before opening a
 * file.  The loader never passes an untrusted path containing traversal to
 * EFI_FILE_PROTOCOL.Open, even though UEFI itself uses backslashes. */
static tcglog_ib_image_path_status_t ib_validate_path(
    const uint16_t *path, size_t path_chars)
{
    size_t component_start = 0;
    size_t i;

    for (i = 0; i <= path_chars; i++) {
        int at_end = i == path_chars;
        if (!at_end && !ib_is_path_separator(path[i])) {
            continue;
        }
        if (i - component_start == 1u && path[component_start] == (uint16_t)'.') {
            return TCGLOG_IB_IMAGE_PATH_ERR_TRAVERSAL;
        }
        if (i - component_start == 2u &&
            path[component_start] == (uint16_t)'.' &&
            path[component_start + 1u] == (uint16_t)'.') {
            return TCGLOG_IB_IMAGE_PATH_ERR_TRAVERSAL;
        }
        component_start = i + 1u;
    }
    return TCGLOG_IB_IMAGE_PATH_OK;
}

tcglog_ib_image_path_status_t tcglog_ib_extract_image_path(
    const tcglog_event_t *event, const uint16_t **path,
    size_t *path_chars)
{
    const uint8_t *event_data;
    const uint8_t *device_path;
    uint64_t device_path_length64;
    size_t event_size;
    size_t device_path_length;
    size_t position = 0;
    const uint16_t *found_path = NULL;
    size_t found_chars = 0;
    int end_seen = 0;

    if (path == NULL || path_chars == NULL) {
        return TCGLOG_IB_IMAGE_PATH_ERR_ARGUMENT;
    }
    *path = NULL;
    *path_chars = 0;
    if (event == NULL || event->event_type !=
        TCGLOG_EVT_EFI_BOOT_SERVICES_APPLICATION) {
        return TCGLOG_IB_IMAGE_PATH_ERR_NOT_IMAGE;
    }
    if (event->event_data == NULL || event->event_size <
        TCGLOG_IB_IMAGE_LOAD_HEADER_SIZE) {
        return TCGLOG_IB_IMAGE_PATH_ERR_TRUNCATED;
    }

    event_data = event->event_data;
    event_size = (size_t)event->event_size;
    device_path_length64 = ib_rd_u64_at(
        event_data + 24u);
    if (device_path_length64 > (uint64_t)TCGLOG_IB_MAX_IMAGE_DEVICE_PATH_SIZE) {
        return TCGLOG_IB_IMAGE_PATH_ERR_TOO_LONG;
    }
    device_path_length = (size_t)device_path_length64;
    if (device_path_length > event_size - TCGLOG_IB_IMAGE_LOAD_HEADER_SIZE) {
        return TCGLOG_IB_IMAGE_PATH_ERR_TRUNCATED;
    }
    if (device_path_length != event_size - TCGLOG_IB_IMAGE_LOAD_HEADER_SIZE) {
        return TCGLOG_IB_IMAGE_PATH_ERR_MALFORMED;
    }
    if (device_path_length < IB_DEVICE_PATH_NODE_HEADER_SIZE) {
        return TCGLOG_IB_IMAGE_PATH_ERR_TRUNCATED;
    }
    device_path = event_data + TCGLOG_IB_IMAGE_LOAD_HEADER_SIZE;

    while (position < device_path_length) {
        const uint8_t *node = device_path + position;
        size_t remaining = device_path_length - position;
        uint16_t node_length;
        uint8_t node_type;
        uint8_t node_subtype;

        if (remaining < IB_DEVICE_PATH_NODE_HEADER_SIZE) {
            return TCGLOG_IB_IMAGE_PATH_ERR_TRUNCATED;
        }
        node_type = node[0];
        node_subtype = node[1];
        node_length = (uint16_t)((uint16_t)node[2] |
                                 ((uint16_t)node[3] << 8));
        if (node_length < IB_DEVICE_PATH_NODE_HEADER_SIZE) {
            return TCGLOG_IB_IMAGE_PATH_ERR_MALFORMED;
        }
        if ((size_t)node_length > remaining) {
            return TCGLOG_IB_IMAGE_PATH_ERR_TRUNCATED;
        }

        if (node_type == IB_END_DEVICE_PATH_TYPE) {
            if (node_subtype != IB_END_ENTIRE_DEVICE_PATH_SUBTYPE ||
                node_length != IB_DEVICE_PATH_NODE_HEADER_SIZE ||
                (size_t)node_length != remaining) {
                return TCGLOG_IB_IMAGE_PATH_ERR_MALFORMED;
            }
            end_seen = 1;
            position += node_length;
            break;
        }

        if (node_type == IB_MEDIA_DEVICE_PATH_TYPE &&
            node_subtype == IB_MEDIA_FILEPATH_SUBTYPE) {
            size_t path_payload_bytes = (size_t)node_length -
                                         IB_DEVICE_PATH_NODE_HEADER_SIZE;
            size_t path_units;
            size_t i;

            if (found_path != NULL || path_payload_bytes < sizeof(uint16_t) ||
                (path_payload_bytes & 1u) != 0u) {
                return TCGLOG_IB_IMAGE_PATH_ERR_MALFORMED;
            }
            path_units = path_payload_bytes / sizeof(uint16_t);
            if (path_units == 0u || path_units >
                TCGLOG_IB_MAX_IMAGE_PATH_CHARS + 1u) {
                return TCGLOG_IB_IMAGE_PATH_ERR_TOO_LONG;
            }
            for (i = 0; i + 1u < path_units; i++) {
                if (ib_rd_path_char(node + IB_DEVICE_PATH_NODE_HEADER_SIZE +
                                    i * sizeof(uint16_t)) == 0u) {
                    return TCGLOG_IB_IMAGE_PATH_ERR_MALFORMED;
                }
            }
            if (ib_rd_path_char(node + IB_DEVICE_PATH_NODE_HEADER_SIZE +
                                (path_units - 1u) * sizeof(uint16_t)) != 0u) {
                return TCGLOG_IB_IMAGE_PATH_ERR_MALFORMED;
            }
            found_path = (const uint16_t *)(const void *)(
                node + IB_DEVICE_PATH_NODE_HEADER_SIZE);
            found_chars = path_units - 1u;
            if (found_chars == 0u) {
                return TCGLOG_IB_IMAGE_PATH_ERR_NO_FILEPATH;
            }
            if (ib_validate_path(found_path, found_chars) !=
                TCGLOG_IB_IMAGE_PATH_OK) {
                return TCGLOG_IB_IMAGE_PATH_ERR_TRAVERSAL;
            }
        }
        position += node_length;
    }

    if (!end_seen || position != device_path_length) {
        return TCGLOG_IB_IMAGE_PATH_ERR_TRUNCATED;
    }
    if (found_path == NULL) {
        return TCGLOG_IB_IMAGE_PATH_ERR_NO_FILEPATH;
    }
    *path = found_path;
    *path_chars = found_chars;
    return TCGLOG_IB_IMAGE_PATH_OK;
}

static uint16_t ib_ascii_lower(uint16_t value)
{
    if (value >= (uint16_t)'A' && value <= (uint16_t)'Z') {
        return (uint16_t)(value + ((uint16_t)'a' - (uint16_t)'A'));
    }
    return value;
}

int tcglog_ib_image_path_is_boot_manager(const uint16_t *path,
                                         size_t path_chars)
{
    static const uint16_t canonical[] = {
        '\\', 'E', 'F', 'I', '\\', 'M', 'i', 'c', 'r', 'o', 's', 'o', 'f', 't',
        '\\', 'B', 'o', 'o', 't', '\\', 'b', 'o', 'o', 't', 'm', 'g', 'f', 'w',
        '.', 'e', 'f', 'i'
    };
    size_t i;

    if (path == NULL || path_chars != sizeof(canonical) / sizeof(canonical[0])) {
        return 0;
    }
    for (i = 0; i < path_chars; i++) {
        if (ib_ascii_lower(path[i]) != ib_ascii_lower(canonical[i])) {
            return 0;
        }
    }
    return 1;
}

int tcglog_ib_event_is_boot_manager(const tcglog_event_t *event)
{
    const uint16_t *path = NULL;
    size_t path_chars = 0;

    return tcglog_ib_extract_image_path(event, &path, &path_chars) ==
               TCGLOG_IB_IMAGE_PATH_OK &&
           tcglog_ib_image_path_is_boot_manager(path, path_chars);
}

/* ------------------------------------------------------------------------- */
/* Boundary detection                                                        */
/* ------------------------------------------------------------------------- */
uint32_t tcglog_ib_boundary(const tcglog_t *log)
{
    uint32_t i;

    if (log == NULL) {
        return 0;
    }

    /* Precedence 1: the canonical boot-manager image-load event. */
    for (i = 0; i < log->event_count; i++) {
        if (tcglog_ib_event_is_boot_manager(&log->events[i])) {
            return i + 1;
        }
    }

    /* Precedence 2: EV_SEPARATOR. */
    for (i = 0; i < log->event_count; i++) {
        if (log->events[i].event_type == TCGLOG_EVT_SEPARATOR) {
            return i + 1;
        }
    }

    /* Precedence 3: first event with PCRIndex >= 8. */
    for (i = 0; i < log->event_count; i++) {
        if (log->events[i].pcr_index >= 8) {
            return i;
        }
    }

    /* No marker: whole log is pre-boot. */
    return log->event_count;
}

/* ------------------------------------------------------------------------- */
/* Container build / read                                                    */
/* ------------------------------------------------------------------------- */
tcglog_ib_status tcglog_ib_build(const tcglog_t *log,
                                 uint8_t **out, size_t *out_len)
{
    uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t *buf;
    size_t pos = 0;
    uint32_t k;
    uint32_t i;
    tcglog_status st;

    if (log == NULL || out == NULL || out_len == NULL) {
        return TCGLOG_IB_ERR_TRUNC;
    }

    k = tcglog_ib_boundary(log);

    /* Defensive PCR 0-7 contract: never emit an event beyond PCR 7. In a
     * well-formed pre-boot log this never truncates (PCR >= 8 events follow
     * the boundary markers), but it guards against malformed input. */
    for (i = 0; i < k; i++) {
        if (log->events[i].pcr_index > 7) {
            k = i;
            break;
        }
    }

    st = tcglog_serialize_range(log, 0, k, &payload, &payload_len);
    if (st != TCGLOG_OK) {
        return TCGLOG_IB_ERR_NOMEM;
    }

    buf = (uint8_t *)TCGLOG_ALLOC(TCGLOG_IB_HDR_SIZE + payload_len);
    if (buf == NULL) {
        TCGLOG_FREE(payload);
        return TCGLOG_IB_ERR_NOMEM;
    }

    buf[0] = TCGLOG_IB_MAGIC0;
    buf[1] = TCGLOG_IB_MAGIC1;
    buf[2] = TCGLOG_IB_MAGIC2;
    buf[3] = TCGLOG_IB_MAGIC3;
    pos = 4;
    ib_wr_u16(buf, &pos, TCGLOG_IB_VERSION);
    ib_wr_u16(buf, &pos, TCGLOG_IB_FLAG_CRYPTO_AGILE);
    ib_wr_u32(buf, &pos, k);
    ib_wr_u32(buf, &pos, (uint32_t)payload_len);
    ib_memcpy(buf + pos, payload, payload_len);
    TCGLOG_FREE(payload);

    *out = buf;
    *out_len = pos + payload_len;
    return TCGLOG_IB_OK;
}

tcglog_ib_status tcglog_ib_read(const uint8_t *buf, size_t len,
                                const tcglog_ib_hdr_t **hdr,
                                const uint8_t **payload, size_t *payload_len)
{
    uint16_t version;
    uint16_t flags;
    uint32_t event_count;
    uint32_t plen;
    size_t pos = 4;

    if (buf == NULL || hdr == NULL) {
        return TCGLOG_IB_ERR_TRUNC;
    }
    if (len < TCGLOG_IB_HDR_SIZE) {
        return TCGLOG_IB_ERR_TRUNC;
    }
    if (buf[0] != TCGLOG_IB_MAGIC0 || buf[1] != TCGLOG_IB_MAGIC1 ||
        buf[2] != TCGLOG_IB_MAGIC2 || buf[3] != TCGLOG_IB_MAGIC3) {
        return TCGLOG_IB_ERR_MAGIC;
    }

    if (!ib_rd_u16(buf, len, &pos, &version)) {
        return TCGLOG_IB_ERR_TRUNC;
    }
    if (version != TCGLOG_IB_VERSION) {
        return TCGLOG_IB_ERR_VERSION;
    }
    /* flags and event_count are consumed here only to bound payload_len. */
    if (!ib_rd_u16(buf, len, &pos, &flags) ||
        !ib_rd_u32(buf, len, &pos, &event_count) ||
        !ib_rd_u32(buf, len, &pos, &plen)) {
        return TCGLOG_IB_ERR_TRUNC;
    }
    (void)flags;
    (void)event_count;
    if ((size_t)plen > len - TCGLOG_IB_HDR_SIZE) {
        return TCGLOG_IB_ERR_TRUNC;
    }

    *hdr = (const tcglog_ib_hdr_t *)buf;
    if (payload != NULL) {
        *payload = buf + TCGLOG_IB_HDR_SIZE;
    }
    if (payload_len != NULL) {
        *payload_len = plen;
    }
    return TCGLOG_IB_OK;
}
