[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$TaskName = 'UEFILoader-Nested-E2E'
$ReplayTaskName = 'UEFILoader-Nested-E2E-Replay'
$AuditTaskName = 'UEFILoader-Nested-E2E-Audit'
$AuditLauncherRegistryPath =
    'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Run'
$AuditLauncherValueName = 'UEFILoaderNestedE2EAudit'
$AuditServiceName = 'UEFILoaderNestedE2EAudit'
$AuditServiceBinary = Join-Path (Split-Path -Parent $PSCommandPath) `
    'ib-nested-e2e-service.exe'
$Deadline = [DateTime]::UtcNow.AddMinutes(12)
$EvidenceRoot = $null
$Request = $null
$DeleteTask = $false
$ResultName = 'nested-result.json'
$RestartForConditionValidation = $false
$MountedEvidenceRoot = $null
$ConditionRebootFlag = Join-Path (Split-Path -Parent $PSCommandPath) `
    'conditioning-reboot.flag'

function Write-Progress {
    param([Parameter(Mandatory=$true)][string]$Stage)
    if ($null -eq $EvidenceRoot) { return }
    $Line = '{0} STAGE={1}' -f [DateTime]::UtcNow.ToString('o'), $Stage
    [IO.File]::AppendAllText((Join-Path $EvidenceRoot `
        'postboot-progress.txt'), $Line + "`r`n",
        [Text.UTF8Encoding]::new($false))
}

function Install-ReplayStartupTask {
    $PowerShell = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
    $TaskCommand = '"{0}" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "{1}"' -f `
        $PowerShell, $PSCommandPath
    & "$env:SystemRoot\System32\schtasks.exe" /Create /TN $ReplayTaskName `
        /SC ONSTART /RU SYSTEM /RL HIGHEST /TR $TaskCommand /F | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Replay startup task creation exited with code $LASTEXITCODE."
    }
    & "$env:SystemRoot\System32\schtasks.exe" /Change /TN $TaskName `
        /Disable | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Preparation startup task disable exited with code $LASTEXITCODE."
    }
}

function Install-AuditTask {
    $PowerShell = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
    $TaskCommand = '"{0}" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "{1}"' -f `
        $PowerShell, $PSCommandPath
    $StartTime = (Get-Date).AddMinutes(1).ToString('HH:mm')
    & "$env:SystemRoot\System32\schtasks.exe" /Create /TN $AuditTaskName `
        /SC MINUTE /MO 1 /ST $StartTime /RU SYSTEM /RL HIGHEST `
        /TR $TaskCommand /F | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Audit startup task creation exited with code $LASTEXITCODE."
    }
    & "$env:SystemRoot\System32\schtasks.exe" /Change /TN $ReplayTaskName `
        /Disable | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Replay startup task disable exited with code $LASTEXITCODE."
    }
    $Launcher = '"{0}" /Run /TN "{1}"' -f `
        "$env:SystemRoot\System32\schtasks.exe", $AuditTaskName
    New-ItemProperty -LiteralPath $AuditLauncherRegistryPath `
        -Name $AuditLauncherValueName -Value $Launcher `
        -PropertyType String -Force | Out-Null
    if (-not (Test-Path -LiteralPath $AuditServiceBinary -PathType Leaf)) {
        throw 'The nested E2E service binary is missing.'
    }
    $ServiceCommand = '"{0}"' -f $AuditServiceBinary
    & "$env:SystemRoot\System32\sc.exe" create $AuditServiceName `
        start= auto obj= LocalSystem binPath= $ServiceCommand | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Audit boot service creation exited with code $LASTEXITCODE."
    }
}

