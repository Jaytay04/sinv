#include "gpu_hal.h"

#include <stdio.h>
#include <string.h>

#define TEST_PIPE_COUNT UINT32_C(4)
#define TEST_REGISTER_CAPACITY UINT32_C(40)
#define TEST_FRAME_WORDS (HV_GPU_PAGE_BYTES / sizeof(uint32_t))
#define TEST_FRAME_SENTINEL UINT32_C(0xff345678)

enum TEST_MAP_MODE {
    TEST_MAP_SUCCESS = 0,
    TEST_MAP_FAILURE = 1,
    TEST_MAP_PARTIAL_FAILURE = 2,
    TEST_MAP_NULL_VIEW = 3,
    TEST_MAP_UNALIGNED_VIEW = 4
};

typedef struct TEST_REGISTER {
    uint32_t Offset;
    uint32_t Value;
} TEST_REGISTER;

typedef struct TEST_BACKEND_CONTEXT {
    uint32_t Owned;
    uint32_t GgttMode;
    uint32_t OwnershipCalls;
    uint32_t OwnershipFalseAtCall;
    TEST_REGISTER Registers[TEST_REGISTER_CAPACITY];
    uint32_t RegisterCount;
    uint32_t RegisterReadCalls;
    uint32_t RegisterReadFailureAtCall;
    uint32_t PteBaseIndex;
    uint64_t Ptes[HV_GPU_MAX_PAGES];
    uint32_t PteCount;
    uint32_t PteReadCalls;
    uint32_t PteReadFailureAtCall;
    uint32_t MapMode;
    uint32_t MapCalls;
    uint32_t MapActive;
    uint64_t MappedPhysical;
    uint64_t MappedBytes;
    uint32_t DrainCalls;
    uint32_t UnmapCalls;
    uint32_t Events[16];
    uint32_t EventCount;
    uint32_t Framebuffer[TEST_FRAME_WORDS + 2];
} TEST_BACKEND_CONTEXT;

static const HV_GPU_ADAPTER gValidAdapter = {
    UINT16_C(0x8086), HV_GPU_INTEL_UHD770_ID, UINT64_C(0xf0000000)
};

static const HV_GPU_ALLOCATION gValidAllocation = {
    UINT64_C(0x04000000),
    UINT64_C(4096),
    UINT32_C(0x01234000),
    UINT32_C(0),
    UINT32_C(16),
    UINT32_C(16),
    UINT32_C(128)
};

static TEST_BACKEND_CONTEXT gBackendContext;
static HV_GPU_STATE gState;
static HV_OVERLAY_ENTITY_LIST gEntities;

#define CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: gpu-hal: %s\n", (message)); \
            return 1; \
        } \
    } while (0)

static uint32_t PipeRegisterBase(uint32_t pipe)
{
    return UINT32_C(0x70180) + pipe * UINT32_C(0x1000);
}

static void AddRegister(TEST_BACKEND_CONTEXT *context, uint32_t offset,
                        uint32_t value)
{
    if (context->RegisterCount < TEST_REGISTER_CAPACITY) {
        context->Registers[context->RegisterCount].Offset = offset;
        context->Registers[context->RegisterCount].Value = value;
        ++context->RegisterCount;
    }
}

static void InstallPlane(TEST_BACKEND_CONTEXT *context, uint32_t pipe,
                         const HV_GPU_ALLOCATION *allocation)
{
    uint32_t base = PipeRegisterBase(pipe);
    uint32_t size = ((allocation->Height - UINT32_C(1)) << 16) |
                    (allocation->Width - UINT32_C(1));
    AddRegister(context, base, HV_GPU_INTEL_PLANE_ENABLE |
                               HV_GPU_INTEL_XRGB8888);
    AddRegister(context, base + UINT32_C(0x08),
                allocation->StrideBytes / UINT32_C(64));
    AddRegister(context, base + UINT32_C(0x0c), 0);
    AddRegister(context, base + UINT32_C(0x10), size);
    AddRegister(context, base + UINT32_C(0x1c), allocation->GgttBase);
    AddRegister(context, base + UINT32_C(0x24), 0);
    AddRegister(context, base + UINT32_C(0x2c), allocation->GgttBase);
    AddRegister(context, base + UINT32_C(0x4c), HV_GPU_INTEL_COLOR_GAMMA_DISABLE);
    AddRegister(context, base + UINT32_C(0x40), 0);
}

static void SetRegister(TEST_BACKEND_CONTEXT *context, uint32_t offset,
                        uint32_t value)
{
    uint32_t index;
    for (index = 0; index < context->RegisterCount; ++index) {
        if (context->Registers[index].Offset == offset) {
            context->Registers[index].Value = value;
            return;
        }
    }
    AddRegister(context, offset, value);
}

static uint32_t TestOwnershipHeld(void *opaque)
{
    TEST_BACKEND_CONTEXT *context = (TEST_BACKEND_CONTEXT *)opaque;
    ++context->OwnershipCalls;
    if (context->Owned == 0 ||
        (context->OwnershipFalseAtCall != 0 &&
         context->OwnershipCalls >= context->OwnershipFalseAtCall)) {
        return 0;
    }
    return 1;
}

