#include "Uefi.h"
#include "efi_hypervisor.h"
#include "efi_hypervisor_math.h"
#include "efi_mp_services.h"
#include "iommu_runtime.h"
#include "runtime_analysis.h"
#include "runtime_process.h"
#include "runtime_scanner.h"
#include "runtime_shadow.h"
#include "scanner.h"
#include "shadow_ept.h"
#include "gpu_native.h"
#include "gpu_boot.h"
#include "efi_boot_diagnostic.h"
#include <intrin.h>
#pragma intrinsic(__wbinvd)
#if defined(HYPERSINV_GPU_VM_SELFTEST)
#include "gpu_vm_wc.h"
#endif

#define HV_EFI_MEMORY_MAP_SLACK_DESCRIPTORS ((UINTN)8)
#define HV_EFI_MEMORY_MAP_MAX_ATTEMPTS UINT32_C(4)
#define HV_EFI_PHYSICAL_IOMMU_TABLE_PAGES UINT32_C(2048)
#define HV_EFI_PHYSICAL_IOMMU_MAX_UNITS HV_EFI_EPT_MAX_MMIO_WINDOWS
#define HV_EFI_PHYSICAL_IOMMU_MAX_PCI_STATES UINT32_C(512)
#define HV_EFI_PHYSICAL_IOMMU_MAX_OPERATIONS UINT32_C(192)
#define HV_EFI_PHYSICAL_IOMMU_SIGNATURE UINT64_C(0x56544452554e3031)
#define HV_EFI_PHYSICAL_IOMMU_POLL_LIMIT UINT32_C(1000000)
#define HV_EFI_PHYSICAL_IOMMU_MMIO_PAGES UINT32_C(4)

#define HV_EFI_VTD_CAP UINT64_C(0x008)
#define HV_EFI_VTD_ECAP UINT64_C(0x010)
#define HV_EFI_VTD_CCMD UINT64_C(0x028)
#define HV_EFI_VTD_CCMD_ICC (UINT64_C(1) << 63)
#define HV_EFI_VTD_CCMD_GLOBAL (UINT64_C(1) << 61)
#define HV_EFI_VTD_CCMD_ACTUAL_GLOBAL (UINT64_C(1) << 59)
#define HV_EFI_VTD_ECAP_IRO_SHIFT UINT32_C(8)
#define HV_EFI_VTD_ECAP_IRO_MASK UINT64_C(0x3ff)
#define HV_EFI_VTD_IOTLB_IVT (UINT64_C(1) << 63)
#define HV_EFI_VTD_IOTLB_GLOBAL (UINT64_C(1) << 60)
#define HV_EFI_VTD_IOTLB_ACTUAL_GLOBAL (UINT64_C(1) << 57)
#define HV_EFI_VTD_IOTLB_DRAIN_READS (UINT64_C(1) << 49)
#define HV_EFI_VTD_IOTLB_DRAIN_WRITES (UINT64_C(1) << 48)

/* Uefi.h declares these globals for the shared freestanding support layer. */
EFI_SYSTEM_TABLE *gST = NULL;
EFI_BOOT_SERVICES *gBS = NULL;

/* The loader owns this record and exposes it only for the synchronous child
 * entry call. Only the BSP preboot path accesses it. */
static hv_boot_diagnostic_t *gHvEfiBootDiagnostic = NULL;
static uint32_t gHvEfiBootDiagnosticStage = HV_BOOT_STAGE_ENTRY;
static uint32_t gHvEfiBootDiagnosticReason = HV_BOOT_REASON_NONE;
static uint32_t gHvEfiBootDiagnosticDetail =
    HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE;

static void HvEfiBootDiagnosticReset(void)
{
    gHvEfiBootDiagnostic = NULL;
    gHvEfiBootDiagnosticStage = HV_BOOT_STAGE_ENTRY;
    gHvEfiBootDiagnosticReason = HV_BOOT_REASON_NONE;
    gHvEfiBootDiagnosticDetail = HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE;
}

static void HvEfiBootDiagnosticAttach(EFI_HANDLE imageHandle)
{
    EFI_GUID loadedImageGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *loadedImage = NULL;
    hv_boot_diagnostic_t *record;

    if (imageHandle == NULL || gBS == NULL || gBS->HandleProtocol == NULL ||
        EFI_ERROR(gBS->HandleProtocol(imageHandle, &loadedImageGuid,
                                      (VOID **)&loadedImage)) ||
        loadedImage == NULL || loadedImage->LoadOptions == NULL ||
        loadedImage->LoadOptionsSize != sizeof(hv_boot_diagnostic_t)) {
        return;
    }

    record = (hv_boot_diagnostic_t *)loadedImage->LoadOptions;
    if (hv_efi_boot_diagnostic_mark_entry(
            record, (uint32_t)loadedImage->LoadOptionsSize)) {
        gHvEfiBootDiagnostic = record;
    }
}

static void HvEfiBootDiagnosticSet(uint32_t stage, uint32_t reason,
                                   uint32_t detail)
{
    gHvEfiBootDiagnosticStage = stage;
    gHvEfiBootDiagnosticReason = reason;
    gHvEfiBootDiagnosticDetail = detail;
    if (gHvEfiBootDiagnostic != NULL &&
        !hv_efi_boot_diagnostic_set_stage(
            gHvEfiBootDiagnostic,
            (uint32_t)sizeof(*gHvEfiBootDiagnostic), stage, reason, detail)) {
        gHvEfiBootDiagnostic = NULL;
    }
}

static void HvEfiBootDiagnosticCapture(
    hv_efi_boot_diagnostic_snapshot_t *snapshot,
    const HV_EFI_CONTEXT *context)
{
    snapshot->Processor = HV_BOOT_DIAGNOSTIC_NO_PROCESSOR;
    snapshot->Detail = gHvEfiBootDiagnosticDetail;
    snapshot->ExpectedCpus = 0;
    snapshot->PreparedCpus = 0;
    snapshot->VmxReadyCpus = 0;
    snapshot->ResourceStatus = 0;
    snapshot->PreparationStatus = 0;
    snapshot->PreparationFailure = 0;
    snapshot->ActivationStatus = 0;
    snapshot->ActivationFailure = 0;
    if (context == NULL) return;

    if (context->CurrentProcessorNumber < context->ProcessorCount) {
        snapshot->Processor = context->CurrentProcessorNumber;
    }
    snapshot->ExpectedCpus = context->EnabledProcessorCount;
    snapshot->PreparedCpus = context->PreparedProcessorCount;
    snapshot->VmxReadyCpus = context->VmxReadyProcessorCount;
    snapshot->ResourceStatus = context->ProcessorResourceStatus;
    snapshot->PreparationStatus = context->ProcessorPreparationStatus;
    snapshot->PreparationFailure = context->ProcessorPreparationFailure;
    snapshot->ActivationStatus = context->ApActivationStatus;
    snapshot->ActivationFailure = context->ApActivationFailure;
}

static EFI_STATUS HvEfiBootDiagnosticReturnWithSnapshot(
    EFI_STATUS status, const hv_efi_boot_diagnostic_snapshot_t *snapshot)
{
    uint32_t stage = gHvEfiBootDiagnosticStage;
    uint32_t reason = gHvEfiBootDiagnosticReason;
    if (status == EFI_SUCCESS) {
        stage = HV_BOOT_STAGE_RETURN;
        reason = HV_BOOT_REASON_NONE;
    } else if (reason == HV_BOOT_REASON_NONE) {
        reason = HV_BOOT_REASON_OPERATION_FAILED;
    }

    if (gHvEfiBootDiagnostic != NULL) {
        (void)hv_efi_boot_diagnostic_record_return(
            gHvEfiBootDiagnostic,
            (uint32_t)sizeof(*gHvEfiBootDiagnostic), stage, reason,
            (uint64_t)status, snapshot);
    }
    gHvEfiBootDiagnostic = NULL;
    return status;
}

static EFI_STATUS HvEfiBootDiagnosticReturn(
    EFI_STATUS status, const HV_EFI_CONTEXT *context)
{
    hv_efi_boot_diagnostic_snapshot_t snapshot;
    HvEfiBootDiagnosticCapture(&snapshot, context);
    return HvEfiBootDiagnosticReturnWithSnapshot(status, &snapshot);
}

static void HvEfiBootDiagnosticSetDetail(uint32_t detail)
{
    HvEfiBootDiagnosticSet(gHvEfiBootDiagnosticStage,
                           gHvEfiBootDiagnosticReason, detail);
}

static void HvEfiBootDiagnosticSetIommu(
    uint32_t substep, uint32_t installStage,
    uint32_t primaryStatus, uint32_t finalStatus)
{
    HvEfiBootDiagnosticSetDetail(hv_efi_boot_diagnostic_iommu_detail(
        substep, installStage, primaryStatus, finalStatus));
}

static void HvEfiBootDiagnosticSetIommuStep(uint32_t substep)
{
    HvEfiBootDiagnosticSetIommu(
        substep, 0, HV_EFI_BOOT_DIAGNOSTIC_IOMMU_NOT_APPLICABLE,
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_NOT_APPLICABLE);
}

typedef struct HV_EFI_PHYSICAL_IOMMU_STATE {
    uint64_t Signature;
    uint64_t OwnerToken;
    uint32_t Enabled;
    uint32_t DmaQuiesced;
    volatile uint32_t DetachRemaining;
    uint32_t ReservedDetach;
    HV_EFI_IOMMU_DMAR_INFO Dmar;
    HV_EFI_IOMMU_DMAR_UNIT Units[8];
    HV_EFI_IOMMU_DMAR_RMRR Rmrrs[32];
    HV_EFI_IOMMU_DMAR_SCOPE Scopes[128];
    HV_EFI_IOMMU_DEVICE_PATH_ELEMENT Paths[256];
    HV_EFI_IOMMU_MCFG_SEGMENT Mcfg[8];
    uint32_t McfgCount;
    uint32_t PciStateCount;
    HV_EFI_IOMMU_PCI_COMMAND_STATE PciStates[
        HV_EFI_PHYSICAL_IOMMU_MAX_PCI_STATES];
    HV_EFI_IOMMU_UNIT_CONFIGURATION Configurations[
        HV_EFI_PHYSICAL_IOMMU_MAX_UNITS];
    HV_EFI_IOMMU_UNIT_PROGRAM Programs[HV_EFI_PHYSICAL_IOMMU_MAX_UNITS];
    HV_EFI_IOMMU_MMIO_OPERATION Operations[
        HV_EFI_PHYSICAL_IOMMU_MAX_OPERATIONS];
    HV_EFI_IOMMU_UNIT_RUNTIME_STATE UnitStates[
        HV_EFI_PHYSICAL_IOMMU_MAX_UNITS];
    HV_EFI_IOMMU_PROGRAM_PLAN Plan;
    HV_EFI_IOMMU_RUNTIME Runtime;
    HV_EFI_IOMMU_ACPI_SHADOW AcpiShadow;
    HV_EFI_IOMMU_LEGACY_TABLE_RESULT Tables;
} HV_EFI_PHYSICAL_IOMMU_STATE;

static EFI_STATUS HvEfiAllocateRuntime(EFI_MEMORY_TYPE type, UINTN pages,
                                       EFI_PHYSICAL_ADDRESS *address);
static void HvEfiFreeRuntime(EFI_PHYSICAL_ADDRESS address, UINTN pages);

extern int HvEfiVmxOn(const uint64_t *physicalAddress);
extern void HvEfiVmxOff(void);
extern int HvEfiVmClear(const uint64_t *physicalAddress);
extern int HvEfiVmPtrld(const uint64_t *physicalAddress);
extern int HvEfiVmwrite(uint64_t field, uint64_t value);
extern int HvEfiVmxLaunch(HV_EFI_CONTEXT *context);
extern int HvEfiVmxLaunchAp(HV_EFI_CONTEXT *context);
extern void HvEfiCaptureCpu(HV_EFI_CONTEXT *context);
extern uint64_t HvEfiReadMsr(uint32_t msr);
extern uint64_t HvEfiReadCr0(void);
extern uint64_t HvEfiReadCr3(void);
extern uint64_t HvEfiReadCr4(void);
extern void HvEfiWriteCr0(uint64_t value);
extern void HvEfiWriteCr4(uint64_t value);
extern void HvEfiCpuid(uint32_t leaf, uint32_t subleaf, uint32_t *out);
extern int HvEfiReadRandom64(uint64_t *out);
extern void HvEfiDebugOutByte(uint16_t port, uint8_t value);
extern void HvEfiFailStop(void);

extern unsigned char HvEfiResidentVmExitStart[];
extern unsigned char HvEfiResidentVmExit[];
extern unsigned char HvEfiGuestBootstrap[];
extern unsigned char HvEfiGuestReturn[];
extern unsigned char HvEfiGuestTransitionStart[];
extern unsigned char HvEfiGuestTransitionEnd[];
extern unsigned char HvEfiResidentFatalInterrupt[];
extern unsigned char HvEfiResidentRootNmi[];
extern unsigned char HvEfiResidentRootUd[];
extern unsigned char HvEfiResidentRootGp[];
extern unsigned char HvEfiResidentRootPf[];
extern unsigned char HvEfiResidentRootMc[];
extern unsigned char HvEfiResidentGuestInterruptGuard[];
extern unsigned char HvEfiResidentGuestInterruptVectorStubs[];
extern unsigned char HvEfiResidentCodeEnd[];

#pragma pack(push, 1)
typedef struct HV_EFI_TSS64 {
    uint32_t Reserved0;
    uint64_t Rsp0;
    uint64_t Rsp1;
    uint64_t Rsp2;
    uint64_t Reserved1;
    uint64_t Ist1;
    uint64_t Ist2;
    uint64_t Ist3;
    uint64_t Ist4;
    uint64_t Ist5;
    uint64_t Ist6;
    uint64_t Ist7;
    uint64_t Reserved2;
    uint16_t Reserved3;
    uint16_t IoMapBase;
} HV_EFI_TSS64;
#pragma pack(pop)

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(HV_EFI_TSS64) == HV_EFI_TSS64_MIN_SIZE,
               "64-bit TSS layout drift");
_Static_assert(sizeof(EFI_MEMORY_DESCRIPTOR) ==
                   sizeof(HV_EFI_MEMORY_DESCRIPTOR_VIEW),
               "UEFI memory descriptor size drift");
_Static_assert(offsetof(EFI_MEMORY_DESCRIPTOR, PhysicalStart) ==
                   offsetof(HV_EFI_MEMORY_DESCRIPTOR_VIEW, PhysicalStart) &&
               offsetof(EFI_MEMORY_DESCRIPTOR, Attribute) ==
                   offsetof(HV_EFI_MEMORY_DESCRIPTOR_VIEW, Attribute),
               "UEFI memory descriptor field drift");
_Static_assert(offsetof(HV_EFI_PHYSICAL_IOMMU_STATE, Dmar) == 32 &&
               offsetof(HV_EFI_PHYSICAL_IOMMU_STATE, PciStateCount) == 6596 &&
               offsetof(HV_EFI_PHYSICAL_IOMMU_STATE, PciStates) == 6600 &&
               offsetof(HV_EFI_PHYSICAL_IOMMU_STATE, Programs) == 15176,
               "resident VT-d teardown layout drift");
#endif

static void HvEfiZero(void *buffer, UINTN size)
{
    UINT8 *bytes = (UINT8 *)buffer;
    while (size-- != 0) {
        *bytes++ = 0;
    }
}

static int HvEfiInitializeRuntimeToken(HV_EFI_CONTEXT *context)
{
    uint32_t word;
    uint32_t attempt;
    uint64_t aggregate = 0;

    if (context == NULL) {
        return 0;
    }
    for (word = 0; word < 4; ++word) {
        for (attempt = 0; attempt < 16; ++attempt) {
            if (HvEfiReadRandom64(&context->RuntimeToken[word]) == 0) {
                break;
            }
        }
        if (attempt == 16) {
            HvEfiZero(context->RuntimeToken, sizeof(context->RuntimeToken));
            return 0;
        }
        aggregate |= context->RuntimeToken[word];
    }
    if (aggregate == 0) {
        HvEfiZero(context->RuntimeToken, sizeof(context->RuntimeToken));
        return 0;
    }
    context->RuntimeGeneration = 1;
    context->RuntimeCapabilities = HV_EFI_RUNTIME_CAPABILITIES;
    context->RuntimeState = HV_EFI_RUNTIME_STATE_ACTIVE;
    return 1;
}

static void HvEfiCopy(void *destination, const void *source, UINTN size)
{
    UINT8 *d = (UINT8 *)destination;
    const UINT8 *s = (const UINT8 *)source;
    while (size-- != 0) {
        *d++ = *s++;
    }
}

static int HvEfiGuidEqual(const EFI_GUID *left, const EFI_GUID *right)
{
    const uint8_t *leftBytes = (const uint8_t *)(const void *)left;
    const uint8_t *rightBytes = (const uint8_t *)(const void *)right;
    UINTN index;
    if (left == NULL || right == NULL) {
        return 0;
    }
    for (index = 0; index < sizeof(EFI_GUID); ++index) {
        if (leftBytes[index] != rightBytes[index]) {
            return 0;
        }
    }
    return 1;
}

static uint64_t HvEfiLocateAcpiRsdp(const EFI_SYSTEM_TABLE *systemTable)
{
    static const EFI_GUID acpi20Guid = {
        UINT32_C(0x8868e871), UINT16_C(0xe4f1), UINT16_C(0x11d3),
        { UINT8_C(0xbc), UINT8_C(0x22), UINT8_C(0x00), UINT8_C(0x80),
          UINT8_C(0xc7), UINT8_C(0x3c), UINT8_C(0x88), UINT8_C(0x81) }
    };
    static const EFI_GUID acpi10Guid = {
        UINT32_C(0xeb9d2d30), UINT16_C(0x2d88), UINT16_C(0x11d3),
        { UINT8_C(0x9a), UINT8_C(0x16), UINT8_C(0x00), UINT8_C(0x90),
          UINT8_C(0x27), UINT8_C(0x3f), UINT8_C(0xc1), UINT8_C(0x4d) }
    };
    uint64_t acpi10 = 0;
    UINTN index;
    if (systemTable == NULL || systemTable->ConfigurationTable == NULL ||
        systemTable->NumberOfTableEntries > (UINTN)1024) {
        return 0;
    }
    for (index = 0; index < systemTable->NumberOfTableEntries; ++index) {
        const EFI_CONFIGURATION_TABLE *entry =
            &systemTable->ConfigurationTable[index];
        if (HvEfiGuidEqual(&entry->VendorGuid, &acpi20Guid)) {
            return (uint64_t)(uintptr_t)entry->VendorTable;
        }
        if (HvEfiGuidEqual(&entry->VendorGuid, &acpi10Guid)) {
            acpi10 = (uint64_t)(uintptr_t)entry->VendorTable;
        }
    }
    return acpi10;
}

