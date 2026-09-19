/* audit_dump_test.c - Detector 1 synthetic fixtures. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "dump.h"
#include "sha256.h"
#include "tcglog.h"
#include "tcglog_ib.h"

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, \
                #expression); \
        return 1; \
    } \
} while (0)

typedef struct {
    unsigned char bytes[4096];
    size_t length;
} fixture_t;

static FILE *open_output(void)
{
#if defined(_MSC_VER)
    FILE *stream = NULL;
    if (tmpfile_s(&stream) != 0) {
        return NULL;
    }
    return stream;
#else
    return tmpfile();
#endif
}

static void put_u16(fixture_t *fixture, unsigned int value)
{
    fixture->bytes[fixture->length++] = (unsigned char)value;
    fixture->bytes[fixture->length++] = (unsigned char)(value >> 8);
}

static void put_u32(fixture_t *fixture, uint32_t value)
{
    fixture->bytes[fixture->length++] = (unsigned char)value;
    fixture->bytes[fixture->length++] = (unsigned char)(value >> 8);
    fixture->bytes[fixture->length++] = (unsigned char)(value >> 16);
    fixture->bytes[fixture->length++] = (unsigned char)(value >> 24);
}

static void put_bytes(fixture_t *fixture, const void *bytes, size_t length)
{
    (void)memcpy(fixture->bytes + fixture->length, bytes, length);
    fixture->length += length;
}

static void build_fixture(fixture_t *fixture, int tamper_digest,
                          int no_action)
{
    static const unsigned char event_data[] = { 'b', 'o', 'o', 't' };
    unsigned char digest[SHA256_DIGEST_SIZE];
    size_t payload_start;
    size_t digest_offset;

    (void)memset(fixture, 0, sizeof(*fixture));
    /* Reserve the fixed tcglog.ib header. */
    fixture->length = 16u;
    payload_start = fixture->length;
    put_bytes(fixture, "Spec ID Event03", 16u);
    put_u32(fixture, 0u); /* platformClass */
    put_bytes(fixture, "\0\2\0\4", 4u);
    put_u32(fixture, 1u); /* algorithm count */
    put_u16(fixture, TCGLOG_ALG_SHA256);
    put_u16(fixture, SHA256_DIGEST_SIZE);
    fixture->bytes[fixture->length++] = 0u; /* vendorInfoSize */

    put_u32(fixture, 0u); /* PCR */
    put_u32(fixture, no_action ? TCGLOG_EVT_NO_ACTION :
             TCGLOG_EVT_ACTION);
    put_u32(fixture, 1u); /* digest count */
    put_u16(fixture, TCGLOG_ALG_SHA256);
    digest_offset = fixture->length;
    if (no_action != 0) {
        (void)memset(digest, 0, sizeof(digest));
    } else {
        if (sha256_hash(event_data, sizeof(event_data), digest) != SHA256_OK) {
            (void)memset(digest, 0, sizeof(digest));
        }
    }
    put_bytes(fixture, digest, sizeof(digest));
    put_u32(fixture, (uint32_t)sizeof(event_data));
    put_bytes(fixture, event_data, sizeof(event_data));

    if (tamper_digest != 0) {
        fixture->bytes[digest_offset] ^= 0x80u;
    }
    fixture->bytes[0] = TCGLOG_IB_MAGIC0;
    fixture->bytes[1] = TCGLOG_IB_MAGIC1;
    fixture->bytes[2] = TCGLOG_IB_MAGIC2;
    fixture->bytes[3] = TCGLOG_IB_MAGIC3;
    fixture->bytes[4] = (unsigned char)TCGLOG_IB_VERSION;
    fixture->bytes[5] = 0u;
    fixture->bytes[6] = (unsigned char)TCGLOG_IB_FLAG_CRYPTO_AGILE;
    fixture->bytes[7] = 0u;
    fixture->bytes[8] = 1u; /* event count */
    fixture->bytes[9] = 0u;
    fixture->bytes[10] = 0u;
    fixture->bytes[11] = 0u;
    /* Write payload length directly; keeping this explicit avoids relying on
     * host struct packing for the on-disk container header. */
    fixture->bytes[12] = (unsigned char)((fixture->length - payload_start) & 0xffu);
    fixture->bytes[13] = (unsigned char)((fixture->length - payload_start) >> 8);
    fixture->bytes[14] = (unsigned char)((fixture->length - payload_start) >> 16);
    fixture->bytes[15] = (unsigned char)((fixture->length - payload_start) >> 24);
}

