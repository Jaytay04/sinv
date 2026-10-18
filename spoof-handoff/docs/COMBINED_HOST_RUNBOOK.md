# Combined identifier host runbook

## Scope and current evidence

The fresh R16 gate passes strict source and archive validation:
spoof-handoff/reports/activation-repair-20261018/gate/combined-vm-gate.json
SHA-256 AF9024D7EFBF725D464825E5A7217F39CADD0538FD56C8155C24C3D9EBDBF483.
It records 22 authenticated actions, 107 source pins, and 352 matching reader
observations across original, active, normal-restored, and cold-restored
phases. The VM is snapshot-restored and powered off. Ten selected regression
suites pass 421 checks; the real repaired stock build passes 28 loader and
two inventory tests, with a 224-character maximum predicted compiler-probe
path against the unchanged 240-character guard.

The physical-host simultaneous proof is complete on DESKTOP-5852FUT. The
2026-10-18 run passed all 12 bindings (nine categories) in original, simultaneous
replacement, and restored modes, with two polls and 90 matching observations
per mode. Final verification returned FINAL_VERIFY_PASS, Secure Boot ON and
test-signing OFF; its journal is Complete. No further action is needed for this
completed run. The sequence below documents the controlled run, not an
instruction to reactivate it. See
[`the final proof`](../reports/review-20261018-combined/combined-host-proof.json)
and [`the closeout`](../reports/review-20261018-combined/README.md).

The host attempt dated 2026-10-18 19:31 UTC was aborted during the stock
Path-B build, before Stage or Arm. CMake reported a 265-character object
directory and MSVC C1041 opening the compiler PDB. Its original combined
journal remains unchanged at CombinedArmIntent. The failed selector was
archived only after the exact storage run reached FinalVerify PASS, with the
original native/WMI disk serial, no filter/service/package, test-signing
absent, owned certificate trust removed, and Secure Boot ON (variable 01).
The aborted-host closeout and archived selector are preserved under
spoof-handoff/reports/activation-repair-20261018/. This is an aborted,
recovered storage/policy cycle; it is not the simultaneous nine-category proof.

Prepare now compiles the complete stock Path-B loader and inventory at a
deterministic short root under spoof-handoff/artifacts/cb/<16-hex-id>/.
It requires both CTest suites (28 loader tests and two inventory tests) and
pins all four outputs and both build logs before storage Prepare or any
policy/driver action. Arm reuses this build instead of compiling after the
replacement disk filter has been installed. It verifies the prepared hashes
and then the exact expected VM-pinned vehicle and host-payload replacements.
The earlier short native-reader roots and short host-payload build roots
remain in place. Path-length and source/signature checks remain enabled.

Combined rollback obtains the authoritative storage phase from Status after
each child action. Remove and VerifyOriginal return statuses rather than a
phase. It preserves reboot boundaries and distinguishes an aborted recovery
from a completed simultaneous test. ABORTED_RECOVERED_NO_SIMULTANEOUS_PROOF
means the recovery endpoint was reached without an all-fields active proof;
it must not be recorded as a successful combined run.

The test fixture still corrects only the disposable VM's SMBIOS 2.x advertised
maximum structure size from 111 to the measured 112. Table contents and the
production guard are unchanged. The scope remains seven SMBIOS fields, the
boot disk, and four adapters (12 bindings, nine categories); USB devices are
excluded and CPU/GPU serials remain disabled. The final report must identify
the actually observed reader paths; a VM result is not a physical-host proof.

The storage filter is boot-start. Keep a known-good system image, Windows
recovery media, BitLocker recovery information, and the separate external
recovery disk available. The offline removal procedure has not been validated
in WinRE on this physical PC.

## Prerequisites

- Run **elevated Windows PowerShell** at the main repository root. Every command
  below calls the implementation in the assigned worktree because the matching
  launchers are absent from the main checkout. Reports and recovery evidence
  mentioned below are written under that worktree unless a command prints a
  separate external recovery path.
- Before Prepare, Secure Boot must be enabled. Prepare independently verifies
  the live mode and records the Secure Boot variable, firmware values,
  supported BCD state, ESP, and current host originals.
- Keep the external recovery disk mounted as `E:` at
  `E:\CodexBackup\Recovery\hsinv-storage-test`. Prepare checks that it is on
  a different physical disk from Windows and writes a unique recovery copy for
  this run.
- Do not manually change test-signing, certificate stores, firmware keys, CSM,
  SATA/VMD, TPM, or other boot settings before Prepare.
- Use a single action at a time. The command launcher serializes steps and
  saves each child process's stdout, stderr, and exit code under
  `spoof-handoff/reports/combined-host-command-steps-20261018/actions/`.
