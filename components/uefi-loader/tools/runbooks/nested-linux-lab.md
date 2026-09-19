# Nested-Linux TPM Lab Runbook

This runbook validates `loader.efi` in a nested Linux lab:

```
Windows host -> VMware Ubuntu guest -> KVM/QEMU -> OVMF + swtpm
```

The stock Ubuntu OVMF test is a negative-path test. It proves that the loader
can locate `EFI_TCG2_PROTOCOL`, query a real emulated TPM, detect populated
PCR banks, and abort without extending anything. It does **not** provide the
empty-PCR positive path required for replay.

## 1. Prepare the outer Ubuntu VM

In VMware Workstation, enable **Virtualize Intel VT-x/EPT** (or AMD-V/RVI) for
the Ubuntu VM. Allocate at least 4 GB RAM and 40 GB disk.

Inside Ubuntu, install the lab packages:

```bash
sudo apt update
sudo apt install -y \
  qemu-system-x86 ovmf swtpm swtpm-tools tpm2-tools \
  dosfstools mtools efi-shell-x64
```

Verify KVM and the required files:

```bash
qemu-system-x86_64 --version
swtpm --version
ls -l /dev/kvm
ls -l /usr/share/OVMF/OVMF_CODE_4M.fd
ls -l /usr/share/OVMF/OVMF_VARS_4M.fd
ls -l /usr/share/efi-shell-x64/shellx64.efi
```

Ubuntu uses the `*_4M.fd` OVMF names shown above. On Debian, inspect
`/usr/share/OVMF/` and substitute the installed CODE/VARS pair.

## 2. Copy the Windows-built loader

Build `loader.efi` on Windows from a Developer PowerShell:

```powershell
cmake --preset windows
cmake --build --preset windows
ctest --test-dir build\windows --output-on-failure
```

Copy `build/windows/loader.efi` into the Ubuntu VM as `~/loader.efi`, then
verify it:

```bash
file ~/loader.efi
ls -l ~/loader.efi
```

`file` must identify an x86-64 PE/EFI application. Do not copy
`ib-tcg-dump.exe` into the UEFI image: it is a Windows user-mode executable,
not a UEFI application.

## 3. Start from a clean swtpm state

The following paths belong only to this disposable test:

```bash
pkill -f 'swtpm socket.*swtpm-sock' 2>/dev/null || true

rm -f \
  /tmp/swtpm-sock \
  /tmp/swtpm-sock.ctrl \
  /tmp/ovmf-vars.fd \
  /tmp/artifacts.img \
  /tmp/startup.nsh \
  /tmp/ovmf-serial.log \
  /tmp/swtpm-baseline.txt

rm -rf /tmp/swtpm-state
mkdir -p /tmp/swtpm-state
```

Start swtpm with separate data and control sockets:

```bash
swtpm socket \
  --tpm2 \
  --tpmstate dir=/tmp/swtpm-state \
  --server type=unixio,path=/tmp/swtpm-sock \
  --ctrl type=unixio,path=/tmp/swtpm-sock.ctrl \
  --flags not-need-init,startup-clear \
  --daemon

ls -l /tmp/swtpm-sock /tmp/swtpm-sock.ctrl
```

The distinction is important:

- `tpm2-tools` connects to the data socket `/tmp/swtpm-sock`.
- QEMU's TPM emulator backend connects to the control socket
  `/tmp/swtpm-sock.ctrl`.

## 4. Record the pre-firmware baseline

Before QEMU attaches, prove that the fresh TPM starts with empty PCRs:

```bash
export TPM2TOOLS_TCTI="swtpm:path=/tmp/swtpm-sock"

tpm2_getcap pcrs
tpm2_pcrread sha256:0,1,2,3,4,5,6,7 \
  | tee /tmp/swtpm-baseline.txt
```

PCR 0-7 must all be zero. This proves the swtpm baseline only; it says
nothing yet about what OVMF will measure.

## 5. Create an automatically bootable EFI image

Create a fresh FAT image:

```bash
truncate -s 64M /tmp/artifacts.img
mkfs.vfat -F 32 /tmp/artifacts.img

mmd -i /tmp/artifacts.img ::/EFI
mmd -i /tmp/artifacts.img ::/EFI/BOOT
```

