/*
 * tcgdump_util.c — Shared formatting/classification helpers for ib-tcg-dump.
 */
#include <stdio.h>
#include <string.h>

#include "tcgdump.h"

const char *tbs_result_name(uint32_t r)
{
    switch (r) {
    case 0x00000000u:               return "TBS_SUCCESS";
    case 0x80280001u:               return "TBS_E_INTERNAL_ERROR";
    case 0x80280002u:               return "TBS_E_BAD_PARAMETER";
    case 0x80280003u:               return "TBS_E_INVALID_OUTPUT_POINTER";
    case 0x80280004u:               return "TBS_E_INVALID_CONTEXT";
    case 0x80280005u:               return "TBS_E_INSUFFICIENT_BUFFER";
    case 0x80280006u:               return "TBS_E_IOERROR";
    case 0x80280007u:               return "TBS_E_INVALID_CONTEXT_PARAM";
    case 0x80280008u:               return "TBS_E_SERVICE_NOT_RUNNING";
    case 0x80280009u:               return "TBS_E_TOO_MANY_TBS_CONTEXTS";
    case 0x8028000Au:               return "TBS_E_TOO_MANY_RESOURCES";
    case 0x8028000Bu:               return "TBS_E_SERVICE_START_PENDING";
    case 0x8028000Cu:               return "TBS_E_PPI_NOT_SUPPORTED";
    case 0x8028000Du:               return "TBS_E_COMMAND_CANCELED";
    case 0x8028000Eu:               return "TBS_E_BUFFER_TOO_LARGE";
    case 0x8028000Fu:               return "TBS_E_TPM_NOT_FOUND";
    case 0x80280010u:               return "TBS_E_SERVICE_DISABLED";
    case 0x80280011u:               return "TBS_E_NO_EVENT_LOG";
    case 0x80280012u:               return "TBS_E_ACCESS_DENIED";
    case 0x80280013u:               return "TBS_E_PROVISIONING_NOT_ALLOWED";
    case 0x80280014u:               return "TBS_E_PPI_FUNCTION_UNSUPPORTED";
    case 0x80280015u:               return "TBS_E_PROVISIONING_UNSUPPORTED";
    case 0x80280016u:               return "TBS_E_TPM_VERSION_UNSUPPORTED";
    default:                        return "TBS_E_UNKNOWN";
    }
}

tcgdump_log_format_t tcgdump_detect_log(const uint8_t *buf, size_t len,
                                        size_t *specid_off, char signature[16])
{
    if (specid_off != NULL) {
        *specid_off = 0;
    }
    if (signature != NULL) {
        memset(signature, 0, 16);
    }
    if (buf == NULL || len < 16) {
        return TCGDUMP_LOG_UNKNOWN;
    }

    /* Raw crypto-agile layout: signature at offset 0. */
    if (memcmp(buf, "Spec ID Event03", 16) == 0) {
        if (signature != NULL) {
            memcpy(signature, buf, 16);
        }
        return TCGDUMP_LOG_CRYPTO_AGILE;
    }
    if (memcmp(buf, "Spec ID Event00", 16) == 0) {
        if (signature != NULL) {
            memcpy(signature, buf, 16);
        }
        return TCGDUMP_LOG_SHA1;
    }

    /* Wrapped layout: TCG_PCR_EVENT { PCRIndex=0, EV_NO_ACTION (3),
     * SHA-1 digest[20], EventSize } followed by the Spec ID header. The
     * signature then begins at offset 32. */
    if (len >= 48) {
        uint32_t pcr = (uint32_t)buf[0]
                     | ((uint32_t)buf[1] << 8)
                     | ((uint32_t)buf[2] << 16)
                     | ((uint32_t)buf[3] << 24);
        uint32_t etype = (uint32_t)buf[4]
                       | ((uint32_t)buf[5] << 8)
                       | ((uint32_t)buf[6] << 16)
                       | ((uint32_t)buf[7] << 24);
        if (pcr == 0 && etype == 3 /* EV_NO_ACTION */) {
            if (memcmp(buf + 32, "Spec ID Event03", 16) == 0) {
                if (signature != NULL) {
                    memcpy(signature, buf + 32, 16);
                }
                if (specid_off != NULL) {
                    *specid_off = 32;
                }
                return TCGDUMP_LOG_CRYPTO_AGILE;
            }
            if (memcmp(buf + 32, "Spec ID Event00", 16) == 0) {
                if (signature != NULL) {
                    memcpy(signature, buf + 32, 16);
                }
                return TCGDUMP_LOG_SHA1;
            }
        }
    }

    return TCGDUMP_LOG_UNKNOWN;
}

void tcgdump_hex(const uint8_t *buf, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        printf("%02x", buf[i]);
    }
}

int tcgdump_is_zero(const uint8_t *buf, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (buf[i] != 0) {
            return 0;
        }
    }
    return 1;
}
