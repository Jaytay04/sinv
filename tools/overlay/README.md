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
  --mapping "Local\HypersinvOverlayDataV1"
```

The overlay covers the target's client area using physical pixels. `WS_EX_TOOLWINDOW`
keeps it out of Alt-Tab and the taskbar, `WS_EX_NOACTIVATE` prevents focus
activation, and `HTTRANSPARENT` plus `WS_EX_TRANSPARENT` make it click-through.
These styles do not—and cannot—hide a top-level window from diagnostic APIs such
as `EnumWindows`.

## Shared-memory producer contract

Include `include/overlay_protocol.h` in the user-mode producer. The default
mapping is `Local\HypersinvOverlayDataV1`. The overlay opens it with
`FILE_MAP_READ`; the producer is responsible for mapping access control.

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
area. The reader validates all counts and copies only stable, even-sequence
snapshots. Failed or torn reads render an empty frame rather than stale data.

## Safety boundary

- No global hooks.
- No DLL injection or remote-process memory access.
- No kernel driver.
- The shared-memory mapping is read-only in the overlay.
- Closing the target window or sending `WM_CLOSE` releases all DirectX, mapping,
  window, and class resources.
