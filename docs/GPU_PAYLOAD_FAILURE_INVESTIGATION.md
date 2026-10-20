# GPU diagnostic payload failure investigation

## Third physical run: memory-policy preparation aborted

Run `gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7` used source
`933b4c676318f26396cdf2c66de01c3ccbf1a950`. Its 567-byte collected diagnostic,
SHA-256 `DCFDD2F7DA355781F73BDE7D5DA078CF757BD9F17CF2AD17833407FAB9DC2AAA`,
contains a valid returned startup record:

```text
startup_stage=ap_prepare
startup_reason=operation_failed
startup_status=0x8000000000000015
startup_detail=2
startup_expected_cpus=20
startup_prepared_cpus=1
startup_vmx_ready_cpus=20
startup_resource_status=0x00000002
startup_preparation_status=0x00000003
startup_preparation_failure=10
startup_activation_status=0x00000000
startup_activation_failure=0
```

Detail 2 is `AP_MEMORY_MAP`; preparation failure 10 is `MEMORY_POLICY`.
The payload entry executed and returned `EFI_ABORTED` before AP preparation
callbacks, GPU arming, and AP activation. The resource-ready result and
VMX-ready count describe earlier setup and capability checks, not successful
processor launches. The prepared count is initialized to one for the BSP.

The failing block in the tested revision of `HvEfiPrepareApProcessorStates`
combines two pool allocations, `HvEfiGetMemoryMap`, the descriptor-version
check, the EPT memory-type plan, the EPT deny inventory, and the scanner RAM
allowlist. All failures in this block produce the same diagnostic. The saved
run does not contain the firmware memory map, allocation results, deny
inventory, or individual helper verdicts. Consequently, it cannot identify
the rejected predicate or a specific physical hardware cause. Missing EPT
coverage alone selects conservative UC memory; it is not a plan rejection.
The separate PCR replay refusal does not explain this payload return.

### VT-d MMIO inventory overlap confirmed and corrected

A subsequent read-only capture of this host's DMAR table found DRHD register
bases `0xFED90000` and `0xFED91000`. The table is 136 bytes, SHA-256
`3F5B6BAF70DC8CD829B666B7AFA5F7D6F51CDB0A6B84902669467443267E43F8`.
It was retrieved through the Windows
[GetSystemFirmwareTable API](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsystemfirmwaretable)
and its signature, length, record bounds, and ACPI checksum were checked.
The retained capture is
`.analysis/ap-preparation-fix/dmar-5c5eaa9e899a406eaa72e48254f6f3e6/dmar.bin`.
This observation wrote only local evidence; it did not change firmware or boot
selection.

The old deny inventory reserves four 4-KiB pages at each DRHD register base.
Those windows are `[0xFED90000, 0xFED94000)` and
`[0xFED91000, 0xFED95000)`. Its final overlap check therefore rejects this
host's inventory deterministically before AP callbacks can run. This is a
confirmed blocker consistent with the saved `AP_MEMORY_MAP`/`MEMORY_POLICY`
record. The coarse historical record cannot establish whether an earlier
predicate in the same block also failed.

`HvEfiBuildEptMmioDenyRanges` now validates and sorts the firmware MMIO windows
and forms their union before they enter the combined deny inventory. For this
host the result is `[0xFED90000, 0xFED95000)`, five pages. All intended protected
bytes remain denied. Only firmware MMIO windows are normalized; overlap
between that union and CPU/GPU/private allocations still fails the final
ownership check. The helper admits at most eight windows and uses a 64-byte
scratch array. No context layout, C/MASM offset, or loader handoff ABI changed.

The preparation block also records each substep separately and preserves a
failing firmware call's EFI status through cleanup. Existing detail values
retain their meanings; new values are:

| `startup_detail` | Preparation substep |
| --- | --- |
| 10 | Allocate EPT memory-type storage |
| 11 | Allocate deny-range storage |
| 12 | Retrieve the UEFI memory map |
| 13 | Validate memory descriptor version |
| 14 | Build the EPT memory-type plan |
| 15 | Build the combined deny inventory |
| 16 | Build the scanner RAM allowlist |

