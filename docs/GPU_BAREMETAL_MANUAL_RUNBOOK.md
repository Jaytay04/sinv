# Manual Intel UHD 770 bare-metal test

**Current state:** run `023306` completed FinalVerify and Archive after a VT-d
capability rejection. Its retained stage is a new prerequisite for another
run. The `-CompletedRun224018` commands below describe the preceding retirement
and cannot remove `023306` files. A new retirement procedure must match that
completed run before the forward steps can be repeated. The
[latest investigation](GPU_PAYLOAD_FAILURE_INVESTIGATION.md#fifth-physical-run-vt-d-programming-plan-capability-rejection)
records the result and the additional diagnostics prepared for a future run.

This is the operator-run physical test you requested, using the existing
`gpu-host-test.ps1` workflow. It is separate from the completed VM test.
The agent has not staged, armed, rebooted, or changed the primary host for this
guide. Repository automated VMX and boot validation remains in disposable VMs;
this guide does not change `AGENTS.md`.

Use **`gpu-host-test.ps1` for this guide**, not `gpu-payload-test.ps1`.
The latter's `Complete` result belongs to the VM, not your physical machine.

Run `224018` passed reviewed recovery verification and its selector is
archived. The payload still failed physical IOMMU preparation; see the
[investigation](GPU_PAYLOAD_FAILURE_INVESTIGATION.md#fourth-physical-run-iommu-preparation-failed).
Its retained stage must pass the fixed `-CompletedRun224018` retirement below
before a fresh run. The older retirement profiles do not own these files.

The [startup diagnostic VM test](GPU_STARTUP_DIAGNOSTIC_VM_RESULT.md) passed
signed EFI boot, the version-1 startup record, and four CPU health checks.
It used Windows 26200.9457, versus the host's
26200.9711, and needed a hard-stop fallback after graceful shutdown timed out.
It did not exercise the physical Intel GPU, native VT-d enforcement, native
framebuffer mapping, or host MOK acceptance. This is a diagnostic experiment,
not a validated deployment.

The new run must include commit `00184a8`, which includes the MMIO-union
correction and the expanded GPU/IOMMU failure diagnostics. The last physical
boot prepared all 20 CPUs and then failed physical IOMMU setup. The new image
records the exact failed substep and available planner/runtime result; it
does not relax that admission check. Its unsigned native build, resident-image
audit, and six relevant host tests passed. No VM is needed or started by this
guide; the current VM path does not exercise physical VT-d installation.
The updated retirement helper passed its synthetic contract tests and an
offline validation of 30 saved evidence pins. All 42 hypervisor host tests
passed. Live retirement and the next physical boot remain operator steps.

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

Confirm the feature worktree is clean and contains the new diagnostics:

```powershell
git -C .\.codex-build\worktrees\gpu-windows-validation status --short --branch
```

```powershell
git -C .\.codex-build\worktrees\gpu-windows-validation merge-base --is-ancestor 00184a8 HEAD
```

Require branch `feat/smbios-boot-serial-probe`, no changed/untracked files, and
exit code zero from the ancestry check. The main checkout may remain on `main`.

Keep the recovery disk connected, a tested full-system backup and recovery
media available, and any BitLocker recovery key offline. The recovery folder
must already exist on a different physical disk from Windows:
`E:\CodexBackup\Recovery\hsinv-gpu-test`. A folder or an ESP copy alone is
not a full-system backup.

Choose the attached USB media before readiness and Prepare. Keep that device
configuration through the test boot, rollback, and recovery verification.
Record any necessary insertion or removal; preserve the original firmware
baseline and review the actual difference if FinalVerify reports a mismatch.
Do not reuse a reviewed-difference approval from another run.

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
$readiness = powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Get-GpuBaremetalReadiness.ps1 | Out-String | ConvertFrom-Json
```

Display the captured result and keep this PowerShell session open through
retirement:

```powershell
$readiness | ConvertTo-Json -Depth 8
```

This saves local observations and temporarily mounts the system ESP at `S:`
using the existing owned-mount helper. It removes only its own mount afterward.
It does not write ESP files or firmware variables, sign, stop a driver, or arm
a boot. `S:` must be free. Require `READY_FOR_FRESH_PREPARE` before step 2.
This result is a preparation gate, not proof of MOK trust, GOP suitability,
GPU isolation, or framebuffer ownership.

**Current retry prerequisite:** run
`gpu-20261020T224018Z-31b7c15e56584f84a68d89b7b3ecb1a1` completed reviewed
reset verification and its selector is archived. Archive preserved its stage.
The recorded collection contains seven files: the five installed files,
`handoff-status.txt`, and `payload-diagnostic.txt`. This guide starts a new run;
it does not reuse those signed artifacts.

If inspection finds that stage, stop at `BLOCKED_READINESS` before Prepare,
Build, Backup, Stage, or Arm. The only expected blocker is the retained
`EFI\insecure-boot` directory. Preserve the report's exact `EvidencePath`.
The explicit `-CompletedRun224018` profile below validates that report against
the fixed completed journal, archive, and all seven recorded file hashes.
The no-argument retirement profile belongs to `150318`, and
`-CompletedRun201140` belongs to `201140`; use neither for this stage.
If readiness is already
`READY_FOR_FRESH_PREPARE`, skip retirement and continue at step 2.

### Retire the matched completed stage

First run the live review from elevated PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Retire-GpuCompletedStage.ps1 -CompletedRun224018 -ReadinessPath $readiness.EvidencePath
```

Require `REVIEWED_STAGE_READY_FOR_RETIREMENT`, `Succeeded: true`,
`EspUnmounted: true`, and no cleanup errors. Review does not remove ESP files.
It saves and verifies local and external copies of all seven files, confirms
health `ABSENT` on the expected CPUs, and checks the saved firmware baseline.
The separately pinned read-only audit tool checks raw BootNext/BootOrder and
referenced entries without a broad TPM/MOK capture. BootNext must be absent
and no referenced boot entry may target the stage. A changed hash, unknown
file, failed read, changed boot selection, or unavailable recovery copy stops
the operation.

The original firmware baseline remains preserved. This profile requires the
exact successful reviewed-difference final result from `224018` and its pinned
review evidence. Its live firmware text must match the exact reviewed
`66786928...1DAA5007` capture. For that capture, `FirmwareBaselineSource` is
`REVIEWED_224018_FINAL_CAPTURE` and `OriginalFirmwareBaselineEqual` is false.
`FirmwareBaselineEqual` must still be true for the selected capture after the
live health and raw boot checks. Every later checkpoint must match that same
selected text and raw boot order `[0,1,2]`; BootNext must be absent and
integrity findings zero. Other changes still stop retirement.
Historical raw BootOrder equality and unreferenced boot variables remain
outside the evidence's scope. Do not rewrite the baseline or delete firmware
entries to force a match.

Only after that successful review, perform the bounded removal:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Retire-GpuCompletedStage.ps1 -CompletedRun224018 -ReadinessPath $readiness.EvidencePath -Apply
```

Apply repeats the checks, saves fresh verified copies and a removal intent,
and removes only the seven matching files and their now-empty directory.
Windows Boot Manager, BCD, firmware variables, the completed journal, and its
archive are preserved. Require `STAGE_RETIRED`, `Succeeded: true`,
`EspUnmounted: true`, and no cleanup errors. If any removal or cleanup fails,
preserve its receipt and do not retry a partial removal.

Repeat the readiness command after successful retirement. Require a fresh
`READY_FOR_FRESH_PREPARE` before step 2. Do not delete or rename the directory
generically, reuse the old stage, or rerun old rollback. This retirement
profile cannot remove a future run's files.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Get-GpuBaremetalReadiness.ps1
```

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

For `startup_stage=ap_prepare`, untagged details 10-16 identify memory-policy
preparation substeps. A high-bit detail identifies an IOMMU/GPU substep and
its available result codes; preserve its complete unsigned decimal value.
The [diagnostic mapping](GPU_PAYLOAD_FAILURE_INVESTIGATION.md#vm-coverage-and-gpu-domain-diagnostic-completion)
distinguishes capability/scope, PCI/RMRR ownership, allocation, identity/root
planning, DID validation, and translation-table construction. Do not interpret
the packed value as one of the older small detail IDs.

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

Use ordinary FinalVerify for a new run. `-ReviewedFirmwareDifference` accepts
only the specifically reviewed runs and receipts encoded in the helper; it
is not a general exception to the firmware comparison. If a fresh run's
comparison fails, preserve its captures for a new review.

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

Windows [disk numbers can change across restarts](https://learn.microsoft.com/en-us/windows-hardware/drivers/storage/msft-disk).
Recovery verification checks the manifest and journal against the current
recovery disk's stable identity, verifies their recorded disk numbers agree
with each other, and requires the recovery disk to remain separate from the
Windows disk. The backup files and their hashes must still match.

Run `gpu-20261020T224018Z-31b7c15e56584f84a68d89b7b3ecb1a1` exposed an
incorrect comparison of the saved recovery disk number with its current
number: the same USB disk changed from 1 to 2, with unchanged unique ID and
serial number. That comparison is corrected. The run subsequently completed
reviewed FinalVerify and Archive; do not repeat its recovery commands or edit
the journal or manifest to replace the old disk number.

### Archived 224018 firmware inspection: completed history

This section records completed recovery work. Its commands are historical;
do not repeat them for the archived run or use them for the new test.

The subsequent FinalVerify for
`gpu-20261020T224018Z-31b7c15e56584f84a68d89b7b3ecb1a1` passed the external
backup, reset, Secure Boot, 20-CPU ABSENT health, adapter, and original Windows
Boot Manager checks. Its only remaining error was the firmware-text mismatch.
The saved mounted-ESP baseline is `4A1535FA...A723FAE` (4336 bytes); the new
capture is `66786928...AA5007` (2890 bytes). The text removes the three generic
CD/DVD, removable-device, and network blocks and their display-order
references. The Windows and two USB blocks are unchanged. This comparison
does not establish historical raw BootOrder equality or the cause of the
enumeration change.

The operator captured the fixed-run read-only inspection using:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Inspect-GpuFinalFirmware.ps1 -Run224018
```

Require `READONLY_FIRMWARE_REVIEW_CAPTURED`, `Succeeded: true`,
`SelectorAndJournalUnchanged: true`, `EspUnmounted: true`, and no cleanup
errors. The helper checks the exact failed journal and text captures, then
captures fresh ABSENT health, Windows Boot Manager integrity, raw BootNext
and BootOrder, and stable mounted firmware text. It requires BootNext absent,
no referenced staged entry, zero integrity findings, and unchanged raw boot
selection during its observation window. It does not assume the old run's
BootOrder or modify firmware.

The inspection passed. Its saved receipt is
`.analysis/gpu-final-firmware-review/1dccd9d33d1b4c808fd1059291a6d19b/result.json`,
SHA-256 `0E7107911CF43F4144F4E67AEBB647623003AF543FA38974EBEAE518F0D1642E`
(15415 bytes). Both raw captures have BootNext absent, BootOrder `[0,1,2]`,
and zero integrity findings. Fresh health reported ABSENT on all 20 CPUs;
the ESP was unmounted and the selector and journal remained unchanged.
Preserve this receipt instead of rerunning inspection after this helper
update or another recovery action changes the captured source or journal.

The completion helper selects this fixed review only for run `224018`. It
checks the pinned receipt and its underlying files, the original baseline,
the exact reviewed current text, host and artifact identities, and the current
FinalVerify intent. It then repeats the raw boot-selection and mounted
firmware checks while ordinary FinalVerify owns the ESP mount. Fresh BootOrder
must match `[0,1,2]`; another device or firmware change still blocks completion.
The baseline remains preserved, and historical raw BootOrder equality and
unreferenced firmware variables remain outside this review's scope.

The operator completed the reviewed recovery using:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 FinalVerify -ReviewedFirmwareDifference
```

The result was
`FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED`,
`Succeeded: true`, phase `Complete`, with an empty verification error list.
The comparison retains `TextEqual: false` and records
`ReviewedDifferenceAccepted: true`. Archive then returned `SELECTOR_ARCHIVED`;
the selector is absent and the journal remains `Complete`. These are completed
steps, not commands to repeat. Do not rewrite the baseline or use this approval
for another run.

The payload's physical IOMMU preparation failure (`startup_detail=4`) remains
unresolved. The new run collects the expanded diagnostics described above;
successful recovery verification does not mark that payload test as passed.

### Historical 201140 recovery: already complete

The following records the completed recovery of the old run. Do not repeat
these inspection or reviewed-FinalVerify commands as part of the new test.
Use the stage-retirement prerequisite in step 1 to prepare the next run.

For the specific `gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7`
failure captured at `2026-10-20T20:50:30Z`, the only textual changes are three
generic BCD entry identifiers and their printed application-block order. The
reset, Secure Boot, 20-CPU ABSENT health, and boot-manager checks passed. That
does not make the failed final verification complete.

From elevated PowerShell in the main workspace, collect separate read-only
evidence with the fixed-run inspection helper:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Inspect-GpuFinalFirmware.ps1
```

Require `READONLY_FIRMWARE_REVIEW_CAPTURED`, successful ESP cleanup, and unchanged
selector/journal pins. The helper checks the two exact reviewed text captures,
fresh health, the mounted ESP and Microsoft boot manager, and two raw
boot-selection observations. It requires BootNext absent, zero audit findings,
no staged toolkit reference among entries selected by BootOrder, and stable
BootOrder during the observation window. It writes only new local evidence and
temporarily mounts the ESP; it does not change boot selection or delete files.

This helper is pinned to that failed run and capture. It reports
`RecoveryCompleted: false` and cannot prove historical raw BootOrder equality;
no raw pre-arm snapshot exists. Preserve its complete result for review before
another FinalVerify or Archive. Do not run the older stage-retirement helper
against this run.

The inspection has now passed for that run. Its immutable result is
`.analysis/gpu-final-firmware-review/8abb7b84ca644a7c84019a1f29b35bb4/result.json`,
SHA-256 `BEA6DCA1FC904B13830908471D6239A906F71AA6937630B32CBA6DC3C2C023D9`.
Both raw captures have BootNext absent, BootOrder `[0,1,2,3,4,5]`, and zero
findings; fresh health reported ABSENT on all 20 CPUs. The ESP was unmounted
and the selector/journal were unchanged. The inspector ran with the module
pins from revision `485d6de`; preserve that receipt rather than rerunning it
after the recovery code changes.

The first reviewed FinalVerify attempt at `2026-10-20T21:26:30Z` exposed a
PowerShell module-scope bug: nested forced imports removed `Write-GpuJson`
from the dispatcher's scope. Its read-only captures and successful ESP unmount
receipt were saved, but the completion journal and failure receipt could not
be written. The journal remains `Disarmed` with an interrupted `FinalVerify`
intent. Preserve that action directory. The repaired helper reuses loaded
modules and avoids the retirement factory's forced imports; it does not edit
the journal or relax any review gate.

The state machine permits retrying FinalVerify from this interrupted
`Disarmed` state. No additional rollback or reset is required for this writer
failure. For this exact reviewed difference, run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 FinalVerify -ReviewedFirmwareDifference
```

Require `FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED`,
`Succeeded: true`, and phase `Complete`. This opt-in applies only to the pinned
201140 run, receipt, firmware captures, and reviewed post-reset boot session.
It repeats the ordinary final checks and requires fresh read-only raw
boot-selection captures and exact reviewed current firmware text. An unrelated
change, a set BootNext, an audit finding, a changed BootOrder, or another failed
verification gate must still fail. `FirmwareEnumeration.TextEqual` remains
false; `ReviewedDifferenceAccepted` records the limited acceptance separately.
The original baseline and previous failed action evidence are preserved.

Only after that successful final result, run the ordinary Archive command from
step 11. This does not attest GPU isolation, historical raw BootOrder equality,
or the complete namespace of unreferenced Boot variables.

If Windows cannot boot, choose the original Windows Boot Manager through the
verified firmware menu or use the prepared recovery media and full-system
backup. These Windows scripts cannot recover an unbootable Windows session.
Do not format an ESP, clear firmware keys, or restore an unidentified file.
