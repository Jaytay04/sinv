#include "iommu_runtime.h"

#define HV_ACPI_HEADER_SIZE UINT32_C(36)
#define HV_RSDP_V2_SIZE UINT32_C(36)
#define HV_DMAR_HEADER_SIZE UINT32_C(48)
#define HV_ACPI_MAX_TABLE_SIZE UINT32_C(1048576)
#define HV_RSDP_MAX_SIZE UINT32_C(4096)

#define HV_VTD_VERSION UINT64_C(0x000)
#define HV_VTD_CAP UINT64_C(0x008)
#define HV_VTD_ECAP UINT64_C(0x010)
#define HV_VTD_GCMD UINT64_C(0x018)
#define HV_VTD_GSTS UINT64_C(0x01c)
#define HV_VTD_RTADDR UINT64_C(0x020)
#define HV_VTD_FSTS UINT64_C(0x034)
#define HV_VTD_IQT UINT64_C(0x088)
#define HV_VTD_IQA UINT64_C(0x090)
#define HV_VTD_IRTA UINT64_C(0x0b8)

#define HV_VTD_GCMD_TE (UINT32_C(1) << 31)
#define HV_VTD_GCMD_SRTP (UINT32_C(1) << 30)
#define HV_VTD_GCMD_QIE (UINT32_C(1) << 26)
#define HV_VTD_GCMD_IRE (UINT32_C(1) << 25)
#define HV_VTD_GCMD_SIRTP (UINT32_C(1) << 24)
#define HV_VTD_GSTS_TES (UINT32_C(1) << 31)
#define HV_VTD_GSTS_RTPS (UINT32_C(1) << 30)
#define HV_VTD_GSTS_QIES (UINT32_C(1) << 26)
#define HV_VTD_GSTS_IRES (UINT32_C(1) << 25)
#define HV_VTD_GSTS_IRTPS (UINT32_C(1) << 24)
#define HV_VTD_IRTA_EIME (UINT64_C(1) << 11)

#define HV_VTD_CAP_SAGAW_SHIFT UINT32_C(8)
#define HV_VTD_CAP_SAGAW_MASK UINT64_C(0x1f)
#define HV_VTD_CAP_MGAW_SHIFT UINT32_C(16)
#define HV_VTD_CAP_MGAW_MASK UINT64_C(0x3f)
#define HV_VTD_CAP_SLLPS_2MB (UINT64_C(1) << 34)
#define HV_VTD_ECAP_QI (UINT64_C(1) << 1)
#define HV_VTD_ECAP_IR (UINT64_C(1) << 3)
#define HV_VTD_ECAP_EIM (UINT64_C(1) << 4)

typedef struct HV_DMAR_COUNTS {
    uint32_t Units;
    uint32_t Rmrrs;
    uint32_t Scopes;
    uint32_t Paths;
} HV_DMAR_COUNTS;

static void HvZero(void *buffer, size_t size)
{
    uint8_t *bytes = (uint8_t *)buffer;
    size_t index;
    if (bytes == NULL) {
        return;
    }
    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static int HvAddU64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (result == NULL || left > UINT64_MAX - right) {
        return 0;
    }
    *result = left + right;
    return 1;
}

static int HvAddU32(uint32_t left, uint32_t right, uint32_t *result)
{
    if (result == NULL || left > UINT32_MAX - right) {
        return 0;
    }
    *result = left + right;
    return 1;
}

static uint16_t HvRead16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] |
        ((uint16_t)bytes[1] << 8));
}

static uint32_t HvRead32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static uint64_t HvRead64(const uint8_t *bytes)
{
    return (uint64_t)HvRead32(bytes) |
        ((uint64_t)HvRead32(bytes + 4) << 32);
}

static int HvBytesEqual(
    const uint8_t *bytes, const char *text, uint32_t count)
{
    uint32_t index;
    for (index = 0; index < count; ++index) {
        if (bytes[index] != (uint8_t)text[index]) {
            return 0;
        }
    }
    return 1;
}

static int HvChecksumValid(const uint8_t *bytes, uint32_t length)
{
    uint8_t sum = 0;
    uint32_t index;
    for (index = 0; index < length; ++index) {
        sum = (uint8_t)(sum + bytes[index]);
    }
    return sum == 0;
}

static HV_EFI_IOMMU_RUNTIME_STATUS HvMapPhysical(
    const HV_EFI_IOMMU_PHYSICAL_VIEW *view,
    uint64_t physical,
    uint64_t size,
    const uint8_t **bytes)
{
    uint64_t offset;
    if (view == NULL || bytes == NULL || size == 0 ||
        (view->Bytes == NULL && view->IdentityMapped == 0) ||
        view->Reserved != 0 || view->IdentityMapped > 1 ||
        physical < view->PhysicalBase) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    offset = physical - view->PhysicalBase;
    if (offset > view->Size || size > view->Size - offset ||
        offset > (uint64_t)SIZE_MAX) {
        return HV_EFI_IOMMU_RUNTIME_OUT_OF_BOUNDS;
    }
    if (view->IdentityMapped != 0) {
        if (physical > (uint64_t)UINTPTR_MAX) {
            return HV_EFI_IOMMU_RUNTIME_OUT_OF_BOUNDS;
        }
        *bytes = (const uint8_t *)(uintptr_t)physical;
    } else {
        *bytes = view->Bytes + (size_t)offset;
    }
    return HV_EFI_IOMMU_RUNTIME_OK;
}

static int HvScopeTypeSupported(uint32_t type)
{
    return type >= HV_EFI_IOMMU_SCOPE_PCI_ENDPOINT &&
        type <= HV_EFI_IOMMU_SCOPE_ACPI_NAMESPACE;
}

static HV_EFI_IOMMU_RUNTIME_STATUS HvParseScopes(
    const uint8_t *bytes,
    uint32_t length,
    uint32_t segment,
    uint32_t ownerKind,
    uint32_t ownerIndex,
    int fill,
    const HV_EFI_IOMMU_DMAR_STORAGE *storage,
    HV_DMAR_COUNTS *counts)
{
    uint32_t offset = 0;
    while (offset < length) {
        uint32_t scopeLength;
        uint32_t pathCount;
        uint32_t pathIndex;
        uint32_t scopeIndex;
        if (length - offset < 8) {
            return HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE;
        }
        scopeLength = bytes[offset + 1];
        if (scopeLength < 8 || scopeLength > length - offset ||
            ((scopeLength - 6) & 1u) != 0 ||
            bytes[offset + 2] != 0 || bytes[offset + 3] != 0 ||
            !HvScopeTypeSupported(bytes[offset]) ||
            (ownerKind == HV_EFI_IOMMU_SCOPE_OWNER_RMRR &&
             bytes[offset] != HV_EFI_IOMMU_SCOPE_PCI_ENDPOINT &&
             bytes[offset] != HV_EFI_IOMMU_SCOPE_PCI_SUB_HIERARCHY)) {
            return !HvScopeTypeSupported(bytes[offset]) ?
                HV_EFI_IOMMU_RUNTIME_UNSUPPORTED_SCOPE :
                HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE;
        }
        pathCount = (scopeLength - 6) / 2;
        scopeIndex = counts->Scopes;
        if (!HvAddU32(counts->Scopes, 1, &counts->Scopes) ||
            !HvAddU32(counts->Paths, pathCount, &counts->Paths)) {
            return HV_EFI_IOMMU_RUNTIME_CAPACITY;
        }
        if (fill) {
            HV_EFI_IOMMU_DMAR_SCOPE *scope = &storage->Scopes[scopeIndex];
            scope->OwnerKind = ownerKind;
            scope->OwnerIndex = ownerIndex;
            scope->Type = bytes[offset];
            scope->Segment = segment;
            scope->EnumerationId = bytes[offset + 4];
            scope->StartBus = bytes[offset + 5];
            scope->FirstPathElement = counts->Paths - pathCount;
            scope->PathElementCount = pathCount;
        }
        for (pathIndex = 0; pathIndex < pathCount; ++pathIndex) {
            const uint8_t device = bytes[offset + 6 + pathIndex * 2];
            const uint8_t function = bytes[offset + 7 + pathIndex * 2];
            if (device > 31 || function > 7) {
                return HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE;
            }
            if (fill) {
                HV_EFI_IOMMU_DEVICE_PATH_ELEMENT *path =
                    &storage->PathElements[
                        counts->Paths - pathCount + pathIndex];
                path->Device = device;
                path->Function = function;
                path->Reserved = 0;
            }
        }
        offset += scopeLength;
    }
    return offset == length ? HV_EFI_IOMMU_RUNTIME_OK :
        HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE;
}

static HV_EFI_IOMMU_RUNTIME_STATUS HvParseDmarBody(
    const uint8_t *dmar,
    uint32_t dmarLength,
    int fill,
    const HV_EFI_IOMMU_DMAR_STORAGE *storage,
    HV_DMAR_COUNTS *counts)
{
    uint32_t offset = HV_DMAR_HEADER_SIZE;
    while (offset < dmarLength) {
        const uint8_t *entry;
        uint32_t type;
        uint32_t length;
        if (dmarLength - offset < 4) {
            return HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE;
        }
        entry = dmar + offset;
        type = HvRead16(entry);
        length = HvRead16(entry + 2);
        if (length < 4 || length > dmarLength - offset) {
            return HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE;
        }
        if (type == 0) {
            uint32_t unitIndex;
            uint64_t registerBase;
            uint32_t scopeStart;
            HV_EFI_IOMMU_RUNTIME_STATUS status;
            uint32_t previous;
            if (length < 16) {
                return HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE;
            }
            registerBase = HvRead64(entry + 8);
            if (registerBase == 0 ||
                (registerBase & HV_EFI_IOMMU_RUNTIME_PAGE_MASK) != 0 ||
                (entry[4] & ~HV_EFI_IOMMU_DRHD_INCLUDE_ALL) != 0 ||
                entry[5] != 0) {
                return HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE;
            }
            unitIndex = counts->Units;
            scopeStart = counts->Scopes;
            if (!HvAddU32(counts->Units, 1, &counts->Units)) {
                return HV_EFI_IOMMU_RUNTIME_CAPACITY;
            }
            status = HvParseScopes(entry + 16, length - 16,
                                   HvRead16(entry + 6),
                                   HV_EFI_IOMMU_SCOPE_OWNER_DRHD,
                                   unitIndex, fill, storage, counts);
            if (status != HV_EFI_IOMMU_RUNTIME_OK) {
                return status;
            }
            if (counts->Scopes == scopeStart &&
                (entry[4] & HV_EFI_IOMMU_DRHD_INCLUDE_ALL) == 0) {
                return HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE;
            }
            if (fill) {
                HV_EFI_IOMMU_DMAR_UNIT *unit = &storage->Units[unitIndex];
                unit->RegisterBase = registerBase;
                unit->Segment = HvRead16(entry + 6);
                unit->Flags = entry[4];
                unit->FirstScope = scopeStart;
                unit->ScopeCount = counts->Scopes - scopeStart;
                for (previous = 0; previous < unitIndex; ++previous) {
                    const HV_EFI_IOMMU_DMAR_UNIT *other =
                        &storage->Units[previous];
                    if (other->RegisterBase == unit->RegisterBase ||
                        (other->Segment == unit->Segment &&
                         (other->Flags & HV_EFI_IOMMU_DRHD_INCLUDE_ALL) != 0 &&
                         (unit->Flags & HV_EFI_IOMMU_DRHD_INCLUDE_ALL) != 0)) {
                        return HV_EFI_IOMMU_RUNTIME_RANGE_OVERLAP;
                    }
                }
            }
        } else if (type == 1) {
            uint32_t rmrrIndex;
            uint32_t scopeStart;
            uint64_t base;
            uint64_t limit;
            uint64_t end;
            HV_EFI_IOMMU_RUNTIME_STATUS status;
            uint32_t previous;
            if (length < 24) {
                return HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE;
            }
            if (HvRead16(entry + 4) != 0) {
                return HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE;
            }
            base = HvRead64(entry + 8);
            limit = HvRead64(entry + 16);
            if (limit == UINT64_MAX || !HvAddU64(limit, 1, &end)) {
                return HV_EFI_IOMMU_RUNTIME_RANGE_OVERFLOW;
            }
            if (base >= end ||
                (base & HV_EFI_IOMMU_RUNTIME_PAGE_MASK) != 0 ||
                (end & HV_EFI_IOMMU_RUNTIME_PAGE_MASK) != 0) {
                return HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE;
            }
            rmrrIndex = counts->Rmrrs;
            scopeStart = counts->Scopes;
            if (!HvAddU32(counts->Rmrrs, 1, &counts->Rmrrs)) {
                return HV_EFI_IOMMU_RUNTIME_CAPACITY;
            }
            status = HvParseScopes(entry + 24, length - 24,
                                   HvRead16(entry + 6),
                                   HV_EFI_IOMMU_SCOPE_OWNER_RMRR,
                                   rmrrIndex, fill, storage, counts);
            if (status != HV_EFI_IOMMU_RUNTIME_OK) {
                return status;
            }
            if (counts->Scopes == scopeStart) {
                return HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE;
            }
            if (fill) {
                HV_EFI_IOMMU_DMAR_RMRR *rmrr = &storage->Rmrrs[rmrrIndex];
                rmrr->PhysicalBase = base;
                rmrr->Size = end - base;
                rmrr->Segment = HvRead16(entry + 6);
                rmrr->FirstScope = scopeStart;
                rmrr->ScopeCount = counts->Scopes - scopeStart;
                rmrr->Reserved = 0;
                for (previous = 0; previous < rmrrIndex; ++previous) {
                    const HV_EFI_IOMMU_DMAR_RMRR *other =
                        &storage->Rmrrs[previous];
                    if (base < other->PhysicalBase + other->Size &&
                        other->PhysicalBase < end) {
                        return HV_EFI_IOMMU_RUNTIME_RANGE_OVERLAP;
                    }
                }
            }
        }
        offset += length;
    }
    return offset == dmarLength ? HV_EFI_IOMMU_RUNTIME_OK :
        HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE;
}

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuParseDmar(
    const HV_EFI_IOMMU_PHYSICAL_VIEW *view,
    uint64_t rsdpPhysical,
    const HV_EFI_IOMMU_DMAR_STORAGE *storage,
    HV_EFI_IOMMU_DMAR_INFO *info)
{
    const uint8_t *rsdp;
    const uint8_t *xsdt;
    const uint8_t *dmar = NULL;
    uint32_t rsdpLength;
    uint32_t xsdtLength;
    uint32_t entryCount;
    uint32_t index;
    uint32_t dmarLength = 0;
    uint64_t dmarPhysical = 0;
    uint32_t dmarMatches = 0;
    HV_DMAR_COUNTS counts;
    HV_EFI_IOMMU_RUNTIME_STATUS status;
    if (info == NULL) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    HvZero(info, sizeof(*info));
    if (view == NULL || storage == NULL) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    status = HvMapPhysical(view, rsdpPhysical, HV_RSDP_V2_SIZE, &rsdp);
    if (status != HV_EFI_IOMMU_RUNTIME_OK) {
        return status;
    }
    if (!HvBytesEqual(rsdp, "RSD PTR ", 8) || rsdp[15] < 2) {
        return HV_EFI_IOMMU_RUNTIME_BAD_SIGNATURE;
    }
    rsdpLength = HvRead32(rsdp + 20);
    if (rsdpLength < HV_RSDP_V2_SIZE ||
        rsdpLength > HV_RSDP_MAX_SIZE) {
        return HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE;
    }
    status = HvMapPhysical(view, rsdpPhysical, rsdpLength, &rsdp);
    if (status != HV_EFI_IOMMU_RUNTIME_OK) {
        return status;
    }
    if (!HvChecksumValid(rsdp, 20) || !HvChecksumValid(rsdp, rsdpLength)) {
        return HV_EFI_IOMMU_RUNTIME_BAD_CHECKSUM;
    }
    status = HvMapPhysical(view, HvRead64(rsdp + 24),
                           HV_ACPI_HEADER_SIZE, &xsdt);
    if (status != HV_EFI_IOMMU_RUNTIME_OK) {
        return status;
    }
    if (!HvBytesEqual(xsdt, "XSDT", 4)) {
        return HV_EFI_IOMMU_RUNTIME_BAD_SIGNATURE;
    }
    xsdtLength = HvRead32(xsdt + 4);
    if (xsdtLength < HV_ACPI_HEADER_SIZE ||
        xsdtLength > HV_ACPI_MAX_TABLE_SIZE ||
        ((xsdtLength - HV_ACPI_HEADER_SIZE) & 7u) != 0) {
        return HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE;
    }
    status = HvMapPhysical(view, HvRead64(rsdp + 24), xsdtLength, &xsdt);
    if (status != HV_EFI_IOMMU_RUNTIME_OK) {
        return status;
    }
    if (!HvChecksumValid(xsdt, xsdtLength)) {
        return HV_EFI_IOMMU_RUNTIME_BAD_CHECKSUM;
    }
    entryCount = (xsdtLength - HV_ACPI_HEADER_SIZE) / 8;
    for (index = 0; index < entryCount; ++index) {
        const uint64_t tablePhysical =
            HvRead64(xsdt + HV_ACPI_HEADER_SIZE + index * 8);
        const uint8_t *header;
        uint32_t tableLength;
        status = HvMapPhysical(view, tablePhysical,
                               HV_ACPI_HEADER_SIZE, &header);
        if (status != HV_EFI_IOMMU_RUNTIME_OK) {
            return status;
        }
        tableLength = HvRead32(header + 4);
        if (tableLength < HV_ACPI_HEADER_SIZE ||
            tableLength > HV_ACPI_MAX_TABLE_SIZE) {
            return HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE;
        }
        status = HvMapPhysical(view, tablePhysical, tableLength, &header);
        if (status != HV_EFI_IOMMU_RUNTIME_OK) {
            return status;
        }
        if (!HvChecksumValid(header, tableLength)) {
            return HV_EFI_IOMMU_RUNTIME_BAD_CHECKSUM;
        }
        if (HvBytesEqual(header, "DMAR", 4)) {
            dmar = header;
            dmarLength = tableLength;
            dmarPhysical = tablePhysical;
            ++dmarMatches;
        }
    }
    if (dmarMatches == 0) {
        return HV_EFI_IOMMU_RUNTIME_DMAR_NOT_FOUND;
    }
    if (dmarMatches != 1 || dmar == NULL ||
        dmarLength < HV_DMAR_HEADER_SIZE) {
        return HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE;
    }
    if ((uint32_t)dmar[36] + 1 < 39 ||
        (uint32_t)dmar[36] + 1 > HV_EFI_IOMMU_RUNTIME_MAX_ADDRESS_BITS ||
        (dmar[37] & ~(HV_EFI_IOMMU_DMAR_FLAG_INTERRUPT_REMAPPING |
                      HV_EFI_IOMMU_DMAR_FLAG_X2APIC_OPT_OUT |
                      HV_EFI_IOMMU_DMAR_FLAG_DMA_CONTROL_OPT_IN)) != 0) {
        return HV_EFI_IOMMU_RUNTIME_REMAPPING_UNSUPPORTED;
    }
    HvZero(&counts, sizeof(counts));
    status = HvParseDmarBody(dmar, dmarLength, 0, storage, &counts);
    if (status != HV_EFI_IOMMU_RUNTIME_OK) {
        return status;
    }
    if (counts.Units == 0) {
        return HV_EFI_IOMMU_RUNTIME_REMAPPING_UNSUPPORTED;
    }
    if (storage->UnitCapacity < counts.Units ||
        storage->RmrrCapacity < counts.Rmrrs ||
        storage->ScopeCapacity < counts.Scopes ||
        storage->PathElementCapacity < counts.Paths ||
        storage->Units == NULL ||
        (counts.Rmrrs != 0 && storage->Rmrrs == NULL) ||
        (counts.Scopes != 0 && storage->Scopes == NULL) ||
        (counts.Paths != 0 && storage->PathElements == NULL)) {
        return HV_EFI_IOMMU_RUNTIME_CAPACITY;
    }
    HvZero(&counts, sizeof(counts));
    status = HvParseDmarBody(dmar, dmarLength, 1, storage, &counts);
    if (status != HV_EFI_IOMMU_RUNTIME_OK) {
        return status;
    }
    for (index = 0; index < counts.Units; ++index) {
        uint64_t registerEnd;
        if (!HvAddU64(storage->Units[index].RegisterBase,
                      HV_EFI_IOMMU_RUNTIME_PAGE_SIZE, &registerEnd) ||
            registerEnd >
                (UINT64_C(1) << ((uint32_t)dmar[36] + 1))) {
            return HV_EFI_IOMMU_RUNTIME_RANGE_OVERFLOW;
        }
    }
    info->RsdpPhysical = rsdpPhysical;
    info->XsdtPhysical = HvRead64(rsdp + 24);
    info->DmarPhysical = dmarPhysical;
    info->HostAddressBits = (uint32_t)dmar[36] + 1;
    info->Flags = dmar[37] &
        (HV_EFI_IOMMU_DMAR_FLAG_INTERRUPT_REMAPPING |
         HV_EFI_IOMMU_DMAR_FLAG_X2APIC_OPT_OUT |
         HV_EFI_IOMMU_DMAR_FLAG_DMA_CONTROL_OPT_IN);
    info->Units = storage->Units;
    info->UnitCount = counts.Units;
    info->Rmrrs = storage->Rmrrs;
    info->RmrrCount = counts.Rmrrs;
    info->Scopes = storage->Scopes;
    info->ScopeCount = counts.Scopes;
    info->PathElements = storage->PathElements;
    info->PathElementCount = counts.Paths;
    return HV_EFI_IOMMU_RUNTIME_OK;
}

