/* tcg2_test.c - Host tests for strict TPM2_PCR_Read response parsing. */
#include <stdio.h>
#include <string.h>

#include "tcg2.h"

EFI_SYSTEM_TABLE *gST = NULL;
EFI_BOOT_SERVICES *gBS = NULL;

static int g_checks;
static int g_failures;

#define CHECK(condition)                                                   \
    do {                                                                    \
        g_checks++;                                                         \
        if (!(condition)) {                                                 \
            g_failures++;                                                   \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);   \
        }                                                                   \
    } while (0)

static UINT8 g_response[1024];
static UINT32 g_response_size;
static UINT32 g_submit_count;

static void put_u16(UINT8 *buffer, UINT32 *offset, UINT16 value)
{
    buffer[(*offset)++] = (UINT8)(value >> 8);
    buffer[(*offset)++] = (UINT8)value;
}

static void put_u32(UINT8 *buffer, UINT32 *offset, UINT32 value)
{
    buffer[(*offset)++] = (UINT8)(value >> 24);
    buffer[(*offset)++] = (UINT8)(value >> 16);
    buffer[(*offset)++] = (UINT8)(value >> 8);
    buffer[(*offset)++] = (UINT8)value;
}

static void set_response_size(UINT32 size)
{
    g_response[2] = (UINT8)(size >> 24);
    g_response[3] = (UINT8)(size >> 16);
    g_response[4] = (UINT8)(size >> 8);
    g_response[5] = (UINT8)size;
    g_response_size = size;
}

static void make_valid_response(TPM_ALG_ID alg)
{
    UINT32 offset = 0;
    UINT32 i;
    UINT16 digest_size = tcg2_digest_size(alg);

    memset(g_response, 0, sizeof(g_response));
    put_u16(g_response, &offset, 0x8001u); /* TPM2_ST_NO_SESSIONS */
    put_u32(g_response, &offset, 0u);      /* responseSize, patched below */
    put_u32(g_response, &offset, 0u);      /* TPM2_RC_SUCCESS */
    put_u32(g_response, &offset, 7u);      /* pcrUpdateCounter */
    put_u32(g_response, &offset, 1u);      /* selection count */
    put_u16(g_response, &offset, alg);
    g_response[offset++] = 3u;             /* sizeofSelect */
    g_response[offset++] = 0xffu;          /* PCR 0-7 only */
    g_response[offset++] = 0u;
    g_response[offset++] = 0u;
    put_u32(g_response, &offset, 8u);      /* eight PCR digests */
    for (i = 0; i < 8u; i++) {
        UINT16 j;
        put_u16(g_response, &offset, digest_size);
        for (j = 0; j < digest_size; j++) {
            g_response[offset++] = (UINT8)(i + j + 1u);
        }
    }
    set_response_size(offset);
}

static EFI_STATUS EFIAPI mock_submit(EFI_TCG2_PROTOCOL *This,
                                     UINT32 InputParameterBlockSize,
                                     UINT8 *InputParameterBlock,
                                     UINT32 OutputParameterBlockSize,
                                     UINT8 *OutputParameterBlock)
{
    (void)This;
    g_submit_count++;
    CHECK(InputParameterBlockSize == 20u);
    CHECK(InputParameterBlock != NULL);
    if (InputParameterBlock != NULL) {
        CHECK(InputParameterBlock[0] == 0x80u &&
              InputParameterBlock[1] == 0x01u);
        CHECK(InputParameterBlock[14] == 0u &&
              InputParameterBlock[15] == TPM_ALG_SHA256);
        CHECK(InputParameterBlock[16] == 3u &&
              InputParameterBlock[17] == 0xffu &&
              InputParameterBlock[18] == 0u &&
              InputParameterBlock[19] == 0u);
    }
    if (OutputParameterBlock == NULL ||
        OutputParameterBlockSize < g_response_size) {
        return EFI_BUFFER_TOO_SMALL;
    }
    memcpy(OutputParameterBlock, g_response, g_response_size);
    return EFI_SUCCESS;
}

static EFI_TCG2_PROTOCOL g_protocol = {
    NULL, NULL, NULL, mock_submit, NULL, NULL, NULL
};

