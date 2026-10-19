# HWID identifier spoof — handoff package

CURRENT HOST PROOF (2026-10-18)

All nine confirmed field categories have original -> replacement -> restored
proof on DESKTOP-5852FUT, across separate SMBIOS, Disk 0, and four-adapter NIC
runs. SMBIOS was read through native RSMB and WMI; Disk 0 through storage IOCTL
and WMI; NIC current addresses through seven native/Windows readers plus the
Intel Wi-Fi DOT11 query. These are the recorded read paths and devices, not a
simultaneous all-nine deployment or a claim about every possible reader.
The NIC run restored original current addresses and typed registry values with
Secure Boot ON, test-signing OFF, no test certificate and no project NIC filter.
CPU/GPU serial members remain zero/disabled. See
`docs/NIC_VM_VALIDATION.md` and
`reports/review-20261017/nic-host-round-trip.json` for evidence and limitations.

The field-to-member map below remains authoritative. The process-local sections
describe the original userspace package and its own visibility limits.

Windows-only confirmed field -> HWID_SPOOF_CONFIG member mapping and process-local
machine identifier spoofing for a client application. The interceptor affects only
the package's explicit userspace read wrappers. Native Windows reads stay intact.

WHAT THIS IS / ISN’T
- WHAT: a read-only collector that enumerated runtime-visible EFI variables
  (class-2 NtEnumerateSystemEnvironmentValuesEx), raw SMBIOS (GetSystemFirmwareTable RSMB),
  and a small set of Win32/WMI device properties; plus a sidecar that maps each
  confirmed source to the exact config member, drops name-only leads with a reason,
  and leaves uncovered fields zero/disabled (never placeholder).
- PROCESS-LOCAL ENGINE: `tools/hwid_spoofer/hwid_spoofer.c` validates the confirmed
  replacement configuration, field masks and explicit disk/adapter bindings.
  `src/client_reads.cpp` wraps fresh Windows RSMB and WMI reads for the client.
  Apply enables those replacements; Remove resumes native observations. The
  existing configuration member names/offsets remain unchanged. No driver,
  service installation, firmware, network setting or system-wide interception is used.

LAYOUT
- README.md ...................... this file
- docs/WINDOWS_RUNTIME_INVENTORY.md  build + capture + re-analysis + scope (verbatim, neutralized)
- tools/hwid_spoofer/ ............. final hwid_spoofer.c/.h (values+mapping) + the new
                                    read-only collector (device_identifiers.cpp/h,
                                    read_only_inventory.cpp/h) + all its tests + README
