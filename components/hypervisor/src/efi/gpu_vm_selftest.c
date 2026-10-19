#include "gpu_vm_selftest.h"

#include <stddef.h>
#include <stdint.h>

#include "gpu_hal.h"

/* Pre-launch model code and callbacks stay in ordinary EFI .text. */
#if defined(_MSC_VER)
#pragma code_seg(push, ".text$gpuvm")
#endif

#define CONFIG_BYTES UINT32_C(256)
#define REGISTER_COUNT UINT32_C(9)
#define FRAME_WORDS UINT32_C(1024)
#define FRAME_SENTINEL UINT32_C(0xff123456)
#define PTE_INDEX (UINT32_C(0x01234000) / (uint32_t)HV_GPU_PAGE_BYTES)

enum { REG_CONTROL, REG_STRIDE, REG_SIZE, REG_SURFACE, REG_LIVE,
       REG_OFFSET, REG_POSITION, REG_COLOR, REG_AUX };
enum { BAD_PLANE = 1, BAD_LIVE, BAD_PTE };

typedef struct MODEL_REGISTER { uint32_t Offset, Value; } MODEL_REGISTER;
typedef struct MODEL_CONTEXT {
    uint32_t Ownership, Ggtt, LoseOnMap, Maps, MapActive;
    uint32_t Drains, Unmaps, LastCleanup, CleanupOrderValid;
    uint32_t ProofReads, RegisterReads, PteReads;
    uint64_t Pte;
    MODEL_REGISTER Registers[REGISTER_COUNT];
    HV_GPU_EXCLUSION_PROOF Proof;
} MODEL_CONTEXT;

static uint8_t sConfig[CONFIG_BYTES];
static uint32_t sFramebuffer[FRAME_WORDS];
static MODEL_CONTEXT sContext;
static HV_GPU_STATE sState;
static HV_GPU_ADAPTER sAdapter;
static HV_GPU_ALLOCATION sAllocation;
static HV_OVERLAY_ENTITY_LIST sEntities;

static void Clear(void *memory, size_t bytes)
{
    volatile uint8_t *p = (volatile uint8_t *)memory;
    while (bytes-- != 0) *p++ = 0;
}

static void Copy(void *destination, const void *source, size_t bytes)
{
    volatile uint8_t *to = (volatile uint8_t *)destination;
    const volatile uint8_t *from = (const volatile uint8_t *)source;
    while (bytes-- != 0) *to++ = *from++;
}

static void Write16(uint32_t offset, uint16_t value)
{
    sConfig[offset] = (uint8_t)value;
    sConfig[offset + 1] = (uint8_t)(value >> 8);
}

static void Write32(uint32_t offset, uint32_t value)
{
    sConfig[offset] = (uint8_t)value;
    sConfig[offset + 1] = (uint8_t)(value >> 8);
    sConfig[offset + 2] = (uint8_t)(value >> 16);
    sConfig[offset + 3] = (uint8_t)(value >> 24);
}

static void InstallProof(void)
{
    HV_GPU_EXCLUSION_PROOF *p = &sContext.Proof;
    p->Size = (uint32_t)sizeof(*p);
    p->Version = HV_GPU_EXCLUSION_VERSION;
    p->Method = HV_GPU_EXCLUSION_BOOT_VPCI_EPT_VTD;
    p->EnforcementMask = HV_GPU_EXCLUSION_REQUIRED;
    p->GuestAccessMask = 0;
    p->LockedUntilReboot = 1;
    p->PhysicalOutputVerified = 1;
    p->Reserved = 0;
    p->SessionId = UINT64_C(0x1234);
    Copy(&p->Adapter, &sAdapter, sizeof(p->Adapter));
    Copy(&p->Allocation, &sAllocation, sizeof(p->Allocation));
}

