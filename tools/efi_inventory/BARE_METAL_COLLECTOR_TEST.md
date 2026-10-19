# User-directed bare-metal EFI collector test

This is a self-directed procedure for the owner of the physical machine. It
is an exception to this repository's VM-only testing guidance in `AGENTS.md`;
project automation must not run the experimental EFI application or a driver
on a primary host. The collector was tested in QEMU/OVMF, **not** on this
machine's firmware. Have verified backups and the BitLocker recovery key
available before changing the boot path.

You do **not** need to image an operating system onto a USB or install a UEFI
shell. A dedicated FAT32 USB containing `\EFI\BOOT\BOOTX64.EFI` is a UEFI
removable-media boot target. The collector runs when that USB is selected from
the one-time boot menu. Do not modify the internal EFI System Partition.

## 1. Build and run host tests

Open a Visual Studio x64 Developer PowerShell at the repository root:

```powershell
$build = '.codex-build\efi_inventory_baremetal'
cmake -S tools/efi_inventory -B $build -G Ninja -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe -DCMAKE_BUILD_TYPE=Release
cmake --build $build
cmake --build $build --target efi_inventory_collector
ctest --test-dir $build --output-on-failure
```

The EFI application is `$build\efi\efi_inventory_collector.efi`. Stop if the
build or tests fail. Use a fresh build directory if CMake reports a generator
mismatch; do not delete an existing build directory just to change generators.

## 2. Check Secure Boot

In an elevated PowerShell:

```powershell
Confirm-SecureBootUEFI
```

The current collector build is unsigned. If this returns `True`, stop: the
firmware is expected to reject the application. This procedure does not
disable Secure Boot or change firmware keys. If the command errors, resolve
the platform/privilege issue before proceeding rather than assuming Secure
Boot is off.

## 3. Prepare a dedicated FAT32 USB

Insert the USB and replace `E` below with its **verified** drive letter. These
commands refuse to replace an existing boot file or report:

```powershell
$letter = 'E'
$usb = "${letter}:\"
if ((Get-Volume -DriveLetter $letter).FileSystem -ne 'FAT32') {
    throw 'Use a dedicated FAT32 USB drive.'
}

$target = Join-Path $usb 'EFI\BOOT\BOOTX64.EFI'
$report = Join-Path $usb 'HypersinvEfiInventory.txt'
if ((Test-Path -LiteralPath $target) -or (Test-Path -LiteralPath $report)) {
    throw 'Boot file or report already exists; use a fresh USB.'
}

New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
Copy-Item -LiteralPath "$build\efi\efi_inventory_collector.efi" -Destination $target
if ((Get-FileHash "$build\efi\efi_inventory_collector.efi").Hash -ne
    (Get-FileHash $target).Hash) { throw 'USB copy failed verification.' }
```

If the USB is not FAT32, back up its contents before formatting **that
verified USB only**. The copy operation above is what places the application
at the UEFI removable boot path; a separate bootable image is unnecessary.

## 4. Run the collector

Reboot and select **UEFI: your USB device** from the firmware's one-time boot
menu. There is no shell command to type: `BOOTX64.EFI` is the collector. On
success it prints `EFI variable inventory written.` and creates
`\HypersinvEfiInventory.txt` at the USB root. It reads variable data briefly
to determine metadata, but records only names, GUIDs, attributes, sizes, and
status; it does not call `SetVariable` or save variable payloads.

The application refuses to overwrite an existing report. If it prints a
failure message, retain the USB for inspection; an incomplete report lacks
the final `END` footer. Remove the USB before the next normal boot so the
collector is not launched again.

## 5. Validate and save the results in Windows

From the repository root, replace `E` with the USB's current drive letter:

