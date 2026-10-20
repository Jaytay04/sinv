# GPU diagnostic payload failure investigation

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

## Added diagnostic coverage

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
