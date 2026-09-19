/* handoff_child_stub.c - Deterministic child image for the OVMF handoff gate. */
#include "Uefi.h"

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#pragma intrinsic(__outbyte)
#endif

EFI_SYSTEM_TABLE  *gST = NULL;
EFI_BOOT_SERVICES *gBS = NULL;

static void emit(const CHAR16 *text)
{
    while (text != NULL && *text != 0) {
        CHAR16 character = *text++;
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
        if (character <= 0x7fu) {
            __outbyte(0xe9u, (unsigned char)character);
        }
#endif
    }
}

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    (void)ImageHandle;
    gST = SystemTable;
    gBS = SystemTable->BootServices;
#if defined(HANDOFF_PAYLOAD_STUB)
    emit(L"handoff payload: START\r\n");
    emit(L"handoff payload: PASS\r\n");
#elif defined(HANDOFF_BOOTMGR_STUB)
    emit(L"handoff boot manager: START\r\n");
    emit(L"handoff boot manager: PASS\r\n");
#else
#error Define one handoff stub role.
#endif
    return EFI_SUCCESS;
}
