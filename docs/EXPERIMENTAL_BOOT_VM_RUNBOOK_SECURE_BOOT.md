# VM runbook: prove the Secure Boot chain before touching hardware

Purpose: run `EXPERIMENTAL_BOOT_BAREMETAL_RUNBOOK_SECURE_BOOT.md` end to end in
the disposable Windows VM, with Secure Boot **on**, so the first thing you learn
about the shim/MOK path is learned on a snapshot instead of on your machine.

Two passes, same chain, different payload:

| Pass | Payload | Proves |
| --- | --- | --- |
| A | `uefi_hypervisor_physical.efi` (the bare-metal build) | shim accepted, MOK enrolled, signed loader ran, the payload was entered, and the platform refused it at DMA admission as expected (VMware has no DMAR table) |
| B | `dist\uefi_hypervisor.efi` (VM self-test build) | the same chain up to a resident hypervisor: VMX, APs, guest interrupt guard, `ib-tcg-dump health` PASS |

A PASS on both is the green light for section 5 of the hardware runbook. A PASS
on A alone is still worth having: it means boot 1 (the enrollment, the part that
has never been run) works.

**Outcome of the first execution (2026-10-25, VMware Workstation, Windows 25H2
26200.9457).** Everything up to the first firmware load was verified, and then
the platform stopped the run:

| Step | Result |
| --- | --- |
| Secure Boot in the guest | `Confirm-SecureBootUEFI` = True |
| `ib-install` firmware probe | aborts with `0x000000cb` until a `Setup` variable exists - see section 0.5 |
| staging | succeeded: shim as `bootmgfw.efi`, `grubx64.efi` = signed loader, `ib-load.efi`, `ib-bootmgfw.efi`, `ib-config.bin`, `mmx64.efi`, `tcglog.ib`, all on the ESP |
| first boot | VMware refused the shim: `ERROR / Verification failed: (0x1C) Unsupported` |

`0x1C` is `EFI_UNSUPPORTED`, not `EFI_SECURITY_VIOLATION`: VMware's Secure Boot
`db` does not carry the Microsoft UEFI CA 2011 that signed the Fedora shim, so
this **firmware** cannot be used to test signature acceptance - for our chain or
any other distro shim. It is not evidence against the chain. Signature acceptance
therefore has to be proven on the hardware runbook (or in a VM whose firmware
ships the UEFI CA 2011, e.g. QEMU/OVMF with the Microsoft certs enrolled).

## 0. Machine and artifacts

VM: `.codex-build\gpu_windows_vm\run-01\standalone\hsinv-Windows-25H2.vmx`
(`firmware = "efi"`, `vhv.enable = "TRUE"`, `vtpm.present = "TRUE"`, Secure Boot
currently absent = off). Snapshot baseline `hsinv-default-25h2-26200.9457`.

The VM is partially encrypted, so `vmrun` needs `-vp $VmPass`; the VMware
console after unlocking needs nothing. Guest work below is written for the
console; `vmrun` is only used for snapshots and power. The password lives in
`.analysis\host-runner\credentials\windows-vm.xml` - section 1 shows how to read
it.

**Where each block runs.** Sections 0, 1 and 5: an **elevated PowerShell on the
host** - working directory does not matter, every path in them is absolute
(repo root is the natural place if only for the relative doc links). Sections 2,
3 and 4: **inside the VM**, at the guest console, elevated. Section 1 step 4
switches from host to guest by powering the VM on. Anything that talks to
`vmrun`, the `.vmx`, or `.codex-build` is host-side; anything that talks to the
ESP, `ib-install.exe` or `ib-tcg-dump.exe` is guest-side.

Artifacts on the host, same ones as the hardware runbook:

```powershell
$Src    = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\.codex-build'
$RefBin = Join-Path $Src 'insecure-boot-c\reference_binaries'
$Dist   = Join-Path $Src 'vm-test-handoff\dist'
$Hv     = 'C:\hsinv-baremetal\uefi_hypervisor_physical.efi'
$Stage  = 'C:\Users\jayli\AppData\Local\Temp\opencode\sb-stage'   # exposed to the guest as a shared folder
```

Build the staging folder on the host:

```powershell
New-Item -ItemType Directory -Force -Path $Stage | Out-Null
Copy-Item (Join-Path $RefBin 'ib-install.exe') $Stage -Force
Copy-Item (Join-Path $RefBin 'shimx64.efi')    $Stage -Force
Copy-Item (Join-Path $RefBin 'mmx64.efi')      $Stage -Force
Copy-Item (Join-Path $Dist   'loader.efi')     (Join-Path $Stage 'ib-loader.efi') -Force
Copy-Item (Join-Path $Dist   'uefi_hypervisor.efi') (Join-Path $Stage 'payload-vm.efi') -Force
Copy-Item (Join-Path $Dist   'ib-tcg-dump.exe') $Stage -Force
Copy-Item $Hv (Join-Path $Stage 'payload-physical.efi') -Force
& (Join-Path $RefBin 'ib-tcg-dump.exe') (Join-Path $Stage 'tcglog.ib')
Get-ChildItem $Stage | ForEach-Object {
    "$($_.Name)  $($_.Length)  $((Get-FileHash $_.FullName -Algorithm SHA256).Hash)"
}
```

`ib-install.exe` `F9AD423D...`, `shimx64.efi` `351E131D...`, `mmx64.efi`
`D383E37E...`, `ib-loader.efi` `3BEA91F8...`, `ib-tcg-dump.exe` `7D5CBCEC...` -
mismatch means stop.

## 0.5 The installer needs an AMI `Setup` variable that VMware does not have

`ib-install` starts by probing the firmware: it reads `Setup`
(`{EC87D643-EBA4-4BB5-A1E5-3F3E36B20DA9}`) and writes it back unchanged, to
prove the writes the staged boot will need are allowed. On VMware that variable
does not exist, and the probe reports only `ERROR_FILE_NOT_FOUND` as "absent",
so the run stops with:

```text
ib-install: reading the Setup firmware variable failed with 0x000000cb
```

`0xCB` is `ERROR_ENVVAR_NOT_FOUND`. On real AMI hardware `Setup` exists and this
step passes; in the VM, create a synthetic one before running the installer.
`64` bytes is enough for the offset the loader reads, and zeroing the whole
buffer then setting `0x1B` to `0x01` declares "already TCG 1.2", so the loader
skips the flip-and-reset boot (see section 3).

In the guest, elevated PowerShell:

```powershell
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Fw
{
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    public struct PrivOne { public uint Count; public long Luid; public uint Attr; }
    [DllImport("advapi32.dll", SetLastError = true)] static extern bool OpenProcessToken(IntPtr p, uint a, out IntPtr t);
    [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern bool LookupPrivilegeValueW(string s, string n, out long l);
    [DllImport("advapi32.dll", SetLastError = true)] static extern bool AdjustTokenPrivileges(IntPtr t, bool d, ref PrivOne s, uint l, IntPtr p, IntPtr r);
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)] public static extern bool SetFirmwareEnvironmentVariableExW(string n, string g, byte[] b, uint s, uint a);
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)] public static extern uint GetFirmwareEnvironmentVariableExW(string n, string g, byte[] b, uint s, out uint a);
    public static void Enable() {
        IntPtr t; OpenProcessToken(new IntPtr(-1), 0x28, out t);
        long l; LookupPrivilegeValueW(null, "SeSystemEnvironmentPrivilege", out l);
        PrivOne s = new PrivOne(); s.Count = 1; s.Luid = l; s.Attr = 2;
        AdjustTokenPrivileges(t, false, ref s, 0, IntPtr.Zero, IntPtr.Zero);
    }
}
'@
[Fw]::Enable()
$buf = New-Object byte[] 64; $buf[0x1B] = 0x01
[Fw]::SetFirmwareEnvironmentVariableExW('Setup', '{EC87D643-EBA4-4BB5-A1E5-3F3E36B20DA9}', $buf, 64, 0x7)
```

Expected: the call returns `True`, and reading it back gives
`size=64 attributes=0x7 byte1B=0x01`. That is what the first run confirmed.

## 1. Prepare the VM

1. Power the VM off (not suspend), then close its tab in Workstation. A VM left
   open in the GUI can rewrite the `.vmx` from memory and silently drop the keys
   added in step 3.
2. Snapshot now, so there is a restore point that predates every change:

```powershell
$Vmrun = 'C:\Program Files\VMware\VMware Workstation\vmrun.exe'
$Vmx   = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\.codex-build\gpu_windows_vm\run-01\standalone\hsinv-Windows-25H2.vmx'
& $Vmrun -vp $VmPass snapshot $Vmx 'pre-secureboot'
```

