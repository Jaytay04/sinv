# Prompt: Intel GPU Scanout Wrapper for the EFI Diagnostic Renderer

## Context

The hsinv hypervisor (`components/hypervisor/src/efi`) is an EFI-resident Intel VMX
hypervisor. Windows runs in VMX non-root; root owns all physical memory. We are
building a direct GPU scanout write path so root can render overlay pixels straight
to the framebuffer, removing the companion driver from the data output path.

The **renderer half is already done and committed** (commit `3e64513`, branch
`feat/smbios-boot-serial-probe`). It lives in:

- `components/hypervisor/src/efi/overlay_render.h/.c`
- `components/hypervisor/src/efi/font_rom.h`
- `components/hypervisor/src/efi/tests/overlay_render_test.c`

Its entry point is:

    HV_OVERLAY_STATUS HvOverlayRender(
        uint32_t *framebuffer,      /* caller-owned, exclusively writable, u32-aligned */
        uint32_t strideBytes,      /* bytes per row, divisible by 4, >= width*4       */
        uint32_t width,
        uint32_t height,           /* declared extent <= 8 MiB total                  */
        const HV_OVERLAY_ENTITY_LIST *entities,
        const HV_OVERLAY_MATRIX *viewProjection);

It is a pure software compositor: no device access, no mappings, no allocation, no
SIMD state management. A VMX-root caller must preserve guest SIMD/FPU state and use
masked floating-point exceptions before calling it. It blends 0xAARRGGBB source over
an opaque RGB destination.

**Do not re-implement the renderer.** Your job is only the thin GPU scanout wrapper
that finds the live framebuffer, maps it read-write, hands it to `HvOverlayRender`,
and unmaps it before guest resume.

## Deliverable

A small HAL behind `gpu_hal.h` (Intel implementation first; AMD/NVIDIA are secondary
targets and may be stubs that return a "not supported" status). The wrapper must:

1. **Discover the Intel iGPU display controller** from PCI config space in the EFI
   phase (vendor 0x8086, class 0x03 display). Enumerate the primary display pipe.
2. **Locate the active scanout surface** via the Intel display registers:
   - DSPSURF (pipe A/B) for the framebuffer physical address,
   - DSPLIN/DSPTYPE or the format register to determine pixel format and stride,
   - DSPSTRIDE where applicable.
   Support Gen 9-12 (UHD/Iris). Read the live surface address; do not assume a fixed
   offset.
3. **Map the bounded framebuffer region** as write-combing (WC) memory, read-write,
   from root. The mapping is the ONLY new read-write root mapping in the system.
   Bounded to the declared extent (<= 8 MiB). WC is required to tolerate the GPU/DWM
   writing the same pages concurrently and to avoid read-modify-write hazards.
4. **Call `HvOverlayRender`** into that mapping with the discovered stride/format.
5. **Unmap before guest resume.** If a VM-exit occurs during the write, the existing
   exit handler must release the mapping. Reuse the existing exit/teardown path; do
   not invent a new one.

## Constraints

- No dynamic allocation after boot-services exit. All buffers and state are static in
  hypervisor-private pages.
- No Windows API calls. No EFI runtime services calls after boot (an optional UEFI
  variable poll for control is acceptable if you need it, but prefer not to).
- The `.hvscan` self-containment check must pass: no external references introduced
  in the resident section. The renderer is already linked via `/INCLUDE:HvOverlayRender`.
- Add to the existing `uefi_hypervisor` source list in
  `components/hypervisor/src/efi/CMakeLists.txt`. No new CMake targets.
- Keep the scanner's read-only mapping unchanged (see `scanner.cpp`).

## Tests

Add a pure-model test that validates the wrapper logic that CAN be tested without
real hardware:
- PCI discovery / register parsing from a synthetic config-space image,
- Surface address + stride extraction from synthetic DSPSURF/DSPLIN values,
- The WC mapping bounds check (extent <= 8 MiB, alignment),
- That a "not supported" GPU returns a clean status and leaves no mapping behind.

The hardware path (actual scanout visible on screen) is validated separately in a VM;
do not block on it here.

## Acceptance Criteria

- `uefi_hypervisor.efi` builds with the new sources and passes `.hvscan`.
- Component CTest suite passes (existing 22 + your new wrapper tests).
- `gpu_hal.h` cleanly separates Intel from future AMD/NVIDIA.
- No new read-write root mapping is left mapped after guest resume.
- The renderer commit (`3e64513`) is untouched; you only add the wrapper.

## Working Rules

- Work in the same feature worktree/branch as the renderer
  (`feat/smbios-boot-serial-probe`), keeping main untouched.
- Conventional commits, no AI/"Co-Authored-By" attribution.
- Other agents may be working concurrently; preserve existing edits and do not rewrite
  files you do not own.
- Report: changed files + hashes, test count, `.hvscan` result, and the commit SHA.
