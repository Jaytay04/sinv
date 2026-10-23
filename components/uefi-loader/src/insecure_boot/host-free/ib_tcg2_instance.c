#include "ib_tcg2.h"

#include "ib_util.h"

/* Offset of an EFI_TCG2_EVENT's own length. */
#define IB_TCG2_EVENT_SIZE_AT 0u
#define IB_TCG2_EVENT_HEADER_LEN_AT (IB_TCG2_EVENT_SIZE_AT + 4u)
#define IB_TCG2_EVENT_HEADER_VERSION_AT (IB_TCG2_EVENT_HEADER_LEN_AT + 4u)
#define IB_TCG2_EVENT_PCR_INDEX_AT (IB_TCG2_EVENT_HEADER_VERSION_AT + 2u)
#define IB_TCG2_EVENT_TYPE_AT (IB_TCG2_EVENT_PCR_INDEX_AT + 4u)
#define IB_TCG2_EVENT_DATA_AT (IB_TCG2_EVENT_TYPE_AT + 4u)

#define IB_TCG2_CAPABILITY_LEN 30u
#define IB_TCG2_CAPABILITY_LEN_1_0 (IB_TCG2_CAPABILITY_LEN - 8u)

static uint16_t ib_tcg2_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t ib_tcg2_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int ib_tcg2_read_le32(const uint8_t *bytes, size_t length, size_t at,
                             uint32_t *out) {
    if (at > length || 4 > length - at) {
        return 0;
    }
    *out = ib_tcg2_le32(bytes + at);
    return 1;
}

static int ib_tcg2_read_le16(const uint8_t *bytes, size_t length, size_t at,
                             uint16_t *out) {
    if (at > length || 2 > length - at) {
        return 0;
    }
    *out = ib_tcg2_le16(bytes + at);
    return 1;
}

static ib_tcg2_code ib_tcg2_from_tpm(ib_tcg2_error_t *error,
                                     const ib_tpm_crb_error_t *tpm) {
    if (error != 0) {
        error->code = IB_TCG2_ERR_TPM;
        error->has_tpm = 1;
        error->tpm = *tpm;
    }
    return IB_TCG2_ERR_TPM;
}

static ib_tcg2_code ib_tcg2_from_reply(ib_tcg2_error_t *error,
                                       const ib_tpm2_error_t *reply) {
    if (error != 0) {
        error->code = IB_TCG2_ERR_REPLY;
        error->has_reply = 1;
        error->reply = *reply;
    }
    return IB_TCG2_ERR_REPLY;
}

static ib_tcg2_code ib_tcg2_command_too_long(ib_tcg2_error_t *error,
                                             const char *command) {
    if (error != 0) {
        error->code = IB_TCG2_ERR_COMMAND_TOO_LONG;
        error->command = command;
    }
    return IB_TCG2_ERR_COMMAND_TOO_LONG;
}

static uint32_t ib_tcg2_bit(ib_tcglog_algorithm_t algorithm) {
    switch (algorithm) {
    case IB_TCGLOG_ALG_SHA1:   return IB_EFI_TCG2_BOOT_HASH_ALG_SHA1;
    case IB_TCGLOG_ALG_SHA256: return IB_EFI_TCG2_BOOT_HASH_ALG_SHA256;
    case IB_TCGLOG_ALG_SHA384: return IB_EFI_TCG2_BOOT_HASH_ALG_SHA384;
    case IB_TCGLOG_ALG_SHA512: return IB_EFI_TCG2_BOOT_HASH_ALG_SHA512;
    case IB_TCGLOG_ALG_SM3_256: return IB_EFI_TCG2_BOOT_HASH_ALG_SM3_256;
    default: return 0;
    }
}

static uint32_t ib_tcg2_bitmap(const ib_tcglog_algorithm_t *banks,
                               size_t bank_count) {
    uint32_t bits = 0;

    for (size_t index = 0; index < bank_count; index++) {
        bits |= ib_tcg2_bit(banks[index]);
    }
    return bits;
}

static uint32_t ib_tcg2_supported_bitmap(void) {
    return IB_EFI_TCG2_BOOT_HASH_ALG_SHA1 |
           IB_EFI_TCG2_BOOT_HASH_ALG_SHA256 |
           IB_EFI_TCG2_BOOT_HASH_ALG_SHA384 |
           IB_EFI_TCG2_BOOT_HASH_ALG_SHA512;
}

static uint8_t ib_tcg2_byte(size_t value) {
    return value <= 0xffu ? (uint8_t)value : 0xffu;
}

