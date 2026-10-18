# NIC reader-path validation

**Current result (2026-10-17): `VM_SEVEN_READER_PASS_HOST_PENDING`.** The follow-up passed
all seven MAC readers on four disposable-VM adapters through original,
replacement and restored values: 28/28 adapter-reader checks in each state. No
physical-host replacement was executed; host proof remains 8/9 categories, with
NIC unproven.

Evidence paths below are relative to the main repository root; implementation source is in the feature worktree.

## Current VM result

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

The validated round trip sets each per-adapter `NetworkAddress` value through
`Set-NetAdapter -MacAddress` and restarts the adapter. Microsoft documents MAC address
setting with [`Set-NetAdapter`](https://learn.microsoft.com/en-us/powershell/module/netadapter/set-netadapter?view=windowsserver2025-ps),
notes that changes can disrupt networking, and documents adapter restart for
properties that need it with [`Restart-NetAdapter`](https://learn.microsoft.com/en-us/powershell/module/netadapter/restart-netadapter?view=windowsserver2025-ps).
Support depends on the adapter and driver; these results cover the tested VM
profile. The NDIS documentation for
[`OID_802_3_CURRENT_ADDRESS`](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/oid-802-3-current-address)
says network-management software cannot set the current station address through
the NDIS interface library and must set it as a configuration parameter. The
successful per-adapter configuration supplies that setter; the lightweight
filter alone did not provide general reader coverage. The evidence does not
establish a specific cache mechanism.

The final normal-boot readback reported the original filter absent and all
seven readers matching. Guest code-integrity `testSigning` was false and the
BCD test-signing element was absent; the test certificate was absent from the
LocalMachine Root and TrustedPublisher stores. The address-restore run
`uefi-windows-probe-20261017-234103-82ec2f8e99` confirmed the `NetworkAddress`
registry value absent and all four adapters administratively Up.

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

## Host boundary and recovery

The disposable VM was returned to the cold original snapshot and powered off;
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
interception. Physical-host replacement remains unexecuted, and NIC remains
the unproven ninth category. No byte-for-byte host BCD, ESP or firmware
verification is claimed.

### Prepared physical-host round trip

The VM gate is complete and passed, but no host test ran. The UAC launch was
canceled before the elevated script started at `2026-10-18T00:02:35.0022787Z`
with the error “This command cannot be run due to the error: The operation was
canceled by the user.” The record is
`spoof-handoff/reports/nic-reader-gap-20261017/host-launches/20261018T000020922Z.1ee17cb890ed4b2a8788646a07e9d376/start-error.json`;
there is no host `start.json`, result or run directory. NIC remains unproven on
the physical host (8/9 categories). Re-run only after opening PowerShell as
administrator and changing to the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\nic-host-test.ps1 Run
```

The command uses the built-in per-adapter MAC configuration. It requires Secure
Boot ON, Windows test-signing OFF, the test certificate absent from LocalMachine
Root and TrustedPublisher, and the NIC test filter absent. It does not install a
test driver or certificate, change OS security settings, or reboot. Wi-Fi and
VMware adapters may briefly disconnect; the run requires their original link
states to return within 30 seconds and does not change SSIDs, IP settings or
network profiles. Firmware, BCD and ESP state are captured before and after;
`EFI/Microsoft/Boot/BOOTSTAT.DAT` is the only ESP comparison exclusion as
Windows may update boot status independently. The seven common readers and
Wi-Fi direct `OID_DOT11_CURRENT_ADDRESS` must match in original, active and
restored states; a reader unavailable in the original capture must retain that
exact unavailable status.

`Run` returns a unique `runDirectory` beneath
`spoof-handoff/reports/nic-reader-gap-20261017/host-runs`. Preserve that exact
directory for journal-based recovery if needed:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\spoof-handoff\commands\nic-host-rollback.ps1 -RunDirectory '<exact runDirectory returned by Run>'
```

Rollback restores the original typed `NetworkAddress` value and adapter
administrative state from the durable pre-mutation journal. The final host gate
independently recomputes 112 observations from the captured native and Windows
reader inputs. Its pure test passed the real four-state comparison and three
synthetic negative cases (altered, missing and duplicate inputs), without
executing host or VM actions. Results are recorded in
`spoof-handoff/reports/nic-reader-gap-20261017/raw-gate-validation/20261018T001149Z.fe8b13147fb34fbfb572e8409ff11ea0/raw-input-gate-tests.json`.
Independent suites passed: NIC journal 191, adapter helper 53, SMBIOS workflow
and snapshot 46, storage model 126, and 40 related CTest cases. The earlier
15 AST/mock checks retain their original source hash. C# helper compiles
passed 2/2; the native reader build used `/W4 /WX`.

The curated result is `spoof-handoff/reports/review-20261017/nic-reader-gap-validation.json`
(`VM_SEVEN_READER_PASS_HOST_PENDING`); immutable gate data is
`spoof-handoff/reports/nic-reader-gap-20261017/vm-seven-reader-gate.json`. Raw evidence
checksums are in `spoof-handoff/reports/review-20261017/nic-reader-gap-SHA256SUMS.txt`.
