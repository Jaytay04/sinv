#include "gpu_enforcement_model.h"

#include <stddef.h>
#include <stdint.h>

#include "gpu_guest_filter.h"
#include "gpu_hal.h"
#include "gpu_wc.h"
#include "iommu_isolation.h"
#include "iommu_runtime.h"

#if defined(_MSC_VER)
#pragma code_seg(push, ".text$gpuenforcement")
#define MODEL_ALIGN4096 __declspec(align(4096))
#else
#define MODEL_ALIGN4096 __attribute__((aligned(4096)))
#endif

#define MODEL_CONFIG_BYTES UINT32_C(256)
#define MODEL_REGISTER_COUNT UINT32_C(9)
#define MODEL_IOMMU_LEGACY_PAGES UINT32_C(1024)
#define MODEL_IOMMU_ROOT_PAGES HV_EFI_IOMMU_ROOT_DEVICE_TABLE_MAX_PAGES
#define MODEL_WC_PAGES UINT32_C(20)
#define MODEL_GGTT_PAGES HV_GPU_MAX_PAGES
#define MODEL_FRAMEBUFFER_WORDS \
    ((uint32_t)(HV_GPU_MAX_BYTES / sizeof(uint32_t)))
#define MODEL_FRAMEBUFFER_SENTINEL UINT32_C(0xff123456)
#define MODEL_HOST_TABLE_PHYSICAL UINT64_C(0x00100000)
#define MODEL_LEGACY_TABLE_PHYSICAL UINT64_C(0x00600000)
#define MODEL_ROOT_TABLE_PHYSICAL UINT64_C(0x00a00000)
#define MODEL_FRAMEBUFFER_PHYSICAL UINT64_C(0x04000000)
#define MODEL_TRANSLATION_PHYSICAL UINT64_C(0x08000000)
#define MODEL_GUEST_RAM_BYTES UINT64_C(0x10000000)
#define MODEL_ECAM_PAGE UINT64_C(0xe0000000)
#define MODEL_BAR0_BASE UINT64_C(0xc0000000)
#define MODEL_BAR2_BASE UINT64_C(0xd0000000)
#define MODEL_BAR_BYTES UINT64_C(0x00100000)
#define MODEL_SESSION_ID UINT64_C(0x4750554d4f44454c)
#define MODEL_PLANE1_PIPE_A UINT32_C(0x70180)
#define MODEL_INVALID_INDEX UINT32_MAX

enum {
    MODEL_REG_CONTROL,
    MODEL_REG_STRIDE,
    MODEL_REG_SIZE,
    MODEL_REG_SURFACE,
    MODEL_REG_LIVE,
    MODEL_REG_OFFSET,
    MODEL_REG_POSITION,
    MODEL_REG_COLOR,
    MODEL_REG_AUX
};

typedef struct MODEL_REGISTER {
    uint32_t Offset;
    uint32_t Value;
} MODEL_REGISTER;

typedef struct MODEL_CONTEXT {
    uint32_t Ownership;
    uint32_t GgttAddressing;
    uint32_t FilterArmed;
    uint32_t PhysicalOutputVerified;
    uint32_t LoseOnMap;
    uint32_t BadPteIndex;
    uint32_t PciForwardCount;
    uint32_t ProofReads;
    uint32_t AuditReads;
    uint32_t RegisterReads;
    uint32_t PteReads;
    uint32_t MapCalls;
    uint32_t DrainCalls;
    uint32_t UnmapCalls;
    uint32_t StoreFences;
    uint32_t Invalidations;
    uint32_t MtrrReads;
    uint32_t CpuStateReads;
    uint32_t WcLeafReads;
    uint32_t WcLeavesVerified;
    uint32_t MapActive;
    uint32_t CleanupOrderValid;
    uint32_t LastCleanup;
    MODEL_REGISTER Registers[MODEL_REGISTER_COUNT];
    HV_GPU_GUEST_FILTER_VCPU PciVcpu;
} MODEL_CONTEXT;

static MODEL_ALIGN4096 uint64_t sLegacyTables[
    MODEL_IOMMU_LEGACY_PAGES * HV_EFI_IOMMU_PAGE_SIZE / sizeof(uint64_t)];
static MODEL_ALIGN4096 uint64_t sRootTables[
    MODEL_IOMMU_ROOT_PAGES * HV_EFI_IOMMU_PAGE_SIZE / sizeof(uint64_t)];
static MODEL_ALIGN4096 uint64_t sHostPageTables[
    MODEL_WC_PAGES * HV_EFI_GPU_WC_PAGE_SIZE / sizeof(uint64_t)];
static MODEL_ALIGN4096 uint32_t sFramebuffer[MODEL_FRAMEBUFFER_WORDS];
static uint8_t sConfig[MODEL_CONFIG_BYTES];
static uint8_t sIoBitmapA[HV_GPU_GUEST_FILTER_IO_BITMAP_BYTES];
static uint8_t sIoBitmapB[HV_GPU_GUEST_FILTER_IO_BITMAP_BYTES];
static MODEL_CONTEXT sContext;
static HV_GPU_STATE sGpuState;
static HV_GPU_ADAPTER sAdapter;
static HV_GPU_ALLOCATION sAllocation;
static HV_OVERLAY_ENTITY_LIST sEntities;
static HV_EFI_GPU_WC_WINDOW sWcWindow;
static uint32_t sWcPrepared;

static HV_EFI_IOMMU_UNIT sUnits[1];
static HV_EFI_IOMMU_DEVICE sDevices[1];
static HV_EFI_IOMMU_RANGE sGuestRam[1];
static HV_EFI_IOMMU_RANGE sDenyRanges[4];
static HV_EFI_IOMMU_RANGE sNormalizedGuestRam[4];
static HV_EFI_IOMMU_RANGE sNormalizedDenyRanges[8];
static HV_EFI_IOMMU_DOMAIN_ASSIGNMENT sAssignments[1];
static HV_EFI_IOMMU_IDENTITY_MAPPING sMappings[16];
static HV_EFI_IOMMU_PLAN_STORAGE sPlanStorage;
static HV_EFI_IOMMU_TOPOLOGY sTopology;
static HV_EFI_IOMMU_PLAN sIdentityPlan;
static HV_EFI_IOMMU_ROOT_DEVICE_RANGE sRootRanges[2];
static HV_EFI_IOMMU_ROOT_DEVICE_PLAN sRootPlan;
static HV_EFI_IOMMU_PROTECTED_RANGE sMappedRanges[1];
static HV_EFI_IOMMU_PROTECTED_RANGE sProtectedDenyRanges[4];
static HV_EFI_IOMMU_LEGACY_TABLE_RESULT sLegacyResult;
static HV_EFI_IOMMU_ROOT_DEVICE_TABLE_RESULT sRootResult;
static HV_GPU_GUEST_FILTER_POLICY sFilterPolicy;

static void ClearBytes(void *memory, size_t bytes)
{
    volatile uint8_t *p = (volatile uint8_t *)memory;
    while (bytes-- != 0) *p++ = 0;
}

static void CopyBytes(void *destination, const void *source, size_t bytes)
{
    volatile uint8_t *to = (volatile uint8_t *)destination;
    const volatile uint8_t *from = (const volatile uint8_t *)source;
    while (bytes-- != 0) *to++ = *from++;
}

static void WriteConfig16(uint32_t offset, uint16_t value)
{
    sConfig[offset] = (uint8_t)value;
    sConfig[offset + 1] = (uint8_t)(value >> 8);
}

