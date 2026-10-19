# Windows-only runtime identifier capture

This worktree adds a read-only collector and field-coverage report. It does not apply the historical spoofing stub, generate replacement identifiers, install drivers, boot EFI images, or change firmware, network, or boot settings.

## Build

From the Windows inventory worktree in normal PowerShell:

~~~powershell
Set-Location 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\.codex-build\worktrees\windows-inventory'
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\efi_inventory\capture-windows.ps1 Build
~~~

The script locates x64 Visual Studio C++ tools, CMake 3.24+ and Ninja. It builds Release/static CRT artifacts under this worktree's .codex-build/efi_inventory_windows and runs the host contracts. EFI_INVENTORY_BUILD_COLLECTOR is explicitly OFF. This standalone tool does not configure the root hypervisor/loader superbuild.

The built executable is .codex-build/efi_inventory_windows/efi_inventory.exe. Run it in place through the script; there is no EFI or boot-volume staging step for this Windows capture.

## Capture this host

Use elevated PowerShell, from the same worktree:

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\efi_inventory\capture-windows.ps1 Capture
~~~

Both output paths must be new. The program reserves both with CREATE_NEW and restricted permissions for the collecting user, Administrators, and SYSTEM. On a normal failure, it removes only its own newly created partial reports. An interrupted process can leave partial files; use fresh names for the next run. Existing input or output files are never replaced.

The supplied SMBIOS capture is byte-compared with a fresh Windows raw-table inventory before it is referenced in the identifier report. A stale or different-machine capture fails explicitly. The original 61-field input is preserved. It predates the Type 4 offset correction and will not match the corrected inventory; use a fresh source or omit -SmbiosReport. Without -SmbiosReport, the report identifies the current raw Windows table as its source.

For subsequent captures, omit the two output parameters; the script creates unique timestamp/GUID names in the worktree build's captures directory. The source parameter remains optional. No reboot, MOK enrollment, ESP mount, or Secure Boot change is required.

## Output and scope

host-vars.txt contains only the exact variable metadata protocol. It is ASCII with CRLF, no BOM:

~~~text
HVEFI001
VAR\t{GUID}\tATTRS(8 hex)\tSIZE(16 hex)\tSTATUS(16 hex)\tNAME
END\tCOUNT(8 hex)
~~~

The displayed \t stands for a literal tab. GUIDs use canonical braces and 8-4-4-4-12 hexadecimal groups. NAME consists of four uppercase hex digits per UTF-16 code unit: read each little-endian native code unit as a 16-bit number, then format it (for example, S is 0053). SIZE is the native value length, not a serial-number length. STATUS is zero for each successfully returned native snapshot record; native query failures are reported as NTSTATUS and do not manufacture a successful empty capture. Attributes are unmodified EFI bits. Any record without EFI_VARIABLE_RUNTIME_ACCESS (0x00000004) is dropped.

NtEnumerateSystemEnvironmentValuesEx class 2 is a native Windows API, not a documented Win32 enumeration contract. The collector resolves it from the already loaded ntdll, bounds buffer growth and parsing, and temporarily enables/restores SeSystemEnvironmentPrivilege. The native call returns value bytes, but output records retain metadata only and do not serialize payloads. This is a post-boot snapshot; boot-service-only data is outside scope. Missing or access-denied data is not inferred.

host-identifiers.txt is a separate ASCII/CRLF sidecar. It preserves all SMBIOS fields, records a final drop decision for every runtime variable name lead, lists each Windows device observation, and maps coverage to the exact current config member. Controls and non-ASCII data are escaped to prevent record injection.

| Requested identifier | Existing config member | Explicit source |
| --- | --- | --- |
| Disk serial, per disk | FakeDiskSerial | Win32_DiskDrive.SerialNumber |
| Current MAC, per physical adapter | FakeMacAddress | Win32_NetworkAdapter.MACAddress |
| System serial/manufacturer/product | FakeSMBIOSSerial / FakeSMBIOSManufacturer / FakeSMBIOSProduct | SMBIOS Type 1 fields |
| Board serial/manufacturer/product | FakeMotherboardSerial / FakeMotherboardManufacturer / FakeMotherboardProduct | SMBIOS Type 2 fields |
| CPU serial | FakeCPUSerial | SMBIOS Type 4 serial; Win32_Processor.SerialNumber |
| GPU serial | FakeGPUSerial | Uncovered by standard Win32_VideoController; no substitute |
| System UUID | FakeSMBIOSUUID (SpoofSMBIOSUUID gate) | Valid SMBIOS Type 1 UUID |
| Board ID | No separate config member | SMBIOS Type 2 defines no separate BoardId; product retains product semantics and asset tag is not relabeled |

CONFIRMED means a source explicitly returned that semantic field. It does not verify factory truth, permanent MAC, uniqueness, application consumption, or successful spoofing. CANDIDATE -> DROPPED means a name/source hint has no verified semantic or value correspondence and receives no target. UNCOVERED means the required value is unavailable or a placeholder. The report ends with a decision table of dropped hints, confirmed source mappings, and uncovered zero/disabled policies. No pending candidate affects coverage.

