#ifndef EFI_SMBIOS_H
#define EFI_SMBIOS_H

#include "Uefi.h"
#include "smbios_table.h"

#define EFI_SMBIOS_ERROR_COMPROMISED EFI_ENCODE_ERROR(33)

#ifdef __cplusplus
extern "C" {
#endif

#define EFI_SMBIOS_MAX_ENTRY_POINTS 2u

typedef enum efi_smbios_context_state {
    EfiSmbiosStateEmpty = 0,
    EfiSmbiosStatePrepared = 1,
    EfiSmbiosStatePublished = 2,
    EfiSmbiosStateRolledBack = 3
} EFI_SMBIOS_CONTEXT_STATE;

typedef struct efi_smbios_entry_context {
    EFI_GUID Guid;
    SM_EP_KIND Kind;
    UINTN ConfigurationIndex;
    VOID *OriginalEntryPoint;
    VOID *OriginalTable;
    VOID *NewEntryPoint;
    VOID *NewTable;
    EFI_PHYSICAL_ADDRESS AllocationBase;
    UINTN AllocationPages;
    UINTN AllocationBytes;
    UINTN OriginalEntryPointBytes;
    UINTN NewEntryPointBytes;
    UINTN OriginalTableBytes;
    UINTN NewTableBytes;
    SM_EP_INFO OriginalEntryInfo;
    SM_VALUES BeforeValues;
    SM_VALUES AfterValues;
    BOOLEAN Published;
} EFI_SMBIOS_ENTRY_CONTEXT;

typedef struct efi_smbios_context {
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_BOOT_SERVICES *BootServices;
    UINT32 FieldMask;
    UINTN EntryCount;
    EFI_SMBIOS_ENTRY_CONTEXT Entries[EFI_SMBIOS_MAX_ENTRY_POINTS];
    EFI_SMBIOS_CONTEXT_STATE State;
    EFI_STATUS LastStatus;
} EFI_SMBIOS_CONTEXT;

/* Read-only discovery, validation, source guard checks, and page preparation.
 * Context must be zero-initialized and must not own live pages from a prior call. */
EFI_STATUS EFIAPI EfiSmbiosPrepare(
    EFI_SYSTEM_TABLE *SystemTable,
    UINT32 FieldMask,
    const SM_VALUES *LockedOriginals,
    const SM_VALUES *Replacements,
    EFI_SMBIOS_CONTEXT *Context);

/* Publish all prepared entry points; a partial failure attempts full rollback. */
EFI_STATUS EFIAPI EfiSmbiosPublish(EFI_SMBIOS_CONTEXT *Context);

/* Rediscover firmware configuration pointers and parse their current tables.
 * Verification failure attempts rollback before returning. */
EFI_STATUS EFIAPI EfiSmbiosReadback(EFI_SMBIOS_CONTEXT *Context);

/* Restore original configuration pointers. Allocated pages are retained if a
 * firmware pointer may still reference them. */
EFI_STATUS EFIAPI EfiSmbiosRollback(EFI_SMBIOS_CONTEXT *Context);

/* Free prepared or safely rolled-back pages; pages still published are kept. */
EFI_STATUS EFIAPI EfiSmbiosFree(EFI_SMBIOS_CONTEXT *Context);

#ifdef __cplusplus
}
#endif

#endif