static int HvBooleanValid(uint32_t value)
{
    return value <= 1;
}

static HV_EFI_IOMMU_RUNTIME_STATUS HvOwnershipValid(
    const HV_EFI_IOMMU_OWNERSHIP_POLICY *ownership)
{
    if (ownership == NULL || ownership->OwnerToken == 0 ||
        !HvBooleanValid(ownership->DmarMmioGuestDenied) ||
        !HvBooleanValid(ownership->RemappingTablesGuestDenied) ||
        !HvBooleanValid(ownership->RemappingTablesDmaDenied) ||
        !HvBooleanValid(ownership->ContinuousAuditEnabled) ||
        !HvBooleanValid(ownership->PlatformLockSupported) ||
        !HvBooleanValid(ownership->PlatformLockEngaged) ||
        !HvBooleanValid(ownership->RequireDmaQuiesce)) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    if (ownership->RequireDmaQuiesce == 0 ||
        ownership->RemappingTablesGuestDenied == 0 ||
        ownership->RemappingTablesDmaDenied == 0) {
        return HV_EFI_IOMMU_RUNTIME_OWNERSHIP_UNAVAILABLE;
    }
    if (ownership->Mode == HV_EFI_IOMMU_OWNERSHIP_EPT_TRAPPED) {
        if (ownership->DmarMmioGuestDenied == 0 ||
            ownership->ContinuousAuditEnabled == 0) {
            return HV_EFI_IOMMU_RUNTIME_OWNERSHIP_UNAVAILABLE;
        }
    } else if (ownership->Mode ==
               HV_EFI_IOMMU_OWNERSHIP_PLATFORM_LOCKED) {
        if (ownership->PlatformLockSupported == 0 ||
            ownership->PlatformLockEngaged == 0) {
            return HV_EFI_IOMMU_RUNTIME_OWNERSHIP_UNAVAILABLE;
        }
    } else {
        return HV_EFI_IOMMU_RUNTIME_OWNERSHIP_UNAVAILABLE;
    }
    return HV_EFI_IOMMU_RUNTIME_OK;
}

static int HvPowerOfTwo(uint32_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

static uint32_t HvLog2(uint32_t value)
{
    uint32_t result = 0;
    while (value > 1) {
        value >>= 1;
        ++result;
    }
    return result;
}

static int HvRangeBelowWidth(
    uint64_t base, uint64_t size, uint32_t addressBits)
{
    uint64_t end;
    return size != 0 && HvAddU64(base, size, &end) &&
        end <= (UINT64_C(1) << addressBits);
}

static int HvRangesOverlap(
    uint64_t leftBase, uint64_t leftSize,
    uint64_t rightBase, uint64_t rightSize)
{
    return leftBase < rightBase + rightSize &&
        rightBase < leftBase + leftSize;
}

static void HvEmitOperation(
    HV_EFI_IOMMU_MMIO_OPERATION *operations,
    uint32_t *count,
    uint32_t unit,
    uint32_t kind,
    uint64_t address,
    uint32_t width,
    uint64_t value,
    uint64_t mask,
    uint32_t invalidationFlags)
{
    HV_EFI_IOMMU_MMIO_OPERATION *operation = &operations[*count];
    operation->Address = address;
    operation->Value = value;
    operation->Mask = mask;
    operation->UnitIndex = unit;
    operation->Kind = kind;
    operation->Width = width;
    operation->InvalidationFlags = invalidationFlags;
    ++*count;
}

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuBuildProgrammingPlan(
    const HV_EFI_IOMMU_DMAR_INFO *dmar,
    const HV_EFI_IOMMU_UNIT_CONFIGURATION *configurations,
    uint32_t configurationCount,
    const HV_EFI_IOMMU_OWNERSHIP_POLICY *ownership,
    const HV_EFI_IOMMU_RUNTIME_CALLBACKS *callbacks,
    const HV_EFI_IOMMU_PROGRAM_STORAGE *storage,
    uint32_t pollLimit,
    HV_EFI_IOMMU_PROGRAM_PLAN *plan)
{
    uint32_t requiredOperations = 0;
    uint32_t unitIndex;
    uint32_t operationCount = 0;
    HV_EFI_IOMMU_RUNTIME_STATUS status;
    if (plan == NULL) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    HvZero(plan, sizeof(*plan));
    if (dmar == NULL || configurations == NULL || callbacks == NULL ||
        callbacks->Read == NULL || callbacks->Write == NULL ||
        callbacks->Invalidate == NULL || callbacks->OwnershipHeld == NULL ||
        callbacks->DmaQuiesced == NULL || storage == NULL ||
        dmar->Units == NULL || dmar->UnitCount == 0 ||
        configurationCount != dmar->UnitCount || pollLimit == 0) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    status = HvOwnershipValid(ownership);
    if (status != HV_EFI_IOMMU_RUNTIME_OK) {
        return status;
    }
    for (unitIndex = 0; unitIndex < dmar->UnitCount; ++unitIndex) {
        const HV_EFI_IOMMU_UNIT_CONFIGURATION *configuration =
            &configurations[unitIndex];
        uint32_t unitOperations = 8;
        if (!HvBooleanValid(configuration->EnableQueuedInvalidation) ||
            !HvBooleanValid(configuration->EnableInterruptRemapping) ||
            !HvBooleanValid(configuration->EnableExtendedInterruptMode) ||
            configuration->Reserved != 0 ||
            configuration->RootTablePhysical == 0 ||
            (configuration->RootTablePhysical &
             HV_EFI_IOMMU_RUNTIME_PAGE_MASK) != 0 ||
            configuration->AddressBits > dmar->HostAddressBits ||
            (configuration->AddressBits != 39 &&
             configuration->AddressBits != 48)) {
            return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
        }
        if (!HvRangeBelowWidth(
                configuration->RootTablePhysical,
                HV_EFI_IOMMU_RUNTIME_PAGE_SIZE,
                configuration->AddressBits)) {
            return HV_EFI_IOMMU_RUNTIME_RANGE_OVERFLOW;
        }
        if (configuration->EnableInterruptRemapping != 0 &&
            configuration->EnableQueuedInvalidation == 0) {
            return HV_EFI_IOMMU_RUNTIME_QI_INCOMPATIBLE;
        }
        if (configuration->EnableExtendedInterruptMode != 0 &&
            configuration->EnableInterruptRemapping == 0) {
            return HV_EFI_IOMMU_RUNTIME_IR_INCOMPATIBLE;
        }
        if (configuration->EnableQueuedInvalidation != 0) {
            if (configuration->InvalidationQueuePhysical == 0 ||
                (configuration->InvalidationQueuePhysical &
                 HV_EFI_IOMMU_RUNTIME_PAGE_MASK) != 0 ||
                !HvRangeBelowWidth(
                    configuration->InvalidationQueuePhysical,
                    HV_EFI_IOMMU_RUNTIME_PAGE_SIZE,
                    configuration->AddressBits)) {
                return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
            }
            if (HvRangesOverlap(
                    configuration->RootTablePhysical,
                    HV_EFI_IOMMU_RUNTIME_PAGE_SIZE,
                    configuration->InvalidationQueuePhysical,
                    HV_EFI_IOMMU_RUNTIME_PAGE_SIZE)) {
                return HV_EFI_IOMMU_RUNTIME_RANGE_OVERLAP;
            }
            unitOperations += 4;
        }
        if (configuration->EnableInterruptRemapping != 0) {
            if (configuration->InterruptTablePhysical == 0 ||
                (configuration->InterruptTablePhysical &
                 HV_EFI_IOMMU_RUNTIME_PAGE_MASK) != 0 ||
                !HvPowerOfTwo(configuration->InterruptEntryCount) ||
                configuration->InterruptEntryCount < 16 ||
                configuration->InterruptEntryCount > 32768 ||
                !HvRangeBelowWidth(
                    configuration->InterruptTablePhysical,
                    (uint64_t)configuration->InterruptEntryCount * 16,
                    configuration->AddressBits) ||
                (dmar->Flags &
                 HV_EFI_IOMMU_DMAR_FLAG_INTERRUPT_REMAPPING) == 0) {
                return HV_EFI_IOMMU_RUNTIME_IR_INCOMPATIBLE;
            }
            if ((configuration->EnableExtendedInterruptMode != 0 &&
                 (dmar->Flags &
                  HV_EFI_IOMMU_DMAR_FLAG_X2APIC_OPT_OUT) != 0)) {
                return HV_EFI_IOMMU_RUNTIME_IR_INCOMPATIBLE;
            }
            if (HvRangesOverlap(
                    configuration->RootTablePhysical,
                    HV_EFI_IOMMU_RUNTIME_PAGE_SIZE,
                    configuration->InterruptTablePhysical,
                    (uint64_t)configuration->InterruptEntryCount * 16) ||
                HvRangesOverlap(
                    configuration->InvalidationQueuePhysical,
                    HV_EFI_IOMMU_RUNTIME_PAGE_SIZE,
                    configuration->InterruptTablePhysical,
                    (uint64_t)configuration->InterruptEntryCount * 16)) {
                return HV_EFI_IOMMU_RUNTIME_RANGE_OVERLAP;
            }
            unitOperations += 6;
        }
        if (!HvAddU32(requiredOperations, unitOperations,
                      &requiredOperations)) {
            return HV_EFI_IOMMU_RUNTIME_CAPACITY;
        }
    }
    for (unitIndex = 0; unitIndex < dmar->UnitCount; ++unitIndex) {
        const HV_EFI_IOMMU_UNIT_CONFIGURATION *current =
            &configurations[unitIndex];
        const uint64_t currentIrSize =
            (uint64_t)current->InterruptEntryCount * 16;
        uint32_t otherIndex;
        for (otherIndex = 0; otherIndex < dmar->UnitCount; ++otherIndex) {
            const uint64_t registerBase = dmar->Units[otherIndex].RegisterBase;
            if (HvRangesOverlap(
                    current->RootTablePhysical,
                    HV_EFI_IOMMU_RUNTIME_PAGE_SIZE,
                    registerBase, HV_EFI_IOMMU_RUNTIME_PAGE_SIZE) ||
                (current->EnableQueuedInvalidation != 0 &&
                 HvRangesOverlap(
                    current->InvalidationQueuePhysical,
                    HV_EFI_IOMMU_RUNTIME_PAGE_SIZE,
                    registerBase, HV_EFI_IOMMU_RUNTIME_PAGE_SIZE)) ||
                (current->EnableInterruptRemapping != 0 &&
                 HvRangesOverlap(
                    current->InterruptTablePhysical, currentIrSize,
                    registerBase, HV_EFI_IOMMU_RUNTIME_PAGE_SIZE))) {
                return HV_EFI_IOMMU_RUNTIME_RANGE_OVERLAP;
            }
        }
        for (otherIndex = 0; otherIndex < unitIndex; ++otherIndex) {
            const HV_EFI_IOMMU_UNIT_CONFIGURATION *previous =
                &configurations[otherIndex];
            const uint64_t previousIrSize =
                (uint64_t)previous->InterruptEntryCount * 16;
            if ((current->EnableQueuedInvalidation != 0 &&
                 previous->EnableQueuedInvalidation != 0 &&
                 HvRangesOverlap(
                    current->InvalidationQueuePhysical,
                    HV_EFI_IOMMU_RUNTIME_PAGE_SIZE,
                    previous->InvalidationQueuePhysical,
                    HV_EFI_IOMMU_RUNTIME_PAGE_SIZE)) ||
                (current->EnableInterruptRemapping != 0 &&
                 previous->EnableInterruptRemapping != 0 &&
                 HvRangesOverlap(
                    current->InterruptTablePhysical, currentIrSize,
                    previous->InterruptTablePhysical, previousIrSize)) ||
                (current->EnableQueuedInvalidation != 0 &&
                 previous->EnableInterruptRemapping != 0 &&
                 HvRangesOverlap(
                    current->InvalidationQueuePhysical,
                    HV_EFI_IOMMU_RUNTIME_PAGE_SIZE,
                    previous->InterruptTablePhysical, previousIrSize)) ||
                (current->EnableInterruptRemapping != 0 &&
                 previous->EnableQueuedInvalidation != 0 &&
                 HvRangesOverlap(
                    current->InterruptTablePhysical, currentIrSize,
                    previous->InvalidationQueuePhysical,
                    HV_EFI_IOMMU_RUNTIME_PAGE_SIZE)) ||
                (current->EnableQueuedInvalidation != 0 &&
                 HvRangesOverlap(
                    current->InvalidationQueuePhysical,
                    HV_EFI_IOMMU_RUNTIME_PAGE_SIZE,
                    previous->RootTablePhysical,
                    HV_EFI_IOMMU_RUNTIME_PAGE_SIZE)) ||
                (previous->EnableQueuedInvalidation != 0 &&
                 HvRangesOverlap(
                    current->RootTablePhysical,
                    HV_EFI_IOMMU_RUNTIME_PAGE_SIZE,
                    previous->InvalidationQueuePhysical,
                    HV_EFI_IOMMU_RUNTIME_PAGE_SIZE)) ||
                (current->EnableInterruptRemapping != 0 &&
                 HvRangesOverlap(
                    current->InterruptTablePhysical, currentIrSize,
                    previous->RootTablePhysical,
                    HV_EFI_IOMMU_RUNTIME_PAGE_SIZE)) ||
                (previous->EnableInterruptRemapping != 0 &&
                 HvRangesOverlap(
                    current->RootTablePhysical,
                    HV_EFI_IOMMU_RUNTIME_PAGE_SIZE,
                    previous->InterruptTablePhysical, previousIrSize))) {
                return HV_EFI_IOMMU_RUNTIME_RANGE_OVERLAP;
            }
        }
    }
    if (storage->Units == NULL || storage->Operations == NULL ||
        storage->UnitCapacity < dmar->UnitCount ||
        storage->OperationCapacity < requiredOperations) {
        return HV_EFI_IOMMU_RUNTIME_CAPACITY;
    }
    if (callbacks->OwnershipHeld(callbacks->Context,
                                 ownership->OwnerToken) == 0 ||
        callbacks->DmaQuiesced(callbacks->Context) == 0) {
        return HV_EFI_IOMMU_RUNTIME_OWNERSHIP_UNAVAILABLE;
    }

    for (unitIndex = 0; unitIndex < dmar->UnitCount; ++unitIndex) {
        const HV_EFI_IOMMU_DMAR_UNIT *unit = &dmar->Units[unitIndex];
        const HV_EFI_IOMMU_UNIT_CONFIGURATION *configuration =
            &configurations[unitIndex];
        HV_EFI_IOMMU_UNIT_PROGRAM *program = &storage->Units[unitIndex];
        uint64_t version;
        uint64_t capability;
        uint64_t extendedCapability;
        uint64_t statusValue;
        uint64_t originalRoot;
        uint64_t faultStatus;
        uint32_t sagaw;
        uint32_t mgaw;
        uint32_t desiredStatus = HV_VTD_GSTS_TES;
        uint32_t command = 0;
        uint32_t agawBit = configuration->AddressBits == 39 ? 1 : 2;
        if (callbacks->Read(callbacks->Context,
                            unit->RegisterBase + HV_VTD_VERSION,
                            4, &version) == 0 ||
            callbacks->Read(callbacks->Context,
                            unit->RegisterBase + HV_VTD_CAP,
                            8, &capability) == 0 ||
            callbacks->Read(callbacks->Context,
                            unit->RegisterBase + HV_VTD_ECAP,
                            8, &extendedCapability) == 0 ||
            callbacks->Read(callbacks->Context,
                            unit->RegisterBase + HV_VTD_GSTS,
                            4, &statusValue) == 0 ||
            callbacks->Read(callbacks->Context,
                            unit->RegisterBase + HV_VTD_RTADDR,
                            8, &originalRoot) == 0 ||
            callbacks->Read(callbacks->Context,
                            unit->RegisterBase + HV_VTD_FSTS,
                            4, &faultStatus) == 0) {
            return HV_EFI_IOMMU_RUNTIME_CALLBACK_FAILED;
        }
        if (((version >> 4) & UINT64_C(0x0f)) == 0) {
            return HV_EFI_IOMMU_RUNTIME_REMAPPING_UNSUPPORTED;
        }
        sagaw = (uint32_t)((capability >> HV_VTD_CAP_SAGAW_SHIFT) &
                           HV_VTD_CAP_SAGAW_MASK);
        mgaw = (uint32_t)((capability >> HV_VTD_CAP_MGAW_SHIFT) &
                          HV_VTD_CAP_MGAW_MASK) + 1;
        if ((sagaw & (UINT32_C(1) << agawBit)) == 0 ||
            mgaw < configuration->AddressBits ||
            (capability & HV_VTD_CAP_SLLPS_2MB) == 0) {
            return HV_EFI_IOMMU_RUNTIME_REMAPPING_UNSUPPORTED;
        }
        if ((statusValue & (HV_VTD_GSTS_TES | HV_VTD_GSTS_QIES |
                            HV_VTD_GSTS_IRES)) != 0) {
            return HV_EFI_IOMMU_RUNTIME_ALREADY_ENABLED;
        }
        if ((uint32_t)faultStatus != 0) {
            return HV_EFI_IOMMU_RUNTIME_AUDIT_FAILED;
        }
        if (configuration->EnableQueuedInvalidation != 0 &&
            (extendedCapability & HV_VTD_ECAP_QI) == 0) {
            return HV_EFI_IOMMU_RUNTIME_QI_INCOMPATIBLE;
        }
        if (configuration->EnableInterruptRemapping != 0 &&
            (extendedCapability & HV_VTD_ECAP_IR) == 0) {
            return HV_EFI_IOMMU_RUNTIME_IR_INCOMPATIBLE;
        }
        if (configuration->EnableExtendedInterruptMode != 0 &&
            (extendedCapability & HV_VTD_ECAP_EIM) == 0) {
            return HV_EFI_IOMMU_RUNTIME_IR_INCOMPATIBLE;
        }
        HvZero(program, sizeof(*program));
        program->RegisterBase = unit->RegisterBase;
        program->Capability = capability;
        program->ExtendedCapability = extendedCapability;
        program->OriginalRootTablePhysical = originalRoot;
        program->RootTablePhysical = configuration->RootTablePhysical;
        program->InvalidationQueuePhysical =
            configuration->InvalidationQueuePhysical;
        program->InterruptTablePhysical =
            configuration->InterruptTablePhysical;
        program->InterruptEntryCount = configuration->InterruptEntryCount;
        program->AddressBits = configuration->AddressBits;
        program->FirstOperation = operationCount;

        HvEmitOperation(storage->Operations, &operationCount, unitIndex,
                        HV_EFI_IOMMU_OPERATION_VERIFY_OWNERSHIP,
                        0, 0, 0, 0, 0);
        HvEmitOperation(storage->Operations, &operationCount, unitIndex,
                        HV_EFI_IOMMU_OPERATION_WRITE64,
                        unit->RegisterBase + HV_VTD_RTADDR, 8,
                        configuration->RootTablePhysical, 0, 0);
        HvEmitOperation(storage->Operations, &operationCount, unitIndex,
                        HV_EFI_IOMMU_OPERATION_WRITE32,
                        unit->RegisterBase + HV_VTD_GCMD, 4,
                        HV_VTD_GCMD_SRTP, 0, 0);
        HvEmitOperation(storage->Operations, &operationCount, unitIndex,
                        HV_EFI_IOMMU_OPERATION_POLL32_SET,
                        unit->RegisterBase + HV_VTD_GSTS, 4,
                        HV_VTD_GSTS_RTPS, HV_VTD_GSTS_RTPS, 0);
        /* SRTP selects the new hierarchy but does not replace the required
         * global context-cache and IOTLB invalidations before TE is set. */
        HvEmitOperation(storage->Operations, &operationCount, unitIndex,
                        HV_EFI_IOMMU_OPERATION_INVALIDATE,
                        0, 0, 0, 0,
                        HV_EFI_IOMMU_INVALIDATE_CONTEXT |
                        HV_EFI_IOMMU_INVALIDATE_IOTLB);
        if (configuration->EnableQueuedInvalidation != 0) {
            command |= HV_VTD_GCMD_QIE;
            desiredStatus |= HV_VTD_GSTS_QIES;
            HvEmitOperation(storage->Operations, &operationCount, unitIndex,
                            HV_EFI_IOMMU_OPERATION_WRITE64,
                            unit->RegisterBase + HV_VTD_IQA, 8,
                            configuration->InvalidationQueuePhysical, 0, 0);
            HvEmitOperation(storage->Operations, &operationCount, unitIndex,
                            HV_EFI_IOMMU_OPERATION_WRITE64,
                            unit->RegisterBase + HV_VTD_IQT, 8, 0, 0, 0);
            HvEmitOperation(storage->Operations, &operationCount, unitIndex,
                            HV_EFI_IOMMU_OPERATION_WRITE32,
                            unit->RegisterBase + HV_VTD_GCMD, 4,
                            command, 0, 0);
            HvEmitOperation(storage->Operations, &operationCount, unitIndex,
                            HV_EFI_IOMMU_OPERATION_POLL32_SET,
                            unit->RegisterBase + HV_VTD_GSTS, 4,
                            HV_VTD_GSTS_QIES, HV_VTD_GSTS_QIES, 0);
        }
        if (configuration->EnableInterruptRemapping != 0) {
            uint64_t irta = configuration->InterruptTablePhysical |
                (uint64_t)(HvLog2(configuration->InterruptEntryCount) - 1);
            if (configuration->EnableExtendedInterruptMode != 0) {
                irta |= HV_VTD_IRTA_EIME;
            }
            HvEmitOperation(storage->Operations, &operationCount, unitIndex,
                            HV_EFI_IOMMU_OPERATION_WRITE64,
                            unit->RegisterBase + HV_VTD_IRTA, 8,
                            irta, 0, 0);
            HvEmitOperation(storage->Operations, &operationCount, unitIndex,
                            HV_EFI_IOMMU_OPERATION_WRITE32,
                            unit->RegisterBase + HV_VTD_GCMD, 4,
                            command | HV_VTD_GCMD_SIRTP, 0, 0);
            HvEmitOperation(storage->Operations, &operationCount, unitIndex,
                            HV_EFI_IOMMU_OPERATION_POLL32_SET,
                            unit->RegisterBase + HV_VTD_GSTS, 4,
                            HV_VTD_GSTS_IRTPS, HV_VTD_GSTS_IRTPS, 0);
            command |= HV_VTD_GCMD_IRE;
            desiredStatus |= HV_VTD_GSTS_IRES;
            HvEmitOperation(storage->Operations, &operationCount, unitIndex,
                            HV_EFI_IOMMU_OPERATION_WRITE32,
                            unit->RegisterBase + HV_VTD_GCMD, 4,
                            command, 0, 0);
            HvEmitOperation(storage->Operations, &operationCount, unitIndex,
                            HV_EFI_IOMMU_OPERATION_POLL32_SET,
                            unit->RegisterBase + HV_VTD_GSTS, 4,
                            HV_VTD_GSTS_IRES, HV_VTD_GSTS_IRES, 0);
            HvEmitOperation(storage->Operations, &operationCount, unitIndex,
                            HV_EFI_IOMMU_OPERATION_INVALIDATE,
                            0, 0, 0, 0,
                            HV_EFI_IOMMU_INVALIDATE_INTERRUPT_ENTRY);
        }
        command |= HV_VTD_GCMD_TE;
        HvEmitOperation(storage->Operations, &operationCount, unitIndex,
                        HV_EFI_IOMMU_OPERATION_WRITE32,
                        unit->RegisterBase + HV_VTD_GCMD, 4,
                        command, 0, 0);
        HvEmitOperation(storage->Operations, &operationCount, unitIndex,
                        HV_EFI_IOMMU_OPERATION_POLL32_SET,
                        unit->RegisterBase + HV_VTD_GSTS, 4,
                        desiredStatus, desiredStatus, 0);
        HvEmitOperation(storage->Operations, &operationCount, unitIndex,
                        HV_EFI_IOMMU_OPERATION_VERIFY_OWNERSHIP,
                        0, 0, 0, 0, 0);
        program->OperationCount = operationCount - program->FirstOperation;
        program->RequiredStatusBits = desiredStatus;
    }
    if (operationCount != requiredOperations) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_STATE;
    }
    plan->Units = storage->Units;
    plan->UnitCount = dmar->UnitCount;
    plan->Operations = storage->Operations;
    plan->OperationCount = operationCount;
    plan->Ownership = *ownership;
    plan->PollLimit = pollLimit;
    plan->Reserved = 0;
    return HV_EFI_IOMMU_RUNTIME_OK;
}

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuRuntimeInitialize(
    HV_EFI_IOMMU_RUNTIME *runtime,
    const HV_EFI_IOMMU_PROGRAM_PLAN *plan,
    HV_EFI_IOMMU_UNIT_RUNTIME_STATE *unitStates,
    uint32_t unitStateCount)
{
    uint32_t unitIndex;
    uint32_t expectedOperation = 0;
    if (runtime == NULL || plan == NULL || plan->Units == NULL ||
        plan->UnitCount == 0 || plan->Operations == NULL ||
        plan->OperationCount == 0 || unitStates == NULL ||
        unitStateCount < plan->UnitCount || plan->PollLimit == 0 ||
        HvOwnershipValid(&plan->Ownership) != HV_EFI_IOMMU_RUNTIME_OK) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    for (unitIndex = 0; unitIndex < plan->UnitCount; ++unitIndex) {
        const HV_EFI_IOMMU_UNIT_PROGRAM *program = &plan->Units[unitIndex];
        uint32_t relative;
        if (program->FirstOperation != expectedOperation ||
            program->OperationCount == 0 ||
            program->OperationCount > plan->OperationCount -
                expectedOperation) {
            return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
        }
        for (relative = 0; relative < program->OperationCount; ++relative) {
            const HV_EFI_IOMMU_MMIO_OPERATION *operation =
                &plan->Operations[expectedOperation + relative];
            if (operation->UnitIndex != unitIndex ||
                operation->Kind <
                    HV_EFI_IOMMU_OPERATION_VERIFY_OWNERSHIP ||
                operation->Kind > HV_EFI_IOMMU_OPERATION_INVALIDATE) {
                return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
            }
        }
        expectedOperation += program->OperationCount;
    }
    if (expectedOperation != plan->OperationCount) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    HvZero(runtime, sizeof(*runtime));
    HvZero(unitStates,
           (size_t)plan->UnitCount * sizeof(unitStates[0]));
    runtime->Plan = plan;
    runtime->UnitStates = unitStates;
    runtime->UnitStateCount = plan->UnitCount;
    runtime->State = HV_EFI_IOMMU_RUNTIME_ADMITTED;
    runtime->FailedUnitIndex = UINT32_MAX;
    return HV_EFI_IOMMU_RUNTIME_OK;
}

