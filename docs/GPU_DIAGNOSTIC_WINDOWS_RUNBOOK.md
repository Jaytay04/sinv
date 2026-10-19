# Intel Diagnostic GPU Windows Validation Runbook

This runbook separates the disposable Windows VM check from physical Intel scanout. The VM can validate Windows boot and the EFI model/root-WC path. VMware SVGA cannot validate UHD 770 PCI/VT-d enforcement, host GPU topology, or pixels on a physical Intel output.

## Current validation target

The host is Windows 11 Home 25H2, build 26200, UBR 9711. The Windows guest's authenticated inspection confirmed 25H2, build 26200, UBR 9457, four CPUs, Secure Boot off, and BitLocker fully decrypted. This matches the host's release and build family, but not its exact patch level. Its display device is VMware SVGA 3D (`15AD:0405`). The inspection recorded the Windows boot-manager hash for an unchanged-after-test comparison. The standalone VM is at `.codex-build/gpu_windows_vm/run-01/standalone/hsinv-Windows-25H2.vmx`. Its converted 80 GiB base disk is independent of the original VMs; all later snapshot disks and backing files remain within the standalone folder. The copied VMX, NVRAM, and vTPM state passed readback checks. This is the repository's default Windows validation VM; keep it at this path and preserve its entire folder instead of cleaning it as disposable build output.

The powered-off default snapshot is `hsinv-default-25h2-26200.9457`. Local metadata is saved in `.analysis/host-runner/default-windows-vm.json`, and `KEEP-VM.txt` in the standalone folder marks the retention requirement. The two original ExactBuild Gate VMs are no longer dependencies. Serial capture is disabled outside controlled tests to preserve the completed evidence. The example Host Runner configuration points to this VM and snapshot; its profile remains disabled until machine-local credentials and lease integration are configured.

Use this standalone copy with its own disk and firmware state. It has EFI firmware, virtual hardware 22, nested virtualization, four vCPUs, 8 GiB RAM, no virtual NIC, and no physical device passthrough. Take powered-off before and after snapshots. Keep network access disabled or controlled.

Configure the VM test image with the hypervisor self-test enabled and native hardware activation disabled. The loader fixture must set `IB_PAYLOAD_REPLAY_INDEPENDENT=ON`; it exists only for this VM test. From an x64 Visual Studio developer PowerShell at the workspace root, the two component builds can be configured separately:

```powershell
cmake -S .\components\hypervisor -B .\.codex-build\gpu-win-hv -G Ninja `
  -DHYPERSINV_BUILD_UEFI=ON -DHYPERSINV_BUILD_CORE=OFF `
  -DHYPERSINV_BUILD_DRIVER=OFF -DHYPERSINV_BUILD_LOADER=OFF `
  -DHYPERSINV_GPU_VM_SELFTEST=ON `
  -DHYPERSINV_GPU_DIAGNOSTIC_NATIVE=OFF `
  -DHYPERSINV_GPU_DIAGNOSTIC_OUTPUT_CONFIRMED=OFF
cmake --build .\.codex-build\gpu-win-hv
ctest --test-dir .\.codex-build\gpu-win-hv --output-on-failure

cmake -S .\components\uefi-loader -B .\.codex-build\gpu-win-loader -G Ninja `
  -DIB_STATIC_RUNTIME=ON -DIB_PAYLOAD_REPLAY_INDEPENDENT=ON `
  -DIB_QEMU_DEBUG_PORTS=ON
cmake --build .\.codex-build\gpu-win-loader
ctest --test-dir .\.codex-build\gpu-win-loader `
  -R '^(install_bootnext_test|install_oneshot_test|hypervisor_health_test)$' `
  --output-on-failure
```

Run the Windows controller with a fresh, nonexistent run directory and the standalone VMX under this workspace's `.codex-build`. `CredentialStateRoot` must point to an existing private Host Runner state whose `credentials` directory contains `windows-vm.xml` and `windows-clone-mailbox-key.xml`; the files are DPAPI protected and must not be copied into run evidence or source control.

```powershell
$WorkspaceRoot = (Resolve-Path .).Path
$VmRoot = Join-Path $WorkspaceRoot '.codex-build\gpu_windows_vm'
$VmxPath = Join-Path $VmRoot 'run-01\standalone\hsinv-Windows-25H2.vmx'
$RunDirectory = Join-Path $VmRoot ('run-' + [guid]::NewGuid().ToString('N'))
$CredentialStateRoot = '<existing private Host Runner state root>'
if (Test-Path -LiteralPath $RunDirectory) { throw 'Choose a fresh run directory.' }

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\components\hypervisor\setup\Test-GpuHalWindowsVm.ps1 `
  -WorkspaceRoot $WorkspaceRoot -VmxPath $VmxPath -RunDirectory $RunDirectory `
  -EfiPath (Join-Path $WorkspaceRoot '.codex-build\gpu-win-hv\src\efi\uefi_hypervisor.efi') `
  -LoaderPath (Join-Path $WorkspaceRoot '.codex-build\gpu-win-loader\loader.efi') `
  -InstallerPath (Join-Path $WorkspaceRoot '.codex-build\gpu-win-loader\ib-install.exe') `
  -ProbePath (Join-Path $WorkspaceRoot '.codex-build\gpu-win-loader\ib-tcg-dump.exe') `
  -CredentialStateRoot $CredentialStateRoot `
  -ExpectedBuild 26200 -ExpectedUbr 9457 -ExpectedCpus 4 -TimeoutSeconds 360
```