Where the VM password comes from: the vmx is partially encrypted, and the
password is the one stored in the archived credential
`.analysis\host-runner\credentials\windows-vm.xml` (a DPAPI-protected
`PSCredential`, same Windows user + machine). Read it with:

```powershell
$VmPass = (Import-Clixml 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv\.analysis\host-runner\credentials\windows-vm.xml').GetNetworkCredential().Password
```

That path is the one `Run-GpuWindows.ps1` passes as `CredentialStateRoot`, so it
is the current password, not a guess from an older archive. Use it only in the
`-vp` argument; never write it into the `.vmx` or into a log.

3. Enable Secure Boot and a serial capture file in the `.vmx`:

```powershell
$Log   = 'C:\Users\jayli\AppData\Local\Temp\opencode\sb-vm-serial.log'
Remove-Item -LiteralPath $Log -Force -ErrorAction SilentlyContinue
$Lines = [Collections.Generic.List[string]]::new([IO.File]::ReadAllLines($Vmx))
function Set-Key($key, $value) {
    $at = -1
    for ($i = 0; $i -lt $Lines.Count; $i++) { if ($Lines[$i] -match ('^' + [regex]::Escape($key) + '\s*=')) { $at = $i } }
    if ($at -ge 0) { $Lines[$at] = "$key = `"$value`"" } else { $Lines.Add("$key = `"$value`"") }
}
Set-Key 'uefi.secureBoot.enabled'      'TRUE'
Set-Key 'serial0.present'              'TRUE'
Set-Key 'serial0.fileType'             'file'
Set-Key 'serial0.fileName'             $Log
Set-Key 'serial0.startConnected'       'TRUE'
Set-Key 'isolation.tools.hgfs.disable' 'FALSE'
Set-Key 'sharedFolder.maxNum'          '1'
Set-Key 'sharedFolder0.present'        'TRUE'
Set-Key 'sharedFolder0.enabled'        'TRUE'
Set-Key 'sharedFolder0.hostPath'       $Stage
Set-Key 'sharedFolder0.guestName'      'sb-stage'
Set-Key 'sharedFolder0.readAccess'     'TRUE'
Set-Key 'sharedFolder0.writeAccess'    'FALSE'
[IO.File]::WriteAllLines($Vmx, $Lines, [Text.UTF8Encoding]::new($false))
Select-String -LiteralPath $Vmx -Pattern 'uefi.secureBoot|serial0|sharedFolder|hgfs'
```

The Secure Boot half is exactly what the harness does in `Set-CloneSecureBoot`,
so that shape is already proven against this VM. The shared-folder keys are
added because this VM ships with host/guest folders off; enabling them is the
only transfer route that needs no guest credentials. GUI equivalents: Settings
-> Options -> Advanced -> Secure Boot, and Options -> Shared Folders.

4. Power on. If Secure Boot does not engage, see section 6.

## 2. Confirm the guest state

Console, elevated PowerShell:

```powershell
Confirm-SecureBootUEFI                                   # True
(Get-CimInstance Win32_ComputerSystem).HypervisorPresent # False
Get-CimInstance Win32_DeviceGuard | Select-Object -ExpandProperty VirtualizationBasedSecurityStatus  # 0
manage-bde -status C: | Select-String 'Conversion Status|Protection Status'
```

If Secure Boot reads `False` or the cmdlet errors, stop: the `.vmx` edit did not
take effect and every result after this point would be about the wrong firmware.

Copy the staged files in over the shared folder. In the guest console
(elevated):

```powershell
$Guest = 'C:\hsinv-sb'
New-Item -ItemType Directory -Force -Path $Guest | Out-Null
$Share = '\\vmware-host\Shared Folders\sb-stage'
if (-not (Test-Path -LiteralPath $Share)) { throw "shared folder not mounted: $Share" }
Copy-Item (Join-Path $Share '*') $Guest -Force
Get-ChildItem $Guest | ForEach-Object {
    "$($_.Name)  $($_.Length)  $((Get-FileHash $_.FullName -Algorithm SHA256).Hash)"
}
Set-Location $Guest
```

Compare those hashes with section 0 before continuing. (`vmrun
copyFileFromHostToGuest` also works: use `-gu`/`-gp` from the same archived
credential file, whose user name is `UEFILoaderVm` - but that flow only ever
used that credential for `-vp`, so the guest account's actual password is not
proven. The shared folder needs no guest credentials at all.)

## 3. Pass A: the bare-metal payload through the chain

Still in `C:\hsinv-sb`, console attached:

```powershell
.\ib-install.exe .\payload-physical.efi
```

Expected:

```text
ib-install: generated a new MOK in mok.der
ib-install: signed ib-loader.efi, <n> bytes with its signature
ib-install: the loader flips the TPM UEFI spec version through the Setup variable
  offset [default 0x1B]:            <- press Enter; VMware has no Setup variable
