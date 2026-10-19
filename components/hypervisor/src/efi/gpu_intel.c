#include "gpu_hal.h"

#include <stddef.h>
#include <stdint.h>

#if defined(_MSC_VER)
#pragma code_seg(push, ".hvscan$m")
#endif

#define HV_GPU_PCI_CONFIG_COMMAND 0x04u
#define HV_GPU_PCI_CONFIG_CLASS 0x0bu
#define HV_GPU_PCI_CONFIG_HEADER_TYPE 0x0eu
#define HV_GPU_PCI_CONFIG_BAR0 0x10u
#define HV_GPU_PCI_COMMAND_MEMORY_ENABLE 0x0002u
#define HV_GPU_PCI_CLASS_DISPLAY 0x03u
#define HV_GPU_PCI_HEADER_NORMAL 0x00u
#define HV_GPU_PCI_BAR_MEMORY_TYPE_MASK 0x06u
#define HV_GPU_PCI_BAR_MEMORY_TYPE_64 0x04u
#define HV_GPU_PCI_BAR_PREFETCHABLE 0x08u

#define HV_GPU_INTEL_PLANE1_PIPE_A 0x70180u
#define HV_GPU_INTEL_PIPE_REGISTER_STRIDE 0x01000u
#define HV_GPU_INTEL_PLANE_STRIDE_OFFSET 0x08u
#define HV_GPU_INTEL_PLANE_SIZE_OFFSET 0x10u
#define HV_GPU_INTEL_PLANE_SURFACE_OFFSET 0x1cu
#define HV_GPU_INTEL_PLANE_SURFLIVE_OFFSET 0x2cu
#define HV_GPU_INTEL_PLANE_OFFSET_OFFSET 0x24u
#define HV_GPU_INTEL_PLANE_POSITION_OFFSET 0x0cu
#define HV_GPU_INTEL_PLANE_COLOR_CONTROL_OFFSET 0x4cu
#define HV_GPU_INTEL_PLANE_AUX_DISTANCE_OFFSET 0x40u
#define HV_GPU_INTEL_PLANE_STRIDE_MASK 0x00000fffu

/* Gen12 GGTT PTE has address[45:12], valid[0]. Bit 1 selects local
 * memory and all other non-address bits are controls/reserved. This adapter
 * contract accepts only ordinary system-memory mappings with no extra flags,
 * further bounded to the 39-bit DMA address profile of Alder Lake-S 4680.
 */
#define HV_GPU_INTEL_GGTT_ADDRESS_MASK UINT64_C(0x0000007ffffff000)
#define HV_GPU_INTEL_GGTT_VALID UINT64_C(0x0000000000000001)

static uint16_t HvGpuReadLe16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] |
                      ((uint16_t)bytes[1] << 8));
}

static uint32_t HvGpuReadLe32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

HV_GPU_STATUS HvGpuIntelParsePci(const uint8_t *config, size_t bytes,
                                HV_GPU_ADAPTER *adapter)
{
    uint16_t vendorId;
    uint16_t deviceId;
    uint16_t command;
    uint32_t barLow;
    uint32_t barHigh;
    uint64_t mmioPhysical;
    uint8_t headerType;
    HV_GPU_ADAPTER parsed;

    if (config == NULL || adapter == NULL) {
        return HV_GPU_INVALID_ARGUMENT;
    }
    if (bytes < (size_t)(HV_GPU_PCI_CONFIG_BAR0 + 8u)) {
        return HV_GPU_INVALID_ARGUMENT;
    }

    vendorId = HvGpuReadLe16(config);
    deviceId = HvGpuReadLe16(config + 2u);
    if (vendorId != UINT16_C(0x8086) ||
        deviceId != HV_GPU_INTEL_UHD770_ID) {
        return HV_GPU_NOT_SUPPORTED;
    }

    if (config[HV_GPU_PCI_CONFIG_CLASS] != HV_GPU_PCI_CLASS_DISPLAY) {
        return HV_GPU_NOT_SUPPORTED;
    }

    headerType = (uint8_t)(config[HV_GPU_PCI_CONFIG_HEADER_TYPE] & 0x7fu);
    if (headerType != HV_GPU_PCI_HEADER_NORMAL) {
        return HV_GPU_NOT_SUPPORTED;
    }

    command = HvGpuReadLe16(config + HV_GPU_PCI_CONFIG_COMMAND);
    if ((command & HV_GPU_PCI_COMMAND_MEMORY_ENABLE) == 0u) {
        return HV_GPU_NOT_SUPPORTED;
    }

    barLow = HvGpuReadLe32(config + HV_GPU_PCI_CONFIG_BAR0);
    barHigh = HvGpuReadLe32(config + HV_GPU_PCI_CONFIG_BAR0 + 4u);
    if ((barLow & 1u) != 0u ||
        (barLow & HV_GPU_PCI_BAR_MEMORY_TYPE_MASK) !=
            HV_GPU_PCI_BAR_MEMORY_TYPE_64 ||
        (barLow & HV_GPU_PCI_BAR_PREFETCHABLE) != 0u) {
        return HV_GPU_NOT_SUPPORTED;
    }

    mmioPhysical = ((uint64_t)barHigh << 32) |
                   (uint64_t)(barLow & UINT32_C(0xfffffff0));
    if (mmioPhysical == 0u ||
        (mmioPhysical & (HV_GPU_PAGE_BYTES - UINT64_C(1))) != 0u) {
        return HV_GPU_NOT_SUPPORTED;
    }

    parsed.VendorId = vendorId;
    parsed.DeviceId = deviceId;
    parsed.MmioPhysical = mmioPhysical;
    *adapter = parsed;
    return HV_GPU_OK;
}

