/* Production loader.efi replay, restore, and handoff entry. */
#include "Uefi.h"
#include "Tcg2Protocol.h"

#include "boot_restore.h"
#include "divergence.h"
#include "efi_file.h"
#include "efi_storage_probe.h"
#include "handoff_diagnostic.h"
#include "image_launch.h"
#include "pcr_gate.h"
#include "payload_diagnostic.h"
#include "reference_log.h"
#include "replay.h"
#include "replay_uefi.h"
#include "sha256.h"
#include "shim_inventory.h"
#include "shim_chain.h"
#include "tcg2.h"

#ifndef IB_PAYLOAD_REPLAY_INDEPENDENT
#define IB_PAYLOAD_REPLAY_INDEPENDENT 0
#endif

#ifndef IB_QEMU_DEBUG_PORTS
#define IB_QEMU_DEBUG_PORTS 0
#endif

#ifndef IB_FIRMWARE_STORAGE_PROBE
#define IB_FIRMWARE_STORAGE_PROBE 0
#endif

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#pragma intrinsic(__outbyte)
#if defined(_M_X64)
#pragma intrinsic(__cpuidex, __readeflags)
#endif
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
static const CHAR16 g_payload_diagnostic_path[] =
    L"\\EFI\\insecure-boot\\payload-diagnostic.txt";
static const CHAR16 g_inventory_path[] = L"\\EFI\\insecure-boot\\inventory.efi";
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
#if IB_QEMU_DEBUGCON_AVAILABLE && IB_QEMU_DEBUG_PORTS
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

static UINT8 g_handoff_diagnostic[HANDOFF_DIAGNOSTIC_MAX_SIZE];
static UINTN g_handoff_diagnostic_size;

static void con_print_ascii_bytes(const UINT8 *bytes, UINTN size)
{
    CHAR16 line[128];
    UINTN input, used = 0u;
    for (input = 0u; input < size; input++) {
        if (used + 3u >= sizeof(line) / sizeof(line[0])) {
            line[used] = 0; con_print(line); used = 0u;
        }
        if (bytes[input] == (UINT8)'\n') { line[used++] = (CHAR16)'\r'; }
        line[used++] = (CHAR16)bytes[input];
    }
    line[used] = 0; con_print(line);
}

static void con_print_digest(const UINT8 digest[32])
{
    UINTN index;
    for (index = 0u; index < 32u; index++) { con_print_hex(digest[index], 2u); }
}

#if IB_FIRMWARE_STORAGE_PROBE
#include "efi_storage_fault_report.h"

static efi_storage_probe_state_t g_storage_probe;
static int g_storage_probe_started;
static int g_storage_probe_failure_sampled;
static efi_storage_fault_report_t g_storage_fault_report;
static int g_storage_fault_report_captured;

static int storage_fault_selector_is_bounded(UINT32 Selector)
{
    UINT32 unit_selector;

    if (Selector <= 2u) {
        return 1;
    }
    if (Selector < HV_STORAGE_DIAGNOSTIC_UNIT_BASE) {
        return 0;
    }
    unit_selector = Selector - HV_STORAGE_DIAGNOSTIC_UNIT_BASE;
    return unit_selector < HV_STORAGE_DIAGNOSTIC_MAX_UNITS *
                           HV_STORAGE_DIAGNOSTIC_UNIT_STRIDE;
}

static int storage_fault_cpuid_query(void *Context, UINT32 Selector,
                                     efi_storage_fault_registers_t *Registers)
{
    (void)Context;
    if (Registers == NULL || !storage_fault_selector_is_bounded(Selector)) {
        return 0;
    }
#if defined(_MSC_VER) && defined(_M_X64)
    {
        int values[4];
        __cpuidex(values, (int)HV_STORAGE_DIAGNOSTIC_LEAF, (int)Selector);
        Registers->Eax = (UINT32)values[0];
        Registers->Ebx = (UINT32)values[1];
        Registers->Ecx = (UINT32)values[2];
        Registers->Edx = (UINT32)values[3];
        return 1;
    }
#else
    return 0;
#endif
}

