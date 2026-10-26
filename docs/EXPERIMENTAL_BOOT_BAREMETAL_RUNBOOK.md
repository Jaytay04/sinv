# Bare-metal runbook: experimental boot -> hypervisor -> Windows

Status: experimental probe, not a product path. The boot chain below is proven
only in a VMware VM (4 and 20 vCPU, boot-manager hash unchanged). This runbook
takes the same chain onto one physical Intel machine and records what happens.
Expect the hypervisor to refuse; a refusal is a result, not a failure of the
procedure.

Read section 0 before touching the machine.

## 0. Hard rules

- Use a machine you can reinstall, or one you have a full disk image of.
- BitLocker must be off or suspended; keep the recovery key in hand.
- Secure Boot must be **off**. The EFI images carry no certificate table. If you
  want the signed shim + MOK path that leaves Secure Boot on, use
  `EXPERIMENTAL_BOOT_BAREMETAL_RUNBOOK_SECURE_BOOT.md` instead: different
  artifacts, different staging, different recovery.
- Never use `ib-install oneshot --profile vm-direct`. The tool itself refuses it
  for physical deployment; the supported profile here is `legacy`.
- The loader deletes `EFI\Microsoft\Boot\mmx64.efi` and
  `EFI\Microsoft\Boot\grubx64.efi`, wipes `MOK`, `SBAT` and `SSP` UEFI
  variables, and rewrites the AMI `Setup` byte when it holds TCG 2.0.
- A failed run ends with the firmware booting Windows normally. There is no
  boot loop: `BootNext` is consumed once and `BootOrder` is never changed.
- Keep a Windows install USB and a firmware-settings photo of the machine's
  current boot configuration.

## 1. What you need

| Item | Why |
| --- | --- |
| Intel CPU with VT-x and EPT | the payload runs `VMXON`/`VMLAUNCH` |
| VT-d / IOMMU enabled in firmware | the payload stops at DMA admission without it |
| COM1 header + USB-TTL adapter + second PC | the hypervisor prints to `0x3F8` only (115200 8N1); without it you cannot see why a run stopped |
| Windows 11 x64, admin PowerShell | staging and the post-boot probe |
| The `vm-test-handoff` bundle | loader, installer, probe, hypervisor source |
| 4-8 GB USB stick | ESP backup |

Serial is optional for the pass/fail verdict (the Windows probe gives that) but
mandatory for diagnosing a refusal. Use PuTTY at 115200 8N1, log to file.

## 2. Pre-flight (record everything)

Elevated 64-bit PowerShell. Save the output to a file and keep it with the run.

```powershell
$Run = 'C:\hsinv-baremetal'
New-Item -ItemType Directory -Force -Path $Run | Out-Null
Start-Transcript -Path (Join-Path $Run 'preflight.txt')

# Firmware and CPU
Get-CimInstance Win32_BIOS | Select-Object Manufacturer, SMBIOSBIOSVersion, Version
Get-CimInstance Win32_Processor |
    Select-Object Name, NumberOfCores, NumberOfLogicalProcessors,
                  VMMonitorModeExtensions, SecondLevelAddressTranslationExtensions,
                  VirtualizationFirmwareEnabled
Get-CimInstance Win32_ComputerSystem | Select-Object Manufacturer, Model

# Gates that must be cleared before staging
Confirm-SecureBootUEFI                       # must be False; if True, disable Secure Boot and reboot
manage-bde -status C:                        # must be Fully Decrypted, or Protection Off/Suspended
Get-CimInstance -ClassName Win32_DeviceGuard | Select-Object -ExpandProperty VirtualizationBasedSecurityStatus
bcdedit /enum {current} | Select-String hypervisorlaunchtype
Get-WindowsOptionalFeature -Online -FeatureName Microsoft-Hyper-V-All |
    Select-Object FeatureName, State
Stop-Transcript
```

Required state:

- `Confirm-SecureBootUEFI` -> `False` (or the cmdlet errors because Secure Boot is off).
- `manage-bde` -> `Protection Off` or `Suspended`. If `Fully Encrypted`, run
  `Suspend-BitLocker -MountPoint C: -RebootCount 2` and keep the recovery key.
- `VirtualizationBasedSecurityStatus` -> `0` (not enabled). If `2` (running),
  turn off Memory Integrity / Core Isolation, Windows Sandbox, WSL2 and Hyper-V,
  then `bcdedit /set hypervisorlaunchtype off` and reboot.
- `VirtualizationFirmwareEnabled` -> `True`. If `False`, enable VT-x in firmware.

Firmware settings to confirm by hand (and photograph):

1. Intel VT-x / VMX: enabled.
2. Intel VT-d (or "VT for Directed I/O"): enabled. Where a board offers
   "VT-d only for ACPI" or "DMA remapping from BIOS", prefer the plain enabled
   state; the payload reports `DMA isolation admission: FAIL 0xE`/`0xF` when the
   firmware already owns the remapping.
3. Secure Boot: disabled.
4. TPM: 2.0 present and enabled (not required for a pass, but it is the path the
   loader prefers).
5. Boot from the internal disk first; leave the boot menu reachable (F8/F11/F12).

Back up the boot volume and the ESP before staging:

```powershell
mountvol S: /S
$BootMgr = 'S:\EFI\Microsoft\Boot\bootmgfw.efi'
"bootmgfw.efi  $((Get-FileHash $BootMgr -Algorithm SHA256).Hash)" |
    Tee-Object -Append (Join-Path 'C:\hsinv-baremetal' 'preflight.txt')
robocopy S:\EFI (Join-Path 'C:\hsinv-baremetal' 'esp-backup\EFI') /E /R:1 /W:1
Get-ChildItem S:\ | Select-Object Name, Length |
    Out-File (Join-Path 'C:\hsinv-baremetal' 'esp-root.txt')
mountvol S: /D
bcdedit /enum firmware | Out-File (Join-Path 'C:\hsinv-baremetal' 'firmware-bcd.txt')
```

## 3. Artifacts

### 3.1 Loader side: use the tested bytes

From the bundle root, verify the pinned artifacts before use:

```powershell
$Bundle = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\.codex-build\vm-test-handoff'
$Dist   = Join-Path $Bundle 'dist'
Get-Content (Join-Path $Dist 'ARTIFACTS.sha256')
Get-ChildItem $Dist | Where-Object Extension -in '.efi','.exe' |
    ForEach-Object { "$($_.Name)  $((Get-FileHash $_.FullName -Algorithm SHA256).Hash)" }
```

The images those tests used:

```text
loader.efi        859E19DDEE14C0C28A556DD36CEE222D840EDE72A6CC8BF7EC109A33D6445BD4
ib-install.exe    32891889F3288962B17CE9EAB8B17C43B9080E18CDE64E8D305ECEDB567A28C2
ib-tcg-dump.exe   7D5CBCEC44332A0FAF51C738D82B54C7398BF2E247E2F3B981758170D850925E
```

If the loader you intend to run was built with the serial-console patch
(`patches/ib_console.c`), its hash differs; still record it. Do not mix a
patched loader with a report that claims the hash above.

### 3.2 Hypervisor side: build a physical image

Do **not** use `dist\uefi_hypervisor.efi` on hardware. It is the VM self-test
image and refuses to start unless CPUID leaf `0x40000000` reports `VMwareVMware`
(`components/hypervisor/src/efi/efi_hypervisor.c:4500-4515`, compiled under
`HYPERSINV_GPU_VM_SELFTEST`). On a physical machine it returns `EFI_UNSUPPORTED`
before `VMXON` and you learn nothing.

Build without that option (it defaults to `OFF`; it is passed explicitly so the
intent is on the record):

