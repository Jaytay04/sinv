# Insecure-Boot to Hypervisor: VM Test Handoff

This is the full brief for a second agent to run the insecure-boot loader end to
end in a disposable VMware VM and hand the hypervisor EFI payload to it. Nothing
was booted or executed on a VM while preparing this brief; only the two host-side
component suites were run, and their verbatim result lines are quoted below.

Everything here was read from this tree, not from memory. Re-verify the hashes
before trusting them: a rebuild changes them.

## 1. Tree state to start from

| Item | Value |
| --- | --- |
| Worktree | `.codex-build\worktrees\gpu-windows-validation` |
| Branch | `feat/smbios-boot-serial-probe` |
| Main checkout | branch `main` @ `12f7250`, clean except untracked `.atl/` |

The review fixes and the new port tests are committed on this branch. Confirm with
`git -C <worktree> log --oneline -9` and expect, newest first:

```text
test(loader): exercise the port's restore, image and formatter paths on the host
fix(hypervisor): stage the boot-manager backup and wipe targets the loader needs
fix(loader): correct the device path GUID and make image cleanup identity-based
fix(host-runner): throw retired gates before any build or staging
fix(host-runner): refuse retired gates before any side effect
fix(loader): promote the staged boot manager copy and trust the bytes written
fix(host-runner): retire workflows whose loader fixtures no longer build
fix(loader): close bounds, lifetime and transaction gaps in the port
feat(loader): replace replay loader with insecure-boot flow
```

plus the commit that adds this document. Do not `git clean -xdf`, `git stash`, or
`git checkout --` the worktree: `.codex-build/` inside it holds the two build
trees this brief points at, and several scripts are only correct on this branch.

## 2. What the flow actually does

`ib_main.c` (src/insecure_boot/loader) runs this sequence, in this order:

1. open the boot volume, read `\ib-config.bin`, parse it (`tcg_spec_offset` = 27);
2. `ib_nvram_ensure_tcg_1_2` - read the AMI-vendor `Setup` NVRAM byte at that
   offset, flip TCG 2.0 to TCG 1.2, cold-reset if it had to flip;
3. `ib_bootmgfw_restore` - rebuild `\EFI\Microsoft\Boot\bootmgfw.efi` from
   `\ib-bootmgfw.efi` (staged copy, verified, promoted, then the copy is wiped);
4. read the payload `\ib-load.efi`, optionally read `\tcglog.ib`;
5. wipe `\ib-load.efi`, `\tcglog.ib` (only when present), `\ib-bootmgfw.efi`,
   `\EFI\Microsoft\Boot\mmx64.efi`, `\EFI\Microsoft\Boot\grubx64.efi`,
   `ib-config.bin`;
6. probe the TPM, replay over the raw TPM when a log is present, install the
   port's own `EFI_TCG2_PROTOCOL`, sweep MOK/SBAT/SSP variables;
7. hand-map the payload image and enter it (the hypervisor), then start
   `bootmgfw.efi` by path.

One line for the report: BootNext to `EFI\insecure-boot\grubx64.efi` runs the
loader, which restores the Windows boot manager, wipes its own staged files,
installs its own TCG 2 protocol, and hand-maps `\ib-load.efi` (the hypervisor)
into memory before returning control to `bootmgfw.efi`.

## 3. Build both components (verified in this session)

The `combined-windows` preset does not work in this environment (FTK1011 / MSB8066).
Configure the two components separately, as `docs/GPU_DIAGNOSTIC_WINDOWS_RUNBOOK.md`
lines 13-32 describe, with an explicit MSVC x64 environment. This is the exact
environment that produced green builds:

```powershell
$vc = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231'
$k  = 'C:/Program Files (x86)/Windows Kits/10'
$s  = '10.0.28000.0'
$env:PATH    = "$vc/bin/Hostx64/x64;$k/bin/$s/x64;$k/bin/$s;" + $env:PATH
$env:INCLUDE = "$vc/include;$k/Include/$s/ucrt;$k/Include/$s/um;$k/Include/$s/km;$k/Include/$s/shared;$k/Include/$s/shared/inc"
$env:LIB = $env:LIBPATH = "$vc/lib/x64;$k/Lib/$s/um/x64;$k/Lib/$s/ucrt/x64;$k/Lib/$s/ucrt/x64;$k/Lib/$s/km/x64"
Set-Location <worktree>

cmake -S ./components/uefi-loader -B ./.codex-build/gpu-win-loader -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_MT="$k/bin/$s/x64/mt.exe" `
  -DIB_STATIC_RUNTIME=ON -DIB_PAYLOAD_REPLAY_INDEPENDENT=ON -DIB_QEMU_DEBUG_PORTS=ON
