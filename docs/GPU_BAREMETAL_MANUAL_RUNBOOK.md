# Manual Intel UHD 770 bare-metal test

This is the operator-run physical test you requested, using the existing
`gpu-host-test.ps1` workflow. It is separate from the completed VM test.
The agent has not staged, armed, rebooted, or changed the primary host for this
guide. Repository automated VMX and boot validation remains in disposable VMs;
this guide does not change `AGENTS.md`.

Use **`gpu-host-test.ps1` for this guide**, not `gpu-payload-test.ps1`.
The latter's `Complete` result belongs to the VM, not your physical machine.

The [startup diagnostic VM test](GPU_STARTUP_DIAGNOSTIC_VM_RESULT.md) passed
signed EFI boot, the version-1 startup record, and four CPU health checks.
It used Windows 26200.9457, versus the host's
26200.9711, and needed a hard-stop fallback after graceful shutdown timed out.
It did not exercise the physical Intel GPU, native VT-d enforcement, native
framebuffer mapping, or host MOK acceptance. This is a diagnostic experiment,
not a validated deployment.

## Before starting

Use elevated **64-bit Windows PowerShell**, and run each command separately.
Do not paste the entire guide into a terminal. Wait for a successful result
before the next forward action. All paths below start at the main workspace;
the scripts operate on the feature worktree and leave `main` untouched.
After your normal Windows restart, confirm that audio and displays work at
baseline before starting the new experiment.

```powershell
Set-Location 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
```

Keep the recovery disk connected, a tested full-system backup and recovery
media available, and any BitLocker recovery key offline. The recovery folder
must already exist on a different physical disk from Windows:
`E:\CodexBackup\Recovery\hsinv-gpu-test`. A folder or an ESP copy alone is
not a full-system backup.

Keep the main display and firmware console on NVIDIA. Connect the diagnostic
display to the motherboard's Intel HDMI/DP output. Windows detecting that
monitor does not prove an active Intel firmware/GOP mode or exclusive ownership.
The native path needs a supported linear Intel GOP mode, one active pipe, and
a stride-times-height extent of at most 8 MiB. It does not enable a connector
or set a display mode for you. Unsupported state must remain unsupported.

Secure Boot must stay enabled. The existing project signer must already be
accepted through the host's intended shim/MOK trust path. A successful local
signature check or VM firmware-db append does not prove that prerequisite.
Use the existing [shim inventory procedure](EFI_INVENTORY_PATH_B.md) for its
scope; do not clear keys, weaken validation, or disable Windows security to
force this test through. VMX/VT-d availability is also required.

Save work and use an offline or controlled network. Do not stop/unbind the
Intel Windows driver manually: native boot enforcement is responsible for
guest exclusion. Firmware/SMM are explicitly trusted; their display or DMA
writes can silently violate root ownership without a guaranteed latch.

## 1. Inspect readiness before creating another run

Optional preview; this performs no queries or filesystem operations:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Get-GpuBaremetalReadiness.ps1 -WhatIf
```

Then run the actual inspection:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Get-GpuBaremetalReadiness.ps1
```

This saves local observations and temporarily mounts the system ESP at `S:`
using the existing owned-mount helper. It removes only its own mount afterward.
It does not write ESP files or firmware variables, sign, stop a driver, or arm
a boot. `S:` must be free. Require `READY_FOR_FRESH_PREPARE` before step 2.
This result is a preparation gate, not proof of MOK trust, GOP suitability,
GPU isolation, or framebuffer ownership.

**Current retry prerequisite:** the latest physical run
`gpu-20261020T150318Z-59a346ed3cb8422bafe4be60268b7df1` completed
reset verification and its selector is archived. Archive preserved its stage.
The recorded collection contains seven files: the five installed files,
`handoff-status.txt`, and `payload-diagnostic.txt`.

If inspection finds that stage, stop at `BLOCKED_READINESS` and preserve the
printed report path. Do not start Prepare, Build, Backup, Stage, or Arm yet.
The current `Retire-GpuCompletedStage.ps1` is hard-pinned to the older
`gpu-20261020T031023Z-9a2a92a17bf54b4faf4b60c6b7231ec7` six-file stage;
its `-Apply` command is not a cleanup command for this latest run.

