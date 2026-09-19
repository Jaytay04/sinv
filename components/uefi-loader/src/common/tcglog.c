/*
 * tcglog.c — Portable crypto-agile TCG event-log parser/serializer.
 *
 * Implementation is freestanding: it depends only on <stdint.h>/<stddef.h>
 * and the TCGLOG_ALLOC/REALLOC/FREE hooks. It uses no C standard library
 * string/stdio functions, so the same source compiles for Windows user-mode
 * tools and the UEFI loader target.
 *
 * Parsing is two-pass:
 *   pass 1  validate the whole log and count events (no allocation);
 *   pass 2  allocate the events array and fill it.
 * This guarantees "no partial events" — the output is committed only after
 * the entire buffer validates.
 *
 * All parsed variable-length data (algorithm list, vendor info, digests,
 * event data) borrows the caller's input buffer; only the events array is
 * heap-owned.
 */
#include "tcglog.h"

/* ------------------------------------------------------------------------- */
/* Freestanding byte helpers (no libc string functions)                      */
/* ------------------------------------------------------------------------- */
static void tcglog_memset(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    while (n-- > 0) {
        *d++ = (unsigned char)c;
    }
}

static void tcglog_memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n-- > 0) {
        *d++ = *s++;
    }
}

static int tcglog_memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    size_t i;
    for (i = 0; i < n; i++) {
        if (x[i] != y[i]) {
            return (int)x[i] - (int)y[i];
        }
    }
    return 0;
}

/* Bounds-checked little-endian reads. Return 0 on overrun. */
static int tcglog_rd_u8(const uint8_t *buf, size_t len, size_t *pos, uint8_t *out)
{
    if (*pos + 1 > len) {
        return 0;
    }
    *out = buf[*pos];
    *pos += 1;
    return 1;
}

static int tcglog_rd_u16(const uint8_t *buf, size_t len, size_t *pos, uint16_t *out)
{
    if (*pos + 2 > len) {
        return 0;
    }
    *out = (uint16_t)((uint16_t)buf[*pos] | ((uint16_t)buf[*pos + 1] << 8));
    *pos += 2;
    return 1;
}

static int tcglog_rd_u32(const uint8_t *buf, size_t len, size_t *pos, uint32_t *out)
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

/* Little-endian writes (no bounds checks; caller pre-sizes the buffer). */
static void tcglog_wr_u16(uint8_t *buf, size_t *pos, uint16_t v)
{
    buf[*pos]     = (uint8_t)(v & 0xffu);
    buf[*pos + 1] = (uint8_t)((v >> 8) & 0xffu);
    *pos += 2;
}

static void tcglog_wr_u32(uint8_t *buf, size_t *pos, uint32_t v)
{
    buf[*pos]     = (uint8_t)(v & 0xffu);
    buf[*pos + 1] = (uint8_t)((v >> 8) & 0xffu);
    buf[*pos + 2] = (uint8_t)((v >> 16) & 0xffu);
    buf[*pos + 3] = (uint8_t)((v >> 24) & 0xffu);
    *pos += 4;
}

