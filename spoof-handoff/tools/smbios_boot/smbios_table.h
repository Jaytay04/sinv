#ifndef SMBIOS_TABLE_H
#define SMBIOS_TABLE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum sm_field {
    SM_FIELD_SYSTEM_SERIAL = 0,
    SM_FIELD_SYSTEM_VENDOR = 1,
    SM_FIELD_SYSTEM_PRODUCT = 2,
    SM_FIELD_BOARD_SERIAL = 3,
    SM_FIELD_BOARD_VENDOR = 4,
    SM_FIELD_BOARD_PRODUCT = 5,
    SM_FIELD_SYSTEM_UUID = 6,
    SM_FIELD_COUNT = 7
} SM_FIELD;

#define SM_VALUE_CHARS 64u
#define SM_MAX_TABLE_BYTES (1024u * 1024u)
#define SM_MASK_SERIAL UINT32_C(0x00000001)
#define SM_MASK_ALL UINT32_C(0x0000007f)

typedef struct sm_values {
    char field[SM_FIELD_COUNT][SM_VALUE_CHARS];
    size_t table_bytes;
    unsigned structure_count;
    unsigned maximum_structure_size;
} SM_VALUES;

typedef enum sm_ep_kind {
    SM_EP_SMBIOS2 = 2,
    SM_EP_SMBIOS3 = 3
} SM_EP_KIND;

typedef struct sm_ep_info {
    SM_EP_KIND kind;
    uint8_t major;
    uint8_t minor;
    uint8_t document_revision;
    uint8_t revision;
    uint32_t table_bytes;
    uint64_t table_address;
    uint16_t structure_count;
    uint16_t maximum_structure_size;
} SM_EP_INFO;

/* Return 1 on success and 0 on invalid, ambiguous, or out-of-bounds input. */
int sm_read_table(const uint8_t *data, size_t size,
                  uint8_t major, uint8_t minor, SM_VALUES *values);

/*
 * Every member of locked_originals.field must exactly match the table, even
 * when its replacement bit is clear. Selected values are appended as new
 * string slots and redirected without changing shared original slots.
 * For mask bits, 1 means emit that replacement. Output must not overlap input,
 * either value structure, or output_size. On failure, output remains untouched
 * and output_size is set to zero when that pointer can safely be written.
 */
int sm_build_table(const uint8_t *input, size_t input_size,
                   uint8_t major, uint8_t minor, uint32_t mask,
                   const SM_VALUES *locked_originals,
                   const SM_VALUES *replacements,
                   uint8_t *output, size_t output_capacity,
                   size_t *output_size);

/* Restore an exact baseline copy; overlapping source/output ranges are refused. */
int sm_restore_table(const uint8_t *baseline, size_t baseline_size,
                     uint8_t *output, size_t output_capacity,
                     size_t *output_size);

/* SMBIOS 2.x entry points are 31 bytes; SMBIOS 3.x entry points are 24 bytes. */
int sm_parse_ep(const uint8_t *bytes, size_t length,
                SM_EP_KIND kind, SM_EP_INFO *info);

/*
 * Build a checksummed entry point referencing the caller's new table address.
 * SMBIOS 2.x uses table_bytes, structure_count, and maximum_structure_size.
 * SMBIOS 3.x uses table_bytes as Maximum Table Size and ignores the last two
 * metadata arguments, which are not represented in that entry point format.
 */
int sm_build_ep(const uint8_t *source, size_t source_length,
                SM_EP_KIND kind, uint64_t table_address,
                uint32_t table_bytes, uint16_t structure_count,
                uint16_t maximum_structure_size,
                uint8_t *output, size_t output_capacity,
                size_t *output_size);

#ifdef __cplusplus
}
#endif

#endif