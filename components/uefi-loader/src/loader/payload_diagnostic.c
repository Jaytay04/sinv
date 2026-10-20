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

int payload_diagnostic_format(const payload_diagnostic_t *Diagnostic,
                              UINT8 *Buffer, UINTN Capacity,
                              UINTN *Written)
{
    UINT8 formatted[PAYLOAD_DIAGNOSTIC_MAX_SIZE];
    UINTN used = 0;
    UINTN phase_index = 0;
    UINTN launch_index;
    UINTN index;
    const char *shim_phase;
    EFI_STATUS shim_status;

    if (Written != NULL) {
        *Written = 0u;
    }
    if (Diagnostic == NULL || Buffer == NULL || Written == NULL ||
        (UINTN)Diagnostic->payload_launch >=
            (UINTN)(sizeof(g_launch_tokens) / sizeof(g_launch_tokens[0]))) {
        return 0;
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

    if (used > Capacity) {
        return 0;
    }
    for (index = 0; index < used; index++) {
        Buffer[index] = formatted[index];
    }
    *Written = used;
    return 1;
}
