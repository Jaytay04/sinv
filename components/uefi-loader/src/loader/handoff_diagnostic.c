#include "handoff_diagnostic.h"

static UINTN token_length(const char *text)
{
    UINTN length;
    if (text == NULL) { return 0u; }
    for (length = 0u; length < 32u; length++) {
        char value = text[length];
        if (value == 0) { return length; }
        if (!((value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '_')) { return 0u; }
    }
    return 0u;
}

static int equal_token(const char *text, const char *expected)
{
    UINTN index = 0u;
    if (text == NULL) { return 0; }
    while (expected[index] != 0) {
        if (text[index] != expected[index]) { return 0; }
        index++;
    }
    return text[index] == 0;
}

static void text(UINT8 *buffer, UINTN *used, const char *value)
{
    while (*value != 0) { buffer[(*used)++] = (UINT8)*value++; }
}

static void hex(UINT8 *buffer, UINTN *used, EFI_STATUS status)
{
    static const char digits[] = "0123456789ABCDEF";
    UINTN index;
    text(buffer, used, "0x");
    for (index = 0; index < 16u; index++) {
        buffer[(*used)++] = (UINT8)digits[((UINT64)status >> ((15u - index) * 4u)) & 15u];
    }
    buffer[(*used)++] = (UINT8)'\n';
}

int handoff_diagnostic_append(UINT8 *Buffer, UINTN Capacity, UINTN *Used,
    const char *Target, const char *Phase, BOOLEAN Completed, EFI_STATUS Status,
    const char *ShimPhase, EFI_STATUS ShimStatus)
{
    /* Three records, each at most 8+12+1+31+8+18+1 bytes. */
    UINT8 record[256];
    UINTN count = 0u;
    UINTN index;
    if (Buffer == NULL || Used == NULL || *Used > Capacity ||
        !(equal_token(Target, "boot_manager") || equal_token(Target, "inventory")) ||
        !(equal_token(Phase, "read") || equal_token(Phase, "device_path") ||
          equal_token(Phase, "load") || equal_token(Phase, "start") ||
          equal_token(Phase, "return")) ||
        (ShimPhase != NULL && token_length(ShimPhase) == 0u)) { return 0; }
    text(record, &count, "handoff_"); text(record, &count, Target);
    text(record, &count, "_"); text(record, &count, Phase);
    text(record, &count, Completed ? "_end_status=" : "_begin_status=");
    hex(record, &count, Status);
    if (ShimPhase != NULL) {
        text(record, &count, "handoff_"); text(record, &count, Target);
        text(record, &count, "_shim_phase="); text(record, &count, ShimPhase);
        record[count++] = (UINT8)'\n';
        text(record, &count, "handoff_"); text(record, &count, Target);
        text(record, &count, "_shim_status="); hex(record, &count, ShimStatus);
    }
    if (count > Capacity - *Used) { return 0; }
    for (index = 0u; index < count; index++) { Buffer[*Used + index] = record[index]; }
    *Used += count;
    return 1;
}