```powershell
$vc = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231'
$k  = 'C:/Program Files (x86)/Windows Kits/10'
$s  = '10.0.28000.0'
$env:PATH    = "$vc/bin/Hostx64/x64;$k/bin/$s/x64;$k/bin/$s;" + $env:PATH
$env:INCLUDE = "$vc/include;$k/Include/$s/ucrt;$k/Include/$s/um;$k/Include/$s/km;$k/Include/$s/shared;$k/Include/$s/shared/inc"
$env:LIB = $env:LIBPATH = "$vc/lib/x64;$k/Lib/$s/um/x64;$k/Lib/$s/ucrt/x64;$k/Lib/$s/km/x64"

$Bundle = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\.codex-build\vm-test-handoff'
$HvBuild = Join-Path $env:TEMP 'hsinv-hv-physical'      # outside the bundle: object-path length

cmake -S (Join-Path $Bundle 'components/hypervisor') -B $HvBuild -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_MT="$k/bin/$s/x64/mt.exe" `
  "-DHYPERSINV_UEFI_SOURCE_DIR=$Bundle/components/uefi-loader" `
  "-DHYPERSINV_UEFI_SUPPORT_DIR=$Bundle/components/uefi-loader/src/vendor" `
  -DHYPERSINV_BUILD_UEFI=ON -DHYPERSINV_BUILD_CORE=OFF `
  -DHYPERSINV_BUILD_DRIVER=OFF -DHYPERSINV_BUILD_LOADER=OFF `
  -DHYPERSINV_TEST_EFI_RUNTIME_COMPANION=OFF `
  -DHYPERSINV_GPU_VM_SELFTEST=OFF
cmake --build $HvBuild --target uefi_hypervisor

$Payload = Join-Path $HvBuild 'src\efi\uefi_hypervisor.efi'
Copy-Item $Payload (Join-Path 'C:\hsinv-baremetal' 'uefi_hypervisor_physical.efi') -Force
"payload  $((Get-FileHash $Payload -Algorithm SHA256).Hash)" |
    Tee-Object -Append (Join-Path 'C:\hsinv-baremetal' 'preflight.txt')
```

Sanity check the image you just built (no VMware gate, VMX diagnostics present):

```powershell
$Bytes = [IO.File]::ReadAllBytes($Payload)
$Text  = [Text.Encoding]::ASCII.GetString($Bytes)
"vmware-gate : $($Text.Contains('VMWARE_TEST_ONLY'))"       # False
"vmx-diag    : $($Text.Contains('BSP virtualization failure'))"  # True
```

## 4. Stage the ESP

The loader reads from the root of the volume it was loaded from, not from
`EFI\hsinv-boot`. All of these are required; a missing wipe target is a hard
loader error (`IB_LOADER_ERR_MISSING_ARTIFACT`).