static void WriteConfig32(uint32_t offset, uint32_t value)
{
    sConfig[offset] = (uint8_t)value;
    sConfig[offset + 1] = (uint8_t)(value >> 8);
    sConfig[offset + 2] = (uint8_t)(value >> 16);
    sConfig[offset + 3] = (uint8_t)(value >> 24);
}

static void InitializePlaneRegisters(void)
{
    const uint32_t base = MODEL_PLANE1_PIPE_A;
    const uint32_t size = ((sAllocation.Height - 1u) << 16) |
        (sAllocation.Width - 1u);
    sContext.Registers[MODEL_REG_CONTROL].Offset = base;
    sContext.Registers[MODEL_REG_CONTROL].Value =
        HV_GPU_INTEL_PLANE_ENABLE | HV_GPU_INTEL_XRGB8888;
    sContext.Registers[MODEL_REG_STRIDE].Offset = base + UINT32_C(0x08);
    sContext.Registers[MODEL_REG_STRIDE].Value =
        sAllocation.StrideBytes / 64u;
    sContext.Registers[MODEL_REG_SIZE].Offset = base + UINT32_C(0x10);
    sContext.Registers[MODEL_REG_SIZE].Value = size;
    sContext.Registers[MODEL_REG_SURFACE].Offset = base + UINT32_C(0x1c);
    sContext.Registers[MODEL_REG_SURFACE].Value = sAllocation.GgttBase;
    sContext.Registers[MODEL_REG_LIVE].Offset = base + UINT32_C(0x2c);
    sContext.Registers[MODEL_REG_LIVE].Value = sAllocation.GgttBase;
    sContext.Registers[MODEL_REG_OFFSET].Offset = base + UINT32_C(0x24);
    sContext.Registers[MODEL_REG_POSITION].Offset = base + UINT32_C(0x0c);
    sContext.Registers[MODEL_REG_COLOR].Offset = base + UINT32_C(0x4c);
    sContext.Registers[MODEL_REG_COLOR].Value =
        HV_GPU_INTEL_COLOR_GAMMA_DISABLE;
    sContext.Registers[MODEL_REG_AUX].Offset = base + UINT32_C(0x40);
}

