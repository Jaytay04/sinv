# Combined identifier readback runbook

## Scope and current readiness

This workflow is limited to the seven locked SMBIOS values, `PHYSICALDRIVE0`,
and the four NIC rows in `reports/client-originals.tsv`. USB disks are excluded.
It requires multiple user-controlled reboots and a manual firmware Secure Boot
setting change. No action in these scripts reboots the computer or writes the
Secure Boot firmware variable.

The combined VM gate is a hard prerequisite for every host-mutating action.
The current guest rejects SMBIOS preparation: its SMBIOS2 entrypoint declares
a maximum structure size of 111 bytes, while the fully parsed table contains a
112-byte structure. The payload returns `0x8000000000000021` before publication.
The guard is preserved. A conforming VM firmware fixture and a complete
active/restored combined VM gate are required before host activation. Therefore
the host sequence below is not ready to run. See
`reports/review-20261017/combined-nine-field-progress-20261018.md` for the actual
diagnostic and recovery evidence. The gate must be an authenticated, hash-pinned full-matrix
PASS for the current source, not a status flag. `Prepare` without a valid gate
is read-only and records `activationAvailable: false`; all later lifecycle
actions reject that journal. A gate must be rebuilt after any pinned source
changes. Do not start host mutation until `Prepare` reports
`PREPARED_READONLY_GATE_PASS` and `activationAvailable: true`. Do not use
placeholder gate, reader, or release paths.

The stages remain explicit so each install, removal, and policy change is
verified after its own reboot. This runbook does not claim that Windows can be
recovered automatically if it becomes unbootable; keep the external recovery
image and the existing procedure in `docs/STORAGE_BOOT_RECOVERY.md` available.

Before any storage-policy or ESP change, `Prepare` builds a fresh, unique
host-values SMBIOS-seven payload under `spoof-handoff/artifacts/chp/<run-id>`.
The builder checks the seven originals and replacements against the locked TSV,
requires the VM-only values profile to be OFF, verifies disk/NIC/CPU/GPU
substitution remains disabled, and requires both SMBIOS CTests to pass. It
hash-pins the resulting EFI image and all source inputs. `ArmCombined` rechecks
that verdict and its source hashes before copying that exact image into the
Path-B inventory directory; it does not consume the older
`smbios-seven-probe/build-verdict.json` artifact.

The Path-B vehicle is the dedicated collector-only `inventory_loader.efi` plus
the one-shot installer binary pinned by the VM gate. It is not the production
VMX loader and is not the enforcing signed-shim profile. This run uses Secure
Boot disabled and Windows test-signing enabled for the boot-start storage
filter. The host-values collector is freshly built and hash-pinned; no new
signing key, certificate enrollment, or EFI signing is performed by this
workflow. The Path-B script first builds locally, then replaces the vehicle
files with the exact VM-gate-pinned bytes while retaining hashes of its local
build outputs.

## Prerequisites

- Elevated Windows PowerShell, started at the repository root.
- Secure Boot enabled for the initial snapshot and original-value capture.
- A completed, current combined VM gate and the pinned storage release package.
- A fresh SMBIOS Windows readback executable built with machine-identity
  metadata, and the NIC readback executables referenced by the host tools.
- The external recovery destination used by the storage-host workflow.
- Access to the firmware setup to disable and later re-enable Secure Boot.

Do not use a stale reader binary. `Prepare` verifies fresh native/WMI originals,
Secure Boot, the supported BCD views/export, all reachable ESP file hashes,
firmware records, and the Secure Boot variable bytes before any host phase.
`BOOTSTAT.DAT` is recorded as mutable Windows boot status; live BCD bytes are
not claimed as raw-hashed. The final check compares supported `bcdedit`
enumerations and the BCD export semantics.

## Commands

