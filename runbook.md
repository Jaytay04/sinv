# Intel UHD 770 diagnostic scanout: bare-metal test runbook

This runbook follows the script-per-action format in [`COMBINED_HOST_RUNBOOK.md`](spoof-handoff/docs/COMBINED_HOST_RUNBOOK.md). It uses a separate one-time, reboot-bounded GPU workflow. The physical test has **not** been run. The Windows VM passed its EFI boot, actual root WC render/drain/unmap, and synthetic GPU model checks, but it had no Intel GPU; it does not prove host scanout, GPU DMA isolation, or the Intel-excluded/NVIDIA-primary topology.

The VM is the default validation machine. Keep the full directory containing `C:\Users\jayli\OneDrive\Documents\Projects\hsinv\.codex-build\gpu_windows_vm\run-01\standalone\hsinv-Windows-25H2.vmx`, including its disks, snapshots, NVRAM, and vTPM, at its current location.

The dispatcher is `tools/gpu_diagnostic/gpu-host-test.ps1`; the separate recovery entry point is `tools/gpu_diagnostic/gpu-host-rollback.ps1`. Run commands from the **main repository root** in a new elevated x64 Windows PowerShell session for each action. Every command below invokes the feature-worktree script directly; no PowerShell variables need to survive between actions or reboots. The scripts require a clean, reviewed `feat/smbios-boot-serial-probe` worktree and pin its commit during Prepare. Preserve unrelated work and do not edit the main checkout.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Status
```

Status reads the saved run state; it does not attest live BootNext or GPU ownership. If it reports an existing or interrupted Prepare, do not start another run. Use only the exact run and receipts reported by the tool. Lifecycle actions save their results or failures under `.analysis\gpu-baremetal\runs\<run-id>\actions`; native child commands also save stdout, stderr, and exit codes. Prepare writes `.analysis\gpu-baremetal\host-prepared.json`. The script prints the exact run and receipt paths. Let an action finish before starting another; an in-progress journal blocks forward actions.

## Prerequisites

Before Prepare, have all of the following ready:

- A tested full-system backup on another physical disk, Windows recovery media, and the BitLocker/device-encryption recovery key available offline. Do not place recovery keys in transcripts or the repository. Prepare records protection status but does not suspend or decrypt it.
- A verified firmware boot-menu route to the original Windows Boot Manager.
- Secure Boot enabled, the project public MOK already enrolled, and firmware acceptance of the vendored Microsoft-signed shim. The prior signed collector only proved its own chain. Do not clear Secure Boot keys, alter revocations, or disable shim validation.
- A diagnostic display physically connected to motherboard Intel HDMI/DP. Keep the Windows desktop and firmware console on NVIDIA. The Intel adapter must expose a supported, active GOP mode, exactly one enabled pipe, and the required plane/GGTT state. The HAL does not enable a connector or set a display mode. Firmware/GOP and connector state must be confirmed by the operator; the Prepare switches below are declarations, not proof.
- Intel VMX and VT-d enabled. Do not disable Hyper-V, VBS, driver signing, or other security features to force admission. Compatibility with this host configuration is unverified.
- The recovery drive mounted as `E:` with a dedicated directory at `E:\CodexBackup\Recovery\hsinv-gpu-test`, on a physical disk separate from Windows. Use the default path unless the operator has prepared a different external recovery disk.
- A disconnected or controlled test network; saved work; and a planned full shutdown/power cycle after the test. A failure after isolation or plane arming may halt the machine and require external recovery.

Firmware/SMM are trusted. If firmware or SMM changes the plane or DMA-writes the surface, root ownership can be violated without a guaranteed latch detecting it. This assumption does not replace guest PCI/MMIO filtering or the separate GPU DMA domain.

Admission requires the expected Intel PCI function (`8086:4680`, segment 0, `00:02.0`), NVIDIA firmware console, supported linear Intel GOP format, one active pipe, unused secondary planes/cursor, valid locked stolen-memory GGTT backing and reserved entries, a linear surface bounded to 8 MiB, and the installed guest filters, VT-d domain, and WC mapping. Unsupported state must stop the run. A 1920x1080 32-bit surface fits the size limit; many larger modes do not.

## Prepare and build

### 1. Optional no-op preview

`-WhatIf` skips host probes and all writes. It is only a command-line preview and does not validate prerequisites.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Prepare -WhatIf
```

