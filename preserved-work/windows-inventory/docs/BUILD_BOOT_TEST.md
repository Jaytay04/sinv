# UEFILoader + EFI Hypervisor - BARE-METAL (primary host) build / boot / test

> Collector-only physical-host testing now uses [EFI_INVENTORY_PATH_B.md](EFI_INVENTORY_PATH_B.md). The commands below describe the separate full hypervisor chain and have not been validated as a physical-host procedure. The repository vendors a Microsoft-signed shim; its MOK chain is now used by the diagnostic guide. Windows certificate-store imports or testsigning alone do not establish firmware Secure Boot trust. The inventory-only authorization does not cover VMX or driver execution; follow AGENTS.md for those tests.


Everything below is the primary host (your PC), not a VM. Intel x86-64,
UEFI. Keeping Secure Boot enabled requires a verified firmware-trusted EFI chain; Windows certificate-store imports and testsigning alone do not establish that trust. The current collector-only Path B uses the signed shim/MOK chain and keeps Secure Boot enabled.
Every command is run from the repo root. Footprint: one armed single-shot
firmware BootNext entry, one ESP staging folder (\EFI\insecure-boot\), bcd
flags, two trust entries, one companion driver/service - all removed in
section 6. The payoff run is Arc Raiders with EAC + Denuvo live.

```powershell
cd C:\Users\jayli\OneDrive\Documents\Projects\hsinv
$B  = .\build\combined\windows-vs
$UL = $B\components\uefi-loader\Release
$HH = $B\components\hypervisor\src\efi\Release
$CD = $B\components\hypervisor\src\driver\efi_runtime_companion\Release
$EP = $B\esp_analysis\Release
```

```powershell
# 0) Preflight (elevated PowerShell):
Get-ComputerInfo | Select-Object VirtualizationFirmwareEnabled   # must be True (VT-x on)
bcdedit /enum "{current}" | Out-File .\baremetal-bcd-before.txt   # recovery backup
bcdedit /export "{current}" .\baremetal-bcd.xml
bcdedit /export "{current}" .\baremetal-bcd.xml    # also: capture hypervisorlaunchtype original value

```powershell
# 1) Build (same superbuild; host kernel image + exact-match PDB pinned in):
cmake --preset combined-windows --fresh
  -DCOMBINED_KERNEL_IMAGE=C:\Windows\system32\ntoskrnl.exe
  -DCOMBINED_KERNEL_PDB=<exact-match ntkrnlmp.pdb for your running build>
  -DCOMBINED_KERNEL_BUILD=<your build, e.g. 26200>
  -DCOMBINED_KERNEL_UBR=<your UBR, e.g. 9457>
  -DCOMBINED_REQUIRE_KERNEL_PROFILE=ON
  -DCOMBINED_BUILD_EFI_RUNTIME_COMPANION=ON
  -DCOMBINED_BUILD_EFI_RUNTIME_SERVICE=ON
cmake --build --preset combined-windows --config Release
ctest --test-dir $B\hypervisor --build-config Release --output-on-failure
ctest --test-dir $B\uefi-loader --build-config Release --output-on-failure
```

```powershell
# 1) Artifacts produced by that build (paths):
#   payload        $HH\uefi_hypervisor.efi (+3 probe .efi)
#   companion      $CD\hvr1_efi_runtime_companion.sys + hvr1_runtime_service.exe
#   boot chain     $UL\loader.efi, $UL\shimx64.efi, $UL\ib-install.exe, $UL\ib-tcg-dump.exe
#   inventory      .codex-build\efi_inventory\efi\inventory.efi (optional second payload)
#   game modules   $EP\hypersinv_pub.exe (+ esp_analysis / hardened_esp archives)
```

```powershell
# 2) Sign + trust (host, once; repo carries no private keys):
$env:HVKEY = "<local signing password>";
certutil -genkeypair -n "CN=hypersinv" -len 3072 -a sha256 -y 1 .\hypersinv-test.pfx
$cer = .\hypersinv-test.cer    # public half - trusted + shipped to guest paths

#    Sign loader + payload exactly the way the gate does:
& $UL\ib-install.exe sign --signtool "<signtool.exe>" --pfx .\hypersinv-test.pfx --password-env HVKEY --signer-cert $cer --loader "$UL\loader.efi" --payload "$HH\uefi_hypervisor.efi" --out-dir .\stage