Run the validated preparation only after the VM gate and release are available:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\combined-host-test.ps1 Prepare -VmGatePath <absolute-path-to-combined-vm-gate.json> -SmbiosReaderPath <absolute-path-to-fresh-smbios_windows_readback.exe> -ReleaseDirectory <absolute-path-to-pinned-storage-release>
```

Preserve the returned run directory. For each command below, replace
`<run-directory>` with that exact path. Commands fail closed if the journal phase
or the verified VM/source hashes do not match.

1. In firmware, disable Secure Boot, save, and boot Windows. On this MSI board,
   enter setup with **Delete**, switch to **Advanced Mode (F7)**, then use
   **Settings > Security > Secure Boot > Disabled**. Firmware labels can vary by
   BIOS revision; verify Secure Boot reports off after Windows starts.
2. Enable the existing test-signing policy and establish the external recovery
   state:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\combined-host-test.ps1 EnableTestMode -RunDirectory <run-directory>
   ```

   Reboot Windows.
3. Install and verify the transparent control package, then remove it and verify
   the original disk state after the next reboot:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\combined-host-test.ps1 InstallTransparent -RunDirectory <run-directory>
   ```

   Reboot, then verify. Remove the control package, reboot, and verify originals:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\combined-host-test.ps1 VerifyTransparent -RunDirectory <run-directory>
   powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\combined-host-test.ps1 Remove -RunDirectory <run-directory>
   ```

   Reboot Windows, then run:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\combined-host-test.ps1 VerifyOriginal -RunDirectory <run-directory>
   ```
4. Install the replacement storage package and verify it after reboot:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\combined-host-test.ps1 InstallReplacement -RunDirectory <run-directory>
   ```

   Reboot, then run:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\combined-host-test.ps1 VerifyReplacement -RunDirectory <run-directory>
   ```
5. Stage and arm the dedicated Path-B collector and SMBIOS-seven payload. This
   sets a one-shot boot entry; it does not reboot:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\combined-host-test.ps1 ArmCombined -RunDirectory <run-directory>
   ```

   Reboot Windows once and run:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\combined-host-test.ps1 CollectCombined -RunDirectory <run-directory>
   ```

   This verifies the boot-time SMBIOS report, performs the locked NIC transaction
   with a durable restore journal, and records the simultaneous readback. The
   NIC values are restored even if the readback fails; preserve the exact error
   and journal.
6. Remove the replacement storage package, reboot, and verify originals. Restore
   test-signing and remove only the trust additions owned by this run:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\combined-host-test.ps1 Remove -RunDirectory <run-directory>
   ```

   Reboot, verify originals, and restore test-signing/trust:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\combined-host-test.ps1 VerifyOriginal -RunDirectory <run-directory>
   powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\combined-host-test.ps1 RestorePolicy -RunDirectory <run-directory>
   ```
7. Reboot with Secure Boot still disabled so test-signing-off takes effect. In
   firmware, re-enable Secure Boot at **Settings > Security > Secure Boot >
   Enabled**, save, and boot Windows. Run the final proof:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\combined-host-test.ps1 FinalVerify -RunDirectory <run-directory>
   ```

`FinalVerify` requires Secure Boot on, test-signing off, the storage package
removed, originals visible through the configured reader matrix, unchanged
supported BCD views, restored ESP file hashes (with the documented mutable
exceptions), and unchanged firmware records. It emits evidence under
`spoof-handoff/reports/combined-host-20261018/`.

Use `Status -RunDirectory <run-directory>` to inspect the current recorded
phase. Do not skip or repeat a transition after an error; use recovery first.

## Recovery

When Windows still boots, invoke the journaled recovery command from the
repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\combined-host-rollback.ps1 -RunDirectory <run-directory>
```

The recovery script restores any interrupted NIC transaction, disarms only its
owned Path-B one-shot state, restores the exact pre-run owned Path-B files, and
uses the existing storage-host state machine to remove the owned package and
restore policy. It reports any required reboot or firmware action and never
claims success while restoration is pending. If Windows cannot boot, use the
verified external full-image recovery procedure and the exact recovery material
recorded by storage-host `Prepare`; this script cannot run in an unbootable OS.

Preserve the run journal, action stdout/stderr, storage-host journal, NIC
transaction journal, Path-B receipts, and the full-image recovery directory.
Do not delete evidence or retry an interrupted mutation until recovery reports
the corresponding state restored.
