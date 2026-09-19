[CmdletBinding()]
param(
    [string]$Stage = 'C:\V3H',
    [string]$EvidenceRoot = 'C:\V3H\evidence'
)

$ErrorActionPreference = 'Stop'
$TotalCycles = 100
$Driver = Join-Path $Stage 'hypervisor_driver.sys'
$DriverPdb = Join-Path $Stage 'hypervisor_driver.pdb'
$Loader = Join-Path $Stage 'hypervisor_loader.exe'
$LoaderPdb = Join-Path $Stage 'hypervisor_loader.pdb'
$Gate36Script = Join-Path $Stage 'gate36-vpid-activation.ps1'
$Gate37Script = Join-Path $Stage 'gate37-vpid-lifecycle-100cycles.ps1'
$Required = @(
    $Driver,
    $DriverPdb,
    $Loader,
    $LoaderPdb,
    $Gate36Script,
    $Gate37Script
)
$Missing = @($Required | Where-Object {
    -not (Test-Path -LiteralPath $_ -PathType Leaf)
})
if ($Missing.Count -ne 0) {
    throw "Missing staged script(s): $($Missing -join ', ')"
}

$Identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$Principal = [Security.Principal.WindowsPrincipal]::new($Identity)
if (-not $Principal.IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from an elevated Windows PowerShell session in the test VM.'
}

if (-not ('Gate37ProcessorGroups' -as [type])) {
    Add-Type @'
using System.Runtime.InteropServices;
public static class Gate37ProcessorGroups
{
    [DllImport("kernel32.dll")]
    public static extern ushort GetActiveProcessorGroupCount();

    [DllImport("kernel32.dll")]
    public static extern uint GetActiveProcessorCount(ushort groupNumber);
}
'@
}

$RunName = 'gate37-vpid-lifecycle-100cycles-' +
    (Get-Date -Format 'yyyyMMdd-HHmmss')
$RunEvidence = Join-Path $EvidenceRoot $RunName
$ZipPath = "$RunEvidence.zip"
$TranscriptPath = Join-Path $RunEvidence 'console.log'
$SummaryPath = Join-Path $RunEvidence 'gate37-summary.txt'
$PowerShellExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$Summary = [System.Collections.Generic.List[string]]::new()
$Failure = $null
$TranscriptStarted = $false
$PassedCycles = 0

function Save-WrapperServiceState(
    [string]$CycleRoot,
    [string]$Name
) {
    $Output = & sc.exe query hypersinv 2>&1
    $ExitCode = $LASTEXITCODE
    $Output | Out-File -LiteralPath (Join-Path $CycleRoot "$Name.txt") -Encoding utf8
    $Output | Write-Host
    [pscustomobject]@{
        ExitCode = $ExitCode
        Output = ($Output -join [Environment]::NewLine)
    }
}

function Get-CycleFailureEvents([datetime]$StartTime) {
    $SystemFailures = @(
        Get-WinEvent -FilterHashtable @{
            LogName = 'System'
            StartTime = $StartTime
        } -ErrorAction SilentlyContinue |
        Where-Object {
            (
                $_.ProviderName -eq 'Microsoft-Windows-Kernel-Power' -and
                $_.Id -eq 41 -and
                $_.Level -le 2
            ) -or (
                $_.ProviderName -eq 'Microsoft-Windows-WHEA-Logger' -and
                $_.Level -le 2
            )
        }
    )
    $ApplicationFailures = @(
        Get-WinEvent -FilterHashtable @{
            LogName = 'Application'
            StartTime = $StartTime
        } -ErrorAction SilentlyContinue |
        Where-Object {
            $_.ProviderName -eq 'Application Error' -and
            $_.Id -eq 1000 -and
            $_.Level -le 2 -and
            $_.Message -match '(?i)\b(hypervisor_loader\.exe|powershell\.exe)\b'
        }
    )
    @($SystemFailures + $ApplicationFailures)
}

New-Item -ItemType Directory -Force -Path $RunEvidence | Out-Null

