# Intel UHD 770: one-time bare-metal diagnostic test

This is the operator-run, first physical test procedure requested for this PC. **It has not been validated on bare metal.** The completed Windows VM test does not establish native Intel scanout, Intel VT-d isolation, or compatibility with the host NVIDIA driver. Preparing this document did not stage files on the host ESP, change firmware variables, sign images, stop drivers, or reboot the host.

The test reserves the Intel adapter until platform reset. Windows continues on NVIDIA. It draws one initial diagnostic pattern on the Intel-connected display; it does not draw over the NVIDIA desktop, provide a continuous render loop, or support handing Intel back to WDDM during the session.

The repository's default execution policy remains VM-only. This document records your separately requested manual physical experiment; it does not authorize automation to run it on the host. Run each numbered section deliberately, and stop at the stated failure conditions.

## 1. Prerequisites and recovery

Before running anything that writes the ESP or arms BootNext:

- Have a tested full system backup on another disk, a Windows recovery USB, and the correct BitLocker/device-encryption recovery key available offline. A different measured boot path can prompt for recovery. Do not put recovery keys in transcripts or source control. This procedure does not decrypt the disk or suspend its protection. See [Microsoft's BitLocker recovery guidance](https://learn.microsoft.com/en-us/windows/security/operating-system-security/data-protection/bitlocker/recovery-overview).
- Know how to select **Windows Boot Manager on the original Windows disk** from this board's firmware boot menu. Verify that recovery path before the experiment; no board-specific recovery procedure is established here.
- Keep Secure Boot enabled. The project public MOK must already be enrolled, and firmware must accept the vendored Microsoft-signed shim. The [signed collector procedure](docs/EFI_INVENTORY_PATH_B.md) previously succeeded on this machine, but that only validated its collector and Windows handoff. If enrollment or firmware trust has changed, stop and resolve that separately; do not clear Secure Boot keys, reset revocations, or disable shim validation.
- Physically attach a separate diagnostic display to the motherboard's Intel HDMI/DP output. Keep the Windows desktop and firmware console on NVIDIA. An Intel adapter listed in Windows with no attached monitor is insufficient.
- Firmware must expose an active Intel GOP mode and exactly one enabled Intel pipe while NVIDIA remains its console. The HAL reuses that mode; it does not bring up an inactive connector or implement display link/mode setting. If firmware cannot provide this topology, the current HAL cannot attach. Do not disable the Intel WDDM device as an ownership substitute.
- Intel VMX and VT-d must be available from firmware. Record existing firmware and Windows virtualization settings. Do not disable Hyper-V, VBS, driver signing, or security features to force the test to pass. Compatibility with this host's Windows virtualization configuration is unverified.
- Keep the test network disconnected or controlled and save all work before the boot. Early failures can return without activation; failures after native isolation/plane arming or a partial CPU launch can intentionally halt until an external reset. A normal Windows return is not guaranteed.

Firmware/SMM are trusted. Their plane writes or DMA into the surface can silently violate ownership without a guaranteed ownership-latch indication. This assumption does not replace PCI/EPT filtering or the separate GPU DMA domain.

Native admission is intentionally strict: PCI `8086:4680` at segment 0, `00:02.0`; NVIDIA firmware console; supported Intel GOP format; one active pipe; unused secondary planes/cursor; valid, locked stolen GGTT backing and unused reserved entries; a bounded linear surface of at most 8 MiB; installed per-CPU guest filters and live VT-d/WC readback. Unsupported firmware state is a reason to stop. A 1920x1080 32-bit surface fits the limit; many higher-resolution modes do not.

## 2. Select the feature worktree and create an evidence directory

Use **x64 Developer PowerShell for Visual Studio**, elevated as your normal signing user for the complete workflow. The paths below deliberately use the feature worktree. Do not switch or modify the main checkout.

```powershell
$ErrorActionPreference = 'Stop'
$WorkspaceRoot = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
$SourceRoot = Join-Path $WorkspaceRoot '.codex-build\worktrees\gpu-windows-validation'
Set-Location -LiteralPath $SourceRoot
if ((git branch --show-current) -ne 'feat/smbios-boot-serial-probe') {
    throw 'Wrong branch.'
}
if (@(git status --porcelain).Count -ne 0) {
    throw 'Preserve existing edits; use a reviewed, clean feature revision.'
}
foreach ($tool in @('cl.exe', 'dumpbin.exe', 'cmake.exe', 'ctest.exe', 'ninja.exe', 'signtool.exe')) {
    $null = Get-Command $tool -ErrorAction Stop
}
function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed: exit $LASTEXITCODE" }
}
$SessionRoot = Join-Path $WorkspaceRoot ('.analysis\gpu-baremetal\' +
    [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $SessionRoot
git rev-parse HEAD | Set-Content (Join-Path $SessionRoot 'source-revision.txt')
$HvBuild = Join-Path $SessionRoot 'hypervisor'
$LoaderBuild = Join-Path $SessionRoot 'loader'
$SignedRoot = Join-Path $SessionRoot 'signed'
$null = New-Item -ItemType Directory -Path $SignedRoot
[ordered]@{ WorkspaceRoot = $WorkspaceRoot; SourceRoot = $SourceRoot;
    SessionRoot = $SessionRoot; HvBuild = $HvBuild;
    LoaderBuild = $LoaderBuild; SignedRoot = $SignedRoot } |
    ConvertTo-Json | Set-Content (Join-Path $SessionRoot 'session.json')
$SessionRoot
```

Keep this PowerShell session open until staging/arming is finished. Save the printed session path; after a reboot, set `$SessionRoot` back to that exact existing directory. Do not create a new directory when collecting this run.

**Keep the default VM untouched:** `.codex-build\gpu_windows_vm\run-01\standalone\hsinv-Windows-25H2.vmx`. Preserve its entire folder, snapshot disks, NVRAM, and vTPM. Do not clean or move `.codex-build` as part of this procedure.

## 3. Read-only host preflight

```powershell
Invoke-Checked 'powershell.exe' @('-NoProfile', '-ExecutionPolicy', 'Bypass',
    '-File', (Join-Path $SourceRoot 'components\hypervisor\setup\Get-GpuDiagnosticPreflight.ps1'),
    '-OutputPath', (Join-Path $SessionRoot 'gpu-preflight-before.json'))
$os = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
$os | Select-Object DisplayVersion, CurrentBuild, UBR |
    ConvertTo-Json | Set-Content (Join-Path $SessionRoot 'windows-version-before.json')
Get-CimInstance Win32_ComputerSystem |
    Select-Object Manufacturer, Model, NumberOfLogicalProcessors, HypervisorPresent |
    ConvertTo-Json | Set-Content (Join-Path $SessionRoot 'system-before.json')
manage-bde.exe -status $env:SystemDrive
Confirm-SecureBootUEFI
```

Run this section elevated if required by the last two queries. Secure Boot must read `True`. If an inventory/security query is unavailable, resolve it before proceeding. Verify the actual connected display routing manually. The prior inventory was Windows 11 Home 25H2 **26200.9711**, Intel UHD 770 **8086:4680**, and NVIDIA RTX 3080 **10DE:2206**, with both reported monitors on NVIDIA. Re-check; do not assume that old inventory is current.

`CanAuthorizeActivation=false` / `BootEnforcementStatus=NotAttested` is expected from the read-only preflight, even when native enforcement exists in source. It cannot attest running root filters, GPU DMA permissions, or the WC window. Do not change its JSON or treat a disabled PnP device as an exclusion proof.

## 4. Build separate native images and run static checks

Only run this native build after physically verifying the Intel display and the prerequisites above. `OUTPUT_CONFIRMED=ON` is your declaration, not automatic connector detection. No VMX executes during these builds or CTests.

```powershell
Invoke-Checked 'cmake.exe' @('-S', (Join-Path $SourceRoot 'components\hypervisor'),
    '-B', $HvBuild, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
    '-DCMAKE_OBJECT_PATH_MAX=180', '-DHYPERSINV_BUILD_CORE=OFF',
    '-DHYPERSINV_BUILD_DRIVER=OFF', '-DHYPERSINV_BUILD_LOADER=OFF',
    '-DHYPERSINV_BUILD_UEFI=ON', '-DHYPERSINV_GPU_VM_SELFTEST=OFF',
    '-DHYPERSINV_GPU_DIAGNOSTIC_NATIVE=ON',
    '-DHYPERSINV_GPU_DIAGNOSTIC_OUTPUT_CONFIRMED=ON')
Invoke-Checked 'cmake.exe' @('--build', $HvBuild, '--parallel', '4')
Invoke-Checked 'ctest.exe' @('--test-dir', $HvBuild, '--output-on-failure',
    '--output-log', (Join-Path $SessionRoot 'hypervisor-ctest.log'))
$NativeEfi = Join-Path $HvBuild 'src\efi\uefi_hypervisor.efi'
$Audit = Join-Path $SourceRoot 'components\hypervisor\src\efi\tests\Test-EfiRuntimeScannerImage.ps1'
$Dumpbin = (Get-Command dumpbin.exe).Source
Invoke-Checked 'powershell.exe' @('-NoProfile', '-ExecutionPolicy', 'Bypass',
    '-File', $Audit, '-ImagePath', $NativeEfi, '-DumpbinPath', $Dumpbin,
    '-MaximumPages', '16') | Tee-Object (Join-Path $SessionRoot 'hvscan-unsigned.txt')

Invoke-Checked 'cmake.exe' @('-S', (Join-Path $SourceRoot 'components\uefi-loader'),
    '-B', $LoaderBuild, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
    '-DIB_STATIC_RUNTIME=ON', '-DIB_QEMU_DEBUG_PORTS=OFF',
    '-DIB_PAYLOAD_REPLAY_INDEPENDENT=ON')
Invoke-Checked 'cmake.exe' @('--build', $LoaderBuild, '--parallel', '4')
Invoke-Checked 'ctest.exe' @('--test-dir', $LoaderBuild, '--output-on-failure',
    '--output-log', (Join-Path $SessionRoot 'loader-ctest.log'))
$Installer = Join-Path $LoaderBuild 'ib-install.exe'
$Probe = Join-Path $LoaderBuild 'ib-tcg-dump.exe'
```

Require **zero test failures** and `PASS: .hvscan is self-contained`. The previous VM build passed 30 hypervisor CTests, including 45 pure controller checks, and 3 affected loader tests. This new native build is different: save its own results and hashes. A pinned kernel/PDB profile is not required for this diagnostic pattern; this procedure does not validate scanner or process features on the host kernel.

The loader flag above means the payload can launch when PCR replay is refused. It does **not** disable the PCR replay gate or force empty PCRs. On a normal measured boot, the loader rejects replay and then uses its existing `image_launch_start_resident` path to launch the signed hypervisor and hand off to Windows. The default loader build without this flag can skip the payload. Do not clear the TPM, use measurement-disabled firmware, or fabricate a replay log. This separate signed build is a new physical-test candidate, **not** the previously tested unsigned VM loader.

The option permits payload launch after **any** replay failure or unavailability, including unavailable TCG2/TPM or unsupported PCR banks. It is not an EFI-time rule requiring the specific nonempty-PCR result. The Windows probe in section 6 is only a preparation check. If replay succeeds instead, the option does not suppress it. Secure Boot image authentication and native GPU admission remain separate checks.

## 5. Sign copies with the existing project key

Run as the Windows user whose current-user certificate store holds the existing private key. Do not export the key or create a new trust root. The public certificate and thumbprint below are pinned by the existing signed-collector tooling.

```powershell
$Certificate = Join-Path $SourceRoot 'components\hypervisor\hypersinv.cer'
$Thumbprint = '59DBD6780CFE60C020B3F4511211FEC22C397B85'
$PublicCert = [Security.Cryptography.X509Certificates.X509Certificate2]::new(
    [IO.File]::ReadAllBytes($Certificate))
if ($PublicCert.Thumbprint -ne $Thumbprint) { throw 'Unexpected public signer.' }
$StoreCert = Get-Item -LiteralPath ('Cert:\CurrentUser\My\' + $Thumbprint)
if (-not $StoreCert.HasPrivateKey) { throw 'Existing signing key is unavailable.' }
$SignedLoader = Join-Path $SignedRoot 'loader.efi'
$SignedPayload = Join-Path $SignedRoot 'payload.efi'
Copy-Item -LiteralPath (Join-Path $LoaderBuild 'loader.efi') -Destination $SignedLoader
Copy-Item -LiteralPath $NativeEfi -Destination $SignedPayload
foreach ($image in @($SignedLoader, $SignedPayload)) {
    Invoke-Checked 'signtool.exe' @('sign', '/s', 'My', '/sha1', $Thumbprint,
        '/fd', 'SHA256', $image)
    Invoke-Checked (Join-Path $LoaderBuild 'install_authenticode_test.exe') @($image, $Certificate)
}
Invoke-Checked 'powershell.exe' @('-NoProfile', '-ExecutionPolicy', 'Bypass',
    '-File', $Audit, '-ImagePath', $SignedPayload, '-DumpbinPath', $Dumpbin,
    '-MaximumPages', '16') | Tee-Object (Join-Path $SessionRoot 'hvscan-signed.txt')
$Shim = Join-Path $SourceRoot 'components\uefi-loader\src\vendor\shimx64.efi'
if ((Get-FileHash -LiteralPath $Shim -Algorithm SHA256).Hash -ne
    '6FE6E1BCBE6CF6BAEC8E056D40361CA1AA715CC04DDCC2855351DE060B84350B') {
    throw 'Vendored shim does not match the reviewed artifact.'
}
@($SignedLoader, $SignedPayload, $Shim, $Certificate, $Installer, $Probe) |
    ForEach-Object { Get-FileHash -LiteralPath $_ -Algorithm SHA256 } |
    Export-Csv -NoTypeInformation (Join-Path $SessionRoot 'artifacts.sha256.csv')
```

The verifier checks the expected Authenticode signer; it does not prove firmware db/dbx/SBAT acceptance. A signed collector success does not prove this hypervisor child will be accepted. If firmware/shim rejects it, retain the error and stop; do not bypass authentication. The [existing signing workflow](tools/efi_inventory/shim-chain-collector.ps1) uses the same current-user `signtool` and pinned-signer verification mechanism. See [Microsoft SignTool documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/signtool).

## 6. Capture TPM input, select the system ESP, and back it up

From here through arming, use **elevated x64 Developer PowerShell under the same signing user**. If opening another terminal, restore the variables from sections 2, 4, and 5 using the existing session directory; do not rebuild or replace artifacts between signing and staging.

```powershell
$ProbeOutput = @(& $Probe probe 2>&1)
$ProbeExit = $LASTEXITCODE
$ProbeOutput | Set-Content (Join-Path $SessionRoot 'tpm-probe-before.txt')
if ($ProbeExit -ne 1 -or
    (($ProbeOutput -join "`n") -notmatch 'PCR\[0-7\] at reset values: NO')) {
    throw 'Expected a readable, normally measured nonempty TPM bank. Review probe output.'
}
$ReferenceLog = Join-Path $SessionRoot 'tcglog.ib'
Invoke-Checked $Probe @('capture', '--out', $ReferenceLog,
    '--full', (Join-Path $SessionRoot 'tcglog-full.bin'))
