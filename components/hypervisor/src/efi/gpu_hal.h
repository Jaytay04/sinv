#ifndef HYPERSINV_EFI_GPU_HAL_H
#define HYPERSINV_EFI_GPU_HAL_H

#include <stddef.h>
#include <stdint.h>
#include "overlay_render.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Dedicated diagnostic display only. Hardware activation is DESIGN-GATED:
 * 1. Physically verify an accessible Intel HDMI/DP connector and attach a
 *    diagnostic display. An enumerated GPU without a monitor proves neither.
 * 2. Before guest launch, the outer VM/device-assignment layer must withhold
 *    the ENTIRE adapter from the guest: PCI enumeration/configuration,
 *    MMIO/apertures, GGTT/command submission, DMA, resets and power control.
 *    Windows uses a different adapter. Disabling/unbinding its WDDM driver,
 *    an idle plane, and register polling do not establish this isolation.
 *    The native boot backend enforces this boundary with PCI filtering,
 *    protected EPT views and a separate read-only scanout DMA domain.
 *    A null backend returns HV_GPU_NOT_SUPPORTED.
 * 3. The boot provider allocates/pins private contiguous pages before EBS,
 *    establishes scanout-read DMA access without exposing other private RAM,
 *    reserves GGTT entries, configures an uncompressed linear XRGB plane,
 *    arms PLANE_SURF and waits for PLANE_SURFLIVE. Only then initialize HAL.
 *
 * Firmware/SMM are TRUSTED platform prerequisites, outside this guest-only
 * exclusion boundary. They must not reprogram the plane/GGTT or write/DMA
 * into its surface, or change physical PAT/MTRR cache policy during rendering.
 * A firmware violation can silently break ownership or cache safety: the
 * latch does not detect transient writes or DMA that leave registers intact.
 * Matching register readback cannot establish that this assumption holds.
 *
 * Linear is a pixel layout, NOT a GGTT bypass. This diagnostic contract uses
 * a known contiguous physical allocation and explicit GGTT PTE validation;
 * graphics and physical addresses need not be equal. No guest GTT is walked.
 * Same-screen overlay over the NVIDIA/Windows desktop is out of scope. It
 * would require cooperating guest display-driver/MPO integration or a larger
 * display-virtualization implementation that arbitrates those resources.
 *
 * No allocation or firmware/Windows calls occur here. The trusted platform
 * owns all state, inputs and callbacks in static hypervisor-private storage.
 * Callback function pointers must refer to relocated resident code, never
 * boot services or code in the original EFI image. Inputs must not alias the
 * framebuffer, and callbacks must not re-enter this serialized HAL.
 * Serialize calls on the owning CPU. Before entering resident HAL code the
 * caller must preserve guest SIMD/FPU state and mask FP exceptions, as for
 * HvOverlayRender itself. Native boot renders the diagnostic pattern from
 * the existing bootstrap exit and calls HvGpuBeforeGuestResume from the
 * existing resume path. Render drains/unmaps before returning normally.
 * Root stores do not cause nested VM-exits; root faults use existing policy.
 */

#define HV_GPU_PAGE_BYTES UINT64_C(4096)
#define HV_GPU_MAX_BYTES HV_OVERLAY_MAX_FRAMEBUFFER_BYTES
#define HV_GPU_MAX_PAGES UINT32_C(2048)
#define HV_GPU_INTEL_UHD770_ID UINT16_C(0x4680)
#define HV_GPU_INTEL_PLANE_ENABLE UINT32_C(0x80000000)
#define HV_GPU_INTEL_XRGB8888 UINT32_C(0x04000000)
#define HV_GPU_INTEL_COLOR_GAMMA_DISABLE UINT32_C(0x00002000)

typedef enum HV_GPU_STATUS {
    HV_GPU_OK = 0,
    HV_GPU_NOT_SUPPORTED = 1,
    HV_GPU_INVALID_ARGUMENT = 2,
    HV_GPU_MAPPING_FAILED = 3,
    HV_GPU_RENDER_FAILED = 4
} HV_GPU_STATUS;

typedef enum HV_GPU_PHASE {
    HV_GPU_NEW = 0,
    HV_GPU_READY = 1,
    HV_GPU_OWNERSHIP_LOST = 2,
    HV_GPU_STOPPED = 3
} HV_GPU_PHASE;

