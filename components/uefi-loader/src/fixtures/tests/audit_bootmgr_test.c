#include <stdio.h>
#include <string.h>

#include "bootmgr.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; \
} } while (0)

typedef struct {
    uint8_t actual_hash[32];
    int signed_by_microsoft;
    int backup_present;
    int boot_next_present;
    int boot_order_present;
    int boot_next_read_error;
    int boot_order_read_error;
    uint8_t boot_next[2];
    size_t boot_next_size;
    uint8_t boot_order[IB_AUDIT_BOOTMGR_MAX_BOOT_ORDER * 2u + 2u];
    size_t boot_order_size;
    uint8_t boot0007[128];
    size_t boot0007_size;
    uint8_t boot0008[128];
    size_t boot0008_size;
} fixture_t;

static int hash_file(const wchar_t *path, uint8_t digest[32], void *context)
{
    fixture_t *fixture = (fixture_t *)context;
    CHECK(wcsstr(path, L"bootmgfw.efi") != NULL);
    memcpy(digest, fixture->actual_hash, 32u);
    return 1;
}

static int verify_file(const wchar_t *path, void *context)
{
    fixture_t *fixture = (fixture_t *)context;
    (void)path;
    return fixture->signed_by_microsoft;
}

static int exists(const wchar_t *path, void *context)
{
    fixture_t *fixture = (fixture_t *)context;
    return wcsstr(path, L".bak") != NULL ? fixture->backup_present : 0;
}

static int read_variable(const wchar_t *name, const ib_audit_guid_t *guid,
                         uint8_t *data, size_t capacity, size_t *data_size,
                         uint32_t *attributes, void *context)
{
    fixture_t *fixture = (fixture_t *)context;
    const uint8_t *source = NULL;
    size_t size = 0u;

    if (!ib_audit_guid_equal(guid, &IB_AUDIT_GLOBAL_GUID)) return -1;
    if (wcscmp(name, L"BootNext") == 0) {
        if (fixture->boot_next_read_error) return IB_AUDIT_READ_ERROR;
        if (!fixture->boot_next_present) return IB_AUDIT_READ_ABSENT;
        source = fixture->boot_next; size = fixture->boot_next_size;
    } else if (wcscmp(name, L"BootOrder") == 0) {
        if (fixture->boot_order_read_error) return IB_AUDIT_READ_ERROR;
        if (!fixture->boot_order_present) return IB_AUDIT_READ_ABSENT;
        source = fixture->boot_order; size = fixture->boot_order_size;
    } else if (wcscmp(name, L"Boot0007") == 0) {
        source = fixture->boot0007; size = fixture->boot0007_size;
    } else if (wcscmp(name, L"Boot0008") == 0) {
        source = fixture->boot0008; size = fixture->boot0008_size;
    } else {
        return IB_AUDIT_READ_ABSENT;
    }
    *attributes = 7u;
    *data_size = size;
    if (data == NULL) return IB_AUDIT_READ_BUFFER_TOO_SMALL;
    if (capacity < size) return IB_AUDIT_READ_BUFFER_TOO_SMALL;
    memcpy(data, source, size);
    return IB_AUDIT_READ_PRESENT;
}

static void make_staged_entry(fixture_t *fixture)
{
    static const wchar_t description[] = L"Insecure Boot Toolkit";
    size_t bytes = sizeof(description);

    memset(fixture->boot0007, 0, sizeof(fixture->boot0007));
    fixture->boot0007[0] = 1u;
    memcpy(fixture->boot0007 + 6u, description, bytes);
    fixture->boot0007_size = 6u + bytes;
}

static void make_clean_entry(uint8_t bytes[128], size_t *size)
{
    memset(bytes, 0, 128u);
    bytes[0] = 1u;
    bytes[6] = 'W';
    bytes[8] = 0u;
    *size = 10u;
}

static void set_boot_order(fixture_t *fixture, const uint16_t *numbers,
                           size_t count)
{
    size_t index;

    fixture->boot_order_present = 1;
    fixture->boot_order_size = count * 2u;
    for (index = 0u; index < count; ++index) {
        fixture->boot_order[index * 2u] = (uint8_t)numbers[index];
        fixture->boot_order[index * 2u + 1u] =
            (uint8_t)(numbers[index] >> 8);
    }
}