UUID/SERIAL/BOARD/SYSTEM/PRODUCT/MACHINE name matches are heuristic EFI leads, never confirmed mappings. Every EFI name hint is dropped with a one-line reason; an existing SMBIOS UUID/serial does not establish that a similarly named EFI variable contains it. EFI payloads are not inspected to establish identifier semantics. CPU ProcessorId is a CPUID-related value and is not substituted for a CPU serial. GPU PNP IDs, names, and UUIDs are not GPU serials. Multiple disks/adapters remain separate; the one-value config does not select a device automatically.

Existing HWID_SPOOF_CONFIG member names and offsets remain unchanged. SpoofSMBIOSUUID and WCHAR FakeSMBIOSUUID[37] are appended for canonical 36-character UUID text plus a terminator. The struct size increases: rebuild every caller against the new header; old-size binaries cannot pass the enlarged struct safely. No separate BoardId is added. CPU/GPU serials remain zero/disabled in default generation, and the new UUID is zero/disabled until explicitly supplied. The historical apply/remove functions remain placeholders; the collector never calls them. The helper now uses WCHAR counts rather than byte counts for serial/string buffer capacities. Windows SDK types replace the unused WDK include so the append/default contract can be compiled and tested without a driver build.

## CPU, GPU, and BoardId source limits

CPU serial uses the SMBIOS Type 4 string index at offset 0x20 and Win32_Processor.SerialNumber. Asset tag and part number are at 0x21/0x22 and are never substituted. Empty/NULL/placeholder serials are valid not-exposed results; failed WMI reads retain an error note. Processor Additional Information is Type 44 (2Ch), distinct from Type 4 (04h): the collector audits presence, block metadata, architecture, and referenced Type 4 handle, but does not treat opaque architecture/vendor data as a generic serial.

BAR0 + 0x40 is device-specific MMIO, not a standardized PCI serial location. Vendor/device IDs describe a model, not a unique board serial. PCIe defines an optional 64-bit Device Serial Number extended capability (not a generic 128-bit serial). Microsoft documents configuration-space reads through kernel-mode bus interfaces/IRPs. This Windows-only collector uses neither a kernel driver nor arbitrary MMIO reads, so FakeGPUSerial is not obtainable at runtime by this collector. This does not assert that the GPU lacks a serial or that a vendor API could never expose one.

Type 2 has no separate standardized BoardId field. Baseboard product, serial, version, asset tag, board type, and location retain their own semantics; no BoardId value or member is invented.

## Re-analyze the existing same-host snapshot

The analyze-windows command needs no firmware-variable privilege. It reads an existing valid runtime metadata report and fresh SMBIOS/WMI data. --same-host is an explicit caller assertion, not an origin check: use only a report already known to come from this host. Provenance states that native enumeration was not repeated. Both current source and output paths below are fresh.

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

To refresh the variable set itself, run Capture from elevated PowerShell. Re-analysis keeps the earlier variable snapshot and changes only the newly created coverage report. Both commands refuse existing output files and discard their newly created partial outputs on normal failures.

## Verified follow-up on this host

The earlier host-smbios.txt (61 fields), host-vars.txt (146 variables), and host-identifiers.txt remain intact. The original CPU/asset/part labels were parsed one byte early; the corrected source below supersedes those observations.

The fresh files under the original checkout's .codex-build/efi_inventory are:

- host-smbios-followup-20261010-357c2c2c.txt: corrected SMBIOS capture, 67 fields including one Type 44 audit.
- host-identifiers-followup-20261010-357c2c2c.txt: current corrected SMBIOS/WMI evidence plus the explicitly reused host-vars.txt snapshot.

Nine of eleven config fields have confirmed source observations, including FakeSMBIOSUUID. CPU serial is not exposed by the inspected Type 4/WMI sources; the Type 44 block has no matching Type 4 handle and supplies no standardized serial mapping. GPU serial is unavailable within this collector's runtime scope. The one EFI name lead is CANDIDATE -> DROPPED with a reason; all confirmed source rows end CONFIRMED -> MAPPED to exact fields.

The Release build and all six host test suites passed, including raw Type 4/44 fixtures, candidate dispositions, UUID mapping, every legacy config offset, and zero/disabled defaults. Existing-file and normal-failure cleanup checks passed. Windows elevation was canceled for a new variable query; no new native query ran. The final report was produced successfully by same-host re-analysis with fresh SMBIOS/WMI reads.

Generated reports and build state are ignored and are not part of the merge patch.

## References

- [SMBIOS 3.9 Type 2/4/44 definitions](https://www.dmtf.org/sites/default/files/standards/documents/DSP0134_3.9.0.pdf)
- [Supported PCI configuration access](https://learn.microsoft.com/en-us/windows-hardware/drivers/pci/accessing-pci-device-configuration-space)
- [PCIe serial capability](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_pci_express_serial_number_capability)
- [Windows raw SMBIOS API](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsystemfirmwaretable)
- [Native enumeration definitions](https://github.com/winsiderss/phnt/blob/master/ntexapi.h)
- [Runtime visibility rules](https://uefi.org/specs/UEFI/2.10/08_Services_Runtime_Services.html)
- [Disk serial property](https://learn.microsoft.com/en-us/windows/win32/cimwin32prov/win32-diskdrive)
- [Network adapter properties](https://learn.microsoft.com/en-us/windows/win32/cimwin32prov/win32-networkadapter)
- [Processor serial and ProcessorId](https://learn.microsoft.com/en-us/windows/win32/cimwin32prov/win32-processor)
- [Video controller properties](https://learn.microsoft.com/en-us/windows/win32/cimwin32prov/win32-videocontroller)
