#include "Uefi.h"

#include <stddef.h>
#include <stdint.h>

#define POST_EBS_CODE_PAGES ((UINTN)1)
#define POST_EBS_MAX_ADDRESS UINT64_C(0x3fffffff)
#define POST_EBS_MAP_SLACK_DESCRIPTORS ((UINTN)8)
#define POST_EBS_MAX_MAP_ATTEMPTS UINT32_C(4)

EFI_SYSTEM_TABLE *gST = NULL;
EFI_BOOT_SERVICES *gBS = NULL;

extern uint8_t HvEfiPostEbsResidentStart;
extern uint8_t HvEfiPostEbsResidentEnd;

typedef EFI_STATUS (*POST_EBS_RESIDENT_ENTRY)(
    EFI_EXIT_BOOT_SERVICES exitBootServices,
    EFI_HANDLE imageHandle,
    UINTN mapKey,
    UINTN residentStackTop);

static void CopyBytes(void *destination, const void *source, UINTN size)
{
    UINT8 *output = (UINT8 *)destination;
    const UINT8 *input = (const UINT8 *)source;
    UINTN index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

static EFI_STATUS GetFinalMemoryMap(EFI_MEMORY_DESCRIPTOR **memoryMap,
                                    UINTN *memoryMapSize,
                                    UINTN *memoryMapCapacity,
                                    UINTN *mapKey,
                                    UINTN *descriptorSize,
                                    UINT32 *descriptorVersion)
{
    EFI_STATUS status;
    UINTN requiredSize = 0;
    UINT32 attempt;

    status = gBS->GetMemoryMap(&requiredSize, NULL, mapKey,
                               descriptorSize, descriptorVersion);
    if (status != EFI_BUFFER_TOO_SMALL || *descriptorSize == 0) {
        return status;
    }

    for (attempt = 0; attempt < POST_EBS_MAX_MAP_ATTEMPTS; ++attempt) {
        UINTN allocationSize = requiredSize +
            (*descriptorSize * POST_EBS_MAP_SLACK_DESCRIPTORS);

        if (*memoryMap != NULL) {
            gBS->FreePool(*memoryMap);
            *memoryMap = NULL;
        }
        status = gBS->AllocatePool(EfiLoaderData, allocationSize,
                                   (VOID **)memoryMap);
        if (EFI_ERROR(status)) {
            return status;
        }

        *memoryMapCapacity = allocationSize;
        *memoryMapSize = *memoryMapCapacity;
        status = gBS->GetMemoryMap(memoryMapSize, *memoryMap, mapKey,
                                   descriptorSize, descriptorVersion);
        if (status != EFI_BUFFER_TOO_SMALL) {
            return status;
        }
        requiredSize = *memoryMapSize;
    }

    return EFI_BUFFER_TOO_SMALL;
}

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE imageHandle,
                          EFI_SYSTEM_TABLE *systemTable)
{
    EFI_PHYSICAL_ADDRESS residentAddress = POST_EBS_MAX_ADDRESS;
    EFI_PHYSICAL_ADDRESS residentStackAddress = POST_EBS_MAX_ADDRESS;
    EFI_MEMORY_DESCRIPTOR *memoryMap = NULL;
    UINTN residentSize;
    UINTN memoryMapSize = 0;
    UINTN memoryMapCapacity = 0;
    UINTN mapKey = 0;
    UINTN descriptorSize = 0;
    UINT32 descriptorVersion = 0;
    EFI_STATUS status;
    UINT32 attempt;

    gST = systemTable;
    gBS = systemTable == NULL ? NULL : systemTable->BootServices;
    if (gBS == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    residentSize = (UINTN)(&HvEfiPostEbsResidentEnd -
                           &HvEfiPostEbsResidentStart);
    if (residentSize == 0 || residentSize > (POST_EBS_CODE_PAGES * 4096)) {
        return EFI_BAD_BUFFER_SIZE;
    }

    status = gBS->AllocatePages(AllocateMaxAddress, EfiRuntimeServicesCode,
                                POST_EBS_CODE_PAGES, &residentAddress);
    if (EFI_ERROR(status)) {
        return status;
    }
    CopyBytes((void *)(uintptr_t)residentAddress,
              &HvEfiPostEbsResidentStart, residentSize);

    status = gBS->AllocatePages(AllocateMaxAddress, EfiRuntimeServicesData,
                                POST_EBS_CODE_PAGES,
                                &residentStackAddress);
    if (EFI_ERROR(status)) {
        gBS->FreePages(residentAddress, POST_EBS_CODE_PAGES);
        return status;
    }

    status = GetFinalMemoryMap(&memoryMap, &memoryMapSize, &memoryMapCapacity,
                               &mapKey,
                               &descriptorSize, &descriptorVersion);
    if (EFI_ERROR(status)) {
        gBS->FreePages(residentStackAddress, POST_EBS_CODE_PAGES);
        gBS->FreePages(residentAddress, POST_EBS_CODE_PAGES);
        return status;
    }

    for (attempt = 0; attempt < POST_EBS_MAX_MAP_ATTEMPTS; ++attempt) {
        status = ((POST_EBS_RESIDENT_ENTRY)(uintptr_t)residentAddress)(
            gBS->ExitBootServices, imageHandle, mapKey,
            (UINTN)(residentStackAddress + 4096));
        if (!EFI_ERROR(status)) {
            return EFI_ABORTED;
        }
        if (status != EFI_INVALID_PARAMETER) {
            break;
        }

        memoryMapSize = memoryMapCapacity;
        status = gBS->GetMemoryMap(&memoryMapSize, memoryMap, &mapKey,
                                   &descriptorSize, &descriptorVersion);
        if (EFI_ERROR(status)) {
            break;
        }
    }

    gBS->FreePool(memoryMap);
    gBS->FreePages(residentStackAddress, POST_EBS_CODE_PAGES);
    gBS->FreePages(residentAddress, POST_EBS_CODE_PAGES);
    return status;
}
