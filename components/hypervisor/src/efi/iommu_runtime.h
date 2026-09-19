#ifndef HYPERSINV_EFI_IOMMU_RUNTIME_H
#define HYPERSINV_EFI_IOMMU_RUNTIME_H

/*
 * Bounded ACPI DMAR parser and callback-driven Intel VT-d runtime adapter.
 * No physical address is dereferenced and no MMIO instruction is issued by
 * this module.  Firmware integration supplies one mapped physical-memory
 * view plus explicit MMIO, invalidation, and ownership callbacks.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HV_EFI_IOMMU_RUNTIME_PAGE_SIZE UINT64_C(4096)
#define HV_EFI_IOMMU_RUNTIME_PAGE_MASK (HV_EFI_IOMMU_RUNTIME_PAGE_SIZE - UINT64_C(1))
#define HV_EFI_IOMMU_RUNTIME_MAX_ADDRESS_BITS UINT32_C(52)

#define HV_EFI_IOMMU_DMAR_FLAG_INTERRUPT_REMAPPING UINT32_C(0x01)
#define HV_EFI_IOMMU_DMAR_FLAG_X2APIC_OPT_OUT UINT32_C(0x02)
#define HV_EFI_IOMMU_DMAR_FLAG_DMA_CONTROL_OPT_IN UINT32_C(0x04)
#define HV_EFI_IOMMU_DRHD_INCLUDE_ALL UINT32_C(0x01)

#define HV_EFI_IOMMU_SCOPE_PCI_ENDPOINT UINT32_C(1)
#define HV_EFI_IOMMU_SCOPE_PCI_SUB_HIERARCHY UINT32_C(2)
#define HV_EFI_IOMMU_SCOPE_IOAPIC UINT32_C(3)
#define HV_EFI_IOMMU_SCOPE_MSI_HPET UINT32_C(4)
#define HV_EFI_IOMMU_SCOPE_ACPI_NAMESPACE UINT32_C(5)
#define HV_EFI_IOMMU_SCOPE_OWNER_DRHD UINT32_C(1)
#define HV_EFI_IOMMU_SCOPE_OWNER_RMRR UINT32_C(2)

#define HV_EFI_IOMMU_OWNERSHIP_EPT_TRAPPED UINT32_C(1)
#define HV_EFI_IOMMU_OWNERSHIP_PLATFORM_LOCKED UINT32_C(2)

#define HV_EFI_IOMMU_OPERATION_VERIFY_OWNERSHIP UINT32_C(1)
#define HV_EFI_IOMMU_OPERATION_WRITE32 UINT32_C(2)
#define HV_EFI_IOMMU_OPERATION_WRITE64 UINT32_C(3)
#define HV_EFI_IOMMU_OPERATION_POLL32_SET UINT32_C(4)
#define HV_EFI_IOMMU_OPERATION_INVALIDATE UINT32_C(5)

#define HV_EFI_IOMMU_INVALIDATE_CONTEXT UINT32_C(0x01)
#define HV_EFI_IOMMU_INVALIDATE_IOTLB UINT32_C(0x02)
#define HV_EFI_IOMMU_INVALIDATE_INTERRUPT_ENTRY UINT32_C(0x04)

#define HV_EFI_IOMMU_RUNTIME_RESET UINT32_C(0)
#define HV_EFI_IOMMU_RUNTIME_ADMITTED UINT32_C(1)
#define HV_EFI_IOMMU_RUNTIME_ENABLING UINT32_C(2)
#define HV_EFI_IOMMU_RUNTIME_ENABLED UINT32_C(3)
#define HV_EFI_IOMMU_RUNTIME_FAILED UINT32_C(4)
#define HV_EFI_IOMMU_RUNTIME_ROLLED_BACK UINT32_C(5)

/* DMA ownership modes published to the resident context.  EXTERNAL means
 * that an outer VMM, not this L1 monitor, owns physical DMA isolation. */