static uint32_t TestAddressingIsGgtt(void *opaque)
{
    const TEST_BACKEND_CONTEXT *context =
        (const TEST_BACKEND_CONTEXT *)opaque;
    return context->GgttMode;
}

static uint32_t TestReadRegister(void *opaque, uint32_t offset,
                                uint32_t *value)
{
    TEST_BACKEND_CONTEXT *context = (TEST_BACKEND_CONTEXT *)opaque;
    uint32_t index;
    ++context->RegisterReadCalls;
    if (value == NULL ||
        (context->RegisterReadFailureAtCall != 0 &&
         context->RegisterReadCalls == context->RegisterReadFailureAtCall)) {
        return 0;
    }
    for (index = 0; index < context->RegisterCount; ++index) {
        if (context->Registers[index].Offset == offset) {
            *value = context->Registers[index].Value;
            return 1;
        }
    }
    return 0;
}

static uint32_t TestReadGgttPte(void *opaque, uint32_t index,
                               uint64_t *value)
{
    TEST_BACKEND_CONTEXT *context = (TEST_BACKEND_CONTEXT *)opaque;
    uint32_t relative;
    ++context->PteReadCalls;
    if (value == NULL ||
        (context->PteReadFailureAtCall != 0 &&
         context->PteReadCalls == context->PteReadFailureAtCall) ||
        index < context->PteBaseIndex) {
        return 0;
    }
    relative = index - context->PteBaseIndex;
    if (relative >= context->PteCount) return 0;
    *value = context->Ptes[relative];
    return 1;
}

static void RecordEvent(TEST_BACKEND_CONTEXT *context, uint32_t event)
{
    if (context->EventCount <
        (uint32_t)(sizeof(context->Events) / sizeof(context->Events[0]))) {
        context->Events[context->EventCount++] = event;
    }
}

static uint32_t TestMapWc(void *opaque, uint64_t physical, uint64_t bytes,
                          uint32_t **view)
{
    TEST_BACKEND_CONTEXT *context = (TEST_BACKEND_CONTEXT *)opaque;
    context->MappedPhysical = physical;
    context->MappedBytes = bytes;
    ++context->MapCalls;
    if (view == NULL) return 0;
    *view = NULL;
    if (context->MapMode == TEST_MAP_FAILURE) return 0;
    if (context->MapMode == TEST_MAP_PARTIAL_FAILURE) {
        context->MapActive = 1;
        return 0;
    }
    context->MapActive = 1;
    if (context->MapMode == TEST_MAP_NULL_VIEW) return 1;
    if (context->MapMode == TEST_MAP_UNALIGNED_VIEW) {
        *view = (uint32_t *)((uint8_t *)&context->Framebuffer[1] + 1);
    } else {
        *view = &context->Framebuffer[1];
    }
    return 1;
}

static void TestDrainWrites(void *opaque)
{
    TEST_BACKEND_CONTEXT *context = (TEST_BACKEND_CONTEXT *)opaque;
    ++context->DrainCalls;
    RecordEvent(context, 1);
}

static void TestUnmapWc(void *opaque)
{
    TEST_BACKEND_CONTEXT *context = (TEST_BACKEND_CONTEXT *)opaque;
    ++context->UnmapCalls;
    context->MapActive = 0;
    RecordEvent(context, 2);
}

static HV_GPU_BACKEND TestBackend(void)
{
    HV_GPU_BACKEND backend;
    backend.Context = &gBackendContext;
    backend.OwnershipHeld = TestOwnershipHeld;
    backend.AddressingIsGgtt = TestAddressingIsGgtt;
    backend.ReadRegister = TestReadRegister;
    backend.ReadGgttPte = TestReadGgttPte;
    backend.MapWc = TestMapWc;
    backend.DrainWrites = TestDrainWrites;
    backend.UnmapWc = TestUnmapWc;
    return backend;
}

static void ResetFixture(void)
{
    uint32_t pipe;
    uint32_t index;
    memset(&gBackendContext, 0, sizeof(gBackendContext));
    memset(&gState, 0, sizeof(gState));
    memset(&gEntities, 0, sizeof(gEntities));
    gBackendContext.Owned = 1;
    gBackendContext.GgttMode = 1;
    gBackendContext.MapMode = TEST_MAP_SUCCESS;
    gBackendContext.PteBaseIndex =
        gValidAllocation.GgttBase / (uint32_t)HV_GPU_PAGE_BYTES;
    gBackendContext.PteCount = (uint32_t)(gValidAllocation.PinnedBytes /
                                           HV_GPU_PAGE_BYTES);
    for (index = 0; index < gBackendContext.PteCount; ++index) {
        gBackendContext.Ptes[index] = gValidAllocation.PhysicalBase +
            (uint64_t)index * HV_GPU_PAGE_BYTES + UINT64_C(1);
    }
    for (pipe = 0; pipe < TEST_PIPE_COUNT; ++pipe) {
        InstallPlane(&gBackendContext, pipe, &gValidAllocation);
    }
    for (index = 0; index < TEST_FRAME_WORDS + 2; ++index) {
        gBackendContext.Framebuffer[index] = TEST_FRAME_SENTINEL;
    }
}

static int InitializeValid(void)
{
    HV_GPU_BACKEND backend = TestBackend();
    return HvGpuInitialize(&gState, &gValidAdapter, &gValidAllocation,
                           &backend) == HV_GPU_OK;
}