static int test_replaced_bootmgr_names_backup_and_entry(void)
{
    fixture_t fixture;
    uint8_t expected[32];
    ib_audit_variable_ops_t variable_ops;
    ib_audit_bootmgr_ops_t ops;
    ib_audit_bootmgr_request_t request;
    ib_audit_bootmgr_report_t report;
    uint16_t order[] = { 7u };

    memset(&fixture, 0, sizeof(fixture));
    memset(expected, 0x11, sizeof(expected));
    memset(fixture.actual_hash, 0x22, sizeof(fixture.actual_hash));
    fixture.signed_by_microsoft = 0;
    fixture.backup_present = 1;
    fixture.boot_next_present = 1;
    fixture.boot_next_size = 2u;
    fixture.boot_next[0] = 7u;
    set_boot_order(&fixture, order, 1u);
    make_staged_entry(&fixture);
    variable_ops.read_variable = read_variable;
    variable_ops.context = &fixture;
    ops.hash_file = hash_file;
    ops.verify_microsoft_authenticode = verify_file;
    ops.path_exists = exists;
    ops.variables = &variable_ops;
    ops.context = &fixture;
    request.esp_root = L"C:\\fixture";
    request.expected_bootmgfw_sha256 = expected;
    request.ops = &ops;
    CHECK(ib_audit_bootmgr_integrity(&request, &report) ==
          IB_AUDIT_BOOTMGR_OK);
    CHECK((report.findings & IB_AUDIT_BOOTMGR_HASH_MISMATCH) != 0u);
    CHECK((report.findings & IB_AUDIT_BOOTMGR_AUTHENTICODE_INVALID) != 0u);
    CHECK((report.findings & IB_AUDIT_BOOTMGR_BACKUP_PRESENT) != 0u);
    CHECK((report.findings & IB_AUDIT_BOOTMGR_STAGED_BOOTNEXT) != 0u);
    CHECK((report.findings & IB_AUDIT_BOOTMGR_STAGED_BOOTORDER) != 0u);
    CHECK(report.residue_mask == IB_AUDIT_BOOTMGR_RESIDUE_BAK);
    CHECK(report.staged_entry_count == 1u && report.staged_entries[0] == 7u);
    CHECK(report.boot_next_present == 1u && report.boot_next == 7u);
    CHECK(report.boot_order_count == 1u && report.boot_order[0] == 7u);
    return 0;
}

static int test_clean(void)
{
    fixture_t fixture;
    uint8_t expected[32];
    ib_audit_variable_ops_t variable_ops;
    ib_audit_bootmgr_ops_t ops;
    ib_audit_bootmgr_request_t request;
    ib_audit_bootmgr_report_t report;
    uint16_t order[] = { 7u };

    memset(&fixture, 0, sizeof(fixture));
    memset(expected, 0x33, sizeof(expected));
    memcpy(fixture.actual_hash, expected, sizeof(expected));
    fixture.signed_by_microsoft = 1;
    fixture.boot_next_present = 1;
    fixture.boot_next_size = 2u;
    fixture.boot_next[0] = 7u;
    set_boot_order(&fixture, order, 1u);
    make_clean_entry(fixture.boot0007, &fixture.boot0007_size);
    make_clean_entry(fixture.boot0008, &fixture.boot0008_size);
    variable_ops.read_variable = read_variable;
    variable_ops.context = &fixture;
    ops.hash_file = hash_file;
    ops.verify_microsoft_authenticode = verify_file;
    ops.path_exists = exists;
    ops.variables = &variable_ops;
    ops.context = &fixture;
    request.esp_root = L"C:\\fixture";
    request.expected_bootmgfw_sha256 = expected;
    request.ops = &ops;
    CHECK(ib_audit_bootmgr_integrity(&request, &report) ==
          IB_AUDIT_BOOTMGR_OK);
    CHECK(report.findings == 0u);
    CHECK(report.boot_next_present == 1u && report.boot_next == 7u);
    CHECK(report.boot_order_count == 1u && report.boot_order[0] == 7u);
    return 0;
}

static void configure_clean_request(fixture_t *fixture, uint8_t expected[32],
                                    ib_audit_variable_ops_t *variable_ops,
                                    ib_audit_bootmgr_ops_t *ops,
                                    ib_audit_bootmgr_request_t *request)
{
    memset(fixture, 0, sizeof(*fixture));
    memset(expected, 0x44, 32u);
    memcpy(fixture->actual_hash, expected, 32u);
    fixture->signed_by_microsoft = 1;
    make_clean_entry(fixture->boot0007, &fixture->boot0007_size);
    make_clean_entry(fixture->boot0008, &fixture->boot0008_size);
    variable_ops->read_variable = read_variable;
    variable_ops->context = fixture;
    ops->hash_file = hash_file;
    ops->verify_microsoft_authenticode = verify_file;
    ops->path_exists = exists;
    ops->variables = variable_ops;
    ops->context = fixture;
    request->esp_root = L"C:\\fixture";
    request->expected_bootmgfw_sha256 = expected;
    request->ops = ops;
}