static uint32_t BuildIommuModel(HV_GPU_ENFORCEMENT_MODEL_EMIT emit)
{
    HV_EFI_IOMMU_STATUS planStatus;
    HV_EFI_IOMMU_RUNTIME_STATUS runtimeStatus;
    HV_EFI_IOMMU_MAPPING_RESULT mapping;
    uint32_t index;
    uint64_t *targetContext;
    uint64_t *otherContext;
    uint64_t expectedLegacyRoot;

    ClearBytes(sLegacyTables, sizeof(sLegacyTables));
    ClearBytes(sRootTables, sizeof(sRootTables));
    ClearBytes(&sPlanStorage, sizeof(sPlanStorage));
    ClearBytes(&sTopology, sizeof(sTopology));
    ClearBytes(&sIdentityPlan, sizeof(sIdentityPlan));
    ClearBytes(&sRootPlan, sizeof(sRootPlan));
    ClearBytes(&sLegacyResult, sizeof(sLegacyResult));
    ClearBytes(&sRootResult, sizeof(sRootResult));

    sUnits[0].RegisterBase = UINT64_C(0xfed90000);
    sUnits[0].Segment = 0;
    sUnits[0].IncludeAll = 1;
    sUnits[0].TranslationSupported = 1;
    sUnits[0].FourKilobytePagesSupported = 1;
    sUnits[0].AddressWidthBits = HV_EFI_IOMMU_MIN_ADDRESS_BITS;

    sDevices[0].Segment = 0;
    sDevices[0].Bus = 0;
    sDevices[0].Device = 2;
    sDevices[0].Function = 0;
    sDevices[0].Kind = HV_EFI_IOMMU_DEVICE_INTEGRATED;
    sDevices[0].DmaCapable = 1;
    sDevices[0].RemappingUnitIndex = 0;

    sGuestRam[0].PhysicalBase = 0;
    sGuestRam[0].Size = MODEL_GUEST_RAM_BYTES;

    sDenyRanges[0].PhysicalBase = MODEL_HOST_TABLE_PHYSICAL;
    sDenyRanges[0].Size =
        (uint64_t)MODEL_WC_PAGES * HV_EFI_IOMMU_RUNTIME_PAGE_SIZE;
    sDenyRanges[1].PhysicalBase = MODEL_LEGACY_TABLE_PHYSICAL;
    sDenyRanges[1].Size =
        (uint64_t)MODEL_IOMMU_LEGACY_PAGES *
            HV_EFI_IOMMU_RUNTIME_PAGE_SIZE +
        (uint64_t)MODEL_IOMMU_ROOT_PAGES *
            HV_EFI_IOMMU_RUNTIME_PAGE_SIZE;
    sDenyRanges[2].PhysicalBase = MODEL_FRAMEBUFFER_PHYSICAL;
    sDenyRanges[2].Size = HV_GPU_MAX_BYTES;
    sDenyRanges[3].PhysicalBase = MODEL_TRANSLATION_PHYSICAL;
    sDenyRanges[3].Size = HV_GPU_MAX_BYTES;

    sTopology.Platform.DmarPresent = 1;
    sTopology.Platform.DeviceEnumerationComplete = 1;
    sTopology.Platform.DmaAddressBits = HV_EFI_IOMMU_MIN_ADDRESS_BITS;
    sTopology.Units = sUnits;
    sTopology.UnitCount = 1;
    sTopology.Devices = sDevices;
    sTopology.DeviceCount = 1;
    sTopology.GuestRamRanges = sGuestRam;
    sTopology.GuestRamRangeCount = 1;
    sTopology.HypervisorDenyRanges = sDenyRanges;
    sTopology.HypervisorDenyRangeCount = 4;

    sPlanStorage.NormalizedGuestRamRanges = sNormalizedGuestRam;
    sPlanStorage.NormalizedGuestRamCapacity = 4;
    sPlanStorage.NormalizedDenyRanges = sNormalizedDenyRanges;
    sPlanStorage.NormalizedDenyCapacity = 8;
    sPlanStorage.Assignments = sAssignments;
    sPlanStorage.AssignmentCapacity = 1;
    sPlanStorage.Mappings = sMappings;
    sPlanStorage.MappingCapacity = 16;

    planStatus = HvEfiIommuBuildIdentityPlan(
        &sTopology, &sPlanStorage, &sIdentityPlan);
    if (planStatus != HV_EFI_IOMMU_OK) {
        emit("GPU_ENFORCEMENT_MODEL:FAIL iommu-identity-plan");
        return 0;
    }
    if (HvEfiIommuValidateIdentityPlan(&sIdentityPlan, NULL, NULL) !=
        HV_EFI_IOMMU_OK) {
        emit("GPU_ENFORCEMENT_MODEL:FAIL iommu-identity-audit");
        return 0;
    }
    emit("GPU_ENFORCEMENT_MODEL:IOMMU_IDENTITY_PLAN_AUDIT PASS");

    sRootRanges[0].PhysicalBase = MODEL_FRAMEBUFFER_PHYSICAL;
    sRootRanges[0].Size = HV_GPU_MAX_BYTES;
    sRootRanges[0].Kind = HV_EFI_IOMMU_ROOT_DEVICE_RANGE_FRAMEBUFFER;
    sRootRanges[0].Access = HV_EFI_IOMMU_ROOT_DEVICE_ACCESS_READ;
    sRootRanges[1].PhysicalBase = MODEL_TRANSLATION_PHYSICAL;
    sRootRanges[1].Size = HV_GPU_MAX_BYTES;
    sRootRanges[1].Kind = HV_EFI_IOMMU_ROOT_DEVICE_RANGE_TRANSLATION;
    sRootRanges[1].Access = HV_EFI_IOMMU_ROOT_DEVICE_ACCESS_READ;
    planStatus = HvEfiIommuBuildRootDevicePlan(
        &sIdentityPlan, 0, UINT32_C(1), UINT32_C(2), sRootRanges, 2,
        &sRootPlan);
    if (planStatus != HV_EFI_IOMMU_OK) {
        emit("GPU_ENFORCEMENT_MODEL:FAIL iommu-root-device-plan");
        return 0;
    }
    if (HvEfiIommuValidateRootDevicePlan(&sIdentityPlan, &sRootPlan) !=
            HV_EFI_IOMMU_OK || sRootPlan.MappedBytes >
            HV_EFI_IOMMU_ROOT_DEVICE_MAX_MAPPED_BYTES) {
        emit("GPU_ENFORCEMENT_MODEL:FAIL iommu-root-plan-validation");
        return 0;
    }
    emit("GPU_ENFORCEMENT_MODEL:ROOT_DEVICE_PLAN_READ_ONLY PASS");

    sMappedRanges[0].PhysicalBase = 0;
    sMappedRanges[0].Size = MODEL_GUEST_RAM_BYTES;
    sMappedRanges[0].Kind = HV_EFI_IOMMU_MAPPING_RAM;
    for (index = 0; index < 4; ++index) {
        sProtectedDenyRanges[index].PhysicalBase =
            sDenyRanges[index].PhysicalBase;
        sProtectedDenyRanges[index].Size = sDenyRanges[index].Size;
        sProtectedDenyRanges[index].Kind =
            HV_EFI_IOMMU_PROTECTED_PRIVATE;
    }
    runtimeStatus = HvEfiIommuBuildLegacyIdentityDomain(
        sLegacyTables, MODEL_IOMMU_LEGACY_PAGES,
        MODEL_LEGACY_TABLE_PHYSICAL, sMappedRanges, 1,
        sProtectedDenyRanges, 4, UINT16_C(1), &sLegacyResult);
    if (runtimeStatus != HV_EFI_IOMMU_RUNTIME_OK) {
        emit("GPU_ENFORCEMENT_MODEL:FAIL iommu-legacy-tables");
        return 0;
    }
    emit("GPU_ENFORCEMENT_MODEL:LEGACY_DOMAIN_TABLES PASS");

    runtimeStatus = HvEfiIommuBuildRootDeviceDomain(
        sLegacyTables, MODEL_IOMMU_LEGACY_PAGES,
        MODEL_LEGACY_TABLE_PHYSICAL, &sLegacyResult,
        sRootTables, MODEL_IOMMU_ROOT_PAGES, MODEL_ROOT_TABLE_PHYSICAL,
        &sRootPlan, &sRootResult);
    if (runtimeStatus != HV_EFI_IOMMU_RUNTIME_OK) {
        emit("GPU_ENFORCEMENT_MODEL:FAIL iommu-root-domain-build");
        return 0;
    }
    if (sRootResult.MappedPageCount != MODEL_GGTT_PAGES * 2u ||
        sRootResult.PagesUsed > MODEL_IOMMU_ROOT_PAGES) {
        emit("GPU_ENFORCEMENT_MODEL:FAIL iommu-root-domain-result");
        return 0;
    }
    if (HvEfiIommuAuditRootDeviceDomain(
            sLegacyTables, MODEL_IOMMU_LEGACY_PAGES,
            MODEL_LEGACY_TABLE_PHYSICAL, &sLegacyResult,
            sRootTables, MODEL_IOMMU_ROOT_PAGES, MODEL_ROOT_TABLE_PHYSICAL,
            &sRootPlan, &sRootResult) != HV_EFI_IOMMU_RUNTIME_OK) {
        emit("GPU_ENFORCEMENT_MODEL:FAIL iommu-root-domain-audit");
        return 0;
    }
    emit("GPU_ENFORCEMENT_MODEL:ROOT_DOMAIN_AUDIT_READ_ONLY PASS");

    if (HvEfiIommuPlanMappingAt(&sIdentityPlan, 0,
            MODEL_FRAMEBUFFER_PHYSICAL, &mapping) != HV_EFI_IOMMU_OK ||
        mapping.Mapped != 0 ||
        HvEfiIommuPlanMappingAt(&sIdentityPlan, 0,
            MODEL_TRANSLATION_PHYSICAL, &mapping) != HV_EFI_IOMMU_OK ||
        mapping.Mapped != 0 ||
        HvEfiIommuPlanMappingAt(&sIdentityPlan, 0,
            UINT64_C(0x00200000), &mapping) != HV_EFI_IOMMU_OK ||
        mapping.Mapped != 1 || mapping.DomainId != 1) {
        emit("GPU_ENFORCEMENT_MODEL:FAIL iommu-legacy-map-scope");
        return 0;
    }
    emit("GPU_ENFORCEMENT_MODEL:LEGACY_DENY_INVENTORY PASS");

    targetContext = sLegacyTables + (size_t)(1u + sRootPlan.Bus) * 512u +
        (size_t)(sRootPlan.Device * 8u + sRootPlan.Function) * 2u;
    otherContext = sLegacyTables + (size_t)(1u + sRootPlan.Bus) * 512u +
        (size_t)(3u * 8u) * 2u;
    expectedLegacyRoot = MODEL_LEGACY_TABLE_PHYSICAL +
        UINT64_C(258) * HV_EFI_IOMMU_RUNTIME_PAGE_SIZE;
    if ((targetContext[0] & ~UINT64_C(1)) != MODEL_ROOT_TABLE_PHYSICAL ||
        targetContext[1] != (UINT64_C(2) << 8 | UINT64_C(1)) ||
        (otherContext[0] & ~UINT64_C(1)) != expectedLegacyRoot ||
        otherContext[1] != (UINT64_C(1) << 8 | UINT64_C(1))) {
        emit("GPU_ENFORCEMENT_MODEL:FAIL iommu-context-scope");
        return 0;
    }
    emit("GPU_ENFORCEMENT_MODEL:TARGET_CONTEXT_ONLY PASS");
    return 1;
}

static uint32_t InitializeFilterModel(void)
{
    ClearBytes(&sFilterPolicy, sizeof(sFilterPolicy));
    ClearBytes(sIoBitmapA, sizeof(sIoBitmapA));
    ClearBytes(sIoBitmapB, sizeof(sIoBitmapB));
    sFilterPolicy.Size = (uint32_t)sizeof(sFilterPolicy);
    sFilterPolicy.Segment = 0;
    sFilterPolicy.Bus = 0;
    sFilterPolicy.Device = 2;
    sFilterPolicy.Function = 0;
    sFilterPolicy.EcamPage = MODEL_ECAM_PAGE;
    sFilterPolicy.Bar0.Base = MODEL_BAR0_BASE;
    sFilterPolicy.Bar0.Bytes = MODEL_BAR_BYTES;
    sFilterPolicy.Bar2.Base = MODEL_BAR2_BASE;
    sFilterPolicy.Bar2.Bytes = MODEL_BAR_BYTES;
    sIoBitmapA[UINT32_C(0x20)] = UINT8_C(0x20);
    if (!HvGpuGuestFilterValidatePolicy(&sFilterPolicy) ||
        !HvGpuGuestFilterArmIoBitmaps(sIoBitmapA, sIoBitmapB,
            HV_GPU_GUEST_FILTER_IO_BITMAP_BYTES) ||
        !HvGpuGuestFilterIoBitmapsValid(sIoBitmapA, sIoBitmapB,
            HV_GPU_GUEST_FILTER_IO_BITMAP_BYTES) ||
        sIoBitmapA[UINT32_C(0x20)] != UINT8_C(0x20) ||
        HvGpuGuestFilterPageProtected(&sFilterPolicy, MODEL_ECAM_PAGE) != 1 ||
        HvGpuGuestFilterPageProtected(&sFilterPolicy, MODEL_BAR0_BASE) != 1 ||
        HvGpuGuestFilterPageProtected(&sFilterPolicy, MODEL_BAR2_BASE) != 1 ||
        HvGpuGuestFilterPageProtected(&sFilterPolicy,
            MODEL_FRAMEBUFFER_PHYSICAL) != 0) {
        return 0;
    }
    sContext.FilterArmed = 1;
    return 1;
}

