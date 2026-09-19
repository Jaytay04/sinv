[CmdletBinding()]
param(
    [string]$Stage = 'C:\V3H',
    [string]$EvidenceRoot = 'C:\V3H\evidence'
)

$ErrorActionPreference = 'Stop'
# This operator-only gate deliberately triggers one supervised write EPT violation.
$Driver = Join-Path $Stage 'hypervisor_driver.sys'
$DriverPdb = Join-Path $Stage 'hypervisor_driver.pdb'
$Loader = Join-Path $Stage 'hypervisor_loader.exe'
$LoaderPdb = Join-Path $Stage 'hypervisor_loader.pdb'
$GateScript = Join-Path $Stage 'gate11-ept-violation-roundtrip.ps1'
$Required = @($Driver, $DriverPdb, $Loader, $LoaderPdb, $GateScript)
$Missing = @($Required | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
if ($Missing.Count -ne 0) { throw "Missing staged artifact(s): $($Missing -join ', ')" }

$Identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$Principal = [Security.Principal.WindowsPrincipal]::new($Identity)
if (-not $Principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from an elevated Windows PowerShell session in the test VM.'
}

$RunName = 'gate11-ept-violation-roundtrip-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
$RunEvidence = Join-Path $EvidenceRoot $RunName
$TranscriptPath = Join-Path $RunEvidence 'console.log'
$ZipPath = "$RunEvidence.zip"
$GateStart = Get-Date
$TranscriptStarted = $false
$Passed = $false

function Save-ServiceState([string]$Name) {
    $Path = Join-Path $RunEvidence "$Name.txt"
    $Output = & sc.exe query hypersinv 2>&1
    $Code = $LASTEXITCODE
    $Output | Out-File -LiteralPath $Path -Encoding utf8
    $Output | Write-Host
    [pscustomobject]@{ ExitCode = $Code; Output = ($Output -join [Environment]::NewLine) }
}

function Save-FailureEvidence {
    try { & sc.exe query hypersinv 2>&1 | Out-File (Join-Path $RunEvidence 'service-on-exit.txt') -Encoding utf8 } catch {}
    try { & sc.exe qc hypersinv 2>&1 | Out-File (Join-Path $RunEvidence 'service-config-on-exit.txt') -Encoding utf8 } catch {}
    try {
        Get-WinEvent -FilterHashtable @{ LogName = 'System'; StartTime = $GateStart } -ErrorAction Stop |
            Where-Object { $_.ProviderName -in @('Service Control Manager', 'Microsoft-Windows-WER-SystemErrorReporting', 'Microsoft-Windows-Kernel-Power') } |
            Select-Object TimeCreated, ProviderName, Id, LevelDisplayName, Message |
            Export-Clixml -LiteralPath (Join-Path $RunEvidence 'system-events.xml')
    } catch { $_ | Out-File (Join-Path $RunEvidence 'system-events-error.txt') }
    try {
        Get-WinEvent -FilterHashtable @{ LogName = 'Application'; StartTime = $GateStart } -ErrorAction Stop |
            Where-Object { $_.ProviderName -in @('Application Error', 'Windows Error Reporting') } |
            Select-Object TimeCreated, ProviderName, Id, LevelDisplayName, Message |
            Export-Clixml -LiteralPath (Join-Path $RunEvidence 'application-events.xml')
    } catch { $_ | Out-File (Join-Path $RunEvidence 'application-events-error.txt') }
}

New-Item -ItemType Directory -Force -Path $RunEvidence | Out-Null
try {
    Start-Transcript -LiteralPath $TranscriptPath -Force | Out-Null
    $TranscriptStarted = $true
    "GateStart=$($GateStart.ToString('o'))"
    "Stage=$Stage"
    "RunEvidence=$RunEvidence"

    Write-Host "`n=== BASELINE ==="
    Get-ComputerInfo | Select-Object WindowsProductName, WindowsVersion, OsBuildNumber | Format-List
    Get-CimInstance Win32_ComputerSystem |
        Select-Object Manufacturer, Model, NumberOfLogicalProcessors | Format-List
    Get-CimInstance Win32_Processor |
        Select-Object Name, NumberOfCores, NumberOfLogicalProcessors | Format-List
    try {
        Get-CimInstance -Namespace root\Microsoft\Windows\DeviceGuard -ClassName Win32_DeviceGuard |
            Format-List *
    } catch { Write-Warning "DeviceGuard query unavailable: $($_.Exception.Message)" }
    bcdedit.exe /enum '{current}'
    Get-Item -LiteralPath $Required | Select-Object FullName, Length, LastWriteTime | Format-List
    Get-FileHash -LiteralPath $Required -Algorithm SHA256 | Format-List Path, Hash
    Get-AuthenticodeSignature -LiteralPath $Driver |
        Select-Object Status, StatusMessage, @{Name='Signer';Expression={$_.SignerCertificate.Subject}} |
        Format-List

    if (-not ('Gate11ProcessorGroups' -as [type])) {
        Add-Type @'
using System.Runtime.InteropServices;
public static class Gate11ProcessorGroups {
    [DllImport("kernel32.dll")]
    public static extern ushort GetActiveProcessorGroupCount();
    [DllImport("kernel32.dll")]
    public static extern uint GetActiveProcessorCount(ushort groupNumber);
}
'@
    }
    $GroupCount = [Gate11ProcessorGroups]::GetActiveProcessorGroupCount()
    $GroupSizes = for ($Group = 0; $Group -lt $GroupCount; ++$Group) {
        [pscustomobject]@{
            Group = $Group
            ActiveProcessors = [Gate11ProcessorGroups]::GetActiveProcessorCount([uint16]$Group)
        }
    }
    $GroupSizes | Format-Table -AutoSize
    $Processors = [int](($GroupSizes | Measure-Object ActiveProcessors -Sum).Sum)
    if ($GroupCount -ne 2 -or $Processors -ne 4 -or
        @($GroupSizes | Where-Object ActiveProcessors -ne 2).Count -ne 0) {
        throw "Gate 11 requires exactly two processor groups with two processors each; groups=$GroupCount processors=$Processors."
    }
    Write-Host 'PASS: 2 groups / 4 processors verified.'
    $Before = Save-ServiceState 'service-before'
    if ($Before.ExitCode -ne 1060) {
        throw "Service was present before LOAD (sc.exe exit $($Before.ExitCode)). Do not continue."
    }

    Write-Host "`n=== LOAD ==="
    $LoadOut = Join-Path $RunEvidence 'load-stdout.txt'
    $LoadErr = Join-Path $RunEvidence 'load-stderr.txt'
    $Load = Start-Process -FilePath $Loader -ArgumentList 'load' -Wait -PassThru -NoNewWindow `
        -RedirectStandardOutput $LoadOut -RedirectStandardError $LoadErr
    Get-Content -LiteralPath $LoadOut | Write-Host
    Get-Content -LiteralPath $LoadErr | Write-Host
    $LoadText = Get-Content -Raw -LiteralPath $LoadOut
    $RuntimeProof = "SUCCESS: Hypervisor loaded on $Processors cores and verified by exact V3 status."
    $EptProof = 'INFO: Exact EPT READY status is verified.'
    if ($Load.ExitCode -ne 0 -or -not $LoadText.Contains($RuntimeProof) -or -not $LoadText.Contains($EptProof)) {
        throw "LOAD did not prove canonical Runtime LOADED and EPT READY (exit $($Load.ExitCode)). Stop; do not retry or run unload manually."
    }
    Save-ServiceState 'service-after-load' | Out-Null

    Write-Host "`n=== ONE-SHOT EPT VIOLATION ROUNDTRIP ==="
    $RoundtripOut = Join-Path $RunEvidence 'hook-roundtrip-stdout.txt'
    $RoundtripErr = Join-Path $RunEvidence 'hook-roundtrip-stderr.txt'
    $Roundtrip = Start-Process -FilePath $Loader -ArgumentList 'ept-violation-roundtrip' -Wait -PassThru -NoNewWindow -RedirectStandardOutput $RoundtripOut -RedirectStandardError $RoundtripErr
    Get-Content -LiteralPath $RoundtripOut | Write-Host
    Get-Content -LiteralPath $RoundtripErr | Write-Host
    $RoundtripText = Get-Content -Raw -LiteralPath $RoundtripOut
    $RoundtripProof = 'SUCCESS: One-shot EPT violation VM-exit roundtrip verified.'
    if ($Roundtrip.ExitCode -ne 0 -or -not $RoundtripText.Contains($RoundtripProof)) {
        throw "EPT violation roundtrip did not prove route/restore/INVEPT/resume/remove/release (exit $($Roundtrip.ExitCode)). Stop; do not retry or run unload manually."
    }

    Write-Host "`n=== UNLOAD ==="
    $UnloadOut = Join-Path $RunEvidence 'unload-stdout.txt'
    $UnloadErr = Join-Path $RunEvidence 'unload-stderr.txt'
    $Unload = Start-Process -FilePath $Loader -ArgumentList 'unload' -Wait -PassThru -NoNewWindow `
        -RedirectStandardOutput $UnloadOut -RedirectStandardError $UnloadErr
    Get-Content -LiteralPath $UnloadOut | Write-Host
    Get-Content -LiteralPath $UnloadErr | Write-Host
    $UnloadText = Get-Content -Raw -LiteralPath $UnloadOut
    if ($Unload.ExitCode -ne 0 -or -not $UnloadText.Contains('SUCCESS: Hypervisor unloaded')) {
        throw "UNLOAD did not prove canonical Runtime/EPT detached cleanup (exit $($Unload.ExitCode)). Stop; do not retry."
    }
    $After = Save-ServiceState 'service-after-unload'
    if ($After.ExitCode -ne 1060) {
        throw "Service removal was not confirmed (sc.exe exit $($After.ExitCode))."
    }

    $CrashDumps = @(Get-ChildItem -LiteralPath (Join-Path $env:LOCALAPPDATA 'CrashDumps') `
        -Filter 'hypervisor_loader*.dmp' -ErrorAction SilentlyContinue |
        Where-Object LastWriteTime -ge $GateStart)
    $MemoryDump = Get-Item -LiteralPath "$env:SystemRoot\MEMORY.DMP" -ErrorAction SilentlyContinue
    if ($CrashDumps.Count -ne 0 -or ($MemoryDump -and $MemoryDump.LastWriteTime -ge $GateStart)) {
        throw 'A new loader or kernel crash dump was detected.'
    }

    @('Result=PASS','ViolationRoundtrip=PASS') | Out-File -LiteralPath (Join-Path $RunEvidence 'summary.txt') -Encoding utf8
    $Passed = $true
    Write-Host 'PASS: Gate 11 one-shot EPT-violation VM-exit route and clean unload completed.'
}
catch {
    "Result=FAIL`nError=$($_.Exception.Message)" |
        Out-File -LiteralPath (Join-Path $RunEvidence 'summary.txt') -Encoding utf8
    Write-Error $_
}
finally {
    Save-FailureEvidence
    if ($TranscriptStarted) { Stop-Transcript | Out-Null }
    if (Test-Path -LiteralPath $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }
    & tar.exe -a -c -f $ZipPath -C $EvidenceRoot $RunName
    if ($LASTEXITCODE -ne 0) { Write-Warning "Evidence archive failed: $ZipPath" }
    else { Write-Host "Evidence: $ZipPath" }
}

if (-not $Passed) { exit 1 }
