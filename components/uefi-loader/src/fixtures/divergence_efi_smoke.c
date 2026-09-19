/* divergence_efi_smoke.c - OVMF gate for raw-extend manifest persistence. */
#include "Uefi.h"

#include "divergence.h"
#include "efi_file.h"
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
} divergence_gate_state_t;

static void debugcon_print(const CHAR16 *text)
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

static void con_print(const CHAR16 *text)
{
    debugcon_print(text);
    if (gST != NULL && gST->ConOut != NULL) {
        gST->ConOut->OutputString(gST->ConOut, (CHAR16 *)text);
    }
}

static void con_print_hex(UINT64 value, UINTN nibbles)
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
    con_print(buffer);
}

static replay_status_t raw_extend(const tcglog_event_t *event,
                                  const replay_digest_t *digests,
                                  uint32_t digest_count, void *context)
{
    divergence_gate_state_t *state = (divergence_gate_state_t *)context;
    tcg2_digest_value_t values[REPLAY_MAX_DIGESTS];
    uint32_t i;
    EFI_STATUS status;

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

static UINT32 active_supported_bank_count(UINT32 active)
{
    static const UINT32 banks[4] = {
        REPLAY_BANK_SHA1, REPLAY_BANK_SHA256,
        REPLAY_BANK_SHA384, REPLAY_BANK_SHA512
    };
    UINT32 count = 0;
    UINTN i;

    for (i = 0; i < 4u; i++) {
        if ((active & banks[i]) != 0u) {
            count++;
        }
    }
    return count;
}

static int record_digests_match(const tcglog_event_t *event,
                                const divergence_record_view_t *record,
                                UINT32 active)
{
    static const UINT16 algorithms[4] = {
        TCGLOG_ALG_SHA1, TCGLOG_ALG_SHA256,
        TCGLOG_ALG_SHA384, TCGLOG_ALG_SHA512
    };
    static const UINT32 banks[4] = {
        REPLAY_BANK_SHA1, REPLAY_BANK_SHA256,
        REPLAY_BANK_SHA384, REPLAY_BANK_SHA512
    };
    UINTN i;

    for (i = 0; i < 4u; i++) {
        const UINT8 *expected;
        const UINT8 *actual;
        size_t expected_size = 0;
        size_t actual_size = 0;
        size_t byte;

        if ((active & banks[i]) == 0u) {
            continue;
        }
        expected = tcglog_event_digest(event, algorithms[i], &expected_size);
        actual = divergence_record_digest(record, algorithms[i],
                                          &actual_size);
        if (expected == NULL || actual == NULL ||
            expected_size != actual_size) {
            return 0;
        }
        for (byte = 0; byte < expected_size; byte++) {
            if (expected[byte] != actual[byte]) {
                return 0;
            }
        }
    }
    return 1;
}

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    static const CHAR16 manifest_path[] =
        L"\\EFI\\insecure-boot\\divergence.ib";
    static UINT8 algorithms[] = {
        0x04, 0x00, 0x14, 0x00,
        0x0b, 0x00, 0x20, 0x00,
        0x0c, 0x00, 0x30, 0x00,
        0x0d, 0x00, 0x40, 0x00
    };
    static UINT8 digests[2u + 20u + 2u + 32u + 2u + 48u + 2u + 64u];
    static UINT8 blob_data[] = { 0x46, 0x57, 0x42, 0x4c, 0x4f, 0x42 };
    static tcglog_header_t header;
    static tcglog_event_t event;
    static tcglog_t log;
    replay_callbacks_t callbacks;
    divergence_gate_state_t state;
    divergence_manifest_view_t manifest;
    divergence_record_view_t record;
    efi_file_buffer_t persisted;
    EFI_TCG2_PROTOCOL *tcg2 = NULL;
    EFI_STATUS efi_status;
    replay_status_t replay_status;
    divergence_status_t divergence_status;
    UINT8 *manifest_bytes = NULL;
    UINT8 pcr_values[TCG2_PCR_READ_BYTES];
    size_t manifest_size = 0;
    UINT32 active = 0;
    UINT32 expected_digests;
    UINT16 pcr_digest_size = 0;
    int pcr0_nonzero = 0;
    UINTN position = 0;
    UINTN bank;
    UINTN byte;

    gST = SystemTable;
    gBS = SystemTable->BootServices;
    con_print(L"\r\ndivergence manifest gate: START\r\n");

    efi_status = tcg2_locate(&tcg2);
    if (EFI_ERROR(efi_status) || tcg2 == NULL ||
        EFI_ERROR(tcg2_get_active_banks(tcg2, &active)) ||
        (active & REPLAY_BANK_SHA256) == 0u) {
        con_print(L"divergence manifest gate: FAIL (TCG2)\r\n");
        return EFI_SUCCESS;
    }
    con_print(L"active PCR banks: 0x");
    con_print_hex(active, 8);
    con_print(L"\r\n");

    for (bank = 0; bank < 4u; bank++) {
        static const UINT16 algorithm_ids[4] = {
            TCGLOG_ALG_SHA1, TCGLOG_ALG_SHA256,
            TCGLOG_ALG_SHA384, TCGLOG_ALG_SHA512
        };
        static const UINT16 sizes[4] = { 20u, 32u, 48u, 64u };
        digests[position++] = (UINT8)(algorithm_ids[bank] & 0xffu);
        digests[position++] = (UINT8)(algorithm_ids[bank] >> 8);
        for (byte = 0; byte < sizes[bank]; byte++) {
            digests[position++] = (UINT8)(0x20u + bank + byte);
        }
    }

    header.alg_count = 4;
    header.algs = algorithms;
    event.pcr_index = 0;
    event.event_type = TCGLOG_EVT_EFI_PLATFORM_FIRMWARE_BLOB;
    event.digest_count = 4;
    event.digests = digests;
    event.event_size = (UINT32)sizeof(blob_data);
    event.event_data = blob_data;
    event.header = &header;
    log.header = header;
    log.event_count = 1;
    log.events = &event;

    state.tcg2 = tcg2;
    state.raw_extensions = 0;
    callbacks.resolve_data = NULL;
    callbacks.hash_extend = NULL;
    callbacks.raw_extend = raw_extend;
    callbacks.context = &state;
    replay_status = replay_run(&log, active, &callbacks);
    if (replay_status != REPLAY_OK || state.raw_extensions != 1u) {
        con_print(L"divergence manifest gate: FAIL (raw extend)\r\n");
        return EFI_SUCCESS;
    }
    efi_status = tcg2_pcr_read_bank(
        tcg2, TPM_ALG_SHA256, pcr_values, &pcr_digest_size);
    if (EFI_ERROR(efi_status) || pcr_digest_size != 32u) {
        con_print(L"divergence manifest gate: FAIL (PCR read)\r\n");
        return EFI_SUCCESS;
    }
    for (byte = 0; byte < pcr_digest_size; byte++) {
        if (pcr_values[byte] != 0u) {
            pcr0_nonzero = 1;
            break;
        }
    }
    if (!pcr0_nonzero) {
        con_print(L"divergence manifest gate: FAIL (PCR unchanged)\r\n");
        return EFI_SUCCESS;
    }

    divergence_status = divergence_build(
        &log, active, &manifest_bytes, &manifest_size);
    if (divergence_status != DIVERGENCE_OK || manifest_bytes == NULL ||
        manifest_size == 0u) {
        con_print(L"divergence manifest gate: FAIL (build)\r\n");
        return EFI_SUCCESS;
    }
    efi_status = efi_file_write(ImageHandle, manifest_path, manifest_bytes,
                                (UINTN)manifest_size);
    TCGLOG_FREE(manifest_bytes);
    manifest_bytes = NULL;
    if (EFI_ERROR(efi_status)) {
        con_print(L"divergence manifest gate: FAIL (write)\r\n");
        return EFI_SUCCESS;
    }

    persisted.bytes = NULL;
    persisted.size = 0;
    efi_status = efi_file_read(ImageHandle, manifest_path, &persisted);
    if (EFI_ERROR(efi_status)) {
        con_print(L"divergence manifest gate: FAIL (reread)\r\n");
        return EFI_SUCCESS;
    }
    divergence_status = divergence_read(
        persisted.bytes, persisted.size, &manifest);
    if (divergence_status != DIVERGENCE_OK || manifest.record_count != 1u ||
        divergence_record_at(&manifest, 0, &record) != DIVERGENCE_OK) {
        efi_file_release(&persisted);
        con_print(L"divergence manifest gate: FAIL (parse)\r\n");
        return EFI_SUCCESS;
    }
    expected_digests = active_supported_bank_count(active);
    if (record.event_index != 0u || record.pcr_index != 0u ||
        record.event_type != TCGLOG_EVT_EFI_PLATFORM_FIRMWARE_BLOB ||
        record.digest_count != expected_digests ||
        !record_digests_match(&event, &record, active)) {
        efi_file_release(&persisted);
        con_print(L"divergence manifest gate: FAIL (record)\r\n");
        return EFI_SUCCESS;
    }

    con_print(L"synthetic events: 00000001\r\n");
    con_print(L"PCR extensions performed: 00000001\r\n");
    con_print(L"post-extend SHA256 PCR0 nonzero: YES\r\n");
    con_print(L"manifest records: 00000001\r\n");
    con_print(L"record event index: 00000000\r\n");
    con_print(L"record PCR: 00000000\r\n");
    con_print(L"record type: 80000008\r\n");
    con_print(L"record active digests: ");
    con_print_hex(record.digest_count, 8);
    con_print(L"\r\nmanifest bytes: 0x");
    con_print_hex((UINT64)persisted.size, 8);
    con_print(L"\r\nmanifest path: \\EFI\\insecure-boot\\divergence.ib\r\n");
    efi_file_release(&persisted);
    con_print(L"divergence manifest gate: PASS\r\n");
    return EFI_SUCCESS;
}