static void WriteLe16(uint8_t *bytes, uint32_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1] = (uint8_t)(value >> 8);
}

static void WriteLe32(uint8_t *bytes, uint32_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1] = (uint8_t)(value >> 8);
    bytes[offset + 2] = (uint8_t)(value >> 16);
    bytes[offset + 3] = (uint8_t)(value >> 24);
}

static void BuildPciConfig(uint8_t config[256])
{
    memset(config, 0, 256);
    WriteLe16(config, 0, UINT16_C(0x8086));
    WriteLe16(config, 2, HV_GPU_INTEL_UHD770_ID);
    WriteLe16(config, 4, UINT16_C(0x0002));
    config[0x0b] = UINT8_C(0x03);
    config[0x0e] = UINT8_C(0x00);
    WriteLe32(config, 0x10, UINT32_C(0xf0000004));
    WriteLe32(config, 0x14, 0);
}

static int TestPciDiscovery(void)
{
    uint8_t config[256];
    HV_GPU_ADAPTER adapter = { 0, 0, 0 };
    HV_GPU_ADAPTER unchanged = {
        UINT16_C(0xaaaa), UINT16_C(0xbbbb), UINT64_C(0xccccdddd)
    };
    BuildPciConfig(config);
    CHECK(HvGpuIntelParsePci(config, sizeof(config), &adapter) == HV_GPU_OK &&
          adapter.VendorId == UINT16_C(0x8086) &&
          adapter.DeviceId == HV_GPU_INTEL_UHD770_ID &&
          adapter.MmioPhysical == UINT64_C(0xf0000000),
          "valid Intel display PCI image decodes its 64-bit BAR");

    adapter = unchanged;
    WriteLe16(config, 0, UINT16_C(0x10de));
    CHECK(HvGpuIntelParsePci(config, sizeof(config), &adapter) ==
              HV_GPU_NOT_SUPPORTED &&
          adapter.VendorId == unchanged.VendorId &&
          adapter.DeviceId == unchanged.DeviceId &&
          adapter.MmioPhysical == unchanged.MmioPhysical,
          "non-Intel display is unsupported without changing output");

    BuildPciConfig(config);
    WriteLe16(config, 2, UINT16_C(0x1234));
    CHECK(HvGpuIntelParsePci(config, sizeof(config), &adapter) ==
              HV_GPU_NOT_SUPPORTED,
          "unlisted Intel display device is unsupported");
    BuildPciConfig(config);
    config[0x0b] = 0x02;
    CHECK(HvGpuIntelParsePci(config, sizeof(config), &adapter) ==
              HV_GPU_NOT_SUPPORTED,
          "non-display PCI class is unsupported");
    BuildPciConfig(config);
    WriteLe32(config, 0x10, UINT32_C(0xf0000000));
    CHECK(HvGpuIntelParsePci(config, sizeof(config), &adapter) ==
              HV_GPU_NOT_SUPPORTED,
          "32-bit memory BAR is rejected by the Gen12 MMIO contract");
    BuildPciConfig(config);
    WriteLe32(config, 0x10, UINT32_C(0xf000000c));
    CHECK(HvGpuIntelParsePci(config, sizeof(config), &adapter) ==
              HV_GPU_NOT_SUPPORTED,
          "prefetchable BAR is rejected by the conservative MMIO contract");
    CHECK(HvGpuIntelParsePci(config, 0x14, &adapter) ==
              HV_GPU_INVALID_ARGUMENT &&
          HvGpuIntelParsePci(NULL, sizeof(config), &adapter) ==
              HV_GPU_INVALID_ARGUMENT,
          "short or null PCI config images are invalid");
    return 0;
}