static void con_print_storage_fault_report(
    const efi_storage_fault_report_t *report, BOOLEAN compact)
{
    UINT32 index;
    int outer_unavailable;

    if (report == NULL) {
        con_print(L"storage_fault status=unavailable\r\n");
        return;
    }
    switch (report->Status) {
    case EFI_STORAGE_FAULT_REPORT_NOT_CAPTURED:
        con_print(L"storage_fault status=not_captured\r\n");
        return;
    case EFI_STORAGE_FAULT_REPORT_UNAVAILABLE:
        con_print(L"storage_fault status=unavailable\r\n");
        return;
    case EFI_STORAGE_FAULT_REPORT_UNSUPPORTED:
        con_print(L"storage_fault status=unsupported\r\n");
        return;
    case EFI_STORAGE_FAULT_REPORT_MALFORMED:
        con_print(L"storage_fault status=malformed\r\n");
        return;
    case EFI_STORAGE_FAULT_REPORT_AVAILABLE:
    default:
        break;
    }

    outer_unavailable =
        (report->Metadata.Edx & HV_STORAGE_DIAGNOSTIC_OUTER_UNAVAILABLE) != 0u;
    con_print(L"storage_fault status=supported capture=");
    con_print(outer_unavailable ? L"unavailable" : L"attempted");
    con_print(L" global_flags=0x"); con_print_hex(report->Metadata.Edx, 8u);
    con_print(L" units=0x"); con_print_hex(report->UnitCount, 2u);
    con_print(L"\r\n");

    if (compact) {
        con_print(L"storage_fault cached_target bdf=0x");
        con_print_hex(report->StorageIdentity.Eax, 8u);
        con_print(L" ids=0x"); con_print_hex(report->StorageIdentity.Ebx, 8u);
        con_print(L" command_pair=0x");
        con_print_hex(report->StorageIdentity.Edx, 8u);
        con_print(L" live_command=0x"); con_print_hex(report->LiveState.Eax, 8u);
        con_print(L" target_flags=0x"); con_print_hex(report->LiveState.Ecx, 8u);
        con_print(L"\r\n");
        for (index = 0u; index < report->UnitCount; index++) {
            const efi_storage_fault_unit_t *unit = &report->Units[index];
            con_print(L"storage_fault cached_unit=0x");
            con_print_hex(index, 2u);
            con_print(L" fsts_base=0x");
            con_print_hex(unit->StatusAndFaults.Eax, 8u);
            con_print(L" fsts_failure=0x");
            con_print_hex(unit->StatusAndFaults.Ebx, 8u);
            con_print(L" gsts_base=0x");
            con_print_hex(unit->StatusAndFaults.Ecx, 8u);
            con_print(L" gsts_failure=0x");
            con_print_hex(unit->StatusAndFaults.Edx, 8u);
            con_print(L" valid_flags=0x");
            con_print_hex(unit->CapAndValidity.Ecx, 8u);
            con_print(L"\r\n");
        }
        return;
    }

    con_print(L"storage_fault metadata signature=0x");
    con_print_hex(report->Metadata.Eax, 8u);
    con_print(L" version=0x"); con_print_hex(report->Metadata.Ebx, 8u);
    con_print(L" unit_count=0x"); con_print_hex(report->Metadata.Ecx, 8u);
    con_print(L" flags=0x"); con_print_hex(report->Metadata.Edx, 8u);
    con_print(L"\r\n");
    con_print(L"storage_fault target bdf=0x");
    con_print_hex(report->StorageIdentity.Eax, 8u);
    con_print(L" vendor_device=0x");
    con_print_hex(report->StorageIdentity.Ebx, 8u);
    con_print(L" class_revision=0x");
    con_print_hex(report->StorageIdentity.Ecx, 8u);
    con_print(L" command_pair=0x");
    con_print_hex(report->StorageIdentity.Edx, 8u);
    con_print(L"\r\n");
    con_print(L"storage_fault live command=0x");
    con_print_hex(report->LiveState.Eax, 8u);
    con_print(L" vendor_device=0x"); con_print_hex(report->LiveState.Ebx, 8u);
    con_print(L" flags=0x"); con_print_hex(report->LiveState.Ecx, 8u);
    con_print(L" reserved=0x"); con_print_hex(report->LiveState.Edx, 8u);
    con_print(L"\r\n");
    for (index = 0u; index < report->UnitCount; index++) {
        const efi_storage_fault_unit_t *unit = &report->Units[index];
        con_print(L"storage_fault unit=0x"); con_print_hex(index, 2u);
        con_print(L" fsts_baseline=0x");
        con_print_hex(unit->StatusAndFaults.Eax, 8u);
        con_print(L" fsts_failure=0x");
        con_print_hex(unit->StatusAndFaults.Ebx, 8u);
        con_print(L" gsts_baseline=0x");
        con_print_hex(unit->StatusAndFaults.Ecx, 8u);
        con_print(L" gsts_failure=0x");
        con_print_hex(unit->StatusAndFaults.Edx, 8u);
        con_print(L"\r\nstorage_fault unit=0x"); con_print_hex(index, 2u);
        con_print(L" failure_frcd_word0=0x");
        con_print_hex(unit->FailureFrcd.Eax, 8u);
        con_print(L" word1=0x"); con_print_hex(unit->FailureFrcd.Ebx, 8u);
        con_print(L" word2=0x"); con_print_hex(unit->FailureFrcd.Ecx, 8u);
        con_print(L" word3=0x"); con_print_hex(unit->FailureFrcd.Edx, 8u);
        con_print(L"\r\nstorage_fault unit=0x"); con_print_hex(index, 2u);
        con_print(L" cap_low=0x");
        con_print_hex(unit->CapAndValidity.Eax, 8u);
        con_print(L" cap_high=0x"); con_print_hex(unit->CapAndValidity.Ebx, 8u);
        con_print(L" valid_flags=0x");
        con_print_hex(unit->CapAndValidity.Ecx, 8u);
        con_print(L" fro_fri=0x");
        con_print_hex(unit->CapAndValidity.Edx, 8u);
        con_print(L"\r\nstorage_fault unit=0x");
        con_print_hex(index, 2u);
        con_print(L" baseline_frcd_word0=0x");
        con_print_hex(unit->BaselineFrcd.Eax, 8u);
        con_print(L" word1=0x"); con_print_hex(unit->BaselineFrcd.Ebx, 8u);
        con_print(L" word2=0x"); con_print_hex(unit->BaselineFrcd.Ecx, 8u);
        con_print(L" word3=0x"); con_print_hex(unit->BaselineFrcd.Edx, 8u);
        con_print(L"\r\n");
    }
}

