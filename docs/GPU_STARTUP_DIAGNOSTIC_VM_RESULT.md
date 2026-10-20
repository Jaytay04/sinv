# Signed VM startup diagnostic result

Run `gpu-payload-20261020T185534Z-f8492b4e91a744639d8617dd3978dfed` completed
with `SIGNED_NATIVE_VM_VALIDATION_PASS` on source commit
`06a13691ccbaa328c00789ed5c4688af3b50b015`.

The disposable VMware guest passed Inspect, Stage, SignedNativeEfi, and
WindowsStartup. Windows reported 4/4 CPUs healthy with Secure Boot enabled;
the original Windows boot manager hash matched its baseline. The guest
observed the expected certificate DER bytes in firmware `db` through a bounded
raw-byte search, and the VMware
log contained the certificate append marker. These observations validate this
VM's signed EFI execution path and firmware setup. Windows Authenticode chain
status remained `UnknownError` because the signer root is not trusted by the
Windows trust provider; the test's pinned signature verifier passed. This VM
trust path is separate from host MOK trust.

The captured 545-byte payload diagnostic contained a valid version 1 startup
record with entry and return flags, `stage=return`, `reason=none`, and status
zero. It recorded 4 expected, 4 prepared, and 4 VMX-ready CPUs; resource,
preparation, and activation statuses were 2, with zero failure codes. This is
evidence of successful return from the instrumented startup
path. The loader reported `replay=ABORTED`; the independently enabled
payload attempt then passed. The replay refusal and launch outcome were
separate in this VM.

```text
payload_launch=ok
startup_record=valid
startup_flags=0x00000003
startup_stage=return
startup_reason=none
startup_expected_cpus=4
startup_prepared_cpus=4
startup_vmx_ready_cpus=4
startup_activation_failure=0
```

The build passed 37 hypervisor and 30 loader component checks, plus 114 focused
controller checks. Unsigned and signed `.hvscan` audits each passed
self-containment for the 54,870-byte resident section.

The Stage guest released all 85 authenticated request chunks before publishing
its response. This resolved the preceding run's VMware GuestInfo capacity
failure. The earlier failure was a transport-publication failure before EFI
execution, not evidence of a VM memory shortage.

The controller created its before/after snapshots, reverted to the before
snapshot, confirmed the VM off, and reported no cleanup failures. Graceful
shutdown timed out and the controller used its hard-stop fallback; graceful
shutdown was not validated. Screen capture was unavailable because VMware
rejected anonymous guest operations. Serial logs and authenticated guest
evidence were available.

## Scope and limits

This result validates signed startup and return in an offline four-CPU,
8-GiB, nested-virtualization VM running Windows build 26200.9457 with no
passthrough. It does not explain the exact EFI_ABORTED branch from the earlier
physical run, prove Intel physical GPU ownership or scanout, test host MOK
acceptance, or establish host DMA isolation. A startup record also cannot be
returned by a nonreturning fail-stop path. The physical host was not booted or
modified for this validation.

The completed physical run remains archived. Its archive and journal pins
matched the verified values; the retained ESP stage and host firmware/security
state were not changed during this VM validation.

## Preserved evidence

Paths are relative to the main workspace. The independent VM copy verified
all 93 files, totaling 58,094,667,483 bytes. Earlier VMs remain on disk.

- VMX: `.codex-build/gpu_payload_vm/vm-20261020T184950Z-3845cd62/clone/hsinv-Windows-25H2.vmx`.
- Workflow: `.analysis/gpu-payload-diagnostic/runs/gpu-payload-20261020T185534Z-f8492b4e91a744639d8617dd3978dfed/`.
- Controller: `.codex-build/gpu-payload-vm/gpu-payload-20261020T185534Z-f8492b4e91a744639d8617dd3978dfed-03ee1f2292c7475ba9e5afe7cf101241/`.

Snapshots were `before-gpu-hal-c589bd89` and `after-gpu-hal-2c480066`.

| Evidence | SHA-256 |
| --- | --- |
| Controller result, 22,614 bytes | `B5DA29E0C0B8630AD8C3EFDAD6B73DDD02965BDE3AED0710B4506B901CE8661E` |
| Signed payload, 187,240 bytes | `F6DEB68359B26F00D1ECDE768459201FC86F67F7F18E0E0186870F43C9B15A20` |
| Signed loader, 65,384 bytes | `118FD525331822D39EB006A756B9A3FF4563FE92FA27DF0E1169442C9F954927` |
| EFI serial log, 2,588 bytes | `529FA1BFEB7607C3507C14EF694E1464F3394DC56336FC870DA6526D274300DF` |
| Windows session log, 55,381 bytes | `E373AC82980DE9E0C019ADDB0DC29728C120C3C81775F12D5106C26A3CC9E4A4` |
| VMware log, 379,230 bytes | `DEFF75A96EB8377B20B2D5B7D991F0C41F1E8A6B958D803BB035FE53FD4E58FB` |
| Workflow journal, 19,084 bytes | `C89ADE0992517B9FB82EFA2C69C3AD6C433FC46F143F1EEEF85DFDCD0EABA071` |
| Full-copy manifest | `AD518D581EA6FB8440D6E7D9616B750E15C0A515E6FC9A1B4196DEE0DC15F9E2` |

The verification receipt is `.analysis/gpu-startup-diagnostic/completed-vm-verification.json`;
its SHA-256 is
`1FC6A904A0512AB32EFA26FEF3ABD2EB8CC0AB3E89E5A53B41DC8B9A300F7E83`.
The controller result and run evidence remain under the `.analysis` and
`.codex-build` paths recorded in that receipt. The runbook links the
[earlier signed VM result](GPU_PAYLOAD_SIGNED_VM_RESULT.md) as historical
evidence.
