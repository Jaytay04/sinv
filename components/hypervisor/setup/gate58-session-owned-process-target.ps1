[CmdletBinding()]
param(
    [string]$Stage = 'C:\V3H',
    [string]$EvidenceRoot = 'C:\V3H\evidence'
)

$ErrorActionPreference = 'Stop'
$Driver = Join-Path $Stage 'hypervisor_driver.sys'
$Loader = Join-Path $Stage 'hypervisor_loader.exe'
$Required = @(
    $Driver
    (Join-Path $Stage 'hypervisor_driver.pdb')
    $Loader
    (Join-Path $Stage 'hypervisor_loader.pdb')
    (Join-Path $Stage 'gate58-session-owned-process-target.ps1')
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

if (-not ('Gate58ProcessorGroups' -as [type])) {
    Add-Type @'
using System.Runtime.InteropServices;
public static class Gate58ProcessorGroups {
    [DllImport("kernel32.dll")]
    public static extern ushort GetActiveProcessorGroupCount();
    [DllImport("kernel32.dll")]
    public static extern uint GetActiveProcessorCount(ushort groupNumber);
}
'@
}

$RunName = 'gate58-session-owned-process-target-' +
    (Get-Date -Format 'yyyyMMdd-HHmmss')
$RunEvidence = Join-Path $EvidenceRoot $RunName
$SummaryPath = Join-Path $RunEvidence 'summary.txt'
$TranscriptPath = Join-Path $RunEvidence 'console.log'
$ZipPath = "$RunEvidence.zip"
$HolderStdout = Join-Path $RunEvidence 'target-holder-stdout.txt'
$HolderStderr = Join-Path $RunEvidence 'target-holder-stderr.txt'
$GateStart = Get-Date
$TranscriptStarted = $false
$Passed = $false
$Failure = $null
$Holder = $null

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
    $OutText = [string](Get-Content -Raw -LiteralPath $Stdout)
    $ErrText = [string](Get-Content -Raw -LiteralPath $Stderr)
    if ($OutText) { $OutText | Write-Host }
    if ($ErrText) { $ErrText | Write-Host }
    [pscustomobject]@{
        ExitCode = [int]$Process.ExitCode
        Stdout = $OutText
        Stderr = $ErrText
    }
}

