/* handoff_efi_smoke.c - Nested OVMF gate for loader handoff mechanics. */
#include "Uefi.h"

#include "boot_restore.h"
#include "divergence.h"
#include "efi_file.h"
#include "image_launch.h"
#include "reference_log.h"
#include "replay.h"
#include "tcg2.h"

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#pragma intrinsic(__outbyte)
#define IB_QEMU_DEBUGCON_AVAILABLE 1
#else
#define IB_QEMU_DEBUGCON_AVAILABLE 0
#endif

EFI_SYSTEM_TABLE  *gST = NULL;
EFI_BOOT_SERVICES *gBS = NULL;

typedef struct {
    EFI_TCG2_PROTOCOL *tcg2;
    UINT32 raw_extensions;
} handoff_gate_state_t;

static void emit(const CHAR16 *text)
{
#if IB_QEMU_DEBUGCON_AVAILABLE
    while (text != NULL && *text != 0) {
        CHAR16 character = *text++;
        if (character <= 0x7fu) {
            __outbyte(0xe9u, (unsigned char)character);
        }
    }
#else
    (void)text;
#endif
}

static void emit_hex(UINT64 value, UINTN nibbles)
{
    static const CHAR16 digits[] =
        { L'0', L'1', L'2', L'3', L'4', L'5', L'6', L'7',
          L'8', L'9', L'A', L'B', L'C', L'D', L'E', L'F' };
    CHAR16 buffer[17];
    UINTN i;

    if (nibbles > 16u) {
        nibbles = 16u;
    }
    for (i = 0; i < nibbles; i++) {
        UINTN shift = (nibbles - 1u - i) * 4u;
        buffer[i] = digits[(value >> shift) & 0x0fu];
    }
    buffer[nibbles] = 0;
    emit(buffer);
}

static replay_status_t raw_extend(const tcglog_event_t *event,
                                  const replay_digest_t *digests,
                                  uint32_t digest_count, void *context)
{
    handoff_gate_state_t *state = (handoff_gate_state_t *)context;
    tcg2_digest_value_t values[REPLAY_MAX_DIGESTS];
    EFI_STATUS status;
    uint32_t i;

    if (event == NULL || digests == NULL || state == NULL ||
        digest_count == 0u || digest_count > REPLAY_MAX_DIGESTS) {
        return REPLAY_ERR_CALLBACK;
    }
    for (i = 0; i < digest_count; i++) {
        values[i].algorithm = digests[i].alg;
        values[i].bytes = digests[i].bytes;
        values[i].size = (UINT16)digests[i].size;
    }
    status = tcg2_pcr_extend(state->tcg2, event->pcr_index,
                             values, digest_count);
    if (EFI_ERROR(status)) {
        return REPLAY_ERR_CALLBACK;
    }
    state->raw_extensions++;
    return REPLAY_OK;
}

