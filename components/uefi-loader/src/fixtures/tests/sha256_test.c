/* sha256_test.c - Host tests for the freestanding SHA-256 implementation. */
#include <stdio.h>
#include <string.h>

#include "sha256.h"

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, \
                #expression); \
        return 1; \
    } \
} while (0)

static int digest_equals_hex(const uint8_t digest[SHA256_DIGEST_SIZE],
                             const char *hex)
{
    static const char digits[] = "0123456789abcdef";
    uint32_t i;

    for (i = 0; i < SHA256_DIGEST_SIZE; i++) {
        if (hex[i * 2u] != digits[digest[i] >> 4] ||
            hex[i * 2u + 1u] != digits[digest[i] & 0x0fu]) {
            return 0;
        }
    }
    return 1;
}

static int test_known_vectors(void)
{
    static const uint8_t abc[] = { 'a', 'b', 'c' };
    static const uint8_t quick[] =
        "The quick brown fox jumps over the lazy dog";
    static const uint8_t quick_period[] =
        "The quick brown fox jumps over the lazy dog.";
    uint8_t digest[SHA256_DIGEST_SIZE];

    CHECK(sha256_hash(NULL, 0u, digest) == SHA256_OK);
    CHECK(digest_equals_hex(digest,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK(sha256_hash(abc, sizeof(abc), digest) == SHA256_OK);
    CHECK(digest_equals_hex(digest,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK(sha256_hash(quick, sizeof(quick) - 1u, digest) == SHA256_OK);
    CHECK(digest_equals_hex(digest,
        "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592"));
    CHECK(sha256_hash(quick_period, sizeof(quick_period) - 1u, digest) == SHA256_OK);
    CHECK(digest_equals_hex(digest,
        "ef537f25c895bfa782526529a9b63d97aa631564d5d789c2b765448c8635fb6c"));
    return 0;
}

static int test_binary_and_padding_boundaries(void)
{
    static const char *expected[] = {
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "e7cf46a078fed4fafd0b5e3aff144802b853f8ae459a4f0c14add3314b7cc3a6",
        "2900465fcb533e05a158fd2b3be0e5e3b03740d83060aa3580e0d98a96bf2384",
        "31454ff48ef36af2f08fd511bdc37d9d5855ac23e992e5ff5445cb6b7674a674",
        "5f6401b96532c36de4e65beec0409b69b1d181864c8009b7a04f43e5d56350d1",
        "94eb5de4943613fd048dc93393ab06877405faa39c11f53e9386083339833e7e",
        "fc518669b6eb4b4dd91827ecacef86689c725bd5bab888fd3b26dbb196eec954",
        "0fe729ff19257bd6fec853acc2ea355f6b34b58e6c0f684c3e188fcdfcd9baae",
        "0aedd4856f8eba0963627336ad5144a9a7dbe12498e6066f0165fc97d8ddee4c",
        "4f1757ae4bffbae86d775b831765b75af154d52f7deaa46dd378051a2d3ad57f"
    };
    static const size_t lengths[] =
        { 0u, 1u, 55u, 56u, 63u, 64u, 65u, 127u, 128u, 129u };
    uint8_t input[129];
    uint8_t digest[SHA256_DIGEST_SIZE];
    uint32_t i;
    size_t j;

    for (j = 0; j < sizeof(input); j++) {
        input[j] = (uint8_t)((j * 37u + 11u) & 0xffu);
    }
    for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        CHECK(sha256_hash(lengths[i] == 0u ? NULL : input,
                          lengths[i], digest) == SHA256_OK);
        CHECK(digest_equals_hex(digest, expected[i]));
    }
    return 0;
}

static int test_argument_validation(void)
{
    uint8_t digest[SHA256_DIGEST_SIZE];
    static const uint8_t byte = 0x5au;

    CHECK(sha256_hash(NULL, 1u, digest) == SHA256_ERR_ARGUMENT);
    CHECK(sha256_hash(&byte, 1u, NULL) == SHA256_ERR_ARGUMENT);
    CHECK(sha256_hash(NULL, 0u, NULL) == SHA256_ERR_ARGUMENT);
    return 0;
}

int main(void)
{
    if (test_known_vectors() != 0 ||
        test_binary_and_padding_boundaries() != 0 ||
        test_argument_validation() != 0) {
        return 1;
    }
    printf("sha256_test: PASS\n");
    return 0;
}
