# Hypersinv transparent overlay

This is a standalone Windows user-mode visualization application. It does not
link to, load, or modify the hypervisor or its companion driver. It uses a
transparent, click-through Win32 window, Direct3D 11, Direct2D, and DirectWrite.

## Build

From a Visual Studio x64 developer prompt:

```powershell
cmake -S tools/overlay -B build/overlay -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/overlay
ctest --test-dir build/overlay --output-on-failure
```

No package manager or third-party dependency is required.

## Run

```powershell
# Visual smoke test over the current foreground window
build\overlay\hypersinv_overlay.exe --demo

# Follow a process's primary top-level window and consume the default mapping
build\overlay\hypersinv_overlay.exe --target-pid 1234

# Explicit window title and mapping
build\overlay\hypersinv_overlay.exe `
  --target-title "Application title" `
  --mapping "Local\HypersinvOverlayDataV2"
```

The overlay covers the target's client area using physical pixels. `WS_EX_TOOLWINDOW`
keeps it out of Alt-Tab and the taskbar, `WS_EX_NOACTIVATE` prevents focus
activation, and `HTTRANSPARENT` plus `WS_EX_TRANSPARENT` make it click-through.
These styles do not—and cannot—hide a top-level window from diagnostic APIs such
as `EnumWindows`.

## Shared-memory producer contract (HVO2)

Include `include/overlay_protocol.h` in the user-mode producer. The default
mapping is `Local\HypersinvOverlayDataV2`. The overlay opens it with
`FILE_MAP_READ`. `SharedMemoryPublisher` owns creation, the writer mutex, the
odd/even sequence protocol, and the 60 Hz rate cap. Its
`AuthenticatedUsersRead` mode gives LocalSystem full control and authenticated
interactive users read-only mapping access; use a `Global\` name when producer
and overlay run in different Windows sessions.

Publish a snapshot as follows:

1. Increment `Header.Sequence` so it is odd.
2. Issue a full memory barrier.
3. Update the header payload and up to 256 `EntityData` records.
4. Set `ProducerTimestampQpc` from `QueryPerformanceCounter` and optionally set
   `TargetWindow` to the target `HWND` value.
5. Issue a full memory barrier and increment `Header.Sequence` so it is even.

Initialize these fields exactly:

```cpp
buffer->Header.Magic = hypersinv::overlay::kOverlayMagic;
buffer->Header.Version = hypersinv::overlay::kOverlayVersion;
buffer->Header.HeaderSize = sizeof(hypersinv::overlay::SharedOverlayHeader);
buffer->Header.EntityStride = sizeof(hypersinv::overlay::EntityData);
buffer->Header.Capacity = hypersinv::overlay::kMaximumEntities;
```

Colors are `0xAARRGGBB`. Entity coordinates are relative to the target client
area. An entity can instead use `CoordinateSpace::World`; in that case the
header must set `kSceneHasViewProjection` and provide a row-major 4x4 matrix.
The overlay projects the world coordinate into the current target-client
viewport before drawing. The reader validates all counts and copies only
stable, even-sequence snapshots. Failed or torn reads render an empty frame
rather than stale data.

The authenticated companion CLI is the production bridge:

```powershell
# Run as LocalSystem after analysis-start/configure/scan/extract.
hvr1_runtime_service.exe analysis-publish `
  "Global\HypersinvOverlayDataV2" 0x0000000000011234 10000 none ascii

# The interactive overlay reads the same mapping.
hypersinv_overlay.exe --target-pid 1234 `
  --mapping "Global\HypersinvOverlayDataV2"
```

The bridge pages `ANALYSIS_RESULTS` through the authenticated HVR1 channel,
transforms typed fields into HVO2 records, and publishes no faster than 60 Hz.
It never gives the overlay the companion device handle or HMAC descriptor.
See [`docs/OVERLAY_PIPELINE.md`](../../docs/OVERLAY_PIPELINE.md) for field-name
conventions, transforms, and test vectors.

The window/renderer gate exercises the complete host-side path from publication
through a read-only snapshot, 3D projection, drawing, and D3D `Present`, and
requires completion below 50 ms.

## Safety boundary

- No global hooks.
- No DLL injection or remote-process memory access.
- No kernel driver.
- The shared-memory mapping is read-only in the overlay.
- Closing the target window or sending `WM_CLOSE` releases all DirectX, mapping,
  window, and class resources.