```powershell
$letter = 'E'
$raw = "${letter}:\HypersinvEfiInventory.txt"
if ((Get-Content -LiteralPath $raw -TotalCount 1) -ne 'HVEFI001' -or
    (Get-Content -LiteralPath $raw -Tail 1) -notmatch '^END\t[0-9A-F]{8}$') {
    throw 'Report missing or incomplete; do not treat it as a valid inventory.'
}

$build = '.codex-build\efi_inventory_baremetal'
$capture = Join-Path $env:LOCALAPPDATA (
    'Hypersinv\EfiInventory\captures\{0}' -f (Get-Date -Format yyyyMMdd-HHmmss))
New-Item -ItemType Directory -Path $capture | Out-Null
$savedRaw = Join-Path $capture 'HypersinvEfiInventory.txt'
Copy-Item -LiteralPath $raw -Destination $savedRaw
& "$build\efi_inventory.exe" scan $savedRaw (Join-Path $capture 'host-inventory.txt')
```

The Windows `scan` command validates the collector report and adds SMBIOS
from the machine on which it runs. Use it here only because the EFI capture
and Windows SMBIOS are from the **same physical machine**. Keep both files
private; they can identify the device. A successful VM test does not guarantee
that the application will behave correctly on every physical firmware.

## 6. Recover if the test goes wrong

These steps apply to a Windows system image made with `wbadmin start backup
-allCritical` on a **separate external drive**. They do not apply to the
collector's metadata report or its DPAPI-protected EFI-variable snapshot;
this tool has no firmware-variable restore command. A Windows disk image also
cannot repair firmware or restore motherboard NVRAM.

Before testing, while Windows still works, attach the backup drive and use an
elevated Command Prompt to confirm the image is discoverable. Substitute its
verified drive letter for `X:`:

```text
wbadmin get versions -backupTarget:X:
wbadmin get items -version:MM/DD/YYYY-HH:MM -backupTarget:X:
```

Use the version identifier printed by the first command in the second. Record
the backup date, identifier, and drive label separately, and confirm that the
listed items include the Windows volume and boot-critical partitions. Keep a
**separate** Windows recovery/installation USB available; do not put it on the
collector USB or backup drive. Merely seeing a backup listed does not prove a
restore will succeed.

1. If the collector hangs or the one-time USB boot fails, remove the collector
   USB and try a normal boot. Do not keep rerunning the collector or clear the
   TPM/reset firmware settings. If Windows boots normally, no disk restore is
   needed; keep any incomplete report for diagnosis.
2. If Windows does not boot, enter the Windows Recovery Environment (WinRE)
   using the recovery/installation USB or the machine's recovery menu. Try
   **Troubleshoot > Advanced options > Startup Repair** first for a boot-file
   problem. If the firmware cannot detect the internal disk or reach WinRE,
   stop and use the computer manufacturer's recovery process: the Windows
   image cannot fix firmware hardware/NVRAM failures.
3. If Windows still cannot boot and restoring the image is necessary, attach
   the external **backup drive**, enter **Troubleshoot > Advanced options >
   System Image Recovery**, and select the recorded image by machine and
   timestamp. Drive letters may differ in WinRE. Inspect every destination
   and partition choice before confirming; disconnect unrelated external
   storage if practical. Do not choose any repartition/format option unless
   you have verified exactly which physical disk it will erase.
4. **Restoring replaces the selected Windows/critical volumes with their
   backup-time state. Changes made since the backup will be lost.** If the
   selected image, source drive, or target disk is uncertain, cancel and
   verify them rather than proceeding. After a successful restore, remove
   recovery and collector media before normal boot.

Do not use `Reset this PC` as a substitute for the system-image restore if
preserving your installed applications and backup-time state is the goal.
See [Microsoft's WinRE recovery tools](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/windows-re-troubleshooting-features?view=windows-11),
[Startup Repair](https://support.microsoft.com/en-us/windows/experience/startup-boot/startup-repair),
and the [`wbadmin` backup-listing commands](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/wbadmin-get-versions).

References: [UEFI removable-media boot behavior](https://uefi.org/specs/UEFI/2.11/03_Boot_Manager.html),
[Microsoft Secure Boot requirements](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/oem-secure-boot),
[Microsoft BitLocker boot-order caution](https://learn.microsoft.com/en-us/windows/security/operating-system-security/data-protection/bitlocker/faq).