if (Get-PSDrive -Name S -ErrorAction SilentlyContinue) {
    throw 'S: is already assigned. Do not unmount another volume; choose and review a free letter.'
}
Invoke-Checked 'mountvol.exe' @('S:', '/S')
try {
    $EspVolume = ((& mountvol.exe S: /L) -join '').Trim()
    if ($LASTEXITCODE -ne 0 -or $EspVolume -notmatch '^\\\\\?\\Volume\{[0-9A-Fa-f-]+\}\\$') {
        throw 'Could not identify the mounted system ESP.'
    }
    $EspVolume | Set-Content (Join-Path $SessionRoot 'esp-volume.txt')
    if (Test-Path -LiteralPath 'S:\EFI\insecure-boot') {
        throw 'An earlier stage exists. Preserve it and review ownership before continuing.'
    }
    foreach ($name in @('bootmgfw.efi.bak', 'bootmgfw.efi.ibnew',
                        'bootmgfw.efi.ibold', 'bootmgfw.sha256', 'grubx64.efi')) {
        if (Test-Path -LiteralPath (Join-Path 'S:\EFI\Microsoft\Boot' $name)) {
            throw "Existing replacement/restore state: $name. Do not continue or delete it blindly."
        }
    }
    Get-FileHash -LiteralPath 'S:\EFI\Microsoft\Boot\bootmgfw.efi' -Algorithm SHA256 |
        Export-Csv -NoTypeInformation (Join-Path $SessionRoot 'boot-manager-before.csv')
    Copy-Item -LiteralPath 'S:\EFI' -Destination (Join-Path $SessionRoot 'esp-efi-before') -Recurse
    Invoke-Checked 'bcdedit.exe' @('/enum', 'firmware', '/v') |
        Set-Content (Join-Path $SessionRoot 'firmware-before.txt')
} finally {
    Invoke-Checked 'mountvol.exe' @('S:', '/D')
}
```

The local EFI copy is evidence, not a replacement for the external full backup. Review that the identified ESP belongs to the currently running, original Windows disk. Stop if multiple installations make that ambiguous. Subsequent installer commands use this explicit volume, not `--esp-volume auto`, which enumerates ESPs and can select a different disk.

The installer requires a real captured `tcglog.ib` as a staging input. This normal-PC test expects replay to be refused because PCRs are nonempty; the log is not permission to replay measurements. A missing TPM, failed capture, or unexpected empty-bank result is a stop condition.

## 7. Stage and arm one boot

**These commands write the system ESP and firmware BootNext. They do not reboot.** Recheck backup, recovery key, display topology, signed hashes, and test logs before running them.

```powershell
$EspVolume = (Get-Content -LiteralPath (Join-Path $SessionRoot 'esp-volume.txt') -Raw).Trim()
Invoke-Checked $Installer @('stage', '--signer-cert', $Certificate,
    '--shim', $Shim, '--loader', $SignedLoader, '--log', $ReferenceLog,
    '--payload', $SignedPayload, '--esp-volume', $EspVolume)
