# efi_inventory — bare-metal run steps (this PC)

Exact, ordered, paste-ready commands to inventory the EFI variables + SMBIOS on the
physical machine. Everything runs from the repo root (`.`).

Target machine (already verified via scan-windows): MAINGEAR VYBE, AMI 1.70 BIOS,
MSI PRO B660M-A (MS-7D37), 2x16 GB Corsair — 61 SMBIOS fields.

## What each file is

- `.codex-build\efi_inventory\efi_inventory.exe` — host scanner used by Path A and Path B.
- `.codex-build\efi_inventory\efi\inventory.efi` — unsigned freestanding EFI variable collector.
- `tools\efi_inventory\bare-metal-collector.ps1` — legacy Path A USB ARM/COLLECT script.
- `tools\efi_inventory\path-b-collector.ps1` — Path B build, stage, status, arm, collect, and disarm workflow; see [EFI_INVENTORY_PATH_B.md](EFI_INVENTORY_PATH_B.md).

Two ways to reach firmware variables; pick one.
- **PATH A:** boot the collector directly from a USB; the installed ESP is untouched.
- **PATH B:** use the dedicated collector-only loader and one-time firmware entry described in [EFI_INVENTORY_PATH_B.md](EFI_INVENTORY_PATH_B.md).

---

## Shared precaution

Before changing firmware settings or the boot path, have the BitLocker/device-encryption recovery key available. For Path B build and staging steps, use [the dedicated Path B guide](EFI_INVENTORY_PATH_B.md). Path A collector artifacts can be built with the command below.

### Path A build (USB collector)

Build (rerun to refresh artifacts). x64 MSVC env is pulled in inline:

```powershell
$vcbat = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
cmd /c "`"$vcbat`" x64 && cmake -S tools\efi_inventory -B .codex-build\efi_inventory -G Ninja -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe -DCMAKE_BUILD_TYPE=Release && cmake --build .codex-build\efi_inventory && ctest --test-dir .codex-build\efi_inventory --output-on-failure"
```

   → both artifacts exist under `.codex-build\efi_inventory\`; tests 2/2.

2) Host-side smoke (optional, no reboot, no elevation — proves the exe runs here):

```powershell
& .codex-build\efi_inventory\efi_inventory.exe scan-windows .codex-build\efi_inventory\host-smbios.txt
```

   → `EFI variables: 0` + the 61 SMBIOS fields.

---

## PATH A — primary: USB one-time UEFI boot (loader not involved)

A spare USB thumb drive, formatted FAT32. Path A does not modify the installed ESP or boot entries. Its EFI collector files are unsigned, so temporarily disable Secure Boot in firmware for that boot, then re-enable it afterward.

```powershell
# (a) If E: is not yet FAT32 (fresh/4G+ sticks are often exFAT), format it:
Format-Volume -DriveLetter E -FileSystem FAT32 -FormatScope Full -Confirm:$false

# (b) ARM — copies inventory.efi -> E:\EFI\BOOT\BOOTX64.EFI, SHA256-checks, stashes old report.
powershell -ExecutionPolicy Bypass -File .\tools\efi_inventory\bare-metal-collector.ps1 Arm E

    # prints: ARMED E:\EFI\BOOT\BOOTX64.EFI

# (c) HARDWARE — reboot, then at POST open the one-time boot menu (ESC / F8 / F12 on AMI),
#     select the USB in UEFI mode, Secure Boot = Disabled for THIS ONE boot only (payload unsigned).

    #   Expected console line:
    #       EFI variable inventory: report ready.
    #   Report written to:  E:\HypersinvEfiInventory.txt   (then exit -> back into Windows)

# (d) COLLECT — elevated PowerShell, repo root. Parses ESP report + host SMBIOS -> one merged file.
powershell -ExecutionPolicy Bypass -File .\tools\efi_inventory\bare-metal-collector.ps1 Collect E

    # -> writes a fresh .codex-build\efi_inventory\baremetal-full.txt (never overwrites an old one)
    #    and prints its head
```

Replace `E` with the actual USB letter everywhere.

---

## PATH B — signed shim/MOK production-loader diagnostic

Path B now tests the signed shim and the actual production loader with Secure Boot enabled. The loader recognizes a dedicated diagnostic directory, runs only the signed inventory collector, and hands off to Windows. Follow the complete [Path B build, sign, stage, arm, MOK enrollment, collection, and recovery guide](EFI_INVENTORY_PATH_B.md).

This tests the loader authentication chain intended for the hypervisor; it does not execute the hypervisor or validate VMX. The earlier direct unsigned inventory-loader instructions are archived in [EFI_INVENTORY_PATH_B_UNSIGNED.md](EFI_INVENTORY_PATH_B_UNSIGNED.md). Keep the BitLocker recovery key available before the test.

## Output + scope

`baremetal-full.txt` contains, for every visible variable: name, vendor GUID,
raw attributes, data size, read status (candidates heuristic-flagged only),
plus the full SMBIOS block. Report is a fresh file each run; existing files are never overwritten.

Scope: pre-boot firmware variables readable by the collector on THIS box — what
`scan-windows` alone cannot reach. A boot-service-only variable absent here still exists.

Related: `EFI_INVENTORY_TOOL_USAGE.md` (VM/QEMU + host commands),
`tools\efi_inventory\README.md`, `BUILD_BOOT_TEST.md` (loader/boot docs).