# Payload diagnostic build: next steps

This document records the earlier preparation checkpoint. For the current
script-per-action workflow, use [Scripted signed payload validation](GPU_PAYLOAD_SCRIPT_RUNBOOK.md).
The controller now has a separate SignedNative profile; the Model profile
described below remains distinct from signed validation.

The previous physical run is recovered and `Complete`. The new diagnostic
loader is committed at `33eac97fa9bb7eb921e0e3f8d3b452e06029eaea`; its build
and 61 component tests passed. It has not been signed, staged, or boot-tested.
Its purpose is to record the missing failure stage and raw EFI/shim errors;
it is not a fix for the still-unknown payload startup failure.

Run commands individually from:

```powershell
Set-Location 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
```

Do not move the retained VM or change the primary host's firmware/security
settings. The repository's [AGENTS.md](../AGENTS.md) requires:
"All VMX execution and end-to-end boot tests are VM-only activities."

## 1. Archive the completed run selector

This is optional local housekeeping, required before a future fresh Prepare.
It preserves the journal, action evidence, and external backup. It does not
delete the staged ESP files, change firmware boot state, or reboot.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-host-test.ps1 Archive
```

Require `SELECTOR_ARCHIVED`. If it fails, preserve the output and stop; do not
remove the selector manually. Do not repeat Disarm or Rollback for this
already recovered run.

## 2. Check the feature checkout

```powershell
git -C .\.codex-build\worktrees\gpu-windows-validation status --short --branch
git -C .\.codex-build\worktrees\gpu-windows-validation log -1 --oneline
```

Require `feat/smbios-boot-serial-probe` and no modified/untracked source files.
The history must contain `33eac97`; later documentation commits are expected.
Do not switch or modify the main checkout.

## 3. Rebuild and check the diagnostic loader without executing EFI

Use x64 Developer PowerShell for Visual Studio. The existing `.codex-build\pdl1`
directory is configured for the diagnostic loader; these commands rebuild and
run Windows host tests only. No elevation, signing, ESP mount, or boot change
is needed for this step. If that build directory is missing, stop rather than
substituting an unrelated cached build.

```powershell
cmake --build .\.codex-build\pdl1 --parallel 4
```

Require exit code zero, then:

```powershell
ctest --test-dir .\.codex-build\pdl1 --output-on-failure
```

Require 29/29 passing tests, including `image_launch_test` and
`payload_diagnostic_test`. The existing full verification also passed 32/32
hypervisor checks and the unchanged signed payload's `.hvscan` audit.

## 4. Stop here: signed native VM validation is still pending

There is currently no supported one-command test of the required signed
Secure Boot/native-enabled combination. The existing
`components/hypervisor/setup/Test-GpuHalWindowsVm.ps1` controller uses the
VM-only direct-loader path and calls `Set-CloneSecureBoot $false`; passing it
again would not close this gap. Do not use that result to authorize a physical
retry, and do not run Prepare/Sign/Backup/Stage/Arm as the next sequence.

The next implementation/validation work is to exercise the signed shim chain
in the retained disposable nested VM, with snapshots, controlled/offline
network, native build options enabled, and saved diagnostic output. The VM
stays at `.codex-build\gpu_windows_vm\run-01\standalone\hsinv-Windows-25H2.vmx`.
It has no Intel GPU passthrough, so Intel admission must remain unsupported;
this cannot prove UHD 770 scanout or the host's dual-GPU isolation.

The previous `EFI\insecure-boot` stage also remains on the physical ESP.
Archive does not retire it, and Backup intentionally refuses an existing
stage. Any later stage retirement needs an exact, reviewed, hash-verified
preservation/cleanup procedure for the completed run. Do not delete it or
overwrite it to make a new run proceed.

## Reading a future diagnostic result

For a controlled run using the new loader, preserve `payload-diagnostic.txt`
and the legacy `handoff-status.txt`. The GPU Collect action captures the new
file as `esp-payload-diagnostic.txt` in the printed action directory when it
exists. The earlier physical run cannot acquire this missing evidence after
the fact.

`payload_launch=load` identifies the shim/firmware loading boundary;
`protocol` identifies loaded-image validation; `start` means the resident
payload entry returned an EFI error. Preserve `payload_status`,
`shim_load_phase`, and `shim_load_status` exactly. A missing file is not a
pass, and a file can be stale if writing failed. See the
[investigation](GPU_PAYLOAD_FAILURE_INVESTIGATION.md) for evidence limits.
