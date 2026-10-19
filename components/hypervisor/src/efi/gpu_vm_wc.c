#include "gpu_vm_wc.h"

#include "efi_hypervisor_math.h"
#include "gpu_native.h"

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(__outbyte)
#pragma code_seg(push, ".text$gpuvmwc")
#endif

#define HV_GPU_VM_WC_FRAMEBUFFER_BYTES UINT64_C(4096)
#define HV_GPU_VM_WC_MIN_FRAMEBUFFER_PHYSICAL UINT64_C(0x00100000)
#define HV_GPU_VM_WC_MTRR_BASE UINT32_C(0x200)
#define HV_GPU_VM_WC_MTRR_LIMIT UINT32_C(0x2ff)
#define HV_GPU_VM_WC_MTRR_PAT UINT32_C(0x277)
#define HV_GPU_VM_WC_MSR_WRITE_BITMAP_OFFSET UINT32_C(0x800)
#define HV_GPU_VM_WC_MTRR_BITMAP_BYTES UINT32_C(32)
#define HV_GPU_VM_WC_FRAME_WIDTH UINT32_C(16)
#define HV_GPU_VM_WC_FRAME_HEIGHT UINT32_C(16)
#define HV_GPU_VM_WC_FRAME_STRIDE UINT32_C(128)
#define HV_GPU_VM_WC_INITIAL_PIXEL UINT32_C(0xff123456)
#define HV_GPU_VM_WC_RENDER_PIXEL UINT32_C(0xffff0000)
#define HV_GPU_VM_WC_LOW_LIMIT (UINT64_C(1) << 30)

static void HvGpuVmWcZero(void *destination, uint64_t bytes)
{
    volatile uint8_t *cursor = (volatile uint8_t *)destination;
    while (bytes-- != 0u) *cursor++ = 0u;
}

static int HvGpuVmWcRangeBelowIdentity(uint64_t base, uint64_t bytes)
{
    return base != 0u && bytes != 0u && base < HV_GPU_VM_WC_LOW_LIMIT &&
           bytes <= HV_GPU_VM_WC_LOW_LIMIT - base;
}

static uint8_t *HvGpuVmWcMtrrBaseline(HV_GPU_NATIVE_STATE *state)
{
    return (uint8_t *)state + sizeof(*state);
}

static void HvGpuVmWcCaptureMtrrWriteBits(HV_GPU_NATIVE_STATE *state,
                                          const uint8_t *bitmap)
{
    const uint8_t *source = bitmap + HV_GPU_VM_WC_MSR_WRITE_BITMAP_OFFSET +
                            HV_GPU_VM_WC_MTRR_BASE / 8u;
    uint8_t *destination = HvGpuVmWcMtrrBaseline(state);
    uint32_t index;
    for (index = 0; index < HV_GPU_VM_WC_MTRR_BITMAP_BYTES; ++index) {
        destination[index] = source[index];
    }
}

static void HvGpuVmWcRestoreMtrrWriteBits(HV_GPU_NATIVE_STATE *state,
                                          uint8_t *bitmap)
{
    const uint8_t *baseline = HvGpuVmWcMtrrBaseline(state);
    uint32_t msr;
    for (msr = HV_GPU_VM_WC_MTRR_BASE;
         msr <= HV_GPU_VM_WC_MTRR_LIMIT; ++msr) {
        uint32_t byteIndex;
        uint8_t bit;
        uint8_t *target;
        if (msr == HV_GPU_VM_WC_MTRR_PAT) continue;
        byteIndex = msr / 8u;
        bit = (uint8_t)(UINT8_C(1) << (msr & UINT32_C(7)));
        target = bitmap + HV_GPU_VM_WC_MSR_WRITE_BITMAP_OFFSET + byteIndex;
        if ((baseline[byteIndex - HV_GPU_VM_WC_MTRR_BASE / 8u] & bit) != 0u) {
            *target = (uint8_t)(*target | bit);
        } else {
            *target = (uint8_t)(*target & (uint8_t)~bit);
        }
    }
}

