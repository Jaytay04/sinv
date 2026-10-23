/*
 * Console output over the firmware's Simple Text Output protocol.
 *
 * The Rust crate writes with `println!`, which goes to the system table's
 * stdout after UTF-8 -> UCS-2 encoding.  Every string this port prints is
 * ASCII, so that encoding is a byte-for-byte widening; `ib_console_*` therefore
 * formats ASCII with `ib_snprintf` and hands the firmware UCS-2.
 */
#ifndef IB_CONSOLE_H
#define IB_CONSOLE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Writes a NUL-terminated ASCII string, encoding it as UCS-2. */
void ib_console_write(const char *text);

/* `print!`/`println!` with this port's minimal formatter. */
void ib_console_printf(const char *format, ...);
void ib_console_println(const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif /* IB_CONSOLE_H */
