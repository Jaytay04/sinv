/*
 * sha256.h - Freestanding SHA-256 one-shot hashing.
 *
 * This interface has no allocation, libc, UEFI, or platform dependencies.
 * The same implementation can therefore be linked by host tools and the
 * loader.  A zero-length input may be represented by a NULL data pointer;
 * non-empty inputs require a valid data pointer.
 */
#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHA256_DIGEST_SIZE 32u

typedef enum {
    SHA256_OK = 0,
    SHA256_ERR_ARGUMENT = 1
} sha256_status_t;

/* Hash exactly `length` bytes at `data` into the 32-byte `digest` buffer. */
sha256_status_t sha256_hash(const uint8_t *data,
                            size_t length,
                            uint8_t digest[SHA256_DIGEST_SIZE]);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SHA256_H */