static HV_EFI_IOMMU_RUNTIME_STATUS HvExecuteOperation(
    const HV_EFI_IOMMU_PROGRAM_PLAN *plan,
    const HV_EFI_IOMMU_MMIO_OPERATION *operation,
    const HV_EFI_IOMMU_RUNTIME_CALLBACKS *callbacks)
{
    uint64_t value;
    uint32_t attempt;
    if (operation->Kind == HV_EFI_IOMMU_OPERATION_VERIFY_OWNERSHIP) {
        return callbacks->OwnershipHeld(
            callbacks->Context, plan->Ownership.OwnerToken) != 0 ?
            HV_EFI_IOMMU_RUNTIME_OK :
            HV_EFI_IOMMU_RUNTIME_OWNERSHIP_UNAVAILABLE;
    }
    if (operation->Kind == HV_EFI_IOMMU_OPERATION_WRITE32 ||
        operation->Kind == HV_EFI_IOMMU_OPERATION_WRITE64) {
        return callbacks->Write(callbacks->Context, operation->Address,
                                operation->Width, operation->Value) != 0 ?
            HV_EFI_IOMMU_RUNTIME_OK :
            HV_EFI_IOMMU_RUNTIME_CALLBACK_FAILED;
    }
    if (operation->Kind == HV_EFI_IOMMU_OPERATION_INVALIDATE) {
        return callbacks->Invalidate(
            callbacks->Context, operation->UnitIndex,
            operation->InvalidationFlags) != 0 ?
            HV_EFI_IOMMU_RUNTIME_OK :
            HV_EFI_IOMMU_RUNTIME_CALLBACK_FAILED;
    }
    if (operation->Kind == HV_EFI_IOMMU_OPERATION_POLL32_SET) {
        for (attempt = 0; attempt < plan->PollLimit; ++attempt) {
            if (callbacks->Read(callbacks->Context, operation->Address,
                                4, &value) == 0) {
                return HV_EFI_IOMMU_RUNTIME_CALLBACK_FAILED;
            }
            if ((value & operation->Mask) == operation->Value) {
                return HV_EFI_IOMMU_RUNTIME_OK;
            }
        }
        return HV_EFI_IOMMU_RUNTIME_TIMEOUT;
    }
    return HV_EFI_IOMMU_RUNTIME_INVALID_STATE;
}

static HV_EFI_IOMMU_RUNTIME_STATUS HvPollClear(
    const HV_EFI_IOMMU_RUNTIME_CALLBACKS *callbacks,
    uint64_t address,
    uint32_t mask,
    uint32_t limit)
{
    uint32_t attempt;
    uint64_t value;
    for (attempt = 0; attempt < limit; ++attempt) {
        if (callbacks->Read(callbacks->Context, address, 4, &value) == 0) {
            return HV_EFI_IOMMU_RUNTIME_CALLBACK_FAILED;
        }
        if ((value & mask) == 0) {
            return HV_EFI_IOMMU_RUNTIME_OK;
        }
    }
    return HV_EFI_IOMMU_RUNTIME_TIMEOUT;
}

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuRuntimeRollback(
    HV_EFI_IOMMU_RUNTIME *runtime,
    const HV_EFI_IOMMU_RUNTIME_CALLBACKS *callbacks)
{
    uint32_t reverse;
    int failed = 0;
    if (runtime == NULL || runtime->Plan == NULL || callbacks == NULL ||
        callbacks->Read == NULL || callbacks->Write == NULL ||
        callbacks->Invalidate == NULL || callbacks->OwnershipHeld == NULL ||
        callbacks->DmaQuiesced == NULL) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    if (runtime->State != HV_EFI_IOMMU_RUNTIME_ENABLING &&
        runtime->State != HV_EFI_IOMMU_RUNTIME_ENABLED &&
        runtime->State != HV_EFI_IOMMU_RUNTIME_FAILED) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_STATE;
    }
    if (callbacks->OwnershipHeld(
            callbacks->Context,
            runtime->Plan->Ownership.OwnerToken) == 0 ||
        callbacks->DmaQuiesced(callbacks->Context) == 0) {
        runtime->State = HV_EFI_IOMMU_RUNTIME_FAILED;
        return HV_EFI_IOMMU_RUNTIME_ROLLBACK_FAILED;
    }
    for (reverse = runtime->Plan->UnitCount; reverse != 0; --reverse) {
        const uint32_t unitIndex = reverse - 1;
        const HV_EFI_IOMMU_UNIT_PROGRAM *program =
            &runtime->Plan->Units[unitIndex];
        HV_EFI_IOMMU_UNIT_RUNTIME_STATE *state =
            &runtime->UnitStates[unitIndex];
        if (state->CompletedOperations == 0 && state->Enabled == 0) {
            state->RollbackComplete = 1;
            continue;
        }
        if (callbacks->Write(callbacks->Context,
                             program->RegisterBase + HV_VTD_GCMD,
                             4, 0) == 0 ||
            HvPollClear(callbacks,
                        program->RegisterBase + HV_VTD_GSTS,
                        HV_VTD_GSTS_TES | HV_VTD_GSTS_QIES |
                            HV_VTD_GSTS_IRES,
                        runtime->Plan->PollLimit) !=
                HV_EFI_IOMMU_RUNTIME_OK ||
            callbacks->Invalidate(
                callbacks->Context, unitIndex,
                HV_EFI_IOMMU_INVALIDATE_CONTEXT |
                HV_EFI_IOMMU_INVALIDATE_IOTLB |
                HV_EFI_IOMMU_INVALIDATE_INTERRUPT_ENTRY) == 0 ||
            callbacks->Write(callbacks->Context,
                             program->RegisterBase + HV_VTD_RTADDR,
                             8, program->OriginalRootTablePhysical) == 0 ||
            callbacks->Write(callbacks->Context,
                             program->RegisterBase + HV_VTD_GCMD,
                             4, HV_VTD_GCMD_SRTP) == 0 ||
            HvExecuteOperation(
                runtime->Plan,
                &(HV_EFI_IOMMU_MMIO_OPERATION){
                    program->RegisterBase + HV_VTD_GSTS,
                    HV_VTD_GSTS_RTPS,
                    HV_VTD_GSTS_RTPS,
                    unitIndex,
                    HV_EFI_IOMMU_OPERATION_POLL32_SET,
                    4,
                    0
                },
                callbacks) != HV_EFI_IOMMU_RUNTIME_OK) {
            failed = 1;
            continue;
        }
        state->CompletedOperations = 0;
        state->Enabled = 0;
        state->RollbackComplete = 1;
    }
    runtime->EnabledUnitCount = 0;
    runtime->State = failed ? HV_EFI_IOMMU_RUNTIME_FAILED :
        HV_EFI_IOMMU_RUNTIME_ROLLED_BACK;
    return failed ? HV_EFI_IOMMU_RUNTIME_ROLLBACK_FAILED :
        HV_EFI_IOMMU_RUNTIME_OK;
}

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuRuntimeEnable(
    HV_EFI_IOMMU_RUNTIME *runtime,
    const HV_EFI_IOMMU_RUNTIME_CALLBACKS *callbacks)
{
    uint32_t unitIndex;
    if (runtime == NULL || runtime->Plan == NULL || callbacks == NULL ||
        callbacks->Read == NULL || callbacks->Write == NULL ||
        callbacks->Invalidate == NULL || callbacks->OwnershipHeld == NULL ||
        callbacks->DmaQuiesced == NULL) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    if (runtime->State != HV_EFI_IOMMU_RUNTIME_ADMITTED) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_STATE;
    }
    if (callbacks->DmaQuiesced(callbacks->Context) == 0) {
        return HV_EFI_IOMMU_RUNTIME_OWNERSHIP_UNAVAILABLE;
    }
    runtime->State = HV_EFI_IOMMU_RUNTIME_ENABLING;
    for (unitIndex = 0; unitIndex < runtime->Plan->UnitCount; ++unitIndex) {
        const HV_EFI_IOMMU_UNIT_PROGRAM *program =
            &runtime->Plan->Units[unitIndex];
        uint32_t relative;
        for (relative = 0; relative < program->OperationCount; ++relative) {
            const HV_EFI_IOMMU_MMIO_OPERATION *operation =
                &runtime->Plan->Operations[
                    program->FirstOperation + relative];
            const HV_EFI_IOMMU_RUNTIME_STATUS status =
                HvExecuteOperation(runtime->Plan, operation, callbacks);
            if (status != HV_EFI_IOMMU_RUNTIME_OK) {
                runtime->FailedUnitIndex = unitIndex;
                runtime->State = HV_EFI_IOMMU_RUNTIME_FAILED;
                if (HvEfiIommuRuntimeRollback(runtime, callbacks) !=
                    HV_EFI_IOMMU_RUNTIME_OK) {
                    return HV_EFI_IOMMU_RUNTIME_ROLLBACK_FAILED;
                }
                return status;
            }
            runtime->UnitStates[unitIndex].CompletedOperations = relative + 1;
        }
        runtime->UnitStates[unitIndex].Enabled = 1;
        ++runtime->EnabledUnitCount;
    }
    runtime->State = HV_EFI_IOMMU_RUNTIME_ENABLED;
    return HV_EFI_IOMMU_RUNTIME_OK;
}

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuRuntimeAudit(
    HV_EFI_IOMMU_RUNTIME *runtime,
    const HV_EFI_IOMMU_RUNTIME_CALLBACKS *callbacks)
{
    uint32_t unitIndex;
    if (runtime == NULL || runtime->Plan == NULL || callbacks == NULL ||
        callbacks->Read == NULL || callbacks->OwnershipHeld == NULL) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    if (runtime->State != HV_EFI_IOMMU_RUNTIME_ENABLED) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_STATE;
    }
    if (callbacks->OwnershipHeld(
            callbacks->Context,
            runtime->Plan->Ownership.OwnerToken) == 0) {
        runtime->State = HV_EFI_IOMMU_RUNTIME_FAILED;
        return HV_EFI_IOMMU_RUNTIME_OWNERSHIP_UNAVAILABLE;
    }
    for (unitIndex = 0; unitIndex < runtime->Plan->UnitCount; ++unitIndex) {
        const HV_EFI_IOMMU_UNIT_PROGRAM *program =
            &runtime->Plan->Units[unitIndex];
        uint64_t root;
        uint64_t status;
        uint64_t faults;
        if (callbacks->Read(callbacks->Context,
                            program->RegisterBase + HV_VTD_RTADDR,
                            8, &root) == 0 ||
            callbacks->Read(callbacks->Context,
                            program->RegisterBase + HV_VTD_GSTS,
                            4, &status) == 0 ||
            callbacks->Read(callbacks->Context,
                            program->RegisterBase + HV_VTD_FSTS,
                            4, &faults) == 0 ||
            root != program->RootTablePhysical ||
            ((uint32_t)status & program->RequiredStatusBits) !=
                program->RequiredStatusBits ||
            (uint32_t)faults != 0) {
            runtime->FailedUnitIndex = unitIndex;
            runtime->State = HV_EFI_IOMMU_RUNTIME_FAILED;
            return HV_EFI_IOMMU_RUNTIME_AUDIT_FAILED;
        }
    }
    return HV_EFI_IOMMU_RUNTIME_OK;
}