#define HV_EFI_DMA_ISOLATION_UNAVAILABLE UINT32_C(0)
#define HV_EFI_DMA_ISOLATION_EXTERNAL_VMM UINT32_C(1)
#define HV_EFI_DMA_ISOLATION_VTD_LOCKED UINT32_C(2)
#define HV_EFI_DMA_ISOLATION_VTD_MEDIATED UINT32_C(3)

#define HV_EFI_IOMMU_MMIO_READ UINT32_C(1)
#define HV_EFI_IOMMU_MMIO_WRITE UINT32_C(2)
#define HV_EFI_IOMMU_MMIO_EMULATE_READ UINT32_C(1)
#define HV_EFI_IOMMU_MMIO_DENY_WRITE UINT32_C(2)
#define HV_EFI_IOMMU_MMIO_FATAL UINT32_C(3)

#define HV_EFI_IOMMU_LEGACY_ROOT_PAGES UINT32_C(1)
#define HV_EFI_IOMMU_LEGACY_CONTEXT_PAGES UINT32_C(256)
#define HV_EFI_IOMMU_SL_PML4_PAGES UINT32_C(1)
#define HV_EFI_IOMMU_SL_PDPT_PAGES UINT32_C(1)
#define HV_EFI_IOMMU_SL_PD_PAGES UINT32_C(512)
#define HV_EFI_IOMMU_LEGACY_BASE_PAGES \
    (HV_EFI_IOMMU_LEGACY_ROOT_PAGES + \
     HV_EFI_IOMMU_LEGACY_CONTEXT_PAGES + \
     HV_EFI_IOMMU_SL_PML4_PAGES + HV_EFI_IOMMU_SL_PDPT_PAGES + \
     HV_EFI_IOMMU_SL_PD_PAGES)

typedef enum HV_EFI_IOMMU_RUNTIME_STATUS {
    HV_EFI_IOMMU_RUNTIME_OK = 0,
    HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT = 1,
    HV_EFI_IOMMU_RUNTIME_OUT_OF_BOUNDS = 2,
    HV_EFI_IOMMU_RUNTIME_BAD_SIGNATURE = 3,
    HV_EFI_IOMMU_RUNTIME_BAD_CHECKSUM = 4,
    HV_EFI_IOMMU_RUNTIME_MALFORMED_TABLE = 5,
    HV_EFI_IOMMU_RUNTIME_DMAR_NOT_FOUND = 6,
    HV_EFI_IOMMU_RUNTIME_UNSUPPORTED_SCOPE = 7,
    HV_EFI_IOMMU_RUNTIME_RANGE_OVERFLOW = 8,
    HV_EFI_IOMMU_RUNTIME_RANGE_OVERLAP = 9,
    HV_EFI_IOMMU_RUNTIME_CAPACITY = 10,
    HV_EFI_IOMMU_RUNTIME_REMAPPING_UNSUPPORTED = 11,
    HV_EFI_IOMMU_RUNTIME_QI_INCOMPATIBLE = 12,
    HV_EFI_IOMMU_RUNTIME_IR_INCOMPATIBLE = 13,
    HV_EFI_IOMMU_RUNTIME_OWNERSHIP_UNAVAILABLE = 14,
    HV_EFI_IOMMU_RUNTIME_ALREADY_ENABLED = 15,
    HV_EFI_IOMMU_RUNTIME_CALLBACK_FAILED = 16,
    HV_EFI_IOMMU_RUNTIME_TIMEOUT = 17,
    HV_EFI_IOMMU_RUNTIME_AUDIT_FAILED = 18,
    HV_EFI_IOMMU_RUNTIME_ROLLBACK_FAILED = 19,
    HV_EFI_IOMMU_RUNTIME_INVALID_STATE = 20,
    HV_EFI_IOMMU_RUNTIME_PROFILE_MISMATCH = 21,
    HV_EFI_IOMMU_RUNTIME_PHYSICAL_MEDIATION_REQUIRED = 22,
    HV_EFI_IOMMU_RUNTIME_CPU_RENDEZVOUS_INCOMPLETE = 23,
    HV_EFI_IOMMU_RUNTIME_DECODE_UNSUPPORTED = 24,
    HV_EFI_IOMMU_RUNTIME_PAGE_WALK_FAILED = 25,
    HV_EFI_IOMMU_RUNTIME_TAKEOVER_BLOCKED = 26
} HV_EFI_IOMMU_RUNTIME_STATUS;