static void storage_fault_report_capture_once(void)
{
    if (g_storage_fault_report_captured) {
        return;
    }
    g_storage_fault_report_captured = 1;
    efi_storage_fault_report_capture(&g_storage_fault_report,
                                     storage_fault_cpuid_query, NULL);
    con_print_storage_fault_report(&g_storage_fault_report, FALSE);
}

static void con_print_storage_probe(const efi_storage_probe_result_t *result)
{
    con_print(L"storage probe[");
    con_print(efi_storage_probe_stage_name(result->Stage));
    con_print(L"] reason=");
    con_print(efi_storage_probe_reason_name(result->Reason));
    con_print(L" status=0x"); con_print_hex(result->EffectiveStatus, 16u);
    con_print(L"\r\nloaded=0x"); con_print_hex(result->HandleLoadedImageStatus, 16u);
    con_print(L" block_io=0x"); con_print_hex(result->HandleBlockIoStatus, 16u);
    con_print(L" allocate=0x"); con_print_hex(result->AllocateStatus, 16u);
    con_print(L"\r\nread_attempted="); con_print_hex(result->ReadAttempted, 1u);
    con_print(L" read=0x"); con_print_hex(result->ReadStatus, 16u);
    con_print(L" hash_valid="); con_print_hex(result->DigestValid, 1u);
    con_print(L"\r\nmedia_valid="); con_print_hex(result->MediaSnapshotValid, 1u);
    con_print(L" media_id=0x"); con_print_hex(result->MediaId, 8u);
    con_print(L" block_size=0x"); con_print_hex(result->BlockSize, 8u);
    con_print(L" io_align=0x"); con_print_hex(result->IoAlign, 8u);
    con_print(L"\r\nlast_lba=0x"); con_print_hex(result->LastBlock, 16u);
    con_print(L" present="); con_print_hex(result->MediaPresent, 1u);
    con_print(L" partition="); con_print_hex(result->LogicalPartition, 1u);
    con_print(L"\r\nidentity_compared="); con_print_hex(result->MediaIdentityCompared, 1u);
    con_print(L" identity_match="); con_print_hex(result->MediaIdentityMatch, 1u);
    con_print(L" content_compared="); con_print_hex(result->ContentCompared, 1u);
    con_print(L" content_match="); con_print_hex(result->ContentMatch, 1u);
    if (result->DigestValid) {
        con_print(L"\r\nblock_sha256="); con_print_digest(result->Digest);
    }
    con_print(L"\r\n");
}

