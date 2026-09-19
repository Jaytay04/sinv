# Deployment and Detection Runbook

This runbook covers the two verified environments. Always start from a clean
snapshot and use the host runner so VM leases, timeouts, evidence capture, and
recovery remain consistent.

## 1. Preflight

From the repository root in PowerShell:

```powershell
$Runner = '.\tools\host-runner\Invoke-UEFILoaderHostRunner.ps1'

powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action Doctor -Profile All
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action Build
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunAuditHostGate -SkipBuild
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunFixtureHostGate -SkipBuild
```

Do not power on either VM manually while its mailbox lease is held. A failed
live run with stop/revert authorization hard-stops the exact configured VM,
restores the Windows clean snapshot when applicable, and releases its lease.

## 2. VMware Windows: record and detect

VMware is the negative replay control because its firmware populates PCR0–7.
It is still the correct environment for the native Windows recorder and audit
adapters.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner `
  -Action RunWindowsProbe `
  -AuthorizeLiveVmRun -AuthorizeVmStop -AuthorizeSnapshotRevert

powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner `
  -Action RunWindowsCapture `
  -AuthorizeLiveVmRun -AuthorizeVmStop -AuthorizeSnapshotRevert
```

Expected probe result: TPM 2.0 and Event03 are present, but the empty-bank
verdict is `NOT_MET`. Expected capture result: `tcglog.ib` and the full TBS log
are returned in the run evidence. The runner stops and reverts the disposable
clone after each run.

For an interactive defensive audit inside a snapshot-protected Windows guest,
run from an elevated terminal after copying the static binaries and captured
log:

```powershell
.\ibaudit.exe dump --log .\tcglog.ib
.\ibaudit.exe verify-pcrs --log .\tcglog.ib
.\ibaudit.exe tcg-version
.\ibaudit.exe audit-vars --expected-mok .\expected-mok.txt

$BootHash = (Get-FileHash C:\Windows\Boot\EFI\bootmgfw.efi -Algorithm SHA256).Hash
.\ibaudit.exe bootmgr-integrity --expected-hash $BootHash
.\ibaudit.exe baseline --log .\tcglog.ib --output .\baseline.json
```

On a later boot, capture a new log and run:

```powershell
.\ibaudit.exe diff --baseline .\baseline.json --log .\tcglog.ib
```

Exit `0` means no finding, `1` means a detector found drift or a security
signal, and `2` means the observation itself was incomplete or unsafe.

## 3. Nested Linux: deploy and replay components

The Linux profile builds pinned measurement-disabled OVMF and creates a fresh
swtpm state per run. These verified actions exercise the positive hard gate,
hybrid replay, PE/COFF rehash, divergence persistence, restore/handoff, and the
shim one-shot abort/restore path:

```powershell
$Actions = @(
  'RunLinuxPositivePath',
  'RunLinuxReplayDryRun',
  'RunLinuxPeRehash',
  'RunLinuxDivergenceManifest',
  'RunLinuxHandoffMechanics',
  'RunLinuxProductionHandoff',
  'RunLinuxOneShotCycle'
)

foreach ($Action in $Actions) {
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner `
    -Action $Action -AuthorizeLiveVmRun -AuthorizeVmStop
  if ($LASTEXITCODE -ne 0) { throw "$Action failed" }
}
```

Never reuse a swtpm state for an empty-bank test. The guest scripts create a
fresh state and use the swtpm control socket for QEMU; direct `tpm2-tools`
queries use the server socket. Mixing those sockets caused the historical
connection error documented in the old lab notes.

For the complete Windows replay and production BootNext walkthroughs, use the
WSL2/KVM actions against the latest immutable prepared Windows disk:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner `
  -Action RunWslNestedWindowsE2E `
  -AuthorizeLiveVmRun -AuthorizeVmStop

powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner `
  -Action RunWslBootNextE2E `
  -AuthorizeLiveVmRun -AuthorizeVmStop
```

The BootNext gate uses the production `ib-install.exe` firmware API path. It
records firmware-manager and `bootmgfw.efi` hashes before mutation, proves
BootNext is consumed before the second Windows startup, removes only its owned
Boot#### entry, and requires the final firmware-manager bytes to match the
pre-gate bytes exactly.

## 4. One-shot staging and rollback

First prove signing and transactional staging on a host-only ESP model:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunInstallerHostGate
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunInstallerOneShotHostGate
```

On a real disposable Windows VM, `ib-install stage` copies the verified shim,
signed loader, log, and payload to `EFI\insecure-boot`. MOK enrollment occurs
only through shim MokManager. It is never performed by the Windows tool.

Prefer BootNext for the first real boot because it does not replace Microsoft's
boot manager. Use replace mode only from a clean snapshot after its host gate
passes. If verification reports residue or a hash mismatch, do not retry in
place: power off, restore the snapshot, and use BootNext on the next attempt.

The audit binary contains no firmware-variable write API. Only `ib-install`
owns BootNext and boot-manager mutation paths.

WSL/QEMU rollback discards the per-run qcow2 overlay, OVMF variable store, and
swtpm directory; it never mutates the immutable prepared VMDK. VMware rollback
hard-stops only the configured disposable clone, restores its named clean
snapshot, verifies it is off, and releases its exact lease.