```powershell
$Bundle  = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\.codex-build\vm-test-handoff'
$Dist    = Join-Path $Bundle 'dist'
$Payload = 'C:\hsinv-baremetal\uefi_hypervisor_physical.efi'
$Run     = 'C:\hsinv-baremetal'
$Esp     = 'S:'

mountvol $Esp /S

$BootMgr = Join-Path $Esp 'EFI\Microsoft\Boot\bootmgfw.efi'
$BootMgrBefore = (Get-FileHash -LiteralPath $BootMgr -Algorithm SHA256).Hash

# Residue from an earlier attempt must not exist.
foreach ($p in @((Join-Path $Esp 'ib-load.efi'), (Join-Path $Esp 'ib-config.bin'),
                 (Join-Path $Esp 'ib-bootmgfw.efi'), (Join-Path $Esp 'EFI\hsinv-boot'))) {
    if (Test-Path -LiteralPath $p) { throw "Already staged: $p" }
}

$Stage = Join-Path $Esp 'EFI\hsinv-boot'
New-Item -ItemType Directory -Path $Stage | Out-Null
Copy-Item (Join-Path $Dist 'loader.efi') (Join-Path $Stage 'shimx64.efi')
Copy-Item (Join-Path $Dist 'loader.efi') (Join-Path $Stage 'grubx64.efi')
Copy-Item $Payload                      (Join-Path $Stage 'payload.efi')


# The loader parses this as an IBTCGLOG dump and aborts the run if it is not
# one, so it has to be a real capture from this machine.
& (Join-Path $Src 'insecure-boot-c\reference_binaries\ib-tcg-dump.exe') (Join-Path $Stage 'tcglog.ib')

$Lines = @('schema=1')
foreach ($Name in @('shimx64.efi', 'grubx64.efi')) {
    $Path   = Join-Path $Stage $Name
    $Item   = Get-Item -LiteralPath $Path
    $Digest = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash   # uppercase on purpose
    $Lines += "$Name size=$($Item.Length) sha256=$Digest"
}
[IO.File]::WriteAllText((Join-Path $Stage 'manifest.sha256'),
    (($Lines -join "`r`n") + "`r`n"), (New-Object Text.UTF8Encoding($false)))

# Root artifacts. ib-config.bin = IBCONFIG, version 1, Setup offset 27 (0x1B).
[IO.File]::WriteAllBytes((Join-Path $Esp 'ib-config.bin'),
    [byte[]](0x49,0x42,0x43,0x4F,0x4E,0x46,0x49,0x47,0x01,0x00,0x00,0x00,0x1B,0x00))
Copy-Item $Payload  (Join-Path $Esp 'ib-load.efi')
Copy-Item $BootMgr  (Join-Path $Esp 'ib-bootmgfw.efi')

# The loader zeroes and deletes these two; they must exist first. Back up real
# ones (dual-boot shim installs) and restore them in section 8.
foreach ($Name in @('mmx64.efi', 'grubx64.efi')) {
    $Path = Join-Path $Esp "EFI\Microsoft\Boot\$Name"
    if (Test-Path -LiteralPath $Path) {
        Copy-Item -LiteralPath $Path -Destination (Join-Path $Run "backup-$Name")
    } else {
        [IO.File]::WriteAllBytes($Path, [byte[]](0x49,0x53,0x20,0x57,0x49,0x50,0x45,0x0D,0x0A))
    }
}

# Verify before arming.
Get-ChildItem -Recurse -File $Stage | Select-Object FullName, Length
"bootmgfw before  $BootMgrBefore"
"ib-bootmgfw      $((Get-FileHash (Join-Path $Esp 'ib-bootmgfw.efi') -Algorithm SHA256).Hash)"
mountvol $Esp /D
```

Note the loader restores `bootmgfw.efi` from `\ib-bootmgfw.efi` after checking
the copy, so stage and boot in one sitting. A Windows update that replaces
`bootmgfw.efi` between staging and boot makes the staged copy stale.

## 5. Arm the one-shot boot

```powershell
$Dist = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\.codex-build\vm-test-handoff\dist'
& (Join-Path $Dist 'ib-install.exe') oneshot --mode bootnext --profile legacy --esp-volume auto
```

Expected: `BootNext set to owned Boot####; BootOrder was not changed.` Record
the `Boot####` number. Exit code must be 0; any `BootNext preflight failed:` or
`one-shot failed:` line means do not reboot, fix the staged layout first.

`--mode bootnext` validates the staged layout before writing: both
`EFI\hsinv-boot\shimx64.efi` and `EFI\hsinv-boot\grubx64.efi` must match their
`manifest.sha256` entries in size and SHA-256 (uppercase hex). The command needs
no `--payload`; the loader reads `\ib-load.efi` itself.

The new firmware entry is described as `hsinv boot Toolkit` and points at
`\EFI\hsinv-boot\shimx64.efi`.

## 6. Boot and watch

1. Start the serial capture (115200 8N1) and keep the speaker/monitor visible.
2. `Restart-Computer` (or reboot from the firmware menu).

Expected sequence, screen (loader, ConOut; exact order is what the tested
`dist\loader.efi` prints):