static void con_print_storage_cpu_state(void)
{
#if defined(_MSC_VER) && defined(_M_X64)
    int registers[4];
    UINT64 flags = __readeflags();
    /* This existing read-only health leaf is safe even when no payload is
     * resident. Raw register values are evidence, not an activation verdict. */
    __cpuidex(registers, 0x48565031, 0);
    con_print(L"storage cpu: rflags=0x"); con_print_hex(flags, 16u);
    con_print(L" health_eax=0x"); con_print_hex((UINT32)registers[0], 8u);
    con_print(L" health_ebx=0x"); con_print_hex((UINT32)registers[1], 8u);
    con_print(L"\r\nhealth_ecx=0x"); con_print_hex((UINT32)registers[2], 8u);
    con_print(L" health_edx=0x"); con_print_hex((UINT32)registers[3], 8u);
    con_print(L"\r\n");
#endif
}

static void storage_probe_before_payload(EFI_HANDLE image_handle)
{
    efi_storage_probe_result_t result;
    EFI_STATUS status = efi_storage_probe_prepare(
        image_handle, &g_storage_probe, &result);
    con_print_storage_probe(&result);
    g_storage_probe_failure_sampled = 0;
    g_storage_probe_started = status == EFI_SUCCESS;
    if (g_storage_probe_started) {
        con_print_storage_cpu_state();
        con_print(L"storage probe[baseline] read_begin\r\n");
        (void)efi_storage_probe_sample(
            &g_storage_probe, EFI_STORAGE_PROBE_STAGE_BASELINE, &result);
        con_print_storage_probe(&result);
    }
}

static void storage_probe_after_volume_failure(const efi_file_io_trace_t *trace)
{
    efi_storage_probe_result_t result;
    if (trace == NULL || g_storage_probe_failure_sampled ||
        trace->Operation != EFI_FILE_IO_OPEN_VOLUME ||
        !EFI_ERROR(trace->RawStatus)) {
        return;
    }
    /* Preserve the first traced volume error before this independent block read.
     * Never retry OpenVolume or create an extra diagnostic file here. */
    g_storage_probe_failure_sampled = 1;
    storage_fault_report_capture_once();
    if (!g_storage_probe_started) {
        return;
    }
    con_print_storage_cpu_state();
    con_print(L"storage probe[post_filesystem_failure] read_begin\r\n");
    (void)efi_storage_probe_sample(&g_storage_probe,
        EFI_STORAGE_PROBE_STAGE_POST_FILESYSTEM_FAILURE, &result);
    con_print_storage_probe(&result);
    efi_storage_probe_release(&g_storage_probe);
}
#endif