typedef struct HV_EFI_IOMMU_PHYSICAL_VIEW {
    const uint8_t *Bytes;
    uint64_t PhysicalBase;
    uint64_t Size;
    /* Before ExitBootServices x64 UEFI uses the physical address as the
     * dereferenceable address.  Tests and host tools leave this zero. */
    uint32_t IdentityMapped;
    uint32_t Reserved;
} HV_EFI_IOMMU_PHYSICAL_VIEW;

typedef struct HV_EFI_DMA_ISOLATION_EVIDENCE {
    uint32_t OuterHypervisorPresent;
    uint32_t OuterDmaIsolationAttested;
    uint32_t PinnedCpuFamily;
    uint32_t PinnedCpuModel;
    uint32_t PinnedCpuStepping;
    uint32_t EnabledProcessorCount;
    uint32_t DmarParsed;
    uint32_t AllDmaSourcesCovered;
    uint32_t RootTablesExcludePrivatePages;
    uint32_t RemappingTablesGuestDenied;
    uint32_t RemappingTablesDmaDenied;
    uint32_t DmarMmioGuestDenied;
    uint32_t ContinuousAuditEnabled;
    uint32_t DmaQuiesced;
    uint32_t RollbackAvailable;
    uint32_t PlatformLockEngaged;
    uint32_t MmioMediatorComplete;
} HV_EFI_DMA_ISOLATION_EVIDENCE;

typedef struct HV_EFI_IOMMU_PROTECTED_RANGE {
    uint64_t PhysicalBase;
    uint64_t Size;
    uint32_t Kind;
    uint32_t Reserved;
} HV_EFI_IOMMU_PROTECTED_RANGE;

typedef struct HV_EFI_IOMMU_MMIO_DECODE {
    uint32_t Access;
    uint32_t Width;
    uint32_t RegisterIndex;
    uint32_t InstructionLength;
} HV_EFI_IOMMU_MMIO_DECODE;

typedef uint32_t (*HV_EFI_IOMMU_READ_PHYSICAL64)(
    void *context, uint64_t physicalAddress, uint64_t *value);

typedef struct HV_EFI_IOMMU_PAGE_WALK {
    void *Context;
    HV_EFI_IOMMU_READ_PHYSICAL64 ReadPhysical64;
    uint32_t PhysicalAddressBits;
    uint32_t La57Enabled;
} HV_EFI_IOMMU_PAGE_WALK;

typedef struct HV_EFI_IOMMU_REGISTER_SHADOW {
    uint64_t RegisterBase;
    uint64_t Capability;
    uint64_t ExtendedCapability;
    uint64_t RootTablePhysical;
    uint64_t InvalidationQueuePhysical;
    uint64_t InvalidationQueueTail;
    uint64_t InterruptTablePhysical;
    uint32_t Version;
    uint32_t GlobalCommand;
    uint32_t GlobalStatus;
    uint32_t FaultStatus;
    uint32_t OwnershipViolationCount;
    uint32_t Reserved;
} HV_EFI_IOMMU_REGISTER_SHADOW;

typedef struct HV_EFI_IOMMU_ACPI_SHADOW {
    uint64_t GuestRsdpPage;
    uint64_t HostRsdpPage;
    uint64_t GuestXsdtPage;
    uint64_t HostXsdtPage;
    uint32_t XsdtLength;
    uint32_t RemovedDmarEntries;
} HV_EFI_IOMMU_ACPI_SHADOW;

