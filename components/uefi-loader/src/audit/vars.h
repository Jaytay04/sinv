/*
 * vars.h - Read-only audit of firmware variables used by Secure Boot.
 *
 * The audit never writes variables.  Callers inject a read-only callback so
 * the same policy can be exercised with Windows firmware APIs and host fakes.
 */
#ifndef IB_AUDIT_VARS_H
#define IB_AUDIT_VARS_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#include "sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
} ib_audit_guid_t;

/* Status values returned by an injected read callback. */
typedef enum {
    IB_AUDIT_READ_ABSENT = 0,
    IB_AUDIT_READ_PRESENT = 1,
    IB_AUDIT_READ_ERROR = -1,
    /* `*data_size` is the required size; no bytes were returned. */
    IB_AUDIT_READ_BUFFER_TOO_SMALL = 2
} ib_audit_read_status_t;

/*
 * Read one variable.  A first call with data == NULL and capacity == 0 is
 * used to discover the size.  For an existing non-empty variable the callback
 * returns IB_AUDIT_READ_BUFFER_TOO_SMALL and sets *data_size.  It then returns
 * IB_AUDIT_READ_PRESENT after copying exactly *data_size bytes.  A missing
 * variable returns IB_AUDIT_READ_ABSENT, while an unsafe/API failure returns
 * IB_AUDIT_READ_ERROR.  No callback operation may write a variable.
 */
typedef int (*ib_audit_read_variable_fn)(const wchar_t *name,
                                         const ib_audit_guid_t *guid,
                                         uint8_t *data, size_t capacity,
                                         size_t *data_size,
                                         uint32_t *attributes,
                                         void *context);

typedef struct {
    ib_audit_read_variable_fn read_variable;
    void *context;
} ib_audit_variable_ops_t;

typedef enum {
    IB_AUDIT_VARIABLE_MOK_LIST = 0,
    IB_AUDIT_VARIABLE_MOK_LIST_RT,
    IB_AUDIT_VARIABLE_SECURE_BOOT,
    IB_AUDIT_VARIABLE_PK,
    IB_AUDIT_VARIABLE_KEK,
    IB_AUDIT_VARIABLE_DB,
    IB_AUDIT_VARIABLE_DBX,
    IB_AUDIT_VARIABLE_BOOT_ORDER,
    IB_AUDIT_VARIABLE_BOOT_NEXT,
    IB_AUDIT_VARIABLE_COUNT
} ib_audit_variable_id_t;

typedef enum {
    IB_AUDIT_VARIABLE_UNREAD = 0,
    IB_AUDIT_VARIABLE_ABSENT,
    IB_AUDIT_VARIABLE_PRESENT,
    IB_AUDIT_VARIABLE_READ_FAILED,
    IB_AUDIT_VARIABLE_MALFORMED
} ib_audit_variable_state_t;

#define IB_AUDIT_MAX_FINGERPRINTS 128u

typedef struct {
    ib_audit_variable_state_t state;
    size_t size;
    uint32_t attributes;
    uint32_t certificate_count;
    uint32_t unknown_certificate_count;
    /* SHA-256 fingerprints in encounter order; populated for X.509 lists. */
    uint8_t fingerprints[IB_AUDIT_MAX_FINGERPRINTS][SHA256_DIGEST_SIZE];
    uint32_t fingerprint_count;
} ib_audit_variable_observation_t;

enum {
    IB_AUDIT_FINDING_UNKNOWN_MOK = 1u << 0,
    IB_AUDIT_FINDING_MALFORMED_VARIABLE = 1u << 1,
    IB_AUDIT_FINDING_SECURE_BOOT_DISABLED = 1u << 2,
    IB_AUDIT_FINDING_BOOTNEXT_MISSING_TARGET = 1u << 3,
    IB_AUDIT_FINDING_VARIABLE_READ_FAILURE = 1u << 4
};

typedef struct {
    uint32_t findings;
    ib_audit_variable_observation_t variables[IB_AUDIT_VARIABLE_COUNT];
} ib_audit_report_t;

typedef enum {
    IB_AUDIT_OK = 0,
    IB_AUDIT_ERR_INVALID_ARGUMENT = 1,
    IB_AUDIT_ERR_MEMORY = 2
} ib_audit_status_t;

/* GUIDs used by the shim variables and global EFI variables. */
extern const ib_audit_guid_t IB_AUDIT_SHIM_GUID;
extern const ib_audit_guid_t IB_AUDIT_GLOBAL_GUID;

/*
 * Audit the Secure Boot and boot-selection variables exposed by `ops`.
 * `expected_mok_sha256` contains SHA-256 fingerprints of trusted X.509 MOK
 * certificates.  The array may be NULL only when expected_count is zero.
 * Findings are policy observations; a successful return does not imply that
 * findings is zero.  The report is reset before any reads are made.
 */
ib_audit_status_t ib_audit_variables(
    const ib_audit_variable_ops_t *ops,
    const uint8_t (*expected_mok_sha256)[SHA256_DIGEST_SIZE],
    size_t expected_count,
    ib_audit_report_t *report);

/* Compare two GUID values without relying on platform EFI headers. */
int ib_audit_guid_equal(const ib_audit_guid_t *left,
                        const ib_audit_guid_t *right);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* IB_AUDIT_VARS_H */