EFI_STATUS HvGpuVmWcAllocate(HV_EFI_CONTEXT *context,
                             EFI_BOOT_SERVICES *bootServices)
{
    EFI_PHYSICAL_ADDRESS statePhysical = 0;
    EFI_PHYSICAL_ADDRESS framebufferPhysical = 0;
    EFI_PHYSICAL_ADDRESS maximumAddress = HV_EFI_MAX_PHYSICAL_ADDRESS;
    EFI_STATUS status;
    uint64_t stateBytes;
    uint64_t roundedStateBytes;
    UINTN statePages;
    UINTN framebufferPages =
        (UINTN)(HV_GPU_VM_WC_FRAMEBUFFER_BYTES / HV_EFI_PAGE_SIZE);
    HV_GPU_NATIVE_STATE *state;
    uint32_t *framebuffer;
    uint64_t index;

    if (context == NULL || bootServices == NULL ||
        bootServices->AllocatePages == NULL || bootServices->FreePages == NULL ||
        context->CurrentProcessorNumber != 0u ||
        context->GpuVmWcStatePhysical != 0u) {
        return EFI_INVALID_PARAMETER;
    }

    stateBytes = (uint64_t)sizeof(HV_GPU_NATIVE_STATE) +
                 HV_GPU_VM_WC_MTRR_BITMAP_BYTES;
    if (stateBytes > UINT64_MAX - (HV_EFI_PAGE_SIZE - 1u)) {
        return EFI_BAD_BUFFER_SIZE;
    }
    roundedStateBytes = (stateBytes + HV_EFI_PAGE_SIZE - 1u) &
                        ~(HV_EFI_PAGE_SIZE - 1u);
    statePages = (UINTN)(roundedStateBytes / HV_EFI_PAGE_SIZE);
    if (statePages == 0u || statePages > UINT32_MAX) {
        return EFI_BAD_BUFFER_SIZE;
    }

    status = bootServices->AllocatePages(AllocateMaxAddress,
        EfiRuntimeServicesData, statePages, &maximumAddress);
    if (EFI_ERROR(status)) return status;
    statePhysical = maximumAddress;
    if ((statePhysical & HV_EFI_PAGE_MASK) != 0u ||
        !HvGpuVmWcRangeBelowIdentity(statePhysical,
            (uint64_t)statePages * HV_EFI_PAGE_SIZE)) {
        (void)bootServices->FreePages(statePhysical, statePages);
        return EFI_UNSUPPORTED;
    }

    maximumAddress = HV_EFI_MAX_PHYSICAL_ADDRESS;
    status = bootServices->AllocatePages(AllocateMaxAddress,
        EfiRuntimeServicesData, framebufferPages, &maximumAddress);
    if (EFI_ERROR(status)) {
        (void)bootServices->FreePages(statePhysical, statePages);
        return status;
    }
    framebufferPhysical = maximumAddress;
    if ((framebufferPhysical & HV_EFI_PAGE_MASK) != 0u ||
        framebufferPhysical < HV_GPU_VM_WC_MIN_FRAMEBUFFER_PHYSICAL ||
        !HvGpuVmWcRangeBelowIdentity(framebufferPhysical,
                                      HV_GPU_VM_WC_FRAMEBUFFER_BYTES) ||
        (statePhysical < framebufferPhysical +
                            HV_GPU_VM_WC_FRAMEBUFFER_BYTES &&
         framebufferPhysical < statePhysical +
                                  (uint64_t)statePages * HV_EFI_PAGE_SIZE)) {
        (void)bootServices->FreePages(framebufferPhysical, framebufferPages);
        (void)bootServices->FreePages(statePhysical, statePages);
        return EFI_UNSUPPORTED;
    }

    HvGpuVmWcZero((void *)(uintptr_t)statePhysical,
                  (uint64_t)statePages * HV_EFI_PAGE_SIZE);
    state = (HV_GPU_NATIVE_STATE *)(uintptr_t)statePhysical;
    state->Marker = HV_GPU_NATIVE_MARKER;
    state->SelfPhysical = statePhysical;
    state->SelfPages = (uint32_t)statePages;
    state->FramebufferPhysical = framebufferPhysical;
    state->FramebufferBytes = HV_GPU_VM_WC_FRAMEBUFFER_BYTES;

    framebuffer = (uint32_t *)(uintptr_t)framebufferPhysical;
    for (index = 0; index < HV_GPU_VM_WC_FRAMEBUFFER_BYTES / sizeof(uint32_t);
         ++index) {
        framebuffer[index] = HV_GPU_VM_WC_INITIAL_PIXEL;
    }

    state->Entities.Count = 1u;
    state->Entities.Entities[0].Coordinates = HV_OVERLAY_SCREEN;
    state->Entities.Entities[0].X = 2;
    state->Entities.Entities[0].Y = 2;
    state->Entities.Entities[0].Width = 4u;
    state->Entities.Entities[0].Height = 4u;
    state->Entities.Entities[0].Color = HV_GPU_VM_WC_RENDER_PIXEL;
    state->Entities.Entities[0].FillColor = 0u;
    state->Entities.Entities[0].LabelLength = 0u;

    context->GpuVmWcStatePhysical = statePhysical;
    return EFI_SUCCESS;
}

