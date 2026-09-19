/*
 * tbs_tcgdump.c — TBS + TPM 2.0 command wrapper implementation.
 *
 * The TPM wire format is tightly byte-packed, so all marshalling here is done
 * with explicit little-endian read/write helpers over byte buffers rather than
 * casting structs over the buffers (avoids padding/alignment pitfalls).
 */
#include <stdlib.h>
#include <string.h>

#include "tbs_tcgdump.h"

/* ------------------------------------------------------------------------- */
/* Big-endian helpers                                                         */
/*                                                                           */
/* NOTE: the TPM 2.0 wire protocol is BIG-endian (network byte order) for     */
/* every multi-octet field in command and response structures — unlike the    */
/* TCG EFI event log, which is little-endian.                                 */
/* ------------------------------------------------------------------------- */
static void wr_u16(uint8_t *b, size_t *p, uint16_t v)
{
    b[*p]     = (uint8_t)((v >> 8) & 0xffu);
    b[*p + 1] = (uint8_t)(v & 0xffu);
    *p += 2;
}

static void wr_u32(uint8_t *b, size_t *p, uint32_t v)
{
    b[*p]     = (uint8_t)((v >> 24) & 0xffu);
    b[*p + 1] = (uint8_t)((v >> 16) & 0xffu);
    b[*p + 2] = (uint8_t)((v >> 8) & 0xffu);
    b[*p + 3] = (uint8_t)(v & 0xffu);
    *p += 4;
}

/* Overwrite the TPM commandSize field (fixed byte offset 2..5, big-endian). */
static void patch_u32(uint8_t *b, uint32_t v)
{
    b[2] = (uint8_t)((v >> 24) & 0xffu);
    b[3] = (uint8_t)((v >> 16) & 0xffu);
    b[4] = (uint8_t)((v >> 8) & 0xffu);
    b[5] = (uint8_t)(v & 0xffu);
}

static uint16_t rd_u16(const uint8_t *b, size_t len, size_t *p)
{
    if (*p + 2 > len) {
        return 0;
    }
    {
        uint16_t v = (uint16_t)(((uint16_t)b[*p] << 8) | (uint16_t)b[*p + 1]);
        *p += 2;
        return v;
    }
}

static uint32_t rd_u32(const uint8_t *b, size_t len, size_t *p)
{
    if (*p + 4 > len) {
        return 0;
    }
    {
        uint32_t v = ((uint32_t)b[*p] << 24)
                   | ((uint32_t)b[*p + 1] << 16)
                   | ((uint32_t)b[*p + 2] << 8)
                   | ((uint32_t)b[*p + 3]);
        *p += 4;
        return v;
    }
}

/* ------------------------------------------------------------------------- */
/* TBS context                                                               */
/* ------------------------------------------------------------------------- */
TBS_RESULT tcgdump_tbs_open(TBS_HCONTEXT *ctx)
{
    TBS_CONTEXT_PARAMS2 params;
    TBS_RESULT r;

    if (ctx == NULL) {
        return (TBS_RESULT)0x80000000u; /* arbitrary non-success */
    }

    /* Version TWO with includeTpm20 selects the TPM 2.0 device when present. */
    memset(&params, 0, sizeof(params));
    params.version = TBS_CONTEXT_VERSION_TWO;
    params.includeTpm20 = 1;

    r = Tbsi_Context_Create((PCTBS_CONTEXT_PARAMS)&params, ctx);
    return r;
}

void tcgdump_tbs_close(TBS_HCONTEXT ctx)
{
    if (ctx != NULL) {
        Tbsip_Context_Close(ctx);
    }
}

TBS_RESULT tcgdump_tbs_device_info(int *present, TPM_DEVICE_INFO *info)
{
    TBS_RESULT r;

    if (present == NULL || info == NULL) {
        return (TBS_RESULT)0x80000000u;
    }

    memset(info, 0, sizeof(*info));
    r = Tbsi_GetDeviceInfo(sizeof(*info), info);
    *present = (r == TBS_SUCCESS) ? 1 : 0;
    return r;
}

