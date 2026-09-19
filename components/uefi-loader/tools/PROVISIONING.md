# M0 Provisioning Notes — Sources & Versions

Status: sources verified 2026-08-30. SHA-256 pins are placeholders until the
first verified download on the lab host.

## QEMU

| Field | Value |
|---|---|
| Package | winget `SoftwareFreedomConservancy.QEMU` |
| Version (verified) | 11.1.0 |
| Source | https://winget.run/pkg/SoftwareFreedomConservancy/QEMU |
| Install | `tools\provision-qemu.ps1` (winget default; scoop/choco fallback) |

## OVMF (UEFI firmware)

| Field | Value |
|---|---|
| Repo | https://github.com/rust-osdev/ovmf-prebuilt |
| Release | `edk2-stable202605-r1` (2026-06-06) |
| Asset | `edk2-stable202605-r1-bin.tar.xz` (6.5 MB) |
| Contents | `x64/code.fd`, `x64/vars.fd`, `x64/shell.efi` |
| Build | standard OvmfPkgX64; the pinned rust-osdev image was later verified to omit usable `Tcg2Dxe` |
| Install | `tools\provision-ovmf.ps1` (downloads + extracts to `tools/ovmf/`) |

Measurement toggling: the earlier assumption that standard OVMF provides a
runtime "disable measurement but keep TCG2 active" setting was not supported by
testing. Ubuntu OVMF exposes `EFI_TCG2_PROTOCOL` and active-bank configuration,
but measured the boot path into every active bank before `loader.efi` ran.
Disabling the TPM or the SHA-256 bank does not satisfy the loader contract. A
test-specific firmware strategy is required for the positive empty-bank path.
The pinned test-only EDK2 patch and complete nested-Linux procedure are in
`tools/runbooks/nested-linux-positive-path.md`.

SHA-256 pin: after the first clean download, record the value here and pass it
to `provision-ovmf.ps1 -ExpectedSha256 <hash>`.

```text
SHA256 (edk2-stable202605-r1-bin.tar.xz) = <PENDING>
```

### Secure Boot variant (needed at M3)

`rust-osdev/ovmf-prebuilt` is the NON-Secure-Boot build. M3 signing work needs a
Secure Boot-capable firmware image (`OVMF_CODE.secboot.fd` + `OVMF_VARS.secboot.fd`).
Sources for that (documented, not yet wired):

- Fedora `edk2-ovmf` RPM (noarch) — extract via 7-Zip. Packages:
  https://packages.fedoraproject.org/pkgs/edk2/edk2-ovmf/
- QEMU Windows installer bundles OVMF (`OVMF_CODE.fd`, `OVMF_CODE.secboot.fd`, ...).

## swtpm (software TPM 2.0)

| Field | Value |
|---|---|
| Upstream | https://github.com/stefanberger/swtpm |
| Latest release | v0.10.2 (2026-08-19) — source tarball only |
| Windows binary | **None** (verified 2026-08-30 and again 2026-09-05) |
| MSYS2 package | **Absent** from package registry (checked packages.msys2.org) |
| winget / choco | **Absent** |

swtpm ships source tarballs only. There is no MSYS2, winget, or Chocolatey
package. **swtpm has zero Windows support in its source** (verified 2026-09-05:
0 `WIN32`/`_WIN32` references; uses `fork()` ×8 in `daemonize.c`, `socketpair()`
×4, `poll()` ×13, `mmap()` in `swtpm_nvstore_linear_file.c`, `AF_UNIX` sockets in
`common.c`/`tpm_bios.c`/`tpm_ioctl.c`, `getopt_long()`, and `setuid()`). A native
Windows build is therefore a porting project, not a configure-and-make.

### Native path: build from source under MSYS2 (MINGW64)

