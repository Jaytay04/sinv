/* payload_diagnostic.c - Bounded payload launch evidence formatter. */
#include "payload_diagnostic.h"

typedef struct {
    const char *Token;
    UINTN Length;
} payload_diagnostic_token_t;

static const payload_diagnostic_token_t g_launch_tokens[] = {
    { "skipped", 7u },
    { "absent", 6u },
    { "presence_error", 14u },
    { "argument", 8u },
    { "not_found", 9u },
    { "read", 4u },
    { "device_path", 11u },
    { "load", 4u },
    { "image_format", 12u },
    { "protocol", 8u },
    { "start", 5u },
    { "ok", 2u }
};

static const payload_diagnostic_token_t g_shim_tokens[] = {
    { "not_applicable", 14u },
    { "argument", 8u },
    { "reentrant", 9u },
    { "firmware_load", 13u },
    { "shim_lookup", 11u },
    { "secure_boot_read", 16u },
    { "firmware_direct", 15u },
    { "enforcement", 11u },
    { "shim_verify", 11u },
    { "security2_lookup", 16u },
    { "security2_interface", 19u }
};

static const payload_diagnostic_token_t g_startup_stage_tokens[] = {
    { "none", 4u },
    { "entry", 5u },
    { "selftest", 8u },
    { "context_alloc", 13u },
    { "topology", 8u },
    { "dma_admission", 13u },
    { "resident_alloc", 14u },
    { "resident_bounds", 15u },
    { "scanner_alloc", 13u },
    { "scanner_bounds", 14u },
    { "transition_alloc", 16u },
    { "transition_bounds", 17u },
    { "vmxon_alloc", 11u },
    { "vmcs_alloc", 10u },
    { "host_stack_alloc", 16u },
    { "guest_stack_alloc", 17u },
    { "page_table_alloc", 16u },
    { "descriptor_alloc", 16u },
    { "bsp_bind", 8u },
    { "private_paging", 14u },
    { "cpu_capture", 11u },
    { "descriptors", 11u },
    { "cpu_features", 12u },
    { "feature_control", 15u },
    { "vmx_basic", 9u },
    { "gpu_prepare", 11u },
    { "ap_resources", 12u },
    { "controls", 8u },
    { "ap_prepare", 10u },
    { "gpu_arm", 7u },
    { "ap_activate", 11u },
    { "vmxon", 5u },
    { "vmclear", 7u },
    { "vmptrld", 7u },
    { "vmcs_controls", 13u },
    { "vmcs_guest", 10u },
    { "vmcs_host", 9u },
    { "vmlaunch", 8u },
    { "return", 6u }
};

static const payload_diagnostic_token_t g_startup_reason_tokens[] = {
    { "none", 4u },
    { "operation_failed", 16u },
    { "invariant", 9u },
    { "bounds", 6u },
    { "unsupported", 11u }
};

static int append_byte(UINT8 *Buffer, UINTN Capacity, UINTN *Used,
                       UINT8 Value)
{
    if (Buffer == NULL || Used == NULL || *Used >= Capacity) {
        return 0;
    }
    Buffer[*Used] = Value;
    (*Used)++;
    return 1;
}

static int append_text(UINT8 *Buffer, UINTN Capacity, UINTN *Used,
                       const char *Text, UINTN Length)
{
    UINTN index;

    if (Buffer == NULL || Text == NULL || Used == NULL || *Used > Capacity ||
        Length > Capacity - *Used) {
        return 0;
    }
    for (index = 0; index < Length; index++) {
        Buffer[*Used + index] = (UINT8)Text[index];
    }
    *Used += Length;
    return 1;
}

static int append_hex64(UINT8 *Buffer, UINTN Capacity, UINTN *Used,
                        UINT64 Value)
{
    static const char digits[] = "0123456789ABCDEF";
    UINTN index;

    if (Buffer == NULL || Used == NULL || *Used > Capacity ||
        Capacity - *Used < 16u) {
        return 0;
    }
    for (index = 0; index < 16u; index++) {
        UINTN shift = (15u - index) * 4u;
        Buffer[*Used + index] = (UINT8)digits[(Value >> shift) & 0x0fu];
    }
    *Used += 16u;
    return 1;
}