static int read_output(FILE *stream, char *buffer, size_t capacity)
{
    size_t length;

    CHECK(fseek(stream, 0L, SEEK_SET) == 0);
    length = fread(buffer, 1u, capacity - 1u, stream);
    buffer[length] = '\0';
    return (int)length;
}

static int test_clean_dump(void)
{
    fixture_t fixture;
    FILE *output = open_output();
    char report[4096];
    audit_dump_status_t status;

    CHECK(output != NULL);
    build_fixture(&fixture, 0, 0);
    status = audit_dump_buffer(fixture.bytes, fixture.length, output);
    CHECK(status == AUDIT_DUMP_OK);
    (void)read_output(output, report, sizeof(report));
    CHECK(strstr(report, "EventNum=0 PCR=0 Type=0x00000005 DigestCount=1 EventSize=4") != NULL);
    CHECK(strstr(report, "Recomputed=SHA256 MATCH") != NULL);
    CHECK(strstr(report, "StructuralFindings=0 DigestFindings=0") != NULL);
    CHECK(strcmp(audit_dump_status_name(status), "ok") == 0);
    (void)fclose(output);
    return 0;
}

static int test_digest_mismatch(void)
{
    fixture_t fixture;
    FILE *output = open_output();
    char report[4096];
    audit_dump_status_t status;

    CHECK(output != NULL);
    build_fixture(&fixture, 1, 0);
    status = audit_dump_buffer(fixture.bytes, fixture.length, output);
    CHECK(status == AUDIT_DUMP_ERR_DIGEST);
    (void)read_output(output, report, sizeof(report));
    CHECK(strstr(report, "DIGEST_MISMATCH EventNum=0 PCR=0") != NULL);
    CHECK(strstr(report, "DigestFindings=1") != NULL);
    (void)fclose(output);
    return 0;
}

static int test_no_action_validation_and_bound(void)
{
    fixture_t fixture;
    FILE *output = open_output();
    audit_dump_status_t status;
    unsigned char empty = 0u;

    CHECK(output != NULL);
    build_fixture(&fixture, 0, 1);
    status = audit_dump_buffer(fixture.bytes, fixture.length, output);
    CHECK(status == AUDIT_DUMP_OK);
    (void)fclose(output);

    output = open_output();
    CHECK(output != NULL);
    status = audit_dump_buffer(fixture.bytes, fixture.length + 1u, output);
    CHECK(status == AUDIT_DUMP_ERR_STRUCT);
    (void)fclose(output);

    output = open_output();
    CHECK(output != NULL);
    status = audit_dump_buffer(&empty, 0u, output);
    CHECK(status == AUDIT_DUMP_ERR_STRUCT);
    (void)fclose(output);
    return 0;
}

static int test_image_digest_is_not_recomputed_from_device_path(void)
{
    fixture_t fixture;
    FILE *output = open_output();
    char report[4096];
    audit_dump_status_t status;
    size_t event_type_offset = 16u + 33u + 4u;

    CHECK(output != NULL);
    build_fixture(&fixture, 1, 0);
    fixture.bytes[event_type_offset] = 0x03u;
    fixture.bytes[event_type_offset + 1u] = 0x00u;
    fixture.bytes[event_type_offset + 2u] = 0x00u;
    fixture.bytes[event_type_offset + 3u] = 0x80u;
    status = audit_dump_buffer(fixture.bytes, fixture.length, output);
    CHECK(status == AUDIT_DUMP_OK);
    (void)read_output(output, report, sizeof(report));
    CHECK(strstr(report,
                 "EventNum=0 PCR=0 Recomputed=UNAVAILABLE Type=0x80000003") != NULL);
    CHECK(strstr(report, "DIGEST_MISMATCH") == NULL);
    (void)fclose(output);
    return 0;
}

int main(void)
{
    if (test_clean_dump() != 0 ||
        test_digest_mismatch() != 0 ||
        test_no_action_validation_and_bound() != 0 ||
        test_image_digest_is_not_recomputed_from_device_path() != 0) {
        return 1;
    }
    printf("audit_dump_test: PASS\n");
    return 0;
}