static uint32_t MtrrCompatible(void *opaque, uint64_t physical,
                                uint64_t bytes)
{
    MODEL_CONTEXT *context = (MODEL_CONTEXT *)opaque;
    ++context->MtrrReads;
    return physical == MODEL_FRAMEBUFFER_PHYSICAL && bytes != 0 &&
        bytes <= HV_GPU_MAX_BYTES && context->Ownership == 1 ? 1u : 0u;
}

static uint32_t ReadCpuState(void *opaque,
                             HV_EFI_GPU_WC_CPU_STATE *state)
{
    MODEL_CONTEXT *context = (MODEL_CONTEXT *)opaque;
    if (state == NULL) return 0;
    ++context->CpuStateReads;
    state->PatMsr = UINT64_C(0x0000050100070406);
    state->Cr0 = UINT64_C(1) << 16;
    state->Efer = UINT64_C(1) << 11;
    return 1;
}

static void InvalidatePage(void *opaque, uint64_t virtualAddress)
{
    MODEL_CONTEXT *context = (MODEL_CONTEXT *)opaque;
    if (virtualAddress >= HV_EFI_GPU_WC_WINDOW_BASE &&
        virtualAddress < HV_EFI_GPU_WC_WINDOW_BASE + HV_EFI_GPU_WC_MAX_BYTES) {
        ++context->Invalidations;
    }
}

static void StoreFence(void *opaque)
{
    MODEL_CONTEXT *context = (MODEL_CONTEXT *)opaque;
    ++context->StoreFences;
}

static uint32_t PrepareWcModel(HV_GPU_ENFORCEMENT_MODEL_EMIT emit)
{
    uint32_t index;
    HV_EFI_GPU_WC_STATUS status;
    ClearBytes(sHostPageTables, sizeof(sHostPageTables));
    ClearBytes(&sWcWindow, sizeof(sWcWindow));
    sContext.MtrrReads = 0;
    sContext.CpuStateReads = 0;
    sContext.Invalidations = 0;
    sContext.StoreFences = 0;
    sHostPageTables[0] = MODEL_HOST_TABLE_PHYSICAL +
        HV_EFI_GPU_WC_PAGE_SIZE | UINT64_C(3);
    sHostPageTables[HV_EFI_GPU_WC_ENTRY_COUNT] =
        MODEL_HOST_TABLE_PHYSICAL +
        (uint64_t)HV_EFI_GPU_WC_LOW_PD_PAGE * HV_EFI_GPU_WC_PAGE_SIZE |
        UINT64_C(3);
    sHostPageTables[HV_EFI_GPU_WC_ENTRY_COUNT *
        HV_EFI_GPU_WC_LOW_PD_PAGE + 2u] = 0;
    for (index = 0; index < HV_EFI_GPU_WC_ENTRY_COUNT; ++index) {
        sHostPageTables[HV_EFI_GPU_WC_ENTRY_COUNT *
            HV_EFI_GPU_WC_LOW_PD_PAGE + index] =
            ((uint64_t)index << 21) | UINT64_C(0x83);
    }
    /* The low identity PD occupies page 2; the GPU PD starts empty at page 8. */
    sHostPageTables[HV_EFI_GPU_WC_ENTRY_COUNT *
        HV_EFI_GPU_WC_LOW_PD_PAGE + 2u] = UINT64_C(0x00400000) |
            UINT64_C(0x83);
    sHostPageTables[HV_EFI_GPU_WC_ENTRY_COUNT *
        HV_EFI_GPU_WC_PD_PAGE] = 0;
    status = HvEfiGpuWcPrepareHostPageTables(
        sHostPageTables, MODEL_WC_PAGES, MODEL_HOST_TABLE_PHYSICAL,
        MODEL_FRAMEBUFFER_PHYSICAL, HV_GPU_MAX_BYTES,
        UINT64_C(0x0000050100070406), 1, 1, MtrrCompatible,
        ReadCpuState, &sContext, &sWcWindow);
    if (status != HV_EFI_GPU_WC_OK || sWcWindow.PatIndex != 4u ||
        sWcWindow.AliasSplitCount != 4u) {
        sWcPrepared = 0;
        emit("GPU_ENFORCEMENT_MODEL:FAIL wc-host-page-table-prepare");
        return 0;
    }
    status = HvEfiGpuWcInitialize(&sWcWindow, InvalidatePage, StoreFence);
    if (status != HV_EFI_GPU_WC_OK || sWcWindow.Active != 0 ||
        sWcWindow.ActivePageCount != 0) {
        sWcPrepared = 0;
        emit("GPU_ENFORCEMENT_MODEL:FAIL wc-window-initialize");
        return 0;
    }
    sWcPrepared = 1;
    emit("GPU_ENFORCEMENT_MODEL:WC_CPU_PAT_MTRR_PREPARE PASS");
    return 1;
}

static uint32_t AuditRootDomain(void)
{
    MODEL_CONTEXT *context = &sContext;
    ++context->AuditReads;
    return HvEfiIommuAuditRootDeviceDomain(
        sLegacyTables, MODEL_IOMMU_LEGACY_PAGES,
        MODEL_LEGACY_TABLE_PHYSICAL, &sLegacyResult,
        sRootTables, MODEL_IOMMU_ROOT_PAGES, MODEL_ROOT_TABLE_PHYSICAL,
        &sRootPlan, &sRootResult) == HV_EFI_IOMMU_RUNTIME_OK;
}

static uint32_t PolicyAndBitmapValid(void)
{
    return sContext.FilterArmed == 1 &&
        HvGpuGuestFilterValidatePolicy(&sFilterPolicy) == 1 &&
        sFilterPolicy.Segment == sRootPlan.Segment &&
        sFilterPolicy.Bus == sRootPlan.Bus &&
        sFilterPolicy.Device == sRootPlan.Device &&
        sFilterPolicy.Function == sRootPlan.Function &&
        sFilterPolicy.Bar0.Base == sAdapter.MmioPhysical &&
        HvGpuGuestFilterIoBitmapsValid(sIoBitmapA, sIoBitmapB,
            HV_GPU_GUEST_FILTER_IO_BITMAP_BYTES) == 1 &&
        HvGpuGuestFilterPageProtected(&sFilterPolicy,
            sFilterPolicy.EcamPage) == 1 &&
        HvGpuGuestFilterPageProtected(&sFilterPolicy,
            sFilterPolicy.Bar0.Base) == 1 &&
        HvGpuGuestFilterPageProtected(&sFilterPolicy,
            sFilterPolicy.Bar2.Base) == 1;
}