Pinned pairing (swtpm v0.10.2 requires `libtpms >= 0.10`, so the "v0.9.x
libtpms" example is invalid for it):

- **libtpms v0.10.2** — https://github.com/stefanberger/libtpms
- **swtpm v0.10.2** — https://github.com/stefanberger/swtpm

Toolchain: existing MSYS2 at `C:\msys64` (gcc 16.2.0, base-devel + autotools
already installed). No extra pacman packages were needed on the lab host:
`mingw-w64-x86_64-{libtasn1,gnutls,json-glib,pkgconf,glib2,openssl}` are all
preinstalled. Invoke with `MSYSTEM=MINGW64` and `/mingw64/bin` on PATH.

libtpms status (verified 2026-09-05): **compiles to ~140 of ~143 objects** with
these five portability patches applied to v0.10.2:

1. `include/libtpms/tpm_types.h` — skip `#include <netinet/in.h>` on Windows
   (vestigial; no byte-order symbols are used outside the disabled TPM 1.2 code).
2. `src/tpm2/TpmProfile_Common.h` — skip `#include <endian.h>` on Windows and
   add a Windows branch (`BIG_ENDIAN_TPM NO`) to the byte-order detection.
3. `src/tpm2/crypto/openssl/tpm_radix.h` — also accept OpenSSL's `SIXTY_FOUR_BIT`
   (Windows LLP64 has 32-bit `long`, so `SIXTY_FOUR_BIT_LONG` is never defined).
4. `src/tpm_library.c` — shim `dprintf`/`vdprintf` over `_write` (absent from
   the Windows CRT).
5. Build flags: `--without-tpm1 --with-tpm2 --with-openssl` and
   `CFLAGS=-D__USE_MINGW_ANSI_STDIO=1` (MSVCRT has no `%zu`).

Remaining blocker: `src/tpm2/simulator_sysheaders.h` + `CompilerDependencies.h`
assume the POSIX socket model (`typedef int SOCKET` under `_POSIX_`) which
clashes with MinGW's winsock headers (`SOCKET` = `UINT_PTR`). The TPM2 code's
`TPM_WINDOWS` path targets MSVC with 32-bit `RADIX_BITS` and is not wired into
the autotools build. Fixing this correctly is the boundary between "build" and
"port". swtpm itself is a larger port (the POSIX APIs listed above).

### WSL2 path

**Banned on this machine** (other projects need Hyper-V off). Do not use or
suggest WSL2 / Virtual-Machine-Platform.

### `tools\provision-swtpm.ps1`

Manages the swtpm socket lifecycle once `C:\msys64\mingw64\bin\swtpm.exe` exists:

- `provision-swtpm.ps1 -Action start`  — detached start with correct PATH,
  pidfile + stdout/stderr to files (state dir: `tools\swtpm\state`, gitignored).
- `provision-swtpm.ps1 -Action status` — `Test-NetConnection 127.0.0.1:2321`.
- `provision-swtpm.ps1 -Action stop`   — kill the pidfile-tracked process.

## Verification

For repeatable host-controlled Linux and Windows guest iterations, see
`tools/host-runner/README.md`.

- `tools\provision-qemu.ps1` then `qemu-system-x86_64 --version`
- `tools\provision-ovmf.ps1` then confirm `tools/ovmf/x64/code.fd` and `vars.fd`
- `tools\provision-swtpm.ps1` then start swtpm and launch `tools\launch-vm.ps1`

Known M0 environment blockers (verified 2026-09-05, see `tools/probe/M0-VERDICT.md`):
1. QEMU 11.1.0 (winget) has no TPM emulator backend (`-tpmdev` is an invalid
   option); MSYS2's QEMU PKGBUILD auto-disables TPM too (libtpms not packaged).
2. The prebuilt OVMF (`rust-osdev/ovmf-prebuilt` `edk2-stable202605-r1`) has no
   `Tcg2Dxe` — the binary contains no `Tcg2Dxe`/`TCG2`/`TPM2` strings and no
   `EFI_TCG2_PROTOCOL` GUID.
3. swtpm has no native Windows build (see above).