static EFI_STATUS HvEfiAdmitDmaIsolation(
    HV_EFI_CONTEXT *context,
    const EFI_SYSTEM_TABLE *systemTable,
    uint32_t *admissionState)
{
    HV_EFI_DMA_ISOLATION_EVIDENCE evidence;
    uint32_t cpuid[4];
    uint32_t mode = HV_EFI_DMA_ISOLATION_UNAVAILABLE;
    uint32_t family;
    uint32_t model;
    uint32_t stepping;

    if (context == NULL || systemTable == NULL || admissionState == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *admissionState = HV_EFI_IOMMU_RUNTIME_INVALID_STATE;
    HvEfiZero(&evidence, sizeof(evidence));
    HvEfiCpuid(1, 0, cpuid);
    evidence.OuterHypervisorPresent = (cpuid[2] >> 31) & 1u;
    evidence.EnabledProcessorCount = context->EnabledProcessorCount;
    evidence.RootTablesExcludePrivatePages = 1;
    if (evidence.OuterHypervisorPresent != 0) {
        /* A VM cannot program the machine's VT-d units.  Its DMA-capable
         * virtual devices are constrained by the L0 guest-physical map; the
         * L1 EPT deny inventory independently protects resident pages. */
        evidence.OuterDmaIsolationAttested = 1;
        *admissionState = (uint32_t)HvEfiIommuEvaluateDmaIsolation(
            &evidence, &mode);
        context->DmaIsolationMode = mode;
        context->DmaIsolationState = *admissionState;
        return *admissionState == HV_EFI_IOMMU_RUNTIME_OK ?
            EFI_SUCCESS : EFI_SECURITY_VIOLATION;
    }

    stepping = cpuid[0] & UINT32_C(0xf);
    family = (cpuid[0] >> 8) & UINT32_C(0xf);
    model = (cpuid[0] >> 4) & UINT32_C(0xf);
    if (family == UINT32_C(0xf)) {
        family += (cpuid[0] >> 20) & UINT32_C(0xff);
    }
    if (family == UINT32_C(6) || family == UINT32_C(0xf)) {
        model |= ((cpuid[0] >> 16) & UINT32_C(0xf)) << 4;
    }
    if (family != 6 || model != 151 || stepping != 2 ||
        context->EnabledProcessorCount != 20) {
        *admissionState = HV_EFI_IOMMU_RUNTIME_PROFILE_MISMATCH;
        return EFI_UNSUPPORTED;
    }

    {
        const uint64_t acpiRsdpPhysical = HvEfiLocateAcpiRsdp(systemTable);
        if (acpiRsdpPhysical != 0) {
            EFI_PHYSICAL_ADDRESS statePhysical = 0;
            EFI_PHYSICAL_ADDRESS tablePhysical = 0;
            EFI_PHYSICAL_ADDRESS shadowPhysical = 0;
            const UINTN statePages = (UINTN)(
                (sizeof(HV_EFI_PHYSICAL_IOMMU_STATE) +
                 HV_EFI_PAGE_SIZE - 1) / HV_EFI_PAGE_SIZE);
            HV_EFI_PHYSICAL_IOMMU_STATE *physicalState;
            HV_EFI_IOMMU_DMAR_STORAGE storage;
            HV_EFI_IOMMU_PHYSICAL_VIEW view;
            HV_EFI_IOMMU_RUNTIME_STATUS isolationStatus;
            EFI_STATUS allocationStatus;
            allocationStatus = HvEfiAllocateRuntime(
                EfiRuntimeServicesData, statePages, &statePhysical);
            if (!EFI_ERROR(allocationStatus)) {
                allocationStatus = HvEfiAllocateRuntime(
                    EfiRuntimeServicesData,
                    HV_EFI_PHYSICAL_IOMMU_TABLE_PAGES, &tablePhysical);
            }
            if (!EFI_ERROR(allocationStatus)) {
                allocationStatus = HvEfiAllocateRuntime(
                    EfiRuntimeServicesData, 2, &shadowPhysical);
            }
            if (EFI_ERROR(allocationStatus)) {
                HvEfiFreeRuntime(shadowPhysical, 2);
                HvEfiFreeRuntime(tablePhysical,
                                 HV_EFI_PHYSICAL_IOMMU_TABLE_PAGES);
                HvEfiFreeRuntime(statePhysical, statePages);
                *admissionState = HV_EFI_IOMMU_RUNTIME_CAPACITY;
                return allocationStatus;
            }
            physicalState = (HV_EFI_PHYSICAL_IOMMU_STATE *)(uintptr_t)
                statePhysical;
            HvEfiZero(physicalState, statePages * HV_EFI_PAGE_SIZE);
            HvEfiZero((void *)(uintptr_t)tablePhysical,
                (UINTN)HV_EFI_PHYSICAL_IOMMU_TABLE_PAGES * HV_EFI_PAGE_SIZE);
            HvEfiZero((void *)(uintptr_t)shadowPhysical,
                      2 * HV_EFI_PAGE_SIZE);
            view.Bytes = NULL;
            view.PhysicalBase = 0;
            view.Size = UINT64_MAX;
            view.IdentityMapped = 1;
            view.Reserved = 0;
            storage.Units = physicalState->Units;
            storage.UnitCapacity = 8;
            storage.Rmrrs = physicalState->Rmrrs;
            storage.RmrrCapacity = 32;
            storage.Scopes = physicalState->Scopes;
            storage.ScopeCapacity = 128;
            storage.PathElements = physicalState->Paths;
            storage.PathElementCapacity = 256;
            isolationStatus = HvEfiIommuParseDmar(
                &view, acpiRsdpPhysical, &storage, &physicalState->Dmar);
            *admissionState = (uint32_t)isolationStatus;
            if (isolationStatus == HV_EFI_IOMMU_RUNTIME_OK) {
                isolationStatus = HvEfiIommuParseMcfg(
                    &view, &physicalState->Dmar, physicalState->Mcfg, 8,
                    &physicalState->McfgCount);
            }
            if (isolationStatus == HV_EFI_IOMMU_RUNTIME_OK) {
                isolationStatus = HvEfiIommuValidatePciCoverage(
                    &physicalState->Dmar, physicalState->Mcfg,
                    physicalState->McfgCount);
            }
            if (isolationStatus == HV_EFI_IOMMU_RUNTIME_OK) {
                isolationStatus = HvEfiIommuBuildGuestAcpiShadow(
                    &view, &physicalState->Dmar, shadowPhysical,
                    (uint8_t *)(uintptr_t)shadowPhysical,
                    shadowPhysical + HV_EFI_PAGE_SIZE,
                    (uint8_t *)(uintptr_t)(shadowPhysical + HV_EFI_PAGE_SIZE),
                    &physicalState->AcpiShadow);
            }
            if (isolationStatus != HV_EFI_IOMMU_RUNTIME_OK ||
                physicalState->Dmar.UnitCount == 0 ||
                physicalState->Dmar.UnitCount >
                    HV_EFI_PHYSICAL_IOMMU_MAX_UNITS ||
                HvEfiReadRandom64(&physicalState->OwnerToken) != 0 ||
                physicalState->OwnerToken == 0) {
                HvEfiFreeRuntime(shadowPhysical, 2);
                HvEfiFreeRuntime(tablePhysical,
                                 HV_EFI_PHYSICAL_IOMMU_TABLE_PAGES);
                HvEfiFreeRuntime(statePhysical, statePages);
                *admissionState = (uint32_t)(
                    isolationStatus == HV_EFI_IOMMU_RUNTIME_OK ?
                    HV_EFI_IOMMU_RUNTIME_OWNERSHIP_UNAVAILABLE :
                    isolationStatus);
                return EFI_SECURITY_VIOLATION;
            }
            physicalState->Signature = HV_EFI_PHYSICAL_IOMMU_SIGNATURE;
            context->PhysicalIommuStatePhysical = statePhysical;
            context->PhysicalIommuStatePages = (uint32_t)statePages;
            context->PhysicalIommuTablePhysical = tablePhysical;
            context->PhysicalIommuTablePages =
                HV_EFI_PHYSICAL_IOMMU_TABLE_PAGES;
            context->AcpiShadowPhysical = shadowPhysical;
            context->AcpiGuestRsdpPage =
                physicalState->AcpiShadow.GuestRsdpPage;
            context->AcpiGuestXsdtPage =
                physicalState->AcpiShadow.GuestXsdtPage;
            context->DmaIsolationMode =
                HV_EFI_DMA_ISOLATION_VTD_MEDIATED;
            context->DmaIsolationState = HV_EFI_IOMMU_RUNTIME_ADMITTED;
            *admissionState = HV_EFI_IOMMU_RUNTIME_OK;
            return EFI_SUCCESS;
        } else {
            *admissionState = HV_EFI_IOMMU_RUNTIME_DMAR_NOT_FOUND;
        }
    }
    return EFI_SECURITY_VIOLATION;
}

static EFI_STATUS HvEfiGetMemoryMap(EFI_MEMORY_DESCRIPTOR **memoryMap,
                                    UINTN *memoryMapSize,
                                    UINTN *descriptorSize,
                                    UINT32 *descriptorVersion)
{
    EFI_STATUS status;
    UINTN requiredSize = 0;
    UINTN mapKey = 0;
    UINT32 attempt;

    if (gBS == NULL || gBS->GetMemoryMap == NULL ||
        gBS->AllocatePool == NULL || gBS->FreePool == NULL ||
        memoryMap == NULL || memoryMapSize == NULL ||
        descriptorSize == NULL || descriptorVersion == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *memoryMap = NULL;
    *memoryMapSize = 0;
    *descriptorSize = 0;
    *descriptorVersion = 0;
    status = gBS->GetMemoryMap(&requiredSize, NULL, &mapKey,
                               descriptorSize, descriptorVersion);
    if (status != EFI_BUFFER_TOO_SMALL || *descriptorSize == 0) {
        return status;
    }
    for (attempt = 0; attempt < HV_EFI_MEMORY_MAP_MAX_ATTEMPTS; ++attempt) {
        UINTN slack;
        UINTN allocationSize;

        if (*descriptorSize > (UINTN)-1 /
                HV_EFI_MEMORY_MAP_SLACK_DESCRIPTORS) {
            return EFI_BAD_BUFFER_SIZE;
        }
        slack = *descriptorSize * HV_EFI_MEMORY_MAP_SLACK_DESCRIPTORS;
        if (requiredSize > (UINTN)-1 - slack) {
            return EFI_BAD_BUFFER_SIZE;
        }
        allocationSize = requiredSize + slack;
        if (*memoryMap != NULL) {
            status = gBS->FreePool(*memoryMap);
            if (EFI_ERROR(status)) {
                return status;
            }
            *memoryMap = NULL;
        }
        status = gBS->AllocatePool(EfiLoaderData, allocationSize,
                                   (void **)memoryMap);
        if (EFI_ERROR(status)) {
            return status;
        }
        *memoryMapSize = allocationSize;
        status = gBS->GetMemoryMap(memoryMapSize, *memoryMap, &mapKey,
                                   descriptorSize, descriptorVersion);
        if (status != EFI_BUFFER_TOO_SMALL) {
            return status;
        }
        requiredSize = *memoryMapSize;
    }
    return EFI_BUFFER_TOO_SMALL;
}

static EFI_STATUS HvEfiAllocateRuntime(EFI_MEMORY_TYPE type, UINTN pages,
                                       EFI_PHYSICAL_ADDRESS *address);
static void HvEfiFreeRuntime(EFI_PHYSICAL_ADDRESS address, UINTN pages);

static void HvEfiDebugInitializeSerial(void)
{
    HvEfiDebugOutByte(UINT16_C(0x3f9), 0);
    HvEfiDebugOutByte(UINT16_C(0x3fb), UINT8_C(0x80));
    HvEfiDebugOutByte(UINT16_C(0x3f8), 1);
    HvEfiDebugOutByte(UINT16_C(0x3f9), 0);
    HvEfiDebugOutByte(UINT16_C(0x3fb), 3);
    HvEfiDebugOutByte(UINT16_C(0x3fa), UINT8_C(0xc7));
    HvEfiDebugOutByte(UINT16_C(0x3fc), UINT8_C(0x0b));
}

static void HvEfiDebugPrintAscii(const char *text)
{
    while (text != NULL && *text != '\0') {
        uint8_t value = (uint8_t)*text++;
        HvEfiDebugOutByte(UINT16_C(0xe9), value);
        HvEfiDebugOutByte(UINT16_C(0x3f8), value);
    }
}

static void HvEfiDebugPrintHex32(uint32_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    int shift;

    for (shift = 28; shift >= 0; shift -= 4) {
        uint8_t digit = (uint8_t)digits[(value >> shift) & 0xfu];
        HvEfiDebugOutByte(UINT16_C(0xe9), digit);
        HvEfiDebugOutByte(UINT16_C(0x3f8), digit);
    }
}

static int HvEfiCurrentProcessorVmxReady(void)
{
    uint32_t cpuid[4];
    uint64_t featureControl;
    uint64_t vmxBasic;
    uint64_t normalized;

    HvEfiCpuid(1, 0, cpuid);
    if ((cpuid[2] & (UINT32_C(1) << 5)) == 0) {
        return 0;
    }
    featureControl = HvEfiReadMsr(HV_EFI_IA32_FEATURE_CONTROL);
    if ((featureControl & (HV_EFI_FEATURE_CONTROL_LOCK |
                           HV_EFI_FEATURE_CONTROL_VMXON_OUTSIDE_SMX)) !=
        (HV_EFI_FEATURE_CONTROL_LOCK |
         HV_EFI_FEATURE_CONTROL_VMXON_OUTSIDE_SMX)) {
        return 0;
    }
    vmxBasic = HvEfiReadMsr(HV_EFI_IA32_VMX_BASIC);
    if (!HvEfiValidateVmxBasic(vmxBasic) ||
        !HvEfiNormalizeControlRegister(
            HvEfiReadCr0(), HvEfiReadMsr(HV_EFI_IA32_VMX_CR0_FIXED0),
            HvEfiReadMsr(HV_EFI_IA32_VMX_CR0_FIXED1), &normalized) ||
        !HvEfiNormalizeControlRegister(
            HvEfiReadCr4(), HvEfiReadMsr(HV_EFI_IA32_VMX_CR4_FIXED0),
            HvEfiReadMsr(HV_EFI_IA32_VMX_CR4_FIXED1), &normalized)) {
        return 0;
    }
    return 1;
}

typedef struct HV_EFI_AP_PREFLIGHT_CONTEXT {
    HV_EFI_MP_SERVICES_PROTOCOL *MpServices;
    HV_EFI_PROCESSOR_RECORD *Records;
    uint32_t RecordCount;
    volatile uint32_t ReadyCount;
    volatile uint32_t FailureCount;
} HV_EFI_AP_PREFLIGHT_CONTEXT;

static void EFIAPI HvEfiApVmxPreflight(void *procedureArgument)
{
    HV_EFI_AP_PREFLIGHT_CONTEXT *preflight =
        (HV_EFI_AP_PREFLIGHT_CONTEXT *)procedureArgument;
    UINTN processorNumber = 0;
    HV_EFI_PROCESSOR_RECORD *record;

    if (preflight == NULL || preflight->MpServices == NULL ||
        preflight->MpServices->WhoAmI == NULL ||
        EFI_ERROR(preflight->MpServices->WhoAmI(
            preflight->MpServices, &processorNumber)) ||
        processorNumber >= preflight->RecordCount) {
        if (preflight != NULL) {
            ++preflight->FailureCount;
        }
        return;
    }
    record = &preflight->Records[processorNumber];
    if ((record->FirmwareStatus & HV_EFI_PROCESSOR_STATUS_ENABLED) != 0 &&
        (record->StateFlags & HV_EFI_PROCESSOR_STATE_VMX_READY) == 0 &&
        HvEfiCurrentProcessorVmxReady()) {
        record->StateFlags |= HV_EFI_PROCESSOR_STATE_VMX_READY;
        ++preflight->ReadyCount;
    } else {
        ++preflight->FailureCount;
    }
}

static void HvEfiCaptureProcessorTopology(HV_EFI_CONTEXT *context)
{
    EFI_GUID mpServicesGuid = HV_EFI_MP_SERVICES_PROTOCOL_GUID;
    HV_EFI_MP_SERVICES_PROTOCOL *mpServices = NULL;
    HV_EFI_AP_PREFLIGHT_CONTEXT preflight = { 0, 0 };
    UINTN processorCount = 0;
    UINTN enabledProcessorCount = 0;
    UINTN bspProcessorNumber = 0;
    HV_EFI_PROCESSOR_RECORD *records = NULL;
    EFI_PHYSICAL_ADDRESS recordsPa = 0;
    uint32_t recordPages = 0;
    uint32_t observedBspCount = 0;
    uint32_t observedEnabledCount = 0;
    EFI_STATUS status;

    if (context == NULL) {
        return;
    }
    context->ApPreflightStatus = HV_EFI_AP_PREFLIGHT_UNAVAILABLE;
    if (!HvEfiCurrentProcessorVmxReady()) {
        context->ApPreflightStatus = HV_EFI_AP_PREFLIGHT_FAILED;
        return;
    }
    context->VmxReadyProcessorCount = 1;
    if (gBS == NULL || gBS->LocateProtocol == NULL ||
        EFI_ERROR(gBS->LocateProtocol(&mpServicesGuid, NULL,
                                      (void **)&mpServices)) ||
        mpServices == NULL || mpServices->GetNumberOfProcessors == NULL ||
        mpServices->GetProcessorInfo == NULL ||
        mpServices->StartupAllAPs == NULL || mpServices->WhoAmI == NULL) {
        return;
    }
    status = mpServices->GetNumberOfProcessors(
        mpServices, &processorCount, &enabledProcessorCount);
    if (EFI_ERROR(status)) {
        context->ApPreflightStatus = HV_EFI_AP_PREFLIGHT_FAILED;
        return;
    }
    status = mpServices->WhoAmI(mpServices, &bspProcessorNumber);
    if (EFI_ERROR(status) ||
        !HvEfiTopologySnapshotValid(processorCount, enabledProcessorCount,
                                    bspProcessorNumber)) {
        context->ApPreflightStatus = HV_EFI_AP_PREFLIGHT_FAILED;
        return;
    }

    if (!HvEfiProcessorRecordPageCount(processorCount, &recordPages) ||
        EFI_ERROR(HvEfiAllocateRuntime(EfiRuntimeServicesData, recordPages,
                                       &recordsPa))) {
        context->ApPreflightStatus = HV_EFI_AP_PREFLIGHT_FAILED;
        return;
    }
    records = (HV_EFI_PROCESSOR_RECORD *)(uintptr_t)recordsPa;
    HvEfiZero(records, (UINTN)recordPages * HV_EFI_PAGE_SIZE);
    for (UINTN processorNumber = 0;
         processorNumber < processorCount;
         ++processorNumber) {
        HV_EFI_PROCESSOR_INFORMATION information;
        HV_EFI_PROCESSOR_RECORD *record = &records[processorNumber];

        HvEfiZero(&information, sizeof(information));
        status = mpServices->GetProcessorInfo(
            mpServices, processorNumber, &information);
        if (EFI_ERROR(status)) {
            HvEfiFreeRuntime(recordsPa, recordPages);
            context->ApPreflightStatus = HV_EFI_AP_PREFLIGHT_FAILED;
            return;
        }
        record->ProcessorId = information.ProcessorId;
        record->ProcessorNumber = (uint32_t)processorNumber;
        record->FirmwareStatus = information.StatusFlag;
        if ((information.StatusFlag & HV_EFI_PROCESSOR_STATUS_ENABLED) != 0) {
            ++observedEnabledCount;
        }
        if ((information.StatusFlag & HV_EFI_PROCESSOR_STATUS_AS_BSP) != 0) {
            ++observedBspCount;
        }
    }
    if (observedEnabledCount != enabledProcessorCount ||
        observedBspCount != 1 ||
        (records[bspProcessorNumber].FirmwareStatus &
         (HV_EFI_PROCESSOR_STATUS_AS_BSP |
          HV_EFI_PROCESSOR_STATUS_ENABLED)) !=
            (HV_EFI_PROCESSOR_STATUS_AS_BSP |
             HV_EFI_PROCESSOR_STATUS_ENABLED)) {
        HvEfiFreeRuntime(recordsPa, recordPages);
        context->ApPreflightStatus = HV_EFI_AP_PREFLIGHT_FAILED;
        return;
    }

    context->ProcessorCount = (uint32_t)processorCount;
    context->EnabledProcessorCount = (uint32_t)enabledProcessorCount;
    context->BspProcessorNumber = (uint32_t)bspProcessorNumber;
    context->ProcessorRecordsPhysical = recordsPa;
    context->ProcessorRecordPages = recordPages;
    context->ProcessorRecordCount = (uint32_t)processorCount;
    records[bspProcessorNumber].StateFlags |=
        HV_EFI_PROCESSOR_STATE_VMX_READY;
    if (enabledProcessorCount == 1) {
        context->ApPreflightStatus = HV_EFI_AP_PREFLIGHT_BSP_ONLY;
        return;
    }

    preflight.MpServices = mpServices;
    preflight.Records = records;
    preflight.RecordCount = (uint32_t)processorCount;
    preflight.ReadyCount = 1;
    status = mpServices->StartupAllAPs(
        mpServices, HvEfiApVmxPreflight, TRUE, NULL,
        HV_EFI_AP_OPERATION_TIMEOUT_US, &preflight, NULL);
    context->VmxReadyProcessorCount = preflight.ReadyCount;
    if (!EFI_ERROR(status) && preflight.FailureCount == 0 &&
        preflight.ReadyCount == enabledProcessorCount) {
        context->ApPreflightStatus = HV_EFI_AP_PREFLIGHT_READY;
    } else {
        context->ApPreflightStatus = HV_EFI_AP_PREFLIGHT_FAILED;
    }
}

static int HvEfiBindBspProcessorResources(
    HV_EFI_CONTEXT *context,
    EFI_PHYSICAL_ADDRESS contextPa,
    EFI_PHYSICAL_ADDRESS vmxonPa,
    EFI_PHYSICAL_ADDRESS vmcsPa,
    EFI_PHYSICAL_ADDRESS hostStackPa,
    EFI_PHYSICAL_ADDRESS guestStackPa,
    EFI_PHYSICAL_ADDRESS pageTablePa,
    EFI_PHYSICAL_ADDRESS descriptorPa)
{
    HV_EFI_PROCESSOR_RECORD *records;
    HV_EFI_PROCESSOR_RECORD *record;
    EFI_PHYSICAL_ADDRESS eptPa = 0;
    EFI_PHYSICAL_ADDRESS shadowEptPa = 0;
    EFI_PHYSICAL_ADDRESS shadowStatePa = 0;

    if (context == NULL) {
        return 0;
    }
    if (context->ProcessorRecordsPhysical == 0) {
        context->CurrentProcessorNumber = 0;
        context->ResourceBoundProcessorCount = 1;
        context->ProcessorResourceStatus =
            HV_EFI_PROCESSOR_RESOURCES_UNAVAILABLE;
        return 1;
    }
    if (context->ProcessorRecordCount == 0 ||
        context->BspProcessorNumber >= context->ProcessorRecordCount ||
        context->ProcessorRecordPages == 0) {
        return 0;
    }
    records = (HV_EFI_PROCESSOR_RECORD *)(uintptr_t)
        context->ProcessorRecordsPhysical;
    record = &records[context->BspProcessorNumber];
    if ((record->FirmwareStatus &
         (HV_EFI_PROCESSOR_STATUS_AS_BSP |
          HV_EFI_PROCESSOR_STATUS_ENABLED)) !=
            (HV_EFI_PROCESSOR_STATUS_AS_BSP |
             HV_EFI_PROCESSOR_STATUS_ENABLED) ||
        (record->StateFlags & HV_EFI_PROCESSOR_STATE_VMX_READY) == 0 ||
        (record->StateFlags & HV_EFI_PROCESSOR_STATE_RESOURCES_BOUND) != 0) {
        return 0;
    }
    if (EFI_ERROR(HvEfiAllocateRuntime(EfiRuntimeServicesData,
                                       HV_EFI_EPT_TABLE_PAGES,
                                       &eptPa))) {
        return 0;
    }
    if (EFI_ERROR(HvEfiAllocateRuntime(EfiRuntimeServicesData,
                                       HV_EFI_EPT_TABLE_PAGES,
                                       &shadowEptPa)) ||
        EFI_ERROR(HvEfiAllocateRuntime(EfiRuntimeServicesData,
                                       HV_EFI_SHADOW_EPT_STATE_PAGES,
                                       &shadowStatePa))) {
        HvEfiFreeRuntime(shadowStatePa, HV_EFI_SHADOW_EPT_STATE_PAGES);
        HvEfiFreeRuntime(shadowEptPa, HV_EFI_EPT_TABLE_PAGES);
        HvEfiFreeRuntime(eptPa, HV_EFI_EPT_TABLE_PAGES);
        return 0;
    }
    record->ContextPhysical = contextPa;
    record->VmxonPhysical = vmxonPa;
    record->VmcsPhysical = vmcsPa;
    record->HostStackPhysical = hostStackPa;
    record->GuestStackPhysical = guestStackPa;
    record->PageTablePhysical = pageTablePa;
    record->DescriptorStoragePhysical = descriptorPa;
    record->EptPhysical = eptPa;
    record->ShadowEptPhysical = shadowEptPa;
    record->ShadowEptStatePhysical = shadowStatePa;
    if (!HvEfiProcessorResourceAddressesValid(record)) {
        record->ContextPhysical = 0;
        record->VmxonPhysical = 0;
        record->VmcsPhysical = 0;
        record->HostStackPhysical = 0;
        record->GuestStackPhysical = 0;
        record->PageTablePhysical = 0;
        record->DescriptorStoragePhysical = 0;
        record->EptPhysical = 0;
        record->ShadowEptPhysical = 0;
        record->ShadowEptStatePhysical = 0;
        HvEfiFreeRuntime(shadowStatePa, HV_EFI_SHADOW_EPT_STATE_PAGES);
        HvEfiFreeRuntime(shadowEptPa, HV_EFI_EPT_TABLE_PAGES);
        HvEfiFreeRuntime(eptPa, HV_EFI_EPT_TABLE_PAGES);
        return 0;
    }
    record->StateFlags |= HV_EFI_PROCESSOR_STATE_RESOURCES_BOUND;
    context->CurrentProcessorNumber = context->BspProcessorNumber;
    context->ResourceBoundProcessorCount = 1;
    context->ProcessorResourceStatus =
        HV_EFI_PROCESSOR_RESOURCES_BSP_ONLY;
    context->EptPhysical = eptPa;
    context->ShadowEptPhysical = shadowEptPa;
    context->ShadowEptStatePhysical = shadowStatePa;
    return 1;
}

static void HvEfiReleaseProcessorRecordResources(
    HV_EFI_PROCESSOR_RECORD *record)
{
    if (record == NULL) {
        return;
    }
    HvEfiFreeRuntime(record->EptPhysical, HV_EFI_EPT_TABLE_PAGES);
    HvEfiFreeRuntime(record->ShadowEptPhysical, HV_EFI_EPT_TABLE_PAGES);
    HvEfiFreeRuntime(record->ShadowEptStatePhysical,
                     HV_EFI_SHADOW_EPT_STATE_PAGES);
    HvEfiFreeRuntime(record->DescriptorStoragePhysical,
                     HV_EFI_DESCRIPTOR_STORAGE_PAGES);
    HvEfiFreeRuntime(record->PageTablePhysical, HV_EFI_PAGE_TABLE_PAGES);
    HvEfiFreeRuntime(record->GuestStackPhysical, HV_EFI_GUEST_STACK_PAGES);
    HvEfiFreeRuntime(record->HostStackPhysical, HV_EFI_HOST_STACK_PAGES);
    HvEfiFreeRuntime(record->VmcsPhysical, HV_EFI_VMCS_PAGES);
    HvEfiFreeRuntime(record->VmxonPhysical, HV_EFI_VMXON_PAGES);
    HvEfiFreeRuntime(record->ContextPhysical, HV_EFI_CONTEXT_PAGES);
    record->ContextPhysical = 0;
    record->VmxonPhysical = 0;
    record->VmcsPhysical = 0;
    record->HostStackPhysical = 0;
    record->GuestStackPhysical = 0;
    record->PageTablePhysical = 0;
    record->DescriptorStoragePhysical = 0;
    record->EptPhysical = 0;
    record->ShadowEptPhysical = 0;
    record->ShadowEptStatePhysical = 0;
    record->StateFlags &= ~(HV_EFI_PROCESSOR_STATE_RESOURCES_BOUND |
                            HV_EFI_PROCESSOR_STATE_VIRTUALIZED |
                            HV_EFI_PROCESSOR_STATE_PREPARED |
                            HV_EFI_PROCESSOR_STATE_STARTUP_CAPABLE |
                            HV_EFI_PROCESSOR_STATE_EPT_READY);
}

static int HvEfiMarkCurrentProcessorState(HV_EFI_CONTEXT *context,
                                          uint32_t stateFlags)
{
    HV_EFI_PROCESSOR_RECORD *records;
    HV_EFI_PROCESSOR_RECORD *record;

    if (context == NULL) {
        return 0;
    }
    if (context->ProcessorRecordsPhysical == 0) {
        return 1;
    }
    if (context->ProcessorRecordCount == 0 ||
        context->CurrentProcessorNumber >= context->ProcessorRecordCount) {
        return 0;
    }
    records = (HV_EFI_PROCESSOR_RECORD *)(uintptr_t)
        context->ProcessorRecordsPhysical;
    record = &records[context->CurrentProcessorNumber];
    if (record->ContextPhysical != (uint64_t)(uintptr_t)context ||
        (record->StateFlags & HV_EFI_PROCESSOR_STATE_RESOURCES_BOUND) == 0) {
        return 0;
    }
    record->StateFlags |= stateFlags;
    return 1;
}

static void HvEfiReleaseApProcessorResources(HV_EFI_CONTEXT *context)
{
    HV_EFI_PROCESSOR_RECORD *records;
    uint32_t processorNumber;

    if (context == NULL || context->ProcessorRecordsPhysical == 0 ||
        context->ProcessorRecordCount == 0 ||
        context->BspProcessorNumber >= context->ProcessorRecordCount) {
        return;
    }
    records = (HV_EFI_PROCESSOR_RECORD *)(uintptr_t)
        context->ProcessorRecordsPhysical;
    for (processorNumber = 0;
         processorNumber < context->ProcessorRecordCount;
         ++processorNumber) {
        if (processorNumber != context->BspProcessorNumber) {
            HvEfiReleaseProcessorRecordResources(&records[processorNumber]);
        }
    }
    context->ResourceBoundProcessorCount =
        (records[context->BspProcessorNumber].StateFlags &
         HV_EFI_PROCESSOR_STATE_RESOURCES_BOUND) != 0 ? 1u : 0u;
}

static void HvEfiReleaseBspEpt(HV_EFI_CONTEXT *context)
{
    HV_EFI_PROCESSOR_RECORD *records;
    HV_EFI_PROCESSOR_RECORD *record;

    if (context == NULL || context->ProcessorRecordsPhysical == 0 ||
        context->BspProcessorNumber >= context->ProcessorRecordCount) {
        return;
    }
    records = (HV_EFI_PROCESSOR_RECORD *)(uintptr_t)
        context->ProcessorRecordsPhysical;
    record = &records[context->BspProcessorNumber];
    HvEfiFreeRuntime(record->EptPhysical, HV_EFI_EPT_TABLE_PAGES);
    HvEfiFreeRuntime(record->ShadowEptPhysical, HV_EFI_EPT_TABLE_PAGES);
    HvEfiFreeRuntime(record->ShadowEptStatePhysical,
                     HV_EFI_SHADOW_EPT_STATE_PAGES);
    record->EptPhysical = 0;
    record->ShadowEptPhysical = 0;
    record->ShadowEptStatePhysical = 0;
    context->EptPhysical = 0;
    context->ShadowEptPhysical = 0;
    context->ShadowEptStatePhysical = 0;
}

static int HvEfiInitializePrivatePageTables(EFI_PHYSICAL_ADDRESS pageTablePa)
{
    uint32_t index;

    HvEfiZero((void *)(uintptr_t)pageTablePa,
              HV_EFI_PAGE_TABLE_PAGES * HV_EFI_PAGE_SIZE);
    ((uint64_t *)(uintptr_t)pageTablePa)[0] = pageTablePa + 0x1000 | 3;
    ((uint64_t *)(uintptr_t)(pageTablePa + 0x1000))[0] =
        pageTablePa + 0x2000 | 3;
    for (index = 0; index < 512; ++index) {
        ((uint64_t *)(uintptr_t)(pageTablePa + 0x2000))[index] =
            HvEfiIdentityPdEntry(index);
    }
    if (!HvEfiScannerPrepareHostPageTables(
            (uint64_t *)(uintptr_t)pageTablePa,
            HV_EFI_PAGE_TABLE_PAGES, pageTablePa)) {
        return 0;
    }
    return HvEfiBuildIoBitmaps(
        (uint8_t *)(uintptr_t)(pageTablePa +
            (uint64_t)HV_EFI_IO_BITMAP_A_PAGE * HV_EFI_PAGE_SIZE),
        (uint8_t *)(uintptr_t)(pageTablePa +
            (uint64_t)HV_EFI_IO_BITMAP_B_PAGE * HV_EFI_PAGE_SIZE),
        HV_EFI_IO_BITMAP_BYTES);
}

static EFI_STATUS HvEfiAllocateProcessorRecordResources(
    HV_EFI_CONTEXT *rootContext,
    HV_EFI_PROCESSOR_RECORD *record,
    uint32_t revision)
{
    HV_EFI_CONTEXT *processorContext;
    EFI_STATUS status;

    if (rootContext == NULL || record == NULL ||
        (record->FirmwareStatus & HV_EFI_PROCESSOR_STATUS_ENABLED) == 0 ||
        (record->FirmwareStatus & HV_EFI_PROCESSOR_STATUS_AS_BSP) != 0 ||
        (record->StateFlags & HV_EFI_PROCESSOR_STATE_VMX_READY) == 0 ||
        (record->StateFlags & HV_EFI_PROCESSOR_STATE_RESOURCES_BOUND) != 0) {
        return EFI_INVALID_PARAMETER;
    }

    status = HvEfiAllocateRuntime(EfiRuntimeServicesData,
                                  HV_EFI_CONTEXT_PAGES,
                                  &record->ContextPhysical);
    if (EFI_ERROR(status)) goto fail;
    status = HvEfiAllocateRuntime(EfiRuntimeServicesData, HV_EFI_VMXON_PAGES,
                                  &record->VmxonPhysical);
    if (EFI_ERROR(status)) goto fail;
    status = HvEfiAllocateRuntime(EfiRuntimeServicesData, HV_EFI_VMCS_PAGES,
                                  &record->VmcsPhysical);
    if (EFI_ERROR(status)) goto fail;
    status = HvEfiAllocateRuntime(EfiRuntimeServicesData,
                                  HV_EFI_HOST_STACK_PAGES,
                                  &record->HostStackPhysical);
    if (EFI_ERROR(status)) goto fail;
    status = HvEfiAllocateRuntime(EfiRuntimeServicesData,
                                  HV_EFI_GUEST_STACK_PAGES,
                                  &record->GuestStackPhysical);
    if (EFI_ERROR(status)) goto fail;
    status = HvEfiAllocateRuntime(EfiRuntimeServicesData,
                                  HV_EFI_PAGE_TABLE_PAGES,
                                  &record->PageTablePhysical);
    if (EFI_ERROR(status)) goto fail;
    status = HvEfiAllocateRuntime(EfiRuntimeServicesData,
                                  HV_EFI_DESCRIPTOR_STORAGE_PAGES,
                                  &record->DescriptorStoragePhysical);
    if (EFI_ERROR(status)) goto fail;
    status = HvEfiAllocateRuntime(EfiRuntimeServicesData,
                                  HV_EFI_EPT_TABLE_PAGES,
                                  &record->EptPhysical);
    if (EFI_ERROR(status)) goto fail;
    status = HvEfiAllocateRuntime(EfiRuntimeServicesData,
                                  HV_EFI_EPT_TABLE_PAGES,
                                  &record->ShadowEptPhysical);
    if (EFI_ERROR(status)) goto fail;
    status = HvEfiAllocateRuntime(EfiRuntimeServicesData,
                                  HV_EFI_SHADOW_EPT_STATE_PAGES,
                                  &record->ShadowEptStatePhysical);
    if (EFI_ERROR(status)) goto fail;
    if (!HvEfiProcessorResourceAddressesValid(record)) {
        status = EFI_ABORTED;
        goto fail;
    }

    processorContext = (HV_EFI_CONTEXT *)(uintptr_t)record->ContextPhysical;
    HvEfiZero(processorContext, HV_EFI_PAGE_SIZE);
    HvEfiZero((void *)(uintptr_t)record->VmxonPhysical, HV_EFI_PAGE_SIZE);
    HvEfiZero((void *)(uintptr_t)record->VmcsPhysical, HV_EFI_PAGE_SIZE);
    HvEfiZero((void *)(uintptr_t)record->HostStackPhysical,
              HV_EFI_PAGE_SIZE);
    HvEfiZero((void *)(uintptr_t)record->GuestStackPhysical,
              HV_EFI_PAGE_SIZE);
    HvEfiZero((void *)(uintptr_t)record->DescriptorStoragePhysical,
              HV_EFI_DESCRIPTOR_STORAGE_PAGES * HV_EFI_PAGE_SIZE);
    HvEfiZero((void *)(uintptr_t)record->EptPhysical,
              HV_EFI_EPT_TABLE_PAGES * HV_EFI_PAGE_SIZE);
    HvEfiZero((void *)(uintptr_t)record->ShadowEptPhysical,
              HV_EFI_EPT_TABLE_PAGES * HV_EFI_PAGE_SIZE);
    HvEfiZero((void *)(uintptr_t)record->ShadowEptStatePhysical,
              HV_EFI_SHADOW_EPT_STATE_PAGES * HV_EFI_PAGE_SIZE);
    if (!HvEfiInitializePrivatePageTables(record->PageTablePhysical)) {
        status = EFI_ABORTED;
        goto fail;
    }
    *(uint32_t *)(uintptr_t)record->VmxonPhysical = revision;
    *(uint32_t *)(uintptr_t)record->VmcsPhysical = revision;

    processorContext->Signature = HV_EFI_CONTEXT_MAGIC;
    processorContext->Version = HV_EFI_CONTEXT_VERSION;
    processorContext->Size = (uint32_t)sizeof(*processorContext);
    processorContext->HostStackTop = record->HostStackPhysical +
        ((uint64_t)HV_EFI_HOST_STACK_PAGES * HV_EFI_PAGE_SIZE / 2u) - 8;
    processorContext->GuestStackTop = record->GuestStackPhysical +
        HV_EFI_PAGE_SIZE - 32;
    processorContext->ResidentCodeBase = rootContext->ResidentCodeBase;
    processorContext->ResidentCodeSize = rootContext->ResidentCodeSize;
    processorContext->ResidentHostRip = rootContext->ResidentHostRip;
    processorContext->GuestBootstrapRip = rootContext->GuestBootstrapRip;
    processorContext->GuestReturnRip = rootContext->GuestReturnRip;
    processorContext->GuestTransitionCodeBase =
        rootContext->GuestTransitionCodeBase;
    processorContext->GuestTransitionCodeSize =
        rootContext->GuestTransitionCodeSize;
    HvEfiCopy(processorContext->RuntimeToken, rootContext->RuntimeToken,
              sizeof(processorContext->RuntimeToken));
    processorContext->RuntimeGeneration = rootContext->RuntimeGeneration;
    processorContext->RuntimeState = rootContext->RuntimeState;
    processorContext->RuntimeCapabilities =
        rootContext->RuntimeCapabilities;
    processorContext->PhysicalIommuStatePhysical =
        rootContext->PhysicalIommuStatePhysical;
    processorContext->PhysicalIommuStatePages =
        rootContext->PhysicalIommuStatePages;
    processorContext->DmaIsolationMode = rootContext->DmaIsolationMode;
    processorContext->PhysicalIommuTablePhysical =
        rootContext->PhysicalIommuTablePhysical;
    processorContext->PhysicalIommuTablePages =
        rootContext->PhysicalIommuTablePages;
    processorContext->DmaIsolationState = rootContext->DmaIsolationState;
    processorContext->AcpiShadowPhysical = rootContext->AcpiShadowPhysical;
    processorContext->AcpiGuestRsdpPage = rootContext->AcpiGuestRsdpPage;
    processorContext->AcpiGuestXsdtPage = rootContext->AcpiGuestXsdtPage;
    processorContext->GpuStatePhysical = rootContext->GpuStatePhysical;
    processorContext->GpuResidentEntry = rootContext->GpuResidentEntry;
    processorContext->RuntimeScannerCodeBase =
        rootContext->RuntimeScannerCodeBase;
    processorContext->RuntimeScannerCodeSize =
        rootContext->RuntimeScannerCodeSize;
    processorContext->RuntimeScannerEntry = rootContext->RuntimeScannerEntry;
    processorContext->RuntimeProcessEntry = rootContext->RuntimeProcessEntry;
    processorContext->RuntimeShadowEntry = rootContext->RuntimeShadowEntry;
    processorContext->RuntimeAnalysisEntry =
        rootContext->RuntimeAnalysisEntry;
    processorContext->RuntimeAnalysisStatePhysical =
        rootContext->RuntimeAnalysisStatePhysical;
    processorContext->RuntimeAnalysisStatePages =
        rootContext->RuntimeAnalysisStatePages;
    processorContext->RuntimeScannerStatePhysical =
        rootContext->RuntimeScannerStatePhysical;
    processorContext->RuntimeScannerRamRangesPhysical =
        rootContext->RuntimeScannerRamRangesPhysical;
    processorContext->RuntimeScannerCodePages =
        rootContext->RuntimeScannerCodePages;
    processorContext->RuntimeScannerRamRangePages =
        rootContext->RuntimeScannerRamRangePages;
    processorContext->RuntimeScannerRamRangeCount =
        rootContext->RuntimeScannerRamRangeCount;
    processorContext->VmxonPhysical = record->VmxonPhysical;
    processorContext->VmcsPhysical = record->VmcsPhysical;
    processorContext->PageTablePhysical = record->PageTablePhysical;
    processorContext->MsrBitmapPhysical = record->PageTablePhysical +
        (uint64_t)HV_EFI_MSR_BITMAP_PAGE * HV_EFI_PAGE_SIZE;
    processorContext->DescriptorStoragePhysical =
        record->DescriptorStoragePhysical;
    processorContext->DescriptorStoragePages =
        HV_EFI_DESCRIPTOR_STORAGE_PAGES;
    processorContext->EptPhysical = record->EptPhysical;
    processorContext->ShadowEptPhysical = record->ShadowEptPhysical;
    processorContext->ShadowEptStatePhysical =
        record->ShadowEptStatePhysical;
    processorContext->ProcessorCount = rootContext->ProcessorCount;
    processorContext->EnabledProcessorCount =
        rootContext->EnabledProcessorCount;
    processorContext->BspProcessorNumber = rootContext->BspProcessorNumber;
    processorContext->VmxReadyProcessorCount =
        rootContext->VmxReadyProcessorCount;
    processorContext->ApPreflightStatus = rootContext->ApPreflightStatus;
    processorContext->ProcessorRecordsPhysical =
        rootContext->ProcessorRecordsPhysical;
    processorContext->ProcessorRecordPages =
        rootContext->ProcessorRecordPages;
    processorContext->ProcessorRecordCount =
        rootContext->ProcessorRecordCount;
    processorContext->CurrentProcessorNumber = record->ProcessorNumber;
    if (record->ProcessorNumber >= UINT16_MAX) {
        status = EFI_UNSUPPORTED;
        goto fail;
    }
    processorContext->Vpid = (uint16_t)(record->ProcessorNumber + 1u);
    *(uint64_t *)(uintptr_t)processorContext->HostStackTop =
        (uint64_t)(uintptr_t)processorContext;
    record->StateFlags |= HV_EFI_PROCESSOR_STATE_RESOURCES_BOUND;
    return EFI_SUCCESS;

fail:
    HvEfiReleaseProcessorRecordResources(record);
    return status;
}

static EFI_STATUS HvEfiAllocateApProcessorResources(
    HV_EFI_CONTEXT *context,
    uint32_t revision)
{
    HV_EFI_PROCESSOR_RECORD *records;
    uint32_t processorNumber;
    EFI_STATUS status = EFI_SUCCESS;

    if (context == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    if (context->ProcessorRecordsPhysical == 0) {
        context->ProcessorResourceStatus =
            HV_EFI_PROCESSOR_RESOURCES_UNAVAILABLE;
        return EFI_SUCCESS;
    }
    if (context->ProcessorRecordCount == 0 ||
        context->BspProcessorNumber >= context->ProcessorRecordCount ||
        context->ResourceBoundProcessorCount != 1) {
        context->ProcessorResourceStatus = HV_EFI_PROCESSOR_RESOURCES_FAILED;
        return EFI_ABORTED;
    }
    if (context->EnabledProcessorCount == 1) {
        context->ProcessorResourceStatus =
            HV_EFI_PROCESSOR_RESOURCES_BSP_ONLY;
        return EFI_SUCCESS;
    }
    if (context->ApPreflightStatus != HV_EFI_AP_PREFLIGHT_READY ||
        context->VmxReadyProcessorCount != context->EnabledProcessorCount) {
        context->ProcessorResourceStatus = HV_EFI_PROCESSOR_RESOURCES_FAILED;
        return EFI_ABORTED;
    }

    records = (HV_EFI_PROCESSOR_RECORD *)(uintptr_t)
        context->ProcessorRecordsPhysical;
    for (processorNumber = 0;
         processorNumber < context->ProcessorRecordCount;
         ++processorNumber) {
        HV_EFI_PROCESSOR_RECORD *record = &records[processorNumber];

        if ((record->FirmwareStatus & HV_EFI_PROCESSOR_STATUS_ENABLED) == 0 ||
            processorNumber == context->BspProcessorNumber) {
            continue;
        }
        status = HvEfiAllocateProcessorRecordResources(
            context, record, revision);
        if (EFI_ERROR(status)) {
            goto fail;
        }
        ++context->ResourceBoundProcessorCount;
    }
    if (context->ResourceBoundProcessorCount !=
        context->EnabledProcessorCount) {
        status = EFI_ABORTED;
        goto fail;
    }
    context->ProcessorResourceStatus = HV_EFI_PROCESSOR_RESOURCES_READY;
    for (processorNumber = 0;
         processorNumber < context->ProcessorRecordCount;
         ++processorNumber) {
        HV_EFI_PROCESSOR_RECORD *record = &records[processorNumber];

        if ((record->StateFlags &
             HV_EFI_PROCESSOR_STATE_RESOURCES_BOUND) != 0) {
            HV_EFI_CONTEXT *processorContext =
                (HV_EFI_CONTEXT *)(uintptr_t)record->ContextPhysical;
            processorContext->ResourceBoundProcessorCount =
                context->ResourceBoundProcessorCount;
            processorContext->ProcessorResourceStatus =
                context->ProcessorResourceStatus;
        }
    }
    return EFI_SUCCESS;

fail:
    HvEfiReleaseApProcessorResources(context);
    HvEfiReleaseBspEpt(context);
    context->ProcessorResourceStatus = HV_EFI_PROCESSOR_RESOURCES_FAILED;
    return status;
}

static void HvEfiReportProcessorResources(const HV_EFI_CONTEXT *context)
{
    if (context == NULL) {
        return;
    }
    switch (context->ProcessorResourceStatus) {
    case HV_EFI_PROCESSOR_RESOURCES_BSP_ONLY:
        HvEfiDebugPrintAscii(
            HV_EFI_PROCESSOR_RESOURCES_BSP_ONLY_MARKER "\n");
        break;
    case HV_EFI_PROCESSOR_RESOURCES_READY:
        HvEfiDebugPrintAscii(HV_EFI_PROCESSOR_RESOURCES_PASS_MARKER "\n");
        break;
    case HV_EFI_PROCESSOR_RESOURCES_FAILED:
        HvEfiDebugPrintAscii(HV_EFI_PROCESSOR_RESOURCES_FAIL_MARKER "\n");
        break;
    default:
        HvEfiDebugPrintAscii(
            HV_EFI_PROCESSOR_RESOURCES_UNAVAILABLE_MARKER "\n");
        break;
    }
}

static void HvEfiReportProcessorTopology(const HV_EFI_CONTEXT *context)
{
    if (context == NULL) {
        return;
    }
    if (context->ProcessorCount != 0) {
        HvEfiDebugPrintAscii(HV_EFI_TOPOLOGY_PASS_MARKER "\n");
    }
    switch (context->ApPreflightStatus) {
    case HV_EFI_AP_PREFLIGHT_BSP_ONLY:
        HvEfiDebugPrintAscii(HV_EFI_AP_PREFLIGHT_BSP_ONLY_MARKER "\n");
        break;
    case HV_EFI_AP_PREFLIGHT_READY:
        HvEfiDebugPrintAscii(HV_EFI_AP_PREFLIGHT_PASS_MARKER "\n");
        break;
    case HV_EFI_AP_PREFLIGHT_FAILED:
        HvEfiDebugPrintAscii(HV_EFI_AP_PREFLIGHT_FAIL_MARKER "\n");
        break;
    default:
        HvEfiDebugPrintAscii(HV_EFI_AP_PREFLIGHT_UNAVAILABLE_MARKER "\n");
        break;
    }
}

static EFI_STATUS HvEfiAllocateRuntime(EFI_MEMORY_TYPE type, UINTN pages,
                                       EFI_PHYSICAL_ADDRESS *address)
{
    EFI_PHYSICAL_ADDRESS maximum = HV_EFI_MAX_PHYSICAL_ADDRESS;
    EFI_STATUS status;

    if (gBS == NULL || address == NULL || pages == 0) {
        return EFI_INVALID_PARAMETER;
    }
    status = gBS->AllocatePages(AllocateMaxAddress, type, pages, &maximum);
    if (EFI_ERROR(status)) {
        return status;
    }
    if (!HvEfiRuntimePageAddressValid(maximum) ||
        pages > (UINTN)((HV_EFI_MAX_PHYSICAL_ADDRESS - maximum + 1) /
                         HV_EFI_PAGE_SIZE)) {
        (void)gBS->FreePages(maximum, pages);
        return EFI_OUT_OF_RESOURCES;
    }
    *address = maximum;
    return EFI_SUCCESS;
}

static void HvEfiFreeRuntime(EFI_PHYSICAL_ADDRESS address, UINTN pages)
{
    if (gBS != NULL && address != 0 && pages != 0) {
        (void)gBS->FreePages(address, pages);
    }
}

static int HvEfiWriteField(uint32_t field, uint64_t value)
{
    return HvEfiVmwrite((uint64_t)field, value) == 0;
}

static uint64_t HvEfiDescriptorBase(const HV_EFI_CONTEXT *context,
                                    uint64_t selector)
{
    const UINT8 *descriptor;
    uint64_t base;
    uint64_t offset;

    if (context == NULL || (selector & UINT64_C(0xfff8)) == 0 ||
        (selector & UINT64_C(4)) != 0) {
        return 0;
    }
    offset = selector & UINT64_C(0xfff8);
    if (offset > context->GdtrLimit || context->GdtrLimit - offset < 11 ||
        context->GdtrBase > UINT64_MAX - offset) {
        return 0;
    }
    descriptor = (const UINT8 *)(uintptr_t)(context->GdtrBase + offset);
    base = (uint64_t)descriptor[2] |
        ((uint64_t)descriptor[3] << 8) |
        ((uint64_t)descriptor[4] << 16) |
        ((uint64_t)descriptor[7] << 24);
    /* A 64-bit TSS descriptor occupies two GDT slots. */
    base |= (uint64_t)descriptor[8] << 32;
    base |= (uint64_t)descriptor[9] << 40;
    base |= (uint64_t)descriptor[10] << 48;
    base |= (uint64_t)descriptor[11] << 56;
    return base;
}

static uint32_t HvEfiSegmentAccess(const HV_EFI_CONTEXT *context,
                                   uint64_t selector)
{
    const UINT8 *descriptor;
    uint64_t offset;

    /* A null FS/GS/LDTR selector is represented as an unusable VMCS segment. */
    if (context == NULL) {
        return 0;
    }
    if ((selector & UINT64_C(0xfff8)) == 0) {
        return UINT32_C(1) << 16;
    }
    /* This minimal payload intentionally does not load descriptors from an LDT. */
    if ((selector & UINT64_C(4)) != 0) {
        return 0;
    }
    offset = selector & UINT64_C(0xfff8);
    if (offset > context->GdtrLimit || context->GdtrLimit - offset < 7 ||
        context->GdtrBase > UINT64_MAX - offset) {
        return 0;
    }
    descriptor = (const UINT8 *)(uintptr_t)(context->GdtrBase + offset);
    return (uint32_t)descriptor[5] |
        ((uint32_t)(descriptor[6] & 0xf0u) << 8);
}

static uint32_t HvEfiGuestTrAccess(const HV_EFI_CONTEXT *context)
{
    uint32_t access;

    if (context == NULL) {
        return 0;
    }
    if (context->ApStartupCapabilitiesReady != 0 ||
        (context->GuestTssBase != 0 &&
         context->TrBase == context->GuestTssBase)) {
        return UINT32_C(0x8b);
    }
    access = HvEfiSegmentAccess(context, context->Tr);
    if (access == 0 || (access & (UINT32_C(1) << 16)) != 0) {
        return access;
    }
    /* A VM-entry guest TR must describe a busy 64-bit TSS.  Firmware may
     * leave TR=0, in which case descriptor preparation synthesizes an
     * available TSS descriptor because no architectural LTR was executed.
     * Normalize only the VMCS access-rights image to the required busy type. */
    return (access & ~UINT32_C(0x0f)) | UINT32_C(0x0b);
}

static int HvEfiPrepareControlRegisters(HV_EFI_CONTEXT *context,
                                        uint64_t vmxBasic,
                                        int enableApStartup)
{
    const int trueControls =
        (vmxBasic & HV_EFI_VMX_BASIC_TRUE_CONTROLS) != 0;
    uint64_t pinCapabilities;
    uint64_t cpuCapabilities;
    uint64_t secondaryCapabilities;
    uint64_t exitCapabilities;
    uint64_t entryCapabilities;
    uint32_t pinControls;
    uint32_t cpuControls;
    uint32_t secondaryControls;
    uint32_t exitControls;
    uint32_t entryControls;
    uint64_t cr0Fixed0;
    uint64_t cr0Fixed1;
    uint64_t cr4Fixed0;
    uint64_t cr4Fixed1;
    uint64_t normalizedCr0;
    uint64_t normalizedCr4;

    cr0Fixed0 = HvEfiReadMsr(HV_EFI_IA32_VMX_CR0_FIXED0);
    cr0Fixed1 = HvEfiReadMsr(HV_EFI_IA32_VMX_CR0_FIXED1);
    cr4Fixed0 = HvEfiReadMsr(HV_EFI_IA32_VMX_CR4_FIXED0);
    cr4Fixed1 = HvEfiReadMsr(HV_EFI_IA32_VMX_CR4_FIXED1);
    if (!HvEfiNormalizeControlRegister(context->GuestCr0, cr0Fixed0,
                                       cr0Fixed1, &normalizedCr0) ||
        !HvEfiNormalizeControlRegister(context->GuestCr4, cr4Fixed0,
                                       cr4Fixed1, &normalizedCr4)) {
        return 0;
    }
    /* Root's read-only scanner mappings rely on hardware write protection.
     * Firmware AP startup state is not required to match the BSP's CR0.WP,
     * so establish the invariant explicitly while leaving the guest image
     * unchanged. */
    if ((normalizedCr0 | HV_EFI_CR0_WP) & ~cr0Fixed1) {
        return 0;
    }
    context->HostCr0 = normalizedCr0 | HV_EFI_CR0_WP;
    context->HostCr4 = normalizedCr4;
    /* VMX fixed bits are also required by the guest-state checks.  Preserve
     * the originals separately for rollback if a later VMX step fails. */
    context->GuestCr0 = normalizedCr0;
    context->GuestCr4 = normalizedCr4;
    context->GuestCr0FixedMask = cr0Fixed0 &
        ~((UINT64_C(1) << 0) | (UINT64_C(1) << 31));
    context->GuestCr4FixedMask = cr4Fixed0;

    /* CR4.VMXE is bit 13 and must be present in the root-mode value. */
    if ((context->HostCr4 | (UINT64_C(1) << 13)) & ~cr4Fixed1) {
        return 0;
    }
    context->HostCr4 |= UINT64_C(1) << 13;
    if (context->GpuStatePhysical != 0 || context->GpuVmWcStatePhysical != 0) {
        const uint64_t simdBits = (UINT64_C(1) << 9) | (UINT64_C(1) << 10);
        if ((context->HostCr4 | simdBits) & ~cr4Fixed1) return 0;
        context->HostCr4 |= simdBits;
        context->HostCr0 &= ~UINT64_C(12); /* root EM/TS clear; guest unchanged */
    }

    pinCapabilities = HvEfiReadMsr(trueControls ?
        HV_EFI_IA32_VMX_TRUE_PINBASED_CTLS : HV_EFI_IA32_VMX_PINBASED_CTLS);
    cpuCapabilities = HvEfiReadMsr(trueControls ?
        HV_EFI_IA32_VMX_TRUE_PROCBASED_CTLS : HV_EFI_IA32_VMX_PROCBASED_CTLS);
    secondaryCapabilities = HvEfiReadMsr(HV_EFI_IA32_VMX_PROCBASED_CTLS2);
    exitCapabilities = HvEfiReadMsr(trueControls ?
        HV_EFI_IA32_VMX_TRUE_EXIT_CTLS : HV_EFI_IA32_VMX_EXIT_CTLS);
    entryCapabilities = HvEfiReadMsr(trueControls ?
        HV_EFI_IA32_VMX_TRUE_ENTRY_CTLS : HV_EFI_IA32_VMX_ENTRY_CTLS);

    /* CPUID and VMCALL exits are unconditional.  Several otherwise ordinary
     * instructions are gated by secondary controls in VMX non-root operation:
     * RDTSCP/RDPID, INVPCID, and XSAVES/XRSTORS.  CPUID is otherwise passed
     * through, so the VMCS must permit these host-advertised instructions. */
    if (!HvEfiSelectDirectInterruptPinControls(
            0, pinCapabilities, &pinControls) ||
        !HvEfiAdjustControls(HV_EFI_VM_CPU_USE_IO_BITMAPS |
                             HV_EFI_VM_CPU_USE_MSR_BITMAPS |
                             HV_EFI_VM_CPU_ACTIVATE_SECONDARY,
                             cpuCapabilities, &cpuControls) ||
        !HvEfiAdjustControls(HV_EFI_VM_SECONDARY_ENABLE_RDTSCP |
                             HV_EFI_VM_SECONDARY_ENABLE_VPID |
                             HV_EFI_VM_SECONDARY_ENABLE_EPT |
                             HV_EFI_VM_SECONDARY_ENABLE_INVPCID |
                             HV_EFI_VM_SECONDARY_ENABLE_XSAVES_XRSTORS |
                             (enableApStartup ?
                                  HV_EFI_VM_SECONDARY_UNRESTRICTED_GUEST :
                                  0),
                             secondaryCapabilities, &secondaryControls) ||
        !HvEfiAdjustControls(HV_EFI_VM_EXIT_HOST_ADDR_SPACE_SIZE |
                             HV_EFI_VM_EXIT_SAVE_PAT |
                             HV_EFI_VM_EXIT_LOAD_PAT |
                             HV_EFI_VM_EXIT_SAVE_EFER |
                             HV_EFI_VM_EXIT_LOAD_EFER,
                             exitCapabilities, &exitControls) ||
        !HvEfiAdjustControls(HV_EFI_VM_ENTRY_IA32E_MODE_GUEST |
                             HV_EFI_VM_ENTRY_LOAD_PAT |
                             HV_EFI_VM_ENTRY_LOAD_EFER,
                             entryCapabilities, &entryControls)) {
        return 0;
    }

    /* Only the explicitly selected VMware probe port is emulated.  Forced
     * unconditional I/O exiting would route ordinary device I/O into a
     * handler that deliberately rejects it. */
    if ((cpuControls & HV_EFI_VM_CPU_UNCOND_IO_EXITING) != 0 ||
        (cpuControls & HV_EFI_VM_CPU_USE_IO_BITMAPS) == 0) {
        return 0;
    }

    context->PinControls = pinControls;
    context->CpuControls = cpuControls;
    context->SecondaryControls = secondaryControls;
    context->ExitControls = exitControls;
    context->EntryControls = entryControls;
    return 1;
}

/* VMCS control fields are intentionally written in a separate phase.  VMX
 * instructions before VMXON/VMPTRLD are architecturally invalid. */
static int HvEfiWriteControls(HV_EFI_CONTEXT *context)
{
    int eptEnabled;
    int vpidEnabled;

    if (!HvEfiVmcsWritePhaseValid(context)) {
        return 0;
    }
    eptEnabled = (context->SecondaryControls &
                  HV_EFI_VM_SECONDARY_ENABLE_EPT) != 0;
    vpidEnabled = (context->SecondaryControls &
                   HV_EFI_VM_SECONDARY_ENABLE_VPID) != 0;
    if (vpidEnabled &&
        (context->Vpid == 0 ||
         !HvEfiWriteField(HV_EFI_VMCS_CTRL_VPID, context->Vpid))) {
        return 0;
    }
    if (eptEnabled &&
        (context->EptHierarchyReady == 0 || context->EptPointer == 0 ||
         !HvEfiWriteField(HV_EFI_VMCS_CTRL_EPT_POINTER,
                          context->EptPointer))) {
        return 0;
    }
    return HvEfiWriteField(HV_EFI_VMCS_CTRL_PIN_BASED, context->PinControls) &&
        HvEfiWriteField(HV_EFI_VMCS_CTRL_CPU_BASED, context->CpuControls) &&
        HvEfiWriteField(HV_EFI_VMCS_CTRL_SECONDARY,
                        context->SecondaryControls) &&
        HvEfiWriteField(HV_EFI_VMCS_CTRL_VM_EXIT, context->ExitControls) &&
        HvEfiWriteField(HV_EFI_VMCS_CTRL_VM_ENTRY, context->EntryControls) &&
        HvEfiWriteField(HV_EFI_VMCS_CTRL_IO_BITMAP_A,
                        context->PageTablePhysical +
                            (uint64_t)HV_EFI_IO_BITMAP_A_PAGE *
                                HV_EFI_PAGE_SIZE) &&
        HvEfiWriteField(HV_EFI_VMCS_CTRL_IO_BITMAP_B,
                        context->PageTablePhysical +
                            (uint64_t)HV_EFI_IO_BITMAP_B_PAGE *
                                HV_EFI_PAGE_SIZE) &&
        HvEfiWriteField(HV_EFI_VMCS_CTRL_MSR_BITMAP,
                        context->MsrBitmapPhysical) &&
        HvEfiWriteField(HV_EFI_VMCS_CTRL_CR0_MASK, 0) &&
        HvEfiWriteField(HV_EFI_VMCS_CTRL_CR4_MASK, 0) &&
        HvEfiWriteField(HV_EFI_VMCS_CTRL_CR0_READ_SHADOW, 0) &&
        HvEfiWriteField(HV_EFI_VMCS_CTRL_CR4_READ_SHADOW, 0) &&
        HvEfiWriteField(HV_EFI_VMCS_CTRL_EXCEPTION_BITMAP, 0) &&
        HvEfiWriteField(HV_EFI_VMCS_CTRL_PF_ERROR_CODE_MASK, 0) &&
        HvEfiWriteField(HV_EFI_VMCS_CTRL_PF_ERROR_CODE_MATCH, 0) &&
        HvEfiWriteField(HV_EFI_VMCS_CTRL_CR3_TARGET_COUNT, 0) &&
        HvEfiWriteField(HV_EFI_VMCS_CTRL_VM_EXIT_MSR_STORE_COUNT, 0) &&
        HvEfiWriteField(HV_EFI_VMCS_CTRL_VM_EXIT_MSR_LOAD_COUNT, 0) &&
        HvEfiWriteField(HV_EFI_VMCS_CTRL_VM_ENTRY_MSR_LOAD_COUNT, 0) &&
        HvEfiWriteField(HV_EFI_VMCS_CTRL_VM_ENTRY_INT_INFO, 0) &&
        HvEfiWriteField(HV_EFI_VMCS_CTRL_VM_ENTRY_EXCEPTION_ERR, 0) &&
        HvEfiWriteField(HV_EFI_VMCS_CTRL_VM_ENTRY_INSTR_LENGTH, 0);
}

static int HvEfiPrepareGuestState(HV_EFI_CONTEXT *context)
{
    const uint32_t esAccess = HvEfiSegmentAccess(context, context->Es);
    const uint32_t csAccess = HvEfiSegmentAccess(context, context->Cs);
    const uint32_t ssAccess = HvEfiSegmentAccess(context, context->Ss);
    const uint32_t dsAccess = HvEfiSegmentAccess(context, context->Ds);
    const uint32_t fsAccess = HvEfiSegmentAccess(context, context->Fs);
    const uint32_t gsAccess = HvEfiSegmentAccess(context, context->Gs);
    const uint32_t ldtrAccess = HvEfiSegmentAccess(context, context->Ldtr);
    const uint32_t trAccess = HvEfiGuestTrAccess(context);

    /* CS/SS/TR must be usable; nullable data segments are represented by the
     * unusable bit returned by HvEfiSegmentAccess above. */
    if ((context->Cs & UINT64_C(0xfff8)) == 0 ||
        (context->Ss & UINT64_C(0xfff8)) == 0 ||
        (context->Tr & UINT64_C(0xfff8)) == 0 || csAccess == 0 ||
        ssAccess == 0 ||
        trAccess == 0 || (csAccess & (UINT32_C(1) << 16)) != 0 ||
        (ssAccess & (UINT32_C(1) << 16)) != 0 ||
        (trAccess & (UINT32_C(1) << 16)) != 0) {
        return 0;
    }

    if (!HvEfiWriteField(HV_EFI_VMCS_GUEST_CR0, context->GuestCr0) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_CR3, context->GuestCr3) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_CR4, context->GuestCr4) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_ES_SELECTOR, context->Es) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_CS_SELECTOR, context->Cs) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_SS_SELECTOR, context->Ss) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_DS_SELECTOR, context->Ds) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_FS_SELECTOR, context->Fs) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_GS_SELECTOR, context->Gs) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_LDTR_SELECTOR, context->Ldtr) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_TR_SELECTOR, context->Tr) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_ES_BASE, 0) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_CS_BASE, 0) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_SS_BASE, 0) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_DS_BASE, 0) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_FS_BASE, context->FsBase) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_GS_BASE, context->GsBase) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_LDTR_BASE, 0) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_TR_BASE, context->TrBase) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_GDTR_BASE, context->GdtrBase) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_IDTR_BASE, context->IdtrBase) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_DR7, context->Dr7) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_RSP, context->GuestRsp) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_RIP, context->GuestBootstrapRip) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_RFLAGS, context->GuestRflags) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_PENDING_DEBUG, 0) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_SYSENTER_ESP,
                         context->SysenterEsp) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_SYSENTER_EIP,
                         context->SysenterEip) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_ES_LIMIT, context->GuestEsLimit) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_CS_LIMIT, context->GuestCsLimit) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_SS_LIMIT, context->GuestSsLimit) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_DS_LIMIT, context->GuestDsLimit) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_FS_LIMIT, context->GuestFsLimit) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_GS_LIMIT, context->GuestGsLimit) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_LDTR_LIMIT,
                         context->GuestLdtrLimit) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_TR_LIMIT, context->GuestTrLimit) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_GDTR_LIMIT, context->GdtrLimit) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_IDTR_LIMIT, context->IdtrLimit) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_ES_ACCESS, esAccess) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_CS_ACCESS, csAccess) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_SS_ACCESS, ssAccess) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_DS_ACCESS, dsAccess) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_FS_ACCESS, fsAccess) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_GS_ACCESS, gsAccess) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_LDTR_ACCESS, ldtrAccess) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_TR_ACCESS, trAccess) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_INTERRUPTIBILITY, 0) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_ACTIVITY, 0) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_SYSENTER_CS,
                         context->SysenterCs) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_LINK_POINTER, UINT64_MAX) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_PAT, context->Pat) ||
        !HvEfiWriteField(HV_EFI_VMCS_GUEST_EFER, context->Efer)) {
        return 0;
    }
    return 1;
}