static uint32_t WcWindowMatchesHalState(void)
{
    uint32_t index;
    if (sGpuState.CpuMappingActive == 0) {
        return sWcWindow.Active == 0 && sWcWindow.ActivePageCount == 0;
    }
    if (sGpuState.CpuMappingActive != 1 || sGpuState.Phase != HV_GPU_READY ||
        sWcWindow.Active != 1 || sWcWindow.PhysicalBase !=
            sAllocation.PhysicalBase ||
        sGpuState.Extent.MappingBytes == 0 ||
        sGpuState.Extent.MappingBytes > sWcWindow.PinnedBytes ||
        sWcWindow.ActivePageCount != sGpuState.Extent.PageCount) {
        return 0;
    }
    for (index = 0;
         index < HV_EFI_GPU_WC_PT_PAGE_COUNT * HV_EFI_GPU_WC_ENTRY_COUNT;
         ++index) {
        const uint64_t entry = sHostPageTables[
            HV_EFI_GPU_WC_PT_FIRST_PAGE * HV_EFI_GPU_WC_ENTRY_COUNT + index];
        if (index < sGpuState.Extent.PageCount) {
            const uint64_t expected = sAllocation.PhysicalBase +
                (uint64_t)index * HV_EFI_GPU_WC_PAGE_SIZE |
                UINT64_C(3) | sWcWindow.PatBits | (UINT64_C(1) << 63);
            if (entry != expected) return 0;
        } else if (entry != 0) {
            return 0;
        }
    }
    return 1;
}

static uint32_t ReadExclusion(void *opaque,
                              HV_GPU_EXCLUSION_PROOF *proof)
{
    MODEL_CONTEXT *context = (MODEL_CONTEXT *)opaque;
    if (proof == NULL) return 0;
    ++context->ProofReads;
    ClearBytes(proof, sizeof(*proof));
    if (context->Ownership != 1 || context->GgttAddressing != 1 ||
        !PolicyAndBitmapValid() || context->PhysicalOutputVerified != 1 ||
        sWcPrepared != 1 || !WcWindowMatchesHalState() ||
        !AuditRootDomain()) {
        return 0;
    }
    proof->Size = (uint32_t)sizeof(*proof);
    proof->Version = HV_GPU_EXCLUSION_VERSION;
    proof->Method = HV_GPU_EXCLUSION_BOOT_VPCI_EPT_VTD;
    proof->EnforcementMask = HV_GPU_EXCLUSION_REQUIRED;
    proof->GuestAccessMask = 0;
    proof->LockedUntilReboot = 1;
    proof->PhysicalOutputVerified = 1;
    proof->SessionId = MODEL_SESSION_ID;
    CopyBytes(&proof->Adapter, &sAdapter, sizeof(sAdapter));
    CopyBytes(&proof->Allocation, &sAllocation, sizeof(sAllocation));
    return 1;
}

static uint32_t OwnershipHeld(void *opaque)
{
    MODEL_CONTEXT *context = (MODEL_CONTEXT *)opaque;
    return context->Ownership == 1 && PolicyAndBitmapValid() &&
        AuditRootDomain() ? 1u : 0u;
}

static uint32_t AddressingIsGgtt(void *opaque)
{
    MODEL_CONTEXT *context = (MODEL_CONTEXT *)opaque;
    return context->GgttAddressing == 1 && AuditRootDomain() ? 1u : 0u;
}

static uint32_t ReadRegister(void *opaque, uint32_t offset, uint32_t *value)
{
    MODEL_CONTEXT *context = (MODEL_CONTEXT *)opaque;
    uint32_t index;
    if (value == NULL) return 0;
    ++context->RegisterReads;
    for (index = 0; index < MODEL_REGISTER_COUNT; ++index) {
        if (context->Registers[index].Offset == offset) {
            *value = context->Registers[index].Value;
            return 1;
        }
    }
    return 0;
}

static uint32_t ReadGgttPte(void *opaque, uint32_t index, uint64_t *value)
{
    MODEL_CONTEXT *context = (MODEL_CONTEXT *)opaque;
    const uint32_t first = sAllocation.GgttBase /
        (uint32_t)HV_GPU_PAGE_BYTES;
    uint32_t page;
    if (value == NULL || index < first ||
        index - first >= MODEL_GGTT_PAGES) return 0;
    ++context->PteReads;
    page = index - first;
    *value = sAllocation.PhysicalBase +
        (uint64_t)page * HV_GPU_PAGE_BYTES | UINT64_C(1);
    if (page == context->BadPteIndex) *value += HV_GPU_PAGE_BYTES;
    return 1;
}

static uint32_t MapWc(void *opaque, uint64_t physical, uint64_t bytes,
                      uint32_t **view)
{
    MODEL_CONTEXT *context = (MODEL_CONTEXT *)opaque;
    uint32_t *windowView = NULL;
    uint32_t pageCount;
    uint32_t index;
    uint32_t leavesValid = 1;
    if (view != NULL) *view = NULL;
    ++context->MapCalls;
    if (view == NULL || physical != sAllocation.PhysicalBase ||
        bytes == 0 || bytes > HV_GPU_MAX_BYTES ||
        !HvEfiGpuWcMapWc(&sWcWindow, physical, bytes, &windowView) ||
        (uintptr_t)windowView != (uintptr_t)HV_EFI_GPU_WC_WINDOW_BASE) {
        return 0;
    }
    pageCount = (uint32_t)(bytes / HV_EFI_GPU_WC_PAGE_SIZE);
    for (index = 0;
         index < HV_EFI_GPU_WC_PT_PAGE_COUNT * HV_EFI_GPU_WC_ENTRY_COUNT;
         ++index) {
        const uint64_t entry = sHostPageTables[
            HV_EFI_GPU_WC_PT_FIRST_PAGE * HV_EFI_GPU_WC_ENTRY_COUNT + index];
        ++context->WcLeafReads;
        if (index < pageCount) {
            const uint64_t expected = physical +
                (uint64_t)index * HV_EFI_GPU_WC_PAGE_SIZE |
                UINT64_C(3) | sWcWindow.PatBits | (UINT64_C(1) << 63);
            if (entry != expected) leavesValid = 0;
        } else if (entry != 0) {
            leavesValid = 0;
        }
    }
    if (leavesValid == 0 || sWcWindow.Active != 1 ||
        sWcWindow.ActivePageCount != pageCount) {
        /* The helper has installed a mapping. Publish it to the HAL cleanup
         * path even when the model's PTE readback detects a defect. */
        *view = sFramebuffer;
        context->MapActive = 1;
        context->LastCleanup = 0;
        return 0;
    }
    context->WcLeavesVerified = 1;
    /* The real WC mapper returns its reserved VA. This synthetic test maps
     * that VA to a private RAM buffer so the compositor never dereferences an
     * unmapped host address and the test makes no claim about real WC stores. */
    *view = sFramebuffer;
    context->MapActive = 1;
    context->LastCleanup = 0;
    if (context->LoseOnMap != 0) context->Ownership = 0;
    return 1;
}

static void DrainWrites(void *opaque)
{
    MODEL_CONTEXT *context = (MODEL_CONTEXT *)opaque;
    ++context->DrainCalls;
    context->LastCleanup = 1;
    HvEfiGpuWcDrainWrites(&sWcWindow);
}

static void UnmapWc(void *opaque)
{
    MODEL_CONTEXT *context = (MODEL_CONTEXT *)opaque;
    ++context->UnmapCalls;
    HvEfiGpuWcUnmap(&sWcWindow);
    context->CleanupOrderValid = context->LastCleanup == 1 &&
        sWcWindow.Active == 0 && sWcWindow.ActivePageCount == 0;
    context->LastCleanup = 2;
    context->MapActive = 0;
}

static uint32_t ForwardPci(void *opaque, uint32_t configAddress,
                            uint16_t dataPort, uint32_t widthBytes,
                            uint32_t isInput, uint32_t *value)
{
    MODEL_CONTEXT *context = (MODEL_CONTEXT *)opaque;
    (void)configAddress;
    (void)dataPort;
    (void)widthBytes;
    if (value == NULL || isInput != 1) return 0;
    ++context->PciForwardCount;
    *value = UINT32_C(0x12345678);
    return 1;
}