int HvGpuVmWcPrepareCpu(HV_EFI_CONTEXT *context)
{
    HV_GPU_NATIVE_STATE *state;
    uint8_t *msrBitmap;
    uint8_t *ioBitmapA;
    uint8_t *ioBitmapB;
    uint64_t stateBytes;
    int prepared;
    int ioRestored;
    if (context == NULL || context->GpuVmWcStatePhysical == 0u ||
        context->CurrentProcessorNumber != 0u ||
        context->MsrBitmapPhysical == 0u || context->PageTablePhysical == 0u) {
        return 0;
    }
    state = (HV_GPU_NATIVE_STATE *)(uintptr_t)context->GpuVmWcStatePhysical;
    if (state->Marker != HV_GPU_NATIVE_MARKER ||
        state->SelfPhysical != context->GpuVmWcStatePhysical ||
        state->SelfPages == 0u ||
        !HvGpuVmWcRangeBelowIdentity(state->SelfPhysical,
            (uint64_t)state->SelfPages * HV_EFI_PAGE_SIZE) ||
        state->FramebufferBytes != HV_GPU_VM_WC_FRAMEBUFFER_BYTES ||
        (state->FramebufferPhysical & HV_EFI_PAGE_MASK) != 0u ||
        state->FramebufferPhysical < HV_GPU_VM_WC_MIN_FRAMEBUFFER_PHYSICAL ||
        !HvGpuVmWcRangeBelowIdentity(state->FramebufferPhysical,
                                      state->FramebufferBytes) ||
        (uint64_t)state->SelfPages * HV_EFI_PAGE_SIZE <
            (uint64_t)sizeof(*state) + HV_GPU_VM_WC_MTRR_BITMAP_BYTES) {
        return 0;
    }
    stateBytes = (uint64_t)state->SelfPages * HV_EFI_PAGE_SIZE;
    if (state->SelfPhysical < state->FramebufferPhysical +
                                  state->FramebufferBytes &&
        state->FramebufferPhysical < state->SelfPhysical + stateBytes) {
        return 0;
    }

    msrBitmap = (uint8_t *)(uintptr_t)context->MsrBitmapPhysical;
    ioBitmapA = (uint8_t *)(uintptr_t)(context->PageTablePhysical +
        (uint64_t)HV_EFI_IO_BITMAP_A_PAGE * HV_EFI_PAGE_SIZE);
    ioBitmapB = (uint8_t *)(uintptr_t)(context->PageTablePhysical +
        (uint64_t)HV_EFI_IO_BITMAP_B_PAGE * HV_EFI_PAGE_SIZE);
    HvGpuVmWcCaptureMtrrWriteBits(state, msrBitmap);

    prepared = HvGpuNativePrepareCpu(state, context);
    ioRestored = HvEfiBuildIoBitmaps(ioBitmapA, ioBitmapB,
                                     HV_EFI_IO_BITMAP_BYTES);
    HvGpuVmWcRestoreMtrrWriteBits(state, msrBitmap);
    if (prepared == 0 || ioRestored == 0 || context->GpuHostPat == 0u) {
        return 0;
    }

    return state->Cpus[0].ContextPhysical == (uint64_t)(uintptr_t)context &&
           state->Cpus[0].Wc.PageTableStorage ==
               (volatile uint64_t *)(uintptr_t)context->PageTablePhysical &&
           state->Cpus[0].Wc.PhysicalBase == state->FramebufferPhysical &&
           state->Cpus[0].Wc.PinnedBytes == state->FramebufferBytes;
}