static int HvEvidenceBooleanValid(
    const HV_EFI_DMA_ISOLATION_EVIDENCE *evidence)
{
    const uint32_t *values = (const uint32_t *)evidence;
    uint32_t index;
    /* The three CPU identity fields and processor count are not booleans. */
    for (index = 0; index < 2; ++index) {
        if (values[index] > 1) {
            return 0;
        }
    }
    for (index = 6; index <
         (uint32_t)(sizeof(*evidence) / sizeof(uint32_t)); ++index) {
        if (values[index] > 1) {
            return 0;
        }
    }
    return 1;
}

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuEvaluateDmaIsolation(
    const HV_EFI_DMA_ISOLATION_EVIDENCE *evidence,
    uint32_t *mode)
{
    if (mode != NULL) {
        *mode = HV_EFI_DMA_ISOLATION_UNAVAILABLE;
    }
    if (evidence == NULL || mode == NULL ||
        !HvEvidenceBooleanValid(evidence) ||
        evidence->EnabledProcessorCount == 0 ||
        evidence->EnabledProcessorCount > 64) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    if (evidence->OuterHypervisorPresent != 0) {
        if (evidence->OuterDmaIsolationAttested == 0 ||
            evidence->RootTablesExcludePrivatePages == 0) {
            return HV_EFI_IOMMU_RUNTIME_OWNERSHIP_UNAVAILABLE;
        }
        *mode = HV_EFI_DMA_ISOLATION_EXTERNAL_VMM;
        return HV_EFI_IOMMU_RUNTIME_OK;
    }
    /* The only bare-metal profile admitted by this release is the user's
     * verified Alder Lake-S topology.  Other systems fail closed. */
    if (evidence->PinnedCpuFamily != 6 ||
        evidence->PinnedCpuModel != 151 ||
        evidence->PinnedCpuStepping != 2 ||
        evidence->EnabledProcessorCount != 20) {
        return HV_EFI_IOMMU_RUNTIME_PROFILE_MISMATCH;
    }
    if (evidence->DmarParsed == 0 ||
        evidence->AllDmaSourcesCovered == 0 ||
        evidence->RootTablesExcludePrivatePages == 0 ||
        evidence->RemappingTablesGuestDenied == 0 ||
        evidence->RemappingTablesDmaDenied == 0 ||
        evidence->DmarMmioGuestDenied == 0 ||
        evidence->ContinuousAuditEnabled == 0 ||
        evidence->DmaQuiesced == 0 ||
        evidence->RollbackAvailable == 0) {
        return HV_EFI_IOMMU_RUNTIME_OWNERSHIP_UNAVAILABLE;
    }
    if (evidence->PlatformLockEngaged != 0) {
        *mode = HV_EFI_DMA_ISOLATION_VTD_LOCKED;
        return HV_EFI_IOMMU_RUNTIME_OK;
    }
    if (evidence->MmioMediatorComplete != 0) {
        *mode = HV_EFI_DMA_ISOLATION_VTD_MEDIATED;
        return HV_EFI_IOMMU_RUNTIME_OK;
    }
    return HV_EFI_IOMMU_RUNTIME_PHYSICAL_MEDIATION_REQUIRED;
}

static HV_EFI_IOMMU_RUNTIME_STATUS HvAppendProtectedRange(
    HV_EFI_IOMMU_PROTECTED_RANGE *output,
    uint32_t capacity,
    uint32_t *count,
    uint64_t base,
    uint64_t size,
    uint32_t kind)
{
    uint64_t end;
    uint64_t alignedBase;
    uint64_t alignedEnd;
    uint32_t index;
    if (size == 0 || !HvAddU64(base, size, &end)) {
        return HV_EFI_IOMMU_RUNTIME_RANGE_OVERFLOW;
    }
    alignedBase = base & ~HV_EFI_IOMMU_RUNTIME_PAGE_MASK;
    if (!HvAddU64(end, HV_EFI_IOMMU_RUNTIME_PAGE_MASK, &alignedEnd)) {
        return HV_EFI_IOMMU_RUNTIME_RANGE_OVERFLOW;
    }
    alignedEnd &= ~HV_EFI_IOMMU_RUNTIME_PAGE_MASK;
    for (index = 0; index < *count; ++index) {
        if (output[index].PhysicalBase == alignedBase &&
            output[index].Size == alignedEnd - alignedBase &&
            output[index].Kind == kind) {
            return HV_EFI_IOMMU_RUNTIME_OK;
        }
    }
    if (*count >= capacity) {
        return HV_EFI_IOMMU_RUNTIME_CAPACITY;
    }
    output[*count].PhysicalBase = alignedBase;
    output[*count].Size = alignedEnd - alignedBase;
    output[*count].Kind = kind;
    output[*count].Reserved = 0;
    ++*count;
    return HV_EFI_IOMMU_RUNTIME_OK;
}

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuBuildProtectedInventory(
    const HV_EFI_IOMMU_DMAR_INFO *dmar,
    const HV_EFI_IOMMU_UNIT_CONFIGURATION *configurations,
    uint32_t configurationCount,
    const HV_EFI_IOMMU_PROTECTED_RANGE *privateRanges,
    uint32_t privateRangeCount,
    HV_EFI_IOMMU_PROTECTED_RANGE *output,
    uint32_t outputCapacity,
    uint32_t *outputCount)
{
    uint32_t index;
    HV_EFI_IOMMU_RUNTIME_STATUS status;
    if (outputCount != NULL) {
        *outputCount = 0;
    }
    if (dmar == NULL || configurations == NULL || output == NULL ||
        outputCount == NULL || dmar->Units == NULL || dmar->UnitCount == 0 ||
        configurationCount != dmar->UnitCount || outputCapacity == 0 ||
        (privateRangeCount != 0 && privateRanges == NULL)) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    for (index = 0; index < privateRangeCount; ++index) {
        if (privateRanges[index].Kind != HV_EFI_IOMMU_PROTECTED_PRIVATE ||
            privateRanges[index].Reserved != 0) {
            return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
        }
        status = HvAppendProtectedRange(output, outputCapacity, outputCount,
            privateRanges[index].PhysicalBase, privateRanges[index].Size,
            HV_EFI_IOMMU_PROTECTED_PRIVATE);
        if (status != HV_EFI_IOMMU_RUNTIME_OK) {
            *outputCount = 0;
            return status;
        }
    }
    for (index = 0; index < dmar->UnitCount; ++index) {
        const HV_EFI_IOMMU_UNIT_CONFIGURATION *configuration =
            &configurations[index];
        if (configuration->RootTablePhysical == 0 ||
            (configuration->RootTablePhysical &
             HV_EFI_IOMMU_RUNTIME_PAGE_MASK) != 0 ||
            !HvBooleanValid(configuration->EnableQueuedInvalidation) ||
            !HvBooleanValid(configuration->EnableInterruptRemapping) ||
            (configuration->EnableQueuedInvalidation != 0 &&
             (configuration->InvalidationQueuePhysical == 0 ||
              (configuration->InvalidationQueuePhysical &
               HV_EFI_IOMMU_RUNTIME_PAGE_MASK) != 0)) ||
            (configuration->EnableInterruptRemapping != 0 &&
             (configuration->InterruptTablePhysical == 0 ||
              (configuration->InterruptTablePhysical &
               HV_EFI_IOMMU_RUNTIME_PAGE_MASK) != 0 ||
              !HvPowerOfTwo(configuration->InterruptEntryCount) ||
              configuration->InterruptEntryCount < 16 ||
              configuration->InterruptEntryCount > 32768))) {
            *outputCount = 0;
            return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
        }
        status = HvAppendProtectedRange(output, outputCapacity, outputCount,
            dmar->Units[index].RegisterBase,
            HV_EFI_IOMMU_RUNTIME_PAGE_SIZE, HV_EFI_IOMMU_PROTECTED_MMIO);
        if (status == HV_EFI_IOMMU_RUNTIME_OK) {
            status = HvAppendProtectedRange(output, outputCapacity, outputCount,
                configuration->RootTablePhysical,
                HV_EFI_IOMMU_RUNTIME_PAGE_SIZE,
                HV_EFI_IOMMU_PROTECTED_REMAP_TABLE);
        }
        if (status == HV_EFI_IOMMU_RUNTIME_OK &&
            configuration->EnableQueuedInvalidation != 0) {
            status = HvAppendProtectedRange(output, outputCapacity, outputCount,
                configuration->InvalidationQueuePhysical,
                HV_EFI_IOMMU_RUNTIME_PAGE_SIZE,
                HV_EFI_IOMMU_PROTECTED_REMAP_TABLE);
        }
        if (status == HV_EFI_IOMMU_RUNTIME_OK &&
            configuration->EnableInterruptRemapping != 0) {
            status = HvAppendProtectedRange(output, outputCapacity, outputCount,
                configuration->InterruptTablePhysical,
                (uint64_t)configuration->InterruptEntryCount * 16,
                HV_EFI_IOMMU_PROTECTED_REMAP_TABLE);
        }
        if (status != HV_EFI_IOMMU_RUNTIME_OK) {
            *outputCount = 0;
            return status;
        }
    }
    return HV_EFI_IOMMU_RUNTIME_OK;
}

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuClassifyMmioAccess(
    const HV_EFI_IOMMU_DMAR_INFO *dmar,
    uint64_t physicalAddress,
    uint32_t width,
    uint32_t access,
    uint32_t *action,
    uint32_t *unitIndex)
{
    uint32_t index;
    if (action != NULL) {
        *action = HV_EFI_IOMMU_MMIO_FATAL;
    }
    if (unitIndex != NULL) {
        *unitIndex = UINT32_MAX;
    }
    if (dmar == NULL || dmar->Units == NULL || action == NULL ||
        unitIndex == NULL || (width != 4 && width != 8) ||
        (access != HV_EFI_IOMMU_MMIO_READ &&
         access != HV_EFI_IOMMU_MMIO_WRITE)) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    for (index = 0; index < dmar->UnitCount; ++index) {
        const uint64_t base = dmar->Units[index].RegisterBase;
        if (physicalAddress >= base &&
            physicalAddress - base < HV_EFI_IOMMU_RUNTIME_PAGE_SIZE &&
            (uint64_t)width <= HV_EFI_IOMMU_RUNTIME_PAGE_SIZE -
                (physicalAddress - base)) {
            const uint64_t offset = physicalAddress - base;
            *unitIndex = index;
            if (access == HV_EFI_IOMMU_MMIO_WRITE) {
                /* No guest write is forwarded.  In particular GCMD, RTADDR,
                 * IQA/IQT and IRTA cannot replace hypervisor ownership. */
                *action = HV_EFI_IOMMU_MMIO_DENY_WRITE;
                return HV_EFI_IOMMU_RUNTIME_OK;
            }
            if ((offset == HV_VTD_VERSION && width == 4) ||
                (offset == HV_VTD_CAP && width == 8) ||
                (offset == HV_VTD_ECAP && width == 8) ||
                (offset == HV_VTD_GSTS && width == 4) ||
                (offset == HV_VTD_RTADDR && width == 8) ||
                (offset == HV_VTD_FSTS && width == 4) ||
                (offset == HV_VTD_IQT && width == 8) ||
                (offset == HV_VTD_IQA && width == 8) ||
                (offset == HV_VTD_IRTA && width == 8)) {
                *action = HV_EFI_IOMMU_MMIO_EMULATE_READ;
                return HV_EFI_IOMMU_RUNTIME_OK;
            }
            return HV_EFI_IOMMU_RUNTIME_UNSUPPORTED_SCOPE;
        }
    }
    return HV_EFI_IOMMU_RUNTIME_OUT_OF_BOUNDS;
}

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuValidateDetachRendezvous(
    uint32_t enabledProcessorCount,
    uint64_t preparedMask,
    uint64_t acknowledgedMask)
{
    uint64_t expected;
    if (enabledProcessorCount == 0 || enabledProcessorCount > 64) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    expected = enabledProcessorCount == 64 ? UINT64_MAX :
        (UINT64_C(1) << enabledProcessorCount) - UINT64_C(1);
    if ((preparedMask & ~expected) != 0 ||
        (acknowledgedMask & ~expected) != 0 ||
        preparedMask != expected || acknowledgedMask != expected) {
        return HV_EFI_IOMMU_RUNTIME_CPU_RENDEZVOUS_INCOMPLETE;
    }
    return HV_EFI_IOMMU_RUNTIME_OK;
}

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuDecodeMmioInstruction(
    const uint8_t *instruction,
    uint32_t availableBytes,
    HV_EFI_IOMMU_MMIO_DECODE *decoded)
{
    uint32_t offset = 0;
    uint32_t rex = 0;
    uint32_t opcode;
    uint32_t modrm;
    uint32_t mod;
    uint32_t rm;
    uint32_t displacement = 0;
    if (decoded != NULL) {
        HvZero(decoded, sizeof(*decoded));
    }
    if (instruction == NULL || decoded == NULL || availableBytes == 0) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    if (availableBytes > 15) {
        availableBytes = 15;
    }
    if (instruction[offset] >= UINT8_C(0x40) &&
        instruction[offset] <= UINT8_C(0x4f)) {
        rex = instruction[offset++];
        if (offset == availableBytes) {
            return HV_EFI_IOMMU_RUNTIME_DECODE_UNSUPPORTED;
        }
    }
    /* LOCK/REP, segment, operand/address overrides, VEX/EVEX, atomics and
     * string forms are outside the exact volatile MOV contract. */
    if (instruction[offset] == UINT8_C(0x66) ||
        instruction[offset] == UINT8_C(0x67) ||
        instruction[offset] == UINT8_C(0xf0) ||
        instruction[offset] == UINT8_C(0xf2) ||
        instruction[offset] == UINT8_C(0xf3) ||
        instruction[offset] == UINT8_C(0x26) ||
        instruction[offset] == UINT8_C(0x2e) ||
        instruction[offset] == UINT8_C(0x36) ||
        instruction[offset] == UINT8_C(0x3e) ||
        instruction[offset] == UINT8_C(0x64) ||
        instruction[offset] == UINT8_C(0x65)) {
        return HV_EFI_IOMMU_RUNTIME_DECODE_UNSUPPORTED;
    }
    opcode = instruction[offset++];
    if (opcode != UINT8_C(0x8b) && opcode != UINT8_C(0x89)) {
        return HV_EFI_IOMMU_RUNTIME_DECODE_UNSUPPORTED;
    }
    if (offset == availableBytes) {
        return HV_EFI_IOMMU_RUNTIME_DECODE_UNSUPPORTED;
    }
    modrm = instruction[offset++];
    mod = modrm >> 6;
    rm = modrm & 7u;
    if (mod == 3) {
        return HV_EFI_IOMMU_RUNTIME_DECODE_UNSUPPORTED;
    }
    if (rm == 4) {
        uint32_t sib;
        if (offset == availableBytes) {
            return HV_EFI_IOMMU_RUNTIME_DECODE_UNSUPPORTED;
        }
        sib = instruction[offset++];
        if (mod == 0 && (sib & 7u) == 5u) {
            displacement = 4;
        }
    } else if (mod == 0 && rm == 5) {
        displacement = 4;
    }
    if (mod == 1) {
        displacement = 1;
    } else if (mod == 2) {
        displacement = 4;
    }
    if (displacement > availableBytes - offset) {
        return HV_EFI_IOMMU_RUNTIME_DECODE_UNSUPPORTED;
    }
    offset += displacement;
    decoded->Access = opcode == UINT8_C(0x8b) ?
        HV_EFI_IOMMU_MMIO_READ : HV_EFI_IOMMU_MMIO_WRITE;
    decoded->Width = (rex & UINT32_C(8)) != 0 ? 8 : 4;
    decoded->RegisterIndex = ((modrm >> 3) & 7u) |
        ((rex & UINT32_C(4)) != 0 ? 8u : 0u);
    decoded->InstructionLength = offset;
    return HV_EFI_IOMMU_RUNTIME_OK;
}

static int HvCanonical48(uint64_t address)
{
    const uint64_t upper = address >> 48;
    return upper == 0 || upper == UINT64_C(0xffff);
}

