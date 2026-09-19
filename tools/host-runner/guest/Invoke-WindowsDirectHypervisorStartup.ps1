[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RunId = '__DIRECT_VERIFY_RUN_ID__'
$InstallRoot = 'C:\ProgramData\UEFILoaderDirectHypervisor'
$Probe = Join-Path $InstallRoot 'ib-tcg-dump.exe'
$Installer = Join-Path $InstallRoot 'ib-direct-install.exe'
$CompanionDriver = Join-Path $InstallRoot 'hvr1_efi_runtime_companion.sys'
$CompanionService = Join-Path $InstallRoot 'hvr1_runtime_service.exe'
$CompanionDriverServiceName = 'HypersinvHvr1EfiRuntimeCompanion'
$KeyPath = 'C:\ProgramData\UEFILoaderRunner\mailbox.key'
$ResultFramePrefix = 'UEFI2HSINV-DIRECT:'
$StageTwoVariableName = 'uefiloader.direct.stage2.' + $RunId
$ExpectedProcessors = __DIRECT_PROCESSOR_COUNT__
$StdoutPath = Join-Path $InstallRoot 'stdout.txt'
$StderrPath = Join-Path $InstallRoot 'stderr.txt'

function Write-DirectDiagnostic([string]$Phase) {
    if ($Phase -cnotmatch '^[A-Z0-9_]{1,48}$') { return }
    $Port = $null
    try {
        $Port = [IO.Ports.SerialPort]::new('COM2', 115200,
            [IO.Ports.Parity]::None, 8, [IO.Ports.StopBits]::One)
        $Port.Handshake = [IO.Ports.Handshake]::None
        $Port.WriteTimeout = 1000
        $Port.Open()
        $Port.WriteLine("UEFI2HSINV-DIAG:${RunId}:$Phase")
        $Port.BaseStream.Flush()
    }
    catch {}
    finally {
        if ($null -ne $Port) {
            if ($Port.IsOpen) { $Port.Close() }
            $Port.Dispose()
        }
    }
}

function Get-HmacSha256Hex([byte[]]$Key, [string]$Text) {
    $Data = [Text.Encoding]::UTF8.GetBytes($Text)
    $Hmac = [Security.Cryptography.HMACSHA256]::new($Key)
    try {
        return ([BitConverter]::ToString(
            $Hmac.ComputeHash($Data))).Replace('-', '')
    }
    finally {
        $Hmac.Dispose()
        [Array]::Clear($Data, 0, $Data.Length)
    }
}

function Get-CurrentWindowsBuildMetadata {
    $CurrentVersionPath = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    $CurrentVersion = Get-ItemProperty -LiteralPath $CurrentVersionPath `
        -Name CurrentBuild, UBR -ErrorAction Stop
    $BuildText = [string]$CurrentVersion.CurrentBuild
    $UbrText = [string]$CurrentVersion.UBR
    $Build = [uint64]0
    $Ubr = [uint64]0
    if ($BuildText -notmatch '^[0-9]+$' -or
        $UbrText -notmatch '^[0-9]+$' -or
        -not [uint64]::TryParse(
            $BuildText,
            [Globalization.NumberStyles]::None,
            [Globalization.CultureInfo]::InvariantCulture,
            [ref]$Build) -or
        -not [uint64]::TryParse(
            $UbrText,
            [Globalization.NumberStyles]::None,
            [Globalization.CultureInfo]::InvariantCulture,
            [ref]$Ubr) -or
        $Build -gt [uint64]4294967295 -or
        $Ubr -gt [uint64]4294967295) {
        throw 'Windows CurrentBuild/UBR metadata is not an unsigned 32-bit decimal value.'
    }
    return [pscustomobject]@{
        Build = $Build
        Ubr = $Ubr
    }
}

function ConvertTo-BoundedBase64([string]$Text, [string]$Description) {
    $Bytes = [Text.Encoding]::UTF8.GetBytes($Text)
    try {
        if ($Bytes.Length -gt 32768) {
            throw "$Description exceeds the 32 KiB direct-result bound."
        }
        return [Convert]::ToBase64String($Bytes)
    }
    finally { [Array]::Clear($Bytes, 0, $Bytes.Length) }
}

function Invoke-BoundedProcess(
    [string]$FilePath,
    [string[]]$Arguments,
    [string]$OutPath,
    [string]$ErrPath) {
    $Info = [Diagnostics.ProcessStartInfo]::new()
    $Info.FileName = $FilePath
    $Info.Arguments = (($Arguments | ForEach-Object {
        '"' + ([string]$_).Replace('"', '\"') + '"'
    }) -join ' ')
    $Info.WorkingDirectory = $InstallRoot
    $Info.UseShellExecute = $false
    $Info.CreateNoWindow = $true
    $Info.RedirectStandardOutput = $true
    $Info.RedirectStandardError = $true
    $Process = [Diagnostics.Process]::new()
    $Process.StartInfo = $Info
    if (-not $Process.Start()) {
        throw "Failed to start $([IO.Path]::GetFileName($FilePath))."
    }
    $OutTask = $Process.StandardOutput.ReadToEndAsync()
    $ErrTask = $Process.StandardError.ReadToEndAsync()
    if (-not $Process.WaitForExit(120000)) {
        $Process.Refresh()
        if (-not $Process.HasExited) {
            & "$env:SystemRoot\System32\taskkill.exe" /PID $Process.Id /T /F |
                Out-Null
        }
        throw "$([IO.Path]::GetFileName($FilePath)) timed out."
    }
    $Process.WaitForExit()
    $Out = [string]$OutTask.Result
    $Err = [string]$ErrTask.Result
    [IO.File]::WriteAllText($OutPath, $Out,
        [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($ErrPath, $Err,
        [Text.UTF8Encoding]::new($false))
    $ExitCode = [int]$Process.ExitCode
    $Process.Dispose()
    return [pscustomobject]@{
        ExitCode = $ExitCode
        Stdout = $Out
        Stderr = $Err
    }
}

function Invoke-WithEsp([scriptblock]$Operation) {
    $EspDrive = $null
    foreach ($Letter in @('R','S','T','U','V','W','X','Y','Z')) {
        if (-not (Test-Path -LiteralPath ($Letter + ':\'))) {
            $EspDrive = $Letter + ':'
            break
        }
    }
    if ($null -eq $EspDrive) { throw 'No bounded ESP mount letter is free.' }
    try {
        & "$env:SystemRoot\System32\mountvol.exe" $EspDrive /S | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "mountvol /S exited with code $LASTEXITCODE."
        }
        & $Operation $EspDrive
    }
    finally {
        if ($null -ne $EspDrive) {
            & "$env:SystemRoot\System32\mountvol.exe" $EspDrive /D | Out-Null
        }
    }
}

function Get-Hvr1DriverStartDiagnostics([int]$ExitCode) {
    $Lines = [Collections.Generic.List[string]]::new()
    $Lines.Add("HVR1_DRIVER_START_EXIT=$ExitCode")
    try {
        $Signature = Get-AuthenticodeSignature -LiteralPath $CompanionDriver
        $Lines.Add("HVR1_DRIVER_SIGNATURE_STATUS=$($Signature.Status)")
        $Lines.Add("HVR1_DRIVER_SIGNATURE_MESSAGE=$($Signature.StatusMessage)")
        if ($null -ne $Signature.SignerCertificate) {
            $Lines.Add("HVR1_DRIVER_SIGNER_SUBJECT=$($Signature.SignerCertificate.Subject)")
            $Lines.Add("HVR1_DRIVER_SIGNER_THUMBPRINT=$($Signature.SignerCertificate.Thumbprint)")
        }
    }
    catch { $Lines.Add("HVR1_DRIVER_SIGNATURE_DIAGNOSTIC_ERROR=$($_.Exception.Message)") }
    try {
        $Bcd = (& "$env:SystemRoot\System32\bcdedit.exe" /enum `
            '{current}' 2>&1 | Out-String).Trim()
        $Lines.Add('HVR1_BCD_CURRENT_BEGIN')
        $Lines.Add($Bcd)
        $Lines.Add('HVR1_BCD_CURRENT_END')
    }
    catch { $Lines.Add("HVR1_BCD_DIAGNOSTIC_ERROR=$($_.Exception.Message)") }
    try {
        $HvciPath = 'HKLM:\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity'
        $Hvci = Get-ItemProperty -LiteralPath $HvciPath -ErrorAction Stop
        $Lines.Add("HVR1_HVCI_ENABLED=$($Hvci.Enabled)")
        $Lines.Add("HVR1_HVCI_LOCKED=$($Hvci.Locked)")
        $Lines.Add("HVR1_HVCI_WAS_ENABLED_BY=$($Hvci.WasEnabledBy)")
    }
    catch { $Lines.Add("HVR1_HVCI_DIAGNOSTIC_ERROR=$($_.Exception.Message)") }
    try {
        $DeviceGuard = Get-CimInstance `
            -Namespace 'root\Microsoft\Windows\DeviceGuard' `
            -ClassName Win32_DeviceGuard -ErrorAction Stop
        $Lines.Add("HVR1_VBS_STATUS=$($DeviceGuard.VirtualizationBasedSecurityStatus)")
        $Lines.Add('HVR1_SECURITY_SERVICES_CONFIGURED=' +
            (@($DeviceGuard.SecurityServicesConfigured) -join ','))
        $Lines.Add('HVR1_SECURITY_SERVICES_RUNNING=' +
            (@($DeviceGuard.SecurityServicesRunning) -join ','))
    }
    catch { $Lines.Add("HVR1_DEVICE_GUARD_DIAGNOSTIC_ERROR=$($_.Exception.Message)") }
    try {
        $Events = @(Get-WinEvent -FilterHashtable @{
                LogName = 'Microsoft-Windows-CodeIntegrity/Operational'
                StartTime = (Get-Date).AddMinutes(-15)
            } -ErrorAction Stop | Select-Object -First 12)
        $Lines.Add("HVR1_CODE_INTEGRITY_EVENT_COUNT=$($Events.Count)")
        foreach ($Event in $Events) {
            $Message = ([string]$Event.Message -replace '[\r\n]+', ' ').Trim()
            if ($Message.Length -gt 2048) { $Message = $Message.Substring(0, 2048) }
            $Lines.Add(('HVR1_CODE_INTEGRITY_EVENT={0}|{1}|{2}' -f `
                    $Event.TimeCreated.ToUniversalTime().ToString('o'),
                    $Event.Id, $Message))
            if ($Event.Id -in @(3004, 3089)) {
                try {
                    $Xml = [xml]$Event.ToXml()
                    $Activity = [string]$Xml.Event.System.Correlation.ActivityID
                    if (-not [string]::IsNullOrWhiteSpace($Activity)) {
                        $Lines.Add("HVR1_CODE_INTEGRITY_ACTIVITY=$($Event.Id)|$Activity")
                    }
                    foreach ($Data in @($Xml.Event.EventData.Data) |
                            Select-Object -First 24) {
                        $Name = [string]$Data.Name
                        $Value = ([string]$Data.InnerText -replace
                            '[\r\n|]+', ' ').Trim()
                        if ($Value.Length -gt 512) {
                            $Value = $Value.Substring(0, 512)
                        }
                        $Lines.Add(('HVR1_CODE_INTEGRITY_DATA={0}|{1}|{2}={3}' -f `
                                $Event.Id, $Activity, $Name, $Value))
                    }
                }
                catch {
                    $Lines.Add("HVR1_CODE_INTEGRITY_XML_ERROR=$($Event.Id)|$($_.Exception.Message)")
                }
            }
        }
    }
    catch { $Lines.Add("HVR1_CODE_INTEGRITY_DIAGNOSTIC_ERROR=$($_.Exception.Message)") }
    $Text = ($Lines -join "`r`n") + "`r`n"
    $Bytes = [Text.Encoding]::UTF8.GetBytes($Text)
    try {
        if ($Bytes.Length -gt 24576) {
            return $Text.Substring(0, [Math]::Min($Text.Length, 12000)) +
                "`r`nHVR1_DIAGNOSTICS_TRUNCATED=YES`r`n"
        }
        return $Text
    }
    finally { [Array]::Clear($Bytes, 0, $Bytes.Length) }
}

