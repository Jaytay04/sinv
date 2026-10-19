# NIC reader-path validation

**Physical-host result: `ROUND_TRIP_PASS_AND_RESTORED` (87/87 applicable reader observations). All nine confirmed field categories now have host round-trip evidence from separate runs.**

The host run `20261018T013020912Z.569ae5e9bb194eb1b48994e33f4cd4a2`
passed after the same-vendor VMware VMnet VM gate (84/84). This closes the
physical Intel AX201 and VMware VMnet reader gap. Earlier failed attempts and
VM-only results below remain historical evidence.

Evidence paths below are relative to the main repository root; implementation source is in the feature worktree.

## Completed physical-host round trip

On DESKTOP-5852FUT (MAINGEAR VYBE, MSI MS-7D37, Windows 11 build
26200.9711), all four locked adapters returned these exact replacements and
then their original current addresses:

| Adapter | Original and restored | Replacement |
|---|---|---|
| VMware VMnet8 | `00:50:56:C0:00:08` | `02:90:10:20:30:40` |
| VMware VMnet1 | `00:50:56:C0:00:01` | `02:90:10:20:30:41` |
| Intel Wi-Fi AX201 | `C4:BD:E5:32:06:D1` | `02:90:10:20:30:42` |
| Realtek Ethernet | `D8:BB:C1:D8:57:20` | `02:90:10:20:30:43` |

The seven common readers were direct `OID_802_3_CURRENT_ADDRESS`,
`GetAdaptersAddresses`, `GetAdaptersInfo`, `GetIfTable2`, fresh
`Win32_NetworkAdapter.MACAddress`, fresh `MSFT_NetAdapter.NetworkAddresses`,
and `Get-NetAdapter.MacAddress`. Four adapters times seven readers times
original/replacement/restored gives 84 observations. Intel Wi-Fi also passed
`OID_DOT11_CURRENT_ADDRESS` in all three states, making 87 applicable
observations. The other three adapters returned error 87 for DOT11 in each
state; those nine not-applicable results are not counted as successful reads.

The mechanism was the per-device typed `NetworkAddress` configuration and an
adapter restart. The read-only native executable used no replacement wrapper.
No project NIC filter, test certificate, boot payload, reboot, or security
setting change was required for this NIC run. Permanent-address properties
were recorded for context and are not claimed changed.

All four typed registry baselines and administrative states were restored.
VMnet8, VMnet1 and Wi-Fi returned connected; Realtek returned disconnected.
Realtek's immediate restore checkpoint recorded `MediaConnectState=0` versus
baseline 2, both classified disconnected; the later final readback returned
2 and Disconnected, matching the baseline. The transient checkpoint is retained.

Before/after evidence confirms Secure Boot ON (`SecureBoot=01`), test-signing
OFF, the pinned certificate absent from Root and TrustedPublisher, and the NIC
filter service/bindings absent. The five captured firmware variables and all
live/exported BCD enumerations matched. ESP inventory contained 164 files:
159 ordinary files matched SHA-256, four live BCD hive/log files were verified
through supported export/enumeration, and BOOTSTAT.DAT was classified mutable.
Raw live BCD hive/log byte equality and uncaptured firmware variables are not
claimed.

The host journal SHA-256 is
`83906F41B77A9B45B605E8DC03A6E9224641413D0EE8CD4C0F069C9508E6A481`.
The replacement readback SHA-256 is
`318203ECC5096020CCB8BB9D4B21059B5D62A447411F1F86FB24952B922BAB29`.
The restored readback SHA-256 is
`9B6645DE3D44BEEB316AEE429CC684FF6E59CB08EAD62F95DDE2A21B4383FF4E`.

Consolidated actual per-reader values, source hashes, platform comparisons,
historical SMBIOS/disk evidence, and limitations are in
`spoof-handoff/reports/review-20261017/nic-host-round-trip.json`.
`nic-host-round-trip-SHA256SUMS.txt` beside it pins the durable evidence files.
Raw local captures remain under
`spoof-handoff/reports/nic-reader-gap-20261017/host-runs/20261018T013020912Z.569ae5e9bb194eb1b48994e33f4cd4a2/`.

