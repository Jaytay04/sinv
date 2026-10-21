# Intel UHD 770 bare-metal diagnostic runbook

This procedure runs the physical-host GPU diagnostic and records where the
payload-to-Windows handoff fails. It is not a VM test. The last completed run,
`gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2`, passed recovery
verification and is archived. The physical activation still has no GPU
isolation attestation. Its console showed successful AP preparation and payload
return, followed later by `start_image() returned Device Error`; the exact
failing EFI operation remains unknown. The diagnostics in commit `15c79ba`
report the file, device-path, `LoadImage`, `StartImage`, and returned EFI
statuses, and check the handoff status and diagnostic files after writing.
They do not claim data persisted across a later power loss.

The archived run left exactly five files in `EFI\insecure-boot`:
`shimx64.efi`, `grubx64.efi`, `payload.efi`, `tcglog.ib`, and
`manifest.sha256`. Before another stage, retire those files using only the
closed profile for that archived run. The original firmware-text baseline is
preserved. Do not treat a firmware-text change on a future run as reviewed by
the older run's inspection.

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

## 1. Inspect readiness and retire the previous stage

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

If it already reports `READY_FOR_FRESH_PREPARE`, skip the retirement commands
and continue to step 2.

If readiness reports `BLOCKED_READINESS` only because the previous stage is
retained, review it against the completed `042142` run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Retire-GpuCompletedStage.ps1 -CompletedRun042142 -ReadinessPath $readiness.EvidencePath
```

Require `REVIEWED_STAGE_READY_FOR_RETIREMENT`, `Succeeded: true`, no errors,
and successful cleanup with the ESP unmounted. This profile compares the
current mounted-ESP firmware text with the fresh reviewed capture for run
042142. It requires BootNext absent, bounded BootOrder entries, no references
to staged boot entries, and zero integrity findings. The saved raw selection
does not prove historical BootOrder continuity. Preserve all local and
external copies. If any check fails, stop and retain its evidence; do not
delete ESP files manually.

Only after review passes, apply the same closed profile:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Retire-GpuCompletedStage.ps1 -CompletedRun042142 -ReadinessPath $readiness.EvidencePath -Apply
```

Require `STAGE_RETIRED`, `Succeeded: true`, no errors, and cleanup success.
Then inspect readiness again and require `READY_FOR_FRESH_PREPARE`:

```powershell
$readiness = powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Get-GpuBaremetalReadiness.ps1 | Out-String | ConvertFrom-Json
```

```powershell
$readiness | Select-Object Status, Blockers, EvidencePath | ConvertTo-Json -Depth 8
```

If there is any blocker other than that retained stage, stop and resolve it
before Prepare. Never generically delete or retry an interrupted action.

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
The new diagnostic distinguishes file read, device-path construction,
`LoadImage`, and `StartImage`, records returned statuses, and checks the two
diagnostic files after writing. A `_begin_status` record without a matching `_end_status`
record means the call's return was not observed; it does not identify why the
operation did not return. A readback proves only that the file could be read at
that check, not that it persisted through a later power loss.

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
only because the firmware text differs, preserve its evidence and stop. The
`042142` firmware inspection and reviewed-difference flag are pinned to that
completed run; do not reuse them for a new run. Obtain a read-only review
profile pinned to the new run before any reviewed-difference verification.
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

## Validation of this guide

The 042142 retirement proof matched all 47 saved evidence pins without changing
the archived journal, selector, or result. The static build passed, and all 48
hypervisor host tests passed after updating the dispatcher fixture's allowlist
for the local 042142 entry-journal helper. This includes the new five-file
retirement fixture. All 23
PowerShell command blocks parsed successfully. This validation did not start a
VM, mount the ESP, retire files, or change firmware or boot selection.