static int TestExtentBounds(void)
{
    HV_GPU_ALLOCATION allocation = gValidAllocation;
    HV_GPU_EXTENT extent;
    HV_GPU_EXTENT unchanged = { UINT64_C(0x11), UINT64_C(0x22), 33 };
    allocation.Width = UINT32_C(1024);
    allocation.Height = UINT32_C(2048);
    allocation.StrideBytes = UINT32_C(4096);
    allocation.PinnedBytes = HV_GPU_MAX_BYTES;
    CHECK(HvGpuValidateExtent(&allocation, &extent) == HV_GPU_OK &&
          extent.Bytes == HV_GPU_MAX_BYTES &&
          extent.MappingBytes == HV_GPU_MAX_BYTES &&
          extent.PageCount == HV_GPU_MAX_PAGES,
          "exactly 8 MiB is accepted with a bounded 2048-page map");

    extent = unchanged;
    allocation.Height += 1;
    CHECK(HvGpuValidateExtent(&allocation, &extent) ==
              HV_GPU_INVALID_ARGUMENT &&
          extent.Bytes == unchanged.Bytes &&
          extent.MappingBytes == unchanged.MappingBytes &&
          extent.PageCount == unchanged.PageCount,
          "extent above 8 MiB is rejected without changing output");

    allocation = gValidAllocation;
    allocation.PinnedBytes = HV_GPU_PAGE_BYTES * 2;
    CHECK(HvGpuValidateExtent(&allocation, &extent) == HV_GPU_OK &&
          extent.Bytes == UINT64_C(2048) &&
          extent.MappingBytes == HV_GPU_PAGE_BYTES && extent.PageCount == 1,
          "small declared extent rounds to a single pinned page");
    allocation.PinnedBytes = HV_GPU_PAGE_BYTES;
    allocation.StrideBytes = UINT32_C(128);
    allocation.Height = UINT32_C(33);
    extent = unchanged;
    CHECK(HvGpuValidateExtent(&allocation, &extent) ==
              HV_GPU_INVALID_ARGUMENT &&
          extent.Bytes == unchanged.Bytes,
          "rounded CPU map cannot extend beyond pinned pages");

    allocation = gValidAllocation;
    allocation.StrideBytes = UINT32_C(65);
    CHECK(HvGpuValidateExtent(&allocation, &extent) ==
              HV_GPU_INVALID_ARGUMENT,
          "stride must satisfy the hardware 64-byte unit alignment");
    allocation = gValidAllocation;
    allocation.Width = 0;
    CHECK(HvGpuValidateExtent(&allocation, &extent) ==
              HV_GPU_INVALID_ARGUMENT,
          "zero dimensions are rejected");
    allocation = gValidAllocation;
    allocation.PhysicalBase += 1;
    CHECK(HvGpuValidateExtent(&allocation, &extent) ==
              HV_GPU_INVALID_ARGUMENT,
          "unaligned physical base is rejected");
    allocation = gValidAllocation;
    allocation.GgttBase += 1;
    CHECK(HvGpuValidateExtent(&allocation, &extent) ==
              HV_GPU_INVALID_ARGUMENT,
          "unaligned GGTT base is rejected");
    allocation = gValidAllocation;
    allocation.GgttBase = UINT32_C(0xfffff000);
    allocation.PinnedBytes = HV_GPU_PAGE_BYTES * 2;
    CHECK(HvGpuValidateExtent(&allocation, &extent) ==
              HV_GPU_INVALID_ARGUMENT,
          "GGTT reservation may not wrap the 32-bit address space");
    allocation = gValidAllocation;
    allocation.PhysicalBase = UINT64_C(1) << 39;
    CHECK(HvGpuValidateExtent(&allocation, &extent) ==
              HV_GPU_INVALID_ARGUMENT,
          "physical reservation outside the Gen12 address range is rejected");
    return 0;
}

static int TestGgttPteParsing(void)
{
    uint64_t physical = UINT64_C(0xaaaaaaaaaaaaaaaa);
    CHECK(HvGpuIntelGgttPhysical(UINT64_C(0x0000001234500001),
                                 &physical) == HV_GPU_OK &&
          physical == UINT64_C(0x0000001234500000),
          "valid GGTT entry separates the physical page from valid bit");
    physical = UINT64_C(0xaaaaaaaaaaaaaaaa);
    CHECK(HvGpuIntelGgttPhysical(0, &physical) == HV_GPU_NOT_SUPPORTED &&
          physical == UINT64_C(0xaaaaaaaaaaaaaaaa),
          "absent GGTT entry fails without changing physical output");
    CHECK(HvGpuIntelGgttPhysical(UINT64_C(0x4000000000000001),
                                 &physical) == HV_GPU_NOT_SUPPORTED &&
          physical == UINT64_C(0xaaaaaaaaaaaaaaaa),
          "reserved GGTT PTE flags are rejected");
    CHECK(HvGpuIntelGgttPhysical(1, NULL) == HV_GPU_INVALID_ARGUMENT,
          "null GGTT physical output is invalid");
    return 0;
}

static int TestPlaneRegisterParsing(void)
{
    HV_GPU_BACKEND backend;
    uint32_t pipe;
    ResetFixture();
    backend = TestBackend();
    for (pipe = 0; pipe < TEST_PIPE_COUNT; ++pipe) {
        HV_GPU_PLANE plane;
        memset(&plane, 0xa5, sizeof(plane));
        CHECK(HvGpuIntelReadPlane(&backend, pipe, &plane) == HV_GPU_OK &&
              plane.Control == (HV_GPU_INTEL_PLANE_ENABLE |
                                HV_GPU_INTEL_XRGB8888) &&
              plane.Stride == UINT32_C(2) &&
              plane.Size == UINT32_C(0x000f000f) &&
              plane.Surface == gValidAllocation.GgttBase &&
              plane.LiveSurface == gValidAllocation.GgttBase &&
              plane.Offset == 0 && plane.Position == 0 &&
              plane.ColorControl == HV_GPU_INTEL_COLOR_GAMMA_DISABLE &&
              plane.AuxDistance == 0,
              "Gen12 plane 1 register offsets decode for each pipe A-D");
    }
    {
        HV_GPU_PLANE plane;
        HV_GPU_PLANE unchanged;
        memset(&plane, 0x5a, sizeof(plane));
        unchanged = plane;
        gBackendContext.RegisterReadCalls = 0;
        gBackendContext.RegisterReadFailureAtCall = 4;
        CHECK(HvGpuIntelReadPlane(&backend, 0, &plane) ==
                  HV_GPU_MAPPING_FAILED &&
              memcmp(&plane, &unchanged, sizeof(plane)) == 0,
              "failed MMIO read leaves parsed plane output untouched");
        CHECK(HvGpuIntelReadPlane(&backend, 4, &plane) ==
                  HV_GPU_INVALID_ARGUMENT &&
              HvGpuIntelReadPlane(NULL, 0, &plane) == HV_GPU_INVALID_ARGUMENT,
              "invalid pipe or backend is rejected");
    }
    return 0;
}