static uint32_t HvEfiGuestStateActivationFailure(
    const HV_EFI_CONTEXT *context)
{
    const uint32_t csAccess = context != NULL ?
        HvEfiSegmentAccess(context, context->Cs) : 0;
    const uint32_t ssAccess = context != NULL ?
        HvEfiSegmentAccess(context, context->Ss) : 0;
    const uint32_t trAccess = HvEfiGuestTrAccess(context);

    if (context == NULL || (context->Cs & UINT64_C(0xfff8)) == 0 ||
        csAccess == 0 || (csAccess & (UINT32_C(1) << 16)) != 0) {
        return HV_EFI_AP_ACTIVATION_FAILURE_VMCS_GUEST_CS;
    }
    if ((context->Ss & UINT64_C(0xfff8)) == 0 || ssAccess == 0 ||
        (ssAccess & (UINT32_C(1) << 16)) != 0) {
        return HV_EFI_AP_ACTIVATION_FAILURE_VMCS_GUEST_SS;
    }
    if ((context->Tr & UINT64_C(0xfff8)) == 0 || trAccess == 0 ||
        (trAccess & (UINT32_C(1) << 16)) != 0) {
        return HV_EFI_AP_ACTIVATION_FAILURE_VMCS_GUEST_TR;
    }
    return HV_EFI_AP_ACTIVATION_FAILURE_NONE;
}

static int HvEfiPrepareHostState(const HV_EFI_CONTEXT *context)
{
    if (context == NULL || context->DescriptorStateReady == 0) {
        return 0;
    }
    return HvEfiWriteField(HV_EFI_VMCS_HOST_CR0, context->HostCr0) &&
        HvEfiWriteField(HV_EFI_VMCS_HOST_CR3, context->PageTablePhysical) &&
        HvEfiWriteField(HV_EFI_VMCS_HOST_CR4, context->HostCr4) &&
        HvEfiWriteField(HV_EFI_VMCS_HOST_FS_BASE, context->FsBase) &&
        HvEfiWriteField(HV_EFI_VMCS_HOST_GS_BASE, context->GsBase) &&
        HvEfiWriteField(HV_EFI_VMCS_HOST_TR_BASE, context->HostTrBase) &&
        HvEfiWriteField(HV_EFI_VMCS_HOST_GDTR_BASE, context->HostGdtrBase) &&
        HvEfiWriteField(HV_EFI_VMCS_HOST_IDTR_BASE, context->HostIdtrBase) &&
        HvEfiWriteField(HV_EFI_VMCS_HOST_SYSENTER_ESP,
                        context->SysenterEsp) &&
        HvEfiWriteField(HV_EFI_VMCS_HOST_SYSENTER_EIP,
                        context->SysenterEip) &&
        HvEfiWriteField(HV_EFI_VMCS_HOST_RSP, context->HostStackTop) &&
        HvEfiWriteField(HV_EFI_VMCS_HOST_RIP, context->ResidentHostRip) &&
        HvEfiWriteField(HV_EFI_VMCS_HOST_PAT,
            context->GpuHostPat != 0 ? context->GpuHostPat : context->Pat) &&
        HvEfiWriteField(HV_EFI_VMCS_HOST_EFER,
                        context->Efer | HV_EFI_EFER_NXE) &&
        HvEfiWriteField(HV_EFI_VMCS_HOST_SYSENTER_CS,
                        context->SysenterCs) &&
        HvEfiWriteField(HV_EFI_VMCS_HOST_ES_SELECTOR,
                        HV_EFI_HOST_DATA_SELECTOR) &&
        HvEfiWriteField(HV_EFI_VMCS_HOST_CS_SELECTOR,
                        HV_EFI_HOST_CODE_SELECTOR) &&
        HvEfiWriteField(HV_EFI_VMCS_HOST_SS_SELECTOR,
                        HV_EFI_HOST_DATA_SELECTOR) &&
        HvEfiWriteField(HV_EFI_VMCS_HOST_DS_SELECTOR,
                        HV_EFI_HOST_DATA_SELECTOR) &&
        HvEfiWriteField(HV_EFI_VMCS_HOST_FS_SELECTOR,
                        HV_EFI_HOST_DATA_SELECTOR) &&
        HvEfiWriteField(HV_EFI_VMCS_HOST_GS_SELECTOR,
                        HV_EFI_HOST_DATA_SELECTOR) &&
        HvEfiWriteField(HV_EFI_VMCS_HOST_TR_SELECTOR,
                        HV_EFI_HOST_TSS_SELECTOR);
}

/* Host-root descriptors never depend on the firmware processor's loaded TSS.
 * This is also the only descriptor package prepared for an AP before its
 * future INIT/SIPI guest-startup contract has been defined. */
static int HvEfiPrepareHostDescriptorState(
    HV_EFI_CONTEXT *context,
    EFI_PHYSICAL_ADDRESS descriptorPa,
    EFI_PHYSICAL_ADDRESS codePa)
{
    uint8_t *hostGdt;
    uint8_t *hostIdt;
    HV_EFI_TSS64 *hostTss;
    uint64_t fatalStub;
    uint64_t rootNmiStub;
    uint64_t rootUdStub;
    uint64_t rootGpStub;
    uint64_t rootPfStub;
    uint64_t rootMcStub;

    if (context == NULL ||
        !HvEfiRuntimePageAddressValid(descriptorPa) ||
        descriptorPa > HV_EFI_MAX_PHYSICAL_ADDRESS -
            ((HV_EFI_DESCRIPTOR_STORAGE_PAGES - 1u) * HV_EFI_PAGE_SIZE) ||
        context->HostStackTop == 0 ||
        !HvEfiIsCanonical(context->HostStackTop)) {
        return 0;
    }
    hostGdt = (uint8_t *)(uintptr_t)(descriptorPa +
        HV_EFI_DESCRIPTOR_HOST_GDT_PAGE * HV_EFI_PAGE_SIZE);
    hostIdt = (uint8_t *)(uintptr_t)(descriptorPa +
        HV_EFI_DESCRIPTOR_HOST_IDT_PAGE * HV_EFI_PAGE_SIZE);
    hostTss = (HV_EFI_TSS64 *)(uintptr_t)(descriptorPa +
        HV_EFI_DESCRIPTOR_HOST_TSS_PAGE * HV_EFI_PAGE_SIZE);
    HvEfiZero(hostGdt,
              (HV_EFI_DESCRIPTOR_STORAGE_PAGES -
               HV_EFI_DESCRIPTOR_HOST_GDT_PAGE) * HV_EFI_PAGE_SIZE);

    /* Host GDT: null, 64-bit code, flat data, and an available 64-bit TSS. */
    ((uint64_t *)(void *)hostGdt)[0] = 0;
    ((uint64_t *)(void *)hostGdt)[1] = UINT64_C(0x00af9a000000ffff);
    ((uint64_t *)(void *)hostGdt)[2] = UINT64_C(0x00cf92000000ffff);
    context->HostTssBase = descriptorPa +
        HV_EFI_DESCRIPTOR_HOST_TSS_PAGE * HV_EFI_PAGE_SIZE;
    if (!HvEfiBuildTssDescriptor(hostGdt, HV_EFI_HOST_GDT_LIMIT,
                                  HV_EFI_HOST_TSS_SELECTOR,
                                  context->HostTssBase,
                                  HV_EFI_TSS64_MIN_SIZE - 1)) {
        return 0;
    }

    fatalStub = codePa + ((uintptr_t)HvEfiResidentFatalInterrupt -
                           (uintptr_t)HvEfiResidentVmExitStart);
    rootNmiStub = codePa + ((uintptr_t)HvEfiResidentRootNmi -
                           (uintptr_t)HvEfiResidentVmExitStart);
    rootUdStub = codePa + ((uintptr_t)HvEfiResidentRootUd -
                          (uintptr_t)HvEfiResidentVmExitStart);
    rootGpStub = codePa + ((uintptr_t)HvEfiResidentRootGp -
                          (uintptr_t)HvEfiResidentVmExitStart);
    rootPfStub = codePa + ((uintptr_t)HvEfiResidentRootPf -
                          (uintptr_t)HvEfiResidentVmExitStart);
    rootMcStub = codePa + ((uintptr_t)HvEfiResidentRootMc -
                          (uintptr_t)HvEfiResidentVmExitStart);
    if (!HvEfiCanonicalRangeValid(fatalStub, 1) ||
        !HvEfiRewriteIdtGates(hostIdt, HV_EFI_HOST_IDT_LIMIT,
                              fatalStub, HV_EFI_HOST_CODE_SELECTOR, 1) ||
        !HvEfiBuildIdtGate(hostIdt, HV_EFI_HOST_IDT_LIMIT, 2,
                           rootNmiStub, HV_EFI_HOST_CODE_SELECTOR, 1) ||
        !HvEfiBuildIdtGate(hostIdt, HV_EFI_HOST_IDT_LIMIT, 6,
                           rootUdStub, HV_EFI_HOST_CODE_SELECTOR, 1) ||
        !HvEfiBuildIdtGate(hostIdt, HV_EFI_HOST_IDT_LIMIT, 13,
                           rootGpStub, HV_EFI_HOST_CODE_SELECTOR, 1) ||
        !HvEfiBuildIdtGate(hostIdt, HV_EFI_HOST_IDT_LIMIT, 14,
                           rootPfStub, HV_EFI_HOST_CODE_SELECTOR, 1) ||
        !HvEfiBuildIdtGate(hostIdt, HV_EFI_HOST_IDT_LIMIT, 18,
                           rootMcStub, HV_EFI_HOST_CODE_SELECTOR, 1) ||
        !HvEfiPatchTssStackStateWithIst(
            (uint8_t *)hostTss, (uint32_t)sizeof(*hostTss),
            context->HostStackTop,
            context->HostStackTop + (HV_EFI_PAGE_SIZE / 2u))) {
        return 0;
    }

    context->HostGdtrBase = descriptorPa +
        HV_EFI_DESCRIPTOR_HOST_GDT_PAGE * HV_EFI_PAGE_SIZE;
    context->HostIdtrBase = descriptorPa +
        HV_EFI_DESCRIPTOR_HOST_IDT_PAGE * HV_EFI_PAGE_SIZE;
    context->HostTrBase = context->HostTssBase;
    context->DescriptorStoragePhysical = descriptorPa;
    context->DescriptorStoragePages = HV_EFI_DESCRIPTOR_STORAGE_PAGES;
    context->HostTssSize = (uint32_t)sizeof(*hostTss);
    context->HostDescriptorStateReady = 1;
    return 1;
}

