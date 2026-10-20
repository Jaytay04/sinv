#include "platform_win.h"

#include <bcrypt.h>
#include <softpub.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wincrypt.h>
#include <winioctl.h>
#include <wintrust.h>

#include "tbs_tcgdump.h"

#define IB_AUDIT_VARIABLE_PROBE_MAX (16u * 1024u * 1024u)

static const GUID g_audit_esp_partition_type = {
    0xC12A7328u, 0xF81Fu, 0x11D2u,
    { 0xBAu, 0x4Bu, 0x00u, 0xA0u, 0xC9u, 0x3Eu, 0xC9u, 0x3Bu }
};

static LPCWSTR audit_bcrypt_algorithm(uint16_t algorithm)
{
    switch (algorithm) {
    case 0x0004u:
        return BCRYPT_SHA1_ALGORITHM;
    case 0x000bu:
        return BCRYPT_SHA256_ALGORITHM;
    case 0x000cu:
        return BCRYPT_SHA384_ALGORITHM;
    case 0x000du:
        return BCRYPT_SHA512_ALGORITHM;
    default:
        return NULL;
    }
}

int ib_audit_windows_hash(void *context, uint16_t algorithm,
                          const uint8_t *data, size_t data_size,
                          uint8_t *digest, size_t digest_capacity,
                          size_t *digest_size)
{
    BCRYPT_ALG_HANDLE provider = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    LPCWSTR name = audit_bcrypt_algorithm(algorithm);
    uint16_t expected = audit_pcr_digest_size(algorithm);
    NTSTATUS status;

    (void)context;
    if (name == NULL || data == NULL || digest == NULL ||
        digest_size == NULL || digest_capacity < expected ||
        data_size > ULONG_MAX) {
        return 1;
    }
    *digest_size = 0u;
    status = BCryptOpenAlgorithmProvider(&provider, name, NULL, 0u);
    if (status < 0) {
        return 1;
    }
    status = BCryptCreateHash(provider, &hash, NULL, 0u, NULL, 0u, 0u);
    if (status >= 0) {
        status = BCryptHashData(hash, (PUCHAR)data, (ULONG)data_size, 0u);
    }
    if (status >= 0) {
        status = BCryptFinishHash(hash, digest, expected, 0u);
    }
    if (hash != NULL) {
        (void)BCryptDestroyHash(hash);
    }
    (void)BCryptCloseAlgorithmProvider(provider, 0u);
    if (status < 0) {
        return 1;
    }
    *digest_size = expected;
    return 0;
}

int ib_audit_windows_tpm_open(ib_audit_windows_tpm_t *tpm,
                              uint16_t algorithms[AUDIT_PCR_MAX_BANKS],
                              uint32_t *algorithm_count)
{
    tcgdump_pcr_banks_t banks;
    uint32_t tpm_rc = 0u;
    uint32_t index;
    TBS_RESULT result;

    if (tpm == NULL || algorithms == NULL || algorithm_count == NULL) {
        return 0;
    }
    tpm->context = NULL;
    *algorithm_count = 0u;
    result = tcgdump_tbs_open(&tpm->context);
    if (result != TBS_SUCCESS) {
        return 0;
    }
    (void)memset(&banks, 0, sizeof(banks));
    result = tcgdump_tpm2_get_pcr_banks(tpm->context, &banks, &tpm_rc);
    if (result != TBS_SUCCESS || tpm_rc != TPM_RC_SUCCESS) {
        ib_audit_windows_tpm_close(tpm);
        return 0;
    }
    for (index = 0u; index < banks.count; index++) {
        const tcgdump_pcr_bank_t *bank = &banks.banks[index];

        if (bank->pcr_select[0] == 0u) {
            continue;
        }
        if (audit_pcr_digest_size(bank->alg) == 0u ||
            bank->digest_size != audit_pcr_digest_size(bank->alg) ||
            *algorithm_count >= AUDIT_PCR_MAX_BANKS) {
            ib_audit_windows_tpm_close(tpm);
            return 0;
        }
        algorithms[*algorithm_count] = bank->alg;
        (*algorithm_count)++;
    }
    if (*algorithm_count == 0u) {
        ib_audit_windows_tpm_close(tpm);
        return 0;
    }
    return 1;
}

