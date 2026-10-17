#include "hwid_spoofer.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define HWID_ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define HWID_MAX_NATIVE_VALUE_CHARS 4096u
#define HWID_STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000DL)
#define HWID_STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BBL)
#define HWID_STATUS_DEVICE_BUSY ((NTSTATUS)0xC000009EL)
#define HWID_STATUS_DEVICE_NOT_READY ((NTSTATUS)0xC00000A3L)
#define HWID_STATUS_INVALID_DEVICE_STATE ((NTSTATUS)0xC0000184L)
#define HWID_STATUS_DATA_ERROR ((NTSTATUS)0xC000003EL)
#define HWID_STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023L)
#define HWID_STATUS_NO_MEMORY ((NTSTATUS)0xC0000017L)

static const HWID_SPOOF_FIELD_INFO g_field_map[HwidFieldCount] = {
    { HwidFieldDiskSerial, "FakeDiskSerial", "Win32_DiskDrive.SerialNumber" },
    { HwidFieldMacAddress, "FakeMacAddress", "Win32_NetworkAdapter.MACAddress" },
    { HwidFieldSystemSerial, "FakeSMBIOSSerial", "SMBIOS Type 1.SerialNumber" },
    { HwidFieldSystemManufacturer, "FakeSMBIOSManufacturer", "SMBIOS Type 1.Manufacturer" },
    { HwidFieldSystemProduct, "FakeSMBIOSProduct", "SMBIOS Type 1.Product" },
    { HwidFieldBoardSerial, "FakeMotherboardSerial", "SMBIOS Type 2.SerialNumber" },
    { HwidFieldBoardManufacturer, "FakeMotherboardManufacturer", "SMBIOS Type 2.Manufacturer" },
    { HwidFieldBoardProduct, "FakeMotherboardProduct", "SMBIOS Type 2.Product" },
    { HwidFieldSystemUuid, "FakeSMBIOSUUID", "SMBIOS Type 1.UUID" }
};

static SRWLOCK g_state_lock = SRWLOCK_INIT;
static HWID_SPOOF_CONFIG g_config;
static DWORD g_field_mask;
static HWID_SPOOF_DEVICE_BINDING g_bindings[HWID_MAX_DEVICE_BINDINGS];
static SIZE_T g_binding_count;
static BOOL g_initialized;
static BOOL g_spoofer_active;

