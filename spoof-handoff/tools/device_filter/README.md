# Device response filters

This standalone package implements bounded response transformations for machine
identifier spoofing for a client application. It is separate from the proven
SMBIOS boot payload and the HVR1 resident-root companion. Existing public EFI and
HVR1 contracts are unchanged.

The source builds two drivers because storage PnP and NDIS lightweight filters
have different registration and lifetime contracts:

- `storage/storage_filter.c` intercepts successful
  `IOCTL_STORAGE_QUERY_PROPERTY` / `StorageDeviceProperty` responses for the
  captured disk PnP instance. The portable transform validates descriptor
  bounds, serial offset, terminator, exact original and equal replacement length.
- `nic/nic_filter.c` clones, forwards and completes regular NDIS current-address
  queries for the four captured adapter PnP instances. It preserves cancellation,
  failure and byte-count forwarding. Permanent-address fields and packets are
  unchanged. The portable policy validates the four unicast bindings and applies
  an exact-value guard.

Only values in `spoof-handoff/reports/client-originals.tsv` are used. The one
captured disk and four selected adapters are explicit bindings. Additional
interfaces have no invented replacements. CPU/GPU and SMBIOS configurations are
unchanged.

## Validation performed

The portable storage and NIC suites prove original-buffer to replacement-buffer
to original-buffer round trips, including malformed-buffer and identity/value
rejections. They require no device, elevation or VM. Their results are host unit
evidence. They do not prove that an application has observed a loaded filter.

Disabled and enabled variants compile and link with the installed x64 WDK.
Kernel imports are checked. Signed review copies use the existing local key;
PKCS#7 signatures, the pinned certificate signature and PE image digests are
verified without importing a certificate. An altered signed image is rejected.
The build defaults to `DEVICE_FILTER_ENABLE_REWRITE=OFF`. Explicit enabled
artifacts are kept separately. No filter was installed or loaded.

## Coverage still requiring validation

The storage handler covers the documented descriptor query that crosses its
attached stack. Other storage protocols and WMI visibility have no live proof.
The NIC handler covers regular NDIS current-address OIDs that traverse its filter
module. It does not update the existing interface provider's MIB state. NDIS can
cache interface information from miniport general attributes. Therefore this
implementation does not establish replacement visibility through `GetIfTable2`,
`GetAdaptersAddresses`, or WMI. Direct/synchronous OID paths are outside this
module. Attachment to all four adapters remains untested.

An INF/catalog and supported PnP/NDIS installation and removal integration remain
missing. Loading either file with a generic service command is not a verified
filter installation. The full system-wide goal remains incomplete.

## Build and signing commands

From the repository root, use new output directories:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\tools\device_filter\build.ps1 -ReportDirectory "$PWD\spoof-handoff\reports\device-filter-build-review" -ArtifactDirectory "$PWD\spoof-handoff\artifacts\device-filter-build-review"
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\tools\device_filter\sign.ps1 -ArtifactDirectory "$PWD\spoof-handoff\artifacts\device-filter-build-review" -ReportDirectory "$PWD\spoof-handoff\reports\device-filter-sign-review" -SignedDirectory "$PWD\spoof-handoff\artifacts\device-filter-signed-review"
```

The build/sign commands only create local artifacts. Neither command installs,
loads, changes boot configuration or reboots.

`commands/read-device-filter-values.ps1` supplies a future native/WMI comparison
probe. It reads normal Windows paths and emits exact expected-versus-actual
mismatches. It has not been invoked for a replacement proof in this phase.

## Boot and rollback boundary

The actual live `bcdedit /set secureboot off` and
`bcdedit /set dbgsigning on` checks both failed with unsupported element-type
errors. Before/after BCD and firmware records match. The real loader GUID also
enumerates from the earlier isolated store, so that store did contain the target.
Firmware Secure Boot state is separate from BCD data.

A supported test-mode validation requires firmware Secure Boot OFF before
enabling Windows TESTSIGNING. Restoring Secure Boot ON requires a later firmware
action unless a verified manufacturer API is supplied. The original TESTSIGNING
element was absent; restoration must preserve that absence. A one-manual-action
whole-cycle rollback is not provided.

The latest report is
`spoof-handoff/reports/live-bcd-device-implementation-20261014/REPORT.txt`.
