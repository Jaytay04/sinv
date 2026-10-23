#include "ib_util.h"

void *ib_memcpy(void *destination, const void *source, size_t length) {
    unsigned char *to = (unsigned char *)destination;
    const unsigned char *from = (const unsigned char *)source;
    size_t at;
    for (at = 0; at < length; at++) {
        to[at] = from[at];
    }
    return destination;
}

void *ib_memmove(void *destination, const void *source, size_t length) {
    unsigned char *to = (unsigned char *)destination;
    const unsigned char *from = (const unsigned char *)source;
    if (to == from || length == 0) {
        return destination;
    }
    if (to < from) {
        size_t at;
        for (at = 0; at < length; at++) {
            to[at] = from[at];
        }
    } else {
        size_t at = length;
        while (at > 0) {
            at--;
            to[at] = from[at];
        }
    }
    return destination;
}

void *ib_memset(void *destination, int value, size_t length) {
    unsigned char *to = (unsigned char *)destination;
    size_t at;
    for (at = 0; at < length; at++) {
        to[at] = (unsigned char)value;
    }
    return destination;
}

int ib_memcmp(const void *left, const void *right, size_t length) {
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;
    size_t at;
    for (at = 0; at < length; at++) {
        if (a[at] != b[at]) {
            return (int)a[at] - (int)b[at];
        }
    }
    return 0;
}

size_t ib_strlen(const char *text) {
    size_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

int ib_strcmp(const char *left, const char *right) {
    size_t at = 0;
    for (;;) {
        unsigned char a = (unsigned char)left[at];
        unsigned char b = (unsigned char)right[at];
        if (a != b) {
            return (int)a - (int)b;
        }
        if (a == 0) {
            return 0;
        }
        at++;
    }
}

int ib_strncmp(const char *left, const char *right, size_t length) {
    size_t at;
    for (at = 0; at < length; at++) {
        unsigned char a = (unsigned char)left[at];
        unsigned char b = (unsigned char)right[at];
        if (a != b) {
            return (int)a - (int)b;
        }
        if (a == 0) {
            return 0;
        }
    }
    return 0;
}

size_t ib_utf16_len(const uint16_t *text) {
    size_t length = 0;
    while (text[length] != 0) {
        length++;
    }
    return length;
}

int ib_utf16_equal(const uint16_t *left, const uint16_t *right) {
    size_t at = 0;
    for (;;) {
        uint16_t a = left[at];
        uint16_t b = right[at];
        if (a != b) {
            return 0;
        }
        if (a == 0) {
            return 1;
        }
        at++;
    }
}
