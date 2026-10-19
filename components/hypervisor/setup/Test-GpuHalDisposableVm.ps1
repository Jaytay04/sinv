[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EfiPath,
    [Parameter(Mandatory = $true)][string]$RunDirectory,
    [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
    [ValidateRange(15, 180)][int]$TimeoutSeconds = 90,
    [string]$VmrunPath = 'C:\Program Files\VMware\VMware Workstation\vmrun.exe',
    [string]$QemuImgPath = 'C:\Program Files\qemu\qemu-img.exe'
)

$ErrorActionPreference = 'Stop'
$script:RunRoot = $null
$script:CommandLog = $null
$script:CommandNumber = 0
$script:VmStarted = $false
$script:VmOffConfirmed = $true
$script:VmStartAttempted = $false
$script:Failure = $null
$script:SerialText = ''
$script:CaptureStatus = 'not_attempted'
$script:SnapshotBefore = 'not_attempted'
$script:SnapshotAfter = 'not_attempted'
$script:StopStatus = 'not_attempted'
$script:Vmrun = $null
$script:Qemu = $null
$script:Vmx = $null
$script:ConfigValidated = $false
$script:NetworkOffline = $false
$script:NoIntelDevice = $false

function Test-Under([string]$Path, [string]$Root) {
    $prefix = $Root.TrimEnd('\') + '\'
    return $Path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
}

function Assert-VmxSafePath([string]$Path) {
    if ($Path.Contains('"') -or $Path.Contains("`r") -or $Path.Contains("`n")) {
        throw 'VMX file paths cannot contain quotes or line breaks.'
    }
}

function ConvertTo-WindowsArgument([string]$Value) {
    $b = New-Object System.Text.StringBuilder
    [void]$b.Append('"')
    $slashes = 0
    foreach ($c in $Value.ToCharArray()) {
        if ($c -eq [char]'\') { $slashes++; continue }
        if ($c -eq [char]'"') {
            [void]$b.Append(('\' * (2 * $slashes + 1)))
            [void]$b.Append('"')
        } else {
            if ($slashes -gt 0) { [void]$b.Append(('\' * $slashes)) }
            [void]$b.Append($c)
        }
        $slashes = 0
    }
    if ($slashes -gt 0) { [void]$b.Append(('\' * (2 * $slashes))) }
    [void]$b.Append('"')
    return $b.ToString()
}

function Invoke-LoggedProcess([string]$Label, [string]$Executable, [string[]]$Arguments) {
    $script:CommandNumber++
    $stem = '{0:D2}-{1}' -f $script:CommandNumber, $Label
    $stdout = Join-Path $script:RunRoot ($stem + '.stdout.log')
    $stderr = Join-Path $script:RunRoot ($stem + '.stderr.log')
    $line = ($Arguments | ForEach-Object { ConvertTo-WindowsArgument ([string]$_) }) -join ' '
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $exitCode = -1
    $launchError = ''
    $timedOut = $false
    try {
        $p = Start-Process -FilePath $Executable -ArgumentList $line -WorkingDirectory $script:RunRoot `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr -WindowStyle Hidden -PassThru
        # Cache the handle before a short-lived helper exits. PowerShell 5.1
        # otherwise sometimes loses ExitCode when Start-Process omits -Wait.
        $null = $p.Handle
        if ($p.WaitForExit(60000)) { $p.Refresh(); $exitCode = $p.ExitCode }
        else {
            $timedOut = $true
            $launchError = 'Helper exceeded 60 seconds; terminating only this helper process.'
            try { $p.Kill(); [void]$p.WaitForExit(5000) } catch { $launchError += ' ' + $_.Exception.Message }
            $exitCode = 124
        }
    } catch {
        $launchError = $_.Exception.Message
        if (-not (Test-Path -LiteralPath $stdout)) { [IO.File]::WriteAllText($stdout, '') }
        if (-not (Test-Path -LiteralPath $stderr)) { [IO.File]::WriteAllText($stderr, $launchError) }
    }
    $timer.Stop()
    $record = [ordered]@{ Label = $Label; Executable = $Executable; Arguments = $Arguments
        CommandLine = $line; ExitCode = $exitCode; TimedOut = $timedOut
        ElapsedMilliseconds = $timer.ElapsedMilliseconds; Stdout = $stdout; Stderr = $stderr
        LaunchError = $launchError }
    [IO.File]::AppendAllText($script:CommandLog, (($record | ConvertTo-Json -Compress) + "`r`n"),
        ([Text.UTF8Encoding]::new($false)))
    return [pscustomobject]$record
}

function Require-Success($Result, [string]$Operation) {
    if ($Result.ExitCode -ne 0) { throw "$Operation failed with exit code $($Result.ExitCode); see $($Result.Stderr)." }
}

function Read-SerialLog([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8, $true)
        try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
}

function Invoke-Vmrun([string]$Label, [string[]]$Arguments) {
    return Invoke-LoggedProcess $Label $script:Vmrun (@('-T', 'ws') + $Arguments)
}

function Write-Result {
    if ($null -eq $script:RunRoot -or -not (Test-Path -LiteralPath $script:RunRoot)) { return }
    $modelPassed = $script:SerialText.Contains('GPU_HAL_MODEL:ALL_PASS')
    $vmxPassed = $script:SerialText.Contains('GPU_HAL_VM:VMX_FIRMWARE_RETURN PASS')
    $enforcementPassed = $script:SerialText.Contains('GPU_ENFORCEMENT_MODEL:ALL_PASS SYNTHETIC_ONLY')
    $wcPassed = $script:SerialText.Contains('GPU_HAL_VM:ROOT_WC_RENDER PASS') -and
        $script:SerialText.Contains('GPU_HAL_VM:ROOT_WC_UNMAPPED PASS') -and
        $script:SerialText.Contains('GPU_HAL_VM:ROOT_WC_BOUNDS PASS') -and
        $script:SerialText.Contains('GPU_HAL_VM:ROOT_WC_NO_BACKEND PASS')
    $badModel = $script:SerialText -match 'GPU_HAL_MODEL:FAIL|GPU_ENFORCEMENT_MODEL:FAIL'
    $badHypervisor = $script:SerialText -match '(?i)uefi hypervisor: .*(FAIL|FATAL)|BSP virtualization failure:|GPU_HAL_VM:.* FAIL'
    $passed = -not $script:Failure -and $modelPassed -and $enforcementPassed -and $wcPassed -and $vmxPassed -and -not $badModel -and -not $badHypervisor -and
        $script:ConfigValidated -and $script:NetworkOffline -and $script:NoIntelDevice -and
        $script:SnapshotBefore -eq 'passed' -and $script:SnapshotAfter -eq 'passed' -and
        $script:StopStatus -in @('not_required', 'not_running', 'passed')
    $result = [ordered]@{
        Passed = [bool]$passed; SeparateEfiModel = [bool]$modelPassed; VMX = [bool]$vmxPassed
        SyntheticEnforcementModel = [bool]$enforcementPassed; ActualRootWcRenderAndUnmap = [bool]$wcPassed
        ModelFailureMarker = [bool]$badModel; HypervisorFailureMarker = [bool]$badHypervisor
        VmxConfiguration = [ordered]@{ Path = $script:Vmx; Validated = [bool]$script:ConfigValidated }
        SnapshotBefore = $script:SnapshotBefore; SnapshotAfter = $script:SnapshotAfter
        CaptureStatus = $script:CaptureStatus; NetworkOffline = [bool]$script:NetworkOffline
        NoIntelDevice = [bool]$script:NoIntelDevice; VmOffConfirmed = [bool]$script:VmOffConfirmed
        RunDirectory = $script:RunRoot
        SerialLog = if ($script:RunRoot) { Join-Path $script:RunRoot 'serial.log' } else { $null }
        Failure = $script:Failure; ScopeLimitations = @(
            'PCI/EPT/VT-d admission and Intel plane/GGTT evidence in the EFI integration model are synthetic; they do not attest physical device exclusion.'
            'ROOT_WC markers separately check a real VMX-root WC CPU mapping, renderer pixels and unmap in VM RAM; no Intel device or DMA engine is involved.'
            'VMware SVGA does not prove Intel UHD 770/Alder Lake PCH physical plane programming or scanout.'
            'This VM does not prove the host dual-GPU topology or that Windows excludes Intel while NVIDIA drives the desktop.'
            'A VMX firmware-return marker proves the EFI payload returned through its VMX path in this disposable VM only.'
        ); CommandLog = $script:CommandLog
    }
    [IO.File]::WriteAllText((Join-Path $script:RunRoot 'result.json'),
        ($result | ConvertTo-Json -Depth 8), ([Text.UTF8Encoding]::new($false)))
}

try {
    $workspace = (Resolve-Path -LiteralPath $WorkspaceRoot).Path
    if (-not (Test-Path -LiteralPath $workspace -PathType Container)) { throw 'WorkspaceRoot must be an existing directory.' }
    $buildCandidate = Join-Path $workspace '.codex-build'
    if (-not (Test-Path -LiteralPath $buildCandidate -PathType Container)) { throw 'WorkspaceRoot must contain an existing .codex-build directory.' }
    $buildRoot = (Resolve-Path -LiteralPath $buildCandidate).Path.TrimEnd('\')
    $workspaceRootResolved = $workspace.TrimEnd('\')
    if (-not (Test-Under $buildRoot $workspaceRootResolved)) { throw '.codex-build resolves outside WorkspaceRoot.' }
    $runCandidate = [IO.Path]::GetFullPath($RunDirectory)
    Assert-VmxSafePath $runCandidate
    if (-not (Test-Under $runCandidate $buildRoot)) { throw 'RunDirectory must be a new child of WorkspaceRoot\.codex-build.' }
    if (Test-Path -LiteralPath $runCandidate) { throw 'RunDirectory already exists; choose a fresh disposable directory.' }
    $existingParent = Split-Path -Parent $runCandidate
    while (-not (Test-Path -LiteralPath $existingParent -PathType Container)) {
        $nextParent = Split-Path -Parent $existingParent
        if (-not $nextParent -or $nextParent -eq $existingParent) { throw 'RunDirectory has no existing parent.' }
        $existingParent = $nextParent
    }
    $resolvedParent = (Resolve-Path -LiteralPath $existingParent).Path.TrimEnd('\')
    if (($resolvedParent -ne $buildRoot) -and -not (Test-Under $resolvedParent $buildRoot)) {
        throw 'An existing RunDirectory parent resolves outside .codex-build.'
    }
    $ancestor = $existingParent
    while ($ancestor -and (($ancestor -eq $buildRoot) -or (Test-Under $ancestor $buildRoot))) {
        $ancestorItem = Get-Item -LiteralPath $ancestor
        if (($ancestorItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "RunDirectory ancestor is a reparse point: $ancestor"
        }
        if ($ancestor -eq $buildRoot) { break }
        $ancestor = Split-Path -Parent $ancestor
    }
    $efi = (Resolve-Path -LiteralPath $EfiPath).Path
    if (-not (Test-Path -LiteralPath $efi -PathType Leaf) -or [IO.Path]::GetExtension($efi) -ine '.efi') { throw 'EfiPath must name an existing EFI image.' }
    $script:Vmrun = (Resolve-Path -LiteralPath $VmrunPath).Path
    $script:Qemu = (Resolve-Path -LiteralPath $QemuImgPath).Path
    if (-not (Test-Path -LiteralPath $script:Vmrun -PathType Leaf) -or -not (Test-Path -LiteralPath $script:Qemu -PathType Leaf)) { throw 'vmrun.exe and qemu-img.exe must both exist.' }

    New-Item -ItemType Directory -Path $runCandidate | Out-Null
    $script:RunRoot = (Resolve-Path -LiteralPath $runCandidate).Path.TrimEnd('\')
    Assert-VmxSafePath $script:RunRoot
    if (-not (Test-Under $script:RunRoot $buildRoot)) { throw 'Resolved RunDirectory escaped .codex-build.' }
    $script:CommandLog = Join-Path $script:RunRoot 'commands.jsonl'
    $media = Join-Path $script:RunRoot 'media'
    $boot = Join-Path $media 'EFI\BOOT'
    New-Item -ItemType Directory -Path $boot -Force | Out-Null
    $efiCopy = Join-Path $boot 'BOOTX64.EFI'
    Copy-Item -LiteralPath $efi -Destination $efiCopy
    if ((Get-Item -LiteralPath $efiCopy).Length -eq 0) { throw 'Copied EFI image is empty.' }

    $raw = Join-Path $script:RunRoot 'boot-fat.raw'
    $vmdk = Join-Path $script:RunRoot 'boot-fat.vmdk'
    $serial = Join-Path $script:RunRoot 'serial.log'
    $script:Vmx = Join-Path $script:RunRoot 'gpu-hal-disposable.vmx'
    $fatSource = 'fat:' + $media
    Require-Success (Invoke-LoggedProcess 'qemu-vvfat-to-raw' $script:Qemu @('convert', '-f', 'vvfat', '-O', 'raw', $fatSource, $raw)) 'qemu-img vvfat conversion'
    Require-Success (Invoke-LoggedProcess 'qemu-raw-to-vmdk' $script:Qemu @('convert', '-f', 'raw', '-O', 'vmdk', '-o', 'subformat=monolithicSparse', $raw, $vmdk)) 'qemu-img VMDK conversion'
    if (-not (Test-Path -LiteralPath $vmdk -PathType Leaf)) { throw 'qemu-img did not create the VM disk.' }

    $leaf = [IO.Path]::GetFileName($script:RunRoot)
    $displayName = 'gpu-hal-disposable-' + [regex]::Replace($leaf, '[^A-Za-z0-9._-]', '_')
    $vmxLines = @('.encoding = "UTF-8"', 'config.version = "8"', 'virtualHW.version = "22"',
        ('displayName = "' + $displayName + '"'), 'guestOS = "other-64"', 'firmware = "efi"',
        'uefi.secureBoot.enabled = "FALSE"', 'vhv.enable = "TRUE"', 'numvcpus = "1"',
        'cpuid.coresPerSocket = "1"', 'memsize = "512"', 'sata0.present = "TRUE"',
        'sata0:0.present = "TRUE"', ('sata0:0.fileName = "' + $vmdk + '"'), 'sata0:0.deviceType = "disk"',
        'serial0.present = "TRUE"', 'serial0.fileType = "file"', ('serial0.fileName = "' + $serial + '"'),
        'serial0.startConnected = "TRUE"', 'serial0.yieldOnMsrRead = "TRUE"',
        'ethernet0.present = "FALSE"', 'usb.present = "FALSE"', 'sound.present = "FALSE"',
        'vmci0.present = "FALSE"', 'sharedFolders.present = "FALSE"', 'mks.enable3d = "FALSE"')
    [IO.File]::WriteAllLines($script:Vmx, $vmxLines, ([Text.UTF8Encoding]::new($false)))
    $vmxText = [IO.File]::ReadAllText($script:Vmx)
    if ($vmxText -match '(?im)^\s*ethernet\d+\.present\s*=\s*"TRUE"' -or
        $vmxText -match '(?i)pciPassthru|PhysicalDrive|deviceType\s*=\s*"rawDisk"') { throw 'VMX safety validation found network access or physical device passthrough.' }
    foreach ($match in [regex]::Matches($vmxText, '(?im)^\s*[^\r\n]*\.fileName\s*=\s*"([^"]+)"')) {
        $path = [IO.Path]::GetFullPath($match.Groups[1].Value)
        if (-not (Test-Under $path $script:RunRoot)) { throw "VMX path escapes RunDirectory: $path" }
    }
    $script:NetworkOffline = $vmxText -match '(?im)^\s*ethernet0\.present\s*=\s*"FALSE"' -and
        $vmxText -notmatch '(?im)^\s*ethernet\d+\.present\s*=\s*"TRUE"'
    $script:NoIntelDevice = $vmxText -notmatch '(?i)pciPassthru|PhysicalDrive|rawDisk'
    $script:ConfigValidated = $script:NetworkOffline -and $script:NoIntelDevice -and
        $vmxText.Contains('virtualHW.version = "22"') -and $vmxText.Contains('vhv.enable = "TRUE"') -and
        $vmxText.Contains('serial0.yieldOnMsrRead = "TRUE"')
    if (-not $script:ConfigValidated) { throw 'VMX configuration did not satisfy the disposable offline test contract.' }

    $before = Invoke-Vmrun 'snapshot-before' @('snapshot', $script:Vmx, 'before-gpu-hal')
    $script:SnapshotBefore = if ($before.ExitCode -eq 0) { 'passed' } else { 'failed' }
    Require-Success $before 'Before snapshot'
    $script:VmStartAttempted = $true
    $script:VmOffConfirmed = $false
    $start = Invoke-Vmrun 'start-nogui' @('start', $script:Vmx, 'nogui')
    $script:VmStarted = $start.ExitCode -eq 0
    Require-Success $start 'VM start'

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $script:SerialText = Read-SerialLog $serial
        if ($script:SerialText -match 'GPU_HAL_MODEL:FAIL|GPU_ENFORCEMENT_MODEL:FAIL|(?i)uefi hypervisor: .*(FAIL|FATAL)|BSP virtualization failure:|GPU_HAL_VM:.* FAIL') { break }
        if ($script:SerialText.Contains('GPU_HAL_MODEL:ALL_PASS') -and $script:SerialText.Contains('GPU_HAL_VM:VMX_FIRMWARE_RETURN PASS')) { break }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $deadline)
    $script:SerialText = Read-SerialLog $serial
    if ($script:SerialText -match 'GPU_HAL_MODEL:FAIL|GPU_ENFORCEMENT_MODEL:FAIL|(?i)uefi hypervisor: .*(FAIL|FATAL)|BSP virtualization failure:|GPU_HAL_VM:.* FAIL') {
        throw 'EFI serial log contains a model or hypervisor failure marker.'
    }
    if (-not $script:SerialText.Contains('GPU_HAL_MODEL:ALL_PASS')) { throw "Timed out waiting for GPU_HAL_MODEL:ALL_PASS; see $serial." }
    if (-not $script:SerialText.Contains('GPU_HAL_VM:VMX_FIRMWARE_RETURN PASS')) { throw "Timed out waiting for GPU_HAL_VM:VMX_FIRMWARE_RETURN PASS; see $serial." }
    if (-not $script:SerialText.Contains('GPU_ENFORCEMENT_MODEL:ALL_PASS SYNTHETIC_ONLY') -or
        -not $script:SerialText.Contains('GPU_HAL_VM:ROOT_WC_RENDER PASS') -or
        -not $script:SerialText.Contains('GPU_HAL_VM:ROOT_WC_UNMAPPED PASS') -or
        -not $script:SerialText.Contains('GPU_HAL_VM:ROOT_WC_BOUNDS PASS') -or
        -not $script:SerialText.Contains('GPU_HAL_VM:ROOT_WC_NO_BACKEND PASS')) {
        throw "Required enforcement-model or actual root WC markers are missing; see $serial."
    }
} catch {
    $script:Failure = $_.Exception.Message
} finally {
    if ($script:VmStartAttempted) {
        $script:VmStarted = $false
        try {
            $listed = Invoke-Vmrun 'list-target-state' @('list')
            if ($listed.ExitCode -eq 0) {
                $runningVms = [IO.File]::ReadAllText($listed.Stdout)
                $script:VmStarted = $runningVms -match [regex]::Escape($script:Vmx)
                $script:VmOffConfirmed = -not $script:VmStarted
            }
        } catch { if (-not $script:Failure) { $script:Failure = 'Could not confirm disposable VM state: ' + $_.Exception.Message } }
    }
    if ($script:VmStarted) {
        try {
            $screen = Join-Path $script:RunRoot 'screen.png'
            $capture = Invoke-Vmrun 'capture-screen' @('captureScreen', $script:Vmx, $screen)
            $script:CaptureStatus = if ($capture.ExitCode -eq 0 -and (Test-Path -LiteralPath $screen)) { 'captured' } else { 'unavailable' }
        } catch { $script:CaptureStatus = 'unavailable: ' + $_.Exception.Message }
    }
    if ($script:VmStartAttempted) {
        try {
            $stop = Invoke-Vmrun 'stop-hard' @('stop', $script:Vmx, 'hard')
            if ($stop.ExitCode -eq 0) { $script:StopStatus = 'passed'; $script:VmOffConfirmed = $true }
            elseif ($script:VmOffConfirmed) { $script:StopStatus = 'not_running' }
            else {
                $script:StopStatus = 'failed'
                if (-not $script:Failure) { $script:Failure = 'VM stop failed; see ' + $stop.Stderr }
            }
        } catch {
            $script:StopStatus = if ($script:VmOffConfirmed) { 'not_running' } else { 'failed' }
            if (-not $script:VmOffConfirmed -and -not $script:Failure) { $script:Failure = $_.Exception.Message }
        }
    } else { $script:StopStatus = 'not_required' }
    if ($script:RunRoot -and $script:Vmx -and (Test-Path -LiteralPath $script:Vmx) -and
        $script:VmOffConfirmed) {
        try {
            $after = Invoke-Vmrun 'snapshot-after' @('snapshot', $script:Vmx, 'after-gpu-hal')
            $script:SnapshotAfter = if ($after.ExitCode -eq 0) { 'passed' } else { 'failed' }
            if ($after.ExitCode -ne 0 -and -not $script:Failure) { $script:Failure = 'After snapshot failed; see ' + $after.Stderr }
        } catch { $script:SnapshotAfter = 'failed'; if (-not $script:Failure) { $script:Failure = $_.Exception.Message } }
    } elseif (-not $script:VmOffConfirmed) {
        $script:SnapshotAfter = 'skipped_vm_off_unconfirmed'
        if (-not $script:Failure) { $script:Failure = 'After snapshot skipped because vmrun did not confirm the VM stopped.' }
    }
    if ($script:RunRoot) { Write-Result }
}

if ($script:Failure) { throw $script:Failure }
Write-Output (Join-Path $script:RunRoot 'result.json')