A retirement procedure for the seven-file stage must first bind the fresh
readiness inventory to the latest archived journal and collected file hashes,
verify the external recovery copies, and verify that no boot selection still
references the stage. That review has not yet been performed for the live
state after your restart. Do not delete or rename the directory, reuse the
stage, edit a journal, or rerun old rollback to get around this gate.

Only after the matching stage has been reviewed and retired should you repeat
the readiness command. Require a fresh `READY_FOR_FRESH_PREPARE` before step 2.
The commands below describe the complete forward test and recovery sequence
once that prerequisite is satisfied; they do not bypass a blocked first step.

If inspection instead finds an active selector, inspect its saved state:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Status
```

Only when that exact run has successful `FinalVerify`, phase `Complete`, and
no action in progress, archive its selector:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Archive
```

If Status says `NOT_PREPARED`, skip Archive. Archive preserves evidence; it
does not clean the ESP. Re-run the readiness inspection after resolving its
specific blocker. Do not edit a journal or select a run by directory timestamp.

## 2. Prepare the fresh physical run

Only after readiness and the manual prerequisites above pass:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Prepare -PhysicalOutputConfirmed -RecoveryReady
```

Require `PREPARED_READONLY_GATE_PASS`. Save the printed run/journal paths.
These switches are your declarations, not connector/GOP/MOK verification.
Prepare records the host and clean feature commit; it does not activate GPU
isolation. Do not edit source, pull, commit, or switch branches between Prepare
and Arm. A changed revision requires closing that run and starting a fresh one.

## 3. Build and test

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Build
```

Require `BUILD_AND_TESTS_PASS`, both component CTest suites green, and unsigned
`.hvscan` pass. The scripts find Visual Studio themselves. This is a new
run-specific build, not reuse of the VM binaries. Native GPU support and
physical-output confirmation are on; the synthetic VM self-test is off.
The host loader disables VM debug ports and enables replay-independent payload
loading. The signed VM test used debug ports on, so its loader hash differs.

## 4. Sign and audit

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Sign
```

Require `SIGNATURE_AND_AUDIT_PASS`. Signing uses the existing current-user key
without exporting it or creating firmware trust. Preserve both signed hashes
and the signed `.hvscan` result. Do not substitute a VM payload or bypass a
signature failure.

## 5. Back up the recorded system ESP

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Backup
```

Require `ESP_BACKUP_VERIFIED` and verified local/external manifests. Backup
records ESP identity, firmware enumeration, EFI files, a logical system BCD
export, and reference TPM logs. It does not modify Windows Boot Manager.
Do not raw-copy a locked BCD hive, delete an older `.bak`, or bypass a residue
failure. A Backup failure goes to the recovery section, not another Backup.

## 6. Stage the signed files

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Stage
```

Require `STAGE_READBACK_PASS`. This is the first forward step that writes EFI
payload files to the ESP. It verifies their hashes and leaves Windows Boot
Manager unchanged. It does not arm the next boot.

## 7. Arm this run's one-shot boot

Recheck recovery access, MOK prerequisites, and all preceding receipts first:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Arm
```

Require `ARMED_MANUAL_REBOOT_REQUIRED`. This changes owned BootNext/Boot####
state; it does not reboot. Do not arm again or change BootOrder.

## 8. Perform the physical test boot

Record the console externally if possible. A verified legacy COM1 connection
can capture hypervisor serial output; a USB serial dongle alone does not
provide the required legacy I/O port. Do not enable VM debug-port writes on
the host. Photograph any firmware, shim, gate, or payload error exactly.

After saving work, run this command yourself to consume the armed boot:

```powershell
shutdown.exe /r /t 0
```

Watch both displays. If admitted, the Intel display should show the one-time
red `ROOT GPU` diagnostic while Windows uses NVIDIA. Record a photograph and
the run ID. Absence of the pattern, a halt, signature rejection, or Windows
health `ABSENT` is not a successful physical test. Do not weaken admission,
force a display mode, sleep/hibernate, or loop repeated boots to obtain a pass.

