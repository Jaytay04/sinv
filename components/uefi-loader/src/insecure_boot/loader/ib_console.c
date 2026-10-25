/* See ib_console.h. */
#include "ib_console.h"

#include <stdarg.h>

#include "ib_format.h"
#include "ib_uefi.h"

/* The formatter needs a whole line at once; the longest line this port prints
 * is the "Setup refused the write ..." message, well under this. */
#define IB_CONSOLE_BUFFER 640

/* Convert and emit in bounded chunks so nothing has to be allocated. */
#define IB_CONSOLE_CHUNK 240

#ifdef IB_CONSOLE_SERIAL_MIRROR
static void ib_console_serial_char(char ch) {
    volatile unsigned char *lsr = (volatile unsigned char *)0x3fdu;
    volatile unsigned char *thr = (volatile unsigned char *)0x3f8u;
    unsigned long spin = 0;

    while (((*lsr) & 0x20u) == 0u && spin < 200000ul) {
        spin++;
    }
    *thr = (unsigned char)ch;
}

static void ib_console_serial_write(const char *text) {
    while (*text != '\0') {
        if (*text == '\n') {
            ib_console_serial_char('\r');
        }
        ib_console_serial_char(*text);
        text++;
    }
}
#endif

void ib_console_write(const char *text) {
    ib_efi_system_table *table = ib_efi_get_system_table();
    ib_efi_simple_text_output_protocol *out;
    ib_char16 units[IB_CONSOLE_CHUNK];
    size_t used = 0;

#ifdef IB_CONSOLE_SERIAL_MIRROR
    ib_console_serial_write(text);
#endif

    if (table == NULL) {
        return;
    }
    out = table->con_out;
    if (out == NULL) {
        return;
    }

    while (*text != '\0') {
        units[used++] = (ib_char16)(unsigned char)*text;
        text++;
        if (used == IB_CONSOLE_CHUNK - 1) {
            units[used] = 0;
            out->output_string(out, units);
            used = 0;
        }
    }
    if (used != 0) {
        units[used] = 0;
        out->output_string(out, units);
    }
}

void ib_console_printf(const char *format, ...) {
    char buffer[IB_CONSOLE_BUFFER];
    va_list arguments;

    va_start(arguments, format);
    ib_vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);

    ib_console_write(buffer);
}

void ib_console_println(const char *format, ...) {
    char buffer[IB_CONSOLE_BUFFER];
    va_list arguments;

    va_start(arguments, format);
    ib_vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);

    ib_console_write(buffer);
    ib_console_write("\n");
}
