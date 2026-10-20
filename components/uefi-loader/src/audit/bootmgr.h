/* bootmgr.h - Detector 4: Windows boot-manager integrity observations. */
#ifndef IB_AUDIT_BOOTMGR_H
#define IB_AUDIT_BOOTMGR_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#include "sha256.h"
#include "vars.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    IB_AUDIT_BOOTMGR_HASH_MISMATCH = 1u << 0,
    IB_AUDIT_BOOTMGR_AUTHENTICODE_INVALID = 1u << 1,
    IB_AUDIT_BOOTMGR_BACKUP_PRESENT = 1u << 2,
    IB_AUDIT_BOOTMGR_TRANSACTION_RESIDUE = 1u << 3,
    IB_AUDIT_BOOTMGR_STAGED_BOOTNEXT = 1u << 4,
    IB_AUDIT_BOOTMGR_STAGED_BOOTORDER = 1u << 5,
    IB_AUDIT_BOOTMGR_READ_FAILURE = 1u << 6,
    IB_AUDIT_BOOTMGR_BOOT_VARIABLE_MALFORMED = 1u << 7
};

enum {
    IB_AUDIT_BOOTMGR_RESIDUE_BAK = 1u << 0,
    IB_AUDIT_BOOTMGR_RESIDUE_IBNEW = 1u << 1,
    IB_AUDIT_BOOTMGR_RESIDUE_IBOLD = 1u << 2,
    IB_AUDIT_BOOTMGR_RESIDUE_HASH_RECORD = 1u << 3
};

typedef int (*ib_audit_bootmgr_hash_fn)(const wchar_t *path,
                                        uint8_t digest[SHA256_DIGEST_SIZE],
                                        void *context);
typedef int (*ib_audit_bootmgr_verify_fn)(const wchar_t *path, void *context);
typedef int (*ib_audit_bootmgr_exists_fn)(const wchar_t *path, void *context);

typedef struct {
    ib_audit_bootmgr_hash_fn hash_file;
    ib_audit_bootmgr_verify_fn verify_microsoft_authenticode;
    ib_audit_bootmgr_exists_fn path_exists;
    const ib_audit_variable_ops_t *variables;
    void *context;
} ib_audit_bootmgr_ops_t;

typedef struct {
    const wchar_t *esp_root;
    const uint8_t *expected_bootmgfw_sha256;
    const ib_audit_bootmgr_ops_t *ops;
} ib_audit_bootmgr_request_t;

#define IB_AUDIT_BOOTMGR_MAX_STAGED_ENTRIES 16u
#define IB_AUDIT_BOOTMGR_MAX_BOOT_ORDER 128u
typedef struct {
    uint32_t findings;
    uint32_t residue_mask;
    uint8_t bootmgfw_sha256[SHA256_DIGEST_SIZE];
    uint16_t staged_entries[IB_AUDIT_BOOTMGR_MAX_STAGED_ENTRIES];
    uint32_t staged_entry_count;
    uint16_t boot_order[IB_AUDIT_BOOTMGR_MAX_BOOT_ORDER];
    uint32_t boot_order_count;
    uint16_t boot_next;
    uint8_t boot_next_present;
} ib_audit_bootmgr_report_t;

typedef enum {
    IB_AUDIT_BOOTMGR_OK = 0,
    IB_AUDIT_BOOTMGR_INVALID_ARGUMENT = 1,
    IB_AUDIT_BOOTMGR_MEMORY_ERROR = 2
} ib_audit_bootmgr_status_t;

ib_audit_bootmgr_status_t ib_audit_bootmgr_integrity(
    const ib_audit_bootmgr_request_t *request,
    ib_audit_bootmgr_report_t *report);

#ifdef __cplusplus
}
#endif

#endif