static void con_print_file_io_failure(const CHAR16 *label,
                                      const efi_file_io_trace_t *trace)
{
    if (trace == NULL || trace->Operation == EFI_FILE_IO_NONE) {
        return;
    }
    con_print(L"filesystem failure[");
    con_print(label);
    con_print(L"] op=");
    con_print(efi_file_io_operation_name(trace->Operation));
    con_print(L"\r\nraw=0x");
    con_print_hex((UINT64)trace->RawStatus, 16u);
    con_print(L" effective=0x");
    con_print_hex((UINT64)trace->EffectiveStatus, 16u);
    con_print(L"\r\nsize_in=0x");
    con_print_hex((UINT64)trace->RequestedSize, 16u);
    con_print(L" size_out=0x");
    con_print_hex((UINT64)trace->ObservedSize, 16u);
    con_print(L"\r\n");
#if IB_FIRMWARE_STORAGE_PROBE
    storage_probe_after_volume_failure(trace);
#endif
}

static void write_diagnostic_verified(EFI_HANDLE image_handle,
    const CHAR16 *path, const CHAR16 *label, const UINT8 *bytes, UINTN size)
{
    efi_file_write_verification_t verification;
    efi_file_io_trace_t io_trace;
    EFI_STATUS result = efi_file_write_verified_traced(
        image_handle, path, bytes, size, &verification, &io_trace);
    if (EFI_ERROR(result) || !verification.Match) {
        con_print_file_io_failure(label, &io_trace);
    }
    con_print(label);
    con_print(EFI_ERROR(verification.WriteStatus) ? L" write: FAIL\r\n" : L" write: PASS\r\n");
    con_print(label);
    if (!verification.ReadAttempted) {
        con_print(L" readback: NOT_RUN\r\n");
    } else {
        con_print(!EFI_ERROR(result) && verification.Match ?
            L" readback: PASS sha256=" : L" readback: FAIL sha256=");
        con_print_digest(verification.ObservedSha256); con_print(L"\r\n");
    }
    if (EFI_ERROR(result) || !verification.Match) {
        con_print(L"diagnostic result=0x"); con_print_hex((UINT64)result, 16u);
        con_print(L" write=0x"); con_print_hex((UINT64)verification.WriteStatus, 16u);
        con_print(L" flush=0x"); con_print_hex((UINT64)verification.FlushStatus, 16u);
        con_print(L" file_close=0x"); con_print_hex((UINT64)verification.FileCloseStatus, 16u);
        con_print(L" root_close=0x"); con_print_hex((UINT64)verification.RootCloseStatus, 16u);
        con_print(L" read=0x"); con_print_hex((UINT64)verification.ReadStatus, 16u);
        con_print(L"\r\nexpected_size=0x"); con_print_hex((UINT64)verification.ExpectedSize, 16u);
        con_print(L" observed_size=0x"); con_print_hex((UINT64)verification.ObservedSize, 16u);
        con_print(L" expected_sha256="); con_print_digest(verification.ExpectedSha256);
        con_print(L"\r\n");
    }
}

static void write_status(EFI_HANDLE image_handle, const UINT8 *text, UINTN size)
{
    UINTN index;
    g_handoff_diagnostic_size = 0u;
    if (size > sizeof(g_handoff_diagnostic)) {
        con_print(L"handoff status format: FAIL\r\n"); return;
    }
    for (index = 0u; index < size; index++) { g_handoff_diagnostic[index] = text[index]; }
    g_handoff_diagnostic_size = size;
    write_diagnostic_verified(image_handle, g_status_path, L"handoff status", text, size);
}

typedef struct {
    EFI_HANDLE ImageHandle;
    const char *Target;
    const CHAR16 *ReadLabel;
    efi_file_io_trace_t *ReadTrace;
} handoff_observer_context_t;

