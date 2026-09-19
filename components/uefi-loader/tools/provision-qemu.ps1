# Provision QEMU on Windows.
#
# Verified source (2026-08-30):
#   winget package id: SoftwareFreedomConservancy.QEMU  (latest 11.1.0)
#   winget.run: https://winget.run/pkg/SoftwareFreedomConservancy/QEMU
#
# Preferred install path is winget (reliable, updates tracked). Scoop and
# Chocolatey are provided as explicit fallbacks but are NOT the default.

param(
    [switch]$UseScoop,
    [switch]$UseChoco
)

$ErrorActionPreference = "Stop"

$qemu = Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue
if ($qemu) {
    Write-Host "QEMU already installed: $($qemu.Source)"
    exit 0
}

if ($UseScoop) {
    Write-Host "Installing QEMU via Scoop..."
    scoop install qemu
}
elseif ($UseChoco) {
    Write-Host "Installing QEMU via Chocolatey..."
    choco install qemu -y
}
else {
    Write-Host "Installing QEMU via winget (SoftwareFreedomConservancy.QEMU)..."
    winget install --id SoftwareFreedomConservancy.QEMU -e `
        --accept-package-agreements --accept-source-agreements
    if ($LASTEXITCODE -ne 0) {
        Write-Error "winget install failed with exit code $LASTEXITCODE"
        exit 1
    }
}

# A freshly installed QEMU may not be on PATH in the current shell.
$qemu = Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue
if (-not $qemu) {
    Write-Warning "qemu-system-x86_64 not found on PATH. Open a new shell and re-run, or add QEMU's install dir to PATH manually."
    exit 1
}

Write-Host "QEMU ready: $($qemu.Source)"
