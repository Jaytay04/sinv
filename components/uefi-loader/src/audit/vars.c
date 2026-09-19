/*
 * vars.c - Read-only Secure Boot variable audit.
 *
 * The parser deliberately does not depend on Windows or UEFI headers.  This
 * keeps the policy testable with a small fake callback and lets the Windows
 * frontend own the firmware API and its error translation.
 */
#include "vars.h"

#include <stdlib.h>
#include <string.h>

#define IB_AUDIT_MAX_VARIABLE_BYTES (16u * 1024u * 1024u)
#define EFI_SIGNATURE_LIST_FIXED_SIZE 28u
#define EFI_SIGNATURE_OWNER_SIZE 16u

const ib_audit_guid_t IB_AUDIT_SHIM_GUID = {
    0x605dab50u, 0xe046u, 0x430au,
    { 0xabu, 0xb6u, 0x3du, 0xd8u, 0x10u, 0xddu, 0x8bu, 0x23u }
};

const ib_audit_guid_t IB_AUDIT_GLOBAL_GUID = {
    0x8be4df61u, 0x93cau, 0x11d2u,
    { 0xaau, 0x0du, 0x00u, 0xe0u, 0x98u, 0x03u, 0x2bu, 0x8cu }
};

/* EFI_CERT_* GUIDs are represented in their in-memory EFI_GUID byte order. */
static const uint8_t g_x509_guid[16] = {
    0xa1u, 0x59u, 0xc0u, 0xa5u, 0xe4u, 0x94u, 0xa7u, 0x4au,
    0x87u, 0xb5u, 0xabu, 0x15u, 0x5cu, 0x2bu, 0x4au, 0x83u
};
static const uint8_t g_sha1_guid[16] = {
    0x12u, 0xa5u, 0x6cu, 0x82u, 0x10u, 0xcfu, 0xc9u, 0x4au,
    0xb1u, 0x87u, 0xbeu, 0x01u, 0x49u, 0x66u, 0x31u, 0xbdu
};
static const uint8_t g_sha256_guid[16] = {
    0x26u, 0x16u, 0xc4u, 0xc1u, 0x4cu, 0x50u, 0x92u, 0x40u,
    0xacu, 0xa9u, 0x41u, 0xf9u, 0x36u, 0x93u, 0x43u, 0x28u
};
static const uint8_t g_sha384_guid[16] = {
    0x07u, 0x53u, 0x3eu, 0xffu, 0xd0u, 0x9fu, 0xc9u, 0x48u,
    0x85u, 0xf1u, 0x8au, 0xd5u, 0x6cu, 0x70u, 0x1eu, 0x01u
};
static const uint8_t g_sha512_guid[16] = {
    0xaeu, 0x0fu, 0x3eu, 0x09u, 0xc4u, 0xa6u, 0x50u, 0x4fu,
    0x9fu, 0x1bu, 0xd4u, 0x1eu, 0x2du, 0xa7u, 0x4eu, 0x9fu
};

typedef struct {
    uint8_t *data;
    size_t size;
} audit_buffer_t;

static uint32_t read_le32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0]) |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static int bytes_equal(const uint8_t *left, const uint8_t *right, size_t size)
{
    size_t index;
    uint8_t different = 0u;

    if (left == NULL || right == NULL) {
        return 0;
    }
    for (index = 0u; index < size; ++index) {
        different = (uint8_t)(different | (uint8_t)(left[index] ^ right[index]));
    }
    return different == 0u;
}

int ib_audit_guid_equal(const ib_audit_guid_t *left,
                        const ib_audit_guid_t *right)
{
    if (left == NULL || right == NULL) {
        return 0;
    }
    return left->data1 == right->data1 &&
           left->data2 == right->data2 &&
           left->data3 == right->data3 &&
           bytes_equal(left->data4, right->data4, sizeof(left->data4));
}

static void buffer_release(audit_buffer_t *buffer)
{
    if (buffer != NULL) {
        free(buffer->data);
        buffer->data = NULL;
        buffer->size = 0u;
    }
}