TBS_RESULT tcgdump_tbs_get_log(TBS_HCONTEXT ctx, uint8_t **out, size_t *out_len)
{
    uint32_t need = 0;
    TBS_RESULT r;
    uint8_t *buf;

    if (out == NULL || out_len == NULL) {
        return (TBS_RESULT)0x80000000u;
    }
    *out = NULL;
    *out_len = 0;

    /* Pass 1: query required size. */
    r = Tbsi_Get_TCG_Log(ctx, NULL, &need);
    if (r != TBS_SUCCESS && need == 0) {
        /* No log available or TBS cannot size it. */
        return r;
    }

    buf = (uint8_t *)malloc(need);
    if (buf == NULL) {
        return (TBS_RESULT)0x80000000u; /* out of memory */
    }

    /* Pass 2: fetch the log. */
    r = Tbsi_Get_TCG_Log(ctx, buf, &need);
    if (r != TBS_SUCCESS) {
        free(buf);
        return r;
    }

    *out = buf;
    *out_len = need;
    return TBS_SUCCESS;
}

/* ------------------------------------------------------------------------- */
/* Submit a raw command and capture the response                              */
/* ------------------------------------------------------------------------- */
#define TCGDUMP_RSP_CAP 4096
#define TCGDUMP_MALFORMED_RESPONSE ((TBS_RESULT)0x80280001u)

static TBS_RESULT tcgdump_submit(TBS_HCONTEXT ctx,
                                 const uint8_t *cmd, uint32_t cmd_len,
                                 uint8_t rsp[TCGDUMP_RSP_CAP],
                                 uint32_t *rsp_len)
{
    *rsp_len = TCGDUMP_RSP_CAP;
    return Tbsip_Submit_Command(ctx, TBS_COMMAND_LOCALITY_ZERO,
                                TBS_COMMAND_PRIORITY_NORMAL,
                                cmd, cmd_len, rsp, rsp_len);
}

static int tcgdump_response_header(const uint8_t *rsp, uint32_t rsp_len,
                                   size_t *position, uint32_t *response_code)
{
    size_t cursor = 0u;
    uint16_t tag;
    uint32_t declared_size;

    if (rsp == NULL || position == NULL || response_code == NULL ||
        rsp_len < 10u || rsp_len > TCGDUMP_RSP_CAP) {
        return 0;
    }
    tag = rd_u16(rsp, rsp_len, &cursor);
    declared_size = rd_u32(rsp, rsp_len, &cursor);
    *response_code = rd_u32(rsp, rsp_len, &cursor);
    if (tag != TPM_ST_NO_SESSIONS || declared_size != rsp_len ||
        cursor != 10u) {
        return 0;
    }
    *position = cursor;
    return 1;
}

static uint8_t tcgdump_digest_size_for_alg(uint16_t alg)
{
    switch (alg) {
    case TPM_ALG_SHA1:    return 20u;
    case TPM_ALG_SHA256:  return 32u;
    case TPM_ALG_SHA384:  return 48u;
    case TPM_ALG_SHA512:  return 64u;
    case TPM_ALG_SM3_256: return 32u;
    default:              return 0u;
    }
}

