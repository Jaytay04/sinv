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

typedef struct {
    /* Injected Win32 boundary; the table must outlive the privilege scope. */
    void *context;
    HANDLE (*get_current_process)(void *context);
    BOOL (*open_process_token)(void *context, HANDLE process, DWORD access,
                               PHANDLE token);
    BOOL (*lookup_privilege_value)(void *context, LPCWSTR system_name,
                                   LPCWSTR name, PLUID luid);
    BOOL (*adjust_token_privileges)(void *context, HANDLE token,
                                   BOOL disable_all,
                                   PTOKEN_PRIVILEGES new_state,
                                   DWORD buffer_length,
                                   PTOKEN_PRIVILEGES previous_state,
                                   PDWORD return_length);
    BOOL (*close_handle)(void *context, HANDLE handle);
    DWORD (*get_last_error)(void *context);
    void (*set_last_error)(void *context, DWORD error);
} ib_audit_firmware_privilege_api_t;

typedef struct {
    HANDLE token;
    TOKEN_PRIVILEGES previous;
    DWORD previous_size;
    int active;
    int restore_required;
    const ib_audit_firmware_privilege_api_t *api;
} ib_audit_firmware_privilege_t;

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
int ib_audit_windows_firmware_privilege_begin(
    ib_audit_firmware_privilege_t *privilege, DWORD *error_code);
int ib_audit_windows_firmware_privilege_begin_with_api(
    ib_audit_firmware_privilege_t *privilege,
    const ib_audit_firmware_privilege_api_t *api, DWORD *error_code);
int ib_audit_windows_firmware_privilege_end(
    ib_audit_firmware_privilege_t *privilege, DWORD *error_code);

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
