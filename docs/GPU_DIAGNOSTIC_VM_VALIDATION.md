# GPU HAL VM Validation

The EFI model checks and nested VMX launch/return passed in a new offline
VMware VM on 2026-10-19. Native Intel exclusion and hardware scanout remain
unimplemented and gated; this result does not authorize host activation.

## VM configuration

| Setting | Observed value |
| --- | --- |
| Hypervisor | VMware Workstation 26.0.0 build 25388281 |
| Virtual hardware | Version 22 |
| CPU and memory | One vCPU, 512 MiB, `vhv.enable = "TRUE"` |
| Firmware | EFI; Secure Boot disabled for the unsigned test image |
| Boot medium | New FAT/VMDK containing only the test EFI fallback application |
| Guest network | No NIC (`ethernet0.present = "FALSE"`) |
| Physical device assignment | None; no Intel GPU passthrough |
| Guest OS | EFI firmware only; Windows was not installed or booted |
| Before snapshot | `before-gpu-hal`, powered off |
| After snapshot | `after-gpu-hal`, powered off after the test |
| Final state | VM stopped; independent `vmrun list` reported zero running VMs |

The VM was created beneath the primary workspace's ignored
`.codex-build/gpu_hal_vm/run-03` directory. Existing VMs and host display,
driver, boot, virtualization and security settings were not changed.

The booted image was built with `HYPERSINV_GPU_VM_SELFTEST=ON`. Its SHA-256 is
`9f48547032a1bcf7386d380259b53e767c9042f914931f265e0da10fd269f46d`.
The normal build leaves that option off. Both images passed the resident audit:
`.hvscan` is self-contained at 40,214 bytes against the 65,536-byte allocation.

## Results

The VM serial capture contains these test-image markers:

```text
GPU_HAL_MODEL:ENVIRONMENT VMWARE_TEST_ONLY
GPU_HAL_MODEL:PCI_DISCOVERY_SYNTHETIC PASS
GPU_HAL_MODEL:EXCLUSION_PROOF SIMULATED
GPU_HAL_MODEL:PLANE_AND_GGTT_VALID PASS
GPU_HAL_MODEL:RENDER_RED_BOX PASS
GPU_HAL_MODEL:MAP_DRAIN_UNMAP PASS
GPU_HAL_MODEL:PLANE_LATCH PASS
GPU_HAL_MODEL:LIVE_SURFACE_LATCH PASS
GPU_HAL_MODEL:GGTT_LATCH PASS
GPU_HAL_MODEL:NOT_SUPPORTED_NO_MAP PASS
GPU_HAL_MODEL:OWNERSHIP_LATCH_CLEANUP PASS
GPU_HAL_MODEL:ALL_PASS
GPU_HAL_VM:VMX_FIRMWARE_RETURN PASS
```

The synthetic HAL exercise runs **before VMX launch**, using static RAM and
simulated PCI, plane, GGTT, exclusion and mapping callbacks. It invokes the
same production HAL validation/render functions. Its known red-box pixel is
checked in CPU memory; it is not displayed through a virtual Intel plane.
The later VMX marker separately proves that the existing EFI payload completed
VMXON, VMCS setup, VMLAUNCH and its firmware-return path inside this VM. The
serial log also records the individual VMX setup PASS markers.

Headless component validation passed 25/25 CTests, including 14 HAL model
cases, the shared EFI model exercise and six preflight-script fixture cases.
The renderer from `3e64513` and `scanner.cpp` remain unchanged.

## Evidence and limits

The generated evidence is retained under `.codex-build/gpu_hal_vm/run-03`:

- `result.json`: separate model/VMX results, offline configuration and snapshot status.
- `serial.log`: captured EFI output, validation failures tested as expected, and VMX return.
- `commands.jsonl` and per-command logs: tool arguments, exit status and timing.
- `gpu-hal-disposable.vmx` and `.vmsd`: VM configuration and both snapshot records.
- `vmware.log`: Workstation version and VM runtime log.

`captureScreen` failed because VMware required an authenticated guest operation.
This EFI-only VM has no guest login; no screenshot was produced. Its failure
text is retained in `06-capture-screen.stdout.log`. The VMware virtual display
is not an Intel Gen12 display, so real Intel register programming and
guest-visible scanout pixels were not tested.

This run does **not** cover physical Alder Lake/PCH scanout, an enforced native
Intel-excluded/NVIDIA-primary topology, real WC/PAT/MTRR mappings, display
modesets, post-ExitBootServices operation, or a Windows driver session. Those
claims require the exclusion backend and the appropriate controlled hardware
or Intel-capable VM. Host-only observations and eventual physical validation
cannot be inferred from this VM result.

## Reproduction

Build the existing component target with the VM option on in a separate build
directory, keeping the normal build's option off. Then run
`components/hypervisor/setup/Test-GpuHalDisposableVm.ps1` with `-EfiPath`, a
fresh `-RunDirectory` under the primary workspace's `.codex-build`, and that
workspace's `-WorkspaceRoot`. Use the Windows account that owns the VMware
session; sandboxed VIX access may be unable to open the VM.

The runner requires snapshots before and after, disables the guest NIC, allows
no physical passthrough, bounds helper execution, captures serial output, and
stops only the VM it created. It does not disable host security features when
nested virtualization is unavailable. A failed required marker produces a
failed report; it cannot be replaced with a headless-model result.