```text
experimental boot: Hello, World!
experimental boot: the Windows boot manager is back in place
experimental boot: wiping MOK/SBAT/SSP variables
experimental boot: Variables cleared
experimental boot: TPM on <start method> ...
experimental boot: entering the payload at <address>
```

On a firmware that keeps no AMI `Setup` variable (VMware, some others) the
second line is instead:

```text
experimental boot: no AMI Setup variable; this firmware keeps no TPM UEFI spec version to reconfigure
```

and there is no `AMI Setup:` line.

Then, serial (hypervisor, `0x3F8`):

```text
uefi hypervisor: DMA isolation admission: PASS
uefi hypervisor: processor topology: PASS
uefi hypervisor: AP VMX preflight: PASS
uefi hypervisor: BSP virtualization: PASS
uefi hypervisor: processor state: PASS
uefi hypervisor: processor resources: PASS
uefi hypervisor: VMX launch: PASS
uefi hypervisor: AP virtualization: PASS
uefi hypervisor: activated AP count: 0x...
UEFI_HYPERVISOR_GUEST_INTERRUPT_GUARD:PASS
```

then Windows starts under the hypervisor. Any of the following instead means
the payload returned and the firmware booted Windows without it:

| Serial line | Meaning | Next step |
| --- | --- | --- |
| `DMA isolation admission: FAIL 0x6` | no DMAR table found | enable VT-d and retry |
| `DMA isolation admission: FAIL 0xE` | firmware/other owner holds the IOMMU | look for "VT-d" / "DMA remapping" ownership options |
| `DMA isolation admission: FAIL 0xF` | remapping already enabled when the payload started | same as above; record the code |
| `DMA isolation admission: FAIL` other | see Appendix C | record the code, stop |
| `BSP virtualization failure: VMXON` | VT-x not available to the payload | check VT-x, Hyper-V/VBS off, no nested VM |
| `BSP virtualization failure: VMCS ...` | VMCS setup rejected on this CPU | record, stop |
| `AP VMX preflight: FAIL` / `AP virtualization: FAIL` | a secondary CPU refused VMX | record which AP and stop |
| `uefi hypervisor: FATAL INTERRUPT` | guest faulted while entering | record, stop |

### 6.1 The AMI cold reset

On AMI firmware whose `Setup` byte at offset 27 reads TCG 2.0, the loader writes
TCG 1.2, prints

```text
experimental boot: TCG UEFI spec version was TCG 2.0, now TCG 1.2
experimental boot: resetting, every staged file stays for the next boot
```

and cold-resets. If the byte already reads TCG 1.2, the loader prints
`experimental boot: TCG UEFI spec version is already TCG 1.2` and boots straight
through; no re-arm is needed. `BootNext` was consumed by the firmware, so the
reboot lands in Windows. Nothing on the ESP was modified yet. Re-arm and boot
once more:

```powershell
& (Join-Path $Dist 'ib-install.exe') oneshot --mode bootnext --profile legacy --esp-volume auto
Restart-Computer
```

The second boot sees TCG 1.2 and continues. After the run the loader writes the
old value back.

If the loader instead prints `Setup holds 0xNN at the configured offset, neither
TCG 1.2 nor TCG 2.0`, offset 27 is wrong for this board. That abort happens
before any write; see Appendix B.

## 7. Verify in Windows

The definitive signal is CPUID leaf `TCGDUMP_HYPERVISOR_HEALTH_LEAF` from every
logical processor:

```powershell
$Dist = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\.codex-build\vm-test-handoff\dist'
& (Join-Path $Dist 'ib-tcg-dump.exe') health | Tee-Object 'C:\hsinv-baremetal\health.txt'
```

- `UEFI_HYPERVISOR_WINDOWS_CPU:g:p:PASS` for every CPU plus
  `UEFI_HYPERVISOR_WINDOWS_CPUID:PASS` -> the hypervisor is resident and healthy;
  record the CPU count and compare it with `NumberOfLogicalProcessors`.