typedef struct HV_EFI_IOMMU_LEGACY_TABLE_RESULT {
    uint64_t RootTablePhysical;
    uint64_t SecondLevelRootPhysical;
    uint32_t PagesUsed;
    uint32_t SplitPageCount;
    uint32_t LargePageCount;
    uint32_t MappedPageCount;
    uint16_t DomainId;
    uint16_t Reserved;
} HV_EFI_IOMMU_LEGACY_TABLE_RESULT;

typedef struct HV_EFI_IOMMU_MCFG_SEGMENT {
    uint64_t EcamBase;
    uint16_t Segment;
    uint8_t StartBus;
    uint8_t EndBus;
} HV_EFI_IOMMU_MCFG_SEGMENT;

typedef struct HV_EFI_IOMMU_PCI_COMMAND_STATE {
    uint64_t CommandAddress;
    uint16_t OriginalCommand;
    uint16_t Segment;
    uint8_t Bus;
    uint8_t Device;
    uint8_t Function;
    uint8_t Restored;
} HV_EFI_IOMMU_PCI_COMMAND_STATE;

typedef uint32_t (*HV_EFI_IOMMU_PCI_READ16)(
    void *context, uint64_t ecamAddress, uint16_t *value);
typedef uint32_t (*HV_EFI_IOMMU_PCI_WRITE16)(
    void *context, uint64_t ecamAddress, uint16_t value);

typedef struct HV_EFI_IOMMU_PCI_CALLBACKS {
    void *Context;
    HV_EFI_IOMMU_PCI_READ16 Read16;
    HV_EFI_IOMMU_PCI_WRITE16 Write16;
} HV_EFI_IOMMU_PCI_CALLBACKS;

#define HV_EFI_IOMMU_PROTECTED_PRIVATE UINT32_C(1)
#define HV_EFI_IOMMU_PROTECTED_MMIO UINT32_C(2)
#define HV_EFI_IOMMU_PROTECTED_REMAP_TABLE UINT32_C(3)
#define HV_EFI_IOMMU_MAPPING_RAM UINT32_C(4)
#define HV_EFI_IOMMU_MAPPING_RMRR UINT32_C(5)

typedef struct HV_EFI_IOMMU_DMAR_UNIT {
    uint64_t RegisterBase;
    uint32_t Segment;
    uint32_t Flags;
    uint32_t FirstScope;
    uint32_t ScopeCount;
} HV_EFI_IOMMU_DMAR_UNIT;

typedef struct HV_EFI_IOMMU_DMAR_RMRR {
    uint64_t PhysicalBase;
    uint64_t Size;
    uint32_t Segment;
    uint32_t FirstScope;
    uint32_t ScopeCount;
    uint32_t Reserved;
} HV_EFI_IOMMU_DMAR_RMRR;

typedef struct HV_EFI_IOMMU_DEVICE_PATH_ELEMENT {
    uint8_t Device;
    uint8_t Function;
    uint16_t Reserved;
} HV_EFI_IOMMU_DEVICE_PATH_ELEMENT;

typedef struct HV_EFI_IOMMU_DMAR_SCOPE {
    uint32_t OwnerKind;
    uint32_t OwnerIndex;
    uint32_t Type;
    uint32_t Segment;
    uint32_t EnumerationId;
    uint32_t StartBus;
    uint32_t FirstPathElement;
    uint32_t PathElementCount;
} HV_EFI_IOMMU_DMAR_SCOPE;

typedef struct HV_EFI_IOMMU_DMAR_STORAGE {
    HV_EFI_IOMMU_DMAR_UNIT *Units;
    uint32_t UnitCapacity;
    HV_EFI_IOMMU_DMAR_RMRR *Rmrrs;
    uint32_t RmrrCapacity;
    HV_EFI_IOMMU_DMAR_SCOPE *Scopes;
    uint32_t ScopeCapacity;
    HV_EFI_IOMMU_DEVICE_PATH_ELEMENT *PathElements;
    uint32_t PathElementCapacity;
} HV_EFI_IOMMU_DMAR_STORAGE;