HV_GPU_STATUS HvGpuIntelReadPlane(const HV_GPU_BACKEND *backend,
                                 uint32_t pipe, HV_GPU_PLANE *plane)
{
    uint32_t registerBase;
    HV_GPU_PLANE parsed;

    if (backend == NULL || plane == NULL || backend->ReadRegister == NULL ||
        pipe > 3u) {
        return HV_GPU_INVALID_ARGUMENT;
    }

    registerBase = HV_GPU_INTEL_PLANE1_PIPE_A +
                   (pipe * HV_GPU_INTEL_PIPE_REGISTER_STRIDE);
    if (backend->ReadRegister(backend->Context, registerBase,
                              &parsed.Control) != 1u ||
        backend->ReadRegister(backend->Context,
                              registerBase + HV_GPU_INTEL_PLANE_STRIDE_OFFSET,
                              &parsed.Stride) != 1u ||
        backend->ReadRegister(backend->Context,
                              registerBase + HV_GPU_INTEL_PLANE_SIZE_OFFSET,
                              &parsed.Size) != 1u ||
        backend->ReadRegister(backend->Context,
                              registerBase + HV_GPU_INTEL_PLANE_SURFACE_OFFSET,
                              &parsed.Surface) != 1u ||
        backend->ReadRegister(backend->Context,
                              registerBase + HV_GPU_INTEL_PLANE_SURFLIVE_OFFSET,
                              &parsed.LiveSurface) != 1u ||
        backend->ReadRegister(backend->Context,
                              registerBase + HV_GPU_INTEL_PLANE_OFFSET_OFFSET,
                              &parsed.Offset) != 1u ||
        backend->ReadRegister(backend->Context,
                              registerBase + HV_GPU_INTEL_PLANE_POSITION_OFFSET,
                              &parsed.Position) != 1u ||
        backend->ReadRegister(backend->Context,
                              registerBase +
                                  HV_GPU_INTEL_PLANE_COLOR_CONTROL_OFFSET,
                              &parsed.ColorControl) != 1u ||
        backend->ReadRegister(backend->Context,
                              registerBase + HV_GPU_INTEL_PLANE_AUX_DISTANCE_OFFSET,
                              &parsed.AuxDistance) != 1u) {
        return HV_GPU_MAPPING_FAILED;
    }

    plane->Control = parsed.Control;
    plane->Stride = parsed.Stride;
    plane->Size = parsed.Size;
    plane->Surface = parsed.Surface;
    plane->LiveSurface = parsed.LiveSurface;
    plane->Offset = parsed.Offset;
    plane->Position = parsed.Position;
    plane->ColorControl = parsed.ColorControl;
    plane->AuxDistance = parsed.AuxDistance;
    return HV_GPU_OK;
}

HV_GPU_STATUS HvGpuIntelValidatePlane(const HV_GPU_PLANE *plane,
                                     const HV_GPU_ALLOCATION *allocation)
{
    HV_GPU_EXTENT extent;
    uint64_t sizeValue;
    uint32_t expectedStride;

    if (plane == NULL || HvGpuValidateExtent(allocation, &extent) != HV_GPU_OK) {
        return HV_GPU_INVALID_ARGUMENT;
    }
    expectedStride = allocation->StrideBytes / 64u;

    sizeValue = ((uint64_t)(allocation->Height - 1u) << 16) |
                (uint64_t)(allocation->Width - 1u);
    if (plane->Control !=
            (HV_GPU_INTEL_PLANE_ENABLE | HV_GPU_INTEL_XRGB8888) ||
        plane->Stride != expectedStride ||
        plane->Size != (uint32_t)sizeValue ||
        plane->Surface != allocation->GgttBase ||
        plane->LiveSurface != allocation->GgttBase ||
        plane->Offset != 0u || plane->Position != 0u ||
        plane->ColorControl != HV_GPU_INTEL_COLOR_GAMMA_DISABLE ||
        plane->AuxDistance != 0u) {
        return HV_GPU_NOT_SUPPORTED;
    }

    return HV_GPU_OK;
}

HV_GPU_STATUS HvGpuIntelGgttPhysical(uint64_t pte, uint64_t *physical)
{
    uint64_t address;

    if (physical == NULL) {
        return HV_GPU_INVALID_ARGUMENT;
    }

    if ((pte & HV_GPU_INTEL_GGTT_VALID) == 0u ||
        (pte & ~(HV_GPU_INTEL_GGTT_ADDRESS_MASK |
                 HV_GPU_INTEL_GGTT_VALID)) != 0u) {
        return HV_GPU_NOT_SUPPORTED;
    }

    address = pte & HV_GPU_INTEL_GGTT_ADDRESS_MASK;
    *physical = address;
    return HV_GPU_OK;
}

#if defined(_MSC_VER)
#pragma code_seg(pop)
#endif
