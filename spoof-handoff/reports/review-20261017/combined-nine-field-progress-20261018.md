# Combined identifier proof: current evidence

Status at the r5 diagnostic checkpoint: **not ready for host activation**.
The nine categories have separate physical-host round trips, as recorded by the
preceding SMBIOS, storage and NIC reports. A simultaneous replacement run on the
physical host has not been completed.

## Precise blocker from the r5 boot

The bounded parser found a 112-byte structure in the VM's SMBIOS2 table, whose
entrypoint advertises a maximum of 111 bytes. Table lengths agreed at 12,842
bytes and structure counts agreed at 165. Preparation stopped at entry 0 before
replacement-table construction or publication:

```text
prepare_check=smbios2_maximum_structure_size
prepare_status_scope=internal_first_failure
prepare_entry_index=0
prepare_cause_status=0x8000000000000021
prepare_ep_kind=smbios2
prepare_ep_version=2.7
prepare_ep_max_structure_size=111
prepare_actual_max_structure_size=112
prepare_replacement_metadata=0
result=failed
```

- Authenticated diagnostic: `spoof-handoff/reports/combined-9field-vm-20261018/uefi-windows-probe-20261018-042120-51e4efcbe6/evidence/kernel-metadata.json`
- SHA-256: `87163E0C5A7281B8DA3D8BF8CB3F42F635F43B345AD10C363CD3C55D085C9E74`
- Embedded patch report SHA-256: `7D18B3A827DF0F090D0EE961EEA17BDB85E9C7283676C864003422C487FACDB5`

