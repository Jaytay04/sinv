#include <stdio.h>
#include <string.h>

#include "vars.h"

static int g_failures;
#define CHECK(x) do { \
    if (!(x)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
        ++g_failures; \
    } \
} while (0)

typedef struct {
    uint8_t bytes[256];
    size_t size;
    int present;
} fake_variable_t;

typedef struct {
    fake_variable_t mok;
    fake_variable_t mokrt;
    fake_variable_t secure_boot;
    fake_variable_t pk;
    fake_variable_t kek;
    fake_variable_t db;
    fake_variable_t dbx;
    fake_variable_t boot_order;
    fake_variable_t boot_next;
    fake_variable_t boot0007;
    int fail_mokrt;
    int wrong_guid;
} fake_firmware_t;

static fake_variable_t *find_variable(fake_firmware_t *firmware,
                                      const wchar_t *name,
                                      const ib_audit_guid_t *guid)
{
    if (ib_audit_guid_equal(guid, &IB_AUDIT_SHIM_GUID)) {
        if (wcscmp(name, L"MokList") == 0) return &firmware->mok;
        if (wcscmp(name, L"MokListRT") == 0) return &firmware->mokrt;
        return NULL;
    }
    if (!ib_audit_guid_equal(guid, &IB_AUDIT_GLOBAL_GUID)) return NULL;
    if (wcscmp(name, L"SecureBoot") == 0) return &firmware->secure_boot;
    if (wcscmp(name, L"PK") == 0) return &firmware->pk;
    if (wcscmp(name, L"KEK") == 0) return &firmware->kek;
    if (wcscmp(name, L"db") == 0) return &firmware->db;
    if (wcscmp(name, L"dbx") == 0) return &firmware->dbx;
    if (wcscmp(name, L"BootOrder") == 0) return &firmware->boot_order;
    if (wcscmp(name, L"BootNext") == 0) return &firmware->boot_next;
    if (wcscmp(name, L"Boot0007") == 0) return &firmware->boot0007;
    return NULL;
}

static int fake_read(const wchar_t *name, const ib_audit_guid_t *guid,
                     uint8_t *data, size_t capacity, size_t *data_size,
                     uint32_t *attributes, void *context)
{
    fake_firmware_t *firmware = (fake_firmware_t *)context;
    fake_variable_t *variable;

    if (firmware->wrong_guid) return IB_AUDIT_READ_ERROR;
    if (firmware->fail_mokrt && wcscmp(name, L"MokListRT") == 0) {
        return IB_AUDIT_READ_ERROR;
    }
    variable = find_variable(firmware, name, guid);
    if (variable == NULL || !variable->present) return IB_AUDIT_READ_ABSENT;
    *attributes = 7u;
    if (data == NULL && capacity == 0u) {
        *data_size = variable->size;
        return variable->size == 0u ? IB_AUDIT_READ_PRESENT
                                    : IB_AUDIT_READ_BUFFER_TOO_SMALL;
    }
    if (capacity < variable->size) {
        *data_size = variable->size;
        return IB_AUDIT_READ_BUFFER_TOO_SMALL;
    }
    memcpy(data, variable->bytes, variable->size);
    *data_size = variable->size;
    return IB_AUDIT_READ_PRESENT;
}

static void set_variable(fake_variable_t *variable,
                         const uint8_t *bytes, size_t size)
{
    memset(variable, 0, sizeof(*variable));
    variable->present = 1;
    variable->size = size;
    memcpy(variable->bytes, bytes, size);
}

static size_t make_x509_list(uint8_t *output, const uint8_t *certificate,
                             size_t certificate_size)
{
    size_t signature_size = 16u + certificate_size;
    size_t list_size = 28u + signature_size;
    memset(output, 0, list_size);
    output[0] = 0xa1u; output[1] = 0x59u; output[2] = 0xc0u; output[3] = 0xa5u;
    output[4] = 0xe4u; output[5] = 0x94u; output[6] = 0xa7u; output[7] = 0x4au;
    output[8] = 0x87u; output[9] = 0xb5u; output[10] = 0xabu; output[11] = 0x15u;
    output[12] = 0x5cu; output[13] = 0x2bu; output[14] = 0x4au; output[15] = 0x83u;
    output[16] = (uint8_t)list_size;
    output[24] = (uint8_t)signature_size;
    memcpy(output + 44u, certificate, certificate_size);
    return list_size;
}

static void make_clean_firmware(fake_firmware_t *firmware,
                                const uint8_t *certificate, size_t cert_size)
{
    uint8_t list[256];
    uint8_t secure_boot = 1u;
    uint8_t boot_order[2] = { 7u, 0u };
    uint8_t boot_next[2] = { 7u, 0u };
    size_t list_size;

    memset(firmware, 0, sizeof(*firmware));
    list_size = make_x509_list(list, certificate, cert_size);
    set_variable(&firmware->mok, list, list_size);
    set_variable(&firmware->mokrt, list, list_size);
    set_variable(&firmware->secure_boot, &secure_boot, 1u);
    set_variable(&firmware->pk, NULL, 0u);
    set_variable(&firmware->kek, NULL, 0u);
    set_variable(&firmware->db, NULL, 0u);
    set_variable(&firmware->dbx, NULL, 0u);
    set_variable(&firmware->boot_order, boot_order, sizeof(boot_order));
    set_variable(&firmware->boot_next, boot_next, sizeof(boot_next));
    set_variable(&firmware->boot0007, (const uint8_t *)"entry", 5u);
}

static ib_audit_variable_ops_t ops_for(fake_firmware_t *firmware)
{
    ib_audit_variable_ops_t ops;
    ops.read_variable = fake_read;
    ops.context = firmware;
    return ops;
}

