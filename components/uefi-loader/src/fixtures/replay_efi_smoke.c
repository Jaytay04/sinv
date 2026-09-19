/*
 * replay_efi_smoke.c - Freestanding UEFI dry-run for the P5 replay core.
 *
 * This application deliberately does not extend PCRs.  It obtains the real
 * active-bank bitmap from EFI_TCG2_PROTOCOL, feeds a three-event synthetic
 * parsed log through replay_run(), and verifies the exact H/R/H callback
 * sequence plus per-bank digest selection.  Host unit tests cover failures;
 * this target proves the same core and callback ABI execute under OVMF.
 */
#include "Uefi.h"
#include "Tcg2Protocol.h"

#include "replay.h"

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
    CHAR16 sequence[4];
    UINT32 sequence_count;
    UINT32 expected_digest_count;
    UINT32 raw_digest_count;
    UINT32 null_data_calls;
    UINT32 active_bank_mask;
} dry_run_state_t;

static void debugcon_print(const CHAR16 *s)
{
#if IB_QEMU_DEBUGCON_AVAILABLE
    while (s != NULL && *s != 0) {
        CHAR16 c = *s++;
        if (c <= 0x7fu) {
            __outbyte(0xe9u, (unsigned char)c);
        }
    }
#else
    (void)s;
#endif
}

static void con_print(const CHAR16 *s)
{
    debugcon_print(s);
    if (gST != NULL && gST->ConOut != NULL) {
        gST->ConOut->OutputString(gST->ConOut, (CHAR16 *)s);
    }
}

static void con_print_hex(UINT64 value, UINTN nibbles)
{
    static const CHAR16 digits[] =
        { L'0', L'1', L'2', L'3', L'4', L'5', L'6', L'7',
          L'8', L'9', L'A', L'B', L'C', L'D', L'E', L'F' };
    CHAR16 buffer[17];
    UINTN i;

    if (nibbles > 16) {
        nibbles = 16;
    }
    for (i = 0; i < nibbles; i++) {
        UINTN shift = (nibbles - 1u - i) * 4u;
        buffer[i] = digits[(value >> shift) & 0x0fu];
    }
    buffer[nibbles] = 0;
    con_print(buffer);
}

static replay_status_t resolve_data(const tcglog_event_t *event,
                                    replay_data_t *out, void *context)
{
    (void)context;
    out->bytes = event->event_data;
    out->size = event->event_size;
    return REPLAY_OK;
}

static replay_status_t record_hash(const tcglog_event_t *event,
                                   const uint8_t *data, size_t data_size,
                                   void *context)
{
    dry_run_state_t *state = (dry_run_state_t *)context;

    if (data == NULL || data_size == 0u) {
        state->null_data_calls++;
        return REPLAY_ERR_CALLBACK;
    }
    if (data != event->event_data || data_size != event->event_size ||
        state->sequence_count >= 3u) {
        return REPLAY_ERR_CALLBACK;
    }
    state->sequence[state->sequence_count++] = L'H';
    return REPLAY_OK;
}

static replay_status_t record_raw(const tcglog_event_t *event,
                                  const replay_digest_t *digests,
                                  uint32_t digest_count, void *context)
{
    static const UINT16 bank_algs[4] = {
        TCGLOG_ALG_SHA1, TCGLOG_ALG_SHA256,
        TCGLOG_ALG_SHA384, TCGLOG_ALG_SHA512
    };
    static const UINT32 bank_bits[4] = {
        REPLAY_BANK_SHA1, REPLAY_BANK_SHA256,
        REPLAY_BANK_SHA384, REPLAY_BANK_SHA512
    };
    dry_run_state_t *state = (dry_run_state_t *)context;
    UINT32 i;
    UINT32 bank = 0;

    (void)event;
    if (digests == NULL || digest_count != state->expected_digest_count ||
        state->sequence_count >= 3u) {
        return REPLAY_ERR_CALLBACK;
    }
    for (i = 0; i < digest_count; i++) {
        while (bank < 4u &&
               (state->active_bank_mask & bank_bits[bank]) == 0u) {
            bank++;
        }
        if (bank >= 4u || digests[i].alg != bank_algs[bank] ||
            digests[i].bytes == NULL ||
            digests[i].size == 0u) {
            return REPLAY_ERR_CALLBACK;
        }
        bank++;
    }
    state->raw_digest_count = digest_count;
    state->sequence[state->sequence_count++] = L'R';
    return REPLAY_OK;
}

