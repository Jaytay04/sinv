# Signed native payload VM result

The local disposable VM copy from this historical run was retired during the
user-requested [workspace cleanup](WORKSPACE_CLEANUP_20261020.md). Its saved
results, signed artifacts, logs, and copied configuration metadata remain;
the test-copy disks and snapshots have been removed.

The controller passed the signed EFI boot and Windows health test on
2026-10-20 UTC. The outer workflow initially rejected the successful result
because it expected an `EfiMarkers` array that the controller does not emit.
The corrected reader verified the saved serial file against its recorded hash
and completed the evidence review without rebuilding, signing, or booting again.
The original controller result, candidate, and wrapper failure remain preserved.

Workflow status is `Complete`, with `SIGNED_NATIVE_VM_EVIDENCE_REVIEW_PASS`.
The review ran at revision `44583f55a0e69d1f9d43847efce51c8eb192f1bd`.

Candidate revision: `928971e7fb118d46d5bfcb54e5b406b0b65389fe` on
`feat/smbios-boot-serial-probe`. Main was not changed. The renderer and scanner
were not changed by this workflow work.

## Configuration and results

| Check | Observed result |
| --- | --- |
| VM | VMware Workstation, EFI, nested virtualization, four CPUs, 8 GiB |
| Windows | 11 Home 25H2, 26200.9457; host family 26200, host patch 9711 differs |
| Isolation | Offline, no physical passthrough; VMware display, no Intel GPU |
| Trust | Secure Boot on; test certificate appended to this VM's firmware db |
| Local signature check | SHA-256-pinned verifier, embedded signer certificate and image integrity passed |
| EFI | DMA admission, AP VMX launches, BSP VMX launch and firmware return passed |
| Payload | `payload_launch=ok`, EFI status zero, `shim_load_phase=firmware_load` |
| Windows health | Authenticated current-run result, four of four CPUs passed |
| Boot manager | Guest Windows boot-manager hash unchanged |
| Recovery | Before/after snapshots created; reverted to before; VM confirmed off |
| Evidence review | Original successful controller receipt and pinned artifacts/logs verified; no repeated boot |
| Component tests | Hypervisor 33/33; loader 29/29 |
| Image audit | Unsigned and signed `.hvscan` passed, 54,870 resident bytes |

The final graceful stop timed out; the controller's bounded hard-stop fallback
succeeded before the after snapshot and baseline revert. Graceful shutdown
under the hypervisor is therefore **not validated**. No cleanup failures were
recorded. Screen capture was unavailable because VMware rejected anonymous
guest capture; the evidence is serial output and authenticated results, not a
pixel capture.

The VM fixture reports `replay=ABORTED`, `restore=PASS`, `payload=PASS`.
Replay-independent loading is enabled, so the replay fixture does not block
the payload. This differs from the earlier physical run's `payload=FAILED`.
That physical run did not preserve the launch error; this result cannot
identify its original cause retrospectively.

## Preserved VM and evidence

All paths below are relative to the main repository workspace, not the feature
worktree. The test-copy path records its location at validation time; the
cleanup note above describes its later retirement. The default VM is retained.

- Test copy: `.codex-build/gpu_payload_vm/vm-20261020T044842Z-6c60e35d/clone/hsinv-Windows-25H2.vmx`.
- Retained default: `.codex-build/gpu_windows_vm/run-01/standalone/hsinv-Windows-25H2.vmx`.
- Copy manifest: `.codex-build/gpu_payload_vm/vm-20261020T044842Z-6c60e35d/copy-manifest.json`.
- Workflow journal: `.analysis/gpu-payload-diagnostic/runs/gpu-payload-20261020T051823Z-d60e12e73d004b94b24ce7a0e000fb1b/journal.json`.
- Controller directory: `.codex-build/gpu-payload-vm/gpu-payload-20261020T051823Z-d60e12e73d004b94b24ce7a0e000fb1b-b1672d8d5a004f048a2333721284f914/`.

The controller directory contains `result.json`, `validation.json`,
`efi-serial.log`, `windows-session.log`, copied VMware logs, authenticated guest
evidence archives, and a redacted command log. Snapshots are
`before-gpu-hal-ff301975` and `after-gpu-hal-8720c9bd`.

The independent copy verified all 93 files, totaling 58,094,667,483 bytes.
The retained source VMX and snapshot metadata remained unchanged during copying.
The copied disk parent chain resolves inside the new copy.

| Artifact | SHA-256 |
| --- | --- |
| Signed payload, 185,192 bytes | `28E131456D89BE634587741506515C99EC20BDDB27FB94DC7179476950C25753` |
| Signed loader, 58,216 bytes | `D7BBB209EF70F0791BF40F052956224650492C07C2C8CF45E338371354C34F53` |
| Controller result | `A0424B014F5D13B83A05B4BA81231D7FA9419E969E6AE50399E9B1702C257A8C` |
| EFI serial log | `585999A1994ECE0AB0DEA0DE43F1D7875836315A43D7A7DFC6FF6650266C807F` |
| Windows serial log | `968809A223769EE427C1A6C2A95EF233311180CE8022506486F3EA36A4FEEE30` |
| Copied VMware log | `BE36DE593E674D9BAE7BBDAFE5E332A48235ADFC390966E26552E0A4FE822EDD` |
| Full-copy manifest | `AD518D581EA6FB8440D6E7D9616B750E15C0A515E6FC9A1B4196DEE0DC15F9E2` |

## Limits

This test proves the signed shim/loader/payload path and nested VMX startup
for this candidate. Its DMA admission is the existing external-VMM trust
assumption. It does not exercise the Intel native backend, GPU plane/GGTT,
per-device VT-d enforcement, or native root WC rendering. Their headless model
tests passed; that is not hardware evidence.

Real UHD 770/PCH scanout, Intel-excluded/NVIDIA-primary topology, physical host
MOK acceptance, and stable bare-metal operation remain untested. Firmware/SMM
remain trusted; their display or DMA writes could silently violate ownership
without a guaranteed latch. No primary-host boot, security, trust-store, ESP,
or GPU configuration was changed. There is no runtime hand-back implementation.

Use [GPU_PAYLOAD_SCRIPT_RUNBOOK.md](GPU_PAYLOAD_SCRIPT_RUNBOOK.md) for the
single-command workflow. VM-only execution remains the project rule.