typedef struct HV_EFI_IOMMU_DMAR_INFO {
    uint64_t RsdpPhysical;
    uint64_t XsdtPhysical;
    uint64_t DmarPhysical;
    uint32_t HostAddressBits;
    uint32_t Flags;
    HV_EFI_IOMMU_DMAR_UNIT *Units;
    uint32_t UnitCount;
    HV_EFI_IOMMU_DMAR_RMRR *Rmrrs;
    uint32_t RmrrCount;
    HV_EFI_IOMMU_DMAR_SCOPE *Scopes;
    uint32_t ScopeCount;
    HV_EFI_IOMMU_DEVICE_PATH_ELEMENT *PathElements;
    uint32_t PathElementCount;
} HV_EFI_IOMMU_DMAR_INFO;

typedef struct HV_EFI_IOMMU_OWNERSHIP_POLICY {
    uint32_t Mode;
    uint32_t DmarMmioGuestDenied;
    uint32_t RemappingTablesGuestDenied;
    uint32_t RemappingTablesDmaDenied;
    uint32_t ContinuousAuditEnabled;
    uint32_t PlatformLockSupported;
    uint32_t PlatformLockEngaged;
    uint32_t RequireDmaQuiesce;
    uint64_t OwnerToken;
} HV_EFI_IOMMU_OWNERSHIP_POLICY;

typedef struct HV_EFI_IOMMU_UNIT_CONFIGURATION {
    uint64_t RootTablePhysical;
    uint64_t InvalidationQueuePhysical;
    uint64_t InterruptTablePhysical;
    uint32_t InterruptEntryCount;
    uint32_t AddressBits;
    uint32_t EnableQueuedInvalidation;
    uint32_t EnableInterruptRemapping;
    uint32_t EnableExtendedInterruptMode;
    uint32_t Reserved;
} HV_EFI_IOMMU_UNIT_CONFIGURATION;

typedef struct HV_EFI_IOMMU_MMIO_OPERATION {
    uint64_t Address;
    uint64_t Value;
    uint64_t Mask;
    uint32_t UnitIndex;
    uint32_t Kind;
    uint32_t Width;
    uint32_t InvalidationFlags;
} HV_EFI_IOMMU_MMIO_OPERATION;

typedef struct HV_EFI_IOMMU_UNIT_PROGRAM {
    uint64_t RegisterBase;
    uint64_t Capability;
    uint64_t ExtendedCapability;
    uint64_t OriginalRootTablePhysical;
    uint64_t RootTablePhysical;
    uint64_t InvalidationQueuePhysical;
    uint64_t InterruptTablePhysical;
    uint32_t InterruptEntryCount;
    uint32_t AddressBits;
    uint32_t FirstOperation;
    uint32_t OperationCount;
    uint32_t RequiredStatusBits;
    uint32_t Reserved;
} HV_EFI_IOMMU_UNIT_PROGRAM;

typedef struct HV_EFI_IOMMU_PROGRAM_STORAGE {
    HV_EFI_IOMMU_UNIT_PROGRAM *Units;
    uint32_t UnitCapacity;
    HV_EFI_IOMMU_MMIO_OPERATION *Operations;
    uint32_t OperationCapacity;
} HV_EFI_IOMMU_PROGRAM_STORAGE;

typedef uint32_t (*HV_EFI_IOMMU_MMIO_READ_CALLBACK)(
    void *context, uint64_t address, uint32_t width, uint64_t *value);
typedef uint32_t (*HV_EFI_IOMMU_MMIO_WRITE_CALLBACK)(
    void *context, uint64_t address, uint32_t width, uint64_t value);
typedef uint32_t (*HV_EFI_IOMMU_INVALIDATE_CALLBACK)(
    void *context, uint32_t unitIndex, uint32_t invalidationFlags);
typedef uint32_t (*HV_EFI_IOMMU_OWNERSHIP_CALLBACK)(
    void *context, uint64_t ownerToken);
typedef uint32_t (*HV_EFI_IOMMU_DMA_QUIESCED_CALLBACK)(void *context);