static void observe_handoff(void *context, const char *phase,
                             BOOLEAN completed, EFI_STATUS status)
{
    handoff_observer_context_t *observer = (handoff_observer_context_t *)context;
    UINTN prior = g_handoff_diagnostic_size;
    const char *shim_phase = NULL;
    EFI_STATUS shim_status = EFI_NOT_STARTED;
    if (completed && observer->ReadLabel != NULL &&
        phase[0] == 'r' && phase[1] == 'e' &&
        phase[2] == 'a' && phase[3] == 'd' && phase[4] == 0 &&
        EFI_ERROR(status)) {
        /* This must precede the observer's next status-file write, which
         * could otherwise obscure where the source read actually failed. */
        con_print_file_io_failure(observer->ReadLabel, observer->ReadTrace);
    }
    if (completed && phase[0] == 'l') {
        shim_chain_get_last_load_trace(&shim_phase, &shim_status);
    }
    if (!handoff_diagnostic_append(g_handoff_diagnostic,
            sizeof(g_handoff_diagnostic), &g_handoff_diagnostic_size,
            observer->Target, phase, completed, status, shim_phase, shim_status)) {
        con_print(L"handoff trace append: FAIL\r\n"); return;
    }
    /* The before-call record remains useful if firmware never returns. */
    con_print_ascii_bytes(g_handoff_diagnostic + prior, g_handoff_diagnostic_size - prior);
    write_diagnostic_verified(observer->ImageHandle, g_status_path, L"handoff status",
        g_handoff_diagnostic, g_handoff_diagnostic_size);
#if IB_FIRMWARE_STORAGE_PROBE
    if (!completed && observer->ReadLabel != NULL &&
        phase[0] == 's' && phase[1] == 't' && phase[2] == 'a' &&
        phase[3] == 'r' && phase[4] == 't' && phase[5] == 0) {
        /* Windows may not return from StartImage. Release the diagnostic
         * buffer before that boundary, after the last pre-start status write. */
        efi_storage_probe_release(&g_storage_probe);
        g_storage_probe_started = 0;
    }
#endif
}

static payload_diagnostic_launch_t payload_diagnostic_launch_from_result(
    image_launch_status_t Launch)
{
    switch (Launch) {
    case IMAGE_LAUNCH_OK:                 return PAYLOAD_DIAG_OK;
    case IMAGE_LAUNCH_ERR_ARGUMENT:       return PAYLOAD_DIAG_ARGUMENT;
    case IMAGE_LAUNCH_ERR_NOT_FOUND:      return PAYLOAD_DIAG_NOT_FOUND;
    case IMAGE_LAUNCH_ERR_READ:           return PAYLOAD_DIAG_READ;
    case IMAGE_LAUNCH_ERR_DEVICE_PATH:    return PAYLOAD_DIAG_DEVICE_PATH;
    case IMAGE_LAUNCH_ERR_LOAD:           return PAYLOAD_DIAG_LOAD;
    case IMAGE_LAUNCH_ERR_IMAGE_FORMAT:   return PAYLOAD_DIAG_IMAGE_FORMAT;
    case IMAGE_LAUNCH_ERR_PROTOCOL:       return PAYLOAD_DIAG_PROTOCOL;
    case IMAGE_LAUNCH_ERR_START:          return PAYLOAD_DIAG_START;
    default:                              return PAYLOAD_DIAG_ARGUMENT;
    }
}

static void report_payload_diagnostic(EFI_HANDLE image_handle,
                                      const payload_diagnostic_t *Diagnostic,
                                      payload_diagnostic_startup_state_t
                                          StartupState,
                                      const hv_boot_diagnostic_t *Startup)
{
    UINT8 bytes[PAYLOAD_DIAGNOSTIC_MAX_SIZE];
    UINTN size = 0;

    if (!payload_diagnostic_format_with_startup(
            Diagnostic, StartupState, Startup, bytes, sizeof(bytes), &size)) {
        con_print(L"payload diagnostic format: FAIL\r\n");
        return;
    }
    con_print_ascii_bytes(bytes, size);

    write_diagnostic_verified(image_handle, g_payload_diagnostic_path,
        L"payload diagnostic", bytes, size);
}

