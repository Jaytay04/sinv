/* bootmgr.c - Portable policy and EFI-load-option parsing for detector 4. */
#include "bootmgr.h"

#include <stdlib.h>
#include <string.h>

#define IB_BOOTMGR_MAX_VARIABLE_BYTES (1024u * 1024u)

typedef struct {
    uint8_t *bytes;
    size_t size;
} bootmgr_buffer_t;

static int append_path(wchar_t *output, size_t count, const wchar_t *root,
                       const wchar_t *relative)
{
    size_t length;

    if (output == NULL || root == NULL || relative == NULL ||
        wcscpy_s(output, count, root) != 0) {
        return 0;
    }
    length = wcslen(output);
    if (length == 0u) return 0;
    if (output[length - 1u] != L'\\' && wcscat_s(output, count, L"\\") != 0) {
        return 0;
    }
    return wcscat_s(output, count, relative) == 0;
}

static int digest_equal(const uint8_t *left, const uint8_t *right)
{
    size_t index;
    uint8_t difference = 0u;

    for (index = 0u; index < SHA256_DIGEST_SIZE; ++index) {
        difference = (uint8_t)(difference | (left[index] ^ right[index]));
    }
    return difference == 0u;
}

static int read_variable(const ib_audit_variable_ops_t *ops,
                         const wchar_t *name, bootmgr_buffer_t *buffer)
{
    size_t required = 0u;
    uint32_t attributes = 0u;
    int status;

    buffer->bytes = NULL;
    buffer->size = 0u;
    status = ops->read_variable(name, &IB_AUDIT_GLOBAL_GUID, NULL, 0u,
                                &required, &attributes, ops->context);
    if (status == IB_AUDIT_READ_ABSENT) return 0;
    if (status == IB_AUDIT_READ_PRESENT && required == 0u) return 1;
    if (status != IB_AUDIT_READ_BUFFER_TOO_SMALL || required == 0u ||
        required > IB_BOOTMGR_MAX_VARIABLE_BYTES) return -1;
    buffer->bytes = (uint8_t *)malloc(required);
    if (buffer->bytes == NULL) return -2;
    buffer->size = required;
    status = ops->read_variable(name, &IB_AUDIT_GLOBAL_GUID, buffer->bytes,
                                required, &buffer->size, &attributes,
                                ops->context);
    if (status != IB_AUDIT_READ_PRESENT || buffer->size != required) {
        free(buffer->bytes);
        buffer->bytes = NULL;
        buffer->size = 0u;
        return -1;
    }
    return 1;
}

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint8_t ascii_lower(uint8_t value)
{
    return value >= (uint8_t)'A' && value <= (uint8_t)'Z'
        ? (uint8_t)(value + ((uint8_t)'a' - (uint8_t)'A')) : value;
}

static int contains_utf16_ascii(const uint8_t *bytes, size_t size,
                                const char *pattern)
{
    size_t pattern_size = strlen(pattern);
    size_t offset;

    if (pattern_size == 0u || size < pattern_size * 2u) return 0;
    for (offset = 0u; offset + pattern_size * 2u <= size; ++offset) {
        size_t index;
        for (index = 0u; index < pattern_size; ++index) {
            uint8_t low = bytes[offset + index * 2u];
            uint8_t high = bytes[offset + index * 2u + 1u];
            if (high != 0u || ascii_lower(low) !=
                              ascii_lower((uint8_t)pattern[index])) break;
        }
        if (index == pattern_size) return 1;
    }
    return 0;
}

static int boot_entry_is_staged(const uint8_t *bytes, size_t size)
{
    /* EFI_LOAD_OPTION begins with Attributes and FilePathListLength. A valid
     * option must also contain a NUL-terminated UTF-16 description. */
    size_t position;

    if (bytes == NULL || size < 8u) return -1;
    for (position = 6u; position + 1u < size; position += 2u) {
        if (bytes[position] == 0u && bytes[position + 1u] == 0u) break;
    }
    if (position + 1u >= size) return -1;
    return contains_utf16_ascii(bytes, size, "\\efi\\insecure-boot\\") ||
           contains_utf16_ascii(bytes, size, "Insecure Boot Toolkit");
}

static void add_staged(ib_audit_bootmgr_report_t *report, uint16_t number)
{
    uint32_t index;

    for (index = 0u; index < report->staged_entry_count; ++index) {
        if (report->staged_entries[index] == number) return;
    }
    if (report->staged_entry_count < IB_AUDIT_BOOTMGR_MAX_STAGED_ENTRIES) {
        report->staged_entries[report->staged_entry_count++] = number;
    }
}

static int inspect_entry(const ib_audit_variable_ops_t *ops, uint16_t number,
                         ib_audit_bootmgr_report_t *report)
{
    wchar_t name[9];
    bootmgr_buffer_t buffer;
    int status;
    int staged;

    if (swprintf_s(name, 9u, L"Boot%04X", (unsigned int)number) != 8) return -1;
    status = read_variable(ops, name, &buffer);
    if (status <= 0) return status == -2 ? -2 : -1;
    staged = boot_entry_is_staged(buffer.bytes, buffer.size);
    free(buffer.bytes);
    if (staged < 0) return -1;
    if (staged != 0) add_staged(report, number);
    return staged;
}

