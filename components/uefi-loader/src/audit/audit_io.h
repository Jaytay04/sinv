#ifndef IB_AUDIT_IO_H
#define IB_AUDIT_IO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IB_AUDIT_MAX_FILE_SIZE (64u * 1024u * 1024u)

int ib_audit_read_file(const char *path, uint8_t **bytes, size_t *size);

#ifdef __cplusplus
}
#endif

#endif
