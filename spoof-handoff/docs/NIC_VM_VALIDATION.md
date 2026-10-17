# NIC device fingerprint validation

This note records host-baseline capture and disposable-VM validation for the
kernel NIC reader path. It does not establish physical-host spoof readiness.
The VM gate requires every reader path to match, not only OID and WMI.

## Host baseline and VM boundary

Four approved host adapter MAC mappings were captured with original values
agreeing across native OID reads, `GetAdaptersAddresses`, `GetIfTable2`, WMI
(`Win32_NetworkAdapter`) and `Get-NetAdapter`. The captured baseline is
`../reports/nic-vm-20261017/host-originals-retry/readback.20261017T220316561Z.534ca308733c42848cbc25e34f05a9fd.json`.

Validation used a disposable Windows 11 Home VM (build 26200.9445) with 4096 MiB,
two CPUs and four disconnected virtual NICs: two e1000e and two vmxnet3. The VM
NICs used the exact approved original MAC pairs. Their guest PnP IDs differ from
the physical host's, so these results are guest-path evidence, not proof about
physical host adapters. The source VM was unchanged; the clone hash is recorded
in `../reports/nic-vm-20261017/clone.json`.

The recovery point was the cold snapshot `DeviceFilter-Nic-ColdOriginal`; the
policy-prepared state was `DeviceFilter-Nic-PolicyPrepared`. A live snapshot
attempt timed out after 180 seconds and was preserved. The tested offline
recovery completed with status `RECOVERED_AND_POWERED_OFF`, recorded in
`../reports/nic-vm-20261017/recovery.20261017T224309329Z.d9c95081e85b474aadb823c1a90a7103.json`.
From the repository root, the recovery command was:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\tools\nic_vm\recover.ps1
```

This command manages the disposable VM only.

## Driver and reader results

The first driver registration attempt used the actual NDIS status
`0xC0010005` (`NDIS_STATUS_BAD_CHARACTERISTICS`), which SCM reported as error 5
(`Access is denied`). This was not a certificate-policy failure. Updating the
driver characteristics revision and structure size to revision 2 allowed the
`DriverEntry` call to succeed and the service to reach `Running`.

With the corrected default profile, diagnostics are off. Revision 1 is retained
only as an explicit regression profile; no other fields or default MAC mappings
changed. The driver was signed with the existing test certificate
`59DBD6780CFE60C020B3F4511211FEC22C397B85` and trusted only inside the guest for
test signing. No host trust or policy, network, ESP, BCD or firmware settings
were changed.

Transparent mode passed all six reader paths while the driver was running and
all four bindings were enabled; each returned the four original values. Service,
bindings and the driver's own INF were then removed successfully. In enabled
mode, fresh OID and `Win32_NetworkAdapter.MACAddress` reads returned all four
locked replacements.

After reboot, the first demand-start attempt found the service stopped and the
runner refused before collecting a count or readback. An explicit service start
succeeded with status 0 and the driver reached `Running`; its kernel binary hash
was `A089523FEB5F3F327B5565BF1D962DA65489FAAEE12E7C78B043A11E1E3844BF`. The
pinned VM profile matched and all four bindings were enabled.

The decisive fresh readback is in
`../reports/nic-vm-20261017/uefi-windows-probe-20261017-223543-a3fed99a59/evidence/kernel-metadata.json`;
`allTargetsMatch` is false. The six-path results were:

| Reader path | Replacement values |
|---|---:|
| OID `OID_802_3_CURRENT_ADDRESS` | 4/4 |
| `Win32_NetworkAdapter.MACAddress` | 4/4 |
| `GetAdaptersAddresses` | 0/4 |
| `GetIfTable2` | 0/4 |
| `MSFT_NetAdapterNetworkAddresses` | 0/4 |
| `Get-NetAdapter.MacAddress` | 0/4 |

The four ordinary adapter API paths returned originals in all four cases. The
evidence records 16 exact mismatches; for example, the first
`GetAdaptersAddresses` value was expected to be `02:90:10:20:30:40` but was
`00:50:56:C0:00:08`.

The VM deployment gate failed. OID rewrite provides partial reader coverage,
while ordinary adapter API reads remain original. The evidence does not prove
an exact cache or other cause. A mechanism that covers those reader values and
a fresh passing VM gate are required before host deployment. No host activation
runbook is supported by the current evidence.

## Cleanup and host boundary

After enabled-mode removal, the VM record reported `phase=Readback`,
`expectedMode=Original`, `requestedFilterState=Absent` and `allTargetsMatch=true`:
four adapters matched across all six reader paths,
with the service and bindings absent. Guest kernel code-integrity status was 0
with options `0x5`; test signing was false, the BCD test-signing element was
absent, and guest Secure Boot was false, matching the original snapshot baseline.
This readback is recorded in
`../reports/nic-vm-20261017/uefi-windows-probe-20261017-224208-c896c3345f/evidence/kernel-metadata.json`.
The recovery record confirms the disposable VM was powered off at its cold
original snapshot.

No physical-host NIC activation was performed. After VM cleanup, all four host
original MACs matched across native OID, `GetAdaptersAddresses`, `GetIfTable2`,
`Win32_NetworkAdapter` and `Get-NetAdapter` in
`../reports/nic-vm-20261017/host-originals-after/readback.20261017T224236496Z.8c62b1ef3df143ad99cd6069adf81a66.json`.
This confirms original-value readback after the VM test; it does not prove host
spoof interception. Host evidence covers eight of nine validation categories,
so all nine categories and host readiness are not claimed. No byte-for-byte host
BCD, ESP or firmware verification is claimed.

The recorded final state is `../reports/nic-vm-20261017/final-state.json`: the
source VM hash was unchanged, no VMs were running, and NIC and storage test
services were absent.

## Related verification and evidence

The recorded host-side suites passed: 49 aggregate tests, storage 126,
identifier 8, SMBIOS 46 and loader 27; the new PowerShell profile has 177 pure
assertions. The new profile's CTest run passed 2/2 in both modes. These software
checks do not replace the failed all-reader VM gate.

The curated validation record is
`../reports/review-20261017/nic-vm-validation.json`; raw evidence checksums are
in `../reports/nic-vm-20261017/SHA256SUMS.txt`.
