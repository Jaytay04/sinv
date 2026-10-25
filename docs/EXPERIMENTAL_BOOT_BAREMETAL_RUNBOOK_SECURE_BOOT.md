# Bare-metal runbook: Secure Boot ON, shim + MOK, then the hypervisor

Status: constructed from source, never executed. The shim/MOK path has not been
validated even in a VM — the project's own provenance note says "Secure Boot
acceptance must still be verified in the snapshot-protected target VM before
release". Treat boot 1 (enrollment) as the acceptance test: run
`EXPERIMENTAL_BOOT_VM_RUNBOOK_SECURE_BOOT.md` first; sections 5-7 below assume
that pass is green.

This is the variant where Secure Boot stays **on**. The sibling
`EXPERIMENTAL_BOOT_BAREMETAL_RUNBOOK.md` covers the unsigned path with Secure
Boot off; the two do not share artifacts or recovery steps.

## 0. What changes, and the two traps

Chain: firmware `Windows Boot Manager` -> **shim** -> **signed loader**
(`grubx64.efi`) -> unsigned hypervisor (mapped by hand, `LoadImage` bypassed) ->
Windows. Secure Boot never turns off; the payload is launched around
verification, not through it.

Trap 1 — `EFI\Microsoft\Boot\bootmgfw.efi` is replaced by shim. The loader puts
the real boot manager back before it wipes anything, but if it aborts first you
must restore it by hand (section 8). Keep `\ib-bootmgfw.efi` reachable.

Trap 2 — the loader deletes `MokList`, `MokListRT`, `MokListX`, `MokSBState`, …
as part of its sweep (`ib_nvram.c:27-37`). One run consumes the enrollment: a
second run needs `ib-install` again so MokManager asks again. Secure Boot stays
on and Windows keeps booting, because Windows' own images are in `db`.

Also: no `BootNext`, no hand-staged manifest, no `EFI\hsinv-boot` directory. The
installer does the staging, and the flag set is different from the other runbook.

## 1. Files, and where they come from

Working directory: `C:\hsinv-sb`. Five files plus the payload.

| Name in CWD | Copy from | Bytes | SHA-256 |
| --- | --- | --- | --- |
| `ib-install.exe` | `.codex-build\insecure-boot-c\reference_binaries\ib-install.exe` | 1,761,792 | `F9AD423DBDE31E341DB431CCC9E2A689DC2F7A10820BEDF9D154B5BC776FAAAD` |
| `ib-loader.efi` | `.codex-build\vm-test-handoff\dist\loader.efi` (renamed) | 89,088 | `B1D264F149D07264CF0473248C2CE98754C73922AA701681D596356698CAE417` |
| `shimx64.efi` | `.codex-build\insecure-boot-c\reference_binaries\shimx64.efi` | 1,036,008 | `351E131D3C4A636704E9CAE7D1296BAF8A05BECA9922CFEBBFA48933C9BF1AD3` |
| `mmx64.efi` | `.codex-build\insecure-boot-c\reference_binaries\mmx64.efi` | 874,360 | `D383E37E4CF82DFFCAAD3A0BD54C7634E04E13169297AE157DBF205122E9C44A` |
| `tcglog.ib` | generated in section 2 | small | not pinned |
| payload | your physical hypervisor build | 173,568 | record it |

Why these exact two binaries:

- `shimx64.efi` here is the **Fedora/Red Hat** shim (Microsoft-signed; its
  embedded `vendor_cert` names Red Hat/Fedora). `mmx64.efi` is Fedora's
  MokManager, signed by the same vendor CA, which is what that shim will
  verify before it runs it. They are a matched pair.
- Do **not** use `hsinv_loader\vendor\shimx64.efi` (hash `6FE6E1BC…`, Ubuntu
  1.58). It is Microsoft-signed and hash-pinned, but it is a **Ubuntu** shim and
  the tree ships no Ubuntu MokManager — an enrollment on that shim would need a
  MokManager signed by the Canonical CA. Mismatched vendor certs mean shim
  refuses MokManager and nothing can be enrolled.