## 9. Collect evidence after Windows returns

Open elevated PowerShell, return to the main workspace, then run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Collect
```

The health-only success status is
`COLLECTED_HEALTH_ONLY_GPU_UNATTESTED`. Require every CPU captured at Prepare
to pass, Secure Boot on, and the same Windows Boot Manager hash. Collection
also saves available `handoff-status.txt` and `payload-diagnostic.txt` as
`esp-handoff-status.txt` and `esp-payload-diagnostic.txt` in its action directory.
Preserve the exact `payload_launch`, `payload_status`, `shim_load_phase`, and
`shim_load_status` values. The new loader also captures `startup_record` and,
when valid, `startup_stage`, `startup_reason`, `startup_status`, processor,
CPU counts, and preparation/activation failure fields. Preserve the entire
file. A valid returned-error record narrows the internal startup failure;
`unavailable` or `invalid` does not identify a stage. A file can be unavailable
if boot stopped before writing it; preserve the console capture in that case.
This record cannot persist a nonreturning fail-stop or guarantee a fresh file
when writing fails. Do not clear TPM PCRs to bypass the replay guard.

`replay=ABORTED` alone does not explain a payload failure: the loader is
replay-independent. A healthy hypervisor or a visible pattern also does not
independently attest every plane/GGTT/DMA permission. The Windows collector
always reports GPU isolation as unattested; do not relabel that result.

If collection fails, use the recovery path below. Do not run another Arm.

## 10. Disarm, then reset the platform

After collection, clear only this run's owned one-shot state:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Disarm
```

Require success. `DISARMED_RESET_REQUIRED` means the physical reset is still
needed. There is no runtime return of the live Intel adapter to WDDM; pages
stay pinned for that session. After saving work, perform a full shutdown:

```powershell
shutdown.exe /s /t 0
```

Once powered off, power the machine on and boot the original Windows Boot
Manager normally. Do not use sleep or hibernation as the reset boundary.

## 11. Verify recovery and preserve the result

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 FinalVerify
```

Require `FINAL_RESET_VERIFIED_GPU_UNATTESTED`, `Succeeded: true`, phase
`Complete`, a changed boot ID after disarm, health `ABSENT` on all expected
CPUs, Secure Boot on, both adapters restored, unchanged Windows Boot Manager,
and matching firmware enumeration in the same ESP mount context. This is
recovery proof, not proof the preceding GPU experiment succeeded.

Only then archive the selector:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Archive
```

Keep the journal, signed hashes, CTest/audit logs, console captures, monitor
photograph, and external recovery copy. Stage files remain evidence until
separately reviewed and retired; Archive does not remove them. A retirement
procedure must match that exact completed run and its files. Keep all retained
repository VM folders on disk as well.

## Recovery after a failed or interrupted action

Stop forward actions and preserve the reported paths. Query the exact saved
run, then use the existing recovery entry point:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Status
```

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-rollback.ps1
```

After an Arm intent, rollback can report
`RECOVERY_PENDING_USER_POWER_CYCLE`. Require successful cleanup, perform the
full shutdown/power-on boundary from step 10, then run FinalVerify. Rollback
does not perform runtime hand-back or erase staged files.

For an early abort with no Arm intent and no recorded ESP backup,
`DISARMED_NO_ACTIVATION` can be followed by FinalVerify without reboot;
require `VERIFIED_ABORTED_NO_ACTIVATION` before Archive. If an owned ESP mount
was acquired, its unmount receipt must also succeed. Never unmount an unrelated
`S:` assignment.

If phase is already `Disarmed` and only FinalVerify failed, correct the
reported verification issue and retry **FinalVerify**, not the already
completed cleanup. Keep every previous receipt. A cleanup error is not success
just because an entry is absent; do not reinterpret an installer's exit code.

If Windows cannot boot, choose the original Windows Boot Manager through the
verified firmware menu or use the prepared recovery media and full-system
backup. These Windows scripts cannot recover an unbootable Windows session.
Do not format an ESP, clear firmware keys, or restore an unidentified file.
