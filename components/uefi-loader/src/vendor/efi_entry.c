/*
 * efi_entry.c — EFI glue and smoke-application entry point.
 *
 * Defines the freestanding application entry (EfiMain) that MSVC links via
 * /SUBSYSTEM:EFI_APPLICATION /ENTRY:EfiMain. The tcglog UEFI allocator hooks
 * live in efi_alloc.c (shared with loader.efi); this file only populates
 * gST/gBS and exercises the dual-target link.
 *
 * This is the P2 dual-target proof: the same src/common/tcglog.c compiles and
 * links unmodified against this UEFI target.
 */
#include "Uefi.h"
#include "Tcg2Protocol.h"

#define TCGLOG_UEFI 1
#include "tcglog.h"

EFI_SYSTEM_TABLE  *gST = NULL;
EFI_BOOT_SERVICES *gBS = NULL;

/* ------------------------------------------------------------------------- */
/* Application entry                                                          */
/* ------------------------------------------------------------------------- */
EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    tcglog_t log;
    size_t err_off = 0;
    tcglog_status st;

    (void)ImageHandle;

    gST = SystemTable;
    gBS = SystemTable->BootServices;

    /* Dual-target link smoke: exercise parse/free against an empty (invalid)
     * buffer so the shared library resolves in the EFI image. Full scenario
     * coverage lives in the host test harness. */
    st = tcglog_parse((const uint8_t *)"", 0, &log, &err_off);
    (void)st;
    tcglog_free(&log);

    return EFI_SUCCESS;
}
