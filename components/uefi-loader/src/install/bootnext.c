#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "bootnext.h"

static const wchar_t g_global_guid[] =
    L"{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}";
static const wchar_t g_legacy_description[] = L"Insecure Boot Toolkit";
static const wchar_t g_legacy_loader_path[] = L"\\EFI\\insecure-boot\\shimx64.efi";
static const wchar_t g_inventory_description[] = L"Hypersinv EFI Inventory";
static const wchar_t g_inventory_loader_path[] = L"\\EFI\\HypersinvInventory\\loader.efi";
static const wchar_t g_shim_inventory_description[] = L"Hypersinv Shim Inventory";
static const wchar_t g_shim_inventory_loader_path[] =
    L"\\EFI\\HypersinvShimInventory\\shimx64.efi";

static void set_error(wchar_t *error, size_t count, const wchar_t *format, ...)
{
    va_list args;
    if (error == NULL || count == 0u) return;
    va_start(args, format);
    _vsnwprintf_s(error, count, _TRUNCATE, format, args);
    va_end(args);
}

static void put_u16(unsigned char *p, unsigned short value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
}

static void put_u32(unsigned char *p, unsigned long value)
{
    size_t i;
    for (i = 0; i < 4u; ++i) p[i] = (unsigned char)(value >> (i * 8u));
}

static void put_u64(unsigned char *p, uint64_t value)
{
    size_t i;
    for (i = 0; i < 8u; ++i) p[i] = (unsigned char)(value >> (i * 8u));
}

static int profile_strings(ib_bootnext_profile_t profile,
                           const wchar_t **description, const wchar_t **loader_path)
{
    if (description == NULL || loader_path == NULL) return 0;
    if (profile == IB_BOOTNEXT_PROFILE_LEGACY) {
        *description = g_legacy_description;
        *loader_path = g_legacy_loader_path;
        return 1;
    }
    if (profile == IB_BOOTNEXT_PROFILE_INVENTORY) {
        *description = g_inventory_description;
        *loader_path = g_inventory_loader_path;
        return 1;
    }
    if (profile == IB_BOOTNEXT_PROFILE_SHIM_INVENTORY) {
        *description = g_shim_inventory_description;
        *loader_path = g_shim_inventory_loader_path;
        return 1;
    }
    return 0;
}

int ib_bootnext_serialize_profile(const ib_bootnext_partition_t *partition,
                                  ib_bootnext_profile_t profile,
                                  unsigned char *output, size_t *output_size)
{
    const wchar_t *description;
    const wchar_t *loader_path;
    size_t description_bytes;
    size_t path_bytes;
    size_t file_node_size;
    size_t total;
    size_t offset;
    size_t i;
    if (partition == NULL || output_size == NULL || partition->partition_number == 0u ||
        partition->partition_size == 0u ||
        !profile_strings(profile, &description, &loader_path)) return 0;
    description_bytes = (wcslen(description) + 1u) * sizeof(wchar_t);
    path_bytes = (wcslen(loader_path) + 1u) * sizeof(wchar_t);
    file_node_size = 4u + path_bytes;
    if (file_node_size > 0xffffu) return 0;
    total = 6u + description_bytes + 42u + file_node_size + 4u;
    if (output == NULL || *output_size < total) {
        *output_size = total;
        return output == NULL;
    }
    memset(output, 0, total);
    put_u32(output, 1ul);
    put_u16(output + 4u, (unsigned short)(42u + file_node_size + 4u));
    offset = 6u;
    for (i = 0; i < description_bytes / 2u; ++i) {
        put_u16(output + offset + i * 2u, (unsigned short)description[i]);
    }
    offset += description_bytes;
    output[offset] = 0x04u; output[offset + 1u] = 0x01u;
    put_u16(output + offset + 2u, 42u);
    put_u32(output + offset + 4u, partition->partition_number);
    put_u64(output + offset + 8u, partition->partition_start);
    put_u64(output + offset + 16u, partition->partition_size);
    memcpy(output + offset + 24u, partition->partition_guid, 16u);
    output[offset + 40u] = 0x02u;
    output[offset + 41u] = 0x02u;
    offset += 42u;
    output[offset] = 0x04u; output[offset + 1u] = 0x04u;
    put_u16(output + offset + 2u, (unsigned short)file_node_size);
    for (i = 0; i < path_bytes / 2u; ++i) {
        put_u16(output + offset + 4u + i * 2u,
                (unsigned short)loader_path[i]);
    }
    offset += file_node_size;
    output[offset] = 0x7fu; output[offset + 1u] = 0xffu;
    put_u16(output + offset + 2u, 4u);
    *output_size = total;
    return 1;
}