function Find-EvidenceRequest {
    $Requests = [ordered]@{
        'UEFILoader-Nested-BootNext-Verify.marker' = 'BootNextVerify'
        'UEFILoader-Nested-BootNext-Setup.marker' = 'BootNextSetup'
        'UEFILoader-Nested-E2E.marker' = 'Replay'
        'UEFILoader-Nested-Condition.marker' = 'Condition'
    }
    foreach ($Drive in [IO.DriveInfo]::GetDrives()) {
        if (-not $Drive.IsReady -or
            $Drive.DriveType -eq [IO.DriveType]::CDRom) { continue }
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
    foreach ($Volume in Get-CimInstance -ClassName Win32_Volume) {
        if ([string]::IsNullOrWhiteSpace([string]$Volume.DeviceID) -or
            -not [string]::IsNullOrWhiteSpace([string]$Volume.DriveLetter)) {
            continue
        }
        foreach ($Letter in @('Q','R','S','T','U','V','W','X','Y','Z')) {
            $MountPoint = $Letter + ':'
            if (Test-Path -LiteralPath ($MountPoint + '\')) { continue }
            & "$env:SystemRoot\System32\mountvol.exe" $MountPoint `
                ([string]$Volume.DeviceID) | Out-Null
            if ($LASTEXITCODE -ne 0) { continue }
            $Root = $MountPoint + '\'
            foreach ($Marker in $Requests.Keys) {
                if (Test-Path -LiteralPath (Join-Path $Root $Marker) `
                        -PathType Leaf) {
                    $script:MountedEvidenceRoot = $MountPoint
                    return [pscustomobject]@{
                        Root = $Root
                        Mode = $Requests[$Marker]
                    }
                }
            }
            & "$env:SystemRoot\System32\mountvol.exe" $MountPoint /D |
                Out-Null
            break
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
    Write-Progress 'REPLAY_HASH_CHECK_START'
    Assert-EvidenceHashes @('ib-tcg-dump.exe', 'ibaudit.exe')
    Write-Progress 'REPLAY_HASH_CHECK_PASS'
    $Dump = Join-Path $EvidenceRoot 'ib-tcg-dump.exe'
    $Audit = Join-Path $EvidenceRoot 'ibaudit.exe'
    Write-Progress 'REPLAY_PROBE_START'
    $ProbeCode = Invoke-CapturedProcess -FilePath $Dump -Arguments @('probe') `
        -StdoutPath (Join-Path $EvidenceRoot 'postboot-probe.stdout.txt') `
        -StderrPath (Join-Path $EvidenceRoot 'postboot-probe.stderr.txt')
    if ($ProbeCode -gt 1) { throw "probe exited with code $ProbeCode." }
    Write-Progress ("REPLAY_PROBE_COMPLETE_{0}" -f $ProbeCode)
    $Log = Join-Path $EvidenceRoot 'postboot-tcglog.ib'
    Write-Progress 'REPLAY_CAPTURE_START'
    $CaptureCode = Invoke-CapturedProcess -FilePath $Dump -Arguments @(
        'capture', '--out', $Log, '--full',
        (Join-Path $EvidenceRoot 'postboot-tcglog-full.bin')) `
        -StdoutPath (Join-Path $EvidenceRoot 'postboot-capture.stdout.txt') `
        -StderrPath (Join-Path $EvidenceRoot 'postboot-capture.stderr.txt')
    if ($CaptureCode -ne 0) { throw "capture exited with code $CaptureCode." }
    Write-Progress 'REPLAY_CAPTURE_PASS'
    Write-Progress 'REPLAY_AUDIT_DUMP_START'
    $DumpCode = Invoke-CapturedProcess -FilePath $Audit -Arguments @(
        'dump', '--log', $Log) -StdoutPath (Join-Path $EvidenceRoot `
        'postboot-audit-dump.stdout.txt') -StderrPath (Join-Path `
        $EvidenceRoot 'postboot-audit-dump.stderr.txt')
    if ($DumpCode -ne 0) { throw "audit dump exited with code $DumpCode." }
    Write-Progress 'REPLAY_AUDIT_DUMP_PASS'
    Write-Progress 'REPLAY_ESP_COPY_START'
    Invoke-WithEsp {
        param($Esp)
        Copy-Item -LiteralPath ($Esp + '\EFI\insecure-boot\divergence.ib') `
            -Destination (Join-Path $EvidenceRoot 'divergence.ib') -Force
        Copy-Item -LiteralPath `
            ($Esp + '\EFI\insecure-boot\handoff-status.txt') `
            -Destination (Join-Path $EvidenceRoot 'handoff-status.txt') -Force
    }
    Write-Progress 'REPLAY_ESP_COPY_PASS'
    Write-Progress 'REPLAY_VERIFY_PCRS_START'
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
    Write-Progress 'REPLAY_VERIFY_PCRS_PASS'
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
    Write-Progress ("REQUEST_{0}" -f $Request.Mode.ToUpperInvariant())
    switch ($Request.Mode) {
        'Condition' {
            $ResultName = 'condition-result.json'
            if (Test-Path -LiteralPath $ConditionRebootFlag -PathType Leaf) {
                Remove-Item -LiteralPath $ConditionRebootFlag -Force
                Write-Progress 'CONDITION_REPLAY_TASK_TRIGGERED'
                Install-AuditTask
                Write-Progress 'CONDITION_AUDIT_TASK_ARMED'
                Write-Progress 'CONDITION_AUDIT_LOGON_LAUNCHER_ARMED'
                Write-Progress 'CONDITION_AUDIT_SERVICE_ARMED'
                Write-Progress 'CONDITION_REPLAY_TASK_DISABLED'
                $Result.Status = 'PASS'
                $Result.Message = 'Windows completed QEMU hardware conditioning and proved the fresh replay startup task across a reboot.'
            }
            else {
                & "$env:SystemRoot\System32\powercfg.exe" /hibernate off |
                    Out-Null
                if ($LASTEXITCODE -ne 0) {
                    throw "powercfg /hibernate off exited with code $LASTEXITCODE."
                }
                Write-Progress 'CONDITION_HIBERNATION_DISABLED'
                Install-ReplayStartupTask
                [IO.File]::WriteAllText($ConditionRebootFlag,
                    [DateTime]::UtcNow.ToString('o') + "`r`n",
                    [Text.UTF8Encoding]::new($false))
                Write-Progress 'CONDITION_REPLAY_TASK_ARMED'
                $RestartForConditionValidation = $true
            }
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
    try {
        Write-Progress ('FAIL_' + ($Result.Message -replace '[^A-Za-z0-9_.-]', '_'))
    } catch {}
}
finally {
    if ($null -ne $EvidenceRoot -and -not $RestartForConditionValidation) {
        Write-Progress ("RESULT_{0}" -f $Result.Status)
        $Result.CapturedUtc = [DateTime]::UtcNow.ToString('o')
        $Temporary = Join-Path $EvidenceRoot ($ResultName + '.tmp')
        $Final = Join-Path $EvidenceRoot $ResultName
        $Result | ConvertTo-Json -Depth 4 |
            Set-Content -LiteralPath $Temporary -Encoding utf8
        Move-Item -LiteralPath $Temporary -Destination $Final -Force
    }
    if ($DeleteTask -and -not $RestartForConditionValidation) {
        foreach ($CompletedTaskName in @(
                $TaskName, $ReplayTaskName, $AuditTaskName)) {
            & "$env:SystemRoot\System32\schtasks.exe" /Delete /TN `
                $CompletedTaskName /F 2>$null | Out-Null
        }
        Remove-ItemProperty -LiteralPath $AuditLauncherRegistryPath `
            -Name $AuditLauncherValueName -Force -ErrorAction SilentlyContinue
        & "$env:SystemRoot\System32\sc.exe" delete $AuditServiceName `
            2>$null | Out-Null
    }
    if ($null -ne $MountedEvidenceRoot) {
        & "$env:SystemRoot\System32\mountvol.exe" $MountedEvidenceRoot /D |
            Out-Null
    }
    if ($RestartForConditionValidation) {
        & "$env:SystemRoot\System32\shutdown.exe" /r /t 5 /f /d p:0:0 /c `
            'UEFILoader validating replay startup task' | Out-Null
    }
    else {
        & "$env:SystemRoot\System32\shutdown.exe" /p /f /d p:0:0 | Out-Null
    }
}