The host regression exercises the captured address pair, all five denied EPT
pages, accessible neighboring pages, unsorted/disjoint/adjacent/duplicate
windows, the eight-window bound, malformed input and unchanged outputs on
rejection, the 39-bit boundary, and continued rejection of private/MMIO
overlap. Diagnostic tests preserve all seven substep values and their raw EFI
errors through the existing startup record. The native-enabled Release EFI
build passed its `.hvscan` self-containment audit at 54,870 bytes. Build outputs
are isolated under `.codex-build/apfix/h`; original signed run artifacts and
recovery evidence are preserved. A new boot has not yet confirmed physical
startup after this correction, and GPU isolation remains unattested.

The unsigned EFI image is 186,368 bytes, SHA-256
`6EF5A1B4C1BBFF039D1CA1EDEEC8F5B7D6AA8A3C5516C4E6E7E0B6F9B8419FC5`.
It was built for source verification, without signing, staging, or arming it.
All 37 hypervisor component tests passed in 98.38 seconds, including the new
MMIO-union and diagnostic regressions. The CTest log is retained at
`.analysis/ap-preparation-fix/ctest.log`.

### Reset passed; firmware text still differs

Rollback succeeded. FinalVerify at `2026-10-20T20:50:30Z` observed a changed
boot ID, Secure Boot enabled, expected ABSENT health on all 20 CPUs, both
display adapters, and the original Microsoft boot-manager hash. Its only
reported error was the firmware textual comparison. At that checkpoint the
journal remained `Disarmed`; completion and Archive were established later
through the reviewed recovery described below.

Both firmware captures are 4,336 bytes. The backup hash is
`ECB8F364A0058CB7D60B313E3941FCF61760F0C8F814EA188FD9BE5122C478CE`; the final
capture hash is
`4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE`.
Offline comparison verified that all seven blocks agree after accounting for
the following three identifier substitutions and the printed application-block
order. The Windows Boot Manager and both USB application blocks are unchanged.

| Description | Backup identifier | Final identifier |
| --- | --- | --- |
| UEFI:CD/DVD Drive | `1eb2958e-ccbc-11f1-8b18-806e6f6e6963` | `a81bb0ad-ccc7-11f1-8b1b-806e6f6e6963` |
| UEFI:Removable Device | `1eb2958f-ccbc-11f1-8b18-806e6f6e6963` | `a81bb0ae-ccc7-11f1-8b1b-806e6f6e6963` |
| UEFI:Network Device | `1eb29590-ccbc-11f1-8b18-806e6f6e6963` | `a81bb0af-ccc7-11f1-8b1b-806e6f6e6963` |

These are BCD identifiers, not raw UEFI Boot numbers. The installer receipts
report that Arm selected owned `Boot0006` and rollback cleared owned one-shot
state without changing BootOrder. There is no saved raw pre-arm BootOrder
snapshot, so historical raw restoration cannot be proved from these records.
The comparison result does not justify deleting these generic entries,
rewriting the original baseline, or treating the failed FinalVerify as passed.

`Inspect-GpuFinalFirmware.ps1` collects a separate observation for this exact
failed run. It pins the three imported modules, audit tool, health probe,
reference log, and both reviewed firmware captures. It requires the same host,
the post-disarm reset boundary, fresh 20-CPU ABSENT health, exact current
mounted-ESP firmware text, and successful raw boot-selection checks before and
after health. It releases only its owned temporary ESP mount and verifies that
the selector, journal, and pinned inputs stayed unchanged. The raw audit scans
BootNext and entries referenced by BootOrder; it does not enumerate orphan
Boot variables. Its result explicitly leaves `RecoveryCompleted` false.

Seven mocked cases with 18 assertions passed, covering preview, journal
selection, successful capture, health failure, unmount failure, raw selection
change, and failure of the second firmware capture. A separate non-admin
preflight reached the expected administrator guard after validating the actual
saved inputs; no raw capture or host query ran, and selector/journal hashes
remained unchanged. Elevated physical-host observation remains an operator
step. These checks do not mark FinalVerify successful or authorize Archive.

### Read-only firmware inspection passed