int ib_bootnext_serialize(const ib_bootnext_partition_t *partition,
                          unsigned char *output, size_t *output_size)
{
    return ib_bootnext_serialize_profile(partition, IB_BOOTNEXT_PROFILE_LEGACY,
                                         output, output_size);
}

int ib_bootnext_is_owned_profile(const unsigned char *data, size_t data_size,
                                 const ib_bootnext_partition_t *partition,
                                 ib_bootnext_profile_t profile)
{
    unsigned char expected[512];
    size_t expected_size = sizeof(expected);
    return data != NULL && ib_bootnext_serialize_profile(partition, profile,
                                                          expected, &expected_size) &&
           data_size == expected_size && memcmp(data, expected, data_size) == 0;
}

int ib_bootnext_is_owned(const unsigned char *data, size_t data_size,
                         const ib_bootnext_partition_t *partition)
{
    return ib_bootnext_is_owned_profile(data, data_size, partition,
                                        IB_BOOTNEXT_PROFILE_LEGACY);
}

static int default_read(const wchar_t *name, unsigned char *data,
                        size_t *data_size, unsigned long *attributes, void *context)
{
    DWORD result;
    DWORD capacity;
    (void)context;
    if (name == NULL || data_size == NULL || *data_size > MAXDWORD) return -1;
    capacity = (DWORD)*data_size;
    SetLastError(ERROR_SUCCESS);
    result = GetFirmwareEnvironmentVariableExW(name, g_global_guid, data,
                                                capacity, attributes);
    if (result != 0u) {
        *data_size = result;
        return 1;
    }
    if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
        *data_size = 0u;
        return 0;
    }
    return -1;
}

static int default_write(const wchar_t *name, const unsigned char *data,
                         size_t data_size, unsigned long attributes, void *context)
{
    (void)context;
    if (data_size > MAXDWORD) return 0;
    return SetFirmwareEnvironmentVariableExW(name, g_global_guid, (PVOID)data,
                                              (DWORD)data_size, attributes) != FALSE;
}

void ib_bootnext_default_ops(ib_bootnext_variable_ops_t *ops)
{
    if (ops == NULL) return;
    ops->read_variable = default_read;
    ops->write_variable = default_write;
    ops->context = NULL;
}

int ib_bootnext_enable_privilege(wchar_t *error, size_t error_count)
{
    HANDLE token = NULL;
    TOKEN_PRIVILEGES privileges;
    LUID luid;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                          &token) || !LookupPrivilegeValueW(NULL,
                          L"SeSystemEnvironmentPrivilege", &luid)) {
        if (token != NULL) CloseHandle(token);
        set_error(error, error_count, L"cannot open SeSystemEnvironmentPrivilege");
        return 0;
    }
    ZeroMemory(&privileges, sizeof(privileges));
    privileges.PrivilegeCount = 1u;
    privileges.Privileges[0].Luid = luid;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    SetLastError(ERROR_SUCCESS);
    if (!AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(privileges),
                               NULL, NULL) || GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
        CloseHandle(token);
        set_error(error, error_count, L"SeSystemEnvironmentPrivilege is unavailable");
        return 0;
    }
    CloseHandle(token);
    return 1;
}

static int read_name(const ib_bootnext_variable_ops_t *ops, const wchar_t *name,
                     unsigned char *data, size_t *size, unsigned long *attributes)
{
    unsigned long ignored_attributes = 0;
    if (attributes == NULL) attributes = &ignored_attributes;
    return ops->read_variable(name, data, size, attributes, ops->context);
}

static void boot_name(unsigned short number, wchar_t name[9])
{
    swprintf_s(name, 9u, L"Boot%04X", (unsigned int)number);
}

/*
 * A firmware variable has no compare-and-delete primitive.  Always re-read
 * the value and attributes immediately before deleting it, and verify that
 * the delete took effect.  A mismatch is deliberately treated as unsafe:
 * callers must not turn an uncertain write into deletion of another owner's
 * variable.
 */
static int remove_exact(const ib_bootnext_request_t *request, const wchar_t *name,
                        const unsigned char *expected, size_t expected_size,
                        unsigned long expected_attributes, int *was_present)
{
    unsigned char actual[4096];
    size_t actual_size = sizeof(actual);
    unsigned long actual_attributes = 0;
    int result;
    if (was_present != NULL) *was_present = 0;
    result = read_name(request->ops, name, actual, &actual_size, &actual_attributes);
    if (result == 0) return 1;
    if (result < 0) return 0;
    if (was_present != NULL) *was_present = 1;
    if (actual_size != expected_size || actual_attributes != expected_attributes ||
        memcmp(actual, expected, expected_size) != 0) return 0;
    if (!request->ops->write_variable(name, NULL, 0u, expected_attributes,
                                      request->ops->context)) return 0;
    actual_size = sizeof(actual);
    actual_attributes = 0;
    result = read_name(request->ops, name, actual, &actual_size, &actual_attributes);
    return result == 0;
}