typedef struct HV_GPU_ADAPTER {
    uint16_t VendorId;
    uint16_t DeviceId;
    uint64_t MmioPhysical;
} HV_GPU_ADAPTER;

/* Pipe A-D, universal plane 1 only, Alder Lake desktop 8086:4680 initially.
 * Explicit device whitelist: unsupported Intel/AMD/NVIDIA return unsupported.
 * Panning, rotation, tiling, compression, alpha and color conversion are not
 * accepted. The provider supplies the simple diagnostic configuration below.
 */
typedef struct HV_GPU_PLANE {
    uint32_t Control;
    uint32_t Stride;
    uint32_t Size;
    uint32_t Surface;
    uint32_t LiveSurface;
    uint32_t Offset;
    uint32_t Position;
    uint32_t ColorControl;
    uint32_t AuxDistance;
} HV_GPU_PLANE;

typedef struct HV_GPU_ALLOCATION {
    uint64_t PhysicalBase;
    uint64_t PinnedBytes;       /* Page aligned, <= 8 MiB, retained until reboot. */
    uint32_t GgttBase;          /* Reserved graphics address, page aligned. */
    uint32_t Pipe;              /* A=0, B=1, C=2, D=3. */
    uint32_t Width;
    uint32_t Height;
    uint32_t StrideBytes;
} HV_GPU_ALLOCATION;

typedef struct HV_GPU_EXTENT {
    uint64_t Bytes;             /* Declared renderer extent. */
    uint64_t MappingBytes;      /* Extent rounded to pages, never > PinnedBytes. */
    uint32_t PageCount;
} HV_GPU_EXTENT;

#define HV_GPU_EXCLUSION_VERSION UINT32_C(2)
#define HV_GPU_EXCLUSION_BOOT_VPCI_EPT_VTD UINT32_C(1)
#define HV_GPU_EXCLUDE_PCI_ENUMERATION UINT32_C(0x001)
#define HV_GPU_EXCLUDE_CONFIG_ACCESS UINT32_C(0x002)
#define HV_GPU_EXCLUDE_MMIO_APERTURES UINT32_C(0x004)
#define HV_GPU_EXCLUDE_GGTT_COMMANDS UINT32_C(0x008)
#define HV_GPU_EXCLUDE_DMA_DOMAIN UINT32_C(0x010)
#define HV_GPU_EXCLUDE_RESET_POWER UINT32_C(0x020)
#define HV_GPU_EXCLUDE_PRIVATE_STATE UINT32_C(0x040)
#define HV_GPU_EXCLUDE_FRAMEBUFFER UINT32_C(0x080)
#define HV_GPU_EXCLUDE_ALL_CPUS UINT32_C(0x100)
/* Legacy name retained for source compatibility. Version 2 means guest
 * control paths are excluded; it does NOT attest isolation from SMM. */
#define HV_GPU_EXCLUDE_GUEST_CONTROL_PATHS UINT32_C(0x200)
#define HV_GPU_EXCLUDE_FIRMWARE_WRITERS HV_GPU_EXCLUDE_GUEST_CONTROL_PATHS
#define HV_GPU_EXCLUSION_REQUIRED UINT32_C(0x3ff)

/* Readback of an already-installed boot exclusion policy. This is NOT an
 * activation request, credential, JSON configuration, or substitute for the
 * enforcement provider. Only trusted resident platform code can supply it,
 * after querying installed PCI filters, EPT and VT-d protection on every CPU.
 * Version 2 explicitly trusts firmware/SMM; the record attests guest control
 * exclusion only. It cannot prove or detect every firmware ownership breach.
 * SessionId is generated once before guest launch and cannot change while
 * scanout remains active. Adapter/allocation binding prevents reuse of an
 * unrelated reservation. Tests explicitly simulate this readback in RAM.
 */
typedef struct HV_GPU_EXCLUSION_PROOF {
    uint32_t Size;
    uint32_t Version;
    uint32_t Method;
    uint32_t EnforcementMask;
    uint32_t GuestAccessMask;
    uint32_t LockedUntilReboot;
    uint32_t PhysicalOutputVerified;
    uint32_t Reserved;
    uint64_t SessionId;
    HV_GPU_ADAPTER Adapter;
    HV_GPU_ALLOCATION Allocation;
} HV_GPU_EXCLUSION_PROOF;

