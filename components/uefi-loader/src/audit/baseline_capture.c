/* baseline_capture.c - Read-only Windows state capture for detector 6. */
#include "baseline_capture.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audit_io.h"
#include "pcr.h"
#include "platform_win.h"
#include "vars.h"

static void set_error(wchar_t *error, size_t count, const wchar_t *format, ...)
{
    va_list arguments;
    if (error == NULL || count == 0u) return;
    va_start(arguments, format);
    _vsnwprintf_s(error, count, _TRUNCATE, format, arguments);
    va_end(arguments);
}

static int append_path(wchar_t *output, size_t count, const wchar_t *root,
                       const wchar_t *relative)
{
    size_t length;
    if (wcscpy_s(output, count, root) != 0) return 0;
    length = wcslen(output);
    if (length == 0u) return 0;
    if (output[length - 1u] != L'\\' && wcscat_s(output, count, L"\\") != 0) {
        return 0;
    }
    return wcscat_s(output, count, relative) == 0;
}

static int mok_present(const ib_audit_baseline_t *state,
                       const uint8_t digest[SHA256_DIGEST_SIZE])
{
    uint32_t index;
    for (index = 0u; index < state->mok_count; ++index) {
        if (memcmp(state->mok_sha256[index], digest,
                   SHA256_DIGEST_SIZE) == 0) return 1;
    }
    return 0;
}

static int capture_moks(ib_audit_baseline_t *state)
{
    ib_audit_variable_ops_t ops;
    ib_audit_report_t report;
    uint32_t variable;

    ops.read_variable = ib_audit_windows_read_variable;
    ops.context = NULL;
    if (ib_audit_variables(&ops, NULL, 0u, &report) != IB_AUDIT_OK) return 0;
    for (variable = IB_AUDIT_VARIABLE_MOK_LIST;
         variable <= IB_AUDIT_VARIABLE_MOK_LIST_RT; ++variable) {
        const ib_audit_variable_observation_t *observation =
            &report.variables[variable];
        uint32_t fingerprint;
        if (observation->state != IB_AUDIT_VARIABLE_PRESENT &&
            observation->state != IB_AUDIT_VARIABLE_ABSENT) return 0;
        for (fingerprint = 0u; fingerprint < observation->fingerprint_count;
             ++fingerprint) {
            const uint8_t *digest = observation->fingerprints[fingerprint];
            if (!mok_present(state, digest)) {
                if (state->mok_count >= IB_AUDIT_BASELINE_MAX_MOK) return 0;
                memcpy(state->mok_sha256[state->mok_count++], digest,
                       SHA256_DIGEST_SIZE);
            }
        }
    }
    return 1;
}

static int read_boot_variable(const wchar_t *name, uint8_t **bytes,
                              size_t *size)
{
    uint32_t attributes = 0u;
    size_t required = 0u;
    int status;

    *bytes = NULL; *size = 0u;
    status = ib_audit_windows_read_variable(name, &IB_AUDIT_GLOBAL_GUID,
                                             NULL, 0u, &required, &attributes,
                                             NULL);
    if (status == IB_AUDIT_READ_ABSENT) return 0;
    if (status == IB_AUDIT_READ_PRESENT && required == 0u) return 1;
    if (status != IB_AUDIT_READ_BUFFER_TOO_SMALL || required == 0u ||
        required > IB_AUDIT_BASELINE_MAX_BOOT_ORDER * 2u) return -1;
    *bytes = (uint8_t *)malloc(required);
    if (*bytes == NULL) return -1;
    *size = required;
    status = ib_audit_windows_read_variable(name, &IB_AUDIT_GLOBAL_GUID,
                                             *bytes, required, size,
                                             &attributes, NULL);
    if (status != IB_AUDIT_READ_PRESENT || *size != required) {
        free(*bytes); *bytes = NULL; *size = 0u; return -1;
    }
    return 1;
}

