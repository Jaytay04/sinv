# Scripted signed payload validation

This workflow builds, signs, and validates the diagnostic loader in a separate
offline nested Windows VM. It never stages or arms the primary host. The old
bare-metal journal, external backup, and retained ESP files remain untouched.
There is no runtime GPU hand-back operation.

Run each action separately from the main repository root. The scripts discover
the existing Visual Studio toolchain; no variables need to survive between
commands. Signing uses the existing current-user private key without exporting
it or changing Windows trust stores. VMware uses the existing DPAPI-protected
Host Runner credentials; do not paste credentials into commands or logs.

```powershell
Set-Location 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
```

## 1. Preview

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-payload-test.ps1 Prepare -WhatIf
```

Preview executes no probes, builds, signing, or VM operations.

## 2. Prepare a local validation run

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-payload-test.ps1 Prepare
```

Require success. The selector is under `.analysis\gpu-payload-diagnostic`,
separate from the recovered bare-metal workflow. Prepare pins the clean feature
revision and relevant source files. Do not edit source between these actions.

## 3. Build and test

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-payload-test.ps1 Build
```

Require component CTest and unsigned `.hvscan` success. This builds the native
configuration with the new diagnostic loader. The VM loader enables serial
debug output; that difference from the previous physical artifact is recorded.
The native image and model self-test are separate configurations and are not
combined.

## 4. Sign and verify

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-payload-test.ps1 Sign
```

Require signature verification and signed `.hvscan` success. Local signed
copies are immutable inputs to the VM action; they are not deployed to the
primary host.

The Sign action and VM controller use the built verifier to check signature
integrity and the pinned signer certificate. Windows host certificate-chain
trust is recorded separately; the scripts do not add a host trusted root.

## 5. Create the disposable signed-test VM

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\New-GpuPayloadVm.ps1
```

This makes an independent offline copy of the stopped VM at the reviewed
`hsinv-default-25h2-26200.9457` snapshot and verifies the copied files. It does not move the retained VM at
`.codex-build\gpu_windows_vm\run-01\standalone\hsinv-Windows-25H2.vmx`.
The clone, disks, and test NVRAM stay under `.codex-build\gpu_payload_vm`.
The script publishes its exact path/hash in `vm-prepared.json`; later actions
never select the newest directory. An existing prepared receipt blocks another
clone instead of silently switching targets.

An untouched prepared copy can be reused after a workflow-only fix when Git
proves that its preparation revision is an ancestor and all four preparation
dependencies have identical blobs. The VMX, retained source, and certificate
pins must still match. This does not authorize reuse of a VM that has booted.

The offline-copy path avoids the VMware encrypted-snapshot clone operation
that failed during validation. A failed preparation keeps its partial output
and evidence; it does not alter the retained VM or publish a usable receipt.

Only the new clone receives the public certificate as an appended firmware
`db` default, with Secure Boot configured on. The trust setup is unverified
until the VM log and guest readback confirm it. It does not enroll a host MOK,
write MokList, disable validation, or replace default Microsoft certificates.
This firmware-db trust path differs from the physical host's MOK trust.

The default VM has Windows build 26200.9457, four CPUs, 8 GiB RAM, nested
virtualization, no network, and no passthrough. This matches the host's 25H2
build family, not its 26200.9711 patch level. Keep both VM folders on disk.

## 6. Validate only in that VM

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-payload-test.ps1 ValidateVm
```

The signed-native controller requires Secure Boot enabled, uses the actual
vendored shim and signed loader/payload, and boots the legacy shim path. It
records before/after snapshots, EFI serial output, authenticated Windows
results, the raw handoff/launch diagnostics, and stop/revert evidence. A
signature rejection, failed payload, missing evidence, or cleanup failure
must remain a failed result. A model test with Secure Boot off cannot satisfy
this gate.

If the controller completed successfully but the wrapper rejected its saved
result because of a reader defect, use the evidence-only review action after
the reviewed fix is committed:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-payload-test.ps1 ReviewVm
```

ReviewVm does not boot or reconfigure a VM. It requires the original successful
controller receipt, matching artifact hashes, complete cleanup proof, and the
saved serial log. It rejects source changes outside the permitted workflow and
documentation files. It cannot turn a failed controller run into a pass.

The VM can prove the signed EFI execution path, the existing external-VMM DMA
admission assumption, VMX startup/return, and Windows per-CPU health. VMware
SVGA cannot exercise Intel native plane/GGTT/DMA enforcement. It cannot prove
UHD 770 physical scanout, host MOK acceptance, or Intel-excluded/NVIDIA-primary
topology. Firmware/SMM remain trusted in the native design; their writes could
silently violate ownership without a guaranteed latch.

## 7. Read the report

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-payload-test.ps1 Report
```

Status and Report are read-only:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\gpu-payload-test.ps1 Status
```

Preserve the printed run directory and receipts on failure. Do not retry a
partially completed mutation, edit its journal, or overwrite its output. A
failed VM action must be reviewed with its stop/revert evidence before another
run. The report must distinguish observation success from workflow validation.

The prior physical failure is documented in
[GPU_PAYLOAD_FAILURE_INVESTIGATION.md](GPU_PAYLOAD_FAILURE_INVESTIGATION.md).
Its lost launch error cannot be recovered by rebuilding after the event.

## Trust setup reference

[Broadcom KB 377306](https://knowledge.broadcom.com/external/article/377306)
documents `uefi.secureBoot.dbDefault.file0` for fresh NVRAM on ESXi. Workstation
support must be established by this test's VMware log and guest readback;
the documentation alone does not prove it. If trust setup fails, keep the
failure and stop. Do not weaken the test to obtain a passing result.
