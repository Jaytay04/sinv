/*
 * Minimal freestanding printf-family formatter.
 *
 * Supports the conversions this port uses: %s %c %d %i %u %x %X %p %%, with
 * '-', '0', '#' flags, a decimal width, and h/hh/l/ll/z/t/j length modifiers.
 * The '#' flag adds the 0x/0X prefix the Rust {:#x} spelling asks for; the
 * prefix counts toward the width, with zero padding landing after it.
 * Precision is accepted and ignored for strings only where it is harmless;
 * this port never asks for a precision.
 */
#ifndef IB_FORMAT_H
#define IB_FORMAT_H

#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

int ib_vsnprintf(char *buffer, size_t capacity, const char *format, va_list arguments);
int ib_snprintf(char *buffer, size_t capacity, const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif /* IB_FORMAT_H */
