#ifndef HYPERSINV_EFI_GPU_GUEST_FILTER_H
#define HYPERSINV_EFI_GPU_GUEST_FILTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HV_GPU_GUEST_FILTER_PAGE_SIZE UINT64_C(4096)
#define HV_GPU_GUEST_FILTER_PHYS_LIMIT UINT64_C(0x0000008000000000)
#define HV_GPU_GUEST_FILTER_IO_BITMAP_BYTES UINT32_C(4096)
#define HV_GPU_GUEST_FILTER_PCI_CONFIG_FIRST UINT16_C(0x0cf8)
#define HV_GPU_GUEST_FILTER_PCI_CONFIG_LAST UINT16_C(0x0cff)
#define HV_GPU_GUEST_FILTER_PCI_DATA_FIRST UINT16_C(0x0cfc)

typedef struct HV_GPU_GUEST_FILTER_RANGE {
    uint64_t Base;
    uint64_t Bytes;
} HV_GPU_GUEST_FILTER_RANGE;

/* Trusted boot-time policy for one segment-zero Intel adapter. EcamPage is
 * the 4 KiB ECAM page for this exact BDF. BAR ranges cover the full hardware
 * apertures; callers deny EPT access to every covered page on every vCPU. */
typedef struct HV_GPU_GUEST_FILTER_POLICY {
    uint32_t Size;
    uint16_t Segment;
    uint8_t Bus;
    uint8_t Device;
    uint8_t Function;
    uint8_t Reserved[3];
    uint64_t EcamPage;
    HV_GPU_GUEST_FILTER_RANGE Bar0;
    HV_GPU_GUEST_FILTER_RANGE Bar2;
} HV_GPU_GUEST_FILTER_POLICY;

/* One instance per vCPU. The CF8 selector is never forwarded to hardware. */
typedef struct HV_GPU_GUEST_FILTER_VCPU {
    uint32_t ConfigAddress;
} HV_GPU_GUEST_FILTER_VCPU;

typedef enum HV_GPU_GUEST_FILTER_IO_RESULT {
    HV_GPU_GUEST_FILTER_IO_FAILED = 0,
    HV_GPU_GUEST_FILTER_IO_HANDLED = 1,
    HV_GPU_GUEST_FILTER_IO_NOT_OURS = 2,
    HV_GPU_GUEST_FILTER_IO_INJECT_GP = 3
} HV_GPU_GUEST_FILTER_IO_RESULT;

/* Callbacks run only for guest configuration accesses to non-target BDFs;
 * writes to host bridge 00:00.0 are rejected with #GP because the parent maps
 * its ECAM page read-only. This fail-closed profile may reject guest platform
 * operations that write host bridge configuration. The parent owns the shared
 * physical CF8/CFC lock and performs each complete transaction under it.
 * value is input for OUT and output for IN. Return exactly 1 on completion. */
typedef uint32_t (*HV_GPU_GUEST_FILTER_PCI_ACCESS)(
    void *context, uint32_t configAddress, uint16_t dataPort,
    uint32_t widthBytes, uint32_t isInput, uint32_t *value);

int HvGpuGuestFilterValidatePolicy(
    const HV_GPU_GUEST_FILTER_POLICY *policy);

/* ORs interception bits for CF8-CFF into bitmap A (ports 0..7FFF) and
 * preserves every other bit in both maps. Validation uses the same exact
 * 4 KiB bitmap ABI. */
int HvGpuGuestFilterArmIoBitmaps(uint8_t *bitmapA, uint8_t *bitmapB,
                                 uint32_t bitmapBytes);
int HvGpuGuestFilterIoBitmapsValid(const uint8_t *bitmapA,
                                   const uint8_t *bitmapB,
                                   uint32_t bitmapBytes);

/* Qualification is VMX VM-exit qualification for an I/O instruction. For a
 * handled operation, guestRax carries the emulated value and advanceRip is 1;
 * the parent injects #GP for INJECT_GP and latches ownership loss for FAILED.
 * String/REP, unsupported sizes, reserved encodings, CF9 OUT, and accesses
 * crossing selector/data boundaries inject #GP without changing state. */
HV_GPU_GUEST_FILTER_IO_RESULT HvGpuGuestFilterHandleIo(
    const HV_GPU_GUEST_FILTER_POLICY *policy,
    HV_GPU_GUEST_FILTER_VCPU *vcpu,
    uint64_t qualification,
    uint32_t guest64BitMode,
    uint64_t *guestRax,
    uint32_t *advanceRip,
    HV_GPU_GUEST_FILTER_PCI_ACCESS pciAccess,
    void *pciAccessContext);

/* Returns 1 for overlap, 0 for no overlap, and -1 for invalid policy/input.
 * Inputs are checked for overflow and the 39-bit adapter address ceiling; an
 * EPT caller must fail closed when the result is negative. */
int HvGpuGuestFilterRangeProtected(
    const HV_GPU_GUEST_FILTER_POLICY *policy,
    uint64_t physicalBase,
    uint64_t bytes);
int HvGpuGuestFilterPageProtected(
    const HV_GPU_GUEST_FILTER_POLICY *policy,
    uint64_t pageBase);

#ifdef __cplusplus
}
#endif
#endif