static HV_EFI_IOMMU_RUNTIME_STATUS HvWalkReadEntry(
    const HV_EFI_IOMMU_PAGE_WALK *walk,
    uint64_t table,
    uint32_t index,
    uint64_t addressMask,
    uint64_t *entry)
{
    uint64_t address;
    if ((table & HV_EFI_IOMMU_RUNTIME_PAGE_MASK) != 0 || index >= 512 ||
        !HvAddU64(table, (uint64_t)index * 8, &address) ||
        address > addressMask + HV_EFI_IOMMU_RUNTIME_PAGE_MASK - 7 ||
        walk->ReadPhysical64(walk->Context, address, entry) == 0 ||
        (*entry & UINT64_C(1)) == 0) {
        return HV_EFI_IOMMU_RUNTIME_PAGE_WALK_FAILED;
    }
    return HV_EFI_IOMMU_RUNTIME_OK;
}

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuWalkGuestLinear(
    const HV_EFI_IOMMU_PAGE_WALK *walk,
    uint64_t guestCr3,
    uint64_t guestLinear,
    uint32_t accessSize,
    uint64_t *guestPhysical)
{
    uint64_t addressMask;
    uint64_t entry;
    uint64_t table;
    HV_EFI_IOMMU_RUNTIME_STATUS status;
    if (guestPhysical != NULL) {
        *guestPhysical = 0;
    }
    if (walk == NULL || walk->ReadPhysical64 == NULL ||
        guestPhysical == NULL || walk->La57Enabled != 0 ||
        walk->PhysicalAddressBits < 39 || walk->PhysicalAddressBits > 52 ||
        accessSize == 0 || accessSize > 15 || !HvCanonical48(guestLinear) ||
        (guestLinear & HV_EFI_IOMMU_RUNTIME_PAGE_MASK) >
            HV_EFI_IOMMU_RUNTIME_PAGE_SIZE - accessSize) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    addressMask = ((UINT64_C(1) << walk->PhysicalAddressBits) - 1) &
        ~HV_EFI_IOMMU_RUNTIME_PAGE_MASK;
    table = guestCr3 & addressMask;
    status = HvWalkReadEntry(walk, table,
        (uint32_t)((guestLinear >> 39) & UINT64_C(0x1ff)),
        addressMask, &entry);
    if (status != HV_EFI_IOMMU_RUNTIME_OK) {
        return status;
    }
    table = entry & addressMask;
    status = HvWalkReadEntry(walk, table,
        (uint32_t)((guestLinear >> 30) & UINT64_C(0x1ff)),
        addressMask, &entry);
    if (status != HV_EFI_IOMMU_RUNTIME_OK) {
        return status;
    }
    if ((entry & (UINT64_C(1) << 7)) != 0) {
        const uint64_t base = entry & addressMask &
            ~((UINT64_C(1) << 30) - 1);
        *guestPhysical = base | (guestLinear &
            ((UINT64_C(1) << 30) - 1));
        return HV_EFI_IOMMU_RUNTIME_OK;
    }
    table = entry & addressMask;
    status = HvWalkReadEntry(walk, table,
        (uint32_t)((guestLinear >> 21) & UINT64_C(0x1ff)),
        addressMask, &entry);
    if (status != HV_EFI_IOMMU_RUNTIME_OK) {
        return status;
    }
    if ((entry & (UINT64_C(1) << 7)) != 0) {
        const uint64_t base = entry & addressMask &
            ~((UINT64_C(1) << 21) - 1);
        *guestPhysical = base | (guestLinear &
            ((UINT64_C(1) << 21) - 1));
        return HV_EFI_IOMMU_RUNTIME_OK;
    }
    table = entry & addressMask;
    status = HvWalkReadEntry(walk, table,
        (uint32_t)((guestLinear >> 12) & UINT64_C(0x1ff)),
        addressMask, &entry);
    if (status != HV_EFI_IOMMU_RUNTIME_OK ||
        (entry & (UINT64_C(1) << 7)) != 0) {
        return HV_EFI_IOMMU_RUNTIME_PAGE_WALK_FAILED;
    }
    *guestPhysical = (entry & addressMask) |
        (guestLinear & HV_EFI_IOMMU_RUNTIME_PAGE_MASK);
    return HV_EFI_IOMMU_RUNTIME_OK;
}

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuShadowAccess(
    HV_EFI_IOMMU_REGISTER_SHADOW *shadow,
    uint64_t physicalAddress,
    uint32_t width,
    uint32_t access,
    uint64_t *value)
{
    uint64_t offset;
    uint64_t current;
    if (shadow == NULL || value == NULL || shadow->RegisterBase == 0 ||
        physicalAddress < shadow->RegisterBase ||
        (width != 4 && width != 8) ||
        (access != HV_EFI_IOMMU_MMIO_READ &&
         access != HV_EFI_IOMMU_MMIO_WRITE)) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    offset = physicalAddress - shadow->RegisterBase;
    if (offset >= HV_EFI_IOMMU_RUNTIME_PAGE_SIZE ||
        (uint64_t)width > HV_EFI_IOMMU_RUNTIME_PAGE_SIZE - offset) {
        return HV_EFI_IOMMU_RUNTIME_OUT_OF_BOUNDS;
    }
    current = 0;
    if (offset == HV_VTD_VERSION && width == 4) {
        current = shadow->Version;
    } else if (offset == HV_VTD_CAP && width == 8) {
        current = shadow->Capability;
    } else if (offset == HV_VTD_ECAP && width == 8) {
        current = shadow->ExtendedCapability;
    } else if (offset == HV_VTD_GCMD && width == 4) {
        current = shadow->GlobalCommand;
    } else if (offset == HV_VTD_GSTS && width == 4) {
        current = shadow->GlobalStatus;
    } else if (offset == HV_VTD_RTADDR && width == 8) {
        current = shadow->RootTablePhysical;
    } else if (offset == HV_VTD_FSTS && width == 4) {
        current = shadow->FaultStatus;
    } else if (offset == HV_VTD_IQT && width == 8) {
        current = shadow->InvalidationQueueTail;
    } else if (offset == HV_VTD_IQA && width == 8) {
        current = shadow->InvalidationQueuePhysical;
    } else if (offset == HV_VTD_IRTA && width == 8) {
        current = shadow->InterruptTablePhysical;
    } else {
        return HV_EFI_IOMMU_RUNTIME_DECODE_UNSUPPORTED;
    }
    if (access == HV_EFI_IOMMU_MMIO_READ) {
        *value = width == 4 ? (uint32_t)current : current;
        return HV_EFI_IOMMU_RUNTIME_OK;
    }
    if (offset == HV_VTD_FSTS && width == 4) {
        shadow->FaultStatus &= ~(uint32_t)*value;
        return HV_EFI_IOMMU_RUNTIME_OK;
    }
    if (offset == HV_VTD_GCMD && width == 4) {
        const uint32_t ownershipBits =
            HV_VTD_GCMD_TE | HV_VTD_GCMD_QIE | HV_VTD_GCMD_IRE;
        if (((uint32_t)*value & ownershipBits) ==
            (shadow->GlobalCommand & ownershipBits)) {
            /* SRTP/SIRTP are acknowledged against the immutable root/IR
             * pointers; no guest-supplied pointer reaches hardware. */
            shadow->GlobalCommand = (shadow->GlobalCommand & ownershipBits) |
                ((uint32_t)*value & (HV_VTD_GCMD_SRTP | HV_VTD_GCMD_SIRTP));
            return HV_EFI_IOMMU_RUNTIME_OK;
        }
    }
    ++shadow->OwnershipViolationCount;
    return HV_EFI_IOMMU_RUNTIME_TAKEOVER_BLOCKED;
}

static void HvCopyBytes(void *destination, const void *source, size_t size)
{
    uint8_t *destinationBytes = (uint8_t *)destination;
    const uint8_t *sourceBytes = (const uint8_t *)source;
    size_t index;
    for (index = 0; index < size; ++index) {
        destinationBytes[index] = sourceBytes[index];
    }
}

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuBuildGuestAcpiShadow(
    const HV_EFI_IOMMU_PHYSICAL_VIEW *view,
    const HV_EFI_IOMMU_DMAR_INFO *dmar,
    uint64_t hostRsdpPage,
    uint8_t *rsdpPage,
    uint64_t hostXsdtPage,
    uint8_t *xsdtPage,
    HV_EFI_IOMMU_ACPI_SHADOW *shadow)
{
    const uint8_t *sourceRsdpPage;
    const uint8_t *sourceXsdtPage;
    uint64_t guestRsdpPage;
    uint64_t guestXsdtPage;
    uint32_t xsdtOffset;
    uint32_t xsdtLength;
    uint32_t entryCount;
    uint32_t sourceIndex;
    uint32_t destinationIndex = 0;
    uint8_t *xsdt;
    HV_EFI_IOMMU_RUNTIME_STATUS status;
    if (shadow != NULL) {
        HvZero(shadow, sizeof(*shadow));
    }
    if (view == NULL || dmar == NULL || rsdpPage == NULL ||
        xsdtPage == NULL || shadow == NULL || dmar->RsdpPhysical == 0 ||
        dmar->XsdtPhysical == 0 || dmar->DmarPhysical == 0 ||
        hostRsdpPage == 0 || hostXsdtPage == 0 ||
        (hostRsdpPage & HV_EFI_IOMMU_RUNTIME_PAGE_MASK) != 0 ||
        (hostXsdtPage & HV_EFI_IOMMU_RUNTIME_PAGE_MASK) != 0 ||
        hostRsdpPage == hostXsdtPage) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    guestRsdpPage = dmar->RsdpPhysical &
        ~HV_EFI_IOMMU_RUNTIME_PAGE_MASK;
    guestXsdtPage = dmar->XsdtPhysical &
        ~HV_EFI_IOMMU_RUNTIME_PAGE_MASK;
    if (guestRsdpPage == guestXsdtPage) {
        return HV_EFI_IOMMU_RUNTIME_UNSUPPORTED_SCOPE;
    }
    status = HvMapPhysical(view, guestRsdpPage,
                           HV_EFI_IOMMU_RUNTIME_PAGE_SIZE,
                           &sourceRsdpPage);
    if (status != HV_EFI_IOMMU_RUNTIME_OK) {
        return status;
    }
    status = HvMapPhysical(view, guestXsdtPage,
                           HV_EFI_IOMMU_RUNTIME_PAGE_SIZE,
                           &sourceXsdtPage);
    if (status != HV_EFI_IOMMU_RUNTIME_OK) {
        return status;
    }
    xsdtOffset = (uint32_t)(dmar->XsdtPhysical - guestXsdtPage);
    if (xsdtOffset > HV_EFI_IOMMU_RUNTIME_PAGE_SIZE - HV_ACPI_HEADER_SIZE) {
        return HV_EFI_IOMMU_RUNTIME_UNSUPPORTED_SCOPE;
    }
    xsdtLength = HvRead32(sourceXsdtPage + xsdtOffset + 4);
    if (xsdtLength < HV_ACPI_HEADER_SIZE ||
        xsdtLength > HV_EFI_IOMMU_RUNTIME_PAGE_SIZE - xsdtOffset ||
        ((xsdtLength - HV_ACPI_HEADER_SIZE) & 7u) != 0) {
        return HV_EFI_IOMMU_RUNTIME_UNSUPPORTED_SCOPE;
    }
    HvCopyBytes(rsdpPage, sourceRsdpPage,
                (size_t)HV_EFI_IOMMU_RUNTIME_PAGE_SIZE);
    HvCopyBytes(xsdtPage, sourceXsdtPage,
                (size_t)HV_EFI_IOMMU_RUNTIME_PAGE_SIZE);
    xsdt = xsdtPage + xsdtOffset;
    entryCount = (xsdtLength - HV_ACPI_HEADER_SIZE) / 8;
    for (sourceIndex = 0; sourceIndex < entryCount; ++sourceIndex) {
        const uint64_t tablePhysical = HvRead64(
            sourceXsdtPage + xsdtOffset + HV_ACPI_HEADER_SIZE +
            sourceIndex * 8);
        if (tablePhysical == dmar->DmarPhysical) {
            continue;
        }
        if (destinationIndex != sourceIndex) {
            uint32_t byteIndex;
            for (byteIndex = 0; byteIndex < 8; ++byteIndex) {
                xsdt[HV_ACPI_HEADER_SIZE + destinationIndex * 8 + byteIndex] =
                    sourceXsdtPage[xsdtOffset + HV_ACPI_HEADER_SIZE +
                        sourceIndex * 8 + byteIndex];
            }
        }
        ++destinationIndex;
    }
    if (destinationIndex + 1 != entryCount) {
        HvZero(rsdpPage, (size_t)HV_EFI_IOMMU_RUNTIME_PAGE_SIZE);
        HvZero(xsdtPage, (size_t)HV_EFI_IOMMU_RUNTIME_PAGE_SIZE);
        return HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE;
    }
    HvZero(xsdt + HV_ACPI_HEADER_SIZE + destinationIndex * 8, 8);
    xsdtLength -= 8;
    xsdt[4] = (uint8_t)xsdtLength;
    xsdt[5] = (uint8_t)(xsdtLength >> 8);
    xsdt[6] = (uint8_t)(xsdtLength >> 16);
    xsdt[7] = (uint8_t)(xsdtLength >> 24);
    xsdt[9] = 0;
    {
        uint8_t sum = 0;
        uint32_t index;
        for (index = 0; index < xsdtLength; ++index) {
            sum = (uint8_t)(sum + xsdt[index]);
        }
        xsdt[9] = (uint8_t)(0u - sum);
    }
    shadow->GuestRsdpPage = guestRsdpPage;
    shadow->HostRsdpPage = hostRsdpPage;
    shadow->GuestXsdtPage = guestXsdtPage;
    shadow->HostXsdtPage = hostXsdtPage;
    shadow->XsdtLength = xsdtLength;
    shadow->RemovedDmarEntries = 1;
    return HV_EFI_IOMMU_RUNTIME_OK;
}

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuInstallEptReadOnlyRemap(
    uint64_t *eptStorage,
    uint32_t eptStoragePages,
    uint64_t eptRootPhysical,
    uint64_t guestPhysicalPage,
    uint64_t hostPhysicalPage)
{
    const uint64_t addressMask = UINT64_C(0x000ffffffffff000);
    uint64_t pdPhysical;
    uint64_t ptPhysical;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    uint32_t pdptIndex;
    uint32_t pdIndex;
    uint32_t ptIndex;
    if (eptStorage == NULL || eptStoragePages < 514 ||
        eptRootPhysical == 0 ||
        (eptRootPhysical & HV_EFI_IOMMU_RUNTIME_PAGE_MASK) != 0 ||
        (guestPhysicalPage & HV_EFI_IOMMU_RUNTIME_PAGE_MASK) != 0 ||
        hostPhysicalPage == 0 ||
        (hostPhysicalPage & HV_EFI_IOMMU_RUNTIME_PAGE_MASK) != 0 ||
        guestPhysicalPage >= (UINT64_C(1) << 39) ||
        hostPhysicalPage > addressMask) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    if ((eptStorage[0] & addressMask) !=
        eptRootPhysical + HV_EFI_IOMMU_RUNTIME_PAGE_SIZE) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_STATE;
    }
    pdpt = eptStorage + 512;
    pdptIndex = (uint32_t)((guestPhysicalPage >> 30) & UINT64_C(0x1ff));
    pdPhysical = pdpt[pdptIndex] & addressMask;
    if (pdPhysical < eptRootPhysical ||
        pdPhysical - eptRootPhysical >=
            (uint64_t)eptStoragePages * HV_EFI_IOMMU_RUNTIME_PAGE_SIZE) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_STATE;
    }
    pd = eptStorage + (size_t)((pdPhysical - eptRootPhysical) / 8);
    pdIndex = (uint32_t)((guestPhysicalPage >> 21) & UINT64_C(0x1ff));
    if ((pd[pdIndex] & UINT64_C(7)) == 0 ||
        (pd[pdIndex] & (UINT64_C(1) << 7)) != 0) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_STATE;
    }
    ptPhysical = pd[pdIndex] & addressMask;
    if (ptPhysical < eptRootPhysical ||
        ptPhysical - eptRootPhysical >=
            (uint64_t)eptStoragePages * HV_EFI_IOMMU_RUNTIME_PAGE_SIZE) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_STATE;
    }
    pt = eptStorage + (size_t)((ptPhysical - eptRootPhysical) / 8);
    ptIndex = (uint32_t)((guestPhysicalPage >> 12) & UINT64_C(0x1ff));
    if (pt[ptIndex] != 0) {
        return HV_EFI_IOMMU_RUNTIME_ALREADY_ENABLED;
    }
    /* EPT read permission plus WB memory type; write and execute absent. */
    pt[ptIndex] = hostPhysicalPage | (UINT64_C(6) << 3) | UINT64_C(1);
    return HV_EFI_IOMMU_RUNTIME_OK;
}

static int HvIommuPageMapped(
    const HV_EFI_IOMMU_PROTECTED_RANGE *ranges,
    uint32_t rangeCount,
    uint64_t page)
{
    uint32_t index;
    for (index = 0; index < rangeCount; ++index) {
        uint64_t end;
        if (HvAddU64(ranges[index].PhysicalBase, ranges[index].Size, &end) &&
            end >= HV_EFI_IOMMU_RUNTIME_PAGE_SIZE &&
            page <= UINT64_MAX - HV_EFI_IOMMU_RUNTIME_PAGE_SIZE &&
            page >= ranges[index].PhysicalBase &&
            page + HV_EFI_IOMMU_RUNTIME_PAGE_SIZE <= end) {
            return 1;
        }
    }
    return 0;
}

static int HvIommuRangeOverlaps(
    const HV_EFI_IOMMU_PROTECTED_RANGE *ranges,
    uint32_t rangeCount,
    uint64_t base,
    uint64_t size)
{
    uint64_t end;
    uint32_t index;
    if (!HvAddU64(base, size, &end)) {
        return 1;
    }
    for (index = 0; index < rangeCount; ++index) {
        uint64_t rangeEnd;
        if (!HvAddU64(ranges[index].PhysicalBase, ranges[index].Size,
                      &rangeEnd) ||
            (base < rangeEnd && ranges[index].PhysicalBase < end)) {
            return 1;
        }
    }
    return 0;
}

