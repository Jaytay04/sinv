#include "gpu_hal.h"

#if defined(_MSC_VER)
#pragma code_seg(push, ".hvscan$m")
#endif

/* Volatile byte copies keep freestanding resident code independent of CRT
 * helpers and avoid implicit SIMD use for structure copies. */
static void CopyBytes(void *to, const void *from, size_t bytes)
{
    volatile uint8_t *destination = (volatile uint8_t *)to;
    const volatile uint8_t *source = (const volatile uint8_t *)from;
    while (bytes-- != 0) *destination++ = *source++;
}

static uint32_t BackendValid(const HV_GPU_BACKEND *backend)
{
    return backend != NULL && backend->OwnershipHeld != NULL &&
        backend->ReadExclusion != NULL &&
        backend->AddressingIsGgtt != NULL &&
        backend->ReadRegister != NULL && backend->ReadGgttPte != NULL &&
        backend->MapWc != NULL && backend->DrainWrites != NULL &&
        backend->UnmapWc != NULL;
}

static void ReleaseCpuMapping(HV_GPU_STATE *state)
{
    if (state->CpuMappingActive != 0) {
        /* Mark inactive after cleanup: the range remains owned until the
         * stores are drained, PTEs cleared and their translations invalidated.
         * Platform callbacks are serialized and cannot recursively enter HAL. */
        state->Backend.DrainWrites(state->Backend.Context);
        state->Backend.UnmapWc(state->Backend.Context);
        state->CpuMappingActive = 0;
    }
}

static HV_GPU_STATUS LoseOwnership(HV_GPU_STATE *state)
{
    ReleaseCpuMapping(state);
    state->Phase = HV_GPU_OWNERSHIP_LOST;
    return HV_GPU_NOT_SUPPORTED;
}

HV_GPU_STATUS HvGpuValidateExclusion(const HV_GPU_EXCLUSION_PROOF *proof,
                                     const HV_GPU_ADAPTER *adapter,
                                     const HV_GPU_ALLOCATION *allocation,
                                     uint64_t expectedSessionId)
{
    if (proof == NULL || adapter == NULL || allocation == NULL)
        return HV_GPU_INVALID_ARGUMENT;
    if (proof->Size != sizeof(*proof) ||
        proof->Version != HV_GPU_EXCLUSION_VERSION ||
        proof->Method != HV_GPU_EXCLUSION_BOOT_VPCI_EPT_VTD ||
        proof->EnforcementMask != HV_GPU_EXCLUSION_REQUIRED ||
        proof->GuestAccessMask != 0 || proof->LockedUntilReboot != 1 ||
        proof->PhysicalOutputVerified != 1 || proof->Reserved != 0 ||
        proof->SessionId == 0 ||
        (expectedSessionId != 0 && proof->SessionId != expectedSessionId) ||
        proof->Adapter.VendorId != adapter->VendorId ||
        proof->Adapter.DeviceId != adapter->DeviceId ||
        proof->Adapter.MmioPhysical != adapter->MmioPhysical ||
        proof->Allocation.PhysicalBase != allocation->PhysicalBase ||
        proof->Allocation.PinnedBytes != allocation->PinnedBytes ||
        proof->Allocation.GgttBase != allocation->GgttBase ||
        proof->Allocation.Pipe != allocation->Pipe ||
        proof->Allocation.Width != allocation->Width ||
        proof->Allocation.Height != allocation->Height ||
        proof->Allocation.StrideBytes != allocation->StrideBytes) {
        return HV_GPU_NOT_SUPPORTED;
    }
    return HV_GPU_OK;
}

static uint32_t ExclusionHeld(HV_GPU_STATE *state)
{
    HV_GPU_EXCLUSION_PROOF proof;
    volatile uint8_t *bytes = (volatile uint8_t *)&proof;
    size_t remaining = sizeof(proof);
    while (remaining-- != 0) *bytes++ = 0;
    if (state->Backend.ReadExclusion(state->Backend.Context, &proof) != 1 ||
        HvGpuValidateExclusion(&proof, &state->Adapter, &state->Allocation,
                               state->SessionId) != HV_GPU_OK) return 0;
    if (state->SessionId == 0) state->SessionId = proof.SessionId;
    return 1;
}

