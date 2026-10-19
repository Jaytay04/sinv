# Storage filter boot recovery

This document covers a Windows boot failure during the guarded Intel storage
filter test. The most relevant observed failure is `INACCESSIBLE_BOOT_DEVICE`
(bug check `0x7B`). A virtual machine reproduced that error with the filter
registered as demand-start, before the driver had loaded. The boot-start variant
passed in the VM, but the physical Intel storage path remains unverified. Treat
any host boot failure as a serious device-stack failure; do not assume its exact
cause from the VM result.

The test runner snapshots the live boot configuration and creates an offline
recovery kit on a separate external drive before changing policy or installing
the filter. A BCD export, source backup, or ESP snapshot is not a Windows system
image and cannot by itself restore a system that cannot boot.

## Preferred recovery

1. Power off the PC. Do not repeatedly attempt normal boot.
2. Boot from the prepared Windows 11 recovery USB.
3. Select **Repair your computer > Troubleshoot > Advanced options > System
   Restore** and restore the point created before the test. If no valid restore
   point exists, use the known-good full system image instead.
4. If BitLocker asks, unlock the Windows volume with the recovery key retrieved
   from its separate safe location. Never put the key in the repository or test
   logs.
5. After Windows starts, reopen elevated PowerShell at the repository root. If
   the interrupted run directory, exact signed release artifacts, and local
   snapshot still exist, reconcile and restore policy in this order:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\storage-host-test.ps1 RecoverOriginal
   powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\storage-host-test.ps1 RestorePolicy
   ```

   Continue only if both commands pass. Reboot Windows. If Secure Boot was
   enabled in the `Prepare` snapshot, restore it in firmware, boot Windows, and
   reopen elevated PowerShell at the repository root. Then run:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\storage-host-test.ps1 FinalVerify
   ```

   If the exact run state, release artifacts, or snapshot are missing after
   System Restore, treat the known-good restore as the recovery endpoint; the
   external offline kit alone cannot resume these automated checks. If any
   check fails, do not reinstall the filter.

The System Restore interface can be opened from Windows Recovery Environment's
**Troubleshoot > Advanced options > System Restore**. Follow Microsoft's
[System Restore guidance](https://support.microsoft.com/en-us/windows/experience/backup-recovery/system-restore)
and [Windows recovery options](https://support.microsoft.com/en-us/windows/experience/backup-recovery/recovery-options-in-windows).

## Offline driver removal

Use this only when System Restore or a full image is unavailable and the
external recovery kit was successfully created by the `Prepare` action. Prefer
the kit's offline-removal script over manual registry edits. It scans the
offline Windows `oem*.inf` files and removes only a file whose contents are
byte-identical to one of the two release INFs copied into that run's kit.

1. Boot the Windows 11 recovery USB and open **Troubleshoot > Advanced options
   > Command Prompt**.
2. Connect the external recovery drive. Drive letters may differ in WinRE. Find
   the recovery directory printed by `Prepare`, under its configured recovery
   root (default `E:\CodexBackup\Recovery\hsinv-storage-test`). The kit is
   stored at
   `E:\CodexBackup\Recovery\hsinv-storage-test\storage-host-recovery-<run-id>\recovery-kit\`.
   Confirm that it contains this run's `README.txt`, both package INFs, and
   `offline-recover.cmd` before proceeding.
3. Identify the Windows volume; do not assume it is `C:`. For example:

   ```text
   dir D:\Windows\System32\config\SYSTEM
   ```

   If BitLocker is enabled, inspect its state with `manage-bde -status D:` and
   unlock it using the recovery key you retrieved separately. Substitute the
   verified Windows volume letter in the command below.
4. Run the kit script, passing the Windows volume letter with a trailing colon:

   ```text
   E:\CodexBackup\Recovery\hsinv-storage-test\storage-host-recovery-<run-id>\recovery-kit\offline-recover.cmd D:
   ```

   Replace `<run-id>` and `D:` with the actual values. Do not type the angle
   brackets. Review the printed package names and result. The script compares
   every offline `oem*.inf` byte-for-byte with the two INFs in this run's
   recovery kit, asks you to type `REMOVE`, then asks DISM to remove only exact
   matches. The matching package number is discovered from the offline image;
   do not guess it from another run.
5. If the script reports an unexpected result, no exact INF match, missing DISM,
   or any other error, stop and use System Restore or the full image. Do not
   substitute an `oemNN.inf` number based on a previous run: Windows assigns
   published names dynamically.
6. Restart. If Windows boots after offline removal or System Restore, return to
   the repository root and reconcile the interrupted run:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\storage-host-test.ps1 RecoverOriginal
   ```

   This action is recovery-only. It checks that the native and WMI serials are
   original, the service and service registry entry are absent, the selected
   disk stack has no filter, file I/O passes, and neither release INF hash is
   present in `Windows\INF`. Its report labels the test outcome
   `ABORTED_ORIGINAL_STATE_RECONCILED`; it does not turn an interrupted test
   into a pass. Continue only when the command reports reconciliation success.
7. Restore test-signing and any certificate trust entries that the run added:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\storage-host-test.ps1 RestorePolicy
   ```

   If that action succeeds, reboot Windows. If Secure Boot was enabled in the
   `Prepare` snapshot, restore it in firmware after the reboot and start Windows
   again. Then run:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\storage-host-test.ps1 FinalVerify
   ```

   `FinalVerify` confirms the captured original values and policy, with both
   release packages removed. If the exact run directory, signed release
   artifacts, or snapshot required by the commands is missing after a full
   image restore, stop. The external offline kit alone does not contain the
   runner state or signed SYS/CAT files needed to resume these automated checks;
   retain or restore those exact artifacts, or use the restored known-good
   system image as the recovery endpoint. Restore the pre-test Secure Boot
   setting in firmware if the test changed it.

Offline servicing follows Microsoft's
[DISM driver servicing guidance](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/dism-driver-servicing-command-line-options-s14?view=windows-11):
offline removal names the published driver package, such as `oem42.inf`, and
boot-critical driver removal can prevent startup. The recovery kit must be
reviewed against the actual implementation before use; its offline path has not
been validated in WinRE on the physical PC.

## Stop conditions

- If the PC still cannot boot after removing the exact test package, restore the
  known-good system image. Do not keep changing driver start types or delete
  unrelated packages.
- Never remove the in-box NVMe, disk, partition manager, storage controller,
  or any package not byte-matched by this test's two captured release INFs.
- Do not delete Secure Boot keys, change SATA/VMD/CSM/TPM settings, or import a
  whole BCD export as a first recovery action. Restore firmware settings to the
  values recorded by `Prepare`.
- If the offline script finds no byte-identical release INF or reports an
  error, stop and use System Restore or the full image. Do not guess the package
  name.

The offline recovery path has not been validated in WinRE on the physical PC.
A successful DISM removal is not proof that the system has returned to its
pre-test policy and trust state; `RecoverOriginal` supplies that live check when
the local run and exact release artifacts remain available. After a boot
failure, prefer System Restore or full image restoration and do not continue the
identifier test from the interrupted run. Retain the run snapshot and
error/evidence files. Report the bug-check code, exact command or phase that
preceded it, whether the offline script found and removed an exact INF match,
and whether image restoration was needed. Do not describe the interrupted test
as clean.