static int HvIommuRangeContained(
    const HV_EFI_IOMMU_PROTECTED_RANGE *ranges,
    uint32_t rangeCount,
    uint64_t base,
    uint64_t size)
{
    uint64_t end;
    uint32_t index;
    if (!HvAddU64(base, size, &end)) {
        return 0;
    }
    for (index = 0; index < rangeCount; ++index) {
        uint64_t rangeEnd;
        if (HvAddU64(ranges[index].PhysicalBase, ranges[index].Size,
                     &rangeEnd) &&
            base >= ranges[index].PhysicalBase && end <= rangeEnd) {
            return 1;
        }
    }
    return 0;
}

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuBuildLegacyIdentityDomain(
    uint64_t *storage,
    uint32_t storagePages,
    uint64_t storagePhysical,
    const HV_EFI_IOMMU_PROTECTED_RANGE *mappedRanges,
    uint32_t mappedRangeCount,
    const HV_EFI_IOMMU_PROTECTED_RANGE *denyRanges,
    uint32_t denyRangeCount,
    uint16_t domainId,
    HV_EFI_IOMMU_LEGACY_TABLE_RESULT *result)
{
    const uint64_t limit = UINT64_C(1) << 39;
    uint32_t bus;
    uint32_t pdptIndex;
    uint32_t nextSplitPage = HV_EFI_IOMMU_LEGACY_BASE_PAGES;
    uint64_t *pdpt;
    if (result != NULL) {
        HvZero(result, sizeof(*result));
    }
    if (storage == NULL || result == NULL ||
        storagePages < HV_EFI_IOMMU_LEGACY_BASE_PAGES ||
        storagePhysical == 0 ||
        (storagePhysical & HV_EFI_IOMMU_RUNTIME_PAGE_MASK) != 0 ||
        (uint64_t)storagePages >
            (uint64_t)SIZE_MAX / HV_EFI_IOMMU_RUNTIME_PAGE_SIZE ||
        mappedRanges == NULL || mappedRangeCount == 0 ||
        (denyRangeCount != 0 && denyRanges == NULL) ||
        domainId == 0) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    if (storagePhysical >= limit || (uint64_t)storagePages >
            (limit - storagePhysical) /
                HV_EFI_IOMMU_RUNTIME_PAGE_SIZE) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    for (bus = 0; bus < mappedRangeCount; ++bus) {
        uint64_t end;
        if ((mappedRanges[bus].Kind != HV_EFI_IOMMU_MAPPING_RAM &&
             mappedRanges[bus].Kind != HV_EFI_IOMMU_MAPPING_RMRR) ||
            mappedRanges[bus].Reserved != 0 ||
            mappedRanges[bus].Size == 0 ||
            !HvAddU64(mappedRanges[bus].PhysicalBase,
                      mappedRanges[bus].Size, &end) || end > limit) {
            return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
        }
        if (mappedRanges[bus].Kind == HV_EFI_IOMMU_MAPPING_RMRR &&
            HvIommuRangeOverlaps(denyRanges, denyRangeCount,
                mappedRanges[bus].PhysicalBase, mappedRanges[bus].Size)) {
            return HV_EFI_IOMMU_RUNTIME_RANGE_OVERLAP;
        }
    }
    HvZero(storage, (size_t)storagePages *
        (size_t)HV_EFI_IOMMU_RUNTIME_PAGE_SIZE);
    /* AW=1 is a 39-bit, three-level walk. The context points directly at
     * this top-level page; page 257 remains reserved for buffer compatibility
     * but is not an additional translation level. */
    pdpt = storage +
        (size_t)(HV_EFI_IOMMU_LEGACY_ROOT_PAGES +
                 HV_EFI_IOMMU_LEGACY_CONTEXT_PAGES +
                 HV_EFI_IOMMU_SL_PML4_PAGES) * 512;

    for (bus = 0; bus < 256; ++bus) {
        uint32_t deviceFunction;
        uint64_t *contexts = storage + (size_t)(1 + bus) * 512;
        const uint64_t contextPhysical = storagePhysical +
            (uint64_t)(1 + bus) * HV_EFI_IOMMU_RUNTIME_PAGE_SIZE;
        storage[bus * 2] = contextPhysical | UINT64_C(1);
        for (deviceFunction = 0; deviceFunction < 256; ++deviceFunction) {
            contexts[deviceFunction * 2] =
                storagePhysical + UINT64_C(258) *
                    HV_EFI_IOMMU_RUNTIME_PAGE_SIZE | UINT64_C(1);
            contexts[deviceFunction * 2 + 1] =
                ((uint64_t)domainId << 8) | UINT64_C(1);
        }
    }

    for (pdptIndex = 0; pdptIndex < 512; ++pdptIndex) {
        uint64_t *pd = storage +
            (size_t)(HV_EFI_IOMMU_LEGACY_ROOT_PAGES +
                     HV_EFI_IOMMU_LEGACY_CONTEXT_PAGES +
                     HV_EFI_IOMMU_SL_PML4_PAGES +
                     HV_EFI_IOMMU_SL_PDPT_PAGES + pdptIndex) * 512;
        uint32_t pdIndex;
        pdpt[pdptIndex] = storagePhysical +
            (uint64_t)(HV_EFI_IOMMU_LEGACY_ROOT_PAGES +
                       HV_EFI_IOMMU_LEGACY_CONTEXT_PAGES +
                       HV_EFI_IOMMU_SL_PML4_PAGES +
                       HV_EFI_IOMMU_SL_PDPT_PAGES + pdptIndex) *
                HV_EFI_IOMMU_RUNTIME_PAGE_SIZE | UINT64_C(3);
        for (pdIndex = 0; pdIndex < 512; ++pdIndex) {
            const uint64_t base =
                ((uint64_t)pdptIndex << 30) | ((uint64_t)pdIndex << 21);
            if (HvIommuRangeContained(mappedRanges, mappedRangeCount,
                                      base, UINT64_C(1) << 21) &&
                !HvIommuRangeOverlaps(denyRanges, denyRangeCount,
                                      base, UINT64_C(1) << 21)) {
                pd[pdIndex] = base | UINT64_C(3) | (UINT64_C(1) << 7);
                ++result->LargePageCount;
            } else {
                uint32_t ptIndex;
                uint64_t *pt;
                if (!HvIommuRangeOverlaps(mappedRanges, mappedRangeCount,
                                           base, UINT64_C(1) << 21)) {
                    continue;
                }
                if (nextSplitPage >= storagePages) {
                    HvZero(storage, (size_t)storagePages *
                        (size_t)HV_EFI_IOMMU_RUNTIME_PAGE_SIZE);
                    HvZero(result, sizeof(*result));
                    return HV_EFI_IOMMU_RUNTIME_CAPACITY;
                }
                pt = storage + (size_t)nextSplitPage * 512;
                pd[pdIndex] = storagePhysical +
                    (uint64_t)nextSplitPage *
                        HV_EFI_IOMMU_RUNTIME_PAGE_SIZE | UINT64_C(3);
                ++nextSplitPage;
                ++result->SplitPageCount;
                for (ptIndex = 0; ptIndex < 512; ++ptIndex) {
                    const uint64_t page = base +
                        (uint64_t)ptIndex * HV_EFI_IOMMU_RUNTIME_PAGE_SIZE;
                    if (HvIommuPageMapped(mappedRanges, mappedRangeCount,
                                          page) &&
                        !HvIommuRangeOverlaps(denyRanges, denyRangeCount,
                            page, HV_EFI_IOMMU_RUNTIME_PAGE_SIZE)) {
                        pt[ptIndex] = page | UINT64_C(3);
                        ++result->MappedPageCount;
                    }
                }
            }
        }
    }
    result->RootTablePhysical = storagePhysical;
    result->SecondLevelRootPhysical = storagePhysical +
        UINT64_C(258) * HV_EFI_IOMMU_RUNTIME_PAGE_SIZE;
    result->PagesUsed = nextSplitPage;
    result->DomainId = domainId;
    return HV_EFI_IOMMU_RUNTIME_OK;
}

#define HV_ROOT_VTD_READ UINT64_C(1)
#define HV_ROOT_VTD_WRITE UINT64_C(2)
#define HV_ROOT_VTD_PRESENT_READ_WRITE (HV_ROOT_VTD_READ | HV_ROOT_VTD_WRITE)
#define HV_ROOT_VTD_ADDRESS_MASK UINT64_C(0x0000007ffffff000)

#if defined(_MSC_VER)
#pragma code_seg(push, ".hvscan$m")
#endif

/* CAP.ND is bits 2:0 and encodes 2^(4 + 2*ND) domain identifiers.
 * ND=7 is reserved. Keep this arithmetic-only helper resident-safe because
 * the root-domain admission check also runs after boot services exit. */
uint32_t HvEfiIommuDomainIdSupported(
    uint64_t capability,
    uint32_t domainId)
{
    const uint32_t nd = (uint32_t)(capability & UINT64_C(7));
    uint32_t domainCount;
    if (nd > 6 || domainId == 0 || domainId > UINT16_MAX) {
        return 0;
    }
    domainCount = UINT32_C(1) << (4u + (2u * nd));
    return domainId < domainCount;
}

/* Keep the resident audit closure self-contained in .hvscan. */
static void HvRootZero(void *buffer, size_t size)
{
    volatile uint8_t *bytes = (volatile uint8_t *)buffer;
    size_t index;
    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static int HvRootAddU64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (result == NULL || left > UINT64_MAX - right) {
        return 0;
    }
    *result = left + right;
    return 1;
}

static int HvRootRangeEnd(
    const HV_EFI_IOMMU_ROOT_DEVICE_RANGE *range,
    uint64_t *end)
{
    return range != NULL && range->Size != 0 &&
        (range->PhysicalBase & HV_EFI_IOMMU_RUNTIME_PAGE_MASK) == 0 &&
        (range->Size & HV_EFI_IOMMU_RUNTIME_PAGE_MASK) == 0 &&
        HvRootAddU64(range->PhysicalBase, range->Size, end) &&
        *end <= (UINT64_C(1) << HV_EFI_IOMMU_ROOT_DEVICE_ADDRESS_BITS);
}

static int HvRootRangeOverlaps(
    const HV_EFI_IOMMU_ROOT_DEVICE_RANGE *range,
    uint64_t base,
    uint64_t size)
{
    uint64_t rangeEnd;
    uint64_t end;
    return !HvRootRangeEnd(range, &rangeEnd) ||
        !HvRootAddU64(base, size, &end) ||
        (range->PhysicalBase < end && base < rangeEnd);
}

static int HvRootPlanPageMapped(
    const HV_EFI_IOMMU_ROOT_DEVICE_PLAN *plan,
    uint64_t page)
{
    uint32_t index;
    for (index = 0; index < plan->MappingCount; ++index) {
        const HV_EFI_IOMMU_ROOT_DEVICE_RANGE *range =
            &plan->Mappings[index];
        if (range->PhysicalBase <= page &&
            page < range->PhysicalBase + range->Size) {
            return 1;
        }
    }
    return 0;
}

static int HvRootPlanOverlaps(
    const HV_EFI_IOMMU_ROOT_DEVICE_PLAN *plan,
    uint64_t base,
    uint64_t size)
{
    uint32_t index;
    for (index = 0; index < plan->MappingCount; ++index) {
        if (HvRootRangeOverlaps(&plan->Mappings[index], base, size)) {
            return 1;
        }
    }
    return 0;
}

static HV_EFI_IOMMU_RUNTIME_STATUS HvRootValidatePlan(
    const HV_EFI_IOMMU_ROOT_DEVICE_PLAN *plan)
{
    uint64_t framebufferBytes = 0;
    uint64_t translationBytes = 0;
    uint64_t mappedBytes = 0;
    uint32_t framebufferCount = 0;
    uint32_t translationCount = 0;
    uint32_t index;
    if (plan == NULL || plan->Segment != 0 || plan->Bus > UINT8_MAX ||
        plan->Device > 31 || plan->Function > 7 ||
        plan->DomainId == 0 ||
        plan->DomainId > HV_EFI_IOMMU_MAX_DOMAIN_ID ||
        plan->LegacyDomainId == 0 || plan->LegacyDomainId > UINT16_MAX ||
        plan->DomainId == plan->LegacyDomainId || plan->MappingCount == 0 ||
        plan->MappingCount > HV_EFI_IOMMU_ROOT_DEVICE_MAX_RANGES ||
        plan->Reserved != 0) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    for (index = 0; index < plan->MappingCount; ++index) {
        const HV_EFI_IOMMU_ROOT_DEVICE_RANGE *range =
            &plan->Mappings[index];
        uint64_t end;
        uint64_t *categoryBytes;
        if (!HvRootRangeEnd(range, &end) ||
            range->Access != HV_EFI_IOMMU_ROOT_DEVICE_ACCESS_READ ||
            (range->Kind !=
                 HV_EFI_IOMMU_ROOT_DEVICE_RANGE_FRAMEBUFFER &&
             range->Kind !=
                 HV_EFI_IOMMU_ROOT_DEVICE_RANGE_TRANSLATION)) {
            return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
        }
        if (index != 0 &&
            plan->Mappings[index - 1].PhysicalBase +
                    plan->Mappings[index - 1].Size > range->PhysicalBase) {
            return HV_EFI_IOMMU_RUNTIME_RANGE_OVERLAP;
        }
        if (range->Kind == HV_EFI_IOMMU_ROOT_DEVICE_RANGE_FRAMEBUFFER) {
            ++framebufferCount;
            categoryBytes = &framebufferBytes;
        } else {
            ++translationCount;
            categoryBytes = &translationBytes;
        }
        if (!HvRootAddU64(*categoryBytes, range->Size, categoryBytes) ||
            !HvRootAddU64(mappedBytes, range->Size, &mappedBytes)) {
            return HV_EFI_IOMMU_RUNTIME_RANGE_OVERFLOW;
        }
    }
    if (framebufferCount != 1 || translationCount == 0 ||
        framebufferBytes == 0 ||
        framebufferBytes >
            HV_EFI_IOMMU_ROOT_DEVICE_MAX_FRAMEBUFFER_BYTES ||
        translationBytes == 0 ||
        translationBytes >
            HV_EFI_IOMMU_ROOT_DEVICE_MAX_TRANSLATION_BYTES ||
        mappedBytes > HV_EFI_IOMMU_ROOT_DEVICE_MAX_MAPPED_BYTES ||
        plan->FramebufferBytes != framebufferBytes ||
        plan->TranslationBytes != translationBytes ||
        plan->MappedBytes != mappedBytes) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    return HV_EFI_IOMMU_RUNTIME_OK;
}

static int HvRootStorageRangesOverlap(
    uint64_t firstBase,
    uint32_t firstPages,
    uint64_t secondBase,
    uint32_t secondPages)
{
    const uint64_t firstSize =
        (uint64_t)firstPages * HV_EFI_IOMMU_RUNTIME_PAGE_SIZE;
    const uint64_t secondSize =
        (uint64_t)secondPages * HV_EFI_IOMMU_RUNTIME_PAGE_SIZE;
    uint64_t firstEnd;
    uint64_t secondEnd;
    return !HvRootAddU64(firstBase, firstSize, &firstEnd) ||
        !HvRootAddU64(secondBase, secondSize, &secondEnd) ||
        (firstBase < secondEnd && secondBase < firstEnd);
}

