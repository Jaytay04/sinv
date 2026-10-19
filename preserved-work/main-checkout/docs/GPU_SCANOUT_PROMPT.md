Implement a direct GPU scanout write path from VMX root in the hsinv hypervisor, eliminating the need for the HVR1 companion driver as the data output transport.

## Current Architecture

- The EFI-resident hypervisor runs in VMX root mode. Windows runs in VMX non-root.
- Root already owns all physical memory and has a read-only mapping primitive (`HvEfiScannerMapReadOnly`).
- The analysis engine (page-table walker, scanner, traversal, extraction) runs entirely in root.
- Currently, results are pushed to Windows via: VMCALL → companion WDM driver → LocalSystem service → shared memory → user-mode overlay.
- The companion driver (`hvr1_efi_runtime_companion.sys`) is the detection surface we want to eliminate.

## Goal

Root writes rendered overlay pixels directly to the active display framebuffer (or GPU overlay plane) each frame. No Windows driver, no shared memory, no VMCALL from ring-0 for the data path. Pixels appear on screen as if the hardware drew them.

## Requirements

### 1. Framebuffer Discovery (EFI phase)

At boot, before Windows handoff, identify the active scanout buffer:
- Read the active display adapter's framebuffer physical address and stride from firmware/GPU MMIO config.
- For Intel iGPUs (primary target): read the DSPSURF register (display surface base) from the GPU's uncore MMIO space. The physical address is the canonical framebuffer location.
- Store: framebuffer physical base, stride (bytes per row), bit depth (assume 32-bit RGBA/BGRA), and active resolution (width × height from DSPLINE/DSPSTRIDE).
- If multiple displays are active, target the primary scanout pipe (Pipe A / DSPA*).
- Fallback: if GPU MMIO is not accessible from EFI (rare), use the UEFI `GraphicsOutput` protocol's frame buffer pointer as a hint, then verify against GPU registers after VMXON.

### 2. Per-Frame Write Path (VMX root)

After each analysis pass (≤60 Hz, gated by TSC):
1. Root composites the overlay content (entity boxes, labels, crosshairs) into a small local RGBA buffer in hypervisor-private memory.
2. Root maps the target framebuffer region (bounded: overlay bounding box, not full screen) using its existing read-write mapping primitive. NOTE: this requires a READ-WRITE mapping, not read-only. The mapping must be:
   - Bounded to the overlay region (e.g., max 1920×1080×4 = 8 MiB, typically much less for sparse content)
   - Unmapped immediately after the write completes
   - Non-cacheable or write-combining (WC) to avoid coherency issues with the GPU's own writes
3. Root performs alpha-blended writes of overlay pixels to the framebuffer at the target position.
4. Root unmaps the region.
5. Guest resumes.

### 3. Timing / Race with DWM

Windows DWM composites and writes the framebuffer every frame (~16.6 ms at 60 Hz). To avoid your write being overwritten:
- **Preferred:** Write during vertical blank (VBlank). The GPU is not scanning out during VBlank, so your write persists until the next frame's DWM update. Detect VBlank by reading the GPU's vertical counter register (e.g., Intel: `PIPESYNCS` or scanline position register) and polling until the counter wraps past the total scanlines.
- **Acceptable fallback:** Write every frame after DWM's write. At 60 Hz, an occasional overwritten frame is imperceptible. Gate writes to once per detected guest frame (use TSC delta ≥ 16 ms).
- **Optional (advanced):** Use the GPU's hardware overlay plane (video overlay) if available. Intel iGPUs have a dedicated overlay surface register (e.g., `VSPSURF` on older gens, or the plane-based model on Gen 11+). Writing to the overlay plane's surface bypasses DWM entirely.

### 4. Overlay Rendering in Root

Implement a minimal renderer in root (C, no allocations after init):
- Fixed-size entity array (max 256 entities, same as current HVO2).
- Primitives: axis-aligned bounding box (4 lines + optional fill), text (use a pre-rasterized 8×16 font ROM in hypervisor-private memory — no TTF, no DirectWrite).
- Color: `0xAARRGGBB` per entity, alpha-blended onto the framebuffer.
- Position: screen-space coordinates (pixels from top-left of primary display).
- World-to-screen projection: root receives the view-projection matrix from the analysis pass and projects 3D positions to 2D screen coords (standard perspective divide + NDC→pixel mapping).
- Total render budget: < 50 µs per frame for 256 entities.

