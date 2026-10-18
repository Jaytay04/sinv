#ifndef HYPERSINV_EFI_SMBIOS_DIAGNOSTIC_H
#define HYPERSINV_EFI_SMBIOS_DIAGNOSTIC_H

#include <stdint.h>

#include "Uefi.h"
#include "smbios_table.h"

#define EFI_SMBIOS_DIAGNOSTIC_ENTRY_NONE UINT64_MAX

typedef enum efi_smbios_prepare_check {
    EfiSmbiosPrepareCheckNone = 0,
    EfiSmbiosPrepareCheckArguments,
    EfiSmbiosPrepareCheckFieldMask,
    EfiSmbiosPrepareCheckSystemTable,
    EfiSmbiosPrepareCheckOriginalValues,
    EfiSmbiosPrepareCheckReplacementValues,
    EfiSmbiosPrepareCheckMemoryMapInitialQuery,
    EfiSmbiosPrepareCheckMemoryMapSize,
    EfiSmbiosPrepareCheckMemoryMapAllocate,
    EfiSmbiosPrepareCheckMemoryMapRead,
    EfiSmbiosPrepareCheckMemoryMapCapacity,
    EfiSmbiosPrepareCheckMemoryMapDescriptor,
    EfiSmbiosPrepareCheckMemoryMapOverlap,
    EfiSmbiosPrepareCheckMemoryMapRelease,
    EfiSmbiosPrepareCheckConfigurationTableHeader,
    EfiSmbiosPrepareCheckConfigurationTableRange,
    EfiSmbiosPrepareCheckConfigurationTableDuplicate,
    EfiSmbiosPrepareCheckConfigurationTableLimit,
    EfiSmbiosPrepareCheckConfigurationTableMissing,
    EfiSmbiosPrepareCheckEntryPointRange,
    EfiSmbiosPrepareCheckEntryPointParse,
    EfiSmbiosPrepareCheckTableRange,
    EfiSmbiosPrepareCheckTableMeasure,
    EfiSmbiosPrepareCheckTableParse,
    EfiSmbiosPrepareCheckTableLength,
    EfiSmbiosPrepareCheckSmbios2TableBytes,
    EfiSmbiosPrepareCheckSmbios2StructureCount,
    EfiSmbiosPrepareCheckSmbios2MaximumStructureSize,
    EfiSmbiosPrepareCheckOriginalValuesMatch,
    EfiSmbiosPrepareCheckScratchAllocate,
    EfiSmbiosPrepareCheckReplacementTableBuild,
    EfiSmbiosPrepareCheckReplacementTableBounds,
    EfiSmbiosPrepareCheckReplacementTableParse,
    EfiSmbiosPrepareCheckReplacementTableLength,
    EfiSmbiosPrepareCheckReplacementValuesMatch,
    EfiSmbiosPrepareCheckOutputSize,
    EfiSmbiosPrepareCheckPageAllocate,
    EfiSmbiosPrepareCheckPageRange,
    EfiSmbiosPrepareCheckReplacementEntryPointBuild,
    EfiSmbiosPrepareCheckScratchRelease,
    EfiSmbiosPrepareCheckReplacementFieldPresent,
    EfiSmbiosPrepareCheckDualEntryVersion,
    EfiSmbiosPrepareCheckDualEntryValues,
    EfiSmbiosPrepareCheckPreparedPagesRelease,
    EfiSmbiosPrepareCheckUnclassified
} EFI_SMBIOS_PREPARE_CHECK;

typedef struct efi_smbios_prepare_diagnostic {
    EFI_SMBIOS_PREPARE_CHECK check;
    UINT64 entry_index;
    EFI_STATUS cause_status;
    EFI_STATUS prepared_pages_cleanup_status;
    EFI_STATUS auxiliary_cleanup_status;
    UINT8 entry_metadata_available;
    UINT8 table_metadata_available;
    UINT8 table_parse_complete;
    UINT8 entry_kind;
    UINT8 entry_major;
    UINT8 entry_minor;
    UINT8 reserved[3];
    UINT64 declared_table_bytes;
    UINT64 declared_structure_count;
    UINT64 declared_maximum_structure_size;
    UINT64 actual_table_bytes;
    UINT64 actual_structure_count;
    UINT64 actual_maximum_structure_size;
    UINT64 replacement_table_bytes;
    UINT64 replacement_structure_count;
    UINT64 replacement_maximum_structure_size;
    UINT8 replacement_metadata_available;
} EFI_SMBIOS_PREPARE_DIAGNOSTIC;

EFI_STATUS EFIAPI EfiSmbiosGetLastPrepareDiagnostic(
    EFI_SMBIOS_PREPARE_DIAGNOSTIC *Diagnostic);

const char *EFIAPI EfiSmbiosPrepareCheckName(EFI_SMBIOS_PREPARE_CHECK Check);

#endif
