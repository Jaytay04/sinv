# Bare-Metal Boot Test Runbook

**Prerequisites (all confirmed):**
- `ntkrnlmp.pdb` at `.\ntkrnlmp.pdb` (26100.9711, verified)
- Signing key: `C:\Users\jayli\OneDrive\Documents\Projects\Hypervisor\hypersinv.pfx`
- Public cert: `.\components\hypervisor\hypersinv.cer` (CN=hypersinv, `59DBD678…`)
- VT-x ON, Secure Boot ON, Windows 26100.9711

---

## Step 0 — Preflight (elevated PowerShell, from repo root)

```powershell
cd C:\Users\jayli\OneDrive\Documents\Projects\hsinv

# Verify VT-x
Get-CimInstance Win32_Processor | Select-Object VirtualizationFirmwareEnabled   # expect True

# BCD recovery reference (owns the restore)
bcdedit /enum "{current}" | Out-File .\baremetal-bcd-before.txt
bcdedit /export "{current}" .\baremetal-bcd.xml

# Have your BitLocker recovery key reachable before proceeding.
```

## Step 1 — Fresh superbuild + component tests

```powershell
cmake --preset combined-windows --fresh `
  -DCOMBINED_KERNEL_IMAGE=C:\Windows\system32\ntoskrnl.exe `
  -DCOMBINED_KERNEL_PDB=C:\Users\jayli\OneDrive\Documents\Projects\hsinv\ntkrnlmp.pdb `
  -DCOMBINED_KERNEL_BUILD=26100 -DCOMBINED_KERNEL_UBR=9711 `
  -DCOMBINED_REQUIRE_KERNEL_PROFILE=ON `
  -DCOMBINED_BUILD_EFI_RUNTIME_COMPANION=ON `
  -DCOMBINED_BUILD_EFI_RUNTIME_SERVICE=ON

cmake --build --preset combined-windows --config Release

# Component tests
ctest --test-dir .\build\combined\windows-vs\components\hypervisor --build-config Release --output-on-failure
ctest --test-dir .\build\combined\windows-vs\components\uefi-loader --build-config Release --output-on-failure
```

**Expected artifacts after build:**

| Variable | Path |
|---|---|
| `$UL` | `.\build\combined\windows-vs\components\uefi-loader\Release` |
| `$HH` | `.\build\combined\windows-vs\components\hypervisor\src\efi\Release` |
| `$CD` | `.\build\combined\windows-vs\components\hypervisor\src\driver\efi_runtime_companion\Release` |
| `$EP` | `.\build\combined\windows-vs\components\esp_analysis\Release` |

Key files: `$UL\loader.efi`, `$UL\ib-install.exe`, `$UL\ib-tcg-dump.exe`, `$HH\uefi_hypervisor.efi`, `$CD\hvr1_efi_runtime_companion.sys`, `$CD\hvr1_runtime_service.exe`, `$EP\hypersinv_pub.exe`

Shim (vendored, not compiled): `.\components\uefi-loader\src\vendor\shimx64.efi`

## Step 2 — Sign chain + trust + boot flags

```powershell
$UL = .\build\combined\windows-vs\components\uefi-loader\Release
$HH = .\build\combined\windows-vs\components\hypervisor\src\efi\Release
$CD = .\build\combined\windows-vs\components\hypervisor\src\driver\efi_runtime_companion\Release
$EP = .\build\combined\windows-vs\components\esp_analysis\Release
$cer = .\components\hypervisor\hypersinv.cer

# Sign loader + payload
$env:HVKEY = "<pfx password>"
& $UL\ib-install.exe sign --signtool "C:\Program Files (x86)\Windows Kits\10\App Certification Kit\signtool.exe" `
  --pfx "C:\Users\jayli\OneDrive\Documents\Projects\Hypervisor\hypersinv.pfx" `
  --password-env HVKEY `
  --signer-cert $cer `
  --loader "$UL\loader.efi" `
  --payload "$HH\uefi_hypervisor.efi" `
  --out-dir .\stage

# Trust (Windows cert stores)
Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null

# BCD window (restored in Step 6)
bcdedit /set "{current}" testsigning on
bcdedit /set "{current}" hypervisorlaunchtype off
bcdedit /debug "{current}" off
bcdedit /bootdebug "{current}" off
```

