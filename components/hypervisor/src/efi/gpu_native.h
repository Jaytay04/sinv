#ifndef HYPERSINV_EFI_GPU_NATIVE_H
#define HYPERSINV_EFI_GPU_NATIVE_H

/* Internal native adapter. Public rendering remains behind gpu_hal.h. */
#include "gpu_hal.h"
#include "gpu_guest_filter.h"
#include "gpu_wc.h"
#include "efi_hypervisor.h"
#include "iommu_runtime.h"

#define HV_GPU_NATIVE_MARKER UINT64_C(0x4750554e41543032)
#define HV_GPU_NATIVE_MMIO_BASE UINT64_C(0xc0000000)
#define HV_GPU_NATIVE_DMA_PAGES UINT32_C(32)
#define HV_GPU_NATIVE_MAX_UNITS UINT32_C(8)
#define HV_GPU_NATIVE_MTRRS UINT32_C(32)
#define HV_GPU_NATIVE_OP_CLEANUP UINT32_C(0)
#define HV_GPU_NATIVE_OP_IO UINT32_C(1)
#define HV_GPU_NATIVE_OP_EPT UINT32_C(2)
#define HV_GPU_NATIVE_OP_RENDER UINT32_C(3)
#define HV_GPU_NATIVE_OP_MTRR UINT32_C(4)

typedef struct HV_GPU_NATIVE_CPU {
    HV_EFI_GPU_WC_WINDOW Wc;
    HV_GPU_GUEST_FILTER_VCPU Pci;
    uint64_t ContextPhysical;
    uint64_t MtrrDefault;
    uint64_t MtrrBase[HV_GPU_NATIVE_MTRRS];
    uint64_t MtrrMask[HV_GPU_NATIVE_MTRRS];
    uint32_t MtrrCount;
    uint32_t PhysicalBits;
    uint32_t Installed;
    uint32_t WcInitialized;
} HV_GPU_NATIVE_CPU;

typedef struct HV_GPU_NATIVE_STATE {
    uint64_t Marker;
    uint64_t SessionId;
    uint32_t Bound;
    uint32_t Lost;
    volatile uint32_t Lock;
    volatile uint32_t PciLock;
    uint32_t CpuCount;
    uint32_t OwnerCpu;
    uint32_t CurrentCpu;
    uint32_t UnitCount;
    uint64_t UnitBases[HV_GPU_NATIVE_MAX_UNITS];
    uint64_t SelfPhysical;
    uint32_t SelfPages;
    uint32_t Reserved;
    uint64_t FramebufferPhysical;
    uint64_t FramebufferBytes;
    uint64_t GgttPhysical;
    uint64_t GgttBytes;
    uint64_t AbsentConfigPhysical;
    uint64_t DmaTablesPhysical;
    uint64_t LegacyTablesPhysical;
    uint32_t LegacyTablePages;
    uint32_t PhysicalOutputVerified;
    HV_EFI_IOMMU_LEGACY_TABLE_RESULT LegacyTables;
    HV_EFI_IOMMU_ROOT_DEVICE_PLAN DmaPlan;
    HV_EFI_IOMMU_ROOT_DEVICE_TABLE_RESULT DmaTables;
    HV_GPU_GUEST_FILTER_POLICY Filter;
    HV_GPU_ADAPTER Adapter;
    HV_GPU_ALLOCATION Allocation;
    uint32_t PrivateRangeCount;
    uint32_t ReservedInventory;
    HV_EFI_EPT_DENY_RANGE PrivateRanges[HV_EFI_EPT_MAX_DENY_RANGES];
    HV_GPU_STATE Hal;
    HV_OVERLAY_ENTITY_LIST Entities;
    HV_GPU_NATIVE_CPU Cpus[HV_EFI_MAX_SUPPORTED_PROCESSORS];
} HV_GPU_NATIVE_STATE;

/* Boot-only helpers operate on caller-pinned private state. No allocation. */
int HvGpuNativePrepareCpu(HV_GPU_NATIVE_STATE *state, HV_EFI_CONTEXT *context);
int HvGpuNativeInstallCpu(HV_GPU_NATIVE_STATE *state, HV_EFI_CONTEXT *context);
int HvGpuNativeInitializeWc(HV_GPU_NATIVE_CPU *cpu);
int HvGpuNativeEptLeaf(uint64_t *storage, uint32_t pages, uint64_t physical,
                       uint64_t address, uint64_t **leaf, uint32_t *shift);

/* Copied with .hvscan. Caller saves/restores guest x87/SSE state. Return IO
 * 1=handled/advance, 2=not ours, 3=inject GP, 0=failed. EPT: 3=inject GP,
 * 2=not ours. Cleanup is called by the existing resume and detach paths. */
uint64_t HvGpuNativeResident(HV_EFI_CONTEXT *context, uint32_t operation,
                             uint64_t argument, uint32_t guest64BitMode);

#endif
