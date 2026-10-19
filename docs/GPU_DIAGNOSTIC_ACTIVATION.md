# Intel Diagnostic GPU Activation

## Scope and prerequisites

The native path targets Intel Alder Lake-S UHD 770 (PCI `8086:4680`, segment 0, `00:02.0`) as a dedicated diagnostic adapter. Windows must use a separate display adapter. Same-screen composition over the Windows desktop is out of scope and would require a cooperating guest display driver or DWM/MPO integration.

Native activation is opt-in at build time with `HYPERSINV_GPU_DIAGNOSTIC_NATIVE=ON`; it also requires the separate `HYPERSINV_GPU_DIAGNOSTIC_OUTPUT_CONFIRMED=ON` declaration after an operator physically verifies and attaches the Intel display output. EFI can validate an active Intel GOP pipe, but cannot prove that a physical sink is attached. Both options default off. A failed or missing ownership proof leaves admission at `HV_GPU_NOT_SUPPORTED`.

Firmware and SMM are trusted platform components. The guest-exclusion proof covers guest/Windows control paths; it does not attest that firmware or SMM cannot modify the display plane, GGTT, framebuffer, PAT, or MTRRs. If firmware/SMM writes the plane or DMA-writes the surface, root ownership can be silently violated; transient writes that restore the validated register values may not trigger the ownership latch. Firmware must also leave the physical cache policy unchanged while rendering. This trust assumption is separate from, and does not replace, guest PCI/MMIO filtering, the GPU DMA domain, or the bounded root WC mapping.

## Boot activation

Activation is one-time for the boot session and takes place before Windows guest launch:

1. On the host, verify the Intel connector and attach a diagnostic display; confirm Windows will use NVIDIA. `components/hypervisor/setup/Get-GpuDiagnosticPreflight.ps1` reports PnP adapter and monitor relationships, but does not establish physical connector routing or authorize activation. Do not change host driver bindings as a substitute for the EFI/VMM boundary.
2. Build with both native and physical-output options enabled only after that physical verification. Before boot-services exit, EFI discovers the supported Intel PCI/GOP device, validates BAR resources and the active linear plane, allocates and pins the bounded diagnostic surface and private state, and reserves entries in the protected stolen-memory GGTT backing. It quiesces Intel bus mastering before the VT-d table switch and retains that state through installation.
3. Before guest entry, the VMM installs the target-device guest filters on every CPU. CF8-CFF accesses use a per-CPU selector and make the target function appear absent; the target ECAM page is read-only all-ones shadow memory. Guest writes to BAR0/BAR2 are denied through both base and shadow EPT views, and host-bridge configuration writes are trapped. Other PCI configuration accesses retain their existing behavior.
4. The existing VT-d planner and installation/invalidation path retain the legacy shared identity domain for other devices and assign the Intel GPU a sibling root-owned domain. Its ID must fit every participating unit's `CAP.ND` domain capacity. The domain grants read-only access to the diagnostic framebuffer and required GGTT backing, not general guest RAM. After CPU protections and live VT-d root/status readback validate, root enables bus mastering for its programmed plane. Admission then requires the installed exclusion readback, plane configuration, live surface, and every reserved GGTT entry to validate.
5. Root maps at most 8 MiB of framebuffer through the private WC window as read-write and non-executable, with conflicting WB aliases removed. PAT/MTRR and write-protect/NX conditions are checked. The boot path renders the initial diagnostic pattern through `HvOverlayRender`, drains writes, and unmaps the CPU window before guest resume; the pinned GGTT surface remains available for scanout. This increment has no runtime render pump or new control API.

The read-only preflight intentionally reports native source availability separately from live enforcement: `Implemented` is not asserted, boot enforcement is `NotAttested`, and `CanAuthorizeActivation` is false. PnP inventory cannot prove which EFI image is running or read back its per-CPU filters, VT-d domain, and WC mapping. It never creates an exclusion proof or changes host configuration.

## Ownership loss, shutdown, and recovery

Missing or inconsistent guest-exclusion evidence, plane state, live surface, or reserved GGTT entries latches ownership loss for the session. The HAL stops rendering, drains and unmaps an active CPU mapping, returns `HV_GPU_NOT_SUPPORTED`, retains pinned pages and GGTT state, and does not re-arm. Runtime hand-back to a live WDDM plane is not implemented; a safe inverse hand-back remains future work.

An error during boot after physical VT-d isolation becomes active fail-stops
instead of returning to firmware with a restricted domain and partially armed
display. Earlier failures may restore the saved PCI command before releasing
unreferenced allocations. During the guest session, protected MMIO/config
writes and MTRR writes are refused; host-bridge configuration writes are also
blocked. This strict profile has not been compatibility-tested with Windows
or the host's NVIDIA driver.

Reboot is the teardown boundary. Use a full platform reset that quiesces the GPU and IOMMU before reclaiming pinned pages; use a full power cycle if a warm reset cannot provide that guarantee. Do not treat firmware/SMM activity as covered by the guest ownership latch.

## Validation boundary

Headless model tests can exercise admission with valid and absent exclusion evidence, plane/GGTT checks, guest-filter policy, DMA-plan permissions, WC mapping bounds and attributes, and the ownership-loss latch. The disposable nested-VM test can validate the EFI payload's VMX-root paths and synthetic enforcement model. It cannot prove real UHD 770 scanout, the host's Intel-excluded/NVIDIA-primary topology, or hardware Intel DMA isolation. Report those as host-only validation; do not infer them from VMware's virtual display device.

All VMX and end-to-end boot validation runs only in a disposable nested-virtualization VM with snapshots and an offline or controlled network. Do not run the payload on the primary host or change host configuration for this test.

## References

- [PCI configuration access](https://learn.microsoft.com/en-us/windows-hardware/drivers/pci/accessing-pci-device-configuration-space)
- [Intel VT-d specification](https://cdrdv2-public.intel.com/831418/vt-directed-io-spec.pdf)
- Implementation: `components/hypervisor/src/efi/gpu_hal.h`, `gpu_boot.c`, `gpu_guest_filter.c`, `gpu_native.c`, `gpu_wc.c`, `iommu_isolation.cpp`, and `iommu_runtime.c`.
- Read-only inventory: `components/hypervisor/setup/Get-GpuDiagnosticPreflight.ps1`.
