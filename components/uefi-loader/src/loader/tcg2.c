/*
 * tcg2.c — EFI_TCG2_PROTOCOL wrapper implementation for loader.efi.
 *
 * The TPM 2.0 command/response wire format is BIG-endian for every multi-octet
 * field (unlike the TCG EFI event log, which is little-endian). tcg2_pcr_read_bank
 * marshals TPM2_PCR_Read manually over byte buffers — never by casting structs
 * over the buffer (avoids padding/alignment pitfalls). No libc: all copies are
 * explicit loops, so this compiles into the freestanding EFI image.
 */
#include <stdint.h>

#include "tcg2.h"

/* TPM 2.0 constants (subset). */
#define TPM2_ST_NO_SESSIONS 0x8001u
#define TPM2_ST_SESSIONS    0x8002u
#define TPM2_CC_PCR_READ    0x0000017Eu
#define TPM2_CC_PCR_EXTEND  0x00000182u
#define TPM2_RS_PW          0x40000009u
#define TPM2_RC_SUCCESS     0x00000000u

/* SubmitCommand response buffer: PCR_Read of 8 SHA-512 digests is ~556 bytes;
 * 1024 gives comfortable headroom and stays under typical MaxResponseSize. */
#define TCG2_RSP_CAP 1024u
#define TCG2_EXTEND_CMD_CAP 320u

/* ------------------------------------------------------------------------- */
/* Big-endian marshalling helpers                                             */
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

/* Overwrite the TPM commandSize field (byte offset 2..5, big-endian). */
static void patch_cmdsize(uint8_t *b, uint32_t v)
{
    b[2] = (uint8_t)((v >> 24) & 0xffu);
    b[3] = (uint8_t)((v >> 16) & 0xffu);
    b[4] = (uint8_t)((v >> 8) & 0xffu);
    b[5] = (uint8_t)(v & 0xffu);
}

static int rd_u16(const uint8_t *b, size_t len, size_t *p, uint16_t *out)
{
    uint16_t v;

    if (b == NULL || p == NULL || out == NULL || *p > len ||
        len - *p < 2u) {
        return 0;
    }
    v = (uint16_t)(((uint16_t)b[*p] << 8) | (uint16_t)b[*p + 1]);
    *p += 2;
    *out = v;
    return 1;
}