- Let each command finish and return before starting another command. The
  wrapper does not cancel a still-running installation; do not launch rollback
  or another action while one is active.

Prepare inputs must be pinned to the newly accepted VM gate, the signed
storage release, the seven-field SMBIOS reader, and the current NIC readers.
Prepare checks the gate and release hashes and pins all readers in the new
host journal. Do not substitute VM originals for the fresh physical-host
capture. The launcher accepts the R16 gate pinned above. Retry Prepare only
after the exact interrupted marker has its reviewed retirement receipt.

## Prepare and save the run

From the main repository root, run this command in elevated PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\commands\combined-host-test.ps1 Prepare
```

Prepare captures host originals and recovery evidence. It does not install a
driver, change boot policy, stage or arm Path-B, or reboot. Continue only when
the command returns `PREPARED_READONLY_GATE_PASS` and
`activationAvailable: true`. The launcher then saves the exact successful
Prepare JSON to the feature worktree's
`spoof-handoff/reports/f12-release/host-prepared.json` using an atomic,
no-overwrite write, then removes the attempt marker. Later commands
automatically select the run recorded there. The launcher refuses another
Prepare while that file exists; preserve it and the run journal until final
verification or verified recovery.

If Prepare fails or is interrupted, stop. Preserve the per-action stdout,
stderr, exit code, the `host-prepare-attempt.json` marker, and any run
directory printed by the command. The marker is stored beside
`host-prepared.json` in the feature worktree at
`spoof-handoff/reports/f12-release/host-prepare-attempt.json`. Its schema is
`combined-host-prepare-attempt.v1`; its initial status is
`PREPARE_STARTED_COMPLETION_UNCONFIRMED`. It records the attempt ID, start
time, report root, action-evidence root, and directories that existed before
the attempt. Do not retry Prepare, remove or edit the marker, choose the
newest reports directory, change firmware settings, or continue to a later
step. The marker intentionally blocks another attempt until the incomplete
attempt is reconciled using its exact evidence and run path. A fully successful
Prepare saves `host-prepared.json` and then removes the marker.

The marker does not itself identify a new run directory. Review the command
receipt and the exact combined/storage journals. Use `-RunDirectory` for
Status or rollback only when the successful Prepare output or an exact
failure receipt identifies that directory and it contains a valid combined
journal. If no valid journal can be identified, preserve the marker and all
action receipts and stop for review; rollback cannot reconstruct a missing
journal and must not guess a run directory. The current wrapper has no
automatic reconciliation command for this partial-Prepare state.

If Prepare returned successfully but the launcher reports that it could not
save `host-prepared.json`, preserve the complete returned JSON, the attempt
marker, and the exact `runDirectory` printed in the error. Do not rerun
Prepare. Use that exact path with the `-RunDirectory` override for Status or
rollback. Never substitute a newer or guessed directory. The marker remains
until the saved successful result is safely written or the failed attempt is
reconciled.

## Run the lifecycle

Run every command below in elevated PowerShell from the main repository root.
Each command is a separate action and does **not** reboot automatically. Wait
for a successful result before proceeding. After every Windows reboot, open a
new elevated PowerShell session at the repository root and run the next
command. The launcher reads the saved Prepare record to select the same run.

### 1. Disable Secure Boot in firmware

Only after Prepare succeeds, enter MSI firmware setup: press **Delete**, then
**F7** for Advanced Mode; open **Settings > Security > Secure Boot** and set
Secure Boot to **Disabled**. Save with **F10** and boot Windows. Confirm
`Confirm-SecureBootUEFI` returns `False`. Do not delete keys or change other
firmware settings.

### 2. Enable Windows test-signing

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\commands\combined-host-test.ps1 EnableTestMode
```

Reboot Windows manually after success. The action enables the recorded
test-signing policy and adds only the owned certificate trust needed by the
existing signed storage release.

### 3. Test the transparent storage filter

Install the transparent package, then reboot:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\commands\combined-host-test.ps1 InstallTransparent
```

After Windows boots, verify the filter is in the boot disk stack while native
and WMI serial reads remain original:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\commands\combined-host-test.ps1 VerifyTransparent
```

Remove the transparent package, then reboot:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\commands\combined-host-test.ps1 Remove
```

After reboot, confirm the original storage path and filter removal:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\commands\combined-host-test.ps1 VerifyOriginal
```

### 4. Verify the replacement storage package

Install the replacement package, then reboot:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\commands\combined-host-test.ps1 InstallReplacement
```

After Windows boots, verify the exact replacement through the native storage
IOCTL and fresh WMI read:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\commands\combined-host-test.ps1 VerifyReplacement
```