The controller requires the disposable VMX, EFI, loader, installer, probe, and fresh run directory under `WorkspaceRoot\.codex-build`. It takes before/after snapshots, stops and restores the target clone, captures serial and VMware evidence, and returns a structured result. Require Windows startup health to report exactly four per-CPU passes and the aggregate pass, and require the boot-manager hash to remain unchanged. The added `ib-tcg-dump` health command checks the Windows probe without waiting for an unrelated TPM empty-bank condition.

Earlier attempts exposed two independent failures: Run 13's test-loader path did not pass the loader allowlist, and an intermittent GuestInfo inspection/replay rejection occurred in another run. The GuestInfo rejection remains unexplained, so earlier failed runs remain failures. A diagnostic-only request cannot turn a failed health result into a pass.

The VM-only `ib-install oneshot --mode bootnext --profile vm-direct --esp-volume auto` profile is implemented. It uses the legacy manifest preflight, targets the already-allowed `EFI\insecure-boot\grubx64.efi` path, and supports only `bootnext` and `off`; the worker still stages identical loader copies as `shimx64.efi` and `grubx64.efi` for manifest validation. Three focused installer/health tests pass. Do not use `vm-direct` on physical systems.

Run 21 passed the full controller with exit code 0. Its EFI serial log contains `GPU_HAL_MODEL:ALL_PASS`, `GPU_ENFORCEMENT_MODEL:ALL_PASS SYNTHETIC_ONLY`, `GPU_HAL_VM:ROOT_WC_NO_BACKEND PASS`, `GPU_HAL_VM:ROOT_WC_BOUNDS PASS`, `GPU_HAL_VM:ROOT_WC_RENDER PASS`, `GPU_HAL_VM:ROOT_WC_UNMAPPED PASS`, and `GPU_HAL_VM:VMX_FIRMWARE_RETURN PASS`. It also records payload launch success and VMX launch success for the bootstrap processor and three APs. The HMAC-verified Windows result reports `Status=PASS`, build 26200 UBR 9457, exactly four CPU health passes plus aggregate pass, and unchanged Windows boot-manager hash. It identifies VMware SVGA 3D (`15AD:0405`) and no Intel adapter. The signed Windows result JSON SHA-256 is `DC0C7B15F4569D6A87A99FA07505DC2BA2884320FDE81782F5D12FA61C53ACC3`; this is not the EFI binary hash. The final report is `.codex-build/gpu_windows_vm/run-21/result.json`; frozen serial copies are `efi-serial.saved.log` and `windows-session.saved.log` in the same run directory. The local `.analysis/gpu-windows-validation-report.json` receipt records source and artifact hashes, tests, VM configuration, and evidence paths.

Run 19's authenticated Windows startup evidence also passed, but its overall controller result failed while hashing a VMware-held serial log. The controller now opens serial files with shared-read access and includes a regression for that condition. That fix was present in Run 21. The independent GuestInfo restart/replay issue seen in an earlier attempt was not root-caused.

The VM-only EFI image uses `IB_PAYLOAD_REPLAY_INDEPENDENT=ON`; the Windows test worker copies the same test loader to `EFI\insecure-boot\shimx64.efi` and `grubx64.efi`. This fixture exists only to run the disposable test. It is not a production boot chain and must never be copied to a physical machine. Physical deployment requires an approved signed EFI chain and its normal platform security policy.

## Physical lab gate

The physical path remains unvalidated. Repository policy currently reserves all experimental VMX execution and end-to-end boot tests for disposable VMs, so this document does not authorize a bare-metal payload run or changes to the user's host. The steps below are for a future controlled lab run after the project establishes a production EFI chain and updates its execution policy.

### Before a lab run