/* ------------------------------------------------------------------------- */
/* Header parsing                                                            */
/* ------------------------------------------------------------------------- */
static tcglog_status tcglog_read_header(const uint8_t *buf, size_t len,
                                        size_t *pos, tcglog_header_t *hdr,
                                        size_t *err_off)
{
    uint32_t n_alg;
    uint32_t i;
    size_t field;

    /* Signature (16 bytes) — always at offset 0. */
    if (*pos + 16 > len) {
        *err_off = *pos;
        return TCGLOG_ERR_TRUNC;
    }
    tcglog_memcpy(hdr->signature, buf + *pos, 16);
    *pos += 16;

    if (tcglog_memcmp(hdr->signature, "Spec ID Event03", 16) != 0) {
        /* Non-crypto-agile log; observed signature stays in hdr->signature. */
        *err_off = 0;
        return TCGLOG_ERR_SHA1_ONLY;
    }

    field = *pos;
    if (!tcglog_rd_u32(buf, len, pos, &hdr->platform_class)) {
        *err_off = field;
        return TCGLOG_ERR_TRUNC;
    }

    field = *pos;
    if (!tcglog_rd_u8(buf, len, pos, &hdr->spec_version_minor) ||
        !tcglog_rd_u8(buf, len, pos, &hdr->spec_version_major) ||
        !tcglog_rd_u8(buf, len, pos, &hdr->spec_errata) ||
        !tcglog_rd_u8(buf, len, pos, &hdr->uintn_size)) {
        *err_off = field;
        return TCGLOG_ERR_TRUNC;
    }

    field = *pos;
    if (!tcglog_rd_u32(buf, len, pos, &n_alg)) {
        *err_off = field;
        return TCGLOG_ERR_TRUNC;
    }
    if (n_alg == 0 || n_alg > TCGLOG_MAX_ALG_COUNT) {
        *err_off = field;
        return TCGLOG_ERR_STRUCT;
    }

    hdr->algs = buf + *pos;
    for (i = 0; i < n_alg; i++) {
        uint16_t alg_id;
        uint16_t digest_size;
        field = *pos;
        if (!tcglog_rd_u16(buf, len, pos, &alg_id) ||
            !tcglog_rd_u16(buf, len, pos, &digest_size)) {
            *err_off = field;
            return TCGLOG_ERR_TRUNC;
        }
        if (digest_size == 0 || digest_size > TCGLOG_MAX_DIGEST_SIZE) {
            *err_off = field;
            return TCGLOG_ERR_STRUCT;
        }
    }
    hdr->alg_count = n_alg;

    field = *pos;
    if (!tcglog_rd_u8(buf, len, pos, &hdr->vendor_info_size)) {
        *err_off = field;
        return TCGLOG_ERR_TRUNC;
    }
    hdr->vendor_info = buf + *pos;
    field = *pos;
    if (*pos + hdr->vendor_info_size > len) {
        *err_off = field;
        return TCGLOG_ERR_TRUNC;
    }
    *pos += hdr->vendor_info_size;

    return TCGLOG_OK;
}

