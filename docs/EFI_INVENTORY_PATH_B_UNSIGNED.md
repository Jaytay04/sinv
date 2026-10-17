# Path B — collector-only EFI inventory through a one-time firmware entry

This guide stages a dedicated inventory loader and collector on the Windows EFI System Partition (ESP), arms a one-time firmware boot entry, collects the report after Windows starts, and clears the owned firmware entry. It is for the explicitly authorized **collector-only** physical-host test. It does not authorize running the project's experimental hypervisor or generic payload on a primary PC.

The dedicated inventory_loader.efi loads only EFI\HypersinvInventory\inventory.efi, then starts Windows Boot Manager at EFI\Microsoft\Boot\bootmgfw.efi. Its entry path does not run TPM replay, load payload.efi, execute VMX, restore or replace Boot Manager, or write EFI variables. If the collector returns an error, the loader still attempts the Windows handoff; the firmware path has no timeout if the collector hangs. The Windows arming tool creates an owned inventory one-shot entry. BootOrder is left alone; BootNext is consumed on the next boot. The Boot#### entry and staged files remain until explicit cleanup.

All EFI files produced by this workflow are unsigned. Secure Boot firmware verifies EFI application signatures, so disable it manually before arming and re-enable it after collecting or abandoning the test ([Microsoft Secure Boot documentation](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/oem-secure-boot)). Have the BitLocker/device-encryption recovery key available before changing firmware or the boot path. Windows certificate stores and test-signing settings do not authorize unsigned EFI images.

## Why this workflow disables Secure Boot

An EFI loader is itself subject to firmware image authentication. The staged loader.efi is the dedicated inventory_loader.efi build; both it and inventory.efi have no embedded signature. This loader uses the normal firmware LoadImage service and does not implement a Secure Boot bypass. The Stage workflow did not enroll firmware trust for either image, and Arm deliberately requires Secure Boot to be off.

The original component documentation describes a separate signed shim/MOK chain and VM-only measured-boot research. Measured-boot replay is different from Secure Boot image authentication. A signed shim chain must establish valid trust for its downstream images; merely having a loader or signing with a Windows-local test certificate does not establish firmware trust.

Keeping Secure Boot enabled would require a separately validated trust workflow, such as signing both EFI images with a key whose certificate is accepted by the firmware, and updating the Arm policy for that workflow. That involves additional key management and firmware trust configuration. The current Path B procedure uses temporary Secure Boot disablement instead.

## Build

Run from the repository root in PowerShell. The host needs x64 Visual Studio C++ build tools, CMake 3.24 or newer, and Ninja; the script discovers the installed tools. Build does not mount the ESP or change firmware state; it regenerates Release configurations in separate directories, enforces the loader component's static host runtime setting, and runs the host tests. Host tests do not verify an actual firmware boot.

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\efi_inventory\path-b-collector.ps1 -Mode Build
~~~

The default output directories are:

| Artifact | Path |
| --- | --- |
| Dedicated inventory loader and installer | .codex-build\efi_inventory_loader\inventory_loader.efi and .codex-build\efi_inventory_loader\ib-install.exe |
| EFI variable collector and host scanner | .codex-build\efi_inventory\efi\inventory.efi and .codex-build\efi_inventory\efi_inventory.exe |
| Host test output | CTest runs in each component build directory |

Build can be repeated to refresh both artifacts. Optional -LoaderBuildDir, -InventoryBuildDir, and -MountLetter parameters are available; use the same parameter values for later modes if you override defaults.

## Stage and verify

Open **elevated PowerShell** at the repository root. The default temporary mount letter is S:; make sure it is unused. Stage mounts the system GPT EFI System Partition temporarily, copies only the dedicated loader, collector, and hash manifest into EFI\HypersinvInventory\, verifies staged hashes and the existing Windows Boot Manager hash, writes a local stage receipt, and unmounts the ESP. Stage does not set BootNext, modify Secure Boot, change BCD or certificate stores, or reboot. Previously staged files are accepted only if their manifest, hashes, and contents match the owned inventory stage; valid files are archived under .codex-build\efi_inventory\path-b-backup.<UTC>.<guid> before replacement. The stage receipt is .codex-build\efi_inventory\path-b-stage.json.

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\efi_inventory\path-b-collector.ps1 -Mode Stage
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\efi_inventory\path-b-collector.ps1 -Mode Status
~~~

Status verifies the staged files against the current build and checks the Boot Manager hash. It does not prove that the loader will boot, or report whether a one-time entry is armed. For a non-firmware staging check, run Stage or Status with -TestRoot <existing-local-non-root-directory>; that test mode does not mount the ESP or change firmware state.

## Arm and perform the one-time boot

1. In firmware setup, temporarily disable Secure Boot and save the setting. Return to Windows. The images are unsigned, so the firmware must permit unsigned EFI applications.
2. In elevated PowerShell at the repository root, arm the dedicated inventory profile:

   ~~~powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\efi_inventory\path-b-collector.ps1 -Mode Arm
   ~~~

   Arm checks that Secure Boot is disabled, verifies the staged files and ESP, and requests the inventory profile's BootNext entry. It refuses to replace an unrelated existing BootNext. It does not start a reboot.