static uint16_t ib_tcg2_half(uint32_t value) {
    return value <= 0xffffu ? (uint16_t)value : 0xffffu;
}

static uint32_t ib_tcg2_word(size_t value) {
    return value <= 0xffffffffu ? (uint32_t)value : 0xffffffffu;
}

static ib_tcg2_code ib_tcg2_allocated(ib_tpm_crb_t *tpm,
                                      ib_tcglog_algorithm_t *banks,
                                      size_t capacity, size_t *out_count,
                                      ib_tcg2_error_t *error) {
    uint8_t command[IB_TPM2_CAPABILITY_COMMAND_CAPACITY];
    uint8_t reply[IB_TPM2_CAPABILITY_REPLY_CAPACITY];
    size_t length;
    size_t reply_length = 0;
    ib_tpm_crb_error_t tpm_error;
    ib_tpm2_error_t reply_error;
    ib_tpm2_code code;
    ib_tpm_crb_code crb;

    if (!ib_tpm2_capability_pcrs(command, sizeof(command), &length)) {
        return ib_tcg2_command_too_long(error, "TPM2_GetCapability");
    }

    crb = ib_tpm_crb_transmit(tpm, command, length, reply, sizeof(reply),
                              &reply_length, &tpm_error);
    if (crb != IB_TPM_CRB_OK) {
        return ib_tcg2_from_tpm(error, &tpm_error);
    }

    code = ib_tpm2_capability_banks(reply, reply_length, banks, capacity,
                                    out_count, &reply_error);
    if (code != IB_TPM2_OK) {
        return ib_tcg2_from_reply(error, &reply_error);
    }

    return IB_TCG2_OK;
}

static ib_tcg2_code ib_tcg2_property(ib_tpm_crb_t *tpm, uint32_t property,
                                     uint32_t *out, ib_tcg2_error_t *error) {
    uint8_t command[IB_TPM2_CAPABILITY_COMMAND_CAPACITY];
    uint8_t reply[IB_TPM2_CAPABILITY_REPLY_CAPACITY];
    size_t length;
    size_t reply_length = 0;
    ib_tpm_crb_error_t tpm_error;
    ib_tpm2_error_t reply_error;
    ib_tpm2_code code;
    ib_tpm_crb_code crb;

    if (!ib_tpm2_capability_property(command, sizeof(command), property,
                                     &length)) {
        return ib_tcg2_command_too_long(error, "TPM2_GetCapability");
    }

    crb = ib_tpm_crb_transmit(tpm, command, length, reply, sizeof(reply),
                              &reply_length, &tpm_error);
    if (crb != IB_TPM_CRB_OK) {
        return ib_tcg2_from_tpm(error, &tpm_error);
    }

    code = ib_tpm2_capability_value(reply, reply_length, out, &reply_error);
    if (code != IB_TPM2_OK) {
        return ib_tcg2_from_reply(error, &reply_error);
    }

    return IB_TCG2_OK;
}