### 5. Arm and collect the combined readback

Stage and arm the dedicated Path-B inventory collector:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\commands\combined-host-test.ps1 ArmCombined
```

Reboot Windows manually only after ArmCombined succeeds. After Windows boots,
collect the simultaneous all-fields readback:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\commands\combined-host-test.ps1 CollectCombined
```

This checks seven SMBIOS values, the boot disk serial, and four selected
network adapters through the configured reader matrix. The NIC transaction is
journaled and restored before CollectCombined succeeds. USB devices remain
excluded.

### 6. Remove the replacement and restore Windows policy

Remove the replacement package, then reboot:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\commands\combined-host-test.ps1 Remove
```

After reboot, confirm original values and filter removal:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\commands\combined-host-test.ps1 VerifyOriginal
```

Restore the test-signing policy and the certificate trust owned by this run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\commands\combined-host-test.ps1 RestorePolicy
```

Reboot Windows manually after success. Secure Boot is still disabled at this
point.

### 7. Re-enable Secure Boot and verify restoration

After the normal Windows boot following RestorePolicy, enter firmware setup
again. Set **Settings > Security > Secure Boot** to **Enabled**, save with
**F10**, and boot Windows. Confirm `Confirm-SecureBootUEFI` returns `True`,
then run the final verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\commands\combined-host-test.ps1 FinalVerify
```

Only a successful `FinalVerify` closes the physical-host run. It checks the
original readers, removed filters and trust, restored test-signing, Secure
Boot, supported BCD enumeration, recorded firmware values, and ESP comparison.
`BOOTSTAT.DAT` is classified as mutable boot status, so the ESP comparison is
not byte-identical for that file. Keep all action receipts, the combined run
journal, the external recovery copy, and final verification report.

The recorded host run required the supplementary finalizer after the original
FinalVerify completed storage verification but failed exporting BCD to a
273-character destination. The supplementary verifier uses a short export
destination, imports the storage reader's model, compares PnP identities without
case sensitivity, and compares absent firmware variables by explicit presence
and read status. It validates the original source pins and completed storage
proof, obtains fresh restoration evidence, and records Complete only after
every check passes. The original pinned runner remains unchanged.

For that exact partial-completion state (storage Complete, idle combined phase
PolicyRestorePendingReboot), the recovery of verification is:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\commands\combined-host-finalize.ps1
```

This is a verification-only continuation with no activation, driver action, or
reboot. It is not needed again after the saved journal is Complete. The final
successful firmware read includes all 21 records, with BootNext absent and
SecureBoot equal to 01. Raw live BCD hive byte equality is not claimed.

## Status and recovery

The current saved run can be inspected without choosing a run directory:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\commands\combined-host-test.ps1 Status
```

If Windows boots but an action fails, stop before the next action or reboot.
Preserve that action's stdout, stderr, exit code, and the run journal. Run
rollback; it selects the saved successful Prepare run automatically:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\commands\combined-host-rollback.ps1
```

Follow the rollback output. If it reports
`RECOVERY_PENDING_USER_REBOOT_OR_FIRMWARE`, perform only the stated manual
reboot or firmware action and run the same rollback command again. Recovery is
complete for a successful combined run only after `RECOVERY_VERIFIED`. An aborted run can instead return `ABORTED_RECOVERED_NO_SIMULTANEOUS_PROOF`: its recovery endpoint is recorded separately, and the active simultaneous proof remains unperformed. Preserve that evidence and review it before preparing a fresh run.

If the saved selector is unavailable but the exact run directory is known,
pass that exact path rather than asking the launcher to guess:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\commands\combined-host-rollback.ps1 -RunDirectory 'C:\exact\runDirectory\printed\by\the\launcher'
```

If Windows cannot boot, neither PowerShell command can run. Use the prepared
Windows recovery USB and follow
[`STORAGE_BOOT_RECOVERY.md`](STORAGE_BOOT_RECOVERY.md) with the exact external
recovery directory printed by Prepare. Prefer System Restore or a known-good
full system image. The offline-removal procedure is limited to exact release
INF matches and has not been validated in WinRE on this physical PC. A BCD
export or ESP snapshot alone is not a Windows system image. Preserve the
interrupted run evidence; do not call an interrupted test a pass.

After `FinalVerify` or a rollback reporting `RECOVERY_VERIFIED`, retain the
completed host-prepared JSON, journal, receipts, and external recovery copy.
If preparing a new run later, archive the old
`host-prepared.json` only after that verified endpoint; the launcher will not
overwrite it.
