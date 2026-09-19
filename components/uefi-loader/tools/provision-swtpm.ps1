# Provision and run swtpm (software TPM 2.0 emulator) on Windows.
#
# VERIFIED STATUS (2026-09-05):
#   - No official Windows swtpm binary exists (source tarballs only).
#   - swtpm is NOT packaged in MSYS2, winget, or Chocolatey.
#   - The MSYS2 source build is the only native-Windows path. libtpms (the
#     required library) compiles under MINGW64 with 5 small, documented
#     portability patches. swtpm itself does NOT compile on Windows as-is: it
#     uses fork(), socketpair(), poll(), mmap(), AF_UNIX sockets, getopt_long(),
#     and setuid() with zero Windows support in its source. See PROVISIONING.md.
#   - WSL2 / Virtual-Machine-Platform is BANNED on this machine (other projects
#     need Hyper-V off). Do not suggest or touch Windows features.
#
# Actions:
#   provision  (default)  detect an installed swtpm and print the exact build/run
#                         steps. Prints the porting boundary honestly.
#   start                 start "swtpm socket" detached with the correct PATH and
#                         a pidfile; stdout/stderr redirected to files.
#   stop                  stop the swtpm socket started by this script.
#   status                report whether the swtpm socket is listening.

param(
    [ValidateSet("provision", "start", "stop", "status")]
    [string]$Action = "provision",
    [int]$ServerPort = 2321,
    [int]$CtrlPort = 2322,
    [string]$StateDir = ""      # default: <repo>\tools\swtpm\state (gitignored)
)

$ErrorActionPreference = "Stop"

$msysRoot = "C:\msys64"
$swtpmExe = Join-Path $msysRoot "mingw64\bin\swtpm.exe"

if (-not $StateDir) {
    $StateDir = Join-Path $PSScriptRoot "swtpm\state"
}
$pidFile = Join-Path $StateDir "swtpm.pid"
$outLog  = Join-Path $StateDir "swtpm.stdout.log"
$errLog  = Join-Path $StateDir "swtpm.stderr.log"

function Get-SwtpmStartArgs {
    # Note: swtpm resolves --tpmstate dir= relative to the WORKING DIRECTORY.
    # We pass an absolute path so the detached process is unambiguous.
    return @(
        "socket", "--tpm2",
        "--tpmstate", "dir=$StateDir",
        "--server", "type=tcp,port=$ServerPort",
        "--ctrl", "type=tcp,port=$CtrlPort",
        "--flags", "not-need-init,startup-clear",
        "--log", "file=$(Join-Path $StateDir 'swtpm.log'),level=5"
    )
}

function Get-RunningPid {
    if (Test-Path -LiteralPath $pidFile) {
        $id = (Get-Content -LiteralPath $pidFile -ErrorAction SilentlyContinue | Select-Object -First 1)
        if ($id -match '^\d+$') {
            $proc = Get-Process -Id ([int]$id) -ErrorAction SilentlyContinue
            if ($proc -and $proc.ProcessName -like "swtpm*") { return [int]$id }
        }
    }
    # Fallback: any swtpm process.
    $p = Get-Process swtpm -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($p) { return $p.Id }
    return $null
}

function Test-PortListening([int]$Port) {
    $conn = Test-NetConnection -ComputerName 127.0.0.1 -Port $Port -WarningAction SilentlyContinue -InformationLevel Quiet
    return [bool]$conn
}

switch ($Action) {
    "provision" {
        if (Test-Path -LiteralPath $swtpmExe) {
            Write-Host "swtpm found: $swtpmExe"
            Write-Host @"
Start it with:
  powershell -File tools\provision-swtpm.ps1 -Action start
Stop it with:
  powershell -File tools\provision-swtpm.ps1 -Action stop
Then boot the VM with tools\launch-vm.ps1 (connects to 127.0.0.1:$ServerPort).
"@
            exit 0
        }

        if (Test-Path -LiteralPath (Join-Path $msysRoot "usr\bin\bash.exe")) {
            Write-Host "MSYS2 found at $msysRoot but swtpm is not built."
        } else {
            Write-Host "MSYS2 not found at $msysRoot."
        }

        # WSL path is intentionally NOT offered: banned on this machine.
        Write-Host @"
There is no drop-in native Windows swtpm binary today (verified 2026-09-05:
no upstream Windows build, no MSYS2/winget/choco package).

The only native-Windows path is building from source under MSYS2:
  - libtpms v0.10.2  -> compiles on MINGW64 with 5 documented patches.
  - swtpm   v0.10.2  -> NOT portable as-is (fork, socketpair, poll, mmap,
                        AF_UNIX sockets, getopt_long, setuid; zero Windows
                        support in its source). Porting is a separate work item.

Exact steps, pinned versions, and the portability patches are in
tools\PROVISIONING.md (swtpm section). This script refuses to fabricate an
install and does not touch Windows features (WSL2/VMP banned here).
"@
        exit 1
    }

    "start" {
        if (-not (Test-Path -LiteralPath $swtpmExe)) {
            Write-Error "swtpm.exe not found at $swtpmExe. Build it first (see tools\PROVISIONING.md)."
        }
        if (Test-PortListening $ServerPort) {
            Write-Warning "Port $ServerPort is already in use; swtpm may already be running."
        }
        New-Item -ItemType Directory -Path $StateDir -Force | Out-Null

        # swtpm needs its MINGW64 runtime DLLs; prepend the MSYS2 bin dir.
        $env:Path = (Join-Path $msysRoot "mingw64\bin") + ";" + $env:Path
        $args = Get-SwtpmStartArgs
        Write-Host "Starting swtpm (detached): $swtpmExe $($args -join ' ')"
        $p = Start-Process -FilePath $swtpmExe -ArgumentList $args `
            -WorkingDirectory $StateDir -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput $outLog -RedirectStandardError $errLog
        Set-Content -LiteralPath $pidFile -Value $p.Id
        Write-Host "swtpm started, pid $($p.Id). Logs: $outLog / $errLog"
        Write-Host "Give it a moment, then check: powershell -File tools\provision-swtpm.ps1 -Action status"
        exit 0
    }

    "stop" {
        $id = Get-RunningPid
        if (-not $id) {
            Write-Host "No running swtpm found."
            exit 0
        }
        Write-Host "Stopping swtpm (pid $id)..."
        Stop-Process -Id $id -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $pidFile -Force -ErrorAction SilentlyContinue
        Write-Host "Stopped."
        exit 0
    }

    "status" {
        if (Test-PortListening $ServerPort) {
            Write-Host "swtpm socket LISTENING on 127.0.0.1:$ServerPort"
            $id = Get-RunningPid
            if ($id) { Write-Host "process pid: $id" }
            exit 0
        }
        Write-Host "swtpm socket NOT listening on 127.0.0.1:$ServerPort"
        $id = Get-RunningPid
        if ($id) { Write-Host "(process pid $id exists but port is not open yet)" }
        exit 1
    }
}