ib_tcg2_code ib_tcg2_instance_new(ib_tcg2_instance_t *out, ib_tpm_crb_t tpm,
                                  const ib_tcglog_t *dump, size_t headroom,
                                  size_t final_capacity,
                                  ib_tcg2_error_t *error) {
    ib_tcg2_state_t *state;
    ib_tcg2_hasher_t hasher;
    uint32_t command_size;
    uint32_t response_size;
    uint32_t manufacturer;
    ib_tcg2_code code;

    ib_memset(out, 0, sizeof(*out));
    out->state.tpm = tpm;
    state = &out->state;

    code = ib_tcg2_allocated(&state->tpm, state->banks,
                             IB_TPM2_BANK_COUNT_MAX, &state->bank_count, error);
    if (code != IB_TCG2_OK) {
        return code;
    }

    if (state->bank_count == 0) {
        if (error != 0) {
            error->code = IB_TCG2_ERR_NO_BANKS;
        }
        return IB_TCG2_ERR_NO_BANKS;
    }

    code = ib_tcg2_hasher_new(&hasher, state->banks, state->bank_count, error);
    if (code != IB_TCG2_OK) {
        return code;
    }

    code = ib_tcg2_property(&state->tpm, IB_TPM2_PROPERTY_MAX_COMMAND_SIZE,
                            &command_size, error);
    if (code != IB_TCG2_OK) {
        return code;
    }
    code = ib_tcg2_property(&state->tpm, IB_TPM2_PROPERTY_MAX_RESPONSE_SIZE,
                            &response_size, error);
    if (code != IB_TCG2_OK) {
        return code;
    }
    code = ib_tcg2_property(&state->tpm, IB_TPM2_PROPERTY_MANUFACTURER,
                            &manufacturer, error);
    if (code != IB_TCG2_OK) {
        return code;
    }

    state->capability.size = ib_tcg2_byte(IB_TCG2_CAPABILITY_LEN);
    state->capability.structure_version.major = IB_TCG2_VERSION_MAJOR;
    state->capability.structure_version.minor = IB_TCG2_VERSION_MINOR;
    state->capability.protocol_version.major = IB_TCG2_VERSION_MAJOR;
    state->capability.protocol_version.minor = IB_TCG2_VERSION_MINOR;
    state->capability.hash_algorithm_bitmap = ib_tcg2_supported_bitmap();
    state->capability.supported_event_logs = IbTcg2EventLogFormatTcg2;
    state->capability.tpm_present_flag = IB_TRUE;
    state->capability.max_command_size = ib_tcg2_half(command_size);
    state->capability.max_response_size = ib_tcg2_half(response_size);
    state->capability.manufacturer_id = manufacturer;
    state->capability.number_of_pcr_banks = ib_tcg2_word(state->bank_count);
    state->capability.active_pcr_banks =
        ib_tcg2_bitmap(state->banks, state->bank_count);

    code = ib_tcg2_log_init(&state->log, dump, state->banks,
                            state->bank_count, headroom, error);
    if (code != IB_TCG2_OK) {
        return code;
    }

    code = ib_tcg2_final_events_install(&state->final_events, final_capacity,
                                        error);
    if (code != IB_TCG2_OK) {
        ib_tcg2_log_release(&state->log);
        return code;
    }

    state->log_taken = 0;
    out->protocol = ib_tcg2_protocol_table();
    return IB_TCG2_OK;
}

void *ib_tcg2_instance_interface(ib_tcg2_instance_t *instance) {
    return &instance->protocol;
}

const ib_tcglog_algorithm_t *ib_tcg2_instance_banks(
    const ib_tcg2_instance_t *instance, size_t *out_count) {
    if (out_count != 0) {
        *out_count = instance->state.bank_count;
    }
    return instance->state.banks;
}

const ib_efi_tcg2_boot_service_capability *ib_tcg2_instance_capability(
    const ib_tcg2_instance_t *instance) {
    return &instance->state.capability;
}

void ib_tcg2_instance_log(const ib_tcg2_instance_t *instance,
                          uint64_t *address, size_t *length, size_t *spare) {
    if (address != 0) {
        *address = ib_tcg2_log_address(&instance->state.log);
    }
    if (length != 0) {
        *length = ib_tcg2_log_length(&instance->state.log);
    }
    if (spare != 0) {
        *spare = ib_tcg2_log_spare(&instance->state.log);
    }
}

void ib_tcg2_instance_final_events(const ib_tcg2_instance_t *instance,
                                   uint64_t *address, uint64_t *events,
                                   size_t *spare) {
    if (address != 0) {
        *address = ib_tcg2_final_events_address(&instance->state.final_events);
    }
    if (events != 0) {
        *events = ib_tcg2_final_events_events(&instance->state.final_events);
    }
    if (spare != 0) {
        *spare = ib_tcg2_final_events_spare(&instance->state.final_events);
    }
}

ib_tcg2_code ib_tcg2_instance_release(ib_tcg2_instance_t *instance,
                                      ib_tpm_crb_t *out_tpm,
                                      ib_tcg2_error_t *error) {
    ib_tcg2_code code;

    code = ib_tcg2_final_events_uninstall(&instance->state.final_events, error);
    if (code != IB_TCG2_OK) {
        return code;
    }

    *out_tpm = instance->state.tpm;

    ib_tcg2_log_release(&instance->state.log);
    return IB_TCG2_OK;
}

/* ------------------------------------------------------------------------- */
/* Protocol entry points                                                     */
/* ------------------------------------------------------------------------- */
static ib_tcg2_instance_t *ib_tcg2_instance_of(ib_efi_tcg2_protocol *protocol) {
    if (protocol == 0) {
        return 0;
    }
    return (ib_tcg2_instance_t *)(void *)protocol;
}