static int write_exact(const ib_bootnext_request_t *request, const wchar_t *name,
                       const unsigned char *value, size_t value_size,
                       unsigned long attributes)
{
    if (!request->ops->write_variable(name, value, value_size, attributes,
                                      request->ops->context)) return 0;
    {
        unsigned char actual[4096];
        size_t actual_size = sizeof(actual);
        unsigned long actual_attributes = 0;
        int result = read_name(request->ops, name, actual, &actual_size,
                               &actual_attributes);
        return result > 0 && actual_size == value_size &&
               actual_attributes == attributes && memcmp(actual, value, value_size) == 0;
    }
}

static int bootnext_allows_entry_cleanup(const ib_bootnext_request_t *request,
                                         unsigned short candidate)
{
    unsigned char value[4096];
    size_t value_size = sizeof(value);
    unsigned long attributes = 0;
    unsigned short referenced;
    int result = read_name(request->ops, L"BootNext", value, &value_size, &attributes);
    if (result == 0) return 1;
    if (result < 0 || value_size != 2u || attributes != IB_BOOTNEXT_VARIABLE_ATTRIBUTES)
        return 0;
    referenced = (unsigned short)(value[0] | ((unsigned short)value[1] << 8));
    return referenced == candidate;
}

int ib_bootnext_set_profile(const ib_bootnext_request_t *request,
                            ib_bootnext_profile_t profile,
                            unsigned short *boot_number,
                            wchar_t *error, size_t error_count)
{
    unsigned char value[4096];
    size_t value_size = sizeof(value);
    unsigned char option[512];
    size_t option_size = sizeof(option);
    unsigned int candidate;
    unsigned int first_unused = 0x10000u;
    unsigned int owned = 0x10000u;
    unsigned int exact_matches = 0u;
    wchar_t name[9];
    unsigned long attributes = 0;
    int owned_attributes_mismatch = 0;
    int reused_entry;
    int result;
    if (request == NULL || request->ops == NULL ||
        request->ops->read_variable == NULL || request->ops->write_variable == NULL ||
        !ib_bootnext_serialize_profile(&request->partition, profile,
                                       option, &option_size)) {
        set_error(error, error_count, L"BootNext request is invalid");
        return 0;
    }
    result = read_name(request->ops, L"BootNext", value, &value_size, &attributes);
    if (result > 0) {
        set_error(error, error_count, L"BootNext already exists; refusing to overwrite it");
        return 0;
    }
    if (result < 0) {
        set_error(error, error_count, L"BootNext cannot be read");
        return 0;
    }
    /* Scan the full namespace before writing so stale owned entries are reused. */
    for (candidate = 0u; candidate <= 0xffffu; ++candidate) {
        value_size = sizeof(value);
        attributes = 0;
        boot_name((unsigned short)candidate, name);
        result = read_name(request->ops, name, value, &value_size, &attributes);
        if (result < 0) {
            set_error(error, error_count, L"%ls cannot be read", name);
            return 0;
        }
        if (result == 0) {
            if (first_unused == 0x10000u) first_unused = candidate;
            continue;
        }
        if (value_size == option_size && memcmp(value, option, option_size) == 0) {
            ++exact_matches;
            if (attributes != IB_BOOTNEXT_VARIABLE_ATTRIBUTES) {
                owned_attributes_mismatch = 1;
            } else {
                owned = candidate;
            }
        }
    }
    if (exact_matches > 1u) {
        set_error(error, error_count, L"multiple owned Boot#### entries require manual review");
        return 0;
    }
    if (owned_attributes_mismatch) {
        set_error(error, error_count, L"owned Boot#### entry has unexpected attributes");
        return 0;
    }
    reused_entry = owned != 0x10000u;
    if (!reused_entry) {
        if (first_unused == 0x10000u) {
            set_error(error, error_count, L"no unused Boot#### entry is available");
            return 0;
        }
        candidate = first_unused;
    } else {
        candidate = owned;
    }
    boot_name((unsigned short)candidate, name);

    /* Do not overwrite BootNext created while the full Boot#### scan ran. */
    value_size = sizeof(value);
    attributes = 0;
    result = read_name(request->ops, L"BootNext", value, &value_size, &attributes);
    if (result != 0) {
        set_error(error, error_count, result < 0
                  ? L"BootNext cannot be read after the Boot#### scan"
                  : L"BootNext appeared during the Boot#### scan; refusing to overwrite it");
        return 0;
    }

    if (reused_entry) {
        /* Revalidate the existing option immediately before creating BootNext. */
        value_size = sizeof(value);
        attributes = 0;
        result = read_name(request->ops, name, value, &value_size, &attributes);
        if (result <= 0 || value_size != option_size ||
            attributes != IB_BOOTNEXT_VARIABLE_ATTRIBUTES ||
            memcmp(value, option, option_size) != 0) {
            set_error(error, error_count, L"owned %ls changed before BootNext could be set", name);
            return 0;
        }
    } else {
        /* The first free slot may have been claimed during the full scan. */
        value_size = sizeof(value);
        attributes = 0;
        result = read_name(request->ops, name, value, &value_size, &attributes);
        if (result != 0) {
            set_error(error, error_count, result < 0
                      ? L"%ls cannot be read before creation"
                      : L"%ls became populated during the Boot#### scan", name);
            return 0;
        }
        if (!write_exact(request, name, option, option_size,
                         IB_BOOTNEXT_VARIABLE_ATTRIBUTES)) {
            int present = 0;
            if (!bootnext_allows_entry_cleanup(request, (unsigned short)candidate) ||
                !remove_exact(request, name, option, option_size,
                              IB_BOOTNEXT_VARIABLE_ATTRIBUTES, &present)) {
                set_error(error, error_count, L"%ls cannot be verified and cleanup was refused", name);
            } else {
                set_error(error, error_count, L"%ls cannot be verified; new entry was removed", name);
            }
            return 0;
        }
    }

    put_u16(value, (unsigned short)candidate);
    if (!write_exact(request, L"BootNext", value, 2u,
                     IB_BOOTNEXT_VARIABLE_ATTRIBUTES)) {
        int bootnext_present = 0;
        int bootnext_clean = remove_exact(request, L"BootNext", value, 2u,
                                          IB_BOOTNEXT_VARIABLE_ATTRIBUTES,
                                          &bootnext_present);
        if (reused_entry) {
            set_error(error, error_count,
                      bootnext_clean
                      ? L"BootNext cannot be verified; existing %ls was preserved"
                      : L"BootNext cannot be verified; cleanup was refused or incomplete; existing %ls was preserved",
                      name);
        } else {
            int entry_present = 0;
            if (bootnext_clean &&
                remove_exact(request, name, option, option_size,
                             IB_BOOTNEXT_VARIABLE_ATTRIBUTES, &entry_present)) {
                set_error(error, error_count, L"BootNext cannot be verified; new %ls was removed", name);
            } else {
                set_error(error, error_count,
                          L"BootNext cannot be verified; cleanup was refused or incomplete");
            }
        }
        return 0;
    }
    if (boot_number != NULL) *boot_number = (unsigned short)candidate;
    return 1;
}

