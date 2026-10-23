/*
 * Tiny freestanding memory/string helpers.
 *
 * The EFI target is linked without a C runtime, so nothing here may call
 * libc.  Every function is pure and host-compilable.
 */
#ifndef IB_UTIL_H
#define IB_UTIL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *ib_memcpy(void *destination, const void *source, size_t length);
void *ib_memmove(void *destination, const void *source, size_t length);
void *ib_memset(void *destination, int value, size_t length);
int   ib_memcmp(const void *left, const void *right, size_t length);
size_t ib_strlen(const char *text);
int   ib_strcmp(const char *left, const char *right);
int   ib_strncmp(const char *left, const char *right, size_t length);
size_t ib_utf16_len(const uint16_t *text);
int   ib_utf16_equal(const uint16_t *left, const uint16_t *right);

#ifdef __cplusplus
}
#endif

#endif /* IB_UTIL_H */