static ib_efi_status ib_tcg2_get_capability(
    ib_efi_tcg2_protocol *self,
    ib_efi_tcg2_boot_service_capability *protocol_capability) {
    ib_tcg2_instance_t *instance = ib_tcg2_instance_of(self);
    ib_efi_tcg2_boot_service_capability value;
    size_t room;
    size_t length;

    if (instance == 0 || protocol_capability == 0) {
        return IB_EFI_INVALID_PARAMETER;
    }

    room = ((const uint8_t *)protocol_capability)[0];

    value = instance->state.capability;
    if (room >= IB_TCG2_CAPABILITY_LEN) {
        length = IB_TCG2_CAPABILITY_LEN;
    } else if (room >= IB_TCG2_CAPABILITY_LEN_1_0) {
        value.structure_version.major = IB_TCG2_VERSION_MAJOR;
        value.structure_version.minor = IB_TCG2_VERSION_1_0_MINOR;
        value.protocol_version.major = IB_TCG2_VERSION_MAJOR;
        value.protocol_version.minor = IB_TCG2_VERSION_1_0_MINOR;
        length = IB_TCG2_CAPABILITY_LEN_1_0;
    } else {
        ((uint8_t *)protocol_capability)[0] =
            ib_tcg2_byte(IB_TCG2_CAPABILITY_LEN);
        return IB_EFI_BUFFER_TOO_SMALL;
    }

    value.size = ib_tcg2_byte(length);
    ib_memcpy(protocol_capability, &value, length);
    return IB_EFI_SUCCESS;
}

static ib_efi_status ib_tcg2_get_event_log(
    ib_efi_tcg2_protocol *self,
    ib_efi_tcg2_event_log_format event_log_format,
    ib_efi_physical_address *event_log_location,
    ib_efi_physical_address *event_log_last_entry,
    ib_boolean *event_log_truncated) {
    ib_tcg2_instance_t *instance = ib_tcg2_instance_of(self);

    if (instance == 0) {
        return IB_EFI_INVALID_PARAMETER;
    }
    if (event_log_format != IbTcg2EventLogFormatTcg2 ||
        event_log_location == 0) {
        return IB_EFI_INVALID_PARAMETER;
    }

    *event_log_location = ib_tcg2_log_address(&instance->state.log);

    if (event_log_last_entry != 0) {
        *event_log_last_entry = ib_tcg2_log_last_entry(&instance->state.log);
    }
    if (event_log_truncated != 0) {
        *event_log_truncated =
            ib_tcg2_log_truncated(&instance->state.log) ? IB_TRUE : IB_FALSE;
    }

    instance->state.log_taken = 1;
    return IB_EFI_SUCCESS;
}

static ib_efi_status ib_tcg2_extend(ib_tpm_crb_t *tpm, uint32_t pcr_index,
                                    const ib_tpm2_digest_t *digests,
                                    size_t digest_count,
                                    ib_tcg2_error_t *error) {
    uint8_t command[IB_TPM2_PCR_EXTEND_CAPACITY];
    uint8_t reply[IB_TPM2_PCR_REPLY_CAPACITY];
    size_t length;
    size_t reply_length = 0;
    ib_tpm_crb_error_t tpm_error;
    ib_tpm2_error_t reply_error;
    ib_tpm2_code code;
    ib_tpm_crb_code crb;

    length = 0;
    if (!ib_tpm2_pcr_extend(command, sizeof(command), pcr_index, digests,
                            digest_count, &length)) {
        ib_tcg2_command_too_long(error, "TPM2_PCR_Extend");
        return IB_EFI_DEVICE_ERROR;
    }

    crb = ib_tpm_crb_transmit(tpm, command, length, reply, sizeof(reply),
                              &reply_length, &tpm_error);
    if (crb != IB_TPM_CRB_OK) {
        ib_tcg2_from_tpm(error, &tpm_error);
        return crb == IB_TPM_CRB_ERR_RESPONSE_TOO_LONG
                   ? IB_EFI_BUFFER_TOO_SMALL
                   : IB_EFI_DEVICE_ERROR;
    }

    code = ib_tpm2_accepted(reply, reply_length, &reply_error);
    if (code != IB_TPM2_OK) {
        ib_tcg2_from_reply(error, &reply_error);
        return IB_EFI_DEVICE_ERROR;
    }

    return IB_EFI_SUCCESS;
}

