#ifndef COMBINED_VM_SMBIOS2_FIXTURE_H
#define COMBINED_VM_SMBIOS2_FIXTURE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SFB_EPS2_SIZE 31u
#define SFB_EPS3_SIZE 24u
#define SFB_MAX_TABLE_BYTES 65535u
#define SFB_MAX_STRUCTURES 2048u

typedef enum sfb_status {
    SFB_OK = 0,
    SFB_INVALID_ARGUMENT,
    SFB_EPS2_SIGNATURE,
    SFB_EPS2_CHECKSUM,
    SFB_EPS2_VERSION,
    SFB_EPS2_LAYOUT,
    SFB_TABLE_LAYOUT,
    SFB_TABLE_COUNT,
    SFB_REQUIRED_STRUCTURE,
    SFB_VM_IDENTITY_MISMATCH,
    SFB_UNEXPECTED_UNDERREPORT,
    SFB_EPS3_INVALID
} SFB_STATUS;

typedef struct sfb_vm_identity {
    const char *system_vendor;
    const char *system_product;
    const char *system_uuid;
} SFB_VM_IDENTITY;

typedef struct sfb_measurement {
    uint8_t major;
    uint8_t minor;
    uint16_t declared_maximum_structure_size;
    uint16_t declared_structure_count;
    uint16_t measured_maximum_structure_size;
    uint16_t measured_structure_count;
    uint16_t table_bytes;
    uint32_t table_address;
    uint8_t primary_checksum_before;
    uint8_t primary_checksum_after;
    uint8_t intermediate_checksum_before;
    uint8_t intermediate_checksum_after;
    uint8_t corrected;
} SFB_MEASUREMENT;

typedef struct sfb_ep3_metadata {
    uint8_t major;
    uint8_t minor;
    uint32_t maximum_table_bytes;
    uint64_t table_address;
} SFB_EP3_METADATA;

/* Validates the SMBIOS 3.x entry point without changing it. */
SFB_STATUS sfb_validate_eps3(const uint8_t *entry, size_t entry_size,
                             SFB_EP3_METADATA *metadata);

/*
 * Validates a 2.7 EPS/table and the exact VM identity, then corrects only the
 * primary checksum and advertised maximum-structure-size bytes when firmware
 * advertises the known underreported value 111. Table bytes are never changed.
 * A conforming EPS is returned byte-identically with corrected==0.
 */
SFB_STATUS sfb_prepare_eps2(const uint8_t *entry, size_t entry_size,
                            const uint8_t *table, size_t table_size,
                            const SFB_VM_IDENTITY *identity,
                            uint8_t corrected_entry[SFB_EPS2_SIZE],
                            SFB_MEASUREMENT *measurement);

const char *sfb_status_name(SFB_STATUS status);
uint8_t sfb_checksum(const uint8_t *bytes, size_t length);

#ifdef __cplusplus
}
#endif

#endif