HV_GPU_STATUS HvGpuValidateExtent(const HV_GPU_ALLOCATION *allocation,
                                  HV_GPU_EXTENT *extent)
{
    HV_GPU_EXTENT candidate;
    uint64_t rounded;
    if (allocation == NULL || extent == NULL ||
        allocation->PhysicalBase == 0 ||
        (allocation->PhysicalBase & (HV_GPU_PAGE_BYTES - 1)) != 0 ||
        allocation->PinnedBytes == 0 ||
        allocation->PinnedBytes > HV_GPU_MAX_BYTES ||
        (allocation->PinnedBytes & (HV_GPU_PAGE_BYTES - 1)) != 0 ||
        allocation->PhysicalBase >= (UINT64_C(1) << 39) ||
        allocation->PinnedBytes >
            (UINT64_C(1) << 39) - allocation->PhysicalBase ||
        allocation->GgttBase == 0 ||
        (allocation->GgttBase & (HV_GPU_PAGE_BYTES - 1)) != 0 ||
        (uint64_t)allocation->GgttBase + allocation->PinnedBytes >
            (UINT64_C(1) << 32) ||
        allocation->Pipe > 3 || allocation->Width == 0 ||
        allocation->Height == 0 || allocation->Width > 65536 ||
        allocation->Height > 65536 || allocation->StrideBytes == 0 ||
        (allocation->StrideBytes & 63) != 0 ||
        allocation->StrideBytes / 64 > 4095 ||
        (uint64_t)allocation->StrideBytes < (uint64_t)allocation->Width * 4) {
        return HV_GPU_INVALID_ARGUMENT;
    }
    candidate.Bytes = (uint64_t)allocation->StrideBytes * allocation->Height;
    if (candidate.Bytes > HV_GPU_MAX_BYTES) return HV_GPU_INVALID_ARGUMENT;
    rounded = (candidate.Bytes + HV_GPU_PAGE_BYTES - 1) &
        ~(HV_GPU_PAGE_BYTES - 1);
    if (rounded > allocation->PinnedBytes) return HV_GPU_INVALID_ARGUMENT;
    candidate.MappingBytes = rounded;
    candidate.PageCount = (uint32_t)(rounded / HV_GPU_PAGE_BYTES);
    CopyBytes(extent, &candidate, sizeof(candidate));
    return HV_GPU_OK;
}

/* These reads validate an already-enforced reservation. They neither acquire
 * ownership nor walk arbitrary Windows GGTT entries. Scanout and CPU views
 * must resolve to precisely the same pre-boot pinned allocation. */
static HV_GPU_STATUS ValidateReservation(HV_GPU_STATE *state)
{
    HV_GPU_PLANE plane;
    uint32_t index;
    const HV_GPU_BACKEND *backend = &state->Backend;
    if (ExclusionHeld(state) != 1 ||
        backend->OwnershipHeld(backend->Context) != 1 ||
        backend->AddressingIsGgtt(backend->Context) != 1 ||
        HvGpuIntelReadPlane(backend, state->Allocation.Pipe, &plane) != HV_GPU_OK ||
        HvGpuIntelValidatePlane(&plane, &state->Allocation) != HV_GPU_OK) {
        return LoseOwnership(state);
    }
    for (index = 0; index < state->Allocation.PinnedBytes / HV_GPU_PAGE_BYTES;
         ++index) {
        uint64_t pte;
        uint64_t physical;
        if (backend->ReadGgttPte(backend->Context,
                state->Allocation.GgttBase / (uint32_t)HV_GPU_PAGE_BYTES + index,
                &pte) != 1 ||
            HvGpuIntelGgttPhysical(pte, &physical) != HV_GPU_OK ||
            physical != state->Allocation.PhysicalBase +
                (uint64_t)index * HV_GPU_PAGE_BYTES) {
            return LoseOwnership(state);
        }
    }
    /* Catch an inconsistent snapshot/read failure, including a pending flip.
     * This is defense in depth, not polling-based exclusion. */
    if (HvGpuIntelReadPlane(backend, state->Allocation.Pipe, &plane) != HV_GPU_OK ||
        HvGpuIntelValidatePlane(&plane, &state->Allocation) != HV_GPU_OK ||
        backend->AddressingIsGgtt(backend->Context) != 1 ||
        backend->OwnershipHeld(backend->Context) != 1 ||
        ExclusionHeld(state) != 1) {
        return LoseOwnership(state);
    }
    return HV_GPU_OK;
}