The operator's inspection succeeded. The result at
`.analysis/gpu-final-firmware-review/8abb7b84ca644a7c84019a1f29b35bb4/result.json`
is 16,924 bytes, SHA-256
`BEA6DCA1FC904B13830908471D6239A906F71AA6937630B32CBA6DC3C2C023D9`.
The two 259-byte raw boot-selection captures both hash to
`B5D4EB51A58A2170EBCA905749E2BDAD177CFD71629A1079A14934F3F5BF63D0`.
They report BootNext absent, BootOrder `[0,1,2,3,4,5]`, expected boot-manager
and reference-log hashes, and zero findings. Both accompanying integrity
commands exited zero. Fresh health passed expected ABSENT on all 20 CPUs;
Secure Boot remained on, the reviewed firmware text stayed stable, and owned
ESP cleanup succeeded. The selector and journal hashes stayed unchanged.

This evidence supports a bounded review of the known textual difference.
`FinalVerify -ReviewedFirmwareDifference` requires this exact pinned receipt,
run, original/current capture pair, and post-reset session, plus ordinary final
checks and fresh raw observations. It leaves textual equality false and records
review acceptance separately. Neither the source baseline nor the failed
FinalVerify result is rewritten. The default FinalVerify remains strict.
No new physical boot, TPM operation, firmware write, or GPU activation is part
of this recovery review. Historical raw equality and unreferenced Boot
variables remain outside the evidence's scope.

The review helper's fixture checks passed 5 cases with 19 assertions; platform
verification passed 18 cases with 406 assertions, and dispatcher/state checks
passed 51 cases with 514 assertions. The actual saved receipt and its cited
evidence also passed offline validation with no native query or journal change.
The opt-in preview reports no host changes. Fresh native observations and the
final journal transition still require the operator's elevated invocation;
these development checks do not themselves complete the run.

### Reviewed verification interrupted by a module reload

The reviewed FinalVerify action at `2026-10-20T21:26:30Z` saved fresh 20-CPU
ABSENT health, matching reviewed firmware text, two identical raw boot-selection
captures with zero findings, and successful boot-manager integrity commands.
Its owned ESP unmount receipt also succeeded. Journal completion and the
failure receipt then failed because `Write-GpuJson` was no longer visible in
the dispatcher. The journal retains phase `Disarmed` and the interrupted
FinalVerify intent; these observations do not establish completion.

Isolated Windows PowerShell reproduction with the actual modules identified
the cause: force-importing `GpuHostState.psm1` from a nested module removes
its exports from the caller. The retirement operations factory performs that
same forced import. The repair reuses loaded dependencies and builds the
review callbacks around existing private capture functions, leaving the
pinned retirement module unchanged. A regression test exercises the actual
wrapper and public helper imports, checks caller command visibility, and
writes fixture journal and failure JSON through the real state writer.
All 17 assertions passed, including the replacement capture callback wiring.
Restoring the forced state import in the isolated module copy reproduced the
missing-writer failure, confirming that the regression test detects the bug.

The saved review receipt and its cited immutable evidence still pass offline
validation without journal changes or native queries. Retry
`FinalVerify -ReviewedFirmwareDifference` through the normal dispatcher; its
existing recovery rule accepts interrupted FinalVerify in phase `Disarmed`.
No manual journal repair, baseline replacement, new rollback, or additional
reset is required for this writer failure.

The operator's retry completed at `2026-10-20T21:37:47Z` with
`FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED`, success,
phase `Complete`, no active intent, and no verification errors. The saved
record retains `TextEqual: false` and records review acceptance separately.
Archive then returned `SELECTOR_ARCHIVED`; the archive exists, the active
selector is absent, and the completed journal is preserved. This closes the
run's recovery workflow without attesting GPU isolation or resolving the
historical raw BootOrder limitation.

## Second physical run: entry returned EFI_ABORTED

Run `gpu-20261020T150318Z-59a346ed3cb8422bafe4be60268b7df1` used clean source
`d79806d51187963cd81578b7287076e17558c2e2`. Unlike the first run below, its
collected launch diagnostic selects the failing phase:

```text
schema=1
payload_launch=start
payload_status=0x8000000000000015
shim_load_phase=firmware_load
shim_load_status=0x0000000000000000
```

