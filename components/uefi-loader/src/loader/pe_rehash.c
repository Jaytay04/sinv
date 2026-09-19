/* pe_rehash.c - Firmware-backed PE/COFF digest verification. */
#include "pe_rehash.h"

#include "efi_file.h"
#include "tcglog.h"

#define PE_REHASH_MAX_DIGESTS 5u
#define PE_REHASH_EVENT_DATA_SIZE 16u

typedef struct {
    UINT16 alg;
    UINT16 size;
    const UINT8 *bytes;
} pe_digest_view_t;

typedef struct {
    UINT32 pcr_index;
    UINT32 event_type;
    UINT32 digest_count;
    pe_digest_view_t digests[PE_REHASH_MAX_DIGESTS];
} pe_event_view_t;

typedef struct {
    UINT32 Size;
    EFI_TCG2_EVENT_HEADER Header;
    UINT8 Event[PE_REHASH_EVENT_DATA_SIZE];
} pe_rehash_event_t;

static UINT16 read_u16_le(const UINT8 *p)
{
    return (UINT16)((UINT16)p[0] | ((UINT16)p[1] << 8));
}

static UINT32 read_u32_le(const UINT8 *p)
{
    return (UINT32)p[0] | ((UINT32)p[1] << 8) |
           ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24);
}

static UINT16 digest_size(UINT16 alg)
{
    switch (alg) {
    case TCGLOG_ALG_SHA1:   return 20u;
    case TCGLOG_ALG_SHA256: return 32u;
    case TCGLOG_ALG_SHA384: return 48u;
    case TCGLOG_ALG_SHA512: return 64u;
    case TPM_ALG_SM3_256:   return 32u;
    default:                return 0u;
    }
}

static UINT32 bank_bit(UINT16 alg)
{
    switch (alg) {
    case TCGLOG_ALG_SHA1:   return EFI_TCG2_BOOT_HASH_ALG_SHA1;
    case TCGLOG_ALG_SHA256: return EFI_TCG2_BOOT_HASH_ALG_SHA256;
    case TCGLOG_ALG_SHA384: return EFI_TCG2_BOOT_HASH_ALG_SHA384;
    case TCGLOG_ALG_SHA512: return EFI_TCG2_BOOT_HASH_ALG_SHA512;
    case TPM_ALG_SM3_256:   return EFI_TCG2_BOOT_HASH_ALG_SM3_256;
    default:                return 0u;
    }
}

static int parse_event(const UINT8 *entry, pe_event_view_t *out)
{
    const UINT8 *p;
    UINT32 i;

    if (entry == NULL || out == NULL) {
        return 0;
    }
    p = entry;
    out->pcr_index = read_u32_le(p);
    p += 4;
    out->event_type = read_u32_le(p);
    p += 4;
    out->digest_count = read_u32_le(p);
    p += 4;
    if (out->pcr_index > 23u || out->digest_count == 0u ||
        out->digest_count > PE_REHASH_MAX_DIGESTS) {
        return 0;
    }
    for (i = 0; i < out->digest_count; i++) {
        UINT16 alg = read_u16_le(p);
        UINT16 size = digest_size(alg);
        if (size == 0u) {
            return 0;
        }
        p += 2;
        out->digests[i].alg = alg;
        out->digests[i].size = size;
        out->digests[i].bytes = p;
        p += size;
    }
    return 1;
}

static const pe_digest_view_t *find_digest(const pe_event_view_t *event,
                                           UINT16 alg)
{
    UINT32 i;
    for (i = 0; i < event->digest_count; i++) {
        if (event->digests[i].alg == alg) {
            return &event->digests[i];
        }
    }
    return NULL;
}

