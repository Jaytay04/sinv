[CmdletBinding()]
param(
    [string]$Stage = 'C:\V3H',
    [string]$EvidenceRoot = 'C:\V3H\evidence'
)

$ErrorActionPreference = 'Stop'

# Operator-only VPID/CR3 integration gate. Never run on a host machine.
$Driver = Join-Path $Stage 'hypervisor_driver.sys'
$DriverPdb = Join-Path $Stage 'hypervisor_driver.pdb'
$Loader = Join-Path $Stage 'hypervisor_loader.exe'
$LoaderPdb = Join-Path $Stage 'hypervisor_loader.pdb'
$GateScript = Join-Path $Stage 'gate38-vpid-cr3-residency-interoperability.ps1'
$ManifestPath = Join-Path $Stage 'SHA256SUMS.txt'
$Required = @(
    $Driver, $DriverPdb, $Loader, $LoaderPdb, $GateScript, $ManifestPath
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

if (-not ('Gate38ProcessorGroups' -as [type])) {
    Add-Type @'
using System.Runtime.InteropServices;
public static class Gate38ProcessorGroups {
    [DllImport("kernel32.dll")]
    public static extern ushort GetActiveProcessorGroupCount();

    [DllImport("kernel32.dll")]
    public static extern uint GetActiveProcessorCount(ushort groupNumber);
}
'@
}

$RunName = 'gate38-vpid-cr3-residency-interoperability-' +
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

function Get-ExactVpidRecords([string]$Text) {
    $Lines = @($Text -split "`r?`n" | Where-Object {
        $_ -like 'VPID CPU: *'
    })
    $Expected = @(
        'VPID CPU: index=0 group=0 number=0 vpid=1 observed=1 owner=1 flags=0x0000001F capabilities=0x0000001F secondary=0x00000022'
        'VPID CPU: index=1 group=0 number=1 vpid=2 observed=2 owner=1 flags=0x0000001F capabilities=0x0000001F secondary=0x00000022'
        'VPID CPU: index=2 group=1 number=0 vpid=3 observed=3 owner=1 flags=0x0000001F capabilities=0x0000001F secondary=0x00000022'
        'VPID CPU: index=3 group=1 number=1 vpid=4 observed=4 owner=1 flags=0x0000001F capabilities=0x0000001F secondary=0x00000022'
    )
    if ($Lines.Count -ne 4) {
        throw "Expected four VPID CPU records; observed $($Lines.Count)."
    }
    for ($Index = 0; $Index -lt $Expected.Count; $Index++) {
        if ($Lines[$Index] -ne $Expected[$Index]) {
            throw "Unexpected VPID record at index ${Index}: $($Lines[$Index])"
        }
    }
    $Lines
}

function Assert-StageManifest {
    $ExpectedNames = @(
        'gate38-vpid-cr3-residency-interoperability.ps1'
        'hypervisor_driver.pdb'
        'hypervisor_driver.sys'
        'hypervisor_loader.exe'
        'hypervisor_loader.pdb'
    )
    $Records = @{}
    foreach ($Line in @(Get-Content -LiteralPath $ManifestPath)) {
        if ($Line -notmatch '^([0-9A-Fa-f]{64})  ([^\\/]+)$') {
            throw "Malformed manifest record: $Line"
        }
        $Name = $Matches[2]
        if ($Records.ContainsKey($Name)) {
            throw "Duplicate manifest record: $Name"
        }
        $Records[$Name] = $Matches[1].ToUpperInvariant()
    }
    if ($Records.Count -ne $ExpectedNames.Count) {
        throw "Expected exactly $($ExpectedNames.Count) manifest records."
    }
    foreach ($Name in $ExpectedNames) {
        if (-not $Records.ContainsKey($Name)) {
            throw "Manifest record missing: $Name"
        }
        $Actual = (Get-FileHash -LiteralPath (Join-Path $Stage $Name) `
            -Algorithm SHA256).Hash
        if ($Actual -ne $Records[$Name]) {
            throw "Manifest hash mismatch: $Name"
        }
    }
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

    Write-Host "`n=== BASELINE ==="
    Assert-StageManifest
    $Signature = Get-AuthenticodeSignature -LiteralPath $Driver
    $Signature | Select-Object Status, StatusMessage,
        @{Name='Signer'; Expression={$_.SignerCertificate.Subject}} |
        Format-List
    if ($null -eq $Signature.SignerCertificate -or
        $Signature.SignerCertificate.Subject -ne 'CN=hypersinv' -or
        ($Signature.Status -ne
            [Management.Automation.SignatureStatus]::Valid -and
         $Signature.Status -ne
            [Management.Automation.SignatureStatus]::UnknownError)) {
        throw 'Driver is not signed by the expected CN=hypersinv test certificate.'
    }

    Get-ComputerInfo |
        Select-Object WindowsProductName, WindowsVersion, OsBuildNumber |
        Format-List
    Get-CimInstance Win32_ComputerSystem |
        Select-Object Manufacturer, Model, NumberOfLogicalProcessors |
        Format-List

    $GroupCount = [Gate38ProcessorGroups]::GetActiveProcessorGroupCount()
    $GroupSizes = @(for ($Group = 0; $Group -lt $GroupCount; $Group++) {
        [pscustomobject]@{
            Group = $Group
            ActiveProcessors =
                [Gate38ProcessorGroups]::GetActiveProcessorCount(
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
    $Recovery | Select-Object DebugInfoType, AutoReboot, WriteDebugInfo,
        DebugFilePath, OverwriteExistingDebugFile | Format-List
    if ($Recovery.DebugInfoType -ne 2 -or $Recovery.AutoReboot -or
        -not $Recovery.WriteDebugInfo) {
        throw 'Crash-dump configuration is incorrect.'
    }

    bcdedit.exe /enum '{current}'
    Get-Item -LiteralPath $Required |
        Select-Object FullName, Length, LastWriteTime | Format-List
    Get-FileHash -LiteralPath $Required -Algorithm SHA256 |
        Format-List Path, Hash

    $BeforeService = Save-ServiceState 'service-before'
    if ($BeforeService.ExitCode -ne 1060) {
        throw "Service was present before LOAD (exit $($BeforeService.ExitCode))."
    }

    Write-Host "`n=== LOAD ==="
    $Load = Invoke-Loader 'load' 'load'
    $LoadProof =
        "SUCCESS: Hypervisor loaded on $TotalProcessors cores and verified by exact V3 status."
    if ($Load.ExitCode -ne 0 -or -not $Load.Stdout.Contains($LoadProof) -or
        -not $Load.Stdout.Contains(
            'INFO: Exact EPT READY status is verified.')) {
        throw "LOAD was not verified (exit $($Load.ExitCode)). STOP; do not retry or unload manually."
    }
    $AfterLoad = Save-ServiceState 'service-after-load'
    if ($AfterLoad.ExitCode -ne 0 -or
        -not $AfterLoad.Output.Contains('STATE              : 4  RUNNING')) {
        throw 'Driver service was not running after LOAD. STOP; do not unload manually.'
    }

    Write-Host "`n=== VPID STATUS BEFORE CR3 PROBE ==="
    $VpidBefore = Invoke-Loader 'vpid-before' 'vpid-status'
    if ($VpidBefore.ExitCode -ne 0 -or
        -not $VpidBefore.Stdout.Contains(
            'SUCCESS: Exact ACTIVE VPID state verified on 4 processors.')) {
        throw 'Pre-probe ACTIVE VPID status was not verified.'
    }
    $VpidBeforeRecords = @(Get-ExactVpidRecords $VpidBefore.Stdout)

    Write-Host "`n=== CR3 STATUS BEFORE RESIDENCY PROBE ==="
    $Cr3Before = Invoke-Loader 'cr3-before' 'cr3-status'
    $Cr3BeforeHeader = [regex]::Match($Cr3Before.Stdout,
        'CR3 Status: class=INACTIVE .* targets=1 .* residents=0 transitions=0')
    if ($Cr3Before.ExitCode -ne 0 -or -not $Cr3BeforeHeader.Success -or
        -not $Cr3Before.Stdout.Contains(
            'SUCCESS: Exact redacted CR3 status snapshot verified (INACTIVE).')) {
        throw 'Pre-probe CR3 status did not prove the inactive target baseline.'
    }

    Write-Host "`n=== CR3 TARGET RESIDENCY ROUNDTRIP ==="
    $Probe = Invoke-Loader 'cr3-residency-probe' 'cr3-residency-probe'
    $ProbeLine = [regex]::Match($Probe.Stdout,
        'CR3 Residency Probe: status=0x([0-9A-Fa-f]{8}) stage=(\d+) cpu=(\d+):(\d+) transitions=(\d+)/(\d+)/(\d+) residency=(\d+)/(\d+)/(\d+) matched=(\d+)/(\d+)/(\d+) writes=(\d+)/(\d+)/(\d+)')
    if ($Probe.ExitCode -ne 0 -or -not $ProbeLine.Success -or
        -not $Probe.Stdout.Contains(
            'SUCCESS: Exact redacted CR3 target ENTER/LEAVE roundtrip verified.')) {
        throw "CR3 target residency probe was not verified (exit $($Probe.ExitCode))."
    }

    $BeforeTransitions = [uint64]$ProbeLine.Groups[5].Value
    $TargetTransitions = [uint64]$ProbeLine.Groups[6].Value
    $AfterTransitions = [uint64]$ProbeLine.Groups[7].Value
    $Residency = ($ProbeLine.Groups[8..10].Value) -join '/'
    $Matched = ($ProbeLine.Groups[11..13].Value) -join '/'
    $Writes = ($ProbeLine.Groups[14..16].Value) -join '/'
    if ($ProbeLine.Groups[1].Value -ne '00000000' -or
        [int]$ProbeLine.Groups[2].Value -ne 7 -or
        $BeforeTransitions -ge $TargetTransitions -or
        $TargetTransitions -ge $AfterTransitions -or
        $Residency -ne '0/1/0' -or
        $Matched -ne '4294967295/0/4294967295' -or
        $Writes -ne '1/1/1') {
        throw 'CR3 target residency response failed exact ENTER/LEAVE validation.'
    }

    Write-Host "`n=== CR3 STATUS AFTER RESIDENCY PROBE ==="
    $Cr3After = Invoke-Loader 'cr3-after' 'cr3-status'
    $Cr3AfterHeader = [regex]::Match($Cr3After.Stdout,
        'CR3 Status: class=OBSERVED .* targets=1 .* residents=0 transitions=(\d+)')
    if ($Cr3After.ExitCode -ne 0 -or -not $Cr3AfterHeader.Success -or
        [uint64]$Cr3AfterHeader.Groups[1].Value -lt $AfterTransitions) {
        throw 'Post-probe CR3 status did not preserve the final nonresident state.'
    }

    Write-Host "`n=== VPID STATUS AFTER CR3 PROBE ==="
    $VpidAfter = Invoke-Loader 'vpid-after' 'vpid-status'
    if ($VpidAfter.ExitCode -ne 0 -or
        -not $VpidAfter.Stdout.Contains(
            'SUCCESS: Exact ACTIVE VPID state verified on 4 processors.')) {
        throw 'Post-probe ACTIVE VPID status was not verified.'
    }
    $VpidAfterRecords = @(Get-ExactVpidRecords $VpidAfter.Stdout)
    if (($VpidBeforeRecords -join "`n") -cne
        ($VpidAfterRecords -join "`n")) {
        throw 'VPID identity drifted across the CR3 residency roundtrip.'
    }

    Write-Host "`n=== UNLOAD ==="
    $Unload = Invoke-Loader 'unload' 'unload'
    if ($Unload.ExitCode -ne 0 -or
        -not $Unload.Stdout.Contains('SUCCESS: Hypervisor unloaded')) {
        throw "UNLOAD was not verified (exit $($Unload.ExitCode)). STOP; do not retry."
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
        'Probe=VPID_CR3_RESIDENCY_INTEROPERABILITY'
        'Processors=4'
        'Groups=2'
        'VpidsBefore=1,2,3,4'
        'VpidsAfter=1,2,3,4'
        'VpidIdentityStable=VERIFIED'
        'Cr3Residency=0/1/0'
        'Cr3Matched=4294967295/0/4294967295'
        'Cr3Writes=1/1/1'
        "BeforeTransitions=$BeforeTransitions"
        "TargetTransitions=$TargetTransitions"
        "AfterTransitions=$AfterTransitions"
        'ServiceAfterUnload=ABSENT'
    ) | Set-Content -LiteralPath $SummaryPath -Encoding utf8
    $Passed = $true
    Write-Host 'PASS: stable per-CPU VPID identity, exact CR3 ENTER/LEAVE residency, canonical unload, and service absence were verified.'
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
    catch {
        $_ | Out-File -LiteralPath `
            (Join-Path $RunEvidence 'failure-events-on-exit-error.txt')
    }
    if ($TranscriptStarted) { Stop-Transcript | Out-Null }
    if (Test-Path -LiteralPath $ZipPath) {
        Remove-Item -LiteralPath $ZipPath -Force
    }
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