Invoke-Checked $Installer @('oneshot', '--mode', 'bootnext',
    '--profile', 'legacy', '--esp-volume', $EspVolume) |
    Tee-Object (Join-Path $SessionRoot 'arm.txt')
Invoke-Checked 'bcdedit.exe' @('/enum', 'firmware', '/v') |
    Set-Content (Join-Path $SessionRoot 'firmware-armed.txt')
```

Expected stage output: verified readback manifest; no Microsoft boot-manager or firmware-variable change from **Stage** itself. Expected arm output: `BootNext set to owned BootXXXX; BootOrder was not changed.` The firmware entry description is `Insecure Boot Toolkit` and points to `\EFI\insecure-boot\shimx64.efi`. Do not select `replace`, `verify`, `inventory`, `shim-inventory`, or `vm-direct` for this test. In particular, `vm-direct` bypasses the shim entry and is reserved for disposable VM fixtures.

The intended chain is:

```text
Secure Boot firmware -> Microsoft-signed shimx64.efi
  -> project-signed grubx64.efi (the separately built loader)
  -> normal nonempty-PCR replay refusal
  -> project-signed payload.efi through image_launch_start_resident
  -> Windows Boot Manager, with Intel excluded only if native admission succeeds
```

Cancel before reboot, or remove the owned one-shot entry after collecting results:

```powershell
Invoke-Checked $Installer @('oneshot', '--mode', 'off',
    '--profile', 'legacy', '--esp-volume', $EspVolume)
