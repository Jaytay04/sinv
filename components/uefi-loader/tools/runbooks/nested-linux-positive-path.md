# Nested-Linux Empty-PCR Positive-Path Test

This is the next operator test after the stock-OVMF negative-path result in
`nested-linux-lab.md`. It builds a dedicated, test-only OVMF image that keeps
the TPM 2.0 device, an active SHA-256 bank, and `EFI_TCG2_PROTOCOL`, while
suppressing OVMF's own PCR extensions. The expected result is that the current
loader reaches its empty-bank gate.

> **Test firmware only.** This patch intentionally disables measured-boot
> protections. Never install or use this OVMF build as production firmware.

## 1. Build and test the current project on Windows

Open an **x64 Native Tools/Developer PowerShell** in the project root:

```powershell
cmake --preset windows
cmake --build --preset windows
ctest --test-dir build\windows --output-on-failure
```

The expected result is four passing host tests, including `replay_test`. Copy
these two files into the Ubuntu VM:

```text
build/windows/loader.efi                    -> ~/loader.efi
tools/patches/edk2-stable202605-no-measure.patch
                                            -> ~/edk2-stable202605-no-measure.patch
```

Inside Ubuntu, confirm both files arrived:

```bash
file ~/loader.efi
ls -l ~/loader.efi ~/edk2-stable202605-no-measure.patch
```

`file` must identify `loader.efi` as an x86-64 PE/EFI application.

## 2. Install the Ubuntu build and lab packages

```bash
sudo apt update
sudo apt install -y \
  git build-essential uuid-dev nasm acpica-tools python3 \
  qemu-system-x86 ovmf swtpm swtpm-tools tpm2-tools \
  dosfstools mtools efi-shell-x64
```

Verify nested KVM is available:

```bash
qemu-system-x86_64 --version
swtpm --version
ls -l /dev/kvm
```

If `/dev/kvm` is missing, stop here and enable nested virtualization for the
Ubuntu VMware VM. Do not silently fall back to a different TPM/firmware test.

## 3. Clone the exact EDK2 revision

This procedure is pinned to `edk2-stable202605` (`b03a21a`). Use a dedicated
source directory so another EDK2 checkout is not modified.

```bash
mkdir -p ~/src
git clone \
  --branch edk2-stable202605 \
  --depth 1 \
  --recurse-submodules \
  --shallow-submodules \
  https://github.com/tianocore/edk2.git \
  ~/src/edk2-uefiloader

cd ~/src/edk2-uefiloader
git rev-parse --short=7 HEAD
git status --short
```

The `git rev-parse` command must print `b03a21a`; the status output must be
empty.

## 4. Apply the test-only no-measure patch

EDK2 keeps some source files with CRLF endings, so use
`--ignore-space-change` for this patch.

```bash
cd ~/src/edk2-uefiloader

git apply --ignore-space-change --check \
  ~/edk2-stable202605-no-measure.patch

git apply --ignore-space-change \
  ~/edk2-stable202605-no-measure.patch

git diff --check
git diff --stat
```

The stat must name these six files:

```text
OvmfPkg/Include/Dsc/OvmfTpmSecurityStub.dsc.inc
OvmfPkg/OvmfPkgX64.dsc
OvmfPkg/OvmfPkgX64.fdf
OvmfPkg/Library/AcpiPlatformLib/QemuFwCfgAcpi.c
SecurityPkg/Tcg/Tcg2Dxe/Tcg2Dxe.c
SecurityPkg/Tcg/Tcg2Pei/Tcg2Pei.c
```

The patch retains TPM initialization and `Tcg2Dxe`, but disables PEI
firmware/FV measurement, DXE boot callbacks, image measurement, SMBIOS
measurement, and QEMU ACPI-data measurement into PCR 1.

## 5. Build the patched OVMF image

```bash
cd ~/src/edk2-uefiloader
make -C BaseTools -j"$(nproc)"
. edksetup.sh

build \
  -a X64 \
  -t GCC \
  -b RELEASE \
  -p OvmfPkg/OvmfPkgX64.dsc \
  -D TPM2_ENABLE=TRUE \
  -D TPM1_ENABLE=FALSE \
  -D SECURE_BOOT_ENABLE=FALSE \
  -D SMM_REQUIRE=FALSE \
  -D CC_MEASUREMENT_ENABLE=FALSE \
  -D BUILD_SHELL=TRUE
```

The build must end with `Build end time` and `- Success -`. Verify the exact
firmware pair before continuing:

```bash
ls -lh \
  Build/OvmfX64/RELEASE_GCC/FV/OVMF_CODE.fd \
  Build/OvmfX64/RELEASE_GCC/FV/OVMF_VARS.fd
```

Set two convenience paths for the remaining commands:

```bash
export IB_OVMF_CODE="$HOME/src/edk2-uefiloader/Build/OvmfX64/RELEASE_GCC/FV/OVMF_CODE.fd"
export IB_OVMF_VARS="$HOME/src/edk2-uefiloader/Build/OvmfX64/RELEASE_GCC/FV/OVMF_VARS.fd"
```

## 6. Create a fresh TPM state

The following paths are disposable and specific to this test:

```bash
pkill -f 'swtpm socket.*swtpm-positive' 2>/dev/null || true

rm -f \
  /tmp/swtpm-positive.sock \
  /tmp/swtpm-positive.sock.ctrl \
  /tmp/ovmf-positive-vars.fd \
  /tmp/loader-positive.img \
  /tmp/positive-startup.nsh \
  /tmp/swtpm-positive-baseline.txt

rm -rf /tmp/swtpm-positive-state
mkdir -p /tmp/swtpm-positive-state

swtpm socket \
  --tpm2 \
  --tpmstate dir=/tmp/swtpm-positive-state \
  --server type=unixio,path=/tmp/swtpm-positive.sock \
  --ctrl type=unixio,path=/tmp/swtpm-positive.sock.ctrl \
  --flags not-need-init,startup-clear \
  --daemon

ls -l /tmp/swtpm-positive.sock /tmp/swtpm-positive.sock.ctrl
```

The socket roles are different:

- `tpm2-tools` uses `/tmp/swtpm-positive.sock`.
- QEMU uses `/tmp/swtpm-positive.sock.ctrl`.

## 7. Prove the pre-firmware PCR baseline

Run this before QEMU attaches:

```bash
export TPM2TOOLS_TCTI="swtpm:path=/tmp/swtpm-positive.sock"

tpm2_getcap pcrs
tpm2_pcrread sha256:0,1,2,3,4,5,6,7 \
  | tee /tmp/swtpm-positive-baseline.txt
```

Every SHA-256 PCR from 0 through 7 must be all zero. If any is non-zero,
discard `/tmp/swtpm-positive-state`, restart from section 6, and do not launch
QEMU until this baseline passes.

## 8. Create the bootable FAT image

```bash
truncate -s 64M /tmp/loader-positive.img
mkfs.vfat -F 32 /tmp/loader-positive.img

mmd -i /tmp/loader-positive.img ::/EFI
mmd -i /tmp/loader-positive.img ::/EFI/BOOT

mcopy -i /tmp/loader-positive.img \
  /usr/share/efi-shell-x64/shellx64.efi \
  ::/EFI/BOOT/BOOTX64.EFI

mcopy -i /tmp/loader-positive.img \
  ~/loader.efi \
  ::/loader.efi

printf 'map -r\r\nfs0:\\loader.efi\r\necho\r\necho Loader returned. Record the output above.\r\npause\r\n' \
  > /tmp/positive-startup.nsh

mcopy -i /tmp/loader-positive.img \
  /tmp/positive-startup.nsh \
  ::/startup.nsh

mcopy -i /tmp/loader-positive.img \
  /tmp/positive-startup.nsh \
  ::/EFI/BOOT/startup.nsh

mdir -i /tmp/loader-positive.img ::/
mdir -i /tmp/loader-positive.img ::/EFI/BOOT
```

## 9. Boot the positive-path test

Create a writable variable-store copy:

```bash
cp "$IB_OVMF_VARS" /tmp/ovmf-positive-vars.fd
```

Launch QEMU visibly. Do not use `timeout`: the loader writes through UEFI
`ConOut`, so the GTK window is the authoritative evidence.

```bash
qemu-system-x86_64 \
  -machine q35,accel=kvm \
  -cpu host \
  -m 2048 \
  -drive if=pflash,format=raw,readonly=on,file="$IB_OVMF_CODE" \
  -drive if=pflash,format=raw,file=/tmp/ovmf-positive-vars.fd \
  -chardev socket,id=chrtpm,path=/tmp/swtpm-positive.sock.ctrl \
  -tpmdev emulator,id=tpm0,chardev=chrtpm \
  -device tpm-tis,tpmdev=tpm0 \
  -drive if=virtio,format=raw,file=/tmp/loader-positive.img \
  -boot order=c,menu=on \
  -net none \
  -display gtk \
  -monitor none \
  -no-reboot
```

Wait for `startup.nsh` to run. If the shell does not find the loader, run:

```text
map -r
fs0:\loader.efi
```

Press `Ctrl+Alt+G` before closing the QEMU window if it grabbed the pointer.

## 10. Pass/fail evidence

The current P4/P5-core loader is expected to print:

```text
EFI_TCG2_PROTOCOL: located
TPM present: YES
active PCR banks: 0x........
SHA256 ... PCR[0-7] 00000000
gate verdict (.. banks): PCR banks empty - replay may proceed
no extensions performed; continuing boot unmodified
```

Pass only if all of these are true:

1. `EFI_TCG2_PROTOCOL` is located and the TPM is present.
2. The active-bank mask includes SHA-256 (`0x00000002`).
3. Every displayed PCR 0-7 bitmap is `00000000`.
4. The gate says `PCR banks empty - replay may proceed`.

Capture a photograph or exact transcript of the GTK window. Do not run
`tpm2_pcrread` concurrently with QEMU; QEMU owns the swtpm connection, and the
loader's own pre-extension read is the authoritative gate evidence.

If the bitmaps remain `11111111`, the test firmware is still measuring. Record
the full loader output and the EDK2 `git diff --stat`; do not proceed to replay
integration on that firmware.

This test validates the empty-bank environment only. The current EFI entry
still reports that replay is not integrated; actual PCR extension begins after
tasks 2.4-2.6 wire the portable replay core to UEFI data resolution, TPM
commands, the divergence manifest, and handoff.
