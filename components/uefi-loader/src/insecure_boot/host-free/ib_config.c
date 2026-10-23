#include "ib_config.h"
#include "ib_format.h"
#include "ib_util.h"

static const uint8_t ib_config_magic[8] = {
    'I', 'B', 'C', 'O', 'N', 'F', 'I', 'G'
};

static const uint32_t ib_config_version = 1u;

static uint16_t ib_config_read_u16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t ib_config_read_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

ib_config_t ib_config_new(uint16_t tcg_spec_offset) {
    ib_config_t config;
    config.tcg_spec_offset = tcg_spec_offset;
    return config;
}

size_t ib_config_tcg_spec_offset(const ib_config_t *config) {
    return (size_t)config->tcg_spec_offset;
}

void ib_config_to_bytes(const ib_config_t *config, uint8_t bytes[IB_CONFIG_LENGTH]) {
    ib_memset(bytes, 0, IB_CONFIG_LENGTH);
    ib_memcpy(bytes, ib_config_magic, sizeof(ib_config_magic));
    bytes[IB_CONFIG_VERSION_AT + 0] = (uint8_t)(ib_config_version & 0xffu);
    bytes[IB_CONFIG_VERSION_AT + 1] = (uint8_t)((ib_config_version >> 8) & 0xffu);
    bytes[IB_CONFIG_VERSION_AT + 2] = (uint8_t)((ib_config_version >> 16) & 0xffu);
    bytes[IB_CONFIG_VERSION_AT + 3] = (uint8_t)((ib_config_version >> 24) & 0xffu);
    bytes[IB_CONFIG_SETUP_OFFSET_AT + 0] = (uint8_t)(config->tcg_spec_offset & 0xffu);
    bytes[IB_CONFIG_SETUP_OFFSET_AT + 1] = (uint8_t)((config->tcg_spec_offset >> 8) & 0xffu);
}

ib_config_code ib_config_parse(const uint8_t *bytes, size_t length,
                               ib_config_t *out, ib_config_error_t *error) {
    uint32_t version;
    uint16_t tcg_spec_offset;

    ib_memset(error, 0, sizeof(*error));

    if (length != IB_CONFIG_LENGTH) {
        error->code = IB_CONFIG_ERR_SIZE_MISMATCH;
        error->actual = length;
        error->expected = IB_CONFIG_LENGTH;
        return error->code;
    }

    if (ib_memcmp(bytes, ib_config_magic, sizeof(ib_config_magic)) != 0) {
        error->code = IB_CONFIG_ERR_NOT_A_CONFIG;
        return error->code;
    }

    version = ib_config_read_u32(bytes + IB_CONFIG_VERSION_AT);
    if (version != ib_config_version) {
        error->code = IB_CONFIG_ERR_UNSUPPORTED_VERSION;
        error->found_version = version;
        error->expected_version = ib_config_version;
        return error->code;
    }

    tcg_spec_offset = ib_config_read_u16(bytes + IB_CONFIG_SETUP_OFFSET_AT);
    out->tcg_spec_offset = tcg_spec_offset;
    error->code = IB_CONFIG_OK;
    return IB_CONFIG_OK;
}

void ib_config_error_message(const ib_config_error_t *error, char *buffer, size_t capacity) {
    switch (error->code) {
    case IB_CONFIG_ERR_NOT_A_CONFIG:
        ib_snprintf(buffer, capacity, "the file does not begin with an ib-config signature");
        break;
    case IB_CONFIG_ERR_UNSUPPORTED_VERSION:
        ib_snprintf(buffer, capacity,
                    "configuration format version %u is not the supported version %u",
                    (unsigned)error->found_version, (unsigned)error->expected_version);
        break;
    case IB_CONFIG_ERR_SIZE_MISMATCH:
        ib_snprintf(buffer, capacity,
                    "the configuration is %u bytes, but the format is %u",
                    (unsigned)error->actual, (unsigned)error->expected);
        break;
    case IB_CONFIG_OK:
    default:
        ib_snprintf(buffer, capacity, "no configuration error");
        break;
    }
}