#if defined(_MSC_VER)
#pragma code_seg(pop)
#pragma const_seg(push, ".hvscan$p")
#endif

static const char gGpuVmWcRenderPass[] =
    "GPU_HAL_VM:ROOT_WC_RENDER PASS\r\n";
static const char gGpuVmWcRenderFail[] =
    "GPU_HAL_VM:ROOT_WC_RENDER FAIL\r\n";
static const char gGpuVmWcUnmappedPass[] =
    "GPU_HAL_VM:ROOT_WC_UNMAPPED PASS\r\n";
static const char gGpuVmWcUnmappedFail[] =
    "GPU_HAL_VM:ROOT_WC_UNMAPPED FAIL\r\n";
static const char gGpuVmWcNoBackendPass[] =
    "GPU_HAL_VM:ROOT_WC_NO_BACKEND PASS\r\n";
static const char gGpuVmWcNoBackendFail[] =
    "GPU_HAL_VM:ROOT_WC_NO_BACKEND FAIL\r\n";
static const char gGpuVmWcBoundsPass[] =
    "GPU_HAL_VM:ROOT_WC_BOUNDS PASS\r\n";
static const char gGpuVmWcBoundsFail[] =
    "GPU_HAL_VM:ROOT_WC_BOUNDS FAIL\r\n";

#if defined(_MSC_VER)
#pragma const_seg(pop)
#pragma code_seg(push, ".hvscan$m")
#endif

static void HvGpuVmWcEmit(const char *text)
{
#if defined(_MSC_VER)
    while (*text != '\0') {
        __outbyte(UINT16_C(0x03f8), (uint8_t)*text++);
    }
#else
    (void)text;
#endif
}

static int HvGpuVmWcWcLeavesClear(const HV_GPU_NATIVE_CPU *cpu)
{
    const volatile uint64_t *entries;
    uint32_t index;
    if (cpu == NULL || cpu->Wc.PageTableStorage == NULL ||
        cpu->Wc.Active != 0u || cpu->Wc.ActivePageCount != 0u) {
        return 0;
    }
    entries = cpu->Wc.PageTableStorage +
        HV_EFI_GPU_WC_PT_FIRST_PAGE * HV_EFI_GPU_WC_ENTRY_COUNT;
    for (index = 0; index < HV_EFI_GPU_WC_PT_PAGE_COUNT *
                              HV_EFI_GPU_WC_ENTRY_COUNT; ++index) {
        if (entries[index] != 0u) return 0;
    }
    return 1;
}

static int HvGpuVmWcResidentRangeBelowIdentity(uint64_t base,
                                               uint64_t bytes)
{
    return base != 0u && bytes != 0u && base < HV_GPU_VM_WC_LOW_LIMIT &&
           bytes <= HV_GPU_VM_WC_LOW_LIMIT - base;
}

static int HvGpuVmWcStateValid(const HV_EFI_CONTEXT *context,
                               const HV_GPU_NATIVE_STATE *state)
{
    uint64_t stateBytes;
    if (context == NULL || state == NULL ||
        context->GpuVmWcStatePhysical == 0u ||
        context->CurrentProcessorNumber != 0u ||
        state->Marker != HV_GPU_NATIVE_MARKER ||
        state->SelfPhysical != context->GpuVmWcStatePhysical ||
        state->SelfPages == 0u || state->FramebufferBytes !=
            HV_GPU_VM_WC_FRAMEBUFFER_BYTES ||
        (state->FramebufferPhysical & HV_EFI_PAGE_MASK) != 0u ||
        state->FramebufferPhysical < HV_GPU_VM_WC_MIN_FRAMEBUFFER_PHYSICAL ||
        !HvGpuVmWcResidentRangeBelowIdentity(state->SelfPhysical,
            (uint64_t)state->SelfPages * HV_EFI_PAGE_SIZE) ||
        !HvGpuVmWcResidentRangeBelowIdentity(state->FramebufferPhysical,
                                      state->FramebufferBytes)) {
        return 0;
    }
    stateBytes = (uint64_t)state->SelfPages * HV_EFI_PAGE_SIZE;
    if (stateBytes < (uint64_t)sizeof(*state) +
                         HV_GPU_VM_WC_MTRR_BITMAP_BYTES ||
        (state->SelfPhysical < state->FramebufferPhysical +
                                   state->FramebufferBytes &&
         state->FramebufferPhysical < state->SelfPhysical + stateBytes)) {
        return 0;
    }
    return 1;
}