Install the Ubuntu UEFI shell as the removable-media boot application and put
the loader at the filesystem root:

```bash
mcopy -i /tmp/artifacts.img \
  /usr/share/efi-shell-x64/shellx64.efi \
  ::/EFI/BOOT/BOOTX64.EFI

mcopy -i /tmp/artifacts.img ~/loader.efi ::/loader.efi
```

Create `startup.nsh` so the shell runs the loader and pauses afterward:

```bash
printf 'map -r\r\nfs0:\\loader.efi\r\necho\r\necho Loader returned. Record the output above.\r\npause\r\n' \
  > /tmp/startup.nsh

mcopy -i /tmp/artifacts.img /tmp/startup.nsh ::/startup.nsh
mcopy -i /tmp/artifacts.img /tmp/startup.nsh ::/EFI/BOOT/startup.nsh
```

Verify the image contents:

```bash
mdir -i /tmp/artifacts.img ::/
mdir -i /tmp/artifacts.img ::/EFI/BOOT
```

## 6. Boot the P4 loader test

Use a disposable copy of the OVMF variable store:

```bash
cp /usr/share/OVMF/OVMF_VARS_4M.fd /tmp/ovmf-vars.fd
```

Run QEMU visibly. The loader writes through UEFI `ConOut`, so the GTK window
is the authoritative output; the serial file is supplemental only.

```bash
qemu-system-x86_64 \
  -machine q35,accel=kvm \
  -cpu host \
  -m 2048 \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=/tmp/ovmf-vars.fd \
  -chardev socket,id=chrtpm,path=/tmp/swtpm-sock.ctrl \
  -tpmdev emulator,id=tpm0,chardev=chrtpm \
  -device tpm-tis,tpmdev=tpm0 \
  -drive if=virtio,format=raw,file=/tmp/artifacts.img \
  -boot order=c,menu=on \
  -net none \
  -display gtk \
  -serial file:/tmp/ovmf-serial.log \
  -monitor none \
  -no-reboot
```

Do not wrap this first visible run in `timeout`. Wait for `startup.nsh` to run,
photograph or transcribe the result, and then close QEMU. Press `Ctrl+Alt+G`
first if QEMU has grabbed the pointer.

## 7. Interpret the result

### Expected stock-Ubuntu negative path

The verified Ubuntu OVMF result is:

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

This is a **PASS for the P4 abort-path smoke test** and **NOT MET for the
empty-bank replay precondition**. OVMF measured the firmware/shell/boot path
before `loader.efi` ran, and the loader correctly refused to double-extend.

### Other outcomes

- `EFI_TCG2_PROTOCOL not found`: the selected OVMF build lacks usable TCG2;
  it cannot validate the loader/TPM path.
- `PCR banks empty - replay may proceed`: positive-path environment found;
  preserve the exact firmware build, QEMU command, and swtpm state as evidence.
- `SHA-256 bank not active`: the environment cannot satisfy the replay contract.

## 8. swtpm lifecycle after QEMU exits

QEMU may shut down the swtpm process when its control session closes. If
`/tmp/swtpm-sock` no longer exists, a later host-side `tpm2_pcrread` will fail.
Do not compare the empty file produced by a failed `tpm2_pcrread | tee` against
the baseline; that is not PCR evidence.

For P4, the loader's per-bank bitmap is sufficient because it reads PCR 0-7
through the real `EFI_TCG2_PROTOCOL` before making the gate decision.

## 9. Next test boundary

Do not repeat this stock-OVMF boot expecting empty banks: the result is now
known and reproducible. Development proceeds with P5's portable replay core
and host tests. The next Linux replay test begins only after both are true:

1. P5 can consume a synthetic parsed log and execute the replay callback
   sequence. This is now covered by the host-side `replay_test`.
2. The test-only EDK2 patch and full operator procedure in
   `nested-linux-positive-path.md` retain `EFI_TCG2_PROTOCOL` plus an active
   SHA-256 bank while suppressing firmware extensions into PCR 0-7.

Disabling the TPM or disabling SHA-256 is not a substitute: either choice
violates the loader's preconditions. Until that positive-path firmware exists,
the nested lab remains the verified P4 negative control.