HV_GPU_STATUS HvGpuInitialize(HV_GPU_STATE *state,
                              const HV_GPU_ADAPTER *adapter,
                              const HV_GPU_ALLOCATION *allocation,
                              const HV_GPU_BACKEND *backend)
{
    HV_GPU_EXTENT extent;
    if (state == NULL) return HV_GPU_INVALID_ARGUMENT;
    if (state->Phase != HV_GPU_NEW) {
        HvGpuBeforeGuestResume(state);
        return HV_GPU_NOT_SUPPORTED;
    }
    if (state->CpuMappingActive != 0) return HV_GPU_INVALID_ARGUMENT;
    /* An absent boot backend never falls back to a guest-managed surface. */
    if (!BackendValid(backend)) {
        state->Phase = HV_GPU_OWNERSHIP_LOST;
        return HV_GPU_NOT_SUPPORTED;
    }
    if (adapter == NULL || allocation == NULL) return HV_GPU_INVALID_ARGUMENT;
    if (adapter->VendorId != UINT16_C(0x8086) ||
        adapter->DeviceId != HV_GPU_INTEL_UHD770_ID) {
        state->Phase = HV_GPU_OWNERSHIP_LOST;
        return HV_GPU_NOT_SUPPORTED;
    }
    if (adapter->MmioPhysical == 0 ||
        (adapter->MmioPhysical & (HV_GPU_PAGE_BYTES - 1)) != 0 ||
        adapter->MmioPhysical > UINT64_C(0x000ffffffff00000) ||
        HvGpuValidateExtent(allocation, &extent) != HV_GPU_OK) {
        return HV_GPU_INVALID_ARGUMENT;
    }
    CopyBytes(&state->Adapter, adapter, sizeof(*adapter));
    CopyBytes(&state->Allocation, allocation, sizeof(*allocation));
    CopyBytes(&state->Extent, &extent, sizeof(extent));
    CopyBytes(&state->Backend, backend, sizeof(*backend));
    if (ValidateReservation(state) != HV_GPU_OK) return HV_GPU_NOT_SUPPORTED;
    state->Phase = HV_GPU_READY;
    return HV_GPU_OK;
}

HV_GPU_STATUS HvGpuRender(HV_GPU_STATE *state,
                          const HV_OVERLAY_ENTITY_LIST *entities,
                          const HV_OVERLAY_MATRIX *viewProjection)
{
    uint32_t *view = NULL;
    HV_OVERLAY_STATUS rendered;
    if (state == NULL) return HV_GPU_INVALID_ARGUMENT;
    if (state->CpuMappingActive != 0) return LoseOwnership(state);
    if (state->Phase != HV_GPU_READY) return HV_GPU_NOT_SUPPORTED;
    if (entities == NULL) return HV_GPU_INVALID_ARGUMENT;
    if (ValidateReservation(state) != HV_GPU_OK) return HV_GPU_NOT_SUPPORTED;
    /* Arm cleanup BEFORE map: even a backend reporting a partial map failure
     * must drain and clear the entire attempted window before returning. */
    state->CpuMappingActive = 1;
    if (state->Backend.MapWc(state->Backend.Context,
            state->Allocation.PhysicalBase, state->Extent.MappingBytes,
            &view) != 1 || view == NULL || ((uintptr_t)view & 3) != 0) {
        (void)LoseOwnership(state);
        return HV_GPU_MAPPING_FAILED;
    }
    if (ValidateReservation(state) != HV_GPU_OK) return HV_GPU_NOT_SUPPORTED;
    rendered = HvOverlayRender(view, state->Allocation.StrideBytes,
        state->Allocation.Width, state->Allocation.Height,
        entities, viewProjection);
    if (ValidateReservation(state) != HV_GPU_OK) return HV_GPU_NOT_SUPPORTED;
    ReleaseCpuMapping(state);
    return rendered == HV_OVERLAY_OK ? HV_GPU_OK : HV_GPU_RENDER_FAILED;
}

void HvGpuBeforeGuestResume(HV_GPU_STATE *state)
{
    if (state != NULL && state->CpuMappingActive != 0) {
        /* An outstanding CPU map means its render was aborted. Never try to
         * resume it or re-arm the plane after this cleanup. */
        (void)LoseOwnership(state);
    }
}

void HvGpuShutdown(HV_GPU_STATE *state)
{
    if (state != NULL) {
        ReleaseCpuMapping(state);
        if (state->Phase != HV_GPU_OWNERSHIP_LOST) state->Phase = HV_GPU_STOPPED;
    }
}

#if defined(_MSC_VER)
#pragma code_seg(pop)
#endif
