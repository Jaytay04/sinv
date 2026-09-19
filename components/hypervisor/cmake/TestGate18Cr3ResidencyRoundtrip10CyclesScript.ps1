param(
    [Parameter(Mandatory=$true)][string]$ScriptPath,
    [Parameter(Mandatory=$true)][string]$Gate17Path,
    [Parameter(Mandatory=$true)][string]$DocPath,
    [Parameter(Mandatory=$true)][string]$TestsCMake
)

$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-Ordered(
    [string]$Source,
    [string[]]$Tokens,
    [string]$Name
) {
    $Last = -1
    foreach ($Token in $Tokens) {
        $At = $Source.IndexOf(
            $Token, $Last + 1, [StringComparison]::Ordinal)
        Assert-True ($At -gt $Last) "$Name order changed at: $Token"
        $Last = $At
    }
}

function Verify([string]$Source, [string]$Doc, [string]$CMake) {
    $Tokens = $null
    $Errors = $null
    [void][Management.Automation.Language.Parser]::ParseInput(
        $Source, [ref]$Tokens, [ref]$Errors)
    Assert-True ($Errors.Count -eq 0) 'Gate 18 script does not parse'

    foreach ($Token in @(
        '$TotalCycles = 10',
        '$Gate17Script = Join-Path $Stage ''gate17-cr3-residency-roundtrip.ps1''',
        'Gate18ProcessorGroups',
        '[uint16]$Group',
        '$GroupCount -ne 2',
        '$TotalProcessors -ne 4',
        '$Recovery.DebugInfoType -ne 2',
        '$Recovery.AutoReboot',
        '-not $Recovery.WriteDebugInfo',
        'Start-Transcript',
        '$Cycle -le $TotalCycles',
        'Save-WrapperServiceState $CycleRoot ''service-before-wrapper''',
        '$BeforeService.ExitCode -ne 1060',
        'Start-Process @StartArguments',
        '$Process.ExitCode -ne 0',
        '$InnerSummaries.Count -ne 1',
        '''Result=PASS''',
        '''ProbeStage=COMPLETE''',
        '''Residency=0/1/0''',
        '''Matched=4294967295/0/4294967295''',
        '''Writes=1/1/1''',
        '''BeforeTransitions=1''',
        '''TargetTransitions=2''',
        '''AfterTransitions=3''',
        'Save-WrapperServiceState $CycleRoot ''service-after-wrapper''',
        '$AfterService.ExitCode -ne 1060',
        'Get-CycleFailureEvents $CycleStart',
        '''Microsoft-Windows-Kernel-Power''',
        '$_.Id -eq 41',
        '''Microsoft-Windows-WHEA-Logger''',
        '$_.Level -le 2',
        '''Application Error''',
        '$_.Id -eq 1000',
        '''(?i)\b(hypervisor_loader\.exe|powershell\.exe)\b''',
        '$FailureEvents.Count -ne 0',
        'C:\Windows\MEMORY.DMP',
        '$Dump.LastWriteTimeUtc -gt $CycleStart.ToUniversalTime()',
        'PassedCycles=$PassedCycles',
        'Stop-Transcript',
        'tar.exe -a -c -f $ZipPath',
        'throw $Failure'
    )) {
        Assert-True ($Source.Contains($Token)) "Gate 18 missing token: $Token"
    }

    Assert-Ordered $Source @(
        '$BeforeService.ExitCode -ne 1060',
        'Start-Process @StartArguments',
        '$Process.ExitCode -ne 0',
        '$InnerSummaries.Count -ne 1',
        '''Result=PASS''',
        '''Residency=0/1/0''',
        '''BeforeTransitions=1''',
        '''TargetTransitions=2''',
        '''AfterTransitions=3''',
        '$AfterService.ExitCode -ne 1060',
        '$FailureEvents.Count -ne 0',
        '$Dump.LastWriteTimeUtc -gt $CycleStart.ToUniversalTime()',
        '$PassedCycles++'
    ) 'Gate 18 cycle'

    Assert-True (
        ([regex]::Matches(
            $Source, '(?i)hypervisor_loader\\\.exe')).Count -eq 1
    ) 'Gate 18 invokes the loader directly or lost its crash-event scope'

    Assert-True ($Source -notmatch
        '(?i)hypervisor_loader(?!\\\.exe)') 'Gate wrapper contains an unscoped direct loader reference'

    Assert-True ($Source -notmatch
        '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') 'Gate 18 bypasses Gate 17 or mutates signing, boot, SCM, or reboot state'

    foreach ($Token in @(
        'Gate 18: 10-cycle CR3 target-residency roundtrip',
        'gate18-cr3-residency-roundtrip-10cycles.ps1',
        'PassedCycles=10',
        'accepted Gate 17',
        'do not retry LOAD'
    )) {
        Assert-True ($Doc.Contains($Token)) "Gate 18 documentation missing: $Token"
    }

    foreach ($Token in @(
        'test_gate18_cr3_residency_roundtrip_10cycles_script_contracts',
        'TestGate18Cr3ResidencyRoundtrip10CyclesScript.ps1',
        'gate18-cr3-residency-roundtrip-10cycles.ps1'
    )) {
        Assert-True ($CMake.Contains($Token)) "Gate 18 CMake missing: $Token"
    }
}

