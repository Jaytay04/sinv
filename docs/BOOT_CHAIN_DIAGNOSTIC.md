# Boot-chain recovery and ownership tests

This workflow tests the C port's ordinary filesystem restoration and firmware
image handoff in isolation. Its VM image links the real `ib_fs.c`, `ib_boot.c`,
and `ib_bootmgfw.c` helpers, with a separate fixture entry and synthetic EFI
child. It excludes the production entry, TPM/replay code, NVRAM mutation,
custom TCG protocols, manual payload mapper, and hypervisor.

The fixture is **VM-only**: it uses QEMU debug ports and exits QEMU when done.
Do not install either fixture on a physical ESP. This workflow does not validate
Windows boot, Secure Boot trust/enrollment, GPU isolation, or the full loader.

## Fixes exercised

- A missing canonical boot-manager file no longer prevents restoration from
  a verified staged copy. Failed restoration retains backup/staging evidence.
- Image lifetime is checked against a snapshot of the owned LoadedImage
  identity. An application returning a security/error status is not assumed to
  be an unstarted image. If the initial identity query fails, the known-owned
  image is released before entry and the operation fails closed.
- A failed create caused by a missing parent directory no longer returns a
  null file handle as success.
- The Device Path Utilities GUID matches UEFI section 10.5:
  `0379BE4E-D706-437D-B037-EDB82FB772A4`. Its previous final bytes prevented
  locating the firmware service, which the VM handoff test exposed.
- Both runner dispatchers reject retired gate actions before invoking builds,
  staging, or gate scripts.

The restore helper still has a delete/write interruption window. Its retained
files provide recovery material, not guaranteed automatic firmware recovery or
crash atomicity. The tests do not authorize a primary-host boot test.

## Build and host verification

From the shared repository root:

```powershell
$tools = '.\.codex-build\worktrees\gpu-windows-validation\tools\boot_chain_diagnostic'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$tools\Build-BootChainDiagnostic.ps1" -WhatIf
$build = powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$tools\Build-BootChainDiagnostic.ps1" | ConvertFrom-Json
$build | Select-Object Status, PackageDirectory, EvidenceDirectory
```

Requires an installed Visual Studio x64 C++ toolchain, CMake, and Ninja. The
script uses fresh short build paths and packages only
`boot_chain_vm_fixture.efi` and `boot_chain_vm_child.efi`. It explicitly builds
the host test targets rather than the full production loader. It runs the
component's 17 CTests and the eight-case retired-gate dispatch test. Source and
artifact hashes, toolchain paths, native-command receipts, and EFI headers are
saved under `.analysis\boot-chain-diagnostic\<id>`.

New host tests include real restore/image-wrapper code with mocked firmware and
filesystem failures, allocation/free balance, returned-application versus
pre-entry rejection, reused handles, rollback failures, and identity-query
failure. A second fixture checks the port's own x64 ABI offsets, formatter
canaries, and the Device Path Utilities GUID. The original 15 tests alone do
not test the port; these are additional registered tests.

## Disposable VM verification

Requires Python 3 and the installed Windows QEMU distribution, including stock
`share\edk2-x86_64-code.fd` and `share\edk2-i386-vars.fd`. After the build:

```powershell
$vmEvidence = Join-Path (Get-Location) ('.analysis\boot-chain-vm\' + [guid]::NewGuid().ToString('N'))
python.exe "$tools\Test-BootChainVm.py" --build-directory $build.PackageDirectory --evidence-directory $vmEvidence
```

This command starts four short, sequential, headless QEMU instances with 512 MiB
RAM, one emulated CPU, no networking, and generated 64 MiB GPT/FAT16 ESPs. Every
case has a private firmware-variable copy and a disposable disk overlay. The
base disk hash must remain unchanged. No physical disk or host ESP is attached.
Each instance exits through the fixture's debug-exit device; timeout handling
kills and waits for that owned QEMU process. No host reboot occurs.

The cases are:

| Case | Required result |
| --- | --- |
| Existing target | Restored bytes match backup; firmware starts the synthetic child; its returned error is preserved. |
| Missing target, retained stage | Canonical file is recreated; the same real firmware handoff succeeds. |
| Missing backup | Restoration fails closed and preserves the canonical file. |
| Missing parent directory | Create fails without a null dereference; backup and stage remain intact. |

Success is `BOOT_ONLY_VM_PASS`. Per-case logs, exit status, timeout result,
input hashes, base-image hashes, command lines, and a stopped-process result
are stored in the selected evidence directory. The synthetic child deliberately
returns `EFI_LOAD_ERROR`; this tests the same status value that can otherwise
be confused with a pre-entry firmware rejection.

## Explicit exclusions

No TPM mode downgrade, PCR replay/falsification, reconstructed logs, measurement
suppression, replacement TCG protocols, MOK/SBAT/SSP variable wiping, unsigned
manual payload mapping, hypervisor/AP/VMX execution, uACPI initialization, MOK
enrollment, signing, MokManager deployment, host ESP replacement, or host boot
selection changes occur in this workflow. Reference Rust sources and prebuilt
reference binaries are not executed. The full production `loader` remains a
separate target and is not the image exercised by this VM test.
