#ifndef HYPERSINV_EFI_IOMMU_ISOLATION_H
#define HYPERSINV_EFI_IOMMU_ISOLATION_H

/*
 * Pure Intel VT-d/DMAR admission and identity-domain planner.
 *
 * The caller parses ACPI DMAR and enumerates every DMA-capable device before
 * invoking this module.  No register, root-table, context-table, interrupt-
 * remapping-table, or invalidation-queue write is performed here.  A
 * successful plan describes the mappings that a separate hardware adapter
 * must install transactionally before bus mastering is admitted.
 *
 * Every physical range is 4-KiB aligned and half-open.  Guest identity
 * domains contain guest-owned RAM with hypervisor-owned ranges carved out,
 * plus only the RMRR ranges scoped to that domain's device.  Unlisted memory
 * is unmapped.  All buffers and input descriptions remain caller-owned.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HV_EFI_IOMMU_PAGE_SHIFT UINT32_C(12)
#define HV_EFI_IOMMU_PAGE_SIZE (UINT64_C(1) << HV_EFI_IOMMU_PAGE_SHIFT)
#define HV_EFI_IOMMU_PAGE_MASK (HV_EFI_IOMMU_PAGE_SIZE - UINT64_C(1))
#define HV_EFI_IOMMU_MIN_ADDRESS_BITS UINT32_C(39)
#define HV_EFI_IOMMU_MAX_ADDRESS_BITS UINT32_C(52)
#define HV_EFI_IOMMU_MAX_DOMAIN_ID UINT32_C(65534)

#define HV_EFI_IOMMU_PLAN_USE_QUEUED_INVALIDATION UINT32_C(0x00000001)
#define HV_EFI_IOMMU_PLAN_USE_INTERRUPT_REMAPPING UINT32_C(0x00000002)
#define HV_EFI_IOMMU_PLAN_USE_EXTENDED_INTERRUPT_MODE UINT32_C(0x00000004)

#define HV_EFI_IOMMU_ASSIGNMENT_INTEGRATED UINT32_C(0x00000001)
#define HV_EFI_IOMMU_ASSIGNMENT_DMA_CAPABLE UINT32_C(0x00000002)
#define HV_EFI_IOMMU_ASSIGNMENT_HAS_RMRR UINT32_C(0x00000004)
#define HV_EFI_IOMMU_ASSIGNMENT_INTERRUPT_REMAP UINT32_C(0x00000008)

#define HV_EFI_IOMMU_MAPPING_GUEST_RAM UINT32_C(0x00000001)
#define HV_EFI_IOMMU_MAPPING_RMRR UINT32_C(0x00000002)

typedef enum HV_EFI_IOMMU_STATUS {
    HV_EFI_IOMMU_OK = 0,
    HV_EFI_IOMMU_INVALID_ARGUMENT = 1,
    HV_EFI_IOMMU_DMAR_ABSENT = 2,
    HV_EFI_IOMMU_ENUMERATION_INCOMPLETE = 3,
    HV_EFI_IOMMU_REMAPPING_UNSUPPORTED = 4,
    HV_EFI_IOMMU_ADDRESS_WIDTH_UNSUPPORTED = 5,
    HV_EFI_IOMMU_QUEUED_INVALIDATION_INCOMPATIBLE = 6,
    HV_EFI_IOMMU_INTERRUPT_REMAPPING_INCOMPATIBLE = 7,
    HV_EFI_IOMMU_INVALID_DEVICE = 8,
    HV_EFI_IOMMU_SCOPE_MISMATCH = 9,
    HV_EFI_IOMMU_INVALID_RANGE = 10,
    HV_EFI_IOMMU_RANGE_OVERFLOW = 11,
    HV_EFI_IOMMU_RANGE_OVERLAP = 12,
    HV_EFI_IOMMU_RMRR_CONFLICT = 13,
    HV_EFI_IOMMU_CAPACITY = 14,
    HV_EFI_IOMMU_DENY_VIOLATION = 15,
    HV_EFI_IOMMU_INVARIANT = 16
} HV_EFI_IOMMU_STATUS;

typedef enum HV_EFI_IOMMU_DEVICE_KIND {
    HV_EFI_IOMMU_DEVICE_DISCRETE = 1,
    HV_EFI_IOMMU_DEVICE_INTEGRATED = 2
} HV_EFI_IOMMU_DEVICE_KIND;

typedef struct HV_EFI_IOMMU_PLATFORM {
    uint32_t DmarPresent;
    uint32_t DeviceEnumerationComplete;
    uint32_t DmaAddressBits;
    uint32_t RequireQueuedInvalidation;
    uint32_t RequireInterruptRemapping;
    uint32_t InterruptRemappingReported;
    uint32_t X2ApicEnabled;
    uint32_t X2ApicOptOut;
} HV_EFI_IOMMU_PLATFORM;

/* One parsed DMAR remapping-hardware definition (DRHD). */
typedef struct HV_EFI_IOMMU_UNIT {
    uint64_t RegisterBase;
    uint32_t Segment;
    uint32_t IncludeAll;
    uint32_t TranslationSupported;
    uint32_t FourKilobytePagesSupported;
    uint32_t AddressWidthBits;
    uint32_t QueuedInvalidationSupported;
    uint32_t InterruptRemappingSupported;
    uint32_t ExtendedInterruptModeSupported;
} HV_EFI_IOMMU_UNIT;