static HV_GPU_BACKEND CreateBackend(void)
{
    HV_GPU_BACKEND backend;
    backend.Context = &sContext;
    backend.ReadExclusion = ReadExclusion;
    backend.OwnershipHeld = OwnershipHeld;
    backend.AddressingIsGgtt = AddressingIsGgtt;
    backend.ReadRegister = ReadRegister;
    backend.ReadGgttPte = ReadGgttPte;
    backend.MapWc = MapWc;
    backend.DrainWrites = DrainWrites;
    backend.UnmapWc = UnmapWc;
    return backend;
}

static void ResetHalState(void)
{
    ClearBytes(&sGpuState, sizeof(sGpuState));
    sContext.MapCalls = 0;
    sContext.DrainCalls = 0;
    sContext.UnmapCalls = 0;
    sContext.StoreFences = 0;
    sContext.MapActive = 0;
    sContext.WcLeafReads = 0;
    sContext.WcLeavesVerified = 0;
    sContext.CleanupOrderValid = 0;
    sContext.LastCleanup = 0;
    sContext.BadPteIndex = MODEL_INVALID_INDEX;
    sContext.LoseOnMap = 0;
}

static uint32_t InitializeGpu(void)
{
    HV_GPU_BACKEND backend = CreateBackend();
    return HvGpuInitialize(&sGpuState, &sAdapter, &sAllocation, &backend) ==
        HV_GPU_OK;
}

static uint32_t FindFramebufferLeaf(uint32_t *leafIndex)
{
    uint32_t page;
    uint32_t index;
    const uint64_t expected = MODEL_FRAMEBUFFER_PHYSICAL | UINT64_C(1);
    if (leafIndex == NULL) return 0;
    for (page = 0; page < sRootResult.PagesUsed; ++page) {
        for (index = 0; index < 512; ++index) {
            const uint32_t absolute = page * 512u + index;
            if (sRootTables[absolute] == expected) {
                *leafIndex = absolute;
                return 1;
            }
        }
    }
    return 0;
}

static uint32_t CheckFilterIoModel(void)
{
    HV_GPU_GUEST_FILTER_IO_RESULT ioStatus;
    uint64_t rax;
    uint32_t advanceRip;
    uint64_t qualification;
    uint32_t targetSelector = UINT32_C(0x80001000);
    uint32_t otherSelector = UINT32_C(0x80000800);
    uint32_t bridgeSelector = UINT32_C(0x80000000);

    ClearBytes(&sContext.PciVcpu, sizeof(sContext.PciVcpu));
    sContext.PciForwardCount = 0;
    qualification = UINT64_C(3) |
        ((uint64_t)HV_GPU_GUEST_FILTER_PCI_CONFIG_FIRST << 16);
    rax = targetSelector;
    ioStatus = HvGpuGuestFilterHandleIo(&sFilterPolicy, &sContext.PciVcpu,
        qualification, 1, &rax, &advanceRip, ForwardPci, &sContext);
    if (ioStatus != HV_GPU_GUEST_FILTER_IO_HANDLED || advanceRip != 1 ||
        sContext.PciVcpu.ConfigAddress != targetSelector) return 0;

    qualification = UINT64_C(3) | UINT64_C(8) |
        ((uint64_t)HV_GPU_GUEST_FILTER_PCI_DATA_FIRST << 16);
    rax = 0;
    ioStatus = HvGpuGuestFilterHandleIo(&sFilterPolicy, &sContext.PciVcpu,
        qualification, 1, &rax, &advanceRip, ForwardPci, &sContext);
    if (ioStatus != HV_GPU_GUEST_FILTER_IO_HANDLED ||
        (uint32_t)rax != UINT32_MAX || advanceRip != 1 ||
        sContext.PciForwardCount != 0) return 0;

    rax = otherSelector;
    qualification = UINT64_C(3) |
        ((uint64_t)HV_GPU_GUEST_FILTER_PCI_CONFIG_FIRST << 16);
    ioStatus = HvGpuGuestFilterHandleIo(&sFilterPolicy, &sContext.PciVcpu,
        qualification, 1, &rax, &advanceRip, ForwardPci, &sContext);
    if (ioStatus != HV_GPU_GUEST_FILTER_IO_HANDLED || advanceRip != 1 ||
        sContext.PciVcpu.ConfigAddress != otherSelector) return 0;
    qualification = UINT64_C(3) | UINT64_C(8) |
        ((uint64_t)HV_GPU_GUEST_FILTER_PCI_DATA_FIRST << 16);
    rax = 0;
    ioStatus = HvGpuGuestFilterHandleIo(&sFilterPolicy, &sContext.PciVcpu,
        qualification, 1, &rax, &advanceRip, ForwardPci, &sContext);
    if (ioStatus != HV_GPU_GUEST_FILTER_IO_HANDLED ||
        (uint32_t)rax != UINT32_C(0x12345678) ||
        sContext.PciForwardCount != 1) return 0;

    rax = bridgeSelector;
    qualification = UINT64_C(3) |
        ((uint64_t)HV_GPU_GUEST_FILTER_PCI_CONFIG_FIRST << 16);
    ioStatus = HvGpuGuestFilterHandleIo(&sFilterPolicy, &sContext.PciVcpu,
        qualification, 1, &rax, &advanceRip, ForwardPci, &sContext);
    if (ioStatus != HV_GPU_GUEST_FILTER_IO_HANDLED || advanceRip != 1)
        return 0;
    rax = UINT32_C(0xfeedc0de);
    qualification = UINT64_C(3) |
        ((uint64_t)HV_GPU_GUEST_FILTER_PCI_DATA_FIRST << 16);
    ioStatus = HvGpuGuestFilterHandleIo(&sFilterPolicy, &sContext.PciVcpu,
        qualification, 1, &rax, &advanceRip, ForwardPci, &sContext);
    return ioStatus == HV_GPU_GUEST_FILTER_IO_INJECT_GP && advanceRip == 0 &&
        sContext.PciForwardCount == 1;
}

