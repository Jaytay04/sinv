/* The CRB driver can only attach if the ACPI table walker finds the TPM2 table,
 * so the walk is exercised against a synthetic RSDP, XSDT and TPM2 table. */
#include <stdio.h>
#include <string.h>

#include "ib_uacpi.h"
#include "ib_uefi.h"

/* Defined in ib_acpi_tables.c so the conversion can be checked without a TSC. */
extern uint64_t ib_acpi_ns_from_ticks(uint64_t ticks, uint64_t ticks_per_ns_q10);

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);        \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static unsigned char fixture_buffer[1024];
static unsigned char fixture_xsdt[64];
static ib_efi_configuration_table fixture_tables[2];
static ib_efi_system_table fixture_system;

static const ib_efi_guid_t acpi_20_guid = {
    0x8868e871, 0xe4f1, 0x11d3,
    { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 }
};

static const ib_efi_guid_t acpi_10_guid = {
    0xeb9d2d30, 0x2d88, 0x11d3,
    { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d }
};

ib_efi_system_table *ib_efi_get_system_table(void) {
    return &fixture_system;
}

static void put_u32(unsigned char *at, unsigned value) {
    at[0] = (unsigned char)(value & 0xffu);
    at[1] = (unsigned char)((value >> 8) & 0xffu);
    at[2] = (unsigned char)((value >> 16) & 0xffu);
    at[3] = (unsigned char)((value >> 24) & 0xffu);
}

static void put_u64(unsigned char *at, unsigned long long value) {
    put_u32(at, (unsigned)(value & 0xffffffffull));
    put_u32(at + 4, (unsigned)(value >> 32));
}

static void fix_checksum_at(unsigned char *table, unsigned length, unsigned at) {
    unsigned index;
    unsigned char sum = 0;

    table[at] = 0;
    for (index = 0; index < length; index++) {
        sum = (unsigned char)(sum + table[index]);
    }
    table[at] = (unsigned char)(0u - sum);
}

static void fix_checksum(unsigned char *table, unsigned length) {
    fix_checksum_at(table, length, 9);
}

static void build_tpm2(unsigned start_method, unsigned long long control_address) {
    unsigned char *tpm2 = fixture_buffer;
    unsigned char *rsdp = fixture_buffer + 256;
    unsigned char *xsdt = fixture_xsdt;
    const char *signature = "TPM2";

    memset(fixture_buffer, 0, sizeof(fixture_buffer));
    memset(xsdt, 0, sizeof(fixture_xsdt));

    memcpy(tpm2, signature, 4);
    put_u32(tpm2 + 4, 64);
    tpm2[8] = 3;
    put_u32(tpm2 + 36, 0);
    put_u64(tpm2 + 40, control_address);
    put_u32(tpm2 + 48, start_method);
    fix_checksum(tpm2, 64);

    memcpy(xsdt, "XSDT", 4);
    put_u32(xsdt + 4, 36u + 8u);
    xsdt[8] = 1;
    put_u64(xsdt + 36, (unsigned long long)(uintptr_t)tpm2);
    fix_checksum(xsdt, 36u + 8u);

    memcpy(rsdp, "RSD PTR ", 8);
    rsdp[15] = 2;
    put_u32(rsdp + 16, 0);
    put_u32(rsdp + 20, 36);
    put_u64(rsdp + 24, (unsigned long long)(uintptr_t)xsdt);
    fix_checksum(rsdp, 20);            /* byte 9 covers the first 20 bytes */
    fix_checksum_at(rsdp, 36, 32);     /* byte 32 covers the whole 36 */

    fixture_tables[0].vendor_guid = acpi_20_guid;
    fixture_tables[0].vendor_table = rsdp;
    memset(&fixture_system, 0, sizeof(fixture_system));
    fixture_system.number_of_table_entries = 1;
    fixture_system.configuration_table = fixture_tables;
}

static int test_walk_and_device(void) {
    ib_uacpi_table_t table;
    ib_uacpi_device_t device;
    ib_uacpi_hardware_id_t hid;
    ib_uacpi_resources_t resources;
    ib_uacpi_error_t error;
    int found;

    build_tpm2(7u, 0xfed40000ull);

    CHECK(ib_uacpi_init(&error) == IB_UACPI_STATUS_OK);
    CHECK(ib_uacpi_find_table("TPM2", &table, &found, &error) == IB_UACPI_STATUS_OK);
    CHECK(found == 1);
    CHECK(table.length == 64);
    CHECK(memcmp(table.bytes, "TPM2", 4) == 0);
    ib_uacpi_table_release(&table);
    CHECK(table.bytes == 0 && table.length == 0);

    CHECK(ib_uacpi_find_table("DMAR", &table, &found, &error) == IB_UACPI_STATUS_OK);
    CHECK(found == 0);

    CHECK(ib_uacpi_find_device("MSFT0101", &device, &found, &error) == IB_UACPI_STATUS_OK);
    CHECK(found == 1);
    CHECK(ib_uacpi_device_hardware_id(device, &hid, &error) == IB_UACPI_STATUS_OK);
    CHECK(strcmp(hid.text, "MSFT0101") == 0);

    CHECK(ib_uacpi_device_resources(device, &resources, &error) == IB_UACPI_STATUS_OK);
    CHECK(ib_uacpi_resources_memory_count(&resources) == 1);
    CHECK(ib_uacpi_memory_range_start(&resources.ranges[0]) == 0xfed40000ull - 0x40ull);
    CHECK(ib_uacpi_memory_range_covers(&resources.ranges[0], 0xfed40000ull, 0x30ull));
    return 0;
}