typedef struct HV_EFI_IOMMU_RUNTIME_CALLBACKS {
    void *Context;
    HV_EFI_IOMMU_MMIO_READ_CALLBACK Read;
    HV_EFI_IOMMU_MMIO_WRITE_CALLBACK Write;
    HV_EFI_IOMMU_INVALIDATE_CALLBACK Invalidate;
    HV_EFI_IOMMU_OWNERSHIP_CALLBACK OwnershipHeld;
    HV_EFI_IOMMU_DMA_QUIESCED_CALLBACK DmaQuiesced;
} HV_EFI_IOMMU_RUNTIME_CALLBACKS;

typedef struct HV_EFI_IOMMU_PROGRAM_PLAN {
    HV_EFI_IOMMU_UNIT_PROGRAM *Units;
    uint32_t UnitCount;
    HV_EFI_IOMMU_MMIO_OPERATION *Operations;
    uint32_t OperationCount;
    HV_EFI_IOMMU_OWNERSHIP_POLICY Ownership;
    uint32_t PollLimit;
    uint32_t Reserved;
} HV_EFI_IOMMU_PROGRAM_PLAN;

typedef struct HV_EFI_IOMMU_UNIT_RUNTIME_STATE {
    uint32_t CompletedOperations;
    uint32_t Enabled;
    uint32_t RollbackComplete;
    uint32_t Reserved;
} HV_EFI_IOMMU_UNIT_RUNTIME_STATE;