/* Return 1 for present, 0 for absent, -1 for callback failure, -2 for OOM. */
static int read_buffer(const ib_audit_variable_ops_t *ops,
                       const wchar_t *name,
                       const ib_audit_guid_t *guid,
                       audit_buffer_t *buffer,
                       uint32_t *attributes)
{
    size_t required = 0u;
    uint32_t first_attributes = 0u;
    int status;

    if (ops == NULL || ops->read_variable == NULL || name == NULL ||
        guid == NULL || buffer == NULL || attributes == NULL) {
        return -1;
    }
    buffer->data = NULL;
    buffer->size = 0u;
    status = ops->read_variable(name, guid, NULL, 0u, &required,
                                &first_attributes, ops->context);
    if (status == IB_AUDIT_READ_ABSENT) {
        return 0;
    }
    if (status == IB_AUDIT_READ_PRESENT) {
        /* Only an empty variable can be returned without a buffer. */
        if (required != 0u) {
            return -1;
        }
        *attributes = first_attributes;
        return 1;
    }
    if (status != IB_AUDIT_READ_BUFFER_TOO_SMALL || required == 0u ||
        required > (size_t)IB_AUDIT_MAX_VARIABLE_BYTES) {
        return -1;
    }

    buffer->data = (uint8_t *)malloc(required);
    if (buffer->data == NULL) {
        return -2;
    }
    buffer->size = required;
    status = ops->read_variable(name, guid, buffer->data, required, &required,
                                attributes, ops->context);
    if (status != IB_AUDIT_READ_PRESENT || required != buffer->size) {
        buffer_release(buffer);
        return -1;
    }
    return 1;
}

static int signature_type(const uint8_t *type, size_t *payload_size,
                          int *is_x509)
{
    if (type == NULL || payload_size == NULL || is_x509 == NULL) {
        return 0;
    }
    if (bytes_equal(type, g_x509_guid, sizeof(g_x509_guid))) {
        *payload_size = 0u;
        *is_x509 = 1;
        return 1;
    }
    if (bytes_equal(type, g_sha1_guid, sizeof(g_sha1_guid))) {
        *payload_size = 20u;
    } else if (bytes_equal(type, g_sha256_guid, sizeof(g_sha256_guid))) {
        *payload_size = 32u;
    } else if (bytes_equal(type, g_sha384_guid, sizeof(g_sha384_guid))) {
        *payload_size = 48u;
    } else if (bytes_equal(type, g_sha512_guid, sizeof(g_sha512_guid))) {
        *payload_size = 64u;
    } else {
        return 0;
    }
    *is_x509 = 0;
    return 1;
}

static int fingerprint_is_expected(const uint8_t fingerprint[SHA256_DIGEST_SIZE],
                                   const uint8_t (*expected)[SHA256_DIGEST_SIZE],
                                   size_t expected_count)
{
    size_t index;

    for (index = 0u; index < expected_count; ++index) {
        if (bytes_equal(fingerprint, expected[index], SHA256_DIGEST_SIZE)) {
            return 1;
        }
    }
    return 0;
}

/*
 * Parse one EFI_SIGNATURE_LIST database.  Every list and every entry must
 * fit inside the supplied buffer.  X.509 entries are fingerprinted; other
 * standard EFI hash entry types are structurally checked but not trusted as
 * MOK certificates.
 */
static int parse_signature_database(const uint8_t *data, size_t size,
                                    int mok_database,
                                    const uint8_t (*expected)[SHA256_DIGEST_SIZE],
                                    size_t expected_count,
                                    ib_audit_variable_observation_t *observation,
                                    ib_audit_report_t *report)
{
    size_t offset = 0u;

    if (observation == NULL || report == NULL || (size != 0u && data == NULL)) {
        return 0;
    }
    while (offset < size) {
        size_t remaining = size - offset;
        size_t header_size;
        size_t signature_size;
        size_t list_size;
        size_t signature_bytes;
        size_t signature_count;
        size_t signature_index;
        int is_x509;
        size_t payload_size;
        const uint8_t *list = data + offset;

        if (remaining < EFI_SIGNATURE_LIST_FIXED_SIZE) {
            return 0;
        }
        list_size = (size_t)read_le32(list + 16u);
        header_size = (size_t)read_le32(list + 20u);
        signature_size = (size_t)read_le32(list + 24u);
        if (list_size < EFI_SIGNATURE_LIST_FIXED_SIZE || list_size > remaining ||
            header_size > list_size - EFI_SIGNATURE_LIST_FIXED_SIZE ||
            signature_size < EFI_SIGNATURE_OWNER_SIZE ||
            !signature_type(list, &payload_size, &is_x509)) {
            return 0;
        }
        signature_bytes = list_size - EFI_SIGNATURE_LIST_FIXED_SIZE - header_size;
        if (signature_bytes == 0u || signature_bytes % signature_size != 0u) {
            return 0;
        }
        if (payload_size != 0u && signature_size !=
            EFI_SIGNATURE_OWNER_SIZE + payload_size) {
            return 0;
        }
        if (mok_database && !is_x509) {
            return 0;
        }
        signature_count = signature_bytes / signature_size;
        if (signature_count > UINT32_MAX - observation->certificate_count) {
            return 0;
        }
        if (is_x509 && observation->certificate_count + signature_count >
            UINT32_MAX) {
            return 0;
        }
        for (signature_index = 0u; signature_index < signature_count;
             ++signature_index) {
            const uint8_t *signature = list + EFI_SIGNATURE_LIST_FIXED_SIZE +
                                       header_size + signature_index * signature_size;
            if (is_x509) {
                uint8_t fingerprint[SHA256_DIGEST_SIZE];
                size_t certificate_size = signature_size - EFI_SIGNATURE_OWNER_SIZE;
                if (sha256_hash(signature + EFI_SIGNATURE_OWNER_SIZE,
                                certificate_size, fingerprint) != SHA256_OK) {
                    return 0;
                }
                ++observation->certificate_count;
                if (observation->fingerprint_count >= IB_AUDIT_MAX_FINGERPRINTS) {
                    return 0;
                }
                memcpy(observation->fingerprints[observation->fingerprint_count],
                       fingerprint, SHA256_DIGEST_SIZE);
                ++observation->fingerprint_count;
                if (!fingerprint_is_expected(fingerprint, expected,
                                             expected_count)) {
                    ++observation->unknown_certificate_count;
                    if (mok_database) {
                        report->findings |= IB_AUDIT_FINDING_UNKNOWN_MOK;
                    }
                }
            }
        }
        offset += list_size;
    }
    return offset == size;
}

