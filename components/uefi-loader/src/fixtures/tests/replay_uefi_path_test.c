/* Host test for BSA device-path resolution and boot-manager substitution. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "replay_uefi.h"

EFI_SYSTEM_TABLE *gST = NULL;
static EFI_BOOT_SERVICES g_boot_services;
EFI_BOOT_SERVICES *gBS = &g_boot_services;

static const UINT8 g_backup_bytes[] = { 'B', 'A', 'K' };
static const UINT8 g_other_bytes[] = { 'O', 'T', 'H', 'E', 'R' };
static const CHAR16 g_bootmgfw_path[] = {
    '\\', 'E', 'F', 'I', '\\', 'M', 'i', 'c', 'r', 'o', 's', 'o', 'f', 't',
    '\\', 'B', 'o', 'o', 't', '\\', 'b', 'o', 'o', 't', 'm', 'g', 'f', 'w',
    '.', 'e', 'f', 'i', 0
};
static const CHAR16 g_backup_path[] = {
    '\\', 'E', 'F', 'I', '\\', 'M', 'i', 'c', 'r', 'o', 's', 'o', 'f', 't',
    '\\', 'B', 'o', 'o', 't', '\\', 'b', 'o', 'o', 't', 'm', 'g', 'f', 'w',
    '.', 'e', 'f', 'i', '.', 'b', 'a', 'k', 0
};

static const CHAR16 g_other_path[] = {
    '\\', 'E', 'F', 'I', '\\', 'O', 't', 'h', 'e', 'r', '\\', 'o', 't', 'h',
    'e', 'r', '.', 'e', 'f', 'i', 0
};

static const CHAR16 *g_last_file_path;
static UINT64 g_last_flags;
static const UINT8 *g_last_data;
static UINTN g_last_data_size;

static int path_equal(const CHAR16 *left, const CHAR16 *right)
{
    size_t i = 0;
    while (left[i] != 0 && right[i] != 0) {
        if (left[i] != right[i]) {
            return 0;
        }
        i++;
    }
    return left[i] == right[i];
}

EFI_STATUS efi_file_read(EFI_HANDLE image_handle, const CHAR16 *path,
                         efi_file_buffer_t *out)
{
    (void)image_handle;
    if (path == NULL || out == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    g_last_file_path = path;
    if (path_equal(path, g_backup_path)) {
        out->bytes = (UINT8 *)(uintptr_t)g_backup_bytes;
        out->size = sizeof(g_backup_bytes);
        return EFI_SUCCESS;
    }
    if (path_equal(path, g_other_path)) {
        out->bytes = (UINT8 *)(uintptr_t)g_other_bytes;
        out->size = sizeof(g_other_bytes);
        return EFI_SUCCESS;
    }
    out->bytes = NULL;
    out->size = 0;
    return EFI_NOT_FOUND;
}

void efi_file_release(efi_file_buffer_t *buffer)
{
    if (buffer != NULL) {
        buffer->bytes = NULL;
        buffer->size = 0;
    }
}

static EFI_STATUS EFIAPI fake_allocate_pool(EFI_MEMORY_TYPE type,
                                              UINTN size, VOID **buffer)
{
    (void)type;
    if (buffer == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *buffer = malloc((size_t)size);
    return *buffer == NULL ? EFI_OUT_OF_RESOURCES : EFI_SUCCESS;
}

static EFI_STATUS EFIAPI fake_free_pool(VOID *buffer)
{
    free(buffer);
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI fake_hash_log_extend_event(
    EFI_TCG2_PROTOCOL *protocol, UINT64 flags,
    EFI_PHYSICAL_ADDRESS data_to_hash, UINT64 data_to_hash_len,
    EFI_TCG2_EVENT *event)
{
    (void)protocol;
    (void)event;
    g_last_flags = flags;
    g_last_data = (const UINT8 *)(uintptr_t)data_to_hash;
    g_last_data_size = (UINTN)data_to_hash_len;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI fake_submit_command(
    EFI_TCG2_PROTOCOL *protocol, UINT32 input_size, UINT8 *input,
    UINT32 output_size, UINT8 *output)
{
    (void)protocol;
    (void)input_size;
    (void)input;
    (void)output_size;
    (void)output;
    return EFI_SUCCESS;
}

static void put_u16(UINT8 *buffer, size_t *position, UINT16 value)
{
    buffer[(*position)++] = (UINT8)(value & 0xffu);
    buffer[(*position)++] = (UINT8)(value >> 8);
}

static void put_u64(UINT8 *buffer, size_t *position, UINT64 value)
{
    size_t i;
    for (i = 0; i < 8; i++) {
        buffer[(*position)++] = (UINT8)(value >> (i * 8u));
    }
}

static size_t build_image_event(UINT8 *buffer, const CHAR16 *path,
                                size_t path_chars)
{
    size_t i;
    size_t position = 0;
    UINT16 node_length = (UINT16)(4u + (path_chars + 1u) * 2u);
    UINT16 device_path_length = (UINT16)(node_length + 4u);

    put_u64(buffer, &position, 0);
    put_u64(buffer, &position, 0);
    put_u64(buffer, &position, 0);
    put_u64(buffer, &position, device_path_length);
    buffer[position++] = 0x04;
    buffer[position++] = 0x04;
    put_u16(buffer, &position, node_length);
    for (i = 0; i < path_chars; i++) {
        put_u16(buffer, &position, path[i]);
    }
    put_u16(buffer, &position, 0);
    buffer[position++] = 0x7f;
    buffer[position++] = 0xff;
    put_u16(buffer, &position, 4);
    return position;
}

static void initialize_uefi_mocks(EFI_TCG2_PROTOCOL *tcg2)
{
    memset(&g_boot_services, 0, sizeof(g_boot_services));
    g_boot_services.AllocatePool = fake_allocate_pool;
    g_boot_services.FreePool = fake_free_pool;
    memset(tcg2, 0, sizeof(*tcg2));
    tcg2->HashLogExtendEvent = fake_hash_log_extend_event;
    tcg2->SubmitCommand = fake_submit_command;
}

static int run_one(const CHAR16 *recorded_path, size_t recorded_chars,
                   const CHAR16 *expected_source,
                   const UINT8 *expected_bytes, size_t expected_size)
{
    UINT8 event_data[256];
    UINT8 digest_wire[34];
    UINT8 header_algs[4] = { 0x0b, 0x00, 0x20, 0x00 };
    tcglog_header_t header;
    tcglog_event_t event;
    tcglog_t log;
    EFI_TCG2_PROTOCOL tcg2;
    replay_uefi_context_t context;
    replay_callbacks_t callbacks;
    size_t event_size;
    size_t i;

    event_size = build_image_event(event_data, recorded_path, recorded_chars);
    digest_wire[0] = 0x0b;
    digest_wire[1] = 0x00;
    for (i = 0; i < 32; i++) {
        digest_wire[2u + i] = (UINT8)(0xa0u + (UINT8)i);
    }
    memset(&header, 0, sizeof(header));
    header.alg_count = 1;
    header.algs = header_algs;
    memset(&event, 0, sizeof(event));
    event.pcr_index = 4;
    event.event_type = TCGLOG_EVT_EFI_BOOT_SERVICES_APPLICATION;
    event.digest_count = 1;
    event.digests = digest_wire;
    event.event_data = event_data;
    event.event_size = (UINT32)event_size;
    event.header = &header;
    log.events = &event;
    log.event_count = 1;
    initialize_uefi_mocks(&tcg2);
    g_last_file_path = NULL;
    g_last_flags = 0;
    g_last_data = NULL;
    g_last_data_size = 0;
    if (replay_uefi_init(&context, (EFI_HANDLE)(uintptr_t)1, &tcg2,
                         g_backup_path, &callbacks) != EFI_SUCCESS) {
        return 0;
    }
    if (replay_run(&log, REPLAY_BANK_SHA256, &callbacks) != REPLAY_OK) {
        replay_uefi_release(&context);
        return 0;
    }
    replay_uefi_release(&context);
    return g_last_file_path != NULL && path_equal(g_last_file_path,
           expected_source) && (g_last_flags & EFI_TCG2_PE_COFF_IMAGE) != 0u &&
           g_last_data_size == expected_size &&
           memcmp(g_last_data, expected_bytes, expected_size) == 0;
}

int main(void)
{
    int failures = 0;
    failures += !run_one(g_bootmgfw_path,
                         sizeof(g_bootmgfw_path) / sizeof(g_bootmgfw_path[0]) - 1u,
                         g_backup_path, g_backup_bytes, sizeof(g_backup_bytes));
    failures += !run_one(g_other_path,
                         sizeof(g_other_path) / sizeof(g_other_path[0]) - 1u,
                         g_other_path, g_other_bytes, sizeof(g_other_bytes));
    printf("replay_uefi_path_test: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