cmake --build ./.codex-build/gpu-win-loader
ctest --test-dir ./.codex-build/gpu-win-loader --output-on-failure

cmake -S ./components/hypervisor -B ./.codex-build/gpu-win-hv -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_MT="$k/bin/$s/x64/mt.exe" `
  -DHYPERSINV_BUILD_UEFI=ON -DHYPERSINV_BUILD_CORE=OFF `
  -DHYPERSINV_BUILD_DRIVER=OFF -DHYPERSINV_BUILD_LOADER=OFF `
  -DHYPERSINV_TEST_EFI_RUNTIME_COMPANION=OFF `
  -DHYPERSINV_GPU_VM_SELFTEST=ON
cmake --build ./.codex-build/gpu-win-hv
ctest --test-dir ./.codex-build/gpu-win-hv --output-on-failure
```

Do not change these: a bare `cmake -G Ninja` picks up Clang 18.1.8; `Debug` adds
`/RTC1` and `loader.efi` then fails to link `_RTC_Shutdown` (it links
`/NODEFAULTLIB` with an empty `MSVC_RUNTIME_LIBRARY`); `$k/bin/$s/mt.exe` does
not exist, only `$k/bin/$s/x64/mt.exe`; backslash `-D` values break quoting;
`HYPERSINV_TEST_EFI_RUNTIME_COMPANION=ON` triggers a `CMAKE_OBJECT_PATH_MAX`
warning.

Artifacts the controller needs, with the sizes and SHA-256 that were green here
(paths are inside the worktree):

| Artifact | Size (bytes) | SHA-256 |
| --- | --- | --- |
| `.codex-build\gpu-win-loader\loader.efi` | 88,576 | `ad9ac9c6d52330dec7761d34df9f89ef23663b2111937aa1d1df824bb63f0a7b` |
| `.codex-build\gpu-win-loader\ib-install.exe` | 254,976 | `1e7069d6e673043cae83f651171d3e3dff879ad20cadaf3ff67ee9c114e4edd2` |
| `.codex-build\gpu-win-loader\ib-tcg-dump.exe` | 172,544 | `ba8398dff5b7117bb7dccb807b8773d22e9db0573d3adad052d45e640cb171ee` |
| `.codex-build\gpu-win-hv\src\efi\uefi_hypervisor.efi` | 196,608 | `6488e51e21d805be4cf6a5f3743302293151b336770610680d134908a1c43259` |
| `.codex-build\gpu-win-hv\src\efi\uefi_hypervisor_probe.efi` | 3,584 | `0cfeb138775a833d3acb2bda1332bfaa481bf450d972c8fbf5d551f4d907241a` |
| `.codex-build\gpu-win-hv\src\efi\uefi_hypervisor_post_ebs_probe.efi` | 5,120 | `a8d41f3576d4ba38086cb94d011a50a15d93d88f63a26a973108e3c2f4ba2222` |

The hypervisor build also prints `PASS: .hvscan is self-contained (57430 bytes).`

## 4. Host suites: what is and is not proven

Both suites were green in this session, verbatim:

```text
100% tests passed, 0 tests failed out of 17
100% tests passed, 0 tests failed out of 61
```

The 17 loader tests now include two that link real port code:
`ib_bootmgfw_port_test` (ib_bootmgfw.c, ib_boot.c, ib_efi_glue.c, ib_util.c with
mocked firmware and filesystem failures) and `ib_port_safety_test` (ib_debug.c,
ib_format.c, ib_util.c: x64 ABI offsets, formatter canaries, the Device Path
Utilities GUID). They are the answer to the review note that the old 15 tests
never touched the port. `uefi_runtime_abi_test` still compiles against
`src/vendor`, not `src/insecure_boot/include/ib_uefi.h`, so it does not pin the
port's own ABI surface. Treat the host suites as a build-and-ownership gate,
not as memory safety for the whole loader: only the VM run below can establish
that.

## 5. Pick the VM

| VMX | Use |
| --- | --- |
| `.codex-build\gpu_windows_vm\run-01\standalone\hsinv-Windows-25H2.vmx` | **Use this one.** The runbook designates it as the default Windows validation VM (25H2, build 26200, UBR 9457, 4 CPUs, Secure Boot off, BitLocker decrypted, nested virtualization, 8 GiB RAM, no NIC). `KEEP-VM.txt` in that folder marks it as retained; never clean that folder. |
| `.codex-build\gpu_payload_vm\vm-20261020T184950Z-3845cd62\clone\hsinv-Windows-25H2.vmx` | Fallback only, if the standalone VM is left in a bad state. It is a payload-test clone, so re-check its build/UBR/CPU facts with the guest inspection before trusting `-ExpectedBuild`/`-ExpectedUbr`/`-ExpectedCpus`. |
| `.codex-build\gpu_windows_vm\run-01\full-copy\UEFILoader-Win11-vTPM-ExactBuild-Gate.vmx` | Do not use. The runbook states the two original ExactBuild Gate VMs are no longer dependencies. |

The controller creates its own `before-gpu-hal-<8hex>` / `after-gpu-hal-<8hex>`
snapshots and reverts to the before snapshot in its `finally` block, so no
pre-existing snapshot is required and the VM is returned to baseline after each
run. VM-only activity: never stage any of these files on a physical ESP or the
host boot selection.

## 6. Run the controller

`Test-GpuHalWindowsVm.ps1` enforces, and this command satisfies:

- `VmxPath` and a fresh, non-existent `RunDirectory` must be under
  `<WorkspaceRoot>\.codex-build` (lines 1859-1862);
- every input artifact, including the two guest scripts, must be inside
  `WorkspaceRoot` (`Test-PathWithin` is a case-insensitive prefix test, so the
  worktree counts as inside the main checkout);
- `tools\host-runner\HostRunner.Core.psm1` and
  `tools\host-runner\Invoke-UEFILoaderHostRunner.ps1` are loaded from
  `WorkspaceRoot`, not from `$PSScriptRoot`;
- every path in the chain is rejected if it is a reparse point, so do not try a
  symlink or junction into the worktree.

Run the worktree's controller with the main checkout as `WorkspaceRoot`:

```powershell
$Main = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
$Work = Join-Path $Main '.codex-build\worktrees\gpu-windows-validation'
$VmRoot = Join-Path $Main '.codex-build\gpu_windows_vm'
$VmxPath = Join-Path $VmRoot 'run-01\standalone\hsinv-Windows-25H2.vmx'
$RunDirectory = Join-Path $VmRoot ('run-' + [guid]::NewGuid().ToString('N'))
if (Test-Path -LiteralPath $RunDirectory) { throw 'Choose a fresh run directory.' }

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File (Join-Path $Work 'components\hypervisor\setup\Test-GpuHalWindowsVm.ps1') `
  -WorkspaceRoot $Main -VmxPath $VmxPath -RunDirectory $RunDirectory `
  -EfiPath      (Join-Path $Work '.codex-build\gpu-win-hv\src\efi\uefi_hypervisor.efi') `
  -LoaderPath   (Join-Path $Work '.codex-build\gpu-win-loader\loader.efi') `
  -InstallerPath (Join-Path $Work '.codex-build\gpu-win-loader\ib-install.exe') `
  -ProbePath    (Join-Path $Work '.codex-build\gpu-win-loader\ib-tcg-dump.exe') `
  -CredentialStateRoot '<existing private Host Runner state root>' `
  -ExpectedBuild 26200 -ExpectedUbr 9457 -ExpectedCpus 4 -TimeoutSeconds 360
```