### 2. Capture the read-only host gate

Only after manually checking the prerequisites, declare the physical output and recovery media ready. These switches are operator assertions; they do not verify a connector, GOP mode, or MOK state. Prepare checks Secure Boot, the Intel/NVIDIA devices, OS and machine identity, CPU count, firmware enumeration, and BitLocker status. It creates the run record and recovery evidence; it does not mount or write the ESP, build or sign binaries, stop drivers, or arm boot state.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Prepare -PhysicalOutputConfirmed -RecoveryReady
```

Proceed only on a successful Prepare result. If it fails or is interrupted, preserve the selector, journal, and printed action receipts. The selector intentionally blocks another Prepare, even after an incomplete attempt. Do not delete it, select the newest run directory, or retry; inspect Status and use recovery against that exact run.

### 3. Build and run checks

Run from elevated **x64 Developer PowerShell for Visual Studio** so the MSVC tools are available. Build produces the native hypervisor and loader candidates, runs component CTests and the unsigned hypervisor `.hvscan` audit, and records hashes and logs. Require all tests and the audit to pass before proceeding.

Build outputs use a run-specific short root under `.codex-build\gb\<16-hex-id>` to avoid the Windows object-path failure documented in the older combined workflow. Journals and command evidence stay under `.analysis\gpu-baremetal\runs`. Neither root is selected by "newest directory" or shared with the retained VM.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Build
```

The loader candidate enables `IB_PAYLOAD_REPLAY_INDEPENDENT` with VM debug ports disabled. This preserves the resident-image launch contract while permitting the signed payload after **any** replay failure or unavailability, including the expected normal nonempty-PCR refusal. It does not suppress successful replay. The later Windows TPM probe is a preparation check, not an EFI-time launch gate. Do not clear the TPM, fabricate a reference log, disable measurements, or reuse unsigned VM images for this test.

### 4. Sign the pinned candidates

Use the existing current-user signing key. The action does not export a key or create trust. Stop if the pinned signer, private key, or firmware acceptance prerequisites fail.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Sign
```

Sign also audits the signed hypervisor image and verifies its pinned Authenticode signer. The signed native candidate has not been proven on this machine. The build/test action's success is not a GPU hardware admission result.

## Back up, stage, and arm

### 5. Capture ESP and recovery evidence

The script selects and validates the current GPT system ESP, captures the original EFI tree and boot-manager hash, records a non-empty TPM/replay log readback, and writes the run-specific backup to the configured external recovery disk. Review the receipt and recovery copy before continuing. It refuses existing stage files or ambiguous/recovery residue; preserve and investigate those files rather than deleting them.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Backup
```

### 6. Stage the signed one-shot payload

Stage writes the fresh verified EFI files to the owned `EFI\insecure-boot` directory on the recorded ESP and verifies readback. It must leave Windows Boot Manager unchanged. It does not set BootNext.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Stage
```

### 7. Arm BootNext

Recheck the external recovery copy, selected Intel display, signed artifact hashes, Secure Boot/MOK prerequisites, and Build/Stage receipts. Arm revalidates the pinned artifacts, ESP, and recovery evidence, then sets the owned one-shot entry. It returns `ARMED_MANUAL_REBOOT_REQUIRED`; it never reboots automatically. Do not proceed unless that exact run's Arm result succeeds.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Arm
```

### 8. Perform one manual boot

Start an external serial capture only if the motherboard's legacy COM1 interface is verified; a USB serial dongle does not provide the legacy I/O port. Save work and reboot manually. Keep Secure Boot enabled and select the one-shot entry. Do not use an unattended reboot loop, change BootOrder, or deliberately trigger device reset/sleep or guest reprogramming.

The expected visible result is a one-time red `ROOT GPU` diagnostic pattern on the Intel-connected display while Windows continues on NVIDIA. Photograph the screen if it appears. A black display, halt, signature rejection, or missing pattern is not a pass; preserve the serial/console output and use recovery. The pattern is a physical observation, not full attestation of DMA isolation or every plane/GGTT permission.

