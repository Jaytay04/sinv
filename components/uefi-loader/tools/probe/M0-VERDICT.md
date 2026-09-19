# M0 Hard-Gate Verdict — Empty-Bank Precondition

> **verdict: PASS.** Environment A is MET through the nested-Linux patched-OVMF
> path. Environment B is the required verified `NOT_MET` negative control
> because VMware performs a genuine measured boot and populates PCR[0–7].
>
> Both probe scenarios in the boot-chain-recorder specification have passed.

## Environment A — OVMF + swtpm (measurement disabled)

**Update 2026-09-06 (positive path CONFIRMED):** The host runner built pinned
EDK2 `edk2-stable202605` (`b03a21a`) with the test-only six-file no-measure
patch, launched it under nested KVM/QEMU with a fresh swtpm, and captured
headless debugcon evidence. Run
`uefi-linux-positive-20260906-080808-ff88025b28` passed:

```text
EFI_TCG2_PROTOCOL: located
TPM present: YES
active PCR banks: 0x0000000F
SHA1   ... PCR[0-7] 00000000
SHA256 ... PCR[0-7] 00000000
SHA384 ... PCR[0-7] 00000000
SHA512 ... PCR[0-7] 00000000
gate verdict (04 banks): PCR banks empty - replay may proceed
```

The pre-firmware SHA-256 PCR 0–7 baseline was also all zero. The tracked
transcript and evidence hashes are in
`tools/probe/evidence/nested-linux-positive-20260906.txt`.

**Historical update 2026-09-05 (P4 negative path CONFIRMED):** The
environment is now real via a nested Linux VM on VMware Workstation (KVM →
QEMU 10.2.1 + Ubuntu OVMF `OVMF_CODE_4M.fd` + swtpm 0.10.1). Windows-native
blockers A1–A3 remain historically true but are bypassed — everything runs
natively inside the Linux guest.

The pre-firmware swtpm baseline was empty:

```
tpm2_getcap pcrs  -> sha1/sha256/sha384/sha512, 24 PCRs each
tpm2_pcrread      -> PCR 0-16 ALL ZEROS in every bank
                     PCR 17-22 = 0xFF..FF (DRTM reset values, spec-correct)
                     PCR 23 = zeros
```

The corrected QEMU boot connected its TPM emulator backend to swtpm's control
socket (`/tmp/swtpm-sock.ctrl`), booted the Ubuntu UEFI shell, and executed the
P4 `loader.efi`. Operator-provided screen evidence showed:

```text
EFI_TCG2_PROTOCOL: located
TPM present: YES
number of PCR banks: 00000004
active PCR banks: 0x0000000F
SHA1   ... PCR[0-7] 11111111
SHA256 ... PCR[0-7] 11111111
SHA384 ... PCR[0-7] 11111111
SHA512 ... PCR[0-7] 11111111
gate verdict (04 banks): ERROR: PCR banks not empty - aborting replay
no extensions performed; continuing boot unmodified
```

Therefore stock Ubuntu OVMF initializes Tcg2Dxe and measures the boot path into
all four active banks before the loader runs. This is a **PASS for the P4
abort-path smoke test** and **NOT MET for the empty-bank replay precondition**.

**Status before the nested lab: BLOCKED — no software TPM could be attached on
this machine.** Three independent blockers, each with real evidence:

| # | Blocker | Evidence (2026-09-05) |
|---|---|---|
| A1 | QEMU has no TPM emulator backend | `qemu-system-x86_64 -tpmdev help` → `-tpmdev: invalid option` (exit 1) on winget QEMU 11.1.0. MSYS2 `mingw-w64-x86_64-qemu` 11.1.1 PKGBUILD auto-disables TPM too (libtpms not packaged). |
| A2 | Prebuilt OVMF has no `Tcg2Dxe` | Byte-scan of `tools/ovmf/x64/code.fd`: `Tcg2Dxe`, `TcgDxe`, `TCG2`, `TPM2`, `Tcg2ConfigDxe` strings all absent; `EFI_TCG2_PROTOCOL` GUID bytes (`6c767f60 5574be42 930be4d7 6db2720f`) absent. |
| A3 | swtpm has no native Windows build | Upstream ships source only; no MSYS2/winget/choco package. Source has 0 Windows references; uses `fork()` ×8, `socketpair()` ×4, `poll()` ×13, `mmap()`, `AF_UNIX` sockets, `getopt_long()`, `setuid()`. libtpms v0.10.2 compiles to ~140/143 objects with 5 patches; swtpm itself is a porting project. |

### Boot evidence (real)