/* Copy every descriptor object that the bootstrap guest can expose through
 * its VMCS.  The firmware tables are only a source image: VMX guest and host
 * descriptor bases are switched to the low, runtime-resident copies below. */
static int HvEfiPrepareDescriptorState(HV_EFI_CONTEXT *context,
                                       EFI_PHYSICAL_ADDRESS descriptorPa,
                                       EFI_PHYSICAL_ADDRESS codePa)
{
    uint8_t *guestGdt;
    uint8_t *guestIdt;
    uint8_t *guestTss;
    const uint8_t *sourceTrDescriptor;
    uint64_t guestTssSource;
    uint64_t guestInterruptGuard;
    uint64_t storageLast;
    UINTN sourceGdtCopySize;
    UINTN guestIdtSize;
    UINTN guestTssSize;
    uint16_t copiedGdtrLimit;
    uint16_t sourceGdtrLimit;
    uint16_t synthesizedTrSelector = 0;
    int synthesizeGuestTss;
    const uint64_t *segmentSelectors;
    UINTN selectorIndex;
    static const UINTN segmentSelectorCount = 7;

    if (context == NULL ||
        !HvEfiRuntimePageAddressValid(descriptorPa) ||
        descriptorPa > HV_EFI_MAX_PHYSICAL_ADDRESS -
            ((HV_EFI_DESCRIPTOR_STORAGE_PAGES - 1u) * HV_EFI_PAGE_SIZE)) {
        return 0;
    }
    storageLast = descriptorPa +
        ((HV_EFI_DESCRIPTOR_STORAGE_PAGES - 1u) * HV_EFI_PAGE_SIZE);
    if (!HvEfiRuntimePageAddressValid(storageLast)) {
        return 0;
    }
    if (context->GdtrLimit > UINT16_MAX ||
        !HvEfiDescriptorTableLimitValid(context->IdtrLimit)) {
        return 0;
    }
    sourceGdtrLimit = (uint16_t)context->GdtrLimit;
    copiedGdtrLimit = sourceGdtrLimit;
    if (copiedGdtrLimit >= HV_EFI_DESCRIPTOR_TABLE_MAX_SIZE) {
        copiedGdtrLimit = HV_EFI_DESCRIPTOR_TABLE_MAX_SIZE - 1;
    }
    synthesizeGuestTss = context->Tr == 0;
    if (synthesizeGuestTss) {
        uint32_t nextSelector = ((uint32_t)sourceGdtrLimit + 8u) & ~7u;
        if (nextSelector == 0 ||
            nextSelector + 15u >= HV_EFI_DESCRIPTOR_TABLE_MAX_SIZE) {
            return 0;
        }
        synthesizedTrSelector = (uint16_t)nextSelector;
        if (copiedGdtrLimit < synthesizedTrSelector + 15u) {
            copiedGdtrLimit = (uint16_t)(synthesizedTrSelector + 15u);
        }
    }
    segmentSelectors = &context->Cs;
    for (selectorIndex = 0; selectorIndex < segmentSelectorCount;
         ++selectorIndex) {
        uint64_t selector = segmentSelectors[selectorIndex];
        if (selector > UINT16_MAX ||
            (selector != 0 && !HvEfiSelectorRangeValid(
                (uint16_t)selector, copiedGdtrLimit, 8))) {
            return 0;
        }
    }
    if (context->Cs == 0 ||
        !HvEfiCanonicalRangeValid(context->GdtrBase,
                                  (uint64_t)sourceGdtrLimit + 1) ||
        !HvEfiCanonicalRangeValid(context->IdtrBase,
                                  context->IdtrLimit + 1)) {
        return 0;
    }
    if (!synthesizeGuestTss &&
        (!HvEfiSelectorRangeValid((uint16_t)context->Tr,
                                  copiedGdtrLimit, 16) ||
         (context->Tr & UINT64_C(3)) != 0 ||
         context->GuestTrLimit + 1 < HV_EFI_TSS64_MIN_SIZE ||
         !HvEfiDescriptorTableLimitValid(context->GuestTrLimit))) {
        return 0;
    }

    guestTssSource = 0;
    guestTssSize = HV_EFI_TSS64_MIN_SIZE;
    if (!synthesizeGuestTss) {
        sourceTrDescriptor = (const uint8_t *)(uintptr_t)(context->GdtrBase +
            (context->Tr & UINT64_C(0xfff8)));
        if ((sourceTrDescriptor[5] & UINT8_C(0x10)) != 0 ||
            (sourceTrDescriptor[5] & UINT8_C(0x80)) == 0 ||
            ((sourceTrDescriptor[5] & UINT8_C(0x0f)) != UINT8_C(9) &&
             (sourceTrDescriptor[5] & UINT8_C(0x0f)) != UINT8_C(0xb))) {
            return 0;
        }
        guestTssSource = HvEfiDescriptorBase(context, context->Tr);
        guestTssSize = (UINTN)context->GuestTrLimit + 1;
        if (!HvEfiCanonicalRangeValid(guestTssSource, guestTssSize)) {
            return 0;
        }
    }

    sourceGdtCopySize = (UINTN)sourceGdtrLimit + 1;
    if (sourceGdtCopySize > HV_EFI_DESCRIPTOR_TABLE_MAX_SIZE) {
        sourceGdtCopySize = HV_EFI_DESCRIPTOR_TABLE_MAX_SIZE;
    }
    guestIdtSize = (UINTN)context->IdtrLimit + 1;
    guestGdt = (uint8_t *)(uintptr_t)(descriptorPa +
        HV_EFI_DESCRIPTOR_GUEST_GDT_PAGE * HV_EFI_PAGE_SIZE);
    guestIdt = (uint8_t *)(uintptr_t)(descriptorPa +
        HV_EFI_DESCRIPTOR_GUEST_IDT_PAGE * HV_EFI_PAGE_SIZE);
    guestTss = (uint8_t *)(uintptr_t)(descriptorPa +
        HV_EFI_DESCRIPTOR_GUEST_TSS_PAGE * HV_EFI_PAGE_SIZE);
    HvEfiZero((void *)(uintptr_t)descriptorPa,
              HV_EFI_DESCRIPTOR_STORAGE_PAGES * HV_EFI_PAGE_SIZE);
    HvEfiCopy(guestGdt, (const void *)(uintptr_t)context->GdtrBase,
              sourceGdtCopySize);
    HvEfiCopy(guestIdt, (const void *)(uintptr_t)context->IdtrBase,
              guestIdtSize);
    if (!synthesizeGuestTss) {
        HvEfiCopy(guestTss, (const void *)(uintptr_t)guestTssSource,
                  guestTssSize);
    }
    context->GdtrLimit = copiedGdtrLimit;

    guestInterruptGuard = context->GuestTransitionCodeBase +
        ((uintptr_t)HvEfiResidentGuestInterruptVectorStubs -
         (uintptr_t)HvEfiGuestTransitionStart);
    if (!HvEfiCanonicalRangeValid(
            guestInterruptGuard,
            HV_EFI_IDT_GATE_COUNT * HV_EFI_GUEST_INTERRUPT_STUB_SIZE) ||
        context->Cs > UINT16_MAX ||
        !HvEfiRewriteIdtVectorGates(
            guestIdt, (uint16_t)context->IdtrLimit, guestInterruptGuard,
            HV_EFI_GUEST_INTERRUPT_STUB_SIZE, (uint16_t)context->Cs,
            HV_EFI_GUEST_INTERRUPT_IST) ||
        !HvEfiPatchTssStackState(guestTss, (uint32_t)guestTssSize,
                                 context->GuestStackTop)) {
        return 0;
    }

    context->GuestTssBase = descriptorPa +
        HV_EFI_DESCRIPTOR_GUEST_TSS_PAGE * HV_EFI_PAGE_SIZE;
    if (synthesizeGuestTss) {
        context->Tr = synthesizedTrSelector;
        context->TrBase = context->GuestTssBase;
        context->GuestTrLimit = HV_EFI_TSS64_MIN_SIZE - 1;
        if (!HvEfiBuildTssDescriptor(
                guestGdt, copiedGdtrLimit, synthesizedTrSelector,
                context->GuestTssBase, context->GuestTrLimit)) {
            return 0;
        }
    } else {
        if (!HvEfiPatchTssDescriptor(guestGdt, copiedGdtrLimit,
                                     (uint16_t)context->Tr,
                                     context->GuestTssBase)) {
            return 0;
        }
    }
    if (!HvEfiPrepareHostDescriptorState(context, descriptorPa, codePa)) {
        return 0;
    }

    /* Keep the firmware guest descriptors active while Boot Services and the
     * loader still legitimately receive interrupts.  The bounded interrupt
     * probe arms these resident copies through a private VMCALL only after it
     * has disabled maskable interrupts.  A real OS may replace the firmware
     * guest descriptors through its normal LGDT/LIDT/LTR transition. */
    context->GuestTssSize = (uint32_t)guestTssSize;
    context->DescriptorStateReady = 1;
    (void)storageLast;
    return 1;
}

static int HvEfiPhysicalIommuAddressValid(
    const HV_EFI_PHYSICAL_IOMMU_STATE *state,
    uint64_t address,
    uint32_t width)
{
    uint32_t index;
    if (state == NULL || state->Signature != HV_EFI_PHYSICAL_IOMMU_SIGNATURE ||
        (width != 4 && width != 8) || (address & (width - 1)) != 0 ||
        address > UINT64_MAX - width) {
        return 0;
    }
    for (index = 0; index < state->Dmar.UnitCount; ++index) {
        const uint64_t base = state->Dmar.Units[index].RegisterBase;
        const uint64_t size = (uint64_t)HV_EFI_PHYSICAL_IOMMU_MMIO_PAGES *
            HV_EFI_PAGE_SIZE;
        if (address >= base && address + width <= base + size) {
            return 1;
        }
    }
    return 0;
}

static uint32_t HvEfiPhysicalIommuRead(
    void *callbackContext, uint64_t address, uint32_t width, uint64_t *value)
{
    HV_EFI_PHYSICAL_IOMMU_STATE *state =
        (HV_EFI_PHYSICAL_IOMMU_STATE *)callbackContext;
    if (value == NULL || !HvEfiPhysicalIommuAddressValid(
            state, address, width)) {
        return 0;
    }
    if (width == 4) {
        *value = *(volatile uint32_t *)(uintptr_t)address;
    } else {
        *value = *(volatile uint64_t *)(uintptr_t)address;
    }
    return 1;
}

static uint32_t HvEfiPhysicalIommuWrite(
    void *callbackContext, uint64_t address, uint32_t width, uint64_t value)
{
    HV_EFI_PHYSICAL_IOMMU_STATE *state =
        (HV_EFI_PHYSICAL_IOMMU_STATE *)callbackContext;
    if (!HvEfiPhysicalIommuAddressValid(state, address, width)) {
        return 0;
    }
    if (width == 4) {
        *(volatile uint32_t *)(uintptr_t)address = (uint32_t)value;
    } else {
        *(volatile uint64_t *)(uintptr_t)address = value;
    }
    return 1;
}

static int HvEfiPhysicalIommuPoll64Clear(
    HV_EFI_PHYSICAL_IOMMU_STATE *state, uint64_t address, uint64_t mask)
{
    uint32_t attempt;
    uint64_t value;
    for (attempt = 0; attempt < HV_EFI_PHYSICAL_IOMMU_POLL_LIMIT;
         ++attempt) {
        if (!HvEfiPhysicalIommuRead(state, address, 8, &value)) {
            return 0;
        }
        if ((value & mask) == 0) {
            return 1;
        }
    }
    return 0;
}

static uint32_t HvEfiPhysicalIommuInvalidate(
    void *callbackContext, uint32_t unitIndex, uint32_t flags)
{
    HV_EFI_PHYSICAL_IOMMU_STATE *state =
        (HV_EFI_PHYSICAL_IOMMU_STATE *)callbackContext;
    uint64_t base;
    uint64_t extendedCapability;
    uint64_t iotlbAddress;
    uint64_t completion;
    if (state == NULL || state->Signature != HV_EFI_PHYSICAL_IOMMU_SIGNATURE ||
        unitIndex >= state->Dmar.UnitCount ||
        (flags & ~(HV_EFI_IOMMU_INVALIDATE_CONTEXT |
                   HV_EFI_IOMMU_INVALIDATE_IOTLB |
                   HV_EFI_IOMMU_INVALIDATE_INTERRUPT_ENTRY)) != 0) {
        return 0;
    }
    base = state->Dmar.Units[unitIndex].RegisterBase;
    if ((flags & HV_EFI_IOMMU_INVALIDATE_CONTEXT) != 0) {
        if (!HvEfiPhysicalIommuWrite(
                state, base + HV_EFI_VTD_CCMD, 8,
                HV_EFI_VTD_CCMD_ICC | HV_EFI_VTD_CCMD_GLOBAL) ||
            !HvEfiPhysicalIommuPoll64Clear(
                state, base + HV_EFI_VTD_CCMD,
                HV_EFI_VTD_CCMD_ICC) ||
            !HvEfiPhysicalIommuRead(
                state, base + HV_EFI_VTD_CCMD, 8, &completion) ||
            (completion & (UINT64_C(3) << 59)) !=
                HV_EFI_VTD_CCMD_ACTUAL_GLOBAL) {
            return 0;
        }
    }
    if ((flags & HV_EFI_IOMMU_INVALIDATE_IOTLB) != 0) {
        if (!HvEfiPhysicalIommuRead(
                state, base + HV_EFI_VTD_ECAP, 8,
                &extendedCapability)) {
            return 0;
        }
        iotlbAddress = base +
            (((extendedCapability >> HV_EFI_VTD_ECAP_IRO_SHIFT) &
               HV_EFI_VTD_ECAP_IRO_MASK) << 4) + 8;
        if (!HvEfiPhysicalIommuWrite(
                state, iotlbAddress, 8,
                HV_EFI_VTD_IOTLB_IVT | HV_EFI_VTD_IOTLB_GLOBAL |
                HV_EFI_VTD_IOTLB_DRAIN_READS |
                HV_EFI_VTD_IOTLB_DRAIN_WRITES) ||
            !HvEfiPhysicalIommuPoll64Clear(
                state, iotlbAddress, HV_EFI_VTD_IOTLB_IVT) ||
            !HvEfiPhysicalIommuRead(
                state, iotlbAddress, 8, &completion) ||
            (completion & (UINT64_C(7) << 57)) !=
                HV_EFI_VTD_IOTLB_ACTUAL_GLOBAL) {
            return 0;
        }
    }
    /* This exact profile leaves interrupt remapping disabled and preserves
     * firmware/Windows direct interrupt delivery.  There is consequently no
     * interrupt-entry cache owned by this runtime to invalidate. */
    return 1;
}

static uint32_t HvEfiPhysicalIommuOwnershipHeld(
    void *callbackContext, uint64_t ownerToken)
{
    const HV_EFI_PHYSICAL_IOMMU_STATE *state =
        (const HV_EFI_PHYSICAL_IOMMU_STATE *)callbackContext;
    return state != NULL &&
        state->Signature == HV_EFI_PHYSICAL_IOMMU_SIGNATURE &&
        state->OwnerToken != 0 && state->OwnerToken == ownerToken;
}

static uint32_t HvEfiPhysicalIommuDmaQuiesced(void *callbackContext)
{
    const HV_EFI_PHYSICAL_IOMMU_STATE *state =
        (const HV_EFI_PHYSICAL_IOMMU_STATE *)callbackContext;
    return state != NULL && state->DmaQuiesced != 0;
}

static int HvEfiPhysicalEcamAddressValid(
    const HV_EFI_PHYSICAL_IOMMU_STATE *state,
    uint64_t address)
{
    uint32_t index;
    if (state == NULL || state->Signature != HV_EFI_PHYSICAL_IOMMU_SIGNATURE ||
        (address & 1) != 0) {
        return 0;
    }
    for (index = 0; index < state->McfgCount; ++index) {
        const HV_EFI_IOMMU_MCFG_SEGMENT *segment = &state->Mcfg[index];
        const uint64_t size =
            (uint64_t)(segment->EndBus - segment->StartBus + 1) << 20;
        if (address >= segment->EcamBase &&
            address + sizeof(uint16_t) <= segment->EcamBase + size &&
            (((address - segment->EcamBase) & UINT64_C(0xfff)) == 0 ||
             ((address - segment->EcamBase) & UINT64_C(0xfff)) == 4)) {
            return 1;
        }
    }
    return 0;
}

static uint32_t HvEfiPhysicalPciRead16(
    void *callbackContext, uint64_t address, uint16_t *value)
{
    HV_EFI_PHYSICAL_IOMMU_STATE *state =
        (HV_EFI_PHYSICAL_IOMMU_STATE *)callbackContext;
    if (value == NULL || !HvEfiPhysicalEcamAddressValid(state, address)) {
        return 0;
    }
    *value = *(volatile uint16_t *)(uintptr_t)address;
    return 1;
}

static uint32_t HvEfiPhysicalPciWrite16(
    void *callbackContext, uint64_t address, uint16_t value)
{
    HV_EFI_PHYSICAL_IOMMU_STATE *state =
        (HV_EFI_PHYSICAL_IOMMU_STATE *)callbackContext;
    if (!HvEfiPhysicalEcamAddressValid(state, address) ||
        (address & UINT64_C(0xfff)) != 4) {
        return 0;
    }
    *(volatile uint16_t *)(uintptr_t)address = value;
    return 1;
}

static int HvEfiInstallPhysicalAcpiShadow(HV_EFI_CONTEXT *context)
{
    if (context == NULL) {
        return 0;
    }
    if (context->DmaIsolationMode != HV_EFI_DMA_ISOLATION_VTD_MEDIATED) {
        return 1;
    }
    if (context->AcpiShadowPhysical == 0 ||
        context->AcpiGuestRsdpPage == 0 ||
        context->AcpiGuestXsdtPage == 0 ||
        HvEfiIommuInstallEptReadOnlyRemap(
            (uint64_t *)(uintptr_t)context->EptPhysical,
            HV_EFI_EPT_TABLE_PAGES, context->EptPhysical,
            context->AcpiGuestRsdpPage,
            context->AcpiShadowPhysical) != HV_EFI_IOMMU_RUNTIME_OK ||
        HvEfiIommuInstallEptReadOnlyRemap(
            (uint64_t *)(uintptr_t)context->EptPhysical,
            HV_EFI_EPT_TABLE_PAGES, context->EptPhysical,
            context->AcpiGuestXsdtPage,
            context->AcpiShadowPhysical + HV_EFI_PAGE_SIZE) !=
                HV_EFI_IOMMU_RUNTIME_OK) {
        return 0;
    }
    return 1;
}

static int HvEfiDmaMappableMemoryType(uint32_t type)
{
    return type == EfiLoaderCode || type == EfiLoaderData ||
        type == EfiBootServicesCode || type == EfiBootServicesData ||
        type == EfiRuntimeServicesCode || type == EfiRuntimeServicesData ||
        type == EfiConventionalMemory || type == EfiACPIReclaimMemory ||
        type == EfiACPIMemoryNVS || type == EfiPersistentMemory;
}

/* Only exact endpoint scopes are admitted for the dedicated profile. A
 * bridge/alias topology needs explicit expansion before it can be assigned. */
static int HvEfiGpuScopeBdf(const HV_EFI_PHYSICAL_IOMMU_STATE *state,
                             const HV_EFI_IOMMU_DMAR_SCOPE *scope,
                             uint32_t *bus, uint32_t *device, uint32_t *function)
{
    const HV_EFI_IOMMU_DEVICE_PATH_ELEMENT *path;
    if (scope->Segment != 0 || scope->Type != HV_EFI_IOMMU_SCOPE_PCI_ENDPOINT ||
        scope->PathElementCount != 1 ||
        scope->FirstPathElement >= state->Dmar.PathElementCount) return 0;
    path = &state->Dmar.PathElements[scope->FirstPathElement];
    *bus = scope->StartBus;
    *device = path->Device;
    *function = path->Function;
    return 1;
}

static int HvEfiGpuRmrrOwner(const HV_EFI_PHYSICAL_IOMMU_STATE *state,
                              const HV_EFI_IOMMU_DMAR_RMRR *rmrr)
{
    uint32_t i, gpu = 0, other = 0;
    for (i = 0; i < rmrr->ScopeCount; ++i) {
        uint32_t bus, device, function;
        const uint32_t index = rmrr->FirstScope + i;
        if (index >= state->Dmar.ScopeCount ||
            !HvEfiGpuScopeBdf(state, &state->Dmar.Scopes[index],
                              &bus, &device, &function)) return -1;
        if (bus == 0 && device == 2 && function == 0) ++gpu;
        else ++other;
    }
    return gpu != 0 ? (other != 0 ? -1 : 1) : 0;
}

static EFI_STATUS HvEfiGpuBuildDmaDomain(
    HV_EFI_CONTEXT *context, HV_EFI_PHYSICAL_IOMMU_STATE *state,
    const HV_EFI_IOMMU_PROTECTED_RANGE *mapped, uint32_t mappedCount,
    const HV_EFI_IOMMU_PROTECTED_RANGE *denied, uint32_t deniedCount)
{
    HV_GPU_NATIVE_STATE *gpu = (HV_GPU_NATIVE_STATE *)(uintptr_t)
        context->GpuStatePhysical;
    HV_EFI_IOMMU_UNIT units[HV_GPU_NATIVE_MAX_UNITS];
    HV_EFI_IOMMU_DEVICE *devices = NULL;
    HV_EFI_IOMMU_RANGE *guest = NULL, *deny = NULL, *normalGuest = NULL,
        *normalDeny = NULL;
    HV_EFI_IOMMU_RMRR *rmrrs = NULL;
    HV_EFI_IOMMU_DOMAIN_ASSIGNMENT *assignments = NULL;
    HV_EFI_IOMMU_IDENTITY_MAPPING *mappings = NULL;
    HV_EFI_IOMMU_TOPOLOGY topology;
    HV_EFI_IOMMU_PLAN_STORAGE storage;
    HV_EFI_IOMMU_PLAN plan;
    HV_EFI_IOMMU_ROOT_DEVICE_RANGE rootRanges[2];
    uint32_t i, deviceCount = 0, guestCount = 0, rmrrCount = 0;
    uint32_t gpuIndex = UINT32_MAX;
    uint64_t mappingCapacity;
    HV_EFI_IOMMU_STATUS plannerStatus;
    HV_EFI_IOMMU_RUNTIME_STATUS runtimeStatus;
    EFI_STATUS status = EFI_OUT_OF_RESOURCES;
#define GPU_POOL(pointer, count) \
    do { HvEfiBootDiagnosticSetIommuStep( \
             HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_ALLOCATE); \
         status = gBS->AllocatePool(EfiLoaderData, \
             (UINTN)(count) * sizeof(*(pointer)), (void **)&(pointer)); \
         if (EFI_ERROR(status)) \
        goto cleanup; } while (0)
    HvEfiBootDiagnosticSetIommuStep(
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_PROFILE);
    if (gpu == NULL || deniedCount > HV_EFI_EPT_MAX_DENY_RANGES ||
        state->McfgCount != 1 ||
        state->Mcfg[0].Segment != 0 || state->Mcfg[0].StartBus != 0 ||
        state->Dmar.UnitCount > HV_GPU_NATIVE_MAX_UNITS) return EFI_UNSUPPORTED;
    GPU_POOL(devices, HV_EFI_PHYSICAL_IOMMU_MAX_PCI_STATES);
    GPU_POOL(guest, mappedCount);
    GPU_POOL(deny, deniedCount);
    GPU_POOL(normalGuest, mappedCount);
    GPU_POOL(normalDeny, deniedCount);
    GPU_POOL(rmrrs, state->Dmar.ScopeCount + 1);
    GPU_POOL(assignments, HV_EFI_PHYSICAL_IOMMU_MAX_PCI_STATES);
    HvEfiZero(&topology, sizeof(topology));
    HvEfiZero(&storage, sizeof(storage));
    HvEfiZero(units, sizeof(units));
    for (i = 0; i < state->Dmar.UnitCount; ++i) {
        const HV_EFI_IOMMU_DMAR_UNIT *unit = &state->Dmar.Units[i];
        const uint64_t cap = *(volatile uint64_t *)(uintptr_t)(unit->RegisterBase + 8);
        const uint64_t ecap = *(volatile uint64_t *)(uintptr_t)(unit->RegisterBase + 16);
        uint32_t scope;
        HvEfiBootDiagnosticSetIommuStep(
            HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_UNIT_CAPABILITY);
        if (unit->Segment != 0 || ((cap >> 8) & 2) == 0) {
            status = EFI_UNSUPPORTED; goto cleanup;
        }
        HvEfiBootDiagnosticSetIommuStep(
            HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_UNIT_SCOPE);
        for (scope = 0; scope < unit->ScopeCount; ++scope) {
            const HV_EFI_IOMMU_DMAR_SCOPE *s =
                &state->Dmar.Scopes[unit->FirstScope + scope];
            if (s->Type == HV_EFI_IOMMU_SCOPE_PCI_SUB_HIERARCHY ||
                (s->Type == HV_EFI_IOMMU_SCOPE_PCI_ENDPOINT &&
                 s->PathElementCount != 1)) {
                status = EFI_UNSUPPORTED; goto cleanup;
            }
        }
        units[i].RegisterBase = unit->RegisterBase;
        units[i].Segment = 0;
        units[i].IncludeAll = (unit->Flags & HV_EFI_IOMMU_DRHD_INCLUDE_ALL) != 0;
        units[i].TranslationSupported = 1;
        units[i].FourKilobytePagesSupported = 1;
        units[i].AddressWidthBits = 39;
        units[i].QueuedInvalidationSupported = (uint32_t)((ecap >> 1) & 1);
        units[i].InterruptRemappingSupported = (uint32_t)((ecap >> 3) & 1);
        units[i].ExtendedInterruptModeSupported = (uint32_t)((ecap >> 4) & 1);
    }
    for (i = 0; i < ((uint32_t)state->Mcfg[0].EndBus + 1) * 256; ++i) {
        const uint32_t bus = i >> 8, device = (i >> 3) & 31, function = i & 7;
        const uint64_t address = state->Mcfg[0].EcamBase + ((uint64_t)i << 12);
        uint32_t unitIndex, selected = UINT32_MAX, fallback = UINT32_MAX;
        if (*(volatile uint16_t *)(uintptr_t)address == UINT16_MAX) continue;
        HvEfiBootDiagnosticSetIommuStep(
            HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_PCI_CAPACITY);
        if (deviceCount == HV_EFI_PHYSICAL_IOMMU_MAX_PCI_STATES) {
            status = EFI_OUT_OF_RESOURCES; goto cleanup;
        }
        HvEfiBootDiagnosticSetIommuStep(
            HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_PCI_OWNER);
        for (unitIndex = 0; unitIndex < state->Dmar.UnitCount; ++unitIndex) {
            const HV_EFI_IOMMU_DMAR_UNIT *unit = &state->Dmar.Units[unitIndex];
            uint32_t scope;
            if (units[unitIndex].IncludeAll != 0) fallback = unitIndex;
            for (scope = 0; scope < unit->ScopeCount; ++scope) {
                uint32_t sb, sd, sf;
                if (HvEfiGpuScopeBdf(state,
                        &state->Dmar.Scopes[unit->FirstScope + scope],
                        &sb, &sd, &sf) && sb == bus && sd == device && sf == function) {
                    if (selected != UINT32_MAX && selected != unitIndex) {
                        status = EFI_UNSUPPORTED; goto cleanup;
                    }
                    selected = unitIndex;
                }
            }
        }
        if (selected == UINT32_MAX) selected = fallback;
        if (selected == UINT32_MAX) { status = EFI_UNSUPPORTED; goto cleanup; }
        devices[deviceCount].Segment = 0;
        devices[deviceCount].Bus = bus;
        devices[deviceCount].Device = device;
        devices[deviceCount].Function = function;
        devices[deviceCount].Kind = bus == 0 && device == 2 && function == 0 ?
            HV_EFI_IOMMU_DEVICE_INTEGRATED : HV_EFI_IOMMU_DEVICE_DISCRETE;
        devices[deviceCount].DmaCapable = 1;
        devices[deviceCount].RequiresInterruptRemapping = 0;
        devices[deviceCount].RemappingUnitIndex = selected;
        if (bus == 0 && device == 2 && function == 0) gpuIndex = deviceCount;
        ++deviceCount;
    }
    HvEfiBootDiagnosticSetIommuStep(
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_NOT_FOUND);
    if (gpuIndex == UINT32_MAX) { status = EFI_UNSUPPORTED; goto cleanup; }
    /* Keep the root requester at index zero in the boot-only topology. */
    { HV_EFI_IOMMU_DEVICE swap = devices[0]; devices[0] = devices[gpuIndex];
      devices[gpuIndex] = swap; }
    for (i = 0; i < mappedCount; ++i) {
        if (mapped[i].Kind != HV_EFI_IOMMU_MAPPING_RAM) continue;
        guest[guestCount].PhysicalBase = mapped[i].PhysicalBase;
        guest[guestCount++].Size = mapped[i].Size;
    }
    for (i = 0; i < deniedCount; ++i) {
        deny[i].PhysicalBase = denied[i].PhysicalBase;
        deny[i].Size = denied[i].Size;
        gpu->PrivateRanges[i].PhysicalBase = denied[i].PhysicalBase;
        gpu->PrivateRanges[i].Size = denied[i].Size;
    }
    gpu->PrivateRangeCount = deniedCount;
    for (i = 0; i < state->Dmar.RmrrCount; ++i) {
        const HV_EFI_IOMMU_DMAR_RMRR *rmrr = &state->Dmar.Rmrrs[i];
        uint32_t scope;
        const int rootOwner = HvEfiGpuRmrrOwner(state, rmrr);
        HvEfiBootDiagnosticSetIommuStep(
            HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_RMRR_OWNER);
        if (rootOwner < 0) { status = EFI_UNSUPPORTED; goto cleanup; }
        if (rootOwner == 1) continue;
        for (scope = 0; scope < rmrr->ScopeCount; ++scope) {
            uint32_t bus, device, function, d;
            HvEfiBootDiagnosticSetIommuStep(
                HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_RMRR_SCOPE);
            if (!HvEfiGpuScopeBdf(state,
                    &state->Dmar.Scopes[rmrr->FirstScope + scope],
                    &bus, &device, &function)) {
                status = EFI_UNSUPPORTED; goto cleanup;
            }
            for (d = 1; d < deviceCount; ++d) {
                if (devices[d].Bus == bus && devices[d].Device == device &&
                    devices[d].Function == function) break;
            }
            HvEfiBootDiagnosticSetIommuStep(
                HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_RMRR_REQUESTER);
            if (d == deviceCount) { status = EFI_UNSUPPORTED; goto cleanup; }
            rmrrs[rmrrCount].PhysicalBase = rmrr->PhysicalBase;
            rmrrs[rmrrCount].Size = rmrr->Size;
            rmrrs[rmrrCount].DeviceIndex = d;
            rmrrs[rmrrCount++].Reserved = 0;
        }
    }
    mappingCapacity = ((uint64_t)guestCount + deniedCount + 1) * deviceCount + rmrrCount;
    HvEfiBootDiagnosticSetIommuStep(
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_MAPPING_CAPACITY);
    if (mappingCapacity == 0 || mappingCapacity > UINT32_MAX) {
        status = EFI_OUT_OF_RESOURCES; goto cleanup;
    }
    GPU_POOL(mappings, mappingCapacity);
    topology.Platform.DmarPresent = 1;
    topology.Platform.DeviceEnumerationComplete = 1;
    topology.Platform.DmaAddressBits = 39;
    topology.Units = units; topology.UnitCount = state->Dmar.UnitCount;
    topology.Devices = devices; topology.DeviceCount = deviceCount;
    topology.GuestRamRanges = guest; topology.GuestRamRangeCount = guestCount;
    topology.HypervisorDenyRanges = deny; topology.HypervisorDenyRangeCount = deniedCount;
    topology.RmrrRanges = rmrrs; topology.RmrrRangeCount = rmrrCount;
    storage.NormalizedGuestRamRanges = normalGuest; storage.NormalizedGuestRamCapacity = mappedCount;
    storage.NormalizedDenyRanges = normalDeny; storage.NormalizedDenyCapacity = deniedCount;
    storage.Assignments = assignments; storage.AssignmentCapacity = deviceCount;
    storage.Mappings = mappings; storage.MappingCapacity = (uint32_t)mappingCapacity;
    rootRanges[0].PhysicalBase = gpu->FramebufferPhysical;
    rootRanges[0].Size = gpu->FramebufferBytes;
    rootRanges[0].Kind = HV_EFI_IOMMU_ROOT_DEVICE_RANGE_FRAMEBUFFER;
    rootRanges[0].Access = HV_EFI_IOMMU_ROOT_DEVICE_ACCESS_READ;
    rootRanges[1].PhysicalBase = gpu->GgttPhysical;
    rootRanges[1].Size = gpu->GgttBytes;
    rootRanges[1].Kind = HV_EFI_IOMMU_ROOT_DEVICE_RANGE_TRANSLATION;
    rootRanges[1].Access = HV_EFI_IOMMU_ROOT_DEVICE_ACCESS_READ;
    status = EFI_UNSUPPORTED;
    HvEfiBootDiagnosticSetIommuStep(
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_IDENTITY_PLAN);
    plannerStatus = HvEfiIommuBuildIdentityPlan(&topology, &storage, &plan);
    if (plannerStatus != HV_EFI_IOMMU_OK) {
        HvEfiBootDiagnosticSetIommu(
            HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_IDENTITY_PLAN, 0,
            (uint32_t)plannerStatus, (uint32_t)plannerStatus);
        goto cleanup;
    }
    HvEfiBootDiagnosticSetIommuStep(
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_ROOT_PLAN);
    plannerStatus = HvEfiIommuBuildRootDevicePlan(
        &plan, 0, state->Tables.DomainId, deviceCount + 1,
        rootRanges, 2, &gpu->DmaPlan);
    if (plannerStatus != HV_EFI_IOMMU_OK) {
        HvEfiBootDiagnosticSetIommu(
            HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_ROOT_PLAN, 0,
            (uint32_t)plannerStatus, (uint32_t)plannerStatus);
        goto cleanup;
    }
    /* Planner DIDs 1..DeviceCount are reserved. Select their first unused
     * sibling, then verify that every unit sharing these context tables can
     * represent it. CAP.ND need not support all 65536 possible domain IDs. */
    HvEfiBootDiagnosticSetIommuStep(
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_DOMAIN_ID);
    for (i = 0; i < state->Dmar.UnitCount; ++i) {
        const uint64_t cap = *(volatile uint64_t *)(uintptr_t)
            (state->Dmar.Units[i].RegisterBase + 8);
        if (!HvEfiIommuDomainIdSupported(cap, gpu->DmaPlan.DomainId))
            goto cleanup;
    }
    HvEfiCopy(&gpu->LegacyTables, &state->Tables, sizeof(state->Tables));
    gpu->LegacyTablesPhysical = context->PhysicalIommuTablePhysical;
    gpu->LegacyTablePages = context->PhysicalIommuTablePages;
    HvEfiBootDiagnosticSetIommuStep(
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_TABLES);
    runtimeStatus = HvEfiIommuBuildRootDeviceDomain(
            (uint64_t *)(uintptr_t)context->PhysicalIommuTablePhysical,
            context->PhysicalIommuTablePages, context->PhysicalIommuTablePhysical,
            &state->Tables, (uint64_t *)(uintptr_t)gpu->DmaTablesPhysical,
            HV_GPU_NATIVE_DMA_PAGES, gpu->DmaTablesPhysical, &gpu->DmaPlan,
            &gpu->DmaTables);
    if (runtimeStatus != HV_EFI_IOMMU_RUNTIME_OK) {
        HvEfiBootDiagnosticSetIommu(
            HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_TABLES, 0,
            (uint32_t)runtimeStatus, (uint32_t)runtimeStatus);
        goto cleanup;
    }
    gpu->UnitCount = state->Dmar.UnitCount;
    for (i = 0; i < gpu->UnitCount; ++i) gpu->UnitBases[i] = state->Dmar.Units[i].RegisterBase;
    status = EFI_SUCCESS;
cleanup:
    if (mappings) gBS->FreePool(mappings);
    if (assignments) gBS->FreePool(assignments);
    if (rmrrs) gBS->FreePool(rmrrs);
    if (normalDeny) gBS->FreePool(normalDeny);
    if (normalGuest) gBS->FreePool(normalGuest);
    if (deny) gBS->FreePool(deny);
    if (guest) gBS->FreePool(guest);
    if (devices) gBS->FreePool(devices);
    return status;
#undef GPU_POOL
}

