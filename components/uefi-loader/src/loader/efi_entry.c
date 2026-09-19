/* Production loader.efi replay, restore, and handoff entry. */
#include "Uefi.h"
#include "Tcg2Protocol.h"

#include "boot_restore.h"
#include "divergence.h"
#include "efi_file.h"
#include "image_launch.h"
#include "pcr_gate.h"
#include "reference_log.h"
#include "replay.h"
#include "replay_uefi.h"
#include "sha256.h"
#include "tcg2.h"

#ifndef IB_PAYLOAD_REPLAY_INDEPENDENT
#define IB_PAYLOAD_REPLAY_INDEPENDENT 0
#endif

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#pragma intrinsic(__outbyte)
#define IB_QEMU_DEBUGCON_AVAILABLE 1
#else
#define IB_QEMU_DEBUGCON_AVAILABLE 0
#endif

EFI_SYSTEM_TABLE  *gST = NULL;
EFI_BOOT_SERVICES *gBS = NULL;

/* Keep a real DIR64 relocation in optimized EFI builds. Firmware may place
 * loader.efi away from the linker-preferred image base. */
void *g_loader_image_relocation_anchor = &g_loader_image_relocation_anchor;

static const CHAR16 g_reference_path[] = L"\\EFI\\insecure-boot\\tcglog.ib";
static const CHAR16 g_divergence_path[] = L"\\EFI\\insecure-boot\\divergence.ib";
static const CHAR16 g_status_path[] = L"\\EFI\\insecure-boot\\handoff-status.txt";
static const CHAR16 g_payload_path[] = L"\\EFI\\insecure-boot\\payload.efi";
static const CHAR16 g_boot_manager_path[] = L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
static const CHAR16 g_backup_path[] = L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi.bak";
static const CHAR16 g_temp_path[] = L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi.ibnew";
static const CHAR16 g_displaced_path[] = L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi.ibold";
static const CHAR16 g_hash_record_path[] = L"\\EFI\\Microsoft\\Boot\\bootmgfw.sha256";

static const TPM_ALG_ID g_alg_order[4] = {
    TPM_ALG_SHA1, TPM_ALG_SHA256, TPM_ALG_SHA384, TPM_ALG_SHA512
};
static const UINT32 g_alg_bits[4] = {
    EFI_TCG2_BOOT_HASH_ALG_SHA1, EFI_TCG2_BOOT_HASH_ALG_SHA256,
    EFI_TCG2_BOOT_HASH_ALG_SHA384, EFI_TCG2_BOOT_HASH_ALG_SHA512
};