/* Device paths and bridge scopes must be expanded to exact source IDs. */
typedef struct HV_EFI_IOMMU_DEVICE {
    uint32_t Segment;
    uint32_t Bus;
    uint32_t Device;
    uint32_t Function;
    uint32_t Kind;
    uint32_t DmaCapable;
    uint32_t RequiresInterruptRemapping;
    uint32_t RemappingUnitIndex;
} HV_EFI_IOMMU_DEVICE;

typedef struct HV_EFI_IOMMU_RANGE {
    uint64_t PhysicalBase;
    uint64_t Size;
} HV_EFI_IOMMU_RANGE;

/* One parsed Reserved Memory Region Reporting structure and exact scope. */
typedef struct HV_EFI_IOMMU_RMRR {
    uint64_t PhysicalBase;
    uint64_t Size;
    uint32_t DeviceIndex;
    uint32_t Reserved;
} HV_EFI_IOMMU_RMRR;

typedef struct HV_EFI_IOMMU_TOPOLOGY {
    HV_EFI_IOMMU_PLATFORM Platform;
    const HV_EFI_IOMMU_UNIT *Units;
    uint32_t UnitCount;
    const HV_EFI_IOMMU_DEVICE *Devices;
    uint32_t DeviceCount;
    const HV_EFI_IOMMU_RANGE *GuestRamRanges;
    uint32_t GuestRamRangeCount;
    const HV_EFI_IOMMU_RANGE *HypervisorDenyRanges;
    uint32_t HypervisorDenyRangeCount;
    const HV_EFI_IOMMU_RMRR *RmrrRanges;
    uint32_t RmrrRangeCount;
} HV_EFI_IOMMU_TOPOLOGY;

typedef struct HV_EFI_IOMMU_DOMAIN_ASSIGNMENT {
    uint32_t DeviceIndex;
    uint32_t DomainId;
    uint32_t RemappingUnitIndex;
    uint32_t Flags;
    uint32_t RmrrCount;
    uint32_t Reserved;
} HV_EFI_IOMMU_DOMAIN_ASSIGNMENT;

typedef struct HV_EFI_IOMMU_IDENTITY_MAPPING {
    uint64_t PhysicalBase;
    uint64_t Size;
    uint32_t DomainId;
    uint32_t Flags;
} HV_EFI_IOMMU_IDENTITY_MAPPING;

typedef struct HV_EFI_IOMMU_PLAN_STORAGE {
    HV_EFI_IOMMU_RANGE *NormalizedGuestRamRanges;
    uint32_t NormalizedGuestRamCapacity;
    HV_EFI_IOMMU_RANGE *NormalizedDenyRanges;
    uint32_t NormalizedDenyCapacity;
    HV_EFI_IOMMU_DOMAIN_ASSIGNMENT *Assignments;
    uint32_t AssignmentCapacity;
    HV_EFI_IOMMU_IDENTITY_MAPPING *Mappings;
    uint32_t MappingCapacity;
} HV_EFI_IOMMU_PLAN_STORAGE;

typedef struct HV_EFI_IOMMU_PLAN {
    const HV_EFI_IOMMU_UNIT *Units;
    uint32_t UnitCount;
    const HV_EFI_IOMMU_DEVICE *Devices;
    uint32_t DeviceCount;
    const HV_EFI_IOMMU_RMRR *RmrrRanges;
    uint32_t RmrrRangeCount;
    const HV_EFI_IOMMU_RANGE *GuestRamRanges;
    uint32_t GuestRamRangeCount;
    const HV_EFI_IOMMU_RANGE *HypervisorDenyRanges;
    uint32_t HypervisorDenyRangeCount;
    HV_EFI_IOMMU_DOMAIN_ASSIGNMENT *Assignments;
    uint32_t AssignmentCount;
    HV_EFI_IOMMU_IDENTITY_MAPPING *Mappings;
    uint32_t MappingCount;
    uint32_t AddressBits;
    uint32_t Flags;
} HV_EFI_IOMMU_PLAN;

typedef struct HV_EFI_IOMMU_MAPPING_RESULT {
    uint32_t Mapped;
    uint32_t DomainId;
    uint32_t Flags;
    uint32_t Reserved;
} HV_EFI_IOMMU_MAPPING_RESULT;

HV_EFI_IOMMU_STATUS HvEfiIommuBuildIdentityPlan(
    const HV_EFI_IOMMU_TOPOLOGY *topology,
    const HV_EFI_IOMMU_PLAN_STORAGE *storage,
    HV_EFI_IOMMU_PLAN *plan);

/* Independently audits assignments, RMRRs, coverage, and every deny range. */
HV_EFI_IOMMU_STATUS HvEfiIommuValidateIdentityPlan(
    const HV_EFI_IOMMU_PLAN *plan,
    uint64_t *checkedDenyRanges,
    uint64_t *checkedMappings);

HV_EFI_IOMMU_STATUS HvEfiIommuPlanMappingAt(
    const HV_EFI_IOMMU_PLAN *plan,
    uint32_t deviceIndex,
    uint64_t physicalAddress,
    HV_EFI_IOMMU_MAPPING_RESULT *result);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_EFI_IOMMU_ISOLATION_H */