The confirmed total is 9/9 categories: seven SMBIOS values via native RSMB and
WMI, one pinned physical disk serial via storage IOCTL and WMI, and current MACs
on four locked adapters via the seven common readers plus Wi-Fi DOT11.
These are separate successful host round trips, not one simultaneous combined
deployment or a claim about every possible reader. CPU/GPU serials remain
zero/disabled. The earlier disk proof's own scope and recovery limitations
remain unchanged.

## Host snapshot failure and retry

The manual run `20261018T001825865Z.94441c028a9945ffafb026e96972b9cf`
stopped during the initial read-only ESP snapshot: `Get-FileHash` could not
open `EFI/Microsoft/Boot/BCD` because Windows was using it. Its unchanged
journal records `mutationStarted=false`, no adapter snapshots or apply
operations, and all four original MACs through all seven readers both before
and after the error. Wi-Fi DOT11 also matched its original value. Captured
SecureBoot bytes remained `01`; BootOrder, BootCurrent and SetupMode bytes
also match. The old `RESTORE_OR_FINAL_VERIFICATION_FAILED` status includes
the second attempt to snapshot the same locked file; it does not establish an
adapter-restoration failure. No adapter rollback is needed for that run.

Evidence is under
`spoof-handoff/reports/nic-reader-gap-20261017/host-runs/20261018T001825865Z.94441c028a9945ffafb026e96972b9cf/`.
New preflight failures use `PREFLIGHT_FAILED_NO_ADAPTER_CHANGES`, retain the
exact errors, and populate both references to the original readback.

