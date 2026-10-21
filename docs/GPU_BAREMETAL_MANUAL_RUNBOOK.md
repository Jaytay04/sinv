# Manual Intel UHD 770 bare-metal test

This runbook starts a fresh physical-host test with the existing `gpu-host-test.ps1`
workflow. It does not use the VM payload workflow. The previous physical run,
`gpu-20261021T032117Z-e70cc55505f948aab50c6c830b9048b1`, completed recovery and
was archived; its seven staged files remain on the ESP until the matched,
bounded retirement procedure in step 2 removes them.

The new `032117` retirement profile passed its read-only check against the
archived evidence and 10 related host regression tests. All 24 PowerShell
blocks below were syntax-checked. No live retirement, VM, or host boot action
was performed while preparing this guide.

The measured failure was a VT-d table-format mismatch: the remapper supported
48-bit tables while advertising a 39-bit maximum guest address width (MGAW).
Candidate fix `ada3e4f` selects a second-level table format supported by all
units while keeping the DMA and table-address limits at 39 bits. Its unsigned
native build, resident-image audit, and 43 host tests passed offline. The fix
has not yet been activated on this host; this run tests that physical behavior.
See the [failure analysis](GPU_PAYLOAD_FAILURE_INVESTIGATION.md#sixth-physical-run-vt-d-supports-only-the-48-bit-table-format)
for the saved evidence and scope of the result.

The prior photo's PCR replay refusal is an independent loader gate. Do not
clear PCRs or weaken that check. Preserve the complete payload diagnostic and
photograph any console result. A successful recovery or a health-only result
does not prove GPU isolation.

## Before starting

Use elevated **64-bit Windows PowerShell**. Run each fenced command separately
and wait for its result before continuing. Commands start from the main
workspace; the scripts operate from the feature worktree.

```powershell
Set-Location 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
```

Confirm the feature worktree is clean, on the expected branch, and contains
the candidate fix:

```powershell
git -C .\.codex-build\worktrees\gpu-windows-validation status --short --branch
```

```powershell
git -C .\.codex-build\worktrees\gpu-windows-validation merge-base --is-ancestor ada3e4f HEAD
```

Require branch `feat/smbios-boot-serial-probe`, no changes or untracked files,
and exit code zero from the ancestry check. Do not make source changes, pull,
commit, or switch branches between Prepare and Arm.

Keep the tested full-system backup and recovery media available, the external
recovery disk connected, and the BitLocker recovery key available offline.
The recovery folder must be on a different physical disk from Windows:
`E:\CodexBackup\Recovery\hsinv-gpu-test`. An ESP copy alone is not a full
system backup. Keep Secure Boot enabled and use the existing host shim/MOK
trust setup; do not clear keys or weaken signature checks. Confirm VMX and
VT-d are enabled in firmware.

Keep the USB-device configuration stable from readiness inspection through
recovery verification. Record any insertion or removal. Check that audio and
both displays work normally before the test so post-run changes are visible.
Keep the main display on NVIDIA and connect the diagnostic display to the
motherboard's Intel HDMI/DP output. Windows monitor detection does not prove
Intel GOP routing. The native path requires a supported linear Intel GOP mode,
one active pipe, and a stride-times-height extent no larger than 8 MiB; it
does not select a connector or set a mode.

Save open work and use an offline or controlled network. Do not manually
unbind the Intel Windows driver. Firmware/SMM remain trusted and can write to
display or DMA state outside the hypervisor's enforcement.

## 1. Inspect readiness and preserve its evidence path

Optional preview:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Get-GpuBaremetalReadiness.ps1 -WhatIf
```

Run the inspection and retain the exact evidence path in the same PowerShell
session:

```powershell
$readiness = powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Get-GpuBaremetalReadiness.ps1 | Out-String | ConvertFrom-Json
```

```powershell
$readiness | ConvertTo-Json -Depth 8
```

The normal inspection saves local evidence and temporarily mounts the system
ESP at `S:` for reads, then removes its own mount. It does not write ESP files
or firmware. Require `READY_FOR_FRESH_PREPARE` to proceed. If the result is
`BLOCKED_READINESS` because the retained `EFI\insecure-boot` stage exists,
continue to step 2. Preserve the report's exact `$readiness.EvidencePath`.
Resolve any other blocker before continuing. An active selector is handled
with Status and, only after successful completed verification, Archive; do
not choose a run by timestamp.

## 2. Retire only the completed 032117 stage, if readiness is blocked by it

This profile is pinned to archived run `gpu-20261021T032117Z-e70cc55505f948aab50c6c830b9048b1`
and its seven recorded stage files. The old profiles for runs `023306`,
`201140`, `224018`, and `150318` do not own these files. Do not use them here.
If readiness is already clear, skip to step 3.

First perform the live review:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Retire-GpuCompletedStage.ps1 -CompletedRun032117 -ReadinessPath $readiness.EvidencePath
```

Require `REVIEWED_STAGE_READY_FOR_RETIREMENT`, `Succeeded: true`, successful
cleanup with the ESP unmounted, and no errors. Review must preserve verified
local and external copies of all seven files, confirm Windows health is
`ABSENT` on all 20 expected CPUs, check the archived mounted-ESP firmware-text
baseline (`66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007`,
2890 bytes), and verify raw boot selection
has no BootNext, no referenced staged entry, and no integrity findings. This
profile has no reviewed-firmware-difference override. If any check fails,
preserve its evidence and stop.

Only after the review passes, apply the bounded retirement:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Retire-GpuCompletedStage.ps1 -CompletedRun032117 -ReadinessPath $readiness.EvidencePath -Apply
```

Require `STAGE_RETIRED`, `Succeeded: true`, successful cleanup, and the ESP
unmounted. Apply rechecks the review conditions and removes only the seven
hash-matched files and their empty stage directory. It preserves Windows Boot
Manager, BCD, firmware variables, the archived journal, and archive. Do not
retry after a partial removal or cleanup error.

Run readiness again and require a fresh `READY_FOR_FRESH_PREPARE`:

```powershell
$readiness = powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Get-GpuBaremetalReadiness.ps1 | Out-String | ConvertFrom-Json
```

```powershell
$readiness | ConvertTo-Json -Depth 8
```

## 3. Prepare a fresh run

Proceed only after readiness passes and the physical-output and recovery
prerequisites are confirmed:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Prepare -PhysicalOutputConfirmed -RecoveryReady
```

Require `PREPARED_READONLY_GATE_PASS` and save the printed run and journal
paths. The two switches record your confirmations; they do not verify GOP,
MOK trust, GPU ownership, or isolation.

## 4. Build and run host tests

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Build
```

Require `BUILD_AND_TESTS_PASS`, passing component tests, and unsigned resident
image audit. This builds for this run; do not substitute VM artifacts.

## 5. Sign and audit the artifacts

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Sign
```

Require `SIGNATURE_AND_AUDIT_PASS`. Preserve the signed artifact hashes and
audit evidence. Signing does not create firmware trust. Stop on any failure.

## 6. Back up the system ESP

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Backup
```

Require `ESP_BACKUP_VERIFIED` and verified local and external manifests. The
backup records ESP identity, files, firmware enumeration, a logical system
BCD export, and TPM reference logs. It does not modify Windows Boot Manager.
Do not raw-copy the locked BCD hive, delete a `.bak`, or bypass a residue
failure. Preserve a failed action and follow step 13; do not retry Backup.

## 7. Stage the signed files

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Stage
```

Require `STAGE_READBACK_PASS`. This writes the run's EFI payload files to the
ESP and verifies their hashes. Windows Boot Manager remains unchanged; the
next boot is not yet armed.

## 8. Arm the one-shot boot

Recheck recovery-disk access, MOK prerequisites, and all preceding receipts.
Then run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Arm
```

Require `ARMED_MANUAL_REBOOT_REQUIRED`. This changes only the workflow-owned
one-shot boot state and does not reboot. Do not arm again or change BootOrder.

## 9. Boot once and record the console

Record the console externally if possible. A verified legacy COM1 connection
can capture hypervisor serial output; a USB serial adapter alone is not the
required legacy I/O port. Photograph any firmware, shim, gate, or payload
message exactly. After saving work, manually restart:

```powershell
shutdown.exe /r /t 0
```

Watch both displays. If admitted, the Intel display should show the one-time
red `ROOT GPU` diagnostic while Windows uses NVIDIA. Do not weaken admission,
force a display mode, or repeat boots to obtain a pass. A PCR-bank replay
refusal is independent: preserve it as evidence and do not clear PCRs.

## 10. Collect the post-boot evidence

After Windows returns, open elevated PowerShell in the main workspace:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Collect
```

The expected health-only success status is
`COLLECTED_HEALTH_ONLY_GPU_UNATTESTED`. Require all expected CPUs to pass,
Secure Boot on, and the same Windows Boot Manager hash. `GPU_UNATTESTED` is
the collector's explicit scope; do not relabel it as isolation success.

Preserve the complete action-directory `esp-payload-diagnostic.txt`,
`esp-handoff-status.txt` when present, and a clear console photograph. Keep
all startup fields intact, including `startup_detail`, AP preparation status,
and shim/payload launch status. The startup record can identify returned
errors but cannot record a nonreturning failure. PCR replay refusal is a
separate loader gate; it does not explain a payload startup failure. Do not
decode capability-detail records using the older detail layout; use the
[failure analysis](GPU_PAYLOAD_FAILURE_INVESTIGATION.md#capability-evidence-for-a-future-diagnostic-run).

If collection fails, preserve the evidence and use step 13. Do not run Arm
again.

## 11. Disarm and reset the platform

After collection, clear only this run's owned one-shot state:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Disarm
```

Require successful disarm. `DISARMED_RESET_REQUIRED` means the machine still
needs a full reset. Save work and shut Windows down fully:

```powershell
shutdown.exe /s /t 0
```

After power-off, turn the machine on and boot the original Windows Boot
Manager. Sleep and hibernation do not satisfy the reset boundary.

## 12. Verify recovery, then archive

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 FinalVerify
```

Require `FINAL_RESET_VERIFIED_GPU_UNATTESTED`, `Succeeded: true`, phase
`Complete`, a boot ID different from the disarm boot ID, health `ABSENT` on
every expected CPU, Secure Boot on, restored adapters, unchanged Windows Boot
Manager, and matching firmware text captured in the same ESP context. A
firmware mismatch is a failed verification until separately reviewed by the designated
read-only firmware review workflow. Preserve its captures; do not approve a
difference from an older run. Recovery verification does not prove the GPU
test succeeded.

Only after successful FinalVerify, archive the selector:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Archive
```

Require `SELECTOR_ARCHIVED` and `JournalPreserved: true`. Archive does not
remove staged files. Retire a completed stage only with a future matched
profile for that exact run; never reuse the 032117 retirement profile.

## 13. Recovery after failure or interruption

Stop forward actions, preserve all reported paths, and inspect the exact run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Status
```

Use rollback for a failed or interrupted run, including a failure before Arm:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-rollback.ps1
```

If rollback reports `RECOVERY_PENDING_USER_POWER_CYCLE`, perform the full
shutdown and power-on from step 11, then run FinalVerify. Rollback does not
return the live Intel adapter to WDDM or erase staged files. For an early
abort with no Arm intent and no recorded ESP target, follow the rollback
result; do not guess a firmware entry.

Do not retry an interrupted forward action. The only retry allowed by this
workflow is FinalVerify after the run is already `Disarmed` and verification
alone failed; preserve all earlier receipts. Never edit a journal, manifest,
or firmware baseline to make a check pass. If Windows cannot boot, use the
prepared recovery media and full-system backup; these scripts cannot repair
an unbootable Windows session.
