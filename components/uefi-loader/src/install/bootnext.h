#ifndef IB_INSTALL_BOOTNEXT_H
#define IB_INSTALL_BOOTNEXT_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#define IB_BOOTNEXT_VARIABLE_ATTRIBUTES 0x00000007ul

typedef struct {
    uint32_t partition_number;
    uint64_t partition_start;
    uint64_t partition_size;
    unsigned char partition_guid[16];
} ib_bootnext_partition_t;

typedef int (*ib_bootnext_read_fn)(const wchar_t *name, unsigned char *data,
                                   size_t *data_size, unsigned long *attributes,
                                   void *context);
/* Read returns 1 when present, 0 when absent, and -1 on an unsafe/read error.
 * Write receives data_size zero only for deletion. */
typedef int (*ib_bootnext_write_fn)(const wchar_t *name,
                                    const unsigned char *data, size_t data_size,
                                    unsigned long attributes, void *context);

typedef struct {
    ib_bootnext_read_fn read_variable;
    ib_bootnext_write_fn write_variable;
    void *context;
} ib_bootnext_variable_ops_t;

typedef struct {
    ib_bootnext_partition_t partition;
    const ib_bootnext_variable_ops_t *ops;
} ib_bootnext_request_t;

int ib_bootnext_serialize(const ib_bootnext_partition_t *partition,
                          unsigned char *output, size_t *output_size);
int ib_bootnext_is_owned(const unsigned char *data, size_t data_size,
                         const ib_bootnext_partition_t *partition);
int ib_bootnext_set(const ib_bootnext_request_t *request,
                    unsigned short *boot_number,
                    wchar_t *error, size_t error_count);
int ib_bootnext_clear_owned(const ib_bootnext_request_t *request,
                            wchar_t *error, size_t error_count);
int ib_bootnext_enable_privilege(wchar_t *error, size_t error_count);
void ib_bootnext_default_ops(ib_bootnext_variable_ops_t *ops);

#endif
