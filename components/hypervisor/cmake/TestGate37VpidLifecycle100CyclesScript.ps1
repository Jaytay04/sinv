param(
    [Parameter(Mandatory=$true)][string]$ScriptPath,
    [Parameter(Mandatory=$true)][string]$Gate36Path,
    [Parameter(Mandatory=$true)][string]$DocPath,
    [Parameter(Mandatory=$true)][string]$RoadmapPath,
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

function Verify(
    [string]$Source,
    [string]$Doc,
    [string]$Roadmap,
    [string]$CMake
) {
    $Tokens = $null
    $Errors = $null
    [void][Management.Automation.Language.Parser]::ParseInput(
        $Source, [ref]$Tokens, [ref]$Errors)
    Assert-True ($Errors.Count -eq 0) 'Gate 37 script does not parse'

    foreach ($Token in @(
        '$TotalCycles = 100',
        '$Driver = Join-Path $Stage ''hypervisor_driver.sys''',
        '$Gate36Script = Join-Path $Stage ''gate36-vpid-activation.ps1''',
        '$Gate37Script = Join-Path $Stage ''gate37-vpid-lifecycle-100cycles.ps1''',
        'Gate37ProcessorGroups',
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
        '[string]::IsNullOrWhiteSpace($RunnerStderrText)',
        '$InnerSummaries.Count -ne 1',
        '''Result=PASS''',
        '''Probe=VPID_ACTIVATION''',
        '''Processors=4''',
        '''Groups=2''',
        '''Vpids=1,2,3,4''',
        '''LiveVmcsVpid=VERIFIED''',
        '''InvvpidBeforeActivation=VERIFIED_BY_RUNTIME''',
        '''InvvpidBeforeReuse=VERIFIED_BY_UNLOAD''',
        '''ServiceAfterUnload=ABSENT''',
        '$VpidOutputs.Count -ne 1',
        'index=0 group=0 number=0 vpid=1 observed=1',
        'index=1 group=0 number=1 vpid=2 observed=2',
        'index=2 group=1 number=0 vpid=3 observed=3',
        'index=3 group=1 number=1 vpid=4 observed=4',
        'SUCCESS: Exact ACTIVE VPID state verified on 4 processors.',
        '$InnerArchives.Count -ne 1',
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
        Assert-True ($Source.Contains($Token)) "Gate 37 missing token: $Token"
    }

    Assert-Ordered $Source @(
        '$BeforeService.ExitCode -ne 1060',
        'Start-Process @StartArguments',
        '$Process.ExitCode -ne 0',
        '[string]::IsNullOrWhiteSpace($RunnerStderrText)',
        '$InnerSummaries.Count -ne 1',
        '''Result=PASS''',
        '''Probe=VPID_ACTIVATION''',
        '''Vpids=1,2,3,4''',
        '''LiveVmcsVpid=VERIFIED''',
        '''InvvpidBeforeActivation=VERIFIED_BY_RUNTIME''',
        '''InvvpidBeforeReuse=VERIFIED_BY_UNLOAD''',
        '$VpidOutputs.Count -ne 1',
        'index=0 group=0 number=0 vpid=1 observed=1',
        'index=3 group=1 number=1 vpid=4 observed=4',
        '$InnerArchives.Count -ne 1',
        '$AfterService.ExitCode -ne 1060',
        '$FailureEvents.Count -ne 0',
        '$Dump.LastWriteTimeUtc -gt $CycleStart.ToUniversalTime()',
        '$PassedCycles++'
    ) 'Gate 37 cycle'

    Assert-True ($Source -notmatch
        '(?im)(?:&\s*\$Loader\b|FilePath\s*=\s*\$Loader\b)') 'Gate 37 invokes the loader directly instead of the accepted Gate 36 transaction'

    Assert-True ($Source -notmatch
        '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') 'Gate 37 bypasses Gate 36 or mutates signing, boot, SCM, or reboot state'

    foreach ($Token in @(
        'Gate 37: 100-cycle VPID lifecycle qualification',
        'gate37-vpid-lifecycle-100cycles.ps1',
        'PassedCycles=100',
        'accepted Gate 36',
        'do not retry LOAD'
    )) {
        Assert-True ($Doc.Contains($Token)) "Gate 37 documentation missing: $Token"
    }

    foreach ($Token in @(
        '| V4F Live activation | Complete',
        '| V4G Lifecycle qualification | Complete',
        'Gate 36 evidence',
        'Gate 37'
    )) {
        Assert-True ($Roadmap.Contains($Token)) "Runtime V4 roadmap missing: $Token"
    }

    foreach ($Token in @(
        'test_gate37_vpid_lifecycle_100cycles_script_contracts',
        'TestGate37VpidLifecycle100CyclesScript.ps1',
        'gate37-vpid-lifecycle-100cycles.ps1'
    )) {
        Assert-True ($CMake.Contains($Token)) "Gate 37 CMake missing: $Token"
    }
}

$Source = [IO.File]::ReadAllText((Resolve-Path $ScriptPath))
$Gate36 = [IO.File]::ReadAllText((Resolve-Path $Gate36Path))
$Doc = [IO.File]::ReadAllText((Resolve-Path $DocPath))
$Roadmap = [IO.File]::ReadAllText((Resolve-Path $RoadmapPath))
$CMake = [IO.File]::ReadAllText((Resolve-Path $TestsCMake))

foreach ($Token in @(
    '''Result=PASS''',
    '''Probe=VPID_ACTIVATION''',
    '''Processors=4''',
    '''Groups=2''',
    '''Vpids=1,2,3,4''',
    '''LiveVmcsVpid=VERIFIED''',
    '''InvvpidBeforeActivation=VERIFIED_BY_RUNTIME''',
    '''InvvpidBeforeReuse=VERIFIED_BY_UNLOAD''',
    '''ServiceAfterUnload=ABSENT''',
    'exit 1'
)) {
    Assert-True ($Gate36.Contains($Token)) "Gate 36 no longer proves: $Token"
}

Verify $Source $Doc $Roadmap $CMake

$Mutations = @(
    $Source.Replace('$TotalCycles = 100', '$TotalCycles = 1'),
    $Source.Replace(
        '$Gate36Script = Join-Path $Stage ''gate36-vpid-activation.ps1''',
        '$Gate36Script = Join-Path $Stage ''gate35-release-candidate-lifecycle.ps1'''),
    $Source.Replace('$GroupCount -ne 2', '$false'),
    $Source.Replace('$Recovery.DebugInfoType -ne 2', '$false'),
    $Source.Replace('$BeforeService.ExitCode -ne 1060', '$false'),
    $Source.Replace('$Process.ExitCode -ne 0', '$false'),
    $Source.Replace(
        '[string]::IsNullOrWhiteSpace($RunnerStderrText)',
        '$true'),
    $Source.Replace('$InnerSummaries.Count -ne 1', '$false'),
    $Source.Replace('''Result=PASS''', '''Result=FAIL'''),
    $Source.Replace('''Probe=VPID_ACTIVATION''', '''Probe=VPID_DISABLED'''),
    $Source.Replace('''Vpids=1,2,3,4''', '''Vpids=0,0,0,0'''),
    $Source.Replace('''LiveVmcsVpid=VERIFIED''', '''LiveVmcsVpid=UNVERIFIED'''),
    $Source.Replace(
        '''InvvpidBeforeReuse=VERIFIED_BY_UNLOAD''',
        '''InvvpidBeforeReuse=UNVERIFIED'''),
    $Source.Replace('$VpidOutputs.Count -ne 1', '$false'),
    $Source.Replace(
        'index=3 group=1 number=1 vpid=4 observed=4',
        'index=3 group=1 number=1 vpid=4 observed=0'),
    $Source.Replace('$InnerArchives.Count -ne 1', '$false'),
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
    Assert-True ($Mutation -ne $Source) 'Gate 37 vacuous mutation'
    $Rejected = $false
    try {
        Verify $Mutation $Doc $Roadmap $CMake
    }
    catch {
        $Rejected = $true
    }
    Assert-True $Rejected 'Gate 37 accepted a safety mutation'
}

Write-Host 'PASS: Gate 37 repeats the exact accepted Gate 36 VPID transaction 100 times with immutable artifacts, exact per-CPU VPID proof, fresh service state, crash checks, transcripts, and nested archives.'
