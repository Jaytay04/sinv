# Storage filter bare-metal test runbook

## Scope and limits

This runbook tests only the Intel NVMe disk serial filter on the captured host.
The test has two phases: a transparent boot-start pass-through check, then the
locked serial replacement check. It does not test NICs, SMBIOS, CPU/GPU values,
or all possible native storage readers.

The VM evidence is useful but does not establish physical-controller safety. A
Windows 11 25H2 VM running build 26200.9445 reproduced `INACCESSIBLE_BOOT_DEVICE`
(0x7B) with the transparent filter configured as demand-start (`StartType=3`).
The transparent boot-start (`StartType=0`) package booted in the VM, and its
enabled replacement package passed native `StorageDeviceProperty` IOCTL and fresh
`Win32_DiskDrive` read-back. The physical host runs build 26200.9711 and uses an
Intel NVMe device. Its controller path has not been tested with the filter.

The host values are pinned by the runner:

| Field | Captured original | Test replacement |
| --- | --- | --- |
| Disk serial | `0000_0000_0100_0000_E4D2_5CDF_3227_5501.` | `1111_1111_1111_1111_A1B2_C3D4_5566_7788.` |

The runner also pins the captured disk PnP instance and hardware IDs. If the
current disk does not match those values, stop; do not force-install the INF.

## Before changing boot policy

Have a bootable Windows 11 recovery USB and a known-good system restore point or
full system image. Confirm that the recovery disk is separate from the Windows
boot disk. In the current host layout, `E:` is the external exFAT backup disk;
the runner checks disk separation and stops if that assumption is false.

If BitLocker/device encryption is enabled, confirm that the recovery key is
available outside this PC. The runner records protection status only; it does
not collect or save recovery keys. Keep the recovery drive connected during the
test so the generated recovery kit and run snapshot remain available after a
boot failure.

The existing release package is signed with the repository's existing local
test certificate. The procedure may add that public certificate to the local
Root and Trusted Publishers stores and enable Windows test-signing policy. It
does not create a key or enroll a new MOK certificate. Secure Boot can prevent
test-signed driver loading, so if the snapshot reports Secure Boot enabled, turn
it off in firmware for this test and restore its recorded original setting only
after the filter has been removed and the normal boot has succeeded.

For the MSI PRO B660M-A firmware interface, the documented path is: press
Delete during startup, press F7 for Advanced Mode, then open **Settings >
Security > Secure Boot** and set **Secure Boot** to **Disabled**. Save with F10.
Menu names can vary by BIOS version. Do not delete Secure Boot keys or change
SATA/VMD, CSM, or TPM settings. No separate firmware test-signing option is
required by this workflow.

## Run the test

Open **elevated PowerShell** at the repository root. Do not run the quarantined
`device-reboot-cycle.ps1`; it is unrelated to this guarded workflow.

First create and verify the snapshot and external recovery kit. This action does
not install a driver or change boot policy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\storage-host-test.ps1 Prepare
```

Read the printed run directory and snapshot summary. Stop if the runner reports
an unexpected boot disk, a missing external recovery disk, a disk identity
mismatch, unavailable recovery media, or a failed snapshot. `Prepare` defaults
to `E:\CodexBackup\Recovery\hsinv-storage-test`; use its documented
`-RecoveryDirectory` override only when the external recovery drive has a
different verified letter.

If Secure Boot was enabled, perform the firmware step above, boot Windows, then
continue. Run each action in elevated PowerShell at the repository root and
wait for its success result before continuing. After every reboot, reopen an
elevated PowerShell window at the repository root. Reboot manually when
instructed; the runner does not reboot automatically.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\storage-host-test.ps1 EnableTestMode
```

Reboot Windows. Then run the transparent test, which loads the filter with
serial rewriting disabled:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\storage-host-test.ps1 InstallTransparent
```

Reboot Windows, then verify the filter is running in the actual disk device
stack, native and WMI reads still return the original serial, and file I/O
checks pass:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\storage-host-test.ps1 VerifyTransparent
```

Remove the pass-through filter and reboot before starting the replacement phase:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\storage-host-test.ps1 Remove
```

After reboot, confirm the original value and that the filter service and stack
entry are absent:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\storage-host-test.ps1 VerifyOriginal
```

Install the replacement package only after that clean baseline passes:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\storage-host-test.ps1 InstallReplacement
```

Reboot Windows, then verify the exact replacement through the native storage
IOCTL and a fresh WMI query, and run the file I/O check:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\storage-host-test.ps1 VerifyReplacement
```

Remove the replacement filter and reboot:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\storage-host-test.ps1 Remove
```

Verify the captured original is restored before restoring boot policy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\storage-host-test.ps1 VerifyOriginal
powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\storage-host-test.ps1 RestorePolicy
```

Reboot. If Secure Boot was enabled before the test, restore it through the same
MSI menu and boot Windows. Complete the final check:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\storage-host-test.ps1 FinalVerify
```

The run directory and evidence paths are printed by the runner. Keep them with
the recovery drive. A passing test proves only this pinned disk on this host,
for the two tested read paths and the recorded file I/O checks. It does not
prove NIC behavior or every storage interface.

If the release artifacts are absent, build them from the repository root, then
rerun `Prepare`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\build-storage-host-release.ps1
```

The build uses the existing signing private key available to the current user.
If the command reports that the key is unavailable, stop; do not create or
import a replacement key. No private key or certificate password belongs in
the repository or a report.

## If a command fails

Do not continue forward after any verification failure. Save the exact error
and run `Status` if Windows starts. If the runner reports an installed or
install-pending package, use `Remove` to request removal, reboot, then use
`VerifyOriginal`. If native/WMI or stack verification still fails, run
`RecoverOriginal`; proceed to `RestorePolicy` only after its recovery-only
read-back passes. Reboot, restore Secure Boot in firmware if it was enabled in
the `Prepare` snapshot, and run `FinalVerify`. A failed test recovered this
way is reported as aborted; it is not a passing replacement test.

If Windows cannot boot, use
[STORAGE_BOOT_RECOVERY.md](STORAGE_BOOT_RECOVERY.md). The runner cannot perform
in-Windows cleanup while the OS is stopped at a boot failure.