All seven fresh native RSMB/WMI observations remained original. The internal
prepared-page, auxiliary and outer cleanup statuses were zero. The source guard
is retained: [DMTF DSP0134 3.1.1](https://www.dmtf.org/sites/default/files/standards/documents/DSP0134_3.1.1.pdf),
sections 5.2.1 and 6.1.3 and Annex A check 3.7, require the largest complete
structure to fit within that advertised maximum. The parser includes the
string-set terminator in its measurement.

The final diagnostic-only source also omits nonexistent SMBIOS3 declared
structure-count and maximum-structure-size fields. This corrects an ancillary
reporting ambiguity in r5; it does not change any substitution predicate. Its
fresh fixture passed both existing CTests and was not booted or staged to a
machine's ESP. The actually booted r5 artifact and its evidence are preserved.

## Selected scope

Seven SMBIOS fields, the locked Intel `PHYSICALDRIVE0` serial, and four NIC
current addresses (including VMnet1 and VMnet8): 12 device/field bindings in nine
categories. USB disks are excluded. CPU and GPU serial members remain disabled.
Every replacement comes from `reports/client-originals.tsv` (SHA-256
`F628CB25D18F549A877C1529A82986B524BFD3D8B9801A776405F969FC4F42C3`).

The reader matrix uses native RSMB and fresh WMI for SMBIOS, storage IOCTL and
fresh WMI for Disk 0, and seven NIC readers: current-address NDIS OID,
GetAdaptersAddresses, GetAdaptersInfo, GetIfTable2, Win32_NetworkAdapter,
MSFT_NetAdapter.NetworkAddresses and Get-NetAdapter. Wi-Fi DOT11 is checked when
supported. A separate file write/read/hash/remove probe checks file integrity;
it is not an identifier source. These are the tested paths, not every possible
Windows interface or a guarantee about all future application readers.

## Fresh physical-host originals

`combined-host-originals-20261018.json` is an exact copy of the read-only capture
on DESKTOP-5852FUT. It passed 90 reader checks over two complete polls in one
Windows boot. All observations were originals; `simultaneousReadbackPass=true`
in that Original-mode report does not claim replacement activation.

- Original capture: `spoof-handoff/reports/combined-host-20261018/f/c.6c287a78d7c0469d9a4baf6a5209efa8/combined-readback.json`
- SHA-256: `5C16247B579FD5DE12FC84C15B56B6E4B741FF54C8FF8BB68290EABB2AF517B2`
- Read-only policy/absence report: `combined-host-readonly-state-20261018.json`
- SHA-256: `0E5930CF20EA11FB1891F952F2AAC5809A7E7887B35BCF79ED2D20A4B79E457D`

The policy report found no project storage/NIC driver service, no temporary
project signer in Root or TrustedPublisher, and effective kernel test-signing
OFF. Its Windows Secure Boot registry flag was 1; this flag is explicitly not a
fresh firmware-variable proof. No host policy, driver, trust, ESP, firmware or
network setting was changed by this combined investigation. Elevated Prepare
has not run in this agent context.

## VM activation failure

The disposable VMware guest uses Windows build 26200.9445 (physical host:
26200.9711), two CPUs and 4096 MiB. The full attempt proved transparent storage
operation/removal and replacement storage IOCTL/WMI reads, then stopped before
NIC mutation because Windows still exposed original SMBIOS values. It did not
pass the full combined gate.

The separately reviewed r4 diagnostic used the dedicated collector-only Path-B
loader, with Secure Boot already OFF in the VM. No driver, trust, test-signing or
NIC setting was changed in that diagnostic iteration. Its authenticated report
contains the exact failure:

```text
failure_phase=prepare
status_scope=public_call_return
failure_status=0x8000000000000021
rollback_attempted=0
free_attempted=0
cleanup_free_status=0x0000000000000000
result=failed
```

This is the payload's compromised-data return from `EfiSmbiosPrepare`.
The diagnostic metadata's outer `status=PASS` means the read-only capture
completed; the payload's `result=failed` remains the activation verdict.
Publication and its read-back were not reached. The wrapper did not request a
publication rollback. Preparation can perform internal page cleanup; the outer
flags do not prove that no allocation or internal cleanup occurred.

- Diagnostic metadata: `spoof-handoff/reports/combined-9field-vm-20261018/efi-only-r4-20261018T035951Z/kernel-metadata.json`
- SHA-256: `C29D89AB37949B354B0929218CDE2E01442B0C14B0B2A49ADE615B642D363FE4`
- Patch report: same directory, `HypersinvSmbiosPatch.txt`
- SHA-256: `401E13EAD3F1ABEB2E7A0C1214195F1A575810113982A4BBE5D8E3C428532119`

Fresh RSMB and WMI agreed on all seven VM originals. The public phase/status
does not yet identify the internal validation check. An initial progress report
incorrectly called the patch report absent after inspecting truncated output;
direct inspection of `esp.files` established the complete report above.

The staged r4 collector SHA-256 was
`DC5F043364981B36AECB6B0D33DC8B0C98CB65997EC52A4793BA43B7C7E6680B`.
Its build ID and 22 source pins were independently checked. The two existing
SMBIOS CTest suites passed. Source/fixture validation is not a live substitution
pass.

## Recovery evidence

The guest was reverted through VMware's supported API to
`Combined-Clean4-20261018`, cold-booted, checked and powered off. A fresh original
matrix passed 88/88 applicable reader checks. The VM has no Wi-Fi DOT11 target,
which accounts for the two-check difference from the host's two-poll matrix.

- Fresh restored state: `spoof-handoff/reports/combined-9field-vm-20261018/cold-original-20261018T040126Z/kernel-metadata.json`
- SHA-256: `9C984E1135B1F5C006EA0172C88128766B9EB5212D3F2FFC08DACD04EEEACE63`
- Original matrix: `spoof-handoff/reports/combined-9field-vm-20261018/cold-original-matrix-raw-20261018T040229Z/f.0001.json`
- SHA-256: `2F582477B9E36338D73CEFECAD1C5EE52982E65141379F175971F7C67D6E1032`

The VM's restored baseline has SecureBoot bytes `00`, test-signing absent and
effectively OFF, temporary signer absent, no storage filter, no diagnostic ESP
stage/report, BootNext absent, and BootOrder
`050000000100020003000400`. Windows Boot Manager stayed at SHA-256
`25D8869E3C79083AE62C698A9DB9030748E3267BD53593357447F3F72761D1F2`.
This is restoration to the VM baseline, not a claim that the VM had Secure Boot
ON. VMware-generated writable-child/VMX changes were preserved and the disk
chain was checked; byte-identical VMX restoration is not asserted.
The restored firmware enumeration matches its baseline and contains no owned
diagnostic entry. A separate raw `Boot0006` read was not captured at this
checkpoint. Fresh BCD export files differ by 705 bytes despite matching
enumerations; byte-identical BCD exports are not claimed.

## Remaining gates

The failing preparation check is now identified. A conforming VM firmware
fixture is needed before repeating the complete active/restored gate; the
current malformed maximum is not waived to manufacture a pass. The combined host workflow
rejects a missing, failed or stale full VM gate before host mutation.
`docs/COMBINED_HOST_RUNBOOK.md` remains conditional preparation documentation;
its host activation sequence is not ready to run. No simultaneous physical-host
replacement or final physical-host teardown is claimed for this task.

## Final r5 restoration and verification

The final r5 diagnostic was followed by a supported Clean4 snapshot restore,
one cold boot and three read-only checks. BootNext and raw Boot0006 are absent
(Win32 203), the 12 bindings pass all 88 original-value reader assertions, and
the VM was then powered off. This supersedes the earlier r4 checkpoint's raw
Boot0006 evidence limit. The VM baseline SecureBoot bytes remain `00`; no
Secure Boot ON VM claim is made. Stable ESP hashes and BCD enumerations match.
The r5 raw BCD exports differ in 708 byte positions; r4's 705-byte count is a
separate observation. No byte-identical BCD export claim is made.

See `combined-vm-diagnostic-restoration-20261018.md`,
`combined-vm-independent-audit-20261018.md`, `combined-vm-off-20261018.json`,
and the exact extracted report `combined-vm-efi-failure-20261018.txt` in this
directory. Their original report locations and all other retained evidence are
listed with sizes and SHA-256 in `combined-evidence-index-20261018.json`.

Final-source host-values payload validation and both SMBIOS CTests pass. The
VM-profile final2 diagnostic build also passes both CTests, but that final2
image was not booted. All 46 SMBIOS workflow/snapshot pins pass. Combined
orchestration and readback each pass 59 assertions, including explicit USB
exclusion. The complete suite results and their limits are recorded in
`combined-validation-20261018.json`. Existing loader CTests were run against
cached host binaries, not a fresh loader build. No aggregate historical check
count is substituted for these results.

All current-task source changes remain under `spoof-handoff/` in the feature
worktree. Main HEAD, index and 131 pinned loader/collector source files remain
unchanged. Changed-file sizes/hashes are in `combined-changed-files-20261018.json`;
the full Git change summary is `combined-diff-stat-20261018.txt`. Runtime
artifacts, private keys, credentials and VM disks are not committed.

Physical host DESKTOP-5852FUT: 9/9 categories are individually host-proven from
prior runs; 9/9 simultaneous replacements are NOT proven. This task captured
only originals on that host. The next required step is a conforming VM firmware
fixture and a successful complete combined active/restored gate before any host
activation. USB storage remains excluded.