function Start-TargetHolder {
    Start-Process -FilePath $Loader -ArgumentList 'load-target-session-owner' `
        -PassThru -NoNewWindow `
        -RedirectStandardOutput $HolderStdout `
        -RedirectStandardError $HolderStderr
}

function Wait-ForHolderReady(
    [System.Diagnostics.Process]$Process,
    [int]$TimeoutSeconds = 120
) {
    $Marker = 'READY: Process target is ACTIVE and self-boundary proof completed; terminate this process to test expiry.'
    $Deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $Process.Refresh()
        $OutText = Get-Content -Raw -LiteralPath $HolderStdout `
            -ErrorAction SilentlyContinue
        if ($null -eq $OutText) { $OutText = [string]::Empty }
        else { $OutText = [string]$OutText }
        if ($OutText.Contains($Marker)) { return $OutText }
        if ($Process.HasExited) {
            $ErrText = [string](Get-Content -Raw -LiteralPath $HolderStderr `
                -ErrorAction SilentlyContinue)
            throw "Target holder exited before READY (exit $($Process.ExitCode)): $ErrText"
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $Deadline)
    throw "Target holder did not reach READY within $TimeoutSeconds seconds."
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
        $_.Message -match '(?i)\b(hypervisor_loader\.exe|powershell\.exe)\b'
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
    $GroupCount = [Gate58ProcessorGroups]::GetActiveProcessorGroupCount()
    $GroupSizes = @(for ($Group = 0; $Group -lt $GroupCount; $Group++) {
        [pscustomobject]@{
            Group = $Group
            ActiveProcessors =
                [Gate58ProcessorGroups]::GetActiveProcessorCount(
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

    $Holder = Start-TargetHolder
    $HolderText = Wait-ForHolderReady $Holder
    $HolderError = [string](Get-Content -Raw -LiteralPath $HolderStderr)
    $HolderText | Write-Host
    if ($HolderError) { $HolderError | Write-Host }
    $LoadProof =
        "SUCCESS: Hypervisor loaded on $TotalProcessors cores and verified by exact V3 status."
    foreach ($Proof in @(
        'Configuring current session owner as process target (no PID input).'
        'SUCCESS: Current control-session owner configured as target.'
        $LoadProof
        'INFO: Exact EPT READY status is verified.'
        'SUCCESS: Exact redacted self-target LEAVE/RETURN roundtrip verified.'
        'READY: Process target is ACTIVE and self-boundary proof completed; terminate this process to test expiry.'
    )) {
        if (-not $HolderText.Contains($Proof)) {
            throw "Target holder output did not prove: $Proof"
        }
    }
    if (-not [string]::IsNullOrEmpty($HolderError)) {
        throw 'Target holder wrote unexpected stderr before READY.'
    }
    if ($HolderText.Contains('Configuring current process target: pid=')) {
        throw 'Session-owned path unexpectedly used caller-selected PID input.'
    }
    $ActiveMatch = [regex]::Match($HolderText,
        'Process Target: status=0 state=2 pid=(\d+) start_key=(\d+) generation=(\d+) flags=0x00000007')
    if (-not $ActiveMatch.Success -or
        [uint64]$ActiveMatch.Groups[1].Value -ne [uint64]$Holder.Id -or
        [uint64]$ActiveMatch.Groups[2].Value -eq 0 -or
        [uint64]$ActiveMatch.Groups[3].Value -eq 0) {
        throw 'Target holder did not publish the exact redacted ACTIVE state.'
    }
    $AfterLoad = Save-ServiceState 'service-after-load'
    if ($AfterLoad.ExitCode -ne 0 -or
        -not $AfterLoad.Output.Contains('STATE              : 4  RUNNING')) {
        throw 'Driver service was not running after target LOAD.'
    }

    $TargetPid = [int]$Holder.Id
    Stop-Process -Id $TargetPid -Force
    Wait-Process -Id $TargetPid -Timeout 15 -ErrorAction SilentlyContinue
    $Holder.Refresh()
    if (-not $Holder.HasExited) {
        throw 'Target holder did not terminate within 15 seconds.'
    }
    Start-Sleep -Milliseconds 500

    $Expired = Invoke-Loader 'target-after-exit' 'target-status'
    if ($Expired.ExitCode -ne 0 -or
        -not [string]::IsNullOrEmpty($Expired.Stderr)) {
        throw "Expired target status query failed (exit $($Expired.ExitCode))."
    }
    $ExpiredMatch = [regex]::Match($Expired.Stdout,
        'Process Target: status=0 state=3 pid=(\d+) start_key=(\d+) generation=(\d+) flags=0x00000003')
    if (-not $ExpiredMatch.Success -or
        [uint64]$ExpiredMatch.Groups[1].Value -ne [uint64]$TargetPid -or
        [uint64]$ExpiredMatch.Groups[2].Value -eq 0 -or
        [uint64]$ExpiredMatch.Groups[3].Value -eq 0 -or
        $ExpiredMatch.Groups[2].Value -ne $ActiveMatch.Groups[2].Value -or
        $ExpiredMatch.Groups[3].Value -ne $ActiveMatch.Groups[3].Value -or
        -not $Expired.Stdout.Contains(
            'SUCCESS: Exact redacted process-target status verified.')) {
        throw 'Process exit did not publish the exact redacted EXPIRED state.'
    }

    $FailClosed = Invoke-Loader 'probe-after-target-exit' `
        'cr3-residency-probe'
    if ($FailClosed.ExitCode -eq 0 -or
        -not $FailClosed.Stdout.Contains(
            'CR3 Residency Probe: status=0x80000001 stage=0') -or
        -not $FailClosed.Stderr.Contains(
            'ERROR: Exact CR3 target ENTER/LEAVE proof was not verified.') -or
        $FailClosed.Stdout.Contains(
            'SUCCESS: Exact redacted CR3 target ENTER/LEAVE roundtrip verified.') -or
        $FailClosed.Stdout.Contains(
            'SUCCESS: Exact redacted self-target LEAVE/RETURN roundtrip verified.')) {
        throw 'Expired process-target route did not fail closed before the boundary.'
    }

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
        'Probe=SESSION_OWNED_PROCESS_TARGET'
        'Processors=4'
        "TargetPid=$TargetPid"
        "ProcessStartKey=$($ExpiredMatch.Groups[2].Value)"
        "TargetGeneration=$($ExpiredMatch.Groups[3].Value)"
        'ConfigurationSource=SESSION_OWNER_NO_PID'
        'ConfiguredState=CONFIGURED'
        'LoadedState=ACTIVE'
        'SelfBoundary=1/0/1'
        'ExitState=EXPIRED'
        'ExpiredFlags=0x00000003'
        'ExpiredBoundary=FAIL_CLOSED_NOT_ACTIVE'
        'ServiceAfterUnload=ABSENT'
    ) | Set-Content -LiteralPath $SummaryPath -Encoding utf8
    $Passed = $true
    Write-Host 'PASS: session-owned process-target activation, expiry, fail-closed route, and canonical unload verified.'
}
catch {
    $Failure = $_
    @('Result=FAIL', "Error=$($_.Exception.Message)") |
        Set-Content -LiteralPath $SummaryPath -Encoding utf8
    Write-Host $_.Exception.Message -ForegroundColor Red
}
finally {
    if ($Holder) {
        try {
            $Holder.Refresh()
            if (-not $Holder.HasExited) {
                Stop-Process -Id $Holder.Id -Force -ErrorAction SilentlyContinue
            }
        }
        catch {}
    }
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
