# Restore-first Windows boot-chain candidate

This isolates the boot-manager restoration and Windows handoff pattern from the
local `insecure-boot` reference. Hypervisor activation is deferred. The candidate
is a separate `boot_chain_loader.efi`; it does not replace the existing `loader`
target or change the GPU host workflow.

## Implemented flow

The intended, separately validated chain is firmware → signed shim at the
canonical Windows Boot Manager path → signed `grubx64.efi` sibling containing
this loader → restored Windows Boot Manager → Windows.

The new entry runs the existing `boot_restore_run()` transaction first. It
requires an installer-owned backup and SHA-256 record, verifies the backup,
writes and verifies a temporary copy, replaces the canonical target, verifies
the restored bytes, and cleans up the verified transaction. A missing or
invalid backup prevents Windows image loading; it does not guess another file.
The restore hash record remains for post-boot verification. FAT rename operations
are not an atomic power-loss guarantee.

Only after restoration succeeds does the loader build a full device path from
its loaded ESP device handle to `\EFI\Microsoft\Boot\bootmgfw.efi`. It calls
firmware `LoadImage(FALSE, parent, path, NULL, 0, ...)`, then `StartImage`.
`FALSE` corresponds to the reference's
[`BootPolicy::ExactMatch`](https://docs.rs/uefi/latest/uefi/proto/enum.BootPolicy.html).
No source buffer, manual PE mapper, shim verification hook, or security-policy
override is used. Firmware applies its configured authentication policy;
Secure Boot must be enabled and checked independently during a later boot test.

Console events and best-effort `EFI\insecure-boot\handoff-status.txt` events
identify entry, restore, path construction, image load, image start, and returned
errors. The marker header states `BOOT_CHAIN_ONLY`, `replay=NOT_REQUESTED`, and
`payload=NOT_REQUESTED`. File-write errors cannot veto restoration or handoff.
Cleanup errors are recorded without replacing the primary launch error. A
return from Windows is abnormal, including a returned `EFI_SUCCESS`, which is
reported as `EFI_ABORTED`.

## Build and host tests

Run from the shared repository root in a normal PowerShell window:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\boot_chain_diagnostic\Build-BootChainDiagnostic.ps1 -WhatIf
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\boot_chain_diagnostic\Build-BootChainDiagnostic.ps1
```

The real command discovers an existing Visual Studio C++ installation, enables
its x64 environment in that process, configures the loader component with
`IB_BUILD_BOOT_CHAIN_LOADER=ON`, builds it and its host fixtures, and runs the
component CTest suite. The option defaults to `OFF` in ordinary builds. Other
existing component targets may be compiled for the suite; only the boot-chain
image is copied into the package.

It saves logs, exit-code receipts, source hashes, EFI headers, and an **unsigned**
candidate under `.analysis\boot-chain-diagnostic\<id>`. Build state uses a short,
unique `.codex-build\bc\<id>` path. It checks source pins again after the build
and verifies the package copy. Success is
`BUILD_AND_TESTS_PASS_UNSIGNED_NOT_BOOT_VALIDATED`. It neither signs nor deploys
anything, mounts an ESP, writes firmware variables, starts a VM, nor reboots.
`-WhatIf` returns before any filesystem access or native command.

Host tests exercise restore-before-load ordering, failed restoration blocking
load/start, the full device-path prefix and Windows file node, exact-match/null
source arguments, path/load/start errors, child/exit-data cleanup, cleanup-error
markers, a returned success status, and marker failures that do not block the
handoff. Existing restore tests exercise file transaction and rollback failures.
These tests do not validate firmware image authentication or an actual Windows
boot.

## What was not incorporated

| Reference behavior | Treatment in this candidate |
| --- | --- |
| AMI `Setup` TCG version byte edits, downgrade to TCG 1.2, automatic cold reset, and later restoration to TCG 2.0 | Omitted. No firmware setup writes or resets. |
| Direct TPM CRB discovery/commands and replaying saved measurements into PCR0–7 | Omitted. No TPM command or PCR replay code is linked. |
| Replacing/displacing TCG/TCG2 protocols, publishing reconstructed logs, suppressing later measurements, and protocol self-exercise/uninstall | Omitted. The firmware's genuine measurement path remains in place. |
| Deleting MOK, SBAT, SSP, and other shim security variables, including the cross-GUID sweep | Omitted. No NVRAM variable cleanup or evidence removal. |
| Manual mapping and direct entry into an unsigned payload | Omitted. No payload is loaded at all; normal firmware loading is used for Windows. |
| Hypervisor, VMX/AP startup, GPU isolation, or post-activation storage handling | Deferred. This candidate establishes the boot-only boundary and does not fix or attest those behaviors. |
| uACPI initialization, TPM/ACPI probes, and associated Rust crates | Omitted because this path needs only ordinary firmware file and image services. |
| Deleting or wiping the staged payload, saved TCG log, configuration, MokManager, and running loader before handoff | Omitted. This path has none of the payload/log/config inputs and performs no broad artifact wipe. Only the existing verified restore transaction removes its temporary/displaced files and consumed backup. |
| Root-level `\ib-bootmgfw.efi` backup and restoration of saved file timestamps/attributes | Kept this repository's sibling `.bak`/`.sha256` transaction instead. Original file metadata is not restored by that helper. |
| Reference prebuilt executables, EFI binaries, Rust loader replacement, and five-second error stall | Not copied or executed. New C sources reuse this repository's restore/file helpers; failures return with console/file diagnostics. |
| Automatic signing, MOK enrollment, MokManager deployment, ESP replacement, BootNext/BootOrder changes, and boot testing | Not automated by this build script. Candidate remains unsigned and unbooted. |

## Deployment boundary and remaining work

The existing installer has a legacy `oneshot --mode replace` route with the
required `.bak`, `.sha256`, `.ibnew`, `.ibold`, and Microsoft Boot sibling paths.
It has **not** been changed or invoked here. Its legacy preflight checks manifest
hashes but does not call its Authenticode verification callback. Do not treat
those hashes as proof that a loader is signed or trusted. A later signed
deployment workflow must independently verify the shim pin, candidate signer,
MOK certificate/enrollment, Secure Boot state, original boot-manager backup,
and owned recovery state before replacing anything.

No primary-host runbook for canonical boot-manager replacement is supplied.
The next end-to-end validation belongs in an explicitly selected disposable VM
with a snapshot and verified recovery, when VM use is available. No VM was
started for this implementation. Existing run journals, archived selectors, and
retained ESP stage files remain outside this source/build change.

Reference points: `source/host/ib-install/src/esp.rs` for canonical placement;
`source/crates/ib-loader/src/bootmgfw.rs` for restore/path-based handoff;
`main.rs`, `nvram.rs`, `payload.rs`, and `source/crates/ib-tcg2/src/lib.rs` for the
omitted behaviors. Inspection of those sources is not independent evidence of
successful execution on this host.