```

`off` clears owned BootNext/Boot#### state. **It does not stop a running hypervisor, unpin the surface, or hand the adapter back during the current session.** It does not remove a MOK certificate or staged files.

## 8. Perform the manual test boot

Start an external capture before reboot if a verified motherboard UART is available. The payload writes legacy COM1 at I/O `0x3f8`, 115200 baud, 8N1; a USB serial dongle attached to this PC does not provide that legacy I/O port. Verify the board's connector/interface before connecting anything. The native GPU path does not emit the VM-only `GPU_HAL_VM:*` markers or a complete plane/GGTT/DMA attestation log.

Save work, then reboot manually when ready. Do not use an unattended reboot loop. BootNext selects the shim once; keep Secure Boot enabled and do not alter BootOrder. If unexpected MOK enrollment or a signature error appears, stop and return to Windows through firmware recovery; this run assumes the existing MOK is already enrolled.

Look for:

1. A normal TPM replay refusal followed by `payload launch: PASS` and Windows handoff. Replay refusal is expected; it is not a GPU failure in this loader configuration.
2. On the Intel-attached screen, a red diagnostic rectangle labelled **ROOT GPU**, near `(16,24)` for a sufficiently large mode. It is a one-time render. Photograph both displays; Windows desktop capture is not a capture of the root-owned Intel surface.
3. Windows starts on NVIDIA and remains usable. Do not deliberately reprogram Intel MMIO/GGTT, request runtime detach, sleep/hibernate, or test device resets to provoke ownership loss. Those are not a supported physical validation workflow.

If the screen is black or the machine halts, preserve the last serial/console evidence and use section 10. Do not repeatedly re-arm without diagnosing the failure.

## 9. Collect results in Windows

Open elevated PowerShell. Replace the session-directory placeholder below with the exact path printed in section 2; do not rerun section 2 and create a new directory. The probe is read-only and does not install or load a driver.

```powershell
$ErrorActionPreference = 'Stop'
$SessionRoot = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\.analysis\gpu-baremetal\<session-directory-from-section-2>'
$state = Get-Content -LiteralPath (Join-Path $SessionRoot 'session.json') -Raw | ConvertFrom-Json
$WorkspaceRoot = $state.WorkspaceRoot
$SourceRoot = $state.SourceRoot
$LoaderBuild = $state.LoaderBuild
$Probe = Join-Path $LoaderBuild 'ib-tcg-dump.exe'
$Installer = Join-Path $LoaderBuild 'ib-install.exe'
$EspVolume = (Get-Content -LiteralPath (Join-Path $SessionRoot 'esp-volume.txt') -Raw).Trim()
function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed: exit $LASTEXITCODE" }
}
Invoke-Checked $Probe @('health') |
    Tee-Object (Join-Path $SessionRoot 'windows-health.txt')
