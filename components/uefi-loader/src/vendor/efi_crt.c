/*
 * efi_crt.c — Freestanding CRT shim for MSVC EFI application builds.
 *
 * EFI images are linked with /NODEFAULTLIB, so the handful of C runtime
 * symbols the compiler may synthesize (or code may reference) must be supplied
 * here. Implementations are byte-exact copies of the standard semantics.
 *
 * Keep this file free of any OS or firmware dependency.
 */
#include <stddef.h>
#include <stdint.h>

/* Referenced by the compiler when floating point is present; keep it defined
 * so a link succeeds whether or not any translation unit uses floats. */
int _fltused = 0;

/* ------------------------------------------------------------------------- */
/* MSVC /RTC runtime-check helpers                                            */
/*                                                                           */
/* Debug builds enable /RTC1, which inserts calls to these CRT helpers. A     */
/* freestanding image (/NODEFAULTLIB) has no CRT, so provide no-op stubs.     */
/* ------------------------------------------------------------------------- */
typedef struct {
    int  addr;
    int  size;
    char *name;
} _RTC_vardesc;

typedef struct {
    int           varCount;
    _RTC_vardesc *variables;
} _RTC_framedesc;

void _RTC_InitBase(void)
{
}

void _RTC_Shutdown(void)
{
}

void _RTC_CheckStackVars(void *frame, _RTC_framedesc *fd)
{
    (void)frame;
    (void)fd;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n-- > 0) {
        *d++ = *s++;
    }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    while (n-- > 0) {
        *d++ = (unsigned char)c;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    size_t i;
    for (i = 0; i < n; i++) {
        if (x[i] != y[i]) {
            return (int)x[i] - (int)y[i];
        }
    }
    return 0;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) {
        return dst;
    }
    if (d < s) {
        while (n-- > 0) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n-- > 0) {
            *--d = *--s;
        }
    }
    return dst;
}