static void mark_read_failure(ib_audit_report_t *report,
                              ib_audit_variable_observation_t *observation)
{
    observation->state = IB_AUDIT_VARIABLE_READ_FAILED;
    report->findings |= IB_AUDIT_FINDING_VARIABLE_READ_FAILURE;
}

static int audit_database(const ib_audit_variable_ops_t *ops,
                          const wchar_t *name,
                          const ib_audit_guid_t *guid,
                          int mok_database,
                          const uint8_t (*expected)[SHA256_DIGEST_SIZE],
                          size_t expected_count,
                          ib_audit_variable_observation_t *observation,
                          ib_audit_report_t *report)
{
    audit_buffer_t buffer = { NULL, 0u };
    uint32_t attributes = 0u;
    int result = read_buffer(ops, name, guid, &buffer, &attributes);

    if (result == -2) {
        return -2;
    }
    if (result < 0) {
        mark_read_failure(report, observation);
        return 0;
    }
    if (result == 0) {
        observation->state = IB_AUDIT_VARIABLE_ABSENT;
        return 0;
    }
    observation->state = IB_AUDIT_VARIABLE_PRESENT;
    observation->size = buffer.size;
    observation->attributes = attributes;
    if (!parse_signature_database(buffer.data, buffer.size, mok_database,
                                  expected, expected_count, observation,
                                  report)) {
        observation->state = IB_AUDIT_VARIABLE_MALFORMED;
        report->findings |= IB_AUDIT_FINDING_MALFORMED_VARIABLE;
    }
    buffer_release(&buffer);
    return 0;
}

static void make_boot_name(uint16_t boot_number, wchar_t name[9])
{
    static const wchar_t digits[] = L"0123456789ABCDEF";
    name[0] = L'B';
    name[1] = L'o';
    name[2] = L'o';
    name[3] = L't';
    name[4] = digits[(boot_number >> 12) & 0x0fu];
    name[5] = digits[(boot_number >> 8) & 0x0fu];
    name[6] = digits[(boot_number >> 4) & 0x0fu];
    name[7] = digits[boot_number & 0x0fu];
    name[8] = L'\0';
}

static int audit_simple_variable(const ib_audit_variable_ops_t *ops,
                                 const wchar_t *name,
                                 const ib_audit_guid_t *guid,
                                 ib_audit_variable_observation_t *observation,
                                 ib_audit_report_t *report,
                                 audit_buffer_t *buffer)
{
    uint32_t attributes = 0u;
    int result = read_buffer(ops, name, guid, buffer, &attributes);

    if (result == -2) {
        return -2;
    }
    if (result < 0) {
        mark_read_failure(report, observation);
        return 0;
    }
    if (result == 0) {
        observation->state = IB_AUDIT_VARIABLE_ABSENT;
        return 0;
    }
    observation->state = IB_AUDIT_VARIABLE_PRESENT;
    observation->size = buffer->size;
    observation->attributes = attributes;
    return 0;
}

static int audit_secure_boot(const ib_audit_variable_ops_t *ops,
                             ib_audit_report_t *report)
{
    ib_audit_variable_observation_t *observation =
        &report->variables[IB_AUDIT_VARIABLE_SECURE_BOOT];
    audit_buffer_t buffer = { NULL, 0u };
    int result = audit_simple_variable(ops, L"SecureBoot", &IB_AUDIT_GLOBAL_GUID,
                                       observation, report, &buffer);
    if (result != 0) {
        return result;
    }
    if (observation->state == IB_AUDIT_VARIABLE_PRESENT) {
        if (buffer.size != 1u) {
            observation->state = IB_AUDIT_VARIABLE_MALFORMED;
            report->findings |= IB_AUDIT_FINDING_MALFORMED_VARIABLE;
        } else if (buffer.data[0] == 0u) {
            report->findings |= IB_AUDIT_FINDING_SECURE_BOOT_DISABLED;
        }
    }
    buffer_release(&buffer);
    return 0;
}