typedef struct HV_GPU_BACKEND {
    /* Trusted callbacks are bound to the supplied adapter. All uint32_t
     * callbacks return exactly 1 for success/true; all other values fail. */
    void *Context;
    uint32_t (*ReadExclusion)(void *context, HV_GPU_EXCLUSION_PROOF *proof);
    /* Must attest ENFORCED full-adapter exclusion described above, on every
     * call. Never infer ownership from the values of display registers. */
    uint32_t (*OwnershipHeld)(void *context);
    /* Validate that the plane uses system-memory GGTT addressing and that
     * display page tables (DPT) are disabled. Never interpret a DPT offset
     * as a GGTT address. Must remain true while ownership is held. */
    uint32_t (*AddressingIsGgtt)(void *context);
    uint32_t (*ReadRegister)(void *context, uint32_t offset, uint32_t *value);
    uint32_t (*ReadGgttPte)(void *context, uint32_t index, uint64_t *value);
    /* Map only this pinned physical range, RW+NX with effective WC verified
     * against host PAT/MTRRs and NO conflicting WB aliases. Use a separate
     * window; scanner.cpp and its read-only window must remain unchanged.
     * A failed/partial map is followed by DrainWrites and UnmapWc as well.
     * UnmapWc is idempotent, clears all leaves and invalidates local TLBs.
     * DrainWrites must issue the required store fence before PTE removal.
     * Both cleanup callbacks are infallible; retain GPU mappings/pinned RAM.
     */
    uint32_t (*MapWc)(void *context, uint64_t physical, uint64_t bytes,
                      uint32_t **view);
    void (*DrainWrites)(void *context);
    void (*UnmapWc)(void *context);
} HV_GPU_BACKEND;

/* Zero-initialize ONCE in private static pages at boot. There is deliberately
 * no reset/re-arm API: lost ownership or shutdown is terminal until reboot.
 * Do not release the allocation/GGTT mapping on loss; scanout may still read it.
 */
typedef struct HV_GPU_STATE {
    uint32_t Phase;
    uint32_t CpuMappingActive;
    uint64_t SessionId;
    HV_GPU_ADAPTER Adapter;
    HV_GPU_ALLOCATION Allocation;
    HV_GPU_EXTENT Extent;
    HV_GPU_BACKEND Backend;
} HV_GPU_STATE;

HV_GPU_STATUS HvGpuIntelParsePci(const uint8_t *config, size_t bytes,
                                HV_GPU_ADAPTER *adapter);
HV_GPU_STATUS HvGpuIntelReadPlane(const HV_GPU_BACKEND *backend,
                                 uint32_t pipe, HV_GPU_PLANE *plane);
HV_GPU_STATUS HvGpuIntelValidatePlane(const HV_GPU_PLANE *plane,
                                     const HV_GPU_ALLOCATION *allocation);
HV_GPU_STATUS HvGpuIntelGgttPhysical(uint64_t pte, uint64_t *physical);
HV_GPU_STATUS HvGpuValidateExtent(const HV_GPU_ALLOCATION *allocation,
                                  HV_GPU_EXTENT *extent);
HV_GPU_STATUS HvGpuValidateExclusion(const HV_GPU_EXCLUSION_PROOF *proof,
                                     const HV_GPU_ADAPTER *adapter,
                                     const HV_GPU_ALLOCATION *allocation,
                                     uint64_t expectedSessionId);

HV_GPU_STATUS HvGpuInitialize(HV_GPU_STATE *state,
                              const HV_GPU_ADAPTER *adapter,
                              const HV_GPU_ALLOCATION *allocation,
                              const HV_GPU_BACKEND *backend);
HV_GPU_STATUS HvGpuRender(HV_GPU_STATE *state,
                          const HV_OVERLAY_ENTITY_LIST *entities,
                          const HV_OVERLAY_MATRIX *viewProjection);
void HvGpuBeforeGuestResume(HV_GPU_STATE *state);
void HvGpuShutdown(HV_GPU_STATE *state);

#ifdef __cplusplus
}
#endif
#endif
