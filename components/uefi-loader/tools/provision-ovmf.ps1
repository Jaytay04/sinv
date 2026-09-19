# Provision OVMF (UEFI firmware) for QEMU.
#
# Verified source (2026-08-30):
#   repo:    https://github.com/rust-osdev/ovmf-prebuilt
#   release: edk2-stable202605-r1 (published 2026-06-06)
#   asset:   edk2-stable202605-r1-bin.tar.xz (6.5 MB)
#   contents: x64/code.fd, x64/vars.fd, x64/shell.efi
#   build:   standard OvmfPkgX64 (includes Tcg2Dxe; NOT the Secure Boot variant)
#
# M3 requires a Secure Boot-capable build (OVMF_CODE.secboot.fd). See
# tools/PROVISIONING.md for the Fedora edk2-ovmf RPM extraction path.

param(
    [string]$Tag = "edk2-stable202605-r1",
    # Expected SHA-256 of the downloaded archive. Leave empty to skip strict
    # verification (with a warning). Pin it after the first verified download
    # (see tools/PROVISIONING.md).
    [string]$ExpectedSha256 = ""
)

$ErrorActionPreference = "Stop"

$asset   = "$Tag-bin.tar.xz"
$url     = "https://github.com/rust-osdev/ovmf-prebuilt/releases/download/$Tag/$asset"
$outDir  = Join-Path $PSScriptRoot "ovmf"
$archive = Join-Path $outDir $asset

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

if (Test-Path (Join-Path $outDir "x64\code.fd")) {
    Write-Host "OVMF already provisioned at $outDir"
}
else {
    Write-Host "Downloading OVMF $Tag..."
    Invoke-WebRequest -Uri $url -OutFile $archive

    if ($ExpectedSha256) {
        $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLower()
        if ($actual -ne $ExpectedSha256.ToLower()) {
            Write-Error "SHA-256 mismatch for $asset`n  expected: $ExpectedSha256`n  actual:   $actual"
            exit 1
        }
        Write-Host "SHA-256 verified: $actual"
    }
    else {
        Write-Warning "No SHA-256 pinned for $asset. Pin it in tools/PROVISIONING.md after first verified download."
    }

    Write-Host "Extracting..."
    # The release tarball wraps everything in a top-level directory named
    # after the tag (e.g. "edk2-stable202605-r1-bin/"). Strip that one level so
    # the files land directly under $outDir (x64/code.fd, x64/vars.fd, ...).
    tar -xf $archive -C $outDir --strip-components=1
}

$code = Join-Path $outDir "x64\code.fd"
$vars = Join-Path $outDir "x64\vars.fd"
if (-not (Test-Path -LiteralPath $code) -or -not (Test-Path -LiteralPath $vars)) {
    Write-Error "Extraction did not produce x64/code.fd and x64/vars.fd."
    exit 1
}

Write-Host "OVMF ready:"
Write-Host "  CODE: $code"
Write-Host "  VARS: $vars"