static HV_EFI_IOMMU_RUNTIME_STATUS HvRootValidateInputs(
    const uint64_t *legacyStorage,
    uint32_t legacyStoragePages,
    uint64_t legacyStoragePhysical,
    const HV_EFI_IOMMU_LEGACY_TABLE_RESULT *legacyResult,
    const uint64_t *rootStorage,
    uint32_t rootStoragePages,
    uint64_t rootStoragePhysical,
    const HV_EFI_IOMMU_ROOT_DEVICE_PLAN *rootPlan)
{
    uint64_t legacyStorageEnd;
    uint64_t rootStorageEnd;
    uint32_t index;
    HV_EFI_IOMMU_RUNTIME_STATUS status = HvRootValidatePlan(rootPlan);
    if (status != HV_EFI_IOMMU_RUNTIME_OK) {
        return status;
    }
    if (legacyStorage == NULL || legacyResult == NULL ||
        rootStorage == NULL || legacyStoragePhysical == 0 ||
        rootStoragePhysical == 0 ||
        (legacyStoragePhysical & HV_EFI_IOMMU_RUNTIME_PAGE_MASK) != 0 ||
        (rootStoragePhysical & HV_EFI_IOMMU_RUNTIME_PAGE_MASK) != 0 ||
        legacyStoragePages < HV_EFI_IOMMU_LEGACY_BASE_PAGES ||
        legacyResult->PagesUsed < HV_EFI_IOMMU_LEGACY_BASE_PAGES ||
        legacyResult->PagesUsed > legacyStoragePages ||
        legacyResult->RootTablePhysical != legacyStoragePhysical ||
        legacyResult->SecondLevelRootPhysical != legacyStoragePhysical +
            (uint64_t)(HV_EFI_IOMMU_LEGACY_ROOT_PAGES +
                       HV_EFI_IOMMU_LEGACY_CONTEXT_PAGES +
                       HV_EFI_IOMMU_SL_PML4_PAGES) *
                HV_EFI_IOMMU_RUNTIME_PAGE_SIZE ||
        legacyResult->DomainId == 0 ||
        legacyResult->DomainId != rootPlan->LegacyDomainId) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    if (rootStoragePages < 2 ||
        rootStoragePages > HV_EFI_IOMMU_ROOT_DEVICE_TABLE_MAX_PAGES) {
        return rootStoragePages >
                HV_EFI_IOMMU_ROOT_DEVICE_TABLE_MAX_PAGES ?
            HV_EFI_IOMMU_RUNTIME_CAPACITY :
            HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    if (!HvRootAddU64(legacyStoragePhysical,
            (uint64_t)legacyStoragePages *
                HV_EFI_IOMMU_RUNTIME_PAGE_SIZE,
            &legacyStorageEnd) ||
        !HvRootAddU64(rootStoragePhysical,
            (uint64_t)rootStoragePages *
                HV_EFI_IOMMU_RUNTIME_PAGE_SIZE,
            &rootStorageEnd) ||
        legacyStorageEnd >
            (UINT64_C(1) << HV_EFI_IOMMU_ROOT_DEVICE_ADDRESS_BITS) ||
        rootStorageEnd >
            (UINT64_C(1) << HV_EFI_IOMMU_ROOT_DEVICE_ADDRESS_BITS)) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    if (HvRootStorageRangesOverlap(
            legacyStoragePhysical, legacyStoragePages,
            rootStoragePhysical, rootStoragePages)) {
        return HV_EFI_IOMMU_RUNTIME_RANGE_OVERLAP;
    }
    for (index = 0; index < rootPlan->MappingCount; ++index) {
        const HV_EFI_IOMMU_ROOT_DEVICE_RANGE *range =
            &rootPlan->Mappings[index];
        if (HvRootStorageRangesOverlap(
                range->PhysicalBase,
                (uint32_t)(range->Size /
                           HV_EFI_IOMMU_RUNTIME_PAGE_SIZE),
                legacyStoragePhysical, legacyStoragePages) ||
            HvRootStorageRangesOverlap(
                range->PhysicalBase,
                (uint32_t)(range->Size /
                           HV_EFI_IOMMU_RUNTIME_PAGE_SIZE),
                rootStoragePhysical, rootStoragePages)) {
            return HV_EFI_IOMMU_RUNTIME_RANGE_OVERLAP;
        }
    }
    return HV_EFI_IOMMU_RUNTIME_OK;
}

static HV_EFI_IOMMU_RUNTIME_STATUS HvRootAuditLegacyContexts(
    const uint64_t *legacyStorage,
    uint64_t legacyStoragePhysical,
    const HV_EFI_IOMMU_LEGACY_TABLE_RESULT *legacyResult,
    uint64_t rootStoragePhysical,
    const HV_EFI_IOMMU_ROOT_DEVICE_PLAN *rootPlan,
    uint32_t rootInstalled)
{
    uint32_t bus;
    const uint64_t legacyRootEntry =
        legacyResult->SecondLevelRootPhysical | UINT64_C(1);
    const uint64_t legacyDomainEntry =
        ((uint64_t)legacyResult->DomainId << 8) | UINT64_C(1);
    const uint64_t rootDomainEntry =
        ((uint64_t)rootPlan->DomainId << 8) | UINT64_C(1);
    for (bus = 0; bus < 256; ++bus) {
        const uint64_t expectedContextPhysical = legacyStoragePhysical +
            (uint64_t)(1 + bus) * HV_EFI_IOMMU_RUNTIME_PAGE_SIZE;
        const uint64_t *contexts = legacyStorage + (size_t)(1 + bus) * 512;
        uint32_t deviceFunction;
        if (legacyStorage[bus * 2] !=
                (expectedContextPhysical | UINT64_C(1)) ||
            legacyStorage[bus * 2 + 1] != 0) {
            return HV_EFI_IOMMU_RUNTIME_AUDIT_FAILED;
        }
        for (deviceFunction = 0; deviceFunction < 256; ++deviceFunction) {
            const uint32_t device = deviceFunction >> 3;
            const uint32_t function = deviceFunction & 7;
            const int target = rootPlan->Segment == 0 &&
                bus == rootPlan->Bus && device == rootPlan->Device &&
                function == rootPlan->Function;
            uint64_t expectedRoot = legacyRootEntry;
            uint64_t expectedDomain = legacyDomainEntry;
            if (target && rootInstalled != 0) {
                expectedRoot = rootStoragePhysical | UINT64_C(1);
                expectedDomain = rootDomainEntry;
            }
            if (contexts[deviceFunction * 2] != expectedRoot ||
                contexts[deviceFunction * 2 + 1] != expectedDomain) {
                return HV_EFI_IOMMU_RUNTIME_AUDIT_FAILED;
            }
        }
    }
    return HV_EFI_IOMMU_RUNTIME_OK;
}

static HV_EFI_IOMMU_RUNTIME_STATUS HvRootAuditLegacySecondLevelRoot(
    const uint64_t *legacyStorage,
    uint64_t legacyStoragePhysical)
{
    const uint64_t *reserved = legacyStorage +
        (size_t)(HV_EFI_IOMMU_LEGACY_ROOT_PAGES +
                 HV_EFI_IOMMU_LEGACY_CONTEXT_PAGES) * 512;
    const uint64_t *root = legacyStorage +
        (size_t)(HV_EFI_IOMMU_LEGACY_ROOT_PAGES +
                 HV_EFI_IOMMU_LEGACY_CONTEXT_PAGES +
                 HV_EFI_IOMMU_SL_PML4_PAGES) * 512;
    uint32_t pdptIndex;
    for (pdptIndex = 0; pdptIndex < 512; ++pdptIndex) {
        if (reserved[pdptIndex] != 0) {
            return HV_EFI_IOMMU_RUNTIME_AUDIT_FAILED;
        }
    }
    for (pdptIndex = 0; pdptIndex < 512; ++pdptIndex) {
        const uint64_t expected = legacyStoragePhysical +
            (uint64_t)(HV_EFI_IOMMU_LEGACY_ROOT_PAGES +
                       HV_EFI_IOMMU_LEGACY_CONTEXT_PAGES +
                       HV_EFI_IOMMU_SL_PML4_PAGES +
                       HV_EFI_IOMMU_SL_PDPT_PAGES + pdptIndex) *
                HV_EFI_IOMMU_RUNTIME_PAGE_SIZE | UINT64_C(3);
        if (root[pdptIndex] != expected) {
            return HV_EFI_IOMMU_RUNTIME_AUDIT_FAILED;
        }
    }
    return HV_EFI_IOMMU_RUNTIME_OK;
}

static HV_EFI_IOMMU_RUNTIME_STATUS HvRootLegacyPageUnmapped(
    const uint64_t *legacyStorage,
    uint32_t legacyStoragePages,
    uint64_t legacyStoragePhysical,
    const HV_EFI_IOMMU_LEGACY_TABLE_RESULT *legacyResult,
    uint64_t page,
    uint32_t *unmapped)
{
    const uint64_t *root;
    const uint64_t *pd;
    uint64_t entry;
    uint64_t pageIndex;
    uint32_t pdIndex;
    if (unmapped == NULL || page >=
            (UINT64_C(1) << HV_EFI_IOMMU_ROOT_DEVICE_ADDRESS_BITS) ||
        (page & HV_EFI_IOMMU_RUNTIME_PAGE_MASK) != 0) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    *unmapped = 0;
    /* AW=1 walks the PDPT at index 258, then one of the PDs from index 259. */
    root = legacyStorage +
        (size_t)(HV_EFI_IOMMU_LEGACY_ROOT_PAGES +
                 HV_EFI_IOMMU_LEGACY_CONTEXT_PAGES +
                 HV_EFI_IOMMU_SL_PML4_PAGES) * 512;
    {
        const uint32_t pdptIndex =
            (uint32_t)((page >> 30) & UINT64_C(0x1ff));
        const uint64_t expected = legacyStoragePhysical +
            (uint64_t)(HV_EFI_IOMMU_LEGACY_ROOT_PAGES +
                       HV_EFI_IOMMU_LEGACY_CONTEXT_PAGES +
                       HV_EFI_IOMMU_SL_PML4_PAGES +
                       HV_EFI_IOMMU_SL_PDPT_PAGES + pdptIndex) *
                HV_EFI_IOMMU_RUNTIME_PAGE_SIZE | UINT64_C(3);
        if (root[pdptIndex] != expected) {
            return HV_EFI_IOMMU_RUNTIME_AUDIT_FAILED;
        }
        pd = legacyStorage +
            (size_t)(HV_EFI_IOMMU_LEGACY_ROOT_PAGES +
                     HV_EFI_IOMMU_LEGACY_CONTEXT_PAGES +
                     HV_EFI_IOMMU_SL_PML4_PAGES +
                     HV_EFI_IOMMU_SL_PDPT_PAGES + pdptIndex) * 512;
    }
    pdIndex = (uint32_t)((page >> 21) & UINT64_C(0x1ff));
    entry = pd[pdIndex];
    if (entry == 0) {
        *unmapped = 1;
        return HV_EFI_IOMMU_RUNTIME_OK;
    }
    if ((entry & (UINT64_C(1) << 7)) != 0) {
        return HV_EFI_IOMMU_RUNTIME_OK;
    }
    pageIndex = (entry & HV_ROOT_VTD_ADDRESS_MASK) - legacyStoragePhysical;
    if ((entry & HV_ROOT_VTD_PRESENT_READ_WRITE) !=
            HV_ROOT_VTD_PRESENT_READ_WRITE ||
        (entry & UINT64_C(0xff8)) != 0 ||
        pageIndex % HV_EFI_IOMMU_RUNTIME_PAGE_SIZE != 0 ||
        pageIndex / HV_EFI_IOMMU_RUNTIME_PAGE_SIZE >= legacyResult->PagesUsed ||
        pageIndex / HV_EFI_IOMMU_RUNTIME_PAGE_SIZE <
            HV_EFI_IOMMU_LEGACY_BASE_PAGES ||
        pageIndex / HV_EFI_IOMMU_RUNTIME_PAGE_SIZE >= legacyStoragePages) {
        return HV_EFI_IOMMU_RUNTIME_AUDIT_FAILED;
    }
    {
        const uint64_t *pt = legacyStorage +
            (size_t)(pageIndex / HV_EFI_IOMMU_RUNTIME_PAGE_SIZE) * 512;
        const uint32_t ptIndex = (uint32_t)((page >> 12) & UINT64_C(0x1ff));
        const uint64_t leaf = pt[ptIndex];
        if ((leaf & HV_ROOT_VTD_READ) == 0) {
            *unmapped = 1;
        }
    }
    return HV_EFI_IOMMU_RUNTIME_OK;
}

static HV_EFI_IOMMU_RUNTIME_STATUS HvRootRequireLegacyUnmapped(
    const uint64_t *legacyStorage,
    uint32_t legacyStoragePages,
    uint64_t legacyStoragePhysical,
    const HV_EFI_IOMMU_LEGACY_TABLE_RESULT *legacyResult,
    uint64_t base,
    uint64_t size)
{
    uint64_t end;
    uint64_t page;
    if (size == 0 || (base & HV_EFI_IOMMU_RUNTIME_PAGE_MASK) != 0 ||
        (size & HV_EFI_IOMMU_RUNTIME_PAGE_MASK) != 0 ||
        !HvRootAddU64(base, size, &end)) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    for (page = base; page < end;
         page += HV_EFI_IOMMU_RUNTIME_PAGE_SIZE) {
        uint32_t unmapped;
        HV_EFI_IOMMU_RUNTIME_STATUS status = HvRootLegacyPageUnmapped(
            legacyStorage, legacyStoragePages, legacyStoragePhysical,
            legacyResult, page, &unmapped);
        if (status != HV_EFI_IOMMU_RUNTIME_OK) {
            return status;
        }
        if (unmapped == 0) {
            return HV_EFI_IOMMU_RUNTIME_AUDIT_FAILED;
        }
    }
    return HV_EFI_IOMMU_RUNTIME_OK;
}

static HV_EFI_IOMMU_RUNTIME_STATUS HvRootAuditSecondLevel(
    const uint64_t *rootStorage,
    uint32_t rootStoragePages,
    uint64_t rootStoragePhysical,
    const HV_EFI_IOMMU_ROOT_DEVICE_PLAN *rootPlan,
    const HV_EFI_IOMMU_ROOT_DEVICE_TABLE_RESULT *result)
{
    uint32_t nextPage = 1;
    uint32_t mappedPages = 0;
    uint32_t pdptIndex;
    const uint64_t *root = rootStorage;
    for (pdptIndex = 0; pdptIndex < 512; ++pdptIndex) {
        const uint64_t oneGiBBase = (uint64_t)pdptIndex << 30;
        const uint64_t oneGiBSize = UINT64_C(1) << 30;
        if (!HvRootPlanOverlaps(rootPlan, oneGiBBase, oneGiBSize)) {
            if (root[pdptIndex] != 0) {
                return HV_EFI_IOMMU_RUNTIME_AUDIT_FAILED;
            }
            continue;
        }
        if (nextPage >= rootStoragePages ||
            root[pdptIndex] != (rootStoragePhysical +
                    (uint64_t)nextPage *
                        HV_EFI_IOMMU_RUNTIME_PAGE_SIZE |
                    HV_ROOT_VTD_PRESENT_READ_WRITE)) {
            return HV_EFI_IOMMU_RUNTIME_AUDIT_FAILED;
        }
        {
            const uint64_t *pd = rootStorage + (size_t)nextPage * 512;
            uint32_t pdIndex;
            ++nextPage;
            for (pdIndex = 0; pdIndex < 512; ++pdIndex) {
                const uint64_t twoMiBBase = oneGiBBase +
                    ((uint64_t)pdIndex << 21);
                const uint64_t twoMiBSize = UINT64_C(1) << 21;
                if (!HvRootPlanOverlaps(
                        rootPlan, twoMiBBase, twoMiBSize)) {
                    if (pd[pdIndex] != 0) {
                        return HV_EFI_IOMMU_RUNTIME_AUDIT_FAILED;
                    }
                    continue;
                }
                if (nextPage >= rootStoragePages ||
                    pd[pdIndex] != (rootStoragePhysical +
                            (uint64_t)nextPage *
                                HV_EFI_IOMMU_RUNTIME_PAGE_SIZE |
                            HV_ROOT_VTD_PRESENT_READ_WRITE)) {
                    return HV_EFI_IOMMU_RUNTIME_AUDIT_FAILED;
                }
                {
                    const uint64_t *pt = rootStorage +
                        (size_t)nextPage * 512;
                    uint32_t ptIndex;
                    ++nextPage;
                    for (ptIndex = 0; ptIndex < 512; ++ptIndex) {
                        const uint64_t page = twoMiBBase +
                            (uint64_t)ptIndex *
                                HV_EFI_IOMMU_RUNTIME_PAGE_SIZE;
                        const uint64_t expected =
                            HvRootPlanPageMapped(rootPlan, page) ?
                                page | HV_ROOT_VTD_READ : 0;
                        if (pt[ptIndex] != expected) {
                            return HV_EFI_IOMMU_RUNTIME_AUDIT_FAILED;
                        }
                        if (expected != 0) {
                            ++mappedPages;
                        }
                    }
                }
            }
        }
    }
    if (nextPage != result->PagesUsed ||
        mappedPages != result->MappedPageCount ||
        nextPage > rootStoragePages) {
        return HV_EFI_IOMMU_RUNTIME_AUDIT_FAILED;
    }
    for (; nextPage < rootStoragePages; ++nextPage) {
        uint32_t word;
        const uint64_t *page = rootStorage + (size_t)nextPage * 512;
        for (word = 0; word < 512; ++word) {
            if (page[word] != 0) {
                return HV_EFI_IOMMU_RUNTIME_AUDIT_FAILED;
            }
        }
    }
    return HV_EFI_IOMMU_RUNTIME_OK;
}

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuAuditRootDeviceDomain(
    const uint64_t *legacyStorage,
    uint32_t legacyStoragePages,
    uint64_t legacyStoragePhysical,
    const HV_EFI_IOMMU_LEGACY_TABLE_RESULT *legacyResult,
    const uint64_t *rootStorage,
    uint32_t rootStoragePages,
    uint64_t rootStoragePhysical,
    const HV_EFI_IOMMU_ROOT_DEVICE_PLAN *rootPlan,
    const HV_EFI_IOMMU_ROOT_DEVICE_TABLE_RESULT *result)
{
    uint32_t index;
    uint64_t legacyBytes;
    uint64_t rootBytes;
    HV_EFI_IOMMU_RUNTIME_STATUS status;
    if (result == NULL) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    status = HvRootValidateInputs(
            legacyStorage, legacyStoragePages, legacyStoragePhysical,
            legacyResult, rootStorage, rootStoragePages,
            rootStoragePhysical, rootPlan);
    if (status != HV_EFI_IOMMU_RUNTIME_OK) {
        return status;
    }
    if (
        result->RootPhysical != rootStoragePhysical ||
        result->DeviceIndex != rootPlan->DeviceIndex ||
        result->Segment != rootPlan->Segment ||
        result->Bus != rootPlan->Bus ||
        result->Device != rootPlan->Device ||
        result->Function != rootPlan->Function ||
        result->DomainId != rootPlan->DomainId ||
        result->LegacyDomainId != rootPlan->LegacyDomainId ||
        result->PagesUsed < 3 || result->PagesUsed > rootStoragePages) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    legacyBytes = (uint64_t)legacyStoragePages *
        HV_EFI_IOMMU_RUNTIME_PAGE_SIZE;
    rootBytes = (uint64_t)rootStoragePages *
        HV_EFI_IOMMU_RUNTIME_PAGE_SIZE;
    status = HvRootAuditLegacyContexts(
        legacyStorage, legacyStoragePhysical, legacyResult,
        rootStoragePhysical, rootPlan, 1);
    if (status != HV_EFI_IOMMU_RUNTIME_OK) {
        return status;
    }
    status = HvRootAuditLegacySecondLevelRoot(
        legacyStorage, legacyStoragePhysical);
    if (status != HV_EFI_IOMMU_RUNTIME_OK) {
        return status;
    }
    status = HvRootAuditSecondLevel(
        rootStorage, rootStoragePages, rootStoragePhysical,
        rootPlan, result);
    if (status != HV_EFI_IOMMU_RUNTIME_OK) {
        return status;
    }
    status = HvRootRequireLegacyUnmapped(
        legacyStorage, legacyStoragePages, legacyStoragePhysical,
        legacyResult, legacyStoragePhysical, legacyBytes);
    if (status != HV_EFI_IOMMU_RUNTIME_OK) {
        return status;
    }
    status = HvRootRequireLegacyUnmapped(
        legacyStorage, legacyStoragePages, legacyStoragePhysical,
        legacyResult, rootStoragePhysical, rootBytes);
    if (status != HV_EFI_IOMMU_RUNTIME_OK) {
        return status;
    }
    for (index = 0; index < rootPlan->MappingCount; ++index) {
        status = HvRootRequireLegacyUnmapped(
            legacyStorage, legacyStoragePages, legacyStoragePhysical,
            legacyResult, rootPlan->Mappings[index].PhysicalBase,
            rootPlan->Mappings[index].Size);
        if (status != HV_EFI_IOMMU_RUNTIME_OK) {
            return status;
        }
    }
    return HV_EFI_IOMMU_RUNTIME_OK;
}

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuBuildRootDeviceDomain(
    uint64_t *legacyStorage,
    uint32_t legacyStoragePages,
    uint64_t legacyStoragePhysical,
    const HV_EFI_IOMMU_LEGACY_TABLE_RESULT *legacyResult,
    uint64_t *rootStorage,
    uint32_t rootStoragePages,
    uint64_t rootStoragePhysical,
    const HV_EFI_IOMMU_ROOT_DEVICE_PLAN *rootPlan,
    HV_EFI_IOMMU_ROOT_DEVICE_TABLE_RESULT *result)
{
    uint64_t *root;
    uint64_t *targetContext;
    uint64_t previousTargetLow;
    uint64_t previousTargetHigh;
    uint32_t nextPage = 1;
    uint32_t mappedPages = 0;
    uint32_t pdptIndex;
    HV_EFI_IOMMU_RUNTIME_STATUS status;
    if (result != NULL) {
        HvRootZero(result, sizeof(*result));
    }
    status = HvRootValidateInputs(
        legacyStorage, legacyStoragePages, legacyStoragePhysical,
        legacyResult, rootStorage, rootStoragePages, rootStoragePhysical,
        rootPlan);
    if (status != HV_EFI_IOMMU_RUNTIME_OK || result == NULL) {
        return status != HV_EFI_IOMMU_RUNTIME_OK ? status :
            HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    status = HvRootAuditLegacyContexts(
        legacyStorage, legacyStoragePhysical, legacyResult,
        rootStoragePhysical, rootPlan, 0);
    if (status != HV_EFI_IOMMU_RUNTIME_OK) {
        return status;
    }
    HvRootZero(rootStorage, (size_t)rootStoragePages *
        (size_t)HV_EFI_IOMMU_RUNTIME_PAGE_SIZE);
    root = rootStorage;
    for (pdptIndex = 0; pdptIndex < 512; ++pdptIndex) {
        const uint64_t oneGiBBase = (uint64_t)pdptIndex << 30;
        if (!HvRootPlanOverlaps(
                rootPlan, oneGiBBase, UINT64_C(1) << 30)) {
            continue;
        }
        if (nextPage >= rootStoragePages) {
            status = HV_EFI_IOMMU_RUNTIME_CAPACITY;
            goto fail;
        }
        root[pdptIndex] = rootStoragePhysical +
            (uint64_t)nextPage * HV_EFI_IOMMU_RUNTIME_PAGE_SIZE |
            HV_ROOT_VTD_PRESENT_READ_WRITE;
        {
            uint64_t *pd = rootStorage + (size_t)nextPage * 512;
            uint32_t pdIndex;
            ++nextPage;
            for (pdIndex = 0; pdIndex < 512; ++pdIndex) {
                const uint64_t twoMiBBase = oneGiBBase +
                    ((uint64_t)pdIndex << 21);
                if (!HvRootPlanOverlaps(
                        rootPlan, twoMiBBase, UINT64_C(1) << 21)) {
                    continue;
                }
                if (nextPage >= rootStoragePages) {
                    status = HV_EFI_IOMMU_RUNTIME_CAPACITY;
                    goto fail;
                }
                pd[pdIndex] = rootStoragePhysical +
                    (uint64_t)nextPage * HV_EFI_IOMMU_RUNTIME_PAGE_SIZE |
                    HV_ROOT_VTD_PRESENT_READ_WRITE;
                {
                    uint64_t *pt = rootStorage + (size_t)nextPage * 512;
                    uint32_t ptIndex;
                    ++nextPage;
                    for (ptIndex = 0; ptIndex < 512; ++ptIndex) {
                        const uint64_t page = twoMiBBase +
                            (uint64_t)ptIndex *
                                HV_EFI_IOMMU_RUNTIME_PAGE_SIZE;
                        if (HvRootPlanPageMapped(rootPlan, page)) {
                            pt[ptIndex] = page | HV_ROOT_VTD_READ;
                            ++mappedPages;
                        }
                    }
                }
            }
        }
    }
    result->RootPhysical = rootStoragePhysical;
    result->PagesUsed = nextPage;
    result->MappedPageCount = mappedPages;
    result->DeviceIndex = rootPlan->DeviceIndex;
    result->Segment = rootPlan->Segment;
    result->Bus = rootPlan->Bus;
    result->Device = rootPlan->Device;
    result->Function = rootPlan->Function;
    result->DomainId = rootPlan->DomainId;
    result->LegacyDomainId = rootPlan->LegacyDomainId;
    targetContext = legacyStorage +
        (size_t)(1 + rootPlan->Bus) * 512 +
        (size_t)(rootPlan->Device * 8 + rootPlan->Function) * 2;
    previousTargetLow = targetContext[0];
    previousTargetHigh = targetContext[1];
    targetContext[0] &= ~UINT64_C(1);
    targetContext[1] = ((uint64_t)rootPlan->DomainId << 8) | UINT64_C(1);
    targetContext[0] = rootStoragePhysical | UINT64_C(1);
    status = HvEfiIommuAuditRootDeviceDomain(
        legacyStorage, legacyStoragePages, legacyStoragePhysical,
        legacyResult, rootStorage, rootStoragePages, rootStoragePhysical,
        rootPlan, result);
    if (status != HV_EFI_IOMMU_RUNTIME_OK) {
        targetContext[0] = previousTargetLow;
        targetContext[1] = previousTargetHigh;
        goto fail;
    }
    return HV_EFI_IOMMU_RUNTIME_OK;

fail:
    HvRootZero(rootStorage, (size_t)rootStoragePages *
        (size_t)HV_EFI_IOMMU_RUNTIME_PAGE_SIZE);
    HvRootZero(result, sizeof(*result));
    return status;
}

#if defined(_MSC_VER)
#pragma code_seg(pop)
#endif

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuParseMcfg(
    const HV_EFI_IOMMU_PHYSICAL_VIEW *view,
    const HV_EFI_IOMMU_DMAR_INFO *dmar,
    HV_EFI_IOMMU_MCFG_SEGMENT *segments,
    uint32_t segmentCapacity,
    uint32_t *segmentCount)
{
    const uint8_t *xsdt;
    uint32_t xsdtLength;
    uint32_t entryCount;
    uint32_t index;
    uint32_t found = 0;
    uint32_t count = 0;
    HV_EFI_IOMMU_RUNTIME_STATUS status;
    if (segmentCount != NULL) {
        *segmentCount = 0;
    }
    if (view == NULL || dmar == NULL || segments == NULL ||
        segmentCount == NULL || segmentCapacity == 0 ||
        dmar->XsdtPhysical == 0) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    status = HvMapPhysical(view, dmar->XsdtPhysical,
                           HV_ACPI_HEADER_SIZE, &xsdt);
    if (status != HV_EFI_IOMMU_RUNTIME_OK) {
        return status;
    }
    xsdtLength = HvRead32(xsdt + 4);
    if (xsdtLength < HV_ACPI_HEADER_SIZE ||
        xsdtLength > HV_ACPI_MAX_TABLE_SIZE ||
        ((xsdtLength - HV_ACPI_HEADER_SIZE) & 7u) != 0) {
        return HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE;
    }
    status = HvMapPhysical(view, dmar->XsdtPhysical, xsdtLength, &xsdt);
    if (status != HV_EFI_IOMMU_RUNTIME_OK ||
        !HvChecksumValid(xsdt, xsdtLength)) {
        return status != HV_EFI_IOMMU_RUNTIME_OK ? status :
            HV_EFI_IOMMU_RUNTIME_BAD_CHECKSUM;
    }
    entryCount = (xsdtLength - HV_ACPI_HEADER_SIZE) / 8;
    for (index = 0; index < entryCount; ++index) {
        const uint64_t tablePhysical =
            HvRead64(xsdt + HV_ACPI_HEADER_SIZE + index * 8);
        const uint8_t *table;
        uint32_t length;
        uint32_t entryOffset;
        status = HvMapPhysical(view, tablePhysical,
                               HV_ACPI_HEADER_SIZE, &table);
        if (status != HV_EFI_IOMMU_RUNTIME_OK) {
            return status;
        }
        if (!HvBytesEqual(table, "MCFG", 4)) {
            continue;
        }
        if (++found != 1) {
            return HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE;
        }
        length = HvRead32(table + 4);
        if (length < 44 || length > HV_ACPI_MAX_TABLE_SIZE ||
            ((length - 44) & 15u) != 0) {
            return HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE;
        }
        status = HvMapPhysical(view, tablePhysical, length, &table);
        if (status != HV_EFI_IOMMU_RUNTIME_OK) {
            return status;
        }
        if (!HvChecksumValid(table, length) || HvRead64(table + 36) != 0) {
            return HV_EFI_IOMMU_RUNTIME_BAD_CHECKSUM;
        }
        for (entryOffset = 44; entryOffset < length; entryOffset += 16) {
            uint64_t ecamBase = HvRead64(table + entryOffset);
            uint16_t segment = HvRead16(table + entryOffset + 8);
            uint8_t startBus = table[entryOffset + 10];
            uint8_t endBus = table[entryOffset + 11];
            uint32_t previous;
            if (HvRead32(table + entryOffset + 12) != 0 ||
                ecamBase == 0 || (ecamBase & UINT64_C(0xfffff)) != 0 ||
                startBus > endBus ||
                ecamBase > UINT64_MAX -
                    ((uint64_t)(endBus - startBus + 1) << 20)) {
                return HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE;
            }
            if (count >= segmentCapacity) {
                *segmentCount = 0;
                return HV_EFI_IOMMU_RUNTIME_CAPACITY;
            }
            for (previous = 0; previous < count; ++previous) {
                if (segments[previous].Segment == segment &&
                    startBus <= segments[previous].EndBus &&
                    segments[previous].StartBus <= endBus) {
                    *segmentCount = 0;
                    return HV_EFI_IOMMU_RUNTIME_RANGE_OVERLAP;
                }
            }
            segments[count].EcamBase = ecamBase;
            segments[count].Segment = segment;
            segments[count].StartBus = startBus;
            segments[count].EndBus = endBus;
            ++count;
        }
    }
    if (found != 1 || count == 0) {
        return HV_EFI_IOMMU_RUNTIME_DMAR_NOT_FOUND;
    }
    *segmentCount = count;
    return HV_EFI_IOMMU_RUNTIME_OK;
}

static uint64_t HvPciFunctionAddress(
    const HV_EFI_IOMMU_MCFG_SEGMENT *segment,
    uint32_t bus,
    uint32_t device,
    uint32_t function)
{
    return segment->EcamBase +
        ((uint64_t)(bus - segment->StartBus) << 20) +
        ((uint64_t)device << 15) + ((uint64_t)function << 12);
}

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuValidatePciCoverage(
    const HV_EFI_IOMMU_DMAR_INFO *dmar,
    const HV_EFI_IOMMU_MCFG_SEGMENT *segments,
    uint32_t segmentCount)
{
    uint32_t segmentIndex;
    uint32_t unitIndex;
    uint32_t rmrrIndex;
    if (dmar == NULL || dmar->Units == NULL || dmar->UnitCount == 0 ||
        segments == NULL || segmentCount == 0) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    for (segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
        uint32_t includeAll = 0;
        for (unitIndex = 0; unitIndex < dmar->UnitCount; ++unitIndex) {
            if (dmar->Units[unitIndex].Segment ==
                    segments[segmentIndex].Segment &&
                (dmar->Units[unitIndex].Flags &
                 HV_EFI_IOMMU_DRHD_INCLUDE_ALL) != 0) {
                includeAll = 1;
                break;
            }
        }
        if (includeAll == 0) {
            return HV_EFI_IOMMU_RUNTIME_UNSUPPORTED_SCOPE;
        }
    }
    for (unitIndex = 0; unitIndex < dmar->UnitCount; ++unitIndex) {
        uint32_t found = 0;
        for (segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
            if (dmar->Units[unitIndex].Segment ==
                segments[segmentIndex].Segment) {
                found = 1;
                break;
            }
        }
        if (found == 0) {
            return HV_EFI_IOMMU_RUNTIME_UNSUPPORTED_SCOPE;
        }
    }
    for (rmrrIndex = 0; rmrrIndex < dmar->RmrrCount; ++rmrrIndex) {
        uint32_t found = 0;
        for (segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
            if (dmar->Rmrrs[rmrrIndex].Segment ==
                segments[segmentIndex].Segment) {
                found = 1;
                break;
            }
        }
        if (found == 0) {
            return HV_EFI_IOMMU_RUNTIME_UNSUPPORTED_SCOPE;
        }
    }
    return HV_EFI_IOMMU_RUNTIME_OK;
}

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuRestorePciBusMasters(
    const HV_EFI_IOMMU_PCI_CALLBACKS *callbacks,
    HV_EFI_IOMMU_PCI_COMMAND_STATE *states,
    uint32_t stateCount)
{
    uint32_t index = stateCount;
    uint32_t failed = 0;
    if (callbacks == NULL || callbacks->Write16 == NULL ||
        (stateCount != 0 && states == NULL)) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    while (index != 0) {
        HV_EFI_IOMMU_PCI_COMMAND_STATE *state = &states[--index];
        if (state->Restored == 0) {
            if (callbacks->Write16(callbacks->Context,
                                   state->CommandAddress,
                                   state->OriginalCommand) == 0) {
                failed = 1;
            } else {
                state->Restored = 1;
            }
        }
    }
    return failed == 0 ? HV_EFI_IOMMU_RUNTIME_OK :
        HV_EFI_IOMMU_RUNTIME_ROLLBACK_FAILED;
}

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuQuiescePciBusMasters(
    const HV_EFI_IOMMU_MCFG_SEGMENT *segments,
    uint32_t segmentCount,
    const HV_EFI_IOMMU_PCI_CALLBACKS *callbacks,
    HV_EFI_IOMMU_PCI_COMMAND_STATE *states,
    uint32_t stateCapacity,
    uint32_t *stateCount)
{
    uint32_t pass;
    uint32_t required = 0;
    if (stateCount != NULL) {
        *stateCount = 0;
    }
    if (segments == NULL || segmentCount == 0 || callbacks == NULL ||
        callbacks->Read16 == NULL || callbacks->Write16 == NULL ||
        states == NULL || stateCount == NULL || stateCapacity == 0) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    for (pass = 0; pass < 2; ++pass) {
        uint32_t segmentIndex;
        uint32_t observed = 0;
        for (segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
            uint32_t bus;
            for (bus = segments[segmentIndex].StartBus;
                 bus <= segments[segmentIndex].EndBus; ++bus) {
                uint32_t device;
                for (device = 0; device < 32; ++device) {
                    uint32_t function;
                    for (function = 0; function < 8; ++function) {
                        const uint64_t base = HvPciFunctionAddress(
                            &segments[segmentIndex], bus, device, function);
                        uint16_t vendor;
                        uint16_t command = 0;
                        if (callbacks->Read16(callbacks->Context, base,
                                              &vendor) == 0 ||
                            (vendor != UINT16_C(0xffff) &&
                             callbacks->Read16(callbacks->Context, base + 4,
                                               &command) == 0)) {
                            if (pass != 0 && observed != 0) {
                                *stateCount = observed;
                                (void)HvEfiIommuRestorePciBusMasters(
                                    callbacks, states, observed);
                                *stateCount = 0;
                            }
                            return HV_EFI_IOMMU_RUNTIME_CALLBACK_FAILED;
                        }
                        if (vendor == UINT16_C(0xffff) ||
                            (command & UINT16_C(4)) == 0) {
                            continue;
                        }
                        if (pass == 0) {
                            ++required;
                            continue;
                        }
                        if (observed >= stateCapacity) {
                            *stateCount = observed;
                            (void)HvEfiIommuRestorePciBusMasters(
                                callbacks, states, observed);
                            *stateCount = 0;
                            return HV_EFI_IOMMU_RUNTIME_CAPACITY;
                        }
                        states[observed].CommandAddress = base + 4;
                        states[observed].OriginalCommand = command;
                        states[observed].Segment = segments[segmentIndex].Segment;
                        states[observed].Bus = (uint8_t)bus;
                        states[observed].Device = (uint8_t)device;
                        states[observed].Function = (uint8_t)function;
                        states[observed].Restored = 0;
                        if (callbacks->Write16(callbacks->Context, base + 4,
                                               command & ~UINT16_C(4)) == 0) {
                            *stateCount = observed + 1;
                            (void)HvEfiIommuRestorePciBusMasters(
                                callbacks, states, observed + 1);
                            *stateCount = 0;
                            return HV_EFI_IOMMU_RUNTIME_CALLBACK_FAILED;
                        }
                        ++observed;
                    }
                }
                if (bus == UINT8_MAX) {
                    break;
                }
            }
        }
        if (pass == 0 && required > stateCapacity) {
            return HV_EFI_IOMMU_RUNTIME_CAPACITY;
        }
        if (pass != 0) {
            if (observed != required) {
                *stateCount = observed;
                (void)HvEfiIommuRestorePciBusMasters(
                    callbacks, states, observed);
                *stateCount = 0;
                return HV_EFI_IOMMU_RUNTIME_AUDIT_FAILED;
            }
            *stateCount = observed;
        }
    }
    return HV_EFI_IOMMU_RUNTIME_OK;
}

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuInstallRuntimeIsolation(
    const HV_EFI_IOMMU_DMAR_INFO *dmar,
    const HV_EFI_IOMMU_UNIT_CONFIGURATION *configurations,
    uint32_t configurationCount,
    const HV_EFI_IOMMU_OWNERSHIP_POLICY *ownership,
    const HV_EFI_IOMMU_RUNTIME_CALLBACKS *runtimeCallbacks,
    const HV_EFI_IOMMU_PROGRAM_STORAGE *programStorage,
    uint32_t pollLimit,
    HV_EFI_IOMMU_PROGRAM_PLAN *plan,
    HV_EFI_IOMMU_RUNTIME *runtime,
    HV_EFI_IOMMU_UNIT_RUNTIME_STATE *unitStates,
    uint32_t unitStateCount,
    const HV_EFI_IOMMU_MCFG_SEGMENT *segments,
    uint32_t segmentCount,
    const HV_EFI_IOMMU_PCI_CALLBACKS *pciCallbacks,
    HV_EFI_IOMMU_PCI_COMMAND_STATE *pciStates,
    uint32_t pciStateCapacity,
    uint32_t *pciStateCount,
    volatile uint32_t *dmaQuiescedState)
{
    HV_EFI_IOMMU_RUNTIME_STATUS status;
    HV_EFI_IOMMU_RUNTIME_STATUS restoreStatus;
    if (runtimeCallbacks == NULL || plan == NULL || runtime == NULL ||
        dmaQuiescedState == NULL || pciStateCount == NULL) {
        return HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
    }
    HvZero(runtime, sizeof(*runtime));
    *dmaQuiescedState = 0;
    *pciStateCount = 0;
    status = HvEfiIommuQuiescePciBusMasters(
        segments, segmentCount, pciCallbacks, pciStates, pciStateCapacity,
        pciStateCount);
    if (status != HV_EFI_IOMMU_RUNTIME_OK) {
        return status;
    }
    *dmaQuiescedState = 1;
    status = HvEfiIommuBuildProgrammingPlan(
        dmar, configurations, configurationCount, ownership,
        runtimeCallbacks, programStorage, pollLimit, plan);
    if (status == HV_EFI_IOMMU_RUNTIME_OK) {
        status = HvEfiIommuRuntimeInitialize(
            runtime, plan, unitStates, unitStateCount);
    }
    if (status == HV_EFI_IOMMU_RUNTIME_OK) {
        status = HvEfiIommuRuntimeEnable(runtime, runtimeCallbacks);
    }
    if (status == HV_EFI_IOMMU_RUNTIME_OK) {
        status = HvEfiIommuRuntimeAudit(runtime, runtimeCallbacks);
    }
    if (status != HV_EFI_IOMMU_RUNTIME_OK &&
        (runtime->State == HV_EFI_IOMMU_RUNTIME_ENABLING ||
         runtime->State == HV_EFI_IOMMU_RUNTIME_ENABLED ||
         runtime->State == HV_EFI_IOMMU_RUNTIME_FAILED)) {
        if (HvEfiIommuRuntimeRollback(runtime, runtimeCallbacks) !=
            HV_EFI_IOMMU_RUNTIME_OK) {
            status = HV_EFI_IOMMU_RUNTIME_ROLLBACK_FAILED;
        }
    }
    restoreStatus = HvEfiIommuRestorePciBusMasters(
        pciCallbacks, pciStates, *pciStateCount);
    *dmaQuiescedState = 0;
    if (restoreStatus != HV_EFI_IOMMU_RUNTIME_OK) {
        return restoreStatus;
    }
    return status;
}
