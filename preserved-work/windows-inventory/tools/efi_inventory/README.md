# Hypersinv EFI inventory

This is a standalone, read-only inventory tool. It does **not** alter EFI
variables, SMBIOS tables, boot settings, TPM state, or hardware identifiers.
It does not load the hypervisor or the existing HVR1 driver. Its purpose is to
establish which identifiers and EFI variable names actually exist before any
device-specific change is considered.


## Windows runtime capture

For the current host identifier task, use [the Windows-only capture guide](../../docs/WINDOWS_RUNTIME_INVENTORY.md). capture-windows enumerates only post-boot variables with EFI_VARIABLE_RUNTIME_ACCESS and writes a separate SMBIOS/device coverage report. The standalone build disables the EFI collector and requires no boot changes.

The pre-boot and VM procedures below cover a different, broader inventory scope.

## UEFI loader integration (this repository)

`efi_inventory_collector.efi` (target `efi_inventory_collector`, output name
`inventory.efi`) can also be launched by the project UEFI loader.
`components/uefi-loader/src/loader/efi_entry.c` checks for
`\EFI\insecure-boot\inventory.efi` immediately before the Windows handoff,
loads and starts it when present (bounded, unloaded afterwards), and prints
`inventory collector: PASS` or a non-fatal `FAIL` line; the handoff verdict is
unchanged by either outcome.

Build it with the commands above, then make `efi/inventory.efi` available on
the ESP next to `loader.efi`, e.g. for one armed boot:

```powershell
# after the normal ib-install.exe stage + oneshot --mode bootnext:
mountvol X: /S
Copy-Item "$b\efi\inventory.efi" "X:\EFI\insecure-boot\inventory.efi"
mountvol X: /D
# reboot into the armed boot; afterwards read the report back:
efi_inventory.exe scan-efi "ESP-absolute:\EFI\insecure-boot\HypersinvEfiInventory.txt" out.txt
```

See [LOADER_PAYLOAD_HANDOFF.md](LOADER_PAYLOAD_HANDOFF.md) for the report
layout and the identifier fields the companion host tool emits.

## Architecture

- `efi_inventory_collector.efi` is an explicit, separate EFI application. It
  reuses the project's UEFI support header and freestanding CRT. Before the OS
  boots, it uses `GetNextVariableName` and `GetVariable` to record each visible
  variable's name, vendor GUID, attributes, data length, and read status. It
  never calls `SetVariable` and never writes variable payloads to disk.
- `efi_inventory.exe` reads that metadata file and uses Windows
  `GetSystemFirmwareTable('RSMB')` to read SMBIOS structures. The report keeps
  EFI and SMBIOS provenance separate. Names that *might* denote an identifier
  are marked for manual review, not asserted to be hardware IDs.
- `efi_inventory.exe serve` hosts a local, read-only named-pipe API at
  `\\.\pipe\HypersinvEfiRuntimeCompanion`. It is not the existing HVR1
  `\\.\HypersinvEfiRuntimeCompanion` device. The pipe permits only SYSTEM and
  Administrators, rejects remote clients, and authenticates each request and
  response with HMAC-SHA256 over a server challenge. Its shared secret is
  stored under `%ProgramData%\Hypersinv\EfiInventory` with a restricted ACL.
  Windows access control is the primary security boundary; HMAC does not
  protect against an administrator or SYSTEM compromise.
- `backup` reads only those inventoried EFI variables that Windows can still
  access at runtime and saves their original bytes and attributes in a
  user-scoped DPAPI-protected file with a restrictive file ACL. It never writes
  firmware. A backup is a data snapshot, **not** a promise that firmware will
  accept a future restore (especially for authenticated variables).

Windows' documented firmware API reads a supplied name and GUID, but does not
enumerate all names. The new capture-windows command uses a native Windows
enumeration API for the runtime-visible set. The EFI collector remains the
pre-boot path for broader vendor-variable discovery. Boot-service-only variables may be visible to the
collector yet unavailable to Windows for `backup`.

A VM capture inventories the VM's firmware, **not** the physical host's NVRAM.
This repository's safety boundary forbids running an experimental EFI payload
on a primary host, so a complete physical-host EFI variable list is not
claimed here. `scan-windows` can still inventory the current host's SMBIOS
through a documented, read-only Windows API.
For physical-machine EFI variable inventory, see
[BARE_METAL_INVENTORY.md](BARE_METAL_INVENTORY.md).