int ib_bootnext_set(const ib_bootnext_request_t *request,
                    unsigned short *boot_number,
                    wchar_t *error, size_t error_count)
{
    return ib_bootnext_set_profile(request, IB_BOOTNEXT_PROFILE_LEGACY,
                                   boot_number, error, error_count);
}

int ib_bootnext_clear_owned_profile(const ib_bootnext_request_t *request,
                                    ib_bootnext_profile_t profile,
                                    wchar_t *error, size_t error_count)
{
    unsigned char value[4096];
    unsigned char option[512];
    size_t option_size = sizeof(option);
    size_t value_size;
    unsigned int candidate = 0u;
    unsigned int owned = 0x10000u;
    wchar_t name[9];
    unsigned long attributes = 0;
    int owned_attributes_mismatch = 0;
    int result;
    int bootnext_present;
    unsigned short bootnext_number = 0;
    if (request == NULL || request->ops == NULL ||
        request->ops->read_variable == NULL || request->ops->write_variable == NULL) {
        set_error(error, error_count, L"BootNext request is invalid");
        return 0;
    }
    if (profile != IB_BOOTNEXT_PROFILE_LEGACY &&
        profile != IB_BOOTNEXT_PROFILE_INVENTORY &&
        profile != IB_BOOTNEXT_PROFILE_SHIM_INVENTORY) {
        set_error(error, error_count, L"BootNext profile is invalid");
        return 0;
    }
    value_size = sizeof(value);
    result = read_name(request->ops, L"BootNext", value, &value_size, &attributes);
    if (result < 0 || (result > 0 &&
                       (value_size != 2u || attributes != IB_BOOTNEXT_VARIABLE_ATTRIBUTES))) {
        set_error(error, error_count, L"BootNext cannot be safely inspected");
        return 0;
    }
    bootnext_present = result > 0;
    if (bootnext_present) bootnext_number = (unsigned short)(value[0] | ((unsigned short)value[1] << 8));
    if (bootnext_present) {
        /* BootNext gives us an exact target; do not scan/delete any other entry. */
        candidate = (unsigned int)bootnext_number;
        boot_name(bootnext_number, name);
        value_size = sizeof(value);
        attributes = 0;
        result = read_name(request->ops, name, value, &value_size, &attributes);
        if (result < 0) {
            set_error(error, error_count, L"%ls cannot be read", name);
            return 0;
        }
        if (result == 0) {
            set_error(error, error_count, L"BootNext target %ls is missing", name);
            return 0;
        }
        if (ib_bootnext_is_owned_profile(value, value_size, &request->partition,
                                         profile) &&
            attributes != IB_BOOTNEXT_VARIABLE_ATTRIBUTES) {
            owned_attributes_mismatch = 1;
        }
        if (!ib_bootnext_is_owned_profile(value, value_size, &request->partition,
                                          profile) ||
            attributes != IB_BOOTNEXT_VARIABLE_ATTRIBUTES) {
            set_error(error, error_count, owned_attributes_mismatch
                      ? L"BootNext target %ls has unexpected attributes"
                      : L"BootNext target %ls is not owned; refusing to clear it", name);
            return 0;
        }
        owned = candidate;
    } else {
        for (candidate = 0u; candidate <= 0xffffu; ++candidate) {
            value_size = sizeof(value);
            attributes = 0;
            boot_name((unsigned short)candidate, name);
            result = read_name(request->ops, name, value, &value_size, &attributes);
            if (result < 0) {
                set_error(error, error_count, L"%ls cannot be read", name);
                return 0;
            }
            if (result > 0 &&
                ib_bootnext_is_owned_profile(value, value_size, &request->partition,
                                             profile)) {
                if (attributes != IB_BOOTNEXT_VARIABLE_ATTRIBUTES) {
                    owned_attributes_mismatch = 1;
                    continue;
                }
                if (owned != 0x10000u) {
                    set_error(error, error_count, L"multiple owned Boot#### entries require manual review");
                    return 0;
                }
                owned = candidate;
            }
        }
    }
    if (owned_attributes_mismatch) {
        set_error(error, error_count, L"owned Boot#### entry has unexpected attributes");
        return 0;
    }
    if (owned == 0x10000u) {
        set_error(error, error_count, L"owned Boot#### entry was not found");
        return 0;
    }

    if (!ib_bootnext_serialize_profile(&request->partition, profile,
                                       option, &option_size)) {
        set_error(error, error_count, L"owned Boot#### entry cannot be reconstructed");
        return 0;
    }
    boot_name((unsigned short)owned, name);
    /* Remove the entry first.  If this fails, BootNext remains usable. */
    {
        int entry_present = 0;
        if (!remove_exact(request, name, option, option_size,
                          IB_BOOTNEXT_VARIABLE_ATTRIBUTES, &entry_present)) {
            set_error(error, error_count, L"owned %ls cannot be safely removed", name);
            return 0;
        }
    }
    if (!bootnext_present) return 1;

    /* Revalidate BootNext after the entry transaction, then remove it. */
    {
        unsigned char bootnext_value[2];
        int bootnext_was_present = 0;
        put_u16(bootnext_value, (unsigned short)owned);
        if (!remove_exact(request, L"BootNext", bootnext_value, sizeof(bootnext_value),
                          IB_BOOTNEXT_VARIABLE_ATTRIBUTES, &bootnext_was_present)) {
            /* Restore the owned entry so a failed cleanup remains recoverable. */
            if (!write_exact(request, name, option, option_size,
                             IB_BOOTNEXT_VARIABLE_ATTRIBUTES)) {
                set_error(error, error_count,
                          L"BootNext cannot be cleared and owned %ls restoration failed", name);
            } else {
                set_error(error, error_count,
                          L"BootNext cannot be cleared; owned %ls was restored", name);
            }
            return 0;
        }
        if (!bootnext_was_present) {
            /* It was removed concurrently; the requested final state is met. */
            return 1;
        }
    }
    return 1;
}

int ib_bootnext_clear_owned(const ib_bootnext_request_t *request,
                            wchar_t *error, size_t error_count)
{
    return ib_bootnext_clear_owned_profile(request, IB_BOOTNEXT_PROFILE_LEGACY,
                                           error, error_count);
}