static int TestPlaneValidation(void)
{
    HV_GPU_ALLOCATION allocation = gValidAllocation;
    HV_GPU_PLANE plane;
    memset(&plane, 0, sizeof(plane));
    plane.Control = HV_GPU_INTEL_PLANE_ENABLE | HV_GPU_INTEL_XRGB8888;
    plane.Stride = allocation.StrideBytes / UINT32_C(64);
    plane.Size = UINT32_C(0x000f000f);
    plane.Surface = allocation.GgttBase;
    plane.LiveSurface = allocation.GgttBase;
    plane.ColorControl = HV_GPU_INTEL_COLOR_GAMMA_DISABLE;
    CHECK(HvGpuIntelValidatePlane(&plane, &allocation) == HV_GPU_OK,
          "simple enabled uncompressed linear plane is accepted");
    plane.LiveSurface += UINT32_C(4096);
    CHECK(HvGpuIntelValidatePlane(&plane, &allocation) ==
              HV_GPU_NOT_SUPPORTED,
          "pending surface differing from live surface is rejected");
    plane.LiveSurface = allocation.GgttBase;
    plane.Control = HV_GPU_INTEL_PLANE_ENABLE | UINT32_C(0x02000000);
    CHECK(HvGpuIntelValidatePlane(&plane, &allocation) ==
              HV_GPU_NOT_SUPPORTED,
          "unsupported pixel format is rejected");
    plane.Control = HV_GPU_INTEL_PLANE_ENABLE | HV_GPU_INTEL_XRGB8888 |
                    (UINT32_C(1) << 10);
    CHECK(HvGpuIntelValidatePlane(&plane, &allocation) ==
              HV_GPU_NOT_SUPPORTED,
          "tiled surface mode is rejected");
    plane.Control = HV_GPU_INTEL_PLANE_ENABLE | HV_GPU_INTEL_XRGB8888 |
                    UINT32_C(1);
    CHECK(HvGpuIntelValidatePlane(&plane, &allocation) ==
              HV_GPU_NOT_SUPPORTED,
          "rotated plane mode is rejected");
    plane.Control = HV_GPU_INTEL_PLANE_ENABLE | HV_GPU_INTEL_XRGB8888;
    plane.Control |= UINT32_C(1) << 15;
    CHECK(HvGpuIntelValidatePlane(&plane, &allocation) ==
              HV_GPU_NOT_SUPPORTED,
          "render decompression is rejected");
    plane.Control = HV_GPU_INTEL_PLANE_ENABLE | HV_GPU_INTEL_XRGB8888;
    plane.ColorControl = 0;
    CHECK(HvGpuIntelValidatePlane(&plane, &allocation) ==
              HV_GPU_NOT_SUPPORTED,
          "plane gamma must be explicitly disabled");
    plane.ColorControl = UINT32_C(1);
    CHECK(HvGpuIntelValidatePlane(&plane, &allocation) ==
              HV_GPU_NOT_SUPPORTED,
          "color conversion is rejected");
    plane.ColorControl = HV_GPU_INTEL_COLOR_GAMMA_DISABLE;
    plane.AuxDistance = UINT32_C(1);
    CHECK(HvGpuIntelValidatePlane(&plane, &allocation) ==
              HV_GPU_NOT_SUPPORTED,
          "auxiliary compression distance is rejected");
    plane.AuxDistance = 0;
    plane.Offset = UINT32_C(1);
    CHECK(HvGpuIntelValidatePlane(&plane, &allocation) ==
              HV_GPU_NOT_SUPPORTED,
          "panning offset is rejected");
    return 0;
}

static int TestUnsupportedInitialization(void)
{
    HV_GPU_ADAPTER unsupported = gValidAdapter;
    HV_GPU_BACKEND backend;
    ResetFixture();
    backend = TestBackend();
    unsupported.DeviceId = UINT16_C(0x9a49);
    CHECK(HvGpuInitialize(&gState, &unsupported, &gValidAllocation,
                          &backend) == HV_GPU_NOT_SUPPORTED &&
          gState.Phase == HV_GPU_OWNERSHIP_LOST &&
          gBackendContext.MapCalls == 0 && gBackendContext.DrainCalls == 0 &&
          gBackendContext.UnmapCalls == 0,
          "unsupported GPU latches unsupported without creating mappings");
    CHECK(HvGpuRender(&gState, &gEntities, NULL) == HV_GPU_NOT_SUPPORTED &&
          gBackendContext.MapCalls == 0,
          "unsupported GPU cannot invoke rendering or map callbacks");

    ResetFixture();
    CHECK(HvGpuInitialize(&gState, &gValidAdapter, &gValidAllocation,
                          NULL) == HV_GPU_NOT_SUPPORTED &&
          gState.Phase == HV_GPU_OWNERSHIP_LOST &&
          gBackendContext.MapCalls == 0,
          "missing provider returns unsupported and leaves no mapping");

    ResetFixture();
    backend = TestBackend();
    gBackendContext.Owned = 0;
    CHECK(HvGpuInitialize(&gState, &gValidAdapter, &gValidAllocation,
                          &backend) == HV_GPU_NOT_SUPPORTED &&
          gState.Phase == HV_GPU_OWNERSHIP_LOST &&
          gBackendContext.MapCalls == 0 && gBackendContext.DrainCalls == 0 &&
          gBackendContext.UnmapCalls == 0,
          "unowned guest adapter is rejected before any CPU mapping");
    return 0;
}

