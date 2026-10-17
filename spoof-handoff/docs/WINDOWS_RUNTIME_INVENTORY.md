# Windows runtime identifier inventory

All implementation and documentation changes are in the feat/windows-runtime-inventory worktree. The original checkout source and earlier capture files are preserved. This workflow is Windows-only and read-only.

## Build

~~~powershell
Set-Location 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\.codex-build\worktrees\windows-inventory'
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\efi_inventory\capture-windows.ps1 Build
~~~

The script builds Release/x64/static CRT and runs all six host suites with EFI_INVENTORY_BUILD_COLLECTOR=OFF. The executable is .codex-build/efi_inventory_windows/efi_inventory.exe. No EFI boot staging is required.

## Capture or update the report

For a new variable snapshot and coverage report, use elevated PowerShell in the worktree:

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\efi_inventory\capture-windows.ps1 Capture
~~~

Output paths are unique timestamp/GUID names. Optional -VariableReport and -IdentifierReport arguments must name new files. Optional -SmbiosReport is checked against the freshly parsed raw table; the original 61-field source predates the Type 4 correction and should not be used as current provenance. Omit that parameter or create a corrected fresh source.

For coverage re-analysis without firmware-variable elevation, use the existing runtime snapshot already known to come from this host:

~~~powershell
$captureDirectory = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\.codex-build\efi_inventory'
$captureSuffix = [Guid]::NewGuid().ToString('N')
$smbiosPath = Join-Path $captureDirectory ('host-smbios.' + $captureSuffix + '.txt')
$identifierPath = Join-Path $captureDirectory ('host-identifiers.' + $captureSuffix + '.txt')
$inventoryTool = '.\.codex-build\efi_inventory_windows\efi_inventory.exe'
& $inventoryTool scan-windows $smbiosPath
if ($LASTEXITCODE -ne 0) { throw 'Fresh SMBIOS capture failed.' }
& $inventoryTool analyze-windows --same-host (Join-Path $captureDirectory 'host-vars.txt') $identifierPath $smbiosPath
~~~

--same-host records caller-asserted provenance; the report explicitly says that runtime enumeration was not repeated. It cannot authenticate a metadata file's hardware origin. Existing files are never overwritten, and normally failed new outputs are discarded. No reboot or Secure Boot change is required.

## Verified final evidence

All reports are in C:\Users\jayli\OneDrive\Documents\Projects\hsinv\.codex-build\efi_inventory:

| File | Role |
| --- | --- |
| host-vars.txt | Existing verified 146-record runtime snapshot, reused unchanged |
| host-smbios-followup-20261010-357c2c2c.txt | Fresh corrected SMBIOS source: 67 fields, including Type 44 audit |
| host-identifiers-followup-20261010-357c2c2c.txt | Final current SMBIOS/WMI coverage and source -> disposition -> exact-field table |

Nine of eleven members have confirmed sources. FakeCPUSerial remains uncovered: corrected Type 4 and Win32_Processor.SerialNumber expose no usable serial. One Type 44 block exists, references no matching Type 4 handle, and defines no standardized serial mapping. FakeGPUSerial is not obtainable at runtime by this Windows-only collector; standard PCIe serial access requires a supported driver interface. CPU/GPU values stay zero and disabled in default config generation.

SpoofSMBIOSUUID and WCHAR FakeSMBIOSUUID[37] are appended; confirmed SMBIOS Type 1 UUID maps to FakeSMBIOSUUID. Every prior member name/offset is unchanged, but struct size increases and callers must be rebuilt. New UUID defaults are zero/disabled. SMBIOS Type 2 defines no separate BoardId, so none is added or inferred from product/asset tag.

The single EFI name lead is explicitly dropped because a name alone cannot establish payload semantics/value correspondence. Confirmed source rows are mapped to exact fields; unknown observations and fields retain explicit unavailable/zero/disabled decisions. Multiple disks/adapters remain separate.

All six integrated suites passed. Format validation confirmed ASCII/CRLF, runtime-access attributes, unique records and END count; the existing parser accepted all 146 records. A new elevated query was canceled by Windows, so the final report honestly reuses the earlier runtime snapshot with fresh corrected SMBIOS/WMI reads. The earlier three capture files remain byte-identical.

For the exact field map, protocol, source references, and failure behavior, see [tools/hwid_spoofer/README.md](../tools/hwid_spoofer/README.md).