static EFI_STATUS HvEfiPreparePhysicalIommu(
    HV_EFI_CONTEXT *context,
    const EFI_MEMORY_DESCRIPTOR *memoryMap,
    UINTN memoryMapSize,
    UINTN descriptorSize,
    const HV_EFI_EPT_DENY_RANGE *eptDenyRanges,
    uint32_t eptDenyRangeCount)
{
    HV_EFI_PHYSICAL_IOMMU_STATE *state;
    HV_EFI_IOMMU_PROTECTED_RANGE *mapped = NULL;
    HV_EFI_IOMMU_PROTECTED_RANGE *denied = NULL;
    HV_EFI_IOMMU_RUNTIME_CALLBACKS callbacks;
    HV_EFI_IOMMU_PCI_CALLBACKS pciCallbacks;
    HV_EFI_IOMMU_PROGRAM_STORAGE programStorage;
    HV_EFI_IOMMU_OWNERSHIP_POLICY ownership;
    HV_EFI_IOMMU_INSTALL_DIAGNOSTIC installDiagnostic;
    uint32_t mappedCapacity;
    uint32_t mappedCount = 0;
    uint32_t index;
    HV_EFI_IOMMU_RUNTIME_STATUS iommuStatus;
    EFI_STATUS status;
    HvEfiBootDiagnosticSetIommu(
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_ARGUMENT, 0,
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_NOT_APPLICABLE,
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_NOT_APPLICABLE);
    if (context == NULL || memoryMap == NULL || descriptorSize <
            sizeof(EFI_MEMORY_DESCRIPTOR) || memoryMapSize < descriptorSize ||
        eptDenyRanges == NULL || eptDenyRangeCount == 0) {
        return EFI_INVALID_PARAMETER;
    }
    if (context->DmaIsolationMode != HV_EFI_DMA_ISOLATION_VTD_MEDIATED) {
        return EFI_SUCCESS;
    }
    state = (HV_EFI_PHYSICAL_IOMMU_STATE *)(uintptr_t)
        context->PhysicalIommuStatePhysical;
    HvEfiBootDiagnosticSetIommu(
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_STATE, 0,
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_NOT_APPLICABLE,
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_NOT_APPLICABLE);
    if (state == NULL || state->Signature != HV_EFI_PHYSICAL_IOMMU_SIGNATURE ||
        state->Enabled != 0 || state->Dmar.UnitCount == 0 ||
        state->McfgCount == 0 ||
        memoryMapSize / descriptorSize > UINT32_MAX - state->Dmar.RmrrCount) {
        return EFI_SECURITY_VIOLATION;
    }
    mappedCapacity = (uint32_t)(memoryMapSize / descriptorSize) +
        state->Dmar.RmrrCount;
    HvEfiBootDiagnosticSetIommu(
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_ALLOC_MAPPED, 0,
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_NOT_APPLICABLE,
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_NOT_APPLICABLE);
    status = gBS->AllocatePool(
        EfiLoaderData,
        (UINTN)mappedCapacity * sizeof(HV_EFI_IOMMU_PROTECTED_RANGE),
        (void **)&mapped);
    if (!EFI_ERROR(status)) {
        HvEfiBootDiagnosticSetIommu(
            HV_EFI_BOOT_DIAGNOSTIC_IOMMU_ALLOC_DENIED, 0,
            HV_EFI_BOOT_DIAGNOSTIC_IOMMU_NOT_APPLICABLE,
            HV_EFI_BOOT_DIAGNOSTIC_IOMMU_NOT_APPLICABLE);
        status = gBS->AllocatePool(
            EfiLoaderData,
            (UINTN)eptDenyRangeCount *
                sizeof(HV_EFI_IOMMU_PROTECTED_RANGE),
            (void **)&denied);
    }
    if (EFI_ERROR(status)) {
        if (mapped != NULL) {
            gBS->FreePool(mapped);
        }
        return status;
    }
    HvEfiBootDiagnosticSetIommu(
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_MEMORY_DESCRIPTOR, 0,
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_NOT_APPLICABLE,
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_NOT_APPLICABLE);
    for (index = 0; index < (uint32_t)(memoryMapSize / descriptorSize);
         ++index) {
        const EFI_MEMORY_DESCRIPTOR *descriptor =
            (const EFI_MEMORY_DESCRIPTOR *)(const void *)(
                (const uint8_t *)(const void *)memoryMap +
                (UINTN)index * descriptorSize);
        if (!HvEfiDmaMappableMemoryType(descriptor->Type)) {
            continue;
        }
        if (descriptor->NumberOfPages == 0 ||
            descriptor->NumberOfPages > UINT64_MAX / HV_EFI_PAGE_SIZE) {
            iommuStatus = HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT;
            HvEfiBootDiagnosticSetIommu(
                HV_EFI_BOOT_DIAGNOSTIC_IOMMU_MEMORY_DESCRIPTOR, 0,
                (uint32_t)iommuStatus, (uint32_t)iommuStatus);
            goto cleanup;
        }
        mapped[mappedCount].PhysicalBase = descriptor->PhysicalStart;
        mapped[mappedCount].Size =
            descriptor->NumberOfPages * HV_EFI_PAGE_SIZE;
        mapped[mappedCount].Kind = HV_EFI_IOMMU_MAPPING_RAM;
        mapped[mappedCount].Reserved = 0;
        ++mappedCount;
    }
    HvEfiBootDiagnosticSetIommu(
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_RMRR_OWNER, 0,
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_NOT_APPLICABLE,
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_NOT_APPLICABLE);
    for (index = 0; index < state->Dmar.RmrrCount; ++index) {
        if (context->GpuStatePhysical != 0) {
            const int owner = HvEfiGpuRmrrOwner(state, &state->Dmar.Rmrrs[index]);
            if (owner < 0) {
                iommuStatus = HV_EFI_IOMMU_RUNTIME_UNSUPPORTED_SCOPE;
                HvEfiBootDiagnosticSetIommu(
                    HV_EFI_BOOT_DIAGNOSTIC_IOMMU_RMRR_OWNER, 0,
                    (uint32_t)iommuStatus, (uint32_t)iommuStatus);
                goto cleanup;
            }
            if (owner == 1) continue;
        }
        mapped[mappedCount].PhysicalBase =
            state->Dmar.Rmrrs[index].PhysicalBase;
        mapped[mappedCount].Size = state->Dmar.Rmrrs[index].Size;
        mapped[mappedCount].Kind = HV_EFI_IOMMU_MAPPING_RMRR;
        mapped[mappedCount].Reserved = 0;
        ++mappedCount;
    }
    for (index = 0; index < eptDenyRangeCount; ++index) {
        denied[index].PhysicalBase = eptDenyRanges[index].PhysicalBase;
        denied[index].Size = eptDenyRanges[index].Size;
        denied[index].Kind = HV_EFI_IOMMU_PROTECTED_PRIVATE;
        denied[index].Reserved = 0;
    }
    HvEfiBootDiagnosticSetIommu(
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_IDENTITY_DOMAIN, 0,
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_NOT_APPLICABLE,
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_NOT_APPLICABLE);
    iommuStatus = HvEfiIommuBuildLegacyIdentityDomain(
        (uint64_t *)(uintptr_t)context->PhysicalIommuTablePhysical,
        context->PhysicalIommuTablePages,
        context->PhysicalIommuTablePhysical,
        mapped, mappedCount, denied, eptDenyRangeCount, 1, &state->Tables);
    if (iommuStatus != HV_EFI_IOMMU_RUNTIME_OK) {
        HvEfiBootDiagnosticSetIommu(
            HV_EFI_BOOT_DIAGNOSTIC_IOMMU_IDENTITY_DOMAIN, 0,
            (uint32_t)iommuStatus, (uint32_t)iommuStatus);
        goto cleanup;
    }
    if (context->GpuStatePhysical != 0) {
        HvEfiBootDiagnosticSetIommu(
            HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_DOMAIN, 0,
            HV_EFI_BOOT_DIAGNOSTIC_IOMMU_NOT_APPLICABLE,
            HV_EFI_BOOT_DIAGNOSTIC_IOMMU_NOT_APPLICABLE);
        status = HvEfiGpuBuildDmaDomain(
            context, state, mapped, mappedCount, denied, eptDenyRangeCount);
        if (EFI_ERROR(status)) {
            iommuStatus = HV_EFI_IOMMU_RUNTIME_AUDIT_FAILED;
            goto cleanup;
        }
    }
    callbacks.Context = state;
    callbacks.Read = HvEfiPhysicalIommuRead;
    callbacks.Write = HvEfiPhysicalIommuWrite;
    callbacks.Invalidate = HvEfiPhysicalIommuInvalidate;
    callbacks.OwnershipHeld = HvEfiPhysicalIommuOwnershipHeld;
    callbacks.DmaQuiesced = HvEfiPhysicalIommuDmaQuiesced;
    pciCallbacks.Context = state;
    pciCallbacks.Read16 = HvEfiPhysicalPciRead16;
    pciCallbacks.Write16 = HvEfiPhysicalPciWrite16;
    for (index = 0; index < state->Dmar.UnitCount; ++index) {
        state->Configurations[index].RootTablePhysical =
            state->Tables.RootTablePhysical;
        state->Configurations[index].InvalidationQueuePhysical = 0;
        state->Configurations[index].InterruptTablePhysical = 0;
        state->Configurations[index].InterruptEntryCount = 0;
        state->Configurations[index].AddressBits = 39;
        state->Configurations[index].EnableQueuedInvalidation = 0;
        state->Configurations[index].EnableInterruptRemapping = 0;
        state->Configurations[index].EnableExtendedInterruptMode = 0;
        state->Configurations[index].Reserved = 0;
    }
    HvEfiZero(&ownership, sizeof(ownership));
    ownership.Mode = HV_EFI_IOMMU_OWNERSHIP_EPT_TRAPPED;
    ownership.DmarMmioGuestDenied = 1;
    ownership.RemappingTablesGuestDenied = 1;
    ownership.RemappingTablesDmaDenied = 1;
    ownership.ContinuousAuditEnabled = 1;
    ownership.RequireDmaQuiesce = 1;
    ownership.OwnerToken = state->OwnerToken;
    programStorage.Units = state->Programs;
    programStorage.UnitCapacity = HV_EFI_PHYSICAL_IOMMU_MAX_UNITS;
    programStorage.Operations = state->Operations;
    programStorage.OperationCapacity =
        HV_EFI_PHYSICAL_IOMMU_MAX_OPERATIONS;
    HvEfiBootDiagnosticSetIommu(
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_RUNTIME_INSTALL, 0,
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_NOT_APPLICABLE,
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_NOT_APPLICABLE);
    iommuStatus = HvEfiIommuInstallRuntimeIsolationWithDiagnostic(
        &state->Dmar, state->Configurations, state->Dmar.UnitCount,
        &ownership, &callbacks, &programStorage,
        HV_EFI_PHYSICAL_IOMMU_POLL_LIMIT, &state->Plan, &state->Runtime,
        state->UnitStates, HV_EFI_PHYSICAL_IOMMU_MAX_UNITS,
        state->Mcfg, state->McfgCount, &pciCallbacks, state->PciStates,
        HV_EFI_PHYSICAL_IOMMU_MAX_PCI_STATES, &state->PciStateCount,
        &state->DmaQuiesced, &installDiagnostic);
    HvEfiBootDiagnosticSetIommu(
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_RUNTIME_INSTALL, installDiagnostic.Stage,
        installDiagnostic.PrimaryStatus, installDiagnostic.FinalStatus);
    if (installDiagnostic.Stage == HV_EFI_IOMMU_INSTALL_STAGE_PROGRAM_PLAN &&
        installDiagnostic.PrimaryStatus ==
            HV_EFI_IOMMU_RUNTIME_REMAPPING_UNSUPPORTED &&
        installDiagnostic.CapabilityFailure !=
            HV_EFI_IOMMU_CAPABILITY_FAILURE_NONE) {
        HvEfiBootDiagnosticSetDetail(
            hv_efi_boot_diagnostic_iommu_capability_detail(
                installDiagnostic.UnitIndex, installDiagnostic.CapabilityFailure,
                installDiagnostic.RequestedAddressBits, installDiagnostic.Version,
                installDiagnostic.Capability, installDiagnostic.FinalStatus));
    }
    if (iommuStatus != HV_EFI_IOMMU_RUNTIME_OK) {
        /* Never return into generic allocation cleanup while a unit may
         * still reference resident tables that cleanup would release. */
        if (state->Runtime.State == HV_EFI_IOMMU_RUNTIME_ENABLED ||
            state->Runtime.State == HV_EFI_IOMMU_RUNTIME_ENABLING ||
            state->Runtime.State == HV_EFI_IOMMU_RUNTIME_FAILED) {
            HvEfiFailStop();
        }
        goto cleanup;
    }
    state->Enabled = 1;
    state->DetachRemaining = context->EnabledProcessorCount;
    context->DmaIsolationState = HV_EFI_IOMMU_RUNTIME_ENABLED;

cleanup:
    gBS->FreePool(denied);
    gBS->FreePool(mapped);
    return EFI_ERROR(status) ? status :
        iommuStatus == HV_EFI_IOMMU_RUNTIME_OK ?
        EFI_SUCCESS : EFI_SECURITY_VIOLATION;
}

typedef struct HV_EFI_AP_PREPARATION_CONTEXT {
    HV_EFI_MP_SERVICES_PROTOCOL *MpServices;
    HV_EFI_CONTEXT *RootContext;
    const uint8_t *EptMemoryTypes;
    uint32_t EptMemoryTypeCount;
    uint32_t EptWriteBackLeafCount;
    const HV_EFI_EPT_DENY_RANGE *EptDenyRanges;
    uint32_t EptDenyRangeCount;
    volatile uint32_t PreparedCount;
    volatile uint32_t FailureCount;
    volatile uint32_t FailureStage;
    volatile uint64_t FailureSecondaryCapabilities;
    volatile uint64_t FailureEptVpidCapabilities;
} HV_EFI_AP_PREPARATION_CONTEXT;

static int HvEfiAppendEptDenyRange(HV_EFI_EPT_DENY_RANGE *ranges,
                                   uint32_t capacity,
                                   uint32_t *count,
                                   uint64_t physicalBase,
                                   uint32_t pages)
{
    if (ranges == NULL || count == NULL || *count >= capacity ||
        physicalBase == 0 || pages == 0) {
        return 0;
    }
    ranges[*count].PhysicalBase = physicalBase;
    ranges[*count].Size = (uint64_t)pages * HV_EFI_PAGE_SIZE;
    ++*count;
    return 1;
}

static int HvEfiBuildEptDenyInventory(
    const HV_EFI_CONTEXT *context,
    HV_EFI_EPT_DENY_RANGE *ranges,
    uint32_t capacity,
    uint32_t *rangeCount)
{
    /* GuestStackPhysical is intentionally not in this inventory.  The
     * bootstrap stub executes in non-root mode on that transient stack, so
     * denying it would fault the first guest push before the VMCALL handoff.
     * It carries no root pointer or control secret and is released on the
     * pre-launch rollback path like the other processor allocations.
     *
     * Descriptor storage is one allocation for deterministic rollback, but
     * only its host GDT/IDT/TSS suffix is root state.  The guest prefix backs
     * live guest VMCS descriptor bases and must remain guest-accessible. */
    static const uint32_t resourcePages[] = {
        HV_EFI_CONTEXT_PAGES,
        HV_EFI_VMXON_PAGES,
        HV_EFI_VMCS_PAGES,
        HV_EFI_HOST_STACK_PAGES,
        HV_EFI_PAGE_TABLE_PAGES,
        HV_EFI_HOST_DESCRIPTOR_STORAGE_PAGES,
        HV_EFI_EPT_TABLE_PAGES,
        HV_EFI_EPT_TABLE_PAGES,
        HV_EFI_SHADOW_EPT_STATE_PAGES
    };
    HV_EFI_PROCESSOR_RECORD *records;
    uint32_t count = 0;
    uint32_t boundCount = 0;
    uint32_t processorNumber;
    uint32_t index;

    if (context == NULL || ranges == NULL || rangeCount == NULL ||
        context->ProcessorRecordsPhysical == 0 ||
        context->ProcessorRecordPages == 0 ||
        context->ProcessorRecordCount == 0 ||
        context->ProcessorRecordCount > HV_EFI_MAX_SUPPORTED_PROCESSORS ||
        context->ResidentCodeBase == 0) {
        return 0;
    }
    if (!HvEfiAppendEptDenyRange(
            ranges, capacity, &count, context->ResidentCodeBase,
            HV_EFI_RESIDENT_CODE_PAGES) ||
        !HvEfiAppendEptDenyRange(
            ranges, capacity, &count, context->RuntimeScannerCodeBase,
            context->RuntimeScannerCodePages) ||
        !HvEfiAppendEptDenyRange(
            ranges, capacity, &count,
            context->RuntimeScannerStatePhysical,
            HV_EFI_RUNTIME_SCANNER_STATE_PAGES) ||
        !HvEfiAppendEptDenyRange(
            ranges, capacity, &count,
            context->RuntimeAnalysisStatePhysical,
            context->RuntimeAnalysisStatePages) ||
        !HvEfiAppendEptDenyRange(
            ranges, capacity, &count,
            context->RuntimeScannerRamRangesPhysical,
            context->RuntimeScannerRamRangePages) ||
        !HvEfiAppendEptDenyRange(
            ranges, capacity, &count,
            context->ProcessorRecordsPhysical,
            context->ProcessorRecordPages)) {
        return 0;
    }
    records = (HV_EFI_PROCESSOR_RECORD *)(uintptr_t)
        context->ProcessorRecordsPhysical;
    for (processorNumber = 0;
         processorNumber < context->ProcessorRecordCount;
         ++processorNumber) {
        const HV_EFI_PROCESSOR_RECORD *record = &records[processorNumber];
        const uint64_t resources[] = {
            record->ContextPhysical,
            record->VmxonPhysical,
            record->VmcsPhysical,
            record->HostStackPhysical,
            record->PageTablePhysical,
            record->DescriptorStoragePhysical +
                HV_EFI_DESCRIPTOR_HOST_GDT_PAGE * HV_EFI_PAGE_SIZE,
            record->EptPhysical,
            record->ShadowEptPhysical,
            record->ShadowEptStatePhysical
        };

        if ((record->StateFlags &
             HV_EFI_PROCESSOR_STATE_RESOURCES_BOUND) == 0) {
            continue;
        }
        if (!HvEfiProcessorResourceAddressesValid(record)) {
            return 0;
        }
        for (index = 0;
             index < sizeof(resources) / sizeof(resources[0]);
             ++index) {
            if (!HvEfiAppendEptDenyRange(
                    ranges, capacity, &count,
                    resources[index], resourcePages[index])) {
                return 0;
            }
        }
        ++boundCount;
    }
    if (boundCount != context->ResourceBoundProcessorCount ||
        boundCount != context->EnabledProcessorCount) {
        return 0;
    }
    if (context->DmaIsolationMode == HV_EFI_DMA_ISOLATION_VTD_MEDIATED) {
        const HV_EFI_PHYSICAL_IOMMU_STATE *physicalState =
            (const HV_EFI_PHYSICAL_IOMMU_STATE *)(uintptr_t)
                context->PhysicalIommuStatePhysical;
        uint64_t registerBases[HV_EFI_PHYSICAL_IOMMU_MAX_UNITS];
        HV_EFI_EPT_DENY_RANGE mmioRanges[HV_EFI_PHYSICAL_IOMMU_MAX_UNITS];
        uint32_t mmioRangeCount = 0;
        if (physicalState == NULL ||
            physicalState->Signature != HV_EFI_PHYSICAL_IOMMU_SIGNATURE ||
            physicalState->Dmar.UnitCount == 0 ||
            physicalState->Dmar.UnitCount >
                HV_EFI_PHYSICAL_IOMMU_MAX_UNITS ||
            !HvEfiAppendEptDenyRange(
                ranges, capacity, &count,
                context->PhysicalIommuStatePhysical,
                context->PhysicalIommuStatePages) ||
            !HvEfiAppendEptDenyRange(
                ranges, capacity, &count,
                context->PhysicalIommuTablePhysical,
                context->PhysicalIommuTablePages) ||
            !HvEfiAppendEptDenyRange(
                ranges, capacity, &count, context->AcpiShadowPhysical, 2) ||
            !HvEfiAppendEptDenyRange(
                ranges, capacity, &count, context->AcpiGuestRsdpPage, 1) ||
            !HvEfiAppendEptDenyRange(
                ranges, capacity, &count, context->AcpiGuestXsdtPage, 1)) {
            return 0;
        }
        for (index = 0; index < physicalState->Dmar.UnitCount; ++index) {
            registerBases[index] = physicalState->Dmar.Units[index].RegisterBase;
        }
        /* Firmware units may be one page apart while our conservative MMIO
         * windows cover four pages each. Union those windows before combining
         * them with root-owned allocations; the final ownership overlap check
         * remains strict for every other range. */
        if (!HvEfiBuildEptMmioDenyRanges(
                registerBases, physicalState->Dmar.UnitCount,
                HV_EFI_PHYSICAL_IOMMU_MMIO_PAGES, mmioRanges,
                HV_EFI_PHYSICAL_IOMMU_MAX_UNITS, &mmioRangeCount)) {
            return 0;
        }
        for (index = 0; index < mmioRangeCount; ++index) {
            if (!HvEfiAppendEptDenyRange(
                    ranges, capacity, &count,
                    mmioRanges[index].PhysicalBase,
                    (uint32_t)(mmioRanges[index].Size / HV_EFI_PAGE_SIZE))) {
                return 0;
            }
        }
    }
    if (context->GpuStatePhysical != 0) {
        const HV_GPU_NATIVE_STATE *gpu = (const HV_GPU_NATIVE_STATE *)(uintptr_t)
            context->GpuStatePhysical;
        const uint64_t hostBridge = gpu->Filter.EcamPage -
            ((uint64_t)gpu->Filter.Bus << 20) -
            ((uint64_t)gpu->Filter.Device << 15) -
            ((uint64_t)gpu->Filter.Function << 12);
        if (gpu->Marker != HV_GPU_NATIVE_MARKER ||
            !HvEfiAppendEptDenyRange(ranges, capacity, &count,
                gpu->SelfPhysical, gpu->SelfPages) ||
            !HvEfiAppendEptDenyRange(ranges, capacity, &count,
                gpu->FramebufferPhysical, (uint32_t)(gpu->FramebufferBytes / 4096)) ||
            !HvEfiAppendEptDenyRange(ranges, capacity, &count,
                gpu->AbsentConfigPhysical, 1) ||
            !HvEfiAppendEptDenyRange(ranges, capacity, &count,
                gpu->DmaTablesPhysical, HV_GPU_NATIVE_DMA_PAGES) ||
            !HvEfiAppendEptDenyRange(ranges, capacity, &count,
                gpu->GgttPhysical, (uint32_t)(gpu->GgttBytes / 4096)) ||
            !HvEfiAppendEptDenyRange(ranges, capacity, &count,
                gpu->Filter.Bar0.Base, (uint32_t)(gpu->Filter.Bar0.Bytes / 4096)) ||
            !HvEfiAppendEptDenyRange(ranges, capacity, &count,
                gpu->Filter.Bar2.Base, (uint32_t)(gpu->Filter.Bar2.Bytes / 4096)) ||
            !HvEfiAppendEptDenyRange(ranges, capacity, &count,
                gpu->Filter.EcamPage, 1) ||
            !HvEfiAppendEptDenyRange(ranges, capacity, &count, hostBridge, 1))
            return 0;
    }
    if (context->GpuVmWcStatePhysical != 0) {
        const HV_GPU_NATIVE_STATE *test = (const HV_GPU_NATIVE_STATE *)(uintptr_t)
            context->GpuVmWcStatePhysical;
        if (!HvEfiAppendEptDenyRange(ranges, capacity, &count,
                test->SelfPhysical, test->SelfPages) ||
            !HvEfiAppendEptDenyRange(ranges, capacity, &count,
                test->FramebufferPhysical, 1)) return 0;
    }
    for (index = 1; index < count; ++index) {
        HV_EFI_EPT_DENY_RANGE value = ranges[index];
        uint32_t position = index;
        while (position != 0 &&
               ranges[position - 1].PhysicalBase > value.PhysicalBase) {
            ranges[position] = ranges[position - 1];
            --position;
        }
        ranges[position] = value;
    }
    for (index = 1; index < count; ++index) {
        if (ranges[index - 1].PhysicalBase + ranges[index - 1].Size >
            ranges[index].PhysicalBase) {
            return 0;
        }
    }
    *rangeCount = count;
    return 1;
}