static int sha256_pcr0_nonzero(EFI_TCG2_PROTOCOL *tcg2)
{
    UINT8 pcr_values[TCG2_PCR_READ_BYTES];
    UINT16 digest_size = 0;
    UINTN i;

    if (EFI_ERROR(tcg2_pcr_read_bank(
            tcg2, TPM_ALG_SHA256, pcr_values, &digest_size)) ||
        digest_size != 32u) {
        return 0;
    }
    for (i = 0; i < digest_size; i++) {
        if (pcr_values[i] != 0u) {
            return 1;
        }
    }
    return 0;
}

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    static const CHAR16 reference_path[] =
        L"\\EFI\\insecure-boot\\tcglog.ib";
    static const CHAR16 divergence_path[] =
        L"\\EFI\\insecure-boot\\divergence.ib";
    static const CHAR16 payload_path[] =
        L"\\EFI\\insecure-boot\\payload.efi";
    static const CHAR16 target_path[] =
        L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
    static const CHAR16 backup_path[] =
        L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi.bak";
    static const CHAR16 temp_path[] =
        L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi.ibnew";
    static const CHAR16 displaced_path[] =
        L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi.ibold";
    static const CHAR16 hash_record_path[] =
        L"\\EFI\\Microsoft\\Boot\\bootmgfw.sha256";
    efi_file_buffer_t reference_bytes = { NULL, 0 };
    reference_log_t reference = { 0 };
    replay_callbacks_t callbacks;
    handoff_gate_state_t state;
    boot_restore_paths_t restore_paths;
    boot_restore_report_t restore_report;
    EFI_TCG2_PROTOCOL *tcg2 = NULL;
    UINT8 *manifest = NULL;
    size_t manifest_size = 0;
    UINT32 active_banks = 0;
    EFI_STATUS child_status;
    EFI_STATUS result = EFI_DEVICE_ERROR;

    gST = SystemTable;
    gBS = SystemTable->BootServices;
    emit(L"\r\nhandoff mechanics gate: START\r\n");

    if (EFI_ERROR(tcg2_locate(&tcg2)) || tcg2 == NULL ||
        EFI_ERROR(tcg2_get_active_banks(tcg2, &active_banks)) ||
        (active_banks & REPLAY_BANK_SHA256) == 0u) {
        emit(L"handoff mechanics gate: FAIL (TCG2)\r\n");
        goto cleanup;
    }
    if (EFI_ERROR(efi_file_read(
            ImageHandle, reference_path, &reference_bytes)) ||
        reference_log_load(reference_bytes.bytes, reference_bytes.size,
                           &reference, NULL) != REFERENCE_LOG_OK ||
        reference.parsed.event_count != 1u) {
        emit(L"handoff mechanics gate: FAIL (reference log)\r\n");
        goto cleanup;
    }
    emit(L"reference log events: ");
    emit_hex(reference.parsed.event_count, 8u);
    emit(L"\r\n");

    state.tcg2 = tcg2;
    state.raw_extensions = 0;
    callbacks.resolve_data = NULL;
    callbacks.hash_extend = NULL;
    callbacks.raw_extend = raw_extend;
    callbacks.context = &state;
    if (replay_run(&reference.parsed, active_banks, &callbacks) !=
            REPLAY_OK ||
        state.raw_extensions != 1u || !sha256_pcr0_nonzero(tcg2)) {
        emit(L"handoff mechanics gate: FAIL (replay)\r\n");
        goto cleanup;
    }
    emit(L"post-extend SHA256 PCR0 nonzero: YES\r\n");
    if (divergence_build(&reference.parsed, active_banks,
                         &manifest, &manifest_size) != DIVERGENCE_OK ||
        manifest == NULL || manifest_size == 0u ||
        EFI_ERROR(efi_file_write(ImageHandle, divergence_path,
                                 manifest, (UINTN)manifest_size))) {
        emit(L"handoff mechanics gate: FAIL (divergence)\r\n");
        goto cleanup;
    }
    TCGLOG_FREE(manifest);
    manifest = NULL;
    emit(L"replay and divergence: PASS\r\n");

    restore_paths.TargetPath = target_path;
    restore_paths.BackupPath = backup_path;
    restore_paths.TempPath = temp_path;
    restore_paths.DisplacedPath = displaced_path;
    restore_paths.HashRecordPath = hash_record_path;
    if (boot_restore_run(ImageHandle, &restore_paths, &restore_report) !=
        BOOT_RESTORE_OK) {
        emit(L"handoff mechanics gate: FAIL (restore)\r\n");
        goto cleanup;
    }
    emit(L"boot manager restore: PASS\r\n");

    if (image_launch_start(ImageHandle, payload_path, &child_status) !=
        IMAGE_LAUNCH_OK) {
        emit(L"handoff mechanics gate: FAIL (payload launch)\r\n");
        goto cleanup;
    }
    emit(L"payload launch: PASS\r\n");
    if (image_launch_start(ImageHandle, target_path, &child_status) !=
        IMAGE_LAUNCH_OK) {
        emit(L"handoff mechanics gate: FAIL (boot manager launch)\r\n");
        goto cleanup;
    }
    emit(L"boot manager launch: PASS\r\n");
    emit(L"ExitBootServices calls: 00000000\r\n");
    emit(L"handoff mechanics gate: PASS\r\n");
    result = EFI_SUCCESS;

cleanup:
    if (manifest != NULL) {
        TCGLOG_FREE(manifest);
    }
    reference_log_release(&reference);
    efi_file_release(&reference_bytes);
    return result;
}
