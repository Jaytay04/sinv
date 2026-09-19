[CmdletBinding()]
param(
    [string]$Stage = 'C:\V3H',
    [string]$EvidenceRoot = 'C:\V3H\evidence'
)

$ErrorActionPreference = 'Stop'
$Driver = Join-Path $Stage 'hypervisor_driver.sys'
$Loader = Join-Path $Stage 'hypervisor_loader.exe'
$RequiredNames = @(
    'hypervisor_driver.sys'
    'hypervisor_driver.pdb'
    'hypervisor_loader.exe'
    'hypervisor_loader.pdb'
    'gate40-vmexit-telemetry-cpuid-delta.ps1'
)
$Missing = @($RequiredNames | Where-Object {
    -not (Test-Path -LiteralPath (Join-Path $Stage $_) -PathType Leaf)
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

if (-not ('Gate40ProcessorGroups' -as [type])) {
    Add-Type @'
using System.Runtime.InteropServices;
public static class Gate40ProcessorGroups {
    [DllImport("kernel32.dll")]
    public static extern ushort GetActiveProcessorGroupCount();
    [DllImport("kernel32.dll")]
    public static extern uint GetActiveProcessorCount(ushort groupNumber);
}
'@
}

$RunName = 'gate40-vmexit-telemetry-cpuid-delta-' +
    (Get-Date -Format 'yyyyMMdd-HHmmss')
$RunEvidence = Join-Path $EvidenceRoot $RunName
$SummaryPath = Join-Path $RunEvidence 'summary.txt'
$TranscriptPath = Join-Path $RunEvidence 'console.log'
$ZipPath = "$RunEvidence.zip"
$GateStart = Get-Date
$TranscriptStarted = $false
$Passed = $false
$Failure = $null

function Invoke-Loader([string]$Name, [string]$Argument) {
    $Stdout = Join-Path $RunEvidence "$Name-stdout.txt"
    $Stderr = Join-Path $RunEvidence "$Name-stderr.txt"
    $Process = Start-Process -FilePath $Loader -ArgumentList $Argument `
        -Wait -PassThru -NoNewWindow `
        -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr
    $OutText = if (Test-Path -LiteralPath $Stdout) {
        Get-Content -LiteralPath $Stdout -Raw
    } else { '' }
    $ErrText = if (Test-Path -LiteralPath $Stderr) {
        Get-Content -LiteralPath $Stderr -Raw
    } else { '' }
    if ($OutText) { $OutText | Write-Host }
    if ($ErrText) { $ErrText | Write-Host }
    [pscustomobject]@{
        ExitCode = $Process.ExitCode
        Stdout = $OutText
        Stderr = $ErrText
    }
}

function Save-ServiceState([string]$Name) {
    $Output = & sc.exe query hypersinv 2>&1
    $Code = $LASTEXITCODE
    $Output | Out-File -LiteralPath (Join-Path $RunEvidence "$Name.txt") `
        -Encoding utf8
    $Output | Write-Host
    [pscustomobject]@{ ExitCode = $Code; Output = ($Output -join "`n") }
}

function Parse-Telemetry([string]$Text) {
    $Pattern = '^TELEMETRY CPU: index=(\d+) group=(\d+) number=(\d+) ' +
        'generation=(\d+) sequence=(\d+) total=(\d+) cpuid=(\d+) ' +
        'cr3=(\d+) msr_read=(\d+) msr_write=(\d+) ept=(\d+) ' +
        'vmcall=(\d+) other=(\d+) fail_closed=(\d+)$'
    $Records = @{}
    foreach ($Line in ($Text -split "`r?`n")) {
        if ($Line -match $Pattern) {
            $Index = [uint32]$Matches[1]
            if ($Records.ContainsKey($Index)) {
                throw "Duplicate telemetry record: $Index"
            }
            $Records[$Index] = [pscustomobject]@{
                Index = $Index
                Group = [uint16]$Matches[2]
                Number = [byte]$Matches[3]
                Generation = [uint64]$Matches[4]
                Sequence = [uint64]$Matches[5]
                Total = [uint64]$Matches[6]
                Cpuid = [uint64]$Matches[7]
                Cr3 = [uint64]$Matches[8]
                MsrRead = [uint64]$Matches[9]
                MsrWrite = [uint64]$Matches[10]
                Ept = [uint64]$Matches[11]
                Vmcall = [uint64]$Matches[12]
                Other = [uint64]$Matches[13]
                FailClosed = [uint64]$Matches[14]
            }
        }
    }
    if ($Records.Count -ne 4) {
        throw "Expected four telemetry records; observed $($Records.Count)."
    }
    return $Records
}

function Assert-TelemetrySet([hashtable]$Records) {
    $Expected = @(@(0,0), @(0,1), @(1,0), @(1,1))
    # Parse-Telemetry stores UInt32 keys. Windows PowerShell 5.1 does not
    # consider an Int32 key equal to the corresponding UInt32 key.
    $Generation = $Records[[uint32]0].Generation
    if ($Generation -eq 0) { throw 'Telemetry generation was zero.' }
    for ($Index = 0; $Index -lt 4; $Index++) {
        $Record = $Records[[uint32]$Index]
        if ($null -eq $Record -or
            $Record.Group -ne $Expected[$Index][0] -or
            $Record.Number -ne $Expected[$Index][1] -or
            $Record.Generation -ne $Generation -or
            ($Record.Sequence -band 1) -ne 0 -or
            $Record.FailClosed -ne 0 -or
            ($Record.Cpuid + $Record.Cr3 + $Record.MsrRead +
             $Record.MsrWrite + $Record.Ept + $Record.Vmcall +
             $Record.Other) -ne $Record.Total) {
            throw "Malformed telemetry record at index $Index."
        }
    }
}

function Get-GateFailureEvents {
    $Providers = @('Microsoft-Windows-WHEA-Logger','Application Error',
        'Windows Error Reporting')
    $Events = @()
    foreach ($Provider in $Providers) {
        $Events += @(Get-WinEvent -FilterHashtable @{
            ProviderName=$Provider
            StartTime=$GateStart
            Level=1,2
        } -ErrorAction SilentlyContinue)
    }
    @($Events)
}

New-Item -ItemType Directory -Force -Path $RunEvidence | Out-Null
try {
    Start-Transcript -LiteralPath $TranscriptPath -Force | Out-Null
    $TranscriptStarted = $true
    "GateStart=$($GateStart.ToString('o'))"
    "Stage=$Stage"
    "RunEvidence=$RunEvidence"

    Get-FileHash -LiteralPath @($RequiredNames | ForEach-Object {
        Join-Path $Stage $_
    }) -Algorithm SHA256 | Format-Table Path,Hash -AutoSize

    $Signature = Get-AuthenticodeSignature -LiteralPath $Driver
    $Signature | Select-Object Status,StatusMessage,
        @{Name='Signer';Expression={$_.SignerCertificate.Subject}} |
        Format-List
    if ($Signature.Status -ne 'Valid' -or
        $null -eq $Signature.SignerCertificate -or
        $Signature.SignerCertificate.Subject -ne 'CN=hypersinv') {
        throw 'Driver signature is not Valid for CN=hypersinv.'
    }

    $GroupCount = [Gate40ProcessorGroups]::GetActiveProcessorGroupCount()
    $GroupSizes = for ($Group = 0; $Group -lt $GroupCount; $Group++) {
        [pscustomobject]@{
            Group = $Group
            ActiveProcessors = [Gate40ProcessorGroups]::GetActiveProcessorCount(
                [uint16]$Group)
        }
    }
    $GroupSizes | Format-Table -AutoSize
    $TotalProcessors =
        ($GroupSizes | Measure-Object ActiveProcessors -Sum).Sum
    if ($GroupCount -ne 2 -or $TotalProcessors -ne 4 -or
        @($GroupSizes | Where-Object ActiveProcessors -ne 2).Count -ne 0) {
        throw 'Expected exactly two groups with two processors each.'
    }

    $Recovery = Get-CimInstance Win32_OSRecoveryConfiguration
    if ($Recovery.DebugInfoType -ne 2 -or $Recovery.AutoReboot -or
        -not $Recovery.WriteDebugInfo) {
        throw 'Crash-dump configuration is incorrect.'
    }

    $BeforeService = Save-ServiceState 'service-before'
    if ($BeforeService.ExitCode -ne 1060) {
        throw "Service was present before LOAD (exit $($BeforeService.ExitCode))."
    }

    $Load = Invoke-Loader 'load' 'load'
    if ($Load.ExitCode -ne 0 -or
        -not $Load.Stdout.Contains(
            'SUCCESS: Hypervisor loaded on 4 cores and verified by exact V3 status.') -or
        -not $Load.Stdout.Contains('INFO: Exact EPT READY status is verified.')) {
        throw "LOAD was not verified (exit $($Load.ExitCode)). Stop; do not retry."
    }

    $BeforeQuery = Invoke-Loader 'telemetry-before' 'telemetry-status'
    if ($BeforeQuery.ExitCode -ne 0 -or
        -not $BeforeQuery.Stdout.Contains(
            'SUCCESS: Exact VM-exit telemetry verified on 4 processors.')) {
        throw 'Baseline telemetry query was not verified.'
    }
    $Before = Parse-Telemetry $BeforeQuery.Stdout
    Assert-TelemetrySet $Before

    $Probe = Invoke-Loader 'cpuid-probe' 'cpuid-probe'
    if ($Probe.ExitCode -ne 0 -or
        -not $Probe.Stdout.Contains(
            'SUCCESS: Persistent CPUID transparency verified.') -or
        -not $Probe.Stdout.Contains(
            'INFO: Authenticated non-root CPUID guest roundtrip completed.')) {
        throw 'Authenticated CPUID probe was not verified.'
    }

    $AfterQuery = Invoke-Loader 'telemetry-after' 'telemetry-status'
    if ($AfterQuery.ExitCode -ne 0 -or
        -not $AfterQuery.Stdout.Contains(
            'SUCCESS: Exact VM-exit telemetry verified on 4 processors.')) {
        throw 'Post-probe telemetry query was not verified.'
    }
    $After = Parse-Telemetry $AfterQuery.Stdout
    Assert-TelemetrySet $After

    for ($Index = 0; $Index -lt 4; $Index++) {
        $A = $After[[uint32]$Index]
        $B = $Before[[uint32]$Index]
        if ($A.Generation -ne $B.Generation) {
            throw "Telemetry generation changed at index $Index."
        }
        $ExpectedTotal = if ($Index -eq 0) { 4 } else { 0 }
        $ExpectedCpuid = if ($Index -eq 0) { 3 } else { 0 }
        $ExpectedVmcall = if ($Index -eq 0) { 1 } else { 0 }
        if (($A.Total - $B.Total) -ne $ExpectedTotal -or
            ($A.Cpuid - $B.Cpuid) -ne $ExpectedCpuid -or
            ($A.Vmcall - $B.Vmcall) -ne $ExpectedVmcall -or
            ($A.Cr3 - $B.Cr3) -ne 0 -or
            ($A.MsrRead - $B.MsrRead) -ne 0 -or
            ($A.MsrWrite - $B.MsrWrite) -ne 0 -or
            ($A.Ept - $B.Ept) -ne 0 -or
            ($A.Other - $B.Other) -ne 0 -or
            ($A.FailClosed - $B.FailClosed) -ne 0) {
            throw "Unexpected telemetry delta at index $Index."
        }
    }

    $Unload = Invoke-Loader 'unload' 'unload'
    if ($Unload.ExitCode -ne 0 -or
        -not $Unload.Stdout.Contains('SUCCESS: Hypervisor unloaded') -or
        -not $Unload.Stdout.Contains('Service removal confirmed.')) {
        throw "UNLOAD was not verified (exit $($Unload.ExitCode)). Stop; do not retry."
    }
    $AfterUnload = Save-ServiceState 'service-after-unload'
    if ($AfterUnload.ExitCode -ne 1060) {
        throw "Service removal was not confirmed (exit $($AfterUnload.ExitCode))."
    }

    $FailureEvents = @(Get-GateFailureEvents)
    $FailureEvents | Select-Object TimeCreated,ProviderName,Id,
        LevelDisplayName,Message | Export-Clixml -LiteralPath (
            Join-Path $RunEvidence 'failure-events.xml')
    if ($FailureEvents.Count -ne 0) {
        throw 'Crash, WHEA, or application-failure event detected.'
    }
    $Dump = Get-Item -LiteralPath 'C:\Windows\MEMORY.DMP' `
        -ErrorAction SilentlyContinue
    if ($Dump -and $Dump.LastWriteTimeUtc -gt $GateStart.ToUniversalTime()) {
        throw 'A new MEMORY.DMP was generated.'
    }

    @(
        'Result=PASS'
        'Probe=VMEXIT_TELEMETRY_CPUID_DELTA'
        'Processors=4'
        'TargetSystemIndex=0'
        'TotalDelta=4'
        'CpuidDelta=3'
        'VmcallDelta=1'
        'NonTargetDelta=0'
        'FailClosedDelta=0'
        'ServiceAfterUnload=ABSENT'
    ) | Set-Content -LiteralPath $SummaryPath -Encoding utf8
    $Passed = $true
    Write-Host 'PASS: exact per-processor VM-exit telemetry and CPUID deltas were verified.'
}
catch {
    $Failure = $_
    @('Result=FAIL', "Error=$($_.Exception.Message)") |
        Set-Content -LiteralPath $SummaryPath -Encoding utf8
    Write-Host $_.Exception.Message -ForegroundColor Red
}
finally {
    try {
        & sc.exe query hypersinv 2>&1 | Out-File -LiteralPath (
            Join-Path $RunEvidence 'service-on-exit.txt') -Encoding utf8
    } catch {}
    try {
        @(Get-GateFailureEvents) | Select-Object TimeCreated,ProviderName,Id,
            LevelDisplayName,Message | Export-Clixml -LiteralPath (
                Join-Path $RunEvidence 'failure-events-on-exit.xml')
    } catch {}
    if ($TranscriptStarted) { Stop-Transcript | Out-Null }
    if (Test-Path -LiteralPath $ZipPath) {
        Remove-Item -LiteralPath $ZipPath -Force
    }
    & tar.exe -a -c -f $ZipPath -C $EvidenceRoot $RunName
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Evidence archive failed: $ZipPath"
    } else {
        Write-Host "Evidence: $ZipPath"
    }
}

if ($Failure) { throw $Failure }
if (-not $Passed) { exit 1 }
