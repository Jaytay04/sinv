/* The CRB driver can only attach if the ACPI table walker finds the TPM2 table,
 * so the walk is exercised against a synthetic RSDP, XSDT and TPM2 table. */
#include <stdio.h>
#include <string.h>

#include "ib_uacpi.h"
#include "ib_uefi.h"

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);        \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static unsigned char fixture_buffer[1024];
static unsigned char fixture_xsdt[64];
static ib_efi_configuration_table fixture_tables[1];
static ib_efi_system_table fixture_system;

static const ib_efi_guid_t acpi_20_guid = {
    0x8868e871, 0xe4f1, 0x11d3,
    { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 }
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
    puts("acpi_tables_test: PASS");
    return 0;
}
