#include <stdio.h>
#include <string.h>

#include "baseline.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; \
} } while (0)

static void make_state(ib_audit_baseline_t *state)
{
    uint32_t pcr;

    memset(state, 0, sizeof(*state));
    state->bank_count = 1u;
    state->banks[0].algorithm = 0x000bu;
    state->banks[0].digest_size = 32u;
    for (pcr = 0u; pcr < IB_AUDIT_BASELINE_PCR_COUNT; ++pcr) {
        memset(state->banks[0].pcrs[pcr], (int)(pcr + 1u), 32u);
    }
    memset(state->log_sha256, 0x11, sizeof(state->log_sha256));
    state->mok_count = 1u;
    memset(state->mok_sha256[0], 0x22, SHA256_DIGEST_SIZE);
    memset(state->bootmgfw_sha256, 0x33, sizeof(state->bootmgfw_sha256));
    state->boot_order_count = 2u;
    state->boot_order[0] = 7u;
    state->boot_order[1] = 1u;
    state->boot_next_present = 1u;
    state->boot_next = 7u;
}

static int test_round_trip(void)
{
    ib_audit_baseline_t expected;
    ib_audit_baseline_t decoded;
    char json[IB_AUDIT_BASELINE_JSON_MAX];
    size_t size = 0u;

    make_state(&expected);
    CHECK(ib_audit_baseline_encode(&expected, json, sizeof(json), &size) ==
          IB_AUDIT_BASELINE_OK);
    CHECK(size != 0u && size < sizeof(json));
    CHECK(ib_audit_baseline_decode(json, size, &decoded) ==
          IB_AUDIT_BASELINE_OK);
    CHECK(memcmp(&expected, &decoded, sizeof(expected)) == 0);
    json[size - 2u] = ']';
    CHECK(ib_audit_baseline_decode(json, size, &decoded) ==
          IB_AUDIT_BASELINE_INVALID_JSON);
    return 0;
}

static int test_mapped_drift(void)
{
    ib_audit_baseline_t before;
    ib_audit_baseline_t after;
    ib_audit_baseline_diff_t diff;

    make_state(&before);
    after = before;
    after.mok_count = 2u;
    memset(after.mok_sha256[1], 0x44, SHA256_DIGEST_SIZE);
    memset(after.bootmgfw_sha256, 0x55, SHA256_DIGEST_SIZE);
    CHECK(ib_audit_baseline_diff(&before, &after, &diff) ==
          IB_AUDIT_BASELINE_OK);
    CHECK(diff.drift == (IB_AUDIT_DRIFT_MOK | IB_AUDIT_DRIFT_BOOTMGFW));
    CHECK(diff.detector_mask == (IB_AUDIT_DIFF_DETECTOR_3 |
                                 IB_AUDIT_DIFF_DETECTOR_4));
    CHECK(diff.added_mok_count == 1u && diff.removed_mok_count == 0u);

    after = before;
    after.banks[0].pcrs[7][0] ^= 1u;
    CHECK(ib_audit_baseline_diff(&before, &after, &diff) ==
          IB_AUDIT_BASELINE_OK);
    CHECK(diff.drift == IB_AUDIT_DRIFT_PCR);
    CHECK(diff.detector_mask == IB_AUDIT_DIFF_DETECTOR_2);
    return 0;
}

int main(void)
{
    if (test_round_trip() != 0 || test_mapped_drift() != 0) return 1;
    puts("PASS: audit baseline JSON round-trip and detector drift mapping");
    return 0;
}