The loader choice: `dist\loader.efi` is the C build with the fix for firmware
that keeps no AMI `Setup` variable, and it carries an `.sbat` section
(`hsinv-boot,1,…`) that shim 15.3+ requires, so it can be signed. The Rust
image the tool shipped with is `reference_binaries\ib-loader.efi`
(`26B756D274AD1D20B0F1572CD676352743202AC58F749D8D359D2C39E5E62DE9`); keep a
copy. If signing or the run misbehaves, swap it in to tell "my loader" from
"the tool" apart.

The payload must be the **physical** build (no `HYPERSINV_GPU_VM_SELFTEST`), same
recipe as section 3.2 of the sibling runbook.

## 2. Assemble the working directory

```powershell
$Src    = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\.codex-build'
$RefBin = Join-Path $Src 'insecure-boot-c\reference_binaries'
$Dist   = Join-Path $Src 'vm-test-handoff\dist'
$Hv     = 'C:\hsinv-baremetal\uefi_hypervisor_physical.efi'
$Work   = 'C:\hsinv-sb'

New-Item -ItemType Directory -Force -Path $Work | Out-Null
Copy-Item (Join-Path $RefBin 'ib-install.exe') $Work -Force
Copy-Item (Join-Path $RefBin 'shimx64.efi')    $Work -Force
Copy-Item (Join-Path $RefBin 'mmx64.efi')      $Work -Force
Copy-Item (Join-Path $Dist   'loader.efi')     (Join-Path $Work 'ib-loader.efi') -Force

# The loader parses this file as an IBTCGLOG dump and aborts the run if it is
# not one, so it has to be a real capture - a text placeholder stops the payload
# from ever starting.
Set-Location $Work
& (Join-Path $RefBin 'ib-tcg-dump.exe') (Join-Path $Work 'tcglog.ib')
Set-Location $env:TEMP

Get-ChildItem $Work | ForEach-Object {
    "$($_.Name)  $($_.Length)  $((Get-FileHash $_.FullName -Algorithm SHA256).Hash)"
}
```

Compare the four hashes with section 1 before going on. `tcglog.ib` is the one
that varies: it is whatever the machine measured on this boot.

Two traps, both learned on hardware. `dist\ib-tcg-dump.exe capture` writes an
`IBLG` boot-chain-recorder container, which the loader rejects with
`the file does not begin with a TCG replay dump signature` - use the Rust
tool in `reference_binaries` (it takes the output path positionally) as above.
And a text placeholder is worse than nothing: the loader wipes the staged files
and restores Windows before it parses this dump, so a bad dump costs a boot and
never reaches the payload. If the capture fails, stage nothing: the loader then
prints `no tcglog.ib in the root of the boot volume` and runs the payload
without the PCR replay.

## 3. Pre-flight

Identical to the unsigned runbook (firmware entry, VT-x, VT-d, BitLocker,
VBS/Hyper-V off, ESP backup to `C:\hsinv-baremetal\esp-backup`), with these
differences:

- Secure Boot must be **on**: `Confirm-SecureBootUEFI` -> `True`.
- No firmware boot-menu work: the Windows Boot Manager entry itself is hijacked.
- Reconfirm `manage-bde -status C:` is `Protection Off`/`Suspended`, and record
  the recovery key. BitLocker plus a changed TPM spec-version byte is exactly
  what a recovery prompt is made of.
- `robocopy` will report `FAILED 2` on `EFI\Microsoft\Boot\BCD` and `BCD.LOG`:
  Windows holds them open. That is expected and harmless here — staging never
  writes those two, and if a full ESP restore is ever needed, `bcdboot` rebuilds
  them. What has to match is the boot manager: check the backup's
  `bootmgfw.efi` hash against the one `preflight.txt` recorded.
- Start serial capture from a second machine (115200 8N1) — the hypervisor still
  speaks only `0x3F8`, and nothing on the booting machine can read it.

### Serial capture, concretely

It cannot run on the machine under test. That machine reboots, so any reader
process dies with it, and during boot 1-3 no OS is running to read the port at
all. Serial is still the only live channel for the payload
(`HvEfiDebugPrintAscii` writes `0x3F8` and `0xE9`; the hypervisor never uses
`ConOut`), so the screen stays blank once it starts.