static uint32_t PrepareModel(HV_GPU_ENFORCEMENT_MODEL_EMIT emit)
{
    ClearBytes(&sContext, sizeof(sContext));
    ClearBytes(&sGpuState, sizeof(sGpuState));
    ClearBytes(&sAdapter, sizeof(sAdapter));
    ClearBytes(&sAllocation, sizeof(sAllocation));
    ClearBytes(&sEntities, sizeof(sEntities));
    ClearBytes(sConfig, sizeof(sConfig));
    ClearBytes(sFramebuffer, sizeof(sFramebuffer));
    ClearBytes(&sWcWindow, sizeof(sWcWindow));
    sWcPrepared = 0;

    sAllocation.PhysicalBase = MODEL_FRAMEBUFFER_PHYSICAL;
    sAllocation.PinnedBytes = HV_GPU_MAX_BYTES;
    sAllocation.GgttBase = UINT32_C(0x01234000);
    sAllocation.Pipe = 0;
    sAllocation.Width = 64;
    sAllocation.Height = 64;
    sAllocation.StrideBytes = 256;
    sContext.Ownership = 1;
    sContext.GgttAddressing = 1;
    sContext.PhysicalOutputVerified = 1;
    sContext.BadPteIndex = MODEL_INVALID_INDEX;

    WriteConfig16(0x00, UINT16_C(0x8086));
    WriteConfig16(0x02, HV_GPU_INTEL_UHD770_ID);
    WriteConfig16(0x04, UINT16_C(0x0002));
    sConfig[0x0b] = UINT8_C(0x03);
    sConfig[0x0e] = 0;
    WriteConfig32(0x10, UINT32_C(0xc0000004));
    WriteConfig32(0x14, 0);
    if (HvGpuIntelParsePci(sConfig, sizeof(sConfig), &sAdapter) != HV_GPU_OK ||
        sAdapter.VendorId != UINT16_C(0x8086) ||
        sAdapter.DeviceId != HV_GPU_INTEL_UHD770_ID ||
        sAdapter.MmioPhysical != MODEL_BAR0_BASE) {
        emit("GPU_ENFORCEMENT_MODEL:FAIL pci-config-model");
        return 0;
    }
    emit("GPU_ENFORCEMENT_MODEL:PCI_CONFIG_DISCOVERY PASS");

    InitializePlaneRegisters();
    sFramebuffer[0] = MODEL_FRAMEBUFFER_SENTINEL;
    sEntities.Count = 1;
    sEntities.Entities[0].Coordinates = HV_OVERLAY_SCREEN;
    sEntities.Entities[0].X = 2;
    sEntities.Entities[0].Y = 2;
    sEntities.Entities[0].Width = 4;
    sEntities.Entities[0].Height = 4;
    sEntities.Entities[0].Color = UINT32_C(0xffff0000);

    if (!BuildIommuModel(emit)) return 0;
    if (!InitializeFilterModel()) {
        emit("GPU_ENFORCEMENT_MODEL:FAIL guest-filter-prepare");
        return 0;
    }
    emit("GPU_ENFORCEMENT_MODEL:GUEST_FILTER_POLICY_IO_BITMAP PASS");
    if (!PrepareWcModel(emit)) {
        return 0;
    }
    return 1;
}

static uint32_t TestWcBoundsAndExtent(void)
{
    uint32_t *view = (uint32_t *)(uintptr_t)1;
    HV_GPU_ALLOCATION invalid;
    HV_GPU_EXTENT extent;
    CopyBytes(&invalid, &sAllocation, sizeof(invalid));
    if (HvEfiGpuWcMapWc(&sWcWindow, sAllocation.PhysicalBase,
            HV_EFI_GPU_WC_MAX_BYTES + HV_EFI_GPU_WC_PAGE_SIZE, &view) != 0 ||
        view != NULL || sWcWindow.Active != 0 ||
        sWcWindow.ActivePageCount != 0) return 0;
    invalid.PinnedBytes += HV_GPU_PAGE_BYTES;
    if (HvGpuValidateExtent(&invalid, &extent) !=
        HV_GPU_INVALID_ARGUMENT) return 0;
    CopyBytes(&invalid, &sAllocation, sizeof(invalid));
    invalid.PhysicalBase += 1;
    if (HvGpuValidateExtent(&invalid, &extent) !=
        HV_GPU_INVALID_ARGUMENT) return 0;
    return sContext.CpuStateReads != 0 && sContext.MtrrReads != 0 &&
        sWcWindow.PatIndex == 4u && sWcWindow.PatBits == UINT64_C(0x80) &&
        sWcWindow.AliasSplitCount == 4u;
}

static uint32_t TestSuccessfulAdmissionAndRender(void)
{
    HV_GPU_EXTENT extent;
    uint32_t index;
    const uint32_t expectedPixel = 2u + 2u * 64u;
    ResetHalState();
    if (!InitializeGpu() || sGpuState.Phase != HV_GPU_READY ||
        sContext.ProofReads == 0 || sContext.AuditReads == 0 ||
        sContext.RegisterReads == 0 || sContext.PteReads < MODEL_GGTT_PAGES ||
        HvGpuValidateExtent(&sAllocation, &extent) != HV_GPU_OK ||
        extent.Bytes != UINT64_C(16384) || extent.MappingBytes != 16384 ||
        extent.PageCount != 4 || sContext.MapCalls != 0 ||
        sWcWindow.Active != 0) return 0;
    if (HvGpuRender(&sGpuState, &sEntities, NULL) != HV_GPU_OK ||
        sFramebuffer[expectedPixel] != UINT32_C(0xffff0000) ||
        sFramebuffer[0] != MODEL_FRAMEBUFFER_SENTINEL ||
        sGpuState.Phase != HV_GPU_READY || sGpuState.CpuMappingActive != 0 ||
        sContext.MapCalls != 1 || sContext.MapActive != 0 ||
        sContext.DrainCalls != 1 || sContext.UnmapCalls != 1 ||
        sContext.WcLeavesVerified != 1 ||
        sContext.WcLeafReads != HV_EFI_GPU_WC_PT_PAGE_COUNT *
            HV_EFI_GPU_WC_ENTRY_COUNT ||
        sContext.CleanupOrderValid != 1 || sContext.StoreFences != 1 ||
        sWcWindow.Active != 0 || sWcWindow.ActivePageCount != 0 ||
        sContext.CpuStateReads < 2 || sContext.MtrrReads < 2) return 0;
    for (index = 0; index < HV_EFI_GPU_WC_PT_PAGE_COUNT * 512u; ++index) {
        if (sHostPageTables[HV_EFI_GPU_WC_PT_FIRST_PAGE * 512u + index] != 0)
            return 0;
    }
    return sContext.Invalidations >= 4u &&
        sContext.PteReads >= MODEL_GGTT_PAGES * 4u;
}

static uint32_t TestAbsentExclusionLatch(void)
{
    HV_GPU_BACKEND backend;
    ResetHalState();
    sContext.FilterArmed = 0;
    backend = CreateBackend();
    if (HvGpuInitialize(&sGpuState, &sAdapter, &sAllocation, &backend) !=
            HV_GPU_NOT_SUPPORTED ||
        sGpuState.Phase != HV_GPU_OWNERSHIP_LOST ||
        sContext.MapCalls != 0 || sContext.MapActive != 0 ||
        sWcWindow.Active != 0) return 0;
    sContext.FilterArmed = 1;
    return HvGpuInitialize(&sGpuState, &sAdapter, &sAllocation, &backend) ==
        HV_GPU_NOT_SUPPORTED &&
        sGpuState.Phase == HV_GPU_OWNERSHIP_LOST &&
        sContext.MapCalls == 0 && sWcWindow.Active == 0;
}

static uint32_t TestIommuPteLatch(void)
{
    HV_GPU_BACKEND backend;
    uint32_t leaf;
    uint64_t saved;
    if (!FindFramebufferLeaf(&leaf)) return 0;
    saved = sRootTables[leaf];
    sRootTables[leaf] = 0;
    ResetHalState();
    backend = CreateBackend();
    if (HvGpuInitialize(&sGpuState, &sAdapter, &sAllocation, &backend) !=
            HV_GPU_NOT_SUPPORTED || sGpuState.Phase != HV_GPU_OWNERSHIP_LOST ||
        sContext.MapCalls != 0 || sWcWindow.Active != 0) {
        sRootTables[leaf] = saved;
        return 0;
    }
    sRootTables[leaf] = saved;
    return HvGpuInitialize(&sGpuState, &sAdapter, &sAllocation, &backend) ==
        HV_GPU_NOT_SUPPORTED && sGpuState.Phase == HV_GPU_OWNERSHIP_LOST &&
        sContext.MapCalls == 0 && AuditRootDomain();
}

