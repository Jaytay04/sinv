#ifndef IB_AUDIT_PLATFORM_WIN_H
#define IB_AUDIT_PLATFORM_WIN_H

#include <stddef.h>
#include <stdint.h>

#include "pcr.h"
#include "bootmgr.h"
#include "vars.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tbs.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    TBS_HCONTEXT context;
} ib_audit_windows_tpm_t;

int ib_audit_windows_tpm_open(ib_audit_windows_tpm_t *tpm,
                              uint16_t algorithms[AUDIT_PCR_MAX_BANKS],
                              uint32_t *algorithm_count);
void ib_audit_windows_tpm_close(ib_audit_windows_tpm_t *tpm);
int ib_audit_windows_tpm_major(uint32_t *major);
int ib_audit_windows_get_log(ib_audit_windows_tpm_t *tpm,
                             uint8_t **log, size_t *log_size);
int ib_audit_windows_find_esp(wchar_t *volume_path, size_t count);
int ib_audit_windows_hash_file(const wchar_t *path,
                               uint8_t digest[SHA256_DIGEST_SIZE],
                               void *context);
int ib_audit_windows_verify_microsoft(const wchar_t *path, void *context);
int ib_audit_windows_path_exists(const wchar_t *path, void *context);

int ib_audit_windows_hash(void *context, uint16_t algorithm,
                          const uint8_t *data, size_t data_size,
                          uint8_t *digest, size_t digest_capacity,
                          size_t *digest_size);
int ib_audit_windows_read_bank(void *context, uint16_t algorithm,
                               uint8_t *pcrs, size_t pcrs_capacity,
                               size_t digest_size);
int ib_audit_windows_read_variable(const wchar_t *name,
                                   const ib_audit_guid_t *guid,
                                   uint8_t *data, size_t capacity,
                                   size_t *data_size, uint32_t *attributes,
                                   void *context);

#ifdef __cplusplus
}
#endif

#endif
