# UEFILoader + EFI Hypervisor — Build / Boot / Test

Everything runs from the repo root against one host runner,
`tools/host-runner/Invoke-UEFILoaderHostRunner.ps1`. Live gates always
build fresh (their -SkipBuild path is rejected), take exact VM leases,
and stop + restore the configured snapshot on failure. Evidence lands
under `.analysis/host-runner/runs/<RunId>/evidence/`.

```powershell
cd C:\Users\jayli\OneDrive\Documents\Projects\hsinv
$Runner = .\tools\host-runner\Invoke-UEFILoaderHostRunner.ps1
```

```powershell
# 0a) One-time credential check (False = missing or unreadable)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action CredentialStatus -Profile All

# 0b) One-time credential store (interactive session, DPAPI per user)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action StoreGuestCredential   -Profile Linux
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action StoreVmCredential      -Profile Windows
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action StoreMailboxCredential -Profile Windows
```

```powershell
# 1) Health check: toolchain, WDK, vmrun, guest, snapshot, profile
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action Doctor -Profile All
```

```powershell
# 2) Build: every enabled target, fresh (this is what live gates reuse)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action Build

# 2) Manual equivalent (preset = VS 18 2026, x64, static CRT) + host tests
cmake --preset combined-windows
cmake --build --preset combined-windows --config Release
ctest --test-dir build/combined/windows-vs -C Release --output-on-failure

# 2) Source-only contract gate (no MSVC/WDK required):
cmake --preset combined-contract-only
ctest --preset combined-contract
```

```powershell
# 3) Static contract tests (runner/host, guest mailbox)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\host-runner\tests\Test-HostRunnerContracts.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\host-runner\tests\Test-GuestInfoMailbox.ps1
```

```powershell
# Where the artifacts land:
#   uefi_hypervisor.efi (+3 probes)     build/combined/windows-vs/components/hypervisor/src/efi/Release/
#   companion .sys + service .exe       build/combined/windows-vs/components/hypervisor/src/driver/efi_runtime_companion/Release/
#   loader.efi + audit/install/tcgdump  build/combined/windows-vs/components/uefi-loader/Release/
```

```powershell
# 4) LIVE VM gates (disposable snapshot VM, auto stop + revert on failure):

# 4) Linux profile (nested QEMU + swtpm): positive TPM path + smoke gates
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunLinuxPositivePath -AuthorizeLiveVmRun -AuthorizeVmStop

# 4) Full 20-vCPU Windows cycle: boot through loader+payload, authenticated
#    all-CPU shutdown, replay reject, post-detach CPUID-absent, companion unload
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunWindowsDirectHypervisorE2E -Profile Windows -AuthorizeLiveVmRun -AuthorizeVmStop -AuthorizeSnapshotRevert

# 4) Same boot, scanner-only (no shutdown gate)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunWindowsDirectScannerE2E -Profile Windows -AuthorizeLiveVmRun -AuthorizeVmStop -AuthorizeSnapshotRevert

# 4) 1/4-CPU variants for isolation: add  -WindowsDirectProcessorCount {1|4}

# 4) Optional: service the guest to a pinned exact Windows build (KB-gated)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunWindowsExactBuildUpdate -Profile Windows -AuthorizeLiveVmRun -AuthorizeVmStop -AuthorizeSnapshotRevert

# 4) TPM: native TBS probe, then full TCG-log capture
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunWindowsProbe  -AuthorizeLiveVmRun -AuthorizeVmStop -AuthorizeSnapshotRevert
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunWindowsCapture -AuthorizeLiveVmRun -AuthorizeVmStop -AuthorizeSnapshotRevert
```

```powershell
# 5) RECOVER / QUARANTINE (only when a live run left the VM stuck);
#    inspect state.json + VM first, then clear after the VM is off:
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action ClearQuarantine -Profile Windows -AuthorizeQuarantineClear
```

```powershell
# 6) Where to look after a run:
#   .analysis/host-runner/runs/<RunId>/evidence/result.json  <- PASS/FAIL + HMAC + full log
#   .analysis/host-runner/runs/<RunId>/evidence/stdout.txt  <- serial/COM2 capture
#   .analysis/host-runner/runs/<RunId>/state.json           <- lease + run stage

# Expected happy output: 20x per-CPU PASS + CPUID aggregate PASS, runtime scan,
# authenticated shutdown, replay reject, all post-detach CPUID ABSENT, unloaded.
```

```powershell
# 7) NOTES
#   - The direct gate requires the runner config Windows profile to carry the
#     ExpectedWindowsBuild/Ubr (26200/.9457) plus matching ntoskrnl+PDB profile,
#     which it then uses to prove the guest process lookup (process-analysis).
#   - Live gates need a free-host check: guest memory default is 8192 MiB; pass
#     -WindowsDirectMemoryMib 6144 if the host-memory reserve check fails.
#   - Nothing here ever runs the payload/driver on a primary host on purpose.
```
