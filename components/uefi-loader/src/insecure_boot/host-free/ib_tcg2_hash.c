#include "ib_tcg2.h"

#include "ib_util.h"

int ib_tcg2_supported(ib_tcglog_algorithm_t algorithm) {
    switch (algorithm) {
    case IB_TCGLOG_ALG_SHA1:
    case IB_TCGLOG_ALG_SHA256:
    case IB_TCGLOG_ALG_SHA384:
    case IB_TCGLOG_ALG_SHA512:
        return 1;
    default:
        return 0;
    }
}

ib_tcg2_code ib_tcg2_hasher_new(ib_tcg2_hasher_t *out,
                                const ib_tcglog_algorithm_t *banks,
                                size_t bank_count, ib_tcg2_error_t *error) {
    size_t index;

    ib_memset(out, 0, sizeof(*out));
    ib_sha1_init(&out->sha1);
    ib_sha256_init(&out->sha256);
    ib_sha384_init(&out->sha384);
    ib_sha512_init(&out->sha512);

    for (index = 0; index < bank_count; index++) {
        ib_tcglog_algorithm_t bank = banks[index];

        if (!ib_tcg2_supported(bank)) {
            if (error != 0) {
                error->code = IB_TCG2_ERR_UNSUPPORTED_BANK;
                error->algorithm = bank;
            }
            return IB_TCG2_ERR_UNSUPPORTED_BANK;
        }

        if (out->length >= IB_TPM2_BANK_COUNT_MAX) {
            if (error != 0) {
                error->code = IB_TCG2_ERR_TOO_MANY_BANKS;
                error->value = bank_count;
            }
            return IB_TCG2_ERR_TOO_MANY_BANKS;
        }

        out->banks[out->length] = bank;
        out->length++;
    }

    return IB_TCG2_OK;
}

void ib_tcg2_hasher_update(ib_tcg2_hasher_t *hasher,
                           const void *data, size_t length) {
    ib_sha1_update(&hasher->sha1, data, length);
    ib_sha256_update(&hasher->sha256, data, length);
    ib_sha384_update(&hasher->sha384, data, length);
    ib_sha512_update(&hasher->sha512, data, length);
}

static void ib_tcg2_digest_new(ib_tcg2_digest_t *digest,
                               ib_tcglog_algorithm_t algorithm,
                               const uint8_t *bytes, size_t length) {
    ib_memset(digest, 0, sizeof(*digest));
    digest->algorithm = algorithm;

    if (length <= IB_TPM2_DIGEST_LEN_MAX) {
        ib_memcpy(digest->bytes, bytes, length);
        digest->length = length;
    }
}

void ib_tcg2_hasher_finish(ib_tcg2_hasher_t *hasher, ib_tcg2_digests_t *out) {
    uint8_t sha1[IB_SHA1_DIGEST_LEN];
    uint8_t sha256[IB_SHA256_DIGEST_LEN];
    uint8_t sha384[IB_SHA384_DIGEST_LEN];
    uint8_t sha512[IB_SHA512_DIGEST_LEN];
    size_t index;

    ib_sha1_final(&hasher->sha1, sha1);
    ib_sha256_final(&hasher->sha256, sha256);
    ib_sha384_final(&hasher->sha384, sha384);
    ib_sha512_final(&hasher->sha512, sha512);

    ib_memset(out, 0, sizeof(*out));

    for (index = 0; index < hasher->length; index++) {
        ib_tcglog_algorithm_t bank = hasher->banks[index];
        ib_tcg2_digest_t *slot;

        if (out->length >= IB_TPM2_BANK_COUNT_MAX) {
            break;
        }
        slot = &out->entries[out->length];

        switch (bank) {
        case IB_TCGLOG_ALG_SHA1:
            ib_tcg2_digest_new(slot, bank, sha1, sizeof(sha1));
            break;
        case IB_TCGLOG_ALG_SHA256:
            ib_tcg2_digest_new(slot, bank, sha256, sizeof(sha256));
            break;
        case IB_TCGLOG_ALG_SHA384:
            ib_tcg2_digest_new(slot, bank, sha384, sizeof(sha384));
            break;
        case IB_TCGLOG_ALG_SHA512:
            ib_tcg2_digest_new(slot, bank, sha512, sizeof(sha512));
            break;
        default:
            continue;
        }

        out->length++;
    }
}

const uint8_t *ib_tcg2_digest_bytes(const ib_tcg2_digest_t *digest,
                                    size_t *out_length) {
    if (out_length != 0) {
        *out_length = digest->length;
    }
    return digest->bytes;
}

size_t ib_tcg2_digests_carry(const ib_tcg2_digests_t *digests,
                             ib_tpm2_digest_t *carried, size_t capacity) {
    size_t written = 0;

    for (size_t index = 0; index < digests->length; index++) {
        const ib_tcg2_digest_t *digest = &digests->entries[index];

        if (written >= capacity) {
            break;
        }

        carried[written].algorithm = digest->algorithm;
        carried[written].bytes = digest->bytes;
        carried[written].length = digest->length;
        written++;
    }

    return written;
}