static int TestInitializationValidationAndLatch(void)
{
    HV_GPU_BACKEND backend;
    ResetFixture();
    backend = TestBackend();
    SetRegister(&gBackendContext, PipeRegisterBase(0) + UINT32_C(0x1c),
                gValidAllocation.GgttBase + UINT32_C(4096));
    CHECK(HvGpuInitialize(&gState, &gValidAdapter, &gValidAllocation,
                          &backend) == HV_GPU_NOT_SUPPORTED &&
          gState.Phase == HV_GPU_OWNERSHIP_LOST &&
          gBackendContext.MapCalls == 0,
          "plane mismatch rejects initialization and latches ownership loss");
    SetRegister(&gBackendContext, PipeRegisterBase(0) + UINT32_C(0x1c),
                gValidAllocation.GgttBase);
    CHECK(HvGpuInitialize(&gState, &gValidAdapter, &gValidAllocation,
                          &backend) == HV_GPU_NOT_SUPPORTED &&
          gState.Phase == HV_GPU_OWNERSHIP_LOST &&
          gBackendContext.MapCalls == 0,
          "repairing registers cannot clear terminal ownership loss");

    ResetFixture();
    backend = TestBackend();
    gBackendContext.Ptes[0] = gValidAllocation.PhysicalBase +
                              HV_GPU_PAGE_BYTES + UINT64_C(1);
    CHECK(HvGpuInitialize(&gState, &gValidAdapter, &gValidAllocation,
                          &backend) == HV_GPU_NOT_SUPPORTED &&
          gState.Phase == HV_GPU_OWNERSHIP_LOST &&
          gBackendContext.MapCalls == 0,
          "incorrect GGTT physical translation rejects initialization");

    ResetFixture();
    backend = TestBackend();
    gBackendContext.PteReadFailureAtCall = 1;
    CHECK(HvGpuInitialize(&gState, &gValidAdapter, &gValidAllocation,
                          &backend) == HV_GPU_NOT_SUPPORTED &&
          gState.Phase == HV_GPU_OWNERSHIP_LOST &&
          gBackendContext.MapCalls == 0,
          "unreadable reserved GGTT entry latches ownership loss");

    ResetFixture();
    backend = TestBackend();
    gBackendContext.PteCount = 0;
    CHECK(HvGpuInitialize(&gState, &gValidAdapter, &gValidAllocation,
                          &backend) == HV_GPU_NOT_SUPPORTED &&
          gState.Phase == HV_GPU_OWNERSHIP_LOST &&
          gBackendContext.MapCalls == 0,
          "missing GGTT page-table entry latches ownership loss");

    ResetFixture();
    backend = TestBackend();
    gBackendContext.Ptes[0] |= UINT64_C(2);
    CHECK(HvGpuInitialize(&gState, &gValidAdapter, &gValidAllocation,
                          &backend) == HV_GPU_NOT_SUPPORTED &&
          gState.Phase == HV_GPU_OWNERSHIP_LOST &&
          gBackendContext.MapCalls == 0,
          "unsupported GGTT PTE flags are rejected before mapping");

    ResetFixture();
    backend = TestBackend();
    {
        HV_GPU_ALLOCATION multiPage = gValidAllocation;
        multiPage.PinnedBytes = HV_GPU_PAGE_BYTES * 2;
        gBackendContext.PteCount = 2;
        gBackendContext.Ptes[1] = gValidAllocation.PhysicalBase +
                                  UINT64_C(3) * HV_GPU_PAGE_BYTES +
                                  UINT64_C(1);
        CHECK(HvGpuInitialize(&gState, &gValidAdapter, &multiPage,
                              &backend) == HV_GPU_NOT_SUPPORTED &&
              gState.Phase == HV_GPU_OWNERSHIP_LOST &&
              gBackendContext.PteReadCalls == 2 &&
              gBackendContext.MapCalls == 0,
              "midrange GGTT page must translate to its matching pinned page");
    }

    ResetFixture();
    backend = TestBackend();
    gBackendContext.GgttMode = 0;
    CHECK(HvGpuInitialize(&gState, &gValidAdapter, &gValidAllocation,
                          &backend) == HV_GPU_NOT_SUPPORTED &&
          gState.Phase == HV_GPU_OWNERSHIP_LOST &&
          gBackendContext.MapCalls == 0,
          "non-GGTT addressing mode is rejected and ownership is latched");
    return 0;
}

