# GPU HAL VM Validation

The EFI enforcement model, an actual VMX-root WC framebuffer mapping, and
nested VMX launch/return passed in a new offline VMware VM on 2026-10-19.
The native Intel backend builds, but its PCI/VT-d enforcement and physical
scanout have not been executed against an Intel GPU. Native activation remains
opt-in and subject to the physical prerequisites in
[GPU_DIAGNOSTIC_ACTIVATION.md](GPU_DIAGNOSTIC_ACTIVATION.md).

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
`.codex-build/gpu_hal_vm/run-06` directory. Existing VMs and host display,
driver, boot, virtualization and security settings were not changed.

The booted image was built with `HYPERSINV_GPU_VM_SELFTEST=ON`. Its SHA-256 is
`c83a02f329dee3341fa71a45de33d98ebcdb1708d1c1c84a9fc8ad44a96e0903`.
The copied boot medium has the same hash. Native activation was disabled in
this VM image; the two activation modes cannot be combined. The VM exposes
VMware SVGA, not an Intel GPU. The normal build
leaves the self-test option off. All three configurations passed the resident
audit against the 65,536-byte allocation:

| Build | Self-contained `.hvscan` bytes |
| --- | ---: |
| Default | 54,870 |
| Native and physical-output options enabled, build only | 54,870 |
| VM self-test | 56,822 |

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
GPU_ENFORCEMENT_MODEL:ROOT_DEVICE_PLAN_READ_ONLY PASS
GPU_ENFORCEMENT_MODEL:ROOT_DOMAIN_AUDIT_READ_ONLY PASS
GPU_ENFORCEMENT_MODEL:LEGACY_DENY_INVENTORY PASS
GPU_ENFORCEMENT_MODEL:TARGET_CONTEXT_ONLY PASS
GPU_ENFORCEMENT_MODEL:GUEST_FILTER_POLICY_IO_BITMAP PASS
GPU_ENFORCEMENT_MODEL:ADMIT_RENDER_AND_UNMAP_MODEL_BUFFER PASS
GPU_ENFORCEMENT_MODEL:NOT_SUPPORTED_NO_MAP_LATCH PASS
GPU_ENFORCEMENT_MODEL:ROOT_PTE_AUDIT_LATCH PASS
GPU_ENFORCEMENT_MODEL:ROOT_CONTEXT_AUDIT_LATCH PASS
GPU_ENFORCEMENT_MODEL:PLANE_LATCH PASS
GPU_ENFORCEMENT_MODEL:LIVE_SURFACE_LATCH PASS
GPU_ENFORCEMENT_MODEL:GGTT_LATCH PASS
GPU_ENFORCEMENT_MODEL:OWNERSHIP_LOSS_DRAIN_UNMAP PASS
GPU_ENFORCEMENT_MODEL:ALL_PASS SYNTHETIC_ONLY
GPU_HAL_MODEL:ALL_PASS
GPU_HAL_VM:ROOT_WC_NO_BACKEND PASS
GPU_HAL_VM:ROOT_WC_BOUNDS PASS
GPU_HAL_VM:ROOT_WC_RENDER PASS
GPU_HAL_VM:ROOT_WC_UNMAPPED PASS
GPU_HAL_VM:VMX_FIRMWARE_RETURN PASS
```

The synthetic exercises run **before VMX launch**. The enforcement model uses
the production per-device planner, sibling VT-d domain builder/auditor, guest
filter policy, WC page-table helpers, HAL and renderer with synthetic hardware
readback. Valid exclusion admits rendering; absent evidence, changed plane,
surface, GGTT, context or DMA PTE rejects and latches. It substitutes model RAM
for the WC virtual address; those model markers do not prove live PCI/EPT/VT-d
enforcement.

The `ROOT_WC_*` checks run separately inside VMX root through the production
native WC backend. They use a private VM-RAM page, remove its root WB alias,
load the root PAT, reject oversized and unaligned requests, render a known red
box with the unchanged compositor, check pixels and an untouched sentinel,
issue the store fence, clear the WC leaves and invalidate their local TLB
entries before guest resume. A null HAL backend returns `HV_GPU_NOT_SUPPORTED`
with no active mapping. This verifies CPU mapping and rendering, not an Intel
DMA engine or scanout. The firmware-return marker separately proves completion
of VMXON, VMCS setup, VMLAUNCH and the existing firmware-return path in this VM.

Headless component validation passed 29/29 CTests, including the existing HAL
cases, the shared enforcement model, seven guest-filter cases, four WC test groups and six
preflight-script fixture cases. The resident audit also checks bounded RVA
jump tables so dumpbin's decoding of table bytes cannot become a false
external-call report.
The renderer from `3e64513` and `scanner.cpp` remain unchanged.

## Evidence and limits

The generated evidence is retained under `.codex-build/gpu_hal_vm/run-06`:

- `result.json`: separate synthetic enforcement, actual root WC and VMX results,
  offline configuration and snapshot status.
- `serial.log`: captured EFI output, validation failures tested as expected, and VMX return.
- `commands.jsonl` and per-command logs: tool arguments, exit status and timing.
- `gpu-hal-disposable.vmx` and `.vmsd`: VM configuration and both snapshot records.
- `vmware.log`: Workstation version and VM runtime log.

`captureScreen` failed because VMware required an authenticated guest operation.
This EFI-only VM has no guest login; no screenshot was produced. Its failure
text is retained in `06-capture-screen.stdout.log`. The VMware virtual display
is not an Intel Gen12 display, so real Intel register programming and
guest-visible scanout pixels were not tested.

This run does **not** cover real Intel PCI/ECAM/MMIO exclusion or DMA isolation,
physical Alder Lake/PCH scanout, an Intel-excluded/NVIDIA-primary Windows
topology, display modesets, post-ExitBootServices operation, or a Windows driver
session. The actual WC mapping uses the VM's PAT/MTRR environment; physical
UHD 770 cache policy is unvalidated. Those hardware and Windows claims require
separate controlled validation. No primary-host activation was performed.

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