static int bytes_equal(const UINT8 *a, const UINT8 *b, UINTN size)
{
    UINTN i;
    for (i = 0; i < size; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

pe_rehash_status_t pe_rehash_verify_last_image(
    EFI_HANDLE ImageHandle, EFI_TCG2_PROTOCOL *Tcg2, const CHAR16 *Path,
    UINT32 ActiveBanks, UINTN *FileSize, UINT32 *VerifiedDigestCount)
{
    static const UINT16 algorithms[5] = {
        TCGLOG_ALG_SHA1, TCGLOG_ALG_SHA256, TCGLOG_ALG_SHA384,
        TCGLOG_ALG_SHA512, TPM_ALG_SM3_256
    };
    static const UINT8 event_data[] = "PE rehash verify";
    EFI_PHYSICAL_ADDRESS log_start = 0;
    EFI_PHYSICAL_ADDRESS last_before = 0;
    EFI_PHYSICAL_ADDRESS last_after = 0;
    BOOLEAN truncated = FALSE;
    pe_event_view_t recorded;
    pe_event_view_t rehashed;
    pe_rehash_event_t event;
    efi_file_buffer_t file;
    EFI_STATUS st;
    UINT32 verified = 0;
    UINTN i;

    if (ImageHandle == NULL || Tcg2 == NULL || Path == NULL ||
        FileSize == NULL || VerifiedDigestCount == NULL ||
        (ActiveBanks & EFI_TCG2_BOOT_HASH_ALG_SHA256) == 0u) {
        return PE_REHASH_ERR_ARGUMENT;
    }
    *FileSize = 0;
    *VerifiedDigestCount = 0;

    st = Tcg2->GetEventLog(Tcg2, EFI_TCG2_EVENT_LOG_FORMAT_TCG_2,
                           &log_start, &last_before, &truncated);
    if (EFI_ERROR(st) || log_start == 0u || last_before == 0u || truncated) {
        return PE_REHASH_ERR_EVENT_LOG;
    }
    if (!parse_event((const UINT8 *)(UINTN)last_before, &recorded) ||
        recorded.event_type != TCGLOG_EVT_EFI_BOOT_SERVICES_APPLICATION) {
        return PE_REHASH_ERR_RECORDED_EVENT;
    }

    file.bytes = NULL;
    file.size = 0;
    st = efi_file_read(ImageHandle, Path, &file);
    if (EFI_ERROR(st)) {
        return PE_REHASH_ERR_FILE_READ;
    }

    event.Size = (UINT32)(offsetof(pe_rehash_event_t, Event) +
                          PE_REHASH_EVENT_DATA_SIZE);
    event.Header.HeaderSize = (UINT32)sizeof(EFI_TCG2_EVENT_HEADER);
    event.Header.HeaderVersion = 1u;
    event.Header.PCRIndex = recorded.pcr_index;
    event.Header.EventType = recorded.event_type;
    for (i = 0; i < PE_REHASH_EVENT_DATA_SIZE; i++) {
        event.Event[i] = event_data[i];
    }

    st = Tcg2->HashLogExtendEvent(
        Tcg2, EFI_TCG2_PE_COFF_IMAGE,
        (EFI_PHYSICAL_ADDRESS)(UINTN)file.bytes, (UINT64)file.size,
        (EFI_TCG2_EVENT *)&event);
    *FileSize = file.size;
    efi_file_release(&file);
    if (EFI_ERROR(st)) {
        return PE_REHASH_ERR_HASH_EXTEND;
    }

    truncated = FALSE;
    st = Tcg2->GetEventLog(Tcg2, EFI_TCG2_EVENT_LOG_FORMAT_TCG_2,
                           &log_start, &last_after, &truncated);
    if (EFI_ERROR(st) || last_after == 0u || last_after == last_before ||
        truncated || !parse_event((const UINT8 *)(UINTN)last_after, &rehashed) ||
        rehashed.pcr_index != recorded.pcr_index ||
        rehashed.event_type != recorded.event_type) {
        return PE_REHASH_ERR_REHASH_EVENT;
    }

    for (i = 0; i < 5; i++) {
        const pe_digest_view_t *a;
        const pe_digest_view_t *b;
        UINT32 bit = bank_bit(algorithms[i]);
        if ((ActiveBanks & bit) == 0u) {
            continue;
        }
        a = find_digest(&recorded, algorithms[i]);
        b = find_digest(&rehashed, algorithms[i]);
        if (a == NULL || b == NULL || a->size != b->size ||
            !bytes_equal(a->bytes, b->bytes, a->size)) {
            return PE_REHASH_ERR_DIGEST_MISMATCH;
        }
        verified++;
    }
    *VerifiedDigestCount = verified;
    return verified == 0u ? PE_REHASH_ERR_DIGEST_MISMATCH : PE_REHASH_OK;
}