static int test_unsupported_start_method(void) {
    ib_uacpi_device_t device;
    ib_uacpi_error_t error;
    int found;

    /* ARM SMC: present in the table, but not something this port can drive. */
    build_tpm2(10u, 0xfed40000ull);
    CHECK(ib_uacpi_find_device("MSFT0101", &device, &found, &error) == IB_UACPI_STATUS_OK);
    CHECK(found == 0);
    return 0;
}

static int test_bad_checksum(void) {
    ib_uacpi_table_t table;
    ib_uacpi_error_t error;
    int found;

    build_tpm2(7u, 0xfed40000ull);
    fixture_buffer[4] ^= 0xffu;   /* break the TPM2 table checksum */
    CHECK(ib_uacpi_find_table("TPM2", &table, &found, &error) == IB_UACPI_STATUS_OK);
    CHECK(found == 0);
    return 0;
}

static int test_tick_conversion(void) {
    /* 3 GHz: 3072 Q10 ticks per nanosecond. One millisecond is 3,000,000
     * ticks, which has to read back as 1,000,000 ns - not 1,000. */
    uint64_t nanoseconds = ib_acpi_ns_from_ticks(3000000ull, 3072ull);
    /* Scaling the absolute counter before the division overflows at 2^54
     * ticks (about 69 days at 3 GHz) and returned zero there. */
    uint64_t large = ib_acpi_ns_from_ticks(1ull << 54, 3072ull);
    uint64_t large_expected = (1ull << 54) / 3ull;
    uint64_t later = ib_acpi_ns_from_ticks((1ull << 54) + 3000000ull, 3072ull);

    CHECK(nanoseconds > 990000ull);
    CHECK(nanoseconds < 1010000ull);
    CHECK(large != 0);
    CHECK(large > large_expected - large_expected / 1000ull);
    CHECK(large < large_expected + large_expected / 1000ull);
    CHECK(later > large);
    CHECK(later - large > 990000ull);
    CHECK(later - large < 1010000ull);
    return 0;
}

static int test_only_method_seven(void) {
    static const unsigned rejected[] = { 1u, 2u, 3u, 4u, 6u, 8u, 9u, 10u, 11u };
    ib_uacpi_device_t device;
    ib_uacpi_error_t error;
    int found;
    size_t index;

    for (index = 0; index < sizeof(rejected) / sizeof(rejected[0]); index++) {
        build_tpm2(rejected[index], 0xfed40000ull);
        CHECK(ib_uacpi_find_device("MSFT0101", &device, &found, &error) ==
              IB_UACPI_STATUS_OK);
        if (found != 0) {
            printf("FAIL: start method %u was advertised\n", rejected[index]);
            return 1;
        }
    }
    return 0;
}

static int test_acpi_20_wins_over_10(void) {
    static unsigned char old_root[64];
    static unsigned char old_rsdt[64];
    unsigned char *new_root = fixture_buffer + 256;
    ib_uacpi_table_t table;
    ib_uacpi_error_t error;
    int found;

    /* ACPI 1.0 root with an empty RSDT, listed first. */
    memset(old_root, 0, sizeof(old_root));
    memset(old_rsdt, 0, sizeof(old_rsdt));
    memcpy(old_rsdt, "RSDT", 4);
    put_u32(old_rsdt + 4, 36);
    old_rsdt[8] = 1;
    fix_checksum(old_rsdt, 36);
    memcpy(old_root, "RSD PTR ", 8);
    old_root[15] = 0;
    put_u32(old_root + 16, (unsigned)(uintptr_t)old_rsdt);
    fix_checksum(old_root, 20);

    build_tpm2(7u, 0xfed40000ull);

    fixture_tables[0].vendor_guid = acpi_10_guid;
    fixture_tables[0].vendor_table = old_root;
    fixture_tables[1].vendor_guid = acpi_20_guid;
    fixture_tables[1].vendor_table = new_root;
    memset(&fixture_system, 0, sizeof(fixture_system));
    fixture_system.number_of_table_entries = 2;
    fixture_system.configuration_table = fixture_tables;

    CHECK(ib_uacpi_find_table("TPM2", &table, &found, &error) == IB_UACPI_STATUS_OK);
    CHECK(found == 1);
    CHECK(memcmp(table.bytes, "TPM2", 4) == 0);
    return 0;
}

int main(void) {
    if (test_walk_and_device() != 0) {
        return 1;
    }
    if (test_unsupported_start_method() != 0) {
        return 1;
    }
    if (test_bad_checksum() != 0) {
        return 1;
    }
    if (test_tick_conversion() != 0) {
        return 1;
    }
    if (test_only_method_seven() != 0) {
        return 1;
    }
    if (test_acpi_20_wins_over_10() != 0) {
        return 1;
    }
    puts("acpi_tables_test: PASS");
    return 0;
}