static void Prepare(void)
{
    uint32_t i, base, size;
    Clear(&sContext, sizeof(sContext));
    Clear(&sState, sizeof(sState));
    Clear(&sAdapter, sizeof(sAdapter));
    Clear(&sAllocation, sizeof(sAllocation));
    Clear(&sEntities, sizeof(sEntities));
    sAllocation.PhysicalBase = UINT64_C(0x04000000);
    sAllocation.PinnedBytes = HV_GPU_PAGE_BYTES;
    sAllocation.GgttBase = UINT32_C(0x01234000);
    sAllocation.Width = 16;
    sAllocation.Height = 16;
    sAllocation.StrideBytes = 128;
    sContext.Ownership = sContext.Ggtt = 1;
    sContext.Pte = sAllocation.PhysicalBase | UINT64_C(1);

    base = UINT32_C(0x70180) + sAllocation.Pipe * UINT32_C(0x1000);
    size = ((sAllocation.Height - 1) << 16) | (sAllocation.Width - 1);
    sContext.Registers[REG_CONTROL].Offset = base;
    sContext.Registers[REG_CONTROL].Value =
        HV_GPU_INTEL_PLANE_ENABLE | HV_GPU_INTEL_XRGB8888;
    sContext.Registers[REG_STRIDE].Offset = base + 0x08;
    sContext.Registers[REG_STRIDE].Value = sAllocation.StrideBytes / 64;
    sContext.Registers[REG_SIZE].Offset = base + 0x10;
    sContext.Registers[REG_SIZE].Value = size;
    sContext.Registers[REG_SURFACE].Offset = base + 0x1c;
    sContext.Registers[REG_SURFACE].Value = sAllocation.GgttBase;
    sContext.Registers[REG_LIVE].Offset = base + 0x2c;
    sContext.Registers[REG_LIVE].Value = sAllocation.GgttBase;
    sContext.Registers[REG_OFFSET].Offset = base + 0x24;
    sContext.Registers[REG_POSITION].Offset = base + 0x0c;
    sContext.Registers[REG_COLOR].Offset = base + 0x4c;
    sContext.Registers[REG_COLOR].Value = HV_GPU_INTEL_COLOR_GAMMA_DISABLE;
    sContext.Registers[REG_AUX].Offset = base + 0x40;

    for (i = 0; i < FRAME_WORDS; ++i) sFramebuffer[i] = FRAME_SENTINEL;
    Clear(sConfig, sizeof(sConfig));
    Write16(0x00, UINT16_C(0x8086));
    Write16(0x02, HV_GPU_INTEL_UHD770_ID);
    Write16(0x04, UINT16_C(0x0002));
    sConfig[0x0b] = UINT8_C(0x03);
    sConfig[0x0e] = 0;
    Write32(0x10, UINT32_C(0xf0000004));
    Write32(0x14, 0);
    if (HvGpuIntelParsePci(sConfig, sizeof(sConfig), &sAdapter) == HV_GPU_OK)
        InstallProof();

    sEntities.Count = 1;
    sEntities.Entities[0].Coordinates = HV_OVERLAY_SCREEN;
    sEntities.Entities[0].X = sEntities.Entities[0].Y = 2;
    sEntities.Entities[0].Width = sEntities.Entities[0].Height = 4;
    sEntities.Entities[0].Color = UINT32_C(0xffff0000);
}

static uint32_t ReadProof(void *opaque, HV_GPU_EXCLUSION_PROOF *proof)
{
    MODEL_CONTEXT *c = (MODEL_CONTEXT *)opaque;
    if (proof == NULL) return 0;
    ++c->ProofReads;
    Copy(proof, &c->Proof, sizeof(*proof));
    return 1;
}

static uint32_t Ownership(void *opaque)
{ return ((MODEL_CONTEXT *)opaque)->Ownership == 1 ? 1u : 0u; }

static uint32_t Addressing(void *opaque)
{ return ((MODEL_CONTEXT *)opaque)->Ggtt == 1 ? 1u : 0u; }

static uint32_t ReadRegister(void *opaque, uint32_t offset, uint32_t *value)
{
    MODEL_CONTEXT *c = (MODEL_CONTEXT *)opaque;
    uint32_t i;
    if (value == NULL) return 0;
    ++c->RegisterReads;
    for (i = 0; i < REGISTER_COUNT; ++i) {
        if (c->Registers[i].Offset == offset) {
            *value = c->Registers[i].Value;
            return 1;
        }
    }
    return 0;
}

static uint32_t ReadPte(void *opaque, uint32_t index, uint64_t *value)
{
    MODEL_CONTEXT *c = (MODEL_CONTEXT *)opaque;
    if (value == NULL || index != PTE_INDEX) return 0;
    ++c->PteReads;
    *value = c->Pte;
    return 1;
}

static uint32_t MapWc(void *opaque, uint64_t physical, uint64_t bytes,
                     uint32_t **view)
{
    MODEL_CONTEXT *c = (MODEL_CONTEXT *)opaque;
    ++c->Maps;
    if (view == NULL || physical != sAllocation.PhysicalBase ||
        bytes != HV_GPU_PAGE_BYTES) return 0;
    *view = sFramebuffer;
    c->MapActive = 1;
    if (c->LoseOnMap != 0) c->Ownership = 0;
    return 1;
}