/* ------------------------------------------------------------------------- */
/* Event parsing (skip mode when `e` is NULL)                                */
/* ------------------------------------------------------------------------- */
static tcglog_status tcglog_read_event(const uint8_t *buf, size_t len,
                                       size_t *pos, const tcglog_header_t *hdr,
                                       tcglog_event_t *e, size_t *err_off)
{
    size_t ev_start = *pos;
    uint32_t pcr;
    uint32_t etype;
    uint32_t dcount;
    uint32_t esize;
    uint32_t i;
    uint32_t j;
    uint16_t seen_algs[TCGLOG_MAX_ALG_COUNT];
    const uint8_t *digests;

    if (!tcglog_rd_u32(buf, len, pos, &pcr) ||
        !tcglog_rd_u32(buf, len, pos, &etype) ||
        !tcglog_rd_u32(buf, len, pos, &dcount)) {
        *err_off = ev_start;
        return TCGLOG_ERR_TRUNC;
    }

    if (pcr > 23) {
        *err_off = ev_start;
        return TCGLOG_ERR_STRUCT;
    }
    if (dcount == 0 || dcount > hdr->alg_count) {
        *err_off = ev_start;
        return TCGLOG_ERR_STRUCT;
    }

    digests = buf + *pos;
    for (i = 0; i < dcount; i++) {
        uint16_t alg_id;
        uint16_t digest_size;
        size_t field = *pos;
        if (!tcglog_rd_u16(buf, len, pos, &alg_id)) {
            *err_off = ev_start;
            return TCGLOG_ERR_TRUNC;
        }
        digest_size = tcglog_alg_digest_size(hdr, alg_id);
        if (digest_size == 0) {
            /* Event carries an algorithm not declared by the header. */
            *err_off = field;
            return TCGLOG_ERR_STRUCT;
        }
        for (j = 0; j < i; j++) {
            if (seen_algs[j] == alg_id) {
                *err_off = field;
                return TCGLOG_ERR_STRUCT;
            }
        }
        seen_algs[i] = alg_id;
        if (*pos + digest_size > len) {
            *err_off = ev_start;
            return TCGLOG_ERR_TRUNC;
        }
        *pos += digest_size;
    }

    if (!tcglog_rd_u32(buf, len, pos, &esize)) {
        *err_off = ev_start;
        return TCGLOG_ERR_TRUNC;
    }
    if (*pos + esize > len) {
        /* EventSize exceeds remaining bytes — truncated final event. */
        *err_off = ev_start;
        return TCGLOG_ERR_TRUNC;
    }

    if (e != NULL) {
        e->pcr_index = pcr;
        e->event_type = etype;
        e->digest_count = dcount;
        e->digests = digests;
        e->event_size = esize;
        e->event_data = buf + *pos;
        e->header = hdr;
    }
    *pos += esize;
    return TCGLOG_OK;
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */
tcglog_status tcglog_parse(const uint8_t *buf, size_t len,
                           tcglog_t *out, size_t *err_off)
{
    size_t pos = 0;
    size_t err = 0;
    tcglog_status st;
    uint32_t count = 0;
    uint32_t i;

    if (out == NULL || buf == NULL) {
        if (err_off != NULL) {
            *err_off = 0;
        }
        return TCGLOG_ERR_STRUCT;
    }

    out->events = NULL;
    out->event_count = 0;
    tcglog_memset(&out->header, 0, sizeof(out->header));

    st = tcglog_read_header(buf, len, &pos, &out->header, &err);
    if (st != TCGLOG_OK) {
        if (err_off != NULL) {
            *err_off = err;
        }
        return st;
    }

    /* Pass 1: validate the remainder and count events (no allocation). */
    {
        size_t p = pos;
        while (p < len) {
            st = tcglog_read_event(buf, len, &p, &out->header, NULL, &err);
            if (st != TCGLOG_OK) {
                if (err_off != NULL) {
                    *err_off = err;
                }
                return st;
            }
            count++;
        }
    }

    /* Pass 2: allocate the events array and fill it. */
    if (count > 0) {
        out->events = (tcglog_event_t *)TCGLOG_ALLOC(
            (size_t)count * sizeof(tcglog_event_t));
        if (out->events == NULL) {
            if (err_off != NULL) {
                *err_off = 0;
            }
            return TCGLOG_ERR_NOMEM;
        }
    }
    out->event_count = count;

    {
        size_t p = pos;
        for (i = 0; i < count; i++) {
            st = tcglog_read_event(buf, len, &p, &out->header,
                                   &out->events[i], &err);
            if (st != TCGLOG_OK) {
                /* Unreachable: pass 1 already validated the buffer. */
                tcglog_free(out);
                if (err_off != NULL) {
                    *err_off = err;
                }
                return st;
            }
        }
    }

    return TCGLOG_OK;
}

void tcglog_free(tcglog_t *log)
{
    if (log == NULL) {
        return;
    }
    if (log->events != NULL) {
        TCGLOG_FREE(log->events);
    }
    log->events = NULL;
    log->event_count = 0;
}

uint16_t tcglog_alg_digest_size(const tcglog_header_t *h, uint16_t alg)
{
    const uint8_t *p;
    uint32_t i;

    if (h == NULL || h->algs == NULL) {
        return 0;
    }
    p = h->algs;
    for (i = 0; i < h->alg_count; i++) {
        uint16_t a = (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
        uint16_t s = (uint16_t)((uint16_t)p[2] | ((uint16_t)p[3] << 8));
        if (a == alg) {
            return s;
        }
        p += 4;
    }
    return 0;
}

const uint8_t *tcglog_event_digest(const tcglog_event_t *e, uint16_t alg,
                                   size_t *size)
{
    const uint8_t *p;
    uint32_t i;

    if (e == NULL || e->digests == NULL) {
        if (size != NULL) {
            *size = 0;
        }
        return NULL;
    }
    p = e->digests;
    for (i = 0; i < e->digest_count; i++) {
        uint16_t a = (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
        uint16_t s = tcglog_alg_digest_size(e->header, a);
        p += 2;
        if (a == alg) {
            if (size != NULL) {
                *size = s;
            }
            return p;
        }
        p += s;
    }
    if (size != NULL) {
        *size = 0;
    }
    return NULL;
}

/* Wire size (bytes) of one event, used to size the serialize buffer. */
static size_t tcglog_event_wire_size(const tcglog_event_t *e)
{
    size_t sz = 4 + 4 + 4; /* PCR index + event type + digest count */
    const uint8_t *p = e->digests;
    uint32_t i;
    for (i = 0; i < e->digest_count; i++) {
        uint16_t a = (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
        uint16_t s = tcglog_alg_digest_size(e->header, a);
        sz += 2 + s;
        p += 2 + s;
    }
    sz += 4 + e->event_size;
    return sz;
}

tcglog_status tcglog_serialize_range(const tcglog_t *log,
                                     uint32_t start, uint32_t end,
                                     uint8_t **out, size_t *out_len)
{
    size_t total;
    size_t pos = 0;
    uint8_t *buf;
    uint32_t i;
    uint32_t j;

    if (log == NULL || out == NULL || out_len == NULL ||
        start > end || end > log->event_count) {
        return TCGLOG_ERR_STRUCT;
    }

    total = 16 + 4 + 4 + 4;                     /* signature + platform class
                                                 * + 4 version bytes + alg count */
    total += (size_t)log->header.alg_count * 4; /* algorithm list */
    total += 1 + log->header.vendor_info_size;  /* vendor info */
    for (i = start; i < end; i++) {
        total += tcglog_event_wire_size(&log->events[i]);
    }

    buf = (uint8_t *)TCGLOG_ALLOC(total);
    if (buf == NULL) {
        return TCGLOG_ERR_NOMEM;
    }

    /* Header. */
    tcglog_memcpy(buf + pos, log->header.signature, 16);
    pos += 16;
    tcglog_wr_u32(buf, &pos, log->header.platform_class);
    buf[pos++] = log->header.spec_version_minor;
    buf[pos++] = log->header.spec_version_major;
    buf[pos++] = log->header.spec_errata;
    buf[pos++] = log->header.uintn_size;
    tcglog_wr_u32(buf, &pos, log->header.alg_count);
    tcglog_memcpy(buf + pos, log->header.algs, (size_t)log->header.alg_count * 4);
    pos += (size_t)log->header.alg_count * 4;
    buf[pos++] = log->header.vendor_info_size;
    tcglog_memcpy(buf + pos, log->header.vendor_info, log->header.vendor_info_size);
    pos += log->header.vendor_info_size;

    /* Events [start, end). */
    for (i = start; i < end; i++) {
        const tcglog_event_t *e = &log->events[i];
        const uint8_t *p = e->digests;
        tcglog_wr_u32(buf, &pos, e->pcr_index);
        tcglog_wr_u32(buf, &pos, e->event_type);
        tcglog_wr_u32(buf, &pos, e->digest_count);
        for (j = 0; j < e->digest_count; j++) {
            uint16_t a = (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
            uint16_t s = tcglog_alg_digest_size(e->header, a);
            tcglog_wr_u16(buf, &pos, a);
            tcglog_memcpy(buf + pos, p + 2, s);
            pos += s;
            p += 2 + s;
        }
        tcglog_wr_u32(buf, &pos, e->event_size);
        tcglog_memcpy(buf + pos, e->event_data, e->event_size);
        pos += e->event_size;
    }

    *out = buf;
    *out_len = pos;
    return TCGLOG_OK;
}

tcglog_status tcglog_serialize(const tcglog_t *log, uint8_t **out,
                               size_t *out_len)
{
    if (log == NULL) {
        return TCGLOG_ERR_STRUCT;
    }
    return tcglog_serialize_range(log, 0, log->event_count, out, out_len);
}