## Step 3 — Stage ESP + arm one-shot BootNext → REBOOT

```powershell
& $UL\ib-install.exe stage `
  --signer-cert $cer `
  --shim ".\components\uefi-loader\src\vendor\shimx64.efi" `
  --loader "$UL\loader.efi" `
  --log .\divergence.ib `
  --payload "$HH\uefi_hypervisor.efi" `
  --esp-volume auto

& $UL\ib-install.exe oneshot --mode bootnext --esp-volume auto
```

**>>> REBOOT NOW <<<**

Chain: `shimx64.efi` → `loader.efi` → `uefi_hypervisor.efi` (all CPUs to root VMX) → `bootmgfw.efi` → normal Windows.

If the CN=hypersinv MOK is not yet enrolled, shim will offer MokManager. Enroll from disk: select `EFI\HypersinvShimInventory\MOK.cer`. Verify SHA-1 `59DBD6780CFE60C020B3F4511211FEC22C397B85` before confirming.

## Step 4 — Health check (once Windows returns)

```powershell
& $UL\ib-tcg-dump.exe probe
# Expect: one line per CPU  UEFI_HYPERVISOR_WINDOWS_CPU:<g>:<p>:PASS
#         plus aggregate     UEFI_HYPERVISOR_WINDOWS_CPUID:PASS

# Companion driver + service
sc.exe create hvr1companion type= file kernel binPath= "$CD\hvr1_efi_runtime_companion.sys" start= demand
sc.exe start hvr1companion
& "$CD\hvr1_runtime_service.exe" status
& "$CD\hvr1_runtime_service.exe" scan
```

## Step 5 — Arc Raiders (anti-cheat live)

```powershell
# Launch Arc Raiders, wait for anti-cheat to initialize, then:

# Service context (<=60 Hz publisher):
& "$CD\hvr1_runtime_service.exe" analysis-publish "Global\HypersinvOverlayDataV2" <HWND>

# User session (ESP reader):
& "$EP\hypersinv_pub.exe" --pid <GAME_PID> [--hwnd <HEX>] [--hz 60]

# Optional overlay:
& <tools\esp_overlay build>\hypersinv_overlay.exe --target-pid <GAME_PID> --mapping "Global\HypersinvOverlayDataV2"
```

## Step 6 — Teardown (reverts everything)

```powershell
& "$CD\hvr1_runtime_service.exe" shutdown
sc.exe stop hvr1companion; sc.exe delete hvr1companion

# Remove ESP staging
"$env:SystemRoot\System32\mountvol.exe" X: /S
Remove-Item -Recurse -Force X:\EFI\insecure-boot
"$env:SystemRoot\System32\mountvol.exe" X: /D

# Disarm one-shot
& $UL\ib-install.exe oneshot --mode off --esp-volume auto

# Restore BCD
bcdedit /set "{current}" testsigning off
bcdedit /set "{current}" hypervisorlaunchtype <original from baremetal-bcd-before.txt>

# Remove trust entries
$tp = (Get-PfxCertificate -FilePath "C:\Users\jayli\OneDrive\Documents\Projects\Hypervisor\hypersinv.pfx" -Password (ConvertTo-SecureString $env:HVKEY -AsPlainText -Force)).Thumbprint
certutil -delstore Root "$tp"
certutil -delstore TrustedPublisher "$tp"

# Verify clean state
bcdedit /enum "{current}" | Out-File .\baremetal-bcd-after.txt
# Diff baremetal-bcd-before.txt vs baremetal-bcd-after.txt → only the owned Boot#### entry should be gone
```

---

**Notes:**
- The armed payload is one-shot: crash → reboot → BootNext self-clears, BootOrder untouched.
- First physical run expectation: same all-CPU PASS + CPUID:PASS the VM gate proved.
- ESP will show placeholder boxes until the bone-transform wiring lands (CL-1426848 constants are in the tree).
- If WDK is not found during configure, `hvr1_efi_runtime_companion.sys` will be skipped — install WDK for Step 4's driver. The service exe builds without WDK.