static UINT32 recognized_bank_count(UINT32 active)
{
    static const UINT32 bits[4] = {
        REPLAY_BANK_SHA1, REPLAY_BANK_SHA256,
        REPLAY_BANK_SHA384, REPLAY_BANK_SHA512
    };
    UINT32 count = 0;
    UINTN i;

    for (i = 0; i < 4; i++) {
        if ((active & bits[i]) != 0u) {
            count++;
        }
    }
    return count;
}

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    static UINT8 algs[] = {
        0x04, 0x00, 0x14, 0x00,
        0x0b, 0x00, 0x20, 0x00,
        0x0c, 0x00, 0x30, 0x00,
        0x0d, 0x00, 0x40, 0x00
    };
    static UINT8 digests[2u + 20u + 2u + 32u + 2u + 48u + 2u + 64u];
    static UINT8 action_data[] = { 'S', 'y', 'n', 't', 'h', 'A', 'c', 't' };
    static UINT8 raw_data[] = { 0x52, 0x41, 0x57 };
    static UINT8 gpt_data[] = { 0x47, 0x50, 0x54 };
    static tcglog_header_t header;
    static tcglog_event_t events[3];
    static tcglog_t log;
    static dry_run_state_t state;
    replay_callbacks_t callbacks;
    EFI_TCG2_PROTOCOL *tcg2 = NULL;
    EFI_GUID tcg2_guid = EFI_TCG2_PROTOCOL_GUID;
    EFI_STATUS efi_status;
    replay_status_t replay_status;
    UINT32 active = 0;
    UINTN digest_pos = 0;
    UINTN bank;
    UINTN byte;

    (void)ImageHandle;
    gST = SystemTable;
    gBS = SystemTable->BootServices;

    con_print(L"\r\nreplay EFI dry-run: START\r\n");
    efi_status = gBS->LocateProtocol(&tcg2_guid, NULL, (VOID **)&tcg2);
    if (EFI_ERROR(efi_status) || tcg2 == NULL) {
        con_print(L"replay EFI dry-run: FAIL (TCG2 unavailable)\r\n");
        return EFI_SUCCESS;
    }
    efi_status = tcg2->GetActivePcrBanks(tcg2, &active);
    if (EFI_ERROR(efi_status) || (active & REPLAY_BANK_SHA256) == 0u) {
        con_print(L"replay EFI dry-run: FAIL (SHA-256 inactive)\r\n");
        return EFI_SUCCESS;
    }
    con_print(L"active PCR banks: 0x");
    con_print_hex(active, 8);
    con_print(L"\r\n");

    for (bank = 0; bank < 4; bank++) {
        static const UINT16 alg_ids[4] = {
            TCGLOG_ALG_SHA1, TCGLOG_ALG_SHA256,
            TCGLOG_ALG_SHA384, TCGLOG_ALG_SHA512
        };
        static const UINT16 sizes[4] = { 20u, 32u, 48u, 64u };
        digests[digest_pos++] = (UINT8)(alg_ids[bank] & 0xffu);
        digests[digest_pos++] = (UINT8)(alg_ids[bank] >> 8);
        for (byte = 0; byte < sizes[bank]; byte++) {
            digests[digest_pos++] = (UINT8)(1u + bank + byte);
        }
    }

    header.alg_count = 4;
    header.algs = algs;
    events[0].pcr_index = 7;
    events[0].event_type = TCGLOG_EVT_EFI_ACTION;
    events[0].digest_count = 4;
    events[0].digests = digests;
    events[0].event_size = (UINT32)sizeof(action_data);
    events[0].event_data = action_data;
    events[0].header = &header;
    events[1] = events[0];
    events[1].pcr_index = 0;
    events[1].event_type = TCGLOG_EVT_EFI_PLATFORM_FIRMWARE_BLOB;
    events[1].event_size = (UINT32)sizeof(raw_data);
    events[1].event_data = raw_data;
    events[2] = events[0];
    events[2].pcr_index = 5;
    events[2].event_type = TCGLOG_EVT_EFI_GPT_EVENT;
    events[2].event_size = (UINT32)sizeof(gpt_data);
    events[2].event_data = gpt_data;
    log.header = header;
    log.event_count = 3;
    log.events = events;

    state.expected_digest_count = recognized_bank_count(active);
    state.active_bank_mask = active;
    callbacks.resolve_data = resolve_data;
    callbacks.hash_extend = record_hash;
    callbacks.raw_extend = record_raw;
    callbacks.context = &state;

    replay_status = replay_run(&log, active, &callbacks);
    if (replay_status != REPLAY_OK || state.sequence_count != 3u ||
        state.sequence[0] != L'H' || state.sequence[1] != L'R' ||
        state.sequence[2] != L'H' || state.null_data_calls != 0u ||
        state.raw_digest_count != state.expected_digest_count) {
        con_print(L"replay EFI dry-run: FAIL (callback contract)\r\n");
        return EFI_SUCCESS;
    }

    state.sequence[3] = 0;
    con_print(L"synthetic events: 00000003\r\n");
    con_print(L"callback sequence: ");
    con_print(state.sequence);
    con_print(L"\r\nraw active digests: ");
    con_print_hex(state.raw_digest_count, 8);
    con_print(L"\r\nnull DataToHash calls: 00000000\r\n");
    con_print(L"PCR extensions performed: 00000000\r\n");
    con_print(L"replay EFI dry-run: PASS\r\n");
    return EFI_SUCCESS;
}