$Source = [IO.File]::ReadAllText((Resolve-Path $ScriptPath))
$Gate17 = [IO.File]::ReadAllText((Resolve-Path $Gate17Path))
$Doc = [IO.File]::ReadAllText((Resolve-Path $DocPath))
$CMake = [IO.File]::ReadAllText((Resolve-Path $TestsCMake))

foreach ($Token in @(
    '''Result=PASS''',
    '''ProbeStage=COMPLETE''',
    '''Residency=0/1/0''',
    '''Matched=4294967295/0/4294967295''',
    '''Writes=1/1/1''',
    'exit 1'
)) {
    Assert-True ($Gate17.Contains($Token)) "Gate 17 no longer proves: $Token"
}

Verify $Source $Doc $CMake

$Mutations = @(
    $Source.Replace('$TotalCycles = 10', '$TotalCycles = 1'),
    $Source.Replace(
        '$Gate17Script = Join-Path $Stage ''gate17-cr3-residency-roundtrip.ps1''',
        '$Gate17Script = Join-Path $Stage ''gate14-cr3-probe-diagnostic.ps1'''),
    $Source.Replace('$GroupCount -ne 2', '$false'),
    $Source.Replace('$Recovery.DebugInfoType -ne 2', '$false'),
    $Source.Replace('$BeforeService.ExitCode -ne 1060', '$false'),
    $Source.Replace('$Process.ExitCode -ne 0', '$false'),
    $Source.Replace('$InnerSummaries.Count -ne 1', '$false'),
    $Source.Replace('''Result=PASS''', '''Result=FAIL'''),
    $Source.Replace('''Residency=0/1/0''', '''Residency=0/0/0'''),
    $Source.Replace('''BeforeTransitions=1''', '''BeforeTransitions=0'''),
    $Source.Replace('$AfterService.ExitCode -ne 1060', '$false'),
    $Source.Replace('$_.Id -eq 41', '$_.Id -eq 566'),
    $Source.Replace('$_.Id -eq 1000', '$_.Id -eq 1001'),
    $Source.Replace('$FailureEvents.Count -ne 0', '$false'),
    $Source.Replace(
        '$Dump.LastWriteTimeUtc -gt $CycleStart.ToUniversalTime()',
        '$false'),
    $Source.Replace(
        'Stop-Transcript | Out-Null',
        '$TranscriptStarted = $false'),
    $Source.Replace(
        'tar.exe -a -c -f $ZipPath',
        'tar.exe -tf $ZipPath')
)

foreach ($Mutation in $Mutations) {
    Assert-True ($Mutation -ne $Source) 'Gate 18 vacuous mutation'
    $Rejected = $false
    try {
        Verify $Mutation $Doc $CMake
    }
    catch {
        $Rejected = $true
    }
    Assert-True $Rejected 'Gate 18 accepted a safety mutation'
}

Write-Host 'PASS: Gate 18 repeats the exact Gate 17 residency transaction 10 times with fresh-state, topology, crash, service, transcript, and archive gates.'
