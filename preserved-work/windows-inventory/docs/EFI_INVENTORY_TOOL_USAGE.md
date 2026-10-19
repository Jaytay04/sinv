# efi_inventory tool — runbook

For Windows-only runtime variables and HWID_SPOOF_CONFIG coverage, use
[WINDOWS_RUNTIME_INVENTORY.md](WINDOWS_RUNTIME_INVENTORY.md). That path builds
with the EFI collector OFF and passed all six host suites. The historical
pre-boot workflow below is outside the current capture scope; its two-test
counts describe the earlier build.

Build is verified green (collector + host exe + 2/2 host tests), so every
command below is runnable. Paths are repo-root-relative (`.\`). The only
toolchain requirement is an x64 MSVC prompt (VS2026 x64 here); cmake, ninja,
and cl.exe are enough — no WDK.

## 0) Build everything (one paste, already verified)

```powershell
$vcbat = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
cmd /c "`"$vcbat`" x64 && cmake -S tools\efi_inventory -B .codex-build\efi_inventory -G Ninja -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe -DCMAKE_BUILD_TYPE=Release && cmake --build .codex-build\efi_inventory && ctest --test-dir .codex-build\efi_inventory --output-on-failure"
```

That produces, in `.codex-build\efi_inventory\`:
- `efi_inventory.exe` — Windows host tool (ESP-report parser + SMBIOS + encrypted backup + read-only local pipe)
- `efi\inventory.efi` — freestanding EFI variable collector (boot payload; also what the project loader hooks)
- `efi_inventory_tests.exe`, `efi_inventory_pipe_tests.exe` — two host tests (`ctest` runs them, 2/2 pass)

Collector only:
```powershell
cmake --build .codex-build\efi_inventory --target efi_inventory_collector
```

## 1) Host-only SMBIOS probe (any machine, no VM, no elevation)

```powershell
& .codex-build\efi_inventory\efi_inventory.exe scan-windows .codex-build\efi_inventory\host-smbios.txt
Get-Content .codex-build\efi_inventory\host-smbios.txt -TotalCount 20
```

Reads only the current machine's SMBIOS (`RSMB`). No EFI variables, no firmware.
This is the safe "what does this host expose" step.

## 2) Full inventory: SMBIOS + armed-boot ESP report (same machine)

```powershell
& .codex-build\efi_inventory\efi_inventory.exe scan <abs-ESP-path>\EFI\insecure-boot\HypersinvEfiInventory.txt .codex-build\efi_inventory\inventory-full.txt
```

Variables-only (no SMBIOS merge; capture from another machine):
```powershell
& .codex-build\efi_inventory\efi_inventory.exe scan-efi <abs-ESP-path>\EFI\insecure-boot\HypersinvEfiInventory.txt .codex-build\efi_inventory\inventory-efi.txt
```

Optional encrypted variable snapshot (elevation + SeSystemEnvironmentPrivilege):
```powershell
& .codex-build\efi_inventory\efi_inventory.exe backup <abs-ESP-path>\EFI\insecure-boot\HypersinvEfiInventory.txt .codex-build\efi_inventory\efi-backup.dpapi
```

Optional local read-only companion (two elevated terminals):
```powershell
& .codex-build\efi_inventory\efi_inventory.exe serve        # terminal 1
& .codex-build\efi_inventory\efi_inventory.exe pipe-ping     # terminal 2 -> "HVEFI-READ-ONLY"
& .codex-build\efi_inventory\efi_inventory.exe pipe-smbios   # SMBIOS over the same pipe
```

## 3) VM/OVMF smoke of the collector (offline, disposable)

Repo root, QEMU + x64 OVMF files; `$qemu` adjustable, `$work` fresh per run:

```powershell
$qemu = 'C:\Program Files\qemu'
$work = Join-Path (Get-Location) '.codex-build\efi_inventory_vm_run'
$fat = Join-Path $work 'fat'; $boot = Join-Path $fat 'EFI\BOOT'
New-Item -ItemType Directory -Path $boot -Force | Out-Null
Copy-Item .codex-build\efi_inventory\efi\inventory.efi (Join-Path $boot 'BOOTX64.EFI')
Copy-Item "$qemu\share\edk2-i386-vars.fd" (Join-Path $work 'vars.fd')
$disk = Join-Path $work 'disk.img'
& "$qemu\qemu-img.exe" convert -O raw "fat:$($fat.Replace('\','/'))" $disk
& "$qemu\qemu-system-x86_64.exe" -machine q35,accel=tcg -m 512 -smp 1 -display none -serial "file:$(Join-Path $work 'serial.log')" -monitor stdio -nic none -no-reboot -boot order=c -drive "if=pflash,format=raw,readonly=on,file=$(Join-Path $qemu 'share\edk2-x86_64-code.fd')" -drive "if=pflash,format=raw,file=$(Join-Path $work 'vars.fd')" -drive "if=ide,format=raw,file=$disk"
```

Wait for `EFI variable inventory written.` in the serial log, then `quit`.
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\efi_inventory\tests\Extract-QemuFatReport.ps1 -ImagePath $disk -OutputPath (Join-Path $work 'report.txt')
& .codex-build\efi_inventory\efi_inventory.exe scan-efi (Join-Path $work 'report.txt') (Join-Path $work 'out.txt')
Get-Content (Join-Path $work 'out.txt') -TotalCount 12
```

A successful capture always lands a `HVEFI001 ... VAR ... END` file;
count/names depend on the VM firmware.

## 4) Real one-shot boot via the project loader (ESP path)

After the usual `ib-install.exe stage + oneshot --mode bootnext`, arm the one
armed boot by copying the collector next to `loader.efi` on the ESP:

```powershell
mountvol X: /S
Copy-Item .codex-build\efi_inventory\efi\inventory.efi "X:\EFI\insecure-boot\inventory.efi"
mountvol X: /D
Restart-Computer
```

Console line to look for: `inventory collector: PASS`. The report is written
to `\EFI\insecure-boot\HypersinvEfiInventory.txt` on the ESP (volume root of
the loaded image); grab it after boot:

```powershell
mountvol X: /S
Copy-Item "X:\EFI\insecure-boot\HypersinvEfiInventory.txt" ".codex-build\efi_inventory\capture\" -Force
del "X:\EFI\insecure-boot\inventory.efi"   # disarm; absence is silently skipped
mountvol X: /D
& .codex-build\efi_inventory\efi_inventory.exe scan ".codex-build\efi_inventory\capture\HypersinvEfiInventory.txt" ".codex-build\efi_inventory\inventory-full.txt"
```

The armed boot does not touch BootOrder and does not persist `inventory.efi`;
afterwards the normal Windows chain runs unchanged.

See also: `tools\efi_inventory\README.md`, `tools\efi_inventory\LOADER_PAYLOAD_HANDOFF.md`,
`tools\efi_inventory\BARE_METAL_COLLECTOR_TEST.md`, and `docs\BUILD_BOOT_TEST.md`.