static int capture_boot_selection(ib_audit_baseline_t *state)
{
    uint8_t *bytes = NULL;
    size_t size = 0u;
    size_t index;
    int status = read_boot_variable(L"BootOrder", &bytes, &size);

    if (status < 0 || (size & 1u) != 0u) { free(bytes); return 0; }
    if (status > 0) {
        state->boot_order_count = (uint32_t)(size / 2u);
        for (index = 0u; index < state->boot_order_count; ++index) {
            state->boot_order[index] =
                (uint16_t)bytes[index * 2u] |
                (uint16_t)((uint16_t)bytes[index * 2u + 1u] << 8);
        }
    }
    free(bytes); bytes = NULL; size = 0u;
    status = read_boot_variable(L"BootNext", &bytes, &size);
    if (status < 0 || (status > 0 && size != 2u)) { free(bytes); return 0; }
    if (status > 0) {
        state->boot_next_present = 1u;
        state->boot_next = (uint16_t)bytes[0] |
                           (uint16_t)((uint16_t)bytes[1] << 8);
    }
    free(bytes);
    return 1;
}

int ib_audit_windows_capture_baseline(const char *log_path,
                                      const wchar_t *esp_root,
                                      ib_audit_baseline_t *state,
                                      wchar_t *error, size_t error_count)
{
    uint8_t *log = NULL;
    size_t log_size = 0u;
    ib_audit_windows_tpm_t tpm;
    uint16_t algorithms[AUDIT_PCR_MAX_BANKS];
    uint32_t algorithm_count = 0u;
    uint32_t index;
    wchar_t discovered[MAX_PATH];
    wchar_t bootmgfw[1024];

    if (log_path == NULL || state == NULL) {
        set_error(error, error_count, L"baseline request is incomplete");
        return 0;
    }
    memset(state, 0, sizeof(*state));
    if (!ib_audit_read_file(log_path, &log, &log_size) ||
        sha256_hash(log, log_size, state->log_sha256) != SHA256_OK) {
        set_error(error, error_count, L"reference log cannot be read and hashed");
        free(log); return 0;
    }
    free(log);
    if (!ib_audit_windows_tpm_open(&tpm, algorithms, &algorithm_count)) {
        set_error(error, error_count, L"TPM bank discovery failed");
        return 0;
    }
    state->bank_count = algorithm_count;
    for (index = 0u; index < algorithm_count; ++index) {
        ib_audit_baseline_bank_t *bank = &state->banks[index];
        size_t digest_size = audit_pcr_digest_size(algorithms[index]);
        uint8_t packed[IB_AUDIT_BASELINE_PCR_COUNT *
                       IB_AUDIT_BASELINE_MAX_DIGEST];
        uint32_t pcr;
        bank->algorithm = algorithms[index];
        bank->digest_size = (uint16_t)digest_size;
        if (ib_audit_windows_read_bank(&tpm, algorithms[index],
                                       packed,
                                       IB_AUDIT_BASELINE_PCR_COUNT * digest_size,
                                       digest_size) != 0) {
            ib_audit_windows_tpm_close(&tpm);
            set_error(error, error_count, L"TPM PCR read failed");
            return 0;
        }
        for (pcr = 0u; pcr < IB_AUDIT_BASELINE_PCR_COUNT; ++pcr) {
            memcpy(bank->pcrs[pcr], packed + (size_t)pcr * digest_size,
                   digest_size);
        }
        SecureZeroMemory(packed, sizeof(packed));
    }
    ib_audit_windows_tpm_close(&tpm);
    if (!capture_moks(state) || !capture_boot_selection(state)) {
        set_error(error, error_count, L"firmware variables cannot be captured safely");
        return 0;
    }
    if (esp_root == NULL) {
        if (!ib_audit_windows_find_esp(discovered, MAX_PATH)) {
            set_error(error, error_count, L"GPT ESP discovery failed");
            return 0;
        }
        esp_root = discovered;
    }
    if (!append_path(bootmgfw, sizeof(bootmgfw) / sizeof(bootmgfw[0]),
                     esp_root, L"EFI\\Microsoft\\Boot\\bootmgfw.efi") ||
        !ib_audit_windows_hash_file(bootmgfw, state->bootmgfw_sha256, NULL)) {
        set_error(error, error_count, L"bootmgfw.efi cannot be read and hashed");
        return 0;
    }
    return 1;
}