static int TestRenderAndDrainOrder(void)
{
    uint32_t y;
    uint32_t x;
    ResetFixture();
    CHECK(InitializeValid(), "valid synthetic plane initializes");
    gEntities.Count = 1;
    gEntities.Entities[0].Coordinates = HV_OVERLAY_SCREEN;
    gEntities.Entities[0].X = 1;
    gEntities.Entities[0].Y = 1;
    gEntities.Entities[0].Width = 3;
    gEntities.Entities[0].Height = 3;
    gEntities.Entities[0].Color = UINT32_C(0xff12ab34);
    CHECK(HvGpuRender(&gState, &gEntities, NULL) == HV_GPU_OK &&
          gState.Phase == HV_GPU_READY && gState.CpuMappingActive == 0 &&
          gBackendContext.MapCalls == 1 && gBackendContext.MapActive == 0 &&
          gBackendContext.DrainCalls == 1 && gBackendContext.UnmapCalls == 1,
          "render uses one bounded WC map and releases it before returning");
    CHECK(gBackendContext.MappedPhysical == gValidAllocation.PhysicalBase &&
          gBackendContext.MappedBytes == HV_GPU_PAGE_BYTES &&
          gBackendContext.MappedBytes <= gValidAllocation.PinnedBytes,
          "CPU map uses pinned physical address and exact page-rounded extent");
    CHECK(gBackendContext.EventCount == 2 &&
          gBackendContext.Events[0] == 1 &&
          gBackendContext.Events[1] == 2,
          "store drain occurs before CPU mapping removal");
    CHECK(gBackendContext.Framebuffer[1] == TEST_FRAME_SENTINEL &&
          gBackendContext.Framebuffer[TEST_FRAME_WORDS + 1] ==
              TEST_FRAME_SENTINEL &&
          gBackendContext.Framebuffer[1 + 1 * 32 + 1] ==
              UINT32_C(0xff12ab34),
          "renderer writes the diagnostic pixel within guarded mapped RAM");
    for (y = 0; y < UINT32_C(16); ++y) {
        for (x = UINT32_C(16); x < UINT32_C(32); ++x) {
            CHECK(gBackendContext.Framebuffer[1 + y * 32 + x] ==
                      TEST_FRAME_SENTINEL,
                  "renderer leaves row padding untouched");
        }
    }
    return 0;
}

static int TestRendererFailureCleanup(void)
{
    ResetFixture();
    CHECK(InitializeValid(), "valid synthetic plane initializes");
    gEntities.Count = HV_OVERLAY_MAX_ENTITIES + 1;
    CHECK(HvGpuRender(&gState, &gEntities, NULL) == HV_GPU_RENDER_FAILED &&
          gState.Phase == HV_GPU_READY && gState.CpuMappingActive == 0 &&
          gBackendContext.MapActive == 0 && gBackendContext.DrainCalls == 1 &&
          gBackendContext.UnmapCalls == 1 &&
          gBackendContext.Events[0] == 1 && gBackendContext.Events[1] == 2,
          "renderer input failure still drains and unmaps while retaining ownership");
    CHECK(gBackendContext.Framebuffer[1] == TEST_FRAME_SENTINEL,
          "invalid renderer inputs fail before writing framebuffer pixels");
    return 0;
}

static int TestMapFailuresLatchAndCleanup(void)
{
    uint32_t mode;
    for (mode = TEST_MAP_FAILURE; mode <= TEST_MAP_UNALIGNED_VIEW; ++mode) {
        ResetFixture();
        gBackendContext.MapMode = mode;
        CHECK(InitializeValid(), "valid synthetic plane initializes");
        CHECK(HvGpuRender(&gState, &gEntities, NULL) ==
                  HV_GPU_MAPPING_FAILED &&
              gState.Phase == HV_GPU_OWNERSHIP_LOST &&
              gState.CpuMappingActive == 0 && gBackendContext.MapActive == 0 &&
              gBackendContext.DrainCalls == 1 &&
              gBackendContext.UnmapCalls == 1 &&
              gBackendContext.Events[0] == 1 &&
              gBackendContext.Events[1] == 2,
              "failed, partial, null, or unaligned map is cleaned and latched");
        CHECK(HvGpuRender(&gState, &gEntities, NULL) ==
                  HV_GPU_NOT_SUPPORTED && gBackendContext.MapCalls == 1,
              "map failure cannot be retried until reboot");
    }
    return 0;
}

static int TestOwnershipLossAroundRender(void)
{
    ResetFixture();
    CHECK(InitializeValid(), "valid synthetic plane initializes");
    gBackendContext.Owned = 0;
    CHECK(HvGpuRender(&gState, &gEntities, NULL) == HV_GPU_NOT_SUPPORTED &&
          gState.Phase == HV_GPU_OWNERSHIP_LOST &&
          gBackendContext.MapCalls == 0 && gBackendContext.DrainCalls == 0 &&
          gBackendContext.UnmapCalls == 0,
          "ownership loss before mapping prevents render and map");

    ResetFixture();
    CHECK(InitializeValid(), "valid synthetic plane initializes");
    gBackendContext.OwnershipFalseAtCall = 8;
    gEntities.Count = 1;
    gEntities.Entities[0].Coordinates = HV_OVERLAY_SCREEN;
    gEntities.Entities[0].X = 1;
    gEntities.Entities[0].Y = 1;
    gEntities.Entities[0].Width = 3;
    gEntities.Entities[0].Height = 3;
    gEntities.Entities[0].Color = UINT32_C(0xff00ff00);
    CHECK(HvGpuRender(&gState, &gEntities, NULL) == HV_GPU_NOT_SUPPORTED &&
          gState.Phase == HV_GPU_OWNERSHIP_LOST &&
          gState.CpuMappingActive == 0 && gBackendContext.MapActive == 0 &&
          gBackendContext.DrainCalls == 1 && gBackendContext.UnmapCalls == 1 &&
          gBackendContext.Events[0] == 1 && gBackendContext.Events[1] == 2 &&
          gBackendContext.Framebuffer[1 + 1 * 32 + 1] ==
              UINT32_C(0xff00ff00),
          "post-render ownership loss drains, unmaps, and latches without rearming");
    return 0;
}