1. Take a full system backup and confirm the machine-specific firmware recovery path before changing boot state. The project does not currently document board-specific recovery.
2. From the repository root, save the read-only PnP inventory with `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\components\hypervisor\setup\Get-GpuDiagnosticPreflight.ps1 -OutputPath .\.codex-build\gpu-preflight.json`. On the inventoried host, it found an Intel UHD 770 (`8086:4680`) and an NVIDIA RTX 3080 (`10DE:2206`); both enumerated monitors were associated with NVIDIA, and Intel's `AssociatedMonitors` list was empty. This does not establish that the Intel HDMI/DP connector is accessible. Physically attach and verify the diagnostic display, then keep Windows on NVIDIA.
3. From an x64 Visual Studio developer PowerShell at the repository root, configure a separate build with `HYPERSINV_GPU_DIAGNOSTIC_NATIVE=ON` and `HYPERSINV_GPU_DIAGNOSTIC_OUTPUT_CONFIRMED=ON`; set the latter only after physical output verification. For example:

   ```powershell
   cmake -S .\components\hypervisor -B .\.codex-build\gpu-native -G Ninja `
     -DHYPERSINV_BUILD_UEFI=ON -DHYPERSINV_BUILD_CORE=OFF `
     -DHYPERSINV_BUILD_DRIVER=OFF -DHYPERSINV_BUILD_LOADER=OFF `
     -DHYPERSINV_GPU_DIAGNOSTIC_NATIVE=ON `
     -DHYPERSINV_GPU_DIAGNOSTIC_OUTPUT_CONFIRMED=ON
   cmake --build .\.codex-build\gpu-native
   ctest --test-dir .\.codex-build\gpu-native --output-on-failure
   ```

   The component defaults `HYPERSINV_UEFI_SOURCE_DIR` to the sibling `components/uefi-loader` and its support directory to `src/vendor`. Native activation options default off and cannot be combined with `HYPERSINV_GPU_VM_SELFTEST`. Require a successful EFI build, `.hvscan`, and CTest run. Save the build log, `.hvscan` report, EFI hash, and source revision.
4. Stop at EFI staging until a production EFI chain is available. The existing `ib-install stage` writes `EFI\insecure-boot`; `oneshot --mode bootnext` targets that directory. The UEFILoader README explicitly scopes this research toolkit and its test firmware to VMs, and the Windows VM fixture uses an unsigned replay-independent loader with Secure Boot disabled. Those files and commands are not a bare-metal deployment path. Do not use them on a physical system.

### EFI staging, one-shot boot, and observation

These actions require a production staging path that the repository does not currently provide. Once that exists, use its signed, policy-approved EFI chain and a recovery-tested lab machine; stage only to that machine's system EFI partition, arm only its owned one-shot entry, and verify BootNext readback before rebooting. Capture the full EFI serial output and confirm installed per-CPU PCI/EPT filters, the Intel VT-d domain, bounded root WC render, validated linear plane/live surface and reserved GGTT entries, write drain, and CPU-map removal before guest resume. Confirm the pattern appears on the Intel-attached display and Windows continues on NVIDIA. Preserve hashes, readbacks, logs, and a screen photograph. VMware cannot supply this evidence.

Firmware and SMM are trusted prerequisites. If they write the plane or DMA the surface, root ownership can be silently violated; the ownership latch does not guarantee detection of these trusted writers. If plane, surface, or reserved GGTT validation fails, the HAL must stop, drain and unmap the CPU window, return `HV_GPU_NOT_SUPPORTED`, retain pinned scanout pages, and remain latched until reboot. There is no re-arm or runtime hand-back to Windows; runtime hand-back is future work.

## Recovery

Reboot is the ownership teardown boundary. Use a full platform reset only when it is known to quiesce the GPU and IOMMU; otherwise power-cycle the lab machine before reclaiming pinned pages. Do not attempt a mid-session hand-back to WDDM. If the experimental image cannot boot, use the machine's previously verified firmware recovery path to select Windows Boot Manager. This repository has no verified board-specific recovery instructions.

## Validation status

The full hypervisor suite passed 30/30 CTests in 10.99 seconds; the controller has 45 pure regression checks, and the focused UEFI-loader installer/health suite passed 3/3 in 0.84 seconds. The VM self-test EFI image passed `.hvscan` at 56,822 bytes with SHA-256 `B077512D734D3750A3BC7E9419E0F3C5358017B1D6E46B23F649DB18E83C5084`. The separate native build with `HYPERSINV_GPU_DIAGNOSTIC_NATIVE=ON` and physical-output confirmation off passed `.hvscan` at 54,870 bytes. Its EFI image SHA-256 is `25FBBC5B1BBD92DB76A7BAA47C7FABD2073145AF79B96B86328F3D2A35960018`, also recorded in the evidence manifest. Run 21 passed all four controller phases: Inspect, Stage, EFI/root-WC, and Windows startup. The controller confirmed the VM stopped, created `after-gpu-hal-87785d6c`, reverted to `before-gpu-hal-407f55a0`, and reported no cleanup failures. The soft `vmrun stop` timed out, so the bounded hard stop was used; graceful guest shutdown is not claimed. The VM remains the repository default at its existing path, and the entire folder must be preserved. No screenshot was available because VMware guest operations denied capture. VMware SVGA is not an Intel vGPU, so real UHD 770/PCH scanout, Intel DMA isolation, physical display pixels, and the Intel-excluded/NVIDIA-primary host topology remain unvalidated.

See [GPU_DIAGNOSTIC_ACTIVATION.md](GPU_DIAGNOSTIC_ACTIVATION.md) for the activation contract and [GPU_DIAGNOSTIC_VM_VALIDATION.md](GPU_DIAGNOSTIC_VM_VALIDATION.md) for the earlier EFI-only VM evidence. The older `BUILD_BOOT_TEST.md` is explicitly unvalidated and is not a procedure for this test.
