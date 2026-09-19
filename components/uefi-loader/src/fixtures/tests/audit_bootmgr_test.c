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
    uint8_t boot_next[2];
    uint8_t boot_order[2];
    uint8_t boot0007[128];
    size_t boot0007_size;
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
        source = fixture->boot_next; size = sizeof(fixture->boot_next);
    } else if (wcscmp(name, L"BootOrder") == 0) {
        source = fixture->boot_order; size = sizeof(fixture->boot_order);
    } else if (wcscmp(name, L"Boot0007") == 0) {
        source = fixture->boot0007; size = fixture->boot0007_size;
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

static int test_replaced_bootmgr_names_backup_and_entry(void)
{
    fixture_t fixture;
    uint8_t expected[32];
    ib_audit_variable_ops_t variable_ops;
    ib_audit_bootmgr_ops_t ops;
    ib_audit_bootmgr_request_t request;
    ib_audit_bootmgr_report_t report;

    memset(&fixture, 0, sizeof(fixture));
    memset(expected, 0x11, sizeof(expected));
    memset(fixture.actual_hash, 0x22, sizeof(fixture.actual_hash));
    fixture.signed_by_microsoft = 0;
    fixture.backup_present = 1;
    fixture.boot_next[0] = fixture.boot_order[0] = 7u;
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

    memset(&fixture, 0, sizeof(fixture));
    memset(expected, 0x33, sizeof(expected));
    memcpy(fixture.actual_hash, expected, sizeof(expected));
    fixture.signed_by_microsoft = 1;
    fixture.boot_next[0] = fixture.boot_order[0] = 7u;
    fixture.boot0007[0] = 1u;
    fixture.boot0007[6] = 'W';
    fixture.boot0007[8] = 0u;
    fixture.boot0007_size = 10u;
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
    return 0;
}

int main(void)
{
    if (test_replaced_bootmgr_names_backup_and_entry() != 0 ||
        test_clean() != 0) return 1;
    puts("PASS: audit boot-manager core detects replacement and residue");
    return 0;
}