static void debugcon_print(const CHAR16 *text)
{
#if IB_QEMU_DEBUGCON_AVAILABLE
    while (text != NULL && *text != 0) {
        CHAR16 character = *text++;
        if (character <= 0x7fu) {
            __outbyte(0xE9u, (unsigned char)character);
            __outbyte(0x3F8u, (unsigned char)character);
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

static const CHAR16 *alg_name(TPM_ALG_ID algorithm)
{
    switch (algorithm) {
    case TPM_ALG_SHA1:   return L"SHA1";
    case TPM_ALG_SHA256: return L"SHA256";
    case TPM_ALG_SHA384: return L"SHA384";
    case TPM_ALG_SHA512: return L"SHA512";
    default:             return L"unknown";
    }
}

static void con_print_bank_state(const CHAR16 *name, UINT16 digest_size,
                                 const UINT8 *digests)
{
    UINTN pcr;

    con_print(L"  ");
    con_print(name);
    con_print(L" (");
    con_print_hex(digest_size, 2u);
    con_print(L" bytes): PCR[0-7] ");
    for (pcr = 0; pcr < PCR_GATE_PCR_COUNT; pcr++) {
        UINTN byte;
        int zero = 1;
        for (byte = 0; byte < digest_size; byte++) {
            if (digests[pcr * digest_size + byte] != 0u) {
                zero = 0;
                break;
            }
        }
        con_print(zero ? L"0" : L"1");
    }
    con_print(L"\r\n");
}

static pcr_gate_verdict_t run_gate(EFI_TCG2_PROTOCOL *tcg2, UINT32 active,
                                   pcr_gate_bank_t *banks,
                                   UINT32 *bank_count_out)
{
    static UINT8 digests[4][PCR_GATE_PCR_COUNT * PCR_GATE_MAX_DIGEST];
    UINT32 count = 0;
    UINTN i;
    int read_failed = 0;

    for (i = 0; i < 4u; i++) {
        EFI_STATUS status;
        UINT16 digest_size = 0;

        if ((active & g_alg_bits[i]) == 0u) {
            continue;
        }
        status = tcg2_pcr_read_bank(tcg2, g_alg_order[i],
                                    digests[count], &digest_size);
        if (EFI_ERROR(status) || digest_size == 0u) {
            con_print(L"  ERROR: PCR_Read failed for bank ");
            con_print(alg_name(g_alg_order[i]));
            con_print(L"\r\n");
            read_failed = 1;
            continue;
        }
        con_print_bank_state(alg_name(g_alg_order[i]), digest_size,
                             digests[count]);
        banks[count].alg = g_alg_order[i];
        banks[count].digest_size = digest_size;
        banks[count].digests = digests[count];
        count++;
    }
    *bank_count_out = count;
    return read_failed ? PCR_GATE_INVALID : pcr_gate_evaluate(banks, count);
}

static int sha256_pcr0_nonzero(EFI_TCG2_PROTOCOL *tcg2)
{
    UINT8 pcr_values[TCG2_PCR_READ_BYTES];
    UINT16 digest_size = 0;
    UINTN byte;

    if (EFI_ERROR(tcg2_pcr_read_bank(tcg2, TPM_ALG_SHA256,
                                     pcr_values, &digest_size)) ||
        digest_size != 32u) {
        return 0;
    }
    for (byte = 0; byte < digest_size; byte++) {
        if (pcr_values[byte] != 0u) {
            return 1;
        }
    }
    return 0;
}

static int sha256_replay_matches(EFI_TCG2_PROTOCOL *tcg2,
                                 const tcglog_t *log)
{
    UINT8 expected[PCR_GATE_PCR_COUNT][32];
    UINT8 fold_input[64];
    UINT8 actual[TCG2_PCR_READ_BYTES];
    UINT16 actual_size = 0;
    size_t event_index;
    UINTN pcr;
    UINTN byte;

    if (tcg2 == NULL || log == NULL) {
        return 0;
    }
    gBS->SetMem(expected, sizeof(expected), 0);
    for (event_index = 0; event_index < log->event_count; event_index++) {
        const tcglog_event_t *event = &log->events[event_index];
        const UINT8 *digest;
        size_t digest_size = 0;

        if (event->pcr_index >= PCR_GATE_PCR_COUNT) {
            continue;
        }
        digest = tcglog_event_digest(event, TPM_ALG_SHA256, &digest_size);
        if (digest == NULL || digest_size != 32u) {
            return 0;
        }
        for (byte = 0; byte < 32u; byte++) {
            fold_input[byte] = expected[event->pcr_index][byte];
            fold_input[32u + byte] = digest[byte];
        }
        if (sha256_hash(fold_input, sizeof(fold_input),
                        expected[event->pcr_index]) != SHA256_OK) {
            return 0;
        }
    }
    if (EFI_ERROR(tcg2_pcr_read_bank(tcg2, TPM_ALG_SHA256,
                                     actual, &actual_size)) ||
        actual_size != 32u) {
        return 0;
    }
    for (pcr = 0; pcr < PCR_GATE_PCR_COUNT; pcr++) {
        for (byte = 0; byte < 32u; byte++) {
            if (actual[pcr * 32u + byte] != expected[pcr][byte]) {
                return 0;
            }
        }
    }
    return 1;
}

static void write_status(EFI_HANDLE image_handle, const UINT8 *text,
                         UINTN size)
{
    (void)efi_file_write(image_handle, g_status_path, text, size);
}

static EFI_STATUS restore_and_handoff(EFI_HANDLE image_handle,
                                      int replay_succeeded)
{
    static const UINT8 status_restore_failed[] =
        "schema=1\nstate=RESTORE_FAILED\n";
    static const UINT8 status_ready_pass[] =
        "schema=1\nstate=READY_BOOT_MANAGER\nreplay=PASS\n"
        "restore=PASS\npayload=PASS\nexit_boot_services_calls=0\n";
    static const UINT8 status_ready_absent[] =
        "schema=1\nstate=READY_BOOT_MANAGER\nreplay=PASS\n"
        "restore=PASS\npayload=ABSENT\nexit_boot_services_calls=0\n";
    static const UINT8 status_ready_abort[] =
        "schema=1\nstate=READY_BOOT_MANAGER\nreplay=ABORTED\n"
        "restore=PASS\npayload=SKIPPED\nexit_boot_services_calls=0\n";
    static const UINT8 status_ready_abort_payload_pass[] =
        "schema=1\nstate=READY_BOOT_MANAGER\nreplay=ABORTED\n"
        "restore=PASS\npayload=PASS\nexit_boot_services_calls=0\n";
    static const UINT8 status_ready_abort_payload_failed[] =
        "schema=1\nstate=READY_BOOT_MANAGER\nreplay=ABORTED\n"
        "restore=PASS\npayload=FAILED\nexit_boot_services_calls=0\n";
    static const UINT8 status_ready_abort_payload_absent[] =
        "schema=1\nstate=READY_BOOT_MANAGER\nreplay=ABORTED\n"
        "restore=PASS\npayload=ABSENT\nexit_boot_services_calls=0\n";
    static const UINT8 status_payload_failed[] =
        "schema=1\nstate=READY_BOOT_MANAGER\nreplay=PASS\n"
        "restore=PASS\npayload=FAILED\nexit_boot_services_calls=0\n";
    boot_restore_paths_t paths;
    boot_restore_report_t report;
    EFI_STATUS status;
    EFI_STATUS child_status;
    image_launch_status_t launch;
    int payload_present = 0;
    int payload_succeeded = 0;

    status = efi_file_exists(image_handle, g_backup_path);
    if (status == EFI_SUCCESS) {
        paths.TargetPath = g_boot_manager_path;
        paths.BackupPath = g_backup_path;
        paths.TempPath = g_temp_path;
        paths.DisplacedPath = g_displaced_path;
        paths.HashRecordPath = g_hash_record_path;
        if (boot_restore_run(image_handle, &paths, &report) !=
            BOOT_RESTORE_OK) {
            con_print(L"loader production handoff: FAIL (restore)\r\n");
            write_status(image_handle, status_restore_failed,
                         sizeof(status_restore_failed) - 1u);
            return EFI_DEVICE_ERROR;
        }
    } else if (status != EFI_NOT_FOUND ||
               EFI_ERROR(efi_file_exists(image_handle,
                                          g_boot_manager_path))) {
        con_print(L"loader production handoff: FAIL (boot manager missing)\r\n");
        write_status(image_handle, status_restore_failed,
                     sizeof(status_restore_failed) - 1u);
        return EFI_NOT_FOUND;
    }
    con_print(L"boot manager restore: PASS\r\n");

    if (replay_succeeded || IB_PAYLOAD_REPLAY_INDEPENDENT) {
        status = efi_file_exists(image_handle, g_payload_path);
        if (status == EFI_SUCCESS) {
            payload_present = 1;
            launch = image_launch_start_resident(
                image_handle, g_payload_path, &child_status);
            if (launch == IMAGE_LAUNCH_OK) {
                payload_succeeded = 1;
                con_print(L"payload launch: PASS\r\n");
            } else {
                con_print(L"payload launch: FAIL; continuing Windows handoff\r\n");
            }
        } else if (status == EFI_NOT_FOUND) {
            con_print(L"payload launch: ABSENT\r\n");
        } else {
            payload_present = 1;
            con_print(L"payload launch: FAIL (filesystem); continuing Windows handoff\r\n");
        }
    } else {
        con_print(L"payload launch: SKIPPED\r\n");
    }

    if (!replay_succeeded && !IB_PAYLOAD_REPLAY_INDEPENDENT) {
        write_status(image_handle, status_ready_abort,
                     sizeof(status_ready_abort) - 1u);
    } else if (!replay_succeeded && payload_present && payload_succeeded) {
        write_status(image_handle, status_ready_abort_payload_pass,
                     sizeof(status_ready_abort_payload_pass) - 1u);
    } else if (!replay_succeeded && payload_present) {
        write_status(image_handle, status_ready_abort_payload_failed,
                     sizeof(status_ready_abort_payload_failed) - 1u);
    } else if (!replay_succeeded) {
        write_status(image_handle, status_ready_abort_payload_absent,
                     sizeof(status_ready_abort_payload_absent) - 1u);
    } else if (!payload_present) {
        write_status(image_handle, status_ready_absent,
                     sizeof(status_ready_absent) - 1u);
    } else if (payload_succeeded) {
        write_status(image_handle, status_ready_pass,
                     sizeof(status_ready_pass) - 1u);
    } else {
        write_status(image_handle, status_payload_failed,
                     sizeof(status_payload_failed) - 1u);
    }

    launch = image_launch_start(image_handle, g_boot_manager_path,
                                &child_status);
    if (launch != IMAGE_LAUNCH_OK) {
        con_print(L"loader production handoff: FAIL (boot manager launch)\r\n");
        return child_status;
    }
    con_print(L"boot manager launch: PASS\r\n");
    con_print(L"ExitBootServices calls: 00000000\r\n");
    if (replay_succeeded && (!payload_present || payload_succeeded)) {
        con_print(L"loader production handoff: PASS\r\n");
    } else if (!replay_succeeded && !IB_PAYLOAD_REPLAY_INDEPENDENT) {
        con_print(L"loader fail-closed handoff: PASS\r\n");
    } else if (!replay_succeeded && payload_succeeded) {
        con_print(L"loader payload-independent handoff: PASS\r\n");
    } else {
        con_print(L"loader production handoff: FAIL (payload)\r\n");
    }
    return EFI_SUCCESS;
}

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE image_handle,
                          EFI_SYSTEM_TABLE *system_table)
{
    static const UINT32 supported_banks =
        EFI_TCG2_BOOT_HASH_ALG_SHA1 | EFI_TCG2_BOOT_HASH_ALG_SHA256 |
        EFI_TCG2_BOOT_HASH_ALG_SHA384 | EFI_TCG2_BOOT_HASH_ALG_SHA512;
    EFI_TCG2_PROTOCOL *tcg2 = NULL;
    EFI_TCG2_BOOT_SERVICE_CAPABILITY capability;
    efi_file_buffer_t reference_bytes = { NULL, 0 };
    reference_log_t reference = { 0 };
    replay_uefi_context_t replay_context;
    replay_callbacks_t callbacks;
    UINT8 *manifest = NULL;
    size_t manifest_size = 0;
    UINT32 active = 0;
    pcr_gate_bank_t banks[4];
    UINT32 bank_count = 0;
    pcr_gate_verdict_t verdict;
    EFI_STATUS status;
    const CHAR16 *replay_boot_manager_path = g_backup_path;
    int replay_succeeded = 0;

    gST = system_table;
    gBS = system_table->BootServices;
    con_print(L"\r\nloader.efi - insecure-boot-toolkit (P5)\r\n");
    con_print(L"loader production handoff: START\r\n");

    status = tcg2_locate(&tcg2);
    if (EFI_ERROR(status) || tcg2 == NULL) {
        con_print(L"ERROR: EFI_TCG2_PROTOCOL not found - replay impossible\r\n");
        goto handoff;
    }
    con_print(L"EFI_TCG2_PROTOCOL: located\r\n");
    status = tcg2_get_capability(tcg2, &capability);
    if (EFI_ERROR(status) || !capability.TPMPresentFlag) {
        con_print(L"ERROR: TPM unavailable - aborting replay\r\n");
        goto handoff;
    }
    con_print(L"TPM present: YES\r\nnumber of PCR banks: ");
    con_print_hex(capability.NumberOfPcrBanks, 8u);
    con_print(L"\r\n");
    status = tcg2_get_active_banks(tcg2, &active);
    if (EFI_ERROR(status)) {
        con_print(L"ERROR: GetActivePcrBanks failed - aborting replay\r\n");
        goto handoff;
    }
    con_print(L"active PCR banks: 0x");
    con_print_hex(active, 8u);
    con_print(L"\r\n");
    if ((active & ~supported_banks) != 0u) {
        con_print(L"ERROR: unsupported active PCR bank - aborting replay\r\n");
        goto handoff;
    }

    verdict = run_gate(tcg2, active, banks, &bank_count);
    con_print(L"gate verdict (");
    con_print_hex(bank_count, 2u);
    con_print(L" banks): ");
    if (verdict == PCR_GATE_EMPTY) {
        con_print(L"PCR banks empty - replay may proceed\r\n");
    } else if (verdict == PCR_GATE_NONEMPTY) {
        con_print(L"ERROR: PCR banks not empty - aborting replay\r\n");
    } else if (verdict == PCR_GATE_NO_SHA256) {
        con_print(L"ERROR: SHA-256 bank not active - replay impossible\r\n");
    } else {
        con_print(L"ERROR: malformed bank data - aborting replay\r\n");
    }
    if (verdict != PCR_GATE_EMPTY) {
        goto handoff;
    }

    status = efi_file_read(image_handle, g_reference_path, &reference_bytes);
    if (EFI_ERROR(status) ||
        reference_log_load(reference_bytes.bytes, reference_bytes.size,
                           &reference, NULL) != REFERENCE_LOG_OK) {
        con_print(L"loader production handoff: FAIL (reference log)\r\n");
        goto handoff;
    }
    con_print(L"reference log events: ");
    con_print_hex(reference.parsed.event_count, 8u);
    con_print(L"\r\n");
    status = efi_file_exists(image_handle, g_backup_path);
    if (status == EFI_NOT_FOUND) {
        status = efi_file_exists(image_handle, g_boot_manager_path);
        if (EFI_ERROR(status)) {
            con_print(L"loader production handoff: FAIL (boot manager source)\r\n");
            goto handoff;
        }
        replay_boot_manager_path = g_boot_manager_path;
    } else if (EFI_ERROR(status)) {
        con_print(L"loader production handoff: FAIL (backup source)\r\n");
        goto handoff;
    }
    /* A previous attempt must not leave evidence that can be mistaken for
     * the current replay.  Keep the path absent until the complete replay
     * has succeeded. */
    status = efi_file_delete(image_handle, g_divergence_path);
    if (status != EFI_SUCCESS && status != EFI_NOT_FOUND) {
        con_print(L"loader production handoff: FAIL (divergence cleanup)\r\n");
        goto handoff;
    }

    status = replay_uefi_init(&replay_context, image_handle, tcg2,
                              replay_boot_manager_path, &callbacks);
    if (EFI_ERROR(status)) {
        con_print(L"loader production handoff: FAIL (replay adapter)\r\n");
        goto handoff;
    }
    if (replay_run(&reference.parsed, active, &callbacks) != REPLAY_OK) {
        replay_uefi_release(&replay_context);
        con_print(L"loader production handoff: FAIL (replay)\r\n");
        goto handoff;
    }
    replay_uefi_release(&replay_context);
    if (!sha256_pcr0_nonzero(tcg2)) {
        con_print(L"loader production handoff: FAIL (PCR unchanged)\r\n");
        goto handoff;
    }
    if (!sha256_replay_matches(tcg2, &reference.parsed)) {
        con_print(L"loader production handoff: FAIL (PCR mismatch)\r\n");
        goto handoff;
    }
    /* Persist the complete raw-event manifest only after replay_run and the
     * post-extend check both prove that the replay completed. */
    if (divergence_build(&reference.parsed, active,
                         &manifest, &manifest_size) != DIVERGENCE_OK ||
        manifest == NULL || manifest_size == 0u ||
        EFI_ERROR(efi_file_write(image_handle, g_divergence_path,
                                 manifest, (UINTN)manifest_size))) {
        /* efi_file_write() replaces by delete-and-create; if its write or
         * flush failed, remove any partial replacement before handoff so it
         * cannot be mistaken for complete replay evidence. */
        (void)efi_file_delete(image_handle, g_divergence_path);
        con_print(L"loader production handoff: FAIL (divergence)\r\n");
        goto handoff;
    }
    TCGLOG_FREE(manifest);
    manifest = NULL;
    con_print(L"post-extend SHA256 PCR0 nonzero: YES\r\n");
    con_print(L"post-replay SHA256 PCR[0-7] exact: YES\r\n");
    con_print(L"replay and divergence: PASS\r\n");
    replay_succeeded = 1;

handoff:
    if (manifest != NULL) {
        TCGLOG_FREE(manifest);
    }
    reference_log_release(&reference);
    efi_file_release(&reference_bytes);
    return restore_and_handoff(image_handle, replay_succeeded);
}