void ib_audit_windows_tpm_close(ib_audit_windows_tpm_t *tpm)
{
    if (tpm != NULL && tpm->context != NULL) {
        tcgdump_tbs_close(tpm->context);
        tpm->context = NULL;
    }
}

int ib_audit_windows_tpm_major(uint32_t *major)
{
    TPM_DEVICE_INFO info;
    int present = 0;

    if (major == NULL ||
        tcgdump_tbs_device_info(&present, &info) != TBS_SUCCESS || !present) {
        return 0;
    }
    if (info.tpmVersion == TPM_VERSION_20) {
        *major = 2u;
    } else if (info.tpmVersion == TPM_VERSION_12) {
        *major = 1u;
    } else {
        *major = 0u;
    }
    return *major != 0u;
}

int ib_audit_windows_get_log(ib_audit_windows_tpm_t *tpm,
                             uint8_t **log, size_t *log_size)
{
    if (tpm == NULL || tpm->context == NULL || log == NULL ||
        log_size == NULL) {
        return 0;
    }
    return tcgdump_tbs_get_log(tpm->context, log, log_size) == TBS_SUCCESS &&
           *log != NULL && *log_size != 0u;
}

static int audit_is_esp(const wchar_t *volume_path)
{
    wchar_t device[MAX_PATH];
    size_t length;
    HANDLE volume;
    PARTITION_INFORMATION_EX partition;
    DWORD returned = 0u;
    int match = 0;

    if (volume_path == NULL || wcsncmp(volume_path, L"\\\\?\\Volume{", 11u) != 0 ||
        wcscpy_s(device, MAX_PATH, volume_path) != 0) return 0;
    length = wcslen(device);
    while (length != 0u && device[length - 1u] == L'\\') {
        device[--length] = L'\0';
    }
    volume = CreateFileW(device, 0u, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (volume == INVALID_HANDLE_VALUE) return 0;
    memset(&partition, 0, sizeof(partition));
    if (DeviceIoControl(volume, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0u,
                        &partition, sizeof(partition), &returned, NULL) &&
        partition.PartitionStyle == PARTITION_STYLE_GPT &&
        memcmp(&partition.Gpt.PartitionType, &g_audit_esp_partition_type,
               sizeof(g_audit_esp_partition_type)) == 0) {
        match = 1;
    }
    CloseHandle(volume);
    return match;
}

int ib_audit_windows_find_esp(wchar_t *volume_path, size_t count)
{
    HANDLE search;
    wchar_t candidate[MAX_PATH];

    if (volume_path == NULL || count < MAX_PATH) return 0;
    search = FindFirstVolumeW(candidate, MAX_PATH);
    if (search == INVALID_HANDLE_VALUE) return 0;
    do {
        if (audit_is_esp(candidate)) {
            int copied = wcscpy_s(volume_path, count, candidate) == 0;
            FindVolumeClose(search);
            return copied;
        }
    } while (FindNextVolumeW(search, candidate, MAX_PATH));
    FindVolumeClose(search);
    return 0;
}

int ib_audit_windows_hash_file(const wchar_t *path,
                               uint8_t digest[SHA256_DIGEST_SIZE],
                               void *context)
{
    HANDLE file;
    BCRYPT_ALG_HANDLE provider = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    uint8_t buffer[64u * 1024u];
    DWORD got = 0u;
    BOOL read_ok = TRUE;
    NTSTATUS status;
    int success = 0;

    (void)context;
    if (path == NULL || digest == NULL) return 0;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE |
                       FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    status = BCryptOpenAlgorithmProvider(&provider, BCRYPT_SHA256_ALGORITHM,
                                         NULL, 0u);
    if (status >= 0) status = BCryptCreateHash(provider, &hash, NULL, 0u,
                                               NULL, 0u, 0u);
    while (status >= 0) {
        read_ok = ReadFile(file, buffer, sizeof(buffer), &got, NULL);
        if (!read_ok || got == 0u) break;
        status = BCryptHashData(hash, buffer, got, 0u);
    }
    if (status >= 0 && read_ok && got == 0u) {
        status = BCryptFinishHash(hash, digest, SHA256_DIGEST_SIZE, 0u);
        success = status >= 0;
    }
    if (hash != NULL) (void)BCryptDestroyHash(hash);
    if (provider != NULL) (void)BCryptCloseAlgorithmProvider(provider, 0u);
    SecureZeroMemory(buffer, sizeof(buffer));
    CloseHandle(file);
    return success;
}

static int audit_wide_contains(const wchar_t *text, const wchar_t *needle)
{
    size_t needle_length;

    if (text == NULL || needle == NULL) return 0;
    needle_length = wcslen(needle);
    while (*text != L'\0') {
        if (_wcsnicmp(text, needle, needle_length) == 0) return 1;
        ++text;
    }
    return 0;
}

int ib_audit_windows_verify_microsoft(const wchar_t *path, void *context)
{
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_FILE_INFO file;
    WINTRUST_DATA trust;
    CRYPT_PROVIDER_DATA *provider_data;
    CRYPT_PROVIDER_SGNR *signer;
    wchar_t subject[512];
    LONG status;
    int microsoft = 0;

    (void)context;
    if (path == NULL) return 0;
    memset(&file, 0, sizeof(file));
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = path;
    memset(&trust, 0, sizeof(trust));
    trust.cbStruct = sizeof(trust);
    trust.dwUIChoice = WTD_UI_NONE;
    trust.fdwRevocationChecks = WTD_REVOKE_NONE;
    trust.dwUnionChoice = WTD_CHOICE_FILE;
    trust.pFile = &file;
    trust.dwStateAction = WTD_STATEACTION_VERIFY;
    trust.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    status = WinVerifyTrust(INVALID_HANDLE_VALUE, &action, &trust);
    if (status == ERROR_SUCCESS) {
        provider_data = WTHelperProvDataFromStateData(trust.hWVTStateData);
        signer = provider_data == NULL ? NULL :
                 WTHelperGetProvSignerFromChain(provider_data, 0u, FALSE, 0u);
        if (signer != NULL && signer->csCertChain != 0u &&
            signer->pasCertChain[0].pCert != NULL &&
            CertGetNameStringW(signer->pasCertChain[0].pCert,
                               CERT_NAME_SIMPLE_DISPLAY_TYPE, 0u, NULL,
                               subject, (DWORD)(sizeof(subject) /
                                                sizeof(subject[0]))) > 1u &&
            audit_wide_contains(subject, L"Microsoft")) {
            microsoft = 1;
        }
    }
    trust.dwStateAction = WTD_STATEACTION_CLOSE;
    (void)WinVerifyTrust(INVALID_HANDLE_VALUE, &action, &trust);
    return microsoft;
}

int ib_audit_windows_path_exists(const wchar_t *path, void *context)
{
    DWORD attributes;

    (void)context;
    if (path == NULL) return -1;
    SetLastError(ERROR_SUCCESS);
    attributes = GetFileAttributesW(path);
    if (attributes != INVALID_FILE_ATTRIBUTES) return 1;
    return GetLastError() == ERROR_FILE_NOT_FOUND ||
           GetLastError() == ERROR_PATH_NOT_FOUND ? 0 : -1;
}

static HANDLE firmware_get_current_process(void *context)
{
    (void)context;
    return GetCurrentProcess();
}

static BOOL firmware_open_process_token(void *context, HANDLE process,
                                        DWORD access, PHANDLE token)
{
    (void)context;
    return OpenProcessToken(process, access, token);
}

static BOOL firmware_lookup_privilege_value(void *context,
                                           LPCWSTR system_name, LPCWSTR name,
                                           PLUID luid)
{
    (void)context;
    return LookupPrivilegeValueW(system_name, name, luid);
}

static BOOL firmware_adjust_token_privileges(
    void *context, HANDLE token, BOOL disable_all,
    PTOKEN_PRIVILEGES new_state, DWORD buffer_length,
    PTOKEN_PRIVILEGES previous_state, PDWORD return_length)
{
    (void)context;
    return AdjustTokenPrivileges(token, disable_all, new_state, buffer_length,
                                 previous_state, return_length);
}

static BOOL firmware_close_handle(void *context, HANDLE handle)
{
    (void)context;
    return CloseHandle(handle);
}

static DWORD firmware_get_last_error(void *context)
{
    (void)context;
    return GetLastError();
}

static void firmware_set_last_error(void *context, DWORD error)
{
    (void)context;
    SetLastError(error);
}

static const ib_audit_firmware_privilege_api_t g_firmware_privilege_api = {
    NULL,
    firmware_get_current_process,
    firmware_open_process_token,
    firmware_lookup_privilege_value,
    firmware_adjust_token_privileges,
    firmware_close_handle,
    firmware_get_last_error,
    firmware_set_last_error
};

static int firmware_privilege_api_valid(
    const ib_audit_firmware_privilege_api_t *api)
{
    return api != NULL && api->get_current_process != NULL &&
           api->open_process_token != NULL &&
           api->lookup_privilege_value != NULL &&
           api->adjust_token_privileges != NULL &&
           api->close_handle != NULL && api->get_last_error != NULL &&
           api->set_last_error != NULL;
}

static DWORD firmware_restore_previous(
    const ib_audit_firmware_privilege_api_t *api, HANDLE token,
    PTOKEN_PRIVILEGES previous)
{
    api->set_last_error(api->context, ERROR_SUCCESS);
    if (!api->adjust_token_privileges(api->context, token, FALSE, previous,
                                      0u, NULL, NULL)) {
        return api->get_last_error(api->context);
    }
    return api->get_last_error(api->context);
}

int ib_audit_windows_firmware_privilege_begin_with_api(
    ib_audit_firmware_privilege_t *privilege,
    const ib_audit_firmware_privilege_api_t *api, DWORD *error_code)
{
    TOKEN_PRIVILEGES requested;
    LUID luid;
    DWORD returned = 0u;
    DWORD error;
    HANDLE process;

    if (error_code != NULL) *error_code = ERROR_SUCCESS;
    if (privilege == NULL || !firmware_privilege_api_valid(api)) {
        if (error_code != NULL) *error_code = ERROR_INVALID_PARAMETER;
        return 0;
    }
    memset(privilege, 0, sizeof(*privilege));
    privilege->api = api;
    process = api->get_current_process(api->context);
    if (!api->open_process_token(api->context, process,
                                 TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                                 &privilege->token)) {
        if (error_code != NULL) *error_code = api->get_last_error(api->context);
        privilege->token = NULL;
        privilege->api = NULL;
        return 0;
    }
    if (!api->lookup_privilege_value(api->context, NULL,
                                    L"SeSystemEnvironmentPrivilege", &luid)) {
        error = api->get_last_error(api->context);
        (void)api->close_handle(api->context, privilege->token);
        privilege->token = NULL;
        privilege->api = NULL;
        if (error_code != NULL) *error_code = error;
        return 0;
    }
    memset(&requested, 0, sizeof(requested));
    requested.PrivilegeCount = 1u;
    requested.Privileges[0].Luid = luid;
    requested.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    api->set_last_error(api->context, ERROR_SUCCESS);
    if (!api->adjust_token_privileges(
            api->context, privilege->token, FALSE, &requested,
            sizeof(privilege->previous), &privilege->previous, &returned)) {
        error = api->get_last_error(api->context);
        (void)api->close_handle(api->context, privilege->token);
        privilege->token = NULL;
        privilege->api = NULL;
        if (error_code != NULL) *error_code = error;
        return 0;
    }
    error = api->get_last_error(api->context);
    if (privilege->previous.PrivilegeCount > 1u) {
        (void)api->close_handle(api->context, privilege->token);
        privilege->token = NULL;
        privilege->api = NULL;
        if (error_code != NULL) *error_code = ERROR_INVALID_DATA;
        return 0;
    }
    if (error == ERROR_NOT_ALL_ASSIGNED) {
        if (privilege->previous.PrivilegeCount != 0u) {
            DWORD restore_error = firmware_restore_previous(
                api, privilege->token, &privilege->previous);
            if (restore_error != ERROR_SUCCESS) error = restore_error;
        }
        (void)api->close_handle(api->context, privilege->token);
        privilege->token = NULL;
        privilege->api = NULL;
        if (error_code != NULL) *error_code = error;
        return 0;
    }
    if (privilege->previous.PrivilegeCount != 0u &&
        returned != (DWORD)sizeof(privilege->previous)) {
        DWORD restore_error = firmware_restore_previous(
            api, privilege->token, &privilege->previous);
        (void)api->close_handle(api->context, privilege->token);
        privilege->token = NULL;
        privilege->api = NULL;
        if (error_code != NULL) {
            *error_code = restore_error != ERROR_SUCCESS ? restore_error :
                          ERROR_INSUFFICIENT_BUFFER;
        }
        return 0;
    }
    privilege->previous_size = privilege->previous.PrivilegeCount != 0u
        ? (DWORD)sizeof(privilege->previous) : 0u;
    privilege->restore_required = privilege->previous.PrivilegeCount != 0u;
    privilege->active = 1;
    return 1;
}

int ib_audit_windows_firmware_privilege_begin(
    ib_audit_firmware_privilege_t *privilege, DWORD *error_code)
{
    return ib_audit_windows_firmware_privilege_begin_with_api(
        privilege, &g_firmware_privilege_api, error_code);
}

int ib_audit_windows_firmware_privilege_end(
    ib_audit_firmware_privilege_t *privilege, DWORD *error_code)
{
    DWORD error = ERROR_SUCCESS;
    int success = 1;
    const ib_audit_firmware_privilege_api_t *api;

    if (error_code != NULL) *error_code = ERROR_SUCCESS;
    if (privilege == NULL || !privilege->active ||
        privilege->token == NULL || !firmware_privilege_api_valid(privilege->api) ||
        (privilege->restore_required && privilege->previous_size == 0u) ||
        privilege->previous_size > sizeof(privilege->previous)) {
        if (error_code != NULL) *error_code = ERROR_INVALID_PARAMETER;
        return 0;
    }
    api = privilege->api;
    if (privilege->restore_required) {
        error = firmware_restore_previous(api, privilege->token,
                                           &privilege->previous);
        if (error != ERROR_SUCCESS) {
            success = 0;
        }
    }
    privilege->active = 0;
    if (!api->close_handle(api->context, privilege->token) && success) {
        error = api->get_last_error(api->context);
        success = 0;
    }
    privilege->token = NULL;
    privilege->previous_size = 0u;
    privilege->api = NULL;
    if (!success && error_code != NULL) *error_code = error;
    return success;
}

int ib_audit_windows_read_bank(void *context, uint16_t algorithm,
                               uint8_t *pcrs, size_t pcrs_capacity,
                               size_t digest_size)
{
    ib_audit_windows_tpm_t *tpm = (ib_audit_windows_tpm_t *)context;
    uint8_t values[TCGDUMP_MAX_PCRS][64];
    uint8_t returned_size = 0u;
    uint32_t tpm_rc = 0u;
    uint32_t index;
    TBS_RESULT result;

    if (tpm == NULL || tpm->context == NULL || pcrs == NULL ||
        digest_size != audit_pcr_digest_size(algorithm) ||
        pcrs_capacity != AUDIT_PCR_COUNT * digest_size) {
        return 1;
    }
    (void)memset(values, 0, sizeof(values));
    result = tcgdump_tpm2_pcr_read(tpm->context, algorithm, values,
                                   &returned_size, &tpm_rc);
    if (result != TBS_SUCCESS || tpm_rc != TPM_RC_SUCCESS ||
        returned_size != digest_size) {
        return 1;
    }
    for (index = 0u; index < AUDIT_PCR_COUNT; index++) {
        (void)memcpy(pcrs + (size_t)index * digest_size, values[index],
                     digest_size);
    }
    return 0;
}

static int audit_guid_text(const ib_audit_guid_t *guid,
                           wchar_t text[40])
{
    int length;

    if (guid == NULL || text == NULL) {
        return 0;
    }
    length = swprintf_s(
        text, 40u,
        L"{%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX}",
        (unsigned long)guid->data1, guid->data2, guid->data3,
        guid->data4[0], guid->data4[1], guid->data4[2], guid->data4[3],
        guid->data4[4], guid->data4[5], guid->data4[6], guid->data4[7]);
    return length == 38;
}

int ib_audit_windows_read_variable(const wchar_t *name,
                                   const ib_audit_guid_t *guid,
                                   uint8_t *data, size_t capacity,
                                   size_t *data_size, uint32_t *attributes,
                                   void *context)
{
    wchar_t guid_text[40];
    DWORD got;
    DWORD error;
    DWORD firmware_attributes = 0u;

    (void)context;
    if (name == NULL || guid == NULL || data_size == NULL ||
        attributes == NULL || !audit_guid_text(guid, guid_text)) {
        return IB_AUDIT_READ_ERROR;
    }
    *data_size = 0u;
    *attributes = 0u;

    if (data == NULL && capacity == 0u) {
        size_t probe_capacity = 256u;

        while (probe_capacity <= IB_AUDIT_VARIABLE_PROBE_MAX) {
            uint8_t *probe = (uint8_t *)malloc(probe_capacity);

            if (probe == NULL) {
                return IB_AUDIT_READ_ERROR;
            }
            SetLastError(ERROR_SUCCESS);
            firmware_attributes = 0u;
            got = GetFirmwareEnvironmentVariableExW(
                name, guid_text, probe, (DWORD)probe_capacity,
                &firmware_attributes);
            error = GetLastError();
            free(probe);
            if (got != 0u || error == ERROR_SUCCESS) {
                *data_size = (size_t)got;
                *attributes = (uint32_t)firmware_attributes;
                return got == 0u ? IB_AUDIT_READ_PRESENT :
                                   IB_AUDIT_READ_BUFFER_TOO_SMALL;
            }
            if (error == ERROR_ENVVAR_NOT_FOUND) {
                return IB_AUDIT_READ_ABSENT;
            }
            if (error != ERROR_INSUFFICIENT_BUFFER) {
                return IB_AUDIT_READ_ERROR;
            }
            probe_capacity *= 2u;
        }
        return IB_AUDIT_READ_ERROR;
    }

    if (data == NULL || capacity == 0u || capacity > MAXDWORD) {
        return IB_AUDIT_READ_ERROR;
    }
    SetLastError(ERROR_SUCCESS);
    firmware_attributes = 0u;
    got = GetFirmwareEnvironmentVariableExW(
        name, guid_text, data, (DWORD)capacity, &firmware_attributes);
    error = GetLastError();
    if (got != 0u || error == ERROR_SUCCESS) {
        *data_size = (size_t)got;
        *attributes = (uint32_t)firmware_attributes;
        return IB_AUDIT_READ_PRESENT;
    }
    if (error == ERROR_ENVVAR_NOT_FOUND) {
        return IB_AUDIT_READ_ABSENT;
    }
    if (error == ERROR_INSUFFICIENT_BUFFER) {
        *data_size = capacity;
        return IB_AUDIT_READ_BUFFER_TOO_SMALL;
    }
    return IB_AUDIT_READ_ERROR;
}