Ran `tools\launch-vm.ps1 -DiskPath <empty-2MB-raw> -Headless -SerialOut <file>`
(empty disk, `-snapshot` discipline, QEMU probes `-tpmdev` and skips TPM
devices). QEMU killed by pid after 25 s. Serial log (311 bytes):

```
BdsDxe: failed to load Boot0002 "UEFI Misc Device" from PciRoot(0x0)/Pci(0x2,0x0): Not Found
BdsDxe: No bootable option or device was found.
BdsDxe: Press any key to enter the Boot Manager Menu.
```

No `Tcg2`/`TPM` line appears — consistent with A1 (no TPM attached) AND A2
(no `Tcg2Dxe` in the firmware). This OVMF build cannot measure boot even if a
TPM were attached.

### Empty-bank answer

**MET through the nested-Linux path.** Windows-native blockers A1–A3 remain
true, but the Ubuntu VMware guest provides KVM, TPM-capable QEMU, swtpm, and a
locally built Tcg2-enabled OVMF. The test-only patch suppresses firmware/FV,
DXE boot/image, SMBIOS, and QEMU ACPI PCR extensions while retaining the TCG2
protocol.

| Check | Result | Evidence |
|---|---|---|
| TPM present + version 2.0 | [x] pass | `TPM present: YES`; TCG2 protocol located |
| Active PCR banks | 4 banks | SHA1, SHA256, SHA384, SHA512 (`0x0000000F`) |
| PCR[0–7] at reset values | [x] all zero | loader debugcon plus pre-firmware SHA-256 baseline |
| **Empty-bank verdict** | [x] MET | `PCR banks empty - replay may proceed` |

## Environment B — VMware Workstation guest

**Status: verified on real VMware (2026-09-05).** Probe executed inside a VMware
Workstation VM with a vTPM attached, using the static build of `ib-tcg-dump`
(`IB_STATIC_RUNTIME=ON`, zero redist dependency).

| Check | Result | Evidence |
|---|---|---|
| TPM present + version 2.0 | [x] pass | TPM 2.0; interface reported as `other` |
| Active PCR banks | 4 banks | SHA1 inactive; SHA256 / SHA384 / SHA512 populated |
| PCR[0–7] at reset values | [x] non-empty | PCR[0–7] NOT at reset |
| TBS log header = `Spec ID Event03` | [x] yes | crypto-agile log, 92,231 bytes; `Spec ID Event03` (TCG_PCR_EVENT-wrapped by Windows TBS) |
| **Empty-bank verdict** | [x] NOT MET | banks populated at probe time |

### Empty-bank answer

**NOT MET.** VMware measures boot before the probe runs, so PCR[0–7] are never
empty and the replay precondition cannot be exercised here. This is expected for
a default VMware guest (Secure Boot measurement active) and is the correct
negative control: the probe correctly reports the precondition as unmet.

## Overall gate

- [x] PASS — nested-Linux patched OVMF reports `MET`, while stock VMware
  reports the specified non-zero `NOT_MET` negative control.

## Sign-off

- Date: 2026-09-06
- Operator: user (Environment B probe, VMware Workstation VM with vTPM)
- Commands / scripts used:
  - `ib-tcg-dump probe` (static build) — Environment B verdict (NOT MET)
  - `qemu-system-x86_64 -tpmdev help` (A1)
  - byte-scan of `tools/ovmf/x64/code.fd` for `Tcg2Dxe`/`TCG2`/`TPM2` strings and `EFI_TCG2_PROTOCOL` GUID (A2)
  - MSYS2 MINGW64 source build attempt: libtpms v0.10.2 (5 patches, ~140/143 objects), swtpm v0.10.2 (0 Windows support) (A3)
  - `tools\launch-vm.ps1 -Headless -SerialOut` headless boot, serial log above (boot evidence)
  - nested-Linux lab (2026-09-05): swtpm socket + `TPM2TOOLS_TCTI="swtpm:path=/tmp/swtpm-sock" tpm2_getcap pcrs` / `tpm2_pcrread` → 4 banks, PCR 0–16 zeros (empty banks at swtpm layer)
  - nested-Linux P4 boot (2026-09-05): QEMU connected to `/tmp/swtpm-sock.ctrl`; `loader.efi` located TCG2, reported 4 active populated banks, returned `PCR banks not empty`, and performed no extensions (operator photo evidence)
  - host-runner positive path (2026-09-06): pinned patched OVMF + fresh swtpm; all four banks PCR 0–7 zero; loader returned `PCR banks empty - replay may proceed`; tracked transcript under `tools/probe/evidence/`
