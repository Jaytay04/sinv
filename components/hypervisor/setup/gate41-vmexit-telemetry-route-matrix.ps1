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
    'gate41-vmexit-telemetry-route-matrix.ps1'
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

if (-not ('Gate41ProcessorGroups' -as [type])) {
    Add-Type @'
using System.Runtime.InteropServices;
public static class Gate41ProcessorGroups {
    [DllImport("kernel32.dll")]
    public static extern ushort GetActiveProcessorGroupCount();
    [DllImport("kernel32.dll")]
    public static extern uint GetActiveProcessorCount(ushort groupNumber);
}
'@
}

$RunName = 'gate41-vmexit-telemetry-route-matrix-' +
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

function Assert-TelemetryDelta(
    [hashtable]$Before,
    [hashtable]$After,
    [uint64]$Total,
    [uint64]$Cpuid,
    [uint64]$Cr3,
    [uint64]$MsrRead,
    [uint64]$MsrWrite,
    [uint64]$Ept,
    [uint64]$Vmcall,
    [uint64]$Other,
    [string]$Label) {
    for ($Index = 0; $Index -lt 4; $Index++) {
        $A = $After[[uint32]$Index]
        $B = $Before[[uint32]$Index]
        if ($A.Generation -ne $B.Generation) {
            throw "$Label generation changed at index $Index."
        }
        $ExpectedTotal = if ($Index -eq 0) { $Total } else { 0 }
        $ExpectedCpuid = if ($Index -eq 0) { $Cpuid } else { 0 }
        $ExpectedCr3 = if ($Index -eq 0) { $Cr3 } else { 0 }
        $ExpectedMsrRead = if ($Index -eq 0) { $MsrRead } else { 0 }
        $ExpectedMsrWrite = if ($Index -eq 0) { $MsrWrite } else { 0 }
        $ExpectedEpt = if ($Index -eq 0) { $Ept } else { 0 }
        $ExpectedVmcall = if ($Index -eq 0) { $Vmcall } else { 0 }
        $ExpectedOther = if ($Index -eq 0) { $Other } else { 0 }
        if (($A.Total - $B.Total) -ne $ExpectedTotal -or
            ($A.Cpuid - $B.Cpuid) -ne $ExpectedCpuid -or
            ($A.Cr3 - $B.Cr3) -ne $ExpectedCr3 -or
            ($A.MsrRead - $B.MsrRead) -ne $ExpectedMsrRead -or
            ($A.MsrWrite - $B.MsrWrite) -ne $ExpectedMsrWrite -or
            ($A.Ept - $B.Ept) -ne $ExpectedEpt -or
            ($A.Vmcall - $B.Vmcall) -ne $ExpectedVmcall -or
            ($A.Other - $B.Other) -ne $ExpectedOther -or
            ($A.FailClosed - $B.FailClosed) -ne 0) {
            throw "$Label telemetry delta mismatch at index $Index."
        }
    }
}

function Get-Telemetry([string]$Name) {
    $Query = Invoke-Loader $Name 'telemetry-status'
    if ($Query.ExitCode -ne 0 -or
        -not $Query.Stdout.Contains(
            'SUCCESS: Exact VM-exit telemetry verified on 4 processors.')) {
        throw "$Name telemetry query was not verified."
    }
    $Records = Parse-Telemetry $Query.Stdout
    Assert-TelemetrySet $Records
    return $Records
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

    $GroupCount = [Gate41ProcessorGroups]::GetActiveProcessorGroupCount()
    $GroupSizes = for ($Group = 0; $Group -lt $GroupCount; $Group++) {
        [pscustomobject]@{
            Group = $Group
            ActiveProcessors = [Gate41ProcessorGroups]::GetActiveProcessorCount(
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

    $Baseline = Get-Telemetry 'telemetry-baseline'

    $MsrProbe = Invoke-Loader 'msr-read-probe' 'msr-read-probe'
    if ($MsrProbe.ExitCode -ne 0 -or
        -not $MsrProbe.Stdout.Contains(
            'SUCCESS: Persistent MSR read transparency verified.') -or
        -not $MsrProbe.Stdout.Contains(
            'INFO: Authenticated non-root RDMSR guest roundtrip completed.') -or
        -not $MsrProbe.Stdout.Contains(
            'INFO: Authenticated non-root WRMSR discard roundtrip completed.') -or
        -not $MsrProbe.Stdout.Contains(
            'INFO: Authenticated native IA32_EFER RDMSR/WRMSR/RDMSR completed without VM-exit.')) {
        throw 'MSR transparency probe was not verified.'
    }
    $AfterMsr = Get-Telemetry 'telemetry-after-msr'
    Assert-TelemetryDelta $Baseline $AfterMsr 5 0 0 3 1 0 1 0 'MSR'

    $Cr3Probe = Invoke-Loader 'cr3-probe' 'cr3-probe'
    if ($Cr3Probe.ExitCode -ne 0 -or
        -not $Cr3Probe.Stdout.Contains(
            'SUCCESS: One-shot CR3-load VM-exit probe verified.')) {
        throw 'CR3 probe was not verified.'
    }
    $AfterCr3 = Get-Telemetry 'telemetry-after-cr3'
    Assert-TelemetryDelta $AfterMsr $AfterCr3 2 0 1 0 0 0 1 0 'CR3'

    $EptProbe = Invoke-Loader 'ept-violation-roundtrip' `
        'ept-violation-roundtrip'
    if ($EptProbe.ExitCode -ne 0 -or
        -not $EptProbe.Stdout.Contains(
            'SUCCESS: One-shot EPT violation VM-exit roundtrip verified.')) {
        throw 'EPT violation roundtrip was not verified.'
    }
    $AfterEpt = Get-Telemetry 'telemetry-after-ept'
    Assert-TelemetryDelta $AfterCr3 $AfterEpt 3 0 1 0 0 1 1 0 'EPT'

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
        'Probe=VMEXIT_TELEMETRY_ROUTE_MATRIX'
        'Processors=4'
        'TargetSystemIndex=0'
        'TotalDelta=10'
        'CpuidDelta=0'
        'Cr3Delta=2'
        'MsrReadDelta=3'
        'MsrWriteDelta=1'
        'EptDelta=1'
        'VmcallDelta=3'
        'OtherDelta=0'
        'NonTargetDelta=0'
        'FailClosedDelta=0'
        'ServiceAfterUnload=ABSENT'
    ) | Set-Content -LiteralPath $SummaryPath -Encoding utf8
    $Passed = $true
    Write-Host 'PASS: exact MSR, CR3, EPT, and VMCALL telemetry routes were verified.'
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