- `...:ABSENT` on all CPUs -> no hypervisor: the payload refused or never ran
  (read the serial log).
- `...:FAIL` on some CPUs -> the hypervisor is resident but its contract is
  wrong on those processors; this is the interesting hybrid-topology case
  (record group, CPU number, and how many).

Also record:

```powershell
manage-bde -status C:
bcdedit /enum firmware | Select-String 'hsinv','BootNext'
Get-CimInstance Win32_Processor | Select-Object NumberOfLogicalProcessors
```

## 8. Cleanup and recovery

Do this on every run, pass or fail, before re-enabling BitLocker or Secure Boot.

```powershell
$Dist = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\.codex-build\vm-test-handoff\dist'

# 1. Remove the owned BootNext / BootXXXX entry (BootOrder untouched).
#    Expect: Owned BootNext/Boot#### state cleared; BootOrder was not changed.
& (Join-Path $Dist 'ib-install.exe') oneshot --mode off --profile legacy --esp-volume auto

# 2. Remove anything the loader did not get to delete.
mountvol S: /S
Remove-Item -Recurse -Force 'S:\EFI\hsinv-boot' -ErrorAction SilentlyContinue
Remove-Item -Force 'S:\ib-load.efi','S:\ib-config.bin','S:\ib-bootmgfw.efi' -ErrorAction SilentlyContinue

# 3. Restore mmx64/grubx64 only if the pre-flight backup exists.
foreach ($Name in @('mmx64.efi','grubx64.efi')) {
    $Backup = "C:\hsinv-baremetal\backup-$Name"
    if (Test-Path $Backup) { Copy-Item $Backup "S:\EFI\Microsoft\Boot\$Name" -Force }
}
mountvol S: /D

# 4. Boot manager must be the one you started with.
mountvol S: /S
(Get-FileHash 'S:\EFI\Microsoft\Boot\bootmgfw.efi' -Algorithm SHA256).Hash
mountvol S: /D
```

Two residue cases the flow above does not catch by itself:

- The TCG spec-version byte is written back to TCG 2.0 by the loader's start-up
  residue pass, and that pass only runs if the loader runs again. A run in which
  the hypervisor kept control ends with the firmware still reporting TCG 1.2 and
  `mmx64.efi`/`grubx64.efi` already removed. To restore, arm once more
  (`--mode bootnext`), boot, let the loader write the byte back and attempt the
  payload, then clean up here. Do this before re-enabling BitLocker: PCR values
  measured under TCG 1.2 differ from TCG 2.0 and BitLocker will ask for the
  recovery key.
- If you ever ran `--mode replace`, `--mode verify --profile legacy
  --esp-volume auto` performs the restore-hash plus owned-residue check and
  prints `Post-boot restore hash and owned-residue cleanup passed.` A
  hand-staged run has no replace transaction, so skip it.

Compare the last hash with `bootmgfw before` from section 4. If they differ,
restore `C:\hsinv-baremetal\esp-backup\EFI\Microsoft\Boot\bootmgfw.efi`.

If the machine refuses to boot Windows at all:

1. Power off, then on, and open the firmware boot menu.
2. Delete the `hsinv boot Toolkit` entry (or run step 1 above from WinRE).
3. Restore `EFI\Microsoft\Boot\bootmgfw.efi` and any `backup-*` files from the
   ESP backup.
4. Boot the Windows install USB and use Startup Repair if the entry is missing.

A dead hypervisor does not damage `bootmgfw.efi`: the loader restores it before
wiping anything, so the worst case is a stale ESP stage plus a BootXXXX entry.

## 9. What this run proves and does not prove

Proves, if the probe passes on every CPU: the firmware dispatched an unsigned
image from the ESP, the loader reconfigured/observed the firmware TPM setting,
handed a resident PE payload into the hypervisor, the hypervisor took VT-x on
this concrete CPU, and the guest Windows survived the handoff on this board.