static payload_diagnostic_startup_state_t
payload_diagnostic_startup_state_from_launch(
    image_launch_diagnostic_state_t State)
{
    switch (State) {
    case IMAGE_LAUNCH_DIAGNOSTIC_UNAVAILABLE:
        return PAYLOAD_DIAG_STARTUP_UNAVAILABLE;
    case IMAGE_LAUNCH_DIAGNOSTIC_INVALID:
        return PAYLOAD_DIAG_STARTUP_INVALID;
    case IMAGE_LAUNCH_DIAGNOSTIC_VALID:
        return PAYLOAD_DIAG_STARTUP_VALID;
    case IMAGE_LAUNCH_DIAGNOSTIC_NONE:
    default:
        return PAYLOAD_DIAG_STARTUP_NONE;
    }
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
    image_launch_diagnostic_state_t launch_diagnostic_state =
        IMAGE_LAUNCH_DIAGNOSTIC_NONE;
    payload_diagnostic_startup_state_t startup_state =
        PAYLOAD_DIAG_STARTUP_NONE;
    hv_boot_diagnostic_t startup_record;
    payload_diagnostic_t payload_diagnostic;
    const char *shim_load_phase = "not_applicable";
    EFI_STATUS shim_load_status = EFI_NOT_STARTED;
    int payload_present = 0;
    int payload_succeeded = 0;
    handoff_observer_context_t boot_observer;
    efi_file_io_trace_t boot_manager_read_trace;

    g_handoff_diagnostic_size = 0u;
    boot_observer.ImageHandle = image_handle;
    boot_observer.Target = "boot_manager";
    boot_observer.ReadLabel = L"boot manager read";
    boot_observer.ReadTrace = &boot_manager_read_trace;
    efi_file_io_trace_initialize(&boot_manager_read_trace);

    payload_diagnostic.payload_launch = PAYLOAD_DIAG_SKIPPED;
    payload_diagnostic.payload_status = EFI_NOT_STARTED;
    payload_diagnostic.shim_load_phase = shim_load_phase;
    payload_diagnostic.shim_load_status = shim_load_status;
    hv_boot_diagnostic_init(&startup_record);

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
#if IB_FIRMWARE_STORAGE_PROBE
            storage_probe_before_payload(image_handle);
#endif
            launch = image_launch_start_resident_diagnostic(
                image_handle, g_payload_path, &child_status,
                &startup_record, &launch_diagnostic_state);
            startup_state = payload_diagnostic_startup_state_from_launch(
                launch_diagnostic_state);
            shim_chain_get_last_load_trace(&shim_load_phase,
                                           &shim_load_status);
            payload_diagnostic.payload_launch =
                payload_diagnostic_launch_from_result(launch);
            payload_diagnostic.payload_status = child_status;
            payload_diagnostic.shim_load_phase = shim_load_phase;
            payload_diagnostic.shim_load_status = shim_load_status;
            if (launch == IMAGE_LAUNCH_OK) {
                payload_succeeded = 1;
                con_print(L"payload launch: PASS\r\n");
            } else {
                con_print(L"payload launch: FAIL; continuing Windows handoff\r\n");
            }
        } else if (status == EFI_NOT_FOUND) {
            payload_diagnostic.payload_launch = PAYLOAD_DIAG_ABSENT;
            payload_diagnostic.payload_status = status;
            con_print(L"payload launch: ABSENT\r\n");
        } else {
            payload_present = 1;
            payload_diagnostic.payload_launch = PAYLOAD_DIAG_PRESENCE_ERROR;
            payload_diagnostic.payload_status = status;
            con_print(L"payload launch: FAIL (filesystem); continuing Windows handoff\r\n");
        }
    } else {
        con_print(L"payload launch: SKIPPED\r\n");
    }

    report_payload_diagnostic(image_handle, &payload_diagnostic,
                              startup_state, &startup_record);

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