static int TestPostMapOwnershipLoss(void)
{
    ResetFixture();
    CHECK(InitializeValid(), "valid synthetic plane initializes");
    /* Initialize uses two ownership checks. Render uses checks 3-4 before
     * mapping, then check 5 immediately after the CPU mapping is installed. */
    gBackendContext.OwnershipFalseAtCall =
        gBackendContext.OwnershipCalls + UINT32_C(3);
    CHECK(HvGpuRender(&gState, &gEntities, NULL) == HV_GPU_NOT_SUPPORTED &&
          gState.Phase == HV_GPU_OWNERSHIP_LOST &&
          gBackendContext.MapCalls == 1 && gBackendContext.MapActive == 0 &&
          gBackendContext.DrainCalls == 1 && gBackendContext.UnmapCalls == 1 &&
          gBackendContext.Events[0] == 1 && gBackendContext.Events[1] == 2 &&
          gBackendContext.Framebuffer[1] == TEST_FRAME_SENTINEL,
          "ownership loss immediately after mapping prevents drawing and cleans up");
    return 0;
}

static int TestResumeAndShutdownCleanup(void)
{
    ResetFixture();
    CHECK(InitializeValid(), "valid synthetic plane initializes");
    gState.CpuMappingActive = 1;
    gBackendContext.MapActive = 1;
    HvGpuBeforeGuestResume(&gState);
    CHECK(gState.Phase == HV_GPU_OWNERSHIP_LOST &&
          gState.CpuMappingActive == 0 && gBackendContext.MapActive == 0 &&
          gBackendContext.DrainCalls == 1 && gBackendContext.UnmapCalls == 1 &&
          gBackendContext.Events[0] == 1 && gBackendContext.Events[1] == 2,
          "resume path abort drains and removes an interrupted active map");
    CHECK(HvGpuRender(&gState, &gEntities, NULL) == HV_GPU_NOT_SUPPORTED,
          "aborted render remains unsupported until reboot");
    HvGpuShutdown(&gState);
    CHECK(gState.Phase == HV_GPU_OWNERSHIP_LOST &&
          gState.Allocation.PhysicalBase == gValidAllocation.PhysicalBase &&
          gState.Allocation.GgttBase == gValidAllocation.GgttBase &&
          gBackendContext.DrainCalls == 1 && gBackendContext.UnmapCalls == 1,
          "shutdown retains pinned scanout allocation after ownership loss");

    ResetFixture();
    CHECK(InitializeValid(), "valid synthetic plane initializes");
    gState.CpuMappingActive = 1;
    gBackendContext.MapActive = 1;
    HvGpuShutdown(&gState);
    CHECK(gState.Phase == HV_GPU_STOPPED &&
          gState.CpuMappingActive == 0 && gBackendContext.MapActive == 0 &&
          gBackendContext.DrainCalls == 1 && gBackendContext.UnmapCalls == 1 &&
          gState.Allocation.PinnedBytes == gValidAllocation.PinnedBytes,
          "shutdown terminally cleans CPU mapping and retains pinned pages");
    return 0;
}

typedef int (*TEST_CASE_FN)(void);

typedef struct TEST_CASE {
    const char *Name;
    TEST_CASE_FN Function;
} TEST_CASE;

int HvGpuHalRunModelTests(void)
{
    static const TEST_CASE cases[] = {
        { "PCI discovery", TestPciDiscovery },
        { "extent bounds", TestExtentBounds },
        { "GGTT PTE parsing", TestGgttPteParsing },
        { "plane register parsing", TestPlaneRegisterParsing },
        { "plane validation", TestPlaneValidation },
        { "unsupported initialization", TestUnsupportedInitialization },
        { "initialization validation and latch", TestInitializationValidationAndLatch },
        { "render and drain order", TestRenderAndDrainOrder },
        { "renderer failure cleanup", TestRendererFailureCleanup },
        { "mapping failure latch", TestMapFailuresLatchAndCleanup },
        { "ownership loss around render", TestOwnershipLossAroundRender },
        { "post-map ownership loss", TestPostMapOwnershipLoss },
        { "resume and shutdown cleanup", TestResumeAndShutdownCleanup }
    };
    uint32_t index;
    uint32_t failures = 0;
    for (index = 0; index <
         (uint32_t)(sizeof(cases) / sizeof(cases[0])); ++index) {
        if (cases[index].Function() != 0) {
            fprintf(stderr, "gpu-hal case failed: %s\n", cases[index].Name);
            ++failures;
        }
    }
    if (failures != 0) return 1;
    printf("gpu_hal_model_tests: %u cases, PASS\n",
           (unsigned)(sizeof(cases) / sizeof(cases[0])));
    return 0;
}
