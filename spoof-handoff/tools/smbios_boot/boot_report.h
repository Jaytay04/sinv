#ifndef HYPERSINV_SMBIOS_BOOT_REPORT_H
#define HYPERSINV_SMBIOS_BOOT_REPORT_H

#include <stddef.h>
#include <stdint.h>

static int sm_boot_report_append(char *buffer,
                                 size_t capacity,
                                 size_t *length,
                                 const char *text)
{
    size_t index = 0u;
    if (buffer == NULL || length == NULL || text == NULL || *length > capacity) {
        return 0;
    }
    while (text[index] != '\0') {
        if (*length >= capacity) {
            return 0;
        }
        buffer[(*length)++] = text[index++];
    }
    return 1;
}

static int sm_boot_report_append_hex64(char *buffer,
                                       size_t capacity,
                                       size_t *length,
                                       uint64_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    size_t index;
    if (buffer == NULL || length == NULL || *length > capacity ||
        capacity - *length < 16u) {
        return 0;
    }
    for (index = 0u; index < 16u; ++index) {
        const unsigned shift = (unsigned)((15u - index) * 4u);
        buffer[(*length)++] = digits[(size_t)((value >> shift) & UINT64_C(0xF))];
    }
    return 1;
}

static int sm_boot_report_phase_is_valid(const char *phase)
{
    size_t index = 0u;
    if (phase == NULL || phase[0] == '\0') {
        return 0;
    }
    while (phase[index] != '\0') {
        const char value = phase[index++];
        if (!((value >= 'a' && value <= 'z') || value == '_')) {
            return 0;
        }
    }
    return 1;
}

/* Pure formatter shared by the EFI payload and host-only regression test. */
static int sm_boot_format_failure_record(char *buffer,
                                         size_t capacity,
                                         size_t *length,
                                         const char *phase,
                                         uint64_t failure_status,
                                         int rollback_attempted,
                                         uint64_t rollback_status,
                                         int free_attempted,
                                         uint64_t free_status,
                                         uint64_t cleanup_free_status)
{
    size_t output_length = 0u;
    if (buffer == NULL || length == NULL || !sm_boot_report_phase_is_valid(phase)) {
        return 0;
    }
    if (!sm_boot_report_append(buffer, capacity, &output_length, "failure_phase=") ||
        !sm_boot_report_append(buffer, capacity, &output_length, phase) ||
        !sm_boot_report_append(buffer, capacity, &output_length, "\r\nstatus_scope=public_call_return") ||
        !sm_boot_report_append(buffer, capacity, &output_length, "\r\nfailure_status=0x") ||
        !sm_boot_report_append_hex64(buffer, capacity, &output_length, failure_status) ||
        !sm_boot_report_append(buffer, capacity, &output_length, "\r\nrollback_attempted=") ||
        !sm_boot_report_append(buffer, capacity, &output_length, rollback_attempted ? "1" : "0") ||
        !sm_boot_report_append(buffer, capacity, &output_length, "\r\nrollback_status=0x") ||
        !sm_boot_report_append_hex64(buffer, capacity, &output_length, rollback_status) ||
        !sm_boot_report_append(buffer, capacity, &output_length, "\r\nfree_attempted=") ||
        !sm_boot_report_append(buffer, capacity, &output_length, free_attempted ? "1" : "0") ||
        !sm_boot_report_append(buffer, capacity, &output_length, "\r\nfree_status=0x") ||
        !sm_boot_report_append_hex64(buffer, capacity, &output_length, free_status) ||
        !sm_boot_report_append(buffer, capacity, &output_length, "\r\ncleanup_free_status=0x") ||
        !sm_boot_report_append_hex64(buffer, capacity, &output_length, cleanup_free_status) ||
        !sm_boot_report_append(buffer, capacity, &output_length, "\r\nresult=failed\r\nEND\r\n")) {
        return 0;
    }
    *length = output_length;
    return 1;
}

#endif