/* ------------------------------------------------------------------------- */
/* TPM2_GetCapability(TPM_CAP_PCRS)                                          */
/* ------------------------------------------------------------------------- */
TBS_RESULT tcgdump_tpm2_get_pcr_banks(TBS_HCONTEXT ctx,
                                      tcgdump_pcr_banks_t *banks,
                                      uint32_t *tpm_rc)
{
    uint8_t cmd[32];
    uint8_t rsp[TCGDUMP_RSP_CAP];
    uint32_t rsp_len = 0;
    size_t pos = 0;
    size_t rpos;
    uint32_t response_code;
    uint32_t count;
    uint32_t i;
    TBS_RESULT r;

    if (banks == NULL) {
        return TCGDUMP_MALFORMED_RESPONSE;
    }
    if (tpm_rc != NULL) {
        *tpm_rc = 0;
    }
    banks->count = 0;

    /* Build GetCapability(TPM_CAP_PCRS, property=0, count=1). */
    wr_u16(cmd, &pos, TPM_ST_NO_SESSIONS);
    wr_u32(cmd, &pos, 0);                 /* commandSize (patched below) */
    wr_u32(cmd, &pos, TPM_CC_GETCAPABILITY);
    wr_u32(cmd, &pos, TPM_CAP_PCRS);
    wr_u32(cmd, &pos, 0);                 /* property */
    wr_u32(cmd, &pos, 1);                 /* propertyCount */
    patch_u32(cmd, (uint32_t)pos);        /* patch commandSize */

    r = tcgdump_submit(ctx, cmd, (uint32_t)pos, rsp, &rsp_len);
    if (r != TBS_SUCCESS) {
        return r;
    }

    /* Response: tag(2) size(4) rc(4) moreData(1) capability(4) count(4) ... */
    if (!tcgdump_response_header(rsp, rsp_len, &rpos, &response_code)) {
        return TCGDUMP_MALFORMED_RESPONSE;
    }
    if (tpm_rc != NULL) {
        *tpm_rc = response_code;
    }
    if (response_code != TPM_RC_SUCCESS) {
        return TBS_SUCCESS;               /* TBS ok, TPM rejected */
    }

    if (rsp_len - rpos < 9u) {
        return TCGDUMP_MALFORMED_RESPONSE;
    }
    rpos++;                               /* moreData */
    if (rd_u32(rsp, rsp_len, &rpos) != TPM_CAP_PCRS) {
        return TCGDUMP_MALFORMED_RESPONSE;
    }
    count = rd_u32(rsp, rsp_len, &rpos);
    if (count > 8) {
        return TCGDUMP_MALFORMED_RESPONSE;
    }

    for (i = 0; i < count; i++) {
        tcgdump_pcr_bank_t *b = &banks->banks[i];
        uint16_t alg;
        uint8_t sel;
        uint32_t j;

        if (rsp_len - rpos < 2u) {
            return TCGDUMP_MALFORMED_RESPONSE;
        }
        alg = rd_u16(rsp, rsp_len, &rpos);
        if (rpos >= rsp_len) {
            return TCGDUMP_MALFORMED_RESPONSE;
        }
        sel = rsp[rpos++];
        if (sel == 0u || sel > sizeof(b->pcr_select) ||
            (size_t)rsp_len - rpos < sel) {
            return TCGDUMP_MALFORMED_RESPONSE;
        }

        b->alg = alg;
        b->digest_size = tcgdump_digest_size_for_alg(alg);
        memset(b->pcr_select, 0, sizeof(b->pcr_select));
        for (j = 0; j < sel; j++) {
            b->pcr_select[j] = rsp[rpos++];
        }
    }
    if (rpos != rsp_len) {
        return TCGDUMP_MALFORMED_RESPONSE;
    }
    banks->count = count;
    return TBS_SUCCESS;
}