static int rd_u32(const uint8_t *b, size_t len, size_t *p, uint32_t *out)
{
    uint32_t v;

    if (b == NULL || p == NULL || out == NULL || *p > len ||
        len - *p < 4u) {
        return 0;
    }
    v = ((uint32_t)b[*p] << 24)
      | ((uint32_t)b[*p + 1] << 16)
      | ((uint32_t)b[*p + 2] << 8)
      | ((uint32_t)b[*p + 3]);
    *p += 4;
    *out = v;
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Protocol wrappers                                                          */
/* ------------------------------------------------------------------------- */
EFI_STATUS tcg2_locate(EFI_TCG2_PROTOCOL **out)
{
    EFI_GUID guid = EFI_TCG2_PROTOCOL_GUID;
    EFI_TCG2_PROTOCOL *p = NULL;
    EFI_STATUS st;

    if (out == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *out = NULL;

    if (gBS == NULL) {
        return EFI_NOT_READY;
    }

    st = gBS->LocateProtocol(&guid, NULL, (VOID **)&p);
    if (!EFI_ERROR(st)) {
        *out = p;
    }
    return st;
}

EFI_STATUS tcg2_get_capability(EFI_TCG2_PROTOCOL *p,
                               EFI_TCG2_BOOT_SERVICE_CAPABILITY *cap)
{
    if (p == NULL || cap == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    /* The protocol requires the caller to report the structure size. */
    if (gBS != NULL) {
        gBS->SetMem(cap, sizeof(*cap), 0);
    }
    cap->Size = (UINT8)sizeof(*cap);
    return p->GetCapability(p, cap);
}

EFI_STATUS tcg2_get_active_banks(EFI_TCG2_PROTOCOL *p, UINT32 *active)
{
    if (p == NULL || active == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *active = 0;
    return p->GetActivePcrBanks(p, active);
}

UINT16 tcg2_digest_size(TPM_ALG_ID alg)
{
    switch (alg) {
    case TPM_ALG_SHA1:   return 20;
    case TPM_ALG_SHA256: return 32;
    case TPM_ALG_SHA384: return 48;
    case TPM_ALG_SHA512: return 64;
    default:             return 0;
    }
}

/* ------------------------------------------------------------------------- */
/* TPM2_PCR_Read via SubmitCommand                                            */
/* ------------------------------------------------------------------------- */
EFI_STATUS tcg2_pcr_read_bank(EFI_TCG2_PROTOCOL *p, TPM_ALG_ID alg,
                              uint8_t out[TCG2_PCR_READ_BYTES],
                              UINT16 *digest_size)
{
    uint8_t cmd[32];
    uint8_t rsp[TCG2_RSP_CAP];
    size_t pos = 0;
    size_t rpos;
    uint32_t rsp_len;
    uint32_t response_code;
    uint32_t sel_count;
    uint32_t digest_count;
    uint16_t response_tag;
    uint16_t selection_alg;
    uint16_t expected_digest_size;
    uint8_t select_size;
    uint32_t i;
    EFI_STATUS st;

    if (p == NULL || out == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    if (digest_size != NULL) {
        *digest_size = 0;
    }
    expected_digest_size = tcg2_digest_size(alg);
    if (expected_digest_size == 0u) {
        return EFI_INVALID_PARAMETER;
    }

    /* TPM2_PCR_Read command:
     *   tag(2) commandSize(4) commandCode(4)
     *   TPML_PCR_SELECTION{ count(4)=1,
     *     TPMS_PCR_SELECTION{ hash(2), sizeofSelect(1)=3, pcrSelect[3] } }
     * sizeofSelect must be >= PCR_SELECT_MIN (3 octets for the 24 platform
     * PCRs); bit 0 of byte 0 selects PCR 0. */
    wr_u16(cmd, &pos, TPM2_ST_NO_SESSIONS);
    wr_u32(cmd, &pos, 0);                 /* commandSize (patched below) */
    wr_u32(cmd, &pos, TPM2_CC_PCR_READ);
    wr_u32(cmd, &pos, 1);                 /* selection count = 1 */
    wr_u16(cmd, &pos, (uint16_t)alg);     /* hash algorithm */
    cmd[pos++] = 3;                       /* sizeofSelect = 3 */
    cmd[pos++] = 0xFF;                    /* pcrSelect[0]: PCR 0-7 */
    cmd[pos++] = 0x00;                    /* pcrSelect[1]: PCR 8-15 */
    cmd[pos++] = 0x00;                    /* pcrSelect[2]: PCR 16-23 */
    patch_cmdsize(cmd, (uint32_t)pos);

    st = p->SubmitCommand(p, (UINT32)pos, cmd, TCG2_RSP_CAP, rsp);
    if (EFI_ERROR(st)) {
        return st;
    }

    /* Parse response header: tag(2) responseSize(4) responseCode(4). */
    rpos = 0;
    if (!rd_u16(rsp, TCG2_RSP_CAP, &rpos, &response_tag) ||
        !rd_u32(rsp, TCG2_RSP_CAP, &rpos, &rsp_len) ||
        response_tag != TPM2_ST_NO_SESSIONS || rsp_len < 10u ||
        rsp_len > TCG2_RSP_CAP) {
        return EFI_DEVICE_ERROR;
    }
    if (!rd_u32(rsp, rsp_len, &rpos, &response_code)) {
        return EFI_DEVICE_ERROR;
    }
    if (response_code != TPM2_RC_SUCCESS) {
        return EFI_DEVICE_ERROR;
    }

    if (!rd_u32(rsp, rsp_len, &rpos, &response_code)) {
        return EFI_DEVICE_ERROR;              /* pcrUpdateCounter */
    }

    /* selectionOut (TPML_PCR_SELECTION): count + count selections. */
    if (!rd_u32(rsp, rsp_len, &rpos, &sel_count) || sel_count != 1u ||
        !rd_u16(rsp, rsp_len, &rpos, &selection_alg) ||
        selection_alg != alg || rpos >= rsp_len) {
        return EFI_DEVICE_ERROR;
    }
    select_size = rsp[rpos++];
    if (select_size != 3u || rsp_len - rpos < 3u ||
        rsp[rpos] != 0xffu || rsp[rpos + 1u] != 0u ||
        rsp[rpos + 2u] != 0u) {
        return EFI_DEVICE_ERROR;
    }
    rpos += 3u;

    /* digests (TPML_DIGEST): count + count TPM2B_DIGEST{ size(2) buffer }. */
    if (!rd_u32(rsp, rsp_len, &rpos, &digest_count) ||
        digest_count != TCG2_PCR_READ_COUNT) {
        return EFI_DEVICE_ERROR;              /* we asked for exactly 8 PCRs */
    }

    for (i = 0; i < digest_count; i++) {
        uint16_t sz;
        uint16_t k;

        if (!rd_u16(rsp, rsp_len, &rpos, &sz) ||
            sz != expected_digest_size || rsp_len - rpos < sz) {
            return EFI_DEVICE_ERROR;
        }
        for (k = 0; k < expected_digest_size; k++) {
            out[i * expected_digest_size + k] = rsp[rpos + k];
        }
        rpos += sz;
    }

    if (rpos != rsp_len) {
        return EFI_DEVICE_ERROR;
    }
    if (digest_size != NULL) {
        *digest_size = expected_digest_size;
    }
    return EFI_SUCCESS;
}

EFI_STATUS tcg2_pcr_extend(EFI_TCG2_PROTOCOL *p, UINT32 pcr_index,
                           const tcg2_digest_value_t *digests,
                           UINT32 digest_count)
{
    uint8_t cmd[TCG2_EXTEND_CMD_CAP];
    uint8_t rsp[TCG2_RSP_CAP];
    size_t pos = 0;
    size_t rpos = 0;
    uint32_t i;
    uint32_t rsp_len;
    uint32_t response_code;
    uint16_t response_tag;
    EFI_STATUS st;

    if (p == NULL || digests == NULL || digest_count == 0u ||
        digest_count > 4u || pcr_index > 23u) {
        return EFI_INVALID_PARAMETER;
    }
    for (i = 0; i < digest_count; i++) {
        UINT16 expected = tcg2_digest_size(digests[i].algorithm);
        if (expected == 0u || digests[i].bytes == NULL ||
            digests[i].size != expected ||
            pos + 2u + expected > TCG2_EXTEND_CMD_CAP) {
            return EFI_INVALID_PARAMETER;
        }
    }

    /* TPM2_PCR_Extend:
     * header, PCR handle, password-session authorization, digest list. */
    wr_u16(cmd, &pos, TPM2_ST_SESSIONS);
    wr_u32(cmd, &pos, 0u);
    wr_u32(cmd, &pos, TPM2_CC_PCR_EXTEND);
    wr_u32(cmd, &pos, pcr_index);
    wr_u32(cmd, &pos, 9u);               /* authorizationSize */
    wr_u32(cmd, &pos, TPM2_RS_PW);
    wr_u16(cmd, &pos, 0u);               /* nonce size */
    cmd[pos++] = 0u;                      /* session attributes */
    wr_u16(cmd, &pos, 0u);               /* HMAC size */
    wr_u32(cmd, &pos, digest_count);
    for (i = 0; i < digest_count; i++) {
        UINT16 byte;
        wr_u16(cmd, &pos, digests[i].algorithm);
        for (byte = 0; byte < digests[i].size; byte++) {
            cmd[pos++] = digests[i].bytes[byte];
        }
    }
    patch_cmdsize(cmd, (uint32_t)pos);

    st = p->SubmitCommand(p, (UINT32)pos, cmd, TCG2_RSP_CAP, rsp);
    if (EFI_ERROR(st)) {
        return st;
    }
    if (!rd_u16(rsp, TCG2_RSP_CAP, &rpos, &response_tag) ||
        !rd_u32(rsp, TCG2_RSP_CAP, &rpos, &rsp_len) ||
        response_tag != TPM2_ST_SESSIONS || rsp_len < 10u ||
        rsp_len > TCG2_RSP_CAP) {
        return EFI_DEVICE_ERROR;
    }
    if (!rd_u32(rsp, rsp_len, &rpos, &response_code)) {
        return EFI_DEVICE_ERROR;
    }
    return response_code == TPM2_RC_SUCCESS ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}