static ib_efi_status ib_tcg2_hash_log_extend_event(
    ib_efi_tcg2_protocol *self, ib_u64 flags,
    ib_efi_physical_address data_to_hash, ib_u64 data_to_hash_len,
    ib_efi_tcg2_event *event) {
    ib_tcg2_instance_t *instance = ib_tcg2_instance_of(self);
    ib_tcg2_state_t *state;
    const uint8_t *event_bytes;
    size_t length;
    size_t header_length;
    size_t measured_length;
    size_t measured_at;
    const uint8_t *measured;
    ib_tcg2_hasher_t hasher;
    ib_tcg2_digests_t digests;
    ib_tpm2_digest_t carried[IB_TPM2_BANK_COUNT_MAX];
    size_t carried_count;
    uint32_t declared;
    uint32_t pcr_index;
    uint32_t event_type;
    uint16_t header_version;
    ib_efi_status status;
    ib_tcg2_error_t error;
    ib_tcg2_code code;

    if (instance == 0) {
        return IB_EFI_INVALID_PARAMETER;
    }
    state = &instance->state;

    if (data_to_hash == 0 || event == 0) {
        return IB_EFI_INVALID_PARAMETER;
    }

    if (!ib_tcg2_read_le32((const uint8_t *)event, 4,
                           IB_TCG2_EVENT_SIZE_AT, &declared)) {
        return IB_EFI_INVALID_PARAMETER;
    }
    length = (size_t)declared;

    if (length < IB_TCG2_EVENT_DATA_AT) {
        return IB_EFI_INVALID_PARAMETER;
    }

    event_bytes = (const uint8_t *)event;

    header_length = 0;
    header_version = 0;
    if (!ib_tcg2_read_le32(event_bytes, length, IB_TCG2_EVENT_HEADER_LEN_AT,
                           &declared) ||
        !ib_tcg2_read_le16(event_bytes, length, IB_TCG2_EVENT_HEADER_VERSION_AT,
                           &header_version) ||
        !ib_tcg2_read_le32(event_bytes, length, IB_TCG2_EVENT_PCR_INDEX_AT,
                           &pcr_index) ||
        !ib_tcg2_read_le32(event_bytes, length, IB_TCG2_EVENT_TYPE_AT,
                           &event_type)) {
        return IB_EFI_INVALID_PARAMETER;
    }
    header_length = declared;

    if (header_length != IB_TCG2_EVENT_HEADER_LEN ||
        header_version != IB_TCG2_EVENT_HEADER_VERSION ||
        pcr_index > IB_TCG2_MAX_PCR_INDEX) {
        return IB_EFI_INVALID_PARAMETER;
    }

    /* PCRs the replay owns are left as the replay left them. */
    if (pcr_index < IB_TCG2_REPLAYED_PCR_COUNT) {
        return IB_EFI_SUCCESS;
    }

    if (data_to_hash_len > (ib_u64)SIZE_MAX) {
        return IB_EFI_INVALID_PARAMETER;
    }
    measured_length = (size_t)data_to_hash_len;
    measured_at = (size_t)data_to_hash;
    measured = (const uint8_t *)(uintptr_t)measured_at;

    code = ib_tcg2_hasher_new(&hasher, state->banks, state->bank_count,
                              &error);
    if (code != IB_TCG2_OK) {
        return IB_EFI_DEVICE_ERROR;
    }

    if ((flags & IB_EFI_TCG2_PE_COFF_IMAGE) != 0) {
        if (ib_tcg2_pecoff_hash(&hasher, measured, measured_length,
                                &error) != IB_TCG2_OK) {
            return IB_EFI_UNSUPPORTED;
        }
    } else {
        ib_tcg2_hasher_update(&hasher, measured, measured_length);
    }

    ib_tcg2_hasher_finish(&hasher, &digests);
    carried_count = ib_tcg2_digests_carry(&digests, carried,
                                          IB_TPM2_BANK_COUNT_MAX);

    status = ib_tcg2_extend(&state->tpm, pcr_index, carried, carried_count,
                            &error);
    if (status != IB_EFI_SUCCESS) {
        return status;
    }

    if ((flags & IB_EFI_TCG2_EXTEND_ONLY) != 0) {
        return IB_EFI_SUCCESS;
    }

    {
        size_t entry_length = 0;
        uint8_t *entry;
        ib_efi_system_table *system = ib_efi_get_system_table();
        ib_efi_status alloc_status;
        int logged;
        int kept;

        code = ib_tcg2_event2(pcr_index, event_type, carried, carried_count,
                              event_bytes + IB_TCG2_EVENT_DATA_AT,
                              length - IB_TCG2_EVENT_DATA_AT, 0, 0,
                              &entry_length);
        if (code != IB_TCG2_OK) {
            return IB_EFI_VOLUME_FULL;
        }

        entry = 0;
        alloc_status = system->boot_services->allocate_pool(
            IbEfiLoaderData, entry_length, (void **)&entry);
        if (IB_EFI_IS_ERROR(alloc_status)) {
            return IB_EFI_VOLUME_FULL;
        }

        code = ib_tcg2_event2(pcr_index, event_type, carried, carried_count,
                              event_bytes + IB_TCG2_EVENT_DATA_AT,
                              length - IB_TCG2_EVENT_DATA_AT, entry,
                              entry_length, &entry_length);
        if (code != IB_TCG2_OK) {
            system->boot_services->free_pool(entry);
            return IB_EFI_VOLUME_FULL;
        }

        logged = ib_tcg2_log_append(&state->log, entry, entry_length);
        kept = !state->log_taken ||
               ib_tcg2_final_events_append(&state->final_events, entry,
                                           entry_length);

        system->boot_services->free_pool(entry);

        return (logged && kept) ? IB_EFI_SUCCESS : IB_EFI_VOLUME_FULL;
    }
}

