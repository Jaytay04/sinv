/*
 * pe_rehash_efi_smoke.c - OVMF gate for pre-relocation PE/COFF replay.
 *
 * A real Microsoft bootmgfw.efi is read from the FAT volume and passed to
 * LoadImage from its raw source buffer, causing stock OVMF to record the
 * genuine PE/COFF image digest. pe_rehash_verify_last_image then rereads the
 * on-disk bytes, measures them with EFI_TCG2_PE_COFF_IMAGE, and compares every
 * active-bank digest with that recorded image-load event.
 */
#include "Uefi.h"
#include "Tcg2Protocol.h"

#include "efi_file.h"
#include "pe_rehash.h"

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#pragma intrinsic(__outbyte)
#define IB_QEMU_DEBUGCON_AVAILABLE 1
#else
#define IB_QEMU_DEBUGCON_AVAILABLE 0
#endif

EFI_SYSTEM_TABLE  *gST = NULL;
EFI_BOOT_SERVICES *gBS = NULL;

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

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    static const CHAR16 bootmgfw_path[] =
        L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
    EFI_GUID tcg2_guid = EFI_TCG2_PROTOCOL_GUID;
    EFI_TCG2_PROTOCOL *tcg2 = NULL;
    EFI_TCG2_BOOT_SERVICE_CAPABILITY capability;
    EFI_PHYSICAL_ADDRESS log_start = 0;
    EFI_PHYSICAL_ADDRESS log_last = 0;
    BOOLEAN log_truncated = FALSE;
    efi_file_buffer_t source;
    EFI_DEVICE_PATH_PROTOCOL *bootmgfw_device_path = NULL;
    EFI_HANDLE child = NULL;
    EFI_STATUS st;
    pe_rehash_status_t rehash_status;
    UINT32 active = 0;
    UINT32 verified = 0;
    UINTN file_size = 0;

    gST = SystemTable;
    gBS = SystemTable->BootServices;
    con_print(L"\r\nPE pre-relocation rehash gate: START\r\n");

    st = gBS->LocateProtocol(&tcg2_guid, NULL, (VOID **)&tcg2);
    if (EFI_ERROR(st) || tcg2 == NULL ||
        EFI_ERROR(tcg2->GetActivePcrBanks(tcg2, &active))) {
        con_print(L"PE pre-relocation rehash gate: FAIL (TCG2)\r\n");
        return EFI_SUCCESS;
    }
    con_print(L"active PCR banks: 0x");
    con_print_hex(active, 8);
    con_print(L"\r\n");

    source.bytes = NULL;
    source.size = 0;
    st = efi_file_read(ImageHandle, bootmgfw_path, &source);
    if (EFI_ERROR(st)) {
        con_print(L"PE pre-relocation rehash gate: FAIL (file read)\r\n");
        return EFI_SUCCESS;
    }

    st = efi_file_build_device_path(
        ImageHandle, bootmgfw_path, &bootmgfw_device_path);
    if (EFI_ERROR(st)) {
        efi_file_release(&source);
        con_print(L"PE pre-relocation rehash gate: FAIL (device path)\r\n");
        return EFI_SUCCESS;
    }

    /* LoadImage receives the same raw file bytes that the verifier will read
     * again. Stock OVMF records this image measurement before relocation. */
    st = gBS->LoadImage(FALSE, ImageHandle, bootmgfw_device_path, source.bytes,
                        source.size, &child);
    efi_file_device_path_release(&bootmgfw_device_path);
    efi_file_release(&source);
    if (EFI_ERROR(st) || child == NULL) {
        con_print(L"PE pre-relocation rehash gate: FAIL (LoadImage)\r\n");
        return EFI_SUCCESS;
    }

    capability.SupportedEventLogs = 0;
    capability.Size = (UINT8)sizeof(capability);
    st = tcg2->GetCapability(tcg2, &capability);
    con_print(L"GetCapability status: 0x");
    con_print_hex((UINT64)st, 16);
    con_print(L"\r\nsupported event logs: 0x");
    con_print_hex((UINT64)capability.SupportedEventLogs, 8);
    con_print(L"\r\n");

    st = tcg2->GetEventLog(tcg2, EFI_TCG2_EVENT_LOG_FORMAT_TCG_2,
                           &log_start, &log_last, &log_truncated);
    con_print(L"GetEventLog(TCG2) status: 0x");
    con_print_hex((UINT64)st, 16);
    con_print(L"\r\nevent log start: 0x");
    con_print_hex((UINT64)log_start, 16);
    con_print(L"\r\nevent log last: 0x");
    con_print_hex((UINT64)log_last, 16);
    con_print(L"\r\nevent log truncated: 0x");
    con_print_hex((UINT64)log_truncated, 2);
    con_print(L"\r\n");

    rehash_status = pe_rehash_verify_last_image(
        ImageHandle, tcg2, bootmgfw_path, active, &file_size, &verified);
    gBS->UnloadImage(child);
    if (rehash_status != PE_REHASH_OK) {
        con_print(L"rehash status: 0x");
        con_print_hex((UINT64)rehash_status, 8);
        con_print(L"\r\nPE pre-relocation rehash gate: FAIL (digest compare)\r\n");
        return EFI_SUCCESS;
    }

    con_print(L"source: \\EFI\\Microsoft\\Boot\\bootmgfw.efi\r\n");
    con_print(L"pre-relocation bytes: 0x");
    con_print_hex((UINT64)file_size, 16);
    con_print(L"\r\nverified active digests: 0x");
    con_print_hex((UINT64)verified, 8);
    con_print(L"\r\nHashLogExtendEvent flags: PE_COFF_IMAGE\r\n");
    con_print(L"PE pre-relocation rehash gate: PASS\r\n");
    return EFI_SUCCESS;
}