uint64_t HvGpuVmWcResident(HV_EFI_CONTEXT *context)
{
    HV_GPU_NATIVE_STATE *state;
    HV_GPU_NATIVE_CPU *cpu;
    uint32_t *view = NULL;
    volatile uint32_t *pixels;
    uint64_t noBackendPass;
    uint64_t renderPass = 0u;
    uint64_t unmappedPass = 0u;
    uint64_t boundsPass = 0u;
    uint32_t mapSucceeded = 0u;

    if (context == NULL || context->GpuVmWcStatePhysical == 0u) {
        HvGpuVmWcEmit(gGpuVmWcRenderFail);
        HvGpuVmWcEmit(gGpuVmWcUnmappedFail);
        HvGpuVmWcEmit(gGpuVmWcNoBackendFail);
        HvGpuVmWcEmit(gGpuVmWcBoundsFail);
        return 0u;
    }
    state = (HV_GPU_NATIVE_STATE *)(uintptr_t)
        context->GpuVmWcStatePhysical;
    if (!HvGpuVmWcStateValid(context, state)) {
        HvGpuVmWcEmit(gGpuVmWcRenderFail);
        HvGpuVmWcEmit(gGpuVmWcUnmappedFail);
        HvGpuVmWcEmit(gGpuVmWcNoBackendFail);
        HvGpuVmWcEmit(gGpuVmWcBoundsFail);
        return 0u;
    }
    cpu = &state->Cpus[context->CurrentProcessorNumber];

    noBackendPass = HvGpuInitialize(&state->Hal, NULL, NULL, NULL) ==
                        HV_GPU_NOT_SUPPORTED &&
                    state->Hal.CpuMappingActive == 0u &&
                    state->Hal.Phase == HV_GPU_OWNERSHIP_LOST &&
                    cpu->Wc.Active == 0u && cpu->Wc.ActivePageCount == 0u;

    if (noBackendPass != 0u && HvGpuNativeInitializeWc(cpu) != 0) {
        uint32_t *rejectedView = NULL;
        const uint32_t oversizedRejected =
            HvEfiGpuWcMapWc(&cpu->Wc, state->FramebufferPhysical,
                HV_EFI_GPU_WC_PAGE_SIZE * UINT64_C(2), &rejectedView) == 0u &&
            rejectedView == NULL && HvGpuVmWcWcLeavesClear(cpu) != 0;
        if (cpu->Wc.Active != 0u || cpu->Wc.ActivePageCount != 0u) {
            HvEfiGpuWcDrainWrites(&cpu->Wc);
            HvEfiGpuWcUnmap(&cpu->Wc);
        }
        rejectedView = NULL;
        {
            const uint32_t unalignedRejected =
                HvEfiGpuWcMapWc(&cpu->Wc,
                    state->FramebufferPhysical + UINT64_C(1),
                    HV_EFI_GPU_WC_PAGE_SIZE, &rejectedView) == 0u &&
                rejectedView == NULL && HvGpuVmWcWcLeavesClear(cpu) != 0;
            if (cpu->Wc.Active != 0u || cpu->Wc.ActivePageCount != 0u) {
                HvEfiGpuWcDrainWrites(&cpu->Wc);
                HvEfiGpuWcUnmap(&cpu->Wc);
            }
            boundsPass = oversizedRejected != 0u && unalignedRejected != 0u &&
                HvGpuVmWcWcLeavesClear(cpu) != 0;
        }

        if (HvEfiGpuWcMapWc(&cpu->Wc, state->FramebufferPhysical,
                             HV_EFI_GPU_WC_PAGE_SIZE, &view) == 1u &&
            view != NULL) {
            HV_OVERLAY_STATUS rendered;
            mapSucceeded = 1u;
            rendered = HvOverlayRender(view, HV_GPU_VM_WC_FRAME_STRIDE,
                HV_GPU_VM_WC_FRAME_WIDTH, HV_GPU_VM_WC_FRAME_HEIGHT,
                &state->Entities, NULL);
            HvEfiGpuWcDrainWrites(&cpu->Wc);
            pixels = (volatile uint32_t *)view;
            if (rendered == HV_OVERLAY_OK &&
                pixels[2u * (HV_GPU_VM_WC_FRAME_STRIDE / 4u) + 2u] ==
                    HV_GPU_VM_WC_RENDER_PIXEL &&
                pixels[8u * (HV_GPU_VM_WC_FRAME_STRIDE / 4u) + 8u] ==
                    HV_GPU_VM_WC_INITIAL_PIXEL) {
                renderPass = 1u;
            }
        }
    }

    if (cpu->Wc.Active != 0u || cpu->Wc.ActivePageCount != 0u) {
        HvEfiGpuWcDrainWrites(&cpu->Wc);
        HvEfiGpuWcUnmap(&cpu->Wc);
    }
    if (mapSucceeded != 0u && HvGpuVmWcWcLeavesClear(cpu) != 0) {
        unmappedPass = 1u;
    }

    HvGpuVmWcEmit(noBackendPass != 0u ? gGpuVmWcNoBackendPass :
                                              gGpuVmWcNoBackendFail);
    HvGpuVmWcEmit(boundsPass != 0u ? gGpuVmWcBoundsPass :
                                        gGpuVmWcBoundsFail);
    HvGpuVmWcEmit(renderPass != 0u ? gGpuVmWcRenderPass :
                                         gGpuVmWcRenderFail);
    HvGpuVmWcEmit(unmappedPass != 0u ? gGpuVmWcUnmappedPass :
                                           gGpuVmWcUnmappedFail);
    return noBackendPass != 0u && boundsPass != 0u && renderPass != 0u &&
           unmappedPass != 0u;
}