/* ------------------------------------------------------------------------- */
/* TPM2_PCR_Read (PCR 0-7)                                                   */
/* ------------------------------------------------------------------------- */
TBS_RESULT tcgdump_tpm2_pcr_read(TBS_HCONTEXT ctx, uint16_t alg,
                                 uint8_t pcr_digests[TCGDUMP_MAX_PCRS][64],
                                 uint8_t *digest_size, uint32_t *tpm_rc)
{
    uint8_t cmd[32];
    uint8_t rsp[TCGDUMP_RSP_CAP];
    uint32_t rsp_len = 0;
    size_t pos = 0;
    size_t rpos;
    uint32_t response_code;
    uint32_t sel_count;
    uint32_t digest_count;
    uint32_t i;
    TBS_RESULT r;

    if (pcr_digests == NULL || digest_size == NULL ||
        tcgdump_digest_size_for_alg(alg) == 0u) {
        return TCGDUMP_MALFORMED_RESPONSE;
    }
    if (tpm_rc != NULL) {
        *tpm_rc = 0;
    }
    *digest_size = 0;
    memset(pcr_digests, 0, TCGDUMP_MAX_PCRS * 64u);

    /* Build PCR_Read: select PCR 0-7. sizeofSelect must be at least
     * PCR_SELECT_MIN (3 octets for the 24 platform PCRs); bit 0 of byte 0
     * selects PCR 0. Structures are packed (no padding). */
    wr_u16(cmd, &pos, TPM_ST_NO_SESSIONS);
    wr_u32(cmd, &pos, 0);                 /* commandSize (patched below) */
    wr_u32(cmd, &pos, TPM_CC_PCR_READ);
    wr_u32(cmd, &pos, 1);                 /* TPML_PCR_SELECTION count = 1 */
    wr_u16(cmd, &pos, alg);               /* hash */
    cmd[pos++] = 3;                       /* sizeofSelect = 3 */
    cmd[pos++] = 0xFF;                    /* pcrSelect[0]: PCR 0-7 */
    cmd[pos++] = 0x00;                    /* pcrSelect[1]: PCR 8-15 */
    cmd[pos++] = 0x00;                    /* pcrSelect[2]: PCR 16-23 */
    patch_u32(cmd, (uint32_t)pos);        /* patch commandSize */

    r = tcgdump_submit(ctx, cmd, (uint32_t)pos, rsp, &rsp_len);
    if (r != TBS_SUCCESS) {
        return r;
    }

    if (!tcgdump_response_header(rsp, rsp_len, &rpos, &response_code)) {
        return TCGDUMP_MALFORMED_RESPONSE;
    }
    if (tpm_rc != NULL) {
        *tpm_rc = response_code;
    }
    if (response_code != TPM_RC_SUCCESS) {
        return TBS_SUCCESS;               /* TBS ok, TPM rejected */
    }

    if (rsp_len - rpos < 8u) {
        return TCGDUMP_MALFORMED_RESPONSE;
    }
    (void)rd_u32(rsp, rsp_len, &rpos);    /* pcrUpdateCounter */

    /* selectionOut (TPML_PCR_SELECTION): count + count selections. */
    sel_count = rd_u32(rsp, rsp_len, &rpos);
    if (sel_count != 1u) {
        return TCGDUMP_MALFORMED_RESPONSE;
    }
    for (i = 0; i < sel_count; i++) {
        uint16_t returned_alg;
        uint8_t sel;

        if ((size_t)rsp_len - rpos < 3u) {
            return TCGDUMP_MALFORMED_RESPONSE;
        }
        returned_alg = rd_u16(rsp, rsp_len, &rpos);
        sel = rsp[rpos++];
        if (sel == 0u || sel > 4u || (size_t)rsp_len - rpos < sel) {
            return TCGDUMP_MALFORMED_RESPONSE;
        }
        if (returned_alg != alg || rsp[rpos] != 0xffu) {
            return TCGDUMP_MALFORMED_RESPONSE;
        }
        rpos += sel;
    }

    /* digests (TPML_DIGEST): count + count TPM2B_DIGEST{ size(2) buffer }. */
    if ((size_t)rsp_len - rpos < 4u) {
        return TCGDUMP_MALFORMED_RESPONSE;
    }
    digest_count = rd_u32(rsp, rsp_len, &rpos);
    if (digest_count != TCGDUMP_MAX_PCRS) {
        return TCGDUMP_MALFORMED_RESPONSE;
    }
    for (i = 0; i < digest_count; i++) {
        uint16_t sz;

        if ((size_t)rsp_len - rpos < 2u) {
            return TCGDUMP_MALFORMED_RESPONSE;
        }
        sz = rd_u16(rsp, rsp_len, &rpos);
        if (sz != tcgdump_digest_size_for_alg(alg) ||
            (size_t)rsp_len - rpos < sz) {
            return TCGDUMP_MALFORMED_RESPONSE;
        }
        memcpy(pcr_digests[i], rsp + rpos, sz);
        rpos += sz;
    }
    if (rpos != rsp_len) {
        return TCGDUMP_MALFORMED_RESPONSE;
    }
    *digest_size = tcgdump_digest_size_for_alg(alg);

    return TBS_SUCCESS;
}
