[CmdletBinding()]
param(
    [string]$Stage = 'C:\V3H',
    [string]$EvidenceRoot = 'C:\V3H\evidence'
)

$ErrorActionPreference = 'Stop'
$Driver = Join-Path $Stage 'hypervisor_driver.sys'
$Loader = Join-Path $Stage 'hypervisor_loader.exe'
$ProtocolProbe = Join-Path $Stage 'control_session_protocol_replay_probe.exe'
$Required = @(
    $Driver
    (Join-Path $Stage 'hypervisor_driver.pdb')
    $Loader
    (Join-Path $Stage 'hypervisor_loader.pdb')
    $ProtocolProbe
    (Join-Path $Stage 'gate53-control-session-protocol-replay.ps1')
)
$Missing = @($Required | Where-Object {
    -not (Test-Path -LiteralPath $_ -PathType Leaf)
})
if ($Missing.Count -ne 0) {
    throw "Missing staged artifact(s): $($Missing -join ', ')"
}

$Identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$Principal = [Security.Principal.WindowsPrincipal]::new($Identity)
if (-not $Principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run from elevated Windows PowerShell in the disposable test VM.'
}

if (-not ('Gate53ProcessorGroups' -as [type])) {
    Add-Type @'
using System.Runtime.InteropServices;
public static class Gate53ProcessorGroups {
    [DllImport("kernel32.dll")]
    public static extern ushort GetActiveProcessorGroupCount();
    [DllImport("kernel32.dll")]
    public static extern uint GetActiveProcessorCount(ushort groupNumber);
}
'@
}


$RunName = 'gate53-control-session-protocol-replay-' +
    (Get-Date -Format 'yyyyMMdd-HHmmss')
$RunEvidence = Join-Path $EvidenceRoot $RunName
$SummaryPath = Join-Path $RunEvidence 'summary.txt'
$TranscriptPath = Join-Path $RunEvidence 'console.log'
$ZipPath = "$RunEvidence.zip"
$GateStart = Get-Date
$TranscriptStarted = $false
$Passed = $false
$Failure = $null