static void Drain(void *opaque)
{
    MODEL_CONTEXT *c = (MODEL_CONTEXT *)opaque;
    ++c->Drains;
    c->LastCleanup = 1;
}

static void Unmap(void *opaque)
{
    MODEL_CONTEXT *c = (MODEL_CONTEXT *)opaque;
    ++c->Unmaps;
    c->CleanupOrderValid = c->LastCleanup == 1;
    c->LastCleanup = 2;
    c->MapActive = 0;
}

static HV_GPU_BACKEND Backend(void)
{
    HV_GPU_BACKEND b;
    b.Context = &sContext;
    b.ReadExclusion = ReadProof;
    b.OwnershipHeld = Ownership;
    b.AddressingIsGgtt = Addressing;
    b.ReadRegister = ReadRegister;
    b.ReadGgttPte = ReadPte;
    b.MapWc = MapWc;
    b.DrainWrites = Drain;
    b.UnmapWc = Unmap;
    return b;
}

static uint32_t Initialize(HV_GPU_BACKEND *backend)
{
    if (HvGpuIntelParsePci(sConfig, sizeof(sConfig), &sAdapter) != HV_GPU_OK ||
        sAdapter.VendorId != UINT16_C(0x8086) ||
        sAdapter.DeviceId != HV_GPU_INTEL_UHD770_ID ||
        sAdapter.MmioPhysical != UINT64_C(0xf0000000)) return 0;
    InstallProof();
    return HvGpuInitialize(&sState, &sAdapter, &sAllocation, backend) ==
        HV_GPU_OK;
}

static uint32_t TestRender(HV_GPU_VM_SELFTEST_EMIT emit)
{
    HV_GPU_BACKEND b;
    uint32_t pixel;
    Prepare();
    pixel = 2 + 2 * (sAllocation.StrideBytes / 4);
    b = Backend();
    if (!Initialize(&b)) {
        emit("GPU_HAL_MODEL:FAIL valid-init");
        return 0;
    }
    emit("GPU_HAL_MODEL:PCI_DISCOVERY_SYNTHETIC PASS");
    emit("GPU_HAL_MODEL:EXCLUSION_PROOF SIMULATED");
    emit("GPU_HAL_MODEL:PLANE_AND_GGTT_VALID PASS");
    if (HvGpuRender(&sState, &sEntities, NULL) != HV_GPU_OK ||
        sFramebuffer[pixel] != UINT32_C(0xffff0000) ||
        sFramebuffer[0] != FRAME_SENTINEL || b.Context == NULL ||
        sContext.Maps != 1 || sContext.MapActive != 0 ||
        sContext.Drains != 1 || sContext.Unmaps != 1 ||
        sContext.CleanupOrderValid != 1 || sState.CpuMappingActive != 0 ||
        sState.Phase != HV_GPU_READY ||
        sState.Allocation.PhysicalBase != sAllocation.PhysicalBase ||
        sContext.ProofReads < 2 || sContext.PteReads == 0) {
        emit("GPU_HAL_MODEL:FAIL render-or-cleanup");
        return 0;
    }
    emit("GPU_HAL_MODEL:RENDER_RED_BOX PASS");
    emit("GPU_HAL_MODEL:MAP_DRAIN_UNMAP PASS");
    return 1;
}

static uint32_t TestMismatch(uint32_t kind)
{
    HV_GPU_BACKEND b;
    uint32_t reg = UINT32_MAX, saved = 0;
    Prepare();
    b = Backend();
    if (kind == BAD_PLANE) {
        reg = REG_STRIDE;
        saved = sContext.Registers[reg].Value;
        ++sContext.Registers[reg].Value;
    } else if (kind == BAD_LIVE) {
        reg = REG_LIVE;
        saved = sContext.Registers[reg].Value;
        sContext.Registers[reg].Value += UINT32_C(0x1000);
    } else {
        sContext.Pte += HV_GPU_PAGE_BYTES;
    }
    if (HvGpuInitialize(&sState, &sAdapter, &sAllocation, &b) !=
            HV_GPU_NOT_SUPPORTED || sState.Phase != HV_GPU_OWNERSHIP_LOST ||
        sContext.Maps != 0 || sContext.MapActive != 0 ||
        sState.CpuMappingActive != 0) return 0;
    if (kind == BAD_PTE) sContext.Pte = sAllocation.PhysicalBase | 1;
    else sContext.Registers[reg].Value = saved;
    return HvGpuInitialize(&sState, &sAdapter, &sAllocation, &b) ==
            HV_GPU_NOT_SUPPORTED &&
        HvGpuRender(&sState, &sEntities, NULL) == HV_GPU_NOT_SUPPORTED &&
        sState.Phase == HV_GPU_OWNERSHIP_LOST && sContext.Maps == 0 &&
        sContext.MapActive == 0 &&
        sState.Allocation.PhysicalBase == sAllocation.PhysicalBase;
}

