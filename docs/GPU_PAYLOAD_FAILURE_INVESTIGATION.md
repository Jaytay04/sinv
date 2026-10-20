# GPU diagnostic payload failure investigation

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
cleanup ordering, and producer/consumer chunk-size agreement. A fresh VM
execution must still verify that tombstoning
these values releases sufficient GuestInfo capacity.