`CredentialStateRoot` must be an existing private Host Runner state whose
`credentials` directory holds `windows-vm.xml` and `windows-clone-mailbox-key.xml`
(DPAPI protected). Never copy those into run evidence or source control.

Leave `-ValidationProfile` at its default `Model`. `SignedNative` additionally
requires pinned shim, certificate and verifier artifacts.

### Overlay the Host Runner first, then restore it

The guest scripts come from the worktree (`$PSScriptRoot\guest\...`), but the
Host Runner comes from the main checkout, and `main` still carries the pre-
replacement gates that demand `ibaudit.exe` and the removed smoke binaries.
Overlay only that one file, and restore it afterwards so the main checkout stays
clean:

```powershell
$hr = Join-Path $Main 'tools\host-runner\Invoke-UEFILoaderHostRunner.ps1'
Copy-Item -LiteralPath $hr -Destination "$hr.pre-overlay.bak"
Copy-Item -LiteralPath (Join-Path $Work 'tools\host-runner\Invoke-UEFILoaderHostRunner.ps1') -Destination $hr
# ... run the controller ...
Copy-Item -LiteralPath "$hr.pre-overlay.bak" -Destination $hr
Remove-Item -LiteralPath "$hr.pre-overlay.bak"
```

`HostRunner.Core.psm1` is identical on both branches (`git diff main..HEAD`
touches only `Invoke-UEFILoaderHostRunner.ps1` and the example config under
`tools\host-runner`), so it needs no overlay. The controller cross-checks the
guest's `VmwareGuestInfo*` constants against the Host Runner AST (line 2046);
the guest patch adds no new constants, so the overlay keeps that check satisfiable.

