# Process-local client device fingerprint

The package implements machine identifier spoofing for a client application using
explicit native read wrappers in that application's own process. The harness is
the controlled client used for host verification. Each call reads Windows anew
and substitutes only configured, confirmed fields in the returned observation.
It does not alter Windows API entry points or affect other processes.

## Confirmed read paths

| Config member | Native source | Selection |
|---|---|---|
| FakeDiskSerial | Win32_DiskDrive.SerialNumber | Explicit DeviceID and exact observed original |
| FakeMacAddress | Win32_NetworkAdapter.MACAddress | Explicit DeviceID + PNPDeviceID and exact observed original |
| FakeSMBIOSSerial | RSMB Type 1, string index at 0x07 | Single captured Type 1 |
| FakeSMBIOSManufacturer | RSMB Type 1, string index at 0x04 | Single captured Type 1 |
| FakeSMBIOSProduct | RSMB Type 1, string index at 0x05 | Single captured Type 1 |
| FakeMotherboardSerial | RSMB Type 2, string index at 0x07 | Single captured Type 2 |
| FakeMotherboardManufacturer | RSMB Type 2, string index at 0x04 | Single captured Type 2 |
| FakeMotherboardProduct | RSMB Type 2, string index at 0x05 | Single captured Type 2 |
| FakeSMBIOSUUID | RSMB Type 1, bytes 0x08 through 0x17 | Single captured Type 1; SpoofSMBIOSUUID gate |

The authoritative field map remains `../README.md`. Unavailable members remain
zero and disabled; initialization rejects enabling or populating them. No separate
board identifier is added. Existing configuration offsets and the appended UUID
layout are preserved by the existing ABI test.

## Lifecycle and client API

1. Populate `HWID_SPOOF_CONFIG` with validated replacement values.
2. Call `HwidSpooferInitialize`, then `HwidSpooferSetDeviceBindings` with explicit
   disk/adapter keys, original values and replacements.
3. Select any confirmed fields with `HwidSpooferSetFieldMask`. Default mask is all
   nine. Config group flags still apply.
4. Call `HwidSpooferApply`, then `ReadClientIdentifiers` from the client.
5. Call `HwidSpooferRemove` to resume freshly read native observations, then Cleanup.

`ReadNativeIdentifiers` always invokes the Windows sources and never consults the
replacement engine. The harness uses it as an independent observation path while
Apply is active. The client wrapper reads the same native sources, resolves each
field through the process-local engine, and rejects incomplete or invalid results.
The C resolver accepts newly read values, not cached originals. Disk/adapter
bindings reject source drift and reused device identities; unselected devices pass
through unchanged. Config/mask/bindings cannot change while active.

This controlled client serializes lifecycle changes and reads. The C state uses
an SRWLOCK, but a multi-field client read is not an atomic transaction against
concurrent Apply/Remove calls. Applications must serialize those operations as
the harness does.

The implementation only wraps these explicit client reads. An application that
continues calling Windows APIs directly sees originals. No drivers, new services,
kernel components, system-wide visibility, ESP/firmware access, reboot, or network
configuration changes are involved.

## Replacement conventions

- Serial replacements use ASCII letters/digits, underscore, hyphen or dot, are
  nonempty and at most 63 characters, and retain the original serial's length.
  The supplied disk/system/board examples also retain their punctuation layout.
  These are package test conventions; they do not assert a universal vendor format.
- Manufacturer/product replacements are nonempty printable ASCII, at most 63
  characters. Their lengths may differ because the wrapper returns string values,
  rather than editing a raw firmware table buffer.
- MAC replacements are valid nonzero unicast six-octet addresses. The supplied
  samples start with `02`, use the locally administered bit, and are distinct for
  every selected adapter. Equivalent octets expressed with another separator or
  case cannot pass the replacement-diff validation. This is the current WMI MAC
  observation, not a claim about a factory/permanent address.
- UUID replacements use canonical 36-character hex-and-hyphen syntax. Nil and
  all-ones UUIDs are refused; the supplied sample uses version 4 and RFC variant bits.
  Windows RSMB UUID decoding uses the SMBIOS 2.6+ mixed-endian convention.

Device keys are `disk:<DeviceID>` and `adapter:<DeviceID>|<PNPDeviceID>`. The
recorded `PhysicalAdapter=TRUE` WMI set includes two virtual adapters; the test
binds the recorded instances explicitly and makes no physical-hardware claim.
Blank or unselected adapter observations remain unmodified.

## Build and verification

Run in normal PowerShell at the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\tools\hwid_spoofer\test-client-identifiers.ps1
```

The script uses installed x64 Visual Studio C++ tools, CMake 3.24+ and Ninja.
All build files, temporary compiler/parser files and reports stay under this
package. Release uses static CRT; existing assertion-based fixtures stay enabled.
The package includes previously omitted unchanged host-test dependencies from the
matching inventory reference. Its Type 4 parser already uses serial offset 0x20;
the readonly main checkout parser is not changed.

All eight suites passed on 2026-10-12:

| Suite | Result |
|---|---|
| efi_inventory_parser | PASS |
| efi_inventory_pipe_contract | PASS |
| efi_runtime_variables_contract | PASS |
| windows_device_identifiers_contract | PASS |
| runtime_identifier_mapping_contract | PASS |
| hwid_config_contract | PASS |
| client_interceptor_contract | PASS |
| client_smbios_reads_contract | PASS |

The new tests cover the exact member/source map, config/format validation, explicit
selection, lifecycle, source drift, duplicate keys, output bounds, aliasing, SMBIOS
string-set bounds, canonical RSMB version offsets, UUID byte order and malformed
records. Pure parser fixtures cannot replace the live reader. Existing firmware
enumeration and pipe fixtures use their pre-existing mock/data paths; they do not
perform a boot test or install/start a service.

The live harness was process 2508. It proved system serial first, then each other
field, then all nine together. Twelve selected field/device rows passed
APPLY/read/REMOVE/read. Every native read while active remained original. The live
proof log is `../reports/client-host-proof.20261012T225155686Z.txt` and ends with
`HARNESS_EXIT=0`. Each original was compared to the previously captured host value.

To run the built harness directly:

```powershell
.\spoof-handoff\.build\userspace\client_identifier_host_test.exe --baseline .\spoof-handoff\reports\client-originals.tsv
```

`--first-field` limits that harness invocation to the system-serial proof.
If a required source, selected device, baseline agreement or read-back is missing,
the harness prints `BOUNDARY`, removes process-local state and exits nonzero. It
does not substitute a fixture, guess a value, or claim a proof for the failed field.

## Sources

Native API and property semantics: [GetSystemFirmwareTable](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsystemfirmwaretable),
[Win32_DiskDrive](https://learn.microsoft.com/en-us/windows/win32/cimwin32prov/win32-diskdrive),
[Win32_NetworkAdapter](https://learn.microsoft.com/en-us/windows/win32/cimwin32prov/win32-networkadapter), and
[DMTF SMBIOS specification](https://www.dmtf.org/sites/default/files/standards/documents/DSP0134_3.8.0.pdf).
