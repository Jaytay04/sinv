# Intel Diagnostic GPU Activation Design

## Status and scope

This document defines the admission design for a dedicated Intel diagnostic adapter whose physical display output is excluded from Windows guest control. Windows continues to use the NVIDIA adapter. Same-screen composition over the NVIDIA desktop is out of scope; it would require a cooperating guest display driver or DWM/MPO integration.

Production activation is not implemented. The current HAL must return `HV_GPU_NOT_SUPPORTED`; there is no trusted exclusion provider or activation script. This document is a design and preflight contract, not evidence that the target host meets it. No host configuration change is authorized by this design.

## Required host and boot sequence

1. **Host preflight (read-only, before reboot).** Confirm the Intel/PCH adapter has an accessible HDMI or DisplayPort connector, attach a diagnostic display, and verify the NVIDIA adapter remains the Windows desktop path. `components/hypervisor/setup/Get-GpuDiagnosticPreflight.ps1` reports adapters and connected-monitor relationships, not physical connector routing. Its output cannot prove ownership or authorize activation. Stop if the physical output is unavailable or the intended topology is uncertain.
2. **One-shot boot admission (EFI/VMM, before launching Windows).** The platform must enter the diagnostic boot mode with the Intel adapter assigned to the root VMM for the entire session. This is an EFI/VMM virtual-PCI and I/O/MMIO isolation mechanism, locked until a full platform reset. A Windows WDDM stop or PnP disable is insufficient: it does not prevent guest reprogramming and can persist across reboot. Do not use host-side GPU-disable commands as an activation mechanism.
3. **Allocate before freezing protection.** Quiesce firmware writers, then allocate and pin the bounded private framebuffer (at most 8 MiB) and required GGTT resources before boot-services exit. Build the final private-memory inventory with those allocations included. No subsequent guest launch may expose these pages.
4. **Install exclusion before guest launch.** Trap CF8-CFF on every vCPU, maintaining the configuration-address selector for correct emulation. Return absent-device values for target-BDF data reads and discard target data writes; preserve other devices' configuration access. Filter or shadow ECAM consistently. Hide the Intel graphics ACPI/OpRegion exposure. Make BAR0 display/GGTT registers, BAR2 aperture and command paths, and reset/power controls unreachable to the guest. EPT violations for these regions must be emulated or denied safely; the current terminal EPT handler cannot serve as this policy. Install requester-specific DMA permissions for scanout reads from the pinned allocation and any required translation resources, without exposing other private RAM or sharing the guest's read/write domain. Freeze the policy on all processors before Windows starts.
5. **Configure and validate scanout (EFI).** Verify installed protection by readback; configuration flags or a preflight JSON report are not proof. Establish the reserved GGTT translation and root WC CPU window without a conflicting WB alias. Configure the supported uncompressed linear plane, disable DPT addressing, arm its GGTT surface address, and wait for the live surface. Validate the plane configuration, live surface and every reserved GGTT entry before admitting the HAL. This does not perform a full monitor modeset implementation.
6. **Render.** The HAL obtains a trusted `HV_GPU_EXCLUSION_PROOF` from its backend, validates it before initialization and again around reservation checks, then renders only to the validated root-owned surface. The proof is bound to the adapter and allocation, carries a nonzero session ID, identifies the boot virtual-PCI/EPT/VT-d method, and requires all ten enforcement bits, zero guest-access bits, a lock-until-reboot guarantee, and verified physical output. Callers must not supply a JSON report or self-asserted booleans as proof. The caller preserves guest SIMD/FPU state; each render drains and unmaps its CPU window before guest resume, while the GPU mapping stays pinned.

The proof is an admission record for protections already enforced by the boot backend; it does not install isolation itself. Once ownership loss is detected, the state remains failed until reboot. The HAL stops writes, drains pending CPU writes, removes its CPU mapping, returns `HV_GPU_NOT_SUPPORTED`, keeps backing pages pinned, and never re-arms the plane. It does not poll registers and treat matching values as proof of exclusive ownership.

## Repository readiness

The required backend does not currently exist in the repository. I/O bitmaps currently trap only VMware port `0x5658`; EPT is identity-mapped except for private pages and VT-d covers a shared read/write domain for PCI functions. ACPI shadowing removes DMAR only. These mechanisms do not exclude the Intel adapter or establish requester-specific scanout DMA permissions. Consequently there is no enabled provider today; the HAL remains `HV_GPU_NOT_SUPPORTED` until the complete enforcement backend and its readback checks exist.

## Failure and teardown

Any missing, inconsistent, or changed proof, plane, surface, or reserved GGTT entry latches ownership loss for the session. Stop and drain CPU writes, unmap the CPU mapping, retain pinned pages, and do not restore or re-arm stale display state. HAL shutdown may stop rendering only; do not perform a mid-session VMX-off/remap/free hand-back while the GPU remains locked to the root surface.

Recovery requires a verified full platform reset followed by a fresh, unarmed boot. The platform must reset/quiesce the GPU and IOMMU before reclaiming pinned pages. Do not assume hybrid shutdown or a warm reset has quiesced DMA; use a full power cycle when that guarantee is unavailable. There is no persistent disable/re-enable operation and no mid-session hand-back to WDDM. A safe inverse hand-back is future work. The operator-supplied bare-metal runbook supplies the one-shot/reboot lifecycle only; this task executes payloads exclusively in disposable VMs and does not run its host deployment commands.

## Validation boundary

Pure-model tests exercise proof rejection (`HV_GPU_NOT_SUPPORTED`), plane and GGTT validation, and the ownership-loss latch without hardware. `HYPERSINV_GPU_VM_SELFTEST=ON` builds a separate test image in the existing EFI target. It refuses outside a VMware CPUID environment, runs synthetic register/GGTT/proof/RAM tests before VMX launch, and emits `GPU_HAL_MODEL` markers. A successful later `GPU_HAL_VM:VMX_FIRMWARE_RETURN PASS` separately proves nested VMX launch/return. Neither marker asserts an installed native exclusion backend or real WC mapping.

`components/hypervisor/setup/Test-GpuHalDisposableVm.ps1` creates a new offline EFI VM with nested virtualization and before/after snapshots; it does not modify an existing VM or host settings. VMware SVGA is not an Intel GPU: CPU-checked model pixels and a VM-console capture do not prove Intel hardware scanout. VM results cannot establish Alder Lake PCH physical scanout or the host's Intel-excluded/NVIDIA-primary topology. Keep runtime evidence and its scope in the generated test report.

## References

- [PCI configuration access](https://learn.microsoft.com/en-us/windows-hardware/drivers/pci/accessing-pci-device-configuration-space)
- [DevCon Disable](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/devcon-disable)
- [Intel VT-d specification](https://cdrdv2-public.intel.com/831418/vt-directed-io-spec.pdf)
- Repository implementation context: `components/hypervisor/src/efi/gpu_hal.h`, `components/hypervisor/src/efi/efi_hypervisor.c`, `components/hypervisor/src/efi/efi_hypervisor_math.c`, and `components/hypervisor/src/efi/iommu_runtime.c`.