static ib_efi_status ib_tcg2_submit_command(
    ib_efi_tcg2_protocol *self, ib_u32 input_parameter_block_size,
    ib_u8 *input_parameter_block, ib_u32 output_parameter_block_size,
    ib_u8 *output_parameter_block) {
    ib_tcg2_instance_t *instance = ib_tcg2_instance_of(self);
    size_t response_length = 0;
    ib_tpm_crb_error_t tpm_error;
    ib_tpm_crb_code crb;

    if (instance == 0) {
        return IB_EFI_INVALID_PARAMETER;
    }

    if (input_parameter_block == 0 || output_parameter_block == 0 ||
        input_parameter_block_size == 0 || output_parameter_block_size == 0) {
        return IB_EFI_INVALID_PARAMETER;
    }

    if (input_parameter_block_size > instance->state.capability.max_command_size ||
        output_parameter_block_size >
            instance->state.capability.max_response_size) {
        return IB_EFI_INVALID_PARAMETER;
    }

    crb = ib_tpm_crb_transmit(&instance->state.tpm, input_parameter_block,
                              input_parameter_block_size,
                              output_parameter_block,
                              output_parameter_block_size, &response_length,
                              &tpm_error);
    if (crb == IB_TPM_CRB_OK) {
        return IB_EFI_SUCCESS;
    }

    return crb == IB_TPM_CRB_ERR_RESPONSE_TOO_LONG
               ? IB_EFI_BUFFER_TOO_SMALL
               : IB_EFI_DEVICE_ERROR;
}

static ib_efi_status ib_tcg2_get_active_pcr_banks(
    ib_efi_tcg2_protocol *self,
    ib_efi_tcg2_event_algorithm_bitmap *active_pcr_banks) {
    ib_tcg2_instance_t *instance = ib_tcg2_instance_of(self);

    if (instance == 0) {
        return IB_EFI_INVALID_PARAMETER;
    }
    if (active_pcr_banks == 0) {
        return IB_EFI_INVALID_PARAMETER;
    }

    *active_pcr_banks = instance->state.capability.active_pcr_banks;
    return IB_EFI_SUCCESS;
}

static ib_efi_status ib_tcg2_set_active_pcr_banks(
    ib_efi_tcg2_protocol *self,
    ib_efi_tcg2_event_algorithm_bitmap active_pcr_banks) {
    (void)self;
    (void)active_pcr_banks;
    return IB_EFI_UNSUPPORTED;
}

static ib_efi_status ib_tcg2_get_result_of_set_active_pcr_banks(
    ib_efi_tcg2_protocol *self, ib_u32 *operation_present, ib_u32 *response) {
    (void)self;

    if (operation_present == 0 || response == 0) {
        return IB_EFI_INVALID_PARAMETER;
    }

    *operation_present = 0;
    *response = 0;
    return IB_EFI_SUCCESS;
}

const ib_efi_tcg2_protocol ib_tcg2_protocol_template = {
    ib_tcg2_get_capability,
    ib_tcg2_get_event_log,
    ib_tcg2_hash_log_extend_event,
    ib_tcg2_submit_command,
    ib_tcg2_get_active_pcr_banks,
    ib_tcg2_set_active_pcr_banks,
    ib_tcg2_get_result_of_set_active_pcr_banks
};

ib_efi_tcg2_protocol ib_tcg2_protocol_table(void) {
    return ib_tcg2_protocol_template;
}