static uint32_t TestRejectedOwnership(void)
{
    HV_GPU_BACKEND b;
    Prepare();
    b = Backend();
    sContext.Proof.GuestAccessMask = HV_GPU_EXCLUDE_PCI_ENUMERATION;
    if (HvGpuInitialize(&sState, &sAdapter, &sAllocation, &b) !=
            HV_GPU_NOT_SUPPORTED || sContext.Maps != 0 ||
        sContext.MapActive != 0 || sState.Phase != HV_GPU_OWNERSHIP_LOST)
        return 0;
    Prepare();
    b = Backend();
    sContext.Ownership = 0;
    if (HvGpuInitialize(&sState, &sAdapter, &sAllocation, &b) !=
            HV_GPU_NOT_SUPPORTED || sContext.Maps != 0 ||
        sContext.MapActive != 0 || sState.Phase != HV_GPU_OWNERSHIP_LOST)
        return 0;
    Prepare();
    return HvGpuInitialize(&sState, &sAdapter, &sAllocation, NULL) ==
            HV_GPU_NOT_SUPPORTED && sContext.Maps == 0 &&
        sContext.MapActive == 0 && sState.Phase == HV_GPU_OWNERSHIP_LOST;
}

static uint32_t TestLossCleanup(void)
{
    HV_GPU_BACKEND b;
    Prepare();
    b = Backend();
    if (!Initialize(&b)) return 0;
    sContext.LoseOnMap = 1;
    if (HvGpuRender(&sState, &sEntities, NULL) != HV_GPU_NOT_SUPPORTED ||
        sState.Phase != HV_GPU_OWNERSHIP_LOST || sState.CpuMappingActive != 0 ||
        sContext.Maps != 1 || sContext.MapActive != 0 || sContext.Drains != 1 ||
        sContext.Unmaps != 1 || sContext.CleanupOrderValid != 1 ||
        sState.Allocation.PhysicalBase != sAllocation.PhysicalBase ||
        sState.Allocation.PinnedBytes != sAllocation.PinnedBytes ||
        sState.Allocation.GgttBase != sAllocation.GgttBase) return 0;
    sContext.Ownership = 1;
    return HvGpuRender(&sState, &sEntities, NULL) == HV_GPU_NOT_SUPPORTED &&
        HvGpuInitialize(&sState, &sAdapter, &sAllocation, &b) ==
            HV_GPU_NOT_SUPPORTED && sContext.Maps == 1 &&
        sContext.MapActive == 0 && sContext.Drains == 1 && sContext.Unmaps == 1;
}

uint32_t HvGpuVmSelfTest(HV_GPU_VM_SELFTEST_EMIT emit)
{
    if (emit == NULL) return 0;
    if (!TestRender(emit)) return 0;
    if (!TestMismatch(BAD_PLANE)) {
        emit("GPU_HAL_MODEL:FAIL plane-latch"); return 0;
    }
    emit("GPU_HAL_MODEL:PLANE_LATCH PASS");
    if (!TestMismatch(BAD_LIVE)) {
        emit("GPU_HAL_MODEL:FAIL live-surface-latch"); return 0;
    }
    emit("GPU_HAL_MODEL:LIVE_SURFACE_LATCH PASS");
    if (!TestMismatch(BAD_PTE)) {
        emit("GPU_HAL_MODEL:FAIL ggtt-latch"); return 0;
    }
    emit("GPU_HAL_MODEL:GGTT_LATCH PASS");
    if (!TestRejectedOwnership()) {
        emit("GPU_HAL_MODEL:FAIL unsupported-no-map"); return 0;
    }
    emit("GPU_HAL_MODEL:NOT_SUPPORTED_NO_MAP PASS");
    if (!TestLossCleanup()) {
        emit("GPU_HAL_MODEL:FAIL ownership-loss-cleanup"); return 0;
    }
    emit("GPU_HAL_MODEL:OWNERSHIP_LATCH_CLEANUP PASS");
    emit("GPU_HAL_MODEL:ALL_PASS");
    return 1;
}

#if defined(_MSC_VER)
#pragma code_seg(pop)
#endif
