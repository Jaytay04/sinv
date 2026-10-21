# Intel UHD 770 bare-metal diagnostic runbook

## Current checkpoint: run 160027 requires recovery completion

Run `gpu-20261021T160027Z-672213bf26314f69990e6d2305c55fff` collected a health
failure. Its video identifies the first failure as firmware `OpenVolume`
returning `EFI_TIMEOUT` after payload startup reported success on all 20 CPUs.
Later volume opens returned `EFI_DEVICE_ERROR`. Rollback has succeeded with
`RECOVERY_PENDING_USER_POWER_CYCLE`; complete the manual power cycle and run
`FinalVerify` before archiving or preparing another run. Older fixed review
and retirement profiles below do not apply to this run.

The investigation found an unchecked PCI bus-master restoration path and
reproduced its false-success behavior with a host fixture. The update verifies
command-register readback and adds an optional, bounded Block I/O discriminator.
It is not yet a demonstrated fix for this host. See the
[current evidence, diagnostic scope and validation](GPU_PAYLOAD_FAILURE_INVESTIGATION.md#run-160027-volume-open-fails-after-successful-payload-return).
The `062106` cleanup commands below are historical; do not use them for the
retained `160027` stage. A fresh run must wait for this run's completed recovery
and ownership-reviewed stage retirement.

## Previous checkpoint: run 062106

This procedure runs the physical-host GPU diagnostic and records where the
payload-to-Windows handoff fails. It is not a VM test. Run
`gpu-20261021T062106Z-d7c2add0ddc44d0fb3b06badc7018942` completed
`FinalVerify` with
`FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED`. Its phase is
complete and there is no action in progress. The payload returned status zero
after preparing and reporting 20 of 20 processors ready. A successful return
restores the firmware caller's guest continuation; it does not turn VMX off.
The BSP and APs continue in VMX non-root with EPT and IOMMU active while the
loader attempts the Windows handoff. Source review found no demonstrated CPU,
AP, interrupt, or device-mapping defect that explains this failure.

The video recorded a diagnostic-file write timing out with `EFI_TIMEOUT`
(`0x12`); Flush and readback were not reached. The later `bootmgfw` file-read
path returned `EFI_DEVICE_ERROR` (`0x7`) before `LoadImage` or `StartImage`.
The existing high-level trace separates file read, device-path construction,
`LoadImage`, `StartImage`, and return, but the file helper combines several EFI
filesystem calls and normalizes some read errors. The exact failing filesystem
operation and any storage or DMA cause therefore remain unproven. The Windows
health probe found the expected custom signature absent on all 20 processors;
that result does not establish that all VMX activity was absent or which path
started Windows.

The PCR replay gate is independent of the observed handoff failure. This build
enabled `IB_PAYLOAD_REPLAY_INDEPENDENT=ON`, and the video shows payload launch
passing after replay was aborted. The physical activation remains
`GPU_UNATTESTED`.

VT-d remains a hypothesis, not an identified cause. The source identity-maps
ordinary EFI RAM and applicable RMRRs, but the run did not capture post-failure
VT-d fault records or PCI Command readback after restoring bus mastering.
The Windows System-log query for 06:00-07:20 UTC on October 21 found no
storage, WHEA, audio, or device warnings/errors among its matching providers.
Windows logs do not establish pre-OS device health or explain the EFI failure.

## Historical cleanup: archive and retire run 062106

Use elevated 64-bit Windows PowerShell from the main workspace. Archive the
completed run before starting another run. Then capture fresh readiness and,
if it reports only the retained stage as a blocker, review and retire that
stage with its run-specific profile. Stop on any other blocker, failed status,
or error. Preserve all evidence; do not manually delete EFI files or retry a
failed `-Apply` action.

Archive has been confirmed for run 062106. If you already received
`SELECTOR_ARCHIVED` for this run, skip the Archive command below and start
with fresh readiness.

```powershell
Set-Location 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
```

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Archive
```

Require `SELECTOR_ARCHIVED` and `JournalPreserved: true`. The retirement
profile checks the archived selector against the exact 533-byte copy with
SHA-256 `4510690FAD83991612344C958E26C334F19850F71D54B6D51669F7EF94AAB484`.

Capture and display a new readiness result:

```powershell
$readiness = powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Get-GpuBaremetalReadiness.ps1 | Out-String | ConvertFrom-Json
```

```powershell
$readiness | Select-Object Status, Blockers, EvidencePath | ConvertTo-Json -Depth 8
```

If readiness is `READY_FOR_FRESH_PREPARE`, skip retirement and continue to
step 2. If it is `BLOCKED_READINESS` only because the 062106 stage is retained,
review it using the exact evidence path captured above:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Retire-GpuCompletedStage.ps1 -CompletedRun062106 -ReadinessPath $readiness.EvidencePath
```

Require `REVIEWED_STAGE_READY_FOR_RETIREMENT`, `Succeeded: true`, no errors,
and successful cleanup with the ESP unmounted. Only then apply the same
run-specific profile:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Retire-GpuCompletedStage.ps1 -CompletedRun062106 -ReadinessPath $readiness.EvidencePath -Apply
```

Require `STAGE_RETIRED`, `Succeeded: true`, no errors, and successful cleanup
with the ESP unmounted. Capture readiness again and require
`READY_FOR_FRESH_PREPARE` with no blockers:

```powershell
$readiness = powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Get-GpuBaremetalReadiness.ps1 | Out-String | ConvertFrom-Json
```

```powershell
$readiness | Select-Object Status, Blockers, EvidencePath | ConvertTo-Json -Depth 8
```

If review or apply fails, or readiness shows any additional blocker, stop and
preserve the evidence. Do not retry `-Apply` or generically delete EFI files.
The older `042142` retirement profile does not apply to run 062106.

## Before starting

Use elevated 64-bit Windows PowerShell. Run each command separately and wait
for its result. Start from the main workspace:

```powershell
Set-Location 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
```

Confirm the diagnostic worktree is clean, on the expected branch, and includes
the diagnostic changes:

```powershell
git -C .\.codex-build\worktrees\gpu-windows-validation status --short --branch
```

```powershell
git -C .\.codex-build\worktrees\gpu-windows-validation merge-base --is-ancestor 15c79ba HEAD
```

Require branch `feat/smbios-boot-serial-probe`, no local changes, and exit code
zero from the ancestry check. Do not update source between Prepare and Arm.

Have the tested full-system backup and recovery media ready, connect the
recovery disk, and keep the BitLocker recovery key available offline. The
recovery directory is `E:\CodexBackup\Recovery\hsinv-gpu-test`, on a separate
physical disk from Windows. Keep Secure Boot and the existing shim/MOK trust
setup unchanged. Confirm VMX and VT-d are enabled in firmware. Keep USB devices
in the same configuration through readiness and recovery checks; record any
device insertion or removal. Check audio and both displays before the run.

Keep the main display on NVIDIA and connect the diagnostic display to the
motherboard's Intel HDMI or DisplayPort output. Monitor inventory does not
prove Intel GOP routing. The native path requires a supported Intel GOP mode,
one active pipe, and a framebuffer no larger than 8 MiB. Save open work and use
a controlled network. Do not unbind the Intel Windows driver.

## Before starting another run

Optional preview only:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Get-GpuBaremetalReadiness.ps1 -WhatIf
```

Run readiness and keep its exact evidence path in this PowerShell session:

```powershell
$readiness = powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Get-GpuBaremetalReadiness.ps1 | Out-String | ConvertFrom-Json
```

```powershell
$readiness | Select-Object Status, Blockers, EvidencePath | ConvertTo-Json -Depth 8
```

The complete archive and retirement procedure for the current completed run is
above. The earlier 042142 retirement procedure is historical and must not be
reused for 062106. Before Prepare, require fresh readiness status
`READY_FOR_FRESH_PREPARE` with no blockers.

## 2. Prepare, build, sign, back up, and stage

Prepare only after readiness and physical-output/recovery prerequisites pass:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Prepare -PhysicalOutputConfirmed -RecoveryReady
```

Require `PREPARED_READONLY_GATE_PASS`. Save the run and journal paths. The
switches record operator confirmations; they do not prove GPU ownership or
isolation.

Build and run the host tests:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Build
```

Require `BUILD_AND_TESTS_PASS` and passing unsigned image audit. Then sign and
audit:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Sign
```

Require `SIGNATURE_AND_AUDIT_PASS`. Then back up the ESP:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Backup
```

Require `ESP_BACKUP_VERIFIED` and verified local and external manifests. This
captures the ESP, a logical system BCD export, firmware enumeration, and TPM
reference logs; it does not replace the full-system backup. Do not retry a
failed Backup.

Stage the signed files:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Stage
```

Require `STAGE_READBACK_PASS` and verify its five file hashes. This writes the
run's files to the ESP; it does not yet arm the boot.

## 3. Arm and boot once

Recheck the recovery disk, MOK prerequisites, and all receipts. Arm:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Arm
```

Require `ARMED_MANUAL_REBOOT_REQUIRED`. The script does not reboot. Do not arm
again or alter BootOrder. Record the console externally if possible. A
verified legacy COM1 connection can capture hypervisor serial output; a USB
serial adapter is not the legacy I/O port. Photograph every shim, gate,
payload, or Windows handoff message. Then restart manually:

```powershell
shutdown.exe /r /t 0
```

Watch both displays. Do not weaken admission or clear PCRs. If the replay gate
reports non-empty PCR banks, preserve the exact message: this is an independent
loader gate and does not establish why a later payload handoff failed.

## 4. Collect and preserve the diagnostics

After Windows returns, collect once:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Collect
```

The expected health-only success status is
`COLLECTED_HEALTH_ONLY_GPU_UNATTESTED`. A failure status still produces
important evidence. Preserve the complete output and the action directory's
`esp-payload-diagnostic.txt`, `esp-handoff-status.txt` when present, health captures,
and console photographs. Keep the raw EFI statuses and shim phase fields.
The existing diagnostic distinguishes file read, device-path construction,
`LoadImage`, and `StartImage`, and records each high-level returned status.
Those phases alone do not identify which filesystem protocol call failed
inside the file read or write helper. The updated loader adds an operation-tagged EFI
file trace with raw and effective status plus the input/output size fields.
`size_out` records the field returned by firmware; it can be unchanged on an
error and does not independently prove how many bytes were transferred.
Retaining the field also preserves partial-write evidence permitted by the
[UEFI File Protocol](https://uefi.org/specs/UEFI/2.10/13_Protocols_Media_Access.html#efi-file-protocol-write).
For reads it keeps the raw `GetInfo` or `Read` error even when the
existing helper returns normalized `EFI_DEVICE_ERROR`; verified writes report
the first failing replacement or readback operation. Failure summaries go to
the console, including the boot-manager read summary before the next
handoff-status write. This change adds no VT-d fault or VM-exit capture and
does not change payload activation or fix the observed failure. Static build
and host-test results for this update are recorded below; the physical
failure has not been retested.
A `_begin_status` record without a matching `_end_status` record means the
call's return was not observed; it does not identify why the operation did not
return. A readback proves only that the file could be read at that check, not
that it persisted through a later power loss.

Do not repeat Arm or reboot to seek a pass. If collection fails, preserve its
evidence and continue to recovery.

## 5. Roll back, power-cycle, verify, and archive

Run rollback once:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-rollback.ps1
```

If it reports `RECOVERY_PENDING_USER_POWER_CYCLE`, save work, shut down fully,
and power the computer on again:

```powershell
shutdown.exe /s /t 0
```

Sleep and hibernation do not meet the reset requirement. After Windows returns,
verify recovery:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 FinalVerify
```

Archive only after `FinalVerify` succeeds with phase `Complete`:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Archive
```

Require `SELECTOR_ARCHIVED` and `JournalPreserved: true`. If FinalVerify fails
only because the firmware text differs, preserve its evidence and stop.
Firmware inspection and reviewed-difference profiles are pinned to a specific
completed run; use only a profile and fresh review capture for that exact run.
Neither the `042142` nor `062106` profile applies to a later run.
Recovery verification and `GPU_UNATTESTED` do not prove GPU isolation or
successful native scanout.

If any action fails or is interrupted, preserve the selector and journal and
inspect Status before another action:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Status
```

Do not retry the interrupted forward action or run rollback a second time.
Follow its saved recovery state; do not edit evidence or firmware baselines to
make checks pass. If Windows cannot boot, use the prepared recovery media and
full-system backup.

## Validation of the file-operation diagnostics

The updated loader and EFI fixtures compiled successfully, and all 31 loader
host tests passed. Both hypervisor static and boot-diagnostic contract checks
also passed. The tests cover raw-versus-normalized EFI errors, short reads and
writes, partial-error size fields, expected sizing/probe results, cleanup
precedence, warning normalization, and image-launch trace propagation.

The change adds no disk calls, retries, payload activation changes, or
hypervisor ABI changes. Its additional failure output uses the existing
console path. No VM, EFI payload, driver, or live boot test was run.

## Validation of the 062106 cleanup

The focused retirement fixtures passed 10 cases and 56 assertions. A
process-local prearchive test checked all 45 saved proof pins using the live
selector's identical expected archive bytes. It did not create an archive or
change the selector, journal, or result. The unchanged production proof
rejected the missing archive. After the operator archived the run, the
unmodified proof also matched all 45 pins against the actual archive, with
the selector absent and journal, archive, and result unchanged. Retirement
still requires fresh readiness evidence and its live review gates.

The static build and all 52 hypervisor host tests passed. All 27 PowerShell
blocks in this guide parsed successfully. Validation did not start a VM,
mount the ESP, remove staged files, or change firmware or boot selection.

## Historical validation of the 042142 retirement procedure

The 042142 retirement proof matched all 47 saved evidence pins without changing
the archived journal, selector, or result. The static build passed, and all 48
hypervisor host tests passed after updating the dispatcher fixture's allowlist
for the local 042142 entry-journal helper. This includes the new five-file
retirement fixture. All 23
PowerShell command blocks parsed successfully. This validation did not start a
VM, mount the ESP, retire files, or change firmware or boot selection.