ib-install: the password MokManager will ask for is "1234"
ib-install: staged; the ESP stays mounted at X:\
```

Then:

1. **Boot 1 - enrollment.** Reboot. Firmware -> shim -> Fedora MokManager.
   `Enroll MOK` -> `View key 0` (`CN=insecure-boot MOK`) -> `Continue` -> `Yes`
   -> password `1234` -> `Reboot`.
   *This step is the whole point of the VM pass.* If shim refuses MokManager you
   see `Security Violation` or `Invalid MokManager`; screenshot it, power off,
   revert to `pre-secureboot`, and stop - the hardware run would fail the same
   way.
2. **Boot 2 - loader.** Screen:

   ```text
   experimental boot: Hello, World!
   experimental boot: no AMI Setup variable; this firmware keeps no TPM UEFI spec version to reconfigure
   experimental boot: the Windows boot manager is back in place
   experimental boot: wiping MOK/SBAT/SSP variables
   experimental boot: Variables cleared
   experimental boot: entering the payload at <address>
   ```

   No cold reset: the TCG flip only happens when `Setup` exists.
3. **Payload.** Serial file `sb-vm-serial.log` should end with the DMA gate:

   ```text
   uefi hypervisor: DMA isolation admission: FAIL 0x6
   ```

   `0x6` is `DMAR_NOT_FOUND` - VMware exposes no IOMMU, so a refusal here is the
   correct result for the physical build. Any other code is worth recording: it
   means the payload got further (or further astray) than expected.

4. Windows boots normally afterwards. Confirm the chain cleaned up after itself:

```powershell
Set-Location C:\hsinv-sb
.\ib-tcg-dump.exe health | Tee-Object .\health-A.txt
mountvol S: /S
Get-ChildItem S:\ib-load.efi, S:\ib-config.bin, S:\ib-bootmgfw.efi, S:\tcglog.ib -ErrorAction SilentlyContinue
"bootmgfw: $((Get-FileHash 'S:\EFI\Microsoft\Boot\bootmgfw.efi' -Algorithm SHA256).Hash)"
Test-Path 'S:\EFI\Microsoft\Boot\grubx64.efi'
Test-Path 'S:\EFI\Microsoft\Boot\mmx64.efi'
mountvol S: /D
```

`health` -> all `ABSENT` is the expected Pass A result (the payload never
reached residency). No `\ib-*` files, no `grubx64.efi`, no `mmx64.efi`, and
`bootmgfw` back to the Windows image mean the loader's restore and wipe paths
all ran - which is what the hardware run depends on.

## 4. Pass B: the VM self-test payload through the same chain

The enrollment was consumed (the loader deleted it), so re-stage with the VM
payload - `mok.der` is reused, `MokNew` is rewritten:

```powershell
Set-Location C:\hsinv-sb
.\ib-install.exe .\payload-vm.efi
```

Boot 1 is MokManager again (password `1234`). Boot 2 is the loader with the same
lines plus `entering the payload`. Boot 3 is the self-test build: expect the
seven markers in the existing retest report
(`.codex-build\vm-test-handoff\.codex-build\retest-20261025T190334Z-ae2fa42e\report.txt`)
on the console, and on serial the VMX/AP lines.

Then in Windows:

```powershell
Set-Location C:\hsinv-sb
.\ib-tcg-dump.exe health | Tee-Object .\health-B.txt
```

`UEFI_HYPERVISOR_WINDOWS_CPU:<group>:<cpu>:PASS` on every CPU and
`UEFI_HYPERVISOR_WINDOWS_CPUID:PASS` = the hypervisor is resident **and** it
arrived through shim -> MOK -> signed loader. That is the combination the
hardware run needs.

If Pass B reports all `ABSENT`, compare its serial log with the retest run's
`efi-serial.log`: the payload path is known-good, so a difference points at
staging (wrong payload name, stale `ib-config.bin`) rather than at the
hypervisor.

## 5. Teardown

1. Power off the VM.
2. Revert to `pre-secureboot` (restores the ESP, the NVRAM, the vTPM state, and
   removes the Secure Boot setting):

```powershell
& $Vmrun -vp $VmPass revertToSnapshot $Vmx 'pre-secureboot'
```

3. If you kept the VM instead of reverting, remove everything section 1 added -
   Secure Boot, serial capture, and the shared folder (`Restore-CloneSecureBoot`
   in the harness is the reference implementation for the Secure Boot half):

```powershell
$Lines = [Collections.Generic.List[string]]::new([IO.File]::ReadAllLines($Vmx))
$Lines.RemoveAll({ param($l)
    $l -match '^uefi\.secureBoot\.enabled\s*=' -or
    $l -match '^serial0\.' -or
    $l -match '^isolation\.tools\.hgfs\.disable\s*=' }) | Out-Null