typedef struct HV_EFI_IOMMU_RUNTIME {
    const HV_EFI_IOMMU_PROGRAM_PLAN *Plan;
    HV_EFI_IOMMU_UNIT_RUNTIME_STATE *UnitStates;
    uint32_t UnitStateCount;
    uint32_t State;
    uint32_t EnabledUnitCount;
    uint32_t FailedUnitIndex;
    uint32_t Reserved;
} HV_EFI_IOMMU_RUNTIME;

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuParseDmar(
    const HV_EFI_IOMMU_PHYSICAL_VIEW *view,
    uint64_t rsdpPhysical,
    const HV_EFI_IOMMU_DMAR_STORAGE *storage,
    HV_EFI_IOMMU_DMAR_INFO *info);

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuBuildProgrammingPlan(
    const HV_EFI_IOMMU_DMAR_INFO *dmar,
    const HV_EFI_IOMMU_UNIT_CONFIGURATION *configurations,
    uint32_t configurationCount,
    const HV_EFI_IOMMU_OWNERSHIP_POLICY *ownership,
    const HV_EFI_IOMMU_RUNTIME_CALLBACKS *callbacks,
    const HV_EFI_IOMMU_PROGRAM_STORAGE *storage,
    uint32_t pollLimit,
    HV_EFI_IOMMU_PROGRAM_PLAN *plan);

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuRuntimeInitialize(
    HV_EFI_IOMMU_RUNTIME *runtime,
    const HV_EFI_IOMMU_PROGRAM_PLAN *plan,
    HV_EFI_IOMMU_UNIT_RUNTIME_STATE *unitStates,
    uint32_t unitStateCount);

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuRuntimeEnable(
    HV_EFI_IOMMU_RUNTIME *runtime,
    const HV_EFI_IOMMU_RUNTIME_CALLBACKS *callbacks);

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuRuntimeAudit(
    HV_EFI_IOMMU_RUNTIME *runtime,
    const HV_EFI_IOMMU_RUNTIME_CALLBACKS *callbacks);

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuRuntimeRollback(
    HV_EFI_IOMMU_RUNTIME *runtime,
    const HV_EFI_IOMMU_RUNTIME_CALLBACKS *callbacks);

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuEvaluateDmaIsolation(
    const HV_EFI_DMA_ISOLATION_EVIDENCE *evidence,
    uint32_t *mode);

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuBuildProtectedInventory(
    const HV_EFI_IOMMU_DMAR_INFO *dmar,
    const HV_EFI_IOMMU_UNIT_CONFIGURATION *configurations,
    uint32_t configurationCount,
    const HV_EFI_IOMMU_PROTECTED_RANGE *privateRanges,
    uint32_t privateRangeCount,
    HV_EFI_IOMMU_PROTECTED_RANGE *output,
    uint32_t outputCapacity,
    uint32_t *outputCount);

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuClassifyMmioAccess(
    const HV_EFI_IOMMU_DMAR_INFO *dmar,
    uint64_t physicalAddress,
    uint32_t width,
    uint32_t access,
    uint32_t *action,
    uint32_t *unitIndex);

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuValidateDetachRendezvous(
    uint32_t enabledProcessorCount,
    uint64_t preparedMask,
    uint64_t acknowledgedMask);

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuDecodeMmioInstruction(
    const uint8_t *instruction,
    uint32_t availableBytes,
    HV_EFI_IOMMU_MMIO_DECODE *decoded);

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuWalkGuestLinear(
    const HV_EFI_IOMMU_PAGE_WALK *walk,
    uint64_t guestCr3,
    uint64_t guestLinear,
    uint32_t accessSize,
    uint64_t *guestPhysical);

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuShadowAccess(
    HV_EFI_IOMMU_REGISTER_SHADOW *shadow,
    uint64_t physicalAddress,
    uint32_t width,
    uint32_t access,
    uint64_t *value);

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuBuildGuestAcpiShadow(
    const HV_EFI_IOMMU_PHYSICAL_VIEW *view,
    const HV_EFI_IOMMU_DMAR_INFO *dmar,
    uint64_t hostRsdpPage,
    uint8_t *rsdpPage,
    uint64_t hostXsdtPage,
    uint8_t *xsdtPage,
    HV_EFI_IOMMU_ACPI_SHADOW *shadow);

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuInstallEptReadOnlyRemap(
    uint64_t *eptStorage,
    uint32_t eptStoragePages,
    uint64_t eptRootPhysical,
    uint64_t guestPhysicalPage,
    uint64_t hostPhysicalPage);

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuBuildLegacyIdentityDomain(
    uint64_t *storage,
    uint32_t storagePages,
    uint64_t storagePhysical,
    const HV_EFI_IOMMU_PROTECTED_RANGE *mappedRanges,
    uint32_t mappedRangeCount,
    const HV_EFI_IOMMU_PROTECTED_RANGE *denyRanges,
    uint32_t denyRangeCount,
    uint16_t domainId,
    HV_EFI_IOMMU_LEGACY_TABLE_RESULT *result);

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuParseMcfg(
    const HV_EFI_IOMMU_PHYSICAL_VIEW *view,
    const HV_EFI_IOMMU_DMAR_INFO *dmar,
    HV_EFI_IOMMU_MCFG_SEGMENT *segments,
    uint32_t segmentCapacity,
    uint32_t *segmentCount);

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuValidatePciCoverage(
    const HV_EFI_IOMMU_DMAR_INFO *dmar,
    const HV_EFI_IOMMU_MCFG_SEGMENT *segments,
    uint32_t segmentCount);

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuQuiescePciBusMasters(
    const HV_EFI_IOMMU_MCFG_SEGMENT *segments,
    uint32_t segmentCount,
    const HV_EFI_IOMMU_PCI_CALLBACKS *callbacks,
    HV_EFI_IOMMU_PCI_COMMAND_STATE *states,
    uint32_t stateCapacity,
    uint32_t *stateCount);

HV_EFI_IOMMU_RUNTIME_STATUS HvEfiIommuRestorePciBusMasters(
    const HV_EFI_IOMMU_PCI_CALLBACKS *callbacks,
    HV_EFI_IOMMU_PCI_COMMAND_STATE *states,
    uint32_t stateCount);

/* Shared physical/VM backend transaction.  The caller supplies only injected
 * MMIO and ECAM accessors; the fail-closed quiesce/program/audit/restore
 * ordering is identical in firmware and nested-IOMMU tests. */
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
    volatile uint32_t *dmaQuiescedState);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_EFI_IOMMU_RUNTIME_H */