- tools/efi_inventory/ ............ Windows collector (main.cpp, runtime_variables.cpp/h,
                                    include/*, capture-windows.ps1, tests) + README
- windows-runtime-inventory.patch  git diff of ALL these changes (tracked + new), applies clean
- git-diff-stat.txt .............. diff --stat for this package scope
- git-diff-stat-full.txt ..........diff --stat for the whole worktree branch

CONFIRMED -> MEMBER MAP (9 of 11 struct members have a confirmed source)
- disk serial (per disk) ............ FakeDiskSerial ...................... Win32_DiskDrive.SerialNumber
- current MAC (per adapter) ......... FakeMacAddress ...................... Win32_NetworkAdapter.MACAddress
- system serial/mfr/product ......... FakeSMBIOSSerial/Manufacturer/Product SMBIOS Type 1
- board serial/mfr/product .......... FakeMotherboardSerial/Mfr/Product ...... SMBIOS Type 2
- system UUID ....................... FakeSMBIOSUUID (SpoofSMBIOSUUID) ...... SMBIOS Type 1 UUID
- CPU serial ......................... FakeCPUSerial ...................... UNCOVERED (Type 4 @0x20 + Win32_Processor.SerialNumber both empty; one Type 44 block, no matching Type 4 handle, no std serial)
- GPU serial ......................... FakeGPUSerial ...................... UNCOVERED (BAR0+0x40 is vendor-specific MMIO; PCIe serial needs a supported driver iface)
- board id ......................... (no member) ......................... SMBIOS Type 2 has no separate BoardId (asset tag not relabeled)
- multiple disks/adapters ............ stay separate; the single-value members do not auto-pick a device

SEMANTICS
- CONFIRMED = a source explicitly returned that semantic field (factory truth / permanence / uniqueness /
  app-consumption are NOT asserted by this). CANDIDATE -> DROPPED = a name/source hint with no verified
  value correspondence (gets no target). UNCOVERED = value unavailable; zero/disabled, no guess.
- EFI variable name matches for UUID/SERIAL/BOARD/SYSTEM/PRODUCT/MACHINE are heuristic leads only; the single
  lead here is dropped with a reason. EFI payloads are not parsed for identifiers.
- post-boot snapshot = runtime-visible set only; boot-service-only vars (no RUNTIME_ACCESS) are out of scope.

BUILD / CAPTURE (verbatim, see docs/WINDOWS_RUNTIME_INVENTORY.md)
  cd <worktree> ; powershell -ExecutionPolicy Bypass -File tools/efi_inventory/capture-windows.ps1 Build
  (elevated) .../capture-windows.ps1 Capture ; outputs are unique-named, existing files never replaced.

README.md is the authoritative map; all other wording was neutralized to client/device terms.

PROCESS-LOCAL BUILD AND PROOF
Run from the repository root in normal PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\tools\hwid_spoofer\test-client-identifiers.ps1
```

This builds under `spoof-handoff/.build/userspace`, runs the six existing host
suites plus two new suites, then runs one package-controlled host test process.
The test proves system serial first, extends field-by-field, and checks the whole
set together. It requires exact agreement with captured originals; source drift
or a missing selected device stops the proof. No elevated shell is required.

The 2026-10-12 run passed all eight suites and all nine fields, with 12 independent
original/spoofed/restored records (one disk and four explicitly selected adapters).
Direct native reads remained original while Apply was active. The saved proof is
`reports/client-host-proof.20261012T225155686Z.txt`; selected originals/replacements
are in `reports/client-originals.tsv` with import provenance beside it.

Client applications must call `hwid_spoofer::client::ReadClientIdentifiers` to
observe the replacements. Calls directly to Windows APIs retain the native values.
Full API, format conventions, test results and scope are in
`docs/CLIENT_IDENTIFIER_INTERCEPTION.md`.

STORAGE FILTER TEST AND RECOVERY

The user-operated storage test uses the existing signing key and a boot-start
filter package. Its ordered workflow tests transparent operation, removal and
restoration before allowing serial substitution. Read the
[storage test runbook](docs/STORAGE_BAREMETAL_RUNBOOK.md) and the
[boot recovery guide](docs/STORAGE_BOOT_RECOVERY.md) before starting.

The VM reproduced the demand-start package's 0x7B and passed the boot-start
native/WMI round trip. The user then completed the transparent and replacement
boot-start tests on the pinned physical Intel boot disk, with native IOCTL and
fresh WMI original/replacement/restored values, successful package removal and
file I/O checks. Test-signing and temporary public certificate trust returned to
their prepared baseline. The snapshot and final check both had Secure Boot OFF.
The user subsequently enabled Secure Boot and reported an elevated
`Confirm-SecureBootUEFI` result of `True`, corroborated by the Windows boot-state
flag. Fresh native/WMI reads still returned the original serial, kernel
test-signing was OFF, and filter registration and temporary certificate trust
were absent. The scoped report and
raw evidence hashes are in
[storage-host-round-trip.json](reports/review-20261017/storage-host-round-trip.json).
This establishes the two tested read paths on that selected disk; other disks,
storage interfaces, stress/power behavior and physical WinRE recovery are untested.
This workflow does not install the NIC filter or change the SMBIOS payload.
CPU and GPU serial fields retain their existing disabled state. The previous
device-cycle entry points remain quarantined. Generated packages, recovery
snapshots and signing private keys stay outside source control.
