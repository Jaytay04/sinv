# Caller-owned diagnostic renderer

`components/hypervisor/src/efi/overlay_render.h` exposes a C11 renderer and a
fixed 256-entry entity list. It draws into ordinary caller-owned RAM. It does
not acquire a framebuffer, map physical memory, register a runtime callback,
change the scanner's read-only mapping policy, or access a display device.
The existing EFI target retains the implementation and ROM in its resident
image so the section audit covers them. This adds resident bytes without
installing a rendering call site or changing how analysis results are published.

```c
#include "overlay_render.h"

static uint32_t diagnosticPixels[640 * 480];
static HV_OVERLAY_ENTITY_LIST diagnosticEntities;

void RenderDiagnosticFrame(void)
{
    diagnosticEntities.Count = 1;
    diagnosticEntities.Entities[0].Coordinates = HV_OVERLAY_SCREEN;
    diagnosticEntities.Entities[0].X = 24;
    diagnosticEntities.Entities[0].Y = 40;
    diagnosticEntities.Entities[0].Width = 96;
    diagnosticEntities.Entities[0].Height = 64;
    diagnosticEntities.Entities[0].Color = UINT32_C(0xffff8000);
    diagnosticEntities.Entities[0].FillColor = UINT32_C(0x20102030);
    diagnosticEntities.Entities[0].LabelLength = 2;
    diagnosticEntities.Entities[0].Label[0] = 'V';
    diagnosticEntities.Entities[0].Label[1] = 'M';
    (void)HvOverlayRender(diagnosticPixels, 640 * 4, 640, 480,
        &diagnosticEntities, 0);
}
```

The caller presents `diagnosticPixels` using its explicit VM display interface.
That interface and its lifetime are outside this module. The renderer never
clears the full destination or retains a destination pointer; the caller supplies
the base pixels again for each frame. Repeated translucent drawing on the same
unchanged background accumulates color, so provide a fresh background as needed.

## Data contract

- `strideBytes` is bytes per row, a multiple of four and at least `width * 4`.
  Allocate at least `strideBytes * height` bytes, including padding. The renderer
  caps this declared extent at 8,388,608 bytes and leaves row padding untouched.
  A raw pointer API cannot establish the actual allocation size.
- Pixels and colors use numeric `0xAARRGGBB`, stored as BGRA bytes on x86-64.
  Source alpha is straight alpha over an opaque RGB destination, with integer
  round-to-nearest division by 255. Alpha zero leaves every destination bit
  untouched; other writes set destination alpha to 255. This is byte-space
  blending, without gamma conversion or premultiplied-alpha interpretation.
- A box uses half-open bounds and a one-pixel border. Each border pixel is drawn
  once, including corners and one-pixel-wide boxes. Optional fill covers the
  interior. Labels start 16 pixels above the top-left anchor. Drawing clips to
  the viewport using 64-bit coordinate arithmetic.
- Labels have an explicit maximum length of 32 ISO-8859-1 bytes, with no NUL
  requirement. All 256 glyph slots exist. Control/spacing slots retain blank
  upstream glyphs. This is a byte font, not UTF-8 or CP437.
- World anchors use a row-major 4x4 `float` matrix and column vectors. Projection
  uses homogeneous division, Direct3D depth `0 <= z <= w`, positive `w`, and
  flips the vertical axis. NDC endpoints map to `0` and `dimension - 1`, with
  nonnegative results truncated to integer pixels. Outside-view anchors are
  skipped; nonfinite inputs reject the complete list before output writes.
- Input structures and destination must not overlap. The caller prevents
  concurrent writes to the destination and keeps inputs stable for the call.

## Resident use and verification boundary

Code and ROM use the existing `.hvscan` segments on MSVC and must pass its
unchanged 16-page self-containment audit. The module has no heap, writable
global workspace, OS/EFI calls, or device dependencies. The caller can keep
its framebuffer and entity array in private static pages; per-call workspace
is bounded stack storage independent of entity count.

Projection uses floating-point/SIMD instructions. The current VM-exit assembly
does not establish an extended-state save/restore contract for this renderer.
A future VMX-root caller must supply that contract and suitable floating-point
control state (including masked exceptions) before entering the renderer. This change does not attach
the renderer to that exit path. No live root-mode or display proof is claimed.
There is no timing guarantee or 60 Hz scheduler in this first pass.

The portable `uefi_hypervisor_overlay_render_tests` target compiles the same C
implementation against caller-owned static test buffers. The tests cover
blending, known projection matrices, invalid inputs, clipping and canaries,
font data, and full-list rendering without VMX, drivers, elevation, or a VM.

From an x64 Visual Studio developer PowerShell in this feature worktree, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\test-diagnostic-renderer.ps1
```

This configures a separate Release build under `spoof-handoff/b/or`, builds the
EFI image and portable component tests with drivers/services disabled, runs the
existing `.hvscan` post-build audit, then runs CTest. Logs are under
`spoof-handoff/reports/diagnostic-renderer/`. It performs no installation or boot.

## Font provenance

`font_rom.h` derives from Daniel Hepper's public-domain
[font8x8](https://github.com/dhepper/font8x8), credited there to Marcel Sondaar
and IBM public-domain VGA fonts. Basic Latin, control, and extended Latin data
are concatenated into ISO-8859-1 order, and each original row is repeated twice.
The header records input SHA-256 values. To regenerate from those source files:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\generate-overlay-font.ps1 -InputDirectory .\spoof-handoff\artifacts\overlay-font-input -OutputPath .\components\hypervisor\src\efi\font_rom.h
```