### 9. Collect the Windows result

After the one-shot boot returns to Windows, open a new elevated x64 PowerShell at the main repository root and run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Collect
```

Review all recorded per-CPU health results against the CPU count captured at Prepare, host inventory/Secure Boot, ESP identity, staged files, and unchanged Windows Boot Manager hash. Healthy collection reports `COLLECTED_HEALTH_ONLY_GPU_UNATTESTED`: it does not assert Intel scanout, live plane/GGTT state, GPU DMA permissions, or the ownership latch. Windows continuing to boot, or Intel disappearing from device enumeration, is not enough to claim GPU admission. Preserve the printed receipts and any Intel-screen photograph.

### 10. Disarm and cross the reset boundary

Clear only this run's owned one-shot boot state:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Disarm
```

Then perform a full Windows shutdown manually and power-cycle the machine. Do not sleep, hibernate, or attempt a live hand-back to WDDM. The adapter and pinned pages are not handed back during the active session. Boot the original Windows Boot Manager normally. No script automatically shuts down or reboots the host.

### 11. Verify the next boot

After normal Windows startup, verify that the test hypervisor health result is absent, Secure Boot remains enabled, both Intel and NVIDIA adapters enumerate as expected, the recorded system ESP is still selected, and the Windows Boot Manager hash is unchanged. The owned EFI stage files remain for evidence and are not automatically removed. Then run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 FinalVerify
```

Only a successful `FinalVerify` records `Complete`. Keep the journal, action receipts, external recovery copy, logs, and photographs. If preparing another test later, archive the selector only after that verified Complete endpoint:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Archive
```

Archive verifies completion, copies and checks the selector under `.analysis\gpu-baremetal\archive`, then removes only the active selector. It preserves the run and every recovery artifact. Never remove the selector manually to clear an error or start a retry.

## Status and recovery

Check the recorded phase without guessing a run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Status
```

If an action fails or is interrupted, stop before another forward action. Preserve its stdout, stderr, exit receipt, selector, and journal. Run rollback, which selects the exact saved run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-rollback.ps1
```

If the selector is unavailable but the exact journal directory is known from the action receipt, provide that exact directory for Status or rollback. Never select a run by timestamp:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Status -RunDirectory 'C:\exact\run\directory\from\receipt'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-rollback.ps1 -RunDirectory 'C:\exact\run\directory\from\receipt'
```

Rollback does not automatically power-cycle the machine. After an Arm intent, it can return `RECOVERY_PENDING_USER_POWER_CYCLE`; follow the recorded recovery result, shut down/power-cycle manually, and run `FinalVerify` only where the action state permits it. Rollback does not remove staged ESP files automatically. Do not delete them recursively; preserve them for exact, reviewed cleanup.

If Windows cannot boot, these Windows scripts cannot run. Use the verified firmware boot menu to choose the original Windows Boot Manager or boot the prepared recovery media and restore the full-system backup. Use the offline BitLocker recovery key if requested. Do not format an ESP, clear Secure Boot keys, or run `bcdboot` against an unidentified partition. Keep the external recovery copy and all run evidence.

## Scope and references

This is a one-time diagnostic test, not same-screen overlay over the NVIDIA desktop. If native admission succeeds, root owns the live Intel adapter for that boot; a runtime hand-back state machine does not exist. Firmware/SMM writes remain a trusted assumption and could silently violate root ownership. Full native evidence requires observing the Intel display pattern; the current Windows collector cannot independently report plane/GGTT ownership or GPU DMA isolation.

- [GPU activation design](docs/GPU_DIAGNOSTIC_ACTIVATION.md)
- [GPU HAL contract](components/hypervisor/src/efi/gpu_hal.h)
- [GPU boot preparation](components/hypervisor/src/efi/gpu_boot.c)
- [VM runbook and evidence scope](docs/GPU_DIAGNOSTIC_WINDOWS_RUNBOOK.md)
- [EFI inventory and signed shim procedure](docs/EFI_INVENTORY_PATH_B.md)