static int test_raw_order_and_absent_boot_next(void)
{
    fixture_t fixture;
    uint8_t expected[32];
    uint16_t first_order[] = { 8u, 7u };
    uint16_t changed_order[] = { 7u, 8u };
    ib_audit_variable_ops_t variable_ops;
    ib_audit_bootmgr_ops_t ops;
    ib_audit_bootmgr_request_t request;
    ib_audit_bootmgr_report_t report;

    configure_clean_request(&fixture, expected, &variable_ops, &ops, &request);
    fixture.boot_next_present = 0;
    set_boot_order(&fixture, first_order, 2u);
    CHECK(ib_audit_bootmgr_integrity(&request, &report) ==
          IB_AUDIT_BOOTMGR_OK);
    CHECK(report.findings == 0u);
    CHECK(report.boot_next_present == 0u);
    CHECK(report.boot_order_count == 2u);
    CHECK(report.boot_order[0] == 8u && report.boot_order[1] == 7u);

    set_boot_order(&fixture, changed_order, 2u);
    CHECK(ib_audit_bootmgr_integrity(&request, &report) ==
          IB_AUDIT_BOOTMGR_OK);
    CHECK(report.findings == 0u);
    CHECK(report.boot_order_count == 2u);
    CHECK(report.boot_order[0] == 7u && report.boot_order[1] == 8u);
    return 0;
}

static int test_malformed_and_oversized_order(void)
{
    fixture_t fixture;
    uint8_t expected[32];
    ib_audit_variable_ops_t variable_ops;
    ib_audit_bootmgr_ops_t ops;
    ib_audit_bootmgr_request_t request;
    ib_audit_bootmgr_report_t report;
    size_t index;

    configure_clean_request(&fixture, expected, &variable_ops, &ops, &request);
    fixture.boot_next_present = 0;
    fixture.boot_order_present = 1;
    fixture.boot_order_size = 3u;
    fixture.boot_order[0] = 7u;
    fixture.boot_order[1] = 0u;
    fixture.boot_order[2] = 7u;
    CHECK(ib_audit_bootmgr_integrity(&request, &report) ==
          IB_AUDIT_BOOTMGR_OK);
    CHECK((report.findings & IB_AUDIT_BOOTMGR_BOOT_VARIABLE_MALFORMED) != 0u);
    CHECK(report.boot_order_count == 0u);

    fixture.boot_order_size = (IB_AUDIT_BOOTMGR_MAX_BOOT_ORDER + 1u) * 2u;
    for (index = 0u; index < fixture.boot_order_size; index += 2u) {
        fixture.boot_order[index] = 7u;
        fixture.boot_order[index + 1u] = 0u;
    }
    CHECK(ib_audit_bootmgr_integrity(&request, &report) ==
          IB_AUDIT_BOOTMGR_OK);
    CHECK((report.findings & IB_AUDIT_BOOTMGR_BOOT_VARIABLE_MALFORMED) != 0u);
    CHECK(report.boot_order_count == 0u);
    return 0;
}

static int test_order_read_error_is_retained_as_finding(void)
{
    fixture_t fixture;
    uint8_t expected[32];
    ib_audit_variable_ops_t variable_ops;
    ib_audit_bootmgr_ops_t ops;
    ib_audit_bootmgr_request_t request;
    ib_audit_bootmgr_report_t report;

    configure_clean_request(&fixture, expected, &variable_ops, &ops, &request);
    fixture.boot_next_present = 0;
    fixture.boot_order_read_error = 1;
    CHECK(ib_audit_bootmgr_integrity(&request, &report) ==
          IB_AUDIT_BOOTMGR_OK);
    CHECK((report.findings & IB_AUDIT_BOOTMGR_READ_FAILURE) != 0u);
    CHECK(report.boot_order_count == 0u);
    return 0;
}

int main(void)
{
    if (test_replaced_bootmgr_names_backup_and_entry() != 0 ||
        test_clean() != 0 || test_raw_order_and_absent_boot_next() != 0 ||
        test_malformed_and_oversized_order() != 0 ||
        test_order_read_error_is_retained_as_finding() != 0) return 1;
    puts("PASS: audit boot-manager core detects replacement and residue");
    return 0;
}
