# UEFILoader

UEFILoader is a VM-only measured-boot research toolkit. It records a Windows
TCG event log, replays the pre-boot portion from a UEFI loader when PCR banks
are still at reset, stages a signed shim/MOK boot chain, and audits the result
with six read-only detectors.

> This project deliberately includes test firmware that suppresses OVMF
> measurements. Use it only in disposable, snapshot-protected virtual
> machines. It is not production firmware.

## Toolkit map

| Component | Purpose |
|---|---|
| `loader.efi` | Enforces the empty-PCR hard gate, performs hybrid replay, persists divergence evidence, restores the boot manager, and hands off. |
| `ib-tcg-dump.exe` | Uses Windows TBS to probe TPM state and capture a strict `tcglog.ib`. |
| `ib-install.exe` | Signs loader/payload images, stages the shim chain, and manages replace/restore or BootNext one-shot boot. |
| `ibaudit.exe` | Dumps/validates logs, verifies PCRs, audits UEFI variables and boot-manager integrity, detects TCG downgrade signals, and creates/diffs JSON baselines. |
| `fixture_gen.exe` | Generates deterministic clean, tampered, downgrade, PCR-state, and baseline-drift fixtures. |
| `tools/host-runner/` | Builds, leases disposable VMware guests, runs bounded gates, captures evidence, and restores the Windows snapshot. |

## Build and local gates

From PowerShell in this repository:

```powershell
$Runner = '.\tools\host-runner\Invoke-UEFILoaderHostRunner.ps1'

powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action Doctor -Profile All
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action Build
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunAuditHostGate
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunFixtureHostGate
```

The static build places Windows binaries in `build\windows-static`. Focused
gate evidence is written below `.analysis\host-runner`.

## Command summary

```text
ib-tcg-dump probe
ib-tcg-dump capture --out tcglog.ib --full tcglog-full.bin

ib-install sign ...
ib-install stage ...
ib-install oneshot --mode replace|verify|bootnext|off ...

ibaudit dump --log tcglog.ib
ibaudit verify-pcrs --log tcglog.ib [--divergence divergence.ib]
ibaudit audit-vars --expected-mok expected-mok.txt
ibaudit tcg-version
ibaudit bootmgr-integrity --expected-hash <sha256> [--esp-root <path>]
ibaudit baseline --log tcglog.ib --output baseline.json [--esp-root <path>]
ibaudit diff --baseline baseline.json --log tcglog.ib [--esp-root <path>]
```

## Validation status

- Host build, unit, installer, six-detector, and generated-fixture gates pass.
- Patched nested OVMF reaches the empty-PCR positive path.
- VMware provides the expected negative control: its firmware measures PCR0–7
  before `loader.efi`, so replay aborts without extending anything.
- Windows TBS probe and capture pass through the passwordless GuestInfo runner.
- The full WSL2/KVM OVMF+swtpm replay reaches Windows and verifies exact
  SHA-256 PCR0-7 replay plus attributed raw-event divergence.
- The production BootNext fallback runs the staged loader once, returns to
  Windows through BootOrder, clears owned firmware state, and leaves
  `bootmgfw.efi` unchanged.

See [deployment and detection](tools/runbooks/deployment-detection.md), the
[OVMF E2E procedure](tools/runbooks/e2e-ovmf.md), and the detailed
[host-runner guide](tools/host-runner/README.md).