### 5. Control Path (replaces companion driver commands)

Without the companion driver, control commands (start/stop/shutdown) need an alternative:
- **Option A (preferred):** Keyboard shortcut intercepted in root. The hypervisor already handles VM-exits; add a scan-code intercept for a specific key combo (e.g., Ctrl+Shift+F12) that toggles overlay visibility or triggers shutdown. No driver needed.
- **Option B:** Firmware variable write. A tiny user-mode tool writes a UEFI variable; root polls it each frame (cheap: one variable read per frame via the existing EFI runtime services stub). 
- **Option C:** Keep the companion driver ONLY for control (start/stop/shutdown/status), but remove it from the data path. The driver becomes a one-shot command interface, not a continuous VMCALL pump. It can be unloaded after the session starts if root has a persistent data pump.

Implement Option A. Keep Option C as a fallback for initial debugging.

### 6. Build Integration

- New source files under `components/hypervisor/src/efi/`:
  - `gpu_scanout.h/.c` — framebuffer discovery, mapping, write path
  - `overlay_render.h/.c` — root-side compositor (boxes, text, projection)
  - `font_rom.h` — pre-rasterized 8×16 font bitmap (generate from a public-domain font)
- These are linked into the existing `uefi_hypervisor.efi` target.
- No new CMake targets. Add to the existing `uefi_hypervisor` source list.
- The `.hvscan` self-containment check must pass (no external references in the resident section).
- Add a pure-model test (`overlay_render_test.c`) that validates:
  - Alpha blending math
  - World-to-screen projection with a known matrix
  - Bounding-box clipping at screen edges
  - Font ROM integrity (all 256 glyphs present, correct dimensions)

### 7. Constraints

- No dynamic allocation after EFI boot-services exit. All buffers are statically allocated in hypervisor-private pages.
- The framebuffer write mapping is the ONLY read-write root mapping. It must be unmapped before guest resume. If a VM-exit occurs during the write (interrupt, etc.), the mapping is released by the existing exit handler.
- Maximum overlay region: 8 MiB (full 1080p frame). Typical usage: < 64 KiB for sparse entities.
- The write must be tolerant of the GPU writing to the same pages concurrently (DWM). Use WC (write-combining) memory type for the mapping to avoid read-modify-write hazards.
- Must work on Intel UHD/Iris graphics (Gen 9–12). AMD/NVIDIA are secondary targets; structure the GPU register access behind a small HAL (`gpu_hal.h`) with an Intel implementation first.
- No Windows API calls. No EFI runtime services calls after boot (except the optional variable poll for Option B control).

### 8. What This Replaces

- `hvr1_runtime_service.exe analysis-publish` (continuous VMCALL pump)
- The HVO2 shared memory mapping (`Global\HypersinvOverlayDataV2`)
- `hypersinv_pub.exe` (user-mode ESP reader)
- `hypersinv_overlay.exe` (D3D11/Direct2D renderer)

The companion driver (`hvr1_efi_runtime_companion.sys`) and its service remain available for:
- Analysis session control (start/stop/configure/scan/traverse/extract)
- Process lookup
- Shadow EPT bind/unbind
- Shutdown/detach

But the **data output path** no longer requires them. Root renders directly.

### 9. Acceptance Criteria

- `uefi_hypervisor.efi` builds with the new sources, passes `.hvscan`.
- Component CTest suite passes (existing + new `overlay_render_test`).
- In a VM with GPU passthrough or emulated GPU: root writes a known test pattern to the framebuffer; the guest displays it.
- With the analysis engine running: entity boxes and labels appear on screen at 60 Hz without the companion driver loaded.
- Unloading (or never loading) the companion driver does not affect overlay visibility.
- No new entries in `PS_MODULE_LIST` beyond what Windows itself loads.