static void clean_database_is_clean(void)
{
    const uint8_t certificate[] = { 0x30u, 0x03u, 0x02u, 0x01u, 0x01u };
    uint8_t expected[SHA256_DIGEST_SIZE];
    fake_firmware_t firmware;
    ib_audit_variable_ops_t ops;
    ib_audit_report_t report;

    CHECK(sha256_hash(certificate, sizeof(certificate), expected) == SHA256_OK);
    make_clean_firmware(&firmware, certificate, sizeof(certificate));
    ops = ops_for(&firmware);
    CHECK(ib_audit_variables(&ops, &expected, 1u, &report) == IB_AUDIT_OK);
    CHECK(report.findings == 0u);
    CHECK(report.variables[IB_AUDIT_VARIABLE_MOK_LIST].state ==
          IB_AUDIT_VARIABLE_PRESENT);
    CHECK(report.variables[IB_AUDIT_VARIABLE_MOK_LIST].certificate_count == 1u);
    CHECK(report.variables[IB_AUDIT_VARIABLE_MOK_LIST].fingerprint_count == 1u);
    CHECK(memcmp(report.variables[IB_AUDIT_VARIABLE_MOK_LIST].fingerprints[0],
                 expected, SHA256_DIGEST_SIZE) == 0);
    CHECK(report.variables[IB_AUDIT_VARIABLE_MOK_LIST].unknown_certificate_count == 0u);
    CHECK(report.variables[IB_AUDIT_VARIABLE_BOOT_NEXT].state ==
          IB_AUDIT_VARIABLE_PRESENT);
}

static void rogue_mok_is_reported(void)
{
    const uint8_t trusted[] = { 0x30u, 0x03u, 0x02u, 0x01u, 0x01u };
    const uint8_t rogue[] = { 0x30u, 0x03u, 0x02u, 0x01u, 0x02u };
    uint8_t expected[SHA256_DIGEST_SIZE];
    fake_firmware_t firmware;
    ib_audit_variable_ops_t ops;
    ib_audit_report_t report;
    uint8_t list[256];
    size_t list_size;

    CHECK(sha256_hash(trusted, sizeof(trusted), expected) == SHA256_OK);
    make_clean_firmware(&firmware, trusted, sizeof(trusted));
    list_size = make_x509_list(list, rogue, sizeof(rogue));
    set_variable(&firmware.mok, list, list_size);
    ops = ops_for(&firmware);
    CHECK(ib_audit_variables(&ops, &expected, 1u, &report) == IB_AUDIT_OK);
    CHECK((report.findings & IB_AUDIT_FINDING_UNKNOWN_MOK) != 0u);
    CHECK(report.variables[IB_AUDIT_VARIABLE_MOK_LIST].unknown_certificate_count == 1u);
}

static void malformed_database_is_reported(void)
{
    const uint8_t certificate[] = { 0x30u, 0x03u, 0x02u, 0x01u, 0x01u };
    uint8_t expected[SHA256_DIGEST_SIZE];
    uint8_t list[256];
    fake_firmware_t firmware;
    ib_audit_variable_ops_t ops;
    ib_audit_report_t report;
    size_t list_size;

    CHECK(sha256_hash(certificate, sizeof(certificate), expected) == SHA256_OK);
    make_clean_firmware(&firmware, certificate, sizeof(certificate));
    list_size = make_x509_list(list, certificate, sizeof(certificate));
    list[16] = (uint8_t)(list_size + 1u);
    set_variable(&firmware.mok, list, list_size);
    ops = ops_for(&firmware);
    CHECK(ib_audit_variables(&ops, &expected, 1u, &report) == IB_AUDIT_OK);
    CHECK((report.findings & IB_AUDIT_FINDING_MALFORMED_VARIABLE) != 0u);
    CHECK(report.variables[IB_AUDIT_VARIABLE_MOK_LIST].state ==
          IB_AUDIT_VARIABLE_MALFORMED);
}

static void absent_and_read_failure_remain_distinct(void)
{
    const uint8_t certificate[] = { 0x30u, 0x03u, 0x02u, 0x01u, 0x01u };
    uint8_t expected[SHA256_DIGEST_SIZE];
    fake_firmware_t firmware;
    ib_audit_variable_ops_t ops;
    ib_audit_report_t report;

    CHECK(sha256_hash(certificate, sizeof(certificate), expected) == SHA256_OK);
    make_clean_firmware(&firmware, certificate, sizeof(certificate));
    firmware.mokrt.present = 0;
    firmware.fail_mokrt = 1;
    ops = ops_for(&firmware);
    CHECK(ib_audit_variables(&ops, &expected, 1u, &report) == IB_AUDIT_OK);
    CHECK(report.variables[IB_AUDIT_VARIABLE_MOK_LIST_RT].state ==
          IB_AUDIT_VARIABLE_READ_FAILED);
    CHECK((report.findings & IB_AUDIT_FINDING_VARIABLE_READ_FAILURE) != 0u);

    firmware.fail_mokrt = 0;
    CHECK(ib_audit_variables(&ops, &expected, 1u, &report) == IB_AUDIT_OK);
    CHECK(report.variables[IB_AUDIT_VARIABLE_MOK_LIST_RT].state ==
          IB_AUDIT_VARIABLE_ABSENT);
    CHECK((report.findings & IB_AUDIT_FINDING_VARIABLE_READ_FAILURE) == 0u);
}

int main(void)
{
    clean_database_is_clean();
    rogue_mok_is_reported();
    malformed_database_is_reported();
    absent_and_read_failure_remain_distinct();
    if (g_failures != 0) {
        fprintf(stderr, "%d audit variable tests failed\n", g_failures);
        return 1;
    }
    puts("audit variable tests passed");
    return 0;
}