## 7. ESP contract

The guest stages everything, then calls
`ib-install.exe oneshot --mode bootnext --profile vm-direct --esp-volume auto`
and requires the line `BootNext set to owned Boot<4hex>; BootOrder was not changed.`

| ESP path | Written by | Consumed by |
| --- | --- | --- |
| `\EFI\insecure-boot\shimx64.efi` | guest, copy of `loader.efi`, only to satisfy the shared manifest preflight | firmware allowlist |
| `\EFI\insecure-boot\grubx64.efi` | guest, `loader.efi` | the BootNext target, i.e. the loader itself |
| `\EFI\insecure-boot\payload.efi` | guest, `uefi_hypervisor.efi` | not read by the loader (the root copy is) |
| `\EFI\insecure-boot\tcglog.ib` | guest, a reference log if one exists, otherwise a sentinel line | optional replay input |
| `\EFI\insecure-boot\manifest.sha256` | guest, `schema=1` plus `shimx64.efi` and `grubx64.efi` | `ib-install` preflight |
| `\ib-config.bin` | guest, 14 bytes `IBCONFIG` + `01 00 00 00` + `1B 00` | loader config, `tcg_spec_offset` = 27 |
| `\ib-load.efi` | guest, `uefi_hypervisor.efi` | loader payload: read, wiped, hand-mapped, entered |
| `\ib-bootmgfw.efi` | guest, copy of `bootmgfw.efi` with a read-back hash match | loader restore source, then wiped |
| `\EFI\Microsoft\Boot\mmx64.efi`, `\EFI\Microsoft\Boot\grubx64.efi` | firmware, or 9-byte ASCII markers created by the patched guest when absent | loader zeroes then wipes |
| `\EFI\Microsoft\Boot\bootmgfw.efi` | firmware | restored by the loader, then started by path |

`ib_fs_wipe` treats an absent file as `IB_LOADER_ERR_MISSING_ARTIFACT`, so all
six wipe targets must exist before the reboot. That is why the patched guest
creates the two `Microsoft\Boot` markers and refuses if `\ib-bootmgfw.efi`
already exists. It records what it created in `WipeSiblingsCreated`.

## 8. Evidence to collect, and the markers to expect

Loader console output reaches the VM serial console the controller captures.
Expected, in order, on a successful run:

```text
insecure-boot: Hello, World!
insecure-boot: TCG UEFI spec version is already TCG 1.2
insecure-boot: the Windows boot manager is back in place
insecure-boot: wiping MOK/SBAT/SSP variables
insecure-boot: AMI Setup:
insecure-boot: Variables cleared
insecure-boot: TPM on <start method> ...
insecure-boot: entering the payload at <address>
insecure-boot: the payload returned <status>
```

Controller and guest markers that must appear in the run evidence:
`GPU_HAL_MODEL:ALL_PASS`, `GPU_ENFORCEMENT_MODEL:ALL_PASS SYNTHETIC_ONLY`, `GPU_HAL_VM:ROOT_WC_RENDER PASS`,
`GPU_HAL_VM:ROOT_WC_UNMAPPED PASS`, `GPU_HAL_VM:ROOT_WC_BOUNDS PASS`,
`GPU_HAL_VM:ROOT_WC_NO_BACKEND PASS`, `GPU_HAL_VM:VMX_FIRMWARE_RETURN PASS`,
plus `GPU_HAL_WINDOWS:SETUP_DIAG:` and `GPU_HAL_WINDOWS:RESULT:` records.
Any of these in the serial capture aborts the run: `GPU_HAL_MODEL:FAIL`, `GPU_ENFORCEMENT_MODEL:FAIL`, `BSP virtualization failure:`, `GPU_HAL_VM:<name> FAIL`, or a line matching `uefi hypervisor: ... FAIL|FATAL`.
Require exactly four per-CPU startup passes plus the aggregate, and require the
Windows boot-manager hash to be unchanged after the run: the loader restores
that file, so an unchanged hash is part of the pass condition.

Post-run ESP state to verify: all six wipe targets gone,
`EFI\Microsoft\Boot\bootmgfw.efi` present with the pre-run hash, and
`EFI\insecure-boot` still holding the stage (the loader never cleans it).

## 9. Gates that decide the run before it starts