static int append_hex32(UINT8 *Buffer, UINTN Capacity, UINTN *Used,
                        UINT32 Value)
{
    static const char digits[] = "0123456789ABCDEF";
    UINTN index;

    if (Buffer == NULL || Used == NULL || *Used > Capacity ||
        Capacity - *Used < 8u) {
        return 0;
    }
    for (index = 0; index < 8u; index++) {
        UINTN shift = (7u - index) * 4u;
        Buffer[*Used + index] = (UINT8)digits[(Value >> shift) & 0x0fu];
    }
    *Used += 8u;
    return 1;
}

static int append_uint32(UINT8 *Buffer, UINTN Capacity, UINTN *Used,
                         UINT32 Value)
{
    UINT8 digits[10];
    UINTN count = 0u;

    do {
        digits[count++] = (UINT8)('0' + (Value % 10u));
        Value /= 10u;
    } while (Value != 0u);
    if (Buffer == NULL || Used == NULL || *Used > Capacity ||
        count > Capacity - *Used) {
        return 0;
    }
    while (count != 0u) {
        Buffer[(*Used)++] = digits[--count];
    }
    return 1;
}

static int token_equals(const char *Text,
                        const payload_diagnostic_token_t *Token)
{
    UINTN index;

    if (Text == NULL || Token == NULL) {
        return 0;
    }
    for (index = 0; index < Token->Length; index++) {
        if (Text[index] != Token->Token[index]) {
            return 0;
        }
    }
    return Text[Token->Length] == '\0';
}

static int valid_shim_token(const char *Text, UINTN *IndexOut)
{
    UINTN index;
    for (index = 0; index <
         (UINTN)(sizeof(g_shim_tokens) / sizeof(g_shim_tokens[0])); index++) {
        if (token_equals(Text, &g_shim_tokens[index])) {
            if (IndexOut != NULL) {
                *IndexOut = index;
            }
            return 1;
        }
    }
    return 0;
}

static int shim_trace_reached(payload_diagnostic_launch_t Launch)
{
    return Launch == PAYLOAD_DIAG_LOAD ||
           Launch == PAYLOAD_DIAG_PROTOCOL ||
           Launch == PAYLOAD_DIAG_START ||
           Launch == PAYLOAD_DIAG_OK;
}

static int startup_record_consistent(const hv_boot_diagnostic_t *Record)
{
    if (!hv_boot_diagnostic_valid(Record, (UINT32)sizeof(*Record)) ||
        Record->Flags != HV_BOOT_DIAGNOSTIC_FLAGS ||
        Record->Stage == HV_BOOT_STAGE_NONE) {
        return 0;
    }
    if (Record->Status == (UINT64)EFI_SUCCESS) {
        return Record->Stage == HV_BOOT_STAGE_RETURN &&
               Record->Reason == HV_BOOT_REASON_NONE;
    }
    if (EFI_ERROR((EFI_STATUS)Record->Status)) {
        return Record->Stage >= HV_BOOT_STAGE_ENTRY &&
               Record->Stage <= HV_BOOT_STAGE_VMLAUNCH &&
               Record->Reason != HV_BOOT_REASON_NONE;
    }
    return 0;
}