static int inspect_boot_variables(const ib_audit_variable_ops_t *ops,
                                  ib_audit_bootmgr_report_t *report)
{
    bootmgr_buffer_t next;
    bootmgr_buffer_t order;
    size_t index;
    int status;

    status = read_variable(ops, L"BootNext", &next);
    if (status == -2) return -2;
    if (status < 0) report->findings |= IB_AUDIT_BOOTMGR_READ_FAILURE;
    if (status > 0) {
        if (next.size != 2u) {
            report->findings |= IB_AUDIT_BOOTMGR_BOOT_VARIABLE_MALFORMED;
        } else {
            int staged = inspect_entry(ops, read_u16(next.bytes), report);
            if (staged == -2) { free(next.bytes); return -2; }
            if (staged < 0) report->findings |= IB_AUDIT_BOOTMGR_READ_FAILURE;
            if (staged > 0) report->findings |= IB_AUDIT_BOOTMGR_STAGED_BOOTNEXT;
        }
        free(next.bytes);
    }

    status = read_variable(ops, L"BootOrder", &order);
    if (status == -2) return -2;
    if (status < 0) report->findings |= IB_AUDIT_BOOTMGR_READ_FAILURE;
    if (status > 0) {
        if ((order.size & 1u) != 0u) {
            report->findings |= IB_AUDIT_BOOTMGR_BOOT_VARIABLE_MALFORMED;
        } else {
            for (index = 0u; index < order.size; index += 2u) {
                int staged = inspect_entry(ops, read_u16(order.bytes + index),
                                           report);
                if (staged == -2) { free(order.bytes); return -2; }
                if (staged < 0) report->findings |= IB_AUDIT_BOOTMGR_READ_FAILURE;
                if (staged > 0) report->findings |= IB_AUDIT_BOOTMGR_STAGED_BOOTORDER;
            }
        }
        free(order.bytes);
    }
    return 0;
}

ib_audit_bootmgr_status_t ib_audit_bootmgr_integrity(
    const ib_audit_bootmgr_request_t *request,
    ib_audit_bootmgr_report_t *report)
{
    static const wchar_t *const residue[] = {
        L"EFI\\Microsoft\\Boot\\bootmgfw.efi.bak",
        L"EFI\\Microsoft\\Boot\\bootmgfw.efi.ibnew",
        L"EFI\\Microsoft\\Boot\\bootmgfw.efi.ibold",
        L"EFI\\Microsoft\\Boot\\bootmgfw.sha256"
    };
    wchar_t path[1024];
    size_t index;
    int primary_path_valid;

    if (request == NULL || report == NULL || request->esp_root == NULL ||
        request->expected_bootmgfw_sha256 == NULL || request->ops == NULL ||
        request->ops->hash_file == NULL ||
        request->ops->verify_microsoft_authenticode == NULL ||
        request->ops->path_exists == NULL || request->ops->variables == NULL ||
        request->ops->variables->read_variable == NULL) {
        return IB_AUDIT_BOOTMGR_INVALID_ARGUMENT;
    }
    memset(report, 0, sizeof(*report));
    primary_path_valid = append_path(
        path, 1024u, request->esp_root,
        L"EFI\\Microsoft\\Boot\\bootmgfw.efi");
    if (!primary_path_valid ||
        request->ops->hash_file(path, report->bootmgfw_sha256,
                                request->ops->context) <= 0) {
        report->findings |= IB_AUDIT_BOOTMGR_READ_FAILURE;
    } else if (!digest_equal(report->bootmgfw_sha256,
                             request->expected_bootmgfw_sha256)) {
        report->findings |= IB_AUDIT_BOOTMGR_HASH_MISMATCH;
    }
    if (!primary_path_valid ||
        request->ops->verify_microsoft_authenticode(
            path, request->ops->context) <= 0) {
        report->findings |= IB_AUDIT_BOOTMGR_AUTHENTICODE_INVALID;
    }
    for (index = 0u; index < sizeof(residue) / sizeof(residue[0]); ++index) {
        int exists;
        if (!append_path(path, 1024u, request->esp_root, residue[index])) {
            report->findings |= IB_AUDIT_BOOTMGR_READ_FAILURE;
            continue;
        }
        exists = request->ops->path_exists(path, request->ops->context);
        if (exists < 0) {
            report->findings |= IB_AUDIT_BOOTMGR_READ_FAILURE;
        } else if (exists > 0) {
            report->residue_mask |= (uint32_t)(1u << index);
        }
    }
    if ((report->residue_mask & IB_AUDIT_BOOTMGR_RESIDUE_BAK) != 0u) {
        report->findings |= IB_AUDIT_BOOTMGR_BACKUP_PRESENT;
    }
    if ((report->residue_mask & ~IB_AUDIT_BOOTMGR_RESIDUE_BAK) != 0u) {
        report->findings |= IB_AUDIT_BOOTMGR_TRANSACTION_RESIDUE;
    }
    if (inspect_boot_variables(request->ops->variables, report) == -2) {
        return IB_AUDIT_BOOTMGR_MEMORY_ERROR;
    }
    return IB_AUDIT_BOOTMGR_OK;
}