{
    EFI_STATUS inventory_status;
    EFI_STATUS inventory_child_status;
    image_launch_status_t inventory_launch;
    handoff_observer_context_t inventory_observer;

    /* Optional, read-only hardware-inventory payload. Its presence never
     * changes the loader verdict; a failure is console-only. */
    inventory_status = efi_file_exists(image_handle, g_inventory_path);
    if (inventory_status == EFI_SUCCESS) {
        inventory_observer.ImageHandle = image_handle;
        inventory_observer.Target = "inventory";
        inventory_observer.ReadLabel = NULL;
        inventory_observer.ReadTrace = NULL;
        inventory_launch = image_launch_start_observed(
            image_handle, g_inventory_path, &inventory_child_status,
            observe_handoff, &inventory_observer);
        observe_handoff(&inventory_observer, "return", TRUE, inventory_child_status);
        if (inventory_launch == IMAGE_LAUNCH_OK) {
            con_print(L"inventory collector: PASS\r\n");
        } else {
            con_print(L"inventory collector: FAIL (non-fatal; \r\n"
                      L"continuing Windows handoff)\r\n");
        }
    }
}

    launch = image_launch_start_observed_traced(
        image_handle, g_boot_manager_path, &child_status, observe_handoff,
        &boot_observer, &boot_manager_read_trace);
    observe_handoff(&boot_observer, "return", TRUE, child_status);
#if IB_FIRMWARE_STORAGE_PROBE
    efi_storage_probe_release(&g_storage_probe);
#endif
    if (launch != IMAGE_LAUNCH_OK) {
        con_print(L"loader production handoff: FAIL (boot manager launch)\r\n");
#if IB_FIRMWARE_STORAGE_PROBE
        if (boot_manager_read_trace.Operation != EFI_FILE_IO_NONE &&
            EFI_ERROR(boot_manager_read_trace.RawStatus)) {
            con_print_storage_fault_report(&g_storage_fault_report, TRUE);
        }
#endif
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
    EFI_GUID loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image = NULL;
    BOOLEAN diagnostic_handled = FALSE;

    /* Classify the loaded image path before any output or production work.
     * shim may reuse its own image handle while loading the second stage, so
     * the current image's FilePath is the only identity used here. */
    if (image_handle == NULL || system_table == NULL ||
        system_table->BootServices == NULL ||
        system_table->BootServices->HandleProtocol == NULL) {
        return EFI_ACCESS_DENIED;
    }
    gST = system_table;
    gBS = system_table->BootServices;
    status = gBS->HandleProtocol(image_handle, &loaded_image_guid,
                                 (VOID **)&loaded_image);
    if (EFI_ERROR(status) || loaded_image == NULL) {
        return EFI_ACCESS_DENIED;
    }
    status = shim_inventory_route(image_handle, system_table,
                                  loaded_image->FilePath,
                                  &diagnostic_handled);
    if (diagnostic_handled || EFI_ERROR(status)) {
        return status;
    }

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

    /* Phase 5: Delete BootNext variable to remove NVRAM traces.
     * This must happen before ExitBootServices so the firmware
     * runtime services are still available. */
    {
        EFI_STATUS nvram_status;
        static const EFI_GUID global_guid =
            { 0x8BE4DF61, 0x93CA, 0x11D2,
              { 0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C } };
        nvram_status = gST->RuntimeServices->SetVariable(
            (CHAR16 *)L"BootNext",
            (EFI_GUID *)&global_guid,
            EFI_VARIABLE_NON_VOLATILE |
            EFI_VARIABLE_BOOTSERVICE_ACCESS |
            EFI_VARIABLE_RUNTIME_ACCESS,
            0, NULL);
        if (EFI_ERROR(nvram_status) && nvram_status != EFI_NOT_FOUND) {
            con_print(L"WARNING: BootNext cleanup failed: ");
            con_print_hex(nvram_status, 8u);
            con_print(L"\r\n");
        } else {
            con_print(L"BootNext cleanup: PASS\r\n");
        }
    }
    return restore_and_handoff(image_handle, replay_succeeded);
}
