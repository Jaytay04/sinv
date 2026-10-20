[CmdletBinding()]
param(
    [switch]$SerialPublishOnly,
    [string]$FrameBase64
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$PrivateRoot = 'C:\ProgramData\HvGpuHalVmTest'
$MetadataPath = Join-Path $PrivateRoot 'metadata.json'
$ResultPath = Join-Path $PrivateRoot 'result.json'
$ResultHmacPath = Join-Path $PrivateRoot 'result.hmac.sha256'
$StdoutPath = Join-Path $PrivateRoot 'stdout.txt'
$StderrPath = Join-Path $PrivateRoot 'stderr.txt'
$MailboxKeyPath = 'C:\ProgramData\UEFILoaderRunner\mailbox.key'

function Write-Bytes {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][byte[]]$Bytes
    )
    [IO.File]::WriteAllBytes($Path, $Bytes)
}

function Write-Utf8NoBom {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Text
    )
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

function Quote-ProcessArgument {
    param([Parameter(Mandatory = $true)][string]$Value)
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $Builder = [Text.StringBuilder]::new()
    [void]$Builder.Append('"')
    $Backslashes = 0
    foreach ($Character in $Value.ToCharArray()) {
        if ($Character -eq [char]92) {
            $Backslashes++
            continue
        }
        if ($Character -eq [char]34) {
            [void]$Builder.Append(('\' * (2 * $Backslashes + 1)))
            [void]$Builder.Append('"')
            $Backslashes = 0
            continue
        }
        if ($Backslashes -gt 0) {
            [void]$Builder.Append(('\' * $Backslashes))
            $Backslashes = 0
        }
        [void]$Builder.Append($Character)
    }
    if ($Backslashes -gt 0) { [void]$Builder.Append(('\' * (2 * $Backslashes))) }
    [void]$Builder.Append('"')
    return $Builder.ToString()
}

function Invoke-CapturedProcess {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][int]$TimeoutSeconds,
        [Parameter(Mandatory = $true)][string]$Label
    )
    $Info = [Diagnostics.ProcessStartInfo]::new()
    $Info.FileName = $FilePath
    $Info.Arguments = (($Arguments | ForEach-Object {
        Quote-ProcessArgument ([string]$_)
    }) -join ' ')
    $Info.WorkingDirectory = Split-Path -Parent $FilePath
    $Info.UseShellExecute = $false
    $Info.CreateNoWindow = $true
    $Info.RedirectStandardOutput = $true
    $Info.RedirectStandardError = $true
    $Process = [Diagnostics.Process]::new()
    $Process.StartInfo = $Info
    if (-not $Process.Start()) { throw "$Label did not start." }
    $StdoutTask = $Process.StandardOutput.ReadToEndAsync()
    $StderrTask = $Process.StandardError.ReadToEndAsync()
    if (-not $Process.WaitForExit($TimeoutSeconds * 1000)) {
        try { $Process.Kill() } catch {}
        throw "$Label exceeded its ${TimeoutSeconds}-second deadline."
    }
    $Process.WaitForExit()
    $Captured = [pscustomobject]@{
        ExitCode = [int]$Process.ExitCode
        Stdout = [string]$StdoutTask.Result
        Stderr = [string]$StderrTask.Result
    }
    $Process.Dispose()
    return $Captured
}

function Publish-SerialFrame {
    param([Parameter(Mandatory = $true)][string]$Frame)
    $Port = [IO.Ports.SerialPort]::new(
        'COM2', 115200, [IO.Ports.Parity]::None, 8,
        [IO.Ports.StopBits]::One)
    $Port.Handshake = [IO.Ports.Handshake]::None
    $Port.DtrEnable = $false
    $Port.RtsEnable = $false
    $Port.WriteTimeout = 5000
    $Port.ReadTimeout = 1000
    try {
        $Port.Open()
        for ($Index = 0; $Index -lt 3; $Index++) {
            $Port.WriteLine($Frame)
            $Port.BaseStream.Flush()
            Start-Sleep -Milliseconds 200
        }
    }
    finally {
        if ($Port.IsOpen) { $Port.Close() }
        $Port.Dispose()
    }
}

try {
    if ([Security.Principal.WindowsIdentity]::GetCurrent().User.Value -cne 'S-1-5-18') {
        exit 2
    }
    $Bios = Get-Item -LiteralPath 'HKLM:\HARDWARE\DESCRIPTION\System\BIOS'
    if ([string]$Bios.GetValue('SystemManufacturer') -notmatch '(?i)^VMware') {
        exit 2
    }
}
catch {
    exit 2
}

if ($SerialPublishOnly) {
    try {
        if ([string]::IsNullOrWhiteSpace($FrameBase64) -or
            $FrameBase64.Length -gt 30000) {
            throw 'The authenticated result frame is absent or exceeds its command-line bound.'
        }
        $FrameBytes = [Convert]::FromBase64String($FrameBase64)
        $Frame = [Text.Encoding]::UTF8.GetString($FrameBytes)
        [Array]::Clear($FrameBytes, 0, $FrameBytes.Length)
        if ($Frame -notmatch '^GPU_HAL_WINDOWS:RESULT:[A-Za-z0-9+/=]+\.[0-9A-F]{64}$') {
            throw 'The authenticated result frame has invalid framing.'
        }
        Publish-SerialFrame -Frame $Frame
        exit 0
    }
    catch {
        exit 1
    }
}

if (-not (Test-Path -LiteralPath $PrivateRoot -PathType Container)) {
    exit 2
}
if (Test-Path -LiteralPath $ResultPath -PathType Leaf) {
    try {
        & (Join-Path $env:SystemRoot 'System32\schtasks.exe') `
            /Delete /TN 'HvGpuHalVmVerify' /F | Out-Null
    } catch {}
    exit 3
}

$OutputBuilder = [Text.StringBuilder]::new()
$ErrorBuilder = [Text.StringBuilder]::new()
$Result = [ordered]@{
    SchemaVersion = 1
    RunId = $null
    Status = 'FAIL'
    CompletedUtc = $null
    CurrentBuild = $null
    Ubr = $null
    ExpectedCpus = $null
    CpuPassCount = 0
    HealthPassed = $false
    BootManagerUnchanged = $false
    Stdout = ''
    Stderr = ''
    DisplayVersion = $null
    SecureBoot = $null
    ValidationProfile = '__GPU_VALIDATION_PROFILE__'
    ExpectedSecureBoot = [bool]::Parse('__GPU_EXPECTED_SECURE_BOOT__')
    ExpectedCertificateDerPresent = $null
    ExpectedCertificateSha256 = $null
    CertificateDbInspection = $null
    HandoffStatus = ''
    HandoffStatusSha256 = $null
    HandoffStatusBytes = 0
    HandoffStatusCapturedBytes = 0
    HandoffStatusTooLarge = $false
    PayloadDiagnostic = ''
    PayloadDiagnosticSha256 = $null
    PayloadDiagnosticBytes = 0
    PayloadDiagnosticCapturedBytes = 0
    PayloadDiagnosticTooLarge = $false
    DiagnosticTextEncoding = 'ISO-8859-1 byte-preserving'
    BcdCurrent = $null
    BitLockerStatus = $null
    GraphicsInventory = @()
    IntelAdapterPresent = $false
    NotSupportedExpectedWhenNoIntel = $true
    HalRootNullMarkerObserved = $null
    HalRootNullMarkerEvidence = 'separate preboot serial evidence'
    HealthQueryExitCode = $null
    BootManagerSha256Before = $null
    BootManagerSha256After = $null
    Message = 'GPU HAL Windows startup verification did not complete.'
}

function Get-SystemVersionInfo {
    $CurrentVersion = Get-ItemProperty `
        -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    $BuildValue = if ($CurrentVersion.PSObject.Properties['CurrentBuildNumber']) {
        [string]$CurrentVersion.CurrentBuildNumber
    } else {
        [string]$CurrentVersion.CurrentBuild
    }
    return [pscustomobject]@{
        Build = [int]$BuildValue
        Ubr = [int]$CurrentVersion.UBR
        DisplayVersion = [string]$CurrentVersion.DisplayVersion
    }
}

function Get-SecureBootValue {
    try {
        $State = Get-Item -LiteralPath `
            'HKLM:\SYSTEM\CurrentControlSet\Control\SecureBoot\State' -ErrorAction Stop
        $Value = $State.GetValue('UEFISecureBootEnabled', $null)
        if ($null -eq $Value -or $Value -notin @(0, 1)) { return $null }
        return [bool]$Value
    }
    catch { return $null }
}

function Get-BcdCurrent {
    $BcdEdit = Join-Path $env:SystemRoot 'System32\bcdedit.exe'
    $Captured = Invoke-CapturedProcess -FilePath $BcdEdit `
        -Arguments @('/enum', '{current}') -TimeoutSeconds 30 `
        -Label 'bcdedit current-entry readback'
    if ($Captured.ExitCode -ne 0) {
        throw 'Could not capture the current Windows BCD entry.'
    }
    return $Captured.Stdout
}

function Invoke-WithEsp {
    param([Parameter(Mandatory = $true)][scriptblock]$Operation)
    $EspDrive = $null
    foreach ($Letter in @('R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z')) {
        if (-not (Test-Path -LiteralPath ($Letter + ':\'))) {
            $EspDrive = $Letter + ':'
            break
        }
    }
    if ($null -eq $EspDrive) { throw 'No bounded ESP mount letter is available.' }
    try {
        $Mount = Invoke-CapturedProcess -FilePath `
            (Join-Path $env:SystemRoot 'System32\mountvol.exe') `
            -Arguments @($EspDrive, '/S') -TimeoutSeconds 30 -Label 'mountvol /S'
        if ($Mount.ExitCode -ne 0) { throw 'mountvol /S failed.' }
        & $Operation $EspDrive
    }
    finally {
        $Unmount = Invoke-CapturedProcess -FilePath `
            (Join-Path $env:SystemRoot 'System32\mountvol.exe') `
            -Arguments @($EspDrive, '/D') -TimeoutSeconds 30 -Label 'mountvol /D'
        if ($Unmount.ExitCode -ne 0) { throw 'mountvol /D failed to release the ESP.' }
    }
}

function Get-BootManagerHash {
    return Invoke-WithEsp {
        param($EspDrive)
        $Path = $EspDrive + '\EFI\Microsoft\Boot\bootmgfw.efi'
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
            throw 'Windows boot manager is absent from the mounted ESP.'
        }
        (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    }
}

function Get-SignedNativeLaunchDiagnostics {
    return Invoke-WithEsp {
        param($EspDrive)
        $StageRoot = $EspDrive + '\EFI\insecure-boot'
        $Evidence = [ordered]@{}
        foreach ($Name in @('handoff-status.txt', 'payload-diagnostic.txt')) {
            $Path = Join-Path $StageRoot $Name
            $Bytes = [byte[]]@()
            $Present = Test-Path -LiteralPath $Path -PathType Leaf
            $OriginalLength = 0
            if ($Present) {
                $Stream = [IO.File]::Open($Path, [IO.FileMode]::Open,
                    [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
                try {
                    $OriginalLength = [long]$Stream.Length
                    $ReadCount = [int][Math]::Min(4097, $Stream.Length)
                    if ($ReadCount -gt 0) {
                        $Bytes = [byte[]]::new($ReadCount)
                        $Offset = 0
                        while ($Offset -lt $ReadCount) {
                            $Count = $Stream.Read($Bytes, $Offset, $ReadCount - $Offset)
                            if ($Count -le 0) { break }
                            $Offset += $Count
                        }
                        $ReadBytes = [byte[]]::new($Offset)
                        if ($Offset -gt 0) { [Array]::Copy($Bytes, $ReadBytes, $Offset) }
                        [Array]::Clear($Bytes, 0, $Bytes.Length)
                        $Bytes = $ReadBytes
                    }
                }
                finally { $Stream.Dispose() }
            }
            $TooLarge = $OriginalLength -gt 4096
            if ($Bytes.Length -gt 4096) {
                $Captured = [byte[]]::new(4096)
                [Array]::Copy($Bytes, $Captured, 4096)
                [Array]::Clear($Bytes, 0, $Bytes.Length)
                $Bytes = $Captured
            }
            $RawText = ''
            if ($Bytes.Length -gt 0) {
                $RawText = [Text.Encoding]::GetEncoding(28591).GetString($Bytes)
            }
            $Sha256 = $null
            if ($Bytes.Length -gt 0) {
                $Sha = [Security.Cryptography.SHA256]::Create()
                try { $Sha256 = ([BitConverter]::ToString($Sha.ComputeHash($Bytes))).Replace('-', '') }
                finally { $Sha.Dispose() }
            }
            $Evidence[$Name] = [pscustomobject]@{
                Present = $Present
                TooLarge = $TooLarge
                Bytes = $OriginalLength
                CapturedBytes = $Bytes.Length
                Sha256 = $Sha256
                Text = $RawText
            }
        }
        [pscustomobject]@{
            Handoff = $Evidence['handoff-status.txt']
            Payload = $Evidence['payload-diagnostic.txt']
        }
    }
}

function Get-BitLockerStatusText {
    $ManageBde = Join-Path $env:SystemRoot 'System32\manage-bde.exe'
    $Captured = Invoke-CapturedProcess -FilePath $ManageBde `
        -Arguments @('-status', 'C:') -TimeoutSeconds 30 -Label 'manage-bde status'
    if ($Captured.ExitCode -ne 0) {
        throw "manage-bde status exited with code $($Captured.ExitCode)."
    }
    return ($Captured.Stdout + $Captured.Stderr).Trim()
}

function Get-GraphicsInventory {
    # Registry enumeration does not attest device ownership or driver activity.
    $PciRoot = 'HKLM:\SYSTEM\CurrentControlSet\Enum\PCI'
    foreach ($Device in @(Get-ChildItem -LiteralPath $PciRoot)) {
        foreach ($Instance in @(Get-ChildItem -LiteralPath $Device.PSPath)) {
            if ([string]$Instance.GetValue('ClassGUID') -ine
                '{4d36e968-e325-11ce-bfc1-08002be10318}') { continue }
            $Driver = [string]$Instance.GetValue('Driver')
            $DriverKey = $null
            if ($Driver -match '^\{[0-9a-fA-F-]{36}\}\\[0-9]{4}$') {
                $DriverKey = Get-Item -LiteralPath (
                    'HKLM:\SYSTEM\CurrentControlSet\Control\Class\' + $Driver) `
                    -ErrorAction SilentlyContinue
            }
            [pscustomobject]@{
                Name = if ($null -ne $DriverKey) {
                    [string]$DriverKey.GetValue('DriverDesc')
                } else { [string]$Instance.GetValue('DeviceDesc') }
                PnpDeviceId = 'PCI\' + $Device.PSChildName + '\' + $Instance.PSChildName
                DriverVersion = if ($null -ne $DriverKey) {
                    [string]$DriverKey.GetValue('DriverVersion')
                } else { $null }
                Status = 'RegistryEnumerated'
                VideoProcessor = $null
            }
        }
    }
}

function Write-ResultAndHmac {
    param([Parameter(Mandatory = $true)]$Value)
    $Value.CompletedUtc = [DateTime]::UtcNow.ToString('o')
    $Value.Stdout = $OutputBuilder.ToString()
    $Value.Stderr = $ErrorBuilder.ToString()
    $Json = $Value | ConvertTo-Json -Compress -Depth 10
    $PayloadBytes = [Text.UTF8Encoding]::new($false).GetBytes($Json)
    Write-Bytes -Path $ResultPath -Bytes $PayloadBytes
    Write-Bytes -Path $StdoutPath -Bytes ([Text.UTF8Encoding]::new($false).GetBytes($Value.Stdout))
    Write-Bytes -Path $StderrPath -Bytes ([Text.UTF8Encoding]::new($false).GetBytes($Value.Stderr))

    $Key = $null
    $Hmac = $null
    $Digest = $null
    try {
        $Key = [IO.File]::ReadAllBytes($MailboxKeyPath)
        if ($Key.Length -ne 32) {
            throw 'The fixed GuestInfo mailbox key must contain exactly 32 bytes.'
        }
        $Hmac = [Security.Cryptography.HMACSHA256]::new($Key)
        $Digest = $Hmac.ComputeHash($PayloadBytes)
        $Hex = ([BitConverter]::ToString($Digest)).Replace('-', '')
        Write-Utf8NoBom -Path $ResultHmacPath -Text ($Hex + "`n")
        return [pscustomobject]@{
            PayloadBytes = $PayloadBytes
            HmacHex = $Hex
            Frame = 'GPU_HAL_WINDOWS:RESULT:{0}.{1}' -f `
                [Convert]::ToBase64String($PayloadBytes), $Hex
        }
    }
    finally {
        if ($null -ne $Key) { [Array]::Clear($Key, 0, $Key.Length) }
        if ($null -ne $Digest) { [Array]::Clear($Digest, 0, $Digest.Length) }
        if ($null -ne $Hmac) { $Hmac.Dispose() }
    }
}

function Publish-FrameBounded {
    param([Parameter(Mandatory = $true)][string]$Frame)
    $FrameBytes = [Text.Encoding]::UTF8.GetBytes($Frame)
    $FrameBase64 = [Convert]::ToBase64String($FrameBytes)
    [Array]::Clear($FrameBytes, 0, $FrameBytes.Length)
    $PowerShellPath = Join-Path $env:SystemRoot `
        'System32\WindowsPowerShell\v1.0\powershell.exe'
    $Info = [Diagnostics.ProcessStartInfo]::new()
    $Info.FileName = $PowerShellPath
    $Info.Arguments = '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File {0} -SerialPublishOnly -FrameBase64 {1}' -f `
        (Quote-ProcessArgument $PSCommandPath), (Quote-ProcessArgument $FrameBase64)
    $Info.UseShellExecute = $false
    $Info.CreateNoWindow = $true
    $Info.RedirectStandardOutput = $true
    $Info.RedirectStandardError = $true
    $Process = [Diagnostics.Process]::new()
    $Process.StartInfo = $Info
    $Clock = [Diagnostics.Stopwatch]::StartNew()
    if (-not $Process.Start()) { throw 'COM2 publisher did not start.' }
    $OutTask = $Process.StandardOutput.ReadToEndAsync()
    $ErrTask = $Process.StandardError.ReadToEndAsync()
    if (-not $Process.WaitForExit(60000)) {
        try { $Process.Kill() } catch {}
        throw 'COM2 result publication exceeded its 60-second deadline.'
    }
    $Process.WaitForExit()
    $Clock.Stop()
    $ExitCode = [int]$Process.ExitCode
    $Process.Dispose()
    if ($Clock.Elapsed.TotalSeconds -gt 60) {
        throw 'COM2 result publication exceeded its 60-second deadline.'
    }
    if ($ExitCode -ne 0) {
        $Detail = ([string]$ErrTask.Result).Trim()
        throw "COM2 result publication failed (exit $ExitCode): $Detail"
    }
    return $true
}

try {
    if (-not (Test-Path -LiteralPath $MetadataPath -PathType Leaf)) {
        throw 'Private GPU VM metadata is missing.'
    }
    $Metadata = Get-Content -Raw -LiteralPath $MetadataPath | ConvertFrom-Json
    if ($Metadata.SchemaVersion -ne 1 -or
        [string]$Metadata.PrivateRoot -cne $PrivateRoot -or
        [string]$Metadata.RunId -notmatch '^[A-Za-z0-9._-]{1,96}$') {
        throw 'The private GPU VM metadata failed its schema or path validation.'
    }
    $Result.RunId = [string]$Metadata.RunId
    $Result.ExpectedCpus = [int]$Metadata.ExpectedCpus
    $Result.ExpectedBuild = [int]$Metadata.ExpectedBuild
    $Result.ExpectedUbr = [int]$Metadata.ExpectedUbr
    $Result.BootManagerSha256Before = [string]$Metadata.BootManagerSha256Before
    $Result.DisplayVersion = [string]$Metadata.DisplayVersion
    if ([string]$Metadata.ValidationProfile -cne [string]$Result.ValidationProfile -or
        [bool]$Metadata.ExpectedSecureBoot -ne [bool]$Result.ExpectedSecureBoot) {
        throw 'The startup metadata profile does not match the embedded validation profile.'
    }
    if ($Result.ValidationProfile -ceq 'SignedNative') {
        $Result.ExpectedCertificateDerPresent = [bool]$Metadata.ExpectedCertificateDerPresent
        $Result.ExpectedCertificateSha256 = [string]$Metadata.ExpectedCertificateSha256
        $Result.CertificateDbInspection = [string]$Metadata.CertificateDbInspection
        if ($Result.ExpectedCertificateSha256 -cnotmatch '^[0-9A-F]{64}$' -or
            $Result.CertificateDbInspection -cne 'BOUNDED_RAW_DER_SUBSEQUENCE_ONLY') {
            throw 'The signed-native startup metadata lacks valid bounded db certificate evidence.'
        }
        $LaunchDiagnostics = Get-SignedNativeLaunchDiagnostics
        $Result.HandoffStatus = [string]$LaunchDiagnostics.Handoff.Text
        $Result.HandoffStatusSha256 = [string]$LaunchDiagnostics.Handoff.Sha256
        $Result.HandoffStatusBytes = [long]$LaunchDiagnostics.Handoff.Bytes
        $Result.HandoffStatusCapturedBytes = [int]$LaunchDiagnostics.Handoff.CapturedBytes
        $Result.HandoffStatusTooLarge = [bool]$LaunchDiagnostics.Handoff.TooLarge
        $Result.PayloadDiagnostic = [string]$LaunchDiagnostics.Payload.Text
        $Result.PayloadDiagnosticSha256 = [string]$LaunchDiagnostics.Payload.Sha256
        $Result.PayloadDiagnosticBytes = [long]$LaunchDiagnostics.Payload.Bytes
        $Result.PayloadDiagnosticCapturedBytes = [int]$LaunchDiagnostics.Payload.CapturedBytes
        $Result.PayloadDiagnosticTooLarge = [bool]$LaunchDiagnostics.Payload.TooLarge
        if (-not $LaunchDiagnostics.Handoff.Present -or
            -not $LaunchDiagnostics.Payload.Present -or
            $LaunchDiagnostics.Handoff.TooLarge -or
            $LaunchDiagnostics.Payload.TooLarge) {
            throw 'Signed-native ESP launch diagnostics are missing or exceed the 4 KiB evidence bound.'
        }
    }

    $Version = Get-SystemVersionInfo
    $Result.CurrentBuild = $Version.Build
    $Result.Ubr = $Version.Ubr
    $Result.DisplayVersion = $Version.DisplayVersion
    $VersionMatched = $Version.Build -eq [int]$Metadata.ExpectedBuild -and
        $Version.Ubr -eq [int]$Metadata.ExpectedUbr
    if (-not $VersionMatched) {
        throw "Windows booted as $($Version.Build).$($Version.Ubr), expected $($Metadata.ExpectedBuild).$($Metadata.ExpectedUbr)."
    }

    $SecureBoot = Get-SecureBootValue
    $Result.SecureBoot = $SecureBoot
    if ($null -eq $SecureBoot -or $SecureBoot -ne [bool]$Result.ExpectedSecureBoot) {
        throw "The test VM Secure Boot state did not match the $($Result.ValidationProfile) profile expectation."
    }
    if ($Result.ValidationProfile -ceq 'SignedNative' -and
        -not $Result.ExpectedCertificateDerPresent) {
        throw 'The expected signed-native certificate DER was not observed in Secure Boot db.'
    }

    $Bcd = Get-BcdCurrent
    $Result.BcdCurrent = $Bcd
    $Result.BitLockerStatus = Get-BitLockerStatusText

    $Executable = Join-Path $PrivateRoot 'ib-tcg-dump.exe'
    if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
        throw 'The private ib-tcg-dump.exe is absent.'
    }
    $Health = Invoke-CapturedProcess -FilePath $Executable `
        -Arguments @('health') -TimeoutSeconds 60 -Label 'ib-tcg-dump health'
    $OutputBuilder.AppendLine($Health.Stdout) | Out-Null
    $ErrorBuilder.AppendLine($Health.Stderr) | Out-Null
    $Result.HealthQueryExitCode = $Health.ExitCode
    if ($Health.ExitCode -ne 0) {
        throw "ib-tcg-dump health failed with exit code: $($Health.ExitCode)."
    }

    $HealthLines = [regex]::Matches(
        $Health.Stdout,
        '(?m)^UEFI_HYPERVISOR_WINDOWS_CPU:(\d+):(\d+):(PASS|FAIL|ABSENT)\r?$')
    $SeenProcessors = @{}
    $CpuPassCount = 0
    $AllCpuRecordsPass = $true
    foreach ($Match in $HealthLines) {
        $Identity = '{0}:{1}' -f $Match.Groups[1].Value, $Match.Groups[2].Value
        if ($SeenProcessors.ContainsKey($Identity)) {
            $AllCpuRecordsPass = $false
            continue
        }
        $SeenProcessors[$Identity] = $true
        if ($Match.Groups[3].Value -ceq 'PASS') { $CpuPassCount++ }
        else { $AllCpuRecordsPass = $false }
    }
    $AggregateLines = [regex]::Matches(
        $Health.Stdout,
        '(?m)^UEFI_HYPERVISOR_WINDOWS_CPUID:(PASS|FAIL|ABSENT)\r?$')
    $AggregatePass = $AggregateLines.Count -eq 1 -and
        $AggregateLines[0].Groups[1].Value -ceq 'PASS'
    $CpuCountMatched = $SeenProcessors.Count -eq [int]$Metadata.ExpectedCpus
    $HealthPassed = $CpuCountMatched -and $AllCpuRecordsPass -and
        $CpuPassCount -eq [int]$Metadata.ExpectedCpus -and $AggregatePass
    $Result.CpuPassCount = $CpuPassCount
    $Result.HealthPassed = $HealthPassed
    if (-not $HealthPassed) {
        throw "Per-CPU health mismatch: $CpuPassCount/$($Metadata.ExpectedCpus) pass records; aggregate pass=$AggregatePass."
    }

    $BootManagerAfter = [string](Get-BootManagerHash)
    $Result.BootManagerSha256After = $BootManagerAfter
    $BootManagerUnchanged = $BootManagerAfter -ceq `
        [string]$Metadata.BootManagerSha256Before
    $Result.BootManagerUnchanged = $BootManagerUnchanged
    if (-not $BootManagerUnchanged) {
        throw 'Microsoft bootmgfw.efi changed during the one-shot guest test.'
    }

    $GraphicsInventory = Get-GraphicsInventory
    $Result.GraphicsInventory = $GraphicsInventory
    $IntelAdapterPresent = @($GraphicsInventory | Where-Object {
        ([string]$_.PnpDeviceId -match '(?i)^PCI\\VEN_8086') -or
        ([string]$_.Name -match '(?i)\bIntel\b')
    }).Count -gt 0
    $Result.IntelAdapterPresent = $IntelAdapterPresent
    $Result.NotSupportedExpectedWhenNoIntel = -not $IntelAdapterPresent
    $Result.AdapterVerdict = if ($IntelAdapterPresent) {
        'INTEL_ADAPTER_PRESENT_REQUIRES_PREBOOT_HAL_EVIDENCE'
    } else {
        'NOT_SUPPORTED_EXPECTED_NO_INTEL_ADAPTER'
    }
    $OutputBuilder.AppendLine('===== graphics inventory =====') | Out-Null
    $OutputBuilder.AppendLine(($GraphicsInventory | ConvertTo-Json -Depth 5)) | Out-Null
    $OutputBuilder.AppendLine("IntelAdapterPresent=$IntelAdapterPresent") | Out-Null
    $OutputBuilder.AppendLine('HAL_ROOT_NULL marker is separate preboot serial evidence.') | Out-Null

    $Result.Status = 'PASS'
    $Result.Message = 'Windows build, CPU health, aggregate health, and boot-manager integrity checks passed.'
}
catch {
    $Result.Status = 'FAIL'
    $Result.Message = $_.Exception.Message
    $ErrorBuilder.AppendLine($_.Exception.ToString()) | Out-Null
}
finally {
    try {
        $TaskDelete = Invoke-CapturedProcess -FilePath `
            (Join-Path $env:SystemRoot 'System32\schtasks.exe') `
            -Arguments @('/Delete', '/TN', 'HvGpuHalVmVerify', '/F') `
            -TimeoutSeconds 15 -Label 'remove one-shot verification task'
        if ($TaskDelete.ExitCode -ne 0) {
            $Result.Status = 'FAIL'
            $Result.Message = 'GPU verification ran, but the one-shot task could not remove itself.'
            $ErrorBuilder.AppendLine('Startup verification task deletion failed.') | Out-Null
        }
    } catch {
        $Result.Status = 'FAIL'
        $Result.Message = 'GPU verification ran, but the one-shot task could not remove itself.'
        $ErrorBuilder.AppendLine($_.Exception.ToString()) | Out-Null
    }
    try {
        $Signed = Write-ResultAndHmac -Value $Result
        try {
            [void](Publish-FrameBounded -Frame $Signed.Frame)
        }
        catch {
            $ErrorBuilder.AppendLine($_.Exception.ToString()) | Out-Null
            # Re-sign the failure state so local evidence records the transport error.
            $Result.Status = 'FAIL'
            $Result.Message = 'Guest verification completed, but authenticated COM2 publication failed.'
            $Signed = Write-ResultAndHmac -Value $Result
            try { [void](Publish-FrameBounded -Frame $Signed.Frame) } catch {}
        }
        if ($null -ne $Signed.PayloadBytes) {
            [Array]::Clear($Signed.PayloadBytes, 0, $Signed.PayloadBytes.Length)
        }
    }
    catch {
        $ErrorBuilder.AppendLine($_.Exception.ToString()) | Out-Null
        try {
            $Result.Status = 'FAIL'
            $Result.Message = 'Guest verification could not produce authenticated evidence.'
            $Result.CompletedUtc = [DateTime]::UtcNow.ToString('o')
            $Result.Stdout = $OutputBuilder.ToString()
            $Result.Stderr = $ErrorBuilder.ToString()
            Write-Utf8NoBom -Path $ResultPath `
                -Text ($Result | ConvertTo-Json -Compress -Depth 10)
            Write-Utf8NoBom -Path $StdoutPath -Text $Result.Stdout
            Write-Utf8NoBom -Path $StderrPath -Text $Result.Stderr
        } catch {}
    }
}

if ($Result.Status -eq 'PASS') { exit 0 }
exit 1