int payload_diagnostic_format_with_startup(
    const payload_diagnostic_t *Diagnostic,
    payload_diagnostic_startup_state_t StartupState,
    const hv_boot_diagnostic_t *StartupRecord,
    UINT8 *Buffer, UINTN Capacity, UINTN *Written)
{
    UINT8 formatted[PAYLOAD_DIAGNOSTIC_MAX_SIZE];
    UINTN used = 0;
    UINTN phase_index = 0;
    UINTN launch_index;
    UINTN stage_index = 0u;
    UINTN reason_index = 0u;
    UINTN index;
    const char *shim_phase;
    EFI_STATUS shim_status;

    if (Written != NULL) {
        *Written = 0u;
    }
    if (Diagnostic == NULL || Buffer == NULL || Written == NULL ||
        (UINTN)Diagnostic->payload_launch >=
            (UINTN)(sizeof(g_launch_tokens) / sizeof(g_launch_tokens[0])) ||
        (UINTN)StartupState > PAYLOAD_DIAG_STARTUP_VALID) {
        return 0;
    }

    if (StartupState == PAYLOAD_DIAG_STARTUP_VALID) {
        if (!startup_record_consistent(StartupRecord) ||
            StartupRecord->Status != (UINT64)Diagnostic->payload_status ||
            (StartupRecord->Status == (UINT64)EFI_SUCCESS &&
             Diagnostic->payload_launch != PAYLOAD_DIAG_OK) ||
            (EFI_ERROR((EFI_STATUS)StartupRecord->Status) &&
             Diagnostic->payload_launch != PAYLOAD_DIAG_START) ||
            StartupRecord->Stage >=
                (UINT32)(sizeof(g_startup_stage_tokens) /
                         sizeof(g_startup_stage_tokens[0])) ||
            StartupRecord->Reason >=
                (UINT32)(sizeof(g_startup_reason_tokens) /
                         sizeof(g_startup_reason_tokens[0]))) {
            return 0;
        }
        stage_index = (UINTN)StartupRecord->Stage;
        reason_index = (UINTN)StartupRecord->Reason;
    }

    shim_phase = Diagnostic->shim_load_phase;
    shim_status = Diagnostic->shim_load_status;
    if (!shim_trace_reached(Diagnostic->payload_launch)) {
        /* No shim load happened for these outcomes. Discard any old trace. */
        shim_phase = "not_applicable";
        shim_status = EFI_NOT_STARTED;
        phase_index = 0u;
    } else if (!valid_shim_token(shim_phase, &phase_index)) {
        return 0;
    }
    launch_index = (UINTN)Diagnostic->payload_launch;

    if (!append_text(formatted, sizeof(formatted), &used, "schema=1\n", 9u) ||
        !append_text(formatted, sizeof(formatted), &used, "payload_launch=", 15u) ||
        !append_text(formatted, sizeof(formatted), &used,
                     g_launch_tokens[launch_index].Token,
                     g_launch_tokens[launch_index].Length) ||
        !append_byte(formatted, sizeof(formatted), &used, '\n') ||
        !append_text(formatted, sizeof(formatted), &used,
                     "payload_status=0x", 17u) ||
        !append_hex64(formatted, sizeof(formatted), &used,
                      (UINT64)Diagnostic->payload_status) ||
        !append_byte(formatted, sizeof(formatted), &used, '\n') ||
        !append_text(formatted, sizeof(formatted), &used,
                     "shim_load_phase=", 16u) ||
        !append_text(formatted, sizeof(formatted), &used,
                     g_shim_tokens[phase_index].Token,
                     g_shim_tokens[phase_index].Length) ||
        !append_byte(formatted, sizeof(formatted), &used, '\n') ||
        !append_text(formatted, sizeof(formatted), &used,
                     "shim_load_status=0x", 19u) ||
        !append_hex64(formatted, sizeof(formatted), &used,
                      (UINT64)shim_status) ||
        !append_byte(formatted, sizeof(formatted), &used, '\n')) {
        return 0;
    }

    if (StartupState != PAYLOAD_DIAG_STARTUP_NONE) {
        const char *state_token = StartupState == PAYLOAD_DIAG_STARTUP_VALID ?
                                  "valid" :
                                  (StartupState == PAYLOAD_DIAG_STARTUP_INVALID ?
                                   "invalid" : "unavailable");
        UINTN state_length = StartupState == PAYLOAD_DIAG_STARTUP_VALID ?
                             5u :
                             (StartupState == PAYLOAD_DIAG_STARTUP_INVALID ?
                              7u : 11u);
        if (!append_text(formatted, sizeof(formatted), &used,
                         "startup_record=", 15u) ||
            !append_text(formatted, sizeof(formatted), &used,
                         state_token, state_length) ||
            !append_byte(formatted, sizeof(formatted), &used, '\n')) {
            return 0;
        }
        if (StartupState == PAYLOAD_DIAG_STARTUP_VALID) {
            const hv_boot_diagnostic_t *record = StartupRecord;
            if (!append_text(formatted, sizeof(formatted), &used,
                             "startup_version=", 16u) ||
                !append_uint32(formatted, sizeof(formatted), &used,
                               record->Version) ||
                !append_byte(formatted, sizeof(formatted), &used, '\n') ||
                !append_text(formatted, sizeof(formatted), &used,
                             "startup_flags=0x", 16u) ||
                !append_hex32(formatted, sizeof(formatted), &used,
                              record->Flags) ||
                !append_byte(formatted, sizeof(formatted), &used, '\n') ||
                !append_text(formatted, sizeof(formatted), &used,
                             "startup_stage=", 14u) ||
                !append_text(formatted, sizeof(formatted), &used,
                             g_startup_stage_tokens[stage_index].Token,
                             g_startup_stage_tokens[stage_index].Length) ||
                !append_byte(formatted, sizeof(formatted), &used, '\n') ||
                !append_text(formatted, sizeof(formatted), &used,
                             "startup_reason=", 15u) ||
                !append_text(formatted, sizeof(formatted), &used,
                             g_startup_reason_tokens[reason_index].Token,
                             g_startup_reason_tokens[reason_index].Length) ||
                !append_byte(formatted, sizeof(formatted), &used, '\n') ||
                !append_text(formatted, sizeof(formatted), &used,
                             "startup_status=0x", 17u) ||
                !append_hex64(formatted, sizeof(formatted), &used,
                              record->Status) ||
                !append_byte(formatted, sizeof(formatted), &used, '\n') ||
                !append_text(formatted, sizeof(formatted), &used,
                             "startup_processor=", 18u) ||
                !append_uint32(formatted, sizeof(formatted), &used,
                               record->Processor) ||
                !append_byte(formatted, sizeof(formatted), &used, '\n') ||
                !append_text(formatted, sizeof(formatted), &used,
                             "startup_detail=", 15u) ||
                !append_uint32(formatted, sizeof(formatted), &used,
                               record->Detail) ||
                !append_byte(formatted, sizeof(formatted), &used, '\n') ||
                !append_text(formatted, sizeof(formatted), &used,
                             "startup_expected_cpus=", 22u) ||
                !append_uint32(formatted, sizeof(formatted), &used,
                               record->ExpectedCpus) ||
                !append_byte(formatted, sizeof(formatted), &used, '\n') ||
                !append_text(formatted, sizeof(formatted), &used,
                             "startup_prepared_cpus=", 22u) ||
                !append_uint32(formatted, sizeof(formatted), &used,
                               record->PreparedCpus) ||
                !append_byte(formatted, sizeof(formatted), &used, '\n') ||
                !append_text(formatted, sizeof(formatted), &used,
                             "startup_vmx_ready_cpus=", 23u) ||
                !append_uint32(formatted, sizeof(formatted), &used,
                               record->VmxReadyCpus) ||
                !append_byte(formatted, sizeof(formatted), &used, '\n') ||
                !append_text(formatted, sizeof(formatted), &used,
                             "startup_resource_status=0x", 26u) ||
                !append_hex32(formatted, sizeof(formatted), &used,
                              record->ResourceStatus) ||
                !append_byte(formatted, sizeof(formatted), &used, '\n') ||
                !append_text(formatted, sizeof(formatted), &used,
                             "startup_preparation_status=0x", 29u) ||
                !append_hex32(formatted, sizeof(formatted), &used,
                              record->PreparationStatus) ||
                !append_byte(formatted, sizeof(formatted), &used, '\n') ||
                !append_text(formatted, sizeof(formatted), &used,
                             "startup_preparation_failure=", 28u) ||
                !append_uint32(formatted, sizeof(formatted), &used,
                               record->PreparationFailure) ||
                !append_byte(formatted, sizeof(formatted), &used, '\n') ||
                !append_text(formatted, sizeof(formatted), &used,
                             "startup_activation_status=0x", 28u) ||
                !append_hex32(formatted, sizeof(formatted), &used,
                              record->ActivationStatus) ||
                !append_byte(formatted, sizeof(formatted), &used, '\n') ||
                !append_text(formatted, sizeof(formatted), &used,
                             "startup_activation_failure=", 27u) ||
                !append_uint32(formatted, sizeof(formatted), &used,
                               record->ActivationFailure) ||
                !append_byte(formatted, sizeof(formatted), &used, '\n')) {
                return 0;
            }
        }
    }

    if (used > Capacity) {
        return 0;
    }
    for (index = 0; index < used; index++) {
        Buffer[index] = formatted[index];
    }
    *Written = used;
    return 1;
}

int payload_diagnostic_format(const payload_diagnostic_t *Diagnostic,
                              UINT8 *Buffer, UINTN Capacity,
                              UINTN *Written)
{
    return payload_diagnostic_format_with_startup(
        Diagnostic, PAYLOAD_DIAG_STARTUP_NONE, NULL,
        Buffer, Capacity, Written);
}