try {
    Start-Transcript -LiteralPath $TranscriptPath -Force | Out-Null
    $TranscriptStarted = $true

    Write-Host '=== GATE 37 BASELINE ==='
    "Stage=$Stage"
    "RunEvidence=$RunEvidence"
    Get-FileHash -LiteralPath $Required -Algorithm SHA256 |
        Format-List Path, Hash

    $GroupCount = [Gate37ProcessorGroups]::GetActiveProcessorGroupCount()
    $GroupSizes = @(
        for ($Group = 0; $Group -lt $GroupCount; $Group++) {
            [pscustomobject]@{
                Group = $Group
                ActiveProcessors =
                    [Gate37ProcessorGroups]::GetActiveProcessorCount(
                        [uint16]$Group)
            }
        }
    )
    $GroupSizes | Format-Table -AutoSize
    $TotalProcessors =
        ($GroupSizes | Measure-Object ActiveProcessors -Sum).Sum
    if (
        ($GroupCount -ne 2) -or
        ($TotalProcessors -ne 4) -or
        (@($GroupSizes |
            Where-Object ActiveProcessors -ne 2).Count -ne 0)
    ) {
        throw "Expected exactly 2 processor groups with 2 processors each; groups=$GroupCount total=$TotalProcessors."
    }

    $Recovery = Get-CimInstance Win32_OSRecoveryConfiguration
    $Recovery |
        Select-Object DebugInfoType, AutoReboot, WriteDebugInfo,
            DebugFilePath, OverwriteExistingDebugFile |
        Format-List
    if (
        ($Recovery.DebugInfoType -ne 2) -or
        $Recovery.AutoReboot -or
        (-not $Recovery.WriteDebugInfo)
    ) {
        throw 'Crash-dump configuration is incorrect.'
    }

    for ($Cycle = 1; $Cycle -le $TotalCycles; $Cycle++) {
        Write-Host
        Write-Host "========== CYCLE $Cycle/$TotalCycles =========="
        $CycleStart = Get-Date
        $CycleRoot = Join-Path $RunEvidence ('cycle-{0:D3}' -f $Cycle)
        New-Item -ItemType Directory -Force -Path $CycleRoot | Out-Null

        $BeforeService =
            Save-WrapperServiceState $CycleRoot 'service-before-wrapper'
        if ($BeforeService.ExitCode -ne 1060) {
            throw "Cycle $($Cycle): service was present before LOAD. Stop without recovery."
        }

        $Stdout = Join-Path $CycleRoot 'runner-stdout.txt'
        $Stderr = Join-Path $CycleRoot 'runner-stderr.txt'
        $Quote = [char]34
        $Arguments =
            "-NoProfile -ExecutionPolicy Bypass " +
            "-File $Quote$Gate36Script$Quote " +
            "-Stage $Quote$Stage$Quote " +
            "-EvidenceRoot $Quote$CycleRoot$Quote"
        $StartArguments = @{
            FilePath = $PowerShellExe
            ArgumentList = $Arguments
            Wait = $true
            PassThru = $true
            NoNewWindow = $true
            RedirectStandardOutput = $Stdout
            RedirectStandardError = $Stderr
        }
        $Process = Start-Process @StartArguments

        if (Test-Path -LiteralPath $Stdout) {
            Get-Content -LiteralPath $Stdout | Write-Host
        }
        if (Test-Path -LiteralPath $Stderr) {
            Get-Content -LiteralPath $Stderr | Write-Host
        }
        if ($Process.ExitCode -ne 0) {
            throw "Cycle $($Cycle): Gate 36 failed with exit $($Process.ExitCode). Do not retry or unload manually."
        }

        $RunnerStderrText = Get-Content -Raw -LiteralPath $Stderr
        if (-not [string]::IsNullOrWhiteSpace($RunnerStderrText)) {
            throw "Cycle $($Cycle): Gate 36 emitted stderr despite a zero exit code."
        }

        $InnerSummaries = @(
            Get-ChildItem -LiteralPath $CycleRoot -Recurse -File |
            Where-Object Name -eq 'summary.txt'
        )
        if ($InnerSummaries.Count -ne 1) {
            throw "Cycle $($Cycle): expected exactly one Gate 36 summary; found $($InnerSummaries.Count)."
        }

        $InnerText =
            Get-Content -Raw -LiteralPath $InnerSummaries[0].FullName
        foreach ($Proof in @(
            'Result=PASS',
            'Probe=VPID_ACTIVATION',
            'Processors=4',
            'Groups=2',
            'Vpids=1,2,3,4',
            'LiveVmcsVpid=VERIFIED',
            'InvvpidBeforeActivation=VERIFIED_BY_RUNTIME',
            'InvvpidBeforeReuse=VERIFIED_BY_UNLOAD',
            'ServiceAfterUnload=ABSENT'
        )) {
            if ($InnerText -notmatch
                ('(?m)^' + [regex]::Escape($Proof) + '\s*$')) {
                throw "Cycle $($Cycle): Gate 36 summary lacks exact proof: $Proof"
            }
        }

        $VpidOutputs = @(
            Get-ChildItem -LiteralPath $CycleRoot -Recurse -File |
            Where-Object Name -eq 'vpid-status-stdout.txt'
        )
        if ($VpidOutputs.Count -ne 1) {
            throw "Cycle $($Cycle): expected exactly one VPID status output; found $($VpidOutputs.Count)."
        }

        $VpidText = Get-Content -Raw -LiteralPath $VpidOutputs[0].FullName
        $ExpectedVpidLines = @(
            'VPID CPU: index=0 group=0 number=0 vpid=1 observed=1 owner=1 flags=0x0000001F capabilities=0x0000001F secondary=0x00000022',
            'VPID CPU: index=1 group=0 number=1 vpid=2 observed=2 owner=1 flags=0x0000001F capabilities=0x0000001F secondary=0x00000022',
            'VPID CPU: index=2 group=1 number=0 vpid=3 observed=3 owner=1 flags=0x0000001F capabilities=0x0000001F secondary=0x00000022',
            'VPID CPU: index=3 group=1 number=1 vpid=4 observed=4 owner=1 flags=0x0000001F capabilities=0x0000001F secondary=0x00000022',
            'SUCCESS: Exact ACTIVE VPID state verified on 4 processors.'
        )
        foreach ($ExpectedVpidLine in $ExpectedVpidLines) {
            if ($VpidText -notmatch
                ('(?m)^' + [regex]::Escape($ExpectedVpidLine) + '\s*$')) {
                throw "Cycle $($Cycle): VPID status lacks exact proof: $ExpectedVpidLine"
            }
        }

        $InnerArchives = @(
            Get-ChildItem -LiteralPath $CycleRoot -Recurse -File |
            Where-Object Name -like 'gate36-vpid-activation-*.zip'
        )
        if ($InnerArchives.Count -ne 1) {
            throw "Cycle $($Cycle): expected exactly one Gate 36 archive; found $($InnerArchives.Count)."
        }

        $AfterService =
            Save-WrapperServiceState $CycleRoot 'service-after-wrapper'
        if ($AfterService.ExitCode -ne 1060) {
            throw "Cycle $($Cycle): service removal was not confirmed."
        }

        $FailureEvents = @(Get-CycleFailureEvents $CycleStart)
        $FailureEvents |
            Select-Object TimeCreated, ProviderName, Id,
                LevelDisplayName, Message |
            Export-Clixml -LiteralPath (
                Join-Path $CycleRoot 'failure-events.xml')
        if ($FailureEvents.Count -ne 0) {
            throw "Cycle $($Cycle): crash or application-failure event detected."
        }

        $Dump = Get-Item -LiteralPath 'C:\Windows\MEMORY.DMP' -ErrorAction SilentlyContinue
        if ($Dump -and $Dump.LastWriteTimeUtc -gt $CycleStart.ToUniversalTime()) {
            throw "Cycle $($Cycle): a new MEMORY.DMP was generated."
        }

        $PassedCycles++
        $Summary.Add(
            ('Cycle{0:D3}=PASS;Vpids=1,2,3,4;' +
             'LiveVmcsVpid=VERIFIED;' +
             'Invalidation=ACTIVATION/RETIREMENT;' +
             'ServiceAfterUnload=ABSENT') -f $Cycle)
        Write-Host "PASS: cycle $Cycle/$TotalCycles"
    }

    $Summary.Insert(0, 'Result=PASS')
    $Summary.Add("PassedCycles=$PassedCycles")
}
catch {
    $Failure = $_
    $Summary.Insert(0, 'Result=FAIL')
    $Summary.Add("PassedCycles=$PassedCycles")
    $Summary.Add("Error=$($_.Exception.Message)")
    Write-Host $_.Exception.Message -ForegroundColor Red
}
finally {
    $Summary | Set-Content -LiteralPath $SummaryPath -Encoding utf8

    if ($TranscriptStarted) {
        Stop-Transcript | Out-Null
    }

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

if ($Failure) {
    throw $Failure
}

Write-Host 'PASS: all 100 VPID lifecycle cycles completed.'
