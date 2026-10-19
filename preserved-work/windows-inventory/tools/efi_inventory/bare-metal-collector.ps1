# Bare-metal EFI variable inventory for a primary host (user-approved safety-bypass path).
#   -Mode Arm     : stages inventory.efi on a USB as EFI\BOOT\BOOTX64.EFI (one-time boot target)
#   -Mode Collect : after the armed boot, parses the USB report + host SMBIOS from Windows
param(
    [Parameter(Position=0)][ValidateSet('Arm','Collect')][string]$Mode,
    [Parameter(Position=1)][string]$Usb,
    [string]$BuildDir
)
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $BuildDir) { $BuildDir = Join-Path $root '.codex-build\efi_inventory' }
$exe = Join-Path $BuildDir 'efi_inventory.exe'
$efi = Join-Path $BuildDir 'efi\inventory.efi'
foreach ($need in @($exe,$efi)) {
    if (-not (Test-Path $need)) { throw "missing ${need} - rebuild with the commands in docs\EFI_INVENTORY_TOOL_USAGE.md" }
}
if (-not $Usb) { throw "-Usb <drive-letter> is required (e.g. -Usb E)" }
$vol = Get-Volume -DriveLetter $Usb
if (-not $vol) { throw "drive ${Usb}: not found" }
if ($vol.FileSystemType -ne 'FAT32') { throw "${Usb}: must be FAT32 (Format-Volume -DriveLetter ${Usb} -FileSystem FAT32)" }
function Fresh([string]$p) {
    if (-not (Test-Path $p)) { return $p }
    $n = 0
    do { $n++; $q = "$p.${n}" } while (Test-Path $q)
    return $q
}
if ($Mode -eq 'Arm') {
    $bootdir = "${Usb}:\EFI\BOOT"
    New-Item -ItemType Directory -Path $bootdir -Force | Out-Null
    $target = Join-Path $bootdir 'BOOTX64.EFI'
    $old = "${Usb}:\HypersinvEfiInventory.txt"
    if (Test-Path $old) {
        $bak = "${Usb}:\old-HypersinvEfiInventory-$(Get-Date -Format yyyyMMddHHmmss).txt"
        Move-Item $old $bak
        Write-Warning "previous report preserved as $(Split-Path $bak -Leaf)"
    }
    Copy-Item $efi $target -Force
    $a = (Get-FileHash $efi -Algorithm SHA256).Hash
    $b = (Get-FileHash $target -Algorithm SHA256).Hash
    if ($a -ne $b) { throw "SHA256 mismatch after copy to ${target}" }
    Write-Output "ARMED ${target}"
    Write-Output "      sha256=${a}"
    Write-Output ""
    Write-Output "Next steps:"
    Write-Output "  1) reboot into your firmware one-time boot menu (ESC/F8/F12 at POST)"
    Write-Output "  2) pick the USB in UEFI mode, Secure Boot OFF for this one boot only (payload is unsigned)"
    Write-Output "     expect console line: EFI variable inventory: report ready."
    Write-Output "     report lands at ${Usb}:\HypersinvEfiInventory.txt"
    Write-Output "  3) after normal boot back into Windows:"
    Write-Output "     next, elevated PowerShell from repo root:"
    Write-Output "     powershell -ExecutionPolicy Bypass -File tools\efi_inventory\bare-metal-collector.ps1 Collect $Usb"
} else {
    $rep = "${Usb}:\HypersinvEfiInventory.txt"
    if (-not (Test-Path $rep)) { throw "no report at ${rep} - armed USB boot not run or collector failed" }
    $repSize = (Get-Item $rep).Length
    if ($repSize -lt 20) { throw "${rep} looks empty (${repSize} bytes); collector failure?" }
    $smb = Fresh (Join-Path $BuildDir 'baremetal-smbios.txt')
    $full = Fresh (Join-Path $BuildDir 'baremetal-full.txt')
    Write-Output "== scan-windows (${smb}) =="
    & $exe scan-windows $smb
    Write-Output "== scan (${rep} + SMBIOS -> ${full}) =="
    & $exe scan $rep $full
    Get-Content $full -TotalCount 15
}