Firmware image loading succeeded. The resident launcher then called the
relocated PE entry directly, and that entry returned `EFI_ABORTED`. This path
does not call firmware `StartImage`; its `ExitData` cannot explain this failure.
The diagnostic file was 130 bytes, SHA-256
`7E7C99AF5816BA4539E070A3AF80AB89F67870AA4020384BF720157DAA97651A`.
The collected payload matched the staged signed image, SHA-256
`242DF01C0B66C4DB48EB04DD1ED954F35DE657462D63637DDEE50AE991CF2FED`.

The operator's boot photo shows SHA-256 PCRs 0 through 7 as nonzero. Each `1`
in `11111111` means that at least one digest byte is nonzero. It does not mean
the digests contain all `FF` bytes. The loader's PCR guard therefore refused
replay. `IB_PAYLOAD_REPLAY_INDEPENDENT=ON` allowed the separate payload attempt,
so replay refusal alone does not explain its returned error. No TPM clear or
weaker PCR check is justified by this evidence.

The exact internal startup check remains unknown. BSP binding, private paging,
state invariants, and AP resource/preparation failures can return the same
`EFI_ABORTED`. The existing hypervisor startup messages use COM1 and port
`0xE9`; this run has no captured serial trace. Failure cleanup frees the context
containing the preparation result. Later fail-stop paths halt instead of
returning, so a clean return to Windows does not select one of those paths.

After rollback and the operator's reset, FinalVerify passed expected ABSENT
health on all 20 CPUs, Secure Boot, the boot-manager hash, and the textual
firmware baseline captured with the ESP mounted at `S:`. The selector was
archived and the journal remains `Complete`. GPU isolation remains unattested.
The retained seven-file ESP stage and all run evidence must be preserved. The
older retirement helper is pinned to the first run and must not be used on
this run.

The sections below retain the first investigation and its earlier diagnostic
coverage. The second run narrows the failure to the payload's internal startup;
it does not establish a specific DMA, EPT, GPU, or CPU failure.

## Observed run and recovery

Run `gpu-20261020T031023Z-9a2a92a17bf54b4faf4b60c6b7231ec7` was prepared from
`c6e65a2b265d1f3c13ac83bfe61c69281907c003`. The operator performed the physical
boot. This investigation used saved copies and source; it did not execute
another EFI payload or change host boot configuration.

The signed payload was 185,192 bytes, SHA-256
`3CB7AC7866CB296C5E6075D1FD299C482A86BE906DF806FBBE1D90B12C4D0E31`.
Stage readback, Arm, and the collected ESP copy agree on these bytes. The
collected `esp-handoff-status.txt` contains:

```text
schema=1
state=READY_BOOT_MANAGER
replay=ABORTED
restore=PASS
payload=FAILED
exit_boot_services_calls=0
```

The post-boot probe reported the test hypervisor absent on all 20 logical CPUs.
After disarm and reset, FinalVerify returned
`FINAL_RESET_VERIFIED_GPU_UNATTESTED`, with Secure Boot enabled, expected ABSENT
health, unchanged Windows Boot Manager, and matching textual firmware
enumeration. The journal is `Complete`. These are recovery checks, not evidence
of successful payload execution or GPU isolation.

Evidence is under `.analysis/gpu-baremetal/runs/<run-id>` in the primary checkout.
The relevant action directories are `20261020T032900399Z-Collect-*` and
`20261020T035702484Z-FinalVerify-*`. Preserve the journal and receipts.

## Diagnosis and its limits

The exact failure cause cannot be recovered from the saved summary. The
production caller in `components/uefi-loader/src/loader/efi_entry.c` discarded
the launch category and raw `EFI_STATUS` returned by `image_launch.c`.
`shim_chain.c` also retained its load gate/status only in memory. Several
failures produce the same `payload=FAILED` value: a filesystem presence/read
error, PE validation, device-path construction, shim/firmware image loading,
loaded-image validation, or an EFI error returned by the payload entry point.
There is no saved host serial or console trace selecting one of these paths.
The constant `exit_boot_services_calls=0` is not a measured child counter and
cannot establish whether its entry point ran.

The deployed loader cache has `IB_PAYLOAD_REPLAY_INDEPENDENT=ON`; its payload
branch runs despite replay refusal. The remembered gate/replay-aborted message
does not establish the reason for the payload failure. The matching collected
payload also does not support a missing or altered staged image as the cause.