static uint32_t TestIommuContextLatch(void)
{
    HV_GPU_BACKEND backend;
    uint64_t *targetContext = sLegacyTables + (size_t)(1u + sRootPlan.Bus) *
        512u + (size_t)(sRootPlan.Device * 8u + sRootPlan.Function) * 2u;
    uint64_t savedLow = targetContext[0];
    ResetHalState();
    backend = CreateBackend();
    targetContext[0] &= ~UINT64_C(1);
    if (HvGpuInitialize(&sGpuState, &sAdapter, &sAllocation, &backend) !=
            HV_GPU_NOT_SUPPORTED || sGpuState.Phase != HV_GPU_OWNERSHIP_LOST ||
        sContext.MapCalls != 0 || sWcWindow.Active != 0) {
        targetContext[0] = savedLow;
        return 0;
    }
    targetContext[0] = savedLow;
    return HvGpuInitialize(&sGpuState, &sAdapter, &sAllocation, &backend) ==
        HV_GPU_NOT_SUPPORTED && sGpuState.Phase == HV_GPU_OWNERSHIP_LOST &&
        sContext.MapCalls == 0 && AuditRootDomain();
}

static uint32_t TestHalMismatchLatch(uint32_t mismatch)
{
    HV_GPU_BACKEND backend;
    uint32_t registerIndex = MODEL_REG_STRIDE;
    uint32_t savedRegister = 0;
    uint32_t savedBadPte = sContext.BadPteIndex;
    if (mismatch == 1u) {
        registerIndex = MODEL_REG_STRIDE;
        savedRegister = sContext.Registers[registerIndex].Value;
        ++sContext.Registers[registerIndex].Value;
    } else if (mismatch == 2u) {
        registerIndex = MODEL_REG_LIVE;
        savedRegister = sContext.Registers[registerIndex].Value;
        sContext.Registers[registerIndex].Value += UINT32_C(0x1000);
    } else {
        sContext.BadPteIndex = 7;
    }
    ResetHalState();
    /* ResetHalState resets counters and loss controls, not the injected fault. */
    if (mismatch == 3u) sContext.BadPteIndex = 7;
    backend = CreateBackend();
    if (HvGpuInitialize(&sGpuState, &sAdapter, &sAllocation, &backend) !=
            HV_GPU_NOT_SUPPORTED || sGpuState.Phase != HV_GPU_OWNERSHIP_LOST ||
        sContext.MapCalls != 0 || sContext.MapActive != 0 ||
        sWcWindow.Active != 0) {
        if (mismatch == 1u || mismatch == 2u)
            sContext.Registers[registerIndex].Value = savedRegister;
        sContext.BadPteIndex = savedBadPte;
        return 0;
    }
    if (mismatch == 1u || mismatch == 2u)
        sContext.Registers[registerIndex].Value = savedRegister;
    sContext.BadPteIndex = savedBadPte;
    return HvGpuInitialize(&sGpuState, &sAdapter, &sAllocation, &backend) ==
        HV_GPU_NOT_SUPPORTED && sGpuState.Phase == HV_GPU_OWNERSHIP_LOST &&
        sContext.MapCalls == 0 && sWcWindow.Active == 0;
}

static uint32_t TestMapLossCleanup(void)
{
    HV_GPU_BACKEND backend;
    ResetHalState();
    backend = CreateBackend();
    if (HvGpuInitialize(&sGpuState, &sAdapter, &sAllocation, &backend) !=
        HV_GPU_OK) return 0;
    sContext.LoseOnMap = 1;
    if (HvGpuRender(&sGpuState, &sEntities, NULL) != HV_GPU_NOT_SUPPORTED ||
        sGpuState.Phase != HV_GPU_OWNERSHIP_LOST ||
        sGpuState.CpuMappingActive != 0 || sContext.MapCalls != 1 ||
        sContext.MapActive != 0 || sContext.DrainCalls != 1 ||
        sContext.UnmapCalls != 1 || sContext.CleanupOrderValid != 1 ||
        sContext.StoreFences != 1 || sWcWindow.Active != 0 ||
        sWcWindow.ActivePageCount != 0) return 0;
    sContext.Ownership = 1;
    return HvGpuRender(&sGpuState, &sEntities, NULL) == HV_GPU_NOT_SUPPORTED &&
        HvGpuInitialize(&sGpuState, &sAdapter, &sAllocation, &backend) ==
            HV_GPU_NOT_SUPPORTED && sContext.MapCalls == 1 &&
        sContext.MapActive == 0 && sContext.DrainCalls == 1 &&
        sContext.UnmapCalls == 1 && sWcWindow.Active == 0;
}

uint32_t HvGpuEnforcementModelTest(HV_GPU_ENFORCEMENT_MODEL_EMIT emit)
{
    if (emit == NULL) return 0;
    if (!PrepareModel(emit)) {
        emit("GPU_ENFORCEMENT_MODEL:FAIL synthetic-setup");
        return 0;
    }
    emit("GPU_ENFORCEMENT_MODEL:IOMMU_AND_FILTER_EVIDENCE_SYNTHETIC PASS");
    if (!CheckFilterIoModel()) {
        emit("GPU_ENFORCEMENT_MODEL:FAIL guest-filter");
        return 0;
    }
    emit("GPU_ENFORCEMENT_MODEL:PCI_ECAM_BAR_FILTER_SYNTHETIC PASS");
    if (!TestWcBoundsAndExtent()) {
        emit("GPU_ENFORCEMENT_MODEL:FAIL wc-bounds-or-readback");
        return 0;
    }
    emit("GPU_ENFORCEMENT_MODEL:WC_BOUNDS_ALIGNMENT_READBACK_SYNTHETIC PASS");
    if (!TestSuccessfulAdmissionAndRender()) {
        emit("GPU_ENFORCEMENT_MODEL:FAIL admit-render-cleanup");
        return 0;
    }
    emit("GPU_ENFORCEMENT_MODEL:ADMIT_RENDER_AND_UNMAP_MODEL_BUFFER PASS");
    if (!TestAbsentExclusionLatch()) {
        emit("GPU_ENFORCEMENT_MODEL:FAIL absent-exclusion");
        return 0;
    }
    emit("GPU_ENFORCEMENT_MODEL:NOT_SUPPORTED_NO_MAP_LATCH PASS");
    if (!TestIommuPteLatch()) {
        emit("GPU_ENFORCEMENT_MODEL:FAIL root-pte-audit-latch");
        return 0;
    }
    emit("GPU_ENFORCEMENT_MODEL:ROOT_PTE_AUDIT_LATCH PASS");
    if (!TestIommuContextLatch()) {
        emit("GPU_ENFORCEMENT_MODEL:FAIL iommu-context-latch");
        return 0;
    }
    emit("GPU_ENFORCEMENT_MODEL:ROOT_CONTEXT_AUDIT_LATCH PASS");
    if (!TestHalMismatchLatch(1u)) {
        emit("GPU_ENFORCEMENT_MODEL:FAIL plane-latch");
        return 0;
    }
    emit("GPU_ENFORCEMENT_MODEL:PLANE_LATCH PASS");
    if (!TestHalMismatchLatch(2u)) {
        emit("GPU_ENFORCEMENT_MODEL:FAIL live-surface-latch");
        return 0;
    }
    emit("GPU_ENFORCEMENT_MODEL:LIVE_SURFACE_LATCH PASS");
    if (!TestHalMismatchLatch(3u)) {
        emit("GPU_ENFORCEMENT_MODEL:FAIL ggtt-latch");
        return 0;
    }
    emit("GPU_ENFORCEMENT_MODEL:GGTT_LATCH PASS");
    if (!TestMapLossCleanup()) {
        emit("GPU_ENFORCEMENT_MODEL:FAIL wc-loss-drain-unmap");
        return 0;
    }
    emit("GPU_ENFORCEMENT_MODEL:OWNERSHIP_LOSS_DRAIN_UNMAP PASS");
    emit("GPU_ENFORCEMENT_MODEL:ALL_PASS SYNTHETIC_ONLY");
    return 1;
}

#if defined(_MSC_VER)
#pragma code_seg(pop)
#endif