The revised snapshot uses successful live BCD enumerations and a hash-pinned
[`bcdedit /export`](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/bcdedit)
backup, whose contents are also enumerated with `/store <export> /enum all /v`.
Only `EFI/Microsoft/Boot/BCD` and `BCD.LOG`, `BCD.LOG1`, `BCD.LOG2` on the
[`IsSystem` partition](https://learn.microsoft.com/en-us/windows-hardware/drivers/storage/msft-partition)
use this classification. Their presence is checked, but raw hive/log byte
equality is not claimed. Both user-run exports succeeded but had different
hashes despite no adapter mutation; those backup hashes remain evidence of
the individual copies, not a stable representation of BCD settings.
Other ESP files, including every EFI executable, retain strict SHA-256
comparison. `BOOTSTAT.DAT` remains explicitly mutable. Unknown locked files,
an unreadable EFI image or a failed BCD export/enumeration still stop the run
before address changes.

A SYSTEM/admin probe on the disposable Windows VM reproduced the lock with
`Get-FileHash`, `File.ReadAllBytes`, and a read-only `FileStream` allowing
`FileShare.ReadWrite | FileShare.Delete`. All three returned the sharing
violation. The supported BCD export and enumeration both succeeded with
exit code zero. The ESP reported `IsSystem=true`.
This evidence is in
`spoof-handoff/reports/nic-snapshot-fix-20261018/probe-20261018T002800Z-facb39e342/guest-evidence/kernel-metadata.json`.

The revised production snapshot helpers also passed two live read-only VM
captures: the same 148-file inventory, 143 matching ordinary file hashes,
four explicitly classified BCD files, and one mutable BOOTSTAT file. All
four live/exported BCD enumeration results match, and both exports are
separately hash-pinned. Evidence is in
`spoof-handoff/reports/nic-snapshot-fix-20261018/helper-validation-20261018T003055Z-b257cac23d/`.
The new `host-snapshot-tests.ps1` passed 8/8 pure cases, including an
exclusive file lock and a preflight failure with zero adapter calls. The
snapshot preflight is addressed; the independent hash-pinned 84-observation
preflight and its host-path wiring remain required before retry.

## Earlier VM result: e1000e/vmxnet3

The VM used Windows 11 Home build 26200.9445 and four disconnected virtual NICs
(two e1000e, two vmxnet3) configured with the approved original MAC pairs. Guest
PnP IDs differ from the physical host's. The seven-reader gate records:

| Reader | Original | Replacement | Restored |
|---|---:|---:|---:|
| `OID_802_3_CURRENT_ADDRESS` | 4/4 | 4/4 | 4/4 |
| `GetAdaptersAddresses` | 4/4 | 4/4 | 4/4 |
| `GetAdaptersInfo` | 4/4 | 4/4 | 4/4 |
| `GetIfTable2` | 4/4 | 4/4 | 4/4 |
| `Win32_NetworkAdapter.MACAddress` | 4/4 | 4/4 | 4/4 |
| `MSFT_NetAdapter.NetworkAddresses` | 4/4 | 4/4 | 4/4 |
| `Get-NetAdapter.MacAddress` | 4/4 | 4/4 | 4/4 |

| VM phase | Evidence run | Result |
|---|---|---|
| Filter active, service `Running`, four bindings enabled | `uefi-windows-probe-20261017-233834-8d4a1017ed` | 28/28 |
| Filter service and bindings absent; `requestedFilterState=AbsentConfigured` | `uefi-windows-probe-20261017-234025-36406a146b` | 28/28 |
| Normal boot with original filter absent | `uefi-windows-probe-20261017-234313-876dc991d3` | 28/28 |

The earlier VM round trip set per-adapter `NetworkAddress` values through
`Set-NetAdapter -MacAddress` and restarted the tested e1000e and vmxnet3
adapters. Microsoft documents MAC address
setting with [`Set-NetAdapter`](https://learn.microsoft.com/en-us/powershell/module/netadapter/set-netadapter?view=windowsserver2025-ps),
notes that changes can disrupt networking, and documents adapter restart for
properties that need it with [`Restart-NetAdapter`](https://learn.microsoft.com/en-us/powershell/module/netadapter/restart-netadapter?view=windowsserver2025-ps).
Support depends on the adapter and driver; those results do not establish
support for other miniports. The NDIS documentation for
[`OID_802_3_CURRENT_ADDRESS`](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/oid-802-3-current-address)
says network-management software cannot set the current station address through
the NDIS interface library and must set it as a configuration parameter. That
documented path is a lead, not proof that a particular miniport accepts it. The
lightweight filter alone did not provide general reader coverage, and the
earlier VM test did not cover the VMware VMnet miniport. The evidence does not
establish a specific cache mechanism.

The final normal-boot readback reported the original filter absent and all
seven readers matching. Guest code-integrity `testSigning` was false and the
BCD test-signing element was absent; the test certificate was absent from the
LocalMachine Root and TrustedPublisher stores. The address-restore run
`uefi-windows-probe-20261017-234103-82ec2f8e99` confirmed the `NetworkAddress`
registry value absent and all four adapters administratively Up.

## Current same-vendor VMware VMnet gate

A disposable Windows 11 VM used two VMware VMnet miniports (driver version
25.0.0.1) and two vmxnet3 adapters, mapped to the four locked physical-host
MAC pairs. The `vmnet-four-target-address-roundtrip.v1` test applied and
restored typed registry `REG_SZ` values named `NetworkAddress` (12 hexadecimal
characters) and restarted the adapters. This exercised the production
apply/restore helper and avoided the `Set-NetAdapter -MacAddress` method that
returned System Error 87 on physical VMnet8.

All seven current-MAC readers matched the expected original, replacement and
restored value for all four targets (4 targets times 7 readers times 3 states):
84/84 observations, zero failures, with restoration confirmed. The result is
specific to these VM targets and does not prove physical-host replacement. The
vendor-signed VMware miniport was installed only in the disposable VM; host
test-signing, certificate stores, filter, Secure Boot and boot configuration
were not changed for this test. Evidence:

- Round-trip evidence: `spoof-handoff/reports/nic-vm-20261017/uefi-windows-probe-20261018-011305-e1ffeb9e48/evidence/kernel-metadata.json` (SHA-256 `067674BBA247E1AD31FCA99826D4F3A0A67655DF989E1D129B8493117C518695`)
- Independent 84-check/3-negative validation: `spoof-handoff/reports/nic-vmnet-miniport-20261018/vmnet-miniport-20261018T004517Z.3b91e91f/independent-validation/20261018T011552924Z.a02b0f0b/test-run.json` (PASS; validation SHA-256 `89BD0DF8E306A74A254D6019F820D050B78E38FDC52E220139BE1E6060EBC14E`)
- Helper bundle SHA-256: `E7B1B841372B890C0D1638E6CCB11D41B5F257A64FBF491B3FEF811B29A212D8`; native reader SHA-256: `BF553F39E4E7024C75272C24215D995858627AEDD5E946A4B1289A8033DA1642`.

The disposable clone was cold-reverted and powered off after the test. The
recovery record confirms the VMX matches its pre-edit backup
(`BD68D9110A26EC274E0D9AD7FAADFF94F93DEAD53DEE31F4B732F76D660B31F3`), the
protected source VMX hash is unchanged
(`537BBE6D91DFA716D698A5E8301F5C2C59FC98CC65308BC905A9938A3129F0C9`), and
`vmrun` reports zero running VMs:
`spoof-handoff/reports/nic-vmnet-miniport-20261018/vmnet-miniport-20261018T004517Z.3b91e91f/vm-recovery.20261018T0115Z.json`.

The host `Run` path wires the independently tested hash-pinned preflight (84
observations plus three negative cases), the original 112-observation gate,
and the typed `NetworkAddress` apply/restore helper. It also pins the live
VMware `vmnetadapter.sys` driver. The wired
`spoof-handoff/tools/nic_vm/host-address-roundtrip.ps1` SHA-256 is
`9B66BB8E0AC09C24C9DA03CF49E3BDA2E31647B9FF87D587CC29BE6C48575285`.
## Earlier failed gate

The earlier filter-only gate, run `uefi-windows-probe-20261017-223543-a3fed99a59`,
failed: OID and WMI returned replacements for 4/4 adapters, while
`GetAdaptersAddresses`, `GetIfTable2`, `MSFT_NetAdapterNetworkAddresses` and
`Get-NetAdapter.MacAddress` returned originals for all four adapters (16
mismatches). This historical result remains valid for that configuration. The
later seven-reader pass followed per-adapter address configuration and restart;
it does not erase or reinterpret the earlier failure. The pre-write PowerShell
run `uefi-windows-probe-20261017-233408-b76a7226b5` was preserved and recovered.
It failed with `MethodException: Cannot find overload for Add count 1` because
PowerShell's automatic `$Matches` variable collided with the intended matcher;
the code was corrected to use local `classEntries`.

## Prior VM recovery and host boundary

After the earlier e1000e/vmxnet3 gate, the disposable VM was returned to the cold
original snapshot and powered off. This recovery predates the newer VMware
VMnet compatibility run and does not describe its current state.
`spoof-handoff/reports/nic-vm-20261017/recovery.20261017T234420706Z.678838734c0744759b2bfa4b84a8e99f.json`
records recovery, and `spoof-handoff/reports/nic-reader-gap-20261017/vm-final-host-state.json`
records zero running VMs. A fresh after-VM host readback matched all four
original MACs across the seven read-only readers in
`spoof-handoff/reports/nic-reader-gap-20261017/host-originals-after-vm/readback.20261017T234420942Z.0f576271d26f464898a87062c9b9c64c.json`.
A later readback after the canceled host launch again matched all four adapters
across seven readers and Wi-Fi `OID_DOT11_CURRENT_ADDRESS`:
`spoof-handoff/reports/nic-reader-gap-20261017/host-originals-after-canceled-launch/readback.20261018T000509195Z.c0ce6e41886e441798fe4ffc22eb5d9e.json`.
The direct Wi-Fi OID passed; the other three adapters retained N/A with error
87. These are original-value readbacks only, not proof of physical-host spoof
interception. At that earlier stage no physical-host replacement had been
applied, and NIC was the unproven ninth category. The completed host round trip
above supersedes that status without broadening the older readback's evidence.

## Historical source and regression checks

A source-scoped raw-input gate run recomputed 112 observations and passed three
negative cases (altered, missing and duplicate inputs) against its captured
source revision. The test invoked only pure reader-verification helpers; it did
not perform host or VM actions. Results and source hashes are recorded in
`spoof-handoff/reports/nic-reader-gap-20261017/raw-gate-validation/20261018T001149Z.fe8b13147fb34fbfb572e8409ff11ea0/raw-input-gate-tests.json`.

## Current validation status

The host apply/capture/wiring test suite passed 32/32, and the snapshot helper
suite passed 8/8. Related recorded suites passed: NIC journal 191, adapter
helper 53, SMBIOS workflow/snapshot 46, storage 126, mailbox 3, typed apply
helper 15, and independent capture gate 15. The hash-pinned raw-input check
recomputed 84 observations and passed three negative cases; the earlier 112
observation gate remains a separate historical check.

Forty prebuilt CTest tests executed and passed. A separate loader run registered
28 tests: 27 pure tests passed and one service test was `Not Run` because
`ib-nested-e2e-service.exe` was unavailable. The 28 registered tests are not
all claimed green. Evidence:

- `spoof-handoff/reports/nic-vmnet-compatibility-20261018/host-registry-apply-tests.20261018T012014858Z.fcdf558564ee4e1e8fccbb70503a4a2c.json` (32/32; SHA-256 `00EF99897EE83AF965D7E2D455A11A627005EABC286511ECCE5138164E64FB51`)
- `spoof-handoff/reports/nic-snapshot-fix-20261018/host-snapshot-tests.20261018T012009397Z.62cc9300958b4eea89cab19dde6a255f.json` (8/8; SHA-256 `B16028D363736C9B5EE2664F83D1846DE5E36436F76015E5AAF3AA29AE4890EA`)
- `spoof-handoff/reports/nic-vmnet-miniport-20261018/vmnet-miniport-20261018T004517Z.3b91e91f/independent-validation/20261018T011552924Z.a02b0f0b/test-run.json`
- `spoof-handoff/reports/nic-vmnet-compatibility-20261018/ctest-loader-pure.txt` and `ctest-loader.txt`
## Physical-host attempt history

The earlier elevated run `20261018T003613346Z.6dba8117ecf8445e8f61e3ecef3406f3`
failed on the first `Set-NetAdapter -MacAddress` call for VMware Network
Adapter VMnet8 (`ROOT\VMWARE\0001`) with Windows System Error 87:
`Requested operation not supported`. No replacement was applied; the remaining
three targets were not attempted. The run ended `FAILED_BUT_RESTORED`: all four
original adapter values, links and seven-reader results were restored. Wi-Fi
`OID_DOT11_CURRENT_ADDRESS` matched; the three non-Wi-Fi DOT11 queries were not
applicable (error 87). Independent platform comparison passed. Secure Boot
stayed enabled, test-signing stayed off, the test certificate and NIC filter
were absent, and BCD, firmware variables and ordinary ESP files were unchanged.

Evidence:

- `spoof-handoff/reports/nic-vmnet-compatibility-20261018/host-restored-verification.json`
- `spoof-handoff/reports/nic-reader-gap-20261017/host-runs/20261018T003613346Z.6dba8117ecf8445e8f61e3ecef3406f3/`
- `spoof-handoff/reports/nic-vmnet-compatibility-20261018/host-driver-capabilities.json`

Driver strings `NdisReadNetworkAddress` and `Using registry-specified MAC
address.` remain investigation leads; they do not prove that configuration is
accepted by the physical VMnet driver.

## Repeating the completed host test

The same-vendor VM gate, independent 84-observation preflight, host
apply/capture/wiring tests, and the physical-host round trip have passed.
No further run or rollback is needed to complete the recorded proof. The
following commands are retained for an explicit future repeat or interrupted-run
recovery; running the test again changes adapter configuration temporarily.

Open an elevated PowerShell session at the repository root. The script checks
all seven common reader paths for each target and Wi-Fi direct OID where
applicable. It requires Secure Boot on, test-signing off, the test certificate
absent from LocalMachine Root and TrustedPublisher, and the NIC filter absent.
It does not reboot the PC, install a test driver or certificate, or change boot
security settings. Adapter restarts can briefly interrupt connectivity. The
run records the original typed address and administrative state and restores
them automatically at completion.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\nic-host-test.ps1 Run
```

If the run is interrupted or does not complete its automatic restore, use the
exact `runDirectory` returned in its output to recover from the durable journal:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\nic-host-rollback.ps1 -RunDirectory '<exact runDirectory returned by Run>'
```