## Build

Use an x64 MSVC installation, Ninja, and CMake. Run these commands from an
**x64 Native Tools Command Prompt** (or a shell initialized with `vcvars64.bat`).
This project does not participate in the root ExternalProject superbuild and
requires no WDK:

```powershell
cmake -S tools/efi_inventory -B .codex-build/efi_inventory_clean -G Ninja -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe -DCMAKE_BUILD_TYPE=Release
cmake --build .codex-build/efi_inventory_clean
ctest --test-dir .codex-build/efi_inventory_clean --output-on-failure
```

The EFI binary is a separate explicit target:

```powershell
cmake --build .codex-build/efi_inventory_clean --target efi_inventory_collector
```

The build produces `efi_inventory.exe` in the build directory and
`efi_inventory_collector.efi` in its `efi` subdirectory. The latter is not part
of the default build. Do not launch it on a primary host.

## Disposable QEMU/OVMF test

This procedure was tested on Windows with QEMU 11.1.0 and its x64 OVMF files.
It tests the EFI collector in an **offline, disposable VM**, without starting
the project's hypervisor or a Windows driver. Run from the repository root in
PowerShell after building both targets above. Adjust `$qemu` if installed
elsewhere. Use a **new** `$work` directory for every run; the EFI report is
deliberately not overwritten.

```powershell
$qemu = 'C:\Program Files\qemu'
$work = Join-Path (Get-Location) '.codex-build\efi_inventory_vm_run1'
$fat = Join-Path $work 'fat'
$boot = Join-Path $fat 'EFI\BOOT'
New-Item -ItemType Directory -Path $boot -Force | Out-Null
Copy-Item .codex-build\efi_inventory_clean\efi\efi_inventory_collector.efi (Join-Path $boot 'BOOTX64.EFI')
Copy-Item (Join-Path $qemu 'share\edk2-i386-vars.fd') (Join-Path $work 'vars.fd')
$disk = Join-Path $work 'disk.img'
& (Join-Path $qemu 'qemu-img.exe') convert -O raw "fat:$($fat.Replace('\','/'))" $disk
$code = (Join-Path $qemu 'share\edk2-x86_64-code.fd').Replace('\','/')
$vars = (Join-Path $work 'vars.fd').Replace('\','/')
$diskArg = $disk.Replace('\','/')
$serial = (Join-Path $work 'serial.log').Replace('\','/')
& (Join-Path $qemu 'qemu-system-x86_64.exe') -machine q35,accel=tcg -m 512 -smp 1 -display none -serial "file:$serial" -monitor stdio -nic none -no-reboot -boot order=c -drive "if=pflash,format=raw,readonly=on,file=$code" -drive "if=pflash,format=raw,file=$vars" -drive "if=ide,format=raw,file=$diskArg"
```

Wait until the serial log contains `EFI variable inventory written.` (in
another terminal: `Get-Content "$work\serial.log" -Tail 20`). Then enter
`quit` at the QEMU monitor prompt and wait for QEMU to exit cleanly. The raw
disk image retains the report; do not use QEMU `-snapshot` or a transient
virtual-FAT drive. Extract and parse it without mounting the disk:

```powershell
$raw = Join-Path $work 'HypersinvEfiInventory.txt'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\efi_inventory\tests\Extract-QemuFatReport.ps1 -ImagePath $disk -OutputPath $raw
& .codex-build\efi_inventory_clean\efi_inventory.exe scan-efi $raw (Join-Path $work 'vm-inventory.txt')
Get-Content (Join-Path $work 'vm-inventory.txt') -TotalCount 12
```

The extractor only reads an MBR FAT16 QEMU image and creates a **new** output
file; the Windows parser validates the `HVEFI001` header, records, and final
`END` count. A successful run prints the EFI variable count. The count and
names depend on the VM firmware. In the verified run, OVMF reported 24
variables; the report was 3,116 bytes. The collector writes the report at
`\HypersinvEfiInventory.txt` on its boot volume, never variable payloads.
It checks for an existing file and refuses to replace one; the UEFI file
protocol has no atomic exclusive-create operation, so use a fresh disk.