Does not prove: GPU/DMA isolation efficacy (the physical GPU path is a different
payload profile), TPM measurement/replay fidelity, Secure Boot compatibility,
stability under load, or that the hypervisor is safe to leave installed. The
loader is a one-shot test chain with no uninstaller beyond section 8.

## Appendix A: optional loader serial mirror

The loader prints to ConOut only. To get its lines on the same serial capture,
build it from `patches\ib_console.c` staged over
`components/uefi-loader/src/hsinv_boot/loader/ib_console.c`, then use that
`loader.efi` everywhere above and record its distinct hash. The patch compiles
the mirror in and compiles to nothing without its define, so it cannot be
enabled by accident.

## Appendix B: changing the Setup offset

`ib-config.bin` is 14 bytes: `IBCONFIG`, version 1 (u32 LE at 0x08), then the
`Setup` offset (u16 LE at 0x0C). Byte 12-13 = `0x1B 0x00` (27) in section 4.

To find the right offset on a board where 27 is wrong, dump the variable from a
Linux USB (efivarfs is unmounted for writes but readable):

```bash
ls /sys/firmware/efi/efivars/ | grep -i '^Setup-ec87d643-eba4-4bb5-a1e5-3f3e36b20da9'
xxd /sys/firmware/efi/efivars/Setup-ec87d643-eba4-4bb5-a1e5-3f3e36b20da9 | head -40
```

The byte must read `01` (TCG 1.2) or `02` (TCG 2.0); compare it with the
firmware's TPM spec-version setting while toggling that setting. Then rewrite
bytes 12-13 (`[BitConverter]::GetBytes([uint16]$Offset)`) and restage. A wrong
offset aborts before any write, so iteration is safe but slow.

## Appendix C: DMA admission codes

`DMA isolation admission: FAIL 0xNN`, from
`components/hypervisor/src/efi/iommu_runtime.h`:

| Code | Name | Usual cause |
| --- | --- | --- |
| 0x1 | INVALID_ARGUMENT | payload bug; record |
| 0x2 | OUT_OF_BOUNDS | DMAR table malformed |
| 0x3 | BAD_SIGNATURE | DMAR signature mismatch |
| 0x4 | BAD_CHECKSUM | DMAR checksum mismatch |
| 0x5 | MALFORMED_TABLE | DMAR structure unsupported |
| 0x6 | DMAR_NOT_FOUND | VT-d disabled |
| 0x7 | UNSUPPORTED_SCOPE | DMAR scope outside the payload's model |
| 0x8 | RANGE_OVERFLOW | address range too large |
| 0x9 | RANGE_OVERLAP | overlapping DRHD ranges |
| 0xA | CAPACITY | too many units/devices |
| 0xB | REMAPPING_UNSUPPORTED | hardware lacks the required remapping |
| 0xC | QI_INCOMPATIBLE | invalidation queue not usable |
| 0xD | IR_INCOMPATIBLE | interrupt remapping not usable |
| 0xE | OWNERSHIP_UNAVAILABLE | another owner holds the IOMMU |
| 0xF | ALREADY_ENABLED | remapping already on at entry |
| 0x10 | CALLBACK_FAILED | firmware callback failed |
| 0x11 | TIMEOUT | invalidation timeout |
| 0x12 | AUDIT_FAILED | PCI/DMA audit refused the handover |
| 0x13 | ROLLBACK_FAILED | could not restore prior state |
| 0x14 | INVALID_STATE | internal |
| 0x15 | PROFILE_MISMATCH | CPU profile not in the model |
| 0x16 | PHYSICAL_MEDIATION_REQUIRED | model requires a mediated path |
| 0x17 | CPU_RENDEZVOUS_INCOMPLETE | an AP did not answer |
| 0x18 | DECODE_UNSUPPORTED | instruction/state decode gap |
| 0x19 | PAGE_WALK_FAILED | page-table walk failed |
| 0x1A | TAKEOVER_BLOCKED | firmware refuses handover |