function Publish-DirectResult($Result, [byte[]]$Key) {
    $Canonical = @(
        "SchemaVersion=$($Result.SchemaVersion)"
        "RunId=$($Result.RunId)"
        "Status=$($Result.Status)"
        "MessageBase64=$($Result.MessageBase64)"
        "CompletedUtc=$($Result.CompletedUtc)"
        "StdoutBase64=$($Result.StdoutBase64)"
        "StderrBase64=$($Result.StderrBase64)"
    ) -join "`n"
    $Result.ResponseHmacSha256 = Get-HmacSha256Hex -Key $Key `
        -Text $Canonical
    $Json = $Result | ConvertTo-Json -Compress -Depth 4
    $JsonBytes = [Text.Encoding]::UTF8.GetBytes($Json)
    try { $Encoded = [Convert]::ToBase64String($JsonBytes) }
    finally { [Array]::Clear($JsonBytes, 0, $JsonBytes.Length) }
    if ($Encoded.Length -gt 65536) {
        throw 'The direct-result envelope exceeds 64 KiB.'
    }

    $Frame = $ResultFramePrefix + $Encoded
    $Deadline = [DateTime]::UtcNow.AddSeconds(180)
    do {
        $Port = $null
        try {
            $Port = [IO.Ports.SerialPort]::new('COM2', 115200,
                [IO.Ports.Parity]::None, 8, [IO.Ports.StopBits]::One)
            $Port.Handshake = [IO.Ports.Handshake]::None
            $Port.WriteTimeout = 15000
            $Port.Open()
            for ($Attempt = 0; $Attempt -lt 3; $Attempt++) {
                $Port.WriteLine($Frame)
                $Port.BaseStream.Flush()
                Start-Sleep -Milliseconds 250
            }
            return
        }
        catch { Start-Sleep -Seconds 1 }
        finally {
            if ($null -ne $Port) {
                if ($Port.IsOpen) { $Port.Close() }
                $Port.Dispose()
            }
        }
    } while ([DateTime]::UtcNow -lt $Deadline)
    throw 'COM2 could not publish the direct result within 180 seconds.'
}

function Publish-DirectStageTwoReady([byte[]]$Key) {
    $Marker = [ordered]@{
        SchemaVersion = 1
        RunId = $RunId
        State = 'BOOTNEXT_READY'
        CompletedUtc = [DateTime]::UtcNow.ToString('o')
        HmacSha256 = ''
    }
    $Canonical = @(
        "SchemaVersion=$($Marker.SchemaVersion)"
        "RunId=$($Marker.RunId)"
        "State=$($Marker.State)"
        "CompletedUtc=$($Marker.CompletedUtc)"
    ) -join "`n"
    $Marker.HmacSha256 = Get-HmacSha256Hex -Key $Key -Text $Canonical
    $Json = $Marker | ConvertTo-Json -Compress -Depth 3
    $JsonBytes = [Text.Encoding]::UTF8.GetBytes($Json)
    try { $Encoded = [Convert]::ToBase64String($JsonBytes) }
    finally { [Array]::Clear($JsonBytes, 0, $JsonBytes.Length) }
    if ($Encoded.Length -gt 4096) {
        throw 'The direct stage-two marker exceeds its bound.'
    }
    $Vmtoolsd = Join-Path $env:ProgramFiles `
        'VMware\VMware Tools\vmtoolsd.exe'
    if (-not (Test-Path -LiteralPath $Vmtoolsd -PathType Leaf)) {
        throw 'VMware Tools is missing for direct stage-two publication.'
    }
    $Publish = Invoke-BoundedProcess -FilePath $Vmtoolsd -Arguments @(
        '--cmd', "info-set guestinfo.$StageTwoVariableName $Encoded") `
        -OutPath (Join-Path $InstallRoot 'stage-two-publish.stdout.txt') `
        -ErrPath (Join-Path $InstallRoot 'stage-two-publish.stderr.txt')
    if ($Publish.ExitCode -ne 0) {
        throw "Direct stage-two publication exited with code $($Publish.ExitCode)."
    }
}

$Key = $null
$PublishResult = $true
$Stdout = ''
$Stderr = ''
$Status = 'FAIL'
$Message = 'The direct Windows hypervisor startup probe failed.'
Write-DirectDiagnostic 'STARTUP_ENTERED'
try {
    if ($RunId -cnotmatch
        '^uefi-windows-probe-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$') {
        throw 'The embedded direct verification RunId is invalid.'
    }
    $Key = [IO.File]::ReadAllBytes($KeyPath)
    if ($Key.Length -ne 32) {
        throw 'The installed mailbox key is not exactly 32 bytes.'
    }
    Write-DirectDiagnostic 'MAILBOX_KEY_VALID'
    foreach ($Path in @($Probe, $Installer, $CompanionDriver,
            $CompanionService)) {
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or
            (Get-Item -LiteralPath $Path).Length -lt 1024) {
            throw "The direct startup artifact is invalid: $Path"
        }
    }
    Write-DirectDiagnostic 'ARTIFACTS_VALID'

    $WindowsBuildMetadata = Get-CurrentWindowsBuildMetadata
    $VersionEvidence =
        "WINDOWS_BUILD=$($WindowsBuildMetadata.Build)`r`n" +
        "WINDOWS_UBR=$($WindowsBuildMetadata.Ubr)`r`n"

    $CurrentBcd = (& "$env:SystemRoot\System32\bcdedit.exe" /enum `
        '{current}' 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0 -or
        $CurrentBcd -notmatch '(?im)^testsigning\s+Yes\s*$' -or
        $CurrentBcd -notmatch
            '(?im)^hypervisorlaunchtype\s+Off\s*$') {
        throw 'Direct verification did not boot with test signing enabled and Hyper-V launch disabled.'
    }
    Write-DirectDiagnostic 'BOOT_POLICY_VALID'

    Write-DirectDiagnostic 'PROBE_START'
    $ProbeResult = Invoke-BoundedProcess -FilePath $Probe `
        -Arguments @('probe') -OutPath $StdoutPath -ErrPath $StderrPath
    Write-DirectDiagnostic 'PROBE_COMPLETE'
    $Stdout = $VersionEvidence + $ProbeResult.Stdout
    $Stderr = $ProbeResult.Stderr
    $Lines = @($Stdout -split '\r?\n')
    $CpuLines = @($Lines | Where-Object {
        $_ -cmatch '^UEFI_HYPERVISOR_WINDOWS_CPU:([0-9]+):([0-9]+):PASS$'
    })
    $AllHealthLines = @($Lines | Where-Object {
        $_ -cmatch '^UEFI_HYPERVISOR_WINDOWS_CPU:'
    })
    $Keys = @($CpuLines | ForEach-Object {
        if ($_ -cmatch
            '^UEFI_HYPERVISOR_WINDOWS_CPU:([0-9]+):([0-9]+):PASS$') {
            $Matches[1] + ':' + $Matches[2]
        }
    } | Sort-Object -Unique)
    $Aggregate = @($Lines | Where-Object {
        $_ -ceq 'UEFI_HYPERVISOR_WINDOWS_CPUID:PASS'
    })
    $AbsentCpuLines = @($Lines | Where-Object {
        $_ -cmatch
            '^UEFI_HYPERVISOR_WINDOWS_CPU:[0-9]+:[0-9]+:ABSENT$'
    })
    $AbsentAggregate = @($Lines | Where-Object {
        $_ -ceq 'UEFI_HYPERVISOR_WINDOWS_CPUID:ABSENT'
    })
    if ($AllHealthLines.Count -eq $ExpectedProcessors -and
        $AbsentCpuLines.Count -eq $ExpectedProcessors -and
        $AbsentAggregate.Count -eq 1 -and
        $CpuLines.Count -eq 0 -and $Aggregate.Count -eq 0) {
        Write-DirectDiagnostic 'PROBE_CLASSIFIED_ABSENT'
        $Bcd = (& "$env:SystemRoot\System32\bcdedit.exe" /enum `
            '{current}' 2>&1 | Out-String)
        if ($LASTEXITCODE -ne 0 -or
            $Bcd -notmatch '(?im)^testsigning\s+Yes\s*$' -or
            $Bcd -notmatch '(?im)^hypervisorlaunchtype\s+Off\s*$') {
            throw 'Direct stage two did not preserve the normalized boot policy.'
        }
        Write-DirectDiagnostic 'BOOTNEXT_REARM_START'
        $StageTwoBootNext = Invoke-BoundedProcess -FilePath $Installer `
            -Arguments @('oneshot', '--mode', 'bootnext', '--esp-volume',
                'auto') `
            -OutPath (Join-Path $InstallRoot `
                'stage-two-bootnext.stdout.txt') `
            -ErrPath (Join-Path $InstallRoot `
                'stage-two-bootnext.stderr.txt')
        if ($StageTwoBootNext.ExitCode -ne 0 -or
            $StageTwoBootNext.Stdout -notmatch
                'BootNext set to owned Boot[0-9A-F]{4}; BootOrder was not changed\.') {
            throw 'Direct stage two could not re-arm the owned BootNext entry.'
        }
        Write-DirectDiagnostic 'BOOTNEXT_REARM_COMPLETE'
        Write-DirectDiagnostic 'STAGE2_PUBLISH_START'
        Publish-DirectStageTwoReady -Key $Key
        Write-DirectDiagnostic 'STAGE2_PUBLISH_COMPLETE'
        $PublishResult = $false
        return
    }
    if ($AllHealthLines.Count -ne $ExpectedProcessors -or
        $CpuLines.Count -ne $ExpectedProcessors -or
        $Keys.Count -ne $ExpectedProcessors -or $Aggregate.Count -ne 1) {
        throw 'The EFI hypervisor CPUID contract did not pass on every expected Windows processor.'
    }
    Write-DirectDiagnostic 'PROBE_CLASSIFIED_PASS'
    $Stdout += "`r`nDIRECT_HYPERVISOR_VERIFIED_PROCESSORS=$ExpectedProcessors`r`n"
    [IO.File]::WriteAllText($StdoutPath, $Stdout,
        [Text.UTF8Encoding]::new($false))

    Write-DirectDiagnostic 'DRIVER_START_BEGIN'
    & "$env:SystemRoot\System32\sc.exe" start $CompanionDriverServiceName |
        Out-Null
    if ($LASTEXITCODE -ne 0) {
        $DriverStartExitCode = $LASTEXITCODE
        $Stderr += Get-Hvr1DriverStartDiagnostics $DriverStartExitCode
        [IO.File]::WriteAllText($StderrPath, $Stderr,
            [Text.UTF8Encoding]::new($false))
        throw "HVR1 companion driver start exited with code $DriverStartExitCode."
    }
    Write-DirectDiagnostic 'DRIVER_START_COMPLETE'
    Write-DirectDiagnostic 'SELFTEST_START'
    $SelfTest = Invoke-BoundedProcess -FilePath $CompanionService `
        -Arguments @('selftest-shutdown') `
        -OutPath (Join-Path $InstallRoot 'hvr1-selftest-stdout.txt') `
        -ErrPath (Join-Path $InstallRoot 'hvr1-selftest-stderr.txt')
    $Stdout += "`r`n" + $SelfTest.Stdout
    $Stderr += $SelfTest.Stderr
    [IO.File]::WriteAllText($StdoutPath, $Stdout,
        [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($StderrPath, $Stderr,
        [Text.UTF8Encoding]::new($false))
    if ($SelfTest.ExitCode -ne 0) {
        throw "HVR1 authenticated shutdown self-test exited with code $($SelfTest.ExitCode)."
    }
    Write-DirectDiagnostic 'SELFTEST_COMPLETE'
    foreach ($Marker in @(
            'HVR1_CRASH_RECOVERY_PRIOR_BOOT=PASS',
            'HVR1_POWER_REQUEST_HELD=PASS',
            'HVR1_AUTH_INVALID_TAG_REJECTED=PASS',
            'HVR1_AUTH_VALID_SHUTDOWN=PASS',
            'HVR1_AUTH_REPLAY_REJECTED=PASS',
            'HVR1_POWER_REQUEST_RELEASED_AFTER_OFFLINE=PASS')) {
        if ([regex]::Matches($SelfTest.Stdout,
                '(?m)^' + [regex]::Escape($Marker) + '\r?$').Count -ne 1) {
            throw "HVR1 authenticated shutdown proof is missing: $Marker"
        }
    }
    if ([regex]::Matches($SelfTest.Stdout,
            ("(?m)^HVR1_AUTH_SELFTEST=PASS cpus={0}/{0} generation=[1-9][0-9]*\r?$" -f
                $ExpectedProcessors)).Count -ne 1) {
        throw "HVR1 self-test did not prove all $ExpectedProcessors processors detached."
    }
    $PostDetachProbe = Invoke-BoundedProcess -FilePath $Probe `
        -Arguments @('probe') `
        -OutPath (Join-Path $InstallRoot 'hvr1-post-detach-stdout.txt') `
        -ErrPath (Join-Path $InstallRoot 'hvr1-post-detach-stderr.txt')
    $PostDetachLines = @($PostDetachProbe.Stdout -split '\r?\n')
    $PostDetachCpuLines = @($PostDetachLines | Where-Object {
        $_ -cmatch '^UEFI_HYPERVISOR_WINDOWS_CPU:'
    })
    $PostDetachAbsentCpuLines = @($PostDetachLines | Where-Object {
        $_ -cmatch
            '^UEFI_HYPERVISOR_WINDOWS_CPU:[0-9]+:[0-9]+:ABSENT$'
    })
    $PostDetachAbsentAggregate = @($PostDetachLines | Where-Object {
        $_ -ceq 'UEFI_HYPERVISOR_WINDOWS_CPUID:ABSENT'
    })
    $Stdout += "`r`n" + $PostDetachProbe.Stdout
    $Stderr += $PostDetachProbe.Stderr
    [IO.File]::WriteAllText($StdoutPath, $Stdout,
        [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($StderrPath, $Stderr,
        [Text.UTF8Encoding]::new($false))
    if ($PostDetachCpuLines.Count -ne $ExpectedProcessors -or
        $PostDetachAbsentCpuLines.Count -ne $ExpectedProcessors -or
        $PostDetachAbsentAggregate.Count -ne 1) {
        throw ("The EFI hypervisor CPUID health contract remained visible " +
            "after detach (exit=$($PostDetachProbe.ExitCode), " +
            "cpus=$($PostDetachCpuLines.Count), " +
            "absent=$($PostDetachAbsentCpuLines.Count), " +
            "aggregate_absent=$($PostDetachAbsentAggregate.Count)).")
    }
    $Stdout += "HVR1_POST_DETACH_CPUID_ABSENT=PASS`r`n"
    & "$env:SystemRoot\System32\sc.exe" stop $CompanionDriverServiceName |
        Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "HVR1 companion driver stop exited with code $LASTEXITCODE."
    }
    & "$env:SystemRoot\System32\sc.exe" delete $CompanionDriverServiceName |
        Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "HVR1 companion driver delete exited with code $LASTEXITCODE."
    }
    $ThumbprintPath = Join-Path $InstallRoot 'hvr1-test.thumbprint'
    $Thumbprint = (Get-Content -Raw -LiteralPath $ThumbprintPath).Trim()
    if ($Thumbprint -cnotmatch '^[0-9A-F]{40}$') {
        throw 'The disposable HVR1 certificate thumbprint is invalid.'
    }
    foreach ($Store in @('Root', 'TrustedPublisher')) {
        $StorePath = "Cert:\LocalMachine\$Store\$Thumbprint"
        if (-not (Test-Path -LiteralPath $StorePath)) {
            throw "The disposable HVR1 certificate is missing from $Store."
        }
        Remove-Item -LiteralPath $StorePath -Force
    }
    $TestSigningBaseline = (Get-Content -Raw -LiteralPath (Join-Path `
            $InstallRoot 'testsigning-was-enabled.txt')).Trim()
    if ($TestSigningBaseline -ceq '0') {
        & "$env:SystemRoot\System32\bcdedit.exe" /set '{current}' `
            testsigning off | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "bcdedit /set testsigning off exited with code $LASTEXITCODE."
        }
    }
    elseif ($TestSigningBaseline -cne '1') {
        throw 'The disposable test-signing baseline is invalid.'
    }
    $HypervisorLaunchBaseline = (Get-Content -Raw -LiteralPath (Join-Path `
            $InstallRoot 'hypervisorlaunchtype-baseline.txt')).Trim()
    if ($HypervisorLaunchBaseline -ceq 'ABSENT') {
        & "$env:SystemRoot\System32\bcdedit.exe" /deletevalue '{current}' `
            hypervisorlaunchtype | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "bcdedit /deletevalue hypervisorlaunchtype exited with code $LASTEXITCODE."
        }
    }
    elseif ($HypervisorLaunchBaseline -ceq 'AUTO') {
        & "$env:SystemRoot\System32\bcdedit.exe" /set '{current}' `
            hypervisorlaunchtype auto | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "bcdedit /set hypervisorlaunchtype auto exited with code $LASTEXITCODE."
        }
    }
    elseif ($HypervisorLaunchBaseline -cne 'OFF') {
        throw 'The disposable Hyper-V launch baseline is invalid.'
    }
    $BitLockerBaseline = (Get-Content -Raw -LiteralPath (Join-Path `
            $InstallRoot 'bitlocker-was-protected.txt')).Trim()
    if ($BitLockerBaseline -ceq '1') {
        & "$env:SystemRoot\System32\manage-bde.exe" -protectors -enable C: |
            Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "manage-bde protector restoration exited with code $LASTEXITCODE."
        }
        $Stdout += "HVR1_BITLOCKER_PROTECTORS_RESTORED=PASS`r`n"
    }
    elseif ($BitLockerBaseline -cne '0') {
        throw 'The disposable BitLocker protection baseline is invalid.'
    }
    $Stdout += "HVR1_COMPANION_UNLOADED=PASS`r`n"
    [IO.File]::WriteAllText($StdoutPath, $Stdout,
        [Text.UTF8Encoding]::new($false))

    $Cleanup = Invoke-BoundedProcess -FilePath $Installer -Arguments @(
        'oneshot', '--mode', 'off', '--esp-volume', 'auto') `
        -OutPath (Join-Path $InstallRoot 'cleanup-stdout.txt') `
        -ErrPath (Join-Path $InstallRoot 'cleanup-stderr.txt')
    if ($Cleanup.ExitCode -ne 0) {
        throw "Direct BootNext cleanup exited with code $($Cleanup.ExitCode)."
    }
    Invoke-WithEsp {
        param($Esp)
        $Stage = $Esp + '\EFI\insecure-boot'
        $ExpectedHashPath = Join-Path $Stage 'direct-bootmgfw-before.sha256'
        if (-not (Test-Path -LiteralPath $ExpectedHashPath -PathType Leaf)) {
            throw 'Direct EFI boot-manager provenance is missing.'
        }
        $ExpectedHash = (Get-Content -Raw `
            -LiteralPath $ExpectedHashPath).Trim()
        $BootManager = $Esp + '\EFI\Microsoft\Boot\bootmgfw.efi'
        $ActualHash = (Get-FileHash -LiteralPath $BootManager `
            -Algorithm SHA256).Hash
        if ($ExpectedHash -cne $ActualHash) {
            throw 'The direct EFI cycle changed bootmgfw.efi.'
        }
        Remove-Item -LiteralPath $Stage -Recurse -Force
    }
    $Status = 'PASS'
    $Message = "Windows verified the EFI hypervisor on $ExpectedProcessors processors."
}
catch {
    Write-DirectDiagnostic 'STARTUP_CAUGHT_FAILURE'
    $Message = $_.Exception.Message
    if (Test-Path -LiteralPath $StdoutPath -PathType Leaf) {
        $Stdout = Get-Content -Raw -LiteralPath $StdoutPath
    }
    if (Test-Path -LiteralPath $StderrPath -PathType Leaf) {
        $Stderr = Get-Content -Raw -LiteralPath $StderrPath
    }
}
finally {
    if ($null -ne $Key) {
        try {
            if ($PublishResult) {
                $Result = [ordered]@{
                    SchemaVersion = 1
                    RunId = $RunId
                    Status = $Status
                    MessageBase64 = ConvertTo-BoundedBase64 $Message 'Message'
                    CompletedUtc = [DateTime]::UtcNow.ToString('o')
                    StdoutBase64 = ConvertTo-BoundedBase64 $Stdout 'Probe stdout'
                    StderrBase64 = ConvertTo-BoundedBase64 $Stderr 'Probe stderr'
                    ResponseHmacSha256 = ''
                }
                Publish-DirectResult -Result $Result -Key $Key
            }
        }
        finally { [Array]::Clear($Key, 0, $Key.Length) }
    }
}