Invoke-Checked 'powershell.exe' @('-NoProfile', '-ExecutionPolicy', 'Bypass',
    '-File', (Join-Path $SourceRoot 'components\hypervisor\setup\Get-GpuDiagnosticPreflight.ps1'),
    '-OutputPath', (Join-Path $SessionRoot 'gpu-preflight-after.json'))
Confirm-SecureBootUEFI
if (Get-PSDrive -Name S -ErrorAction SilentlyContinue) { throw 'S: is already assigned.' }
Invoke-Checked 'mountvol.exe' @('S:', '/S')
try {
    $CurrentEspVolume = ((& mountvol.exe S: /L) -join '').Trim()
    if ($LASTEXITCODE -ne 0 -or -not [string]::Equals(
        $CurrentEspVolume, $EspVolume, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The current system ESP differs from this run. Stop; do not compare or clean up another disk.'
    }
    $before = Import-Csv -LiteralPath (Join-Path $SessionRoot 'boot-manager-before.csv')
    $after = Get-FileHash -LiteralPath 'S:\EFI\Microsoft\Boot\bootmgfw.efi' -Algorithm SHA256
    $after | Export-Csv -NoTypeInformation (Join-Path $SessionRoot 'boot-manager-after.csv')
    if ($before.Hash -ne $after.Hash) { throw 'Windows boot-manager hash changed; preserve evidence.' }
    Copy-Item -LiteralPath 'S:\EFI\insecure-boot' -Destination (Join-Path $SessionRoot 'esp-stage-after') -Recurse
} finally {
    Invoke-Checked 'mountvol.exe' @('S:', '/D')
}
Invoke-Checked $Installer @('oneshot', '--mode', 'off',
    '--profile', 'legacy', '--esp-volume', $EspVolume)
Invoke-Checked 'bcdedit.exe' @('/enum', 'firmware', '/v') |
    Set-Content (Join-Path $SessionRoot 'firmware-after-cleanup.txt')
```

Read the actual health output: every active logical CPU must have `UEFI_HYPERVISOR_WINDOWS_CPU:<group>:<cpu>:PASS`, followed by `UEFI_HYPERVISOR_WINDOWS_CPUID:PASS`. Require the host's CPU count, not the VM's four CPUs. Exit code zero only means the query completed; `FAIL` or `ABSENT` in the report is not a pass.

Evaluate the observations separately:

| Observation | What it establishes |
| --- | --- |
| Windows health passes | Resident hypervisor health on the queried CPUs; not GPU admission. |
| Intel disappears from guest enumeration | Consistent with exclusion; not independent proof of DMA isolation. |
| Intel remains visible / no ROOT GPU pattern | Native preparation may have declined and continued without a GPU backend. Successful Windows boot alone must not be recorded as GPU success. |
| ROOT GPU visible on the Intel-connected display | Initial physical scanout/render observation for this boot. Keep the photograph and image hashes. |
| No native isolation telemetry | Hardware domain/filter/GGTT permissions are not independently attested by the current Windows CLI. Record this evidence gap explicitly. |

Retain `handoff-status.txt`, the manifest and staged hashes, serial logs if available, photographs, pre/post inventory, Windows health, and boot-manager/firmware comparisons. Neither the PnP preflight nor the health CLI reports `HV_GPU_NOT_SUPPORTED` or the private ownership-latch state directly. Missing pattern/readback evidence is **INCONCLUSIVE/NOT ADMITTED**, not a reason to edit admission flags. The detailed admission and latch negative tests remain headless/VM evidence until a native reporting facility is added.

## 10. Revert and recover

After saving evidence and clearing only this run's owned one-shot entry, perform a full shutdown:

```powershell
shutdown.exe /s /t 0
```

Do not use sleep/hibernate or a runtime hand-back command. With power off, use a full platform power cycle if warm reset cannot be trusted to quiesce GPU DMA and VT-d state. Boot the original **Windows Boot Manager** normally, with Secure Boot enabled. Since BootOrder was not changed and the one-shot entry was cleared, the experimental payload should not run again. Verify ordinary Intel/NVIDIA enumeration and normal Windows operation. Pinned pages are reclaimed only across the reset boundary; no mid-session reclamation is supported.

If Windows never starts, power off and use the previously verified firmware boot menu to select Windows Boot Manager on the original disk. BootNext is normally consumed before the selected application runs, but do not rely on that as your only recovery method. If prompted, use the correct offline recovery key. Once Windows starts, run the owned `oneshot --mode off --profile legacy` cleanup with this run's ESP volume. If the original Windows entry still will not boot, use the recovery USB and Startup Repair or the tested full backup. Do not format an ESP, reset Secure Boot keys, or blindly run `bcdboot` against an unidentified partition.

Leave the inactive `EFI\insecure-boot` stage and local evidence preserved until reviewed. No automatic recursive ESP deletion is part of this runbook. Future staging refuses an existing directory by design; review and remove only the identified owned files before a later test. MOK enrollment is persistent and separate from GPU teardown; this procedure creates no new enrollment. Keep the default VM on disk in its existing location.

## Source and validation references

- [Native activation contract](docs/GPU_DIAGNOSTIC_ACTIVATION.md): guest PCI/EPT filters, sibling VT-d domain, WC mapping, trusted firmware assumption, and reboot-only teardown.
- [HAL contract](components/hypervisor/src/efi/gpu_hal.h) and [native boot preparation](components/hypervisor/src/efi/gpu_boot.c): strict ownership/plane/GGTT admission and initial pattern.
- [Loader payload condition](components/uefi-loader/src/loader/efi_entry.c) and [resident image launch](components/uefi-loader/src/loader/image_launch.c): the replay-independent option exists already; the native signed configuration above remains physically untested. Do not launch the hypervisor directly from a generic UEFI shell, which uses a different image-return/cleanup contract.
- [Installer stage](components/uefi-loader/src/install/stage.c) and [BootNext profiles](components/uefi-loader/src/install/bootnext.c): fresh-stage refusal, readback manifest, and owned one-time entry.
- [Completed VM procedure/evidence](docs/GPU_DIAGNOSTIC_WINDOWS_RUNBOOK.md): Windows 25H2 build 26200.9457, versus the inventoried host 26200.9711. The VM cannot prove physical Intel scanout or the Intel-excluded/NVIDIA-primary topology. Its unsigned images remain VM-only; the separate signed images built here are new candidates.

The old reference `docs/BAREMETAL_RUNBOOK.md` is absent in this revision. This root `runbook.md` is the physical-test procedure; do not assume an untracked recovery/deployment script exists at the old path.