static int HvEfiScannerRamMemoryType(uint32_t type)
{
    return type == EfiLoaderCode || type == EfiLoaderData ||
        type == EfiBootServicesCode || type == EfiBootServicesData ||
        type == EfiRuntimeServicesCode || type == EfiRuntimeServicesData ||
        type == EfiConventionalMemory || type == EfiACPIReclaimMemory ||
        type == EfiACPIMemoryNVS || type == EfiPersistentMemory;
}

static int HvEfiAppendScannerRamRange(
    HV_EFI_SCANNER_RAM_RANGE *ranges,
    uint32_t capacity,
    uint32_t *count,
    uint64_t start,
    uint64_t end)
{
    if (ranges == NULL || count == NULL || start >= end ||
        (start & HV_EFI_SCANNER_PAGE_MASK) != 0 ||
        (end & HV_EFI_SCANNER_PAGE_MASK) != 0 || *count >= capacity) {
        return 0;
    }
    ranges[*count].PhysicalBase = start;
    ranges[*count].Size = end - start;
    ++*count;
    return 1;
}

/* Persist guest-visible RAM only. Root-private EPT deny ranges are subtracted
 * so an authenticated scan cannot be used as an oracle over runtime tokens,
 * VMCS state, scanner results, or other hypervisor-owned storage. */
static int HvEfiBuildRuntimeScannerAllowlist(
    HV_EFI_CONTEXT *context,
    const EFI_MEMORY_DESCRIPTOR *memoryMap,
    UINTN memoryMapSize,
    UINTN descriptorSize,
    const HV_EFI_EPT_DENY_RANGE *denied,
    uint32_t deniedCount)
{
    HV_EFI_SCANNER_RAM_RANGE *ranges;
    uint32_t count = 0;
    uint32_t descriptorCount;
    uint32_t descriptorIndex;

    if (context == NULL || memoryMap == NULL ||
        descriptorSize < sizeof(EFI_MEMORY_DESCRIPTOR) ||
        memoryMapSize < descriptorSize || denied == NULL || deniedCount == 0 ||
        context->RuntimeScannerRamRangesPhysical == 0 ||
        context->RuntimeScannerRamRangePages !=
            HV_EFI_RUNTIME_SCANNER_RANGE_PAGES) {
        return 0;
    }
    ranges = (HV_EFI_SCANNER_RAM_RANGE *)(uintptr_t)
        context->RuntimeScannerRamRangesPhysical;
    HvEfiZero(ranges,
        HV_EFI_RUNTIME_SCANNER_RANGE_PAGES * HV_EFI_PAGE_SIZE);
    descriptorCount = (uint32_t)(memoryMapSize / descriptorSize);
    for (descriptorIndex = 0;
         descriptorIndex < descriptorCount;
         ++descriptorIndex) {
        const EFI_MEMORY_DESCRIPTOR *descriptor =
            (const EFI_MEMORY_DESCRIPTOR *)(const void *)(
                (const uint8_t *)(const void *)memoryMap +
                (UINTN)descriptorIndex * descriptorSize);
        uint64_t cursor;
        uint64_t end;
        uint32_t denyIndex;
        if (!HvEfiScannerRamMemoryType(descriptor->Type) ||
            descriptor->NumberOfPages == 0 ||
            descriptor->NumberOfPages >
                HV_EFI_SCANNER_PHYSICAL_LIMIT / HV_EFI_PAGE_SIZE) {
            continue;
        }
        cursor = descriptor->PhysicalStart;
        if (cursor >= HV_EFI_SCANNER_PHYSICAL_LIMIT ||
            descriptor->NumberOfPages >
                (HV_EFI_SCANNER_PHYSICAL_LIMIT - cursor) /
                    HV_EFI_PAGE_SIZE) {
            continue;
        }
        end = cursor + descriptor->NumberOfPages * HV_EFI_PAGE_SIZE;
        for (denyIndex = 0; denyIndex < deniedCount; ++denyIndex) {
            uint64_t denyEnd;
            if (denied[denyIndex].Size == 0 ||
                denied[denyIndex].PhysicalBase >
                    UINT64_MAX - denied[denyIndex].Size) {
                return 0;
            }
            denyEnd = denied[denyIndex].PhysicalBase +
                denied[denyIndex].Size;
            if (denyEnd <= cursor) {
                continue;
            }
            if (denied[denyIndex].PhysicalBase >= end) {
                break;
            }
            if (cursor < denied[denyIndex].PhysicalBase &&
                !HvEfiAppendScannerRamRange(
                    ranges, HV_EFI_RUNTIME_SCANNER_MAX_RAM_RANGES,
                    &count, cursor, denied[denyIndex].PhysicalBase)) {
                return 0;
            }
            if (cursor < denyEnd) {
                cursor = denyEnd;
            }
            if (cursor >= end) {
                break;
            }
        }
        if (cursor < end && !HvEfiAppendScannerRamRange(
                ranges, HV_EFI_RUNTIME_SCANNER_MAX_RAM_RANGES,
                &count, cursor, end)) {
            return 0;
        }
    }
    if (count == 0) {
        return 0;
    }
    for (descriptorIndex = 1; descriptorIndex < count; ++descriptorIndex) {
        HV_EFI_SCANNER_RAM_RANGE value = ranges[descriptorIndex];
        uint32_t position = descriptorIndex;
        while (position != 0 &&
               ranges[position - 1].PhysicalBase > value.PhysicalBase) {
            ranges[position] = ranges[position - 1];
            --position;
        }
        ranges[position] = value;
    }
    {
        uint32_t mergedCount = 1;
        for (descriptorIndex = 1; descriptorIndex < count;
             ++descriptorIndex) {
            HV_EFI_SCANNER_RAM_RANGE *previous = &ranges[mergedCount - 1];
            uint64_t previousEnd;
            if (previous->Size == 0 ||
                previous->PhysicalBase > UINT64_MAX - previous->Size) {
                return 0;
            }
            previousEnd = previous->PhysicalBase + previous->Size;
            if (ranges[descriptorIndex].PhysicalBase < previousEnd) {
                return 0;
            }
            if (ranges[descriptorIndex].PhysicalBase == previousEnd) {
                if (previous->Size >
                    UINT64_MAX - ranges[descriptorIndex].Size) {
                    return 0;
                }
                previous->Size += ranges[descriptorIndex].Size;
            } else {
                ranges[mergedCount++] = ranges[descriptorIndex];
            }
        }
        count = mergedCount;
    }
    context->RuntimeScannerRamRangeCount = count;
    return 1;
}

static void HvEfiRecordApPreparationFailure(
    HV_EFI_AP_PREPARATION_CONTEXT *preparation,
    uint32_t failureStage)
{
    if (preparation == NULL) {
        return;
    }
    if (preparation->FailureStage ==
        HV_EFI_PROCESSOR_PREPARATION_FAILURE_NONE) {
        preparation->FailureStage = failureStage;
    }
    ++preparation->FailureCount;
}

static void EFIAPI HvEfiApPrepareProcessorState(void *procedureArgument)
{
    HV_EFI_AP_PREPARATION_CONTEXT *preparation =
        (HV_EFI_AP_PREPARATION_CONTEXT *)procedureArgument;
    HV_EFI_CONTEXT *rootContext;
    HV_EFI_PROCESSOR_RECORD *records;
    HV_EFI_PROCESSOR_RECORD *record;
    HV_EFI_CONTEXT *processorContext;
    uint64_t processorVmxBasic;
    uint64_t secondaryCapabilities;
    uint64_t eptVpidCapabilities;
    UINTN processorNumber = 0;

    if (preparation == NULL || preparation->MpServices == NULL ||
        preparation->RootContext == NULL ||
        preparation->EptMemoryTypes == NULL ||
        preparation->EptMemoryTypeCount !=
            HV_EFI_EPT_IDENTITY_PAGE_COUNT ||
        preparation->EptDenyRanges == NULL ||
        preparation->EptDenyRangeCount == 0 ||
        preparation->MpServices->WhoAmI == NULL ||
        EFI_ERROR(preparation->MpServices->WhoAmI(
            preparation->MpServices, &processorNumber))) {
        HvEfiRecordApPreparationFailure(
            preparation, HV_EFI_PROCESSOR_PREPARATION_FAILURE_PROTOCOL);
        return;
    }
    rootContext = preparation->RootContext;
    if (rootContext->ProcessorRecordsPhysical == 0 ||
        processorNumber >= rootContext->ProcessorRecordCount ||
        processorNumber == rootContext->BspProcessorNumber) {
        HvEfiRecordApPreparationFailure(
            preparation, HV_EFI_PROCESSOR_PREPARATION_FAILURE_IDENTITY);
        return;
    }
    records = (HV_EFI_PROCESSOR_RECORD *)(uintptr_t)
        rootContext->ProcessorRecordsPhysical;
    record = &records[processorNumber];
    if ((record->FirmwareStatus & HV_EFI_PROCESSOR_STATUS_ENABLED) == 0 ||
        (record->StateFlags & HV_EFI_PROCESSOR_STATE_VMX_READY) == 0 ||
        (record->StateFlags & HV_EFI_PROCESSOR_STATE_RESOURCES_BOUND) == 0 ||
        (record->StateFlags & HV_EFI_PROCESSOR_STATE_PREPARED) != 0 ||
        !HvEfiProcessorResourceAddressesValid(record)) {
        HvEfiRecordApPreparationFailure(
            preparation, HV_EFI_PROCESSOR_PREPARATION_FAILURE_RECORD);
        return;
    }
    processorContext =
        (HV_EFI_CONTEXT *)(uintptr_t)record->ContextPhysical;
    if (processorContext->Signature != HV_EFI_CONTEXT_MAGIC ||
        processorContext->Version != HV_EFI_CONTEXT_VERSION ||
        processorContext->Size != sizeof(*processorContext) ||
        processorContext->CurrentProcessorNumber != processorNumber) {
        HvEfiRecordApPreparationFailure(
            preparation, HV_EFI_PROCESSOR_PREPARATION_FAILURE_CONTEXT);
        return;
    }
    /* AP resources are allocated before the final boot memory map is
     * captured. Refresh the shared scanner metadata now that the root has
     * built and frozen its guest-RAM allowlist. */
    processorContext->RuntimeScannerCodeBase =
        rootContext->RuntimeScannerCodeBase;
    processorContext->RuntimeScannerCodeSize =
        rootContext->RuntimeScannerCodeSize;
    processorContext->RuntimeScannerEntry = rootContext->RuntimeScannerEntry;
    processorContext->RuntimeProcessEntry = rootContext->RuntimeProcessEntry;
    processorContext->RuntimeShadowEntry = rootContext->RuntimeShadowEntry;
    processorContext->RuntimeAnalysisEntry =
        rootContext->RuntimeAnalysisEntry;
    processorContext->RuntimeAnalysisStatePhysical =
        rootContext->RuntimeAnalysisStatePhysical;
    processorContext->RuntimeAnalysisStatePages =
        rootContext->RuntimeAnalysisStatePages;
    processorContext->RuntimeScannerStatePhysical =
        rootContext->RuntimeScannerStatePhysical;
    processorContext->RuntimeScannerRamRangesPhysical =
        rootContext->RuntimeScannerRamRangesPhysical;
    processorContext->RuntimeScannerCodePages =
        rootContext->RuntimeScannerCodePages;
    processorContext->RuntimeScannerRamRangePages =
        rootContext->RuntimeScannerRamRangePages;
    processorContext->RuntimeScannerRamRangeCount =
        rootContext->RuntimeScannerRamRangeCount;
    processorContext->GpuStatePhysical = rootContext->GpuStatePhysical;
    processorContext->GpuResidentEntry = rootContext->GpuResidentEntry;
    processorVmxBasic = HvEfiReadMsr(HV_EFI_IA32_VMX_BASIC);
    if (!HvEfiValidateVmxBasic(processorVmxBasic)) {
        HvEfiRecordApPreparationFailure(
            preparation, HV_EFI_PROCESSOR_PREPARATION_FAILURE_VMX_BASIC);
        return;
    }
    /* VMX regions are processor-local.  Initialize each AP from its own
     * IA32_VMX_BASIC revision instead of assuming hybrid core classes expose
     * the BSP's complete capability value. */
    *(uint32_t *)(uintptr_t)record->VmxonPhysical =
        (uint32_t)(processorVmxBasic & HV_EFI_VMX_BASIC_REVISION_MASK);
    *(uint32_t *)(uintptr_t)record->VmcsPhysical =
        (uint32_t)(processorVmxBasic & HV_EFI_VMX_BASIC_REVISION_MASK);

    HvEfiCaptureCpu(processorContext);
    processorContext->OriginalCr0 = processorContext->GuestCr0;
    processorContext->OriginalCr4 = processorContext->GuestCr4;
    processorContext->TrBase = HvEfiDescriptorBase(
        processorContext, processorContext->Tr);
    processorContext->GuestRsp = processorContext->GuestStackTop;
    processorContext->GuestRflags = processorContext->Rflags | 2;
    processorContext->GuestR15 =
        (uint64_t)(uintptr_t)processorContext;
    processorContext->GuestCr0 = HvEfiReadCr0();
    processorContext->GuestCr3 = HvEfiReadCr3();
    processorContext->GuestCr4 = HvEfiReadCr4();
    record->StateFlags |= HV_EFI_PROCESSOR_STATE_CPU_CAPTURED;

    if (!HvEfiPrepareHostDescriptorState(
            processorContext, record->DescriptorStoragePhysical,
            rootContext->ResidentCodeBase)) {
        HvEfiRecordApPreparationFailure(
            preparation, HV_EFI_PROCESSOR_PREPARATION_FAILURE_DESCRIPTOR);
        return;
    }
    /* OVMF and other firmware may park APs with TR=0.  VM entry requires a
     * usable TR even with unrestricted guest enabled.  Reuse the private host
     * TSS storage as an independent guest VMCS tuple until intercepted INIT
     * installs the architecturally reset AP state; the callback itself does
     * not execute task-switching instructions. */
    processorContext->Tr = HV_EFI_HOST_TSS_SELECTOR;
    processorContext->TrBase = processorContext->HostTrBase;
    processorContext->GuestTrLimit = HV_EFI_TSS64_MIN_SIZE - 1;
    /* The AP resumes the live firmware callback with its captured guest
     * descriptors.  INIT replaces every architecturally relevant guest
     * descriptor VMCS field before SIPI, so the BSP-only interrupt-guard
     * copies are neither consumed nor required on this path. */
    processorContext->DescriptorStateReady = 1;
    record->StateFlags |= HV_EFI_PROCESSOR_STATE_DESCRIPTORS_READY;
    if (!HvEfiPrepareControlRegisters(
            processorContext, processorVmxBasic, 1)) {
        HvEfiRecordApPreparationFailure(
            preparation, HV_EFI_PROCESSOR_PREPARATION_FAILURE_CONTROLS);
        return;
    }
    record->StateFlags |= HV_EFI_PROCESSOR_STATE_CONTROLS_READY;
    secondaryCapabilities = HvEfiReadMsr(
        HV_EFI_IA32_VMX_PROCBASED_CTLS2);
    eptVpidCapabilities = HvEfiReadMsr(HV_EFI_IA32_VMX_EPT_VPID_CAP);
    if (!HvEfiApStartupCapabilitiesValid(
            secondaryCapabilities, eptVpidCapabilities)) {
        if (preparation->FailureStage ==
            HV_EFI_PROCESSOR_PREPARATION_FAILURE_NONE) {
            preparation->FailureSecondaryCapabilities =
                secondaryCapabilities;
            preparation->FailureEptVpidCapabilities =
                eptVpidCapabilities;
        }
        HvEfiRecordApPreparationFailure(
            preparation,
            HV_EFI_PROCESSOR_PREPARATION_FAILURE_STARTUP_CAPABILITIES);
        return;
    }
    processorContext->EptVpidCapabilities = eptVpidCapabilities;
    processorContext->ApStartupCapabilitiesReady = 1;
    if (processorContext->GpuStatePhysical != 0 &&
        !HvGpuNativePrepareCpu((HV_GPU_NATIVE_STATE *)(uintptr_t)
            processorContext->GpuStatePhysical, processorContext)) {
        HvEfiRecordApPreparationFailure(preparation,
            HV_EFI_PROCESSOR_PREPARATION_FAILURE_MEMORY_POLICY);
        return;
    }
    if (!HvEfiBuildProtectedEptIdentityHierarchy(
            (uint64_t *)(uintptr_t)processorContext->EptPhysical,
            HV_EFI_EPT_TABLE_PAGES, processorContext->EptPhysical,
            eptVpidCapabilities, preparation->EptMemoryTypes,
            preparation->EptMemoryTypeCount,
            preparation->EptDenyRanges,
            preparation->EptDenyRangeCount,
            &processorContext->EptPointer,
            &processorContext->EptSplitPageCount,
            &processorContext->EptDeniedPageCount) ||
        !HvEfiProtectedEptIdentityHierarchyValid(
            (const uint64_t *)(uintptr_t)processorContext->EptPhysical,
            HV_EFI_EPT_TABLE_PAGES, processorContext->EptPhysical,
            eptVpidCapabilities, preparation->EptMemoryTypes,
            preparation->EptMemoryTypeCount,
            preparation->EptDenyRanges,
            preparation->EptDenyRangeCount,
            processorContext->EptPointer,
            preparation->EptWriteBackLeafCount,
            processorContext->EptSplitPageCount,
            processorContext->EptDeniedPageCount) ||
        (processorContext->GpuStatePhysical != 0 &&
         !HvGpuNativeInstallCpu((HV_GPU_NATIVE_STATE *)(uintptr_t)
             processorContext->GpuStatePhysical, processorContext)) ||
        !HvEfiInstallPhysicalAcpiShadow(processorContext) ||
        !HvEfiBuildEptPointer(processorContext->ShadowEptPhysical,
                              eptVpidCapabilities,
                              &processorContext->ShadowEptPointer) ||
        HvEfiShadowEptInitialize(
            (ShadowEptManager *)(uintptr_t)
                processorContext->ShadowEptStatePhysical,
            (const uint64_t *)(uintptr_t)processorContext->EptPhysical,
            processorContext->EptPhysical, processorContext->EptPointer,
            (uint64_t *)(uintptr_t)processorContext->ShadowEptPhysical,
            processorContext->ShadowEptPhysical, HV_EFI_EPT_TABLE_PAGES,
            processorContext->ShadowEptPointer) != HV_EFI_SHADOW_EPT_OK) {
        HvEfiRecordApPreparationFailure(
            preparation,
            HV_EFI_PROCESSOR_PREPARATION_FAILURE_EPT_HIERARCHY);
        return;
    }
    processorContext->EptWriteBackLeafCount =
        preparation->EptWriteBackLeafCount;
    processorContext->EptHierarchyReady = 1;
    record->StateFlags |= HV_EFI_PROCESSOR_STATE_STARTUP_CAPABLE |
        HV_EFI_PROCESSOR_STATE_EPT_READY;
    if (!HvEfiProcessorPreparationStateValid(record, processorContext)) {
        HvEfiRecordApPreparationFailure(
            preparation, HV_EFI_PROCESSOR_PREPARATION_FAILURE_INVARIANT);
        return;
    }
    ++preparation->PreparedCount;
}