static void test_valid_response(void)
{
    UINT8 output[TCG2_PCR_READ_BYTES];
    UINT16 digest_size = 0;

    make_valid_response(TPM_ALG_SHA256);
    g_submit_count = 0;
    memset(output, 0, sizeof(output));
    CHECK(tcg2_pcr_read_bank(&g_protocol, TPM_ALG_SHA256, output,
                             &digest_size) == EFI_SUCCESS);
    CHECK(digest_size == 32u);
    CHECK(g_submit_count == 1u);
    CHECK(output[0] == 1u && output[31] == 32u);
    CHECK(output[32] == 2u && output[255] == 39u);
}

static void test_reject_selection_and_digest_shape(void)
{
    UINT8 output[TCG2_PCR_READ_BYTES];
    UINT16 digest_size;

    make_valid_response(TPM_ALG_SHA256);

    g_response[18] = 0u;
    g_response[19] = TPM_ALG_SHA1;
    CHECK(tcg2_pcr_read_bank(&g_protocol, TPM_ALG_SHA256, output,
                             &digest_size) == EFI_DEVICE_ERROR);

    make_valid_response(TPM_ALG_SHA256);
    g_response[14] = 0u;
    g_response[15] = 2u; /* two selections are not the requested one */
    CHECK(tcg2_pcr_read_bank(&g_protocol, TPM_ALG_SHA256, output,
                             &digest_size) == EFI_DEVICE_ERROR);

    make_valid_response(TPM_ALG_SHA256);
    g_response[20] = 2u; /* sizeofSelect must be exactly three */
    CHECK(tcg2_pcr_read_bank(&g_protocol, TPM_ALG_SHA256, output,
                             &digest_size) == EFI_DEVICE_ERROR);

    make_valid_response(TPM_ALG_SHA256);
    g_response[21] = 0x01u; /* PCR0 only, not the exact PCR0-7 selection */
    CHECK(tcg2_pcr_read_bank(&g_protocol, TPM_ALG_SHA256, output,
                             &digest_size) == EFI_DEVICE_ERROR);

    make_valid_response(TPM_ALG_SHA256);
    g_response[28] = 0u;
    g_response[29] = 31u; /* digest size must match SHA-256 exactly */
    CHECK(tcg2_pcr_read_bank(&g_protocol, TPM_ALG_SHA256, output,
                             &digest_size) == EFI_DEVICE_ERROR);
}

static void test_reject_truncation_trailing_and_tag(void)
{
    UINT8 output[TCG2_PCR_READ_BYTES];
    UINT16 digest_size;

    make_valid_response(TPM_ALG_SHA256);
    g_response_size--;
    set_response_size(g_response_size);
    CHECK(tcg2_pcr_read_bank(&g_protocol, TPM_ALG_SHA256, output,
                             &digest_size) == EFI_DEVICE_ERROR);

    make_valid_response(TPM_ALG_SHA256);
    g_response[g_response_size] = 0xa5u;
    set_response_size(g_response_size + 1u);
    CHECK(tcg2_pcr_read_bank(&g_protocol, TPM_ALG_SHA256, output,
                             &digest_size) == EFI_DEVICE_ERROR);

    make_valid_response(TPM_ALG_SHA256);
    g_response[0] = 0x80u;
    g_response[1] = 0x02u; /* response to PCR_Read must be no-sessions */
    CHECK(tcg2_pcr_read_bank(&g_protocol, TPM_ALG_SHA256, output,
                             &digest_size) == EFI_DEVICE_ERROR);
}

static void test_reject_unknown_algorithm(void)
{
    UINT8 output[TCG2_PCR_READ_BYTES];
    UINT16 digest_size = 99u;
    UINT32 submit_count;

    make_valid_response(TPM_ALG_SHA256);
    g_submit_count = 0;
    submit_count = g_submit_count;
    CHECK(tcg2_pcr_read_bank(&g_protocol, TPM_ALG_SM3_256, output,
                             &digest_size) == EFI_INVALID_PARAMETER);
    CHECK(digest_size == 0u);
    CHECK(g_submit_count == submit_count);
}

int main(void)
{
    test_valid_response();
    test_reject_selection_and_digest_shape();
    test_reject_truncation_trailing_and_tag();
    test_reject_unknown_algorithm();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