Source-supported native startup candidates include strict DMA admission,
allocation failures, PAT/MTRR compatibility for WC mappings, and an already
active firmware VT-d unit. In particular, `iommu_runtime.c` rejects units with
TES/QIES/IRES already set; physical IOMMU preparation maps installation failure
to an EFI error. None is established as the cause of this boot. Do not weaken
those checks or disable platform protection based on these candidates. An
ordinary GPU discovery `EFI_UNSUPPORTED` is optional in `efi_hypervisor.c` and
does not by itself explain failure of the entire payload.

VM run 21 used different artifacts, native GPU/output confirmation off, and
Secure Boot off. It passed EFI/root WC rendering, model/latch checks, and
four-CPU Windows health with VMware SVGA and build 26200.9457. The physical run
used native/output confirmation on, Secure Boot on, 20 CPUs, and build
26200.9711. The VM result does not validate the failed signed native-enabled
configuration. Real UHD 770 scanout, physical Intel DMA isolation, and the
dual-adapter topology remain unverified.

## Initial launch diagnostic coverage

The loader now prints and attempts to write the optional
`EFI\insecure-boot\payload-diagnostic.txt` before inventory and Windows handoff.
Its bounded, allocation-free formatter records five ASCII lines, for example:

```text
schema=1
payload_launch=load
payload_status=0x800000000000001A
shim_load_phase=security2_lookup
shim_load_status=0x800000000000000E
```

This example is not a result from the failed boot. The legacy handoff status
schema/bytes are unchanged. Diagnostic errors cannot change the launch verdict
or bypass trust, ownership, DMA, or WC checks. A write error is printed and
does not prevent the existing Windows handoff. Collect preserves the optional
file with a verified hash; older runs without it remain collectable. The file
is not GPU attestation or a freshness guarantee: if writing failed, an existing
file may be stale. Correlate it with console output and the reviewed stage/run.

Launch phases are `skipped`, `absent`, `presence_error`, `argument`, `not_found`,
`read`, `device_path`, `image_format`, `load`, `protocol`, `start`, and `ok`.
In the resident path, `start` means the direct payload entry returned an EFI
error; firmware StartImage is not used there. The shim trace is captured before
another child load overwrites it. Pre-load failures use `not_applicable` and
`EFI_NOT_STARTED`, so an older load trace is not attributed to the payload.

Headless tests cover exact records, raw errors, bounds, stale-trace handling,
resident failure cleanup, and optional evidence capture. No new VM or physical
boot was performed for this instrumentation. Future disposable-VM validation
should exercise the signed Secure Boot chain with native build options; absent
Intel hardware must still be unsupported. If a later diagnostic reaches
`start`, hypervisor startup-stage output is still needed to distinguish its
internal checks. No further recovery command or physical retry is required
for the completed run.

The instrumentation revision built successfully with MSVC as an unsigned
loader. CTest passed 29/29 loader and 32/32 hypervisor checks. The existing
signed hypervisor payload passed its static `.hvscan` audit at 54,870 bytes;
its binary, renderer, and scanner sources were unchanged. The new loader was
not signed, staged, or executed.

## Startup diagnostic extension

The loader now supplies an optional versioned 80-byte record through the
child's `EFI_LOADED_IMAGE_PROTOCOL.LoadOptions` during the synchronous direct
entry call. It restores both original LoadOptions fields immediately after
return, validates the record against the actual returned EFI status, and copies
valid data before unloading a failed child. The existing resident launch API
and the original five schema-1 lines remain compatible.

New launched children append `startup_record=valid`, `unavailable`, or `invalid`
to `payload-diagnostic.txt`. Only a valid record appends startup version, stage,
reason, status, observer processor index, detail, CPU counts, and resource,
preparation, and activation result fields. Output is bounded to 1,024 bytes.
An old payload that leaves the initialized record untouched is unavailable;
missing or malformed data cannot be presented as a known startup stage.

The BSP records setup phases from context allocation through AP preparation and
VMX launch. Safe failure paths snapshot scalar results before cleanup frees
the context, then mark a returned result only after cleanup completes. The
success path takes its context snapshot before VMLAUNCH because that context
is EPT-denied after successful guest return. AP callbacks and resident VMX-root
code do not access the loader-owned record. No context ABI, resident scanner,
renderer, admission checks, or fail-stop behavior is changed.

