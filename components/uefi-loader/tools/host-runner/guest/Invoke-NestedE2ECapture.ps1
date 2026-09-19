[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$TaskName = 'UEFILoader-Nested-E2E'
$Deadline = [DateTime]::UtcNow.AddMinutes(12)
$EvidenceRoot = $null
$Request = $null
$DeleteTask = $false
$ResultName = 'nested-result.json'

function Find-EvidenceRequest {
    $Requests = [ordered]@{
        'UEFILoader-Nested-BootNext-Verify.marker' = 'BootNextVerify'
        'UEFILoader-Nested-BootNext-Setup.marker' = 'BootNextSetup'
        'UEFILoader-Nested-E2E.marker' = 'Replay'
        'UEFILoader-Nested-Condition.marker' = 'Condition'
    }
    foreach ($Drive in [IO.DriveInfo]::GetDrives()) {
        if (-not $Drive.IsReady -or
            $Drive.DriveType -ne [IO.DriveType]::Fixed) { continue }
        foreach ($Marker in $Requests.Keys) {
            if (Test-Path -LiteralPath (Join-Path `
                    $Drive.RootDirectory.FullName $Marker) -PathType Leaf) {
                return [pscustomobject]@{
                    Root = $Drive.RootDirectory.FullName
                    Mode = $Requests[$Marker]
                }
            }
        }
    }
    return $null
}

function Invoke-CapturedProcess {
    param(
        [Parameter(Mandatory=$true)][string]$FilePath,
        [Parameter(Mandatory=$true)][string[]]$Arguments,
        [Parameter(Mandatory=$true)][string]$StdoutPath,
        [Parameter(Mandatory=$true)][string]$StderrPath,
        [int]$TimeoutSeconds = 300
    )
    $Info = [Diagnostics.ProcessStartInfo]::new()
    $Info.FileName = $FilePath
    $Quote = {
        param([string]$Value)
        if ($Value -notmatch '[\s"]') { return $Value }
        return '"' + $Value.Replace('"', '\"') + '"'
    }
    $Info.Arguments = (($Arguments | ForEach-Object {
        & $Quote ([string]$_)
    }) -join ' ')
    $Info.WorkingDirectory = Split-Path -Parent $FilePath
    $Info.UseShellExecute = $false
    $Info.CreateNoWindow = $true
    $Info.RedirectStandardOutput = $true
    $Info.RedirectStandardError = $true
    $Process = [Diagnostics.Process]::new()
    $Process.StartInfo = $Info
    if (-not $Process.Start()) { throw "Failed to start $FilePath." }
    $OutTask = $Process.StandardOutput.ReadToEndAsync()
    $ErrTask = $Process.StandardError.ReadToEndAsync()
    if (-not $Process.WaitForExit($TimeoutSeconds * 1000)) {
        try { $Process.Kill() } catch {}
        throw "$FilePath exceeded its bounded deadline."
    }
    $Process.WaitForExit()
    [IO.File]::WriteAllText($StdoutPath, [string]$OutTask.Result,
        [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($StderrPath, [string]$ErrTask.Result,
        [Text.UTF8Encoding]::new($false))
    $Code = [int]$Process.ExitCode
    $Process.Dispose()
    return $Code
}

function Invoke-WithEsp {
    param([Parameter(Mandatory=$true)][scriptblock]$Operation)
    $Esp = $null
    foreach ($Letter in @('R','S','T','U','V','W','X','Y','Z')) {
        if (-not (Test-Path -LiteralPath ($Letter + ':\'))) {
            $Esp = $Letter + ':'
            break
        }
    }
    if ($null -eq $Esp) { throw 'No bounded ESP mount letter is free.' }
    try {
        & "$env:SystemRoot\System32\mountvol.exe" $Esp /S | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "mountvol /S exited with code $LASTEXITCODE."
        }
        & $Operation $Esp
    }
    finally {
        & "$env:SystemRoot\System32\mountvol.exe" $Esp /D | Out-Null
    }
}

function Save-FirmwareBootManager {
    param([Parameter(Mandatory=$true)][string]$Path)
    $Text = (& "$env:SystemRoot\System32\bcdedit.exe" /enum `
        '{fwbootmgr}' 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0) {
        throw "bcdedit firmware query exited with code $LASTEXITCODE."
    }
    [IO.File]::WriteAllText($Path, $Text,
        [Text.UTF8Encoding]::new($false))
    return $Text
}

function Assert-EvidenceHashes {
    param([Parameter(Mandatory=$true)][string[]]$Names)
    $Expected = @{}
    foreach ($Line in Get-Content -LiteralPath (Join-Path `
            $EvidenceRoot 'nested-SHA256SUMS.txt')) {
        if ($Line -cnotmatch '^([0-9A-F]{64})  ([A-Za-z0-9_.-]+)$') {
            throw 'Invalid nested evidence manifest.'
        }
        $Expected[$Matches[2]] = $Matches[1]
    }
    foreach ($Name in $Names) {
        $Path = Join-Path $EvidenceRoot $Name
        if (-not $Expected.ContainsKey($Name) -or
            -not (Test-Path -LiteralPath $Path -PathType Leaf) -or
            (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -cne
                $Expected[$Name]) {
            throw "Evidence hash mismatch: $Name"
        }
    }
}

function Invoke-ReplayCapture {
    Assert-EvidenceHashes @('ib-tcg-dump.exe', 'ibaudit.exe')
    $Dump = Join-Path $EvidenceRoot 'ib-tcg-dump.exe'
    $Audit = Join-Path $EvidenceRoot 'ibaudit.exe'
    $ProbeCode = Invoke-CapturedProcess -FilePath $Dump -Arguments @('probe') `
        -StdoutPath (Join-Path $EvidenceRoot 'postboot-probe.stdout.txt') `
        -StderrPath (Join-Path $EvidenceRoot 'postboot-probe.stderr.txt')
    if ($ProbeCode -gt 1) { throw "probe exited with code $ProbeCode." }
    $Log = Join-Path $EvidenceRoot 'postboot-tcglog.ib'
    $CaptureCode = Invoke-CapturedProcess -FilePath $Dump -Arguments @(
        'capture', '--out', $Log, '--full',
        (Join-Path $EvidenceRoot 'postboot-tcglog-full.bin')) `
        -StdoutPath (Join-Path $EvidenceRoot 'postboot-capture.stdout.txt') `
        -StderrPath (Join-Path $EvidenceRoot 'postboot-capture.stderr.txt')
    if ($CaptureCode -ne 0) { throw "capture exited with code $CaptureCode." }
    $DumpCode = Invoke-CapturedProcess -FilePath $Audit -Arguments @(
        'dump', '--log', $Log) -StdoutPath (Join-Path $EvidenceRoot `
        'postboot-audit-dump.stdout.txt') -StderrPath (Join-Path `
        $EvidenceRoot 'postboot-audit-dump.stderr.txt')
    if ($DumpCode -ne 0) { throw "audit dump exited with code $DumpCode." }
    Invoke-WithEsp {
        param($Esp)
        Copy-Item -LiteralPath ($Esp + '\EFI\insecure-boot\divergence.ib') `
            -Destination (Join-Path $EvidenceRoot 'divergence.ib') -Force
        Copy-Item -LiteralPath `
            ($Esp + '\EFI\insecure-boot\handoff-status.txt') `
            -Destination (Join-Path $EvidenceRoot 'handoff-status.txt') -Force
    }
    $VerifyCode = Invoke-CapturedProcess -FilePath $Audit -Arguments @(
        'verify-pcrs', '--log', $Log, '--divergence',
        (Join-Path $EvidenceRoot 'divergence.ib')) -StdoutPath (Join-Path `
        $EvidenceRoot 'postboot-verify-pcrs.stdout.txt') -StderrPath `
        (Join-Path $EvidenceRoot 'postboot-verify-pcrs.stderr.txt')
    $VerifyText = Get-Content -Raw -LiteralPath (Join-Path $EvidenceRoot `
        'postboot-verify-pcrs.stdout.txt')
    if ($VerifyCode -ne 1 -or
        $VerifyText -notmatch '(?m)^Algorithm=0x000B PCRMismatchMask=0x[0-9A-F]{2} Verdict=log under-reports PCR; replay divergence attributed\r?$' -or
        $VerifyText -notmatch '(?m)^DivergenceManifestRecords=[1-9][0-9]* Attributed=YES\r?$' -or
        $VerifyText -match 'log over-reports PCR') {
        throw "verify-pcrs unexpected result: exit=$VerifyCode"
    }
}

function Invoke-BootNextSetup {
    Assert-EvidenceHashes @('ib-install.exe', 'loader.efi', 'shimx64.efi')
    Save-FirmwareBootManager (Join-Path $EvidenceRoot `
        'bootnext-firmware-before.txt') | Out-Null
    Invoke-WithEsp {
        param($Esp)
        $Stage = $Esp + '\EFI\insecure-boot'
        if (Test-Path -LiteralPath $Stage) {
            throw 'BootNext stage directory already exists.'
        }
        New-Item -ItemType Directory -Path $Stage -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $EvidenceRoot 'shimx64.efi') `
            -Destination (Join-Path $Stage 'shimx64.efi')
        Copy-Item -LiteralPath (Join-Path $EvidenceRoot 'loader.efi') `
            -Destination (Join-Path $Stage 'grubx64.efi')
        $Lines = @('schema=1')
        foreach ($Name in @('shimx64.efi', 'grubx64.efi')) {
            $Path = Join-Path $Stage $Name
            $Item = Get-Item -LiteralPath $Path
            $Hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
            $Lines += "$Name size=$($Item.Length) sha256=$Hash"
        }
        [IO.File]::WriteAllText((Join-Path $Stage 'manifest.sha256'),
            (($Lines -join "`r`n") + "`r`n"),
            [Text.UTF8Encoding]::new($false))
        $BootManager = $Esp + '\EFI\Microsoft\Boot\bootmgfw.efi'
        $Before = (Get-FileHash -LiteralPath $BootManager `
            -Algorithm SHA256).Hash
        [IO.File]::WriteAllText((Join-Path $EvidenceRoot `
            'bootnext-bootmgfw-before.sha256'), $Before + "`r`n",
            [Text.ASCIIEncoding]::new())
        $Code = Invoke-CapturedProcess -FilePath (Join-Path $EvidenceRoot `
            'ib-install.exe') -Arguments @('oneshot', '--mode', 'bootnext',
            '--esp-volume', 'auto') -StdoutPath (Join-Path $EvidenceRoot `
            'bootnext-set.stdout.txt') -StderrPath (Join-Path $EvidenceRoot `
            'bootnext-set.stderr.txt')
        if ($Code -ne 0) { throw "BootNext set exited with code $Code." }
        $Output = Get-Content -Raw -LiteralPath (Join-Path $EvidenceRoot `
            'bootnext-set.stdout.txt')
        if ($Output -notmatch 'BootNext set to owned Boot[0-9A-F]{4}; BootOrder was not changed\.') {
            throw 'BootNext set did not report its owned entry contract.'
        }
        $After = (Get-FileHash -LiteralPath $BootManager `
            -Algorithm SHA256).Hash
        if ($After -cne $Before) { throw 'BootNext setup changed bootmgfw.efi.' }
    }
    $FirmwareAfter = Save-FirmwareBootManager (Join-Path $EvidenceRoot `
        'bootnext-firmware-after-set.txt')
    if ($FirmwareAfter -notmatch '(?im)^bootsequence\s+') {
        throw 'Firmware did not expose BootNext as bootsequence after set.'
    }
    Move-Item -LiteralPath (Join-Path $EvidenceRoot `
        'UEFILoader-Nested-BootNext-Setup.marker') -Destination (Join-Path `
        $EvidenceRoot 'UEFILoader-Nested-BootNext-Verify.marker')
}

function Invoke-BootNextVerify {
    Assert-EvidenceHashes @('ib-install.exe', 'loader.efi', 'shimx64.efi')
    $FirmwareBeforeCleanup = Save-FirmwareBootManager (Join-Path `
        $EvidenceRoot 'bootnext-firmware-before-cleanup.txt')
    if ($FirmwareBeforeCleanup -match '(?im)^bootsequence\s+') {
        throw 'BootNext was not consumed before Windows handoff.'
    }
    Invoke-WithEsp {
        param($Esp)
        $StatusPath = $Esp + '\EFI\insecure-boot\handoff-status.txt'
        Copy-Item -LiteralPath $StatusPath -Destination (Join-Path `
            $EvidenceRoot 'bootnext-handoff-status.txt') -Force
        $StatusText = Get-Content -Raw -LiteralPath $StatusPath
        foreach ($Marker in @('state=READY_BOOT_MANAGER', 'replay=ABORTED',
                'restore=PASS', 'payload=SKIPPED',
                'exit_boot_services_calls=0')) {
            if ($StatusText -notmatch [regex]::Escape($Marker)) {
                throw "Loader BootNext handoff is missing: $Marker"
            }
        }
        $ExpectedHash = (Get-Content -Raw -LiteralPath (Join-Path `
            $EvidenceRoot 'bootnext-bootmgfw-before.sha256')).Trim()
        $BootManager = $Esp + '\EFI\Microsoft\Boot\bootmgfw.efi'
        $ActualHash = (Get-FileHash -LiteralPath $BootManager `
            -Algorithm SHA256).Hash
        [IO.File]::WriteAllText((Join-Path $EvidenceRoot `
            'bootnext-bootmgfw-after.sha256'), $ActualHash + "`r`n",
            [Text.ASCIIEncoding]::new())
        if ($ActualHash -cne $ExpectedHash) {
            throw 'BootNext cycle changed bootmgfw.efi.'
        }
        $Code = Invoke-CapturedProcess -FilePath (Join-Path $EvidenceRoot `
            'ib-install.exe') -Arguments @('oneshot', '--mode', 'off',
            '--esp-volume', 'auto') -StdoutPath (Join-Path $EvidenceRoot `
            'bootnext-clear.stdout.txt') -StderrPath (Join-Path $EvidenceRoot `
            'bootnext-clear.stderr.txt')
        if ($Code -ne 0) { throw "BootNext cleanup exited with code $Code." }
    }
    $FirmwareAfterCleanup = Save-FirmwareBootManager (Join-Path `
        $EvidenceRoot 'bootnext-firmware-after-cleanup.txt')
    $FirmwareBefore = Get-Content -Raw -LiteralPath (Join-Path `
        $EvidenceRoot 'bootnext-firmware-before.txt')
    if ($FirmwareAfterCleanup -cne $FirmwareBefore) {
        throw 'Firmware boot-manager state did not return to its pre-gate bytes.'
    }
}

$Result = [ordered]@{
    SchemaVersion = 1
    Status = 'FAIL'
    Message = 'Nested Windows startup workflow failed.'
    CapturedUtc = $null
}

try {
    do {
        $Request = Find-EvidenceRequest
        if ($null -ne $Request) { break }
        Start-Sleep -Seconds 2
    } while ([DateTime]::UtcNow -lt $Deadline)
    if ($null -eq $Request) {
        throw 'The marker evidence volume did not appear before the deadline.'
    }
    $EvidenceRoot = $Request.Root
    switch ($Request.Mode) {
        'Condition' {
            $ResultName = 'condition-result.json'
            $Result.Status = 'PASS'
            $Result.Message = 'Windows completed QEMU hardware conditioning.'
        }
        'Replay' {
            $DeleteTask = $true
            Invoke-ReplayCapture
            $Result.Status = 'PASS'
            $Result.Message = 'Windows startup captured and verified its post-replay TPM state.'
        }
        'BootNextSetup' {
            $ResultName = 'bootnext-setup-result.json'
            Invoke-BootNextSetup
            $Result.Status = 'PASS'
            $Result.Message = 'Windows set owned BootNext without replacing bootmgfw.efi.'
        }
        'BootNextVerify' {
            $ResultName = 'bootnext-result.json'
            $DeleteTask = $true
            Invoke-BootNextVerify
            $Result.Status = 'PASS'
            $Result.Message = 'Firmware consumed BootNext, loader ran once, and BootOrder was preserved.'
        }
        default { throw "Unsupported nested startup mode: $($Request.Mode)" }
    }
}
catch {
    $Result.Message = $_.Exception.Message
}
finally {
    if ($null -ne $EvidenceRoot) {
        $Result.CapturedUtc = [DateTime]::UtcNow.ToString('o')
        $Temporary = Join-Path $EvidenceRoot ($ResultName + '.tmp')
        $Final = Join-Path $EvidenceRoot $ResultName
        $Result | ConvertTo-Json -Depth 4 |
            Set-Content -LiteralPath $Temporary -Encoding utf8
        Move-Item -LiteralPath $Temporary -Destination $Final -Force
    }
    if ($DeleteTask) {
        & "$env:SystemRoot\System32\schtasks.exe" /Delete /TN $TaskName /F |
            Out-Null
    }
    & "$env:SystemRoot\System32\shutdown.exe" /s /t 5 /f /d p:0:0 /c `
        'UEFILoader nested startup workflow complete' | Out-Null
}