3. Disconnect the Path A collector USB for this test so firmware does not boot its fallback image and obscure whether the internal ESP entry ran.
4. Reboot when ready. Do not launch loader.efi from Windows or select another one-time boot target that would override BootNext. On the next firmware boot, BootNext starts the dedicated loader, which runs the collector and then hands off to Windows Boot Manager.
5. Expected collector output includes “EFI variable inventory: report ready.” and “inventory collector: PASS”, followed by Windows startup. The report is written at the **ESP root** as \HypersinvEfiInventory.txt (not inside the loader directory). The collector refuses to overwrite an existing report.

If firmware rejects the image, restart and choose the normal Windows Boot Manager entry from firmware's boot menu. A collector hang can prevent the handoff, so restart and select Windows Boot Manager manually if Windows does not start. After Windows returns, run Status and then Disarm if the test will not be retried. Do not reset the TPM or delete/replace Windows Boot Manager files.

## Collect and restore Secure Boot

After Windows has started, run elevated PowerShell from the repository root:

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\efi_inventory\path-b-collector.ps1 -Mode Collect
~~~

Collect validates the report, copies it to a uniquely named capture file under .codex-build\efi_inventory\captures\, and runs the host scanner to create a second uniquely named merged inventory file in that same flat directory. It verifies the copied report hash and scanner output, clears the owned inventory BootNext/Boot#### state, and removes the ESP report only if it is unchanged. It preserves the staged EFI files and local captures. The command prints the exact COLLECTED and SCANNED output paths.

Then re-enable Secure Boot manually in firmware. If you are stopping without a report or after a failed boot, clear the owned firmware entry with elevated PowerShell:

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\efi_inventory\path-b-collector.ps1 -Mode Disarm
~~~

Disarm clears only the inventory profile's owned BootNext/Boot#### entry and preserves staged files and any report. To stage again after collecting, run Stage, then repeat the Secure Boot and Arm steps. Do not run the generic loader installer profile for this workflow.

## Recovery if the test fails

Before testing, have the BitLocker/device-encryption recovery key accessible from another device or a printout if the Windows drive is encrypted. The 48-digit key is separate from a Windows backup; use the key whose ID matches the recovery screen ([Microsoft recovery-key guidance](https://support.microsoft.com/en-us/windows/security/encryption/find-your-bitlocker-recovery-key)). Confirm your Windows installation USB boots to its repair screen without starting an installation. Keep the spare backup drive disconnected during the test, and reconnect it when needed for recovery. Have the restore instructions and any rescue media for the tool that created the backup.

1. If the EFI loader or collector freezes, power off, then open the firmware one-time boot menu. Select **Windows Boot Manager for the original Windows drive**. Do not select Hypersinv EFI Inventory or the Path A USB. UEFI specifies that BootNext is deleted before control passes to its selected image, so a conforming firmware should resume the normal boot order on the following boot ([UEFI boot-manager specification](https://uefi.org/specs/UEFI/2.11/03_Boot_Manager.html)). The inventory workflow leaves BootOrder unchanged.
2. Restore Secure Boot to its original enabled setting in firmware when abandoning the test. Keep the original UEFI/storage settings. Do not clear Secure Boot keys, reset the TPM, or reset all firmware settings as a recovery step.
3. If Windows displays BitLocker recovery, enter the matching 48-digit recovery key. A recovery prompt alone does not establish that Windows files were damaged.
4. Once the original Windows installation starts, open elevated PowerShell at this repository root and clear the inventory profile's owned firmware entry:

   ~~~powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\efi_inventory\path-b-collector.ps1 -Mode Disarm
   ~~~

   Disarm preserves the staged files and any report. It does not require a successful collector report. Run this from the original installation; booting a spare clone can make the system ESP refer to a different disk.
5. If selecting the normal Windows entry still fails, boot the regular Windows installation USB **in UEFI mode**. Choose **Repair your computer / Repair my PC**, then **Troubleshoot > Advanced options > Startup Repair**. Enter the BitLocker key if requested. Use the repair flow before reinstalling or formatting the Windows disk ([Microsoft WinRE instructions](https://support.microsoft.com/en-us/windows/experience/backup-recovery/windows-recovery-environment), [Startup Repair](https://support.microsoft.com/en-us/windows/experience/startup-boot/startup-repair)).
6. If repair cannot recover Windows, reconnect the spare drive and restore with the tool that created the backup. A Windows system-image backup can use WinRE's **System Image Recovery**; other image formats can require their own rescue media. Verify that the restore destination is the original Windows disk, not the drive holding the backup. If the spare drive is a verified bootable clone, it can instead provide a working OS while the original disk is preserved for diagnosis.

The staged Windows Boot Manager hash was verified unchanged. The actual firmware boot remains untested, and the recovery procedure does not guarantee recovery from every possible hardware or firmware failure.

Related: [EFI inventory tool usage](EFI_INVENTORY_TOOL_USAGE.md), [Path A bare-metal steps](EFI_INVENTORY_BAREMETAL_STEPS.md), and [loader boot-test guide](BUILD_BOOT_TEST.md).