#if defined(_MSC_VER)
#pragma code_seg(pop)
#pragma code_seg(push, ".text$gpuvmwc")
#endif

void HvGpuVmWcFree(HV_EFI_CONTEXT *context,
                   EFI_BOOT_SERVICES *bootServices)
{
    HV_GPU_NATIVE_STATE *state;
    EFI_PHYSICAL_ADDRESS statePhysical;
    EFI_PHYSICAL_ADDRESS framebufferPhysical;
    UINTN statePages;
    UINTN framebufferPages;
    uint64_t stateBytes;

    if (context == NULL || bootServices == NULL ||
        bootServices->FreePages == NULL ||
        context->GpuVmWcStatePhysical == 0u) {
        return;
    }
    statePhysical = context->GpuVmWcStatePhysical;
    state = (HV_GPU_NATIVE_STATE *)(uintptr_t)statePhysical;
    if (state->Marker != HV_GPU_NATIVE_MARKER ||
        state->SelfPhysical != statePhysical || state->SelfPages == 0u ||
        !HvGpuVmWcRangeBelowIdentity(statePhysical,
            (uint64_t)state->SelfPages * HV_EFI_PAGE_SIZE) ||
        state->FramebufferBytes != HV_GPU_VM_WC_FRAMEBUFFER_BYTES ||
        (state->FramebufferPhysical & HV_EFI_PAGE_MASK) != 0u ||
        state->FramebufferPhysical < HV_GPU_VM_WC_MIN_FRAMEBUFFER_PHYSICAL ||
        !HvGpuVmWcRangeBelowIdentity(state->FramebufferPhysical,
                                      state->FramebufferBytes)) {
        return;
    }
    stateBytes = (uint64_t)state->SelfPages * HV_EFI_PAGE_SIZE;
    if (statePhysical < state->FramebufferPhysical + state->FramebufferBytes &&
        state->FramebufferPhysical < statePhysical + stateBytes) {
        return;
    }

    framebufferPhysical = state->FramebufferPhysical;
    framebufferPages = (UINTN)(state->FramebufferBytes / HV_EFI_PAGE_SIZE);
    statePages = (UINTN)state->SelfPages;
    context->GpuVmWcStatePhysical = 0u;
    context->GpuVmWcEntry = 0u;
    (void)bootServices->FreePages(framebufferPhysical, framebufferPages);
    (void)bootServices->FreePages(statePhysical, statePages);
}

#if defined(_MSC_VER)
#pragma code_seg(pop)
#endif