foreach ($Key in 'sharedFolder0.present', 'sharedFolder0.enabled') {
    $Index = $Lines.FindIndex({ param($l) $l -match ('^' + [regex]::Escape($Key) + '\s*=') })
    if ($Index -ge 0) { $Lines[$Index] = "$Key = `"FALSE`"" }
}
[IO.File]::WriteAllLines($Vmx, $Lines, [Text.UTF8Encoding]::new($false))
Select-String -LiteralPath $Vmx -Pattern 'secureBoot|serial0|sharedFolder|hgfs'
```

Deleting the `serial0.*` lines restores the default (no serial port); VMware
re-adds `sharedFolder.maxNum` on its own.

Keep `health-A.txt`, `health-B.txt`, the serial logs and the screenshots with the
run; they are the evidence that the hardware run has a validated chain behind it.

## 6. When it does not cooperate

| Symptom | Cause | Action |
| --- | --- | --- |
| `Confirm-SecureBootUEFI` false or erroring after section 1 | VMware did not provision Secure Boot keys into the NVRAM | power off, snapshot, delete `*.nvram` beside the `.vmx`, power on; VMware re-creates it with its default key set. If Windows then has no boot entry, boot WinRE and `bcdboot C:\Windows /s S: /f UEFI` |
| firmware message about a signature violation before any guest screen | VMware's `db` does not carry the Microsoft UEFI CA 2011 that signed our shim | expected on this firmware: the first run stopped at `Verification failed: (0x1C) Unsupported`. Nothing to fix in our chain; prove signature acceptance on hardware, or in a QEMU/OVMF VM with the Microsoft certs in `db` |
| `ib-install` stops at `reading the Setup firmware variable failed with 0x000000cb` | no AMI `Setup` variable (VMware) | run the synthetic-variable step in section 0.5 first. On AMI hardware this means the variable is protected: turn off the firmware's variable/BiOS write protection |
| `captureScreen` says "Anonymous guest operations are not allowed" | this VM requires guest credentials for capture | add `-gu UEFILoaderVm -gp $VmPass`; capture also cannot work while the guest OS is not running (firmware error screens), so read those off the console window instead |
| `Security Violation` / `Invalid MokManager` after shim starts | shim would not verify `mmx64.efi` or the loader | the vendor pairing or the signature is wrong; this is the failure the VM pass exists to catch |
| MokManager never appears, Windows boots straight through | `MokNew` not written (privilege), or the MOK is already enrolled, or Secure Boot is off | check the installer output and `Confirm-SecureBootUEFI` |
| `vmrun` reports the VM is encrypted | the VM is partially encrypted | add `-vp $VmPass` to every `vmrun` call, or use the console |
| serial log stays empty | `serial0.present` not set, or the guest never wrote to COM1 | the guest only speaks on COM1 from the hypervisor up; the loader writes to the screen, so an empty log with a refused payload means the payload never started |
| Pass A reaches `DMA isolation admission: PASS` | unexpected: VMware exposed an IOMMU | record it; a PASS is not a failure, but it changes what Pass A proves |

## 7. What each outcome licenses

- Pass A green (enrollment, loader, payload entry, clean restore): the hardware
  runbook's boot 1 will work, and a refusal on hardware is the machine's answer,
  not the chain's.
- Pass B green as well: VMX, AP bring-up and the health contract survive the
  signed path, so a hardware PASS becomes a statement about your board, not
  about the tooling.
- Pass A red: do not touch the hardware. The failure is in signing, pairing, or
  staging, and every one of those is cheaper to fix here.