static BOOL bounded_length(const WCHAR *text, SIZE_T capacity, SIZE_T *length)
{
    SIZE_T i;
    if (text == NULL || length == NULL) return FALSE;
    for (i = 0; i < capacity; ++i) {
        if (text[i] == L'\0') {
            *length = i;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL copy_bounded(WCHAR *destination, SIZE_T capacity,
                         const WCHAR *source, SIZE_T *length)
{
    SIZE_T i;
    if (destination == NULL || source == NULL || capacity == 0u ||
        length == NULL) {
        return FALSE;
    }
    for (i = 0; i < capacity; ++i) {
        destination[i] = source[i];
        if (source[i] == L'\0') {
            *length = i;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL is_ascii_hex(WCHAR value)
{
    return (value >= L'0' && value <= L'9') ||
           (value >= L'A' && value <= L'F') ||
           (value >= L'a' && value <= L'f');
}

static BOOL is_serial_character(WCHAR value)
{
    return (value >= L'A' && value <= L'Z') ||
           (value >= L'a' && value <= L'z') ||
           (value >= L'0' && value <= L'9') ||
           value == L'_' || value == L'.' || value == L'-';
}

static BOOL valid_serial_text(const WCHAR *text, SIZE_T capacity,
                              SIZE_T *length)
{
    SIZE_T i;
    SIZE_T text_length;
    if (!bounded_length(text, capacity, &text_length) || text_length == 0u ||
        text_length > 63u) {
        return FALSE;
    }
    for (i = 0; i < text_length; ++i) {
        if (!is_serial_character(text[i])) return FALSE;
    }
    if (length != NULL) *length = text_length;
    return TRUE;
}

static BOOL valid_printable_text(const WCHAR *text, SIZE_T capacity)
{
    SIZE_T i;
    SIZE_T text_length;
    if (!bounded_length(text, capacity, &text_length) || text_length == 0u ||
        text_length > 63u) {
        return FALSE;
    }
    for (i = 0; i < text_length; ++i) {
        if (text[i] < 0x20u || text[i] > 0x7eu) return FALSE;
    }
    return TRUE;
}

static BOOL all_zero_text(const WCHAR *text, SIZE_T capacity)
{
    SIZE_T i;
    for (i = 0; i < capacity; ++i) {
        if (text[i] != L'\0') return FALSE;
    }
    return TRUE;
}

static BOOL valid_uuid_text(const WCHAR *text, SIZE_T capacity)
{
    static const SIZE_T hyphens[] = { 8u, 13u, 18u, 23u };
    SIZE_T i;
    SIZE_T length;
    BOOL all_zero = TRUE;
    BOOL all_f = TRUE;
    SIZE_T hyphen_index = 0u;

    if (!bounded_length(text, capacity, &length) || length != 36u) {
        return FALSE;
    }
    for (i = 0; i < length; ++i) {
        if (hyphen_index < HWID_ARRAY_COUNT(hyphens) &&
            i == hyphens[hyphen_index]) {
            if (text[i] != L'-') return FALSE;
            ++hyphen_index;
            continue;
        }
        if (!is_ascii_hex(text[i])) return FALSE;
        if (text[i] != L'0') all_zero = FALSE;
        if (text[i] != L'F' && text[i] != L'f') all_f = FALSE;
    }
    return !all_zero && !all_f;
}

static BOOL valid_mac_bytes(const BYTE mac[6])
{
    SIZE_T i;
    BOOL all_zero = TRUE;
    BOOL all_ff = TRUE;
    if ((mac[0] & 0x01u) != 0u) return FALSE;
    for (i = 0; i < 6u; ++i) {
        if (mac[i] != 0u) all_zero = FALSE;
        if (mac[i] != 0xffu) all_ff = FALSE;
    }
    return !all_zero && !all_ff;
}

static BOOL valid_mac_text(const WCHAR *text, SIZE_T capacity,
                           SIZE_T *length_out, BYTE decoded_octets[6])
{
    BYTE octets[6] = { 0u, 0u, 0u, 0u, 0u, 0u };
    SIZE_T length;
    SIZE_T i;
    SIZE_T nibble = 0u;
    WCHAR separator = L'\0';

    if (!bounded_length(text, capacity, &length) ||
        (length != 12u && length != 17u)) {
        return FALSE;
    }
    if (length == 17u) {
        separator = text[2];
        if (separator != L':' && separator != L'-') return FALSE;
    }
    for (i = 0; i < length; ++i) {
        unsigned value;
        if (length == 17u && i % 3u == 2u) {
            if (text[i] != separator) return FALSE;
            continue;
        }
        if (!is_ascii_hex(text[i])) return FALSE;
        if (text[i] >= L'0' && text[i] <= L'9') {
            value = (unsigned)(text[i] - L'0');
        } else if (text[i] >= L'A' && text[i] <= L'F') {
            value = (unsigned)(text[i] - L'A') + 10u;
        } else {
            value = (unsigned)(text[i] - L'a') + 10u;
        }
        if ((nibble & 1u) == 0u) octets[nibble / 2u] = (BYTE)(value << 4);
        else octets[nibble / 2u] = (BYTE)(octets[nibble / 2u] | value);
        ++nibble;
    }
    if (nibble != 12u || !valid_mac_bytes(octets)) return FALSE;
    if (length_out != NULL) *length_out = length;
    if (decoded_octets != NULL) memcpy(decoded_octets, octets, sizeof(octets));
    return TRUE;
}

static BOOL valid_config_boolean(BOOL value)
{
    return value == FALSE || value == TRUE;
}

NTSTATUS HwidSpooferValidateConfig(const HWID_SPOOF_CONFIG *config)
{
    if (config == NULL) return HWID_STATUS_INVALID_PARAMETER;
    if (!valid_config_boolean(config->SpoofDiskSerial) ||
        !valid_config_boolean(config->SpoofMacAddress) ||
        !valid_config_boolean(config->SpoofSMBIOS) ||
        !valid_config_boolean(config->SpoofGPU) ||
        !valid_config_boolean(config->SpoofMotherboard) ||
        !valid_config_boolean(config->SpoofCPU) ||
        !valid_config_boolean(config->SpoofSMBIOSUUID)) {
        return HWID_STATUS_INVALID_PARAMETER;
    }
    if (config->SpoofCPU || config->SpoofGPU ||
        !all_zero_text(config->FakeCPUSerial,
                       HWID_ARRAY_COUNT(config->FakeCPUSerial)) ||
        !all_zero_text(config->FakeGPUSerial,
                       HWID_ARRAY_COUNT(config->FakeGPUSerial))) {
        return HWID_STATUS_NOT_SUPPORTED;
    }
    if (config->SpoofDiskSerial &&
        !valid_serial_text(config->FakeDiskSerial,
                           HWID_ARRAY_COUNT(config->FakeDiskSerial), NULL)) {
        return HWID_STATUS_INVALID_PARAMETER;
    }
    if (config->SpoofMacAddress && !valid_mac_bytes(config->FakeMacAddress)) {
        return HWID_STATUS_INVALID_PARAMETER;
    }
    if (config->SpoofSMBIOS &&
        (!valid_serial_text(config->FakeSMBIOSSerial,
                            HWID_ARRAY_COUNT(config->FakeSMBIOSSerial), NULL) ||
         !valid_printable_text(config->FakeSMBIOSManufacturer,
                              HWID_ARRAY_COUNT(config->FakeSMBIOSManufacturer)) ||
         !valid_printable_text(config->FakeSMBIOSProduct,
                              HWID_ARRAY_COUNT(config->FakeSMBIOSProduct)))) {
        return HWID_STATUS_INVALID_PARAMETER;
    }
    if (config->SpoofMotherboard &&
        (!valid_serial_text(config->FakeMotherboardSerial,
                            HWID_ARRAY_COUNT(config->FakeMotherboardSerial), NULL) ||
         !valid_printable_text(config->FakeMotherboardManufacturer,
                              HWID_ARRAY_COUNT(config->FakeMotherboardManufacturer)) ||
         !valid_printable_text(config->FakeMotherboardProduct,
                              HWID_ARRAY_COUNT(config->FakeMotherboardProduct)))) {
        return HWID_STATUS_INVALID_PARAMETER;
    }
    if (config->SpoofSMBIOSUUID &&
        !valid_uuid_text(config->FakeSMBIOSUUID,
                         HWID_ARRAY_COUNT(config->FakeSMBIOSUUID))) {
        return HWID_STATUS_INVALID_PARAMETER;
    }
    return (NTSTATUS)0;
}

const HWID_SPOOF_FIELD_INFO *HwidSpooferGetFieldMap(SIZE_T *count)
{
    if (count != NULL) *count = HWID_ARRAY_COUNT(g_field_map);
    return g_field_map;
}

static void generate_random_serial(WCHAR *buffer, SIZE_T capacity)
{
    static const WCHAR characters[] = L"0123456789ABCDEF";
    SIZE_T i;
    if (buffer == NULL || capacity == 0u) return;
    for (i = 0; i + 1u < capacity; ++i) {
        buffer[i] = characters[(unsigned)rand() % 16u];
    }
    buffer[capacity - 1u] = L'\0';
}

static void generate_random_mac(BYTE mac[6])
{
    SIZE_T i;
    if (mac == NULL) return;
    mac[0] = (BYTE)(((unsigned)rand() & 0xfcu) | 0x02u);
    for (i = 1u; i < 6u; ++i) mac[i] = (BYTE)rand();
}

VOID HwidSpooferGenerateRandomConfig(PHWID_SPOOF_CONFIG config)
{
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    config->SpoofDiskSerial = TRUE;
    config->SpoofMacAddress = TRUE;
    config->SpoofSMBIOS = TRUE;
    config->SpoofMotherboard = TRUE;
    config->SpoofGPU = FALSE;
    config->SpoofCPU = FALSE;
    config->SpoofSMBIOSUUID = FALSE;
    generate_random_serial(config->FakeDiskSerial,
                           HWID_ARRAY_COUNT(config->FakeDiskSerial));
    generate_random_mac(config->FakeMacAddress);
    generate_random_serial(config->FakeSMBIOSSerial,
                           HWID_ARRAY_COUNT(config->FakeSMBIOSSerial));
    (void)wcscpy_s(config->FakeSMBIOSManufacturer,
                   HWID_ARRAY_COUNT(config->FakeSMBIOSManufacturer), L"Generic");
    (void)wcscpy_s(config->FakeSMBIOSProduct,
                   HWID_ARRAY_COUNT(config->FakeSMBIOSProduct), L"System Product");
    generate_random_serial(config->FakeMotherboardSerial,
                           HWID_ARRAY_COUNT(config->FakeMotherboardSerial));
    (void)wcscpy_s(config->FakeMotherboardManufacturer,
                   HWID_ARRAY_COUNT(config->FakeMotherboardManufacturer), L"Generic");
    (void)wcscpy_s(config->FakeMotherboardProduct,
                   HWID_ARRAY_COUNT(config->FakeMotherboardProduct), L"Motherboard");
}

NTSTATUS HwidSpooferInitialize(PHWID_SPOOF_CONFIG config)
{
    HWID_SPOOF_CONFIG candidate;
    NTSTATUS status;
    if (config == NULL) HwidSpooferGenerateRandomConfig(&candidate);
    else memcpy(&candidate, config, sizeof(candidate));

    AcquireSRWLockExclusive(&g_state_lock);
    if (g_spoofer_active) {
        status = HWID_STATUS_DEVICE_BUSY;
    } else {
        status = HwidSpooferValidateConfig(&candidate);
        if (status == (NTSTATUS)0) {
            SecureZeroMemory(&g_config, sizeof(g_config));
            memcpy(&g_config, &candidate, sizeof(g_config));
            SecureZeroMemory(g_bindings, sizeof(g_bindings));
            g_binding_count = 0u;
            g_field_mask = HWID_ALL_CONFIRMED_FIELDS;
            g_initialized = TRUE;
            g_spoofer_active = FALSE;
        }
    }
    ReleaseSRWLockExclusive(&g_state_lock);
    SecureZeroMemory(&candidate, sizeof(candidate));
    return status;
}

static BOOL field_is_valid(HWID_SPOOF_FIELD field)
{
    return (unsigned)field < (unsigned)HwidFieldCount;
}

static BOOL field_is_serial(HWID_SPOOF_FIELD field)
{
    return field == HwidFieldDiskSerial || field == HwidFieldSystemSerial ||
           field == HwidFieldBoardSerial;
}

static BOOL field_is_device_bound(HWID_SPOOF_FIELD field)
{
    return field == HwidFieldDiskSerial || field == HwidFieldMacAddress;
}

static BOOL device_key_valid(const WCHAR *key, SIZE_T *length)
{
    SIZE_T i;
    if (key == NULL || key[0] == L'\0') return FALSE;
    for (i = 0; i < 512u; ++i) {
        if (key[i] == L'\0') {
            if (length != NULL) *length = i;
            return TRUE;
        }
    }
    return FALSE;
}

static NTSTATUS validate_binding(const HWID_SPOOF_DEVICE_BINDING *binding)
{
    SIZE_T original_length;
    SIZE_T replacement_length;
    SIZE_T key_length;
    BYTE original_mac[6];
    BYTE replacement_mac[6];
    if (binding == NULL || !field_is_device_bound(binding->Field) ||
        !device_key_valid(binding->DeviceKey, &key_length) ||
        key_length > 511u) {
        return HWID_STATUS_INVALID_PARAMETER;
    }
    if (binding->Field == HwidFieldDiskSerial) {
        if (!valid_serial_text(binding->OriginalValue,
                               HWID_ARRAY_COUNT(binding->OriginalValue),
                               &original_length) ||
            !valid_serial_text(binding->ReplacementValue,
                               HWID_ARRAY_COUNT(binding->ReplacementValue),
                               &replacement_length) ||
            original_length != replacement_length ||
            wcscmp(binding->OriginalValue, binding->ReplacementValue) == 0) {
            return HWID_STATUS_INVALID_PARAMETER;
        }
    } else if (!valid_mac_text(binding->OriginalValue,
                               HWID_ARRAY_COUNT(binding->OriginalValue), NULL,
                               original_mac) ||
               !valid_mac_text(binding->ReplacementValue,
                               HWID_ARRAY_COUNT(binding->ReplacementValue), NULL,
                               replacement_mac) ||
               memcmp(original_mac, replacement_mac,
                      sizeof(original_mac)) == 0) {
        return HWID_STATUS_INVALID_PARAMETER;
    }
    return (NTSTATUS)0;
}

static BOOL has_binding_for_field(HWID_SPOOF_FIELD field)
{
    SIZE_T i;
    for (i = 0; i < g_binding_count; ++i) {
        if (g_bindings[i].Field == field) return TRUE;
    }
    return FALSE;
}

NTSTATUS HwidSpooferSetFieldMask(DWORD mask)
{
    NTSTATUS status = (NTSTATUS)0;
    if ((mask & ~HWID_ALL_CONFIRMED_FIELDS) != 0u) {
        return HWID_STATUS_INVALID_PARAMETER;
    }
    AcquireSRWLockExclusive(&g_state_lock);
    if (g_spoofer_active) status = HWID_STATUS_DEVICE_BUSY;
    else if (!g_initialized) status = HWID_STATUS_DEVICE_NOT_READY;
    else g_field_mask = mask;
    ReleaseSRWLockExclusive(&g_state_lock);
    return status;
}

NTSTATUS HwidSpooferSetDeviceBindings(
    const HWID_SPOOF_DEVICE_BINDING *bindings, SIZE_T count)
{
    HWID_SPOOF_DEVICE_BINDING *staged = NULL;
    NTSTATUS status = (NTSTATUS)0;
    SIZE_T i;
    SIZE_T j;

    if (count > HWID_MAX_DEVICE_BINDINGS ||
        (count != 0u && bindings == NULL)) {
        return HWID_STATUS_INVALID_PARAMETER;
    }
    if (count != 0u) {
        staged = (HWID_SPOOF_DEVICE_BINDING *)HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(*staged));
        if (staged == NULL) return HWID_STATUS_NO_MEMORY;
        for (i = 0; i < count; ++i) {
            memcpy(&staged[i], &bindings[i], sizeof(staged[i]));
            status = validate_binding(&staged[i]);
            if (status != (NTSTATUS)0) break;
            for (j = 0; j < i; ++j) {
                if (staged[i].Field == staged[j].Field &&
                    wcscmp(staged[i].DeviceKey, staged[j].DeviceKey) == 0) {
                    status = HWID_STATUS_INVALID_PARAMETER;
                    break;
                }
            }
            if (status != (NTSTATUS)0) break;
        }
    }
    if (status == (NTSTATUS)0) {
        AcquireSRWLockExclusive(&g_state_lock);
        if (g_spoofer_active) status = HWID_STATUS_DEVICE_BUSY;
        else if (!g_initialized) status = HWID_STATUS_DEVICE_NOT_READY;
        else {
            SecureZeroMemory(g_bindings, sizeof(g_bindings));
            if (count != 0u) {
                memcpy(g_bindings, staged, count * sizeof(*staged));
            }
            g_binding_count = count;
        }
        ReleaseSRWLockExclusive(&g_state_lock);
    }
    if (staged != NULL) {
        SecureZeroMemory(staged, count * sizeof(*staged));
        HeapFree(GetProcessHeap(), 0u, staged);
    }
    return status;
}

NTSTATUS HwidSpooferApply(VOID)
{
    NTSTATUS status = (NTSTATUS)0;
    AcquireSRWLockExclusive(&g_state_lock);
    if (g_spoofer_active) {
        status = (NTSTATUS)0;
    } else if (!g_initialized) {
        status = HWID_STATUS_DEVICE_NOT_READY;
    } else if ((g_config.SpoofDiskSerial &&
                (g_field_mask & HWID_FIELD_MASK(HwidFieldDiskSerial)) != 0u &&
                !has_binding_for_field(HwidFieldDiskSerial)) ||
               (g_config.SpoofMacAddress &&
                (g_field_mask & HWID_FIELD_MASK(HwidFieldMacAddress)) != 0u &&
                !has_binding_for_field(HwidFieldMacAddress))) {
        status = HWID_STATUS_DEVICE_NOT_READY;
    } else {
        g_spoofer_active = TRUE;
    }
    ReleaseSRWLockExclusive(&g_state_lock);
    return status;
}

NTSTATUS HwidSpooferRemove(VOID)
{
    AcquireSRWLockExclusive(&g_state_lock);
    g_spoofer_active = FALSE;
    ReleaseSRWLockExclusive(&g_state_lock);
    return (NTSTATUS)0;
}

BOOL HwidSpooferIsActive(VOID)
{
    BOOL active;
    AcquireSRWLockShared(&g_state_lock);
    active = g_spoofer_active;
    ReleaseSRWLockShared(&g_state_lock);
    return active;
}

VOID HwidSpooferCleanup(VOID)
{
    AcquireSRWLockExclusive(&g_state_lock);
    g_spoofer_active = FALSE;
    g_initialized = FALSE;
    g_field_mask = 0u;
    g_binding_count = 0u;
    SecureZeroMemory(&g_config, sizeof(g_config));
    SecureZeroMemory(g_bindings, sizeof(g_bindings));
    ReleaseSRWLockExclusive(&g_state_lock);
}

static BOOL field_is_enabled(HWID_SPOOF_FIELD field)
{
    if ((g_field_mask & HWID_FIELD_MASK(field)) == 0u) return FALSE;
    switch (field) {
    case HwidFieldDiskSerial: return g_config.SpoofDiskSerial;
    case HwidFieldMacAddress: return g_config.SpoofMacAddress;
    case HwidFieldSystemSerial:
    case HwidFieldSystemManufacturer:
    case HwidFieldSystemProduct: return g_config.SpoofSMBIOS;
    case HwidFieldBoardSerial:
    case HwidFieldBoardManufacturer:
    case HwidFieldBoardProduct: return g_config.SpoofMotherboard;
    case HwidFieldSystemUuid: return g_config.SpoofSMBIOSUUID;
    default: return FALSE;
    }
}

static const WCHAR *configured_replacement(HWID_SPOOF_FIELD field,
                                           SIZE_T *capacity)
{
    if (capacity == NULL) return NULL;
    switch (field) {
    case HwidFieldSystemSerial:
        *capacity = HWID_ARRAY_COUNT(g_config.FakeSMBIOSSerial);
        return g_config.FakeSMBIOSSerial;
    case HwidFieldSystemManufacturer:
        *capacity = HWID_ARRAY_COUNT(g_config.FakeSMBIOSManufacturer);
        return g_config.FakeSMBIOSManufacturer;
    case HwidFieldSystemProduct:
        *capacity = HWID_ARRAY_COUNT(g_config.FakeSMBIOSProduct);
        return g_config.FakeSMBIOSProduct;
    case HwidFieldBoardSerial:
        *capacity = HWID_ARRAY_COUNT(g_config.FakeMotherboardSerial);
        return g_config.FakeMotherboardSerial;
    case HwidFieldBoardManufacturer:
        *capacity = HWID_ARRAY_COUNT(g_config.FakeMotherboardManufacturer);
        return g_config.FakeMotherboardManufacturer;
    case HwidFieldBoardProduct:
        *capacity = HWID_ARRAY_COUNT(g_config.FakeMotherboardProduct);
        return g_config.FakeMotherboardProduct;
    case HwidFieldSystemUuid:
        *capacity = HWID_ARRAY_COUNT(g_config.FakeSMBIOSUUID);
        return g_config.FakeSMBIOSUUID;
    default: return NULL;
    }
}

NTSTATUS HwidSpooferReadOverride(
    HWID_SPOOF_FIELD field, const WCHAR *deviceKey, const WCHAR *actualValue,
    WCHAR *output, SIZE_T outputCharacters)
{
    WCHAR actual_copy[HWID_MAX_NATIVE_VALUE_CHARS + 1u];
    WCHAR key_copy[512];
    SIZE_T actual_length = 0u;
    SIZE_T key_length = 0u;
    BOOL key_valid = FALSE;
    const WCHAR *replacement = NULL;
    SIZE_T replacement_length = 0u;
    SIZE_T replacement_capacity = 0u;
    SIZE_T i;
    NTSTATUS status = (NTSTATUS)0;
    BOOL enabled;

    if (output == NULL) return HWID_STATUS_INVALID_PARAMETER;
    if (outputCharacters == 0u) return HWID_STATUS_BUFFER_TOO_SMALL;
    if (!field_is_valid(field) || actualValue == NULL ||
        !copy_bounded(actual_copy, HWID_ARRAY_COUNT(actual_copy),
                      actualValue, &actual_length)) {
        output[0] = L'\0';
        return HWID_STATUS_INVALID_PARAMETER;
    }
    if (deviceKey != NULL) {
        key_valid = copy_bounded(key_copy, HWID_ARRAY_COUNT(key_copy),
                                 deviceKey, &key_length) && key_length != 0u;
    }
    output[0] = L'\0';

    AcquireSRWLockShared(&g_state_lock);
    enabled = g_initialized && g_spoofer_active && field_is_enabled(field);
    if (enabled && field_is_device_bound(field)) {
        const HWID_SPOOF_DEVICE_BINDING *binding = NULL;
        if (key_valid) {
            for (i = 0; i < g_binding_count; ++i) {
                if (g_bindings[i].Field == field &&
                    wcscmp(g_bindings[i].DeviceKey, key_copy) == 0) {
                    binding = &g_bindings[i];
                    break;
                }
            }
        }
        if (binding != NULL) {
            SIZE_T original_length;
            SIZE_T bound_replacement_length;
            if (actual_length > 63u ||
                !bounded_length(binding->OriginalValue,
                    HWID_ARRAY_COUNT(binding->OriginalValue), &original_length) ||
                original_length != actual_length ||
                wmemcmp(binding->OriginalValue, actual_copy, actual_length) != 0) {
                status = HWID_STATUS_DATA_ERROR;
            } else {
                replacement = binding->ReplacementValue;
                if (!bounded_length(replacement,
                        HWID_ARRAY_COUNT(binding->ReplacementValue),
                        &bound_replacement_length) ||
                    (field == HwidFieldDiskSerial &&
                     bound_replacement_length != actual_length)) {
                    status = HWID_STATUS_INVALID_DEVICE_STATE;
                } else {
                    replacement_length = bound_replacement_length;
                }
            }
        } else {
            replacement = actual_copy;
            replacement_length = actual_length;
        }
    } else if (enabled) {
        replacement = configured_replacement(field, &replacement_capacity);
        if (replacement == NULL ||
            !bounded_length(replacement, replacement_capacity,
                            &replacement_length)) {
            status = HWID_STATUS_INVALID_DEVICE_STATE;
        } else if (field_is_serial(field) &&
                   replacement_length != actual_length) {
            status = HWID_STATUS_DATA_ERROR;
        }
    } else {
        replacement = actual_copy;
        replacement_length = actual_length;
    }

    if (status == (NTSTATUS)0) {
        if (replacement_length + 1u > outputCharacters) {
            status = HWID_STATUS_BUFFER_TOO_SMALL;
        } else {
            for (i = 0; i < replacement_length; ++i) {
                output[i] = replacement[i];
            }
            output[replacement_length] = L'\0';
        }
    }
    if (status != (NTSTATUS)0) output[0] = L'\0';
    ReleaseSRWLockShared(&g_state_lock);
    SecureZeroMemory(actual_copy, sizeof(actual_copy));
    SecureZeroMemory(key_copy, sizeof(key_copy));
    return status;
}