static int audit_boot_order(const ib_audit_variable_ops_t *ops,
                            ib_audit_report_t *report)
{
    ib_audit_variable_observation_t *observation =
        &report->variables[IB_AUDIT_VARIABLE_BOOT_ORDER];
    audit_buffer_t buffer = { NULL, 0u };
    int result = audit_simple_variable(ops, L"BootOrder", &IB_AUDIT_GLOBAL_GUID,
                                       observation, report, &buffer);
    if (result != 0) {
        return result;
    }
    if (observation->state == IB_AUDIT_VARIABLE_PRESENT &&
        (buffer.size & 1u) != 0u) {
        observation->state = IB_AUDIT_VARIABLE_MALFORMED;
        report->findings |= IB_AUDIT_FINDING_MALFORMED_VARIABLE;
    }
    buffer_release(&buffer);
    return 0;
}

static int audit_boot_next(const ib_audit_variable_ops_t *ops,
                           ib_audit_report_t *report)
{
    ib_audit_variable_observation_t *observation =
        &report->variables[IB_AUDIT_VARIABLE_BOOT_NEXT];
    audit_buffer_t buffer = { NULL, 0u };
    uint16_t boot_number;
    wchar_t boot_name[9];
    int result;

    result = audit_simple_variable(ops, L"BootNext", &IB_AUDIT_GLOBAL_GUID,
                                   observation, report, &buffer);
    if (result != 0 || observation->state != IB_AUDIT_VARIABLE_PRESENT) {
        buffer_release(&buffer);
        return result;
    }
    if (buffer.size != sizeof(boot_number)) {
        observation->state = IB_AUDIT_VARIABLE_MALFORMED;
        report->findings |= IB_AUDIT_FINDING_MALFORMED_VARIABLE;
        buffer_release(&buffer);
        return 0;
    }
    boot_number = (uint16_t)buffer.data[0] |
                  (uint16_t)((uint16_t)buffer.data[1] << 8);
    make_boot_name(boot_number, boot_name);
    buffer_release(&buffer);

    result = read_buffer(ops, boot_name, &IB_AUDIT_GLOBAL_GUID, &buffer,
                         &observation->attributes);
    if (result == -2) {
        return -2;
    }
    if (result < 0) {
        report->findings |= IB_AUDIT_FINDING_VARIABLE_READ_FAILURE;
    } else if (result == 0) {
        report->findings |= IB_AUDIT_FINDING_BOOTNEXT_MISSING_TARGET;
    }
    buffer_release(&buffer);
    return 0;
}

ib_audit_status_t ib_audit_variables(
    const ib_audit_variable_ops_t *ops,
    const uint8_t (*expected_mok_sha256)[SHA256_DIGEST_SIZE],
    size_t expected_count,
    ib_audit_report_t *report)
{
    static const wchar_t *const shim_names[] = { L"MokList", L"MokListRT" };
    static const wchar_t *const global_database_names[] = {
        L"PK", L"KEK", L"db", L"dbx"
    };
    size_t index;
    int result;

    if (ops == NULL || ops->read_variable == NULL || report == NULL ||
        (expected_count != 0u && expected_mok_sha256 == NULL)) {
        return IB_AUDIT_ERR_INVALID_ARGUMENT;
    }
    memset(report, 0, sizeof(*report));
    for (index = 0u; index < IB_AUDIT_VARIABLE_COUNT; ++index) {
        report->variables[index].state = IB_AUDIT_VARIABLE_UNREAD;
    }
    for (index = 0u; index < 2u; ++index) {
        result = audit_database(ops, shim_names[index], &IB_AUDIT_SHIM_GUID, 1,
                                expected_mok_sha256, expected_count,
                                &report->variables[index], report);
        if (result == -2) {
            return IB_AUDIT_ERR_MEMORY;
        }
    }
    for (index = 0u; index < 4u; ++index) {
        size_t observation_index = IB_AUDIT_VARIABLE_PK + index;
        result = audit_database(ops, global_database_names[index],
                                &IB_AUDIT_GLOBAL_GUID, 0,
                                expected_mok_sha256, expected_count,
                                &report->variables[observation_index], report);
        if (result == -2) {
            return IB_AUDIT_ERR_MEMORY;
        }
    }
    result = audit_secure_boot(ops, report);
    if (result == -2) {
        return IB_AUDIT_ERR_MEMORY;
    }
    result = audit_boot_order(ops, report);
    if (result == -2) {
        return IB_AUDIT_ERR_MEMORY;
    }
    result = audit_boot_next(ops, report);
    if (result == -2) {
        return IB_AUDIT_ERR_MEMORY;
    }
    return IB_AUDIT_OK;
}