Capture from a second machine, wired to the board's COM header:

- second PC with a USB-serial adapter, or DB9 plus a null-modem cable;
- a BMC/IPMI Serial-over-LAN console, with the session logged to a file;
- a hardware serial logger.

On that second machine, point the port at the adapter (not necessarily `COM1`)
and log to a file:

```powershell
$Log  = 'C:\hsinv-baremetal\hypervisor-serial.log'
$Port = New-Object System.IO.Ports.SerialPort 'COM3',115200,'None',8,'One'
$Port.ReadTimeout = 500
$Port.Open()
"capturing 115200 8N1 to $Log - leave this window open"
$Stop = (Get-Date).AddMinutes(60)
while ((Get-Date) -lt $Stop) {
    try {
        $Chunk = $Port.ReadExisting()
        if ($Chunk) { Add-Content -LiteralPath $Log -Value $Chunk -NoNewline -Encoding Ascii }
    } catch { }
    Start-Sleep -Milliseconds 100
}
$Port.Close()
```

Or PuTTY there: Serial, the adapter's port, 115200, 8N1, Logging -> "All session
output". Start it **before** the reboot in section 5.

Two things to expect:

- `COM1` in this machine's Device Manager proves nothing about the board: the
  SuperIO port may have no header wired. Check the board before relying on it.
- The log stays empty through boot 1 (MokManager) and boot 2 (loader): the
  loader prints to the screen only, because the shipped `dist\loader.efi` was
  built without `IB_CONSOLE_SERIAL_MIRROR`. First bytes should be
  `uefi hypervisor: ...` in boot 3.

With no second machine, skip serial. The screen going blank after the loader
lines is expected, and the verdict still comes from section 7: `ib-tcg-dump.exe
health` in Windows after boot 3. You lose the in-boot markers, not the result.

## 4. Sign and stage

Elevated PowerShell, inside the working directory, console attached (it prompts):

```powershell
Set-Location C:\hsinv-sb
.\ib-install.exe C:\hsinv-baremetal\uefi_hypervisor_physical.efi
```

Before it prompts, the tool probes the firmware: it reads the AMI `Setup`
variable (`{EC87D643-EBA4-4BB5-A1E5-3F3E36B20DA9}`) and writes it back with zero
bytes changed, to prove the writes the staged boot needs are allowed. Three ways
that probe can stop the run:

| Message | Meaning | Fix |
| --- | --- | --- |
| `the firmware keeps no Setup variable; the offset this run asks for has nothing to point into` | the variable is absent (VMware VMs return `reading the Setup firmware variable failed with 0x000000cb` instead) | not an AMI Aptio firmware, or the machine is a VM: this runbook needs the real board |
| `the firmware refused writing Setup unchanged (0x…); UEFI Variable Runtime Protection ... is enabled` | the board protects runtime variables | disable "Password protection of Runtime Variables" / BIOS Lock in firmware setup, boot Windows, run again |
| `this tool needs an administrator console holding SeSystemEnvironmentPrivilege` | not elevated | re-open PowerShell as Administrator |

Expected output, in order:

```text
ib-install: generated a new MOK in mok.der            # first run only
ib-install: signed ib-loader.efi, <n> bytes with its signature
ib-install: the loader flips the TPM UEFI spec version through the Setup variable
  byte offset of "TPM UEFI Spec Version" within Setup
  offset [default 0x1B]:                             <- press Enter for AMI
ib-install: the password MokManager will ask for is "1234"
ib-install: staged; the ESP stays mounted at X:\
  next boot: MokManager asks for the password and whether to enroll the key
  the boot after: the loader flips TCG UEFI spec version to TCG 1.2 and resets, if it was TCG 2.0
  the boot after that: the loader runs once, restores Windows, and boots it
```

If instead of `generated` it prints `using the MOK already in mok.der`, that is
fine; keep `mok.key` and `mok.der` — losing them means a new key and a new
enrollment.

What it wrote, so you can check it:

| ESP path | Content |
| --- | --- |
| `\ib-load.efi` | your hypervisor |
| `\ib-config.bin` | `IBCONFIG` v1, Setup offset (default 0x1B) |
| `\tcglog.ib` | the measured-boot log `ib-tcg-dump capture` wrote |
| `\ib-bootmgfw.efi` | backup of the real Windows boot manager |
| `EFI\Microsoft\Boot\bootmgfw.efi` | **shim** (replaces the boot manager) |
| `EFI\Microsoft\Boot\mmx64.efi` | MokManager |
| `EFI\Microsoft\Boot\grubx64.efi` | **your signed loader** |

Verify the substitution before rebooting:

```powershell
mountvol S: /S
"bootmgfw now : $((Get-FileHash 'S:\EFI\Microsoft\Boot\bootmgfw.efi' -Algorithm SHA256).Hash)"
"shim         : 351E131D3C4A636704E9CAE7D1296BAF8A05BECA9922CFEBBFA48933C9BF1AD3"
"backup exists: $(Test-Path 'S:\ib-bootmgfw.efi')"
mountvol S: /D
```

If `bootmgfw now` is not the shim hash, stop and re-run section 4. Do not reboot
until the backup exists. The installer leaves the ESP mounted on the letter it
picked; you can `mountvol X: /D` it or just reboot, the letter does not survive
a restart.

## 5. Boot 1: enrollment

`Restart-Computer`. Firmware -> shim. Because `MokNew`/`MokAuth` are set, shim
runs Fedora **MokManager**:

1. `Enroll MOK` -> `View key 0` (confirm `CN=insecure-boot MOK`) -> `Continue`
   -> `Yes` -> password `1234`.
2. `Reboot`.

This is the acceptance test the project never ran: shim must accept the
MokManager it was shipped beside. If you instead see `Security Violation`,
`Invalid MokManager` or the firmware skipping the entry, the vendor-cert pairing
is wrong — power off and go to section 8, the machine still has its boot manager
in `\ib-bootmgfw.efi`.

Photograph the screens; this is the evidence that matters most.

## 6. Boot 2: TCG flip (only if `Setup` byte is TCG 2.0)

Shim verifies the loader against the MOK you just enrolled and runs
`grubx64.efi`. Screen:

```text
experimental boot: Hello, World!
experimental boot: the Windows boot manager is back in place
experimental boot: wiping MOK/SBAT/SSP variables
experimental boot: Variables cleared
experimental boot: TCG UEFI spec version was TCG 2.0, now TCG 1.2
experimental boot: resetting, every staged file stays for the next boot
```

then a cold reset. The unit restarts into shim again by itself — no re-arm
needed, because shim is the boot manager now. Second time through the same lines
read `experimental boot: TCG UEFI spec version is already TCG 1.2` and the boot
continues.

Firmware that keeps no AMI `Setup` variable prints instead:

```text
experimental boot: no AMI Setup variable; this firmware keeps no TPM UEFI spec version to reconfigure
```

and continues without a reset. That is the fixed behaviour; without it this run
would abort before restoring the boot manager.

## 7. Boot 3: the hypervisor

Same serial expectations as section 6 of the unsigned runbook — the payload is
the same build:

```text
uefi hypervisor: DMA isolation admission: PASS
uefi hypervisor: processor topology: PASS
uefi hypervisor: VMX launch: PASS
uefi hypervisor: AP virtualization: PASS
```

`DMA isolation admission: FAIL 0xNN` and the VMX failure strings use the same
codes (Appendix C of the sibling runbook). On refusal, the loader has already
restored the boot manager, so Windows starts normally on the next reset.

Then, in Windows:

```powershell
& 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\.codex-build\vm-test-handoff\dist\ib-tcg-dump.exe' health
```

`UEFI_HYPERVISOR_WINDOWS_CPU:…:PASS` on every CPU plus
`UEFI_HYPERVISOR_WINDOWS_CPUID:PASS` means the hypervisor is resident with
Secure Boot still on. `ABSENT` means it refused; read the serial log.

## 8. Recovery

The boot-manager restore is the only thing standing between a failed run and a
machine that cannot boot Windows. Order of preference:

1. Boot the firmware menu and pick another entry (Windows Boot Manager backup,
   or a USB Windows installer). `\EFI\BOOT\BOOTX64.EFI` on the ESP may also be
   usable.
2. From WinRE (`Repair your computer` -> `Command Prompt`) or any live USB:

```text
diskpart
list volume
select volume <the 100-300 MB FAT32 ESP>
assign letter=S
exit
mountvol S: /S
copy S:\ib-bootmgfw.efi S:\EFI\Microsoft\Boot\bootmgfw.efi
dir  S:\EFI\Microsoft\Boot
```

   Confirm `bootmgfw.efi` is back to your pre-flight hash, then remove the
   leftovers:

```text
del S:\ib-load.efi S:\ib-config.bin S:\tcglog.ib
if exist S:\EFI\Microsoft\Boot\grubx64.efi del S:\EFI\Microsoft\Boot\grubx64.efi
if exist S:\EFI\Microsoft\Boot\mmx64.efi  del S:\EFI\Microsoft\Boot\mmx64.efi
```

3. If `\ib-bootmgfw.efi` is gone as well, restore
   `C:\hsinv-baremetal\esp-backup\EFI\Microsoft\Boot\bootmgfw.efi`.

A failed run also leaves `MokNew`/`MokAuth` behind. They are harmless; MokManager
will offer the enrollment again on the next shim boot, and `Enroll MOK` can be
declined (`Continue boot`).

## 9. Repeat and cleanup

After a successful run the loader has already restored `bootmgfw.efi` and
deleted `\ib-load.efi`, `\ib-config.bin`, `\tcglog.ib`, `\ib-bootmgfw.efi`,
`EFI\Microsoft\Boot\grubx64.efi` and `mmx64.efi`. Nothing to clean; verify:

```powershell
mountvol S: /S
(Get-FileHash 'S:\EFI\Microsoft\Boot\bootmgfw.efi' -Algorithm SHA256).Hash
mountvol S: /D
```

Compare with the pre-flight hash. To run again, repeat section 4 — `mok.der`
means the same key is reused, `MokNew` is rewritten, and MokManager asks again.

If the loader aborted after the restore but before the wipes, delete the
remaining `\ib-*` files and `EFI\Microsoft\Boot\{grubx64,mmx64}.efi` by hand;
they are inert without the boot-manager substitution.

## 10. Failure table

| Symptom | Cause | Action |
| --- | --- | --- |
| firmware goes straight to Windows after section 4 | Secure Boot off, or `MokNew` refused | check `Confirm-SecureBootUEFI`; re-run section 4 |
| MokManager does not appear | `MokNew` written under a GUID this shim does not read, or the MOK is already enrolled | expected GUID is `605dab50-e046-4300-…`, which both shipped shims carry; otherwise restore and report |
| `Security Violation` / `Invalid MokManager` | shim would not verify MokManager or the loader | restore `bootmgfw.efi` (section 8); pairing or signature problem |
| loader stops at `Setup holds 0xNN … neither TCG 1.2 nor TCG 2.0` | wrong `Setup` offset | this abort is **before** the boot-manager restore: restore by hand (section 8), re-run with the right offset |
| loader prints `missing artifact` | a wipe target absent | only reachable with hand-edited staging; restore and re-run section 4 |
| serial shows `DMA isolation admission: FAIL 0xNN` | platform refuses the IOMMU handover | expected experiment result; Windows still boots |
| Windows asks for a BitLocker recovery key | TPM spec version / PCR change | enter the key; the loader writes the old value back on its next start |

## 11. What this proves

If the probe reports PASS on every CPU: the firmware launched a
Microsoft-signed shim with Secure Boot enforcing, the shim accepted a
MokManager and a loader signed by a key you enrolled, the loader handed an
unsigned payload into VT-x on this installed machine, and Windows ran inside it
— without disabling Secure Boot and without a firmware configuration change.

It does not prove: that the enrollment survives (the loader deletes it), that
the shim/MokManager pairing is a supported vendor combination on other boards,
GPU/DMA isolation efficacy, or fitness for anything but a one-shot test.
