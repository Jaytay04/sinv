#include "ib_format.h"
#include "ib_util.h"

typedef struct {
    char *buffer;
    size_t capacity;
    size_t written;
} ib_format_sink;

static void ib_format_putc(ib_format_sink *sink, char value) {
    if (sink->written + 1 < sink->capacity) {
        sink->buffer[sink->written] = value;
    }
    sink->written++;
}

static void ib_format_pad(ib_format_sink *sink, char value, size_t count) {
    while (count > 0) {
        ib_format_putc(sink, value);
        count--;
    }
}

static void ib_format_prefix(ib_format_sink *sink, const char *prefix) {
    size_t index;
    for (index = 0; prefix[index] != '\0'; index++) {
        ib_format_putc(sink, prefix[index]);
    }
}

static void ib_format_unsigned(ib_format_sink *sink, uint64_t value, unsigned base,
                               int uppercase, const char *prefix, int width,
                               int zero_pad, int left_align) {
    char digits[32];
    const char *alphabet = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    size_t count = 0;
    size_t prefix_len = ib_strlen(prefix);
    size_t total;
    size_t padding;
    size_t at;

    if (value == 0) {
        digits[count++] = '0';
    }
    while (value != 0) {
        digits[count++] = alphabet[value % base];
        value /= base;
    }

    total = count + prefix_len;
    padding = (width > (int)total) ? (size_t)width - total : 0;

    if (!left_align && !zero_pad) {
        ib_format_pad(sink, ' ', padding);
    }
    ib_format_prefix(sink, prefix);
    if (zero_pad && !left_align) {
        ib_format_pad(sink, '0', padding);
    }
    for (at = count; at > 0; at--) {
        ib_format_putc(sink, digits[at - 1]);
    }
    if (left_align) {
        ib_format_pad(sink, ' ', padding);
    }
}

int ib_vsnprintf(char *buffer, size_t capacity, const char *format, va_list arguments) {
    ib_format_sink sink;
    const char *at = format;

    sink.buffer = buffer;
    sink.capacity = capacity;
    sink.written = 0;

    while (*at != '\0') {
        int left_align = 0;
        int zero_pad = 0;
        int alternate = 0;
        int width = 0;
        int length = 0;
        char conversion;

        if (*at != '%') {
            ib_format_putc(&sink, *at++);
            continue;
        }
        at++;

        if (*at == '%') {
            ib_format_putc(&sink, '%');
            at++;
            continue;
        }

        for (;;) {
            if (*at == '-') {
                left_align = 1;
                at++;
            } else if (*at == '0') {
                zero_pad = 1;
                at++;
            } else if (*at == '#') {
                alternate = 1;
                at++;
            } else if (*at == '+' || *at == ' ') {
                at++;
            } else {
                break;
            }
        }

        while (*at >= '0' && *at <= '9') {
            width = width * 10 + (*at - '0');
            at++;
        }

        if (*at == '.') {
            at++;
            while (*at >= '0' && *at <= '9') {
                at++;
            }
        }

        if (*at == 'h') {
            length = 1;
            at++;
            if (*at == 'h') {
                length = 2;
                at++;
            }
        } else if (*at == 'l') {
            length = 3;
            at++;
            if (*at == 'l') {
                length = 4;
                at++;
            }
        } else if (*at == 'z' || *at == 't' || *at == 'j') {
            length = 5;
            at++;
        }

        conversion = *at;
        if (conversion == '\0') {
            break;
        }
        at++;

        switch (conversion) {
        case 's': {
            const char *text = va_arg(arguments, const char *);
            size_t text_len = text != 0 ? ib_strlen(text) : 0;
            size_t padding = (width > (int)text_len) ? (size_t)width - text_len : 0;
            size_t index;
            if (text == 0) {
                text = "(null)";
                text_len = 6;
                padding = (width > (int)text_len) ? (size_t)width - text_len : 0;
            }
            if (!left_align) {
                ib_format_pad(&sink, ' ', padding);
            }
            for (index = 0; index < text_len; index++) {
                ib_format_putc(&sink, text[index]);
            }
            if (left_align) {
                ib_format_pad(&sink, ' ', padding);
            }
            break;
        }
        case 'c': {
            char value = (char)va_arg(arguments, int);
            int padding = width > 1 ? width - 1 : 0;
            if (!left_align) {
                ib_format_pad(&sink, ' ', (size_t)padding);
            }
            ib_format_putc(&sink, value);
            if (left_align) {
                ib_format_pad(&sink, ' ', (size_t)padding);
            }
            break;
        }
        case 'd':
        case 'i': {
            int64_t value;
            int negative;
            uint64_t magnitude;
            if (length >= 3) {
                value = va_arg(arguments, int64_t);
            } else {
                value = (int64_t)va_arg(arguments, int);
            }
            negative = value < 0;
            magnitude = negative ? (uint64_t)(-(value + 1)) + 1u : (uint64_t)value;
            ib_format_unsigned(&sink, magnitude, 10, 0, negative ? "-" : "", width,
                               zero_pad, left_align);
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            uint64_t value;
            unsigned base = (conversion == 'u') ? 10u : 16u;
            const char *prefix = "";
            if (length >= 3) {
                value = va_arg(arguments, uint64_t);
            } else {
                value = (uint64_t)va_arg(arguments, unsigned int);
            }
            if (alternate && conversion != 'u') {
                prefix = (conversion == 'X') ? "0X" : "0x";
            }
            ib_format_unsigned(&sink, value, base, conversion == 'X', prefix, width,
                               zero_pad, left_align);
            break;
        }
        case 'p': {
            uint64_t value = (uint64_t)(uintptr_t)va_arg(arguments, void *);
            ib_format_unsigned(&sink, value, 16, 0, "0x", width, 1, 0);
            break;
        }
        default:
            ib_format_putc(&sink, '%');
            ib_format_putc(&sink, conversion);
            break;
        }
    }

    if (capacity > 0) {
        size_t end = sink.written < capacity ? sink.written : capacity - 1;
        sink.buffer[end] = '\0';
    }
    return (int)sink.written;
}

int ib_snprintf(char *buffer, size_t capacity, const char *format, ...) {
    va_list arguments;
    int written;
    va_start(arguments, format);
    written = ib_vsnprintf(buffer, capacity, format, arguments);
    va_end(arguments);
    return written;
}
