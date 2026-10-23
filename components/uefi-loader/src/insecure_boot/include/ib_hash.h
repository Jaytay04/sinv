/*
 * Self-contained SHA-1/SHA-256/SHA-384/SHA-512.
 *
 * The EFI target links no crypto library, and the TCG2 protocol must hash a
 * measurement once per allocated bank.  All four contexts are plain values so a
 * caller can keep several in flight without allocation.
 */
#ifndef IB_HASH_H
#define IB_HASH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IB_SHA1_DIGEST_LEN   20u
#define IB_SHA256_DIGEST_LEN 32u
#define IB_SHA384_DIGEST_LEN 48u
#define IB_SHA512_DIGEST_LEN 64u

typedef struct {
    uint32_t state[5];
    uint64_t length;
    uint8_t block[64];
    size_t used;
} ib_sha1_ctx;

typedef struct {
    uint32_t state[8];
    uint64_t length;
    uint8_t block[64];
    size_t used;
} ib_sha256_ctx;

typedef struct {
    uint64_t state[8];
    uint64_t length_low;
    uint64_t length_high;
    uint8_t block[128];
    size_t used;
} ib_sha512_ctx;

void ib_sha1_init(ib_sha1_ctx *context);
void ib_sha1_update(ib_sha1_ctx *context, const void *data, size_t length);
void ib_sha1_final(ib_sha1_ctx *context, uint8_t digest[IB_SHA1_DIGEST_LEN]);

void ib_sha256_init(ib_sha256_ctx *context);
void ib_sha256_update(ib_sha256_ctx *context, const void *data, size_t length);
void ib_sha256_final(ib_sha256_ctx *context, uint8_t digest[IB_SHA256_DIGEST_LEN]);

void ib_sha384_init(ib_sha512_ctx *context);
void ib_sha384_update(ib_sha512_ctx *context, const void *data, size_t length);
void ib_sha384_final(ib_sha512_ctx *context, uint8_t digest[IB_SHA384_DIGEST_LEN]);

void ib_sha512_init(ib_sha512_ctx *context);
void ib_sha512_update(ib_sha512_ctx *context, const void *data, size_t length);
void ib_sha512_final(ib_sha512_ctx *context, uint8_t digest[IB_SHA512_DIGEST_LEN]);

#ifdef __cplusplus
}
#endif

#endif /* IB_HASH_H */