**The Setup-variable gate is the hard one.** `ib_nvram.c:124-170`: if the AMI
`Setup` byte at offset 27 reads TCG 2.0 (2), the loader writes Setup, prints
`insecure-boot: TCG UEFI spec version was TCG 2.0, now TCG 1.2` and
`insecure-boot: resetting, every staged file stays for the next boot`, then
calls `ib_boot_reset_cold()`, which never returns. UEFI consumes BootNext before
entry, so the rebooted boot never re-runs the loader, and the controller's
`revertToSnapshot` to the before snapshot undoes the flip - so every attempt
repeats the same cold reset. A pass needs Setup to already read TCG 1.2 (1).
Look for `already TCG 1.2` in the serial capture to tell the two cases apart.
Probing the saved NVRAM on disk was inconclusive (the 2,424,832-byte
`standalone/UEFILoader-Win11-vTPM-ExactBuild-Gate.nvram` has no hit for the AMI
GUID tail `2fb772a4`, the prefix `4ebe7903`, `Setup` in ASCII or UTF-16, or
`Boot0000`); do not re-scan it, read the live console instead.

Other loader failures, with their exact text:

| Symptom | Meaning |
| --- | --- |
| `the firmware keeps no Setup variable to reconfigure` | no AMI `Setup` NVRAM variable, `IB_LOADER_ERR_NO_SETUP_VARIABLE` |
| `the Setup variable is %zu bytes, too small for offset %#zx` | config offset beyond the variable |
| `Setup holds %#04x at the configured offset, neither TCG 1.2 nor TCG 2.0` | wrong offset or wrong vendor variable |
| `Setup refused the write (%s); if UEFI Variable Runtime Protection ... disable it in the firmware setup and power-cycle` | firmware runtime-variable protection |

Guest-side refusals to expect: `EFI\insecure-boot already exists; refusing to
overwrite an ESP stage.`, `ib-bootmgfw.efi already exists on the boot volume;
refusing to overwrite staged state.`, `bootmgfw.efi changed after initial
inspection.`, `BitLocker protection state could not be classified; refusing to
stage BootNext.`. The guest suspends BitLocker with `-RebootCount 2`.

Recovery between attempts: the baseline revert normally clears the ESP staging.
If a run dies before the revert, or the revert fails, the next attempt throws on
the leftover stage. Clean `EFI\insecure-boot` plus the six root files off the
clone's ESP, or take a fresh clone, before retrying.

## 10. Known caveats to report honestly

- `port/host-free/ib_uacpi_stub.c` is a stub: `ib_uacpi_init` reports OK and
  `ib_uacpi_find_table` reports not found, so the TPM probe reports the CRB
  interface absent and replay is skipped. Every stub names the Rust symbol it
  stands in for, because the reference project's uACPI runtime tree is empty.
  Expect the TPM lines to report absent, not a bug in the port.
- `ib_bootmgfw_replace` still has a delete/write interruption window. The
  promoted staged copy is recovery material, not crash atomicity.
- The retired host gates now throw before any build or staging:
  `The installer host gate is retired: its gate binaries no longer have build
  targets.` and the same shape for one-shot, audit and fixture. Stale binaries
  left in an old build directory can mask a real regression; build fresh.
- The boot-chain VM fixture is behind `-DIB_BUILD_BOOT_CHAIN_VM_FIXTURES=ON` and
  deliberately excludes the production entry, TPM, replay, NVRAM mutation, custom
  TCG protocols and the payload mapper.
- The reference Rust sources and `reference_binaries\` are read-only ground truth
  and are never executed.

## 11. Cheaper pre-flight before spending a VM run

`docs/BOOT_CHAIN_DIAGNOSTIC.md` describes a QEMU-only rehearsal of the same
restore and image-handoff code in four short headless cases (512 MiB RAM, one
emulated CPU, no network, generated 64 MiB ESPs, debug-exit device). It needs
Python 3 and the installed QEMU distribution with stock
`share\edk2-x86_64-code.fd` and `share\edk2-i386-vars.fd`. Success marker is
`BOOT_ONLY_VM_PASS`. Run it first: it is where a port bug in the restore or
handoff path should be caught, at a fraction of the VM cost.

## 12. Definition of done for the VM run

Report, in this order: the four artifact hashes actually used; the controller's
structured result and `RunDirectory`; the serial capture lines quoted in section
8; the controller markers from section 8; the post-run ESP state; the boot-manager
hash comparison; and, if the run failed, which gate in section 9 it hit with the
exact message. Do not convert a failed health result into a pass, and do not
report a pass from the host suites alone.
