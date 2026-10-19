#include "iommu_isolation.h"

#include <stddef.h>

namespace {

void ZeroBytes(void *buffer, size_t size)
{
    uint8_t *bytes = static_cast<uint8_t *>(buffer);
    if (bytes == nullptr) {
        return;
    }
    for (size_t index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

void ClearPlan(HV_EFI_IOMMU_PLAN *plan)
{
    ZeroBytes(plan, sizeof(*plan));
}

bool BooleanValid(uint32_t value)
{
    return value <= 1;
}

bool AddChecked(uint64_t left, uint64_t right, uint64_t *result)
{
    if (result == nullptr || left > UINT64_MAX - right) {
        return false;
    }
    *result = left + right;
    return true;
}

bool AddCountChecked(uint32_t left, uint32_t right, uint32_t *result)
{
    if (result == nullptr || left > UINT32_MAX - right) {
        return false;
    }
    *result = left + right;
    return true;
}

uint64_t AddressLimit(uint32_t addressBits)
{
    return UINT64_C(1) << addressBits;
}

HV_EFI_IOMMU_STATUS ValidateRange(
    const HV_EFI_IOMMU_RANGE *range, uint64_t addressLimit)
{
    uint64_t end;
    if (range == nullptr || range->Size == 0 ||
        (range->PhysicalBase & HV_EFI_IOMMU_PAGE_MASK) != 0 ||
        (range->Size & HV_EFI_IOMMU_PAGE_MASK) != 0) {
        return HV_EFI_IOMMU_INVALID_RANGE;
    }
    if (!AddChecked(range->PhysicalBase, range->Size, &end) ||
        end > addressLimit) {
        return HV_EFI_IOMMU_RANGE_OVERFLOW;
    }
    return HV_EFI_IOMMU_OK;
}

uint64_t RangeEnd(const HV_EFI_IOMMU_RANGE *range)
{
    return range->PhysicalBase + range->Size;
}

bool RangesIntersect(
    uint64_t leftBase, uint64_t leftSize,
    uint64_t rightBase, uint64_t rightSize)
{
    return leftBase < rightBase + rightSize &&
        rightBase < leftBase + leftSize;
}

void SortRanges(HV_EFI_IOMMU_RANGE *ranges, uint32_t count)
{
    for (uint32_t index = 1; index < count; ++index) {
        const HV_EFI_IOMMU_RANGE value = ranges[index];
        uint32_t position = index;
        while (position != 0 &&
               ranges[position - 1].PhysicalBase > value.PhysicalBase) {
            ranges[position] = ranges[position - 1];
            --position;
        }
        ranges[position] = value;
    }
}

HV_EFI_IOMMU_STATUS NormalizeRanges(
    const HV_EFI_IOMMU_RANGE *input,
    uint32_t inputCount,
    HV_EFI_IOMMU_RANGE *output,
    uint32_t outputCapacity,
    uint64_t addressLimit,
    uint32_t *outputCount)
{
    if (outputCount == nullptr) {
        return HV_EFI_IOMMU_INVALID_ARGUMENT;
    }
    *outputCount = 0;
    if (inputCount == 0 || input == nullptr || output == nullptr) {
        return HV_EFI_IOMMU_INVALID_ARGUMENT;
    }
    if (outputCapacity < inputCount) {
        return HV_EFI_IOMMU_CAPACITY;
    }
    for (uint32_t index = 0; index < inputCount; ++index) {
        const HV_EFI_IOMMU_STATUS status =
            ValidateRange(&input[index], addressLimit);
        if (status != HV_EFI_IOMMU_OK) {
            return status;
        }
        output[index] = input[index];
    }
    SortRanges(output, inputCount);

    uint32_t count = 0;
    for (uint32_t index = 0; index < inputCount; ++index) {
        const HV_EFI_IOMMU_RANGE value = output[index];
        if (count == 0) {
            output[count++] = value;
            continue;
        }
        HV_EFI_IOMMU_RANGE *previous = &output[count - 1];
        const uint64_t previousEnd = RangeEnd(previous);
        if (value.PhysicalBase < previousEnd) {
            return HV_EFI_IOMMU_RANGE_OVERLAP;
        }
        if (value.PhysicalBase == previousEnd) {
            previous->Size += value.Size;
        } else {
            output[count++] = value;
        }
    }
    *outputCount = count;
    return HV_EFI_IOMMU_OK;
}

bool SameSourceId(
    const HV_EFI_IOMMU_DEVICE *left,
    const HV_EFI_IOMMU_DEVICE *right)
{
    return left->Segment == right->Segment &&
        left->Bus == right->Bus &&
        left->Device == right->Device &&
        left->Function == right->Function;
}

HV_EFI_IOMMU_STATUS ValidateDevicesAndUnits(
    const HV_EFI_IOMMU_TOPOLOGY *topology,
    uint32_t *planFlags)
{
    const HV_EFI_IOMMU_PLATFORM *platform = &topology->Platform;
    bool interruptRemapping = platform->RequireInterruptRemapping != 0;
    if (topology->Units == nullptr || topology->UnitCount == 0 ||
        topology->Devices == nullptr || topology->DeviceCount == 0 ||
        topology->DeviceCount > HV_EFI_IOMMU_MAX_DOMAIN_ID) {
        return HV_EFI_IOMMU_INVALID_ARGUMENT;
    }
    for (uint32_t index = 0; index < topology->DeviceCount; ++index) {
        const HV_EFI_IOMMU_DEVICE *device = &topology->Devices[index];
        if (device->RequiresInterruptRemapping != 0) {
            interruptRemapping = true;
        }
    }
    const bool queuedInvalidation =
        platform->RequireQueuedInvalidation != 0 || interruptRemapping;

    if (interruptRemapping &&
        platform->InterruptRemappingReported == 0) {
        return HV_EFI_IOMMU_INTERRUPT_REMAPPING_INCOMPATIBLE;
    }
    if (interruptRemapping && platform->X2ApicEnabled != 0 &&
        platform->X2ApicOptOut != 0) {
        return HV_EFI_IOMMU_INTERRUPT_REMAPPING_INCOMPATIBLE;
    }

    for (uint32_t index = 0; index < topology->UnitCount; ++index) {
        const HV_EFI_IOMMU_UNIT *unit = &topology->Units[index];
        uint64_t registerEnd;
        if (unit->Segment > UINT16_MAX ||
            !BooleanValid(unit->IncludeAll) ||
            !BooleanValid(unit->TranslationSupported) ||
            !BooleanValid(unit->FourKilobytePagesSupported) ||
            !BooleanValid(unit->QueuedInvalidationSupported) ||
            !BooleanValid(unit->InterruptRemappingSupported) ||
            !BooleanValid(unit->ExtendedInterruptModeSupported) ||
            unit->RegisterBase == 0 ||
            (unit->RegisterBase & HV_EFI_IOMMU_PAGE_MASK) != 0 ||
            !AddChecked(unit->RegisterBase, HV_EFI_IOMMU_PAGE_SIZE,
                        &registerEnd) ||
            registerEnd > AddressLimit(platform->DmaAddressBits)) {
            return HV_EFI_IOMMU_REMAPPING_UNSUPPORTED;
        }
        if (unit->TranslationSupported == 0 ||
            unit->FourKilobytePagesSupported == 0) {
            return HV_EFI_IOMMU_REMAPPING_UNSUPPORTED;
        }
        if (unit->AddressWidthBits < platform->DmaAddressBits ||
            unit->AddressWidthBits > HV_EFI_IOMMU_MAX_ADDRESS_BITS) {
            return HV_EFI_IOMMU_ADDRESS_WIDTH_UNSUPPORTED;
        }
        if (queuedInvalidation && unit->QueuedInvalidationSupported == 0) {
            return HV_EFI_IOMMU_QUEUED_INVALIDATION_INCOMPATIBLE;
        }
        if (interruptRemapping &&
            unit->InterruptRemappingSupported == 0) {
            return HV_EFI_IOMMU_INTERRUPT_REMAPPING_INCOMPATIBLE;
        }
        if (interruptRemapping && platform->X2ApicEnabled != 0 &&
            unit->ExtendedInterruptModeSupported == 0) {
            return HV_EFI_IOMMU_INTERRUPT_REMAPPING_INCOMPATIBLE;
        }
        for (uint32_t previous = 0; previous < index; ++previous) {
            const HV_EFI_IOMMU_UNIT *other = &topology->Units[previous];
            if (unit->RegisterBase == other->RegisterBase ||
                (unit->Segment == other->Segment && unit->IncludeAll != 0 &&
                 other->IncludeAll != 0)) {
                return HV_EFI_IOMMU_SCOPE_MISMATCH;
            }
        }
    }

    for (uint32_t index = 0; index < topology->DeviceCount; ++index) {
        const HV_EFI_IOMMU_DEVICE *device = &topology->Devices[index];
        if (device->Segment > UINT16_MAX || device->Bus > UINT8_MAX ||
            device->Device > 31 || device->Function > 7 ||
            (device->Kind != HV_EFI_IOMMU_DEVICE_DISCRETE &&
             device->Kind != HV_EFI_IOMMU_DEVICE_INTEGRATED) ||
            !BooleanValid(device->DmaCapable) ||
            !BooleanValid(device->RequiresInterruptRemapping)) {
            return HV_EFI_IOMMU_INVALID_DEVICE;
        }
        if (device->RemappingUnitIndex >= topology->UnitCount ||
            topology->Units[device->RemappingUnitIndex].Segment !=
                device->Segment) {
            return HV_EFI_IOMMU_SCOPE_MISMATCH;
        }
        for (uint32_t previous = 0; previous < index; ++previous) {
            if (SameSourceId(device, &topology->Devices[previous])) {
                return HV_EFI_IOMMU_INVALID_DEVICE;
            }
        }
    }

    *planFlags = 0;
    if (queuedInvalidation) {
        *planFlags |= HV_EFI_IOMMU_PLAN_USE_QUEUED_INVALIDATION;
    }
    if (interruptRemapping) {
        *planFlags |= HV_EFI_IOMMU_PLAN_USE_INTERRUPT_REMAPPING;
        if (platform->X2ApicEnabled != 0) {
            *planFlags |= HV_EFI_IOMMU_PLAN_USE_EXTENDED_INTERRUPT_MODE;
        }
    }
    return HV_EFI_IOMMU_OK;
}

HV_EFI_IOMMU_STATUS ValidateRmrrs(
    const HV_EFI_IOMMU_TOPOLOGY *topology,
    const HV_EFI_IOMMU_RANGE *denyRanges,
    uint32_t denyCount,
    uint64_t addressLimit)
{
    if (topology->RmrrRangeCount != 0 && topology->RmrrRanges == nullptr) {
        return HV_EFI_IOMMU_INVALID_ARGUMENT;
    }
    for (uint32_t index = 0; index < topology->RmrrRangeCount; ++index) {
        const HV_EFI_IOMMU_RMRR *rmrr = &topology->RmrrRanges[index];
        const HV_EFI_IOMMU_RANGE range = {
            rmrr->PhysicalBase, rmrr->Size
        };
        const HV_EFI_IOMMU_STATUS status =
            ValidateRange(&range, addressLimit);
        if (status != HV_EFI_IOMMU_OK) {
            return status;
        }
        if (rmrr->DeviceIndex >= topology->DeviceCount ||
            rmrr->Reserved != 0) {
            return HV_EFI_IOMMU_SCOPE_MISMATCH;
        }
        if (topology->Devices[rmrr->DeviceIndex].DmaCapable == 0) {
            return HV_EFI_IOMMU_SCOPE_MISMATCH;
        }
        for (uint32_t denyIndex = 0; denyIndex < denyCount; ++denyIndex) {
            if (RangesIntersect(rmrr->PhysicalBase, rmrr->Size,
                                denyRanges[denyIndex].PhysicalBase,
                                denyRanges[denyIndex].Size)) {
                return HV_EFI_IOMMU_RMRR_CONFLICT;
            }
        }
        for (uint32_t previous = 0; previous < index; ++previous) {
            const HV_EFI_IOMMU_RMRR *other = &topology->RmrrRanges[previous];
            if (rmrr->DeviceIndex == other->DeviceIndex &&
                RangesIntersect(rmrr->PhysicalBase, rmrr->Size,
                                other->PhysicalBase, other->Size)) {
                return HV_EFI_IOMMU_RANGE_OVERLAP;
            }
        }
    }
    return HV_EFI_IOMMU_OK;
}

uint32_t CountRmrrsForDevice(
    const HV_EFI_IOMMU_TOPOLOGY *topology, uint32_t deviceIndex)
{
    uint32_t count = 0;
    for (uint32_t index = 0; index < topology->RmrrRangeCount; ++index) {
        if (topology->RmrrRanges[index].DeviceIndex == deviceIndex) {
            ++count;
        }
    }
    return count;
}

bool CountGuestFragments(
    const HV_EFI_IOMMU_RANGE *guestRanges,
    uint32_t guestCount,
    const HV_EFI_IOMMU_RANGE *denyRanges,
    uint32_t denyCount,
    uint32_t *fragmentCount)
{
    uint32_t count = 0;
    for (uint32_t guestIndex = 0; guestIndex < guestCount; ++guestIndex) {
        uint64_t cursor = guestRanges[guestIndex].PhysicalBase;
        const uint64_t guestEnd = RangeEnd(&guestRanges[guestIndex]);
        for (uint32_t denyIndex = 0; denyIndex < denyCount; ++denyIndex) {
            const uint64_t denyEnd = RangeEnd(&denyRanges[denyIndex]);
            if (denyEnd <= cursor) {
                continue;
            }
            if (denyRanges[denyIndex].PhysicalBase >= guestEnd) {
                break;
            }
            if (denyRanges[denyIndex].PhysicalBase > cursor &&
                !AddCountChecked(count, 1, &count)) {
                return false;
            }
            if (denyEnd > cursor) {
                cursor = denyEnd;
            }
            if (cursor >= guestEnd) {
                break;
            }
        }
        if (cursor < guestEnd && !AddCountChecked(count, 1, &count)) {
            return false;
        }
    }
    *fragmentCount = count;
    return true;
}

uint32_t EmitGuestFragments(
    const HV_EFI_IOMMU_RANGE *guestRanges,
    uint32_t guestCount,
    const HV_EFI_IOMMU_RANGE *denyRanges,
    uint32_t denyCount,
    uint32_t domainId,
    HV_EFI_IOMMU_IDENTITY_MAPPING *mappings,
    uint32_t mappingIndex)
{
    for (uint32_t guestIndex = 0; guestIndex < guestCount; ++guestIndex) {
        uint64_t cursor = guestRanges[guestIndex].PhysicalBase;
        const uint64_t guestEnd = RangeEnd(&guestRanges[guestIndex]);
        for (uint32_t denyIndex = 0; denyIndex < denyCount; ++denyIndex) {
            const uint64_t denyEnd = RangeEnd(&denyRanges[denyIndex]);
            if (denyEnd <= cursor) {
                continue;
            }
            if (denyRanges[denyIndex].PhysicalBase >= guestEnd) {
                break;
            }
            if (denyRanges[denyIndex].PhysicalBase > cursor) {
                mappings[mappingIndex++] = {
                    cursor,
                    denyRanges[denyIndex].PhysicalBase - cursor,
                    domainId,
                    HV_EFI_IOMMU_MAPPING_GUEST_RAM
                };
            }
            if (denyEnd > cursor) {
                cursor = denyEnd;
            }
            if (cursor >= guestEnd) {
                break;
            }
        }
        if (cursor < guestEnd) {
            mappings[mappingIndex++] = {
                cursor, guestEnd - cursor, domainId,
                HV_EFI_IOMMU_MAPPING_GUEST_RAM
            };
        }
    }
    return mappingIndex;
}

void SortMappings(HV_EFI_IOMMU_IDENTITY_MAPPING *mappings, uint32_t count)
{
    for (uint32_t index = 1; index < count; ++index) {
        const HV_EFI_IOMMU_IDENTITY_MAPPING value = mappings[index];
        uint32_t position = index;
        while (position != 0 &&
               (mappings[position - 1].DomainId > value.DomainId ||
                (mappings[position - 1].DomainId == value.DomainId &&
                 mappings[position - 1].PhysicalBase > value.PhysicalBase))) {
            mappings[position] = mappings[position - 1];
            --position;
        }
        mappings[position] = value;
    }
}

uint32_t MergeMappings(
    HV_EFI_IOMMU_IDENTITY_MAPPING *mappings, uint32_t count)
{
    uint32_t mergedCount = 0;
    for (uint32_t index = 0; index < count; ++index) {
        const HV_EFI_IOMMU_IDENTITY_MAPPING value = mappings[index];
        if (mergedCount == 0) {
            mappings[mergedCount++] = value;
            continue;
        }
        HV_EFI_IOMMU_IDENTITY_MAPPING *previous =
            &mappings[mergedCount - 1];
        const uint64_t previousEnd = previous->PhysicalBase + previous->Size;
        const uint64_t valueEnd = value.PhysicalBase + value.Size;
        if (previous->DomainId == value.DomainId &&
            value.PhysicalBase <= previousEnd) {
            if (valueEnd > previousEnd) {
                previous->Size = valueEnd - previous->PhysicalBase;
            }
            previous->Flags |= value.Flags;
        } else {
            mappings[mergedCount++] = value;
        }
    }
    return mergedCount;
}

bool RangeCovered(
    const HV_EFI_IOMMU_PLAN *plan,
    uint32_t domainId,
    uint64_t base,
    uint64_t size)
{
    uint64_t cursor = base;
    const uint64_t end = base + size;
    for (uint32_t index = 0; index < plan->MappingCount; ++index) {
        const HV_EFI_IOMMU_IDENTITY_MAPPING *mapping =
            &plan->Mappings[index];
        if (mapping->DomainId < domainId) {
            continue;
        }
        if (mapping->DomainId > domainId ||
            mapping->PhysicalBase > cursor) {
            return false;
        }
        const uint64_t mappingEnd =
            mapping->PhysicalBase + mapping->Size;
        if (mappingEnd > cursor) {
            cursor = mappingEnd;
        }
        if (cursor >= end) {
            return true;
        }
    }
    return false;
}

bool GuestFragmentsCovered(
    const HV_EFI_IOMMU_PLAN *plan, uint32_t domainId)
{
    for (uint32_t guestIndex = 0;
         guestIndex < plan->GuestRamRangeCount; ++guestIndex) {
        uint64_t cursor = plan->GuestRamRanges[guestIndex].PhysicalBase;
        const uint64_t guestEnd = RangeEnd(&plan->GuestRamRanges[guestIndex]);
        for (uint32_t denyIndex = 0;
             denyIndex < plan->HypervisorDenyRangeCount; ++denyIndex) {
            const HV_EFI_IOMMU_RANGE *deny =
                &plan->HypervisorDenyRanges[denyIndex];
            const uint64_t denyEnd = RangeEnd(deny);
            if (denyEnd <= cursor) {
                continue;
            }
            if (deny->PhysicalBase >= guestEnd) {
                break;
            }
            if (deny->PhysicalBase > cursor &&
                !RangeCovered(plan, domainId, cursor,
                              deny->PhysicalBase - cursor)) {
                return false;
            }
            if (denyEnd > cursor) {
                cursor = denyEnd;
            }
            if (cursor >= guestEnd) {
                break;
            }
        }
        if (cursor < guestEnd &&
            !RangeCovered(plan, domainId, cursor, guestEnd - cursor)) {
            return false;
        }
    }
    return true;
}

bool MappingAuthorized(
    const HV_EFI_IOMMU_PLAN *plan,
    const HV_EFI_IOMMU_IDENTITY_MAPPING *mapping)
{
    uint64_t cursor = mapping->PhysicalBase;
    const uint64_t end = mapping->PhysicalBase + mapping->Size;
    const uint32_t deviceIndex = mapping->DomainId - 1;
    while (cursor < end) {
        uint64_t coveredEnd = cursor;
        for (uint32_t guestIndex = 0;
             guestIndex < plan->GuestRamRangeCount; ++guestIndex) {
            const HV_EFI_IOMMU_RANGE *guest =
                &plan->GuestRamRanges[guestIndex];
            const uint64_t guestEnd = RangeEnd(guest);
            if (guest->PhysicalBase <= cursor && cursor < guestEnd &&
                guestEnd > coveredEnd) {
                coveredEnd = guestEnd;
            }
        }
        for (uint32_t rmrrIndex = 0;
             rmrrIndex < plan->RmrrRangeCount; ++rmrrIndex) {
            const HV_EFI_IOMMU_RMRR *rmrr =
                &plan->RmrrRanges[rmrrIndex];
            const uint64_t rmrrEnd = rmrr->PhysicalBase + rmrr->Size;
            if (rmrr->DeviceIndex == deviceIndex &&
                rmrr->PhysicalBase <= cursor && cursor < rmrrEnd &&
                rmrrEnd > coveredEnd) {
                coveredEnd = rmrrEnd;
            }
        }
        if (coveredEnd == cursor) {
            return false;
        }
        cursor = coveredEnd;
    }
    return true;
}

bool NormalizedRangesValid(
    const HV_EFI_IOMMU_RANGE *ranges,
    uint32_t count,
    uint64_t addressLimit)
{
    if (ranges == nullptr || count == 0) {
        return false;
    }
    for (uint32_t index = 0; index < count; ++index) {
        if (ValidateRange(&ranges[index], addressLimit) != HV_EFI_IOMMU_OK) {
            return false;
        }
        if (index != 0 &&
            RangeEnd(&ranges[index - 1]) >= ranges[index].PhysicalBase) {
            return false;
        }
    }
    return true;
}

uint32_t ExpectedAssignmentFlags(
    const HV_EFI_IOMMU_PLAN *plan,
    uint32_t deviceIndex,
    uint32_t rmrrCount)
{
    const HV_EFI_IOMMU_DEVICE *device = &plan->Devices[deviceIndex];
    uint32_t flags = 0;
    if (device->Kind == HV_EFI_IOMMU_DEVICE_INTEGRATED) {
        flags |= HV_EFI_IOMMU_ASSIGNMENT_INTEGRATED;
    }
    if (device->DmaCapable != 0) {
        flags |= HV_EFI_IOMMU_ASSIGNMENT_DMA_CAPABLE;
    }
    if (rmrrCount != 0) {
        flags |= HV_EFI_IOMMU_ASSIGNMENT_HAS_RMRR;
    }
    if ((plan->Flags & HV_EFI_IOMMU_PLAN_USE_INTERRUPT_REMAPPING) != 0 ||
        device->RequiresInterruptRemapping != 0) {
        flags |= HV_EFI_IOMMU_ASSIGNMENT_INTERRUPT_REMAP;
    }
    return flags;
}

bool RootRangeCoveredByDenyInventory(
    const HV_EFI_IOMMU_PLAN *identityPlan,
    const HV_EFI_IOMMU_ROOT_DEVICE_RANGE *range)
{
    uint64_t cursor = range->PhysicalBase;
    const uint64_t end = range->PhysicalBase + range->Size;
    for (uint32_t index = 0;
         index < identityPlan->HypervisorDenyRangeCount && cursor < end;
         ++index) {
        const HV_EFI_IOMMU_RANGE *deny =
            &identityPlan->HypervisorDenyRanges[index];
        const uint64_t denyEnd = RangeEnd(deny);
        if (denyEnd <= cursor) {
            continue;
        }
        if (deny->PhysicalBase > cursor) {
            return false;
        }
        cursor = denyEnd < end ? denyEnd : end;
    }
    return cursor == end;
}

HV_EFI_IOMMU_STATUS ValidateRootDevicePlanFields(
    const HV_EFI_IOMMU_PLAN *identityPlan,
    const HV_EFI_IOMMU_ROOT_DEVICE_PLAN *rootPlan)
{
    uint32_t framebufferCount = 0;
    uint32_t translationCount = 0;
    uint64_t framebufferBytes = 0;
    uint64_t translationBytes = 0;
    uint64_t mappedBytes = 0;
    const uint64_t rootAddressLimit = UINT64_C(1) << 39;
    if (identityPlan == nullptr || rootPlan == nullptr ||
        identityPlan->Devices == nullptr ||
        identityPlan->Units == nullptr ||
        identityPlan->Assignments == nullptr ||
        rootPlan->DeviceIndex >= identityPlan->DeviceCount ||
        rootPlan->DomainId == 0 ||
        rootPlan->DomainId > HV_EFI_IOMMU_MAX_DOMAIN_ID ||
        rootPlan->MappingCount == 0 ||
        rootPlan->MappingCount > HV_EFI_IOMMU_ROOT_DEVICE_MAX_RANGES ||
        rootPlan->Reserved != 0) {
        return HV_EFI_IOMMU_INVALID_ARGUMENT;
    }
    const HV_EFI_IOMMU_DEVICE *device =
        &identityPlan->Devices[rootPlan->DeviceIndex];
    const HV_EFI_IOMMU_DOMAIN_ASSIGNMENT *assignment =
        &identityPlan->Assignments[rootPlan->DeviceIndex];
    if (device->Segment != 0 || rootPlan->Segment != 0 ||
        device->Bus != rootPlan->Bus ||
        device->Device != rootPlan->Device ||
        device->Function != rootPlan->Function ||
        device->Kind != HV_EFI_IOMMU_DEVICE_INTEGRATED ||
        device->DmaCapable == 0 ||
        device->RemappingUnitIndex != rootPlan->RemappingUnitIndex ||
        assignment->DeviceIndex != rootPlan->DeviceIndex ||
        rootPlan->LegacyDomainId == 0 ||
        rootPlan->LegacyDomainId > UINT16_MAX ||
        rootPlan->LegacyDomainId == rootPlan->DomainId ||
        identityPlan->Units[rootPlan->RemappingUnitIndex].Segment != 0 ||
        identityPlan->Units[rootPlan->RemappingUnitIndex]
                .FourKilobytePagesSupported == 0) {
        return HV_EFI_IOMMU_SCOPE_MISMATCH;
    }
    for (uint32_t index = 0; index < identityPlan->AssignmentCount; ++index) {
        if (identityPlan->Assignments[index].DomainId == rootPlan->DomainId) {
            return HV_EFI_IOMMU_SCOPE_MISMATCH;
        }
    }
    for (uint32_t index = 0; index < rootPlan->MappingCount; ++index) {
        const HV_EFI_IOMMU_ROOT_DEVICE_RANGE *range =
            &rootPlan->Mappings[index];
        const HV_EFI_IOMMU_RANGE bounds = {
            range->PhysicalBase, range->Size
        };
        if ((range->Kind != HV_EFI_IOMMU_ROOT_DEVICE_RANGE_FRAMEBUFFER &&
             range->Kind != HV_EFI_IOMMU_ROOT_DEVICE_RANGE_TRANSLATION) ||
            range->Access != HV_EFI_IOMMU_ROOT_DEVICE_ACCESS_READ) {
            return HV_EFI_IOMMU_INVALID_ARGUMENT;
        }
        if (ValidateRange(&bounds, rootAddressLimit) != HV_EFI_IOMMU_OK) {
            return HV_EFI_IOMMU_RANGE_OVERFLOW;
        }
        if (index != 0 &&
            rootPlan->Mappings[index - 1].PhysicalBase +
                    rootPlan->Mappings[index - 1].Size >
                range->PhysicalBase) {
            return HV_EFI_IOMMU_RANGE_OVERLAP;
        }
        if (!RootRangeCoveredByDenyInventory(identityPlan, range)) {
            return HV_EFI_IOMMU_DENY_VIOLATION;
        }
        for (uint32_t mappingIndex = 0;
             mappingIndex < identityPlan->MappingCount; ++mappingIndex) {
            const HV_EFI_IOMMU_IDENTITY_MAPPING *mapping =
                &identityPlan->Mappings[mappingIndex];
            if (RangesIntersect(range->PhysicalBase, range->Size,
                                mapping->PhysicalBase, mapping->Size)) {
                return HV_EFI_IOMMU_DENY_VIOLATION;
            }
        }
        if (range->Kind == HV_EFI_IOMMU_ROOT_DEVICE_RANGE_FRAMEBUFFER) {
            ++framebufferCount;
            if (!AddChecked(framebufferBytes, range->Size,
                            &framebufferBytes)) {
                return HV_EFI_IOMMU_RANGE_OVERFLOW;
            }
        } else {
            ++translationCount;
            if (!AddChecked(translationBytes, range->Size,
                            &translationBytes)) {
                return HV_EFI_IOMMU_RANGE_OVERFLOW;
            }
        }
        if (!AddChecked(mappedBytes, range->Size, &mappedBytes)) {
            return HV_EFI_IOMMU_RANGE_OVERFLOW;
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
        rootPlan->FramebufferBytes != framebufferBytes ||
        rootPlan->TranslationBytes != translationBytes ||
        rootPlan->MappedBytes != mappedBytes) {
        return HV_EFI_IOMMU_INVARIANT;
    }
    return HV_EFI_IOMMU_OK;
}

} // namespace

extern "C" HV_EFI_IOMMU_STATUS HvEfiIommuBuildIdentityPlan(
    const HV_EFI_IOMMU_TOPOLOGY *topology,
    const HV_EFI_IOMMU_PLAN_STORAGE *storage,
    HV_EFI_IOMMU_PLAN *plan)
{
    uint32_t guestCount = 0;
    uint32_t denyCount = 0;
    uint32_t fragmentCount = 0;
    uint32_t rawMappingCount = 0;
    uint32_t planFlags = 0;
    uint32_t mappingIndex = 0;
    HV_EFI_IOMMU_STATUS status;

    if (plan == nullptr) {
        return HV_EFI_IOMMU_INVALID_ARGUMENT;
    }
    ClearPlan(plan);
    if (topology == nullptr || storage == nullptr) {
        return HV_EFI_IOMMU_INVALID_ARGUMENT;
    }
    const HV_EFI_IOMMU_PLATFORM *platform = &topology->Platform;
    if (!BooleanValid(platform->DmarPresent) ||
        !BooleanValid(platform->DeviceEnumerationComplete) ||
        !BooleanValid(platform->RequireQueuedInvalidation) ||
        !BooleanValid(platform->RequireInterruptRemapping) ||
        !BooleanValid(platform->InterruptRemappingReported) ||
        !BooleanValid(platform->X2ApicEnabled) ||
        !BooleanValid(platform->X2ApicOptOut)) {
        return HV_EFI_IOMMU_INVALID_ARGUMENT;
    }
    if (platform->DmarPresent == 0) {
        return HV_EFI_IOMMU_DMAR_ABSENT;
    }
    if (platform->DeviceEnumerationComplete == 0) {
        return HV_EFI_IOMMU_ENUMERATION_INCOMPLETE;
    }
    if (platform->DmaAddressBits < HV_EFI_IOMMU_MIN_ADDRESS_BITS ||
        platform->DmaAddressBits > HV_EFI_IOMMU_MAX_ADDRESS_BITS) {
        return HV_EFI_IOMMU_ADDRESS_WIDTH_UNSUPPORTED;
    }
    const uint64_t addressLimit = AddressLimit(platform->DmaAddressBits);
    status = NormalizeRanges(
        topology->GuestRamRanges, topology->GuestRamRangeCount,
        storage->NormalizedGuestRamRanges,
        storage->NormalizedGuestRamCapacity,
        addressLimit, &guestCount);
    if (status != HV_EFI_IOMMU_OK) {
        return status;
    }
    status = NormalizeRanges(
        topology->HypervisorDenyRanges,
        topology->HypervisorDenyRangeCount,
        storage->NormalizedDenyRanges,
        storage->NormalizedDenyCapacity,
        addressLimit, &denyCount);
    if (status != HV_EFI_IOMMU_OK) {
        return status;
    }
    status = ValidateDevicesAndUnits(topology, &planFlags);
    if (status != HV_EFI_IOMMU_OK) {
        return status;
    }
    status = ValidateRmrrs(
        topology, storage->NormalizedDenyRanges, denyCount, addressLimit);
    if (status != HV_EFI_IOMMU_OK) {
        return status;
    }
    if (storage->Assignments == nullptr ||
        storage->AssignmentCapacity < topology->DeviceCount) {
        return HV_EFI_IOMMU_CAPACITY;
    }
    if (!CountGuestFragments(
            storage->NormalizedGuestRamRanges, guestCount,
            storage->NormalizedDenyRanges, denyCount, &fragmentCount) ||
        fragmentCount == 0) {
        return HV_EFI_IOMMU_INVARIANT;
    }
    for (uint32_t index = 0; index < topology->DeviceCount; ++index) {
        if (!AddCountChecked(rawMappingCount, fragmentCount,
                             &rawMappingCount)) {
            return HV_EFI_IOMMU_CAPACITY;
        }
    }
    if (!AddCountChecked(rawMappingCount, topology->RmrrRangeCount,
                         &rawMappingCount) ||
        storage->MappingCapacity < rawMappingCount ||
        (rawMappingCount != 0 && storage->Mappings == nullptr)) {
        return HV_EFI_IOMMU_CAPACITY;
    }

    for (uint32_t deviceIndex = 0;
         deviceIndex < topology->DeviceCount; ++deviceIndex) {
        const uint32_t domainId = deviceIndex + 1;
        const uint32_t rmrrCount =
            CountRmrrsForDevice(topology, deviceIndex);
        plan->Devices = topology->Devices;
        plan->Flags = planFlags;
        storage->Assignments[deviceIndex] = {
            deviceIndex,
            domainId,
            topology->Devices[deviceIndex].RemappingUnitIndex,
            ExpectedAssignmentFlags(plan, deviceIndex, rmrrCount),
            rmrrCount,
            0
        };
        mappingIndex = EmitGuestFragments(
            storage->NormalizedGuestRamRanges, guestCount,
            storage->NormalizedDenyRanges, denyCount,
            domainId, storage->Mappings, mappingIndex);
        for (uint32_t rmrrIndex = 0;
             rmrrIndex < topology->RmrrRangeCount; ++rmrrIndex) {
            const HV_EFI_IOMMU_RMRR *rmrr =
                &topology->RmrrRanges[rmrrIndex];
            if (rmrr->DeviceIndex == deviceIndex) {
                storage->Mappings[mappingIndex++] = {
                    rmrr->PhysicalBase,
                    rmrr->Size,
                    domainId,
                    HV_EFI_IOMMU_PLAN_MAPPING_RMRR
                };
            }
        }
    }
    if (mappingIndex != rawMappingCount) {
        ClearPlan(plan);
        return HV_EFI_IOMMU_INVARIANT;
    }
    SortMappings(storage->Mappings, mappingIndex);
    mappingIndex = MergeMappings(storage->Mappings, mappingIndex);

    plan->Units = topology->Units;
    plan->UnitCount = topology->UnitCount;
    plan->Devices = topology->Devices;
    plan->DeviceCount = topology->DeviceCount;
    plan->RmrrRanges = topology->RmrrRanges;
    plan->RmrrRangeCount = topology->RmrrRangeCount;
    plan->GuestRamRanges = storage->NormalizedGuestRamRanges;
    plan->GuestRamRangeCount = guestCount;
    plan->HypervisorDenyRanges = storage->NormalizedDenyRanges;
    plan->HypervisorDenyRangeCount = denyCount;
    plan->Assignments = storage->Assignments;
    plan->AssignmentCount = topology->DeviceCount;
    plan->Mappings = storage->Mappings;
    plan->MappingCount = mappingIndex;
    plan->AddressBits = platform->DmaAddressBits;
    plan->Flags = planFlags;

    status = HvEfiIommuValidateIdentityPlan(plan, nullptr, nullptr);
    if (status != HV_EFI_IOMMU_OK) {
        ClearPlan(plan);
        return status;
    }
    return HV_EFI_IOMMU_OK;
}

extern "C" HV_EFI_IOMMU_STATUS HvEfiIommuValidateIdentityPlan(
    const HV_EFI_IOMMU_PLAN *plan,
    uint64_t *checkedDenyRanges,
    uint64_t *checkedMappings)
{
    if (checkedDenyRanges != nullptr) {
        *checkedDenyRanges = 0;
    }
    if (checkedMappings != nullptr) {
        *checkedMappings = 0;
    }
    if (plan == nullptr || plan->Units == nullptr || plan->UnitCount == 0 ||
        plan->Devices == nullptr || plan->DeviceCount == 0 ||
        plan->GuestRamRanges == nullptr || plan->GuestRamRangeCount == 0 ||
        plan->HypervisorDenyRanges == nullptr ||
        plan->HypervisorDenyRangeCount == 0 ||
        plan->Assignments == nullptr ||
        plan->AssignmentCount != plan->DeviceCount ||
        plan->Mappings == nullptr || plan->MappingCount == 0 ||
        plan->AddressBits < HV_EFI_IOMMU_MIN_ADDRESS_BITS ||
        plan->AddressBits > HV_EFI_IOMMU_MAX_ADDRESS_BITS ||
        (plan->Flags & ~(HV_EFI_IOMMU_PLAN_USE_QUEUED_INVALIDATION |
                         HV_EFI_IOMMU_PLAN_USE_INTERRUPT_REMAPPING |
                         HV_EFI_IOMMU_PLAN_USE_EXTENDED_INTERRUPT_MODE)) != 0 ||
        ((plan->Flags & HV_EFI_IOMMU_PLAN_USE_EXTENDED_INTERRUPT_MODE) != 0 &&
         (plan->Flags & HV_EFI_IOMMU_PLAN_USE_INTERRUPT_REMAPPING) == 0) ||
        ((plan->Flags & HV_EFI_IOMMU_PLAN_USE_INTERRUPT_REMAPPING) != 0 &&
         (plan->Flags & HV_EFI_IOMMU_PLAN_USE_QUEUED_INVALIDATION) == 0) ||
        (plan->RmrrRangeCount != 0 && plan->RmrrRanges == nullptr)) {
        return HV_EFI_IOMMU_INVALID_ARGUMENT;
    }
    const uint64_t addressLimit = AddressLimit(plan->AddressBits);
    if (!NormalizedRangesValid(
            plan->GuestRamRanges, plan->GuestRamRangeCount, addressLimit) ||
        !NormalizedRangesValid(
            plan->HypervisorDenyRanges,
            plan->HypervisorDenyRangeCount, addressLimit)) {
        return HV_EFI_IOMMU_INVARIANT;
    }
    for (uint32_t unitIndex = 0;
         unitIndex < plan->UnitCount; ++unitIndex) {
        const HV_EFI_IOMMU_UNIT *unit = &plan->Units[unitIndex];
        if (unit->TranslationSupported == 0 ||
            unit->FourKilobytePagesSupported == 0 ||
            unit->AddressWidthBits < plan->AddressBits ||
            ((plan->Flags &
              HV_EFI_IOMMU_PLAN_USE_QUEUED_INVALIDATION) != 0 &&
             unit->QueuedInvalidationSupported == 0) ||
            ((plan->Flags &
              HV_EFI_IOMMU_PLAN_USE_INTERRUPT_REMAPPING) != 0 &&
             unit->InterruptRemappingSupported == 0) ||
            ((plan->Flags &
              HV_EFI_IOMMU_PLAN_USE_EXTENDED_INTERRUPT_MODE) != 0 &&
             unit->ExtendedInterruptModeSupported == 0)) {
            return HV_EFI_IOMMU_INVARIANT;
        }
    }
    for (uint32_t rmrrIndex = 0;
         rmrrIndex < plan->RmrrRangeCount; ++rmrrIndex) {
        const HV_EFI_IOMMU_RMRR *rmrr = &plan->RmrrRanges[rmrrIndex];
        const HV_EFI_IOMMU_RANGE range = {
            rmrr->PhysicalBase, rmrr->Size
        };
        if (rmrr->DeviceIndex >= plan->DeviceCount || rmrr->Reserved != 0 ||
            plan->Devices[rmrr->DeviceIndex].DmaCapable == 0 ||
            ValidateRange(&range, addressLimit) != HV_EFI_IOMMU_OK) {
            return HV_EFI_IOMMU_INVARIANT;
        }
        for (uint32_t denyIndex = 0;
             denyIndex < plan->HypervisorDenyRangeCount; ++denyIndex) {
            if (RangesIntersect(
                    rmrr->PhysicalBase, rmrr->Size,
                    plan->HypervisorDenyRanges[denyIndex].PhysicalBase,
                    plan->HypervisorDenyRanges[denyIndex].Size)) {
                return HV_EFI_IOMMU_RMRR_CONFLICT;
            }
        }
        for (uint32_t previous = 0; previous < rmrrIndex; ++previous) {
            const HV_EFI_IOMMU_RMRR *other = &plan->RmrrRanges[previous];
            if (other->DeviceIndex == rmrr->DeviceIndex &&
                RangesIntersect(other->PhysicalBase, other->Size,
                                rmrr->PhysicalBase, rmrr->Size)) {
                return HV_EFI_IOMMU_INVARIANT;
            }
        }
    }
    for (uint32_t index = 0; index < plan->MappingCount; ++index) {
        const HV_EFI_IOMMU_IDENTITY_MAPPING *mapping =
            &plan->Mappings[index];
        const HV_EFI_IOMMU_RANGE range = {
            mapping->PhysicalBase, mapping->Size
        };
        if (ValidateRange(&range, addressLimit) != HV_EFI_IOMMU_OK ||
            mapping->DomainId == 0 ||
            mapping->DomainId > plan->DeviceCount ||
            mapping->Flags == 0 ||
            (mapping->Flags & ~(HV_EFI_IOMMU_MAPPING_GUEST_RAM |
                                HV_EFI_IOMMU_PLAN_MAPPING_RMRR)) != 0 ||
            !MappingAuthorized(plan, mapping)) {
            return HV_EFI_IOMMU_INVARIANT;
        }
        if (index != 0) {
            const HV_EFI_IOMMU_IDENTITY_MAPPING *previous =
                &plan->Mappings[index - 1];
            if (previous->DomainId > mapping->DomainId ||
                (previous->DomainId == mapping->DomainId &&
                 previous->PhysicalBase + previous->Size >=
                     mapping->PhysicalBase)) {
                return HV_EFI_IOMMU_INVARIANT;
            }
        }
        for (uint32_t denyIndex = 0;
             denyIndex < plan->HypervisorDenyRangeCount; ++denyIndex) {
            const HV_EFI_IOMMU_RANGE *deny =
                &plan->HypervisorDenyRanges[denyIndex];
            if (RangesIntersect(mapping->PhysicalBase, mapping->Size,
                                deny->PhysicalBase, deny->Size)) {
                return HV_EFI_IOMMU_DENY_VIOLATION;
            }
        }
    }

    for (uint32_t deviceIndex = 0;
         deviceIndex < plan->DeviceCount; ++deviceIndex) {
        const HV_EFI_IOMMU_DEVICE *device = &plan->Devices[deviceIndex];
        const HV_EFI_IOMMU_DOMAIN_ASSIGNMENT *assignment =
            &plan->Assignments[deviceIndex];
        uint32_t rmrrCount = 0;
        for (uint32_t rmrrIndex = 0;
             rmrrIndex < plan->RmrrRangeCount; ++rmrrIndex) {
            if (plan->RmrrRanges[rmrrIndex].DeviceIndex == deviceIndex) {
                ++rmrrCount;
            }
        }
        if (device->RemappingUnitIndex >= plan->UnitCount ||
            plan->Units[device->RemappingUnitIndex].Segment !=
                device->Segment ||
            assignment->DeviceIndex != deviceIndex ||
            assignment->DomainId != deviceIndex + 1 ||
            assignment->RemappingUnitIndex !=
                plan->Devices[deviceIndex].RemappingUnitIndex ||
            assignment->RmrrCount != rmrrCount || assignment->Reserved != 0 ||
            assignment->Flags !=
                ExpectedAssignmentFlags(plan, deviceIndex, rmrrCount) ||
            !GuestFragmentsCovered(plan, assignment->DomainId)) {
            return HV_EFI_IOMMU_INVARIANT;
        }
        for (uint32_t rmrrIndex = 0;
             rmrrIndex < plan->RmrrRangeCount; ++rmrrIndex) {
            const HV_EFI_IOMMU_RMRR *rmrr =
                &plan->RmrrRanges[rmrrIndex];
            if (rmrr->DeviceIndex == deviceIndex &&
                !RangeCovered(plan, assignment->DomainId,
                              rmrr->PhysicalBase, rmrr->Size)) {
                return HV_EFI_IOMMU_INVARIANT;
            }
        }
    }
    if (checkedDenyRanges != nullptr) {
        *checkedDenyRanges = plan->HypervisorDenyRangeCount;
    }
    if (checkedMappings != nullptr) {
        *checkedMappings = plan->MappingCount;
    }
    return HV_EFI_IOMMU_OK;
}

extern "C" HV_EFI_IOMMU_STATUS HvEfiIommuPlanMappingAt(
    const HV_EFI_IOMMU_PLAN *plan,
    uint32_t deviceIndex,
    uint64_t physicalAddress,
    HV_EFI_IOMMU_MAPPING_RESULT *result)
{
    if (result == nullptr) {
        return HV_EFI_IOMMU_INVALID_ARGUMENT;
    }
    ZeroBytes(result, sizeof(*result));
    if (plan == nullptr || plan->Assignments == nullptr ||
        plan->Mappings == nullptr || deviceIndex >= plan->DeviceCount ||
        plan->AddressBits < HV_EFI_IOMMU_MIN_ADDRESS_BITS ||
        plan->AddressBits > HV_EFI_IOMMU_MAX_ADDRESS_BITS ||
        physicalAddress >= AddressLimit(plan->AddressBits)) {
        return HV_EFI_IOMMU_INVALID_ARGUMENT;
    }
    const uint32_t domainId = plan->Assignments[deviceIndex].DomainId;
    result->DomainId = domainId;
    for (uint32_t index = 0; index < plan->MappingCount; ++index) {
        const HV_EFI_IOMMU_IDENTITY_MAPPING *mapping =
            &plan->Mappings[index];
        if (mapping->DomainId == domainId &&
            mapping->PhysicalBase <= physicalAddress &&
            physicalAddress < mapping->PhysicalBase + mapping->Size) {
            result->Mapped = 1;
            result->Flags = mapping->Flags;
            return HV_EFI_IOMMU_OK;
        }
    }
    return HV_EFI_IOMMU_OK;
}

extern "C" HV_EFI_IOMMU_STATUS HvEfiIommuBuildRootDevicePlan(
    const HV_EFI_IOMMU_PLAN *identityPlan,
    uint32_t deviceIndex,
    uint32_t legacyDomainId,
    uint32_t domainId,
    const HV_EFI_IOMMU_ROOT_DEVICE_RANGE *ranges,
    uint32_t rangeCount,
    HV_EFI_IOMMU_ROOT_DEVICE_PLAN *rootPlan)
{
    HV_EFI_IOMMU_STATUS status;
    if (rootPlan == nullptr) {
        return HV_EFI_IOMMU_INVALID_ARGUMENT;
    }
    ZeroBytes(rootPlan, sizeof(*rootPlan));
    if (identityPlan == nullptr || ranges == nullptr ||
        rangeCount == 0 ||
        rangeCount > HV_EFI_IOMMU_ROOT_DEVICE_MAX_RANGES ||
        legacyDomainId == 0 || legacyDomainId > UINT16_MAX ||
        domainId == 0 ||
        domainId > HV_EFI_IOMMU_MAX_DOMAIN_ID) {
        return HV_EFI_IOMMU_INVALID_ARGUMENT;
    }
    status = HvEfiIommuValidateIdentityPlan(identityPlan, nullptr, nullptr);
    if (status != HV_EFI_IOMMU_OK) {
        return status;
    }
    if (deviceIndex >= identityPlan->DeviceCount) {
        return HV_EFI_IOMMU_INVALID_ARGUMENT;
    }
    const HV_EFI_IOMMU_DOMAIN_ASSIGNMENT *assignment =
        &identityPlan->Assignments[deviceIndex];
    rootPlan->DeviceIndex = deviceIndex;
    rootPlan->Segment = identityPlan->Devices[deviceIndex].Segment;
    rootPlan->Bus = identityPlan->Devices[deviceIndex].Bus;
    rootPlan->Device = identityPlan->Devices[deviceIndex].Device;
    rootPlan->Function = identityPlan->Devices[deviceIndex].Function;
    rootPlan->RemappingUnitIndex = assignment->RemappingUnitIndex;
    rootPlan->LegacyDomainId = legacyDomainId;
    rootPlan->DomainId = domainId;
    rootPlan->MappingCount = rangeCount;
    for (uint32_t index = 0; index < rangeCount; ++index) {
        rootPlan->Mappings[index] = ranges[index];
    }
    for (uint32_t index = 1; index < rangeCount; ++index) {
        const HV_EFI_IOMMU_ROOT_DEVICE_RANGE value =
            rootPlan->Mappings[index];
        uint32_t position = index;
        while (position != 0 &&
               rootPlan->Mappings[position - 1].PhysicalBase >
                   value.PhysicalBase) {
            rootPlan->Mappings[position] =
                rootPlan->Mappings[position - 1];
            --position;
        }
        rootPlan->Mappings[position] = value;
    }
    for (uint32_t index = 0; index < rangeCount; ++index) {
        const HV_EFI_IOMMU_ROOT_DEVICE_RANGE *range =
            &rootPlan->Mappings[index];
        if (range->Kind ==
            HV_EFI_IOMMU_ROOT_DEVICE_RANGE_FRAMEBUFFER) {
            if (!AddChecked(rootPlan->FramebufferBytes,
                            range->Size,
                            &rootPlan->FramebufferBytes)) {
                ZeroBytes(rootPlan, sizeof(*rootPlan));
                return HV_EFI_IOMMU_RANGE_OVERFLOW;
            }
        } else if (range->Kind ==
                   HV_EFI_IOMMU_ROOT_DEVICE_RANGE_TRANSLATION) {
            if (!AddChecked(rootPlan->TranslationBytes,
                            range->Size,
                            &rootPlan->TranslationBytes)) {
                ZeroBytes(rootPlan, sizeof(*rootPlan));
                return HV_EFI_IOMMU_RANGE_OVERFLOW;
            }
        }
        if (!AddChecked(rootPlan->MappedBytes, range->Size,
                        &rootPlan->MappedBytes)) {
            ZeroBytes(rootPlan, sizeof(*rootPlan));
            return HV_EFI_IOMMU_RANGE_OVERFLOW;
        }
    }
    status = ValidateRootDevicePlanFields(identityPlan, rootPlan);
    if (status != HV_EFI_IOMMU_OK) {
        ZeroBytes(rootPlan, sizeof(*rootPlan));
    }
    return status;
}

extern "C" HV_EFI_IOMMU_STATUS HvEfiIommuValidateRootDevicePlan(
    const HV_EFI_IOMMU_PLAN *identityPlan,
    const HV_EFI_IOMMU_ROOT_DEVICE_PLAN *rootPlan)
{
    const HV_EFI_IOMMU_STATUS identityStatus =
        HvEfiIommuValidateIdentityPlan(identityPlan, nullptr, nullptr);
    if (identityStatus != HV_EFI_IOMMU_OK) {
        return identityStatus;
    }
    return ValidateRootDevicePlanFields(identityPlan, rootPlan);
}