`startup_processor` identifies the observed BSP/current processor, not
necessarily a failing AP; `4294967295` means unavailable. Counts and status
fields are the existing context snapshot, not new per-CPU attestations.
`startup_detail` is stage-dependent: at `dma_admission` it is the raw DMA
admission-state value; at `ap_prepare` it identifies a bounded BSP substep
defined in `efi_boot_diagnostic.h`. Preparation-failure numbers retain their
existing meanings in `efi_hypervisor.h`. These values narrow a returned error
without authorizing a weaker check.

This transport cannot persist a fail-stop or a boot that never returns to the
loader. File-write failure can still leave a stale diagnostic file. It cannot
retroactively recover the second run's missing startup reason or prove GPU
ownership, scanout, or DMA isolation. Another physical boot is not part of this
investigation increment.

The native-enabled Release payload and loader built with MSVC 19.51.36248.
The loader used replay independence on and debug ports off, matching those
options in the second physical run. CTest passed 37/37 hypervisor checks and
30/30 loader checks. The unsigned payload passed the `.hvscan` self-containment
audit at 54,870 bytes. Separate mutation checks confirmed that the lifetime
contract test rejects an AP diagnostic write, premature cleanup-return marker,
and a context dereference after successful guest return. These initial static
artifacts were not signed or booted. Subsequent VM validation uses separate
signed copies with loader debug ports enabled, as recorded below.

## VM validation transport failure

Candidate `d4c8101c406694d2cec7c80343f4586c33ad3f37` built and signed through the
existing VM-only workflow, passing both component suites and unsigned/signed
image audits. A fresh independent copy of the reviewed offline Windows VM
verified all 93 files, totaling 58,094,667,483 bytes. Validation run
`gpu-payload-20261020T182528Z-7d1ce3dea92145b6840364370c03dbcb` then passed
guest inspection but failed while returning the Stage result:

```text
vmtoolsd info-set failed: Total guest info exceeds maximum allocated space
```

The request ZIP was 762,672 bytes, transported as 1,016,896 Base64 characters
across 85 chunks. The worker retained those request values while publishing
response evidence. The saved authenticated failure identifies an `info-set`
failure during response publication, but does not identify the failed chunk
or manifest write. The subsequent diagnostic request also failed to allocate
GuestInfo space. This was a VM transport failure before the controller's signed
EFI stage, not evidence of another payload startup rejection.

The controller created before/after snapshots, reverted to before, and confirmed
the VM off with no cleanup failures. The failed run and its used preparation
receipt were archived; the VM and all evidence remain on disk. Its result is
`FAIL`, SHA-256
`B4B47BE2E020E4BA152C8376AFA193ABA98C6025EE397C76175F4D84E2592B5F`.

The follow-up transport change releases only the authenticated Stage request's
current-RunId chunk values after the VM/SYSTEM and package checks pass, before
publishing setup diagnostics. The verified local package remains available.
The controller also clears a saved failed response's owned mailbox values
before requesting Diagnose, after preserving any late inspection evidence.
The focused controller suite passes 114 pure
checks, including exact chunk names, rejection without writes, write failure,
cleanup ordering, and producer/consumer chunk-size agreement.

The subsequent fresh VM execution at candidate
`06a13691ccbaa328c00789ed5c4688af3b50b015` passed Stage after releasing its
85 consumed request chunks, then passed signed EFI execution and authenticated
four-CPU Windows health. Its 545-byte payload diagnostic reports a valid
version-1 record, `startup_stage=return`, `startup_reason=none`, and EFI status
zero. The EFI serial and authenticated Windows copies contain the same startup
fields. Both snapshots were created, baseline revert completed, and the VM was
confirmed off without cleanup failures. Graceful shutdown timed out and used
the bounded hard-stop fallback; graceful shutdown remains unvalidated.

See [the startup diagnostic VM result](GPU_STARTUP_DIAGNOSTIC_VM_RESULT.md)
for pinned artifacts, paths, and limits. This validates the new diagnostic
transport in the nested VM. It does not recover the original physical
rejection reason or validate Intel physical GPU isolation. The archived
physical run's journal and archive hashes remain unchanged.