static EFI_STATUS HvEfiPrepareApProcessorStates(HV_EFI_CONTEXT *context)
{
    EFI_GUID mpServicesGuid = HV_EFI_MP_SERVICES_PROTOCOL_GUID;
    HV_EFI_MP_SERVICES_PROTOCOL *mpServices = NULL;
    HV_EFI_AP_PREPARATION_CONTEXT preparation = { 0 };
    HV_EFI_PROCESSOR_RECORD *records;
    HV_EFI_PROCESSOR_RECORD *bspRecord;
    EFI_MEMORY_DESCRIPTOR *memoryMap = NULL;
    uint8_t *eptMemoryTypes = NULL;
    HV_EFI_EPT_DENY_RANGE *eptDenyRanges = NULL;
    UINTN memoryMapSize = 0;
    UINTN descriptorSize = 0;
    UINT32 descriptorVersion = 0;
    uint32_t writeBackLeafCount = 0;
    uint32_t eptDenyRangeCount = 0;
    uint64_t eptVpidCapabilities;
    EFI_STATUS status = EFI_SUCCESS;
    EFI_STATUS memoryMapFreeStatus = EFI_SUCCESS;
    EFI_STATUS eptPlanFreeStatus = EFI_SUCCESS;
    EFI_STATUS denyPlanFreeStatus = EFI_SUCCESS;
    uint32_t preparationDetailBeforePoolRelease;
    int preparationFailedBeforePoolRelease;
    uint32_t processorNumber;

    if (context == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    HvEfiBootDiagnosticSetDetail(
        HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_NATIVE_CPU);
    if (context->GpuStatePhysical != 0 &&
        !HvGpuNativePrepareCpu((HV_GPU_NATIVE_STATE *)(uintptr_t)
            context->GpuStatePhysical, context)) return EFI_UNSUPPORTED;
#if defined(HYPERSINV_GPU_VM_SELFTEST)
    HvEfiBootDiagnosticSetDetail(
        HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_VM_WC_CPU);
    if (context->GpuVmWcStatePhysical != 0 && !HvGpuVmWcPrepareCpu(context))
        return EFI_UNSUPPORTED;
#endif
    context->ProcessorPreparationStatus =
        HV_EFI_PROCESSOR_PREPARATION_UNAVAILABLE;
    context->ProcessorPreparationFailure =
        HV_EFI_PROCESSOR_PREPARATION_FAILURE_NONE;
    context->PreparedProcessorCount = 1;
    HvEfiBootDiagnosticSetDetail(
        HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_RESOURCE_VALIDATION);
    if (context->ProcessorRecordsPhysical == 0 ||
        context->ProcessorRecordCount == 0 ||
        context->CurrentProcessorNumber >= context->ProcessorRecordCount) {
        context->ProcessorPreparationStatus =
            HV_EFI_PROCESSOR_PREPARATION_FAILED;
        return EFI_ABORTED;
    }
    records = (HV_EFI_PROCESSOR_RECORD *)(uintptr_t)
        context->ProcessorRecordsPhysical;
    bspRecord = &records[context->CurrentProcessorNumber];
    if (bspRecord->ContextPhysical != (uint64_t)(uintptr_t)context ||
        (bspRecord->StateFlags &
         (HV_EFI_PROCESSOR_STATE_RESOURCES_BOUND |
          HV_EFI_PROCESSOR_STATE_PREPARED)) !=
            (HV_EFI_PROCESSOR_STATE_RESOURCES_BOUND |
             HV_EFI_PROCESSOR_STATE_PREPARED) ||
        context->HostDescriptorStateReady == 0 ||
        !HvEfiProcessorResourceAddressesValid(bspRecord)) {
        context->ProcessorPreparationStatus =
            HV_EFI_PROCESSOR_PREPARATION_FAILED;
        return EFI_ABORTED;
    }
    HvEfiBootDiagnosticSetDetail(
        HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_CALLBACK);
    if (gBS == NULL ||
        (context->EnabledProcessorCount > 1 &&
         (context->ProcessorResourceStatus !=
              HV_EFI_PROCESSOR_RESOURCES_READY ||
          gBS->LocateProtocol == NULL ||
          EFI_ERROR(gBS->LocateProtocol(&mpServicesGuid, NULL,
                                        (void **)&mpServices)) ||
          mpServices == NULL || mpServices->StartupAllAPs == NULL ||
          mpServices->WhoAmI == NULL))) {
        context->ProcessorPreparationStatus =
            HV_EFI_PROCESSOR_PREPARATION_FAILED;
        return EFI_ABORTED;
    }

    HvEfiBootDiagnosticSetDetail(
        HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_ALLOC_MEMORY_TYPES);
    status = gBS->AllocatePool(
        EfiLoaderData, HV_EFI_EPT_IDENTITY_PAGE_COUNT,
        (void **)&eptMemoryTypes);
    if (!EFI_ERROR(status)) {
        HvEfiBootDiagnosticSetDetail(
            HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_ALLOC_DENY_RANGES);
        status = gBS->AllocatePool(
            EfiLoaderData,
            (UINTN)HV_EFI_EPT_MAX_DENY_RANGES *
                sizeof(HV_EFI_EPT_DENY_RANGE),
            (void **)&eptDenyRanges);
    }
    if (!EFI_ERROR(status)) {
        HvEfiBootDiagnosticSetDetail(
            HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_GET_MEMORY_MAP);
        status = HvEfiGetMemoryMap(&memoryMap, &memoryMapSize,
                                   &descriptorSize, &descriptorVersion);
    }
    if (!EFI_ERROR(status)) {
        HvEfiBootDiagnosticSetDetail(
            HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_DESCRIPTOR_VERSION);
        if (descriptorVersion != HV_EFI_MEMORY_DESCRIPTOR_VERSION) {
            status = EFI_UNSUPPORTED;
        }
    }
    if (!EFI_ERROR(status)) {
        HvEfiBootDiagnosticSetDetail(
            HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_MEMORY_TYPE_PLAN);
        if (!HvEfiBuildEptMemoryTypePlan(
            (const uint8_t *)(const void *)memoryMap, memoryMapSize,
            descriptorSize, eptMemoryTypes,
            HV_EFI_EPT_IDENTITY_PAGE_COUNT,
            &writeBackLeafCount)) {
            status = EFI_ABORTED;
        }
    }
    if (!EFI_ERROR(status)) {
        HvEfiBootDiagnosticSetDetail(
            HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_DENY_INVENTORY);
        if (!HvEfiBuildEptDenyInventory(
            context, eptDenyRanges,
            HV_EFI_EPT_MAX_DENY_RANGES,
            &eptDenyRangeCount)) {
            status = EFI_ABORTED;
        }
    }
    if (!EFI_ERROR(status)) {
        HvEfiBootDiagnosticSetDetail(
            HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_SCANNER_ALLOWLIST);
        if (!HvEfiBuildRuntimeScannerAllowlist(
            context, memoryMap, memoryMapSize, descriptorSize,
            eptDenyRanges, eptDenyRangeCount)) {
            status = EFI_ABORTED;
        }
    }
    if (EFI_ERROR(status)) {
        if (memoryMap != NULL) {
            gBS->FreePool(memoryMap);
        }
        if (eptMemoryTypes != NULL) {
            gBS->FreePool(eptMemoryTypes);
        }
        if (eptDenyRanges != NULL) {
            gBS->FreePool(eptDenyRanges);
        }
        context->ProcessorPreparationFailure =
            HV_EFI_PROCESSOR_PREPARATION_FAILURE_MEMORY_POLICY;
        context->ProcessorPreparationStatus =
            HV_EFI_PROCESSOR_PREPARATION_FAILED;
        return status;
    }

    HvEfiBootDiagnosticSetDetail(
        HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_EPT_HIERARCHY);
    eptVpidCapabilities = HvEfiReadMsr(HV_EFI_IA32_VMX_EPT_VPID_CAP);
    context->EptVpidCapabilities = eptVpidCapabilities;
    if ((eptVpidCapabilities &
         (HV_EFI_EPT_CAP_WALK_LENGTH_4 |
          HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB |
          HV_EFI_EPT_CAP_2MB_PAGE |
          HV_EFI_EPT_CAP_INVEPT |
          HV_EFI_EPT_CAP_INVEPT_SINGLE_CONTEXT |
          HV_EFI_VPID_CAP_INVVPID |
          HV_EFI_VPID_CAP_INVVPID_SINGLE_CONTEXT)) !=
            (HV_EFI_EPT_CAP_WALK_LENGTH_4 |
             HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB |
             HV_EFI_EPT_CAP_2MB_PAGE |
             HV_EFI_EPT_CAP_INVEPT |
             HV_EFI_EPT_CAP_INVEPT_SINGLE_CONTEXT |
             HV_EFI_VPID_CAP_INVVPID |
             HV_EFI_VPID_CAP_INVVPID_SINGLE_CONTEXT) ||
        !HvEfiBuildProtectedEptIdentityHierarchy(
            (uint64_t *)(uintptr_t)context->EptPhysical,
            HV_EFI_EPT_TABLE_PAGES, context->EptPhysical,
            eptVpidCapabilities, eptMemoryTypes,
            HV_EFI_EPT_IDENTITY_PAGE_COUNT,
            eptDenyRanges, eptDenyRangeCount,
            &context->EptPointer,
            &context->EptSplitPageCount,
            &context->EptDeniedPageCount) ||
        !HvEfiProtectedEptIdentityHierarchyValid(
            (const uint64_t *)(uintptr_t)context->EptPhysical,
            HV_EFI_EPT_TABLE_PAGES, context->EptPhysical,
            eptVpidCapabilities, eptMemoryTypes,
            HV_EFI_EPT_IDENTITY_PAGE_COUNT,
            eptDenyRanges, eptDenyRangeCount,
            context->EptPointer, writeBackLeafCount,
            context->EptSplitPageCount,
            context->EptDeniedPageCount) ||
        (context->GpuStatePhysical != 0 &&
         !HvGpuNativeInstallCpu((HV_GPU_NATIVE_STATE *)(uintptr_t)
             context->GpuStatePhysical, context)) ||
        !HvEfiInstallPhysicalAcpiShadow(context) ||
        !HvEfiBuildEptPointer(context->ShadowEptPhysical,
                              eptVpidCapabilities,
                              &context->ShadowEptPointer) ||
        HvEfiShadowEptInitialize(
            (ShadowEptManager *)(uintptr_t)context->ShadowEptStatePhysical,
            (const uint64_t *)(uintptr_t)context->EptPhysical,
            context->EptPhysical, context->EptPointer,
            (uint64_t *)(uintptr_t)context->ShadowEptPhysical,
            context->ShadowEptPhysical, HV_EFI_EPT_TABLE_PAGES,
            context->ShadowEptPointer) != HV_EFI_SHADOW_EPT_OK) {
        context->ProcessorPreparationFailure =
            HV_EFI_PROCESSOR_PREPARATION_FAILURE_EPT_HIERARCHY;
        status = EFI_ABORTED;
    } else {
        context->EptWriteBackLeafCount = writeBackLeafCount;
        context->EptHierarchyReady = 1;
        bspRecord->StateFlags |= HV_EFI_PROCESSOR_STATE_EPT_READY;
        if (!HvEfiProcessorPreparationStateValid(bspRecord, context)) {
            context->ProcessorPreparationFailure =
                HV_EFI_PROCESSOR_PREPARATION_FAILURE_INVARIANT;
            status = EFI_ABORTED;
        }
    }

    preparation.MpServices = mpServices;
    preparation.RootContext = context;
    preparation.EptMemoryTypes = eptMemoryTypes;
    preparation.EptMemoryTypeCount =
        HV_EFI_EPT_IDENTITY_PAGE_COUNT;
    preparation.EptWriteBackLeafCount = writeBackLeafCount;
    preparation.EptDenyRanges = eptDenyRanges;
    preparation.EptDenyRangeCount = eptDenyRangeCount;
    preparation.PreparedCount = 1;
    if (!EFI_ERROR(status) && context->EnabledProcessorCount > 1) {
        HvEfiBootDiagnosticSetDetail(
            HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_CALLBACK);
        status = mpServices->StartupAllAPs(
            mpServices, HvEfiApPrepareProcessorState, TRUE, NULL,
            HV_EFI_AP_OPERATION_TIMEOUT_US, &preparation, NULL);
    }
    if (!EFI_ERROR(status) && preparation.FailureCount == 0 &&
        preparation.PreparedCount == context->EnabledProcessorCount) {
        HvEfiBootDiagnosticSetDetail(
            HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_PHYSICAL_IOMMU);
        status = HvEfiPreparePhysicalIommu(
            context, memoryMap, memoryMapSize, descriptorSize,
            eptDenyRanges, eptDenyRangeCount);
        if (EFI_ERROR(status)) {
            context->ProcessorPreparationFailure =
                HV_EFI_PROCESSOR_PREPARATION_FAILURE_MEMORY_POLICY;
        }
    }
    preparationDetailBeforePoolRelease = gHvEfiBootDiagnosticDetail;
    preparationFailedBeforePoolRelease = EFI_ERROR(status) ||
        preparation.FailureCount != 0 ||
        preparation.FailureStage !=
            HV_EFI_PROCESSOR_PREPARATION_FAILURE_NONE ||
        preparation.PreparedCount != context->EnabledProcessorCount ||
        context->ProcessorPreparationFailure !=
            HV_EFI_PROCESSOR_PREPARATION_FAILURE_NONE;
    if (!preparationFailedBeforePoolRelease) {
        HvEfiBootDiagnosticSetDetail(
            HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_POOL_RELEASE);
    }
    memoryMapFreeStatus = gBS->FreePool(memoryMap);
    eptPlanFreeStatus = gBS->FreePool(eptMemoryTypes);
    denyPlanFreeStatus = gBS->FreePool(eptDenyRanges);
    if (EFI_ERROR(memoryMapFreeStatus) || EFI_ERROR(eptPlanFreeStatus) ||
        EFI_ERROR(denyPlanFreeStatus)) {
        context->ProcessorPreparationFailure =
            HV_EFI_PROCESSOR_PREPARATION_FAILURE_MEMORY_POLICY;
        context->ProcessorPreparationStatus =
            HV_EFI_PROCESSOR_PREPARATION_FAILED;
        return EFI_ERROR(status) ? status : EFI_ABORTED;
    }
    if (!preparationFailedBeforePoolRelease) {
        HvEfiBootDiagnosticSetDetail(preparationDetailBeforePoolRelease);
    }
    context->PreparedProcessorCount = preparation.PreparedCount;
    if (preparation.FailureStage !=
        HV_EFI_PROCESSOR_PREPARATION_FAILURE_NONE) {
        context->ProcessorPreparationFailure = preparation.FailureStage;
    }
    if (preparation.FailureStage ==
        HV_EFI_PROCESSOR_PREPARATION_FAILURE_STARTUP_CAPABILITIES) {
        HvEfiDebugPrintAscii(
            "uefi hypervisor: AP secondary controls allowed: 0x");
        HvEfiDebugPrintHex32((uint32_t)(
            preparation.FailureSecondaryCapabilities >> 32));
        HvEfiDebugPrintHex32((uint32_t)
            preparation.FailureSecondaryCapabilities);
        HvEfiDebugPrintAscii(
            "\nuefi hypervisor: AP EPT/VPID capabilities: 0x");
        HvEfiDebugPrintHex32((uint32_t)(
            preparation.FailureEptVpidCapabilities >> 32));
        HvEfiDebugPrintHex32((uint32_t)
            preparation.FailureEptVpidCapabilities);
        HvEfiDebugPrintAscii("\n");
    }
    if (EFI_ERROR(status) || preparation.FailureCount != 0 ||
        preparation.PreparedCount != context->EnabledProcessorCount) {
        context->ProcessorPreparationStatus =
            HV_EFI_PROCESSOR_PREPARATION_FAILED;
        return EFI_ERROR(status) ? status : EFI_ABORTED;
    }
    context->ProcessorPreparationStatus =
        context->EnabledProcessorCount == 1 ?
            HV_EFI_PROCESSOR_PREPARATION_BSP_ONLY :
            HV_EFI_PROCESSOR_PREPARATION_READY;
    for (processorNumber = 0;
         processorNumber < context->ProcessorRecordCount;
         ++processorNumber) {
        HV_EFI_PROCESSOR_RECORD *record = &records[processorNumber];

        if ((record->StateFlags &
             HV_EFI_PROCESSOR_STATE_RESOURCES_BOUND) != 0) {
            HV_EFI_CONTEXT *processorContext =
                (HV_EFI_CONTEXT *)(uintptr_t)record->ContextPhysical;
            processorContext->PreparedProcessorCount =
                context->PreparedProcessorCount;
            processorContext->ProcessorPreparationStatus =
                context->ProcessorPreparationStatus;
        }
    }
    return EFI_SUCCESS;
}

static void HvEfiReportProcessorPreparation(const HV_EFI_CONTEXT *context)
{
    if (context == NULL) {
        return;
    }
    switch (context->ProcessorPreparationStatus) {
    case HV_EFI_PROCESSOR_PREPARATION_BSP_ONLY:
        HvEfiDebugPrintAscii(
            HV_EFI_PROCESSOR_PREPARATION_BSP_ONLY_MARKER "\n");
        break;
    case HV_EFI_PROCESSOR_PREPARATION_READY:
        HvEfiDebugPrintAscii(
            HV_EFI_PROCESSOR_PREPARATION_PASS_MARKER "\n");
        HvEfiDebugPrintAscii("uefi hypervisor: EPT WB 2MiB leaves: 0x");
        HvEfiDebugPrintHex32(context->EptWriteBackLeafCount);
        HvEfiDebugPrintAscii("\n");
        break;
    case HV_EFI_PROCESSOR_PREPARATION_FAILED:
        HvEfiDebugPrintAscii(
            HV_EFI_PROCESSOR_PREPARATION_FAIL_MARKER "\n");
        switch (context->ProcessorPreparationFailure) {
        case HV_EFI_PROCESSOR_PREPARATION_FAILURE_PROTOCOL:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: processor state failure: PROTOCOL\n");
            break;
        case HV_EFI_PROCESSOR_PREPARATION_FAILURE_IDENTITY:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: processor state failure: IDENTITY\n");
            break;
        case HV_EFI_PROCESSOR_PREPARATION_FAILURE_RECORD:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: processor state failure: RECORD\n");
            break;
        case HV_EFI_PROCESSOR_PREPARATION_FAILURE_CONTEXT:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: processor state failure: CONTEXT\n");
            break;
        case HV_EFI_PROCESSOR_PREPARATION_FAILURE_VMX_BASIC:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: processor state failure: VMX_BASIC\n");
            break;
        case HV_EFI_PROCESSOR_PREPARATION_FAILURE_DESCRIPTOR:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: processor state failure: DESCRIPTOR\n");
            break;
        case HV_EFI_PROCESSOR_PREPARATION_FAILURE_CONTROLS:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: processor state failure: CONTROLS\n");
            break;
        case HV_EFI_PROCESSOR_PREPARATION_FAILURE_INVARIANT:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: processor state failure: INVARIANT\n");
            break;
        case HV_EFI_PROCESSOR_PREPARATION_FAILURE_STARTUP_CAPABILITIES:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: processor state failure: STARTUP_CAPABILITIES\n");
            break;
        case HV_EFI_PROCESSOR_PREPARATION_FAILURE_MEMORY_POLICY:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: processor state failure: MEMORY_POLICY\n");
            break;
        case HV_EFI_PROCESSOR_PREPARATION_FAILURE_EPT_HIERARCHY:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: processor state failure: EPT_HIERARCHY\n");
            break;
        default:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: processor state failure: UNKNOWN\n");
            break;
        }
        break;
    default:
        HvEfiDebugPrintAscii(
            HV_EFI_PROCESSOR_PREPARATION_UNAVAILABLE_MARKER "\n");
        break;
    }
}

typedef struct HV_EFI_AP_ACTIVATION_CONTEXT {
    HV_EFI_MP_SERVICES_PROTOCOL *MpServices;
    HV_EFI_CONTEXT *RootContext;
    volatile uint32_t ActivatedCount;
    volatile uint32_t FailureCount;
    volatile uint32_t FailureStage;
} HV_EFI_AP_ACTIVATION_CONTEXT;

static void HvEfiRecordApActivationFailure(
    HV_EFI_AP_ACTIVATION_CONTEXT *activation,
    uint32_t failureStage)
{
    if (activation == NULL) {
        return;
    }
    if (activation->FailureStage == HV_EFI_AP_ACTIVATION_FAILURE_NONE) {
        activation->FailureStage = failureStage;
    }
    ++activation->FailureCount;
}

static void EFIAPI HvEfiApActivateProcessor(void *procedureArgument)
{
    HV_EFI_AP_ACTIVATION_CONTEXT *activation =
        (HV_EFI_AP_ACTIVATION_CONTEXT *)procedureArgument;
    HV_EFI_CONTEXT *rootContext;
    HV_EFI_PROCESSOR_RECORD *records;
    HV_EFI_PROCESSOR_RECORD *record;
    HV_EFI_CONTEXT *processorContext;
    UINTN processorNumber = 0;
    int vmxonActive = 0;
    int launchResult;
    uint32_t guestStateFailure;

    if (activation == NULL || activation->MpServices == NULL ||
        activation->RootContext == NULL ||
        activation->MpServices->WhoAmI == NULL ||
        EFI_ERROR(activation->MpServices->WhoAmI(
            activation->MpServices, &processorNumber))) {
        HvEfiRecordApActivationFailure(
            activation, HV_EFI_AP_ACTIVATION_FAILURE_IDENTITY);
        return;
    }
    rootContext = activation->RootContext;
    if (rootContext->ProcessorRecordsPhysical == 0 ||
        processorNumber >= rootContext->ProcessorRecordCount ||
        processorNumber == rootContext->BspProcessorNumber) {
        HvEfiRecordApActivationFailure(
            activation, HV_EFI_AP_ACTIVATION_FAILURE_IDENTITY);
        return;
    }
    records = (HV_EFI_PROCESSOR_RECORD *)(uintptr_t)
        rootContext->ProcessorRecordsPhysical;
    record = &records[processorNumber];
    processorContext =
        (HV_EFI_CONTEXT *)(uintptr_t)record->ContextPhysical;
    if (!HvEfiProcessorPreparationStateValid(record, processorContext) ||
        !HvEfiApVmcsControlsValid(processorContext) ||
        (record->StateFlags & HV_EFI_PROCESSOR_STATE_VIRTUALIZED) != 0) {
        HvEfiRecordApActivationFailure(
            activation, HV_EFI_AP_ACTIVATION_FAILURE_INVARIANT);
        return;
    }

    HvEfiWriteCr0(processorContext->HostCr0);
    HvEfiWriteCr4(processorContext->HostCr4);
    if (processorContext->GpuStatePhysical != 0) {
        /* All CPUs finish boot-time WB writes before using the alias-free
         * private WC mapping. No framebuffer stores occur after arming. */
        __wbinvd();
    }
    if (HvEfiVmxOn(&processorContext->VmxonPhysical) != 0) {
        HvEfiRecordApActivationFailure(
            activation, HV_EFI_AP_ACTIVATION_FAILURE_VMXON);
        goto rollback;
    }
    vmxonActive = 1;
    if (HvEfiVmClear(&processorContext->VmcsPhysical) != 0) {
        HvEfiRecordApActivationFailure(
            activation, HV_EFI_AP_ACTIVATION_FAILURE_VMCLEAR);
        goto rollback;
    }
    if (HvEfiVmPtrld(&processorContext->VmcsPhysical) != 0) {
        HvEfiRecordApActivationFailure(
            activation, HV_EFI_AP_ACTIVATION_FAILURE_VMPTRLD);
        goto rollback;
    }
    processorContext->VmcsActive = 1;
    if (!HvEfiWriteControls(processorContext)) {
        HvEfiRecordApActivationFailure(
            activation, HV_EFI_AP_ACTIVATION_FAILURE_VMCS_CONTROLS);
        goto rollback;
    }
    guestStateFailure = HvEfiGuestStateActivationFailure(processorContext);
    if (guestStateFailure != HV_EFI_AP_ACTIVATION_FAILURE_NONE) {
        HvEfiRecordApActivationFailure(activation, guestStateFailure);
        goto rollback;
    }
    if (!HvEfiPrepareGuestState(processorContext)) {
        HvEfiRecordApActivationFailure(
            activation, HV_EFI_AP_ACTIVATION_FAILURE_VMCS_GUEST_WRITE);
        goto rollback;
    }
    if (!HvEfiPrepareHostState(processorContext)) {
        HvEfiRecordApActivationFailure(
            activation, HV_EFI_AP_ACTIVATION_FAILURE_VMCS_HOST);
        goto rollback;
    }

    processorContext->State = 1;
    launchResult = HvEfiVmxLaunchAp(processorContext);
    if (launchResult != 0) {
        HvEfiRecordApActivationFailure(
            activation, HV_EFI_AP_ACTIVATION_FAILURE_VMLAUNCH);
        goto rollback;
    }
    /* A zero return is reachable only through the resident bootstrap and
     * guest-return stub, which publish ownership before returning here. */
    ++activation->ActivatedCount;
    return;

rollback:
    processorContext->VmcsActive = 0;
    if (vmxonActive) {
        HvEfiVmxOff();
    }
    HvEfiWriteCr4(processorContext->OriginalCr4);
    HvEfiWriteCr0(processorContext->OriginalCr0);
}

static EFI_STATUS HvEfiActivateApProcessors(HV_EFI_CONTEXT *context)
{
    EFI_GUID mpServicesGuid = HV_EFI_MP_SERVICES_PROTOCOL_GUID;
    HV_EFI_MP_SERVICES_PROTOCOL *mpServices = NULL;
    HV_EFI_AP_ACTIVATION_CONTEXT activation = { 0, 0, 0, 0, 0 };
    EFI_STATUS status;

    if (context == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    context->ApActivationStatus = HV_EFI_AP_ACTIVATION_UNAVAILABLE;
    context->ApActivationFailure = HV_EFI_AP_ACTIVATION_FAILURE_NONE;
    context->ActivatedApCount = 0;
    if (context->EnabledProcessorCount == 1) {
        context->ApActivationStatus = HV_EFI_AP_ACTIVATION_BSP_ONLY;
        return EFI_SUCCESS;
    }
    if (!HvEfiApActivationTopologyValid(context->EnabledProcessorCount) ||
        context->ProcessorPreparationStatus !=
            HV_EFI_PROCESSOR_PREPARATION_READY ||
        context->PreparedProcessorCount != context->EnabledProcessorCount) {
        context->ApActivationStatus = HV_EFI_AP_ACTIVATION_FAILED;
        context->ApActivationFailure =
            HV_EFI_AP_ACTIVATION_FAILURE_TOPOLOGY;
        return EFI_UNSUPPORTED;
    }
    if (gBS == NULL || gBS->LocateProtocol == NULL ||
        EFI_ERROR(gBS->LocateProtocol(&mpServicesGuid, NULL,
                                      (void **)&mpServices)) ||
        mpServices == NULL || mpServices->StartupAllAPs == NULL ||
        mpServices->WhoAmI == NULL) {
        context->ApActivationStatus = HV_EFI_AP_ACTIVATION_FAILED;
        context->ApActivationFailure =
            HV_EFI_AP_ACTIVATION_FAILURE_IDENTITY;
        return EFI_ABORTED;
    }

    activation.MpServices = mpServices;
    activation.RootContext = context;
    status = mpServices->StartupAllAPs(
        mpServices, HvEfiApActivateProcessor, TRUE, NULL,
        HV_EFI_AP_OPERATION_TIMEOUT_US, &activation, NULL);
    context->ActivatedApCount = activation.ActivatedCount;
    context->ApActivationFailure = activation.FailureStage;
    if (EFI_ERROR(status) || !HvEfiApActivationResultValid(
            context->EnabledProcessorCount,
            context->PreparedProcessorCount,
            activation.ActivatedCount,
            activation.FailureCount)) {
        context->ApActivationStatus = HV_EFI_AP_ACTIVATION_FAILED;
        HvEfiDebugPrintAscii(HV_EFI_AP_ACTIVATION_FAIL_STOP_MARKER "\n");
        HvEfiFailStop();
        return EFI_ABORTED;
    }
    context->ApActivationStatus = HV_EFI_AP_ACTIVATION_READY;
    return EFI_SUCCESS;
}

static void HvEfiReportApActivation(const HV_EFI_CONTEXT *context)
{
    if (context == NULL) {
        return;
    }
    switch (context->ApActivationStatus) {
    case HV_EFI_AP_ACTIVATION_BSP_ONLY:
        HvEfiDebugPrintAscii(HV_EFI_AP_ACTIVATION_BSP_ONLY_MARKER "\n");
        break;
    case HV_EFI_AP_ACTIVATION_READY:
        HvEfiDebugPrintAscii(HV_EFI_AP_ACTIVATION_PASS_MARKER "\n");
        HvEfiDebugPrintAscii(HV_EFI_AP_ACTIVATION_COUNT_MARKER);
        HvEfiDebugPrintHex32(context->ActivatedApCount);
        HvEfiDebugPrintAscii("\n");
        break;
    case HV_EFI_AP_ACTIVATION_FAILED:
        HvEfiDebugPrintAscii(HV_EFI_AP_ACTIVATION_FAIL_MARKER "\n");
        switch (context->ApActivationFailure) {
        case HV_EFI_AP_ACTIVATION_FAILURE_TOPOLOGY:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: AP virtualization failure: TOPOLOGY\n");
            break;
        case HV_EFI_AP_ACTIVATION_FAILURE_IDENTITY:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: AP virtualization failure: IDENTITY\n");
            break;
        case HV_EFI_AP_ACTIVATION_FAILURE_INVARIANT:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: AP virtualization failure: INVARIANT\n");
            break;
        case HV_EFI_AP_ACTIVATION_FAILURE_VMXON:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: AP virtualization failure: VMXON\n");
            break;
        case HV_EFI_AP_ACTIVATION_FAILURE_VMCLEAR:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: AP virtualization failure: VMCLEAR\n");
            break;
        case HV_EFI_AP_ACTIVATION_FAILURE_VMPTRLD:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: AP virtualization failure: VMPTRLD\n");
            break;
        case HV_EFI_AP_ACTIVATION_FAILURE_VMCS_CONTROLS:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: AP virtualization failure: VMCS CONTROLS\n");
            break;
        case HV_EFI_AP_ACTIVATION_FAILURE_VMCS_GUEST_CS:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: AP virtualization failure: VMCS GUEST CS\n");
            break;
        case HV_EFI_AP_ACTIVATION_FAILURE_VMCS_GUEST_SS:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: AP virtualization failure: VMCS GUEST SS\n");
            break;
        case HV_EFI_AP_ACTIVATION_FAILURE_VMCS_GUEST_TR:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: AP virtualization failure: VMCS GUEST TR\n");
            break;
        case HV_EFI_AP_ACTIVATION_FAILURE_VMCS_GUEST_WRITE:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: AP virtualization failure: VMCS GUEST WRITE\n");
            break;
        case HV_EFI_AP_ACTIVATION_FAILURE_VMCS_HOST:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: AP virtualization failure: VMCS HOST\n");
            break;
        case HV_EFI_AP_ACTIVATION_FAILURE_VMLAUNCH:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: AP virtualization failure: VMLAUNCH\n");
            break;
        default:
            HvEfiDebugPrintAscii(
                "uefi hypervisor: AP virtualization failure: UNKNOWN\n");
            break;
        }
        break;
    default:
        break;
    }
}

static EFI_STATUS HvEfiFailAndFree(HV_EFI_CONTEXT *context,
                                   EFI_PHYSICAL_ADDRESS contextPa,
                                   EFI_PHYSICAL_ADDRESS codePa,
                                   EFI_PHYSICAL_ADDRESS vmxonPa,
                                   EFI_PHYSICAL_ADDRESS vmcsPa,
                                   EFI_PHYSICAL_ADDRESS hostStackPa,
                                   EFI_PHYSICAL_ADDRESS guestStackPa,
                                   EFI_PHYSICAL_ADDRESS pageTablePa,
                                   EFI_PHYSICAL_ADDRESS descriptorPa,
                                   int vmxonActive,
                                   EFI_STATUS status)
{
    hv_efi_boot_diagnostic_snapshot_t diagnosticSnapshot;
    /* Capture before cleanup destroys context-owned scalar results. */
    HvEfiBootDiagnosticCapture(&diagnosticSnapshot, context);
    if (context != NULL && context->GpuStatePhysical != 0 &&
        context->PhysicalIommuStatePhysical != 0 &&
        ((const HV_EFI_PHYSICAL_IOMMU_STATE *)(uintptr_t)
            context->PhysicalIommuStatePhysical)->Enabled != 0)
        HvEfiFailStop();
    if (context != NULL) HvGpuBootReleaseBeforeLaunch(context);
#if defined(HYPERSINV_GPU_VM_SELFTEST)
    if (context != NULL) HvGpuVmWcFree(context, gBS);
#endif
    if (vmxonActive) {
        HvEfiVmxOff();
        if (context != NULL) {
            HvEfiWriteCr4(context->OriginalCr4);
            HvEfiWriteCr0(context->OriginalCr0);
        }
    }
    HvEfiReleaseApProcessorResources(context);
    HvEfiReleaseBspEpt(context);
    HvEfiFreeRuntime(descriptorPa, HV_EFI_DESCRIPTOR_STORAGE_PAGES);
    HvEfiFreeRuntime(pageTablePa, HV_EFI_PAGE_TABLE_PAGES);
    HvEfiFreeRuntime(guestStackPa, HV_EFI_GUEST_STACK_PAGES);
    HvEfiFreeRuntime(hostStackPa, HV_EFI_HOST_STACK_PAGES);
    HvEfiFreeRuntime(vmcsPa, HV_EFI_VMCS_PAGES);
    HvEfiFreeRuntime(vmxonPa, HV_EFI_VMXON_PAGES);
    if (context != NULL) {
        HvEfiFreeRuntime(context->RuntimeScannerRamRangesPhysical,
                         context->RuntimeScannerRamRangePages);
        context->RuntimeScannerRamRangesPhysical = 0;
        context->RuntimeScannerRamRangePages = 0;
        context->RuntimeScannerRamRangeCount = 0;
        HvEfiFreeRuntime(context->RuntimeScannerStatePhysical,
                         HV_EFI_RUNTIME_SCANNER_STATE_PAGES);
        context->RuntimeScannerStatePhysical = 0;
        HvEfiFreeRuntime(context->RuntimeScannerCodeBase,
                         context->RuntimeScannerCodePages);
        context->RuntimeScannerCodeBase = 0;
        context->RuntimeScannerCodeSize = 0;
        context->RuntimeScannerEntry = 0;
        context->RuntimeProcessEntry = 0;
        context->RuntimeShadowEntry = 0;
        context->RuntimeScannerCodePages = 0;
        HvEfiFreeRuntime(context->RuntimeAnalysisStatePhysical,
                         context->RuntimeAnalysisStatePages);
        context->RuntimeAnalysisStatePhysical = 0;
        context->RuntimeAnalysisStatePages = 0;
        context->RuntimeAnalysisEntry = 0;
        HvEfiFreeRuntime(context->GuestTransitionCodeBase,
                         HV_EFI_GUEST_TRANSITION_CODE_PAGES);
        context->GuestTransitionCodeBase = 0;
        context->GuestTransitionCodeSize = 0;
    }
    HvEfiFreeRuntime(codePa, HV_EFI_RESIDENT_CODE_PAGES);
    if (context != NULL) {
        HvEfiFreeRuntime(context->AcpiShadowPhysical, 2);
        context->AcpiShadowPhysical = 0;
        HvEfiFreeRuntime(context->PhysicalIommuTablePhysical,
                         context->PhysicalIommuTablePages);
        context->PhysicalIommuTablePhysical = 0;
        context->PhysicalIommuTablePages = 0;
        HvEfiFreeRuntime(context->PhysicalIommuStatePhysical,
                         context->PhysicalIommuStatePages);
        context->PhysicalIommuStatePhysical = 0;
        context->PhysicalIommuStatePages = 0;
        HvEfiFreeRuntime(context->ProcessorRecordsPhysical,
                         context->ProcessorRecordPages);
        context->ProcessorRecordsPhysical = 0;
        context->ProcessorRecordPages = 0;
        context->ProcessorRecordCount = 0;
    }
    HvEfiFreeRuntime(contextPa, HV_EFI_CONTEXT_PAGES);
    (void)HvEfiBootDiagnosticReturnWithSnapshot(status, &diagnosticSnapshot);
    return status;
}

#if defined(HYPERSINV_GPU_VM_SELFTEST)
#include "gpu_vm_selftest.h"

static void HvEfiGpuVmEmit(const char *message)
{
    CHAR16 text[128];
    uint32_t index = 0;
    HvEfiDebugPrintAscii(message);
    HvEfiDebugPrintAscii("\n");
    while (message[index] != 0 && index < 125) {
        text[index] = (CHAR16)(uint8_t)message[index];
        ++index;
    }
    text[index++] = '\r';
    text[index++] = '\n';
    text[index] = 0;
    if (gST->ConOut != NULL && gST->ConOut->OutputString != NULL)
        (void)gST->ConOut->OutputString(gST->ConOut, text);
}
#endif

/* Called by the assembly EfiMain wrapper.  The resident guest return resumes
 * the active launch call, after which this body returns normally to firmware. */
EFI_STATUS EFIAPI HvEfiMainBody(EFI_HANDLE imageHandle,
                                EFI_SYSTEM_TABLE *systemTable)
{
    EFI_PHYSICAL_ADDRESS contextPa = 0;
    EFI_PHYSICAL_ADDRESS codePa = 0;
    EFI_PHYSICAL_ADDRESS guestTransitionPa = 0;
    EFI_PHYSICAL_ADDRESS vmxonPa = 0;
    EFI_PHYSICAL_ADDRESS vmcsPa = 0;
    EFI_PHYSICAL_ADDRESS hostStackPa = 0;
    EFI_PHYSICAL_ADDRESS guestStackPa = 0;
    EFI_PHYSICAL_ADDRESS pageTablePa = 0;
    EFI_PHYSICAL_ADDRESS descriptorPa = 0;
    HV_EFI_CONTEXT *context = NULL;
    EFI_STATUS status;
    uint32_t cpuid[4];
    uint64_t featureControl;
    uint64_t vmxBasic;
    uint32_t revision;
    UINTN residentSize;
    UINTN runtimeScannerSize;
    UINTN guestTransitionSize;
    int launchResult;
    uint32_t dmaIsolationState = 0;
    hv_efi_boot_diagnostic_snapshot_t diagnosticSnapshot;

    HvEfiBootDiagnosticReset();
    if (systemTable == NULL || systemTable->BootServices == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    gST = systemTable;
    gBS = systemTable->BootServices;
    HvEfiBootDiagnosticAttach(imageHandle);
    HvEfiDebugInitializeSerial();

#if defined(HYPERSINV_GPU_VM_SELFTEST)
    /* Test-only image: refuse before VMX on anything except the explicitly
     * selected VMware lab environment. This does not enable physical GPU use.
     * The synthetic callbacks exist only before launch in the EFI image. */
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_SELFTEST,
                           HV_BOOT_REASON_UNSUPPORTED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    HvEfiCpuid(1, 0, cpuid);
    if ((cpuid[2] & (UINT32_C(1) << 31)) == 0) {
        return HvEfiBootDiagnosticReturn(EFI_UNSUPPORTED, NULL);
    }
    HvEfiCpuid(UINT32_C(0x40000000), 0, cpuid);
    if (cpuid[1] != UINT32_C(0x61774d56) ||
        cpuid[2] != UINT32_C(0x4d566572) ||
        cpuid[3] != UINT32_C(0x65726177)) {
        return HvEfiBootDiagnosticReturn(EFI_UNSUPPORTED, NULL);
    }
    HvEfiGpuVmEmit("GPU_HAL_MODEL:ENVIRONMENT VMWARE_TEST_ONLY");
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_SELFTEST,
                           HV_BOOT_REASON_OPERATION_FAILED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    if (HvGpuVmSelfTest(HvEfiGpuVmEmit) != 1) {
        return HvEfiBootDiagnosticReturn(EFI_ABORTED, NULL);
    }
#endif

    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_CONTEXT_ALLOC,
                           HV_BOOT_REASON_OPERATION_FAILED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    status = HvEfiAllocateRuntime(EfiRuntimeServicesData, 1, &contextPa);
    if (EFI_ERROR(status)) {
        return HvEfiBootDiagnosticReturn(status, NULL);
    }
    context = (HV_EFI_CONTEXT *)(uintptr_t)contextPa;
    HvEfiZero(context, sizeof(*context));
    context->Signature = HV_EFI_CONTEXT_MAGIC;
    context->Version = HV_EFI_CONTEXT_VERSION;
    context->Size = (uint32_t)sizeof(*context);
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_TOPOLOGY,
                           HV_BOOT_REASON_UNSUPPORTED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    HvEfiCaptureProcessorTopology(context);
    if (context->CurrentProcessorNumber >= UINT16_MAX) {
        return HvEfiFailAndFree(context, contextPa, 0, 0, 0, 0, 0, 0, 0, 0,
                                EFI_UNSUPPORTED);
    }
    context->Vpid = (uint16_t)(context->CurrentProcessorNumber + 1u);
    HvEfiReportProcessorTopology(context);

    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_DMA_ADMISSION,
                           HV_BOOT_REASON_OPERATION_FAILED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    status = HvEfiAdmitDmaIsolation(
        context, systemTable, &dmaIsolationState);
    if (EFI_ERROR(status)) {
        HvEfiBootDiagnosticSet(HV_BOOT_STAGE_DMA_ADMISSION,
                               HV_BOOT_REASON_OPERATION_FAILED,
                               dmaIsolationState);
        HvEfiDebugPrintAscii(
            "uefi hypervisor: DMA isolation admission: FAIL 0x");
        HvEfiDebugPrintHex32(dmaIsolationState);
        HvEfiDebugPrintAscii("\n");
        return HvEfiFailAndFree(context, contextPa, 0, 0, 0, 0, 0, 0, 0, 0,
                                status);
    }
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_DMA_ADMISSION,
                           HV_BOOT_REASON_NONE, dmaIsolationState);
    HvEfiDebugPrintAscii(
        "uefi hypervisor: DMA isolation admission: PASS\n");

    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_RESIDENT_ALLOC,
                           HV_BOOT_REASON_OPERATION_FAILED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    status = HvEfiAllocateRuntime(EfiRuntimeServicesCode,
                                  HV_EFI_RESIDENT_CODE_PAGES, &codePa);
    if (EFI_ERROR(status)) {
        return HvEfiFailAndFree(context, contextPa, 0, 0, 0, 0, 0, 0, 0, 0,
                                status);
    }
    HvEfiDebugPrintAscii("uefi hypervisor: resident code allocation: PASS\n");
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_RESIDENT_BOUNDS,
                           HV_BOOT_REASON_BOUNDS,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    residentSize = (UINTN)((uintptr_t)HvEfiResidentCodeEnd -
                            (uintptr_t)HvEfiResidentVmExitStart);
    if (residentSize == 0 ||
        residentSize >
            HV_EFI_RESIDENT_CODE_PAGES * (UINTN)HV_EFI_PAGE_SIZE ||
        codePa > HV_EFI_MAX_PHYSICAL_ADDRESS -
            (HV_EFI_RESIDENT_CODE_PAGES - 1u) * HV_EFI_PAGE_SIZE) {
        return HvEfiFailAndFree(context, contextPa, codePa, 0, 0, 0, 0, 0, 0,
                                0,
                                EFI_BAD_BUFFER_SIZE);
    }
    HvEfiCopy((void *)(uintptr_t)codePa, HvEfiResidentVmExitStart,
              residentSize);

    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_SCANNER_ALLOC,
                           HV_BOOT_REASON_OPERATION_FAILED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    status = HvEfiAllocateRuntime(
        EfiRuntimeServicesCode, HV_EFI_RUNTIME_SCANNER_CODE_PAGES,
        &context->RuntimeScannerCodeBase);
    if (!EFI_ERROR(status)) {
        context->RuntimeScannerCodePages =
            HV_EFI_RUNTIME_SCANNER_CODE_PAGES;
        status = HvEfiAllocateRuntime(
            EfiRuntimeServicesData, HV_EFI_RUNTIME_SCANNER_STATE_PAGES,
            &context->RuntimeScannerStatePhysical);
    }
    if (!EFI_ERROR(status)) {
        context->RuntimeAnalysisStatePages = HV_EFI_ANALYSIS_STATE_PAGES;
        status = HvEfiAllocateRuntime(
            EfiRuntimeServicesData, HV_EFI_ANALYSIS_STATE_PAGES,
            &context->RuntimeAnalysisStatePhysical);
    }
    if (!EFI_ERROR(status)) {
        context->RuntimeScannerRamRangePages =
            HV_EFI_RUNTIME_SCANNER_RANGE_PAGES;
        status = HvEfiAllocateRuntime(
            EfiRuntimeServicesData, HV_EFI_RUNTIME_SCANNER_RANGE_PAGES,
            &context->RuntimeScannerRamRangesPhysical);
    }
    if (EFI_ERROR(status)) {
        HvEfiDebugPrintAscii(
            "uefi hypervisor: runtime scanner allocation: FAIL 0x");
        HvEfiDebugPrintHex32((uint32_t)status);
        HvEfiDebugPrintAscii("\n");
        return HvEfiFailAndFree(context, contextPa, codePa, 0, 0, 0, 0, 0, 0,
                                0, status);
    }
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_SCANNER_BOUNDS,
                           HV_BOOT_REASON_BOUNDS,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    runtimeScannerSize = (UINTN)(
        (uintptr_t)HvEfiRuntimeScannerResidentEnd -
        (uintptr_t)HvEfiRuntimeScannerResidentStart);
    if (runtimeScannerSize == 0 ||
        runtimeScannerSize >
            HV_EFI_RUNTIME_SCANNER_CODE_PAGES * (UINTN)HV_EFI_PAGE_SIZE ||
        (uintptr_t)HvEfiRuntimeScannerResidentCommand <
            (uintptr_t)HvEfiRuntimeScannerResidentStart ||
        (uintptr_t)HvEfiRuntimeScannerResidentCommand >=
            (uintptr_t)HvEfiRuntimeScannerResidentEnd ||
        (uintptr_t)HvEfiRuntimeProcessResidentCommand <
            (uintptr_t)HvEfiRuntimeScannerResidentStart ||
        (uintptr_t)HvEfiRuntimeProcessResidentCommand >=
            (uintptr_t)HvEfiRuntimeScannerResidentEnd ||
        (uintptr_t)HvEfiRuntimeShadowResidentCommand <
            (uintptr_t)HvEfiRuntimeScannerResidentStart ||
        (uintptr_t)HvEfiRuntimeShadowResidentCommand >=
            (uintptr_t)HvEfiRuntimeScannerResidentEnd ||
        (uintptr_t)HvEfiRuntimeAnalysisResidentCommand <
            (uintptr_t)HvEfiRuntimeScannerResidentStart ||
        (uintptr_t)HvEfiRuntimeAnalysisResidentCommand >=
            (uintptr_t)HvEfiRuntimeScannerResidentEnd ||
        HV_EFI_RUNTIME_PROCESS_STATE_PAGE_OFFSET +
            sizeof(HV_EFI_RUNTIME_PROCESS_STATE) >
            HV_EFI_RUNTIME_SCANNER_STATE_PAGES * HV_EFI_PAGE_SIZE) {
        HvEfiDebugPrintAscii(
            "uefi hypervisor: runtime scanner image bounds: FAIL\n");
        return HvEfiFailAndFree(context, contextPa, codePa, 0, 0, 0, 0, 0, 0,
                                0, EFI_BAD_BUFFER_SIZE);
    }
    HvEfiZero((void *)(uintptr_t)context->RuntimeScannerCodeBase,
              HV_EFI_RUNTIME_SCANNER_CODE_PAGES * HV_EFI_PAGE_SIZE);
    HvEfiCopy((void *)(uintptr_t)context->RuntimeScannerCodeBase,
              HvEfiRuntimeScannerResidentStart, runtimeScannerSize);
    context->RuntimeScannerCodeSize = runtimeScannerSize;
    context->RuntimeScannerEntry = context->RuntimeScannerCodeBase +
        ((uintptr_t)HvEfiRuntimeScannerResidentCommand -
         (uintptr_t)HvEfiRuntimeScannerResidentStart);
    context->RuntimeProcessEntry = context->RuntimeScannerCodeBase +
        ((uintptr_t)HvEfiRuntimeProcessResidentCommand -
         (uintptr_t)HvEfiRuntimeScannerResidentStart);
    context->RuntimeShadowEntry = context->RuntimeScannerCodeBase +
        ((uintptr_t)HvEfiRuntimeShadowResidentCommand -
         (uintptr_t)HvEfiRuntimeScannerResidentStart);
    context->RuntimeAnalysisEntry = context->RuntimeScannerCodeBase +
        ((uintptr_t)HvEfiRuntimeAnalysisResidentCommand -
         (uintptr_t)HvEfiRuntimeScannerResidentStart);
    HvEfiZero((void *)(uintptr_t)context->RuntimeScannerStatePhysical,
              HV_EFI_RUNTIME_SCANNER_STATE_PAGES * HV_EFI_PAGE_SIZE);
    ((HV_EFI_RUNTIME_SCAN_STATE *)(uintptr_t)
        context->RuntimeScannerStatePhysical)->Marker =
            HV_EFI_RUNTIME_SCAN_STATE_MARKER;
    ((HV_EFI_RUNTIME_PROCESS_STATE *)(uintptr_t)
        (context->RuntimeScannerStatePhysical +
         HV_EFI_RUNTIME_PROCESS_STATE_PAGE_OFFSET))->Marker =
            HV_EFI_RUNTIME_PROCESS_STATE_MARKER;
    HvEfiZero((void *)(uintptr_t)context->RuntimeAnalysisStatePhysical,
              HV_EFI_ANALYSIS_STATE_PAGES * HV_EFI_PAGE_SIZE);
    ((HV_EFI_ANALYSIS_STATE *)(uintptr_t)
        context->RuntimeAnalysisStatePhysical)->Marker =
            HV_EFI_ANALYSIS_STATE_MARKER;
    HvEfiZero((void *)(uintptr_t)context->RuntimeScannerRamRangesPhysical,
              HV_EFI_RUNTIME_SCANNER_RANGE_PAGES * HV_EFI_PAGE_SIZE);
    HvEfiDebugPrintAscii(
        "uefi hypervisor: runtime scanner allocation: PASS\n");

    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_TRANSITION_ALLOC,
                           HV_BOOT_REASON_OPERATION_FAILED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    status = HvEfiAllocateRuntime(EfiRuntimeServicesCode,
                                  HV_EFI_GUEST_TRANSITION_CODE_PAGES,
                                  &guestTransitionPa);
    if (EFI_ERROR(status)) {
        return HvEfiFailAndFree(context, contextPa, codePa, 0, 0, 0, 0, 0, 0,
                                0, status);
    }
    context->GuestTransitionCodeBase = guestTransitionPa;
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_TRANSITION_BOUNDS,
                           HV_BOOT_REASON_BOUNDS,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    guestTransitionSize = (UINTN)(
        (uintptr_t)HvEfiGuestTransitionEnd -
        (uintptr_t)HvEfiGuestTransitionStart);
    if (guestTransitionSize == 0 ||
        guestTransitionSize >
            HV_EFI_GUEST_TRANSITION_CODE_PAGES *
                (UINTN)HV_EFI_PAGE_SIZE ||
        guestTransitionPa > HV_EFI_MAX_PHYSICAL_ADDRESS -
            (HV_EFI_GUEST_TRANSITION_CODE_PAGES - 1u) *
                HV_EFI_PAGE_SIZE ||
        (uintptr_t)HvEfiGuestBootstrap <
            (uintptr_t)HvEfiGuestTransitionStart ||
        (uintptr_t)HvEfiGuestBootstrap >=
            (uintptr_t)HvEfiGuestTransitionEnd ||
        (uintptr_t)HvEfiGuestReturn <
            (uintptr_t)HvEfiGuestTransitionStart ||
        (uintptr_t)HvEfiGuestReturn >=
            (uintptr_t)HvEfiGuestTransitionEnd ||
        (uintptr_t)HvEfiResidentGuestInterruptVectorStubs <
            (uintptr_t)HvEfiGuestTransitionStart ||
        (uintptr_t)HvEfiResidentGuestInterruptVectorStubs >=
            (uintptr_t)HvEfiGuestTransitionEnd ||
        (uintptr_t)HvEfiResidentGuestInterruptGuard <
            (uintptr_t)HvEfiGuestTransitionStart ||
        (uintptr_t)HvEfiResidentGuestInterruptGuard >=
            (uintptr_t)HvEfiGuestTransitionEnd) {
        return HvEfiFailAndFree(context, contextPa, codePa, 0, 0, 0, 0, 0, 0,
                                0, EFI_BAD_BUFFER_SIZE);
    }
    HvEfiCopy((void *)(uintptr_t)guestTransitionPa,
              HvEfiGuestTransitionStart, guestTransitionSize);
    context->GuestTransitionCodeSize = guestTransitionSize;
    HvEfiDebugPrintAscii(
        "uefi hypervisor: guest transition allocation: PASS\n");

    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_VMXON_ALLOC,
                           HV_BOOT_REASON_OPERATION_FAILED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    status = HvEfiAllocateRuntime(EfiRuntimeServicesData,
                                  HV_EFI_VMXON_PAGES, &vmxonPa);
    if (EFI_ERROR(status)) {
        return HvEfiFailAndFree(context, contextPa, codePa, 0, 0, 0, 0, 0, 0,
                                0,
                                status);
    }
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_VMCS_ALLOC,
                           HV_BOOT_REASON_OPERATION_FAILED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    status = HvEfiAllocateRuntime(EfiRuntimeServicesData,
                                  HV_EFI_VMCS_PAGES, &vmcsPa);
    if (EFI_ERROR(status)) {
        return HvEfiFailAndFree(context, contextPa, codePa, vmxonPa, 0, 0, 0,
                                0, 0, 0, status);
    }
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_HOST_STACK_ALLOC,
                           HV_BOOT_REASON_OPERATION_FAILED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    status = HvEfiAllocateRuntime(EfiRuntimeServicesData,
                                  HV_EFI_HOST_STACK_PAGES, &hostStackPa);
    if (EFI_ERROR(status)) {
        return HvEfiFailAndFree(context, contextPa, codePa, vmxonPa, vmcsPa, 0,
                                0, 0, 0, 0, status);
    }
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_GUEST_STACK_ALLOC,
                           HV_BOOT_REASON_OPERATION_FAILED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    status = HvEfiAllocateRuntime(EfiRuntimeServicesData,
                                  HV_EFI_GUEST_STACK_PAGES, &guestStackPa);
    if (EFI_ERROR(status)) {
        return HvEfiFailAndFree(context, contextPa, codePa, vmxonPa, vmcsPa,
                                hostStackPa, 0, 0, 0, 0, status);
    }
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_PAGE_TABLE_ALLOC,
                           HV_BOOT_REASON_OPERATION_FAILED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    status = HvEfiAllocateRuntime(EfiRuntimeServicesData,
                                  HV_EFI_PAGE_TABLE_PAGES, &pageTablePa);
    if (EFI_ERROR(status)) {
        return HvEfiFailAndFree(context, contextPa, codePa, vmxonPa, vmcsPa,
                                hostStackPa, guestStackPa, 0, 0, 0, status);
    }
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_DESCRIPTOR_ALLOC,
                           HV_BOOT_REASON_OPERATION_FAILED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    status = HvEfiAllocateRuntime(EfiRuntimeServicesData,
                                  HV_EFI_DESCRIPTOR_STORAGE_PAGES,
                                  &descriptorPa);
    if (EFI_ERROR(status)) {
        return HvEfiFailAndFree(context, contextPa, codePa, vmxonPa, vmcsPa,
                                hostStackPa, guestStackPa, pageTablePa, 0,
                                0, status);
    }
    HvEfiDebugPrintAscii("uefi hypervisor: BSP resource allocation: PASS\n");

    /* VM-exit hardware loads this exact RSP.  The context cookie occupies
     * [RSP], below the first push in the resident handler. */
    context->HostStackTop = hostStackPa +
        ((uint64_t)HV_EFI_HOST_STACK_PAGES * HV_EFI_PAGE_SIZE / 2u) - 8;
    context->GuestStackTop = guestStackPa + HV_EFI_PAGE_SIZE - 32;
    context->ResidentCodeBase = codePa;
    context->ResidentCodeSize = residentSize;
    context->ResidentHostRip = codePa +
        ((uintptr_t)HvEfiResidentVmExit -
         (uintptr_t)HvEfiResidentVmExitStart);
    context->GuestBootstrapRip = guestTransitionPa +
        ((uintptr_t)HvEfiGuestBootstrap -
         (uintptr_t)HvEfiGuestTransitionStart);
    context->GuestReturnRip = guestTransitionPa +
        ((uintptr_t)HvEfiGuestReturn -
         (uintptr_t)HvEfiGuestTransitionStart);
    context->VmxonPhysical = vmxonPa;
    context->VmcsPhysical = vmcsPa;
    context->PageTablePhysical = pageTablePa;
    context->MsrBitmapPhysical = pageTablePa +
        (uint64_t)HV_EFI_MSR_BITMAP_PAGE * HV_EFI_PAGE_SIZE;
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_BSP_BIND,
                           HV_BOOT_REASON_INVARIANT,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    if (!HvEfiBindBspProcessorResources(
            context, contextPa, vmxonPa, vmcsPa, hostStackPa, guestStackPa,
            pageTablePa, descriptorPa)) {
        return HvEfiFailAndFree(context, contextPa, codePa, vmxonPa, vmcsPa,
                                hostStackPa, guestStackPa, pageTablePa,
                                descriptorPa, 0, EFI_ABORTED);
    }
    HvEfiDebugPrintAscii("uefi hypervisor: BSP resource binding: PASS\n");

    HvEfiZero((void *)(uintptr_t)vmxonPa, HV_EFI_PAGE_SIZE);
    HvEfiZero((void *)(uintptr_t)vmcsPa, HV_EFI_PAGE_SIZE);
    HvEfiZero((void *)(uintptr_t)hostStackPa, HV_EFI_PAGE_SIZE);
    HvEfiZero((void *)(uintptr_t)guestStackPa, HV_EFI_PAGE_SIZE);
    HvEfiZero((void *)(uintptr_t)context->EptPhysical,
              HV_EFI_EPT_TABLE_PAGES * HV_EFI_PAGE_SIZE);
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_PRIVATE_PAGING,
                           HV_BOOT_REASON_INVARIANT,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    if (!HvEfiInitializePrivatePageTables(pageTablePa)) {
        return HvEfiFailAndFree(context, contextPa, codePa, vmxonPa, vmcsPa,
                                hostStackPa, guestStackPa, pageTablePa,
                                descriptorPa, 0, EFI_ABORTED);
    }
    HvEfiDebugPrintAscii("uefi hypervisor: BSP private paging: PASS\n");
    *(uint64_t *)(uintptr_t)context->HostStackTop =
        (uint64_t)(uintptr_t)context;

    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_CPU_CAPTURE,
                           HV_BOOT_REASON_INVARIANT,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    HvEfiCaptureCpu(context);
    context->OriginalCr0 = context->GuestCr0;
    context->OriginalCr4 = context->GuestCr4;
    context->TrBase = HvEfiDescriptorBase(context, context->Tr);
    context->GuestRsp = context->GuestStackTop;
    context->GuestRflags = context->Rflags | 2;
    context->GuestR15 = (uint64_t)(uintptr_t)context;
    context->GuestCr0 = HvEfiReadCr0();
    context->GuestCr3 = HvEfiReadCr3();
    context->GuestCr4 = HvEfiReadCr4();
    if (!HvEfiMarkCurrentProcessorState(
            context, HV_EFI_PROCESSOR_STATE_CPU_CAPTURED)) {
        return HvEfiFailAndFree(context, contextPa, codePa, vmxonPa, vmcsPa,
                                hostStackPa, guestStackPa, pageTablePa,
                                descriptorPa, 0, EFI_ABORTED);
    }
    HvEfiDebugPrintAscii("uefi hypervisor: BSP CPU capture: PASS\n");
    HvEfiDebugPrintAscii("uefi hypervisor: GDTR limit: 0x");
    HvEfiDebugPrintHex32((uint32_t)context->GdtrLimit);
    HvEfiDebugPrintAscii("\nuefi hypervisor: IDTR limit: 0x");
    HvEfiDebugPrintHex32((uint32_t)context->IdtrLimit);
    HvEfiDebugPrintAscii("\nuefi hypervisor: selectors CS/TR/LDTR: 0x");
    HvEfiDebugPrintHex32((uint32_t)context->Cs);
    HvEfiDebugPrintAscii("/0x");
    HvEfiDebugPrintHex32((uint32_t)context->Tr);
    HvEfiDebugPrintAscii("/0x");
    HvEfiDebugPrintHex32((uint32_t)context->Ldtr);
    HvEfiDebugPrintAscii("\nuefi hypervisor: guest TR limit: 0x");
    HvEfiDebugPrintHex32(context->GuestTrLimit);
    HvEfiDebugPrintAscii("\n");

    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_DESCRIPTORS,
                           HV_BOOT_REASON_UNSUPPORTED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    if (!HvEfiPrepareDescriptorState(context, descriptorPa, codePa)) {
        return HvEfiFailAndFree(context, contextPa, codePa, vmxonPa, vmcsPa,
                                hostStackPa, guestStackPa, pageTablePa,
                                descriptorPa, 0, EFI_UNSUPPORTED);
    }
    HvEfiDebugPrintAscii("uefi hypervisor: BSP descriptor state: PASS\n");
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_DESCRIPTORS,
                           HV_BOOT_REASON_INVARIANT,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    if (!HvEfiMarkCurrentProcessorState(
            context, HV_EFI_PROCESSOR_STATE_DESCRIPTORS_READY)) {
        return HvEfiFailAndFree(context, contextPa, codePa, vmxonPa, vmcsPa,
                                hostStackPa, guestStackPa, pageTablePa,
                                descriptorPa, 0, EFI_ABORTED);
    }

    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_CPU_FEATURES,
                           HV_BOOT_REASON_UNSUPPORTED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    HvEfiCpuid(1, 0, cpuid);
    if ((cpuid[2] & (UINT32_C(1) << 5)) == 0 ||
        (cpuid[2] & (UINT32_C(1) << 30)) == 0 ||
        !HvEfiInitializeRuntimeToken(context)) {
        return HvEfiFailAndFree(context, contextPa, codePa, vmxonPa, vmcsPa,
                                hostStackPa, guestStackPa, pageTablePa,
                                descriptorPa, 0,
                                EFI_UNSUPPORTED);
    }
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_FEATURE_CONTROL,
                           HV_BOOT_REASON_UNSUPPORTED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    featureControl = HvEfiReadMsr(HV_EFI_IA32_FEATURE_CONTROL);
    if ((featureControl & (HV_EFI_FEATURE_CONTROL_LOCK |
                           HV_EFI_FEATURE_CONTROL_VMXON_OUTSIDE_SMX)) !=
        (HV_EFI_FEATURE_CONTROL_LOCK |
         HV_EFI_FEATURE_CONTROL_VMXON_OUTSIDE_SMX)) {
        return HvEfiFailAndFree(context, contextPa, codePa, vmxonPa, vmcsPa,
                                hostStackPa, guestStackPa, pageTablePa,
                                descriptorPa, 0,
                                EFI_ACCESS_DENIED);
    }
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_VMX_BASIC,
                           HV_BOOT_REASON_UNSUPPORTED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    vmxBasic = HvEfiReadMsr(HV_EFI_IA32_VMX_BASIC);
    if (!HvEfiValidateVmxBasic(vmxBasic) ||
        !HvEfiIsCanonical(context->ResidentHostRip) ||
        !HvEfiIsCanonical(context->GuestBootstrapRip) ||
        !HvEfiIsCanonical(context->GuestReturnRip)) {
        return HvEfiFailAndFree(context, contextPa, codePa, vmxonPa, vmcsPa,
                                hostStackPa, guestStackPa, pageTablePa,
                                descriptorPa, 0,
                                EFI_UNSUPPORTED);
    }
    revision = (uint32_t)(vmxBasic & HV_EFI_VMX_BASIC_REVISION_MASK);
    HvEfiDebugPrintAscii("uefi hypervisor: BSP VMX capability: PASS\n");
    *(uint32_t *)(uintptr_t)vmxonPa = revision;
    *(uint32_t *)(uintptr_t)vmcsPa = revision;

    if (context->DmaIsolationMode == HV_EFI_DMA_ISOLATION_VTD_MEDIATED) {
        const HV_EFI_PHYSICAL_IOMMU_STATE *iommu =
            (const HV_EFI_PHYSICAL_IOMMU_STATE *)(uintptr_t)
                context->PhysicalIommuStatePhysical;
        uint32_t segment;
        for (segment = 0; segment < iommu->McfgCount; ++segment) {
            if (iommu->Mcfg[segment].Segment != 0 ||
                iommu->Mcfg[segment].StartBus != 0) continue;
            HvEfiBootDiagnosticSet(HV_BOOT_STAGE_GPU_PREPARE,
                                   HV_BOOT_REASON_OPERATION_FAILED,
                                   HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
            status = HvGpuBootPrepare(context, systemTable,
                iommu->Mcfg[segment].EcamBase,
                (uint64_t)(iommu->Mcfg[segment].EndBus + 1) << 20);
            if (EFI_ERROR(status) && status != EFI_UNSUPPORTED)
                return HvEfiFailAndFree(context, contextPa, codePa,
                    vmxonPa, vmcsPa, hostStackPa, guestStackPa, pageTablePa,
                    descriptorPa, 0, status);
            HvEfiBootDiagnosticSet(HV_BOOT_STAGE_GPU_PREPARE,
                                   HV_BOOT_REASON_NONE,
                                   HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
            break;
        }
        if (context->GpuStatePhysical != 0) {
            context->GpuResidentEntry = context->RuntimeScannerCodeBase +
                ((uintptr_t)HvGpuNativeResident -
                 (uintptr_t)HvEfiRuntimeScannerResidentStart);
            context->RuntimeCapabilities &= ~(HV_EFI_RUNTIME_CAP_SHUTDOWN |
                HV_EFI_RUNTIME_CAP_SUSPEND_PREPARE);
        }
    }
#if defined(HYPERSINV_GPU_VM_SELFTEST)
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_GPU_PREPARE,
                           HV_BOOT_REASON_OPERATION_FAILED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    status = HvGpuVmWcAllocate(context, gBS);
    if (EFI_ERROR(status)) return HvEfiFailAndFree(context, contextPa,
        codePa, vmxonPa, vmcsPa, hostStackPa, guestStackPa, pageTablePa,
        descriptorPa, 0, status);
    context->GpuVmWcEntry = context->RuntimeScannerCodeBase +
        ((uintptr_t)HvGpuVmWcResident - (uintptr_t)HvEfiRuntimeScannerResidentStart);
#endif

    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_AP_RESOURCES,
                           HV_BOOT_REASON_OPERATION_FAILED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    status = HvEfiAllocateApProcessorResources(context, revision);
    HvEfiReportProcessorResources(context);
    if (EFI_ERROR(status)) {
        return HvEfiFailAndFree(context, contextPa, codePa, vmxonPa, vmcsPa,
                                hostStackPa, guestStackPa, pageTablePa,
                                descriptorPa, 0, status);
    }

    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_CONTROLS,
                           HV_BOOT_REASON_UNSUPPORTED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    if (!HvEfiPrepareControlRegisters(context, vmxBasic, 0)) {
        return HvEfiFailAndFree(context, contextPa, codePa, vmxonPa, vmcsPa,
                                hostStackPa, guestStackPa, pageTablePa,
                                descriptorPa, 0,
                                EFI_UNSUPPORTED);
    }
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_CONTROLS,
                           HV_BOOT_REASON_INVARIANT,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    if (!HvEfiMarkCurrentProcessorState(
            context, HV_EFI_PROCESSOR_STATE_CONTROLS_READY)) {
        return HvEfiFailAndFree(context, contextPa, codePa, vmxonPa, vmcsPa,
                                hostStackPa, guestStackPa, pageTablePa,
                                descriptorPa, 0, EFI_ABORTED);
    }

    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_AP_PREPARE,
                           HV_BOOT_REASON_OPERATION_FAILED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    status = HvEfiPrepareApProcessorStates(context);
    HvEfiReportProcessorPreparation(context);
    if (EFI_ERROR(status)) {
        return HvEfiFailAndFree(context, contextPa, codePa, vmxonPa, vmcsPa,
                                hostStackPa, guestStackPa, pageTablePa,
                                descriptorPa, 0, status);
    }

    if (context->GpuStatePhysical != 0) {
        HvEfiBootDiagnosticSet(HV_BOOT_STAGE_GPU_ARM,
                               HV_BOOT_REASON_OPERATION_FAILED,
                               HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    }
    if (context->GpuStatePhysical != 0 && EFI_ERROR(HvGpuBootArm(context))) {
        /* Device DMA may reference the reservation once arming begins.
         * No generic cleanup, native detach or firmware hand-back is safe. */
        HvEfiDebugPrintAscii("uefi hypervisor: GPU arm: FAIL STOP\n");
        HvEfiFailStop();
    }

    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_AP_ACTIVATE,
                           HV_BOOT_REASON_OPERATION_FAILED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    status = HvEfiActivateApProcessors(context);
    HvEfiReportApActivation(context);
    if (EFI_ERROR(status)) {
        return HvEfiFailAndFree(context, contextPa, codePa, vmxonPa, vmcsPa,
                                hostStackPa, guestStackPa, pageTablePa,
                                descriptorPa, 0, status);
    }

    /* Enable VMX only after all software preflight checks pass. */
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_VMXON,
                           HV_BOOT_REASON_OPERATION_FAILED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    HvEfiWriteCr0(context->HostCr0);
    HvEfiWriteCr4(context->HostCr4);
    if (context->GpuStatePhysical != 0 || context->GpuVmWcStatePhysical != 0)
        __wbinvd();
    status = (HvEfiVmxOn(&context->VmxonPhysical) == 0) ?
        EFI_SUCCESS : EFI_DEVICE_ERROR;
    if (EFI_ERROR(status)) {
        HvEfiWriteCr4(context->OriginalCr4);
        HvEfiWriteCr0(context->OriginalCr0);
        HvEfiDebugPrintAscii(
            "uefi hypervisor: BSP virtualization failure: VMXON\n");
        HvEfiFailStop();
        return HvEfiBootDiagnosticReturn(EFI_ABORTED, context);
    }
    HvEfiDebugPrintAscii("uefi hypervisor: BSP VMXON: PASS\n");
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_VMCLEAR,
                           HV_BOOT_REASON_OPERATION_FAILED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    if (HvEfiVmClear(&context->VmcsPhysical) != 0) {
        HvEfiDebugPrintAscii(
            "uefi hypervisor: BSP virtualization failure: VMCLEAR\n");
        HvEfiFailStop();
        return HvEfiBootDiagnosticReturn(EFI_ABORTED, context);
    }
    HvEfiDebugPrintAscii("uefi hypervisor: BSP VMCLEAR: PASS\n");
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_VMPTRLD,
                           HV_BOOT_REASON_OPERATION_FAILED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    if (HvEfiVmPtrld(&context->VmcsPhysical) != 0) {
        HvEfiDebugPrintAscii(
            "uefi hypervisor: BSP virtualization failure: VMPTRLD\n");
        HvEfiFailStop();
        return HvEfiBootDiagnosticReturn(EFI_ABORTED, context);
    }
    HvEfiDebugPrintAscii("uefi hypervisor: BSP VMPTRLD: PASS\n");
    /* From this point VMWRITE is architecturally valid. */
    context->VmcsActive = 1;
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_VMCS_CONTROLS,
                           HV_BOOT_REASON_OPERATION_FAILED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    if (!HvEfiWriteControls(context)) {
        HvEfiDebugPrintAscii(
            "uefi hypervisor: BSP virtualization failure: VMCS CONTROLS\n");
        HvEfiFailStop();
        return HvEfiBootDiagnosticReturn(EFI_ABORTED, context);
    }
    HvEfiDebugPrintAscii("uefi hypervisor: BSP VMCS controls: PASS\n");
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_VMCS_GUEST,
                           HV_BOOT_REASON_OPERATION_FAILED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    if (!HvEfiPrepareGuestState(context)) {
        HvEfiDebugPrintAscii(
            "uefi hypervisor: BSP virtualization failure: VMCS GUEST\n");
        HvEfiFailStop();
        return HvEfiBootDiagnosticReturn(EFI_ABORTED, context);
    }
    HvEfiDebugPrintAscii("uefi hypervisor: BSP VMCS guest: PASS\n");
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_VMCS_HOST,
                           HV_BOOT_REASON_OPERATION_FAILED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    if (!HvEfiPrepareHostState(context)) {
        HvEfiDebugPrintAscii(
            "uefi hypervisor: BSP virtualization failure: VMCS HOST\n");
        HvEfiFailStop();
        return HvEfiBootDiagnosticReturn(EFI_ABORTED, context);
    }
    HvEfiDebugPrintAscii("uefi hypervisor: BSP VMCS host: PASS\n");

    context->State = 1;
    /* Use the same ordinary call/return continuation that is already proven
     * for AP callbacks.  Bypassing the active C frames and returning directly
     * to the EFI caller is not portable across firmware implementations. */
    HvEfiBootDiagnosticSet(HV_BOOT_STAGE_VMLAUNCH,
                           HV_BOOT_REASON_OPERATION_FAILED,
                           HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE);
    HvEfiBootDiagnosticCapture(&diagnosticSnapshot, context);
    launchResult = HvEfiVmxLaunchAp(context);
    /* A zero return is reachable only through ResidentBootstrapVmcall and
     * the guest-transition return stub.  Root mode validates health, records
     * ownership, and stages the native return before resuming this frame.
     * Do not dereference the now-EPT-denied context from non-root mode. */
    if (launchResult == 0) {
#if defined(HYPERSINV_GPU_VM_SELFTEST)
        HvEfiGpuVmEmit("GPU_HAL_VM:VMX_FIRMWARE_RETURN PASS");
#endif
        HvEfiDebugPrintAscii(
            "uefi hypervisor: BSP firmware return: PASS\n");
        return HvEfiBootDiagnosticReturnWithSnapshot(
            EFI_SUCCESS, &diagnosticSnapshot);
    }
    HvEfiDebugPrintAscii(
        "uefi hypervisor: BSP virtualization failure: VMLAUNCH 0x");
    HvEfiDebugPrintHex32((uint32_t)launchResult);
    if (context->VmxErrorValid != 0) {
        HvEfiDebugPrintAscii(" error 0x");
        HvEfiDebugPrintHex32(context->VmxError);
    }
    HvEfiDebugPrintAscii("\n");
    HvEfiFailStop();
    return HvEfiBootDiagnosticReturnWithSnapshot(
        EFI_ABORTED, &diagnosticSnapshot);
}