function Save-ServiceState([string]$Name) {
    $Output = & sc.exe query hypersinv 2>&1
    $Code = $LASTEXITCODE
    $Output | Out-File -LiteralPath (Join-Path $RunEvidence "$Name.txt") `
        -Encoding utf8
    $Output | Write-Host
    [pscustomobject]@{
        ExitCode = $Code
        Output = ($Output -join [Environment]::NewLine)
    }
}

function Invoke-Loader([string]$Name, [string]$Arguments) {
    $Stdout = Join-Path $RunEvidence "$Name-stdout.txt"
    $Stderr = Join-Path $RunEvidence "$Name-stderr.txt"
    $Process = Start-Process -FilePath $Loader -ArgumentList $Arguments `
        -Wait -PassThru -NoNewWindow `
        -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr
    $OutText = Get-Content -Raw -LiteralPath $Stdout
    $ErrText = Get-Content -Raw -LiteralPath $Stderr
    if ($OutText) { $OutText | Write-Host }
    if ($ErrText) { $ErrText | Write-Host }
    [pscustomobject]@{
        ExitCode = [int]$Process.ExitCode
        Stdout = $OutText
        Stderr = $ErrText
    }
}

function Invoke-ProtocolProbe {
    $Stdout = Join-Path $RunEvidence 'protocol-replay-probe-stdout.txt'
    $Stderr = Join-Path $RunEvidence 'protocol-replay-probe-stderr.txt'
    $Process = Start-Process -FilePath $ProtocolProbe `
        -Wait -PassThru -NoNewWindow `
        -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr
    $OutText = Get-Content -Raw -LiteralPath $Stdout
    $ErrText = Get-Content -Raw -LiteralPath $Stderr
    if ($OutText) { $OutText | Write-Host }
    if ($ErrText) { $ErrText | Write-Host }
    [pscustomobject]@{
        ExitCode = [int]$Process.ExitCode
        Stdout = $OutText
        Stderr = $ErrText
    }
}

function Assert-ExactRecords(
    [string]$Text,
    [string]$Prefix,
    [string[]]$Expected
) {
    $Lines = @($Text -split "`r?`n" | Where-Object {
        $_ -like "$Prefix*"
    })
    if ($Lines.Count -ne $Expected.Count) {
        throw "Expected $($Expected.Count) $Prefix records; observed $($Lines.Count)."
    }
    for ($Index = 0; $Index -lt $Expected.Count; $Index++) {
        if ($Lines[$Index] -ne $Expected[$Index]) {
            throw "Unexpected $Prefix record at index ${Index}: $($Lines[$Index])"
        }
    }
}

function Get-InitialDispatchLines {
    $Suffix = 'generation=1 telemetry_sequence=2 dispatch_sequence=2 ' +
        'reason=18 length=3 action=1 protocol=2 ' +
        'protocol_code=0x00000FFF validation_code=0 fail_closed=0 ' +
        'validation_flags=0x00000003 vmread_outcome=1 ' +
        'vmread_error_valid=0 vmread_error=0 vmwrite_outcome=1 ' +
        'vmwrite_error_valid=0 vmwrite_error=0 failed_vmcs_field=0x0'
    @(
        "DISPATCH CPU: index=0 group=0 number=0 $Suffix"
        "DISPATCH CPU: index=1 group=0 number=1 $Suffix"
        "DISPATCH CPU: index=2 group=1 number=0 $Suffix"
        "DISPATCH CPU: index=3 group=1 number=1 $Suffix"
    )
}

function Get-InitialTelemetryLines {
    $Suffix = 'generation=1 sequence=2 total=1 cpuid=0 cr3=0 ' +
        'msr_read=0 msr_write=0 ept=0 vmcall=1 other=0 fail_closed=0'
    @(
        "TELEMETRY CPU: index=0 group=0 number=0 $Suffix"
        "TELEMETRY CPU: index=1 group=0 number=1 $Suffix"
        "TELEMETRY CPU: index=2 group=1 number=0 $Suffix"
        "TELEMETRY CPU: index=3 group=1 number=1 $Suffix"
    )
}

function Get-PostProbeDispatchLines {
    $Initial = @(Get-InitialDispatchLines)
    $Suffix = 'generation=1 telemetry_sequence=10 dispatch_sequence=10 ' +
        'reason=18 length=3 action=5 protocol=7 ' +
        'protocol_code=0x00000FFF validation_code=0 fail_closed=0 ' +
        'validation_flags=0x00000033 vmread_outcome=1 ' +
        'vmread_error_valid=0 vmread_error=0 vmwrite_outcome=1 ' +
        'vmwrite_error_valid=0 vmwrite_error=0 failed_vmcs_field=0x0'
    @("DISPATCH CPU: index=0 group=0 number=0 $Suffix") + $Initial[1..3]
}

function Get-PostProbeTelemetryLines {
    $Initial = @(Get-InitialTelemetryLines)
    $Suffix = 'generation=1 sequence=10 total=5 cpuid=3 cr3=0 ' +
        'msr_read=0 msr_write=0 ept=0 vmcall=2 other=0 fail_closed=0'
    @("TELEMETRY CPU: index=0 group=0 number=0 $Suffix") + $Initial[1..3]
}

function Get-InitialObservationLines {
    $Suffix = 'generation=1 telemetry_sequence=2 dispatch_sequence=2 ' +
        'total=1 cpuid=0 cr3=0 msr_read=0 msr_write=0 ept=0 vmcall=1 ' +
        'other=0 fail_closed_events=0 reason=18 length=3 action=1 protocol=2 ' +
        'protocol_code=0x00000FFF validation_code=0 fail_closed=0 ' +
        'validation_flags=0x00000003 vmread_outcome=1 ' +
        'vmread_error_valid=0 vmread_error=0 vmwrite_outcome=1 ' +
        'vmwrite_error_valid=0 vmwrite_error=0 failed_vmcs_field=0x0'
    @(
        "OBSERVATION CPU: index=0 group=0 number=0 $Suffix"
        "OBSERVATION CPU: index=1 group=0 number=1 $Suffix"
        "OBSERVATION CPU: index=2 group=1 number=0 $Suffix"
        "OBSERVATION CPU: index=3 group=1 number=1 $Suffix"
    )
}

function Get-PostProbeObservationLines {
    $Initial = @(Get-InitialObservationLines)
    $Suffix = 'generation=1 telemetry_sequence=10 dispatch_sequence=10 ' +
        'total=5 cpuid=3 cr3=0 msr_read=0 msr_write=0 ept=0 vmcall=2 ' +
        'other=0 fail_closed_events=0 reason=18 length=3 action=5 protocol=7 ' +
        'protocol_code=0x00000FFF validation_code=0 fail_closed=0 ' +
        'validation_flags=0x00000033 vmread_outcome=1 ' +
        'vmread_error_valid=0 vmread_error=0 vmwrite_outcome=1 ' +
        'vmwrite_error_valid=0 vmwrite_error=0 failed_vmcs_field=0x0'
    @("OBSERVATION CPU: index=0 group=0 number=0 $Suffix") + $Initial[1..3]
}

function Get-GateFailureEvents {
    $System = @(Get-WinEvent -FilterHashtable @{
        LogName = 'System'; StartTime = $GateStart
    } -ErrorAction SilentlyContinue | Where-Object {
        ($_.ProviderName -eq 'Microsoft-Windows-Kernel-Power' -and
            $_.Id -eq 41 -and $_.Level -le 2) -or
        ($_.ProviderName -eq 'Microsoft-Windows-WHEA-Logger' -and
            $_.Level -le 2)
    })
    $Application = @(Get-WinEvent -FilterHashtable @{
        LogName = 'Application'; StartTime = $GateStart
    } -ErrorAction SilentlyContinue | Where-Object {
        $_.ProviderName -eq 'Application Error' -and
        $_.Id -eq 1000 -and $_.Level -le 2 -and
        $_.Message -match '(?i)\b(hypervisor_loader\.exe|control_session_protocol_probe\.exe|powershell\.exe)\b'
    })
    @($System + $Application)
}

New-Item -ItemType Directory -Force -Path $RunEvidence | Out-Null

try {
    Start-Transcript -LiteralPath $TranscriptPath -Force | Out-Null
    $TranscriptStarted = $true
    "GateStart=$($GateStart.ToString('o'))"
    "Stage=$Stage"
    "RunEvidence=$RunEvidence"

    $Signature = Get-AuthenticodeSignature -LiteralPath $Driver
    $Signature | Select-Object Status, StatusMessage,
        @{Name='Signer'; Expression={$_.SignerCertificate.Subject}} |
        Format-List
    if ($Signature.Status -ne
            [Management.Automation.SignatureStatus]::Valid -or
        $Signature.SignerCertificate.Subject -ne 'CN=hypersinv') {
        throw 'Driver signature is not valid for CN=hypersinv in this VM.'
    }

    Get-ComputerInfo |
        Select-Object WindowsProductName, WindowsVersion, OsBuildNumber |
        Format-List
    Get-CimInstance Win32_ComputerSystem |
        Select-Object Manufacturer, Model, NumberOfLogicalProcessors |
        Format-List
    $GroupCount = [Gate53ProcessorGroups]::GetActiveProcessorGroupCount()
    $GroupSizes = @(for ($Group = 0; $Group -lt $GroupCount; $Group++) {
        [pscustomobject]@{
            Group = $Group
            ActiveProcessors =
                [Gate53ProcessorGroups]::GetActiveProcessorCount(
                    [uint16]$Group)
        }
    })
    $GroupSizes | Format-Table -AutoSize
    $TotalProcessors =
        ($GroupSizes | Measure-Object ActiveProcessors -Sum).Sum
    if ($GroupCount -ne 2 -or $TotalProcessors -ne 4 -or
        @($GroupSizes | Where-Object ActiveProcessors -ne 2).Count -ne 0) {
        throw "Expected 2 groups with 2 processors each; groups=$GroupCount total=$TotalProcessors."
    }

    $Recovery = Get-CimInstance Win32_OSRecoveryConfiguration
    if ($Recovery.DebugInfoType -ne 2 -or $Recovery.AutoReboot -or
        -not $Recovery.WriteDebugInfo) {
        throw 'Crash-dump configuration is incorrect.'
    }
    Get-Item -LiteralPath $Required |
        Select-Object FullName, Length, LastWriteTime | Format-List
    Get-FileHash -LiteralPath $Required -Algorithm SHA256 |
        Format-List Path, Hash

    $BeforeService = Save-ServiceState 'service-before'
    if ($BeforeService.ExitCode -ne 1060) {
        throw "Service was present before LOAD (exit $($BeforeService.ExitCode))."
    }

    $Load = Invoke-Loader 'load' 'load'
    $LoadProof =
        "SUCCESS: Hypervisor loaded on $TotalProcessors cores and verified by exact V3 status."
    if ($Load.ExitCode -ne 0 -or -not $Load.Stdout.Contains($LoadProof) -or
        -not $Load.Stdout.Contains('Protocol negotiated: 1.0') -or
        -not $Load.Stdout.Contains('INFO: Exact EPT READY status is verified.')) {
        throw "LOAD was not verified (exit $($Load.ExitCode)). Stop; do not retry."
    }
    $AfterLoad = Save-ServiceState 'service-after-load'
    if ($AfterLoad.ExitCode -ne 0 -or
        -not $AfterLoad.Output.Contains('STATE              : 4  RUNNING')) {
        throw 'Driver service was not running after LOAD.'
    }

    $Protocol = Invoke-ProtocolProbe
    $ExpectedProtocolLines = @(
        'InitialNegotiation=ACCEPTED_1_0'
        'ExactReplay=DENIED_5'
        'IncompatibleReplay=DENIED_5'
        'PostReplayObservation=EXACT_ACTIVE'
    )
    if ($Protocol.ExitCode -ne 0 -or
        -not [string]::IsNullOrEmpty($Protocol.Stderr)) {
        throw "Control-session protocol probe failed with exit $($Protocol.ExitCode)."
    }
    foreach ($ExpectedLine in $ExpectedProtocolLines) {
        if (-not ($Protocol.Stdout -split "`r?`n").Contains($ExpectedLine)) {
            throw "Control-session protocol probe missing: $ExpectedLine"
        }
    }
    $ExpectedProtocolLines | Set-Content -LiteralPath `
        (Join-Path $RunEvidence 'protocol-boundary.txt') -Encoding utf8

    $Observation = Invoke-Loader `
        'observation-after-protocol-replay' 'observation-status'
    if ($Observation.ExitCode -ne 0 -or
        -not $Observation.Stdout.Contains(
            'SUCCESS: Coherent VM-exit observation bundle verified on 4 processors.')) {
        throw 'Observation status did not recover after the protocol probe closed.'
    }
    Assert-ExactRecords $Observation.Stdout 'OBSERVATION CPU: ' `
        @(Get-InitialObservationLines)

    $Unload = Invoke-Loader 'unload' 'unload'
    if ($Unload.ExitCode -ne 0 -or
        -not $Unload.Stdout.Contains('SUCCESS: Hypervisor unloaded')) {
        throw "UNLOAD was not verified (exit $($Unload.ExitCode)). Stop; do not retry."
    }
    $AfterUnload = Save-ServiceState 'service-after-unload'
    if ($AfterUnload.ExitCode -ne 1060) {
        throw "Service removal was not confirmed (exit $($AfterUnload.ExitCode))."
    }

    $FailureEvents = @(Get-GateFailureEvents)
    $FailureEvents | Select-Object TimeCreated, ProviderName, Id,
        LevelDisplayName, Message |
        Export-Clixml -LiteralPath (Join-Path $RunEvidence 'failure-events.xml')
    if ($FailureEvents.Count -ne 0) {
        throw 'Crash, WHEA, or application-failure event detected.'
    }
    $Dump = Get-Item -LiteralPath 'C:\Windows\MEMORY.DMP' `
        -ErrorAction SilentlyContinue
    if ($Dump -and
        $Dump.LastWriteTimeUtc -gt $GateStart.ToUniversalTime()) {
        throw 'A new MEMORY.DMP was generated.'
    }

    @(
        'Result=PASS'
        'Probe=CONTROL_SESSION_PROTOCOL_REPLAY'
        'Processors=4'
        'InitialNegotiation=ACCEPTED_1_0'
        'ExactReplay=DENIED_5'
        'IncompatibleReplay=DENIED_5'
        'PostReplayObservation=EXACT_ACTIVE'
        'ObservationAfterReplayProbeClose=EXACT_ACTIVE'
        'OwnerGeneration=1,1,1,1'
        'TelemetrySequence=2,2,2,2'
        'DispatchSequence=2,2,2,2'
        'ServiceAfterUnload=ABSENT'
    ) | Set-Content -LiteralPath $SummaryPath -Encoding utf8
    $Passed = $true
    Write-Host 'PASS: immutable one-shot protocol negotiation and canonical unload verified.'
}
catch {
    $Failure = $_
    @('Result=FAIL', "Error=$($_.Exception.Message)") |
        Set-Content -LiteralPath $SummaryPath -Encoding utf8
    Write-Host $_.Exception.Message -ForegroundColor Red
}
finally {
    try {
        & sc.exe query hypersinv 2>&1 |
            Out-File -LiteralPath (Join-Path $RunEvidence 'service-on-exit.txt') `
                -Encoding utf8
    }
    catch {}
    try {
        & sc.exe qc hypersinv 2>&1 |
            Out-File -LiteralPath `
                (Join-Path $RunEvidence 'service-config-on-exit.txt') `
                -Encoding utf8
    }
    catch {}
    try {
        @(Get-GateFailureEvents) |
            Select-Object TimeCreated, ProviderName, Id, LevelDisplayName,
                Message |
            Export-Clixml -LiteralPath `
                (Join-Path $RunEvidence 'failure-events-on-exit.xml')
    }
    catch {}
    if ($TranscriptStarted) { Stop-Transcript | Out-Null }
    & tar.exe -a -c -f $ZipPath -C $EvidenceRoot $RunName
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Evidence archive failed: $ZipPath"
    }
    else {
        Write-Host "Evidence: $ZipPath"
    }
}

if ($Failure) { throw $Failure }
if (-not $Passed) { exit 1 }

