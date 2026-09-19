# Launch a QEMU VM with OVMF firmware (pflash) and a swtpm-backed TPM 2.0.
#
# Snapshot discipline: the pristine VARS image is never mutated; this script
# copies it to a per-launch working copy. Pass -Snapshot to make disk writes
# temporary (recommended for all boot tests).
#
# TPM wiring (verified 2026-09-05): swtpm must run first and listen on a TCP
# port reachable from Windows QEMU:
#   swtpm socket --tpm2 --tpmstate dir=/tmp/swtpm-state \
#     --server type=tcp,port=2321 --ctrl type=tcp,port=2322 \
#     --flags not-need-init,startup-clear
# QEMU then connects with:
#   -chardev socket,id=chrtpm,host=127.0.0.1,port=2321
#   -tpmdev emulator,id=tpm0,chardev=chrtpm
#   -device tpm-tis,tpmdev=tpm0
# NOTE: not every Windows QEMU build ships the TPM emulator backend (the
# winget build on this machine rejects `-tpmdev` outright). This script probes
# the backend directly via `-tpmdev help` and skips the TPM devices with a
# warning when they are unavailable (or when -NoTpm is set).

param(
    [Parameter(Mandatory = $true)]
    [string]$DiskPath,          # Windows guest disk (raw or qcow2)
    [int]$TpmPort = 2321,       # swtpm TCP data port (see provision-swtpm.ps1)
    [int]$MemoryMB = 4096,
    [int]$Cpus = 4,
    [string]$Accel = "tcg",     # tcg (always works) or whpx (Windows Hypervisor Platform)
    [switch]$Snapshot,
    [switch]$NoTpm,             # force-skip TPM devices even if supported
    [switch]$Headless,          # no display; serial console to -SerialOut or a temp file (never stdio)
    [string]$SerialOut = ""     # when set, redirect serial console to this file
)

$ErrorActionPreference = "Stop"

# Resolve QEMU. winget does not add C:\Program Files\qemu to PATH, so probe
# common install locations before falling back to PATH lookup.
$qemuExe = $null
$candidates = @(
    "C:\Program Files\qemu\qemu-system-x86_64.exe",
    "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe"
)
foreach ($c in $candidates) {
    if (Test-Path -LiteralPath $c) { $qemuExe = $c; break }
}
if (-not $qemuExe) {
    $onPath = Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue
    if ($onPath) { $qemuExe = $onPath.Source }
}
if (-not $qemuExe) { Write-Error "qemu-system-x86_64 not found. Run tools/provision-qemu.ps1 first." }

$ovmfDir = Join-Path $PSScriptRoot "ovmf"
$code = Join-Path $ovmfDir "x64\code.fd"
$vars = Join-Path $ovmfDir "x64\vars.fd"

if (-not (Test-Path -LiteralPath $code)) { Write-Error "OVMF CODE not found. Run tools/provision-ovmf.ps1 first."; exit 1 }
if (-not (Test-Path -LiteralPath $vars)) { Write-Error "OVMF VARS not found. Run tools/provision-ovmf.ps1 first."; exit 1 }
if (-not (Test-Path -LiteralPath $DiskPath)) { Write-Error "Disk image not found: $DiskPath"; exit 1 }

# Per-launch working VARS copy (never mutate the pristine VARS).
$workVars = Join-Path $ovmfDir ("vars-" + [guid]::NewGuid().ToString("N") + ".fd")
Copy-Item -LiteralPath $vars -Destination $workVars

# Probe QEMU TPM support once (cache result in this process). `-tpmdev help`
# succeeds only when the TPM backend subsystem is compiled in; builds without
# it reject the option and exit non-zero. Checking `tpm-tis` in `-device help`
# is NOT equivalent: the frontend device can exist without the emulator backend.
$tpmSupported = $false
if (-not $NoTpm) {
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $probeOut = & $qemuExe -tpmdev help 2>&1
    $tpmSupported = ($LASTEXITCODE -eq 0) -and ((@($probeOut) -match "emulator").Count -gt 0)
    $ErrorActionPreference = $prevEap
}

$args = @(
    "-machine", "q35,accel=$Accel",
    "-m", "$MemoryMB",
    "-smp", "$Cpus",
    "-drive", "if=pflash,format=raw,readonly=on,file=$code",
    "-drive", "if=pflash,format=raw,file=$workVars"
)

if ($tpmSupported -and -not $NoTpm) {
    $args += @(
        "-chardev", "socket,id=chrtpm,host=127.0.0.1,port=$TpmPort",
        "-tpmdev", "emulator,id=tpm0,chardev=chrtpm",
        "-device", "tpm-tis,tpmdev=tpm0"
    )
    Write-Host "TPM 2.0 attached via swtpm on 127.0.0.1:$TpmPort."
}
else {
    Write-Warning "TPM devices SKIPPED: swtpm backend unavailable or QEMU build lacks -tpmdev support. Boot proceeds without a TPM."
}

$args += @(
    "-drive", "file=$DiskPath,format=raw,if=virtio",
    "-net", "none"
)
if ($Snapshot) { $args += "-snapshot" }

# Headless runs must never expose the serial console on inherited stdio: a
# long-running QEMU child holding a parent pipe open blocks the caller forever.
# Serial always goes to a file (-SerialOut wins if both are passed).
if ($Headless -and -not $SerialOut) {
    $SerialOut = Join-Path $env:TEMP ("ibqemu-serial-" + [guid]::NewGuid().ToString("N") + ".log")
}
if ($Headless -or $SerialOut) {
    $args += @("-display", "none", "-monitor", "none", "-serial", "file:$SerialOut")
    if ($Headless) { Write-Host "Serial console -> $SerialOut" }
}

Write-Host "Launching QEMU (accel=$Accel, qemu=$qemuExe)..."
& $qemuExe @args