#    Trust (same stores the validated guest path uses):
& Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
& Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null

#    bcd window (restored in section 6):
bcdedit /set "{current}" testsigning on
bcdedit /set "{current}" hypervisorlaunchtype off
bcdedit /debug "{current}" off
bcdedit /bootdebug "{current}" off
```

```powershell
# 3) BOOT: stage the ESP, then arm the one-shot firmware boot
#    (reboot into it; payload is non-persistent, BootOrder untouched):
& $UL\ib-install.exe stage --signer-cert $cer --shim "$UL\shimx64.efi" --loader "$UL\loader.efi" --log .\divergence.ib --payload "$HH\uefi_hypervisor.efi" --esp-volume auto
#    Optional: also place .codex-build\efi_inventory\efi\inventory.efi into ESP\EFI\insecure-boot\
#    so loader.efi runs the variable inventory (prints "inventory collector: PASS";
#    report -> ESP\EFI\insecure-boot\HypersinvEfiInventory.txt; see tools/efi_inventory/LOADER_PAYLOAD_HANDOFF.md).
& $UL\ib-install.exe oneshot --mode bootnext --esp-volume auto
#   -> prints: BootNext set to owned Boot%04X; BootOrder was not changed.

#    Reboot now. Chain: shimx64.efi -> loader.efi -> uefi_hypervisor.efi
#    (all CPUs to root VMX) -> bootmgfw.efi -> normal Windows.
```

```powershell
# 4) HEALTH, once Windows has returned (authenticated, no driver needed):
& $UL\ib-tcg-dump.exe probe
#   expect: one line per active CPU  UEFI_HYPERVISOR_WINDOWS_CPU:<g>:<p>:PASS
#   plus the aggregate                UEFI_HYPERVISOR_WINDOWS_CPUID:PASS

#    Companion WDM + control service (service context = LocalSystem):
& sc.exe create <SERVICE_NAME> type= file kernel binPath= "$CD\hvr1_efi_runtime_companion.sys" start= demand
& sc.exe start <SERVICE_NAME>

#    Authenticated game commands (service context), handoff 5.x:
hvr1_runtime_service.exe status
hvr1_runtime_service.exe scan
hvr1_runtime_service.exe shutdown          # all-CPU detach gate + replay reject
hvr1_runtime_service.exe process <game_exe>
```

```powershell
# 5) GAME (Arc Raiders session with anti-cheat live):

#    publisher (service context; <=60 Hz until Ctrl+C):
hvr1_runtime_service.exe analysis-publish "Global\HypersinvOverlayDataV2" <HWND>

#    ESP reader (user session):
& $EP\hypersinv_pub.exe --pid <GAME_PID> [--hwnd <HEX>] [--hz 60]

#    overlay (user session; standalone tools/overlay project):
& <overlay build dir>\hypersinv_overlay.exe --target-pid <GAME_PID> --mapping "Global\HypersinvOverlayDataV2"

#    wire contract + field table: docs/OVERLAY_PIPELINE.md
```

```powershell
# 6) TEARDOWN (when done; leaves no owned state behind):
& sc.exe stop <SERVICE_NAME>; & sc.exe delete <SERVICE_NAME>
& "$env:SystemRoot\System32\mountvol.exe" X: /S
& Remove-Item -Recurse -Force X:\EFI\insecure-boot
& "$env:SystemRoot\System32\mountvol.exe" X: /D
& $UL\ib-install.exe oneshot --mode off --esp-volume auto
bcdedit /set "{current}" testsigning off
bcdedit /set "{current}" hypervisorlaunchtype <original value from .\baremetal-bcd-before.txt>
certutil -delstore Root "<thumbprint>"; certutil -delstore TrustedPublisher "<thumbprint>"
certutil -delkey CN=hypersinv

#    verify: bcdedit /enum "{current}" | diff vs .\baremetal-bcd-before.txt ->
#    only the owned Boot#### entry is gone.

# NOTES
# - The armed payload lives one boot: crash -> reboot -> BootNext self-clears,
#   BootOrder was never touched, normal Windows resumes (the VM-gate contract).
# - First physical run: expect the same 20/20 PASS + aggregate CPUID PASS the
#   disposable VM gate already proved; everything else runs against live RAM.
# - Fixed 1x1.8x1 entity boxes = known CL-1372005 bone-transform placeholder
#   (decrypt spec in docs/arc-raiders-offsets.md) until that TODO lands.
```