If no success marker appears, inspect the serial log and confirm the boot file
is at `EFI\BOOT\BOOTX64.EFI`. If the marker appears but extraction fails,
confirm QEMU was allowed to exit cleanly and that the raw disk (not the staged
`fat` folder) was used. A failed or truncated capture lacks the final `END`
record and is rejected. VM results describe OVMF, **not** the physical host.

## Windows usage

`scan-windows` and `scan` can run as the current user. `capture-windows` and
`backup` need an
elevated token with `SeSystemEnvironmentPrivilege`; the pipe server/client
need an elevated administrator or SYSTEM token. New output files have ACLs
for the collecting user, Administrators, and SYSTEM; existing files are never
replaced. Treat reports and backups as sensitive because SMBIOS serials and
variable names can identify the device.

```powershell
efi_inventory.exe scan-windows C:\inventory-smbios.txt
efi_inventory.exe scan-efi C:\HypersinvEfiInventory.txt C:\inventory-efi.txt
efi_inventory.exe scan C:\HypersinvEfiInventory.txt C:\inventory-full.txt
efi_inventory.exe backup C:\HypersinvEfiInventory.txt C:\efi-backup.dpapi
```

Use `scan-efi` when analyzing a report captured from a VM on a different
machine. `scan` reads SMBIOS from the Windows machine executing the command;
run it only in the **same guest** that produced the EFI capture. Otherwise it
would silently combine two machines' identities. `backup` similarly reads
the current Windows machine's runtime variables and must not be run against a
VM capture on the host. For a physical-host SMBIOS check, use `scan-windows`.
For the runtime variable and identifier set, use `capture-windows` with
elevation as described in the Windows-only guide.

For a local Windows smoke check, from the repository root (no elevation):

```powershell
& .codex-build\efi_inventory_clean\efi_inventory.exe scan-windows .codex-build\efi_inventory_clean\host-smbios.txt
Get-Content .codex-build\efi_inventory_clean\host-smbios.txt -TotalCount 12
```

Expect `EFI variables: 0` and SMBIOS entries below. This reads the current
machine's SMBIOS and creates a sensitive report; choose a fresh output path.
The unit/contract checks are the `ctest` command in **Build**. To smoke-test
the optional pipe, open two elevated terminals: run `efi_inventory.exe serve`
in the first and `efi_inventory.exe pipe-ping` in the second; expect
`HVEFI-READ-ONLY`. Stop the server with Ctrl+C. `pipe-smbios` returns a
read-only SMBIOS report through the same authenticated channel.

The companion is optional for local reports. Run `efi_inventory.exe serve` in
one elevated terminal; use `pipe-ping` or `pipe-smbios` in another. Its wire
protocol has no write request. Do not expose the key file or pipe to untrusted
users. The backup is tied to the Windows user profile used for collection; it
is not portable to another account or machine by copying the file alone.
Use new output paths each time. The Windows reports and backups refuse to
replace existing files.

The backup command reports saved, skipped, and changed-record counts. Do not
use a partial snapshot as a future restore prerequisite. There is deliberately
no restore command or firmware write path in this release.

## Data formats and interpretation

The EFI metadata file is UTF-8-compatible ASCII: `HVEFI001` header; `VAR`
lines with canonical GUID, attributes, size, status, and UTF-16 name encoded
as hexadecimal code units; and an `END` line with the exact record count. It
contains no variable payload. The backup plaintext envelope before DPAPI
encryption is versioned (`HVBK001`, version 1, record count) and contains
length-delimited UTF-16 names, GUIDs, attributes, and bytes. No restore parser
is intentionally shipped yet.

An EFI variable is identified by **name and vendor GUID**, and its attributes
determine nonvolatile, boot-service, runtime, and authenticated-write behavior.
There is no standard mapping from arbitrary EFI variables to SMBIOS serials or
UUIDs. SMBIOS values are firmware-provided structures; a suspicious-looking
EFI name is only a lead for vendor documentation and controlled testing.
Boot-order, Secure Boot, and TPM state are not hardware-ID modification
targets. Never infer that a value is safe to change solely from its name or
from a successful read.

References: [UEFI variable services](https://uefi.org/specs/UEFI/2.10/08_Services_Runtime_Services.html),
[Microsoft firmware-variable API](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getfirmwareenvironmentvariableexw),
[Microsoft SMBIOS guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/smbios),
[Microsoft named-pipe security](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights).
